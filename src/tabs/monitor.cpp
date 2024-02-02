//
// Copyright 2021 - 2022 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <utility/skif_imgui.h>
#include <utility/utility.h>

#include <utility/injection.h>
#include <utility/fsutil.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>

#include <filesystem>
#include <iostream>
#include <set>
#include <map>
#include <unordered_map>
#include <stack>

#include <Windows.h>
#include <wmsdk.h>
#include <Tlhelp32.h>
#include <appmodel.h>
#include <psapi.h>

#include <typeindex>
#include <tabs/common_ui.h>

#include <utility/registry.h>

CONDITION_VARIABLE ProcRefreshPaused = { };

enum class SK_RenderAPI
{
  Reserved  = 0x0001u,

  // Native API Implementations
  OpenGL    = 0x0002u,
  Vulkan    = 0x0004u,
  D3D9      = 0x0008u,
  D3D9Ex    = 0x0018u,
  D3D10     = 0x0020u, // Don't care
  D3D11     = 0x0040u,
  D3D12     = 0x0080u,

  // These aren't native, but we need the bitmask anyway
  D3D8      = 0x2000u,
  DDraw     = 0x4000u,
  Glide     = 0x8000u,

  // Wrapped APIs (D3D12 Flavor)
  D3D8On12  = 0x2080u,
  DDrawOn12 = 0x4080u,
  GlideOn12 = 0x8080u,
//D3D11On12 = 0x00C0u,

  // Wrapped APIs (D3D11 Flavor)
  D3D8On11  = 0x2040u,
  DDrawOn11 = 0x4040u,
  GlideOn11 = 0x8040u,
};

#define MAX_INJECTED_PROCS        32
#define MAX_INJECTED_PROC_HISTORY 128
#define INJECTION_RECORD_ABI_VER  "1.2"

extern "C"
{
struct SK_InjectionRecord_s
{
  struct {
    wchar_t      name  [MAX_PATH + 2] = { 0 };
    DWORD        id                   =   0  ;
    __time64_t   inject               =  0ULL;
    __time64_t   eject                =  0ULL;
    bool         crashed              = false;
    wchar_t      win_title [     128] = { 0 };
  } process;

  struct {
    ULONG64    frames                    =  0ULL;
    SK_RenderAPI api                       = SK_RenderAPI::Reserved;
    bool         fullscreen                = false;
    bool         dpi_aware                 = false;
  } render;

  // Use a bitmask instead of this stupidness
  struct {
    bool         xinput                    = false;
    bool         raw_input                 = false;
    bool         direct_input              = false;
    bool         hid                       = false;
    bool         steam                     = false;
  } input;

  struct {
    uint32_t     steam_appid     =  0 ;
    wchar_t      uwp_full_name [
             PACKAGE_FULL_NAME_MAX_LENGTH
                               ] = { };
    // Others?
  } platform;

  static volatile LONG count;
  static volatile LONG rollovers;
};
};

using SKX_GetInjectedPIDs_pfn   = size_t                (__stdcall *)(DWORD *pdwList, size_t capacity);
using SK_Inject_GetRecord_pfn   = SK_InjectionRecord_s* (__stdcall *)(DWORD  dwPid);
using SK_Inject_AuditRecord_pfn = HRESULT               (__stdcall *)(DWORD  dwPid, SK_InjectionRecord_s* pData, size_t cbSize);


#pragma pack (push,8)
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-erref/596a1078-e883-4972-9bbc-49e60bebca55
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_INFO_LENGTH_MISMATCH      ((NTSTATUS)0xC0000004L)
#define STATUS_INSUFFICIENT_RESOURCES    ((NTSTATUS)0xC000009AL)
#define STATUS_BUFFER_OVERFLOW           ((NTSTATUS)0x80000005L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)

#define SystemHandleInformationSize 0x200000

static HMODULE _NtDll =
  GetModuleHandle (L"NtDll");

#define SKIF_NameOf(x) #x
#define SKIF_GetNtDllProcAddress(proc) GetProcAddress(_NtDll,proc)

template <typename _T>
__declspec (noinline)
void SKIF_ImportFromNtDll_Impl (_T& pfn, const char* name)
{
  pfn =
    reinterpret_cast <_T> (
      SKIF_GetNtDllProcAddress (name)
    );
}

#define SKIF_ImportFromNtDll(proc) \
 SKIF_ImportFromNtDll_Impl(  proc,#proc )


enum SKIF_PROCESS_INFORMATION_CLASS
{
    ProcessBasicInformation                     = 0x00, //  0
    ProcessDebugPort                            = 0x07, //  7
    ProcessWow64Information                     = 0x1A, // 26
    ProcessImageFileName                        = 0x1B, // 27
    ProcessBreakOnTermination                   = 0x1D, // 29
    ProcessSubsystemInformation                 = 0x4B  // 75
};

enum SYSTEM_INFORMATION_CLASS
{ SystemBasicInformation                = 0,
  SystemPerformanceInformation          = 2,
  SystemTimeOfDayInformation            = 3,
  SystemProcessInformation              = 5,
  SystemProcessorPerformanceInformation = 8,
  SystemHandleInformation               = 16, // 0x10
  SystemInterruptInformation            = 23,
  SystemExceptionInformation            = 33,
  SystemRegistryQuotaInformation        = 37,
  SystemLookasideInformation            = 45,
  SystemExtendedHandleInformation       = 64, // 0x40
  SystemCodeIntegrityInformation        = 103,
  SystemPolicyInformation               = 134,
};

typedef enum _OBJECT_INFORMATION_CLASS {
  ObjectBasicInformation,
  ObjectNameInformation,
  ObjectTypeInformation,
  ObjectTypesInformation,
  ObjectHandleFlagInformation,
  ObjectSessionInformation,
} OBJECT_INFORMATION_CLASS;

using NtResumeProcess_pfn =
  NTSTATUS (NTAPI *)(
       IN  HANDLE                    Handle
  );

using NtSuspendProcess_pfn =
  NTSTATUS (NTAPI *)(
       IN  HANDLE                    Handle
  );

using NtQueryInformationProcess_pfn =
  NTSTATUS (NTAPI *)(
       IN  HANDLE                    Handle,
       IN  SKIF_PROCESS_INFORMATION_CLASS ProcessInformationClass, // PROCESSINFOCLASS 
       OUT PVOID                     ProcessInformation,
       IN  ULONG                     ProcessInformationLength,
       OUT PULONG                    ReturnLength OPTIONAL
  );

using NtQuerySystemInformation_pfn =
  NTSTATUS (NTAPI *)(
        IN  SYSTEM_INFORMATION_CLASS SystemInformationClass,
        OUT PVOID                    SystemInformation,
        IN  ULONG                    SystemInformationLength,
        OUT PULONG                   ReturnLength OPTIONAL
  );

using NtQueryObject_pfn =
  NTSTATUS (NTAPI *)(
       IN  HANDLE                   Handle       OPTIONAL,
       IN  OBJECT_INFORMATION_CLASS ObjectInformationClass,
       OUT PVOID                    ObjectInformation,
       IN  ULONG                    ObjectInformationLength,
       OUT PULONG                   ReturnLength OPTIONAL
  );

using NtDuplicateObject_pfn =
  NTSTATUS (NTAPI *)(
        IN  HANDLE      SourceProcessHandle,
        IN  HANDLE      SourceHandle,
        OUT HANDLE      TargetProcessHandle,
        OUT PHANDLE     TargetHandle,
        IN  ACCESS_MASK DesiredAccess OPTIONAL,
        IN  ULONG       Attributes    OPTIONAL,
        IN  ULONG       Options       OPTIONAL
  );

NtQueryInformationProcess_pfn NtQueryInformationProcess = nullptr;
NtQuerySystemInformation_pfn  NtQuerySystemInformation  = nullptr;
NtDuplicateObject_pfn         NtDuplicateObject         = nullptr;
NtQueryObject_pfn             NtQueryObject             = nullptr;
NtSuspendProcess_pfn          NtSuspendProcess          = nullptr;
NtResumeProcess_pfn           NtResumeProcess           = nullptr;

typedef struct _SK_SYSTEM_HANDLE_TABLE_ENTRY_INFO
{
  USHORT UniqueProcessId;
  USHORT CreatorBackTraceIndex;
  UCHAR  ObjectTypeIndex;
  UCHAR  HandleAttributes;
  USHORT HandleValue;
  PVOID  Object;
  ULONG  GrantedAccess;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO,
*PSYSTEM_HANDLE_TABLE_ENTRY_INFO;

typedef struct _SK_SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX
{
  PVOID       Object;
  union
  {
    ULONG_PTR UniqueProcessId;

    struct
    {
#ifdef _WIN64
      DWORD   ProcessId;
      DWORD   ThreadId; // ?? ( What are the upper-bits for anyway ? )
#else
      WORD    ProcessId;
      WORD    ThreadId; // ?? ( What are the upper-bits for anyway ? )
#endif
    };
  };

  union
  {
    ULONG_PTR HandleValue;
    HANDLE    Handle;
  };

  ULONG       GrantedAccess;
  USHORT      CreatorBackTraceIndex;
  USHORT      ObjectTypeIndex;
  ULONG       HandleAttributes;
  ULONG       Reserved;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX,
*PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SK_SYSTEM_HANDLE_INFORMATION
{ ULONG                          NumberOfHandles;
  SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles     [1];
} SYSTEM_HANDLE_INFORMATION,
 *PSYSTEM_HANDLE_INFORMATION;

typedef struct _SK_SYSTEM_HANDLE_INFORMATION_EX
{ ULONG_PTR                         NumberOfHandles;
  ULONG_PTR                         Reserved;
  SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles     [1];
} SYSTEM_HANDLE_INFORMATION_EX,
*PSYSTEM_HANDLE_INFORMATION_EX;

typedef struct _SK_UNICODE_STRING
{ USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} UNICODE_STRING,
*PUNICODE_STRING;

typedef const UNICODE_STRING 
           *PCUNICODE_STRING;

typedef struct _SK_PUBLIC_OBJECT_TYPE_INFORMATION
{ UNICODE_STRING TypeName;
  ULONG          Reserved [22];    // reserved for internal use
} PUBLIC_OBJECT_TYPE_INFORMATION,
*PPUBLIC_OBJECT_TYPE_INFORMATION;

typedef struct _SK_OBJECT_NAME_INFORMATION
{ UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION,
*POBJECT_NAME_INFORMATION;
#pragma pack (pop)

using _ByteArray = std::vector <uint8_t>;

std::string   SKIF_PresentDebugStr [2];
volatile LONG SKIF_PresentIdx    =   0;

// Required to check suspended processes
typedef LONG       KPRIORITY;
typedef struct _PROCESS_BASIC_INFORMATION {
  NTSTATUS ExitStatus;
  PVOID PebBaseAddress;
  ULONG_PTR AffinityMask;
  KPRIORITY BasePriority;
  ULONG_PTR UniqueProcessId;
  ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION;

typedef struct _PROCESS_EXTENDED_BASIC_INFORMATION {
    SIZE_T Size;    // Ignored as input, written with structure size on output
    PROCESS_BASIC_INFORMATION BasicInfo;
    union {
        ULONG Flags;
        struct {
            ULONG IsProtectedProcess : 1;
            ULONG IsWow64Process : 1;
            ULONG IsProcessDeleting : 1;
            ULONG IsCrossSessionCreate : 1;
            ULONG IsFrozen : 1;
            ULONG IsBackground : 1;
            ULONG IsStronglyNamed : 1;
            ULONG IsSecureProcess : 1;
            ULONG IsSubsystemProcess : 1;
            ULONG SpareBits : 23;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
} PROCESS_EXTENDED_BASIC_INFORMATION, *PPROCESS_EXTENDED_BASIC_INFORMATION;



// CC BY-SA 4.0: https://stackoverflow.com/a/59908355
std::map<std::wstring, std::wstring> GetDosPathDevicePathMap()
{
  // It's not really related to MAX_PATH, but I guess it should be enough.
  // Though the docs say "The first null-terminated string stored into the buffer is the current mapping for the device.
  //                      The other null-terminated strings represent undeleted prior mappings for the device."
  wchar_t devicePath[MAX_PATH + 2] = { 0 };
  std::map<std::wstring, std::wstring> result;
  std::wstring dosPath = L"A:";

  for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
  {
    dosPath[0] = letter;
    if (QueryDosDeviceW (dosPath.c_str(), devicePath, MAX_PATH)) // may want to properly handle errors instead ... e.g. check ERROR_INSUFFICIENT_BUFFER
    {
      result[dosPath] = std::wstring(devicePath) + LR"(\)";
    }
  }
  return result;
}

// MIT: https://github.com/antonioCoco/ConPtyShell/blob/master/ConPtyShell.cs
typedef struct _OBJECT_TYPE_INFORMATION_V2 {
  UNICODE_STRING TypeName;
  ULONG TotalNumberOfObjects;
  ULONG TotalNumberOfHandles;
  ULONG TotalPagedPoolUsage;
  ULONG TotalNonPagedPoolUsage;
  ULONG TotalNamePoolUsage;
  ULONG TotalHandleTableUsage;
  ULONG HighWaterNumberOfObjects;
  ULONG HighWaterNumberOfHandles;
  ULONG HighWaterPagedPoolUsage;
  ULONG HighWaterNonPagedPoolUsage;
  ULONG HighWaterNamePoolUsage;
  ULONG HighWaterHandleTableUsage;
  ULONG InvalidAttributes;
  GENERIC_MAPPING GenericMapping;
  ULONG ValidAccessMask;
  BOOLEAN SecurityRequired;
  BOOLEAN MaintainHandleCount;
  UCHAR TypeIndex;   // Added in V2
  CHAR ReservedByte; // Added in V2
  ULONG PoolType;
  ULONG DefaultPagedPoolCharge;
  ULONG DefaultNonPagedPoolCharge;
} OBJECT_TYPE_INFORMATION_V2, *POBJECT_TYPE_INFORMATION_V2;

// CC BY-SA 3.0: https://stackoverflow.com/a/39104745
typedef struct _OBJECT_TYPES_INFORMATION {
  LONG NumberOfTypes;
} OBJECT_TYPES_INFORMATION, *POBJECT_TYPES_INFORMATION;

/* Unused
SK_Inject_AuditRecord_pfn
SK_Inject_AuditRecord = nullptr;

SK_Inject_GetRecord_pfn
SK_Inject_GetRecord   = nullptr;
*/

// Some string manipulation to assume whether a process is an Xbox app or not
bool SKIF_Debug_IsXboxApp(std::string path, std::string processName)
{
  // Does the string contain "\Content\" ?
  if (path.find(R"(\Content\)") != std::string::npos)
    return true;

  // Does the string contain "XboxGames" ?
  if (path.find("XboxGames")    != std::string::npos)
    return true;

  // Does the string contain "Xbox Games" ?
  if (path.find("Xbox Games")   != std::string::npos)
    return true;

  // Legacy stuff that's disabled since they match non-games as well
#if 0
  // Does the string contain "WindowsApps" ?
  if (path.find("WindowsApps") != std::string::npos)
    return true;

  // If the string does not contain any of the above,
  //   do some string manipulation and assumptions
  if (path.length() > 0)
  {
    // \Device\HarddiskVolume21\Hades.exe

    std::string p1 = R"(\Device\HarddiskVolume)";
    if (path.find(p1) != std::string::npos)
      path.erase(path.find(p1), p1.length());

    // 21\Hades.exe

    std::string p2 = R"(\)" + processName;
    if (path.find(p2) != std::string::npos)
      path.erase(path.find(p2), p2.length());

    // 21

    if (strlen(path.c_str()) < 3) // 0 - 99
      return true;
  }
#endif

  return false;
}

// Some string manipulation to assume whether a process is a Steam app or not
bool SKIF_Debug_IsSteamApp(std::string path, std::string processName)
{
  return (path.find("SteamApps") != std::string::npos);
}

// CC BY-SA 3.0: https://stackoverflow.com/a/39104745
//               https://jadro-windows.cz/download/ntqueryobject.zip
#define ALIGN_DOWN(Length, Type)       ((ULONG)(Length) & ~(sizeof(Type) - 1))
#define ALIGN_UP(Length, Type)         (ALIGN_DOWN(((ULONG)(Length) + sizeof(Type) - 1), Type))

// CC BY-SA 3.0: https://stackoverflow.com/a/39104745
//               https://jadro-windows.cz/download/ntqueryobject.zip
// Modified to use POBJECT_TYPE_INFORMATION_V2 instead of POBJECT_TYPE_INFORMATION
USHORT GetTypeIndexByName (std::wstring TypeName)
{
  POBJECT_TYPE_INFORMATION_V2  TypeInfo = NULL;
  POBJECT_TYPES_INFORMATION   TypesInfo = NULL;
  USHORT   ret                          = USHRT_MAX;
  NTSTATUS ntStatTypesInfo;
  ULONG    BufferLength (28);

  do
  {
    if (TypesInfo != NULL)
      free(TypesInfo);

    TypesInfo = (POBJECT_TYPES_INFORMATION)calloc(BufferLength, sizeof(POBJECT_TYPES_INFORMATION));

    ntStatTypesInfo = NtQueryObject(NULL, ObjectTypesInformation, TypesInfo, BufferLength, &BufferLength);
  } while (ntStatTypesInfo == STATUS_INFO_LENGTH_MISMATCH);

  if (NT_SUCCESS (ntStatTypesInfo) && TypesInfo->NumberOfTypes > 0)
  {
    PLOG_VERBOSE << "Number of Types: " << std::to_wstring (TypesInfo->NumberOfTypes);

    // Align to first element of the array
    TypeInfo = (POBJECT_TYPE_INFORMATION_V2)((PCHAR)TypesInfo + ALIGN_UP(sizeof(*TypesInfo), ULONG_PTR));
    for (int i = 0; i < TypesInfo->NumberOfTypes; i++)
    {
      //USHORT     _TypeIndex = i + 2;               // OBJECT_TYPE_INFORMATION requires adding 2 to get the proper type index
      USHORT       _TypeIndex = TypeInfo->TypeIndex; // OBJECT_TYPE_INFORMATION_V2 includes it in the struct
      std::wstring _TypeName  = std::wstring(TypeInfo->TypeName.Buffer, TypeInfo->TypeName.Length / sizeof(WCHAR));

      if (TypeName == _TypeName)
      {
        PLOG_VERBOSE << std::to_wstring(TypeInfo->TypeIndex) << " - " << _TypeName;

        ret = _TypeIndex;
        break;
      }

      // Align to the next element of the array
      TypeInfo = (POBJECT_TYPE_INFORMATION_V2)((PCHAR)(TypeInfo + 1) + ALIGN_UP(TypeInfo->TypeName.MaximumLength, ULONG_PTR));
    }
  }

  // Free up the memory that was allocated by calloc
  if (TypesInfo != NULL)
    free (TypesInfo);

  if (ret == USHRT_MAX)
    PLOG_ERROR << "Failed to locate TypeIndex for Events!";

  return ret;
}

enum inject_policy {
  Blacklist,
  DontCare,
  Whitelist
};

struct standby_record_s {
  DWORD         pid;
  std::wstring  filename;
  std::wstring  path;
  std::string   pathUTF8;
  std::string   tooltip;
  std::string   details;
  std::string   arch;
  bool          admin;
  int           status = 255; // 1 - Active Global    2 - Local   3 - Inert   254 - Stuck?      255 - Unknown
  inject_policy policy = DontCare;
  int           sortedBy  = -1;    // Only first item is ever read/written to
  bool          sortedAsc = false; // Only first item is ever read/written to
};

void SortProcesses (std::vector <standby_record_s> &processes)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // Always sort ascending
  auto _SortByStatus = [&](const standby_record_s &a, const standby_record_s& b) -> bool
  {
    //if (_registry.bProcessSortAscending)
      return a.status < b.status;

    //return a.status > b.status;
  };

  auto _SortByPID = [&](const standby_record_s &a, const standby_record_s& b) -> bool
  {
    if (_registry.bProcessSortAscending)
      return a.pid < b.pid;

    return a.pid > b.pid;
  };

  auto _SortByArch = [&](const standby_record_s &a, const standby_record_s& b) -> bool
  {
    if (_registry.bProcessSortAscending)
      return a.arch < b.arch;

    return a.arch > b.arch;
  };

  auto _SortByAdmin = [&](const standby_record_s &a, const standby_record_s& b) -> bool
  {
    if (_registry.bProcessSortAscending)
      return a.admin < b.admin;

    return a.admin > b.admin;
  };

  auto _SortByName = [&](const standby_record_s &a, const standby_record_s& b) -> bool
  {
    // Need to transform to lowercase
    std::wstring la = SKIF_Util_ToLowerW(a.filename),
                 lb = SKIF_Util_ToLowerW(b.filename);

    if (_registry.bProcessSortAscending)
      return la < lb;

    return la > lb;
  };

  if (! processes.empty())
  {
    if (_registry.iProcessSort == 0)      // Status
      std::sort (processes.begin(), processes.end(), _SortByStatus);
    else if (_registry.iProcessSort == 1) // PID
      std::sort (processes.begin(), processes.end(), _SortByPID);
    else if (_registry.iProcessSort == 2) // Arch
      std::sort (processes.begin(), processes.end(), _SortByArch);
    else if (_registry.iProcessSort == 3) // Admin
      std::sort (processes.begin(), processes.end(), _SortByAdmin);
    else if (_registry.iProcessSort == 4) // Process Name
      std::sort (processes.begin(), processes.end(), _SortByName);
    else                             // Status (in case someone messes with the registry)
      std::sort (processes.begin(), processes.end(), _SortByStatus);

    processes[0].sortedBy  = _registry.iProcessSort;
    processes[0].sortedAsc = _registry.bProcessSortAscending;
  }
}

void
SKIF_UI_Tab_DrawMonitor (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  extern float SKIF_ImGui_GlobalDPIScale;
  static std::atomic<DWORD> refreshIntervalInMsec;

  static bool setInitialRefreshInterval = true;
  if (setInitialRefreshInterval)
  {   setInitialRefreshInterval = false;
    if (     _registry.iProcessRefreshInterval == 0) // Paused
      refreshIntervalInMsec.store(0);
    else if (_registry.iProcessRefreshInterval == 1) // Slow (5s)
      refreshIntervalInMsec.store(5000);
    else if (_registry.iProcessRefreshInterval == 2) // Normal (1s)
      refreshIntervalInMsec.store(1000);
    else                                             // Treat everything else as High (0.5s)
      refreshIntervalInMsec.store(500);

    InitializeConditionVariable (&ProcRefreshPaused);
  }
  
  if (SKIF_Tab_Selected != UITab_Monitor && _registry.iProcessRefreshInterval != 0)
    WakeConditionVariable (&ProcRefreshPaused);

  SKIF_Tab_Selected = UITab_Monitor;
  if (SKIF_Tab_ChangeTo == UITab_Monitor)
      SKIF_Tab_ChangeTo  = UITab_None;

  // Triple-buffer updates so we can go lock-free
  //
  struct injection_snapshot_s {
    //std::map <DWORD, standby_record_s> Processes;
    std::vector <standby_record_s> Processes;
    DWORD dwPIDs [MAX_INJECTED_PROCS] = { };
  } static snapshots [3];

  //static volatile LONG snapshot_idx = 0;
  static std::atomic<int> snapshot_idx_reading = 0,
                          snapshot_idx_written = 0;

  // Always read from the last written index
  int nowReading = snapshot_idx_written.load ( );
  snapshot_idx_reading.store (nowReading);

  auto& snapshot =
    snapshots [nowReading];
    //snapshots [ReadAcquire (&snapshot_idx)];

  auto&      processes = snapshot.Processes;

  SKIF_ImGui_Spacing      ( );

  SKIF_ImGui_Columns      (2, nullptr, true);

  SK_RunOnce (
    ImGui::SetColumnWidth (0, 560.0f * SKIF_ImGui_GlobalDPIScale)
  );

  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  ImGui::TextWrapped      ("Use the below list to identify injected processes using the " ICON_FA_CIRCLE " status indicator to the left."
                           " Note that while a lot of processes might have Special K injected in them, only those that are whitelisted"
                           " and uses a render API will see Special K active. Remaning processes have it inert and can"
                           " be ignored."
  );

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );

  if (ImGui::Checkbox ("Show remaining processes",  &_registry.bProcessIncludeAll))
    _registry.regKVProcessIncludeAll.putData        (_registry.bProcessIncludeAll);

  SKIF_ImGui_SetHoverTip ("If this is enabled the below list will also include uninjected processes.\n"
                          "This is indicated by the lack of a " ICON_FA_CIRCLE " icon under the Status column.");
  
  ImGui::EndGroup         ( );

  ImGui::SameLine         ( );

  ImGui::BeginGroup       ( );
  ImGui::TreePush         ( );

  const char* RefreshInterval[] = { "Paused",   // 0 (never)
                                    "Slow",     // 1 (5s)
                                    "Normal",   // 2 (1s)
                                    "High"      // 3 (0.5s; not implemented)
  };
  static const char* RefreshIntervalCurrent = RefreshInterval[_registry.iProcessRefreshInterval];
          
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Update speed:"
  );

  ImGui::SameLine    ( );

  ImGui::SetNextItemWidth (150.0f * SKIF_ImGui_GlobalDPIScale);

  if (ImGui::BeginCombo ("###SKIF_iProcRefreshIntervalCombo", RefreshIntervalCurrent)) // The second parameter is the label previewed before opening the combo.
  {
    for (int n = 0; n < IM_ARRAYSIZE (RefreshInterval); n++)
    {
      bool is_selected = (RefreshIntervalCurrent == RefreshInterval[n]); // You can store your selection however you want, outside or inside your objects
      if (ImGui::Selectable (RefreshInterval[n], is_selected))
      {
        // Unpause the child thread that refreshes processes
        //if (_registry.iProcessRefreshInterval == 0 && n != 0)
        //  WakeConditionVariable (&ProcRefreshPaused);

        _registry.iProcessRefreshInterval = n;
        _registry.regKVProcessRefreshInterval.putData  (_registry.iProcessRefreshInterval);
        RefreshIntervalCurrent = RefreshInterval[_registry.iProcessRefreshInterval];
        
        if (     _registry.iProcessRefreshInterval == 0) // Paused
          refreshIntervalInMsec.store(0);
        else if (_registry.iProcessRefreshInterval == 1) // Slow (5s)
          refreshIntervalInMsec.store(5000);
        else if (_registry.iProcessRefreshInterval == 2) // Normal (1s)
          refreshIntervalInMsec.store(1000);
        else                                        // Treat everything else as High (0.5s)
          refreshIntervalInMsec.store(500);

        // Unpause the child thread that refreshes processes
        WakeConditionVariable (&ProcRefreshPaused);
      }
      if (is_selected)
          ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
    }
    ImGui::EndCombo  ( );
  }

  if (_registry.iProcessRefreshInterval == 0)
  {
    ImGui::SameLine         ( );
    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImColor::HSV (0.11F,   1.F, 1.F),
        ICON_FA_TRIANGLE_EXCLAMATION " ");
    ImGui::EndGroup         ( );

    SKIF_ImGui_SetHoverTip ("Real-time updates are paused.");
  }

  ImGui::TreePop          ( );
  ImGui::EndGroup         ( );

  ImGui::NewLine          ( );

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "Toggle Service:"
                            );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ("A forced stop of the service may sometime help in case of an issue,"
                            " even if the service is not currently running.");

  SKIF_ImGui_Spacing      ( );

  ImGui::TreePush   ( );

  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success));
  if (ImGui::Button ( ICON_FA_TOGGLE_ON  "  Force start", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, // ICON_FA_PLAY
                                                            30.0f * SKIF_ImGui_GlobalDPIScale )))
    _inject._StartStopInject (false, _registry.bStopOnInjection);
  ImGui::PopStyleColor ( );

  ImGui::SameLine   ( );
    
  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning));
  if (ImGui::Button ( ICON_FA_TOGGLE_OFF "  Force stop", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, // ICON_FA_STOP
                                                            30.0f * SKIF_ImGui_GlobalDPIScale )))
    _inject._StartStopInject(true);
  ImGui::PopStyleColor ( );
    
  SKIF_ImGui_Spacing      ( );

#ifdef _WIN64
  if ( _inject.SKVer64 >= L"21.08.12" &&
       _inject.SKVer32 >= L"21.08.12" )
#else
  if ( _inject.SKVer32 >= L"21.08.12" )
#endif
  {
    if (ImGui::Checkbox ("Stop automatically", &_registry.bStopOnInjection))
      _inject.ToggleStopOnInjection ( );

    ImGui::SameLine         ( );

    ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  ("This controls whether the configured auto-stop behavior (see Settings tab) should be used when the service is manually started.\n"
                            "Note that having this unchecked does not disable the auto-stop behavior if a game is launched without the service already running.");

    ImGui::SameLine         ( );
  }

  extern bool SKIF_ImGui_IconButton (ImGuiID id, std::string icon, std::string label, const ImVec4 & colIcon);
    
  if (SKIF_ImGui_IconButton (0x97848, ICON_FA_FOLDER_OPEN, "Install folder", ImColor(255, 207, 72)))
    SKIF_Util_ExplorePath (_path_cache.specialk_userdata);

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       (
    SK_WideCharToUTF8 (_path_cache.specialk_userdata).c_str ()
  );

  ImGui::TreePop    ( );

  ImGui::PopStyleColor    ( );

  ImGui::NextColumn       ( ); // Next Column

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Status:"
  );

  SKIF_ImGui_Spacing      ( );

  SKIF_UI_DrawPlatformStatus ( );

  ImGui::NewLine          ( );

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Components:"
  );

  SKIF_ImGui_Spacing      ( );
    
  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f)
                            );

  SKIF_UI_DrawComponentVersion ();

  ImGui::PopStyleColor    ( );

  ImGui::Columns          (1);

  ImGui::Spacing          ( );
  ImGui::Spacing          ( );

  //ImGui::Separator      ( );

  static
    std::once_flag init_ntdll;
  std::call_once ( init_ntdll, [](void)
  {
    SKIF_ImportFromNtDll (NtQueryInformationProcess);
    SKIF_ImportFromNtDll (NtQuerySystemInformation);
    SKIF_ImportFromNtDll (NtQueryObject);
    SKIF_ImportFromNtDll (NtDuplicateObject);
    SKIF_ImportFromNtDll (NtSuspendProcess);
    SKIF_ImportFromNtDll (NtResumeProcess);
  }              );

  static      USHORT EventIndex = GetTypeIndexByName (L"Event");

  if (EventIndex != USHRT_MAX)
  {
    static HANDLE hThread =
      CreateThread ( nullptr, 0x0,
        [](LPVOID)
      -> DWORD
      {
        CRITICAL_SECTION            ProcessRefreshJob = { };
        InitializeCriticalSection (&ProcessRefreshJob);
        EnterCriticalSection      (&ProcessRefreshJob);

        SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_ProcessRefreshJob");
        
        // Is this combo really appropriate for this thread?
        SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
        SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

        extern std::wstring SKIF_Util_GetProductName (const wchar_t* wszName);

        struct known_dll_s {
          std::wstring path;
          bool isSpecialK;
        };

        static std::vector<known_dll_s> knownDLLs;

        do
        {
          while (SKIF_Tab_Selected != UITab_Monitor || refreshIntervalInMsec.load() == 0)
          {
            SleepConditionVariableCS (
              &ProcRefreshPaused, &ProcessRefreshJob,
                INFINITE
            );
          }

          static int lastWritten = 0;
          int currReading        = snapshot_idx_reading.load ( );

          // This is some half-assed attempt of implementing triple-buffering where we don't overwrite our last finished snapshot.
          // If the main thread is currently reading from the next intended target, we skip that one as it means we have somehow
          //   managed to loop all the way around before the main thread started reading our last written result.
          int currWriting = (currReading == (lastWritten + 1) % 3)
                                          ? (lastWritten + 2) % 3  // Jump over very next one as it is currently being read from
                                          : (lastWritten + 1) % 3; // It is fine to write to the very next one

          //long idx =
            //( ReadAcquire (&snapshot_idx) + 1 ) % 2;

          auto &snapshot =
            snapshots [currWriting];

          auto& Processes      = snapshot.Processes;

          Processes.clear    ();

          using _PerProcessHandleMap =
            std::map        < DWORD,
                std::vector < SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX >
                                                                >;

          _PerProcessHandleMap
            handles_by_process;
          
          static HANDLE hProcessDst =
            SKIF_Util_GetCurrentProcess (); // Pseudo Handle
          static DWORD dwPidOfMe =
            GetCurrentProcessId         (); // Actual Pid

#pragma region Collect All Event Handles
          NTSTATUS ntStatusHandles;

          ULONG      handle_info_size ( SystemHandleInformationSize );
          _ByteArray handle_info_buffer;

          do
          {
            handle_info_buffer.resize (
                      handle_info_size );

            ntStatusHandles =
              NtQuerySystemInformation (
                SystemExtendedHandleInformation,
                  handle_info_buffer.data (),
                  handle_info_size,
                  &handle_info_size     );

          } while (ntStatusHandles == STATUS_INFO_LENGTH_MISMATCH);

          if (NT_SUCCESS (ntStatusHandles))
          {
            auto handleTableInformationEx =
              PSYSTEM_HANDLE_INFORMATION_EX (
                  handle_info_buffer.data ()
              );

            // Go through all handles of the system
            for ( unsigned int i = 0;
                                i < handleTableInformationEx->NumberOfHandles;
                                i++)
            {
              // Skip handles belong to SKIF
              if (handleTableInformationEx->Handles [i].ProcessId       == dwPidOfMe)
                continue;

              // If it is not the index that corresponds to Events, skip it
              if (handleTableInformationEx->Handles [i].ObjectTypeIndex != EventIndex)
                continue;

              // Add the remaining handles to the list of handles to go through
              handles_by_process [handleTableInformationEx->Handles [i].ProcessId]
                   .emplace_back (handleTableInformationEx->Handles [i]);
            }
          }

#pragma endregion

#pragma region Detect Special K Module and Handle (primary method)

          PROCESSENTRY32W pe32 = { };
          MODULEENTRY32W  me32 = { };

          SK_AutoHandle hProcessSnap (
            CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
          );

          if ((intptr_t)hProcessSnap.m_h > 0)
          {
            pe32.dwSize = sizeof (PROCESSENTRY32W);

            if (Process32FirstW (hProcessSnap, &pe32))
            {
              do
              {
                // Skip everything belonging to SKIF
                if (pe32.th32ProcessID == dwPidOfMe ||
                    pe32.th32ProcessID == 0)
                  continue;

                HANDLE hProcessSrc =
                    OpenProcess (
                        PROCESS_DUP_HANDLE | // Required to open handles (will fail on elevated processes)
                        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, // We don't actually need the additional stuff of PROCESS_QUERY_INFORMATION
                      pe32.th32ProcessID );

                // If we cannot open the process with PROCESS_DUP_HANDLE, it's probably because it's running elevated. Let's try without it
                if (! hProcessSrc)
                {
                  hProcessSrc =
                    OpenProcess (
                        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, // We don't actually need the additional stuff of PROCESS_QUERY_INFORMATION
                      pe32.th32ProcessID );
                }

                if (! hProcessSrc) continue;
                
                // Initialize a variable were we'll store all stuff in
                standby_record_s proc = standby_record_s{};

                SK_AutoHandle hModuleSnap (
                  CreateToolhelp32Snapshot (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pe32.th32ProcessID)
                );

                // Go through modules first (local injection)
                if ((intptr_t)hModuleSnap.m_h > 0)
                {
                  me32.dwSize = sizeof (MODULEENTRY32W);

                  if (Module32FirstW (hModuleSnap, &me32))
                  {
                    do
                    {
                      std::wstring moduleName = me32.szModule;

                      // Special K's global DLL files
                      if (StrStrIW (moduleName.c_str(), L"SpecialK32.dll") || 
                          StrStrIW (moduleName.c_str(), L"SpecialK64.dll"))
                      {
                        proc.status = 254; // Stuck?
                        // We'll keep checking the modules for a potential local injection
                      }

                      // Fallback of the fallback -- detect locally injected copies of SK!
                      else {
                        static std::wstring localDLLs[] = { // The small things matter -- array is sorted in the order of most expected
                          L"DXGI.dll",
                          L"D3D11.dll",
                          L"D3D9.dll",
                          L"OpenGL32.dll"
                          L"DInput8.dll",
                          L"D3D8.dll",
                          L"DDraw.dll",
                        };

                        for (auto& localDLL : localDLLs)
                        {
                          // Skip if it doesn't have the name of a local wrapper DLL
                          if (StrStrIW (moduleName.c_str(), localDLL.c_str()) == NULL)
                            continue;

                          // Skip system modules below \Windows\System32 and \Windows\SysWOW64
                          if (StrStrIW (me32.szExePath, LR"(\Windows\Sys)"))
                            continue;

                          bool isKnownDLL = false,
                               isSpecialK = false;

                          // Is it known?
                          for (auto& knownDLL : knownDLLs)
                          {
                            // We're dealing with a known DLL
                            if (knownDLL.path == me32.szExePath) //if (StrStrIW (me32.szExePath, knownDLL.path.c_str()))
                            {
                              //PLOG_VERBOSE << "Known DLL detected!";
                              isKnownDLL = true;
                              isSpecialK = knownDLL.isSpecialK;

                              // Skip checking the remaining known DLLs for a match
                              break;
                            }
                          }

                          if (isKnownDLL && isSpecialK)
                          {
                            proc.status = 2; // Local injection

                            // Skip checking the remaining local SK DLLs for this module
                            break;
                          }

                          // DLL file is not known -- let it be known
                          else {
                            std::wstring productName = SKIF_Util_GetProductName (me32.szExePath);

                            known_dll_s be_known = known_dll_s { };
                            be_known.path        = me32.szExePath;
                            be_known.isSpecialK  = StrStrIW (productName.c_str(), L"Special K");

                            knownDLLs.emplace_back (be_known);

                            PLOG_VERBOSE << "Unknown DLL detected, let it be known: " << me32.szExePath;
                            PLOG_VERBOSE << "DLL " << ((be_known.isSpecialK) ? "is" : "is not") << " Special K!";
                            PLOG_VERBOSE << "Full product name: " << productName;

                            // Let us not forget to flag the process as injected as well... :)
                            if (be_known.isSpecialK)
                              proc.status = 2;
                          }
                        }
                      }

                      // If we have detected a local injection we shouldn't keep checking the remaining modules
                      if (proc.status == 2)
                        break;
                    } while (Module32NextW (hModuleSnap, &me32));
                  }
                }

                // Go through each handle the process contains (but only if not local)
                if (proc.status != 2)
                {
                  for ( auto& handle : handles_by_process[pe32.th32ProcessID])
                  {
                    auto hHandleSrc = handle.Handle;

                    // Debug purposes
                    //PLOG_VERBOSE << "Handle Granted Access: " << handle.GrantedAccess;

                    HANDLE   hDupHandle;
                    NTSTATUS ntStat     =
                      NtDuplicateObject (
                        hProcessSrc,  hHandleSrc,
                        hProcessDst, &hDupHandle,
                                0, 0, 0 );

                    if (! NT_SUCCESS (ntStat)) continue;

                    std::wstring handle_name = L"";

                    ULONG      _ObjectNameLen ( 64 );
                    _ByteArray pObjectName;

                    do
                    {
                      pObjectName.resize (
                          _ObjectNameLen );

                      ntStat =
                        NtQueryObject (
                          hDupHandle,
                                ObjectNameInformation,
                              pObjectName.data (),
                              _ObjectNameLen,
                              &_ObjectNameLen );

                    } while (ntStat == STATUS_INFO_LENGTH_MISMATCH);

                    if (NT_SUCCESS (ntStat))
                    {
                      POBJECT_NAME_INFORMATION _pni =
                        (POBJECT_NAME_INFORMATION) pObjectName.data ();

                      handle_name = _pni != nullptr ?
                                    _pni->Name.Length > 0 ?
                                    _pni->Name.Buffer     : L""
                                                          : L"";
                    }

                    CloseHandle (hDupHandle);

                    // Examine what we got
                    if ( (std::wstring::npos != handle_name.find ( L"SK_GlobalHookTeardown32" )  ||
                          std::wstring::npos != handle_name.find ( L"SK_GlobalHookTeardown64" )) )
                    {
                      proc.status = 3; // Some form of global injection -- set to Inert for now

                      // Skip checking the remaining handles for this process
                      break;
                    }
                  }
                }

                // If some form of injection was detected, add it to the list
                if (_registry.bProcessIncludeAll || proc.status != 255)
                {
                  wchar_t                                wszProcessName [MAX_PATH + 2] = { };
                  GetProcessImageFileNameW (hProcessSrc, wszProcessName, MAX_PATH);

                  std::wstring friendlyPath = std::wstring(wszProcessName);

                  static std::map <std::wstring, std::wstring>
                    deviceMap = GetDosPathDevicePathMap ( );

                  for (auto& device : deviceMap)
                  {
                    if (friendlyPath.find(device.second) != std::wstring::npos)
                    {
                      friendlyPath.replace(0, device.second.length(), (device.first + L"\\"));
                      // Strip all null terminator \0 characters from the string
                      friendlyPath.erase(std::find(friendlyPath.begin(), friendlyPath.end(), '\0'), friendlyPath.end());
                      break;
                    }
                  }

                  proc.pid      = pe32.th32ProcessID;
                  proc.arch     = SKIF_Util_IsProcessX86 (hProcessSrc) ? "32-bit" : "64-bit";
                  proc.filename = (friendlyPath.empty()) ? L"<unknown>" : friendlyPath;
                  proc.path     = friendlyPath;
                  proc.pathUTF8 = SK_WideCharToUTF8 (friendlyPath);
                  proc.tooltip  = proc.pathUTF8;
                  proc.admin    = SKIF_Util_IsProcessAdmin (pe32.th32ProcessID);

                  if      (_inject._TestUserList     (proc.pathUTF8.c_str(), false))
                    proc.policy = Blacklist;
                  else if (_inject._TestUserList     (proc.pathUTF8.c_str(),  true))
                    proc.policy = Whitelist;
                  else
                    proc.policy = DontCare;

                  PathStripPathW (proc.filename.data());

                  // Strip all null terminator \0 characters from the string
                  proc.filename.erase(std::find(proc.filename.begin(), proc.filename.end(), '\0'), proc.filename.end());

                  if (proc.filename == L"SKIFsvc32.exe")
                    proc.details = "Special K 32-bit Injection Service Host ";

                  if (proc.filename == L"SKIFsvc64.exe")
                    proc.details = "Special K 64-bit Injection Service Host ";

                  if (proc.filename == L"SKIFdrv.exe")
                    proc.details = "Special K Driver Manager ";

                  if (proc.admin && ! ::IsUserAnAdmin ( ))
                    proc.details += "<access denied> ";

                  // Check if process is suspended
                  NTSTATUS ntStatusInfoProc;
                  PROCESS_EXTENDED_BASIC_INFORMATION pebi{};

                  ntStatusInfoProc = 
                    NtQueryInformationProcess (
                      hProcessSrc,
                        ProcessBasicInformation,
                        &pebi,
                        sizeof(pebi),
                        0                     );

                  if (NT_SUCCESS (ntStatusInfoProc) && pebi.Size >= sizeof (pebi))
                  {
                    // This does not detect all suspended processes, e.g. suspended using NtSuspendProcess()
                    if (pebi.IsFrozen)
                      proc.details += "<suspended> ";

                    if (pebi.IsProtectedProcess)
                      proc.details += "<protected> ";

                    //if (pebi.IsWow64Process)
                    //  proc.details += "<wow64> ";

                    if (pebi.IsProcessDeleting)
                      proc.details += "<zombie process> ";

                    if (pebi.IsBackground)
                      proc.details += "<background> ";

                    if (pebi.IsSecureProcess)
                      proc.details += "<secure> ";
                  }

                  // Add it to the list, but only if it's not a zombie process
                  if (! pebi.IsProcessDeleting)
                    Processes.emplace_back(proc);
                }

                CloseHandle (hProcessSrc);
              } while (Process32NextW (hProcessSnap, &pe32));
            }
          }

#pragma endregion

#pragma region Detect Active Injections

          // Check if DLL is currently loaded. If not, load it.
          extern HMODULE hModSpecialK;
          if (hModSpecialK == nullptr)
#ifdef _WIN64
            hModSpecialK = LoadLibraryW (L"SpecialK64.dll");
#else
            hModSpecialK = LoadLibraryW (L"SpecialK32.dll");
#endif

          if (hModSpecialK != nullptr)
          {
            // This retrieves a list of the 32 latest injected processes.
            // There is no guarantee any of these are still running.
              SKX_GetInjectedPIDs_pfn
              SKX_GetInjectedPIDs     =
            (SKX_GetInjectedPIDs_pfn)GetProcAddress   (hModSpecialK,
            "SKX_GetInjectedPIDs");

            /* Unused
              SK_Inject_GetRecord     =
            (SK_Inject_GetRecord_pfn)GetProcAddress   (hModSpecialK,
            "SK_Inject_GetRecord");

              SK_Inject_AuditRecord     =
            (SK_Inject_AuditRecord_pfn)GetProcAddress (hModSpecialK,
            "SK_Inject_AuditRecord");
            */

            if (SKX_GetInjectedPIDs != nullptr)
            {
              size_t num_pids =
                SKX_GetInjectedPIDs (snapshot.dwPIDs, MAX_INJECTED_PROCS);

              while (num_pids > 0)
              {
                DWORD dwPID =
                  snapshot.dwPIDs[--num_pids];

                for (auto& proc : Processes)
                  if (proc.pid == dwPID)
                    proc.status   = 1; // Active
              }
            }
          }

#pragma endregion

          // Sort the results
          SortProcesses (Processes);

          // Swap in the results
          lastWritten = currWriting;
          snapshot_idx_written.store (lastWritten);

          // Force a repaint
          PostMessage (SKIF_ImGui_hWnd, WM_NULL, 0x0, 0x0);

          // Sleep until it's time to check again
          Sleep (refreshIntervalInMsec.load());

        } while (IsWindow (SKIF_ImGui_hWnd)); // Keep thread alive until exit

        SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

        LeaveCriticalSection  (&ProcessRefreshJob);
        DeleteCriticalSection (&ProcessRefreshJob);

        return 0;
      }, nullptr, 0x0, nullptr
    );
  }

  ImGui::Spacing          ( );
  ImGui::Spacing          ( );

  static standby_record_s static_proc = { };
  static DWORD hoveredPID = 0;

  auto _ProcessMenu = [&](standby_record_s proc) -> void
  {
    static bool opened = false;
    static bool openedWithAltMethod = false;

    bool _GamePadRightClick =
      ( ImGui::IsItemFocused ( ) && ( ImGui::GetIO ( ).NavInputsDownDuration     [ImGuiNavInput_Input] != 0.0f &&
                                      ImGui::GetIO ( ).NavInputsDownDurationPrev [ImGuiNavInput_Input] == 0.0f &&
                                            ImGui::GetCurrentContext ()->NavInputSource == ImGuiInputSource_NavGamepad ) );

    static constexpr float _LONG_INTERVAL = .15f;

    bool _NavLongActivate =
      ( ImGui::IsItemFocused ( ) && ( ImGui::GetIO ( ).NavInputsDownDuration     [ImGuiNavInput_Activate] >= _LONG_INTERVAL &&
                                      ImGui::GetIO ( ).NavInputsDownDurationPrev [ImGuiNavInput_Activate] <= _LONG_INTERVAL ) );

    if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
          _GamePadRightClick                            ||
          _NavLongActivate)
    {
      ImGui::OpenPopup ("ProcessMenu");

      if (_GamePadRightClick || _NavLongActivate)
        openedWithAltMethod = true;
    }

    if (openedWithAltMethod)
      ImGui::SetNextWindowPos (ImGui::GetCursorScreenPos() + ImVec2 (250.0f * SKIF_ImGui_GlobalDPIScale, 0.0f));

    if (ImGui::BeginPopup     ("ProcessMenu", ImGuiWindowFlags_NoMove))
    {
      if (! proc.tooltip.empty())
      {
        std::filesystem::path p = proc.tooltip;

        if (ImGui::BeginMenu    (ICON_FA_TOOLBOX " Actions:"))
        {
          if (ImGui::Selectable (ICON_FA_BAN " Blacklist"))
            _inject.BlacklistPath (p.string());

          if (ImGui::Selectable (ICON_FA_CHECK " Whitelist"))
            _inject.WhitelistPath (p.string());

          ImGui::EndMenu ( );
        }

        ImGui::Separator ( );

        /*

        if (ImGui::BeginMenu    (ICON_FA_WRENCH " Debug:"))
        {
          if (ImGui::Selectable (ICON_FA_PAUSE " Suspend"))
          {
            HANDLE hProcessSrc =
                OpenProcess (
                    PROCESS_SUSPEND_RESUME, FALSE, // We don't actually need the additional stuff of PROCESS_QUERY_INFORMATION
                  proc.pid );

            NtSuspendProcess (hProcessSrc);

            CloseHandle (hProcessSrc);
          }

          if (ImGui::Selectable (ICON_FA_PLAY " Resume"))
          {
            HANDLE hProcessSrc =
                OpenProcess (
                    PROCESS_SUSPEND_RESUME, FALSE, // We don't actually need the additional stuff of PROCESS_QUERY_INFORMATION
                  proc.pid );

            NtResumeProcess (hProcessSrc);

            CloseHandle (hProcessSrc);
          }

          ImGui::EndMenu ( );
        }

        ImGui::Separator ( );

        */

        if (ImGui::Selectable  (ICON_FA_FOLDER_OPEN " Browse"))
          SKIF_Util_ExplorePath (SK_UTF8ToWideChar (p.parent_path().string()).c_str());

        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (p.parent_path().string().c_str());

        ImGui::Separator ( );
      }

      if (ImGui::Selectable  (ICON_FA_SQUARE_XMARK " End task"))
      {
        static_proc = proc;
      }

      ImGui::EndPopup        ( );
    }
    else {
      openedWithAltMethod = false;
    }
  };

  auto _ChangeSort = [&](const int& method) -> void
  {
    static int  prevMethod    = _registry.iProcessSort;
    static bool prevAscending = _registry.bProcessSortAscending;

    if (method == 0 || method != prevMethod)
      _registry.bProcessSortAscending = true; // Always sort ascending first
    else if (method == prevMethod)
      _registry.bProcessSortAscending = ! _registry.bProcessSortAscending;

    if (method != prevMethod) {
      _registry.iProcessSort = method;
      _registry.regKVProcessSort.putData          (_registry.iProcessSort);
      _registry.regKVProcessSortAscending.putData (_registry.bProcessSortAscending);
    }
    else if (_registry.bProcessSortAscending != prevAscending)
      _registry.regKVProcessSortAscending.putData (_registry.bProcessSortAscending);

    prevMethod    = _registry.iProcessSort;
    prevAscending = _registry.bProcessSortAscending;
  };


  // ActiveProcessMonitoring
#pragma region Process List

  ImGui::PushStyleColor (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                          );

  static bool bHLStatus, bHLPID, bHLArch, bHLAdmin, bHLName;

  static ImVec4 colHLNormal = ImGui::GetStyleColorVec4 (ImGuiCol_Text),
                colHLActive = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption);

  ImGui::TextColored ((bHLStatus) ? colHLActive : colHLNormal, "Status");
  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverTip ("Injection status");
  if (ImGui::IsItemClicked ()) _ChangeSort (0);
  if (ImGui::IsItemHovered ()) bHLStatus = true; else bHLStatus = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 ( 70.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLPID) ? colHLActive : colHLNormal, "PID");
  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverTip ("Process ID");
  if (ImGui::IsItemClicked ()) _ChangeSort (1);
  if (ImGui::IsItemHovered ()) bHLPID = true; else bHLPID = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (125.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::Text        ("%s", "Type");
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (170.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLArch) ? colHLActive : colHLNormal, "Arch");
  SKIF_ImGui_SetHoverTip ("CPU architecture");
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked ()) _ChangeSort (2);
  if (ImGui::IsItemHovered ()) bHLArch = true; else bHLArch = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (220.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLAdmin) ? colHLActive : colHLNormal, "Admin");
  SKIF_ImGui_SetHoverTip ("Elevated process");
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked ()) _ChangeSort (3);
  if (ImGui::IsItemHovered ()) bHLAdmin = true; else bHLAdmin = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (275.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLName) ? colHLActive : colHLNormal, "Process Name");
  SKIF_ImGui_SetHoverTip ("Process name");
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked ()) _ChangeSort (4);
  if (ImGui::IsItemHovered ()) bHLName = true; else bHLName = false;
  /* Disabled Detail column (cannot find any use for it)
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (500.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::Text        ("%s", "Details");
  */

  ImGui::PopStyleColor ( );

  ImGui::Separator   ( );

  SKIF_ImGui_BeginChildFrame (0x68992, ImVec2 (ImGui::GetContentRegionAvail ().x,
           (_registry.bHorizonMode) ? 250.0f : ImGui::GetContentRegionAvail ().y /* / 1.3f */), ImGuiWindowFlags_NoBackground); // | ImGuiWindowFlags_AlwaysVerticalScrollbar
      
  ImGui::PushStyleColor (
    ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase)
                          );

  if (EventIndex == USHRT_MAX)
    ImGui::Text ("Error occurred when trying to locate type index for events!");
  else if (processes.empty () && _registry.iProcessRefreshInterval == 0)
    ImGui::Text ("Real-time updates are paused.");
  else if (processes.empty ())
    ImGui::Text ("Special K is currently not injected in any process.");

  // This will ensure that the new sort order is applied immediately and only sorted once
  if (! processes.empty ( ) && (processes[0].sortedBy != _registry.iProcessSort || processes[0].sortedAsc != _registry.bProcessSortAscending))
    SortProcesses (processes);

  for ( auto& proc : processes )
  {
    std::string pretty_str       = ICON_FA_WINDOWS,
                pretty_str_hover = "Windows";
        
    if (StrStrIA(proc.tooltip.c_str(), "SteamApps") != NULL)
    {
      pretty_str       = ICON_FA_STEAM;
      pretty_str_hover = "Steam";
    }
    else if (SKIF_Debug_IsXboxApp(proc.tooltip, SK_WideCharToUTF8(proc.filename)))
    {
      pretty_str       = ICON_FA_XBOX;
      pretty_str_hover = "Xbox";
    }

    ImGui::PushID (proc.pid);

    ImVec2 curPos = ImGui::GetCursorPos ( );
    ImGui::Selectable   ("", (hoveredPID == proc.pid), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap);
    _ProcessMenu (proc);
    if (ImGui::IsItemHovered ( ))
      hoveredPID = proc.pid;
    ImGui::SetCursorPos (curPos);

    ImVec4      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg);
    std::string txtPolicy = "",
                hovPolicy = "";
          
    if (proc.policy == Blacklist)
    {
      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure);
      txtPolicy = ICON_FA_BAN;
      hovPolicy = "Process is blacklisted";
    }
    else if (proc.policy == Whitelist)
    {
      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
      txtPolicy = ICON_FA_CHECK;
      hovPolicy = "Process is whitelisted";
    }

    ImVec4 colText         =    (hoveredPID == proc.pid)    ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption) :
                                                              ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase)    ;

    ImVec4 colStatus      = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    std::string hovStatus = "";

    switch (proc.status)
    {
    case 1: // Active Global Injection
      colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
      hovStatus = "Active";
      break;
    case 2: // Local Injection
      colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info);
      hovStatus = "Local";
      break;
    case 3: // Inert Global Injection
      if (_inject.bCurrentState) {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase);
        hovStatus = "Inert";
      } else {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        hovStatus = "Stuck (end the process to eject Special K)";
      }
      break;
    case 254: // Potentially stuck
      if (_inject.bCurrentState) {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        hovStatus = "Inert (potentially stuck)";
      } else {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        hovStatus = "Stuck (end the process to eject Special K)";
      }
      break;
    case 255: // Default / Unknown / Uninjected processes
      break;
    }

    ImGui::TextColored     (colStatus, ICON_FA_CIRCLE);
    if (! hovStatus.empty())
      SKIF_ImGui_SetHoverTip (hovStatus.c_str());
    ImGui::SameLine        ( );        
    ImGui::TextColored     (colPolicy, txtPolicy.c_str());
    if (! hovPolicy.empty())
      SKIF_ImGui_SetHoverTip (hovPolicy.c_str());
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 ( 65.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%i", proc.pid);
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (120.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "  %s", pretty_str.c_str());
    SKIF_ImGui_SetHoverTip (pretty_str_hover);
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (165.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%s", proc.arch.c_str());
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (225.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%s", proc.admin ? "Yes" : "No");
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (270.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     ((proc.status <= 2) ? colStatus
                                                : colText,
                                          "%s", SK_WideCharToUTF8 (proc.filename).c_str());
    if (! proc.tooltip.empty())
      SKIF_ImGui_SetHoverTip (proc.tooltip);
    /* Detail column is so far only used for special purposes */
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (495.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%s", proc.details.c_str());
    if (proc.details.length() > 73)
      SKIF_ImGui_SetHoverTip (proc.details);
    else if (proc.details == "<access denied>")
      SKIF_ImGui_SetHoverTip ("Injection status cannot be determined due to a lack of permissions.");

    ImGui::PopID  ( );
  }

  if (! ImGui::IsAnyItemHovered ( ))
    hoveredPID = 0;

  ImGui::PopStyleColor ( );

  ImGui::EndChildFrame ( );

#pragma endregion
    
  // Confirm prompt

  if (static_proc.pid != 0)
  {
    ImGui::OpenPopup         ("Task Manager###TaskManagerMonitor");

    ImGui::SetNextWindowSize (ImVec2 (400.0f * SKIF_ImGui_GlobalDPIScale, 0.0f));
    ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal ( "Task Manager###TaskManagerMonitor", nullptr,
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_AlwaysAutoResize )
        )
    {

      ImGui::Text        ("Do you want to end");
      ImGui::SameLine    ( );
      ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), SK_WideCharToUTF8 (static_proc.filename).c_str());
      ImGui::SameLine    ( );
      ImGui::Text        ("?");
      SKIF_ImGui_Spacing ( );
      ImGui::TextWrapped ("If an open program is associated with this process, it will close and you will lose any unsaved data. "
                          "If you end a system process, it might result in system instability. Are you sure you want to continue?");

      SKIF_ImGui_Spacing ( );

      ImGui::SetCursorPos (ImGui::GetCursorPos() + ImVec2(170.0f, 0));

      if (ImGui::Button ("End Process", ImVec2 (  100 * SKIF_ImGui_GlobalDPIScale,
                                                   25 * SKIF_ImGui_GlobalDPIScale )))
      {
        SKIF_Util_TerminateProcess (static_proc.pid, 0x0);

        static_proc = standby_record_s{};
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_proc = standby_record_s{};
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::EndPopup ( );
    }
  }
}

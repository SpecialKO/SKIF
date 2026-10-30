//
// Copyright 2021 - 2026  Andon "Kaldaien" Coleman and Aemony
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

#include <tabs/monitor.h>

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

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

HMODULE hModSpecialK; // Used to dynamically load and unload the Special K DLL file when switching back and forth to the tab
std::atomic <bool> SKIF_Monitor_bReloadDLL = false; // Used by main thread to signal if the Special K DLL file should be reloaded.

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
#define INJECTION_RECORD_ABI_VER  "1.2.1"

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
    uint64_t     steam_appid     =  0 ;
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
  std::ignore = processName;

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
  std::ignore = processName;

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
  int           handles; // count
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

  bool enableColums = (ImGui::GetContentRegionAvail().x / SKIF_ImGui_GlobalDPIScale >= 750.f);
  float maxWidth = 0.56f * ImGui::GetContentRegionAvail().x; // Needs to be before the SKIF_ImGui_Columns() call

  SKIF_ImGui_Spacing      ( );

  if (enableColums)
  {
    SKIF_ImGui_Columns    (2, "SKIF_COLUMN_MONITOR", true);
    ImGui::SetColumnWidth (0, maxWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
  }
  else {
    // This is needed to reproduce the same padding on the left side as when using columns
    ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
    ImGui::BeginGroup    ( );
  }

  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  // TRANSLATORS: {0} is a circle icon
  ImGui::TextWrapped      ("%s", F_("Use the below list to identify injected processes using the {0} status indicator to the left."
                                    " Note that while a lot of processes might have Special K injected in them, only those that are whitelisted"
                                    " and use a render API will see Special K active. Remaining processes have it inert and can"
                                    " be ignored.", ICON_FA_CIRCLE).c_str ( ));

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );

  if (SKIF_ImGui_Checkbox (L_("Show misc processes"),  &_registry.bProcessIncludeAll))
    _registry.regKVProcessIncludeAll.putData        (_registry.bProcessIncludeAll);

  // TRANSLATORS: {0} is a circle icon
  SKIF_ImGui_SetHoverTip (F_("If this is enabled the below list will also include uninjected processes.\n"
                             "This is indicated by the lack of a {0} icon under the Status column.", ICON_FA_CIRCLE));

  ImGui::EndGroup         ( );

  const char* RefreshInterval[] = { NC_("update speed", "Paused"), // 0 (never)
                                    NC_("update speed", "Slow"),   // 1 (5s)
                                    NC_("update speed", "Normal"), // 2 (1s)
                                    NC_("update speed", "High")    // 3 (0.5s; not implemented)
  };
  static const char* RefreshIntervalCurrent = RefreshInterval[_registry.iProcessRefreshInterval];

  // The update speed continues the row only if all of it fits (the indent, caption, combo and the paused icon), else it moves below
  const float fRefreshComboWidth =
    SKIF_ImGui_ComboWidth ({ CT_("update speed", RefreshInterval[0]), CT_("update speed", RefreshInterval[1]),
                             CT_("update speed", RefreshInterval[2]), CT_("update speed", RefreshInterval[3]) },
                           100.0f * SKIF_ImGui_GlobalDPIScale);

  SKIF_ImGui_SameLineIfFits (ImGui::GetStyle ( ).IndentSpacing +
                             ImGui::CalcTextSize (_("Update speed:")).x + ImGui::GetStyle ( ).ItemSpacing.x + fRefreshComboWidth +
                             ((_registry.iProcessRefreshInterval == 0) ? ImGui::GetStyle ( ).ItemSpacing.x * 3.0f + ImGui::CalcTextSize (ICON_FA_TRIANGLE_EXCLAMATION " ").x : 0.0f));

  ImGui::BeginGroup       ( );
  ImGui::TreePush         ("RealTimeUpdates");

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), "%s",
      _("Update speed:")
  );

  ImGui::SameLine    ( );

  ImGui::SetNextItemWidth (fRefreshComboWidth);

  if (ImGui::BeginCombo ("###SKIF_iProcRefreshIntervalCombo", CT_("update speed", RefreshIntervalCurrent))) // The second parameter is the label previewed before opening the combo.
  {
    for (int n = 0; n < IM_ARRAYSIZE (RefreshInterval); n++)
    {
      bool is_selected = (RefreshIntervalCurrent == RefreshInterval[n]); // You can store your selection however you want, outside or inside your objects
      if (ImGui::Selectable (CT_("update speed", RefreshInterval[n]), is_selected))
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

    SKIF_ImGui_SetHoverTip (_("Real-time updates are paused."));
  }

  ImGui::TreePop          ( );
  ImGui::EndGroup         ( );

  ImGui::NewLine          ( );

  SKIF_ImGui_TextColoredWrapped (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            _("Toggle Service:"));

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ("%s", _("A forced stop of the service may sometime help in case of an issue,"
                            " even if the service is not currently running."));

  SKIF_ImGui_Spacing      ( );

  ImGui::TreePush   ("ServiceToggles");

  const char* szForceStart = LI_(ICON_FA_TOGGLE_ON  " ", "Force start");
  const char* szForceStop  = LI_(ICON_FA_TOGGLE_OFF " ", "Force stop");
  const float fForceWidth  = std::max ({ 150.0f * SKIF_ImGui_GlobalDPIScale,
                                         ImGui::CalcTextSize (szForceStart, nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                         ImGui::CalcTextSize (szForceStop,  nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f });

  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success));
  if (ImGui::Button ( szForceStart, ImVec2 (fForceWidth, // ICON_FA_PLAY
                                                            30.0f * SKIF_ImGui_GlobalDPIScale )))
    _inject._StartStopInject (false, _registry.bStopOnInjection);
  ImGui::PopStyleColor ( );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ServiceMenu = PopupState_Open;

  SKIF_ImGui_SameLineIfFits (fForceWidth);

  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning));
  if (ImGui::Button ( szForceStop, ImVec2 (fForceWidth, // ICON_FA_STOP
                                                            30.0f * SKIF_ImGui_GlobalDPIScale )))
    _inject._StartStopInject(true);
  ImGui::PopStyleColor ( );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ServiceMenu = PopupState_Open;

  SKIF_ImGui_Spacing      ( );

#ifdef _WIN64
  if ( _inject.SKVer64 >= L"21.08.12" &&
       _inject.SKVer32 >= L"21.08.12" )
#else
  if ( _inject.SKVer32 >= L"21.08.12" )
#endif
  {
    if (SKIF_ImGui_Checkbox (L_("Stop automatically"), &_registry.bStopOnInjection))
      _inject.ToggleStopOnInjection ( );

    ImGui::SameLine         ( );

    ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  (_("This controls whether the configured auto-stop behavior (see Settings tab) should be used when the service is manually started.\n"
                            "Note that having this unchecked does not disable the auto-stop behavior if a game is launched without the service already running."));

    ImGui::SameLine         ( );
  }

  if (SKIF_ImGui_IconButton (0x97848, ICON_FA_FOLDER_OPEN, _("Install folder"), ImColor(255, 207, 72)))
    SKIF_Util_ExplorePath (_path_cache.specialk_userdata);

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       (
    SK_WideCharToUTF8 (_path_cache.specialk_userdata).c_str ()
  );

  ImGui::TreePop    ( );

  ImGui::PopStyleColor    ( );

  if (enableColums)
  {
    ImGui::NextColumn    ( );
  }
  else {
    ImGui::Spacing       ( );
    ImGui::Spacing       ( );
  }

  SKIF_ImGui_TextColoredWrapped (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      _("Status:"));

  SKIF_ImGui_Spacing      ( );

  SKIF_UI_DrawPlatformStatus ( );

  ImGui::NewLine          ( );

  SKIF_ImGui_TextColoredWrapped (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      _("Components:"));

  SKIF_ImGui_Spacing      ( );

  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f)
                            );

  SKIF_UI_DrawComponentVersion ();

  ImGui::PopStyleColor    ( );

  if (enableColums)
    ImGui::Columns       (1);
  else
    ImGui::EndGroup      ( );

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
    static HANDLE hWorkerThread = (HANDLE)
    _beginthreadex (nullptr, 0x0, [](void*) -> unsigned
    {
      CRITICAL_SECTION            ProcessRefreshJob = { };
      InitializeCriticalSection (&ProcessRefreshJob);
      EnterCriticalSection      (&ProcessRefreshJob);

      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_ProcessRefreshJob");

      // Is this combo really appropriate for this thread?
      SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
      SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

      struct known_dll_s {
        std::wstring path;
        bool isSpecialK;
      };

      static std::vector<known_dll_s> knownDLLs;
      static SKX_GetInjectedPIDs_pfn
              SKX_GetInjectedPIDs = nullptr;

      // The system handle table is several MB, so it is reused across refreshes instead of being
      //   re-allocated and zero-filled every time (it is released while the tab is not in use)
      static ULONG      handle_info_capacity = SystemHandleInformationSize;
      static _ByteArray handle_info_buffer;

      do
      {
        while (SKIF_Tab_Selected != UITab_Monitor || refreshIntervalInMsec.load() == 0)
        {
          // If the Special K DLL file is currently loaded, unload it
          if (hModSpecialK != nullptr)
          {
            FreeLibrary (hModSpecialK);
            hModSpecialK = nullptr;
            SKX_GetInjectedPIDs = nullptr;
          }

          // Release the handle table buffer while the tab is not in use
          if (! handle_info_buffer.empty ( ))
            _ByteArray ( ).swap (handle_info_buffer);

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

        ULONG      handle_info_size ( handle_info_capacity );

        do
        {
          if (handle_info_buffer.size ( ) < handle_info_size)
              handle_info_buffer.resize (   handle_info_size );

          ntStatusHandles =
            NtQuerySystemInformation (
              SystemExtendedHandleInformation,
                handle_info_buffer.data (),
                static_cast <ULONG> (handle_info_buffer.size ()),
                &handle_info_size     );

          // Add some headroom, since new handles may appear before the next attempt
          if (ntStatusHandles == STATUS_INFO_LENGTH_MISMATCH)
            handle_info_size += handle_info_size / 8;

        } while (ntStatusHandles == STATUS_INFO_LENGTH_MISMATCH);

        handle_info_capacity =
          static_cast <ULONG> (handle_info_buffer.size ());

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
                        L"OpenGL32.dll",
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
                proc.handles = 0;

                for ( auto& handle : handles_by_process[pe32.th32ProcessID])
                {
                  proc.handles++;

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
                wchar_t                                          wszProcessName [MAX_PATH + 2] = { };
                SKIF_Util_GetProcessImageFileNameW (hProcessSrc, wszProcessName, MAX_PATH);

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
                  Processes.emplace_back (std::move (proc));
              }

              CloseHandle (hProcessSrc);
            } while (Process32NextW (hProcessSnap, &pe32));
          }
        }

#pragma endregion

#pragma region Detect Active Injections

        if (SKIF_Monitor_bReloadDLL.load())
        {
          SKIF_Monitor_bReloadDLL.store (false);

          // If the Special K DLL file is currently loaded, unload it
          if (hModSpecialK != nullptr)
          {
            FreeLibrary (hModSpecialK);
            hModSpecialK = nullptr;
            SKX_GetInjectedPIDs = nullptr;
          }
        }

        // Check if DLL is currently loaded. If not, load it.
        if (hModSpecialK == nullptr)
        {
          static wchar_t fullPath [MAX_PATH];
          static DWORD length =
#ifdef _WIN64
            GetFullPathNameW (L"SpecialK64.dll", MAX_PATH, fullPath, NULL);
#else
            GetFullPathNameW (L"SpecialK32.dll", MAX_PATH, fullPath, NULL);
#endif

          if (0 < length && length < MAX_PATH)
          {
            hModSpecialK = LoadLibraryW (fullPath);
            SKX_GetInjectedPIDs = (SKX_GetInjectedPIDs_pfn)GetProcAddress (hModSpecialK, "SKX_GetInjectedPIDs");

            /* Unused
            SK_Inject_GetRecord     =
            (SK_Inject_GetRecord_pfn)GetProcAddress   (hModSpecialK,
            "SK_Inject_GetRecord");

            SK_Inject_AuditRecord     =
            (SK_Inject_AuditRecord_pfn)GetProcAddress (hModSpecialK,
            "SK_Inject_AuditRecord");
            */
          }
        }

        if (hModSpecialK != nullptr && SKX_GetInjectedPIDs != nullptr)
        {
          // This retrieves a list of the 32 latest injected processes.
          // There is no guarantee any of these are still running.
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
    }, nullptr, 0x0, nullptr);

    if (hWorkerThread != NULL)
    {
      CloseHandle (hWorkerThread);
      hWorkerThread = NULL;
    }
  }

  ImGui::Spacing          ( );
  ImGui::Spacing          ( );

  static standby_record_s static_proc = { };

  // Matches gathered when Reset is selected in the process menu, used by the confirmation prompt
  struct user_list_reset_s {
    std::string              filename;           // Empty when the prompt is closed
    std::vector<std::string> whitelist;          // Matching user patterns in the whitelist
    std::vector<std::string> blacklist;          // Matching user patterns in the blacklist
    const char*              builtInWhitelist = nullptr; // Matching built-in patterns, which cannot be removed
    const char*              builtInBlacklist = nullptr;
    bool                     failed           = false;   // Removal failed to save
  };

  static user_list_reset_s static_reset = { };
  static DWORD hoveredPID = 0;

  auto _ProcessMenu = [&](standby_record_s proc) -> void
  {
    static bool opened = false;
    static bool openedWithAltMethod = false;

    bool _GamePadRightClick =
      ( ImGui::IsItemFocused ( ) && ( ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration     != 0.0f &&
                                      ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDurationPrev == 0.0f ));

    static constexpr float _LONG_INTERVAL = .15f;

    bool _NavLongActivate =
      ( ImGui::IsItemFocused ( ) && ( ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration     >= _LONG_INTERVAL   &&
                                      ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDurationPrev <= _LONG_INTERVAL ) ||
                                    ( ImGui::GetKeyData (ImGuiKey_Enter          )->DownDuration     >= _LONG_INTERVAL   &&
                                      ImGui::GetKeyData (ImGuiKey_Enter          )->DownDurationPrev <= _LONG_INTERVAL ) );

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

        if (ImGui::BeginMenu    (LI_(ICON_FA_TOOLBOX, "Actions:")))
        {
          // The blacklist takes precedence, so whitelisting is only offered for items that are neither
          if (proc.policy == DontCare)
          {
            if (ImGui::Selectable (LI_(ICON_FA_CHECK, "Whitelist")))
              _inject.WhitelistPath (p.string());
          }

          if (proc.policy != Blacklist)
          {
            if (ImGui::Selectable (LI_(ICON_FA_BAN, "Blacklist")))
              _inject.BlacklistPath (p.string());
          }

          if (proc.policy != DontCare)
          {
            if (ImGui::Selectable (LI_(ICON_FA_ROTATE_LEFT, "Reset")))
            {
              const std::string path = p.string ();

              static_reset                  = user_list_reset_s { };
              static_reset.filename         = p.filename ().string ();
              static_reset.whitelist        = _inject.GetUserListMatches  (path.c_str (), true);
              static_reset.blacklist        = _inject.GetUserListMatches  (path.c_str (), false);
              static_reset.builtInWhitelist = _inject.GetBuiltInListMatch (path.c_str (), true);
              static_reset.builtInBlacklist = _inject.GetBuiltInListMatch (path.c_str (), false);
            }

            SKIF_ImGui_SetHoverTip (_("Remove the whitelist and blacklist patterns matching this process."));
          }

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

        if (ImGui::Selectable (LI_(ICON_FA_FOLDER_OPEN, "Browse")))
          SKIF_Util_FileExplorer_SelectFile (SK_UTF8ToWideChar (p.string()).c_str());

        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (p.parent_path().string().c_str());

        ImGui::Separator ( );
      }

      if (ImGui::Selectable  (LI_(ICON_FA_SQUARE_XMARK, "End task")))
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

  // Column positions: the defaults (scaled), moved right where a translated header needs more room. The rows use the same positions.
  //   Status, PID, Type, Handles, Arch, Admin, Process Name, Details
  static float colPos [8] = { };
  {
    const char* headers [7] = { _("Status"), _("PID"), _("Type"), _("Handles"), _("Arch"), _("Admin"), _("Process Name") };
    const float defaults [8] = { 0.0f, 70.0f, 125.0f, 170.0f, 235.0f, 295.0f, 350.0f, 645.0f };
    const float spacing      = ImGui::GetStyle ( ).ItemSpacing.x * 2.0f;

    colPos [0] = ImGui::GetCursorPosX ( );

    for (int i = 1; i < 7; i++)
    {
      float needed = ImGui::CalcTextSize (headers [i - 1]).x;

      if (i == 6) // Admin: also the Yes / No of the rows
        needed = std::max ({ needed, ImGui::CalcTextSize (_("Yes")).x, ImGui::CalcTextSize (_("No")).x });

      colPos [i] = std::max (defaults [i] * SKIF_ImGui_GlobalDPIScale, colPos [i - 1] + needed + spacing);
    }

    // Details: moved as much as the process name column
    colPos [7] = defaults [7] * SKIF_ImGui_GlobalDPIScale + (colPos [6] - defaults [6] * SKIF_ImGui_GlobalDPIScale);
  }

  ImGui::TextColored ((bHLStatus) ? colHLActive : colHLNormal, "%s", _("Status"));
  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverTip (_("Injection status"));
  if (ImGui::IsItemClicked ()) _ChangeSort (0);
  if (ImGui::IsItemHovered ()) bHLStatus = true; else bHLStatus = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (colPos [1] - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLPID) ? colHLActive : colHLNormal, "%s", _("PID"));
  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverTip (_("Process ID"));
  if (ImGui::IsItemClicked ()) _ChangeSort (1);
  if (ImGui::IsItemHovered ()) bHLPID = true; else bHLPID = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (colPos [2] - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::Text        ("%s", _("Type"));
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (colPos [3] - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::Text        ("%s", _("Handles"));
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (colPos [4] - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLArch) ? colHLActive : colHLNormal, "%s", _("Arch"));
  SKIF_ImGui_SetHoverTip (_("CPU architecture"));
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked ()) _ChangeSort (2);
  if (ImGui::IsItemHovered ()) bHLArch = true; else bHLArch = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (colPos [5] - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLAdmin) ? colHLActive : colHLNormal, "%s", _("Admin"));
  SKIF_ImGui_SetHoverTip (_("Elevated process"));
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked ()) _ChangeSort (3);
  if (ImGui::IsItemHovered ()) bHLAdmin = true; else bHLAdmin = false;
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (colPos [6] - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::TextColored ((bHLName) ? colHLActive : colHLNormal, "%s", _("Process Name"));
  SKIF_ImGui_SetHoverTip (_("Process name"));
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked ()) _ChangeSort (4);
  if (ImGui::IsItemHovered ()) bHLName = true; else bHLName = false;
  /* Disabled Detail column (cannot find any use for it)
  ImGui::SameLine    ( );
  ImGui::ItemSize    (ImVec2 (500.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
  ImGui::SameLine    ( );
  ImGui::Text        ("%s", "Details"); // Not translated (disabled)
  */

  ImGui::PopStyleColor ( );

  ImGui::Separator   ( );

  SKIF_ImGui_BeginChildFrame (0x68992, ImVec2 (ImGui::GetContentRegionAvail().x,
                            (std::max (250.0f * SKIF_ImGui_GlobalDPIScale, SKIF_ImGui_GetColumnAvailHeight ( )))), // Fills the rest of the tab (the tab is drawn in a centered column that grows with its content)
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar); // | ImGuiWindowFlags_AlwaysVerticalScrollbar

  ImGui::PushStyleColor (
    ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase)
                          );

  if (EventIndex == USHRT_MAX)
    ImGui::TextWrapped ("%s", _("Error occurred when trying to locate type index for events!"));
  else if (processes.empty () && _registry.iProcessRefreshInterval == 0)
    ImGui::TextWrapped ("%s", _("Real-time updates are paused."));
  else if (processes.empty ())
    ImGui::TextWrapped ("%s", _("Special K is currently not injected in any process."));

  // This will ensure that the new sort order is applied immediately and only sorted once
  if (! processes.empty ( ) && (processes[0].sortedBy != _registry.iProcessSort || processes[0].sortedAsc != _registry.bProcessSortAscending))
    SortProcesses (processes);

  DWORD hovered = NULL;

  SKIF_ImGui_BeginHighlightLine ( );

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

    ImVec4      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg);
    std::string txtPolicy = "",
                hovPolicy = "";

    if (proc.policy == Blacklist)
    {
      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure);
      txtPolicy = ICON_FA_BAN;
      hovPolicy = _("Process is blacklisted");
    }
    else if (proc.policy == Whitelist)
    {
      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
      txtPolicy = ICON_FA_CHECK;
      hovPolicy = _("Process is whitelisted");
    }

    ImVec4 colText         =    (hoveredPID == proc.pid)    ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption) :
                                                              ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase)    ;

    ImVec4 colStatus      = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    std::string hovStatus = "";

    switch (proc.status)
    {
    case 1: // Active Global Injection
      colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
      hovStatus = C_("injection status", "Active");
      break;
    case 2: // Local Injection
      colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info);
      hovStatus = C_("injection status", "Local");
      break;
    case 3: // Inert Global Injection
      if (_inject.bCurrentState) {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase);
        hovStatus = C_("injection status", "Inert");
      } else {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        hovStatus = C_("injection status", "Stuck (end the process to eject Special K)");
      }
      break;
    case 254: // Potentially stuck
      if (_inject.bCurrentState) {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        hovStatus = C_("injection status", "Inert (potentially stuck)");
      } else {
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        hovStatus = C_("injection status", "Stuck (end the process to eject Special K)");
      }
      break;
    case 255: // Default / Unknown / Uninjected processes
      break;
    }

    SKIF_ImGui_PushHighlightLineLine ( );

    ImGui::BeginGroup      ( );

    ImGui::TextColored     (colStatus, ICON_FA_CIRCLE);
    if (! hovStatus.empty())
      SKIF_ImGui_SetHoverTip (hovStatus.c_str());
    ImGui::SameLine        ( );
    ImGui::TextColored     (colPolicy, "%s", txtPolicy.c_str());
    if (! hovPolicy.empty())
      SKIF_ImGui_SetHoverTip (hovPolicy.c_str());
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [1] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%i", proc.pid);
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [2] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "  %s", pretty_str.c_str());
    SKIF_ImGui_SetHoverTip (pretty_str_hover);
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [3] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%i", proc.handles);
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [4] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%s", proc.arch.c_str());
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [5] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%s", proc.admin ? _("Yes") : _("No"));
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [6] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     ((proc.status <= 2) ? colStatus
                                               : colText,
                                          "%s", SK_WideCharToUTF8 (proc.filename).c_str());
    if (! proc.tooltip.empty())
      SKIF_ImGui_SetHoverTip (proc.tooltip);
    /* Detail column is so far only used for special purposes */
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (colPos [7] - 5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (colText, "%s", proc.details.c_str());
    if (ImGui::IsItemHovered ( ))
    {
      if (proc.details.length() > 73)
        SKIF_ImGui_SetHoverTip (proc.details);
      else if (proc.details.find("<access denied>") != std::string::npos)
        SKIF_ImGui_SetHoverTip (_("Injection status cannot be determined due to a lack of permissions."));
    }
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight()));

    ImGui::EndGroup        ( );

    if (ImGui::IsItemHovered ( ))
      hoveredPID = hovered = proc.pid;

    SKIF_ImGui_PopHighlightLineLine ( );

    _ProcessMenu (proc);

    ImGui::PopID  ( );
  }

  SKIF_ImGui_EndHighlightLine ( );

  if (! hovered)
    hoveredPID = 0;

  ImGui::PopStyleColor ( );

  // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
  SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

  ImGui::EndChild      ( );

#pragma endregion

  // Service Menu

  SKIF_ImGui_ServiceMenu ( );

  // Confirm prompt

  if (static_proc.pid != 0)
  {
    // Centered with a dimmed background, and closed by resetting static_proc
    if (SKIF_ImGui_BeginPopup ( L_("Task Manager###TaskManagerMonitor"), nullptr,
                                  ImVec2 (400.0f * SKIF_ImGui_GlobalDPIScale, 0.0f) )
        )
    {
      // TRANSLATORS: {0} is the file name of a process
      SKIF_ImGui_TextHighlighted (_("Do you want to end {0}?"), SK_WideCharToUTF8 (static_proc.filename).c_str(), ImColor::HSV (0.11F, 1.F, 1.F));
      SKIF_ImGui_Spacing ( );
      ImGui::TextWrapped ("%s", _("If an open program is associated with this process, it will close and you will lose any unsaved data. "
                          "If you end a system process, it might result in system instability. Are you sure you want to continue?"));

      SKIF_ImGui_Spacing ( );

      // Right-aligned, both as wide as the wider label (at least 100 px)
      const float fButtonWidth = std::max ({ 100.0f * SKIF_ImGui_GlobalDPIScale,
                                    ImGui::CalcTextSize (L_("End Process"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                    ImGui::CalcTextSize (L_("Cancel"),      nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f });

      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + std::max (0.0f, ImGui::GetContentRegionAvail ( ).x - fButtonWidth * 2.0f - ImGui::GetStyle ( ).ItemSpacing.x * 2.0f));

      if (ImGui::Button (L_("End Process"), ImVec2 (  fButtonWidth,
                                                   25 * SKIF_ImGui_GlobalDPIScale )))
      {
        SKIF_Util_TerminateProcess (static_proc.pid, 0x0);

        static_proc = standby_record_s{};
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button (L_("Cancel"), ImVec2 ( fButtonWidth,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_proc = standby_record_s{};
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
    }

    SKIF_ImGui_EndPopup ( ); // Must always be called, regardless of the return value of SKIF_ImGui_BeginPopup
  }

  // Reset whitelist / blacklist prompt

  if (! static_reset.filename.empty ())
  {
    // Centered with a dimmed background, and closed by resetting static_reset
    if (SKIF_ImGui_BeginPopup ( L_("Reset Whitelist / Blacklist###ResetUserListMonitor"), nullptr,
                                  ImVec2 (500.0f * SKIF_ImGui_GlobalDPIScale, 0.0f) )
        )
    {
      const bool hasPatterns = (! static_reset.whitelist.empty () ||
                                ! static_reset.blacklist.empty ());

      auto _ListPatterns = [](const char* icon, ImGuiCol color, const char* caption, const std::vector<std::string>& patterns)
      {
        if (patterns.empty ())
          return;

        ImGui::TextColored (ImGui::GetStyleColorVec4 (color), "%s", icon);
        ImGui::SameLine    ( );
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption), "%s", caption);

        for (const auto& pattern : patterns)
        {
          ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
          ImGui::SameLine        ( );
          ImGui::TextUnformatted (pattern.c_str ()); // Unformatted as patterns may contain %
        }

        SKIF_ImGui_Spacing ( );
      };

      // TRANSLATORS: {0} is the file name of a process
      SKIF_ImGui_TextHighlighted (_("Patterns matching {0}:"), static_reset.filename.c_str (), ImColor::HSV (0.11F, 1.F, 1.F));

      SKIF_ImGui_Spacing ( );

      _ListPatterns (ICON_FA_SQUARE_PLUS,  ImGuiCol_SKIF_Success, _("Whitelist:"), static_reset.whitelist);
      _ListPatterns (ICON_FA_SQUARE_MINUS, ImGuiCol_SKIF_Failure, _("Blacklist:"), static_reset.blacklist);

      if (! hasPatterns)
      {
        ImGui::TextWrapped ("%s", _("No whitelist or blacklist patterns of yours match this process."));
        SKIF_ImGui_Spacing ( );
      }

      // Built-in patterns are part of Special K itself and cannot be removed from here
      for (const char* builtIn : { static_reset.builtInWhitelist, static_reset.builtInBlacklist })
      {
        if (builtIn == nullptr)
          continue;

        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
        ImGui::TextWrapped    ("%s", (builtIn == static_reset.builtInWhitelist)
          ? F_("The process is also whitelisted by the built-in pattern \"{0}\", which cannot be removed.", builtIn).c_str ( )
          : F_("The process is also blacklisted by the built-in pattern \"{0}\", which cannot be removed.", builtIn).c_str ( ));
        ImGui::PopStyleColor  ( );
        SKIF_ImGui_Spacing    ( );
      }

      if (hasPatterns)
      {
        ImGui::TextWrapped ("%s", _("Removing a pattern affects every application it matches, not just this one. "
                            "Do you want to remove the patterns listed above?"));
        SKIF_ImGui_Spacing ( );
      }

      if (static_reset.failed)
      {
        SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), _("The changes could not be saved!"));
        SKIF_ImGui_Spacing ( );
      }

      // Right-aligned, both as wide as the wider label (at least 100 px)
      const float fButtonWidth = std::max ({ 100.0f * SKIF_ImGui_GlobalDPIScale,
                                    ImGui::CalcTextSize (L_("Remove"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                    ImGui::CalcTextSize (L_("Cancel"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f });

      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + std::max (0.0f, ImGui::GetContentRegionAvail ( ).x - fButtonWidth * 2.0f - ImGui::GetStyle ( ).ItemSpacing.x * 2.0f));

      if (! hasPatterns)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::Button (L_("Remove"), ImVec2 ( fButtonWidth,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        bool white_removed = _inject.RemoveUserListPatterns (static_reset.whitelist, true);
        bool black_removed = _inject.RemoveUserListPatterns (static_reset.blacklist, false);

        // Remember what was removed so a retry does not attempt it again
        if (white_removed) static_reset.whitelist.clear ( );
        if (black_removed) static_reset.blacklist.clear ( );

        if (white_removed && black_removed)
          static_reset = user_list_reset_s { };

        else
          static_reset.failed = true;
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );

      if (! hasPatterns)
        SKIF_ImGui_PopDisableState  ( );

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button (L_("Cancel"), ImVec2 ( fButtonWidth,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_reset = user_list_reset_s { };
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
    }

    SKIF_ImGui_EndPopup ( ); // Must always be called, regardless of the return value of SKIF_ImGui_BeginPopup
  }
}

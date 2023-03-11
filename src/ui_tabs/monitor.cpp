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

#include <SKIF_imgui.h>
#include <SKIF_utility.h>

#include <injection.h>
#include <fsutil.h>

#include <font_awesome.h>
#include <sk_utility/utility.h>

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

extern SKIF_InjectionContext _inject;

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

#include <typeindex>

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

NtQuerySystemInformation_pfn NtQuerySystemInformation = nullptr;
NtDuplicateObject_pfn        NtDuplicateObject        = nullptr;
NtQueryObject_pfn            NtQueryObject            = nullptr;

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


// CC BY-SA 4.0: https://stackoverflow.com/a/59908355/15133327
std::map<std::wstring, std::wstring> GetDosPathDevicePathMap()
{
  // It's not really related to MAX_PATH, but I guess it should be enough.
  // Though the docs say "The first null-terminated string stored into the buffer is the current mapping for the device.
  //                      The other null-terminated strings represent undeleted prior mappings for the device."
  wchar_t devicePath[MAX_PATH] = { 0 };
  std::map<std::wstring, std::wstring> result;
  std::wstring dosPath = L"A:";

  for (wchar_t letter = L'A'; letter <= L'Z'; ++letter)
  {
      dosPath[0] = letter;
      if (QueryDosDeviceW(dosPath.c_str(), devicePath, MAX_PATH)) // may want to properly handle errors instead ... e.g. check ERROR_INSUFFICIENT_BUFFER
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

// CC BY-SA 3.0: https://stackoverflow.com/a/39104745/15133327
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
  // Does the string contain "WindowsApps" ?
  bool xbox_app = (path.find("WindowsApps") != std::string::npos);

  // If the string does not contain "WindowsApps", do some string manipulation and assumptions
  if (!xbox_app && path.length() > 0)
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
      xbox_app = true;
  }

  return xbox_app;
}

// Some string manipulation to assume whether a process is a Steam app or not
bool SKIF_Debug_IsSteamApp(std::string path, std::string processName)
{
  return (path.find("SteamApps") != std::string::npos);
}

// CC BY-SA 3.0: https://stackoverflow.com/a/39104745/15133327
//               https://jadro-windows.cz/download/ntqueryobject.zip
#define ALIGN_DOWN(Length, Type)       ((ULONG)(Length) & ~(sizeof(Type) - 1))
#define ALIGN_UP(Length, Type)         (ALIGN_DOWN(((ULONG)(Length) + sizeof(Type) - 1), Type))

// CC BY-SA 3.0: https://stackoverflow.com/a/39104745/15133327
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

      PLOG_VERBOSE << std::to_wstring(TypeInfo->TypeIndex) << " - " << _TypeName;

      if (TypeName == _TypeName)
      {
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

void
SKIF_UI_Tab_DrawMonitor (void)
{
  extern float SKIF_ImGui_GlobalDPIScale;

  static bool  active_listing = true;

  enum inject_policy {
    Blacklist,
    DontCare,
    Whitelist
  };

  struct standby_record_s {
    std::wstring  path;
    std::string   pathUTF8;
    std::wstring  filename;
    std::string   tooltip;
    std::string   details;
    std::string   arch;
    bool          admin;
    int           status; // 0 - Active   1 - Idle/Inactive   2 - Stuck?    3 - Local?
    inject_policy policy;
    //DWORD         pid;
  };

  static std::map <std::wstring, std::wstring> deviceMap = GetDosPathDevicePathMap ( );

  // Double-buffer updates so we can go lock-free
  //
  struct injection_snapshot_s {
    std::map <DWORD, standby_record_s> Processes;

    DWORD dwPIDs [MAX_INJECTED_PROCS] = { };
  } static snapshots [2];

  static volatile LONG snapshot_idx = 0;

  auto &snapshot =
    snapshots [ReadAcquire (&snapshot_idx)];

  auto&      processes = snapshot.Processes;
    
  extern void SKIF_UI_DrawPlatformStatus   (void);
  extern void SKIF_UI_DrawComponentVersion (void);

  SKIF_ImGui_Spacing      ( );

  SKIF_ImGui_Columns      (2, nullptr, true);

  SK_RunOnce (
    ImGui::SetColumnWidth (0, 600.0f * SKIF_ImGui_GlobalDPIScale)
  );

  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  ImGui::TextWrapped      ("Use the below list to identify injected processes. While many processes may be listed"
                            " only whitelisted ones using a render API will see Special K active. Remaning processes"
                            " has it inert and can be ignored unless an issue is being experienced."
  );

  ImGui::NewLine          ( );
  /*
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "Status Indicator:"
                            );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CIRCLE " ");
  ImGui::SameLine         ( );
  ImGui::Text             ("indicates processes where Special K is currently active.");

  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), ICON_FA_CIRCLE " ");
  ImGui::SameLine         ( );
  ImGui::Text             ("indicates processes where Special K is inert.");

  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), ICON_FA_CIRCLE " ");
  ImGui::SameLine         ( );
  ImGui::Text             ("indicates processes where Special K is possibly stuck if the service is no longer running.");

  ImGui::NewLine          ( );
  */
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "Toggle Service:"
                            );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ("A forced stop of the service may sometime help in case of an issue,"
                            " even if the service is not currently running.");

  SKIF_ImGui_Spacing      ( );

  extern bool SKIF_bStopOnInjection;

  ImGui::TreePush   ( );

  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success));
  if (ImGui::Button ( ICON_FA_TOGGLE_ON "  Force Start", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, // ICON_FA_PLAY
                                                            30.0f * SKIF_ImGui_GlobalDPIScale )))
    _inject._StartStopInject (false, SKIF_bStopOnInjection);
  ImGui::PopStyleColor ( );

  ImGui::SameLine   ( );
    
  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning));
  if (ImGui::Button (ICON_FA_TOGGLE_OFF "  Force Stop", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, // ICON_FA_STOP
                                                            30.0f * SKIF_ImGui_GlobalDPIScale )))
    _inject._StartStopInject(true);
  ImGui::PopStyleColor ( );
    
  SKIF_ImGui_Spacing      ( );

  extern void SKIF_putStopOnInjection(bool in);

#ifdef _WIN64
  if ( _inject.SKVer64 >= "21.08.12" &&
        _inject.SKVer32 >= "21.08.12" )
#else
  if ( _inject.SKVer32 >= "21.08.12" )
#endif
  {
    if (ImGui::Checkbox ("Stop automatically", &SKIF_bStopOnInjection))
      SKIF_putStopOnInjection (SKIF_bStopOnInjection);

    ImGui::SameLine         ( );

    ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip  ("This controls whether the configured auto-stop behavior (see Settings tab) should be used when the service is manually started.\n"
                            "Note that having this unchecked does not disable the auto-stop behavior if a game is launched without the service already running.");

    ImGui::SameLine         ( );
  }

  extern bool SKIF_ImGui_IconButton (ImGuiID id, std::string icon, std::string label, const ImVec4 & colIcon);
    
  if (SKIF_ImGui_IconButton (0x97848, ICON_FA_FOLDER_OPEN, "Config Root", ImColor(255, 207, 72)))
    SKIF_Util_ExplorePath (path_cache.specialk_userdata);

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       (
    SK_WideCharToUTF8 (path_cache.specialk_userdata).c_str ()
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
    SKIF_ImportFromNtDll (NtQuerySystemInformation);
    SKIF_ImportFromNtDll (NtQueryObject);
    SKIF_ImportFromNtDll (NtDuplicateObject);
  }              );

  static      USHORT EventIndex = GetTypeIndexByName (L"Event");

  if (EventIndex != USHRT_MAX)
  {
    static HANDLE hThread =
      CreateThread ( nullptr, 0x0,
        [](LPVOID)
      -> DWORD
      {
        extern std::wstring SKIF_GetProductName (const wchar_t* wszName);

        static constexpr auto
          RefreshIntervalInMsec = 500UL; // 250UL

        struct known_dll_s {
          std::wstring path;
          bool isSpecialK;
        };

        static std::vector<known_dll_s> knownDLLs;

        do
        {
          if (SKIF_Tab_Selected != Debug || ! active_listing)
          {
            Sleep (RefreshIntervalInMsec * 4);
            continue;
          }

          long idx =
            ( ReadAcquire (&snapshot_idx) + 1 ) % 2;

          auto &snapshot =
            snapshots [idx];

          auto& Processes      = snapshot.Processes;

          Processes.clear    ();

          using _PerProcessHandleMap =
            std::map        < DWORD,
                std::vector < SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX >
                                                                >;

          _PerProcessHandleMap
            handles_by_process;

          std::set <DWORD> _Detected;
          
          static HANDLE hProcessDst =
            GetCurrentProcess   (); // Pseudo Handle
          static DWORD dwPidOfMe =
            GetCurrentProcessId (); // Actual Pid

#pragma region Detect Teardown Event
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

            // Go through each process
            /*
            for ( auto& handles : handles_by_process )
            {
              auto dwProcId = handles.first;

              HANDLE hProcessSrc =
                  OpenProcess (
                      PROCESS_DUP_HANDLE |
                      PROCESS_QUERY_INFORMATION, FALSE,
                    dwProcId  );

              if (! hProcessSrc) continue;

              // Go through each handle the process contains
              for ( auto& handle : handles.second )
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
                      std::wstring::npos != handle_name.find ( L"SK_GlobalHookTeardown64" )) &&
                      _Detected.emplace(dwProcId).second )
                {
                  wchar_t                                wszProcessName [MAX_PATH] = { };
                  GetProcessImageFileNameW (hProcessSrc, wszProcessName, MAX_PATH);

                  std::wstring friendlyPath = std::wstring(wszProcessName);

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

                  standby_record_s proc = standby_record_s{};
                  proc.path     = friendlyPath;
                  proc.pathUTF8 = SK_WideCharToUTF8 (friendlyPath);
                  proc.tooltip  = proc.pathUTF8;
                  proc.filename = wszProcessName;
                  proc.arch     = (std::wstring::npos != handle_name.find(L"SK_GlobalHookTeardown64")) ? "64-bit" : "32-bit";
                  proc.admin    = SKIF_Util_IsProcessAdmin (dwProcId);

                  if      (_inject._TestUserList     (proc.pathUTF8.c_str(), false))
                    proc.policy = Blacklist;
                  else if (_inject._TestUserList     (proc.pathUTF8.c_str(),  true))
                    proc.policy = Whitelist;
                  else
                    proc.policy = DontCare;

                  proc.status = 1; // Inactive

                  PathStripPathW (proc.filename.data());

                  Processes[dwProcId] = proc;
                  break;
                }
              }

              CloseHandle (hProcessSrc);
            }
            */
          }

#pragma endregion

#pragma region Detect Special K Module (fallback)

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

                // Skip already detected processes (through the teardown event)
                if (_Detected.find(pe32.th32ProcessID) != _Detected.end())
                  continue;

                HANDLE hProcessSrc =
                    OpenProcess (
                        PROCESS_DUP_HANDLE | // Required to open handles
                        PROCESS_QUERY_INFORMATION, FALSE,
                      pe32.th32ProcessID );

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
                      std::wstring modName = me32.szModule;

                      // Special K's global DLL files
                      if (modName == L"SpecialK32.dll" ||
                          modName == L"SpecialK64.dll" )
                      {
                        proc.status = 2; // Stuck?
                        // We'll keep checking the modules for a potential local injection
                      }

                      // Fallback of the fallback -- detect locally injected copies of SK!
                      else {
                        static std::wstring localDLLs[] = {
                          L"dxgi.dll",
                          L"d3d11.dll",
                          L"d3d9.dll",
                          L"dinput8.dll",
                          L"opengl32.dll"
                        };

                        for (auto& dll : localDLLs)
                        {
                          // Skip if it doesn't have the name of a local wrapper DLL
                          if (modName != dll)
                            continue;

                          // Skip system modules below \Windows\System32 and \Windows\SysWOW64
                          if (StrStrIW (me32.szExePath, LR"(\Windows\Sys)"))
                            continue;

                          bool isKnownDLL = false,
                               isSpecialK = false;

                          // Is it known?
                          for (auto& knownDLL : knownDLLs)
                          {
                            if (knownDLL.path == me32.szExePath) // We're dealing with a known DLL
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
                            proc.status = 4; // Local injection

                            // Skip checking the remaining local SK DLLs for this module
                            break;
                          }

                          // DLL file is not known -- let it be known
                          else {
                            std::wstring productName = SKIF_GetProductName (me32.szExePath);

                            known_dll_s be_known = known_dll_s{};
                            be_known.path = me32.szExePath;
                            be_known.isSpecialK = StrStrIW (productName.c_str(), L"Special K");

                            knownDLLs.emplace_back (be_known);

                            PLOG_VERBOSE << "Unknown DLL detected, let it be known: " << me32.szExePath;
                            PLOG_VERBOSE << "DLL " << ((be_known.isSpecialK) ? "is" : "is not") << " Special K!";
                            PLOG_VERBOSE << "Full product name: " << productName;
                          }
                        }
                      }

                      // If we have detected a local injection we shouldn't keep checking the remaining modules
                      if (proc.status == 4)
                        break;
                    } while (Module32NextW (hModuleSnap, &me32));
                  }
                }

                // Go through each handle the process contains (but only if not local)
                if (proc.status != 4)
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
                      proc.status = 1; // Some form of global injection -- set to Inactive for now

                      // Skip checking the remaining handles for this process
                      break;
                    }
                  }
                }

                // If some form of injection was detected, add it to the list
                if (proc.status != 0)
                {
                  wchar_t                                wszProcessName [MAX_PATH] = { };
                  GetProcessImageFileNameW (hProcessSrc, wszProcessName, MAX_PATH);

                  std::wstring friendlyPath = std::wstring(wszProcessName);

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

                  proc.arch     = SKIF_Util_IsProcessX86 (hProcessSrc) ? "32-bit" : "64-bit";
                  proc.path     = friendlyPath;
                  proc.pathUTF8 = SK_WideCharToUTF8 (friendlyPath);
                  proc.tooltip  = proc.pathUTF8;
                  proc.filename = wszProcessName;
                  proc.admin    = SKIF_Util_IsProcessAdmin (pe32.th32ProcessID);

                  if      (_inject._TestUserList     (proc.pathUTF8.c_str(), false))
                    proc.policy = Blacklist;
                  else if (_inject._TestUserList     (proc.pathUTF8.c_str(),  true))
                    proc.policy = Whitelist;
                  else
                    proc.policy = DontCare;

                  PathStripPathW (proc.filename.data());

                  Processes[pe32.th32ProcessID] = proc;
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

                Processes[dwPID].status = 0; // Active
              }
            }
          }

#pragma endregion

          Sleep (RefreshIntervalInMsec);

          InterlockedExchange (&snapshot_idx, idx);

          // Force a repaint
          SetEvent (SKIF_RefreshEvent);
        } while (true); // Keep thread alive until exit

        return 0;
      }, nullptr, 0x0, nullptr
    );
  }

  static std::pair <DWORD, standby_record_s> static_proc = { 0, standby_record_s{} };

  auto _ProcessMenu = [&](std::pair <const DWORD, standby_record_s> proc) -> void
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

    if (ImGui::BeginPopup     ("ProcessMenu"))
    {
      std::string_view
        path = proc.second.tooltip;

      if (! path.empty())
      {
        std::filesystem::path p = path;

        if (ImGui::BeginMenu    (ICON_FA_TOOLBOX " Actions:"))
        {
          if (ImGui::Selectable (ICON_FA_BAN " Blacklist"))
            _inject._BlacklistBasedOnPath (p.string()); // SK_WideCharToUTF8 (proc.second)

          if (ImGui::Selectable (ICON_FA_CHECK " Whitelist"))
            _inject._WhitelistBasedOnPath (p.string()); // SK_WideCharToUTF8 (proc.second)

          ImGui::EndMenu ( );
        }

        ImGui::Separator ( );

        if (ImGui::Selectable  (ICON_FA_FOLDER_OPEN " Browse"))
          SKIF_Util_ExplorePath (SK_UTF8ToWideChar (p.parent_path().string()).c_str());

        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (p.parent_path().string().c_str());

        ImGui::Separator ( );
      }

      if (ImGui::Selectable  (ICON_FA_WINDOW_CLOSE " End task"))
      {
        static_proc = { proc.first, proc.second };
      }

      ImGui::EndPopup        ( );
    }
    else {
      openedWithAltMethod = false;
    }
  };

  ImGui::Spacing          ( );
  ImGui::Spacing          ( );

  static DWORD hoveredPID = 0;

  active_listing = ImGui::CollapsingHeader ("Processes###ActiveProcessMonitoring", ImGuiTreeNodeFlags_DefaultOpen);

  if (active_listing)
  {
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                            );

    ImGui::Text        ("%s", "Status");
    ImGui::SameLine    ( );
    ImGui::ItemSize    (ImVec2 ( 70.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine    ( );
    ImGui::Text        ("%s", "PID");
    ImGui::SameLine    ( );
    ImGui::ItemSize    (ImVec2 (125.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine    ( );
    ImGui::Text        ("%s", "Type");
    ImGui::SameLine    ( );
    ImGui::ItemSize    (ImVec2 (170.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine    ( );
    ImGui::Text        ("%s", "Arch");
    ImGui::SameLine    ( );
    ImGui::ItemSize    (ImVec2 (220.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine    ( );
    ImGui::Text        ("%s", "Admin");
    SKIF_ImGui_SetHoverTip ("Is the process running elevated?");
    ImGui::SameLine    ( );
    ImGui::ItemSize    (ImVec2 (275.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine    ( );
    ImGui::Text        ("%s", "Process Name");
    /* Disabled Detail column (cannot find any use for it)
    ImGui::SameLine    ( );
    ImGui::ItemSize    (ImVec2 (500.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine    ( );
    ImGui::Text        ("%s", "Details");
    */

    ImGui::PopStyleColor ( );

    ImGui::Separator   ( );

    SKIF_ImGui_BeginChildFrame (0x68992, ImVec2 (ImGui::GetContentRegionAvail ().x,
                                                 ImGui::GetContentRegionAvail ().y /* / 1.3f */), ImGuiWindowFlags_NoBackground); // | ImGuiWindowFlags_AlwaysVerticalScrollbar
      
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

    if (EventIndex == USHRT_MAX)
      ImGui::Text ("Error occurred when trying to locate type index for events!");
    else if (processes.empty ())
      ImGui::Text ("Special K is currently not injected in any process.");

    for ( auto& proc : processes )
    {
      std::string pretty_str       = ICON_FA_WINDOWS,
                  pretty_str_hover = "Windows";
        
      if (StrStrIA(proc.second.tooltip.c_str(), "SteamApps") != NULL)
      {
        pretty_str       = ICON_FA_STEAM;
        pretty_str_hover = "Steam";
      }
      else if (SKIF_Debug_IsXboxApp(proc.second.tooltip, SK_WideCharToUTF8(proc.second.filename)))
      {
        pretty_str       = ICON_FA_XBOX;
        pretty_str_hover = "Xbox";
      }

      ImGui::PushID (proc.first);

      ImVec2 curPos = ImGui::GetCursorPos ( );
      ImGui::Selectable   ("", (hoveredPID == proc.first), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap);
      _ProcessMenu (proc);
      if (ImGui::IsItemHovered ( ))
        hoveredPID = proc.first;
      ImGui::SetCursorPos (curPos);

      ImVec4      colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg);
      std::string txtPolicy = "",
                  hovPolicy = "";
          
      if (proc.second.policy == Blacklist)
      {
        colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure);
        txtPolicy = ICON_FA_BAN;
        hovPolicy = "Process is blacklisted";
      }
      else if (proc.second.policy == Whitelist)
      {
        colPolicy = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
        txtPolicy = ICON_FA_CHECK;
        hovPolicy = "Process is whitelisted";
      }

      ImVec4 colText         =    (hoveredPID == proc.first)    ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption) :
                                                                  ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase)    ;

      ImVec4 colStatus      = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
      std::string hovStatus = "Unknown state...";

      switch (proc.second.status)
      {
      case 0: // Active Global Injection
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
        hovStatus = "Active";
        break;
      case 1: // Inert Global Injection
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase);
        hovStatus = "Inert";
        break;
      case 2: // Potentially stuck
        if (_inject.bCurrentState) {
          colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
          hovStatus = "Inert (potentially stuck)";
        } else {
          colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
          hovStatus = "Stuck (end the process to eject Special K)";
        }
        break;
      case 4: // Local Injection
        colStatus = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info);
        hovStatus = "Local";
        break;
      }

      ImGui::TextColored     (colStatus, ICON_FA_CIRCLE);
      SKIF_ImGui_SetHoverTip (hovStatus.c_str());
      ImGui::SameLine        ( );        
      ImGui::TextColored     (colPolicy, txtPolicy.c_str());
      if (!hovPolicy.empty())
        SKIF_ImGui_SetHoverTip (hovPolicy.c_str());
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 ( 65.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (colText, "%i", proc.first);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (120.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (colText, "  %s", pretty_str.c_str());
      SKIF_ImGui_SetHoverTip (pretty_str_hover);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (165.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (colText, "%s", proc.second.arch.c_str());
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (225.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (colText, "%s", proc.second.admin ? "Yes" : "No");
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (270.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (proc.second.status == 0 ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)
                                                      : colText,
                                            "%s", SK_WideCharToUTF8 (proc.second.filename).c_str());
      SKIF_ImGui_SetHoverTip (proc.second.tooltip);
      /* Disabled Detail column (cannot find any use for it)
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (495.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (colText, "%s", proc.second.details.c_str());
      if (proc.second.details.length() > 73)
        SKIF_ImGui_SetHoverTip (proc.second.details);
      */

      ImGui::PopID  ( );
    }

    if (! ImGui::IsAnyItemHovered ( ))
      hoveredPID = 0;

    ImGui::PopStyleColor ( );

    ImGui::EndChildFrame ( );
  }
    
  // Confirm prompt

  if (static_proc.first != 0)
  {
    ImGui::OpenPopup         ("SKIF Task Manager");

    ImGui::SetNextWindowSize (ImVec2 (400.0f * SKIF_ImGui_GlobalDPIScale, 0.0f));
    ImGui::SetNextWindowPos  (ImGui::GetCurrentWindow()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal ( "SKIF Task Manager", nullptr,
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_AlwaysAutoResize )
        )
    {

      ImGui::Text        ("Do you want to end");
      ImGui::SameLine    ( );
      ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), SK_WideCharToUTF8(static_proc.second.filename).c_str());
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
        SK_TerminatePID (static_proc.first, 0x0);

        static_proc = { 0, standby_record_s{} };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                              25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_proc = { 0, standby_record_s{} };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::EndPopup ( );
    }
  }
}
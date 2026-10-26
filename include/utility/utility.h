#pragma once

#include <utility/sk_utility.h>

#include <string>
#include <map>
#include <atomic>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <optional>
#include <Windows.h>
#include <wtypes.h>

#include <WinInet.h>
#include <shellapi.h>
#include <Tlhelp32.h>
#include <shobjidl_core.h>
#include <shlobj_core.h>
#include <KnownFolders.h>


// Requires Windows 10, version 1703
//        + Per Monitor v2 DPI awareness
//
// We use an opposite IF statement than
//   Microsoft's to get these defined
#if(WINVER < 0x0605)
#define WM_DPICHANGED_BEFOREPARENT      0x02E2
#define WM_DPICHANGED_AFTERPARENT       0x02E3
#define WM_GETDPISCALEDSIZE             0x02E4
#endif /* WINVER >= 0x0605 */


#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

// Stuff

enum UITab {
  UITab_None,
  UITab_Library,
  UITab_Monitor,
  UITab_Hardware,
  UITab_Settings,
  UITab_About,
  UITab_MiniMode,
  UITab_ALL      // Total number of elements in enum (technically against Microsoft's enum design guidelines, but whatever)
};

struct FileSignature {
  std::wstring          mime_type;
  std::wstring          file_extension;
  std::vector <uint8_t> signature = { };
  std::vector <uint8_t> mask      = { };

  FileSignature (std::wstring m, std::wstring e, std::vector <uint8_t> s) : mime_type(m), file_extension(e), signature(s)
  {
    // Fill the mask with 0xFF everywhere
    mask = std::vector <uint8_t> (signature.size(), 0xFF);
  };

  FileSignature (std::wstring m, std::wstring e, std::vector <uint8_t> s, std::vector <uint8_t> m2) : mime_type(m), file_extension(e), signature(s), mask(m2)
  {
    if (mask.size() != signature.size())
      throw std::invalid_argument ("different sizes for signature and mask");
  };
};

extern UITab       SKIF_Tab_Selected; // Current selected tab
extern UITab       SKIF_Tab_ChangeTo; // Tab we want to change to

extern std::vector<HANDLE> vWatchHandles[UITab_ALL];

// The main loop waits on the handles of the current tab with MsgWaitForMultipleObjects ( ), which accepts at most
//   MAXIMUM_WAIT_OBJECTS - 1 (63) handles. Always add and remove handles through these helpers, which enforce that
//     limit per tab (a watch over the limit still works when polled, but no longer wakes the main loop by itself).
constexpr size_t SKIF_MaxWatchHandles = MAXIMUM_WAIT_OBJECTS - 1;

bool            SKIF_Util_AddWatchHandle              (HANDLE handle, UITab waitTab); // UITab_ALL adds it to every tab; false if any tab was full
void            SKIF_Util_RemoveWatchHandle           (HANDLE handle, UITab waitTab); // Must be called before the handle is closed

// Generic Utilities

const char *
__cdecl         SKIF_Util_FormatStringRaw             (char    const* const _Format, ...);
std:: string    SKIF_Util_ToLower                     (std:: string_view input);
std::wstring    SKIF_Util_ToLowerW                    (std::wstring_view input);
std:: string    SKIF_Util_ToUpper                     (std:: string_view input);
std::wstring    SKIF_Util_ToUpperW                    (std::wstring_view input);
void            SKIF_Util_CleanString                 (std:: string&     input);
void            SKIF_Util_CleanStringW                (std::wstring&     input);
void            SKIF_Util_StripNulls                  (std:: string&     input);
void            SKIF_Util_StripNullsW                 (std::wstring&     input);
void            SKIF_Util_StripCarriageReturns        (std:: string&     input);
void            SKIF_Util_StripCarriageReturnsW       (std::wstring&     input);
void            SKIF_Util_TrimSpaces                  (std:: string&     input);
void            SKIF_Util_TrimSpacesW                 (std::wstring&     input);
void            SKIF_Util_TrimLeadingSpaces           (std:: string&     input);
void            SKIF_Util_TrimLeadingSpacesW          (std::wstring&     input);
void            SKIF_Util_TrimTrailingSpaces          (std:: string&     input);
void            SKIF_Util_TrimTrailingSpacesW         (std::wstring&     input);
void            SKIF_Util_TrimNewLines                (std:: string&     input);
void            SKIF_Util_TrimNewLinesW               (std::wstring&     input);
void            SKIF_Util_TrimLeadingNewlines         (std:: string&     input);
void            SKIF_Util_TrimLeadingNewlinesW        (std::wstring&     input);
void            SKIF_Util_TrimTrailingNewlines        (std:: string&     input);
void            SKIF_Util_TrimTrailingNewlinesW       (std::wstring&     input);
void            SKIF_Util_ReplaceAll                  (std:: string&     input, std:: string_view from, std:: string_view to);
void            SKIF_Util_ReplaceAllW                 (std::wstring&     input, std::wstring_view from, std::wstring_view to);
void            SKIF_Util_ReplaceAllI                 (std:: string&     input, std:: string_view from, std:: string_view to); // ASCII letters only
void            SKIF_Util_ReplaceAllIW                (std::wstring&     input, std::wstring_view from, std::wstring_view to); // Ordinal (all letters)
std::wstring    SKIF_Util_GetErrorAsWStr              (DWORD error = GetLastError ( ), HMODULE module = NULL);
void            SKIF_Util_GetErrorAsMsgBox            (std::wstring winTitle = L"Error detected", std::wstring preMsg = L"", DWORD error = GetLastError ( ), HMODULE module = NULL);
DWORD           SKIF_Util_timeGetTime                 (void);
DWORD           SKIF_Util_timeGetTime1                (void); // Original non-cached data
std::wstring    SKIF_Util_timeGetTimeAsWStr           (time_t time);
std::wstring    SKIF_Util_timeGetTimeAsWStr           (const std::wstring& format = L"H:M:s.m");
int             SKIF_Util_CompareVersionStrings       (std:: string_view string1, std:: string_view string2);
int             SKIF_Util_CompareVersionStrings       (std::wstring_view string1, std::wstring_view string2);
int             SKIF_Util_RandomInteger               (int low = 0, int high = RAND_MAX);

// Filenames

std:: string    SKIF_Util_StripInvalidFilenameChars   (std:: string  name);
std::wstring    SKIF_Util_StripInvalidFilenameChars   (std::wstring  name);
std:: string    SKIF_Util_ReplaceInvalidFilenameChars (std:: string  name,    char replacement);
std::wstring    SKIF_Util_ReplaceInvalidFilenameChars (std::wstring  name, wchar_t replacement);
std:: string    SKIF_Util_NormalizeFullPath           (std:: string  path);
std::wstring    SKIF_Util_NormalizeFullPath           (std::wstring  path);
bool            SKIF_Util_HasFileSignature            (const std::vector<char>& header, const FileSignature& signature);

// Usernames
std:: string    SKIF_Util_StripPersonalData           (std:: string input);
std::wstring    SKIF_Util_StripPersonalData           (std::wstring input);
void            SKIF_Util_Debug_LogUserNames          (void);
void            SKIF_Util_GenerateInstallGUID         (void);


// Process environment
//
// The environment of the SKIF process is shared by all of its threads, and every process SKIF starts through
//   ShellExecuteEx ( ) inherits it (that API has no environment parameter). All changes to it should go through
//     these helpers, which share one lock, so concurrent launches cannot pick up (or undo) each other's variables.

// Holds the environment lock for as long as the returned object lives, for code that changes the environment in other ways
std::unique_lock <std::recursive_mutex>
                SKIF_Util_LockEnvironment             (void);
// Sets a variable of the SKIF process (nullptr removes it) while holding the environment lock
bool            SKIF_Util_SetEnvironmentVariable      (const wchar_t* name, const wchar_t* value);

// Temporarily overrides variables of the SKIF process, e.g. around a ShellExecuteEx ( ) call. Holds the environment
//   lock for its whole lifetime, and restores every variable to its previous value (or removes it if it did not exist)
//     when it goes out of scope. An empty value removes the variable for the duration.
//
// Use it around ShellExecuteEx ( ) calls with SEE_MASK_NOASYNC, so the new process has received its copy of the
//   environment before the variables are restored, and keep the scope short, as other launches wait for it.
class SKIF_Util_ScopedEnvironment
{
public:
  explicit SKIF_Util_ScopedEnvironment (const std::map <std::wstring, std::wstring>& overrides);
          ~SKIF_Util_ScopedEnvironment (void);

  SKIF_Util_ScopedEnvironment            (const SKIF_Util_ScopedEnvironment&) = delete;
  SKIF_Util_ScopedEnvironment& operator= (const SKIF_Util_ScopedEnvironment&) = delete;

private:
  std::unique_lock <std::recursive_mutex>                                   _lock;
  std::vector      <std::pair <std::wstring, std::optional <std::wstring>>> _previous;
};


// ShellExecute

// Struct used to hold monitored data populated by worker threads
struct SKIF_Util_CreateProcess_s {
  uint32_t            id            =  0; // App ID
  int                 store_id      = -1;
  std::atomic<HANDLE> hWorkerThread = NULL; // Holds a handle to SKIF's worker thread servicing the request
  std::atomic<HANDLE> hProcess      = NULL; // Holds a handle to the spawned process
  std::atomic<DWORD>  dwProcessId   =  0;
  std::atomic<int>    iReturnCode   = -1; // Could the separate process be spawned through CreateProcess ? (0 == NO_ERROR; 1+ == ERROR_xxx)
};

HINSTANCE       SKIF_Util_ExplorePath                 (std::wstring path);
HINSTANCE       SKIF_Util_OpenURI                     (const std::wstring& path, int nShow = SW_SHOWNORMAL, LPCWSTR verb      = L"OPEN", LPCWSTR parameters = NULL, LPCWSTR directory = NULL, UINT flags = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS);
bool            SKIF_Util_ShellExecuteUnelevated      (const std::wstring& path, int nShow = SW_SHOWNORMAL, LPCWSTR verb      = L"OPEN", LPCWSTR parameters = NULL, LPCWSTR directory = NULL);
bool            SKIF_Util_CreateProcess               (const std::wstring& path, const std::wstring& parameters, const std::wstring& directory, std::map<std::wstring, std::wstring>* env = nullptr, SKIF_Util_CreateProcess_s* proc = nullptr);


// Windows

typedef struct _SKIF_MEMORY_PRIORITY_INFORMATION {
  ULONG MemoryPriority;
} SKIF_MEMORY_PRIORITY_INFORMATION, *SKIF_PMEMORY_PRIORITY_INFORMATION;

typedef int PEBitness;  // -> enum PEBitness_

enum PEBitness_
{
  PEBitness_Unknown = -1,
  PEBitness_Invalid =  0,
  PEBitness_32bit   =  1,
  PEBitness_64bit   =  2,
};

enum class PreferredAppMode //
{
  Default,
  AllowDark,
  ForceDark,
  ForceLight,
  Max
};

enum class SKIF_Elevation {
  NotElevated,
  Elevated,
  Unknown /* no access: likely elevated or protected */
};

HANDLE          SKIF_Util_GetCurrentProcess           (void);
HANDLE          SKIF_Util_GetCurrentProcessToken      (void);
HANDLE          SKIF_Util_GetCurrentUserToken         (void);
BOOL            SKIF_Util_TerminateProcess            (DWORD  dwProcessId, UINT uExitCode);
BOOL    WINAPI  SKIF_Util_TerminateProcess            (HANDLE  hProcess,   UINT uExitCode);
std::wstring    SKIF_Util_GetFileVersion              (const wchar_t* wszName);
std::wstring    SKIF_Util_GetSpecialKDLLVersion       (const wchar_t* wszName);
std::wstring    SKIF_Util_GetProductName              (const wchar_t* wszName);
PEBitness       SKIF_Util_GetBinaryType               (const LPCTSTR pszPathToBinary);
BOOL    WINAPI  SKIF_Util_GetLogicalProcessorInformation   (                                                 PSYSTEM_LOGICAL_PROCESSOR_INFORMATION    Buffer, PDWORD ReturnedLength);
BOOL    WINAPI  SKIF_Util_GetLogicalProcessorInformationEx (LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer, PDWORD ReturnedLength);
BOOL    WINAPI  SKIF_Util_CompactWorkingSet           (void);
BOOL    WINAPI  SKIF_Util_GetSystemCpuSetInformation  (PSYSTEM_CPU_SET_INFORMATION Information, ULONG BufferLength, PULONG ReturnedLength, HANDLE Process, ULONG Flags);
bool            SKIF_Util_SetThreadPrefersECores      (void);
bool            SKIF_Util_SetThreadPrefersPCores      (void); // The current thread; false if not a hybrid CPU
BOOL    WINAPI  SKIF_Util_SetThreadInformation        (HANDLE hThread,  THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID ThreadInformation, DWORD ThreadInformationSize);
HRESULT WINAPI  SKIF_Util_SetThreadDescription        (HANDLE hThread,  PCWSTR lpThreadDescription);
VOID    WINAPI  SKIF_Util_GetSystemTimePreciseAsFileTime (LPFILETIME lpSystemTimeAsFileTime);
BOOL    WINAPI  SKIF_Util_SetThreadSelectedCpuSets    (HANDLE hThread,  const ULONG *CpuSetIds, ULONG CpuSetIdCount);
BOOL    WINAPI  SKIF_Util_SetThreadGroupAffinity      (HANDLE hThread, const GROUP_AFFINITY* GroupAffinity, PGROUP_AFFINITY PreviousGroupAffinity);
bool            SKIF_Util_SetThreadPowerThrottling    (HANDLE threadHandle, INT state);
bool            SKIF_Util_SetThreadMemoryPriority     (HANDLE threadHandle, ULONG memoryPriority);
bool            SKIF_Util_IsHybridCPU                 (void);
bool            SKIF_Util_SetProcessPrefersECores     (void);
bool            SKIF_Util_SetProcessPrefersAnyCores   (void);
BOOL    WINAPI  SKIF_Util_SetProcessDefaultCpuSets    (HANDLE hProcess, const ULONG *CpuSetIds, ULONG CpuSetIdCount);
BOOL    WINAPI  SKIF_Util_SetProcessInformation       (HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize);
bool            SKIF_Util_SetProcessPowerThrottling   (HANDLE processHandle, INT state);
bool            SKIF_Util_SetProcessMemoryPriority    (HANDLE processHandle, ULONG memoryPriority);
DWORD   WINAPI  SKIF_Util_GetProcessImageFileNameW    (HANDLE hProcess, LPWSTR lpImageFileName, DWORD nSize);
HRESULT WINAPI  SKIF_Util_SetCurrentProcessExplicitAppUserModelID (PCWSTR AppID);
bool            SKIF_Util_IsWindows8OrGreater         (void);
bool            SKIF_Util_IsWindows8Point1OrGreater   (void);
bool            SKIF_Util_IsWindows10OrGreater        (void);
bool            SKIF_Util_IsWindows10v1607OrGreater   (void);
bool            SKIF_Util_IsWindows10v1709OrGreater   (void);
bool            SKIF_Util_IsWindows10v1803OrGreater   (void);
bool            SKIF_Util_IsWindows10v1903OrGreater   (void);
bool            SKIF_Util_IsWindows11orGreater        (void);
bool            SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
bool            SKIF_Util_IsWindowsVersionExactly     (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
bool            SKIF_Util_IsTouchCapable              (void);
SKIF_Elevation  SKIF_Util_GetProcessElevation         (DWORD PID);
bool            SKIF_Util_IsProcessAdmin              (DWORD PID);
bool            SKIF_Util_IsProcessX86                (HANDLE process);
PROCESSENTRY32W SKIF_Util_FindProcessByName           (const wchar_t* wszName);
DWORD           SKIF_Util_GetProcessIdFromHwnd        (HWND hwnd);
HANDLE          SKIF_Util_GetProcessHandleFromHwnd    (HWND hwnd, DWORD dwDesiredAccess);
bool            SKIF_Util_SaveImageAsICO              (const wchar_t* imgFilePath, const wchar_t* icoFilePath, int iColorBits);
bool            SKIF_Util_SaveExtractExeIcon          (std::wstring exePath, std::wstring targetPath);
bool            SKIF_Util_GetDragFromMaximized        (bool refresh = false);
bool            SKIF_Util_GetControlledFolderAccess   (void);
int             SKIF_Util_RegisterApp                 (bool force   = false);
bool            SKIF_Util_IsMPOsDisabledInRegistry    (bool refresh = false);
void            SKIF_Util_GetMonitorHzPeriod          (HWND hwnd, DWORD dwFlags, DWORD& dwPeriod);
bool            SKIF_Util_SetClipboardData            (const std::wstring_view& data);
std::wstring    SKIF_Util_AddEnvironmentBlock         (const void* pEnvBlock, const std::wstring& varName, const std::wstring& varValue);
std::wstring    SKIF_Util_AddEnvironmentBlock         (const void* pEnvBlock, const std::map<std::wstring, std::wstring>& vars);
void            SKIF_Util_FileExplorer_SelectFile     (PCWSTR filePath);
bool            SKIF_Util_FileExplorer_DeleteFile     (PCWSTR filePath, bool hideWarning);
void            SKIF_Util_FileExplorer_ContextMenuFile(PCWSTR filePath, HWND hWndOwner);
HRESULT         SKIF_Util_FileExplorer_SaveFile       (LPWSTR *pszPath, HWND hwndOwner, std::vector<COMDLG_FILTERSPEC> fileTypes = { }, FILEOPENDIALOGOPTIONS dialogOptions = _FILEOPENDIALOGOPTIONS::FOS_FILEMUSTEXIST, const GUID defaultFolder = FOLDERID_ComputerFolder, PCWSTR defaultFolderPath = nullptr, PCWSTR defaultExtension = nullptr);
HRESULT         SKIF_Util_FileExplorer_BrowseForFile  (LPWSTR *pszPath, HWND hWndOwner, std::vector<COMDLG_FILTERSPEC> fileTypes = { }, FILEOPENDIALOGOPTIONS dialogOptions = _FILEOPENDIALOGOPTIONS::FOS_FILEMUSTEXIST, const GUID defaultFolder = FOLDERID_ComputerFolder, PCWSTR defaultFolderPath = nullptr);
HRESULT         SKIF_Util_FileExplorer_BrowseForFolder(LPWSTR *pszPath, HWND hWndOwner, const GUID defaultFolder = FOLDERID_ComputerFolder, PCWSTR defaultFolderPath = nullptr);
std::wstring    SKIF_Util_FileExplorer_BrowseForFolderXP(PCWSTR defaultPath);
bool            SKIF_Util_Files_PruneOlderThan        (std::wstring path, ULONGLONG secondsSince);
bool            SKIF_Util_Files_PruneToLatestN        (std::wstring path, size_t filesToRetain);
bool            SKIF_Util_AllowDarkModeForApp         (bool allow);
PreferredAppMode SKIF_Util_SetPreferredAppMode        (PreferredAppMode mode);
void            SKIF_Util_UpdateAppColorMode          (void);
HRESULT         SKIF_Util_ExtractFromZip              (PCWSTR zipPath, PCWSTR destDir, std::vector <std::pair <std::wstring, HRESULT>>* innerPaths);
HRESULT WINAPI  SKIF_Util_SHGetKnownFolderItem        (REFKNOWNFOLDERID rfid, KNOWN_FOLDER_FLAG flags, HANDLE hToken, REFIID riid, void** ppv);
std::string     SKIF_Util_GetWindowMessageAsStr       (UINT msg);


// Power Mode

std::string     SKIF_Util_GetEffectivePowerMode              (void);
void            SKIF_Util_SetEffectivePowerModeNotifications (bool enable);


// High Dynamic Range (HDR)

LONG    WINAPI  SKIF_Util_DisplayConfigSetDeviceInfo  (DISPLAYCONFIG_DEVICE_INFO_HEADER *setPacket);
LONG    WINAPI  SKIF_Util_DisplayConfigGetDeviceInfo  (DISPLAYCONFIG_DEVICE_INFO_HEADER *getPacket);
LONG    WINAPI  SKIF_Util_GetDisplayConfigBufferSizes (UINT32 flags, UINT32 *numPathArrayElements, UINT32 *numModeInfoArrayElements);
LONG    WINAPI  SKIF_Util_QueryDisplayConfig          (UINT32 flags, UINT32 *numPathArrayElements, DISPLAYCONFIG_PATH_INFO *pathArray, UINT32 *numModeInfoArrayElements, DISPLAYCONFIG_MODE_INFO *modeInfoArray, DISPLAYCONFIG_TOPOLOGY_ID *currentTopologyId);
bool            SKIF_Util_IsHDRSupported              (HMONITOR hMonitor);
bool            SKIF_Util_IsHDRActive                 (HMONITOR hMonitor);
float           SKIF_Util_GetSDRWhiteLevel            (HMONITOR hMonitor);
bool            SKIF_Util_EnableHDROutput             (void);
void            SKIF_Util_UpdateMonitors              (void); // Triggers a refresh of the data that feeds all of the above; called by WM_DISPLAYCHANGE


// Global hotkeys

bool            SKIF_Util_RegisterHotKeyHDRToggle     (const SK_Keybind* binding);
bool            SKIF_Util_UnregisterHotKeyHDRToggle   (void);
bool            SKIF_Util_GetHotKeyStateHDRToggle     (void);
bool            SKIF_Util_RegisterHotKeySVCTemp       (const SK_Keybind* binding);
bool            SKIF_Util_UnregisterHotKeySVCTemp     (void);
bool            SKIF_Util_GetHotKeyStateSVCTemp       (void);


// Web

struct skif_get_web_uri_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH]      = { };
  wchar_t wszExtraInfo[INTERNET_MAX_PATH_LENGTH]      = { };
  wchar_t wszLocalPath[MAX_PATH + 2]                  = { };
  LPCWSTR method                                      = L"GET";
  bool         https                                  = false;
  std::string  body                                   = { };
  std::wstring header                                 = { };
  std::wstring user_agent                             = L"Special K - Asset Crawler";
};

DWORD        SKIF_Util_GetWebResource         (std::wstring url, std::wstring_view file_path, std::wstring method = L"GET", std::wstring header = L"", std::string body = "", std::wstring user_agent = L"", std::string* response_body = nullptr);
skif_get_web_uri_t SKIF_Util_CrackWebUrl      (const std::wstring  url);
std:: string SKIF_Util_URLEncode              (const std:: string& url);
std::wstring SKIF_Util_URLEncode              (const std::wstring& url);


// Shortcuts (*.lnk)

void         SKIF_Util_ResolveShortcut       (HWND hwnd, LPCWSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, LPWSTR lpszWorkingDir, int iPathBufferSize);
bool         SKIF_Util_CreateShortcut        (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs = L"\0", LPCWSTR lpszWorkDir = L"\0", LPCWSTR lpszDesc = L"\0", LPCWSTR lpszIconLocation = L"\0", int iIcon = 0);


// Directory Watch

// Both of these flags are required to properly detect new files when they have finished writing as well as deleted files
// FILE_NOTIFY_CHANGE_FILE_NAME  - Detects when files has been created, renamed, or deleted
// FILE_NOTIFY_CHANGE_LAST_WRITE - Detects when processes have finished writing to a file

struct SKIF_DirectoryWatch
{
  SKIF_DirectoryWatch  (void) { };
  SKIF_DirectoryWatch  (std::wstring_view wstrPath,
                                    UITab waitTab        = UITab_None,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
  ~SKIF_DirectoryWatch (void);

  // Copy: creates a fresh, independent watch on the same path.
  // The source's handle is left untouched.
  SKIF_DirectoryWatch            (const SKIF_DirectoryWatch&  other);
  SKIF_DirectoryWatch& operator= (const SKIF_DirectoryWatch&  other);

  // Move: takes ownership of the source's handle.
  SKIF_DirectoryWatch            (      SKIF_DirectoryWatch&& other) noexcept;
  SKIF_DirectoryWatch& operator= (      SKIF_DirectoryWatch&& other) noexcept;

  std::wstring getPath (void) const;
  bool isActive        (void) const;
  bool isSignaled      (void) const;
  bool isSignaled      (std::wstring_view wstrPath,
                                    UITab waitTab        = UITab_None,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
  void reset           (void);


private:
  void registerNotify  (std::wstring_view wstrPath,
                                    UITab waitTab        = UITab_None,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);

  HANDLE       _hChangeNotification = INVALID_HANDLE_VALUE; // If the FindFirstChangeNotification function fails, the return value is INVALID_HANDLE_VALUE.
  UITab        _waitTab             = UITab_None;
  std::wstring _path                = L"";
  BOOL         _watchSubtree        = FALSE;
  DWORD        _notifyFilter        = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;
};


// Registry Watch

struct SKIF_RegistryWatch {
   SKIF_RegistryWatch (HKEY hRootKey,
             const wchar_t* wszSubKey,
             const wchar_t* wszEventName,
                       BOOL bWatchSubtree  = TRUE,
                      DWORD dwNotifyFilter = REG_NOTIFY_CHANGE_LAST_SET,
                      UITab waitTab        = UITab_None,
                       bool bWOW6432Key    = false,  // Access a 32-bit key from either a 32-bit or 64-bit application.
                       bool bWOW6464Key    = false); // Access a 64-bit key from either a 32-bit or 64-bit application.

  ~SKIF_RegistryWatch    (void);

  SKIF_RegistryWatch            (const SKIF_RegistryWatch&  other);
  SKIF_RegistryWatch& operator= (const SKIF_RegistryWatch&  other);
  SKIF_RegistryWatch            (      SKIF_RegistryWatch&& other) noexcept;
  SKIF_RegistryWatch& operator= (      SKIF_RegistryWatch&& other) noexcept;

  bool isSignaled        (void);

  // Read-only access to the watched key, e.g. to read values from the same key that is being watched
  HKEY                getKey       (void) const; // The opened key, or NULL if it could not be opened
  HKEY                getRootKey   (void) const;
  const std::wstring& getSubKey    (void) const;
  bool                isWOW6432Key (void) const;
  bool                isWOW6464Key (void) const;

private:
  LSTATUS registerNotify (void);
  void    reset          (void);

  struct {
    HKEY         root          = NULL;
    std::wstring sub_key;
    std::wstring event_name;            // Base name given by the caller (see _eventNameActual)
    BOOL         watch_subtree = FALSE;
    DWORD        filter_mask   = 0;
    BOOL         wow64_32key   = FALSE; // Access a 32-bit key from either a 32-bit or 64-bit application.
    BOOL         wow64_64key   = FALSE; // Access a 64-bit key from either a 32-bit or 64-bit application.
  } _init;

  HKEY         _hKeyBase        = NULL;
  HANDLE       _hEvent          = NULL; // If the CreateEvent function fails, the return value is NULL.
  UITab        _waitTab         = UITab_None;
  std::wstring _eventNameActual = L"";  // Named, so the event is easy to identify in e.g. Process Explorer (made unique per object)

  void swap (SKIF_RegistryWatch& other) noexcept;
};
#pragma once
#include <wtypes.h>
#include <string>
#include <wininet.h>
#include <atlbase.h>


// Generic Utilities

std::wstring SKIF_Util_TowLower              (std::wstring_view wstr);
std::wstring SKIF_Util_GetLastError          (void);
DWORD        SKIF_Util_timeGetTime           (void);


// Filenames

std::string  SKIF_Util_StripInvalidFilenameChars   ( std::string name);
std::wstring SKIF_Util_StripInvalidFilenameChars   (std::wstring name);
std::string  SKIF_Util_ReplaceInvalidFilenameChars ( std::string name,    char replacement);
std::wstring SKIF_Util_ReplaceInvalidFilenameChars (std::wstring name, wchar_t replacement);


// ShellExecute

HINSTANCE    SKIF_Util_OpenURI                     (const std::wstring_view& path, DWORD dwAction = SW_SHOWNORMAL, LPCWSTR verb = L"OPEN", LPCWSTR parameters = L"");
HINSTANCE    SKIF_Util_ExplorePath                 (const std::wstring_view& path);
HINSTANCE    SKIF_Util_ExplorePath_Formatted       (                const wchar_t* const wszFmt, ...);
HINSTANCE    SKIF_Util_OpenURI_Formatted           (DWORD dwAction, const wchar_t* const wszFmt, ...);
void         SKIF_Util_OpenURI_Threaded            (                const LPCWSTR path);


// Time


// Windows

BOOL WINAPI  SKIF_Util_CompactWorkingSet           (void);
BOOL         SKIF_Util_IsWindows8Point1OrGreater   (void);
BOOL         SKIF_Util_IsWindows10OrGreater        (void);
BOOL         SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);


// Web

struct skif_get_web_uri_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH] = { };
  wchar_t wszLocalPath[MAX_PATH + 2] = { };
  LPCWSTR method = L"GET";
  std::string body;
  std::wstring header;
};

DWORD WINAPI SKIF_Util_GetWebUri              (skif_get_web_uri_t* get);
void         SKIF_Util_GetWebResource         (std::wstring url, std::wstring_view destination, std::wstring method = L"GET", std::wstring header = L"", std::string body = "");
void         SKIF_Util_GetWebResourceThreaded (std::wstring url, std::wstring_view destination);


// Directory Watch

struct SKIF_DirectoryWatch
{
  ~SKIF_DirectoryWatch (void);

  bool isSignaled (std::wstring_view path);

  HANDLE hChangeNotification = INVALID_HANDLE_VALUE;
};


// Registry Watch

struct SKIF_RegistryWatch {
  SKIF_RegistryWatch ( HKEY   hRootKey,
              const wchar_t* wszSubKey,
              const wchar_t* wszEventName,
                        BOOL bWatchSubtree  = TRUE,
                       DWORD dwNotifyFilter = REG_NOTIFY_CHANGE_LAST_SET );

  void registerNotify (void);
  void reset          (void);
  bool isSignaled     (void);

  struct {
    HKEY         root        = { };
    std::wstring sub_key;
    BOOL         watch_subtree;
    DWORD        filter_mask;
  } _init;

  CRegKey hKeyBase;
  CHandle hEvent;
};


// Shortcuts (*.lnk)

void         SKIF_Util_ResolveShortcut       (HWND hwnd, LPCSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize);
bool         SKIF_Util_CreateShortcut        (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs = L"\0", LPCWSTR lpszWorkDir = L"\0", LPCWSTR lpszDesc = L"\0", LPCWSTR lpszIconLocation = L"\0", int iIcon = 0);

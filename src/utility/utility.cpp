#include <utility/utility.h>
#include <utility/sk_utility.h>
#include <comdef.h>
#include <gsl/gsl_util>
#include <Psapi.h>
#include <cwctype>
#include <unordered_set>
#include <ShlObj.h>
#include <strsafe.h>
#include <filesystem>
#include <DbgHelp.h>
#include <gdiplus.h>
#include <UserEnv.h>

#pragma comment (lib, "Gdiplus.lib")

#include <SKIF.h>
#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>

std::pair<UITab, std::vector<HANDLE>> vWatchHandles[UITab_COUNT];

bool bHotKeyHDR = false,
     bHotKeySVC = false;

CRITICAL_SECTION CriticalSectionDbgHelp = { };

// Generic Utilities

std::string
SKIF_Util_ToLower      (std::string_view input)
{
  std::string copy = std::string(input);
  std::transform (copy.begin(), copy.end(), copy.begin(), [](char c) { return std::tolower(c, std::locale{}); });
  return copy;
}

std::wstring
SKIF_Util_ToLowerW     (std::wstring_view input)
{
  std::wstring    copy = std::wstring(input);
  std::transform (copy.begin(), copy.end(), copy.begin(), [](wchar_t c) { return std::towlower(c); });
  return copy;
}

std::string
SKIF_Util_ToUpper      (std::string_view input)
{
  std::string copy = std::string(input);
  std::transform (copy.begin(), copy.end(), copy.begin(), [](char c) { return std::toupper(c, std::locale{}); });
  return copy;
}

std::wstring
SKIF_Util_ToUpperW     (std::wstring_view input)
{
  std::wstring copy = std::wstring(input);
  std::transform (copy.begin(), copy.end(), copy.begin(), [](wchar_t c) { return std::towupper(c); });
  return copy;
}

// Handles System Error Codes, https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes#system-error-codes
//                             https://learn.microsoft.com/en-us/windows/win32/wininet/appendix-c-handling-errors
std::wstring
SKIF_Util_GetErrorAsWStr (DWORD error, HMODULE module)
{
  LPWSTR messageBuffer = nullptr;

  size_t size = FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM     |
                                FORMAT_MESSAGE_IGNORE_INSERTS  |
              (module != NULL ? FORMAT_MESSAGE_FROM_HMODULE    : 0x0),
                                module, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);

  std::wstring message (messageBuffer, size);
  LocalFree (messageBuffer);

  message.erase (std::remove (message.begin(), message.end(), '\n'), message.end());

  message = L"[" + std::to_wstring(error) + L"] " + message;

  return message;
}

void
SKIF_Util_GetErrorAsMsgBox (std::wstring winTitle, std::wstring preMsg, DWORD error, HMODULE module)
{
  std::wstring message = SKIF_Util_GetErrorAsWStr (error, module);

  if (! preMsg.empty())
    preMsg += L"\n\n";

  message = preMsg + message;

  MessageBox (NULL, message.c_str(),
                    winTitle.c_str(), MB_OK | MB_ICONERROR);
}

DWORD
SKIF_Util_timeGetTime (void)
{
  static LARGE_INTEGER qpcFreq = { };
         LARGE_INTEGER li      = { };

  /*
  using timeGetTime_pfn =
          DWORD (WINAPI *)(void);
  static timeGetTime_pfn
   winmm_timeGetTime     = nullptr;

  if (  winmm_timeGetTime == nullptr || qpcFreq.QuadPart == 1)
  {
    if (winmm_timeGetTime == nullptr)
    {
      HMODULE hModWinMM =
        LoadLibraryEx ( L"winmm.dll", nullptr,
                          LOAD_LIBRARY_SEARCH_SYSTEM32 );
        winmm_timeGetTime =
             (timeGetTime_pfn)GetProcAddress (hModWinMM,
             "timeGetTime"                   );
    }

    return winmm_timeGetTime != nullptr ?
           winmm_timeGetTime ()         : static_cast <DWORD> (-1);
  }
  */

  if ( qpcFreq.QuadPart == 1)
  {
    return timeGetTime ();
  }

  if (QueryPerformanceCounter (&li))
  {
    if (qpcFreq.QuadPart == 0 && QueryPerformanceFrequency (&qpcFreq) == FALSE)
    {   qpcFreq.QuadPart  = 1;

      return rand ();
    }

    return
      static_cast <DWORD> ( li.QuadPart /
                      (qpcFreq.QuadPart / 1000ULL) );
  }

  return static_cast <DWORD> (-1);
}


// A function that returns the current time as a string in a custom format
std::wstring
SKIF_Util_timeGetTimeAsWStr (const std::wstring& format)
{
  std::wostringstream woss;

  // Get current time
  auto now_tp = std::chrono::system_clock::now();

  // Milliseconds
  auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(now_tp.time_since_epoch()) % 1000;

  // Hours, Minutes, Seconds
  std::time_t t       = std::chrono::system_clock::to_time_t(now_tp);
  std::tm* local_now  = std::localtime(&t);

  for (wchar_t c : format) {
    switch (c) {
      case L'H':
        woss << std::setfill(L'0') << std::setw(2) << local_now->tm_hour;
        break;
      case L'M':
        woss << std::setfill(L'0') << std::setw(2) << local_now->tm_min;
        break;
      case L's':
        woss << std::setfill(L'0') << std::setw(2) << local_now->tm_sec;
        break;
      case L'm':
        woss << std::setfill(L'0') << std::setw(3) << ms.count();
        break;
      default: // Append the rest as is (e.g. separator)
        woss << c;
    }
  }

  return woss.str();
}


// Handles comparisons of a version string split between dots by
// looping through the parts that makes up the string one by one.
// 
// Outputs:
//  1 = if string1 is more than string2
//  0 = if both strings are equal
// -1 = if string1 is less than string2
//  
// Basically https://www.geeksforgeeks.org/compare-two-version-numbers/
int
SKIF_Util_CompareVersionStrings (std::string string1, std::string string2)
{
  int sum1 = 0, sum2 = 0;

  for ( size_t i = 0, j = 0; (i < string1.length ( ) ||
                              j < string2.length ( )); )
  {
    while ( i < string1.length() && string1[i] != '.' )
    {
      sum1 = sum1 * 10 + (string1[i] - '0');
      i++;
    }

    while ( j < string2.length() && string2[j] != '.' )
    {
      sum2 = sum2 * 10 + (string2[j] - '0');
      j++;
    }

    // If string1 is higher than string2, return 1
    if (sum1 > sum2) return 1;

    // If string2 is higher than string1, return -1
    if (sum2 > sum1) return -1;

    // if equal, reset variables and go for next numeric part 
    sum1 = sum2 = 0;
    i++;
    j++;
  }

  // If both strings are equal, return 0
  return 0; 
}

int
SKIF_Util_CompareVersionStrings (std::wstring string1, std::wstring string2)
{
  int sum1 = 0, sum2 = 0;

  for ( size_t i = 0, j = 0; (i < string1.length ( ) ||
                              j < string2.length ( )); )
  {
    while ( i < string1.length() && string1[i] != '.' )
    {
      sum1 = sum1 * 10 + (string1[i] - '0');
      i++;
    }

    while ( j < string2.length() && string2[j] != '.' )
    {
      sum2 = sum2 * 10 + (string2[j] - '0');
      j++;
    }

    // If string1 is higher than string2, return 1
    if (sum1 > sum2) return 1;

    // If string2 is higher than string1, return -1
    if (sum2 > sum1) return -1;

    // if equal, reset variables and go for next numeric part 
    sum1 = sum2 = 0;
    i++;
    j++;
  }

  // If both strings are equal, return 0
  return 0; 
}


// Filenames

std::wstring
SKIF_Util_StripInvalidFilenameChars (std::wstring name)
{
  // Non-trivial name = custom path, remove the old-style <program.exe>
  if (! name.empty())
  {
    name.erase ( std::remove_if ( name.begin (),
                                  name.end   (),

                                    [](wchar_t tval)
                                    {
                                      static
                                      const std::unordered_set <wchar_t>
                                        invalid_file_char =
                                        {
                                          L'\\', L'/', L':',
                                          L'*',  L'?', L'\"',
                                          L'<',  L'>', L'|',
                                        //L'&',

                                          //
                                          // Obviously a period is not an invalid character,
                                          //   but three of them in a row messes with
                                          //     Windows Explorer and some Steam games use
                                          //       ellipsis in their titles.
                                          //
                                          L'.'
                                        };

                                      return
                                        ( invalid_file_char.find (tval) !=
                                          invalid_file_char.end  (    ) );
                                    }
                                ),

                     name.end ()
               );

    // Strip trailing spaces from name, these are usually the result of
    //   deleting one of the non-useable characters above.
    for (auto it = name.rbegin (); it != name.rend (); ++it)
    {
      if (*it == L' ') *it = L'\0';
      else                   break;
    }
  }

  return name;
}

std::string
SKIF_Util_StripInvalidFilenameChars (std::string name)
{
  // Non-trivial name = custom path, remove the old-style <program.exe>
  if (! name.empty())
  {
    name.erase ( std::remove_if ( name.begin (),
                                  name.end   (),

                                    [](char tval)
                                    {
                                      static
                                      const std::unordered_set <char>
                                        invalid_file_char =
                                        {
                                          '\\', '/', ':',
                                          '*',  '?', '\"',
                                          '<',  '>', '|',
                                        //L'&',

                                          //
                                          // Obviously a period is not an invalid character,
                                          //   but three of them in a row messes with
                                          //     Windows Explorer and some Steam games use
                                          //       ellipsis in their titles.
                                          //
                                          '.'
                                        };

                                      return
                                        ( invalid_file_char.find (tval) !=
                                          invalid_file_char.end  (    ) );
                                    }
                                ),

                     name.end ()
               );

    // Strip trailing spaces from name, these are usually the result of
    //   deleting one of the non-useable characters above.
    for (auto it = name.rbegin (); it != name.rend (); ++it)
    {
      if (*it == ' ') *it = '\0';
      else                   break;
    }
  }

  return name;
}


std::wstring
SKIF_Util_ReplaceInvalidFilenameChars (std::wstring name, wchar_t replacement)
{
  if (! name.empty())
  {
    std::replace_if ( name.begin (),
                      name.end   (),

                        [](wchar_t tval)
                        {
                          static
                          const std::unordered_set <wchar_t>
                            invalid_file_char =
                            {
                              '\\', '/', ':',
                              '*',  '?', '\"',
                              '<',  '>', '|',
                            //L'&',

                              //
                              // Obviously a period is not an invalid character,
                              //   but three of them in a row messes with
                              //     Windows Explorer and some Steam games use
                              //       ellipsis in their titles.
                              //
                              '.'
                            };

                          return
                            ( invalid_file_char.find (tval) !=
                              invalid_file_char.end  (    ) );
                        }
                    , replacement
                   );
  }

  return name;
}


std::string
SKIF_Util_ReplaceInvalidFilenameChars (std::string name, char replacement)
{
  if (! name.empty())
  {
    std::replace_if ( name.begin (),
                      name.end   (),

                        [](char tval)
                        {
                          static
                          const std::unordered_set <char>
                            invalid_file_char =
                            {
                              '\\', '/', ':',
                              '*',  '?', '\"',
                              '<',  '>', '|',
                            //L'&',

                              //
                              // Obviously a period is not an invalid character,
                              //   but three of them in a row messes with
                              //     Windows Explorer and some Steam games use
                              //       ellipsis in their titles.
                              //
                              '.'
                            };

                          return
                            ( invalid_file_char.find (tval) !=
                              invalid_file_char.end  (    ) );
                        }
                    , replacement
                   );
  }

  return name;
}


// ShellExecute

HINSTANCE
SKIF_Util_ExplorePath (
  const std::wstring_view& path )
{
  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"EXPLORE";
    sexi.lpFile       = path.data ();
    sexi.nShow        = SW_SHOWNORMAL;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

HINSTANCE
SKIF_Util_OpenURI (
  const std::wstring_view& path,
               int         nShow,
               LPCWSTR     verb,
               LPCWSTR     parameters,
               LPCWSTR     directory,
               UINT        flags)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  HINSTANCE ret   = 0;

  if ((flags & SEE_MASK_NOASYNC) == 0x0 &&
      (flags & SEE_MASK_ASYNCOK) == 0x0)
    flags |= 
      ((_registry._LoadedSteamOverlay) ? SEE_MASK_NOASYNC    //  Synchronous - Required for the SetEnvironmentVariable() calls to be respected
                                       : SEE_MASK_ASYNCOK ); // Asynchronous - It is fine to defer loading the new process until later

  //UINT flags =   SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS

#if 1
  PLOG_VERBOSE                           << "Performing a ShellExecute call...";
  PLOG_VERBOSE_IF(! path.empty())        << "File      : " << path;
  PLOG_VERBOSE_IF(verb       != nullptr) << "Verb      : " << std::wstring(verb);
  PLOG_VERBOSE_IF(parameters != nullptr) << "Parameters: " << std::wstring(parameters);
  PLOG_VERBOSE_IF(directory  != nullptr) << "Directory : " << std::wstring(directory);
//PLOG_VERBOSE                           << "Flags     : " << flags;
#endif

  if (_registry._LoadedSteamOverlay)
    SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);
  
  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = verb;
    sexi.lpFile       = path.data ();
    sexi.lpParameters = parameters;
    sexi.lpDirectory  = directory;
    sexi.nShow        = nShow;
    sexi.fMask        = flags;
    // Note that any new process will inherit SKIF's environment variables

  if (ShellExecuteExW (&sexi))
  {
    PLOG_VERBOSE << "The operation was successful.";
    ret = sexi.hInstApp;
  }

  else {
    PLOG_VERBOSE << "The operation failed!";
  }

  if (_registry._LoadedSteamOverlay)
    SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");

  return ret;
}

bool
SKIF_Util_CreateProcess (
                     const std::wstring_view& path,
                                     LPCWSTR  parameters,
                                     LPCWSTR  directory,
        std::map<std::wstring, std::wstring>* env,
                   SKIF_Util_CreateProcess_s* proc)
{
  struct thread_s {
    std::wstring path          = L"";
    std::wstring parameters    = L"";
    std::wstring directory     = L"";
    std::map<std::wstring, std::wstring> env;
    SKIF_Util_CreateProcess_s* proc;
  };
  
  thread_s* data = new thread_s;

  data->path       = path;
  data->parameters = parameters;
  data->directory  = directory;
  data->env        = *env;
  data->proc       = proc;

  uintptr_t hWorkerThread =
    _beginthreadex (nullptr, 0x0, [](void * var) -> unsigned
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_CreateProcessWorker");

      SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

      PLOG_DEBUG << "SKIF_CreateProcessWorker thread started!";

      thread_s* _data = static_cast<thread_s*>(var);

      PROCESS_INFORMATION procinfo = { };
      STARTUPINFO         supinfo  = { };
      SecureZeroMemory  (&supinfo,   sizeof (STARTUPINFO));
      supinfo.cb                   = sizeof (STARTUPINFO);
      
      LPVOID lpEnvBlock            = nullptr;
      std::wstring wsEnvBlock;
      
      // Create a clear and empty environment block for the current user
      CreateEnvironmentBlock (&lpEnvBlock, SKIF_Util_GetCurrentProcessToken(), FALSE);

      // Convert to a nicely stored wstring
      wsEnvBlock   = SKIF_Util_AddEnvironmentBlock (lpEnvBlock, L"", L"");

      // Add any custom variables to it
      for (auto& env_var : _data->env)
        wsEnvBlock = SKIF_Util_AddEnvironmentBlock (wsEnvBlock.c_str(), env_var.first, env_var.second);

      // Destroy the block once we are done with it
      DestroyEnvironmentBlock (lpEnvBlock);
      
    //PLOG_VERBOSE                                 << "Performing a CreateProcess call...";
      PLOG_VERBOSE_IF(! _data->path.empty())       << "File        : " << _data->path;
      PLOG_VERBOSE_IF(! _data->parameters.empty()) << "Parameters  : " << _data->parameters;
      PLOG_VERBOSE_IF(! _data->directory .empty()) << "Directory   : " << _data->directory;
      PLOG_VERBOSE_IF(! _data->env       .empty()) << "Environment : " << _data->env;

      if (CreateProcessW (
          _data->path.c_str(),
          const_cast<wchar_t *>(_data->parameters.c_str()),
          NULL,
          NULL,
          FALSE,
          NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT,
          const_cast<wchar_t *>(wsEnvBlock.c_str()), // Environment variable block
          _data->directory.c_str(),
          &supinfo,
          &procinfo))
      {
        if (_data->proc != nullptr)
          _data->proc->hProcess.store (procinfo.hProcess);
        else // We don't have a proc structure, so the handle is unneeded
          CloseHandle (procinfo.hProcess);

        // Close the external thread handle as we do not use it for anything
        CloseHandle (procinfo.hThread);
      }

      else
        PLOG_VERBOSE << "CreateProcess failed: " << SKIF_Util_GetErrorAsWStr ( );

      // Free up the memory we allocated
      delete _data;

      PLOG_DEBUG << "SKIF_CreateProcessWorker thread stopped!";

      return 0;
    }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != 0);

  if (threadCreated && proc != nullptr)
    proc->hWorkerThread.store (reinterpret_cast<HANDLE>(hWorkerThread));
  else if (threadCreated) // We don't have a proc structure, so the handle is unneeded
    CloseHandle (reinterpret_cast<HANDLE>(hWorkerThread));
  else // Someting went wrong during thread creation, so free up the memory we allocated earlier
    delete data;

  return threadCreated;
}


// Windows


// Returns a pseudo handle interpreted as the current process handle
HANDLE
SKIF_Util_GetCurrentProcess (void)
{
  // A pseudo handle is a special constant, currently (HANDLE)-1, that is interpreted as the current process handle.
  // For compatibility with future operating systems, it is best to call GetCurrentProcess instead of hard-coding this constant value.
  static HANDLE
         handle = GetCurrentProcess ( );
  return handle;
}

// Returns a pseudo handle interpreted as the access token associated with a process
HANDLE
SKIF_Util_GetCurrentProcessToken (void)
{
  // A pseudo handle is a special constant, currently (HANDLE)-4, that is interpreted as the current process access token.
  // https://github.com/microsoft/win32metadata/issues/436
  return (HANDLE)(LONG_PTR) -4;
}

// Terminates the process with the given process ID
BOOL
SKIF_Util_TerminateProcess (DWORD dwProcessId, UINT uExitCode)
{
  CHandle hProcess (
    OpenProcess (PROCESS_TERMINATE, FALSE, dwProcessId)
  );

  return
    SKIF_Util_TerminateProcess (hProcess, uExitCode);
}

// Terminates the process of the given handle
BOOL
SKIF_Util_TerminateProcess (HANDLE hProcess, UINT uExitCode)
{
  if (hProcess == INVALID_HANDLE_VALUE)
    return FALSE;
   
  return
    TerminateProcess (hProcess, uExitCode);
}


using VerQueryValueW_pfn        = BOOL (APIENTRY *)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
using GetFileVersionInfoExW_pfn = BOOL (APIENTRY *)(DWORD,LPCWSTR,DWORD,DWORD,LPVOID);

std::wstring
SKIF_Util_GetSpecialKDLLVersion (const wchar_t* wszName)
{
  if (! wszName)
    return L"";

  /*
  static VerQueryValueW_pfn
    SKIF_VerQueryValueW = (VerQueryValueW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "VerQueryValueW"                                     );

  static GetFileVersionInfoExW_pfn
    SKIF_Util_GetFileVersionInfoExW = (GetFileVersionInfoExW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "GetFileVersionInfoExW"                                            );
  */

  UINT cbTranslatedBytes = 0,
       cbProductBytes    = 0,
       cbVersionBytes    = 0;

  std::vector <uint8_t>
    cbData (16384, 0ui8);

  wchar_t* wszProduct    = nullptr; // Will point somewhere in cbData
  wchar_t* wszVersion    = nullptr;

  struct LANGANDCODEPAGE {
    WORD wLanguage;
    WORD wCodePage;
  } *lpTranslate = nullptr;

  BOOL bRet =
    GetFileVersionInfoExW ( FILE_VER_GET_PREFETCHED,
                              wszName,
                                0x00,
              static_cast <DWORD> (cbData.size ()),
                                    cbData.data () );

  if (! bRet) return L"";

  if ( VerQueryValueW ( cbData.data (),
                          TEXT ("\\VarFileInfo\\Translation"),
        static_cast_p2p <void> (&lpTranslate),
                                &cbTranslatedBytes ) &&
                                  cbTranslatedBytes   && lpTranslate )
  {
    wchar_t        wszPropName [64] = { };
    _snwprintf_s ( wszPropName, 63,
                   LR"(\StringFileInfo\%04x%04x\ProductName)",
                     lpTranslate   [0].wLanguage,
                       lpTranslate [0].wCodePage );

    VerQueryValueW ( cbData.data (),
                       wszPropName,
      static_cast_p2p <void> (&wszProduct),
                              &cbProductBytes );

    if ( cbProductBytes && (StrStrIW (wszProduct, L"Special K")) )
    {
      _snwprintf_s ( wszPropName, 63,
                       LR"(\StringFileInfo\%04x%04x\ProductVersion)",
                         lpTranslate   [0].wLanguage,
                           lpTranslate [0].wCodePage );

      VerQueryValueW ( cbData.data (),
                         wszPropName,
        static_cast_p2p <void> (&wszVersion),
                                &cbVersionBytes );

      if (cbVersionBytes)
        return wszVersion;
    }
  }

  return L"";
}

std::wstring
SKIF_Util_GetFileVersion (const wchar_t* wszName)
{
  if (! wszName)
    return L"";

  /*
  static VerQueryValueW_pfn
    SKIF_VerQueryValueW = (VerQueryValueW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "VerQueryValueW"                                     );

  static GetFileVersionInfoExW_pfn
    SKIF_Util_GetFileVersionInfoExW = (GetFileVersionInfoExW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "GetFileVersionInfoExW"                                            );
  */

  UINT cbTranslatedBytes = 0,
       cbVersionBytes    = 0;

  std::vector <uint8_t>
    cbData (16384, 0ui8);

  wchar_t* wszVersion    = nullptr; // Will point somewhere in cbData

  struct LANGANDCODEPAGE {
    WORD wLanguage;
    WORD wCodePage;
  } *lpTranslate = nullptr;

  BOOL bRet =
    GetFileVersionInfoExW ( FILE_VER_GET_PREFETCHED,
                                   wszName,
                                     0x00,
                    static_cast <DWORD> (cbData.size ()),
                                         cbData.data () );

  if (! bRet) return L"";

  if ( VerQueryValueW ( cbData.data (),
                             TEXT ("\\VarFileInfo\\Translation"),
            static_cast_p2p <void> (&lpTranslate),
                                    &cbTranslatedBytes ) &&
                                     cbTranslatedBytes   && lpTranslate )
  {
    wchar_t        wszPropName [64] = { };
    _snwprintf_s ( wszPropName, 63,
                      LR"(\StringFileInfo\%04x%04x\ProductVersion)",
                        lpTranslate   [0].wLanguage,
                          lpTranslate [0].wCodePage );

    VerQueryValueW ( cbData.data (),
                            wszPropName,
            static_cast_p2p <void> (&wszVersion),
                                    &cbVersionBytes );

    if (cbVersionBytes)
      return wszVersion;
  }

  return L"";
}

std::wstring
SKIF_Util_GetProductName (const wchar_t* wszName)
{
  if (! wszName)
    return L"";

  /*
  static VerQueryValueW_pfn
    SKIF_VerQueryValueW = (VerQueryValueW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "VerQueryValueW"                                     );

  static GetFileVersionInfoExW_pfn
    SKIF_Util_GetFileVersionInfoExW = (GetFileVersionInfoExW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "GetFileVersionInfoExW"                                            );
  */

  UINT cbTranslatedBytes = 0,
       cbProductBytes    = 0;

  std::vector <uint8_t>
    cbData (16384, 0ui8);

  wchar_t* wszProduct    = nullptr; // Will point somewhere in cbData

  struct LANGANDCODEPAGE {
    WORD wLanguage;
    WORD wCodePage;
  } *lpTranslate = nullptr;

  BOOL bRet =
    GetFileVersionInfoExW ( FILE_VER_GET_PREFETCHED,
                                   wszName,
                                     0x00,
                    static_cast <DWORD> (cbData.size ()),
                                         cbData.data () );

  if (! bRet) return L"";

  if ( VerQueryValueW ( cbData.data (),
                             TEXT ("\\VarFileInfo\\Translation"),
            static_cast_p2p <void> (&lpTranslate),
                                    &cbTranslatedBytes ) &&
                                     cbTranslatedBytes   && lpTranslate )
  {
    wchar_t        wszPropName [64] = { };
    _snwprintf_s ( wszPropName, 63,
                   LR"(\StringFileInfo\%04x%04x\ProductName)",
                     lpTranslate   [0].wLanguage,
                       lpTranslate [0].wCodePage );

    VerQueryValueW ( cbData.data (),
                            wszPropName,
           static_cast_p2p <void> (&wszProduct),
                                   &cbProductBytes );

    if ( cbProductBytes )
      return std::wstring(wszProduct);
  }

  return L"";
}

/*
  Returns 0 for errors, 1 for x86, 2 for x64, and -1 for unknown types
*/
int
SKIF_Util_GetBinaryType (const LPCTSTR pszPathToBinary)
{
  int arch = 0;

  EnterCriticalSection (&CriticalSectionDbgHelp);

  HANDLE hFile = CreateFile (pszPathToBinary, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
  if (hFile != INVALID_HANDLE_VALUE)
  {
    HANDLE hMapping = CreateFileMapping (hFile, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    if (hMapping != INVALID_HANDLE_VALUE)
    {
      LPVOID addrHeader = MapViewOfFile (hMapping, FILE_MAP_READ, 0, 0, 0);
      if (addrHeader != NULL)
      {
        // All DbgHelp functions, such as ImageNtHeader, are single threaded.
        //  Therefore, calls from more than one thread to this function will likely result in unexpected behavior or memory corruption.
        //   To avoid this, you must synchronize all concurrent calls from more than one thread to this function.
        PIMAGE_NT_HEADERS peHdr = ImageNtHeader (addrHeader);
        if (peHdr != NULL)
        {
          switch (peHdr->FileHeader.Machine)
          {
            case IMAGE_FILE_MACHINE_I386:
              arch = 1;
              break;
            case IMAGE_FILE_MACHINE_AMD64:
              arch = 2;
              break;
            default:
              arch = -1;
          }
        }
        UnmapViewOfFile (addrHeader);
      }
      CloseHandle (hMapping);
    }
    CloseHandle (hFile);
  }

  LeaveCriticalSection (&CriticalSectionDbgHelp);

  return arch;
}

BOOL
WINAPI
SKIF_Util_CompactWorkingSet (void)
{
  return
    EmptyWorkingSet (
      SKIF_Util_GetCurrentProcess ()
    );
}

HRESULT
SKIF_Util_SetThreadDescription (HANDLE hThread, PCWSTR lpThreadDescription)
{
  using SetThreadDescription_pfn =
    HRESULT (WINAPI *)(HANDLE hThread, PCWSTR lpThreadDescription);

  static SetThreadDescription_pfn
    SKIF_SetThreadDescription =
        (SetThreadDescription_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetThreadDescription");

  if (SKIF_SetThreadDescription == nullptr)
    return false;
  
  return SKIF_SetThreadDescription (hThread, lpThreadDescription);
}

bool
SKIF_Util_IsWindows8Point1OrGreater (void)
{
  SK_RunOnce (
    SetLastError (NO_ERROR)
  );

  static bool bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "GetSystemTimePreciseAsFileTime"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

bool
SKIF_Util_IsWindows10OrGreater (void)
{
  SK_RunOnce (
    SetLastError (NO_ERROR)
  );

  static bool bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "SetThreadDescription"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

// Windows 10 1709+ (Build 16299) or newer
bool
SKIF_Util_IsWindows10v1709OrGreater (void)
{
  static bool bResult =
    SKIF_Util_IsWindowsVersionOrGreater (10, 0, 16299);

  return bResult;
}

// Windows 10 1903+ (Build 18362) or newer
bool
SKIF_Util_IsWindows10v1903OrGreater (void)
{
  static bool bResult =
    SKIF_Util_IsWindowsVersionOrGreater (10, 0, 18362);

  return bResult;
}

// Windows 11 (Build 22000) or newer
bool
SKIF_Util_IsWindows11orGreater (void)
{
  static bool bResult =
    SKIF_Util_IsWindowsVersionOrGreater (10, 0, 22000);

  return bResult;
}

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

bool
SKIF_Util_IsWindowsVersionOrGreater (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber)
{
  NTSTATUS(WINAPI *SKIF_RtlGetVersion)(LPOSVERSIONINFOEXW) = nullptr;

  OSVERSIONINFOEXW
    osInfo                     = { };
    osInfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

  *reinterpret_cast<FARPROC *>(&SKIF_RtlGetVersion) =
    GetProcAddress (GetModuleHandleW (L"ntdll"), "RtlGetVersion");

  if (SKIF_RtlGetVersion != nullptr)
  {
    if (NT_SUCCESS (SKIF_RtlGetVersion (&osInfo)))
    {
      return
          ( osInfo.dwMajorVersion >  dwMajorVersion ||
          ( osInfo.dwMajorVersion == dwMajorVersion &&
            osInfo.dwMinorVersion >= dwMinorVersion &&
            osInfo.dwBuildNumber  >= dwBuildNumber   )
        );
    }
  }

  return false;
}

bool
SKIF_Util_IsProcessAdmin (DWORD PID)
{
  bool          bRet = false;
  SK_AutoHandle hToken (INVALID_HANDLE_VALUE);

  SetLastError (NO_ERROR);

  SK_AutoHandle hProcess = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, PID);

  if (GetLastError() == ERROR_ACCESS_DENIED)
    return true;

  if ( OpenProcessToken ( hProcess,
                            TOKEN_QUERY,
                              &hToken.m_h )
     )
  {
    TOKEN_ELEVATION Elevation = { };

    DWORD cbSize =
      sizeof (TOKEN_ELEVATION);

    if ( GetTokenInformation ( hToken.m_h,
                                 TokenElevation,
                                   &Elevation,
                                     sizeof (Elevation),
                                       &cbSize )
       )
    {
      bRet =
        ( Elevation.TokenIsElevated != 0 );
    }
  }

  return bRet;
}

bool
SKIF_Util_IsProcessX86 (HANDLE process)
{
    SYSTEM_INFO si = { 0 };
    GetNativeSystemInfo (&si);

    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        return true;

    BOOL bIsWow64 = FALSE;

    typedef BOOL(WINAPI * Win32_IsWow64Process_lpfn) (HANDLE, PBOOL);
    Win32_IsWow64Process_lpfn
          IsWow64Process =
    (Win32_IsWow64Process_lpfn)GetProcAddress (GetModuleHandle(TEXT("kernel32")),
          "IsWow64Process");

    if (IsWow64Process != nullptr)
    {
      if (! IsWow64Process (process, &bIsWow64))
      {
        PLOG_ERROR << "IsWow64Process failed: " << SKIF_Util_GetErrorAsWStr ( );
      }
    }

    return bIsWow64;
}

PROCESSENTRY32W
SKIF_Util_FindProcessByName (const wchar_t* wszName)
{
  PROCESSENTRY32W none = { },
                  pe32 = { };

  SK_AutoHandle hProcessSnap (
    CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
  );

  if ((intptr_t)hProcessSnap.m_h <= 0) // == INVALID_HANDLE_VALUE)
    return none;

  pe32.dwSize = sizeof (PROCESSENTRY32W);

  if (! Process32FirstW (hProcessSnap, &pe32))
    return none;

  do
  {
    if (wcsstr (pe32.szExeFile, wszName))
      return pe32;
  } while (Process32NextW (hProcessSnap, &pe32));

  return none;
}

bool
SKIF_Util_SaveExtractExeIcon (std::wstring exePath, std::wstring targetPath)
{
  bool ret = PathFileExists (targetPath.c_str());

  if (! ret && PathFileExists (exePath.c_str()))
  {
    std::filesystem::path target = targetPath;

    std::error_code ec;
    // Create any missing directories
    if (! std::filesystem::exists (            target.parent_path(), ec))
          std::filesystem::create_directories (target.parent_path(), ec);
    
    // GDI+ Image Encoder CLSIDs (haven't changed forever)
    //
    //              {distinct-same-same-same-samesamesame}
    // image/bmp  : {557cf400-1a04-11d3-9a73-0000f81ef32e}
    // image/jpeg : {557cf401-1a04-11d3-9a73-0000f81ef32e}
    // image/gif  : {557cf402-1a04-11d3-9a73-0000f81ef32e}
    // image/tiff : {557cf405-1a04-11d3-9a73-0000f81ef32e}
    // image/png  : {557cf406-1a04-11d3-9a73-0000f81ef32e}

    const CLSID pngEncoderClsId =
      { 0x557cf406, 0x1a04, 0x11d3,{ 0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e } };

    // Variables
    HICON hIcon;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;

    // Extract the icon    
    if (SUCCEEDED (SHDefExtractIcon (exePath.c_str(), 0, 0, &hIcon, 0, 32))) // 256
    {
      // Start up GDI+
      if (Gdiplus::Status::Ok == Gdiplus::GdiplusStartup (&gdiplusToken, &gdiplusStartupInput, NULL))
      {
        // Create the GDI+ object
        Gdiplus::Bitmap* gdiplusImage =
          Gdiplus::Bitmap::FromHICON (hIcon);

        // Save the image in PNG as GIF loses the transparency
        if (Gdiplus::Status::Ok == gdiplusImage->Save (targetPath.c_str (), &pngEncoderClsId, NULL))
          ret = true;

        // Delete the object
        delete gdiplusImage;
        gdiplusImage = NULL;

        // Shut down GDI+
        Gdiplus::GdiplusShutdown (gdiplusToken);
      }

      // Destroy the icon
      DestroyIcon (hIcon);
    }
  }

  return ret;
}

bool
SKIF_Util_GetDragFromMaximized (bool refresh)
{
  // DragFromMaximize and WindowArrangementActive regístry keys are used since at least Windows 7
  // For some bloody reason these are _string_ registry values that holds a '1' or a '0'... WTF?!

  static int state = -1;

  if (state != -1 && ! refresh)
    return state;

  HKEY hKey;
  DWORD dwSize = 0;
  WCHAR szData[MAX_PATH];

  // Check if DragFromMaximize is enabled
  if (ERROR_SUCCESS   == RegOpenKeyExW (HKEY_CURRENT_USER, LR"(Control Panel\Desktop)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
  {
    dwSize = sizeof(szData) / sizeof(WCHAR);
    if (ERROR_SUCCESS == RegGetValueW (hKey, NULL, L"DragFromMaximize",        RRF_RT_REG_SZ, NULL, &szData, &dwSize))
      state = (wcscmp (szData, L"1") == 0);

    dwSize = sizeof(szData) / sizeof(WCHAR);
    if (state && // Only check WindowArrangementActive if DragFromMaximize is enabled
        ERROR_SUCCESS == RegGetValueW (hKey, NULL, L"WindowArrangementActive", RRF_RT_REG_SZ, NULL, &szData, &dwSize))
      state = (wcscmp (szData, L"1") == 0);
    
    if (state)
      PLOG_DEBUG << "DragFromMaximize and WindowArrangementActive registry keys are enabled in Windows";
    else
      PLOG_DEBUG << "DragFromMaximize and/or WindowArrangementActive registry keys are disabled in Windows";

    RegCloseKey (hKey);
  }

  // If the state failed to be checked, assume it is disabled
  if (state == -1)
    state = 0;

  return state;
}

bool
SKIF_Util_GetControlledFolderAccess (void)
{
  if (! SKIF_Util_IsWindows10OrGreater ( ))
    return false;

  static int state = -1;

  if (state != -1)
    return state;

  HKEY hKey;
  DWORD buffer = 0;
  unsigned long size = 1024;

  // Check if Controlled Folder Access is enabled
  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Defender\Windows Defender Exploit Guard\Controlled Folder Access\)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
  {
    if (ERROR_SUCCESS == RegQueryValueEx (hKey, L"EnableControlledFolderAccess", NULL, NULL, (LPBYTE)&buffer, &size))
      state = buffer;

    PLOG_DEBUG << "Detected CFA as being " << ((state) ? "enabled" : "disabled");

    RegCloseKey (hKey);

    if (state)
    {
      // Regular users / unelevated processes has read access to this key on Windows 10,
      //   but apparently not on Windows 11 so this check will fail on that OS.
      if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Defender\Windows Defender Exploit Guard\Controlled Folder Access\AllowedApplications\)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
      {
        static TCHAR               szExePath[MAX_PATH + 2];
        GetModuleFileName   (NULL, szExePath, _countof(szExePath));

        if (ERROR_SUCCESS == RegQueryValueEx (hKey, szExePath, NULL, NULL, NULL, NULL))
          state = 0;

        if (state)
          PLOG_DEBUG << "SKIF has been whitelisted";
        else
          PLOG_DEBUG << "SKIF has not been whitelisted!";

        RegCloseKey (hKey);
      }
    }
  }

  // If the state failed to be checked, assume it is disabled
  if (state == -1)
    state = 0;

  return state;
}

int
SKIF_Util_RegisterApp (bool force)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static int ret = -1;

  if (ret != -1 && ! force)
    return ret;

  if (! _inject.bHasServlet)
  {
    PLOG_ERROR << "Missing critical service components!";
    ret = 0;
    return ret;
  }

  std::wstring wsExePath = std::wstring (_path_cache.skif_executable);

  if (_registry.wsPath            == _path_cache.specialk_userdata &&
      _registry.wsAppRegistration == wsExePath)
  {
    ret = 1;
  }

  else if (force || _registry.wsPath.empty() || _registry.wsAppRegistration.empty())
  {
    ret = 1;

    if (_registry.regKVAppRegistration.putData (wsExePath))
      PLOG_INFO << "App registration was successful: " << wsExePath;
    else
    {
      PLOG_ERROR << "Failed to register SKIF in Windows";
      ret = 0;
    }

    if (_registry.regKVPath.putData (_path_cache.specialk_userdata))
      PLOG_INFO << "Updated central Special K userdata location: " << _path_cache.specialk_userdata;
    else
    {
      PLOG_ERROR << "Failed to update the central Special K userdata location!";
      ret = 0;
    }
  }

  return ret;
}


typedef unsigned long UpdateFlags;  // -> enum DWMOverlayTestModeFlags_

enum DWMOverlayTestModeFlags_
{
  DWMOverlayTestModeFlags_None        = 0,      // No overlay test mode flag is set
  DWMOverlayTestModeFlags_MPORelated1 = 1 << 0, // Unknown purpose (but MPO related)
  DWMOverlayTestModeFlags_Unknown1    = 1 << 1, // Unknown purpose
  DWMOverlayTestModeFlags_MPORelated2 = 1 << 2, // Unknown purpose (but MPO related)
  DWMOverlayTestModeFlags_Unknown2    = 1 << 4, // Unknown purpose
  DWMOverlayTestModeFlags_INITIAL     = 1 << 16 // Initial dummy value before we check it
};

bool
SKIF_Util_IsMPOsDisabledInRegistry (bool refresh)
{
  if (! SKIF_Util_IsWindows10OrGreater ( ))
    return false;

  static UpdateFlags flag = DWMOverlayTestModeFlags_INITIAL;
  static bool  isDisabled = false;

  if (flag != DWMOverlayTestModeFlags_INITIAL && ! refresh)
    return isDisabled;

  isDisabled = false;

  HKEY hKey;
  //DWORD buffer = 0;
  unsigned long size = 1024;

  // Check if DWM's OverlayTestMode has MPOs disabled
  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\Dwm\)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
  {
    if (ERROR_SUCCESS == RegQueryValueEx (hKey, L"OverlayTestMode", NULL, NULL, (LPBYTE)&flag, &size))
    {
      if ((flag & DWMOverlayTestModeFlags_MPORelated1) == DWMOverlayTestModeFlags_MPORelated1 &&
          (flag & DWMOverlayTestModeFlags_MPORelated2) == DWMOverlayTestModeFlags_MPORelated2)
        isDisabled = true;

      PLOG_VERBOSE << "OverlayTestMode registry value is set to: " << flag;
    }
    else {
      flag = DWMOverlayTestModeFlags_None;
    }

    RegCloseKey (hKey);
  }
  else {
    flag = DWMOverlayTestModeFlags_None;
  }

  return isDisabled;
}


void
SKIF_Util_GetMonitorHzPeriod (HWND hwnd, DWORD dwFlags, DWORD& dwPeriod)
{
  DEVMODE 
    dm        = { };
    dm.dmSize = sizeof (DEVMODE);
    
  MONITORINFOEX
    minfoex        = { };
    minfoex.cbSize = sizeof (MONITORINFOEX);

  HMONITOR
      hMonitor  = MonitorFromWindow (hwnd, dwFlags);
  if (hMonitor != NULL)
    if (GetMonitorInfo (hMonitor, (LPMONITORINFOEX)&minfoex))
      if (EnumDisplaySettings (minfoex.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        dwPeriod = (1000 / dm.dmDisplayFrequency);

  if (dwPeriod < 16)
    dwPeriod = 16; // In case we go too low, use 16 ms (60 Hz) to prevent division by zero later
}

bool
SKIF_Util_SetClipboardData (const std::wstring_view& data)
{
  bool result = false;

  if (OpenClipboard (SKIF_ImGui_hWnd))
  {
    HGLOBAL hGlobal = GlobalAlloc (GMEM_MOVEABLE, (data.size() + 1)  * sizeof (wchar_t));

    if (hGlobal)
    {
      EmptyClipboard ( );

      memcpy (GlobalLock (hGlobal), data.data(), (data.size() + 1)  * sizeof (wchar_t));
      GlobalUnlock (hGlobal);

      result = SetClipboardData (CF_UNICODETEXT, hGlobal);

      if (! result)
        GlobalFree (hGlobal);
    }

    CloseClipboard ( );
  }

  return result;
}

// Adds/removes environment variables in a given environment block
// Parts of this is CC BY-SA 4.0, https://stackoverflow.com/a/59597519
std::wstring
SKIF_Util_AddEnvironmentBlock (const void* pEnvBlock, const std::wstring& varName, const std::wstring& varValue)
{
  std::map <std::wstring,
            std::wstring> env;
  const wchar_t*   currentEnv = (const wchar_t*)pEnvBlock;

  // Parse the given block into a map of key/value pairs
  while (*currentEnv)
  {
    std::wstring  key, value,
                  keyValue = currentEnv;

    size_t pos  = keyValue.find_last_of(L'=');
    if (   pos !=     std::wstring::npos)
      key       = keyValue.substr(0, pos); // Extract the environment variable name from the given "key=value" string
    else
      key       = keyValue; // Environment variable lacks a = sign ?!
    
    // We store the whole "key=value" string as the value in the map
    // Environment variables must also end with a null terminator
    value       = keyValue + L'\0';

    env[SKIF_Util_ToLowerW (key)] = value;
    currentEnv += keyValue.size() + 1;
  }

  // If given a variable name, add/remove it based on the given variable value
  if (! varName.empty())
  {
    if (varValue.empty())
      env.erase (SKIF_Util_ToLowerW (varName));
    else
      env[       SKIF_Util_ToLowerW (varName)] = varName + L'=' + varValue + L'\0';
  }

#if _DEBUG
  for (auto& item : env)
  {
    OutputDebugString(L"key: ");
    OutputDebugString(item.first.c_str());
    OutputDebugString(L" - value: ");
    OutputDebugString(item.second.c_str());
    OutputDebugString(L"\n");
  }
#endif
  
  // Serialize the map into a new buffer
  std::wstring result;

  for (auto& item : env)
    result += item.second;

  // Indicate the end of the environment block using an additional null terminator
  result += L'\0';

  return result;
}

// Effective Power Mode (Windows 10 1809+)
typedef enum EFFECTIVE_POWER_MODE {
    EffectivePowerModeNone    = -1,   // Used as default value if querying failed
    EffectivePowerModeBatterySaver,
    EffectivePowerModeBetterBattery,
    EffectivePowerModeBalanced,
    EffectivePowerModeHighPerformance,
    EffectivePowerModeMaxPerformance, // EFFECTIVE_POWER_MODE_V1
    EffectivePowerModeGameMode,
    EffectivePowerModeMixedReality,   // EFFECTIVE_POWER_MODE_V2
} EFFECTIVE_POWER_MODE;

std::atomic<EFFECTIVE_POWER_MODE> enumEffectivePowerMode          = EffectivePowerModeNone;

#define EFFECTIVE_POWER_MODE_V1 (0x00000001)
#define EFFECTIVE_POWER_MODE_V2 (0x00000002)

typedef VOID WINAPI EFFECTIVE_POWER_MODE_CALLBACK (
    _In_     EFFECTIVE_POWER_MODE  Mode,
    _In_opt_ VOID                 *Context
);

VOID WINAPI SKIF_Util_EffectivePowerModeCallback (
    _In_     EFFECTIVE_POWER_MODE  Mode,
    _In_opt_ VOID                 *Context
)
{
  UNREFERENCED_PARAMETER (Context);

  enumEffectivePowerMode.store (Mode);

  PostMessage (SKIF_Notify_hWnd, WM_SKIF_POWERMODE, NULL, NULL);
};

// Retrieve the current effective power mode as a string
std::string SKIF_Util_GetEffectivePowerMode (void)
{
  std::string sMode;

  switch (enumEffectivePowerMode.load( ))
  {
  case EffectivePowerModeNone:
    sMode = "None";
    break;
  case EffectivePowerModeBatterySaver:
    sMode = "Battery Saver";
    break;
  case EffectivePowerModeBetterBattery:
    sMode = "Better Battery";
    break;
  case EffectivePowerModeBalanced:
    sMode = "Balanced";
    break;
  case EffectivePowerModeHighPerformance:
    sMode = "High Performance";
    break;
  case EffectivePowerModeMaxPerformance:
    sMode = "Max Performance";
    break;
  case EffectivePowerModeGameMode:
    sMode = "Game Mode";
    break;
  case EffectivePowerModeMixedReality:
    sMode = "Mixed Reality";
    break;
  default:
    sMode = "Unknown Mode";
    break;
  }

  return sMode;
}

// Register SKIF for effective power notifications on Windows 10 1809+
void SKIF_Util_SetEffectivePowerModeNotifications (bool enable)
{
  static HANDLE
      hEffectivePowerModeRegistration  = NULL;
  if (hEffectivePowerModeRegistration != NULL &&   enable) return;
  if (hEffectivePowerModeRegistration == NULL && ! enable) return;

  using PowerRegisterForEffectivePowerModeNotifications_pfn =
    HRESULT (WINAPI *)(ULONG Version, EFFECTIVE_POWER_MODE_CALLBACK *Callback, VOID *Context, VOID **RegistrationHandle);

  static PowerRegisterForEffectivePowerModeNotifications_pfn
    SKIF_PowerRegisterForEffectivePowerModeNotifications =
        (PowerRegisterForEffectivePowerModeNotifications_pfn)GetProcAddress (LoadLibraryEx (L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "PowerRegisterForEffectivePowerModeNotifications");

  using PowerUnregisterFromEffectivePowerModeNotifications_pfn =
    HRESULT (WINAPI *)(VOID *RegistrationHandle);

  static PowerUnregisterFromEffectivePowerModeNotifications_pfn
    SKIF_PowerUnregisterFromEffectivePowerModeNotifications =
        (PowerUnregisterFromEffectivePowerModeNotifications_pfn)GetProcAddress (LoadLibraryEx (L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "PowerUnregisterFromEffectivePowerModeNotifications");

  if (SKIF_PowerRegisterForEffectivePowerModeNotifications      != nullptr)
  {
    if (SKIF_PowerUnregisterFromEffectivePowerModeNotifications != nullptr)
    {
      if (enable)
      {
        PLOG_DEBUG << "Registering SKIF for effective power mode notifications";
        SKIF_PowerRegisterForEffectivePowerModeNotifications    (EFFECTIVE_POWER_MODE_V2, SKIF_Util_EffectivePowerModeCallback, NULL, &hEffectivePowerModeRegistration);
      }

      else {
        PLOG_DEBUG << "Unregistering SKIF for effective power mode notifications...";
        SKIF_PowerUnregisterFromEffectivePowerModeNotifications (hEffectivePowerModeRegistration);
      }
    }
  }
}



// High Dynamic Range (HDR)

// Check if one of the connected displays supports HDR
bool
SKIF_Util_IsHDRSupported (bool refresh)
{
  if (! SKIF_Util_IsWindows10v1709OrGreater ( ))
    return false;

  std::vector<DISPLAYCONFIG_PATH_INFO> pathArray;
  std::vector<DISPLAYCONFIG_MODE_INFO> modeArray;
  DWORD result = ERROR_SUCCESS;
  
  static bool state = false;

  if (! refresh)
    return state;
  
  state = false;

  do
  {
    // Determine how many path and mode structures to allocate
    UINT32 pathCount, modeCount;
    result = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "GetDisplayConfigBufferSizes failed: " << SKIF_Util_GetErrorAsWStr (result);
      state = false;
    }

    // Allocate the path and mode arrays
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // Get all active paths and their modes
    result = QueryDisplayConfig ( QDC_ONLY_ACTIVE_PATHS, &pathCount, pathArray.data(),
                                                         &modeCount, modeArray.data(), nullptr);

    // The function may have returned fewer paths/modes than estimated
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
    // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS)
  {
    PLOG_ERROR << "QueryDisplayConfig failed: " << SKIF_Util_GetErrorAsWStr (result);
    state = false;
  }

  // For each active path
  for (auto& path : pathArray)
  {
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO
      getDisplayHDR                   = { };
      getDisplayHDR.header.adapterId  = path.targetInfo.adapterId;
      getDisplayHDR.header.id         = path.targetInfo.id;
      getDisplayHDR.header.type       = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      getDisplayHDR.header.size       = sizeof (getDisplayHDR);

    result = DisplayConfigGetDeviceInfo (&getDisplayHDR.header);

    if (result == ERROR_SUCCESS)
    {
      if (getDisplayHDR.advancedColorSupported)
      {
        state = true;
        break;
      }
    }
    else {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr(result);
    }
  }

  return state;
}

// Check if one of the connected displays supports HDR
bool
SKIF_Util_IsHDRActive (bool refresh)
{
  if (! SKIF_Util_IsWindows10v1709OrGreater ( ))
    return false;

  std::vector<DISPLAYCONFIG_PATH_INFO> pathArray;
  std::vector<DISPLAYCONFIG_MODE_INFO> modeArray;
  DWORD result = ERROR_SUCCESS;
  
  static bool state = false;

  if (! refresh)
    return state;
  
  state = false;

  do
  {
    // Determine how many path and mode structures to allocate
    UINT32 pathCount, modeCount;
    result = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "GetDisplayConfigBufferSizes failed: " << SKIF_Util_GetErrorAsWStr (result);
      state = false;
    }

    // Allocate the path and mode arrays
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // Get all active paths and their modes
    result = QueryDisplayConfig ( QDC_ONLY_ACTIVE_PATHS, &pathCount, pathArray.data(),
                                                         &modeCount, modeArray.data(), nullptr);

    // The function may have returned fewer paths/modes than estimated
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
    // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS)
  {
    PLOG_ERROR << "QueryDisplayConfig failed: " << SKIF_Util_GetErrorAsWStr (result);
    state = false;
  }

  // For each active path
  for (auto& path : pathArray)
  {
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO
      getDisplayHDR                   = { };
      getDisplayHDR.header.adapterId  = path.targetInfo.adapterId;
      getDisplayHDR.header.id         = path.targetInfo.id;
      getDisplayHDR.header.type       = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
      getDisplayHDR.header.size       = sizeof (getDisplayHDR);

    result = DisplayConfigGetDeviceInfo (&getDisplayHDR.header);

    if (result == ERROR_SUCCESS)
    {
      if (getDisplayHDR.advancedColorSupported && getDisplayHDR.advancedColorEnabled)
      {
        state = true;
        break;
      }
    }
    else {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr(result);
    }
  }

  return state;
}

// Get the SDR white level for a display
// Parts of this is CC BY-SA 4.0, https://stackoverflow.com/a/74605112
float
SKIF_Util_GetSDRWhiteLevelForHMONITOR (HMONITOR hMonitor)
{
  std::vector<DISPLAYCONFIG_PATH_INFO> pathArray;
  std::vector<DISPLAYCONFIG_MODE_INFO> modeArray;
  DWORD result = ERROR_SUCCESS;

  do
  {
    // Determine how many path and mode structures to allocate
    UINT32 pathCount, modeCount;
    result = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "GetDisplayConfigBufferSizes failed: " << SKIF_Util_GetErrorAsWStr (result);
      return 80.0f;
    }

    // Allocate the path and mode arrays
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // Get all active paths and their modes
    result = QueryDisplayConfig ( QDC_ONLY_ACTIVE_PATHS, &pathCount, pathArray.data(),
                                                         &modeCount, modeArray.data(), nullptr);

    // The function may have returned fewer paths/modes than estimated
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
    // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS)
  {
    PLOG_ERROR << "QueryDisplayConfig failed: " << SKIF_Util_GetErrorAsWStr (result);
    return 80.0f;
  }

  // Enumerate all monitors => (handle, device name)>
  std::vector<std::tuple<HMONITOR, std::wstring>> monitors;
  EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmon, HDC hdc, LPRECT rc, LPARAM lp)
  {
      UNREFERENCED_PARAMETER(hdc);
      UNREFERENCED_PARAMETER(rc);

      MONITORINFOEX mi = {};
      mi.cbSize = sizeof(MONITORINFOEX);
      GetMonitorInfo(hmon, &mi);
      auto monitors = (std::vector<std::tuple<HMONITOR, std::wstring>>*)lp;
      monitors->push_back({ hmon, mi.szDevice });
      return TRUE;
  }, (LPARAM)&monitors);

  // For each active path
  for (auto& path : pathArray)
  {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME
      sourceName                  = {};
      sourceName.header.adapterId = path.targetInfo.adapterId;
      sourceName.header.id        = path.sourceInfo.id;
      sourceName.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      sourceName.header.size      =  sizeof (DISPLAYCONFIG_SOURCE_DEVICE_NAME);

    if (ERROR_SUCCESS != DisplayConfigGetDeviceInfo ((DISPLAYCONFIG_DEVICE_INFO_HEADER*)&sourceName))
        continue;

    // Find the monitor with this device name
    auto mon = std::find_if(monitors.begin(), monitors.end(), [&sourceName](std::tuple<HMONITOR, std::wstring> t)
    {
        return !std::get<1>(t).compare(sourceName.viewGdiDeviceName);
    });

    if (std::get<0>(*mon) != hMonitor)
      continue;

    // At this point we are working with the correct monitor

    DISPLAYCONFIG_SDR_WHITE_LEVEL
      getSDRWhiteLevel                  = { };
      getSDRWhiteLevel.header.adapterId = path.targetInfo.adapterId;
      getSDRWhiteLevel.header.id        = path.targetInfo.id;
      getSDRWhiteLevel.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
      getSDRWhiteLevel.header.size      =         sizeof (DISPLAYCONFIG_SDR_WHITE_LEVEL);
        
    if (ERROR_SUCCESS != DisplayConfigGetDeviceInfo ((DISPLAYCONFIG_DEVICE_INFO_HEADER*)&getSDRWhiteLevel))
        break;

    // SDRWhiteLevel represents a multiplier for standard SDR white
    // peak value i.e. 80 nits represented as fixed point.
    // To get value in nits use the following conversion
    // SDRWhiteLevel in nits = (SDRWhiteLevel / 1000) * 80
    if (getSDRWhiteLevel.SDRWhiteLevel)
      return (static_cast<float> (getSDRWhiteLevel.SDRWhiteLevel) / 1000.0f) * 80.0f;
  }

  return 80.0f;
}

// Toggles the HDR state of the display the cursor is currently on
bool
SKIF_Util_EnableHDROutput (void)
{
  if (! SKIF_Util_IsWindows10v1709OrGreater ( ))
    return false;

  std::vector<DISPLAYCONFIG_PATH_INFO> pathArray;
  std::vector<DISPLAYCONFIG_MODE_INFO> modeArray;
  POINT mousePosition;

  // Retrieve the monitor the mouse cursor is currently located on
  // Use GetPhysicalCursorPos() instead of GetCursorPos() to ensure
  //   we do not get virtualized coordinates.
  if (GetPhysicalCursorPos (&mousePosition))
  {
    DWORD result = ERROR_SUCCESS;

    do
    {
      // Determine how many path and mode structures to allocate
      UINT32 pathCount, modeCount;
      result = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);

      if (result != ERROR_SUCCESS)
        PLOG_ERROR << "GetDisplayConfigBufferSizes failed: " << SKIF_Util_GetErrorAsWStr (result);

      // Allocate the path and mode arrays
      pathArray.resize(pathCount);
      modeArray.resize(modeCount);

      // Get all active paths and their modes
      result = QueryDisplayConfig ( QDC_ONLY_ACTIVE_PATHS, &pathCount, pathArray.data(),
                                                           &modeCount, modeArray.data(), nullptr);

      // The function may have returned fewer paths/modes than estimated
      pathArray.resize(pathCount);
      modeArray.resize(modeCount);

      // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
      // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
    } while (result == ERROR_INSUFFICIENT_BUFFER);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "QueryDisplayConfig failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // For each active path
    for (auto& path : pathArray)
    {
      UINT32 idx = (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)
                  ? path.sourceInfo.sourceModeInfoIdx
                  : path.sourceInfo.modeInfoIdx;

      DISPLAYCONFIG_SOURCE_MODE *pSourceMode =
                 &modeArray [idx].sourceMode;

      RECT displayRect {
        pSourceMode->position.x, // Left
        pSourceMode->position.y, // Top
        pSourceMode->position.x, // Right
        pSourceMode->position.y  // Bottom
      };

      displayRect.right  += pSourceMode->width;
      displayRect.bottom += pSourceMode->height;

      if (! PtInRect (&displayRect, mousePosition))
        continue;

      // At this point we are working with the correct monitor
      DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO
        getHDRSupport                     = { };
        getHDRSupport.header.adapterId    = path.targetInfo.adapterId;
        getHDRSupport.header.id           = path.targetInfo.id;
        getHDRSupport.header.type         = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        getHDRSupport.header.size         =     sizeof (DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
      
      result = DisplayConfigGetDeviceInfo ((DISPLAYCONFIG_DEVICE_INFO_HEADER*)&getHDRSupport);

      if (ERROR_SUCCESS != result)
      {
        PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
        break;
      }

      if (getHDRSupport.advancedColorSupported)
      {
        bool NewHDRState = (getHDRSupport.advancedColorEnabled == false);

        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE
          setHDRState                     = { };
          setHDRState.enableAdvancedColor = NewHDRState;
          setHDRState.header.adapterId    = path.targetInfo.adapterId;
          setHDRState.header.id           = path.targetInfo.id;
          setHDRState.header.type         = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
          setHDRState.header.size         =     sizeof (DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);

        result = DisplayConfigSetDeviceInfo ((DISPLAYCONFIG_DEVICE_INFO_HEADER*)&setHDRState);

        if (ERROR_SUCCESS != result)
        {
          PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
          break;
        }

        PLOG_INFO << "Toggled HDR HDR display output on the current display to " << NewHDRState;
        return true;
      }

      PLOG_WARNING << "HDR display output is not supported on the current display";
    }
  }

  return false;
}

// Register a hotkey for toggling HDR on a per-display basis (WinKey + Ctrl + Shift + H)
bool
SKIF_Util_RegisterHotKeyHDRToggle (void)
{
  if (bHotKeyHDR)
    return true;

  /*
  * Re. MOD_WIN: Either WINDOWS key was held down. These keys are labeled with the Windows logo.
  *              Keyboard shortcuts that involve the WINDOWS key are reserved for use by the operating system.
  */

constexpr auto VK_H = 0x48;

  if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    if (SKIF_Util_IsHDRSupported (true))
      if (RegisterHotKey (SKIF_Notify_hWnd, SKIF_HotKey_HDR, MOD_WIN | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_H))
      {
        bHotKeyHDR = true;
        PLOG_INFO << "Successfully registered hotkey (WinKey + Ctrl + Shift + H) for toggling HDR for individual displays.";
      }
      else
        PLOG_ERROR << "Failed to register hotkey for toggling HDR: " << SKIF_Util_GetErrorAsWStr ( );
    else
      PLOG_INFO << "No HDR capable display detected on the system.";
  else
    PLOG_INFO << "OS does not support HDR display output.";

  return bHotKeyHDR;
}

// Unregisters a hotkey for toggling HDR on a per-display basis (WinKey + Ctrl + Shift + H)
bool
SKIF_Util_UnregisterHotKeyHDRToggle(void)
{
  if (! bHotKeyHDR)
    return true;

  if (UnregisterHotKey (SKIF_Notify_hWnd, SKIF_HotKey_HDR))
  {
    bHotKeyHDR = false;
    PLOG_INFO << "Removed the HDR toggling hotkey.";
  }

  return ! bHotKeyHDR;
}

// Get the registration state of the hotkey for toggling HDR on a per-display basis (WinKey + Ctrl + Shift + H)
bool
SKIF_Util_GetHotKeyStateHDRToggle (void)
{
  return bHotKeyHDR;
}

// Register a hotkey for starting the service with auto-stop (WinKey + Shift + Insert)
bool
SKIF_Util_RegisterHotKeySVCTemp (void)
{
  if (bHotKeySVC)
    return true;

  /*
  * Re. MOD_WIN: Either WINDOWS key was held down. These keys are labeled with the Windows logo.
  *              Keyboard shortcuts that involve the WINDOWS key are reserved for use by the operating system.
  */

  if (RegisterHotKey (SKIF_Notify_hWnd, SKIF_HotKey_SVC, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, VK_INSERT))
    PLOG_INFO << "Successfully registered hotkey (WinKey + Shift + Insert) for starting the service with auto-stop.";
  else
    PLOG_ERROR << "Failed to register hotkey for starting the service with auto-stop: " << SKIF_Util_GetErrorAsWStr ( );

  return bHotKeySVC;
}

// Unregisters a hotkey for starting the service with auto-stop (WinKey + Shift + Insert)
bool
SKIF_Util_UnregisterHotKeySVCTemp (void)
{
  if (! bHotKeySVC)
    return true;

  if (UnregisterHotKey (SKIF_Notify_hWnd, SKIF_HotKey_SVC))
  {
    bHotKeySVC = false;
    PLOG_INFO << "Removed the hotkey for starting the service with auto-stop.";
  }

  return ! bHotKeySVC;
}

// Get the registration state of the hotkey for starting the service with auto-stop (WinKey + Shift + Insert)
bool
SKIF_Util_GetHotKeyStateSVCTemp (void)
{
  return bHotKeySVC;
}


// Web

DWORD
WINAPI
SKIF_Util_GetWebUri (skif_get_web_uri_t* get)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ULONG     ulTimeout        = 5000UL;
  PCWSTR rgpszAcceptTypes [] = { L"*/*", nullptr };
  HINTERNET hInetHTTPGetReq  = nullptr,
            hInetHost        = nullptr,
            hInetRoot        = nullptr;

  // (Cleanup On Error)
  auto CLEANUP = [&](bool clean = false) ->
  DWORD
  {
    if (! clean)
    {
#if 0
      DWORD dwLastError =
           GetLastError ();

      std::wstring wsError = (std::wstring(L"WinInet Failure (") + std::to_wstring(dwLastError) + std::wstring(L"): ") + _com_error(dwLastError).ErrorMessage());
#endif

      PLOG_VERBOSE << L"WinInet Failure: " << SKIF_Util_GetErrorAsWStr (GetLastError ( ), GetModuleHandle (L"wininet.dll"));
    }

    if (hInetHTTPGetReq != nullptr) InternetCloseHandle (hInetHTTPGetReq);
    if (hInetHost       != nullptr) InternetCloseHandle (hInetHost);
    if (hInetRoot       != nullptr) InternetCloseHandle (hInetRoot);

    skif_get_web_uri_t* to_delete = nullptr;
    std::swap   (get,   to_delete);
    delete              to_delete;

    return 0;
  };
  
  PLOG_VERBOSE                           << "Method: " << std::wstring(get->method);
  PLOG_VERBOSE                           << "Target: " << ((get->https) ? "https://" : "http://") << get->wszHostName << get->wszHostPath;
  PLOG_VERBOSE_IF(! get->header.empty()) << "Header: " << get->header;
  PLOG_VERBOSE_IF(! get->body.empty())   << "  Body: " << get->body;

  hInetRoot =
    InternetOpen (
      L"Special K - Asset Crawler",
        INTERNET_OPEN_TYPE_DIRECT,
          nullptr, nullptr,
            0x00 );

  if (hInetRoot == nullptr)
    return CLEANUP ();

  DWORD_PTR dwInetCtx = 0;

  hInetHost =
    InternetConnect ( hInetRoot,
                        get->wszHostName,
                          (get->https) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
                            nullptr, nullptr,
                              INTERNET_SERVICE_HTTP,
                                0x00,
                                  (DWORD_PTR)&dwInetCtx );

  if (hInetHost == nullptr)
    return CLEANUP ();

  int flags = ((get->https) ? INTERNET_FLAG_SECURE : 0x0) |
              INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP  | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS |
              INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID;

  if (_registry.bLowBandwidthMode)
    flags  |= INTERNET_FLAG_RESYNCHRONIZE            | INTERNET_FLAG_CACHE_IF_NET_FAIL        | INTERNET_FLAG_CACHE_ASYNC;
  else
    flags  |= INTERNET_FLAG_RELOAD                   | INTERNET_FLAG_NO_CACHE_WRITE           | INTERNET_FLAG_PRAGMA_NOCACHE;

  hInetHTTPGetReq =
    HttpOpenRequest ( hInetHost,
                        get->method,
                          get->wszHostPath,
                            L"HTTP/1.1",
                              nullptr,
                                rgpszAcceptTypes,
                                  flags,
                                    (DWORD_PTR)&dwInetCtx );

  // Wait 5000 msecs for a dead connection, then give up
  //
  InternetSetOptionW ( hInetHTTPGetReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                         &ulTimeout,    sizeof (ULONG) );

  if (hInetHTTPGetReq == nullptr)
    return CLEANUP ();

  if ( HttpSendRequestW ( hInetHTTPGetReq,
                            get->header.c_str(),
                              static_cast<DWORD>(get->header.length()),
                                (LPVOID)get->body.c_str(),
                                  static_cast<DWORD>(get->body.size()) ) )
  {
    DWORD dwStatusCode        = 0;
    DWORD dwStatusCode_Len    = sizeof (DWORD);

    DWORD dwContentLength     = 0;
    DWORD dwContentLength_Len = sizeof (DWORD);
    DWORD dwSizeAvailable;

    HttpQueryInfo ( hInetHTTPGetReq,
                     HTTP_QUERY_STATUS_CODE |
                     HTTP_QUERY_FLAG_NUMBER,
                       &dwStatusCode,
                         &dwStatusCode_Len,
                           nullptr );

    if (dwStatusCode == 200)
    {
      HttpQueryInfo ( hInetHTTPGetReq,
                        HTTP_QUERY_CONTENT_LENGTH |
                        HTTP_QUERY_FLAG_NUMBER,
                          &dwContentLength,
                            &dwContentLength_Len,
                              nullptr );

      std::vector <char> http_chunk;
      std::vector <char> concat_buffer;

      while ( InternetQueryDataAvailable ( hInetHTTPGetReq,
                                             &dwSizeAvailable,
                                               0x00, NULL )
        )
      {
        if (dwSizeAvailable > 0)
        {
          DWORD dwSizeRead = 0;

          if (http_chunk.size () < dwSizeAvailable)
              http_chunk.resize   (dwSizeAvailable);

          if ( InternetReadFile ( hInetHTTPGetReq,
                                    http_chunk.data (),
                                      dwSizeAvailable,
                                        &dwSizeRead )
             )
          {
            if (dwSizeRead == 0)
              break;

            concat_buffer.insert ( concat_buffer.cend   (),
                                    http_chunk.cbegin   (),
                                      http_chunk.cbegin () + dwSizeRead );

            if (dwSizeRead < dwSizeAvailable)
              break;
          }
        }

        else
          break;
      }

      FILE *fOut = nullptr;

      _wfopen_s (&fOut, get->wszLocalPath, L"wb+" );

      if (fOut != nullptr)
      {
        fwrite (concat_buffer.data (), concat_buffer.size (), 1, fOut);
        fflush (fOut);
        fclose (fOut);

        CLEANUP (true);
        return 1;
      }
    }

    else { // dwStatusCode != 200
      PLOG_WARNING << "HttpSendRequestW failed -> HTTP Status Code: " << dwStatusCode;
    }
  }

  return CLEANUP ( );
}

DWORD
SKIF_Util_GetWebResource (std::wstring url, std::wstring_view destination, std::wstring method, std::wstring header, std::string body)
{
  auto* get =
    new skif_get_web_uri_t { };

  URL_COMPONENTSW urlcomps = { };

  urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

  urlcomps.lpszHostName     = get->wszHostName;
  urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

  urlcomps.lpszUrlPath      = get->wszHostPath;
  urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

  if (! method.empty())
    get->method = method.c_str();

  if (! header.empty())
    get->header = header.c_str();

  if (! body.empty())
    get->body = body;

  if ( InternetCrackUrl (          url.c_str  (),
         gsl::narrow_cast <DWORD> (url.length ()),
                            0x00,
                              &urlcomps
                        )
     )
  {
    wcsncpy ( get->wszLocalPath,
                           destination.data (),
                       MAX_PATH );

    get->https = (urlcomps.nScheme == INTERNET_SCHEME_HTTPS);

    return SKIF_Util_GetWebUri (get);
  }

  else {
    PLOG_ERROR << "Failed to cracks a URL into its component parts!";
    PLOG_VERBOSE_IF(! method.empty()) << "Method: " << method;
    PLOG_VERBOSE_IF(!    url.empty()) << "Target: " << url;
    PLOG_VERBOSE_IF(! header.empty()) << "Header: " << header;
    PLOG_VERBOSE_IF(!   body.empty()) << "  Body: " << body;
  }

  return 0;
}


// Directory Watch

SKIF_DirectoryWatch::SKIF_DirectoryWatch (std::wstring_view wstrPath, bool bGlobalWait, bool bWaitAllTabs, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
  registerNotify (wstrPath, bGlobalWait, bWaitAllTabs, bWatchSubtree, dwNotifyFilter);
}

bool
SKIF_DirectoryWatch::isSignaled (void)
{
  bool bRet = false;

  if (_hChangeNotification != INVALID_HANDLE_VALUE)
  {
    bRet =
      ( WAIT_OBJECT_0 ==
          WaitForSingleObject (_hChangeNotification, 0) );

    if (bRet)
    {
      FindNextChangeNotification (
        _hChangeNotification
      );
    }
  }

  return bRet;
}

bool
SKIF_DirectoryWatch::isSignaled (std::wstring_view wstrPath, bool bGlobalWait, bool bWaitAllTabs, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
  bool bRet = false;

  if (_hChangeNotification != INVALID_HANDLE_VALUE)
    bRet = isSignaled ( );

  else if (! wstrPath.empty())
    registerNotify (wstrPath, bGlobalWait, bWaitAllTabs, bWatchSubtree, dwNotifyFilter);

  return bRet;
}

void
SKIF_DirectoryWatch::reset (void)
{
  if (      _hChangeNotification != INVALID_HANDLE_VALUE)
    FindCloseChangeNotification (_hChangeNotification);

  if (_bGlobalWait)
  {
    if (_bWaitAllTabs)
    {
      for (auto& vWatchHandle : vWatchHandles)
      {
        if (! vWatchHandle.second.empty())
          vWatchHandle.second.erase(std::remove(vWatchHandle.second.begin(), vWatchHandle.second.end(), _hChangeNotification), vWatchHandle.second.end());
      }
    }
    else if (! vWatchHandles[SKIF_Tab_Selected].second.empty())
    {
      vWatchHandles[SKIF_Tab_Selected].second.erase(std::remove(vWatchHandles[SKIF_Tab_Selected].second.begin(), vWatchHandles[SKIF_Tab_Selected].second.end(), _hChangeNotification), vWatchHandles[SKIF_Tab_Selected].second.end());
    }
  }

  // Reset variables
  _hChangeNotification = INVALID_HANDLE_VALUE;
  _bGlobalWait         = false;
  _bWaitAllTabs        = false;
  _path                = L"";
}

void
SKIF_DirectoryWatch::registerNotify (std::wstring_view wstrPath, bool bGlobalWait, bool bWaitAllTabs, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
  if (! wstrPath.empty())
  {
    _path = wstrPath;

    _hChangeNotification =
      FindFirstChangeNotificationW (
        _path.c_str(), bWatchSubtree,
        dwNotifyFilter
      );

    if (_hChangeNotification != INVALID_HANDLE_VALUE)
    {
      FindNextChangeNotification (
        _hChangeNotification
      );

      _bGlobalWait  = bGlobalWait;
      _bWaitAllTabs = bWaitAllTabs;

      if (_bGlobalWait)
      {
        if (_bWaitAllTabs)
        {
          for (auto& vWatchHandle : vWatchHandles)
          {
            vWatchHandle.second.push_back (_hChangeNotification);
          }
        }
        else {
          vWatchHandles[SKIF_Tab_Selected].second.push_back (_hChangeNotification);
        }
      }
    }
  }

  else {
    PLOG_ERROR << "Unexpected empty string was received when trying to register for directory change notifications!";
  }
}

SKIF_DirectoryWatch::~SKIF_DirectoryWatch (void)
{
  reset ( );
}


// Registry Watch

SKIF_RegistryWatch::SKIF_RegistryWatch ( HKEY hRootKey, const wchar_t* wszSubKey, const wchar_t* wszEventName, BOOL bWatchSubtree, DWORD dwNotifyFilter, bool bGlobalWait, bool bWOW6432Key, bool bWOW6464Key )
{
  _init.root          = hRootKey;
  _init.sub_key       = wszSubKey;
  _init.watch_subtree = bWatchSubtree;
  _init.filter_mask   = dwNotifyFilter;
  _init.wow64_32key   = bWOW6432Key;
  _init.wow64_64key   = bWOW6464Key;

  _hEvent.m_h =
      CreateEvent ( nullptr, TRUE,
                            FALSE, wszEventName );

  reset ();

  _bGlobalWait = bGlobalWait;

  if (_bGlobalWait)
    vWatchHandles[SKIF_Tab_Selected].second.push_back(_hEvent.m_h);
}

SKIF_RegistryWatch::~SKIF_RegistryWatch (void)
{
  if (_bGlobalWait && ! vWatchHandles[SKIF_Tab_Selected].second.empty())
    vWatchHandles[SKIF_Tab_Selected].second.erase (std::remove(vWatchHandles[SKIF_Tab_Selected].second.begin(), vWatchHandles[SKIF_Tab_Selected].second.end(), _hEvent.m_h), vWatchHandles[SKIF_Tab_Selected].second.end());
}

void
SKIF_RegistryWatch::registerNotify (void)
{
  _hKeyBase.NotifyChangeKeyValue (
    _init.watch_subtree,
      _init.filter_mask,
        _hEvent.m_h
  );
}

void
SKIF_RegistryWatch::reset (void)
{
  _hKeyBase.Close ();

  if ((intptr_t)_hEvent.m_h > 0)
    ResetEvent (_hEvent.m_h);

  LSTATUS lStat =
    _hKeyBase.Open (_init.root,
                    _init.sub_key.c_str (), KEY_NOTIFY | ((_init.wow64_32key) ? KEY_WOW64_32KEY : 0x0) | ((_init.wow64_64key) ? KEY_WOW64_64KEY : 0x0) );

  if (lStat == ERROR_SUCCESS)
  {
    registerNotify ( );
  }
}

bool
SKIF_RegistryWatch::isSignaled (void)
{
  bool signaled =
    WaitForSingleObjectEx (
      _hEvent.m_h, 0UL, FALSE
    ) == WAIT_OBJECT_0;

  if (signaled)
    reset ();

  return signaled;
}


// Shortcuts (*.lnk)

//
// https://docs.microsoft.com/en-au/windows/win32/shell/links?redirectedfrom=MSDN#resolving-a-shortcut
//
// ResolveIt - Uses the Shell's IShellLink and IPersistFile interfaces
//             to retrieve the path and description from an existing shortcut.
//
// Returns the result of calling the member functions of the interfaces.
//
// Parameters:
// hwnd            - A handle to the parent window. The Shell uses this window to
//                   display a dialog box if it needs to prompt the user for more
//                   information while resolving the link.
// lpszLinkFile    - Address of a buffer that contains the path of the link to resolve,
//                   including the .lnk file name.
// lpszTarget      - Address of a buffer that receives the path of the link target,
//                   including the file name.
// lpszArguments   - Address of a buffer that receives the arguments of the Shell link.
// iPathBufferSize - Size of the buffers at lpszLinkFile and lpszTarget. Typically MAX_PATH.
//

void
SKIF_Util_ResolveShortcut (HWND hwnd, LPCWSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize)
{
  IShellLink* psl = nullptr;

  WCHAR szArguments [MAX_PATH + 2] = { };
  WCHAR szTarget    [MAX_PATH + 2] = { };

  *lpszTarget    = 0; // Assume failure
  *lpszArguments = 0; // Assume failure

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface.
  if (SUCCEEDED (CoCreateInstance (CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
  {
    IPersistFile* ppf = nullptr;

    // Get a pointer to the IPersistFile interface.
    if (SUCCEEDED (psl->QueryInterface (IID_IPersistFile, (void**)&ppf)))
    {
      //WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode.
      //MultiByteToWideChar (CP_ACP, 0, lpszLinkFile, -1, wsz, MAX_PATH);

      // Add code here to check return value from MultiByteWideChar
      // for success.

      // Load the shortcut.
      if (SUCCEEDED (ppf->Load (lpszLinkFile, STGM_READ)))
      {
        // Disables the UI and hopefully sets a timeout duration of 10ms,
        //   since we don't actually care all that much about resolving the target.
        DWORD flags = MAKELONG (SLR_NO_UI, 10);

        // Resolve the link.
        if (SUCCEEDED (psl->Resolve (hwnd, flags)))
        {
          // Get the link target.
          if (SUCCEEDED (psl->GetPath (szTarget, MAX_PATH, NULL, SLGP_RAWPATH)))
            StringCbCopy (lpszTarget, iPathBufferSize, szTarget);

          // Get the arguments of the target.
          if (SUCCEEDED (psl->GetArguments (szArguments, MAX_PATH)))
            StringCbCopy (lpszArguments, iPathBufferSize, szArguments);
        }
      }

      // Release the pointer to the IPersistFile interface.
      ppf->Release();
    }

    // Release the pointer to the IShellLink interface.
    psl->Release();
  }
}

//
// https://docs.microsoft.com/en-au/windows/win32/shell/links?redirectedfrom=MSDN#creating-a-shortcut-and-a-folder-shortcut-to-a-file
// 
// CreateLink - Uses the Shell's IShellLink and IPersistFile interfaces 
//              to create and store a shortcut to the specified object. 
//
// Returns true if successful; false if an error occurred.
//
// Parameters:
// lpszPathLink     - Address of a buffer that contains the full path where the
//                    Shell link is to be stored, including the .lnk file name.
// lpszTarget       - Address of a buffer that contains the path of the target,
//                    including the file name if relevant.
// lpszArgs         - Address of a buffer that contains any arguments for the
//                    target, if relevant.
// lpszWorkDir      - Address of a buffer that contains the working directory
//                    the target should launch in, if relevant.
// lpszDesc         - Address of a buffer that contains a description of the
//                    Shell link, stored in the Comment field of the link
//                    properties.
// lpszIconLocation - Address of a buffer that contains the full path of icon
//                    used for the shortcut, if relevant.
// iIcon            - Index for the icon at lpszIconLocation to use.
// 

bool
SKIF_Util_CreateShortcut (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs, LPCWSTR lpszWorkDir, LPCWSTR lpszDesc, LPCWSTR lpszIconLocation, int iIcon)
{
  bool ret = false;
  IShellLink* psl = nullptr;

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
  // has already been called.

  if (SUCCEEDED (CoCreateInstance (CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
  {
    IPersistFile* ppf = nullptr;

    // Set the specifics of the shortcut. 
    psl->SetPath               (lpszTarget);

    if (wcscmp (lpszWorkDir, L"\0") == 0) // lpszWorkDir == L"\0"
      psl->SetWorkingDirectory (std::filesystem::path(lpszTarget).parent_path().c_str());
    else
      psl->SetWorkingDirectory (lpszWorkDir);

    if (wcscmp (lpszArgs, L"\0") != 0) // lpszArgs != L"\0"
      psl->SetArguments          (lpszArgs);

    if (wcscmp (lpszDesc, L"\0") != 0) // lpszDesc != L"\0"
      psl->SetDescription      (lpszDesc);

    if (wcscmp (lpszIconLocation, L"\0") != 0) // (lpszIconLocation != L"\0")
      psl->SetIconLocation     (lpszIconLocation, iIcon);

    // Query IShellLink for the IPersistFile interface, used for saving the 
    // shortcut in persistent storage. 
    //hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

    if (SUCCEEDED (psl->QueryInterface (IID_IPersistFile, (void**)&ppf)))
    {
      //WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode. 
      //MultiByteToWideChar (CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);

      // Save the link by calling IPersistFile::Save. 
      if (SUCCEEDED (ppf->Save (lpszPathLink, FALSE)))
        ret = true;

      ppf->Release();
    }
    psl->Release();
  }

  return ret;
}
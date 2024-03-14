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
#include <regex>

#ifndef SECURITY_WIN32 
#define SECURITY_WIN32 
#endif

#include <Security.h>
#include <secext.h>
#include <userenv.h>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Gdiplus.lib")

#include <SKIF.h>
#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <HybridDetect.h>

std::vector<HANDLE> vWatchHandles[UITab_ALL];
INT64               SKIF_TimeInMilliseconds = 0;

bool bHotKeyHDR = false,
     bHotKeySVC = false;

CRITICAL_SECTION CriticalSectionDbgHelp = { };

// Generic Utilities

// Companion variant of SK_FormatString
// Not safe to use in scenarios such as:
//   - Recursion (calling itself)
//   - Multiple as part of a single expression
// since the static memory buffer gets overwritten
// Basically only keep to using this in ImGui label scenarios
char *
__cdecl
SKIF_Util_FormatStringRaw (char const* const _Format, ...)
{
  if (_Format == NULL)
    return "";

  size_t len      = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( nullptr, 0, _Format, _ArgList ) + 1ui64;
  }
  va_end   (_ArgList);

  static thread_local size_t s_alloc_size = 0;
  static thread_local std::unique_ptr <char[]> s_pData;

  size_t alloc_size = sizeof (char) * (len + 2);

  if (s_alloc_size != alloc_size)
    s_pData = std::make_unique <char []> (alloc_size);

  if (! s_pData)
    return "";

  s_alloc_size = alloc_size;
  s_pData.get ()[0] = '\0';

  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( s_pData.get (), len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    s_pData.get ();
}

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

// Get the time that the current frame started processing, updated by ImGui_ImplWin32_NewFrame()
// Original non-cached function was moved over to SKIF_Util_timeGetTime1
DWORD
SKIF_Util_timeGetTime (void)
{
  return static_cast<DWORD>
          ( SKIF_TimeInMilliseconds & 0xFFFFFFFFLL );
}

DWORD
SKIF_Util_timeGetTime1 (void)
{
  static LARGE_INTEGER qpcFreq = { };
         LARGE_INTEGER li      = { };

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

// Converts Unix timestamp to a given human-friendly format (UTC)
std::wstring
SKIF_Util_timeGetTimeAsWStr (time_t time)
{
  wchar_t    wszSystemTime [64]  = { };
  wchar_t    wszSystemDate [64]  = { };
  wchar_t    wszFormatTime [128] = { };
  SYSTEMTIME  stFormatTime       = { };
  FILETIME    ftFormatTime       =
    SK_Win32_time_t_to_FILETIME (time);

  FileTimeToSystemTime ( &ftFormatTime, &stFormatTime );

  // Uses Ex functions since Microsoft recommends this
  // DATE_SHORTDATE solves | character in the date format caused by LTR / RTL markers that ImGui cannot handle properly
  GetDateFormatEx (LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE,
    &stFormatTime, NULL, wszSystemDate, 63, NULL);
  GetTimeFormatEx (LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
    &stFormatTime, NULL, wszSystemTime, 63);

  StringCchCatW (wszFormatTime, 127, wszSystemDate);
  StringCchCatW (wszFormatTime, 127, L" ");
  StringCchCatW (wszFormatTime, 127, wszSystemTime);

  return std::wstring (wszFormatTime);
}

// A function that returns the current time as a string in a custom format
// Not really necessary since PLOG can output to Visual Studio's debug output stream
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

  for (wchar_t c : format)
  {
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

std::wstring
SKIF_Util_NormalizeFullPath (std::wstring string)
{
  // Are we dealing with a network path? (\\server\share)
  bool isUNCpath = PathIsNetworkPathW (string.c_str());

  // "Clean" the path... But also removes one of the initial two backslashes
  //   in an UNC path if the path begins with four backslashes (\\\\server\\share)
  string = std::filesystem::path(string).lexically_normal();

  // If we are dealing with an UNC path that is no more,
  //   we need to fix the initial missing backslash...
  if (isUNCpath && ! PathIsNetworkPathW (string.c_str()))
    string.insert(0, 1, '\\');

  return string;
}

std::string
SKIF_Util_NormalizeFullPath (std::string string)
{
  // Are we dealing with a network path? (\\server\share)
  bool isUNCpath = PathIsNetworkPathA (string.c_str());

  // "Clean" the path... But also removes one of the initial two backslashes
  //   in an UNC path if the path begins with four backslashes (\\\\server\\share)
  string = std::filesystem::path(string).lexically_normal().string();

  // If we are dealing with an UNC path that is no more,
  //   we need to fix the initial missing backslash...
  if (isUNCpath && ! PathIsNetworkPathA (string.c_str()))
    string.insert(0, 1, '\\');

  return string;
}


// Usernames / Machine Name

std::wstring userProfile;
std:: string userProfileUTF8;
std::wstring userSamName;
std:: string userSamNameUTF8;
std::wstring userDisName;
std:: string userDisNameUTF8;
std::wstring machineName;
std:: string machineNameUTF8;

// Just a bunch of wildcards (*)
std::wstring replProfile;
std:: string replProfileUTF8;
std::wstring replSamName;
std:: string replSamNameUTF8;
std::wstring replDisName;
std:: string replDisNameUTF8;
std::wstring replMacName;
std:: string replMacNameUTF8;

void
SKIF_UtilInt_IniUserMachineStrip (void)
{
  static bool
      runOnce = true;
  if (runOnce)
  {   runOnce = false;

    DWORD   dwLen     =     MAX_PATH;
    wchar_t wszUserProfile [MAX_PATH] = { };
    wchar_t wszUserSamName [MAX_PATH] = { };
    wchar_t wszUserDisName [MAX_PATH] = { };
    wchar_t wszMachineName [MAX_PATH] = { };

    if (GetUserProfileDirectoryW (SKIF_Util_GetCurrentProcessToken ( ), wszUserProfile, &dwLen))
    {
      PathStripPathW                   (wszUserProfile);
      userProfile     = std::wstring   (wszUserProfile);
      userProfileUTF8 = SK_WideCharToUTF8 (userProfile);

      replProfile     = std::wstring      (userProfile.length(), L'*');
      replProfileUTF8 = SK_WideCharToUTF8 (replProfile);
    }
    
    dwLen             = MAX_PATH;

    if (GetUserNameExW (NameSamCompatible, wszUserSamName, &dwLen))
    {
      wcscpy_s (wszMachineName, MAX_PATH, wszUserSamName);
      PathStripPathW                     (wszUserSamName); // Strip machine name
      PathRemoveFileSpecW                (wszMachineName); // Strip username

      userSamName     = std::wstring     (wszUserSamName);
      userSamNameUTF8 = SK_WideCharToUTF8   (userSamName);

      replSamName     = std::wstring        (userSamName.length(), L'*');
      replSamNameUTF8 = SK_WideCharToUTF8   (replSamName);

      machineName     = std::wstring     (wszMachineName);
      machineNameUTF8 = SK_WideCharToUTF8   (machineName);

      replMacName     = std::wstring        (machineName.length(), L'*');
      replMacNameUTF8 = SK_WideCharToUTF8   (replMacName);
    }
    
    dwLen             = MAX_PATH;

    if (GetUserNameExW (NameDisplay,    wszUserDisName, &dwLen))
    {
      userDisName     = std::wstring   (wszUserDisName,  dwLen);
      userDisNameUTF8 = SK_WideCharToUTF8 (userDisName);

      replDisName     = std::wstring      (userDisName.length(), L'*');
      replDisNameUTF8 = SK_WideCharToUTF8 (replDisName);
    }
  }
}

std::string
SKIF_Util_StripPersonalData (std::string input)
{
  SKIF_UtilInt_IniUserMachineStrip ( );
  
  if (! userDisNameUTF8.empty())
    input = std::regex_replace (input, std::regex  (userDisNameUTF8.c_str()), replDisNameUTF8.c_str()); // Strip Display Name first as it is most likely to include the profile/SAM account name

  if (! userProfileUTF8.empty())
    input = std::regex_replace (input, std::regex  (userProfileUTF8.c_str()), replProfileUTF8.c_str());

  if (! userSamNameUTF8.empty())
    input = std::regex_replace (input, std::regex  (userSamNameUTF8.c_str()), replSamNameUTF8.c_str());

  if (! machineNameUTF8.empty())
    input = std::regex_replace (input, std::regex  (machineNameUTF8.c_str()), replMacNameUTF8.c_str());
  
  // Trim a single trailing newline
  if (! input.empty() && input.back() == '\n')
    input.pop_back();

  return input;
}

std::wstring
SKIF_Util_StripPersonalData (std::wstring input)
{
  SKIF_UtilInt_IniUserMachineStrip ( );
  
  if (! userDisName.empty())
    input = std::regex_replace (input, std::wregex (userDisName.c_str()),    replDisName.c_str()); // Strip Display Name first as it is most likely to include the profile/SAM account name

  if (! userProfile.empty())
    input = std::regex_replace (input, std::wregex (userProfile.c_str()),    replProfile.c_str());

  if (! userSamName.empty())
    input = std::regex_replace (input, std::wregex (userSamName.c_str()),    replSamName.c_str());

  if (! machineName.empty())
    input = std::regex_replace (input, std::wregex (machineName.c_str()),    replMacName.c_str());
  
  // Trim a single trailing newline
  if (! input.empty() && input.back() == L'\n')
    input.pop_back();

  return input;
}

void
SKIF_Util_Debug_LogUserNames (void)
{
  SKIF_UtilInt_IniUserMachineStrip ( );

  std::wstring names      = SK_FormatStringW   (L"ProfileName: %s, UserName: %s, DisplayName: %s, MachineName: %s", userProfile    .c_str(), userSamName    .c_str(), userDisName    .c_str(), machineName    .c_str());
  std:: string names_utf8 = SKIF_Util_FormatStringRaw ( "ProfileName: %s, UserName: %s, DisplayName: %s, MachineName: %s", userProfileUTF8.c_str(), userSamNameUTF8.c_str(), userDisNameUTF8.c_str(), machineNameUTF8.c_str());

  PLOG_VERBOSE << names;
  PLOG_VERBOSE << names_utf8;
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

  PLOG_INFO                           << "Performing a ShellExecuteEx call...";
  PLOG_INFO_IF(! path.empty())        << "File      : " << path;
  PLOG_INFO_IF(verb       != nullptr) << "Verb      : " << std::wstring(verb);
  PLOG_INFO_IF(parameters != nullptr) << "Parameters: " << std::wstring(parameters);
  PLOG_INFO_IF(directory  != nullptr) << "Directory : " << std::wstring(directory);
//PLOG_INFO                           << "Flags     : " << flags;

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
    PLOG_INFO << "The operation was successful.";
    ret = sexi.hInstApp;
  }

  else {
    PLOG_ERROR << "The operation failed!";
  }

  if (_registry._LoadedSteamOverlay)
    SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");

  return ret;
}

// Make an unelevated ShellExecute call through Explorer.
// Allows elevated processes to spawn unelevated processes.
// https://devblogs.microsoft.com/oldnewthing/20131118-00/?p=2643
bool
SKIF_Util_ShellExecuteUnelevated (
  const std::wstring_view& path,
               int         nShow,
               LPCWSTR     verb,
               LPCWSTR     parameters,
               LPCWSTR     directory)
{
  auto _FindDesktopFolderView = [&](REFIID riid, void **ppv) -> void
  {
    CComPtr<IShellWindows> spShellWindows;
    spShellWindows.CoCreateInstance(CLSID_ShellWindows);

    CComVariant vtLoc(CSIDL_DESKTOP);
    CComVariant vtEmpty;
    long lhwnd;
    CComPtr<IDispatch> spdisp;
    spShellWindows->FindWindowSW(
        &vtLoc, &vtEmpty,
        SWC_DESKTOP, &lhwnd, SWFO_NEEDDISPATCH, &spdisp);

    CComPtr<IShellBrowser> spBrowser;
    CComQIPtr<IServiceProvider>(spdisp)->
        QueryService(SID_STopLevelBrowser,
                    IID_PPV_ARGS(&spBrowser));

    CComPtr<IShellView> spView;
    spBrowser->QueryActiveShellView(&spView);

    spView->QueryInterface(riid, ppv);
  };
    
  auto _GetDesktopAutomationObject = [&](REFIID riid, void **ppv) -> void
  {
    CComPtr<IShellView> spsv;
    _FindDesktopFolderView (IID_PPV_ARGS(&spsv));
    CComPtr<IDispatch> spdispView;
    spsv->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&spdispView));
    spdispView->QueryInterface(riid, ppv);
  };

  CComPtr<IShellFolderViewDual> spFolderView;
  _GetDesktopAutomationObject (IID_PPV_ARGS(&spFolderView));
  CComPtr<IDispatch> spdispShell;
  spFolderView->get_Application(&spdispShell);
  CComQIPtr<IShellDispatch2> shell(spdispShell);

  HRESULT hr = E_UNEXPECTED;

  if (shell != nullptr)
  {
    PLOG_INFO                           << "Performing a ShellExecuteUnelevated call...";
    PLOG_INFO_IF(! path.empty())        << "File      : " << path;
    PLOG_INFO_IF(verb       != nullptr) << "Verb      : " << std::wstring(verb);
    PLOG_INFO_IF(parameters != nullptr) << "Parameters: " << std::wstring(parameters);
    PLOG_INFO_IF(directory  != nullptr) << "Directory : " << std::wstring(directory);

    hr = shell->ShellExecuteW (
                   CComBSTR    (path.data()),
                   CComVariant (parameters != nullptr ? parameters : L""),
                   CComVariant (directory  != nullptr ? directory  : L""),
                   CComVariant (verb       != nullptr ? verb       : L""),
                   CComVariant (nShow)
    );
  }

  return SUCCEEDED (hr);
}

// This stupid API doesn't support automatic elevation through e.g. "Run as administrator" registry flags etc
bool
SKIF_Util_CreateProcess (
                     const std::wstring_view& path,
                     const std::wstring_view& parameters,
                     const std::wstring_view& directory,
        std::map<std::wstring, std::wstring>* env,
                   SKIF_Util_CreateProcess_s* proc)
{
  // We need a path at least!
  //if (path.empty())
  //  return false;

  struct thread_s {
    std::wstring path              = L"";
    std::wstring parameters        = L""; // First token (argv[0]) is expected to be the module name
    std::wstring parameters_actual = L"";
    std::wstring directory         = L"";
    std::map<std::wstring, std::wstring> env;
    SKIF_Util_CreateProcess_s* proc;
  };
  
  thread_s* data = new thread_s;

  data->proc = proc;

  if (! path.empty ())
    data->path = path;

  if (! parameters.empty())
    data->parameters = parameters;

  // We use a custom combination of <"path" parameter> because many apps expects the module name as the first arg (argv[0]),
  //   and as such ignores it when processing command line arguments, so not doing that could cause unexpected behaviour.
  if (! path.empty ())
  {
    data->parameters_actual = (LR"(")" + std::wstring(path) + LR"(")");

    if (! parameters.empty())
      data->parameters_actual += (LR"( )" + std::wstring(parameters));
  }
  // If we have no path, forward the given parameters straight away
  else if (! parameters.empty())
    data->parameters_actual = data->parameters;

  // If a directory is not provided, retrieve the folder of the application we are about to launch
  if (! directory.empty())
    data->directory    = directory;
  else
  {
    wchar_t              wszExecutableBase [MAX_PATH] = { };
    StringCchCatW       (wszExecutableBase, MAX_PATH, path.data());
    PathRemoveFileSpecW (wszExecutableBase);
    data->directory    = wszExecutableBase;
  }
  
  if (env != nullptr)
    data->env = *env;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void * var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_CreateProcessWorker");

    // Is this combo really appropriate for this thread?
    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    PLOG_DEBUG << "SKIF_CreateProcessWorker thread started!";

    thread_s* _data = static_cast<thread_s*>(var);

    PROCESS_INFORMATION procinfo = { };
    STARTUPINFO         supinfo  = { };
    SecureZeroMemory  (&supinfo,   sizeof (STARTUPINFO));
    supinfo.cb                   = sizeof (STARTUPINFO);
      
    LPVOID       lpEnvBlock      = nullptr;
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
      
    PLOG_INFO                                          << "Creating process...";
    PLOG_INFO_IF  (! _data->path             .empty()) << "Application         : " << _data->path;
    PLOG_INFO_IF  (! _data->parameters       .empty()) << "Parameters          : " << _data->parameters;
    PLOG_INFO_IF  (! _data->parameters_actual.empty() && _data->parameters_actual  != _data->parameters) << "Parameters (actual) : " << _data->parameters_actual;
    PLOG_INFO_IF  (! _data->directory        .empty()) << "Directory           : " << _data->directory;
    PLOG_INFO_IF  (! _data->env              .empty()) << "Environment         : " << _data->env;

    if (CreateProcessW (
        (_data->path             .empty()) ? NULL : _data->path.c_str(),
        (_data->parameters_actual.empty()) ? NULL : const_cast<wchar_t *>(_data->parameters_actual.c_str()),
        NULL,
        NULL,
        FALSE,
        NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT,
        const_cast<wchar_t *>(wsEnvBlock.c_str()), // Environment variable block
        (_data->directory.empty()) ? NULL : _data->directory.c_str(),
        &supinfo,
        &procinfo))
    {
      if (_data->proc != nullptr)
      {
        _data->proc->iReturnCode.store (0);
        _data->proc->hProcess.store    (procinfo.hProcess);
        _data->proc->dwProcessId.store (procinfo.dwProcessId);
      } else // We don't have a proc structure, so the handle is unneeded
        CloseHandle (procinfo.hProcess);

      // Close the external thread handle as we do not use it for anything
      CloseHandle (procinfo.hThread);
    }

    // Use fallback if the primary failed
    else
    {
      DWORD error = GetLastError ( );
      PLOG_WARNING << "CreateProcess failed: " << SKIF_Util_GetErrorAsWStr (error);

      // In case elevation is needed, try ShellExecute!
      if (error == 740)
      {
        PLOG_INFO << "Attempting elevation fallback...";

        static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

        // Note that any new process will inherit SKIF's environment variables
        if (_registry._LoadedSteamOverlay)
          SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);

        // Set any custom environment variables
        for (auto& env_var : _data->env)
          SetEnvironmentVariable (env_var.first.c_str(), env_var.second.c_str());

        PLOG_INFO << "Performing a ShellExecuteEx call...";
  
        SHELLEXECUTEINFOW
          sexi              = { };
          sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
          sexi.lpVerb       = L"OPEN";
          sexi.lpFile       = (_data->path      .empty()) ? NULL : _data->path.c_str();
          sexi.lpParameters = (_data->parameters.empty()) ? NULL : _data->parameters.c_str();
          sexi.lpDirectory  = (_data->directory .empty()) ? NULL : _data->directory.c_str();
          sexi.nShow        = SW_SHOWNORMAL;
          sexi.fMask        = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS | SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;

        if (ShellExecuteExW (&sexi))
        {
          PLOG_INFO << "The operation was successful.";
          _data->proc->iReturnCode.store (0);
          _data->proc->hProcess.store    (sexi.hProcess);
        }

        else {
          error = GetLastError ( );
          PLOG_ERROR << "ShellExecuteEx failed: " << SKIF_Util_GetErrorAsWStr (error);
          _data->proc->iReturnCode.store (error);
        }

        // Remove any custom environment variables
        for (auto& env_var : _data->env)
          SetEnvironmentVariable (env_var.first.c_str(), NULL);

        if (_registry._LoadedSteamOverlay)
          SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");
      }

      // No fallback available
      else {
        PLOG_ERROR << "No fallback was available!";
        _data->proc->iReturnCode.store (error);
      }
    }

    // Free up the memory we allocated
    delete _data;

    PLOG_DEBUG << "SKIF_CreateProcessWorker thread stopped!";

    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    return 0;
  }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated && proc != nullptr)
    proc->hWorkerThread.store (hWorkerThread);
  else if (threadCreated) // We don't have a proc structure, so the handle is unneeded
    CloseHandle (hWorkerThread);
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
WINAPI
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
    SKIF_GetFileVersionInfoExW = (GetFileVersionInfoExW_pfn)GetProcAddress (
                LoadLibraryEx ( L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "GetFileVersionInfoExW"                                            );
  */

  DWORD start = SKIF_Util_timeGetTime1 ( );

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
    }
  }

  DWORD stop = SKIF_Util_timeGetTime1 ( );

  PLOG_DEBUG << "Processing took " << (stop - start) << " ms.";

  return (cbVersionBytes) ? wszVersion : L"";
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

  DWORD start = SKIF_Util_timeGetTime1 ( );

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
  }

  DWORD stop = SKIF_Util_timeGetTime1 ( );

  PLOG_DEBUG << "Processing took " << (stop - start) << " ms.";

  return (cbVersionBytes) ? wszVersion  : L"";
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

HybridDetect::PROCESSOR_INFO*
SKIF_Util_GetProcessInfoHybridDetect (void)
{
  static HybridDetect::PROCESSOR_INFO procInfo;
  
  static bool
      runOnce = true;
  if (runOnce)
  {   runOnce = false;

    // Use Intel's HybridDetect to retrieve processor capabilities
    HybridDetect::GetProcessorInfo (procInfo);
    PLOG_INFO << "Detected CPU model: " << procInfo.brandString << ((procInfo.hybrid) ? " (Hybrid)" : "");
  }

  return &procInfo;
}

BOOL
WINAPI
SKIF_Util_GetSystemCpuSetInformation (PSYSTEM_CPU_SET_INFORMATION Information, ULONG BufferLength, PULONG ReturnedLength, HANDLE Process, ULONG Flags)
{
  using GetSystemCpuSetInformation_pfn =
    BOOL (WINAPI *)(PSYSTEM_CPU_SET_INFORMATION Information, ULONG BufferLength, PULONG ReturnedLength, HANDLE Process, ULONG Flags);

  static GetSystemCpuSetInformation_pfn
    SKIF_GetSystemCpuSetInformation =
        (GetSystemCpuSetInformation_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "GetSystemCpuSetInformation");

  if (SKIF_GetSystemCpuSetInformation == nullptr)
    return FALSE;
  
  return SKIF_GetSystemCpuSetInformation (Information, BufferLength, ReturnedLength, Process, Flags);
}

bool
SKIF_Util_SetThreadPrefersECores (void)
{
  if (! SKIF_Util_IsWindows10OrGreater ( ))
    return false;

  HybridDetect::PROCESSOR_INFO procInfo = *SKIF_Util_GetProcessInfoHybridDetect ( );
  
  bool succeeded = false;
  
  // From Intel's Game Dev Guide for 12th Gen Intel® Core™ Processor:
  //  - CPU Sets provide APIs to declare application thread affinity in a “soft” manner that is compatible with OS power management (unlike the ThreadAffinityMask APIs).
  //  - SetThreadAffinityMask() is in the “strong” affinity class of Windows API functions.
  //
  // Enforcing E-core usage at the thread level using CPU sets seems pretty spotty with little effect, according to Intel VTune Profiler,
  //   however setting it using an affinity mask means any new child process inherits the affinity mask, which we absolutely do not want!
  if (procInfo.hybrid)
  {

#ifdef ENABLE_CPU_SETS
    succeeded =
      (1 == HybridDetect::RunOn (procInfo, HybridDetect::CoreTypes::INTEL_ATOM, procInfo.cpuSets   [HybridDetect::CoreTypes::ANY]));

    PLOG_VERBOSE_IF(succeeded) << "The CPU set of the thread was set to E-cores!";
#else
    false; // Do not use affinity mask as this is inherited by any spawned child process!
    //(1 == HybridDetect::RunOn (procInfo, HybridDetect::CoreTypes::INTEL_ATOM, procInfo.coreMasks [HybridDetect::CoreTypes::ANY]));

    PLOG_VERBOSE_IF(succeeded) << "The affinity mask of the thread was set to E-cores!";
#endif
  }

  return succeeded;
}

BOOL
WINAPI
SKIF_Util_SetThreadInformation (HANDLE hThread, THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID ThreadInformation, DWORD ThreadInformationSize)
{
  using SetThreadInformation_pfn =
    BOOL (WINAPI *)(HANDLE hThread, THREAD_INFORMATION_CLASS ThreadInformationClass, LPVOID ThreadInformation, DWORD ThreadInformationSize);

  static SetThreadInformation_pfn
    SKIF_SetThreadInformation =
        (SetThreadInformation_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetThreadInformation");

  if (SKIF_SetThreadInformation == nullptr)
    return FALSE;
  
  return SKIF_SetThreadInformation (hThread, ThreadInformationClass, ThreadInformation, ThreadInformationSize);
}

HRESULT
WINAPI
SKIF_Util_SetThreadDescription (HANDLE hThread, PCWSTR lpThreadDescription)
{
  using SetThreadDescription_pfn =
    HRESULT (WINAPI *)(HANDLE hThread, PCWSTR lpThreadDescription);

  static SetThreadDescription_pfn
    SKIF_SetThreadDescription =
        (SetThreadDescription_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetThreadDescription");

  if (SKIF_SetThreadDescription == nullptr)
    return 0;
  
  return SKIF_SetThreadDescription (hThread, lpThreadDescription);
}

BOOL
WINAPI
SKIF_Util_SetThreadSelectedCpuSets (HANDLE hThread, const ULONG* CpuSetIds, ULONG CpuSetIdCount)
{
  using SetThreadSelectedCpuSets_pfn =
    BOOL (WINAPI *)(HANDLE Thread, const ULONG* CpuSetIds, ULONG CpuSetIdCount);

  static SetThreadSelectedCpuSets_pfn
    SKIF_SetThreadSelectedCpuSets =
        (SetThreadSelectedCpuSets_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetThreadSelectedCpuSets");

  if (SKIF_SetThreadSelectedCpuSets == nullptr)
    return FALSE;
  
  return SKIF_SetThreadSelectedCpuSets (hThread, CpuSetIds, CpuSetIdCount);
}

// Sets the power throttling execution speed (EcoQoS) of a thread
//   1 = enable; 0 = disable; -1 = auto-managed
bool
SKIF_Util_SetThreadPowerThrottling (HANDLE threadHandle, INT state)
{
  THREAD_POWER_THROTTLING_STATE throttlingState;
  ZeroMemory(&throttlingState, sizeof(throttlingState));

  throttlingState.Version     =                THREAD_POWER_THROTTLING_CURRENT_VERSION;
  throttlingState.ControlMask = (state > -1) ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
  throttlingState.StateMask   = (state == 1) ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;

  return SKIF_Util_SetThreadInformation (threadHandle, ThreadPowerThrottling, &throttlingState, sizeof(throttlingState));
}

// Sets the memory priority of a thread
bool
SKIF_Util_SetThreadMemoryPriority (HANDLE threadHandle, ULONG memoryPriority)
{
  SKIF_MEMORY_PRIORITY_INFORMATION memoryPriorityInfo;
  ZeroMemory(&memoryPriorityInfo, sizeof(memoryPriorityInfo));

  memoryPriorityInfo.MemoryPriority = memoryPriority;

  return SKIF_Util_SetThreadInformation (threadHandle, ThreadMemoryPriority, &memoryPriorityInfo, sizeof(memoryPriorityInfo));
}

bool
SKIF_Util_SetProcessPrefersECores (void)
{
  if (! SKIF_Util_IsWindows10OrGreater ( ))
    return false;

  HybridDetect::PROCESSOR_INFO procInfo = *SKIF_Util_GetProcessInfoHybridDetect ( );

  bool succeeded = false;
  
  // From Intel's Game Dev Guide for 12th Gen Intel® Core™ Processor:
  //  - CPU Sets provide APIs to declare application thread affinity in a “soft” manner that is compatible with OS power management (unlike the ThreadAffinityMask APIs).
  //  - SetThreadAffinityMask() is in the “strong” affinity class of Windows API functions.
  //
  // Enforcing E-core usage at the thread level using CPU sets seems pretty spotty with little effect, according to Intel VTune Profiler,
  //   however setting it using an affinity mask means any new child process inherits the affinity mask, which we absolutely do not want!
  if (procInfo.hybrid)
  {
    succeeded =
#ifdef ENABLE_CPU_SETS
      (1 == HybridDetect::RunProcOn (procInfo, SKIF_Util_GetCurrentProcess(), HybridDetect::CoreTypes::INTEL_ATOM, procInfo.cpuSets   [HybridDetect::CoreTypes::ANY]));

    PLOG_VERBOSE_IF(succeeded) << "The default CPU set of the process was set to E-cores!";
#else
      false; // Do not use affinity mask as this is inherited by any spawned child process!
    (1 == HybridDetect::RunProcOn (procInfo, SKIF_Util_GetCurrentProcess(), HybridDetect::CoreTypes::INTEL_ATOM, procInfo.coreMasks [HybridDetect::CoreTypes::ANY]));

    PLOG_VERBOSE_IF(succeeded) << "The affinity mask of the process was set to E-cores!";
#endif
  }

  return succeeded;
}

BOOL
WINAPI
SKIF_Util_SetProcessDefaultCpuSets (HANDLE hProcess, const ULONG* CpuSetIds, ULONG CpuSetIdCount)
{
  using SetProcessDefaultCpuSets_pfn =
    BOOL (WINAPI *)(HANDLE Thread, const ULONG* CpuSetIds, ULONG CpuSetIdCount);

  static SetProcessDefaultCpuSets_pfn
    SKIF_SetProcessDefaultCpuSets =
        (SetProcessDefaultCpuSets_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetProcessDefaultCpuSets");

  if (SKIF_SetProcessDefaultCpuSets == nullptr)
    return FALSE;
  
  return SKIF_SetProcessDefaultCpuSets (hProcess, CpuSetIds, CpuSetIdCount);
}

BOOL
WINAPI
SKIF_Util_SetProcessInformation (HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize)
{
  // SetProcessInformation (Windows 8+)
  using SetProcessInformation_pfn =
    BOOL (WINAPI *)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

  static SetProcessInformation_pfn
    SKIF_SetProcessInformation =
        (SetProcessInformation_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetProcessInformation");

  if (SKIF_SetProcessInformation == nullptr)
    return FALSE;

  return SKIF_SetProcessInformation (hProcess, ProcessInformationClass, ProcessInformation, ProcessInformationSize);
}

// Sets the power throttling execution speed (EcoQoS) of a process
//   1 = enable; 0 = disable; -1 = auto-managed
bool
SKIF_Util_SetProcessPowerThrottling (HANDLE processHandle, INT state)
{
  PROCESS_POWER_THROTTLING_STATE throttlingState;
  ZeroMemory(&throttlingState, sizeof(throttlingState));

  throttlingState.Version     =                PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  throttlingState.ControlMask = (state > -1) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
  throttlingState.StateMask   = (state == 1) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

  return SKIF_Util_SetProcessInformation (processHandle, ProcessPowerThrottling, &throttlingState, sizeof(throttlingState));
}

// Sets the memory priority of a process
bool
SKIF_Util_SetProcessMemoryPriority (HANDLE processHandle, ULONG memoryPriority)
{
  SKIF_MEMORY_PRIORITY_INFORMATION memoryPriorityInfo;
  ZeroMemory(&memoryPriorityInfo, sizeof(memoryPriorityInfo));

  memoryPriorityInfo.MemoryPriority = memoryPriority;

  return SKIF_Util_SetProcessInformation (processHandle, ProcessMemoryPriority, &memoryPriorityInfo, sizeof(memoryPriorityInfo));
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
SKIF_Util_IsTouchCapable (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  int digitizer = ::GetSystemMetrics (SM_DIGITIZER);
               // ::GetSystemMetrics (SM_MAXIMUMTOUCHES) > 0

  return (_registry.bTouchInput  &&
         (digitizer & NID_READY) &&
         (digitizer & NID_INTEGRATED_TOUCH));
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
SKIF_Util_SaveExtractExeIcon (std::wstring sourcePath, std::wstring targetPath)
{
  bool  ret  = PathFileExists (targetPath.c_str());

  if (! ret && PathFileExists (sourcePath.c_str()))
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
    HRESULT hr = SHDefExtractIcon (sourcePath.c_str(), 0, 0, &hIcon, 0, 32); // 256
    if (SUCCEEDED(hr) && hr != S_FALSE) // S_FALSE = The requested icon is not present.
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

    // Something went wrong -- let's try to look for an .ico by the same filename instead
    else if (sourcePath.rfind(L".exe") != std::wstring::npos)
    {
      sourcePath.replace(sourcePath.rfind(L".exe"), 4, L".ico");
      ret = SKIF_Util_SaveExtractExeIcon (sourcePath, targetPath);
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
  DWORD dwSize           =  0;
  WCHAR szData[MAX_PATH] = { };

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
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

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
        if (ERROR_SUCCESS == RegQueryValueEx (hKey, _path_cache.skif_executable, NULL, NULL, NULL, NULL))
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


typedef unsigned long DWMOverlayTestModeFlags;  // -> enum DWMOverlayTestModeFlags_

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

  static DWMOverlayTestModeFlags flagOverlayTestMode = DWMOverlayTestModeFlags_INITIAL;
  static int iDisableOverlay = 0;
  static bool  isDisabled    = false;

  if (flagOverlayTestMode != DWMOverlayTestModeFlags_INITIAL && ! refresh)
    return isDisabled;

  isDisabled = false;

  HKEY hKey;
  unsigned long size = 1024;

  // Check if GraphicsDrivers's DisableOverlays has MPOs disabled
  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SYSTEM\CurrentControlSet\Control\GraphicsDrivers\)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
  {
    if (ERROR_SUCCESS == RegQueryValueEx (hKey, L"DisableOverlays", NULL, NULL, (LPBYTE)&iDisableOverlay, &size))
      PLOG_VERBOSE << "DisableOverlays registry value is set to: " << iDisableOverlay;
    else
      iDisableOverlay = 0;

    RegCloseKey (hKey);
  }

  else
    iDisableOverlay = 0;

  // Check if DWM's OverlayTestMode has MPOs disabled
  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\Dwm\)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
  {
    if (ERROR_SUCCESS == RegQueryValueEx (hKey, L"OverlayTestMode", NULL, NULL, (LPBYTE)&flagOverlayTestMode, &size))
      PLOG_VERBOSE << "OverlayTestMode registry value is set to: " << flagOverlayTestMode;
    else
      flagOverlayTestMode = DWMOverlayTestModeFlags_None;

    RegCloseKey (hKey);
  }

  else
    flagOverlayTestMode = DWMOverlayTestModeFlags_None;

  
  if (iDisableOverlay || ((flagOverlayTestMode & DWMOverlayTestModeFlags_MPORelated1) == DWMOverlayTestModeFlags_MPORelated1 &&
                          (flagOverlayTestMode & DWMOverlayTestModeFlags_MPORelated2) == DWMOverlayTestModeFlags_MPORelated2))
    isDisabled = true;

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

  if (dwPeriod < 8)
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

      wchar_t *pszDestination = static_cast<wchar_t*> (GlobalLock (hGlobal));

      if (pszDestination != nullptr)
      {
        memcpy (pszDestination, data.data(), (data.length() + 1) * sizeof (wchar_t));
        GlobalUnlock (hGlobal);
       
        result = SetClipboardData (CF_UNICODETEXT, hGlobal);
      }

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

      PLOG_ERROR << L"WinInet Failure: " << SKIF_Util_GetErrorAsWStr (GetLastError ( ), GetModuleHandle (L"wininet.dll"));
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

SKIF_DirectoryWatch::SKIF_DirectoryWatch (std::wstring_view wstrPath, UITab waitTab, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
  registerNotify (wstrPath, waitTab, bWatchSubtree, dwNotifyFilter);
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
SKIF_DirectoryWatch::isSignaled (std::wstring_view wstrPath, UITab waitTab, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
  bool bRet = false;

  if (_hChangeNotification != INVALID_HANDLE_VALUE)
    bRet = isSignaled ( );

  else if (! wstrPath.empty())
    registerNotify (wstrPath, waitTab, bWatchSubtree, dwNotifyFilter);

  return bRet;
}

void
SKIF_DirectoryWatch::reset (void)
{
  if (      _hChangeNotification != INVALID_HANDLE_VALUE)
    FindCloseChangeNotification (_hChangeNotification);


  if (_waitTab == UITab_ALL)
  {
    for (auto& vWatchHandle : vWatchHandles)
    {
      if (! vWatchHandle.empty())
        vWatchHandle.erase(std::remove(vWatchHandle.begin(), vWatchHandle.end(), _hChangeNotification), vWatchHandle.end());
    }
  }
  else if (_waitTab != UITab_None && ! vWatchHandles[_waitTab].empty())
    vWatchHandles[_waitTab].erase(std::remove(vWatchHandles[_waitTab].begin(), vWatchHandles[_waitTab].end(), _hChangeNotification), vWatchHandles[_waitTab].end());

  // Reset variables
  _hChangeNotification = INVALID_HANDLE_VALUE;
  _waitTab             = UITab_None; // ?
  _path                = L"";
}

void
SKIF_DirectoryWatch::registerNotify (std::wstring_view wstrPath, UITab waitTab, BOOL bWatchSubtree, DWORD dwNotifyFilter)
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

      _waitTab  = waitTab;

      if (_waitTab == UITab_ALL)
      {
        for (auto& vWatchHandle : vWatchHandles)
          vWatchHandle.push_back (_hChangeNotification);
      }
      else if (_waitTab != UITab_None)
        vWatchHandles[_waitTab].push_back (_hChangeNotification);
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

SKIF_RegistryWatch::SKIF_RegistryWatch ( HKEY hRootKey, const wchar_t* wszSubKey, const wchar_t* wszEventName, BOOL bWatchSubtree, DWORD dwNotifyFilter, UITab waitTab, bool bWOW6432Key, bool bWOW6464Key )
{
  _init.root          = hRootKey;
  _init.sub_key       = wszSubKey;
  _init.watch_subtree = bWatchSubtree;
  _init.filter_mask   = dwNotifyFilter;
  _init.wow64_32key   = bWOW6432Key;
  _init.wow64_64key   = bWOW6464Key;
  _waitTab            = waitTab;

  _hEvent             =
      CreateEvent ( nullptr, TRUE,
                            FALSE, wszEventName );

  reset ();

  if (_waitTab != UITab_None && _hEvent != NULL)
    vWatchHandles[_waitTab].push_back(_hEvent);

  if (_hEvent != NULL)
  {
    if (_waitTab == UITab_ALL)
    {
      for (auto& vWatchHandle : vWatchHandles)
        vWatchHandle.push_back (_hEvent);
    }
    else if (_waitTab != UITab_None)
      vWatchHandles[_waitTab].push_back (_hEvent);
  }
}

SKIF_RegistryWatch::~SKIF_RegistryWatch (void)
{
  if (_hEvent != NULL)
  {
    if (_waitTab == UITab_ALL)
    {
      for (auto& vWatchHandle : vWatchHandles)
      {
        if (! vWatchHandle.empty())
          vWatchHandle.erase(std::remove(vWatchHandle.begin(), vWatchHandle.end(), _hEvent), vWatchHandle.end());
      }
    }
    else if (_waitTab != UITab_None && ! vWatchHandles[_waitTab].empty())
      vWatchHandles[_waitTab].erase(std::remove(vWatchHandles[_waitTab].begin(), vWatchHandles[_waitTab].end(), _hEvent), vWatchHandles[_waitTab].end());
  }

  RegCloseKey (_hKeyBase);
  CloseHandle (_hEvent);
  _hEvent = NULL;
}

LSTATUS
SKIF_RegistryWatch::registerNotify (void)
{
  return RegNotifyChangeKeyValue (_hKeyBase, _init.watch_subtree, _init.filter_mask, _hEvent, TRUE);
}

void
SKIF_RegistryWatch::reset (void)
{
  RegCloseKey (_hKeyBase);

  if ((intptr_t)_hEvent > 0)
    ResetEvent (_hEvent);

  LSTATUS lStat =
    RegOpenKeyEx (_init.root, _init.sub_key.c_str (), 0, KEY_NOTIFY
                              | ((_init.wow64_32key)  ?  KEY_WOW64_32KEY : 0x0)
                              | ((_init.wow64_64key)  ?  KEY_WOW64_64KEY : 0x0), &_hKeyBase);

  if (lStat == ERROR_SUCCESS)
    lStat = registerNotify ( );

  if (lStat != ERROR_SUCCESS)
  {
    PLOG_ERROR << "Failed to register for registry notifications: " << _init.sub_key << ", " << _init.watch_subtree;
    RegCloseKey (_hKeyBase);
    CloseHandle (_hEvent);
    _hEvent = NULL;
  }
}

bool
SKIF_RegistryWatch::isSignaled (void)
{
  if (_hEvent == NULL)
    return false;

  bool signaled =
    WaitForSingleObjectEx (
      _hEvent, 0UL, FALSE
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
          // In the case of a Unicode string, there is no limitation on maximum string length.
          // In the case of an ANSI string, the maximum length of the returned string varies depending on the version of Windows—MAX_PATH prior to Windows 2000 and INFOTIPSIZE (defined in Commctrl.h) in Windows 2000 and later.
          if (SUCCEEDED (psl->GetArguments (szArguments, 1024)))
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

  PLOG_INFO                                      << "Creating a desktop shortcut...";
  PLOG_INFO_IF(lpszPathLink != nullptr)          << "Shortcut    : " << std::wstring(lpszPathLink);
  PLOG_INFO_IF(lpszTarget   != nullptr)          << "Target      : " << std::wstring(lpszTarget);
  PLOG_INFO_IF(wcscmp (lpszArgs,    L"\0") != 0) << "Parameters  : " << std::wstring(lpszArgs);
  PLOG_INFO_IF(wcscmp (lpszWorkDir, L"\0") != 0) << "Start in    : " << std::wstring(lpszWorkDir);
  PLOG_INFO_IF(wcscmp (lpszDesc,    L"\0") != 0) << "Description : " << std::wstring(lpszDesc);

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

  if (ret)
    PLOG_INFO << "Shortcut was created successfully.";
  else
    PLOG_ERROR << "Failed to create shortcut!";

  return ret;
}

std::string
SKIF_Util_GetWindowMessageAsStr (UINT msg)
{
  // https://www.autoitscript.com/autoit3/docs/appendix/WinMsgCodes.htm

  // Some random defines that usually pops up every now and then
#define WM_UAHDESTROYWINDOW             0x0090
#define WM_UAHDRAWMENU                  0x0091
#define WM_UAHDRAWMENUITEM              0x0092
#define WM_UAHINITMENU                  0x0093
#define WM_UAHMEASUREMENUITEM           0x0094
#define WM_UAHNCPAINTMENUPOPUP          0x0095
#define WM_UAHUPDATE                    0x0096

  // Some are undefined with our current inclusions and so are excluded
  switch (msg)
  {
    case WM_NULL: return "WM_NULL";
    case WM_CREATE: return "WM_CREATE";
    case WM_DESTROY: return "WM_DESTROY";
    case WM_MOVE: return "WM_MOVE";
    case WM_SIZE: return "WM_SIZE";
    case WM_ACTIVATE: return "WM_ACTIVATE";
    case WM_SETFOCUS: return "WM_SETFOCUS";
    case WM_KILLFOCUS: return "WM_KILLFOCUS";
    case WM_ENABLE: return "WM_ENABLE";
    case WM_SETREDRAW: return "WM_SETREDRAW";
    case WM_SETTEXT: return "WM_SETTEXT";
    case WM_GETTEXT: return "WM_GETTEXT";
    case WM_GETTEXTLENGTH: return "WM_GETTEXTLENGTH";
    case WM_PAINT: return "WM_PAINT";
    case WM_CLOSE: return "WM_CLOSE";
    case WM_QUERYENDSESSION: return "WM_QUERYENDSESSION";
    case WM_QUIT: return "WM_QUIT";
    case WM_QUERYOPEN: return "WM_QUERYOPEN";
    case WM_ERASEBKGND: return "WM_ERASEBKGND";
    case WM_SYSCOLORCHANGE: return "WM_SYSCOLORCHANGE";
    case WM_ENDSESSION: return "WM_ENDSESSION";
    case WM_SHOWWINDOW: return "WM_SHOWWINDOW";
  //case WM_CTLCOLOR: return "WM_CTLCOLOR"; // 16-bit Windows
    case WM_SETTINGCHANGE: return "WM_SETTINGCHANGE";
  //case WM_WININICHANGE: return "WM_WININICHANGE"; // Alias of WM_SETTINGCHANGE
    case WM_DEVMODECHANGE: return "WM_DEVMODECHANGE";
    case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
    case WM_FONTCHANGE: return "WM_FONTCHANGE";
    case WM_TIMECHANGE: return "WM_TIMECHANGE";
    case WM_CANCELMODE: return "WM_CANCELMODE";
    case WM_SETCURSOR: return "WM_SETCURSOR";
    case WM_MOUSEACTIVATE: return "WM_MOUSEACTIVATE";
    case WM_CHILDACTIVATE: return "WM_CHILDACTIVATE";
    case WM_QUEUESYNC: return "WM_QUEUESYNC";
    case WM_GETMINMAXINFO: return "WM_GETMINMAXINFO";
    case WM_PAINTICON: return "WM_PAINTICON";
    case WM_ICONERASEBKGND: return "WM_ICONERASEBKGND";
    case WM_NEXTDLGCTL: return "WM_NEXTDLGCTL";
    case WM_SPOOLERSTATUS: return "WM_SPOOLERSTATUS";
    case WM_DRAWITEM: return "WM_DRAWITEM";
    case WM_MEASUREITEM: return "WM_MEASUREITEM";
    case WM_DELETEITEM: return "WM_DELETEITEM";
    case WM_VKEYTOITEM: return "WM_VKEYTOITEM";
    case WM_CHARTOITEM: return "WM_CHARTOITEM";
    case WM_SETFONT: return "WM_SETFONT";
    case WM_GETFONT: return "WM_GETFONT";
    case WM_SETHOTKEY: return "WM_SETHOTKEY";
    case WM_GETHOTKEY: return "WM_GETHOTKEY";
    case WM_QUERYDRAGICON: return "WM_QUERYDRAGICON";
    case WM_COMPAREITEM: return "WM_COMPAREITEM";
    case WM_GETOBJECT: return "WM_GETOBJECT";
    case WM_COMPACTING: return "WM_COMPACTING";
    case WM_COMMNOTIFY: return "WM_COMMNOTIFY";
    case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
    case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
    case WM_POWER: return "WM_POWER";
  //case WM_COPYGLOBALDATA: return "WM_COPYGLOBALDATA";
    case WM_COPYDATA: return "WM_COPYDATA";
    case WM_CANCELJOURNAL: return "WM_CANCELJOURNAL";
    case WM_NOTIFY: return "WM_NOTIFY";
    case WM_INPUTLANGCHANGEREQUEST: return "WM_INPUTLANGCHANGEREQUEST";
    case WM_INPUTLANGCHANGE: return "WM_INPUTLANGCHANGE";
    case WM_TCARD: return "WM_TCARD";
    case WM_HELP: return "WM_HELP";
    case WM_USERCHANGED: return "WM_USERCHANGED";
    case WM_NOTIFYFORMAT: return "WM_NOTIFYFORMAT";
    case WM_CONTEXTMENU: return "WM_CONTEXTMENU";
    case WM_STYLECHANGING: return "WM_STYLECHANGING";
    case WM_STYLECHANGED: return "WM_STYLECHANGED";
    case WM_DISPLAYCHANGE: return "WM_DISPLAYCHANGE";
    case WM_GETICON: return "WM_GETICON";
    case WM_SETICON: return "WM_SETICON";
    case WM_NCCREATE: return "WM_NCCREATE";
    case WM_NCDESTROY: return "WM_NCDESTROY";
    case WM_NCCALCSIZE: return "WM_NCCALCSIZE";
    case WM_NCHITTEST: return "WM_NCHITTEST";
    case WM_NCPAINT: return "WM_NCPAINT";
    case WM_NCACTIVATE: return "WM_NCACTIVATE";
    case WM_GETDLGCODE: return "WM_GETDLGCODE";
    case WM_SYNCPAINT: return "WM_SYNCPAINT";
    case WM_UAHDESTROYWINDOW: "WM_UAHDESTROYWINDOW";
    case WM_UAHDRAWMENU: "WM_UAHDRAWMENU";
    case WM_UAHDRAWMENUITEM: "WM_UAHDRAWMENUITEM";
    case WM_UAHINITMENU: "WM_UAHINITMENU";
    case WM_UAHMEASUREMENUITEM: "WM_UAHMEASUREMENUITEM";
    case WM_UAHNCPAINTMENUPOPUP: "WM_UAHNCPAINTMENUPOPUP";
    case WM_UAHUPDATE: return "WM_UAHUPDATE";
    case WM_NCMOUSEMOVE: return "WM_NCMOUSEMOVE";
    case WM_NCLBUTTONDOWN: return "WM_NCLBUTTONDOWN";
    case WM_NCLBUTTONUP: return "WM_NCLBUTTONUP";
    case WM_NCLBUTTONDBLCLK: return "WM_NCLBUTTONDBLCLK";
    case WM_NCRBUTTONDOWN: return "WM_NCRBUTTONDOWN";
    case WM_NCRBUTTONUP: return "WM_NCRBUTTONUP";
    case WM_NCRBUTTONDBLCLK: return "WM_NCRBUTTONDBLCLK";
    case WM_NCMBUTTONDOWN: return "WM_NCMBUTTONDOWN";
    case WM_NCMBUTTONUP: return "WM_NCMBUTTONUP";
    case WM_NCMBUTTONDBLCLK: return "WM_NCMBUTTONDBLCLK";
    case WM_NCXBUTTONDOWN: return "WM_NCXBUTTONDOWN";
    case WM_NCXBUTTONUP: return "WM_NCXBUTTONUP";
    case WM_NCXBUTTONDBLCLK: return "WM_NCXBUTTONDBLCLK";
    case EM_GETSEL: return "EM_GETSEL";
    case EM_SETSEL: return "EM_SETSEL";
    case EM_GETRECT: return "EM_GETRECT";
    case EM_SETRECT: return "EM_SETRECT";
    case EM_SETRECTNP: return "EM_SETRECTNP";
    case EM_SCROLL: return "EM_SCROLL";
    case EM_LINESCROLL: return "EM_LINESCROLL";
    case EM_SCROLLCARET: return "EM_SCROLLCARET";
    case EM_GETMODIFY: return "EM_GETMODIFY";
    case EM_SETMODIFY: return "EM_SETMODIFY";
    case EM_GETLINECOUNT: return "EM_GETLINECOUNT";
    case EM_LINEINDEX: return "EM_LINEINDEX";
    case EM_SETHANDLE: return "EM_SETHANDLE";
    case EM_GETHANDLE: return "EM_GETHANDLE";
    case EM_GETTHUMB: return "EM_GETTHUMB";
    case EM_LINELENGTH: return "EM_LINELENGTH";
    case EM_REPLACESEL: return "EM_REPLACESEL";
  //case EM_SETFONT: return "EM_SETFONT";
    case EM_GETLINE: return "EM_GETLINE";
    case EM_LIMITTEXT: return "EM_LIMITTEXT / EM_SETLIMITTEXT";
    case EM_CANUNDO: return "EM_CANUNDO";
    case EM_UNDO: return "EM_UNDO";
    case EM_FMTLINES: return "EM_FMTLINES";
    case EM_LINEFROMCHAR: return "EM_LINEFROMCHAR";
  //case EM_SETWORDBREAK: return "EM_SETWORDBREAK";
    case EM_SETTABSTOPS: return "EM_SETTABSTOPS";
    case EM_SETPASSWORDCHAR: return "EM_SETPASSWORDCHAR";
    case EM_EMPTYUNDOBUFFER: return "EM_EMPTYUNDOBUFFER";
    case EM_GETFIRSTVISIBLELINE: return "EM_GETFIRSTVISIBLELINE";
    case EM_SETREADONLY: return "EM_SETREADONLY";
    case EM_SETWORDBREAKPROC: return "EM_SETWORDBREAKPROC";
    case EM_GETWORDBREAKPROC: return "EM_GETWORDBREAKPROC";
    case EM_GETPASSWORDCHAR: return "EM_GETPASSWORDCHAR";
    case EM_SETMARGINS: return "EM_SETMARGINS";
    case EM_GETMARGINS: return "EM_GETMARGINS";
    case EM_GETLIMITTEXT: return "EM_GETLIMITTEXT";
    case EM_POSFROMCHAR: return "EM_POSFROMCHAR";
    case EM_CHARFROMPOS: return "EM_CHARFROMPOS";
    case EM_SETIMESTATUS: return "EM_SETIMESTATUS";
    case EM_GETIMESTATUS: return "EM_GETIMESTATUS";
    case SBM_SETPOS: return "SBM_SETPOS";
    case SBM_GETPOS: return "SBM_GETPOS";
    case SBM_SETRANGE: return "SBM_SETRANGE";
    case SBM_GETRANGE: return "SBM_GETRANGE";
    case SBM_ENABLE_ARROWS: return "SBM_ENABLE_ARROWS";
    case SBM_SETRANGEREDRAW: return "SBM_SETRANGEREDRAW";
    case SBM_SETSCROLLINFO: return "SBM_SETSCROLLINFO";
    case SBM_GETSCROLLINFO: return "SBM_GETSCROLLINFO";
    case SBM_GETSCROLLBARINFO: return "SBM_GETSCROLLBARINFO";
    case BM_GETCHECK: return "BM_GETCHECK";
    case BM_SETCHECK: return "BM_SETCHECK";
    case BM_GETSTATE: return "BM_GETSTATE";
    case BM_SETSTATE: return "BM_SETSTATE";
    case BM_SETSTYLE: return "BM_SETSTYLE";
    case BM_CLICK: return "BM_CLICK";
    case BM_GETIMAGE: return "BM_GETIMAGE";
    case BM_SETIMAGE: return "BM_SETIMAGE";
    case BM_SETDONTCLICK: return "BM_SETDONTCLICK";
    case WM_INPUT: return "WM_INPUT";
    case WM_KEYDOWN: return "WM_KEYDOWN";
  //case WM_KEYFIRST: return "WM_KEYFIRST"; // Alias for WM_KEYDOWN
    case WM_KEYUP: return "WM_KEYUP";
    case WM_CHAR: return "WM_CHAR";
    case WM_DEADCHAR: return "WM_DEADCHAR";
    case WM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
    case WM_SYSKEYUP: return "WM_SYSKEYUP";
    case WM_SYSCHAR: return "WM_SYSCHAR";
    case WM_SYSDEADCHAR: return "WM_SYSDEADCHAR";
    case WM_UNICHAR: return "WM_UNICHAR";
  //case WM_KEYLAST: return "WM_KEYLAST"; // Alias for WM_UNICHAR
  //case WM_WNT_CONVERTREQUESTEX: return "WM_WNT_CONVERTREQUESTEX";
  //case WM_CONVERTREQUEST: return "WM_CONVERTREQUEST";
  //case WM_CONVERTRESULT: return "WM_CONVERTRESULT";
  //case WM_INTERIM: return "WM_INTERIM";
    case WM_IME_STARTCOMPOSITION: return "WM_IME_STARTCOMPOSITION";
    case WM_IME_ENDCOMPOSITION: return "WM_IME_ENDCOMPOSITION";
    case WM_IME_COMPOSITION: return "WM_IME_COMPOSITION";
  //case WM_IME_KEYLAST: return "WM_IME_KEYLAST"; // Alias for WM_IME_COMPOSITION
    case WM_INITDIALOG: return "WM_INITDIALOG";
    case WM_COMMAND: return "WM_COMMAND";
    case WM_SYSCOMMAND: return "WM_SYSCOMMAND";
    case WM_TIMER: return "WM_TIMER";
    case WM_HSCROLL: return "WM_HSCROLL";
    case WM_VSCROLL: return "WM_VSCROLL";
    case WM_INITMENU: return "WM_INITMENU";
    case WM_INITMENUPOPUP: return "WM_INITMENUPOPUP";
  //case WM_SYSTIMER: return "WM_SYSTIMER"; // Undocumented, used for caret blinks
    case WM_MENUSELECT: return "WM_MENUSELECT";
    case WM_MENUCHAR: return "WM_MENUCHAR";
    case WM_ENTERIDLE: return "WM_ENTERIDLE";
    case WM_MENURBUTTONUP: return "WM_MENURBUTTONUP";
    case WM_MENUDRAG: return "WM_MENUDRAG";
    case WM_MENUGETOBJECT: return "WM_MENUGETOBJECT";
    case WM_UNINITMENUPOPUP: return "WM_UNINITMENUPOPUP";
    case WM_MENUCOMMAND: return "WM_MENUCOMMAND";
    case WM_CHANGEUISTATE: return "WM_CHANGEUISTATE";
    case WM_UPDATEUISTATE: return "WM_UPDATEUISTATE";
    case WM_QUERYUISTATE: return "WM_QUERYUISTATE";
  //case WM_LBTRACKPOINT: return "WM_LBTRACKPOINT";
    case WM_CTLCOLORMSGBOX: return "WM_CTLCOLORMSGBOX";
    case WM_CTLCOLOREDIT: return "WM_CTLCOLOREDIT";
    case WM_CTLCOLORLISTBOX: return "WM_CTLCOLORLISTBOX";
    case WM_CTLCOLORBTN: return "WM_CTLCOLORBTN";
    case WM_CTLCOLORDLG: return "WM_CTLCOLORDLG";
    case WM_CTLCOLORSCROLLBAR: return "WM_CTLCOLORSCROLLBAR";
    case WM_CTLCOLORSTATIC: return "WM_CTLCOLORSTATIC";
    case CB_GETEDITSEL: return "CB_GETEDITSEL";
    case CB_LIMITTEXT: return "CB_LIMITTEXT";
    case CB_SETEDITSEL: return "CB_SETEDITSEL";
    case CB_ADDSTRING: return "CB_ADDSTRING";
    case CB_DELETESTRING: return "CB_DELETESTRING";
    case CB_DIR: return "CB_DIR";
    case CB_GETCOUNT: return "CB_GETCOUNT";
    case CB_GETCURSEL: return "CB_GETCURSEL";
    case CB_GETLBTEXT: return "CB_GETLBTEXT";
    case CB_GETLBTEXTLEN: return "CB_GETLBTEXTLEN";
    case CB_INSERTSTRING: return "CB_INSERTSTRING";
    case CB_RESETCONTENT: return "CB_RESETCONTENT";
    case CB_FINDSTRING: return "CB_FINDSTRING";
    case CB_SELECTSTRING: return "CB_SELECTSTRING";
    case CB_SETCURSEL: return "CB_SETCURSEL";
    case CB_SHOWDROPDOWN: return "CB_SHOWDROPDOWN";
    case CB_GETITEMDATA: return "CB_GETITEMDATA";
    case CB_SETITEMDATA: return "CB_SETITEMDATA";
    case CB_GETDROPPEDCONTROLRECT: return "CB_GETDROPPEDCONTROLRECT";
    case CB_SETITEMHEIGHT: return "CB_SETITEMHEIGHT";
    case CB_GETITEMHEIGHT: return "CB_GETITEMHEIGHT";
    case CB_SETEXTENDEDUI: return "CB_SETEXTENDEDUI";
    case CB_GETEXTENDEDUI: return "CB_GETEXTENDEDUI";
    case CB_GETDROPPEDSTATE: return "CB_GETDROPPEDSTATE";
    case CB_FINDSTRINGEXACT: return "CB_FINDSTRINGEXACT";
    case CB_SETLOCALE: return "CB_SETLOCALE";
    case CB_GETLOCALE: return "CB_GETLOCALE";
    case CB_GETTOPINDEX: return "CB_GETTOPINDEX";
    case CB_SETTOPINDEX: return "CB_SETTOPINDEX";
    case CB_GETHORIZONTALEXTENT: return "CB_GETHORIZONTALEXTENT";
    case CB_SETHORIZONTALEXTENT: return "CB_SETHORIZONTALEXTENT";
    case CB_GETDROPPEDWIDTH: return "CB_GETDROPPEDWIDTH";
    case CB_SETDROPPEDWIDTH: return "CB_SETDROPPEDWIDTH";
    case CB_INITSTORAGE: return "CB_INITSTORAGE";
  //case CB_MULTIPLEADDSTRING: return "CB_MULTIPLEADDSTRING";
    case CB_GETCOMBOBOXINFO: return "CB_GETCOMBOBOXINFO";
    case CB_MSGMAX: return "CB_MSGMAX";
  //case WM_MOUSEFIRST: return "WM_MOUSEFIRST"; // Alias for WM_MOUSEMOVE
    case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
    case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP: return "WM_LBUTTONUP";
    case WM_LBUTTONDBLCLK: return "WM_LBUTTONDBLCLK";
    case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP: return "WM_RBUTTONUP";
    case WM_RBUTTONDBLCLK: return "WM_RBUTTONDBLCLK";
    case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
    case WM_MBUTTONUP: return "WM_MBUTTONUP";
    case WM_MBUTTONDBLCLK: return "WM_MBUTTONDBLCLK";
    case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
    case WM_XBUTTONDOWN: return "WM_XBUTTONDOWN";
    case WM_XBUTTONUP: return "WM_XBUTTONUP";
    case WM_XBUTTONDBLCLK: return "WM_XBUTTONDBLCLK";
    case WM_MOUSEHWHEEL: return "WM_MOUSEHWHEEL";
  //case WM_MOUSELAST: return "WM_MOUSELAST"; // Alias for WM_MOUSEHWHEEL
    case WM_PARENTNOTIFY: return "WM_PARENTNOTIFY";
    case WM_ENTERMENULOOP: return "WM_ENTERMENULOOP";
    case WM_EXITMENULOOP: return "WM_EXITMENULOOP";
    case WM_NEXTMENU: return "WM_NEXTMENU";
    case WM_SIZING: return "WM_SIZING";
    case WM_CAPTURECHANGED: return "WM_CAPTURECHANGED";
    case WM_MOVING: return "WM_MOVING";
    case WM_POWERBROADCAST: return "WM_POWERBROADCAST";
    case WM_DEVICECHANGE: return "WM_DEVICECHANGE";
    case WM_MDICREATE: return "WM_MDICREATE";
    case WM_MDIDESTROY: return "WM_MDIDESTROY";
    case WM_MDIACTIVATE: return "WM_MDIACTIVATE";
    case WM_MDIRESTORE: return "WM_MDIRESTORE";
    case WM_MDINEXT: return "WM_MDINEXT";
    case WM_MDIMAXIMIZE: return "WM_MDIMAXIMIZE";
    case WM_MDITILE: return "WM_MDITILE";
    case WM_MDICASCADE: return "WM_MDICASCADE";
    case WM_MDIICONARRANGE: return "WM_MDIICONARRANGE";
    case WM_MDIGETACTIVE: return "WM_MDIGETACTIVE";
    case WM_MDISETMENU: return "WM_MDISETMENU";
    case WM_ENTERSIZEMOVE: return "WM_ENTERSIZEMOVE";
    case WM_EXITSIZEMOVE: return "WM_EXITSIZEMOVE";
    case WM_DROPFILES: return "WM_DROPFILES";
    case WM_MDIREFRESHMENU: return "WM_MDIREFRESHMENU";
  //case WM_IME_REPORT: return "WM_IME_REPORT";
    case WM_IME_SETCONTEXT: return "WM_IME_SETCONTEXT";
    case WM_IME_NOTIFY: return "WM_IME_NOTIFY";
    case WM_IME_CONTROL: return "WM_IME_CONTROL";
    case WM_IME_COMPOSITIONFULL: return "WM_IME_COMPOSITIONFULL";
    case WM_IME_SELECT: return "WM_IME_SELECT";
    case WM_IME_CHAR: return "WM_IME_CHAR";
    case WM_IME_REQUEST: return "WM_IME_REQUEST";
  // case WM_IMEKEYDOWN: return "WM_IMEKEYDOWN";
    case WM_IME_KEYDOWN: return "WM_IME_KEYDOWN";
  //case WM_IMEKEYUP: return "WM_IMEKEYUP";
    case WM_IME_KEYUP: return "WM_IME_KEYUP";
    case WM_NCMOUSEHOVER: return "WM_NCMOUSEHOVER";
    case WM_MOUSEHOVER: return "WM_MOUSEHOVER";
    case WM_NCMOUSELEAVE: return "WM_NCMOUSELEAVE";
    case WM_MOUSELEAVE: return "WM_MOUSELEAVE";
    case WM_DPICHANGED: return "WM_DPICHANGED";
    case WM_DPICHANGED_BEFOREPARENT: return "WM_DPICHANGED_BEFOREPARENT";
    case WM_DPICHANGED_AFTERPARENT: return "WM_DPICHANGED_AFTERPARENT";
    case WM_GETDPISCALEDSIZE: return "WM_GETDPISCALEDSIZE";
    case WM_CUT: return "WM_CUT";
    case WM_COPY: return "WM_COPY";
    case WM_PASTE: return "WM_PASTE";
    case WM_CLEAR: return "WM_CLEAR";
    case WM_UNDO: return "WM_UNDO";
    case WM_RENDERFORMAT: return "WM_RENDERFORMAT";
    case WM_RENDERALLFORMATS: return "WM_RENDERALLFORMATS";
    case WM_DESTROYCLIPBOARD: return "WM_DESTROYCLIPBOARD";
    case WM_DRAWCLIPBOARD: return "WM_DRAWCLIPBOARD";
    case WM_PAINTCLIPBOARD: return "WM_PAINTCLIPBOARD";
    case WM_VSCROLLCLIPBOARD: return "WM_VSCROLLCLIPBOARD";
    case WM_SIZECLIPBOARD: return "WM_SIZECLIPBOARD";
    case WM_ASKCBFORMATNAME: return "WM_ASKCBFORMATNAME";
    case WM_CHANGECBCHAIN: return "WM_CHANGECBCHAIN";
    case WM_HSCROLLCLIPBOARD: return "WM_HSCROLLCLIPBOARD";
    case WM_QUERYNEWPALETTE: return "WM_QUERYNEWPALETTE";
    case WM_PALETTEISCHANGING: return "WM_PALETTEISCHANGING";
    case WM_PALETTECHANGED: return "WM_PALETTECHANGED";
    case WM_HOTKEY: return "WM_HOTKEY";
    case WM_PRINT: return "WM_PRINT";
    case WM_PRINTCLIENT: return "WM_PRINTCLIENT";
    case WM_APPCOMMAND: return "WM_APPCOMMAND";
    case WM_HANDHELDFIRST: return "WM_HANDHELDFIRST";
    case WM_HANDHELDLAST: return "WM_HANDHELDLAST";
    case WM_AFXFIRST: return "WM_AFXFIRST";
    case WM_AFXLAST: return "WM_AFXLAST";
    case WM_PENWINFIRST: return "WM_PENWINFIRST";
#if 0
    case WM_RCRESULT: return "WM_RCRESULT";
    case WM_HOOKRCRESULT: return "WM_HOOKRCRESULT";
    case WM_GLOBALRCCHANGE: return "WM_GLOBALRCCHANGE";
    case WM_PENMISCINFO: return "WM_PENMISCINFO";
    case WM_SKB: return "WM_SKB";
    case WM_HEDITCTL: return "WM_HEDITCTL";
    case WM_PENCTL: return "WM_PENCTL";
    case WM_PENMISC: return "WM_PENMISC";
    case WM_CTLINIT: return "WM_CTLINIT";
    case WM_PENEVENT: return "WM_PENEVENT";
#endif
    case WM_PENWINLAST: return "WM_PENWINLAST";
  // Some remaining reserved undefined system messages...
  // And then the end of the 0-1023 system messages (0 - (WM_USER-1)).

  //       0 - (WM_USER – 1)  Messages reserved for use by the system.
  // WM_USER -  0x7FFF        Integer messages for use by private window classes.
  //  WM_APP -  0xBFFF        Messages available for use by applications.
  //  0xC000 -  0xFFFF        String messages for use by applications.
  //          > 0xFFFF        Reserved by the system.
  // 
  // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-app
    default:
    {
      if      (      0    <= msg && msg <= (WM_USER-1))
        return "SYSTEM UNKNOWN";

      else if ( WM_USER   <= msg && msg <= 0x7FFF)
        return std::format ("WM_USER+0x{:x}", (msg - WM_USER));

      else if ( WM_APP    <= msg && msg <= 0xBFFF)
        return std::format ( "WM_APP+0x{:x}", (msg - WM_APP));

      // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerwindowmessagea
      else if ( 0xC000    <= msg && msg <= 0xFFFF)
      {
        if      (msg == SHELL_TASKBAR_RESTART)
          return "TaskbarCreated";
        else if (msg == SHELL_TASKBAR_BUTTON_CREATED)
          return "TaskbarButtonCreated";
        else
          return "APP WINDOW MESSAGE";
      }

      else
        return "SYSTEM RESERVED";
    }
  }

  return "UNKNOWN";
}

#include <SKIF_utility.h>
#include <sk_utility/utility.h>
#include <comdef.h>
#include <gsl/gsl_util>
#include <Psapi.h>
#include <cwctype>
#include <unordered_set>
#include <ShlObj.h>
#include <strsafe.h>
#include <filesystem>


// Generic Utilities

std::wstring
SKIF_Util_TowLower     (std::wstring_view input)
{
  std::wstring copy = std::wstring(input);
  std::transform (copy.begin(), copy.end(), copy.begin(), [](wchar_t c) { return std::towlower(c); });
  return copy;
}

std::wstring
SKIF_Util_GetLastError (void)
{
  LPWSTR messageBuffer = nullptr;

  size_t size = FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, GetLastError ( ), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);

  std::wstring message (messageBuffer, size);
  LocalFree (messageBuffer);

  return message;
}

DWORD
SKIF_Util_timeGetTime (void)
{
  static LARGE_INTEGER qpcFreq = { };
         LARGE_INTEGER li      = { };

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
  //return
    //ShellExecuteW ( nullptr, L"EXPLORE",
      //path.data (), nullptr,
                    //nullptr, SW_SHOWNORMAL );

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
SKIF_Util_ExplorePath_Formatted (
  const wchar_t * const wszFmt,
                        ... )
{  static        thread_local
        wchar_t _thread_localPath
        [ 4UL * INTERNET_MAX_PATH_LENGTH ] =
        {                                };
  va_list       vArgs   =   nullptr;
  va_start    ( vArgs,  wszFmt    );
  wvnsprintfW ( _thread_localPath,
    ARRAYSIZE ( _thread_localPath ),
                        wszFmt,
                vArgs             );
  va_end      ( vArgs             );

  //return
    //ShellExecuteW (
      //nullptr, L"EXPLORE",
                //_thread_localPath,
      //nullptr,    nullptr,
             //SW_SHOWNORMAL
                  //);

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"EXPLORE";
    sexi.lpFile       = _thread_localPath;
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
               DWORD       dwAction,
               LPCWSTR     verb,
               LPCWSTR     parameters)
{
  //return
    //ShellExecuteW ( nullptr, L"OPEN",
      //path.data (), nullptr,
                    //nullptr, dwAction );

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = verb;
    sexi.lpFile       = path.data ();
    sexi.lpParameters = parameters;
    sexi.nShow        = dwAction;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

HINSTANCE
SKIF_Util_OpenURI_Formatted (
          DWORD       dwAction,
  const wchar_t * const wszFmt,
                        ... )
{  static        thread_local
        wchar_t _thread_localPath
        [ 4UL * INTERNET_MAX_PATH_LENGTH ] =
        {                                };
  va_list       vArgs   =   nullptr;
  va_start    ( vArgs,  wszFmt    );
  wvnsprintfW ( _thread_localPath,
    ARRAYSIZE ( _thread_localPath ),
                        wszFmt,
                vArgs             );
  va_end      ( vArgs             );

  //return
    //ShellExecuteW ( nullptr,
      //L"OPEN",  _thread_localPath,
         //nullptr,   nullptr,   dwAction
                  //);

    SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = _thread_localPath;
    sexi.nShow        = dwAction;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

void
SKIF_Util_OpenURI_Threaded (
        const LPCWSTR path )
{
  _beginthreadex(nullptr,
                         0,
  [](LPVOID lpUser)->unsigned
  {
    LPCWSTR _path = (LPCWSTR)lpUser;

    CoInitializeEx (nullptr,
      COINIT_APARTMENTTHREADED |
      COINIT_DISABLE_OLE1DDE
    );

    SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = _path;
    sexi.nShow        = SW_SHOWNORMAL;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_NOASYNC    | SEE_MASK_NOZONECHECKS;

    ShellExecuteExW (&sexi);

    _endthreadex(0);

    return 0;
  }, (LPVOID)path, NULL, NULL);
}


// Windows

BOOL
WINAPI
SKIF_Util_CompactWorkingSet (void)
{
  return
    EmptyWorkingSet (
      GetCurrentProcess ()
    );
}

BOOL
SKIF_Util_IsWindows8Point1OrGreater (void)
{
  SK_RunOnce (
    SetLastError (NO_ERROR)
  );

  static BOOL
    bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "GetSystemTimePreciseAsFileTime"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

BOOL
SKIF_Util_IsWindows10OrGreater (void)
{
  SK_RunOnce (
    SetLastError (NO_ERROR)
  );

  static BOOL
    bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "SetThreadDescription"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

BOOL
SKIF_Util_IsWindowsVersionOrGreater (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber)
{
  NTSTATUS(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEXW);

  OSVERSIONINFOEXW
    osInfo                     = { };
    osInfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

  *reinterpret_cast<FARPROC *>(&RtlGetVersion) =
    GetProcAddress (GetModuleHandleW (L"ntdll"), "RtlGetVersion");

  if (RtlGetVersion != nullptr)
  {
    if (NT_SUCCESS (RtlGetVersion (&osInfo)))
    {
      return
        ( osInfo.dwMajorVersion   >  dwMajorVersion ||
          ( osInfo.dwMajorVersion == dwMajorVersion &&
            osInfo.dwMinorVersion >= dwMinorVersion &&
            osInfo.dwBuildNumber  >= dwBuildNumber  )
        );
    }
  }

  return FALSE;
}


// Web

DWORD
WINAPI
SKIF_Util_GetWebUri (skif_get_web_uri_t* get)
{
  ULONG ulTimeout = 5000UL;

  PCWSTR rgpszAcceptTypes [] = { L"*/*", nullptr };
  HINTERNET hInetHTTPGetReq  = nullptr,
            hInetHost        = nullptr,
  hInetRoot                  =
    InternetOpen (
      L"Special K - Asset Crawler",
        INTERNET_OPEN_TYPE_DIRECT,
          nullptr, nullptr,
            0x00
    );

  // (Cleanup On Error)
  auto CLEANUP = [&](bool clean = false) ->
  DWORD
  {
    if (! clean)
    {
      DWORD dwLastError =
           GetLastError ();

      OutputDebugStringW (
        ( std::wstring (L"WinInet Failure (") +
              std::to_wstring (dwLastError)   +
          std::wstring (L"): ")               +
                 _com_error   (dwLastError).ErrorMessage ()
        ).c_str ()
      );
    }

    if (hInetHTTPGetReq != nullptr) InternetCloseHandle (hInetHTTPGetReq);
    if (hInetHost       != nullptr) InternetCloseHandle (hInetHost);
    if (hInetRoot       != nullptr) InternetCloseHandle (hInetRoot);

    skif_get_web_uri_t*     to_delete = nullptr;
    std::swap   (get,   to_delete);
    delete              to_delete;

    return 0;
  };

  if (hInetRoot == nullptr)
    return CLEANUP ();

  DWORD_PTR dwInetCtx = 0;

  hInetHost =
    InternetConnect ( hInetRoot,
                        get->wszHostName,
                          INTERNET_DEFAULT_HTTP_PORT,
                            nullptr, nullptr,
                              INTERNET_SERVICE_HTTP,
                                0x00,
                                  (DWORD_PTR)&dwInetCtx );

  if (hInetHost == nullptr)
  {
    return CLEANUP ();
  }

  hInetHTTPGetReq =
    HttpOpenRequest ( hInetHost,
                        get->method,
                          get->wszHostPath,
                            L"HTTP/1.1",
                              nullptr,
                                rgpszAcceptTypes,
                                                                    INTERNET_FLAG_IGNORE_CERT_DATE_INVALID |
                                  INTERNET_FLAG_CACHE_IF_NET_FAIL | INTERNET_FLAG_IGNORE_CERT_CN_INVALID   |
                                  INTERNET_FLAG_RESYNCHRONIZE     | INTERNET_FLAG_CACHE_ASYNC,
                                    (DWORD_PTR)&dwInetCtx );

  // Wait 2500 msecs for a dead connection, then give up
  //
  InternetSetOptionW ( hInetHTTPGetReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                         &ulTimeout,    sizeof (ULONG) );

  if (hInetHTTPGetReq == nullptr)
  {
    return CLEANUP ();
  }

  if ( HttpSendRequestW ( hInetHTTPGetReq,
                            get->header.c_str(),
                              get->header.length(),
                                (LPVOID)get->body.c_str(),
                                  (DWORD)get->body.size() ) )
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

      FILE *fOut =
        _wfopen ( get->wszLocalPath, L"wb+" );

      if (fOut != nullptr)
      {
        fwrite (concat_buffer.data (), concat_buffer.size (), 1, fOut);
        fclose (fOut);
      }
    }
  }

  CLEANUP (true);

  return 1;
}

void
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

  if (!method.empty())
    get->method = method.c_str();

  if (!header.empty())
    get->header = header.c_str();

  if (!body.empty())
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

    SKIF_Util_GetWebUri (get);
  }
}

void
SKIF_Util_GetWebResourceThreaded (std::wstring url, std::wstring_view destination)
{
  auto* get =
    new skif_get_web_uri_t { };

  URL_COMPONENTSW urlcomps = { };

  urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

  urlcomps.lpszHostName     = get->wszHostName;
  urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

  urlcomps.lpszUrlPath      = get->wszHostPath;
  urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

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

    _beginthreadex (
       nullptr, 0, (_beginthreadex_proc_type) SKIF_Util_GetWebUri,
           get, 0x0, nullptr
                   );
  }
}


// Directory Watch

bool
SKIF_DirectoryWatch::isSignaled (std::wstring_view path)
{
  bool bRet = false;

  if (hChangeNotification != INVALID_HANDLE_VALUE)
  {
    bRet =
      ( WAIT_OBJECT_0 ==
          WaitForSingleObject (hChangeNotification, 0) );

    if (bRet)
    {
      FindNextChangeNotification (
        hChangeNotification
      );
    }
  }
  else {
    if (! path.empty())
    {
      hChangeNotification =
        FindFirstChangeNotificationW (
          std::wstring(path).c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME
        );

      if (hChangeNotification != INVALID_HANDLE_VALUE)
      {
        FindNextChangeNotification (
          hChangeNotification
        );
      }
    }
  }

  return bRet;
}

SKIF_DirectoryWatch::~SKIF_DirectoryWatch (void)
{
  if (      hChangeNotification != INVALID_HANDLE_VALUE)
    FindCloseChangeNotification (hChangeNotification);
}


// Registry Watch

SKIF_RegistryWatch::SKIF_RegistryWatch ( HKEY hRootKey, const wchar_t* wszSubKey, const wchar_t* wszEventName, BOOL bWatchSubtree, DWORD dwNotifyFilter )
{
  _init.root          = hRootKey;
  _init.sub_key       = wszSubKey;
  _init.watch_subtree = bWatchSubtree;
  _init.filter_mask   = dwNotifyFilter;

  hEvent.m_h =
      CreateEvent ( nullptr, TRUE,
                            FALSE, wszEventName );

  reset ();
}

void
SKIF_RegistryWatch::registerNotify (void)
{
  hKeyBase.NotifyChangeKeyValue (
    _init.watch_subtree,
      _init.filter_mask,
        hEvent.m_h
  );
}

void
SKIF_RegistryWatch::reset (void)
{
  hKeyBase.Close ();

  if ((intptr_t)hEvent.m_h > 0)
    ResetEvent (hEvent.m_h);

  LSTATUS lStat =
    hKeyBase.Open ( _init.root,
                    _init.sub_key.c_str () );

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
      hEvent.m_h, 0UL, FALSE
    ) == WAIT_OBJECT_0;

  if (signaled)
  {
    reset ();
  }

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
// hwnd         - A handle to the parent window. The Shell uses this window to
//                display a dialog box if it needs to prompt the user for more
//                information while resolving the link.
// lpszLinkFile - Address of a buffer that contains the path of the link,
//                including the file name.
// lpszPath     - Address of a buffer that receives the path of the link target,
//                including the file name.
// lpszDesc     - Address of a buffer that receives the description of the
//                Shell link, stored in the Comment field of the link
//                properties.

void
SKIF_Util_ResolveShortcut (HWND hwnd, LPCSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize)
{
  IShellLink* psl;

  ///WCHAR szGotPath[MAX_PATH];
  WCHAR szArguments[MAX_PATH];
  WCHAR szTarget  [MAX_PATH];
  //WIN32_FIND_DATA wfd;

  *lpszTarget    = 0; // Assume failure
  *lpszArguments = 0; // Assume failure

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface.
  if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
  {
    IPersistFile* ppf;

    // Get a pointer to the IPersistFile interface.
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)))
    {
      WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode.
      MultiByteToWideChar(CP_ACP, 0, lpszLinkFile, -1, wsz, MAX_PATH);

      // Add code here to check return value from MultiByteWideChar
      // for success.

      // Load the shortcut.
      if (SUCCEEDED(ppf->Load(wsz, STGM_READ)))
      {
        // Disables the UI and hopefully sets a timeout duration of 10ms,
        //   since we don't actually care all that much about resolving the target.
        DWORD flags = MAKELONG(SLR_NO_UI, 10);

        // Resolve the link.
        if (SUCCEEDED(psl->Resolve(hwnd, flags)))
        {
          // Get the link target.
          if (SUCCEEDED(psl->GetPath(szTarget, MAX_PATH, NULL, SLGP_RAWPATH)))
            StringCbCopy(lpszTarget, iPathBufferSize, szTarget);

          // Get the arguments of the target.
          if (SUCCEEDED(psl->GetArguments(szArguments, MAX_PATH)))
            StringCbCopy(lpszArguments, iPathBufferSize, szArguments);
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
// Returns the result of calling the member functions of the interfaces. 
//
// Parameters:
// lpszPathObj  - Address of a buffer that contains the path of the object,
//                including the file name.
// lpszPathLink - Address of a buffer that contains the path where the 
//                Shell link is to be stored, including the file name.
// lpszDesc     - Address of a buffer that contains a description of the 
//                Shell link, stored in the Comment field of the link
//                properties.

bool
SKIF_Util_CreateShortcut (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs, LPCWSTR lpszWorkDir, LPCWSTR lpszDesc, LPCWSTR lpszIconLocation, int iIcon)
{
  bool ret = false;
  IShellLink* psl;

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
  // has already been called.

  if (SUCCEEDED (CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
  {
    IPersistFile* ppf;

    // Set the specifics of the shortcut. 
    psl->SetPath               (lpszTarget);

    if (wcscmp(lpszWorkDir, L"\0") == 0) // lpszWorkDir == L"\0"
      psl->SetWorkingDirectory (std::filesystem::path(lpszTarget).parent_path().c_str());
    else
      psl->SetWorkingDirectory (lpszWorkDir);

    if (wcscmp(lpszArgs, L"\0") != 0) // lpszArgs != L"\0"
      psl->SetArguments          (lpszArgs);

    if (wcscmp(lpszDesc, L"\0") != 0) // lpszDesc != L"\0"
      psl->SetDescription      (lpszDesc);

    if (wcscmp(lpszIconLocation, L"\0") != 0) // (lpszIconLocation != L"\0")
      psl->SetIconLocation     (lpszIconLocation, iIcon);

    // Query IShellLink for the IPersistFile interface, used for saving the 
    // shortcut in persistent storage. 
    //hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)))
    {

      //WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode. 
      //MultiByteToWideChar (CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);

      // Save the link by calling IPersistFile::Save. 
      if (SUCCEEDED (ppf->Save(lpszPathLink, FALSE)))
        ret = true;

      ppf->Release();
    }
    psl->Release();
  }

  return ret;
}
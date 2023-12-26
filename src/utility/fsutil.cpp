//
// Copyright 2020 Andon "Kaldaien" Coleman
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

#include <utility/sk_utility.h>

class SK_AutoCOMInit
{
public:
  SK_AutoCOMInit (DWORD dwCoInit = COINIT_MULTITHREADED) :
           init_flags_ (dwCoInit)
  {
    //if (_assert_not_dllmain ())
    {
      const HRESULT hr =
        CoInitializeEx (nullptr, init_flags_);

      if (SUCCEEDED (hr))
        success_ = true;
      else
        init_flags_ = ~init_flags_;
    }
  }

  ~SK_AutoCOMInit (void) noexcept
  {
    if (success_)
      CoUninitialize ();
  }

  bool  isInit       (void) noexcept { return success_;    }
  DWORD getInitFlags (void) noexcept { return init_flags_; }

protected:
  //static bool _assert_not_dllmain (void);

private:
  DWORD init_flags_ = COINIT_MULTITHREADED;
  bool  success_    = false;
};

bool
SK_FileHasSpaces (const wchar_t* wszLongFileName)
{
  return
    StrStrIW (wszLongFileName, L" ") != nullptr;
}

bool
SK_FileHas8Dot3Name (const wchar_t* wszLongFileName)
{
  wchar_t wszShortPath [MAX_PATH + 2] = { };

  if ((! GetShortPathName   (wszLongFileName, wszShortPath, 1)) ||
         GetFileAttributesW (wszShortPath) == INVALID_FILE_ATTRIBUTES   ||
         StrStrIW           (wszLongFileName, L" "))
  {
    return false;
  }

  return true;
}

HRESULT
ModifyPrivilege (
  IN LPCTSTR szPrivilege,
  IN BOOL    fEnable )
{
  TOKEN_PRIVILEGES NewState = { };
  LUID             luid     = { };
  HRESULT          hr       = S_OK;
  HANDLE           hToken   = nullptr;

  // Open the process token for this process.
  if (! OpenProcessToken ( GetCurrentProcess (),
                             TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                               &hToken ))
  {
      return ERROR_FUNCTION_FAILED;
  }

  // Get the local unique ID for the privilege.
  if (! LookupPrivilegeValue ( nullptr,
                                 szPrivilege,
                                   &luid ) )
  {
    CloseHandle (hToken);

    return ERROR_FUNCTION_FAILED;
  }

  // Assign values to the TOKEN_PRIVILEGE structure.
  NewState.PrivilegeCount            = 1;
  NewState.Privileges [0].Luid       = luid;
  NewState.Privileges [0].Attributes =
            (fEnable ? SE_PRIVILEGE_ENABLED : 0);

  // Adjust the token privilege.
  if (! AdjustTokenPrivileges ( hToken,
                                FALSE,
                                &NewState,
                                0,
                                nullptr,
                                nullptr )
     )
  {
    hr = ERROR_FUNCTION_FAILED;
  }

  // Close the handle.
  CloseHandle (hToken);

  return hr;
}

bool
SK_Generate8Dot3 (const wchar_t* wszLongFileName)
{
  CHeapPtr <wchar_t> wszFileName  (_wcsdup (wszLongFileName));
  CHeapPtr <wchar_t> wszFileName1 (_wcsdup (wszLongFileName));

  wchar_t  wsz8     [11] = { }; // One non-nul for overflow
  wchar_t  wszDot3  [ 4] = { };
  wchar_t  wsz8Dot3 [14] = { };

  while (SK_FileHasSpaces (wszFileName))
  {
    ModifyPrivilege (SE_RESTORE_NAME, TRUE);
    ModifyPrivilege (SE_BACKUP_NAME,  TRUE);

    CHandle hFile (
      CreateFileW ( wszFileName,
                      GENERIC_WRITE      | DELETE,
                        FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr,
                            OPEN_EXISTING,
                              GetFileAttributes (wszFileName) |
                              FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr ) );

    if (hFile == INVALID_HANDLE_VALUE)
    {
      return false;
    }

    DWORD dwAttrib =
      GetFileAttributes (wszFileName);

    if (dwAttrib == INVALID_FILE_ATTRIBUTES)
    {
      return false;
    }

    bool dir = false;

    if (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)
    {
      dir = true;
    }

    else
    {
      dir = false;

      const wchar_t* pwszExt =
        PathFindExtension (wszFileName);

      if ( pwszExt != nullptr &&
          *pwszExt == L'.' )
      {
        _swprintf ( wszDot3,
                     L"%3s",
                       CharNextW (pwszExt) );
      }

      PathRemoveExtension (wszFileName);
    }

    PathStripPath       (wszFileName);
    PathRemoveBackslash (wszFileName);
    PathRemoveBlanks    (wszFileName);

    wcsncpy (wsz8, wszFileName, 10);
             wsz8              [10] = L'\0';

    wchar_t idx = 0;

    if (wcslen (wsz8) > 8)
    {
      wsz8 [6] = L'~';
      wsz8 [7] = L'0';
      wsz8 [8] = L'\0';

      _swprintf (wsz8Dot3, dir ? L"%s" : L"%s.%s", wsz8, wszDot3);

      while ((! SetFileShortNameW (hFile, wsz8Dot3)) && idx < 9)
      {
        wsz8 [6] = L'~';
        wsz8 [7] = L'0' + idx++;
        wsz8 [8] = L'\0';

        _swprintf (wsz8Dot3, dir ? L"%s" : L"%s.%s", wsz8, wszDot3);
      }
    }

    else
    {
      _swprintf (wsz8Dot3, dir ? L"%s" : L"%s.%s", wsz8, wszDot3);
    }

    if (idx == 9)
    {
      return false;
    }

    PathRemoveFileSpec  (wszFileName1);
    wcscpy (wszFileName, wszFileName1);
  }

  return true;
}


// Alternate name? ( SK_Time_UnixEpochToWin32File )
FILETIME
SK_Win32_time_t_to_FILETIME (time_t epoch)
{
  LONGLONG lt =
    Int32x32To64 (epoch, 10000000) +
                         116444736000000000;

  return FILETIME {
    (DWORD) lt,
    (DWORD)(lt >> 32)
  };
}



#include <utility/fsutil.h>
#include <utility/utility.h>

HRESULT
SK_Shell32_GetKnownFolderPath ( _In_ REFKNOWNFOLDERID rfid,
                                     std::wstring&     dir,
                            volatile LONG*             _RunOnce )
{
  // Use the current directory if COM or permissions are mucking things up
  auto _FailFastAndDie =
  [&] (void)->HRESULT
  {
    wchar_t wszCurrentDir [MAX_PATH + 2] = { };
    GetCurrentDirectoryW  (MAX_PATH, wszCurrentDir);

    dir = wszCurrentDir;

    InterlockedIncrementRelease (_RunOnce);

    return E_ACCESSDENIED;
  };

  auto _TrySHGetKnownFolderPath =
    [&](HANDLE hToken, wchar_t** ppStr)->HRESULT
  {
    HRESULT try_result =
      SHGetKnownFolderPath (rfid, 0, hToken, ppStr);

    if (SUCCEEDED (try_result))
    {
      dir = *ppStr;

      CoTaskMemFree (*ppStr);

      InterlockedIncrementRelease (_RunOnce);
    }

    return try_result;
  };


  HRESULT hr =
    S_OK;

  if (! InterlockedCompareExchangeAcquire (_RunOnce, 1, 0))
  {
    CHandle  hToken (INVALID_HANDLE_VALUE);
    wchar_t* str    = nullptr;

    if (! OpenProcessToken (
            GetCurrentProcess (), TOKEN_QUERY | TOKEN_IMPERSONATE |
                                  TOKEN_READ, &hToken.m_h )
       )
    {
      return
        _FailFastAndDie ();
    }

    hr =
      _TrySHGetKnownFolderPath (hToken.m_h, &str);

    // Second chance
    if (FAILED (hr))
    {
      SK_AutoCOMInit _com_base;

      hr =
        _TrySHGetKnownFolderPath (hToken.m_h, &str);
    }

    //We're @#$%'d
    if (FAILED (hr))
      return _FailFastAndDie ();
  }

  return hr;
}

SKIF_CommonPathsCache::SKIF_CommonPathsCache (void)
{
  // Cache user profile locations
  SKIF_GetFolderPath ( &my_documents       );
  SKIF_GetFolderPath ( &app_data_local     );
  SKIF_GetFolderPath ( &app_data_local_low );
  SKIF_GetFolderPath ( &app_data_roaming   );
  SKIF_GetFolderPath ( &win_saved_games    );
  SKIF_GetFolderPath ( &desktop            );

  // Launching SKIF through the Win10 start menu can at times default the working directory to system32.
  // Store the original working directory in a variable, since it's used by custom launch, for example.
  GetCurrentDirectoryW (MAX_PATH, skif_workdir_org);

  // Store the full path to SKIF's executable
  GetModuleFileNameW  (nullptr, skif_executable, MAX_PATH);

  // Store the full path to the folder SKIF.exe is running from
  wcsncpy_s (specialk_install,  MAX_PATH,
              skif_executable, _TRUNCATE );
  PathRemoveFileSpecW (specialk_install); // Strip SKIF.exe from the path

  // Cache the Steam install folder
  extern const wchar_t* SK_GetSteamDir (void);
  wcsncpy_s ( steam_install,       MAX_PATH,
              SK_GetSteamDir ( ), _TRUNCATE );
}

void
SKIF_GetFolderPath (SKIF_CommonPathsCache::win_path_s* path)
{
  std::wstring dir;

  if (
    SUCCEEDED (
      SK_Shell32_GetKnownFolderPath (
        path->folderid, dir, &path->__init
      )
    )
  )
  {
    wcsncpy_s ( path->path,   MAX_PATH,
                dir.c_str (), _TRUNCATE
    );
  }

  else
  {
    ExpandEnvironmentStringsW (
      path->legacy_env_var, path->path, MAX_PATH
    );
  }

  PathAddBackslashW (path->path);
};

std::wstring
SK_GetFontsDir (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (*_path_cache.fonts.path == L'\0')
    SKIF_GetFolderPath ( &_path_cache.fonts );

  return
    _path_cache.fonts.path;
}

bool
SK_FileOpenDialog (LPWSTR *pszPath, const COMDLG_FILTERSPEC fileTypes, UINT cFileTypes, FILEOPENDIALOGOPTIONS dialogOptions, const GUID defaultFolder)
{
  bool success = false;
  IFileOpenDialog  *pFileOpen = nullptr;

  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen))))
  {
    IShellItem* psiDefaultFolder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderItem(defaultFolder, KF_FLAG_DEFAULT, NULL, IID_IShellItem, (void**)&psiDefaultFolder)))
    {
      pFileOpen->SetDefaultFolder(psiDefaultFolder);
      psiDefaultFolder->Release();
    }
    pFileOpen->SetFileTypes (cFileTypes, &fileTypes);
    pFileOpen->SetOptions   (dialogOptions);

    if (SUCCEEDED(pFileOpen->Show(NULL)))
    {
      IShellItem *pItem = nullptr;

      if (SUCCEEDED(pFileOpen->GetResult(&pItem)))
      {
        HRESULT hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, pszPath); // SIGDN_URL
        if (SUCCEEDED(hr))
          success = true;
        else
          PLOG_ERROR << SKIF_Util_GetErrorAsWStr (HRESULT_CODE(hr));

        pItem->Release();
      }
    }
    pFileOpen->Release();
  }

  return success;
}
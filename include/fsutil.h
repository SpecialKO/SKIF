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

#pragma once

#include <atlbase.h>
#include <ShlObj.h>
#include <string>

bool
SK_FileHasSpaces    (const wchar_t *wszLongFileName);
BOOL
SK_FileHas8Dot3Name (const wchar_t *wszLongFileName);

bool
SK_Generate8Dot3    (const wchar_t *wszLongFileName);

HRESULT
ModifyPrivilege (
  IN LPCTSTR szPrivilege,
  IN BOOL    fEnable
);

// Cache of paths that do not change between AppIDs
struct path_cache_s {
  struct win_path_s {
    KNOWNFOLDERID   folderid        = { };
    const wchar_t*  legacy_env_var  = L"";
    wchar_t         path [MAX_PATH] = { };
    volatile LONG __init            =  0;
  };

  win_path_s my_documents       =
  {            FOLDERID_Documents,
    L"%USERPROFILE%\\My Documents"
  };
  win_path_s app_data_local     =
  {                  FOLDERID_LocalAppData,
    L"%USERPROFILE%\\AppData\\Local"
  };
  win_path_s app_data_local_low =
  {           FOLDERID_LocalAppDataLow,
    L"%USERPROFILE%\\AppData\\LocalLow"
  };
  win_path_s app_data_roaming   =
  {                  FOLDERID_RoamingAppData,
    L"%USERPROFILE%\\AppData\\Roaming"
  };
  win_path_s win_saved_games    =
  {          FOLDERID_SavedGames,
    L"%USERPROFILE%\\Saved Games"
  };
  win_path_s fonts              =
  {    FOLDERID_Fonts,
    L"%WINDIR%\\Fonts"
  };
  win_path_s specialk_userdata  =
  {            FOLDERID_Documents,
    L"%USERPROFILE%\\My Documents"
  };
  wchar_t steam_install [MAX_PATH] = { };
} extern path_cache;

HRESULT
SK_Shell32_GetKnownFolderPath ( _In_ REFKNOWNFOLDERID rfid,
                                     std::wstring&     dir,
                            volatile LONG*             _RunOnce );

std::wstring
SK_GetFontsDir (void);

void
SKIF_GetFolderPath (path_cache_s::win_path_s* path);

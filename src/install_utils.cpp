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

#include <SKIF.h>
#include <install_utils.h>
#include <fsutil.h>
#include <stores/Steam/apps_list.h>
#include <stores/Steam/steam_library.h>

using VerQueryValueW_pfn        = BOOL (APIENTRY *)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
using GetFileVersionInfoExW_pfn = BOOL (APIENTRY *)(DWORD,LPCWSTR,DWORD,DWORD,LPVOID);

std::wstring
SKIF_GetSpecialKDLLVersion (const wchar_t* wszName)
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
SKIF_GetFileVersion (const wchar_t* wszName)
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
SKIF_GetProductName (const wchar_t* wszName)
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


sk_install_state_s
SKIF_InstallUtils_GetInjectionStrategy (uint32_t appid)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  sk_install_state_s
     install_state;
     install_state = {
        .injection = { .bitness  = InjectionBitness::Unknown,
                       .entry_pt = InjectionPoint::CBTHook,
                       .type     = InjectionType::Global,
                     },
           .config = { .type = ConfigType::Centralized,
                       .file = L"SpecialK.ini",
                     }
                     };

  app_record_s    *pApp     =    nullptr;
  skValveDataFile::appinfo_s
                  *pAppInfo =
    appinfo->getAppInfo ( appid );

  UNREFERENCED_PARAMETER (pAppInfo);

  install_state.injection.bitness =
        InjectionBitness::Unknown;

  for (auto& app : apps)
  {
    if (app.second.id != appid)
      continue;

    pApp = &app.second;

    for ( auto& launch_cfg : app.second.launch_configs )
    {
#if 0
      app_record_s::CPUType
                    cputype = app.second.
      common_config.cpu_type;

      if (cputype != app_record_s::CPUType::Any)
      {
        if (launch_cfg.second.cpu_type != app_record_s::CPUType::Common)
        {
          cputype =
            launch_cfg.second.cpu_type;
        }
      }

      else
      {
        // The any case will just be 64-bit for us, since SK only runs on
        //   64-bit systems. Thus, ignore 32-bit launch configs.
#ifdef _WIN64
        if (launch_cfg.second.cpu_type == app_record_s::CPUType::x86)
#else
        if (launch_cfg.second.cpu_type == app_record_s::CPUType::x64)
#endif
        {
          //OutputDebugStringW (launch_cfg.second.description.c_str ());
          continue;
        }

        else {
          cputype =
            launch_cfg.second.cpu_type;
        }
      }

      if (cputype == app_record_s::CPUType::x86)
        install_state.injection.bitness = InjectionBitness::ThirtyTwo;
      else if (cputype == app_record_s::CPUType::x64)
        install_state.injection.bitness = InjectionBitness::SixtyFour;
      else if (cputype == app_record_s::CPUType::Any)
      {
        std::wstring exec_path =
          launch_cfg.second.getExecutableFullPath (appid);

        DWORD dwBinaryType = MAXDWORD;
        if ( GetBinaryTypeW (exec_path.c_str (), &dwBinaryType) )
        {
          if (dwBinaryType == SCS_32BIT_BINARY)
            install_state.injection.bitness = InjectionBitness::ThirtyTwo;
          else if (dwBinaryType == SCS_64BIT_BINARY)
            install_state.injection.bitness = InjectionBitness::SixtyFour;
        }
      }
#endif

      std::wstring exec_path =
        launch_cfg.second.getExecutableFullPath (appid, false);

      DWORD dwBinaryType = MAXDWORD;
      if ( GetBinaryTypeW (exec_path.c_str (), &dwBinaryType) )
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          install_state.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          install_state.injection.bitness = InjectionBitness::SixtyFour;
      }

      std::wstring test_path =
        launch_cfg.second.getExecutableDir (pApp->id, false);
        //launch_cfg.second.working_dir; // Doesn't contain a full path

      struct {
        InjectionBitness bitness;
        InjectionPoint   entry_pt;
        std::wstring     name;
        std::wstring     path;
      } test_dlls [] = {
        { install_state.injection.bitness, InjectionPoint::D3D9,    L"d3d9",     L"" },
        { install_state.injection.bitness, InjectionPoint::DXGI,    L"dxgi",     L"" },
        { install_state.injection.bitness, InjectionPoint::D3D11,   L"d3d11",    L"" },
        { install_state.injection.bitness, InjectionPoint::OpenGL,  L"OpenGL32", L"" },
        { install_state.injection.bitness, InjectionPoint::DInput8, L"dinput8",  L"" }
      };

      for ( auto& dll : test_dlls )
      {
        dll.path =
          ( test_path + LR"(\)" ) +
            ( dll.name + L".dll" );

        if (PathFileExistsW (dll.path.c_str ()))
        {
          std::wstring dll_ver =
            SKIF_GetSpecialKDLLVersion (dll.path.c_str ());

          if (! dll_ver.empty ())
          {
            install_state.injection = {
              dll.bitness,
              dll.entry_pt, InjectionType::Local,
              dll.path,     dll_ver
            };

            if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
            {
              install_state.config.type =
                ConfigType::Centralized;
            }

            else
            {
              install_state.config = {
                ConfigType::Localized,
                test_path
              };
            }

            install_state.config.file =
              dll.name + L".ini";

            break;
          }
        }
      }

      // Naively assume the first launch config's always the one we want
      break;
    }
  }

  if ( InjectionType::Global ==
         install_state.injection.type )
  {
    // Assume 32-bit if we don't know otherwise
    bool bIs64Bit =
      ( install_state.injection.bitness ==
                       InjectionBitness::SixtyFour );

    //install_state.config.type =
    //  ConfigType::Centralized;

    wchar_t                 wszPathToSelf [MAX_PATH] = { };
    GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
    PathRemoveFileSpecW (   wszPathToSelf);
    PathAppendW         (   wszPathToSelf,
                              bIs64Bit ? L"SpecialK64.dll"
                                       : L"SpecialK32.dll" );
    install_state.injection.dll_path = wszPathToSelf;
    install_state.injection.dll_ver  =
    SKIF_GetSpecialKDLLVersion (       wszPathToSelf);
  }

  if (pApp != nullptr)
  {
    install_state.localized_name =
      SK_UseManifestToGetAppName (pApp->id);
  }

  else
    install_state.localized_name = "<executable_name_here>";

  if ( ConfigType::Centralized ==
         install_state.config.type )
  {
    std::wstring name =
      SK_UTF8ToWideChar (
        install_state.localized_name
      );

    name.erase ( std::remove_if ( name.begin (),
                                  name.end   (),

                              [](wchar_t tval)
                              {
                                static
                                const std::set <wchar_t>
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

    install_state.config.dir =
      SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                           _path_cache.specialk_userdata,
                             name.c_str () );
  }

  install_state.config.file =
    ( install_state.config.dir + LR"(\)" ) +
      install_state.config.file;

  return install_state;
}
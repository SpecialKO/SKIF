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
#include <utility/install_utils.h>
#include <utility/fsutil.h>
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


// Only used for Steam games!
void
SKIF_InstallUtils_GetInjectionStrategy (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Assume global
  pApp->specialk.injection.injection.type =
    InjectionType::Global;
  pApp->specialk.injection.injection.entry_pt =
    InjectionPoint::CBTHook;
  pApp->specialk.injection.config.type =
    ConfigType::Centralized;
  pApp->specialk.injection.config.file =
    L"SpecialK.ini";

  // Parse appinfo data for the current game
  skValveDataFile::appinfo_s
                  *pAppInfo =
    appinfo->getAppInfo ( pApp->id );

  UNREFERENCED_PARAMETER (pAppInfo);

  int firstValidFound = -1;

  // TODO: Go through all code and change pApp->launch_configs[0] to refer to whatever "preferred" launch config we've found...
  for ( auto& launch_cfg : pApp->launch_configs )
  {
    if (! launch_cfg.second.valid)
      continue;

    if (! launch_cfg.second.isExecutableFullPathValid ( ))
      continue;

    if (firstValidFound == -1)
      firstValidFound = launch_cfg.first;

    // Check bitness
    if (pApp->specialk.injection.injection.bitness == InjectionBitness::Unknown)
    {

#define TRUST_LAUNCH_CONFIG
#ifdef TRUST_LAUNCH_CONFIG
      app_record_s::CPUType
                    cputype = pApp->common_config.cpu_type;

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
        pApp->specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;
      else if (cputype == app_record_s::CPUType::x64)
        pApp->specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
      else if (cputype == app_record_s::CPUType::Any)
      {
        std::wstring exec_path =
          launch_cfg.second.getExecutableFullPath ( );

        DWORD dwBinaryType = MAXDWORD;
        if ( GetBinaryTypeW (exec_path.c_str (), &dwBinaryType) )
        {
          if (dwBinaryType == SCS_32BIT_BINARY)
            pApp->specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;
          else if (dwBinaryType == SCS_64BIT_BINARY)
            pApp->specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
        }
      }

#else

      std::wstring exec_path =
        launch_cfg.second.getExecutableFullPath ( );

      DWORD dwBinaryType = MAXDWORD;
      if ( GetBinaryTypeW (exec_path.c_str (), &dwBinaryType) )
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          install_state.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          install_state.injection.bitness = InjectionBitness::SixtyFour;
      }

#endif

    } // End checking bitness

    std::wstring test_path =
      launch_cfg.second.getExecutableDir ( );
      //launch_cfg.second.working_dir; // Doesn't contain a full path

    struct {
      InjectionPoint   entry_pt;
      std::wstring     name;
      std::wstring     path;
    } test_dlls [] = {
      { InjectionPoint::D3D9,    L"d3d9",     L"" },
      { InjectionPoint::DXGI,    L"dxgi",     L"" },
      { InjectionPoint::D3D11,   L"d3d11",    L"" },
      { InjectionPoint::OpenGL,  L"OpenGL32", L"" },
      { InjectionPoint::DInput8, L"dinput8",  L"" }
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
          pApp->specialk.injection.injection = {
            pApp->specialk.injection.injection.bitness,
            dll.entry_pt, InjectionType::Local,
            dll.path,     dll_ver
          };

          if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
          {
            pApp->specialk.injection.config.type =
              ConfigType::Centralized;
          }

          else
          {
            pApp->specialk.injection.config = {
              ConfigType::Localized,
              test_path
            };
          }

          pApp->specialk.injection.config.file =
            dll.name + L".ini";

          break;
        }
      }
    }

    // Naively assume the first valid launch config that we are pointed to is the primary one
    // If we're not at the first launch config, move it to the first position
    //break;
  }

  // Swap out the first element for the first valid one we found
  if (firstValidFound != -1)
  {
    app_record_s::launch_config_s copy    = pApp->launch_configs[0];
    pApp->launch_configs[0]               = pApp->launch_configs[firstValidFound];
    pApp->launch_configs[firstValidFound] = copy;
  }

  if ( InjectionType::Global ==
         pApp->specialk.injection.injection.type )
  {
    // Assume 32-bit if we don't know otherwise
    bool bIs64Bit =
      (pApp->specialk.injection.injection.bitness ==
                       InjectionBitness::SixtyFour );

    wchar_t                 wszPathToSelf [MAX_PATH] = { };
    GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
    PathRemoveFileSpecW (   wszPathToSelf);
    PathAppendW         (   wszPathToSelf,
                              bIs64Bit ? L"SpecialK64.dll"
                                       : L"SpecialK32.dll" );
    pApp->specialk.injection.injection.dll_path = wszPathToSelf;
    pApp->specialk.injection.injection.dll_ver  =
    SKIF_GetSpecialKDLLVersion (       wszPathToSelf);
  }

  if (pApp != nullptr)
  {
    pApp->specialk.injection.localized_name =
      SK_UseManifestToGetAppName (pApp);
  }

  else
    pApp->specialk.injection.localized_name = "<executable_name_here>";

  if ( ConfigType::Centralized ==
         pApp->specialk.injection.config.type )
  {
    std::wstring name =
      SK_UTF8ToWideChar (
        pApp->specialk.injection.localized_name
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

    pApp->specialk.injection.config.dir =
      SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                           _path_cache.specialk_userdata,
                             name.c_str () );
  }

  pApp->specialk.injection.config.file =
    (pApp->specialk.injection.config.dir + LR"(\)" ) +
     pApp->specialk.injection.config.file;
}
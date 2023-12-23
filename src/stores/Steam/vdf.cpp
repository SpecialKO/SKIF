//
// Copyright 2020-2021 Andon "Kaldaien" Coleman
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

#include <stores/Steam/vdf.h>
#include <utility/fsutil.h>
#include <regex>
#include <stores/Steam/steam_library.h>

const int SKIF_STEAM_APPID = 1157970;

// Shorthands, to make life less painful
using appinfo_s     = skValveDataFile::appinfo_s;
using app_section_s =                  appinfo_s::section_s;

uint32_t skValveDataFile::vdf_version = 0x27; // Default to Pre-December 2022

skValveDataFile::skValveDataFile (std::wstring source) : path (source)
{
  FILE *fData = nullptr;

  _wfopen_s (&fData, path.c_str (), L"rbS");

  if (fData != nullptr)
  {
#ifdef _WIN64
    _fseeki64 (fData, 0, SEEK_END);
#else
    fseek     (fData, 0, SEEK_END);
#endif
    size_t
    size =
#ifdef _WIN64
    _ftelli64 (fData);
#else
    ftell     (fData);
#endif
    rewind    (fData);

    _data.resize (size);

    fread  (_data.data (), size, 1, fData);
    fclose (                        fData);

    base =
      reinterpret_cast <
             header_s *> (_data.data ());

    vdf_version =
      ((uint8_t *)&base->version)[0];

    root =
      &base->head;
  }
}

void
app_section_s::parse (section_desc_s& desc)
{
  static
    std::map <_TokenOp,size_t>
                    operand_sizes =
    { { Int32, sizeof (int32_t) },
      { Int64, sizeof (int64_t) } };

  std::vector <
    section_data_s
  > raw_sections;

  static bool exception = false;

  if (! exception)
  {
    for ( uint8_t *cur = (uint8_t *)desc.blob             ;
                   cur < (uint8_t *)desc.blob + desc.size ;
                   cur++ )
    {
      auto op =
        (_TokenOp)(*cur);

      auto name =
        (char *)(cur + 1);

      if (op != SectionEnd)
      {
        // Skip past name declarations, except for </Section> because it has no name.
                cur++;
        while (*cur != '\0')
              ++cur;
      }

      if (op == SectionBegin)
      {
        if (! raw_sections.empty ())
              raw_sections.push_back ({ raw_sections.back ().name + std::string (".") +
                                        name, {  (void *)cur, 0 } });
        else
          raw_sections.push_back     ({ name, {  (void *)cur, 0 } });
      }

      else if (op == SectionEnd)
      {
        if (! raw_sections.empty ())
        {     raw_sections.back  ().desc.size =
            (uintptr_t)cur -
            (uintptr_t)raw_sections.back      ().desc.blob;
                  finished_sections.push_back (raw_sections.back ());
                        raw_sections.pop_back  ();
        }
      }

      else
      {
        ++cur;

        switch (op)
        {
          case String:
            if (! raw_sections.empty ())
            {     raw_sections.back ().keys.push_back (
                { name, { String, (void *)cur }}
              );
            } else { exception = true; }
            while (*cur != '\0')     ++cur;
            break;

          case Int32:
          case Int64:
            if (! raw_sections.empty ())
            {     raw_sections.back ().keys.push_back (
                { name, { op, (void *)cur }}
              );
            } else { exception = true; }
            cur += (operand_sizes [op]-1);
            break;

          default:
            //MessageBox (nullptr, std::to_wstring (op).c_str (), L"Unknown VDF Token Operator", MB_OK);
            PLOG_WARNING << "Unknown VDF Token Operator: " << op;
            exception = true;
            break;
        }
      }
    }
  }
}

void*
appinfo_s::getRootSection (size_t* pSize)
{
  size_t vdf_header_size =
    ( vdf_version > 0x27 ? sizeof (appinfo_s)
                         : sizeof (appinfo27_s) );

  auto _CheckVersion =
    [&](void) -> void
  {
    switch (vdf_version)
    {
      case 0x28: // v40
        PLOG_INFO    << "Steam appinfo.vdf version: " << vdf_version << " (December 2022)";
        break;
      case 0x27: // v39
        PLOG_INFO    << "Steam appinfo.vdf version: " << vdf_version << " (pre-December 2022)";
        break;
      default:
        PLOG_WARNING << "Steam appinfo.vdf version: " << vdf_version << " (unknown/unsupported)";
        MessageBox ( nullptr, std::to_wstring (vdf_version).c_str (),
                       L"Unsupported VDF Version", MB_OK );
    }
  };

  SK_RunOnce(_CheckVersion());

  size_t kv_size =
    (size - vdf_header_size + 8);

  if (pSize != nullptr)
     *pSize  = kv_size;

  return
    (uint8_t*)&appid + vdf_header_size;
}

appinfo_s*
appinfo_s::getNextApp (void)
{
  section_desc_s root_sec{};

  root_sec.blob =
    getRootSection (&root_sec.size);

  auto *pNext =
    (appinfo_s *)(
      (uint8_t *)root_sec.blob +
                 root_sec.size);

  return
    ( pNext->appid == _LastSteamApp ) ?
                              nullptr : pNext;
}

appinfo_s*
skValveDataFile::getAppInfo ( uint32_t     appid )
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  extern bool SKIF_STEAM_OWNER;

  // Skip call if it concerns someone whom does not have SKIF installed on Steam
  if ( appid == SKIF_STEAM_APPID &&
             (! SKIF_STEAM_OWNER) )
    return nullptr;

  if (root != nullptr)
  {
    skValveDataFile::appinfo_s *pIter = root;

    while (pIter != nullptr && pIter->appid != _LastSteamApp)
    {
      if (pIter->appid == appid)
      {
        app_record_s* pAppRecord = nullptr;

        for (auto& app : g_apps)
        {
          if (app.second.id == appid && app.second.store == app_record_s::Store::Steam)
          {
            pAppRecord = &app.second;
            break;
          }
        }

        if (! pAppRecord)
        {
          static app_record_s _non_steam (appid);
          pAppRecord = &_non_steam;
        }

        static DWORD dwSingleThread  = GetCurrentThreadId ();
        assert      (dwSingleThread == GetCurrentThreadId ());

        static appinfo_s::section_s      section;
        appinfo_s::section_desc_s app_desc{};

        section.finished_sections.clear ();

        app_desc.blob =
          pIter->getRootSection (&app_desc.size);

        section.parse (app_desc);
//#define _WRITE_APPID_INI
#ifdef  _WRITE_APPID_INI
        FILE* fTest =
          fopen ("appid.ini", "w");
#endif

        bool populate_appinfo_extended =
          pAppRecord->extended_config.vac.enabled == -1;
        bool populate_common =
          pAppRecord->common_config.appid == 0;
        bool populate_cloud_saves =
         //pAppRecord != nullptr &&
          pAppRecord->cloud_saves.empty ();

        bool populate_branches  =
        //pAppRecord != nullptr &&
          pAppRecord->branches.empty ();

        bool populate_launch_configs =
        //pAppRecord != nullptr      &&
          pAppRecord->launch_configs.empty ();

        pAppRecord->install_dir =
          SK_UseManifestToGetInstallDir (pAppRecord);

        // Strip double backslashes characters from the string
        try
        {
          pAppRecord->install_dir = std::regex_replace(pAppRecord->install_dir, std::wregex(LR"(\\\\)"), LR"(\)");
        }
        catch (const std::exception& e)
        {
          UNREFERENCED_PARAMETER(e);
        }

        for (auto& finished_section : section.finished_sections)
        {
          if (finished_section.keys.empty ())
            continue;

          if (pAppRecord != nullptr)
          {
            if ( populate_appinfo_extended &&
                 0 == finished_section.name.find ("appinfo.extended") )
            {
              auto *pVac =
                &pAppRecord->extended_config.vac;

              //auto *pDev =
              //  &pAppRecord->extended_config.dev;

              for (auto& key : finished_section.keys)
              {
                if (! _stricmp (key.first, "vacmodulefilename"))
                {
                  pVac->vacmodulefilename =
                    (const char *)key.second.second;
                }

                if (! pVac->vacmodulefilename.empty ())
                      pVac->enabled = true;
                else
                      pVac->enabled = false;
              }

              if (pVac->enabled == -1)
                pVac->enabled = false;
            }
            
            else if (pAppRecord->extended_config.vac.enabled == -1)
              pAppRecord->extended_config.vac.enabled = false;

            auto _ParseOSArch =
            [&](appinfo_s::section_s::_kv_pair& kv) ->
            app_record_s::CPUType
            {
              app_record_s::CPUType cpu_type = app_record_s::CPUType::Common;
              int                   bits = -1;

              // This key is an integer sometimes and a string others, it's a PITA!
              if (kv.second.first == appinfo_s::section_s::String)
              {
                bits =
                  *(char *)kv.second.second;

                if (bits == 0)
                {
                  cpu_type =
                   app_record_s::CPUType::Any;
                }

                else
                {
                  bits =
                    std::atoi ((char *)kv.second.second);
                }
              }

              if (bits != 0)
              {
                // We have an int32 key, need to extract the value
                if (bits == -1)
                  bits = *(int32_t *)kv.second.second;

                // else
                // ... Otherwise we already got the value as a string

                switch (bits)
                {
                  case 32:
                    cpu_type =
                      app_record_s::CPUType::x86;
                    break;
                  case 64:
                    cpu_type =
                      app_record_s::CPUType::x64;
                    break;
                  default:
                    cpu_type =
                      app_record_s::CPUType::Any;
                    OutputDebugStringA (
                      SK_FormatString ( "Got unexpected (int32) CPU osarch=%lu",
                                          bits ).c_str ()
                    ); break;
                }
              }

              return cpu_type;
            };

            if ( populate_common &&
                 0 ==
                 finished_section.name.find ("appinfo.common") )
            {
              pAppRecord->common_config.appid = pAppRecord->id;

              for (auto& key : finished_section.keys)
              {
                if (! _stricmp (key.first, "osarch"))
                {
                  pAppRecord->common_config.cpu_type =
                    _ParseOSArch (key);
                }

#if 0
                if (! _stricmp (key.first, "type"))
                {
                  if      (! _stricmp ((char *)key.second.second, "game"))
                    pAppRecord->type = "Game";
                  else if (! _stricmp ((char *)key.second.second, "application"))
                    pAppRecord->type = "Application";
                  else if (! _stricmp ((char *)key.second.second, "tool"))
                    pAppRecord->type = "Tool";
                  else if (! _stricmp ((char *)key.second.second, "music"))
                    pAppRecord->type = "Music";
                  else if (! _stricmp ((char *)key.second.second, "demo"))
                    pAppRecord->type = "Demo";
                  else
                    pAppRecord->type = SK_FormatString ("(?) %s", (char *)key.second.second);
                }
#endif
              }
            }

            if ( populate_launch_configs &&
                 0 ==
                 finished_section.name.find ("appinfo.config.launch.")
               )
            {
              int launch_idx = 0;

              std::sscanf ( finished_section.name.c_str (),
                              "appinfo.config.launch.%d", &launch_idx );

              auto& launch_cfg =
                pAppRecord->launch_configs [launch_idx];

              launch_cfg.id = launch_idx;

              std::unordered_map <std::string, std::wstring*>
                string_map = {
                  { "executable",  &launch_cfg.executable     },
                  { "arguments",   &launch_cfg.launch_options },
                  { "description", &launch_cfg.description    },
                  { "workingdir",  &launch_cfg.working_dir    },
                  { "type",        &launch_cfg.type           }
                };

              for (auto& key : finished_section.keys)
              {
                if (! _stricmp (key.first, "oslist"))
                {
                  if (StrStrIA ((const char *)key.second.second, "windows"))
                  {
                    app_record_s::addSupportFor (
                      pAppRecord->launch_configs [launch_idx].platforms,
                                                app_record_s::Platform::Windows
                      );
                  }

                  else
                    pAppRecord->launch_configs [launch_idx].platforms =
                      app_record_s::Platform::Unknown;
                }

                else if (! _stricmp (key.first, "osarch"))
                {
                  pAppRecord->launch_configs [launch_idx].cpu_type =
                    _ParseOSArch (key);
                }

                else if (! _stricmp (key.first, "type"))
                {
                  if (_stricmp ((char *)key.second.second, "default") &&
                      _stricmp ((char *)key.second.second, "none"))
                  {
                    pAppRecord->launch_configs [launch_idx].app_type =
                      app_record_s::AppType::Unspecified;
                  }
                }

                else if (string_map.count (key.first) != 0)
                {
                  auto& string_dest =
                    string_map [key.first];

                  *string_dest =
                    SK_UTF8ToWideChar ((const char *)key.second.second);
                }
              }

              // Convert all forward slashes (/) into backwards slashes (\) to comply with Windows norms
              if (! launch_cfg.executable.empty ())
                std::replace (launch_cfg.executable.begin  (), launch_cfg.executable.end  (), '/', '\\');
              if (! launch_cfg.working_dir.empty ())
                std::replace (launch_cfg.working_dir.begin (), launch_cfg.working_dir.end (), '/', '\\');
            }
          }
        }

        std::set    <std::wstring>          _used_executables;
        std::set    <std::wstring>          _used_executables_arguments;
        std::vector <app_record_s::launch_config_s> _launches;

        for ( auto& launch_cfg : pAppRecord->launch_configs )
        {
          auto& launch = launch_cfg.second;

        //launch.parent = pAppRecord;
        //launch.isBlacklisted ( );

          // File extension, so we can strip out non-executable ones
          wchar_t  wszExtension[MAX_PATH] = { };
          const wchar_t* pwszExt =
            PathFindExtension (launch.executable.c_str());

          // If we cannot find an extension, treat it as non-valid (does this filter out Link2EA ???)
          if (pwszExt == NULL || (pwszExt + 1) == NULL)
            launch.valid = false;

          wcsncpy_s (wszExtension, MAX_PATH, pwszExt, _TRUNCATE);

          // Flag duplicates
          if (launch.isExecutableFileNameValid())
          {
            // Use the executable to identify duplicates (affects Disable Special K menu)
            if (! _used_executables.emplace (launch.getExecutableFileName()).second)
              launch.duplicate_exe = true;

            // Use a combination of the executable and arguments to identify duplicates (affects Instant Play menu)
            if (!_used_executables_arguments.emplace (launch.getExecutableFileName() + launch.getLaunchOptions()).second)
              launch.duplicate_exe_args = true;
          }

          if ( (! app_record_s::supports (launch.platforms,
                                     app_record_s::Platform::Windows) )  ||
               (  _wcsicmp (wszExtension, L".exe") != 0)                 || // Let's filter out all non-executables
               (!       launch.valid)
             )
          {
            launch.valid = false;
          }

          else
          {
            // Working dir = empty, ., / or \ all need fixups
            if ( wcslen (launch.working_dir.c_str ()) < 2 )
            {
              if ( launch.working_dir [0] == L'/'  ||
                   launch.working_dir [0] == L'\\' ||
                   launch.working_dir [0] == L'.'  ||
                   launch.working_dir [0] == L'\0' )
              {
                launch.working_dir =
                  launch.getExecutableDir ( );
              }
            }

            // Fix working directories
            // TODO: Test this out properly with games with different working directories set!
            //       See if there's any games that uses different working directories between launch configs, but otherwise the same executable and cmd-line arguments!
            if (! launch.working_dir.empty()                        &&
                  launch.working_dir != launch.getExecutableDir ( ) &&
                  launch.working_dir != pAppRecord->install_dir)
              launch.working_dir = pAppRecord->install_dir + launch.working_dir;

            _launches.push_back (launch);
          }
        }
        
        pAppRecord->launch_configs.clear ();

        for ( auto& launch : _launches )
        {
          int i = static_cast<int> (pAppRecord->launch_configs.size());
          pAppRecord->launch_configs.emplace (i, launch);
        //pAppRecord->launch_configs[i].parent = pAppRecord;
        }

        std::map <std::string, std::wstring> roots = {
          { "WinMyDocuments",        _path_cache.my_documents.path          },
          { "WinAppDataLocal",       _path_cache.app_data_local.path        },
          { "WinAppDataLocalLow",    _path_cache.app_data_local_low.path    },
          { "WinAppDataRoaming",     _path_cache.app_data_roaming.path      },
          { "WinSavedGames",         _path_cache.win_saved_games.path       },
          { "App Install Directory", pAppRecord->install_dir               },
          { "gameinstall",           pAppRecord->install_dir               },
          { "SteamCloudDocuments",   L"<Steam Cloud Docs>"                 }
        };

        std::wstring account_id_str   = L"anonymous";

        std::wstring cloud_path       =
          SK_FormatStringW (      LR"(%ws\userdata\%ws\%d\)",
              _path_cache.steam_install, account_id_str.c_str (),
                                             appid );

        roots ["SteamCloudDocuments"] =
          cloud_path;

        for (auto& finished_section : section.finished_sections)
        {
          if (finished_section.keys.empty ())
            continue;

          if (pAppRecord != nullptr)
          {
            if ( populate_cloud_saves &&
                   0 ==
                     finished_section.name.find ("appinfo.ufs.rootoverrides.") )
            {
              std::wstring ufs_root;
              std::wstring use_instead;
              std::wstring add_path;

              std::unordered_map <std::string, std::wstring*>
                string_map = {
                  { "root",       &ufs_root    },
                  { "useinstead", &use_instead },
                  { "addpath",    &add_path    }
                };

              for ( auto& key : finished_section.keys )
              {
                if (! _stricmp (key.first, "os"))
                {
                  if (_stricmp ((char *)key.second.second, "windows"))
                  {
                    ufs_root.clear    ();
                    add_path.clear    ();
                    use_instead.clear ();
                    break;
                  }
                }

                else
                {
                  std::wstring* pStr = nullptr;

                  auto it =
                    string_map.find (key.first);

                  if (it != string_map.cend ())
                    pStr = it->second;

                  if (pStr != nullptr)
                     *pStr = SK_UTF8ToWideChar ((char *)key.second.second);

                  if (pStr == &add_path)
                    add_path += LR"(\)";
                }
              }

              if (! ufs_root.empty ())
              {
                auto utf8_ufs_root =
                  SK_WideCharToUTF8 (ufs_root);

                if (! use_instead.empty ())
                { // Some games have overrides that don't override anything
                  if (! use_instead._Equal (ufs_root))
                  {
                    auto utf8_use_instead =
                      SK_WideCharToUTF8 (use_instead);

                    roots [utf8_ufs_root] = roots.count (utf8_use_instead) != 0 ?
                                                  roots [utf8_use_instead]      :
                                                              use_instead;
                  }
                }

                if (! add_path.empty ())
                {
                  roots [utf8_ufs_root] += add_path;
                }
              }
            }

            if ( populate_branches &&
                                 0 ==
                   finished_section.name.find ("appinfo.depots.branches."))
            {
              std::string branch_name =
                finished_section.name.substr (24);

              auto *branch_ptr =
                &pAppRecord->branches [branch_name];


              static const
                std::unordered_map <std::string, ptrdiff_t>
                  int_map = {
                    { "buildid",     offsetof (app_record_s::branch_record_s, build_id)     },
                    { "pwdrequired", offsetof (app_record_s::branch_record_s, pwd_required) },
                    { "timeupdated", offsetof (app_record_s::branch_record_s, time_updated) }
                  };

              static const
                std::unordered_map <std::string, ptrdiff_t>
                  str_map = {
                    { "description", offsetof (app_record_s::branch_record_s, description) }
                  };

              int matches = 0;

              for ( auto& key : finished_section.keys )
              {
                auto pInt =
                  int_map.find (key.first);

                if (pInt != int_map.cend ())
                  *(uint32_t *)VOID_OFFSET (branch_ptr,pInt->second) =
                    *(uint32_t *)key.second.second, ++matches;

                else
                {
                  auto pStr =
                    str_map.find (key.first);

                  if (pStr != str_map.cend ()) {
                     *(std::wstring *)VOID_OFFSET (branch_ptr,pStr->second) =
                       SK_UTF8ToWideChar (
                         (char *)key.second.second
                       ), ++matches;
                  }
                }
              }

              if (matches > 0)
                pAppRecord->branches       [branch_name].parent = pAppRecord;
              else
                pAppRecord->branches.erase (branch_name);
            }
          }
        }

        for ( auto& finished_section : section.finished_sections )
        {
          if (finished_section.keys.empty ())
            continue;

          if (pAppRecord != nullptr)
          {
            if ( populate_cloud_saves &&
                                    0 ==
                   finished_section.name.find ("appinfo.ufs") )

            {
              if (finished_section.name._Equal ("appinfo.ufs"))
              {
                for ( auto& ufs_key : finished_section.keys )
                {
                  if (! _stricmp (ufs_key.first, "hidecloudui"))
                  {
                    pAppRecord->cloud_enabled =
                      (*(uint32_t *)ufs_key.second.second) == 0;
                  }
                }
              }

              else if (0 == finished_section.name.find ("appinfo.ufs.savefiles."))
              {
                int cloud_idx = 0;

                std::sscanf (
                  finished_section.name.c_str (),
                    "appinfo.ufs.savefiles.%d",
                      &cloud_idx
                );

                static const
                  std::unordered_map <std::string, app_record_s::Platform>
                    platform_map = {
                      { "windows", app_record_s::Platform::Windows },
                      { "linux",   app_record_s::Platform::Linux   },
                      { "mac",     app_record_s::Platform::Mac     },
                      { "all",     app_record_s::Platform::All     }
                    };

                if (finished_section.name.find ("platforms") != std::string::npos)
                {
                  for (auto& platform : finished_section.keys)
                  {
                    if (pAppRecord->cloud_saves.count (cloud_idx) != 0)
                    {
                      try
                      {
                        pAppRecord->cloud_saves [cloud_idx].platforms =
                          platform_map.at ((const char *)platform.second.second);
                      }
                      catch (const std::out_of_range& e) { UNREFERENCED_PARAMETER (e); };
                    }
                  }
                }

                else
                {
                  for (auto& key : finished_section.keys)
                  {
                    if (! _stricmp (key.first, "root"))
                    {
                      pAppRecord->cloud_saves [cloud_idx].root =
                        roots [(const char *)key.second.second];
                    }

                    else if (! _stricmp (key.first, "path"))
                    {
                      auto replaceSpecialValues = []
                      (       std::wstring& str,
                        const std::wstring& special,
                        const std::wstring& substitute )
                      {
                        if (special.empty ())
                          return;

                        size_t start_pos = 0;

                        while ((start_pos = str.find (special, start_pos)) != std::string::npos)
                        {
                          str.replace (
                            start_pos, special.length (),
                            substitute
                          );
                          start_pos += substitute.length ();
                        }
                      };

                      auto& rkCloudSave =
                        pAppRecord->cloud_saves [cloud_idx];

                      rkCloudSave.path =
                        SK_UTF8ToWideChar ((const char *)key.second.second);

                      static unsigned long Steam3AccountID = 0UL;
                      static uint64        Steam64BitID    = 0ULL;

                      static auto
                        CacheAccountIDs =
                         [&](void)
                          {
                            WCHAR                    szData [255] = { };
                            DWORD   dwSize = sizeof (szData);
                            PVOID   pvData =         szData;
                            CRegKey hKey ((HKEY)0);

                            if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey.m_hKey) == ERROR_SUCCESS)
                            {
                              if (RegGetValueW (hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
                                Steam3AccountID = *(DWORD*)pvData;
                            }

                            Steam64BitID = std::stoull (
                              SK_UseManifestToGetAppOwner (pAppRecord));
                          };

                        SK_RunOnce (
                          CacheAccountIDs ()
                        );

                      replaceSpecialValues ( rkCloudSave.path,
                                             L"{64BitSteamID}",
                               std::to_wstring (Steam64BitID) );

                      replaceSpecialValues ( rkCloudSave.path,
                                                          L"{Steam3AccountID}",
                                            std::to_wstring (Steam3AccountID) );
                    }
                  }
                }
              }
            }
          }

#ifdef _WRITE_APPID_INI
          fprintf (fTest, "[%s]\n", finished_section.name.c_str ());

          for ( auto& datum : finished_section.keys )
          {
            if (datum.second.first == appinfo_s::section_s::String)
              fprintf (fTest, "%s=%s\n",  datum.first,  (char     *)datum.second.second);
            else if (datum.second.first == appinfo_s::section_s::Int32)
              fprintf (fTest, "%s=%lu\n", datum.first, *(uint32_t *)datum.second.second);
          }
          fprintf (fTest, "\n");
#endif
        }

        if (pAppRecord != nullptr)
        {
          std::set <std::wstring> _used_paths;

          for ( auto& cloud_save : pAppRecord->cloud_saves )
          {
            // This only needs to be done once per-game, per-cloud path
            if (! cloud_save.second.evaluated_dir.empty ())
              continue;

            wchar_t     wszTestPath [MAX_PATH] = { };
            wnsprintf ( wszTestPath, MAX_PATH - 1,
                          L"%s\\%s", cloud_save.second.root.c_str (),
                                     cloud_save.second.path.c_str () );

            SK_FixSlashesW           (wszTestPath);
            SK_StripTrailingSlashesW (wszTestPath);
            SK_StripLeadingSlashesW  (wszTestPath);

            cloud_save.second.evaluated_dir =
              wszTestPath;

            // Skip duplicate Auto-Cloud entries
            if (! _used_paths.emplace (cloud_save.second.evaluated_dir).second)
              continue;
          }

          // Steam requires we resolve executable_path here as well
          for ( auto& launch_cfg : pAppRecord->launch_configs )
          {
            // EA games using link2ea:// protocol handlers to launch games does not have an executable,
            //  so this ensures we do not end up testing the installation folder instead (since this has
            //   bearing on whether a launch config is deemed valid or not as part of the blacklist check) 
            if (launch_cfg.second.isExecutableFileNameValid ( ))
            {
              if (! pAppRecord->install_dir.empty())
              {
                launch_cfg.second.executable_path = pAppRecord->install_dir;
                launch_cfg.second.executable_path.append (L"\\");
                launch_cfg.second.executable_path.append (launch_cfg.second.getExecutableFileName ( ));
              }
            }

            // Populate empty launch descriptions as well
            if (launch_cfg.second.description.empty())
            {
              launch_cfg.second.description      = SK_UTF8ToWideChar (pAppRecord->names.normal);
              launch_cfg.second.description_utf8 = pAppRecord->names.normal;
            }
          }
        }

#ifdef _WRITE_APPID_INI
        fclose (fTest);
#endif

        if (pAppRecord != nullptr)
          pAppRecord->processed = true;

        return pIter;
      }

      pIter =
        pIter->getNextApp ();
    }
  }

  return nullptr;
}
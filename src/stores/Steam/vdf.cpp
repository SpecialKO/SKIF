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

#include <stores/Steam/vdf.h>
#include <stores/Steam/apps_list.h>

#include <fsutil.h>

// Shorthands, to make life less painful
using appinfo_s     = skValveDataFile::appinfo_s;
using app_section_s =                  appinfo_s::section_s;

skValveDataFile::skValveDataFile (std::wstring source)
{
  path        = source;
  FILE *fData = nullptr;

  _wfopen_s (&fData, path.c_str (), L"rbS");

  if (fData != nullptr)
  {
    fseek  (fData, 0, SEEK_END);
    size_t size =
    ftell  (fData);
    rewind (fData);

    _data.resize (size);

    fread  (_data.data (), size, 1, fData);
    fclose (                        fData);

    base = (header_s *)
      _data.data ();
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
          raw_sections.back ().keys.push_back (
            { name, { String, (void *)cur }}
          );
          while (*cur != '\0')     ++cur;
          break;

        case Int32:
        case Int64:
          raw_sections.back ().keys.push_back (
            { name, { op, (void *)cur }}
          );
             cur += (operand_sizes [op]-1);
          break;

        default:
          MessageBox (nullptr, std::to_wstring (op).c_str (), L"Unknown VDF Token Operator", MB_OK);
          break;
      }
    }
  }
}

void*
appinfo_s::getRootSection (size_t* pSize)
{
  size_t kv_size =
    (size - sizeof (appinfo_s) + 8);

  if (pSize != nullptr)
     *pSize  = kv_size;

  return
    (uint8_t*)&appid + sizeof (appinfo_s);
}

appinfo_s*
appinfo_s::getNextApp (void)
{
  section_desc_s root_sec;

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
skValveDataFile::getAppInfo ( uint32_t     appid,
                              ISteamUser *pUser )
{
  if (root != nullptr)
  {
    skValveDataFile::appinfo_s *pIter = root;

    while (pIter != nullptr && pIter->appid != _LastSteamApp)
    {
      if (pIter->appid == appid)
      {
        app_record_s* pAppRecord = nullptr;

        for (auto& app : apps)
        {
          if (app.second.id == appid)
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

        static appinfo_s::section_s      section;
               appinfo_s::section_desc_s app_desc;

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
          pAppRecord != nullptr   &&
          pAppRecord->cloud_saves.empty ();

        bool populate_branches  =
          pAppRecord != nullptr &&
          pAppRecord->branches.empty ();

        bool populate_launch_configs =
          pAppRecord != nullptr      &&
          pAppRecord->launch_configs.empty ();

        if (path_cache.my_documents.path [0] == 0)
        {
          wcsncpy_s ( path_cache.steam_install, MAX_PATH,
                        SK_GetSteamDir (),      _TRUNCATE );

          SKIF_GetFolderPath ( &path_cache.my_documents       );
          SKIF_GetFolderPath ( &path_cache.app_data_local     );
          SKIF_GetFolderPath ( &path_cache.app_data_local_low );
          SKIF_GetFolderPath ( &path_cache.app_data_roaming   );
          SKIF_GetFolderPath ( &path_cache.win_saved_games    );
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
                  { "executable",  &launch_cfg.executable  },
                  { "description", &launch_cfg.description },
                  { "workingdir",  &launch_cfg.working_dir },
                  { "type",        &launch_cfg.type        }
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
            }
          }
        }

        std::set    <std::wstring>             _used_launches;
        std::vector <app_record_s::launch_config_s> _launches;

        for ( auto& launch_cfg : pAppRecord->launch_configs )
        {
          auto id     = pAppRecord->id;
          auto launch = launch_cfg.second;

          launch.isBlacklisted (id);

          if ( (! app_record_s::supports (launch.platforms,
                                     app_record_s::Platform::Windows) )  ||
               (! _used_launches.emplace (launch.blacklist_file).second) ||
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
                  launch.getExecutableDir (appid);
              }
            }

            _launches.push_back (launch);
          }
        }

        pAppRecord->launch_configs.clear ();

        for ( auto& launch : _launches )
        {
          //if (_used_launches.emplace (launch.blacklist_file).second)
          //{
            pAppRecord->launch_configs [launch.id] =
                                        launch;
          //}
        }

        std::map <std::string, std::wstring> roots = {
          { "WinMyDocuments",        path_cache.my_documents.path          },
          { "WinAppDataLocal",       path_cache.app_data_local.path        },
          { "WinAppDataLocalLow",    path_cache.app_data_local_low.path    },
          { "WinAppDataRoaming",     path_cache.app_data_roaming.path      },
          { "WinSavedGames",         path_cache.win_saved_games.path       },
          { "App Install Directory", SK_UseManifestToGetInstallDir (appid) },
          { "gameinstall",           SK_UseManifestToGetInstallDir (appid) },
          { "SteamCloudDocuments",   L"<Steam Cloud Docs>"                 }
        };

        std::wstring account_id_str   =
                          (pUser != nullptr)                     ?
          std::to_wstring (pUser->GetSteamID ().GetAccountID ()) : L"anonymous";

        std::wstring cloud_path       =
          SK_FormatStringW (      LR"(%ws\userdata\%ws\%d\)",
              path_cache.steam_install, account_id_str.c_str (),
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
                    ufs_root    = L"";
                    add_path    = L"";
                    use_instead = L"";
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

                      pAppRecord->cloud_saves [cloud_idx].path =
                        SK_UTF8ToWideChar ((const char *)key.second.second);

                      if (pUser != nullptr)
                      {
                        replaceSpecialValues (
                          pAppRecord->cloud_saves [cloud_idx].path,
                                               L"{64BitSteamID}",
                            std::to_wstring (pUser->GetSteamID ().ConvertToUint64 ())
                        );

                        replaceSpecialValues (
                          pAppRecord->cloud_saves [cloud_idx].path,
                                                            L"{Steam3AccountID}",
                            std::to_wstring (pUser->GetSteamID ().GetAccountID ())
                        );
                      }
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

            cloud_save.second.valid =
              PathFileExistsW (wszTestPath);
          }
        }

#ifdef _WRITE_APPID_INI
        fclose (fTest);
#endif

        return pIter;
      }

      pIter =
        pIter->getNextApp ();
    }
  }

  return nullptr;
}
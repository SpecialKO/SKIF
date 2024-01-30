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
#include <filesystem>

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
skValveDataFile::getAppInfo ( uint32_t appid, std::vector <std::pair < std::string, app_record_s > > *apps )
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

        for (auto& app : *apps)
        {
          if (app.second.id == appid && app.second.store == app_record_s::Store::Steam)
          {
            pAppRecord = &app.second;
            break;
          }
        }

        // If we're dealing with an unrecognized app, process it into a dummy object
        if (! pAppRecord)
        {
          static app_record_s _non_steam (appid);
          pAppRecord = &_non_steam;
        }

        // Skip already processed apps
        else if (pAppRecord->processed)
          break;

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
                    PLOG_ERROR << SK_FormatString ("Got unexpected (int32) CPU osarch=%lu", bits);
                    break;
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
                // OS Arch? More like CPU Arch...
                // 
                // This key, under common_config, either:
                //  - do not exist at all, see  23310: The Last Remnant           (what does this mean?)
                //  -            is empty, see    480: Spacewar (or most games)   (what does this mean? x86?)
                //  -      is set to "64", see 546560: Half-Life: Alyx
                //
                // This makes it utterly useless for anything reliable, lol
                // 
                // See SteamDB's unique values search:
                // - https://steamdb.info/search/?a=app_keynames&type=-1&keyname=369&operator=9&keyvalue=&display_value=on

                if (! _stricmp (key.first, "osarch"))
                {
                  pAppRecord->common_config.cpu_type =
                    _ParseOSArch (key);
                }
                
                if (! _stricmp (key.first, "type"))
                {
                  if      (! _stricmp ((char *)key.second.second, "game"))
                    pAppRecord->common_config.type =
                      app_record_s::common_config_s::AppType::Game;
                  else if (! _stricmp ((char *)key.second.second, "application"))
                    pAppRecord->common_config.type =
                      app_record_s::common_config_s::AppType::Application;
                  else if (! _stricmp ((char *)key.second.second, "tool"))
                    pAppRecord->common_config.type =
                      app_record_s::common_config_s::AppType::Tool;
                  else if (! _stricmp ((char *)key.second.second, "music"))
                    pAppRecord->common_config.type =
                      app_record_s::common_config_s::AppType::Music;
                  else if (! _stricmp ((char *)key.second.second, "demo"))
                    pAppRecord->common_config.type =
                      app_record_s::common_config_s::AppType::Demo;
                }
              }
            }

            if ( populate_launch_configs &&
                 0 ==
                 finished_section.name.find ("appinfo.config.launch.")
               )
            {
              //PLOG_VERBOSE << "---------------------------";

              int launch_idx_skif =
                static_cast<int> (pAppRecord->launch_configs.size());
              int launch_idx_steam = 0; // We do not currently actually use this for anything.
                                        // It is also unreliable as developers can remove launch configs...

              std::sscanf ( finished_section.name.c_str (),
                              "appinfo.config.launch.%d", &launch_idx_steam);

              // The index used solely for parsing
              int idx = launch_idx_steam;

              // Use the Steam launch key to workaround some parsing issue or another...
              // TODO: Fix this shit -- it's a shitty workaround for stupid duplicate parsing!
              //       AND it breaks Instant Play custom options... :(
              auto& launch_cfg =
                pAppRecord->launch_configs [launch_idx_steam]; 

              launch_cfg.id       = launch_idx_skif;
              launch_cfg.id_steam = launch_idx_steam;

              // Holds Widechar strings (external)
              std::unordered_map <std::string, std::wstring*>
                wstring_map = {
                  { "executable",  &launch_cfg.executable     },
                  { "arguments",   &launch_cfg.launch_options },
                  { "description", &launch_cfg.description    },
                  { "workingdir",  &launch_cfg.working_dir    }
                };

              // Holds UTF8 strings (internal only)
              std::unordered_map <std::string, std::string*>
                 string_map = {
              //  { "betakey",     &launch_cfg.beta_key       }, // TODO: Fix this shit -- it's landing on the duplicate launch configs
                  { "ownsdlc",     &launch_cfg.requires_dlc   }
                };

              for (auto& key : finished_section.keys)
              {
                if (! _stricmp (key.first, "oslist"))
                {
                  if (StrStrIA ((const char *)key.second.second, "windows"))
                  {
                    app_record_s::addSupportFor (
                      pAppRecord->launch_configs [idx].platforms,
                                                app_record_s::Platform::Windows
                      );
                  }

                  else
                    pAppRecord->launch_configs [idx].platforms =
                      app_record_s::Platform::Unknown;
                }

                else if (! _stricmp (key.first, "osarch"))
                {
                  // OS Arch? More like CPU Arch...
                  pAppRecord->launch_configs [idx].cpu_type =
                    _ParseOSArch (key);
                }

                else if (! _stricmp (key.first, "betakey"))
                {
                  // Populate required betas for this launch option
                  std::istringstream betas((const char *)key.second.second);
                  std::string beta;
                  while (std::getline (betas, beta, ' '))
                    pAppRecord->launch_configs [idx].branches.emplace (beta);
                }

                else if (! _stricmp (key.first, "type"))
                {
                  if (! _stricmp ((char *)key.second.second,      "default"))
                    pAppRecord->launch_configs [idx].type =
                      app_record_s::launch_config_s::Type::Default;

                  else if (! _stricmp ((char *)key.second.second, "option1"))
                    pAppRecord->launch_configs [idx].type =
                      app_record_s::launch_config_s::Type::Option1;

                  else if (! _stricmp ((char *)key.second.second, "option2"))
                    pAppRecord->launch_configs [idx].type =
                      app_record_s::launch_config_s::Type::Option2;

                  else if (! _stricmp ((char *)key.second.second, "option3"))
                    pAppRecord->launch_configs [idx].type =
                      app_record_s::launch_config_s::Type::Option3;

                  else if (! _stricmp ((char *)key.second.second, "none"))
                    pAppRecord->launch_configs [idx].type =
                      app_record_s::launch_config_s::Type::Unspecified;
                }

                else if (wstring_map.count (key.first) != 0)
                {
                  auto& string_dest =
                    wstring_map [key.first];

                  *string_dest =
                    SK_UTF8ToWideChar ((const char *)key.second.second);
                }

                else if (string_map.count (key.first) != 0)
                {
                  auto& string_dest =
                    string_map [key.first];

                  *string_dest =
                    std::string ((const char *)key.second.second);
                }
              }
              
              // There is a really annoying bug in SKIF where parsing the launch configs results in semi-duplicate empty entries,
              //   though I have no idea why... Maybe has to do with some "padding" between the sections in the appinfo.vdf file?

#if 0

              PLOG_VERBOSE << "Steam Launch ID   : " <<       launch_idx_steam;
              PLOG_VERBOSE << "SKIF  Launch ID   : " <<       launch_idx_skif;
              PLOG_VERBOSE << "Executable        : " <<       launch_cfg.executable;
              PLOG_VERBOSE << "Arguments         : " <<       launch_cfg.launch_options;
              PLOG_VERBOSE << "Working Directory : " <<       launch_cfg.working_dir;
              PLOG_VERBOSE << "Launch Type       : " << (int) launch_cfg.type;
              PLOG_VERBOSE << "Description       : " <<       launch_cfg.description;
              PLOG_VERBOSE << "Operating System  : " << (int) launch_cfg.platforms;
              PLOG_VERBOSE << "CPU Architecture  : " << (int) launch_cfg.cpu_type;
              PLOG_VERBOSE << "Beta key          : " <<       launch_cfg.beta_key;
              PLOG_VERBOSE << "Owns DLC          : " <<       launch_cfg.owns_dlc;

#endif

            }
          }
        }

        // At this point, since we used Steam's index as the position to stuff data into, the vector has
        //   has objects all over -- some at 0, some at 1, others at 0-4, not 5, 6-9. Basically we cannot
        //     be certain of anything, at all, what so bloody ever! So let's just recreate the std::map!

        std::vector <app_record_s::launch_config_s> _cleaner;

        // Put away put away put away put away
        for ( auto& keep : pAppRecord->launch_configs )
          _cleaner.push_back (keep.second);
        
        // Clean clean clean clean!
        pAppRecord->launch_configs.clear ();

        // Restore restore restore restore!
        for ( auto& launch : _cleaner )
        {
          // Reset SKIF's internal identifer for the launch configs
          launch.id = 
            static_cast<int> (pAppRecord->launch_configs.size());

          // Add it back
          pAppRecord->launch_configs.emplace (
            static_cast<int> (pAppRecord->launch_configs.size()),
            launch);
        }
        // std::map is now reliable again!

        std::set    <std::wstring>          _used_executables;
        std::set    <std::wstring>          _used_executables_arguments;
        std::vector <app_record_s::launch_config_s> _launches;

        // Now add in our custom launch configs!
        for (auto& custom_cfg : pAppRecord->launch_configs_custom)
        {
          custom_cfg.second.id =
            static_cast<int> (pAppRecord->launch_configs.size());

          // Custom launch config, so Steam launch ID is nothing we use
          custom_cfg.second.id_steam = -1;

          if (! pAppRecord->launch_configs.emplace (custom_cfg.second.id, custom_cfg.second).second)
            PLOG_ERROR << "Failed adding a custom launch config to the launch map. An element at that position already exists!";
        }

        // Clear out the custom launches once they've been populated
        pAppRecord->launch_configs_custom.clear();

        for ( auto& launch_cfg : pAppRecord->launch_configs )
        {
          auto& launch = launch_cfg.second;

          // Summarize the branches in a comma-delimited string as well
          if (! launch.branches.empty())
          {
            for (auto const& branch : launch.branches)
              launch.branches_joined += branch + ", ";

            // Remove trailing comma and space
            launch.branches_joined.pop_back();
            launch.branches_joined.pop_back();
          }

          // Filter launch configurations for other OSes
          if ( (! app_record_s::supports (launch.platforms,
                                   app_record_s::Platform::Windows) ))
            continue;

          // Filter launch configurations lacking an executable
          // This also skips all the semi-weird duplicate launch configs SKIF randomly creates
          if (launch.executable.empty())
            continue;

          launch.executable = std::filesystem::path(launch.executable).lexically_normal();

          // Filter launch configurations requiring a beta branch that the user do not have
          if (! launch.branches.empty() && launch.branches.count (pAppRecord->steam.branch) == 0)
            launch.valid = 0;

          // File extension, so we can flag out non-executable ones (e.g. link2ea)
          const wchar_t* pwszExtension =
            PathFindExtension (launch.executable.c_str());

          // If we cannot find an extension, or if it's not .exe, flag the launch config as invalid
          if (pwszExtension == NULL || (pwszExtension + 1) == NULL || _wcsicmp (pwszExtension, L".exe") != 0)
            launch.valid = 0;

          // Flag duplicates...
          //   but not if we found them as not valid above (as that would filter out custom launch configs that matches beta launch configs...)
          if (launch.isExecutableFileNameValid() && launch.valid)
          {
            // Use the executable to identify duplicates (affects Disable Special K menu)
            if (! _used_executables.emplace (launch.getExecutableFileName()).second)
              launch.duplicate_exe = true;

            // Use a combination of the executable and arguments to identify duplicates (affects Instant Play menu)
            if (! _used_executables_arguments.emplace (launch.getExecutableFileName() + launch.getLaunchOptions()).second)
              launch.duplicate_exe_args = true;
          }

          // Working dir = empty, ., / or \ all need fixups
          if ( wcslen (launch.working_dir.c_str ()) < 2 )
          {
            if ( launch.working_dir [0] == L'/'  ||
                 launch.working_dir [0] == L'\\' ||
                 launch.working_dir [0] == L'.'  ||
                 launch.working_dir [0] == L'\0' )
            {
              launch.working_dir.clear();

              //launch.working_dir =
              //  launch.getExecutableDir ( ); // Returns NULL at this point
            }
          }

          // Fix working directories
          // TODO: Test this out properly with games with different working directories set!
          //       See if there's any games that uses different working directories between launch configs, but otherwise the same executable and cmd-line arguments!
          if (! launch.working_dir.empty())
              launch.working_dir = std::wstring (pAppRecord->install_dir + launch.working_dir + L"\0\0");

          // Flag the launch config to be added back
          _launches.push_back (launch);
        }
        
        pAppRecord->launch_configs.clear ();

        for ( auto& launch : _launches )
        {
          // Reset SKIF's internal identifer for the launch configs
          launch.id = 
            static_cast<int> (pAppRecord->launch_configs.size());

          // Convert all forward slashes (/) into backwards slashes (\) to comply with Windows norms
          if (launch.valid == 1)
          {
            if (! launch.executable.empty ())
              std::replace (launch.executable.begin  (), launch.executable.end  (), '/', '\\');
            if (! launch.working_dir.empty ())
              std::replace (launch.working_dir.begin (), launch.working_dir.end (), '/', '\\');
          }
          
#if 0

          PLOG_VERBOSE << "SKIF  Launch ID   : " <<       launch.id;
          PLOG_VERBOSE << "Steam Launch ID   : " <<       launch.id_steam;
          PLOG_VERBOSE << "Executable        : " <<       launch.executable;
          PLOG_VERBOSE << "Arguments         : " <<       launch.launch_options;
          PLOG_VERBOSE << "Working Directory : " <<       launch.working_dir;
          PLOG_VERBOSE << "Launch Type       : " << (int) launch.type;
          PLOG_VERBOSE << "Description       : " <<       launch.description;
          PLOG_VERBOSE << "Operating System  : " << (int) launch.platforms;
          PLOG_VERBOSE << "CPU Architecture  : " << (int) launch.cpu_type;
          PLOG_VERBOSE << "Beta key          : " <<       launch.beta_key;
          PLOG_VERBOSE << "Owns DLC          : " <<       launch.owns_dlc;

#endif

          // Add it back
          pAppRecord->launch_configs.emplace (
            static_cast<int> (pAppRecord->launch_configs.size()),
            launch);
        }

        // Naively move the first Default launch config type to the front
        int firstValidFound = -1;

        for (auto& launch : pAppRecord->launch_configs)
        {
          // Ignore launch options requiring a beta key
          // TODO: Contemplate this choice now that SKIF can read current beta!
          if (! launch.second.branches.empty())
            continue;

          if (launch.second.type == app_record_s::launch_config_s::Type::Default)
          {
            firstValidFound = launch.first;
            break;
          }
        }

        if (firstValidFound != -1)
        {
          app_record_s::launch_config_s copy             = pAppRecord->launch_configs[0];
          pAppRecord->launch_configs[0]                  = pAppRecord->launch_configs[firstValidFound];
          pAppRecord->launch_configs[firstValidFound]    = copy;

          // Swap the internal identifers that SKIF uses (default becomes 0, not-default becomes not)
          int copy_id                                    = pAppRecord->launch_configs[0].id;
          pAppRecord->launch_configs[0].id               = 0;
          pAppRecord->launch_configs[firstValidFound].id = copy_id;
        }
          
#if 0

        for (auto& launch : pAppRecord->launch_configs)
        {
          PLOG_VERBOSE << "SKIF  Launch ID   : " <<       launch.second.id;
          PLOG_VERBOSE << "Steam Launch ID   : " <<       launch.second.id_steam;
          PLOG_VERBOSE << "Executable        : " <<       launch.second.executable;
          PLOG_VERBOSE << "Arguments         : " <<       launch.second.launch_options;
          PLOG_VERBOSE << "Working Directory : " <<       launch.second.working_dir;
          PLOG_VERBOSE << "Launch Type       : " << (int) launch.second.type;
          PLOG_VERBOSE << "Description       : " <<       launch.second.description;
          PLOG_VERBOSE << "Operating System  : " << (int) launch.second.platforms;
          PLOG_VERBOSE << "CPU Architecture  : " << (int) launch.second.cpu_type;
          PLOG_VERBOSE << "Beta key          : " <<       launch.second.beta_key;
          PLOG_VERBOSE << "Owns DLC          : " <<       launch.second.owns_dlc;
        }

#endif

        std::map <std::string, std::wstring> roots = {
          { "WinMyDocuments",        _path_cache.my_documents.path          },
          { "WinAppDataLocal",       _path_cache.app_data_local.path        },
          { "WinAppDataLocalLow",    _path_cache.app_data_local_low.path    },
          { "WinAppDataRoaming",     _path_cache.app_data_roaming.path      },
          { "WinSavedGames",         _path_cache.win_saved_games.path       },
          { "App Install Directory", pAppRecord->install_dir                },
          { "gameinstall",           pAppRecord->install_dir                },
          { "SteamCloudDocuments",   L"<Steam Cloud Docs>"                  }
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

            wchar_t     wszTestPath [MAX_PATH + 2] = { };
            wnsprintf ( wszTestPath, MAX_PATH,
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
            if (! pAppRecord->install_dir.empty())
            {
              launch_cfg.second.install_dir = pAppRecord->install_dir;
              
              // EA games using link2ea:// protocol handlers to launch games does not have an executable,
              //  so this ensures we do not end up testing the installation folder instead (since this has
              //   bearing on whether a launch config is deemed valid or not as part of the blacklist check)
              if (launch_cfg.second.isExecutableFileNameValid ( ))
              {
                launch_cfg.second.executable_path = launch_cfg.second.install_dir;
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
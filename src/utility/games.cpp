#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include "stores/Steam/apps_ignore.h"
#include <utility/fsutil.h>
#include <stores/GOG/gog_library.h>
#include <stores/epic/epic_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

// Registry Settings
#include <utility/registry.h>
#include <stores/Steam/steam_library.h>
#include <regex>

// Stuff
#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <concurrent_queue.h>

CONDITION_VARIABLE LibRefreshPaused = { };

void SKIF_GamesCollection::LoadCustomGames (std::vector <std::unique_ptr<app_generic_s>> *apps)
{
  PLOG_INFO << "Detecting custom SKIF games v2...";

  HKEY    hKey;
  DWORD   dwIndex = 0, dwResult, dwSize;
  DWORD32 dwData  = 0;
  WCHAR   szSubKey[MAX_PATH];
  WCHAR   szData  [     500];

  extern uint32_t SelectNewSKIFGame;

  /* Load custom titles from registry */
  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Games\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwIndex, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;

        dwSize   = sizeof (szSubKey) / sizeof (WCHAR);
        dwResult = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          dwSize = sizeof (DWORD32);
          if (RegGetValueW (hKey, szSubKey, L"ID", RRF_RT_REG_DWORD, NULL, &dwData, &dwSize) == ERROR_SUCCESS)
          {
            app_skif_s record;

            record.id = dwData;
            record._status.installed = true;

            dwSize = sizeof(szData) / sizeof (WCHAR);
            if (RegGetValueW (hKey, szSubKey, L"Name", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
            {
              record.names.normal = SK_WideCharToUTF8 (szData);

              // Strip null terminators
              //record.names.normal.erase(std::find(record.names.normal.begin(), record.names.normal.end(), '\0'), record.names.normal.end());

              // Add (recently added) at the end of a newly added game
              if (SelectNewSKIFGame == record.id)
                record.names.normal = SK_FormatString("%s (recently added)", record.names.normal.c_str());
            }

            dwSize = sizeof (szData) / sizeof (WCHAR);
            if (RegGetValueW (hKey, szSubKey, L"InstallDir", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
              record.install_dir = szData;

            dwSize = sizeof (szData) / sizeof (WCHAR);
            if (RegGetValueW (hKey, szSubKey, L"ExeFileName", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS) // L"Exe"
            {
              app_generic_s::launch_config_s lc(&record);
              lc.id           = 0;
              lc.valid        = true;
              lc.executable   = szData;
              lc.working_dir  = record.install_dir;

              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"Exe", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                lc.executable_path = szData;

              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"LaunchOptions", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                lc.launch_options = szData;

              //record.launch_configs.emplace (0, lc);

              /*
              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"ExeFileName", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                record.specialk.profile_dir = szData;
              */
              record.specialk.profile_dir = lc.executable;

              record.specialk.injection.injection.type = app_generic_s::sk_install_state_s::Injection::Type::Global;

              apps->emplace_back (std::make_unique <app_skif_s>(record));
            }
          }
        }
      }
    }

    RegCloseKey (hKey);
  }
}

SKIF_GamesCollection::SKIF_GamesCollection (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );


}

bool
SKIF_GamesCollection::RefreshGames (bool refresh)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  /*
  PLOG_INFO_IF(m_pPatTexSRV == nullptr) << "Loading the embedded Patreon texture...";
  ImVec2 dontCare1, dontCare2;
  if (m_pPatTexSRV == nullptr)
    LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, m_pPatTexSRV,          L"patreon.png",         dontCare1, dontCare2);
  if (m_pSKLogoTexSRV == nullptr)
    LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, m_pSKLogoTexSRV,       L"sk_boxart.png",       dontCare1, dontCare2);
  if (m_pSKLogoTexSRV_small == nullptr)
    LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, m_pSKLogoTexSRV_small, L"sk_boxart_small.png", dontCare1, dontCare2);
  */

  struct lib_worker_thread_s {
    std::vector <
      std::pair <std::string, app_record_s >
                > apps;
    Trie          labels  = Trie { };
    HANDLE        hWorker = NULL;
    int           iWorker = 0;
  };

  static lib_worker_thread_s* library_worker = nullptr;

  if (refresh && library_worker == nullptr)
  {
    PLOG_INFO << "Populating library list...";

    library_worker = new lib_worker_thread_s;

    uintptr_t hWorkerThread =
    _beginthreadex (nullptr, 0x0, [](void * var) -> unsigned
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibraryWorker");

      // Is this combo really appropriate for this thread?
      //SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
      //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

      PLOG_DEBUG << "SKIF_LibraryWorker thread started!";

      lib_worker_thread_s* _data = static_cast<lib_worker_thread_s*>(var);

      static SKIF_GamesCollection& parent = SKIF_GamesCollection::GetInstance ( );

      // Load Steam titles from disk
      if (_registry.bLibrarySteam)
      {
        SKIF_Steam_GetInstalledAppIDs (&_data->apps);

        // Refresh the current Steam user
        SKIF_Steam_GetCurrentUser ( );
      
        // Preload user-specific stuff for all Steam games (custom launch options + DLC ownership)
        SKIF_Steam_PreloadUserLocalConfig (SKIF_Steam_GetCurrentUser ( ), &_data->apps);
      }

      const int              SKIF_STEAM_APPID  = 1157970;
      bool                   SKIF_STEAM_OWNER  = false;

      if ( ! SKIF_STEAM_OWNER )
      {
        app_record_s SKIF_record (SKIF_STEAM_APPID);

        SKIF_record.id              = SKIF_STEAM_APPID;
        SKIF_record.names.normal    = "Special K";
        SKIF_record.names.all_upper = "SPECIAL K";
        SKIF_record.install_dir     = _path_cache.specialk_install;
        SKIF_record.store           = app_record_s::Store::Steam;
        SKIF_record.store_utf8      = "Steam";
        SKIF_record.ImGuiLabelAndID = SK_FormatString("%s###%i-%i", SKIF_record.names.normal.c_str(), (int)SKIF_record.store, SKIF_record.id);
        SKIF_record.ImGuiPushID     = SK_FormatString("%i-%i", (int)SKIF_record.store, SKIF_record.id);

        SKIF_record.specialk.profile_dir =
          SK_FormatStringW(LR"(%ws\Profiles)",
            _path_cache.specialk_userdata);

        std::pair <std::string, app_record_s>
          SKIF ( "Special K", SKIF_record );

        _data->apps.emplace_back (SKIF);
      }

      // Load GOG titles from registry
      if (_registry.bLibraryGOG)
        SKIF_GOG_GetInstalledAppIDs (&_data->apps);

      // Load Epic titles from disk
      if (_registry.bLibraryEpic)
        SKIF_Epic_GetInstalledAppIDs (&_data->apps);
    
      if (_registry.bLibraryXbox)
        SKIF_Xbox_GetInstalledAppIDs (&_data->apps);

      // Load custom SKIF titles from registry
      if (_registry.bLibraryCustom)
        SKIF_GetCustomAppIDs (&_data->apps);

      PLOG_INFO << "Loading custom launch configs synchronously...";

      static const std::pair <bool, std::wstring> lc_files[] = {
        { false, SK_FormatStringW(LR"(%ws\Assets\lc_user.json)", _path_cache.specialk_userdata) }, // We load user-specified first
        {  true, SK_FormatStringW(LR"(%ws\Assets\lc.json)",      _path_cache.specialk_userdata) }
      };

      for (auto& lc_file : lc_files)
      {
        std::ifstream file(lc_file.second);
        nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
        file.close();

        if (jf.is_discarded ( ))
        {
          PLOG_ERROR << "Error occurred while trying to parse " << lc_file.second;

          // We are dealing with lc.json and something went wrong
          //   delete the file so a new attempt is performed later
          if (lc_file.first)
            PLOG_INFO_IF(DeleteFile (lc_file.second.c_str())) << "Deleting file so a retry occurs the next time an online check is performed...";

          continue;
        }

        try {
          for (auto& app : _data->apps)
          {
            auto& record     =  app.second;
            auto& append_cfg = (record.store == app_record_s::Store::Steam) ? record.launch_configs_custom
                                                                            : record.launch_configs;

            std::string key  = (record.store == app_record_s::Store::Epic)  ? record.Epic_AppName     :
                               (record.store == app_record_s::Store::Xbox)  ? record.Xbox_PackageName :
                                                              std::to_string (record.id);

            for (auto& launch_config : jf[record.store_utf8][key])
            {
              app_record_s::launch_config_s lc;
              lc.id                       = static_cast<int> (append_cfg.size());
              lc.valid                    = 1;
              lc.description              = SK_UTF8ToWideChar(launch_config.at("Desc"));
              lc.executable               = SK_UTF8ToWideChar(launch_config.at("Exe"));
              std::replace(lc.executable.begin(), lc.executable.end(), '/', '\\'); // Replaces all forward slashes with backslashes
              lc.working_dir              = SK_UTF8ToWideChar(launch_config.at("Dir"));
              lc.launch_options           = SK_UTF8ToWideChar(launch_config.at("Args"));
              lc.executable_path          = record.install_dir + L"\\" + lc.executable;
              lc.install_dir              = record.install_dir;
              lc.custom_skif              =   lc_file.first;
              lc.custom_user              = ! lc.custom_skif;

              append_cfg.emplace (lc.id, lc);
            }
          }
        }
        catch (const std::exception&)
        {
          PLOG_ERROR << "Error occurred when trying to parse " << ((lc_file.first) ? "online-based" : "user-specified") << " launch configs";
        }
      }

      PLOG_INFO << "Loading game names...";

      // Process the list of apps -- prepare their names, keyboard search, as well as remove any uninstalled entries
      for (auto& app : _data->apps)
      {
        //PLOG_DEBUG << "Working on " << app.second.id << " (" << app.second.store_utf8 << ")";

        // Steam handling...
        if (app.second.store == app_record_s::Store::Steam)
        { 
          // Special handling for non-Steam owners of Special K / SKIF
          if (app.second.id == SKIF_STEAM_APPID)
            app.first = "Special K";

          // Regular handling for the remaining Steam games
          else {
            app.first.clear ();

            if (SKIF_Steam_UpdateAppState (&app.second))
              app.second._status.dwTimeLastChecked = SKIF_Util_timeGetTime1 ( ) + 333UL; // _RefreshInterval
          }
        }

        // Only bother opening the application manifest
        //   and looking for a name if the client claims
        //     the app is installed.
        if (app.second._status.installed)
        {
          if (! app.second.names.normal.empty ())
          {
            app.first = app.second.names.normal;
          }

          // Some games have an install state but no name,
          //   for those we have to consult the app manifest
          else if (app.second.store == app_record_s::Store::Steam)
          {
            app.first =
              SK_UseManifestToGetAppName (&app.second);
          }

          // Corrupted app manifest / not known to Steam client; SKIP!
          if (app.first.empty ())
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has no name; ignoring!";

            app.second.id = 0;
            continue;
          }

          /*
          if (app.second.launch_configs.size() == 0)
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has no launch config; ignoring!";

            app.second.id = 0;
            continue;
          }
          */

          std::string original_name = app.first;

          // Some games use weird Unicode character combos that ImGui can't handle,
          //  so let's replace those with the normal ones.

          // Replace RIGHT SINGLE QUOTATION MARK (Code: 2019 | UTF-8: E2 80 99)
          //  with a APOSTROPHE (Code: 0027 | UTF-8: 27)
          app.first = std::regex_replace(app.first, std::regex("\xE2\x80\x99"), "\x27");

          // Replace LATIN SMALL LETTER O (Code: 006F | UTF-8: 6F) and COMBINING DIAERESIS (Code: 0308 | UTF-8: CC 88)
          //  with a LATIN SMALL LETTER O WITH DIAERESIS (Code: 00F6 | UTF-8: C3 B6)
          app.first = std::regex_replace(app.first, std::regex("\x6F\xCC\x88"), "\xC3\xB6");

          // Strip game names from special symbols (disabled due to breaking some Chinese characters)
          //const char* chars = (const char *)u8"\u00A9\u00AE\u2122"; // Copyright (c), Registered (R), Trademark (TM)
          //for (unsigned int i = 0; i < strlen(chars); ++i)
            //app.first.erase(std::remove(app.first.begin(), app.first.end(), chars[i]), app.first.end());

          // Remove COPYRIGHT SIGN (Code: 00A9 | UTF-8: C2 A9)
          app.first = std::regex_replace(app.first, std::regex("\xC2\xA9"), "");

          // Remove REGISTERED SIGN (Code: 00AE | UTF-8: C2 AE)
          app.first = std::regex_replace(app.first, std::regex("\xC2\xAE"), "");

          // Remove TRADE MARK SIGN (Code: 2122 | UTF-8: E2 84 A2)
          app.first = std::regex_replace(app.first, std::regex("\xE2\x84\xA2"), "");

          if (original_name != app.first)
            PLOG_DEBUG << "Game title was changed: " << SK_UTF8ToWideChar(original_name.c_str()) << " (" << original_name << ") --> " << SK_UTF8ToWideChar(app.first.c_str()) << " (" << app.first << ")";

          // Strip any remaining null terminators
          app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

          // Trim leftover spaces
          app.first.erase(app.first.begin(), std::find_if(app.first.begin(), app.first.end(), [](unsigned char ch) { return !std::isspace(ch); }));
          app.first.erase(std::find_if(app.first.rbegin(), app.first.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), app.first.end());
          
          // Update ImGuiLabelAndID and ImGuiPushID
          app.second.ImGuiLabelAndID = SK_FormatString("%s###%i-%i", app.first.c_str(), (int)app.second.store, app.second.id);
          app.second.ImGuiPushID     = SK_FormatString("%i-%i", (int)app.second.store, app.second.id);
        }

        // Check if install folder exists (but not for SKIF)
        if (app.second.id    != SKIF_STEAM_APPID          &&
            app.second.store != app_record_s::Store::Xbox )
        {
          std::wstring install_dir;

          if (app.second.store == app_record_s::Store::Steam)
            install_dir = SK_UseManifestToGetInstallDir (&app.second);
          else
            install_dir = app.second.install_dir;
          
          if (! PathFileExists(install_dir.c_str()))
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has non-existent install folder; ignoring!";

            app.second.id = 0;
            continue;
          }

          // Preload active branch for Steam games
          if (app.second.store == app_record_s::Store::Steam)
          {
            app.second.branch = SK_UseManifestToGetCurrentBranch (&app.second);

            if (! app.second.branch.empty())
              PLOG_VERBOSE << "App ID " << app.second.id << " has active branch : " << app.second.branch;
          }
        }

        // Prepare for the keyboard hint / search/filter functionality
        if ( app.second._status.installed || app.second.id == SKIF_STEAM_APPID)
        {
          InsertTrieKey (&app, &_data->labels);
        }
      }

      PLOG_INFO << "Finished loading game names...";
    
      std::sort ( _data->apps.begin (),
                  _data->apps.end   (),
        []( const std::pair <std::string, app_record_s>& a,
            const std::pair <std::string, app_record_s>& b ) -> int
        {
          return a.second.names.all_upper_alnum.compare(
                 b.second.names.all_upper_alnum
          ) < 0;
        }
      );

      //PLOG_INFO << "Apps were sorted!";

      PLOG_INFO << "Finished populating the library list.";

      // Force a refresh when the game icons have finished being streamed
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

      PLOG_DEBUG << "SKIF_LibraryWorker thread stopped!";

      //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

      return 0;
    }, library_worker, 0x0, nullptr);

    bool threadCreated = (hWorkerThread != 0);

    if (threadCreated)
    {
      library_worker->hWorker = reinterpret_cast<HANDLE>(hWorkerThread);
      library_worker->iWorker = 1;
    }
    else // Someting went wrong during thread creation, so free up the memory we allocated earlier
    {
      delete library_worker;
      library_worker = nullptr;
    }
  }

  else if (refresh && library_worker != nullptr && library_worker->iWorker == 1 && WaitForSingleObject (library_worker->hWorker, 0) == WAIT_OBJECT_0)
  {

    struct IconCache {
      app_record_s::tex_registry_s tex_icon;
      app_record_s::Store store;
      AppId_t id;
    };

    std::vector<IconCache> icon_cache;

    // Clear up any unacknowledged icon workers
    for (auto& app : g_apps)
    {
      if (app.second.tex_icon.iWorker == 1)
      {
        if (WaitForSingleObject (app.second.tex_icon.hWorker, 200) == WAIT_OBJECT_0) // 200 second timeout (maybe change it?)
        {
          CloseHandle (app.second.tex_icon.hWorker);
          app.second.tex_icon.hWorker = NULL;
          app.second.tex_icon.iWorker = 2;
          m_iIconWorkers--;
        }
      }
      
      // Cache any existing icon textures...
      if (app.second.tex_icon.texture.p != nullptr)
      {
        icon_cache.push_back ({
          app.second.tex_icon,
          app.second.store,
          app.second.id
        });

        //SKIF_ResourcesToFree.push(app.second.tex_icon.texture.p);
        //app.second.tex_icon.texture.p = nullptr;
      }
    }

    // Clear current data
    g_apps.clear();
    m_tLabels = Trie { };

    // Insert new data
    g_apps = library_worker->apps;
    m_tLabels = library_worker->labels;

    // Move cached icons over
    for (auto& app : g_apps)
    {
      for (auto& icon : icon_cache)
      {
        if (icon.id    == app.second.id
         && icon.store == app.second.store)
        {
          app.second.tex_icon = icon.tex_icon; // Move it over
          icon.id = 0; // Mark it _not_ for release
          break;
        }
      }
    }

    // Push unused icons for release
    for (auto& icon : icon_cache)
    {
      if (icon.id == 0)
        continue; // Skip icons marked as 0
      
      //SKIF_ResourcesToFree.push(icon.tex_icon.texture.p);
      icon.tex_icon.texture.p = nullptr;
    }

    CloseHandle (library_worker->hWorker);
    library_worker->hWorker = NULL;
    library_worker->iWorker = 2;
    library_worker->labels  = Trie { };
    library_worker->apps.clear ();

    delete library_worker;
    library_worker = nullptr;

    //populated    = true;
    //sort_changed = true;
  }


}

std::vector <std::unique_ptr<app_generic_s>>*
SKIF_GamesCollection::GetGames (void)
{
  static snapshot_s& snapshot = snapshots[0];
  static int lastRead = 0;
  int lastWritten = snapshot_idx_written.load ( );

  if (lastRead != lastWritten)
  {
    lastRead = lastWritten;
    snapshot_idx_reading.store (lastRead);
    snapshot = snapshots[lastRead];
  }

  return snapshot.apps;
}

#pragma region Trie Keyboard Hint Search

// Iterative function to insert a key in the Trie
void
Trie::insert (const std::string& key)
{
  // start from root node
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // create a new node if path doesn't exists
    if (curr->character [key [i]] == nullptr)
        curr->character [key [i]]  = new Trie ();

    // go to next node
    curr = curr->character [key [i]];
  }

  // mark current node as leaf
  curr->isLeaf = true;
}

// Iterative function to search a key in Trie. It returns true
// if the key is found in the Trie, else it returns false
bool
Trie::search (const std::string& key)
{
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // go to next node
    curr = curr->character [key [i]];

    // if string is invalid (reached end of path in Trie)
    if (curr == nullptr)
      return false;
  }

  // if current node is a leaf and we have reached the
  // end of the string, return true
  return curr->isLeaf;
}

// returns true if given node has any children
bool
Trie::haveChildren (Trie const* curr)
{
  for (int i = 0; i < CHAR_SIZE; i++)
    if (curr->character [i])
      return true;  // child found

  return false;
}

// Recursive function to delete a key in the Trie
bool
Trie::deletion (Trie*& curr, const std::string& key)
{
  // return if Trie is empty
  if (curr == nullptr)
    return false;

  // if we have not reached the end of the key
  if (key.length ())
  {
    // recur for the node corresponding to next character in the key
    // and if it returns true, delete current node (if it is non-leaf)

    if (        curr                      != nullptr       &&
                curr->character [key [0]] != nullptr       &&
      deletion (curr->character [key [0]], key.substr (1)) &&
                curr->isLeaf == false)
    {
      if (! haveChildren (curr))
      {
        delete curr;
        curr = nullptr;
        return true;
      }

      else {
        return false;
      }
    }
  }

  // if we have reached the end of the key
  if (key.length () == 0 && curr->isLeaf)
  {
    // if current node is a leaf node and don't have any children
    if (! haveChildren (curr))
    {
      // delete current node
      delete curr;
      curr = nullptr;

      // delete non-leaf parent nodes
      return true;
    }

    // if current node is a leaf node and have children
    else
    {
      // mark current node as non-leaf node (DON'T DELETE IT)
      curr->isLeaf = false;

      // don't delete its parent nodes
      return false;
    }
  }

  return false;
}

void
InsertTrieKey (std::pair <std::string, app_record_s>* app, Trie* labels)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  std::string all_upper = SKIF_Util_ToUpper (app->first),
              all_upper_alnum;
          
  for (const char c : app->first)
  {
    if (! ( isalnum (c) || isspace (c) ))
      continue;

    all_upper_alnum += (char)toupper (c);
  }

  size_t stripped = 0;

  if (_registry.bLibraryIgnoreArticles)
  {
    static const
      std::string toSkip [] =
      {
        std::string ("A "),
        std::string ("AN "),
        std::string ("THE ")
      };

    for ( auto& skip_ : toSkip )
    {
      if (all_upper_alnum.find (skip_) == 0)
      {
        all_upper_alnum =
          all_upper_alnum.substr (
            skip_.length ()
          );

        stripped = skip_.length ();
        break;
      }
    }
  }

  std::string trie_builder;

  for ( const char c : all_upper_alnum)
  {
    trie_builder += c;

    labels->insert (trie_builder);
  }
        
  app->second.names.normal          = app->first;
  app->second.names.all_upper       = all_upper;
  app->second.names.all_upper_alnum = all_upper_alnum;
  app->second.names.pre_stripped    = stripped;
}

#pragma endregion
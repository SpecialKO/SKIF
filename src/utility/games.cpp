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

CONDITION_VARIABLE LibRefreshPaused = { };

void SKIF_GamesCollection::LoadCustomGames (std::vector <std::unique_ptr<app_generic_s>> *apps)
{
  PLOG_INFO << "Detecting custom SKIF games v2...";

  HKEY    hKey;
  DWORD   dwIndex = 0, dwResult, dwSize;
  DWORD32 dwData  = 0;
  WCHAR   szSubKey[MAX_PATH * sizeof (WCHAR)];
  WCHAR   szData  [     500 * sizeof (WCHAR)];

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

              //record.launch_configs[0] = lc;

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

  InitializeConditionVariable (&LibRefreshPaused);

  // Start the child thread that is responsible for refreshing the library
  static HANDLE hThread =
    CreateThread ( nullptr, 0x0,
      [](LPVOID)
    -> DWORD
    {
      CRITICAL_SECTION            LibraryRefreshJob = { };
      InitializeCriticalSection (&LibraryRefreshJob);
      EnterCriticalSection      (&LibraryRefreshJob);

      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibraryRefreshJob");

      SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

      static SKIF_GamesCollection& parent = SKIF_GamesCollection::GetInstance();
      extern bool SKIF_Shutdown;

      do
      {
        static int lastWritten = 0;
        int currReading        = parent.snapshot_idx_reading.load ( );

        // This is some half-assed attempt of implementing triple-buffering where we don't overwrite our last finished snapshot.
        // If the main thread is currently reading from the next intended target, we skip that one as it means we have somehow
        //   managed to loop all the way around before the main thread started reading our last written result.
        int currWriting = (currReading == (lastWritten + 1) % 3)
                                        ? (lastWritten + 2) % 3  // Jump over very next one as it is currently being read from
                                        : (lastWritten + 1) % 3; // It is fine to write to the very next one

        auto &snapshot =
          parent.snapshots [currWriting];

        auto& Apps     = snapshot.apps;
        auto& Labels   = snapshot.labels;


        // If Apps is currently not an object, create one!
        if (Apps == nullptr)
          Apps = new std::vector <std::unique_ptr<app_generic_s>>;

        // Maybe not clear stuff out? It would mean destroying icon (and cover; but mostly icon) references and forcing them to be reloaded...
        // Maybe we should instead create a new temporary structure, load the basics in it, then move icon (and cover?) references over, and finally remove any now-removed games?
        else
          Apps->clear    ();

        // DO STUFF

        //OutputDebugString (L"SKIF_LibraryRefreshJob - Refreshed!\n");
    
        std::vector <std::pair <std::string, app_record_s>> ret;

        // TODO: Unsafe thread handling which can crash!!
        if (_registry.bLibrarySteam)
        {
          std::set <uint32_t> unique_apps;

          for ( auto app : SK_Steam_GetInstalledAppIDs ( ))
          {
            // Skip Steamworks Common Redists
            if (app == 228980) continue;

            // Skip IDs related to apps, DLCs, music, and tools (including Special K for now)
            if (std::find(std::begin(steam_apps_ignorable), std::end(steam_apps_ignorable), app) != std::end(steam_apps_ignorable)) continue;

            if (unique_apps.emplace (app).second)
            {
              // Opening the manifests to read the names is a
              //   lengthy operation, so defer names and icons
              ret.emplace_back (
                "Loading...", app
              );
            }
          }
        }

        bool      SKIF_STEAM_OWNER = false;
        const int SKIF_STEAM_APPID = 1157970;

        for (const auto& app : *Apps)
          if (app->id == SKIF_STEAM_APPID)
            SKIF_STEAM_OWNER = true;

        /*
        if ( ! SKIF_STEAM_OWNER )
        {
          app_steam_s SKIF_record;

          SKIF_record.id              = SKIF_STEAM_APPID;
          SKIF_record.names.normal    = "Special K";
          SKIF_record.names.all_upper = "SPECIAL K";
          SKIF_record.install_dir     = _path_cache.specialk_install;

          Apps->emplace_back (std::make_unique <app_steam_s>(SKIF_record));
        }
        */

        parent.LoadCustomGames (Apps);

        /*
        std::vector <std::pair < std::string, app_record_s > > apps;

        // Load GOG titles from registry
        if (! _registry.bDisableGOGLibrary)
          SKIF_GOG_GetInstalledAppIDs (&apps);

        // Load Epic titles from disk
        if (! _registry.bDisableEpicLibrary)
          SKIF_Epc_GetInstalledAppIDs (&apps);
    
        if (! _registry.bDisableXboxLibrary)
          SKIF_Xbox_GetInstalledAppIDs (&apps);

        // Load custom SKIF titles from registry
        SKIF_GetCustomAppIDs (&apps);
        */

        // END DO STUFF

        // Parse titles

        
        // Handle names first
        for (const auto& app : *Apps)
        {
          //PLOG_DEBUG << "Working on " << app->id << " (" << app->store << ")";

          // Regular handling for the remaining Steam games
#if 0
          // Needs differentialized approach between platforms maybe?
          if (app->store == app_generic_s::Store::Steam &&
              app->id    != SKIF_STEAM_APPID)
            app->_status.refresh (app.get());
#endif

          // Only bother opening the application manifest
          //   and looking for a name if the client claims
          //     the app is installed.
          if (app->_status.installed)
          {
            // Some games have an install state but no name,
            //   for those we have to consult the app manifest
            if (app->store == app_generic_s::Store::Steam)
            {
              app->names.normal =
                SK_UseManifestToGetAppName (
                              app->id );
            }

            // Corrupted app manifest / not known to Steam client; SKIP!
            if (app->names.normal.empty ())
            {
              PLOG_DEBUG << "App ID " << app->id << " (" << (int)app->store << ") has no name; ignoring!";

              app->id = 0;
              continue;
            }

            std::string original_name = app->names.normal;

            // Some games use weird Unicode character combos that ImGui can't handle,
            //  so let's replace those with the normal ones.

            // Replace RIGHT SINGLE QUOTATION MARK (Code: 2019 | UTF-8: E2 80 99)
            //  with a APOSTROPHE (Code: 0027 | UTF-8: 27)
            app->names.normal = std::regex_replace(app->names.normal, std::regex("\xE2\x80\x99"), "\x27");

            // Replace LATIN SMALL LETTER O (Code: 006F | UTF-8: 6F) and COMBINING DIAERESIS (Code: 0308 | UTF-8: CC 88)
            //  with a LATIN SMALL LETTER O WITH DIAERESIS (Code: 00F6 | UTF-8: C3 B6)
            app->names.normal = std::regex_replace(app->names.normal, std::regex("\x6F\xCC\x88"), "\xC3\xB6");

            // Strip game names from special symbols (disabled due to breaking some Chinese characters)
            //const char* chars = (const char *)u8"\u00A9\u00AE\u2122"; // Copyright (c), Registered (R), Trademark (TM)
            //for (unsigned int i = 0; i < strlen(chars); ++i)
              //app->names.normal.erase(std::remove(app->names.normal.begin(), app->names.normal.end(), chars[i]), app->names.normal.end());

            // Remove COPYRIGHT SIGN (Code: 00A9 | UTF-8: C2 A9)
            app->names.normal = std::regex_replace(app->names.normal, std::regex("\xC2\xA9"), "");

            // Remove REGISTERED SIGN (Code: 00AE | UTF-8: C2 AE)
            app->names.normal = std::regex_replace(app->names.normal, std::regex("\xC2\xAE"), "");

            // Remove TRADE MARK SIGN (Code: 2122 | UTF-8: E2 84 A2)
            app->names.normal = std::regex_replace(app->names.normal, std::regex("\xE2\x84\xA2"), "");

            if (original_name != app->names.normal)
            {
              PLOG_DEBUG << "Game title was changed:";
              PLOG_DEBUG << "Old: " << SK_UTF8ToWideChar(original_name.c_str())     << " (" << original_name     << ")";
              PLOG_DEBUG << "New: " << SK_UTF8ToWideChar(app->names.normal.c_str()) << " (" << app->names.normal << ")";
            }

            // Strip any remaining null terminators
            app->names.normal.erase(std::find(app->names.normal.begin(), app->names.normal.end(), '\0'), app->names.normal.end());

            // Trim leftover spaces
            app->names.normal.erase(app->names.normal.begin(), std::find_if(app->names.normal.begin(), app->names.normal.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            app->names.normal.erase(std::find_if(app->names.normal.rbegin(), app->names.normal.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), app->names.normal.end());
          
            // Update ImGuiLabelAndID and ImGuiPushID
            app->ImGuiLabelAndID = SK_FormatString("%s###%i%i", app->names.normal.c_str(), (int)app->store, app->id);
            app->ImGuiPushID     = SK_FormatString("%i%i", (int)app->store, app->id);
          }

          // Check if install folder exists (but not for SKIF)
          if (app->id != SKIF_STEAM_APPID && app->store != app_generic_s::Store::Xbox)
          {
            std::wstring install_dir;

            if (app->store == app_generic_s::Store::Steam)
              install_dir = SK_UseManifestToGetInstallDir(app->id);
            else
              install_dir = app->install_dir;
          
            if (! PathFileExists(install_dir.c_str()))
            {
              PLOG_DEBUG << "App ID " << app->id << " (" << (int)app->store << ") has non-existent install folder; ignoring!";

              app->id = 0;
              continue;
            }
          }

          // Prepare for the keyboard hint / search/filter functionality
          if ( app->_status.installed || app->id == SKIF_STEAM_APPID)
          {
            std::string all_upper = SKIF_Util_ToUpper (app->names.normal),
                        all_upper_alnum;
          
            for (const char c : app->names.normal)
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

              Labels.insert (trie_builder);
            }
        
            app->names.normal          = app->names.normal;
            app->names.all_upper       = all_upper;
            app->names.all_upper_alnum = all_upper_alnum;
            app->names.pre_stripped    = stripped;
          }
        }

        // Sort the results
        std::sort ( Apps->begin (),
                    Apps->end   (),
          []( const std::unique_ptr<app_generic_s>& a,
              const std::unique_ptr<app_generic_s>& b ) -> int
          {
            return a.get()->names.all_upper_alnum.compare(
                   b.get()->names.all_upper
            ) < 0;
          }
        );

        // Swap in the results
        lastWritten = currWriting;
        parent.snapshot_idx_written.store (lastWritten);

        parent.awake.store (false);

        while (! parent.awake.load ( ))
        {
          SleepConditionVariableCS (
            &LibRefreshPaused, &LibraryRefreshJob,
              INFINITE
          );
        }

      } while (! SKIF_Shutdown); // Keep thread alive until exit

      SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

      LeaveCriticalSection  (&LibraryRefreshJob);
      DeleteCriticalSection (&LibraryRefreshJob);

      return 0;
    }, nullptr, 0x0, nullptr
  );
}

void
SKIF_GamesCollection::RefreshGames (void)
{
  awake.store (true);

  WakeConditionVariable (&LibRefreshPaused);
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

#pragma endregion
#include <games.h>
#include <SKIF.h>
#include <SKIF_utility.h>
#include "stores/Steam/apps_ignore.h"
#include <fsutil.h>
#include <stores/GOG/gog_library.h>
#include <stores/EGS/epic_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

// Registry Settings
#include <registry.h>
#include <stores/Steam/steam_library.h>

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
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwResult, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      do
      {
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
              if (RegGetValueW(hKey, szSubKey, L"Exe", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                lc.executable_path = szData;

              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW(hKey, szSubKey, L"LaunchOptions", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
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

        dwIndex++;

      } while (1);
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

        auto& Apps      = snapshot.apps;


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
        if (! _registry.bDisableSteamLibrary)
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

        // Load EGS titles from disk
        if (! _registry.bDisableEGSLibrary)
          SKIF_EGS_GetInstalledAppIDs (&apps);
    
        if (! _registry.bDisableXboxLibrary)
          SKIF_Xbox_GetInstalledAppIDs (&apps);

        // Load custom SKIF titles from registry
        SKIF_GetCustomAppIDs (&apps);
        */

        // END DO STUFF

        // Sort the results
        //SortProcesses (Processes);

        // Swap in the results
        lastWritten = currWriting;
        parent.snapshot_idx_written.store (lastWritten);

        SleepConditionVariableCS (
          &LibRefreshPaused, &LibraryRefreshJob,
            INFINITE
        );

      } while (IsWindow (SKIF_hWnd)); // Keep thread alive until exit

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

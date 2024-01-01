
#include <stores/gog/gog_library.h>
#include <wtypes.h>
#include <filesystem>


/*
GOG Galaxy / Offline Installers shared registry struture


Root Key: HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\GOG.com\Games\

Each game is stored in a separate key beneath, named after the Game ID/Product ID of the game.
Each key have a bunch of values with some basic data of the game, its location, and launch options.

Registry values and the data they contain:

    exe                     -- Full path to game executable
    exeFile                 -- Filename of game executable
    gameID                  -- App ID of the game
    gameName                -- Title of the game
    launchCommand           -- Default launch full path and parameter of the game
    launchParam             -- Default launch parameter of the game
    path                    -- Path to the game folder
    productID               -- Same as App ID of the game ?
    uninstallCommand        -- Full path to the uninstaller of the game
    workingDir              -- Working directory of the game

There are more values, but they aren't listed here.


GOG Galaxy Custom Default Launch Option:
To launch a game using the Galaxy user's customized launch option,
it's enough to launch the game through Galaxy like the start menu shortcuts does, like this:

    "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

*/

void
SKIF_GOG_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
{
  PLOG_INFO << "Detecting GOG games...";

  HKEY  hKey;
  DWORD dwIndex  = 0,
        dwResult = 0,
        dwSize   = 0,
        dwRead   = 0;
  WCHAR szSubKey[MAX_PATH] = { };
  WCHAR szData  [MAX_PATH] = { };

  /* Load GOG titles from registry */
  if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\GOG.com\Games\)", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwIndex, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;

        dwSize   = sizeof(szSubKey) / sizeof(WCHAR);
        dwResult = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          dwSize = sizeof(szData) / sizeof(WCHAR);

          if (RegGetValueW (hKey, szSubKey, L"dependsOn", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS &&
            wcslen(szData) == 0) // Only handles items without a dependency (skips DLCs)
          {
            dwSize = sizeof(szData) / sizeof(WCHAR);

            if (RegGetValueW (hKey, szSubKey, L"GameID", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
            {
              int appid = _wtoi(szData);
              app_record_s record(appid);

              record.store      = app_record_s::Store::GOG;
              record.store_utf8 = "GOG";
              record._status.installed = true;

              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"GameName", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                record.names.normal = SK_WideCharToUTF8(szData);

              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"path", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                record.install_dir = szData;

              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"exeFile", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
              {
                app_record_s::launch_config_s lc;
                lc.id         = 0;
                lc.valid      = true;
                lc.executable = szData;
                lc.install_dir = record.install_dir;

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW (hKey, szSubKey, L"exe", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                  lc.executable_path = szData;

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW (hKey, szSubKey, L"workingDir", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                  lc.working_dir = szData;

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW (hKey, szSubKey, L"launchParam", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                  lc.launch_options = szData;

                record.launch_configs.emplace (0, lc);

                record.specialk.profile_dir = lc.executable;
                record.specialk.injection.injection.type = InjectionType::Global;

                std::pair <std::string, app_record_s>
                  GOG(record.names.normal, record);

                apps->emplace_back(GOG);

                dwRead++;
              }
            }
          }
        }
      }
    }

    RegCloseKey (hKey);

    // If an item was read, see if we can detect GOG Galaxy as well
    if (dwRead > 0)
    {
      if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\GOG.com\GalaxyClient\)", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
      {
        extern std::wstring GOGGalaxy_Path;
        extern std::wstring GOGGalaxy_Folder;
        extern         bool GOGGalaxy_Installed;

        dwSize = sizeof(szData) / sizeof(WCHAR);
        if (RegGetValueW (hKey, L"paths", L"client", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
        {
          GOGGalaxy_Folder = szData;

          dwSize = sizeof(szData) / sizeof(WCHAR);
          if (RegGetValueW (hKey, NULL, L"clientExecutable", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
          {
            GOGGalaxy_Path = SK_FormatStringW(LR"(%ws\%ws)", GOGGalaxy_Folder.c_str(), szData);

            if (PathFileExistsW(GOGGalaxy_Path.c_str()))
              GOGGalaxy_Installed = true;
          }
        }

        RegCloseKey (hKey);

        // Galaxy User ID
        SKIF_GOG_UpdateGalaxyUserID ( );
      }
    }
  }
}

// Retrieves the Galaxy User ID
void
SKIF_GOG_UpdateGalaxyUserID (void)
{
  extern std::wstring GOGGalaxy_UserID;

  GOGGalaxy_UserID = L""; // Reset it

  HKEY  hKey;
  DWORD dwSize = 0;
  WCHAR szData[MAX_PATH] = { };

  dwSize = sizeof(szData) / sizeof(WCHAR);
  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\GOG.com\Galaxy\settings\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {
    if (RegGetValueW (hKey, NULL, L"userId", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
      GOGGalaxy_UserID = szData;

    RegCloseKey (hKey);
  }
}

bool
SKIF_GOG_hasInstalledGamesChanged (void)
{
  static DWORD dwLastSignalCheck = 0;

  bool signal = false;
  if (SKIF_Util_timeGetTime ( ) > dwLastSignalCheck + 1000) // Tigher checks as we're waiting for both new subkeys as well as changed values
  {
    static SKIF_RegistryWatch
      appWatch ( HKEY_LOCAL_MACHINE,
                   LR"(SOFTWARE\GOG.com\Games)",
                     L"GOGInstallNotify", TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, true, true);
  
    signal            = appWatch.isSignaled   ( );
    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}

bool
SKIF_GOG_hasGalaxySettingsChanged (void)
{
  static DWORD dwLastSignalCheck = 0;

  bool signal = false;
  if (SKIF_Util_timeGetTime ( ) > dwLastSignalCheck + 5000)
  {
    static SKIF_RegistryWatch
      appWatch ( HKEY_CURRENT_USER,
                   LR"(SOFTWARE\GOG.com\Galaxy\settings)",
                     L"GOGGalaxyNotify", TRUE, REG_NOTIFY_CHANGE_LAST_SET, false);
  
    signal            = appWatch.isSignaled   ( );
    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}

#include <stores/gog/gog_library.h>
#include <wtypes.h>
#include <fstream>
#include <filesystem>
#include <utility/registry.h>
#include <nlohmann/json.hpp>


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
          HKEY hSubKey = nullptr;
          
          if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, SK_FormatStringW (LR"(SOFTWARE\GOG.com\Games\%ws)", szSubKey).c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hSubKey) == ERROR_SUCCESS)
          {
            dwSize = sizeof(szData) / sizeof(WCHAR);

            if (RegGetValueW (hSubKey, NULL, L"dependsOn", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS &&
              wcslen(szData) == 0) // Only handles items without a dependency (skips DLCs)
            {
              dwSize = sizeof(szData) / sizeof(WCHAR);

              if (RegGetValueW (hSubKey, NULL, L"GameID", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
              {
                int appid = _wtoi(szData);
                app_record_s record(appid);

                record.store      = app_record_s::Store::GOG;
                record.store_utf8 = "GOG";
                record._status.installed = true;

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW (hSubKey, NULL, L"GameName", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                {
                  record.names.normal   = SK_WideCharToUTF8(szData);
                  record.names.original = record.names.normal;
                }

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW (hSubKey, NULL, L"path", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                  record.install_dir = szData;

                record.install_dir   = std::filesystem::path (record.install_dir).lexically_normal();

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW (hSubKey, NULL, L"exeFile", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                {
                  app_record_s::launch_config_s lc;
                  lc.id         = 0;
                  lc.valid      = 1;
                  lc.executable = szData;
                  lc.install_dir = record.install_dir;

                  dwSize = sizeof(szData) / sizeof(WCHAR);
                  if (RegGetValueW (hSubKey, NULL, L"exe", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                    lc.executable_path = szData;

                  dwSize = sizeof(szData) / sizeof(WCHAR);
                  if (RegGetValueW (hSubKey, NULL, L"workingDir", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                    lc.working_dir = szData;

                  dwSize = sizeof(szData) / sizeof(WCHAR);
                  if (RegGetValueW (hSubKey, NULL, L"launchParam", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                    lc.launch_options = szData;

                  record.launch_configs.emplace (0, lc);

                  record.specialk.profile_dir              = lc.executable;
                  record.specialk.profile_dir_utf8         = SK_WideCharToUTF8(record.specialk.profile_dir);
                  record.specialk.injection.injection.type = InjectionType::Global;

                  std::pair <std::string, app_record_s>
                    GOG(record.names.normal, record);

                  apps->emplace_back(GOG);

                  dwRead++;
                }
              }
            }

            RegCloseKey (hSubKey);
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
                     L"GOGInstallNotify", TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, UITab_None, true); // UITab_Library
  
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
                     L"GOGGalaxyNotify", TRUE, REG_NOTIFY_CHANGE_LAST_SET, UITab_None);
  
    signal            = appWatch.isSignaled   ( );
    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}

void
SKIF_GOG_IdentifyAssetPCGW (uint32_t app_id)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\GOG\%i\)", _path_cache.specialk_userdata, app_id);

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            targetAssetPath, ec))
        std::filesystem::create_directories (targetAssetPath, ec);

  // Do not load a high-res copy if low-res covers are being used,
  //   as in those scenarios we prefer to load the local GOG Galaxy cover
  if (! _registry._UseLowResCovers || _registry._UseLowResCoversHiDPIBypass)
  {

    // We intend to download PCGW's cover
    // https://www.pcgamingwiki.com/w/api.php?action=cargoquery&format=json&tables=Infobox_game&fields=Cover_URL&where=GOGcom_ID+HOLDS+'2099051765'
    // https://www.pcgamingwiki.com/w/api.php?action=cargoquery&format=json&tables=Infobox_game&fields=Cover_URL&where=GOGcom_ID%20HOLDS%20%272099051765%27
    std::wstring url  = L"https://www.pcgamingwiki.com/w/api.php?action=cargoquery&format=json&tables=Infobox_game&fields=Cover_URL&where=GOGcom_ID+HOLDS+'";
                  url += std::to_wstring (app_id);
                  url += L"'";

    // If PCGW cover has not been downloaded
    if (! PathFileExistsW ((targetAssetPath + L"cover-pcgw.png").c_str()))
    {
      PLOG_DEBUG << "Downloading PCGW json: " << url;

      SKIF_Util_GetWebResource (url, (targetAssetPath + L"pcgw.json"));

      try
      {
        std::ifstream fileJson(targetAssetPath + L"pcgw.json");
        nlohmann::json jf = nlohmann::json::parse(fileJson, nullptr, false);
        fileJson.close();

        if (jf.is_discarded ( ))
        {
          PLOG_ERROR << "Could not read PCGW JSON!";
        }

        else
        {
          if (jf["errors"].is_array())
          {
            PLOG_ERROR << "Could not read PCGW JSON!";
          }

          else
          {
            for (auto& image : jf["cargoquery"][0]["title"]["Cover URL"])
            {
              // Convert the URL value to a regular string
              std::string assetUrl = image; // will throw exception if value does not exist

              PLOG_DEBUG << "Downloading cover asset: " << assetUrl;

              SKIF_Util_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"cover-pcgw.png");
            }
          }
        }
      }
      catch (const std::exception&)
      {
        PLOG_ERROR << "Error parsing PCGW JSON!";
      }

      // Delete the JSON file when we are done
      if (_registry.iLogging < 5)
        DeleteFile ((targetAssetPath + L"pcgw.json").c_str());
    }
  }
}
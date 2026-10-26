
#include <stores/gog/gog_library.h>
#include <wtypes.h>
#include <fstream>
#include <filesystem>
#include <utility/registry.h>
#include <nlohmann/json.hpp>
#include <utility/games.h>
#include <SKIF.h>
#include <utility/skif_imgui.h>
#include <fonts/fa_621.h>
#include <utility/utility.h>


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
SKIF_GOG_GetInstalledAppIDs (app_list_t *apps)
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
                app_record_gog_s record(appid);

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

                  std::string entry_name = record.names.normal; // Copied first: the record is moved from below

                  app_entry_t
                    GOG (std::move (entry_name), std::make_unique <app_record_gog_s> (std::move (record)));

                  apps->emplace_back (std::move (GOG));

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
    std::wstring url  = L"https://specialk.aemony.workers.dev/?p=gog&id=";
                 url += std::to_wstring (app_id);

    // If PCGW cover has not been downloaded
    if (! PathFileExistsW ((targetAssetPath + L"cover-pcgw.png").c_str()))
    {
      std::string response;

      PLOG_DEBUG << "Downloading PCGW json: " << url;

      SKIF_Util_GetWebResource (url, L"", L"GET", L"", "", L"", &response);

      try
      {
        nlohmann::json jf = nlohmann::json::parse (response);

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


#pragma region Launching

bool
app_record_gog_s::PrefersInstantLaunch (bool fromGameMenu, launch_config_s* config) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  extern bool GOGGalaxy_Installed;

  UNREFERENCED_PARAMETER (config);

  // Fallback for GOG games if the Galaxy client is not installed
  if (! GOGGalaxy_Installed)
    return true;

  return (! fromGameMenu && UsesInstantPlay (GetInstantPlayDefault ( )));
}

// Launch GOG Galaxy game (default for GOG games since 2024-01-05), unless an instant launch was requested
void
app_record_gog_s::Launch (const launch_context_s& ctx)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (ctx.instant)
  {
    LaunchInstant (ctx);
    return;
  }

  PLOG_VERBOSE << "Performing a GOG Galaxy launch...";

  extern std::wstring GOGGalaxy_Path;
  extern std::wstring GOGGalaxy_Folder;

  // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

  std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", id, install_dir.c_str());

  //SKIF_Util_OpenURI (GOGGalaxy_Path, SW_SHOWDEFAULT, L"OPEN", launchOptions.c_str());
  if (SKIF_Util_CreateProcess (GOGGalaxy_Path, launchOptions.c_str(), L""))
  {
    std::scoped_lock app_lock (_games.GetMutex());

    // Don't check the running state for at least 7.5 seconds
    _status.dwTimeDelayChecks = ctx.timeNow + 7500;
    _status.running           = true;

    SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal + " (Galaxy)"), GOGGalaxy_Path, launchOptions, GOGGalaxy_Folder, ctx.config->getExecutableFullPath(), (! ctx.localInjection && ctx.usingSK));
  }
}

#pragma endregion

#pragma region Library UI

bool
app_record_gog_s::GetInstantPlayDefault (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return _registry.bInstantPlayGOG;
}

std::wstring
app_record_gog_s::GetStoreAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, id);
}

#pragma endregion

#pragma region Context Menu

void
app_record_gog_s::DrawWebsiteMenuItems (void)
{
  if (SKIF_ImGui_MenuItemEx2 ("GOG Database", ICON_FA_DATABASE, ImColor(155, 89, 182, 255)))
    SKIF_Util_OpenURI (L"https://www.gogdb.org/product/" + std::to_wstring (id));

  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("https://www.gogdb.org/product/%lu", id));
}

#pragma endregion

#pragma region Artwork

std::wstring
app_record_gog_s::ResolveArtwork (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  std::wstring SKIFCustomPath = GetStoreAssetDirectory ( ) + ((type == LibraryTexture::Cover) ? L"cover" : L"icon");
  std::wstring load_str       = FindCustomArtwork (type);

  isCustom = (! load_str.empty ( ));

  if (! isCustom)
  {
    if      (type == LibraryTexture::Icon &&
             SKIF_Util_SaveExtractExeIcon (launch_configs[0].getExecutableFullPath ( ), SKIFCustomPath + L"-original.png"))
      load_str =             SKIFCustomPath + L"-original.png";
    else if (type == LibraryTexture::Icon)
    {
      isManaged = false; // GOG default icons are not managed
      load_str =             source;
    }

    else if (type == LibraryTexture::Cover)
    {
      // Load PCGW cover
      if (_registry.bPCGWCoversGOG &&
          PathFileExistsW ((SKIFCustomPath + L"-pcgw.png").c_str()))
      {
        load_str =          SKIFCustomPath + L"-pcgw.png";

        isManaged = true; // PCGW covers are managed
      }

      // Load GOG Galaxy cover
      else {
        isManaged = false; // GOG covers are not managed

        extern std::wstring GOGGalaxy_UserID;
        load_str = SK_FormatStringW (LR"(C:\ProgramData\GOG.com\Galaxy\webcache\%ws\gog\%i\)", GOGGalaxy_UserID.c_str(), id);

        HANDLE hFind        = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATA ffd = { };

        hFind =
          FindFirstFileExW ((load_str + source).c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);
        //FindFirstFile((load_str + source).c_str(), &ffd);

        if (INVALID_HANDLE_VALUE != hFind)
        {
          load_str += ffd.cFileName;
          FindClose(hFind);
        }
      }
    }
  }

  return load_str;
}

// Cover worker thread: GOG Galaxy's web cache cover (pattern), and the PCGamingWiki cover if enabled
std::wstring
app_record_gog_s::PrepareCoverSource (bool queryPCGW)
{
  std::wstring load_str;

  load_str = L"*_glx_vertical_cover.webp"; // Fallback

  // Decided on the main thread (SKIF_LibraryArtwork::StartCoverWorker), which also marks tex_cover.queriedPCGW
  if (queryPCGW)
    SKIF_GOG_IdentifyAssetPCGW (id);

  return load_str;
}

bool
app_record_gog_s::WantsPCGWCover (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return _registry.bPCGWCoversGOG;
}

// We may also store a PCGW cover that we must reset
bool
app_record_gog_s::DeleteStoreCoverFiles (const std::wstring& assetDir)
{
  tex_cover.queriedPCGW = false;

  return DeleteFile ((assetDir + L"cover-pcgw.png").c_str());
}

bool
app_record_gog_s::CoverNeedsClientSignIn (void) const
{
  extern std::wstring GOGGalaxy_UserID;

  return GOGGalaxy_UserID.empty();
}

#pragma endregion

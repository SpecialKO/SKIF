
#include <stores/egs/egs_library.h>
#include <wtypes.h>
#include <fstream>
#include <filesystem>

#include <comdef.h>
#include <process.h>

#include <gsl/gsl>

/*
EGS registry / folder struture


Root Key:           HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Epic Games\EpicGamesLauncher\AppDataPath
Root Folder:        C:\ProgramData\Epic\EpicGamesLauncher\Data\
Manifest Folder:    C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests

Each game is stored in a separate .item file, named after the InstallationGuid of the install.
Each file have a bunch of values with detailed data of the game, its location, and launch options.

Parameters and the data they contain:

    InstallLocation         -- Full path of the install folder
    LaunchExecutable        -- Filename of executable
    AppCategories           -- Categories for the app ("games" indicates a game)
    DisplayName             -- Title of the app
    InstallationGuid        -- Installation guid, same as the file name

    CatalogNamespace        -- Used to construct the launch command
    CatalogItemId           -- Used to construct the launch command
    AppName                 -- Used to construct the launch command

There are more values, but they aren't listed here.

EGS Custom Default Launch Option:
To launch a game using the user's customized launch option in the client,
it's enough to launch the game through EGS like the start menu shortcuts does, like this:

    com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

*/

std::wstring SKIF_EGS_AppDataPath;

void
SKIF_EGS_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
{
  HKEY hKey;
  DWORD dwSize;
  WCHAR szData[MAX_PATH];
  bool registrySuccess = false;

  // See if we can retrieve the launcher's appdata path from registry
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Epic Games\EpicGamesLauncher\)", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
  {
    dwSize = sizeof(szData) / sizeof(WCHAR);
    if (RegGetValueW(hKey, NULL, L"AppDataPath", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
    {
      SKIF_EGS_AppDataPath = szData; // C:\ProgramData\Epic\EpicGamesLauncher\Data
      SKIF_EGS_AppDataPath += LR"(\Manifests\)";

      registrySuccess = true;

      RegCloseKey(hKey);
    }
  }

  // Fallback: If the registry value does not exist (which happens surprisingly often) assume the default path is used
  if (! registrySuccess)
    SKIF_EGS_AppDataPath = LR"(C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests\)";

  // Abort if the folder does not exist
  if (! PathFileExists (SKIF_EGS_AppDataPath.c_str()))
  {
    SKIF_EGS_AppDataPath = L"";
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(SKIF_EGS_AppDataPath))
  {
    if (entry.is_directory() == false &&
        entry.path().extension().string() == ".item" )
    {
      try {
        std::ifstream file(entry.path());
        nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
        file.close();

        // Skip if we're dealing with a broken manifest
        if (jf.is_discarded ( ))
          continue;

        // Skip if a launch executable does not exist (easiest way to filter out Borderlands 3's DLCs, I guess?)
        if (jf.at ("LaunchExecutable").get <std::string_view>().empty())
          continue;

        // Skip if the install location does not exist
        if (! PathFileExists (SK_UTF8ToWideChar (std::string (jf.at ("InstallLocation"))).c_str()))
          continue;

        bool isGame = false;

        for (auto& categories : jf["AppCategories"])
        {
          if (categories.get <std::string_view>()._Equal(R"(games)"))
            isGame = true;
        }

        if (isGame)
        {
          int appid = jf.at("InstallSize");

          // Set SKIF's internal "appid" to the installsize of the game.
          //   TODO: Replace with some other proper solution that ensures no hits with other platforms
          app_record_s record(appid);

          //record.install_dir.erase(std::find(record.install_dir.begin(), record.install_dir.end(), '\0'), record.install_dir.end());

          record.store                = "EGS";
          record.type                 = "Game";
          record._status.installed    = true;
          record.install_dir          = SK_UTF8ToWideChar (jf.at ("InstallLocation"));

          record.names.normal         = jf.at ("DisplayName");

          record.names.all_upper      = record.names.normal;
          std::for_each(record.names.all_upper.begin(), record.names.all_upper.end(), ::toupper);


          app_record_s::launch_config_s lc;
          lc.id                           = 0;
          lc.valid                        = true;
          lc.store                        = L"EGS";
          lc.executable                   = SK_UTF8ToWideChar(jf.at("LaunchExecutable")); // record.install_dir + L"\\" +
          lc.executable_path              = record.install_dir + L"\\" + lc.executable;
          std::replace(lc.executable_path.begin(), lc.executable_path.end(), '/', '\\'); // Replaces all / with \

          // Strip out the subfolders from the executable variable
          std::wstring
             substr = lc.executable;
          auto npos = substr.find_last_of(L"/\\");
          if (npos != std::wstring::npos)
            substr  = substr.substr(npos + 1);
          if (! substr.empty() )
            lc.executable = substr;

          lc.working_dir                  = record.install_dir;
          //lc.launch_options = SK_UTF8ToWideChar(app.at("LaunchCommand"));

          std::string CatalogNamespace    = jf.at("CatalogNamespace"),
                      CatalogItemId       = jf.at("CatalogItemId"),
                      AppName             = jf.at("AppName");

          // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
          lc.launch_options = SK_UTF8ToWideChar(CatalogNamespace + "%3A" + CatalogItemId + "%3A" + AppName);
          lc.launch_options.erase(std::find(lc.launch_options.begin(), lc.launch_options.end(), '\0'), lc.launch_options.end());

          record.launch_configs[0]    = lc;

          record.EGS_CatalogNamespace = CatalogNamespace;
          record.EGS_CatalogItemId    = CatalogItemId;
          record.EGS_AppName          = AppName;
          record.EGS_DisplayName      = record.names.normal;

          record.specialk.profile_dir = SK_UTF8ToWideChar (record.EGS_DisplayName);
          record.specialk.injection.injection.type = sk_install_state_s::Injection::Type::Global;

          // Strip invalid filename characters
          record.specialk.profile_dir = SKIF_Util_StripInvalidFilenameChars (record.specialk.profile_dir);
            
          std::pair <std::string, app_record_s>
            EGS(record.names.normal, record);

          apps->emplace_back(EGS);

          // Documents\My Mods\SpecialK\Profiles\AppCache\#EpicApps\<AppName>
          std::wstring AppCacheDir = SK_FormatStringW(LR"(%ws\Profiles\AppCache\#EpicApps\%ws)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(AppName).c_str());

          // Create necessary directories if they do not exist
          std::filesystem::create_directories (AppCacheDir);

          // Copy manifest to AppCache directory
          CopyFile (entry.path().c_str(), (AppCacheDir + LR"(\manifest.json)").c_str(), false);
        }
      }
      catch (const std::exception&)
      {

      }
    }
  }
}


void
SKIF_EGS_IdentifyAssetNew (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName)
{
  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\EGS\%ws\)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(AppName).c_str());
  std::filesystem::create_directories(targetAssetPath);

  // Download JSON for the offered games/edition/base
  if (! PathFileExists ((targetAssetPath + L"offer.json").c_str()))
  {
    // Can be retrieved by monitoring requests made by the storefront in a web browser of choice
    // Up to 2020-04: 6e7c4dd0177150eb9a47d624be221929582df8648e7ec271c821838ff4ee148e
    //  From 2020-04: 4bebe12f9eab12438766fb5971b0bc54422ba81954539f294ec23b0a29ff92ad
    std::wstring query = SK_FormatStringW (
      LR"(https://graphql.epicgames.com/graphql?operationName=searchStoreQuery&variables={"country":"US", "category": "games/edition/base", "namespace": "%ws"}&extensions={"persistedQuery":{"version":1,"sha256Hash":"4bebe12f9eab12438766fb5971b0bc54422ba81954539f294ec23b0a29ff92ad"}})",
      SK_UTF8ToWideChar(CatalogNamespace).c_str()
    );

    SKIF_Util_GetWebResource (query, targetAssetPath + L"offer.json");
  }

  try
  {
    std::ifstream fileOffer(targetAssetPath + L"offer.json");
    nlohmann::json jf = nlohmann::json::parse(fileOffer, nullptr, false);
    fileOffer.close();

    if (jf.is_discarded ( ))
    {
      DeleteFile ((targetAssetPath + L"offer.json").c_str()); // Something went wrong -- delete the file so a new attempt is performed next time
    }

    else
    {
      if (jf["errors"].is_array())
      {
        DeleteFile ((targetAssetPath + L"offer.json").c_str()); // Something went wrong -- delete the file so a new attempt is performed next time
      }

      else
      {
        for (auto& image : jf["data"]["Catalog"]["searchStore"]["elements"][0]["keyImages"])
        {
          if (image["type"].get <std::string_view>()._Equal(R"(OfferImageTall)")) //.dump() == R"("OfferImageTall")")
          {
            // Convert the URL value to a regular string
            std::string assetUrl = image["url"]; // will throw exception if "url" does not exist

            // Download a downscaled copy of the cover
            extern bool            SKIF_bLowBandwidthMode;

            if (SKIF_bLowBandwidthMode)
              assetUrl += "?h=900&w=600&resize=1"; // TAKES TOO LONG! :D

            SKIF_Util_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"OfferImageTall.jpg");
          }
        }
      }
    }
  }
  catch (const std::exception&)
  {

  }
}
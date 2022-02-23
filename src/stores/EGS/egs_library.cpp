
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

skif_egs_directory_watch_s skif_egs_dir_watch;

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
      std::ifstream file(entry.path());
      nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
      file.close();

      // Skip if we're dealing with a broken manifest
      if (jf.is_discarded ( ))
        continue;

      // Skip if the install location does not exist
      if (! PathFileExists (SK_UTF8ToWideChar (std::string (jf.at ("InstallLocation"))).c_str()))
        continue;

      bool isGame = false;

      try {
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
          extern std::wstring SKIF_StripInvalidFilenameChars (std::wstring);
          record.specialk.profile_dir = SKIF_StripInvalidFilenameChars (record.specialk.profile_dir);
            
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

/* Old -- not in use any longer
void SKIF_EGS_GetCatalogNamespaces (bool forceUpdate)
{
  if (SKIF_EGS_JSON_CatalogNamespaces == NULL || forceUpdate)
  {
    std::wstring root = SK_FormatStringW(LR"(%ws\Assets\EGS\)", path_cache.specialk_userdata.path);
    std::wstring path = root + LR"(cache\namespaces.json)";

    // Create necessary directories if they do not exist
    std::filesystem::create_directories(root + LR"(cache\offers)");

    // Download namespaces.json if it does not exist or if we're forcing an update
    if (! PathFileExists(path.c_str()) || forceUpdate)
    {
      SKIF_GetWebResource (L"https://raw.githubusercontent.com/srdrabx/offers-tracker/master/database/namespaces.json", path);
    }
    else {
      WIN32_FILE_ATTRIBUTE_DATA fileAttributes;

      if (GetFileAttributesEx (path.c_str(),    GetFileExInfoStandard, &fileAttributes))
      {
        FILETIME ftSystemTime, ftAdjustedFileTime;
        SYSTEMTIME systemTime;
        GetSystemTime (&systemTime);

        if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
        {
          ULARGE_INTEGER uintLastWriteTime;

          // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
          uintLastWriteTime.HighPart        = fileAttributes.ftLastWriteTime.dwHighDateTime;
          uintLastWriteTime.LowPart         = fileAttributes.ftLastWriteTime.dwLowDateTime;

          // Perform 64-bit arithmetic to add 7 days to last modified timestamp
          uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(7 * 24 * 60 * 60 * 1.0e+7);

          // Copy the results to an FILETIME struct
          ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
          ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

          // Compare with system time, and if system time is later (1), then update the local cache
          if (CompareFileTime(&ftSystemTime, &ftAdjustedFileTime) == 1)
          {
            SKIF_GetWebResource (L"https://raw.githubusercontent.com/srdrabx/offers-tracker/master/database/namespaces.json", path);
          }
        }
      }
    }
    
    std::ifstream file(path);
    nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
    file.close();

    if (jf.is_discarded ( ))
      DeleteFile (path.c_str()); // Something went wrong -- delete the file so a new attempt is performed on next launch
    else
      SKIF_EGS_JSON_CatalogNamespaces = jf;
  }
}

void SKIF_EGS_IdentifyAsset (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName)
{
  if (SKIF_EGS_JSON_CatalogNamespaces != NULL)
  {
    //
    //OutputDebugString(L"Namespace: ");
    //OutputDebugString(SK_UTF8ToWideChar(CatalogNamespace).c_str());
    //OutputDebugString(L"\n");
    //
    //OutputDebugString(L"JSON: ");
    //OutputDebugString(SK_UTF8ToWideChar(SKIF_EGS_JSON_CatalogNamespaces[CatalogNamespace].dump()).c_str());
    //OutputDebugString(L"\n");
    //

    // Force an update of the file if it lacks the expected namespace we're looking for
    if (SKIF_EGS_JSON_CatalogNamespaces[CatalogNamespace].is_null ())
      SKIF_EGS_GetCatalogNamespaces (true);

    for (auto& offer : SKIF_EGS_JSON_CatalogNamespaces[CatalogNamespace])
    {
      std::wstring offerID = SK_UTF8ToWideChar(offer.dump());
      offerID.erase(std::remove(offerID.begin(), offerID.end(), '"'), offerID.end());

      //OutputDebugString(L"Offer: ");
      //OutputDebugString(offerID.c_str());
      //OutputDebugString(L"\n");

      std::wstring targetPath      = SK_FormatStringW(LR"(%ws\Assets\EGS\cache\offers\%ws.json)", path_cache.specialk_userdata.path, offerID.c_str());
      std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\EGS\%ws\)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(AppName).c_str());

      // Download offer JSON
      if (! PathFileExists (targetPath.c_str()))
      {
        SKIF_GetWebResource (SK_FormatStringW (L"https://raw.githubusercontent.com/srdrabx/offers-tracker/master/database/offers/%ws.json", offerID.c_str()), targetPath);
      }

      std::ifstream fileOffer(targetPath);
      nlohmann::json jf = nlohmann::json::parse(fileOffer, nullptr, false);
      fileOffer.close();

      if (jf.is_discarded ( ))
      {
        DeleteFile(targetPath.c_str()); // Something went wrong -- delete the file so a new attempt is performed on next launch
      }
      else
      {
        bool isGame  = false;
        //bool isTitle = false;

        for (auto& category : jf["categories"])
        {
          if (category["path"].dump() == R"("games")")
            isGame = true;
        }

        // Not always the same -- Horizon Zero Dawn has a :tm: included in the offer title which is missing in the local DisplayName manifest.
        //if (jf["title"].dump() == "\"" + DisplayName + "\"")
        //{
        //  isTitle = true;
        //}
        //

        if (isGame) // && isTitle)
        {
          std::filesystem::create_directories(targetAssetPath);

          bool assetDownloaded = false;

          for (auto& image : jf["keyImages"])
          {
            if (image["type"].dump() == R"("OfferImageTall")" && ! PathFileExists ((targetAssetPath + L"OfferImageTall.jpg").c_str()))
            {
              //OutputDebugString(L"OfferImageTall: ");
              //OutputDebugString(SK_UTF8ToWideChar(image["url"]).c_str());
              //OutputDebugString(L"\n");

              // Convert the URL value to a regular string
              std::string assetUrl = image["url"];

              // Download a downscaled copy of the cover
              //assetUrl += "?h=900&w=600&resize=1"; // TAKES TOO LONG! :D

              SKIF_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"OfferImageTall.jpg");

              assetDownloaded = true;
            }

            //if (image["type"].dump() == R"("ProductLogo")" && !PathFileExists((targetAssetPath + L"ProductLogo.jpg").c_str()))
            //{
            //  OutputDebugString(L"ProductLogo: ");
            //  OutputDebugString(SK_UTF8ToWideChar(image["url"]).c_str());
            //  OutputDebugString(L"\n");
            //
            //  SKIF_GetWebResource (SK_UTF8ToWideChar(image["url"]), targetAssetPath + L"ProductLogo.jpg");
            //}
          }

          // We've retrieved the proper assets -- break the main loop
          if (assetDownloaded)
            break;
        }
      }
    }
  }
}
*/

void SKIF_EGS_IdentifyAssetNew (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName)
{
  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\EGS\%ws\)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(AppName).c_str());
  std::filesystem::create_directories(targetAssetPath);

  // Download JSON for the offered games/edition/base
  if (! PathFileExists ((targetAssetPath + L"offer.json").c_str()))
  {
    std::wstring query = SK_FormatStringW (
      LR"(https://graphql.epicgames.com/graphql?operationName=searchStoreQuery&variables={"country":"US", "category": "games/edition/base", "namespace": "%ws"}&extensions={"persistedQuery":{"version":1,"sha256Hash":"6e7c4dd0177150eb9a47d624be221929582df8648e7ec271c821838ff4ee148e"}})",
      SK_UTF8ToWideChar(CatalogNamespace).c_str()
    );

    SKIF_GetWebResource (query, targetAssetPath + L"offer.json");
  }
  
  std::ifstream fileOffer(targetAssetPath + L"offer.json");
  nlohmann::json jf = nlohmann::json::parse(fileOffer, nullptr, false);
  fileOffer.close();

  if (jf.is_discarded ( ))
  {
    DeleteFile ((targetAssetPath + L"offer.json").c_str()); // Something went wrong -- delete the file so a new attempt is performed next time
  }
  else
  {
    try
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

          SKIF_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"OfferImageTall.jpg");
        }
      }
    }
    catch (const std::exception&)
    {

    }
  }
}


bool
skif_egs_directory_watch_s::isSignaled (void)
{
  bool bRet = false;

  if (hChangeNotification != INVALID_HANDLE_VALUE)
  {
    bRet =
      ( WAIT_OBJECT_0 ==
          WaitForSingleObject (hChangeNotification, 0) );

    if (bRet)
    {
      FindNextChangeNotification (
        hChangeNotification
      );
    }
  }
  else {
    if (! SKIF_EGS_AppDataPath.empty())
    {
      hChangeNotification =
        FindFirstChangeNotificationW (
          SKIF_EGS_AppDataPath.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME
        );

      if (hChangeNotification != INVALID_HANDLE_VALUE)
      {
        FindNextChangeNotification (
          hChangeNotification
        );
      }
    }
  }

  return bRet;
}

skif_egs_directory_watch_s::~skif_egs_directory_watch_s(void)
{
  if (      hChangeNotification != INVALID_HANDLE_VALUE)
    FindCloseChangeNotification (hChangeNotification);
}
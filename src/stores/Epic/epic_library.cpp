
#include <stores/epic/epic_library.h>
#include <wtypes.h>
#include <fstream>
#include <filesystem>

#include <comdef.h>
#include <process.h>

#include <utility/registry.h>
#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>

/*
Epic registry / folder struture


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

Epic Custom Default Launch Option:
To launch a game using the user's customized launch option in the client,
it's enough to launch the game through Epic like the start menu shortcuts does, like this:

    com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

*/


bool
SKIF_EpicCollection::isSignaled (void)
{
  RefreshInstallState ( );

  return this->watch.isSignaled ( );
}

void
SKIF_EpicCollection::RefreshInstallState (void)
{
  if (this->appdatapath.empty())
  {
    PLOG_INFO << "Detecting Epic AppData path...";

    HKEY  hKey;
    DWORD dwSize = 0;
    WCHAR szData[MAX_PATH] = { };
    struct       Candidate { int version; std::wstring path_root; std::wstring path_child; };
    std::vector <Candidate>
                 candidates;

    // See if we can retrieve the launcher's appdata path from registry
    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Epic Games\EpicGamesLauncher\)", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
    {
      dwSize = sizeof(szData) / sizeof(WCHAR);
      if (RegGetValueW (hKey, NULL, L"AppDataPath", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
      {
        std::wstring appdata_path;

        // One of either:
        //   %LOCALAPPDATA%\com.epicgames.eglv2
        //   %PROGRAMDATA%\Epic\EpicGamesLauncher\Data
        appdata_path = szData;
        RegCloseKey (hKey);

        // User-supplied base path
        if (! appdata_path.empty ( ))
        {
          candidates = {
            { 2, appdata_path,  LR"(\installations.jsonl)" },
            { 1, appdata_path + LR"(\Manifests)",   LR"()" },
          };
        }
      }
    }

    // Fallback: If the registry value does not exist (which happens surprisingly often) assume the default path is used
    if (candidates.empty())
    {
      PLOG_WARNING << "Failed to read Epic manifest location from the registry!";

      static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

      candidates = {
        { 2, SK_FormatStringW (LR"(%ws\com.epicgames.eglv2)",                   _path_cache.app_data_local.path), LR"(\installations.jsonl)" },
        { 1, SK_FormatStringW (LR"(%ws\Epic\EpicGamesLauncher\Data\Manifests)", _path_cache.program_data.path  ),                     LR"()" },
      };
    }

    // Probe v2 first, then v1
    for (const auto& c : candidates)
    {
      if (PathFileExists ((c.path_root + c.path_child).c_str ()))
      {
        this->version     = c.version;
        this->appdatapath = c.path_root;

        watch.reset();
        watch.isSignaled (c.path_root, UITab_Library);

        PLOG_VERBOSE << "Epic v" << c.version << " AppData path detected at : " << c.path_root;
        break;
      }
    }
  }
}

void
SKIF_EpicCollection::GetInstalledAppIDs (app_list_t *apps) const
{
  if (this->version == 0)
  {
    PLOG_WARNING << "Could not find Epic installed on the system!";
    return;
  }

  PLOG_INFO << "Detecting Epic games...";

  if (this->version == 2)
    this->FindInstalledGames2 (apps);

  if (this->version == 1)
    this->FindInstalledGames1 (apps);
}

bool
SKIF_EpicCollection::FindInstalledGames2 (app_list_t* apps) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  bool ret = false;

  std::ifstream
        file (this->appdatapath + LR"(\installations.jsonl)");
  if (! file.is_open ())
  {
    PLOG_ERROR << "Failed to open " << (this->appdatapath + LR"(\installations.jsonl)");
    return ret;
  }

  nlohmann::json
      catalog_cache;
  std::ifstream
      catalog_cache_file (this->appdatapath + LR"(\cache\prod\catalog-cache.json)");
  if (catalog_cache_file.is_open())
  {
    catalog_cache = nlohmann::json::parse (catalog_cache_file, nullptr);
    catalog_cache_file.close();

    if (catalog_cache.is_discarded ( ) || ! catalog_cache.is_object ( ))
      PLOG_WARNING << "catalog_cache is discarded or not an object";
    else
      PLOG_VERBOSE << "Read catalog-cache.json successfully.";
  }

  std::string line;
  size_t      lineNo            = 0;
  size_t      expectedEntries   = 0;
  size_t      discoveredEntries = 0;
  std::unordered_map<std::string, size_t> indexById; // installationId -> slot in installs

  while (std::getline (file, line))
  {
    ++lineNo;

    // Strip trailing CR (Windows line endings) and skip blank lines
    if (! line.empty () && line.back () == '\r')
      line.pop_back ();
    if (line.empty ())
      continue;

    nlohmann::json
        jl = nlohmann::json::parse (line, nullptr, false);
    if (jl.is_discarded () || ! jl.is_object ())
    {
      PLOG_DEBUG << "Line " << lineNo << ": invalid JSON; skipping...";
      continue;
    }

    // Header line: {"version":1,"writtenAt":"...","entries":4}
    if (lineNo == 1 && jl.contains ("entries") && ! jl.contains ("installationId"))
    {
      expectedEntries = jl.value ("entries", 0);
      continue;
    }

    // Filter out incomplete installs
    if (! jl.value ("complete", false))
    {
      PLOG_DEBUG << "Line " << lineNo << ": 'complete' missing or false; skipping...";
      continue;
    }

    std::string CatalogNamespace    = jl.at ("namespace"),
                CatalogItemId       = jl.at ("itemId"),
                AppName             = jl.at ("artifactId"); // artifactId == appName

    std::wstring egsstore_manifest =
      SK_FormatStringW  (LR"(%ws\.egstore\%ws.manifest)",
      SK_UTF8ToWideChar (jl.at ("dir")).c_str(),
      SK_UTF8ToWideChar (jl.at ("installationId")).c_str()
    );
    SKIF_Util_ReplaceAllW (egsstore_manifest, LR"(\\)", LR"(\)");

    // Skip if a corresponding manifest file does not reside in the expected .egstore folder
    if (! PathFileExists (egsstore_manifest.c_str()))
    {
      PLOG_DEBUG << "Line " << lineNo << ": .egstore manifest file not found; skipping...";
      continue;
    }

    auto [it, inserted] = indexById.try_emplace (jl.at ("installationId"), lineNo);

    // Skip already processed installs
    if (! inserted)
    {
      PLOG_DEBUG << "Line " << lineNo << ": installationId already processed; skipping...";
      continue;
    }

    // Hash the AppName into a unique integer we use for internal tracking purposes
    std::hash <std::string> stoi_hasher;
    size_t int_hash = stoi_hasher (AppName);

    app_record_epic_s record(static_cast<uint32_t>(int_hash));

    //record.install_dir.erase(std::find(record.install_dir.begin(), record.install_dir.end(), '\0'), record.install_dir.end());

    record.store                = app_record_s::Store::Epic;
    record.store_utf8           = "Epic";
    record._status.installed    = true;
    record.install_dir          = SK_UTF8ToWideChar (jl.at ("dir"));
    record.install_dir          = std::filesystem::path (record.install_dir).lexically_normal();

    // Fallback
    if (catalog_cache.is_discarded ( ) || ! catalog_cache.is_object ( ))
    {
      record.names.normal       = jl.value ("launchExecutable", std::string());
      SKIF_Util_ReplaceAll       (record.names.normal, ".exe", "");
    }

    else {
      const nlohmann::json& item =
        catalog_cache.at ("items").at (CatalogItemId).at ("item");

      if (item.contains ("mainGameItem") && ! item["mainGameItem"].is_null ( ))
      {
        PLOG_DEBUG << "Line " << lineNo << ": item not a parent; skipping...";
        continue;
      }

      record.names.normal       = item.at ("title").get<std::string> ();

      for (const auto& img : item.at ("keyImages"))
      {
        if (img.at ("type") == "DieselGameBoxTall")
        {
          record.epic.gamebox_tall = img.at ("url").get<std::string> ();
          break;
        }
      }
    }

    record.names.original       = record.names.normal;

    app_record_s::launch_config_s lc;
    lc.id                       = 0;
    lc.valid                    = 1;
    lc.executable               = SK_UTF8ToWideChar (jl.at("launchExecutable")); // record.install_dir + L"\\" +
    lc.executable_path          = record.install_dir + LR"(\)" + lc.executable;
    lc.install_dir              = record.install_dir;
    std::replace(lc.executable_path.begin(), lc.executable_path.end(), '/', '\\'); // Replaces all / with \

    // Strip out the subfolders from the executable variable
    std::wstring
        substr = lc.executable;
    auto npos = substr.find_last_of(L"/\\");
    if (npos != std::wstring::npos)
      substr  = substr.substr(npos + 1);
    if (! substr.empty() )
      lc.executable = substr;

    lc.working_dir               = record.install_dir;
    //lc.launch_options = SK_UTF8ToWideChar(app.at("LaunchCommand"));

    // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
    lc.launch_options = SK_UTF8ToWideChar(CatalogNamespace + "%3A" + CatalogItemId + "%3A" + AppName);
    lc.launch_options.erase(std::find(lc.launch_options.begin(), lc.launch_options.end(), '\0'), lc.launch_options.end());

    record.launch_configs.emplace (0, lc);

    record.epic.catalog_namespace = CatalogNamespace;
    record.epic.catalog_item_id   = CatalogItemId;
    record.epic.name_app          = AppName;
    record.epic.name_display      = record.names.normal;

    record.specialk.injection.injection.type = InjectionType::Global;

    // Strip invalid filename characters
    record.specialk.profile_dir_utf8 = SKIF_Util_StripInvalidFilenameChars (record.epic.name_display);
    record.specialk.profile_dir      = SK_UTF8ToWideChar (record.specialk.profile_dir_utf8);

    app_entry_t
      Epic (record.names.normal, std::make_unique <app_record_epic_s> (record));

    discoveredEntries++;

    apps->emplace_back (std::move (Epic));
    ret = true;
  }

  PLOG_VERBOSE_IF(expectedEntries != 0 && expectedEntries != discoveredEntries) << "Header listed " << expectedEntries << "; only parsed " << discoveredEntries;

  return ret;
}

bool
SKIF_EpicCollection::FindInstalledGames1 (app_list_t* apps) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  bool ret = false;

  for (const auto& entry : std::filesystem::directory_iterator (this->appdatapath))
  {
    if (entry.is_directory()               == false    &&
        entry.path().extension().wstring() == L".item" )
    {
      try
      {
        PLOG_DEBUG << "Parsing " << entry.path();

        std::ifstream file(entry.path());
        nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
        file.close();

        // Skip if we're dealing with a broken manifest
        if (jf.is_discarded ( ))
          continue;

        // Skip if a launch executable does not exist (easiest way to filter out Borderlands 3's DLCs, I guess?)
        if (jf.at ("LaunchExecutable").get <std::string_view>().empty())
          continue;

        bool isGame = false;

        for (auto& categories : jf["AppCategories"])
        {
          if (categories.get <std::string_view>()._Equal(R"(games)"))
            isGame = true;
        }

        if (isGame)
        {
          std::wstring egsstore_manifest =
            SK_FormatStringW  (LR"(%ws\%ws.manifest)",
            SK_UTF8ToWideChar (jf.at ("ManifestLocation")).c_str(),
            SK_UTF8ToWideChar (jf.at ("InstallationGuid")).c_str()
          );

          // Skip if a corresponding manifest file does not reside in the expected .egstore folder
          if (! PathFileExists (egsstore_manifest.c_str()))
            continue;

          std::string CatalogNamespace    = jf.at("CatalogNamespace"),
                      CatalogItemId       = jf.at("CatalogItemId"),
                      AppName             = jf.at("AppName");

          // Hash the AppName into a unique integer we use for internal tracking purposes
          std::hash <std::string> stoi_hasher;
          size_t int_hash = stoi_hasher (AppName);

          app_record_epic_s record(static_cast<uint32_t>(int_hash));

          //record.install_dir.erase(std::find(record.install_dir.begin(), record.install_dir.end(), '\0'), record.install_dir.end());

          record.store                = app_record_s::Store::Epic;
          record.store_utf8           = "Epic";
          record._status.installed    = true;
          record.install_dir          = SK_UTF8ToWideChar (jf.at ("InstallLocation"));
          record.install_dir          = std::filesystem::path (record.install_dir).lexically_normal();
          record.names.normal         = jf.at ("DisplayName");
          record.names.original       = record.names.normal;


          app_record_s::launch_config_s lc;
          lc.id                       = 0;
          lc.valid                    = 1;
          lc.executable               = SK_UTF8ToWideChar(jf.at("LaunchExecutable")); // record.install_dir + L"\\" +
          lc.executable_path          = record.install_dir + LR"(\)" + lc.executable;
          lc.install_dir              = record.install_dir;
          std::replace(lc.executable_path.begin(), lc.executable_path.end(), '/', '\\'); // Replaces all / with \

          // Strip out the subfolders from the executable variable
          std::wstring
             substr = lc.executable;
          auto npos = substr.find_last_of(L"/\\");
          if (npos != std::wstring::npos)
            substr  = substr.substr(npos + 1);
          if (! substr.empty() )
            lc.executable = substr;

          lc.working_dir               = record.install_dir;
          //lc.launch_options = SK_UTF8ToWideChar(app.at("LaunchCommand"));

          // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
          lc.launch_options = SK_UTF8ToWideChar(CatalogNamespace + "%3A" + CatalogItemId + "%3A" + AppName);
          lc.launch_options.erase(std::find(lc.launch_options.begin(), lc.launch_options.end(), '\0'), lc.launch_options.end());

          record.launch_configs.emplace (0, lc);

          record.epic.catalog_namespace = CatalogNamespace;
          record.epic.catalog_item_id   = CatalogItemId;
          record.epic.name_app          = AppName;
          record.epic.name_display      = record.names.normal;

          record.specialk.injection.injection.type = InjectionType::Global;

          // Strip invalid filename characters
          record.specialk.profile_dir_utf8 = SKIF_Util_StripInvalidFilenameChars (record.epic.name_display);
          record.specialk.profile_dir      = SK_UTF8ToWideChar (record.specialk.profile_dir_utf8);

          app_entry_t
            Epic (record.names.normal, std::make_unique <app_record_epic_s> (record));

          apps->emplace_back (std::move (Epic));
          ret = true;

          // Documents\My Mods\SpecialK\Profiles\AppCache\#EpicApps\<AppName>
          std::wstring AppCacheDir = SK_FormatStringW(LR"(%ws\Profiles\AppCache\#EpicApps\%ws)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(AppName).c_str());

          std::error_code ec;
          // Create any missing directories
          if (! std::filesystem::exists (            AppCacheDir, ec))
                std::filesystem::create_directories (AppCacheDir, ec);

          // Copy manifest to AppCache directory
          CopyFile (entry.path().c_str(), (AppCacheDir + LR"(\manifest.json)").c_str(), false);
        }
      }
      catch (const std::exception& e)
      {
        PLOG_ERROR << "Failed to parse manifest: " << entry.path() << ": " << e.what ();
      }
    }
  }

  return ret;
}

void
SKIF_EpicCollection::IdentifyAssetNew (const app_record_epic_s::epic_s& epic)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\Epic\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar (epic.name_app).c_str());

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            targetAssetPath, ec))
        std::filesystem::create_directories (targetAssetPath, ec);

  // Download JSON for the offered games/edition/base
  if (! PathFileExists ((targetAssetPath + L"offer.json").c_str()))
  {
    // The new sha256Hash be retrieved by monitoring requests made by the storefront in a web browser of choice
    //
    // MAIN LIMITATION:
    //    Relies on the product being published on the storefront, so covers for
    //      games removed from the storefront will not appear (e.g. >observer_)
    //
    // Up to 2020-04: 6e7c4dd0177150eb9a47d624be221929582df8648e7ec271c821838ff4ee148e
    //  From 2020-04: 4bebe12f9eab12438766fb5971b0bc54422ba81954539f294ec23b0a29ff92ad
    //  From 2023-xx: 7d58e12d9dd8cb14c84a3ff18d360bf9f0caa96bf218f2c5fda68ba88d68a437
    //
    // graphql. stopped working sometime in 2025, so use launcher.store. instead
    std::wstring query = SK_FormatStringW (
      LR"(https://launcher.store.epicgames.com/graphql?operationName=searchStoreQuery&variables={"country":"US", "category": "games/edition/base", "namespace": "%ws"}&extensions={"persistedQuery":{"version":1,"sha256Hash":"%ws"}})",
      SK_UTF8ToWideChar (epic.catalog_namespace).c_str(),
      L"7d58e12d9dd8cb14c84a3ff18d360bf9f0caa96bf218f2c5fda68ba88d68a437" // sha256Hash
    );

    PLOG_DEBUG << "Downloading platform JSON: " << query;

    // Epic's GraphQL API now seems to perform a basic User-Agent validation...
    SKIF_Util_GetWebResource (query, targetAssetPath + L"offer.json", L"GET", L"", "", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) EpicGamesLauncher");
  }

  std::wstring query = SK_FormatStringW (
    LR"(https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/public/assets/v2/platform/Windows/namespace/%ws/catalogItem/%ws/app/%ws/label/Live)",
    SK_UTF8ToWideChar (epic.catalog_namespace).c_str(),
    SK_UTF8ToWideChar (epic.catalog_item_id).c_str(),
    SK_UTF8ToWideChar (epic.name_app).c_str(),
    L"7d58e12d9dd8cb14c84a3ff18d360bf9f0caa96bf218f2c5fda68ba88d68a437" // sha256Hash
  );

  try
  {
    std::ifstream fileOffer(targetAssetPath + L"offer.json");
    nlohmann::json jf = nlohmann::json::parse(fileOffer, nullptr, false);
    fileOffer.close();

    if (jf.is_discarded ( ))
    {
      PLOG_ERROR << "Could not read platform JSON!";
    }

    else
    {
      if (jf["errors"].is_array())
      {
        PLOG_ERROR << "Could not read platform JSON!";
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

            if (_registry.bLowBandwidthMode)
              assetUrl += "?h=900&w=600&resize=1"; // TAKES TOO LONG! :D

            PLOG_DEBUG << "Downloading cover asset: " << assetUrl;

            SKIF_Util_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"cover-original.jpg");
          }
        }
      }
    }
  }
  catch (const std::exception&)
  {

  }

  // Delete the JSON file when we are done
  if (_registry.iLogging < 5)
    DeleteFile ((targetAssetPath + L"offer.json").c_str());
}


#pragma region Launching

// Launch Epic game (always through the Epic Games Launcher)
void
app_record_epic_s::Launch (const launch_context_s& ctx)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  PLOG_VERBOSE << "Performing an Epic launch...";

  // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

  std::wstring launchOptions = SK_FormatStringW(LR"(com.epicgames.launcher://apps/%ws?action=launch&silent=true)", ctx.config->getLaunchOptions().c_str());
  if (SKIF_Util_OpenURI (launchOptions) != 0)
  {
    std::scoped_lock app_lock (_games.GetMutex());

    // Don't check the running state for at least 7.5 seconds
    _status.dwTimeDelayChecks = ctx.timeNow + 7500;
    _status.running           = true;

    std::wstring iconPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\icon-original.ico)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(epic.name_app).c_str());

    SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal), L"", launchOptions, L"", iconPath, (! ctx.localInjection && ctx.usingSK));
  }
}

#pragma endregion

#pragma region Library UI

std::wstring
app_record_epic_s::GetStoreAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(epic.name_app).c_str());
}

std::vector <std::pair <std::string, std::string>>
app_record_epic_s::GetStoreDebugFields (void) const
{
  return {
    { "App Name",          epic.name_app          },
    { "Display Name",      epic.name_display      },
    { "Catalog Namespace", epic.catalog_namespace },
    { "Catalog Item ID",   epic.catalog_item_id   },
    { "DieselGameBoxTall", epic.gamebox_tall      }
  };
}

#pragma endregion

#pragma region Artwork

std::wstring
app_record_epic_s::ResolveArtwork (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged)
{
  UNREFERENCED_PARAMETER (source);
  UNREFERENCED_PARAMETER (isManaged);

  std::wstring EpicAssetPath  = GetStoreAssetDirectory ( );
  std::wstring SKIFCustomPath = EpicAssetPath + ((type == LibraryTexture::Cover) ? L"cover" : L"icon");
  std::wstring load_str       = FindCustomArtwork (type);

  isCustom = (! load_str.empty ( ));

  if (! isCustom)
  {
    if      (type == LibraryTexture::Cover &&
             PathFileExistsW ((EpicAssetPath + L"cover-original.jpg").c_str()))
      load_str =               EpicAssetPath + L"cover-original.jpg";
    else if (type == LibraryTexture::Icon &&
             SKIF_Util_SaveExtractExeIcon (launch_configs[0].getExecutableFullPath ( ), EpicAssetPath + L"icon-original.png"))
      load_str =               SKIFCustomPath + L"-original.png";
  }

  // Extract a copy of the icon and save it as an .ico
  if (type == LibraryTexture::Icon)
  {
    std::wstring
      OriginalIconPath  = EpicAssetPath + L"icon-original.png",
      ExtractedIconPath = EpicAssetPath + L"icon-original.ico";

    if (! PathFileExistsW (ExtractedIconPath.c_str()))
      SKIF_Util_SaveImageAsICO (OriginalIconPath.c_str(), ExtractedIconPath.c_str(), 32);
  }

  return load_str;
}

// Cover worker thread: the downloaded cover, else download it (known URL) or look it up in the Epic store
std::wstring
app_record_epic_s::PrepareCoverSource (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_EpicCollection&   _epic       = SKIF_EpicCollection  ::GetInstance ( );

  std::wstring load_str;

  load_str =
    SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\cover-original.jpg)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(epic.name_app).c_str());

  if ( ! PathFileExistsW (load_str.   c_str ()) )
  {
    if (! epic.gamebox_tall.empty())
      SKIF_Util_GetWebResource (SK_UTF8ToWideChar (epic.gamebox_tall), load_str);
    else
      _epic.IdentifyAssetNew (epic);
  }

  return load_str;
}

#pragma endregion

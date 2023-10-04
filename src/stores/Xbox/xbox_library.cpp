
#include <stores/xbox/xbox_library.h>
#include <pugixml.hpp>
#include <wtypes.h>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <SKIF.h>
#include <utility/utility.h>

#include <utility/registry.h>

/*
Xbox / MS Store games shared registry struture


Root Key: HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\GamingServices\PackageRepository\Root\

Each game is stored in a separate key beneath, with each key mostly just containing the package name as well as
root installation folder.

To get more information about the game, parsing the AppXManifest.xml in the root folder seems to be necessary.

*/

void
SKIF_Xbox_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  PLOG_INFO << "Detecting Xbox games...";

  HKEY  hKey,
        hSubKey;
  DWORD dwIndexKey    = 0,
        dwIndexSubKey = 0,
        dwResult      = 0,
        dwSize        = 0;
  WCHAR szSubKeyGUID[MAX_PATH] { },
        szSubKey    [MAX_PATH] { },
        szData      [MAX_PATH] { };
  int AppID = 1; // All apps with 0 will be ignored, so start at 1
  std::vector<std::pair<int, std::wstring>> gamingRoots;

  /* Load Xbox titles from registry */
  if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\GamingServices\PackageRepository\Root\)", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwIndexKey, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndexKey > 0)
      {
        // Enumerate Root -> GUIDs
        dwIndexKey--;

        dwSize   = sizeof(szSubKeyGUID) / sizeof(WCHAR);
        dwResult = RegEnumKeyExW (hKey, dwIndexKey, szSubKeyGUID, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          // Parsing GUID keys

          if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, (LR"(SOFTWARE\Microsoft\GamingServices\PackageRepository\Root\)" + std::wstring(szSubKeyGUID)).c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hSubKey) == ERROR_SUCCESS)
          {
            if (RegQueryInfoKeyW (hSubKey, NULL, NULL, NULL, &dwIndexSubKey, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
            {
              while (dwIndexSubKey > 0)
              {
                // Enumerate GUIDs -> Entries
                dwIndexSubKey--;

                dwSize   = sizeof(szSubKey) / sizeof(WCHAR);
                dwResult = RegEnumKeyExW (hSubKey, dwIndexSubKey, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

                if (dwResult == ERROR_NO_MORE_ITEMS)
                  break;

                if (dwResult == ERROR_SUCCESS)
                {
                  dwSize = sizeof(szData) / sizeof(WCHAR);
                  if (RegGetValueW (hSubKey, szSubKey, L"Package", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                  {
                    PLOG_VERBOSE << "Package: " << szData;

                    dwSize = sizeof(szData) / sizeof(WCHAR);
                    if (RegGetValueW (hSubKey, szSubKey, L"Root", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                    {
                      pugi::xml_document manifest, config;
                      app_record_s record (AppID);

                      PLOG_VERBOSE << "Root: " << szData;

                      record.store = "Xbox";
                      record.type  = "Game";
                      record._status.installed = true;
                      record.install_dir = szData;
                      record.install_dir = record.install_dir.substr(4); // Strip \\?\

                      if (record.install_dir.rfind(LR"(\)") != record.install_dir.size() - 1)
                        record.install_dir += LR"(\)";

                      PLOG_VERBOSE << "Adjusted install dir: " << record.install_dir;

                      record.Xbox_AppDirectory = record.install_dir;

                      // Try to load the AppX XML Manifest in the install folder
                      if (! manifest.load_file((record.install_dir + LR"(appxmanifest.xml)").c_str()))
                      {
                        PLOG_VERBOSE << "Failed to load AppX manifest at: " << record.install_dir << "appxmanifest.xml";
                        continue; // Skip to the next enumeration
                      }

                      // Load succeeded! Let's proceed.
                      PLOG_VERBOSE << "Successfully loaded appxmanifest.xml";

                      pugi::xml_node xmlRoot  = manifest.document_element();
                      std::wstring virtualFolder;
                      int driveAsInt = (record.install_dir.front() - '0');

                      // Use a vector to ensure we don't make unnecessary queries to the filesystem
                      for (auto& root : gamingRoots)
                      {
                        if (root.first == driveAsInt)
                          virtualFolder = root.second;
                      }

                      if (virtualFolder.empty())
                      {
                        std::wstring driveRoot = record.install_dir.substr(0, 3) + LR"(.GamingRoot)";

                        // .GamingRoot seems to be encoded in UTF-16 LE
                        if (PathFileExists(driveRoot.c_str()))
                        {
                          std::ifstream fin(driveRoot.c_str(), std::ios::binary);
                          fin.seekg(0, std::ios::end);
                          size_t size = (size_t)fin.tellg();

                          // skip initial 8 bytes: RGBX\0\0\0
                          fin.seekg(8, std::ios::beg);
                          size -= 8;

                          // skip null terminators at the end: \0\0
                          size -= 2;

                          // read
                          std::u16string u16((size / 2), '\0'); // (size / 2) + 1
                          fin.read((char*)&u16[0], size);

                          // convert to wstring
                          //std::wstring path(u16.begin(), u16.end());

                          // convert to wstring + partial path
                          virtualFolder = record.install_dir.substr(0, 3) + std::wstring(u16.begin(), u16.end()); // H:\ + path

                          // Push unto the vector
                          gamingRoots.push_back(std::pair<int, std::wstring>(driveAsInt, virtualFolder));
                        }
                      }

                      PLOG_VERBOSE << "virtualFolder: " << virtualFolder;

                      record.Xbox_PackageName = xmlRoot.child("Identity").attribute("Name").value();
                      record.names.normal     = xmlRoot.child("Properties").child_value("DisplayName");

                      // If we have found a partial path, construct the assumed full path
                      if (! virtualFolder.empty())
                        virtualFolder = SK_FormatStringW (LR"(%ws\%ws\Content\)", virtualFolder.c_str(), SK_UTF8ToWideChar (SKIF_Util_ReplaceInvalidFilenameChars (record.names.normal, '-')).c_str()); //LR"(\)" + SK_UTF8ToWideChar(SKIF_ReplaceInvalidFilenameChars(record.names.normal, '-')) + LR"(\Content\)";

                      // Ensure that it actually exists before we swap it in!
                      if (PathFileExists(virtualFolder.c_str()))
                        record.install_dir = virtualFolder;

                      PLOG_VERBOSE << "install_dir: " << record.install_dir;

                      std::string bitness = xmlRoot.child("Identity").attribute("ProcessorArchitecture").value();

                      if (bitness == "x64")
                        record.specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
                      else
                        record.specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;

                      std::wstring targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar (record.Xbox_PackageName).c_str());
                      std::wstring iconPath   = targetPath + L"icon-original.png";
                      std::wstring coverPath  = targetPath + L"cover-fallback.png";

                      bool icon  = PathFileExists(iconPath .c_str()),
                           cover = PathFileExists(coverPath.c_str());

                      int lid = 0;
                      for (pugi::xml_node app = xmlRoot.child("Applications").child("Application"); app; app = app.next_sibling("Application"))
                      {
                        app_record_s::launch_config_s lc;
                        lc.id = lid;
                        lc.valid = true;
                        lc.store = L"Xbox";

                        std::string strAppID = app.attribute("Id").value();

                        if (config.load_file((record.install_dir + LR"(MicrosoftGame.config)").c_str()))
                        {
                          PLOG_VERBOSE << "Successfully loaded MicrosoftGame.config";

                          pugi::xml_node xmlConfigRoot  = config.document_element();

                          record.Xbox_StoreId = xmlConfigRoot.child_value("StoreId");

                          int exeCount = 0;

                          // Minecraft Launcher only defines one Executable in MicrosoftGame.Config, and without defining an ID for it so if the
                          //   total amount of defined executables are 1 we assume that it is the one we are after. This way SKIF will still use
                          //     ExecutableName.exe instead of GameLaunchHelper.exe as the name of the profile folder.
                          for (pugi::xml_node exe = xmlConfigRoot.child("ExecutableList").child("Executable"); exe; exe = exe.next_sibling("Executable"))
                          {
                            exeCount++;
                          }

                          for (pugi::xml_node exe = xmlConfigRoot.child("ExecutableList").child("Executable"); exe; exe = exe.next_sibling("Executable"))
                          {
                            if (exeCount == 1 || strAppID == exe.attribute("Id").value())
                            {
                              lc.executable = SK_UTF8ToWideChar (exe.attribute("Name").value());

                              size_t pos = lc.executable.rfind(LR"(\)");
                              if (pos != std::wstring::npos)
                              {
                                lc.executable_path = record.install_dir + lc.executable;
                                lc.executable = lc.executable.substr(pos + 1);
                              }
                            }
                          }
                        }

                        else {
                          PLOG_VERBOSE << "Failed to load MicrosoftGame config at: " << record.install_dir << "MicrosoftGame.config";
                        }

                        // Some games (e.g. Quake 2) needs to be launched through the gamelaunchhelper.exe, so retain that value
                        lc.executable_helper = record.install_dir + L"\\" + SK_UTF8ToWideChar (app.attribute("Executable").value());

                        if (lc.executable.empty())
                          lc.executable = lc.executable_helper;

                        if (lc.executable_path.empty())
                          lc.executable_path = (lc.executable == lc.executable_helper)
                                              ? lc.executable
                                              : record.install_dir + lc.executable;

                        std::replace(lc.executable_path.begin(), lc.executable_path.end(), '/', '\\'); // Replaces all / with \

                        // Naively assume the first launch config has the more appropriate name
                        if (lid == 0)
                        {
                          std::string appDisplayName = app.child("uap:VisualElements").attribute("DisplayName").value();

                          if (! appDisplayName.empty() && appDisplayName.find("ms-resource") == std::string::npos)
                            record.names.normal = appDisplayName;
                        }
                      
                        lc.working_dir = record.install_dir;

                        record.launch_configs[lid] = lc;
                        lid++;

                        // Create necessary directories if they do not exist
                        if (!icon || !cover)
                        {
                          std::error_code ec;
                          // Create any missing directories
                          if (! std::filesystem::exists (            targetPath, ec))
                                std::filesystem::create_directories (targetPath, ec);
                        }

                        if (!icon)
                        {
                          std::wstring file = SK_UTF8ToWideChar (app.child("uap:VisualElements").attribute("Square44x44Logo").value());

                          if (!file.empty())
                          {
                            std::wstring fullPath = record.install_dir + file;

                            if (CopyFile(fullPath.c_str(), iconPath.c_str(), FALSE))
                              icon = true;
                          }
                        }

                        if (!cover)
                        {
                          std::wstring file = SK_UTF8ToWideChar (app.child("uap:VisualElements").child("uap:SplashScreen").attribute("Image").value());

                          if (!file.empty())
                          {
                            std::wstring fullPath = record.install_dir + file;

                            if (CopyFile(fullPath.c_str(), coverPath.c_str(), FALSE))
                              cover = true;
                          }
                        }
                      }

                      if (record.launch_configs.size() == 0)
                        continue;

                      // Naively assume first launch option is right one
                      std::wstring trimmed = record.launch_configs[0].executable;
                      size_t pos = 0;

                      // Some basic trimming
                      pos = record.launch_configs[0].executable.rfind('\\');
                      if (pos != std::wstring::npos)
                        trimmed = trimmed.substr(pos + 1);

                      pos = trimmed.rfind('/');
                      if (pos != std::wstring::npos)
                        trimmed = trimmed.substr(pos + 1);

                      record.specialk.profile_dir = trimmed;

                      std::pair <std::string, app_record_s>
                        Xbox (record.names.normal, record);

                      PLOG_VERBOSE << "Added to the list of detected games!";
                      apps->emplace_back (Xbox);
                      
                      // Increment the internal app ID
                      AppID++;
                    }
                  }
                }

                // End GUIDs -> Entries enumeration
              }
            }

            RegCloseKey (hSubKey);
          }
        }

        // End Root -> GUIDs enumeration
      }
    }

    RegCloseKey (hKey);
  }
}

void
SKIF_Xbox_IdentifyAssetNew (std::string PackageName, std::string StoreID)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\Xbox\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(PackageName).c_str());

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            targetAssetPath, ec))
        std::filesystem::create_directories (targetAssetPath, ec);

  // Download JSON for the cover
  if (! PathFileExists ((targetAssetPath + L"store.json").c_str()))
  {
    std::wstring query = L"https://storeedgefd.dsx.mp.microsoft.com/v8.0/sdk/products?market=US&locale=en-US&deviceFamily=Windows.Desktop";
    std::string body  = SK_FormatString(R"({ "productIds": "%s" })", StoreID.c_str());

    SKIF_Util_GetWebResource (query, targetAssetPath + L"store.json", L"POST", L"Content-Type: application/json; charset=utf-8", body);
  }

  try
  {
    std::ifstream fileStore(targetAssetPath + L"store.json");
    nlohmann::json jf = nlohmann::json::parse(fileStore, nullptr, false);
    fileStore.close();

    if (jf.is_discarded ( ))
    {
      DeleteFile ((targetAssetPath + L"store.json").c_str()); // Something went wrong -- delete the file so a new attempt is performed next time
    }
    else
    {
      for (auto& image : jf["Products"][0]["LocalizedProperties"][0]["Images"])
      {
        if (image["ImagePurpose"].get <std::string_view>()._Equal(R"(Poster)"))
        {
          // Download a downscaled copy of the cover

          // Convert the URL value to a regular string
          std::string assetUrl = image["Uri"]; // will throw exception if "Uri" does not exist

          // Strip the first two characters (//)
          assetUrl = SK_FormatString(R"(https://%s%s)", assetUrl.substr(2).c_str(), ((_registry.bLowBandwidthMode) ? "?q=90&h=900&w=600" : ""));

          SKIF_Util_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"cover-original.png", L"GET", L"", "");
        }
      }
    }
  }
  catch (const std::exception&)
  {

  }
}

bool
SKIF_Xbox_hasInstalledGamesChanged (void)
{
  static DWORD dwLastSignalCheck = 0;

  bool signal = false;
  if (SKIF_Util_timeGetTime ( ) > dwLastSignalCheck + 5000)
  {
    static SKIF_RegistryWatch
      appWatch ( HKEY_LOCAL_MACHINE,
                   LR"(SOFTWARE\Microsoft\GamingServices\PackageRepository\Root)",
                     L"XboxInstallNotify", TRUE, REG_NOTIFY_CHANGE_NAME, true, false, true);
  
    signal            = appWatch.isSignaled   ( );
    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}
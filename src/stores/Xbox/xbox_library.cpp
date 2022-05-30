
#include <stores/xbox/xbox_library.h>
#include <pugixml.hpp>
#include <wtypes.h>
#include <fstream>
#include <filesystem>
#include <json.hpp>
#include <SKIF.h>
#include <SKIF_utility.h>


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
  PLOG_INFO << "Detecting Xbox games...";

  HKEY  hKey, hSubKey;
  DWORD dwIndex = 0, dwResult, dwSize;
  WCHAR szSubKeyGUID[MAX_PATH];
  WCHAR szSubKey[MAX_PATH];
  WCHAR szData[MAX_PATH];
  int AppID = 1; // All apps with 0 will be ignored, so start at 1
  std::vector<std::pair<int, std::wstring>> gamingRoots;

  /* Load Xbox titles from registry */
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\GamingServices\PackageRepository\Root\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &dwResult, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      do
      {
        // Enumerate Root -> GUIDs

        dwSize   = sizeof(szSubKeyGUID) / sizeof(WCHAR);
        dwResult = RegEnumKeyExW(hKey, dwIndex, szSubKeyGUID, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          // Enumerate keys below the GUIDs

          if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, (LR"(SOFTWARE\Microsoft\GamingServices\PackageRepository\Root\)" + std::wstring(szSubKeyGUID)).c_str(), 0, KEY_READ, &hSubKey) == ERROR_SUCCESS)
          {
            dwSize   = sizeof(szSubKey) / sizeof(WCHAR);
            dwResult = RegEnumKeyExW(hSubKey, 0, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

            if (dwResult == ERROR_NO_MORE_ITEMS)
              break;

            if (dwResult == ERROR_SUCCESS)
            {
              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW(hSubKey, szSubKey, L"Package", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
              {
                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW(hSubKey, szSubKey, L"Root", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                {
                  pugi::xml_document manifest, config;
                  app_record_s record (AppID);

                  record.store = "Xbox";
                  record.type = "Game";
                  record._status.installed = true;
                  record.install_dir = szData;
                  record.install_dir = record.install_dir.substr(4); // Strip \\?\

                  if (record.install_dir.rfind(LR"(\)") != record.install_dir.size() - 1)
                    record.install_dir += LR"(\)";

                  record.Xbox_AppDirectory = record.install_dir;

                  if (manifest.load_file((record.install_dir + LR"(appxmanifest.xml)").c_str()))
                  {
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

                    record.Xbox_PackageName = xmlRoot.child("Identity").attribute("Name").value();
                    record.names.normal     = xmlRoot.child("Properties").child_value("DisplayName");

                    // If we have found a partial path, construct the assumed full path
                    if (! virtualFolder.empty())
                      virtualFolder = SK_FormatStringW(LR"(%ws\%ws\Content\)", virtualFolder.c_str(), SK_UTF8ToWideChar(SKIF_Util_ReplaceInvalidFilenameChars(record.names.normal, '-')).c_str()); //LR"(\)" + SK_UTF8ToWideChar(SKIF_ReplaceInvalidFilenameChars(record.names.normal, '-')) + LR"(\Content\)";

                    // Ensure that it actually exists before we swap it in!
                    if (PathFileExists(virtualFolder.c_str()))
                      record.install_dir = virtualFolder;

                    std::string bitness = xmlRoot.child("Identity").attribute("ProcessorArchitecture").value();

                    if (bitness == "x64")
                      record.specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
                    else
                      record.specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;

                    std::wstring targetPath = SK_FormatStringW(LR"(%ws\Assets\Xbox\%ws\)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(record.Xbox_PackageName).c_str());
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

                      std::string appId = app.attribute("Id").value();

                      if (config.load_file((record.install_dir + LR"(MicrosoftGame.config)").c_str()))
                      {
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
                          if (exeCount == 1 || appId == exe.attribute("Id").value())
                          {
                            lc.executable = SK_UTF8ToWideChar(exe.attribute("Name").value());

                            size_t pos = lc.executable.rfind(LR"(\)");
                            if (pos != std::wstring::npos)
                            {
                              lc.executable_path = record.install_dir + lc.executable;
                              lc.executable = lc.executable.substr(pos + 1);
                            }
                          }
                        }
                      }

                      // Some games (e.g. Quake 2) needs to be launched through the gamelaunchhelper.exe, so retain that value
                      lc.executable_helper = record.install_dir + L"\\" + SK_UTF8ToWideChar(app.attribute("Executable").value());

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
                        std::filesystem::create_directories(targetPath);

                      if (!icon)
                      {
                        std::wstring file = SK_UTF8ToWideChar(app.child("uap:VisualElements").attribute("Square44x44Logo").value());

                        if (!file.empty())
                        {
                          std::wstring fullPath = record.install_dir + file;

                          if (CopyFile(fullPath.c_str(), iconPath.c_str(), FALSE))
                            icon = true;
                        }
                      }

                      if (!cover)
                      {
                        std::wstring file = SK_UTF8ToWideChar(app.child("uap:VisualElements").child("uap:SplashScreen").attribute("Image").value());

                        if (!file.empty())
                        {
                          std::wstring fullPath = record.install_dir + file;

                          if (CopyFile(fullPath.c_str(), coverPath.c_str(), FALSE))
                            cover = true;
                        }
                      }
                    }

                    record.names.all_upper  = record.names.normal;
                    std::for_each(record.names.all_upper.begin(), record.names.all_upper.end(), ::toupper);

                    if (record.launch_configs.size() > 0)
                    {
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

                      apps->emplace_back (Xbox);
                    }
                  }
                }

                AppID++;
              }
            }

            RegCloseKey(hSubKey);
          }
        }

        dwIndex++;

      } while (1);
    }

    RegCloseKey(hKey);
  }
}

void
SKIF_Xbox_IdentifyAssetNew (std::string PackageName, std::string StoreID)
{
  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\Xbox\%ws\)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(PackageName).c_str());
  std::filesystem::create_directories(targetAssetPath);

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
          extern bool SKIF_bLowBandwidthMode; // TAKES TOO LONG! :D

          // Convert the URL value to a regular string
          std::string assetUrl = image["Uri"]; // will throw exception if "Uri" does not exist

          // Strip the first two characters (//)
          assetUrl = SK_FormatString(R"(https://%s%s)", assetUrl.substr(2).c_str(), ((SKIF_bLowBandwidthMode) ? "?q=90&h=900&w=600" : ""));

          SKIF_Util_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"cover-original.png", L"GET", L"", "");
        }
      }
    }
  }
  catch (const std::exception&)
  {

  }
}
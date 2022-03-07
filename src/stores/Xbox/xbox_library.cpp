
#include <stores/xbox/xbox_library.h>
#include <pugixml.hpp>
#include <wtypes.h>
#include <fstream>
#include <filesystem>
#include <json.hpp>


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
  HKEY  hKey, hSubKey;
  DWORD dwIndex = 0, dwResult, dwSize;
  WCHAR szSubKeyGUID[MAX_PATH];
  WCHAR szSubKey[MAX_PATH];
  WCHAR szData[MAX_PATH];
  int AppID = 1; // All apps with 0 will be ignored, so start at 1

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

                  if (manifest.load_file((record.install_dir + LR"(appxmanifest.xml)").c_str()))
                  {
                    pugi::xml_node xmlRoot  = manifest.document_element();

                    record.Xbox_PackageName = xmlRoot.child("Identity").attribute("Name").value();
                    record.names.normal     = xmlRoot.child("Properties").child_value("DisplayName");

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

                        for (pugi::xml_node exe = xmlConfigRoot.child("ExecutableList").child("Executable"); exe; exe = exe.next_sibling("Executable"))
                        {
                          if (appId == exe.attribute("Id").value())
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

                      if (lc.executable.empty())
                        lc.executable = SK_UTF8ToWideChar(app.attribute("Executable").value());

                      if (lc.executable_path.empty())
                        lc.executable_path = record.install_dir + lc.executable;

                      // Naively assume the first launch config has the more appropriate name
                      if (lid == 0)
                      {
                        std::string appDisplayName = app.child("uap:VisualElements").attribute("DisplayName").value();

                        if (!appDisplayName.empty() && appDisplayName.find("ms-resource") == std::string::npos)
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
                      record.specialk.profile_dir = record.launch_configs[0].executable;

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

    SKIF_GetWebResource (query, targetAssetPath + L"store.json", L"POST", L"Content-Type: application/json; charset=utf-8", body);
  }

  std::ifstream fileStore(targetAssetPath + L"store.json");
  nlohmann::json jf = nlohmann::json::parse(fileStore, nullptr, false);
  fileStore.close();

  if (jf.is_discarded ( ))
  {
    DeleteFile ((targetAssetPath + L"store.json").c_str()); // Something went wrong -- delete the file so a new attempt is performed next time
  }
  else
  {
    try
    {
      for (auto& image : jf["Products"][0]["LocalizedProperties"][0]["Images"])
      {
        if (image["ImagePurpose"].get <std::string_view>()._Equal(R"(Poster)"))
        {
          // Download a downscaled copy of the cover
          extern bool            SKIF_bLowBandwidthMode; // TAKES TOO LONG! :D

          // Convert the URL value to a regular string
          std::string assetUrl = image["Uri"]; // will throw exception if "Uri" does not exist

          // Strip the first two characters (//)
          assetUrl = SK_FormatString(R"(https://%s%s)", assetUrl.substr(2).c_str(), ((SKIF_bLowBandwidthMode) ? "?q=90&h=900&w=600" : ""));

          SKIF_GetWebResource (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"cover-original.png", L"GET", L"", "");
        }
      }
    }
    catch (const std::exception&)
    {

    }
  }
}
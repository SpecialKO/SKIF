
#include <stores/egs/egs_library.h>
//#include <stores/generic_app.h>
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

  /* Load EGS appdata path from registry */
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Epic Games\EpicGamesLauncher\)", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
  {
    dwSize = sizeof(szData) / sizeof(WCHAR);
    if (RegGetValueW(hKey, NULL, L"AppDataPath", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
    {
      SKIF_EGS_AppDataPath = szData; // C:\ProgramData\Epic\EpicGamesLauncher\Data
      SKIF_EGS_AppDataPath += LR"(\Manifests\)";

      RegCloseKey(hKey);

      // Abort if the folder does not exist
      if (! PathFileExists(SKIF_EGS_AppDataPath.c_str()))
      {
        SKIF_EGS_AppDataPath = L"";
        return;
      }

      //OutputDebugString(L"Reading Epic Games Store manifests\n");
      //OutputDebugString(EGS_AppDataPath.c_str());
      //OutputDebugString(L"\n");

      bool hasGames = false;

      for (const auto& entry : std::filesystem::directory_iterator(SKIF_EGS_AppDataPath))
      {
        //OutputDebugString(entry.path().wstring().c_str());
        //OutputDebugString(L"\n");

        if (entry.is_directory() == false &&
            entry.path().extension().string() == ".item" )
        {
          std::ifstream file(entry.path());
          nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
          file.close();

          // Parse file
          //OutputDebugString(SK_UTF8ToWideChar(jf.dump()).c_str());
          //OutputDebugString(L"\n");

          // Skip if we're dealing with a broken manifest
          if (jf.is_discarded ( ))
            continue;

          // Skip if the install location does not exist
          if (! PathFileExists (SK_UTF8ToWideChar (std::string (jf.at ("InstallLocation"))).c_str()))
            continue;

          bool isGame = false;

          for (auto& categories : jf["AppCategories"])
          {
            if (categories.dump() == R"("games")")
              isGame = true;
          }

          if (isGame)
          {
            hasGames = true;

            app_record_s EGS_record (jf.at ("InstallSize"));

            EGS_record.store                = "EGS";
            EGS_record.type                 = "Game";
            EGS_record._status.installed    = true;
            EGS_record.install_dir          = SK_UTF8ToWideChar (jf.at ("InstallLocation"));
            EGS_record.install_dir.erase(std::find(EGS_record.install_dir.begin(), EGS_record.install_dir.end(), '\0'), EGS_record.install_dir.end());

            EGS_record.names.normal         = jf.at ("DisplayName");

            EGS_record.names.all_upper      = EGS_record.names.normal;
            std::for_each(EGS_record.names.all_upper.begin(), EGS_record.names.all_upper.end(), ::toupper);

            app_record_s::launch_config_s lc;
            lc.id                           = 0;
            lc.store                        = L"EGS";
            lc.executable                   = EGS_record.install_dir + L"\\" + SK_UTF8ToWideChar(jf.at("LaunchExecutable"));
            //lc.executable.erase(std::find(lc.executable.begin(), lc.executable.end(), '\0'), lc.executable.end());

            //OutputDebugString(lc.executable.c_str());
            //OutputDebugString(L"\n");

            lc.working_dir                  = EGS_record.install_dir;
            //lc.launch_options = SK_UTF8ToWideChar(app.at("LaunchCommand"));

            std::string CatalogNamespace    = jf.at("CatalogNamespace"),
                        CatalogItemId       = jf.at("CatalogItemId"),
                        AppName             = jf.at("AppName");

            // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
            lc.launch_options = SK_UTF8ToWideChar(CatalogNamespace + "%3A" + CatalogItemId + "%3A" + AppName);
            lc.launch_options.erase(std::find(lc.launch_options.begin(), lc.launch_options.end(), '\0'), lc.launch_options.end());

            EGS_record.launch_configs[0]    = lc;

            EGS_record.EGS_CatalogNamespace = CatalogNamespace;
            EGS_record.EGS_CatalogItemId    = CatalogItemId;
            EGS_record.EGS_AppName          = AppName;
            EGS_record.EGS_DisplayName      = EGS_record.names.normal;

            EGS_record.specialk.profile_dir = SK_UTF8ToWideChar (EGS_record.EGS_DisplayName);
            EGS_record.specialk.injection.injection.type = sk_install_state_s::Injection::Type::Global;

            // Strip invalid filename characters
            extern std::wstring SKIF_StripInvalidFilenameChars (std::wstring);
            EGS_record.specialk.profile_dir = SKIF_StripInvalidFilenameChars (EGS_record.specialk.profile_dir);
            
            std::pair <std::string, app_record_s>
              EGS(EGS_record.names.normal, EGS_record);

            apps->emplace_back(EGS);

            // Documents\My Mods\SpecialK\Profiles\AppCache\#EpicApps\<AppName>
            std::wstring AppCacheDir = SK_FormatStringW(LR"(%ws\Profiles\AppCache\#EpicApps\%ws)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(AppName).c_str());

            // Create necessary directories if they do not exist
            std::filesystem::create_directories (AppCacheDir);

            // Copy manifest to AppCache directory
            CopyFile (entry.path().c_str(), (AppCacheDir + LR"(\manifest.json)").c_str(), false);
          }
        }
      }

      if (hasGames)
      {
        SKIF_EGS_GetCatalogNamespaces ( );
      }
    }
  }
}

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
      SKIF_EGS_GetWebAsset (L"https://raw.githubusercontent.com/srdrabx/offers-tracker/master/database/namespaces.json", path);
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
            SKIF_EGS_GetWebAsset(L"https://raw.githubusercontent.com/srdrabx/offers-tracker/master/database/namespaces.json", path);
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
    /*
    OutputDebugString(L"Namespace: ");
    OutputDebugString(SK_UTF8ToWideChar(CatalogNamespace).c_str());
    OutputDebugString(L"\n");

    OutputDebugString(L"JSON: ");
    OutputDebugString(SK_UTF8ToWideChar(SKIF_EGS_JSON_CatalogNamespaces[CatalogNamespace].dump()).c_str());
    OutputDebugString(L"\n");
    */

    // Force an update of the file if it lacks the expected namespace we're looking for
    if (SKIF_EGS_JSON_CatalogNamespaces[CatalogNamespace].is_null())
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
        SKIF_EGS_GetWebAsset (SK_FormatStringW (L"https://raw.githubusercontent.com/srdrabx/offers-tracker/master/database/offers/%ws.json", offerID.c_str()), targetPath);
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

        /* Not always the same -- Horizon Zero Dawn has a :tm: included in the offer title which is missing in the local DisplayName manifest.
        if (jf["title"].dump() == "\"" + DisplayName + "\"")
        {
          isTitle = true;
        }
        */

        if (isGame /* && isTitle */)
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

              SKIF_EGS_GetWebAsset (SK_UTF8ToWideChar (assetUrl), targetAssetPath + L"OfferImageTall.jpg");

              assetDownloaded = true;
            }

            /*
            if (image["type"].dump() == R"("ProductLogo")" && !PathFileExists((targetAssetPath + L"ProductLogo.jpg").c_str()))
            {
              OutputDebugString(L"ProductLogo: ");
              OutputDebugString(SK_UTF8ToWideChar(image["url"]).c_str());
              OutputDebugString(L"\n");

              SKIF_EGS_GetWebAsset(SK_UTF8ToWideChar(image["url"]), targetAssetPath + L"ProductLogo.jpg");
            }
            */
          }

          // We've retrieved the proper assets -- break the main loop
          if (assetDownloaded)
            break;
        }
      }
    }
  }
}


DWORD
WINAPI
SKIF_EGS_FetchAsset (skif_get_asset_t* get)
{
  ULONG ulTimeout = 5000UL;

  PCWSTR rgpszAcceptTypes [] = { L"*/*", nullptr };
  HINTERNET hInetHTTPGetReq  = nullptr,
            hInetHost        = nullptr,
  hInetRoot                  =
    InternetOpen (
      L"Special K - Asset Crawler",
        INTERNET_OPEN_TYPE_DIRECT,
          nullptr, nullptr,
            0x00
    );

  // (Cleanup On Error)
  auto CLEANUP = [&](bool clean = false) ->
  DWORD
  {
    if (! clean)
    {
      DWORD dwLastError =
           GetLastError ();

      OutputDebugStringW (
        ( std::wstring (L"WinInet Failure (") +
              std::to_wstring (dwLastError)   +
          std::wstring (L"): ")               +
                 _com_error   (dwLastError).ErrorMessage ()
        ).c_str ()
      );
    }

    if (hInetHTTPGetReq != nullptr) InternetCloseHandle (hInetHTTPGetReq);
    if (hInetHost       != nullptr) InternetCloseHandle (hInetHost);
    if (hInetRoot       != nullptr) InternetCloseHandle (hInetRoot);

    skif_get_asset_t* to_delete = nullptr;
    std::swap   (get,   to_delete);
    delete              to_delete;

    return 0;
  };

  if (hInetRoot == nullptr)
    return CLEANUP ();

  DWORD_PTR dwInetCtx = 0;

  hInetHost =
    InternetConnect ( hInetRoot,
                        get->wszHostName,
                          INTERNET_DEFAULT_HTTP_PORT,
                            nullptr, nullptr,
                              INTERNET_SERVICE_HTTP,
                                0x00,
                                  (DWORD_PTR)&dwInetCtx );

  if (hInetHost == nullptr)
  {
    return CLEANUP ();
  }

  hInetHTTPGetReq =
    HttpOpenRequest ( hInetHost,
                        nullptr,
                          get->wszHostPath,
                            L"HTTP/1.1",
                              nullptr,
                                rgpszAcceptTypes,
                                                                    INTERNET_FLAG_IGNORE_CERT_DATE_INVALID |
                                  INTERNET_FLAG_CACHE_IF_NET_FAIL | INTERNET_FLAG_IGNORE_CERT_CN_INVALID   |
                                  INTERNET_FLAG_RESYNCHRONIZE     | INTERNET_FLAG_CACHE_ASYNC,
                                    (DWORD_PTR)&dwInetCtx );


  // Wait 2500 msecs for a dead connection, then give up
  //
  InternetSetOptionW ( hInetHTTPGetReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                         &ulTimeout,    sizeof (ULONG) );

  if (hInetHTTPGetReq == nullptr)
  {
    return CLEANUP ();
  }

  if ( HttpSendRequestW ( hInetHTTPGetReq,
                            nullptr,
                              0,
                                nullptr,
                                  0 ) )
  {
    DWORD dwStatusCode        = 0;
    DWORD dwStatusCode_Len    = sizeof (DWORD);

    DWORD dwContentLength     = 0;
    DWORD dwContentLength_Len = sizeof (DWORD);
    DWORD dwSizeAvailable;

    HttpQueryInfo ( hInetHTTPGetReq,
                     HTTP_QUERY_STATUS_CODE |
                     HTTP_QUERY_FLAG_NUMBER,
                       &dwStatusCode,
                         &dwStatusCode_Len,
                           nullptr );

    if (dwStatusCode == 200)
    {
      HttpQueryInfo ( hInetHTTPGetReq,
                        HTTP_QUERY_CONTENT_LENGTH |
                        HTTP_QUERY_FLAG_NUMBER,
                          &dwContentLength,
                            &dwContentLength_Len,
                              nullptr );

      std::vector <char> http_chunk;
      std::vector <char> concat_buffer;

      while ( InternetQueryDataAvailable ( hInetHTTPGetReq,
                                             &dwSizeAvailable,
                                               0x00, NULL )
        )
      {
        if (dwSizeAvailable > 0)
        {
          DWORD dwSizeRead = 0;

          if (http_chunk.size () < dwSizeAvailable)
              http_chunk.resize   (dwSizeAvailable);

          if ( InternetReadFile ( hInetHTTPGetReq,
                                    http_chunk.data (),
                                      dwSizeAvailable,
                                        &dwSizeRead )
             )
          {
            if (dwSizeRead == 0)
              break;

            concat_buffer.insert ( concat_buffer.cend   (),
                                    http_chunk.cbegin   (),
                                      http_chunk.cbegin () + dwSizeRead );

            if (dwSizeRead < dwSizeAvailable)
              break;
          }
        }

        else
          break;
      }

      FILE *fOut =
        _wfopen ( get->wszLocalPath, L"wb+" );

      if (fOut != nullptr)
      {
        fwrite (concat_buffer.data (), concat_buffer.size (), 1, fOut);
        fclose (fOut);
      }
    }
  }

  CLEANUP (true);

  return 1;
}

void
SKIF_EGS_GetWebAsset (std::wstring url, std::wstring_view destination)
{
  auto* get =
    new skif_get_asset_t { };

  URL_COMPONENTSW urlcomps = { };

  urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

  urlcomps.lpszHostName     = get->wszHostName;
  urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

  urlcomps.lpszUrlPath      = get->wszHostPath;
  urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

  if ( InternetCrackUrl (          url.c_str  (),
         gsl::narrow_cast <DWORD> (url.length ()),
                            0x00,
                              &urlcomps
                        )
     )
  {
    wcsncpy ( get->wszLocalPath,
                           destination.data (),
                       MAX_PATH );

    SKIF_EGS_FetchAsset (get);
  }
}

void
SKIF_EGS_GetWebAssetThreaded (std::wstring url, std::wstring_view destination)
{
  auto* get =
    new skif_get_asset_t { };

  URL_COMPONENTSW urlcomps = { };

  urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

  urlcomps.lpszHostName     = get->wszHostName;
  urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

  urlcomps.lpszUrlPath      = get->wszHostPath;
  urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

  if ( InternetCrackUrl (          url.c_str  (),
         gsl::narrow_cast <DWORD> (url.length ()),
                            0x00,
                              &urlcomps
                        )
     )
  {
    wcsncpy ( get->wszLocalPath,
                           destination.data (),
                       MAX_PATH );

    _beginthreadex (
       nullptr, 0, (_beginthreadex_proc_type)SKIF_EGS_FetchAsset,
           get, 0x0, nullptr
                   );
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
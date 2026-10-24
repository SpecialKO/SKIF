//
// Copyright 2020 - 2026  Andon "Kaldaien" Coleman and Aemony
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <stores/steam/steam_library.h>
#include <utility/registry.h>
#include <utility/utility.h>
#include <stores/Steam/apps_ignore.h>
#include <vdf_parser.hpp>
#include <utility/fsutil.h>
#include <stores/Steam/vdf.h>

#include <fstream>
#include <filesystem>
#include <regex>
#include <utility/injection.h>
#include <nlohmann/json.hpp>

#include <utility/games.h>
#include <utility/skif_imgui.h>
#include <SKIF.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

// Steam-store entries in the app list are always app_record_steam_s
static app_record_steam_s*
AsSteamRecord (app_entry_t& app)
{
  return static_cast <app_record_steam_s*> (app.second.get ( ));
}

std::unique_ptr <skValveDataFile> appinfo = nullptr;

SteamId3_t g_SteamUserID      = 0;
DWORD      g_dwSteamProcessID = 0;

// This is used to indicate to the main thread whether appmanifest files have
//   been parsed by the child thread or not yet.
// This is a critical optimization as it ensures the main thread isn't parsing
//   the Steam libraries the first time around.
std::atomic<bool> g_SteamLibrariesParsed = false;

#define MAX_STEAM_LIBRARIES 16

extern std::atomic<int> SKIF_FrameCount;

CRITICAL_SECTION VFSManifestSection;

// Thread safety is backed by the VFSManifestSection critical section
struct {
  int                 frame_last_scanned = 0; // 0 == not initialized nor scanned
  SKIF_DirectoryWatch watch;
  SK_VirtualFS        manifest_vfs;
  DWORD               signaled = 0;
  int                 count    = 0;
  wchar_t             path [MAX_PATH + 2] = { };
  UINT_PTR            timer;
} static steam_libraries[MAX_STEAM_LIBRARIES];

// Helper function used to perform a case insensitive search of tyti::vdf::objects
static std::shared_ptr <tyti::vdf::object>
SKIF_VDF_ciSearch (const std::string value, const std::unordered_map <std::string, std::shared_ptr<tyti::vdf::object>>* map)
{
  for (auto& child : *map)
  {
    if (lstrcmpiA (child.first.c_str(), value.c_str()) == 0)
      return child.second;
  }

  return nullptr;
}

int
SK_VFS_ScanTree ( SK_VirtualFS::vfsNode* pVFSRoot,
                                wchar_t* wszDir,
                                wchar_t* wszPattern,
                                    int  max_depth,
                                    int      depth,
                  SK_VirtualFS::vfsNode* pVFSImmutableRoot )
{
  if (pVFSRoot == nullptr)
    return 0;

  if (pVFSImmutableRoot == nullptr)
      pVFSImmutableRoot = pVFSRoot;

  if (depth > max_depth)
    return 0;

  int        found                  = 0;
  wchar_t    wszPath [MAX_PATH + 2] = { };
  _swprintf (wszPath, LR"(%s\%s)", wszDir, wszPattern);

  WIN32_FIND_DATA ffd         = { };
  HANDLE          hFind       =
    FindFirstFileExW (wszPath, FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

  if (hFind == INVALID_HANDLE_VALUE) { return 0; }

  // 2023-12-22: Added the input directory as parent directory for all files
  pVFSRoot = pVFSImmutableRoot->addDirectory (wszDir);

  do
  {
    if ( wcscmp (ffd.cFileName, L".")  == 0 ||
         wcscmp (ffd.cFileName, L"..") == 0 )
    {
      continue;
    }

    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
       wchar_t   wszDescend [MAX_PATH + 2] = { };
      _swprintf (wszDescend, LR"(%s\%s)", wszDir, ffd.cFileName);

      if (! pVFSImmutableRoot->containsDirectory (wszDescend))
      {
        auto* child =
          pVFSImmutableRoot->addDirectory (wszDescend);

        found +=
          SK_VFS_ScanTree (child, wszDescend, L"*", max_depth, depth + 1, pVFSImmutableRoot);
      }
    }

    else
    {

#ifdef _DEBUG
      SK_VirtualFS::File* pFile =
#endif

        pVFSRoot->addFile (ffd.cFileName);

#ifdef _DEBUG
      PLOG_VERBOSE << pFile->getFullPath ();
#endif

      ++found;
    }
  } while (FindNextFile (hFind, &ffd));

  FindClose (hFind);

  return found;
}

std::string
SK_GetManifestContentsForAppID (app_record_steam_s *app)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  static AppId_t     manifest_id = 0;
  static std::string manifest;

  if (! app->steam.manifest_data.empty())
    return app->steam.manifest_data;

  if (manifest_id == app->id && (! manifest.empty ()))
    return manifest;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);

  if (! steam_lib_paths)
    return "";

  if (steam_libs != 0)
  {
    bool found = false;
    wchar_t    wszManifestFullPath [MAX_PATH + 2] = { };

    EnterCriticalSection (&VFSManifestSection);

    for (int i = 0; i < steam_libs; i++)
    {
      auto& library =
        steam_libraries[i];

      SK_VirtualFS::vfsNode* pFolder =
        library.manifest_vfs;

      wchar_t    wszManifest [MAX_PATH + 2] = { };
      swprintf ( wszManifest, MAX_PATH,
                   LR"(appmanifest_%u.acf)",
                            app->id );

      // This will really only iterate once, over [0]...
      for (const auto& folder : pFolder->children)
      {
        if (folder.second->containsFile (wszManifest))
        {
          auto* file =
            folder.second->children[wszManifest];

          wcsncpy_s (wszManifestFullPath,  MAX_PATH,
             file->getFullPath().c_str(), _TRUNCATE
          );

          found = true;
          break;
        }
      }

      if (found)
        break;
    }

    LeaveCriticalSection (&VFSManifestSection);

    // When opening an existing file, the CreateFile function performs the following actions:
    // [...] and ignores any file attributes (FILE_ATTRIBUTE_*) specified by dwFlagsAndAttributes.
    CHandle hManifest (
      CreateFileW ( wszManifestFullPath,
                      GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr,        OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, // GetFileAttributesW (wszManifestFullPath)
                              nullptr ) );

    if (hManifest != INVALID_HANDLE_VALUE)
    {
      //PLOG_VERBOSE << "Reading " << wszManifest;

      DWORD dwSizeHigh = 0,
            dwRead     = 0,
            dwSize     =
        GetFileSize (hManifest, &dwSizeHigh);

      auto szManifestData =
        std::make_unique <char []> (
          std::size_t (dwSize) + std::size_t (1)
        );
      auto manifest_data =
        szManifestData.get ();

      if (! manifest_data)
        return "";

      const bool bRead =
        ReadFile ( hManifest,
                      manifest_data,
                        dwSize,
                      &dwRead,
                          nullptr );

      if (bRead && dwRead)
      {
        manifest =
          std::move (manifest_data);

        manifest_id = app->id;

        app->steam.manifest_data = manifest;
        app->steam.manifest_path = wszManifestFullPath;
        return app->steam.manifest_data;
      }
    }
  }

  app->steam.manifest_data =  "<InvalidData>";
  app->steam.manifest_path = L"<InvalidPath>";

  return "";
}


std::string
SKIF_Steam_GetUserConfigStore (SteamId3_t userid, ConfigStore config)
{
  static SteamId3_t  cachedUser  [ConfigStore_ALL] = { };
  static std::string cachedConfig[ConfigStore_ALL];
  const  wchar_t*    pathFormats [ConfigStore_ALL] = {
     LR"(%s\userdata\%i\config\localconfig.vdf)",   // ConfigStore_UserLocal
     LR"(%s\userdata\%i\7\remote\sharedconfig.vdf)" // ConfigStore_UserRoaming... AppID 7 == Steam Client
  };

  if (cachedUser[config] == userid && (! cachedConfig[config].empty()))
    return cachedConfig[config];

  // AppID: 7 == Steam Client
  wchar_t    wszConfig [MAX_PATH + 2] = { };
  swprintf ( wszConfig, MAX_PATH,
              pathFormats[config],
              SK_GetSteamDir ( ),
                        userid );

  if (wszConfig[0] == L'\0')
    return cachedConfig[config];


  // When opening an existing file, the CreateFile function performs the following actions:
  // [...] and ignores any file attributes (FILE_ATTRIBUTE_*) specified by dwFlagsAndAttributes.
  CHandle hConfig (
    CreateFileW (wszConfig,
                   GENERIC_READ,
                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr,        OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, // GetFileAttributesW (wszConfig)
                           nullptr ) );

  if (hConfig != INVALID_HANDLE_VALUE)
  {
    //PLOG_VERBOSE << "Reading " << wszLocalConfig;

    DWORD dwSizeHigh = 0,
          dwRead     = 0,
          dwSize     =
     GetFileSize (hConfig, &dwSizeHigh);

    auto szConfigData =
      std::make_unique <char []> (
        std::size_t (dwSize) + std::size_t (1)
      );
    auto Config_data =
       szConfigData.get ();

    if (! Config_data)
      return "";

    const bool bRead =
      ReadFile (hConfig,
                  Config_data,
                     dwSize,
                    &dwRead,
                       nullptr );

    if (bRead && dwRead)
    {
      cachedConfig[config] =
        std::move (Config_data);

      cachedUser[config] = userid;

      return
        cachedConfig[config];
    }
  }

  return cachedConfig[config];
}

std::string
SKIF_Steam_GetUserConfigStorePath (SteamId3_t userid, ConfigStore config)
{
  const  char*    pathFormats [ConfigStore_ALL] = {
     R"(%s\userdata\%i\config\localconfig.vdf)",   // ConfigStore_UserLocal
     R"(%s\userdata\%i\7\remote\sharedconfig.vdf)" // ConfigStore_UserRoaming... AppID 7 == Steam Client
  };

  char    szConfig [MAX_PATH + 2] = { };
  snprintf( szConfig, MAX_PATH,
                pathFormats[config],
              SK_GetSteamDirUTF8 ( ),
                        userid );

  return szConfig;
}

const wchar_t*
SK_GetSteamDir (void)
{
  static wchar_t
       wszSteamPath [MAX_PATH + 2] = { };
  if (*wszSteamPath == L'\0')
  {
    // Don't keep querying the registry if Steam is not installed
    wszSteamPath [0] = L'?';

    DWORD     len    =      MAX_PATH;

    // Rely on HKCU path first and foremost
    LSTATUS   status =
      RegGetValueW ( HKEY_CURRENT_USER,
                       LR"(SOFTWARE\Valve\Steam\)",
                                        L"SteamPath",
                         RRF_RT_REG_SZ,
                           nullptr,
                             wszSteamPath,
                               (LPDWORD)&len );

    // Use the HKCU path if it exists
    if (status == ERROR_SUCCESS && PathFileExists (wszSteamPath))
      return wszSteamPath;

    std::fill (std::begin (wszSteamPath), std::end(wszSteamPath), L'\0');

    // In case of issues with the HKCU path, try the HKLM path
    status =
      RegGetValueW ( HKEY_LOCAL_MACHINE,
                        LR"(SOFTWARE\Valve\Steam\)",
                                        L"InstallPath",
                          RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, // Steam stores this path in the Wow6432Node key
                            nullptr,
                              wszSteamPath,
                                (LPDWORD)&len );

    if (status == ERROR_SUCCESS)
      return wszSteamPath;

    return L"";
  }

  return wszSteamPath;
}

const char*
SK_GetSteamDirUTF8 (void)
{
  static char
       szSteamPath [MAX_PATH + 2] = { };
  if (*szSteamPath == L'\0')
    strncpy_s (szSteamPath, SK_WideCharToUTF8 (SK_GetSteamDir()).data(), MAX_PATH);

  return szSteamPath;
}

int
SK_Steam_GetLibraries (steam_library_t** ppLibraries)
{
  static bool            scanned_libs = false;
  static int             steam_libs   = 0;
  static steam_library_t steam_lib_paths [MAX_STEAM_LIBRARIES] = { };

  static const wchar_t* wszSteamPath;

  if (! scanned_libs)
  {
    wszSteamPath =
      SK_GetSteamDir ();

    if (wszSteamPath != nullptr)
    {
      PLOG_VERBOSE << "Steam Install Folder : " << std::wstring(wszSteamPath);

      wchar_t wszLibraryFolders [MAX_PATH + 2] = { };

      // Old: \steamapps\libraryfolders.vdf
      // New:    \config\libraryfolders.vdf
      lstrcpyW (wszLibraryFolders, wszSteamPath);
      lstrcatW (wszLibraryFolders, LR"(\config\libraryfolders.vdf)");

      // Some Steam installs still relies on the old file apparently,
      //   so if the new file does not exist we need to use the old one.
      if (! PathFileExists (wszLibraryFolders))
      {
        lstrcpyW (wszLibraryFolders, wszSteamPath);
        lstrcatW (wszLibraryFolders, LR"(\steamapps\libraryfolders.vdf)");
      }

      PLOG_VERBOSE << "Steam Library VDF    : " << std::wstring(wszLibraryFolders);

      // When opening an existing file, the CreateFile function performs the following actions:
      // [...] and ignores any file attributes (FILE_ATTRIBUTE_*) specified by dwFlagsAndAttributes.
      CHandle hLibFolders (
        CreateFileW ( wszLibraryFolders,
                        GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr,        OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, // GetFileAttributesW (wszLibraryFolders)
                                nullptr ) );

      if (hLibFolders != INVALID_HANDLE_VALUE)
      {
        DWORD dwSizeHigh = 0,
              dwRead     = 0,
              dwSize     =
         GetFileSize (hLibFolders, &dwSizeHigh);

        std::unique_ptr <char []>
          local_data;
        char*   data = nullptr;

        local_data =
          std::make_unique <char []> (static_cast<size_t>(dwSize) + 4u);
              data = local_data.get ();

        if (data == nullptr)
          return steam_libs;

        dwRead = dwSize;

        if (ReadFile (hLibFolders, data, dwSize, &dwRead, nullptr))
        {
          data [dwSize] = '\0';

          for (int i = 1; i < MAX_STEAM_LIBRARIES - 1; i++)
          {
            // Old libraryfolders.vdf format
            std::wstring lib_path =
              SK_Steam_KeyValues::getValueAsUTF16 (
                data, { "LibraryFolders" }, std::to_string (i)
              );

            if (lib_path.empty ())
            {
              // New (July 2021) libraryfolders.vdf format
              lib_path =
                SK_Steam_KeyValues::getValueAsUTF16 (
                  data, { "LibraryFolders", std::to_string (i) }, "path"
                );
            }

            if (! lib_path.empty ())
            {
              lib_path = SKIF_Util_NormalizeFullPath (lib_path);

              wcsncpy_s (
                (wchar_t *)steam_lib_paths [steam_libs++], MAX_PATH,
                                 lib_path.c_str (),       _TRUNCATE );
            }

            else
              break;
          }
        }
      }

      // Finally, add the default Steam library
      wcsncpy_s ( (wchar_t *)steam_lib_paths [steam_libs++],
                                               MAX_PATH,
                          wszSteamPath,       _TRUNCATE );

      for (int i = 0; i < steam_libs; i++)
        PLOG_VERBOSE << "Steam Library [" << i << "]    : " << std::wstring(steam_lib_paths[i]);
    }

    scanned_libs = true;
  }

  if (ppLibraries != nullptr)
    *ppLibraries = steam_lib_paths;

  return steam_libs;
}

std::string
SK_UseManifestToGetAppName (app_record_steam_s *app)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  if (app->steam.manifest_data.empty())
      app->steam.manifest_data =
        SK_GetManifestContentsForAppID (app);

  if (! app->steam.manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << app->id;

    std::string app_name =
      SK_Steam_KeyValues::getValue (
        app->steam.manifest_data, { "AppState" }, "name"
      );

    if (! app_name.empty ())
    {
      return app_name;
    }
  }

  return "";
}

std::string
SK_UseManifestToGetCurrentBranch (app_record_steam_s *app)
{
  if (app->steam.manifest_data.empty())
      app->steam.manifest_data =
        SK_GetManifestContentsForAppID (app);

  if (! app->steam.manifest_data.empty ())
  {
    std::string branch =
      SK_Steam_KeyValues::getValue (
        app->steam.manifest_data, { "AppState", "UserConfig" }, "BetaKey"
      );

    if (! branch.empty ())
    {
      return branch;
    }
  }

  return "";
}

std::string
SKIF_Steam_GetLaunchOptions (AppId_t appid, SteamId3_t userid , app_record_steam_s *app)
{
  // Clear values
  app->steam.local.launch_option.clear();
  app->steam.local.launch_option_parsed.clear();

  // Implementation using the ValveFileVDF project
  std::ifstream file (SKIF_Steam_GetUserConfigStorePath (userid, ConfigStore_UserLocal));

  if (file.is_open())
  {
    try
    {
      auto user_localconfig = tyti::vdf::read(file);
      file.close();

      if (user_localconfig.childs.size() > 0)
      {
        // LaunchOptions is tracked at "UserLocalConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "LaunchOptions"

        std::shared_ptr <tyti::vdf::object>
                                         apps_localconfig = SKIF_VDF_ciSearch ("Software", &user_localconfig.childs);
        if (apps_localconfig != nullptr) apps_localconfig = SKIF_VDF_ciSearch ("Valve",    &apps_localconfig->childs);
        if (apps_localconfig != nullptr) apps_localconfig = SKIF_VDF_ciSearch ("Steam",    &apps_localconfig->childs);
        if (apps_localconfig != nullptr) apps_localconfig = SKIF_VDF_ciSearch ("Apps",     &apps_localconfig->childs);

        /* case sensitive -- cannot be used
        auto& apps_localconfig =
          user_localconfig.
            childs.at("Software") ->
              childs.at("valve")  ->
                childs.at("Steam")->
                  childs.at("apps");
        */

        if (apps_localconfig != nullptr &&
            apps_localconfig->childs.size() > 0)
        {
          auto lc_app = apps_localconfig->childs.find(std::to_string(appid));
          if (lc_app != apps_localconfig->childs.end())
          {
            auto& attribs = lc_app->second->attribs;

            if (! attribs.empty() && attribs.count("LaunchOptions") > 0)
            {
              // Also updates the copy
              if (app != nullptr)
                app->steam.local.launch_option = attribs.at("LaunchOptions");

              return attribs.at("LaunchOptions");
            }
          }
        }
      }
    }

    // I don't expect this to throw any std::out_of_range exceptions any more
    //   but one can never be too sure when it comes to data structures like these.
    //     - Aemony
    catch (const std::exception& e)
    {
      UNREFERENCED_PARAMETER(e);
    }
  }

  return "";
}

bool
SKIF_Steam_PreloadUserLocalConfig (SteamId3_t userid, app_list_t *apps, std::set <std::string> *apptickets)
{
  if (apps->empty())
    return false;

  // Clear any cached app tickets to prevent stale data from sticking around
  apptickets->clear ( );

  // Clear out any cached launch option data
  for (auto& app : *apps)
  {
    if (app.second->store != app_record_s::Store::Steam)
      continue;

    // Reset any current data
    AsSteamRecord (app)->steam.local = { };
  }

  // Abort if the user signed out
  if (userid == 0)
    return false;

  PLOG_INFO << "Preloading Steam user local config...";

  // Implementation using the ValveFileVDF project
  std::ifstream file (SKIF_Steam_GetUserConfigStorePath (userid, ConfigStore_UserLocal));

  if (file.is_open())
  {
    try
    {
      auto user_localconfig = tyti::vdf::read(file);
      file.close();

      if (user_localconfig.childs.size() > 0)
      {
        // Preload LaunchOptions...
        // LaunchOptions are tracked at "UserLocalConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "LaunchOptions"

        std::shared_ptr <tyti::vdf::object>
                                         apps_localconfig = SKIF_VDF_ciSearch ("Software", &user_localconfig.childs);
        if (apps_localconfig != nullptr) apps_localconfig = SKIF_VDF_ciSearch ("Valve",    &apps_localconfig->childs);
        if (apps_localconfig != nullptr) apps_localconfig = SKIF_VDF_ciSearch ("Steam",    &apps_localconfig->childs);
        if (apps_localconfig != nullptr) apps_localconfig = SKIF_VDF_ciSearch ("Apps",     &apps_localconfig->childs);
      //if (apps_localconfig != nullptr) PLOG_VERBOSE << "Found a match!";

        /* case sensitive -- cannot be used
        auto& apps_localconfig =
          user_localconfig.
            childs.at("Software") ->
              childs.at("valve")  ->
                childs.at("Steam")->
                  childs.at("apps");
        */

        if (apps_localconfig != nullptr &&
            apps_localconfig->childs.size() > 0)
        {
          for (auto& app : *apps)
          {
            if (app.second->store != app_record_s::Store::Steam)
              continue;

            auto lc_app = apps_localconfig->childs.find(std::to_string(app.second->id));
            if (lc_app != apps_localconfig->childs.end())
            {
              auto& attribs = lc_app->second->attribs;

              if (! attribs.empty() && attribs.count("LaunchOptions") > 0)
                AsSteamRecord (app)->steam.local.launch_option = attribs.at("LaunchOptions");
            }
          }
        }

        // Preload DLC ownership...
        //
        // AppTickets are tracked at "UserLocalConfigStore" -> "apptickets" -> "<app-id>"
        // This is used to determine if a DLC related launch option should be visible
        std::shared_ptr <tyti::vdf::object>
          apptickets_localconfig =
            SKIF_VDF_ciSearch ("apptickets", &user_localconfig.childs);
            //user_localconfig.childs.at("apptickets"); // case sensitive -- cannot be used

        if (apptickets_localconfig != nullptr &&
            apptickets_localconfig->attribs.size() > 0)
        {
          // Naively assume an app ticket indicates ownership
          for (auto& child : apptickets_localconfig->attribs)
          {
            if (! child.first.empty())
            {
              try {
                apptickets->emplace (child.first);
              }
              catch (const std::exception& e)
              {
                UNREFERENCED_PARAMETER(e);
              }
            }
          }
        }

        return true;
      }
    }

    // I don't expect this to throw any std::out_of_range exceptions any more
    //   but one can never be too sure when it comes to data structures like these.
    //     - Aemony
    catch (const std::exception& e)
    {
      PLOG_ERROR << "Unknown error occurred when trying to parse localconfig.vdf: " << e.what ();
    }
  }

  return false;
}

bool
SKIF_Steam_PreloadUserSharedConfig (SteamId3_t userid, app_list_t *apps)
{
  if (apps->empty())
    return false;

  // Clear out any cached launch option data
  for (auto& app : *apps)
  {
    if (app.second->store != app_record_s::Store::Steam)
      continue;

    // Reset any current data
    AsSteamRecord (app)->steam.shared = { };
  }

  // Abort if the user signed out
  if (userid == 0)
    return false;

  PLOG_INFO << "Preloading Steam user roaming config...";

  // Implementation using the ValveFileVDF project
  std::ifstream file (SKIF_Steam_GetUserConfigStorePath (userid, ConfigStore_UserRoaming));

  if (file.is_open())
  {
    try
    {
      auto vdfConfig = tyti::vdf::read(file);
      file.close();

      if (vdfConfig.childs.size() > 0)
      {
        std::shared_ptr <tyti::vdf::object>
                                          vdfConfigAppsTree  = SKIF_VDF_ciSearch ("Software", &vdfConfig.childs);
        if (vdfConfigAppsTree != nullptr) vdfConfigAppsTree  = SKIF_VDF_ciSearch ("Valve",    &vdfConfigAppsTree->childs);
        if (vdfConfigAppsTree != nullptr) vdfConfigAppsTree  = SKIF_VDF_ciSearch ("Steam",    &vdfConfigAppsTree->childs);
        if (vdfConfigAppsTree != nullptr) vdfConfigAppsTree  = SKIF_VDF_ciSearch ("Apps",     &vdfConfigAppsTree->childs);
      //if (vdfConfigAppsTree != nullptr) PLOG_VERBOSE << "Found a match!";

        /* case sensitive -- cannot be used
        auto& vdfConfigAppsTree =
          vdfConfig. // UserRoamingConfigStore
            childs.at("Software") ->
              childs.at("Valve")  ->
                childs.at("Steam")->
                  childs.at("apps");
        */

        if (vdfConfigAppsTree != nullptr &&
            vdfConfigAppsTree->childs.size() > 0)
        {
          for (auto& app : *apps)
          {
            if (app.second->store != app_record_s::Store::Steam)
              continue;

            auto conf_app  = vdfConfigAppsTree->childs.find(std::to_string(app.second->id));
            if ( conf_app != vdfConfigAppsTree->childs.end())
            {
              // Load hidden state...
              // "UserRoamingConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "hidden" == 1
              auto& attribs = conf_app->second->attribs;
              if (! attribs.empty() && attribs.count("hidden") > 0 && attribs.at("hidden") == "1")
                AsSteamRecord (app)->steam.shared.hidden     = 1;

              // Load favorite state...
              // "UserRoamingConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "tags" -> "<order>" == "favorite"
              if (! conf_app->second->childs.empty() && conf_app->second->childs.count("tags") > 0)
              {
                auto& tags = conf_app->second->childs.at("tags");
                for (auto& child : tags->attribs)
                {
                  if (! child.first.empty() && child.second == "favorite")
                    AsSteamRecord (app)->steam.shared.favorite = 1;
                }
              }
            }
          }
        }

        return true;
      }
    }

    // I don't expect this to throw any std::out_of_range exceptions any more
    //   but one can never be too sure when it comes to data structures like these.
    //     - Aemony
    catch (const std::exception& e)
    {
      PLOG_ERROR << "Unknown error occurred when trying to parse sharedconfig.vdf: " << e.what ();
    }
  }

  return false;
}

void
SKIF_Steam_PreloadUserConfig (SteamId3_t userid, app_list_t* apps, std::set<std::string>* apptickets)
{
  SKIF_Steam_PreloadUserLocalConfig  (userid, apps, apptickets);
  SKIF_Steam_PreloadUserSharedConfig (userid, apps);
}

bool
SKIF_Steam_isSteamOverlayEnabled (AppId_t appid, SteamId3_t userid)
{
  std::ifstream file (SKIF_Steam_GetUserConfigStorePath (userid, ConfigStore_UserLocal));

  if (file.is_open())
  {
    try
    {
      auto user_localconfig = tyti::vdf::read(file);
      file.close();

      if (user_localconfig.childs.size() > 0)
      {
        // There are two relevant here:
        // - Global state is tracked at "UserLocalConfigStore" -> "system" -> "EnableGameOverlay"
        // - Game-specific state is tracked at "UserLocalConfigStore" -> "apps" -> "<app-id>" -> "OverlayAppEnable"

        std::shared_ptr <tyti::vdf::object>
          system_localconfig =
            SKIF_VDF_ciSearch ("system", &user_localconfig.childs);
          //user_localconfig.childs.at("system"); // case sensitive -- cannot be used


        // If global state is disabled, don't bother checking the local state
        if (! system_localconfig->attribs.empty() && system_localconfig->attribs.count("EnableGameOverlay") > 0 && system_localconfig->attribs.at("EnableGameOverlay") == "0")
          return false;

        // Continue checking the local state
        std::shared_ptr <tyti::vdf::object>
          apps_localconfig =
            SKIF_VDF_ciSearch ("apps", &user_localconfig.childs);
          //user_localconfig.childs.at("apps"); // case sensitive -- cannot be used

        if (apps_localconfig != nullptr &&
            apps_localconfig->childs.size() > 0)
        {
          auto lc_app = apps_localconfig->childs.find(std::to_string(appid));
          if (lc_app != apps_localconfig->childs.end())
          {
            auto& attribs = lc_app->second->attribs;

            if (! attribs.empty() && attribs.count("OverlayAppEnable") > 0 && attribs.at("OverlayAppEnable") == "0")
              return false;
          }
        }
      }
    }

    // I don't expect this to throw any std::out_of_range exceptions any more
    //   but one can never be too sure when it comes to data structures like these.
    //     - Aemony
    catch (const std::exception& e)
    {
      UNREFERENCED_PARAMETER(e);
    }
  }

  return true;
}

std::string
SK_UseManifestToGetAppOwner (app_record_steam_s *app)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (app);

  if (! manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

    std::string app_owner =
      SK_Steam_KeyValues::getValue (
        manifest_data, { "AppState" }, "LastOwner"
      );

    if (! app_owner.empty ())
    {
      return app_owner;
    }
  }

  return "";
}

std::wstring
SK_UseManifestToGetInstallDir (app_record_steam_s *app)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  if (! app->install_dir.empty())
    return app->install_dir;

  if (app->steam.manifest_data.empty())
    app->steam.manifest_data =
      SK_GetManifestContentsForAppID (app);

  if (! app->steam.manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

    std::wstring app_path =
      SK_Steam_KeyValues::getValueAsUTF16 (
        app->steam.manifest_data, { "AppState" }, "installdir"
      );

    if (! app_path.empty ())
    {
      wchar_t    app_root [MAX_PATH + 2] = { };
      wcsncpy_s (app_root, MAX_PATH,
            app->steam.manifest_path.c_str (), _TRUNCATE);

      PathRemoveFileSpecW (app_root);
      PathAppendW         (app_root, L"common\\");

      wchar_t path [MAX_PATH + 2] = { };

      PathCombineW (path, app_root,
                          app_path.c_str ());

      app->install_dir = SKIF_Util_NormalizeFullPath (path);
    }
  }

  return app->install_dir;
}

std::vector <SK_Steam_Depot>
SK_UseManifestToGetDepots (app_record_steam_s *app)
{
  std::vector <SK_Steam_Depot> depots;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (app);

  if (! manifest_data.empty ())
  {
    std::vector <std::string> values;
    auto                      mounted_depots =
      SK_Steam_KeyValues::getKeys (
        manifest_data, { "AppState", "MountedDepots" }, &values
      );

    int idx = 0;

    for ( auto& it : mounted_depots )
    {
      depots.push_back (
        SK_Steam_Depot {
          "", static_cast <uint32_t> (atoi  (it            .c_str ())),
              static_cast <uint64_t> (atoll (values [idx++].c_str ()))
        }
      );
    }
  }

  return depots;
}

ManifestId_t
SK_UseManifestToGetDepotManifest (app_record_steam_s *app, DepotId_t depot)
{
  std::string manifest_data =
    SK_GetManifestContentsForAppID (app);

  if (! manifest_data.empty ())
  {
    return
      atoll (
        SK_Steam_KeyValues::getValue (
          manifest_data, {
            "AppState", "InstalledDepots", std::to_string (depot)
          },
          "manifest"
        ).c_str ()
      );
  }

  return 0;
}

bool
SKIF_Steam_areLibrariesSignaled (void)
{
  bool isSignaled = false;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);

  extern HWND      SKIF_Notify_hWnd;

  if (SKIF_Notify_hWnd == NULL)
    return false;

  if (! steam_lib_paths)
    return false;

  if (steam_libs == 0)
    return false;

  if (! g_SteamLibrariesParsed.load())
    return false;

  if (! TryEnterCriticalSection (&VFSManifestSection))
    return false;

  for (int i = 0; i < steam_libs; i++)
  {
    auto& library =
      steam_libraries[i];

    // SKIF_FrameCount iterates at the start of the frame, so even the first frame will be frame count 1
    if (library.frame_last_scanned == 0)
    {
      swprintf (library.path, MAX_PATH,
                    LR"(%s\steamapps)",
                (wchar_t *)steam_lib_paths [i] );

      library.timer = static_cast <UINT_PTR>(1983 + i); // 1983-1999
    }

    bool countFiles = (library.frame_last_scanned == 0);

    // If we detect any changes, delay checking the details for a couple of seconds
    if (library.watch.isSignaled (library.path)) // UITab_Library // We do not wake up when unfocused as that causes SKIF to constantly be active during downloads/updates
    {
      library.signaled = SKIF_Util_timeGetTime ( );

      // Create a timer to trigger a refresh after the time has expired
      SetTimer (SKIF_Notify_hWnd, library.timer, 2500 + 50, NULL);
    }

    // Only refresh if there has been no further changes to the folder recently
    if (0 < library.signaled && library.signaled + 2500 < SKIF_Util_timeGetTime ( ))
    {
      KillTimer (SKIF_Notify_hWnd, library.timer);
      library.signaled = 0;
      countFiles = true;
    }

    if (countFiles)
    {
      library.frame_last_scanned = SKIF_FrameCount.load();

      int prevCount = library.count;

      // Clear out any existing paths
      library.manifest_vfs.clear();

      library.count =
        SK_VFS_ScanTree ( library.manifest_vfs,
                          library.path, L"appmanifest_*.acf", 0);

      if (library.count != prevCount)
        isSignaled = true;
    }
  }

  LeaveCriticalSection (&VFSManifestSection);

  return isSignaled;
};


// This is an internal helper function used by SKIF_Steam_GetInstalledAppIDs ( ).
// This function discovers and returns an unprocessed vector of all apps on the system.
static std::vector <AppId_t>
SKIF_SteamInt_DiscoverInstalledApps (void)
{
  std::vector <AppId_t> apps;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);

  if (! steam_lib_paths)
    return apps;

  bool bHasSpecialK = false;

  int frame_count_ = SKIF_FrameCount.load();

  if (steam_libs != 0)
  {
    EnterCriticalSection (&VFSManifestSection);

    // Scan through the libraries first for all appmanifest files they contain
    for (int i = 0; i < steam_libs; i++)
    {
      auto& library =
        steam_libraries[i];

      // SKIF_FrameCount iterates at the start of the frame, so even the first frame will be frame count 1
      if (library.frame_last_scanned == 0)
      {
        swprintf (library.path, MAX_PATH + 2,
                      LR"(%s\steamapps)",
                  (wchar_t *)steam_lib_paths [i] );

        library.timer = static_cast <UINT_PTR>(1983 + i); // 1983-1999
      }

      // Scan the folder if we haven't already done so during this frame
      if (library.frame_last_scanned != frame_count_)
      {
        library.frame_last_scanned    = frame_count_;

        // Clear out any existing paths
        library.manifest_vfs.clear();

        library.count =
          SK_VFS_ScanTree ( library.manifest_vfs,
                            library.path, L"appmanifest_*.acf", 0);
      }

      SK_VirtualFS::vfsNode* pFolder =
        library.manifest_vfs;

      // Really, this will just iterate once, across pFolder->children[0]
      //   ...as long as SKIF doesn't dynamically recognize
      //        new Steam libraries during runtime, that is...
      for (const auto& folder : pFolder->children)
      {
        // Now add the App IDs of all manifests that are installed,
        //   and also check for Special K ownership on Steam...
        for (const auto& file : folder.second->children)
        {
          uint32_t appid;

          if ( swscanf (file.first.c_str(),
                            L"appmanifest_%lu.acf",
                              &appid ) == 1 )
          {
            apps.push_back (appid);

            if (appid == 1157970)
              bHasSpecialK = true;
          }
        }
      }
    }

    g_SteamLibrariesParsed.store (true);

    LeaveCriticalSection (&VFSManifestSection);
  }

  if (bHasSpecialK)
  {
    static bool
          bInit = false;
    if (! bInit)
    {
      CreateThread (nullptr, 0x0, [](LPVOID)->DWORD
      {
        static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

        // We don't want Steam to draw its overlay on us
        _registry._LoadedSteamOverlay = true;
        SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");

        // Store the current state of the environment variables
        auto env_str = GetEnvironmentStringsW ( );

        static bool bLoaded =
          (LoadLibraryW (L"steam_api64.dll") != nullptr);

        if (bLoaded)
        {
          using  SteamAPI_Init_pfn = bool (__cdecl *)(void);
          static SteamAPI_Init_pfn
                _SteamAPI_Init     = nullptr;

          if (_SteamAPI_Init == nullptr)
          {   _SteamAPI_Init =
              (SteamAPI_Init_pfn)GetProcAddress (GetModuleHandleW (
                 SK_RunLHIfBitness ( 64, L"steam_api64.dll",
                                         L"steam_api.dll" )
           ), "SteamAPI_Init");
          }

          if (_SteamAPI_Init != nullptr)
          {
            std::ofstream ("steam_appid.txt") << std::to_string (1157970);

            if (_SteamAPI_Init ())
            {
              DeleteFileW (L"steam_appid.txt");
              bInit = true;
            }
          }
        }

        // Restore the state (clears out any additional Steam set variables)
        SetEnvironmentStringsW  (env_str);
        FreeEnvironmentStringsW (env_str);

        // If the DLL file could not be loaded, go back to regular handling
        if (! bLoaded)
        {
          SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);
          _registry._LoadedSteamOverlay = false;
        }

        return 0;
      }, nullptr, 0x0, nullptr);
    }
  }

  return apps;
}


// Updates the given vector with a filtered list of all discovered Steam apps.
// Filters out various non-game type of apps.
void
SKIF_Steam_GetInstalledAppIDs (app_list_t *apps)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  std::set <uint32_t> unique_apps;

  PLOG_INFO << "Detecting Steam games...";

  for (auto app : SKIF_SteamInt_DiscoverInstalledApps ( ))
  {
    // Skip Steamworks Common Redists
    if (app == 228980) continue;

    // Skip IDs related to apps, DLCs, music, and tools (including Special K for now)
    if (std::find(std::begin(steam_apps_ignorable), std::end(steam_apps_ignorable), app) != std::end(steam_apps_ignorable)) continue;

    if (unique_apps.emplace (app).second)
    {
      app_record_steam_s record (app);
      record.store      = app_record_s::Store::Steam;
      record.store_utf8 = "Steam";

      // Opening the manifests to read the names is a
      //   lengthy operation, so defer names and icons
      //   (LibraryWorkerThread resolves the name)
      apps->emplace_back (
        "", std::make_unique <app_record_steam_s> (record)
      );
    }
  }
};


// NOT THREAD-SAFE!!!
bool
SKIF_Steam_HasActiveProcessChanged (app_list_t *apps, std::set <std::string> *apptickets)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_RegistryWatch SteamActiveProcess (HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess)", L"SteamActiveUser", FALSE);
  static bool               firstRun = true;

  if (SteamActiveProcess.isSignaled () || firstRun)
  {
    WCHAR                    szData [255] = { };
    DWORD   dwSize = sizeof (szData);
    PVOID   pvData =         szData;
    CRegKey hKey ((HKEY)0);

    SteamId3_t oldUserID = g_SteamUserID;
    DWORD      oldProcID = g_dwSteamProcessID;
    firstRun         = false;

    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey.m_hKey) == ERROR_SUCCESS)
    {
      if (RegGetValueW (hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
        g_SteamUserID      = *(DWORD*)pvData;

      // Update SKIF's fallback registry value if the user is new
      if (g_SteamUserID != 0 && g_SteamUserID != _registry.uiSteamUser)
      {
        _registry.uiSteamUser = g_SteamUserID;
        _registry.regKVSteamUser.putData (g_SteamUserID);
      }

      // If the registry does not hold a value, fall back to using the last recognized user
      else if (g_SteamUserID == 0)
        g_SteamUserID = _registry.uiSteamUser;

      if (RegGetValueW (hKey, NULL, L"pid", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
        g_dwSteamProcessID = *(DWORD*)pvData;

      hKey.Close ( );
    }

    // Refresh stuff if the current user has changed
    if ((g_SteamUserID != oldUserID || g_dwSteamProcessID != oldProcID)
               && apps != nullptr   &&         apptickets != nullptr)
    {
      // Preload user's local config
      SKIF_Steam_PreloadUserConfig (g_SteamUserID, apps, apptickets);

      // Reset DLC ownership
      for (auto& app : *apps)
        for (auto& launch_cfg : app.second->launch_configs)
          launch_cfg.second.owns_dlc = -1;
    }

    return (g_SteamUserID != oldUserID || g_dwSteamProcessID != oldProcID);
  }

  return false;
};


// Get SteamID3 of the signed in user.
SteamId3_t
SKIF_Steam_GetCurrentUser (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return (g_SteamUserID != 0) ? g_SteamUserID : _registry.uiSteamUser;
}

DWORD
SKIF_Steam_GetActiveProcess (void)
{
  return g_dwSteamProcessID;
}

std::wstring
SKIF_Steam_GetAppStateString (HKEY* hKey, const wchar_t *wszStateKey)
{
  std::wstring str ( MAX_PATH, L'\0' );
  DWORD        len = MAX_PATH;
  LSTATUS   status =
    RegGetValueW ( *hKey,
                     NULL,
                       wszStateKey,
                         RRF_RT_REG_SZ,
                           nullptr,
                             str.data (),
                               &len );

  if (status == ERROR_SUCCESS)
    return str;
  else
    return L"";
}

wchar_t
SKIF_Steam_GetAppStateDWORD (HKEY* hKey, const wchar_t *wszStateKey, DWORD *pdwStateVal)
{
  DWORD     len    = sizeof (DWORD);
  LSTATUS   status =
    RegGetValueW ( *hKey,
                     NULL,
                       wszStateKey,
                         RRF_RT_DWORD,
                           nullptr,
                             pdwStateVal,
                               &len );

  if (status == ERROR_SUCCESS)
    return TRUE;
  else
    return FALSE;
}

bool
SKIF_Steam_UpdateAppState (app_record_s *pApp)
{
  if (! pApp)
    return false;

  HKEY hKey = nullptr;

  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_CURRENT_USER, SK_FormatStringW (LR"(SOFTWARE\Valve\Steam\Apps\%lu)", pApp->id).c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey))
  {
    std::scoped_lock app_lock  (SKIF_GamingCollection::Get().GetMutex());

    SKIF_Steam_GetAppStateDWORD (
      &hKey,   L"Installed",
      &pApp->_status.installed);

    if (pApp->_status.installed != 0x0)
    {   pApp->_status.running    = 0x0;

      SKIF_Steam_GetAppStateDWORD (
        &hKey,   L"Running",
        &pApp->_status.running);

      if (! pApp->_status.running)
      {
        SKIF_Steam_GetAppStateDWORD (
          &hKey,   L"Updating",
          &pApp->_status.updating);
      }

      else
      {
        pApp->_status.updating = 0x0;
      }

      if (pApp->names.normal.empty ())
      {
        std::wstring wide_name =
          SKIF_Steam_GetAppStateString (
            &hKey,   L"Name"
          );

        if (! wide_name.empty ())
        {
          pApp->names.normal =
            SK_WideCharToUTF8 (wide_name);
        }
      }
    }

    RegCloseKey (hKey);
  }

  return true;
}

void
SKIF_Steam_IdentifyAssetPCGW (uint32_t app_id)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring targetAssetPath = SK_FormatStringW(LR"(%ws\Assets\Steam\%i\)", _path_cache.specialk_userdata, app_id);

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            targetAssetPath, ec))
        std::filesystem::create_directories (targetAssetPath, ec);

  // Do not load a high-res copy if low-res covers are being used,
  //   as in those scenarios we prefer to load the original Steam cover
  if (! _registry._UseLowResCovers || _registry._UseLowResCoversHiDPIBypass)
  {

    // We intend to download PCGW's cover
    // https://www.pcgamingwiki.com/w/api.php?action=cargoquery&format=json&tables=Infobox_game&fields=Cover_URL&where=Steam_AppID+HOLDS+'220'
    // https://www.pcgamingwiki.com/w/api.php?action=cargoquery&format=json&tables=Infobox_game&fields=Cover_URL&where=Steam_AppID%20HOLDS%20%27220%27
    std::wstring url  = L"https://specialk.aemony.workers.dev/?p=steam&id=";
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
      catch (const std::exception& e)
      {
        PLOG_ERROR << "Error parsing PCGW JSON! " << e.what ();
      }

      // Delete the JSON file when we are done
      if (_registry.iLogging < 5)
        DeleteFile ((targetAssetPath + L"pcgw.json").c_str());
    }
  }
}

std::wstring
SKIF_Steam_GetCoverURI (uint32_t app_id, bool force_2x)
{
  const std::string SteamCDN = "https://shared.fastly.steamstatic.com/store_item_assets/";
  const std::initializer_list<const char*> countryCodes = { "US", "UK", "AU", "FR", "PL", "DE", "JP", "CN", "RU" };
  const std::string pattern = "${FILENAME}";
        std::string asset    = "";

  for (const auto& cc : countryCodes)
  {
    std::wstring request =
      SKIF_SteamWebAPI_IStoreBrowseService_GetItems1 (app_id, cc);

    std::string response;

    PLOG_DEBUG << "Downloading Steam GetItems v1 JSON: " << request;

    SKIF_Util_GetWebResource (request, L"", L"GET", L"", "", L"", &response);

    try
    {
      nlohmann::json jf = nlohmann::json::parse (response);

      //PLOG_DEBUG << "Retrieved Steam GetItem v1 JSON: " << jf.dump();

      if (jf.is_discarded ( ))
      {
        PLOG_ERROR << "Could not read Steam GetItems v1 JSON!";
      }

      else
      {
        const auto& item = jf["response"]["store_items"][0];

        if (item.value ("unavailable_for_country_restriction", false))
          continue;

        std::string
        //store_url_path           = item          .value ("store_url_path",     std::string{}), // app/1675830/1000xRESIST
          asset_url_format         = item["assets"].value ("asset_url_format",   std::string{}), // steam/apps/2483190/${FILENAME}?t=1779221600
          asset_library_capsule_2x = item["assets"].value ("library_capsule_2x", std::string{}), // library_600x900_2x.jpg
          asset_library_capsule    = item["assets"].value ("library_capsule",    std::string{}); // library_600x900.jpg
        //asset_header             = item["assets"].value ("header",             std::string{}); // header.jpg

        if (asset_library_capsule_2x.empty() && asset_library_capsule.empty())
          continue;

        asset = (! asset_library_capsule_2x.empty())
                 ? asset_library_capsule_2x
                 : (force_2x                    // Force library_600x900_2x.jpg even if only library_600x900.jpg is set,
                    ? "library_600x900_2x.jpg"  //   as apparently some games are just weird like that, such as Returnal (1649240) and Forbidden West (2420110).
                    : asset_library_capsule);

        asset = SteamCDN + asset_url_format.replace (asset_url_format.find (pattern), pattern.length(), asset);

        PLOG_VERBOSE << "asset_url_format: " << asset_url_format;
        PLOG_VERBOSE << "asset: " << asset;

        break;
      }
    }
    catch (const std::exception& e)
    {
      PLOG_ERROR << "Error parsing Steam GetItems v1 JSON! " << e.what ();
      continue;
    }
  }

  return SK_UTF8ToWideChar (asset);
}

std::wstring
SKIF_SteamWebAPI_AppDetails (uint32_t app_id)
{
  const std::wstring URI = L"https://store.steampowered.com/api/appdetails/";

  std::wstring request =
    URI +
    L"?appids=" + std::to_wstring (app_id);

  return request;
}

std::wstring
SKIF_SteamWebAPI_IStoreBrowseService_GetItems1 (uint32_t app_id, std::string country_code)
{
  const std::wstring URI = L"https://api.steampowered.com/IStoreBrowseService/GetItems/v1/";

  nlohmann::json payload =
  {
    {"ids", {
      {
        {"appid", app_id}
      }
    }},
    {"context", {
      {"country_code", country_code},
      {"language", "english"}
    }},
    {"data_request", {
      {"include_assets",                   true},
      {"include_release",                 false},
      {"include_platforms",               false},
      {"include_all_purchase_options",    false},
      {"include_screenshots",             false},
      {"include_trailers",                false},
      {"include_ratings",                 false},
      {"include_reviews",                 false},
    //{"include_tag_count",                   1}, // omitted so it excludes tags entirely
      {"include_basic_info",              false},
      {"include_supported_languages",     false},
      {"include_full_description",        false},
      {"include_included_items",          false},
      {"include_assets_without_overrides", true},
      {"include_links",                   false}
    }}
  };

  std::string payloadText =
    SKIF_Util_URLEncode (payload.dump());

  std::wstring request =
    URI +
    L"?input_json=" + SK_UTF8ToWideChar (payloadText);

  return request;
}

std::wstring
SKIF_SteamWebAPI_ISteamUserStats_GetNumberOfCurrentPlayers1 (uint32_t app_id)
{
  const std::wstring URI = L"https://api.steampowered.com/ISteamUserStats/GetNumberOfCurrentPlayers/v1/";

  std::wstring request =
    URI +
    L"?appid=" + std::to_wstring (app_id);

  return request;
}

// Polls the Steam client (through the registry) for this game's running/updating state,
//   unless a delay is active after a recent state change
void
app_record_steam_s::RefreshClientState (DWORD dwTimeNow)
{
  extern bool steamRunning; // tabs/library.cpp

  if (! steamRunning)
    return;

  if (_status.dwTimeDelayChecks != 0)
  {
    if (_status.dwTimeDelayChecks < dwTimeNow)
      _status.dwTimeDelayChecks = 0;
  }

  else
    _status.refresh (this);
}


#pragma region Launching

std::wstring
app_record_steam_s::GetJumpListIconPath (launch_config_s* config) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  UNREFERENCED_PARAMETER (config);

  return SK_FormatStringW (LR"(%ws\Assets\Steam\%i\icon-original.ico)",  _path_cache.specialk_userdata, id);
}

std::string
app_record_steam_s::GetStoreLaunchOptions (uint32_t steamAppID)
{
  return SKIF_Steam_GetLaunchOptions (steamAppID, SKIF_Steam_GetCurrentUser(), this);
}

bool
app_record_steam_s::PrefersInstantLaunch (bool fromGameMenu, launch_config_s* config) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // Convert all except those where we are dealing with an invalid launch config (e.g. Link2EA)
  return (! fromGameMenu && UsesInstantPlay (GetInstantPlayDefault ( )) && config->isExecutableFullPathValid ( ));
}

// Launch Steam game (regular), unless an instant launch was requested
void
app_record_steam_s::Launch (const launch_context_s& ctx)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (ctx.instant)
  {
    LaunchInstant (ctx);
    return;
  }

  PLOG_VERBOSE << "Performing a Steam launch...";

  bool launchDecision = true;

  // Check localconfig.vdf if user is attempting to launch without Special K
  if (! ctx.usingSK && ! ctx.localInjection)
  {
    if (SKIF_Steam_GetLaunchOptions (id, SKIF_Steam_GetCurrentUser(), this).size() > 0)
    {
      if (SKIF_Util_ToLower (steam.local.launch_option).find("skif ") == 0)
      {
        launchDecision = false;

        std::string confirmCopy = steam.local.launch_option;

        // Escape any percent signs (%)
        for (auto pos  = confirmCopy.find ('%');          // Find the first occurence
                  pos != std::string::npos;                  // Validate we're still in the string
                  confirmCopy.insert      (pos, R"(%)"),  // Escape the character
                  pos  = confirmCopy.find ('%', pos + 2)) // Find the next occurence
        { }

        SKIF_ImGui_InfoMessage ("Conflicting configuration",
                                "Could not launch game due to conflicting launch options in Steam:\n"
                                "\n"
                              +  confirmCopy + "\n"
                                "\n"
                                "Please change the launch options in Steam before trying again.");

        PLOG_WARNING << "Steam game " << id << " (" << names.normal << ") was unable to launch due to a conflict with the launch option of Steam: " << confirmCopy;
      }
    }
  }

  if (launchDecision)
  {
    // steam://run/1289310/          <- Always launches using the developer-specified primary launch configuration
    // steam://launch/1289310/dialog <- For games with multiple launch configurations, opens the launch config dialog or uses the user-specified default launch configuration
    std::wstring launchOptions = SK_FormatStringW (LR"(steam://launch/%d/dialog)", id);
    if (SKIF_Util_OpenURI (launchOptions) != 0)
    {
      std::scoped_lock app_lock (_games.GetMutex());

      // Don't check the running state for at least 7.5 seconds
      _status.dwTimeDelayChecks = ctx.timeNow + 7500;
      _status.running           = true;

      std::wstring iconPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\icon-original.ico)",  _path_cache.specialk_userdata, id);

      SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal), L"", launchOptions, L"", iconPath, (! ctx.localInjection && ctx.usingSK));
    }
  }
}

#pragma endregion

#pragma region Library UI

bool
app_record_steam_s::GetInstantPlayDefault (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return _registry.bInstantPlaySteam;
}

std::wstring
app_record_steam_s::GetStoreAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, id);
}

std::string
app_record_steam_s::GetLocalizedName (void)
{
  return SK_UseManifestToGetAppName (this);
}

bool
app_record_steam_s::CanCreateDesktopShortcut (void)
{
  return CanLaunchExecutableDirectly ( );
}

std::wstring
app_record_steam_s::GetDesktopShortcutArgs (void)
{
  return app_record_s::GetDesktopShortcutArgs ( ) + L" SKIF_SteamAppId=" + std::to_wstring (id);
}

std::vector <std::pair <std::string, std::string>>
app_record_steam_s::GetStoreDebugFields (void) const
{
  return {
    { "Branch",           steam.branch                                    },
    { "Launch Option",    steam.local.launch_option                       },
    { "Manifest Path",    SK_WideCharToUTF8 (steam.manifest_path)         },
    { "CPU Architecture", std::to_string ((int)common_config.cpu_type)    },
    { "Favorited",        std::to_string (steam.shared.favorite)          },
    { "Hidden",           std::to_string (steam.shared.hidden)            }
  };
}

#pragma endregion

#pragma region Context Menu

void
app_record_steam_s::DrawStoreMenu (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  if (ImGui::BeginMenuEx ("Steam", ICON_FA_STEAM))
  {
    if (SKIF_ImGui_MenuItemEx2 ("Details", ICON_FA_BARS_STAGGERED, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      SKIF_Util_OpenURI ((L"steam://nav/games/details/" + std::to_wstring (id)).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://nav/games/details/%lu", id));

    if (SKIF_ImGui_MenuItemEx2 ("Steam Input", ICON_FA_GAMEPAD, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      SKIF_Util_OpenURI ((L"steam://controllerconfig/" + std::to_wstring (id)).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverTip        ("A controller must be connected.");
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://controllerconfig/%lu", id));

    if (SKIF_ImGui_MenuItemEx2 ("Game properties", ICON_FA_WRENCH, ImColor(200, 200, 200, 255)))
      SKIF_Util_OpenURI ((L"steam://gameproperties/" + std::to_wstring (id)).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://gameproperties/%lu", id));

    ImGui::EndMenu     ( );
  }
}

void
app_record_steam_s::DrawWebsiteMenuItems (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  if (SKIF_ImGui_MenuItemEx2 ("Steam Community", ICON_FA_STEAM_SYMBOL, ((_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255))))
    SKIF_Util_OpenURI ((L"https://steamcommunity.com/app/" + std::to_wstring (id)).c_str());

  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("https://steamcommunity.com/app/%lu", id));

  if (SKIF_ImGui_MenuItemEx2 ("SteamDB", ICON_FA_DATABASE, ImColor(101, 192, 244, 255)))
    SKIF_Util_OpenURI ((L"https://steamdb.info/app/" + std::to_wstring (id)).c_str());

  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("https://steamdb.info/app/%lu", id));
}

void
app_record_steam_s::DrawDeveloperDataMenus (void)
{
  if (SKIF_ImGui_BeginMenuEx2 ("Steam Web API", ICON_FA_DATABASE))
  {
    ImGui::PushID         ("#Storefront");

    // ---

    if (steam.urls.AppDetails.empty())
    {
      steam.urls.AppDetails      = SKIF_SteamWebAPI_AppDetails (id);
      steam.urls.AppDetails_utf8 = SK_WideCharToUTF8 (steam.urls.AppDetails);
    }

    if (SKIF_ImGui_MenuItemEx2 ("/AppDetails/", (const char*)u8"\u2022", ImColor(101, 192, 244, 255)))
      SKIF_Util_OpenURI (steam.urls.AppDetails.c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (steam.urls.AppDetails_utf8.c_str());

    // ---

    if (steam.urls.ISteamUserStats_GetNumberOfCurrentPlayers1.empty())
    {
      steam.urls.ISteamUserStats_GetNumberOfCurrentPlayers1 = SKIF_SteamWebAPI_ISteamUserStats_GetNumberOfCurrentPlayers1 (id);
      steam.urls.ISteamUserStats_GetNumberOfCurrentPlayers1_utf8 = SK_WideCharToUTF8 (steam.urls.ISteamUserStats_GetNumberOfCurrentPlayers1);
    }

    if (SKIF_ImGui_MenuItemEx2 ("/ISteamUserStats/GetNumberOfCurrentPlayers/v1/", (const char*)u8"\u2022", ImColor(101, 192, 244, 255)))
      SKIF_Util_OpenURI (steam.urls.ISteamUserStats_GetNumberOfCurrentPlayers1.c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (steam.urls.ISteamUserStats_GetNumberOfCurrentPlayers1_utf8.c_str());

    // ---

    if (steam.urls.IStoreBrowseService_GetItems1.empty())
    {
      steam.urls.IStoreBrowseService_GetItems1      = SKIF_SteamWebAPI_IStoreBrowseService_GetItems1 (id, "US");
      steam.urls.IStoreBrowseService_GetItems1_utf8 = SK_WideCharToUTF8 (steam.urls.IStoreBrowseService_GetItems1);
    }

    if (SKIF_ImGui_MenuItemEx2 ("/IStoreBrowseService/GetItems/v1/", (const char*)u8"\u2022", ImColor(101, 192, 244, 255)))
      SKIF_Util_OpenURI (steam.urls.IStoreBrowseService_GetItems1.c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (steam.urls.IStoreBrowseService_GetItems1_utf8.c_str());

    // ---

    ImGui::PopID        ( );
    ImGui::EndMenu      ( );
  }
}

void
app_record_steam_s::DrawDeveloperLaunchConfigItems (launch_config_s& launch)
{
  if (launch.id_steam != -1)
  {
    
    ImGui::Separator ( );

    ImGui::PushID       ("#Steam");
    ImGui::TextDisabled ("Steam Data");
    if (ImGui::MenuItem ("Application ID", std::to_string(launch.id_steam).c_str()))
      SKIF_Util_SetClipboardData (std::to_wstring(launch.id_steam));
    ImGui::PopID        ( );
  }
}

#pragma endregion

#pragma region Library Population

void
app_record_steam_s::RefreshInstallState (void)
{
  if (SKIF_Steam_UpdateAppState (this))
    _status.dwTimeLastChecked = SKIF_Util_timeGetTime1 ( ) + 333UL; // _RefreshInterval

  // Set uninstalled apps id to 0 so that they are filtered out
  if (! _status.installed)
    id = 0;
}

// This populates install_dir for Steam games
void
app_record_steam_s::ResolveInstallDirectory (void)
{
  SK_UseManifestToGetInstallDir (this);
}

// Preload active branch for Steam games
void
app_record_steam_s::ResolveStoreDetails (void)
{
  steam.branch = SK_UseManifestToGetCurrentBranch (this);

  if (! steam.branch.empty())
    PLOG_VERBOSE << "App ID " << id << " has active branch : " << steam.branch;
}

// Parse appinfo data for the current game
// This must be done from the main thread since it also manipulates the g_apps array
void
app_record_steam_s::LoadStoreDetails (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (! processed)
    appinfo->getAppInfo ( id, &_games.GetApps() );
}

// Do not check games that are being updated (aka installed)
//   nor expose the option for shell execute based games (e.g. Link2EA, which requires the Steam client running)
bool
app_record_steam_s::CanLaunchExecutableDirectly (void)
{
  if (launch_configs.empty ( ) || _status.updating)
    return false;

  return launch_configs.begin ( )->second.isExecutableFullPathValid ( );
}

#pragma endregion

#pragma region Run State

app_record_s::ProcessMatch
app_record_steam_s::MatchProcess (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen)
{
  extern bool steamFallback; // tabs/library.cpp

  UNREFERENCED_PARAMETER (wszExeFile);

  if (dwExePathLen != 0 && _wcsnicmp (launch_configs[0].getExecutableFullPath ( ).c_str(), wszExePath, dwExePathLen) == 0)
  {
    // Only set the running state if the primary registry monitoring is unavailable
    return (steamFallback) ? ProcessMatch::Running : ProcessMatch::PidOnly;
  }

  return ProcessMatch::None;
}

bool
app_record_steam_s::KeepsRunningStateDuringScan (void) const
{
  extern bool steamRunning;  // tabs/library.cpp
  extern bool steamFallback; // tabs/library.cpp

  return (steamRunning || ! steamFallback);
}

bool
app_record_steam_s::IsRunningStateFromClient (void) const
{
  extern bool steamRunning;  // tabs/library.cpp
  extern bool steamFallback; // tabs/library.cpp

  return (steamRunning && ! steamFallback);
}

#pragma endregion

#pragma region Client Launch Options

// Builds the command line from the launch options set in the Steam client when they use %COMMAND%
std::string
app_record_steam_s::GetClientCommandLine (launch_config_s* pLaunchCfg)
{
  if (! steam.local.launch_option_parsed.empty())
    return steam.local.launch_option_parsed;

  // Check if there is a custom launch option set up
  if (steam.local.launch_option.size() > 0)
  {
    std::string strSteam_Command  = "%command%"; // Only for Steam games
    size_t start_pos = SKIF_Util_ToLower (steam.local.launch_option).find (strSteam_Command);

    // Check if a %COMMAND% is a part of the launch options
    // %COMMAND% is special in that the original developer-specified launch options are placed at its exact position,
    //   making the user-specified Steam launch options the _primary_ and the developer-specified launch options _secondary_
    if (start_pos != std::string::npos)
    {
      // Base is dev-specified executable
      std::string newCmdLine = pLaunchCfg->getExecutableFullPathUTF8 ( );
      std::string strSteamLO = steam.local.launch_option;

      // Appended is dev-specified cmd line arguments
      if (! pLaunchCfg->getLaunchOptionsUTF8().empty())
        newCmdLine += " " + pLaunchCfg->getLaunchOptionsUTF8();

      // Replace %command% with the dev-specified exe and args
      strSteamLO.replace (start_pos, strSteam_Command.length(), newCmdLine);

      // Swap in the result and flag to use shell execute
      steam.local.launch_option_parsed = strSteamLO;
    }
  }

  return steam.local.launch_option_parsed;
}

#pragma endregion

#pragma region Special K Compatibility

// Exclude the local DLL update check for games with known older versions
bool
app_record_steam_s::HasLegacySpecialKVersion (void) const
{
  return (id == 405900 || // Disgaea PC
        //id == 359870 || // FFX/X-2 HD Remaster  // Not compatible with the latest version as of 2026-10-01
        //id == 578330 || // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
          id == 429660 || // Tales of Berseria
          id == 372360 || // Tales of Symphonia
        //id == 738540 || // Tales of Vesperia DE // Do not exclude from the updater as mainline SK further improves the game
          id == 351970);  // Tales of Zestiria
}

// Expose installer for those games with better specific mods
bool
app_record_steam_s::HasSpecialKModInstaller (void) const
{
  return (//id == 405900 || // Disgaea PC
          //id == 359870 || // FFX/X-2 HD Remaster  // Not compatible with the latest version as of 2026-10-01
          //id == 578330 || // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
            id == 429660 || // Tales of Berseria
            id == 372360    // Tales of Symphonia
          //id == 738540 || // Tales of Vesperia DE
          //id == 351970    // Tales of Zestiria
         );
}

#pragma endregion

#pragma region Artwork

std::wstring
app_record_steam_s::ResolveArtwork (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Special K: only custom artwork in Assets\ (otherwise LoadLibraryTexture uses the embedded resources)
  if (IsSpecialK ( ))
  {
    std::wstring load_str = FindCustomArtwork (type);

    isCustom = isManaged = (! load_str.empty ( ));

    return load_str;
  }

  std::wstring SKIFCustomPath  = GetStoreAssetDirectory ( ) + ((type == LibraryTexture::Cover) ? L"cover" : L"icon");
  std::wstring SteamCustomPath = SK_FormatStringW (LR"(%ws\userdata\%i\config\grid\%i)", _path_cache.steam_install, SKIF_Steam_GetCurrentUser ( ), id);

  // Extract a copy of the official Steam icon and save it as a .ico
  if (type == LibraryTexture::Icon)
  {
    std::wstring
      ExtractedIconPath = GetStoreAssetDirectory ( ) + L"icon-original.ico";

    if (! PathFileExistsW (ExtractedIconPath.c_str()))
      SKIF_Util_SaveImageAsICO (source.c_str(), ExtractedIconPath.c_str(), 32);
  }

  std::wstring load_str = FindCustomArtwork (type);

  isCustom = (! load_str.empty ( ));

  if (! isCustom)
  {
    isManaged = false; // Steam's user-specific custom covers are not managed

    if      (type == LibraryTexture::Cover && _registry.bPCGWCoversSteam &&
             PathFileExistsW ((SKIFCustomPath + L"-pcgw.png").c_str()))
    {
      load_str =               SKIFCustomPath + L"-pcgw.png";
      isManaged = true; // PCGW cover is managed!
    }
    else if (type == LibraryTexture::Cover &&
             PathFileExistsW ((SteamCustomPath + L"p.png").c_str()))
      load_str =               SteamCustomPath + L"p.png";
    else if (type == LibraryTexture::Cover &&
             PathFileExistsW ((SteamCustomPath + L"p.jpg").c_str()))
      load_str =               SteamCustomPath + L"p.jpg";
    else {
      load_str =               source;

      // The fallback is normally managed! <%ws\Assets\Steam\%i\cover-original.jpg>
      // The only time it is not is when SKIF is in tinyCovers mode and loaded the original 300x450 cover from the Steam client
      if (load_str.find (L"cover-original.jpg") != std::wstring::npos)
        isManaged = true;
    }
  }

  return load_str;
}

// Cover worker thread: library cache cover, upgraded to the 600x900 cover from the CDN when available
std::wstring
app_record_steam_s::PrepareCoverSource (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Special K: no store cover
  if (IsSpecialK ( ))
    return L"";

  std::wstring load_str;

  std::wstring load_str_2x (
    SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", _path_cache.specialk_userdata, id)
  );

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            load_str_2x, ec))
        std::filesystem::create_directories (load_str_2x, ec);

  load_str_2x += L"cover-original.jpg";
  load_str     = _path_cache.steam_install;
  load_str    += LR"(/appcache/librarycache/)" +
    std::to_wstring (id)                +
                            L"/" + SK_FormatStringW (L"%hs", common_config.boxart_hash.c_str ());

  if (_registry.bPCGWCoversSteam && ! tex_cover.queriedPCGW)
  {
    tex_cover.queriedPCGW = true;
    SKIF_Steam_IdentifyAssetPCGW (id);
  }

  // Regular files only: with an unknown boxart hash, load_str is just the app's librarycache folder
  auto _IsFile = [](const std::wstring& path) -> bool
  {
    DWORD dwAttributes = GetFileAttributesW (path.c_str ());
    return (dwAttributes != INVALID_FILE_ATTRIBUTES && ! (dwAttributes & FILE_ATTRIBUTE_DIRECTORY));
  };

  bool hasClientCover = _IsFile (load_str);    // The Steam client's library cache copy (600x900, sometimes really 300x450)
  bool hasSKIFCover   = _IsFile (load_str_2x); // SKIF's downloaded 600x900_2x copy
  bool useLowRes      = (_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass);

  // Steam typically uses one of two different CDNs:
  // * CloudFlare : https://cdn.cloudflare.steamstatic.com/steam/apps/2673660/library_600x900_2x.jpg
  // * Akamai     :        https://steamcdn-a.akamaihd.net/steam/apps/2673660/library_600x900_2x.jpg
  // Historically the Akamai CDN has been ever so slightly more reliable than the CloudFlare CDN.
  // SKIF_Steam_GetCoverURI ( ) looks the asset up through the Steam store API instead.

  // Neither the Steam client nor SKIF has a copy: download one (also with low-res covers, as anything beats a missing cover)
  if (! hasClientCover && ! hasSKIFCover)
  {
    // ... but only if low bandwidth mode is disabled
    if (! _registry.bLowBandwidthMode)
    {
      std::wstring url = SKIF_Steam_GetCoverURI (id, true);

      if (! url.empty ( ))
      {
        PLOG_DEBUG << "Downloading cover asset: " << url;
        SKIF_Util_GetWebResource (url, load_str_2x);
      }

      else
        PLOG_WARNING << "Could not find a cover asset for app ID " << id;
    }

    if (_IsFile (load_str_2x))
      load_str = load_str_2x;
  }

  // Only SKIF's copy exists: use it (also with low-res covers)
  else if (! hasClientCover)
    load_str = load_str_2x;

  // Do not load a high-res copy if low-res covers are being used,
  //   as in those scenarios we prefer to load the original 300x450 cover
  else if (! useLowRes)
  {
    std::wstring load_str_final = load_str;

    // If 600x900 exists but 600x900_x2 cannot be found
    if (! hasSKIFCover)
    {
      DirectX::TexMetadata meta = { };

      // Load the metadata from 600x900, but only if low bandwidth mode is not enabled
      if ( ! _registry.bLowBandwidthMode &&
            SUCCEEDED (
            DirectX::GetMetadataFromWICFile (
              load_str.c_str (),
                DirectX::WIC_FLAGS_FILTER_POINT,
                  meta
              )
            )
          )
      {
        // If the image is in reality 300x450, which indicates a real cover,
        //   download the real 600x900 cover and store it in _x2
        if (meta.width  == 300 &&
            meta.height == 450)
        {
          std::wstring url = SKIF_Steam_GetCoverURI (id, true);
          PLOG_DEBUG << "Downloading cover asset: " << url;

          SKIF_Util_GetWebResource (url, load_str_2x);

          if (_IsFile (load_str_2x))
            load_str_final = load_str_2x;
        }
      }
    }

    // If 600x900_x2 exists, check the last modified time stamps
    else {
      WIN32_FILE_ATTRIBUTE_DATA faX1{}, faX2{};

      // ... but only if low bandwidth mode is disabled
      if (! _registry.bLowBandwidthMode &&
          GetFileAttributesEx (load_str   .c_str (), GetFileExInfoStandard, &faX1) &&
          GetFileAttributesEx (load_str_2x.c_str (), GetFileExInfoStandard, &faX2))
      {
        // If 600x900 has been edited after 600_900_x2,
        //   download new copy of the 600_900_x2 cover
        if (CompareFileTime (&faX1.ftLastWriteTime, &faX2.ftLastWriteTime) == 1)
        {
          DeleteFile (load_str_2x.c_str ());

          std::wstring url = SKIF_Steam_GetCoverURI (id, true);
          PLOG_DEBUG << "Downloading cover asset: " << url;

          SKIF_Util_GetWebResource (url, load_str_2x);
        }
      }

      // If 600x900_x2 exists now, load it
      if (_IsFile (load_str_2x))
        load_str_final = load_str_2x;
    }

    load_str = load_str_final;
  }

  return load_str;
}

std::wstring
app_record_steam_s::GetIconSource (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (IsSpecialK ( ))
    return L"sk_icon.jpg";

  return SK_FormatStringW(LR"(%ws\appcache\librarycache\%i\%hs.jpg)", _path_cache.steam_install, id, common_config.icon_hash.c_str ()); //L"_icon.jpg"
}

void
app_record_steam_s::PrepareIconSource (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (! IsSpecialK ( ))
    appinfo->getAppInfo ( id, &_games.GetApps() );
}

// We may also store a PCGW cover that we must reset
bool
app_record_steam_s::DeleteStoreCoverFiles (const std::wstring& assetDir)
{
  tex_cover.queriedPCGW = false;

  return DeleteFile ((assetDir + L"cover-pcgw.png").c_str());
}

#pragma endregion

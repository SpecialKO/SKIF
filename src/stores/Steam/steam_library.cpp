//
// Copyright 2020 Andon "Kaldaien" Coleman
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
SK_GetManifestContentsForAppID (app_record_s *app)
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
     LR"(%s\userdata\%i\7\remote\sharedconfig.vdf)" // ConfigStore_UserRoaming
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
    LSTATUS   status =
      RegGetValueW ( HKEY_CURRENT_USER,
                       LR"(SOFTWARE\Valve\Steam\)",
                                        L"SteamPath",
                         RRF_RT_REG_SZ,
                           nullptr,
                             wszSteamPath,
                               (LPDWORD)&len );

    if (status == ERROR_SUCCESS)
      return wszSteamPath;
    else
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
SK_UseManifestToGetAppName (app_record_s *app)
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
SK_UseManifestToGetCurrentBranch (app_record_s *app)
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
SKIF_Steam_GetLaunchOptions (AppId_t appid, SteamId3_t userid , app_record_s *app)
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
        auto& apps_localconfig =
          user_localconfig.
            childs.at("Software") ->
              childs.at("valve")  ->
                childs.at("Steam")->
                  childs.at("apps");

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
SKIF_Steam_PreloadUserLocalConfig (SteamId3_t userid, std::vector <std::pair < std::string, app_record_s > > *apps, std::set <std::string> *apptickets)
{
  if (apps->empty())
    return false;

  // Clear any cached app tickets to prevent stale data from sticking around
  apptickets->clear ( );

  // Clear out any cached launch option data
  for (auto& app : *apps)
  {
    if (app.second.store != app_record_s::Store::Steam)
      continue;

    // Reset any current data
    app.second.steam.local = { };
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
        // 
        // LaunchOptions are tracked at "UserLocalConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "LaunchOptions"
        auto& apps_localconfig =
          user_localconfig.
            childs.at("Software") ->
              childs.at("valve")  ->
                childs.at("Steam")->
                  childs.at("apps");

        if (apps_localconfig != nullptr &&
            apps_localconfig->childs.size() > 0)
        {
          for (auto& app : *apps)
          {
            if (app.second.store != app_record_s::Store::Steam)
              continue;

            auto lc_app = apps_localconfig->childs.find(std::to_string(app.second.id));
            if (lc_app != apps_localconfig->childs.end())
            {
              auto& attribs = lc_app->second->attribs;

              if (! attribs.empty() && attribs.count("LaunchOptions") > 0)
                app.second.steam.local.launch_option = attribs.at("LaunchOptions");
            }
          }
        }

        // Preload DLC ownership...
        // 
        // AppTickets are tracked at "UserLocalConfigStore" -> "apptickets" -> "<app-id>"
        // This is used to determine if a DLC related launch option should be visible
        auto& apptickets_localconfig =
          user_localconfig.
            childs.at("apptickets");

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
      UNREFERENCED_PARAMETER(e);

      PLOG_ERROR << "Unknown error occurred when trying to parse localconfig.vdf";
    }
  }

  return false;
}

bool
SKIF_Steam_PreloadUserSharedConfig (SteamId3_t userid, std::vector <std::pair < std::string, app_record_s > > *apps)
{
  if (apps->empty())
    return false;

  // Clear out any cached launch option data
  for (auto& app : *apps)
  {
    if (app.second.store != app_record_s::Store::Steam)
      continue;

    // Reset any current data
    app.second.steam.shared = { };
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
        auto& vdfConfigAppsTree =
          vdfConfig. // UserRoamingConfigStore
            childs.at("Software") ->
              childs.at("valve")  ->
                childs.at("Steam")->
                  childs.at("apps");

        if (vdfConfigAppsTree != nullptr &&
            vdfConfigAppsTree->childs.size() > 0)
        {
          for (auto& app : *apps)
          {
            if (app.second.store != app_record_s::Store::Steam)
              continue;

            auto conf_app  = vdfConfigAppsTree->childs.find(std::to_string(app.second.id));
            if ( conf_app != vdfConfigAppsTree->childs.end())
            {
              // Load hidden state...
              // "UserRoamingConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "hidden" == 1
              auto& attribs = conf_app->second->attribs;
              if (! attribs.empty() && attribs.count("hidden") > 0 && attribs.at("hidden") == "1")
                app.second.steam.shared.hidden     = 1;

              // Load favorite state...
              // "UserRoamingConfigStore" -> "Software" -> "valve" -> "Steam" -> "apps" -> "<app-id>" -> "tags" -> "<order>" == "favorite"
              auto& tags = conf_app->second->childs.at("tags");
              for (auto& child : tags->attribs)
              {
                if (! child.first.empty() && child.second == "favorite")
                  app.second.steam.shared.favorite = 1;
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
      UNREFERENCED_PARAMETER(e);

      PLOG_ERROR << "Unknown error occurred when trying to parse localconfig.vdf";
    }
  }

  return false;
}

void
SKIF_Steam_PreloadUserConfig (SteamId3_t userid, std::vector<std::pair<std::string,app_record_s>>* apps, std::set<std::string>* apptickets)
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

        auto& system_localconfig =
          user_localconfig.
            childs.at("system");

        // If global state is disabled, don't bother checking the local state
        if (! system_localconfig->attribs.empty() && system_localconfig->attribs.count("EnableGameOverlay") > 0 && system_localconfig->attribs.at("EnableGameOverlay") == "0")
          return false;

        // Continue checking the local state
        auto& apps_localconfig =
          user_localconfig.
            childs.at("apps");

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
SK_UseManifestToGetAppOwner (app_record_s *app)
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
SK_UseManifestToGetInstallDir (app_record_s *app)
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
SK_UseManifestToGetDepots (app_record_s *app)
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
SK_UseManifestToGetDepotManifest (app_record_s *app, DepotId_t depot)
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
    }
  }

  return apps;
}


// Updates the given vector with a filtered list of all discovered Steam apps.
// Filters out various non-game type of apps.
void
SKIF_Steam_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
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
      app_record_s record (app);
      record.store      = app_record_s::Store::Steam;
      record.store_utf8 = "Steam";

      // Opening the manifests to read the names is a
      //   lengthy operation, so defer names and icons
      apps->emplace_back (
        "Loading...", record
      );
    }
  }
};


// NOT THREAD-SAFE!!!
bool
SKIF_Steam_HasActiveProcessChanged (std::vector <std::pair < std::string, app_record_s > > *apps, std::set <std::string> *apptickets)
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
        for (auto& launch_cfg : app.second.launch_configs)
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
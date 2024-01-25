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

SteamId3_t    g_SteamUserID   = 0;
DWORD         g_dwSteamProcessID = 0;

#define MAX_STEAM_LIBRARIES 16

extern std::atomic<int> SKIF_FrameCount;

CRITICAL_SECTION VFSManifestSection;

// Thread safety is backed by the VFSManifestSection critical section
struct derp {
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

// There's two of these:
// *   SK_Steam_GetInstalledAppIDs ( ) <- The one below
// * SKIF_Steam_GetInstalledAppIDs ( )
std::vector <AppId_t>
SK_Steam_GetInstalledAppIDs (void)
{
  PLOG_INFO << "Detecting Steam games...";

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

std::wstring
SK_Steam_GetApplicationManifestPath (app_record_s *app)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  if (! app->Steam_ManifestPath.empty())
    return app->Steam_ManifestPath;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);

  if (! steam_lib_paths)
    return L"";

  if (steam_libs != 0)
  {
    for (int i = 0; i < steam_libs; i++)
    {
      wchar_t    wszManifest [MAX_PATH + 2] = { };
      swprintf ( wszManifest, MAX_PATH,
                   LR"(%s\steamapps\appmanifest_%u.acf)",
               (wchar_t *)steam_lib_paths [i],
                            app->id );

      CHandle hManifest (
        CreateFileW ( wszManifest,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr,        OPEN_EXISTING,
                            GetFileAttributesW (wszManifest),
                              nullptr
                    )
      );

      if (hManifest != INVALID_HANDLE_VALUE)
      {
        app->Steam_ManifestPath = wszManifest;
        break;
      }
    }
  }

  return app->Steam_ManifestPath;
}

std::wstring
SK_Steam_GetLocalConfigPath (SteamId3_t userid)
{

  wchar_t    wszLocalConfig [MAX_PATH + 2] = { };
  swprintf ( wszLocalConfig, MAX_PATH,
                LR"(%s\userdata\%i\config\localconfig.vdf)",
              SK_GetSteamDir ( ),
                        userid );

  CHandle hLocalConfig (
    CreateFileW ( wszLocalConfig,
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                      nullptr,        OPEN_EXISTING,
                        GetFileAttributesW (wszLocalConfig),
                          nullptr
                )
  );

  if (hLocalConfig != INVALID_HANDLE_VALUE)
    return wszLocalConfig;

  return L"";
}


std::string
SK_GetManifestContentsForAppID (app_record_s *app)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  static AppId_t     manifest_id = 0;
  static std::string manifest;
  
  if (! app->Steam_ManifestData.empty())
    return app->Steam_ManifestData;

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

    CHandle hManifest (
      CreateFileW ( wszManifestFullPath,
                      GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,        OPEN_EXISTING,
                          GetFileAttributesW (wszManifestFullPath),
                            nullptr
                  )
    );

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

        app->Steam_ManifestData = manifest;
        app->Steam_ManifestPath = wszManifestFullPath;
        return app->Steam_ManifestData;
      }
    }
  }

  app->Steam_ManifestData =  "<InvalidData>";
  app->Steam_ManifestPath = L"<InvalidPath>";

  return "";
}

std::string
SK_GetLocalConfigForSteamUser (SteamId3_t userid)
{
  static SteamId3_t  localConfig_id = 0;
  static std::string localConfig;

  if (localConfig_id == userid && (! localConfig.empty ()))
    return localConfig;

  std::wstring wszLocalConfig =
    SK_Steam_GetLocalConfigPath (userid);

  if (wszLocalConfig.empty ())
    return localConfig;

  CHandle hLocalConfig (
    CreateFileW ( wszLocalConfig.c_str (),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                      nullptr,        OPEN_EXISTING,
                        GetFileAttributesW (wszLocalConfig.c_str ()),
                          nullptr
                )
  );

  if (hLocalConfig != INVALID_HANDLE_VALUE)
  {
    //PLOG_VERBOSE << "Reading " << wszLocalConfig;

    DWORD dwSizeHigh = 0,
          dwRead     = 0,
          dwSize     =
     GetFileSize (hLocalConfig, &dwSizeHigh);

    auto szLocalConfigData =
      std::make_unique <char []> (
        std::size_t (dwSize) + std::size_t (1)
      );
    auto localConfig_data =
       szLocalConfigData.get ();

    if (! localConfig_data)
      return "";

    const bool bRead =
      ReadFile ( hLocalConfig,
                  localConfig_data,
                     dwSize,
                    &dwRead,
                       nullptr );

    if (bRead && dwRead)
    {
      localConfig =
        std::move (localConfig_data);

      localConfig_id = userid;

      return
        localConfig;
    }
  }

  return localConfig;
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

      CHandle hLibFolders (
        CreateFileW ( wszLibraryFolders,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr,        OPEN_EXISTING,
                            GetFileAttributesW (wszLibraryFolders),
                              nullptr
                    )
      );

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
          std::make_unique <char []> (dwSize + 4u);
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
              // Strip double backslashes characters from the string
              try
              {
                lib_path = std::regex_replace (lib_path, std::wregex(LR"(\\\\)"), LR"(\)");
              }
              catch (const std::exception& e)
              {
                UNREFERENCED_PARAMETER(e);
              }

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

  if (app->Steam_ManifestData.empty())
      app->Steam_ManifestData =
        SK_GetManifestContentsForAppID (app);

  if (! app->Steam_ManifestData.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

    std::string app_name =
      SK_Steam_KeyValues::getValue (
        app->Steam_ManifestData, { "AppState" }, "name"
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
  if (app->Steam_ManifestData.empty())
      app->Steam_ManifestData =
        SK_GetManifestContentsForAppID (app);

  if (! app->Steam_ManifestData.empty ())
  {
    std::string branch =
      SK_Steam_KeyValues::getValue (
        app->Steam_ManifestData, { "AppState", "UserConfig" }, "BetaKey"
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
  // Original implementation using SKIF's SK_Steam_KeyValue implementation
#if 0
  std::string localConfig_data =
    SK_GetLocalConfigForSteamUser (userid);

  if (! localConfig_data.empty ())
  {
    std::string launch_options =
      SK_Steam_KeyValues::getValue (
        localConfig_data, { "UserLocalConfigStore", "Software", "valve", "Steam", "apps", std::to_string (appid) }, "LaunchOptions"
      );

    if (! launch_options.empty ())
    {
      return launch_options;
    }
  }
#endif

  // Implementation using the ValveFileVDF project
  std::string localConfig_data =
    SK_WideCharToUTF8 (
      SK_Steam_GetLocalConfigPath (userid)
  );

  if (localConfig_data != "")
  {
    std::ifstream file (localConfig_data);

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
                  app->Steam_LaunchOption = attribs.at("LaunchOptions");

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
    app.second.Steam_LaunchOption  = "";
    app.second.Steam_LaunchOption1 = "";
  }

  // Abort if the user signed out
  if (userid == 0)
    return false;

  // Implementation using the ValveFileVDF project
  std::string localConfig_data =
    SK_WideCharToUTF8 (
      SK_Steam_GetLocalConfigPath (userid)
  );

  if (localConfig_data != "")
  {
    std::ifstream file (localConfig_data);

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
                  app.second.Steam_LaunchOption = attribs.at("LaunchOptions");
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
  }

  return false;
}

bool
SKIF_Steam_isSteamOverlayEnabled (AppId_t appid, SteamId3_t userid)
{
  std::string localConfig_data =
    SK_WideCharToUTF8 (
      SK_Steam_GetLocalConfigPath (userid)
  );

  if (localConfig_data != "")
  {
    std::ifstream file (localConfig_data);

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

  if (app->Steam_ManifestData.empty())
    app->Steam_ManifestData =
      SK_GetManifestContentsForAppID (app);

  if (! app->Steam_ManifestData.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

    std::wstring app_path =
      SK_Steam_KeyValues::getValueAsUTF16 (
        app->Steam_ManifestData, { "AppState" }, "installdir"
      );

    if (! app_path.empty ())
    {
      wchar_t    app_root [MAX_PATH + 2] = { };
      wcsncpy_s (app_root, MAX_PATH,
            app->Steam_ManifestPath.c_str (), _TRUNCATE);

      PathRemoveFileSpecW (app_root);
      PathAppendW         (app_root, L"common\\");

      wchar_t path [MAX_PATH + 2] = { };

      PathCombineW (path, app_root,
                          app_path.c_str ());

      app->install_dir = path;
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

// TODO: Fix thread race and shared resources between
//  - SKIF_Steam_isLibrariesSignaled (main thread)
//  - SK_Steam_GetInstalledAppIDs (library worker)
bool
SKIF_Steam_areLibrariesSignaled (void)
{
  bool isSignaled = false;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);
  static bool      isInitialized   = false;

  extern HWND      SKIF_Notify_hWnd;

  if (SKIF_Notify_hWnd == NULL)
    return false;

  if (! steam_lib_paths)
    return false;

  if (steam_libs == 0)
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
    if (library.watch.isSignaled (library.path, UITab_Library))
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

  isInitialized = true;

  return isSignaled;
};


// There's two of these:
// *   SK_Steam_GetInstalledAppIDs ( )
// * SKIF_Steam_GetInstalledAppIDs ( ) <- The one below
void
SKIF_Steam_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  std::set <uint32_t> unique_apps;

  for ( auto app : SK_Steam_GetInstalledAppIDs ( ))
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

      if (RegGetValueW (hKey, NULL, L"pid", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
        g_dwSteamProcessID = *(DWORD*)pvData;

      hKey.Close ( );
    }

    // Refresh stuff if the current user has changed
    if ((g_SteamUserID != oldUserID || g_dwSteamProcessID != oldProcID)
               && apps != nullptr    &&          apptickets != nullptr)
    {
      // Preload user's local config
      SKIF_Steam_PreloadUserLocalConfig (g_SteamUserID, apps, apptickets);

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
  return g_SteamUserID;

  /* LEGACY METHOD:
  static SteamId3_t SteamUserID = 0;

  if (refresh)
  {
    WCHAR                    szData [255] = { };
    DWORD   dwSize = sizeof (szData);
    PVOID   pvData =         szData;
    CRegKey hKey ((HKEY)0);

    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey.m_hKey) == ERROR_SUCCESS)
    {
      if (RegGetValueW (hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
        SteamUserID = *(DWORD*)pvData;

      hKey.Close ( );
    }
  }

  return SteamUserID;
  */
}

DWORD
SKIF_Steam_GetActiveProcess (void)
{
  return g_dwSteamProcessID;
}


#if 0
// Only used for Steam games!
void
SKIF_Steam_GetInjectionStrategy (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  int firstValidFound = -1;

  // TODO: Go through all code and change pApp->launch_configs[0] to refer to whatever "preferred" launch config we've found...
  for ( auto& launch_cfg : pApp->launch_configs )
  {
    auto& launch = launch_cfg.second;

    if (! launch.valid)
      continue;

    if (! launch.isExecutableFullPathValid ( ))
      continue;

    if (firstValidFound == -1)
      firstValidFound = launch_cfg.first;

    // Assume global
    launch.injection.injection.type        =
      InjectionType::Global;
    launch.injection.injection.entry_pt    =
      InjectionPoint::CBTHook;
    launch.injection.config.type           =
      ConfigType::Centralized;
    launch.injection.config.shorthand      =
      L"SpecialK.ini";
    launch.injection.config.shorthand_utf8 =
      SK_WideCharToUTF8 (launch.injection.config.shorthand);

    // Check bitness
    
    // TODO: This needs to be gone through and reworked entirely as the logic still gets thrown off by edge cases
    //  - e.g. EA games with "invalid" launch options where the 'osarch' of the common_config ends up never being used
    if (launch.injection.injection.bitness == InjectionBitness::Unknown)
    {

#define TRUST_LAUNCH_CONFIG
#ifdef TRUST_LAUNCH_CONFIG
      app_record_s::CPUType
          cpu_type  = pApp->common_config.cpu_type; // We start by using the common config

      if (cpu_type != app_record_s::CPUType::Any)
      {
        if (launch.cpu_type != app_record_s::CPUType::Common)
        {
          cpu_type =
            launch.cpu_type;
        }
      }

      else
      {
        // The any case will just be 64-bit for us, since SK only runs on
        //   64-bit systems. Thus, ignore 32-bit launch configs.
#ifdef _WIN64
        if (launch.cpu_type == app_record_s::CPUType::x86)
#else
        if (launch.cpu_type == app_record_s::CPUType::x64)
#endif
        {
          continue;
        }

        else {
          cpu_type =
            launch.cpu_type;
        }
      }
      
      if (     cpu_type == app_record_s::CPUType::x64)
        launch.injection.injection.bitness = InjectionBitness::SixtyFour;

      else if (cpu_type == app_record_s::CPUType::x86)
        launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;

      // If we still haven't resolved it, use SKIF's cached property
      else if (pApp->skif.cpu_type != 0)
        launch.injection.injection.bitness = (InjectionBitness)pApp->skif.cpu_type;

      // In case we still haven't resolved the CPU architecture,
      //   we need to check the actual arch of the game executable
      else // Common || Any
      {
        if (launch.isExecutableFullPathValid ())
        {
          std::wstring exec_path =
            launch.getExecutableFullPath ( );

          DWORD dwBinaryType = MAXDWORD;
          if (GetBinaryTypeW (exec_path.c_str (), &dwBinaryType))
          {
            if (dwBinaryType == SCS_64BIT_BINARY)
              launch.injection.injection.bitness = InjectionBitness::SixtyFour;
            else if (dwBinaryType == SCS_32BIT_BINARY)
              launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;
          }
        }
      }
#else

      std::wstring exec_path =
        launch.getExecutableFullPath ( );

      DWORD dwBinaryType = MAXDWORD;
      if ( GetBinaryTypeW (exec_path.c_str (), &dwBinaryType) )
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          install_state.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          install_state.injection.bitness = InjectionBitness::SixtyFour;
      }

#endif

    } // End checking bitness

    struct {
      InjectionPoint   entry_pt;
      std::wstring     name;
      std::wstring     path;
    } test_dlls [] = { // The small things matter -- array is sorted in the order of most expected
      { InjectionPoint::DXGI,    L"DXGI",     L"" },
      { InjectionPoint::D3D11,   L"D3D11",    L"" },
      { InjectionPoint::D3D9,    L"D3D9",     L"" },
      { InjectionPoint::OpenGL,  L"OpenGL32", L"" },
      { InjectionPoint::DInput8, L"DInput8",  L"" },
      { InjectionPoint::D3D8,    L"D3D8",     L"" },
      { InjectionPoint::DDraw,   L"DDraw",    L"" }
    };

    std::wstring test_path    =
      launch.getExecutableDir ( );
    std::wstring test_pattern =
      test_path + LR"(\*.dll)";

    WIN32_FIND_DATA ffd         = { };
    HANDLE          hFind       =
      FindFirstFileExW (test_pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

    if (hFind != INVALID_HANDLE_VALUE)
    {
      bool breakOuterLoop = false;

      do
      {
        if ( wcscmp (ffd.cFileName, L".")  == 0 ||
             wcscmp (ffd.cFileName, L"..") == 0 )
          continue;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
          continue;

        for ( auto& dll : test_dlls )
        {
          // Filename + extension
          dll.path = dll.name + L".dll";
          
          if (StrStrIW (ffd.cFileName, dll.path.c_str()) == NULL)
            continue;
          
          // Full path
          dll.path = test_path + LR"(\)" + dll.path;

          std::wstring dll_ver =
            SKIF_Util_GetSpecialKDLLVersion (dll.path.c_str ());

          if (dll_ver.empty ())
            continue;

          launch.injection.injection = {
            launch.injection.injection.bitness,
            dll.entry_pt, InjectionType::Local
          };

          launch.injection.dll.full_path     = dll.path;
          launch.injection.dll.full_path_utf8 = SK_WideCharToUTF8 (launch.injection.dll.full_path);
          launch.injection.dll.version        = dll_ver;
          launch.injection.dll.version_utf8   = SK_WideCharToUTF8 (launch.injection.dll.version);

          if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
            launch.injection.config.type =   ConfigType::Centralized;
          else
          {
            launch.injection.config.type          = ConfigType::Localized;
            launch.injection.config.root_dir      = test_path;
            launch.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (launch.injection.config.root_dir);
          }

          launch.injection.config.shorthand      = dll.name + L".ini";
          launch.injection.config.shorthand_utf8 = SK_WideCharToUTF8 (launch.injection.config.shorthand);

          breakOuterLoop = true;
          break;
        }

        if (breakOuterLoop)
          break;

      } while (FindNextFile (hFind, &ffd));

      FindClose (hFind);
    }

    // Check if the launch config is elevated or blacklisted
    launch.isElevated    (true);
    launch.isBlacklisted (true);
  }

  // Swap out the first element for the first valid one we found
  if (firstValidFound != -1)
  {
    app_record_s::launch_config_s copy    = pApp->launch_configs[0];
    pApp->launch_configs[0]               = pApp->launch_configs[firstValidFound];
    pApp->launch_configs[firstValidFound] = copy;
  }

  // TODO: Make the specialk.injection bitness/state/etc stuff bound
  //         to launch_config so it is not universal any longer

  auto& launch = pApp->launch_configs.begin()->second;

  // If primary launch config was invalid (e.g. Link2EA games) then set it to use global
  if (launch.injection.injection.type == InjectionType::Unknown)
  {
    // Assume global
    launch.injection.injection.type        =
      InjectionType::Global;
    launch.injection.injection.entry_pt    =
      InjectionPoint::CBTHook;
    launch.injection.config.type           =
      ConfigType::Centralized;
    launch.injection.config.shorthand      =
      L"SpecialK.ini";
    launch.injection.config.shorthand_utf8 =
      SK_WideCharToUTF8 (launch.injection.config.shorthand);
  }

  // Main UI stuff should follow the primary launch config
  pApp->specialk.injection = launch.injection;

  if ( InjectionType::Global ==
         pApp->specialk.injection.injection.type )
  {
    // Assume Global 32-bit if we don't know otherwise
    bool bIs64Bit =
      (launch.injection.injection.bitness ==
                        InjectionBitness::SixtyFour);

    pApp->specialk.injection.config.type =
      ConfigType::Centralized;
    
    pApp->specialk.injection.dll.full_path      = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, (bIs64Bit) ? L"SpecialK64.dll" : L"SpecialK32.dll");
    pApp->specialk.injection.dll.full_path_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.dll.full_path);
    pApp->specialk.injection.dll.version        = 
                                       bIs64Bit ? _inject.SKVer64
                                                : _inject.SKVer32;
    pApp->specialk.injection.dll.version_utf8   = SK_WideCharToUTF8 (pApp->specialk.injection.dll.version);
  }

  pApp->specialk.injection.localized_name =
    SK_UseManifestToGetAppName (pApp);

  if ( ConfigType::Centralized ==
         pApp->specialk.injection.config.type )
  {
    std::wstring name =
      SK_UTF8ToWideChar (
        pApp->specialk.injection.localized_name
      );

    name.erase ( std::remove_if ( name.begin (),
                                  name.end   (),

                              [](wchar_t tval)
                              {
                                static
                                const std::set <wchar_t>
                                  invalid_file_char =
                                  {
                                    L'\\', L'/', L':',
                                    L'*',  L'?', L'\"',
                                    L'<',  L'>', L'|',
                                  //L'&',

                                    //
                                    // Obviously a period is not an invalid character,
                                    //   but three of them in a row messes with
                                    //     Windows Explorer and some Steam games use
                                    //       ellipsis in their titles.
                                    //
                                    L'.'
                                  };

                                return
                                  ( invalid_file_char.find (tval) !=
                                    invalid_file_char.end  (    ) );
                              }
                          ),

               name.end ()
         );

    // Strip trailing spaces from name, these are usually the result of
    //   deleting one of the non-useable characters above.
    for (auto it = name.rbegin (); it != name.rend (); ++it)
    {
      if (*it == L' ') *it = L'\0';
      else                   break;
    }

    pApp->specialk.injection.config.root_dir =
      SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                           _path_cache.specialk_userdata,
                             name.c_str () );
    pApp->specialk.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.root_dir);
  }

  pApp->specialk.injection.config.full_path =
    (pApp->specialk.injection.config.root_dir + LR"(\)" ) +
     pApp->specialk.injection.config.shorthand;
  pApp->specialk.injection.config.full_path_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.full_path);
}

#endif

std::wstring
SKIF_Steam_GetAppStateString (       AppId_t  appid,
                               const wchar_t *wszStateKey )
{
  std::wstring str ( MAX_PATH, L'\0' );
  DWORD        len = MAX_PATH;
  LSTATUS   status =
    RegGetValueW ( HKEY_CURRENT_USER,
      SK_FormatStringW ( LR"(SOFTWARE\Valve\Steam\Apps\%lu)",
                           appid ).c_str (),
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
SKIF_Steam_GetAppStateDWORD (       AppId_t  appid,
                              const wchar_t *wszStateKey,
                                    DWORD   *pdwStateVal )
{
  DWORD     len    = sizeof (DWORD);
  LSTATUS   status =
    RegGetValueW ( HKEY_CURRENT_USER,
      SK_FormatStringW ( LR"(SOFTWARE\Valve\Steam\Apps\%lu)",
                           appid ).c_str (),
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

  SKIF_Steam_GetAppStateDWORD (
     pApp->id,   L"Installed",
    &pApp->_status.installed );

  if (pApp->_status.installed != 0x0)
  {   pApp->_status.running    = 0x0;

    SKIF_Steam_GetAppStateDWORD (
       pApp->id,   L"Running",
      &pApp->_status.running );

    if (! pApp->_status.running)
    {
      SKIF_Steam_GetAppStateDWORD (
         pApp->id,   L"Updating",
        &pApp->_status.updating );
    }

    else
    {
      pApp->_status.updating = 0x0;
    }

    if (pApp->names.normal.empty ())
    {
      std::wstring wide_name =
        SKIF_Steam_GetAppStateString (
          pApp->id,   L"Name"
        );

      if (! wide_name.empty ())
      {
        pApp->names.normal =
          SK_WideCharToUTF8 (wide_name);
      }
    }
  }

  return true;
}
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

std::vector <
  std::pair < std::string, app_record_s >
            > g_apps;

std::set    < std::string >
              g_apptickets;

std::unique_ptr <skValveDataFile> appinfo = nullptr;

#define MAX_STEAM_LIBRARIES 16

extern int SKIF_FrameCount;

// {95FF906C-3D28-4463-B558-A4D1E5786767}
const GUID IID_VFS_SteamUGC =
{ 0x95ff906c, 0x3d28, 0x4463, { 0xb5, 0x58, 0xa4, 0xd1, 0xe5, 0x78, 0x67, 0x67 } };

void*
SK_VFS_Steam::WorkshopFile::getSubclass (REFIID iid)
{
  if (iid == IID_VFS_SteamUGC)
    return this;

  return
    SK_VirtualFS::vfsNode::getSubclass (iid);
}

std::shared_ptr <SK_VFS_Steam::WorkshopFile>
SK_VFS_Steam::UGC_RootFS::getPublishedFile (PublishedFileId_t id)
{
  auto find =
    pub_id_to_file.find (id);

  if (find != pub_id_to_file.end ( ))
    return find->second;

  return nullptr;
}

std::shared_ptr <SK_VFS_Steam::UGCFile>
SK_VFS_Steam::UGC_RootFS::getUGCFile (UGCHandle_t handle)
{
  auto find =
    ugc_handle_to_file.find (handle);

  if (find != ugc_handle_to_file.end ( ))
    return find->second;

  return nullptr;

}

std::vector <std::shared_ptr <SK_VFS_Steam::WorkshopFile>>
SK_VFS_Steam::WorkshopFile::getRequiredFiles (void)
{
  std::vector <std::shared_ptr <SK_VFS_Steam::WorkshopFile>>
    list;

  for (auto &id : depends.files)
  {
    auto file =
      ugc_root.getPublishedFile (id);

    if (file != nullptr)
      list.push_back (file);
  }

  return
    list;
}

SK_VFS_Steam::UGC_RootFS SK_VFS_Steam::ugc_root;

//SK_VirtualFS manifest_vfs;

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
  //FindFirstFileW   (wszPath, &fd);

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
      //OutputDebugStringW (pFile->getFullPath ().c_str ());
      //OutputDebugStringW (L"\n");
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

  if (steam_libs != 0)
  {
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
      if (library.frame_last_scanned != SKIF_FrameCount)
      {   library.frame_last_scanned  = SKIF_FrameCount;

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

// NOT THREAD SAFE! Updates g_apps with the new value
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

// NOT THREAD SAFE! Updates g_apps with the new value
bool
SKIF_Steam_PreloadUserLocalConfig (SteamId3_t userid)
{
  if (g_apps.empty())
    return false;

  // Clear any cached app tickets to prevent stale data from sticking around
  g_apptickets.clear ( );

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
            for (auto& app : g_apps)
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
                g_apptickets.emplace (child.first);
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

// Temporarily disabled since this gets triggered on game launch/shutdown as well...
bool
SKIF_Steam_isLibrariesSignaled (void)
{
  //static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  //if (! _registry.bLibrarySteam)
  //  return false;

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

    bool countFiles = false;

    // If we detect any changes, delay checking the details for a couple of seconds 
    if (library.watch.isSignaled (library.path, true))
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

    if (library.frame_last_scanned == 0 || countFiles)
    {   library.frame_last_scanned  = SKIF_FrameCount;

      int prevCount = library.count;

      library.count =
        SK_VFS_ScanTree ( library.manifest_vfs,
                          library.path, L"appmanifest_*.acf", 0);

      if (library.count != prevCount)
        isSignaled = true;
    }
  }

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


// Get SteamID3 of the signed in user.
SteamId3_t
SKIF_Steam_GetCurrentUser (bool refresh)
{
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
}


// Only used for Steam games!
void
SKIF_Steam_GetInjectionStrategy (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Parse appinfo data for the current game
  //skValveDataFile::appinfo_s
  //                *pAppInfo =

  if (! pApp->processed)
    appinfo->getAppInfo ( pApp->id );

  //UNREFERENCED_PARAMETER (pAppInfo);

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
    launch.injection.injection.type =
      InjectionType::Global;
    launch.injection.injection.entry_pt =
      InjectionPoint::CBTHook;
    launch.injection.config.type =
      ConfigType::Centralized;
    launch.injection.config.file =
      L"SpecialK.ini";

    // Check bitness
    if (launch.injection.injection.bitness == InjectionBitness::Unknown)
    {

#define TRUST_LAUNCH_CONFIG
#ifdef TRUST_LAUNCH_CONFIG
      app_record_s::CPUType
                    cputype = pApp->common_config.cpu_type;

      if (cputype != app_record_s::CPUType::Any)
      {
        if (launch.cpu_type != app_record_s::CPUType::Common)
        {
          cputype =
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
          //OutputDebugStringW (launch.description.c_str ());
          continue;
        }

        else {
          cputype =
            launch.cpu_type;
        }
      }

      if (cputype == app_record_s::CPUType::x86)
        launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;
      else if (cputype == app_record_s::CPUType::x64)
        launch.injection.injection.bitness = InjectionBitness::SixtyFour;
      else if (cputype == app_record_s::CPUType::Any)
      {
        std::wstring exec_path =
          launch.getExecutableFullPath ( );

        DWORD dwBinaryType = MAXDWORD;
        if ( GetBinaryTypeW (exec_path.c_str (), &dwBinaryType) )
        {
          if (dwBinaryType == SCS_32BIT_BINARY)
            launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;
          else if (dwBinaryType == SCS_64BIT_BINARY)
            launch.injection.injection.bitness = InjectionBitness::SixtyFour;
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
            dll.entry_pt, InjectionType::Local,
            dll.path,     dll_ver
          };

          if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
            launch.injection.config.type =   ConfigType::Centralized;
          else
            launch.injection.config      = { ConfigType::Localized, test_path };

          launch.injection.config.file =
            dll.name + L".ini";

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

    // Naively assume the first valid launch config that we are pointed to is the primary one
    // If we're not at the first launch config, move it to the first position
    //break;
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

  // If primary launch config was invalid (e.g. Link2EA games) then set it to use global
  if (pApp->launch_configs[0].injection.injection.type == InjectionType::Unknown)
  {
    // Assume global
    pApp->launch_configs[0].injection.injection.type =
      InjectionType::Global;
    pApp->launch_configs[0].injection.injection.entry_pt =
      InjectionPoint::CBTHook;
    pApp->launch_configs[0].injection.config.type =
      ConfigType::Centralized;
    pApp->launch_configs[0].injection.config.file =
      L"SpecialK.ini";
  }

  // Main UI stuff should follow the primary launch config
  pApp->specialk.injection = pApp->launch_configs[0].injection;

  if ( InjectionType::Global ==
         pApp->specialk.injection.injection.type )
  {
    // Assume 32-bit if we don't know otherwise
    bool bIs64Bit =
      (pApp->specialk.injection.injection.bitness ==
                       InjectionBitness::SixtyFour );

    wchar_t                 wszPathToSelf [MAX_PATH + 2] = { };
    GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
    PathRemoveFileSpecW (   wszPathToSelf);
    PathAppendW         (   wszPathToSelf,
                              bIs64Bit ? L"SpecialK64.dll"
                                       : L"SpecialK32.dll" );
    pApp->specialk.injection.injection.dll_path = wszPathToSelf;
    pApp->specialk.injection.injection.dll_ver  =
    SKIF_Util_GetSpecialKDLLVersion (       wszPathToSelf);
  }

  if (pApp != nullptr)
  {
    pApp->specialk.injection.localized_name =
      SK_UseManifestToGetAppName (pApp);
  }

  else
    pApp->specialk.injection.localized_name = "<executable_name_here>";

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

    pApp->specialk.injection.config.dir =
      SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                           _path_cache.specialk_userdata,
                             name.c_str () );
  }

  pApp->specialk.injection.config.file =
    (pApp->specialk.injection.config.dir + LR"(\)" ) +
     pApp->specialk.injection.config.file;
}

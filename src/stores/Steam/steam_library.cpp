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
#include <fstream>
#include <utility/registry.h>
#include <utility/utility.h>
#include <filesystem>
#include <stores/Steam/apps_ignore.h>
#include <regex>
#include <vdf_parser.hpp>

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

using steam_library_t = wchar_t* [MAX_PATH * 2];

SK_VirtualFS manifest_vfs;

int
SK_VFS_ScanTree ( SK_VirtualFS::vfsNode* pVFSRoot,
                                wchar_t* wszDir,
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
  _swprintf (wszPath, LR"(%s\*)", wszDir);

  WIN32_FIND_DATA fd          = {   };
  HANDLE          hFind       =
    FindFirstFileW (wszPath, &fd);

  if (hFind == INVALID_HANDLE_VALUE) { return 0; }

  do
  {
    if ( wcscmp (fd.cFileName, L".")  == 0 ||
         wcscmp (fd.cFileName, L"..") == 0 )
    {
      continue;
    }

    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
       wchar_t   wszDescend [MAX_PATH + 2] = { };
      _swprintf (wszDescend, LR"(%s\%s)", wszDir, fd.cFileName);

      if (! pVFSImmutableRoot->containsDirectory (wszDescend))
      {
        auto* child =
          pVFSImmutableRoot->addDirectory (wszDescend);

        found +=
          SK_VFS_ScanTree (child, wszDescend, max_depth, depth + 1, pVFSImmutableRoot);
      }
    }

    else
    {
#ifdef _DEBUG
      SK_VirtualFS::File* pFile =
#endif
        pVFSRoot->addFile (fd.cFileName);

#ifdef _DEBUG
      OutputDebugStringW (pFile->getFullPath ().c_str ());
      OutputDebugStringW (L"\n");
#endif

      ++found;
    }
  } while (FindNextFile (hFind, &fd));

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
    for (int i = 0; i < steam_libs; i++)
    {
      wchar_t    wszManifestDir [MAX_PATH + 2] = { };
      swprintf ( wszManifestDir, MAX_PATH + 2,
                   LR"(%s\steamapps)",
               (wchar_t *)steam_lib_paths [i] );

      SK_VFS_ScanTree ( manifest_vfs,
                          wszManifestDir, 0 );

      SK_VirtualFS::vfsNode* pFile =
        manifest_vfs;

      for (const auto& it : pFile->children)
      {
        uint32_t appid;
        if ( swscanf ( it.first.c_str (),
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
SK_Steam_GetApplicationManifestPath (AppId_t appid)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);

  if (! steam_lib_paths)
    return L"";

  if (steam_libs != 0)
  {
    for (int i = 0; i < steam_libs; i++)
    {
      wchar_t    wszManifest [MAX_PATH + 2] = { };
      swprintf ( wszManifest, MAX_PATH + 2,
                   LR"(%s\steamapps\appmanifest_%u.acf)",
               (wchar_t *)steam_lib_paths [i],
                            appid );

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
        return wszManifest;
    }
  }

  return L"";
}

std::wstring
SK_Steam_GetLocalConfigPath (SteamId3_t userid)
{

  wchar_t    wszLocalConfig [MAX_PATH + 2] = { };
  swprintf ( wszLocalConfig, MAX_PATH + 2,
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
SK_GetManifestContentsForAppID (AppId_t appid)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  static AppId_t     manifest_id = 0;
  static std::string manifest;

  if (manifest_id == appid && (! manifest.empty ()))
    return manifest;

  std::wstring wszManifest =
    SK_Steam_GetApplicationManifestPath (appid);

  if (wszManifest.empty ())
    return manifest;

  CHandle hManifest (
    CreateFileW ( wszManifest.c_str (),
                    GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                      nullptr,        OPEN_EXISTING,
                        GetFileAttributesW (wszManifest.c_str ()),
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

      manifest_id = appid;

      return
        manifest;
    }
  }

  return manifest;
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
#define MAX_STEAM_LIBRARIES 16

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
SK_UseManifestToGetAppName (AppId_t appid)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

  if (! manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

    std::string app_name =
      SK_Steam_KeyValues::getValue (
        manifest_data, { "AppState" }, "name"
      );

    if (! app_name.empty ())
    {
      return app_name;
    }
  }

  return "";
}

std::string
SKIF_Steam_GetLaunchOptions (AppId_t appid, SteamId3_t userid)
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
        if (user_localconfig.childs.size() > 0)
        {
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
                return attribs.at("LaunchOptions");
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

      file.close();
    }
  }

  return "";
}

std::string
SK_UseManifestToGetAppOwner (AppId_t appid)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

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
SK_UseManifestToGetInstallDir (AppId_t appid)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

  if (! manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

    std::wstring app_path =
      SK_Steam_KeyValues::getValueAsUTF16 (
        manifest_data, { "AppState" }, "installdir"
      );

    if (! app_path.empty ())
    {
      std::wstring manifest_path =
        SK_Steam_GetApplicationManifestPath (appid);

      wchar_t    app_root [MAX_PATH] = { };
      wcsncpy_s (app_root, MAX_PATH,
            manifest_path.c_str (), _TRUNCATE);

      PathRemoveFileSpecW (app_root);
      PathAppendW         (app_root, L"common\\");

      wchar_t ret [MAX_PATH];

      PathCombineW ( ret, app_root,
                          app_path.c_str () );

      return ret;
    }
  }

  return L"";
}

std::vector <SK_Steam_Depot>
SK_UseManifestToGetDepots (AppId_t appid)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  std::vector <SK_Steam_Depot> depots;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

  if (! manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

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
SK_UseManifestToGetDepotManifest (AppId_t appid, DepotId_t depot)
{
  //PLOG_VERBOSE << "Steam AppID: " << appid;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

  if (! manifest_data.empty ())
  {
    //PLOG_VERBOSE << "Parsing manifest for AppID: " << appid;

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

#define MAX_STEAM_LIBRARIES 16

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

  struct {
    SKIF_DirectoryWatch watch;
    DWORD               signaled = 0;
    int                 count    = 0;
    wchar_t             path [MAX_PATH + 2] = { };
    UINT_PTR            timer;
  } static steam_libraries[MAX_STEAM_LIBRARIES];

  for (int i = 0; i < steam_libs; i++)
  {
    auto& library =
      steam_libraries[i];

    if (! isInitialized)
    {
      swprintf (library.path, MAX_PATH + 2,
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

    if (countFiles || ! isInitialized)
    {
      int currCount = steam_libraries[i].count;

      std::error_code ec;
      std::filesystem::directory_iterator iterator = 
        std::filesystem::directory_iterator (library.path, ec);

      // Only iterate over the files if the directory exists and is accessible
      if (! ec)
      {
        currCount = 0;
        for (auto& directory_entry : iterator)
          if (directory_entry.is_regular_file())
            currCount++;
      }

      if (steam_libraries[i].count != currCount)
      {
        steam_libraries[i].count = currCount;
        isSignaled = true;
      }
    }
  }

  isInitialized = true;

  return isSignaled;
};


// There's two of these:
// *   SK_Steam_GetInstalledAppIDs ( )
// * SKIF_Steam_GetInstalledAppIDs ( ) <- The one below
std::vector <std::pair <std::string, app_record_s>>
SKIF_Steam_GetInstalledAppIDs (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  std::vector <std::pair <std::string, app_record_s>> ret;

  if (! _registry.bLibrarySteam)
    return ret;

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
      ret.emplace_back (
        "Loading...", record
      );
    }
  }

  return ret;
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
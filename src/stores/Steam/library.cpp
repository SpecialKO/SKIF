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

#include <stores/steam/library.h>

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

std::string
SK_UseManifestToGetAppName (AppId_t appid);

std::vector <SK_Steam_Depot>
SK_UseManifestToGetDepots (AppId_t appid);

ManifestId_t
SK_UseManifestToGetDepotManifest (AppId_t appid, DepotId_t depot);

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

std::vector <AppId_t>
SK_Steam_GetInstalledAppIDs (void)
{
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
      wsprintf ( wszManifestDir,
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

  if ( false ) // ! bHasSpecialK)
  {
    wchar_t wszManifestDir [MAX_PATH + 2] = { };

    for (int i = 0; i < steam_libs; i++)
    {
      wsprintf ( wszManifestDir,
                   LR"(%s\steamapps)",
               (wchar_t *)steam_lib_paths [i] );

      if (! PathIsDirectoryW (wszManifestDir))
        continue;
    }

    std::wstring wstr_path = wszManifestDir;

    FILE *fAppManifest =
      _wfopen ((wstr_path + L"\\appmanifest_1157970.acf").c_str (), L"w+");

    if (fAppManifest != nullptr)
    {
      fputs ("AppState\n"
             "{\n"
             "	\"appid\"		                        \"1157970\"\n"
             "	\"Universe\"		                    \"1\"\n"
             "	\"name\"	       	                  \"Special K\"\n"
             "	\"StateFlags\"		                  \"4\"\n"
             "	\"installdir\"		                  \"Special K\"\n"
             "	\"LastUpdated\"		                  \"1595505511\"\n"
             "	\"UpdateResult\"		                \"0\"\n"
             "	\"SizeOnDisk\"		                  \"36732902\"\n"
             "	\"buildid\"		                      \"5321214\"\n"
             "	\"LastOwner\"		                    \"76561198026681638\"\n"
             "	\"BytesToDownload\"		              \"399904\"\n"
             "	\"BytesDownloaded\"		              \"399904\"\n"
             "	\"AutoUpdateBehavior\"		          \"0\"\n"
             "	\"AllowOtherDownloadsWhileRunning\"	\"0\"\n"
             "	\"ScheduledAutoUpdate\"		          \"0\"\n"
             "	\"InstalledDepots\"\n"
             "	{\n"
             "		\"1157971\"\n"
             "		{\n"
             "			\"manifest\"		                \"4600382575410915787\"\n"
             "		}\n"
             "	}\n"
             "	\"MountedDepots\"\n"
             "	{\n"
             "		\"1157971\"		                    \"4600382575410915787\"\n"
             "	}\n"
             "	\"SharedDepots\"\n"
             "	{\n"
             "		\"228988\"		                    \"228980\"\n"
             "		\"228990\"		                    \"228980\"\n"
             "	}\n"
             "	\"UserConfig\"\n"
             "	{\n"
             "		\"language\"		                  \"english\"\n"
             "		\"betakey\"		                    \"\"\n"
             "		\"DisabledDLC\"		                \"1166340\"\n"
             "		\"optionaldlc\"		                \"\"\n"
             "	}\n"
             "}\n",
              fAppManifest);
      fclose (fAppManifest);
    }
  }

  return apps;
}

std::wstring
SK_Steam_GetApplicationManifestPath (AppId_t appid)
{
  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);

  if (! steam_lib_paths)
    return L"";

  if (steam_libs != 0)
  {
    for (int i = 0; i < steam_libs; i++)
    {
      wchar_t    wszManifest [MAX_PATH + 2] = { };
      wsprintf ( wszManifest,
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


std::string
SK_GetManifestContentsForAppID (AppId_t appid)
{
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

const wchar_t*
SK_GetSteamDir (void)
{
  extern bool SKIF_bDisableSteamLibrary;

  static wchar_t
       wszSteamPath [MAX_PATH + 2] = { };
  if (*wszSteamPath == L'\0' && ! SKIF_bDisableSteamLibrary)
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

      lstrcpyW (wszLibraryFolders, wszSteamPath);
      lstrcatW (wszLibraryFolders, LR"(\steamapps\libraryfolders.vdf)");

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
  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

  if (! manifest_data.empty ())
  {
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

std::wstring
SK_UseManifestToGetInstallDir (AppId_t appid)
{
  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

  if (! manifest_data.empty ())
  {
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
  std::vector <SK_Steam_Depot> depots;

  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

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
SK_UseManifestToGetDepotManifest (AppId_t appid, DepotId_t depot)
{
  std::string manifest_data =
    SK_GetManifestContentsForAppID (appid);

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
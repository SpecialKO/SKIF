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

#pragma once

#include <Windows.h>
#include <vector>
#include <stack>

#include <stores/Steam/app_record.h>
#include "utility/sk_utility.h"

//#include "steam/steam_api.h"
#include <utility/vfs.h>
#include <stores/Steam/vdf.h>


extern
  std::unique_ptr <skValveDataFile> appinfo;

using steam_library_t = wchar_t [MAX_PATH + 2];

struct SK_Steam_Depot {
  std::string  name; // Has to be queried w/ WebAPI
  DepotId_t    depot;
  ManifestId_t manifest;
};

enum ConfigStore
{
  ConfigStore_UserLocal,
  ConfigStore_UserRoaming,
  ConfigStore_ALL
};


// Barely functional Steam Key/Value Parser
//   -> Does not handle unquoted kv pairs.
//   -> Does not unescape: \" and \\ are kept as written (but \" does not end a string)
//   -> Input with a { or a string that is never closed (e.g. a truncated file) is rejected as a whole
class SK_Steam_KeyValues
{
public:
  static
  std::vector <std::string>
  getKeys ( const std::string               &input,
            const std::deque  <std::string> &sections,
                  std::vector <std::string> *values = nullptr )
  {
    std::vector <std::string> ret;

    if (sections.empty () || input.empty ())
      return ret;

    struct {
      std::deque <std::string> path;

      struct {
        std::string actual;
        std::string test;
      } heap;

      void heapify (std::deque <std::string> const *sections = nullptr)
      {
        int i = 0;

        auto& in  = (sections == nullptr) ? path        : *sections;
        auto& out = (sections == nullptr) ? heap.actual : heap.test;

        out = "";

        for ( auto& str : in )
        {
          if (i++ > 0)
            out += "\x01";

          out += str;
        }
      }
    } search_tree;

    search_tree.heapify (&sections);

    std::string name   = "";
    std::string value  = "";
    int         quotes = 0;    // 1 = reading the key, 3 = reading the value, 4 = pair complete

    // Results are only handed over once all of the input has been read (see the check after the loop)
    std::vector <std::string> found_keys,
                              found_values;

    const auto _clear = [&](void)
    {
      name.clear  ();
      value.clear ();
      quotes = 0;
    };

    const size_t len = input.size ();

    for (size_t i = 0; i < len; ++i)
    {
      const char c        = input [i];
      const bool inString = (quotes == 1 || quotes == 3);

      if (inString)
      {
        std::string& target = (quotes == 1) ? name : value;

        // Escape sequences (\" and \\) are kept as written, but \" does not end the string
        if (c == '\\' && i + 1 < len)
        {
          target += c;
          target += input [++i];
          continue;
        }

        // Braces inside a quoted string are just text
        if (c != '"')
        {
          target += c;
          continue;
        }
      }

      if (c == '"')
        ++quotes;

      if (quotes == 4)
      {
        if (! _stricmp ( search_tree.heap.test.c_str   (),
                         search_tree.heap.actual.c_str () ) )
        {
          found_keys.emplace_back (name);

          if (values != nullptr)
            found_values.emplace_back (value);
        }

        _clear ();
      }

      if (c == '{')
      {
        search_tree.path.push_back (name);
        search_tree.heapify        (    );

        _clear ();
      }

      else if (c == '}')
      {
        if (! search_tree.path.empty())
          search_tree.path.pop_back ();
        else
          PLOG_ERROR << "Corrupt manifest detected!";

        search_tree.heapify ( );

        _clear ();
      }

      else { search_tree.heapify (); } // Needed to be able to handle key/value pairs placed after { } objects
    }

    // An unclosed { or string means the input is incomplete (e.g. a truncated file)
    if (! search_tree.path.empty () || quotes == 1 || quotes == 3)
      return ret;

    ret = std::move (found_keys);

    if (values != nullptr)
      values->insert (values->end ( ), found_values.begin ( ), found_values.end ( ));

    return ret;
  }

  static
  std::string
  getValue ( const std::string              &input,
             const std::deque <std::string> &sections,
             const std::string              &key )
  {
    std::vector <std::string> values;
    std::vector <std::string> keys (
      SK_Steam_KeyValues::getKeys (input, sections, &values)
    );

    int idx = 0;

    for ( auto& it : keys )
    {
      if (it._Equal (key))
        return values [idx];

      ++idx;
    }

    return "";
  }

  static
  std::wstring
  getValueAsUTF16 ( const std::string              &input,
                    const std::deque <std::string> &sections,
                    const std::string              &key )
  {
    std::vector <std::string> values;
    std::vector <std::string> keys (
      SK_Steam_KeyValues::getKeys (input, sections, &values)
    );

    int idx = 0;

    for ( auto& it : keys )
    {
      if (it._Equal (key))
      {
        return
          SK_UTF8ToWideChar (
            values [idx]
          );
      }

      ++idx;
    }

    return L"";
  }
};

int                          SK_VFS_ScanTree (SK_VirtualFS::vfsNode* pVFSRoot,
                                                            wchar_t* wszDir,
                                                            wchar_t* wszPattern        = L"*",
                                                                int  max_depth         = 1,
                                                                int      depth         = 0,
                                              SK_VirtualFS::vfsNode* pVFSImmutableRoot = nullptr
);
int                          SK_Steam_GetLibraries               (steam_library_t **ppLibraries);
struct app_record_steam_s;

// Steam client state (steam_library.cpp), updated by the library tab
extern bool                  steamRunning;  // The Steam client is running
extern bool                  steamFallback; // Registry monitoring is unavailable: running state comes from the process scan

std::string                  SK_GetManifestContentsForAppID      (app_record_steam_s *app);
const wchar_t *              SK_GetSteamDir                      (void);
const  char   *              SK_GetSteamDirUTF8                  (void);
std::wstring                 SK_UseManifestToGetInstallDir       (app_record_steam_s *app);
std::string                  SK_UseManifestToGetAppName          (app_record_steam_s *app);
std::string                  SK_UseManifestToGetCurrentBranch    (app_record_steam_s *app);
std::string                  SK_UseManifestToGetAppOwner         (app_record_steam_s *app);
std::vector <SK_Steam_Depot> SK_UseManifestToGetDepots           (app_record_steam_s *app);
ManifestId_t                 SK_UseManifestToGetDepotManifest    (app_record_steam_s *app, DepotId_t depot);
std::string                  SKIF_Steam_GetLaunchOptions         (AppId_t appid, SteamId3_t userid, app_record_steam_s *app = nullptr);
std::string                  SKIF_Steam_GetUserConfigStore       (SteamId3_t userid, ConfigStore config);
std::wstring                 SKIF_Steam_GetUserConfigStorePath   (SteamId3_t userid, ConfigStore config);
void                         SKIF_Steam_PreloadUserConfig        (SteamId3_t userid, app_list_t *apps, std::set <std::string> *apptickets);
bool                         SKIF_Steam_isSteamOverlayEnabled    (AppId_t appid, SteamId3_t userid);
bool                         SKIF_Steam_areLibrariesSignaled     (void);
void                         SKIF_Steam_GetInstalledAppIDs       (app_list_t *apps);
bool                         SKIF_Steam_HasActiveProcessChanged  (app_list_t *apps, std::set <std::string> *apptickets);
SteamId3_t                   SKIF_Steam_GetCurrentUser           (void);
DWORD                        SKIF_Steam_GetActiveProcess         (void);
std::wstring                 SKIF_Steam_GetAppStateString        (HKEY* hKey, const wchar_t *wszStateKey);
wchar_t                      SKIF_Steam_GetAppStateDWORD         (HKEY* hKey, const wchar_t *wszStateKey, DWORD *pdwStateVal);
bool                         SKIF_Steam_UpdateAppState           (app_record_s *pApp);
void                         SKIF_Steam_IdentifyAssetPCGW        (uint32_t app_id);
std::wstring                 SKIF_Steam_GetCoverURI              (uint32_t app_id, bool force_2x);
std::wstring                 SKIF_SteamWebAPI_AppDetails         (uint32_t app_id);
std::wstring                 SKIF_SteamWebAPI_IStoreBrowseService_GetItems1 (uint32_t app_id, std::string country_code);
std::wstring                 SKIF_SteamWebAPI_ISteamUserStats_GetNumberOfCurrentPlayers1 (uint32_t app_id);

// Library record for a Steam game
struct app_record_steam_s : app_record_store_s <app_record_steam_s, app_record_s::Store::Steam>
{
  using app_record_store_s::app_record_store_s;

  // Steam-specific data
  struct steam_s {
    struct {
      std::string launch_option        = ""; // Holds the custom launch option set in the Steam client
      std::string launch_option_parsed = ""; // Holds a cached parsed value of the launch option set in the Steam client
    } local;

    struct {
      int hidden   = 0; // Hidden in Steam Client (not the same as flagged as private)
      int favorite = 0; // Favorited in Steam Client
      std::vector <std::string> tags; // ???
    } shared; // roaming

    struct {
      std::wstring AppDetails      = L"";
      std:: string AppDetails_utf8 =  "";
      std::wstring IStoreBrowseService_GetItems1      = L"";
      std:: string IStoreBrowseService_GetItems1_utf8 =  "";
      std::wstring ISteamUserStats_GetNumberOfCurrentPlayers1      = L"";
      std:: string ISteamUserStats_GetNumberOfCurrentPlayers1_utf8 =  "";
    } urls; // Used by the Developer menu

    std::string  manifest_data    =  "";
    std::wstring manifest_path    = L"";
    std::string  branch           = "public"; // Holds the current "beta" branch set in the Steam client (default: public)
  } steam;

  bool        isInstantPlayCompatible (void) const override { return true; }
  std::string GetPCGamingWikiURL      (void) const override { return "https://www.pcgamingwiki.com/api/appid.php?appid=" + std::to_string (id); }
  std::string GetSteamGridDBURL       (bool icons) const override { return SKIF_Util_FormatStringRaw ((icons) ? "https://www.steamgriddb.com/steam/%lu/icons"
                                                                                                             : "https://www.steamgriddb.com/steam/%lu/grids", id); }
  void        RefreshClientState      (DWORD dwTimeNow) override; // steam_library.cpp

  // Launching (steam_library.cpp)
  bool         RequiresValidExecutable (void) const override { return false; } // Launches through the Steam client
  void         WhitelistForInjection   (launch_config_s* config) override { UNREFERENCED_PARAMETER (config); } // Steam games need no manual whitelisting
  bool         PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override;
  void         Launch                  (const launch_context_s& ctx) override;
  uint32_t     GetSteamAppID           (void) const override { return id; }
  std::string  GetStoreLaunchOptions   (uint32_t steamAppID) override;
  std::wstring GetJumpListIconPath     (launch_config_s* config) const override;

  // Library UI (steam_library.cpp)
  bool         GetInstantPlayDefault   (void) const override;
  std::wstring GetStoreAssetDirectory  (void) const override;
  std::string  GetClientLaunchOptions  (void) const override { return steam.local.launch_option; }
  std::string  GetClientCommandLine    (launch_config_s* config) override;
  int          GetClientFavorite       (void) const override { return steam.shared.favorite; }
  int          GetClientHidden         (void) const override { return steam.shared.hidden;   }
  bool         HasLegacySpecialKVersion(void) const override;
  bool         HasSpecialKModInstaller (void) const override;

  // Artwork (steam_library.cpp)
  std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged) override;
  std::wstring PrepareCoverSource      (bool queryPCGW) override;
  bool         WantsPCGWCover          (void) const override;
  bool         WantsIGDBCover          (void) const override;
  bool         DownloadIGDBCover       (const std::wstring& targetPath) override;
  std::wstring GetIconSource           (void) const override;
  void         PrepareIconSource       (void) override;
  bool         DeleteStoreCoverFiles   (const std::wstring& assetDir) override;
  std::string  GetLocalizedName        (void)       override;
  bool         CanCreateDesktopShortcut(void)       override;
  std::wstring GetDesktopShortcutArgs  (void)       override;
  std::vector <std::pair <std::string, std::string>>
               GetStoreDebugFields     (void) const override;

  // Context menu hooks (steam_library.cpp)
  void         DrawStoreMenu                  (void) override;
  void         DrawWebsiteMenuItems           (void) override;
  void         DrawDeveloperDataMenus         (void) override;
  void         DrawDeveloperLaunchConfigItems (launch_config_s& launch) override;

  // Library population and selection (steam_library.cpp)
  void         RefreshInstallState        (void) override;
  std::string  GetFallbackName            (void) override { return GetLocalizedName ( ); } // Both come from the app manifest
  void         ResolveInstallDirectory    (void) override;
  void         ResolveStoreDetails        (void) override;
  void         LoadStoreDetails           (void) override;
  bool         CanLaunchExecutableDirectly(void) override;

  // Library processing: the app manifest is in the library the game is installed to (known from the library scan)
  std::wstring GetStorageLocation      (void) const override { return (! steam.manifest_path.empty ( )) ? steam.manifest_path : install_dir; }

  // Run-state detection (steam_library.cpp)
  ProcessMatch MatchProcess                (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen) override;
  bool         KeepsRunningStateDuringScan (void) const override;
  bool         IsRunningStateFromClient    (void) const override;
  bool         HasPendingStoreDetails      (void) const override { return (id != 0 && id != SKIF_STEAM_APPID && ! processed); }

  // Steam parses its original launch configs again when a game is selected, so online ones are kept apart and merged then
  std::map <int, launch_config_s>&
              GetOnlineLaunchConfigs  (void) override { return launch_configs_custom; }

  // Library refresh: launch_configs is built on selection (appinfo.vdf), so only the online ones are compared. A game
  //   update, branch change, etc. changes the app manifest, so its record is replaced then.
  const std::map <int, launch_config_s>&
               GetPopulatedLaunchConfigs (void) const override { return launch_configs_custom; }
  bool         HasSameLibraryData        (const app_record_s& other) const override
  {
    if (! app_record_s::HasSameLibraryData (other))
      return false;

    const steam_s& o = static_cast <const app_record_steam_s&> (other).steam;

    return (steam.manifest_data       == o.manifest_data       &&
            steam.manifest_path       == o.manifest_path       &&
            steam.branch              == o.branch              &&
            steam.local.launch_option == o.local.launch_option &&
            steam.shared.hidden       == o.shared.hidden       &&
            steam.shared.favorite     == o.shared.favorite     &&
            steam.shared.tags         == o.shared.tags);
  }
};

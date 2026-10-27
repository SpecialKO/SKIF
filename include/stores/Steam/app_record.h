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

//#include <stores/Steam/steam_library.h>

#include <map>
#include <string>
#include <strsafe.h>
#include <assert.h>
#include <Shlwapi.h>

#include <utility/sk_utility.h>

#include <Windows.h>

#include <utility/utility.h>

//#include "steam/steam_api.h"
#include <utility/vfs.h>

#include <vector>
#include <memory>
#include <typeinfo>

#include <stores/generic_library.h> // d3d11_tex_ref_s


// Steam App ID of Special K itself; also used for SKIF's own entry in the library
constexpr int SKIF_STEAM_APPID = 1157970;

// Base record for a game in the library. Each store has its own record type derived from this
//   (see app_record_store_s below and the store headers); store-specific behavior lives in the
//   virtual functions, so consumers should call those instead of switching on `store`.
enum class LibraryTexture; // stores/generic_library2.h
struct     app_key_s;      // Below app_record_s

struct app_record_s {
  app_record_s (uint32_t id_) : id (id_) { };
  virtual ~app_record_s (void) = default;

  app_record_s            (const app_record_s&) = default;
  app_record_s& operator= (const app_record_s&) = default;

  // Declared explicitly: the virtual destructor and the copy operations above would otherwise
  //   suppress the implicit move operations, turning every std::move of a record into a deep copy.
  app_record_s            (app_record_s&&) noexcept = default;
  app_record_s& operator= (app_record_s&&) noexcept = default;

  // Polymorphic copy (game workers refresh a copy of a record on a background thread).
  //   Clone    returns a heap copy of the same record type.
  //   CopyFrom assigns from a record of the same type in place, so the record keeps its address.
  virtual std::unique_ptr <app_record_s>
                       Clone                   (void) const                 { return std::make_unique <app_record_s> (*this); }
  virtual void         CopyFrom                (const app_record_s& src)    { *this = src; }

  // Store-specific behavior
  virtual bool         isInstantPlayCompatible (void) const                 { return false; }                // Steam, GOG, Xbox, EA, itch
  virtual std::string  GetPersistentKey        (void) const                 { return std::to_string (id); }  // Key used in db.json and lc.json

  struct client_state_s {
    bool refresh    (app_record_s *pApp);

    DWORD running     = 0;
    DWORD installed   = 0;
    DWORD updating    = 0;

    DWORD running_pid = 0;

    // For Registry Key Watch
    static DWORD
      _TimeLastNotified;

    DWORD                    dwTimeDelayChecks = 0; // Used to prevent the status from changing for X number of milliseconds.
    DWORD                    dwTimeLastChecked = 0;
    void invalidate (void) { dwTimeLastChecked = 0; }
  } _status, _staging;

  struct names_s {
    std::string all_upper;
    std::string all_upper_alnum;
    std::string original;  // Holds the original name
    std::string clean;     // Holds a cleaned copy of the original name
    std::string normal;    // Name used in SKIF, and any custom name applied
    size_t      pre_stripped = 0;
  } names;

  struct tex_registry_s {
    d3d11_tex_ref_s texture;
    bool            isCustom    = false; // Indicates whether SKIF is using a custom texture or not
    bool            isManaged   = false; // Indicates whether the texture is managed by SKIF or not (can be refreshed by SKIF)
    bool            queriedPCGW = false; // Has we queried PCGW for an artwork?
    int             iWorker     = 0;     // 0 = worker not started, 1 = worker active, 2 = worker done
    HANDLE          hWorker     = NULL;
  } tex_icon, tex_cover;

  enum class Store {
    Steam       = 0x1,   // Initial commit
    GOG         = 0x2,   // Sep 17, 2021
    Custom      = 0x3,   // Oct  2, 2021 - SKIF custom games
    Epic        = 0x4,   // Dec 27, 2021
    Xbox        = 0x5,   // Mar  6, 2022
    EA          = 0x6,   // Oct 26, 2026
    itch        = 0x7,   // Oct 27, 2026
    Unspecified = 0xffff
  };

  enum class Platform {
    Unknown = 0x0,
    Windows = 0x1,
    Linux   = 0x2,
    Mac     = 0x4,
    All     = 0xffff
  };

  enum class CPUType {
    Common = 0x0, // Check the common config if encountered
    x86    = 0x1,
    x64    = 0x2,
    Any    = 0xffff
  };

  struct sk_install_state_s {
    struct Injection {
      enum class Bitness {
        ThirtyTwo = 0x1,
        SixtyFour = 0x2,
        Unknown   = 0x0
      }            bitness = Bitness::Unknown;
#if 0
      enum class EntryPoint {
        DDraw   = 0x1,
        D3D8    = 0x2,
        D3D9    = 0x4,
        D3D11   = 0x8,
        DXGI    = 0x10,
        OpenGL  = 0x20,
        DInput8 = 0x40,
        CBTHook = 0x80,
        Unknown = 0x0
      }            entry_pt = EntryPoint::Unknown;
#endif
      enum class Type {
        Global  = 0x1,
        Local   = 0x2,
        Unknown = 0x0
      }            type     = Type::Unknown;
    } injection;

    struct Config {
      enum class Type {
        Centralized = 0x1,
        Localized   = 0x2,
        Unknown     = 0x0
      }            type = Type::Unknown;
      std:: string type_utf8 = "Centralized"; // Assume global by default

      std::wstring shorthand      = L"SpecialK.ini"; // Assume global by default
      std:: string shorthand_utf8 =  "SpecialK.ini"; // Converted to utf-8 from utf-16
      std::wstring root_dir;
      std:: string root_dir_utf8;  // Converted to utf-8 from utf-16
      std::wstring full_path;
      std:: string full_path_utf8; // Converted to utf-8 from utf-16
    } config;

    struct {
      std::wstring shorthand;
      std:: string shorthand_utf8; // Converted to utf-8 from utf-16
      std::wstring version;
      std:: string version_utf8;   // Converted to utf-8 from utf-16
      std::wstring full_path;
      std:: string full_path_utf8; // Converted to utf-8 from utf-16
    } dll;

    std::string    localized_name; // UTF-8
  };


  struct common_config_s {

    enum class AppType {   // Used by Steam to indicate the type of "app" we're dealing with
      Unknown     = 0x0,   // Default in SKIF
      Game        = 0x1,   // game
      Application = 0x2,   // application
      Tool        = 0x3,   // tool
      Music       = 0x4,   // music
      Demo        = 0x5,   // demo
      Unspecified = 0xffff // not expected to be seen
    };

    uint32_t     appid     = 0;
    CPUType      cpu_type  = CPUType::Any;     // This is an utterly useless key on Steam -- do not expect it to contain anything important! It's either not set, empty, or set to 64 -- nothing else!
    AppType      type      = AppType::Unknown; // Steam
    std::string  icon_hash;                    // Steam's internal name for the icon (40 digits)
    std::string  boxart_hash;                  // Alternate filesystem location for boxart
  };

  struct extended_config_s {
    struct vac_config_s    {
      uint32_t    vacmacmodulecache =     0;
      uint32_t    vacmodulecache    =     0;
      std::string vacmodulefilename =    "";
      int         enabled           =    -1;
    } vac;

    struct developer_config_s {
      std::string publisher;
      std::string developer;
      std::string homepage;
    } dev;
  } extended_config;

  struct launch_config_s {

    enum class Type {
      Default     = 0x0,   // default - Launch (Default)
      Option1     = 0x1,   // option1 - Play %%description%% (previously Config)
      Option2     = 0x2,   // option2 - Play %%description%%
      Option3     = 0x3,   // option3 - Play %%description%%
      Unspecified = 0xffff // none    - Unspecified
    };

    int          id        = 0; // SKIF's internal launch identifier
    int          id_steam  = 0; // Steam's internal launch identifer
    sk_install_state_s     injection;

    Type         type      = Type::Unspecified;
    CPUType      cpu_type  = CPUType::Common;
    Platform     platforms = Platform::All;

    std::wstring getBlacklistFilename       (void);
    bool         setBlacklisted             (bool blacklist);
    bool          isBlacklisted             (bool refresh = false);

    std::wstring getElevatedFilename        (void);
    bool         setElevated                (bool elevated);
    bool          isElevated                (bool refresh = false);

    std::wstring getExecutableFileName      (void);
    std:: string getExecutableFileNameUTF8  (void);
    bool          isExecutableFileNameValid (void);

    std::wstring getExecutableDir           (void) const;
    std:: string getExecutableDirUTF8       (void) const;
    bool          isExecutableDirValid      (void) const;

    std::wstring getExecutableFullPath      (void) const;
    std:: string getExecutableFullPathUTF8  (void);
    bool          isExecutableFullPathValid (void);

    std::wstring getDescription             (void);
    std:: string getDescriptionUTF8         (void);

    std::wstring getLaunchOptions           (void) const;
    std:: string getLaunchOptionsUTF8       (void);

    std::wstring getWorkingDirectory        (void) const;
    std:: string getWorkingDirectoryUTF8    (void);

    std::wstring getWorkOrExeDirectory      (void) const;
    std:: string getWorkOrExeDirectoryUTF8  (void);

  //private:
  //app_record_s* parent = nullptr;

    std::wstring executable;
    std:: string executable_utf8;
    int          executable_valid      = -1;

    std::wstring executable_path;
    std:: string executable_path_utf8;
    int          executable_path_valid = -1;

    std::wstring install_dir;
    std:: string Xbox_ApplicationId; // e.g. HaloMCCShipping or HaloMCCShippingNoEAC

    std::wstring description;
    std:: string description_utf8;
    std::wstring launch_options;
    std:: string launch_options_utf8;
    std::wstring working_dir;
    std:: string working_dir_utf8;
    std::wstring blacklist_file;
    std::wstring elevated_file;
    std::wstring executable_helper;  // Used by Xbox to hold gamelaunchhelper.exe
    std::set <std::string>
                 branches;           // Steam: Only show launch option if one of these beta branches are active
    std:: string branches_joined;
    std:: string requires_dlc;       // Steam: Only show launch option if this DLC is owned

    int          valid              = -1;    // Launch config is valid (what does this actually mean?)
    bool         duplicate_exe      = false; // Used for Steam games indicating that a launch option is a duplicate (shares the same executable as another)
    bool         duplicate_exe_args = false; // Used for Steam games indicating that a launch option is a duplicate (shares the same executable and arguments as another)
    bool         custom_skif        = false; // Is the launch config an online-based custom one populated by SKIF ?
    bool         custom_user        = false; // Is the launch config a user-specied custom one?
    int          owns_dlc           = -1;    // Does the user own the required DLC ?
    int          blacklisted        = -1;
    int          elevated           = -1;
  };

  struct cloud_save_record_s {
    Platform     platforms = Platform::Unknown;

    std::wstring root;
    std::wstring path;
    std::wstring evaluated_dir;
    std::wstring pattern;

    int          valid = -1; // Path points to a real directory
  };

  struct branch_record_s {
    app_record_s *parent;
    uint32_t      build_id;
    uint32_t      pwd_required;
    time_t        time_updated;
    std::wstring  description;
    std:: string  description_utf8; // For non-Latin characters to print in ImGui
    std::wstring  time_string;      // Cached text representation
    std:: string  time_string_utf8; // Cached text representation

    std::wstring getDescription     (void) const;
    std:: string getDescriptionUTF8 (void);
    std::wstring getTime            (void);
    std:: string getTimeUTF8        (void);
  };


  // Cached data
  struct CloudPath
  {
    std::wstring path;
    std:: string path_utf8;
    std:: string label;

    CloudPath (int i, std::wstring p)
    {
      path      = p;
      path_utf8 = SK_WideCharToUTF8 (p);
      label     =
        SK_FormatString ("%s###CloudUFS.%d", path_utf8.c_str(), i);
    };
  };

  using branch_t =
    std::pair <std::string, branch_record_s>;

  // This struct holds the cache for the right click context menu
  struct {
    int                                   numSecondaryLaunchConfigs = 0; // Secondary launch options
  //bool                                  profileFolderExists       = false; // 2024-03-04: Not actually used any longer
    bool                                  screenshotsFolderExists   = false;
    std::wstring                          wsScreenshotDir           = L"";
    std::vector   <CloudPath>             cloud_paths;   // Steam Auto-Cloud
    std::multimap <int64_t, branch_t>     branches;      // Steam Branches

    // PCGamingWiki
    std:: string pcgw;

    // SteamGridDB
    std:: string sgdbGrids;
    std:: string sgdbIcons;

    std:: string label_version; // type_version
  } ui;

  struct specialk_config_s {
    std::wstring           profile_dir;
    std:: string           profile_dir_utf8;
    std::set <std::string> screenshots; // utf8 path
    sk_install_state_s     injection;
  } specialk;

  std::map <std::string, branch_record_s    > branches;
  std::map <int,         cloud_save_record_s> cloud_saves;
  std::map <int,         launch_config_s    > launch_configs;
  std::map <int,         launch_config_s    > launch_configs_custom; // Workaround for Steam games parsing original launch configs on selection
  common_config_s                             common_config;

  uint32_t     id;
  bool         processed             =  false; // indicates if we have processed appinfo
  bool         loading               =  false; // indicates if we are processing in a background thread
  bool         filtered              =  false; // indicates if the app has been filtered out (used for search; not the same as the skif.hidden state!)
  bool         cloud_enabled         =   true; // hidecloudui=false
  std::wstring install_dir; // Should NOT be backslash-terminated
  Store        store                 =  Store::Unspecified;
  std::string  store_utf8            =  "";

  std::string  ImGuiPushID           =  "";
  std::string  ImGuiLabelID          =  "";

  // Struct used to hold custom SKIF metadata about the game
  struct custom_metadata_s {
    std::string        name;
    int            cpu_type =  0; // 0 = Common,             1 = x86,                 2 = x64,                0xFFFF = Any
    int        instant_play =  0; // 0 = use global default, 1 = always instant play, 2 = never instant play
    int           auto_stop =  0; // 0 = use global default, 1 = stop on injection,   2 = stop on game exit,  3 = never stop
    int                uses =  0; // Number of times game has been launched
    std::string        used = ""; // Unix timestamp (in string) of when the game was last used
    std::string        used_formatted = ""; // Friendly human-readable representation of the Unix timestamp
    std::string    category = ""; // Category to sort the game under
    int              hidden = -1; // Is app hidden?    (-1 = unset;   0 = no;   1 = yes)
    int              pinned = -1; // Is app favorited? (-1 = unset;   0 = no;   1 = yes;   51 = yes (on top);   99 = Special K)
  } skif;

  bool           launch_failed           = false;

  // Store-specific behavior (continued; declared here as they use the nested types above)
  virtual std::string  GetPCGamingWikiURL      (void)       const { return "https://www.pcgamingwiki.com/w/index.php?search=" + names.clean; }
  virtual std::string  GetSteamGridDBURL       (bool icons) const { return SKIF_Util_FormatStringRaw ((icons) ? "https://www.steamgriddb.com/search/icons?term=%s"
                                                                                                                 : "https://www.steamgriddb.com/search/grids?term=%s", names.clean.c_str ( )); }
  virtual void         RefreshClientState      (DWORD dwTimeNow)  { UNREFERENCED_PARAMETER (dwTimeNow); } // Polls the store client for running/updating state (Steam only)

  // Where launch configs from lc.json / lc_user.json are added
  virtual std::map <int, launch_config_s>&
                       GetOnlineLaunchConfigs  (void)             { return launch_configs; }

  // Launching. LaunchGame ( ) in library.cpp does the shared preparation (usage stats, injection
  //   service, instant-launch decision) and then calls Launch ( ) with this context.
  struct launch_context_s {
    launch_config_s* config         = nullptr; // Launch config to use
    bool             instant        = false;   // Launch the executable directly instead of through the store client
    bool             usingSK        = false;   // Special K will be injected through the global injection service
    bool             localInjection = false;   // Special K is injected through a local DLL
    DWORD            timeNow        = 0;
  };

  virtual bool         RequiresValidExecutable (void) const               { return true; }   // Steam / Epic launch through their client
  virtual void         WhitelistForInjection   (launch_config_s* config);                     // app_record.cpp
  virtual bool         PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const { UNREFERENCED_PARAMETER (fromGameMenu); UNREFERENCED_PARAMETER (config); return false; }
  virtual void         Launch                  (const launch_context_s& ctx);                 // app_record.cpp: instant launch if requested, else nothing applicable
  void                 LaunchInstant           (const launch_context_s& ctx);                 // app_record.cpp: runs the executable directly

  // Hooks used by LaunchInstant ( )
  virtual uint32_t     GetSteamAppID           (void) const                { return 0; }      // Steam App ID exposed to the game (SteamAppId)
  virtual std::string  GetStoreLaunchOptions   (uint32_t steamAppID)       { UNREFERENCED_PARAMETER (steamAppID); return ""; } // Launch options set in the store client
  virtual std::wstring GetJumpListIconPath     (launch_config_s* config) const { return config->getExecutableFullPath ( ); }

  // Per-game Instant Play preference (skif.instant_play: 0 = global default, 1 = always, 2 = never)
  bool                 UsesInstantPlay         (bool globalDefault) const { return (skif.instant_play == 1 || (skif.instant_play == 0 && globalDefault)); }
  virtual bool         GetInstantPlayDefault   (void) const               { return false; }  // The store's global Instant Play setting

  // Library UI
  std::wstring         GetAssetDirectory       (void) const;                                   // app_record.cpp: SK's Assets folder for this game (backslash-terminated), or empty
  virtual std::wstring GetStoreAssetDirectory  (void) const               { return L""; }     // Assets\<Store>\<id or key>\ (store .cpp files)
  virtual bool         IsUserAdded             (void) const               { return false; }   // Custom game added by the user (editable target, removable)
  virtual std::string  GetClientLaunchOptions  (void) const               { return ""; }      // Launch options the user set in the store client
  virtual std::string  GetClientCommandLine    (launch_config_s* config)  { UNREFERENCED_PARAMETER (config); return ""; } // Command line built from them when they use %command% (Steam)

  // Special K compatibility
  virtual bool         HasLegacySpecialKVersion(void) const               { return false; }   // Game is known to use an older, game-specific Special K: skip the local DLL update check
  virtual bool         HasSpecialKModInstaller (void) const               { return false; }   // Game has a better, game-specific mod offered through the installer button

  // Artwork (covers and icons). Custom artwork is <GetAssetDirectory ( )>cover / icon (.png, .jpg; .ico for icons).
  //   The source hint comes from PrepareCoverSource ( ) / GetIconSource ( ) and is passed through LoadLibraryTexture.
  std::wstring         FindCustomArtwork       (LibraryTexture type) const;                    // app_record.cpp: the user's custom cover / icon, or empty
  virtual std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged); // File for LoadLibraryTexture to load. Base: custom, else the executable's icon
  virtual std::wstring PrepareCoverSource      (bool queryPCGW) { UNREFERENCED_PARAMETER (queryPCGW); return L""; } // Cover worker thread: fetch / download the store cover; returns the source hint. queryPCGW: also look up a PCGamingWiki cover
  virtual bool         WantsPCGWCover          (void) const { return false; }                  // PCGamingWiki covers are enabled for this store (Settings: Steam, GOG)
  virtual bool         WantsIGDBCover          (void) const { return false; }                  // IGDB covers are preferred over the store's own cover (Settings: Epic, GOG, Steam, Xbox); a PCGamingWiki cover still comes first
  virtual bool         DownloadIGDBCover       (const std::wstring& targetPath);               // app_record.cpp, cover worker thread: look the game up on IGDB. Base: by name; stores with an IGDB source try their store ID first
  std::wstring         GetPreferredIGDBCover   (void) const;                                   // app_record.cpp: <GetAssetDirectory ( )>cover-igdb.jpg when IGDB covers are preferred and there is no custom or PCGamingWiki cover; else empty
  virtual std::wstring GetIconSource           (void) const { return L"icon"; }                // Icon source hint (Steam: library cache icon; GOG: goggame-<id>.ico)
  virtual void         PrepareIconSource       (void)       { }                                // Main thread, before loading the icon (Steam: icon hash from appinfo.vdf)
  virtual bool         HasStoreCover           (void) const { return ! IsSpecialK ( ); }       // A default cover exists, so the cover menu offers Refresh / Reset
  virtual bool         DeleteStoreCoverFiles   (const std::wstring& assetDir) { UNREFERENCED_PARAMETER (assetDir); return false; } // Cover reset: delete store-managed fallbacks; true if any was removed
  virtual bool         CoverNeedsClientSignIn  (void) const { return false; }                  // The cover is missing because the store client is signed out (GOG Galaxy)
  virtual std::string  GetLocalizedName        (void)                     { return names.original; } // Name used for the Special K profile folder
  virtual bool         CanCreateDesktopShortcut(void)                     { return false; }
  virtual std::wstring GetDesktopShortcutArgs  (void);                                         // app_record.cpp: SKIF arguments that launch the game
  virtual std::wstring GetDesktopShortcutIcon  (void)                     { return launch_configs[0].getExecutableFullPath ( ); }
  virtual std::vector <std::pair <std::string, std::string>>
                       GetStoreDebugFields     (void) const               { return { }; }      // (label, value) pairs for the "<Store> Data" developer menu

  // Context menu hooks. library.cpp calls these at fixed points in the game context menu; stores draw
  //   their own items there (in their .cpp files) using SKIF's ImGui helpers. All are no-ops by default.
  virtual void         DrawStoreMenu                  (void) { }                       // Top-level store submenu (Steam)
  virtual void         DrawWebsiteMenuItems           (void) { }                       // "Websites", before PCGamingWiki (Steam, GOG)
  virtual void         DrawDeveloperDataMenus         (void) { }                       // "Developer", after "Application" (Steam Web API)
  virtual void         DrawDeveloperLaunchConfigItems (launch_config_s& launch) { UNREFERENCED_PARAMETER (launch); } // "Developer" > "Launches" > each config, at the end (Steam, Xbox)
  virtual void         DrawDeveloperActions           (void) { }                       // "Developer", before "Copy ID" (Xbox: Open Terminal)

  // Special K's own entry in the library
  bool                 IsSpecialK                 (void) const { return (store == Store::Steam && id == SKIF_STEAM_APPID); }

  // This game's platform as a SKIF_StoreFlag bit (registry.h), for the per-platform cover preferences
  int                  GetStoreFlag               (void) const { const int s = static_cast <int> (store); return (s >= 1 && s <= 31) ? (1 << (s - 1)) : 0; }

  // Library population (LibraryWorkerThread in games.cpp), in the order they are called
  virtual void         RefreshInstallState        (void)       { }                   // Steam: queries the client; uninstalled games get id 0
  virtual std::string  GetFallbackName            (void)       { return ""; }        // Name if the scanner found none (Steam: app manifest)
  virtual bool         ValidatesInstallDirectory  (void) const { return true; }      // Whether a missing install folder drops the game (Xbox: no)
  virtual void         ResolveInstallDirectory    (void)       { }                   // Steam: from the app manifest
  virtual void         ResolveStoreDetails        (void)       { }                   // After the install folder is confirmed (Steam: active branch)
  virtual std::string  GetAppIdMapping            (void) const { return ""; }        // Path-to-AppID registry mapping for Special K (Epic: AppName)
  virtual std::vector <std::wstring>
                       GetAdditionalProfilePaths  (void) const { return { }; }       // Extra folders mapped to the profile name (Xbox)
  virtual std::wstring GetStorageLocation         (void) const { return install_dir; } // Where the game's files are: the library worker groups games by its volume (Steam: the app manifest)

  // Selection
  virtual void         LoadStoreDetails           (void)       { }                   // Main thread, before the game worker runs (Steam: appinfo.vdf)

  // State set in the store client (Steam), used as defaults when SKIF's own value is unset (-1)
  virtual int          GetClientFavorite          (void) const { return 0; }         // 1 = favorited in the store client
  virtual int          GetClientHidden            (void) const { return 0; }         // 1 = hidden in the store client
  bool                 IsFavorite                 (void) const { return (skif.pinned > 0 || (skif.pinned == -1 && GetClientFavorite ( ) == 1)); }
  bool                 IsHidden                   (void) const { return (skif.hidden == 1 || (skif.hidden == -1 && GetClientHidden   ( ) == 1)); }
  app_key_s            GetKey                     (void) const; // (store, id): identifies the game across library refreshes
  virtual bool         CanLaunchExecutableDirectly(void)       { return true; }      // Steam: primary launch config has a valid executable and the game isn't updating

  // Run-state detection (RefreshRunningApps in games.cpp)
  enum class ProcessMatch {
    None,    // Not this game's process
    Running, // This game's process: mark it as running and stop looking
    PidOnly  // This game's process, but the store client owns the running state: record the PID and keep looking
  };

  virtual ProcessMatch MatchProcess                (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen); // app_record.cpp: full path match
  virtual bool         KeepsRunningStateDuringScan (void) const { return false; } // Keep the staged running state instead of clearing it before the process scan (Steam: unless in process fallback without the client)
  virtual bool         IsRunningStateFromClient    (void) const { return false; } // The store client owns the running state, so the scan result is not committed (Steam: client running, registry monitoring available)
  virtual bool         HasPendingStoreDetails      (void) const { return false; } // Store data still to be parsed in the background (Steam: appinfo.vdf, for the process fallback)

  template <class _Tp> static
    constexpr bool
      supports (_Tp bitset, _Tp test)
      {
        return
          ( static_cast <int> (bitset) &
            static_cast <int> (test)   ) ==
            static_cast <int> (test);
      }

  template <class _Tp> static
    constexpr _Tp
      addSupportFor (_Tp bitset, _Tp instance)
      {
        bitset =
          static_cast <_Tp> (
            ( static_cast <int> (bitset)   |
              static_cast <int> (instance) )
          );

        return bitset;
      }
};

using InjectionBitness =
  app_record_s::sk_install_state_s::Injection::Bitness;
using InjectionType =
  app_record_s::sk_install_state_s::Injection::Type;
using ConfigType =
  app_record_s::sk_install_state_s::Config::Type;

// An entry in the game list: the (cleaned) display name used for sorting/searching, and the record itself.
// Records are heap-allocated so the list can later hold store-specific derived types without slicing.
using app_entry_t = std::pair <std::string, std::unique_ptr <app_record_s>>;
using app_list_t  = std::vector <app_entry_t>;

// Identifies a game across library refreshes (records are replaced on every refresh): (store, appid)
struct app_key_s {
  app_record_s::Store store = app_record_s::Store::Unspecified;
  uint32_t            appid = 0;

  bool operator== (const app_key_s& other) const { return (store == other.store && appid == other.appid); }
  bool operator!= (const app_key_s& other) const { return ! (*this == other); }
};

inline app_key_s app_record_s::GetKey (void) const { return { store, id }; }

// Helper base for the per-store record types: fixes the store, and implements Clone / CopyFrom
//   for the derived type. Usage: struct app_record_foo_s : app_record_store_s <app_record_foo_s, Store::Foo> { ... };
template <class Derived, app_record_s::Store StoreType>
struct app_record_store_s : app_record_s
{
  app_record_store_s (uint32_t id_) : app_record_s (id_) { store = StoreType; }

  std::unique_ptr <app_record_s>
       Clone    (void) const              override { return std::make_unique <Derived> (static_cast <const Derived&> (*this)); }
  void CopyFrom (const app_record_s& src) override
  {
    assert (typeid (src) == typeid (Derived));
    static_cast <Derived&> (*this) = static_cast <const Derived&> (src);
  }
};

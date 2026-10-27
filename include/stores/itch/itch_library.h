#pragma once
#include <stores/Steam/app_record.h>
#include <string>
#include <vector>

/*
itch.io app

The itch app keeps its library in a SQLite database written by its butler daemon:

    %APPDATA%\itch\db\butler.db   (+ butler.db-wal, butler.db-shm)

The database runs in WAL mode and is rarely checkpointed: the main file can be a single empty page while every
  table lives in butler.db-wal, so it must be read through SQLite (which applies the WAL), never parsed directly.
  SKIF uses the copy of SQLite that ships with Windows 10 and newer (System32\winsqlite3.dll), loaded on demand;
  the itch app itself requires Windows 10, so the DLL is present wherever itch games can be installed.

Each install is a "cave":

    caves             (id TEXT, game_id, upload_id, build_id, install_location_id, install_folder_name,
                       custom_install_folder, verdict TEXT, ...)
    install_locations (id TEXT, path)
    games             (id, title, url, classification, cover_url, still_cover_url, ...)

  The game folder is custom_install_folder if set, else <install_locations.path>\<install_folder_name>.
  The cave's verdict is the app's own executable detection (redistributables and installers already filtered out):

    { "basePath": "R:\\Games\\itch Games\\celeste", "totalSize": 1168371053,
      "candidates": [ { "path": "Celeste.exe", "depth": 1, "flavor": "windows", "arch": "386",
                        "windowsInfo": { "dotNet": true, "arch": "386" } } ] }

Launching through the itch app, headless (no confirmation prompt; play time is recorded):

    "%LOCALAPPDATA%\itch\itch-setup.exe" --appname itch --run-game <game_id>

  (itch-setup's folder is read from HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\itch\InstallLocation when set.)

The canary app (kitch) can be installed alongside, and is read and launched the same way, with "kitch" in place of
  "itch": %APPDATA%\kitch\db\butler.db, %LOCALAPPDATA%\kitch\itch-setup.exe --appname kitch.

  Note: with the app's "Run games in a sandbox" option enabled, such launches run the game as a separate
  itch-player-* Windows user. Instant launches (running the executable directly) are not affected.
*/

// Library record for an itch.io game
struct app_record_itch_s : app_record_store_s <app_record_itch_s, app_record_s::Store::itch>
{
  using app_record_store_s::app_record_store_s;

  // itch-specific data
  struct itch_s {
    std::string  key            = ""; // Persistent key: the game ID, or <game ID>-<upload ID> for additional installs of the same game
    std::string  app            = "itch"; // The itch app it was installed with: "itch", or "kitch" (the canary app)
    int64_t      game_id        =  0;
    int64_t      upload_id      =  0;
    int64_t      build_id       =  0;
    std::string  cave_id        = ""; // Changes when the game is reinstalled
    std::string  classification = ""; // game, tool, ...
    std::string  url            = ""; // Store page
    std::string  cover_url      = ""; // Store cover (landscape, 315x250)
  } itch;

  std::string  GetPersistentKey        (void) const override { return itch.key; }

  // Launching (itch_library.cpp)
  bool         isInstantPlayCompatible (void) const override { return true; }
  bool         RequiresValidExecutable (void) const override { return false; } // Launches through the itch app
  bool         PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override;
  void         Launch                  (const launch_context_s& ctx) override;

  // Library UI (itch_library.cpp)
  bool         GetInstantPlayDefault   (void) const override;
  std::wstring GetStoreAssetDirectory  (void) const override;
  bool         CanCreateDesktopShortcut(void)       override { return true; }
  std::vector <std::pair <std::string, std::string>>
               GetStoreDebugFields     (void) const override;

  // Artwork (itch_library.cpp): the cover comes from IGDB (looked up by the itch game ID) and is saved as
  //   Assets\itch\<key>\cover-original.jpg; itch's own covers are landscape, so they are not used.
  //   The icon is extracted from the executable.
  std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged) override;
  std::wstring PrepareCoverSource      (bool queryPCGW) override;
};


void SKIF_itch_GetInstalledAppIDs        (app_list_t *apps); // Library worker (any thread)
bool SKIF_itch_hasInstalledGamesChanged  (void);             // Main thread
bool SKIF_itch_IsClientInstalled         (const std::string& app = "itch"); // The app's itch-setup.exe is present (used for headless launches)

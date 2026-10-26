#pragma once
#include <stores/Steam/app_record.h>
#include <string>
#include <vector>

/*
EA app

Installed games are found through their uninstall entries: every game installed by the EA app has one under
  HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ whose UninstallString runs the EAInstaller's cleanup tool:

    "C:\Program Files (x86)\Common Files\EAInstaller\<Slug>\Cleanup.exe" uninstall_game -autologging

The entry's InstallLocation holds the game folder, which contains __Installer\installerdata.xml (the "DiP manifest"):

    <DiPManifest version="4.0">
      <contentIDs><contentID>196173</contentID></contentIDs>                         -- Offer IDs, used to launch through the EA app
      <gameTitles><gameTitle locale="en_US">Sea of Solitude</gameTitle></gameTitles>
      <runtime>
        <launcher>
          <name locale="en_US">Sea of Solitude (English US)</name>
          <filePath>[HKEY_LOCAL_MACHINE\SOFTWARE\JoMei\SeaOfSolitude\Install Dir]SeaOfSolitude.exe</filePath>
          <parameters />
          <requires64BitOS>1</requires64BitOS>
          <trial>0</trial>
        </launcher>
      </runtime>
    </DiPManifest>

Launching through the EA app (the origin2 protocol is registered by the EA app):

    origin2://game/launch?offerIds=<contentID>[,<contentID>...]
*/

// Library record for an EA app game
struct app_record_ea_s : app_record_store_s <app_record_ea_s, app_record_s::Store::EA>
{
  using app_record_store_s::app_record_store_s;

  // EA-specific data
  struct ea_s {
    std::string  key           = ""; // Persistent key: the first content ID, or the uninstall subkey if the manifest has none
    std::string  offer_ids     = ""; // All content IDs, comma-separated (origin2 launch URI)
    std::string  uninstall_key = ""; // Uninstall subkey name (often a GUID)
    std::string  installer     = ""; // Folder name under Common Files\EAInstaller\ (e.g. Reckoning)
    std::string  manifest      = ""; // DiPManifest version ("" if installerdata.xml was not found)
  } ea;

  std::string  GetPersistentKey        (void) const override { return ea.key; }

  // Launching (ea_library.cpp)
  bool         isInstantPlayCompatible (void) const override { return true; }
  bool         RequiresValidExecutable (void) const override { return false; } // Launches through the EA app
  bool         PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override;
  void         Launch                  (const launch_context_s& ctx) override;

  // Library UI (ea_library.cpp)
  bool         GetInstantPlayDefault   (void) const override;
  std::wstring GetStoreAssetDirectory  (void) const override;
  bool         CanCreateDesktopShortcut(void)       override { return true; }
  std::vector <std::pair <std::string, std::string>>
               GetStoreDebugFields     (void) const override;

  // Artwork: no store cover yet (custom covers only); the icon is extracted from the executable
  bool         HasStoreCover           (void) const override { return false; }
};


void SKIF_EA_GetInstalledAppIDs        (app_list_t *apps); // Library worker (any thread)
bool SKIF_EA_hasInstalledGamesChanged  (void);             // Main thread
bool SKIF_EA_IsClientInstalled         (void);             // The EA app's origin2 protocol is registered

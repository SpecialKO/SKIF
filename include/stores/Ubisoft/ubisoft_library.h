#pragma once
#include <stores/Steam/app_record.h>
#include <string>
#include <vector>

/*
Ubisoft Connect

The client (32-bit) keeps its own folder and the installed games in the 32-bit registry view:

    HKLM\SOFTWARE\WOW6432Node\Ubisoft\Launcher                        InstallDir = D:\Games\Ubisoft Connect\
    HKLM\SOFTWARE\WOW6432Node\Ubisoft\Launcher\Installs\<product ID>  InstallDir = F:/Games/Ubisoft/Far Cry Primal/
                                                                      Language   = en-US

  Every install also has an uninstall entry, whose icon is the game's icon in the client's data folder:

    HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Uplay Install <product ID>
      DisplayName     = Far Cry Primal   (not always a proper title: "thesettlers2")
      DisplayIcon     = D:/Games/Ubisoft Connect/data/f9f40d84061123e2c39393a6d7f18949.ico
      UninstallString = "D:\Games\Ubisoft Connect\upc.exe" uplay://uninstall/2010

The product configurations (names, executables, artwork) are cached by the client in a protobuf file:

    <client>\cache\configuration\configurations

    repeated (field 1) { 1: varint product ID, 2: varint (unknown), 3: string configuration (YAML) }

  Only products that can be launched have a start_game section (most entries are DLC, packages, etc.):

    version: 2.0
    root:
      name: Far Cry Primal                   # or a localization key (l1), see localizations
      icon_image: f9f40d84061123e2c39393a6d7f18949.ico
      start_game:
        online:                              # offline: usually the same executables
          executables:
          - shortcut_name: Far Cry Primal
            path:
              relative: bin/FCPrimal.exe     # relative to the install folder, or
            # register: HKEY_CURRENT_USER\SOFTWARE\UBISOFT\Far Cry 3\ExecutableName
            working_directory:
              register: HKEY_LOCAL_MACHINE\SOFTWARE\Ubisoft\Launcher\Installs\2010\InstallDir
              append: ''
            arguments: -EXTRA1               # also play_arguments, arguments_register
    localizations:
      default:
        l1: Assassin's Creed® III

Launching through Ubisoft Connect (the uplay protocol is registered by the client):

    uplay://launch/<product ID>/0
*/

// Library record for a Ubisoft Connect game
struct app_record_ubisoft_s : app_record_store_s <app_record_ubisoft_s, app_record_s::Store::Ubisoft>
{
  using app_record_store_s::app_record_store_s;

  // Ubisoft-specific data
  struct ubisoft_s {
    std::string  key           = ""; // Persistent key: the product ID
    uint32_t     product_id    =  0;
    std::string  display_name  = ""; // Uninstall entry's DisplayName (also tried for the cover)
    std::string  language      = ""; // Install language (en-US)
    std::wstring icon          = L""; // The client's .ico for the game (<client>\data\<hash>.ico)
    bool         configuration = false; // Launch configs come from the client's configurations cache
  } ubisoft;

  // Library refresh (app_record.h)
  bool         HasSameLibraryData      (const app_record_s& other) const override
  {
    if (! app_record_s::HasSameLibraryData (other))
      return false;

    const ubisoft_s& o = static_cast <const app_record_ubisoft_s&> (other).ubisoft;

    return (ubisoft.key           == o.key           &&
            ubisoft.product_id    == o.product_id    &&
            ubisoft.display_name  == o.display_name  &&
            ubisoft.language      == o.language      &&
            ubisoft.icon          == o.icon          &&
            ubisoft.configuration == o.configuration);
  }

  std::string  GetPersistentKey        (void) const override { return ubisoft.key; }

  // Launching (ubisoft_library.cpp)
  bool         isInstantPlayCompatible (void) const override { return true; }
  bool         RequiresValidExecutable (void) const override { return false; } // Launches through Ubisoft Connect
  bool         PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override;
  void         Launch                  (const launch_context_s& ctx) override;

  // Library UI (ubisoft_library.cpp)
  bool         GetInstantPlayDefault   (void) const override;
  std::wstring GetStoreAssetDirectory  (void) const override;
  bool         CanCreateDesktopShortcut(void)       override { return true; }
  std::vector <std::pair <std::string, std::string>>
               GetStoreDebugFields     (void) const override;

  // Artwork (ubisoft_library.cpp): IGDB has no Ubisoft source and the client's own thumbnails are small (240x280),
  //   so the cover is looked up on IGDB by name and saved as Assets\Ubisoft\<key>\cover-original.jpg.
  //   The icon is extracted from the executable, else the client's .ico is used.
  std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged) override;
  std::wstring PrepareCoverSource      (bool queryPCGW) override;
};


void SKIF_Ubisoft_GetInstalledAppIDs       (app_list_t *apps); // Library worker (any thread)
bool SKIF_Ubisoft_hasInstalledGamesChanged (void);             // Main thread
bool SKIF_Ubisoft_IsClientInstalled        (void);             // upc.exe is present and the uplay protocol is registered

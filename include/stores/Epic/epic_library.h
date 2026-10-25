#pragma once
#include <utility/fsutil.h>
#include <vector>
#include <wtypes.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>
#include <utility/utility.h>
#include <stores/generic_library2.h>
#include <stores/Steam/app_record.h>


// Library record for an Epic Games Store game
struct app_record_epic_s : app_record_store_s <app_record_epic_s, app_record_s::Store::Epic>
{
  using app_record_store_s::app_record_store_s;

  // Epic-specific data
  struct epic_s {
    std::string  catalog_namespace =  ""; // CatalogNamespace
    std::string  catalog_item_id   =  ""; // CatalogItemId
    std::string  name_app          =  ""; // AppName (v1) / ArtifactId (v2)
    std::string  name_display      =  ""; // DisplayName; Used for asset retrieval
    std::string  gamebox_tall      =  "";
  } epic;

  std::string GetPersistentKey        (void) const override { return epic.name_app; }

  // Launching (epic_library.cpp)
  bool        RequiresValidExecutable (void) const override { return false; } // Launches through the Epic Games Launcher
  void        Launch                  (const launch_context_s& ctx) override;

  // Library UI (epic_library.cpp)
  std::wstring GetStoreAssetDirectory  (void) const override;
  std::vector <std::pair <std::string, std::string>>
               GetStoreDebugFields     (void) const override;

  // Library population: Special K has no Epic manifest parser, so it maps the install folder to the AppName
  std::string  GetAppIdMapping            (void) const override { return epic.name_app; }

  // Artwork (epic_library.cpp)
  std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged) override;
  std::wstring PrepareCoverSource      (bool queryPCGW) override;
};


struct SKIF_EpicCollection {

  bool isSignaled         (void);
  void GetInstalledAppIDs (app_list_t *apps) const;
  void IdentifyAssetNew   (const app_record_epic_s::epic_s& epic);

  static SKIF_EpicCollection& GetInstance (void)
  {
    static SKIF_EpicCollection instance;
    return instance;
  }

private:
  SKIF_EpicCollection (void) { };
  SKIF_EpicCollection (SKIF_EpicCollection const&) = delete;
  SKIF_EpicCollection (SKIF_EpicCollection&&)      = delete;

  bool RefreshInstallState (void); // Main thread; true if Epic was found now after not having been found before
  bool FindInstalledGames2 (app_list_t *apps) const;
  bool FindInstalledGames1 (app_list_t *apps) const;

  SKIF_DirectoryWatch watch;
  std::wstring  appdatapath;
  int               version = -1; // -1 unchecked, 0 uninstalled, 1 v1, 2 v2
  DWORD             lastCheck = 0;  // SKIF_Util_timeGetTime ( ) of the last detection while not installed
};

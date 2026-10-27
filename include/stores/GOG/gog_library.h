#pragma once
#include <stores/Steam/app_record.h>
#include <utility/fsutil.h>
#include <vector>

void SKIF_GOG_GetInstalledAppIDs       (app_list_t *apps);
void SKIF_GOG_UpdateGalaxyUserID       (void);
bool SKIF_GOG_hasInstalledGamesChanged (void);
bool SKIF_GOG_hasGalaxySettingsChanged (void);
void SKIF_GOG_IdentifyAssetPCGW        (uint32_t app_id);

// Library record for a GOG game
struct app_record_gog_s : app_record_store_s <app_record_gog_s, app_record_s::Store::GOG>
{
  using app_record_store_s::app_record_store_s;

  bool        isInstantPlayCompatible (void) const override { return true; }
  std::string GetPCGamingWikiURL      (void) const override { return "https://www.pcgamingwiki.com/api/gog.php?page=" + std::to_string (id); }

  // Launching (gog_library.cpp)
  bool        PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override;
  void        Launch                  (const launch_context_s& ctx) override;

  // Library UI (gog_library.cpp)
  bool         GetInstantPlayDefault   (void) const override;
  std::wstring GetStoreAssetDirectory  (void) const override;
  bool         CanCreateDesktopShortcut(void)       override { return true; }

  // Context menu hooks (gog_library.cpp)
  void         DrawWebsiteMenuItems           (void) override;

  // Artwork (gog_library.cpp)
  std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged) override;
  std::wstring PrepareCoverSource      (bool queryPCGW) override;
  bool         WantsPCGWCover          (void) const override;
  bool         WantsIGDBCover          (void) const override;
  bool         DownloadIGDBCover       (const std::wstring& targetPath) override;
  std::wstring GetIconSource           (void) const override { return install_dir + L"\\goggame-" + std::to_wstring(id) + L".ico"; }
  bool         DeleteStoreCoverFiles   (const std::wstring& assetDir) override;
  bool         CoverNeedsClientSignIn  (void) const override;
};

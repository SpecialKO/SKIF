#pragma once
#include <stores/generic_library2.h>
#include <stores/Steam/app_record.h>

bool                  SKIF_RemoveCustomAppID (uint32_t appid);
int                   SKIF_AddCustomAppID    (std::wstring  name, std::wstring      exePath, std::wstring      args, std::wstring workingPath);
bool                  SKIF_ModifyCustomAppID (app_record_s* pApp, std::wstring_view exePath, std::wstring_view args, std::wstring workingPath);
void                  SKIF_GetCustomAppIDs   (app_list_t *apps);
bool                  SKIF_Custom_AddGame    (void);
bool                  SKIF_Custom_RemoveGame (uint32_t appid);

// Library record for a custom game added to SKIF
struct app_record_custom_s : app_record_store_s <app_record_custom_s, app_record_s::Store::Custom>
{
  using app_record_store_s::app_record_store_s;

  // Custom games always use an instant launch
  bool        PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override { UNREFERENCED_PARAMETER (fromGameMenu); UNREFERENCED_PARAMETER (config); return true; }

  // Library UI (custom_library.cpp)
  std::wstring GetStoreAssetDirectory  (void) const override;
  bool         IsUserAdded             (void) const override { return true; }
  bool         HasStoreCover           (void) const override { return false; } // Only custom covers
  bool         CanCreateDesktopShortcut(void)       override { return true; }
};

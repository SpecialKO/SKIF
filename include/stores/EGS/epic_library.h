#pragma once
#include <fsutil.h>
#include <vector>
#include <wtypes.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>
#include <SKIF_utility.h>
#include <stores/generic_library2.h>
#include <stores/Steam/app_record.h>

void         SKIF_EGS_GetInstalledAppIDs   (std::vector <std::pair < std::string, app_record_s > > *apps);
void         SKIF_EGS_IdentifyAssetNew     (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName);

// Epic entries
struct app_epic_s : app_generic_s {
  std::string  Epic_CatalogNamespace = "";
  std::string  Epic_CatalogItemId = "";
  std::string  Epic_AppName = "";
  std::string  Epic_DisplayName = "";
  
  void                      launchGame (void) override;
  ID3D11ShaderResourceView* getCover   (void) override;
  ID3D11ShaderResourceView* getIcon    (void) override;
};
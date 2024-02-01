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

void      SKIF_Epic_GetInstalledAppIDs   (std::vector <std::pair < std::string, app_record_s > > *apps);
void      SKIF_Epic_IdentifyAssetNew     (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName);

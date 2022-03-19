#pragma once
#include <stores/Steam/app_record.h>
#include <fsutil.h>
#include <vector>
#include <wtypes.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <json.hpp>
#include <SKIF_utility.h>

void         SKIF_EGS_GetInstalledAppIDs   (std::vector <std::pair < std::string, app_record_s > > *apps);
void         SKIF_EGS_IdentifyAssetNew     (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName);
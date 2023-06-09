#pragma once
#include <stores/Steam/app_record.h>
#include <utility/fsutil.h>
#include <vector>

void        SKIF_Xbox_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps);
void        SKIF_Xbox_IdentifyAssetNew   (std::string PackageName, std::string StoreID);
bool        SKIF_Xbox_hasInstalledGamesChanged (void);
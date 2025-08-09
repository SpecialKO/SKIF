#pragma once
#include <stores/Steam/app_record.h>
#include <utility/fsutil.h>
#include <vector>

void SKIF_GOG_GetInstalledAppIDs       (std::vector <std::pair < std::string, app_record_s > > *apps);
void SKIF_GOG_UpdateGalaxyUserID       (void);
bool SKIF_GOG_hasInstalledGamesChanged (void);
bool SKIF_GOG_hasGalaxySettingsChanged (void);
void SKIF_GOG_IdentifyAssetPCGW        (uint32_t app_id);
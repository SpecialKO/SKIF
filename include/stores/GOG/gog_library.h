#pragma once
#include <stores/Steam/app_record.h>
#include <fsutil.h>
#include <vector>

void SKIF_GOG_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps);
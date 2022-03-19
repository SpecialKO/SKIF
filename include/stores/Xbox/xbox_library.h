#pragma once
#include <stores/Steam/app_record.h>
#include <fsutil.h>
#include <vector>

extern void  SKIF_Util_GetWebResource (std::wstring url, std::wstring_view destination, std::wstring method, std::wstring header, std::string body);

void        SKIF_Xbox_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps);
void        SKIF_Xbox_IdentifyAssetNew   (std::string PackageName, std::string StoreID);
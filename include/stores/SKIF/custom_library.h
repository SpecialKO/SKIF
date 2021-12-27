#pragma once
#include <stores/Steam/app_record.h>
#include <vector>


bool                  SKIF_RemoveCustomAppID (uint32_t appid);
int                   SKIF_AddCustomAppID    (std::vector<std::pair<std::string, app_record_s>>* apps,
                                                std::wstring name, std::wstring path, std::wstring args);
bool                  SKIF_ModifyCustomAppID (app_record_s* pApp,
                                                std::wstring name, std::wstring path, std::wstring args);
void									SKIF_GetCustomAppIDs   (std::vector <std::pair < std::string, app_record_s > > *apps);
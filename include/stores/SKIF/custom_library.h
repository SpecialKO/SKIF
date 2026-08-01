#pragma once
#include <stores/generic_library2.h>
#include <stores/Steam/app_record.h>

bool                  SKIF_RemoveCustomAppID (uint32_t appid);
int                   SKIF_AddCustomAppID    (std::wstring  name, std::wstring      exePath, std::wstring      args, std::wstring workingPath);
bool                  SKIF_ModifyCustomAppID (app_record_s* pApp, std::wstring_view exePath, std::wstring_view args, std::wstring workingPath);
void                  SKIF_GetCustomAppIDs   (std::vector <std::pair < std::string, app_record_s > > *apps);

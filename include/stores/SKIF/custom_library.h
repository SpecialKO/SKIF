#pragma once
#include <stores/generic_library2.h>
#include <stores/Steam/app_record.h>

bool                  SKIF_RemoveCustomAppID (uint32_t appid);
int                   SKIF_AddCustomAppID    (std::vector<std::pair<std::string, app_record_s>>* apps,
                                                std::wstring name, std::wstring path, std::wstring args);
bool                  SKIF_ModifyCustomAppID (app_record_s* pApp,
                                                std::wstring name, std::wstring path, std::wstring args);
void                  SKIF_GetCustomAppIDs   (std::vector <std::pair < std::string, app_record_s > > *apps);

// SKIF entries
struct app_skif_s : app_generic_s {
  
  void                      launchGame (void) override;
  ID3D11ShaderResourceView* getCover   (void) override;
  ID3D11ShaderResourceView* getIcon    (void) override;
};
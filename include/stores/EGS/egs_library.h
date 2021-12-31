#pragma once
#include <stores/Steam/app_record.h>
#include <fsutil.h>
#include <vector>
#include <wtypes.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <json.hpp>

static nlohmann::json SKIF_EGS_JSON_CatalogNamespaces = NULL;

void         SKIF_EGS_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps);
void         SKIF_EGS_GetCatalogNamespaces (bool forceUpdate = false); // Populates SKIF_EGS_JSON_CatalogNamespaces

struct skif_get_asset_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH] = { };
  wchar_t wszLocalPath[MAX_PATH + 2] = { };
};

DWORD WINAPI SKIF_EGS_FetchAsset  (skif_get_asset_t* get);
void         SKIF_EGS_GetWebAsset (std::wstring url, std::wstring_view   path);
void         SKIF_EGS_IdentifyAsset (std::string CatalogNamespace, std::string CatalogItemId, std::string AppName, std::string DisplayName);

static std::wstring SKIF_EGS_AppDataPath;

struct skif_egs_directory_watch_s
{
  ~skif_egs_directory_watch_s (void);

  bool isSignaled (void);

  HANDLE hChangeNotification = INVALID_HANDLE_VALUE;
} extern skif_egs_dir_watch;
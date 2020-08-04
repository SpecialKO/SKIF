#pragma once

#include <wtypes.h>
#include <WinInet.h>

#include <cstdint>
#include <string>

struct skif_box_art_get_t {
  wchar_t wszHostName  [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath  [INTERNET_MAX_PATH_LENGTH]      = { };
  wchar_t wszLocalPath [MAX_PATH + 2]                  = { };
};

DWORD WINAPI SKIF_Steam_FetchBoxArt ( skif_box_art_get_t* get );
void         SKIF_HTTP_GetAppLibImg ( uint32_t            app_id,
                                      std::wstring_view   path );

bool SKIF_LibraryAssets_CheckForUpdates (bool reset = false);
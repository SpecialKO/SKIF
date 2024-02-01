#pragma once
#include <cstdint>
#include <string>
#include <wtypes.h>
#include <set>
#include <map>
#include <combaseapi.h>
#include <atlbase.h>
#include <d3d11.h>
#include <vector>
#include <atomic>

#include "Steam/app_record.h"
#include <imgui/imgui.h>

enum class LibraryTexture
{
  Icon,
  Cover,
  Patreon,
  Logo
};

void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             vCoverUv0,
        ImVec2&                             vCoverUv1,
        app_record_s*                       pApp = nullptr);

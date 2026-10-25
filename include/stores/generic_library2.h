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

#include "DirectXTex/DirectXTex.h"

enum class LibraryTexture
{
  Icon,
  Cover,
  Patreon,
  Logo
};

enum ImageDecoder {
  ImageDecoder_WIC,
  ImageDecoder_stbi
};

bool
FastTextureLoading (const std::wstring& path, DirectX::TexMetadata& meta, DirectX::ScratchImage& img);

// A loaded library texture; the caller decides where it goes (see LoadLibraryTextureEx)
struct library_texture_s {
  CComPtr <ID3D11ShaderResourceView> srv;
  ImVec2                             resolution = ImVec2 (0, 0);
  bool                               isCustom   = false; // The user's custom artwork (games only)
  bool                               isManaged  = false; // A file SKIF manages, so the menus can refresh / reset it (games only)
};

// Loads a cover / icon (pApp: from ResolveArtwork) or an embedded Special K texture into 'out'. Returns true if out.srv is set.
//   Safe to call from several threads at once for different games: it only reads settings, creates the texture on the
//   (free-threaded) D3D11 device and may write files in the game's asset folder (ResolveArtwork). It writes nothing to
//   pApp or to any texture in use, so results are applied on the main thread (SKIF_LibraryArtwork::ApplyWorkerResults).
bool
LoadLibraryTextureEx (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        const std::wstring&                 name,
        app_record_s*                       pApp,
        library_texture_s&                  out);

// LoadLibraryTextureEx, applied directly: replaces pLibTexSRV (the old one is released after the frame) and pApp's
//   custom / managed flags. Only for textures nothing else is using at the time (the library worker's Patreon / logo).
void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             resolution,
        app_record_s*                       pApp = nullptr);

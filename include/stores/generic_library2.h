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
#include <functional>

#include "Steam/app_record.h"
#include <imgui/imgui.h>

#include "DirectXTex/DirectXTex.h"

enum class LibraryTexture
{
  Icon,
  Cover,
  Patreon,
  Logo,
  Thumbnail  // A cover scaled down for the grid view (loaded as a Cover, see library_thumbnail_s)
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

// A cover loaded as a thumbnail (the grid view): scaled to fill width x height, and saved to cachePath (if set)
//   before it is turned into a texture, so the next time it loads from there (LoadLibraryThumbnailFromCache)
struct library_thumbnail_s {
  float                              width      = 0.0f;
  float                              height     = 0.0f;
  std::wstring                       cachePath;
  std::function <bool (void)>        wanted;    // If set: whether the texture is still needed (its tile in view); asked before the
                                                //   texture is created, so one no longer needed is never created (the cache is still saved)
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
        library_texture_s&                  out,
        const library_thumbnail_s*          thumb = nullptr); // Covers only: load as a thumbnail

// A thumbnail saved by LoadLibraryTextureEx (thumb->cachePath). Thread-safe like LoadLibraryTextureEx.
bool
LoadLibraryThumbnailFromCache (const std::wstring& path, library_texture_s& out, const library_thumbnail_s* thumb = nullptr); // thumb: its wanted ( ) only

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

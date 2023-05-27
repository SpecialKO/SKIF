#include "generic_library2.h"

#include "DirectXTex.h"
#include <SKIF_imgui.h>

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

bool app_generic_s::loadCoverFromFile (std::wstring path)
{
  PLOG_DEBUG << "Texture to load: " << path;
  bool succeeded = false;
  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  if (path != L"\0" &&
      SUCCEEDED(
        DirectX::LoadFromWICFile (
          path.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // WIC_FLAGS_IGNORE_SRGB solves some PNGs appearing too dark
              &meta, img
        )
      )
    )
  {
    succeeded = true;
  }

  if (succeeded)
  {
    //if (libTexToLoad == LibraryTexture::Cover)
    //  OutputDebugString((L"[App#" + std::to_wstring(appid) + L"] Loading the source image succeeded...\n").c_str());

    DirectX::ScratchImage* pImg   =
                                &img;
    DirectX::ScratchImage   converted_img;

    // Start aspect ratio
    ImVec2 vCoverUv0 = ImVec2(0.f, 0.f); // Top left corner
    ImVec2 vCoverUv1 = ImVec2(1.f, 1.f); // Bottom right corner
    ImVec2 vecTex2D = ImVec2(600.f, 900.f);

    vecTex2D.x = static_cast<float>(meta.width);
    vecTex2D.y = static_cast<float>(meta.height);

    ImVec2 diff = ImVec2(0.0f, 0.0f);

    // Crop wider aspect ratios by their width
    if ((vecTex2D.x / vecTex2D.y) > (600.f / 900.f))
    {
      float newWidth = vecTex2D.x / vecTex2D.y * 900.0f;
      diff.x = (600.0f / newWidth);
      diff.x -= 1.0f;
      diff.x /= 2;

      vCoverUv0.x = 0.f - diff.x;
      vCoverUv1.x = 1.f + diff.x;
    }

    // Crop thinner aspect ratios by their height
    else if ((vecTex2D.x / vecTex2D.y) < (600.f / 900.f))
    {
      float newHeight = vecTex2D.y / vecTex2D.x * 600.0f;
      diff.y = (900.0f / newHeight);
      diff.y -= 1.0f;
      diff.y /= 2;
      
      vCoverUv0.y = 0.f - diff.y;
      vCoverUv1.y = 1.f + diff.y;
    }

    // End aspect ratio

    // We don't want single-channel icons, so convert to RGBA
    if (meta.format == DXGI_FORMAT_R8_UNORM)
    {
      if (
        SUCCEEDED (
          DirectX::Convert (
            pImg->GetImages   (), pImg->GetImageCount (),
            pImg->GetMetadata (), DXGI_FORMAT_R8G8B8A8_UNORM,
              DirectX::TEX_FILTER_DEFAULT,
              DirectX::TEX_THRESHOLD_DEFAULT,
                converted_img
          )
        )
      ) { meta =  converted_img.GetMetadata ();
          pImg = &converted_img; }
    }

    auto pDevice =
      SKIF_D3D11_GetDevice ();

    if (! pDevice)
      return false;

    if (
      SUCCEEDED (
        DirectX::CreateTexture (
          pDevice,
            pImg->GetImages (), pImg->GetImageCount (),
              meta, (ID3D11Resource **)&pTex2D.p
        )
      )
    )
    {
      D3D11_SHADER_RESOURCE_VIEW_DESC
        srv_desc                           = { };
        srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels       = UINT_MAX;
        srv_desc.Texture2D.MostDetailedMip =  0;

      if (    pTex2D.p == nullptr ||
        FAILED (
          pDevice->CreateShaderResourceView (
              pTex2D.p, &srv_desc,
            &this->textures.cover
          )
        )
      )
      {
        //pLibTexSRV = pOrigTexSRV;
      }

      // SRV is holding a reference, this is not needed anymore.
      pTex2D = nullptr;
    }
  }

  return false;
}

bool app_generic_s::loadIconFromFile (std::wstring file)
{
  return false;
}

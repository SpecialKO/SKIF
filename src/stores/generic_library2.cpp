#include <stores/generic_library2.h>

#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/fsutil.h>
#include <filesystem>

#include <images/patreon.png.h>
#include <images/sk_icon.jpg.h>
#include <images/sk_boxart.png.h>
//#include <images/sk_boxart.jpg.h>
//#include <images/sk_boxart2.jpg.h>

#include <concurrent_queue.h>
#include "stores/Steam/steam_library.h"
#include <utility/registry.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
//#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
//#define STBI_ONLY_GIF
//#define STBI_ONLY_HDR
//#define STBI_ONLY_PIC
//#define STBI_ONLY_PNM

#include <stb_image.h>

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

bool
FastTextureLoading (const std::wstring& path, DirectX::TexMetadata& meta, DirectX::ScratchImage& img)
{
  bool success = false;

  const std::filesystem::path imagePath (path.data());
  std::wstring   ext = SKIF_Util_ToLowerW(imagePath.extension().wstring());
  std::string szPath = SK_WideCharToUTF8(path);

  ImageDecoder decoder = ImageDecoder_stbi; // Always try to use stbi first

  if (decoder == ImageDecoder_stbi)
  {
    PLOG_VERBOSE << "Using stbi decoder...";

    // If desired_channels is non-zero, *channels_in_file has the number of components that _would_ have been
    // output otherwise. E.g. if you set desired_channels to 4, you will always get RGBA output, but you can
    // check *channels_in_file to see if it's trivially opaque because e.g. there were only 3 channels in the source image.

    int width            = 0,
        height           = 0,
        channels_in_file = 0,
        desired_channels = STBI_rgb_alpha;

    unsigned char *pixels = stbi_load (szPath.c_str(), &width, &height, &channels_in_file, desired_channels);

    if (pixels != NULL)
    {
      meta.width     = width;
      meta.height    = height;
      meta.depth     = 1;
      meta.arraySize = 1;
      meta.mipLevels = 1;
      meta.format    = DXGI_FORMAT_R8G8B8A8_UNORM; // STBI_rgb_alpha
      meta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

      if (SUCCEEDED (img.Initialize2D (meta.format, width, height, 1, 1)))
      {
        size_t   imageSize = static_cast<size_t> (width * height * desired_channels * sizeof(uint8_t));
        uint8_t* pDest     = img.GetImage(0, 0, 0)->pixels;
        memcpy(pDest, pixels, imageSize);

        success = true;
      }
    }

    stbi_image_free (pixels);
  }

  // Also try WIC if stbi fails
  if (! success)
    decoder = ImageDecoder_WIC;

  if (decoder == ImageDecoder_WIC)
  {
    PLOG_VERBOSE << "Using WIC decoder...";

    if (SUCCEEDED (
        DirectX::LoadFromWICFile (
          path.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // WIC_FLAGS_IGNORE_SRGB solves some PNGs appearing too dark
              &meta, img)))
    {
      success = true;
    }
  }

  return success;
}

bool
LoadLibraryTextureEx (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        const std::wstring&                 name,
        app_record_s*                       pApp,
        library_texture_s&                  out)
{
  // Settings and paths are only read here; see the threading notes in generic_library2.h
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  std::wstring load_str = L"\0";

  bool succeeded    = false;
  bool customAsset  = false;
  bool managedAsset = true; // Assume true (only GOG and SKIF itself is not managed)

  DWORD pre = SKIF_Util_timeGetTime1();

  // Games (not embedded Special K resources)
  if (pApp != nullptr)
  {
    appid = pApp->id;

    // Store-specific: the user's custom artwork, else the store's (Special K: custom only, else embedded below)
    load_str = pApp->ResolveArtwork (libTexToLoad, name, customAsset, managedAsset);
  }

  PLOG_VERBOSE_IF (load_str != L"\0") << "Texture to load: " << load_str;

  if (load_str != L"\0")
  {
    succeeded = FastTextureLoading (load_str, meta, img);

    // A cover SKIF downloaded that does not decode (e.g. an error page saved as the image) would otherwise stay
    //   forever, since an existing file is never downloaded again: delete it so the next load fetches it anew
    if (! succeeded && pApp != nullptr && libTexToLoad == LibraryTexture::Cover && managedAsset && ! customAsset)
    {
      const std::filesystem::path fsPath (load_str);
      std::wstring fileName = SKIF_Util_ToLowerW (fsPath.filename ( ).wstring ( ));

      if ((fileName.rfind (L"cover-original.", 0) == 0 || fileName == L"cover-pcgw.png") &&
          SKIF_Util_ToLowerW (fsPath.parent_path ( ).wstring ( ) + L"\\") == SKIF_Util_ToLowerW (pApp->GetAssetDirectory ( )))
      {
        PLOG_WARNING << "Deleting a downloaded cover that could not be decoded: " << load_str;
        DeleteFileW (load_str.c_str ( ));
      }
    }
  }

  else if (appid        == SKIF_STEAM_APPID     &&
           libTexToLoad != LibraryTexture::Cover ) // We have no embedded cover any longer
  {
    PLOG_VERBOSE << "Texture to load (embedded): " << name;

    if (SUCCEEDED(
          DirectX::LoadFromWICMemory(
            (libTexToLoad == LibraryTexture::Icon) ?        sk_icon_jpg  : (libTexToLoad == LibraryTexture::Logo) ?        sk_boxart_png  :        patreon_png,
            (libTexToLoad == LibraryTexture::Icon) ? sizeof(sk_icon_jpg) : (libTexToLoad == LibraryTexture::Logo) ? sizeof(sk_boxart_png) : sizeof(patreon_png),
              DirectX::WIC_FLAGS_FILTER_POINT,
                &meta, img
          )
        )
      )
    {
      succeeded = true;
    }
  }

  if (! succeeded)
    return false;

  DirectX::ScratchImage* pImg  =   &img;
  DirectX::ScratchImage   converted_img;

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
    )
    {
      meta =  converted_img.GetMetadata ();
      pImg = &converted_img;
    }
  }

  // Downscale covers to 220x330, which will then be shown in horizon mode
  if ((_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass && libTexToLoad == LibraryTexture::Cover) ||
      (libTexToLoad == LibraryTexture::Logo  && name == L"sk_boxart_small.png"))
  {
    float width  = 220.0f;
    float height = 330.0f;
    float imageAspectRatio   = static_cast<float> (meta.width) / static_cast<float> (meta.height);
    float defaultAspectRatio = static_cast<float> (width) / static_cast<float> (height);

    if (imageAspectRatio > defaultAspectRatio)
      width = height * imageAspectRatio;
    else
      height = width / imageAspectRatio;

    if (
      SUCCEEDED (
        DirectX::Resize (
          pImg->GetImages   (), pImg->GetImageCount (),
          pImg->GetMetadata (), static_cast<size_t> (width), static_cast<size_t> (height),
          DirectX::TEX_FILTER_FANT,
              converted_img
        )
      )
    )
    {
      meta =  converted_img.GetMetadata ();
      pImg = &converted_img;
    }
  }

  // Store the resolution of the loaded image
  out.resolution.x = static_cast<float> (meta.width);
  out.resolution.y = static_cast<float> (meta.height);

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return false;

  pTex2D = nullptr;

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

      //CComPtr <ID3D11ShaderResourceView>
      //    pOrigTexSRV (pLibTexSRV.p);
                        //pLibTexSRV = nullptr; // Crashes on Intel

      //if (libTexToLoad == LibraryTexture::Cover)
      //  OutputDebugString((L"[App#" + std::to_wstring(appid) + L"] Game cover loaded\n").c_str());

    if (    pTex2D.p == nullptr ||
      FAILED (
        pDevice->CreateShaderResourceView (
            pTex2D.p, &srv_desc,
          &out.srv.p
        )
      )
    )
    {
      //pLibTexSRV = pOrigTexSRV;
    }

    // If everything went well
    else {
      DWORD post = SKIF_Util_timeGetTime1 ( );
      PLOG_DEBUG << "[Image Processing] Processed image in " << (post - pre) << " ms.";

      out.isCustom  = customAsset;
      out.isManaged = managedAsset;
    }

    // SRV is holding a reference, this is not needed anymore.
    pTex2D = nullptr;
  }

  return (out.srv.p != nullptr);
}

void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             resolution,
        app_record_s*                       pApp)
{
  library_texture_s tex;

  bool succeeded =
    LoadLibraryTextureEx (libTexToLoad, appid, name, pApp, tex);

  // Push the existing texture to a stack to be released after the frame
  //   Do this regardless of whether we could actually load the new texture or not
  if (pLibTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pLibTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push (pLibTexSRV.p);
    pLibTexSRV.p = nullptr;
  }

  resolution = tex.resolution;
  pLibTexSRV = tex.srv;

  if (pApp != nullptr)
  {
    if      (libTexToLoad == LibraryTexture::Cover)
    {
      pApp->tex_cover.isCustom  = (succeeded && tex.isCustom);
      pApp->tex_cover.isManaged = (succeeded && tex.isManaged);
    }
    else if (libTexToLoad == LibraryTexture::Icon)
    {
      pApp->tex_icon.isCustom   = (succeeded && tex.isCustom);
      pApp->tex_icon.isManaged  = (succeeded && tex.isManaged);
    }
  }
}
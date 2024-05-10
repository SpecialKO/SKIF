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
    PLOG_DEBUG << "Using stbi decoder...";

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
    PLOG_DEBUG << "Using WIC decoder...";

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

void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             resolution,
      //ImVec2&                             vCoverUv0,
      //ImVec2&                             vCoverUv1,
        app_record_s*                       pApp)
{
  // NOT REALLY THREAD-SAFE WHILE IT RELIES ON THESE STATIC GLOBAL OBJECTS!
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  static const int SKIF_STEAM_APPID = 1157970;

  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  std::wstring load_str = L"\0",
               SKIFCustomPath,
               SteamCustomPath;

  bool succeeded    = false;
  bool customAsset  = false;
  bool managedAsset = true; // Assume true (only GOG and SKIF itself is not managed)

  DWORD pre = SKIF_Util_timeGetTime1();

  // Games (not embedded Special K resources)
  if (pApp != nullptr)
  {
    appid = pApp->id;
  
    if (libTexToLoad == LibraryTexture::Cover)
      pApp->tex_cover.isCustom = pApp->tex_cover.isManaged = false;
  
    if (libTexToLoad == LibraryTexture::Icon)
      pApp->tex_icon.isCustom  = pApp->tex_icon.isManaged  = false;

    // SKIF
    if (       appid == SKIF_STEAM_APPID           &&
         pApp->store == app_record_s::Store::Steam  )
    {
      SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\)", _path_cache.specialk_userdata);

      if (libTexToLoad == LibraryTexture::Cover)
        SKIFCustomPath += L"cover";
      else
        SKIFCustomPath += L"icon";

      if      (PathFileExistsW ((SKIFCustomPath + L".png").c_str()))
        load_str =               SKIFCustomPath + L".png";
      else if (PathFileExistsW ((SKIFCustomPath + L".jpg").c_str()))
        load_str =               SKIFCustomPath + L".jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               PathFileExistsW ((SKIFCustomPath + L".ico").c_str()))
        load_str =               SKIFCustomPath + L".ico";

      customAsset = managedAsset = (load_str != L"\0");
    }

    // SKIF Custom
    else if (pApp->store == app_record_s::Store::Custom)
    {
      SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, appid);

      if (libTexToLoad == LibraryTexture::Cover)
        SKIFCustomPath += L"cover";
      else
        SKIFCustomPath += L"icon";

      if      (PathFileExistsW    ((SKIFCustomPath + L".png").c_str()))
        load_str =                  SKIFCustomPath + L".png";
      else if (PathFileExistsW    ((SKIFCustomPath + L".jpg").c_str()))
        load_str =                  SKIFCustomPath + L".jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               PathFileExistsW    ((SKIFCustomPath + L".ico").c_str()))
        load_str =                  SKIFCustomPath + L".ico";

      customAsset = (load_str != L"\0");

      if (! customAsset)
      {
        if      (libTexToLoad == LibraryTexture::Icon &&
                 SKIF_Util_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath ( ), SKIFCustomPath + L"-original.png"))
          load_str =                SKIFCustomPath + L"-original.png";
      }
    }

    // Epic
    else if (pApp->store == app_record_s::Store::Epic)
    {
      std::wstring EpicAssetPath = SK_FormatStringW(LR"(%ws\Assets\Epic\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->epic.name_app).c_str());
      SKIFCustomPath = std::wstring(EpicAssetPath);

      if (libTexToLoad == LibraryTexture::Cover)
        SKIFCustomPath += L"cover";
      else
        SKIFCustomPath += L"icon";

      if      (PathFileExistsW  ((SKIFCustomPath + L".png").c_str()))
        load_str =                SKIFCustomPath + L".png";
      else if (PathFileExistsW  ((SKIFCustomPath + L".jpg").c_str()))
        load_str =                SKIFCustomPath + L".jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               PathFileExistsW  ((SKIFCustomPath + L".ico").c_str()))
        load_str =                SKIFCustomPath + L".ico";

      customAsset = (load_str != L"\0");

      if (! customAsset)
      {
        if      (libTexToLoad == LibraryTexture::Cover &&
                 PathFileExistsW ((EpicAssetPath + L"cover-original.jpg").c_str()))
          load_str =               EpicAssetPath + L"cover-original.jpg";
        else if (libTexToLoad == LibraryTexture::Icon &&
                 SKIF_Util_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath ( ), EpicAssetPath + L"icon-original.png"))
          load_str =               SKIFCustomPath + L"-original.png";
      }
    }

    // GOG
    else if (pApp->store == app_record_s::Store::GOG)
    {
      SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)", _path_cache.specialk_userdata, appid);

      if (libTexToLoad == LibraryTexture::Cover)
        SKIFCustomPath += L"cover";
      else
        SKIFCustomPath += L"icon";

      if      (PathFileExistsW ((SKIFCustomPath + L".png").c_str()))
        load_str =               SKIFCustomPath + L".png";
      else if (PathFileExistsW ((SKIFCustomPath + L".jpg").c_str()))
        load_str =               SKIFCustomPath + L".jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               PathFileExistsW ((SKIFCustomPath + L".ico").c_str()))
        load_str =               SKIFCustomPath + L".ico";

      customAsset = (load_str != L"\0");

      if (! customAsset)
      {
        if      (libTexToLoad == LibraryTexture::Icon &&
                 SKIF_Util_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath ( ), SKIFCustomPath + L"-original.png"))
          load_str =             SKIFCustomPath + L"-original.png";
        else if (libTexToLoad == LibraryTexture::Icon)
        {
          managedAsset = false; // GOG default icons are not managed
          load_str =             name;
        }

        else if (libTexToLoad == LibraryTexture::Cover)
        {
          managedAsset = false; // GOG covers are not managed

          extern std::wstring GOGGalaxy_UserID;
          load_str = SK_FormatStringW (LR"(C:\ProgramData\GOG.com\Galaxy\webcache\%ws\gog\%i\)", GOGGalaxy_UserID.c_str(), appid);

          HANDLE hFind        = INVALID_HANDLE_VALUE;
          WIN32_FIND_DATA ffd = { };

          hFind =
            FindFirstFileExW ((load_str + name).c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);
          //FindFirstFile((load_str + name).c_str(), &ffd);

          if (INVALID_HANDLE_VALUE != hFind)
          {
            load_str += ffd.cFileName;
            FindClose(hFind);
          }
        }
      }
    }

    // Xbox
    else if (pApp->store == app_record_s::Store::Xbox)
    {
      std::wstring XboxAssetPath = SK_FormatStringW(LR"(%ws\Assets\Xbox\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->xbox.package_name).c_str());
      SKIFCustomPath = std::wstring(XboxAssetPath);

      if (libTexToLoad == LibraryTexture::Cover)
        SKIFCustomPath += L"cover";
      else
        SKIFCustomPath += L"icon";

      if      (PathFileExistsW  ((SKIFCustomPath + L".png").c_str()))
        load_str =                SKIFCustomPath + L".png";
      else if (PathFileExistsW  ((SKIFCustomPath + L".jpg").c_str()))
        load_str =                SKIFCustomPath + L".jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               PathFileExistsW  ((SKIFCustomPath + L".ico").c_str()))
        load_str =                SKIFCustomPath + L".ico";

      customAsset = (load_str != L"\0");

      if (! customAsset)
      {
        if      (libTexToLoad == LibraryTexture::Cover &&
                 PathFileExistsW ((SKIFCustomPath + L"-original.png").c_str()))
          load_str =               SKIFCustomPath + L"-original.png";
        else if (libTexToLoad == LibraryTexture::Cover &&
                 PathFileExistsW ((SKIFCustomPath + L"-fallback.png").c_str()))
          load_str =               SKIFCustomPath + L"-fallback.png";
        else if (libTexToLoad == LibraryTexture::Icon &&
                 PathFileExistsW ((SKIFCustomPath + L"-original.png").c_str()))
          load_str =               SKIFCustomPath + L"-original.png";
      }
    }

    // STEAM
    else if (pApp->store == app_record_s::Store::Steam)
    {

      SKIFCustomPath  = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",           _path_cache.specialk_userdata,                            appid);
      SteamCustomPath = SK_FormatStringW (LR"(%ws\userdata\%i\config\grid\%i)", _path_cache.steam_install, SKIF_Steam_GetCurrentUser ( ), appid);

      if (libTexToLoad == LibraryTexture::Cover)
        SKIFCustomPath += L"cover";
      else
        SKIFCustomPath += L"icon";

      if      (PathFileExistsW (( SKIFCustomPath +  L".png").c_str()))
        load_str =                SKIFCustomPath +  L".png";
      else if (PathFileExistsW (( SKIFCustomPath +  L".jpg").c_str()))
        load_str =                SKIFCustomPath +  L".jpg";
      else if (libTexToLoad == LibraryTexture::Icon  &&
               PathFileExistsW (( SKIFCustomPath +  L".ico").c_str()))
        load_str =                SKIFCustomPath +  L".ico";

      customAsset = (load_str != L"\0");

      if (! customAsset)
      {
        managedAsset = false; // Steam's user-specific custom covers are not managed

        if      (libTexToLoad == LibraryTexture::Cover &&
                 PathFileExistsW ((SteamCustomPath + L"p.png").c_str()))
          load_str =               SteamCustomPath + L"p.png";
        else if (libTexToLoad == LibraryTexture::Cover &&
                 PathFileExistsW ((SteamCustomPath + L"p.jpg").c_str()))
          load_str =               SteamCustomPath + L"p.jpg";
        else {
          load_str =               name;

          // The fallback is normally managed! <%ws\Assets\Steam\%i\cover-original.jpg>
          // The only time it is not is when SKIF is in tinyCovers mode and loaded the original 300x450 cover from the Steam client
          if (load_str.find (L"cover-original.jpg") != std::wstring::npos)
            managedAsset = true;
        }
      }
    }
  }

  PLOG_VERBOSE_IF (load_str != L"\0") << "Texture to load: " << load_str;

  if (load_str != L"\0")
  {
    succeeded = FastTextureLoading (load_str, meta, img);
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

  // Push the existing texture to a stack to be released after the frame
  //   Do this regardless of whether we could actually load the new cover or not
  if (pLibTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pLibTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push (pLibTexSRV.p);
    pLibTexSRV.p = nullptr;
  }

  if (! succeeded)
    return;

  DirectX::ScratchImage* pImg  =   &img;
  DirectX::ScratchImage   converted_img;

  // Start aspect ratio
#if 0
  vCoverUv0 = ImVec2(0.f, 0.f); // Top left corner
  vCoverUv1 = ImVec2(1.f, 1.f); // Bottom right corner
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
#endif
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
  resolution.x = static_cast<float> (meta.width);
  resolution.y = static_cast<float> (meta.height);

  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return;

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
          &pLibTexSRV.p
        )
      )
    )
    {
      //pLibTexSRV = pOrigTexSRV;
    }

    // If everything went well
    else {
      DWORD post = SKIF_Util_timeGetTime1 ( );
      PLOG_INFO << "[Image Processing] Processed image in " << (post - pre) << " ms.";

      if (pApp != nullptr)
      {
        if      (libTexToLoad == LibraryTexture::Cover)
        {
          pApp->tex_cover.isCustom  = customAsset;
          pApp->tex_cover.isManaged = managedAsset;
        }
        else if (libTexToLoad == LibraryTexture::Icon)
        {
          pApp->tex_icon.isCustom  = customAsset;
          pApp->tex_icon.isManaged = managedAsset;
        }
      }
    }

    // SRV is holding a reference, this is not needed anymore.
    pTex2D = nullptr;
  }
};
#include <stores/generic_library2.h>

#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/fsutil.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>

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
#define STBI_ONLY_GIF // Custom covers can be GIFs (their first frame; the rest are played while hovered, see LoadAnimatedLibraryTexture)
//#define STBI_ONLY_HDR
//#define STBI_ONLY_PIC
//#define STBI_ONLY_PNM

#include <stb_image.h>

// WebP covers, still and animated (packages_misc/libwebp: its decoder and demux)
#include <libwebp/src/webp/decode.h>
#include <libwebp/src/webp/demux.h>

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

static bool _LoadWebPImage (const std::wstring& path, DirectX::TexMetadata& meta, DirectX::ScratchImage& img); // Animated Covers

bool
FastTextureLoading (const std::wstring& path, DirectX::TexMetadata& meta, DirectX::ScratchImage& img)
{
  bool success = false;

  const std::filesystem::path imagePath (path.data());
  std::wstring   ext = SKIF_Util_ToLowerW(imagePath.extension().wstring());
  std::string szPath = SK_WideCharToUTF8(path);

  // WebP (libwebp): stb_image has no WebP decoder, and WIC only one if the WebP Image Extension is installed. An animated
  //   WebP gives its first frame. If libwebp cannot decode it, WIC is tried below.
  if (_LoadWebPImage (path, meta, img))
    return true;

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

// Scales the image to fill width x height, keeping its aspect ratio (one side ends up larger: it is cropped where drawn).
//   multipleOf4: both sides rounded up to a multiple of 4 (block-compressed textures need that). On success, pImg points
//     to target and meta is its metadata.
static bool
_ScaleLibraryImageToFill (DirectX::TexMetadata& meta, DirectX::ScratchImage*& pImg, DirectX::ScratchImage& target, float width, float height, bool multipleOf4 = false)
{
  float imageAspectRatio   = static_cast<float> (meta.width) / static_cast<float> (meta.height);
  float defaultAspectRatio = static_cast<float> (width) / static_cast<float> (height);

  if (imageAspectRatio > defaultAspectRatio)
    width = height * imageAspectRatio;
  else
    height = width / imageAspectRatio;

  if (multipleOf4)
  {
    width  = std::ceil (width  / 4.0f) * 4.0f;
    height = std::ceil (height / 4.0f) * 4.0f;
  }

  // Resize into an image of its own: pImg may already point to target (single-channel covers converted earlier),
  //   and DirectX::Resize () releases its output image before reading the source images (and metadata) it was given
  DirectX::ScratchImage resized_img;

  if (
    SUCCEEDED (
      DirectX::Resize (
        pImg->GetImages   (), pImg->GetImageCount (),
        pImg->GetMetadata (), std::max (static_cast<size_t> (width), size_t (1)), std::max (static_cast<size_t> (height), size_t (1)),
        DirectX::TEX_FILTER_FANT,
            resized_img
      )
    )
  )
  {
    target = std::move (resized_img);
    meta   =  target.GetMetadata ();
    pImg   = &target;
    return true;
  }

  return false;
}

// A thumbnail as it is kept: 3 mip levels (it is drawn at most about 1.5 times smaller than it is made), compressed to
//   BC7 (a quarter of the memory of RGBA, also on the GPU). If compressing fails it stays uncompressed, with its mips.
//   On success, pImg points to target and meta is its metadata. Worker threads: CPU only.
static bool
_MakeThumbnailImage (DirectX::TexMetadata& meta, DirectX::ScratchImage*& pImg, DirectX::ScratchImage& target)
{
  DirectX::ScratchImage mipmapped;

  if (FAILED (DirectX::GenerateMipMaps (pImg->GetImages ( ), pImg->GetImageCount ( ), pImg->GetMetadata ( ),
                                        DirectX::TEX_FILTER_FORCE_NON_WIC, 3, mipmapped)))
    return false;

  DirectX::ScratchImage compressed;

  // BC7_QUICK: the fast mode of the CPU encoder (good for covers; the full search is many times slower)
  if (SUCCEEDED (DirectX::Compress (mipmapped.GetImages ( ), mipmapped.GetImageCount ( ), mipmapped.GetMetadata ( ),
                                    DXGI_FORMAT_BC7_UNORM, DirectX::TEX_COMPRESS_BC7_QUICK, DirectX::TEX_THRESHOLD_DEFAULT, compressed)))
    target = std::move (compressed);

  else
  {
    PLOG_WARNING << "Could not compress a thumbnail to BC7; keeping it uncompressed.";
    target = std::move (mipmapped);
  }

  meta =  target.GetMetadata ( );
  pImg = &target;

  return true;
}

// The last steps of loading a library texture: mipmaps, then the texture and its view on the D3D11 device.
//   pImg is the decoded (and converted / scaled) image, meta its metadata. Sets out.srv and out.resolution.
static bool
_CreateLibraryTexture (DirectX::TexMetadata& meta, DirectX::ScratchImage* pImg, library_texture_s& out)
{
  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::ScratchImage     converted_img;

  // Generate mipmaps, so covers and icons drawn smaller than their resolution (e.g. 600x900 covers in horizon mode, or the
  //   larger extracted icons at low DPI scales) are filtered smoothly instead of aliasing. Images with transparency are
  //     mipmapped with premultiplied alpha, as the color of fully transparent pixels (often black) would otherwise bleed
  //       into the edges. FORCE_NON_WIC keeps this independent of COM/WIC on the worker thread.
  if (meta.mipLevels <= 1 && (meta.width > 1 || meta.height > 1))
  {
    DirectX::ScratchImage  premultiplied_img,
                           mipmapped_img,
                           straight_img;
    DirectX::ScratchImage* pSrc     = pImg;
    const bool             hasAlpha = ! pImg->IsAlphaAllOpaque ( );

    if (hasAlpha &&
      SUCCEEDED (
        DirectX::PremultiplyAlpha (
          pImg->GetImages   (), pImg->GetImageCount (),
          pImg->GetMetadata (), DirectX::TEX_PMALPHA_DEFAULT,
              premultiplied_img
        )
      )
    )
      pSrc = &premultiplied_img;

    if (
      SUCCEEDED (
        DirectX::GenerateMipMaps (
          pSrc->GetImages   (), pSrc->GetImageCount (),
          pSrc->GetMetadata (), DirectX::TEX_FILTER_FORCE_NON_WIC, 0, // 0 = the full mip chain
              mipmapped_img
        )
      )
    )
    {
      bool success = true;

      // Back to straight alpha, which is what the rest of SKIF expects
      if (pSrc == &premultiplied_img)
      {
        success =
          SUCCEEDED (
            DirectX::PremultiplyAlpha (
              mipmapped_img.GetImages   (), mipmapped_img.GetImageCount (),
              mipmapped_img.GetMetadata (), DirectX::TEX_PMALPHA_REVERSE,
                  straight_img
            )
          );

        if (success)
          mipmapped_img = std::move (straight_img);
      }

      if (success)
      {
        converted_img = std::move (mipmapped_img);
        meta          =  converted_img.GetMetadata ();
        pImg          = &converted_img;
      }
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

    // SRV is holding a reference, this is not needed anymore.
    pTex2D = nullptr;
  }

  return (out.srv.p != nullptr);
}

bool
LoadLibraryTextureEx (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        const std::wstring&                 name,
        app_record_s*                       pApp,
        library_texture_s&                  out,
        const library_thumbnail_s*          thumb)
{
  // Settings and paths are only read here; see the threading notes in generic_library2.h
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  std::wstring load_str = L"\0";

  bool succeeded    = false;
  bool customAsset  = false;
  bool managedAsset = true; // Assume true (only GOG and SKIF itself is not managed)

  DWORD pre = SKIF_Util_timeGetTime1();

  // A cover SKIF downloaded that does not decode (e.g. an error page saved as the image) would otherwise stay
  //   forever, since an existing file is never downloaded again: delete it. Returns true if it was deleted.
  auto _DeleteUndecodableDownload = [&](const std::wstring& path) -> bool
  {
    if (pApp == nullptr || libTexToLoad != LibraryTexture::Cover || ! managedAsset || customAsset)
      return false;

    const std::filesystem::path fsPath (path);
    std::wstring fileName = SKIF_Util_ToLowerW (fsPath.filename ( ).wstring ( ));

    if ((fileName.rfind (L"cover-original.", 0) == 0 || fileName == L"cover-pcgw.png" || fileName == L"cover-igdb.jpg") &&
        SKIF_Util_ToLowerW (fsPath.parent_path ( ).wstring ( ) + L"\\") == SKIF_Util_ToLowerW (pApp->GetAssetDirectory ( )) &&
        DeleteFileW (path.c_str ( )))
    {
      PLOG_WARNING << "Deleted a downloaded cover that could not be decoded: " << path;
      return true;
    }

    return false;
  };

  // Games (not embedded Special K resources)
  if (pApp != nullptr)
  {
    appid = pApp->id;

    // Store-specific: the user's custom artwork, else the store's (Special K: custom only, else embedded below).
    //   If a downloaded cover does not decode it is deleted, and the next candidate in the store's order is tried
    //   right away (e.g. the PCGamingWiki cover, then the store's cover). Each retry needs one file fewer, so this ends.
    for (int attempt = 0; attempt < 3; attempt++)
    {
      customAsset  = false;
      managedAsset = true; // Assume true (only GOG and SKIF itself is not managed)

      // A preferred IGDB cover (Settings) comes before the store's own cover, but never before a custom or
      //   PCGamingWiki cover (GetPreferredIGDBCover returns nothing then, so ResolveArtwork picks those)
      load_str     = (libTexToLoad == LibraryTexture::Cover) ? pApp->GetPreferredIGDBCover ( ) : L"";

      if (load_str.empty ( ))
        load_str   = pApp->ResolveArtwork (libTexToLoad, name, customAsset, managedAsset);

      if (load_str == L"\0")
        break;

      PLOG_VERBOSE << "Texture to load: " << load_str;

      succeeded = FastTextureLoading (load_str, meta, img);

      if (succeeded || ! _DeleteUndecodableDownload (load_str))
        break;

      PLOG_INFO << "Trying the next cover candidate...";
    }
  }

  if (load_str     == L"\0"                &&
      appid        == SKIF_STEAM_APPID     &&
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

  // Thumbnails (the grid view): the cover, scaled to fill the tile, mipmapped and compressed (_MakeThumbnailImage), and
  //   saved as the game's thumbnail cache: a DDS, which loads as it is (no decoding, scaling or mipmaps)
  DirectX::ScratchImage thumb_img;

  if (thumb != nullptr && libTexToLoad == LibraryTexture::Cover)
  {
    if (_ScaleLibraryImageToFill (meta, pImg, converted_img, thumb->width, thumb->height, true) &&
        _MakeThumbnailImage      (meta, pImg, thumb_img) && ! thumb->cachePath.empty ( ))
    {
      // Written to a temporary file first, so a thumbnail being read (another worker) is never a partial file
      std::wstring tmp = thumb->cachePath + L".tmp";

      if (SUCCEEDED (DirectX::SaveToDDSFile (pImg->GetImages ( ), pImg->GetImageCount ( ), pImg->GetMetadata ( ),
                                             DirectX::DDS_FLAGS_NONE, tmp.c_str ( ))))
      {
        if (! MoveFileExW (tmp.c_str ( ), thumb->cachePath.c_str ( ), MOVEFILE_REPLACE_EXISTING))
          DeleteFileW (tmp.c_str ( ));
      }

      else
        PLOG_WARNING << "Could not save the thumbnail: " << thumb->cachePath;
    }
  }

  // Downscale covers to 220x330, which will then be shown in horizon mode
  else if ((_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass && libTexToLoad == LibraryTexture::Cover) ||
           (libTexToLoad == LibraryTexture::Logo  && name == L"sk_boxart_small.png"))
    _ScaleLibraryImageToFill (meta, pImg, converted_img, 220.0f, 330.0f);

  // A thumbnail no longer needed (its tile went out of view while it loaded): no texture
  if (thumb != nullptr && thumb->wanted && ! thumb->wanted ( ))
    return false;

  if (! _CreateLibraryTexture (meta, pImg, out))
    return false;

  DWORD post = SKIF_Util_timeGetTime1 ( );
  PLOG_DEBUG << "[Image Processing] Processed image in " << (post - pre) << " ms.";

  out.isCustom  = customAsset;
  out.isManaged = managedAsset;

  // An animated cover (APNG, GIF) shows its first frame (thumbnails too); the rest are loaded once it is hovered (SKIF_LibraryArtwork)
  if (libTexToLoad == LibraryTexture::Cover && pApp != nullptr && IsAnimatedImageFile (load_str))
    out.animatedSource = load_str;

  return true;
}

// A thumbnail from the thumbnail cache: a DDS SKIF saved (BC7 with its mips, see _MakeThumbnailImage), used as it is
bool
LoadLibraryThumbnailFromCache (const std::wstring& path, library_texture_s& out, const library_thumbnail_s* thumb)
{
  DirectX::TexMetadata  meta = { };
  DirectX::ScratchImage img  = { };

  if (FAILED (DirectX::LoadFromDDSFile (path.c_str ( ), DirectX::DDS_FLAGS_NONE, &meta, img)))
    return false;

  // No longer needed (see library_thumbnail_s::wanted)
  if (thumb != nullptr && thumb->wanted && ! thumb->wanted ( ))
    return false;

  return _CreateLibraryTexture (meta, &img, out);
}

#pragma region Animated Covers

// Animated covers (APNG, GIF, WebP), decoded frame by frame by the decoders of animated_images.inl (which says how, and why)
#include "animated_images.inl" // The decoders (APNG, GIF, WebP) and IsAnimatedImageFile, in a file of their own (tools\image_tests)

// A WebP file as a still image, for FastTextureLoading. False if it is not a WebP, or cannot be decoded.
static bool
_LoadWebPImage (const std::wstring& path, DirectX::TexMetadata& meta, DirectX::ScratchImage& img)
{
  std::ifstream stream (std::filesystem::path (path), std::ios::binary);

  uint8_t header [12] = { };

  if (! stream.read (reinterpret_cast <char*> (header), sizeof (header)) || ! _IsWebP (header, sizeof (header)))
    return false;

  std::error_code ec;
  const auto fileSize = std::filesystem::file_size (std::filesystem::path (path), ec);

  if (ec || fileSize > 256 * 1024 * 1024)
    return false;

  std::vector <uint8_t> file (static_cast <size_t> (fileSize));
  stream.seekg (0);

  if (! stream.read (reinterpret_cast <char*> (file.data ( )), file.size ( )))
    return false;

  PLOG_VERBOSE << "Using libwebp decoder...";

  uint32_t              width  = 0,
                        height = 0;
  std::vector <uint8_t> rgba;

  if (! _DecodeWebPStill (file.data ( ), file.size ( ), width, height, rgba))
    return false;

  if (FAILED (img.Initialize2D (DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1)))
    return false;

  memcpy (img.GetImage (0, 0, 0)->pixels, rgba.data ( ), rgba.size ( ));
  meta = img.GetMetadata ( );

  return true;
}


// An animated image (APNG, GIF), read and parsed: its canvas, and how many frames it has. Decode ( ) hands over its frames.
struct animated_image_s
{
  std::vector <uint8_t> file;
  apng_s                apng;    // APNG: the parsed frames
  bool                  isGIF  = false;
  bool                  isWebP = false;
  uint32_t              width  = 0,
                        height = 0,
                        count  = 0;

  int Decode (const std::function <bool (const uint8_t*, uint32_t)>& onFrame) const
  {
    return (isWebP) ? _DecodeWebP (file.data ( ), file.size ( ), onFrame) :
           (isGIF)  ? _DecodeGIF  (file.data ( ), file.size ( ), onFrame)
                    : _DecodeAPNG (apng, onFrame);
  }
};

// Reads and parses an animated image. False if it is not one (fewer than two frames), is malformed, or has more than
//   maxFrames frames.
static bool
_OpenAnimatedImage (const std::wstring& path, animated_image_s& image, uint32_t maxFrames)
{
  // The whole file (APNG frames are parsed from it, GIF frames decoded from it)
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size (std::filesystem::path (path), ec);

  if (ec || fileSize < 16 || fileSize > 256 * 1024 * 1024)
    return false;

  image.file.resize (static_cast <size_t> (fileSize));
  {
    std::ifstream stream (std::filesystem::path (path), std::ios::binary);

    if (! stream.read (reinterpret_cast <char*> (image.file.data ( )), image.file.size ( )))
      return false;
  }

  // The canvas and the number of frames
  image.isWebP = _IsWebP (image.file.data ( ), image.file.size ( ));
  image.isGIF  = (memcmp (image.file.data ( ), "GIF8", 4) == 0);

  if (image.isWebP)
  {
    if (! _ScanWebP (image.file.data ( ), image.file.size ( ), image.width, image.height, image.count))
      return false;
  }

  else if (image.isGIF)
  {
    if (! _ScanGIF (image.file.data ( ), image.file.size ( ), image.width, image.height, image.count))
      return false;
  }

  else
  {
    if (! _ParseAPNG (image.file.data ( ), image.file.size ( ), image.apng))
      return false;

    image.width  = image.apng.width;
    image.height = image.apng.height;
    image.count  = static_cast <uint32_t> (image.apng.frames.size ( ));
  }

  if (image.count < 2 || image.count > maxFrames || image.width == 0 || image.height == 0 || image.width > 16384 || image.height > 16384)
  {
    PLOG_WARNING_IF (image.count > maxFrames) << "Animated cover has too many frames (" << image.count << "): " << path;
    return false;
  }

  return true;
}

bool
LoadAnimatedLibraryTexture (const std::wstring&                path,
                            std::vector <library_texture_s>&   frames,
                            std::vector <DWORD>&               delays,
                            const std::function <bool (void)>& wanted)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // All the frames together, mipmaps included, are kept below this (they are scaled down to fit), and there can be no more
  //   frames than this. Below a quarter of the size (each way) it would look too poor: it stays a still cover then.
  constexpr double   budgetBytes = 192.0 * 1024 * 1024;
  constexpr uint32_t maxFrames   = 1000;
  constexpr double   minScale    = 0.25;

  frames.clear ( );
  delays.clear ( );

  DWORD pre = SKIF_Util_timeGetTime1 ( );

  animated_image_s image;

  if (! _OpenAnimatedImage (path, image, maxFrames))
    return false;

  const uint32_t width  = image.width,
                 height = image.height,
                 count  = image.count;

  // The size of the frames: as the cover is shown (low-res covers: 220x330, filled as _ScaleLibraryImageToFill does),
  //   then smaller if all of them would take more than the budget
  double targetWidth  = width,
         targetHeight = height;

  if (_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass)
  {
    targetWidth  = 220.0;
    targetHeight = 330.0;

    if (static_cast <double> (width) / height > targetWidth / targetHeight)
      targetWidth  = targetHeight * width / height;
    else
      targetHeight = targetWidth * height / width;

    // Never larger than they are (the frames are drawn at the cover's size either way)
    if (targetWidth > width)
    {
      targetWidth  = width;
      targetHeight = height;
    }
  }

  const double bytes = targetWidth * targetHeight * 4.0 * (4.0 / 3.0) * count; // RGBA, with mipmaps

  if (bytes > budgetBytes)
  {
    const double scale = std::sqrt (budgetBytes / bytes);

    if (scale * targetWidth / width < minScale)
    {
      PLOG_WARNING << "Animated cover is too large to play (" << width << "x" << height << ", " << count << " frames): " << path;
      return false;
    }

    targetWidth  *= scale;
    targetHeight *= scale;
  }

  const size_t outWidth  = std::max (static_cast <size_t> (targetWidth),  size_t (1)),
               outHeight = std::max (static_cast <size_t> (targetHeight), size_t (1));
  const bool   resize    = (outWidth != width || outHeight != height);

  frames.reserve (count);
  delays.reserve (count);

  bool failed = false;

  // Each frame: the canvas as it is at that point, scaled, made into a texture
  auto _OnFrame = [&](const uint8_t* canvas, uint32_t delay) -> bool
  {
    if (wanted && ! wanted ( ))
    {
      failed = true;
      return false;
    }

    DirectX::ScratchImage  img;
    DirectX::ScratchImage  resized;
    DirectX::ScratchImage* pImg = &img;

    if (FAILED (img.Initialize2D (DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1)))
    {
      failed = true;
      return false;
    }

    memcpy (img.GetImage (0, 0, 0)->pixels, canvas, static_cast <size_t> (width) * height * 4);

    if (resize)
    {
      if (FAILED (DirectX::Resize (img.GetImages ( ), img.GetImageCount ( ), img.GetMetadata ( ),
                                   outWidth, outHeight, DirectX::TEX_FILTER_FANT, resized)))
      {
        failed = true;
        return false;
      }

      pImg = &resized;
    }

    DirectX::TexMetadata meta = pImg->GetMetadata ( );
    library_texture_s    texture;

    if (! _CreateLibraryTexture (meta, pImg, texture))
    {
      failed = true;
      return false;
    }

    frames.push_back (std::move (texture));
    delays.push_back (delay);

    return true;
  };

  const int decoded = image.Decode (_OnFrame);

  if (failed || decoded < 2 || frames.size ( ) < 2)
  {
    PLOG_WARNING_IF (! failed) << "Could not decode the animated cover: " << path;
    frames.clear ( ); // Released right away: nothing else has seen them
    delays.clear ( );
    return false;
  }

  DWORD post = SKIF_Util_timeGetTime1 ( );
  PLOG_DEBUG << "[Image Processing] Loaded " << frames.size ( ) << " frames (" << outWidth << "x" << outHeight
             << ") of an animated cover in " << (post - pre) << " ms.";

  return true;
}

// The animation cache of the grid view (LoadAnimatedLibraryThumbnail): a small header, each frame's delay (ms), then the
//   frames as a DDS texture array (each frame a thumbnail: BC7, 3 mips; see _MakeThumbnailImage)
struct anim_cache_header_s
{
  char     magic [4] = { 'S', 'K', 'A', 'N' };
  uint32_t version   = 1;
  uint32_t frames    = 0;
  uint32_t reserved  = 0;
};

static bool
_LoadAnimationCache (const std::wstring& path, DirectX::ScratchImage& array, std::vector <DWORD>& delays)
{
  std::error_code ec;
  const auto size = std::filesystem::file_size (std::filesystem::path (path), ec);

  if (ec || size < sizeof (anim_cache_header_s) || size > 1024ull * 1024 * 1024)
    return false;

  std::vector <uint8_t> data (static_cast <size_t> (size));
  {
    std::ifstream stream (std::filesystem::path (path), std::ios::binary);

    if (! stream.read (reinterpret_cast <char*> (data.data ( )), data.size ( )))
      return false;
  }

  anim_cache_header_s header;
  const anim_cache_header_s expected;
  memcpy (&header, data.data ( ), sizeof (header));

  if (memcmp (header.magic, expected.magic, 4) != 0 || header.version != expected.version || header.frames < 2 ||
      data.size ( ) < sizeof (header) + static_cast <size_t> (header.frames) * sizeof (uint32_t))
    return false;

  delays.resize (header.frames);

  for (uint32_t i = 0; i < header.frames; i++)
  {
    uint32_t delay = 0;
    memcpy (&delay, data.data ( ) + sizeof (header) + i * sizeof (uint32_t), sizeof (uint32_t));
    delays [i] = delay;
  }

  const size_t offset = sizeof (header) + static_cast <size_t> (header.frames) * sizeof (uint32_t);

  DirectX::TexMetadata meta = { };

  if (FAILED (DirectX::LoadFromDDSMemory (data.data ( ) + offset, data.size ( ) - offset, DirectX::DDS_FLAGS_NONE, &meta, array)))
    return false;

  return (meta.arraySize == header.frames && meta.dimension == DirectX::TEX_DIMENSION_TEXTURE2D);
}

static bool
_SaveAnimationCache (const std::wstring& path, const DirectX::ScratchImage& array, const std::vector <DWORD>& delays)
{
  DirectX::Blob blob;

  if (FAILED (DirectX::SaveToDDSMemory (array.GetImages ( ), array.GetImageCount ( ), array.GetMetadata ( ), DirectX::DDS_FLAGS_NONE, blob)))
    return false;

  anim_cache_header_s header;
  header.frames = static_cast <uint32_t> (delays.size ( ));

  // Written to a temporary file first, so one being read (another worker) is never a partial file
  const std::wstring tmp = path + L".tmp";
  {
    std::ofstream stream (std::filesystem::path (tmp), std::ios::binary | std::ios::trunc);

    if (! stream)
      return false;

    stream.write (reinterpret_cast <const char*> (&header), sizeof (header));

    for (DWORD delay : delays)
    {
      const uint32_t value = static_cast <uint32_t> (delay);
      stream.write (reinterpret_cast <const char*> (&value), sizeof (value));
    }

    stream.write (reinterpret_cast <const char*> (blob.GetBufferPointer ( )), static_cast <std::streamsize> (blob.GetBufferSize ( )));

    if (! stream)
    {
      stream.close ( );
      DeleteFileW (tmp.c_str ( ));
      return false;
    }
  }

  if (! MoveFileExW (tmp.c_str ( ), path.c_str ( ), MOVEFILE_REPLACE_EXISTING))
  {
    DeleteFileW (tmp.c_str ( ));
    return false;
  }

  return true;
}

// A frame of an animated thumbnail: 3 mip levels (as a thumbnail), compressed to BC1, or BC3 if the cover has transparency
//   (alpha). Not BC7 as still thumbnails are: the CPU encoder takes several hundred milliseconds a frame, BC1 / BC3 a few,
//     and BC1 takes half the memory. On success, pImg points to target and meta is its metadata. Worker threads: CPU only.
static bool
_MakeAnimatedThumbnailFrame (DirectX::TexMetadata& meta, DirectX::ScratchImage*& pImg, DirectX::ScratchImage& target, bool alpha)
{
  DirectX::ScratchImage mipmapped;

  if (FAILED (DirectX::GenerateMipMaps (pImg->GetImages ( ), pImg->GetImageCount ( ), pImg->GetMetadata ( ),
                                        DirectX::TEX_FILTER_FORCE_NON_WIC, 3, mipmapped)))
    return false;

  if (FAILED (DirectX::Compress (mipmapped.GetImages ( ), mipmapped.GetImageCount ( ), mipmapped.GetMetadata ( ),
                                 (alpha) ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC1_UNORM,
                                 DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, target)))
    return false;

  meta =  target.GetMetadata ( );
  pImg = &target;

  return true;
}

// The frames of an animated cover as thumbnails, in a texture array: each scaled to fill width x height (as a thumbnail)
//   and made a frame (_MakeAnimatedThumbnailFrame; BC1 / BC3 by the first frame's alpha). tooLarge: they would take more
//     than maxBytes (stopped once the first frame says so).
static bool
_BuildAnimatedThumbnail (const std::wstring& source, float width, float height, size_t maxBytes,
                         DirectX::ScratchImage& array, std::vector <DWORD>& delays, bool& tooLarge,
                         const std::function <bool (void)>& wanted)
{
  constexpr uint32_t maxFrames = 1000;

  animated_image_s image;

  if (! _OpenAnimatedImage (source, image, maxFrames))
    return false;

  std::vector <DirectX::ScratchImage> made;
  made.reserve (image.count);
  delays.clear ( );

  bool failed = false;
  bool alpha  = false; // The first frame has transparency: BC3 for all of them

  auto _OnFrame = [&](const uint8_t* canvas, uint32_t delay) -> bool
  {
    if (wanted && ! wanted ( ))
    {
      failed = true;
      return false;
    }

    DirectX::ScratchImage  img;
    DirectX::ScratchImage  scaled;
    DirectX::ScratchImage  thumb;
    DirectX::ScratchImage* pImg = &img;

    if (FAILED (img.Initialize2D (DXGI_FORMAT_R8G8B8A8_UNORM, image.width, image.height, 1, 1)))
    {
      failed = true;
      return false;
    }

    memcpy (img.GetImage (0, 0, 0)->pixels, canvas, static_cast <size_t> (image.width) * image.height * 4);

    if (made.empty ( ))
      alpha = ! img.IsAlphaAllOpaque ( );

    DirectX::TexMetadata meta = img.GetMetadata ( );

    if (! _ScaleLibraryImageToFill    (meta, pImg, scaled, width, height, true) ||
        ! _MakeAnimatedThumbnailFrame (meta, pImg, thumb, alpha))
    {
      failed = true;
      return false;
    }

    // All of them would take more than the budget (each frame takes as much as the first)
    if (made.empty ( ) && thumb.GetPixelsSize ( ) * image.count > maxBytes)
    {
      tooLarge = true;
      failed   = true;
      return false;
    }

    // Each frame must be like the first
    if (! made.empty ( ) && (thumb.GetMetadata ( ).format    != made [0].GetMetadata ( ).format ||
                             thumb.GetMetadata ( ).width     != made [0].GetMetadata ( ).width  ||
                             thumb.GetMetadata ( ).mipLevels != made [0].GetMetadata ( ).mipLevels))
    {
      failed = true;
      return false;
    }

    made.push_back   (std::move (thumb));
    delays.push_back (delay);

    return true;
  };

  if (image.Decode (_OnFrame) < 2 || failed || made.size ( ) < 2)
    return false;

  // Into one texture array
  const DirectX::TexMetadata& first = made [0].GetMetadata ( );

  if (FAILED (array.Initialize2D (first.format, first.width, first.height, made.size ( ), first.mipLevels)))
    return false;

  for (size_t item = 0; item < made.size ( ); item++)
  {
    for (size_t mip = 0; mip < first.mipLevels; mip++)
    {
      const DirectX::Image* src = made [item].GetImage (mip, 0, 0);
      const DirectX::Image* dst = array.GetImage       (mip, item, 0);

      if (src == nullptr || dst == nullptr || src->slicePitch != dst->slicePitch)
        return false;

      memcpy (dst->pixels, src->pixels, src->slicePitch);
    }
  }

  return true;
}

// A texture (and its view) from images already made: a frame of a texture array, with its mips
static bool
_CreateLibraryTextureFromImages (const DirectX::TexMetadata& meta, const DirectX::Image* images, size_t count, library_texture_s& out)
{
  auto pDevice =
    SKIF_D3D11_GetDevice ();

  if (! pDevice)
    return false;

  CComPtr <ID3D11Texture2D> pTex2D;

  if (FAILED (DirectX::CreateTexture (pDevice, images, count, meta, (ID3D11Resource **)&pTex2D.p)) || pTex2D.p == nullptr)
    return false;

  D3D11_SHADER_RESOURCE_VIEW_DESC
    srv_desc                           = { };
    srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels       = UINT_MAX;
    srv_desc.Texture2D.MostDetailedMip =  0;

  if (FAILED (pDevice->CreateShaderResourceView (pTex2D.p, &srv_desc, &out.srv.p)))
    return false;

  out.resolution = ImVec2 (static_cast <float> (meta.width), static_cast <float> (meta.height));

  return (out.srv.p != nullptr);
}

bool
LoadAnimatedLibraryThumbnail (const std::wstring&                source,
                              const library_thumbnail_s&         thumb,
                              bool                               useCache,
                              size_t                             maxBytes,
                              bool                               createTextures,
                              std::vector <library_texture_s>&   frames,
                              std::vector <DWORD>&               delays,
                              size_t&                            bytes,
                              bool&                              tooLarge)
{
  frames.clear ( );
  delays.clear ( );
  bytes    = 0;
  tooLarge = false;

  DWORD pre = SKIF_Util_timeGetTime1 ( );

  DirectX::ScratchImage array;
  bool                  built = false;

  // The cache, else a build (saved to the cache)
  if (! useCache || thumb.cachePath.empty ( ) || ! _LoadAnimationCache (thumb.cachePath, array, delays))
  {
    if (! _BuildAnimatedThumbnail (source, thumb.width, thumb.height, maxBytes, array, delays, tooLarge, thumb.wanted))
    {
      PLOG_WARNING_IF (tooLarge) << "Animated cover is too large for the memory set aside for animated covers in the grid view: " << source;
      return false;
    }

    built = true;

    if (! thumb.cachePath.empty ( ) && ! _SaveAnimationCache (thumb.cachePath, array, delays))
      PLOG_WARNING << "Could not save the animated thumbnail: " << thumb.cachePath;
  }

  const DirectX::TexMetadata& meta = array.GetMetadata ( );

  bytes = array.GetPixelsSize ( );

  // Made for a larger budget (Settings)
  if (bytes > maxBytes || delays.size ( ) != meta.arraySize)
  {
    tooLarge = (bytes > maxBytes);
    return false;
  }

  if (createTextures)
  {
    DirectX::TexMetadata frameMeta = meta;
    frameMeta.arraySize = 1;

    frames.reserve (meta.arraySize);

    for (size_t item = 0; item < meta.arraySize; item++)
    {
      // No longer needed
      if (thumb.wanted && ! thumb.wanted ( ))
      {
        frames.clear ( );
        return false;
      }

      library_texture_s texture;

      // A frame's images (its mips) follow each other in the array (DirectXTex: by item, then by mip)
      if (! _CreateLibraryTextureFromImages (frameMeta, array.GetImage (0, item, 0), meta.mipLevels, texture))
      {
        frames.clear ( );
        return false;
      }

      frames.push_back (std::move (texture));
    }
  }

  DWORD post = SKIF_Util_timeGetTime1 ( );
  PLOG_DEBUG << "[Image Processing] " << ((built) ? "Built" : "Loaded") << " an animated thumbnail (" << delays.size ( ) << " frames, "
             << meta.width << "x" << meta.height << ", " << (bytes / 1024) << " KB) in " << (post - pre) << " ms.";

  return true;
}

#pragma endregion

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
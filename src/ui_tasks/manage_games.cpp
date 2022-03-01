//
// Copyright 2020 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <wmsdk.h>
#include <filesystem>
#include <SKIF.h>

#include <injection.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "../DirectXTex/DirectXTex.h"

#include <font_awesome.h>

#include <stores/Steam/apps_list.h>
#include <stores/Steam/asset_fetch.h>
#include <stores/GOG/gog_library.h>
#include <stores/SKIF/custom_library.h>

#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <concurrent_queue.h>

#include <stores/Steam/apps_ignore.h>

const int   SKIF_STEAM_APPID        = 1157970;
bool        SKIF_STEAM_OWNER        = false;
static bool clickedGameLaunch,
            clickedGameLaunchWoSK,
            clickedGalaxyLaunch,
            clickedGalaxyLaunchWoSK = false,
            openedGameContextMenu   = false;

PopupState ServiceMenu     = PopupState::Closed;

PopupState AddGamePopup    = PopupState::Closed;
PopupState RemoveGamePopup = PopupState::Closed;
PopupState ModifyGamePopup = PopupState::Closed;
PopupState ConfirmPopup    = PopupState::Closed;

std::string confirmPopupTitle;
std::string confirmPopupText;


extern int             SKIF_iStyle;
extern bool            SKIF_bLowBandwidthMode;
extern bool            SKIF_bDisableBorders;
extern float           SKIF_ImGui_GlobalDPIScale;
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern bool            SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);
CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

static std::wstring sshot_file = L"";

std::atomic<int> textureLoadQueueLength{ 0 };

int getTextureLoadQueuePos() {
  return textureLoadQueueLength.fetch_add(1, std::memory_order_relaxed) + 1;
}

HINSTANCE
SKIF_Util_ExplorePath (
  const std::wstring_view& path )
{
  //return
    //ShellExecuteW ( nullptr, L"EXPLORE",
      //path.data (), nullptr,
                    //nullptr, SW_SHOWNORMAL );

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"EXPLORE";
    sexi.lpFile       = path.data ();
    sexi.nShow        = SW_SHOWNORMAL;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

HINSTANCE
SKIF_Util_ExplorePath_Formatted (
  const wchar_t * const wszFmt,
                        ... )
{  static        thread_local
        wchar_t _thread_localPath
        [ 4UL * INTERNET_MAX_PATH_LENGTH ] =
        {                                };
  va_list       vArgs   =   nullptr;
  va_start    ( vArgs,  wszFmt    );
  wvnsprintfW ( _thread_localPath,
    ARRAYSIZE ( _thread_localPath ),
                        wszFmt,
                vArgs             );
  va_end      ( vArgs             );

  //return
    //ShellExecuteW (
      //nullptr, L"EXPLORE",
                //_thread_localPath,
      //nullptr,    nullptr,
             //SW_SHOWNORMAL
                  //);

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"EXPLORE";
    sexi.lpFile       = _thread_localPath;
    sexi.nShow        = SW_SHOWNORMAL;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

HINSTANCE
SKIF_Util_OpenURI (
  const std::wstring_view& path,
               DWORD       dwAction,
               LPCWSTR     verb,
               LPCWSTR     parameters)
{
  //return
    //ShellExecuteW ( nullptr, L"OPEN",
      //path.data (), nullptr,
                    //nullptr, dwAction );

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = verb;
    sexi.lpFile       = path.data ();
    sexi.lpParameters = parameters;
    sexi.nShow        = dwAction;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

HINSTANCE
SKIF_Util_OpenURI_Formatted (
          DWORD       dwAction,
  const wchar_t * const wszFmt,
                        ... )
{  static        thread_local
        wchar_t _thread_localPath
        [ 4UL * INTERNET_MAX_PATH_LENGTH ] =
        {                                };
  va_list       vArgs   =   nullptr;
  va_start    ( vArgs,  wszFmt    );
  wvnsprintfW ( _thread_localPath,
    ARRAYSIZE ( _thread_localPath ),
                        wszFmt,
                vArgs             );
  va_end      ( vArgs             );

  //return
    //ShellExecuteW ( nullptr,
      //L"OPEN",  _thread_localPath,
         //nullptr,   nullptr,   dwAction
                  //);

    SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = _thread_localPath;
    sexi.nShow        = dwAction;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

  if (ShellExecuteExW (&sexi))
    return sexi.hInstApp;

  return 0;
}

void
SKIF_Util_OpenURI_Threaded (
        const LPCWSTR path )
{
  _beginthreadex(nullptr,
                         0,
  [](LPVOID lpUser)->unsigned
  {
    LPCWSTR _path = (LPCWSTR)lpUser;

    CoInitializeEx (nullptr,
      COINIT_APARTMENTTHREADED |
      COINIT_DISABLE_OLE1DDE
    );

    SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = _path;
    sexi.nShow        = SW_SHOWNORMAL;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                        SEE_MASK_NOASYNC    | SEE_MASK_NOZONECHECKS;

    ShellExecuteExW (&sexi);

    _endthreadex(0);

    return 0;
  }, (LPVOID)path, NULL, NULL);
}

#include <patreon.png.h>
#include <sk_icon.jpg.h>
#include <sk_boxart.png.h>
#include <fsutil.h>
#include <stores/EGS/egs_library.h>
#include <atlimage.h>
#include <TlHelp32.h>

CComPtr <ID3D11Texture2D>          pPatTex2D;
CComPtr <ID3D11ShaderResourceView> pPatTexSRV;

enum class LibraryTexture
{
  Icon,
  Cover,
  Patreon
};

void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             vCoverUv0,
        ImVec2&                             vCoverUv1,
        app_record_s*                       pApp = nullptr)
{

  extern bool SKIF_SaveExtractExeIcon (std::wstring exePath, std::wstring targetPath);

  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  std::wstring load_str = L"\0",
               SKIFCustomPath,
               SteamCustomPath;

  bool succeeded   = false;
  bool customAsset = false;

  if (pApp != nullptr)
    appid = pApp->id;

  // SKIF
  if (       appid == SKIF_STEAM_APPID &&
      libTexToLoad != LibraryTexture::Patreon)
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\)", path_cache.specialk_userdata.path);

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
  }

  // SKIF Custom
  else if (pApp != nullptr && pApp->store == "SKIF")
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata.path, appid);

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
               SKIF_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath(pApp->id), SKIFCustomPath + L"-original.png"))
        load_str =                SKIFCustomPath + L"-original.png";
    }
  }

  // EGS
  else if (pApp != nullptr && pApp->store == "EGS")
  {
    std::wstring EGSAssetPath = SK_FormatStringW(LR"(%ws\Assets\EGS\%ws\)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
    SKIFCustomPath = std::wstring(EGSAssetPath);

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
               PathFileExistsW ((EGSAssetPath + L"OfferImageTall.jpg").c_str()))
        load_str =               EGSAssetPath + L"OfferImageTall.jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               SKIF_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath(pApp->id), EGSAssetPath + L"icon-original.png"))
        load_str =               SKIFCustomPath + L"-original.png";
    }
  }

  // GOG
  else if (pApp != nullptr && pApp->store == "GOG")
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)", path_cache.specialk_userdata.path, appid);

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
               SKIF_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath(pApp->id), SKIFCustomPath + L"-original.png"))
        load_str =             SKIFCustomPath + L"-original.png";
      else if (libTexToLoad == LibraryTexture::Icon)
      {
        load_str =             name;
      }

      else if (libTexToLoad == LibraryTexture::Cover)
      {
        extern std::wstring GOGGalaxy_UserID;
        load_str = SK_FormatStringW (LR"(C:\ProgramData\GOG.com\Galaxy\webcache\%ws\gog\%i\)", GOGGalaxy_UserID.c_str(), appid);

        HANDLE hFind = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATA ffd;

        hFind = FindFirstFile((load_str + name).c_str(), &ffd);

        if (INVALID_HANDLE_VALUE != hFind)
        {
          load_str += ffd.cFileName;
          FindClose(hFind);
        }
      }
    }
  }

  // STEAM
  else if (pApp != nullptr && pApp->store == "Steam")
  {
    static unsigned long SteamUserID = 0;

    if (SteamUserID == 0)
    {
      WCHAR                    szData [255] = { };
      DWORD   dwSize = sizeof (szData);
      PVOID   pvData =         szData;
      CRegKey hKey ((HKEY)0);

      if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey.m_hKey) == ERROR_SUCCESS)
      {
        if (RegGetValueW (hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
          SteamUserID = *(DWORD*)pvData;
      }
    }

    SKIFCustomPath  = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", path_cache.specialk_userdata.path, appid);
    SteamCustomPath = SK_FormatStringW (LR"(%ws\userdata\%i\config\grid\%i)", SK_GetSteamDir(), SteamUserID, appid);

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
      if      (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((SteamCustomPath + L"p.png").c_str()))
        load_str =               SteamCustomPath + L"p.png";
      else if (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((SteamCustomPath + L"p.jpg").c_str()))
        load_str =               SteamCustomPath + L"p.jpg";
      else
        load_str = name;
        //load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i%ws)", SK_GetSteamDir(), appid, name.c_str());
    }
  }

  if (pApp != nullptr)
  {
    if      (libTexToLoad == LibraryTexture::Cover)
      pApp->textures.isCustomCover = customAsset;
    else if (libTexToLoad == LibraryTexture::Icon)
      pApp->textures.isCustomIcon  = customAsset;
  }

  if (load_str != L"\0" &&
      SUCCEEDED(
        DirectX::LoadFromWICFile (
          load_str.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // WIC_FLAGS_IGNORE_SRGB solves some PNGs appearing too dark
              &meta, img
        )
      )
    )
  {
    succeeded = true;
  }

  else if (appid == SKIF_STEAM_APPID)
  {
    if (SUCCEEDED(
          DirectX::LoadFromWICMemory(
            (libTexToLoad == LibraryTexture::Icon) ?        sk_icon_jpg  : (libTexToLoad == LibraryTexture::Cover) ?        sk_boxart_png  :        patreon_png,
            (libTexToLoad == LibraryTexture::Icon) ? sizeof(sk_icon_jpg) : (libTexToLoad == LibraryTexture::Cover) ? sizeof(sk_boxart_png) : sizeof(patreon_png),
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
  if (pLibTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
    SKIF_ResourcesToFree.push (pLibTexSRV.p);
    pLibTexSRV.p = nullptr;
  }

  if (succeeded)
  {
    DirectX::ScratchImage* pImg   =
                                &img;
    DirectX::ScratchImage   converted_img;

    // Start aspect ratio
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
      //vCoverUv0.y = 1.f;
      //vCoverUv1.y = 1.f;
    }

    // Crop thinner aspect ratios by their height
    else if ((vecTex2D.x / vecTex2D.y) < (600.f / 900.f))
    {
      float newHeight = vecTex2D.y / vecTex2D.x * 600.0f;
      diff.y = (900.0f / newHeight);
      diff.y -= 1.0f;
      diff.y /= 2;
      
      //vCoverUv0.x = 1.f;
      //vCoverUv1.x = 1.f;
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

      // SRV is holding a reference, this is not needed anymore.
      pTex2D = nullptr;
    }
  }
};

/*
void
SKIF_GameManagement_ShowScreenshot (const std::wstring& filename)
{
  static CComPtr <ID3D11Texture2D>          pTex2D;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV;

  static DirectX::TexMetadata  meta = { };
  static DirectX::ScratchImage img  = { };

  if ( filename != sshot_file)
  {
    sshot_file = filename;

    if (PathFileExistsW (filename.c_str ()))
    {
      if (
        SUCCEEDED (
          DirectX::LoadFromWICFile (
            filename.c_str (),
              DirectX::WIC_FLAGS_FORCE_LINEAR,
                &meta, img
          )
        )
      )
      {
        DirectX::ScratchImage pm_img;

        DirectX::PremultiplyAlpha (*img.GetImage (0,0,0), DirectX::TEX_PMALPHA_DEFAULT, pm_img);

        auto pDevice =
          SKIF_D3D11_GetDevice ();

        if (! pDevice)
          return;

        pTex2D = nullptr;

        if (
          SUCCEEDED (
            DirectX::CreateTexture (
              pDevice,
                pm_img.GetImages (), pm_img.GetImageCount (),
                  meta, (ID3D11Resource **)&pTex2D.p
            )
          )
        )
        {
          D3D11_SHADER_RESOURCE_VIEW_DESC
            srv_desc                           = { };
            srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
            srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels       =  1;
            srv_desc.Texture2D.MostDetailedMip =  0;

          CComPtr <ID3D11ShaderResourceView>
            pOrigTexSRV (pTexSRV.p);
                         pTexSRV = nullptr;

          if (   pTex2D.p == nullptr ||
            FAILED (
              pDevice->CreateShaderResourceView (
                 pTex2D.p, &srv_desc,
                &pTexSRV.p
              )
            )
          )
          {
            pTexSRV = pOrigTexSRV;
          }

          // SRV is holding a reference, this is not needed anymore.
          pTex2D = nullptr;
        }
      }
    }
  };

  if (sshot_file.empty ())
    pTexSRV = nullptr;

  if (pTexSRV.p != nullptr)
  {
    // Compensate for HDR rescale if needed
    extern BOOL  SKIF_IsHDR           (void);
    extern FLOAT SKIF_GetHDRWhiteLuma (void);

    //float inverse_hdr_scale =
    //  SKIF_IsHDR () ? 1.0f / (80.0f / SKIF_GetHDRWhiteLuma ())
    //                : 1.0f;

    ImGui::SetNextWindowSize (ImVec2 ((float)meta.width, (float)meta.height));
  //ImGui::SetNextWindowPos  (ImVec2 (0.0f, 0.0f));
    ImGui::Begin             ("Screenshot View");
  //ImGui::BeginChild        ("ScreenshotImage", ImVec2 (0, 0), false, ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NavFlattened);
    ImGui::Image ((ImTextureID)pTexSRV.p, ImVec2 ((float)meta.width, (float)meta.height),
                  ImVec2 (0, 0), ImVec2 (1, 1));
  //ImGui::EndChild   ();
    ImGui::End        ();
  }
}
*/

using app_entry_t =
        std::pair < std::string,
                    app_record_s >;
using   app_ptr_t = app_entry_t const*;


// define character size
#define CHAR_SIZE 128

// A Class representing a Trie node
class Trie
{
public:
  bool  isLeaf                = false;
  Trie* character [CHAR_SIZE] = {   };

  // Constructor
  Trie (void)
  {
    this->isLeaf = false;

    for (int i = 0; i < CHAR_SIZE; i++)
      this->character [i] = nullptr;
  }

  void insert       (        const std::string&);
  bool deletion     (Trie*&, const std::string&);
  bool search       (        const std::string&);
  bool haveChildren (Trie const*);
};

// Iterative function to insert a key in the Trie
void
Trie::insert (const std::string& key)
{
  // start from root node
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // create a new node if path doesn't exists
    if (curr->character [key [i]] == nullptr)
        curr->character [key [i]]  = new Trie ();

    // go to next node
    curr = curr->character [key [i]];
  }

  // mark current node as leaf
  curr->isLeaf = true;
}

// Iterative function to search a key in Trie. It returns true
// if the key is found in the Trie, else it returns false
bool Trie::search (const std::string& key)
{
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // go to next node
    curr = curr->character [key [i]];

    // if string is invalid (reached end of path in Trie)
    if (curr == nullptr)
      return false;
  }

  // if current node is a leaf and we have reached the
  // end of the string, return true
  return curr->isLeaf;
}

// returns true if given node has any children
bool Trie::haveChildren (Trie const* curr)
{
  for (int i = 0; i < CHAR_SIZE; i++)
    if (curr->character [i])
      return true;  // child found

  return false;
}

// Recursive function to delete a key in the Trie
bool Trie::deletion (Trie*& curr, const std::string& key)
{
  // return if Trie is empty
  if (curr == nullptr)
    return false;

  // if we have not reached the end of the key
  if (key.length ())
  {
    // recur for the node corresponding to next character in the key
    // and if it returns true, delete current node (if it is non-leaf)

    if (        curr                      != nullptr       &&
                curr->character [key [0]] != nullptr       &&
      deletion (curr->character [key [0]], key.substr (1)) &&
                curr->isLeaf == false)
    {
      if (! haveChildren (curr))
      {
        delete curr;
        curr = nullptr;
        return true;
      }

      else {
        return false;
      }
    }
  }

  // if we have reached the end of the key
  if (key.length () == 0 && curr->isLeaf)
  {
    // if current node is a leaf node and don't have any children
    if (! haveChildren (curr))
    {
      // delete current node
      delete curr;
      curr = nullptr;

      // delete non-leaf parent nodes
      return true;
    }

    // if current node is a leaf node and have children
    else
    {
      // mark current node as non-leaf node (DON'T DELETE IT)
      curr->isLeaf = false;

      // don't delete its parent nodes
      return false;
    }
  }

  return false;
}

struct {
  uint32_t    id = 0;
  std::string store;
} static manual_selection;

Trie labels;

void
SKIF_GameManagement_DrawTab (void)
{
  /*
  if (! sshot_file.empty ())
  {
    SKIF_GameManagement_ShowScreenshot (sshot_file);
  }
  */

  static CComPtr <ID3D11Texture2D>          pTex2D;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV;
  //static ImVec2                             vecTex2D;

  static ImVec2 vecCoverUv0 = ImVec2 (0, 0), 
                vecCoverUv1 = ImVec2 (1, 1);

  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };

  static
    std::wstring appinfo_path (
      SK_GetSteamDir ()
    );

  SK_RunOnce (
    appinfo_path.append (
      LR"(\appcache\appinfo.vdf)"
    )
  );

  SK_RunOnce (
    appinfo =
      std::make_unique <skValveDataFile> (
        appinfo_path
      )
  );

  //ImGui::DockSpace(ImGui::GetID("Foobar?!"), ImVec2(600, 900), ImGuiDockNodeFlags_KeepAliveOnly | ImGuiDockNodeFlags_NoResize);

  auto& io =
    ImGui::GetIO ();

  static float max_app_name_len = 640.0f / 2.0f;

  /* What does this even do? Nothing?
  std::vector <AppId_t>
    SK_Steam_GetInstalledAppIDs (void);
  */

  static std::vector <AppId_t> appids;

  extern bool SKIF_bDisableSteamLibrary;

  auto PopulateAppRecords = [&](void) ->
    std::vector <std::pair <std::string, app_record_s>>
  { std::vector <std::pair <std::string, app_record_s>> ret;

    std::set <uint32_t> unique_apps;

    if ( SKIF_bDisableSteamLibrary )
      return ret;

    appids =
      SK_Steam_GetInstalledAppIDs ();

    for ( auto app : appids )
    {
      // Skip Steamworks Common Redists
      if (app == 228980) continue;

      // Skip IDs related to apps, DLCs, music, and tools (including Special K for now)
      if (std::find(std::begin(steam_apps_ignorable), std::end(steam_apps_ignorable), app) != std::end(steam_apps_ignorable)) continue;

      if (unique_apps.emplace (app).second)
      {
        // Opening the manifests to read the names is a
        //   lengthy operation, so defer names and icons
        ret.emplace_back (
          "Loading...", app
        );
      }
    }

    return ret;
  };

  static volatile LONG icon_thread  = 1;
  static volatile LONG need_sort    = 0;
  bool                 sort_changed = false;

  if (InterlockedCompareExchange (&need_sort, 0, 1))
  {
    std::sort ( apps.begin (),
                apps.end   (),
      []( const app_entry_t& a,
          const app_entry_t& b ) -> int
      {
        return a.second.names.all_upper.compare(
               b.second.names.all_upper
        ) < 0;
      }
    );

    sort_changed = true;
  }

  extern uint32_t SKIF_iLastSelected;

  static bool     update         = true;

  struct {
    uint32_t    appid = SKIF_STEAM_APPID;
    std::string store = "Steam";

    void reset()
    {
      appid = SKIF_STEAM_APPID;
      store = "Steam";
    }
  } static selection;

  static bool     populated      = false;

  extern bool RepopulateGames;
  extern bool SKIF_bDisableEGSLibrary;
  extern bool SKIF_bDisableGOGLibrary;

  if (! SKIF_bDisableEGSLibrary && skif_egs_dir_watch.isSignaled ( ))
    RepopulateGames = true;

  if (RepopulateGames)
  {
    RepopulateGames = false;

    // Clear cached lists
    apps.clear   ();
    appids.clear ();

    // Reset selection to Special K, but only if set to something else than -1
    if (selection.appid != 0)
      selection.reset();

    update    = true;

    populated = false;
  }

  if (! populated)
  {
    InterlockedExchange (&icon_thread, 1);

    apps      = PopulateAppRecords ();

    for (auto& app : apps)
      if (app.second.id == SKIF_STEAM_APPID)
        SKIF_STEAM_OWNER = true;

    if ( ! SKIF_STEAM_OWNER )
    {
      app_record_s SKIF_record (SKIF_STEAM_APPID);

      SKIF_record.id              = SKIF_STEAM_APPID;
      SKIF_record.names.normal    = "Special K";
      SKIF_record.names.all_upper = "SPECIAL K";
      SKIF_record.install_dir     = std::filesystem::current_path ();
      SKIF_record.store           = "Steam";
      SKIF_record.ImGuiLabelAndID = SK_FormatString("%s###%s%i", SKIF_record.names.normal.c_str(), SKIF_record.store.c_str(), SKIF_record.id);

      std::pair <std::string, app_record_s>
        SKIF ( "Special K", SKIF_record );

      apps.emplace_back (SKIF);
    }

    // Load GOG titles from registry
    if (! SKIF_bDisableGOGLibrary)
      SKIF_GOG_GetInstalledAppIDs (&apps);

    // Load EGS titles from disk
    if (! SKIF_bDisableEGSLibrary)
      SKIF_EGS_GetInstalledAppIDs (&apps);

    // Load custom SKIF titles from registry
    SKIF_GetCustomAppIDs (&apps);

    // Set to last selected if it can be found
    if (selection.appid == SKIF_STEAM_APPID)
    {
      for (auto& app : apps)
        if (app.second.id == SKIF_iLastSelected)
        {
          selection.appid = SKIF_iLastSelected;
          selection.store = app.second.store;
          update = true;
        }
    }

    // We're going to stream icons in asynchronously on this thread
    _beginthread ([](void*)->void
    {
      CoInitializeEx (nullptr, 0x0);

      ImVec2 dontCare1, dontCare2;
      SK_RunOnce (
        LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, pPatTexSRV, L"(patreon.png)", dontCare1, dontCare2)
      );

      for ( auto& app : apps )
      {
        // Special handling for non-Steam owners of Special K / SKIF
        if ( app.second.id == SKIF_STEAM_APPID )
          app.first = "Special K";

        // Regular handling for the remaining Steam games
        else if (app.second.store == "Steam") {
          app.first.clear ();

          app.second._status.refresh (&app.second);
        }

        // Only bother opening the application manifest
        //   and looking for a name if the client claims
        //     the app is installed.
        if (app.second._status.installed)
        {
          if (! app.second.names.normal.empty ())
          {
            app.first = app.second.names.normal;
          }

          // Some games have an install state but no name,
          //   for those we have to consult the app manifest
          else if (app.second.store == "Steam")
          {
            app.first =
              SK_UseManifestToGetAppName (
                           app.second.id );
          }

          // Strip null terminators
          //app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

          // Strip game names from special symbols and null terminators
          const char* chars = (const char *)u8"©®™";
          for (unsigned int i = 0; i < strlen(chars); ++i)
            app.first.erase(std::remove(app.first.begin(), app.first.end(), chars[i]), app.first.end());
          
          app.second.ImGuiLabelAndID = SK_FormatString("%s###%s%i", app.first.c_str(), app.second.store.c_str(), app.second.id);
        }

        // Corrupted app manifest / not known to Steam client; SKIP!
        if (app.first.empty ())
        {
          app.second.id = 0;
          continue;
        }

        // Check if install folder exists (but not for SKIF)
        if (app.second.id != SKIF_STEAM_APPID)
        {
          std::wstring install_dir;

          if (app.second.store == "Steam")
            install_dir = SK_UseManifestToGetInstallDir(app.second.id);
          else
            install_dir = app.second.install_dir;
          
          if (! PathFileExists(install_dir.c_str()))
          {
            app.second.id = 0;
            continue;
          }
        }

        if (app.second._status.installed || app.second.id == SKIF_STEAM_APPID)
        {
          std::string all_upper;

          for (const char c : app.first)
          {
            if (! ( isalnum (c) || isspace (c)))
              continue;

            all_upper += (char)toupper (c);
          }

          static const
            std::string toSkip [] =
            {
              std::string ("A "),
              std::string ("THE ")
            };

          for ( auto& skip_ : toSkip )
          {
            if (all_upper.find (skip_) == 0)
            {
              all_upper =
                all_upper.substr (
                  skip_.length ()
                );
              break;
            }
          }

          std::string trie_builder;

          for ( const char c : all_upper)
          {
            trie_builder += c;

            labels.insert (trie_builder);
          }

          app.second.names.all_upper = trie_builder;
          app.second.names.normal    = app.first;
        }

        std::wstring load_str;
        
        if (app.second.id == SKIF_STEAM_APPID) // SKIF
          load_str = L"_icon.jpg";
        else  if (app.second.store == "SKIF")  // SKIF Custom
          load_str = L"icon";
        else  if (app.second.store == "EGS")   // EGS
          load_str = L"icon";
        else  if (app.second.store == "GOG")   // GOG
          load_str = app.second.install_dir + L"\\goggame-" + std::to_wstring(app.second.id) + L".ico";
        else if (app.second.store == "Steam")  // STEAM
          load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i_icon.jpg)", SK_GetSteamDir(), app.second.id); //L"_icon.jpg"

        LoadLibraryTexture ( LibraryTexture::Icon,
                               app.second.id,
                                 app.second.textures.icon,
                                   load_str,
                                     dontCare1,
                                       dontCare2,
                                         &app.second );

        static auto *pFont =
          ImGui::GetFont ();

        max_app_name_len =
          std::max ( max_app_name_len,
                       pFont->CalcTextSizeA (1.0f, FLT_MAX, 0.0f,
                         app.first.c_str (),
                StrStrA (app.first.c_str (), "##")
                       ).x
          );
      }

      InterlockedExchange (&icon_thread, 0);
      InterlockedExchange (&need_sort, 1);
    }, 0x0, NULL);

    populated = true;
  }

  if (! update)
  {
    update =
      SKIF_LibraryAssets_CheckForUpdates (true);
  }

  extern int   SKIF_iDimCovers;
  static int   tmpSKIF_iDimCovers = SKIF_iDimCovers;
  const  float fTintMin = 0.75f;
  static float fTint = (SKIF_iDimCovers == 0) ? 1.0f : fTintMin;
  
  static
    app_record_s* pApp = nullptr;

  for (auto& app : apps)
    if (app.second.id == selection.appid && app.second.store == selection.store)
      pApp = &app.second;

  // Apply changes when the selected game changes
  if (update)
  {
    fTint = (SKIF_iDimCovers == 0) ? 1.0f : fTintMin;
  }

  // Apply changes when the SKIF_iDimCovers var have been changed in the Settings tab
  else if (tmpSKIF_iDimCovers != SKIF_iDimCovers)
  {
    fTint = (SKIF_iDimCovers == 0) ? 1.0f : fTintMin;

    tmpSKIF_iDimCovers = SKIF_iDimCovers;
  }

  ImGui::BeginGroup    (                                                  );
  float fX =
  ImGui::GetCursorPosX (                                                  );

  // Display cover image
  SKIF_ImGui_OptImage  (pTexSRV.p,
                                                    ImVec2 (600.0F * SKIF_ImGui_GlobalDPIScale,
                                                            900.0F * SKIF_ImGui_GlobalDPIScale),
                                                    vecCoverUv0, // Top Left coordinates
                                                    vecCoverUv1, // Bottom Right coordinates
                                                    (selection.appid == SKIF_STEAM_APPID)
                                                    ? ImVec4 ( 1.0f,  1.0f,  1.0f, 1.0f) // Tint for Special K (always full strength)
                                                    : ImVec4 (fTint, fTint, fTint, 1.0f), // Tint for other games (transition up and down as mouse is hovered)
                                  (! SKIF_bDisableBorders) ? ImGui::GetStyleColorVec4 (ImGuiCol_Border) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f) // Border
  );

  if (SKIF_iDimCovers == 2)
  {
    if (ImGui::IsItemHovered() && fTint < 1.0f)
    {
      fTint = fTint + 0.01f;
    }
    else if (fTint > fTintMin)
    {
      fTint = fTint - 0.01f;
    }
  }

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ImGui::OpenPopup ("CoverMenu");

  if (ImGui::BeginPopup ("CoverMenu"))
  {
    //static
     // app_record_s* pApp = nullptr;

    //for (auto& app : apps)
    //  if (app.second.id == appid)
    //    pApp = &app.second;

    if (pApp != nullptr)
    {
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE)       .x, ImGui::GetTextLineHeight()));
      if (pApp->textures.isCustomCover)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UNDO_ALT)       .x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_EXTERNAL_LINK_ALT).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Set Custom Artwork",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png" }, 1, _FILEOPENDIALOGOPTIONS::FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata.path);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata.path, pApp->id);

          if (targetPath != L"")
          {
            std::filesystem::create_directories (targetPath);
            targetPath += L"cover";

            if (ext == L".jpg")
              DeleteFile((targetPath + L".png").c_str());

            CopyFile(pwszFilePath, (targetPath + ext).c_str(), false);

            update = true;
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }

      if (pApp->textures.isCustomCover)
      {
        if (ImGui::Selectable ("Clear Custom Artwork", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata.path);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata.path, pApp->id);

          if (PathFileExists (targetPath.c_str()))
          {
            targetPath += L"cover";

            bool d1 = DeleteFile ((targetPath + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + L".jpg").c_str());

            // If any file was removed
            if (d1 || d2)
              update  = true;
          }
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
        }
      }

      ImGui::Separator  (  );

      // Strip (recently added) from the game name
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      std::string linkGridDB = (pApp->store == "Steam")
                             ? SK_FormatString("https://www.steamgriddb.com/steam/%lu/grids", pApp->id)
                             : SK_FormatString("https://www.steamgriddb.com/search/grids?term=%s", name.c_str());

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(linkGridDB).c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (linkGridDB);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (SKIF_iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->textures.isCustomCover)
        ImGui::TextColored (
          (SKIF_iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_UNDO_ALT
                              );

      ImGui::Separator  (  );

      ImGui::TextColored (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_EXTERNAL_LINK_ALT
                            );

    }

    ImGui::EndPopup   (  );
  }

  float fY =
  ImGui::GetCursorPosY (                                                  );

  ImGui::EndGroup             ( );
  ImGui::SameLine             ( );
  
  float fZ =
  ImGui::GetCursorPosX (                                                  );

  static bool loadTexture = false;
  if (update)
  {
    //SKIF_GameManagement_ShowScreenshot (L"");
    loadTexture = true;
    update      = false;
  }
    
  if (loadTexture && populated && ! InterlockedCompareExchange (&icon_thread, 0, 0)) // && ImGui::GetFrameCount() > 2)
  { // Load cover first on third frame (>2) to fix one copy leaking of the cover -- CRASHES IN WIN8.1 VMware VIRTUAL MACHINE
    loadTexture = false;

    if ( appinfo != nullptr && pApp->store == "Steam")
    {
      skValveDataFile::appinfo_s *pAppInfo =
        appinfo->getAppInfo ( pApp->id, nullptr );

      DBG_UNREFERENCED_LOCAL_VARIABLE (pAppInfo);
    }

    // We're going to stream the cover in asynchronously on this thread
    _beginthread([](void*)->void
    {
      CoInitializeEx(nullptr, 0x0);

      static int queuePos = getTextureLoadQueuePos();
      static ImVec2 _vecCoverUv0(vecCoverUv0);
      static ImVec2 _vecCoverUv1(vecCoverUv1);
      static CComPtr <ID3D11ShaderResourceView> _pTexSRV (pTexSRV.p);

      std::wstring load_str;

      // SKIF
      if (pApp->id == SKIF_STEAM_APPID)
      {
        load_str = L"_library_600x900_x2.jpg";
      }

      // SKIF Custom
      else if (pApp->store == "SKIF")
      {
        load_str = L"cover";
      }

      // GOG
      else if (pApp->store == "GOG")
      {
        load_str = L"*_glx_vertical_cover.webp";
      }

      // EGS
      else if (pApp->store == "EGS")
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\OfferImageTall.jpg)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_EGS_IdentifyAssetNew (pApp->EGS_CatalogNamespace, pApp->EGS_CatalogItemId, pApp->EGS_AppName, pApp->EGS_DisplayName);

        //
        } else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! SKIF_bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_EGS_IdentifyAssetNew (pApp->EGS_CatalogNamespace, pApp->EGS_CatalogItemId, pApp->EGS_AppName, pApp->EGS_DisplayName);
            }
          }
        }
      }

      // STEAM
      else if (pApp->store == "Steam")
      {
        std::wstring load_str_2x (
          SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", path_cache.specialk_userdata.path, pApp->id)
        );

        std::filesystem::create_directories (load_str_2x);

        load_str_2x += L"library_600x900_x2.jpg";
      
        load_str = SK_GetSteamDir ();

        load_str   += LR"(/appcache/librarycache/)" +
          std::to_wstring (pApp->id)                +
                                  L"_library_600x900.jpg";

        std::wstring load_str_final = load_str;
        //std::wstring load_str_final = L"_library_600x900.jpg";

        // If 600x900 exists but 600x900_x2 cannot be found 
        if (   PathFileExistsW (load_str.   c_str ()) &&
              ! PathFileExistsW (load_str_2x.c_str ()) )
        {
          // Load the metadata from 600x900, but only if low bandwidth mode is not enabled
          if ( ! SKIF_bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 300x450, which indicates a real cover,
            //   download the real 600x900 cover and store it in _x2
            if (meta.width  == 300 &&
                meta.height == 450)
            {
              SKIF_HTTP_GetAppLibImg (pApp->id, load_str_2x);
              load_str_final = load_str_2x;
              //load_str_final = L"_library_600x900_x2.jpg";
            }
          }
        }

        // If 600x900_x2 exists, check the last modified time stamps
        else {
          WIN32_FILE_ATTRIBUTE_DATA faX1, faX2;

          // ... but only if low bandwidth mode is disabled
          if (! SKIF_bLowBandwidthMode &&
              GetFileAttributesEx (load_str   .c_str(), GetFileExInfoStandard, &faX1) &&
              GetFileAttributesEx (load_str_2x.c_str(), GetFileExInfoStandard, &faX2))
          {
            // If 600x900 has been edited after 600_900_x2,
            //   download new copy of the 600_900_x2 cover
            if (CompareFileTime (&faX1.ftLastWriteTime, &faX2.ftLastWriteTime) == 1)
            {
              DeleteFile (load_str_2x.c_str());
              SKIF_HTTP_GetAppLibImg (pApp->id, load_str_2x);
            }
          }

          load_str_final = load_str_2x;
        }

        load_str = load_str_final;
      }
    
      LoadLibraryTexture ( LibraryTexture::Cover,
                              pApp->id,
                                _pTexSRV,
                                  load_str,
                                    _vecCoverUv0,
                                      _vecCoverUv1,
                                        pApp);


      if (textureLoadQueueLength == queuePos)
      {
        vecCoverUv0 = _vecCoverUv0;
        vecCoverUv1 = _vecCoverUv1;
        pTexSRV     = _pTexSRV;
      }
      else if (_pTexSRV.p != nullptr)
      {
        extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
        SKIF_ResourcesToFree.push(_pTexSRV.p);
        _pTexSRV.p = nullptr;
      }

      //InterlockedExchange(&cover_thread, 0);
    }, 0x0, NULL);
  }

    /*
    static
      app_record_s* pApp = nullptr;

    for (auto& app : apps)
      if (app.second.id == appid)
        pApp = &app.second;

    std::wstring load_str;

    // SKIF
    // SKIF Custom
    // GOG
    if ( appid == SKIF_STEAM_APPID ||
         pApp->store == "SKIF"     ||
         pApp->store == "GOG" )
    {
      load_str = L"_library_600x900_x2.jpg";

      if (pApp->store == "SKIF" || pApp->store == "EGS")
        load_str = L"cover";
      else if (pApp->store == "GOG")
        load_str = L"*_glx_vertical_cover.webp";
    }

    // EGS
    else if ( pApp->store == "EGS" )
    {
      load_str = 
        SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\OfferImageTall.jpg)", path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());

      std::wstring load_str_2x
                  (load_str);

      if ( ! PathFileExistsW (load_str.   c_str ()) )
      {
        //SKIF_EGS_IdentifyAsset (pApp->EGS_CatalogNamespace, pApp->EGS_CatalogItemId, pApp->EGS_AppName, pApp->EGS_DisplayName);
        SKIF_EGS_IdentifyAssetNew (pApp->EGS_CatalogNamespace, pApp->EGS_CatalogItemId, pApp->EGS_AppName, pApp->EGS_DisplayName);
      }
    }

    // STEAM
    else if (pApp->store == "Steam")
    {

      if ( appinfo != nullptr )
      {
        skValveDataFile::appinfo_s *pAppInfo =
          appinfo->getAppInfo ( appid, nullptr );

        DBG_UNREFERENCED_LOCAL_VARIABLE (pAppInfo);
      }

      std::wstring load_str_2x (
        SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", path_cache.specialk_userdata.path, appid)
      );

      std::filesystem::create_directories (load_str_2x);

      load_str_2x += L"library_600x900_x2.jpg";
      
      load_str = SK_GetSteamDir ();

      load_str   += LR"(/appcache/librarycache/)" +
        std::to_wstring (appid)                   +
                                L"_library_600x900.jpg";

      std::wstring load_str_final = load_str;
      //std::wstring load_str_final = L"_library_600x900.jpg";

      // If 600x900 exists but 600x900_x2 cannot be found
      if (   PathFileExistsW (load_str.   c_str ()) &&
         ( ! PathFileExistsW (load_str_2x.c_str ()) ) )
      {
        // Load the metadata from 600x900
        if (
          SUCCEEDED (
          DirectX::GetMetadataFromWICFile (
            load_str.c_str (),
              DirectX::WIC_FLAGS_FILTER_POINT,
                meta
            )
          )
        )
        {
          // If the image is in reality 300x450, which indicates a real cover,
          //   download the real 600x900 cover and store it in _x2
          if (meta.width  == 300 &&
              meta.height == 450)
          {
            SKIF_HTTP_GetAppLibImg (appid, load_str_2x);
            load_str_final = load_str_2x;
            //load_str_final = L"_library_600x900_x2.jpg";
          }
        }
      }

      // If 600x900_x2 exists, check the last modified time stamps
      else {
        WIN32_FILE_ATTRIBUTE_DATA faX1, faX2;

        if (GetFileAttributesEx (load_str   .c_str(), GetFileExInfoStandard, &faX1) &&
            GetFileAttributesEx (load_str_2x.c_str(), GetFileExInfoStandard, &faX2))
        {
          // If 600x900 has been edited after 600_900_x2,
          //   download new copy of the 600_900_x2 cover
          if (CompareFileTime (&faX1.ftLastWriteTime, &faX2.ftLastWriteTime) == 1)
          {
            DeleteFile (load_str_2x.c_str());
            SKIF_HTTP_GetAppLibImg (appid, load_str_2x);
          }
        }

        load_str_final = load_str_2x;
        //load_str_final = L"_library_600x900_x2.jpg";
      }

      load_str = load_str_final;
    }
    
    LoadLibraryTexture ( LibraryTexture::Cover,
                            appid,
                              pTexSRV,
                                load_str,
                                  pApp );
    */

    // Reset variables
    /*
    vecTex2D    = ImVec2 (600.f, 900.f);
    vecCoverUv0 = ImVec2 (0.f, 0.f); // Top left corner
    vecCoverUv1 = ImVec2 (1.f, 1.f); // Bottom right corner

    // Update vecTex2D with the size of the cover
    if (pTexSRV.p != nullptr)
    {
      pTexSRV->GetResource(reinterpret_cast<ID3D11Resource**>(&pTex2D.p));

      if (pTex2D != nullptr)
      {
        D3D11_TEXTURE2D_DESC desc;
        pTex2D->GetDesc(&desc);
        vecTex2D.x = static_cast<float>(desc.Width);
        vecTex2D.y = static_cast<float>(desc.Height);

        pTex2D.Release();

        ImVec2 diff = ImVec2(0.0f, 0.0f);

        // Crop wider aspect ratios by their width
        if ((vecTex2D.x / vecTex2D.y) > (600.f / 900.f))
        {
          float newWidth = vecTex2D.x / vecTex2D.y * 900.0f;
          diff.x = (600.0f / newWidth);
          diff.x -= 1.0f;
          diff.x /= 2;

          vecCoverUv0.x = 0.f - diff.x;
          vecCoverUv1.x = 1.f + diff.x;
        }

        // Crop thinner aspect ratios by their height
        else if ((vecTex2D.x / vecTex2D.y) < (600.f / 900.f))
        {
          float newHeight = vecTex2D.y / vecTex2D.x * 600.0f;
          diff.y = (900.0f / newHeight);
          diff.y -= 1.0f;
          diff.y /= 2;

          vecCoverUv0.y = 0.f - diff.y;
          vecCoverUv1.y = 1.f + diff.y;
        }
      }
    }
    */
  //}

  /*
  float fTestScale    = SKIF_ImGui_GlobalDPIScale,
        fScrollbar    = ImGui::GetStyle ().ScrollbarSize,
        fFrameWidth   = ImGui::GetStyle ().FramePadding.x * 4.0f,
        fSpacing      = ImGui::GetStyle ().ItemSpacing.x  * 4.0f;
        fDecorations  = (fFrameWidth + fSpacing + fScrollbar);
      //fFrameHeight  = ImGui::GetStyle ().FramePadding.y * 2.0f,
      //fSpaceHeight  = ImGui::GetStyle ().ItemSpacing.y  * 2.0f,
        fInjectWidth =
         ( sk_global_ctl_x  + fDecorations - fScrollbar * 2.0f ),
        fLongestLabel =
         ( 32 + max_app_name_len + fDecorations );
  */

// AppListInset1
#define _WIDTH (414.0f * SKIF_ImGui_GlobalDPIScale) // std::max ( fInjectWidth * fTestScale, std::min ( 640.0f * fTestScale, fLongestLabel * fTestScale ) )
  //_WIDTH  (640/2)
#define _HEIGHT (620.0f * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x - 2.0f) //(float)_WIDTH / (fAspect)
  //_HEIGHT (360/2)

// AppListInset2
#define _WIDTH2  (414.0f * SKIF_ImGui_GlobalDPIScale) //((float)_WIDTH)
#define _HEIGHT2 (280.0f * SKIF_ImGui_GlobalDPIScale) // (900.0f * SKIF_ImGui_GlobalDPIScale/(21.0f/9.0f)/2.0f + 88.0f /*(float)_WIDTH / (21.0f/9.0f) + fFrameHeight + fSpaceHeight * 2.0f*/)

  ImGui::BeginGroup ();

  auto _HandleKeyboardInput = [&](void)
  {
          auto& duration     = io.KeysDownDuration;
           bool bText        = false;
    static char test_ [1024] = {      };
           char out   [2]    = { 0, 0 };

    auto _Append = [&](char c) {
      out [0] = c; StrCatA (test_, out);
      bText   = true;
    };

    static auto
      constexpr _text_chars =
        { 'A','B','C','D','E','F','G','H',
          'I','J','K','L','M','N','O','P',
          'Q','R','S','T','U','V','W','X',
          'Y','Z','0','1','2','3','4','5',
          '6','7','8','9',' ','-',':','.' };

    for ( auto c : _text_chars )
    {
      if (duration [c] == 0.0f)
      {
        _Append (c);
      }
    }

    const  DWORD dwTimeout    = 850UL; // 425UL
    static DWORD dwLastUpdate = SKIF_timeGetTime ();

    struct {
      std::string text = "";
      std::string store;
      uint32_t    app_id = 0;
    } static result;

    if (bText)
    {
      dwLastUpdate = SKIF_timeGetTime ();

      if (labels.search (test_))
      {
        for (auto& app : apps)
        {
          if (app.second.names.all_upper.find (test_) == 0)
          {
            result.text   = app.second.names.normal;
            result.store  = app.second.store;
            result.app_id = app.second.id;

            break;
          }
        }
      }

      else
      {
        strncpy (test_, result.text.c_str (), 1023);
      }
    }

    if (! result.text.empty ())
    {
      //extern std::string SKIF_StatusBarText;
      //extern std::string SKIF_StatusBarHelp;
      //extern int          WindowsCursorSize;

      size_t len =
        strlen (test_);

      //SKIF_StatusBarText = result.text.substr (0, len);
      //SKIF_StatusBarHelp = result.text.substr (len, result.text.length () - len);

      std::string strText = result.text.substr(0, len),
                  strHelp = result.text.substr (len, result.text.length () - len);

      ImGui::OpenPopup("KeyboardHint");

      //ImVec2 cursorPos   = io.MousePos;
      //int    cursorScale = WindowsCursorSize;

      //ImGui::SetNextWindowPos (
      //  ImVec2 ( cursorPos.x + 16      + 4 * (cursorScale - 1),
      //           cursorPos.y + 8 /* 16 + 4 * (cursorScale - 1) */ )
      //);

      //ImGui::SetNextWindowSize (ImVec2 (0.0f, 0.0f));
      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindow()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

      if (ImGui::BeginPopupModal("KeyboardHint", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
      {
        ImGui::TextColored ( ImColor::HSV(0.0f, 0.0f, 0.75f), // ImColor(53, 255, 3)
                                "%s", strText.c_str ()
        );

        if (! strHelp.empty ())
        {
          ImGui::SameLine ();
          ImGui::SetCursorPosX (
            ImGui::GetCursorPosX () -
            ImGui::GetStyle      ().ItemSpacing.x
          );
          ImGui::TextDisabled ("%s", strHelp.c_str ());
        }

        ImGui::EndPopup ( );
      }
    }

    if (                       dwLastUpdate != MAXDWORD &&
         SKIF_timeGetTime () - dwLastUpdate >
                               dwTimeout )
    {
      if (result.app_id != 0)
      {
        *test_           = '\0';
        dwLastUpdate     = MAXDWORD;
        if (result.app_id != pApp->id || 
            result.store  != pApp->store)
        {
          manual_selection.id    = result.app_id;
          manual_selection.store = result.store;
        }
        result.app_id    = 0;
        result.store.clear ();
        result.text .clear ();
      }
    }
  };

  if (AddGamePopup    == PopupState::Closed &&
      ModifyGamePopup == PopupState::Closed &&
      RemoveGamePopup == PopupState::Closed &&
      ! io.KeyCtrl)
    _HandleKeyboardInput ();

  auto _PrintInjectionSummary = [&](app_record_s* pTargetApp) ->
  float
  {
    if ( pTargetApp != nullptr && pTargetApp->id != SKIF_STEAM_APPID )
    {
      struct summary_cache_s {
        struct {
          std::string   type;
          struct {
            std::string text;
            ImColor     color;
            ImColor     color_hover;
          } status;
          std::string   hover_text;
        } injection;
        std::string config_repo;
        struct {
          std::string shorthand; // Converted to utf-8 from utf-16
          std::string root_dir;  // Converted to utf-8 from utf-16
          std::string full_path; // Converted to utf-8 from utf-16
        } config;
        struct {
          std::string shorthand; // Converted to utf-8 from utf-16
          std::string version;   // Converted to utf-8 from utf-16
          std::string full_path; // Converted to utf-8 from utf-16
        } dll;
        AppId_t     app_id   = 0;
        DWORD       running  = 0;
        bool        service  = false;
        bool        autostop = false;
      } static cache;

      if (         cache.service  != _inject.bCurrentState  ||
                   cache.running  != pTargetApp->_status.running ||
                   cache.autostop != _inject.bAckInj
         )
      {
        cache.app_id = 0;
      }

      if (pTargetApp->id != cache.app_id)
      {
        cache.app_id   = pTargetApp->id;
        cache.running  = pTargetApp->_status.running;
        cache.autostop = _inject.bAckInj;

        cache.service  = (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
                         (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::SixtyFour &&  _inject.pid64) ||
                         (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::Unknown   && (_inject.pid32  &&
                                                                                                             _inject.pid64));

        sk_install_state_s& sk_install =
          pTargetApp->specialk.injection;

        wchar_t     wszDLLPath [MAX_PATH];
        wcsncpy_s ( wszDLLPath, MAX_PATH,
                      sk_install.injection.dll_path.c_str (),
                              _TRUNCATE );

        cache.dll.full_path = SK_WideCharToUTF8 (wszDLLPath);

        PathStripPathW (                         wszDLLPath);
        cache.dll.shorthand = SK_WideCharToUTF8 (wszDLLPath);
        cache.dll.version   = SK_WideCharToUTF8 (sk_install.injection.dll_ver);

        wchar_t     wszConfigPath [MAX_PATH];
        wcsncpy_s ( wszConfigPath, MAX_PATH,
                      sk_install.config.file.c_str (),
                              _TRUNCATE );

        auto& cfg           = cache.config;
        cfg.root_dir        = SK_WideCharToUTF8 (sk_install.config.dir);
        cfg.full_path
                            = SK_WideCharToUTF8 (wszConfigPath);
        PathStripPathW (                         wszConfigPath);
        cfg.shorthand       = SK_WideCharToUTF8 (wszConfigPath);

        //if (! PathFileExistsW (sk_install.config.file.c_str ()))
        //  cfg.shorthand.clear ();

        //if (! PathFileExistsA (cache.dll.full_path.c_str ()))
        //  cache.dll.shorthand.clear ();

        cache.injection.type        = "None";
        cache.injection.status.text.clear ();
        cache.injection.hover_text.clear  ();

        switch (sk_install.injection.type)
        {
          case sk_install_state_s::Injection::Type::Local:
            cache.injection.type = "Local";
            break;

          case sk_install_state_s::Injection::Type::Global:
          default: // Unknown injection strategy, but let's assume global would work

            if ( _inject.bHasServlet )
            {
              cache.injection.type         = "Global";
              cache.injection.status.text  = 
                         (cache.service)   ? (_inject.bAckInj) ? "Waiting for game..." : "Running"
                                           : "Stopped";

              cache.injection.status.color =
                         (cache.service)   ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)  // HSV (0.3F,  0.99F, 1.F)
                                           : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning); // HSV (0.08F, 0.99F, 1.F);
              cache.injection.status.color_hover =
                         (cache.service)   ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                                           : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
              cache.injection.hover_text   =
                         (cache.service)   ? "Click to stop the service"
                                           : "Click to start the service";
            }
            break;
        }

        switch (sk_install.config.type)
        {
          case ConfigType::Centralized:
            cache.config_repo = "Centralized"; break;
          case ConfigType::Localized:
            cache.config_repo = "Localized";   break;
          default:
            cache.config_repo = "Unknown";
            cache.config.shorthand.clear ();   break;
        }
      }

      static constexpr float
           num_lines = 4.0f;
      auto line_ht   =
        ImGui::GetTextLineHeightWithSpacing ();

      auto frame_id =
        ImGui::GetID ("###Injection_Summary_Frame");

      SKIF_ImGui_BeginChildFrame ( frame_id,
                                     ImVec2 ( _WIDTH - ImGui::GetStyle ().FrameBorderSize * 2.0f,
                                                                                num_lines * line_ht ),
                                        ImGuiWindowFlags_NavFlattened      |
                                        ImGuiWindowFlags_NoScrollbar       |
                                        ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_AlwaysAutoResize  |
                                        ImGuiWindowFlags_NoBackground
                                 );

      ImGui::BeginGroup       ();

      // Column 1
      ImGui::BeginGroup       ();
      ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
      ImGui::TextUnformatted  ("Injection Strategy:");
      ImGui::TextUnformatted  ("Injection DLL:");
      ImGui::TextUnformatted  ("Config Root:");
      ImGui::TextUnformatted  ("Config File:");
      ImGui::PopStyleColor    ();
      ImGui::ItemSize         (ImVec2 (140.f * SKIF_ImGui_GlobalDPIScale,
                                         0.f)
                              ); // Column should have min-width 130px (scaled with the DPI)
      ImGui::EndGroup         ();

      ImGui::SameLine         ();

      // Column 2
      ImGui::BeginGroup       ();
      ImGui::TextUnformatted  (cache.injection.type.c_str   ());

      if (! cache.dll.shorthand.empty ())
      {
        ImGui::TextUnformatted  (cache.dll.shorthand.c_str  ());
        SKIF_ImGui_SetHoverText (cache.dll.full_path.c_str  ());
      }

      else
        ImGui::TextUnformatted ("N/A");

      if (! cache.config.shorthand.empty ())
      {
        // Config Root
        if (ImGui::Selectable         (cache.config_repo.c_str ()))
        {
          std::filesystem::create_directories (SK_UTF8ToWideChar(cache.config.root_dir));

          SKIF_Util_ExplorePath(
            SK_UTF8ToWideChar         (cache.config.root_dir)
          );

          /* Cannot handle special characters such as (c), (r), etc
          SKIF_Util_OpenURI_Formatted (SW_SHOWNORMAL, L"%hs",
                                       cache.config.root_dir.c_str ());
          */
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (cache.config.root_dir.c_str ());
        //SKIF_ImGui_SetHoverTip        ("Open the config root folder");

        // Config File
        if (ImGui::Selectable         (cache.config.shorthand.c_str ()))
        {
          std::filesystem::create_directories (SK_UTF8ToWideChar (cache.config.root_dir));
          HANDLE h = CreateFile ( SK_UTF8ToWideChar (cache.config.full_path).c_str(),
                         GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                               CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL,
                                   NULL );

          // We need to close the handle as well, as otherwise Notepad will think the file
          //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
          if (h != INVALID_HANDLE_VALUE)
            CloseHandle (h);

          /*
          ShellExecuteW ( nullptr,
            L"OPEN", SK_UTF8ToWideChar(cache.config.full_path).c_str(),
                nullptr,   nullptr, SW_SHOWNORMAL
          );
          */

          SKIF_Util_OpenURI (SK_UTF8ToWideChar (cache.config.full_path).c_str(), SW_SHOWNORMAL, NULL);

          /* Cannot handle special characters such as (c), (r), etc
          SKIF_Util_OpenURI_Formatted (SW_SHOWNORMAL, L"%hs", cache.config.full_path.c_str ());
          */
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (cache.config.full_path.c_str ());
        //SKIF_ImGui_SetHoverTip        ("Open the config file");


        if ( ! ImGui::IsPopupOpen ("ConfigFileMenu") &&
               ImGui::IsItemClicked (ImGuiMouseButton_Right))
          ImGui::OpenPopup      ("ConfigFileMenu");

        if (ImGui::BeginPopup ("ConfigFileMenu"))
        {
          ImGui::TextColored (
            ImColor::HSV (0.11F, 1.F, 1.F),
              "Troubleshooting:"
          );

          ImGui::Separator ( );

          if (ImGui::Selectable ("Apply Compatibility Config"))
          {
            std::wofstream config_file(SK_UTF8ToWideChar (cache.config.full_path).c_str());

            if (config_file.is_open())
            {
              std::wstring out_text =
LR"([SpecialK.System]
ShowEULA=false
EnableCEGUI=false

[Steam.Log]
Silent=true

[Input.XInput]
Enable=false

[Input.Gamepad]
EnableDirectInput7=false
EnableDirectInput8=false
EnableHID=false
EnableNativePS4=false
AllowHapticUI=false

[Input.Keyboard]
CatchAltF4=false
BypassAltF4Handler=false

[Textures.D3D11]
Cache=false)";

              config_file.write(out_text.c_str(),
                out_text.length());

              config_file.close();
            }
          }

          SKIF_ImGui_SetHoverTip ("Known as the \"sledgehammer\" config within the community as it disables\n"
                                  "various features of Special K in an attempt to improve compatibility.");

          if (ImGui::Selectable ("Reset Config File"))
          {
            HANDLE h = CreateFile ( SK_UTF8ToWideChar (cache.config.full_path).c_str(),
                        GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                              TRUNCATE_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                  NULL );

            // We need to close the handle as well, as otherwise Notepad will think the file
            //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
            if (h != INVALID_HANDLE_VALUE)
              CloseHandle (h);
          }

          ImGui::EndPopup ( );
        }
      }

      else
      {
        ImGui::TextUnformatted        (cache.config_repo.c_str ());
        ImGui::TextUnformatted        ("N/A");
      }

      // Column should have min-width 100px (scaled with the DPI)
      ImGui::ItemSize         (
        ImVec2 ( 100.0f * SKIF_ImGui_GlobalDPIScale,
                   0.0f
               )                );
      ImGui::EndGroup         ( );
      ImGui::SameLine         ( );

      // Column 3
      ImGui::BeginGroup       ( );

      static bool quickServiceHover = false;

      ImGui::TextColored      (
        (_inject.isPending()) ? ImColor(3, 179, 255)
                              : (quickServiceHover) ? cache.injection.status.color_hover
                                                    : cache.injection.status.color,
        cache.injection.status.text.empty () ? "      "
                                             : "%s",
        (_inject.isPending()) ? (_inject.runState == SKIF_InjectionContext::RunningState::Starting)
                                ? "Starting..."
                                : "Stopping..."
                              : cache.injection.status.text.c_str ()
      );

      quickServiceHover = ImGui::IsItemHovered ();

      if ( ! ImGui::IsPopupOpen ("ServiceMenu") &&
              ImGui::IsItemClicked (ImGuiMouseButton_Right))
        ServiceMenu = PopupState::Open;

      if (cache.injection.type._Equal ("Global") && ! _inject.isPending())
      {
        if (ImGui::IsItemClicked ())
        {
          extern bool SKIF_bStopOnInjection;

          _inject._StartStopInject (cache.service, SKIF_bStopOnInjection);

          cache.app_id = 0;
        }

        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverTip        (
          cache.injection.hover_text.c_str ()
        );
      }

      if (! cache.dll.shorthand.empty ())
      {
        ImGui::Text           (cache.dll.version.empty () ? "      "
                                                          : "v %s",
                               cache.dll.version.c_str ());
      }
      ImGui::EndGroup         ();

      // End of columns
      ImGui::EndGroup         ();

      ImGui::EndChildFrame    ();

      ImGui::Separator ();

      auto frame_id2 =
        ImGui::GetID ("###Injection_Play_Button_Frame");

      ImGui::PushStyleVar (
        ImGuiStyleVar_FramePadding,
          ImVec2 ( 120.0f * SKIF_ImGui_GlobalDPIScale,
                    40.0f * SKIF_ImGui_GlobalDPIScale)
      );

      SKIF_ImGui_BeginChildFrame (
        frame_id2, ImVec2 (  0.0f,
                            110.f * SKIF_ImGui_GlobalDPIScale ),
          ImGuiWindowFlags_NavFlattened      |
          ImGuiWindowFlags_NoScrollbar       |
          ImGuiWindowFlags_NoScrollWithMouse |
          ImGuiWindowFlags_AlwaysAutoResize  |
          ImGuiWindowFlags_NoBackground
      );

      ImGui::PopStyleVar ();

      std::string      buttonLabel = ICON_FA_GAMEPAD "  Launch ";// + pTargetApp->type;
      ImGuiButtonFlags buttonFlags = ImGuiButtonFlags_None;

      if (pTargetApp->_status.running)
      {
          buttonLabel = "Running...";
          buttonFlags = ImGuiButtonFlags_Disabled;
          ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
      }

      // Disable the button for global injection types if the servlets are missing
      if ( ! _inject.bHasServlet && ! cache.injection.type._Equal ("Local") )
      {
        ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
      }

      // This captures two events -- launching through context menu + large button
      if ( ImGui::ButtonEx (
                  buttonLabel.c_str (),
                      ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                50.0f * SKIF_ImGui_GlobalDPIScale ), buttonFlags )
           ||
        clickedGameLaunch
           ||
        clickedGameLaunchWoSK )
      {

        if ( pTargetApp->store != "Steam" && pTargetApp->store != "EGS" &&
             pTargetApp->launch_configs[0].getExecutableFullPath(pApp->id).find(L"InvalidPath") != std::wstring::npos )
        {
          confirmPopupText = "Could not launch game due to missing executable:\n\n" + SK_WideCharToUTF8(pTargetApp->launch_configs[0].getExecutableFullPath(pApp->id, false));
          ConfirmPopup     = PopupState::Open;
        }

        else {
          // Launch preparations for Global
          if (! cache.injection.type._Equal ("Local"))
          {
            std::string fullPath    = SK_WideCharToUTF8(pTargetApp->launch_configs[0].getExecutableFullPath (pTargetApp->id));
            bool isLocalBlacklisted  = pTargetApp->launch_configs[0].isBlacklisted (pTargetApp->id),
                 isGlobalBlacklisted = _inject._TestUserList (fullPath.c_str (), false);

            if (! clickedGameLaunchWoSK &&
                ! isLocalBlacklisted    &&
                ! isGlobalBlacklisted
               )
            {
              // Whitelist the path if it haven't been already
              _inject._WhitelistBasedOnPath (fullPath);
            }

            // Kickstart service if it is currently not running
            if (! clickedGameLaunchWoSK && ! _inject.bCurrentState )
              _inject._StartStopInject (false, true);

            // Stop the service if the user attempts to launch without SK
            else if ( clickedGameLaunchWoSK && _inject.bCurrentState )
              _inject._StartStopInject   (true);
          }

          extern bool SKIF_bPreferGOGGalaxyLaunch;
          extern bool GOGGalaxy_Installed;

          // Launch game
          if (pTargetApp->store == "GOG" && GOGGalaxy_Installed && SKIF_bPreferGOGGalaxyLaunch && ! clickedGameLaunch && ! clickedGameLaunchWoSK)
          {
            extern std::wstring GOGGalaxy_Path;

            // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

            std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = GOGGalaxy_Path.c_str();
            sexi.lpParameters = launchOptions.c_str();
          //sexi.lpDirectory  = NULL;
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);
          }

          else if (pTargetApp->store == "EGS")
          {
            // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
            SKIF_Util_OpenURI ((L"com.epicgames.launcher://apps/" + pTargetApp->launch_configs[0].launch_options + L"?action=launch&silent=true").c_str());
          }

          else if (pTargetApp->store == "Steam") {
            //SKIF_Util_OpenURI_Threaded ((L"steam://run/" + std::to_wstring(pTargetApp->id)).c_str()); // This is seemingly unreliable
            SKIF_Util_OpenURI ((L"steam://run/" + std::to_wstring(pTargetApp->id)).c_str());
            pTargetApp->_status.invalidate();
          }
          
          else { // SKIF Custom, GOG without Galaxy
            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = pTargetApp->launch_configs[0].getExecutableFullPath(pTargetApp->id).c_str();
            sexi.lpParameters = pTargetApp->launch_configs[0].launch_options.c_str();
            sexi.lpDirectory  = pTargetApp->launch_configs[0].working_dir   .c_str();
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);
          }
        }

        clickedGameLaunch = clickedGameLaunchWoSK = false;
      }
      
      // Disable the button for global injection types if the servlets are missing
      if ( ! _inject.bHasServlet && ! cache.injection.type._Equal ("Local") )
      {
        ImGui::PopStyleVar ();
        ImGui::PopItemFlag ();
      }

      if (pTargetApp->_status.running)
        ImGui::PopStyleVar ();

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
          ! openedGameContextMenu)
      {
        openedGameContextMenu = true;
      }

      ImGui::EndChildFrame ();
    }

    return 0.0f;
  };


  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
  ImGui::BeginChild          ( "###AppListInset",
                                ImVec2 ( _WIDTH2,
                                         _HEIGHT ), (! SKIF_bDisableBorders),
                                    ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_AlwaysUseWindowPadding );
  ImGui::BeginGroup          ( );

  auto _HandleItemSelection = [&](bool iconMenu = false) ->
  bool
  {
    bool _GamePadRightClick =
      ( ImGui::IsItemFocused ( ) && ( io.NavInputsDownDuration     [ImGuiNavInput_Input] != 0.0f &&
                                      io.NavInputsDownDurationPrev [ImGuiNavInput_Input] == 0.0f &&
                                            ImGui::GetCurrentContext ()->NavInputSource == ImGuiInputSource_NavGamepad ) );

    static constexpr float _LONG_INTERVAL = .15f;

    bool _NavLongActivate =
      ( ImGui::IsItemFocused ( ) && ( io.NavInputsDownDuration     [ImGuiNavInput_Activate] >= _LONG_INTERVAL &&
                                      io.NavInputsDownDurationPrev [ImGuiNavInput_Activate] <= _LONG_INTERVAL ) );

    bool ret =
      ImGui::IsItemActivated (                      ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Right) ||
      _GamePadRightClick                              ||
      _NavLongActivate;

    // If the item is activated, but not visible, scroll to it
    if (ret)
    {
      if (! ImGui::IsItemVisible    (    )) {
        ImGui::SetScrollHereY       (0.5f);
      }
    }

    if (iconMenu)
    {
      if ( ! ImGui::IsPopupOpen ("IconMenu") &&
             ImGui::IsItemClicked (ImGuiMouseButton_Right))
        ImGui::OpenPopup      ("IconMenu");
    }
    else {
      if ( ! openedGameContextMenu)
      {
        if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
             _GamePadRightClick                            ||
             _NavLongActivate)
        {
          openedGameContextMenu = true;
        }
      }
    }

    return ret;
  };

  static constexpr float __ICON_HEIGHT = 32.0f;

  bool  dontcare     = false;
  float fScale       =
    ( ImGui::GetTextLineHeightWithSpacing () / __ICON_HEIGHT ),

        _ICON_HEIGHT =
    std::min (1.0f, std::max (0.1f, fScale)) * __ICON_HEIGHT;

  ImVec2 f0 = ImGui::GetCursorPos (  );
    ImGui::Selectable ("###zero", &dontcare, ImGuiSelectableFlags_Disabled);
  ImVec2 f1 = ImGui::GetCursorPos (  );
    ImGui::SameLine (                );
    SKIF_ImGui_OptImage (nullptr, ImVec2 (_ICON_HEIGHT, _ICON_HEIGHT));
  ImVec2 f2 = ImGui::GetCursorPos (  );
             ImGui::SetCursorPosY (f0.y);

  float fOffset =
    ( std::max (f2.y, f1.y) - std::min (f2.y, f1.y) -
           ImGui::GetStyle ().ItemSpacing.y / 2.0f ) * SKIF_ImGui_GlobalDPIScale / 2.0f + (1.0f * SKIF_ImGui_GlobalDPIScale);

  static bool deferred_update = false;


  // Start populating the whole list

  for (auto& app : apps)
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second.id == 0)
      continue;

    // If not game, skip
  //if (app.second.type != "Game")
  //  continue; // This doesn't work since its reliant on loading the manifest, which is only done when an item is actually selected
    
    bool selected = (selection.appid == app.second.id &&
                     selection.store == app.second.store);
    bool change   = false;

    app.second._status.refresh (&app.second);

    float fOriginalY =
      ImGui::GetCursorPosY ();


    // Start Icon + Selectable row

    ImGui::BeginGroup      ();
    ImGui::PushID          (SK_FormatString("%s%i", app.second.store.c_str(), app.second.id).c_str());

    SKIF_ImGui_OptImage    (app.second.textures.icon.p,
                              ImVec2 ( _ICON_HEIGHT,
                                       _ICON_HEIGHT )
                            );

    change |=
      _HandleItemSelection (true);

    ImGui::SameLine        ();

    ImVec4 _color =
      ( app.second._status.updating != 0x0 )
                  ? ImColor::HSV (0.6f, .6f, 1.f) :
      ( app.second._status.running  != 0x0 )
                  ? ImColor::HSV (0.3f, 1.f, 1.f) :
                    ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Game Title
    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::Selectable      (app.second.ImGuiLabelAndID.c_str(), &selected, ImGuiSelectableFlags_SpanAvailWidth); // (app.first + "###" + app.second.store + std::to_string(app.second.id)).c_str()
    ImGui::PopStyleColor   (2                    );

    static DWORD timeClicked = 0;

    if ( ImGui::IsItemHovered ( ) )
    {
      if ( ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) &&
           timeClicked != 0 )
      {
        timeClicked = 0;

        if ( pApp     != nullptr          &&
             pApp->id != SKIF_STEAM_APPID &&
           ! pApp->_status.running
          )
        {
          clickedGameLaunch = true;
        }
      }
      
      else if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) )
      {
        timeClicked = SKIF_timeGetTime ( );
      }
    }

    change |=
      _HandleItemSelection ();

    // Show full title in tooltip if the title is made up out of more than 48 characters.
    //   Use strlen(.c_str()) to strip \0 characters in the string that would otherwise also be counted.
    if (app.first.length() > 48)
      SKIF_ImGui_SetHoverTip(app.first);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::PopID           ();
    ImGui::EndGroup        ();

    // End Icon + Selectable row


    //change |=
    //  _HandleItemSelection ();

    if (manual_selection.id    == app.second.id &&
        manual_selection.store == app.second.store)
    {
      selection.appid     = 0;
      manual_selection.id = 0;
      manual_selection.store.clear();
      change           = true;
    }

    if ( app.second.id == selection.appid &&
                   sort_changed &&
        (! ImGui::IsItemVisible ()) )
    {
      selection.appid  = 0;
      change = true;
    }

    if (change)
    {
      update = (selection.appid != app.second.id ||
                selection.store != app.second.store);

      selection.appid      = app.second.id;
      selection.store      = app.second.store;
      selected   = true;
      SKIF_iLastSelected = selection.appid;

      if (update)
      {
        timeClicked = SKIF_timeGetTime ( );

        app.second._status.invalidate ();

        if (! ImGui::IsMouseDown (ImGuiMouseButton_Right))
        {
          // Activate the row of the current game
          ImGui::ActivateItem (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()));

          if (! ImGui::IsItemVisible    (    )) {
            ImGui::SetScrollHereY       (0.5f);
          } ImGui::SetKeyboardFocusHere (    );

          // This fixes ImGui not allowing the GameContextMenu to be opened on first search
          //   without an additional keyboard input
          ImGuiContext& g = *ImGui::GetCurrentContext();
          g.NavDisableHighlight = false;
        }

        //ImGui::SetFocusID(ImGui::GetID(app.first.c_str()), ImGui::GetCurrentWindow());

        deferred_update = true;
      }
    }

    if (selected)
    {
      // This allows the scroll to reset on DPI changes, to keep the selected item on-screen
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last)
        ImGui::SetScrollHereY (0.5f);

      if (! update)
      {
        if (std::exchange (deferred_update, false))
        {
          ImGui::SameLine ();
          /*
          auto _ClipMouseToWindowX = [&](void) -> float
          {
            float fWindowX   = ImGui::GetWindowPos              ().x,
                  fScrollX   = ImGui::GetScrollX                (),
                  fContentX0 = ImGui::GetWindowContentRegionMin ().x,
                  fContentX1 = ImGui::GetWindowContentRegionMax ().x,
                  fLocalX    = io.MousePos.x - fWindowX - fScrollX;

            return
              fWindowX + fScrollX +
                std::max   ( fContentX0,
                  std::min ( fContentX1, fLocalX ) );
          };

          // Span the entire width of the list, not just the part with text
          ImVec2 vMax (
            ImGui::GetItemRectMin ().x + ImGui::GetWindowContentRegionWidth (),
            ImGui::GetItemRectMax ().y
          );
          */
          /*
          if (ImGui::IsItemVisible ())
          {
            auto _IsMouseInWindow = [&](void) -> bool
            {
              HWND  hWndActive = ::GetForegroundWindow ();
              RECT  rcClient   = { };
              POINT ptMouse    = { };

              GetClientRect  (hWndActive, &rcClient);
              GetCursorPos   (            &ptMouse );
              ScreenToClient (hWndActive, &ptMouse );

              return PtInRect (&rcClient, ptMouse);
            };

            if (_IsMouseInWindow ())
            {
              if (! ImGui::IsMouseHoveringRect (
                            ImGui::GetItemRectMin (),
                              vMax
                    )
                 ) io.MousePos.y = ImGui::GetCursorScreenPos ().y;
              io.MousePos.x      =       _ClipMouseToWindowX ();
              io.WantSetMousePos = true;
            }
          }
          */

          ImGui::Dummy    (ImVec2 (0,0));
        }
      }

      pApp = &app.second;
    }
  }

  float fOriginalY =
    ImGui::GetCursorPosY ( );

  extern bool SKIF_bDisableStatusBar;
  if (SKIF_bDisableStatusBar)
  {
    ImGui::BeginGroup      ( );

    static bool btnHovered;
    ImGui::PushStyleColor (ImGuiCol_Header,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

    if (btnHovered)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    else
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));

    ImGui::SetCursorPosY   (fOriginalY + fOffset     + ( 1.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + ( 3.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::Text            (ICON_FA_PLUS_SQUARE);
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + (30.0f * SKIF_ImGui_GlobalDPIScale));

    if (ImGui::Selectable      ("Add Game"))
      AddGamePopup = PopupState::Open;

    btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

    ImGui::PopStyleColor(4);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::EndGroup        ();
  }

  // Stop populating the whole list

  // This ensures the next block gets run when launching SKIF with a last selected item
  SK_RunOnce (update = true);

  if (update && pApp != nullptr)
  {
    // Handle GOG, EGS, and SKIF Custom games
    if (pApp->store != "Steam")
    {
      DWORD dwBinaryType = MAXDWORD;
      if ( GetBinaryTypeW (pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str (), &dwBinaryType) )
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          pApp->specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          pApp->specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
      }

      std::wstring test_paths[] = { 
        pApp->launch_configs[0].getExecutableDir(pApp->id, false),
        pApp->launch_configs[0].working_dir
      };

      if (test_paths[0] == test_paths[1])
        test_paths[1] = L"";

      struct {
        InjectionBitness bitness;
        InjectionPoint   entry_pt;
        std::wstring     name;
        std::wstring     path;
      } test_dlls [] = {
        { pApp->specialk.injection.injection.bitness, InjectionPoint::D3D9,    L"d3d9",     L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::DXGI,    L"dxgi",     L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::D3D11,   L"d3d11",    L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::OpenGL,  L"OpenGL32", L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::DInput8, L"dinput8",  L"" }
      };

      // Assume Global 32-bit if we don't know otherwise
      bool bIs64Bit =
        ( pApp->specialk.injection.injection.bitness ==
                         InjectionBitness::SixtyFour );

      pApp->specialk.injection.config.type =
        ConfigType::Centralized;

      wchar_t                 wszPathToSelf [MAX_PATH] = { };
      GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
      PathRemoveFileSpecW (   wszPathToSelf);
      PathAppendW         (   wszPathToSelf,
                                bIs64Bit ? L"SpecialK64.dll"
                                         : L"SpecialK32.dll" );
      pApp->specialk.injection.injection.dll_path = wszPathToSelf;
      pApp->specialk.injection.injection.dll_ver  =
      SKIF_GetSpecialKDLLVersion (       wszPathToSelf);

      pApp->specialk.injection.injection.type =
        InjectionType::Global;
      pApp->specialk.injection.injection.entry_pt =
        InjectionPoint::CBTHook;
      pApp->specialk.injection.config.file =
        L"SpecialK.ini";

      bool breakOuterLoop = false;
      for ( auto& test_path : test_paths)
      {
        if (test_path.empty())
          continue;

        for ( auto& dll : test_dlls )
        {
          dll.path =
            ( test_path + LR"(\)" ) +
             ( dll.name + L".dll" );

          if (PathFileExistsW (dll.path.c_str ()))
          {
            std::wstring dll_ver =
              SKIF_GetSpecialKDLLVersion (dll.path.c_str ());

            if (! dll_ver.empty ())
            {
              pApp->specialk.injection.injection = {
                dll.bitness,
                dll.entry_pt, InjectionType::Local,
                dll.path,     dll_ver
              };

              if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
              {
                pApp->specialk.injection.config.type =
                  ConfigType::Centralized;
              }

              else
              {
                pApp->specialk.injection.config = {
                  ConfigType::Localized,
                  test_path
                };
              }

              pApp->specialk.injection.config.file =
                dll.name + L".ini";

              breakOuterLoop = true;
              break;
            }
          }
        }

        if (breakOuterLoop)
          break;
      }

      if (pApp->specialk.injection.config.type == ConfigType::Centralized)
      {
        pApp->specialk.injection.config.dir =
          SK_FormatStringW(LR"(%ws\Profiles\%ws)",
            path_cache.specialk_userdata.path,
            pApp->specialk.profile_dir.c_str());
      }

      pApp->specialk.injection.config.file =
        ( pApp->specialk.injection.config.dir + LR"(\)" ) +
          pApp->specialk.injection.config.file;

    }

    // Handle Steam games
    else {
      pApp->specialk.injection =
        SKIF_InstallUtils_GetInjectionStrategy (pApp->id);

      // Scan Special K configuration, etc.
      if (pApp->specialk.profile_dir.empty ())
      {
        pApp->specialk.profile_dir = pApp->specialk.injection.config.dir;

        if (! pApp->specialk.profile_dir.empty ())
        {
          SK_VirtualFS profile_vfs;

          int files =
            SK_VFS_ScanTree ( profile_vfs,
                                pApp->specialk.profile_dir.data (), 2 );

          UNREFERENCED_PARAMETER (files);

          //SK_VirtualFS::vfsNode* pFile =
          //  profile_vfs;

          // 4/15/21: Temporarily disable Screenshot Browser, it's not functional enough
          //            to have it distract users yet.
          //
          /////for (const auto& it : pFile->children)
          /////{
          /////  if (it.second->type_ == SK_VirtualFS::vfsNode::type::Directory)
          /////  {
          /////    if (it.second->name.find (LR"(\Screenshots)") != std::wstring::npos)
          /////    {
          /////      for ( const auto& it2 : it.second->children )
          /////      {
          /////        pApp->specialk.screenshots.emplace (
          /////          SK_WideCharToUTF8 (it2.second->getFullPath ())
          /////        );
          /////      }
          /////    }
          /////  }
          /////}
        }
      }
    }
  }



  if (ImGui::BeginPopup ("IconMenu"))
  {
    if (pApp != nullptr)
    {
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE)       .x, ImGui::GetTextLineHeight()));
      if (pApp->textures.isCustomIcon)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UNDO_ALT)         .x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_EXTERNAL_LINK_ALT).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Set Custom Icon",    dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png;*.ico" }, 1, _FILEOPENDIALOGOPTIONS::FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata.path);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata.path, pApp->id);

          if (targetPath != L"")
          {
            std::filesystem::create_directories (targetPath);
            targetPath += L"icon";

            DeleteFile ((targetPath + L".png").c_str());
            DeleteFile ((targetPath + L".jpg").c_str());
            DeleteFile ((targetPath + L".ico").c_str());

            CopyFile(pwszFilePath, (targetPath + ext).c_str(), false);
            
            ImVec2 dontCare1, dontCare2;

            // Reload the icon
            LoadLibraryTexture (LibraryTexture::Icon,
                                  pApp->id,
                                    pApp->textures.icon,
                                      (pApp->store == "GOG")
                                      ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                      : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", SK_GetSteamDir(), pApp->id), //L"_icon.jpg",
                                          dontCare1,
                                            dontCare2,
                                              pApp );
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }

      if (pApp->textures.isCustomIcon)
      {
        if (ImGui::Selectable ("Clear Custom Icon",  dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata.path);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata.path, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata.path, pApp->id);
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata.path, pApp->id);

          if (PathFileExists(targetPath.c_str()))
          {
            targetPath += L"icon";

            bool d1 = DeleteFile ((targetPath + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + L".jpg").c_str()),
                 d3 = DeleteFile ((targetPath + L".ico").c_str());

            // If any file was removed
            if (d1 || d2 || d3)
            {
              ImVec2 dontCare1, dontCare2;

              // Reload the icon
              LoadLibraryTexture (LibraryTexture::Icon,
                                    pApp->id,
                                      pApp->textures.icon,
                                       (pApp->store == "GOG")
                                        ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                        : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", SK_GetSteamDir(), pApp->id), //L"_icon.jpg",
                                            dontCare1,
                                              dontCare2,
                                                pApp );
            }
          }
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
        }
      }

      ImGui::Separator();

      // Strip (recently added) from the game name
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      std::string linkGridDB = (pApp->store == "Steam")
                             ? SK_FormatString("https://www.steamgriddb.com/steam/%lu/icons", pApp->id)
                             : SK_FormatString("https://www.steamgriddb.com/search/icons?term=%s", name.c_str());

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(linkGridDB).c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (linkGridDB);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (SKIF_iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->textures.isCustomIcon)
        ImGui::TextColored (
          (SKIF_iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_UNDO_ALT
                              );

      ImGui::Separator   ( );

      ImGui::TextColored (
              ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_EXTERNAL_LINK_ALT
                            );
    }

    ImGui::EndPopup      ( );
  }

  ImGui::EndGroup        ( );
  ImGui::EndChild        ( );
  ImGui::PopStyleColor   ( );

  if (! ImGui::IsAnyPopupOpen   ( ) &&
      ! ImGui::IsAnyItemHovered ( ) &&
        ImGui::IsItemClicked    (ImGuiMouseButton_Right))
  {
    ImGui::OpenPopup      ("GameListEmptySpaceMenu");
  }

  if (ImGui::BeginPopup   ("GameListEmptySpaceMenu"))
  {
    bool dontCare = false;
    
    ImGui::BeginGroup     ( );
    ImVec2 iconPos = ImGui::GetCursorPos();
    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_PLUS_SQUARE).x, ImGui::GetTextLineHeight()));
    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );
    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_REDO).x, ImGui::GetTextLineHeight()));
    ImGui::EndGroup       ( );

    ImGui::SameLine       ( );

    ImGui::BeginGroup     ( );
     if (ImGui::Selectable ("Add Game", dontCare, ImGuiSelectableFlags_SpanAllColumns))
       AddGamePopup = PopupState::Open;
    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );
    if (ImGui::Selectable ("Refresh",  dontCare, ImGuiSelectableFlags_SpanAllColumns))
      RepopulateGames = true;
    ImGui::EndGroup       ( );

    ImGui::SetCursorPos   (iconPos);
    ImGui::Text           (ICON_FA_PLUS_SQUARE);
    ImGui::Separator      ( );
    ImGui::Text           (ICON_FA_REDO);
    ImGui::EndPopup       ( );
  }

  // Applies hover text on the whole AppListInset1
  //if (SKIF_StatusBarText.empty ()) // Prevents the text from overriding the keyboard search hint
    //SKIF_ImGui_SetHoverText ("Right click for more options");

  ImGui::BeginChild (
    "###AppListInset2",
      ImVec2 ( _WIDTH2,
               _HEIGHT2 ), (! SKIF_bDisableBorders),
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NavFlattened      |
        ImGuiWindowFlags_AlwaysUseWindowPadding
  );
  ImGui::BeginGroup ();

  if ( pApp     == nullptr       ||
       pApp->id == SKIF_STEAM_APPID )
  {
    _inject._GlobalInjectionCtl ();
  }

  else if (pApp != nullptr)
  {
    _PrintInjectionSummary (pApp);

    if ( pApp->extended_config.vac.enabled == 1 )
    {
        SKIF_StatusBarText = "Warning: ";
        SKIF_StatusBarHelp = "VAC protected game - Injection is not recommended!";
    }

    if ( pApp->specialk.injection.injection.type != InjectionType::Local )
    {
      ImGui::SetCursorPosY (
        ImGui::GetWindowHeight () - fBottomDist -
        ImGui::GetStyle        ().ItemSpacing.y
      );

      ImGui::Separator     ( );

      SKIF_ImGui_BeginChildFrame  ( ImGui::GetID ("###launch_cfg"),
                                    ImVec2 (ImGui::GetContentRegionAvail ().x,
                                  std::max (ImGui::GetContentRegionAvail ().y,
                                            ImGui::GetTextLineHeight () + ImGui::GetStyle ().FramePadding.y * 2.0f + ImGui::GetStyle ().ItemSpacing.y * 2
                                           )),
                                    ImGuiWindowFlags_NavFlattened      |
                                    ImGuiWindowFlags_NoScrollbar       |
                                    ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoBackground
      );

      auto _BlacklistCfg =
      [&](app_record_s::launch_config_s& launch_cfg, bool menu = false) ->
      void
      {
        /*
        if (pApp->extended_config.vac.enabled == 1)
        {
          launch_cfg.setBlacklisted (pApp->id, true);
        }
        */

        bool blacklist =
          launch_cfg.isBlacklisted (pApp->id);
          //|| _inject._TestUserList(SK_WideCharToUTF8(launch_cfg.getExecutableFullPath(pApp->id)).c_str(), false);

        char          szButtonLabel [256] = { };
        if (menu)
        {
          sprintf_s ( szButtonLabel, 255,
                        " for \"%ws\"###DisableLaunch%d",
                          launch_cfg.description.empty ()
                            ? launch_cfg.executable .c_str ()
                            : launch_cfg.description.c_str (),
                          launch_cfg.id);
          
          if (ImGui::Checkbox (szButtonLabel,   &blacklist))
            launch_cfg.setBlacklisted (pApp->id, blacklist);

          SKIF_ImGui_SetHoverText (
                      SK_FormatString (
                        menu
                          ? R"(%ws    )"
                          : R"(%ws)",
                        launch_cfg.executable.c_str  ()
                      ).c_str ()
          );
        }
        else
        {
          sprintf_s ( szButtonLabel, 255,
                        " Disable Special K###DisableLaunch%d",
                          launch_cfg.id );
          
          if (ImGui::Checkbox (szButtonLabel,   &blacklist))
            launch_cfg.setBlacklisted (pApp->id, blacklist);
         
          /*
          if (ImGui::Checkbox (szButtonLabel,   &blacklist))
            _inject._BlacklistBasedOnPath(SK_WideCharToUTF8(launch_cfg.getExecutableFullPath(pApp->id)));
          */
        }
      };

      if ( ! _inject.bHasServlet )
      {
        ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Service is unavailable due to missing files.");
      }

      else if ( ! pApp->launch_configs.empty() )
      {
        // Set horizontal position
        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX  () +
          ImGui::GetColumnWidth () -
          ImGui::CalcTextSize   ("Disable Special K >").x -
          ImGui::GetScrollX     () -
          ImGui::GetStyle       ().ItemSpacing.x * 2
        );

        // If there is only one launch option
        if ( pApp->launch_configs.size  () == 1 )
        {
          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () - 1.0f
          );

          // Only if it is a valid one
          if ( pApp->launch_configs.begin()->second.valid)
          {
            _BlacklistCfg          (
                 pApp->launch_configs.begin ()->second );
          }
        }

        // If there are more than one launch option
        else
        {
          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () +
            ImGui::GetStyle      ().ItemSpacing.y / 2.0f
          );

          if ( ImGui::BeginMenu ("Disable Special K") )
          {
            std::set <std::wstring> _used_launches;
            for ( auto& launch : pApp->launch_configs )
            {
              // TODO: Secondary-Launch-Options: Need to ensure launch options that share an executable only gets listed once.
              if (! launch.second.valid ) /* ||
                  ! _used_launches.emplace (launch.second.blacklist_file).second ) */
                continue;

              _BlacklistCfg (launch.second, true);
            }

            ImGui::EndMenu       ();
          }
        }
      }

      ImGui::EndChildFrame     ();

      fBottomDist = ImGui::GetItemRectSize().y;
    }
  }

  ImGui::EndGroup     (                  );
  ImGui::EndChild     (                  );
  ImGui::EndGroup     (                  );

  

  // Special handling at the bottom for Special K
  if ( selection.appid == SKIF_STEAM_APPID ) {
    ImGui::SetCursorPos  (                           ImVec2 ( fX + ImGui::GetStyle().FrameBorderSize,
                                                              fY - floorf((204.f * SKIF_ImGui_GlobalDPIScale) + ImGui::GetStyle().FrameBorderSize) ));
    ImGui::BeginGroup    ();
    static bool hoveredPatButton  = false,
                hoveredPatCredits = false;

    // Set all button styling to transparent
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0, 0, 0, 0));

    // Remove frame border
    ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 0.0f);

    bool        clicked =
    ImGui::ImageButton   ((ImTextureID)pPatTexSRV.p, ImVec2 (200.0F * SKIF_ImGui_GlobalDPIScale,
                                                             200.0F * SKIF_ImGui_GlobalDPIScale),
                                                     ImVec2 (0.f,       0.f),
                                                     ImVec2 (1.f,       1.f),     0,
                                                     ImVec4 (0, 0, 0, 0), // Use a transparent background
                                  hoveredPatButton ? ImVec4 (  1.f,  1.f,  1.f, 1.0f)
                                                   : ImVec4 (  .8f,  .8f,  .8f, .66f));

    // Restore frame border
    ImGui::PopStyleVar   ( );

    // Restore the custom button styling
    ImGui::PopStyleColor (3);

    hoveredPatButton =
    ImGui::IsItemHovered ( );

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");
    //SKIF_ImGui_SetHoverTip  ("Click to help support the project");

    if (clicked)
      SKIF_Util_OpenURI (
        L"https://www.patreon.com/Kaldaien"
      );

    ImGui::SetCursorPos  (ImVec2 (fZ - (238.0f * SKIF_ImGui_GlobalDPIScale),
                                  fY - (204.0f * SKIF_ImGui_GlobalDPIScale)) );

    ImGui::PushStyleColor     (ImGuiCol_ChildBg,        hoveredPatCredits ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
                                                                          : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(.8f, .8f, .8f, .66f));
    ImGui::BeginChild         ("###PatronsChild", ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale,
                                                          200.0f * SKIF_ImGui_GlobalDPIScale),
                                                                      (! SKIF_bDisableBorders),
                                                      ImGuiWindowFlags_NoScrollbar            |
                                                      ImGuiWindowFlags_AlwaysUseWindowPadding |
                        ((pApp->textures.isCustomCover || SKIF_iStyle == 2) ? 0x0 : ImGuiWindowFlags_NoBackground));

    ImGui::TextColored        (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f), "SpecialK Thanks to our Patrons:");

    extern std::string SKIF_GetPatrons (void);
    static std::string patrons_ =
      SKIF_GetPatrons () + '\0';

    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    
    ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor     (ImGuiCol_Text,           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4  (0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_ScrollbarBg,    ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImColor (0, 0, 0, 0).Value);
    ImGui::InputTextMultiline ("###Patrons", patrons_.data (), patrons_.length (),
                   ImVec2 (205.0f * SKIF_ImGui_GlobalDPIScale,
                           160.0f * SKIF_ImGui_GlobalDPIScale),
                                    ImGuiInputTextFlags_ReadOnly );
    ImGui::PopStyleColor      (4);
    ImGui::PopStyleVar        ( );

    hoveredPatCredits =
    ImGui::IsItemActive();

    ImGui::EndChild           ( );
    ImGui::PopStyleColor      ( );

    hoveredPatCredits = hoveredPatCredits ||
    ImGui::IsItemHovered      ( );

    ImGui::EndGroup           ( );
  }


  // Refresh running state of SKIF Custom, EGS, and GOG titles
  static DWORD lastGameRefresh = 0;

  if (SKIF_timeGetTime() > lastGameRefresh + 5000)
  {
    for (auto& app : apps)
    {
      if (app.second.store == "Steam")
        continue;
      app.second._status.running = false;
    }

    PROCESSENTRY32W none = { },
                    pe32 = { };

    SK_AutoHandle hProcessSnap (
      CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
    );

    if ((intptr_t)hProcessSnap.m_h > 0)
    {
      pe32.dwSize = sizeof(PROCESSENTRY32W);

      if (Process32FirstW (hProcessSnap, &pe32))
      {
        do
        {
          std::wstring fullPath;

          SetLastError (NO_ERROR);
          CHandle hProcess (OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID));
          // Use PROCESS_QUERY_LIMITED_INFORMATION since that allows us to retrieve exit code/full process name for elevated processes

          bool accessDenied =
            GetLastError ( ) == ERROR_ACCESS_DENIED;

          // Get exit code to filter out zombie processes
          DWORD dwExitCode = 0;
          GetExitCodeProcess (hProcess, &dwExitCode);

          if (! accessDenied)
          {
            // If the process is not active any longer, skip it (terminated or zombie process)
            if (dwExitCode != STILL_ACTIVE)
              continue;

            WCHAR szExePath[MAX_PATH];
            DWORD len = MAX_PATH;

            // See if we can retrieve the full path of the executable
            if (QueryFullProcessImageName (hProcess, 0, szExePath, &len))
              fullPath = std::wstring (szExePath);
          }

          for (auto& app : apps)
          {
            // Steam games are covered through separate registry monitoring
            if (app.second.store == "Steam")
              continue;

            // EGS, GOG and SKIF Custom should be straight forward
            else if (fullPath == app.second.launch_configs[0].getExecutableFullPath(app.second.id, false)) // full patch
            {
              app.second._status.running = true;
              break;

              // One can also perform a partial match with the below OR clause in the IF statement, however from testing
              //   PROCESS_QUERY_LIMITED_INFORMATION gives us GetExitCodeProcess() and QueryFullProcessImageName() rights
              //     even to elevated processes, meaning the below OR clause is unnecessary.
              // 
              // (fullPath.empty() && ! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str()))
              //
            }
          }

        } while (Process32NextW (hProcessSnap, &pe32));
      }
    }

    lastGameRefresh = SKIF_timeGetTime();
  }


  extern void SKIF_ImGui_ServiceMenu (void);

  SKIF_ImGui_ServiceMenu ( );



  if (openedGameContextMenu)
  {
    ImGui::OpenPopup    ("GameContextMenu");
    openedGameContextMenu = false;
  }


  if (ImGui::BeginPopup ("GameContextMenu"))
  {
    if (pApp != nullptr)
    {
      // Hide the Launch option for Special K
      if (pApp->id != SKIF_STEAM_APPID)
      {
        if ( ImGui::Selectable (("Launch " + pApp->type).c_str (), false,
                               ((pApp->_status.running != 0x0)
                                 ? ImGuiSelectableFlags_Disabled
                                 : ImGuiSelectableFlags_None)))
          clickedGameLaunch = true;

        if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
        {
          if (! _inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Starts the global injection service as well.");

          ImGui::PushStyleColor  ( ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
          );

          if ( ImGui::Selectable (("Launch " + pApp->type + " without Special K").c_str(), false,
                                 ((pApp->_status.running != 0x0)
                                   ? ImGuiSelectableFlags_Disabled
                                   : ImGuiSelectableFlags_None)))
            clickedGameLaunchWoSK = true;

          ImGui::PopStyleColor   ( );
          if (_inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Stops the global injection service as well.");
        }

        extern std::wstring GOGGalaxy_Path;
        extern bool GOGGalaxy_Installed;

        if (GOGGalaxy_Installed && pApp->store == "GOG")
        {
          if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
          {
            ImGui::Separator        ( );

            if (ImGui::BeginMenu    ("Launch using GOG Galaxy"))
            {
              if (ImGui::Selectable (("Launch " + pApp->type).c_str(), false,
                                    ((pApp->_status.running != 0x0)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
                clickedGalaxyLaunch = true;

              ImGui::PushStyleColor ( ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
              );

              if (ImGui::Selectable (("Launch " + pApp->type + " without Special K").c_str(), false,
                                    ((pApp->_status.running != 0x0)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
                clickedGalaxyLaunchWoSK = true;

              ImGui::PopStyleColor  ( );
              ImGui::EndMenu        ( );
            }
          }

          else {
            if (ImGui::Selectable   ("Launch using GOG Galaxy", false,
                                    ((pApp->_status.running != 0x0)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
              clickedGalaxyLaunch = true;
          }

          if (clickedGalaxyLaunch ||
              clickedGalaxyLaunchWoSK)
          {
            // Launch preparations for Global
            if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
            {
              std::string fullPath = SK_WideCharToUTF8 (pApp->launch_configs[0].getExecutableFullPath(pApp->id));
              bool isLocalBlacklisted  = pApp->launch_configs[0].isBlacklisted (pApp->id),
                   isGlobalBlacklisted = _inject._TestUserList (fullPath.c_str (), false);

              if (! clickedGalaxyLaunchWoSK &&
                  ! isLocalBlacklisted      &&
                  ! isGlobalBlacklisted
                 )
              {
                // Whitelist the path if it haven't been already
                _inject._WhitelistBasedOnPath (fullPath);
              }

              // Kickstart service if it is currently not running
              if (clickedGalaxyLaunch && ! _inject.bCurrentState)
                _inject._StartStopInject (false, true);

              // Stop the service if the user attempts to launch without SK
              else if (clickedGalaxyLaunchWoSK && _inject.bCurrentState)
                _inject._StartStopInject (true);
            }

            // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

            std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = GOGGalaxy_Path.c_str();
            sexi.lpParameters = launchOptions.c_str();
          //sexi.lpDirectory  = NULL;
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);

            clickedGalaxyLaunch = clickedGalaxyLaunchWoSK = false;
          }
        }
      }

      // Special K is selected -- relevant show quick links
      else
      {
        ImGui::BeginGroup  ( );
        ImVec2 iconPos = ImGui::GetCursorPos();

        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_BOOK_OPEN).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCORD)  .x, ImGui::GetTextLineHeight()));
        ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
        ImGui::Separator  (  );
        ImGui::PopStyleColor (  );
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCOURSE).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_PATREON)  .x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_GITLAB)   .x, ImGui::GetTextLineHeight()));

        ImGui::EndGroup   (  );
        ImGui::SameLine   (  );
        ImGui::BeginGroup (  );
        bool dontCare = false;

        if (ImGui::Selectable ("Wiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://wiki.special-k.info/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/");


        if (ImGui::Selectable ("Discord", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://discord.gg/specialk"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discord.gg/specialk");


        if (ImGui::Selectable ("Forum", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://discourse.differentk.fyi/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discourse.differentk.fyi/");


        if (ImGui::Selectable ("Patreon", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://www.patreon.com/Kaldaien"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://www.patreon.com/Kaldaien");

        if (ImGui::Selectable ("GitLab", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://gitlab.special-k.info/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://gitlab.special-k.info/");

        ImGui::EndGroup   ( );

        ImGui::SetCursorPos(iconPos);

        ImGui::TextColored (
               ImColor   (25, 118, 210),
                 ICON_FA_BOOK
                             );
        ImGui::TextColored (
               ImColor   (114, 137, 218),
                 ICON_FA_DISCORD
                             );
        ImGui::TextColored (
               (SKIF_iStyle == 2) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImColor (247, 241, 169),
                 ICON_FA_DISCOURSE
                             );
        ImGui::TextColored (
               ImColor   (249, 104,  84),
                 ICON_FA_PATREON
                             );
        ImGui::TextColored (
               ImColor   (226,  67,  40),
                 ICON_FA_GITLAB
                             );
      }

      if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                    (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
        ImGui::Separator  ( );

      /*
      if (! pApp->specialk.screenshots.empty ())
      {
        if (ImGui::BeginMenu ("Screenshots"))
        {
          for (auto& screenshot : pApp->specialk.screenshots)
          {
            if (ImGui::Selectable (screenshot.c_str ()))
            {
              SKIF_GameManagement_ShowScreenshot (
                SK_UTF8ToWideChar (screenshot)
              );
            }

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText       (screenshot.c_str ());
          }

          ImGui::EndMenu ();
        }
      }
      */

      if (! pApp->cloud_saves.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_SD_CARD "   Game Saves and Config");

        std::set <std::wstring> used_paths_;

        if (bMenuOpen)
        {
          if (! pApp->cloud_enabled)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
                                   ICON_FA_EXCLAMATION_TRIANGLE " Auto-Cloud is not enabled" );
            ImGui::Separator   ( );
          }

          bool bCloudSaves = false;

          for (auto& cloud : pApp->cloud_saves)
          {
            if (! cloud.second.valid)
              continue;

            if ( app_record_s::Platform::Unknown == cloud.second.platforms ||
                 app_record_s::supports (           cloud.second.platforms,
                 app_record_s::Platform::Windows )
               )
            {
              wchar_t sel_path [MAX_PATH    ] = { };
              char    label    [MAX_PATH * 2] = { };

              swprintf ( sel_path, L"%ws",
                           cloud.second.evaluated_dir.c_str () );

              sprintf ( label, "%ws###CloudUFS.%d", sel_path,
                                     cloud.first );

              bool selected = false;

              if (used_paths_.emplace (sel_path).second)
              {
                if (ImGui::Selectable (label, &selected))
                {
                  SKIF_Util_ExplorePath (sel_path);

                  ImGui::CloseCurrentPopup ();
                }
                SKIF_ImGui_SetMouseCursorHand ();
                SKIF_ImGui_SetHoverText       (
                             SK_FormatString ( R"(%ws)",
                                        cloud.second.evaluated_dir.c_str ()
                                             ).c_str ()
                                              );
                bCloudSaves = true;
              }
            }
          }

          if (! bCloudSaves)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "No locations could be found.");
          }

          ImGui::EndMenu ();

        //SKIF_ImGui_SetHoverTip ("Browse files cloud-sync'd by Steam");
        }
      }

      if (! pApp->branches.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_CODE_BRANCH "   Software Branches");

        static
          std::set  < std::string >
                      used_branches_;

        using branch_ptr_t =
          std::pair <          std::string*,
             app_record_s::branch_record_s* >;

        static
          std::multimap <
            int64_t, branch_ptr_t
          > branches;

        // Clear the cache when changing selection
        if ( (! branches.empty ()) &&
                branches.begin ()->second.second->parent != pApp )
        {
          branches.clear       ();
          used_branches_.clear ();
        }

        if (bMenuOpen)
        {
          if (branches.empty ())
          {
            for ( auto& it : pApp->branches )
            {
              if (used_branches_.emplace (it.first).second)
              {
                auto& branch =
                  it.second;

                // Sort in descending order
                branches.emplace (
                  std::make_pair   (-(int64_t)branch.build_id,
                    std::make_pair (
                      const_cast <std::string                   *> (&it.first),
                      const_cast <app_record_s::branch_record_s *> (&it.second)
                    )
                  )
                );
              }
            }
          }

          for ( auto& it : branches )
          {
            auto& branch_name =
                  *(it.second.first);

            auto& branch =
                  *(it.second.second);

            ImGui::PushStyleColor (
              ImGuiCol_Text, branch.pwd_required ?
                               ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
                                                 :
                               ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
            );

            bool bExpand =
              ImGui::BeginMenu (branch_name.c_str ());

            ImGui::PopStyleColor ();

            if (bExpand)
            {
              if (! branch.description.empty ())
              {
                ImGui::MenuItem ( "Description",
                            branch.getDescAsUTF8 ().c_str () );
              }

              ImGui::MenuItem ( "App Build #",
                                  std::to_string (
                                                  branch.build_id
                                                 ).c_str ()
              );

              if (branch.time_updated > 0)
              {
                ImGui::MenuItem ( "Last Update", branch.getTimeAsCStr ().c_str () );
              }

              ImGui::MenuItem ( "Accessibility", branch.pwd_required ?
                                       "Private (Password Required)" :
                                              "Public (No Password)" );

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }
      }

      // Manage [Custom] Game
      if (pApp->store == "SKIF" || pApp->store == "GOG")
      {
        ImGui::Separator ( );

        if (ImGui::BeginMenu ("Manage"))
        {
          if (pApp->store == "SKIF")
          {
            if (ImGui::Selectable ("Properties"))
              ModifyGamePopup = PopupState::Open;

            ImGui::Separator ( );
          }

          if (ImGui::Selectable ("Create shortcut"))
          {
            std::string name = pApp->names.normal;

            // Strip (recently added) from the desktop shortcuts
            try {
              name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
            }
            catch (const std::exception& e)
            {
              UNREFERENCED_PARAMETER(e);
            }

            /* Old method
            // Strip invalid filename characters
            //const std::string forbidden = "\\/:?\"<>|";
            //std::transform(name.begin(), name.end(), name.begin(), [&forbidden](char c) { return forbidden.find(c) != std::string::npos ? ' ' : c; });

            // Remove double spaces
            //name = std::regex_replace(name, std::regex(R"(  )"), " ");
            */

            extern std::string SKIF_StripInvalidFilenameChars (std::string);
            name = SKIF_StripInvalidFilenameChars (name);

            std::wstring linkPath = SK_FormatStringW (LR"(%ws\%ws.lnk)", std::wstring(path_cache.desktop.path).c_str(), SK_UTF8ToWideChar(name).c_str());
            std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str(), pApp->launch_configs[0].launch_options.c_str());
            wchar_t wszPath[MAX_PATH + 2] = { };
            GetModuleFileNameW (hModSKIF, wszPath, MAX_PATH);

            confirmPopupTitle = "Create Shortcut";

            if (CreateLink (
                linkPath.c_str(),
                wszPath,
                linkArgs.c_str(),
                pApp->launch_configs[0].working_dir.c_str(),
                SK_UTF8ToWideChar(name).c_str(),
                pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str()
                )
              )
              confirmPopupText = "A desktop shortcut has been created.";
            else
              confirmPopupText = "Failed to create a desktop shortcut!";

            ConfirmPopup = PopupState::Open;
          }

          if (pApp->store == "SKIF")
          {
            if (ImGui::Selectable ("Remove"))
              RemoveGamePopup = PopupState::Open;
          }

          ImGui::EndMenu ( );
        }
      }

      ImGui::PushStyleColor ( ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
      );

      ImGui::Separator ( );

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN) .x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TOOLS)       .x, ImGui::GetTextLineHeight()));

      if (pApp->store == "GOG")
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
      }

      else if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
      }
      ImGui::EndGroup    ( );
      ImGui::SameLine    ( );
      ImGui::BeginGroup  ( );
      bool dontCare = false;
      if (ImGui::Selectable ("Browse Install Folder", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_ExplorePath (pApp->install_dir);
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (pApp->install_dir)
                                        );
      }

      std::wstring pcgwLink =
        (pApp->store == "GOG") ? L"http://www.pcgamingwiki.com/api/gog.php?page=%ws"
                               : (pApp->store == "Steam") ? L"http://www.pcgamingwiki.com/api/appid.php?appid=%ws"
                                                          : L"https://www.pcgamingwiki.com/w/index.php?search=%ws";
      std::wstring pcgwValue =
        (pApp->store == "SKIF") ? SK_UTF8ToWideChar (pApp->names.normal)
                                : (pApp->store == "EGS") ? SK_UTF8ToWideChar(pApp->names.normal) : std::to_wstring(pApp->id);

      if (ImGui::Selectable  ("Browse PCGamingWiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL, pcgwLink.c_str(), pcgwValue.c_str() );
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (
            SK_FormatStringW ( pcgwLink.c_str(), pcgwValue.c_str() )
          ).c_str()
        );
      }

      if (pApp->store == "GOG")
      {
        if (ImGui::Selectable  ("Browse GOG Database", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"https://www.gogdb.org/product/%lu", pApp->id
          );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://www.gogdb.org/product/%lu", pApp->id
            )
          );
        }
      }
      else if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        if (ImGui::Selectable  ("Browse SteamDB", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"https://steamdb.info/app/%lu", pApp->id
                                        );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://steamdb.info/app/%lu", pApp->id
                            )
                                          );
        }

        if (ImGui::Selectable  ("Browse Steam Community", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"https://steamcommunity.com/app/%lu", pApp->id
                                        );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://steamcommunity.com/app/%lu", pApp->id
                            )
                                          );
        }
      }
      ImGui::EndGroup      ( );
      ImGui::PopStyleColor ( );

      ImGui::SetCursorPos  (iconPos);

      ImGui::TextColored (
               ImColor (255, 207, 72),
                 ICON_FA_FOLDER_OPEN
                           );
      ImGui::TextColored (
               ImColor   (200, 200, 200, 255),
                 ICON_FA_TOOLS
                           );

      if (pApp->store == "GOG")
      {
        ImGui::TextColored (
         ImColor   (155, 89, 182, 255),
           ICON_FA_DATABASE );
      }

      else if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        ImGui::TextColored (
         ImColor   (101, 192, 244, 255).Value,
           ICON_FA_DATABASE );

        ImGui::TextColored (
         (SKIF_iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
           ICON_FA_STEAM_SYMBOL );
      }

    }

    else if (! update)
    {
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }


  if (ConfirmPopup == PopupState::Open)
  {
    ImGui::OpenPopup("###ConfirmPopup");
    ConfirmPopup = PopupState::Opened;
  }

  float fConfirmPopupWidth = ImGui::CalcTextSize(confirmPopupText.c_str()).x + 60.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fConfirmPopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ((confirmPopupTitle + "###ConfirmPopup").c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        (confirmPopupText.c_str());

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fConfirmPopupWidth / 2 - vButtonSize.x / 2);

    if (ImGui::Button  ("OK", vButtonSize))
    {
      confirmPopupText = "";
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }


  if (RemoveGamePopup == PopupState::Open)
  {
    ImGui::OpenPopup("###RemoveGamePopup");
    RemoveGamePopup = PopupState::Opened;
  }


  float fRemoveGamePopupWidth = 360.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fRemoveGamePopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ("Remove Game###RemoveGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        ("Do you want to remove this game from SKIF?");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Yes", vButtonSize))
    {
      if (SKIF_RemoveCustomAppID(selection.appid))
      {
        // Hide entry
        pApp->id = 0;

        // Release the icon texture (the cover will be handled by LoadLibraryTexture on next frame
        if (pApp->textures.icon.p != nullptr)
        {
          extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
          SKIF_ResourcesToFree.push(pApp->textures.icon.p);
          pApp->textures.icon.p = nullptr;
        }

        // Reset selection to Special K
        selection.appid = SKIF_STEAM_APPID;
        selection.store = "Steam";

        for (auto& app : apps)
          if (app.second.id == selection.appid)
            pApp = &app.second;

        update = true;
      }

      RemoveGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("No", vButtonSize))
    {
      RemoveGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }


  if (AddGamePopup == PopupState::Open)
  {
    ImGui::OpenPopup("###AddGamePopup");
    //AddGamePopup = PopupState::Opened; // Disabled to allow quick link from the About tab to function
  }

  float fAddGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fAddGamePopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ("Add Game###AddGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    /*
      name          - String -- Title/Name
      exe           - String -- Full Path to executable
      launchOptions - String -- Cmd line args
      id            - Autogenerated
      installDir    - Autogenerated
      exeFileName   - Autogenerated
    */

    static char charName     [MAX_PATH],
                charPath     [MAX_PATH],
                charArgs     [500];
    static bool error = false;

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      extern HWND SKIF_hWnd;

      LPWSTR pwszFilePath = NULL;
      if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe" }, 1, _FILEOPENDIALOGOPTIONS::FOS_NODEREFERENCELINKS))
      {
        error = false;
        std::filesystem::path p = pwszFilePath;

        if (p.extension() == L".lnk")
        {
          WCHAR szTarget   [MAX_PATH];
          WCHAR szArguments[MAX_PATH];

          ResolveIt (SKIF_hWnd, (const char *)p.u8string().c_str(), szTarget, szArguments,       MAX_PATH);

          std::filesystem::path p2 = szTarget;
          std::wstring productName = SKIF_GetProductName (p2.c_str());
          productName.erase(std::find_if(productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
          strncpy (charPath, SK_WideCharToUTF8 (szTarget).c_str(),                  MAX_PATH);
          strncpy (charArgs, SK_WideCharToUTF8 (szArguments).c_str(),               500);
          strncpy (charName, (productName != L"")
                              ? SK_WideCharToUTF8 (productName).c_str()
                              : (const char *)p.replace_extension().filename().u8string().c_str(), MAX_PATH);
        }
        else if (p.extension() == L".exe") {
          std::wstring productName = SKIF_GetProductName (p.c_str());
          productName.erase(std::find_if(productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());

          strncpy (charPath, (const char *)p.u8string().c_str(),                                  MAX_PATH);
          strncpy (charName, (productName != L"")
                              ? SK_WideCharToUTF8 (productName).c_str()
                              : (const char *)p.replace_extension().filename().u8string().c_str(), MAX_PATH);
        }
        else {
          error = true;
          strncpy (charPath, "\0", MAX_PATH);
        }
      }
      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }
    ImGui::SameLine    ( );

    float fAddGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fAddGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 500, ImVec2(0,0), ImGuiInputTextFlags_None);
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    if (ImGui::Button  ("Add Game", vButtonSize))
    {
      int newAppId = SKIF_AddCustomAppID(&apps, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs));

      if (newAppId > 0)
        InterlockedExchange (&need_sort, 1);

      // Clear variables
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      // Change selection to the new game
      selection.appid = newAppId;
      for (auto& app : apps)
        if (app.second.id == selection.appid && app.second.store == "SKIF")
          pApp = &app.second;

      ImVec2 dontCare1, dontCare2;

      // Load the new icon (hopefully)
      LoadLibraryTexture (LibraryTexture::Icon,
                            newAppId,
                              pApp->textures.icon,
                                L"icon",
                                    dontCare1,
                                      dontCare2,
                                        pApp );

      update = true;

      // Unload any current cover
      if (pTexSRV.p != nullptr)
      {
        extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
        SKIF_ResourcesToFree.push(pTexSRV.p);
        pTexSRV.p = nullptr;
      }

      AddGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    if (disabled)
    {
      ImGui::PopItemFlag ( );
      ImGui::PopStyleVar ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      AddGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }



  if (ModifyGamePopup == PopupState::Open)
    ImGui::OpenPopup("###ModifyGamePopup");

  float fModifyGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fModifyGamePopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ("Manage Game###ModifyGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char charName     [MAX_PATH],
                charPath     [MAX_PATH],
                charArgs     [500];
                //charProfile  [MAX_PATH];
    static bool error = false;

    if (ModifyGamePopup == PopupState::Open)
    {
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      strncpy (charName, name.c_str( ), MAX_PATH);
      strncpy (charPath, SK_WideCharToUTF8 (pApp->launch_configs[0].getExecutableFullPath(pApp->id, false)).c_str(), MAX_PATH);
      strncpy (charArgs, SK_WideCharToUTF8 (pApp->launch_configs[0].launch_options).c_str(), 500);
      //strncpy (charProfile, SK_WideCharToUTF8 (SK_FormatStringW(LR"(%s\Profiles\%s)", path_cache.specialk_userdata.path, pApp->specialk.profile_dir.c_str())).c_str(), MAX_PATH);

      ModifyGamePopup = PopupState::Opened;
    }

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      LPWSTR pwszFilePath = NULL;
      if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe" }, 1))
      {
        error = false;
        strncpy (charPath, (const char *)std::filesystem::path(pwszFilePath).u8string().c_str(), MAX_PATH);
      }
      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }
    ImGui::SameLine    ( );

    float fModifyGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fModifyGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fModifyGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fModifyGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 500, ImVec2(0,0), ImGuiInputTextFlags_None);
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    if (ImGui::Button  ("Update", vButtonSize))
    {
      if (SKIF_ModifyCustomAppID (pApp, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs)))
      {
        for (auto& app : apps)
        {
          if (app.second.id == pApp->id && app.second.store == "SKIF")
          {
            app.first = pApp->names.normal;

            std::string all_upper;

            for (const char c : app.first)
            {
              if (! ( isalnum (c) || isspace (c)))
                continue;

              all_upper += (char)toupper (c);
            }

            static const
              std::string toSkip [] =
              {
                std::string ("A "),
                std::string ("THE ")
              };

            for ( auto& skip_ : toSkip )
            {
              if (all_upper.find (skip_) == 0)
              {
                all_upper =
                  all_upper.substr (
                    skip_.length ()
                  );
                break;
              }
            }

            std::string trie_builder;

            for ( const char c : all_upper)
            {
              trie_builder += c;

              labels.insert (trie_builder);
            }

            pApp->names.all_upper = trie_builder;
          }
        }

        InterlockedExchange (&need_sort, 1);

        // Clear variables
        strncpy (charName, "\0", MAX_PATH);
        strncpy (charPath, "\0", MAX_PATH);
        strncpy (charArgs, "\0", 500);

        update = true;

        ModifyGamePopup = PopupState::Closed;
        ImGui::CloseCurrentPopup();
      }
    }

    if (disabled)
    {
      ImGui::PopItemFlag ( );
      ImGui::PopStyleVar ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      ModifyGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }

  extern uint32_t SelectNewSKIFGame;

  if (SelectNewSKIFGame > 0)
  {
    // Change selection to the new game
    selection.appid = SelectNewSKIFGame;
    selection.store = "SKIF";
    for (auto& app : apps)
      if (app.second.id == selection.appid && app.second.store == "SKIF")
        pApp = &app.second;

    update = true;

    SelectNewSKIFGame = 0;
  }
}


#if 0
struct SKIF_Store {

};

struct SKIF_Anticheat {
  bool has;
};

enum class DRM_Authority {
  None,
  Steam,
  Origin,
  uPlay,
  MicrosoftStore,
  EpicGameStore,
  Bethesda_net,
  Galaxy
};

enum class AntiTamper_Type {
  None,
  Denuvo
};

struct SKIF_DRM {
  DRM_Authority   drm_auth;
  AntiTamper_Type anti_tamper;
};

struct SKIF_GameFeatures {
  bool vr;
  bool hdr;
  bool cloud;
  bool achievements;
  bool leaderboards;
  bool rich_presence;
//bool raytraced;
};
struct SKIF_Game {
  SKIF_Store     parent;
  std::wstring   name;
  std::wstring   executable;
  std::vector <
    std::wstring
  >              config_paths;
  bool           enable_injection;
  int            render_api;
  int            input_api;
  int            bitness;
  SKIF_DRM       drm;
};
#endif
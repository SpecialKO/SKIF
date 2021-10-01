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

const int   SKIF_STEAM_APPID      = 1157970;
bool        SKIF_STEAM_OWNER      = false;
static bool clickedGameLaunch,
            clickedGameLaunchWoSK,
            clickedGalaxyLaunch,
            clickedGalaxyLaunchWoSK = false,
            openedGameContextMenu = false;

#include <SKIF.h>
#include <injection.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "../DirectXTex/DirectXTex.h"

#include <font_awesome.h>

extern ID3D11Device* g_pd3dDevice;
extern float         SKIF_ImGui_GlobalDPIScale;
extern float         SKIF_ImGui_GlobalDPIScale_Last;
extern std::string   SKIF_StatusBarHelp;
extern std::string   SKIF_StatusBarText;
extern bool          SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);

#include <stores/Steam/apps_list.h>
#include <stores/Steam/asset_fetch.h>
#include <stores/GOG/gog_library.h>

static std::wstring sshot_file = L"";

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
               DWORD       dwAction )
{
  //return
    //ShellExecuteW ( nullptr, L"OPEN",
      //path.data (), nullptr,
                    //nullptr, dwAction );
  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = path.data ();
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
        app_record_s*                       pApp = nullptr)
{
  static CComPtr <ID3D11Texture2D> pTex2D;
  static DirectX::TexMetadata        meta = { };
  static DirectX::ScratchImage        img = { };

  std::wstring load_str = L"\0",
               SKIFCustomPath,
               SteamCustomPath;

  bool succeeded = false;

  if (pApp != nullptr)
    appid = pApp->id;

  // SKIF
  if (       appid == SKIF_STEAM_APPID &&
      libTexToLoad != LibraryTexture::Patreon)
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\)", std::wstring(path_cache.specialk_userdata.path).c_str());

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
  }

  // GOG
  else if (pApp != nullptr && pApp->store == "GOG")
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)", std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
    
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
    else if (libTexToLoad == LibraryTexture::Icon)
      load_str =               name;

    if (libTexToLoad == LibraryTexture::Cover &&
        load_str == L"\0")
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

  // STEAM
  else if (pApp != nullptr && pApp->store == "Steam")
  {
    static unsigned long SteamUserID = 0;
     
    if (SteamUserID == 0)
    {
      HKEY hKey;
      WCHAR szData[255];
      DWORD dwSize = sizeof(szData);
      PVOID pvData = szData;

      //Allocationg memory for a DWORD value.
      DWORD dataType;

      if (RegOpenKeyExW(HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
      {
        if (RegGetValueW(hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, &dataType, pvData, &dwSize) == ERROR_SUCCESS)
          SteamUserID = *(DWORD*)pvData;

        RegCloseKey(hKey);
      }
    }

    SKIFCustomPath  = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", std::wstring(path_cache.specialk_userdata.path).c_str(), appid);
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
    else if (libTexToLoad == LibraryTexture::Cover &&
             PathFileExistsW ((SteamCustomPath + L"p.png").c_str()))
      load_str =               SteamCustomPath + L"p.png";
    else if (libTexToLoad == LibraryTexture::Cover &&
             PathFileExistsW ((SteamCustomPath + L"p.jpg").c_str()))
      load_str =               SteamCustomPath + L"p.jpg";
    else
      load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i%ws)", SK_GetSteamDir(), appid, name.c_str());
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

  if (succeeded)
  {
    DirectX::ScratchImage* pImg   =
                                &img;
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
      ) { meta =  converted_img.GetMetadata ();
          pImg = &converted_img; }
    }

    if (
      SUCCEEDED (
        DirectX::CreateTexture (
          (ID3D11Device *)g_pd3dDevice,
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
          g_pd3dDevice->CreateShaderResourceView (
              pTex2D.p, &srv_desc,
            &pLibTexSRV.p
          )
        )
      )
      {
        pLibTexSRV.p = nullptr;
      }

      // SRV is holding a reference, this is not needed anymore.
      pTex2D.Release ();
    }
  }
};

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

        if (
          SUCCEEDED (
            DirectX::CreateTexture (
              (ID3D11Device *)g_pd3dDevice,
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

          if (   pTex2D.p == nullptr ||
            FAILED (
              g_pd3dDevice->CreateShaderResourceView (
                 pTex2D.p, &srv_desc,
                &pTexSRV.p
              )
            )
          )
          {
            pTexSRV.p = nullptr;
          }

          // SRV is holding a reference, this is not needed anymore.
          pTex2D.Release ();
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
	for (size_t i = 0; i < key.length(); i++)
	{
		// go to next node
		curr = curr->character[key[i]];

		// if string is invalid (reached end of path in Trie)
		if (curr == nullptr)
			return false;
	}

	// if current node is a leaf and we have reached the
	// end of the string, return true
	return curr->isLeaf;
}

// returns true if given node has any children
bool Trie::haveChildren(Trie const* curr)
{
	for (int i = 0; i < CHAR_SIZE; i++)
		if (curr->character[i])
			return true;	// child found

	return false;
}

// Recursive function to delete a key in the Trie
bool Trie::deletion (Trie*& curr, const std::string& key)
{
	// return if Trie is empty
	if (curr == nullptr)
		return false;

	// if we have not reached the end of the key
	if (key.length())
	{
		// recur for the node corresponding to next character in the key
		// and if it returns true, delete current node (if it is non-leaf)

		if (curr != nullptr &&
			curr->character[key[0]] != nullptr &&
			deletion(curr->character[key[0]], key.substr(1)) &&
			curr->isLeaf == false)
		{
			if (!haveChildren(curr))
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
	if (key.length() == 0 && curr->isLeaf)
	{
		// if current node is a leaf node and don't have any children
		if (!haveChildren(curr))
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

uint32_t manual_selection = 0;

Trie labels;

void
SKIF_GameManagement_DrawTab (void)
{

  if (! sshot_file.empty ())
  {
    SKIF_GameManagement_ShowScreenshot (sshot_file);
  }

  static CComPtr <ID3D11Texture2D>          pTex2D;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV;

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

  static std::vector <AppId_t> appids =
    SK_Steam_GetInstalledAppIDs ();

  auto PopulateAppRecords = [&](void) ->
    std::vector <std::pair <std::string, app_record_s>>
  { std::vector <std::pair <std::string, app_record_s>> ret;

    std::set <uint32_t> unique_apps;

    for ( auto app : appids )
    {
      // Skip Steamworks Common Redists
      if (app == 228980) continue;

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

  static volatile LONG need_sort    = 0;
  bool                 sort_changed = false;

  if (InterlockedCompareExchange (&need_sort, 0, 1))
  {
    std::sort ( apps.begin (),
                apps.end   (),
      []( const app_entry_t& a,
          const app_entry_t& b ) -> int
      {
        return a.second.names.all_upper.compare (
               b.second.names.all_upper
        ) < 0;
      }
    );

    sort_changed = true;
  }

  static bool     update        = true;
  static uint32_t appid         = SKIF_STEAM_APPID;
  static bool     populated     = false;

  if (! populated)
  {
    populated      = true;
    apps           = PopulateAppRecords ();

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

      std::pair <std::string, app_record_s>
        SKIF ( "Special K", SKIF_record );

      apps.emplace_back (SKIF);
    }

    // Load GOG titles from registry
    extern bool SKIF_bDisableGOGLibrary;
    if (! SKIF_bDisableGOGLibrary)
      SKIF_GOG_GetInstalledAppIDs (&apps);

    // We're going to stream icons in asynchronously on this thread
    _beginthread ([](LPVOID pUser)->void
    {
      auto* pUserTex2D =
        (CComPtr <ID3D11Texture2D>*)pUser;

      CoInitializeEx (nullptr, 0x0);

      DirectX::TexMetadata  local_meta = { };
      DirectX::ScratchImage local_img  = { };

      CComPtr <ID3D11Texture2D>& pLocalTex2D =
                                 *pUserTex2D;

      auto _LoadLibraryTexture =
      [&] (
              LibraryTexture                      libTexToLoad,
              uint32_t                            appid,
              CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
              const std::wstring&                 name,
              app_record_s*                       pApp = nullptr)
      {
        std::wstring load_str = L"\0",
                     SKIFCustomPath,
                     SteamCustomPath;

        bool succeeded = false;

        if (pApp != nullptr)
          appid = pApp->id;

        // SKIF
        if (       appid == SKIF_STEAM_APPID &&
            libTexToLoad != LibraryTexture::Patreon)
        {
          SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\)", std::wstring(path_cache.specialk_userdata.path).c_str());

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
        }

        // GOG
        else if (pApp != nullptr && pApp->store == "GOG")
        {
          SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)", std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
    
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
          else if (libTexToLoad == LibraryTexture::Icon)
            load_str =               name;

          if (libTexToLoad == LibraryTexture::Cover &&
              load_str == L"\0")
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

        // STEAM
        else if (pApp != nullptr && pApp->store == "Steam")
        {
          static unsigned long SteamUserID = 0;
     
          if (SteamUserID == 0)
          {
            HKEY hKey;
            WCHAR szData[255];
            DWORD dwSize = sizeof(szData);
            PVOID pvData = szData;

            //Allocationg memory for a DWORD value.
            DWORD dataType;

            if (RegOpenKeyExW(HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
              if (RegGetValueW(hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, &dataType, pvData, &dwSize) == ERROR_SUCCESS)
                SteamUserID = *(DWORD*)pvData;

              RegCloseKey(hKey);
            }
          }

          SKIFCustomPath  = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", std::wstring(path_cache.specialk_userdata.path).c_str(), appid);
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
          else if (libTexToLoad == LibraryTexture::Cover &&
                   PathFileExistsW ((SteamCustomPath + L"p.png").c_str()))
            load_str =               SteamCustomPath + L"p.png";
          else if (libTexToLoad == LibraryTexture::Cover &&
                   PathFileExistsW ((SteamCustomPath + L"p.jpg").c_str()))
            load_str =               SteamCustomPath + L"p.jpg";
          else
            load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i%ws)", SK_GetSteamDir(), appid, name.c_str());
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

        if (succeeded)
        {
          DirectX::ScratchImage* pImg   =
                                      &img;
          DirectX::ScratchImage   converted_img;

          // We don't want single-channel icons, so convert to RGBA
          if (meta.format == DXGI_FORMAT_R8_UNORM)
          {
            if (
              SUCCEEDED (
                DirectX::Convert (
                  img.GetImages   (), img.GetImageCount (),
                  img.GetMetadata (), DXGI_FORMAT_R8G8B8A8_UNORM,
                    DirectX::TEX_FILTER_DEFAULT,
                    DirectX::TEX_THRESHOLD_DEFAULT,
                      converted_img
                )
              )
            ) { meta =  converted_img.GetMetadata ();
                pImg = &converted_img; }
          }

          if (
            SUCCEEDED (
              DirectX::CreateTexture (
                (ID3D11Device *)g_pd3dDevice,
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
                g_pd3dDevice->CreateShaderResourceView (
                    pTex2D.p, &srv_desc,
                  &pLibTexSRV.p
                )
              )
            )
            {
              pLibTexSRV.p = nullptr;
            }

            // SRV is holding a reference, this is not needed anymore.
            pTex2D.Release ();
          }
        }
      };

      SK_RunOnce (
        //__LoadPatreonTexture (0, pPatTexSRV, L"(patreon.png)")
        _LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, pPatTexSRV, L"(patreon.png)")
      );

      for ( auto& app : apps )
      {
        // Special handling for non-Steam owners of Special K / SKIF
        if ( app.second.id == SKIF_STEAM_APPID )
          app.first = "Special K";

        // Regular handling for the remaining Steam games
        else if (app.second.store != "GOG") {
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
          else
          {
            app.first =
              SK_UseManifestToGetAppName (
                           app.second.id );
          }

          // Strip null terminators
          app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());
        }

        // Corrupted app manifest / not known to Steam client; SKIP!
        if (app.first.empty ())
        {
          app.second.id = 0;
          continue;
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

        // SKIF
        if ( app.second.id == SKIF_STEAM_APPID )
          _LoadLibraryTexture (LibraryTexture::Icon,
                                app.second.id,
                                app.second.textures.icon,
                                L"_icon.jpg"
        );

        // GOG
        else  if (app.second.store == "GOG")
          _LoadLibraryTexture (LibraryTexture::Icon,
                                app.second.id,
                                app.second.textures.icon,
                                app.second.install_dir + L"\\goggame-" + std::to_wstring(app.second.id) + L".ico",
                                &app.second
        );

        // STEAM
        else if (app.second.store == "Steam")
          _LoadLibraryTexture (LibraryTexture::Icon,
                                app.second.id,
                                app.second.textures.icon,
                                L"_icon.jpg",
                                &app.second
        );

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

      InterlockedExchange (&need_sort, 1);
    }, 0x0, (void *)&pTex2D);
  }

  if (! update)
  {
    update =
      SKIF_LibraryAssets_CheckForUpdates (true);
  }

  if (update)
  {
    pTex2D  = nullptr;
    pTexSRV = nullptr;
  }


  ImGui::BeginGroup    (                                                  );
  float fX =
  ImGui::GetCursorPosX (                                                  );

  // Display cover image
  ImGui::Image         ((ImTextureID)pTexSRV.p,    ImVec2 (600.0F * SKIF_ImGui_GlobalDPIScale,
                                                           900.0F * SKIF_ImGui_GlobalDPIScale ),
                                                   ImVec2 (0,0),
                                                   ImVec2 (1,1),
                                                   ImVec4 (1,1,1,1),
                                 ImGui::GetStyleColorVec4 (ImGuiCol_Border)
  );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ImGui::OpenPopup ("CoverMenu");

  if (ImGui::BeginPopup ("CoverMenu"))
  {
    static
      app_record_s* pApp = nullptr;

    for (auto& app : apps)
      if (app.second.id == appid)
        pApp = &app.second;

    if (pApp != nullptr)
    {
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();
      
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE)       .x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UNDO_ALT)         .x, ImGui::GetTextLineHeight()));
      ImGui::Separator  (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_EXTERNAL_LINK_ALT).x, ImGui::GetTextLineHeight()));
      ImGui::PopStyleColor (  );

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Set Custom Artwork",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
	      IFileOpenDialog  *pFileOpen;
        COMDLG_FILTERSPEC fileTypes{ L"Images", L"*.jpg;*.png" };

	      PWSTR pszFilePath = NULL;

	      // Create the FileOpenDialog object.
	      HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		                    IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

	      if (SUCCEEDED(hr))
	      {
               pFileOpen->SetFileTypes(1, &fileTypes);
		      hr = pFileOpen->Show(NULL);

		      // Get the file name from the dialog box.
		      if (SUCCEEDED(hr))
		      {
			      IShellItem *pItem;
			      hr = pFileOpen->GetResult(&pItem);

			      if (SUCCEEDED(hr))
			      {
				      hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

              std::wstring targetPath = L"";
              std::wstring ext        = std::filesystem::path(pszFilePath).extension().wstring();

              if (pApp->id == SKIF_STEAM_APPID)
                targetPath = SK_FormatStringW(LR"(%ws\Assets\)",          std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
              else if (pApp->store == "GOG")
                targetPath = SK_FormatStringW(LR"(%ws\Assets\GOG\%i\)",   std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
              else
                targetPath = SK_FormatStringW(LR"(%ws\Assets\Steam\%i\)", std::wstring (path_cache.specialk_userdata.path).c_str(), appid);

              if (targetPath != L"")
              {
                std::filesystem::create_directories (targetPath);
                targetPath += L"cover";

                if (ext == L".jpg")
                  DeleteFile((targetPath + L".png").c_str());

                CopyFile(pszFilePath, (targetPath + ext).c_str(), false);

                update = true;
              }

				      pItem->Release();
			      }
		      }
		      pFileOpen->Release();
	      }
      }

      if (ImGui::Selectable ("Clear Custom Artwork", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        std::wstring targetPath = L"";
        
        if (pApp->id == SKIF_STEAM_APPID)
          targetPath = SK_FormatStringW(LR"(%ws\Assets\)",          std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
        else if (pApp->store == "GOG")
          targetPath = SK_FormatStringW(LR"(%ws\Assets\GOG\%i\)",   std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
        else
          targetPath = SK_FormatStringW(LR"(%ws\Assets\Steam\%i\)", std::wstring (path_cache.specialk_userdata.path).c_str(), appid);

        if (PathFileExists(targetPath.c_str()))
        {
          targetPath += L"cover";

          DeleteFile((targetPath + L".png").c_str());
          DeleteFile((targetPath + L".jpg").c_str());

          update = true;
        }
      }

      ImGui::Separator  (  );

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        if (pApp->store == "GOG")
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL, L"https://www.steamgriddb.com/search/grids?term=%ws", SK_UTF8ToWideChar(pApp->names.normal).c_str());
        else
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL, L"https://www.steamgriddb.com/steam/%lu", appid);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
              ImColor   (255, 255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );
      ImGui::TextColored (
              ImColor   (255, 255, 255, 255),
                ICON_FA_UNDO_ALT
                            );

      ImGui::Separator  (  );

      ImGui::TextColored (
              ImColor   (255, 255, 255, 255),
                ICON_FA_EXTERNAL_LINK_ALT
                            );

    }

    ImGui::EndPopup   (  );
  }

  // Special handling at the bottom for Special K
  if ( appid == SKIF_STEAM_APPID ) {
    float fY =
    ImGui::GetCursorPosY (                                                  );
    ImGui::SetCursorPos  (                           ImVec2 ( fX +    1.0f,
                                                              fY - (204.5f * SKIF_ImGui_GlobalDPIScale)) );
    ImGui::BeginGroup    ();
    static bool hoveredPatButton  = false,
                hoveredPatCredits = false;

    // Set all button styling to transparent
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0, 0, 0, 0));

    bool        clicked = 
    ImGui::ImageButton   ((ImTextureID)pPatTexSRV.p, ImVec2 (200.0F * SKIF_ImGui_GlobalDPIScale,
                                                             200.0F * SKIF_ImGui_GlobalDPIScale),
                                                     ImVec2 (0.f,       0.f),
                                                     ImVec2 (1.f,       1.f),     0,
                                                     ImVec4 (0, 0, 0, 0), // Use a transparent background
                                  hoveredPatButton ? ImVec4 (  1.f,  1.f,  1.f, 1.0f)
                                                   : ImVec4 (  .8f,  .8f,  .8f, .66f));

    // Restore the custom button styling
    ImGui::PopStyleColor (3);

    hoveredPatButton =
    ImGui::IsItemHovered (                                                  );

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");
    SKIF_ImGui_SetHoverTip  ("Click to help support the project");

    if (clicked)
      SKIF_Util_OpenURI (
        L"https://www.patreon.com/Kaldaien"
      );

    ImGui::SetCursorPos  (                           ImVec2 (379.5f * SKIF_ImGui_GlobalDPIScale,
                                                       fY - (204.0f  * SKIF_ImGui_GlobalDPIScale)) );

    ImGui::PushStyleColor     (ImGuiCol_ChildBg,        hoveredPatCredits ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
                                                                          : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(.8f, .8f, .8f, .66f));
    ImGui::BeginChild         ("###PatronsChild", ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale,
                                                          200.0f * SKIF_ImGui_GlobalDPIScale), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::TextColored        (ImVec4 (0.8f, 0.8f, 0.8f, 1.0f), "SpecialK Thanks to our Patrons:");

    extern std::string SKIF_GetPatrons (void);
    static std::string patrons_ =
      SKIF_GetPatrons () + '\0';

    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    
    ImGui::PushStyleColor     (ImGuiCol_Text,           ImVec4  (0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_ScrollbarBg,    ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImColor (0, 0, 0, 0).Value);
    ImGui::InputTextMultiline ("###Patrons", patrons_.data (), patrons_.length (),
                   ImVec2 (205.0f * SKIF_ImGui_GlobalDPIScale,
                           160.0f * SKIF_ImGui_GlobalDPIScale),
                                    ImGuiInputTextFlags_ReadOnly );
    ImGui::PopStyleColor      (4);

    hoveredPatCredits =
    ImGui::IsItemActive();

    ImGui::EndChild           ( );
    ImGui::PopStyleColor      ( );
    
    hoveredPatCredits = hoveredPatCredits ||
    ImGui::IsItemHovered      ( );

    ImGui::EndGroup           ( );
  }


  ImGui::EndGroup             ( );
  ImGui::SameLine             ( );

  if (update)
  {
    SKIF_GameManagement_ShowScreenshot (L"");

    update  = false;

    static
      app_record_s* pApp = nullptr;

    for (auto& app : apps)
      if (app.second.id == appid)
        pApp = &app.second;

    // SKIF
    if ( appid == SKIF_STEAM_APPID )
    {
      LoadLibraryTexture ( LibraryTexture::Cover,
                              SKIF_STEAM_APPID,
                                pTexSRV,
                                  L"_library_600x900_x2.jpg" );
    }

    // GOG
    else if ( pApp->store == "GOG" )
    {
      LoadLibraryTexture ( LibraryTexture::Cover,
                             appid,
                                  pTexSRV,
                                    L"*_glx_vertical_cover.webp",
                                      pApp );
    }
    
    // STEAM
    else if (pApp->store == "Steam") {

      if ( appinfo != nullptr )
      {
        skValveDataFile::appinfo_s *pAppInfo =
          appinfo->getAppInfo ( appid, nullptr );

        DBG_UNREFERENCED_LOCAL_VARIABLE (pAppInfo);
      }

      std::wstring load_str_2x (
        SK_GetSteamDir ()
      );
      std::wstring load_str
                  (load_str_2x);

      load_str_2x += LR"(/appcache/librarycache/)" +
        std::to_wstring  (appid)                   +
                                L"_library_600x900_x2.jpg";

      load_str   += LR"(/appcache/librarycache/)" +
        std::to_wstring (appid)                   +
                                L"_library_600x900.jpg";

      std::wstring load_str_final = L"_library_600x900.jpg";

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
            load_str_final = L"_library_600x900_x2.jpg";
          }
        }
      }

      // If 600x900_x2 exists, check the last modified time stamps
      else {
        WIN32_FILE_ATTRIBUTE_DATA faX1, faX2;

        if (GetFileAttributesEx (load_str.c_str(),    GetFileExInfoStandard, &faX1) &&
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

        load_str_final = L"_library_600x900_x2.jpg";
      }

      LoadLibraryTexture ( LibraryTexture::Cover,
                             appid,
                               pTexSRV,
                                 load_str_final,
                                   pApp );
    }
  }

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

    const  DWORD dwTimeout    = 425UL;
    static DWORD dwLastUpdate = SKIF_timeGetTime ();

    struct {
      std::string text = "";
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
        if (result.app_id != appid)
          manual_selection = result.app_id;
        result.app_id    = 0;
        result.text.clear ();
      }
    }
  };

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
        AppId_t     app_id  = 0;
        DWORD       running = 0;
        bool        service = false;
      } static cache;

      if (         cache.service != _inject.bCurrentState  ||
                   cache.running != pTargetApp->_status.running)
      {
        cache.app_id = 0;
      }

      if (pTargetApp->id              != cache.app_id ||
          pTargetApp->_status.running != cache.running )
      {
        cache.app_id  = pTargetApp->id;
        cache.running = pTargetApp->_status.running;

        cache.service = (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
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

        if (! PathFileExistsW (sk_install.config.file.c_str ()))
          cfg.shorthand.clear ();

        if (! PathFileExistsA (cache.dll.full_path.c_str ()))
          cache.dll.shorthand.clear ();

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
                         (cache.service)   ? "Service Running"
                                           : "Service Stopped";

              cache.injection.status.color =
                         (cache.service)   ? ImColor ( 53, 255,   3)  // HSV (0.3F,  0.99F, 1.F)
                                           : ImColor (255, 124,   3); // HSV (0.08F, 0.99F, 1.F);
              cache.injection.status.color_hover =
                         (cache.service)   ? ImColor (154, 255, 129)
                                           : ImColor (255, 189, 129);
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
          /*
          ShellExecuteW ( nullptr,
            L"OPEN", SK_UTF8ToWideChar(cache.config.full_path).c_str(),
                nullptr,   nullptr, SW_SHOWNORMAL
          );
          */

          SKIF_Util_OpenURI (SK_UTF8ToWideChar(cache.config.full_path).c_str(), SW_SHOWNORMAL);

          /* Cannot handle special characters such as (c), (r), etc
          SKIF_Util_OpenURI_Formatted (SW_SHOWNORMAL, L"%hs", cache.config.full_path.c_str ());
          */
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (cache.config.full_path.c_str ());
        //SKIF_ImGui_SetHoverTip        ("Open the config file");
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
        (quickServiceHover) ? cache.injection.status.color_hover
                            : cache.injection.status.color,
        cache.injection.status.text.empty () ?
                                    "      " : "( %s )",
        cache.injection.status.text.c_str ()
                                );

      quickServiceHover = ImGui::IsItemHovered ();

      if (cache.injection.type._Equal ("Global"))
      {
        if (ImGui::IsItemClicked ())
        {
          extern bool SKIF_bStopOnInjection;

          _inject._StartStopInject (cache.service, SKIF_bStopOnInjection);

          //_inject.run_lvl_changed = false;

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
                                                          : "( v %s )",
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

      std::string      buttonLabel = "Launch " + pTargetApp->type;
      ImGuiButtonFlags buttonFlags = ImGuiButtonFlags_None;

      if (pTargetApp->_status.running)
      {
          buttonLabel = "Running...";
          buttonFlags = ImGuiButtonFlags_Disabled;
          ImGui::PushStyleVar ( ImGuiStyleVar_Alpha,
              ImGui::GetStyle ().Alpha *
                 ((SKIF_IsHDR ()) ? 0.1f
                                  : 0.5f));
      }

      if ( ImGui::ButtonEx (
                  buttonLabel.c_str (),
                      ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                50.0f * SKIF_ImGui_GlobalDPIScale ), buttonFlags )
           ||
        clickedGameLaunch
           ||
        clickedGameLaunchWoSK )
      {
        bool isLocalBlacklisted  = false,
             isGlobalBlacklisted = false;

        // Launch preparations for non-Local installs
        if (! cache.injection.type._Equal ("Local"))
        {
          isLocalBlacklisted  = pTargetApp->launch_configs[0].isBlacklisted (pTargetApp->id),
          isGlobalBlacklisted = _inject._TestUserList (SK_WideCharToUTF8(pTargetApp->launch_configs[0].getExecutableFullPath(pTargetApp->id)).c_str(), false);

          // This captures two events -- launching through context menu + large button
          if (! clickedGameLaunchWoSK &&
              ! _inject.bCurrentState &&
              ! isLocalBlacklisted    &&
              ! isGlobalBlacklisted
             )
          {
            _inject._StartStopInject (false, true);
          }

          // Stop the service if the user attempts to launch without SK
          else if (  clickedGameLaunchWoSK &&
                    _inject.bCurrentState )
          {
            _inject._StartStopInject (true);
          }
        }

        // Launch game
        if (pTargetApp->store == "GOG")
        {
          // name of parent folder
          std::string  parentFolder     = std::filesystem::path(pTargetApp->launch_configs[0].executable).parent_path().filename().string();

          // Check if the path has been whitelisted, and parentFolder is at least a character in length
          if (
              ! isLocalBlacklisted  &&
              ! isGlobalBlacklisted &&
              ! _inject._TestUserList (SK_WideCharToUTF8(pTargetApp->launch_configs[0].executable).c_str(), true) &&
                parentFolder.length() > 0)
          {
            _inject._AddUserList(parentFolder, true);
            _inject._StoreList(true);
          }

          SHELLEXECUTEINFOW
          sexi              = { };
          sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
          sexi.lpVerb       = L"OPEN";
          sexi.lpFile       = pTargetApp->launch_configs[0].executable    .c_str();
          sexi.lpParameters = pTargetApp->launch_configs[0].launch_options.c_str();
          sexi.lpDirectory  = pTargetApp->launch_configs[0].working_dir   .c_str();
          sexi.nShow        = SW_SHOWDEFAULT;
          sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                              SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

          ShellExecuteExW (&sexi);
        }

        else {
          SKIF_Util_OpenURI_Threaded ((L"steam://run/" + std::to_wstring(pTargetApp->id)).c_str());
          pTargetApp->_status.invalidate();
        }

        clickedGameLaunch = clickedGameLaunchWoSK = false;
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

  static
    app_record_s *pApp = nullptr;

  ImGui::BeginChild ( "###AppListInset",
                        ImVec2 ( _WIDTH2,
                                 _HEIGHT ), true,
                            ImGuiWindowFlags_NavFlattened );
  ImGui::BeginGroup ();

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

  float f0 = ImGui::GetCursorPosY (  );
    ImGui::Selectable ("###zero", &dontcare, ImGuiSelectableFlags_Disabled);
  float f1 = ImGui::GetCursorPosY (  );
    ImGui::SameLine (                );
    ImGui::Image (nullptr, ImVec2 (_ICON_HEIGHT, _ICON_HEIGHT));
  float f2 = ImGui::GetCursorPosY (  );
             ImGui::SetCursorPosY (f0);

  float fOffset =
    ( std::max (f2, f1) - std::min (f2, f1) -
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

    bool selected = (appid == app.second.id);
    bool change   = false;

    app.second._status.refresh (&app.second);

    float fOriginalY =
      ImGui::GetCursorPosY ();


    // Start Icon + Selectable row

    ImGui::BeginGroup      ();
    ImGui::Image           (app.second.textures.icon.p,
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
                    ImColor::HSV (0.0f, 0.f, 1.f);

    // Game Title

    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::Selectable      ((app.first + "###" + app.second.store + std::to_string(app.second.id)).c_str(), &selected, ImGuiSelectableFlags_SpanAvailWidth);
    ImGui::PopStyleColor   (2                    );

    if ( ImGui::IsItemHovered        () &&
         ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) )
    {
      if ( pApp     != nullptr          &&
           pApp->id != SKIF_STEAM_APPID &&
         ! pApp->_status.running
        )
      {
        clickedGameLaunch = true;
      }
    }

    change |=
      _HandleItemSelection ();

    // Show full title in tooltip if the title is made up out of more than 48 characters.
    //   Use strlen(.c_str()) to strip \0 characters in the string that would otherwise also be counted.
    if (strlen(app.first.c_str()) > 48)
      SKIF_ImGui_SetHoverTip(app.first);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::EndGroup        ();

    // End Icon + Selectable row


    //change |=
    //  _HandleItemSelection ();

    if (manual_selection == app.second.id)
    {
      appid            = 0;
      manual_selection = 0;
      change           = true;
    }

    if ( app.second.id == appid &&
                   sort_changed &&
        (! ImGui::IsItemVisible ()) )
    {
      appid  = 0;
      change = true;
    }

    if (change)
    {
      if (appid != app.second.id) update = true;
      else                        update = false;

      appid      = app.second.id;
      selected   = true;

      if (update)
      {
        app.second._status.invalidate ();

        if (! ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
          // Activate the row of the current game
          ImGui::ActivateItem (ImGui::GetID ((app.first + "###" + app.second.store + std::to_string(app.second.id)).c_str()));

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

  // Stop populating the whole list


  if (update && pApp != nullptr)
  {
    // Launch button got changed, now we don't need this anymore.
#ifdef _USE_LOGO
    if (pApp->textures.logo.p == nullptr)
    {
      _LoadLibraryTexture (pApp->id,
                  pApp->textures.logo,
                              L"_logo.png");
    }
#endif

    // Handle GOG games

    if (pApp->store == "GOG")
    {
      extern path_cache_s path_cache;

      DWORD dwBinaryType = MAXDWORD;
      if ( GetBinaryTypeW (pApp->launch_configs[0].executable.c_str (), &dwBinaryType) )
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          pApp->specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          pApp->specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
      }

      std::wstring test_path =
        pApp->launch_configs[0].working_dir;

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

            break;
          }
        }
      }

      if (pApp->specialk.injection.config.type == ConfigType::Centralized)
        pApp->specialk.injection.config.dir =
          SK_FormatStringW(LR"(%ws\Profiles\%ws)",
            path_cache.specialk_userdata.path,
            pApp->specialk.profile_dir.c_str());

      pApp->specialk.injection.config.file =
        ( pApp->specialk.injection.config.dir + LR"(\)" ) +
          pApp->specialk.injection.config.file;

    } else {
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

      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE)       .x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UNDO_ALT)         .x, ImGui::GetTextLineHeight()));
      ImGui::Separator  (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_EXTERNAL_LINK_ALT).x, ImGui::GetTextLineHeight()));
      ImGui::PopStyleColor (  );

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Set Custom Icon",    dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
	      IFileOpenDialog  *pFileOpen;
        COMDLG_FILTERSPEC fileTypes{ L"Images", L"*.jpg;*.png;*.ico" };

	      PWSTR pszFilePath = NULL;

	      // Create the FileOpenDialog object.
	      HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		                    IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

	      if (SUCCEEDED(hr))
	      {
                pFileOpen->SetFileTypes(1, &fileTypes);
		      hr = pFileOpen->Show(NULL);

		      // Get the file name from the dialog box.
		      if (SUCCEEDED(hr))
		      {
			      IShellItem *pItem;
			      hr = pFileOpen->GetResult(&pItem);

			      if (SUCCEEDED(hr))
			      {
				      hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

              std::wstring targetPath = L"";
              std::wstring ext        = std::filesystem::path(pszFilePath).extension().wstring();

              if (pApp->id == SKIF_STEAM_APPID)
                targetPath = SK_FormatStringW(LR"(%ws\Assets\)",          std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
              else if (pApp->store == "GOG")
                targetPath = SK_FormatStringW(LR"(%ws\Assets\GOG\%i\)",   std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
              else
                targetPath = SK_FormatStringW(LR"(%ws\Assets\Steam\%i\)", std::wstring (path_cache.specialk_userdata.path).c_str(), appid);

              if (targetPath != L"")
              {
                std::filesystem::create_directories (targetPath);
                targetPath += L"icon";

                DeleteFile((targetPath + L".jpg").c_str());
                DeleteFile((targetPath + L".ico").c_str());
                DeleteFile((targetPath + L".png").c_str());

                CopyFile(pszFilePath, (targetPath + ext).c_str(), false);

                // Release current icon
                pApp->textures.icon.Release();

                // Reload the icon
                LoadLibraryTexture (LibraryTexture::Icon,
                                      appid,
                                        pApp->textures.icon,
                                         (pApp->store == "GOG")
                                          ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                          : L"_icon.jpg",
                                            pApp );
              }

				      pItem->Release();
			      }
		      }
		      pFileOpen->Release();
	      }
      }

      if (ImGui::Selectable ("Clear Custom Icon",  dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        std::wstring targetPath = L"";
        
        if (pApp->id == SKIF_STEAM_APPID)
          targetPath = SK_FormatStringW(LR"(%ws\Assets\)",          std::wstring (path_cache.specialk_userdata.path).c_str());
        else if (pApp->store == "GOG")
          targetPath = SK_FormatStringW(LR"(%ws\Assets\GOG\%i\)",   std::wstring (path_cache.specialk_userdata.path).c_str(), appid);
        else
          targetPath = SK_FormatStringW(LR"(%ws\Assets\Steam\%i\)", std::wstring (path_cache.specialk_userdata.path).c_str(), appid);

        if (PathFileExists(targetPath.c_str()))
        {
          targetPath += L"icon";
          
          DeleteFile ((targetPath + L".png").c_str());
          DeleteFile ((targetPath + L".jpg").c_str());
          DeleteFile ((targetPath + L".ico").c_str());

          // Release current icon
          pApp->textures.icon.Release();
          
          // Reload the icon
          LoadLibraryTexture (LibraryTexture::Icon,
                                pApp->id,
                                  pApp->textures.icon,
                                   (pApp->store == "GOG")
                                    ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                    : L"_icon.jpg",
                                        pApp );
        }
      }

      ImGui::Separator();

      if (ImGui::Selectable ("Browse SteamGridDB", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        if (pApp->store == "GOG")
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL, L"https://www.steamgriddb.com/search/grids?term=%ws", SK_UTF8ToWideChar(pApp->names.normal).c_str());
        else
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL, L"https://www.steamgriddb.com/steam/%lu", appid);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
              ImColor   (255, 255, 255, 255).Value,
                ICON_FA_FILE_IMAGE
                            );
      ImGui::TextColored (
              ImColor   (255, 255, 255, 255).Value,
                ICON_FA_UNDO_ALT
                            );

      ImGui::Separator   ( );

      ImGui::TextColored (
              ImColor   (255, 255,  255, 255).Value,
                ICON_FA_EXTERNAL_LINK_ALT
                            );
    }

    ImGui::EndPopup   (  );
  }

  ImGui::EndGroup   ();
  ImGui::EndChild   ();

  // Applies hover text on the whole AppListInset1
  //if (SKIF_StatusBarText.empty ()) // Prevents the text from overriding the keyboard search hint
    //SKIF_ImGui_SetHoverText ("Right click for more options");

  ImGui::BeginChild (
    "###AppListInset2",
      ImVec2 ( _WIDTH2,
               _HEIGHT2 ), true,
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NavFlattened
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

    if (pApp->extended_config.vac.enabled == 1)
    {
        SKIF_StatusBarText = "Warning: ";
        SKIF_StatusBarHelp = "Injection Disabled for VAC Protected Game";
    }

    if (! pApp->launch_configs.empty ())
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
        if (pApp->extended_config.vac.enabled == 1)
        {
          launch_cfg.setBlacklisted (pApp->id, true);
        }

        bool blacklist =
          launch_cfg.isBlacklisted (pApp->id);

        char          szButtonLabel [256] = { };
        if (menu)
          sprintf_s ( szButtonLabel, 255,
                        " for \"%ws\"###DisableLaunch%d",
                          launch_cfg.description.empty ()
                            ? launch_cfg.executable .c_str ()
                            : launch_cfg.description.c_str (),
                          launch_cfg.id);
        else
          sprintf_s ( szButtonLabel, 255,
                        " Disable Special K###DisableLaunch%d",
                          launch_cfg.id );

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
      };

      // Set horizontal position
      ImGui::SetCursorPosX (
        ImGui::GetCursorPosX  () +
        ImGui::GetColumnWidth () -
        ImGui::CalcTextSize   ("Disable Special K =>").x -
        ImGui::GetScrollX     () -
        ImGui::GetStyle       ().ItemSpacing.x * 2
      );

      // If there is only one launch option
      if ( pApp->launch_configs.size  () == 1 )
      {
        ImGui::SetCursorPosY (
          ImGui::GetCursorPosY () - 1.0f
        );

        _BlacklistCfg          (
             pApp->launch_configs.begin ()->second );
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
          for ( auto& launch : pApp->launch_configs )
          {
            if (! launch.second.valid)
              continue;

            _BlacklistCfg (launch.second, true);
          }

          ImGui::EndMenu       ();
        }
      }

      ImGui::EndChildFrame     ();

      fBottomDist = ImGui::GetItemRectSize().y;
    }
  }

  ImGui::EndGroup     (                  );
  ImGui::EndChild     (                  );
  ImGui::EndGroup     (                  );


  
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
        if ( ImGui::Selectable (
                    ("Launch " + pApp->type).c_str (),
                      false,  ( (pApp->_status.running != 0x0) ?
                                 ImGuiSelectableFlags_Disabled :
                                 ImGuiSelectableFlags_None )
                               )
           )
        {
          clickedGameLaunch = true;
        }

        if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
        {
          if (! _inject.bCurrentState)
            SKIF_ImGui_SetHoverText("Starts the global injection service as well.");

          ImGui::PushStyleColor ( ImGuiCol_Text,
            (ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f));

          if ( ImGui::Selectable (
                      ("Launch " + pApp->type + " without Special K").c_str(),
                        false,  ( (pApp->_status.running != 0x0) ?
                                   ImGuiSelectableFlags_Disabled :
                                   ImGuiSelectableFlags_None )
                                 )
             )
          {
            clickedGameLaunchWoSK = true;
          }

          ImGui::PopStyleColor    ( );
          if (_inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Stops the global injection service as well.");
        }

        extern std::wstring GOGGalaxy_Path;
        extern bool GOGGalaxy_Installed;

        if (GOGGalaxy_Installed && pApp->store == "GOG")
        {
          ImGui::PushStyleColor ( ImGuiCol_Text,
            (ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f));

          if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
          {
            ImGui::Separator ( );

            if (ImGui::BeginMenu ("Launch using GOG Galaxy"))
            {

              if (ImGui::Selectable(
                ("Launch " + pApp->type).c_str(),
                false, ((pApp->_status.running != 0x0) ?
                  ImGuiSelectableFlags_Disabled :
                  ImGuiSelectableFlags_None)
              )
                )
              {
                clickedGalaxyLaunch = true;
              }

              if (ImGui::Selectable(
                ("Launch " + pApp->type + " without Special K").c_str(),
                false, ((pApp->_status.running != 0x0) ?
                  ImGuiSelectableFlags_Disabled :
                  ImGuiSelectableFlags_None)
              )
                )
              {
                clickedGalaxyLaunchWoSK = true;
              }

              ImGui::EndMenu ( );
            }
          }

          else {
            if (ImGui::Selectable(
              "Launch using GOG Galaxy",
              false, ((pApp->_status.running != 0x0) ?
                ImGuiSelectableFlags_Disabled :
                ImGuiSelectableFlags_None)
            )
              )
            {
              clickedGalaxyLaunch = true;
            }
          }

          ImGui::PopStyleColor();

          if (clickedGalaxyLaunch ||
              clickedGalaxyLaunchWoSK)
          {

            if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
            {
              // name of parent folder
              std::string  parentFolder     = std::filesystem::path(pApp->launch_configs[0].executable).parent_path().filename().string();

              // Check if the path has been whitelisted, and parentFolder is at least a character in length
              if (! _inject._TestUserList (SK_WideCharToUTF8(pApp->launch_configs[0].executable).c_str(), true) && parentFolder.length() > 0)
              {
                _inject._AddUserList(parentFolder, true);
                _inject._StoreList(true);
              }
            
              if (clickedGalaxyLaunch && ! _inject.bCurrentState)
                _inject._StartStopInject (false, true);

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

        ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_BOOK_OPEN).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCORD)  .x, ImGui::GetTextLineHeight()));
        ImGui::Separator  (  );
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCOURSE).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_PATREON)  .x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_GITLAB)   .x, ImGui::GetTextLineHeight()));
        ImGui::PopStyleColor (  );

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
            L"https://discord.com/invite/ER4EDBJPTa"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discord.com/invite/ER4EDBJPTa");


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
               ImColor   (24, 118, 210, 255).Value,
                 ICON_FA_BOOK_OPEN
                             );
        ImGui::TextColored (
               ImColor   (114, 137, 218, 255).Value,
                 ICON_FA_DISCORD
                             );
        ImGui::TextColored (
               ImColor   (255, 249, 175, 255).Value,
                 ICON_FA_DISCOURSE
                             );
        ImGui::TextColored (
               ImColor   (249, 104,  84, 255).Value,
                 ICON_FA_PATREON
                             );
        ImGui::TextColored (
               ImColor   (226,  67,  40, 255).Value,
                 ICON_FA_GITLAB
                             );
      }

      if (pApp->store == "Steam")
        ImGui::Separator  ( );

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

      if (! pApp->cloud_saves.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_SD_CARD "\tGame Saves and Config");

        std::set <std::wstring> used_paths_;

        if (bMenuOpen)
        {
          if (! pApp->cloud_enabled)
          {
            ImGui::TextColored ( ImColor::HSV (0.08F, 0.99F, 1.F),
                                   "Developer forgot to enable Steam auto-cloud (!!)" );
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

              wsprintf ( sel_path, L"%ws",
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
            ImGui::TextColored (ImColor::HSV(0.0f, 0.0f, 0.75f), "N/A");
          }

          ImGui::EndMenu ();

        //SKIF_ImGui_SetHoverTip ("Browse files cloud-sync'd by Steam");
        }
      }

      if (! pApp->branches.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_CODE_BRANCH "\tSoftware Branches");

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
                               ImVec4 (0.7f, 0.7f, 0.7f, 1.f)
                                                 :
                               ImVec4 ( 1.f,  1.f,  1.f, 1.f)
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

      ImGui::PushStyleColor ( ImGuiCol_Text,
        (ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
      );

      if ( pApp->id != SKIF_STEAM_APPID || SKIF_STEAM_OWNER )
        ImGui::Separator ( );

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN) .x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TOOLS)       .x, ImGui::GetTextLineHeight()));

      if (pApp->store == "GOG")
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
      }

      else if (pApp->id != SKIF_STEAM_APPID || SKIF_STEAM_OWNER)
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

      if (ImGui::Selectable  ("Browse PCGamingWiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
               (pApp->store == "GOG") ? L"http://www.pcgamingwiki.com/api/gog.php?page=%lu"
                                      : L"http://www.pcgamingwiki.com/api/appid.php?appid=%lu",
                                        pApp->id
        );
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_FormatString (
            (pApp->store == "GOG") ? "http://www.pcgamingwiki.com/api/gog.php?page=%lu"
                                   : "http://www.pcgamingwiki.com/api/appid.php?appid=%lu",
                                     pApp->id
          )
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
      else if (pApp->id != SKIF_STEAM_APPID || SKIF_STEAM_OWNER)
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
               ImColor   (255, 229, 150, 255).Value,
                 ICON_FA_FOLDER_OPEN
                           );
      ImGui::TextColored (
               ImColor   (200, 200, 200, 255).Value,
                 ICON_FA_TOOLS
                           );
      
      if (pApp->store == "GOG")
      {
        ImGui::TextColored (
         ImColor   (155, 89, 182, 255).Value,
           ICON_FA_DATABASE );
      }

      else if (pApp->id != SKIF_STEAM_APPID || SKIF_STEAM_OWNER)
      {
        ImGui::TextColored (
         ImColor   (101, 192, 244, 255).Value,
           ICON_FA_DATABASE );

        ImGui::TextColored (
         ImColor   (255, 255, 255, 255).Value,
           ICON_FA_STEAM_SYMBOL );
      }

    }

    else if (! update)
    {
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
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
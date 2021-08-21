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
      bool  SKIF_STEAM_OWNER      = false;
static bool clickedGameLaunch     = false,
            clickedGameLaunchWoSK = false;

#include <SKIF.h>
#include <injection.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include "../DirectXTex/DirectXTex.h"

#include <font_awesome.h>

extern ID3D11Device* g_pd3dDevice;
extern float         SKIF_ImGui_GlobalDPIScale;
extern std::string   SKIF_StatusBarHelp;
extern std::string   SKIF_StatusBarText;
extern bool          SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);

#include <stores/Steam/apps_list.h>
#include <stores/Steam/asset_fetch.h>

static std::wstring sshot_file = L"";

HINSTANCE
SKIF_Util_ExplorePath (
  const std::wstring_view& path )
{
  return
    ShellExecuteW ( nullptr, L"EXPLORE",
      path.data (), nullptr,
                    nullptr, SW_SHOWNORMAL );
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

  return
    ShellExecuteW (
      nullptr, L"EXPLORE",
                _thread_localPath,
      nullptr,    nullptr,
             SW_SHOWNORMAL
                  );
}

HINSTANCE
SKIF_Util_OpenURI (
  const std::wstring_view& path,
               DWORD       dwAction )
{
  return
    ShellExecuteW ( nullptr, L"OPEN",
      path.data (), nullptr,
                    nullptr, dwAction );
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

  return
    ShellExecuteW ( nullptr,
      L"OPEN",  _thread_localPath,
         nullptr,   nullptr,   dwAction
                  );
}

#include <patreon.png.h>
#include <sk_icon.jpg.h>
#include <sk_boxart.png.h>

CComPtr <ID3D11Texture2D>          pPatTex2D;
CComPtr <ID3D11ShaderResourceView> pPatTexSRV;

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

  auto _LoadLibraryTexture =
    [&]( uint32_t                            appid,
         CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
         const std::wstring&                 name )
  {
    std::wstring load_str  (
      SK_GetSteamDir ()
    );

    load_str +=   LR"(\appcache\librarycache\)" +
      std::to_wstring (appid) + name.c_str ();

    if (
      SUCCEEDED (
        DirectX::LoadFromWICFile (
          load_str.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT,
              &meta, img
        )
      )
    )
    {
      if (
        SUCCEEDED (
          DirectX::CreateTexture (
            (ID3D11Device *)g_pd3dDevice,
              img.GetImages (), img.GetImageCount (),
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

  auto _LoadSKLibraryTexture =
    [&]( uint32_t                            appid,
         CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
         const std::wstring&                 name )
  {
    UNREFERENCED_PARAMETER(appid);
    UNREFERENCED_PARAMETER(name);

    if (
      SUCCEEDED (
        DirectX::LoadFromWICMemory (
          sk_boxart_png, sizeof (sk_boxart_png),
            DirectX::WIC_FLAGS_FILTER_POINT,
              &meta, img
        )
      )
    )
    {
      if (
        SUCCEEDED (
          DirectX::CreateTexture (
            (ID3D11Device *)g_pd3dDevice,
              img.GetImages (), img.GetImageCount (),
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

  //ImGui::DockSpace(ImGui::GetID("Foobar?!"), ImVec2(600, 900), ImGuiDockNodeFlags_KeepAliveOnly | ImGuiDockNodeFlags_NoResize);

  auto& io =
    ImGui::GetIO ();

  static float max_app_name_len = 640.0f / 2.0f;

  std::vector <AppId_t>
    SK_Steam_GetInstalledAppIDs (void);

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
      SKIF_record.install_dir =
        std::filesystem::current_path ();

      std::pair <std::string, app_record_s>
        SKIF ( "Special K", SKIF_record );

      apps.emplace_back (SKIF);
    }

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

      auto __LoadPatreonTexture =
      [&]( uint32_t                             appid,
           CComPtr <ID3D11ShaderResourceView>& kpPatTexSRV,
           const std::wstring&                  name
         )
      {
        UNREFERENCED_PARAMETER (appid);
        UNREFERENCED_PARAMETER  (name);

        if (
          SUCCEEDED (
            DirectX::LoadFromWICMemory (
              patreon_png, sizeof (patreon_png),
                DirectX::WIC_FLAGS_FILTER_POINT,
                  &local_meta, local_img
            )
          )
        )
        {
          DirectX::ScratchImage* pImg   =
                                     &local_img;
          DirectX::ScratchImage   converted_img;

          if (
            SUCCEEDED (
              DirectX::CreateTexture (
                (ID3D11Device *)g_pd3dDevice,
                  pImg->GetImages (), pImg->GetImageCount (),
                    local_meta, (ID3D11Resource **)&pPatTex2D.p
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

            if (   pPatTex2D  .p == nullptr ||
              FAILED (
                g_pd3dDevice->CreateShaderResourceView (
                   pPatTex2D  .p, &srv_desc,
                  &kpPatTexSRV.p
                )
              )
            )
            {
              pPatTexSRV.p = nullptr;
            }

            // SRV is holding a reference, this is not needed anymore.
            pPatTex2D.Release ();
          }
        }
      };

      SK_RunOnce (
        __LoadPatreonTexture (0, pPatTexSRV, L"(patreon.png)")
      );

      auto __LoadSKIconTexture =
      [&]( uint32_t                            appid,
           CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
           const std::wstring&                 name
         )
      {
        UNREFERENCED_PARAMETER (appid);
        UNREFERENCED_PARAMETER (name);

        if (
          SUCCEEDED (
            DirectX::LoadFromWICMemory (
              sk_icon_jpg, sizeof (sk_icon_jpg),
                DirectX::WIC_FLAGS_FILTER_POINT,
                  &local_meta, local_img
            )
          )
        )
        {
          DirectX::ScratchImage* pImg   =
                                     &local_img;
          DirectX::ScratchImage   converted_img;

          // We don't want single-channel icons, so convert to RGBA
          if (local_meta.format == DXGI_FORMAT_R8_UNORM)
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
            ) { local_meta =  converted_img.GetMetadata ();
                pImg       = &converted_img; }
          }

          if (
            SUCCEEDED (
              DirectX::CreateTexture (
                (ID3D11Device *)g_pd3dDevice,
                  pImg->GetImages (), pImg->GetImageCount (),
                    local_meta, (ID3D11Resource **)&pLocalTex2D.p
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

            if (   pLocalTex2D.p == nullptr ||
              FAILED (
                g_pd3dDevice->CreateShaderResourceView (
                   pLocalTex2D.p, &srv_desc,
                  &pLibTexSRV.p
                )
              )
            )
            {
              pLibTexSRV.p = nullptr;
            }

            // SRV is holding a reference, this is not needed anymore.
            pLocalTex2D.Release ();
          }
        }
      };

      auto __LoadLibraryTexture =
      [&]( uint32_t                            appid,
           CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
           const std::wstring&                 name
         )
      {
        std::wstring load_str  (
          SK_GetSteamDir ()
        );

        load_str +=   LR"(/appcache/librarycache/)" +
          std::to_wstring (appid)    +   name.c_str ();

        if (
          SUCCEEDED (
            DirectX::LoadFromWICFile (
              load_str.c_str (),
                DirectX::WIC_FLAGS_FILTER_POINT,
                  &local_meta, local_img
            )
          )
        )
        {
          DirectX::ScratchImage* pImg   =
                                     &local_img;
          DirectX::ScratchImage   converted_img;

          // We don't want single-channel icons, so convert to RGBA
          if (local_meta.format == DXGI_FORMAT_R8_UNORM)
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
            ) { local_meta =  converted_img.GetMetadata ();
                pImg       = &converted_img; }
          }

          if (
            SUCCEEDED (
              DirectX::CreateTexture (
                (ID3D11Device *)g_pd3dDevice,
                  pImg->GetImages (), pImg->GetImageCount (),
                    local_meta, (ID3D11Resource **)&pLocalTex2D.p
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

            if (   pLocalTex2D.p == nullptr ||
              FAILED (
                g_pd3dDevice->CreateShaderResourceView (
                   pLocalTex2D.p, &srv_desc,
                  &pLibTexSRV.p
                )
              )
            )
            {
              pLibTexSRV.p = nullptr;
            }

            // SRV is holding a reference, this is not needed anymore.
            pLocalTex2D.Release ();
          }
        }
      };


      for ( auto& app : apps )
      {
        // Special handling for non-Steam owners of Special K / SKIF
        if ( app.second.id == SKIF_STEAM_APPID )
          app.first = "Special K";

        // Regular handling for the remaining Steam games
        else {
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
        }

        // Corrupted app manifest / not known to Steam client; SKIP!
        if (app.first.empty ())
        {
          app.second.id = 0;
          continue;
        }

        if (app.second._status.installed)
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

        // Load Special K's icon from the embedded resource
        if ( app.second.id == SKIF_STEAM_APPID )
          __LoadSKIconTexture ( app.second.id,
                                app.second.textures.icon,
                                                 L"_icon.jpg"
        );

        // Load all other apps from the librarycache of the Steam client
        else
          __LoadLibraryTexture ( app.second.id,
                                 app.second.textures.icon,
                                                  L"_icon.jpg"
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
  ImGui::Image         ((ImTextureID)pTexSRV.p,    ImVec2 ( 600.0F * SKIF_ImGui_GlobalDPIScale,
                                                            900.0F * SKIF_ImGui_GlobalDPIScale ));

  // Special handling at the bottom for Special K
  if ( appid == SKIF_STEAM_APPID ) {
    float fY =
    ImGui::GetCursorPosY (                                                  );
    ImGui::SetCursorPosX (                                    fX            );
    ImGui::SetCursorPos  (                           ImVec2 ( fX,
                                                              fY - (204.f * SKIF_ImGui_GlobalDPIScale)) );
    ImGui::BeginGroup    ();
    static bool hovered = false;
    bool        clicked =
    ImGui::ImageButton   ((ImTextureID)pPatTexSRV.p, ImVec2 (200.0F * SKIF_ImGui_GlobalDPIScale,
                                                             200.0F * SKIF_ImGui_GlobalDPIScale),
                                                     ImVec2 (0.f,       0.f),
                                                     ImVec2 (1.f,       1.f),     0,
                                                     ImVec4 (.033f,.033f,.033f, 1.0f),
                                           hovered ? ImVec4 (  1.f,  1.f,  1.f, 1.0f)
                                                   : ImVec4 (  .8f,  .8f,  .8f, .66f));
    hovered =
    ImGui::IsItemHovered (                                                  );

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");
    SKIF_ImGui_SetHoverTip  ("Click to help support the project");

    if (clicked)
      SKIF_Util_OpenURI (L"https://www.patreon.com/Kaldaien");

    ImGui::SameLine           ( );
    ImGui::ItemSize           (ImVec2 (160.0f * SKIF_ImGui_GlobalDPIScale,
                                       200.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetTextLineHeightWithSpacing ()));
    ImGui::SameLine();
    ImGui::BeginGroup         ( );
    //ImGui::SetCursorPosY      (ImGui::GetCursorPosY () + 66.67f);
    ImGui::PushStyleColor     (ImGuiCol_Text, ImVec4 (0.8f, 0.8f, 0.8f, 1.0f));
    ImGui::TextUnformatted    ("SpecialK Thanks to our Patrons:");

    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    ImGui::Spacing            ( );
    ImGui::SameLine           ( );

    ImGui::PushStyleColor     (ImGuiCol_FrameBg, ImVec4 (0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor     (ImGuiCol_Text,    ImVec4 (0.6f, 0.6f, 0.6f, 1.0f));

    extern std::string SKIF_GetPatrons (void);
    static std::string patrons_ =
      SKIF_GetPatrons () + '\0';

    ImGui::InputTextMultiline ("###Patrons",patrons_.data (), patrons_.length (),
                   ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetStyle ().ItemSpacing.x * 3,
                           200.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetTextLineHeightWithSpacing () ),
                                    ImGuiInputTextFlags_ReadOnly );
    ImGui::PopStyleColor (                                                 3);
    ImGui::EndGroup      (                                                  );
    ImGui::EndGroup      (                                                  );
  }


  ImGui::EndGroup      (                                                  );
  ImGui::SameLine      (                                                  );

  if (update)
  {
    SKIF_GameManagement_ShowScreenshot (L"");

    update  = false;

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

    // Load Special K's boxart from the embedded resource
    if ( appid == SKIF_STEAM_APPID )
      _LoadSKLibraryTexture ( appid,
                                pTexSRV,
                                  L"_library_600x900_x2.jpg" );

    // Load all other apps from the librarycache of the Steam client
    else
    {

      // If 600x900 exists but 600x900_x2 cannot be found
      if (   PathFileExistsW (load_str.   c_str ()) &&
         ( ! PathFileExistsW (load_str_2x.c_str ()) ) )
      {
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
          if (meta.width  == 600 &&
              meta.height == 900)
          {
            // We have a 600x900 image, but it's auto-gen,
            //   just make a copy called 600x900_2x so we
            //     don't hit the server up for this image
            //       constantly.
            CopyFileW ( load_str.   c_str (),
                        load_str_2x.c_str (), TRUE );
          }
        }

        SKIF_HTTP_GetAppLibImg (appid, load_str_2x);
      }

      _LoadLibraryTexture ( appid,
                              pTexSRV,
                                L"_library_600x900_x2.jpg" );
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
#define _HEIGHT (620.0f * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x * SKIF_ImGui_GlobalDPIScale) //(float)_WIDTH / (fAspect)
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
      extern std::string SKIF_StatusBarText;
      extern std::string SKIF_StatusBarHelp;

      size_t len =
        strlen (test_);

      SKIF_StatusBarText = result.text.substr (0, len);
      SKIF_StatusBarHelp = result.text.substr (len, result.text.length () - len);
    }

    if (                       dwLastUpdate != MAXDWORD &&
         SKIF_timeGetTime () - dwLastUpdate >
                               dwTimeout )
    {
      if (result.app_id != 0)
      {
        *test_           = '\0';
        dwLastUpdate     = MAXDWORD;
        manual_selection = result.app_id;
        result.app_id    = 0;
        result.text.clear ();
      }
    }
  };

  _HandleKeyboardInput ();

  static std::string launch_description = "";

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
        bool        service = _inject.bCurrentState;
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
        cache.service = _inject.bCurrentState;

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
          case sk_install_state_s::Injection::Type::Global:
            launch_description = "Click to launch game (global injection)";

            if ( _inject.bHasServlet )
            {
              cache.injection.type         = "Global";
              cache.injection.status.text  =
                   _inject.bCurrentState   ? "Service Running"
                                           : "Service Stopped";

              cache.injection.status.color =
                   _inject.bCurrentState   ? ImColor::HSV (0.3F,  0.99F, 1.F)
                                           : ImColor::HSV (0.08F, 0.99F, 1.F);
              cache.injection.hover_text   =
                   _inject.bCurrentState   ? "Click to stop injection service"
                                           : "Click to start injection service";

            }
            /* Not needed any longer since the service autostarts when launching the game
            if (! _inject.bCurrentState)
              cache.dll.shorthand.clear ();
            else
            {
              launch_description = "Click to launch game (global injection)";
            }
            */
            break;

          case sk_install_state_s::Injection::Type::Local:
            cache.injection.type = "Local";
              launch_description = "Click to launch game (local injection)";
            break;

          // Unknown injection strategy, but let's assume global would work
          default:
            if (_inject.bHasServlet)
            {
              cache.injection.type = "Global";
              launch_description   = "Click to launch game (unknown; using global injection)";
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
        SKIF_ImGui_SetHoverTip        ("Open the config root folder");

        // Config File
        if (ImGui::Selectable         (cache.config.shorthand.c_str ()))
        {
          ShellExecuteW ( nullptr,
            L"OPEN", SK_UTF8ToWideChar(cache.config.full_path).c_str(),
                nullptr,   nullptr, SW_SHOWNORMAL
          );

          /* Cannot handle special characters such as (c), (r), etc
          SKIF_Util_OpenURI_Formatted (SW_SHOWNORMAL, L"%hs", cache.config.full_path.c_str ());
          */
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (cache.config.full_path.c_str ());
        SKIF_ImGui_SetHoverTip        ("Open the config file");
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
      ImGui::TextColored      (
        cache.injection.status.color,
        cache.injection.status.text.empty () ?
                                    "      " : "( %s )",
        cache.injection.status.text.c_str ()
                                );

      if (cache.injection.type._Equal ("Global"))
      {
        if (ImGui::IsItemClicked ())
        {
          _inject._StartStopInject (_inject.bCurrentState);

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

        // Launch preparations for non-Local installs
        if (! cache.injection.type._Equal ("Local"))
        {
          // This captures two events -- launching through context menu + large button
          if (! clickedGameLaunchWoSK &&
              !_inject.bCurrentState )
          {
            _inject._StartStopInject (false, true);
          }

          // Stop the service if the user attempts to launch without SK
          else if (  clickedGameLaunchWoSK &&
                    _inject.bCurrentState )
          {
            _inject._StartStopInject (true);
          }

          //ImGui::OpenPopup ("Confirm Launch");
        }

        // Launch game
        SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
          L"steam://run/%lu", pTargetApp->id
        );                    pTargetApp->_status.invalidate ();

        clickedGameLaunch = false;
        clickedGameLaunchWoSK = false;
      }

      if (pTargetApp->_status.running)
        ImGui::PopStyleVar ();

      else
        SKIF_ImGui_SetHoverTip (launch_description.c_str ());

      ImGui::SetNextWindowSize (
        ImVec2 (464.0f * SKIF_ImGui_GlobalDPIScale,
                  0.0f)
      );
      if (ImGui::BeginPopupModal ("Confirm Launch", nullptr,
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove   |
                                    ImGuiWindowFlags_AlwaysAutoResize)
         )
      {
        SKIF_ImGui_Spacing ();

        ImGui::TextColored (
          ImColor::HSV (0.11F, 1.F, 1.F),
            "      Special K will be unavailable in the game unless the global"
            "\n                             injection service is started."
        );

        SKIF_ImGui_Spacing ();

        if (ImGui::Button ("Start Service And Launch Game",
                             ImVec2 ( 0 * SKIF_ImGui_GlobalDPIScale,
                                     25 * SKIF_ImGui_GlobalDPIScale)
                          )
           )
        {
          _inject._StartStopInject (false, true);

          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"steam://run/%lu", pTargetApp->id
          );                    pTargetApp->_status.invalidate ();

          ImGui::CloseCurrentPopup ();
        }

        ImGui::SameLine ();
        ImGui::Spacing  ();
        ImGui::SameLine ();

        if ( ImGui::Button ( "Launch Game",
                               ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                         25 * SKIF_ImGui_GlobalDPIScale )
                           )
           )
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
               L"steam://run/%lu", pTargetApp->id
          );                       pTargetApp->_status.invalidate ();

          ImGui::CloseCurrentPopup ();
        }

        ImGui::SameLine ();
        ImGui::Spacing  ();
        ImGui::SameLine ();

        if ( ImGui::Button ( "Cancel",
                               ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                         25 * SKIF_ImGui_GlobalDPIScale )
                           )
           ) ImGui::CloseCurrentPopup ();
                      ImGui::EndPopup ();
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

  auto _HandleItemSelection = [&](void) ->
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

    if ( ! ImGui::IsPopupOpen ("GameContextMenu"))
    {
      if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
           _GamePadRightClick                            ||
           _NavLongActivate )
      {
        ImGui::OpenPopup      ("GameContextMenu");
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
           ImGui::GetStyle ().ItemSpacing.y / 2.0f ) / 2.0f;

  static bool deferred_update = false;


  // Start populating the whole list

  for (auto& app : apps)
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second.id == 0)
      continue;

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
      _HandleItemSelection ();

    ImGui::SameLine        ();

    ImVec4 _color =
      ( app.second._status.updating != 0x0 )
                  ? ImColor::HSV (0.6f, .6f, 1.f) :
      ( app.second._status.running  != 0x0 )
                  ? ImColor::HSV (0.3f, 1.f, 1.f) :
                    ImColor::HSV (0.0f, 0.f, 1.f);

    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::SetCursorPosY   (fOriginalY + fOffset );
    ImGui::Selectable      (app.first.c_str (), &selected, ImGuiSelectableFlags_SpanAvailWidth);
    ImGui::PopStyleColor   (                     );

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

    // Show full title in tooltip if the title is made up out of more than 48 characters.
    //   Use strlen(.c_str()) to strip \0 characters in the string that would otherwise also be counted.
    if (strlen(app.first.c_str()) > 48)
      SKIF_ImGui_SetHoverTip(app.first);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    change |=
      _HandleItemSelection ();

    ImGui::EndGroup        ();

    // End Icon + Selectable row


    change |=
      _HandleItemSelection ();

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

        ImGui::ActivateItem (ImGui::GetID (app.first.c_str ()));

        if (! ImGui::IsItemVisible    (    )) {
          ImGui::SetScrollHereY       (0.5f * SKIF_ImGui_GlobalDPIScale);
        } ImGui::SetKeyboardFocusHere (    );

        deferred_update = true;
      }
    }

    if (selected)
    {
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
      }

      // Special K is selected -- relevant show quick links
      else
      {
        ImGui::BeginGroup  ( );
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
        ImGui::EndGroup   (  );
        ImGui::SameLine   (  );
        ImGui::BeginGroup (  );

        if (ImGui::Selectable ("Wiki"))
        {
          SKIF_Util_OpenURI (
            L"https://wiki.special-k.info/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/");


        if (ImGui::Selectable ("Discord"))
        {
          SKIF_Util_OpenURI (
            L"https://discord.com/invite/ER4EDBJPTa"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discord.com/invite/ER4EDBJPTa");


        if (ImGui::Selectable ("Forum"))
        {
          SKIF_Util_OpenURI (
            L"https://discourse.differentk.fyi/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discourse.differentk.fyi/");


        if (ImGui::Selectable ("Patreon"))
        {
          SKIF_Util_OpenURI (
            L"https://www.patreon.com/Kaldaien"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://www.patreon.com/Kaldaien");

        if (ImGui::Selectable ("GitLab"))
        {
          SKIF_Util_OpenURI (
            L"https://gitlab.special-k.info/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://gitlab.special-k.info/");

        ImGui::EndGroup   ( );
        ImGui::Separator  ( );
      }

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
      ImGui::TextColored (
               ImColor   (255, 229, 150, 255).Value,
                 ICON_FA_FOLDER_OPEN
                           );
      ImGui::TextColored (
               ImColor   (200, 200, 200, 255).Value,
                 ICON_FA_TOOLS
                           );
      if (pApp->id != SKIF_STEAM_APPID || SKIF_STEAM_OWNER)
      {
        ImGui::TextColored (
         ImColor   (101, 192, 244, 255).Value,
           ICON_FA_DATABASE );
      }
      ImGui::EndGroup    ( );
      ImGui::SameLine    ( );
      ImGui::BeginGroup  ( );
      if (ImGui::Selectable ("Browse Install Folder"))
      {
        SKIF_Util_ExplorePath         (
          pApp->install_dir             );
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (pApp->install_dir)
                                        );
      }

      if (ImGui::Selectable  ("Browse PCGamingWiki"))
      {
        SKIF_Util_OpenURI_Formatted   ( SW_SHOWNORMAL,
               L"http://pcgamingwiki.com/api/appid.php?appid=%lu", pApp->id
                                        );
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_FormatString (
            "http://pcgamingwiki.com/api/appid.php?appid=%lu", pApp->id
                          )             );
      }

      if (pApp->id != SKIF_STEAM_APPID || SKIF_STEAM_OWNER)
      {
        if (ImGui::Selectable  ("Browse SteamDB"))
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
      }
      ImGui::EndGroup      ( );
      ImGui::PopStyleColor ( );
    }

    else if (! update)
    {
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }

  ImGui::EndGroup   ();
  ImGui::EndChild   ();

  // Applies hover text on the whole AppListInset1
  if (SKIF_StatusBarText.empty ()) // Prevents the text from overriding the keyboard search hint
    SKIF_ImGui_SetHoverText ("Right click for more details");

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

    if (pApp->extended_config.vac.enabled)
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
        if (pApp->extended_config.vac.enabled)
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

      SK_RunOnce(fBottomDist = ImGui::GetItemRectSize().y);
    }
  }


  ImGui::EndGroup     (                  );
  ImGui::EndChild     (                  );
  ImGui::EndGroup     (                  );
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
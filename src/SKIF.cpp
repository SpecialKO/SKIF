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

const int SKIF_STEAM_APPID = 1157970;
int WindowsCursorSize = 1;

#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L

bool SKIF_bDPIScaling = false,
     SKIF_bDisableExitConfirmation = false,
     SKIF_bDisableTooltips = false;

#include <SKIF.h>

#include <stores/Steam/library.h>

#include <injection.h>
#include "version.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <dxgi1_6.h>

#include <fsutil.h>

#include <fstream>
#include <typeindex>

#pragma comment (lib, "winmm.lib")

#define SK_BORDERLESS ( WS_VISIBLE | WS_POPUP        | WS_MINIMIZEBOX | \
                        WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS  )

#define SK_BORDERLESS_EX      ( WS_EX_APPWINDOW | WS_EX_NOACTIVATE )
#define SK_BORDERLESS_WIN8_EX ( SK_BORDERLESS_EX | WS_EX_NOREDIRECTIONBITMAP )

#define SK_FULLSCREEN_X GetSystemMetrics (SM_CXFULLSCREEN)
#define SK_FULLSCREEN_Y GetSystemMetrics (SM_CYFULLSCREEN)

#define GCL_HICON           (-14)

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

BOOL
SKIF_IsWindows8Point1OrGreater (void)
{
  SK_RunOnce (
    SetLastError (NO_ERROR)
  );

  static BOOL
    bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "GetSystemTimePreciseAsFileTime"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

BOOL
SKIF_IsWindows10OrGreater (void)
{
  SK_RunOnce (
    SetLastError (NO_ERROR)
  );

  static BOOL
    bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "SetThreadDescription"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

#include <dwmapi.h>

HRESULT
WINAPI
SK_DWM_GetCompositionTimingInfo (DWM_TIMING_INFO *pTimingInfo)
{
  static HMODULE hModDwmApi =
    LoadLibraryW (L"dwmapi.dll");

  typedef HRESULT (WINAPI *DwmGetCompositionTimingInfo_pfn)(
                   HWND             hwnd,
                   DWM_TIMING_INFO *pTimingInfo);

  static                   DwmGetCompositionTimingInfo_pfn
                           DwmGetCompositionTimingInfo =
         reinterpret_cast <DwmGetCompositionTimingInfo_pfn> (
      GetProcAddress ( hModDwmApi,
                          "DwmGetCompositionTimingInfo" )   );

  pTimingInfo->cbSize =
    sizeof (DWM_TIMING_INFO);

  return
    DwmGetCompositionTimingInfo ( 0, pTimingInfo );
}


float fAspect     = 16.0f / 9.0f;
float fBottomDist = 0.0f;

#define WM_DXGI_OCCLUSION WM_USER
#include "imgui/d3d11/imgui_impl_dx11.h"
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
ID3D11Device*           g_pd3dDevice           = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
IDXGISwapChain*         g_pSwapChain           = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
BOOL                    bOccluded              =   FALSE;

// Forward declarations of helper functions
bool CreateDeviceD3D           (HWND hWnd);
void CleanupDeviceD3D          (void);
void CreateRenderTarget        (void);
void CleanupRenderTarget       (void);
void ResizeSwapChain           (HWND hWnd, int width, int height);
LRESULT WINAPI
     WndProc                   (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class SKIF_AutoWndClass {
public:
   SKIF_AutoWndClass (WNDCLASSEX wc) : wc_ (wc) { };
  ~SKIF_AutoWndClass (void)
  {
    UnregisterClass ( wc_.lpszClassName,
                      wc_.hInstance );
  }

private:
  WNDCLASSEX wc_;
};


int __width  = 0;
int __height = 0;



#pragma comment (lib, "wininet.lib")



float SKIF_ImGui_GlobalDPIScale = 1.0f;

std::string SKIF_StatusBarText;
std::string SKIF_StatusBarHelp;
HWND        SKIF_hWnd;

CONDITION_VARIABLE SKIF_IsFocused    = { };
CONDITION_VARIABLE SKIF_IsNotFocused = { };

extern bool SKIF_ImGui_IsFocused (void);

bool SKIF_ImGui_IsHoverable (void)
{
  if (! SKIF_ImGui_IsFocused ())
    return false;

  return true;
}

void SKIF_ImGui_SetMouseCursorHand (void)
{
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

void SKIF_ImGui_SetHoverTip  (const char* szText)
{
  if (SKIF_ImGui_IsHoverable () && ! SKIF_bDisableTooltips)
  {
      if (ImGui::IsItemHovered())
      {
          auto& io    = ImGui::GetIO();

          ImVec2 cursorPos = io.MousePos;
          int cursorScale = WindowsCursorSize;
          ImVec2 tooltip_pos = ImVec2(cursorPos.x + 16 + 4 * (cursorScale - 1), cursorPos.y + 8 /* 16 + 4 * (cursorScale - 1) */ );
          ImGui::SetNextWindowPos(tooltip_pos);
          
          ImGui::SetTooltip(szText);
      }
  }
}

void SKIF_ImGui_SetHoverText (const char* szText)
{
  if (ImGui::IsItemHovered () && SKIF_StatusBarText.empty ())
    SKIF_StatusBarText = szText;
}

const ImWchar*
SK_ImGui_GetGlyphRangesDefaultEx (void)
{
  static const ImWchar ranges [] =
  {
    0x0020, 0x00FF, // Basic Latin + Latin Supplement
    0x0100, 0x03FF, // Latin, IPA, Greek
    0x2000, 0x206F, // General Punctuation
    0x2100, 0x21FF, // Letterlike Symbols
    0x2600, 0x26FF, // Misc. Characters
    0x2700, 0x27BF, // Dingbats
    0x207f, 0x2090, // N/A (literally, the symbols for N/A :P)
    0xc2b1, 0xc2b3, // �
    0
  };
  return &ranges [0];
}

// Fixed-width font
#define SK_IMGUI_FIXED_FONT 1

class SK_ImGui_AutoFont {
public:
   SK_ImGui_AutoFont (ImFont* pFont);
  ~SK_ImGui_AutoFont (void);

  bool Detach (void);

protected:
  ImFont* font_ = nullptr;
};

SK_ImGui_AutoFont::SK_ImGui_AutoFont (ImFont* pFont)
{
  if (pFont != nullptr)
  {
    ImGui::PushFont (pFont);
    font_ = pFont;
  }
}

SK_ImGui_AutoFont::~SK_ImGui_AutoFont (void)
{
  Detach ();
}

bool
SK_ImGui_AutoFont::Detach (void)
{
  if (font_ != nullptr)
  {
    font_ = nullptr;
    ImGui::PopFont ();

    return true;
  }

  return false;
}

auto SKIF_ImGui_LoadFont =
  [&]( const std::wstring& filename,
             float         point_size,
       const ImWchar*      glyph_range,
             ImFontConfig* cfg = nullptr )
{
  auto& io =
    ImGui::GetIO ();

  wchar_t wszFullPath [ MAX_PATH + 2 ] = { };

  if (GetFileAttributesW (              filename.c_str ()) != INVALID_FILE_ATTRIBUTES)
     wcsncpy_s ( wszFullPath, MAX_PATH, filename.c_str (),
                             _TRUNCATE );

  else
  {
    wchar_t     wszFontsDir [MAX_PATH] = { };
    wcsncpy_s ( wszFontsDir, MAX_PATH,
             SK_GetFontsDir ().c_str (), _TRUNCATE );

    PathCombineW ( wszFullPath,
                   wszFontsDir, filename.c_str () );

    if (GetFileAttributesW (wszFullPath) == INVALID_FILE_ATTRIBUTES)
      *wszFullPath = L'\0';
  }

  if (*wszFullPath != L'\0')
  {
    return
      io.Fonts->AddFontFromFileTTF ( SK_WideCharToUTF8 (wszFullPath).c_str (),
                                       point_size,
                                         cfg,
                                           glyph_range );
  }

  return (ImFont *)nullptr;
};

#include <imgui/imgui_internal.h>

auto SKIF_ImGui_InitFonts = [&](void)
{
  auto& io =
    ImGui::GetIO ();

  extern ImGuiContext *GImGui;

  if (io.Fonts != nullptr)
  {
    if (GImGui->FontAtlasOwnedByContext)
    {
      if (GImGui->Font != nullptr)
      {
        GImGui->Font->ClearOutputData ();

        if (GImGui->Font->ContainerAtlas != nullptr)
            GImGui->Font->ContainerAtlas->Clear ();
      }

      io.FontDefault = nullptr;

      IM_DELETE (io.Fonts);
                 io.Fonts = IM_NEW (ImFontAtlas)();
    }
  }

  ImFontConfig
  font_cfg           = {  };
  font_cfg.MergeMode = true;

  if (! SKIF_ImGui_LoadFont (
          L"Tahoma.ttf",
            18.0F,
              SK_ImGui_GetGlyphRangesDefaultEx () ) )
  {
    io.Fonts->AddFontDefault ();
  }

  else
    io.Fonts->AddFontDefault ();
};



namespace SKIF
{
  namespace WindowsRegistry
  {
    template <class _Tp>
    class KeyValue
    {
      struct KeyDesc {
        HKEY         hKey                 = HKEY_CURRENT_USER;
        wchar_t    wszSubKey   [MAX_PATH] =               L"";
        wchar_t    wszKeyValue [MAX_PATH] =               L"";
        DWORD        dwType               =          REG_NONE;
        DWORD        dwFlags              =        RRF_RT_ANY;
      };

    public:
      _Tp getData (void)
      {
        auto _GetValue =
        [&]( _Tp*     pVal,
               DWORD* pLen = nullptr ) ->
        LSTATUS
        {
          LSTATUS lStat =
            RegGetValueW ( _desc.hKey,
                             _desc.wszSubKey,
                               _desc.wszKeyValue,
                               _desc.dwFlags,
                                 &_desc.dwType,
                                   pVal, pLen );

          return lStat;
        };

        auto _SizeofData =
        [&](void) ->
        DWORD
        {
          DWORD len = 0;

          if ( ERROR_SUCCESS ==
                 _GetValue ( nullptr, &len )
             ) return len;

          return 0;
        };

        _Tp   out;
        DWORD dwOutLen;

        auto type_idx =
          std::type_index (typeid (_Tp));

        //if ( type_idx == std::type_index (typeid (std::wstring)) )
        //{
        //  std::wstring _out;
        //
        //  _out.resize (_SizeofData () + 1);
        //
        //  if ( ERROR_SUCCESS !=
        //         _GetValue   (_out.data ()) ) out = _Tp ();
        //
        //  return out;
        //}

        if ( type_idx == std::type_index (typeid (bool)) )
        {
          _desc.dwType = REG_BINARY;
              dwOutLen = sizeof (bool);
        }

        if ( type_idx == std::type_index (typeid (int)) )
        {
          _desc.dwType = REG_DWORD;
              dwOutLen = sizeof (int);
        }

        if ( type_idx == std::type_index (typeid (float)) )
        {
          _desc.dwFlags = RRF_RT_REG_BINARY;
          _desc.dwType  = REG_BINARY;
               dwOutLen = sizeof (float);
        }

        if ( ERROR_SUCCESS !=
               _GetValue (&out, &dwOutLen) ) out = _Tp ();

        return out;
      };

      _Tp putData (_Tp in)
      {
        auto _SetValue =
        [&]( _Tp*    pVal,
             LPDWORD pLen = nullptr ) ->
        LSTATUS
        {
          LSTATUS lStat         = STATUS_INVALID_DISPOSITION;
          HKEY    hKeyToSet     = 0;
          DWORD   dwDisposition = 0;
          DWORD   dwDataSize    = 0;

          lStat =
            RegCreateKeyExW (
              _desc.hKey,
                _desc.wszSubKey,
                  0x00, nullptr,
                    REG_OPTION_NON_VOLATILE,
                    KEY_ALL_ACCESS, nullptr,
                      &hKeyToSet, &dwDisposition );

          auto type_idx =
            std::type_index (typeid (_Tp));

            //case std::type_index (std::wstring)
            //{
            //  auto& _out =
            //    dynamic_cast <std::wstring> (out);
            //
            //  _out.resize (_SizeofData () + 1);
            //
            //  if ( ERROR_SUCCESS !=
            //         _GetValue   (_out) ) out = _Tp ();
            //
            //  return out;
            //} break;

          if ( type_idx == std::type_index (typeid (bool)) )
          {
            _desc.dwType     = REG_BINARY;
                  dwDataSize = sizeof (bool);
          }

          if ( type_idx == std::type_index (typeid (int)) )
          {
            _desc.dwType     = REG_DWORD;
                  dwDataSize = sizeof (int);
          }

          if ( type_idx == std::type_index (typeid (float)) )
          {
            _desc.dwFlags    = RRF_RT_DWORD;
            _desc.dwType     = REG_BINARY;
                  dwDataSize = sizeof (float);
          }

          lStat =
            RegSetKeyValueW ( hKeyToSet,
                                nullptr,
                                _desc.wszKeyValue,
                                _desc.dwType,
                                  pVal, dwDataSize );

          RegCloseKey (hKeyToSet);

          UNREFERENCED_PARAMETER (pLen);

          return lStat;
        };

        if ( ERROR_SUCCESS == _SetValue (&in) )
          return in;

        return _Tp ();
      };

      static KeyValue <typename _Tp>
         MakeKeyValue ( const wchar_t *wszSubKey,
                        const wchar_t *wszKeyValue,
                        HKEY           hKey    = HKEY_CURRENT_USER,
                        LPDWORD        pdwType = nullptr,
                        DWORD          dwFlags = RRF_RT_ANY )
      {
        KeyValue <_Tp> kv;

        wcsncpy_s ( kv._desc.wszSubKey, MAX_PATH,
                             wszSubKey, _TRUNCATE );

        wcsncpy_s ( kv._desc.wszKeyValue, MAX_PATH,
                             wszKeyValue, _TRUNCATE );

        kv._desc.hKey    = hKey;
        kv._desc.dwType  = ( pdwType != nullptr ) ?
                                         *pdwType : REG_NONE;
        kv._desc.dwFlags = dwFlags;

        return kv;
      }

    protected:
    private:
      KeyDesc _desc;
    };
  };

#define SKIF_MakeRegKeyF  SKIF::WindowsRegistry::KeyValue <float>::MakeKeyValue
#define SKIF_MakeRegKeyB  SKIF::WindowsRegistry::KeyValue <bool>::MakeKeyValue
#define SKIF_MakeRegKeyI  SKIF::WindowsRegistry::KeyValue <int>::MakeKeyValue
#define SKIF_MakeRegKeyWS SKIF::WindowsRegistry::KeyValue <std::wstring>::MakeKeyValue
};

std::string
SKIF_GetPatrons (void)
{
  FILE *fPatrons =
    _wfopen (L"patrons.txt", L"rb");

  if (fPatrons != nullptr)
  {
    std::string out;

    fseek (fPatrons, 0, SEEK_END);

    size_t size =
      ftell (fPatrons);
    rewind  (fPatrons);

    out.resize (size);

    fread (out.data (), size, 1, fPatrons);
           out += '\0';
    return out;
  }

  return "";
}


#include <wtypes.h>
#include <WinInet.h>

#include <gsl/gsl>
#include <comdef.h>

struct skif_version_info_t {
  wchar_t wszHostName  [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath  [INTERNET_MAX_PATH_LENGTH]      = { };
  std::wstring
          product                                      = L"SKIF";
  std::wstring
          branch                                       = L"Default";
  int     build                                        =   0;
  bool    success                                      = false;
};

struct skif_patron_info_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH]      = { };
  bool    success                                     = false;
};

struct SKIF_VersionControl {
  DWORD WINAPI Runner            (skif_version_info_t *get);
  void         SetCheckFrequency (const wchar_t       *wszProduct,
                                        int            minutes);
  uint32_t     GetCheckFrequency (const wchar_t       *wszProduct);
  bool         CheckForUpdates   (const wchar_t       *wszProduct,
                                        int            local_build  = 0,
                                        int            remote_build = 0,
                                        const wchar_t *wszBranch    = nullptr,
                                        bool           force        = false);
  DWORD WINAPI PatronRunner      (skif_patron_info_t *get);
} SKIF_VersionCtl;

DWORD
WINAPI
SKIF_VersionControl::Runner (skif_version_info_t* get)
{
  ULONG ulTimeout = 125UL;

  PCWSTR rgpszAcceptTypes [] = { L"*/*", nullptr };
  HINTERNET hInetHTTPGetReq  = nullptr,
            hInetHost        = nullptr,
  hInetRoot                  =
    InternetOpen (
      L"Special K - Version Check",
        INTERNET_OPEN_TYPE_DIRECT,
          nullptr, nullptr,
            0x00
    );

  // (Cleanup On Error)
  auto CLEANUP = [&](bool clean = false) ->
  DWORD
  {
    if (! clean)
    {
      DWORD dwLastError =
           GetLastError ();

      OutputDebugStringW (
        ( std::wstring (L"WinInet Failure (") +
              std::to_wstring (dwLastError)   +
          std::wstring (L"): ")               +
                 _com_error   (dwLastError).ErrorMessage ()
        ).c_str ()
      );
    }

    if (hInetHTTPGetReq != nullptr) InternetCloseHandle (hInetHTTPGetReq);
    if (hInetHost       != nullptr) InternetCloseHandle (hInetHost);
    if (hInetRoot       != nullptr) InternetCloseHandle (hInetRoot);

    return 0;
  };

  if (hInetRoot == nullptr)
    return CLEANUP ();

  DWORD_PTR dwInetCtx = 0;

  hInetHost =
    InternetConnect ( hInetRoot,
                        get->wszHostName,
                          INTERNET_DEFAULT_HTTP_PORT,
                            nullptr, nullptr,
                              INTERNET_SERVICE_HTTP,
                                0x00,
                                  (DWORD_PTR)&dwInetCtx );

  if (hInetHost == nullptr)
  {
    return CLEANUP ();
  }

  hInetHTTPGetReq =
    HttpOpenRequest ( hInetHost,
                        nullptr,
                          get->wszHostPath,
                            L"HTTP/1.1",
                              nullptr,
                                rgpszAcceptTypes,
                                                                    INTERNET_FLAG_IGNORE_CERT_DATE_INVALID |
                                  INTERNET_FLAG_CACHE_IF_NET_FAIL | INTERNET_FLAG_IGNORE_CERT_CN_INVALID   |
                                  INTERNET_FLAG_RESYNCHRONIZE     | INTERNET_FLAG_CACHE_ASYNC,
                                    (DWORD_PTR)&dwInetCtx );


  // Wait 125 msecs for a dead connection, then give up
  //
  InternetSetOptionW ( hInetHTTPGetReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                         &ulTimeout,    sizeof (ULONG) );


  if (hInetHTTPGetReq == nullptr)
  {
    return CLEANUP ();
  }

  if ( HttpSendRequestW ( hInetHTTPGetReq,
                            nullptr,
                              0,
                                nullptr,
                                  0 ) )
  {
    DWORD dwStatusCode        = 0;
    DWORD dwStatusCode_Len    = sizeof (DWORD);

    DWORD dwContentLength     = 0;
    DWORD dwContentLength_Len = sizeof (DWORD);
    DWORD dwSizeAvailable;

    HttpQueryInfo ( hInetHTTPGetReq,
                     HTTP_QUERY_STATUS_CODE |
                     HTTP_QUERY_FLAG_NUMBER,
                       &dwStatusCode,
                         &dwStatusCode_Len,
                           nullptr );

    if (dwStatusCode == 200)
    {
      HttpQueryInfo ( hInetHTTPGetReq,
                        HTTP_QUERY_CONTENT_LENGTH |
                        HTTP_QUERY_FLAG_NUMBER,
                          &dwContentLength,
                            &dwContentLength_Len,
                              nullptr );

      std::vector <char> http_chunk;
      std::vector <char> concat_buffer;

      while ( InternetQueryDataAvailable ( hInetHTTPGetReq,
                                             &dwSizeAvailable,
                                               0x00, NULL )
        )
      {
        if (dwSizeAvailable > 0)
        {
          DWORD dwSizeRead = 0;

          if (http_chunk.size () < dwSizeAvailable)
              http_chunk.resize   (dwSizeAvailable);

          if ( InternetReadFile ( hInetHTTPGetReq,
                                    http_chunk.data (),
                                      dwSizeAvailable,
                                        &dwSizeRead )
             )
          {
            if (dwSizeRead == 0)
              break;

            concat_buffer.insert ( concat_buffer.cend   (),
                                    http_chunk.cbegin   (),
                                      http_chunk.cbegin () + dwSizeRead );

            if (dwSizeRead < dwSizeAvailable)
              break;
          }
        }

        else
          break;
      }

      concat_buffer.push_back ('\0');

      get->build =
        std::atoi (concat_buffer.data ());
    }
  }

  CLEANUP (true);

  return 1;
}

DWORD
WINAPI
SKIF_VersionControl::PatronRunner (skif_patron_info_t* get)
{
  ULONG ulTimeout = 125UL;

  PCWSTR rgpszAcceptTypes [] = { L"*/*", nullptr };
  HINTERNET hInetHTTPGetReq  = nullptr,
            hInetHost        = nullptr,
  hInetRoot                  =
    InternetOpen (
      L"Special K - Version Check",
        INTERNET_OPEN_TYPE_DIRECT,
          nullptr, nullptr,
            0x00
    );

  // (Cleanup On Error)
  auto CLEANUP = [&](bool clean = false) ->
  DWORD
  {
    if (! clean)
    {
      DWORD dwLastError =
           GetLastError ();

      OutputDebugStringW (
        ( std::wstring (L"WinInet Failure (") +
              std::to_wstring (dwLastError)   +
          std::wstring (L"): ")               +
                 _com_error   (dwLastError).ErrorMessage ()
        ).c_str ()
      );
    }

    if (hInetHTTPGetReq != nullptr) InternetCloseHandle (hInetHTTPGetReq);
    if (hInetHost       != nullptr) InternetCloseHandle (hInetHost);
    if (hInetRoot       != nullptr) InternetCloseHandle (hInetRoot);

    return 0;
  };

  if (hInetRoot == nullptr)
    return CLEANUP ();

  DWORD_PTR dwInetCtx = 0;

  hInetHost =
    InternetConnect ( hInetRoot,
                        get->wszHostName,
                          INTERNET_DEFAULT_HTTP_PORT,
                            nullptr, nullptr,
                              INTERNET_SERVICE_HTTP,
                                0x00,
                                  (DWORD_PTR)&dwInetCtx );

  if (hInetHost == nullptr)
  {
    return CLEANUP ();
  }

  hInetHTTPGetReq =
    HttpOpenRequest ( hInetHost,
                        nullptr,
                          get->wszHostPath,
                            L"HTTP/1.1",
                              nullptr,
                                rgpszAcceptTypes,
                                                                    INTERNET_FLAG_IGNORE_CERT_DATE_INVALID |
                                  INTERNET_FLAG_CACHE_IF_NET_FAIL | INTERNET_FLAG_IGNORE_CERT_CN_INVALID   |
                                  INTERNET_FLAG_RESYNCHRONIZE     | INTERNET_FLAG_CACHE_ASYNC,
                                    (DWORD_PTR)&dwInetCtx );


  // Wait 125 msecs for a dead connection, then give up
  //
  InternetSetOptionW ( hInetHTTPGetReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                         &ulTimeout,    sizeof (ULONG) );


  if (hInetHTTPGetReq == nullptr)
  {
    return CLEANUP ();
  }

  if ( HttpSendRequestW ( hInetHTTPGetReq,
                            nullptr,
                              0,
                                nullptr,
                                  0 ) )
  {
    DWORD dwStatusCode        = 0;
    DWORD dwStatusCode_Len    = sizeof (DWORD);

    DWORD dwContentLength     = 0;
    DWORD dwContentLength_Len = sizeof (DWORD);
    DWORD dwSizeAvailable     = 0;

    HttpQueryInfo ( hInetHTTPGetReq,
                     HTTP_QUERY_STATUS_CODE |
                     HTTP_QUERY_FLAG_NUMBER,
                       &dwStatusCode,
                         &dwStatusCode_Len,
                           nullptr );

    if (dwStatusCode == 200)
    {
      HttpQueryInfo ( hInetHTTPGetReq,
                        HTTP_QUERY_CONTENT_LENGTH |
                        HTTP_QUERY_FLAG_NUMBER,
                          &dwContentLength,
                            &dwContentLength_Len,
                              nullptr );

      std::vector <char> http_chunk;
      std::vector <char> concat_buffer;

      while ( InternetQueryDataAvailable ( hInetHTTPGetReq,
                                             &dwSizeAvailable,
                                               0x00, NULL )
        )
      {
        if (dwSizeAvailable > 0)
        {
          DWORD dwSizeRead = 0;

          if (http_chunk.size () < dwSizeAvailable)
              http_chunk.resize   (dwSizeAvailable + 1);

          if ( InternetReadFile ( hInetHTTPGetReq,
                                    http_chunk.data (),
                                      dwSizeAvailable,
                                        &dwSizeRead )
             )
          {
            if (dwSizeRead == 0)
              break;

            concat_buffer.insert ( concat_buffer.cend   (),
                                    http_chunk.cbegin   (),
                                      http_chunk.cbegin () + dwSizeRead );

            if (dwSizeRead < dwSizeAvailable)
              break;
          }
        }

        else
          break;

        dwSizeAvailable = 0;
      }

      concat_buffer.push_back ('\0');

      FILE *fPatrons =
        _wfopen (L"patrons.txt", L"wb+");

      if (fPatrons != nullptr)
      {
        fwrite ( concat_buffer.data (),
                 concat_buffer.size (), 1, fPatrons );
        fclose (                           fPatrons );
      }
    }
  }

  CLEANUP (true);

  return 1;
}

void
SKIF_VersionControl::SetCheckFrequency ( const wchar_t *wszProduct,
                                               int      minutes )
{
  std::wstring path =
    SK_FormatStringW (
      LR"(SOFTWARE\Kaldaien\Special K\VersionControl\%ws\)",
        wszProduct
    );

  auto update_freq_key =
    SKIF_MakeRegKeyI ( path.c_str (),
                       LR"(UpdateFrequency)" );

  update_freq_key.putData (minutes);
}

uint32_t
SKIF_VersionControl::GetCheckFrequency (const wchar_t *wszProduct)
{
  std::wstring path =
    SK_FormatStringW (
      LR"(SOFTWARE\Kaldaien\Special K\VersionControl\%ws\)",
        wszProduct
    );

  auto update_freq_key =
    SKIF_MakeRegKeyI ( path.c_str (),
                       LR"(UpdateFrequency)" );

  return
    static_cast <uint32_t> (
      update_freq_key.getData ()
    );
}

bool
SKIF_VersionControl::CheckForUpdates ( const wchar_t *wszProduct,
                                             int      local_build,
                                             int      remote_build,
                                       const wchar_t *wszBranch,
                                             bool     force )
{
  std::wstring base_key_str =
    SK_FormatStringW (
      LR"(SOFTWARE\Kaldaien\Special K\VersionControl\%ws\)",
          wszProduct );

  const time_t update_freq =
    GetCheckFrequency (wszProduct);

  auto last_check =
    SKIF_MakeRegKeyI (
      base_key_str.c_str (),
        L"LastChecked"
    );

  auto installed_build =
    SKIF_MakeRegKeyI (
      base_key_str.c_str (),
        L"InstalledBuild"
    );

  if (update_freq <= 0)
      SetCheckFrequency (wszProduct, 60 * 12);

  bool check_ver = true;

  if (! force)
  {
    const time_t
     scheduled_recheck =
        update_freq    + static_cast <time_t> (
          last_check.getData ()
        );

    if (time (nullptr) / 60 < scheduled_recheck)
    {
      check_ver = false;
    }
  }

  auto get =
    std::make_unique <skif_version_info_t> ();


  if (check_ver || (! PathFileExistsW (L"patrons.txt")))
  {
    URL_COMPONENTSW urlcomps = { };

    urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

    urlcomps.lpszHostName     = get->wszHostName;
    urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

    urlcomps.lpszUrlPath      = get->wszHostPath;
    urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

    if (wszBranch != nullptr)
      get->branch = wszBranch;

    get->product = wszProduct;

    const wchar_t *wszVersionControlRoot =
      L"https://sk-data.special-k.info/VersionControl";

    std::wstring url =
      wszVersionControlRoot;

    url += LR"(/)" + get->product;
    url += LR"(/)" + get->branch;
    url += LR"(/current_build)";

    if ( InternetCrackUrl (          url.c_str  (),
           gsl::narrow_cast <DWORD> (url.length ()),
                              0x00,
                                &urlcomps
                          )
       )
    {
      if (Runner (get.get ()))
        get->success = true;
    }

    skif_patron_info_t
        info = { };
    urlcomps = { };

    urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

    urlcomps.lpszHostName     = info.wszHostName;
    urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

    urlcomps.lpszUrlPath      = info.wszHostPath;
    urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

    url = L"https://sk-data.special-k.info/patrons.txt";

    if ( InternetCrackUrl (          url.c_str  (),
           gsl::narrow_cast <DWORD> (url.length ()),
                              0x00,
                                &urlcomps
                          )
       )
    {
      PatronRunner (&info);
    }

    if (get->success)
      remote_build = get->build;

    if (installed_build.getData () == 0)
        installed_build.putData (local_build);

    if (local_build != remote_build &&
                  0 != remote_build)
    {
      if ( IDYES ==
        MessageBox ( 0,
          L"A new version of SKIF is available for manual update, see details?",
            L"New Version Available", MB_YESNO )
         )
      {
        extern void
        SKIF_Util_OpenURI (std::wstring path, DWORD dwAction = SW_SHOWNORMAL);

        SKIF_Util_OpenURI (
          L"https://discourse.differentk.fyi/c/development/version-history/"
        );
      }
    }

    last_check.putData (
      static_cast <uint32_t> (time (nullptr) / 60)
    );
  }

  if (local_build != 0)
  {
    installed_build.putData (local_build);
  }

  return true;
}

ImGuiStyle SKIF_ImGui_DefaultStyle;

// Main code
int
APIENTRY
wWinMain ( _In_     HINSTANCE hInstance,
           _In_opt_ HINSTANCE hPrevInstance,
           _In_     LPWSTR    lpCmdLine,
           _In_     int       nCmdShow )
{
  UNREFERENCED_PARAMETER (hPrevInstance);
  UNREFERENCED_PARAMETER (hInstance);

  CoInitializeEx (nullptr, 0x0);

  WindowsCursorSize = SKIF_MakeRegKeyI(LR"(SOFTWARE\Microsoft\Accessibility\)",
      LR"(CursorSize)").getData();

  static auto regKVDPIScaling =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(SKIF DPI Scaling)" );

  static auto regKVDisableExitConfirmation =
      SKIF_MakeRegKeyB(LR"(SOFTWARE\Kaldaien\Special K\)",
          LR"(Disable Exit Confirmation)" );

  static auto regKVDisableTooltips =
      SKIF_MakeRegKeyB(LR"(SOFTWARE\Kaldaien\Special K\)",
          LR"(Disable Tooltips)" );

  SKIF_bDPIScaling =
    regKVDPIScaling.getData ();

  SKIF_bDisableExitConfirmation =
      regKVDisableExitConfirmation.getData ();

  SKIF_bDisableTooltips =
      regKVDisableTooltips.getData();

  SKIF_VersionCtl.CheckForUpdates (L"SKIF", SKIF_DEPLOYED_BUILD);

  if (SKIF_bDPIScaling)
    ImGui_ImplWin32_EnableDpiAwareness ();

  SKIF_GetFolderPath (&path_cache.specialk_userdata);
  PathAppendW (        path_cache.specialk_userdata.path,
                         LR"(My Mods\SpecialK)"  );

  int                                    app_id = SKIF_STEAM_APPID;
  if (StrStrW (lpCmdLine, L"AppID="))
  {   assert ( 1 ==
      swscanf (lpCmdLine, L"AppID=%li", &app_id)
             );
  }

  char      szAppID [16] = { };
  snprintf (szAppID, 15, "%li",          app_id);

  HMODULE hModSelf =
    GetModuleHandleW (nullptr);

  // Create application window
  WNDCLASSEX wc =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, WndProc,
            0L,         0L,
    hModSelf, nullptr,  nullptr,
              nullptr,  nullptr,
    _T ("SK_Injection_Frontend"),
              nullptr          };

  if (! ::RegisterClassEx (&wc))
  {
    return 0;
  }

  bool bKeepWindowAlive = true,
       bKeepProcessAlive = true;

  DWORD dwStyle   = SK_BORDERLESS,
        dwStyleEx =
    SKIF_IsWindows8Point1OrGreater () ? SK_BORDERLESS_WIN8_EX
                                      : SK_BORDERLESS_EX;

  if (nCmdShow != SW_SHOWMINNOACTIVE &&
      nCmdShow != SW_SHOWNOACTIVATE  &&
      nCmdShow != SW_SHOWNA          &&
      nCmdShow != SW_HIDE)
    dwStyleEx &= ~WS_EX_NOACTIVATE;

  SKIF_hWnd             =
    CreateWindowExW (                      dwStyleEx,
      wc.lpszClassName, SKIF_WINDOW_TITLE, dwStyle,
      SK_FULLSCREEN_X / 2 - __width  / 2,
      SK_FULLSCREEN_Y / 2 - __height / 2,
                   __width, __height,
                   nullptr, nullptr,
              wc.hInstance, nullptr
    );

  HWND  hWnd  = SKIF_hWnd;
  HICON hIcon =
    LoadIcon (hModSelf, MAKEINTRESOURCE (IDI_SKIF));

  InitializeConditionVariable (&SKIF_IsFocused);
  InitializeConditionVariable (&SKIF_IsNotFocused);

  SendMessage   (hWnd, WM_SETICON, ICON_BIG,    (LPARAM)hIcon);
  SendMessage   (hWnd, WM_SETICON, ICON_SMALL,  (LPARAM)hIcon);
  SendMessage   (hWnd, WM_SETICON, ICON_SMALL2, (LPARAM)hIcon);
  SetClassLongW (hWnd, GCL_HICON,         (LONG)(LPARAM)hIcon);

  // Initialize Direct3D
  if (! CreateDeviceD3D (hWnd))
  {
    CleanupDeviceD3D ();
    return 1;
  }

  SetWindowLong (hWnd, GWL_EXSTYLE, dwStyleEx & ~WS_EX_NOACTIVATE);

  // Show the window
  ShowWindow   (hWnd, SW_SHOWDEFAULT);
  UpdateWindow (hWnd);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION   ();
  ImGui::CreateContext ();

  ImGuiIO& io =
    ImGui::GetIO ();

  (void)io; // WTF does this do?!

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
  io.ConfigViewportsNoAutoMerge      = false;
  io.ConfigViewportsNoTaskBarIcon    =  true;
  io.ConfigViewportsNoDefaultParent  = false;
  io.ConfigDockingAlwaysTabBar       = false;
  io.ConfigDockingTransparentPayload =  true;

  if (SKIF_bDPIScaling)
  {
  //io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI
  }

  // Setup Dear ImGui style
  ImGui::StyleColorsDark ();
  //ImGui::StyleColorsClassic();

  using ImStyle =
        ImGuiStyle;

  // When viewports are enabled we tweak WindowRounding/WindowBg
  //   so platform windows can look identical to regular ones.
  ImStyle&  style =
  ImGui::GetStyle ();

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    style.WindowRounding               = 5.0F;
    style.Colors [ImGuiCol_WindowBg].w = 1.0F;
  }

  style.WindowRounding = 4.0F;// style.ScrollbarRounding;
  style.ChildRounding  = style.WindowRounding;
  style.TabRounding    = style.WindowRounding;
  style.FrameRounding  = style.WindowRounding;

  SKIF_ImGui_DefaultStyle = style;

  // Setup Platform/Renderer bindings
  ImGui_ImplWin32_Init (hWnd);
  ImGui_ImplDX11_Init  (g_pd3dDevice, g_pd3dDeviceContext);

  SKIF_ImGui_InitFonts ();

  // Our state
  ImVec4 clear_color         =
    ImVec4 (0.45F, 0.55F, 0.60F, 1.00F);

  // Main loop
  MSG msg = { };

  CHandle hSwapWait (0);
  HDC     hDC =
    GetWindowDC (hWnd);

  CComQIPtr <IDXGISwapChain3>
      pSwap3 (g_pSwapChain);
  if (pSwap3 != nullptr)
  {
    pSwap3->SetMaximumFrameLatency (1);
    
    hSwapWait.Attach (
      pSwap3->GetFrameLatencyWaitableObject ()
    );
  }

  while (msg.message != WM_QUIT)
  {      msg                   = { };
    auto _TranslateAndDispatch = [&](void) -> bool
    {
      while ( PeekMessage (&msg, 0, 0U, 0U, PM_REMOVE) &&
                            msg.message  !=  WM_QUIT )
      {
        TranslateMessage (&msg);
        DispatchMessage  (&msg);
      }

      return
        ( msg.message != WM_QUIT );
    };

    DWORD dwWait = WAIT_OBJECT_0;

    if (WaitForSingleObject (hSwapWait.m_h, 0) != WAIT_TIMEOUT)
      dwWait = MsgWaitForMultipleObjects ( 1, &hSwapWait.m_h, FALSE,
                                             INFINITE, QS_ALLINPUT );

    if (dwWait != WAIT_OBJECT_0)
    {
      if ( dwWait == WAIT_FAILED ||
           dwWait == WAIT_TIMEOUT )
      {
      }

      else
      {
        if (! _TranslateAndDispatch ())
          break;

        continue;
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame  ();
    ImGui_ImplWin32_NewFrame ();
    ImGui::NewFrame          ();
    {
      io.FontGlobalScale =
       ( io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports )
                        ? SKIF_ImGui_GlobalDPIScale
                        : 1.0f;

      ImGui::Begin ( SKIF_WINDOW_TITLE_A SKIF_WINDOW_HASH,
                       &bKeepWindowAlive,
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse
                   );

      ImGuiTabBarFlags flags =
        ImGuiTabItemFlags_None;

      FLOAT SKIF_GetHDRWhiteLuma (void);
      void  SKIF_SetHDRWhiteLuma (FLOAT);

      static bool bEnableHDR;

      static auto regKVLuma =
        SKIF_MakeRegKeyF (
          LR"(SOFTWARE\Kaldaien\Special K\)",
                   LR"(ImGui HDR Luminance)"
        );

      auto _InitFromRegistry =
      [&](void) ->
      float
      {
        float fLumaInReg =
          regKVLuma.getData ();

        if (fLumaInReg == 0.0f)
        {
          fLumaInReg = SKIF_GetHDRWhiteLuma ();
          regKVLuma.putData (fLumaInReg);
        }

        else
        {
          SKIF_SetHDRWhiteLuma (fLumaInReg);
        }

        return fLumaInReg;
      };

//          ImGui::Checkbox ("Enable HDR###HDR_ImGui", &bEnableHDR);

      static float fLuma =
        _InitFromRegistry ();

      auto _DrawHDRConfig = [&](void)
      {
        static bool bFullRange = false;

        FLOAT fMaxLuma =
          SKIF_GetMaxHDRLuminance (bFullRange);

        if (fMaxLuma != 0.0f)
        {
          if (ImGui::SliderFloat ("###HDR Paper White", &fLuma, 80.0f, fMaxLuma, u8"HDR White:\t%04.1f cd/m²"))
          {
            SKIF_SetHDRWhiteLuma (fLuma);
            regKVLuma.putData    (fLuma);
          }
        }

        else
          ImGui::Spacing ();
      };

      enum _Selection {
        Global,
        Management,
        InjectionConfig,
        Help
      } static tab_selected = Management;

      ImGui::BeginGroup       (                  );
      ImGui::BeginTabBar      ("###SKIF_TAB_BAR", ImGuiTabBarFlags_FittingPolicyResizeDown|
                                                  ImGuiTabBarFlags_FittingPolicyScroll);
      if (ImGui::BeginTabItem ("Global Injection"))
      {
        // Select the 2nd tab on first frame
        SK_RunOnce (flags = ImGuiTabItemFlags_SetSelected);

        if (tab_selected != Global)
            _inject._RefreshSKDLLVersions ();

        tab_selected = Global;

        _inject._GlobalInjectionCtl ();

      ImGui::EndTabItem   (                 );
      }

      extern const wchar_t* SK_GetSteamDir(void);
      static
          std::wstring steam_path(
              SK_GetSteamDir()
          );

      if ( ! steam_path.empty() )
      {
          if (ImGui::BeginTabItem("Steam Management", nullptr, flags))
          {
              if (tab_selected != Management)
                  _inject._RefreshSKDLLVersions();

              tab_selected = Management;

              extern void SKIF_GameManagement_DrawTab(void);
              SKIF_GameManagement_DrawTab();

              ImGui::EndTabItem();
          }
      }

      if (ImGui::BeginTabItem ("Options"))
      { 
        tab_selected = InjectionConfig;

        // SKIF Options
        if (ImGui::CollapsingHeader("Frontend v " SKIF_VERSION_STR_A " (" __DATE__ ")", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginGroup();
            ImGui::Spacing();

            if (ImGui::Checkbox("Disable UI tooltips", &SKIF_bDisableTooltips))
                regKVDisableTooltips.putData(SKIF_bDisableTooltips);

            SKIF_ImGui_SetHoverTip("Tooltips may sometime contain additional information");
              
            ImGui::SetNextItemWidth(ImGui::GetWindowWidth());
            if (ImGui::Checkbox("Scale the UI based on the DPI###EnableDPI", &SKIF_bDPIScaling))
            {
                ImGui_ImplWin32_EnableDpiAwareness();

                SKIF_ImGui_GlobalDPIScale =
                        ImGui_ImplWin32_GetDpiScaleForHwnd(0);

                io.ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleViewports;

                if (SKIF_bDPIScaling)
                        io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

                regKVDPIScaling.putData(SKIF_bDPIScaling);
            }

            SKIF_ImGui_SetHoverTip("This feature is still experimental");
            SKIF_ImGui_SetHoverText("Experimental; UI may misbehave and per-monitor DPI scaling is unsupported");

            if (ImGui::Checkbox("Do not prompt about a running service when closing SKIF", &SKIF_bDisableExitConfirmation))
                regKVDisableExitConfirmation.putData(SKIF_bDisableExitConfirmation);

            SKIF_ImGui_SetHoverTip("The global injector will remain active in the background");
            SKIF_ImGui_SetHoverText("The global injector will remain active in the background");

            _DrawHDRConfig();

            ImGui::EndGroup();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // WinRing0
        if (ImGui::CollapsingHeader("Extended CPU monitoring metrics"))
        {
            ImGui::BeginGroup();
            ImGui::Spacing();
            ImGui::Text("Special K can make use of the WinRing0 kernel driver to provide extended CPU monitoring metrics.\nThis driver is optional and only necessary for users who want the CPU widget to display core\nfrequency and power draw as well.\n\nUse the below button to install or uninstall the driver.");

            ImGui::BeginGroup();
            ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), " Kernel Driver: ");
            ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "Unknown"); // Not Installed coloring
            //ImGui::TextColored(ImColor::HSV(0.3F, 0.99F, 1.F), "Installed"); // Installed coloring

            ImGui::EndGroup();

            // Disabled button
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha *
                ((SKIF_IsHDR()) ? 0.1f
                    : 0.5f
                    ));

            bool button = ImGui::ButtonEx("**Not Implemented**", ImVec2(200, 25), ImGuiButtonFlags_Disabled);

            // Disabled button
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();

            SKIF_ImGui_SetHoverTip("Currently not implemented");
            SKIF_ImGui_SetHoverText("Currently not implemented");

            if (button)
            {
                // Install/Uninstall driver.
            }

            ImGui::EndGroup();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Global injection
        if (ImGui::CollapsingHeader("Global injection", ImGuiTreeNodeFlags_DefaultOpen))
        {
                _inject._StartAtLogonCtrl();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // InjectionConfig
        if (ImGui::CollapsingHeader("Injection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static std::wstring root_dir =
                std::wstring(path_cache.specialk_userdata.path) + LR"(\Global\)";

            static char whitelist[MAX_PATH * 16 * 2] = { };
            static char blacklist[MAX_PATH * 16 * 2] = { };
            static bool white_edited = false,
                black_edited = false;

            auto _StoreList = [](char* szOut, std::wstring fname)->void
            {
                std::wofstream list_file(
                    fname
                );

                if (list_file.is_open())
                {
                    std::wstring out_text =
                        SK_UTF8ToWideChar(szOut);

                    list_file.write(out_text.c_str(), out_text.length());
                    list_file.close();
                }
            };
            auto _LoadList = [](char* szIn, std::wstring fname)->void
            {
                std::wifstream list_file(
                     fname
                );

                std::wstring full_text;

                if (list_file.is_open())
                {
                    std::wstring line;

                    while (list_file.good())
                    {
                        std::getline(list_file, line);

                        full_text += line;
                        full_text += L'\n';
                    }
                    full_text.resize(full_text.length() - 1);

                    list_file.close();
                    strcpy(szIn, SK_WideCharToUTF8(full_text).c_str());
                }
            };

            SK_RunOnce(_LoadList(whitelist, root_dir + L"whitelist.ini"));
            SK_RunOnce(_LoadList(blacklist, root_dir + L"blacklist.ini"));

            ImGui::BeginGroup();
            ImGui::Spacing();

            ImGui::Text("The following lists manage initialization in processes using RegEx patterns.");

            ImGui::Spacing();
            ImGui::Spacing();

            extern void
                SKIF_Util_OpenURI(std::wstring path, DWORD dwAction = SW_SHOWNORMAL);

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F), "Note that these lists do not prevent Special K from being injected into matching processes.");
            ImGui::EndGroup();

            SKIF_ImGui_SetMouseCursorHand();
            SKIF_ImGui_SetHoverTip("The service injects Special K's DLL files into any process that deals with\nsystem input or some sort of window or keyboard/mouse input activity.\n\nThese lists control whether Special K should be initalized (hook APIs etc)\nor remain idle/inert within the injected process.");
            if (ImGui::IsItemClicked())
                SKIF_Util_OpenURI(L"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Enter up to 16 patterns for each list.");
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Easiest is to use the name of the executable or folder of the game.");
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Folder separators must use two backslashes \"\\\\\" and not one \"\\\".");
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Typing \"Games\" (w/o the citation marks) will match all executables below a \"Games\" folder.");
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "If more than 16 patterns are required, combine multiple lines on a single line delimited by | characters.");
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::Text("Whitelist Patterns:");
            white_edited |=
                ImGui::InputTextMultiline("###WhitelistPatterns", whitelist, MAX_PATH * 16 - 1, ImVec2(700, 150));

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::Text("Blacklist Patterns:");
            black_edited |=
                ImGui::InputTextMultiline("###BlacklistPatterns", blacklist, MAX_PATH * 16 - 1, ImVec2(700, 100));

            ImGui::Separator();

            bool bDisabled = (white_edited || black_edited) ? false : true ;

            if (bDisabled)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 
                    ImGui::GetStyle().Alpha *
                        ((SKIF_IsHDR()) ? 0.1f
                                        : 0.5f
                ));
            }

            if (ImGui::Button("Save Changes"))
            {
                // Create the folder if it does not already exist
                CreateDirectoryW(root_dir.c_str(), NULL);

                if (white_edited)
                {
                    _StoreList(whitelist, root_dir + L"whitelist.ini");
                    white_edited = false;
                }
                if (black_edited)
                {
                    _StoreList(blacklist, root_dir + L"blacklist.ini");
                    black_edited = false;
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Reset"))
            {
                if (white_edited)
                {
                    _LoadList(whitelist, root_dir + L"whitelist.ini");
                    white_edited = false;
                }
                if (black_edited)
                {
                    _LoadList(blacklist, root_dir + L"blacklist.ini");
                    black_edited = false;
                }
            }

            if (bDisabled)
            {
                ImGui::PopItemFlag();
                ImGui::PopStyleVar();
            }

                ImGui::EndGroup();
            }
            ImGui::EndTabItem   (                          ); 
      }


      if (ImGui::BeginTabItem("Help"))
      {
          tab_selected = Help;

          ImGui::Spacing();

          ImGui::Text("Special K is an extensive game modifying framework allowing\nfor various forms of in-depth tweaking of a game.");

          ImGui::Spacing();
          ImGui::Spacing();

          ImGui::Text("Online resources:");
          ImGui::Spacing();

          extern void
              SKIF_Util_OpenURI(std::wstring path, DWORD dwAction = SW_SHOWNORMAL);


          ImGui::BeginGroup();
          ImGui::Spacing(); ImGui::SameLine();
          ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(i)"); ImGui::SameLine();
          if (ImGui::Selectable("Discord"))
              SKIF_Util_OpenURI(L"https://discord.com/invite/ER4EDBJPTa");
          SKIF_ImGui_SetMouseCursorHand();
          ImGui::EndGroup();

          ImGui::Spacing();

          ImGui::BeginGroup();
          ImGui::Spacing(); ImGui::SameLine();
          ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(i)"); ImGui::SameLine();
          if (ImGui::Selectable("Forum"))
              SKIF_Util_OpenURI(L"https://discourse.differentk.fyi/");
          SKIF_ImGui_SetMouseCursorHand();
          ImGui::EndGroup();

          ImGui::Spacing();

          ImGui::BeginGroup();
          ImGui::Spacing(); ImGui::SameLine();
          ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(i)"); ImGui::SameLine();
          if (ImGui::Selectable("Wiki"))
              SKIF_Util_OpenURI(L"https://wiki.special-k.info/");
          SKIF_ImGui_SetMouseCursorHand();
          ImGui::EndGroup();

          ImGui::Spacing();

          ImGui::BeginGroup();
          ImGui::Spacing(); ImGui::SameLine();
          ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(i)"); ImGui::SameLine();
          if (ImGui::Selectable("Patreon"))
              SKIF_Util_OpenURI(L"https://www.patreon.com/bePatron?u=33423623");
          SKIF_ImGui_SetMouseCursorHand();
          ImGui::EndGroup();

          ImGui::Spacing();
          ImGui::Spacing();

          ImGui::EndTabItem();
      }
        
      ImGui::EndTabBar    (                  );

      /*
      if (tab_selected == Management)
      {
        ImGui::SameLine     (                  );
        ImGui::BeginGroup   (                  );
        if (ImGui::BeginMenu ("Options###SKIF_UI_PREFS_MENU"))
        {
          if (ImGui::Checkbox ("DPI Scaling###EnableDPI", &SKIF_bDPIScaling))
          {
            ImGui_ImplWin32_EnableDpiAwareness ();

            SKIF_ImGui_GlobalDPIScale =
              ImGui_ImplWin32_GetDpiScaleForHwnd (0);

            io.ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleViewports;

            if (SKIF_bDPIScaling)
              io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports;

            regKVDPIScaling.putData (SKIF_bDPIScaling);
          }

          SKIF_ImGui_SetHoverText ("Experimental; UI may misbehave.");

          _DrawHDRConfig ();

          ImGui::EndMenu ();
        }
        ImGui::EndGroup   (                  );
      }
      */

      ImGui::EndGroup     (                  );

      ImGui::Separator    (                  );
      ImGui::Columns      ( 2, nullptr, false);

      // Exit / Collapse
      if (ImGui::Button("Exit") || ! bKeepWindowAlive)
      {
          if (SKIF_ServiceRunning && ! SKIF_bDisableExitConfirmation)
              ImGui::OpenPopup("Confirm Exit");
          else
              bKeepProcessAlive = false;
      }

      if (ImGui::BeginPopupModal("Confirm Exit", nullptr, ImGuiWindowFlags_NoResize + ImGuiWindowFlags_NoMove + ImGuiWindowFlags_AlwaysAutoResize))
      {
          ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F), "            Exiting without stopping the service will leave the global\n                          injection running in the background.");

          ImGui::Spacing();
          ImGui::Spacing();
          ImGui::Spacing();

          if (ImGui::Button("Minimize", ImVec2(100, 25)))
          {
              bKeepWindowAlive = true;
              ImGui::CloseCurrentPopup();
              ShowWindow(hWnd, SW_MINIMIZE);
          }

          ImGui::SameLine();
          ImGui::Spacing();
          ImGui::SameLine();

          if (ImGui::Button("Stop Service And Exit", ImVec2(0, 25)))
          {
              _inject._StartStopInject(true);
              bKeepProcessAlive = false;
          }

          ImGui::SameLine();
          ImGui::Spacing();
          ImGui::SameLine();

          if (ImGui::Button("Exit", ImVec2(100, 25)))
          {
              bKeepProcessAlive = false;
          }

          ImGui::SameLine();
          ImGui::Spacing();
          ImGui::SameLine();

          if (ImGui::Button("Cancel", ImVec2(100, 25)))
          {
              bKeepWindowAlive = true;
              ImGui::CloseCurrentPopup();
          }

          ImGui::EndPopup();
      }

      if (SKIF_ServiceRunning && SKIF_bDisableExitConfirmation)
          SKIF_ImGui_SetHoverText("Global Injection service will continue running after exit");

      ImGui::SameLine     (                  );

      ImGui::SetColumnWidth ( 0,
        ImGui::GetCursorPosX ()                   +
        ImGui::GetStyle      ().ColumnsMinSpacing +
        ImGui::GetStyle      ().ItemSpacing.x     +
        ImGui::GetStyle      ().FramePadding.x    * 2.0f
      );

      auto _StatusPartSize = [&](std::string& part) -> float
      {
        return
          part.empty () ?
                   0.0f : ImGui::CalcTextSize (
                            part.c_str ()
                          ).x;
      };

      float fStatusWidth = _StatusPartSize (SKIF_StatusBarText),
            fHelpWidth   = _StatusPartSize (SKIF_StatusBarHelp);

      ImGui::NextColumn    ( );

      ImGui::SetCursorPosX ( ImGui::GetCursorPosX  () +
                             ImGui::GetColumnWidth () -
                                    ( fStatusWidth +
                                        fHelpWidth  ) -
                             ImGui::GetScrollX     () -
                         2 * ImGui::GetStyle       ().ItemSpacing.x );
      ImGui::SetCursorPosY ( ImGui::GetCursorPosY  () +
                             ImGui::GetTextLineHeight () / 4.0f );
      ImGui::TextColored   ( ImColor::HSV (0.0f, 0.0f, 0.75f),
                               "%s",
                                 SKIF_StatusBarText.c_str () );

      if (! SKIF_StatusBarHelp.empty ())
      {
        ImGui::SameLine      ();
        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX () -
               ImGui::GetStyle ().ItemSpacing.x
                             );
        ImGui::TextDisabled  ("%s", SKIF_StatusBarHelp.c_str ());
      }

      // Clear the status every frame, it's mostly used for mouse hover tooltips.
      SKIF_StatusBarText = "";
      SKIF_StatusBarHelp = "";

      ImGui::Columns       (1);
      ImGui::End           ( );
    }

    // Rendering
    ImGui::Render ();

    g_pd3dDeviceContext->OMSetRenderTargets    (1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView (    g_mainRenderTargetView, (float *)&clear_color );

    ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
      ImGui::UpdatePlatformWindows        ();
      ImGui::RenderPlatformWindowsDefault ();
    }

    if (FAILED (g_pSwapChain->Present (1, 0x0)))
      break;

    if (hDC != 0)
    {
      RECT              rcClipBox = { };
      GetClipBox (hDC, &rcClipBox);

      bOccluded =
        IsRectEmpty (&rcClipBox);
    }

    else
      bOccluded = FALSE;

    if ((! bKeepProcessAlive) && hWnd != 0)
      PostMessage (hWnd, WM_QUIT, 0x0, 0x0);

    else if (bOccluded || IsIconic (hWnd))
    {
      static 
        bool               first = true;
      if (! std::exchange (first, false))
        MsgWaitForMultipleObjects ( 0, nullptr, FALSE,
                                      INFINITE, QS_ALLINPUT );
      else
      {
        _beginthread ( [](void *)
        {
          CRITICAL_SECTION            GamepadInputPump = { };

          InitializeCriticalSection (&GamepadInputPump);
          EnterCriticalSection      (&GamepadInputPump);

          while (IsWindow (SKIF_hWnd))
          {
            extern bool
                ImGui_ImplWin32_UpdateGamepads (void);
            if (ImGui_ImplWin32_UpdateGamepads (    ))
            {
              // XInput tends to have ~3-7 ms of latency between updates
              //   best-case, try to delay the next poll until there's
              //     new data.
              Sleep (6);
            }

            else if (! SKIF_ImGui_IsFocused ())
            {
              SleepConditionVariableCS (
                &SKIF_IsFocused, &GamepadInputPump,
                  INFINITE
              );
            }

            if (                  SKIF_hWnd != 0)
              SendMessageTimeout (SKIF_hWnd, WM_NULL, 0, 0, 0x0, 1, nullptr);

            if (! SKIF_ImGui_IsFocused ())
            {
              SleepConditionVariableCS (
                &SKIF_IsFocused, &GamepadInputPump,
                  125UL
              );
            }
          }

          LeaveCriticalSection  (&GamepadInputPump);
          DeleteCriticalSection (&GamepadInputPump);

          _endthread ();
        }, 0, nullptr );
      }
    }
  }

  if (hDC != 0)
    ReleaseDC (hWnd, hDC);

  ImGui_ImplDX11_Shutdown   ();
  ImGui_ImplWin32_Shutdown  ();

  CleanupDeviceD3D          (    );
  DestroyWindow             (hWnd);

  ImGui::DestroyContext     ();

  SKIF_hWnd = 0;
       hWnd = 0;

  return 0;
}

// Helper functions

bool CreateDeviceD3D (HWND hWnd)
{
  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC
    sd                                  = { };
  sd.BufferCount                        = 2;
  sd.BufferDesc.Width                   = 2;
  sd.BufferDesc.Height                  = 2;
  sd.BufferDesc.Format                  =
    SKIF_IsWindows10OrGreater      () ? DXGI_FORMAT_R10G10B10A2_UNORM
                                      : DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator   = 0;
  sd.BufferDesc.RefreshRate.Denominator = 0;
  sd.Flags                              =
    SKIF_IsWindows8Point1OrGreater () ?   DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
                                      :   DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow                       = hWnd;
  sd.SampleDesc.Count                   = 1;
  sd.SampleDesc.Quality                 = 0;
  sd.Windowed                           = TRUE;

  sd.SwapEffect                         =
    SKIF_IsWindows10OrGreater ()      ? DXGI_SWAP_EFFECT_FLIP_DISCARD
                                      : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

  UINT createDeviceFlags = 0;
  //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;

  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL
                    featureLevelArray [2] = {
    D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
  };

  if ( D3D11CreateDeviceAndSwapChain ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                         createDeviceFlags, featureLevelArray,
                                                    sizeof (featureLevelArray) / sizeof featureLevel,
                                           D3D11_SDK_VERSION,
                                             &sd, &g_pSwapChain,
                                                  &g_pd3dDevice,
                                                           &featureLevel,
                                                  &g_pd3dDeviceContext) != S_OK ) return false;

  CreateRenderTarget ();
  
  return true;
}

void CleanupDeviceD3D (void)
{
  CleanupRenderTarget ();

  if (g_pSwapChain)        { g_pSwapChain->Release        (); g_pSwapChain        = nullptr; }
  if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release (); g_pd3dDeviceContext = nullptr; }
  if (g_pd3dDevice)        { g_pd3dDevice->Release        (); g_pd3dDevice        = nullptr; }
}

void CreateRenderTarget (void)
{
  ID3D11Texture2D*                           pBackBuffer = nullptr;
  g_pSwapChain->GetBuffer (0, IID_PPV_ARGS (&pBackBuffer));

  if (pBackBuffer != nullptr)
  {
    g_pd3dDevice->CreateRenderTargetView   ( pBackBuffer, nullptr, &g_mainRenderTargetView);
                                             pBackBuffer->Release ();
  }
}

void CleanupRenderTarget (void)
{
  if (g_mainRenderTargetView) { g_mainRenderTargetView->Release (); g_mainRenderTargetView = nullptr; }
}

// Win32 message handler
extern LRESULT
ImGui_ImplWin32_WndProcHandler (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT
WINAPI
WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (ImGui_ImplWin32_WndProcHandler (hWnd, msg, wParam, lParam))
    return true;

  switch (msg)
  {
  case WM_SIZE:
    if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
    {
      CleanupRenderTarget ();
      g_pSwapChain->ResizeBuffers (
        0, (UINT)LOWORD (lParam),
           (UINT)HIWORD (lParam),
          DXGI_FORMAT_UNKNOWN, SKIF_IsWindows8Point1OrGreater () ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                                                                 : 0
      );
      CreateRenderTarget ();
    }
    return 0;

  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
    {
      // Disable ALT application menu
      if ( lParam == 0x00 ||
           lParam == 0x20 )
      {
        return 0;
      }
    }
    break;
  case WM_DESTROY:
    ::PostQuitMessage (0);
    return 0;

  case WM_DPICHANGED:
    if (ImGui::GetIO ().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
    {
      SKIF_ImGui_GlobalDPIScale = (float)HIWORD(wParam) / 96.0f * 100.0f;

      //const int dpi = HIWORD(wParam);
      //printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
      const RECT *suggested_rect =
           (RECT *)lParam;

      ::SetWindowPos ( hWnd, nullptr,
                         suggested_rect->left,                         suggested_rect->top,
                         suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS );
    }
    break;
  }
  return
    ::DefWindowProc (hWnd, msg, wParam, lParam);
}
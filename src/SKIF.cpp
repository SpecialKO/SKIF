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

bool SKIF_bDPIScaling = true,
     SKIF_bDisableExitConfirmation = true,
     SKIF_bDisableTooltips = true;

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

float fAspect     = 16.0f / 9.0f;
float fBottomDist = 0.0f;

#ifdef __D3D12__
#include "imgui/d3d12/imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_4.h>

#define DX12_ENABLE_DEBUG_LAYER     0

struct FrameContext
{
  ID3D12CommandAllocator* CommandAllocator;
  UINT64                  FenceValue;
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_frameContext [NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static int const                    NUM_BACK_BUFFERS           = 3;
static ID3D12Device*                g_pd3dDevice               = NULL;
static ID3D12DescriptorHeap*        g_pd3dRtvDescHeap          = NULL;
static ID3D12DescriptorHeap*        g_pd3dSrvDescHeap          = NULL;
static ID3D12CommandQueue*          g_pd3dCommandQueue         = NULL;
static ID3D12GraphicsCommandList*   g_pd3dCommandList          = NULL;
static ID3D12Fence*                 g_fence                    = NULL;
static HANDLE                       g_fenceEvent               = NULL;
static UINT64                       g_fenceLastSignaledValue   = 0;
static IDXGISwapChain3*             g_pSwapChain               = NULL;
static HANDLE                       g_hSwapChainWaitableObject = NULL;
static ID3D12Resource*              g_mainRenderTargetResource   [NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor [NUM_BACK_BUFFERS] = {};
#else
#define WM_DXGI_OCCLUSION WM_USER
#include "imgui/d3d11/imgui_impl_dx11.h"
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
ID3D11Device*           g_pd3dDevice           = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
IDXGISwapChain*         g_pSwapChain           = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
DWORD                   dwOcclusionCookie      =       0;
BOOL                    bOccluded              =   FALSE;
#endif


// Forward declarations of helper functions
bool CreateDeviceD3D           (HWND hWnd);
void CleanupDeviceD3D          (void);
void CreateRenderTarget        (void);
void CleanupRenderTarget       (void);
#ifdef __D3D12__
void WaitForLastSubmittedFrame (void);
FrameContext*
     WaitForNextFrameResources (void);
#endif
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

extern bool SKIF_ImGui_IsFocused (void);

bool SKIF_ImGui_IsHoverable (void)
{
  if (! SKIF_ImGui_IsFocused ())
    return false;

  return true;
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
    0xc2b1, 0xc2b3, // ²
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

  SK_RunOnce (SKIF_GetFolderPath (&path_cache.specialk_userdata));
  SK_RunOnce (PathAppendW (        path_cache.specialk_userdata.path,
                                     LR"(My Mods\SpecialK)"  ));

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

  HWND hWnd = SKIF_hWnd;

  HICON hIcon =
    LoadIcon (hModSelf, MAKEINTRESOURCE (IDI_SKIF));

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

#ifdef __D3D12__
  ImGui_ImplDX12_Init (g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
    DXGI_FORMAT_R10G10B10A2_UNORM,
    g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart (),
    g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart ());
#else
  ImGui_ImplDX11_Init (g_pd3dDevice, g_pd3dDeviceContext);
#endif

  SKIF_ImGui_InitFonts ();

  // Our state
  ImVec4 clear_color         =
    ImVec4 (0.45F, 0.55F, 0.60F, 1.00F);

  // Main loop
  MSG msg = { };


  while (msg.message != WM_QUIT)
  {
    auto _TranslateAndDispatch = [&](void)
    {
#define MAX_PUMP_TIME 0

      DWORD dwStart = timeGetTime ();

      while (PeekMessage (&msg, nullptr, 0U, 0U, PM_REMOVE))
      {
        TranslateMessage (&msg);
        DispatchMessage  (&msg);

        if (timeGetTime () - dwStart > MAX_PUMP_TIME)
          break;
      }
    };

    CComQIPtr <IDXGISwapChain2>
        pSwapChain2 (g_pSwapChain);
    if (pSwapChain2)
    {
      SK_RunOnce (
        pSwapChain2->SetMaximumFrameLatency (1)
      );

      CHandle hSwapChainWait (
        pSwapChain2->GetFrameLatencyWaitableObject ()
      );

      if (hSwapChainWait.m_h != 0)
      {
        DWORD dwWait =
          MsgWaitForMultipleObjects (1, &hSwapChainWait.m_h, FALSE, 8, QS_ALLINPUT | QS_ALLPOSTMESSAGE);

        if (dwWait != WAIT_OBJECT_0 + 1 && dwWait != WAIT_TIMEOUT)
        {
          _TranslateAndDispatch ();

          continue;
        }
      }
    }

    _TranslateAndDispatch ();

#if 0
    bool ctrl  = (GetAsyncKeyState (VK_MENU)  & 0x8000) != 0;
    bool shift = (GetAsyncKeyState (VK_SHIFT) & 0x8000) != 0;
    bool z     = (GetAsyncKeyState ('X')      & 0x8000) != 0;

    if ( ctrl && shift && z && (! _inject.bOnDemandInject) )
    {
      _inject.bOnDemandInject = true;
      _inject._StartStopInject (_inject.running);
      _inject.run_lvl_changed = false;
    }
#endif

    if (bOccluded || IsIconic (hWnd))
    {
      MsgWaitForMultipleObjects (0, nullptr, FALSE, 250, QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    }

    // Start the Dear ImGui frame
#ifdef __D3D12__
    ImGui_ImplDX12_NewFrame  ();
#else
    ImGui_ImplDX11_NewFrame  ();
#endif
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
        InjectionConfig
      } static tab_selected = Management;

      ImGui::BeginGroup       (                  );
      ImGui::BeginTabBar      ("###SKIF_TAB_BAR", ImGuiTabBarFlags_FittingPolicyResizeDown|
                                                  ImGuiTabBarFlags_FittingPolicyScroll);
      if (ImGui::BeginTabItem ("Global Injection"))
      {
        // Select the 2nd tab on first frame
        SK_RunOnce (flags = ImGuiTabItemFlags_SetSelected);

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

            if (ImGui::Checkbox("Disable UI tooltips                                                                                                                                                                                 ", &SKIF_bDisableTooltips))
                regKVDisableTooltips.putData(SKIF_bDisableTooltips);

            SKIF_ImGui_SetHoverTip("Tooltips may sometime contain additional information");
              
            ImGui::SetNextItemWidth(ImGui::GetWindowWidth());
            if (ImGui::Checkbox("Scale the UI based on the DPI                                                                                                                                                         ###EnableDPI", &SKIF_bDPIScaling))
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

            if (ImGui::Checkbox("Do not prompt about a running service when closing SKIF                                                                                                ", &SKIF_bDisableExitConfirmation))
                regKVDisableExitConfirmation.putData(SKIF_bDisableExitConfirmation);

            SKIF_ImGui_SetHoverTip("The global injector will remain active in the background");
            SKIF_ImGui_SetHoverText("The global injector will remain active in the background");

            _DrawHDRConfig();
            ImGui::EndGroup();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // WinRing0
        if (ImGui::CollapsingHeader("Extended CPU monitoring metrics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginGroup();
            ImGui::Text("Special K can make use of the WinRing0 kernel driver to provide extended CPU monitoring metrics.\nThis driver is optional and only necessary for users who want the CPU widget to display core\nfrequency and power draw as well.\n\nUse the below button to install or uninstall the driver.");

            ImGui::BeginGroup();
            ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), " Kernel Driver: ");
            ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "Unknown"); // Not Installed coloring
            //ImGui::TextColored(ImColor::HSV(0.3F, 0.99F, 1.F), "Installed"); // Installed coloring

            ImGui::EndGroup();

            bool button = ImGui::Button("**Not Implemented**", ImVec2(200, 25));

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
            ImGui::Text("The following fields manage injection in games and can be used to enable Special K in non-Steam games.");

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Enter up to 16 patterns for each list.");
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Folder separators must use two backslashes \"\\\\\" and not one \"\\\".");
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "(!)"); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "Typing \"Games\" (w/o the citation marks) will match all executables below a \"Games\" folder.");
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::Text("Whitelist Patterns:");
            white_edited |=
                ImGui::InputTextMultiline("###WhitelistPatterns", whitelist, MAX_PATH * 16 - 1, ImVec2(700, 200));

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::Text("Blacklist Patterns:");
            black_edited |=
                ImGui::InputTextMultiline("###BlacklistPatterns", blacklist, MAX_PATH * 16 - 1, ImVec2(700, 200));

        ImGui::Separator();

        bool bDisabled = (white_edited || black_edited) ? false : true ;

        if (bDisabled)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }

        if (ImGui::Button("Save Changes"))
        {
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
          ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F), "    Exiting without stopping the service will leave the global\n               injection running in the background.");

          ImGui::Spacing();
          ImGui::Spacing();
          ImGui::Spacing();

          if (ImGui::Button("Stop Service And Exit", ImVec2(0, 25)))
          {
              _inject._StartStopInject(SKIF_ServiceRunning);
              bKeepProcessAlive = false;
          }

          ImGui::SameLine();
          ImGui::Spacing();
          ImGui::SameLine();

          if (ImGui::Button("Exit", ImVec2(100, 25)))
              bKeepProcessAlive = false;

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
#ifdef __D3D12__
    FrameContext* frameCtxt     =
      WaitForNextFrameResources ();
    UINT          backBufferIdx =
      g_pSwapChain->GetCurrentBackBufferIndex ();
    frameCtxt->CommandAllocator->Reset ();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = g_mainRenderTargetResource [backBufferIdx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

    g_pd3dCommandList->Reset                 (frameCtxt->CommandAllocator, NULL);
    g_pd3dCommandList->ResourceBarrier       (1, &barrier);
    g_pd3dCommandList->ClearRenderTargetView (    g_mainRenderTargetDescriptor [backBufferIdx], (float*)& clear_color, 0, NULL);
    g_pd3dCommandList->OMSetRenderTargets    (1, &g_mainRenderTargetDescriptor [backBufferIdx], FALSE, NULL);
    g_pd3dCommandList->SetDescriptorHeaps    (1, &g_pd3dSrvDescHeap);
#endif
    ImGui::Render ();

#ifdef __D3D12__
    ImGui_ImplDX12_RenderDrawData (ImGui::GetDrawData (), g_pd3dCommandList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;

    g_pd3dCommandList->ResourceBarrier (1, &barrier);
    g_pd3dCommandList->Close ();

    g_pd3dCommandQueue->ExecuteCommandLists (
      1, (ID3D12CommandList * const*)& g_pd3dCommandList
    );
#else
    g_pd3dDeviceContext->OMSetRenderTargets (1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView ( g_mainRenderTargetView, (float *)&clear_color );

    ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());
#endif

    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
      ImGui::UpdatePlatformWindows        ();
      ImGui::RenderPlatformWindowsDefault ();
    }

    g_pSwapChain->Present (1, 0x0);

#ifdef __D3D12__
    UINT64 fenceValue =
         g_fenceLastSignaledValue + 1;

    g_pd3dCommandQueue->Signal (g_fence, fenceValue);

    g_fenceLastSignaledValue = fenceValue;
    frameCtxt->FenceValue    = fenceValue;
#endif

    if (! bKeepProcessAlive)
      PostMessage (hWnd, WM_QUIT, 0x0, 0x0);
  }

#ifdef __D3D12__
  WaitForLastSubmittedFrame ();
  ImGui_ImplDX12_Shutdown   ();
#else
  ImGui_ImplDX11_Shutdown   ();
#endif
  ImGui_ImplWin32_Shutdown  ();
  ImGui::DestroyContext     ();

  CleanupDeviceD3D ();
  DestroyWindow (hWnd);

  return 0;
}

#if 0
// Helper functions

bool
CreateDeviceD3D (HWND hWnd)
{
  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC1 sd = { };
  {
    sd.BufferCount        = NUM_BACK_BUFFERS;
    sd.Width              = __width;
    sd.Height             = __height;
    sd.Format             = DXGI_FORMAT_R10G10B10A2_UNORM;
    sd.Flags              = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                            DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
    sd.Scaling            = DXGI_SCALING_STRETCH;
    sd.Stereo             = FALSE;
  }

  if (DX12_ENABLE_DEBUG_LAYER)
  {
    ID3D12Debug* dx12Debug = nullptr;

    if (SUCCEEDED (D3D12GetDebugInterface (IID_PPV_ARGS (&dx12Debug))))
    {
      dx12Debug->EnableDebugLayer ();
      dx12Debug->Release          ();
    }
  }

  D3D_FEATURE_LEVEL featureLevel =
    D3D_FEATURE_LEVEL_11_0;

  if (D3D12CreateDevice (NULL, featureLevel, IID_PPV_ARGS (&g_pd3dDevice)) != S_OK)
    return false;

  {
    D3D12_DESCRIPTOR_HEAP_DESC
      desc                = { };
      desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      desc.NumDescriptors = NUM_BACK_BUFFERS;
      desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      desc.NodeMask       = 1;

    if (g_pd3dDevice->CreateDescriptorHeap (&desc, IID_PPV_ARGS (&g_pd3dRtvDescHeap)) != S_OK)
      return false;

    SIZE_T                      rtvDescriptorSize
      = g_pd3dDevice->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle
      = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart ();

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
      g_mainRenderTargetDescriptor [i] = rtvHandle;
      rtvHandle.ptr += rtvDescriptorSize;
    }
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC
      desc = { };
      desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      desc.NumDescriptors = 1;
      desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (g_pd3dDevice->CreateDescriptorHeap (&desc, IID_PPV_ARGS (&g_pd3dSrvDescHeap)) != S_OK)
      return false;
  }

  {
    D3D12_COMMAND_QUEUE_DESC
      desc = { };
      desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
      desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
      desc.NodeMask = 1;

    if (g_pd3dDevice->CreateCommandQueue (&desc, IID_PPV_ARGS (&g_pd3dCommandQueue)) != S_OK)
      return false;
  }

  for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
  {
    if (g_pd3dDevice->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS (&g_frameContext[i].CommandAllocator)) != S_OK)
      return false;
  }

  if (g_pd3dDevice->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, NULL, IID_PPV_ARGS (&g_pd3dCommandList)) != S_OK ||
    g_pd3dCommandList->Close () != S_OK)
  {
    return false;
  }

  if (g_pd3dDevice->CreateFence (0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&g_fence)) != S_OK)
    return false;

  g_fenceEvent =
    CreateEvent (NULL, FALSE, FALSE, NULL);

  if (g_fenceEvent == NULL)
    return false;

  {
    CComPtr <IDXGIFactory4>   pFactory;
    CComPtr <IDXGISwapChain1> pSwapChain;

    if (CreateDXGIFactory1 (IID_PPV_ARGS (&pFactory)) != S_OK ||
                 pFactory->CreateSwapChainForHwnd (
                       g_pd3dCommandQueue, hWnd, &sd,
                           NULL, NULL, &pSwapChain )  != S_OK ||
      pSwapChain->QueryInterface (
                        IID_PPV_ARGS (&g_pSwapChain)
                                  )                   != S_OK)
    {
      return
        false;
    }

    g_pSwapChain->SetMaximumFrameLatency (NUM_BACK_BUFFERS);
    g_hSwapChainWaitableObject =
      g_pSwapChain->GetFrameLatencyWaitableObject ();
  }

  CreateRenderTarget ();

  return
    true;
}

void
CleanupDeviceD3D (void)
{
  CleanupRenderTarget ();

  if (g_pSwapChain)                       { g_pSwapChain->Release (); g_pSwapChain = NULL; }
  if (g_hSwapChainWaitableObject != NULL) { CloseHandle (g_hSwapChainWaitableObject); }

  for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
  {
    if (g_frameContext [i].CommandAllocator)
    {   g_frameContext [i].CommandAllocator->Release ();
        g_frameContext [i].CommandAllocator = NULL;
    }
  }

  if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release (); g_pd3dCommandQueue = NULL; }
  if (g_pd3dCommandList)  {  g_pd3dCommandList->Release (); g_pd3dCommandList  = NULL; }
  if (g_pd3dRtvDescHeap)  {  g_pd3dRtvDescHeap->Release (); g_pd3dRtvDescHeap  = NULL; }
  if (g_pd3dSrvDescHeap)  {  g_pd3dSrvDescHeap->Release (); g_pd3dSrvDescHeap  = NULL; }
  if (g_fence)            {            g_fence->Release (); g_fence            = NULL; }
  if (g_fenceEvent)       { CloseHandle (g_fenceEvent);     g_fenceEvent       = NULL; }
  if (g_pd3dDevice)       {       g_pd3dDevice->Release (); g_pd3dDevice       = NULL; }
}

void
CreateRenderTarget (void)
{
  for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
  {
    ID3D12Resource*
                pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer (i, IID_PPV_ARGS (&pBackBuffer));

    g_pd3dDevice->CreateRenderTargetView (
      pBackBuffer, nullptr,
        g_mainRenderTargetDescriptor [i]
    );

    g_mainRenderTargetResource [i] = pBackBuffer;
  }
}

void
CleanupRenderTarget (void)
{
  WaitForLastSubmittedFrame ();

  for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
  {
    if (g_mainRenderTargetResource [i])
    {   g_mainRenderTargetResource [i]->Release ();
        g_mainRenderTargetResource [i] = nullptr; }
  }
}

void
WaitForLastSubmittedFrame (void)
{
  FrameContext* frameCtxt =
             &g_frameContext [g_frameIndex % NUM_FRAMES_IN_FLIGHT];

  UINT64 fenceValue = frameCtxt->FenceValue;
  if (   fenceValue == 0 )
    return; // No fence was signaled

  frameCtxt->FenceValue = 0;
  if (g_fence->GetCompletedValue () >= fenceValue)
    return;

  g_fence->SetEventOnCompletion (
    fenceValue,        g_fenceEvent);
  WaitForSingleObject (g_fenceEvent, INFINITE);
}

FrameContext*
WaitForNextFrameResources (void)
{
  UINT nextFrameIndex = g_frameIndex + 1;
         g_frameIndex = nextFrameIndex;

  HANDLE    waitableObjects [] = { g_hSwapChainWaitableObject, NULL };
  DWORD  numWaitableObjects    = 1;

  FrameContext* frameCtxt =
    &g_frameContext [nextFrameIndex % NUM_FRAMES_IN_FLIGHT];

  UINT64 fenceValue = frameCtxt->FenceValue;

  if (fenceValue != 0) // means no fence was signaled
  {
    frameCtxt->FenceValue = 0;

    g_fence->SetEventOnCompletion (fenceValue, g_fenceEvent);

    waitableObjects [1] = g_fenceEvent;
    numWaitableObjects  = 2;
  }

  WaitForMultipleObjects (
    numWaitableObjects,
       waitableObjects,
         TRUE, INFINITE );

  return frameCtxt;
}

void
ResizeSwapChain (HWND hWnd, int width, int height)
{
  DXGI_SWAP_CHAIN_DESC1    sd = { };
  g_pSwapChain->GetDesc1 (&sd);
  sd.Width  = width;
  sd.Height = height;

  IDXGIFactory4*   pFactory   = nullptr;
  IDXGISwapChain1* pSwapChain = nullptr;

  g_pSwapChain->GetParent (IID_PPV_ARGS (&pFactory));
  g_pSwapChain->Release ();

  CloseHandle (g_hSwapChainWaitableObject);

  pFactory->CreateSwapChainForHwnd (
    g_pd3dCommandQueue, hWnd, &sd,
            NULL, NULL, &pSwapChain
  );

  if (pSwapChain != nullptr)
  {
      pSwapChain->QueryInterface <IDXGISwapChain3> (&g_pSwapChain);
    g_pSwapChain->SetMaximumFrameLatency (NUM_BACK_BUFFERS);

    g_hSwapChainWaitableObject =
      g_pSwapChain->GetFrameLatencyWaitableObject ();
  }

  assert (g_hSwapChainWaitableObject != NULL);
}

// Win32 message handler
extern LRESULT
ImGui_ImplWin32_WndProcHandler ( HWND   hWnd,   UINT   msg,
                                 WPARAM wParam, LPARAM lParam );

LRESULT
WINAPI
WndProc ( HWND   hWnd,
          UINT   msg,
          WPARAM wParam,
          LPARAM lParam )
{
  if (ImGui_ImplWin32_WndProcHandler (hWnd, msg, wParam, lParam))
    return true;

  switch (msg)
  {
    case WM_SIZING:
    {
      if ( g_pd3dDevice != NULL &&
                 wParam != SIZE_MINIMIZED )
      {
        ImGui_ImplDX12_InvalidateDeviceObjects ();

        CleanupRenderTarget ();
        ResizeSwapChain     (hWnd, (UINT)LOWORD (lParam), (UINT)HIWORD (lParam));
        CreateRenderTarget  ();

        ImGui_ImplDX12_CreateDeviceObjects ();
      }

      return 0;
    }

    case WM_SYSCOMMAND:
    {
      if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
        return 0;
    } break;

    case WM_DESTROY:
    {
      PostQuitMessage (0);
      return 0;
    }
  }

  return
    DefWindowProc (hWnd, msg, wParam, lParam);
}
#endif

// Helper functions

bool CreateDeviceD3D (HWND hWnd)
{
  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC
    sd                                  = { };
  sd.BufferCount                        = 3;
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
    SKIF_IsWindows10OrGreater      () ? DXGI_SWAP_EFFECT_FLIP_DISCARD
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
                                                  &g_pd3dDeviceContext) != S_OK )
    return false;

  CComPtr <IDXGIFactory2> pFactory2;

  if (SUCCEEDED (g_pSwapChain->GetParent (IID_PPV_ARGS (&pFactory2.p))))
  {
    pFactory2->RegisterOcclusionStatusWindow (hWnd, WM_DXGI_OCCLUSION, &dwOcclusionCookie);
  }

  CreateRenderTarget ();
  return true;
}

void CleanupDeviceD3D ()
{
  CComPtr <IDXGIFactory2> pFactory2;

  if (           g_pSwapChain != nullptr &&
      SUCCEEDED (g_pSwapChain->GetParent (IID_PPV_ARGS (&pFactory2.p))))
  {
    pFactory2->UnregisterOcclusionStatus (dwOcclusionCookie);
  }

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

void CleanupRenderTarget ()
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
  case WM_DXGI_OCCLUSION:
    if (g_pSwapChain != nullptr)
    {
      bOccluded = (
        DXGI_STATUS_OCCLUDED == g_pSwapChain->Present (0, DXGI_PRESENT_TEST)
      );
    }
    return 0;

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
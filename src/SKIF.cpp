//
// Copyright 2020 - 2021 Andon "Kaldaien" Coleman
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

#include <wtypes.h>
#include <dxgi1_5.h>

#include <gsl/gsl_util>

const GUID IID_IDXGIFactory5 =
  { 0x7632e1f5, 0xee65, 0x4dca, { 0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d } };

const int SKIF_STEAM_APPID = 1157970;
int WindowsCursorSize = 1;
bool RepositionSKIF = false;

#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L

extern void SKIF_ProcessCommandLine (const char* szCmd);

bool SKIF_bDisableDPIScaling       = false,
     SKIF_bDisableExitConfirmation = false,
     SKIF_bDisableTooltips         = false,
     SKIF_bDisableStatusBar        = false,
     SKIF_bDisableSteamLibrary     = false,
     SKIF_bSmallMode               = false,
     SKIF_bFirstLaunch             = false,
     SKIF_bEnableDebugMode         = false,
     SKIF_bAllowMultipleInstances  = false,
     SKIF_bAllowBackgroundService  = false,
     SKIF_bEnableHDR               = false,
     SKIF_bOpenAtCursorPosition    = false;
BOOL SKIF_bAllowTearing            = FALSE;

#include <sk_utility/command.h>

extern        SK_ICommandProcessor*
  __stdcall SK_GetCommandProcessor (void);

#include <SKIF.h>

#include <stores/Steam/library.h>

#include "version.h"
#include <injection.h>
#include <font_awesome.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_internal.h>
#include <dxgi1_6.h>
#include <xinput.h>

#include <fsutil.h>
#include <psapi.h>

#include <fstream>
#include <typeindex>

#pragma comment (lib, "wininet.lib")

using  GetSystemMetricsForDpi_pfn = int (WINAPI *)(int, UINT);
static GetSystemMetricsForDpi_pfn
       GetSystemMetricsForDpi = nullptr;

#define SK_BORDERLESS ( WS_VISIBLE | WS_POPUP | WS_MINIMIZEBOX | \
                        WS_SYSMENU )

#define SK_BORDERLESS_EX      ( WS_EX_APPWINDOW | WS_EX_NOACTIVATE )
#define SK_BORDERLESS_WIN8_EX ( SK_BORDERLESS_EX | WS_EX_NOREDIRECTIONBITMAP )

#define SK_FULLSCREEN_X(dpi) (GetSystemMetricsForDpi != nullptr) ? GetSystemMetricsForDpi (SM_CXFULLSCREEN, (dpi)) : GetSystemMetrics (SM_CXFULLSCREEN)
#define SK_FULLSCREEN_Y(dpi) (GetSystemMetricsForDpi != nullptr) ? GetSystemMetricsForDpi (SM_CYFULLSCREEN, (dpi)) : GetSystemMetrics (SM_CYFULLSCREEN)

#define GCL_HICON           (-14)

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

DWORD SKIF_timeGetTime (void)
{
  static LARGE_INTEGER qpcFreq = { };
         LARGE_INTEGER li      = { };

  using timeGetTime_pfn =
          DWORD (WINAPI *)(void);
  static timeGetTime_pfn
   winmm_timeGetTime     = nullptr;

  if (  winmm_timeGetTime == nullptr || qpcFreq.QuadPart == 1)
  {
    if (winmm_timeGetTime == nullptr)
    {
      HMODULE hModWinMM =
        LoadLibraryEx ( L"winmm.dll", nullptr,
                          LOAD_LIBRARY_SEARCH_SYSTEM32 );
        winmm_timeGetTime =
             (timeGetTime_pfn)GetProcAddress (hModWinMM,
             "timeGetTime"                   );
    }

    return winmm_timeGetTime != nullptr ?
           winmm_timeGetTime ()         : static_cast <DWORD> (-1);
  }

  if (QueryPerformanceCounter (&li))
  {
    if (qpcFreq.QuadPart == 0 && QueryPerformanceFrequency (&qpcFreq) == FALSE)
    {   qpcFreq.QuadPart  = 1;

      return rand ();
    }

    return
      li.QuadPart / ( qpcFreq.QuadPart / 1000ULL);
  }

  return static_cast <DWORD> (-1);
}

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

bool    bStopOnInjection = false; // Used to stop SKIF on a successful injection if it's used merely as a launcher
CHandle hInjectAck (0); // Signalled when a game finishes injection
CHandle hSwapWait  (0);


int __width  = 0;
int __height = 0;

float SKIF_ImGui_GlobalDPIScale = 1.0f;

std::string SKIF_StatusBarText = "";
std::string SKIF_StatusBarHelp = "";
HWND        SKIF_hWnd          =  0;

CONDITION_VARIABLE SKIF_IsFocused    = { };
CONDITION_VARIABLE SKIF_IsNotFocused = { };

extern bool SKIF_ImGui_IsFocused (void);

bool
SKIF_ImGui_IsHoverable (void)
{
  if (! SKIF_ImGui_IsFocused ())
    return false;

  return true;
}

void
SKIF_ImGui_SetMouseCursorHand (void)
{
  if (ImGui::IsItemHovered ())
  {
    ImGui::SetMouseCursor (
      ImGuiMouseCursor_Hand
    );
  }
}

void
SKIF_ImGui_SetHoverTip (const std::string_view& szText)
{
  if ( SKIF_ImGui_IsHoverable () && (! SKIF_bSmallMode) )
  {
    if (ImGui::IsItemHovered ())
    {
      if (! SKIF_bDisableTooltips)
      {
        auto& io =
          ImGui::GetIO ();

        ImVec2 cursorPos   = io.MousePos;
        int    cursorScale = WindowsCursorSize;

        ImGui::SetNextWindowPos (
          ImVec2 ( cursorPos.x + 16      + 4 * (cursorScale - 1),
                   cursorPos.y + 8 /* 16 + 4 * (cursorScale - 1) */ )
        );

        ImGui::SetTooltip (
          "%hs", szText.data ()
        );
      }

      else
      {
        SKIF_StatusBarText =
          "Info: ";

        SKIF_ImGui_SetHoverText (
          szText.data (), true
        );
      }
    }
  }
}

void
SKIF_ImGui_SetHoverText ( const std::string_view& szText,
                                bool  overrideExistingText )
{
  if ( ImGui::IsItemHovered ()                                  &&
        ( overrideExistingText || SKIF_StatusBarHelp.empty () ) &&
        (                       ! SKIF_bSmallMode             )
     )
  {
    SKIF_StatusBarHelp.assign (szText);
  }
}

void SKIF_ImGui_Spacing (float multiplier)
{
  ImGui::ItemSize (
    ImVec2 ( ImGui::GetTextLineHeightWithSpacing () * multiplier,
             ImGui::GetTextLineHeightWithSpacing () * multiplier )
  );
}

// Difference to regular BeginChildFrame? No ImGuiWindowFlags_NoMove!
bool
SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags)
{
  const ImGuiStyle& style =
    ImGui::GetStyle ();

  ImGui::PushStyleColor (ImGuiCol_ChildBg,              style.Colors [ImGuiCol_FrameBg]);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding,   style.FrameRounding);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildBorderSize, style.FrameBorderSize);
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,   style.FramePadding);

  bool ret =
    ImGui::BeginChild (id, size, true, ImGuiWindowFlags_AlwaysUseWindowPadding | extra_flags);

  ImGui::PopStyleVar   (3);
  ImGui::PopStyleColor ( );

  return ret;
}

void SKIF_ImGui_BeginTabChildFrame (void)
{
  auto frame_content_area_id =
    ImGui::GetID ("###SKIF_Content_Area");

  SKIF_ImGui_BeginChildFrame (
    frame_content_area_id,
      ImVec2 (   0.0f,
               900.0f * SKIF_ImGui_GlobalDPIScale ),
        ImGuiWindowFlags_NavFlattened |
        ImGuiWindowFlags_NoBackground
  );
}

const ImWchar*
SK_ImGui_GetGlyphRangesDefaultEx (void)
{
  static const ImWchar ranges [] =
  {
    0x0020,  0x00FF, // Basic Latin + Latin Supplement
    0x0100,  0x03FF, // Latin, IPA, Greek
    0x2000,  0x206F, // General Punctuation
    0x2100,  0x21FF, // Letterlike Symbols
    0x2600,  0x26FF, // Misc. Characters
    0x2700,  0x27BF, // Dingbats
    0x207f,  0x2090, // N/A (literally, the symbols for N/A :P)
    0xc2b1,  0xc2b3, // ²
    0
  };
  return &ranges [0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesFontAwesome (void)
{
  static const ImWchar ranges [] =
  {
    ICON_MIN_FA, ICON_MAX_FA,
    0 // 🔗, 🗙
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

#include <fonts/fa_regular_400.ttf.h>
#include <fonts/fa_brands_400.ttf.h>
#include <fonts/fa_solid_900.ttf.h>

#include <filesystem>

namespace skif_fs = std::filesystem;

auto SKIF_ImGui_InitFonts = [&](float fontSize = 18.0F)
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

    SKIF_ImGui_LoadFont (
    L"Tahoma.ttf",
      fontSize,
        SK_ImGui_GetGlyphRangesDefaultEx ()
  );

  skif_fs::path fontDir
          (path_cache.specialk_userdata.
           path);

  fontDir /= L"Fonts";

  if (! skif_fs::exists (            fontDir))
        skif_fs::create_directories (fontDir);

  static auto
    skif_fs_wb = ( std::ios_base::binary
                 | std::ios_base::out  );

  auto _UnpackFontIfNeeded =
  [&]( const char*   szFont,
       const uint8_t akData [],
       const size_t  cbSize )
  {
    if (! skif_fs::is_regular_file ( fontDir / szFont)            )
                     std::ofstream ( fontDir / szFont, skif_fs_wb ).
      write ( reinterpret_cast <const char *> (akData),
                                               cbSize);
  };

  auto      awesome_fonts = {
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAR, fa_regular_400_ttf,
                   _ARRAYSIZE (fa_regular_400_ttf) ),
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAS, fa_solid_900_ttf,
                   _ARRAYSIZE (fa_solid_900_ttf) ),
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAB, fa_brands_400_ttf,
                   _ARRAYSIZE (fa_brands_400_ttf) )
                            };

  std::for_each (
            awesome_fonts.begin (),
            awesome_fonts.end   (),
    [&](const auto& font)
    {        _UnpackFontIfNeeded (
      std::get <0> (font),
      std::get <1> (font),
      std::get <2> (font)        );
     SKIF_ImGui_LoadFont (
                    fontDir/
      std::get <0> (font),
                    fontSize,
        SK_ImGui_GetGlyphRangesFontAwesome (),
                   &font_cfg
                         );
    }           );

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

    _fseeki64 (fPatrons, 0, SEEK_END);

    size_t size =
      gsl::narrow_cast <size_t> (
      _ftelli64 (fPatrons)      );
    rewind      (fPatrons);

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
#include <filesystem>
#include <regex>

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

    if (local_build <  remote_build &&
                  0 != remote_build)
    {
      if ( IDYES ==
        MessageBox ( 0,
          L"A new version of SKIF is available for manual update, see details?",
            L"New Version Available", MB_YESNO )
         )
      {
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


BOOL
WINAPI
SKIF_Util_CompactWorkingSet (void)
{
  return
    EmptyWorkingSet (
      GetCurrentProcess ()
    );
}

ImGuiStyle SKIF_ImGui_DefaultStyle;

HWND hWndOrigForeground;

struct SKIF_Signals {
  BOOL Stop          = FALSE;
  BOOL Start         = FALSE;
  BOOL Quit          = FALSE;
  BOOL Minimize      = FALSE;
  BOOL Restore       =  TRUE;
  BOOL CustomLaunch  = FALSE;

  BOOL _Disowned = FALSE;
} _Signal;

constexpr UINT WM_SKIF_CUSTOMLAUNCH  = WM_USER + 0x8192;
constexpr UINT WM_SKIF_REPOSITION    = WM_USER + 0x4096;
constexpr UINT WM_SKIF_STOP          = WM_USER + 0x2048;
constexpr UINT WM_SKIF_START         = WM_USER + 0x1024;
constexpr UINT WM_SKIF_MINIMIZE      = WM_USER +  0x512;

const wchar_t* SKIF_WindowClass =
             L"SK_Injection_Frontend";

#include <sstream>

void
SKIF_ProxyCommandAndExitIfRunning (LPWSTR lpCmdLine)
{
  HWND hwndAlreadyExists =
    FindWindowExW (0, 0, SKIF_WindowClass, nullptr);

  _Signal.Stop =
    StrStrIW (lpCmdLine, L"Stop")     != nullptr;

  _Signal.Start =
    StrStrIW (lpCmdLine, L"Start")    != nullptr;

  _Signal.Quit =
    StrStrIW (lpCmdLine, L"Quit")     != nullptr;

  _Signal.Minimize =
    StrStrIW (lpCmdLine, L"Minimize") != nullptr;

  _Signal.CustomLaunch =
    StrStrIW (lpCmdLine, L"\\")       != nullptr;

  if ( hwndAlreadyExists != 0 && (
              (! SKIF_bAllowMultipleInstances)  ||
                                   _Signal.Stop || _Signal.Start ||
                                   _Signal.Quit || _Signal.Minimize
                                 )
     )
  {
    if (IsIconic        (hwndAlreadyExists))
      ShowWindow        (hwndAlreadyExists, SW_SHOWNA);
    SetForegroundWindow (hwndAlreadyExists);

    if (_Signal.CustomLaunch)
      PostMessage         (hwndAlreadyExists, WM_SKIF_CUSTOMLAUNCH, 0x0, 0x0); // How do we PostMessage / SendMessage the lpCmdLine over to the running instance?! :|
    else
      PostMessage         (hwndAlreadyExists, WM_SKIF_REPOSITION, 0x0, 0x0);

    struct injection_probe_s {
      FILE          *pid;
      const wchar_t *wszFile;
      DWORD_PTR      x = 0;
    } _32 { nullptr, LR"(Servlet\SpecialK32.pid)" },
      _64 { nullptr, LR"(Servlet\SpecialK64.pid)" };

    if (_Signal.Stop || _Signal.Quit)
    {
      if (_Signal.Stop)
      {
        for ( auto probe : { _32, _64 } )
        {
          do {
            probe.pid =
              _wfopen (probe.wszFile, L"r");

            if (probe.pid != nullptr)
            {
              fclose (probe.pid);

              SendMessageTimeout (
                hwndAlreadyExists, WM_SKIF_STOP,
                              0x0, 0x0, 0x0,
                                     2, &probe.x
                                 );
            }
          } while (probe.pid != nullptr);
        }
      }

      if (_Signal.Quit)
        PostMessage ( hwndAlreadyExists, WM_QUIT,
                                    0x0, 0x0 );
    }

    if (_Signal.Start)
    {
      for ( auto probe : { _32, _64 } )
      {
        do {
          probe.pid =
            _wfopen (probe.wszFile, L"r");

          if (probe.pid == nullptr)
          {
            SendMessageTimeout (
              hwndAlreadyExists, WM_SKIF_START,
                            0x0, 0x0, 0x0,
                                   2, &probe.x
                               );
          }

          else
          {
            fclose (probe.pid);
            break;
          }
        } while (probe.pid == nullptr);
      }
    }

    if (_Signal.Minimize)
    {
      SendMessageTimeout (
        hwndAlreadyExists, WM_SKIF_MINIMIZE,
                      0x0, 0x0, 0x0,
                             2, &_32.x
                         );
    }

    if (IsIconic        (hWndOrigForeground))
      ShowWindow        (hWndOrigForeground, SW_SHOWNA);
    SetForegroundWindow (hWndOrigForeground);

    if (_Signal.Quit || (! _Signal._Disowned))
       ExitProcess (0x0);
  }

  if (_Signal.CustomLaunch)
  {
    auto _SK_Inject_TestUserWhitelist = [](const char* wszExecutable)->bool
    {
      if (*_inject.whitelist == '\0')
        return false;

      std::istringstream iss(_inject.whitelist);

      for (std::string line; std::getline(iss, line); )
      {
        std::regex regexp (line, std::regex_constants::icase);

        if (std::regex_search(wszExecutable, regexp))
          return true;
      }

      return false;
    };

    if (! _inject.bCurrentState)
    {
      bStopOnInjection = true;
      _inject._StartStopInject(false, true); //true
    }

    std::wstring cmdLine        = std::wstring(lpCmdLine);
    std::wstring delimiter      = L".exe"; // split lpCmdLine at the .exe
    std::wstring path           = cmdLine.substr(0, cmdLine.find(delimiter) + delimiter.length()); // path
    std::wstring proxiedCmdLine = cmdLine.substr(cmdLine.find(delimiter) + delimiter.length(), cmdLine.length()); // proxied command line
    std::string  parentFolder = std::filesystem::path(path).parent_path().filename().string(); // name of parent folder

    if (  path.find(L"steamapps") == std::wstring::npos &&
        ! _SK_Inject_TestUserWhitelist (SK_WideCharToUTF8(path).c_str())
       )
    {
      if (*_inject.whitelist == '\0')
        snprintf(_inject.whitelist, sizeof _inject.whitelist, "%s%s", _inject.whitelist, parentFolder.c_str());
      else
        snprintf(_inject.whitelist, sizeof _inject.whitelist, "%s%s", _inject.whitelist, ("|" + parentFolder).c_str());

      _inject._StoreList(true);
    }

    SHELLEXECUTEINFOW
      sexi              = { };
      sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
      sexi.lpVerb       = L"OPEN";
      sexi.lpFile       = path.c_str();
      sexi.lpParameters = proxiedCmdLine.c_str();
      sexi.lpDirectory  = std::filesystem::path(path).remove_filename().c_str();
      sexi.nShow        = SW_SHOW;
      sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                          SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

      ShellExecuteExW(&sexi);
  }
}

void
SKIF_AddToEnvironmentalPath(void)
{
  BYTE buffer[4000];
  DWORD buffsz = sizeof(buffer);
  //HKEY_CURRENT_USER\Environment
  HKEY key;

  if (RegOpenKeyEx    (HKEY_CURRENT_USER, L"Environment", 0, KEY_ALL_ACCESS, std::addressof(key)) == 0 &&
      RegQueryValueEx (key, L"Path", nullptr, nullptr, buffer, std::addressof(buffsz)) == 0)
  {
    std::wstring env = reinterpret_cast<const wchar_t*>(buffer);
    std::wstring currentPath = std::filesystem::current_path().wstring();

    if (env.find(currentPath) == std::string::npos)
    {
      std::wstring new_env = env + L";" + currentPath;
      RegSetValueEx(key, L"Path", 0, REG_SZ, (LPBYTE)(new_env.c_str()), (new_env.size() + 1) * sizeof(wchar_t));
      RegCloseKey(key);
    }
  }
}

bool bKeepWindowAlive  = true,
     bKeepProcessAlive = true;

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

  SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT);

  ImGui_ImplWin32_EnableDpiAwareness ();

  GetSystemMetricsForDpi =
 (GetSystemMetricsForDpi_pfn)GetProcAddress (GetModuleHandle (L"user32.dll"),
 "GetSystemMetricsForDpi");

  CoInitializeEx (nullptr, 0x0);

  WindowsCursorSize =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Microsoft\Accessibility\)",
                         LR"(CursorSize)" ).getData ();

  static auto regKVGlobalServiceTimout =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                        LR"(Global Service Timeout)" );

  static auto regKVDisableExitConfirmation =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Exit Confirmation)" );

  static auto regKVDisableDPIScaling =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable DPI Scaling)" );

  static auto regKVEnableDebugMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Enable Debug Mode)" );

  static auto regKVDisableTooltips =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Tooltips)" );

  static auto regKVDisableStatusBar =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Status Bar)" );

  static auto regKVDisableSteamLibrary =
    SKIF_MakeRegKeyB(LR"(SOFTWARE\Kaldaien\Special K\)",
      LR"(Disable Steam Library)");

  static auto regKVSmallMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Small Mode)" );

  static auto regKVFirstLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(First Launch)" );

  static auto regKVAllowMultipleInstances =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Allow Multiple SKIF Instances)" );

  static auto regKVAllowBackgroundService =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Allow Background Service)" );

  static auto regKVEnableHDR =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(HDR)" );

  static auto regKVOpenAtCursorPosition =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Open At Cursor Position)" );

  SKIF_bDisableDPIScaling       = regKVDisableDPIScaling.getData       ( );
  SKIF_bDisableExitConfirmation = regKVDisableExitConfirmation.getData ( );
  SKIF_bDisableTooltips         = regKVDisableTooltips.getData         ( );
  SKIF_bDisableStatusBar        = regKVDisableStatusBar.getData        ( );
  SKIF_bDisableSteamLibrary     = regKVDisableSteamLibrary.getData     ( );
  SKIF_bEnableDebugMode         = regKVEnableDebugMode.getData         ( );
  SKIF_bSmallMode               = regKVSmallMode.getData               ( );
  SKIF_bFirstLaunch             = regKVFirstLaunch.getData             ( );
  SKIF_bAllowMultipleInstances  = regKVAllowMultipleInstances.getData  ( );
  SKIF_bAllowBackgroundService  = regKVAllowBackgroundService.getData  ( );
  SKIF_bEnableHDR               = regKVEnableHDR.getData               ( );
  SKIF_bOpenAtCursorPosition    = regKVOpenAtCursorPosition.getData    ( );

  hWndOrigForeground =
    GetForegroundWindow ();

  SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);

  // Check for updates
  SKIF_VersionCtl.CheckForUpdates (
    L"SKIF", SKIF_DEPLOYED_BUILD
  );

  // Add SKIF to the user's environmental PATH variable
  SKIF_AddToEnvironmentalPath ( );

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

  DWORD dwStyle   = SK_BORDERLESS,
        dwStyleEx =
    SKIF_IsWindows8Point1OrGreater () ? SK_BORDERLESS_WIN8_EX
                                      : SK_BORDERLESS_EX;

  if (nCmdShow != SW_SHOWMINNOACTIVE &&
      nCmdShow != SW_SHOWNOACTIVATE  &&
      nCmdShow != SW_SHOWNA          &&
      nCmdShow != SW_HIDE)
    dwStyleEx &= ~WS_EX_NOACTIVATE;

  HMONITOR hMonitor =
    MonitorFromWindow (hWndOrigForeground, MONITOR_DEFAULTTONEAREST);

  MONITORINFOEX
    miex        = {           };
    miex.cbSize = sizeof (miex);

  UINT dpi = 0;

  if ( GetMonitorInfoW (hMonitor, &miex) )
  {
    float fdpiX =
      ImGui_ImplWin32_GetDpiScaleForMonitor (hMonitor);

    dpi =
      static_cast <UINT> (fdpiX * 96.0f);

    //int cxLogical = ( miex.rcMonitor.right  -
    //                  miex.rcMonitor.left  );
    //int cyLogical = ( miex.rcMonitor.bottom -
    //                  miex.rcMonitor.top   );
  }

  SKIF_hWnd             =
    CreateWindowExW (                      dwStyleEx,
      wc.lpszClassName, SKIF_WINDOW_TITLE, dwStyle,
      SK_FULLSCREEN_X (dpi) / 2 - __width  / 2,
      SK_FULLSCREEN_Y (dpi) / 2 - __height / 2,
                   __width, __height,
                   nullptr, nullptr,
              wc.hInstance, nullptr
    );

  HWND  hWnd  = SKIF_hWnd;
  HICON hIcon =
    LoadIcon (hModSelf, MAKEINTRESOURCE (IDI_SKIF));

  InitializeConditionVariable (&SKIF_IsFocused);
  InitializeConditionVariable (&SKIF_IsNotFocused);

  SendMessage      (hWnd, WM_SETICON, ICON_BIG,        (LPARAM)hIcon);
  SendMessage      (hWnd, WM_SETICON, ICON_SMALL,      (LPARAM)hIcon);
  SendMessage      (hWnd, WM_SETICON, ICON_SMALL2,     (LPARAM)hIcon);
  SetClassLongPtrW (hWnd, GCL_HICON,         (LONG_PTR)(LPARAM)hIcon);

  // Initialize Direct3D
  if (! CreateDeviceD3D (hWnd))
  {
    CleanupDeviceD3D ();
    return 1;
  }

  SetWindowLongPtr (hWnd, GWL_EXSTYLE, dwStyleEx & ~WS_EX_NOACTIVATE);

  // Show the window
  ShowWindow   (hWnd, SW_SHOWDEFAULT);
  UpdateWindow (hWnd);

  // Set a timer to trigger a window refresh every 1000ms,
  //   since two counts are apparently required for each new frame.
  SetTimer( hWnd,
            IDT_REFRESH,
            500,
            (TIMERPROC) NULL );

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
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
  io.ConfigViewportsNoAutoMerge      = false;
  io.ConfigViewportsNoTaskBarIcon    =  true;
  io.ConfigViewportsNoDefaultParent  = false;
  io.ConfigDockingAlwaysTabBar       = false;
  io.ConfigDockingTransparentPayload =  true;


  if (SKIF_bDisableDPIScaling)
  {
    io.ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
  //io.ConfigFlags |=  ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI
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

  // Setup Platform/Renderer bindings
  ImGui_ImplWin32_Init (hWnd);
  ImGui_ImplDX11_Init  (g_pd3dDevice, g_pd3dDeviceContext);

  SKIF_ImGui_InitFonts ();

  // Our state
  ImVec4 clear_color         =
    ImVec4 (0.45F, 0.55F, 0.60F, 1.00F);

  // Main loop
  MSG msg = { };

  CComQIPtr <IDXGISwapChain3>
      pSwap3 (g_pSwapChain);
  if (pSwap3 != nullptr)
  {
    pSwap3->SetMaximumFrameLatency (1);

    hSwapWait.Attach (
      pSwap3->GetFrameLatencyWaitableObject ()
    );
  }

  int monitor_idx = 0;
  ImGuiPlatformMonitor* monitor = nullptr;
  ImVec2 monitor_wz,
         windowPos;
  ImRect monitor_extent   = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
  bool changedMode        = false,
       todoFixUglyHack    = (! PathFileExistsW(L"imgui.ini") || SKIF_bOpenAtCursorPosition);

  // Handle cases where a Start / Stop Command Line was Passed,
  //   but no running instance existed to service it yet...
  _Signal._Disowned = TRUE;

  if      (_Signal.Start)
    SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);
  else if (_Signal.Stop)
    SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);

  while (IsWindow (hWnd) && msg.message != WM_QUIT)
  {                         msg          = { };
    auto _TranslateAndDispatch = [&](void) -> bool
    {
      while ( PeekMessage (&msg, 0, 0U, 0U, PM_REMOVE) &&
                            msg.message  !=  WM_QUIT)
      {
        if (! IsWindow (hWnd))
          return false;

        TranslateMessage (&msg);
        DispatchMessage  (&msg);
      }

      return
        ( msg.message != WM_QUIT );
    };

    auto _UpdateOcclusionStatus = [&](void)
    {
      HDC hDC =
        GetWindowDC (hWnd);

      if (hDC != 0)
      {
        using  GetClipBox_pfn = int (WINAPI *)(HDC,LPRECT);
        static GetClipBox_pfn
          SKIF_GetClipBox     = (GetClipBox_pfn)GetProcAddress (
                LoadLibraryEx ( L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
              "GetClipBox"                                     );

             RECT              rcClipBox = { };
        SKIF_GetClipBox (hDC, &rcClipBox);

        bOccluded =
          IsRectEmpty (&rcClipBox);

        ReleaseDC (hWnd, hDC);
      }

      else
        bOccluded = FALSE;
    };

    const int           max_wait_objs  = 2;
    HANDLE hWaitStates [max_wait_objs] = {
      hSwapWait.m_h,
      hInjectAck.m_h,
    };

    int num_wait_objs =
      ( hInjectAck.m_h > 0 ) ?
                           2 : 1;

    DWORD dwWait =
      MsgWaitForMultipleObjects ( num_wait_objs, hWaitStates, FALSE,
                                    INFINITE, QS_ALLINPUT );

    //static int frameCount = 0;

    if (dwWait == WAIT_OBJECT_0 + num_wait_objs)
    {
      if (! _TranslateAndDispatch ())
        break;
    }

    else if (dwWait == WAIT_OBJECT_0)
    {
      //// Injection acknowledgment; shutdown injection
      if (                     hInjectAck.m_h != 0 &&
          WaitForSingleObject (hInjectAck.m_h, 0) == WAIT_OBJECT_0)
      {
        hInjectAck.Close ();
        PostMessage (hWnd, WM_SKIF_STOP, 0, 0);
        if (bStopOnInjection)
        {
          bStopOnInjection = false;
          PostMessage(hWnd, WM_QUIT, 0, 0);
        }
      }

      /*
      OutputDebugString(L"New Frame ");
      OutputDebugString(std::to_wstring(frameCount).c_str());
      OutputDebugString(L"\n");
      frameCount++;
      */

      io.FontGlobalScale = 1.0f;

      // Handling sub-1000px resolutions by rebuilding the font at 11px
      static bool tinyDPIFonts = false;

      if (SKIF_ImGui_GlobalDPIScale < 1.0f && (! tinyDPIFonts))
      {
        SKIF_ImGui_InitFonts (11.0F);
        ImGui::GetIO ().Fonts->Build ();
        ImGui_ImplDX11_InvalidateDeviceObjects ();

        tinyDPIFonts = true;
      }

      else if (SKIF_ImGui_GlobalDPIScale >= 1.0f && tinyDPIFonts)
      {
        SKIF_ImGui_InitFonts ();
        ImGui::GetIO ().Fonts->Build ();
        ImGui_ImplDX11_InvalidateDeviceObjects ();

        tinyDPIFonts = false;
      }

      // Start the Dear ImGui frame
      ImGui_ImplDX11_NewFrame  ();
      ImGui_ImplWin32_NewFrame ();
      ImGui::NewFrame          ();
      {

        // Fixes the wobble that occurs when switching between tabs,
        //  as the width/height of the window isn't dynamically calculated.
#define SKIF_wLargeMode 1038
#define SKIF_hLargeMode  944 // Does not include the status bar
#define SKIF_wSmallMode  415
#define SKIF_hSmallMode  305

        static ImVec2 SKIF_vecSmallMode,
                      SKIF_vecLargeMode,
                      SKIF_vecCurrentMode;

        SKIF_vecSmallMode   = ImVec2 ( SKIF_wSmallMode * SKIF_ImGui_GlobalDPIScale,
                                       SKIF_hSmallMode * SKIF_ImGui_GlobalDPIScale );
        SKIF_vecLargeMode   = ImVec2 ( SKIF_wLargeMode * SKIF_ImGui_GlobalDPIScale,
                                       SKIF_hLargeMode * SKIF_ImGui_GlobalDPIScale );

        // Add the status bar if it is not disabled
        if ( ! SKIF_bDisableStatusBar )
        {
          SKIF_vecLargeMode.y += 31.0f * SKIF_ImGui_GlobalDPIScale;
          SKIF_vecLargeMode.y += (SKIF_bDisableTooltips) ?
                    (ImGui::GetTextLineHeight () * 0.7f) : 0.0f;
        }

        // Fixes weird size difference with this exact combination of settings which only occurs when DPI scaling is disabled
        if ((! SKIF_bDisableStatusBar) && SKIF_bDisableTooltips && SKIF_bDisableDPIScaling)
          SKIF_vecLargeMode.y += 6.0f;

        SKIF_vecCurrentMode  =
                      (SKIF_bSmallMode) ? SKIF_vecSmallMode
                                        : SKIF_vecLargeMode;

        ImGui::SetNextWindowSize (SKIF_vecCurrentMode);

        // Fix for window being created in the bottom right corner on first ever launch when an imgui.ini file is missing
        //   Usually just using ImGuiCond_FirstUseEver should do the trick, but for some reason it fails, which causes the above section with boundaries calculations
        //     to never trigger when changing modes etc... So instead we make the call only if imgui.ini is actually missing from the working directory.
        if (todoFixUglyHack)
        {
          todoFixUglyHack = false;

          //ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));

          // Positiong the window at the mouse cursor
          ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x - SKIF_vecCurrentMode.x / 2.0f, ImGui::GetMousePos().y - SKIF_vecCurrentMode.y / 2.0f));

          /*
          if (SKIF_bOpenAtCursorPosition)
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x - SKIF_vecCurrentMode.x / 2.0f, ImGui::GetMousePos().y - SKIF_vecCurrentMode.y / 2.0f));
          else
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_FirstUseEver);
          */
        }

        if (RepositionSKIF)
        {
          //OutputDebugString(L"RepositionSKIF recognized\n");
          // Repositions the window in the center of the current monitor it is on
          //ImGui::SetNextWindowPos(ImVec2(monitor_extent.GetCenter().x - SKIF_vecCurrentMode.x / 2.0f, monitor_extent.GetCenter().y - SKIF_vecCurrentMode.y / 2.0f));
          ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x - SKIF_vecCurrentMode.x / 2.0f, ImGui::GetMousePos().y - SKIF_vecCurrentMode.y / 2.0f));
          SetForegroundWindow(SKIF_hWnd);
          RepositionSKIF = false;
        }

        // Calculate new window boundaries and changes to fit within the workspace if it doesn't fit
        //   Delay running the code to on the third frame to allow other required parts to have already executed...
        //     Otherwise window gets positioned wrong on smaller monitors !
        if (changedMode && ImGui::GetFrameCount() > 2)
        {
          changedMode = false;

          ImVec2 topLeft      = ImVec2( windowPos.x,
                                        windowPos.y ),
                 bottomRight  = ImVec2( windowPos.x + SKIF_vecCurrentMode.x,
                                        windowPos.y + SKIF_vecCurrentMode.y ),
                 newWindowPos = windowPos;

          if ( topLeft.x < monitor_extent.Min.x)
            newWindowPos.x = monitor_extent.Min.x;
          if ( topLeft.y < monitor_extent.Min.y )
            newWindowPos.y = monitor_extent.Min.y;

          if ( bottomRight.x > monitor_extent.Max.x )
            newWindowPos.x = monitor_extent.Max.x - SKIF_vecCurrentMode.x;
          if ( bottomRight.y > monitor_extent.Max.y )
            newWindowPos.y = monitor_extent.Max.y - SKIF_vecCurrentMode.y;

          if ( newWindowPos.x != windowPos.x || newWindowPos.y != windowPos.y )
            ImGui::SetNextWindowPos(newWindowPos);
        }

        ImGui::Begin ( SKIF_WINDOW_TITLE_A SKIF_WINDOW_HASH,
                         nullptr,
                           ImGuiWindowFlags_NoResize         |
                           ImGuiWindowFlags_NoCollapse       |
                           ImGuiWindowFlags_NoTitleBar       |
                           ImGuiWindowFlags_NoScrollbar      | // Hide the scrollbar for the main window
                           ImGuiWindowFlags_NoScrollWithMouse  // Prevent scrolling with the mouse as well
        );

        // Update current monitors/worksize etc;
        monitor_idx =  ImGui::GetCurrentWindowRead ()->ViewportAllowPlatformMonitorExtend;
        monitor     = &ImGui::GetPlatformIO        ().Monitors [monitor_idx];
        monitor_wz  = monitor->WorkSize;

        if ( monitor_wz.y < SKIF_hLargeMode && ImGui::GetFrameCount() > 1 )
        {
          SKIF_ImGui_GlobalDPIScale = (monitor_wz.y - 100.0f) / SKIF_hLargeMode;
        } else {
          SKIF_ImGui_GlobalDPIScale = (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts) ? (ImGui::GetCurrentWindow ()->Viewport->DpiScale <= 2.0f) ? ImGui::GetCurrentWindow ()->Viewport->DpiScale : 2.0f : 1.0f;
          /////SKIF_ImGui_GlobalDPIScale = (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts) ? ImGui::GetCurrentWindow()->Viewport->DpiScale : 1.0f;
        }

        /*
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
          style.WindowRounding = 5.0F * SKIF_ImGui_GlobalDPIScale;
          style.Colors[ImGuiCol_WindowBg].w = 1.0F;
        }*/

        // Rescale the style on DPI changes
        style.WindowRounding    =          4.0F                                      * SKIF_ImGui_GlobalDPIScale; // style.ScrollbarRounding;
        style.ChildRounding     = style.WindowRounding;
        style.TabRounding       = style.WindowRounding;
        style.FrameRounding     = style.WindowRounding;
        style.FramePadding      = ImVec2 (SKIF_ImGui_DefaultStyle.FramePadding.x     * SKIF_ImGui_GlobalDPIScale,
                                          SKIF_ImGui_DefaultStyle.FramePadding.y     * SKIF_ImGui_GlobalDPIScale);
        style.ItemSpacing       = ImVec2 (SKIF_ImGui_DefaultStyle.ItemSpacing.x      * SKIF_ImGui_GlobalDPIScale,
                                          SKIF_ImGui_DefaultStyle.ItemSpacing.y      * SKIF_ImGui_GlobalDPIScale);
        style.ItemInnerSpacing  = ImVec2 (SKIF_ImGui_DefaultStyle.ItemInnerSpacing.x * SKIF_ImGui_GlobalDPIScale,
                                          SKIF_ImGui_DefaultStyle.ItemInnerSpacing.y * SKIF_ImGui_GlobalDPIScale);
        style.IndentSpacing     =         SKIF_ImGui_DefaultStyle.IndentSpacing      * SKIF_ImGui_GlobalDPIScale;
        style.ColumnsMinSpacing =         SKIF_ImGui_DefaultStyle.ColumnsMinSpacing  * SKIF_ImGui_GlobalDPIScale;
        style.ScrollbarSize     =         SKIF_ImGui_DefaultStyle.ScrollbarSize      * SKIF_ImGui_GlobalDPIScale;
        // Finish style rescale


        ImGuiTabBarFlags flags =
          ImGuiTabItemFlags_None;

        FLOAT SKIF_GetHDRWhiteLuma (void);
        void  SKIF_SetHDRWhiteLuma (FLOAT);

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

        static float fLuma =
          _InitFromRegistry ();

        auto _DrawHDRConfig = [&](void)
        {
          static bool bFullRange = false;

          FLOAT fMaxLuma =
            SKIF_GetMaxHDRLuminance (bFullRange);

          if (fMaxLuma != 0.0f)
          {
            ImGui::TreePush("");
            ImGui::SetNextItemWidth(300.0f * SKIF_ImGui_GlobalDPIScale);
            if (ImGui::SliderFloat ("###HDR Paper White", &fLuma, 80.0f, fMaxLuma, u8"HDR White:\t%04.1f cd/m²"))
            {
              SKIF_SetHDRWhiteLuma (fLuma);
              regKVLuma.putData    (fLuma);
            }
            ImGui::TreePop ( );
            ImGui::Spacing();
          }
        };

        enum _Selection {
          Injection,
          Settings,
          Help,
          Debug
        } static tab_selected = Injection;

        SK_RunOnce (
          _inject._RefreshSKDLLVersions ()
        );


        // Top right window buttons
        ImVec2 topCursorPos =
          ImGui::GetCursorPos ();

        ImGui::SetCursorPos (
          ImVec2 ( SKIF_vecCurrentMode.x - 120.0f * SKIF_ImGui_GlobalDPIScale,
                                             4.0f * SKIF_ImGui_GlobalDPIScale )
        );

        ImGui::PushStyleVar (
          ImGuiStyleVar_FrameRounding, 25.0f * SKIF_ImGui_GlobalDPIScale
        );

        if (ImGui::Button ( (SKIF_bSmallMode) ? ICON_FA_EXPAND_ARROWS_ALT
                                              : ICON_FA_COMPRESS_ARROWS_ALT,
                              ImVec2 ( 40.0f * SKIF_ImGui_GlobalDPIScale,
                                        0.0f ) )
           )
        {
          SKIF_ProcessCommandLine ("SKIF.UI.SmallMode Toggle");

          regKVSmallMode.putData (  SKIF_bSmallMode);

          changedMode = true;
        }

        ImGui::SameLine ();

        if ( ImGui::Button (ICON_FA_WINDOW_MINIMIZE, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale,
                                                               0.0f ) ) )
        {
          ShowWindow (hWnd, SW_MINIMIZE);
        }

        ImGui::SameLine ();

        if ( ImGui::Button (ICON_FA_WINDOW_CLOSE, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale,
                                                            0.0f ) )
            || bKeepWindowAlive == false
           )
        {
          if (_inject.bCurrentState && (!SKIF_bDisableExitConfirmation))
          {
            ImGui::OpenPopup("Confirm Exit");
          }
          else
          {
            if (_inject.bCurrentState && ! SKIF_bAllowBackgroundService )
              _inject._StartStopInject (true);

            bKeepProcessAlive = false;
          }
        }

        ImGui::PopStyleVar ();

        if (_inject.bCurrentState && SKIF_bDisableExitConfirmation && SKIF_bAllowBackgroundService)
          SKIF_ImGui_SetHoverTip ("Service continues running after SKIF is closed");

        ImGui::SetCursorPos (topCursorPos);

        // End of top right window buttons


        ImGui::BeginGroup ();

        // Begin Small Mode
        if (SKIF_bSmallMode)
        {
          auto smallMode_id =
            ImGui::GetID ("###Small_Mode_Frame");

          // A new line to allow the window titlebar buttons to be accessible
          ImGui::NewLine ();

          SKIF_ImGui_BeginChildFrame ( smallMode_id,
            ImVec2 ( 400.0f * SKIF_ImGui_GlobalDPIScale,
                      12.0f * ImGui::GetTextLineHeightWithSpacing () ),
              ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NavFlattened      |
              ImGuiWindowFlags_NoScrollbar            | ImGuiWindowFlags_NoScrollWithMouse |
              ImGuiWindowFlags_NoBackground
          );

          _inject._GlobalInjectionCtl ();

          ImGui::EndChildFrame ();
        } // End Small Mode

        // Begin Large Mode
        else
        {
          ImGui::BeginTabBar ( "###SKIF_TAB_BAR",
                                 ImGuiTabBarFlags_FittingPolicyResizeDown |
                                 ImGuiTabBarFlags_FittingPolicyScroll );

          if (ImGui::BeginTabItem (ICON_FA_SYRINGE " Injection"))
          {
            /* TODO: Fix mouse pos changing on launch in manage_games.cpp before enabling this! */
            if (!SKIF_bFirstLaunch)
            {
              // Select the Help tab on first launch
              SKIF_bFirstLaunch = !SKIF_bFirstLaunch;
              flags = ImGuiTabItemFlags_SetSelected;

              // Store in the registry so this only occur once.
              regKVFirstLaunch.putData(SKIF_bFirstLaunch);
            }

            if (tab_selected != Injection)
              _inject._RefreshSKDLLVersions();

            tab_selected = Injection;

            extern void SKIF_GameManagement_DrawTab(void);
            SKIF_GameManagement_DrawTab();

            ImGui::EndTabItem ();
          }

          if (ImGui::BeginTabItem (ICON_FA_COG " Settings"))
          {
            SKIF_ImGui_BeginTabChildFrame ();

            static std::wstring
                       driverBinaryPath    = L"";

            enum Status {
              NotInstalled,
              Installed,
              OtherDriverInstalled
            }

            static driverStatus        = NotInstalled,
                   driverStatusPending = NotInstalled;

            // Check if the WinRing0_1_2_0 kernel driver service is installed or not
            auto _CheckDriver = [](Status& _status)->std::wstring
            {
              std::wstring       binaryPath = L"";
              SC_HANDLE        schSCManager = NULL,
                                svcWinRing0 = NULL;
              LPQUERY_SERVICE_CONFIG   lpsc = {  };
              DWORD                    dwBytesNeeded,
                                       cbBufSize {},
                                       dwError;

              // Reset the current status to not installed.
              _status = NotInstalled;

              // Get a handle to the SCM database.
              schSCManager =
                OpenSCManager (
                  nullptr,             // local computer
                  nullptr,             // servicesActive database
                  STANDARD_RIGHTS_READ // enumerate services
                );

              if (nullptr != schSCManager)
              {
                // Get a handle to the service.
                svcWinRing0 =
                  OpenService (
                    schSCManager,        // SCM database
                    L"WinRing0_1_2_0",   // name of service
                    SERVICE_QUERY_CONFIG // query config
                  );

                if (nullptr != svcWinRing0)
                {
                  // Attempt to get the configuration information to get an idea of what buffer size is required.
                  if (! QueryServiceConfig (
                          svcWinRing0,
                            nullptr, 0,
                              &dwBytesNeeded )
                     )
                  {
                    dwError =
                      GetLastError ();

                    if (ERROR_INSUFFICIENT_BUFFER == dwError)
                    {
                      cbBufSize = dwBytesNeeded;
                      lpsc      = (LPQUERY_SERVICE_CONFIG)LocalAlloc (LMEM_FIXED, cbBufSize);

                      // Get the configuration information with the necessary buffer size.
                      if ( QueryServiceConfig (
                             svcWinRing0,
                               lpsc, cbBufSize,
                                 &dwBytesNeeded )
                         )
                      {
                        // Store the binary path of the installed driver.
                        binaryPath = std::wstring (lpsc->lpBinaryPathName);

                        // Check if 'SpecialK' can be found in the path.
                        if (binaryPath.find (L"SpecialK") != std::wstring::npos)
                          _status = Installed; // SK driver installed
                        else
                          _status = OtherDriverInstalled; // Other driver installed
                      }
                      LocalFree (lpsc);
                    }
                  }
                  CloseServiceHandle (svcWinRing0);
                }
                CloseServiceHandle (schSCManager);
              }

              return binaryPath;
            };

            // Driver is supposedly getting a new state -- check for an update
            //   on each frame until driverStatus matches driverStatusPending
            if (driverStatusPending != driverStatus)
              driverBinaryPath = _CheckDriver (driverStatus);

            // Reset and refresh things when visiting from another tab
            if (tab_selected != Settings)
            {
              driverBinaryPath    = _CheckDriver (driverStatus);
              driverStatusPending =               driverStatus;

              _inject._RefreshSKDLLVersions ();
            }

            tab_selected = Settings;

            // SKIF Options
            if (ImGui::CollapsingHeader ("Frontend v " SKIF_VERSION_STR_A " (" __DATE__ ")", ImGuiTreeNodeFlags_DefaultOpen))
            {
              ImGui::Spacing    ( );

              ImGui::Columns    (2, nullptr, true);

              SK_RunOnce(
                ImGui::SetColumnWidth(0, SKIF_vecCurrentMode.x / 2.0f)
              );

              if (ImGui::Checkbox("When closing SKIF allow the global injector to remain active",
                                                     &SKIF_bAllowBackgroundService))
                regKVAllowBackgroundService.putData  (SKIF_bAllowBackgroundService);

              _inject._StartAtLogonCtrl ( );

              ImGui::Spacing       ( );
              ImGui::Spacing       ( );

              ImGui::Text("Disable UI elements");
              ImGui::TreePush("");

              if (ImGui::Checkbox ("Exit prompt  ",
                                                     &SKIF_bDisableExitConfirmation))
                regKVDisableExitConfirmation.putData (SKIF_bDisableExitConfirmation);

              if (SKIF_bAllowBackgroundService)
                SKIF_ImGui_SetHoverTip(
                  "The global injector will remain active in the background."
                );
              else
                SKIF_ImGui_SetHoverTip (
                  "The global injector will stop automatically."
                );

              ImGui::SameLine ( );
              ImGui::Spacing  ( );
              ImGui::SameLine ( );

              if (ImGui::Checkbox ("Tooltips", &SKIF_bDisableTooltips))
                regKVDisableTooltips.putData (             SKIF_bDisableTooltips);

              if (ImGui::IsItemHovered ())
                SKIF_StatusBarText = "Info: ";

              SKIF_ImGui_SetHoverText ("This is where the info will be displayed.");
              SKIF_ImGui_SetHoverTip  ("The info will instead be displayed in the status bar at the bottom."
                                       "\nNote that some links cannot be previewed as a result.");

              ImGui::SameLine ( );
              ImGui::Spacing  ( );
              ImGui::SameLine ( );

              if (ImGui::Checkbox ("Status bar", &SKIF_bDisableStatusBar))
                regKVDisableStatusBar.putData (              SKIF_bDisableStatusBar);

              SKIF_ImGui_SetHoverTip (
                "Combining this with disabled UI tooltips will hide all context based information or tips."
              );

              // New line

              if (ImGui::Checkbox ("HiDPI scaling", &SKIF_bDisableDPIScaling))
              {
                io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

                if (SKIF_bDisableDPIScaling)
                  io.ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleFonts;

                regKVDisableDPIScaling.putData (SKIF_bDisableDPIScaling);
              }

              SKIF_ImGui_SetHoverTip (
                "This application will appear much smaller on HiDPI monitors."
              );

              ImGui::SameLine ( );
              ImGui::Spacing  ( );
              ImGui::SameLine ( );

              if (ImGui::Checkbox ("Steam Library (restart required)", &SKIF_bDisableSteamLibrary))
                regKVDisableSteamLibrary.putData (                   SKIF_bDisableSteamLibrary);

              SKIF_ImGui_SetHoverTip (
                "This will prevent SKIF from listing discovered Steam games."
              );

              if (SKIF_bDisableTooltips && SKIF_bDisableStatusBar)
              {
                ImGui::BeginGroup     ( );
                ImGui::Spacing        ( );
                ImGui::SameLine       ( );
                ImGui::TextColored    (ImColor::HSV (0.55F, 0.99F, 1.F), u8"• ");
                ImGui::SameLine       ( );
                ImGui::PushStyleColor (ImGuiCol_Text, ImVec4(0.68F, 0.68F, 0.68F, 1.0f));
                ImGui::TextWrapped    ("Both tooltips and status bar are disabled; context based information or tips will not appear!");
                ImGui::PopStyleColor  ( );
                ImGui::EndGroup       ( );
              }

              ImGui::TreePop();

              ImGui::NextColumn    ( );

              // New column

              ImGui::Text          ("Experimental SKIF features");
              ImGui::TreePush      ("");

              if (ImGui::Checkbox  ("HDR on compatible displays (restart required)###HDR_ImGui", &SKIF_bEnableHDR))
                regKVEnableHDR.putData (                                                          SKIF_bEnableHDR);

              _DrawHDRConfig       ( );

              if (ImGui::Checkbox ("Debug Mode (" ICON_FA_BUG ")",
                                                         &SKIF_bEnableDebugMode))
              {
                SKIF_ProcessCommandLine ( ( std::string ("SKIF.UI.DebugMode ") +
                                            std::string ( SKIF_bEnableDebugMode ? "On"
                                                                                : "Off" )
                                          ).c_str ()

                );
                regKVEnableDebugMode.putData(             SKIF_bEnableDebugMode);
              }

              if (SKIF_bEnableDebugMode)
              {
                  SKIF_ImGui_SetHoverTip (
                    "Halt " ICON_FA_EXCLAMATION_CIRCLE "\n " ICON_FA_ELLIPSIS_H
                    " Try Not To Catch Fire " ICON_FA_FREE_CODE_CAMP
                  );

                ImGui::TreePush ("");

                if ( ImGui::Checkbox (
                       "Allow Multiple Instances of SKIF",
                         &SKIF_bAllowMultipleInstances )
                   )
                {
                  if (! SKIF_bAllowMultipleInstances)
                  {
                    // Immediately close out any duplicate instances, they're undesirables
                    EnumWindows ( []( HWND   hWnd,
                                      LPARAM lParam ) -> BOOL
                    {
                      wchar_t                         wszRealWindowClass [64] = { };
                      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
                      {
                        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
                        {
                          if (SKIF_hWnd != hWnd)
                            PostMessage (  hWnd, WM_QUIT,
                                            0x0, 0x0  );
                        }
                      }
                      return TRUE;
                    }, (LPARAM)SKIF_WindowClass);
                  }

                  regKVAllowMultipleInstances.putData (
                    SKIF_bAllowMultipleInstances
                   );
                }

                if ( ImGui::Checkbox ( "Open SKIF at the position of the mouse", &SKIF_bOpenAtCursorPosition ) )
                  regKVOpenAtCursorPosition.putData(                              SKIF_bOpenAtCursorPosition );

                ImGui::TreePop  ( );
              }
              ImGui::TreePop    ( );
              ImGui::Columns    (1);
            }

            ImGui::Spacing ();
            ImGui::Spacing ();


            if (ImGui::CollapsingHeader ("Advanced Monitoring"))
            {
              // PresentMon prerequisites
              ImGui::BeginGroup  ();
              ImGui::Spacing     ();

              ImGui::Text        ("SwapChain Presentation Monitor");

              ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f));
              ImGui::TextWrapped    (
                "Special K can give users an insight into how frames is presented by tracking ETW events and changes as they occur."
              );
              ImGui::PopStyleColor  ();

              ImGui::Spacing     ();
              ImGui::Spacing     ();

              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "Tell at a glance whether:"
              );

              ImGui::BeginGroup  ();
              ImGui::Spacing     ();
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"?!");
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "DirectFlip optimizations are engaged, and desktop composition (DWM) is bypassed."
              );
              ImGui::EndGroup    ();

              SKIF_ImGui_SetHoverTip("Appears as 'Hardware [Composed]: Independent Flip'");
              SKIF_ImGui_SetMouseCursorHand ();
              SKIF_ImGui_SetHoverText       ("https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model#directflip");

              if (ImGui::IsItemClicked      ())
                SKIF_Util_OpenURI           (L"https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model#directflip");

              ImGui::BeginGroup  ();
              ImGui::Spacing     ();
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"?!");
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "Legacy Exclusive Fullscreen (FSE) mode has enaged or if Fullscreen Optimizations (FSO) overrides it."
              );
              ImGui::EndGroup    ();

              SKIF_ImGui_SetHoverTip(
                                  "FSE appears as 'Hardware: Legacy Flip' or 'Hardware: Legacy Copy to front buffer'"
                                  "\nFSO appears as 'Hardware [Composed]: Independent Flip'"
              );
              SKIF_ImGui_SetMouseCursorHand ();
              SKIF_ImGui_SetHoverText       ("https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

              if (ImGui::IsItemClicked      ())
                SKIF_Util_OpenURI           (L"https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

              ImGui::BeginGroup  ();
              ImGui::Spacing     ();
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"? ");
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "The game is running in a suboptimal presentation mode."
              );
              ImGui::EndGroup    ();

              SKIF_ImGui_SetHoverTip("Appears as 'Composed: [Flip|Copy with GPU GDI|Copy with CPU GDI|Composition Atlas]'");

              ImGui::Spacing();
              ImGui::Spacing();

              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "Requirement:"
              );

              static BOOL  pfuAccessToken = FALSE;
              static BYTE  pfuSID[SECURITY_MAX_SID_SIZE];
              static DWORD cbSize = sizeof(pfuSID);

              SK_RunOnce ( CreateWellKnownSid   (WELL_KNOWN_SID_TYPE::WinBuiltinPerfLoggingUsersSid, NULL, &pfuSID, &cbSize));
              SK_RunOnce ( CheckTokenMembership (NULL, &pfuSID, &pfuAccessToken));

              enum pfuPermissions {
                Missing,
                Granted,
                Pending
              } static pfuState = (pfuAccessToken) ? Granted : Missing;

              ImGui::BeginGroup  ();
              ImGui::Spacing     ();
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"• ");
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F), "Granted 'Performance Log Users' permission?");
              ImGui::SameLine    ();
              if      (pfuState == Granted)
                ImGui::TextColored (ImColor::HSV ( 0.3F, 0.99F, 1.F), "Yes");
              else if (pfuState == Missing)
                ImGui::TextColored (ImColor::HSV (0.11F,   1.F, 1.F), "No");
              else // (pfuState == Pending)
                ImGui::TextColored (ImColor::HSV (0.11F,   1.F, 1.F), "Yes, but a sign out from Windows is needed to allow the changes to take effect.");
              ImGui::EndGroup    ();

              ImGui::Spacing  ();
              ImGui::Spacing  ();

              // Disable button for granted + pending states
              if (pfuState != Missing)
              {
                ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                                ImGui::GetStyle ().Alpha *
                                   ((SKIF_IsHDR ()) ? 0.1f
                                                    : 0.5f)
                );
              }

              std::string btnPfuLabel = (pfuState == Granted) ?                                ICON_FA_CHECK " Permissions granted!" // Granted
                                                              : (pfuState == Missing) ?   ICON_FA_SHIELD_ALT " Grant permissions"    // Missing
                                                                                      : ICON_FA_SIGN_OUT_ALT " Sign out to apply";   // Pending

              if ( ImGui::ButtonEx ( btnPfuLabel.c_str(), ImVec2( 200 * SKIF_ImGui_GlobalDPIScale,
                                                                   25 * SKIF_ImGui_GlobalDPIScale)))
              {
                if (
                  // Add NT AUTHORITY\INTERACTIVE to the BUILT-IN\Performance Log Users group
                  ShellExecuteW (
                    nullptr, L"runas",
                      L"powershell",
                        L"-NoProfile -NonInteractive -WindowStyle Hidden -Command \"Add-LocalGroupMember -SID 'S-1-5-32-559' -Member 'S-1-5-4'\"", nullptr,
                          SW_SHOW ) > (HINSTANCE)32) // COM exception is thrown?
                {
                  // PowerShell call succeeded!
                  pfuState    = Pending;
                }
              }

              // Disable button for granted + pending states
              else if (pfuState != Missing)
              {
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
              }

              else
              {
                SKIF_ImGui_SetHoverTip(
                  "Administrative privileges are required on the system to toggle this."
                );
              }

              ImGui::Spacing  ();
              ImGui::Spacing  ();

              ImGui::EndGroup    ();

              ImGui::Separator   ();

              // WinRing0
              ImGui::BeginGroup  ();
              ImGui::Spacing     ();

              ImGui::Text        ("Advanced CPU Hardware Reporting (Intel only)");

              ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f));
              ImGui::TextWrapped    (
                "Special K can make use of an optional kernel driver to provide additional metrics in the CPU widget."
              );
              ImGui::PopStyleColor  ();

              ImGui::Spacing     ();
              ImGui::Spacing     ();

              ImGui::BeginGroup  ();
              ImGui::Spacing     ();
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"• ");
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "Extends the CPU widget with thermals, energy, and precise clock rate on supported Intel hardware."
              );
              ImGui::EndGroup    ();

              ImGui::Spacing();
              ImGui::Spacing();

              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F),
                                  "Requirement:"
              );

              ImGui::BeginGroup  ();
              ImGui::Spacing     ();
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"• ");
              ImGui::SameLine    ();
              ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F), "Kernel Driver:");
              ImGui::SameLine    ();

              static std::string btnDriverLabel;
              const wchar_t* wszDriverTaskCmd = L"";

              static bool requiredFiles =
                PathFileExistsW (LR"(Servlet\driver_install.bat)")   &&
                PathFileExistsW (LR"(Servlet\driver_install.ps1)")   &&
                PathFileExistsW (LR"(Servlet\driver_uninstall.bat)") &&
                PathFileExistsW (LR"(Servlet\driver_uninstall.ps1)");

              // Missing required files
              if (! requiredFiles)
              {
                btnDriverLabel = "Not available";
                ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), "Unsupported");
              }

              // Status is pending...
              else if (driverStatus != driverStatusPending)
              {
                btnDriverLabel = ICON_FA_SPINNER " Please Wait...";
                ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), "Pending...");
              }

              // Driver is installed
              else if (driverStatus == Installed)
              {
                wszDriverTaskCmd = LR"(Servlet\driver_uninstall.bat)";
                btnDriverLabel   = ICON_FA_SHIELD_ALT " Uninstall Driver";
                ImGui::TextColored (ImColor::HSV (0.3F, 0.99F, 1.F), "Installed");
              }

              // Other driver is installed
              else if (driverStatus == OtherDriverInstalled)
              {
                btnDriverLabel = ICON_FA_BAN " Unavailable";
                ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), "Unsupported");
              }

              // Driver is not installed
              else {
                wszDriverTaskCmd = LR"(Servlet\driver_install.bat)";
                btnDriverLabel   = ICON_FA_SHIELD_ALT " Install Driver";
                ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), "Not Installed");
              }

              ImGui::EndGroup ();

              ImGui::Spacing  ();
              ImGui::Spacing  ();

              // Disable button if the required files are missing, status is pending, or if another driver is installed
              if ( (! requiredFiles)                     ||
                     driverStatusPending != driverStatus ||
                    OtherDriverInstalled == driverStatus )
              {
                ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                                ImGui::GetStyle ().Alpha *
                                   ((SKIF_IsHDR ()) ? 0.1f
                                                    : 0.5f)
                );
              }

              // Show button
              bool driverButton =
                ImGui::ButtonEx (btnDriverLabel.c_str(), ImVec2(200 * SKIF_ImGui_GlobalDPIScale,
                                                                 25 * SKIF_ImGui_GlobalDPIScale));

              SKIF_ImGui_SetHoverTip (
                "Administrative privileges are required on the system to enable this."
              );

              if ( driverButton && requiredFiles )
              {
                if (
                  ShellExecuteW (
                    nullptr, L"runas",
                      wszDriverTaskCmd,
                        nullptr, nullptr,
                          SW_HIDE
                  ) > (HINSTANCE)32 )
                {
                  // Batch call succeeded -- change driverStatusPending to the
                  //   opposite of driverStatus to signal that a new state is pending.
                  driverStatusPending =
                        (driverStatus == Installed) ?
                                       NotInstalled : Installed;
                }
              }

              // Disabled button
              //   the 'else if' is only to prevent the code from being called on the same frame as the button is pressed
              else if ( (! requiredFiles)                     ||
                          driverStatusPending != driverStatus ||
                         OtherDriverInstalled == driverStatus )
              {
                ImGui::PopStyleVar ();
                ImGui::PopItemFlag ();
              }

              // Show warning about missing files
              if (!requiredFiles)
              {
                ImGui::SameLine   ();
                ImGui::BeginGroup ();
                ImGui::Spacing    ();
                ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), u8"• ");
                ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),
                                                          "Option is unavailable as one or more of the required files are missing."
                );
                ImGui::EndGroup   ();
              }

              // Show warning about another driver being installed
              else if (OtherDriverInstalled == driverStatus)
              {
                ImGui::SameLine   ();
                ImGui::BeginGroup ();
                ImGui::Spacing    ();
                ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), "? ");
                ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),
                                                          "Option is unavailable as another application have already installed a copy of the driver."
                );
                ImGui::EndGroup   ();

                SKIF_ImGui_SetHoverTip (
                  SK_WideCharToUTF8 (driverBinaryPath).c_str ()
                );
              }

              ImGui::EndGroup ();
            }

            ImGui::Spacing ();
            ImGui::Spacing ();

            // Whitelist/Blacklist
            if (ImGui::CollapsingHeader ("Whitelist/Blacklist", ImGuiTreeNodeFlags_DefaultOpen))
            {
              //static std::wstring root_dir =
                //std::wstring (path_cache.specialk_userdata.path) + LR"(\Global\)";

              //static char whitelist [MAX_PATH * 16 * 2] = { };
              //static char blacklist [MAX_PATH * 16 * 2] = { };
              static bool white_edited = false,
                          black_edited = false,
                          white_stored = true,
                          black_stored = true;

              auto _StoreList = [](char* szOut, std::wstring fname)->bool
              {
                bool ret = false;

                // std::ofstream list_file // UTF-8
                std::wofstream list_file ( // ANSI
                  fname
                );

                if (list_file.is_open ())
                {
                  /* ANSI */
                  std::wstring out_text =
                    SK_UTF8ToWideChar (szOut);

                  // Strip all null terminator \0 characters from the string
                  out_text.erase(std::find(out_text.begin(), out_text.end(), '\0'), out_text.end());

                  list_file.write ( out_text.c_str  (),
                                    out_text.length () );

                  if (list_file.good())
                    ret = true;

                  /* UTF-8
                  list_file.write ( szOut,
                            strlen( szOut) );
                  */
                  list_file.close ();
                }

                return ret;
              };

              auto _LoadList = [](char* szIn, std::wstring fname)->void
              {
                // std::ifstream list_file // UTF-8
                std::wifstream list_file( // ANSI
                  fname
                );

                // std::string full_text; // UTF-8
                std::wstring full_text; // ANSI

                if (list_file.is_open ())
                {
                  // std::string line; // UTF-8
                  std::wstring line; // ANSI

                  while (list_file.good ())
                  {
                    std::getline (
                      list_file, line
                    );

                    full_text += line;
                    full_text += L'\n';
                  }
                  full_text.resize (full_text.length () - 1);

                  list_file.close ();

                  /* ANSI */
                  strcpy ( szIn,
                             SK_WideCharToUTF8 (full_text).c_str ()

                  /* UTF-8
                  strcpy ( szIn,
                             full_text.c_str ()
                  */
                  );
                }
              };

              auto _CheckWarnings = [](char* szList)->void
              {
                static int i, count;

                if (strchr (szList, '\"') != nullptr)
                {
                  ImGui::BeginGroup ();
                  ImGui::Spacing    ();
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),  u8"• ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),    "Please remove all double quotes");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.86f, 0.2f,  0.27f), R"( " )");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),    "from the list.");
                  ImGui::EndGroup   ();
                }

                // Loop through the list, checking the existance of a lone \ not proceeded or followed by other \.
                // i == 0 to prevent szList[i - 1] from executing when at the first character.
                for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 16 * 2; i++)
                  count += (szList[i] == '\\' && szList[i + 1] != '\\' && (i == 0 || szList[i - 1] != '\\'));

                if (count > 0)
                {
                  ImGui::BeginGroup ();
                  ImGui::Spacing    ();
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),   "? ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),   "Folders must be separated using two backslashes");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.2f, 0.86f,  0.27f), R"( \\ )");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),   "instead of one");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.86f, 0.2f,  0.27f), R"( \ )");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),   "backslash.");
                  ImGui::EndGroup   ();

                  SKIF_ImGui_SetHoverTip (
                    R"(e.g. C:\\Program Files (x86)\\Uplay\\games)"
                  );
                }

                // Loop through the list, counting the number of occurances of a newline
                for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 16 * 2; i++)
                  count += (szList[i] == '\n');

                if (count > 15)
                {
                  ImGui::BeginGroup ();
                  ImGui::Spacing    ();
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),   "? ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),   "The list can only include");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.86f, 0.2f,  0.27f), " 16 ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),   "lines, though multiple can be combined using a pipe");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.2f,  0.86f, 0.27f), " | ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F),   "character.");
                  ImGui::EndGroup   ();

                  SKIF_ImGui_SetHoverTip (
                    R"(e.g. "NieRAutomataPC|Epic Games" will match any application"
                            "installed under a NieRAutomataPC or Epic Games folder.)"
                  );
                }
              };

              //SK_RunOnce (_inject._LoadList(true));
              //SK_RunOnce (_inject._LoadList(false));

              ImGui::BeginGroup ();
              ImGui::Spacing    ();

              ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f));
              ImGui::TextWrapped    ("The following lists manage Special K in processes as patterns are matched against the full path of the injected process.");
              ImGui::PopStyleColor  ();

              ImGui::Spacing    ();
              ImGui::Spacing    ();

              ImGui::BeginGroup ();
              ImGui::Spacing    ();
              ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),   "? ");
              ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F), "Easiest is to use the name of the executable or folder of the game.");
              ImGui::EndGroup   ();

              SKIF_ImGui_SetHoverTip (
                "e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at"
                 "\nC:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"
              );

              ImGui::BeginGroup ();
              ImGui::Spacing    ();
              ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),   "? ");
              ImGui::SameLine   (); ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F), "Typing the name of a shared parent folder will match all applications below that folder.");
              ImGui::EndGroup   ();

              SKIF_ImGui_SetHoverTip (
                "e.g. a pattern like \"Epic Games\" will match any"
                 "\napplication installed under the Epic Games folder."
              );

              ImGui::Spacing    ();
              ImGui::Spacing    ();

              ImGui::BeginGroup ();
              ImGui::Spacing    ();
              ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), "?!");
              ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F), "Note that these lists do not prevent Special K from being injected into processes.");
              ImGui::EndGroup   ();

              SKIF_ImGui_SetMouseCursorHand ();
              SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

              if (SKIF_bDisableTooltips)
              {
                SKIF_ImGui_SetHoverTip (
                  "These lists control whether Special K should be enabled (the whitelist) to hook APIs etc,"
                  "\nor remain disabled/idle/inert (the blacklist) within the injected process."
                );
              }

              else
              {
                SKIF_ImGui_SetHoverTip (
                  "The global injection service injects Special K into any process that deals"
                  "\nwith system input or some sort of window or keyboard/mouse input activity."
                  "\n\n"


                  "These lists control whether Special K should be enabled (the whitelist),"
                  "\nor remain idle/inert (the blacklist) within the injected process."
                );
              }

              if (ImGui::IsItemClicked ())
                SKIF_Util_OpenURI (L"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

              ImGui::Spacing ();
              ImGui::Spacing ();

              // Whitelist section

              ImGui::BeginGroup ();
              ImGui::Text       (" " ICON_FA_PLUS_CIRCLE "  Whitelist Patterns:");

              SKIF_ImGui_Spacing ();

              white_edited |=
                ImGui::InputTextEx ( "###WhitelistPatterns", "SteamApps",
                                       _inject.whitelist, MAX_PATH * 16 - 1,
                                         ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                                  150 * SKIF_ImGui_GlobalDPIScale ),
                                            ImGuiInputTextFlags_Multiline );

              if (*_inject.whitelist == '\0')
              {
                SKIF_ImGui_SetHoverTip (
                  "SteamApps is the pattern used internally to enable Special K for all Steam games."
                  "\nIt is presented here solely as an example of how a potential pattern might look like."
                );
              }

              _CheckWarnings (_inject.whitelist);

              ImGui::EndGroup   ();

              ImGui::SameLine   ();

              ImGui::BeginGroup ();
              ImGui::Text       ("Add Common Patterns:");

              SKIF_ImGui_Spacing ();

              ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f));
              ImGui::TextWrapped    ("Click on an item below to add it to the whitelist, or hover over it"
                                     "to display more information about what the pattern covers.");
              ImGui::PopStyleColor  ();

              SKIF_ImGui_Spacing ();
              SKIF_ImGui_Spacing ();

              ImGui::SameLine    ();
              ImGui::BeginGroup  ();
              ImGui::Text        (ICON_FA_GAMEPAD);
              ImGui::Text        (ICON_FA_WINDOWS);
              ImGui::EndGroup    ();

              ImGui::SameLine    ();

              ImGui::BeginGroup  ();
              if (ImGui::Selectable ("Games"))
              {
                white_edited = true;

                if (*_inject.whitelist == '\0')
                  snprintf (_inject.whitelist, sizeof _inject.whitelist, "%s%s", _inject.whitelist, "Games");
                else
                  snprintf (_inject.whitelist, sizeof _inject.whitelist, "%s%s", _inject.whitelist, "\nGames");
              }

              SKIF_ImGui_SetHoverTip (
                "Whitelists games on most platforms, such as Epic Games, Origin, Uplay, etc."
              );

              if (ImGui::Selectable ("WindowsApps"))
              {
                white_edited = true;

                if (*_inject.whitelist == '\0')
                  snprintf (_inject.whitelist, sizeof _inject.whitelist, "%s%s", _inject.whitelist, "WindowsApps");
                else
                  snprintf (_inject.whitelist, sizeof _inject.whitelist, "%s%s", _inject.whitelist, "\nWindowsApps");
              }

              SKIF_ImGui_SetHoverTip (
                "Whitelists games on the Microsoft Store."
              );
              ImGui::EndGroup ();
              ImGui::EndGroup ();

              ImGui::Spacing  ();
              ImGui::Spacing  ();

              // Blacklist section

              ImGui::Text (" " ICON_FA_MINUS_CIRCLE "  Blacklist Patterns:");

              SKIF_ImGui_Spacing ();

              black_edited |=
                ImGui::InputTextEx ( "###BlacklistPatterns", "launcher.exe",
                                       _inject.blacklist, MAX_PATH * 16 - 1,
                                         ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                                  100 * SKIF_ImGui_GlobalDPIScale ),
                                           ImGuiInputTextFlags_Multiline );

              _CheckWarnings (_inject.blacklist);

              ImGui::Separator ();

              bool bDisabled =
                (white_edited || black_edited) ?
                                         false : true;

              if (bDisabled)
              {
                ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                                ImGui::GetStyle ().Alpha *
                                   ((SKIF_IsHDR ()) ? 0.1f
                                                    : 0.5f));
              }

              // Hotkey: Ctrl+S
              if (ImGui::Button (ICON_FA_SAVE " Save Changes") || ((! bDisabled) && io.KeyCtrl && io.KeysDown ['S']))
              {
                if (white_edited)
                {
                  white_stored = _inject._StoreList(true);

                  if (white_stored)
                    white_edited = false;
                }

                if (black_edited)
                {
                  black_stored = _inject._StoreList (false);

                  if (black_stored)
                    black_edited = false;
                }
              }

              ImGui::SameLine ();

              if (ImGui::Button (ICON_FA_UNDO " Reset"))
              {
                if (white_edited)
                {
                  _inject._LoadList (true);

                  white_edited = false;
                  white_stored = true;
                }

                if (black_edited)
                {
                  _inject._LoadList(false);

                  black_edited = false;
                  black_stored = true;
                }
              }

              if (bDisabled)
              {
                ImGui::PopItemFlag  ( );
                ImGui::PopStyleVar  ( );
              }

              ImGui::Spacing();

              if (! white_stored)
              {
                  ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),  u8"• ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F), "The whitelist could not be saved! Please remove any non-Latin characters and try again.");
              }

              if (! black_stored)
              {
                  ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F),  u8"• ");
                  ImGui::SameLine   (); ImGui::TextColored (ImColor::HSV (0.11F, 1.F,   1.F), "The blacklist could not be saved! Please remove any non-Latin characters and try again.");
              }

              ImGui::EndGroup       ( );
            }

            ImGui::EndChildFrame    ( );
            ImGui::EndTabItem       ( );
          }

          bool helpSelected =
            ImGui::BeginTabItem     (ICON_FA_QUESTION_CIRCLE " Help",
                                                             nullptr, flags);

          if (helpSelected)
          {
            SKIF_ImGui_BeginTabChildFrame ();

            tab_selected = Help;

            ImGui::NewLine          ( );
            ImGui::Columns          (2, nullptr, true);

            SK_RunOnce (
              ImGui::SetColumnWidth (0, 600.0f * SKIF_ImGui_GlobalDPIScale)
            );

            ImGui::PushStyleColor   (
              ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f)
                                      );

            ImGui::TextColored      (
              ImColor::HSV (0.11F, 1.F, 1.F),
                                     "Beginner's Guide to Special K and SKIF:"
                                      );

            SKIF_ImGui_Spacing      ( );

            ImGui::TextWrapped      ("You are looking at the 'Special K Injection Frontend', commonly referred to as 'SKIF'.\n\n"
                                     "SKIF is used to start and stop Special K's 'Global Injection Service', "
                                     "which injects Special K's features into games as they start (and even games that are already running). "
                                     "The tool also provides convenient shortcuts to special locations, including config and log files, cloud saves, and external resources like PCGamingWiki and SteamDB.");

            ImGui::NewLine          ( );

            ImGui::TextWrapped      ("Global injection will inject Special K into most games, however Special K only activates itself in Steam-based software by default. "
                                     "To use Special K in a non-Steam application, consult the Whitelist / Blacklist section of '" ICON_FA_COG " Settings'.");

            ImGui::NewLine          ( );
            ImGui::NewLine          ( );

            ImGui::TextColored      (
              ImColor::HSV (0.11F, 1.F, 1.F),
                                     "Getting started with Steam games:");

            SKIF_ImGui_Spacing      ( );

            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "1 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Go to the '" ICON_FA_SYRINGE " Injection' tab.");

            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "2 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Select and launch the Steam game.");

            ImGui::NewLine          ( );
            ImGui::NewLine          ( );

            ImGui::TextColored      (
              ImColor::HSV (0.11F, 1.F, 1.F),
                                     "Getting started with other games:");

            SKIF_ImGui_Spacing      ( );

            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "1 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Start the 'Global Injection Service' using the '" ICON_FA_SYRINGE " Injection' tab, selecting 'Special K', and pressing 'Start Service'.");

            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "2 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Whitelist the game using the '" ICON_FA_COG " Settings' tab and typing a game's folder or executable in the '" ICON_FA_PLUS_CIRCLE " Whitelist Patterns' section.");

            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "3 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Launch the game as usual.");

            ImGui::NewLine          ( );
            ImGui::NewLine          ( );

            ImGui::TextColored (
              ImColor::HSV (0.11F, 1.F, 1.F),
                           "UI hints:");

            SKIF_ImGui_Spacing      ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                u8"• ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("This indicates a regular bullet point.");
            ImGui::EndGroup         ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "? ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("More info is available when hovering the mouse cursor over the item.");
            ImGui::EndGroup         ( );

            SKIF_ImGui_SetHoverTip  ("The info either further elaborates on the topic"
                                     "\nor provides relevant recommendations or tips.");

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "?!");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("In addition to having more info when hovering, the item can also be clicked to open a relevant link.");
            ImGui::EndGroup         ( );

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/");
            SKIF_ImGui_SetHoverTip  ( "Click this item to open the Special K wiki which"
                                      "\ncan contain even more relevant information." );
            if (ImGui::IsItemClicked ())
              SKIF_Util_OpenURI     (L"https://wiki.special-k.info/");

            float pushColumnSeparator =
              (900.0f * SKIF_ImGui_GlobalDPIScale) - ImGui::GetCursorPosY                () -
                                                    (ImGui::GetTextLineHeightWithSpacing () );

            ImGui::ItemSize (
              ImVec2 (0.0f, pushColumnSeparator)
            );

            ImGui::NextColumn       ( ); // Next Column
            ImGui::TextColored      (
              ImColor::HSV (0.11F, 1.F, 1.F),
                "About Special K:"    );

            SKIF_ImGui_Spacing      ( );

            ImGui::TextWrapped      ("Lovingly referred to as the Swiss Army Knife of PC gaming, Special K does a bit of everything.");
            ImGui::NewLine          ( );
            ImGui::TextWrapped      ("It is best known for fixing and enhancing graphics, its many detailed performance analysisand correction mods, "
                                     "and a constantly growing palette of tools that solve a wide variety of issues affecting PC games.");

            ImGui::PopStyleColor    ( );

            ImGui::NewLine          ( );
            ImGui::NewLine          ( );

            ImGui::TextColored (
              ImColor::HSV (0.11F, 1.F, 1.F),
                "How to inject Special K into games:"
            );

            SKIF_ImGui_Spacing      ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );

            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                ICON_FA_LINK " "      );
            ImGui::SameLine         ( );
            if (ImGui::Selectable   ("Global (system-wide)"))
              SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Global");
            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Global");
            ImGui::EndGroup         ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                ICON_FA_LINK " "      );
            ImGui::SameLine         ( );
            if (ImGui::Selectable   ("Local (game-specific)"))
              SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Local");
            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Local");
            ImGui::EndGroup         ( );

            ImGui::NewLine          ( );
            ImGui::NewLine          ( );

            ImGui::TextColored      (
              ImColor::HSV (0.11F, 1.F, 1.F),
                "Online resources:"   );
            SKIF_ImGui_Spacing      ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                ICON_FA_NODE_JS " "   );
            ImGui::SameLine         ( );

            if (ImGui::Selectable   ("Wiki"))
              SKIF_Util_OpenURI     (L"https://wiki.special-k.info/");

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/");
            ImGui::EndGroup         ( );


            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                ICON_FA_DISCORD " "   );
            ImGui::SameLine         ( );

            if (ImGui::Selectable   ("Discord"))
              SKIF_Util_OpenURI     (L"https://discord.com/invite/ER4EDBJPTa");

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://discord.com/invite/ER4EDBJPTa");
            ImGui::EndGroup         ( );


            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                ICON_FA_DISCOURSE " " );
            ImGui::SameLine         ( );

            if (ImGui::Selectable   ("Forum"))
              SKIF_Util_OpenURI     (L"https://discourse.differentk.fyi/");

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://discourse.differentk.fyi/");
            ImGui::EndGroup         ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                ICON_FA_PATREON " "   );
            ImGui::SameLine         ( );
            if (ImGui::Selectable   ("Patreon"))
              SKIF_Util_OpenURI     (L"https://www.patreon.com/Kaldaien");

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText ( "https://www.patreon.com/Kaldaien");
            ImGui::EndGroup         ( );

            ImGui::Columns          (1);

            ImGui::EndChildFrame    ( );
            ImGui::EndTabItem       ( );
          }

          bool debugSelected =       SKIF_bEnableDebugMode &&
            ImGui::BeginTabItem     (ICON_FA_BUG " Debug", nullptr, flags);

          if (debugSelected)
          {
            SKIF_ImGui_BeginTabChildFrame ();

            tab_selected = Debug;

            extern HRESULT
              SKIF_Debug_DrawUI (void);
              SKIF_Debug_DrawUI (    );

            ImGui::EndChildFrame    ( );
            ImGui::EndTabItem       ( );
          }

          auto title_len =
            ImGui::GetFont ()->CalcTextSizeA ( (tinyDPIFonts) ? 11.0F : 18.0F,
                                      FLT_MAX, 0.0f,
                         SKIF_WINDOW_TITLE_A ).x;

          /*
          OutputDebugString(L"title_len: ");
          OutputDebugString(std::to_wstring(title_len).c_str());
          OutputDebugString(L"\n");
          */

          float title_pos =                                     ImGui::GetCursorPos().x +
              (
                (ImGui::GetContentRegionAvail().x - 100.0f * SKIF_ImGui_GlobalDPIScale) - title_len
              )
                                                                                        / 2.0f;

          ImGui::SetCursorPosX (title_pos);

          /*
          OutputDebugString(L"title_pos: ");
          OutputDebugString(std::to_wstring(title_pos).c_str());
          OutputDebugString(L"\n");

          OutputDebugString(L"GetCursorPos: ");
          OutputDebugString(std::to_wstring(ImGui::GetCursorPos().x).c_str());
          OutputDebugString(L"\n");
          */

          ImGui::SetCursorPosY (
            7.0f * SKIF_ImGui_GlobalDPIScale
          );

          ImGui::TextColored (ImVec4 (.666f, .666f, .666f, 1.f), SKIF_WINDOW_TITLE_A_EX);

          if (_inject.bCurrentState && SKIF_bEnableDebugMode)
          {
            ImGui::SameLine(); // Required for subsequent GetCursorPosX() calls to get the right pos, as otherwise it resets to 0.0f

            static float direction = -1.0f;
            static float fMinPos   = 0.0f;
            /*
            static float fMinPos   =                ImGui::GetFont ()->CalcTextSizeA ( (tinyDPIFonts) ? 11.0F : 18.0F, FLT_MAX, 0.0f,
                                                                                             ICON_FA_GHOST ).x;
            */

            float fMaxPos   = SKIF_vecCurrentMode.x - ImGui::GetCursorPosX() - 125.0f * SKIF_ImGui_GlobalDPIScale; // Needs to be updated constantly due to mixed-DPI monitor configs
            /*
            static float fMaxPos   = (ImGui::GetContentRegionAvail ().x - ImGui::GetCursorPos ().x) / 6.0f -
                                                    ImGui::GetFont ()->CalcTextSizeA ( (tinyDPIFonts) ? 11.0F : 18.0F, FLT_MAX, 0.0f,
                                                                                             ICON_FA_GHOST ).x;
            */
            static float fNewPos   =
                       ( fMaxPos   -
                         fMinPos ) * 0.5f;

                     fNewPos +=
                         direction * SKIF_ImGui_GlobalDPIScale;

            if (     fNewPos < fMinPos)
            {        fNewPos = fMinPos - direction * 2.0f; direction = -direction; }
            else if (fNewPos > fMaxPos)
            {        fNewPos = fMaxPos - direction * 2.0f; direction = -direction; }

            ImGui::SameLine    ( 0.0f, fNewPos );

            auto current_time =
              SKIF_timeGetTime ();

            ImGui::SetCursorPosY (
              ImGui::GetCursorPosY () + 4.0f * sin ((current_time % 500) / 125.f)
                                 );

            ImVec4 vGhostColor =
              ImColor::HSV (   (float)(current_time % 1400)/ 2800.f,
                  (.5f + (sin ((float)(current_time % 750) /  250.f)) * .5f) / 2.f,
                     1.f );

            ImGui::TextColored (vGhostColor, ICON_FA_GHOST);
          }

          ImGui::EndTabBar          ( );
        } // End Large Mode

        ImGui::EndGroup             ( );

        // Status Bar at the bottom
        if ( ! SKIF_bSmallMode )
        {
          ImVec2 currPos =
            ImGui::GetCursorPos ();

          if ( ! SKIF_bDisableStatusBar )
          {
            ImGui::SetCursorPos (currPos);
            ImGui::Separator    (       );

            // End Separation

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

            ImGui::SetCursorPosX (
              ImGui::GetCursorPosX () +
              ImGui::GetWindowSize ().x -
                ( fStatusWidth +
                  fHelpWidth ) -
              ImGui::GetScrollX () -
              ImGui::GetStyle   ().ItemSpacing.x * 2
            );

            ImGui::SetCursorPosY ( ImGui::GetCursorPosY     () +
                                   ImGui::GetTextLineHeight () / 4.0f );

            ImGui::TextColored ( ImColor::HSV (0.0f, 0.0f, 0.75f),
                                   "%s", SKIF_StatusBarText.c_str ()
            );

            if (! SKIF_StatusBarHelp.empty ())
            {
              ImGui::SameLine ();
              ImGui::SetCursorPosX (
                ImGui::GetCursorPosX () -
                ImGui::GetStyle      ().ItemSpacing.x
              );
              ImGui::TextDisabled ("%s", SKIF_StatusBarHelp.c_str ());
            }

            // Clear the status every frame, it's mostly used for mouse hover tooltips.
            SKIF_StatusBarText.clear ();
            SKIF_StatusBarHelp.clear ();
          }
        }


        // Confirm Exit prompt
        ImGui::SetNextWindowSize (
          ImVec2 ( (SKIF_bAllowBackgroundService)
                      ? 515.0f * SKIF_ImGui_GlobalDPIScale
                      : 350.0f * SKIF_ImGui_GlobalDPIScale,
                     0.0f )
        );

        if (ImGui::BeginPopupModal ( "Confirm Exit", nullptr,
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_AlwaysAutoResize )
           )
        {
          SKIF_ImGui_Spacing ();

          if (SKIF_bAllowBackgroundService)
            ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F),
              "                           Exiting will leave the global injection"
              "\n                            service running in the background."
            );
          else
            ImGui::TextColored ( ImColor::HSV (0.11F, 1.F, 1.F),
                  "      Exiting will stop the global injection service."
            );

          SKIF_ImGui_Spacing ();

          if (SKIF_bAllowBackgroundService)
          {
            if (ImGui::Button ("Stop Service And Exit", ImVec2 (  0 * SKIF_ImGui_GlobalDPIScale,
                                                                 25 * SKIF_ImGui_GlobalDPIScale )))
            {
              _inject._StartStopInject (true);

              bKeepProcessAlive = false;
            }

            ImGui::SameLine ();
            ImGui::Spacing  ();
            ImGui::SameLine ();
          }

          if (ImGui::Button ("Exit", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                               25 * SKIF_ImGui_GlobalDPIScale )))
          {
            if (! SKIF_bAllowBackgroundService)
              _inject._StartStopInject (true);

            bKeepProcessAlive = false;
          }

          ImGui::SameLine ();
          ImGui::Spacing  ();
          ImGui::SameLine ();

          if (ImGui::Button ("Minimize", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                   25 * SKIF_ImGui_GlobalDPIScale )))
          {
            bKeepWindowAlive = true;

            ImGui::CloseCurrentPopup ();

            ShowWindow (hWnd, SW_MINIMIZE);
          }

          ImGui::SameLine ();
          ImGui::Spacing  ();
          ImGui::SameLine ();

          if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                 25 * SKIF_ImGui_GlobalDPIScale )))
          {
            bKeepWindowAlive = true;
            ImGui::CloseCurrentPopup ();
          }

          ImGui::EndPopup ();
        }

        // Uses a Directory Watch signal, so this is cheap; do it once every frame
        _inject.TestServletRunlevel ();

        if (_inject.bTaskbarOverlayIcon != _inject.bCurrentState)
          _inject._SetTaskbarOverlay(_inject.bCurrentState);

        monitor_extent =
          ImGui::GetWindowAllowedExtentRect (
            ImGui::GetCurrentWindowRead ()
          );
        windowPos      = ImGui::GetWindowPos ();

        // This allows us to ensure the window gets set within the workspace on the second frame after launch
        SK_RunOnce (
          changedMode = true
        );

        ImGui::End ();
      }

      // Rendering
      ImGui::Render ();

      g_pd3dDeviceContext->OMSetRenderTargets    (1, &g_mainRenderTargetView, nullptr);
      g_pd3dDeviceContext->ClearRenderTargetView (    g_mainRenderTargetView, (float*)&clear_color);

      ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

      // Update and Render additional Platform Windows
      if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
      {
        ImGui::UpdatePlatformWindows        ();
        ImGui::RenderPlatformWindowsDefault ();
      }

      UINT Interval =
        SKIF_bAllowTearing ? 0
                           : 1;
      UINT  Flags   =
        SKIF_bAllowTearing ? DXGI_PRESENT_ALLOW_TEARING
                           : 0x0;

      if (FAILED (g_pSwapChain->Present (Interval, Flags)))
        break;
    }

    _UpdateOcclusionStatus ();

    if ((! bKeepProcessAlive) && hWnd != 0)
      PostMessage(hWnd, WM_QUIT, 0x0, 0x0);

    else if (bOccluded || IsIconic (hWnd))
    {
      static CHandle event (
               CreateEvent (nullptr, false, false, nullptr)
        );

      static auto thread =
        _beginthreadex ( nullptr, 0x0, [](void *) -> unsigned
        {
          CRITICAL_SECTION            GamepadInputPump = { };
          InitializeCriticalSection (&GamepadInputPump);
          EnterCriticalSection      (&GamepadInputPump);

          while (IsWindow (SKIF_hWnd))
          {
            extern DWORD ImGui_ImplWin32_UpdateGamepads (void);
                         ImGui_ImplWin32_UpdateGamepads ();

            SetEvent           (event);
            PostMessage        (SKIF_hWnd, WM_NULL, 0x0, 0x0);
          //SendMessageTimeout (SKIF_hWnd, WM_NULL, 0x0, 0x0, 0x0, 0, nullptr);

            if (! SKIF_ImGui_IsFocused ())
            {
              SKIF_Util_CompactWorkingSet ();

              SleepConditionVariableCS (
                &SKIF_IsFocused, &GamepadInputPump,
                  INFINITE
              );
            }

            // XInput tends to have ~3-7 ms of latency between updates
            //   best-case, try to delay the next poll until there's
            //     new data.
            Sleep (5);
          }

          LeaveCriticalSection  (&GamepadInputPump);
          DeleteCriticalSection (&GamepadInputPump);

          _endthreadex (0x0);

          return 0;
        }, nullptr, 0x0, nullptr
      );

      while ( IsWindow (hWnd) &&
                WAIT_OBJECT_0 != MsgWaitForMultipleObjects ( 1, &event.m_h, FALSE,
                                                              INFINITE, QS_ALLINPUT ) )
      {
        if (! _TranslateAndDispatch ())
          break;

        else if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) break;
        else if (msg.message >= WM_KEYFIRST   && msg.message <= WM_KEYLAST)   break;
        else if (msg.message == WM_SETFOCUS   || msg.message <= WM_KILLFOCUS) break;
        else if (msg.message == WM_TIMER)                                     break;
      }
    }
  }

  ImGui_ImplDX11_Shutdown   (    );
  ImGui_ImplWin32_Shutdown  (    );

  CleanupDeviceD3D          (    );
  DestroyWindow             (hWnd);

  ImGui::DestroyContext     (    );

  SKIF_hWnd = 0;
       hWnd = 0;

  return 0;
}

using CreateDXGIFactory1_pfn            = HRESULT (WINAPI *)(REFIID riid, _COM_Outptr_ void **ppFactory);
using D3D11CreateDeviceAndSwapChain_pfn = HRESULT (WINAPI *)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                  CONST D3D_FEATURE_LEVEL*,                     UINT, UINT,
                                                  CONST DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                                                               ID3D11Device**,
                                                        D3D_FEATURE_LEVEL*,    ID3D11DeviceContext**);

CreateDXGIFactory1_pfn            SKIF_CreateDXGIFactory1;
D3D11CreateDeviceAndSwapChain_pfn SKIF_D3D11CreateDeviceAndSwapChain;

// Helper functions

bool CreateDeviceD3D (HWND hWnd)
{
  HMODULE hModD3D11 =
    LoadLibraryEx (L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

  HMODULE hModDXGI =
    LoadLibraryEx (L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

  SKIF_CreateDXGIFactory1 =
      (CreateDXGIFactory1_pfn)GetProcAddress (hModDXGI,
      "CreateDXGIFactory1");

  CComPtr <IDXGIFactory5>
               pFactory5;

  if ( SUCCEEDED (
    SKIF_CreateDXGIFactory1 (
       IID_IDXGIFactory5,
     (void **)&pFactory5.p ) ) )
               pFactory5->CheckFeatureSupport (
                          DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                        &SKIF_bAllowTearing,
                                sizeof ( SKIF_bAllowTearing )
                                              );

  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC
    sd                                  = { };
  sd.BufferCount                        =  3 ;
  sd.BufferDesc.Width                   =  4 ;
  sd.BufferDesc.Height                  =  4 ;
  sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator   = 0;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags                              =
    SKIF_IsWindows8Point1OrGreater () ?   DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                                      :   0x0;
  sd.Flags |=
    SKIF_bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                       : 0x0;

  sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT |
                                          DXGI_USAGE_BACK_BUFFER;
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

  SKIF_D3D11CreateDeviceAndSwapChain =
      (D3D11CreateDeviceAndSwapChain_pfn)GetProcAddress (hModD3D11,
      "D3D11CreateDeviceAndSwapChain");

  if ( SKIF_D3D11CreateDeviceAndSwapChain ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
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
    case WM_SKIF_REPOSITION:
      RepositionSKIF = true;            break;

    case WM_SKIF_MINIMIZE:
      ShowWindow (hWnd, SW_MINIMIZE);   break;

    case WM_SKIF_START:
      _inject._StartStopInject (false); break;

    case WM_SKIF_STOP:
      _inject._StartStopInject  (true); break;

    case WM_SKIF_CUSTOMLAUNCH:
      break; // receive, start injection, launch game

    case WM_TIMER:
      if (wParam == IDT_REFRESH)
        return 0;
      break;

    case WM_SIZE:
      if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
      {
        CleanupRenderTarget ();
        g_pSwapChain->ResizeBuffers (
          0, (UINT)LOWORD (lParam),
             (UINT)HIWORD (lParam),
            DXGI_FORMAT_UNKNOWN, SKIF_bAllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                                                    : 0x0 |
                  SKIF_IsWindows8Point1OrGreater () ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                                                    : 0x0
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
        //SKIF_ImGui_GlobalDPIScale = (float)HIWORD(wParam) / 96.0f * 100.0f;
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
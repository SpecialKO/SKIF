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

#include <strsafe.h>
#include <wtypes.h>
#include <dxgi1_5.h>

#include <gsl/gsl_util>

const GUID IID_IDXGIFactory5 =
  { 0x7632e1f5, 0xee65, 0x4dca, { 0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d } };

const int SKIF_STEAM_APPID = 1157970;
int WindowsCursorSize = 1;
bool RepositionSKIF   = false;
bool tinyDPIFonts     = false;
bool invalidateFonts  = false;
bool failedLoadFonts  = false;
bool failedLoadFontsPrompt = false;
DWORD invalidatedFonts = 0;
bool startedMinimized = false;

#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L

extern void SKIF_ProcessCommandLine (const char* szCmd);

int  SKIF_iNotifications           = 2;
bool SKIF_bDisableDPIScaling       = false,
     SKIF_bDisableExitConfirmation = true,
     SKIF_bDisableTooltips         = false,
     SKIF_bDisableStatusBar        = false,
     SKIF_bDisableSteamLibrary     = false,
     SKIF_bDisableGOGLibrary       = false,
     SKIF_bSmallMode               = false,
     SKIF_bFirstLaunch             = false,
     SKIF_bEnableDebugMode         = false,
     SKIF_bAllowMultipleInstances  = false,
     SKIF_bAllowBackgroundService  = false,
     SKIF_bEnableHDR               = false,
     SKIF_bOpenAtCursorPosition    = false,
     SKIF_bStopOnInjection         = false,
     SKIF_bAlwaysShowGhost         = false,
     SKIF_bCloseToTray             = false,
     SKIF_bFontChineseSimplified   = false,
     SKIF_bFontChineseAll          = false,
     SKIF_bFontCyrillic            = false,
     SKIF_bFontJapanese            = false,
     SKIF_bFontKorean              = false,
     SKIF_bFontThai                = false,
     SKIF_bFontVietnamese          = false;
BOOL SKIF_bAllowTearing            = FALSE,
     SKIF_bCanFlip                 = FALSE,
     SKIF_bCanFlipDiscard          = FALSE;

// GOG Galaxy stuff
std::wstring GOGGalaxy_Path        = L"";
std::wstring GOGGalaxy_Folder      = L"";
std::wstring GOGGalaxy_UserID      = L"";
bool GOGGalaxy_Installed           = false;

DWORD    RepopulateGamesWasSet     = 0;
bool     RepopulateGames           = false;
uint32_t SelectNewSKIFGame         = 0;

bool  HoverTipActive               = false;
DWORD HoverTipDuration             = 0;

// Notification icon stuff
#define SKIF_NOTIFY_ICON                    0x1330
#define SKIF_NOTIFY_EXIT                    0x1331
#define SKIF_NOTIFY_START                   0x1332
#define SKIF_NOTIFY_STOP                    0x1333
#define SKIF_NOTIFY_STARTWITHSTOP           0x1334
#define WM_SKIF_NOTIFY_ICON      (WM_USER + 0x150)
bool SKIF_isTrayed = false;
NOTIFYICONDATA niData;
HMENU hMenu;

// Cmd line argument stuff
struct SKIF_Signals {
  BOOL Stop          = FALSE;
  BOOL Start         = FALSE;
  BOOL Temporary     = FALSE;
  BOOL Quit          = FALSE;
  BOOL Minimize      = FALSE;
  BOOL Restore       =  TRUE;
  BOOL QuickLaunch   = FALSE;
  BOOL AddSKIFGame   = FALSE;

  BOOL _Disowned     = FALSE;
} _Signal;

#include <sk_utility/command.h>

extern        SK_ICommandProcessor*
  __stdcall SK_GetCommandProcessor (void);

#include <SKIF.h>

#include <stores/Steam/library.h>

#include "../version.h"
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

#include <filesystem>
#include <regex>

#pragma comment (lib, "wininet.lib")

HMODULE hModSKIF     = nullptr;
HMODULE hModSpecialK = nullptr;

std::filesystem::path orgWorkingDirectory;

using  GetSystemMetricsForDpi_pfn = int (WINAPI *)(int, UINT);
static GetSystemMetricsForDpi_pfn
       GetSystemMetricsForDpi = nullptr;

#define SK_BORDERLESS ( WS_VISIBLE | WS_POPUP | WS_MINIMIZEBOX | \
                        WS_SYSMENU )

#define SK_BORDERLESS_EX      ( WS_EX_APPWINDOW | WS_EX_NOACTIVATE )
//#define SK_BORDERLESS_WIN8_EX ( SK_BORDERLESS_EX | WS_EX_NOREDIRECTIONBITMAP ) // We don't support Win8.0 or older

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
      static_cast <DWORD> ( li.QuadPart /
                      (qpcFreq.QuadPart / 1000ULL) );
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

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

BOOL
SKIF_IsWindowsVersionOrGreater (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber)
{
  NTSTATUS(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEXW);

  OSVERSIONINFOEXW
    osInfo                     = { };
    osInfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEXW);

  *reinterpret_cast<FARPROC *>(&RtlGetVersion) =
    GetProcAddress (GetModuleHandleW (L"ntdll"), "RtlGetVersion");

  if (RtlGetVersion != nullptr)
  {
    if (NT_SUCCESS (RtlGetVersion (&osInfo)))
    {
      return
        ( osInfo.dwMajorVersion   >  dwMajorVersion ||
          ( osInfo.dwMajorVersion == dwMajorVersion &&
            osInfo.dwMinorVersion >= dwMinorVersion &&
            osInfo.dwBuildNumber  >= dwBuildNumber  )
        );
    }
  }

  return FALSE;
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
LRESULT WINAPI
     SKIF_Notify_WndProc       (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

bool    bExitOnInjection = false; // Used to exit SKIF on a successful injection if it's used merely as a launcher
CHandle hInjectAck (0);           // Signalled when a game finishes injection
CHandle hSwapWait  (0);           // Signalled by a waitable swapchain

int __width  = 0;
int __height = 0;

// Holds current global DPI scaling, 1.0f == 100%, 1.5f == 150%.
// Can go below 1.0f if SKIF is shown on a smaller screen with less than 1000px in height.
float SKIF_ImGui_GlobalDPIScale      = 1.0f;
// Holds last frame's DPI scaling
float SKIF_ImGui_GlobalDPIScale_Last = 1.0f;

std::string SKIF_StatusBarText = "";
std::string SKIF_StatusBarHelp = "";
HWND        SKIF_hWnd          =  0;
HWND        SKIF_Notify_hWnd   =  0;

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
        /* Disabled as it's not needed any longer since the mouse pos never changes
             when interfacing with the app any longer. Disabling this also restored
               original tooltips constrained within the monitor area.
        ImVec2 cursorPos   = (ImGui::IsItemFocused ( ))
                                ? ImGui::GetCursorScreenPos ( ) // If the item has keyboard focus, grab the position of the rendering cursor
                                : ImGui::GetIO ( ).MousePos;    // If the item lacks keyboard focus, grab the position of the mouse cursor
        int    cursorScale = WindowsCursorSize;

        ImGui::SetNextWindowPos (
          ImVec2 ( cursorPos.x + 16      + 4 * (cursorScale - 1),
                   cursorPos.y + 8 ) // 16 + 4 * (cursorScale - 1) )
        );
        */
        HoverTipActive = true;

        if ( HoverTipDuration == 0)
          HoverTipDuration = SKIF_timeGetTime ( );

        else if ( HoverTipDuration + 500 < SKIF_timeGetTime() )
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

// Difference from regular
void
SKIF_ImGui_Columns(int columns_count, const char* id, bool border, bool resizeble = false)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  IM_ASSERT(columns_count >= 1);

  ImGuiColumnsFlags flags = (border ? 0 : ImGuiColumnsFlags_NoBorder);
  if (! resizeble)
    flags |= ImGuiColumnsFlags_NoResize;
  //flags |= ImGuiColumnsFlags_NoPreserveWidths; // NB: Legacy behavior
  ImGuiColumns* columns = window->DC.CurrentColumns;
  if (columns != NULL && columns->Count == columns_count && columns->Flags == flags)
    return;

  if (columns != NULL)
    ImGui::EndColumns();

  if (columns_count != 1)
    ImGui::BeginColumns(id, columns_count, flags);
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
SK_ImGui_GetGlyphRangesKorean (void)
{
  static const ImWchar ranges[] =
  {
      0x0020, 0x00FF, // Basic Latin + Latin Supplement
      0x3131, 0x3163, // Korean alphabets
//#ifdef _WIN64
      0xAC00, 0xD7A3, // Korean characters (Hangul syllables) -- should not be included on 32-bit OSes due to system limitations
//#endif
      0,
  };
  return &ranges[0];
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

  // Core character set
  SKIF_ImGui_LoadFont     (L"Tahoma.ttf",   fontSize,  SK_ImGui_GetGlyphRangesDefaultEx               ()           );

  // Load extended character sets when SKIF is not used as a launcher
  if (! _Signal.QuickLaunch)
  {
    // Cyrillic character set
    if (SKIF_bFontCyrillic)
      SKIF_ImGui_LoadFont   (L"Tahoma.ttf",   fontSize, io.Fonts->GetGlyphRangesCyrillic                (), &font_cfg);
  
    // Japanese character set
    // Load before Chinese to not overwrite the Japanese font
    if (SKIF_bFontJapanese)
    {
      if (SKIF_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"Yugothic.ttf", fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
    }

    // Simplified Chinese character set
    // Also includes almost alll of the Japanese characters except for some Kanjis
    if (SKIF_bFontChineseSimplified)
      SKIF_ImGui_LoadFont   (L"msyh.ttc",     fontSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon (), &font_cfg);
    
    // All Chinese character sets
    if (SKIF_bFontChineseAll)
      SKIF_ImGui_LoadFont   (L"msjh.ttc",     fontSize, io.Fonts->GetGlyphRangesChineseFull             (), &font_cfg);

    // Korean character set
    // On 32-bit builds this does not include Hangul syllables due to system limitaitons
    if (SKIF_bFontKorean)
      SKIF_ImGui_LoadFont   (L"malgun.ttf",   fontSize, SK_ImGui_GetGlyphRangesKorean                   (), &font_cfg);

    // Thai character set
    if (SKIF_bFontThai)
      SKIF_ImGui_LoadFont   (L"Tahoma.ttf",   fontSize, io.Fonts->GetGlyphRangesThai                    (), &font_cfg);

    // Vietnamese character set
    if (SKIF_bFontVietnamese)
      SKIF_ImGui_LoadFont   (L"Tahoma.ttf",   fontSize, io.Fonts->GetGlyphRangesVietnamese              (), &font_cfg);
  }
  
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
      bool hasData (void)
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
               _GetValue (&out, &dwOutLen) ) return false;

        return true;
      };

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
#ifdef _WIN64
    _fseeki64 (fPatrons, 0, SEEK_END);
#else
    fseek     (fPatrons, 0, SEEK_END);
#endif

    size_t size =
      gsl::narrow_cast <size_t> (
#ifdef _WIN64
      _ftelli64 (fPatrons)      );
#else
      ftell     (fPatrons)      );
#endif
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

constexpr UINT WM_SKIF_START         = WM_USER + 0x1024;
constexpr UINT WM_SKIF_TEMPSTART     = WM_USER + 0x1025;
constexpr UINT WM_SKIF_QUICKLAUNCH   = WM_USER + 0x1026;
constexpr UINT WM_SKIF_REFRESHGAMES  = WM_USER + 0x1027;
constexpr UINT WM_SKIF_STOP          = WM_USER + 0x2048;
constexpr UINT WM_SKIF_RESTORE       = WM_USER +  0x513;
constexpr UINT WM_SKIF_MINIMIZE      = WM_USER +  0x512;

const wchar_t* SKIF_WindowClass =
             L"SK_Injection_Frontend";

#include <sstream>
#include <cwctype>
#include <stores/Steam/app_record.h>

void
SKIF_ProxyCommandAndExitIfRunning (LPWSTR lpCmdLine)
{
  HWND hwndAlreadyExists =
    FindWindowExW (0, 0, SKIF_WindowClass, nullptr);

  _Signal.Start =
    StrStrIW (lpCmdLine, L"Start")    != NULL;

  _Signal.Temporary =
    StrStrIW (lpCmdLine, L"Temp")     != NULL;

  _Signal.Stop =
    StrStrIW (lpCmdLine, L"Stop")     != NULL;

  _Signal.Quit =
    StrStrIW (lpCmdLine, L"Quit")     != NULL;

  _Signal.Minimize =
    StrStrIW (lpCmdLine, L"Minimize") != NULL;

  _Signal.AddSKIFGame =
    StrStrIW (lpCmdLine, L"AddGame=") != NULL;

  _Signal.QuickLaunch =
    StrStrIW (lpCmdLine, L".exe")     != NULL;

  if (  hwndAlreadyExists != 0 && (
              (! SKIF_bAllowMultipleInstances)  ||
                                   _Signal.Stop || _Signal.Start ||
                                   _Signal.Quit || _Signal.Minimize
                                  ) &&
        _Signal.QuickLaunch == false &&
        _Signal.AddSKIFGame == false
     )
  {
    if (! _Signal.Start     &&
        ! _Signal.Temporary &&
        ! _Signal.Stop      &&
        ! _Signal.Quit      &&
        ! _Signal.Minimize  &&
        ! _Signal.QuickLaunch)
    {
      //if (IsIconic        (hwndAlreadyExists))
      //  ShowWindow        (hwndAlreadyExists, SW_SHOWNA);
      
      PostMessage (hwndAlreadyExists, WM_SKIF_RESTORE, 0x0, 0x0);
      //SetForegroundWindow (hwndAlreadyExists);
    }

    if (_Signal.Stop)
      PostMessage (hwndAlreadyExists, WM_SKIF_STOP, 0x0, 0x0);

    if (_Signal.Quit)
      PostMessage (hwndAlreadyExists, WM_CLOSE, 0x0, 0x0);

    if (_Signal.Start)
      PostMessage (hwndAlreadyExists, (_Signal.Temporary) ? WM_SKIF_TEMPSTART : WM_SKIF_START, 0x0, 0x0);
    
    if (_Signal.Minimize)
      PostMessage (hwndAlreadyExists, WM_SKIF_MINIMIZE, 0x0, 0x0);

    else {
      if (IsIconic        (hWndOrigForeground))
        ShowWindow        (hWndOrigForeground, SW_SHOWNA);
      SetForegroundWindow (hWndOrigForeground);
    }

    if (_Signal.Quit || (! _Signal._Disowned))
       ExitProcess (0x0);
  }

  else if (_Signal.Quit)
  {
    ExitProcess (0x0);
  }

  // Handle adding custom game
  if (_Signal.AddSKIFGame)
  {
    // O:\WindowsApps\DevolverDigital.MyFriendPedroWin10_1.0.6.0_x64__6kzv4j18v0c96\MyFriendPedro.exe

    char charName     [MAX_PATH],
         charPath     [MAX_PATH],
         charArgs     [MAX_PATH];

    std::wstring cmdLine        = std::wstring(lpCmdLine);
    std::wstring cmdLineArgs    = cmdLine;

    // Transform to lowercase
    std::wstring cmdLineLower   = cmdLine;
    std::transform(cmdLineLower.begin(), cmdLineLower.end(), cmdLineLower.begin(), std::towlower);

    std::wstring splitPos1Lower = L"addgame="; // Start split
    std::wstring splitEXELower  = L".exe";     // Stop split (exe)
    std::wstring splitLNKLower  = L".lnk";     // Stop split (lnk)

    // Exclude anything before "addgame=", if any
    cmdLine = cmdLine.substr(cmdLineLower.find(splitPos1Lower) + splitPos1Lower.length());

    // First position is a space -- skip that one
    if (cmdLine.find(L" ") == 0)
      cmdLine = cmdLine.substr(1);

    // First position is a quotation mark -- we need to strip those
    if (cmdLine.find(L"\"") == 0)
      cmdLine = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1) + cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos);

    // Update lowercase
    cmdLineLower   = cmdLine;
    std::transform(cmdLineLower.begin(), cmdLineLower.end(), cmdLineLower.begin(), std::towlower);

    // If .exe is part of the string
    if (cmdLineLower.find(splitEXELower) != std::wstring::npos)
    {
      // Extract proxied arguments, if any
      cmdLineArgs = cmdLine.substr(cmdLineLower.find(splitEXELower) + splitEXELower.length());

      // Exclude anything past ".exe"
      cmdLine = cmdLine.substr(0, cmdLineLower.find(splitEXELower) + splitEXELower.length());
    }

    // If .lnk is part of the string
    else if (cmdLineLower.find(splitLNKLower) != std::wstring::npos)
    {
      // Exclude anything past ".lnk" since we're reading the arguments from the shortcut itself
      cmdLine = cmdLine.substr(0, cmdLineLower.find(splitLNKLower) + splitLNKLower.length());
      
      WCHAR szTarget   [MAX_PATH];
      WCHAR szArguments[MAX_PATH];

      ResolveIt (SKIF_hWnd, SK_WideCharToUTF8(cmdLine).c_str(), szTarget, szArguments, MAX_PATH);

      cmdLine     = std::wstring(szTarget);
      cmdLineArgs = std::wstring(szArguments);
    }

    // Clear var if no valid path was found
    else {
      cmdLine.clear();
    }

    // Only proceed if we have an actual valid path
    if (cmdLine.length() > 0)
    {
      // First position of the arguments is a space -- skip that one
      if (cmdLineArgs.find(L" ") == 0)
        cmdLineArgs = cmdLineArgs.substr(1);

      /*
      OutputDebugString(L"Extracted path: ");
      OutputDebugString(cmdLine.c_str());
      OutputDebugString(L"\n");

      OutputDebugString(L"Extracted args: ");
      OutputDebugString(cmdLineArgs.c_str());
      OutputDebugString(L"\n");
      */

      extern std::wstring SKIF_GetProductName    (const wchar_t* wszName);
      extern int          SKIF_AddCustomAppID    (std::vector<std::pair<std::string, app_record_s>>* apps,
                                                  std::wstring name, std::wstring path, std::wstring args);
      extern
        std::vector <
          std::pair < std::string, app_record_s >
                    > apps;

      std::filesystem::path p = cmdLine;
      std::wstring productName = SKIF_GetProductName (p.c_str());

      strncpy (charPath, p.u8string().c_str(),                                   MAX_PATH);
      strncpy (charName, (productName != L"")
                          ? SK_WideCharToUTF8 (productName).c_str()
                          : p.replace_extension().filename().u8string().c_str(), MAX_PATH);
      strncpy (charArgs, SK_WideCharToUTF8(cmdLineArgs).c_str(),                 MAX_PATH);

      SelectNewSKIFGame = (uint32_t)SKIF_AddCustomAppID (&apps, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs));
    
      // If a running instance of SKIF already exists, terminate this one as it has served its purpose
      if (SelectNewSKIFGame > 0 && hwndAlreadyExists != 0)
      {
        SendMessage (hwndAlreadyExists, WM_SKIF_REFRESHGAMES, SelectNewSKIFGame, 0x0);
        ExitProcess (0x0);
      }
    }

    // Terminate the process if given a non-valid string
    else {
      ExitProcess(0x0);
    }
  }
  
  // Handle quick launching
  else if (_Signal.QuickLaunch)
  {
    // Display in small mode
    SKIF_bSmallMode = true;

    std::wstring cmdLine        = std::wstring(lpCmdLine);
    std::wstring delimiter      = L".exe"; // split lpCmdLine at the .exe

    // First position is a quotation mark -- we need to strip those
    if (cmdLine.find(L"\"") == 0)
      cmdLine = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1) + cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos);

    // Transform to lowercase
    std::wstring cmdLineLower = cmdLine;
    std::transform(cmdLineLower.begin(), cmdLineLower.end(), cmdLineLower.begin(), std::towlower);

    // Extract the target path and any proxied command line arguments
    std::wstring path           = cmdLine.substr(0, cmdLineLower.find(delimiter) + delimiter.length());                        // path
    std::wstring proxiedCmdLine = cmdLine.substr(   cmdLineLower.find(delimiter) + delimiter.length(), cmdLineLower.length()); // proxied command line

    // Path does not seem to be absolute -- add the current working directory in front of the path
    if (path.find(L"\\") == std::wstring::npos)
      path = orgWorkingDirectory.wstring() + L"\\" + path;

    std::string  parentFolder     = std::filesystem::path(path).parent_path().filename().string();                   // name of parent folder
    std::wstring workingDirectory = std::filesystem::path(path).parent_path().wstring();                             // path to the parent folder

    bool isLocalBlacklisted  = false,
         isGlobalBlacklisted = false;

    if (PathFileExistsW (path.c_str()))
    {
      std::wstring blacklistFile = SK_FormatStringW (L"%s\\SpecialK.deny.%ws",
                                                     std::filesystem::path(path).parent_path().wstring().c_str(),                 // full path to parent folder
                                                     std::filesystem::path(path).filename().replace_extension().wstring().c_str() // filename without extension
      );

      // Check if the executable is blacklisted
      isLocalBlacklisted  = PathFileExistsW(blacklistFile.c_str());
      isGlobalBlacklisted = _inject._TestUserList(SK_WideCharToUTF8(path).c_str(), false);

      if (! isLocalBlacklisted &&
          ! isGlobalBlacklisted)
      {
        // Whitelist the path if it haven't been already
        _inject._WhitelistBasedOnPath (SK_WideCharToUTF8(path));

        if (hwndAlreadyExists != 0)
          SendMessage (hwndAlreadyExists, WM_SKIF_QUICKLAUNCH, 0x0, 0x0);

        else if (! _inject.bCurrentState)
        {
          bExitOnInjection = true;
          _inject._StartStopInject (false, true);
        }
      }

      SHELLEXECUTEINFOW
        sexi              = { };
        sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
        sexi.lpVerb       = L"OPEN";
        sexi.lpFile       = path.c_str();
        sexi.lpParameters = proxiedCmdLine.c_str();
        sexi.lpDirectory  = workingDirectory.c_str();
        sexi.nShow        = SW_SHOW;
        sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                            SEE_MASK_NOASYNC    | SEE_MASK_NOZONECHECKS;

      // Launch executable
      ShellExecuteExW(&sexi);
    }

    // If a running instance of SKIF already exists, or the game was blacklisted, terminate this one as it has served its purpose
    if (hwndAlreadyExists != 0 || isLocalBlacklisted || isGlobalBlacklisted)
      ExitProcess(0x0);
  }
}

bool
SKIF_RegisterApp (void)
{
  HKEY hKey;
  //BYTE buffer[4000];
  //DWORD buffsz = sizeof(buffer);
  static int ret = -1;

  if (ret != -1)
    return ret;

  if (RegCreateKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\SKIF.exe)", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
  {
    TCHAR               szExePath[MAX_PATH];
    GetModuleFileName   (NULL, szExePath, _countof(szExePath));

    std::wstring wsPath = std::wstring(szExePath);

    if (ERROR_SUCCESS == RegSetValueExW(hKey, NULL, 0, REG_SZ, (LPBYTE)wsPath.data(),
                                                                (DWORD)wsPath.size() * sizeof(wchar_t)))
      ret = 1;
    else
      ret = 0;

    RegCloseKey (hKey);
  }

  /*
  //HKEY_CURRENT_USER\Environment
  if (RegOpenKeyEx    (HKEY_CURRENT_USER, L"Environment", 0, KEY_ALL_ACCESS, std::addressof(key)) == 0 &&
      RegQueryValueEx (key, L"Path", nullptr, nullptr, buffer, std::addressof(buffsz)) == 0)
  {
    std::wstring env         = reinterpret_cast<const wchar_t*>(buffer),
                 currentPath = std::filesystem::current_path().wstring();

    if (env         .find (currentPath)             == std::wstring::npos &&
        currentPath .find (LR"(\My Mods\SpecialK)") != std::wstring::npos)
    {
      std::wstring new_env = env;

      if (env.rfind(L";") == env.length() - 1)
        new_env += currentPath + L";";
      else
        new_env += L";" + currentPath + L";";

      if (RegSetValueEx(key, L"Path", 0, REG_SZ, (LPBYTE)(new_env.c_str()), (DWORD)((new_env.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS)
        ret = 1;
      else
        ret = 0;
    }

    else if (env    .find (currentPath)             != std::wstring::npos)
      ret = 1;

    else
      ret = 0;

    RegCloseKey (key);
  }
  */

  return ret;
}

bool
SKIF_hasControlledFolderAccess (void)
{
  if (! SKIF_IsWindows10OrGreater ( ))
    return false;

  HKEY hKey;
  DWORD buffer;
  unsigned long size = 1024;
  bool enabled = false;

  // Check if Controlled Folder Access is enabled
  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Defender\Windows Defender Exploit Guard\Controlled Folder Access\)", 0, KEY_READ, &hKey))
  {
    if (ERROR_SUCCESS == RegQueryValueEx (hKey, L"EnableControlledFolderAccess", NULL, NULL, (LPBYTE)&buffer, &size))
      enabled = buffer;

    RegCloseKey (hKey);
  }

  if (enabled)
  {
    if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Defender\Windows Defender Exploit Guard\Controlled Folder Access\AllowedApplications\)", 0, KEY_READ, &hKey))
    {
      static TCHAR               szExePath[MAX_PATH];
      GetModuleFileName   (NULL, szExePath, _countof(szExePath));

      if (ERROR_SUCCESS == RegQueryValueEx (hKey, szExePath, NULL, NULL, NULL, NULL))
        enabled = false;

      RegCloseKey(hKey);
    }
  }

  return enabled;
}

static auto regKVDisableStopOnInjection =
  SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                        LR"(Disable Stop On Injection)" );

void SKIF_putStopOnInjection (bool in)
{
  regKVDisableStopOnInjection.putData(!in);

  if (_inject.bCurrentState)
    _inject._ToggleOnDemand (in);
}

#include <Tlhelp32.h>

bool
SK_IsProcessAdmin (PROCESSENTRY32W proc)
{
  bool          bRet = false;
  SK_AutoHandle hToken (INVALID_HANDLE_VALUE);

  SetLastError(NO_ERROR);

  SK_AutoHandle hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, proc.th32ProcessID);

  if (GetLastError() == ERROR_ACCESS_DENIED)
    return true;

  if ( OpenProcessToken ( hProcess,
                            TOKEN_QUERY,
                              &hToken.m_h )
     )
  {
    TOKEN_ELEVATION Elevation = { };

    DWORD cbSize =
      sizeof (TOKEN_ELEVATION);

    if ( GetTokenInformation ( hToken.m_h,
                                 TokenElevation,
                                   &Elevation,
                                     sizeof (Elevation),
                                       &cbSize )
       )
    {
      bRet =
        ( Elevation.TokenIsElevated != 0 );
    }
  }

  return bRet;
}

PROCESSENTRY32W
SK_FindProcessByName (const wchar_t* wszName)
{
  PROCESSENTRY32W none = { },
                  pe32 = { };

  SK_AutoHandle hProcessSnap (
    CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
  );

  if ((intptr_t)hProcessSnap.m_h <= 0) // == INVALID_HANDLE_VALUE)
    return none;

  pe32.dwSize = sizeof (PROCESSENTRY32W);

  if (! Process32FirstW (hProcessSnap, &pe32))
    return none;

  do
  {
    if (wcsstr (pe32.szExeFile, wszName))
      return pe32;
  } while (Process32NextW (hProcessSnap, &pe32));

  return none;
}

//
// https://docs.microsoft.com/en-au/windows/win32/shell/links?redirectedfrom=MSDN#resolving-a-shortcut
//
// ResolveIt - Uses the Shell's IShellLink and IPersistFile interfaces
//             to retrieve the path and description from an existing shortcut.
//
// Returns the result of calling the member functions of the interfaces.
//
// Parameters:
// hwnd         - A handle to the parent window. The Shell uses this window to
//                display a dialog box if it needs to prompt the user for more
//                information while resolving the link.
// lpszLinkFile - Address of a buffer that contains the path of the link,
//                including the file name.
// lpszPath     - Address of a buffer that receives the path of the link target,
//                including the file name.
// lpszDesc     - Address of a buffer that receives the description of the
//                Shell link, stored in the Comment field of the link
//                properties.

void
ResolveIt(HWND hwnd, LPCSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize)
{
  IShellLink* psl;

  ///WCHAR szGotPath[MAX_PATH];
  WCHAR szArguments[MAX_PATH];
  WCHAR szTarget  [MAX_PATH];
  //WIN32_FIND_DATA wfd;

  *lpszTarget    = 0; // Assume failure
  *lpszArguments = 0; // Assume failure

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface.
  if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
  {
    IPersistFile* ppf;

    // Get a pointer to the IPersistFile interface.
    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)))
    {
      WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode.
      MultiByteToWideChar(CP_ACP, 0, lpszLinkFile, -1, wsz, MAX_PATH);

      // Add code here to check return value from MultiByteWideChar
      // for success.

      // Load the shortcut.
      if (SUCCEEDED(ppf->Load(wsz, STGM_READ)))
      {
        // Disables the UI and hopefully sets a timeout duration of 10ms,
        //   since we don't actually care all that much about resolving the target.
        DWORD flags = MAKELONG(SLR_NO_UI, 10);

        // Resolve the link.
        if (SUCCEEDED(psl->Resolve(hwnd, flags)))
        {
          // Get the link target.
          if (SUCCEEDED(psl->GetPath(szTarget, MAX_PATH, NULL, SLGP_RAWPATH)))
            StringCbCopy(lpszTarget, iPathBufferSize, szTarget);

          // Get the arguments of the target.
          if (SUCCEEDED(psl->GetArguments(szArguments, MAX_PATH)))
            StringCbCopy(lpszArguments, iPathBufferSize, szArguments);
        }
      }

      // Release the pointer to the IPersistFile interface.
      ppf->Release();
    }

    // Release the pointer to the IShellLink interface.
    psl->Release();
  }
}

//
// https://docs.microsoft.com/en-au/windows/win32/shell/links?redirectedfrom=MSDN#creating-a-shortcut-and-a-folder-shortcut-to-a-file
// 
// CreateLink - Uses the Shell's IShellLink and IPersistFile interfaces 
//              to create and store a shortcut to the specified object. 
//
// Returns the result of calling the member functions of the interfaces. 
//
// Parameters:
// lpszPathObj  - Address of a buffer that contains the path of the object,
//                including the file name.
// lpszPathLink - Address of a buffer that contains the path where the 
//                Shell link is to be stored, including the file name.
// lpszDesc     - Address of a buffer that contains a description of the 
//                Shell link, stored in the Comment field of the link
//                properties.

bool
CreateLink (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs, LPCWSTR lpszWorkDir, LPCWSTR lpszDesc, LPCWSTR lpszIconLocation, int iIcon)
{
  bool ret = false;
  IShellLink* psl;

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
  // has already been called.

  if (SUCCEEDED (CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl)))
  {
    IPersistFile* ppf;

    // Set the specifics of the shortcut. 
    psl->SetPath               (lpszTarget);

    if (wcscmp(lpszWorkDir, L"\0") == 0) // lpszWorkDir == L"\0"
      psl->SetWorkingDirectory (std::filesystem::path(lpszTarget).parent_path().c_str());
    else
      psl->SetWorkingDirectory (lpszWorkDir);

    if (wcscmp(lpszArgs, L"\0") != 0) // lpszArgs != L"\0"
      psl->SetArguments          (lpszArgs);

    if (wcscmp(lpszDesc, L"\0") != 0) // lpszDesc != L"\0"
      psl->SetDescription      (lpszDesc);

    if (wcscmp(lpszIconLocation, L"\0") != 0) // (lpszIconLocation != L"\0")
      psl->SetIconLocation     (lpszIconLocation, iIcon);

    // Query IShellLink for the IPersistFile interface, used for saving the 
    // shortcut in persistent storage. 
    //hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

    if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)))
    {

      //WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode. 
      //MultiByteToWideChar (CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);

      // Save the link by calling IPersistFile::Save. 
      if (SUCCEEDED (ppf->Save(lpszPathLink, FALSE)))
        ret = true;

      ppf->Release();
    }
    psl->Release();
  }

  return ret;
}

void SKIF_CreateUpdateNotifyMenu (void)
{
  if (hMenu != NULL)
    DestroyMenu (hMenu);

  bool svcRunning         = false,
       svcRunningAutoStop = false,
       svcStopped         = false;

  if (_inject.bCurrentState      && hInjectAck.m_h <= 0)
    svcRunning         = true;
  else if (_inject.bCurrentState && hInjectAck.m_h != 0)
    svcRunningAutoStop = true;
  else
    svcStopped         = true;

  hMenu = CreatePopupMenu ( );
  AppendMenu (hMenu, MF_STRING | ((svcRunning)         ? MF_CHECKED | MF_GRAYED : (svcRunningAutoStop) ? MF_GRAYED : 0x0), SKIF_NOTIFY_START,         L"Start Injection");
  AppendMenu (hMenu, MF_STRING | ((svcRunningAutoStop) ? MF_CHECKED | MF_GRAYED : (svcRunning)         ? MF_GRAYED : 0x0), SKIF_NOTIFY_STARTWITHSTOP, L"Start Injection (with auto stop)");
  AppendMenu (hMenu, MF_STRING | ((svcStopped)         ? MF_CHECKED | MF_GRAYED :                                    0x0), SKIF_NOTIFY_STOP,          L"Stop Injection");
  AppendMenu (hMenu, MF_SEPARATOR, 0, NULL);
  AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_EXIT,          L"Exit");
}

void SKIF_CreateNotifyIcon (void)
{
  ZeroMemory (&niData,  sizeof (NOTIFYICONDATA));
  niData.cbSize       = sizeof (NOTIFYICONDATA); // 6.0.6 or higher (Windows Vista and later)	
  niData.uID          = SKIF_NOTIFY_ICON;
  niData.uFlags       = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  niData.hIcon        = LoadIcon (hModSKIF, MAKEINTRESOURCE(IDI_SKIF));
  niData.hWnd         = SKIF_Notify_hWnd;
  niData.uVersion     = NOTIFYICON_VERSION_4;
  wcsncpy_s (niData.szTip,      128, L"Special K Injection Frontend",   128);

  niData.uCallbackMessage = WM_SKIF_NOTIFY_ICON;

  Shell_NotifyIcon (NIM_ADD, &niData);
  //Shell_NotifyIcon (NIM_SETVERSION, &niData); // Breaks shit, lol
}

void SKIF_UpdateNotifyIcon (void)
{
  niData.uFlags        = NIF_ICON;
  if (_inject.bCurrentState)
    niData.hIcon       = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIFONNOTIFY));
  else
    niData.hIcon       = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));

  Shell_NotifyIcon (NIM_MODIFY, &niData);
}

void SKIF_CreateNotifyToast (std::wstring message, std::wstring title = L"")
{
  if (SKIF_iNotifications == 1 || // Always
      SKIF_iNotifications == 2 && ! SKIF_ImGui_IsFocused() // When Unfocused
    )
  {
    niData.uFlags       = NIF_INFO;
    niData.dwInfoFlags  = NIIF_NONE | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(niData.szInfoTitle, 64, title.c_str(), 64);
    wcsncpy_s(niData.szInfo, 256, message.c_str(), 256);

    Shell_NotifyIcon (NIM_MODIFY, &niData);
  }
}

std::wstring SKIF_GetLastError (void)
{
  LPWSTR messageBuffer = nullptr;

  size_t size = FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, GetLastError ( ), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);

  std::wstring message (messageBuffer, size);
  LocalFree (messageBuffer);

  return message;
}

void SKIF_Initialize (void)
{
  static bool isInitalized = false;

  if (! isInitalized)
  {
    CoInitializeEx (nullptr, 0x0);

    hModSKIF =
      GetModuleHandleW (nullptr);

    wchar_t             wszPath
     [MAX_PATH + 2] = { };
    GetCurrentDirectoryW (
      MAX_PATH,         wszPath);
    SK_Generate8Dot3   (wszPath);
    GetModuleFileNameW (hModSKIF,
                        wszPath,
      MAX_PATH                 );
    SK_Generate8Dot3   (wszPath);

    // Launching SKIF through the Win10 start menu can at times default the working directory to system32.
    // Store the original working directory in a variable, since it's used by custom launch, for example.
    orgWorkingDirectory = std::filesystem::current_path();

    // Let's change the current working directory to the folder of the executable itself.
    std::filesystem::current_path (
      std::filesystem::path (wszPath).remove_filename ()
    );

    CreateDirectoryW (L"Servlet", nullptr); // Attempt to create the Servlet folder if it does not exist
    
    if (path_cache.my_documents.path [0] == 0)
    {
      wcsncpy_s ( path_cache.steam_install, MAX_PATH,
                    SK_GetSteamDir (),      _TRUNCATE );

      SKIF_GetFolderPath ( &path_cache.my_documents       );
      SKIF_GetFolderPath ( &path_cache.app_data_local     );
      SKIF_GetFolderPath ( &path_cache.app_data_local_low );
      SKIF_GetFolderPath ( &path_cache.app_data_roaming   );
      SKIF_GetFolderPath ( &path_cache.win_saved_games    );
      SKIF_GetFolderPath ( &path_cache.desktop            );
    }

    isInitalized = true;
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

  if (! SKIF_IsWindows8Point1OrGreater ( ))
  {
    MessageBox (NULL, L"Special K requires at least Windows 8.1\nPlease update to a newer version of Windows.", L"Unsupported Windows", MB_OK | MB_ICONERROR);
    return 0;
  }

  ImGui_ImplWin32_EnableDpiAwareness ();

  GetSystemMetricsForDpi =
 (GetSystemMetricsForDpi_pfn)GetProcAddress (GetModuleHandle (L"user32.dll"),
 "GetSystemMetricsForDpi");

  //CoInitializeEx (nullptr, 0x0);

  WindowsCursorSize =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Microsoft\Accessibility\)",
                         LR"(CursorSize)" ).getData ();

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

  static auto regKVDisableGOGLibrary =
    SKIF_MakeRegKeyB(LR"(SOFTWARE\Kaldaien\Special K\)",
      LR"(Disable GOG Library)");

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

  static auto regKVAlwaysShowGhost =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Always Show Ghost)" );

  static auto regKVCloseToTray =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Minimize To Notification Area On Close)" );

  static auto regKVFontChineseSimplified =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Chinese Simplified Characters)" );

  static auto regKVFontChineseAll =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Chinese All Characters)" );

  static auto regKVFontCyrillic =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Cyrillic Characters)" );

  static auto regKVFontJapanese =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Japanese Characters)" );

  static auto regKVFontKorean =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Korean Characters)" );

  static auto regKVFontThai =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Thai Characters)" );

  static auto regKVFontVietnamese =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Load Vietnamese Characters)" );

  static auto regKVNotifications =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Notifications)" );

  SKIF_bDisableDPIScaling       =   regKVDisableDPIScaling.getData       ( );
//SKIF_bDisableExitConfirmation =   regKVDisableExitConfirmation.getData ( );
  SKIF_bDisableTooltips         =   regKVDisableTooltips.getData         ( );
  SKIF_bDisableStatusBar        =   regKVDisableStatusBar.getData        ( );
  SKIF_bDisableSteamLibrary     =   regKVDisableSteamLibrary.getData     ( );
  SKIF_bDisableGOGLibrary       =   regKVDisableGOGLibrary.getData       ( );
  SKIF_bEnableDebugMode         =   regKVEnableDebugMode.getData         ( );
  SKIF_bSmallMode               =   regKVSmallMode.getData               ( );
  SKIF_bFirstLaunch             =   regKVFirstLaunch.getData             ( );
  SKIF_bAllowMultipleInstances  =   regKVAllowMultipleInstances.getData  ( );
  SKIF_bAllowBackgroundService  =   regKVAllowBackgroundService.getData  ( );
//SKIF_bEnableHDR               =   regKVEnableHDR.getData               ( );
  SKIF_bOpenAtCursorPosition    =   regKVOpenAtCursorPosition.getData    ( );
  SKIF_bStopOnInjection         = ! regKVDisableStopOnInjection.getData  ( );
  SKIF_bAlwaysShowGhost         =   regKVAlwaysShowGhost.getData         ( );
  SKIF_bCloseToTray             =   regKVCloseToTray.getData             ( );
  SKIF_bFontChineseSimplified   =   regKVFontChineseSimplified.getData   ( );
  SKIF_bFontChineseAll          =   regKVFontChineseAll.getData          ( );
  SKIF_bFontCyrillic            =   regKVFontCyrillic.getData            ( );
  SKIF_bFontJapanese            =   regKVFontJapanese.getData            ( );
  SKIF_bFontKorean              =   regKVFontKorean.getData              ( );
  SKIF_bFontThai                =   regKVFontThai.getData                ( );
  SKIF_bFontVietnamese          =   regKVFontVietnamese.getData          ( );

  if ( regKVNotifications.hasData() )
    SKIF_iNotifications           =   regKVNotifications.getData         ( );

  hWndOrigForeground =
    GetForegroundWindow ();

  SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);

  // First round
  if (_Signal.Minimize)
    nCmdShow = SW_SHOWMINNOACTIVE;

  if (nCmdShow == SW_SHOWMINNOACTIVE && SKIF_bCloseToTray)
    nCmdShow = SW_HIDE;

  // Second round
  if (nCmdShow == SW_SHOWMINNOACTIVE)
    startedMinimized = true;
  else if (nCmdShow == SW_HIDE)
    startedMinimized = SKIF_isTrayed = true;

  // Check for updates
  SKIF_VersionCtl.CheckForUpdates (
    L"SKIF", SKIF_DEPLOYED_BUILD
  );

  // Check if Controlled Folder Access is enabled
  if (SKIF_hasControlledFolderAccess ( ))
  {
    MessageBox(NULL, L"Controlled Folder Access is enabled in Windows and may prevent Special K from working properly. "
                     L"It is recommended to either disable the feature or add exclusions for games where Special K is used as well as SKIF (this application)."
                     L"\n\n"
                     L"This warning will appear until SKIF (this application) have been excluded or the feature have been disabled."
                     L"\n\n"
                     L"Microsoft's support page with more information will be opened upon clicking OK.",
                     L"Warning about Controlled Folder Access",
               MB_ICONWARNING | MB_OK);

    SKIF_Util_OpenURI(L"https://support.microsoft.com/windows/allow-an-app-to-access-controlled-folders-b5b6627a-b008-2ca2-7931-7e51e912b034");
  }

  // Register SKIF in Windows to enable quick launching.
  SKIF_RegisterApp ( );

  // Cache the Special K user data path
  /* This is handled in the injection.cpp constructor instead
  SKIF_GetFolderPath (&path_cache.specialk_userdata);
  PathAppendW (        path_cache.specialk_userdata.path,
                         LR"(My Mods\SpecialK)"  );
  */

  /*
  int                                    app_id = SKIF_STEAM_APPID;
  if (StrStrW (lpCmdLine, L"AppID="))
  {   assert ( 1 ==
      swscanf (lpCmdLine, L"AppID=%li", &app_id)
             );
  }

  char      szAppID [16] = { };
  snprintf (szAppID, 15, "%li",          app_id);
  */

  // Create application window
  WNDCLASSEX wc =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, WndProc,
            0L,         0L,
    hModSKIF, nullptr,  nullptr,
              nullptr,  nullptr,
    _T ("SK_Injection_Frontend"),
              nullptr          };

  if (! ::RegisterClassEx (&wc))
  {
    return 0;
  }

  // Create invisible notify window (for the traybar icon and notification toasts)
  WNDCLASSEX wcNotify =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, SKIF_Notify_WndProc,
            0L,         0L,
        NULL, nullptr,  nullptr,
              nullptr,  nullptr,
    _T ("SK_Injection_Frontend_Notify"),
              nullptr          };

  if (! ::RegisterClassEx (&wcNotify))
  {
    return 0;
  }

  DWORD dwStyle   = SK_BORDERLESS,
        dwStyleEx = SK_BORDERLESS_EX;

  if (nCmdShow != SW_SHOWMINNOACTIVE &&
      nCmdShow != SW_SHOWNOACTIVATE  &&
      nCmdShow != SW_SHOWNA          &&
      nCmdShow != SW_HIDE)
    dwStyleEx &= ~WS_EX_NOACTIVATE;

  if (SKIF_isTrayed)
    dwStyle &= ~WS_VISIBLE;

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

  SKIF_Notify_hWnd      =
    CreateWindowExW (                                             WS_EX_NOACTIVATE,
      wcNotify.lpszClassName, _T("SK Injection Frontend Notify"), WS_ICONIC,
                         0, 0,
                         0, 0,
                   nullptr, nullptr,
        wcNotify.hInstance, nullptr
    );

  HWND  hWnd  = SKIF_hWnd;
  HDC   hDC   =
    GetWindowDC (hWnd);
  HICON hIcon =
    LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));

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

  // The window has been created but not displayed.
  // Now we have a parent window to which a notification tray icon can be associated.
  SKIF_CreateNotifyIcon       ();
  SKIF_CreateUpdateNotifyMenu ();

  // Show the window
  if (! SKIF_isTrayed)
  {
    ShowWindowAsync (hWnd, nCmdShow);
    UpdateWindow    (hWnd);
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION   ();
  ImGui::CreateContext ();

  ImGuiIO& io =
    ImGui::GetIO ();

  (void)io; // WTF does this do?!

  io.IniFilename = "SKIF.ini";                                   // nullptr to disable imgui.ini
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
//io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
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
  ImGui::StyleColorsDark    ( );
//ImGui::StyleColorsClassic ( );
//ImGui::StyleColorsLight   ( );

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
  if (pSwap3 != nullptr && SKIF_bCanFlipDiscard)
  {
    pSwap3->SetMaximumFrameLatency (1);

    hSwapWait.Attach (
      pSwap3->GetFrameLatencyWaitableObject ()
    );
  }

  ImGuiPlatformMonitor* monitor = nullptr;
  ImVec2 windowPos;
  ImRect monitor_extent   = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
  bool changedMode        = false;
       RepositionSKIF     = (! PathFileExistsW(L"SKIF.ini") || SKIF_bOpenAtCursorPosition);

  // Handle cases where a Start / Stop Command Line was Passed,
  //   but no running instance existed to service it yet...
  _Signal._Disowned = TRUE;

  if      (_Signal.Start || _Signal.Stop)
    SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);

  bool HiddenFramesContinueRendering = false;

  // Force a one-time check before we enter the main loop
  _inject.TestServletRunlevel (true);

  while (IsWindow (hWnd) && msg.message != WM_QUIT)
  {                         msg          = { };
    static UINT uiLastMsg = 0x0;
    auto _TranslateAndDispatch = [&](void) -> bool
    {
      while ( PeekMessage (&msg, 0, 0U, 0U, PM_REMOVE) &&
                            msg.message  !=  WM_QUIT)
      {
        if (! IsWindow (hWnd))
          return false;

        TranslateMessage (&msg);
        DispatchMessage  (&msg);

        uiLastMsg = msg.message;
      }

      return
        ( msg.message != WM_QUIT );
    };

    auto _UpdateOcclusionStatus = [&](HDC hDC)
    {
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
      }

      else
        bOccluded = FALSE;
    };

    const int           max_wait_objs  = 2;
    HANDLE hWaitStates [max_wait_objs] = {
      hSwapWait.m_h,
      hInjectAck.m_h
    };

    DWORD dwWait =
      MsgWaitForMultipleObjects (              0,
	                      //(hSwapWait.m_h != 0) ? 1
                                             //: 0,
                                hWaitStates, TRUE,
                                   INFINITE, QS_ALLINPUT );

    // Injection acknowledgment; shutdown injection
    //
    //  * This is backed by a periodic WM_TIMER message if injection
    //      was programmatically started and ACK has not signaled
    //
    if (                     hInjectAck.m_h != 0 &&
        WaitForSingleObject (hInjectAck.m_h,   0) == WAIT_OBJECT_0)
    {
      hInjectAck.Close ();
      _inject.bAckInjSignaled = true;
      _inject._StartStopInject (true);
    }

    // If SKIF is acting as a temporary launcher, exit when the running service has been stopped
    if (bExitOnInjection && _inject.runState == SKIF_InjectionContext::RunningState::Stopped)
    {
      static DWORD dwExitDelay = SKIF_timeGetTime();
      static int iDuration = -1;

      if (iDuration == -1)
      {
        HKEY    hKey;
        DWORD32 dwData  = 0;
        DWORD   dwSize  = sizeof (DWORD32);

        if (RegOpenKeyW (HKEY_CURRENT_USER, LR"(Control Panel\Accessibility\)", &hKey) == ERROR_SUCCESS)
        {
          iDuration = (RegGetValueW(hKey, NULL, L"MessageDuration", RRF_RT_REG_DWORD, NULL, &dwData, &dwSize) == ERROR_SUCCESS) ? dwData : 5;
          RegCloseKey(hKey);
        }
        else {
          iDuration = 5;
        }
      }
      // MessageDuration * 2 seconds delay to allow Windows to send both notifications properly
      // If notifications are disabled, exit immediately
      if (dwExitDelay + iDuration * 2 * 1000 < SKIF_timeGetTime() || SKIF_iNotifications == 0)
      {
        bExitOnInjection = false;
        PostMessage (hWnd, WM_QUIT, 0, 0);
      }
    }

    if (! _TranslateAndDispatch ())
      break;

    // Set DPI related variables
    //io.FontGlobalScale = 1.0f;
    SKIF_ImGui_GlobalDPIScale_Last = SKIF_ImGui_GlobalDPIScale;

    // Handling sub-1000px resolutions by rebuilding the font at 11px
    if (SKIF_ImGui_GlobalDPIScale < 1.0f && (! tinyDPIFonts))
    {
      SKIF_ImGui_InitFonts (11.0F);
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      tinyDPIFonts = true;
      invalidatedFonts = SKIF_timeGetTime();
    }

    else if (SKIF_ImGui_GlobalDPIScale >= 1.0f && tinyDPIFonts)
    {
      SKIF_ImGui_InitFonts ();
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      tinyDPIFonts = false;
      invalidatedFonts = SKIF_timeGetTime();
    }

    else if (invalidateFonts)
    {
      SKIF_ImGui_InitFonts ();
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      invalidateFonts = false;
      invalidatedFonts = SKIF_timeGetTime();
    }
    
    // This occurs on the next frame, as failedLoadFonts gets evaluated and set as part of ImGui_ImplDX11_NewFrame
    else if (failedLoadFonts)
    {
      SKIF_bFontChineseSimplified = false;
      SKIF_bFontChineseAll        = false;
      SKIF_bFontCyrillic          = false;
      SKIF_bFontJapanese          = false;
      SKIF_bFontKorean            = false;
      SKIF_bFontThai              = false;
      SKIF_bFontVietnamese        = false;

      regKVFontChineseSimplified.putData (SKIF_bFontChineseSimplified);
      regKVFontChineseAll       .putData (SKIF_bFontChineseAll);
      regKVFontCyrillic         .putData (SKIF_bFontCyrillic);
      regKVFontJapanese         .putData (SKIF_bFontJapanese);
      regKVFontKorean           .putData (SKIF_bFontKorean);
      regKVFontThai             .putData (SKIF_bFontThai);
      regKVFontVietnamese       .putData (SKIF_bFontVietnamese);
      
      SKIF_ImGui_InitFonts ();
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      failedLoadFonts = false;
      failedLoadFontsPrompt = true;
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
      ImRect rectCursorMonitor;

      // RepositionSKIF -- Step 1: Retrieve monitor of cursor, and set global DPI scale
      if (RepositionSKIF)
      {
        ImRect t;
        for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
        {
          const ImGuiPlatformMonitor& tmpMonitor = ImGui::GetPlatformIO().Monitors[monitor_n];
          t = ImRect(tmpMonitor.MainPos, (tmpMonitor.MainPos + tmpMonitor.MainSize));
          if (t.Contains(ImGui::GetMousePos()))
          {
            SKIF_ImGui_GlobalDPIScale = tmpMonitor.DpiScale;
            rectCursorMonitor = t;
          }
        }
      }

      SKIF_vecSmallMode   = ImVec2 ( SKIF_wSmallMode * SKIF_ImGui_GlobalDPIScale,
                                     SKIF_hSmallMode * SKIF_ImGui_GlobalDPIScale );
      SKIF_vecLargeMode   = ImVec2 ( SKIF_wLargeMode * SKIF_ImGui_GlobalDPIScale,
                                     SKIF_hLargeMode * SKIF_ImGui_GlobalDPIScale );

      // Add the status bar if it is not disabled
      if ( ! SKIF_bDisableStatusBar )
      {
        SKIF_vecLargeMode.y += 31.0f * SKIF_ImGui_GlobalDPIScale;
        SKIF_vecLargeMode.y += (SKIF_bDisableTooltips) ?
                  (ImGui::GetTextLineHeight () /* * 0.7f */) : 0.0f;
      }

      // Fixes weird size difference with this exact combination of settings which only occurs when DPI scaling is disabled
      if ((! SKIF_bDisableStatusBar) && SKIF_bDisableTooltips && SKIF_bDisableDPIScaling)
        SKIF_vecLargeMode.y += 6.0f;

      SKIF_vecCurrentMode  =
                    (SKIF_bSmallMode) ? SKIF_vecSmallMode
                                      : SKIF_vecLargeMode;

      if (ImGui::GetFrameCount() > 2)
        ImGui::SetNextWindowSize (SKIF_vecCurrentMode);

      // RepositionSKIF -- Step 2: Repositon the window
      if (RepositionSKIF)
      {
        // Repositions the window in the center of the monitor the cursor is currently on
        ImGui::SetNextWindowPos (ImVec2(rectCursorMonitor.GetCenter().x - (SKIF_vecCurrentMode.x / 2.0f), rectCursorMonitor.GetCenter().y - (SKIF_vecCurrentMode.y / 2.0f)));
      }

      // Calculate new window boundaries and changes to fit within the workspace if it doesn't fit
      //   Delay running the code to on the third frame to allow other required parts to have already executed...
      //     Otherwise window gets positioned wrong on smaller monitors !
      if (changedMode && ImGui::GetFrameCount() > 2)
      {
        changedMode = false;

        ImVec2 topLeft      = windowPos,
               bottomRight  = windowPos + SKIF_vecCurrentMode,
               newWindowPos = windowPos;

        if (      topLeft.x < monitor_extent.Min.x )
             newWindowPos.x = monitor_extent.Min.x;
        if (      topLeft.y < monitor_extent.Min.y )
             newWindowPos.y = monitor_extent.Min.y;

        if (  bottomRight.x > monitor_extent.Max.x )
             newWindowPos.x = monitor_extent.Max.x - SKIF_vecCurrentMode.x;
        if (  bottomRight.y > monitor_extent.Max.y )
             newWindowPos.y = monitor_extent.Max.y - SKIF_vecCurrentMode.y;

        if ( newWindowPos.x != windowPos.x ||
             newWindowPos.y != windowPos.y )
          ImGui::SetNextWindowPos(newWindowPos);
      }

      ImGui::Begin ( SKIF_WINDOW_TITLE_A SKIF_WINDOW_HASH,
                       nullptr,
                         ImGuiWindowFlags_NoResize          |
                         ImGuiWindowFlags_NoCollapse        |
                         ImGuiWindowFlags_NoTitleBar        |
                         ImGuiWindowFlags_NoScrollbar       | // Hide the scrollbar for the main window
                         ImGuiWindowFlags_NoScrollWithMouse   // Prevent scrolling with the mouse as well
      );

      SK_RunOnce (ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems += 2);

      HiddenFramesContinueRendering = (ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems > 0);
      HoverTipActive = false;

      // Update current monitors/worksize etc;
      monitor     = &ImGui::GetPlatformIO        ().Monitors [ImGui::GetCurrentWindowRead()->ViewportAllowPlatformMonitorExtend];

      // Move the invisible Win32 parent window over to the current monitor.
      //   This solves multiple taskbars not showing SKIF's window on all monitors properly.
      if (monitor->MainPos.x != ImGui::GetMainViewport()->Pos.x ||
          monitor->MainPos.y != ImGui::GetMainViewport()->Pos.y )
        MoveWindow (SKIF_hWnd, (int)monitor->MainPos.x, (int)monitor->MainPos.y, 0, 0, false);

      // RepositionSKIF -- Step 3: The Final Step -- Prevent the global DPI scale from potentially being set to outdated values
      if ( RepositionSKIF )
      {
        RepositionSKIF = false;
      } else if ( monitor->WorkSize.y < SKIF_hLargeMode && ImGui::GetFrameCount() > 1)
      {
        SKIF_ImGui_GlobalDPIScale = (monitor->WorkSize.y - 100.0f) / SKIF_hLargeMode;
      } else {
        SKIF_ImGui_GlobalDPIScale = (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts) ? (ImGui::GetCurrentWindow ()->Viewport->DpiScale <= 2.0f) ? ImGui::GetCurrentWindow ()->Viewport->DpiScale : 2.0f : 1.0f;
        /////SKIF_ImGui_GlobalDPIScale = (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts) ? ImGui::GetCurrentWindow()->Viewport->DpiScale : 1.0f;
      }

      // Rescale the style on DPI changes
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last)
      {
        style.WindowPadding                         = SKIF_ImGui_DefaultStyle.WindowPadding                       * SKIF_ImGui_GlobalDPIScale;
        style.WindowRounding                        = 4.0F                                                        * SKIF_ImGui_GlobalDPIScale;
        style.WindowMinSize                         = SKIF_ImGui_DefaultStyle.WindowMinSize                       * SKIF_ImGui_GlobalDPIScale;
        style.ChildRounding                         = style.WindowRounding;
        style.PopupRounding                         = SKIF_ImGui_DefaultStyle.PopupRounding                       * SKIF_ImGui_GlobalDPIScale;
        style.FramePadding                          = SKIF_ImGui_DefaultStyle.FramePadding                        * SKIF_ImGui_GlobalDPIScale;
        style.FrameRounding                         = style.WindowRounding;
        style.ItemSpacing                           = SKIF_ImGui_DefaultStyle.ItemSpacing                         * SKIF_ImGui_GlobalDPIScale;
        style.ItemInnerSpacing                      = SKIF_ImGui_DefaultStyle.ItemInnerSpacing                    * SKIF_ImGui_GlobalDPIScale;
        style.TouchExtraPadding                     = SKIF_ImGui_DefaultStyle.TouchExtraPadding                   * SKIF_ImGui_GlobalDPIScale;
        style.IndentSpacing                         = SKIF_ImGui_DefaultStyle.IndentSpacing                       * SKIF_ImGui_GlobalDPIScale;
        style.ColumnsMinSpacing                     = SKIF_ImGui_DefaultStyle.ColumnsMinSpacing                   * SKIF_ImGui_GlobalDPIScale;
        style.ScrollbarSize                         = SKIF_ImGui_DefaultStyle.ScrollbarSize                       * SKIF_ImGui_GlobalDPIScale;
        style.ScrollbarRounding                     = SKIF_ImGui_DefaultStyle.ScrollbarRounding                   * SKIF_ImGui_GlobalDPIScale;
        style.GrabMinSize                           = SKIF_ImGui_DefaultStyle.GrabMinSize                         * SKIF_ImGui_GlobalDPIScale;
        style.GrabRounding                          = SKIF_ImGui_DefaultStyle.GrabRounding                        * SKIF_ImGui_GlobalDPIScale;
        style.TabRounding                           = style.WindowRounding;
        if (style.TabMinWidthForUnselectedCloseButton != FLT_MAX)
	        style.TabMinWidthForUnselectedCloseButton = SKIF_ImGui_DefaultStyle.TabMinWidthForUnselectedCloseButton * SKIF_ImGui_GlobalDPIScale;
        style.DisplayWindowPadding                  = SKIF_ImGui_DefaultStyle.DisplayWindowPadding                * SKIF_ImGui_GlobalDPIScale;
        style.DisplaySafeAreaPadding                = SKIF_ImGui_DefaultStyle.DisplaySafeAreaPadding              * SKIF_ImGui_GlobalDPIScale;
        style.MouseCursorScale                      = SKIF_ImGui_DefaultStyle.MouseCursorScale                    * SKIF_ImGui_GlobalDPIScale;
      }

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

        // Hide the window for the 3 following frames as ImGui determines the sizes of items etc.
        //   This prevent flashing and elements appearing too large during those frames.
        ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems += 4;

        /* TODO: Fix QuickLaunch creating timers on SKIF_hWnd = 0,
         * causing SKIF to be unable to close them later if switched out from the mode.
        
        // If the user changed mode, cancel the exit action.
        if (bExitOnInjection)
          bExitOnInjection = false;

        // Be sure to load all extended character sets when changing mode
        if (_Signal.QuickLaunch)
          invalidateFonts = true;

        */
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
        if (SKIF_bCloseToTray && bKeepWindowAlive && ! SKIF_isTrayed)
        {
          bKeepWindowAlive = true;
          ShowWindow       (hWnd, SW_MINIMIZE);
          ShowWindow       (hWnd, SW_HIDE);
          UpdateWindow     (hWnd);
          SKIF_isTrayed    = true;
        }
        else
        {
          if (_inject.bCurrentState && ! SKIF_bDisableExitConfirmation)
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
      }

      ImGui::PopStyleVar ();
      
      if (SKIF_bCloseToTray)
        SKIF_ImGui_SetHoverTip ("SKIF will minimize to the notification area");
      else if (_inject.bCurrentState && SKIF_bDisableExitConfirmation && SKIF_bAllowBackgroundService)
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

          //if (tab_selected != Injection)
            //_inject._RefreshSKDLLVersions ();

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

            static DWORD dwLastRefresh = SKIF_timeGetTime();

            // Refresh once every 500 ms
            if (dwLastRefresh + 500 < SKIF_timeGetTime())
            {
              dwLastRefresh = SKIF_timeGetTime();

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

            //_inject._RefreshSKDLLVersions ();
          }

          tab_selected = Settings;

          // SKIF Options
          if (ImGui::CollapsingHeader ("Frontend v " SKIF_VERSION_STR_A " (" __DATE__ ")", ImGuiTreeNodeFlags_DefaultOpen))
          {
            ImGui::Spacing    ( );

            SKIF_ImGui_Columns      (2, nullptr, true);

            SK_RunOnce(
              ImGui::SetColumnWidth (0, SKIF_vecCurrentMode.x / 2.0f)
            );

            if ( ImGui::Checkbox ( "Always open SKIF on the same monitor as the mouse", &SKIF_bOpenAtCursorPosition ) )
              regKVOpenAtCursorPosition.putData(                                         SKIF_bOpenAtCursorPosition );
            
            if ( ImGui::Checkbox ( "Minimize SKIF to the notification area on close", &SKIF_bCloseToTray ) )
              regKVCloseToTray.putData(                                                SKIF_bCloseToTray );

            if (ImGui::Checkbox("Do not stop the global injection service when closing SKIF",
                                                   &SKIF_bAllowBackgroundService))
              regKVAllowBackgroundService.putData  (SKIF_bAllowBackgroundService);

            if ( ImGui::Checkbox (
                    "Allow multiple instances of SKIF",
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

            if ( ImGui::Checkbox ( "Always show Shelly the ghost",           &SKIF_bAlwaysShowGhost ) )
              regKVAlwaysShowGhost.putData(                                   SKIF_bAlwaysShowGhost );

            SKIF_ImGui_SetHoverTip    ("Every time the UI renders a frame, Shelly the Ghost moves a little bit.\n"
                                        "This otherwise only appears when with debug mode and the service running.");

            _inject._StartAtLogonCtrl ( );

            ImGui::Spacing       ( );
            ImGui::Spacing       ( );

            ImGui::Text          ("Hide games from select platforms");
            ImGui::TreePush      ("");

            if (ImGui::Checkbox        ("GOG", &SKIF_bDisableGOGLibrary))
            {
              regKVDisableGOGLibrary.putData   (SKIF_bDisableGOGLibrary);
              RepopulateGames = true;
            }

            ImGui::SameLine ( );
            ImGui::Spacing  ( );
            ImGui::SameLine ( );

            if (ImGui::Checkbox      ("Steam", &SKIF_bDisableSteamLibrary))
            {
              regKVDisableSteamLibrary.putData (SKIF_bDisableSteamLibrary);
              RepopulateGames = true;
            }

            ImGui::TreePop       ( );

            ImGui::NextColumn    ( );

            // New column

            ImGui::Text("Show notifications: ");
            ImGui::TreePush ("");
            if (ImGui::RadioButton ("Never", &SKIF_iNotifications, 0))
              regKVNotifications.putData(SKIF_iNotifications);
            ImGui::SameLine ( );
            if (ImGui::RadioButton ("Always", &SKIF_iNotifications, 1))
              regKVNotifications.putData(SKIF_iNotifications);
            ImGui::SameLine ( );
            if (ImGui::RadioButton ("When unfocused", &SKIF_iNotifications, 2))
              regKVNotifications.putData(SKIF_iNotifications);
            ImGui::TreePop  ( );

            ImGui::Spacing       ( );
            ImGui::Spacing       ( );

            ImGui::Text          ("Disable UI elements");
            ImGui::TreePush      ("");

            /*
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
            */

            if (ImGui::Checkbox ("Tooltips", &SKIF_bDisableTooltips))
              regKVDisableTooltips.putData (  SKIF_bDisableTooltips);

            if (ImGui::IsItemHovered ())
              SKIF_StatusBarText = "Info: ";

            SKIF_ImGui_SetHoverText ("This is where the info will be displayed.");
            SKIF_ImGui_SetHoverTip  ("The info will instead be displayed in the status bar at the bottom."
                                     "\nNote that some links cannot be previewed as a result.");

            ImGui::SameLine ( );
            ImGui::Spacing  ( );
            ImGui::SameLine ( );

            if (ImGui::Checkbox ("Status bar", &SKIF_bDisableStatusBar))
              regKVDisableStatusBar.putData (   SKIF_bDisableStatusBar);

            SKIF_ImGui_SetHoverTip (
              "Combining this with disabled UI tooltips will hide all context based information or tips."
            );

            ImGui::SameLine ( );
            ImGui::Spacing  ( );
            ImGui::SameLine ( );

            if (ImGui::Checkbox ("HiDPI scaling", &SKIF_bDisableDPIScaling))
            {
              io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

              if (SKIF_bDisableDPIScaling)
                io.ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleFonts;

              regKVDisableDPIScaling.putData      (SKIF_bDisableDPIScaling);
            }

            SKIF_ImGui_SetHoverTip (
              "This application will appear smaller on HiDPI monitors."
            );

            if (SKIF_bDisableTooltips &&
                SKIF_bDisableStatusBar)
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

            ImGui::TreePop       ( );

            ImGui::Spacing       ( );
            ImGui::Spacing       ( );

            ImGui::Text          ("Enable extended character sets (" ICON_FA_EXCLAMATION_TRIANGLE ")");

            SKIF_ImGui_SetHoverTip ("Can noticeably slow down the launch time of SKIF.");

            ImGui::TreePush      ("");

            // First column
            ImGui::BeginGroup ( );

            if (ImGui::Checkbox      ("Chinese (Simplified)", &SKIF_bFontChineseSimplified))
            {
              SKIF_bFontChineseAll = false;
              regKVFontChineseSimplified.putData (SKIF_bFontChineseSimplified);
              regKVFontChineseAll.putData        (SKIF_bFontChineseAll);
              invalidateFonts = true;
            }

            if (ImGui::Checkbox      ("Japanese", &SKIF_bFontJapanese))
            {
              regKVFontJapanese.putData (SKIF_bFontJapanese);
              invalidateFonts = true;
            }

            if (ImGui::Checkbox      ("Vietnamese", &SKIF_bFontVietnamese))
            {
              regKVFontVietnamese.putData (SKIF_bFontVietnamese);
              invalidateFonts = true;
            }

            ImGui::EndGroup ( );

            ImGui::SameLine ( );
            ImGui::Spacing  ( );
            ImGui::SameLine ( );

            // Second column
            ImGui::BeginGroup ( );

/*
#ifndef _WIN64
            ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
#endif
*/

            if (ImGui::Checkbox      ("Chinese (All)", &SKIF_bFontChineseAll))
            {
              SKIF_bFontChineseSimplified = false;
              regKVFontChineseSimplified.putData (SKIF_bFontChineseSimplified);
              regKVFontChineseAll.putData        (SKIF_bFontChineseAll);
              invalidateFonts = true;
            }

/*
#ifndef _WIN64
            ImGui::PopStyleVar ( );
            ImGui::PopItemFlag ( );
            
            SKIF_ImGui_SetHoverTip ("Not included in 32-bit SKIF due to system limitations.");
#endif
*/

            if (ImGui::Checkbox      ("Korean", &SKIF_bFontKorean))
            {
              regKVFontKorean.putData (SKIF_bFontKorean);
              invalidateFonts = true;
            }

/*
#ifndef _WIN64
            SKIF_ImGui_SetHoverTip ("32-bit SKIF does not include Hangul syllables due to system limitations.");
#endif
*/

            ImGui::EndGroup      ( );

            ImGui::SameLine      ( );
            ImGui::Spacing       ( );
            ImGui::SameLine      ( );

            // Third column
            ImGui::BeginGroup    ( );

            if (ImGui::Checkbox      ("Cyrillic", &SKIF_bFontCyrillic))
            {
              regKVFontCyrillic.putData   (SKIF_bFontCyrillic);
              invalidateFonts = true;
            }

            if (ImGui::Checkbox      ("Thai", &SKIF_bFontThai))
            {
              regKVFontThai.putData (SKIF_bFontThai);
              invalidateFonts = true;
            }

            ImGui::EndGroup      ( );

            ImGui::TreePop       ( );

            ImGui::NewLine       ( );

            ImGui::Text          ("Experimental SKIF features");
            ImGui::TreePush      ("");

            if (ImGui::Checkbox ("Enable debug mode (" ICON_FA_BUG ")",
                                                       &SKIF_bEnableDebugMode))
            {
              SKIF_ProcessCommandLine ( ( std::string ("SKIF.UI.DebugMode ") +
                                          std::string ( SKIF_bEnableDebugMode ? "On"
                                                                              : "Off" )
                                        ).c_str ()

              );
              regKVEnableDebugMode.putData(             SKIF_bEnableDebugMode);
            }

            /* HDR was only needed for screenshot viewing
            if (ImGui::Checkbox  ("HDR on compatible displays (restart required)###HDR_ImGui", &SKIF_bEnableHDR))
              regKVEnableHDR.putData (                                                          SKIF_bEnableHDR);

            _DrawHDRConfig       ( );
            */

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
              "Special K can give users an insight into how frames are presented by tracking ETW events and changes as they occur."
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
              ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
            }

            std::string btnPfuLabel = (pfuState == Granted) ?                                ICON_FA_CHECK " Permissions granted!" // Granted
                                                            : (pfuState == Missing) ?   ICON_FA_SHIELD_ALT " Grant permissions"    // Missing
                                                                                    : ICON_FA_SIGN_OUT_ALT " Sign out to apply";   // Pending

            if ( ImGui::ButtonEx ( btnPfuLabel.c_str(), ImVec2( 200 * SKIF_ImGui_GlobalDPIScale,
                                                                 25 * SKIF_ImGui_GlobalDPIScale)))
            {
              std::wstring exeArgs;

              // Use the SIDs since the user and group have different names on non-English systems
              // S-1-5-4      = NT AUTHORITY\INTERACTIVE           == Any interactive user sessions
              // S-1-5-32-559 =     BUILT-IN\Performance Log Users == Members of this group may schedule logging of performance counters, enable trace providers, and collect event traces both locally and via remote access to this computer

              if (SKIF_IsWindows10OrGreater ( )) // On Windows 10, use the native PowerShell cmdlet Add-LocalGroupMember since it supports SIDs
                exeArgs  = LR"(-NoProfile -NonInteractive -WindowStyle Hidden -Command "Add-LocalGroupMember -SID 'S-1-5-32-559' -Member 'S-1-5-4'")";
              else                               // Windows 8.1 lacks Add-LocalGroupMember, so fall back on using WMI (to retrieve the localized names of the group and user) and NET to add the user to the group
                exeArgs  = LR"(-NoProfile -NonInteractive -WindowStyle Hidden -Command "$Group = (Get-WmiObject -Class Win32_Group -Filter 'LocalAccount = True AND SID = \"S-1-5-32-559\"').Name; $User = (Get-WmiObject -Class Win32_SystemAccount -Filter 'LocalAccount = True AND SID = \"S-1-5-4\"').Name; net localgroup \"$Group\" \"$User\" /add")";

              if (ShellExecuteW (nullptr, L"runas", L"powershell", exeArgs.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32) // COM exception is thrown?
                pfuState = Pending;
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

            ImGui::EndGroup();

#ifdef _WIN64
            ImGui::Spacing  ();
            ImGui::Spacing  ();

            ImGui::Separator   ();

            // WinRing0
            ImGui::BeginGroup  ();
            ImGui::Spacing     ();

            ImGui::Text        ("Extended CPU Hardware Reporting");

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
                                "Extends the CPU widget with thermals, energy, and precise clock rate on modern hardware."
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
            static std::wstring wszDriverTaskCmd;

            static bool requiredFiles =
              PathFileExistsW (LR"(Servlet\driver_install.ps1)")   &&
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
              wszDriverTaskCmd = (LR"(-ExecutionPolicy Bypass -File ")" + std::filesystem::current_path().wstring() + LR"(\Servlet\driver_uninstall.ps1")");
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
              wszDriverTaskCmd = (LR"(-ExecutionPolicy Bypass -File ")" + std::filesystem::current_path().wstring() + LR"(\Servlet\driver_install.ps1")");
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
              ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
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
                    L"powershell",
                      wszDriverTaskCmd.c_str(), nullptr,
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
#endif
          }

          ImGui::Spacing ();
          ImGui::Spacing ();

          // Whitelist/Blacklist
          if (ImGui::CollapsingHeader ("Whitelist/Blacklist", ImGuiTreeNodeFlags_DefaultOpen))
          {
            static bool white_edited = false,
                        black_edited = false,
                        white_stored = true,
                        black_stored = true;

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

              _inject._AddUserList("Games", true);
            }

            SKIF_ImGui_SetHoverTip (
              "Whitelists games on most platforms, such as Epic Games, Origin, Uplay, etc."
            );

            if (ImGui::Selectable ("WindowsApps"))
            {
              white_edited = true;

              _inject._AddUserList("WindowsApps", true);
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
              ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
            }

            // Hotkey: Ctrl+S
            if (ImGui::Button (ICON_FA_SAVE " Save Changes") || ((! bDisabled) && io.KeyCtrl && io.KeysDown ['S']))
            {
              // Clear the active ID to prevent ImGui from holding outdated copies of the variable
              //   if saving succeeds, to allow _StoreList to update the variable successfully
              ImGui::ClearActiveID();

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
          SKIF_ImGui_Columns      (2, nullptr, true);

          SK_RunOnce (
            ImGui::SetColumnWidth (0, 600.0f * SKIF_ImGui_GlobalDPIScale)
          );

          ImGui::PushStyleColor   (
            ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f)
                                    );

          // ImColor::HSV (0.11F, 1.F, 1.F)   // Orange
          // ImColor::HSV (0.55F, 0.99F, 1.F) // Blue Bullets
          // ImColor(100, 255, 218); // Teal
          //ImGui::GetStyleColorVec4(ImGuiCol_TabHovered);
          ImColor colTitle = ImColor(3, 179, 255);

          ImGui::TextColored      (
            colTitle,
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
            colTitle,
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
            colTitle,
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

          ImGui::TextColored      (
            colTitle, ICON_FA_ROCKET "  "
              "Quick launch Special K for select games through Steam:");
          if (SKIF_RegisterApp())
          {
            ImGui::TextWrapped("Your computer is set up to quickly launch injection through Steam.");

            SKIF_ImGui_Spacing      ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "1 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Right click the desired game in Steam, and select \"Properties...\".");
            ImGui::EndGroup         ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "2 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Copy and paste the below into the \"Launch Options\" field.");
            ImGui::EndGroup         ( );

            ImGui::TreePush         ("");
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::InputTextEx      ("###QuickLaunch", NULL, "SKIF %COMMAND%", MAX_PATH, ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor    ( );
            ImGui::TreePop          ( );

            ImGui::BeginGroup       ( );
            ImGui::Spacing          ( );
            ImGui::SameLine         ( );
            ImGui::TextColored      (
              ImColor::HSV (0.55F, 0.99F, 1.F),
                                  "3 ");
            ImGui::SameLine         ( );
            ImGui::TextWrapped      ("Launch the game as usual through Steam.");
            ImGui::EndGroup         ( );
          }
          else {
            ImGui::TextWrapped("Your computer is not set up to quickly launch injection through Steam.");
          }

          ImGui::NewLine          ( );
          ImGui::NewLine          ( );

          ImGui::TextColored      (
            colTitle,
                                   ICON_FA_WRENCH "  Compatibility Options:");

          SKIF_ImGui_Spacing      ( );

          ImGui::TextWrapped      ("Hold down CTRL + Shift when starting a game to access compatibility options.");


          float pushColumnSeparator =
            (900.0f * SKIF_ImGui_GlobalDPIScale) - ImGui::GetCursorPosY                () -
                                                  (ImGui::GetTextLineHeightWithSpacing () );

          ImGui::ItemSize (
            ImVec2 (0.0f, pushColumnSeparator)
          );


          ImGui::NextColumn       ( ); // Next Column
          ImGui::TextColored      (
            colTitle,
              "About Special K:"    );

          SKIF_ImGui_Spacing      ( );

          ImGui::TextWrapped      ("Lovingly referred to as the Swiss Army Knife of PC gaming, Special K does a bit of everything.");
          ImGui::NewLine          ( );
          ImGui::TextWrapped      ("It is best known for fixing and enhancing graphics, its many detailed performance analysis and correction mods, "
                                   "and a constantly growing palette of tools that solve a wide variety of issues affecting PC games.");

          ImGui::PopStyleColor    ( );

          ImGui::NewLine          ( );
          ImGui::NewLine          ( );

          ImGui::TextColored (
            colTitle,
                         "About multiplayer games:");

          SKIF_ImGui_Spacing      ( );

          ImGui::BeginGroup  ();
          ImGui::Spacing     ();
          ImGui::SameLine    ();
          ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), "? ");
          ImGui::SameLine    ();
          ImGui::TextColored (ImColor (0.68F, 0.68F, 0.68F),
                                                                "The service injects Special K into most user processes."
          );
          ImGui::EndGroup    ();

          SKIF_ImGui_SetHoverTip (
            "Any that deal with system input or some sort\nof window or keyboard/mouse input activity."
          );

          ImGui::BeginGroup  ();
          ImGui::Spacing     (); ImGui::SameLine ();
          ImGui::TextColored (ImColor::HSV (0.55F, 0.99F, 1.F), "?!");
          ImGui::SameLine    ();
          ImGui::TextColored (ImColor (0.68F, 0.68F, 0.68F),
                                                                "Stop the service before playing a multiplayer game."
          );
          ImGui::EndGroup    ();

          SKIF_ImGui_SetMouseCursorHand ();
          SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");
          SKIF_ImGui_SetHoverTip        ("In particular games where anti-cheat\nprotection might be present.");

          if (ImGui::IsItemClicked ())
            SKIF_Util_OpenURI (L"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

          ImGui::NewLine          ( );
          ImGui::NewLine          ( );

          ImGui::TextColored (
            colTitle,
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
            colTitle,
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
            SKIF_Util_OpenURI     (L"https://discord.gg/specialk");

          SKIF_ImGui_SetMouseCursorHand ();
          SKIF_ImGui_SetHoverText ( "https://discord.gg/specialk");
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

          /*

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
          SKIF_ImGui_SetHoverTip  ( "The link that will be opened is also shown at"
                                    "\nthe bottom of the window as in a web browser.");
          if (ImGui::IsItemClicked ())
            SKIF_Util_OpenURI     (L"https://wiki.special-k.info/");

          */

          ImGui::NewLine          ( );
          ImGui::NewLine          ( );

          ImGui::TextColored (
            colTitle,
              "Status:"
          );

          SKIF_ImGui_Spacing      ( );

          ImGui::BeginGroup       ( );
          ImGui::Spacing          ( );
          ImGui::SameLine         ( );

          static bool isSKIFAdmin = IsUserAnAdmin();
          if (isSKIFAdmin)
          {
            ImGui::TextColored     (ImColor (255, 204, 0), ICON_FA_EXCLAMATION_TRIANGLE " ");
            ImGui::SameLine        ( );
            ImGui::TextColored     (ImColor (255, 204, 0), "SKIF is running as an administrator!");
            SKIF_ImGui_SetHoverTip ( "Running elevated is not recommended as it will inject Special K into system processes.\n"
                                     "Please restart the global injector service and SKIF as a regular user.");
          }
          else {
            ImGui::TextColored     (ImColor (144, 238, 144), ICON_FA_CHECK " ");
            ImGui::SameLine        ( );
            ImGui::TextColored     (ImColor(144, 238, 144), "SKIF is running with normal privileges.");
            SKIF_ImGui_SetHoverTip ( "This is the recommended option as Special K will not be injected\n"
                                     "into system processes nor games running as an administrator.");
          }

          ImGui::EndGroup         ( );

          struct Platform {
            std::string     Name;
            std::wstring    ProcessName;
            DWORD           ProcessID   = 0,
                            PreviousPID = 0;
            bool            isRunning   = false,
                            isAdmin     = false;

            Platform (std::string n, std::wstring pn)
            {
              Name        =  n;
              ProcessName = pn;
              Refresh ( );
            }

            void Refresh (void)
            {
              PROCESSENTRY32W pe = SK_FindProcessByName (ProcessName.c_str());
              ProcessID = pe.th32ProcessID;

              if (ProcessID != PreviousPID)
              {
                PreviousPID = ProcessID;
                isRunning   = (ProcessID > 0);

                if (isRunning)
                  isAdmin = SK_IsProcessAdmin (pe);
              }
            }
          };

          static DWORD dwLastRefresh = 0;
          static Platform Platforms[] = {
            {"32-bit Service",      L"SKIFsvc32.exe"},
#ifdef _WIN64
            {"64-bit Service",      L"SKIFsvc64.exe"},
#endif
            {"Steam",               L"steam.exe"},
            {"Origin",              L"Origin.exe"},
            {"Galaxy",              L"GalaxyClient.exe"},
            {"EA Desktop",          L"EADesktop.exe"},
            {"Epic Games Launcher", L"EpicGamesLauncher.exe"},
            {"Ubisoft Connect",     L"upc.exe"}
          };

          for each (Platform &p in Platforms)
          {
            if ( dwLastRefresh + 1000 < SKIF_timeGetTime ())
                p.Refresh ( ); // Timer has expired, refresh

            if (p.isRunning)
            {
              ImGui::BeginGroup       ( );
              ImGui::Spacing          ( );
              ImGui::SameLine         ( );

              if (p.isAdmin)
              {
                ImGui::TextColored     (ImColor (255, 204, 0), ICON_FA_EXCLAMATION_TRIANGLE " ");
                ImGui::SameLine        ( );
                ImGui::TextColored     (ImColor (255, 204, 0), (p.Name + " is running as an administrator!").c_str() );

                if (isSKIFAdmin)
                  SKIF_ImGui_SetHoverTip ( ("It is not recommended to run either " + p.Name + " or SKIF as an administrator.\n"
                                            "Please restart both as a normal user.").c_str());
                else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
                  SKIF_ImGui_SetHoverTip ( "Running elevated is not recommended as it will inject Special K into system processes.\n"
                                           "Please restart the global injector service and SKIF as a regular user.");
                else
                  SKIF_ImGui_SetHoverTip ( ("Running elevated will prevent injection into these games.\n"
                                            "Please restart " + p.Name + " as a normal user.").c_str());
              }
              else {
                ImGui::TextColored     (ImColor (144, 238, 144), ICON_FA_CHECK " ");
                ImGui::SameLine        ( );
                ImGui::TextColored     (ImColor (144, 238, 144), (p.Name + " is running.").c_str());
              }

              ImGui::EndGroup          ( );
            }
            else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
            {
              ImGui::Spacing          ( );
              ImGui::SameLine         ( );
              ImGui::TextColored     (ImColor (0.68F, 0.68F, 0.68F), " " ICON_FA_TIMES " ");
              ImGui::SameLine        ( );
              ImGui::TextColored     (ImColor (0.68F, 0.68F, 0.68F), (p.Name + " is not running.").c_str());
            }

            if (p.ProcessName == L"SKIFsvc64.exe")
              ImGui::NewLine           ( );
          }

          if ( dwLastRefresh + 1000 < SKIF_timeGetTime ())
               dwLastRefresh        = SKIF_timeGetTime (); // Set timer for next refresh

          ImGui::Columns          (1);

          ImGui::EndChildFrame    ( );
          ImGui::EndTabItem       ( );
        }

//#ifdef _WIN64
        bool debugSelected =       SKIF_bEnableDebugMode &&
          ImGui::BeginTabItem     (ICON_FA_BUG " Debug", nullptr, flags);

        if (debugSelected)
        {
          SKIF_ImGui_BeginTabChildFrame ();

          if (tab_selected != Debug)
          {
            SetTimer (SKIF_hWnd,
                      IDT_REFRESH_DEBUG,
                      500,
                      (TIMERPROC)NULL
            );
          }

          tab_selected = Debug;

          extern HRESULT
            SKIF_Debug_DrawUI (void);
            SKIF_Debug_DrawUI (    );

          ImGui::EndChildFrame    ( );
          ImGui::EndTabItem       ( );
        }

        else {
          if (SKIF_bEnableDebugMode && hModSpecialK != 0)
          {
            FreeLibrary (hModSpecialK);
            hModSpecialK = nullptr;

            KillTimer(SKIF_hWnd, IDT_REFRESH_DEBUG);
          }
        }
//#endif

        // Ghost

        float title_len = ImGui::CalcTextSize(SKIF_WINDOW_TITLE_A).x;
          //ImGui::GetFont ()->CalcTextSizeA ((tinyDPIFonts) ? 11.0F : 18.0F, FLT_MAX, 0.0f, SKIF_WINDOW_TITLE_A ).x;

        float title_pos = SKIF_vecCurrentMode.x / 2.0f - title_len / 2.0f;
          /*
          ImGui::GetCursorPos().x +
            (
              (ImGui::GetContentRegionAvail().x - 100.0f * SKIF_ImGui_GlobalDPIScale) - title_len
            )
                                                                                      / 2.0f;
          */

        ImGui::SetCursorPosX (title_pos);

        ImGui::SetCursorPosY (
          7.0f * SKIF_ImGui_GlobalDPIScale
        );

        ImGui::TextColored (ImVec4 (0.5f, 0.5f, 0.5f, 1.f), SKIF_WINDOW_TITLE_A_EX); // ImVec4 (.666f, .666f, .666f, 1.f)

        if (SKIF_bAlwaysShowGhost || (_inject.bCurrentState && SKIF_bEnableDebugMode) )
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

          if (tab_selected == Injection)
          {
            ImVec2 tmpPos = ImGui::GetCursorPos();

            static bool btnHovered = false;
            ImGui::PushStyleColor (ImGuiCol_Button,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
            ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
            ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

            if (btnHovered)
              ImGui::PushStyleColor (ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            else
              ImGui::PushStyleColor (ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));

            if (ImGui::Button( ICON_FA_PLUS_SQUARE " Add Game"))
              AddGamePopup = PopupState::Open;

            btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

            ImGui::PopStyleColor (4);

            ImGui::SetCursorPos(tmpPos);
          }

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
            ImGui::GetCursorPosX () -
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


      // Font warning
      if (failedLoadFontsPrompt && ! HiddenFramesContinueRendering)
      {
        failedLoadFontsPrompt = false;

        ImGui::OpenPopup ("###FailedFontsPopup");
      }
      

      float ffailedLoadFontsWidth = 400.0f * SKIF_ImGui_GlobalDPIScale;
      ImGui::SetNextWindowSize (ImVec2 (ffailedLoadFontsWidth, 0.0f));

      if (ImGui::BeginPopupModal ("Fonts failed to load###FailedFontsPopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
      {
        ImGui::TreePush    ("");

        SKIF_ImGui_Spacing ( );

        ImGui::TextWrapped ("The selected character sets failed to load due to system limitations and have been reset.");
        ImGui::NewLine     ( );
        ImGui::TextWrapped ("Please limit the selection to only the most essential.");

        SKIF_ImGui_Spacing ( );
        SKIF_ImGui_Spacing ( );

        ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

        ImGui::SetCursorPosX (ffailedLoadFontsWidth / 2 - vButtonSize.x / 2);

        if (ImGui::Button  ("OK", vButtonSize))
          ImGui::CloseCurrentPopup ( );

        SKIF_ImGui_Spacing ( );

        ImGui::TreePop     ( );

        ImGui::EndPopup ( );
      }


      // Confirm Exit prompt
      ImGui::SetNextWindowSize (
        ImVec2 ( (SKIF_bAllowBackgroundService)
                    ? 515.0f * SKIF_ImGui_GlobalDPIScale
                    : 350.0f * SKIF_ImGui_GlobalDPIScale,
                   0.0f )
      );
      ImGui::SetNextWindowPos (ImGui::GetCurrentWindow()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      if (ImGui::BeginPopupModal ( "Confirm Exit", nullptr,
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
        SKIF_ImGui_Spacing ();

        if (SKIF_bAllowBackgroundService)
          ImGui::TextColored ( ImColor::HSV (0.11F, 1.F, 1.F),
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

      // Uses a Directory Watch signal, so this is cheap; do it once every frame.
      _inject.TestServletRunlevel       ();

      // Ensure the taskbar overlay icon always shows the correct state
      if (_inject.bTaskbarOverlayIcon != _inject.bCurrentState)
        _inject._SetTaskbarOverlay      (_inject.bCurrentState);

      monitor_extent =
        ImGui::GetWindowAllowedExtentRect (
          ImGui::GetCurrentWindowRead ()
        );
      windowPos      = ImGui::GetWindowPos ();

      if (! HoverTipActive)
        HoverTipDuration = 0;

      // This allows us to ensure the window gets set within the workspace on the second frame after launch
      SK_RunOnce (
        changedMode = true
      );

      // This allows us to compact the working set on launch
      SK_RunOnce (
        invalidatedFonts = SKIF_timeGetTime ( )
      );

      if (invalidatedFonts > 0 &&
          invalidatedFonts + 500 < SKIF_timeGetTime())
      {
        SKIF_Util_CompactWorkingSet ();
        invalidatedFonts = 0;
      }

      ImGui::End ();
    }

    SK_RunOnce (_inject._InitializeJumpList ( ));

    // Actual rendering is conditional, this just processes input
    ImGui::Render ();


    bool bRefresh   = (SKIF_isTrayed) ? false : true;
    bool bDwmTiming = false;

    float fDwmPeriod = 6.0f;

    DWM_TIMING_INFO dwm_timing = { };
                    dwm_timing.cbSize = sizeof (DWM_TIMING_INFO);

    if (SUCCEEDED (SK_DWM_GetCompositionTimingInfo (&dwm_timing)))
    {
      bDwmTiming = true;
      fDwmPeriod =
        1000.0f / ( static_cast <float> (dwm_timing.rateRefresh.uiNumerator) /
                    static_cast <float> (dwm_timing.rateRefresh.uiDenominator) );
    }

    const auto uiMinMousePeriod =
      SKIF_ImGui_IsFocused () ? static_cast <UINT> (fDwmPeriod * 0.5f)
                              : static_cast <UINT> (fDwmPeriod * 2.5f);

    if (uiLastMsg == WM_MOUSEMOVE)
    {
      static UINT
          uiLastMove = 0;
      if (uiLastMove > SKIF_timeGetTime () - uiMinMousePeriod)
      {
        bRefresh = false;
      }

      else
        uiLastMove = SKIF_timeGetTime ();
    }

    if (bDwmTiming)
    {
      static QPC_TIME             dwmLastVBlank = 0;

      if (dwm_timing.qpcVBlank <= dwmLastVBlank)
        bRefresh = false;

      if (bRefresh && hSwapWait.m_h != 0)
      {
        // Wait on SwapChain
        bRefresh =
          (WaitForSingleObject (hSwapWait.m_h, 0) != WAIT_TIMEOUT);
      }

      if (bRefresh)
        dwmLastVBlank = dwm_timing.qpcVBlank;
    }

    // First message always draws
    SK_RunOnce (bRefresh = true);

    if (bRefresh)
    {
      g_pd3dDeviceContext->OMSetRenderTargets    (1, &g_mainRenderTargetView, nullptr);
      g_pd3dDeviceContext->ClearRenderTargetView (    g_mainRenderTargetView, (float*)&clear_color);

      if (! startedMinimized && ! SKIF_isTrayed)
      {
        ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
          ImGui::UpdatePlatformWindows        ();
          ImGui::RenderPlatformWindowsDefault ();
        }
      }
    }

    if ( startedMinimized && SKIF_ImGui_IsFocused ( ) )
    {
      startedMinimized = false;
      if ( SKIF_bOpenAtCursorPosition )
        RepositionSKIF = true;
    }

    UINT Interval =
      SKIF_bAllowTearing ? 0
                         : 1;
    UINT  Flags   =
      SKIF_bAllowTearing ? DXGI_PRESENT_ALLOW_TEARING
                         : 0x0;

    if (bRefresh)
    {
      if (FAILED (g_pSwapChain->Present (Interval, Flags)))
        break;
    }

    _UpdateOcclusionStatus (hDC);

    if ((! bKeepProcessAlive) && hWnd != 0)
      PostMessage (hWnd, WM_QUIT, 0x0, 0x0);

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

      while ( IsWindow (hWnd) && ! SKIF_ImGui_IsFocused ( ) && ! HiddenFramesContinueRendering &&
                WAIT_OBJECT_0 != MsgWaitForMultipleObjects ( 1, &event.m_h, FALSE,
                                                              INFINITE, QS_ALLINPUT ) )
      {
        if (! _TranslateAndDispatch ())
          break;

        else if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) break;
        else if (msg.message >= WM_KEYFIRST   && msg.message <= WM_KEYLAST)   break;
        else if (msg.message == WM_SETFOCUS   || msg.message == WM_KILLFOCUS) break;
        else if (msg.message == WM_TIMER)                                     break;
      }
    }
  }

  KillTimer (SKIF_hWnd, IDT_REFRESH_ONDEMAND);
  KillTimer (SKIF_hWnd, IDT_REFRESH_PENDING);
  KillTimer (SKIF_hWnd, IDT_REFRESH_DEBUG);

  ImGui_ImplDX11_Shutdown   (    );
  ImGui_ImplWin32_Shutdown  (    );

  CleanupDeviceD3D          (    );

  Shell_NotifyIcon          (NIM_DELETE, &niData);
  DeleteObject              (niData.hIcon);
  niData.hIcon               = 0;
  DestroyWindow             (SKIF_Notify_hWnd);

  if (hDC != 0)
    ReleaseDC               (hWnd, hDC);
  DestroyWindow             (hWnd);

  ImGui::DestroyContext     (    );

  SKIF_Notify_hWnd = 0;
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

  SKIF_bCanFlip               =
    SKIF_IsWindows8Point1OrGreater () != FALSE,
  SKIF_bCanFlipDiscard        =
    SKIF_IsWindows10OrGreater      () != FALSE;

  // Overrides
//SKIF_bAllowTearing          = FALSE; // Disable ALLOW_TEARING on all systems (this overrides the variable assignment just 10 lines above, pFactory5->CheckFeatureSupport)
//SKIF_bCanFlipDiscard        = FALSE; // Flip Discard
//SKIF_bCanFlip               = FALSE; // Flip Sequential

  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC
    sd                                  = { };
  sd.BufferCount                        =
                          SKIF_bCanFlip ?  3
                                        :  2 ;
  sd.BufferDesc.Width                   =  4 ;
  sd.BufferDesc.Height                  =  4 ;
  sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator   = 0;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags                              =
                   SKIF_bCanFlipDiscard ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                                        : 0x0;
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
   SKIF_bCanFlipDiscard ?                 DXGI_SWAP_EFFECT_FLIP_DISCARD
                        : SKIF_bCanFlip ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
                                        : DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;

  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL
                    featureLevelArray [4] = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
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

  IUnknown_AtomicRelease ((void **)&g_pSwapChain);
  IUnknown_AtomicRelease ((void **)&g_pd3dDeviceContext);
  IUnknown_AtomicRelease ((void **)&g_pd3dDevice);
}

// Prevent race conditions between asset loading and device init
//
void SKIF_WaitForDeviceInitD3D (void)
{
  while ( g_pd3dDevice        == nullptr ||
          g_pd3dDeviceContext == nullptr ||
          g_pSwapChain        == nullptr )
  {
    Sleep (10UL);
  }
}

CComPtr <ID3D11Device>
SKIF_D3D11_GetDevice (bool bWait)
{
  if (bWait)
    SKIF_WaitForDeviceInitD3D ();

  return
    g_pd3dDevice;
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
  IUnknown_AtomicRelease ((void **)&g_mainRenderTargetView);
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
    case WM_SKIF_MINIMIZE:
      if (SKIF_bCloseToTray && ! SKIF_isTrayed)
      {
        ShowWindow       (hWnd, SW_MINIMIZE);
        ShowWindow       (hWnd, SW_HIDE);
        UpdateWindow     (hWnd);
        SKIF_isTrayed    = true;
      }

      else if (! SKIF_bCloseToTray) {
        ShowWindowAsync (hWnd, SW_MINIMIZE);
      }
      break;

    case WM_SKIF_START:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false);
      break;

    case WM_SKIF_TEMPSTART:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false, true);
      break;

    case WM_SKIF_STOP:
      _inject._StartStopInject   (true);
      break;

    case WM_SKIF_REFRESHGAMES:
      RepopulateGamesWasSet = SKIF_timeGetTime();
      RepopulateGames = true;
      SelectNewSKIFGame = (uint32_t)wParam;

      SetTimer (SKIF_hWnd,
          IDT_REFRESH_GAMES,
          16,
          (TIMERPROC) NULL
      );
      break;

    case WM_SKIF_QUICKLAUNCH:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false, true);

      // Reload the whitelist as it might have been changed
      _inject._LoadList          (true);
      break;

    case WM_SKIF_RESTORE:
      _inject.bTaskbarOverlayIcon = false;

      if (! SKIF_isTrayed && ! IsIconic (hWnd))
        RepositionSKIF = true;

      if (SKIF_isTrayed)
      {
        SKIF_isTrayed               = false;
        ShowWindowAsync (hWnd, SW_SHOW);
      }

      ShowWindowAsync     (hWnd, SW_RESTORE);
      UpdateWindow        (hWnd);

      SetForegroundWindow (hWnd);
      SetActiveWindow     (hWnd);
      break;

    case WM_TIMER:
      switch (wParam)
      {
        case IDT_REFRESH_ONDEMAND:
        case IDT_REFRESH_PENDING:
        case IDT_REFRESH_DEBUG:
        case IDT_REFRESH_GAMES:
        //OutputDebugString(L"Tick\n");

          if (wParam == IDT_REFRESH_GAMES && RepopulateGamesWasSet != 0)
          {
            if (RepopulateGamesWasSet != 0 && RepopulateGamesWasSet + 1000 < SKIF_timeGetTime())
            {
              RepopulateGamesWasSet = 0;
              KillTimer  (SKIF_hWnd, IDT_REFRESH_GAMES);
            }
          }

          /*
          if (wParam == IDT_REFRESH_DEBUG)
            OutputDebugString(L"Debug tick\n");
          else if (wParam == IDT_REFRESH_ONDEMAND)
            OutputDebugString(L"OnDemand tick\n");
          else if (wParam == IDT_REFRESH_PENDING)
            OutputDebugString(L"Pending tick\n");
          else
            OutputDebugString(L"Unknown tick\n");
          */

          // SKIF is focused -- eat my NULL and don't redraw at all!
          if (SKIF_ImGui_IsFocused ( ))
          {
            //OutputDebugString (L"Tock\n");
            PostMessage (hWnd, WM_NULL, 0x0, 0x0);
            return 1;
          }

          // If we have a refresh pending, check for a new state (when we're unfocused) -- replaced with transition logic in TestServletRunLevel
          //else if (wParam == IDT_REFRESH_PENDING) {
          //    _inject.TestServletRunlevel (true);
          //}

        //OutputDebugString(L"Tale\n");
          return 0;
      }
      break;

    case WM_SIZE:
      if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
      {
        CleanupRenderTarget ();
        g_pSwapChain->ResizeBuffers (
          0, (UINT)LOWORD (lParam),
             (UINT)HIWORD (lParam),
            DXGI_FORMAT_UNKNOWN, ((SKIF_bAllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                                                       : 0x0) |
                                      ((SKIF_bCanFlipDiscard) ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
                                                              : 0x0)
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

    case WM_CLOSE:
      // Already handled in ImGui_ImplWin32_WndProcHandler
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

LRESULT
WINAPI
SKIF_Notify_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_SKIF_NOTIFY_ICON:
      switch (lParam)
      {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
          PostMessage (SKIF_hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
          return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
          // Get current mouse position.
          POINT curPoint;
          GetCursorPos (&curPoint);

          // To display a context menu for a notification icon, the current window must be the foreground window
          // before the application calls TrackPopupMenu or TrackPopupMenuEx. Otherwise, the menu will not disappear
          // when the user clicks outside of the menu or the window that created the menu (if it is visible).
          SetForegroundWindow (hWnd);

          // TrackPopupMenu blocks the app until TrackPopupMenu returns
          TrackPopupMenu (
            hMenu,
            TPM_RIGHTBUTTON,
            curPoint.x,
            curPoint.y,
            0,
            hWnd,
            NULL
          );

          // However, when the current window is the foreground window, the second time this menu is displayed,
          // it appears and then immediately disappears. To correct this, you must force a task switch to the
          // application that called TrackPopupMenu. This is done by posting a benign message to the window or
          // thread, as shown in the following code sample:
          PostMessage (hWnd, WM_NULL, 0, 0);
          return 0;
        case NIN_BALLOONHIDE:
        case NIN_BALLOONSHOW:
        case NIN_BALLOONTIMEOUT:
        case NIN_BALLOONUSERCLICK:
        case NIN_POPUPCLOSE:
        case NIN_POPUPOPEN:
          break;
      }
      break;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case SKIF_NOTIFY_START:
          PostMessage (SKIF_hWnd, WM_SKIF_START, 0, 0);
          break;
        case SKIF_NOTIFY_STARTWITHSTOP:
          PostMessage (SKIF_hWnd, WM_SKIF_TEMPSTART, 0, 0);
          break;
        case SKIF_NOTIFY_STOP:
          PostMessage (SKIF_hWnd, WM_SKIF_STOP, 0, 0);
          break;
        case SKIF_NOTIFY_EXIT:
          PostMessage (SKIF_hWnd, WM_CLOSE, 0, 0);
          break;
      }
      break;
  }
  return
    ::DefWindowProc (hWnd, msg, wParam, lParam);
}
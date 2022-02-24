//
// Copyright 2019-2021 Andon "Kaldaien" Coleman
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

#pragma once

#include "../resource.h"

#include <combaseapi.h>
#include <comdef.h>
#include <wtype.h>

#include <string>
#include <string_view>

// This file is included mostly everywhere else, so lets define using ImGui's math operators here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

class SK_AutoCOMInit
{
public:
  SK_AutoCOMInit (DWORD dwCoInit = COINIT_MULTITHREADED) :
           init_flags_ (dwCoInit)
  {
    //if (_assert_not_dllmain ())
    {
      const HRESULT hr =
        CoInitializeEx (nullptr, init_flags_);

      if (SUCCEEDED (hr))
        success_ = true;
      else
        init_flags_ = ~init_flags_;
    }
  }

  ~SK_AutoCOMInit (void) noexcept
  {
    if (success_)
      CoUninitialize ();
  }

  bool  isInit       (void) noexcept { return success_;    }
  DWORD getInitFlags (void) noexcept { return init_flags_; }

protected:
  //static bool _assert_not_dllmain (void);

private:
  DWORD init_flags_ = COINIT_MULTITHREADED;
  bool  success_    = false;
};

extern HMODULE hModSKIF;
extern HMODULE hModSpecialK;
void SKIF_Initialize                (void);
std::wstring SKIF_GetLastError      (void);

BOOL SKIF_IsWindows8Point1OrGreater (void);
BOOL SKIF_IsWindows10OrGreater      (void);
BOOL SKIF_IsWindowsVersionOrGreater (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);

bool SKIF_ImGui_IsHoverable        (void);
void SKIF_ImGui_SetMouseCursorHand (void);
void SKIF_ImGui_SetHoverTip        (const std::string_view& szText);
void SKIF_ImGui_SetHoverText       (const std::string_view& szText, bool overrideExistingText = false);
bool SKIF_ImGui_BeginChildFrame    (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags);
void SKIF_ImGui_OptImage           (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));

void SKIF_UI_DrawPlatformStatus    (void);

void  SKIF_SetHDRWhiteLuma    (float fLuma);
FLOAT SKIF_GetHDRWhiteLuma    (void);
FLOAT SKIF_GetMaxHDRLuminance (bool bAllowLocalRange);
BOOL  SKIF_IsHDR              (void);

HINSTANCE SKIF_Util_OpenURI     (const std::wstring_view& path, DWORD dwAction = SW_SHOWNORMAL, LPCWSTR verb = L"OPEN", LPCWSTR parameters = L"");
HINSTANCE SKIF_Util_ExplorePath (const std::wstring_view& path);

HINSTANCE SKIF_Util_ExplorePath_Formatted (                const wchar_t* const wszFmt, ...);
HINSTANCE SKIF_Util_OpenURI_Formatted     (DWORD dwAction, const wchar_t* const wszFmt, ...);
void      SKIF_Util_OpenURI_Threaded      (                const LPCWSTR path);

extern float fAspect;
extern float fBottomDist;

using  CreateDXGIFactory1_pfn = HRESULT (WINAPI *)(REFIID riid, _COM_Outptr_ void **ppFactory);
extern CreateDXGIFactory1_pfn
  SKIF_CreateDXGIFactory1;

DWORD   SKIF_timeGetTime   (void);
DWORD64 SKIF_timeGetTimeEx (void);

const UINT_PTR IDT_REFRESH_ONDEMAND = 1337;
const UINT_PTR IDT_REFRESH_PENDING  = 1338;
const UINT_PTR IDT_REFRESH_DEBUG    = 1339;
const UINT_PTR IDT_REFRESH_GAMES    = 1340;

void ResolveIt  (HWND hwnd, LPCSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize);
bool CreateLink (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs = L"\0", LPCWSTR lpszWorkDir = L"\0", LPCWSTR lpszDesc = L"\0", LPCWSTR lpszIconLocation = L"\0", int iIcon = 0);

struct skif_directory_watch_s
{
  ~skif_directory_watch_s (void);

  bool isSignaled (std::wstring path);

  HANDLE hChangeNotification = INVALID_HANDLE_VALUE;
};

enum class PopupState {
  Closed,
  Open,
  Opened
};

extern PopupState ServiceMenu;

extern PopupState AddGamePopup;
extern PopupState RemoveGamePopup;
extern PopupState ModifyGamePopup;
extern PopupState ConfirmPopup;

/* values moved from main file to this header */
/*---*/
/*---*/
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#define SKIF_NOTIFY_ICON                    0x1330
#define SKIF_NOTIFY_EXIT                    0x1331
#define SKIF_NOTIFY_START                   0x1332
#define SKIF_NOTIFY_STOP                    0x1333
#define SKIF_NOTIFY_STARTWITHSTOP           0x1334
#define WM_SKIF_NOTIFY_ICON      (WM_USER + 0x150)
#define SK_BORDERLESS_EX      ( WS_EX_APPWINDOW | WS_EX_NOACTIVATE )
//#define SK_BORDERLESS_WIN8_EX ( SK_BORDERLESS_EX | WS_EX_NOREDIRECTIONBITMAP ) // We don't support Win8.0 or older
#define SK_FULLSCREEN_X(dpi) (GetSystemMetricsForDpi != nullptr) ? GetSystemMetricsForDpi (SM_CXFULLSCREEN, (dpi)) : GetSystemMetrics (SM_CXFULLSCREEN)
#define SK_FULLSCREEN_Y(dpi) (GetSystemMetricsForDpi != nullptr) ? GetSystemMetricsForDpi (SM_CYFULLSCREEN, (dpi)) : GetSystemMetrics (SM_CYFULLSCREEN)
#define GCL_HICON           (-14)
#define SK_BORDERLESS ( WS_VISIBLE | WS_POPUP | WS_MINIMIZEBOX | \
                        WS_SYSMENU )
#define DIRECTINPUT_VERSION 0x0800
// Fixed-width font
#define SK_IMGUI_FIXED_FONT 1



#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib, "wininet.lib")


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
bool SKIF_UpdateReady = false;
bool showUpdatePrompt = false;
bool changedUpdateChannel = false;

int  SKIF_iNotifications           = 2, // 0 = Never,       1 = Always,       2 = When unfocused
     SKIF_iGhostVisibility         = 0, // 0 = Never,       1 = Always,       2 = While service is running
     SKIF_iStyle                   = 0, // 0 = SKIF Dark,   1 = ImGui Dark,   2 = ImGui Light,               3 = ImGui Classic
     SKIF_iDimCovers               = 0, // 0 = Never,       1 = Always,       2 = On mouse hover
     SKIF_iCheckForUpdates         = 1, // 0 = Never,       1 = Weekly,       2 = On each launch
     SKIF_iUpdateChannel           = 1; // 0 = Discord,     1 = Website,      2 = Ancient

uint32_t
     SKIF_iLastSelected            = SKIF_STEAM_APPID;

bool SKIF_bRememberLastSelected    = false,
     SKIF_bDisableDPIScaling       = false,
     SKIF_bDisableExitConfirmation =  true,
     SKIF_bDisableTooltips         = false,
     SKIF_bDisableStatusBar        = false,
     SKIF_bDisableBorders          =  true, // default to true
     SKIF_bDisableSteamLibrary     = false,
     SKIF_bDisableEGSLibrary       = false,
     SKIF_bDisableGOGLibrary       = false,
     SKIF_bSmallMode               = false,
     SKIF_bFirstLaunch             = false,
     SKIF_bEnableDebugMode         = false,
     SKIF_bAllowMultipleInstances  = false,
     SKIF_bAllowBackgroundService  = false,
     SKIF_bEnableHDR               = false,
     SKIF_bOpenAtCursorPosition    = false,
     SKIF_bStopOnInjection         = false,
     SKIF_bCloseToTray             = false,
     SKIF_bFontChineseSimplified   = false,
     SKIF_bFontChineseAll          = false,
     SKIF_bFontCyrillic            = false,
     SKIF_bFontJapanese            = false,
     SKIF_bFontKorean              = false,
     SKIF_bFontThai                = false,
     SKIF_bFontVietnamese          = false,
     SKIF_bLowBandwidthMode        = false,
     SKIF_bPreferGOGGalaxyLaunch   = false;

std::wstring 
     SKIF_wsIgnoreUpdate,
     SKIF_wsUpdateChannel;

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

// Texture related locks to prevent driver crashes
concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;

std::filesystem::path orgWorkingDirectory;

PopupState UpdatePromptPopup    = PopupState::Closed;
HMODULE hModSKIF     = nullptr;
HMODULE hModSpecialK = nullptr;

using  GetSystemMetricsForDpi_pfn = int (WINAPI *)(int, UINT);
static GetSystemMetricsForDpi_pfn
       GetSystemMetricsForDpi = nullptr;

float fAspect     = 16.0f / 9.0f;
float fBottomDist = 0.0f;

std::vector <std::pair<std::string, std::string>> updateChannels{};
static volatile LONG update_thread = 0;
struct SKIF_UpdateCheckResults {
  std::wstring version;
  std::wstring filename;
  std::wstring description;
  std::wstring releasenotes;
};

bool bKeepWindowAlive  = true,
     bKeepProcessAlive = true;

ID3D11Device*           g_pd3dDevice           = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
IDXGISwapChain*         g_pSwapChain           = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
BOOL                    bOccluded              =   FALSE;

using CreateDXGIFactory1_pfn            = HRESULT (WINAPI *)(REFIID riid, _COM_Outptr_ void **ppFactory);
using D3D11CreateDeviceAndSwapChain_pfn = HRESULT (WINAPI *)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                  CONST D3D_FEATURE_LEVEL*,                     UINT, UINT,
                                                  CONST DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                                                               ID3D11Device**,
                                                        D3D_FEATURE_LEVEL*,    ID3D11DeviceContext**);

CreateDXGIFactory1_pfn            SKIF_CreateDXGIFactory1;
D3D11CreateDeviceAndSwapChain_pfn SKIF_D3D11CreateDeviceAndSwapChain;

struct skif_get_web_uri_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH] = { };
  wchar_t wszLocalPath[MAX_PATH + 2] = { };
};

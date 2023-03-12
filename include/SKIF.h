//
// Copyright 2019-2022 Andon "Kaldaien" Coleman
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
#include <atlbase.h>

#include <string>
#include <string_view>

#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"

#include <injection.h>
#include <registry.h>

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

struct SKIF_UpdateCheckResults {
  std::wstring version;
  std::wstring filename;
  std::wstring description;
  std::wstring releasenotes;
  std::string  history;
} extern;

extern HMODULE hModSKIF;
extern HMODULE hModSpecialK;
std::string SKIF_GetPatrons        (void);
void        SKIF_Initialize        (void);

extern float SKIF_ImGui_GlobalDPIScale;
extern float SKIF_ImGui_GlobalDPIScale_Last;

extern std::string SKIF_StatusBarText;
extern std::string SKIF_StatusBarHelp;
extern HWND        SKIF_hWnd;
extern HWND        SKIF_Notify_hWnd;

extern bool RepopulateGames;
extern bool RefreshMPOSupport;

void SKIF_UI_DrawPlatformStatus    (void);
void SKIF_UI_DrawComponentVersion  (void);

void  SKIF_SetHDRWhiteLuma    (float fLuma);
FLOAT SKIF_GetHDRWhiteLuma    (void);
FLOAT SKIF_GetMaxHDRLuminance (bool bAllowLocalRange);
BOOL  SKIF_IsHDR              (void);

extern float fAspect;
extern float fBottomDist;

using  CreateDXGIFactory1_pfn = HRESULT (WINAPI *)(REFIID riid, _COM_Outptr_ void **ppFactory);
extern CreateDXGIFactory1_pfn
  SKIF_CreateDXGIFactory1;

const UINT_PTR IDT_REFRESH_ONDEMAND = 1337;
const UINT_PTR IDT_REFRESH_PENDING  = 1338;
const UINT_PTR IDT_REFRESH_DEBUG    = 1339;
const UINT_PTR IDT_REFRESH_GAMES    = 1340;

constexpr UINT WM_SKIF_START         = WM_USER + 0x1024;
constexpr UINT WM_SKIF_TEMPSTART     = WM_USER + 0x1025;
constexpr UINT WM_SKIF_LAUNCHER      = WM_USER + 0x1026;
constexpr UINT WM_SKIF_REFRESHGAMES  = WM_USER + 0x1027;
constexpr UINT WM_SKIF_STOP          = WM_USER + 0x2048;
constexpr UINT WM_SKIF_RESTORE       = WM_USER +  0x513;
constexpr UINT WM_SKIF_MINIMIZE      = WM_USER +  0x512;

constexpr wchar_t* SKIF_WindowClass =
             L"SK_Injection_Frontend";

// Settings
extern int SKIF_iNotifications;
extern int SKIF_iGhostVisibility;
extern int SKIF_iStyle;
extern int SKIF_iDimCovers;
extern int SKIF_iCheckForUpdates;
extern int SKIF_iAutoStopBehavior;
extern int SKIF_iLogging;
extern int SKIF_iProcessSort;

extern uint32_t SKIF_iLastSelected;
extern bool SKIF_bRememberLastSelected;
extern bool SKIF_bDisableDPIScaling;
extern bool SKIF_bDisableTooltips;
extern bool SKIF_bDisableStatusBar;
extern bool SKIF_bDisableBorders;
extern bool SKIF_bDisableSteamLibrary;
extern bool SKIF_bDisableEGSLibrary;
extern bool SKIF_bDisableGOGLibrary;
extern bool SKIF_bDisableXboxLibrary;
extern bool SKIF_bSmallMode;
extern bool SKIF_bFirstLaunch;
extern bool SKIF_bEnableDebugMode;
extern bool SKIF_bAllowMultipleInstances;
extern bool SKIF_bAllowBackgroundService;
extern bool SKIF_bEnableHDR;
extern bool SKIF_bDisableVSYNC;
extern bool SKIF_bOpenAtCursorPosition;
extern bool SKIF_bStopOnInjection;
extern bool SKIF_bCloseToTray;
extern bool SKIF_bLowBandwidthMode;
extern bool SKIF_bPreferGOGGalaxyLaunch;
extern bool SKIF_bMinimizeOnGameLaunch;
extern bool SKIF_bProcessSortAscending;
extern bool SKIF_bProcessIncludeAll;

// Not a saved setting, this indicates the DXGI runtime supports tearing
extern BOOL SKIF_bAllowTearing;

// This is used in conjunction with SKIF_bMinimizeOnGameLaunch to suppress the "Please start game" notification
extern BOOL SKIF_bSuppressServiceNotification;


enum class PopupState {
  Closed,
  Open,
  Opened
};

extern PopupState IconMenu;
extern PopupState ServiceMenu;

extern PopupState AddGamePopup;
extern PopupState RemoveGamePopup;
extern PopupState ModifyGamePopup;
extern PopupState ConfirmPopup;

enum UITab {
  None,
  Library,
  Monitor,
  Settings,
  About,
};

extern UITab SKIF_Tab_Selected;
extern UITab SKIF_Tab_ChangeTo;

// Call SetEvent (...) on this to force a redraw, even if window is not focused
extern CHandle SKIF_RefreshEvent;
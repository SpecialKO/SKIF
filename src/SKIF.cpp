﻿//
// Copyright 2020 - 2024 Andon "Kaldaien" Coleman
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

#include "../resource.h"
#include "../version.h"

#include <strsafe.h>
#include <cwctype>
#include <dxgi1_5.h>

#include <MinHook.h>
#include <SKIF.h>

// Plog ini includes (must be included after SKIF.h)
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"
#include "plog/Appenders/DebugOutputAppender.h"
#include <utility/plog_formatter.h>

#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <utility/gamepad.h>

#include <utility/injection.h>

#include <fonts/fa_621.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include "imgui/imgui_impl_dx11.h"
#include <imgui/imgui_internal.h>
#include <xinput.h>

#include <utility/fsutil.h>

#include <filesystem>
#include <concurrent_queue.h>
#include <oleidl.h>
#include <utility/droptarget.hpp>

#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800

#include <stores/Steam/app_record.h>

#include <tabs/about.h>
#include <tabs/settings.h>

#include <utility/registry.h>
#include <utility/updater.h>

#include <utility/drvreset.h>
#include <tabs/common_ui.h>
#include <Dbt.h>

// Header Files for Jump List features
#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <netlistmgr.h>
#include <dwmapi.h>

const int SKIF_STEAM_APPID      = 1157970;
bool  RecreateSwapChains        = false;
bool  RecreateSwapChainsPending = false;
bool  RecreateWin32Windows      = false;
bool  RepositionSKIF            = false;
bool  RespectMonBoundaries      = false;
bool  changedHiDPIScaling       = false;
bool  invalidateFonts           = false;
bool  failedLoadFonts           = false;
bool  failedLoadFontsPrompt     = false;
DWORD invalidatedFonts          = 0;
DWORD invalidatedDevice         = 0;
bool  startedMinimized          = false;
bool  msgDontRedraw             = false;
bool  coverFadeActive           = false;
std::atomic<bool> SKIF_Shutdown = false;
bool  SKIF_NoInternet           = false;
int   SKIF_ExitCode             = 0;
int   SKIF_nCmdShow             = -1;
std::atomic<int>  SKIF_FrameCount = 0;
int   addAdditionalFrames       = 0;
DWORD dwDwmPeriod               = 62500; // Assume 60 Hz (16 ms) by default
bool  SteamOverlayDisabled      = false;
bool  allowShortcutCtrlA        = true; // Used to disable the Ctrl+A when interacting with text input
bool  SKIF_MouseDragMoveAllowed = true;
bool  SKIF_debuggerPresent      = false;
DWORD SKIF_startupTime          = 0; // Used as a basis of how long the initialization took
DWORD SKIF_firstFrameTime       = 0; // Used as a basis of how long the initialization took
HANDLE SteamProcessHandle       = NULL;

// Shell messages (registered window messages)
UINT SHELL_TASKBAR_RESTART        = 0; // TaskbarCreated
UINT SHELL_TASKBAR_BUTTON_CREATED = 0; // TaskbarButtonCreated

// A fixed size for the application window fixes the wobble that otherwise
//   occurs when switching between tabs as the size isn't dynamically calculated.

// --- App Mode (regular)
ImVec2 SKIF_vecRegularMode          = ImVec2 (0.0f, 0.0f);        // DPI-aware
ImVec2 SKIF_vecRegularModeDefault   = ImVec2 (1000.0f, 944.0f);   // Does not include the status bar // SKIF_fStatusBarHeight == 31.0f
ImVec2 SKIF_vecRegularModeAdjusted  = SKIF_vecRegularModeDefault; // Adjusted for status bar and tooltips (NO DPI scaling!)
// --- Service Mode
ImVec2 SKIF_vecServiceMode          = ImVec2 (0.0f, 0.0f);        // DPI-aware
ImVec2 SKIF_vecServiceModeDefault   = ImVec2 (415.0f, 305.0f);    // TODO 2024-05-05: 415px should probably be raised to 435px to allow smooth window resizing with a matched style between regular + service
// --- Horizontal Mode (used when regular mode is not available)
ImVec2 SKIF_vecHorizonMode          = ImVec2 (0.0f, 0.0f);        // DPI-aware
ImVec2 SKIF_vecHorizonModeDefault   = ImVec2 (1000.0f, 374.0f);   // Does not include the status bar (2024-01-20: 325 -> 375; 2024-05-19: 375 -> 374 to fix cover scaling at default size)
ImVec2 SKIF_vecHorizonModeAdjusted  = SKIF_vecHorizonModeDefault; // Adjusted for status bar and tooltips (NO DPI scaling!)
// --- Variables
ImVec2 SKIF_vecCurrentPosition      = ImVec2 (0.0f, 0.0f); // Gets updated after ImGui::EndFrame()
ImVec2 SKIF_vecCurrentMode          = ImVec2 (0.0f, 0.0f); // Gets updated after ImGui::EndFrame()
ImVec2 SKIF_vecCurrentModeNext      = ImVec2 (0.0f, 0.0f); // Holds the new expected size
float  SKIF_fStatusBarHeight        = 31.0f; // Status bar enabled // 33 ?
float  SKIF_fStatusBarDisabled      = 8.0f;  // Status bar disabled
float  SKIF_fStatusBarHeightTips    = 18.0f; // Disabled tooltips (two-line status bar)

// Custom Global Key States used for moving SKIF around using WinKey + Arrows
bool KeyWinKey = false;
int  SnapKeys  = 0;     // 2 = Left, 4 = Up, 8 = Right, 16 = Down

// Holds swapchain wait handles
std::vector<HANDLE> vSwapchainWaitHandles;

// GOG Galaxy stuff
std::wstring GOGGalaxy_Path        = L"";
std::wstring GOGGalaxy_Folder      = L"";
std::wstring GOGGalaxy_UserID      = L"";
bool         GOGGalaxy_Installed   = false;

DWORD    RepopulateGamesWasSet     = 0;
bool     RepopulateGames           = false,
         RefreshSettingsTab        = false;
uint32_t SelectNewSKIFGame         = 0;

bool  HoverTipActive               = false;
DWORD HoverTipDuration             = 0;

// Notification icon stuff
static const GUID SKIF_NOTIFY_GUID = // {8142287D-5BC6-4131-95CD-709A2613E1F5}
{ 0x8142287d, 0x5bc6, 0x4131, { 0x95, 0xcd, 0x70, 0x9a, 0x26, 0x13, 0xe1, 0xf5 } };
#define SKIF_NOTIFY_ICON                    0x1330 // 4912
#define SKIF_NOTIFY_EXIT                    0x1331 // 4913
#define SKIF_NOTIFY_START                   0x1332 // 4914
#define SKIF_NOTIFY_STOP                    0x1333 // 4915
#define SKIF_NOTIFY_STARTWITHSTOP           0x1334 // 4916
#define SKIF_NOTIFY_RUN_UPDATER             0x1335 // 4917
#define WM_SKIF_NOTIFY_ICON      (WM_USER + 0x150) // 1360
bool SKIF_isTrayed = false;
NOTIFYICONDATA niData;
HMENU hMenu;

// Cmd line argument stuff
SKIF_Signals _Signal;

PopupState UpdatePromptPopup = PopupState_Closed;
PopupState HistoryPopup      = PopupState_Closed;
PopupState AutoUpdatePopup   = PopupState_Closed;
UITab SKIF_Tab_Selected      = UITab_Library,
      SKIF_Tab_ChangeTo      = UITab_None;

// Variables related to the display SKIF is visible on
ImVec2  windowPos;
ImRect  windowRect       = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
ImRect  monitor_extent   = ImRect(0.0f, 0.0f, 0.0f, 0.0f);

HMODULE hModSKIF     = nullptr;
HMODULE hModSpecialK = nullptr;
HICON   hIcon        = nullptr;
#define GCL_HICON      (-14)

// Texture related locks to prevent driver crashes
concurrency::concurrent_queue <IUnknown*> SKIF_ResourcesToFree; // CComPtr <IUnknown>

float fBottomDist = 0.0f;

ID3D11Device*           SKIF_pd3dDevice           = nullptr;
ID3D11DeviceContext*    SKIF_pd3dDeviceContext    = nullptr;
//ID3D11RenderTargetView* SKIF_g_mainRenderTargetView = nullptr;

// Forward declarations
bool                CreateDeviceD3D                           (HWND hWnd);
void                CleanupDeviceD3D                          (void);
LRESULT WINAPI      SKIF_WndProc                              (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI      SKIF_Notify_WndProc                       (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void                SKIF_Initialize                           (LPWSTR lpCmdLine);

CHandle hInjectAck       (0); // Signalled when injection service should be stopped
CHandle hInjectAckEx     (0); // Signalled when a successful injection occurs (minimizes SKIF)
CHandle hInjectExitAckEx (0); // Signalled when an injected game exits (restores SKIF)

// Holds current global DPI scaling, 1.0f == 100%, 1.5f == 150%.
float SKIF_ImGui_GlobalDPIScale      = 1.0f;
// Holds last frame's DPI scaling
float SKIF_ImGui_GlobalDPIScale_Last = 1.0f; // Always identical to SKIF_ImGui_GlobalDPIScale within ImGui::NewFrame()
//float SKIF_ImGui_GlobalDPIScale_New  = 1.0f;
float SKIF_ImGui_FontSizeDefault     = 18.0f; // 18.0F

std::string SKIF_StatusBarText = "";
std::string SKIF_StatusBarHelp = "";
HWND        SKIF_ImGui_hWnd    = NULL;
HWND        SKIF_Notify_hWnd   = NULL;

HWND  hWndOrigForeground;
HWND  hWndForegroundFocusOnExit = nullptr; // Game HWND as reported by Special K through WM_SKIF_EVENT_SIGNAL
DWORD pidForegroundFocusOnExit  = NULL;    // Used to hold the game process ID that SKIF launched

void CALLBACK
SKIF_EfficiencyModeTimerProc (HWND hWnd, UINT Msg, UINT wParamIDEvent, DWORD dwTime)
{
  UNREFERENCED_PARAMETER (Msg);
  UNREFERENCED_PARAMETER (wParamIDEvent);
  UNREFERENCED_PARAMETER (dwTime);

  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  KillTimer (hWnd, cIDT_TIMER_EFFICIENCY);
  
  if (_registry.bEfficiencyMode && ! _registry._EfficiencyMode && ! SKIF_ImGui_IsFocused ( ))
  {
    _registry._EfficiencyMode = true;
    msgDontRedraw = true;

    PLOG_DEBUG << "Engaging efficiency mode";

    // Enable Efficiency Mode in Windows 11 (requires idle (low) priority + EcoQoS)
    SKIF_Util_SetProcessPowerThrottling (SKIF_Util_GetCurrentProcess(), 1);
    SetPriorityClass (SKIF_Util_GetCurrentProcess(), IDLE_PRIORITY_CLASS );
  }
}

void
SKIF_Startup_SetGameAsForeground (void)
{
  // Exit if there is nothing to actually do
  if (pidForegroundFocusOnExit  == NULL ||
      hWndForegroundFocusOnExit == nullptr)
    return;

  DWORD _pid = 0;

  if (hWndForegroundFocusOnExit != nullptr &&
      hWndForegroundFocusOnExit == GetForegroundWindow ())
  {
    // This is a nop, bail-out before screwing things up even more
    PLOG_VERBOSE << "hWndForegroundFocusOnExit is the foreground window already";
    return;
  }

  if (GetWindowThreadProcessId (GetForegroundWindow (), &_pid))
  {
    if (_pid == pidForegroundFocusOnExit)
    {
      // This is a nop, bail-out before screwing things up even more
      PLOG_VERBOSE << "pidForegroundFocusOnExit is the foreground window already";
      return;
    }
  }

  if (SKIF_ImGui_hWnd != NULL && 
      SKIF_ImGui_hWnd == GetForegroundWindow ( ))
    PLOG_VERBOSE << "SKIF_ImGui_hWnd is the foreground window";
  
  if (SKIF_Notify_hWnd != NULL && 
      SKIF_Notify_hWnd == GetForegroundWindow ( ))
    PLOG_VERBOSE << "SKIF_Notify_hWnd is the foreground window";

  PLOG_INFO << "Attempting to find game window to set as foreground...";

  // Primary approach -- use the HWND reported by Special K
  if (hWndForegroundFocusOnExit != nullptr)
  {
    if (GetWindowThreadProcessId (hWndForegroundFocusOnExit,  &_pid) &&
                                   pidForegroundFocusOnExit == _pid)
    {
      if (IsWindowVisible (hWndForegroundFocusOnExit))
      {
        PLOG_INFO << "Special K reported a game window, setting as foreground...";
        if (! SetForegroundWindow (hWndForegroundFocusOnExit))
          PLOG_WARNING << "SetForegroundWindow ( ) failed!";
      }

      return;
    }
  }

  // Fallback approach -- attempt to find a window belonging to the process
  EnumWindows ( []( HWND   hWnd,
                    LPARAM lParam ) -> BOOL
  {
    DWORD                                _pid = 0;
    if (GetWindowThreadProcessId (hWnd, &_pid))
    {
      if (_pid != NULL && 
          _pid == (DWORD)lParam)
      {
        PLOG_INFO << "Found game window, setting as foreground...";

        // Try to make this awful thing more reliable, by narrowing down the candidates to
        //   windows that show up in the taskbar when they're activated.
        LONG_PTR dwExStyle =
          GetWindowLongPtrW (hWnd, GWL_EXSTYLE);

        // If it doesn't have this style, then don't set it foreground.
        if (dwExStyle & WS_EX_APPWINDOW)
        {
          if (IsWindowVisible (hWnd))
          {
            if (! SetForegroundWindow (hWnd))
              PLOG_WARNING << "SetForegroundWindow ( ) failed!";
          }

          return FALSE; // Stop enumeration
        }
      }
    }
    return TRUE;
  }, (LPARAM)pidForegroundFocusOnExit);
}

void
SKIF_Startup_ProcessCmdLineArgs (LPWSTR lpCmdLine)
{
  _Signal.Start =
    StrStrIW (lpCmdLine, L"Start")       != NULL;

  _Signal.Temporary =
    StrStrIW (lpCmdLine, L"Temp")        != NULL;

  _Signal.Stop =
    StrStrIW (lpCmdLine, L"Stop")        != NULL;

  _Signal.Quit =
    StrStrIW (lpCmdLine, L"Quit")        != NULL;

  _Signal.Minimize =
    StrStrIW (lpCmdLine, L"Minimize")    != NULL;

  _Signal.AddSKIFGame =
    StrStrIW (lpCmdLine, L"AddGame=")    != NULL;

  _Signal.LauncherURI =
    StrStrIW (lpCmdLine, L"SKIF_URI=")   != NULL;

  _Signal.CheckForUpdates =
    StrStrIW (lpCmdLine, L"RunUpdater")  != NULL;

  _Signal.ServiceMode =
    StrStrIW (lpCmdLine, L"ServiceMode") != NULL;

  // Both AddSKIFGame, SKIF_URI, and Launcher can include .exe in
  //   the argument so only set Launcher if the others are false.
  if (! _Signal.AddSKIFGame &&
      ! _Signal.LauncherURI)
        _Signal.Launcher =
      StrStrIW (lpCmdLine, L".exe")     != NULL;

  // Check if we are dealing with a .bat target
  if (! _Signal.Launcher &&
      StrStrIW (lpCmdLine, L".bat")     != NULL)
        _Signal.Launcher    =
        _Signal.LauncherBAT = true;

  _Signal._RunningInstance =
    FindWindowExW (0, 0, SKIF_NotifyIcoClass, nullptr);
}

void
SKIF_Startup_AddGame (LPWSTR lpCmdLine)
{
  if (! _Signal.AddSKIFGame)
    return;

  PLOG_INFO << "Adding custom game to SKIF...";

  // O:\WindowsApps\DevolverDigital.MyFriendPedroWin10_1.0.6.0_x64__6kzv4j18v0c96\MyFriendPedro.exe

  std::wstring cmdLine        = std::wstring(lpCmdLine);
  std::wstring cmdLineArgs    = cmdLine;

  // Transform to lowercase
  std::wstring cmdLineLower   = SKIF_Util_ToLowerW (cmdLine);

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
  cmdLineLower   = SKIF_Util_ToLowerW (cmdLine);

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
      
    WCHAR wszTarget   [MAX_PATH + 2] = { };
    WCHAR wszArguments[MAX_PATH + 2] = { };

    SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, cmdLine.c_str(), wszTarget, wszArguments, MAX_PATH * sizeof (WCHAR));

    cmdLine     = std::wstring(wszTarget);
    cmdLineArgs = std::wstring(wszArguments);
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

    extern int          SKIF_AddCustomAppID         (std::wstring name, std::wstring path, std::wstring args);

    if (PathFileExists (cmdLine.c_str()))
    {
      std::wstring productName = SKIF_Util_GetProductName (cmdLine.c_str());

      if (productName == L"")
        productName = std::filesystem::path (cmdLine).replace_extension().filename().wstring();

      SelectNewSKIFGame = (uint32_t)SKIF_AddCustomAppID (productName, cmdLine, cmdLineArgs);
    
      // If a running instance of SKIF already exists, terminate this one as it has served its purpose
      if (SelectNewSKIFGame > 0 && _Signal._RunningInstance != 0)
      {
        SendMessage (_Signal._RunningInstance, WM_SKIF_REFRESHGAMES, SelectNewSKIFGame, 0x0);
        PLOG_INFO << "Terminating due to one of these contions were found to be true:";
        PLOG_INFO << "SelectNewSKIFGame > 0: "  << (SelectNewSKIFGame  > 0);
        PLOG_INFO << "hwndAlreadyExists != 0: " << (_Signal._RunningInstance != 0);
        ExitProcess (0x0);
      }
    }

    else {
      PLOG_ERROR << "Non-valid path detected: " << cmdLine;
    }
  }

  else {
    PLOG_ERROR << "Non-valid string detected: " << lpCmdLine;
  }
}

void
SKIF_Startup_LaunchGamePreparation (LPWSTR lpCmdLine)
{
  if (! _Signal.Launcher)
    return;

  PLOG_INFO << "Preparing game path, launch options, and working directory...";
  
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  std::wstring cmdLine        = std::wstring(lpCmdLine);
  std::wstring delimiter      = (_Signal.LauncherBAT) ? L".bat" : L".exe"; // split lpCmdLine at the .bat/.exe

  // First position is a quotation mark -- we need to strip those
  if (cmdLine.find(L"\"") == 0)
    cmdLine = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1) + cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos);

  // Transform to lowercase
  std::wstring cmdLineLower = SKIF_Util_ToLowerW (cmdLine);

  // Extract the SKIF_SteamAppID cmd line argument
  const std::wstring argSKIF_SteamAppID = L"skif_steamappid=";
  size_t posSKIF_SteamAppID_start       = cmdLineLower.find (argSKIF_SteamAppID);
  std::wstring steamAppId               = L"";

  if (posSKIF_SteamAppID_start != std::wstring::npos)
  {
    size_t
      posSKIF_SteamAppID_end    = cmdLineLower.find (L" ", posSKIF_SteamAppID_start);

    if (posSKIF_SteamAppID_end == std::wstring::npos)
      posSKIF_SteamAppID_end    = cmdLineLower.length ( );

    // Length of the substring to remove
    posSKIF_SteamAppID_end -= posSKIF_SteamAppID_start;

    steamAppId = cmdLineLower.substr(posSKIF_SteamAppID_start + argSKIF_SteamAppID.length ( ), posSKIF_SteamAppID_end);

    // Remove substring from the original variables
    cmdLine     .erase (posSKIF_SteamAppID_start, posSKIF_SteamAppID_end);
    cmdLineLower.erase (posSKIF_SteamAppID_start, posSKIF_SteamAppID_end);
  }

  // Extract the target path and any proxied command line arguments
  std::wstring path           = cmdLine.substr(0, cmdLineLower.find(delimiter) + delimiter.length());                        // path
  std::wstring proxiedCmdLine = cmdLine.substr(   cmdLineLower.find(delimiter) + delimiter.length(), cmdLineLower.length()); // proxied command line

  SKIF_Util_TrimSpacesW (proxiedCmdLine);

  std::wstring workingDirectory = _path_cache.skif_workdir_org;
  
  // Fall back to using the folder of the game executable if the original working directory fails a few simple checks
  if (workingDirectory.empty() || _wcsicmp (_path_cache.skif_workdir_org, _path_cache.skif_workdir) == 0 || workingDirectory.find(L"system32") != std::wstring::npos)
    workingDirectory = std::filesystem::path(path).parent_path().wstring();

  // Path does not seem to be absolute -- add the current working directory in front of the path
  if (path.find(L"\\") == std::wstring::npos)
    path = SK_FormatStringW (LR"(%ws\%ws)", workingDirectory.c_str(), path.c_str()); //orgWorkingDirectory.wstring() + L"\\" + path;

  PLOG_INFO                               << "Executable:        " << path;
  PLOG_INFO_IF (! proxiedCmdLine.empty()) << "Command Line Args: " << proxiedCmdLine;
  PLOG_INFO                               << "Working Directory: " << workingDirectory;

  bool isLocalBlacklisted  = false,
       isGlobalBlacklisted = false;

  if (PathFileExists (path.c_str()))
  {
    _Signal._GamePath        = path;
    _Signal._GameArgs        = proxiedCmdLine;
    _Signal._GameWorkDir     = workingDirectory;
    if (! steamAppId.empty())
      _Signal._SteamAppID    = steamAppId;

    std::wstring blacklistFile = SK_FormatStringW (L"%s\\SpecialK.deny.%ws",
                                                    std::filesystem::path(path).parent_path().wstring().c_str(),                 // full path to parent folder
                                                    std::filesystem::path(path).filename().replace_extension().wstring().c_str() // filename without extension
    );

    // Check if the executable is blacklisted
    isLocalBlacklisted  = PathFileExistsW (blacklistFile.c_str());
    isGlobalBlacklisted = _inject._TestUserList (SK_WideCharToUTF8(path).c_str(), false);

    _Signal._DoNotUseService = (isLocalBlacklisted || isGlobalBlacklisted);

    if (! _Signal._DoNotUseService)
    {
      // Whitelist the path if it haven't been already
      _inject.WhitelistPath (SK_WideCharToUTF8(path));

      std::wstring elevationFile = SK_FormatStringW (L"%s\\SpecialK.admin.%ws",
                                                      std::filesystem::path(path).parent_path().wstring().c_str(),                 // full path to parent folder
                                                      std::filesystem::path(path).filename().replace_extension().wstring().c_str() // filename without extension
      );

      _Signal._ElevatedService = PathFileExists (elevationFile.c_str());
    }
  }

  else {
    PLOG_ERROR << "Non-valid path detected: " << path;

    ExitProcess (0x0);
  }
}

void
SKIF_Startup_LaunchURIPreparation (LPWSTR lpCmdLine)
{
  if (! _Signal.LauncherURI)
    return;

  PLOG_INFO << "Preparing the shell execute call...";

  if (! _Signal.Start)
    _Signal._DoNotUseService = true;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  //static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  std::wstring cmdLine      = std::wstring     (lpCmdLine);
  std::wstring cmdLineLower = SKIF_Util_ToLowerW (cmdLine);

  const std::wstring argSKIF_URI = L"skif_uri=";
  std::wstring argSKIF_URI_found = L"";
  size_t posArgumentStart        = cmdLineLower.find (argSKIF_URI);
  
  // Extract the SKIF_XXX cmd line argument
  if (posArgumentStart != std::wstring::npos)
  {
    size_t
      posArgumentEnd = cmdLineLower.find (L" ", posArgumentStart);

    if (posArgumentEnd == std::wstring::npos)
      posArgumentEnd = cmdLineLower.length ( );

    // Length of the substring to remove
    posArgumentEnd -= posArgumentStart;

    argSKIF_URI_found = cmdLine.substr(posArgumentStart + argSKIF_URI.length ( ), posArgumentEnd);

    // Remove substring from the original variables
    cmdLine     .erase (posArgumentStart, posArgumentEnd);
    cmdLineLower.erase (posArgumentStart, posArgumentEnd);
  }

  if (! argSKIF_URI_found.empty())
  {
    PLOG_INFO << "URI: " << argSKIF_URI_found;

    _Signal._GamePath = argSKIF_URI_found;

    // If we are dealing with an executable path, also find a working directory
    if (StrStrIW (argSKIF_URI_found.c_str(), L".exe") != NULL)
    {
      std::wstring workingDirectory = _path_cache.skif_workdir_org;
  
      // Fall back to using the folder of the game executable if the original working directory fails a few simple checks
      if (workingDirectory.empty() || _wcsicmp (_path_cache.skif_workdir_org, _path_cache.skif_workdir) == 0 || workingDirectory.find(L"system32") != std::wstring::npos)
        workingDirectory = std::filesystem::path (argSKIF_URI_found).parent_path().wstring();

      _Signal._GameWorkDir = workingDirectory;
    }
  }

  else {
    PLOG_ERROR << "Non-valid URI detected: " << cmdLine;

    ExitProcess (0x0);
  }
}

void
SKIF_Startup_LaunchGameService (void)
{
  if (_Signal._GamePath.empty())
    return;
  
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  if (_Signal._DoNotUseService)
  {
    // 2023-11-14: I am unsure how effective _inject.bCurrentState is here...
    //             Is it even accurate at this point in time? // Aemony
    if (_Signal._RunningInstance && (_inject.bCurrentState || _Signal.LauncherURI))
    {
      PLOG_INFO << "Stopping injection service...";
      SendMessage (_Signal._RunningInstance, WM_SKIF_STOP, 0x0, 0x0);
    }
  }

  else {
    PLOG_INFO << "Suppressing the initial 'Please launch a game to continue' notification...";
    _registry._SuppressServiceNotification = true;

    PLOG_INFO << "Starting injection service...";

    if (_Signal._RunningInstance)
      SendMessage (_Signal._RunningInstance, WM_SKIF_LAUNCHER, _Signal._ElevatedService, 0x0);

    else if (! _inject.bCurrentState)
    {

      _registry._ExitOnInjection = true;
      _inject._StartStopInject (false, true, _Signal._ElevatedService);
    }
  }
}


void
SKIF_Startup_LaunchGame (void)
{
  if (_Signal._GamePath.empty())
    return;
      
  PLOG_INFO                                    << "Launching executable : " << _Signal._GamePath;
  PLOG_INFO_IF(! _Signal._GameWorkDir.empty()) << "   Working directory : " << _Signal._GameWorkDir;
  PLOG_INFO_IF(! _Signal._GameArgs   .empty()) << "           Arguments : " << _Signal._GameArgs;

  if (! _Signal._SteamAppID.empty ( ))
  {
    PLOG_INFO << "        Steam App ID : " << _Signal._SteamAppID;
    SetEnvironmentVariable (L"SteamAppId",    _Signal._SteamAppID.c_str());
    SetEnvironmentVariable (L"SteamGameId",   _Signal._SteamAppID.c_str());
  }

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = _Signal._GamePath.c_str();
    sexi.lpParameters = (! _Signal._GameArgs.empty()) ? _Signal._GameArgs.c_str() : NULL;
    sexi.lpDirectory  = _Signal._GameWorkDir.c_str();
    sexi.nShow        = SW_SHOWNORMAL;
    sexi.fMask        = SEE_MASK_NOCLOSEPROCESS | // We need the PID of the process that gets started
                        SEE_MASK_NOASYNC        | // Never async since our own process might stop executing before the new process is ready
                        SEE_MASK_NOZONECHECKS;    // No zone check needs to be performed

  // Launch executable
  ShellExecuteExW (&sexi);

  if (! _Signal._SteamAppID.empty ( ))
  {
    SetEnvironmentVariable (L"SteamAppId",  NULL);
    SetEnvironmentVariable (L"SteamGameId", NULL);
  }

  // Set the new process as foreground window
  if (sexi.hInstApp  > (HINSTANCE)32 &&
      sexi.hProcess != NULL)
  {
    pidForegroundFocusOnExit = GetProcessId (sexi.hProcess);
    CloseHandle (sexi.hProcess);
  }

  // If a running instance of SKIF already exists, or the game was blacklisted, terminate this one as it has served its purpose
  if (_Signal._RunningInstance || _Signal._DoNotUseService)
  {
    SKIF_Startup_SetGameAsForeground ( );
    PLOG_INFO << "Terminating as this instance has fulfilled its purpose.";

    ExitProcess (0x0);
  }
}

void
SKIF_Startup_ProxyCommandLineArguments (void)
{
  if (! _Signal._RunningInstance)
    return;

  if (! _Signal.Start            && 
      ! _Signal.Stop             &&
      ! _Signal.Minimize         &&
      ! _Signal.CheckForUpdates  &&
      ! _Signal.Quit)
    return;

  PLOG_INFO << "Proxying command line arguments...";

  if (_Signal.Start)
  {
    // This means we proxied this cmd to ourselves, in which
    // case we want to set up bExitOnInjection as well
    if (SKIF_Notify_hWnd != NULL && _Signal._RunningInstance == SKIF_Notify_hWnd)
    {
      PostMessage (_Signal._RunningInstance, (_Signal.Temporary) ? WM_SKIF_TEMPSTARTEXIT : WM_SKIF_START, 0x0, 0x0);
      _Signal.Quit = false; // Disallow using Start and Quit at the same time, but only if Temp is not being used
    }
    else
      PostMessage (_Signal._RunningInstance, (_Signal.Temporary) ? WM_SKIF_TEMPSTART     : WM_SKIF_START, 0x0, 0x0);
  }

  if (_Signal.Stop)
    PostMessage (_Signal._RunningInstance, WM_SKIF_STOP, 0x0, 0x0);
    
  if (_Signal.Minimize)
  {
    //PostMessage (_Signal._RunningInstance, WM_SKIF_MINIMIZE, 0x0, 0x0);

    // Send WM_SKIF_MINIMIZE to all running instances (including ourselves, though we won't act upon it if SKIF_ImGui_hWnd hasn't been created)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
          PostMessage (hWnd, WM_SKIF_MINIMIZE, 0x0, 0x0);
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  if (_Signal.CheckForUpdates)
  {
    // PostMessage (_Signal._RunningInstance, WM_SKIF_RUN_UPDATER, 0x0, 0x0);

    // Send WM_SKIF_RUN_UPDATER to all running instances (including ourselves)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
          PostMessage (hWnd, WM_SKIF_RUN_UPDATER, 0x0, 0x0);
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  if (_Signal.Quit)
  {
    // PostMessage (_Signal._RunningInstance, WM_CLOSE, 0x0, 0x0);

    // Send WM_CLOSE to all running instances (including ourselves)
    EnumWindows ( []( HWND   hWnd,
                      LPARAM lParam ) -> BOOL
    {
      wchar_t                         wszRealWindowClass [64] = { };
      if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
        if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
          PostMessage (hWnd, WM_CLOSE, 0x0, 0x0);
      return TRUE;
    }, (LPARAM)SKIF_NotifyIcoClass);
  }

  // Restore the foreground state to whatever app had it before
  if (SKIF_Notify_hWnd == NULL)
  {
    if (hWndOrigForeground != 0)
    {
      if (IsIconic        (hWndOrigForeground))
        ShowWindow        (hWndOrigForeground, SW_SHOWNA);
      SetForegroundWindow (hWndOrigForeground);
    }

    PLOG_INFO << "Terminating due to this instance having done its job.";
    ExitProcess (0x0);
  }
}

void
SKIF_Startup_RaiseRunningInstance (void)
{
  if (! _Signal._RunningInstance)
    return;
  
  // We must allow the existing process to set the foreground window
  //   as this is part of the WM_SKIF_RESTORE procedure
  DWORD pidAlreadyExists = 0;
  GetWindowThreadProcessId (_Signal._RunningInstance, &pidAlreadyExists);
  if (pidAlreadyExists)
    AllowSetForegroundWindow (pidAlreadyExists);

  PLOG_INFO << "Attempting to restore the running instance: " << pidAlreadyExists;
  SendMessage (_Signal._RunningInstance, WM_SKIF_RESTORE, 0x0, 0x0);
  
  PLOG_INFO << "Terminating due to this instance having done its job.";
  ExitProcess (0x0);
}

void SKIF_Shell_CreateUpdateNotifyMenu (void)
{
  if (hMenu != NULL)
    DestroyMenu (hMenu);

  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

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
  if (hMenu != NULL)
  {
    AppendMenu (hMenu, MF_STRING | ((svcRunningAutoStop) ? MF_CHECKED | MF_GRAYED : (svcRunning)         ? MF_GRAYED : 0x0), SKIF_NOTIFY_STARTWITHSTOP, L"Start Service");
    AppendMenu (hMenu, MF_STRING | ((svcRunning)         ? MF_CHECKED | MF_GRAYED : (svcRunningAutoStop) ? MF_GRAYED : 0x0), SKIF_NOTIFY_START,         L"Start Service (manual stop)");
    AppendMenu (hMenu, MF_STRING | ((svcStopped)         ? MF_CHECKED | MF_GRAYED :                                    0x0), SKIF_NOTIFY_STOP,          L"Stop Service");
    AppendMenu (hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_RUN_UPDATER,   L"Check for updates");

  //AppendMenu (hMenu, MF_STRING | ((  _inject.bCurrentState) ? MF_CHECKED | MF_GRAYED : 0x0), SKIF_NOTIFY_START, L"Start Injection");
  //AppendMenu (hMenu, MF_STRING | ((! _inject.bCurrentState) ? MF_CHECKED | MF_GRAYED : 0x0), SKIF_NOTIFY_STOP,  L"Stop Injection");
    AppendMenu (hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_EXIT,          L"Exit");
  }
}

// This creates a notification icon
void SKIF_Shell_CreateNotifyIcon (void)
{
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  ZeroMemory (&niData,  sizeof (NOTIFYICONDATA));
  niData.cbSize       = sizeof (NOTIFYICONDATA); // 6.0.6 or higher (Windows Vista and later)
  niData.uID          = SKIF_NOTIFY_ICON;
//niData.guidItem     = SKIF_NOTIFY_GUID; // Prevents notification icons from appearing for separate running instances
  niData.uFlags       = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP; // NIF_GUID
  niData.hIcon        = (_inject.bCurrentState)
    ? LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIFONNOTIFY))
    : LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));
  niData.hWnd         = SKIF_Notify_hWnd;
  niData.uVersion     = NOTIFYICON_VERSION_4;
  wcsncpy_s (niData.szTip,      128, L"Special K",   128);

  niData.uCallbackMessage = WM_SKIF_NOTIFY_ICON;

  Shell_NotifyIcon (NIM_ADD, &niData);
  //Shell_NotifyIcon (NIM_SETVERSION, &niData); // Breaks shit, lol
}

// This populates the notification icon with the appropriate icon
void SKIF_Shell_UpdateNotifyIcon (void)
{
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  niData.uFlags       = NIF_ICON;
  niData.hIcon        = (_inject.bCurrentState)
    ? LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIFONNOTIFY))
    : LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));
  Shell_NotifyIcon (NIM_MODIFY, &niData);
}

// This deletes the notification icon
void SKIF_Shell_DeleteNotifyIcon (void)
{
  Shell_NotifyIcon (NIM_DELETE, &niData);
  DeleteObject     (niData.hIcon);
  niData.hIcon = 0;
}

// Show a desktop notification
// SKIF_NTOAST_UPDATE  - Appears always
// SKIF_NTOAST_SERVICE - Appears conditionally
void SKIF_Shell_CreateNotifyToast (UINT type, std::wstring message, std::wstring title = L"")
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  if (  (type == SKIF_NTOAST_UPDATE)  ||
      (_registry.iNotifications == 1) ||                             // Always
      (_registry.iNotifications == 2  && ! SKIF_ImGui_IsFocused ( )) // When Unfocused
     )
  {
    niData.uFlags       = 
        NIF_INFO  | NIF_REALTIME;  // NIF_REALTIME to indicate the notifications should be discarded if not displayed immediately

    niData.dwInfoFlags  = 
      (type == SKIF_NTOAST_SERVICE)
      ? NIIF_NONE | NIIF_RESPECT_QUIET_TIME | NIIF_NOSOUND // Mute the sound for service notifications
      : NIIF_NONE | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s (niData.szInfoTitle, 64,   title.c_str(),  64);
    wcsncpy_s (niData.szInfo,     256, message.c_str(), 256);

    Shell_NotifyIcon (NIM_MODIFY, &niData);

    // Set up a timer that automatically refreshes SKIF when the notification clears,
    //   allowing us to perform some maintenance and whatnot when that occurs
    SetTimer (SKIF_Notify_hWnd, IDT_REFRESH_NOTIFY, _registry.iNotificationsDuration, NULL);
  }
}

void SKIF_Shell_CreateJumpList (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <ICustomDestinationList>   pDestList;                                 // The jump list
  CComPtr <IObjectCollection>        pObjColl;                                  // Object collection to hold the custom tasks.
  CComPtr <IShellLink>               pLink;                                     // Reused for the custom tasks
  CComPtr <IObjectArray>             pRemovedItems;                             // Not actually used since we don't carry custom destinations
  PROPVARIANT                        pv;                                        // Used to give the custom tasks a title
  UINT                               cMaxSlots;                                 // Not actually used since we don't carry custom destinations
       
  // Create a jump list COM object.
  if     (SUCCEEDED (pDestList.CoCreateInstance (CLSID_DestinationList)))
  {
    pDestList     ->SetAppID        (SKIF_AppUserModelID);
    pDestList     ->BeginList       (&cMaxSlots, IID_PPV_ARGS (&pRemovedItems));

    pDestList     ->AppendKnownCategory (KDC_RECENT);

    if   (SUCCEEDED (pObjColl.CoCreateInstance (CLSID_EnumerableObjectCollection)))
    {

      // Task #2: Start Service (w/ auto stop)
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skif_executable);
        pLink     ->SetArguments    (L"Start Temp");                            // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skif_executable, 1);           // Set the icon location.
        pLink     ->SetDescription  (L"Starts the injection service and\n"
                                     L"stops it after injection.");             // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Start Service", &pv);
        pPropStore->SetValue               (PKEY_Title, pv);                    // Set the title property.
        PropVariantClear                              (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Task #1: Start Service (w/o autostop)
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skif_executable);
        pLink     ->SetArguments    (L"Start");                                 // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skif_executable, 1);           // Set the icon location.
        pLink     ->SetDescription  (L"Starts the injection service but\n"
                                     L"does not stop it after injection.");     // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Start Service (manual stop)", &pv);
        pPropStore->SetValue                 (PKEY_Title, pv);                  // Set the title property.
        PropVariantClear                                (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Task #3: Stop Service
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skif_executable);
        pLink     ->SetArguments    (L"Stop");                                  // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skif_executable, 2);           // Set the icon location.
        pLink     ->SetDescription  (L"Stops the injection service");           // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Stop Service", &pv);
        pPropStore->SetValue                (PKEY_Title, pv);                   // Set the title property.
        PropVariantClear                               (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Separator
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        InitPropVariantFromBoolean  (TRUE, &pv);
        pPropStore->SetValue (PKEY_AppUserModel_IsDestListSeparator, pv);       // Set the separator property.
        PropVariantClear                                  (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Task #4: Check for Updates
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skif_executable);
        pLink     ->SetArguments    (L"RunUpdater");                            // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skif_executable, 0);           // Set the icon location.
        pLink     ->SetDescription  (L"Checks for any available updates");      // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Check for Updates", &pv);
        pPropStore->SetValue                   (PKEY_Title, pv);                // Set the title property.
        PropVariantClear                                  (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      /*
      // Separator
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        InitPropVariantFromBoolean  (TRUE, &pv);
        pPropStore->SetValue (PKEY_AppUserModel_IsDestListSeparator, pv);       // Set the separator property.
        PropVariantClear                                  (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }
      */

      // Task #5: Exit
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (_path_cache.skif_executable);
        pLink     ->SetArguments    (L"Quit");                                  // Set the arguments
        pLink     ->SetIconLocation (_path_cache.skif_executable, 0);           // Set the icon location.
      //pLink     ->SetDescription  (L"Closes the application");                // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Exit", &pv);
        pPropStore->SetValue                (PKEY_Title, pv);                   // Set the title property.
        PropVariantClear                               (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      CComQIPtr <IObjectArray>       pTasksArray = pObjColl.p;                  // Get an IObjectArray interface for AddUserTasks.
      pDestList   ->AddUserTasks    (pTasksArray);                              // Add the tasks to the jump list.
      pDestList   ->CommitList      ( );                                        // Save the jump list.
      pTasksArray  .Release         ( );

      pObjColl     .Release         ( );
    }
    else
      PLOG_ERROR << "Failed to create CLSID_EnumerableObjectCollection object!";

    pDestList      .Release         ( );
  }
  else
    PLOG_ERROR << "Failed to create CLSID_DestinationList object!";
}

void SKIF_Shell_AddJumpList (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CComPtr <IShellLink>               pLink;                                     // Reused for the custom tasks
  PROPVARIANT                        pv;                                        // Used to give the custom tasks a title

  if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
  {
    CComQIPtr <IPropertyStore>   pPropStore = pLink.p;                      // The link title is kept in the object's property store, so QI for that interface.

    bool uriLaunch = (path.empty() || ! PathFileExists (path.c_str()));

    if (uriLaunch)
    {
      parameters = L"SKIF_URI=" + parameters;

      if (! bService)
        name       = name + L" (w/o Special K)";
      else
        parameters = L"Start Temp " + parameters;
    }

    else {
      parameters = SK_FormatStringW (LR"("%ws" %ws)", path.c_str(), parameters.c_str());
    }

    pLink     ->SetPath             (_path_cache.skif_executable);         // Point to SKIF.exe
    pLink     ->SetArguments        (parameters.c_str());                  // Set the arguments

    if (! directory.empty())
      pLink   ->SetWorkingDirectory (directory.c_str());                   // Set the working directory

    if (PathFileExists (icon_path.c_str()))
      pLink   ->SetIconLocation     (icon_path.c_str(), 0);                // Set the icon location

    pLink     ->SetDescription      (parameters.c_str());                  // Set the link description (tooltip on the jump list item)

    InitPropVariantFromString       (name.c_str(), &pv);
    pPropStore->SetValue            (PKEY_Title,    pv);                   // Set the title property
    PropVariantClear                              (&pv);

    pPropStore->Commit              ( );                                   // Save the changes we made to the property store

    SHAddToRecentDocs               (SHARD_LINK, pLink.p);                 // Add to the Recent list

    pPropStore .Release             ( );
    pLink      .Release             ( );
  }
}


void SKIF_Initialize (LPWSTR lpCmdLine)
{
  static bool isInitalized = false;

  if (isInitalized)
    return;

  isInitalized = true;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
    
  // Let's change the current working directory to the folder of the executable itself.
  SetCurrentDirectory (_path_cache.specialk_install);

  // Generate 8.3 filenames
  //SK_Generate8Dot3    (_path_cache.skif_workdir_org);
  SK_Generate8Dot3    (_path_cache.specialk_install);

  bool fallback = true;

  // See if we can interact with the install folder
  // This section of the code triggers a refresh of the DLL files for other running SKIF instances
  // TODO: Find a better way to determine permissions that does not rely on creating dummy files/folders?
  std::filesystem::path testDir  (_path_cache.specialk_install);
  std::filesystem::path testFile (testDir);

  testDir  /= L"SKIFTMPDIR";
  testFile /= L"SKIFTMPFILE.tmp";

  // Try to delete any existing tmp folder or file (won't throw an exception at least)
  RemoveDirectory (testDir.c_str());
  DeleteFile      (testFile.wstring().c_str());

  std::error_code ec;
  // See if we can create a folder
  if (! std::filesystem::exists (            testDir, ec) &&
        std::filesystem::create_directories (testDir, ec))
  {
    // Delete it
    RemoveDirectory (testDir.c_str());

    // See if we can create a file
    HANDLE h = CreateFile ( testFile.wstring().c_str(),
            GENERIC_READ | GENERIC_WRITE,
              FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                  CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                      NULL );

    // If the file was created successfully
    if (h != INVALID_HANDLE_VALUE)
    {
      // We need to close the handle as well
      CloseHandle (h);

      // Delete it
      DeleteFile (testFile.wstring().c_str());

      // Use current path as we have write permissions
      wcsncpy_s ( _path_cache.specialk_userdata, MAX_PATH,
                  _path_cache.specialk_install, _TRUNCATE );

      // No need to rely on the fallback
      fallback = false;
    }
  }

  if (fallback)
  {
    // Fall back to appdata in case of issues
    std::wstring fallbackDir =
      std::wstring (_path_cache.my_documents.path) + LR"(\My Mods\SpecialK\)";

    wcsncpy_s ( _path_cache.specialk_userdata, MAX_PATH,
                fallbackDir.c_str(), _TRUNCATE);
        
    // Create any missing directories
    if (! std::filesystem::exists (            fallbackDir, ec))
          std::filesystem::create_directories (fallbackDir, ec);
  }

  // Now we can proceed with initializing the logging
  // If SKIF is used as a launcher, use a separate log file
  std::wstring logPath =
    SK_FormatStringW ((_Signal.Launcher || _Signal.LauncherURI)
                ? LR"(%ws\SKIF_launcher.log)" 
                : LR"(%ws\SKIF.log)",
          _path_cache.specialk_userdata
    );

  std::wstring logPath_old = logPath + L".bak";

  // Delete the .old log file and rename any previous log to .old
  DeleteFile (logPath_old.c_str());
  MoveFile   (logPath.c_str(), logPath_old.c_str());

  // Engage logging!
  static plog::RollingFileAppender<plog::LogFormatterUtcTime> fileAppender(logPath.c_str(), 10000000, 1);
  plog::init (plog::debug, &fileAppender);

  // Let us do a one-time check if a debugger is attached,
  //   and if so set up PLOG to push logs there as well
  BOOL isRemoteDebuggerPresent = FALSE;
  CheckRemoteDebuggerPresent (SKIF_Util_GetCurrentProcess(), &isRemoteDebuggerPresent);

  if (isRemoteDebuggerPresent || IsDebuggerPresent())
  {
    static plog::DebugOutputAppender<plog::TxtFormatter> debugOutputAppender;
    plog::get()->addAppender (&debugOutputAppender);

    SKIF_debuggerPresent = true;
  }
  
  GetCurrentDirectoryW    (MAX_PATH, _path_cache.skif_workdir);


#ifdef _WIN64
  PLOG_INFO << "Initializing Special K Injection Frontend (SKIF) 64-bit..."
#else
  PLOG_INFO << "Initializing Special K Injection Frontend (SKIF) 32-bit..."
#endif
            << "\n+------------------+-------------------------------------+"
            << "\n| SKIF Executable  | " << _path_cache.skif_executable
            << "\n|    > version     | " << SKIF_VERSION_STR_A
            << "\n|    > build       | " << __DATE__ ", " __TIME__
            << "\n|    > mode        | " << ((_Signal.Launcher || _Signal.LauncherURI) ? "Launcher" : "Regular")
            << "\n|    > arguments   | " << lpCmdLine
            << "\n|    > directory   | "
            << "\n|      > original  | " << _path_cache.skif_workdir_org
            << "\n|      > adjusted  | " << _path_cache.skif_workdir
            << "\n| SK Install       | " << _path_cache.specialk_install
            << "\n| SK User Data     | " << _path_cache.specialk_userdata
            << "\n+------------------+-------------------------------------+";
}

bool bKeepWindowAlive  = true,
     bKeepProcessAlive = true;

// Uninstall registry keys
// Current User: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\{F4A43527-9457-424A-90A6-17CF02ACF677}_is1
// All Users:   HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{F4A43527-9457-424A-90A6-17CF02ACF677}_is1

// Install folders
// Legacy:                              Documents\My Mods\SpecialK
// Modern (Current User; non-elevated): %LOCALAPPDATA%\Programs\Special K
// Modern (All Users;        elevated): %PROGRAMFILES%\Special K


using LoadLibraryA_pfn   = HMODULE (WINAPI*)(LPCSTR);
using LoadLibraryW_pfn   = HMODULE (WINAPI*)(LPCWSTR);
using LoadLibraryExA_pfn = HMODULE (WINAPI*)(LPCSTR,HANDLE,DWORD);
using LoadLibraryExW_pfn = HMODULE (WINAPI*)(LPCWSTR,HANDLE,DWORD);

LoadLibraryA_pfn   LoadLibraryA_Original   = nullptr;
LoadLibraryW_pfn   LoadLibraryW_Original   = nullptr;
LoadLibraryExA_pfn LoadLibraryExA_Original = nullptr;
LoadLibraryExW_pfn LoadLibraryExW_Original = nullptr;

HMODULE
WINAPI
LoadLibraryExW_Hook (LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
  if (StrStrIW (lpLibFileName, L"GameOverlayRenderer") || StrStrIW (lpLibFileName, L"RTSS"))
  {
    SetLastError (ERROR_MOD_NOT_FOUND);
    return nullptr;
  }

  return
    LoadLibraryExW_Original (lpLibFileName, hFile, dwFlags);
}

HMODULE
WINAPI
LoadLibraryExA_Hook (LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
  if (StrStrIA (lpLibFileName, "GameOverlayRenderer") || StrStrIA (lpLibFileName, "RTSS"))
  {
    SetLastError (ERROR_MOD_NOT_FOUND);
    return nullptr;
  }

  return
    LoadLibraryExA_Original (lpLibFileName, hFile, dwFlags);
}

HMODULE
WINAPI
LoadLibraryW_Hook (LPCWSTR lpLibFileName)
{
  if (StrStrIW (lpLibFileName, L"GameOverlayRenderer") || StrStrIW (lpLibFileName, L"RTSS"))
  {
    SetLastError (ERROR_MOD_NOT_FOUND);
    return nullptr;
  }

  return
    LoadLibraryW_Original (lpLibFileName);
}

HMODULE
WINAPI
LoadLibraryA_Hook (LPCSTR lpLibFileName)
{
  if (StrStrIA (lpLibFileName, "GameOverlayRenderer") || StrStrIA (lpLibFileName, "RTSS"))
  {
    SetLastError (ERROR_MOD_NOT_FOUND);
    return nullptr;
  }

  return
    LoadLibraryA_Original (lpLibFileName);
}

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

  MH_Initialize ();

  MH_CreateHookApi (L"kernel32.dll", "LoadLibraryW",   LoadLibraryW_Hook,   (LPVOID *)&LoadLibraryW_Original);
  MH_CreateHookApi (L"kernel32.dll", "LoadLibraryExW", LoadLibraryExW_Hook, (LPVOID *)&LoadLibraryExW_Original);
  MH_CreateHookApi (L"kernel32.dll", "LoadLibraryA",   LoadLibraryA_Hook,   (LPVOID *)&LoadLibraryA_Original);
  MH_CreateHookApi (L"kernel32.dll", "LoadLibraryExA", LoadLibraryExA_Hook, (LPVOID *)&LoadLibraryExA_Original);

  MH_QueueEnableHook (MH_ALL_HOOKS);
  MH_ApplyQueued     ();
 

  SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT);
  
  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_MainThread");

  //CoInitializeEx (nullptr, 0x0);
  OleInitialize (NULL); // Needed for IDropTarget

  // All DbgHelp functions, such as ImageNtHeader, are single threaded.
  extern   CRITICAL_SECTION   CriticalSectionDbgHelp;
  InitializeCriticalSection (&CriticalSectionDbgHelp);

  // Initialize
  extern   CRITICAL_SECTION   VFSManifestSection;
  InitializeCriticalSection (&VFSManifestSection);

  if (StrStrIW (lpCmdLine, L"ResetOverlayMode") != NULL)
  {
    if (::IsUserAnAdmin ( ))
    {
      // HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\ -> DisableOverlays (DWORD; 1 = disabled)
      // HKLM\SOFTWARE\Microsoft\Windows\Dwm\                   -> OverlayTestMode (DWORD; 5 = disabled)

      HKEY hKey;

      if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SYSTEM\CurrentControlSet\Control\GraphicsDrivers\)", 0, KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, &hKey))
      {
        RegDeleteValueW (hKey, L"DisableOverlays");
        RegCloseKey     (hKey);
      }

      if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\Dwm\)", 0, KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, &hKey))
      {
        RegDeleteValueW (hKey, L"OverlayTestMode");
        RegCloseKey     (hKey);
      }
    }

    else {
      MessageBox(NULL, L"The 'ResetOverlayMode' command line argument requires"
                       L" elevated permissions to work properly.",
                       L"Admin privileges required",
               MB_ICONERROR | MB_OK);
    }
  }

  if (StrStrIW (lpCmdLine, L"RestartDisplDrv" ) != NULL)
  {
    if (::IsUserAnAdmin ( ))
    {
      if (! RestartDriver ( ))
        SKIF_Util_GetErrorAsMsgBox (L"Failed to restart display driver");
    }

    else {
      MessageBox(NULL, L"The 'RestartDisplDrv' command line argument requires"
                       L" elevated permissions to work properly.",
                       L"Admin privileges required",
               MB_ICONERROR | MB_OK);
    }
  }

  // Don't stick around if any of these commands are being used.
  if (StrStrIW (lpCmdLine, L"ResetOverlayMode") != NULL ||
      StrStrIW (lpCmdLine, L"RestartDisplDrv" ) != NULL)
    ExitProcess (0x0);

  // Get the current time to use as a basis of how long the initialization took
  SKIF_startupTime = SKIF_Util_timeGetTime1();
  
  // Process cmd line arguments (1/4) -- this sets up the necessary variables
  SKIF_Startup_ProcessCmdLineArgs (lpCmdLine);

  // This constructs these singleton objects
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( ); // Does not rely on anything
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( ); // Does not rely on anything

  // Initialize SKIF
  SKIF_Initialize (lpCmdLine); // Relies on _path_cache and sets up logging

  plog::get()->setMaxSeverity((plog::Severity) _registry.iLogging);

  PLOG_INFO << "Max severity to log was set to " << _registry.iLogging;

  // Set process preference to E-cores using only CPU sets, :)
  //  as affinity masks are inherited by child processes... :(
  SKIF_Util_SetProcessPrefersECores ( );

  // This constructs the singleton object
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( ); // Relies on SKIF_Initialize (working dir) + _path_cache (cached paths) + logging
  
  // Process cmd line arguments (2/4)
  hWndOrigForeground = // Remember what third-party window is currently in the foreground
    GetForegroundWindow ( );
  SKIF_Startup_AddGame               (lpCmdLine);
  SKIF_Startup_LaunchGamePreparation (lpCmdLine);
  SKIF_Startup_LaunchURIPreparation  (lpCmdLine);

  // Force a one-time check so we know whether to start/stop the service...
  _inject._TestServletRunlevel (true);

  // If there already an instance of SKIF running we do not
  //   need to create a window to service the cmd line args
  // Process cmd line arguments (3/4)
  if (_Signal._RunningInstance)
  {
    PLOG_INFO << "An already running instance was detected.";

    SKIF_Startup_LaunchGameService         ( );
    SKIF_Startup_LaunchGame                ( );

    // The below only execute if SKIF is not used as a launcher
    SKIF_Startup_ProxyCommandLineArguments ( );
    if (! _registry.bAllowMultipleInstances)
      SKIF_Startup_RaiseRunningInstance    ( );
  }

  // Sets the current process app user model ID (used for jump lists and the like)
  if (FAILED (SetCurrentProcessExplicitAppUserModelID (SKIF_AppUserModelID)))
    PLOG_ERROR << "Call to SetCurrentProcessExplicitAppUserModelID failed!";

  // Load the SKIF.exe module (used to populate the icon here and there)
  hModSKIF =
    GetModuleHandleW (nullptr);

  //SKIF_Util_Debug_LogUserNames ( );

  // First round
  if (_Signal.Minimize)
    nCmdShow = SW_MINIMIZE;

  if (nCmdShow == SW_MINIMIZE && _registry.bCloseToTray)
    nCmdShow = SW_HIDE;

  // "Run: Minimized" uses SW_SHOWMINNOACTIVE but that causes an empty window on the desktop...? Weird...
  else if (nCmdShow == SW_SHOWMINNOACTIVE)
    nCmdShow = SW_MINIMIZE;

  // "Run: Maximized" uses SW_SHOWMAXIMIZED but we do not actually support that specific flag... (misaligned and incorrectly sized window)
  else if (nCmdShow == SW_SHOWMAXIMIZED)
  {
    PLOG_ERROR << "Ignoring SW_SHOWMAXIMIZED (\"Run: Maximized\") as it is not supported!";

    // No idea how to solve this one. Right now SKIF opens maximized,
    //   then gets restored straight after since we set *out_style |= WS_MAXIMIZE;
  }

  // Second round
  if (nCmdShow == SW_MINIMIZE)
    startedMinimized = true;
  else if (nCmdShow == SW_HIDE)
    startedMinimized = SKIF_isTrayed = true;

  SKIF_nCmdShow = nCmdShow;

  // Check if Controlled Folder Access is enabled
  if (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode)
  {
    if (_registry.bDisableCFAWarning == false && SKIF_Util_GetControlledFolderAccess())
    {
      if (IDYES == MessageBox(NULL, L"Controlled Folder Access is enabled in Windows and may prevent Special K and even some games from working properly. "
                                    L"It is recommended to either disable the feature or add exclusions for games where Special K is used as well as SKIF (this application)."
                                    L"\n\n"
                                    L"Do you want to disable this warning for all future launches?"
                                    L"\n\n"
                                    L"Microsoft's support page with more information will open when you select any of the options below.",
                                    L"Warning about Controlled Folder Access",
                 MB_ICONWARNING | MB_YESNOCANCEL))
      {
        _registry.regKVDisableCFAWarning.putData (true);
      }

      SKIF_Util_OpenURI (L"https://support.microsoft.com/windows/allow-an-app-to-access-controlled-folders-b5b6627a-b008-2ca2-7931-7e51e912b034");
    }

    // Register SKIF in Windows to enable quick launching.

    PLOG_INFO << "Checking global registry values..."
              << "\n+------------------+-------------------------------------+"
              << "\n| Central path     | " << _registry.wsPath
              << "\n| App registration | " << _registry.wsAppRegistration
              << "\n+------------------+-------------------------------------+";

    SKIF_Util_RegisterApp ( );
  }

  // Generate install GUID if relevant and one does not exist
  SKIF_Util_GenerateInstallGUID ( );

  PLOG_INFO << "Creating notification icon...";

  // Create invisible notify window (for the traybar icon and notification toasts, and for doing D3D11 tests)
  WNDCLASSEX wcNotify =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, SKIF_Notify_WndProc,
            0L,         0L,
        NULL, nullptr,  nullptr,
              nullptr,  nullptr,
              SKIF_NotifyIcoClass,
              nullptr          };

  if (! ::RegisterClassEx (&wcNotify))
  {
    return 0;
  }

  SKIF_Notify_hWnd      =
    CreateWindowExW (                                            WS_EX_NOACTIVATE,
      wcNotify.lpszClassName, _T("Special K Notification Icon"), WS_ICONIC,
                         0, 0,
                         0, 0,
                   nullptr, nullptr,
        wcNotify.hInstance, nullptr
    );

  hIcon = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));

  // The notify window has been created but not displayed.
  // Now we have a parent window to which a notification tray icon can be associated.
  SKIF_Shell_CreateNotifyIcon       ();
  SKIF_Shell_CreateUpdateNotifyMenu ();

  // Initialize the gamepad input child thread
  static SKIF_GamePadInputHelper& _gamepad =
         SKIF_GamePadInputHelper::GetInstance ( );

  // If there were not an instance of SKIF already running
  //   we need to handle any remaining tasks here after 
  //   we have a window ready to handle remaining cmds
  // Process cmd line arguments (4/4)
  if (! _Signal._RunningInstance || _registry.bAllowMultipleInstances)
  {
    if (! _Signal._GamePath.empty())
    {
      SKIF_Startup_LaunchGameService ( );
      SKIF_Startup_LaunchGame        ( );
      _registry.bMiniMode = true;

      // We do not want the Steam overlay to draw upon SKIF so we have to disable it,
      // though we need to set this _after_ we have launched any game but before we set up Direct3D.
      // It is later removed when the user switches away from the small mode
      PLOG_INFO << "Setting SteamNoOverlayUIDrawing to prevent the Steam overlay from hooking SKIF...";
      SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");
      SteamOverlayDisabled = true;
    }

    // If we are not acting as a launcher,
    //   send messages to ourselves.
    else {
      _Signal._RunningInstance = SKIF_Notify_hWnd;
      SKIF_Startup_ProxyCommandLineArguments ( );

      // If we are starting the service,
      //   let us start in small mode
      //if (_Signal.Start)
      //  _registry.bMiniMode = true;

      if (_Signal.ServiceMode)
        _registry.bMiniMode = true;
      
      // If we are intending to quit, let us
      //   start in small mode so that the
      //     updater etc are not executed...
      if (_Signal.Quit)
        _registry.bMiniMode = true;
    }
  }
  
  PLOG_INFO << "Initializing Direct3D...";

  DWORD temp_time = SKIF_Util_timeGetTime1();

  // Initialize Direct3D
  if (! CreateDeviceD3D (SKIF_Notify_hWnd))
  {
    CleanupDeviceD3D ();
    return 1;
  }

  PLOG_DEBUG << "Operation [CreateDeviceD3D] took " << (SKIF_Util_timeGetTime1() - temp_time) << " ms.";

  // Register to be notified if the effective power mode changes
  //SKIF_Util_SetEffectivePowerModeNotifications (true); // (this serves no purpose yet)

  // The DropTarget object used for drag-and-drop support for new covers
  static SKIF_DropTargetObject& _drag_drop  = SKIF_DropTargetObject::GetInstance ( );
  
  PLOG_INFO << "Initializing ImGui...";

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION   ();
  ImGui::CreateContext ();

  ImGuiIO& io =
    ImGui::GetIO ();

  (void)io; // WTF does this do?!

  io.IniFilename = "SKIF.ini";                                // nullptr to disable imgui.ini
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
//io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
//io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP! 
//io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI

  // Viewports
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
  io.ConfigViewportsNoAutoMerge      = false;
  io.ConfigViewportsNoTaskBarIcon    = false;
  io.ConfigViewportsNoDecoration     = false; // We want decoration (OS-provided animations etc)
  io.ConfigViewportsNoDefaultParent  = false;

  // Docking
//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  io.ConfigDockingAlwaysTabBar       = false;
  io.ConfigDockingTransparentPayload =  true;

  // Main window override flags
  ImGuiWindowClass SKIF_AppWindow;
  // This prevents the main window from ever being merged into the implicit Debug##Default fallback window...
  // ... which works around a pesky bug that occurs on OS snapping/resizing...
  SKIF_AppWindow.ViewportFlagsOverrideSet |= ImGuiViewportFlags_NoAutoMerge;

  // Enable ImGui's debug logging output
  ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                            ? ImGuiDebugLogFlags_EventMask_
                                            : ImGuiDebugLogFlags_EventViewport);

  // Setup Dear ImGui style
  ImGuiStyle& style =
      ImGui::GetStyle ( );
  SKIF_ImGui_SetStyle (&style);

#if 0
  // When viewports are enabled we tweak WindowRounding/WindowBg
  //   so platform windows can look identical to regular ones.

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    style.WindowRounding               = 5.0F;
    style.Colors [ImGuiCol_WindowBg].w = 1.0F;
  }
#endif

  // Setup Platform/Renderer bindings
  PLOG_INFO << "Initializing ImGui Win32 platform...";
  ImGui_ImplWin32_Init (nullptr); // This sets up a separate window/hWnd as well, though it will first be created at the end of the main loop
  PLOG_INFO << "Initializing ImGui D3D11 platform...";
  ImGui_ImplDX11_Init  (SKIF_pd3dDevice, SKIF_pd3dDeviceContext);

  //SKIF_Util_GetMonitorHzPeriod (SKIF_hWnd, MONITOR_DEFAULTTOPRIMARY, dwDwmPeriod);
  //OutputDebugString((L"Initial refresh rate period: " + std::to_wstring (dwDwmPeriod) + L"\n").c_str());

  // Message queue/pump
  MSG msg = { };

  RepositionSKIF   = (! PathFileExistsW (L"SKIF.ini") || _registry.bOpenAtCursorPosition);

  // Add the status bar if it is not disabled
  SKIF_ImGui_AdjustAppModeSize (NULL);

  // Initialize ImGui fonts
  PLOG_INFO << "Initializing ImGui fonts...";
  SKIF_ImGui_InitFonts (SKIF_ImGui_FontSizeDefault, (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode) );

  // Variable related to continue/pause processing behaviour
  bool HiddenFramesContinueProcessing = true;  // We always have hidden frames that require to continue processing on init
  int  HiddenFramesRemaining          = 0;
  bool svcTransitionFromPendingState  = false; // This is used to continue processing if we transitioned over from a pending state (which kills the refresh timer)

  bool repositionToCenter = false;

  // Check if the display supports HDR
  SKIF_Util_IsHDRSupported (true);

  // Do final checks and actions if we are expected to live longer than a few seconds
  if (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode)
  {
    // Register HDR toggle hotkey (if applicable)
    SKIF_Util_RegisterHotKeyHDRToggle (&_registry.kbToggleHDRDisplay);

    // Register service (auto-stop) hotkey
    SKIF_Util_RegisterHotKeySVCTemp   (&_registry.kbStartService);
  }

  // Spawn the gamepad input thread
  _gamepad.SpawnChildThread ( );

  PLOG_INFO << "Initializing updater...";
  // Initialize the updater
  static SKIF_Updater& _updater = 
         SKIF_Updater::GetInstance ( );

  // Main loop
  PLOG_INFO << "Entering main loop...";
  SKIF_firstFrameTime = SKIF_Util_timeGetTime1 ( );

  while (! SKIF_Shutdown.load() ) // && IsWindow (hWnd) )
  {
    // Reset on each frame
    SKIF_MouseDragMoveAllowed = true;
    coverFadeActive           = false; // Assume there's no cover fade effect active
    msg                       = { };
    static UINT uiLastMsg     = 0x0;

#ifdef DEBUG

    // When built in debug mode, we should check if a debugger has been attached
    //   on every frame to identify and output to debuggers attached later
    if (! SKIF_debuggerPresent)
    {
      BOOL isRemoteDebuggerPresent = FALSE;
      CheckRemoteDebuggerPresent (SKIF_Util_GetCurrentProcess(), &isRemoteDebuggerPresent);

      if (isRemoteDebuggerPresent || IsDebuggerPresent())
      {
        static plog::DebugOutputAppender<plog::TxtFormatter> debugOutputAppender;
        plog::get()->addAppender (&debugOutputAppender);
        SKIF_debuggerPresent = true;
      }
    }

#endif // DEBUG

    // Various hotkeys that SKIF supports (resets on every frame)
    bool hotkeyF5    = (              ImGui::GetKeyData (ImGuiKey_F5 )->DownDuration == 0.0f), // Library/About: Refresh data
         hotkeyF6    = (              ImGui::GetKeyData (ImGuiKey_F6 )->DownDuration == 0.0f), // Appearance: Toggle DPI scaling
         hotkeyF7    = (              ImGui::GetKeyData (ImGuiKey_F7 )->DownDuration == 0.0f), // Appearance: Cycle between color themes
         hotkeyF8    = (              ImGui::GetKeyData (ImGuiKey_F8 )->DownDuration == 0.0f), // Appearance: Toggle UI borders
         hotkeyF9    = (              ImGui::GetKeyData (ImGuiKey_F9 )->DownDuration == 0.0f), // Appearance: Toggle color depth
         hotkeyF11   = (              ImGui::GetKeyData (ImGuiKey_F11)->DownDuration == 0.0f), // Appearance: Toggle app mode (Library/Service)
         hotkeyCtrlQ = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_Q  )->DownDuration == 0.0f), // Close the app
         hotkeyCtrlW = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_W  )->DownDuration == 0.0f), // Close the app
         hotkeyCtrlR = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_R  )->DownDuration == 0.0f), // Library/About: Refresh data
         hotkeyCtrlT = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_T  )->DownDuration == 0.0f), // Appearance: Toggle app mode (Library/Service)
         hotkeyCtrlA = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_A  )->DownDuration == 0.0f), // Library: Add game
         hotkeyCtrlN = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_N  )->DownDuration == 0.0f), // Minimize app
         hotkeyCtrl1 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_1  )->DownDuration == 0.0f), // Switch to Library
         hotkeyCtrl2 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_2  )->DownDuration == 0.0f), // Switch to Monitor
         hotkeyCtrl3 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_3  )->DownDuration == 0.0f), // Switch to Settings
         hotkeyCtrl4 = (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_4  )->DownDuration == 0.0f); // Switch to About

    auto _TranslateAndDispatch = [&](void) -> bool
    {
      while (! SKIF_Shutdown.load() && PeekMessage (&msg, 0, 0U, 0U, PM_REMOVE))
      {
        if (msg.message == WM_QUIT)
        {
          SKIF_Shutdown.store (true);
          SKIF_ExitCode = (int) msg.wParam;
          return false; // return false on exit or system shutdown
        }

        // There are four different window procedures that a message can be dispatched to based on the HWND of the message
        // 
        //                           SKIF_Notify_WndProc ( )  <=  SKIF_Notify_hWnd                         :: Handles messages meant for the notification icon.
        //                                  SKIF_WndProc ( )  <=  SKIF_Notify_hWnd                         :: Handles all custom SKIF window messages and actions.
        //                                                                                                    - Gets called by SKIF_Notify_WndProc ( ).
        // 
        // ImGui_ImplWin32_WndProcHandler_PlatformWindow ( )  <=  SKIF_ImGui_hWnd, Other HWNDs             :: Handles messages meant for the overarching ImGui Platform window of SKIF, as well as any
        //                                                                                                      additional swapchain windows (menus/tooltips that stretches beyond SKIF_ImGui_hWnd).
        // ImGui_ImplWin32_WndProcHandler                ( )  <=  SKIF_ImGui_hWnd, Other HWNDs             :: Handles mouse/key input and focus events for ImGui platform windows.
        //                                                                                                    - Gets called by ImGui_ImplWin32_WndProcHandler_PlatformWindow ( ).
        // 
        TranslateMessage (&msg);
        DispatchMessage  (&msg);

        if (msg.hwnd == 0) // Don't redraw on thread messages
          msgDontRedraw = true;

        if (msg.message == WM_MOUSEMOVE)
        {
          static LPARAM lParamPrev;
          static WPARAM wParamPrev;

          // Workaround for a bug in System Informer where it sends a fake WM_MOUSEMOVE message to the window the cursor is over
          // Ignore the message if WM_MOUSEMOVE has identical data as the previous msg
          if (msg.lParam == lParamPrev &&
              msg.wParam == wParamPrev)
            msgDontRedraw = true;
          else {
            lParamPrev = msg.lParam;
            wParamPrev = msg.wParam;
          }
        }

        uiLastMsg = msg.message;
      }

      return ! SKIF_Shutdown.load(); // return false on exit or system shutdown
    };

    static bool restoreOnInjExitAck = false;

    // Injection acknowledgment; minimize SKIF
    if (                     hInjectAckEx.m_h != 0 &&
        WaitForSingleObject (hInjectAckEx.m_h,   0) == WAIT_OBJECT_0)
    {
      PLOG_DEBUG << "Injection was acknowledged!";
      hInjectAckEx.Close ();

      if (SKIF_ImGui_hWnd != NULL)
      {
        // Set up exit acknowledge as well
        if (_registry.bRestoreOnGameExit)
          restoreOnInjExitAck = ! startedMinimized && ! IsIconic (SKIF_ImGui_hWnd) && ! SKIF_isTrayed;

        if (_registry.bMinimizeOnGameLaunch && ! IsIconic (SKIF_ImGui_hWnd) && ! SKIF_isTrayed)
          ShowWindowAsync (SKIF_ImGui_hWnd, SW_SHOWMINNOACTIVE);
      }

      // If we do not use auto-stop mode, reset the signal
      if (! _inject.bAckInj && _inject.bCurrentState)
        _inject.SetInjectAckEx (true);
    }

    // Exit acknowledgement; restores SKIF
    if (                     hInjectExitAckEx.m_h != 0 &&
        WaitForSingleObject (hInjectExitAckEx.m_h,   0) == WAIT_OBJECT_0)
    {
      PLOG_DEBUG << "Game exit was acknowledged!";
      hInjectExitAckEx.Close ();

      if (_registry.bRestoreOnGameExit && restoreOnInjExitAck && (SKIF_ImGui_hWnd != NULL) && IsIconic (SKIF_ImGui_hWnd))
        SendMessage (SKIF_Notify_hWnd, WM_SKIF_RESTORE, 0x0, 0x0);

      restoreOnInjExitAck = false;

      // If we do not use auto-stop mode, reset the signal
      if (! _inject.bAckInj && _inject.bCurrentState)
        _inject.SetInjectExitAckEx (true);
    }

    // Injection acknowledgment; shutdown service
    if (                     hInjectAck.m_h != 0 &&
        WaitForSingleObject (hInjectAck.m_h,   0) == WAIT_OBJECT_0)
    {
      PLOG_DEBUG << "Injection was acknowledged, service is being stopped!";
      hInjectAck.Close ();
      
      SKIF_Startup_SetGameAsForeground ( );

      _inject.bAckInjSignaled = true;
      _inject._StartStopInject (true);
    }

    // If SKIF is acting as a temporary launcher, exit when the running service has been stopped
    if (_registry._ExitOnInjection && _inject.runState == SKIF_InjectionContext::RunningState::Stopped)
    {
      static DWORD dwExitDelay = SKIF_Util_timeGetTime() + _registry.iNotificationsDuration;

      // MessageDuration seconds delay to allow Windows to send both notifications properly
      // If notifications are disabled, exit immediately
      if (_registry.iNotifications == 0 ||
         (dwExitDelay < SKIF_Util_timeGetTime()))
      {
        SKIF_Startup_SetGameAsForeground ( );

        PLOG_INFO << "Terminating as the app is set to exit on injection...";

        _registry._ExitOnInjection = false;
        //PostMessage (hWnd, WM_QUIT, 0, 0);
        PostQuitMessage (0);
      }
    }

    // Attempt to set the game window as foreground after SKIF's window has appeared
    static bool
        runPostWindowCreation =  true;
    if ((_Signal.Launcher || _Signal.LauncherURI) &&
        runPostWindowCreation && SKIF_ImGui_hWnd != NULL)
    {   runPostWindowCreation =  false;
      SKIF_Startup_SetGameAsForeground ( );
    }

    // Window stuff

    ImRect rectCursorMonitor; // RepositionSKIF

    // RepositionSKIF -- Step 1: Retrieve monitor of cursor
    if (RepositionSKIF)
    {
      ImRect t;
      for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
      {
        const ImGuiPlatformMonitor& tmpMonitor = ImGui::GetPlatformIO().Monitors[monitor_n];
        t = ImRect(tmpMonitor.MainPos, (tmpMonitor.MainPos + tmpMonitor.MainSize));

        POINT               mouse_screen_pos = { };
        if (::GetCursorPos (&mouse_screen_pos))
        {
          ImVec2 os_pos = ImVec2( (float)mouse_screen_pos.x,
                                  (float)mouse_screen_pos.y );
          if (t.Contains (os_pos))
          {
            rectCursorMonitor = t;
            //SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? tmpMonitor.DpiScale : 1.0f;
          }
        }
      }
    }

    SKIF_vecServiceMode     = SKIF_vecServiceModeDefault  * SKIF_ImGui_GlobalDPIScale;
    SKIF_vecHorizonMode     = SKIF_vecHorizonModeAdjusted * SKIF_ImGui_GlobalDPIScale;
    SKIF_vecRegularMode     = SKIF_vecRegularModeAdjusted * SKIF_ImGui_GlobalDPIScale;

    // Add support for an even smaller regular mode, at 800x675, but only if the regular size 1000x944 can't be used
    if ((SKIF_vecRegularMode.x > monitor_extent.GetWidth () ||
         SKIF_vecRegularMode.y > monitor_extent.GetHeight()))
    {
      SKIF_vecRegularMode.x = 800.0f * SKIF_ImGui_GlobalDPIScale;
      SKIF_vecRegularMode.y = 675.0f * SKIF_ImGui_GlobalDPIScale;
    }

    SKIF_vecServiceMode     = ImFloor (SKIF_vecServiceMode);
    SKIF_vecHorizonMode     = ImFloor (SKIF_vecHorizonMode);
    SKIF_vecRegularMode     = ImFloor (SKIF_vecRegularMode);

    /*
    SKIF_vecCurrentMode =
                  (_registry.bMiniMode) ? SKIF_vecServiceMode :
                  (_registry.bHorizonMode) ? SKIF_vecHorizonMode :
                                             SKIF_vecRegularMode ;
    */

    // F6 to toggle DPI scaling
    if (changedHiDPIScaling || hotkeyF6)
    {
      // We only change bDPIScaling if ImGui::Checkbox (settings tab) was not used,
      //   as otherwise it have already been changed to reflect its new value
      if (! changedHiDPIScaling)
        _registry.bDPIScaling =        ! _registry.bDPIScaling;
      _registry.regKVDPIScaling.putData (_registry.bDPIScaling);

      changedHiDPIScaling = false;

      // Take the current display into account
      HMONITOR monitor =
        ::MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);
        
      SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? ImGui_ImplWin32_GetDpiScaleForMonitor (monitor) : 1.0f;

      // Divide the window size with its associated DPI scale to get the base size, then multiply with the new DPI scale
      SKIF_vecCurrentModeNext = (SKIF_vecCurrentMode / SKIF_ImGui_GlobalDPIScale_Last) * SKIF_ImGui_GlobalDPIScale;

      ImGuiStyle              newStyle;
      SKIF_ImGui_SetStyle   (&newStyle);

      SKIF_ImGui_AdjustAppModeSize (monitor);

      if (IsZoomed (SKIF_ImGui_hWnd))
        repositionToCenter   = true;
    }

    // Restore the last remembered window size on launch,
    //   but only if we are not running in service mode!
    static bool
        applySizeOnLaunch = true;
    if (applySizeOnLaunch)
    {   applySizeOnLaunch = false;

      if (! _registry.bMiniMode && _registry.iUIWidth > 0 && _registry.iUIHeight > 0)
        SKIF_vecCurrentModeNext =
                       ImVec2 (static_cast<float> (_registry.iUIWidth),
                               static_cast<float> (_registry.iUIHeight));
      else
        SKIF_vecCurrentModeNext =
                      (_registry.bMiniMode) ? SKIF_vecServiceMode :
                      (_registry.bHorizonMode) ? SKIF_vecHorizonMode :
                                                 SKIF_vecRegularMode ;
    }

    // SKIF_vecCurrentModeNext 1/2
    if (SKIF_vecCurrentModeNext.x != 0.0f)
    {
      // Shrink the window on low-res displays (will be applied on the next frame)
      // Emulates auto-horizon mode
      if (ImGui::GetFrameCount() > 2 &&
          (SKIF_vecCurrentModeNext.x > monitor_extent.GetWidth () ||
           SKIF_vecCurrentModeNext.y > monitor_extent.GetHeight()))
      {
        float arWindow = SKIF_vecCurrentModeNext.x / SKIF_vecCurrentModeNext.y;

        if (monitor_extent.GetWidth() < SKIF_vecCurrentModeNext.x)
        {
          SKIF_vecCurrentModeNext.x = monitor_extent.GetWidth();
          SKIF_vecCurrentModeNext.y = SKIF_vecCurrentModeNext.x / arWindow;
        }

        if (monitor_extent.GetHeight() < SKIF_vecCurrentModeNext.y)
        {
          SKIF_vecCurrentModeNext.y = monitor_extent.GetHeight();
          SKIF_vecCurrentModeNext.x = SKIF_vecCurrentModeNext.y * arWindow;
        }
      }

      SKIF_vecCurrentMode = SKIF_vecCurrentModeNext;
    }

    static bool newHorizonMode = _registry.bHorizonMode;
    static bool newServiceMode = _registry.bMiniMode;

    // Change mode based on window size
    static ImVec2
        last_size  = ImVec2 (0, 0);
    if (last_size != SKIF_vecCurrentMode)
    {   last_size  = SKIF_vecCurrentMode;

      // Regular mode
      if (     SKIF_vecCurrentMode.x >= SKIF_vecRegularMode.x &&
               SKIF_vecCurrentMode.y >= SKIF_vecRegularMode.y)
      {
        newServiceMode = false;
        newHorizonMode = false;
      }

      // Enable service mode
      else if (SKIF_vecCurrentMode.x <= SKIF_vecServiceMode.x * 1.2f &&
               SKIF_vecCurrentMode.y <= SKIF_vecServiceMode.y * 1.2f)
      {
        newServiceMode = true;
      }

      // Enable horizon mode (when above a certain width *and* below a certain height)
      else //if (SKIF_vecCurrentMode.x >= SKIF_vecHorizonMode.x * 0.6f &&
           //    SKIF_vecCurrentMode.y <= SKIF_vecHorizonMode.y * 1.2f)
      {
        newServiceMode = false;
        newHorizonMode = true;
      }

      // Enforce monitor boundaries on resizes (once the mouse button is let go)
      //RespectMonBoundaries = true;
    }

    // Apply new service mode state
    if (newServiceMode != _registry.bMiniMode)
    {
      _registry.bMiniMode = newServiceMode;
      _registry._ExitOnInjection = false;

      PLOG_DEBUG << "Switched to " << ((_registry.bMiniMode) ? "Service mode" : "App mode");

      SKIF_ImGui_AdjustAppModeSize (NULL);

      if (IsZoomed (SKIF_ImGui_hWnd))
        repositionToCenter   = true;

      if (SteamOverlayDisabled)
      {
        PLOG_INFO << "Removing the SteamNoOverlayUIDrawing variable to prevent pollution...";
        SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);
        SteamOverlayDisabled = false;
      }

      // If we switch to small mode, close all popups
      if (newServiceMode)
      {
        ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
      }

      // If we switch back to large mode, re-open a few specific ones
      else {
        if (AddGamePopup == PopupState_Opened)
          AddGamePopup = PopupState_Open;

        if (UpdatePromptPopup == PopupState_Opened)
          UpdatePromptPopup = PopupState_Open;

        if (HistoryPopup == PopupState_Opened)
          HistoryPopup = PopupState_Open;

        // If SKIF was used as a launcher, initialize stuff that we did not set up while in the small mode
        if (_Signal.Launcher || _Signal.LauncherURI || _Signal.Quit || _Signal.ServiceMode)
        {   _Signal.Launcher =  _Signal.LauncherURI =  _Signal.Quit =  _Signal.ServiceMode = false;

          // Register HDR toggle hotkey (if applicable)
          SKIF_Util_RegisterHotKeyHDRToggle (&_registry.kbToggleHDRDisplay);

          // Register service (auto-stop) hotkey
          SKIF_Util_RegisterHotKeySVCTemp   (&_registry.kbStartService);

          /*
          // Check for the presence of an internet connection
          if (SUCCEEDED (hrNLM))
          {
            VARIANT_BOOL connStatus = 0;
            dwNLM = SKIF_Util_timeGetTime ( );

            PLOG_DEBUG << "Checking for an online connection...";
            if (SUCCEEDED (pNLM->get_IsConnectedToInternet (&connStatus)))
              SKIF_NoInternet = (VARIANT_FALSE == connStatus);
          }
          */

          static bool
              runOnce = true;
          if (runOnce)
          {   runOnce = false;

            // Kickstart the update thread
            _updater.CheckForUpdates ( );
          }
        }
      }
    }

    // Apply new horizon mode state
    // Should auto-engage at a window size of 625x820px or above
    else if (newHorizonMode != _registry.bHorizonMode)
    {
      _registry.bHorizonMode  =                   newHorizonMode;
      _registry.regKVHorizonMode.putData (_registry.bHorizonMode);

      PLOG_DEBUG << "Switched to " << ((_registry.bHorizonMode) ? "Horizon mode" : "App mode");

      SKIF_ImGui_AdjustAppModeSize (NULL);

      if (IsZoomed (SKIF_ImGui_hWnd))
        repositionToCenter   = true;
    }

    // Apply any changes to the ImGui style
    // Do it at the beginning of frames to prevent ImGui::Push... from affecting the styling
    // Note that Win11 rounded border color won't be applied until after a restart
      
    // F7 to cycle between color themes
    if ( (_registry.iStyleTemp != _registry.iStyle) || hotkeyF7)
    {
      _registry.iStyle            = (_registry.iStyleTemp != _registry.iStyle)
                                  ?  _registry.iStyleTemp
                                  : (_registry.iStyle + 1) % UIStyle_COUNT;
      _registry.regKVStyle.putData  (_registry.iStyle);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);

      _registry.iStyleTemp = _registry.iStyle;

      extern void
        SKIF_ImGui_ImplWin32_UpdateDWMBorders (void);
        SKIF_ImGui_ImplWin32_UpdateDWMBorders (    );
    }

    // Registry watch to check if snapping/drag from window settings has changed in Windows
    // No need to for SKIF to wake up on changes when unfocused, so skip having it be global
    /*
    static SKIF_RegistryWatch
      dwmWatch ( HKEY_CURRENT_USER,
                   LR"(Control Panel\Desktop)",
                     L"WinDesktopNotify", FALSE, REG_NOTIFY_CHANGE_LAST_SET, UITab_None, false, true);

    // When the registry is changed, update our internal state accordingly
    if (dwmWatch.isSignaled ( ))
    {
      _registry.bMaximizeOnDoubleClick   =
        SKIF_Util_GetDragFromMaximized (true)                // IF the OS prerequisites are enabled
        ? _registry.regKVMaximizeOnDoubleClick.hasData ( )   // AND we have data in the registry
          ? _registry.regKVMaximizeOnDoubleClick.getData ( ) // THEN use the data,
          : true                                             // otherwise default to true,
        : false;                                             // and false if OS prerequisites are disabled
    }
    */

    // F8 to toggle UI borders
    if (hotkeyF8)
    {
      _registry.bUIBorders = ! _registry.bUIBorders;
      _registry.regKVUIBorders.putData (_registry.bUIBorders);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    // F9 to cycle between color depths
    if (hotkeyF9)
    {
      if (_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive())
        _registry.iHDRMode = 1 + (_registry.iHDRMode % 2); // Cycle between 1 (10 bpc) and 2 (16 bpc)
      else 
        _registry.iSDRMode = (_registry.iSDRMode + 1) % 3; // Cycle between 0 (8 bpc), 1 (10 bpc), and 2 (16 bpc)

      RecreateSwapChains = true;
    }

    // Should we invalidate the fonts and/or recreate them?

    if (SKIF_ImGui_GlobalDPIScale_Last != SKIF_ImGui_GlobalDPIScale)
    {   SKIF_ImGui_GlobalDPIScale_Last  = SKIF_ImGui_GlobalDPIScale;
      invalidateFonts = true;
    }

    float fontScale = 18.0F * SKIF_ImGui_GlobalDPIScale;
    if (fontScale < 15.0F)
      fontScale += 1.0F;

    if (invalidateFonts)
    {
      invalidateFonts = false;
      SKIF_ImGui_InvalidateFonts ( );
    }
    
    // This occurs on the next frame, as failedLoadFonts gets evaluated and set as part of ImGui_ImplDX11_NewFrame
    else if (failedLoadFonts)
    {
      // This scenario should basically never happen nowadays that SKIF only loads the specific characters needed from each character set

      SKIF_ImGui_InvalidateFonts ( );

      failedLoadFonts       = false;
      failedLoadFontsPrompt = true;
    }

    //temp_time = SKIF_Util_timeGetTime1();
    //PLOG_INFO << "Operation took " << (temp_time - SKIF_Util_timeGetTime1()) << " ms.";

#pragma region New UI Frame

#if 0
    if (RecreateSwapChains)
    {
      // If the device have been removed/reset/hung, we need to invalidate all resources
      if (FAILED (SKIF_pd3dDevice->GetDeviceRemovedReason ( )))
      {
        // Invalidate resources
        ImGui_ImplDX11_InvalidateDeviceObjects ( );
        ImGui_ImplDX11_InvalidateDevice        ( );
        CleanupDeviceD3D                       ( );

        // Signal to ImGui_ImplDX11_NewFrame() that the swapchains needs recreating
        RecreateSwapChains = true;

        // Recreate
        CreateDeviceD3D                        (SKIF_Notify_hWnd);
        ImGui_ImplDX11_Init                    (SKIF_pd3dDevice, SKIF_pd3dDeviceContext);

        // This is used to flag that rendering should not occur until
        // any loaded textures and such also have been unloaded
        invalidatedDevice = 1;
      }
    }
#endif

    extern bool
      SKIF_ImGui_ImplWin32_WantUpdateMonitors (void);
    bool _WantUpdateMonitors =
      SKIF_ImGui_ImplWin32_WantUpdateMonitors (    );

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame  (); // (Re)create individual swapchain windows
    ImGui_ImplWin32_NewFrame (); // Handle input
    ImGui::NewFrame          ();
    {
      SKIF_FrameCount.store(ImGui::GetFrameCount());

      if (SKIF_vecCurrentModeNext.x != 0.0f &&
          SKIF_vecCurrentModeNext   != SKIF_vecCurrentMode)
      {
        SKIF_vecCurrentModeNext.x = 0.0f;

        SKIF_vecCurrentMode = SKIF_vecCurrentModeNext;
      }

      // SKIF_vecCurrentModeNext 2/2
      if (SKIF_vecCurrentModeNext.x != 0.0f)
      {   SKIF_vecCurrentModeNext.x  = 0.0f;
        ImGui::SetNextWindowSizeConstraints (SKIF_vecCurrentMode, SKIF_vecCurrentMode);
      }

      else
        ImGui::SetNextWindowSizeConstraints (SKIF_vecServiceMode, ImVec2 (FLT_MAX, FLT_MAX));

      ImGui::SetNextWindowClass (&SKIF_AppWindow);

      // Restore the last remembered window position on launch
      static bool
          applyPositionOnLaunch = true;
      if (applyPositionOnLaunch)
      {   applyPositionOnLaunch = false;

        if (_registry.iUIPositionX != -1 &&
            _registry.iUIPositionY != -1)
          ImGui::SetNextWindowPos (ImVec2 (static_cast<float> (_registry.iUIPositionX),
                                           static_cast<float> (_registry.iUIPositionY)));
      }

      // RepositionSKIF -- Step 2: Repositon the window
      // Repositions the window in the center of the monitor the cursor is currently on
      if (RepositionSKIF)
        ImGui::SetNextWindowPos (ImVec2 (rectCursorMonitor.GetCenter().x - (SKIF_vecCurrentMode.x / 2.0f),
                                         rectCursorMonitor.GetCenter().y - (SKIF_vecCurrentMode.y / 2.0f)));

      // Calculate new window boundaries and changes to fit within the workspace if it doesn't fit
      //   Delay running the code to on the third frame to allow other required parts to have already executed...
      //     Otherwise window gets positioned wrong on smaller monitors !
      if (RespectMonBoundaries && ImGui::GetFrameCount() > 2 && ! ImGui::IsAnyMouseDown ( ))
      {   RespectMonBoundaries = false;

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
          ImGui::SetNextWindowPos (newWindowPos);
      }

      // If toggling mode when maximized, we need to reposition the window
      if (repositionToCenter && ! ImGui::IsAnyMouseDown ( ))
      {   repositionToCenter = false;
        ImGui::SetNextWindowPos  (monitor_extent.GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      }
      
      ImGui::Begin ( SKIF_WINDOW_TITLE_SHORT_A SKIF_WINDOW_HASH,
                       nullptr,
                       //ImGuiWindowFlags_NoResize          |
                         ImGuiWindowFlags_NoCollapse        |
                         ImGuiWindowFlags_NoTitleBar        |
                         ImGuiWindowFlags_NoScrollbar       | // Hide the scrollbar for the main window
                         ImGuiWindowFlags_NoScrollWithMouse | // Prevent scrolling with the mouse as well
                         ImGuiWindowFlags_NoSavedSettings     // We handle size/position persistently on our own
                      // ImGuiWindowFlags_NoMove              // This was added in #8bf06af, but I am unsure why.
                      // The only comment is that it was DPI related? This prevents Ctrl+Tab from moving the window so must not be used
      );

      SK_RunOnce (ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems += 2);

      HiddenFramesRemaining          = ImGui::GetCurrentWindowRead()->HiddenFramesCannotSkipItems;
      HiddenFramesContinueProcessing = (HiddenFramesRemaining > 0);
      HoverTipActive = false;

      extern ImGuiPlatformMonitor*
        SKIF_ImGui_ImplWin32_GetPlatformMonitorProxy (ImGuiViewport* viewport, bool center);
      ImGuiPlatformMonitor* actMonitor =
        SKIF_ImGui_ImplWin32_GetPlatformMonitorProxy (ImGui::GetWindowViewport ( ), true);

      // Crop the window on resolutions with a height smaller than what SKIF requires
      if (actMonitor != nullptr)
      {
        static ImGuiPlatformMonitor* preMonitor = nullptr;

        // Only act once at launch or if we are, in fact, on a new display
        if (preMonitor != actMonitor || _WantUpdateMonitors)
        {
          // This is only necessary to run once on launch, to account for the startup display DPI
          SK_RunOnce (SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? ImGui::GetWindowViewport()->DpiScale : 1.0f);

          //ImVec2 tmpCurrentSize  = (_registry.bMiniMode) ? SKIF_vecServiceModeDefault  :
          //                         (_registry.bHorizonMode) ? SKIF_vecHorizonModeAdjusted :
          //                                                    SKIF_vecRegularModeAdjusted ;

          // 2024-05-07: Temporarily limited to only run once on launch and no more,
          //               due to causing issues when snap-moving the window between displays!
          if (preMonitor == nullptr)
          {
            // Divide the window size with its associated DPI scale to get the base size, then multiply with the new DPI scale
            SKIF_vecCurrentModeNext = (SKIF_vecCurrentMode / SKIF_ImGui_GlobalDPIScale_Last) * SKIF_ImGui_GlobalDPIScale;

            //if (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale > (actMonitor->WorkSize.y))
            //  SKIF_vecAlteredSize.y = (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale - (actMonitor->WorkSize.y)); // (actMonitor->WorkSize.y - 50.0f)

            float arWindow = SKIF_vecCurrentModeNext.x / SKIF_vecCurrentModeNext.y;

            if (actMonitor->WorkSize.x < SKIF_vecCurrentModeNext.x)
            {
              SKIF_vecCurrentModeNext.x = actMonitor->WorkSize.x;
              SKIF_vecCurrentModeNext.y = SKIF_vecCurrentModeNext.x / arWindow;
            }

            if (actMonitor->WorkSize.y < SKIF_vecCurrentModeNext.y)
            {
              SKIF_vecCurrentModeNext.y = actMonitor->WorkSize.y;
              SKIF_vecCurrentModeNext.x = SKIF_vecCurrentModeNext.y * arWindow;
            }

            // Ensure we keep within the monitor borders
            RespectMonBoundaries = true;
          }

          // Also recreate the swapchain (applies any HDR/SDR changes between displays)
          //   but not the first time to prevent unnecessary swapchain recreation on launch
          if (preMonitor != nullptr)
            RecreateSwapChains = true;

          preMonitor = actMonitor;
        }
      }

      // RepositionSKIF -- Step 3: The Final Step -- Prevent the global DPI scale from potentially being set to outdated values
      if (RepositionSKIF && ImGui::GetFrameCount() > 2)
        RepositionSKIF = false;

      if (hotkeyCtrlR || hotkeyF5)
      {
        if (SKIF_Tab_Selected == UITab_Library)
          RepopulateGames   = true;

        if (SKIF_Tab_Selected == UITab_Settings)
          RefreshSettingsTab = true;
      }

      // Only allow navigational hotkeys when in Large Mode and as long as no popups are opened
      if (! _registry.bMiniMode && ! SKIF_ImGui_IsAnyPopupOpen ( ))
      {
        int gamepad_tab = 0;

        if (ImGui::IsKeyPressed (ImGuiKey_GamepadL1)) gamepad_tab--;
        if (ImGui::IsKeyPressed (ImGuiKey_GamepadR1)) gamepad_tab++;
        
        if (hotkeyCtrl1)
        {
          if (SKIF_Tab_Selected != UITab_Library)
              SKIF_Tab_ChangeTo  = UITab_Library;
        }

        if (hotkeyCtrl2)
        {
          if (SKIF_Tab_Selected != UITab_Monitor)
              SKIF_Tab_ChangeTo  = UITab_Monitor;
        }

        if (hotkeyCtrl3)
        {
          if (SKIF_Tab_Selected != UITab_Settings)
              SKIF_Tab_ChangeTo  = UITab_Settings;
        }

        if (hotkeyCtrl4)
        {
          if (SKIF_Tab_Selected != UITab_About)
              SKIF_Tab_ChangeTo  = UITab_About;
        }

        if (gamepad_tab != 0)
        {
          int tab_select =
            SKIF_Tab_Selected + gamepad_tab;

          if (tab_select < UITab_Library)
              tab_select = UITab_About;
          if (tab_select > UITab_About)
              tab_select = UITab_Library;

          if (tab_select != SKIF_Tab_Selected)
          {
            SKIF_Tab_ChangeTo = (UITab)tab_select;
          }
        }

        if (hotkeyCtrlA && allowShortcutCtrlA)
        {
          if (SKIF_Tab_Selected != UITab_Library)
              SKIF_Tab_ChangeTo  = UITab_Library;

          AddGamePopup = PopupState_Open;
        }
      }

      allowShortcutCtrlA = true;

      if (ImGui::IsKeyPressed (ImGuiKey_Escape))
      {
        if (AddGamePopup        != PopupState_Closed ||
            ModifyGamePopup     != PopupState_Closed ||
            PopupCategoryModify != PopupState_Closed ||
            RemoveGamePopup     != PopupState_Closed ||
            UpdatePromptPopup   != PopupState_Closed ||
            HistoryPopup        != PopupState_Closed  )
        {
          AddGamePopup         = PopupState_Closed;
          ModifyGamePopup      = PopupState_Closed;
          PopupCategoryModify  = PopupState_Closed;
          RemoveGamePopup      = PopupState_Closed;
          UpdatePromptPopup    = PopupState_Closed;
          HistoryPopup         = PopupState_Closed;

          ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
        }
      }

      // Begin Mini Mode
#pragma region UI: Mini Mode

      if (_registry.bMiniMode)
      {
        SKIF_Tab_Selected = UITab_MiniMode;

        auto miniMode_id =
          ImGui::GetID ("###Small_Mode_Frame");

        // A new line to pad the top a bit
        ImGui::NewLine ();

        SKIF_ImGui_BeginChildFrame ( miniMode_id,
          ImVec2 ( 400.0f * SKIF_ImGui_GlobalDPIScale,
                    12.0f * ImGui::GetTextLineHeightWithSpacing () ),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NavFlattened      |
            ImGuiWindowFlags_NoScrollbar       |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground
        );

        _inject._GlobalInjectionCtl ( );

        ImGui::EndChild             ( );

        SKIF_ImGui_ServiceMenu      ( );

      } // End Mini Mode

#pragma endregion

      // Begin Regular Mode
#pragma region UI: Large Mode

      else
      {
        ImGui::BeginTabBar ( "###SKIF_TAB_BAR",
                               ImGuiTabBarFlags_FittingPolicyResizeDown |
                               ImGuiTabBarFlags_FittingPolicyScroll );

        if (ImGui::BeginTabItem (" " ICON_FA_GAMEPAD " Library ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_Library) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          if (! _registry.bFirstLaunch)
          {
            // Select the About tab on first launch
            _registry.bFirstLaunch = ! _registry.bFirstLaunch;
            SKIF_Tab_ChangeTo = UITab_About;

            // Store in the registry so this only occur once.
            _registry.regKVFirstLaunch.putData(_registry.bFirstLaunch);
          }

          extern float fTint;
          if (SKIF_Tab_Selected != UITab_Library)
          {
            // Reset the dimmed cover when going back to the tab
            if (_registry.iDimCovers == 2)
              fTint = 0.75f;

            PLOG_DEBUG << "Switched to tab: Library";
          }

          // Ensure we have set up the drop target on the Library tab
          if (SKIF_ImGui_hWnd != NULL)
            _drag_drop.Register (SKIF_ImGui_hWnd);

          SKIF_Tab_Selected = UITab_Library;
          if (SKIF_Tab_ChangeTo == UITab_Library)
              SKIF_Tab_ChangeTo  = UITab_None;

          extern void
            SKIF_UI_Tab_DrawLibrary (void);
            SKIF_UI_Tab_DrawLibrary (     );
            
          ImGui::EndChild         ( );
          ImGui::EndTabItem       ( );
        }

        // Disable the drop target when navigating away from the library tab
        else
          _drag_drop.Revoke (SKIF_ImGui_hWnd);


        if (ImGui::BeginTabItem (" " ICON_FA_MICROCHIP " Monitor ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_Monitor) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          if (SKIF_Tab_Selected != UITab_Monitor)
            PLOG_DEBUG << "Switched to tab: Monitor";

          extern void
            SKIF_UI_Tab_DrawMonitor (void);
            SKIF_UI_Tab_DrawMonitor (    );

          ImGui::EndChild         ( );
          ImGui::EndTabItem       ( );
        }

        // Unload the SpecialK DLL file if the tab is not selected
        else if (hModSpecialK != 0)
        {
          FreeLibrary (hModSpecialK);
          hModSpecialK = nullptr;
        }

        if (ImGui::BeginTabItem (" " ICON_FA_LIST_CHECK " Settings ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_Settings) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          if (SKIF_Tab_Selected != UITab_Settings)
            PLOG_DEBUG << "Switched to tab: Settings";

          SKIF_UI_Tab_DrawSettings( );

          // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
          SKIF_ImGui_AutoScroll  (true, SKIF_ImGuiAxis_Y);

          ImGui::EndChild         ( );
          ImGui::EndTabItem       ( );
        }
        
        if (ImGui::BeginTabItem (" " ICON_FA_COMMENT " About ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_About) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          if (SKIF_Tab_Selected != UITab_About)
            PLOG_DEBUG << "Switched to tab: About";

          SKIF_Tab_Selected = UITab_About;
          if (SKIF_Tab_ChangeTo == UITab_About)
              SKIF_Tab_ChangeTo  = UITab_None;

          // About Tab
          SKIF_UI_Tab_DrawAbout   ( );

          ImGui::EndChild         ( );
          ImGui::EndTabItem       ( );
        }

        // TODO: Change to a period refresh when focused (every 500ms or so) which then after
        //   having been == Game Mode a couple of times (5+ seconds) prompts SKIF to warn about it
        //if (SKIF_Util_GetEffectivePowerMode ( ) != "None")
        //  ImGui::TextColored (ImVec4 (0.5f, 0.5f, 0.5f, 1.f), SK_FormatString (R"(%s (%s))", SKIF_WINDOW_TITLE_A, SKIF_Util_GetEffectivePowerMode ( ).c_str ( ) ).c_str ( ));

        ImGui::EndTabBar          ( );
      } // End Regular Mode

#pragma endregion

#pragma region StatusBar

        // Status Bar at the bottom
      if (! _registry.bMiniMode && _registry.bUIStatusBar)
      {
        // This counteracts math performed on SKIF_vecRegularMode.y at the beginning of the frame
        ImGui::SetCursorPosY (ImGui::GetCursorPosY ( ) - 2.0f * SKIF_ImGui_GlobalDPIScale);
        ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 0.0f);

        // Begin Add Game
        ImVec2 tmpPos = ImGui::GetCursorPos ( );

        // Prevents selecting the Add Game or Filter button with a keyboard or gamepad (fixes awkward and annoying nav selection)
        if (SKIF_Tab_Selected == UITab_Library)
          ImGui::PushItemFlag   (ImGuiItemFlags_NoNav, true);

        static bool btnHovered  = false;
        ImGui::PushStyleColor (ImGuiCol_Button,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
        ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
        ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

        if (btnHovered)
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption)); //ImVec4(1, 1, 1, 1));
        else
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)); //ImVec4(0.5f, 0.5f, 0.5f, 1.f));

        if (ImGui::Button   (ICON_FA_SQUARE_PLUS " Add Game"))
        {
          AddGamePopup = PopupState_Open;
          if (SKIF_Tab_Selected != UITab_Library)
            SKIF_Tab_ChangeTo = UITab_Library;
        }

        btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

        ImGui::PopStyleColor   (4); // ImGuiCol_Text, ImGuiCol_ButtonActive, ImGuiCol_ButtonHovered, ImGuiCol_Button

        if (SKIF_Tab_Selected == UITab_Library)
          ImGui::PopItemFlag     ( );

        ImGui::SetCursorPos    (tmpPos);
        // End Add Game

        // Begin Pulsating Refresh Icon
        if (_updater.IsRunning ( ))
        {
          ImGui::SetCursorPosX (
            ImGui::GetCursorPosX () +
            ImGui::GetWindowSize ().x -
              ( ImGui::CalcTextSize ( ICON_FA_ROTATE ).x ) -
            ImGui::GetCursorPosX () -
            ImGui::GetStyle   ().ItemSpacing.x * 2
          );

          ImGui::SetCursorPosY ( ImGui::GetCursorPosY () + ImGui::GetStyle ( ).FramePadding.y);

          ImGui::TextColored ( ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) *
                                ImVec4 (0.75f, 0.75f, 0.75f, 0.50f + 0.5f * (float)sin (SKIF_Util_timeGetTime() * 1 * 3.14 * 2)
                                ), ICON_FA_ROTATE );
        }

        ImGui::SetCursorPos(tmpPos);
        // End Refresh Icon

        // Begin Status Bar Text
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

        ImGui::SetCursorPosY ( ImGui::GetCursorPosY () + ImGui::GetStyle ( ).FramePadding.y);

        ImGui::TextColored ( ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4 (0.75f, 0.75f, 0.75f, 1.00f),
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

        // End Status Bar Text

        ImGui::PopStyleVar ();
      }

#pragma endregion

#pragma region Shelly the Ghost

      if (_registry.iGhostVisibility == 1 ||
         (_registry.iGhostVisibility == 2 && _inject.bCurrentState))
      {
        ImVec2 preShellyPos =
          ImGui::GetCursorPos ();

        ImVec2 shelly_movable_area = ImVec2 (
            250.0f * SKIF_ImGui_GlobalDPIScale,
            ImGui::CalcTextSize (ICON_FA_GHOST).y + 4.0f * SKIF_ImGui_GlobalDPIScale
        );

        ImGui::SetCursorPos (ImVec2 (
              ImGui::GetWindowContentRegionMax().x / 2 - shelly_movable_area.x / 2,
              10.0f * SKIF_ImGui_GlobalDPIScale
        ));
        
        ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2());
        ImGui::PushStyleVar (ImGuiStyleVar_FramePadding,  ImVec2());
        bool shelly_show = ImGui::BeginChild ("###SKIV_SHELLY", shelly_movable_area, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMouseInputs);
        ImGui::PopStyleVar  (2);

        if (shelly_show)
          SKIF_UI_DrawShellyTheGhost ( );

        ImGui::EndChild ( );

        ImGui::SetCursorPos (preShellyPos);
      }

#pragma endregion

#pragma region WindowButtons

      // 164px - regular mode
      // 116px - service mode

      ImVec2 window_btn_size = ImVec2 (
        164.0f,
         24.0f
      );

      bool showBtnHorizon  = true,
           showBtnService  = true,
           showBtnMinimize = true;

      // If in service mode
      if (_registry.bMiniMode)
      {
        showBtnHorizon = false;
        window_btn_size.x -= 48.0f; // -48px due to one less button
      }

      // Regular mode: Three less buttons
      else if (ImGui::GetWindowSize ().x < 450.0f * SKIF_ImGui_GlobalDPIScale)
      {
        showBtnHorizon = false;
        window_btn_size.x -= 48.0f;
        showBtnService = false;
        window_btn_size.x -= 48.0f;
        showBtnMinimize = false;
        window_btn_size.x -= 38.0f; // Special handling for this one
      }

      // Regular mode: Two less buttons
      else if (ImGui::GetWindowSize ().x < 500.0f * SKIF_ImGui_GlobalDPIScale)
      {
        showBtnHorizon = false;
        window_btn_size.x -= 48.0f;
        showBtnService = false;
        window_btn_size.x -= 48.0f;
      }

      // Regular mode: One less button
      else if (ImGui::GetWindowSize ().x < 550.0f * SKIF_ImGui_GlobalDPIScale ||
               monitor_extent.GetWidth () < SKIF_vecRegularMode.x || // Hide the Horizon button if the display is only large enough to fit it
               monitor_extent.GetHeight() < SKIF_vecRegularMode.y)
      {
        showBtnHorizon = false;
        window_btn_size.x -= 48.0f;
      }

      window_btn_size *= SKIF_ImGui_GlobalDPIScale;

      ImVec2 prevCursorPos =
        ImGui::GetCursorPos ();

      ImVec2 window_btn_pos = ImFloor (
        ImVec2 ( (ImGui::GetWindowContentRegionMax().x - window_btn_size.x ),
                ((ImGui::GetStyle().WindowPadding.y) - ImGui::GetStyle().FrameBorderSize * 2 - ImGui::GetStyle().WindowBorderSize * 2))
      );

      ImGui::SetCursorPos (window_btn_pos);

      if (ImGui::BeginChild ("###WINDOW_BUTTONS", window_btn_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground))
      {
        ImGui::PushStyleVar (
          ImGuiStyleVar_FrameRounding, 25.0f * SKIF_ImGui_GlobalDPIScale
        );

        if (showBtnHorizon)
        {
          if (ImGui::Button ((_registry.bHorizonMode ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_UP), ImVec2 ( 40.0f * SKIF_ImGui_GlobalDPIScale, 0.0f ))) //
          {
            newHorizonMode = ! _registry.bHorizonMode;

            // Changes the app window to the proper size
            SKIF_vecCurrentModeNext = (newHorizonMode) ? SKIF_vecHorizonMode : SKIF_vecRegularMode;
          }

          SKIF_ImGui_SetHoverTip ("Toggle horizontal mode");

          ImGui::SameLine ();
        }

        if (showBtnService)
        {
          if (ImGui::Button ((_registry.bMiniMode) ? ICON_FA_EXPAND : ICON_FA_COMPRESS, ImVec2 ( 40.0f * SKIF_ImGui_GlobalDPIScale, 0.0f ))) // ICON_FA_MAXIMIZE : ICON_FA_MINIMIZE
            hotkeyCtrlT = true;

          SKIF_ImGui_SetHoverTip ("Toggle mini mode");

          ImGui::SameLine ();
        }

        if (showBtnMinimize)
        {
          if (ImGui::Button (ICON_FA_WINDOW_MINIMIZE, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale, 0.0f )))
            hotkeyCtrlN = true;

          ImGui::SameLine ();
        }

        ImGui::PushStyleColor   (ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure));
        ImGui::PushStyleColor   (ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure) * ImVec4(1.2f, 1.2f, 1.2f, 1.0f));

        static bool closeButtonHoverActive = false;

        if (_registry._StyleLightMode && closeButtonHoverActive)
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImVec4 (0.9F, 0.9F, 0.9F, 1.0f));

        if (ImGui::Button (ICON_FA_XMARK, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale, 0.0f ) )) // HotkeyEsc is situational
          hotkeyCtrlQ = true;
      
        if (_registry._StyleLightMode)
        {
          if (closeButtonHoverActive)
            ImGui::PopStyleColor ( );
          
          closeButtonHoverActive = (ImGui::IsItemHovered () || ImGui::IsItemActivated ());
        }

        ImGui::PopStyleColor (2);

        ImGui::PopStyleVar ();

        // Tooltip for close button
        if (_registry.bCloseToTray)
          SKIF_ImGui_SetHoverTip ("This app will close to the notification area.");
        else if (_inject.bCurrentState && _registry.bAllowBackgroundService)
          SKIF_ImGui_SetHoverTip ("Service continues running after this app is closed.");
      }

      ImGui::EndChild ( );

      ImGui::SetCursorPos (prevCursorPos);

      ImGui::Dummy (ImVec2 (0, 0)); // Dummy required here to solve ImGui::ErrorCheckUsingSetCursorPosToExtendParentBoundaries()

#pragma endregion

#pragma region CaptionActions

      if (hotkeyCtrlT || hotkeyF11)
      {
        newServiceMode = ! _registry.bMiniMode;

        // Changes the app window to the proper size
        SKIF_vecCurrentModeNext = (newServiceMode) ? SKIF_vecServiceMode :
                          (_registry.bHorizonMode) ? SKIF_vecHorizonMode :
                                                     SKIF_vecRegularMode ;
      }

      if (hotkeyCtrlN)
        ShowWindow (SKIF_ImGui_hWnd, SW_MINIMIZE);

      if (hotkeyCtrlQ || hotkeyCtrlW || bKeepWindowAlive == false)
      {
        if (_registry.bCloseToTray && bKeepWindowAlive && ! SKIF_isTrayed)
        {
          bKeepWindowAlive = true;
          ShowWindow       (SKIF_ImGui_hWnd, SW_MINIMIZE);
          ShowWindow       (SKIF_ImGui_hWnd, SW_HIDE);
          UpdateWindow     (SKIF_ImGui_hWnd);
          SKIF_isTrayed    = true;
        }

        else
        {
          if (_inject.bCurrentState && ! _registry.bAllowBackgroundService )
            _inject._StartStopInject (true);

          bKeepProcessAlive = false;
        }
      }

#pragma endregion


      // Font warning
      if (failedLoadFontsPrompt && !HiddenFramesContinueProcessing)
      {
        failedLoadFontsPrompt = false;

        ImGui::OpenPopup ("###FailedFontsPopup");
      }
      

      float fFailedLoadFontsWidth = 400.0f * SKIF_ImGui_GlobalDPIScale;
      ImGui::SetNextWindowSize (ImVec2 (fFailedLoadFontsWidth, 0.0f));
      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      if (ImGui::BeginPopupModal ("Fonts failed to load###FailedFontsPopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
      {
        ImGui::TreePush    ("FailedFontsTreePush");

        SKIF_ImGui_Spacing ( );

        ImGui::TextWrapped ("The selected character sets failed to load due to system limitations and have been reset.");
        ImGui::NewLine     ( );
        ImGui::TextWrapped ("Please limit the selection to only the most essential.");

        SKIF_ImGui_Spacing ( );
        SKIF_ImGui_Spacing ( );

        ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

        ImGui::SetCursorPosX (fFailedLoadFontsWidth / 2 - vButtonSize.x / 2);

        if (ImGui::Button  ("OK", vButtonSize))
          ImGui::CloseCurrentPopup ( );

        SKIF_ImGui_Spacing ( );

        ImGui::TreePop     ( );

        ImGui::EndPopup ( );
      }

      // Uses a Directory Watch signal, so this is cheap; do it once every frame.
      svcTransitionFromPendingState = _inject._TestServletRunlevel (false);

      // Another Directory Watch signal to check if DLL files should be refreshed.
      static SKIF_DirectoryWatch root_folder;
      static DWORD               root_folder_signaled = 0;
      const  DWORD               root_folder_auto_refresh = 1000;
      if (SKIF_Notify_hWnd != NULL && SKIF_ImGui_hWnd != NULL && root_folder.isSignaled (_path_cache.specialk_install, UITab_ALL, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME))
      {
        root_folder_signaled = SKIF_Util_timeGetTime ( );

        // Create a timer to trigger a refresh after the time has expired
        SetTimer (SKIF_Notify_hWnd, IDT_REFRESH_DIR_ROOT, root_folder_auto_refresh + 50, NULL);
      }

      // if we were signaled more than 5 seconds ago, do the usual refesh
      //   ... or if we are refreshing the view!
      if ((root_folder_signaled > 0 && root_folder_signaled + root_folder_auto_refresh < SKIF_Util_timeGetTime ( )) || hotkeyCtrlR || hotkeyF5)
      {   root_folder_signaled = 0;
        
        // Destroy the timer
        KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_DIR_ROOT);

        // If the Special K DLL file is currently loaded, unload it
        if (hModSpecialK != 0)
        {
          FreeLibrary (hModSpecialK);
          hModSpecialK = nullptr;
        }

        _inject._DanceOfTheDLLFiles     ();
        _inject._RefreshSKDLLVersions   ();
      }


      // Process any existing message popups
      extern void
        SKIF_ImGui_InfoMessage_Process (void);
      SKIF_ImGui_InfoMessage_Process ( );


      // Handle the update popup

      static std::wstring updateRoot = SK_FormatStringW (LR"(%ws\Version\)", _path_cache.specialk_userdata);
      static float  UpdateAvailableWidth = 0.0f;

      // Only open the update prompt after the library has appeared (fixes the popup weirdly closing for some unknown reason)
      if (PopulatedGames && UpdatePromptPopup == PopupState_Open && ! _registry.bMiniMode && ! HiddenFramesContinueProcessing && ! SKIF_ImGui_IsAnyPopupOpen ( ) && ! ImGui::IsMouseDragging (ImGuiMouseButton_Left))
      {
        //UpdateAvailableWidth = ImGui::CalcTextSize ((SK_WideCharToUTF8 (newVersion.description) + " is ready to be installed.").c_str()).x + 3 * ImGui::GetStyle().ItemSpacing.x;
        UpdateAvailableWidth = 360.0f;

        // 8.0f  per character
        // 25.0f for the scrollbar
        float calculatedWidth = static_cast<float>(_updater.GetResults().release_notes_formatted.max_length) * 8.0f + 25.0f;

        if (calculatedWidth > UpdateAvailableWidth)
          UpdateAvailableWidth = calculatedWidth;

        UpdateAvailableWidth = std::min<float> (UpdateAvailableWidth, SKIF_vecCurrentMode.x * 0.9f);

        ImGui::OpenPopup ("###UpdatePrompt");
      }

      // Update Available prompt
      // 730px    - Full popup width
      // 715px    - Release Notes width
      //  15px    - Approx. scrollbar width
      //   7.78px - Approx. character width (700px / 90 characters)
      ImGui::SetNextWindowSize (ImVec2 (UpdateAvailableWidth * SKIF_ImGui_GlobalDPIScale, 0.0f));
      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      if (ImGui::BeginPopupModal ( "Version Available###UpdatePrompt", nullptr,
                                     ImGuiWindowFlags_NoResize         |
                                     ImGuiWindowFlags_NoMove           |
                                     ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
#ifdef _WIN64
        std::string currentVersion = _inject.SKVer64_utf8;
#else
        std::string currentVersion = _inject.SKVer32_utf8;
#endif
        std::string compareLabel;
        ImVec4      compareColor;
        bool        compareNewer = (SKIF_Util_CompareVersionStrings (_updater.GetResults().version, currentVersion) > 0);

        if (UpdatePromptPopup == PopupState_Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###UpdatePrompt");
          if (window != nullptr && ! window->Appearing)
            UpdatePromptPopup = PopupState_Opened;
        }

        if (compareNewer)
        {
          compareLabel = "This version is newer than the currently installed.";
          compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
        }
        else
        {
          compareLabel = "WARNING: You are about to roll back Special K as this version is older than the currently installed!";
          compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        }

        SKIF_ImGui_Spacing ();

        std::string updateTxt = "is ready to be installed.";

        if ((_updater.GetState ( ) & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
          updateTxt = "is available for download.";

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(((_updater.GetResults().description) + updateTxt).c_str()).x + (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (compareColor, (_updater.GetResults().description).c_str());
        ImGui::SameLine ( );
        ImGui::Text (updateTxt.c_str());

        SKIF_ImGui_Spacing ();

        if ((_updater.GetState ( ) & UpdateFlags_Failed) == UpdateFlags_Failed)
        {
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));
          ImGui::TextWrapped ("A failed download was detected! Click Download below to try again.");
          ImGui::Text        ("In case of continued errors, consult SKIF.log for more details.");
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        ImGui::Text     ("Target Folder:");
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info));
        ImGui::TextWrapped    (SK_WideCharToUTF8 (_path_cache.specialk_install).c_str());
        ImGui::PopStyleColor  ( );

        SKIF_ImGui_Spacing ();

        if (!_updater.GetResults().release_notes_formatted.notes.empty())
        {
          if (! _updater.GetResults().description_installed.empty())
          {
            ImGui::Text           ("Changes from");
            ImGui::SameLine       ( );
            ImGui::TextColored    (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), (_updater.GetResults().description_installed + ":").c_str());
          }
          else {
            ImGui::Text           ("Changes:");
          }

          ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###UpdatePromptChanges", "The update does not contain any release notes...",
                                    _updater.GetResults().release_notes_formatted.notes.data(),
                                      static_cast<int>(_updater.GetResults().release_notes_formatted.notes.size()),
                                        ImVec2 ( (UpdateAvailableWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                          std::min<float>(
                                            std::min<float>(_updater.GetResults().history_formatted.lines, 40.0f) * fontConsolas->FontSize,
                                              SKIF_vecCurrentMode.y * 0.5f)
                                               ), ImGuiInputTextFlags_ReadOnly | static_cast<ImGuiInputTextFlags_>(ImGuiInputTextFlags_Multiline));

          SKIF_ImGui_DisallowMouseDragMove ( );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(compareLabel.c_str()).x + (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);
          
        ImGui::TextColored (compareColor, compareLabel.c_str());

        SKIF_ImGui_Spacing ();

        fX = (ImGui::GetContentRegionAvail().x - (((compareNewer) ? 3 : 2) * 100 * SKIF_ImGui_GlobalDPIScale) - (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);

        std::string btnLabel = (compareNewer) ? "Update" : "Rollback";

        if ((_updater.GetState ( ) & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
          btnLabel = "Download";

        if (ImGui::Button (btnLabel.c_str(), ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                       25 * SKIF_ImGui_GlobalDPIScale )))
        {
          if (btnLabel == "Download")
          {
            _registry.wsIgnoreUpdate = L"";
            _registry.regKVIgnoreUpdate.putData (_registry.wsIgnoreUpdate);

            // Trigger a new check for updates (which should download the installer)
            _updater.CheckForUpdates (false, ! compareNewer);
          }

          else {
            std::wstring args = SK_FormatStringW (LR"(/VerySilent /NoRestart /Shortcuts=false /StartService=%d /DIR="%ws")",
                                                 (_inject.bCurrentState && ! _inject.bAckInj), _path_cache.specialk_install);

            if (_inject.bCurrentState)
              _inject._StartStopInject (true);

            SKIF_Util_OpenURI (updateRoot + _updater.GetResults().filename, SW_SHOWNORMAL, L"OPEN", args.c_str());

            //bExitOnInjection = true; // Used to close SKIF once the service had been stopped

            //Sleep(50);
            //bKeepProcessAlive = false;
          }

          UpdatePromptPopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::SameLine ();
        ImGui::Spacing  ();
        ImGui::SameLine ();

        if (compareNewer)
        {
          if (ImGui::Button ("Ignore", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                 25 * SKIF_ImGui_GlobalDPIScale )))
          {
            _updater.SetIgnoredUpdate (SK_UTF8ToWideChar(_updater.GetResults().version));

            UpdatePromptPopup = PopupState_Closed;
            ImGui::CloseCurrentPopup ();
          }

          SKIF_ImGui_SetHoverTip ("SKIF will not prompt about this version again.");

          ImGui::SameLine ();
          ImGui::Spacing  ();
          ImGui::SameLine ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                               25 * SKIF_ImGui_GlobalDPIScale )))
        {
          UpdatePromptPopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::EndPopup ();
      }
      
      static float  HistoryPopupWidth          = 0.0f;
      static std::string HistoryPopupTitle;

      if (HistoryPopup == PopupState_Open && ! HiddenFramesContinueProcessing && ! SKIF_ImGui_IsAnyPopupOpen ( ))
      {
        HistoryPopupWidth = 360.0f;

        // 8.0f  per character
        // 25.0f for the scrollbar
        float calcHistoryPopupWidth = static_cast<float> (_updater.GetResults().history_formatted.max_length) * 8.0f + 25.0f;

        if (calcHistoryPopupWidth > HistoryPopupWidth)
          HistoryPopupWidth = calcHistoryPopupWidth;

        HistoryPopupWidth = std::min<float> (HistoryPopupWidth, SKIF_vecCurrentMode.x * 0.9f);

        HistoryPopupTitle = "Changelog";

        if (! _updater.GetChannel()->first.empty())
          HistoryPopupTitle += " (" + _updater.GetChannel()->first + ")";

        HistoryPopupTitle += "###History";

        ImGui::OpenPopup ("###History");
      
        ImGui::SetNextWindowSize (ImVec2 (HistoryPopupWidth * SKIF_ImGui_GlobalDPIScale, 0.0f));
      }
      
      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      if (ImGui::BeginPopupModal (HistoryPopupTitle.c_str(), nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
        if (HistoryPopup == PopupState_Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###History");
          if (window != nullptr && ! window->Appearing)
            HistoryPopup = PopupState_Opened;
        }

        /*
        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(updateTxt.c_str()).x + ImGui::GetStyle().ItemSpacing.x) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), updateTxt.c_str());
        */

        ImGui::Text        ("You are currently using");
        ImGui::SameLine    ( );
#ifdef _WIN64
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ("Special K v " + _inject.SKVer64_utf8).c_str());
#else
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ("Special K v " + _inject.SKVer32_utf8).c_str());
#endif

        SKIF_ImGui_Spacing ();

        if (! _updater.GetResults().history_formatted.notes.empty())
        {
          ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###HistoryChanges", "No historical changes detected...",
                                    _updater.GetResults().history_formatted.notes.data(),
                                      static_cast<int>(_updater.GetResults().history_formatted.notes.size()),
                                        ImVec2 ( (HistoryPopupWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                          std::min<float> (
                                              std::min<float>(_updater.GetResults().history_formatted.lines, 40.0f) * fontConsolas->FontSize,
                                              SKIF_vecCurrentMode.y * 0.6f)
                                               ), ImGuiInputTextFlags_ReadOnly | static_cast<ImGuiInputTextFlags_>(ImGuiInputTextFlags_Multiline));

          SKIF_ImGui_DisallowMouseDragMove ( );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - 100 * SKIF_ImGui_GlobalDPIScale) / 2;

        ImGui::SetCursorPosX(fX);

        if (ImGui::Button ("Close", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                              25 * SKIF_ImGui_GlobalDPIScale )))
        {
          HistoryPopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::EndPopup ();
      }
      
      static float       AutoUpdatePopupWidth          = 0.0f;
      static std::string AutoUpdatePopupTitle;
      static bool        AutoUpdateChanges = (_updater.GetAutoUpdateNotes().max_length > 0 && ! _inject.SKVer32.empty() && _inject.SKVer32 == _registry.wsAutoUpdateVersion);

      if (AutoUpdateChanges)
      {
        AutoUpdateChanges = false;
        AutoUpdatePopup = PopupState_Open;
      }
      
      // Only open the popup prompt after the library has appeared (fixes the popup weirdly closing for some unknown reason)
      if (PopulatedGames && AutoUpdatePopup == PopupState_Open && ! HiddenFramesContinueProcessing && ! SKIF_ImGui_IsAnyPopupOpen ( ))
      {
        AutoUpdatePopupWidth = 360.0f;

        // 8.0f  per character
        // 25.0f for the scrollbar
        float calcAutoUpdatePopupWidth = static_cast<float> (_updater.GetAutoUpdateNotes().max_length) * 8.0f + 25.0f;

        if (calcAutoUpdatePopupWidth > AutoUpdatePopupWidth)
          AutoUpdatePopupWidth = calcAutoUpdatePopupWidth;

        AutoUpdatePopupWidth = std::min<float> (AutoUpdatePopupWidth, SKIF_vecCurrentMode.x * 0.9f);

        AutoUpdatePopupTitle = "An update was installed automatically###AutoUpdater";

        ImGui::OpenPopup ("###AutoUpdater");
      
        ImGui::SetNextWindowSize (ImVec2 (AutoUpdatePopupWidth* SKIF_ImGui_GlobalDPIScale, 0.0f));
      }
      
      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
      if (ImGui::BeginPopupModal (AutoUpdatePopupTitle.c_str(), nullptr,
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
        if (AutoUpdatePopup == PopupState_Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###AutoUpdater");
          if (window != nullptr && ! window->Appearing)
            AutoUpdatePopup = PopupState_Opened;
        }

        /*
        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(updateTxt.c_str()).x + ImGui::GetStyle().ItemSpacing.x) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), updateTxt.c_str());
        */

        ImGui::Text        ("You are now using");
        ImGui::SameLine    ( );
#ifdef _WIN64
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ("Special K v " + _inject.SKVer64_utf8).c_str());
#else
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ("Special K v " + _inject.SKVer32_utf8).c_str());
#endif

        SKIF_ImGui_Spacing ();

        if (! _updater.GetAutoUpdateNotes().notes.empty())
        {
          ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###AutoUpdaterChanges", "No changes detected...",
                                    _updater.GetAutoUpdateNotes().notes.data(),
                                      static_cast<int>(_updater.GetAutoUpdateNotes().notes.size()),
                                        ImVec2 ( (AutoUpdatePopupWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                          std::min<float> (
                                              std::min<float>(_updater.GetAutoUpdateNotes().lines, 40.0f) * fontConsolas->FontSize,
                                              SKIF_vecCurrentMode.y * 0.6f)
                                               ), ImGuiInputTextFlags_ReadOnly | static_cast<ImGuiInputTextFlags_>(ImGuiInputTextFlags_Multiline));

          SKIF_ImGui_DisallowMouseDragMove ( );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - 100 * SKIF_ImGui_GlobalDPIScale) / 2;

        ImGui::SetCursorPosX(fX);

        if (ImGui::Button ("Close", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                              25 * SKIF_ImGui_GlobalDPIScale )))
        {
          _registry.regKVAutoUpdateVersion.putData(L"");
          AutoUpdatePopup = PopupState_Closed;
          ImGui::CloseCurrentPopup ();
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::EndPopup ();
      }

      /* 2023-08-04: Disabled due to having been replaced by a new 
      // Special handling to allow the main window to be moved when some popups are opened
      if (ImGui::IsMouseDragging (ImGuiMouseButton_Left) &&
                 SKIF_ImGui_GetWindowModeState ( ) &&
          (      AddGamePopup == PopupState_Opened ||
             PopupMessageInfo == PopupState_Opened ||
              ModifyGamePopup == PopupState_Opened ||
              RemoveGamePopup == PopupState_Opened ||
            UpdatePromptPopup == PopupState_Opened ||
                 HistoryPopup == PopupState_Opened ))
      {
        ImGui::StartMouseMovingWindow (ImGui::GetCurrentWindowRead());

        if      (     AddGamePopup == PopupState_Opened)
                      AddGamePopup  = PopupState_Open;
        else if ( PopupMessageInfo == PopupState_Opened)
                  PopupMessageInfo  = PopupState_Open;
        else if (  ModifyGamePopup == PopupState_Opened)
                   ModifyGamePopup  = PopupState_Open;
        else if (  RemoveGamePopup == PopupState_Opened)
                   RemoveGamePopup  = PopupState_Open;
        else if (UpdatePromptPopup == PopupState_Opened)
                 UpdatePromptPopup  = PopupState_Open;
        else if (     HistoryPopup == PopupState_Opened)
                      HistoryPopup  = PopupState_Open;
      }
      */

      // Ensure the taskbar overlay icon always shows the correct state
      if (_inject.bTaskbarOverlayIcon != _inject.bCurrentState)
        _inject._SetTaskbarOverlay      (_inject.bCurrentState);
      
      monitor_extent =
        ImGui::GetPopupAllowedExtentRect ( // ImGui::GetWindowAllowedExtentRect
          ImGui::GetCurrentWindowRead   ()
        );
      windowPos      = ImGui::GetWindowPos ( );
      windowRect.Min = ImGui::GetWindowPos ( );
      windowRect.Max = ImGui::GetWindowPos ( ) + ImGui::GetWindowSize ( );

      if (! HoverTipActive)
        HoverTipDuration = 0;

      // This allows us to ensure the window gets set within the workspace on the second frame after launch
      SK_RunOnce (
        RespectMonBoundaries = true
      );

      // This allows us to compact the working set on launch
#if 0
      SK_RunOnce (
        invalidatedFonts = SKIF_Util_timeGetTime ( )
      );

      if (invalidatedFonts > 0 &&
          invalidatedFonts + 500 < SKIF_Util_timeGetTime())
      {
        SKIF_Util_CompactWorkingSet ();
        invalidatedFonts = 0;
      }
#endif

      //OutputDebugString((L"Hidden frames: " + std::to_wstring(ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems) + L"\n").c_str());

      SKIF_vecCurrentMode     = ImGui::GetWindowSize ( );
      SKIF_vecCurrentPosition = ImGui::GetWindowPos  ( );

      // End the main ImGui window
      ImGui::End ( );
    }

#pragma endregion

    // Do stuff when focus is changed
    static int
        AppHasFocus  = -1;
    if (_registry.bControllers &&
        AppHasFocus != (int)SKIF_ImGui_IsFocused())
    {   AppHasFocus  = (int)SKIF_ImGui_IsFocused();

      // If focus was received
      if (AppHasFocus)
      {
        PLOG_VERBOSE << "Waking...";
        _gamepad.WakeThread  ( );
      }

      // If focus was lost
      else
      {
        PLOG_VERBOSE << "Sleeping...";
        _gamepad.SleepThread ( );
      }
    }

    // If there is any popups opened when SKIF is unfocused and not hovered, close them.
    // This can probably mistakenly bug out, seeing how the focus state isn't tracked reliable at times
    if (! SKIF_ImGui_IsFocused ( ) && ! ImGui::IsAnyItemHovered ( ) && SKIF_ImGui_IsAnyPopupOpen ( ))
    {
      // But don't close those of interest
      if (     AddGamePopup != PopupState_Open   &&
               AddGamePopup != PopupState_Opened &&
           PopupMessageInfo != PopupState_Open   &&
           PopupMessageInfo != PopupState_Opened &&
            ModifyGamePopup != PopupState_Open   &&
            ModifyGamePopup != PopupState_Opened &&
        PopupCategoryModify != PopupState_Open   &&
        PopupCategoryModify != PopupState_Opened &&
          UpdatePromptPopup != PopupState_Open   &&
          UpdatePromptPopup != PopupState_Opened &&
               HistoryPopup != PopupState_Open   &&
               HistoryPopup != PopupState_Opened &&
            AutoUpdatePopup != PopupState_Open   &&
            AutoUpdatePopup != PopupState_Opened)
        ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
    }

    // Actual rendering is conditional, this just processes input and ends the ImGui frame.
    ImGui::Render (); // also calls ImGui::EndFrame ();

    if (SKIF_ImGui_hWnd != NULL)
    {
      SK_RunOnce (SKIF_Shell_CreateJumpList ( ));
    }

    // Conditional rendering, but only if SKIF_ImGui_hWnd has actually been created
    bool bRefresh = (SKIF_ImGui_hWnd != NULL && (SKIF_isTrayed || IsIconic (SKIF_ImGui_hWnd))) ? false : true;

    if (invalidatedDevice > 0 && SKIF_Tab_Selected == UITab_Library)
      bRefresh = false;

    // Disable navigation highlight on first frames
    SK_RunOnce(
      ImGuiContext& g = *ImGui::GetCurrentContext();
      g.NavDisableHighlight = true;
    );


    // From ImHex: https://github.com/WerWolv/ImHex/blob/09bffb674505fa2b09f0135a519d213f6fb6077e/main/gui/source/window/window.cpp#L631-L672
    // GPL-2.0 license: https://github.com/WerWolv/ImHex/blob/master/LICENSE
    if (bRefresh)
    {
      bRefresh = false;
      static std::vector<uint8_t> previousVtxData;
      static size_t previousVtxDataSize = 0;

      size_t offset = 0;
      size_t vtxDataSize = 0;

      for (const auto viewPort : ImGui::GetPlatformIO().Viewports) {
        auto drawData = viewPort->DrawData;
        for (int n = 0; n < drawData->CmdListsCount; n++) {
          vtxDataSize += drawData->CmdLists[n]->VtxBuffer.size() * sizeof(ImDrawVert);
        }
      }
      for (const auto viewPort : ImGui::GetPlatformIO().Viewports) {
        auto drawData = viewPort->DrawData;
        for (int n = 0; n < drawData->CmdListsCount; n++) {
          const ImDrawList *cmdList = drawData->CmdLists[n];

          if (vtxDataSize == previousVtxDataSize) {
            bRefresh = bRefresh || std::memcmp(previousVtxData.data() + offset, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size() * sizeof(ImDrawVert)) != 0;
          } else {
            bRefresh = true;
          }

          if (previousVtxData.size() < offset + cmdList->VtxBuffer.size() * sizeof(ImDrawVert)) {
            previousVtxData.resize(offset + cmdList->VtxBuffer.size() * sizeof(ImDrawVert));
          }

          std::memcpy(previousVtxData.data() + offset, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size() * sizeof(ImDrawVert));
          offset += cmdList->VtxBuffer.size() * sizeof(ImDrawVert);
        }
      }

      previousVtxDataSize = vtxDataSize;
    }

    // Update, Render and Present the main and any additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
      // This recreates any additional viewports (index 1+)
      if (RecreateWin32Windows)
      {   RecreateWin32Windows = false;

        // If the Win32 windows should be recreated, we set the LastFrameActive to 0 here to
        //   force ImGui::UpdatePlatformWindows() below to recreate them.
        for (int i = 1; i < ImGui::GetCurrentContext()->Viewports.Size; i++)
        {
          ImGuiViewportP* viewport = ImGui::GetCurrentContext()->Viewports[i];
          viewport->LastFrameActive = 0;
        }
      }
    }

    ImGui::UpdatePlatformWindows ( ); // This creates all ImGui related windows, including the main application window, and also updates the window and swapchain sizes etc

    if (bRefresh)
    {
      // This renders the main viewport (index 0)
      ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

      // This renders any additional viewports (index 1+)
      ImGui::RenderPlatformWindowsDefault (); // Also eventually calls ImGui_ImplDX11_SwapBuffers ( ) which Presents ( )

      // This runs only once, after the ImGui window has been created
      static bool
          runOnce = true;
      if (runOnce && SKIF_ImGui_hWnd != NULL)
      {   runOnce = false;

        SKIF_Util_GetMonitorHzPeriod (SKIF_ImGui_hWnd, MONITOR_DEFAULTTOPRIMARY, dwDwmPeriod);
        //OutputDebugString((L"Initial refresh rate period: " + std::to_wstring (dwDwmPeriod) + L"\n").c_str());
      }
    }

    if ( startedMinimized && SKIF_ImGui_IsFocused ( ) )
    {
      startedMinimized = false;
      if ( _registry.bOpenAtCursorPosition )
        RepositionSKIF = true;
    }

    // Release any leftover resources from last frame
    IUnknown* pResource = nullptr;
    while (! SKIF_ResourcesToFree.empty ())
    {
      if (SKIF_ResourcesToFree.try_pop (pResource))
      {
        CComPtr <IUnknown> ptr = pResource;
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Releasing " << ptr.p;
        ptr.p->Release();
      }
      
      if (invalidatedDevice == 2)
        invalidatedDevice = 0;
    }

    // If process should stop, post WM_QUIT
    if ((! bKeepProcessAlive))// && SKIF_ImGui_hWnd != 0)
      PostQuitMessage (0);
      //PostMessage (hWnd, WM_QUIT, 0x0, 0x0);

    // Handle dynamic pausing
    bool pause = false;
    static int
      processAdditionalFrames = 0;

    bool input = SKIF_ImGui_IsAnyInputDown ( ) || uiLastMsg == WM_SKIF_GAMEPAD ||
                   (uiLastMsg >= WM_MOUSEFIRST && uiLastMsg <= WM_MOUSELAST)   ||
                   (uiLastMsg >= WM_KEYFIRST   && uiLastMsg <= WM_KEYLAST  );
    
    // We want SKIF to continue rendering in some specific scenarios
    ImGuiWindow* wnd = ImGui::FindWindowByName ("###KeyboardHint");
    if (wnd != nullptr && wnd->Active)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the keyboard hint/search is active
    else if (uiLastMsg == WM_SETCURSOR  || uiLastMsg == WM_TIMER   ||
             uiLastMsg == WM_SETFOCUS   || uiLastMsg == WM_KILLFOCUS)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we received some event changes
    else if (input)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we received any gamepad input or an input is held down
    else if (svcTransitionFromPendingState)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we transitioned away from a pending service state
    else if (1.0f > ImGui::GetCurrentContext()->DimBgRatio && ImGui::GetCurrentContext()->DimBgRatio > 0.0f)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the background is currently currently undergoing a fade effect
    else if (SKIF_Tab_Selected == UITab_Library && coverFadeActive)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the cover is currently undergoing a fade effect
    else if (addAdditionalFrames > 0)
      processAdditionalFrames = ImGui::GetFrameCount ( ) + addAdditionalFrames; // Used when the cover is currently loading in, or the update check just completed
    /*
    else if (  AddGamePopup == PopupState_Open ||
               ConfirmPopup == PopupState_Open ||
            ModifyGamePopup == PopupState_Open ||
          UpdatePromptPopup == PopupState_Open ||
               HistoryPopup == PopupState_Open )
      processAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If a popup is transitioning to an opened state
    */
    else if (ImGui::GetFrameCount ( ) > processAdditionalFrames)
      processAdditionalFrames = 0;

    addAdditionalFrames = 0;

    //OutputDebugString((L"Framerate: " + std::to_wstring(ImGui::GetIO().Framerate) + L"\n").c_str());

    // Clear gamepad/nav input for the next frame as we're done with it
    //memset (ImGui::GetIO ( ).NavInputs, 0, sizeof(ImGui::GetIO ( ).NavInputs));

    //if (uiLastMsg == WM_SKIF_GAMEPAD)
    //  OutputDebugString(L"[doWhile] Message spotted: WM_SKIF_GAMEPAD\n");
    //else if (uiLastMsg == WM_SKIF_COVER)
    //  OutputDebugString(L"[doWhile] Message spotted: WM_SKIF_COVER\n");
    //else if (uiLastMsg != 0x0)
    //  OutputDebugString((L"[doWhile] Message spotted: " + std::to_wstring(uiLastMsg) + L"\n").c_str());
    
    // Pause if we don't need to render any additional frames
    if (processAdditionalFrames == 0)
      pause = true;

    // Don't pause if there's hidden frames that needs rendering
    if (HiddenFramesContinueProcessing)
      pause = false;

    // Follow up on our attempt to restart the Steam client
    if (SteamProcessHandle != NULL &&
        SteamProcessHandle != INVALID_HANDLE_VALUE)
    {
      // When Steam has closed, restart it again
      if (WaitForSingleObject (SteamProcessHandle, 0) != WAIT_TIMEOUT)
      {
        // Stop waiting for it on all tabs
        for (auto& vWatchHandle : vWatchHandles)
        {
          if (! vWatchHandle.empty())
            vWatchHandle.erase(std::remove(vWatchHandle.begin(), vWatchHandle.end(), SteamProcessHandle), vWatchHandle.end());
        }

        PLOG_INFO << "Starting up the Steam client...";
        SKIF_Util_OpenURI (L"steam://open/main");

        CloseHandle (SteamProcessHandle);
        SteamProcessHandle = NULL;
      }
    }

    SK_RunOnce (PLOG_INFO << "Processed first frame! Start -> End took " << (SKIF_Util_timeGetTime1() - SKIF_firstFrameTime) << " ms.");

    do
    {
      DWORD msSleep = 1000000 / dwDwmPeriod; // Assume 60 Hz (16 ms) by default

      // Pause rendering
      if (pause)
      {
        static bool bWaitTimeoutMsgInputFallback = false;

        // Empty working set before we pause
        // - Bad idea because it will immediately hitch when that stuff has to be moved back from the pagefile
        //   There's no predicting how long it will take to move those pages back into memory
        SK_RunOnce (SKIF_Util_CompactWorkingSet ( ));

        // Create/update the timer when we are pausing
        if (_registry.bEfficiencyMode && ! _registry._EfficiencyMode && SKIF_Notify_hWnd != NULL && ! msgDontRedraw && ! SKIF_ImGui_IsFocused ( ))
          SetTimer (SKIF_Notify_hWnd, cIDT_TIMER_EFFICIENCY, 1000, (TIMERPROC) &SKIF_EfficiencyModeTimerProc);

        // Sleep until a message is in the queue or a change notification occurs
        DWORD res =
          MsgWaitForMultipleObjects (static_cast<DWORD>(vWatchHandles[SKIF_Tab_Selected].size()), vWatchHandles[SKIF_Tab_Selected].data(), false, bWaitTimeoutMsgInputFallback ? msSleep : INFINITE, QS_ALLINPUT);

        // The below is required as a fallback if V-Sync OFF is forced on SKIF and e.g. analog stick drift is causing constant input.
        // Throttle to monitors refresh rate unless a new event is triggered, or user input is posted, but only if the frame rate is detected as being unlocked
        if (res == WAIT_FAILED)
        {
          SK_RunOnce (
          {
            PLOG_ERROR << "Waiting on a new message or change notification failed with error message: " << SKIF_Util_GetErrorAsWStr ( );
            PLOG_ERROR << "Timeout has permanently been set to the monitors refresh rate period (" << static_cast<float> (dwDwmPeriod / 1000) << ", " << msSleep << "ms) !";
            bWaitTimeoutMsgInputFallback = true;
          });
        }

        // Always process 3 additional frames after we wake up
        processAdditionalFrames = ImGui::GetFrameCount() + 3;
      }

      if (bRefresh && ! msgDontRedraw && SKIF_ImGui_hWnd != NULL)
      {
        static bool frameRateUnlocked = false;
        static int  unlockedCount     = 0;

        // If the frame rate was ever detected as being unlocked, use sleep as a limiter instead
        if (frameRateUnlocked)
        {
          timeBeginPeriod (1);
          Sleep           (msSleep);
          timeEndPeriod   (1);
        }

        else
        {
          //auto timePre = SKIF_Util_timeGetTime1 ( );

          // Waitable Swapchains (used for Flip)
          if (! vSwapchainWaitHandles.empty())
          {
            static bool bWaitTimeoutSwapChainsFallback = false;

            DWORD res =
              WaitForMultipleObjectsEx (static_cast<DWORD>(vSwapchainWaitHandles.size()), vSwapchainWaitHandles.data(), true, bWaitTimeoutSwapChainsFallback ? msSleep : 1000, true);

            //OutputDebugString((L"[" + SKIF_Util_timeGetTimeAsWStr() + L"][#" + std::to_wstring(ImGui::GetFrameCount()) + L"] Maybe we'll be waiting? (handles: " + std::to_wstring(vSwapchainWaitHandles.size()) + L")\n").c_str());
            if (res == WAIT_TIMEOUT)
            {
              // This is only expected to occur when an issue arises
              // e.g. the display driver resets and invalidates the
              // swapchain in the middle of a frame.
              PLOG_ERROR << "Timed out while waiting on the swapchain wait objects!";
            }

            // Only reason we use a timeout here is in case a swapchain gets destroyed on the same frame we try waiting on its handle
            else if (res == WAIT_FAILED)
            {
              SK_RunOnce (
              {
                PLOG_ERROR << "Waiting on the swapchain wait objects failed with error message: " << SKIF_Util_GetErrorAsWStr ( );
                PLOG_ERROR << "Timeout has permanently been set to the monitors refresh rate period (" << static_cast<float> (dwDwmPeriod / 1000) << ", " << msSleep << "ms !";
                bWaitTimeoutSwapChainsFallback = true;
              });
            }
          }

          // BitBlt relies on regular V-Sync (or the fallback ""framerate limiter"")
          else { }

#if 0
          auto timePost = SKIF_Util_timeGetTime1 ( );
          auto timeDiff = timePost - timePre;
          //PLOG_VERBOSE << "Waited: " << timeDiff << " ms (handles : " << vSwapchainWaitHandles.size() << ")";

          // Fallback ""framerate limiter""
          static DWORD lastInput = timePre;

          if (SKIF_ImGui_IsAnyInputDown ( ))
            lastInput = timePost;

          if (! frameRateUnlocked && ! HiddenFramesContinueProcessing && lastInput != timePost && timeDiff <= 1 && ImGui::GetFrameCount() > 1000)
          {
            float maxFPS = static_cast<float> (dwDwmPeriod) / 400; // 400 == 2.5x FPS

            // If ImGui's detected frame rate is above the max FPS...
            if (ImGui::GetIO().Framerate > maxFPS)
            {
              // If we haven't received an input in the last 250ms...
              if (timePost > (lastInput + 250))
              {
                PLOG_WARNING << "Detected an unexpectedly high frame rate! Expected max " << maxFPS << " FPS (2.5x refresh rate), received: " << ImGui::GetIO().Framerate << " FPS...";
                unlockedCount++;
              }
            }
          }

          if (unlockedCount > 10)
          {
            frameRateUnlocked = true;
            PLOG_ERROR << "Framerate was detected as being unlocked, and an additional limiter has been enforced to the monitors refresh rate period (" << static_cast<float> (dwDwmPeriod / 1000) << ", " << msSleep << "ms) !";
          }
#endif
        }
      }
      
      // Reset stuff that's set as part of pumping the message queue
      msgDontRedraw = false;
      uiLastMsg     = 0x0;

      // Pump the message queue, and break if we receive a false (WM_QUIT or WM_QUERYENDSESSION)
      if (! _TranslateAndDispatch ( ))
        break;
      
      // If we added more frames, ensure we exit the loop
      if (addAdditionalFrames > 0)
        msgDontRedraw = false;

      // Disable Efficiency Mode when we are being interacted with
      if (_registry._EfficiencyMode && SKIF_Notify_hWnd != NULL && ! msgDontRedraw && SKIF_ImGui_IsFocused ( ))
      {
        // Wake up and disable idle priority + ECO QoS (let the system take over)
        SetPriorityClass (SKIF_Util_GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        SKIF_Util_SetProcessPowerThrottling (SKIF_Util_GetCurrentProcess(), -1);

        PLOG_DEBUG << "Disengaged efficiency mode";

        _registry._EfficiencyMode = false;
      }

      // Break if SKIF is no longer a window
      //if (! IsWindow (hWnd))
      //  break;

    } while (! SKIF_Shutdown.load() && msgDontRedraw); // For messages we don't want to redraw on, we set msgDontRedraw to true.
  }

  PLOG_INFO << "Exited main loop...";
  
  // Handle the service before we exit
  if (_inject.bCurrentState && ! _registry.bAllowBackgroundService )
  {
    PLOG_INFO << "Shutting down the service...";
    _inject._StartStopInject (true);
  }
  
  if (! _registry._LastSelectedWritten)
  {
    _registry.regKVLastSelectedGame.putData  (_registry.uiLastSelectedGame);
    _registry.regKVLastSelectedStore.putData (_registry.uiLastSelectedStore);
    _registry._LastSelectedWritten = true;
    PLOG_INFO << "Wrote the last selected game to registry: " << _registry.uiLastSelectedGame << " (" << _registry.uiLastSelectedStore << ")";
  }

  // Update the registry
  std::vector<std::wstring> _inNames, _inBools;
  for (auto& category : _registry.vecCategories)
  {
    _inNames.push_back (SK_UTF8ToWideChar (category.name));
    _inBools.push_back (std::to_wstring   (category.expanded));
  }

  _registry.regKVCategories.     putDataMultiSZ (_inNames);
  _registry.regKVCategoriesState.putDataMultiSZ (_inBools);
  PLOG_INFO << "Wrote the collapsible category state to the registry.";

  // TODO: Make an exception for scenarios where remembering the size and pos makes sense,
  //         e.g. when size / DPI <= regular size * 1.5x or something like that!!!
  // 
  // Only store window size and position to the registry if we are not in a maximized state
  ImVec2 vecCurrentModeDPIUnaware = ImFloor (SKIF_vecCurrentMode / SKIF_ImGui_GlobalDPIScale);

  if (! IsZoomed (SKIF_ImGui_hWnd) ||
     (vecCurrentModeDPIUnaware.x <= SKIF_vecRegularModeDefault.x * 1.5f &&
      vecCurrentModeDPIUnaware.y <= SKIF_vecRegularModeDefault.y * 1.5f))
  {
    // Only store the window size if we are not in service mode
    if (! _registry.bMiniMode  &&
        vecCurrentModeDPIUnaware.x > 0 &&
        vecCurrentModeDPIUnaware.y > 0)
    {
      // Store a DPI-unaware size, so SKIF can automatically adjust it to the proper DPI on launch
      _registry.iUIWidth  = static_cast<int> (vecCurrentModeDPIUnaware.x);
      _registry.iUIHeight = static_cast<int> (vecCurrentModeDPIUnaware.y);
    
      _registry.regKVUIWidth .putData (_registry.iUIWidth);
      _registry.regKVUIHeight.putData (_registry.iUIHeight);

      PLOG_INFO << "Wrote the window size to the registry: " << _registry.iUIWidth << "x" << _registry.iUIHeight;
    }

    // Note that negative window positions are valid!
    if (SKIF_vecCurrentPosition.x != -1.0f &&
        SKIF_vecCurrentPosition.y != -1.0f)
    {
      // Doesn't seem to be needed
      //RECT dwmBorder = { };
      //if (S_OK == DwmGetWindowAttribute (SKIF_ImGui_hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwmBorder, sizeof(dwmBorder)))
      //{ }

      _registry.iUIPositionX = static_cast<int> (SKIF_vecCurrentPosition.x);
      _registry.iUIPositionY = static_cast<int> (SKIF_vecCurrentPosition.y);
    
      _registry.regKVUIPositionX.putData (_registry.iUIPositionX);
      _registry.regKVUIPositionY.putData (_registry.iUIPositionY);

      PLOG_INFO << "Wrote the window position to the registry: " << _registry.iUIPositionX << ", " << _registry.iUIPositionY;
    }
  }

  SKIF_Util_UnregisterHotKeySVCTemp   ( );
  SKIF_Util_UnregisterHotKeyHDRToggle ( );
  //SKIF_Util_SetEffectivePowerModeNotifications (false); // (this serves no purpose yet)

  PLOG_INFO << "Killing timers...";
  KillTimer (SKIF_Notify_hWnd, _inject.IDT_REFRESH_INJECTACK);
  KillTimer (SKIF_Notify_hWnd, _inject.IDT_REFRESH_PENDING);
  KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_TOOLTIP);
  KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_UPDATER);
  KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_GAMES);
  KillTimer (SKIF_Notify_hWnd, cIDT_TIMER_EFFICIENCY);

  PLOG_INFO << "Shutting down ImGui...";
  ImGui_ImplDX11_Shutdown     ( );
  ImGui_ImplWin32_Shutdown    ( );

  CleanupDeviceD3D            ( );

  PLOG_INFO << "Destroying notification icon...";
  _gamepad.UnregisterDevNotification ( );
  SKIF_Shell_DeleteNotifyIcon ( );
  DestroyWindow             (SKIF_Notify_hWnd);

  PLOG_INFO << "Destroying ImGui context...";
  ImGui::DestroyContext       ( );

  SKIF_ImGui_hWnd  = NULL;
  SKIF_Notify_hWnd = NULL;

  DeleteCriticalSection (&CriticalSectionDbgHelp);

  PLOG_INFO << "Exiting process with code " << SKIF_ExitCode;
  return SKIF_ExitCode;
}


// Helper functions

// D3D9 test stuff
//#define SKIF_D3D9_TEST

#ifdef SKIF_D3D9_TEST

#define D3D_DEBUG_INFO
#pragma comment (lib, "d3d9.lib")
#include <D3D9.h>

#endif

bool CreateDeviceD3D (HWND hWnd)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

#ifdef SKIF_D3D9_TEST
  /* Test D3D9 debugging */
  IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);

  if (d3d != nullptr)
  {
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 800;
    pp.BackBufferHeight = 600;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = NULL;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;

    IDirect3DDevice9* device = nullptr;
    // Intentionally passing an invalid parameter to CreateDevice to cause an exception to be thrown by the D3D9 debug layer
    //HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, (D3DDEVTYPE)100, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);

    if (FAILED(hr))
    {
      //OutputDebugString(L"d3d->CreateDevice() failed!\n");
    }
  }

  else {
    OutputDebugString(L"Direct3DCreate9() failed!\n");
  }
#endif
  
  CComPtr <IDXGIFactory2> pFactory2;

  if (FAILED (CreateDXGIFactory1 (__uuidof (IDXGIFactory2), (void **)&pFactory2.p)))
    return false;

  // Windows 7 (2013 Platform Update), or Windows 8+
  // SKIF_bCanFlip            =         true; // Should never be set to false here

  // Windows 8.1+
  _registry._RendererCanWaitSwapchain      =
    SKIF_Util_IsWindows8Point1OrGreater ();

  // Windows 10 1709+ (Build 16299)
  _registry._RendererCanHDR                =
    SKIF_Util_IsWindows10v1709OrGreater (    ) &&
    SKIF_Util_IsHDRActive               (true);

  CComQIPtr <IDXGIFactory5>
                  pFactory5 (pFactory2.p);

  // Windows 10+
  if (pFactory5 != nullptr)
  {
    BOOL supportsTearing = FALSE;
    pFactory5->CheckFeatureSupport (
                          DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                        &supportsTearing,
                                sizeof  (supportsTearing)
                                              );
    _registry._RendererCanAllowTearing = (supportsTearing != FALSE);

    pFactory5.Release();
  }

  // Overrides
  //_registry._RendererCanAllowTearing       = false; // Allow Tearing
  //SKIF_bCanFlip               = false; // Flip Sequential (if this is false, BitBlt Discard will be used instead)
  //SKIF_bCanWaitSwapchain      = false; // Waitable Swapchain

  // D3D11Device
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL
                    featureLevelArray [4] = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
  };

  UINT createDeviceFlags = 0;
  // This MUST be disabled before public release! Otherwise systems without the Windows SDK installed will crash on launch.
  //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG; // Enable debug layer of D3D11

  if (FAILED (D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                              createDeviceFlags, featureLevelArray,
                                                         sizeof (featureLevelArray) / sizeof featureLevel,
                                                D3D11_SDK_VERSION,
                                                       &SKIF_pd3dDevice,
                                                                &featureLevel,
                                                       &SKIF_pd3dDeviceContext)))
  {
    //OutputDebugString(L"D3D11CreateDevice failed!\n");
    PLOG_ERROR << "D3D11CreateDevice failed!";
    return false;
  }

  //return true; // No idea why this was left in https://github.com/SpecialKO/SKIF/commit/1c03d60642fcc62d4aa27bd440dc24115f6cf907 ... A typo probably?

  // We need to try creating a dummy swapchain before we actually start creating
  //   viewport windows. This is to ensure a compatible format is used from the
  //   get go, as e.g. using WS_EX_NOREDIRECTIONBITMAP on a BitBlt window will
  //   cause it to be hidden entirely.

  if (pFactory2 != nullptr)
  {
    CComQIPtr <IDXGISwapChain1>
                   pSwapChain1;

    DXGI_FORMAT dxgi_format;

    // HDR formats
    if (_registry._RendererCanHDR && _registry.iHDRMode > 0)
    {
      // Disable support for HDR10, it looks terrible and I want it gone.
      if (_registry.iHDRMode == 1)
          _registry.iHDRMode = 2;

      if (_registry.iHDRMode == 2)
        dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // scRGB (16 bpc)
      else
        dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR10 (10 bpc)
    }

    // SDR formats
    else {
      if      (_registry.iSDRMode == 2)
        dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // 16 bpc
      else if (_registry.iSDRMode == 1 && SKIF_Util_IsWindowsVersionOrGreater (10, 0, 16299))
        dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // 10 bpc (apparently only supported for flip on Win10 1709+
      else
        dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;     // 8 bpc;
    }

    // Create a dummy swapchain for the dummy viewport
    DXGI_SWAP_CHAIN_DESC1
      swap_desc                  = { };
    swap_desc.Width              = 8;
    swap_desc.Height             = 8;
    swap_desc.Format             = dxgi_format;
    swap_desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.Flags              = 0x0;
    swap_desc.SampleDesc.Count   = 1;
    swap_desc.SampleDesc.Quality = 0;

    // Assume flip by default
    swap_desc.BufferCount  = 3; // Must be 2-16 for flip model

    if (_registry._RendererCanWaitSwapchain)
      swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    if (_registry._RendererCanAllowTearing)
      swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    for (auto  _swapEffect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_DISCARD}) // DXGI_SWAP_EFFECT_FLIP_DISCARD
    {
      swap_desc.SwapEffect = _swapEffect;

      // In case flip failed, fall back to using BitBlt
      if (_swapEffect == DXGI_SWAP_EFFECT_DISCARD)
      {
        swap_desc.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.BufferCount  = 1;
        swap_desc.Flags        = 0x0;
        _registry._RendererCanHDR           = false;
        _registry._RendererCanWaitSwapchain = false;
        _registry._RendererCanAllowTearing = false;
        _registry.iUIMode      = 0;
      }

      if (SUCCEEDED (pFactory2->CreateSwapChainForHwnd (SKIF_pd3dDevice, hWnd, &swap_desc, NULL, NULL,
                                &pSwapChain1 )))
      {
        pSwapChain1.Release();
        break;
      }
    }

    pFactory2.Release();

    return true;
  }

  //CreateRenderTarget ();

  return false;
}

void CleanupDeviceD3D (void)
{
  //CleanupRenderTarget ();

  //IUnknown_AtomicRelease ((void **)&g_pSwapChain);
  IUnknown_AtomicRelease ((void **)&SKIF_pd3dDeviceContext);
  IUnknown_AtomicRelease ((void **)&SKIF_pd3dDevice);
}

// Prevent race conditions between asset loading and device init
//
void SKIF_WaitForDeviceInitD3D (void)
{
  while (SKIF_pd3dDevice        == nullptr    ||
         SKIF_pd3dDeviceContext == nullptr /* ||
         SKIF_g_pSwapChain        == nullptr  */ )
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
    SKIF_pd3dDevice;
}

bool SKIF_D3D11_IsDevicePtr (void)
{
  return (SKIF_pd3dDevice != nullptr)
                     ? true : false;
}

/*
void CreateRenderTarget (void)
{
  ID3D11Texture2D*                           pBackBuffer = nullptr;
  g_pSwapChain->GetBuffer (0, IID_PPV_ARGS (&pBackBuffer));

  if (pBackBuffer != nullptr)
  {
    g_pd3dDevice->CreateRenderTargetView   ( pBackBuffer, nullptr, &SKIF_g_mainRenderTargetView);
                                             pBackBuffer->Release ();
  }
}

void CleanupRenderTarget (void)
{
  IUnknown_AtomicRelease ((void **)&SKIF_g_mainRenderTargetView);
}
*/

// Win32 message handler
LRESULT
WINAPI
SKIF_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  UNREFERENCED_PARAMETER (hWnd);
  UNREFERENCED_PARAMETER (lParam);

  // This is the message procedure that handles all custom SKIF window messages and actions
  
  UpdateFlags uFlags = UpdateFlags_Unknown;
  
  static SKIF_CommonPathsCache&   _path_cache = SKIF_CommonPathsCache  ::GetInstance ( );
  static SKIF_RegistrySettings&   _registry   = SKIF_RegistrySettings  ::GetInstance ( );
  static SKIF_InjectionContext&   _inject     = SKIF_InjectionContext  ::GetInstance ( );
  static SKIF_GamePadInputHelper& _gamepad    = SKIF_GamePadInputHelper::GetInstance ( );
  // We don't define this here to ensure it doesn't get created before we are ready to handle it
//static SKIF_Updater&          _updater    = SKIF_Updater         ::GetInstance ( );

  switch (msg)
  {
    case WM_DEVICECHANGE:
      switch (wParam)
      {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
        {
          // Only process these if controller support is enabled
          if (_registry.bControllers)
          {
            DEV_BROADCAST_HDR* pDevHdr =
              (DEV_BROADCAST_HDR *)lParam;
            if (pDevHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
            {
              DEV_BROADCAST_DEVICEINTERFACE_W *pDev =
                (DEV_BROADCAST_DEVICEINTERFACE_W *)pDevHdr;

              static constexpr GUID GUID_DEVINTERFACE_HID =
                { 0x4D1E55B2L, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

              if (IsEqualGUID (pDev->dbcc_classguid, GUID_DEVINTERFACE_HID))
              {
                // Check for changes in case any device has arrived
                if (wParam == DBT_DEVICEARRIVAL)
                {
                  PLOG_VERBOSE << "A HID device has arrived, and we need to refresh gamepad connectivity...";
                  _gamepad.InvalidateGamePads( );
                  _gamepad.WakeThread ( );
                }

                // Only check for changes if a device was removed if we actually had a connected gamepad
                else if (wParam == DBT_DEVICEREMOVECOMPLETE && _gamepad.HasGamePad ( ))
                {
                  PLOG_VERBOSE << "A HID device was removed, and we need to refresh gamepad connectivity...";
                  _gamepad.InvalidateGamePads( );
                  _gamepad.WakeThread ( );
                }
              }
            }
          }
        }
      }
      break;

    case WM_HOTKEY:
      if (wParam == SKIF_HotKey_HDR)
        SKIF_Util_EnableHDROutput ( );

      // Only toggle the service if we think we are not already in a game
      else if (wParam == SKIF_HotKey_SVC && hInjectExitAckEx.m_h == 0)
      {
        // If the service is not running, start it
        if (     _inject.runState != SKIF_InjectionContext::RunningState::Started)
                 _inject._StartStopInject (false, true);

        // This design is currently unsatisfactory. We want to be able to toggle injection service
        //   on and off when no game is injected, but if a game is injected we should not be able
        //     to trigger it... But this doesn't work as desired as SKIF isn't set up to properly
        //       track stuff that detailed yet. 'hInjectExitAckEx' is created when the service
        //         is started... Maybe change that to be created when injection is signaled?

        // If the service is running, stop it
        //else if (_inject.runState == SKIF_InjectionContext::RunningState::Started)
        //         _inject._StartStopInject (true);
      }
        
    break;

    // System wants to shut down and is asking if we can allow it
    case WM_QUERYENDSESSION:
      PLOG_INFO << "System in querying if we can shut down!";
      return true;
      break;

    case WM_ENDSESSION: 
      // Session is shutting down -- perform any last minute changes!
      if (wParam == 1)
      {
        PLOG_INFO << "Received system shutdown signal!";

        if (! _registry._LastSelectedWritten)
        {
          _registry.regKVLastSelectedGame.putData  (_registry.uiLastSelectedGame);
          _registry.regKVLastSelectedStore.putData (_registry.uiLastSelectedStore);
          _registry._LastSelectedWritten = true;
          PLOG_INFO << "Wrote the last selected game to registry: " << _registry.uiLastSelectedGame << " (" << _registry.uiLastSelectedStore << ")";
        }

        SKIF_Shutdown.store(true);
      }
      //return 0;
      break;

    case WM_QUIT:
      SKIF_Shutdown.store(true);
      break;

    case WM_SETTINGCHANGE:
      // ImmersiveColorSet is sent when either SystemUsesLightTheme (OS) or AppsUseLightTheme (apps) changes
      // If both are changed by the OS at the same time, two messages are sent to all apps
      if (lParam != NULL && _registry.iStyle == 0 && _wcsicmp (L"ImmersiveColorSet", reinterpret_cast<wchar_t*> (lParam)) == 0)
      {
        bool oldMode = _registry._StyleLightMode;

        ImGuiStyle            newStyle;
        SKIF_ImGui_SetStyle (&newStyle);

        // Only log and change the DWM borders if the color mode was actually changed
        if (oldMode != _registry._StyleLightMode)
        {
          PLOG_VERBOSE << "Detected a color change through a ImmersiveColorSet broadcast.";

          extern void
            SKIF_ImGui_ImplWin32_UpdateDWMBorders (void);
            SKIF_ImGui_ImplWin32_UpdateDWMBorders (    );
        }
      }

      break;

    case WM_POWERBROADCAST:
      if (wParam == PBT_APMSUSPEND)
      {
        // The system allows approximately two seconds for an application to handle this notification.
        // If an application is still performing operations after its time allotment has expired, the system may interrupt the application.
        PLOG_INFO << "System is suspending operation.";
        if (! _registry._LastSelectedWritten)
        {
          _registry.regKVLastSelectedGame.putData  (_registry.uiLastSelectedGame);
          _registry.regKVLastSelectedStore.putData (_registry.uiLastSelectedStore);
          _registry._LastSelectedWritten = true;
          PLOG_INFO << "Wrote the last selected game to registry: " << _registry.uiLastSelectedGame << " (" << _registry.uiLastSelectedStore << ")";
        }
      }

      // If the system wakes due to an external wake signal (remote wake), the system broadcasts only the PBT_APMRESUMEAUTOMATIC event.
      // The PBT_APMRESUMESUSPEND event is not sent.
      if (wParam == PBT_APMRESUMEAUTOMATIC)
      {
        PLOG_DEBUG << "Operation is resuming automatically from a low-power state.";
      }

      // If the system wakes due to user activity (such as pressing the power button) or if the system detects user interaction at the physical
      // console (such as mouse or keyboard input) after waking unattended, the system first broadcasts the PBT_APMRESUMEAUTOMATIC event, then
      // it broadcasts the PBT_APMRESUMESUSPEND event. In addition, the system turns on the display.
      // Your application should reopen files that it closed when the system entered sleep and prepare for user input.
      if (wParam == PBT_APMRESUMESUSPEND)
      {
        PLOG_INFO << "Operation is resuming from a low-power state due to user activity.";
        if (_registry.iCheckForUpdates == 2)
          SKIF_Updater::GetInstance ( ).CheckForUpdates ( );
      }

      break;

    case WM_GETICON: // Work around bug in Task Manager sending this message every time it refreshes its process list
      msgDontRedraw = true;
      return true;
      break;

    case WM_DISPLAYCHANGE:
      SKIF_Util_GetMonitorHzPeriod (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST, dwDwmPeriod);

      if (SKIF_Tab_Selected == UITab_Settings)
        RefreshSettingsTab = true; // Only set this if the Settings tab is actually selected
      break;

    case WM_SKIF_MINIMIZE:
      if (SKIF_ImGui_hWnd != NULL)
      {
        if (_registry.bCloseToTray && ! SKIF_isTrayed)
        {
          ShowWindow       (SKIF_ImGui_hWnd, SW_MINIMIZE);
          ShowWindow       (SKIF_ImGui_hWnd, SW_HIDE);
          UpdateWindow     (SKIF_ImGui_hWnd);
          SKIF_isTrayed    = true;
        }

        else if (! _registry.bCloseToTray) {
          ShowWindowAsync (SKIF_ImGui_hWnd, SW_MINIMIZE);
        }
      }
      break;

    case WM_SKIF_START:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false);
      else if (_inject.runState == SKIF_InjectionContext::RunningState::Started)
        _inject.SetStopOnInjectionEx (false);
      break;

    case WM_SKIF_TEMPSTART:
    case WM_SKIF_TEMPSTARTEXIT:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false, true);
      else if (_inject.runState == SKIF_InjectionContext::RunningState::Started)
        _inject.SetStopOnInjectionEx (true);

      if (msg == WM_SKIF_TEMPSTARTEXIT)
        _registry._ExitOnInjection = true;
      break;

    case WM_SKIF_STOP:
      _inject._StartStopInject   (true);
      break;

    case WM_SKIF_REFRESHGAMES:
      RepopulateGamesWasSet = SKIF_Util_timeGetTime();
      RepopulateGames = true;
      SelectNewSKIFGame = (uint32_t)wParam;

      SetTimer (SKIF_Notify_hWnd,
          IDT_REFRESH_GAMES,
          50,
          (TIMERPROC) NULL
      );
      break;

    case WM_SKIF_REFRESHFOCUS:
      // Ensure the gamepad input thread knows what state we are actually in
      if (SKIF_ImGui_IsFocused ( ))
        _gamepad.WakeThread  ( );
      else
        _gamepad.SleepThread ( );
      break;

    case WM_SKIF_LAUNCHER:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
      {
        PLOG_INFO << "Suppressing the initial 'Please launch a game to continue' notification...";
        _registry._SuppressServiceNotification = true;
        _inject._StartStopInject (false, true, wParam);
      }

      // Reload the whitelist as it might have been changed
      _inject.LoadWhitelist      ( );
      break;

    case WM_SKIF_COVER:
      addAdditionalFrames += 3;

      // Update tryingToLoadCover
      extern bool tryingToLoadCover;
      extern std::atomic<bool> gameCoverLoading;
      tryingToLoadCover = gameCoverLoading.load();
      
      // Empty working set after the cover has finished loading
      if (! tryingToLoadCover)
        SKIF_Util_CompactWorkingSet ( );
      break;

    case WM_SKIF_REFRESHCOVER:
      addAdditionalFrames += 3;

      // Update refreshCover
      extern bool     coverRefresh; // This just triggers a refresh of the cover
      extern uint32_t coverRefreshAppId;
      extern int      coverRefreshStore;

      coverRefresh      = true;
      coverRefreshAppId = (uint32_t)wParam;
      coverRefreshStore = ( int    )lParam;

      break;

    case WM_SKIF_ICON:
      addAdditionalFrames += 3;
      break;

    case WM_SKIF_RUN_UPDATER:
      SKIF_Updater::GetInstance ( ).CheckForUpdates ( );
      break;

    case WM_SKIF_UPDATER:
      SKIF_Updater::GetInstance ( ).RefreshResults ( ); // Swap in the new results

      uFlags = (UpdateFlags)wParam;

      if (uFlags != UpdateFlags_Unknown)
      {
        // Only show the update prompt if we have a file downloaded and we either
        // forced it (switched channel) or it is not ignored nor an older version
        if ( (uFlags & UpdateFlags_Downloaded) == UpdateFlags_Downloaded &&
            ((uFlags & UpdateFlags_Forced)     == UpdateFlags_Forced     ||
            ((uFlags & UpdateFlags_Ignored)    != UpdateFlags_Ignored    &&
             (uFlags & UpdateFlags_Older)      != UpdateFlags_Older     )))
        {
          // If we use auto-update *experimental*
          // But only if we have servlets, so we don't auto-install ourselves in users' Downloads folder :)
          if (_registry.bAutoUpdate && _inject.bHasServlet)
          {
            SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_UPDATE, L"Blink and you'll miss it ;)", L"An update is being installed...");

            PLOG_INFO << "The app is performing an automatic update...";

            _registry.regKVAutoUpdateVersion.putData (SK_UTF8ToWideChar (SKIF_Updater::GetInstance ( ).GetResults ( ).version));

            bool startService   = (_Signal.Start         && NULL == SKIF_ImGui_hWnd) ||
                                  ((_inject.bCurrentState || _inject.runState == SKIF_InjectionContext::RunningState::Starting) && ! _inject.bAckInj);
            bool startMinimized = _Signal.Minimize && (                   // If we started minimized, and
                                               NULL == SKIF_ImGui_hWnd || // if ImGui window haven't been created yet, or
                           (SKIF_isTrayed || IsIconic (SKIF_ImGui_hWnd))  // we are currently minimized or trayed
            );
            
            std::wstring update = SK_FormatStringW (LR"(%ws\Version\%ws)", _path_cache.specialk_userdata, SKIF_Updater::GetInstance ( ).GetResults ( ).filename.c_str());
            std::wstring args   = SK_FormatStringW (LR"(/VerySilent /NoRestart /Shortcuts=false /StartService=%d /StartMinimized=%d /DIR="%ws")",
                                                                                                 startService,    startMinimized, _path_cache.specialk_install);

            if (_inject.bCurrentState)
              _inject._StartStopInject (true);

            SKIF_Util_OpenURI (update.c_str(), SW_SHOWNORMAL, L"OPEN", args.c_str());
          }

          // Classic update procedure
          else {
            SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_UPDATE, L"Open the app to continue.", L"An update to Special K is available!");

            PLOG_INFO << "An update is available!";

            UpdatePromptPopup = PopupState_Open;
          }
          addAdditionalFrames += 3;
        }

        else if ((uFlags & UpdateFlags_Failed) == UpdateFlags_Failed)
        {
          SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_UPDATE, L"The update will be retried later.", L"Update failed :(");
        }
      }
      break;

    case WM_SKIF_RESTORE:
      _inject.bTaskbarOverlayIcon = false;

      if (SKIF_ImGui_hWnd != NULL)
      {
        if (! SKIF_isTrayed && ! IsIconic (SKIF_ImGui_hWnd))
          RepositionSKIF            = true;

        if (SKIF_isTrayed)
        {   SKIF_isTrayed           = false;
          ShowWindow   (SKIF_ImGui_hWnd, SW_SHOW); // ShowWindowAsync
        }

        ShowWindow     (SKIF_ImGui_hWnd, SW_RESTORE); // ShowWindowAsync

        if (! UpdateWindow        (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "UpdateWindow ( ) failed!";

        if (! SetForegroundWindow (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "SetForegroundWindow ( ) failed!";

        if (! SetActiveWindow     (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "SetActiveWindow ( ) failed: "  << SKIF_Util_GetErrorAsWStr ( );

        if (! BringWindowToTop    (SKIF_ImGui_hWnd))
          PLOG_DEBUG << "BringWindowToTop ( ) failed: " << SKIF_Util_GetErrorAsWStr ( );
      }
      break;

    // Custom refresh window messages
    case WM_SKIF_POWERMODE:
      break;
    case WM_SKIF_EVENT_SIGNAL:
        addAdditionalFrames += 3;

        if ((HWND)wParam != nullptr)
          hWndForegroundFocusOnExit = (HWND)wParam;
      break;

    case WM_TIMER:
      addAdditionalFrames += 3;
      switch (wParam)
      {
        case IDT_REFRESH_NOTIFY:
          KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_NOTIFY);
          break;
        case IDT_REFRESH_TOOLTIP:
          // Do not redraw if SKIF is not being hovered by the mouse or a hover tip is not longer "active" any longer
          if (! SKIF_ImGui_IsMouseHovered ( ) || ! HoverTipActive)
          {
            msgDontRedraw = true;
            addAdditionalFrames -= 3; // Undo the 3 frames we added just above
          }
          
          KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_TOOLTIP);
          break;
        case IDT_REFRESH_GAMES: // TODO: Contemplate this design, and its position in the new design with situational pausing. Concerns WM_SKIF_REFRESHGAMES / IDT_REFRESH_GAMES.
          if (RepopulateGamesWasSet != 0 && RepopulateGamesWasSet + 1000 < SKIF_Util_timeGetTime())
          {
            RepopulateGamesWasSet = 0;
            KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_GAMES);
          }
          break;
        // These are just dummy events to get SKIF to refresh for a couple of frames more periodically
        case cIDT_REFRESH_INJECTACK:
          //OutputDebugString(L"cIDT_REFRESH_INJECTACK\n");
          break;
        case cIDT_REFRESH_PENDING:
          //OutputDebugString(L"cIDT_REFRESH_PENDING\n");
          break;
        case  IDT_REFRESH_UPDATER:
          //OutputDebugString(L"IDT_REFRESH_UPDATER\n");
          break;
      }
      break;

    case WM_SYSCOMMAND:

      /*
      if ((wParam & 0xfff0) == SC_KEYMENU)
      {
        // Disable ALT application menu
        if ( lParam == 0x00 ||
             lParam == 0x20 )
        {
          return true;
        }
      }

      else if ((wParam & 0xfff0) == SC_MOVE)
      {
        // Disables the native move modal loop of Windows and
        // use the RepositionSKIF approach to move the window
        // to the center of the display the cursor is on.
        PostMessage (hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
      }
      */
      break;

    case WM_DESTROY:
      ::PostQuitMessage (0);
      break;
  }
  
  // Tell the main thread to render at least three more frames after we have processed the message
  if (SKIF_ImGui_hWnd != NULL && ! msgDontRedraw)
  {
    addAdditionalFrames += 3;
    //PostMessage (SKIF_ImGui_hWnd, WM_NULL, 0, 0);
  }

  return 0;

  //return
  //  ::DefWindowProc (hWnd, msg, wParam, lParam);
}

LRESULT
WINAPI
SKIF_Notify_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // This is the message procedure for the notification icon window that also handles custom SKIF messages

  static SKIF_RegistrySettings&   _registry  = SKIF_RegistrySettings  ::GetInstance ( );

  PLOG_VERBOSE_IF(_registry.isDevLogging()) << std::format("[0x{:<4x}] [{:5d}] [{:20s}]{:s}[0x{:x}, {:d}{:s}] [0x{:x}, {:d}]",
                  msg, // Hexadecimal
                  msg, // Decimal
                  SKIF_Util_GetWindowMessageAsStr (msg), // String
                    (hWnd == SKIF_Notify_hWnd ?  " [SKIF_Notify_hWnd] " : " "), // Is the message meant SKIF_Notify_hWnd ?
                  wParam, wParam,
            ((HWND)wParam == SKIF_Notify_hWnd ?  ", SKIF_Notify_hWnd"   : ""),  // Does wParam point to SKIF_Notify_hWnd ?
                  lParam, lParam);

  if (SKIF_WndProc (hWnd, msg, wParam, lParam))
    return true;

  switch (msg)
  {
    case WM_SKIF_NOTIFY_ICON:
      msgDontRedraw = true; // Don't redraw the main window when we're interacting with the notification icon
      switch (lParam)
      {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
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
          //PostMessage (SKIF_hWnd, (_registry.bStopOnInjection) ? WM_SKIF_TEMPSTART : WM_SKIF_START, 0, 0);
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_START, 0, 0);
          break;
        case SKIF_NOTIFY_STARTWITHSTOP:
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_TEMPSTART, 0, 0);
          break;
        case SKIF_NOTIFY_STOP:
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_STOP, 0, 0);
          break;
        case SKIF_NOTIFY_RUN_UPDATER:
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_RUN_UPDATER, 0, 0);
          break;
        case SKIF_NOTIFY_EXIT:
          if (SKIF_ImGui_hWnd != NULL)
            PostMessage (SKIF_ImGui_hWnd, WM_CLOSE, 0, 0);
          break;
      }
      break;

    case WM_CREATE:
      SK_RunOnce (
        SHELL_TASKBAR_RESTART        = RegisterWindowMessage (TEXT ("TaskbarCreated"));
        SHELL_TASKBAR_BUTTON_CREATED = RegisterWindowMessage (TEXT ("TaskbarButtonCreated"));
      );
      break;
        
    default:
      // Taskbar was recreated (explorer.exe restarted),
      //   so we need to recreate the notification icon
      if (msg == SHELL_TASKBAR_RESTART)
      {
        SKIF_Shell_DeleteNotifyIcon ( );
        SKIF_Shell_CreateNotifyIcon ( );
      }
      break;
  }
  return
    ::DefWindowProc (hWnd, msg, wParam, lParam);
}
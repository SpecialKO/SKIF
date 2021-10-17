//
// Copyright 2020-2021 Andon "Kaldaien" Coleman
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

#include <SKIF.h>
#include <injection.h>
#include <fsutil.h>

#include <font_awesome.h>

#include <sk_utility/utility.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <wtypes.h>
#include <process.h>
#include <Shlwapi.h>
#include <cstdio>
#include <array>
#include <vector>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <regex>
#include <string>
#include <sstream>

SKIF_InjectionContext _inject;

bool
SKIF_InjectionContext::pid_directory_watch_s::isSignaled (void)
{
  bool bRet = false;

  if (hChangeNotification != INVALID_HANDLE_VALUE)
  {
    bRet =
      ( WAIT_OBJECT_0 ==
          WaitForSingleObject (hChangeNotification, 0) );

    if (bRet)
    {
      FindNextChangeNotification (
        hChangeNotification
      );
    }
  }

  return bRet;
}

SKIF_InjectionContext::pid_directory_watch_s::pid_directory_watch_s (void)
{
  // This actually runs first out of the whole executable, so initialize core components.
  SKIF_Initialize ( );

  GetCurrentDirectoryW ( MAX_PATH, wszDirectory             );
  PathAppendW          (           wszDirectory, L"Servlet" );

  hChangeNotification =
    FindFirstChangeNotificationW (
      wszDirectory, FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME
    );

  if (hChangeNotification != INVALID_HANDLE_VALUE)
  {
    FindNextChangeNotification (
      hChangeNotification
    );
  }
}

SKIF_InjectionContext::pid_directory_watch_s::~pid_directory_watch_s (void)
{
  if (      hChangeNotification != INVALID_HANDLE_VALUE)
    FindCloseChangeNotification (hChangeNotification);
}

void SKIF_InjectionContext::_ToggleOnDemand (bool newState)
{
  extern HWND    SKIF_hWnd;
  extern CHandle hInjectAck;

  // Close any existing handles
  KillTimer (SKIF_hWnd, IDT_REFRESH_ONDEMAND);
  hInjectAck.Close();

  // Create a new handle if requested
  if (newState && hInjectAck.m_h <= 0)
  {
    hInjectAck.Attach (
      CreateEvent ( nullptr, FALSE, FALSE, LR"(Local\SKIF_InjectAck)" )
    );

    SetTimer (SKIF_hWnd,
              IDT_REFRESH_ONDEMAND,
              500,
              (TIMERPROC) NULL
    );
  }
}

bool SKIF_InjectionContext::isPending(void)
{
  if (runState == Starting || runState == Stopping)
    return true;
  else
    return false;
}

bool SKIF_InjectionContext::_StartStopInject (bool currentRunningState, bool autoStop)
{
  extern HWND    SKIF_hWnd;
  bool ret = false;

  KillTimer (SKIF_hWnd, IDT_REFRESH_PENDING);

  bAckInj = (! currentRunningState) ? autoStop : bAckInj;
  _ToggleOnDemand ((! currentRunningState) ? autoStop : false);

#if 0
  const wchar_t *wszStartStopCommand =
              LR"(rundll32.exe)";

  const wchar_t *wszStartStopParams32 =
    currentRunningState ? L"../SpecialK32.dll,RunDLL_InjectionManager Remove"
                        : L"../SpecialK32.dll,RunDLL_InjectionManager Install";
  wchar_t                   wszStartStopCommand32 [MAX_PATH + 2] = { };
  GetSystemWow64DirectoryW (wszStartStopCommand32, MAX_PATH);
  //GetSystemDirectoryW      (wszStartStopCommand32, MAX_PATH);
  PathAppendW              (wszStartStopCommand32, wszStartStopCommand);

  const wchar_t *wszStartStopParams64 =
    currentRunningState ? L"../SpecialK64.dll,RunDLL_InjectionManager Remove"
                        : L"../SpecialK64.dll,RunDLL_InjectionManager Install";
  wchar_t                   wszStartStopCommand64 [MAX_PATH + 2] = { };

  GetSystemDirectoryW      (wszStartStopCommand64, MAX_PATH);
  PathAppendW              (wszStartStopCommand64, wszStartStopCommand);
#endif

  //HANDLE h32, h64;

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = L"OPEN";
    sexi.lpFile       = LR"(SKIFsvc32.exe)";
    sexi.lpParameters = currentRunningState ? L"Stop" : L"Start";
    sexi.lpDirectory  = L"Servlet";
    sexi.nShow        = SW_HIDE;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI | /* SEE_MASK_NOCLOSEPROCESS | */
                        SEE_MASK_NOASYNC    | SEE_MASK_NOZONECHECKS;
    //sexi.hProcess     = &h32;

#ifdef _WIN64
  if ( ShellExecuteExW (&sexi) || currentRunningState )
  {
    // If we are currently running, try to shutdown 64-bit even if 32-bit fails.
    sexi.lpFile       = LR"(SKIFsvc64.exe)";
    sexi.lpParameters = currentRunningState ? L"Stop" : L"Start";
    //sexi.hProcess     = &h64;

    ret =
      ShellExecuteExW (&sexi);
  }
#else
  ret =
    ShellExecuteExW (&sexi);
#endif

  if (currentRunningState)
    runState = RunningState::Stopping;
  else
    runState = RunningState::Starting;

  SetTimer (SKIF_hWnd,
            IDT_REFRESH_PENDING,
            500,
            (TIMERPROC) NULL
  );

  dwLastSignaled = SKIF_timeGetTime();

  return ret;
};

SKIF_InjectionContext::SKIF_InjectionContext (void)
{
  bHasServlet =
    PathFileExistsW  (L"Servlet");

  // Cache the Special K user data path
  SKIF_GetFolderPath (&path_cache.specialk_userdata);
  PathAppendW (        path_cache.specialk_userdata.path,
                         LR"(My Mods\SpecialK)"  );

  bLogonTaskEnabled =
    PathFileExistsW (LR"(Servlet\SpecialK.LogOn)");

  if (! bLogonTaskEnabled)
    DeleteFile (LR"(Servlet\task_inject.bat)");

  // Attempt to remove .old files, if any exists
  DeleteFile (L"SpecialK32.old");
#ifdef _WIN64
  DeleteFile (L"SpecialK64.old");
#endif

  struct updated_file_s {
    const wchar_t* wszFileName;
    const wchar_t* wszRealExt;
    const wchar_t* wszPIDFile;

    const wchar_t*
      combineParts ( wchar_t* wszInOut,
               const wchar_t* wszName,
               const wchar_t* wszExt ) const {
      StrCatW           (wszInOut, wszName);
      PathAddExtensionW (wszInOut, wszExt );

      return wszInOut;
    }

    bool isNewer (void) const {
      wchar_t        wszNewFile [MAX_PATH] = { };
      combineParts ( wszNewFile, wszFileName,
                      L".new" );

      return
        PathFileExistsW (wszNewFile);
    }

    // In rare cases, SK's DLLs may be stuck in a rogue app and we cannot
    //   release the lock on them. We need to move the files out of the way.
    bool shuffleLockedFiles (void) const {
      wchar_t wszNewFile [MAX_PATH] = { },
              wszOldFile [MAX_PATH] = { },
              wszIOFile  [MAX_PATH] = { };

      combineParts (wszNewFile, wszFileName, L".new");
      combineParts (wszOldFile, wszFileName, L".old");
      combineParts (wszIOFile,  wszFileName, wszRealExt);

      MoveFileExW ( wszIOFile,  wszOldFile,
                    MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH );
      MoveFileExW ( wszNewFile, wszIOFile,
                    MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH );

      return true;
    }
  };

  std::vector <updated_file_s>
               updated_files =
    { { L"SpecialK64", L".dll", LR"(Servlet\SpecialK64.pid)" },
      { L"SpecialK32", L".dll", LR"(Servlet\SpecialK32.pid)" } };

  int updates_pending = 0;

  for ( const auto& file : updated_files )
    if ( file.isNewer () )
      ++updates_pending;

  if (updates_pending > 0)
  {
    for ( auto& file : updated_files )
    {
      FILE* fPID =
        _wfopen ( file.wszPIDFile, L"r" );

      if (fPID != nullptr)
      {
        int pid   = 0;
        int count =
          fwscanf (fPID, L"%li", &pid);
        fclose    (fPID);

        DeleteFileW (file.wszPIDFile);

        if (count == 1 &&  pid != 0)
          SK_TerminatePID (pid, 0x0);
      }

      file.shuffleLockedFiles ();
    }
  }

  bHasServlet =
    bHasServlet &&
    PathFileExistsW (L"SpecialK32.dll");

#ifdef _WIN64
  bHasServlet =
    bHasServlet &&
    PathFileExistsW (L"SpecialK64.dll");
#endif

  // Force a one-time check on launch
  TestServletRunlevel (true);

  // Update bCurrentState to reflect the run level
  bCurrentState = (pid32 || pid64);
  
  // Force the overlay to update itself as well
  //_SetTaskbarOverlay (bCurrentState);

  // Load the whitelist and blacklist
  _LoadList  (true);
  _LoadList (false);
}

void
SKIF_InjectionContext::TestServletRunlevel (bool forcedCheck)
{
  static DWORD dwFailed   = NULL;
  static bool  triedToFix = false;

  // Perform a forced check every 500ms if we have been transitioning over for longer than half a second
  if ((runState == Starting || runState == Stopping) && dwLastSignaled + 500 < SKIF_timeGetTime())
    forcedCheck = true;

  if (dir_watch.isSignaled() || forcedCheck)
  {
    dwLastSignaled = SKIF_timeGetTime();

    for ( auto& record : records )
    {
      // If we currently assume the service is not running, check if it's running
      if (                 *record.pPid == 0 &&
           PathFileExistsW (record.wszPidFilename) )
      {
        record.fPidFile =
          _wfopen (record.wszPidFilename, L"r");

        if (record.fPidFile != nullptr)
        {
          int count =
            fwscanf (record.fPidFile, L"%li", record.pPid);
            fclose  (record.fPidFile);

          if (count != 1)
               *record.pPid = 0;
        }
      }

      // Verify the claimed PID is still running...
      SetLastError (NO_ERROR);
      CHandle hProcess (
                (*record.pPid != 0)
                              ? OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, *record.pPid)
                              : NULL);

      bool accessDenied =
        GetLastError ( ) == ERROR_ACCESS_DENIED;

      // Do not continue it if we get access denied, as it means the PID is running outside of our security context
      if (! accessDenied)
      {
        // Get exit code to filter out zombie processes
        DWORD dwExitCode = 0;
        GetExitCodeProcess(hProcess, &dwExitCode);

        // If the PID is not active (it is either terminated or a zombie process), delete the file.
        if (dwExitCode != STILL_ACTIVE)
        {
          DeleteFileW (record.wszPidFilename);
                      *record.pPid = 0;
        }
      }
    }

    extern void SKIF_CreateNotifyToast (std::wstring message, std::wstring title = L"");
    extern void SKIF_UpdateNotifyIcon (void);
    extern CHandle hInjectAck;

    // If we are transitioning away from a pending state
#ifdef _WIN64
    if (runState == Starting &&   pid32 &&   pid64 ||
        runState == Stopping && ! pid32 && ! pid64)
    {
      if (pid32 && pid64)
#else
    if (runState == Starting &&   pid32 ||
        runState == Stopping && ! pid32)
    {
      if (pid32)
#endif
      {
        bCurrentState     = true;
        runState          = Started;

        SKIF_UpdateNotifyIcon ( );

        if (bAckInj)
          SKIF_CreateNotifyToast (L"Special K is waiting for the game to launch...",   L"Service started");
        else
          SKIF_CreateNotifyToast (L"Special K is now being injected into your games!", L"Service started");
      }
      else
      {
        bCurrentState     = false;
        bAckInj           = false;
        bAckInjSignaled   = false;
        runState          = Stopped;

        SKIF_UpdateNotifyIcon ( );

        if (bAckInjSignaled)
          SKIF_CreateNotifyToast (L"Special K has now been injected into your game!",  L"Service stopped");
        else
          SKIF_CreateNotifyToast (L"Special K will no longer be injected into games.", L"Service stopped");
      }

      dwFailed   = NULL;
      triedToFix = false;
      
      _SetTaskbarOverlay (bCurrentState);
        
      extern HWND SKIF_hWnd;
      KillTimer  (SKIF_hWnd, IDT_REFRESH_PENDING);
    }
    // Switch the state over if the service has been
    //   toggled in the background through another method
#if _WIN64
    else if (runState == Stopped &&   pid32 &&   pid64)
#else
    else if (runState == Stopped &&   pid32)
#endif
    {
      runState = Started;
      bCurrentState = true;
    }
#if _WIN64
    else if (runState == Started && ! pid32 && ! pid64)
#else
    else if (runState == Stopped && ! pid32)
#endif
    {
      runState = Stopped;
      bCurrentState = false;
    }
    // If SKIF seems stuck in a starting transition, attempt to forcefully start the service again after 5000ms
    else if (runState == Starting && ! triedToFix)
    {
      if (dwFailed == NULL)
        dwFailed = SKIF_timeGetTime();

      if (dwFailed + 5000 < SKIF_timeGetTime())
      {
        triedToFix = true;
        dwFailed = NULL;
        _StartStopInject (false, bAckInj);
      }
    }
    // If SKIF seems stuck in a stopping transition, attempt to forcefully stop the service again after 5000ms
    else if (runState == Stopping && ! triedToFix)
    {
      if (dwFailed == NULL)
        dwFailed = SKIF_timeGetTime();

      if (dwFailed + 5000 < SKIF_timeGetTime())
      {
        triedToFix = true;
        dwFailed = NULL;
        _StartStopInject (true);
      }
    }
    else {
      dwFailed = NULL;
    }
  }
};

extern bool SKIF_ImGui_BeginChildFrame(ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);
extern std::wstring SKIF_GetSpecialKDLLVersion(const wchar_t*);
extern bool SKIF_bDisableExitConfirmation;

void SKIF_InjectionContext::_RefreshSKDLLVersions (void)
{
  wchar_t                       wszPathToSelf32 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSelf32, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSelf32);
  PathAppendW         (         wszPathToSelf32, L"SpecialK32.dll");
  SKVer32 =
    SK_WideCharToUTF8 (SKIF_GetSpecialKDLLVersion (wszPathToSelf32));

#ifdef _WIN64
  wchar_t                       wszPathToSelf64 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSelf64, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSelf64);
  PathAppendW         (         wszPathToSelf64, L"SpecialK64.dll");
  SKVer64 =
    SK_WideCharToUTF8 (SKIF_GetSpecialKDLLVersion (wszPathToSelf64));
#endif
}

void
SKIF_InjectionContext::_GlobalInjectionCtl (void)
{
  extern float SKIF_ImGui_GlobalDPIScale;
  extern bool  SKIF_bStopOnInjection;

  //running =
    //TestServletRunlevel (run_lvl_changed);

  // Injection Summary
  auto frame_id =
    ImGui::GetID ("###Global_Injection_Summary_Frame");

  SKIF_ImGui_BeginChildFrame (
    frame_id, ImVec2 (0.0f, 4.0f * ImGui::GetTextLineHeightWithSpacing ()),
      ImGuiWindowFlags_NavFlattened      |
      ImGuiWindowFlags_NoScrollbar       |
      ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBackground
  );

  // Begin columns
  ImGui::BeginGroup ();

  // Column 1
  ImGui::BeginGroup      ();
#ifdef _WIN64
  ImGui::TextUnformatted ( (SKVer32 == SKVer64) ?
            ( "Special K v " + SKVer32 ).c_str () :
              "Special K" );
#else
  ImGui::TextUnformatted ( ( "Special K v " + SKVer32 ).c_str () );
#endif
  ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  ImGui::TextUnformatted ("Config Root:");
  ImGui::TextUnformatted ("32-bit Service:");
#ifdef _WIN64
  ImGui::TextUnformatted ("64-bit Service:");
#else
  ImGui::NewLine         ();
#endif
  ImGui::PopStyleColor   ();
  ImGui::ItemSize        (
    ImVec2 ( 140.f * SKIF_ImGui_GlobalDPIScale,
               0.f )     ); // Column should have min-width 130px (scaled with the DPI)
  ImGui::EndGroup        ();

  ImGui::SameLine        ();
                         
  // Column 2            
  ImGui::BeginGroup      ();
  ImGui::NewLine         ();

  // Config Root
  static std::wstring root_dir =
    std::wstring (path_cache.specialk_userdata.path);

  if (ImGui::Selectable ("Centralized"))
  {
    SKIF_Util_OpenURI (root_dir);
  }

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       (
    SK_WideCharToUTF8 (root_dir).c_str ()
  );
  //SKIF_ImGui_SetHoverTip        ("Open the config root folder");

  // 32-bit/64-bit Services
  if (pid32)
    ImGui::TextColored (ImColor::HSV (0.3F,  0.99F, 1.F), "Running");
  else
    ImGui::TextColored (ImColor::HSV (0.08F, 0.99F, 1.F), "Not Running");

#ifdef _WIN64
  if (pid64)
    ImGui::TextColored (ImColor::HSV (0.3F,  0.99F, 1.F), "Running");
  else
    ImGui::TextColored (ImColor::HSV (0.08F, 0.99F, 1.F), "Not Running");
#else
  ImGui::NewLine  ();
#endif

  ImGui::ItemSize ( ImVec2 (
                      100.f * SKIF_ImGui_GlobalDPIScale, 
                        0.f)
                  ); // Column should have min-width 100px (scaled with the DPI)
  ImGui::EndGroup ();

  ImGui::SameLine ();

  // Column 3
  ImGui::BeginGroup ();
  // empty
  ImGui::NewLine    ();
  ImGui::NewLine    ();

#ifdef _WIN64
  if (SKVer32 != SKVer64)
  {
    ImGui::Text ("( v %s )", SKVer32.c_str ());
    ImGui::Text ("( v %s )", SKVer64.c_str ());
  }
  else
    ImGui::NewLine ();
#endif

  ImGui::EndGroup  ();

  // End columns
  ImGui::EndGroup      ();
  ImGui::EndChildFrame ();


  ImGui::Separator ();



  // Start/Stop Service
  auto frame_id2 =
    ImGui::GetID ("###Global_Injection_Toggle_Frame");

  ImGui::PushStyleVar (
    ImGuiStyleVar_FramePadding,
      ImVec2 ( 120.0f * SKIF_ImGui_GlobalDPIScale,
                40.0f * SKIF_ImGui_GlobalDPIScale )
  );

  SKIF_ImGui_BeginChildFrame (
    frame_id2,
      ImVec2 (  0.0f,
               110.f *SKIF_ImGui_GlobalDPIScale ),
        ImGuiWindowFlags_NavFlattened      |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground
  );

  ImGui::PopStyleVar ();


  if ( ! bHasServlet )
  {
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
  }
    
  if (runState == Started || runState == Stopped)
  {
    const char *szStartStopLabel =
      bCurrentState ?  "Stop Service###GlobalStartStop"  :
                       "Start Service###GlobalStartStop";

    if (ImGui::Button (szStartStopLabel, ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                                   50.0f * SKIF_ImGui_GlobalDPIScale )))
      _StartStopInject (bCurrentState, SKIF_bStopOnInjection);
  }

  else
    ImGui::ButtonEx (runState == Stopping ? "Stopping...###GlobalStartStop" :
                                            "Starting...###GlobalStartStop",
                      ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                50.0f * SKIF_ImGui_GlobalDPIScale ),
                        ImGuiButtonFlags_Disabled );

  if ( ! bCurrentState && SKIF_bDisableExitConfirmation)
      SKIF_ImGui_SetHoverTip ("Service continues running after SKIF is closed");
    
  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ImGui::OpenPopup ("ServiceMenu");

  if (ImGui::BeginPopup ("ServiceMenu"))
  {
    ImGui::TextColored (
      ImColor::HSV (0.11F, 1.F, 1.F),
        "Troubleshooting:"
    );

    ImGui::Separator ( );

    if (ImGui::Selectable("Force Start Service"))
      _StartStopInject (false, SKIF_bStopOnInjection);

    if (ImGui::Selectable("Force Stop Service"))
      _StartStopInject (true);

    ImGui::EndPopup ( );
  }

  if ( ! bHasServlet )
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
  }

  ImGui::EndChildFrame ();



  // Tips 'n Tricks
  auto frame_id3 =
    ImGui::GetID ("###Global_Injection_TipsNTricks");
  
  ImGui::SetCursorPosY (
    ImGui::GetWindowHeight () - fBottomDist -
    ImGui::GetStyle        ().ItemSpacing.y
  );

  ImGui::Separator     ( );

  SKIF_ImGui_BeginChildFrame ( frame_id3,
                                ImVec2 (ImGui::GetContentRegionAvail ().x,
                              std::max (ImGui::GetContentRegionAvail ().y,
                                        ImGui::GetTextLineHeight () + ImGui::GetStyle ().FramePadding.y * 2.0f + ImGui::GetStyle ().ItemSpacing.y * 2
                                       )),
                                ImGuiWindowFlags_NavFlattened      |
                                ImGuiWindowFlags_NoScrollbar       |
                                ImGuiWindowFlags_NoScrollWithMouse |
                                ImGuiWindowFlags_NoBackground
  );

  if ( bHasServlet )
  {
    /*
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
    */

    extern void SKIF_putStopOnInjection(bool in);

#ifdef _WIN64
    if (_inject.SKVer64 >= "21.08.12" &&
        _inject.SKVer32 >= "21.08.12")
#else
    if (_inject.SKVer32 >= "21.08.12")
#endif
    {
      if (ImGui::Checkbox ("Stop automatically", &SKIF_bStopOnInjection))
        SKIF_putStopOnInjection (SKIF_bStopOnInjection);

      SKIF_ImGui_SetHoverTip ("If this is enabled the service will stop automatically\n"
                              "when Special K is injected into a whitelisted game.");
    }

    else {
      ImGui::TextColored  (ImColor::HSV (0.11F, 1.F, 1.F), "Auto-stop is not available due to Special K being outdated.");
      SKIF_ImGui_SetHoverTip ("The feature requires Special K v21.08.12 or newer.");
    }
  }

  else {
    ImGui::TextColored    (ImColor::HSV (0.11F, 1.F, 1.F), "Global injection is unavailable due to missing files.");
  }

  ImGui::EndChildFrame ();

  fBottomDist = ImGui::GetItemRectSize().y;
};

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

HRESULT
CreateLink(LPCWSTR lpszPathObj, LPCSTR lpszPathLink, LPCWSTR lpszArgs, LPCWSTR lpszDesc)
{
  HRESULT hres;
  IShellLink* psl;

  //CoInitializeEx (nullptr, 0x0);

  // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
  // has already been called.
  hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);

  if (SUCCEEDED(hres))
  {
    IPersistFile* ppf;

    // Set the path to the shortcut target and add the description. 
    psl->SetPath(lpszPathObj);
    psl->SetWorkingDirectory(std::filesystem::path(lpszPathObj).parent_path().c_str());
    psl->SetArguments(lpszArgs);
    psl->SetDescription(lpszDesc);

    // Query IShellLink for the IPersistFile interface, used for saving the 
    // shortcut in persistent storage. 
    //hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
    hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);

    if (SUCCEEDED(hres))
    {

      WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode. 
      MultiByteToWideChar(CP_ACP, 0, lpszPathLink, -1, wsz, MAX_PATH);

      // Save the link by calling IPersistFile::Save. 
      hres = ppf->Save(wsz, FALSE);
      if (SUCCEEDED(hres))
      {
        // Handle success
        // Despite succeeding, this throws an error...?
      }
      else
      {
        // Handle the error
      }

      ppf->Release();
    }
    psl->Release();
  }
  return hres;
}

void
SKIF_InjectionContext::_StartAtLogonCtrl (void)
{
  ImGui::BeginGroup ();

  static bool argsChecked = false;
  static std::wstring args = L"\0";
  static HKEY hKey;

  auto _CheckRegistry = [&](void) ->
    bool
  {
    bool ret = false;

    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
      if ((ERROR_SUCCESS == RegGetValueW (hKey, NULL, L"Special K 32-bit Global Injection Service Host", RRF_RT_REG_SZ, NULL, NULL, NULL)) ||
          (ERROR_SUCCESS == RegGetValueW (hKey, NULL, L"Special K 64-bit Global Injection Service Host", RRF_RT_REG_SZ, NULL, NULL, NULL)))
        ret = true;

      RegCloseKey (hKey);
    }

    return ret;
  };

  static path_cache_s::win_path_s user_startup  =
  {            FOLDERID_Startup,
    L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp"
  };

  SK_RunOnce (SKIF_GetFolderPath (&user_startup));

  static std::string link    = SK_FormatString ( R"(%ws\SKIF.lnk)",
                               user_startup.path );

  static std::wstring Svc32Target = SK_FormatStringW(LR"("%ws\Servlet\SKIFsvc32.exe")", path_cache.specialk_userdata.path),
                      Svc64Target = SK_FormatStringW(LR"("%ws\Servlet\SKIFsvc64.exe")", path_cache.specialk_userdata.path);

  static std::string  Svc32Link = SK_FormatString(R"(%ws\SKIFsvc32.lnk)", user_startup.path),
                      Svc64Link = SK_FormatString(R"(%ws\SKIFsvc64.lnk)", user_startup.path);

  static bool dontCare = bAutoStartServiceOnly = _CheckRegistry() ||
                                                 PathFileExistsW(SK_UTF8ToWideChar(Svc32Link).c_str()) ||
                                                 PathFileExistsW(SK_UTF8ToWideChar(Svc32Link).c_str());

  // New method

  if (bLogonTaskEnabled ||
      bAutoStartServiceOnly)
  {
    // Disable button
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
  }
  
  ImGui::BeginGroup  ();

  /*
  SK_RunOnce(
    bAutoStartSKIF = PathFileExists(SK_UTF8ToWideChar(link).c_str())
  );
  */

  if ( ! argsChecked ) // && bAutoStartSKIF )
  {
    //extern HWND SKIF_hWnd;
    //WCHAR szTarget   [MAX_PATH];
    WCHAR szArguments[MAX_PATH];

    //ResolveIt (SKIF_hWnd, link.c_str(), szTarget, szArguments, MAX_PATH);

    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
      DWORD dwSize = sizeof(szArguments) / sizeof(WCHAR);
      if (ERROR_SUCCESS == RegGetValueW (hKey, NULL, L"Special K", RRF_RT_REG_SZ, NULL, &szArguments, &dwSize))
      {
        bAutoStartSKIF = true;
        args = szArguments;
      }

      RegCloseKey (hKey);
    }
    
    bAutoStartService = (args.find (L"START")    != std::wstring::npos);
    bStartMinimized   = (args.find (L"MINIMIZE") != std::wstring::npos);
  }

  argsChecked = true;

  static bool changes = false;

  if (ImGui::Checkbox("Start SKIF with Windows", &bAutoStartSKIF))
    changes = true;

  if (! bAutoStartSKIF)
  {
    // Disable buttons
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
  }

  ImGui::TreePush ("");

  extern bool SKIF_bCloseToTray;

  if (ImGui::Checkbox(" " ICON_FA_PLAY " Start the global injection service as well", &bAutoStartService))
    changes = true;
    

  if (ImGui::Checkbox((SKIF_bCloseToTray) ? " " ICON_FA_WINDOW_MINIMIZE " Start SKIF minimized in notification area" :
                                            " " ICON_FA_WINDOW_MINIMIZE " Start SKIF minimized", &bStartMinimized))
    changes = true;

  ImGui::TreePop  ( );

  if (! bAutoStartSKIF)
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
  }

  if (changes)
  {
    DeleteFileW (SK_UTF8ToWideChar(link).c_str());

    if (bStartMinimized)
      args = (bAutoStartService) ? L"START MINIMIZE" : L"MINIMIZE";
    else
      args = (bAutoStartService) ? L"START"          : L"";
    
    /*
    static TCHAR                             szExePath[MAX_PATH];
    GetModuleFileName                 (NULL, szExePath, _countof(szExePath));     // Set the executable path
    */
    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
      if (bAutoStartSKIF)
      {
        //CreateLink (szExePath, link.c_str(), args.c_str(), L"Special K Injection Frontend");
          static TCHAR               szExePath[MAX_PATH];
          GetModuleFileName   (NULL, szExePath, _countof(szExePath));

          std::wstring wsPath = LR"(")" + std::wstring(szExePath) + LR"(" )" + args;

          RegSetValueExW (hKey, L"Special K", 0, REG_SZ, (LPBYTE)wsPath.data(),
                                                          (DWORD)wsPath.size() * sizeof(wchar_t));
      }
      else
      {
        RegDeleteValueW  (hKey, L"Special K");

        bAutoStartService = bStartMinimized = false;
      }

      RegCloseKey (hKey);
    }

    changes = false;
  }

  ImGui::EndGroup    ();
  
  if (bLogonTaskEnabled ||
      bAutoStartServiceOnly)
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();

    SKIF_ImGui_SetHoverTip ( "The current autostart method needs to be disabled to migrate over to this method.\n"
                             "The difference is that this method autostarts SKIF, and not just the GI service." );
  }

  // Legacy method, only appear if it is actually enabled or debug mode is enabled
  extern bool SKIF_bEnableDebugMode;

  if ( bLogonTaskEnabled     ||
       bAutoStartServiceOnly ||
       SKIF_bEnableDebugMode )
  {

    if (bLogonTaskEnabled)
      _StartAtLogonCtrlLegacy ( );
    
    // New approach to the legacy method
    ImGui::BeginGroup ();

    if ( bLogonTaskEnabled || 
          bAutoStartSKIF )
    {
      // Disable button
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }
  
    if (ImGui::Checkbox ("Start the global injection service with Windows", &dontCare))
    {
      if (! bAutoStartServiceOnly)
      {
        if (MessageBox(NULL, L"This will start the global injection service hidden in the background with Windows.\n"
                              L"\n"
                              L"Special K Injection Frontend (SKIF) will not autostart.\n"
                              L"\n"
                              L"Are you sure you want to proceed?",
                              L"Confirm autostart",
                              MB_YESNO | MB_ICONWARNING) == IDYES)
        {
          //CreateLink (Svc32Target.c_str(), Svc32Link.c_str(), NULL, L"Special K 32-bit Global Injection Service Host");
          //CreateLink (Svc64Target.c_str(), Svc64Link.c_str(), NULL, L"Special K 64-bit Global Injection Service Host");
        
          if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
          {
            TCHAR               szExePath[MAX_PATH];
            GetModuleFileName   (NULL, szExePath, _countof(szExePath));

            std::wstring wsPath = std::wstring(szExePath);

            RegSetValueExW (hKey, L"Special K 32-bit Global Injection Service Host", 0, REG_SZ, (LPBYTE)Svc32Target.data(),
                                                                                                 (DWORD)Svc32Target.size() * sizeof(wchar_t));
#ifdef _WIN64
            RegSetValueExW (hKey, L"Special K 64-bit Global Injection Service Host", 0, REG_SZ, (LPBYTE)Svc64Target.data(),
                                                                                                 (DWORD)Svc64Target.size() * sizeof(wchar_t));
#endif
            RegCloseKey (hKey);
          }

          bAutoStartServiceOnly = ! bAutoStartServiceOnly;
        }

        else {
          dontCare = ! dontCare;
        }
      }

      else {
        DeleteFileW(SK_UTF8ToWideChar(Svc32Link).c_str());
        DeleteFileW(SK_UTF8ToWideChar(Svc64Link).c_str());
        
        if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
        {
          RegDeleteValueW (hKey, L"Special K 32-bit Global Injection Service Host");
          RegDeleteValueW (hKey, L"Special K 64-bit Global Injection Service Host");

          RegCloseKey (hKey);
        }

        bAutoStartServiceOnly = ! bAutoStartServiceOnly;
      }
    }
  
    SKIF_ImGui_SetHoverTip (
        "Note that this injection frontend (SKIF) will not start with Windows."
    );
  
    if ( bLogonTaskEnabled || 
          bAutoStartSKIF )
    {
      ImGui::PopStyleVar ();
      ImGui::PopItemFlag ();

      if (bAutoStartSKIF)
        SKIF_ImGui_SetHoverTip ( "The regular autostart method needs to be disabled to migrate over to this method.\n"
                                  "The difference is that the current method autostarts SKIF, and not just the GI service.");
      else
        SKIF_ImGui_SetHoverTip ( "The old legacy method needs to be disabled to migrate over to this new method.\n"
                                  "The difference is that this method does not require elevated privileges." );
    }

    ImGui::EndGroup      ();

  }

  ImGui::EndGroup      ( );
}

/* Legacy option
* 
* Autostarts the global injection service on logon.
* SKIF will not start alongside the background service!
* 
*/
void
SKIF_InjectionContext::_StartAtLogonCtrlLegacy (void)
{
  ImGui::BeginGroup ();
  
  if (ImGui::Checkbox ("Start Global Injection Service At Logon (obsolete) " ICON_FA_SHIELD_ALT, &bLogonTaskEnabled))
  {
    if (
      ShellExecuteW (
        nullptr, L"runas",
          L"SCHTASKS",
            LR"(/delete /tn "SK_InjectLogon" /f)", nullptr,
              SW_HIDE ) > (HINSTANCE)32 )
    {
      DeleteFile (LR"(Servlet\SpecialK.LogOn)");
      DeleteFile (LR"(Servlet\task_inject.bat)");
    }
    else
      bLogonTaskEnabled =
        ! bLogonTaskEnabled;
  }
  
  SKIF_ImGui_SetHoverTip ("This method is obsolete, and can only be disabled.");

  ImGui::EndGroup      ();
}

HRESULT
SKIF_InjectionContext::_SetTaskbarOverlay (bool show)
{
  //CoInitializeEx (nullptr, 0x0); // Breaks overlay on start

  extern void SKIF_CreateUpdateNotifyMenu (void);
  SKIF_CreateUpdateNotifyMenu ( );

  CComPtr <ITaskbarList3> taskbar;
  if ( SUCCEEDED (
         CoCreateInstance ( CLSID_TaskbarList, 0, CLSCTX_INPROC_SERVER,
                              IID_ITaskbarList3, (void **)&taskbar.p)
     ) )
  {
    extern HWND    SKIF_hWnd;
    HICON hIcon = LoadIcon(hModSKIF, MAKEINTRESOURCE(IDI_SKIFON));

    if (hIcon != NULL)
    {
      if (show)
        taskbar->SetOverlayIcon (SKIF_hWnd, hIcon, L"Global injection service is running.");
      else
        taskbar->SetOverlayIcon (SKIF_hWnd, NULL, NULL);

      DestroyIcon (hIcon);
      bTaskbarOverlayIcon = show;

      return S_OK;
    }
  }
  bTaskbarOverlayIcon = false;
  return E_UNEXPECTED;
}

bool SKIF_InjectionContext::_StoreList(bool whitelist_)
{
  bool ret = false;
  static std::wstring root_dir =
           std::wstring(path_cache.specialk_userdata.path) + LR"(\Global\)";

  // Create the Documents/My Mods/SpecialK/Global/ folder, and any intermediate ones, if it does not already exist
  std::filesystem::create_directories (root_dir.c_str ());

  std::wofstream list_file(
    (whitelist_) ? (root_dir + LR"(\whitelist.ini)").c_str()
                 : (root_dir + LR"(\blacklist.ini)").c_str()
  );

  if (list_file.is_open())
  {
    // Requires Windows 10 1903+ (Build 18362)
    if (SKIF_IsWindowsVersionOrGreater (10, 0, 18362))
    {
      list_file.imbue (
          std::locale (".UTF-8")
      );
    }

    else
    {
      // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(suppress : 4996)
      list_file.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
    }

    std::wstring out_text =
      SK_UTF8ToWideChar((whitelist_) ? whitelist : blacklist);

    // Strip all null terminator \0 characters from the string
    out_text.erase(std::find(out_text.begin(), out_text.end(), '\0'), out_text.end());

    // Strip all double (or more) newline characters from the string
    out_text = std::regex_replace(out_text, std::wregex(  LR"(\n\n+)"),   L"\n");

    // Strip double pipe characters from the string
    out_text = std::regex_replace(out_text, std::wregex(  LR"(\|\|)"),    L"|");

    // Strip pipe characters at the end of a line from the string
    out_text = std::regex_replace(out_text, std::wregex(  LR"(\|\n)"),    L"\n");

    // Strip trailing pipe characters from the string
    out_text = std::regex_replace(out_text, std::wregex(  LR"(\|+$)"),    L"");

    // Strip trailing newline characters from the string
    out_text = std::regex_replace(out_text, std::wregex(  LR"(\n+$)"),    L"");

    list_file.write(out_text.c_str(),
      out_text.length());

    if (list_file.good())
    {
      // Update the internal variable with the manipulated string
      if (whitelist_)
        snprintf(whitelist, sizeof whitelist, "%s", SK_WideCharToUTF8(out_text).c_str());
      else
        snprintf(blacklist, sizeof blacklist, "%s", SK_WideCharToUTF8(out_text).c_str());

      ret = true;
    }

    list_file.close();
  }

  return ret;
}

void SKIF_InjectionContext::_LoadList(bool whitelist_)
{
  static std::wstring root_dir =
           std::wstring(path_cache.specialk_userdata.path) + LR"(\Global\)";

  std::wifstream list_file(
    (whitelist_) ? (root_dir + LR"(whitelist.ini)").c_str()
                 : (root_dir + LR"(blacklist.ini)").c_str()
  );

  std::wstring full_text;

  if (list_file.is_open ())
  {
    // Requires Windows 10 1903+ (Build 18362)
    if (SKIF_IsWindowsVersionOrGreater (10, 0, 18362))
    {
      list_file.imbue (
          std::locale (".UTF-8")
      );
    }

    else
    {
      // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(suppress : 4996)
      list_file.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
    }

    std::wstring line;

    while (list_file.good ())
    {
      std::getline (list_file, line);

      // Skip blank lines, since they would match everything....
      for (const auto& it : line)
      {
        if (iswalpha(it) != 0)
        {
          full_text += line + L'\n';
          break;
        }
      }
    }

    if (full_text.length() > 0)
      full_text.resize (full_text.length () - 1);

    list_file.close ();

    strcpy ( (whitelist_) ? whitelist : blacklist,
                SK_WideCharToUTF8 (full_text).c_str ()
    );
  }
}

bool SKIF_InjectionContext::_TestUserList (const char* szExecutable, bool whitelist_)
{
  if (  whitelist_ && *whitelist == '\0' ||
      ! whitelist_ && *blacklist == '\0')
    return false;

  if (  whitelist_ && StrStrIA (szExecutable, "SteamApps") != NULL ||
      ! whitelist_ && StrStrIA (szExecutable, "GameBar"  ) != NULL /* ||
      ! whitelist_ && StrStrIA(szExecutable, "Launcher") != NULL */ )
    return true;

  // Check if the executable filename has "launcher" in it:
  // TODO: Confirm this shit works!
  /*
  char     szExecutableCopy [MAX_PATH] = { };
  strncpy (szExecutableCopy, szExecutable, MAX_PATH);
  PathStripPathA (szExecutableCopy);

  if (! whitelist_ && StrStrIA (szExecutableCopy, "Launcher") != NULL )
    return true;
  */

  std::istringstream iss(  (whitelist_)
                          ? whitelist
                          : blacklist);

  for (std::string line; std::getline(iss, line); )
  {
    std::regex regexp (line, std::regex_constants::icase);

    if (std::regex_search(szExecutable, regexp))
      return true;
  }

  return false;
}

void SKIF_InjectionContext::_AddUserList(std::string pattern, bool whitelist_)
{
  if (whitelist_)
  {
    if (*whitelist == '\0')
      snprintf(whitelist, sizeof whitelist, "%s%s", whitelist, pattern.c_str());
    else
      snprintf(whitelist, sizeof whitelist, "%s%s", whitelist, ("|" + pattern).c_str());
  }

  else {
    if (*blacklist == '\0')
      snprintf(blacklist, sizeof blacklist, "%s%s", blacklist, pattern.c_str());
    else
      snprintf(blacklist, sizeof blacklist, "%s%s", blacklist, ("|" + pattern).c_str());
  }
}

void SKIF_InjectionContext::_WhitelistBasedOnPath(std::string fullPath)
{
  // Check if the path has been whitelisted
  if (! _inject._TestUserList (fullPath.c_str(), true))
  {
    // name of parent folder
    std::filesystem::path exePath = std::filesystem::path(fullPath);
    std::string whitelistPattern;

    // Does a parent folder exist?
    if (exePath.has_parent_path() && exePath.parent_path().has_filename())
    {
      // Does another parent folder one level up exist? If so, add it to the pattern
      if (exePath.parent_path().has_parent_path() && exePath.parent_path().parent_path().has_filename())
        whitelistPattern = exePath.parent_path().parent_path().filename().string() + R"(\\)";

      // Add the name of the parent folder to the pattern
      whitelistPattern += exePath.parent_path().filename().string();

      // If this is an Unreal Engine 4 game, add the executable as well
      if (whitelistPattern == R"(Binaries\\Win64)" || whitelistPattern == R"(Binaries\\Win32)")
        whitelistPattern += R"(\\)" + exePath.filename().string();
    }
    else {
      // Add the executable to the pattern if all else fails
      whitelistPattern = std::filesystem::path(fullPath).filename().string();
    }

    // Whitelist path
    _inject._AddUserList (whitelistPattern, true);
    _inject._StoreList   (true);
  }
}

// Header Files for Jump List features
#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <knownfolders.h>
#include <shlobj.h>

void
SKIF_InjectionContext::_InitializeJumpList (void)
{
  //CoInitializeEx (nullptr, 0x0);

  CComPtr <ICustomDestinationList>   pDestList;                                 // The jump list
  CComPtr <IObjectCollection>        pObjColl;                                  // Object collection to hold the custom tasks.
  CComPtr <IShellLink>               pLink;                                     // Reused for the custom tasks
  CComPtr <IObjectArray>             pRemovedItems;                             // Not actually used since we don't carry custom destinations
  PROPVARIANT                        pv;                                        // Used to give the custom tasks a title
  UINT                               cMaxSlots;                                 // Not actually used since we don't carry custom destinations

  TCHAR                                    szExePath[MAX_PATH];
  GetModuleFileName                 (NULL, szExePath, _countof(szExePath));     // Set the executable path
       
  // Create a jump list COM object.
  if     (SUCCEEDED (pDestList.CoCreateInstance (CLSID_DestinationList)))
  {
    pDestList     ->BeginList       (&cMaxSlots, IID_PPV_ARGS(&pRemovedItems));

    if   (SUCCEEDED (pObjColl.CoCreateInstance (CLSID_EnumerableObjectCollection)))
    {
      // Task #1: Start Injection
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink;                        // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (szExePath);
        pLink     ->SetArguments    (L"Start");                                 // Set the arguments  
        pLink     ->SetIconLocation (szExePath, 1);                             // Set the icon location.  
        pLink     ->SetDescription  (L"Starts the global injection service");   // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Start Injection", &pv);
        pPropStore->SetValue                 (PKEY_Title, pv);                  // Set the title property.
        PropVariantClear                                (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Task #2: Start Injection (with auto stop)
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink;                        // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (szExePath);
        pLink     ->SetArguments    (L"Temp Start");                            // Set the arguments  
        pLink     ->SetIconLocation (szExePath, 1);                             // Set the icon location.  
        pLink     ->SetDescription  (L"Starts the global injection service and\n"
                                     L"automatically stops it after injection.");    // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Start Injection (with auto stop)", &pv);
        pPropStore->SetValue                 (PKEY_Title, pv);                  // Set the title property.
        PropVariantClear                                (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Task #3: Stop Injection
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink;                        // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (szExePath);
        pLink     ->SetArguments    (L"Stop");                                  // Set the arguments  
        pLink     ->SetIconLocation (szExePath, 2);                             // Set the icon location.  
        pLink     ->SetDescription  (L"Stops the global injection service");    // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Stop Injection", &pv);
        pPropStore->SetValue                (PKEY_Title, pv);                   // Set the title property.
        PropVariantClear                               (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      // Task #4: Exit
      if (SUCCEEDED (pLink.CoCreateInstance (CLSID_ShellLink)))
      {
        CComQIPtr <IPropertyStore>   pPropStore = pLink;                        // The link title is kept in the object's property store, so QI for that interface.

        pLink     ->SetPath         (szExePath);
        pLink     ->SetArguments    (L"Quit");                                  // Set the arguments  
        pLink     ->SetIconLocation (szExePath, 0);                             // Set the icon location.  
      //pLink     ->SetDescription  (L"Closes the application");                // Set the link description (tooltip on the jump list item)
        InitPropVariantFromString   (L"Exit", &pv);
        pPropStore->SetValue                (PKEY_Title, pv);                   // Set the title property.
        PropVariantClear                               (&pv);
        pPropStore->Commit          ( );                                        // Save the changes we made to the property store
        pObjColl  ->AddObject       (pLink);                                    // Add this shell link to the object collection.
        pPropStore .Release         ( );
        pLink      .Release         ( );
      }

      CComQIPtr <IObjectArray>       pTasksArray = pObjColl;                    // Get an IObjectArray interface for AddUserTasks.
      pDestList   ->AddUserTasks    (pTasksArray);                              // Add the tasks to the jump list.
      pDestList   ->CommitList      ( );                                        // Save the jump list.
      pTasksArray  .Release         ( );

      pObjColl     .Release         ( );
    }
    pDestList      .Release         ( );
  }
}

#if 0
#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <knownfolders.h>
#include <shlobj.h>

wchar_t const c_szTitle       [] = L"Custom Jump List Sample";
wchar_t const c_szWindowClass [] = L"CUSTOMJUMPLISTSAMPLE";

PCWSTR const c_rgpszFiles[] =
{
    L"Microsoft_Sample_1.txt",
    L"Microsoft_Sample_2.txt",
    L"Microsoft_Sample_3.doc",
    L"Microsoft_Sample_4.doc"
};

HRESULT
_CreateShellLink (PCWSTR pszArguments, PCWSTR pszTitle, IShellLink **ppsl)
{
  CComPtr <IShellLink> psl;

  HRESULT hr =
    CoCreateInstance ( CLSID_ShellLink, nullptr,
                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&psl.p) );

  if (SUCCEEDED (hr))
  {
    // Determine our executable's file path so the task will execute this application
    wchar_t                           szAppPath [MAX_PATH] = { };
    if ( GetModuleFileName ( nullptr, szAppPath,
                           ARRAYSIZE (szAppPath) ) )
    {
      hr =
        psl->SetPath (szAppPath);

      if (SUCCEEDED (hr))
      {
        hr =
          psl->SetArguments (pszArguments);

        if (SUCCEEDED (hr))
        {
          // The title property is required on Jump List items provided as an IShellLink
          // instance.  This value is used as the display name in the Jump List.
          CComPtr <IPropertyStore> pps;

          hr =
            psl->QueryInterface (IID_PPV_ARGS (&pps.p));

          if (SUCCEEDED (hr))
          {
            PROPVARIANT
            propvar = { };

            hr =
              InitPropVariantFromString (pszTitle, &propvar);

            if (SUCCEEDED (hr))
            {
              hr =
                pps->SetValue (PKEY_Title, propvar);

              if (SUCCEEDED (hr))
              {
                hr =
                  pps->Commit ();

                if (SUCCEEDED (hr))
                {
                  hr =
                    psl->QueryInterface (IID_PPV_ARGS (ppsl));
                }
              }

              PropVariantClear (&propvar);
            }
          }
        }
      }
    }

    else
    {
      hr =
        HRESULT_FROM_WIN32 (GetLastError ());
    }
  }

  return hr;
}

HRESULT
_CreateSeparatorLink (IShellLink **ppsl)
{
  CComPtr <IPropertyStore> pps;

  HRESULT hr =
    CoCreateInstance ( CLSID_ShellLink, nullptr,
                       CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&pps.p)
    );

  if (SUCCEEDED (hr))
  {
    PROPVARIANT
    propvar = { };
    hr      =
      InitPropVariantFromBoolean (TRUE, &propvar);

    if (SUCCEEDED (hr))
    {
      hr =
        pps->SetValue (PKEY_AppUserModel_IsDestListSeparator, propvar);

      if (SUCCEEDED (hr))
      {
        hr =
          pps->Commit ();

        if (SUCCEEDED (hr))
        {
          hr =
            pps->QueryInterface (IID_PPV_ARGS (ppsl));
        }
      }

      PropVariantClear (&propvar);
    }
  }

  return hr;
}

HRESULT
_AddTasksToList (ICustomDestinationList *pcdl)
{
  CComPtr <IObjectCollection> poc;

  HRESULT hr =
    CoCreateInstance ( CLSID_EnumerableObjectCollection, nullptr,
                       CLSCTX_INPROC, IID_PPV_ARGS (&poc.p) );

  if (SUCCEEDED (hr))
  {
    IShellLink* psl;

    hr =
      _CreateShellLink (L"/Task1", L"Task 1", &psl);

    if (SUCCEEDED (hr))
    {
      hr =
        poc->AddObject (psl);

      psl->Release ();
    }

    if (SUCCEEDED (hr))
    {
      hr =
        _CreateShellLink (L"/Task2", L"Second Task", &psl);

      if (SUCCEEDED (hr))
      {
        hr =
          poc->AddObject (psl);

        psl->Release ();
      }
    }

    if (SUCCEEDED (hr))
    {
      hr =
        _CreateSeparatorLink (&psl);

      if (SUCCEEDED (hr))
      {
        hr =
          poc->AddObject (psl);
      }
    }

    if (SUCCEEDED (hr))
    {
      hr =
        _CreateShellLink (L"/Task3", L"Task 3", &psl);

      if (SUCCEEDED (hr))
      {
        hr =
          poc->AddObject (psl);
      }
    }

    if (SUCCEEDED (hr))
    {
      CComPtr <IObjectArray> poa;

      hr =
        poc->QueryInterface (IID_PPV_ARGS (&poa.p));

      if (SUCCEEDED (hr))
      {
        // Add the tasks to the Jump List. Tasks always appear in the canonical "Tasks"
        // category that is displayed at the bottom of the Jump List, after all other
        // categories.
        hr =
          pcdl->AddUserTasks (poa);
      }
    }
  }

  return hr;
}

bool
_IsItemInArray (IShellItem *psi, IObjectArray *poaRemoved)
{
  bool fRet = false;

  UINT cItems;

  if (SUCCEEDED (poaRemoved->GetCount (&cItems)))
  {
    CComPtr <IShellItem> psiCompare;

    for (UINT i = 0; !fRet && i < cItems; i++)
    {
      if ( SUCCEEDED (
             poaRemoved->GetAt (i, IID_PPV_ARGS (&psiCompare.p))
         )           )
      {
        int iOrder;

        fRet =
          SUCCEEDED (psiCompare->Compare (psi, SICHINT_CANONICAL, &iOrder)) && (0 == iOrder);
      }
    }
  }

  return fRet;
}

// Adds a custom category to the Jump List.  Each item that should be in the category is added to
// an ordered collection, and then the category is appended to the Jump List as a whole.
HRESULT
_AddCategoryToList (ICustomDestinationList *pcdl, IObjectArray *poaRemoved)
{
  CComPtr <IObjectCollection> poc;

  HRESULT hr =
    CoCreateInstance ( CLSID_EnumerableObjectCollection, nullptr,
                       CLSCTX_INPROC, IID_PPV_ARGS (&poc.p) );

  if (SUCCEEDED (hr))
  {
    for (UINT i = 0; i < ARRAYSIZE (c_rgpszFiles); i++)
    {
      CComPtr <IShellItem> psi;

      if ( SUCCEEDED (
             SHCreateItemInKnownFolder ( FOLDERID_Documents, KF_FLAG_DEFAULT,
                                         c_rgpszFiles [i], IID_PPV_ARGS (&psi.p) )
                     )
         )
      {
        // Items listed in the removed list may not be re-added to the Jump List during this
        // list-building transaction.  They should not be re-added to the Jump List until
        // the user has used the item again.  The AppendCategory call below will fail if
        // an attempt to add an item in the removed list is made.
        if (! _IsItemInArray (psi, poaRemoved))
        {
          poc->AddObject (psi);
        }
      }
    }

    CComQIPtr <IObjectArray>
        poa (poc);
    if (poa != nullptr)
    {
      // Add the category to the Jump List.  If there were more categories, they would appear
      // from top to bottom in the order they were appended.
      hr =
        pcdl->AppendCategory (L"Custom Category", poa);
    }
  }

  return hr;
}

#pragma comment (lib,    "comctl32.lib")
#pragma comment (linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

void
InitializeJumpList (void)
{
  // Create the custom Jump List object.
  CComPtr <ICustomDestinationList> pCustomDestList;

  HRESULT hr =
    CoCreateInstance ( CLSID_DestinationList, nullptr,
                       CLSCTX_INPROC_SERVER,
       IID_PPV_ARGS (&pCustomDestList.p)
    );

  if (SUCCEEDED (hr))
  {
    UINT                   cMinSlots;
    CComPtr <IObjectArray> poaRemoved;

    hr =
      pCustomDestList->BeginList ( &cMinSlots,
                                     IID_PPV_ARGS (&poaRemoved.p) );
    if (SUCCEEDED (hr))
    {
      // Add content to the Jump List.
      hr =
        _AddCategoryToList (pCustomDestList, poaRemoved);

      if (SUCCEEDED (hr))
      {
        hr =
          _AddTasksToList (pCustomDestList);

        if (SUCCEEDED (hr))
        {
          hr =
            pCustomDestList->CommitList ();
        }
      }
    }
  }
}

// Removes that existing custom Jump List for this application.
void DeleteJumpList()
{
    ICustomDestinationList *pcdl;
    HRESULT hr = CoCreateInstance(CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pcdl));
    if (SUCCEEDED(hr))
    {
        hr = pcdl->DeleteList(NULL);
        pcdl->Release();
    }
}
#endif
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
#include <strsafe.h>

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

bool SKIF_InjectionContext::_StartStopInject (bool running_, bool autoStop)
{
  bool         _inout = running_;

  extern CHandle hInjectAck;

  if (autoStop && hInjectAck.m_h <= 0)
  {
    hInjectAck.Attach (
      CreateEvent ( nullptr, FALSE, FALSE, LR"(Local\SKIF_InjectAck)" )
    );
  }

  unsigned int tid;
  HANDLE       hThread =
 (HANDLE)
  _beginthreadex ( nullptr,
                         0,
  [](LPVOID lpUser)->unsigned
  {
    bool*           _inout = (bool *)lpUser;
    bool running = *_inout;

    CoInitializeEx ( nullptr,
      COINIT_APARTMENTTHREADED |
      COINIT_DISABLE_OLE1DDE
    );

    const wchar_t *wszStartStopCommand =
                LR"(rundll32.exe)";

    const wchar_t *wszStartStopParams32 =
      running ? L"../SpecialK32.dll,RunDLL_InjectionManager Remove"
              : L"../SpecialK32.dll,RunDLL_InjectionManager Install";

    const wchar_t *wszStartStopParams64 =
      running ? L"../SpecialK64.dll,RunDLL_InjectionManager Remove"
              : L"../SpecialK64.dll,RunDLL_InjectionManager Install";

    wchar_t                   wszStartStopCommand32 [MAX_PATH + 2] = { };
    wchar_t                   wszStartStopCommand64 [MAX_PATH + 2] = { };

    GetSystemWow64DirectoryW (wszStartStopCommand32, MAX_PATH);
    PathAppendW              (wszStartStopCommand32, wszStartStopCommand);

    GetSystemDirectoryW      (wszStartStopCommand64, MAX_PATH);
    PathAppendW              (wszStartStopCommand64, wszStartStopCommand);

    SHELLEXECUTEINFOW
      sexi              = { };
      sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
      sexi.lpVerb       = L"OPEN";
      sexi.lpFile       = wszStartStopCommand32;
      sexi.lpParameters = wszStartStopParams32;
      sexi.lpDirectory  = L"Servlet";
      sexi.nShow        = SW_HIDE;
      sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                          SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;


    if (! running && PathFileExistsW(LR"(Servlet\SKIFsvc32.exe)"))
    {
      sexi.lpFile       = LR"(SKIFsvc32.exe)";
      sexi.lpParameters = L"";
    }

    /*
    if (IsUserAnAdmin())
    {
      sexi.lpParameters = (std::wstring(L"/trustlevel:0x20000 ") + sexi.lpFile + L" " + sexi.lpParameters).c_str();
      sexi.lpFile       = L"runas";
    }

    OutputDebugString(sexi.lpParameters);
    OutputDebugString(L"\n");
    OutputDebugString(sexi.lpFile);
    OutputDebugString(L"\n");
    */

    if ( ShellExecuteExW (&sexi) || running )
    {  // If we are currently running, try to shutdown 64-bit even if 32-bit fails.
      sexi.lpFile       = wszStartStopCommand64;
      sexi.lpParameters = wszStartStopParams64;

      if (! running && PathFileExistsW(LR"(Servlet\SKIFsvc64.exe)"))
      {
        sexi.lpFile       = LR"(SKIFsvc64.exe)";
        sexi.lpParameters = L"";
      }

      /*
      if (IsUserAnAdmin())
      {
        sexi.lpParameters = (std::wstring(L"/trustlevel:0x20000 ") + sexi.lpFile + L" " + sexi.lpParameters).c_str();
        sexi.lpFile       = L"runas";
      }
      */

      *_inout =
        ShellExecuteExW (&sexi);
    }

    else
      *_inout = false;

    _endthreadex (0);

    return 0;
  }, (LPVOID)&_inout, CREATE_SUSPENDED, &tid);

  if (hThread != 0)
  {
    ResumeThread        (hThread);
    WaitForSingleObject (hThread, INFINITE);
    CloseHandle         (hThread);
  }

  // Hack-a-la-Aemony to fix stupid service not stopping properly on exit
  Sleep(50);

  bExpectedState = ! running_;
  bPendingState = true;

  return _inout;
};

SKIF_InjectionContext::SKIF_InjectionContext (void)
{
  hModSelf =
    GetModuleHandleW (nullptr);

  wchar_t             wszPath
   [MAX_PATH + 2] = { };
  GetCurrentDirectoryW (
    MAX_PATH,         wszPath);
  SK_Generate8Dot3   (wszPath);
  GetModuleFileNameW (hModSelf,
                      wszPath,
    MAX_PATH                 );
  SK_Generate8Dot3   (wszPath);

  // Launching SKIF through the Win10 start menu can at times default the working directory to system32.
  // Let's change that to the folder of the executable itself.
  std::filesystem::current_path (
    std::filesystem::path (wszPath).remove_filename ()
  );

  bHasServlet =
    PathFileExistsW  (L"Servlet") ||
    CreateDirectoryW (L"Servlet", nullptr); // Attempt to create the folder if it does not exist

  bLogonTaskEnabled =
    PathFileExistsW (LR"(Servlet\SpecialK.LogOn)");

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
    //if (TestServletRunlevel (run_lvl_changed))
    //  _StartStopInject (true);

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

  //running =
    //TestServletRunlevel (run_lvl_changed);

  bHasServlet =
    bHasServlet &&
    PathFileExistsW (L"SpecialK32.dll") &&
    PathFileExistsW (L"SpecialK64.dll");

  // Force a one-time check on launch
  TestServletRunlevel (true);
  bExpectedState = bCurrentState;

  // Load the whitelist and blacklist
  _LoadList(true);
  _LoadList(false);
}

void
SKIF_InjectionContext::TestServletRunlevel (bool forcedCheck)
{
  if (bExpectedState != bCurrentState)
    forcedCheck = true;

  bool prevState = bCurrentState;

  if (dir_watch.isSignaled () || forcedCheck)
  {
    /*
    OutputDebugString(L"Directory signaled or forced!\n");

    OutputDebugString(L"bCurrentState: ");
    OutputDebugString(std::to_wstring(bCurrentState).c_str());
    OutputDebugString(L"\n");

    OutputDebugString(L"bExpectedState: ");
    OutputDebugString(std::to_wstring(bExpectedState).c_str());
    OutputDebugString(L"\n");
    */

    for ( auto& record : records )
    {
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
        } else *record.pPid = 0;
      }

      // Verify the claimed PID is still running...
      CHandle hProcess (
          *record.pPid != 0 ?
           OpenProcess ( PROCESS_QUERY_INFORMATION, FALSE,
                           *record.pPid )
                            : INVALID_HANDLE_VALUE
                       );

      // Nope, delete the PID file.
      if ((intptr_t)hProcess.m_h <= 0)
      {
        DeleteFileW (record.wszPidFilename);
                    *record.pPid = 0;
      }
      /*
      else {
        OutputDebugString(L"File: ");
        OutputDebugString(record.wszPidFilename);
        OutputDebugString(L" - PID: ");
        OutputDebugString(std::to_wstring(*record.pPid).c_str());
        OutputDebugString(L"\n");
      }
      */
    }

    bCurrentState =
      ( pid32 || pid64 );

    /*
    OutputDebugString(L"bCurrentState (new): ");
    OutputDebugString(std::to_wstring(bCurrentState).c_str());
    OutputDebugString(L"\n");
    */

    if (bCurrentState != prevState)
    {
      _SetTaskbarOverlay (bCurrentState);

      if (bPendingState)
        bPendingState = false;
    }
  }
};

extern bool SKIF_ImGui_BeginChildFrame(ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);
extern std::wstring SKIF_GetSpecialKDLLVersion(const wchar_t*);
extern bool SKIF_bDisableExitConfirmation;

void SKIF_InjectionContext::_RefreshSKDLLVersions (void)
{
  wchar_t                       wszPathToSelf64 [MAX_PATH + 2] = { };
  wchar_t                       wszPathToSelf32 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSelf64, MAX_PATH);
  GetModuleFileNameW  (nullptr, wszPathToSelf32, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSelf64);
  PathRemoveFileSpecW (         wszPathToSelf32);
  PathAppendW         (         wszPathToSelf64, L"SpecialK64.dll");
  PathAppendW         (         wszPathToSelf32, L"SpecialK32.dll");
  SKVer32 =
    SK_WideCharToUTF8 (SKIF_GetSpecialKDLLVersion (wszPathToSelf32));
  SKVer64 =
    SK_WideCharToUTF8 (SKIF_GetSpecialKDLLVersion (wszPathToSelf64));
}

void
SKIF_InjectionContext::_GlobalInjectionCtl (void)
{
  extern float SKIF_ImGui_GlobalDPIScale;

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
  ImGui::TextUnformatted ( (SKVer32 == SKVer64) ?
            ( "Special K v " + SKVer32 ).c_str () :
              "Special K" );
  ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  ImGui::TextUnformatted ("Config Root:");
  ImGui::TextUnformatted ("32-bit Service:");
  ImGui::TextUnformatted ("64-bit Service:");
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
  SKIF_ImGui_SetHoverTip        (
    "Open the config root folder"
  );

  // 32-bit/64-bit Services
  if (pid32)
    ImGui::TextColored (ImColor::HSV (0.3F,  0.99F, 1.F), "Running");
  else
    ImGui::TextColored (ImColor::HSV (0.08F, 0.99F, 1.F), "Not Running");

  if (pid64)
    ImGui::TextColored (ImColor::HSV (0.3F,  0.99F, 1.F), "Running");
  else
    ImGui::TextColored (ImColor::HSV (0.08F, 0.99F, 1.F), "Not Running");

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

  if (SKVer32 != SKVer64)
  {
    ImGui::Text ("( v %s )", SKVer32.c_str ());
    ImGui::Text ("( v %s )", SKVer64.c_str ());
  }
  else
    ImGui::NewLine ();

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
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                           ImGui::GetStyle ().Alpha *
                             ( (SKIF_IsHDR ()) ? 0.1f
                                               : 0.5f
                             )
    );
  }
    
  if (bCurrentState == bExpectedState)
  {
    const char *szStartStopLabel =
      bCurrentState ?  "Stop Service###GlobalStartStop"  :
                       "Start Service###GlobalStartStop";

    if (ImGui::Button (szStartStopLabel, ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                                   50.0f * SKIF_ImGui_GlobalDPIScale )))
    {
      _StartStopInject (bCurrentState);
    }
  }

  else
    ImGui::ButtonEx (bCurrentState ? "Stopping...###GlobalStartStop" :
                                     "Starting...###GlobalStartStop",
                      ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                50.0f * SKIF_ImGui_GlobalDPIScale ),
                        ImGuiButtonFlags_Disabled );

  if ( ! bCurrentState && SKIF_bDisableExitConfirmation)
      SKIF_ImGui_SetHoverTip ("Service continues running after SKIF is closed");

  if ( ! bHasServlet )
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
  }

  ImGui::EndChildFrame ();



  // Tips 'n Tricks
  auto frame_id3 =
    ImGui::GetID ("###Global_Injection_TipsNTricks");

  SKIF_ImGui_BeginChildFrame ( frame_id3,
                                 ImVec2 ( 0.0f,
                                          2.0f * ImGui::GetTextLineHeightWithSpacing () ),
                                   ImGuiWindowFlags_NavFlattened      |
                                   ImGuiWindowFlags_NoScrollbar       |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoBackground
  );

  if ( bHasServlet )
  {
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
  }

  else {
    ImGui::PushStyleColor (ImGuiCol_Text, ImColor::HSV (0.11F, 1.F, 1.F).Value);
    ImGui::TextWrapped    (
      "Global injection service is unavailable as one or more of the required files are missing."
    );
    ImGui::PopStyleColor  ();
  }

  ImGui::EndChildFrame ();
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

  CoInitializeEx(nullptr, 0x0);

  // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
  // has already been called.
  hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);

  if (SUCCEEDED(hres))
  {
    IPersistFile* ppf;

    // Set the path to the shortcut target and add the description. 
    psl->SetPath(lpszPathObj);
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
// lpszPath     - Address of a buffer that receives the path of the link target, including the file name.
// lpszDesc     - Address of a buffer that receives the description of the 
//                Shell link, stored in the Comment field of the link
//                properties.

HRESULT
ResolveIt(HWND hwnd, LPCSTR lpszLinkFile, LPWSTR lpszArguments, int iPathBufferSize)
{
  HRESULT hres;
  IShellLink* psl;

  WCHAR szGotPath[MAX_PATH];
  WCHAR szArguments[MAX_PATH];
  WIN32_FIND_DATA wfd;

  *lpszArguments = 0; // Assume failure

  CoInitializeEx(nullptr, 0x0);

  // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
  // has already been called. 

  hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);

  if (SUCCEEDED(hres))
  {
    IPersistFile* ppf;

    // Get a pointer to the IPersistFile interface. 
    hres = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);

    if (SUCCEEDED(hres))
    {
      WCHAR wsz[MAX_PATH];

      // Ensure that the string is Unicode. 
      MultiByteToWideChar(CP_ACP, 0, lpszLinkFile, -1, wsz, MAX_PATH);

      // Add code here to check return value from MultiByteWideChar 
      // for success.

      // Load the shortcut. 
      hres = ppf->Load(wsz, STGM_READ);

      if (SUCCEEDED(hres))
      {
        // Resolve the link. 
        hres = psl->Resolve(hwnd, 0);

        if (SUCCEEDED(hres))
        {
          // Get the path to the link target. 
          hres = psl->GetPath(szGotPath, MAX_PATH, (WIN32_FIND_DATA*)&wfd, SLGP_SHORTPATH);

          if (SUCCEEDED(hres))
          {
            // Get the arguments of the target. 
            hres = psl->GetArguments(szArguments, MAX_PATH);

            if (SUCCEEDED(hres))
            {
              hres = StringCbCopy(lpszArguments, iPathBufferSize, szArguments);
              if (SUCCEEDED(hres))
              {
                // Handle success
              }
              else
              {
                // Handle the error
              }
            }
          }
        }
      }

      // Release the pointer to the IPersistFile interface. 
      ppf->Release();
    }

    // Release the pointer to the IShellLink interface. 
    psl->Release();
  }
  return hres;
}

void
SKIF_InjectionContext::_StartAtLogonCtrl (void)
{
  ImGui::BeginGroup ();

  extern bool SKIF_bEnableDebugMode;
  
  /*
  ImGui::Spacing     ();
  
  ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.68F, 0.68F, 0.68F, 1.0f));
  ImGui::TextWrapped    (
    "SKIF and the global injection service can be configured to start automatically with Windows."
  );
  ImGui::PopStyleColor  ();
  
  ImGui::Spacing ();
  ImGui::Spacing ();
  */

  // New method

  if (bLogonTaskEnabled)
  {
    // Disable button
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                            ImGui::GetStyle ().Alpha *
                              ( (SKIF_IsHDR ()) ? 0.1f
                                                : 0.5f
                              )
    );
  }
  
  ImGui::BeginGroup  ();

  path_cache_s::win_path_s user_startup  =
  {            FOLDERID_Startup,
    L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\StartUp"
  };

  SKIF_GetFolderPath(&user_startup);

  static std::wstring target = SK_FormatStringW ( LR"(%ws\SKIF.exe)",
                               path_cache.specialk_userdata.path );

  static std::string link    = SK_FormatString ( R"(%ws\SKIF.lnk)",
                               user_startup.path );

  SK_RunOnce(
    bAutoStartSKIF = PathFileExists(SK_UTF8ToWideChar(link).c_str())
  );

  static bool argsChecked = false;
  static std::wstring args = L"\0";

  if ( ! argsChecked && bAutoStartSKIF )
  {
    extern HWND SKIF_hWnd;
    WCHAR szArguments[MAX_PATH];
    SK_RunOnce(ResolveIt(SKIF_hWnd, link.c_str(), szArguments, MAX_PATH));
    args = szArguments;
    
    bAutoStartService = (args.find(L"START")    != std::wstring::npos);
    bStartMinimized   = (args.find(L"MINIMIZE") != std::wstring::npos);

    argsChecked = true;
  }

  static bool changes = false;

  if (ImGui::Checkbox("Start Special K Injection Frontend (SKIF) with Windows", &bAutoStartSKIF))
    changes = true;

  if (bAutoStartSKIF)
  {
    ImGui::TreePush ("");
    if (ImGui::Checkbox(" " ICON_FA_PLAY " Autostart global injection service", &bAutoStartService))
      changes = true;
    
    // Disable button
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                            ImGui::GetStyle ().Alpha *
                              ( (SKIF_IsHDR ()) ? 0.1f
                                                : 0.5f
                              )
    );
    if (ImGui::Checkbox(" " ICON_FA_WINDOW_MINIMIZE " Start minimized", &bStartMinimized))
      changes = true;
    
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
    SKIF_ImGui_SetHoverTip ("This option is currently disabled due to not working properly.");


    ImGui::TreePop  ( );
  }

  if (changes)
  {
    DeleteFileW(SK_UTF8ToWideChar(link).c_str());

    if (bStartMinimized)
      args = (bAutoStartService) ? L"START MINIMIZE" : L"STOP MINIMIZE";
    else
      args = (bAutoStartService) ? L"START"          : L"";

    if (bAutoStartSKIF)
     CreateLink(target.c_str(), link.c_str(), args.c_str(), L"Special K Injection Frontend");
    else
      bAutoStartService = bStartMinimized = false;

    changes = false;
  }

  ImGui::EndGroup    ();
  
  if (bLogonTaskEnabled)
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();

    SKIF_ImGui_SetHoverTip (   "The legacy autostart method needs to be disabled to migrate over to the new method."
                             "\nThe difference is that the new method autostarts SKIF, and not just the GI service." );
  }


  // Legacy method, only appear if it is actually enabled or debug mode is enabled

  if ( bLogonTaskEnabled ||
       SKIF_bEnableDebugMode )
  {
    _StartAtLogonCtrlLegacy ( );
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
  
  static bool requiredFiles =
    PathFileExistsW (LR"(Servlet\enable_logon.bat)" ) &&
    PathFileExistsW (LR"(Servlet\disable_logon.bat)") && 
    PathFileExistsW (LR"(Servlet\task_inject.bat)"  );
  //PathFileExistsW (LR"(Servlet\task_eject.bat)"); // Not actually required for StartAtLogon feature

  if (! requiredFiles || 
        bAutoStartSKIF )
  {
    // Disable button
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha,
                            ImGui::GetStyle ().Alpha *
                              ( (SKIF_IsHDR ()) ? 0.1f
                                                : 0.5f
                              )
    );
  }
  
  if (ImGui::Checkbox ("Start Global Injection Service At Logon (legacy) " ICON_FA_SHIELD_ALT, &bLogonTaskEnabled))
  {
    const wchar_t* wszLogonTaskCmd =
      (bLogonTaskEnabled ?
        LR"(Servlet\enable_logon.bat)" :
        LR"(Servlet\disable_logon.bat)");
  
    if (
      ShellExecuteW (
        nullptr, L"runas",
          wszLogonTaskCmd,
            nullptr, nullptr,
              SW_HIDE ) < (HINSTANCE)32 )
    {
      bLogonTaskEnabled =
        ! bLogonTaskEnabled;
    }
  }
  
  SKIF_ImGui_SetHoverTip (
      "Administrative privileges are required on the system to toggle this."
    "\nNote that this injection frontend (SKIF) will not start with Windows."
  );
  
  if (! requiredFiles ||
        bAutoStartSKIF )
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
  
    ImGui::SameLine    ();

    if (! requiredFiles)
      ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F),
        "Option is unavailable as one or more of the required files are missing.");
    else
      SKIF_ImGui_SetHoverTip (   "The regular autostart method needs to be disabled to migrate over to this legacy method."
                               "\nThe difference is that this legacy method just starts the GI service, and not SKIF." );
  }

  ImGui::EndGroup      ();
}

void
InitializeJumpList (void);

HRESULT
SKIF_InjectionContext::_SetTaskbarOverlay (bool show)
{
  CComPtr <ITaskbarList3> taskbar;
  if ( SUCCEEDED (
         CoCreateInstance ( CLSID_TaskbarList, 0, CLSCTX_INPROC_SERVER,
                              IID_ITaskbarList3, (void **)&taskbar.p)
     ) )
  {
    extern HWND SKIF_hWnd;

    //HICON hIcon = LoadIcon(hModSelf, MAKEINTRESOURCE(IDI_SKIF));
    HICON hIcon = LoadIcon(hModSelf, MAKEINTRESOURCE(IDI_SKIFO));

    /*
    SHSTOCKICONINFO
      sii        = {          };
      sii.cbSize = sizeof (sii);

    if ( SUCCEEDED (
           SHGetStockIconInfo ( SIID_INFO,
                                  SHGSI_ICON | SHGSI_LARGEICON,
                                    &sii )
       ) )
    */

    if (hIcon != NULL)
    {
      if (show)
        taskbar->SetOverlayIcon (SKIF_hWnd, hIcon, L"Global injection service is running.");
      else
        taskbar->SetOverlayIcon (SKIF_hWnd, NULL, NULL);

      //DestroyIcon (sii.hIcon);
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
         std::filesystem::current_path().wstring();

  // Create the Documents/My Mods/SpecialK/Global/ folder, and any intermediate ones, if it does not already exist
  std::filesystem::create_directories ((root_dir + LR"(\Global\)").c_str ());

  // std::ofstream list_file // UTF-8
  std::wofstream list_file( // ANSI
    (whitelist_) ? (root_dir + LR"(\Global\whitelist.ini)").c_str()
                 : (root_dir + LR"(\Global\blacklist.ini)").c_str()
  );

  if (list_file.is_open())
  {
    /* ANSI */
    std::wstring out_text =
      SK_UTF8ToWideChar((whitelist_) ? whitelist : blacklist);

    // Strip all null terminator \0 characters from the string
    out_text.erase(std::find(out_text.begin(), out_text.end(), '\0'), out_text.end());

    list_file.write(out_text.c_str(),
      out_text.length());

    if (list_file.good())
      ret = true;

    /* UTF-8
    list_file.write ( szOut,
              strlen( szOut) );
    */
    list_file.close();
  }

  return ret;
}

void SKIF_InjectionContext::_LoadList(bool whitelist_)
{
  static std::wstring root_dir =
         std::filesystem::current_path().wstring();

  // std::ifstream list_file // UTF-8
  std::wifstream list_file( // ANSI
    (whitelist_) ? (root_dir + LR"(\Global\whitelist.ini)").c_str()
                 : (root_dir + LR"(\Global\blacklist.ini)").c_str()
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
    strcpy ( (whitelist_) ? whitelist : blacklist,
                SK_WideCharToUTF8 (full_text).c_str ()
    );

    /* UTF-8
    strcpy ( szIn,
                full_text.c_str ()
    );
    */
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
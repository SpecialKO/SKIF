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

#pragma once

#include <SKIF.h>
#include <injection.h>
#include <fsutil.h>

#include <sk_utility/utility.h>
#include <imgui/imgui.h>

#include <wtypes.h>
#include <process.h>
#include <Shlwapi.h>
#include <cstdio>
#include <array>
#include <vector>
#include "windows.h"
#include <iostream>
#include <comdef.h>
#include <filesystem>

float sk_global_ctl_x;
bool  SKIF_ServiceRunning;

SKIF_InjectionContext _inject;

bool
SKIF_InjectionContext::pid_directory_watch_s::isSignaled (void)
{
  if (wszDirectory [0] == L'\0')
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

    return true;
  }

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

SKIF_InjectionContext::pid_directory_watch_s::~pid_directory_watch_s (void)
{
  if (      hChangeNotification != INVALID_HANDLE_VALUE)
    FindCloseChangeNotification (hChangeNotification);
}

bool SKIF_InjectionContext::_StartStopInject (bool running_)
{
  bool         _inout = running_;

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

    ///const wchar_t *wszStartStopCommand =
    ///  running ? LR"(Servlet\stop.bat)" :
    ///            LR"(Servlet\start.bat)";

    const wchar_t *wszStartStopCommand =
                LR"(rundll32.exe)";

    const wchar_t *wszStartStopParams32 =
      running ? L"../SpecialK32.dll,RunDLL_InjectionManager Remove"
              : L"../SpecialK32.dll,RunDLL_InjectionManager Install";

    const wchar_t *wszStartStopParams64 =
      running ? L"../SpecialK64.dll,RunDLL_InjectionManager Remove"
              : L"../SpecialK64.dll,RunDLL_InjectionManager Install";

    HWND hWndForeground =
          GetForegroundWindow ();

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

    if ( ShellExecuteExW (&sexi) || running )
    {  // If we are currently running, try to shutdown 64-bit even if 32-bit fails.
      sexi.lpFile       = wszStartStopCommand64;
      sexi.lpParameters = wszStartStopParams64;

      if ( ShellExecuteExW (&sexi) )
      {
        *_inout= true;

        Sleep               (800UL);
        SetForegroundWindow (hWndForeground);
        SetFocus            (hWndForeground);
      }

      else *_inout = false;
    }

    else
      *_inout = false;

    _endthreadex (0);

    return 0;
  }, (LPVOID)&_inout, 0x0, &tid);

  if (hThread != 0)
  {
    WaitForSingleObject (hThread, INFINITE);
    CloseHandle         (hThread);
  }

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
  std::filesystem::current_path(std::filesystem::path(wszPath).remove_filename());

  bHasServlet  =
    PathFileExistsW (L"Servlet");

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
    if (TestServletRunlevel (run_lvl_changed))
      _StartStopInject (true);

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

  running =
    TestServletRunlevel (run_lvl_changed);
}

bool SKIF_InjectionContext::TestServletRunlevel (bool& changed_state)
{
  bool ret = running;

  if (dir_watch.isSignaled ())
  {
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
    }

    ret =
      ( pid32 || pid64 );

    changed_state = true;
  }

  return ret;
};

extern std::wstring SKIF_GetSpecialKDLLVersion(const wchar_t*);
extern bool SKIF_bDisableExitConfirmation;

void what(std::wstring call)
{
    //DWORD dwLastError = GetLastError();

    OutputDebugStringW(
        (
            call + std::wstring(L"\n") /* +
            std::wstring(L" (") +
            std::to_wstring(dwLastError) +
            std::wstring(L"): ") +
            _com_error(dwLastError).ErrorMessage() +
            std::wstring(L"\n") */
        ).c_str()
    );
}

bool
SKIF_InjectionContext::_InjectProcess (void)
{
    SK_RunOnce(
        SetLastError(NO_ERROR)
    );

    bool injected = false;

    DWORD PID = NULL;

    HWND window = GetForegroundWindow();
    if (!window)
        what(L"GetForegroundWindow");

    DWORD thread = GetWindowThreadProcessId(window, &PID);
    if (!thread)
        what(L"GetWindowThreadProcessId");

    if (!PID)
        what(L"PID");

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, PID);
    if (!process)
        what(L"OpenProcess");

    BOOL bIsWow64 = false;

    if (0 == IsWow64Process(process, &bIsWow64))
        what(L"IsWow64Process");

    if (bIsWow64)
    {
        // 32-bit application on 64-bit Windows

        // doable? https://stackoverflow.com/questions/8776437/c-injecting-32-bit-targets-from-64-bit-process
        // otherwise helper app needed?
    }
    else {
        // 64-bit application
        // or technically 32-bit application on 32-bit Windows, but then again SKIF doesn't have a 32-bit build

        //const WCHAR dll_path[] = L"F:\\Aemony\\Documents\\GitHub\\SKIF\\x64\\Release\\SpecialK64.dll";
        WCHAR dll_pathTest[MAX_PATH] = { };
        GetModuleFileNameW(0, dll_pathTest, MAX_PATH);
        PathRemoveFileSpecW(dll_pathTest);
        PathAppendW(dll_pathTest, L"SpecialK64.dll");
        const int dll_path_size = (wcslen(dll_pathTest) + 1) * sizeof(WCHAR); // or sizeof(dll_path); or sizeof(WCHAR);

        FARPROC lib = GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW");
        if (!lib)
            what(L"GetProcAddress");

        LPVOID base = VirtualAllocEx(process, 0, dll_path_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!base)
            what(L"VirtualAllocEx");

        BOOL good = WriteProcessMemory(process, base, dll_pathTest, dll_path_size, 0);
        if (!good)
            what(L"WriteProcessMemory");

        HANDLE thread = CreateRemoteThread(process, 0, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(lib), base, 0, 0);
        if (!thread)
            what(L"CreateRemoteThread");

        OutputDebugStringW(
            (
                std::wstring(L"Remote thread successfully created in process ") +
                std::to_wstring(PID) +
                std::wstring(L".\n")).c_str()
        );

        WaitForSingleObject(thread, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeThread(thread, &exitCode);

        if (exitCode != 0)
        {
            injected = true;
            OutputDebugStringW(
                (
                    std::wstring(L"DLL loaded successfully in process ") +
                    std::to_wstring(PID) +
                    std::wstring(L".\n")).c_str()
            );
        }
        else
        {
            OutputDebugStringW(
                (
                    std::wstring(L"DLL load failed in process ") +
                    std::to_wstring(PID) +
                    std::wstring(L".\n")).c_str()
            );
        }

#if 1
        CloseHandle(thread);
        CloseHandle(process);
#endif

        return injected;
    }
}

extern void
SKIF_Util_OpenURI(std::wstring path, DWORD dwAction = SW_SHOWNORMAL);

void SKIF_InjectionContext::_RefreshSKDLLVersions(void)
{
    wchar_t                 wszPathToSelf64[MAX_PATH] = { };
    wchar_t                 wszPathToSelf32[MAX_PATH] = { };
    GetModuleFileNameW(0, wszPathToSelf64, MAX_PATH);
    GetModuleFileNameW(0, wszPathToSelf32, MAX_PATH);
    PathRemoveFileSpecW(wszPathToSelf64);
    PathRemoveFileSpecW(wszPathToSelf32);
    PathAppendW(wszPathToSelf64, L"SpecialK64.dll");
    PathAppendW(wszPathToSelf32, L"SpecialK32.dll");
    SKVer32 = SK_WideCharToUTF8(SKIF_GetSpecialKDLLVersion(wszPathToSelf32));
    SKVer64 = SK_WideCharToUTF8(SKIF_GetSpecialKDLLVersion(wszPathToSelf64));
}

bool
SKIF_InjectionContext::_GlobalInjectionCtl (void)
{
  ImGui::BeginGroup   (                  );
  if (bHasServlet)
  {
    running =
      TestServletRunlevel (run_lvl_changed);

    ImGui::BeginGroup();

    ImGui::Spacing();
    ImGui::Spacing();

    if (SKVer32 == SKVer64)
    {
        std::string SKVer3264Label = "Special K 32/64-bit v " + SKVer32;

        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::Text(SKVer3264Label.c_str());
    }
    else {
        std::string SKVer32Label = "Special K 32-Bit v " + SKVer32;
        std::string SKVer64Label = "Special K 64-Bit v " + SKVer64;

        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::Text(SKVer32Label.c_str());

        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::Text(SKVer64Label.c_str());
    }

    ImGui::EndGroup();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::Spacing    ();
    ImGui::Spacing    ();
    ImGui::SameLine   ();

    if (run_lvl_changed)
    {
      const char *szStartStopLabel =
        running ?  "Stop Service###GlobalStartStop"  :
                  "Start Service###GlobalStartStop";

      if (ImGui::Button (szStartStopLabel))
      {
        _StartStopInject (running);

        run_lvl_changed = false;
      }
    }
    else
      ImGui::Button (running ? "Stopping...###GlobalStartStop" :
                               "Starting...###GlobalStartStop");

    if (SKIF_bDisableExitConfirmation)
        SKIF_ImGui_SetHoverTip ( running                  ?
          ""                                               :
          "Service continues running after SKIF is closed"
        );

    ImGui::EndGroup    ();
    ImGui::SameLine    ();
    ImGui::BeginGroup  ();

    enum {
          RUNNING = 0,
      NOT_RUNNING = 1
    };

    struct status_s {
      ImColor     color;
      const char* text;

      void Display (int bits)
      {
        ImGui::BeginGroup  ();
        ImGui::TextColored (ImColor (0.68F, 0.68F, 0.68F), " %d-Bit Service: ", bits);
        ImGui::SameLine    ();
        ImGui::TextColored (color, text);
        ImGui::EndGroup    ();
      }
    } static status [] =
      {
        { ImColor::HSV (0.3F,  0.99F, 1.F),     "Running" },
        { ImColor::HSV (0.08F, 0.99F, 1.F), "Not Running" }
      };

    status [pid32 != 0 ? RUNNING : NOT_RUNNING].Display (32);
    status [pid64 != 0 ? RUNNING : NOT_RUNNING].Display (64);

    ImGui::SameLine   ();
    ImGui::Spacing    ();
    ImGui::EndGroup   (); /* ImGui::SameLine ();
    ImGui::BeginGroup ();
    ImGui::Spacing    (); ImGui::Spacing  ();

    if (ImGui::Checkbox ("Start At Logon", &bLogonTaskEnabled))
    {
      const wchar_t* wszLogonTaskCmd =
        ( bLogonTaskEnabled ?
            LR"(Servlet\enable_logon.bat)" :
            LR"(Servlet\disable_logon.bat)" );

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

    ImGui::EndGroup    (); */

    ImGui::Spacing                ();
    ImGui::Spacing                ();

    ImGui::BeginGroup             ();
    ImGui::Spacing                (); ImGui::SameLine();
    ImGui::TextColored            (ImColor::HSV (0.55F, 0.99F, 1.F), "? "); ImGui::SameLine (); ImGui::TextColored (ImColor(0.68F, 0.68F, 0.68F), "The service injects Special K into most user processes.");
    ImGui::EndGroup               ();

    SKIF_ImGui_SetHoverTip        ("Any that deal with system input or some sort\nof window or keyboard/mouse input activity.");

    ImGui::BeginGroup             ();
    ImGui::Spacing                (); ImGui::SameLine();
    ImGui::TextColored            (ImColor::HSV (0.55F, 0.99F, 1.F), "?!"); ImGui::SameLine (); ImGui::TextColored (ImColor(0.68F, 0.68F, 0.68F), "Stop the service before playing a multiplayer game.");
    ImGui::EndGroup               ();

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");
    SKIF_ImGui_SetHoverTip        ("In particular games where anti-cheat\nprotection might be present.");

    if ( ImGui::IsItemClicked     ())
        SKIF_Util_OpenURI         (L"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

    ImGui::Spacing();

#if 0

    if (ImGui::Button("Inject SpecialK64.dll in SKIF!", ImVec2(0, 0)))
        _InjectProcess();

#endif // 1

  } else {
    ImGui::Spacing();

    ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F), "Global injection service is unavailable as the required\n\"Servlets\" subfolder and files are missing.");

    ImGui::Spacing();
  }

  ImGui::EndGroup      ();

  sk_global_ctl_x     = ImGui::GetItemRectSize ().x;
  SKIF_ServiceRunning = running;

  return running;
};

void
SKIF_InjectionContext::_StartAtLogonCtrl (void)
{
    ImGui::BeginGroup();

    if (bHasServlet)
    {
        ImGui::Spacing();
        ImGui::Text("Global injection can be configured to start automatically with Windows.");

        ImGui::BeginGroup();
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::TextColored(ImColor::HSV(0.55F, 0.99F, 1.F), "• "); ImGui::SameLine(); ImGui::TextColored(ImColor(0.68F, 0.68F, 0.68F), "This setting affects all users on the system.");
        ImGui::EndGroup();

        ImGui::Spacing();

        if (ImGui::Checkbox("Start At Logon", &bLogonTaskEnabled))
        {
            const wchar_t* wszLogonTaskCmd =
                (bLogonTaskEnabled ?
                    LR"(Servlet\enable_logon.bat)" :
                    LR"(Servlet\disable_logon.bat)");

            if (
                ShellExecuteW(
                    nullptr, L"runas",
                    wszLogonTaskCmd,
                    nullptr, nullptr,
                    SW_HIDE) < (HINSTANCE)32)
            {
                bLogonTaskEnabled =
                    !bLogonTaskEnabled;
            }
        }

        SKIF_ImGui_SetHoverTip("Administrative privileges are required on the system to enable this.");
    }
    else
    {
        ImGui::Spacing();

        ImGui::TextColored(ImColor::HSV(0.11F, 1.F, 1.F), "Settings are unavailable as the required files are missing.");
    }

    ImGui::EndGroup();
}
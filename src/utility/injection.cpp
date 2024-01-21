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

#include <utility/injection.h>

#include "../resource.h"

#include <SKIF.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <utility/registry.h>
#include <utility/fsutil.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

#include <utility/sk_utility.h>
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
#include <strsafe.h>

// External functions
extern bool SKIF_ImGui_BeginChildFrame         (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);
extern std::wstring SKIF_Util_GetSpecialKDLLVersion (const wchar_t*);
extern std::wstring SKIF_Util_GetFileVersion        (const wchar_t*);

// Helper Functions

void CALLBACK
InjectionTimerProc (HWND hWnd, UINT Msg, UINT wParamIDEvent, DWORD dwTime)
{
  UNREFERENCED_PARAMETER (dwTime);

  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  // Translates threaded messages created before the window was created when used as a launcher
  //   into their proper window message counterparts.
  if (hWnd == NULL && SKIF_Notify_hWnd != NULL)
  {
    if (     wParamIDEvent == _inject.IDT_REFRESH_INJECTACK)
      PostMessage (SKIF_Notify_hWnd, Msg, cIDT_REFRESH_INJECTACK, NULL);
    else if (wParamIDEvent == _inject.IDT_REFRESH_PENDING)
      PostMessage (SKIF_Notify_hWnd, Msg, cIDT_REFRESH_PENDING,   NULL);
    else
      PLOG_DEBUG << "Unrecognized timer ID?!";
  }

  // Forward all timer triggers with a corresponding hWnd to their appropriate window
  else
    PostMessage (hWnd, Msg, wParamIDEvent, NULL);
}

// Class Members

SKIF_InjectionContext::SKIF_InjectionContext (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  PLOG_VERBOSE << "SKIF_InjectionContext RAN!";

  bHasServlet =
    PathFileExistsW  (L"Servlet");

  bLogonTaskEnabled =
    PathFileExistsW (LR"(Servlet\SpecialK.LogOn)");

  if (! bLogonTaskEnabled)
    DeleteFile (LR"(Servlet\task_inject.bat)");

  bHasServlet =
    bHasServlet &&
    PathFileExistsW (LR"(Servlet\SKIFsvc32.exe)") &&
    PathFileExistsW (L"SpecialK32.dll");

#ifdef _WIN64
  bHasServlet =
    bHasServlet &&
    PathFileExistsW (LR"(Servlet\SKIFsvc64.exe)") &&
    PathFileExistsW (L"SpecialK64.dll");
#endif

  // Initialize the PID file watches
  records =
    {   SK_FormatStringW (LR"(%ws\Servlet\SpecialK32.pid)", _path_cache.specialk_userdata), nullptr, &pid32
#ifdef _WIN64
      , SK_FormatStringW (LR"(%ws\Servlet\SpecialK64.pid)", _path_cache.specialk_userdata), nullptr, &pid64,
#endif
    };

  PLOG_VERBOSE << "Watching 32-bit PID file: " << records[0].wsPidFilename;
#ifdef _WIN64
  PLOG_VERBOSE << "Watching 64-bit PID file: " << records[1].wsPidFilename;
#endif

  // Perform the dance of the DLL files
  _DanceOfTheDLLFiles ( );

  runState = RunningState::Stopped;

  // Load the whitelist and blacklist
  LoadWhitelist ( );
  LoadBlacklist ( );

  // Fetch SK DLL versions
  _RefreshSKDLLVersions ();
}

bool
SKIF_InjectionContext::_TestServletRunlevel (bool forcedCheck)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static DWORD dwFailed   = NULL;
  static bool  triedToFix = false;

  // Perform a forced check every 500ms if we have been transitioning over for longer than half a second
  if ((runState == Starting || runState == Stopping) && dwLastSignaled + 500 < SKIF_Util_timeGetTime())
    forcedCheck = true;
  
  static std::wstring servletDir = SK_FormatStringW (LR"(%ws\Servlet\)", _path_cache.specialk_install );
  static SKIF_DirectoryWatch servlet_folder;

  if (servlet_folder.isSignaled (servletDir, false) || forcedCheck)
  {
    dwLastSignaled = SKIF_Util_timeGetTime();

    for ( auto& record : records )
    {
      //PLOG_VERBOSE << "record.wsPidFilename                   == " << record.wsPidFilename;
      //PLOG_VERBOSE << "PathFileExistsW (record.wsPidFilename) == " << PathFileExistsW (record.wsPidFilename.c_str());

      // If we currently assume the service is not running, check if it's running
      if (                 *record.pPid == 0 &&
           PathFileExistsW (record.wsPidFilename.c_str()) )
      {
        record.fPidFile =
          _wfopen (record.wsPidFilename.c_str(), L"r");

        if (record.fPidFile != nullptr)
        {
          int count =
            fwscanf (record.fPidFile, L"%li", record.pPid);
            fclose  (record.fPidFile);

          if (count != 1)
               *record.pPid = 0;
        }
      }

      if (*record.pPid != 0)
      {
        // Verify the claimed PID is still running...
        SetLastError (NO_ERROR);
        CHandle hProcess (OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *record.pPid));
        // Use PROCESS_QUERY_LIMITED_INFORMATION since that allows us to retrieve exit code/full process name for elevated processes

        bool accessDenied =
          GetLastError ( ) == ERROR_ACCESS_DENIED;

        // Do not continue if we get access denied, as it means the PID is running outside of our security context
        if (! accessDenied)
        {
          DWORD dwExitCode = 0;

          // Get exit code to filter out zombie processes
          if (hProcess != nullptr)
            GetExitCodeProcess (hProcess, &dwExitCode);

          // If the PID is not active (it is either terminated or a zombie process), delete the file.
          if (dwExitCode != STILL_ACTIVE)
          {
            PLOG_INFO << "Detected terminated or zombie process (PID: " << std::to_wstring(*record.pPid) << "), deleting " << record.wsPidFilename;
            DeleteFileW (record.wsPidFilename.c_str());
            *record.pPid = 0;
          }
        }
      }
    }

    extern void SKIF_Shell_CreateNotifyToast (UINT type, std::wstring message, std::wstring title = L"");

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

        // Do not show the first message if SKIF was hidden as a result of the game launch
        if (! _registry._SuppressServiceNotification)
        {
          if (bAckInj)
            SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_SERVICE, L"Please launch a game to continue.",           L"Special K is ready to be injected into your game!");
          else
            SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_SERVICE, L"The injection service was started.",          L"Special K is now being injected into games!");
        }

        else
          _registry._SuppressServiceNotification = false;
      }
      else
      {
        bCurrentState     = false;
        runState          = Stopped;

        if (bAckInjSignaled)
          SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_SERVICE, L"Press Ctrl + Shift + Backspace while in-game.", L"Special K has been injected into your game!");
        else
          SKIF_Shell_CreateNotifyToast (SKIF_NTOAST_SERVICE, L"The injection service was stopped.",            L"Special K will no longer be injected into games.");

        bAckInj = false;
        bAckInjSignaled = false;
      }

      dwFailed   = NULL;
      triedToFix = false;
      
      _SetTaskbarOverlay (bCurrentState);
      
      if (KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_PENDING))
        IDT_REFRESH_PENDING = 0;

      return true;
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

      _SetTaskbarOverlay (bCurrentState);
    }
#if _WIN64
    else if (runState == Started && ! pid32 && ! pid64)
#else
    else if (runState == Stopped && ! pid32)
#endif
    {
      runState = Stopped;
      bCurrentState = false;

      _SetTaskbarOverlay (bCurrentState);
    }
    // If SKIF seems stuck in a starting transition, attempt to forcefully start the service again after 5000ms
    else if (runState == Starting && ! triedToFix)
    {
      if (dwFailed == NULL)
        dwFailed = SKIF_Util_timeGetTime();

      if (dwFailed + 5000 < SKIF_Util_timeGetTime())
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
        dwFailed = SKIF_Util_timeGetTime();

      if (dwFailed + 5000 < SKIF_Util_timeGetTime())
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

  return false;
};

void
SKIF_InjectionContext::SetStopOnInjectionEx (bool newState, int autoStopBehavior)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  extern CHandle hInjectAck;

  // If autoStopBehaviour is set to 3 (never stop), we should stop the injection acknowledgement
  if (autoStopBehavior == 3)
    newState = false;

  // Set to its current new state
  bAckInj = newState;

  // Close any existing timer
  if (KillTimer ((IDT_REFRESH_INJECTACK == cIDT_REFRESH_INJECTACK) ? SKIF_Notify_hWnd : NULL, IDT_REFRESH_INJECTACK))
    IDT_REFRESH_INJECTACK = 0;

  // Close any existing handles
  hInjectAck.Close();

  // Create a new handle if requested
  // This is used to auto-stop the service
  if (newState && hInjectAck.m_h <= 0)
  {
    hInjectAck.Attach (
      CreateEvent ( nullptr, FALSE, FALSE,  (autoStopBehavior == 0) // 0 == Use global default (_registry.iAutoStopBehavior)
                               ? (_registry.iAutoStopBehavior == 2) ? LR"(Local\SKIF_InjectExitAck)"  // _registry.iAutoStopBehavior == 2
                                                                    : LR"(Local\SKIF_InjectAck)"      // _registry.iAutoStopBehavior == 1
                               : (           autoStopBehavior == 2) ? LR"(Local\SKIF_InjectExitAck)"  //            autoStopBehavior == 2
                                                                    : LR"(Local\SKIF_InjectAck)")     //            autoStopBehavior == 1
    );

#if 1
    // Set up a periodic refresh until the injection gets acknowledged
    // Shouldn't be necessary after 2023-08-27 and the implementation of WM_SKIF_EVENT_SIGNAL
    // Might still be necessary due to annoying focus bugs?
    if (IDT_REFRESH_INJECTACK == 0)
      IDT_REFRESH_INJECTACK =
        SetTimer (SKIF_Notify_hWnd,
                  cIDT_REFRESH_INJECTACK,
                  1000,
                  (TIMERPROC) &InjectionTimerProc
        );
#endif
  }
}

void
SKIF_InjectionContext::ToggleStopOnInjection (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  _registry.regKVDisableStopOnInjection.putData (! _registry.bStopOnInjection);

  if (_inject.bCurrentState)
    _inject.SetStopOnInjectionEx (_registry.bStopOnInjection);
}

bool
SKIF_InjectionContext::isPending(void)
{
  if (runState == Starting || runState == Stopping)
    return true;
  else
    return false;
}

bool
SKIF_InjectionContext::_StartStopInject (bool currentRunningState, bool autoStop, bool elevated, int autoStopBehavior)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  PLOG_INFO << "Attempting to " << ((currentRunningState) ? "STOP" : "START") << " the service...";

  extern HWND SKIF_Notify_hWnd;
  bool        ret = false;

#if 0
  if (! bHasServlet)
  {
    PLOG_ERROR << "Missing critical service components!";
    return ret;
  }
#endif
  
  if (KillTimer ((IDT_REFRESH_PENDING == cIDT_REFRESH_PENDING) ? SKIF_Notify_hWnd : NULL, IDT_REFRESH_PENDING))
    IDT_REFRESH_PENDING = 0;

  SetStopOnInjectionEx ( (! currentRunningState && autoStop), autoStopBehavior);

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

  static std::wstring instDir   = SK_FormatStringW (LR"(%ws\Servlet\)", _path_cache.specialk_install );
  static std::wstring workDir   = SK_FormatStringW (LR"(%ws\Servlet\)", _path_cache.specialk_userdata);
  static std::wstring SKIFsvc32 = instDir + L"SKIFsvc32.exe";

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            workDir, ec))
        std::filesystem::create_directories (workDir, ec);

  SetLastError (NO_ERROR);

  SHELLEXECUTEINFOW
    sexi              = { };
    sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
    sexi.lpVerb       = (elevated) ? L"RUNAS" : L"OPEN";
    sexi.lpFile       = SKIFsvc32.c_str();
    sexi.lpParameters = (currentRunningState) ? L"Stop" : L"Start";
    sexi.lpDirectory  = workDir.c_str(); // LR"(D:\Games\Special K\Servlet)"; // SK_FormatStringW (LR"(%ws\Servlet\)", _path_cache.specialk_userdata).c_str(); // L"Servlet"
    sexi.nShow        = SW_HIDE;
    sexi.fMask        = SEE_MASK_FLAG_NO_UI | /* SEE_MASK_NOCLOSEPROCESS | */
                        SEE_MASK_NOASYNC    | SEE_MASK_NOZONECHECKS;

#ifdef _WIN64
  // Proxy64 cmd line argument is only available on newer service hosts and when the curDir and workDir is the same directory
  static bool Proxy64 = (SKSvc32 >= L"1.0.2.0" && instDir == workDir);

  if (Proxy64 && elevated)
  {
    PLOG_VERBOSE << "SKIFsvc >= 1.0.2.0 and curDir == workDir. Using Proxy64 call.";

    sexi.lpParameters = (currentRunningState) ? L"Stop Proxy64" : L"Start Proxy64";

    ret =
      ShellExecuteExW (&sexi);
  }

  else if ( ShellExecuteExW (&sexi) || currentRunningState )
  {
    if (elevated)
      PLOG_VERBOSE << "SKIFsvc < 1.0.2.0 or curDir != workDir. Using fallback calls.";

    // If we are currently running, try to shutdown 64-bit even if 32-bit fails.
    static std::wstring SKIFsvc64 = instDir + L"SKIFsvc64.exe";
    sexi.lpFile       = SKIFsvc64.c_str();

    ret =
      ShellExecuteExW (&sexi);
  }
#else
  ret =
    ShellExecuteExW (&sexi);
#endif // _WIN64

  if (GetLastError ( ) != NO_ERROR)
    PLOG_DEBUG << SKIF_Util_GetErrorAsWStr ();

  if (currentRunningState)
    runState = RunningState::Stopping;
  else
    runState = RunningState::Starting;

  // This is used to run general actions when SKIF is signaled by SK
  extern CHandle hInjectAckEx;
  extern CHandle hInjectExitAckEx;

  // Create the events when the service is started
  if (! currentRunningState)
  {
    if (hInjectAckEx.m_h == 0)
      SetInjectAckEx (true);
    
    if (hInjectExitAckEx.m_h == 0)
      SetInjectExitAckEx (true);

    // Set up a periodic refresh since we are pending a new state
    if (IDT_REFRESH_PENDING == 0)
      IDT_REFRESH_PENDING =
        SetTimer (SKIF_Notify_hWnd,
                  cIDT_REFRESH_PENDING,
                  500,
                  (TIMERPROC) &InjectionTimerProc
      );
  }

  // Close the events when the service is being stopped
  else {
    hInjectAckEx.Close();

    if (! bAckInjSignaled) // Only if auto-stop event was not signaled
      hInjectExitAckEx.Close();
  }

  dwLastSignaled = SKIF_Util_timeGetTime ();

  Sleep (30);

  if (ret)
    PLOG_INFO  << "The operation was successful.";
  else
    PLOG_ERROR << "The operation was unsuccessful.";

  return ret;
};

void
SKIF_InjectionContext::_DanceOfTheDLLFiles (void)
{
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
      wchar_t        wszNewFile [MAX_PATH + 2] = { };
      combineParts ( wszNewFile, wszFileName,
                      L".new" );

      return
        PathFileExistsW (wszNewFile);
    }

    void cleanOldFiles (void) const {
      HANDLE hFind        = INVALID_HANDLE_VALUE;
      WIN32_FIND_DATA ffd = { };

      hFind =
        FindFirstFileExW (L"*.old", FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);
      //FindFirstFile    (L"*.old", &fd);

      if (INVALID_HANDLE_VALUE != hFind)
      {
        DeleteFile (ffd.cFileName);

        while (FindNextFile(hFind, &ffd))
          DeleteFile (ffd.cFileName);

        FindClose (hFind);
      }
    }

    // In rare cases, SK's DLLs may be stuck in a rogue app and we cannot
    //   release the lock on them. We need to move the files out of the way.
    bool shuffleLockedFiles (void) const {
      wchar_t wszNewFile [MAX_PATH + 2] = { },
              wszOldFile [MAX_PATH + 2] = { },
              wszIOFile  [MAX_PATH + 2] = { };

      combineParts (wszNewFile, wszFileName, L".new");
      combineParts (wszOldFile, wszFileName, L".old");
      combineParts (wszIOFile,  wszFileName, wszRealExt);

      // Attempt to rename existing file to .old
      if (! MoveFileExW ( wszIOFile,  wszOldFile,
                          MOVEFILE_REPLACE_EXISTING |
                          MOVEFILE_WRITE_THROUGH ))
      {
        // If we cannot replace the existing .old, do an additional twirl and add today's date and time to it
        wchar_t    wszSystemTime [64]  = { };
        wchar_t    wszSystemDate [64]  = { };
        wchar_t    wszCombinTime [128] = { };
        
        GetDateFormatEx (LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE,
          NULL, NULL, wszSystemDate, 63, NULL);
        GetTimeFormatEx (LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
          NULL, NULL, wszSystemTime, 63);

        StringCchCatW (wszCombinTime, 127, wszSystemDate);
        StringCchCatW (wszCombinTime, 127, L"_");
        StringCchCatW (wszCombinTime, 127, wszSystemTime);
        
        std::wstring wsAltFile = std::wstring (wszFileName) + L"_" + SKIF_Util_StripInvalidFilenameChars (wszCombinTime) + L".old";

        // Rename it to the new "DLLName_Datestamp_Timestamp.old" name
        MoveFileExW ( wszIOFile, wsAltFile.c_str(),
                      MOVEFILE_REPLACE_EXISTING |
                      MOVEFILE_WRITE_THROUGH );
      }

      // Move new file into place
      MoveFileExW ( wszNewFile, wszIOFile,
                    MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH );

      return true;
    }

    void dance (void) const {
      FILE* fPID =
        _wfopen ( wszPIDFile, L"r" );

      if (fPID != nullptr)
      {
        int pid   = 0;
        int count =
          fwscanf (fPID, L"%li", &pid);
        fclose    (fPID);

        DeleteFileW (wszPIDFile);

        if (count == 1 &&  pid != 0)
          SKIF_Util_TerminateProcess (pid, 0x0);
      }

      shuffleLockedFiles ();
    }
  };

  std::vector <updated_file_s>
               updated_files =
    { { L"SpecialK32",       L".dll", records[0].wsPidFilename.c_str()}
#ifdef _WIN64
     ,{ L"SpecialK64",       L".dll", records[1].wsPidFilename.c_str() }
#endif
    };

  for ( const auto& file : updated_files )
  {
    file.cleanOldFiles ();

    if ( file.isNewer  () )
      file.dance ();
  }
}

void
SKIF_InjectionContext::_RefreshSKDLLVersions (void)
{
  static std::wstring SKVer32_old, SKSvc32_old,
                      SKVer64_old, SKSvc64_old;

  wchar_t                       wszPathToSelf32 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSelf32, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSelf32);
  PathAppendW         (         wszPathToSelf32,       L"SpecialK32.dll");
  SKVer32      =
    SKIF_Util_GetSpecialKDLLVersion (wszPathToSelf32);
  SKVer32_utf8 =
    SK_WideCharToUTF8 (SKVer32);

  wchar_t                       wszPathToSvc32 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSvc32, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSvc32);
  PathAppendW         (         wszPathToSvc32,       LR"(Servlet\SKIFsvc32.exe)");
  SKSvc32      =
    SKIF_Util_GetFileVersion (  wszPathToSvc32);

  SKSvc32_utf8 =
    SK_WideCharToUTF8 (SKSvc32);

  if (SKVer32_old != SKVer32)
    libCacheRefresh = true;

  SKVer32_old = SKVer32;
  SKSvc32_old = SKSvc32;

#ifdef _WIN64
  wchar_t                       wszPathToSelf64 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSelf64, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSelf64);
  PathAppendW         (         wszPathToSelf64,       L"SpecialK64.dll");
  SKVer64      =
    SKIF_Util_GetSpecialKDLLVersion (wszPathToSelf64);
  SKVer64_utf8 =
    SK_WideCharToUTF8 (SKVer64);

  wchar_t                       wszPathToSvc64 [MAX_PATH + 2] = { };
  GetModuleFileNameW  (nullptr, wszPathToSvc64, MAX_PATH);
  PathRemoveFileSpecW (         wszPathToSvc64);
  PathAppendW         (         wszPathToSvc64,       LR"(Servlet\SKIFsvc64.exe)");
  SKSvc64      =
    SKIF_Util_GetFileVersion (  wszPathToSvc64);
  SKSvc64_utf8 =
    SK_WideCharToUTF8 (SKSvc64);

  if (SKVer64_old != SKVer64)
    libCacheRefresh = true;

  SKVer64_old = SKVer64;
  SKSvc64_old = SKSvc64;
#endif
  
  if (libCacheRefresh)
  {
    PLOG_INFO << "Special K DLL files and service hosts have been refreshed..."
              << "\n+------------------+-------------------------------------+"
              << "\n| SpecialK32.dll   | " << SKVer32
              << "\n|  SKIFsvc32.exe   | " << SKSvc32
              << "\n| SpecialK64.dll   | " << SKVer64
              << "\n|  SKIFsvc64.exe   | " << SKSvc64
              << "\n+------------------+-------------------------------------+";
  }
}

void
SKIF_InjectionContext::_GlobalInjectionCtl (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

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
  ImGui::TextUnformatted ("Special K");
  ImGui::PushStyleColor  (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  ImGui::TextUnformatted ("Config folder:");
  ImGui::TextUnformatted ("32-bit Service:");
#ifdef _WIN64
  ImGui::TextUnformatted ("64-bit Service:");
#else
  ImGui::NewLine         ();
#endif
  ImGui::PopStyleColor   ();
  ImGui::ItemSize        (ImVec2 (105.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 1 should have min-width 105px (scaled with the DPI)
  ImGui::EndGroup        ();

  ImGui::SameLine        ();
                         
  // Column 2            
  ImGui::BeginGroup      ();
#ifdef _WIN64
  if (SKVer32 == SKVer64)
    ImGui::TextUnformatted (("v " + SKVer64_utf8).c_str ());
  else
    ImGui::NewLine       ();
#else
    ImGui::TextUnformatted (("v " + SKVer32_utf8).c_str ());
#endif

  // Config Root
  //static std::wstring root_dir =
  //  _path_cache.specialk_userdata.path;

  if (ImGui::Selectable ("Centralized"))
  {
    SKIF_Util_ExplorePath (_path_cache.specialk_userdata);
    //SKIF_Util_OpenURI (root_dir);
  }

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       (
    SK_WideCharToUTF8 (_path_cache.specialk_userdata).c_str ()
  );

  // 32-bit/64-bit Services
  if (pid32 && bAckInj)
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Waiting for game...");
  else if (pid32)
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Running");
  else
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Stopped");

#ifdef _WIN64
  if (pid64 && bAckInj)
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Waiting for game...");
  else if (pid64)
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Running");
  else
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Stopped");
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
    if (! SKVer32.empty())
      ImGui::Text ("( v %s )", SKVer32_utf8.c_str ());
    else
      ImGui::Text ("( Missing )");

    if (! SKVer64.empty())
      ImGui::Text ("( v %s )", SKVer64_utf8.c_str ());
    else
      ImGui::Text ("( Missing )");
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

  /*
  ImGui::PushStyleVar (
    ImGuiStyleVar_FramePadding,
      ImVec2 (((_registry.bUIBorders) ? 104.0f : 105.0f) * SKIF_ImGui_GlobalDPIScale,
                40.0f * SKIF_ImGui_GlobalDPIScale )
  );
  */

  SKIF_ImGui_BeginChildFrame (
    frame_id2,
      ImVec2 (  0.0f,
                0.0f), //110.f *SKIF_ImGui_GlobalDPIScale ),
        ImGuiWindowFlags_NavFlattened      |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground
  );

  //ImGui::PopStyleVar ();

  if ( ! bHasServlet )
    SKIF_ImGui_PushDisableState ( );

  ImVec2 posButton =
     ImGui::GetCursorPos ( );

  // Horizontal center-align
  ImGui::SetCursorPosX (
     ImGui::GetCursorPosX ( ) +
    (ImGui::GetContentRegionAvail ( ).x - (150.0f * SKIF_ImGui_GlobalDPIScale) +
     ImGui::GetStyle ( ).FramePadding.x) / 2
  );

  // Vertical center-align
  ImGui::SetCursorPosY (
     ImGui::GetCursorPosY ( )           +
    (ImGui::GetContentRegionAvail ( ).y -
     ImGui::GetFrameHeightWithSpacing() -
     ImGui::GetStyle().FramePadding.y   -
     (_registry.bUIBorders ? ImGui::GetStyle().WindowBorderSize : 0.0f) -
     (50.0f * SKIF_ImGui_GlobalDPIScale)
    ) / 2
  );
    
  if (runState == Started || runState == Stopped)
  {
    const char *szStartStopLabel =
      bCurrentState ?  ICON_FA_TOGGLE_ON  "  Service###GlobalStartStop"  :
                       ICON_FA_TOGGLE_OFF "  Service###GlobalStartStop";

    if (ImGui::Button (szStartStopLabel, ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                                   50.0f * SKIF_ImGui_GlobalDPIScale )))
      _StartStopInject (bCurrentState, _registry.bStopOnInjection);
  }

  else
    ImGui::ButtonEx (runState == Stopping ? ICON_FA_TOGGLE_ON  "  Stopping...###GlobalStartStop" :
                                            ICON_FA_TOGGLE_OFF "  Starting...###GlobalStartStop",
                      ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                50.0f * SKIF_ImGui_GlobalDPIScale ),
                        ImGuiButtonFlags_Disabled );

  ImGui::SetCursorPos (posButton);

  if ( ! bCurrentState && _registry.bAllowBackgroundService)
      SKIF_ImGui_SetHoverTip ("Service continues running after this app is closed.");
    
  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ServiceMenu = PopupState_Open;

  if ( ! bHasServlet )
    SKIF_ImGui_PopDisableState  ( );

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
#ifdef _WIN64
    if (SKVer64 >= L"21.08.12" &&
        SKVer32 >= L"21.08.12")
#else
    if (SKVer32 >= L"21.08.12")
#endif
    {

      if (ImGui::Checkbox ("Stop automatically", &_registry.bStopOnInjection))
        ToggleStopOnInjection ( );

      ImGui::SameLine        ( );

      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip ("This controls whether the configured auto-stop behavior (see Settings tab) should be used when the service is manually started.\n"
                              "Note that having this unchecked does not disable the auto-stop behavior if a game is launched without the service already running.");
    }

    else {
      ImGui::TextColored  (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Auto-stop is currently unavailable.");
      SKIF_ImGui_SetHoverTip ("The feature requires Special K v21.08.12 or newer.");
    }
  }

  else {
    ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Service is unavailable due to missing files.");
  }

  ImGui::EndChildFrame ();

  fBottomDist = ImGui::GetItemRectSize().y;
};

void
SKIF_InjectionContext::_StartAtLogonCtrl (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

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

  SK_RunOnce (SKIF_GetFolderPath (&_path_cache.user_startup));

  static std::string link    = SK_FormatString ( R"(%ws\SKIF.lnk)",
                               _path_cache.user_startup.path );

  static std::wstring Svc32Target = SK_FormatStringW(LR"("%ws\Servlet\SKIFsvc32.exe")", _path_cache.specialk_userdata),
                      Svc64Target = SK_FormatStringW(LR"("%ws\Servlet\SKIFsvc64.exe")", _path_cache.specialk_userdata);

  static std::string  Svc32Link = SK_FormatString(R"(%ws\SKIFsvc32.lnk)", _path_cache.user_startup.path),
                      Svc64Link = SK_FormatString(R"(%ws\SKIFsvc64.lnk)", _path_cache.user_startup.path);

  static bool dontCare = bAutoStartServiceOnly = _CheckRegistry() ||
                                                 PathFileExistsW(SK_UTF8ToWideChar(Svc32Link).c_str()) ||
                                                 PathFileExistsW(SK_UTF8ToWideChar(Svc32Link).c_str());

  // New method

  if (bLogonTaskEnabled ||
      bAutoStartServiceOnly)
    SKIF_ImGui_PushDisableState ( );
  
  ImGui::BeginGroup  ();

  if ( ! argsChecked )
  {
    WCHAR szArguments[MAX_PATH + 2] = { };

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
    
    bAutoStartService = (args.find (L"Start")    != std::wstring::npos);
    bStartMinimized   = (args.find (L"Minimize") != std::wstring::npos);
  }

  argsChecked = true;

  static bool changes = false;

  if (ImGui::Checkbox("Start with Windows", &bAutoStartSKIF))
    changes = true;

  if (! bAutoStartSKIF)
    SKIF_ImGui_PushDisableState ( );

  ImGui::TreePush ("StartupBehaviour");

  if (ImGui::Checkbox((_registry.bCloseToTray) ? " " ICON_FA_WINDOW_MINIMIZE " Start minimized in the notification area" :
                                                 " " ICON_FA_WINDOW_MINIMIZE " Start minimized", &bStartMinimized))
    changes = true;

  if (ImGui::Checkbox(" " ICON_FA_PLAY " Start the injection service as well", &bAutoStartService))
    changes = true;

  ImGui::TreePop  ( );

  if (! bAutoStartSKIF)
    SKIF_ImGui_PopDisableState ( );

  if (changes)
  {
    DeleteFileW (SK_UTF8ToWideChar(link).c_str());

    if (bStartMinimized)
      args = (bAutoStartService) ? L"Start Minimize" : L"Minimize";
    else
      args = (bAutoStartService) ? L"Start"          : L"";

    if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
      if (bAutoStartSKIF)
      {
          static TCHAR               szExePath[MAX_PATH + 2];
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
    SKIF_ImGui_PopDisableState  ( );

    SKIF_ImGui_SetHoverTip ("The current autostart method needs to be disabled to migrate over to this method.\n"
                            "The difference is that this method autostarts SKIF, and not just the GI service." );
  }

  // Legacy method, only appear if it is actually enabled or debug mode is enabled
  //_registry.bEnableDebugMode;

  if ( bLogonTaskEnabled     ||
       bAutoStartServiceOnly/* ||
       _registry.bEnableDebugMode*/ )
  {
    // Legacy option that autostarts the injection service on logon.
    // SKIF will not start alongside the background service!
    if (bLogonTaskEnabled)
    {
      ImGui::BeginGroup ();
  
      if (ImGui::Checkbox ("Start Injection Service At Logon (obsolete) " ICON_FA_USER_SHIELD, &bLogonTaskEnabled))
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
    
    // New approach to the legacy method
    ImGui::BeginGroup ();

    if ( bLogonTaskEnabled || 
         bAutoStartSKIF )
      SKIF_ImGui_PushDisableState ( );
  
    if (ImGui::Checkbox ("Start the injection service with Windows", &dontCare))
    {
      if (! bAutoStartServiceOnly)
      {
        if (MessageBox(NULL, L"This will start the injection service hidden in the background with Windows.\n"
                              L"\n"
                              L"Special K Injection Frontend (SKIF) will not autostart.\n"
                              L"\n"
                              L"Are you sure you want to proceed?",
                              L"Confirm autostart",
                              MB_YESNO | MB_ICONWARNING) == IDYES)
        {
          if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Run)", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
          {
            TCHAR               szExePath[MAX_PATH + 2];
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
  
    SKIF_ImGui_SetHoverTip ("Note that this frontend app will not start with Windows.");
  
    if ( bLogonTaskEnabled || 
         bAutoStartSKIF )
    {
      SKIF_ImGui_PopDisableState  ( );

      if (bAutoStartSKIF)
        SKIF_ImGui_SetHoverTip ("The regular autostart method needs to be disabled to migrate over to this method.\n"
                                "The difference is that the current method autostarts SKIF, and not just the injection service.");
      else
        SKIF_ImGui_SetHoverTip ("The old legacy method needs to be disabled to migrate over to this new method.\n"
                                "The difference is that this method does not require elevated privileges." );
    }

    ImGui::EndGroup      ();

  }
}

void
SKIF_InjectionContext::_SetTaskbarOverlay (bool show)
{
  extern void SKIF_Shell_CreateUpdateNotifyMenu (void);
  extern void SKIF_Shell_UpdateNotifyIcon       (void);
  extern UINT SHELL_TASKBAR_BUTTON_CREATED;
  SKIF_Shell_CreateUpdateNotifyMenu             (    );
  SKIF_Shell_UpdateNotifyIcon                   (    );

  // Do not call any ITaskbarList3 methods before
  //   TaskbarButtonCreated msg has been received
  if (SHELL_TASKBAR_BUTTON_CREATED == 0)
    return;

  CComPtr <ITaskbarList3> taskbar;
  if (SKIF_ImGui_hWnd != NULL &&
      SUCCEEDED (
         CoCreateInstance ( CLSID_TaskbarList, 0, CLSCTX_INPROC_SERVER,
                             IID_ITaskbarList3, (void **)&taskbar.p)
     ) )
  {
    extern HMODULE hModSKIF;
    HICON hIcon = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIFON));

    if (hIcon != NULL)
    {
      if (show)
        taskbar->SetOverlayIcon (SKIF_ImGui_hWnd, hIcon, L"Injection service is running.");
      else
        taskbar->SetOverlayIcon (SKIF_ImGui_hWnd, NULL, NULL);

      DestroyIcon (hIcon);
      bTaskbarOverlayIcon = show;

      return;
    }
  }

  bTaskbarOverlayIcon = false;
  return;
}

bool SKIF_InjectionContext::SaveUserList (bool whitelist_)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  bool ret = false;
  static std::wstring root_dir =
         std::wstring (_path_cache.specialk_userdata) + LR"(\Global\)";

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            root_dir, ec))
        std::filesystem::create_directories (root_dir, ec);

  std::wofstream list_file(
    (whitelist_) ? (root_dir + LR"(\whitelist.ini)").c_str()
                 : (root_dir + LR"(\blacklist.ini)").c_str()
  );

  if (list_file.is_open ())
  {
    // Requires Windows 10 1903+ (Build 18362)
    if (SKIF_Util_IsWindows10v1903OrGreater ( ))
    {
      list_file.imbue (
          std::locale (".UTF-8")
      );
    }

    else
    {
      // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(disable : 4996)
      list_file.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
    }

    std::wstring out_text =
      SK_UTF8ToWideChar((whitelist_) ? whitelist : blacklist);

    // Strip all null terminator \0 characters from the string
    out_text.erase(std::find(out_text.begin(), out_text.end(), '\0'), out_text.end());

    try
    {
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
    }
    catch (const std::exception& e)
    {
      UNREFERENCED_PARAMETER(e);
    }

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

bool SKIF_InjectionContext::LoadUserList (bool whitelist_)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  
  bool  ret = false;
  static std::wstring root_dir =
           std::wstring(_path_cache.specialk_userdata) + LR"(\Global\)";

  std::wifstream list_file(
    (whitelist_) ? (root_dir + LR"(whitelist.ini)").c_str()
                 : (root_dir + LR"(blacklist.ini)").c_str()
  );

  std::wstring full_text;

  if (list_file.is_open ())
  {
    // Requires Windows 10 1903+ (Build 18362)
    if (SKIF_Util_IsWindows10v1903OrGreater ( ))
    {
      list_file.imbue (
          std::locale (".UTF-8")
      );
    }

    else
    {
      // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(disable : 4996)
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

    ret = true;
  }

  return ret;
}

bool SKIF_InjectionContext::SaveWhitelist (void)
{
  return SaveUserList (true);
}

bool SKIF_InjectionContext::SaveBlacklist (void)
{
  return SaveUserList (false);
}

bool SKIF_InjectionContext::LoadWhitelist (void)
{
  return LoadUserList (true);
}

bool SKIF_InjectionContext::LoadBlacklist (void)
{
  return LoadUserList (false);
}

void SKIF_InjectionContext::SetInjectAckEx (bool newState)
{
  extern CHandle hInjectAckEx;

  hInjectAckEx.Close();

  if (newState)
    hInjectAckEx.Attach (
      CreateEvent ( nullptr, FALSE, FALSE, LR"(Local\SKIF_InjectAckEx)")
    );
}

void SKIF_InjectionContext::SetInjectExitAckEx (bool newState)
{
  extern CHandle hInjectExitAckEx;

  hInjectExitAckEx.Close();

  if (newState)
  {
    hInjectExitAckEx.Attach (
      CreateEvent ( nullptr, FALSE, FALSE, LR"(Local\SKIF_InjectExitAckEx)")
    );
  }
}

bool SKIF_InjectionContext::_TestUserList (const char* szExecutable, bool whitelist_)
{
  // Simplified variant of the internal whitelisting that SK performs
  if (whitelist_)
  {
    if (StrStrIA (szExecutable, R"(SteamApps)")        != NULL ||
        StrStrIA (szExecutable, R"(Epic Games\)")      != NULL ||
        StrStrIA (szExecutable, R"(GOG Galaxy\Games)") != NULL ||
        StrStrIA (szExecutable, R"(Origin Games\)")    != NULL  )
      return true;
  }

  // Simplified variant of the internal blacklisting that SK performs
  else {
    if (StrStrIA(szExecutable, "GameBar")              != NULL/* ||
        StrStrIA(szExecutable, "Launcher")             != NULL */ )
      return true;
  }

  if (  whitelist_ && *whitelist == '\0' ||
      ! whitelist_ && *blacklist == '\0')
    return false;

  std::istringstream iss(  (whitelist_)
                          ? whitelist
                          : blacklist);

  for (std::string line; std::getline(iss, line); )
  {
    try {
      std::regex regexp (line, std::regex_constants::icase);

      if (std::regex_search(szExecutable, regexp))
        return true;
    }
    catch (const std::exception& e)
    {
      UNREFERENCED_PARAMETER(e);
    }
  }

  return false;
}

bool SKIF_InjectionContext::AddUserListPattern (std::string pattern, bool whitelist_)
{
  size_t cx, size;

  if (whitelist_)
  {
    size = sizeof whitelist;

    if (*whitelist == '\0')
      cx = snprintf (whitelist, size, "%s%s", whitelist, pattern.c_str());
    else
      cx = snprintf (whitelist, size, "%s%s", whitelist, ("\n" + pattern).c_str());
  }

  else
  {
    size = sizeof blacklist;

    if (*blacklist == '\0')
      cx = snprintf (blacklist, size, "%s%s", blacklist, pattern.c_str());
    else
      cx = snprintf (blacklist, size, "%s%s", blacklist, ("\n" + pattern).c_str());
  }

  return (size > cx && cx >= 0);
}

bool SKIF_InjectionContext::AddUserListPath (std::string fullPath, bool whitelist_)
{
  // Check if the path is already included
  bool ret = _TestUserList (fullPath.c_str(), whitelist_);

  if (! ret)
  {
    // name of parent folder
    std::filesystem::path exePath = std::filesystem::path(fullPath);
    std::string pattern;

    // Does a parent folder exist?
    if (exePath.has_parent_path() && exePath.parent_path().has_filename())
    {
      // Does another parent folder one level up exist? If so, add it to the pattern
      if (exePath.parent_path().has_parent_path() && exePath.parent_path().parent_path().has_filename())
        pattern = exePath.parent_path().parent_path().filename().string() + R"(\)";

      // Add the name of the parent folder to the pattern
      //pattern += SK_WideCharToUTF8 (exePath.parent_path().filename().wstring());
      pattern += exePath.parent_path().filename().string();

      // If this is an Unreal Engine 4 game, add the executable as well
      if ( pattern == R"(Binaries\Win64)" || // Unreal Engine 3-4
           pattern == R"(Binaries\Win32)" || // Unreal Engine 3-4
           pattern == R"(bin\x64)" ||        // CD Project Red
           pattern == R"(bin\x86)" )         // CD Project Red
        pattern += R"(\)" + exePath.filename().string();
    }

    else
    {
      // Add the executable to the pattern if all else fails
      pattern = std::filesystem::path(fullPath).filename().string();
    }

    // Escape regular expression characters (uses a variant of nlohmann::json's replace_substring call)
    if (! pattern.empty ())
    {
      // Regex meta characters
      // \ must be run first to prevent generating additional slashes
      // . is omitted because it seldom matters
      // * ? < > | is invalid filename characters anyway... might want to omit them too?
      char chars[] = R"(\^[]${}*()+|?<>)"; // .
      for (unsigned int i = 0; i < strlen (chars); ++i)
      {
        for (auto pos  = pattern.find (chars[i]);          // Find the first occurence
                  pos != std::string::npos;                // Validate we're still in the string
                  pattern.insert      (pos, R"(\)"),       // Escape the character
                  pos  = pattern.find (chars[i], pos + 2)) // Find the next occurence
        { }
      }
    }

    // Add to user list
    ret = AddUserListPattern (pattern, whitelist_) && SaveUserList (whitelist_);
  }

  return ret;
}

bool SKIF_InjectionContext::WhitelistPath (std::string fullPath)
{
  return (fullPath.find("InvalidPath") == std::string::npos)
    ? AddUserListPath (fullPath, true)
    : false;
}

bool SKIF_InjectionContext::BlacklistPath (std::string fullPath)
{
  return (fullPath.find("InvalidPath") == std::string::npos)
    ? AddUserListPath (fullPath, false)
    : false;
}

bool SKIF_InjectionContext::WhitelistPattern (std::string pattern)
{
  return (pattern.find("InvalidPath") == std::string::npos)
    ? AddUserListPattern (pattern, true)
    : false;
}

bool SKIF_InjectionContext::BlacklistPattern (std::string pattern)
{
  return (pattern.find("InvalidPath") == std::string::npos)
    ? AddUserListPattern (pattern, false)
    : false;
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
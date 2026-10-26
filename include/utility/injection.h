//
// Copyright 2020 - 2026  Andon "Kaldaien" Coleman and Aemony
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

#include <wtypes.h>
#include <cstdio>
#include <array>
#include <string>
#include <vector>
#include <regex>
#include <mutex>
#include <imgui/imgui.h>

// Singleton struct
struct SKIF_InjectionContext {

  UINT_PTR IDT_REFRESH_INJECTACK = 0; // Holds current timer ID
  UINT_PTR IDT_REFRESH_PENDING   = 0; // Holds current timer ID

  // Committed lists, used by _TestUserList. Only modified by Save/Load/AddUserList* (UI thread) while holding _userListsMutex.
  //   The settings tab edits its own copies and commits them through SaveWhitelist/SaveBlacklist.
  char    whitelist[MAX_PATH * 16 * 2] = { };
  char    blacklist[MAX_PATH * 16 * 2] = { };

  // Incremented whenever a new whitelist/blacklist is committed. Lets the compiled patterns
  //   and the settings tab's copies know when they need to be refreshed.
  int     whitelistRevision = 0;
  int     blacklistRevision = 0;

  DWORD   dwLastSignaled    = 0;

  bool    bHasServlet       = false;
  bool    bHasUpdatedFiles  = false;
  bool    bLogonTaskEnabled = false; // Obsolete

  bool    bAutoStartSKIF    = false;
  bool    bAutoStartService = false;
  bool    bStartMinimized   = false;

  bool    bAutoStartServiceOnly = false;

  bool    bCurrentState       = false; // true = running; false = not running
  bool    bAckInj             = false; // Indicates if we're using auto-stop mode or not
  bool    bAckInjSignaled     = false; // Indicates that the auto-stop event was signaled
  bool    bTaskbarOverlayIcon = false;

  int     pid32             = 0,
          pid64             = 0;

  // Used for the quick toggle / status summary when a game is selected
  struct {
    ImColor       color;
    ImColor       color_hover;
    std::string   text = "##ui_game_summary";
    std::string   hover_tip;
  } ui_game_summary;

  enum RunningState
  {
    Stopped  = 0,
    Started  = 1,
    Stopping = 2,
    Starting = 3
  } runState;

  struct pid_file_watch_s {
    std::wstring     wsPidFilename;
          FILE*      fPidFile;
          int*       pPid;
  };

#ifdef _WIN64
  std::array <pid_file_watch_s, 2> records;
#else
  std::array <pid_file_watch_s, 1> records;
#endif

  std::wstring SKVer32      = L"";
  std::string  SKVer32_utf8 = "";
  std::wstring SKSvc32      = L"";
  std::string  SKSvc32_utf8 = "";
//#ifdef _WIN64 // these needs to remain in 32-bit despite not being used since we use them with the SK_GetBitness() alias elsewhere
  std::wstring SKVer64      = L"";
  std::string  SKVer64_utf8 =  "";
  std::wstring SKSvc64      = L"";
  std::string  SKSvc64_utf8 =  "";
//#endif
  bool         libCacheRefresh = false; // Signals to the library that it should refresh the injection cache for a game

  bool    isPending               (void);
  bool    _StartStopInject        (bool running_, bool autoStop = false, bool elevated = false, int autoStopBehavior = 0); // autoStopBehaviour: 0 = use global default, 1 = stop on injection,   2 = stop on game exit,  3 = never stop
  bool    _TestServletRunlevel    (bool forcedCheck); // Returns true ONLY if we transitioned over from a pending state
  void    _DanceOfTheDLLFiles     (void);
  void    _RefreshSKDLLVersions   (void);
  void    _GlobalInjectionCtl     (void);
  void    _StartAtLogonCtrl       (void);
  void    _SetTaskbarOverlay      (bool show);
  void    _RefreshUIQuickToggle   (bool active);
  bool    _TestUserList           (const char* wszExecutable, bool whitelist_);

  const char*              GetBuiltInListMatch    (const char* szExecutable, bool whitelist_); // Returns the built-in pattern matching the executable, or nullptr
  std::vector<std::string> GetUserListMatches     (const char* szExecutable, bool whitelist_); // Returns the user patterns (lines) matching the executable
  bool                     RemoveUserListPatterns (const std::vector<std::string>& patterns, bool whitelist_); // Removes the given lines from the list and saves it

  bool    WhitelistPath           (std::string fullPath);
  bool    BlacklistPath           (std::string fullPath);
  bool    WhitelistPattern        (std::string pattern);
  bool    BlacklistPattern        (std::string pattern);
  bool    SaveWhitelist           (const char* szList = nullptr); // Saves and commits szList, or re-saves the current list if nullptr
  bool    SaveBlacklist           (const char* szList = nullptr); // Saves and commits szList, or re-saves the current list if nullptr
  bool    LoadWhitelist           (void);
  bool    LoadBlacklist           (void);
  void    SetInjectAckEx          (bool newState);
  void    SetInjectExitAckEx      (bool newState);
  void    SetStopOnInjectionEx    (bool newState, int autoStopBehavior = 0);  // Main toggle function. autoStopBehaviour: 0 = use global default, 1 = stop on injection,   2 = stop on game exit,  3 = never stop
  void    ToggleStopOnInjection   (void);           // Used primarily to switch the registry value

  static SKIF_InjectionContext& GetInstance (void)
  {
      static SKIF_InjectionContext instance;
      return instance;
  }

  SKIF_InjectionContext (SKIF_InjectionContext const&) = delete; // Delete copy constructor
  SKIF_InjectionContext (SKIF_InjectionContext&&)      = delete; // Delete move constructor

private:
  SKIF_InjectionContext           (void);
  bool    AddUserListPath         (std::string fullPath,      bool whitelist_);
  bool    AddUserListPattern      (std::string pattern,       bool whitelist_);
  bool    SaveUserList            (bool whitelist_, const char* szList = nullptr);
  bool    LoadUserList            (bool whitelist_);

  // Compiled regex cache for _TestUserList, rebuilt only after a new list has been committed
  struct CompiledUserList {
    int                      revision = -1; // Revision of the list the patterns were compiled from
    std::vector<std::regex>  patterns;
    std::vector<std::string> lines;         // Source line of each compiled pattern (same index)
  };

  CompiledUserList _compiledWhitelist;
  CompiledUserList _compiledBlacklist;
  std::mutex       _userListsMutex;     // Guards whitelist/blacklist and the compiled caches; _TestUserList runs on both the UI thread and the process refresh thread

  void    _CompileUserList        (CompiledUserList& list, const char* szList, int revision);
};
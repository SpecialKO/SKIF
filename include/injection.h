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

#include <wtypes.h>
#include <cstdio>
#include <array>
#include <string>

// Move somewhere else please
extern float sk_global_ctl_x;
extern bool  SKIF_ServiceRunning;

struct SKIF_InjectionContext {
  SKIF_InjectionContext (void);

  HMODULE hModSelf          = nullptr;

  bool    bHasServlet       = false;
  bool    bHasUpdatedFiles  = false;
  bool    bLogonTaskEnabled = false;
  bool    bHasServletTasks  = false;

  bool    bOnDemandInject   = false;

  bool    running           = false;
  int     pid32             = 0,
          pid64             = 0;

  bool    run_lvl_changed   = false;

  struct pid_file_watch_s {
    const wchar_t* wszPidFilename;
          FILE*      fPidFile;
          int*       pPid;
  };

  struct pid_directory_watch_s
  {
    ~pid_directory_watch_s (void);

    bool isSignaled (void);

    wchar_t wszDirectory [MAX_PATH] = {                  };
    HANDLE    hChangeNotification   = INVALID_HANDLE_VALUE;
  } dir_watch;

  std::array <pid_file_watch_s, 2> records =
    { LR"(Servlet\SpecialK64.pid)", nullptr, &pid64,
      LR"(Servlet\SpecialK32.pid)", nullptr, &pid32 };

  std::string SKVer32 = "";
  std::string SKVer64 = "";

  bool    _StartStopInject      (bool running_);

  bool     TestServletRunlevel  (bool& changed_state);
  void    _RefreshSKDLLVersions (void);
  bool    _GlobalInjectionCtl   (void);
  void    _StartAtLogonCtrl     (void);
  HRESULT _SetTaskbarOverlay    (bool show);
} extern _inject;
#include <utility/gamepad.h>
#include <utility/utility.h>
#include <utility/registry.h>
#include <SKIF.h>
#include <Dbt.h>
#include <utility>

#include <unordered_map>

bool SKIF_ImGui_IsFocused (void);

bool SKIF_ImGui_ProcessGamepadInput = false;

void
SKIF_ImGui_UpdateGamepadProcessingEligibility (void)
{
  RECT                             rcSKIF = { };
  GetWindowRect (SKIF_ImGui_hWnd, &rcSKIF);

  static HWND  hWndLastForeground = { };
  static DWORD dwActivePid        = { };

  WINDOWINFO winfo        = {                 };
             winfo.cbSize = sizeof (WINDOWINFO);

  bool windowIsActive =
    ( SKIF_ImGui_IsFocused (                ) &&
      GetWindowInfo (SKIF_ImGui_hWnd, &winfo) &&
                                 0 != (winfo.dwWindowStatus & WS_ACTIVECAPTION) );

  if (windowIsActive)
  {
    SKIF_ImGui_ProcessGamepadInput = true;

    //
    // Check for another window covering the game window, if the user cannot
    //   see the game then allowing background gamepad input would be very dangerous...
    //
    bool process_input = true;

    auto GetWindowsAbove =
    [&]( HWND targetHwnd, std::vector <HWND>& windowsAbove ) noexcept
    {
      if (targetHwnd == nullptr)
        return;

      static auto constexpr          _MaxExpectedWindows = 32;
      if (windowsAbove.capacity () < _MaxExpectedWindows)
          windowsAbove.reserve      (_MaxExpectedWindows);

      windowsAbove.clear ();

      HWND hwnd =
        GetTopWindow (nullptr);

      while ( hwnd != nullptr &&
              hwnd != targetHwnd )
      {
        if (     IsWindowVisible (hwnd))
          windowsAbove.push_back (hwnd);

        hwnd = GetNextWindow
          (hwnd, GW_HWNDNEXT);
      }
    };

    auto IsWindowOverlapping =
    [&]( HWND hWndContainer, const RECT& rectContained, RECT& rcIntersect ) noexcept
    {
      RECT                                 rectContainer = {};
      if (! GetWindowRect (hWndContainer, &rectContainer))
        return false;

      SetRectEmpty (&rcIntersect);

      return
        IntersectRect (&rcIntersect, &rectContainer, &rectContained) != FALSE;
    };

    static std::unordered_map <HWND, BOOL> injected_pid_cache;
    static std::vector        <HWND>       windows_above = {};
    GetWindowsAbove (SKIF_ImGui_hWnd,      windows_above    );

    if (process_input)
    {
      bool any_injected = false;

      for (const auto& window : windows_above)
      {
        if (injected_pid_cache.find (window) ==
            injected_pid_cache.end  (      ))
        {
          DWORD                              dwPid = 0x0;
          GetWindowThreadProcessId (window, &dwPid);

          wchar_t     wszInjectionSignature [33] = {};
          _snwprintf (wszInjectionSignature, 32, LR"(Local\SK_InjectedPid_%d)", dwPid);

          SK_AutoHandle hInjectionSignature (
            OpenEventW (EVENT_ALL_ACCESS, FALSE, wszInjectionSignature)
          );

          injected_pid_cache [window] =
            hInjectionSignature.isValid ();
        }

        if (! any_injected)
              any_injected = injected_pid_cache [window];
      }

      if (any_injected)
      {
        HMONITOR hMonSKIF =
          MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);

        MONITORINFO minfo        = {                  };
                    minfo.cbSize = sizeof (MONITORINFO);

        if (GetMonitorInfoW (hMonSKIF, &minfo))
        {
          RECT rcVisibleWindow = {};
          RECT rcIntersection  = {};

          // Use the work area to avoid the region occupied by the taskbar when testing occlusion
          rcVisibleWindow.left   = std::max (minfo.rcWork.left,   rcSKIF.left);
          rcVisibleWindow.right  = std::min (minfo.rcWork.right,  rcSKIF.right);
          rcVisibleWindow.top    = std::max (minfo.rcWork.top,    rcSKIF.top);
          rcVisibleWindow.bottom = std::min (minfo.rcWork.bottom, rcSKIF.bottom);

          for (const auto& window : windows_above)
          {
            if (IsWindowOverlapping (window, rcVisibleWindow, rcIntersection) &&
                 injected_pid_cache [window])
            {
              process_input = false;
              break;
            }
          }
        }
      }
    }

    SKIF_ImGui_ProcessGamepadInput = process_input;
    return;
  }

  SKIF_ImGui_ProcessGamepadInput = false;
}


SKIF_GamePadInputHelper::SKIF_GamePadInputHelper (void)
{
  // Initialize the condition variable that the gamepad input thread will sleep on
  InitializeConditionVariable (&m_GamePadInput);
}

bool
SKIF_GamePadInputHelper::HasGamePad (void)
{
  for (int i = 0; i < XUSER_MAX_COUNT; i++)
    if (m_bGamepads[i].load())
      return true;

  return false;
}

std::vector <bool>
SKIF_GamePadInputHelper::GetGamePads (void)
{
  std::vector<bool> vec;

  for (int i = 0; i < XUSER_MAX_COUNT; i++)
    vec.push_back (m_bGamepads[i].load());

  return vec;
}

void
SKIF_GamePadInputHelper::InvalidateGamePads (void)
{
  m_bWantUpdate.store (true);
}

XINPUT_STATE
SKIF_GamePadInputHelper::GetXInputState (void)
{
  static HWND    hWndLastForeground = 0;
  static DWORD  dwActivePid         = 0x0;

  HWND hWndForeground = GetForegroundWindow ();

  // GetWindowThreadProcessId has surprisingly high overhead, if the foreground window
  //   has not changed, then we can avoid calling it.
  if (std::exchange (hWndLastForeground, hWndForeground) != hWndForeground)
  {
    GetWindowThreadProcessId (hWndForeground, &dwActivePid);
  }

  auto pGamepad = m_xisGamepad;

  if (dwActivePid == GetCurrentProcessId () && pGamepad != nullptr)
  {
    WINDOWINFO winfo        = {                 };
               winfo.cbSize = sizeof (WINDOWINFO);

    // This method for testing if the app is active is simpler than the crap above,
    //   consider replacing all of that junk with simply this.
    if ((! GetWindowInfo (SKIF_ImGui_hWnd, &winfo)) ||
                                           (winfo.dwWindowStatus & WS_ACTIVECAPTION) == 0)
    {
      return {};
    }

    SKIF_ImGui_UpdateGamepadProcessingEligibility ();

    if (SKIF_ImGui_ProcessGamepadInput)
      return *pGamepad;

    return {};
  }

  return {};
}

HMODULE                                            SKIF_GamePadInputHelper::hModXInput                 = nullptr;
SKIF_GamePadInputHelper::XInputGetState_pfn        SKIF_GamePadInputHelper::SKIF_XInputGetState        = nullptr;
SKIF_GamePadInputHelper::XInputGetCapabilities_pfn SKIF_GamePadInputHelper::SKIF_XInputGetCapabilities = nullptr;
bool                                               SKIF_GamePadInputHelper::runOnce                    = true;
bool                                               SKIF_GamePadInputHelper::skipFreeLibrary            = false;

// If we really wanted to limit the exposure of this function,
//   it should've technically been a part of the thread in
//     SpawnChildThread ( ) since that is the only one intended
//       to update the XInput state.
XINPUT_STATE
SKIF_GamePadInputHelper::UpdateXInputState (void)
{
  static constexpr XINPUT_STATE XSTATE_EMPTY  =
    { 0, XINPUT_GAMEPAD { 0 } };
  static constexpr auto         XUSER_INDEXES =
    std::array <DWORD, 4> { 0, 1, 2, 3 };

  // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
  // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
  if (m_bWantUpdate.load ())
  {
    bool hasGamepad = false;

    if (hModXInput == nullptr)
    {
      static constexpr wchar_t* xinput_dlls[] =
      {
        LR"(XInput1_4.dll)",
        LR"(XInput1_3.dll)",
        LR"(XInput9_1_0.dll)"
      };

      for (wchar_t* dll : xinput_dlls)
      {
        hModXInput = LoadLibraryW (dll);

        if (hModXInput == nullptr)
          continue;

        PLOG_VERBOSE << "Loaded the XInput library: " << dll;

        if (runOnce)
        {
          skipFreeLibrary = PathFileExists (dll);
          PLOG_VERBOSE_IF(skipFreeLibrary) << "Custom XInput DLL file detected; will not free the library after use!";
          runOnce = false;
        }

        SKIF_XInputGetCapabilities = (XInputGetCapabilities_pfn)
          GetProcAddress (hModXInput, "XInputGetCapabilities");

        SKIF_XInputGetState = (XInputGetState_pfn)
          GetProcAddress (hModXInput, "XInputGetState");
        break;
      }
    }

    PLOG_ERROR_IF (hModXInput                 == nullptr) << "Failed to load XInput library?!";
    PLOG_ERROR_IF (SKIF_XInputGetCapabilities == nullptr) << "Failed to get SKIF_XInputGetCapabilities address?!";
    PLOG_ERROR_IF (SKIF_XInputGetState        == nullptr) << "Failed to get SKIF_XInputGetState address?!";

    if (SKIF_XInputGetCapabilities != nullptr)
    {
      XINPUT_CAPABILITIES caps;
      for ( auto idx : XUSER_INDEXES )
      {
        bool connected = (SKIF_XInputGetCapabilities (idx, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS);

        m_bGamepads [idx].store (connected);

        if (connected)
        {
          hasGamepad = true;
          PLOG_VERBOSE << "A gamepad is connected at XInput slot " << idx << ".";
        }
      }
    }

    else
    {
      for ( auto idx : XUSER_INDEXES )
        m_bGamepads [idx].store (false);
    }

    // If we have no gamepad, free up any reference to XInput
    if (! hasGamepad)
    {
      PLOG_VERBOSE << "No gamepad connected.";

      if (hModXInput != nullptr && ! skipFreeLibrary)
      {
        SKIF_XInputGetState              = nullptr;
        SKIF_XInputGetCapabilities       = nullptr;

        FreeLibrary (hModXInput);
        hModXInput = nullptr;

        PLOG_VERBOSE << "Released the XInput library!";
      }
    }

    m_bWantUpdate.store (false);

    static HWND    hWndLastForeground = 0;
    static DWORD  dwActivePid         = 0x0;

    HWND                                   hWndForeground =    GetForegroundWindow ();
    if (std::exchange (hWndLastForeground, hWndForeground) != hWndForeground)
      GetWindowThreadProcessId (           hWndForeground, &dwActivePid);

    if (dwActivePid == GetProcessId (GetCurrentProcess ()))
    {
      WINDOWINFO winfo        = {                 };
                 winfo.cbSize = sizeof (WINDOWINFO);

      if (GetWindowInfo (SKIF_ImGui_hWnd, &winfo) &&
                                          (winfo.dwWindowStatus & WS_ACTIVECAPTION) != 0)
      {
        SKIF_ImGui_UpdateGamepadProcessingEligibility ();

        if (SKIF_ImGui_ProcessGamepadInput)
        {
          // Trigger the main thread to refresh its focus, which will also trickle down to us
          SendMessage (m_hWindowHandle, WM_SKIF_REFRESHFOCUS, 0x0, 0x0);
        }
      }
    }
  }

  if (SKIF_XInputGetState == nullptr)
  {
    m_xisGamepad = nullptr;

    return XSTATE_EMPTY;
  }

  struct gamepad_state_s {
    XINPUT_STATE  last_state = {         };
    LARGE_INTEGER last_qpc   = { 0, 0ULL };
  };

  struct {
    LARGE_INTEGER qpc   = { 0, 0ULL };
    XINPUT_STATE  state = {         };
    DWORD         slot  =    INFINITE;
  } newest;

  static std::array<gamepad_state_s, XUSER_MAX_COUNT> history;

  static HWND    hWndLastForeground = 0;
  static DWORD  dwActivePid         = 0x0;

  bool windowIsActive = false;

  WINDOWINFO winfo        = {                 };
             winfo.cbSize = sizeof (WINDOWINFO);

  if (SKIF_ImGui_IsFocused ())
  {
    if (GetWindowInfo (SKIF_ImGui_hWnd, &winfo) &&
                                        (winfo.dwWindowStatus & WS_ACTIVECAPTION) != 0)
    {
      windowIsActive = true;
    }
  }

  // GetWindowThreadProcessId has surprisingly high overhead, if the foreground window
  //   has not changed, then we can avoid calling it.
  HWND                                   hWndForeground =    GetForegroundWindow ();
  if (std::exchange (hWndLastForeground, hWndForeground) != hWndForeground)
    GetWindowThreadProcessId (           hWndForeground, &dwActivePid);

  static DWORD dwLastActivePid    = dwActivePid;
  static DWORD dwTimeOfActivation = 0;

  if (dwLastActivePid != dwActivePid && dwActivePid == GetCurrentProcessId ())
  {
    if (windowIsActive)
    {
      SKIF_ImGui_UpdateGamepadProcessingEligibility ();

      if (SKIF_ImGui_ProcessGamepadInput)
        dwTimeOfActivation = SKIF_Util_timeGetTime ();
    }
  }

  for ( auto idx : XUSER_INDEXES )
  {
    // Load the static object
    gamepad_state_s            local        = history [idx];
    XINPUT_STATE               xinput_state = { };

    if (m_bGamepads [idx].load ())
    {
      DWORD dwResult = SKIF_XInputGetState (idx, &xinput_state);

      if (dwResult == ERROR_DEVICE_NOT_CONNECTED)
        m_bGamepads    [idx].store (false);

      else if (dwResult == ERROR_SUCCESS && SKIF_ImGui_ProcessGamepadInput &&
                                            GetCurrentProcessId () == dwActivePid &&
                                                   dwLastActivePid == dwActivePid &&
                                                   dwTimeOfActivation < SKIF_Util_timeGetTime () - 500UL &&
                                                   windowIsActive)
      {
        // If button state is different, this controller is active...
        if ( xinput_state.dwPacketNumber != local.last_state.dwPacketNumber )
        {
          if (                      xinput_state.Gamepad.wButtons !=
                        local.last_state.Gamepad.wButtons ||
                                    xinput_state.Gamepad.bLeftTrigger !=
                        local.last_state.Gamepad.bLeftTrigger ||
                                    xinput_state.Gamepad.bRightTrigger !=
                        local.last_state.Gamepad.bRightTrigger )
          {
                                      local.last_state = xinput_state;
            QueryPerformanceCounter (&local.last_qpc);
          }

          // Analog input may contain jitter, perform deadzone test.
          else
          {
  #define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
  #define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
  #define XINPUT_GAMEPAD_TRIGGER_THRESHOLD    30

            float LX = xinput_state.Gamepad.sThumbLX,
                  LY = xinput_state.Gamepad.sThumbLY;
            float RX = xinput_state.Gamepad.sThumbRX,
                  RY = xinput_state.Gamepad.sThumbRY;

            float NL = sqrt (LX*LX + LY*LY);
            float NR = sqrt (RX*RX + RY*RY);

            if (NL > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
                NR > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
            {
                                        local.last_state = xinput_state;
              QueryPerformanceCounter (&local.last_qpc);
            }

            // Inside deadzone, record position but do not update the packet count
            else
            {
              local.last_state.Gamepad =
                                   xinput_state.Gamepad;
            }
          }
        }
      }

      else
      {
        local.last_qpc.QuadPart = 0;
      }

      if (local.last_qpc.QuadPart > newest.qpc.QuadPart )
      {
        newest.slot         = idx;
        newest.state        = local.last_state;
        newest.qpc.QuadPart = local.last_qpc.QuadPart;
      }

      // Save to the static object
      history[idx] = local;
    }
  }

  if (newest.slot == INFINITE)
      newest.state = XSTATE_EMPTY;

  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  if (!SKIF_ImGui_ProcessGamepadInput ||
      dwActivePid != GetCurrentProcessId () ||
         !IsWindowVisible (SKIF_ImGui_hWnd) ||
      dwTimeOfActivation > SKIF_Util_timeGetTime () - 500UL ||
      !windowIsActive ||
      !_registry.bControllers)
  {
    // Neutralize input because SKIF is not in the foreground
    newest.state = XSTATE_EMPTY;
  }

  dwLastActivePid = dwActivePid;

  auto pGamepad =
    &m_xisGamepadBuffers [(m_iCurrentGamepad + 1) % 3];

  *pGamepad = newest.state;

  m_xisGamepad = pGamepad;
  m_iCurrentGamepad++;

  return newest.state;
}

bool
SKIF_GamePadInputHelper::RegisterDevNotification (HWND hWnd)
{
  GUID GUID_DEVINTERFACE_HID =
  { 0x4D1E55B2L, 0xF16F, 0x11CF,
          { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

  m_hWindowHandle = hWnd;

  DEV_BROADCAST_DEVICEINTERFACE_W
    NotificationFilter                 = { };
    NotificationFilter.dbcc_size       = sizeof (DEV_BROADCAST_DEVICEINTERFACE_W);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid  = GUID_DEVINTERFACE_HID;

  m_hDeviceNotify =
    RegisterDeviceNotificationW (
      m_hWindowHandle, &NotificationFilter,
      DEVICE_NOTIFY_WINDOW_HANDLE
    );

  return (m_hDeviceNotify != NULL);
}

void
SKIF_GamePadInputHelper::WakeThread (void)
{
  if (! m_bThreadAwake.load())
  {
    m_xisGamepad = nullptr;
    m_iCurrentGamepad++;

    PLOG_VERBOSE << "Waking the gamepad input child thread...";
    m_bThreadAwake.store (true);

    WakeAllConditionVariable (&m_GamePadInput);
  }
}

void
SKIF_GamePadInputHelper::SleepThread (void)
{
  m_bThreadAwake.store (false);

  m_xisGamepad = nullptr;
  m_iCurrentGamepad++;
}

void
SKIF_GamePadInputHelper::SpawnChildThread (void)
{
  PLOG_VERBOSE << "Spawning SKIF_GamePadInputPump thread...";

  // Start the child thread that is responsible for checking for gamepad input
  static HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void*) -> unsigned
  {
    CRITICAL_SECTION            GamepadInputPump = { };
    InitializeCriticalSection (&GamepadInputPump);
    EnterCriticalSection      (&GamepadInputPump);

    SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_ABOVE_NORMAL);

    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_GamePadInputPump");

    static SKIF_GamePadInputHelper& parent = SKIF_GamePadInputHelper::GetInstance ( );
    extern std::atomic<bool> SKIF_Shutdown;

    // Register for device notifications
    parent.RegisterDevNotification (SKIF_Notify_hWnd);

    DWORD packetLast = 0,
          packetNew  = 0;

    do
    {
      static auto constexpr
        _ThrottleIdleInputAfterMs = 3333UL;

      static DWORD dwLastInputTime =
        SKIF_Util_timeGetTime (),
                   dwCurrentTime;

      auto _IsGamepadIdle =
      [&]{ return
           (dwLastInputTime < dwCurrentTime - _ThrottleIdleInputAfterMs);
       };

      dwCurrentTime = SKIF_Util_timeGetTime    ();
      packetNew     = parent.UpdateXInputState ().dwPacketNumber;

      if (packetNew  > 0  &&
          packetNew != packetLast)
      {
        packetLast = packetNew;

        if (! _IsGamepadIdle ())
        {
          SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_ABOVE_NORMAL);
        }

        dwLastInputTime =
          dwCurrentTime;

        SendMessage (parent.m_hWindowHandle, WM_SKIF_GAMEPAD, 0x0, 0x0);
      }

      static bool
        s_wasGamepadIdle = false;
      if (_IsGamepadIdle ())
      {
        if (! std::exchange (s_wasGamepadIdle, true))
          SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_IDLE);

        if (! parent.HasGamePad ())
          SleepEx (750UL, TRUE);
      }
      else
        s_wasGamepadIdle = false;


      using  SK_Input_HasPlayStationDevices_pfn = bool (WINAPI *)(void);
      static SK_Input_HasPlayStationDevices_pfn
             SK_Input_HasPlayStationDevices =
            (SK_Input_HasPlayStationDevices_pfn)
             GetProcAddress (SKIF_GamePadInputHelper::hModXInput, "SK_Input_HasPlayStationDevices");

      // Real XInput is MUCH higher overhead than SK's HID implementation,
      //   so it needs to be throttled.
      if (SK_Input_HasPlayStationDevices == nullptr||
          SK_Input_HasPlayStationDevices () == false)
      {
        static int
            spins         = 0;
        if (spins++ % 10 == 0)
          SleepEx (2, FALSE);
      }
    } while (! SKIF_Shutdown.load());

    LeaveCriticalSection  (&GamepadInputPump);
    DeleteCriticalSection (&GamepadInputPump);

    PLOG_VERBOSE << "Shutting down...";

    return 0;
  }, nullptr, 0x0, nullptr);

  if (hWorkerThread != NULL)
  {
    CloseHandle (hWorkerThread);
    hWorkerThread = NULL;
  }
}

bool
SKIF_GamePadInputHelper::UnregisterDevNotification (void)
{
  return (m_hDeviceNotify != NULL) ? UnregisterDeviceNotification (m_hDeviceNotify) : true;
}

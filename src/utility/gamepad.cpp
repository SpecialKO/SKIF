#include <utility/gamepad.h>
#include <utility/utility.h>
#include <SKIF.h>
#include <Dbt.h>

SKIF_GamePadInputHelper::SKIF_GamePadInputHelper (void)
{
  // Initialize the condition variable that the gamepad input thread will sleep on
  InitializeConditionVariable (&m_GamePadInput);
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
  return m_xisGamepad.load();
}

// If we really wanted to limit the exposure of this function,
//   it should've technically been a part of the thread in
//     SpawnChildThread ( ) since that is the only one intended
//       to update the XInput state.
XINPUT_STATE
SKIF_GamePadInputHelper::UpdateXInputState (void)
{
  using XInputGetState_pfn =
    DWORD (WINAPI *)( DWORD, XINPUT_STATE * );
  using XInputGetCapabilities_pfn =
    DWORD (WINAPI *)( DWORD, DWORD, XINPUT_CAPABILITIES * );

  static constexpr XINPUT_STATE XSTATE_EMPTY  =
    { 0, XINPUT_GAMEPAD { 0 } };
  static constexpr auto         XUSER_INDEXES =
    std::array <DWORD, 4> { 0, 1, 2, 3 };
  
  static HMODULE                         hModXInput                       = nullptr;
  static XInputGetState_pfn              SKIF_XInputGetState              = nullptr;
  static XInputGetCapabilities_pfn       SKIF_XInputGetCapabilities       = nullptr;
  PLOG_VERBOSE << "herp?";

  // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
  // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
  if (m_bWantUpdate.load())
  {
    PLOG_VERBOSE << "derp?";
    bool hasGamepad = false;

    if (hModXInput == nullptr)
    {
      hModXInput = LoadLibraryW (L"XInput1_4.dll");

      if (hModXInput == nullptr)
        hModXInput = LoadLibraryW (L"XInput1_3.dll");

      if (hModXInput == nullptr)
        hModXInput = LoadLibraryW (L"XInput9_1_0.dll");

      if (hModXInput != nullptr)
      {
        PLOG_VERBOSE << "Loaded the XInput library!";

        SKIF_XInputGetState = (XInputGetState_pfn)
          GetProcAddress (hModXInput, "XInputGetState");

        SKIF_XInputGetCapabilities = (XInputGetCapabilities_pfn)
          GetProcAddress (hModXInput, "XInputGetCapabilities");
      }
    }

    if (SKIF_XInputGetCapabilities != nullptr)
    {
      XINPUT_CAPABILITIES caps;
      for ( auto idx : XUSER_INDEXES )
      {
        bool connected = (SKIF_XInputGetCapabilities (idx, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS);

        m_bGamepads [idx].store (connected);

        if (connected)
          hasGamepad = true;
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

      if (hModXInput != nullptr)
      {
        SKIF_XInputGetState              = nullptr;
        SKIF_XInputGetCapabilities       = nullptr;

        FreeLibrary (hModXInput);
        hModXInput = nullptr;

        PLOG_VERBOSE << "Released the XInput library!";
      }
    }

    m_bWantUpdate.store(false);

    // Trigger the main thread to refresh its focus, which will also trickle down to us
    PostMessage (m_hWindowHandle, WM_SKIF_REFRESHFOCUS, 0x0, 0x0);
  }

  if (SKIF_XInputGetState == nullptr)
  {
    m_xisGamepad.store (XSTATE_EMPTY);
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

  for ( auto idx : XUSER_INDEXES )
  {
    // Load the static object
    gamepad_state_s            local        = history [idx];
    XINPUT_STATE               xinput_state = { };

    if (m_bGamepads [idx].load())
    {
      DWORD dwResult = SKIF_XInputGetState (idx, &xinput_state);

      if (dwResult == ERROR_DEVICE_NOT_CONNECTED)
        m_bGamepads    [idx].store (false);

      else if (dwResult == ERROR_SUCCESS)
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

  PLOG_VERBOSE << "Stored new data!";
  m_xisGamepad.store (newest.state);

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
    PLOG_VERBOSE << "Waking the gamepad input child thread...";
    m_bThreadAwake.store (true);
    WakeAllConditionVariable (&m_GamePadInput);
  }
}

void
SKIF_GamePadInputHelper::SleepThread (void)
{
  m_bThreadAwake.store (false);
}

void
SKIF_GamePadInputHelper::SpawnChildThread (void)
{
  PLOG_VERBOSE << "Spawning SKIF_GamePadInputPump thread...";
  
  // Start the child thread that is responsible for checking for gamepad input
  static HANDLE hThread = CreateThread ( nullptr, 0x0,
      [](LPVOID)
    -> DWORD
    {
      CRITICAL_SECTION            GamepadInputPump = { };
      InitializeCriticalSection (&GamepadInputPump);
      EnterCriticalSection      (&GamepadInputPump);
        
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_GamePadInputPump");

      static SKIF_GamePadInputHelper& parent = SKIF_GamePadInputHelper::GetInstance ( );
      extern std::atomic<bool> SKIF_Shutdown;

      DWORD packetLast = 0,
            packetNew  = 0;

      do
      {
        // Sleep when there's nothing to do
        while (! parent.m_bThreadAwake.load())
        {
          PLOG_VERBOSE << "SLEEP!";
          SleepConditionVariableCS (
            &parent.m_GamePadInput, &GamepadInputPump,
              INFINITE
          );
        }

        PLOG_VERBOSE << "Awake!";

        packetNew  = parent.UpdateXInputState ( ).dwPacketNumber;

        if (packetNew  > 0  &&
            packetNew != packetLast)
        {
          packetLast = packetNew;
          PostMessage (parent.m_hWindowHandle, WM_SKIF_GAMEPAD, 0x0, 0x0);
        }

        // XInput tends to have ~3-7 ms of latency between updates
        //   best-case, try to delay the next poll until there's
        //     new data.
        Sleep (5);

      } while (! SKIF_Shutdown.load());

      LeaveCriticalSection  (&GamepadInputPump);
      DeleteCriticalSection (&GamepadInputPump);

      PLOG_VERBOSE << "Shutting down...";

      return 0;
    }, nullptr, 0x0, nullptr
  );
}

bool
SKIF_GamePadInputHelper::UnregisterDevNotification (void)
{
  return (m_hDeviceNotify != NULL) ? UnregisterDeviceNotification (m_hDeviceNotify) : true;
}

#pragma once
#include <atomic>
#include <array>
#include <vector>
#include <Windows.h>
#include <Xinput.h>

#define XINPUT_GAMEPAD_GUIDE         0x00400
#define XINPUT_GAMEPAD_LEFT_TRIGGER  0x10000 // Custom
#define XINPUT_GAMEPAD_RIGHT_TRIGGER 0x20000 // Custom

struct SKIF_GamePadInputHelper
{
  bool               HasGamePad                (void);      // Returns true if at least one gamepad is connected
  std::vector <bool> GetGamePads               (void);      // Returns a boolean vector indicating the connectivity state of each XInput slot
  void               InvalidateGamePads        (void);      // This triggers a check for connected gamepads next time UpdateXInputState ( ) executes
  XINPUT_STATE       GetXInputState            (void);      // Returns the latest XInput state across all slots
  void               WakeThread                (void);      // Wake the gamepad worker thread
  void               SleepThread               (void);      // Sleep the gamepad worker thread
  void               SpawnChildThread          (void);      // Spawns the gamepad worker thread
  bool               RegisterDevNotification   (HWND hWnd); // Registers the given window for device notifications
  bool               UnregisterDevNotification (void);      // Unregister the window for device notifications

  SKIF_GamePadInputHelper (SKIF_GamePadInputHelper const&) = delete; // Delete copy constructor
  SKIF_GamePadInputHelper (SKIF_GamePadInputHelper&&)      = delete; // Delete move constructor

  static SKIF_GamePadInputHelper& GetInstance  (void)
  {
      static SKIF_GamePadInputHelper instance;
      return instance;
  }

protected:
  XINPUT_STATE       UpdateXInputState         (void); // Responsible for updating the latest XINPUT_STATE; only called by the child thread!

private:
  SKIF_GamePadInputHelper (void);

  HWND                       m_hWindowHandle   = NULL;
  HDEVNOTIFY                 m_hDeviceNotify   = NULL;
  CONDITION_VARIABLE         m_GamePadInput    = {  };
  std::array <
    std::atomic <bool>, XUSER_MAX_COUNT
  >                          m_bGamepads       = { false, false, false, false };
  std::atomic <bool>         m_bThreadAwake    = false; // 0 - No focus, so sleep.       1 - Focus, so remain awake
  std::atomic_int            m_iCurrentGamepad = 0;
  XINPUT_STATE*              m_xisGamepad      = nullptr;
  XINPUT_STATE               m_xisGamepadBuffers [3];
  std::atomic <bool>         m_bWantUpdate     = true;

  using XInputGetState_pfn =
    DWORD (WINAPI *)( DWORD, XINPUT_STATE * );
  using XInputGetCapabilities_pfn =
    DWORD (WINAPI *)( DWORD, DWORD, XINPUT_CAPABILITIES * );

  static HMODULE                         hModXInput;
  static XInputGetState_pfn              SKIF_XInputGetState;
  static XInputGetCapabilities_pfn       SKIF_XInputGetCapabilities;
  static bool                            runOnce;
  static bool                            skipFreeLibrary;
};
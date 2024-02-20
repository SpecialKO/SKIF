#pragma once
#include <atomic>
#include <array>
#include <vector>
#include <Windows.h>
#include <Xinput.h>

struct SKIF_GamePadInputHelper
{
  std::vector <bool> GetGamePads               (void);
  void               InvalidateGamePads        (void); // This triggers a check for connected gamepads next time UpdateXInputState ( ) executes
  XINPUT_STATE       GetXInputState            (void);
  void               WakeThread                (void);
  void               SleepThread               (void);
  void               SpawnChildThread          (void);
  bool                 RegisterDevNotification (HWND hWnd);
  bool               UnregisterDevNotification (void);

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

  HWND                       m_hWindowHandle = NULL;
  HDEVNOTIFY                 m_hDeviceNotify = NULL;
  CONDITION_VARIABLE         m_GamePadInput  = {  };
  std::array <
    std::atomic <bool>, XUSER_MAX_COUNT
  >                          m_bGamepads     = { false, false, false, false };
  std::atomic <bool>         m_bThreadAwake  = false; // 0 - No focus, so sleep.       1 - Focus, so remain awake
  std::atomic <XINPUT_STATE> m_xisGamepad    = { };
  std::atomic <bool>         m_bWantUpdate   = true;
};
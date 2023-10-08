// dear imgui: Platform Binding for Windows (standard windows API for 32 and 64 bits applications)
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)

// Implemented features:
//  [X] Platform: Clipboard support (for Win32 this is actually part of core imgui)
//  [X] Platform: Mouse cursor shape and visibility. Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: Keyboard arrays indexed using VK_* Virtual Key Codes, e.g. ImGui::IsKeyPressed(VK_SPACE).
//  [X] Platform: Gamepad support. Enabled with 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad'.
//  [X] Platform: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

#define IMGUI_DEFINE_MATH_OPERATORS

#include <dwmapi.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_win32.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <Dbt.h>
#include <XInput.h>
#include <tchar.h>
#include <limits>
#include <array>
#include <algorithm>
#include <format>
#include <utility/injection.h>

// PLOG
#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"

// Registry Settings
#include <utility/registry.h>

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

bool removeDWMBorders = false;

auto constexpr XUSER_INDEXES =
  std::array <DWORD, 4> { 0, 1, 2, 3 };

constexpr const wchar_t* SKIF_ImGui_WindowClass = L"SKIF_ImGuiWindow";
constexpr const wchar_t* SKIF_ImGui_WindowTitle = L"Special K Popup"; // Default

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2018-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
//  2019-05-11: Inputs: Don't filter value from WM_CHAR before calling AddInputCharacter().
//  2019-01-17: Misc: Using GetForegroundWindow()+IsChild() instead of GetActiveWindow() to be compatible with windows created in a different thread or parent.
//  2019-01-17: Inputs: Added support for mouse buttons 4 and 5 via WM_XBUTTON* messages.
//  2019-01-15: Inputs: Added support for XInput gamepads (if ImGuiConfigFlags_NavEnableGamepad is set by user application).
//  2018-11-30: Misc: Setting up io.BackendPlatformName so it can be displayed in the About Window.
//  2018-06-29: Inputs: Added support for the ImGuiMouseCursor_Hand cursor.
//  2018-06-10: Inputs: Fixed handling of mouse wheel messages to support fine position messages (typically sent by track-pads).
//  2018-06-08: Misc: Extracted imgui_impl_win32.cpp/.h away from the old combined DX9/DX10/DX11/DX12 examples.
//  2018-03-20: Misc: Setup io.BackendFlags ImGuiBackendFlags_HasMouseCursors and ImGuiBackendFlags_HasSetMousePos flags + honor ImGuiConfigFlags_NoMouseCursorChange flag.
//  2018-02-20: Inputs: Added support for mouse cursors (ImGui::GetMouseCursor() value and WM_SETCURSOR message handling).
//  2018-02-06: Inputs: Added mapping for ImGuiKey_Space.
//  2018-02-06: Inputs: Honoring the io.WantSetMousePos by repositioning the mouse (when using navigation and ImGuiConfigFlags_NavMoveMouse is set).
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2018-01-20: Inputs: Added Horizontal Mouse Wheel support.
//  2018-01-08: Inputs: Added mapping for ImGuiKey_Insert.
//  2018-01-05: Inputs: Added WM_LBUTTONDBLCLK double-click handlers for window classes with the CS_DBLCLKS flag.
//  2017-10-23: Inputs: Added WM_SYSKEYDOWN / WM_SYSKEYUP handlers so e.g. the VK_MENU key can be read.
//  2017-10-23: Inputs: Using Win32 ::SetCapture/::GetCapture() to retrieve mouse positions outside the client area when dragging.
//  2016-11-12: Inputs: Only call Win32 ::SetCursor(NULL) when io.MouseDrawCursor is set.

// External functions
extern bool SKIF_Util_IsWindows8Point1OrGreater (void);
extern bool SKIF_Util_IsWindows10OrGreater      (void);
extern bool SKIF_Util_IsWindows11orGreater      (void);
extern bool SKIF_Util_GetDragFromMaximized      (void);

// Forward Declarations
static void                  ImGui_ImplWin32_InitPlatformInterface     (void);
static void                  ImGui_ImplWin32_ShutdownPlatformInterface (void);
static void                  ImGui_ImplWin32_UpdateMonitors            (void);
static ImGuiPlatformMonitor* ImGui_ImplWin32_GetPlatformMonitor        (ImGuiViewport *viewport, bool center);

// Win32 Data
static HWND                 g_hWnd = 0;
static INT64                g_Time = 0;
static bool                 g_Focused = false; // Always assume we don't have focus on launch
static INT64                g_TicksPerSecond = 0;
static ImGuiMouseCursor     g_LastMouseCursor = ImGuiMouseCursor_COUNT;
//static bool                 g_HasGamepad [XUSER_MAX_COUNT] = { false, false, false, false };
std::array<std::atomic<bool>, XUSER_MAX_COUNT> g_HasGamepad = { false, false, false, false };
struct S_XINPUT_STATE {
  XINPUT_STATE  last_state = {         };
  LARGE_INTEGER last_qpc   = { 0, 0ULL };
};

struct {
  XINPUT_STATE  last_state = {         };
  LARGE_INTEGER last_qpc   = { 0, 0ULL };
} static                    g_GamepadHistory [XUSER_MAX_COUNT];
static bool                 g_WantUpdateHasGamepad = true;
static bool                 g_WantUpdateMonitors   = true;

using XInputGetState_pfn =
DWORD (WINAPI *)( DWORD, XINPUT_STATE * );
using XInputGetCapabilities_pfn =
DWORD (WINAPI *)( DWORD, DWORD, XINPUT_CAPABILITIES * );

static XInputGetState_pfn        ImGui_XInputGetState = nullptr;
static XInputGetCapabilities_pfn ImGui_XInputGetCapabilities = nullptr;
static HMODULE                   g_hModXInput = nullptr;

// Peripheral Functions
bool SKIF_ImGui_ImplWin32_IsFocused (void)
{
  // We should be able to trust g_Focused as it is fed by WM_SETFOCUS and WM_KILLFOCUS
  return g_Focused;

#if 0
  extern HWND SKIF_hWnd;
  extern HWND SKIF_ImGui_hWnd;
  static int uglyAssWorkaround = 0;

  // The g_Focused state should be trustworthy after the initial 500 frames or so
  //  This is an ugly workaround to ensure we don't run the below code every single frame
  //   and instead trust on WM_SETFOCUS and WM_KILLFOCUS to be correct
  if (uglyAssWorkaround == 2 || SKIF_ImGui_hWnd != NULL && ImGui::GetFrameCount ( ) > 500)
    return g_Focused;
    
  // Fallback which only executes the first couple of frames
  static INT64 lastTime  = std::numeric_limits <INT64>::max ();
  static bool  lastFocus = false;

  // Executes once per frame
  if (lastTime != g_Time)
  {   lastTime  = g_Time;
    if (HWND focused_hwnd = ::GetForegroundWindow ())
    {
      if (::IsChild (focused_hwnd,  SKIF_hWnd))  // g_hWnd 
      {              focused_hwnd = SKIF_hWnd; } // g_hWnd
        DWORD
          dwWindowOwnerPid = 0;

      GetWindowThreadProcessId (
        focused_hwnd,
          &dwWindowOwnerPid
      );

      static DWORD
        dwPidOfMe = GetCurrentProcessId ();

      lastFocus = (dwWindowOwnerPid == dwPidOfMe);

      /*
      if (SKIF_Util_IsWindows10OrGreater ( ) && g_Focused != lastFocus && SKIF_ImGui_hWnd != NULL)
      {
        // Ugly-ass workaround for the window never receiving WM_KILLFOCUS on launch if it gets unfocused quickly
        // We also need to send a WM_SETFOCUS when it regains focus because apparently Windows doesn't send it either...?
        // Only do it for the first 1000 frames...
        if (g_Focused && uglyAssWorkaround == 0)
        {
          uglyAssWorkaround++;
          //OutputDebugString (L"Ugly-ass workaround... Sending WM_KILLFOCUS !\n");
          PLOG_INFO << "Ugly-ass workaround... Sending WM_KILLFOCUS !";
          //SK_RunOnce (SendMessage (SKIF_ImGui_hWnd, WM_KILLFOCUS, 0, 0));
        }
        else if (uglyAssWorkaround == 1)
        {
          uglyAssWorkaround++;
          // If we have run the ugly-ass workaround, we need to send an additional WM_SETFOCUS to restore things once SKIF regains focus
          //OutputDebugString (L"Ugly-ass workaround... Sending WM_SETFOCUS !\n");
          PLOG_INFO << "Ugly-ass workaround... Sending WM_SETFOCUS !";
          //SK_RunOnce (SendMessage (SKIF_ImGui_hWnd, WM_SETFOCUS, 0, 0));
        }
      }
      */
    }
    else // In the case that GetForegroundWindow () fails, assume g_Focused is correct
      lastFocus = g_Focused;
  }

  return lastFocus;
#endif
}

bool    ImGui_ImplWin32_InitXInput (void)
{
  if (g_hModXInput != nullptr)
    return true;

  g_hModXInput =
    LoadLibraryW (L"XInput1_4.dll");

  if (g_hModXInput == nullptr)
    g_hModXInput =
    LoadLibraryW (L"XInput1_3.dll");

  if (g_hModXInput == nullptr)
    g_hModXInput =
    LoadLibraryW (L"XInput9_1_0.dll");

  if (g_hModXInput == nullptr)
    return false;

  ImGui_XInputGetState = (XInputGetState_pfn)
    GetProcAddress (g_hModXInput, "XInputGetState");
  ImGui_XInputGetCapabilities = (XInputGetCapabilities_pfn)
    GetProcAddress (g_hModXInput, "XInputGetCapabilities");

  return true;
}

bool    ImGui_ImplWin32_RegisterXInputNotifications (void *hwnd)
{
  static
    HDEVNOTIFY hRegisteredNotification = nullptr;

  if (hRegisteredNotification != nullptr)
    return true;

  GUID GUID_DEVINTERFACE_HID =
  { 0x4D1E55B2L, 0xF16F, 0x11CF,
         { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

  DEV_BROADCAST_DEVICEINTERFACE_W
    NotificationFilter                 = { };
    NotificationFilter.dbcc_size       = sizeof (DEV_BROADCAST_DEVICEINTERFACE_W);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid  = GUID_DEVINTERFACE_HID;

  hRegisteredNotification =
      RegisterDeviceNotificationW (
        hwnd, &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
      );

  return true;
}

// Functions
bool    ImGui_ImplWin32_Init (void *hwnd)
{
  UNREFERENCED_PARAMETER (hwnd);

  if (!::QueryPerformanceFrequency ((LARGE_INTEGER *)&g_TicksPerSecond))
    return false;
  if (!::QueryPerformanceCounter ((LARGE_INTEGER *)&g_Time))
    return false;

// Setup back-end capabilities flags
  ImGuiIO &io = ImGui::GetIO ( );
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
  io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
  io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)
  io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport; // We can set io.MouseHoveredViewport correctly (optional, not easy)
  io.BackendPlatformName = "imgui_impl_win32_skif";

//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  // Our mouse update function expect PlatformHandle to be filled for the main viewport
  g_hWnd = nullptr; // (HWND)hwnd;
  ImGuiViewport *main_viewport = ImGui::GetMainViewport ( );
  main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (void *)g_hWnd;

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    ImGui_ImplWin32_InitPlatformInterface ( ); // Sets up another overarching window for SKIF

// Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array that we will update during the application lifetime.
  io.KeyMap [ImGuiKey_Tab]         = VK_TAB;
  io.KeyMap [ImGuiKey_LeftArrow]   = VK_LEFT;
  io.KeyMap [ImGuiKey_RightArrow]  = VK_RIGHT;
  io.KeyMap [ImGuiKey_UpArrow]     = VK_UP;
  io.KeyMap [ImGuiKey_DownArrow]   = VK_DOWN;
  io.KeyMap [ImGuiKey_PageUp]      = VK_PRIOR;
  io.KeyMap [ImGuiKey_PageDown]    = VK_NEXT;
  io.KeyMap [ImGuiKey_Home]        = VK_HOME;
  io.KeyMap [ImGuiKey_End]         = VK_END;
  io.KeyMap [ImGuiKey_Insert]      = VK_INSERT;
  io.KeyMap [ImGuiKey_Delete]      = VK_DELETE;
  io.KeyMap [ImGuiKey_Backspace]   = VK_BACK;
  io.KeyMap [ImGuiKey_Space]       = VK_SPACE;
  io.KeyMap [ImGuiKey_Enter]       = VK_RETURN;
  io.KeyMap [ImGuiKey_Escape]      = VK_ESCAPE;
  io.KeyMap [ImGuiKey_KeyPadEnter] = VK_RETURN;
  io.KeyMap [ImGuiKey_A]           = 'A';
  io.KeyMap [ImGuiKey_C]           = 'C';
  io.KeyMap [ImGuiKey_V]           = 'V';
  io.KeyMap [ImGuiKey_X]           = 'X';
  io.KeyMap [ImGuiKey_Y]           = 'Y';
  io.KeyMap [ImGuiKey_Z]           = 'Z';

  ImGui_ImplWin32_InitXInput ( );

  return true;
}

void
ImGui_ImplWin32_Shutdown (void)
{
  ImGui_ImplWin32_ShutdownPlatformInterface ( );
  g_hWnd = (HWND)0;

  if (g_hModXInput != nullptr)
  {
    if (FreeLibrary (g_hModXInput))
      g_hModXInput = nullptr;
  }
}

static bool
ImGui_ImplWin32_UpdateMouseCursor (void)
{
  ImGuiIO &io =
    ImGui::GetIO ( );

  if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
    return false;

  ImGuiMouseCursor imgui_cursor =
    ImGui::GetMouseCursor ( );

  if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
  {
    // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
    ::SetCursor (NULL);
  }

  else
  {
    // Show OS mouse cursor
    LPTSTR  win32_cursor = IDC_ARROW;
    switch (imgui_cursor)
    {
    case ImGuiMouseCursor_Arrow:        win32_cursor = IDC_ARROW;    break;
    case ImGuiMouseCursor_TextInput:    win32_cursor = IDC_IBEAM;    break;
    case ImGuiMouseCursor_ResizeAll:    win32_cursor = IDC_SIZEALL;  break;
    case ImGuiMouseCursor_ResizeEW:     win32_cursor = IDC_SIZEWE;   break;
    case ImGuiMouseCursor_ResizeNS:     win32_cursor = IDC_SIZENS;   break;
    case ImGuiMouseCursor_ResizeNESW:   win32_cursor = IDC_SIZENESW; break;
    case ImGuiMouseCursor_ResizeNWSE:   win32_cursor = IDC_SIZENWSE; break;
    case ImGuiMouseCursor_Hand:         win32_cursor = IDC_HAND;     break;
    }

    ::SetCursor (::LoadCursor (NULL, win32_cursor));
  }

  return true;
}

// This code supports multi-viewports (multiple OS Windows mapped into different Dear ImGui viewports)
// Because of that, it is a little more complicated than your typical single-viewport binding code!
static void
ImGui_ImplWin32_UpdateMousePos (void)
{
  ImGuiIO &io =
    ImGui::GetIO ( );

  // Set OS mouse position if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
  // (When multi-viewports are enabled, all imgui positions are same as OS positions)
  if (io.WantSetMousePos && g_Focused && io.ConfigFlags & ImGuiConfigFlags_NavEnableSetMousePos)
  {
    POINT pos = {
      (int)io.MousePos.x,
      (int)io.MousePos.y
    };

    if (( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) == 0)
      ::ClientToScreen (g_hWnd, &pos);

    ::SetCursorPos (pos.x, pos.y);
  }

  io.MousePos = ImVec2 (-FLT_MAX, -FLT_MAX);
  io.MouseHoveredViewport = 0;

  // Set imgui mouse position
  POINT                 mouse_screen_pos = { };
  if (!::GetCursorPos (&mouse_screen_pos))
    return;

  // If we are unfocused, see if we are currently hovering over one of our viewports
  if (! g_Focused)
  {
    if (HWND hovered_hwnd = ::WindowFromPoint (mouse_screen_pos))
    {
      if (NULL == ImGui::FindViewportByPlatformHandle ((void *)hovered_hwnd))
      {
        io.MousePos = ImVec2 (-FLT_MAX, -FLT_MAX);
        return; // We are not in fact hovering over anything, so reset position and abort
      }
    }
  }
  

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
    // This is the position you can get with GetCursorPos(). In theory adding viewport->Pos is also the reverse operation of doing ScreenToClient().
  ////if (ImGui::FindViewportByPlatformHandle ((void *)focused_hwnd) != NULL)
    //{
      io.MousePos =
        ImVec2 ( (float)mouse_screen_pos.x,
                 (float)mouse_screen_pos.y );
    //}
  }

  else {
    if (HWND focused_hwnd = ::GetForegroundWindow ( ))
    {
      if (::IsChild (focused_hwnd, g_hWnd))
        focused_hwnd = g_hWnd;

      // Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window.)
      // This is the position you can get with GetCursorPos() + ScreenToClient() or from WM_MOUSEMOVE.
      if (focused_hwnd == g_hWnd)
      {
        POINT mouse_client_pos =
          mouse_screen_pos;

        ::ScreenToClient (g_hWnd, &mouse_client_pos);

        io.MousePos =
          ImVec2 ( (float)mouse_client_pos.x,
                    (float)mouse_client_pos.y );
      }
    }
  }

  // (Optional) When using multiple viewports: set io.MouseHoveredViewport to the viewport the OS mouse cursor is hovering.
  // Important: this information is not easy to provide and many high-level windowing library won't be able to provide it correctly, because
  // - This is _ignoring_ viewports with the ImGuiViewportFlags_NoInputs flag (pass-through windows).
  // - This is _regardless_ of whether another viewport is focused or being dragged from.
  // If ImGuiBackendFlags_HasMouseHoveredViewport is not set by the back-end, imgui will ignore this field and infer the information by relying on the
  // rectangles and last focused time of every viewports it knows about. It will be unaware of foreign windows that may be sitting between or over your windows.
  if (HWND hovered_hwnd = ::WindowFromPoint (mouse_screen_pos))
  {
    if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle ((void *)hovered_hwnd))
    {
      if (( viewport->Flags & ImGuiViewportFlags_NoInputs ) == 0) // FIXME: We still get our NoInputs window with WM_NCHITTEST/HTTRANSPARENT code when decorated?
      {
        io.MouseHoveredViewport = viewport->ID;
      }
    }
  }
}

void *
memset_size (
  void *dest,
  size_t  n,
  const char *src,
  size_t  element)
{
  if (n > 0 && element > 0)
  {
    memcpy (dest,
      src, element);
    n *= element  ;
    while (n > element)
    {
      size_t remaining = n - element;
      size_t n_this_time = remaining > element ?
        element :
        remaining;
      memcpy (
        (BYTE *)dest +
        element, dest, n_this_time
      ); element += n_this_time;
    }
  }

  return
    dest;
}


XINPUT_STATE ImGui_ImplWin32_GetXInputPackage ( )
{
  struct {
    XINPUT_STATE  last_state = {         };
    LARGE_INTEGER last_qpc   = { 0, 0ULL };
  } static thread_local history [XUSER_MAX_COUNT];

  XINPUT_STATE xinput_state = { };

  for ( auto idx : XUSER_INDEXES )
  {
    if ( g_HasGamepad [idx].load() && ImGui_XInputGetState(idx, &xinput_state) == ERROR_SUCCESS)
    {
      // If button state is different, this controller is active...
      if ( xinput_state.dwPacketNumber != history [idx].last_state.dwPacketNumber )
      {
        if (                      xinput_state.Gamepad.wButtons !=
                      history [idx].last_state.Gamepad.wButtons ||
                                  xinput_state.Gamepad.bLeftTrigger !=
                      history [idx].last_state.Gamepad.bLeftTrigger ||
                                  xinput_state.Gamepad.bRightTrigger !=
                      history [idx].last_state.Gamepad.bRightTrigger )
        {
                                    history [idx].last_state = xinput_state;
          QueryPerformanceCounter (&history [idx].last_qpc);
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
                                      history [idx].last_state = xinput_state;
            QueryPerformanceCounter (&history [idx].last_qpc);
          }

          // Inside deadzone, record position but do not update the packet count
          else
          {
            history [idx].last_state.Gamepad =
                                 xinput_state.Gamepad;
          }
        }
      }
    }

    else
    {
      history [idx].last_qpc.QuadPart = 0;
    }
  }

  struct {
    LARGE_INTEGER qpc  = { 0, 0ULL };
    DWORD         slot =    INFINITE;
  } newest;

  for ( auto idx : XUSER_INDEXES )
  {
    auto qpc =
      history [idx].last_qpc.QuadPart;

    if ( qpc > newest.qpc.QuadPart )
    {
      newest.slot         = idx;
      newest.qpc.QuadPart = qpc;
    }
  }

  if (newest.slot != INFINITE)
  {
    xinput_state =
      history [newest.slot].last_state;
  }

  return newest.slot != INFINITE ?
        xinput_state :  XINPUT_STATE { 0, XINPUT_GAMEPAD { } };
}

// Gamepad navigation mapping
void ImGui_ImplWin32_UpdateGamepads ( )
{
  ImGuiIO &io =
    ImGui::GetIO ( );

  std::fill_n (
    io.NavInputs, ImGuiNavInput_COUNT, 0.0f
  );

  // ----

  if (! g_Focused)
    return;

  if (( io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad ) == 0)
    return;

  //
  // Fail-safe in case the state of g_Focused is out-of-sync
  //
  if (HWND focused_hwnd = ::GetForegroundWindow ())
  {
    DWORD
      dwWindowOwnerPid = 0;

    GetWindowThreadProcessId (
      focused_hwnd,
        &dwWindowOwnerPid
    );

    static DWORD
      dwPidOfMe = GetCurrentProcessId ();

    // Don't poll the gamepad when we're not focused.
    if (dwWindowOwnerPid != dwPidOfMe)
    {
      PLOG_VERBOSE << "g_Focused is out-of-sync!";
      return;
    }
  }

  // ----

  // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
  // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
  if (g_WantUpdateHasGamepad && (ImGui_XInputGetCapabilities != nullptr))
  {
    XINPUT_CAPABILITIES caps;

    for ( auto idx : XUSER_INDEXES )
    {
      g_HasGamepad [idx].store ((ImGui_XInputGetCapabilities(idx, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS));
    }

    g_WantUpdateHasGamepad = false;
  }

  else if (ImGui_XInputGetCapabilities == nullptr)
  {
    for ( auto idx : XUSER_INDEXES )
    {
      g_HasGamepad [idx].store (false);
    }
  }

  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  
  XINPUT_STATE state =
    ImGui_ImplWin32_GetXInputPackage ( );

  if (state.dwPacketNumber != 0)
  {
    const XINPUT_GAMEPAD &gamepad = state.Gamepad;

    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

#define MAP_BUTTON(NAV_NO, BUTTON_ENUM)     { io.NavInputs[NAV_NO] = (gamepad.wButtons & BUTTON_ENUM) ? 1.0f : 0.0f; }
#define MAP_ANALOG(NAV_NO, VALUE, V0, V1)   { float vn = (float)(VALUE - V0) / (float)(V1 - V0); if (vn > 1.0f) vn = 1.0f; if (vn > 0.0f && io.NavInputs[NAV_NO] < vn) io.NavInputs[NAV_NO] = vn; }
    MAP_BUTTON (ImGuiNavInput_Activate,    XINPUT_GAMEPAD_A);              // Cross / A
    MAP_BUTTON (ImGuiNavInput_Cancel,      XINPUT_GAMEPAD_B);              // Circle / B
    MAP_BUTTON (ImGuiNavInput_Menu,        XINPUT_GAMEPAD_X);              // Square / X
    MAP_BUTTON (ImGuiNavInput_Input,       XINPUT_GAMEPAD_Y);              // Triangle / Y
    MAP_BUTTON (ImGuiNavInput_DpadLeft,    XINPUT_GAMEPAD_DPAD_LEFT);      // D-Pad Left
    MAP_BUTTON (ImGuiNavInput_DpadRight,   XINPUT_GAMEPAD_DPAD_RIGHT);     // D-Pad Right
    MAP_BUTTON (ImGuiNavInput_DpadUp,      XINPUT_GAMEPAD_DPAD_UP);        // D-Pad Up
    MAP_BUTTON (ImGuiNavInput_DpadDown,    XINPUT_GAMEPAD_DPAD_DOWN);      // D-Pad Down
    MAP_BUTTON (ImGuiNavInput_FocusPrev,   XINPUT_GAMEPAD_LEFT_SHOULDER);  // L1 / LB
    MAP_BUTTON (ImGuiNavInput_FocusNext,   XINPUT_GAMEPAD_RIGHT_SHOULDER); // R1 / RB
    MAP_BUTTON (ImGuiNavInput_TweakSlow,   XINPUT_GAMEPAD_LEFT_SHOULDER);  // L1 / LB
    MAP_BUTTON (ImGuiNavInput_TweakFast,   XINPUT_GAMEPAD_RIGHT_SHOULDER); // R1 / RB
    MAP_ANALOG (ImGuiNavInput_LStickLeft,  gamepad.sThumbLX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG (ImGuiNavInput_LStickRight, gamepad.sThumbLX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG (ImGuiNavInput_LStickUp,    gamepad.sThumbLY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG (ImGuiNavInput_LStickDown,  gamepad.sThumbLY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32767);
#undef MAP_BUTTON
#undef MAP_ANALOG
  }

  if (io.KeysDown  [VK_RETURN])
      io.NavInputs [ImGuiNavInput_Activate] = 1.0f;
}

INT64 current_time;
INT64 current_time_ms;

void
ImGui_ImplWin32_NewFrame (void)
{
  ImGuiIO &io =
    ImGui::GetIO ( );

  // Disabling this assertion since the font isn't expected to be rendered at this point since we're using a custom callback
  // Aemony, 2023-09-24
  //IM_ASSERT (io.Fonts->IsBuilt ( ) && "Font atlas not built! It is generally built by the renderer back-end. Missing call to renderer _NewFrame() function? e.g. ImGui_ImplOpenGL3_NewFrame().");

  // Setup display size (every frame to accommodate for window resizing)
  /*
  RECT                      rect = { };
  ::GetClientRect (g_hWnd, &rect);

  io.DisplaySize =
    ImVec2 ( (float)( rect.right  - rect.left ),
             (float)( rect.bottom - rect.top  ) );
  */

  if (g_WantUpdateMonitors)
    ImGui_ImplWin32_UpdateMonitors ( );

  // Setup time step
  ::QueryPerformanceCounter ((LARGE_INTEGER *)&current_time);

  io.DeltaTime =
    std::min ( 1.0f,
    std::max ( 0.0f, static_cast <float> (
                    (static_cast <long double> (                       current_time) -
                     static_cast <long double> (std::exchange (g_Time, current_time))) /
                     static_cast <long double> (               g_TicksPerSecond      ) ) )
    );

  current_time_ms =
  current_time /
      ( g_TicksPerSecond / 1000LL );

  // Reset keyboard modifiers inputs
  io.KeyCtrl  = false;
  io.KeyShift = false;
  io.KeyAlt   = false;
  io.KeySuper = false; // Isn't actually used, so always set this to false

  // Read keyboard modifiers inputs (but only if we're focused)
  if (g_Focused)
  {
    io.KeyCtrl  = ( ::GetKeyState (VK_CONTROL) & 0x8000 ) != 0;
    io.KeyShift = ( ::GetKeyState (VK_SHIFT)   & 0x8000 ) != 0;
    io.KeyAlt   = ( ::GetKeyState (VK_MENU)    & 0x8000 ) != 0;

    // Update game controllers (if enabled and available)
    ImGui_ImplWin32_UpdateGamepads ( );
  }

  // io.KeysDown[], io.MousePos, io.MouseDown[], io.MouseWheel: filled by the WndProc handler below.

  // Update OS mouse position
  ImGui_ImplWin32_UpdateMousePos ();

  // Update OS mouse cursor with the cursor requested by imgui
  ImGuiMouseCursor mouse_cursor =
       io.MouseDrawCursor ?
    ImGuiMouseCursor_None : ImGui::GetMouseCursor ();

  if (g_LastMouseCursor != mouse_cursor)
  {
    g_LastMouseCursor = mouse_cursor;
    ImGui_ImplWin32_UpdateMouseCursor ();
  }
}

// Allow compilation with old Windows SDK. MinGW doesn't have default _WIN32_WINNT/WINVER versions.
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
#ifndef DBT_DEVNODES_CHANGED
#define DBT_DEVNODES_CHANGED 0x0007
#endif

// Process Win32 mouse/keyboard inputs.
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
// PS: In this Win32 handler, we use the capture API (GetCapture/SetCapture/ReleaseCapture) to be able to read mouse coordinates when dragging mouse outside of our window bounds.
// PS: We treat DBLCLK messages as regular mouse down messages, so this code will work on windows classes that have the CS_DBLCLKS flag set. Our own example app code doesn't set this flag.
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // This is called by SKIF_WndProc ( ) as well as ImGui_ImplWin32_WndProcHandler_PlatformWindow ( ).
  // It gets called for the main SKIF window/viewport, as well as any additional viewport windows.

  if (ImGui::GetCurrentContext ( ) == NULL)
    return 0;

  ImGuiIO &io = ImGui::GetIO ( );

  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );
  extern std::atomic<bool> gamepadThreadAwake;
  extern HWND SKIF_ImGui_hWnd;

  extern bool msgDontRedraw;

  extern bool KeyWinKey;
  extern int  SnapKeys;

  switch (msg)
  {
  case WM_GETICON: // Work around bug in Task Manager sending this message every time it refreshes its process list
    msgDontRedraw = true;
    return true;
    break;

  case WM_CLOSE:
    extern bool bKeepProcessAlive;

    // Handle attempt to close the window
    if (hwnd != nullptr)
    {
      // Handle the service before we exit (handled later now)
      //if (_inject.bCurrentState && ! _registry.bAllowBackgroundService )
      //  _inject._StartStopInject (true);

      bKeepProcessAlive = false;

      //PostMessage (hwnd, WM_QUIT, 0, 0);
      PostQuitMessage (0);
      return 1;
    }
    break;

  case WM_SETFOCUS:
    // Gets sent incorrectly by e.g. NieR: Replicant in FSE mode
    //   when switching to the start menu using WinKey, resulting
    //     in a stucked and incorrect focus state.
    //        hwnd == SKIF_hWnd in those cases.

    //OutputDebugString(L"WM_SETFOCUS\n");

    if (ImGui::FindViewportByPlatformHandle (hwnd) != NULL) // Should be enough in all scenarios
    // (SKIF_ImGui_hWnd != NULL &&
    // (SKIF_ImGui_hWnd == hwnd ||
    //  SKIF_ImGui_hWnd == GetAncestor (hwnd, GA_ROOTOWNER)))
    {
      g_Focused = true;
      gamepadThreadAwake.store (true);
      //OutputDebugString(L"Gained focus\n");
      PLOG_VERBOSE << "Gained focus";

      extern CONDITION_VARIABLE  SKIF_IsFocused;
      WakeAllConditionVariable (&SKIF_IsFocused);

      return 0;
    }

    break;

  case WM_KILLFOCUS:
    // (! IsChild (SKIF_hWnd, (HWND)wParam)) cannot be used since
    //   SKIF_hWnd is not the PARENT window, but only the ROOTOWNER
    //     of all underlying ImGui windows
    // 
    // IsChild () also cannot be used since technically no ImGui popup windows are
    //   any children of their parent window -- they're all their own separate window

    //OutputDebugString(L"WM_KILLFOCUS\n");

    if (ImGui::FindViewportByPlatformHandle ((void *)wParam) == NULL) // Should be enough in all scenarios
    // (SKIF_ImGui_hWnd != NULL         &&
    // (SKIF_ImGui_hWnd != (HWND)wParam &&
    //  SKIF_ImGui_hWnd != GetAncestor ((HWND)wParam, GA_ROOTOWNER)))
    {
      g_Focused = false;
      gamepadThreadAwake.store (false);
      //OutputDebugString(L"Killed focus\n");
      PLOG_VERBOSE << "Killed focus";

      std::fill ( std::begin (io.KeysDown), std::end (io.KeysDown),
                  false );

      // Kill mouse capture on focus lost
      io.MouseDown [0] = false;
      io.MouseDown [1] = false;
      io.MouseDown [2] = false;
      io.MouseDown [3] = false;
      io.MouseDown [4] = false;
      if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == hwnd)
        ::ReleaseCapture ( );
    }
    return 0;
    break;

  case WM_LBUTTONDOWN:   case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN:   case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN:   case WM_MBUTTONDBLCLK:
  case WM_XBUTTONDOWN:   case WM_XBUTTONDBLCLK:
  {
    int button = 0;
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) { button = 0; }
    if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) { button = 1; }
    if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) { button = 2; }
    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) { button = ( GET_XBUTTON_WPARAM (wParam) == XBUTTON1 ) ? 3 : 4; }
    if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == NULL)
      ::SetCapture (hwnd);
    io.MouseDown [button] = true;
    return 0;
  }
  // This is needed to open ImGui menus on right clicks in "non-client" areas (aka draggable areas)
  case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK:
  {
    int button = 1;
    if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == NULL)
      ::SetCapture (hwnd);
    io.MouseDown [button] = true;
    return 0;
  }
#if 0
  // This is not necessary since we convert HTCLIENT to HTCAPTION,
  //   so all non-client mouse input is handled by the OS itself
  case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK:
  case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK:
  case WM_NCMBUTTONDOWN: case WM_NCMBUTTONDBLCLK:
  case WM_NCXBUTTONDOWN: case WM_NCXBUTTONDBLCLK:
  {
    int button = 0;
    if (msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK) { button = 0; }
    if (msg == WM_NCRBUTTONDOWN || msg == WM_NCRBUTTONDBLCLK) { button = 1; }
    if (msg == WM_NCMBUTTONDOWN || msg == WM_NCMBUTTONDBLCLK) { button = 2; }
    if (msg == WM_NCXBUTTONDOWN || msg == WM_NCXBUTTONDBLCLK) { button = ( GET_XBUTTON_WPARAM (wParam) == XBUTTON1 ) ? 3 : 4; }
    //if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == NULL)
    //  ::SetCapture (hwnd);
    io.MouseDown [button] = true;
    //return 0;
  }
#endif
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  case WM_XBUTTONUP:
  {
    int button = 0;
    if (msg == WM_LBUTTONUP) { button = 0; }
    if (msg == WM_RBUTTONUP) { button = 1; }
    if (msg == WM_MBUTTONUP) { button = 2; }
    if (msg == WM_XBUTTONUP) { button = ( GET_XBUTTON_WPARAM (wParam) == XBUTTON1 ) ? 3 : 4; }
    io.MouseDown [button] = false;
    if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == hwnd)
      ::ReleaseCapture ( );
    return 0;
  }
  // This is needed to open ImGui menus on right clicks in "non-client" areas (aka draggable areas)
  case WM_NCRBUTTONUP:
  {
    int button = 1;
    io.MouseDown [button] = false;
    if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == hwnd)
      ::ReleaseCapture ( );
    return 0;
  }
#if 0
  // This is not necessary since we convert HTCLIENT to HTCAPTION,
  //   so all non-client mouse input is handled by the OS itself
  case WM_NCLBUTTONUP:
  case WM_NCRBUTTONUP:
  case WM_NCMBUTTONUP:
  case WM_NCXBUTTONUP:
  {
    int button = 0;
    if (msg == WM_NCLBUTTONUP) { button = 0; }
    if (msg == WM_NCRBUTTONUP) { button = 1; }
    if (msg == WM_NCMBUTTONUP) { button = 2; }
    if (msg == WM_NCXBUTTONUP) { button = ( GET_XBUTTON_WPARAM (wParam) == XBUTTON1 ) ? 3 : 4; }
    io.MouseDown [button] = false;
    //if (!ImGui::IsAnyMouseDown ( ) && ::GetCapture ( ) == hwnd)
    //  ::ReleaseCapture ( );
    return 0;
  }
#endif
  case WM_MOUSEWHEEL:
    io.MouseWheel += (float)GET_WHEEL_DELTA_WPARAM (wParam) / (float)WHEEL_DELTA;
    return 0;
  case WM_MOUSEHWHEEL:
    io.MouseWheelH += (float)GET_WHEEL_DELTA_WPARAM (wParam) / (float)WHEEL_DELTA;
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (wParam < 256 && g_Focused)
      io.KeysDown [wParam] = 1;
    return 0;
  case WM_KEYUP:
  case WM_SYSKEYUP:
    if (wParam < 256 && g_Focused)
      io.KeysDown [wParam] = 0;
    return 0;
  case WM_CHAR:
      // You can also use ToAscii()+GetKeyboardState() to retrieve characters.
    io.AddInputCharacter ((unsigned int)wParam);
    return 0;
  case WM_SETCURSOR:
    if (LOWORD (lParam) == HTCLIENT && ImGui_ImplWin32_UpdateMouseCursor ( ))
      return 1;
    return 0;
  case WM_DEVICECHANGE:
    switch (wParam)
    {
      case DBT_DEVICEARRIVAL:
      case DBT_DEVICEREMOVECOMPLETE:
      {
        DEV_BROADCAST_HDR* pDevHdr =
          (DEV_BROADCAST_HDR *)lParam;
        if (pDevHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
        {
          DEV_BROADCAST_DEVICEINTERFACE_W *pDev =
            (DEV_BROADCAST_DEVICEINTERFACE_W *)pDevHdr;

          static constexpr GUID GUID_DEVINTERFACE_HID =
            { 0x4D1E55B2L, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

          if (IsEqualGUID (pDev->dbcc_classguid, GUID_DEVINTERFACE_HID))
          {
            g_WantUpdateHasGamepad = true;
          }
        }
      } break;
    }

    return 0;
  case WM_DISPLAYCHANGE:
    g_WantUpdateMonitors = true;
    return 0;
  }
  return 0;
}

//--------------------------------------------------------------------------------------------------------
// DPI handling
// Those in theory should be simple calls but Windows has multiple ways to handle DPI, and most of them
// require recent Windows versions at runtime or recent Windows SDK at compile-time. Neither we want to depend on.
// So we dynamically select and load those functions to avoid dependencies. This is the scheme successfully
// used by GLFW (from which we borrowed some of the code here) and other applications aiming to be portable.
//---------------------------------------------------------------------------------------------------------
// At this point ImGui_ImplWin32_EnableDpiAwareness() is just a helper called by main.cpp, we don't call it automatically.
//---------------------------------------------------------------------------------------------------------

#ifndef DPI_ENUMS_DECLARED
typedef enum {
  PROCESS_DPI_UNAWARE           = 0,
  PROCESS_SYSTEM_DPI_AWARE      = 1,
  PROCESS_PER_MONITOR_DPI_AWARE = 2
} PROCESS_DPI_AWARENESS;
typedef enum {
  MDT_EFFECTIVE_DPI = 0,
  MDT_ANGULAR_DPI   = 1,
  MDT_RAW_DPI       = 2,
  MDT_DEFAULT       = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
#endif
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE (DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    (DPI_AWARENESS_CONTEXT)-3
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (DPI_AWARENESS_CONTEXT)-4
#endif
typedef HRESULT               (WINAPI *PFN_SetProcessDpiAwareness)      ( PROCESS_DPI_AWARENESS );                     // Shcore.lib+dll, Windows 8.1
typedef HRESULT               (WINAPI *PFN_GetDpiForMonitor)            ( HMONITOR, MONITOR_DPI_TYPE, UINT *, UINT * );        // Shcore.lib+dll, Windows 8.1
typedef DPI_AWARENESS_CONTEXT (WINAPI *PFN_SetThreadDpiAwarenessContext)( DPI_AWARENESS_CONTEXT ); // User32.lib+dll, Windows 10 v1607 (Creators Update)

float
ImGui_ImplWin32_GetDpiScaleForMonitor (void *monitor)
{
  UINT xdpi = 96,
       ydpi = 96;

  if (SKIF_Util_IsWindows8Point1OrGreater ( ))
  {
    static HINSTANCE    shcore_dll =
      ::LoadLibraryW (L"shcore.dll"); // Reference counted per-process

    static PFN_GetDpiForMonitor
               GetDpiForMonitorFn =
          (PFN_GetDpiForMonitor)::GetProcAddress (shcore_dll,
              "GetDpiForMonitor");

    if (GetDpiForMonitorFn != nullptr)
    {   GetDpiForMonitorFn ((HMONITOR)monitor, MDT_EFFECTIVE_DPI, &xdpi,
                                                                  &ydpi); }
  }

  else
  {
    /*
    using  GetDeviceCaps_pfn = int (WINAPI *)(HDC,int);
    static GetDeviceCaps_pfn
          SKIF_GetDeviceCaps = (GetDeviceCaps_pfn)GetProcAddress (
                LoadLibraryEx ( L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
              "GetDeviceCaps"                                    );
    */

    const HDC dc = ::GetDC         (nullptr);
            xdpi = ::GetDeviceCaps (dc, LOGPIXELSX);
            ydpi = ::GetDeviceCaps (dc, LOGPIXELSY);
                   ::ReleaseDC     (nullptr, dc);
  }

  IM_ASSERT (xdpi == ydpi); // Please contact me if you hit this assert!
  return     xdpi / 96.0f;
}

float
ImGui_ImplWin32_GetDpiScaleForHwnd (void *hwnd)
{
  HMONITOR monitor =
    ::MonitorFromWindow ((HWND)hwnd, MONITOR_DEFAULTTONEAREST);

  return
    ImGui_ImplWin32_GetDpiScaleForMonitor (monitor);
}

void
ImGui_ImplWin32_SetDWMBorders (void* hwnd)
{
  if (! hwnd)
    return;
  
  if (! SKIF_Util_IsWindows11orGreater ( ))
    return;

  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  
  if (! _registry.bWin11Corners)
    return;

  DWM_WINDOW_CORNER_PREFERENCE
           dwmCornerPreference = DWMWCP_DEFAULT;
  COLORREF dwmBorderColor      = DWMWA_COLOR_DEFAULT; // DWMWA_COLOR_NONE
  BOOL     dwmUseDarkMode      = true;
  ImVec4   imguiBorderColor    = ImGui::GetStyleColorVec4 (ImGuiCol_Border);
        
  dwmBorderColor = RGB ((255 * imguiBorderColor.x),
                        (255 * imguiBorderColor.y),
                        (255 * imguiBorderColor.z));
  
  extern HWND
      SKIF_ImGui_hWnd;
  if (SKIF_ImGui_hWnd ==       NULL ||
      SKIF_ImGui_hWnd == (HWND)hwnd)
    dwmCornerPreference = DWMWCP_ROUND;      // Main window
  else
    dwmCornerPreference = DWMWCP_ROUNDSMALL; // Popups (spanning outside of the main window)

  ::DwmSetWindowAttribute ((HWND)hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &dwmCornerPreference, sizeof (dwmCornerPreference));
  ::DwmSetWindowAttribute ((HWND)hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,  &dwmUseDarkMode,      sizeof (dwmUseDarkMode));
  ::DwmSetWindowAttribute ((HWND)hwnd, DWMWA_BORDER_COLOR,             &dwmBorderColor,      sizeof (dwmBorderColor));
}

void
ImGui_ImplWin32_UpdateDWMBorders (void)
{
  if (! SKIF_Util_IsWindows11orGreater ( ))
    return;

  ImGuiPlatformIO& platform_io =
    ImGui::GetPlatformIO ();

  //// Skip the main viewport (index 0), which is always fully handled by the application!
  for (int i = 1; i < platform_io.Viewports.Size; i++)
  {
    ImGuiViewport* viewport =
       platform_io.Viewports [i];

    if (viewport->PlatformHandleRaw != NULL)
      ImGui_ImplWin32_SetDWMBorders (viewport->PlatformHandleRaw);
  }
}

#undef  __GNUC__
#undef  IMGUI_DISABLE_WIN32_FUNCTIONS
#undef  IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS

//--------------------------------------------------------------------------------------------------------
// IME (Input Method Editor) basic support for e.g. Asian language users
//--------------------------------------------------------------------------------------------------------

#if defined(_WIN32) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS) && !defined(IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS) && !defined(__GNUC__)
#define HAS_WIN32_IME   1
#include <imm.h>
#ifdef _MSC_VER
#pragma comment(lib, "imm32")
#endif
static void
ImGui_ImplWin32_SetImeInputPos (ImGuiViewport *viewport, ImVec2 pos)
{
  COMPOSITIONFORM cf = {
    CFS_FORCE_POSITION, { (LONG)( pos.x - viewport->Pos.x ),
                          (LONG)( pos.y - viewport->Pos.y ) },
                        { 0, 0,
                          0, 0 }
  };

  if (HWND hwnd = (HWND)viewport->PlatformHandle)
  {
    if (HIMC himc = ::ImmGetContext (hwnd))
    {
      ::ImmSetCompositionWindow (himc, &cf);
      ::ImmReleaseContext (hwnd, himc);
    }
  }
}
#else
#define HAS_WIN32_IME   0
#endif

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the back-end to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

struct ImGuiViewportDataWin32 {
  HWND  Hwnd;
  DWORD DwStyle;
  DWORD DwExStyle;
  bool  HwndOwned;
  bool  RemovedDWMBorders;

  ImGuiViewportDataWin32 (void)
  {
    Hwnd      = nullptr;
    HwndOwned = false;
    DwStyle   =
    DwExStyle = 0;
    RemovedDWMBorders = false;
  }

  ~ImGuiViewportDataWin32 (void) { IM_ASSERT (Hwnd == NULL); }
};

static void
ImGui_ImplWin32_GetWin32StyleFromViewportFlags (
  ImGuiViewportFlags  flags,
               DWORD *out_style,
               DWORD *out_ex_style,
               HWND   owner         // Used to tell if we're dealing with the main platform window or a child window
)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  if (flags & ImGuiViewportFlags_NoDecoration)
    *out_style = WS_POPUP;   // Popups / Tooltips        (alternate look: WS_POPUPWINDOW, or WS_POPUP | WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX)
  else {
    *out_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX; // Main Window (WS_OVERLAPPEDWINDOW)
    
    // Only enable the maximized box if DragFromMaximize is available in Windows
    if (_registry.bMaximizeOnDoubleClick && SKIF_Util_GetDragFromMaximized ( ))
      *out_style |= WS_MAXIMIZEBOX;
  }

  // WS_OVERLAPPEDWINDOW == WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
  // - WS_OVERLAPPED  - The window is an overlapped window          // not really required by SKIF
  // - WS_CAPTION     - The window has a title bar                  // not really required by SKIF
  // - WS_SYSMENU     - Window menu           (WndMenu -> Move      // alt+space, right click window on taskbar
  // - WS_THICKFRAME  - Resize grip on window (WndMenu -> Size)     // enables WinKey+Left/Right snapping
  // - WS_MINIMIZEBOX - Minimize button       (WndMenu -> Minimize) // enables WinKey+Down minimize shortcut
  // - WS_MAXIMIZEBOX - Maximize button       (WndMenu -> Maximize) // enables WinKey+Up maximize shortcut

  if (flags & ImGuiViewportFlags_NoTaskBarIcon)
    *out_ex_style  = WS_EX_TOOLWINDOW; // Popups / Tooltips
  else
    *out_ex_style  = WS_EX_APPWINDOW;  // Main Window

  if (flags & ImGuiViewportFlags_TopMost)
    *out_ex_style |= WS_EX_TOPMOST;

  // This flag is Windows 8+, and only applicable to flip swapchains
  // Using this flag breaks GDI based swapchains (BitBlt Discard, DXVK, OpenGL, Vulkan, etc)
  if (SKIF_Util_IsWindows8Point1OrGreater ( ) && _registry.iUIMode > 0)
    *out_ex_style |= WS_EX_NOREDIRECTIONBITMAP;

  // Main platform window must respect nCmdShow and add
  // WS_EX_NOACTIVATE if we start in a minimized state
  extern int SKIF_nCmdShow;
  if (owner == nullptr && SKIF_nCmdShow != -1)
  {
    if (SKIF_nCmdShow == SW_SHOWMINIMIZED   ||
        SKIF_nCmdShow == SW_SHOWMINNOACTIVE ||
        SKIF_nCmdShow == SW_SHOWNOACTIVATE  ||
        SKIF_nCmdShow == SW_SHOWNA          ||
        SKIF_nCmdShow == SW_HIDE)
      *out_ex_style |= WS_EX_NOACTIVATE;
  }
}

// This is called for all viewports that gets created
static void
ImGui_ImplWin32_CreateWindow (ImGuiViewport *viewport)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  extern HWND  SKIF_ImGui_hWnd;
  extern float SKIF_ImGui_GlobalDPIScale;

  ImGuiViewportDataWin32 *data =
    IM_NEW (ImGuiViewportDataWin32)();

  viewport->PlatformUserData = data;

  // Select owner window
  HWND owner_window = nullptr;
  if (viewport->ParentViewportId != 0)
    if (ImGuiViewport *parent_viewport = ImGui::FindViewportByID (viewport->ParentViewportId))
      owner_window = (HWND)parent_viewport->PlatformHandle;

  // Create window
  RECT rect =
  { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
    (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
  };
  
  // Select style
  ImGui_ImplWin32_GetWin32StyleFromViewportFlags (viewport->Flags, &data->DwStyle, &data->DwExStyle, owner_window);

  //::AdjustWindowRectEx ( &rect, data->DwStyle,
  //                       FALSE, data->DwExStyle );

  data->Hwnd =
    ::CreateWindowEx (
      data->DwExStyle,
      SKIF_ImGui_WindowClass,
      SKIF_ImGui_WindowTitle,

      data->DwStyle, // Style, class name, window name

                   rect.left,               rect.top,
      rect.right - rect.left, rect.bottom - rect.top, // Window area

      owner_window,     nullptr,
      ::GetModuleHandle (nullptr), nullptr
    ); // Parent window, Menu, Instance, Param

  // Stuff to do for the overarching ImGui Platform window (main window; meaning there is no parent)
  if (owner_window == nullptr && SKIF_ImGui_hWnd == NULL) // ! IsWindow (SKIF_ImGui_hWnd)
  {
    // Store the handle globally
    SKIF_ImGui_hWnd = data->Hwnd;

    // Retrieve the DPI scaling of the current display
    SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? ImGui_ImplWin32_GetDpiScaleForHwnd (data->Hwnd) : 1.0f;

    // Update the style scaling to reflect the current DPI scaling
    ImGuiStyle              newStyle;
    extern void
      SKIF_ImGui_SetStyle (ImGuiStyle * dst = nullptr);
      SKIF_ImGui_SetStyle (&newStyle);
  }

  data->HwndOwned                 = true;
  viewport->PlatformRequestResize = false;
  viewport->PlatformHandle        = viewport->PlatformHandleRaw = data->Hwnd;

  // Add icons to the window
  extern HICON hIcon;
#define GCL_HICON (-14)

  SendMessage      (data->Hwnd, WM_SETICON, ICON_BIG,        (LPARAM)hIcon);
  SendMessage      (data->Hwnd, WM_SETICON, ICON_SMALL,      (LPARAM)hIcon);
  SendMessage      (data->Hwnd, WM_SETICON, ICON_SMALL2,     (LPARAM)hIcon);
  SetClassLongPtrW (data->Hwnd, GCL_HICON,         (LONG_PTR)(LPARAM)hIcon);
}

static void ImGui_ImplWin32_DestroyWindow (ImGuiViewport *viewport)
{
  if (ImGuiViewportDataWin32 *data = (ImGuiViewportDataWin32 *)viewport->PlatformUserData)
  {
    if (::GetCapture () == data->Hwnd)
    {
        // Transfer capture so if we started dragging from a window that later disappears, we'll still receive the MOUSEUP event.
      ::ReleaseCapture (      );
      //::SetCapture     (g_hWnd);
    }
    if (data->Hwnd &&  data->HwndOwned)
      ::DestroyWindow (data->Hwnd);

    // If this is the main platform window, reset the global handle for it
    extern HWND SKIF_ImGui_hWnd;
    if (data->Hwnd == SKIF_ImGui_hWnd)
      SKIF_ImGui_hWnd = NULL;

    data->Hwnd = nullptr;

    IM_DELETE (data);
  }

  viewport->PlatformUserData =
    viewport->PlatformHandle = nullptr;
}

static void
ImGui_ImplWin32_ShowWindow(ImGuiViewport* viewport)
{
  ImGuiViewportDataWin32* data =
    (ImGuiViewportDataWin32*)viewport->PlatformUserData;

  IM_ASSERT(data->Hwnd != 0);

  static bool
       runOnce = true;
  if ( runOnce )
  {    runOnce = false;
    // Main platform window must respect nCmdShow
    //  in the first ShowWindow() call
    extern int SKIF_nCmdShow;
    ::ShowWindow (data->Hwnd, SKIF_nCmdShow);
    SKIF_nCmdShow = -1; // SKIF_nCmdShow has served its purpose by now
  }

  else if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
    ::ShowWindow (data->Hwnd, SW_SHOWNA);

  else
    ::ShowWindow (data->Hwnd, (g_Focused) ? SW_SHOW : SW_SHOWNA);

}

static void
ImGui_ImplWin32_UpdateWindow (ImGuiViewport *viewport)
{
    // (Optional) Update Win32 style if it changed _after_ creation.
    // Generally they won't change unless configuration flags are changed, but advanced uses (such as manually rewriting viewport flags) make this useful.
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  DWORD new_style;
  DWORD new_ex_style;
  
  // Select owner window
  HWND owner_window = nullptr;
  if (viewport->ParentViewportId != 0)
    if (ImGuiViewport *parent_viewport = ImGui::FindViewportByID (viewport->ParentViewportId))
      owner_window = (HWND)parent_viewport->PlatformHandle;

  ImGui_ImplWin32_GetWin32StyleFromViewportFlags (viewport->Flags, &new_style, &new_ex_style, owner_window);

  static bool hasNoRedirectionBitmap = (bool)(data->DwExStyle & WS_EX_NOREDIRECTIONBITMAP);

  // Only reapply the flags that have been changed from our point of view (as other flags are being modified by Windows)
  if ( data->DwStyle   != new_style ||
       data->DwExStyle != new_ex_style )
  {
    data->DwStyle   = new_style;
    data->DwExStyle = new_ex_style;

    ::SetWindowLongPtrW ( data->Hwnd, GWL_STYLE,   data->DwStyle  );
    ::SetWindowLongPtrW ( data->Hwnd, GWL_EXSTYLE, data->DwExStyle);

    // Force recreating the window if the NoRedirectionBitmap flag has changed
    if (hasNoRedirectionBitmap != (bool)(data->DwExStyle & WS_EX_NOREDIRECTIONBITMAP))
    {   hasNoRedirectionBitmap  = (bool)(data->DwExStyle & WS_EX_NOREDIRECTIONBITMAP);
      ImGuiViewportP* viewportP =
        static_cast <ImGuiViewportP*> (
                          viewport
        );
      viewportP->LastFrameActive = 0;
    }

    RECT rect =
    { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
      (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
    };

    //::AdjustWindowRectEx ( &rect, data->DwStyle,
    //                       FALSE, data->DwExStyle ); // Client to Screen

    ::SetWindowPos       ( data->Hwnd, nullptr,
                                          rect.left,               rect.top,
                             rect.right - rect.left, rect.bottom - rect.top,
                               SWP_NOZORDER     | SWP_NOACTIVATE |
                               SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS );

    // A ShowWindow() call is necessary when we alter the style
    ::ShowWindow ( data->Hwnd, SW_SHOWNA );

    viewport->PlatformRequestMove =
      viewport->PlatformRequestResize = true;
  }
  
  // Run only once per window -- to remove the Standard Frame of DWM windows
  ///* 2023-07-31: Not needed any longer as its handled in WM_CREATE instead
  //   CORRECTION: Still needed
  else if ((viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0 &&
          ! data->RemovedDWMBorders)
  {
    data->RemovedDWMBorders = true;

    RECT rect =
    { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
      (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
    };

    ::SetWindowPos       ( data->Hwnd, nullptr,
                                          rect.left,               rect.top,
                             rect.right - rect.left, rect.bottom - rect.top,
                               SWP_FRAMECHANGED );
  }
  //*/
}

static ImVec2
ImGui_ImplWin32_GetWindowPos (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  POINT                          pos = { 0, 0 };
  ::ClientToScreen (data->Hwnd, &pos);

  return
    ImVec2 ((float)pos.x, (float)pos.y);
}

static void
ImGui_ImplWin32_SetWindowPos ( ImGuiViewport *viewport,
                               ImVec2         pos )
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  RECT rect = {
    (LONG)pos.x, (LONG)pos.y,
    (LONG)pos.x, (LONG)pos.y
  };

  //::AdjustWindowRectEx ( &rect, data->DwStyle,
  //                       FALSE, data->DwExStyle );

  ::SetWindowPos ( data->Hwnd, nullptr,
                     rect.left, rect.top,
                     0,                0,
                       SWP_NOZORDER       | SWP_NOSIZE   |
                       SWP_NOACTIVATE     | SWP_ASYNCWINDOWPOS );
}

static ImVec2
ImGui_ImplWin32_GetWindowSize (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  RECT                          rect = { };
  ::GetClientRect (data->Hwnd, &rect);

  return
    ImVec2 (float (rect.right  - rect.left),
            float (rect.bottom - rect.top));
}

static void
ImGui_ImplWin32_SetWindowSize ( ImGuiViewport *viewport,
                                ImVec2         size )
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  RECT rect = {
    0, 0, (LONG)size.x, (LONG)size.y
  };

  //::AdjustWindowRectEx ( &rect, data->DwStyle,
  //                       FALSE, data->DwExStyle ); // Client to Screen

  ::SetWindowPos ( data->Hwnd, nullptr,
                   0,                    0,
                   rect.right  - rect.left,
                   rect.bottom - rect.top,
                     SWP_NOZORDER   | SWP_NOMOVE   |
                     SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS );
}

static void
ImGui_ImplWin32_SetWindowFocus (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  ::BringWindowToTop    (data->Hwnd);
  ::SetForegroundWindow (data->Hwnd);
  ::SetFocus            (data->Hwnd);
}

static bool
ImGui_ImplWin32_GetWindowFocus (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  return
    ::GetForegroundWindow () == data->Hwnd;
}

static bool
ImGui_ImplWin32_GetWindowMinimized (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  if (data == nullptr)
    return false;

  IM_ASSERT (data->Hwnd != 0);

  HWND hWndSelfOrOwner =
    GetWindow (data->Hwnd, GW_OWNER);

  if (hWndSelfOrOwner == 0)
    hWndSelfOrOwner = data->Hwnd;
  else
    IM_ASSERT (data->HwndOwned);

  return
    ::IsIconic (hWndSelfOrOwner) != 0;
}

static void
ImGui_ImplWin32_SetWindowTitle ( ImGuiViewport *viewport,
                                 const char    *title )
{
  if (strlen(title) == 0)
    return;

  // ::SetWindowTextA() doesn't properly handle UTF-8 so we explicitely convert our string.
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  int n =
    ::MultiByteToWideChar (CP_UTF8, 0, title, -1, NULL, 0);

  ImVector <wchar_t> title_w;
                     title_w.resize (n);

  ::MultiByteToWideChar (CP_UTF8, 0, title, -1, title_w.Data, n);

  ::SetWindowTextW (data->Hwnd, title_w.Data);
}

static void
ImGui_ImplWin32_SetWindowAlpha (ImGuiViewport *viewport, float alpha)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);
  IM_ASSERT (alpha >= 0.0f && alpha <= 1.0f);

  if (alpha < 1.0f)
  {
    DWORD_PTR style =
      ::GetWindowLongPtrW (data->Hwnd, GWL_EXSTYLE) | WS_EX_LAYERED;
    ::SetWindowLongPtrW   (data->Hwnd, GWL_EXSTYLE, style);

    ::SetLayeredWindowAttributes (
                           data->Hwnd, 0,
                (BYTE)( 255 * alpha ), LWA_ALPHA );
  }

  else
  {
    DWORD_PTR style =
      ::GetWindowLongPtrW (data->Hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED;
    ::SetWindowLongPtrW   (data->Hwnd, GWL_EXSTYLE, style);
  }
}

static float
ImGui_ImplWin32_GetWindowDpiScale (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  return
    ImGui_ImplWin32_GetDpiScaleForHwnd (data->Hwnd);
}



// FIXME-DPI: Testing DPI related ideas
static void ImGui_ImplWin32_OnChangedViewport (ImGuiViewport *viewport)
{
  //ImGuiViewportDataWin32 *data =
  //  (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  UNREFERENCED_PARAMETER (viewport);
  //(void)viewport;

  //if (viewport->Size.x == 0 ||
  //    viewport->Size.y == 0)
  //  return;

  /*
  OutputDebugString(L"viewport->Size.x: ");
  OutputDebugString(std::to_wstring(viewport->Size.x).c_str());
  OutputDebugString(L"\n");

  OutputDebugString(L"viewport->Size.y: ");
  OutputDebugString(std::to_wstring(viewport->Size.y).c_str());
  OutputDebugString(L"\n");
  */

  // Update the style scaling
  /*
  ImGuiStyle              newStyle;
  extern float
    SKIF_ImGui_GlobalDPIScale;
    SKIF_ImGui_GlobalDPIScale =
           viewport->DpiScale;

  extern void
    SKIF_ImGui_SetStyle (ImGuiStyle * dst = nullptr);
    SKIF_ImGui_SetStyle (&newStyle);
  */

  /*
  OutputDebugString(L"SKIF_ImGui_GlobalDPIScale: ");
  OutputDebugString(std::to_wstring(SKIF_ImGui_GlobalDPIScale).c_str());
  OutputDebugString(L"\n");
  */



  //extern bool       SKIF_bDPIScaling;
  //extern ImGuiStyle SKIF_ImGui_DefaultStyle;

  /* Disabled alongside move from ImGuiConfigFlags_DpiEnableScaleViewports
  *    over to ImGuiConfigFlags_DpiEnableScaleFonts

  ImGuiStyle default_style =
    SKIF_ImGui_DefaultStyle;

  ImGuiStyle &style =
    ImGui::GetStyle ();

  if (SKIF_bDPIScaling)
  {
    //default_style.WindowPadding    = ImVec2(0, 0);
    //default_style.WindowBorderSize = 0.0f;
    //default_style.ItemSpacing.y    = 3.0f;
    //default_style.FramePadding     = ImVec2(0, 0);

    default_style.ScaleAllSizes (viewport->DpiScale);

    ImGui::GetIO ().FontGlobalScale = viewport->DpiScale;
  }

  else
    ImGui::GetIO ().FontGlobalScale = 1.0f;


  static float fLastDPI = 0.0f;

  if (fLastDPI != ImGui::GetIO ().FontGlobalScale)
  {
    // Reset minimum sizing hints
    extern float sk_global_ctl_x;
                 sk_global_ctl_x = 0.0f;

    fLastDPI = ImGui::GetIO ().FontGlobalScale;
  }


  style = default_style;
  */
}

static
LRESULT
CALLBACK
ImGui_ImplWin32_WndProcHandler_PlatformWindow (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // This is the message procedure for the main ImGui Platform window as well as
  //   any additional viewport windows (menus/tooltips that stretches beyond SKIF_ImGui_hWnd).

  extern HWND SKIF_ImGui_hWnd;
  extern HWND SKIF_Notify_hWnd;
  extern float SKIF_ImGui_GlobalDPIScale;
  extern ImVec2 SKIF_vecAppModeDefault;  // Does not include the status bar
  extern ImVec2 SKIF_vecAppModeAdjusted; // Adjusted for status bar and tooltips
  extern ImVec2 SKIF_vecAlteredSize;
  extern ImVec2 SKIF_vecSvcModeDefault;
  //extern bool KeyWinKey;
  //extern int  SnapKeys; // 2 = Left, 4 = Up, 8 = Right, 16 = Down
  
  //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow] Message spotted: 0x" + std::format(L"{:x}", msg)    + L" (" + std::to_wstring(msg)    + L")" + (msg == WM_SETFOCUS ? L" == WM_SETFOCUS" : msg == WM_KILLFOCUS ? L" == WM_KILLFOCUS" : L"") + L"\n").c_str());
  //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L")" + ((HWND)wParam == NULL ? L" == NULL" : (HWND)wParam == SKIF_hWnd ? L" == SKIF_hWnd" : (HWND)wParam == SKIF_ImGui_hWnd ? L" == SKIF_ImGui_hWnd" : (HWND)wParam == SKIF_Notify_hWnd ? L" == SKIF_Notify_hWnd" : L"") + L"\n").c_str());
  //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow] Message spotted: 0x" + std::format(L"{:x}", msg)    + L" (" + std::to_wstring(msg)    + L")\n").c_str());
  //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L")\n").c_str());
  //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L") " + ((HWND)wParam == SKIF_hWnd ? L"== SKIF_hWnd" : ((HWND)wParam == SKIF_ImGui_hWnd ? L"== SKIF_ImGui_hWnd" : (HWND)wParam == SKIF_Notify_hWnd ? L"== SKIF_Notify_hWnd" : L"")) + L"\n").c_str());
  //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow]          lParam: 0x" + std::format(L"{:x}", lParam) + L" (" + std::to_wstring(lParam) + L")\n").c_str());

  /*
  if (msg != WM_NULL        && 
      msg != WM_NCHITTEST   &&
      msg != WM_MOUSEFIRST  &&
      msg != WM_MOUSEMOVE
    )
  {
    //OutputDebugString((L"[ImGui_ImplWin32_WndProcHandler_PlatformWindow] Message spotted: " + std::to_wstring(msg) + L" w wParam: " + std::to_wstring(wParam) + L"\n").c_str());
  }
  */

  if (ImGui_ImplWin32_WndProcHandler (hWnd, msg, wParam, lParam))
    return true;
  
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // WM_NCCALCSIZE allows us to remove the Standard Frame of the window that DWM creates.
  // This is necessary as our main window requires WS_CAPTION | WS_SYSMENU and
  //   a bunch of other window styles to enable modern built-in features such as
  //   window moving, resizing, WinKey+Arrows, animations, etc, but we do not
  //   want the window border to actually appear around our window.
  // 
  // See https://learn.microsoft.com/en-us/windows/win32/dwm/customframe#removing-the-standard-frame
  // 
  // P.S: Requires the window to be resized afterwards, which is handled through
  //        the RemovedDWMBorders boolean.

  //static bool restore_from_maximized = false;

  switch (msg)
  {
    case WM_SYSCOMMAND:
    {
      //OutputDebugString(L"WM_SYSCOMMAND\n");

      // Disable the window menu (Alt+Space, etc) when SKIF is focused
      if (wParam == SC_KEYMENU && g_Focused) // && (lParam == 0x00 || lParam == 0x20))
        return true;

      break;
    }

    case WM_NCCALCSIZE:
      // Removes the Standard Frame of DWM windows
      if (wParam == TRUE)
        return 0;
      break;

    // For some reason this causes issues with focus not being properly gained on launch
    case WM_CREATE:
    {
      ImGui_ImplWin32_SetDWMBorders (hWnd);

#if 0
      // Change the application window rect
      RECT rcClient;
      ::GetWindowRect (hWnd, &rcClient);

      // Inform the application of the frame change.
      ::SetWindowPos  (hWnd, 
                      NULL, 
                           rcClient.left,                   rcClient.top,
          rcClient.right - rcClient.left, rcClient.bottom - rcClient.top,
                      SWP_FRAMECHANGED );
      // SWP_NOACTIVATE seems to be needed to fix the focus issue on launch but causes issues 
      //   with, well... the app window _not_ being focused when it's being created...
      
      // Apply the styles
      LPCREATESTRUCT cs =
        (LPCREATESTRUCT) lParam;

      ::SetWindowLongPtrW (hWnd, GWL_STYLE,   cs->style    );
      ::SetWindowLongPtrW (hWnd, GWL_EXSTYLE, cs->dwExStyle);

      return 0;
#endif

      break;
    }
  }



#define SKIF_MAXIMIZE_POS 27 // 27
#define SWP_STATECHANGED  0x8000 // Undocumented
  static bool moveModal    = false;

  if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle ((void *)hWnd))
  {
    switch (msg)
    {

#pragma region WM_WINDOWPOSCHANGING

      // Gets fired on window creation, and screws up the positioning
      case WM_WINDOWPOSCHANGING:
      {
        //OutputDebugString(L"WM_WINDOWPOSCHANGING\n");

        // Do not run this if any mouse button is being held down
        if (ImGui::IsAnyMouseDown ( ))
          break;

        // Do not run this within the first 5 frames, as this prevent screwing up restoring the original position of the window
        if (ImGui::GetFrameCount  ( ) < 5)
          break;

        // Do not manipulate the pos/change of the Ctrl+Tab window (not actually used)
        //if (ImGui::FindWindowByName("###NavWindowingList") != nullptr &&
        //    ImGui::FindWindowByName("###NavWindowingList")->Viewport == viewport)
        //  break;
        
        //LRESULT def =  DefWindowProc (hWnd, msg, wParam, lParam);
        WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*> (lParam);
        
        POINT ptLeftTop  = {
          wp->x,
          wp->y
        };
        
        for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
        {
          const ImGuiPlatformMonitor& targetMonitor = ImGui::GetPlatformIO().Monitors[monitor_n];
          
          ImVec2 MaxWorkSize    = ImVec2 (targetMonitor.WorkPos.x + targetMonitor.WorkSize.x,
                                          targetMonitor.WorkPos.y + targetMonitor.WorkSize.y);
          ImRect targetWorkArea = ImRect (targetMonitor.WorkPos, MaxWorkSize);

          // Find the intended display
          if (targetWorkArea.Contains (ImVec2 (static_cast<float> (wp->x), static_cast<float> (wp->y))))
          {
            // If we are being maximized...
            if (wp->flags == (SWP_STATECHANGED | SWP_FRAMECHANGED) &&
                wp->x - targetWorkArea.Min.x == SKIF_MAXIMIZE_POS  &&
                wp->y - targetWorkArea.Min.y == SKIF_MAXIMIZE_POS)
            {
              ImVec2 tmpExpectedSize = ImVec2 (0.0f, 0.0f);

              float targetDPI = (_registry.bDPIScaling) ? targetMonitor.DpiScale : 1.0f;

              if (! _registry.bServiceMode)
              {
                ImVec2 tmpAlteredSize  = ImVec2 (0.0f, 0.0f);
                tmpExpectedSize = SKIF_vecAppModeAdjusted * targetDPI;

                // Needed to account for an altered size on the target display
                if (SKIF_vecAppModeAdjusted.y * targetDPI > targetWorkArea.Max.y)
                  tmpAlteredSize.y = (SKIF_vecAppModeAdjusted.y * targetDPI - targetWorkArea.Max.y);

                tmpExpectedSize.y -= tmpAlteredSize.y;
              }

              else
                tmpExpectedSize = SKIF_vecSvcModeDefault * targetDPI;

              // Change the intended position to the actual center of the display
              wp->x = static_cast<int> (targetWorkArea.GetCenter().x - (tmpExpectedSize.x * 0.5f));
              wp->y = static_cast<int> (targetWorkArea.GetCenter().y - (tmpExpectedSize.y * 0.5f));

              return 0;
            }
          }
        }

        wp->cx = static_cast<int> (viewport->Size.x); // / viewport->DpiScale * targetMonitor.DpiScale
        wp->cy = static_cast<int> (viewport->Size.y); // / viewport->DpiScale * targetMonitor.DpiScale;

        return 0;
        break;
      }

#pragma endregion

      case WM_ENTERSIZEMOVE:
      case WM_EXITSIZEMOVE:
        //OutputDebugString(L"WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE\n");
        moveModal = ! moveModal;
        break;
      
#pragma region WM_DPICHANGED

      case WM_DPICHANGED:
      {
        //OutputDebugString(L"WM_DPICHANGED\n");

        int g_dpi    = HIWORD (wParam);
        RECT* const prcNewWindow = (RECT*) lParam;

        //float SKIF_ImGui_GlobalDPIScale_New;
        //SKIF_ImGui_GlobalDPIScale_New = (float) g_dpi / USER_DEFAULT_SCREEN_DPI;

        ///*
        // Update the style scaling
        SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? (float) g_dpi / USER_DEFAULT_SCREEN_DPI : 1.0f;

        ImGuiStyle              newStyle;
        extern void
          SKIF_ImGui_SetStyle (ImGuiStyle * dst = nullptr);
          SKIF_ImGui_SetStyle (&newStyle);

        extern bool
          invalidateFonts;
          invalidateFonts = true;
        //*/

        // Reset reduced height
        SKIF_vecAlteredSize.y = 0.0f;

        POINT ptLeftTop = {
          prcNewWindow->left,
          prcNewWindow->top
        };
      
        HMONITOR hMonitor =
          ::MonitorFromPoint  (ptLeftTop,    MONITOR_DEFAULTTONEAREST); // Returns the monitor we expect to end up on based on the top left position
        //::MonitorFromRect   (prcNewWindow, MONITOR_DEFAULTTONEAREST); // Returns the monitor we expect to end up on based on the suggested rect
        //::MonitorFromWindow (hWnd,         MONITOR_DEFAULTTONEAREST); // Ends up being the previous monitor as the window haven't been moved yet

        MONITORINFO
          info        = {                  };
          info.cbSize = sizeof (MONITORINFO);

        if (::GetMonitorInfo (hMonitor, &info))
        {
          ImVec2 WorkSize =
            ImVec2 ( (float)( info.rcWork.right  - info.rcWork.left ),
                     (float)( info.rcWork.bottom - info.rcWork.top  ) );

          if (SKIF_vecAppModeAdjusted.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
            SKIF_vecAlteredSize.y = (SKIF_vecAppModeAdjusted.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y)); // (WorkSize.y - 50.0f);

          if (ImGui::IsAnyMouseDown ( ))
            return 0;

          if (moveModal)
            return 0;

          bool reposition = false;

          if (prcNewWindow->left <= info.rcWork.left)
          {
            // Switching to a display on the left
            prcNewWindow->left = info.rcWork.left;
            reposition = true;
          }

          else if (prcNewWindow->right >= info.rcWork.right)
          {
            // Switching to a display on the right
            prcNewWindow->left = info.rcWork.right - static_cast<long> (SKIF_vecAppModeAdjusted.x * SKIF_ImGui_GlobalDPIScale);
            reposition = true;
          }

          if (reposition)
          {
            SetWindowPos (hWnd,
                NULL,
                prcNewWindow->left,
                prcNewWindow->top,
                prcNewWindow->right  - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
          }
        }

        return 0;
        break;
      }

#pragma endregion

#pragma region WM_GETMINMAXINFO

      // This is used to inform Windows of the min/max size of the viewport, so
      //   that features such as Aero Snap takes the enforced size into account
      case WM_GETMINMAXINFO:
      {
        //OutputDebugString(L"WM_GETMINMAXINFO\n");

        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        static POINT sizeMin, sizeMax, pos;

        // For systems with multiple monitors, the ptMaxSize and ptMaxPosition members describe the maximized size and position of the window on the primary monitor,
        // even if the window ultimately maximizes onto a secondary monitor. In that case, the window manager adjusts these values to compensate for differences between
        // the primary monitor and the monitor that displays the window.
        // 
        // ImGui_ImplWin32_UpdateMonitors_EnumFunc() always pushes the primary monitor to the front of ImGui::GetPlatformIO().Monitors

#if 0
        if (ImGui::GetPlatformIO().Monitors.Size > 0)
        {
          const ImGuiPlatformMonitor& primMonitor = ImGui::GetPlatformIO().Monitors[0]; // Primary display
          //ImRect primWorkArea   = ImRect (primMonitor.MainPos, primMonitor.MainPos + primMonitor.WorkSize);

          // Since the window manager adjusts automatically for differences between the primary monitor and the monitor that diplays the window,
          //   we need to undo any active DPI scaling of the current display, and re-apply any active DPI scaling of the primary display.
          
          //sizeMax.x = static_cast<long> (viewport->Size.x / primMonitor.DpiScale);
          //sizeMax.y = static_cast<long> (viewport->Size.y / primMonitor.DpiScale);
        }
#endif

        sizeMax.x = static_cast<long> (viewport->Size.x);
        sizeMax.y = static_cast<long> (viewport->Size.y);
        //sizeMin.x = SKIF_vecAppModeAdjusted.x;
        //sizeMin.y = SKIF_vecAppModeAdjusted.y;

        // To ensure that a "maximized" window is centered on the display, we are
        // using a custom position to detect maximized state later down the line
        pos.x = SKIF_MAXIMIZE_POS;
        pos.y = SKIF_MAXIMIZE_POS;

        // The position of the left side of the maximized window (x member) and the position of the top of the maximized window (y member).
        // For top-level windows, this value is based on the position of the primary monitor.
        mmi->ptMaxPosition  = pos;
           
        // The maximized width (x member) and the maximized height (y member) of the window.
        // For top-level windows, this value is based on the width of the primary monitor.
        mmi->ptMaxSize      = sizeMax; // Maximized size
       
        // Informs Windows of the window sizes, so it doesn't try to resize the window when docking it to the left or right sides
        mmi->ptMinTrackSize = sizeMax; // Minimum tracking size
        mmi->ptMaxTrackSize = sizeMax; // Maximum tracking size

        return 0;

        break;
      }

#pragma endregion

      case WM_CLOSE:
        if (viewport->ParentViewportId == ImGui::GetMainViewport ( )->ID)
          PostQuitMessage (0x0);
        else
          viewport->PlatformRequestClose = true;
        return 0;

      case WM_MOVE:
        viewport->PlatformRequestMove = true;
        break;

      case WM_SIZE:
      {
        //viewport->PlatformRequestResize = true;
        //OutputDebugString(L"WM_SIZE\n");

        extern bool SKIF_D3D11_IsDevicePtr (void);
        extern bool RecreateSwapChains;

        // Might as well trigger a recreation on WM_SIZE when not minimized
        // It is possible this catches device reset/hung scenarios
        if (SKIF_D3D11_IsDevicePtr ( ) && wParam != SIZE_MINIMIZED) // && ImGui::GetFrameCount ( ) > 4
          RecreateSwapChains = true;
        
        // Instead of handling this on the next frame, lets just handle it all there immediately
#if 0
        UINT width  = LOWORD (lParam);
        UINT height = HIWORD (lParam);

        OutputDebugString((L"Size:   " + std::to_wstring(width) + L"x" + std::to_wstring(height) + L"\n").c_str());
        OutputDebugString((L"wParam: " + std::to_wstring(wParam) + L"\n").c_str());

        ImGuiViewportP* viewportP = static_cast <ImGuiViewportP*> (viewport);
        viewport->Size =
          viewportP->LastPlatformSize =
          viewportP->LastRendererSize =
                 ImVec2 (static_cast<float> (width),
                         static_cast<float> (height));
        ImGui::GetPlatformIO ( ).Platform_SetWindowSize (viewport, viewport->Size);
        ImGui::GetPlatformIO ( ).Renderer_SetWindowSize (viewport, viewport->Size); // This ResizeBuffers()
#endif

        break;
      }

      case WM_MOUSEACTIVATE:
        if (viewport->Flags & ImGuiViewportFlags_NoFocusOnClick)
          return MA_NOACTIVATE;
        break;

      case WM_NCHITTEST:
      {
        //OutputDebugString(L"WM_NCHITTEST\n");

        // Let mouse pass-through the window. This will allow the back-end to set io.MouseHoveredViewport properly (which is OPTIONAL).
        // The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
        // If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
        // your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
        if (viewport->Flags & ImGuiViewportFlags_NoInputs)
          return HTTRANSPARENT;

        LRESULT hitTest =
          DefWindowProc (hWnd, msg, wParam, lParam);

        extern bool
            SKIF_ImGui_CanMouseDragMove (void);
        if (SKIF_ImGui_CanMouseDragMove (    ))
        {
          // Necessary to allow OS provided drag-mouse functionality
          if (hitTest == HTCLIENT)
              hitTest  = HTCAPTION;
        }

        // The following are needed for Windows 7 compatibility,
        //   as apparently despite the 0x0 non-client area the
        //     OS still "provides" hidden window buttons.
        if (hitTest == HTCLOSE     ||
            hitTest == HTMAXBUTTON ||
            hitTest == HTMINBUTTON )
            hitTest  = HTCLIENT;

        return
            hitTest;

        break;
      }

      case    WM_NCLBUTTONDBLCLK:
      {
        // Only convert "non-client" double clicks to single clicks
        //   if dragging windows from a maximized state is disabled
        if (! _registry.bMaximizeOnDoubleClick && ! SKIF_Util_GetDragFromMaximized ( ))
          msg = WM_NCLBUTTONDOWN;
        break;
      }
    }
  }

  return DefWindowProc (hWnd, msg, wParam, lParam);
}

static BOOL
CALLBACK
ImGui_ImplWin32_UpdateMonitors_EnumFunc (HMONITOR monitor, HDC, LPRECT, LPARAM)
{
  MONITORINFO
    info        = {                  };
    info.cbSize = sizeof (MONITORINFO);

  if (!::GetMonitorInfo (monitor, &info))
    return TRUE;

  ImGuiPlatformMonitor imgui_monitor = { };

  imgui_monitor.MainPos  =
    ImVec2 ( (float)info.rcMonitor.left,
             (float)info.rcMonitor.top );

  imgui_monitor.MainSize =
    ImVec2 ( (float)( info.rcMonitor.right  - info.rcMonitor.left ),
             (float)( info.rcMonitor.bottom - info.rcMonitor.top  ) );

  imgui_monitor.WorkPos  =
    ImVec2 ( (float)info.rcWork.left,
             (float)info.rcWork.top );

  imgui_monitor.WorkSize =
    ImVec2 ( (float)( info.rcWork.right  - info.rcWork.left ),
             (float)( info.rcWork.bottom - info.rcWork.top  ) );

  imgui_monitor.DpiScale =
    ImGui_ImplWin32_GetDpiScaleForMonitor (monitor);

  ImGuiPlatformIO &io =
    ImGui::GetPlatformIO ();

  if (info.dwFlags & MONITORINFOF_PRIMARY)
    io.Monitors.push_front (imgui_monitor);
  else
    io.Monitors.push_back  (imgui_monitor);

  return TRUE;
}

#include "../resource.h"

static void
ImGui_ImplWin32_UpdateMonitors (void)
{
  ImGui::GetPlatformIO ().Monitors.resize (0);

  ::EnumDisplayMonitors ( nullptr, nullptr,
    ImGui_ImplWin32_UpdateMonitors_EnumFunc,
                    MAKELPARAM (0, 0)
  );

  g_WantUpdateMonitors = false;
}

bool
ImGui_ImplWin32_WantUpdateMonitors (void)
{
  return g_WantUpdateMonitors;
}

ImGuiPlatformMonitor*
ImGui_ImplWin32_GetPlatformMonitorProxy (ImGuiViewport* viewport, bool center)
{
  return ImGui_ImplWin32_GetPlatformMonitor (viewport, center);
}

static ImGuiPlatformMonitor*
ImGui_ImplWin32_GetPlatformMonitor (ImGuiViewport* viewport, bool center)
{
  if (viewport == nullptr)
    return nullptr;

  ImRect viewportRect = ImRect (viewport->Pos, viewport->Pos + viewport->Size);

  for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
  {
    ImGuiPlatformMonitor& monitor   = ImGui::GetPlatformIO().Monitors[monitor_n];
    ImRect monitorRect = ImRect (monitor.MainPos, monitor.MainPos + monitor.MainSize);

    if ((! center && monitorRect.Contains (viewport->Pos)) ||
        (  center && monitorRect.Contains (viewportRect.GetCenter ( ))))
      return &monitor;
  }

  return nullptr;
}

static void
ImGui_ImplWin32_InitPlatformInterface (void)
{
  HMODULE hModHost =
    ::GetModuleHandle (nullptr);

  WNDCLASSEX
   wcex               = {                 };
   wcex.cbSize        = sizeof (WNDCLASSEX); // Real classy sex
   wcex.style         = 0x0;//CS_HREDRAW | CS_VREDRAW;
   wcex.lpfnWndProc   = ImGui_ImplWin32_WndProcHandler_PlatformWindow;
   wcex.cbClsExtra    = 0;
   wcex.cbWndExtra    = 0;
   wcex.hInstance     = hModHost;
   wcex.hCursor       = nullptr;
   wcex.hbrBackground = (HBRUSH)( COLOR_BACKGROUND + 1 );
   wcex.lpszMenuName  = nullptr;
   wcex.lpszClassName = SKIF_ImGui_WindowClass;
   wcex.hIcon         =
     LoadIcon (hModHost, MAKEINTRESOURCE (IDI_SKIF));
   wcex.hIconSm       =
     LoadIcon (hModHost, MAKEINTRESOURCE (IDI_SKIF));

  // Registered Sex Offender
  ::RegisterClassEx (&wcex);

  ImGui_ImplWin32_UpdateMonitors ();

  // Register platform interface (will be coupled with a renderer interface)
  ImGuiPlatformIO &platform_io =
    ImGui::GetPlatformIO ();

  platform_io.Platform_CreateWindow       = ImGui_ImplWin32_CreateWindow;
  platform_io.Platform_DestroyWindow      = ImGui_ImplWin32_DestroyWindow;
  platform_io.Platform_ShowWindow         = ImGui_ImplWin32_ShowWindow;
  platform_io.Platform_SetWindowPos       = ImGui_ImplWin32_SetWindowPos;
  platform_io.Platform_GetWindowPos       = ImGui_ImplWin32_GetWindowPos;
  platform_io.Platform_SetWindowSize      = ImGui_ImplWin32_SetWindowSize;
  platform_io.Platform_GetWindowSize      = ImGui_ImplWin32_GetWindowSize;
  platform_io.Platform_SetWindowFocus     = ImGui_ImplWin32_SetWindowFocus;
  platform_io.Platform_GetWindowFocus     = ImGui_ImplWin32_GetWindowFocus;
  platform_io.Platform_GetWindowMinimized = ImGui_ImplWin32_GetWindowMinimized;
  platform_io.Platform_SetWindowTitle     = ImGui_ImplWin32_SetWindowTitle;
  platform_io.Platform_SetWindowAlpha     = ImGui_ImplWin32_SetWindowAlpha;
  platform_io.Platform_UpdateWindow       = ImGui_ImplWin32_UpdateWindow;
  platform_io.Platform_GetWindowDpiScale  = ImGui_ImplWin32_GetWindowDpiScale; // FIXME-DPI
  platform_io.Platform_OnChangedViewport  = ImGui_ImplWin32_OnChangedViewport; // FIXME-DPI
#if HAS_WIN32_IME
  platform_io.Platform_SetImeInputPos     = ImGui_ImplWin32_SetImeInputPos;
#endif

  // Register main window handle (which is owned by the main application, not by us)
  ImGuiViewport
    *main_viewport = ImGui::GetMainViewport ();
  ImGuiViewportDataWin32
    *data          = IM_NEW (ImGuiViewportDataWin32)();
     data->Hwnd                      = g_hWnd;
     data->HwndOwned                 = false;

     main_viewport->PlatformUserData = data;
     main_viewport->PlatformHandle   = (void *)g_hWnd;
}

static void
ImGui_ImplWin32_ShutdownPlatformInterface (void)
{
  ::UnregisterClass (
    SKIF_ImGui_WindowClass, ::GetModuleHandle (nullptr)
  );
}
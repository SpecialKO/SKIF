// dear imgui: Platform Binding for Windows (standard windows API for 32 and 64 bits applications)
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)

// Implemented features:
//  [X] Platform: Clipboard support (for Win32 this is actually part of core imgui)
//  [X] Platform: Mouse cursor shape and visibility. Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: Keyboard arrays indexed using VK_* Virtual Key Codes, e.g. ImGui::IsKeyPressed(VK_SPACE).
//  [X] Platform: Gamepad support. Enabled with 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad'.
//  [X] Platform: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <Dbt.h>
#include <XInput.h>
#include <tchar.h>
#include <limits>
#include <array>

auto constexpr XUSER_INDEXES =
  std::array <DWORD, 4> { 0, 1, 2, 3 };

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

// Win32 Data
static HWND                 g_hWnd = 0;
static INT64                g_Time = 0;
static bool                 g_Focused = true;
static INT64                g_TicksPerSecond = 0;
static ImGuiMouseCursor     g_LastMouseCursor = ImGuiMouseCursor_COUNT;
static bool                 g_HasGamepad [XUSER_MAX_COUNT] = { false, false, false, false };
struct {
  XINPUT_STATE  last_state = {         };
  LARGE_INTEGER last_qpc   = { 0, 0ULL };
} static                    g_GamepadHistory [XUSER_MAX_COUNT];
static bool                 g_WantUpdateHasGamepad = true;
static bool                 g_WantUpdateMonitors = true;

bool SKIF_ImGui_IsFocused (void)
{
  if (! g_Focused)
    return false;

  static INT64 lastTime  = std::numeric_limits <INT64>::max ();
  static bool  lastFocus = false;

  if (lastTime != g_Time)
  {   lastTime  = g_Time;
    if (HWND focused_hwnd = ::GetForegroundWindow ())
    {
      if (::IsChild (focused_hwnd,  g_hWnd))
      {              focused_hwnd = g_hWnd; }
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
        lastFocus = false;
    }

    lastFocus = true;
  }

  return lastFocus;
}

using XInputGetState_pfn =
DWORD (WINAPI *)( DWORD, XINPUT_STATE * );
using XInputGetCapabilities_pfn =
DWORD (WINAPI *)( DWORD, DWORD, XINPUT_CAPABILITIES * );

static XInputGetState_pfn        ImGui_XInputGetState = nullptr;
static XInputGetCapabilities_pfn ImGui_XInputGetCapabilities = nullptr;
static HMODULE                   g_hModXInput = nullptr;

bool    ImGui_ImplWin32_InitXInput (void *hwnd)
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

  GUID GUID_DEVINTERFACE_HID =
  { 0x4D1E55B2L, 0xF16F, 0x11CF,
         { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

  DEV_BROADCAST_DEVICEINTERFACE_W
    NotificationFilter                 = { };
    NotificationFilter.dbcc_size       = sizeof (DEV_BROADCAST_DEVICEINTERFACE_W);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid  = GUID_DEVINTERFACE_HID;

  static
    HDEVNOTIFY hRegisteredNotification =
      RegisterDeviceNotificationW (
        hwnd, &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
      );

  return true;
}

// Forward Declarations
static void ImGui_ImplWin32_InitPlatformInterface (void);
static void ImGui_ImplWin32_ShutdownPlatformInterface (void);
static void ImGui_ImplWin32_UpdateMonitors (void);

// Functions
bool    ImGui_ImplWin32_Init (void *hwnd)
{
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
  io.BackendPlatformName = "imgui_impl_win32";

//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  // Our mouse update function expect PlatformHandle to be filled for the main viewport
  g_hWnd = (HWND)hwnd;
  ImGuiViewport *main_viewport = ImGui::GetMainViewport ( );
  main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (void *)g_hWnd;

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    ImGui_ImplWin32_InitPlatformInterface ( );

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

  ImGui_ImplWin32_InitXInput (hwnd);

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

  if (HWND focused_hwnd = ::GetForegroundWindow ( ))
  {
    if (::IsChild (focused_hwnd, g_hWnd))
      focused_hwnd = g_hWnd;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
      // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
      // This is the position you can get with GetCursorPos(). In theory adding viewport->Pos is also the reverse operation of doing ScreenToClient().
    ////if (ImGui::FindViewportByPlatformHandle ((void *)focused_hwnd) != NULL)
      {
        io.MousePos =
          ImVec2 ( (float)mouse_screen_pos.x,
                   (float)mouse_screen_pos.y );
      }
    }

    else
    {
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

extern CONDITION_VARIABLE SKIF_IsFocused;

// Gamepad navigation mapping
DWORD ImGui_ImplWin32_UpdateGamepads ( )
{
  ImGuiIO &io =
    ImGui::GetIO ( );

  static constexpr float
    _0 = 0.0f;

  static constexpr
    float _fZeros [] = {
      _0, _0, _0, _0, _0, _0, _0,
      _0, _0, _0, _0, _0, _0, _0,
      _0, _0, _0, _0, _0, _0, _0
  };

  memcpy (io.NavInputs,
            _fZeros,
    sizeof (_fZeros));

  if (! g_Focused)
    return 0;

  if (( io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad ) == 0)
    return 0;

  if (HWND focused_hwnd = ::GetForegroundWindow ())
  {
    if (::IsChild (focused_hwnd,  g_hWnd))
    {              focused_hwnd = g_hWnd; }

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
      return 0;
  }

 // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
 // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
  if (g_WantUpdateHasGamepad && ( ImGui_XInputGetCapabilities != nullptr ))
  {
    XINPUT_CAPABILITIES caps;

    for ( auto idx : XUSER_INDEXES )
    {
      g_HasGamepad [idx] = ( ImGui_XInputGetCapabilities (idx, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS );
    }

    g_WantUpdateHasGamepad = false;
  }

  else if (ImGui_XInputGetCapabilities == nullptr)
  {
    for ( auto idx : XUSER_INDEXES )
    {
      g_HasGamepad [idx] = false;
    }
  }

  XINPUT_STATE xinput_state = { };

  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;

  for ( auto idx : XUSER_INDEXES )
  {
    if ( g_HasGamepad [idx] && ImGui_XInputGetState (idx, &xinput_state) == ERROR_SUCCESS )
    {
      if ( xinput_state.dwPacketNumber != g_GamepadHistory [idx].last_state.dwPacketNumber )
      {
                                  g_GamepadHistory [idx].last_state = xinput_state;
        QueryPerformanceCounter (&g_GamepadHistory [idx].last_qpc);
      }
    }

    else
    {
      g_GamepadHistory [idx].last_qpc.QuadPart = 0;
    }
  }

  struct {
    LARGE_INTEGER qpc  = { 0, 0ULL };
    DWORD         slot =    INFINITE;
  } newest;

  for ( auto idx : XUSER_INDEXES )
  {
    auto qpc =
      g_GamepadHistory [idx].last_qpc.QuadPart;

    if ( qpc > newest.qpc.QuadPart )
    {
      newest.slot         = idx;
      newest.qpc.QuadPart = qpc;
    }
  }

  if (newest.slot != INFINITE)
  {
    xinput_state =
      g_GamepadHistory [newest.slot].last_state;

    const XINPUT_GAMEPAD &gamepad =
      xinput_state.Gamepad;

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

  return     newest.slot != INFINITE ?
         xinput_state.dwPacketNumber : 0;
}

INT64 current_time;
INT64 current_time_ms;

#include <algorithm>
#include <injection.h>

void
ImGui_ImplWin32_NewFrame (void)
{
  ImGuiIO &io =
    ImGui::GetIO ( );

  IM_ASSERT (io.Fonts->IsBuilt ( ) && "Font atlas not built! It is generally built by the renderer back-end. Missing call to renderer _NewFrame() function? e.g. ImGui_ImplOpenGL3_NewFrame().");

  // Setup display size (every frame to accommodate for window resizing)
  RECT                      rect = { };
  ::GetClientRect (g_hWnd, &rect);

  io.DisplaySize =
    ImVec2 ( (float)( rect.right  - rect.left ),
             (float)( rect.bottom - rect.top  ) );

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

  // Read keyboard modifiers inputs
  io.KeyCtrl  = ( ::GetKeyState (VK_CONTROL) & 0x8000 ) != 0;
  io.KeyShift = ( ::GetKeyState (VK_SHIFT)   & 0x8000 ) != 0;
  io.KeyAlt   = ( ::GetKeyState (VK_MENU)    & 0x8000 ) != 0;
  io.KeySuper = false;

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

  // Update game controllers (if enabled and available)
  ImGui_ImplWin32_UpdateGamepads ();
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
  if (ImGui::GetCurrentContext ( ) == NULL)
    return 0;

  ImGuiIO &io = ImGui::GetIO ( );
  switch (msg)
  {

  case WM_CLOSE:
    extern bool bKeepProcessAlive;
    extern bool SKIF_bAllowBackgroundService;
    //extern SKIF_InjectionContext _inject;

    // Handle attempt to close the window
    if (hwnd != nullptr)
    {
      // Handle the service before we exit
      if (_inject.bCurrentState && ! SKIF_bAllowBackgroundService )
        _inject._StartStopInject (true);

      bKeepProcessAlive = false;

      PostMessage (hwnd, WM_QUIT, 0, 0);
      return 1;
    }

    // Handle second attempt to close the window, by defaulting as if the exit prompt was disabled
    /*
    else if (hwnd != nullptr && ! bKeepWindowAlive)
    {
      if (_inject.bCurrentState && ! SKIF_bAllowBackgroundService)
        _inject._StartStopInject (true);

      bKeepProcessAlive = false;
      return 1;
    }
    */
    break;

  case WM_SETFOCUS:
    g_Focused = true;

    extern CONDITION_VARIABLE  SKIF_IsFocused;
    WakeAllConditionVariable (&SKIF_IsFocused);

    return 0;
    break;

  case WM_KILLFOCUS:
    if ((HWND)wParam != g_hWnd && (! IsChild (g_hWnd, (HWND)wParam)))
    {
      g_Focused = false;

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

  case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
  case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
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

///BOOL
///IsWindowsVersionOrGreater (WORD major, WORD minor, WORD sp)
///{
///  OSVERSIONINFOEXW osvi = {
///           sizeof (osvi), major, minor, 0, 0,
///                                      { 0 }, sp
///  };
///
///  DWORD
///    mask = VER_MAJORVERSION | VER_MINORVERSION |
///           VER_SERVICEPACKMAJOR;
///  ULONGLONG
///    cond = VerSetConditionMask (   0, VER_MAJORVERSION,     VER_GREATER_EQUAL);
///    cond = VerSetConditionMask (cond, VER_MINORVERSION,     VER_GREATER_EQUAL);
///    cond = VerSetConditionMask (cond, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
///
///  return
///    VerifyVersionInfoW (&osvi, mask, cond);
///}
///
///
///#define IsWindows8Point1OrGreater()  IsWindowsVersionOrGreater (HIBYTE(0x0602), LOBYTE(0x0602), 0) // _WIN32_WINNT_WINBLUE
///#define IsWindows10OrGreater()       IsWindowsVersionOrGreater (HIBYTE(0x0A00), LOBYTE(0x0A00), 0) // _WIN32_WINNT_WIN10
BOOL
IsWindows8Point1OrGreater (void)
{
  SetLastError (NO_ERROR);

  static BOOL
    bResult =
      GetProcAddress (
        GetModuleHandleW (L"kernel32.dll"),
                           "GetSystemTimePreciseAsFileTime"
                     ) != nullptr &&
      GetLastError  () == NO_ERROR;

  return bResult;
}

BOOL
IsWindows10OrGreater (void)
{
  SetLastError (NO_ERROR);

  static BOOL
  bResult =
    GetProcAddress (
      GetModuleHandleW (L"kernel32.dll"),
                         "SetThreadDescription"
                   ) != nullptr &&
    GetLastError  () == NO_ERROR;

  return bResult;
}

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

void
ImGui_ImplWin32_EnableDpiAwareness (void)
{
  if (IsWindows10OrGreater()) // FIXME-DPI: This needs a manifest to succeed. Instead we try to grab the function pointer.
  {
    static HINSTANCE    user32_dll =
      ::LoadLibraryW (L"user32.dll"); // Reference counted per-process

    if (PFN_SetThreadDpiAwarenessContext SetThreadDpiAwarenessContextFn =
      ( PFN_SetThreadDpiAwarenessContext )::GetProcAddress (user32_dll,
        "SetThreadDpiAwarenessContext")
      )
    {
      SetThreadDpiAwarenessContextFn (
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
      );

      return;
    }
  }

  if (IsWindows8Point1OrGreater ( ))
  {
    static HINSTANCE    shcore_dll =
      ::LoadLibraryW (L"shcore.dll"); // Reference counted per-process

    if (PFN_SetProcessDpiAwareness SetProcessDpiAwarenessFn =
      ( PFN_SetProcessDpiAwareness )::GetProcAddress (shcore_dll,
        "SetProcessDpiAwareness")
      )
    {
      SetProcessDpiAwarenessFn (PROCESS_PER_MONITOR_DPI_AWARE);
    }
  }

  else
  {
    SetProcessDPIAware ( );
  }
}

float
ImGui_ImplWin32_GetDpiScaleForMonitor (void *monitor)
{
  UINT xdpi = 96,
       ydpi = 96;

  if (::IsWindows8Point1OrGreater ( ))
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
    using  GetDeviceCaps_pfn = int (WINAPI *)(HDC,int);
    static GetDeviceCaps_pfn
          SKIF_GetDeviceCaps = (GetDeviceCaps_pfn)GetProcAddress (
                LoadLibraryEx ( L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
              "GetDeviceCaps"                                    );

    const HDC dc =    ::GetDC         (nullptr);
            xdpi = SKIF_GetDeviceCaps (dc, LOGPIXELSX);
            ydpi = SKIF_GetDeviceCaps (dc, LOGPIXELSY);
                   ::ReleaseDC        (nullptr, dc);
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

  ImGuiViewportDataWin32 (void)
  {
    Hwnd      = nullptr;
    HwndOwned = false;
    DwStyle   =
    DwExStyle = 0;
  }
  ~ImGuiViewportDataWin32 (void) { IM_ASSERT (Hwnd == NULL); }
};

static void
ImGui_ImplWin32_GetWin32StyleFromViewportFlags (
  ImGuiViewportFlags  flags,
               DWORD *out_style,
               DWORD *out_ex_style
)
{
  if (flags & ImGuiViewportFlags_NoDecoration)
    *out_style = WS_POPUP;
  else
    *out_style = WS_OVERLAPPEDWINDOW;

  if (flags & ImGuiViewportFlags_NoTaskBarIcon)
    *out_ex_style = WS_EX_TOOLWINDOW;
  else
    *out_ex_style = WS_EX_APPWINDOW;

  if (flags & ImGuiViewportFlags_TopMost)
    *out_ex_style |= WS_EX_TOPMOST;

  #define WS_EX_NOREDIRECTIONBITMAP 0x00200000L

  *out_ex_style |=
    WS_EX_NOREDIRECTIONBITMAP;
}

static void
ImGui_ImplWin32_CreateWindow (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    IM_NEW (ImGuiViewportDataWin32)();

  viewport->PlatformUserData = data;

  // Select style and parent window
  ImGui_ImplWin32_GetWin32StyleFromViewportFlags (
    viewport->Flags, &data->DwStyle,
                     &data->DwExStyle
  );

  HWND parent_window = nullptr;

  if (viewport->ParentViewportId != 0)
    if (ImGuiViewport *parent_viewport = ImGui::FindViewportByID (viewport->ParentViewportId))
      parent_window = (HWND)parent_viewport->PlatformHandle;

// Create window
  RECT rect =
  { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
    (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
  };

  ::AdjustWindowRectEx ( &rect, data->DwStyle,
                         FALSE, data->DwExStyle );

  data->Hwnd =
    ::CreateWindowEx (
      data->DwExStyle,
      _T ("ImGui Platform"),
      _T ("Untitled"),

      data->DwStyle, // Style, class name, window name

                   rect.left,               rect.top,
      rect.right - rect.left, rect.bottom - rect.top, // Window area

      parent_window,     nullptr,
      ::GetModuleHandle (nullptr), nullptr
    ); // Parent window, Menu, Instance, Param

  data->HwndOwned                 = true;
  viewport->PlatformRequestResize = false;
  viewport->PlatformHandle        = viewport->PlatformHandleRaw = data->Hwnd;
}

static void ImGui_ImplWin32_DestroyWindow (ImGuiViewport *viewport)
{
  if (ImGuiViewportDataWin32 *data = (ImGuiViewportDataWin32 *)viewport->PlatformUserData)
  {
    if (::GetCapture () == data->Hwnd)
    {
        // Transfer capture so if we started dragging from a window that later disappears, we'll still receive the MOUSEUP event.
      ::ReleaseCapture (      );
      ::SetCapture     (g_hWnd);
    }
    if (data->Hwnd &&  data->HwndOwned)
      ::DestroyWindow (data->Hwnd);

    data->Hwnd = nullptr;

    IM_DELETE (data);
  }

  viewport->PlatformUserData =
    viewport->PlatformHandle = nullptr;
}

static void
ImGui_ImplWin32_ShowWindow (ImGuiViewport *viewport)
{
  ImGuiViewportDataWin32 *data =
    (ImGuiViewportDataWin32 *)viewport->PlatformUserData;

  IM_ASSERT (data->Hwnd != 0);

  if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
    ::ShowWindow (data->Hwnd, SW_SHOWNA);
  else
    ::ShowWindow (data->Hwnd, SW_SHOW);
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

  ImGui_ImplWin32_GetWin32StyleFromViewportFlags (
    viewport->Flags, &new_style,
                     &new_ex_style
  );

  // Only reapply the flags that have been changed from our point of view (as other flags are being modified by Windows)
  if ( data->DwStyle   != new_style ||
       data->DwExStyle != new_ex_style )
  {
    data->DwStyle   = new_style;
    data->DwExStyle = new_ex_style;

    ::SetWindowLongPtrW ( data->Hwnd, GWL_STYLE,   data->DwStyle  );
    ::SetWindowLongPtrW ( data->Hwnd, GWL_EXSTYLE, data->DwExStyle);

    RECT rect =
    { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
      (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
    };

    ::AdjustWindowRectEx ( &rect, data->DwStyle,
                           FALSE, data->DwExStyle ); // Client to Screen

    ::SetWindowPos       ( data->Hwnd, nullptr,
                                          rect.left,               rect.top,
                             rect.right - rect.left, rect.bottom - rect.top,
                               SWP_NOZORDER     | SWP_NOACTIVATE |
                               SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS );

     // This is necessary when we alter the style
    ::ShowWindow         ( data->Hwnd, SW_SHOWNA );

    viewport->PlatformRequestMove =
      viewport->PlatformRequestResize = true;
  }
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

  ::AdjustWindowRectEx ( &rect, data->DwStyle,
                         FALSE, data->DwExStyle );

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

  ::AdjustWindowRectEx ( &rect, data->DwStyle,
                         FALSE, data->DwExStyle ); // Client to Screen

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
  (void)viewport;

  extern bool       SKIF_bDPIScaling;
  extern ImGuiStyle SKIF_ImGui_DefaultStyle;

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
  if (ImGui_ImplWin32_WndProcHandler (hWnd, msg, wParam, lParam))
    return true;

  if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle ((void *)hWnd))
  {
    switch (msg)
    {
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
      viewport->PlatformRequestResize = true;
      break;
    case WM_MOUSEACTIVATE:
      if (viewport->Flags & ImGuiViewportFlags_NoFocusOnClick)
        return MA_NOACTIVATE;
      break;
    case WM_NCHITTEST:
        // Let mouse pass-through the window. This will allow the back-end to set io.MouseHoveredViewport properly (which is OPTIONAL).
        // The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
        // If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
        // your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
      if (viewport->Flags & ImGuiViewportFlags_NoInputs)
        return HTTRANSPARENT;
      break;
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
   wcex.lpszClassName = L"ImGui Platform";
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
  platform_io.Platform_SetImeInputPos = ImGui_ImplWin32_SetImeInputPos;
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
    L"ImGui Platform", ::GetModuleHandle (nullptr)
  );
}
// dear imgui: Platform Backend for Windows (standard windows API for 32-bits AND 64-bits applications)
// This needs to be used along with a Renderer (e.g. DirectX11, OpenGL3, Vulkan..)

// Implemented features:
//  [X] Platform: Clipboard support (for Win32 this is actually part of core dear imgui)
//  [X] Platform: Mouse support. Can discriminate Mouse/TouchScreen/Pen.
//  [X] Platform: Keyboard support. Since 1.87 we are using the io.AddKeyEvent() function. Pass ImGuiKey values to all key functions e.g. ImGui::IsKeyPressed(ImGuiKey_Space). [Legacy VK_* values will also be supported unless IMGUI_DISABLE_OBSOLETE_KEYIO is set]
//  [X] Platform: Gamepad support. Enabled with 'io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad'.
//  [X] Platform: Mouse cursor shape and visibility. Disable with 'io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange'.
//  [X] Platform: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Configuration flags to add in your imconfig file:
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD              // Disable gamepad support. This was meaningful before <1.81 but we now load XInput dynamically so the option is now less relevant.

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2024-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
//  2023-10-05: Inputs: Added support for extra ImGuiKey values: F13 to F24 function keys, app back/forward keys.
//  2023-09-25: Inputs: Synthesize key-down event on key-up for VK_SNAPSHOT / ImGuiKey_PrintScreen as Windows doesn't emit it (same behavior as GLFW/SDL).
//  2023-09-07: Inputs: Added support for keyboard codepage conversion for when application is compiled in MBCS mode and using a non-Unicode window.
//  2023-04-19: Added ImGui_ImplWin32_InitForOpenGL() to facilitate combining raw Win32/Winapi with OpenGL. (#3218)
//  2023-04-04: Inputs: Added support for io.AddMouseSourceEvent() to discriminate ImGuiMouseSource_Mouse/ImGuiMouseSource_TouchScreen/ImGuiMouseSource_Pen. (#2702)
//  2023-02-15: Inputs: Use WM_NCMOUSEMOVE / WM_NCMOUSELEAVE to track mouse position over non-client area (e.g. OS decorations) when app is not focused. (#6045, #6162)
//  2023-02-02: Inputs: Flipping WM_MOUSEHWHEEL (horizontal mouse-wheel) value to match other backends and offer consistent horizontal scrolling direction. (#4019, #6096, #1463)
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2022-09-28: Inputs: Convert WM_CHAR values with MultiByteToWideChar() when window class was registered as MBCS (not Unicode).
//  2022-09-26: Inputs: Renamed ImGuiKey_ModXXX introduced in 1.87 to ImGuiMod_XXX (old names still supported).
//  2022-01-26: Inputs: replaced short-lived io.AddKeyModsEvent() (added two weeks ago) with io.AddKeyEvent() using ImGuiKey_ModXXX flags. Sorry for the confusion.
//  2021-01-20: Inputs: calling new io.AddKeyAnalogEvent() for gamepad support, instead of writing directly to io.NavInputs[].
//  2022-01-17: Inputs: calling new io.AddMousePosEvent(), io.AddMouseButtonEvent(), io.AddMouseWheelEvent() API (1.87+).
//  2022-01-17: Inputs: always update key mods next and before a key event (not in NewFrame) to fix input queue with very low framerates.
//  2022-01-12: Inputs: Update mouse inputs using WM_MOUSEMOVE/WM_MOUSELEAVE + fallback to provide it when focused but not hovered/captured. More standard and will allow us to pass it to future input queue API.
//  2022-01-12: Inputs: Maintain our own copy of MouseButtonsDown mask instead of using ImGui::IsAnyMouseDown() which will be obsoleted.
//  2022-01-10: Inputs: calling new io.AddKeyEvent(), io.AddKeyModsEvent() + io.SetKeyEventNativeData() API (1.87+). Support for full ImGuiKey range.
//  2021-12-16: Inputs: Fill VK_LCONTROL/VK_RCONTROL/VK_LSHIFT/VK_RSHIFT/VK_LMENU/VK_RMENU for completeness.
//  2021-08-17: Calling io.AddFocusEvent() on WM_SETFOCUS/WM_KILLFOCUS messages.
//  2021-08-02: Inputs: Fixed keyboard modifiers being reported when host window doesn't have focus.
//  2021-07-29: Inputs: MousePos is correctly reported when the host platform window is hovered but not focused (using TrackMouseEvent() to receive WM_MOUSELEAVE events).
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-06-08: Fixed ImGui_ImplWin32_EnableDpiAwareness() and ImGui_ImplWin32_GetDpiScaleForMonitor() to handle Windows 8.1/10 features without a manifest (per-monitor DPI, and properly calls SetProcessDpiAwareness() on 8.1).
//  2021-03-23: Inputs: Clearing keyboard down array when losing focus (WM_KILLFOCUS).
//  2021-02-18: Added ImGui_ImplWin32_EnableAlphaCompositing(). Non Visual Studio users will need to link with dwmapi.lib (MinGW/gcc: use -ldwmapi).
//  2021-02-17: Fixed ImGui_ImplWin32_EnableDpiAwareness() attempting to get SetProcessDpiAwareness from shcore.dll on Windows 8 whereas it is only supported on Windows 8.1.
//  2021-01-25: Inputs: Dynamically loading XInput DLL.
//  2020-12-04: Misc: Fixed setting of io.DisplaySize to invalid/uninitialized data when after hwnd has been closed.
//  2020-03-03: Inputs: Calling AddInputCharacterUTF16() to support surrogate pairs leading to codepoint >= 0x10000 (for more complete CJK inputs)
//  2020-02-17: Added ImGui_ImplWin32_EnableDpiAwareness(), ImGui_ImplWin32_GetDpiScaleForHwnd(), ImGui_ImplWin32_GetDpiScaleForMonitor() helper functions.
//  2020-01-14: Inputs: Added support for #define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD/IMGUI_IMPL_WIN32_DISABLE_LINKING_XINPUT.
//  2019-12-05: Inputs: Added support for ImGuiMouseCursor_NotAllowed mouse cursor.
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
//  2016-11-12: Inputs: Only call Win32 ::SetCursor(nullptr) when io.MouseDrawCursor is set.

#include "imgui/imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui/imgui_impl_win32.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM(), GET_Y_LPARAM()
#include <tchar.h>
#include <dwmapi.h>

#define SKIF_Win32

#include "imgui/imgui_internal.h"
#include "../resource.h"
#include <utility/utility.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <utility/gamepad.h>
#include <utility/droptarget.hpp>
#include <plog/Log.h>

#ifdef SKIF_Win32
constexpr const wchar_t* SKIF_ImGui_WindowClass = L"SKIF_ImGuiWindow";
constexpr const wchar_t* SKIF_ImGui_WindowTitle = L"Special K Popup"; // Default
extern HWND SKIF_ImGui_hWnd;
extern int  SKIF_nCmdShow;
#endif // !SKIF_Win32

// Using XInput for gamepad (will load DLL dynamically)
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#include <xinput.h>
typedef DWORD(WINAPI* PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
#endif

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"     // warning: cast between incompatible function types (for loader)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"       // warning: cast between incompatible function types (for loader)
#endif

// Forward Declarations
static void ImGui_ImplWin32_InitPlatformInterface(bool platform_has_own_dc);
static void ImGui_ImplWin32_ShutdownPlatformInterface();
static void ImGui_ImplWin32_UpdateMonitors();

// SKIF CUSTOM Forward Declarations
void    SKIF_ImGui_ImplWin32_UpdateDWMBorders (void);
void    SKIF_ImGui_ImplWin32_SetDWMBorders    (void* hWnd);
bool    SKIF_ImGui_ImplWin32_IsFocused        (void);

struct ImGui_ImplWin32_Data
{
    HWND                        hWnd;
    HWND                        MouseHwnd;
    int                         MouseTrackedArea;   // 0: not tracked, 1: client are, 2: non-client area
    int                         MouseButtonsDown;
    INT64                       Time;
    INT64                       TicksPerSecond;
    ImGuiMouseCursor            LastMouseCursor;
    UINT32                      KeyboardCodePage;
    bool                        WantUpdateMonitors;

#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
    bool                        HasGamepad;
    bool                        WantUpdateHasGamepad;
    HMODULE                     XInputDLL;
    PFN_XInputGetCapabilities   XInputGetCapabilities;
    PFN_XInputGetState          XInputGetState;
#endif

    ImGui_ImplWin32_Data()      { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static ImGui_ImplWin32_Data* ImGui_ImplWin32_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplWin32_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

// Functions
static void ImGui_ImplWin32_UpdateKeyboardCodePage()
{
    // Retrieve keyboard code page, required for handling of non-Unicode Windows.
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    HKL keyboard_layout = ::GetKeyboardLayout(0);
    LCID keyboard_lcid = MAKELCID(HIWORD(keyboard_layout), SORT_DEFAULT);
    if (::GetLocaleInfoA(keyboard_lcid, (LOCALE_RETURN_NUMBER | LOCALE_IDEFAULTANSICODEPAGE), (LPSTR)&bd->KeyboardCodePage, sizeof(bd->KeyboardCodePage)) == 0)
        bd->KeyboardCodePage = CP_ACP; // Fallback to default ANSI code page when fails.
}

static bool ImGui_ImplWin32_InitEx(void* hwnd, bool platform_has_own_dc)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    INT64 perf_frequency, perf_counter;
    if (!::QueryPerformanceFrequency((LARGE_INTEGER*)&perf_frequency))
        return false;
    if (!::QueryPerformanceCounter((LARGE_INTEGER*)&perf_counter))
        return false;

    // Setup backend capabilities flags
    ImGui_ImplWin32_Data* bd = IM_NEW(ImGui_ImplWin32_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = "imgui_impl_win32";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport; // We can call io.AddMouseViewportEvent() with correct data (optional)

    bd->hWnd = (HWND)hwnd;
    bd->WantUpdateMonitors = true;
    bd->TicksPerSecond = perf_frequency;
    bd->Time = perf_counter;
    bd->LastMouseCursor = ImGuiMouseCursor_COUNT;
    ImGui_ImplWin32_UpdateKeyboardCodePage();

    // Our mouse update function expect PlatformHandle to be filled for the main viewport
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (void*)bd->hWnd;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        ImGui_ImplWin32_InitPlatformInterface(platform_has_own_dc);

    // Dynamically load XInput library
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
    bd->WantUpdateHasGamepad = true;
    const char* xinput_dll_names[] =
    {
        "xinput1_4.dll",   // Windows 8+
        "xinput1_3.dll",   // DirectX SDK
        "xinput9_1_0.dll", // Windows Vista, Windows 7
        "xinput1_2.dll",   // DirectX SDK
        "xinput1_1.dll"    // DirectX SDK
    };
    for (int n = 0; n < IM_ARRAYSIZE(xinput_dll_names); n++)
        if (HMODULE dll = ::LoadLibraryA(xinput_dll_names[n]))
        {
            bd->XInputDLL = dll;
            bd->XInputGetCapabilities = (PFN_XInputGetCapabilities)::GetProcAddress(dll, "XInputGetCapabilities");
            bd->XInputGetState = (PFN_XInputGetState)::GetProcAddress(dll, "XInputGetState");
            break;
        }
#endif // IMGUI_IMPL_WIN32_DISABLE_GAMEPAD

    return true;
}

IMGUI_IMPL_API bool     ImGui_ImplWin32_Init(void* hwnd)
{
    return ImGui_ImplWin32_InitEx(hwnd, false);
}

IMGUI_IMPL_API bool     ImGui_ImplWin32_InitForOpenGL(void* hwnd)
{
    // OpenGL needs CS_OWNDC
    return ImGui_ImplWin32_InitEx(hwnd, true);
}

void    ImGui_ImplWin32_Shutdown()
{
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplWin32_ShutdownPlatformInterface();

    // Unload XInput library
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
    if (bd->XInputDLL)
        ::FreeLibrary(bd->XInputDLL);
#endif // IMGUI_IMPL_WIN32_DISABLE_GAMEPAD

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseHoveredViewport);
    IM_DELETE(bd);
}

static bool ImGui_ImplWin32_UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
        return false;

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        ::SetCursor(nullptr);
    }
    else
    {
        // Show OS mouse cursor
        LPTSTR win32_cursor = IDC_ARROW;
        switch (imgui_cursor)
        {
        case ImGuiMouseCursor_Arrow:        win32_cursor = IDC_ARROW; break;
        case ImGuiMouseCursor_TextInput:    win32_cursor = IDC_IBEAM; break;
        case ImGuiMouseCursor_ResizeAll:    win32_cursor = IDC_SIZEALL; break;
        case ImGuiMouseCursor_ResizeEW:     win32_cursor = IDC_SIZEWE; break;
        case ImGuiMouseCursor_ResizeNS:     win32_cursor = IDC_SIZENS; break;
        case ImGuiMouseCursor_ResizeNESW:   win32_cursor = IDC_SIZENESW; break;
        case ImGuiMouseCursor_ResizeNWSE:   win32_cursor = IDC_SIZENWSE; break;
        case ImGuiMouseCursor_Hand:         win32_cursor = IDC_HAND; break;
        case ImGuiMouseCursor_NotAllowed:   win32_cursor = IDC_NO; break;
        }
        ::SetCursor(::LoadCursor(nullptr, win32_cursor));
    }
    return true;
}

static bool IsVkDown(int vk)
{
    return (::GetKeyState(vk) & 0x8000) != 0;
}

static void ImGui_ImplWin32_AddKeyEvent(ImGuiKey key, bool down, int native_keycode, int native_scancode = -1)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, native_keycode, native_scancode); // To support legacy indexing (<1.87 user code)
    IM_UNUSED(native_scancode);
}

static void ImGui_ImplWin32_ProcessKeyEventsWorkarounds()
{
    // Left & right Shift keys: when both are pressed together, Windows tend to not generate the WM_KEYUP event for the first released one.
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) && !IsVkDown(VK_LSHIFT))
        ImGui_ImplWin32_AddKeyEvent(ImGuiKey_LeftShift, false, VK_LSHIFT);
    if (ImGui::IsKeyDown(ImGuiKey_RightShift) && !IsVkDown(VK_RSHIFT))
        ImGui_ImplWin32_AddKeyEvent(ImGuiKey_RightShift, false, VK_RSHIFT);

    // Sometimes WM_KEYUP for Win key is not passed down to the app (e.g. for Win+V on some setups, according to GLFW).
    if (ImGui::IsKeyDown(ImGuiKey_LeftSuper) && !IsVkDown(VK_LWIN))
        ImGui_ImplWin32_AddKeyEvent(ImGuiKey_LeftSuper, false, VK_LWIN);
    if (ImGui::IsKeyDown(ImGuiKey_RightSuper) && !IsVkDown(VK_RWIN))
        ImGui_ImplWin32_AddKeyEvent(ImGuiKey_RightSuper, false, VK_RWIN);
}

static void ImGui_ImplWin32_UpdateKeyModifiers()
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, IsVkDown(VK_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsVkDown(VK_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsVkDown(VK_MENU));
    io.AddKeyEvent(ImGuiMod_Super, (IsVkDown(VK_LWIN) || IsVkDown(VK_RWIN)));
}

// This code supports multi-viewports (multiple OS Windows mapped into different Dear ImGui viewports)
// Because of that, it is a little more complicated than your typical single-viewport binding code!
static void ImGui_ImplWin32_UpdateMouseData()
{
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    ImGuiIO& io = ImGui::GetIO();
    if (bd->hWnd == 0) return; // SKIF CUSTOM
    IM_ASSERT(bd->hWnd != 0);

    POINT mouse_screen_pos;
    bool has_mouse_screen_pos = ::GetCursorPos(&mouse_screen_pos) != 0;

    HWND focused_window = ::GetForegroundWindow();
    const bool is_app_focused = (focused_window && (focused_window == bd->hWnd || ::IsChild(focused_window, bd->hWnd) || ImGui::FindViewportByPlatformHandle((void*)focused_window)));
    if (is_app_focused)
    {
        // (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
        // When multi-viewports are enabled, all Dear ImGui positions are same as OS positions.
        if (io.WantSetMousePos)
        {
            POINT pos = { (int)io.MousePos.x, (int)io.MousePos.y };
            if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0)
                ::ClientToScreen(focused_window, &pos);
            ::SetCursorPos(pos.x, pos.y);
        }

        // (Optional) Fallback to provide mouse position when focused (WM_MOUSEMOVE already provides this when hovered or captured)
        // This also fills a short gap when clicking non-client area: WM_NCMOUSELEAVE -> modal OS move -> gap -> WM_NCMOUSEMOVE
        if (!io.WantSetMousePos && bd->MouseTrackedArea == 0 && has_mouse_screen_pos)
        {
            // Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
            // (This is the position you can get with ::GetCursorPos() + ::ScreenToClient() or WM_MOUSEMOVE.)
            // Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
            // (This is the position you can get with ::GetCursorPos() or WM_MOUSEMOVE + ::ClientToScreen(). In theory adding viewport->Pos to a client position would also be the same.)
            POINT mouse_pos = mouse_screen_pos;
            if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
                ::ScreenToClient(bd->hWnd, &mouse_pos);
            io.AddMousePosEvent((float)mouse_pos.x, (float)mouse_pos.y);
        }
    }

    // (Optional) When using multiple viewports: call io.AddMouseViewportEvent() with the viewport the OS mouse cursor is hovering.
    // If ImGuiBackendFlags_HasMouseHoveredViewport is not set by the backend, Dear imGui will ignore this field and infer the information using its flawed heuristic.
    // - [X] Win32 backend correctly ignore viewports with the _NoInputs flag (here using ::WindowFromPoint with WM_NCHITTEST + HTTRANSPARENT in WndProc does that)
    //       Some backend are not able to handle that correctly. If a backend report an hovered viewport that has the _NoInputs flag (e.g. when dragging a window
    //       for docking, the viewport has the _NoInputs flag in order to allow us to find the viewport under), then Dear ImGui is forced to ignore the value reported
    //       by the backend, and use its flawed heuristic to guess the viewport behind.
    // - [X] Win32 backend correctly reports this regardless of another viewport behind focused and dragged from (we need this to find a useful drag and drop target).
    ImGuiID mouse_viewport_id = 0;
    if (has_mouse_screen_pos)
        if (HWND hovered_hwnd = ::WindowFromPoint(mouse_screen_pos))
            if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle((void*)hovered_hwnd))
                mouse_viewport_id = viewport->ID;
    io.AddMouseViewportEvent(mouse_viewport_id);
}

// Gamepad navigation mapping
static void ImGui_ImplWin32_UpdateGamepads()
{
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    //if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) // FIXME: Technically feeding gamepad shouldn't depend on this now that they are regular inputs.
    //    return;

    // Calling XInputGetState() every frame on disconnected gamepads is unfortunately too slow.
    // Instead we refresh gamepad availability by calling XInputGetCapabilities() _only_ after receiving WM_DEVICECHANGE.
    if (bd->WantUpdateHasGamepad)
    {
        XINPUT_CAPABILITIES caps = {};
        bd->HasGamepad = bd->XInputGetCapabilities ? (bd->XInputGetCapabilities(0, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS) : false;
        bd->WantUpdateHasGamepad = false;
    }

    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    XINPUT_STATE xinput_state;
    XINPUT_GAMEPAD& gamepad = xinput_state.Gamepad;
    if (!bd->HasGamepad || bd->XInputGetState == nullptr || bd->XInputGetState(0, &xinput_state) != ERROR_SUCCESS)
        return;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
#endif // #ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
    
    static SKIF_GamePadInputHelper& _gamepad  = SKIF_GamePadInputHelper::GetInstance ( );

    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;

    XINPUT_STATE xinput_state =
      _gamepad.GetXInputState ( );

    if (xinput_state.dwPacketNumber == 0)
      return;

    const XINPUT_GAMEPAD& gamepad = xinput_state.Gamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    #define IM_SATURATE(V)                      (V < 0.0f ? 0.0f : V > 1.0f ? 1.0f : V)
    #define MAP_BUTTON(KEY_NO, BUTTON_ENUM)     { io.AddKeyEvent(KEY_NO, (gamepad.wButtons & BUTTON_ENUM) != 0); }
    #define MAP_ANALOG(KEY_NO, VALUE, V0, V1)   { float vn = (float)(VALUE - V0) / (float)(V1 - V0); io.AddKeyAnalogEvent(KEY_NO, vn > 0.10f, IM_SATURATE(vn)); }
    MAP_BUTTON(ImGuiKey_GamepadStart,           XINPUT_GAMEPAD_START);
    MAP_BUTTON(ImGuiKey_GamepadBack,            XINPUT_GAMEPAD_BACK);
    MAP_BUTTON(ImGuiKey_GamepadFaceLeft,        XINPUT_GAMEPAD_X);
    MAP_BUTTON(ImGuiKey_GamepadFaceRight,       XINPUT_GAMEPAD_B);
    MAP_BUTTON(ImGuiKey_GamepadFaceUp,          XINPUT_GAMEPAD_Y);
    MAP_BUTTON(ImGuiKey_GamepadFaceDown,        XINPUT_GAMEPAD_A);
    MAP_BUTTON(ImGuiKey_GamepadDpadLeft,        XINPUT_GAMEPAD_DPAD_LEFT);
    MAP_BUTTON(ImGuiKey_GamepadDpadRight,       XINPUT_GAMEPAD_DPAD_RIGHT);
    MAP_BUTTON(ImGuiKey_GamepadDpadUp,          XINPUT_GAMEPAD_DPAD_UP);
    MAP_BUTTON(ImGuiKey_GamepadDpadDown,        XINPUT_GAMEPAD_DPAD_DOWN);
    MAP_BUTTON(ImGuiKey_GamepadL1,              XINPUT_GAMEPAD_LEFT_SHOULDER);
    MAP_BUTTON(ImGuiKey_GamepadR1,              XINPUT_GAMEPAD_RIGHT_SHOULDER);
    MAP_ANALOG(ImGuiKey_GamepadL2,              gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
    MAP_ANALOG(ImGuiKey_GamepadR2,              gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD, 255);
    MAP_BUTTON(ImGuiKey_GamepadL3,              XINPUT_GAMEPAD_LEFT_THUMB);
    MAP_BUTTON(ImGuiKey_GamepadR3,              XINPUT_GAMEPAD_RIGHT_THUMB);
    MAP_ANALOG(ImGuiKey_GamepadLStickLeft,      gamepad.sThumbLX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(ImGuiKey_GamepadLStickRight,     gamepad.sThumbLX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadLStickUp,        gamepad.sThumbLY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadLStickDown,      gamepad.sThumbLY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(ImGuiKey_GamepadRStickLeft,      gamepad.sThumbRX, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    MAP_ANALOG(ImGuiKey_GamepadRStickRight,     gamepad.sThumbRX, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadRStickUp,        gamepad.sThumbRY, +XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, +32767);
    MAP_ANALOG(ImGuiKey_GamepadRStickDown,      gamepad.sThumbRY, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768);
    #undef MAP_BUTTON
    #undef MAP_ANALOG
//#endif // #ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
}

static BOOL CALLBACK ImGui_ImplWin32_UpdateMonitors_EnumFunc(HMONITOR monitor, HDC, LPRECT, LPARAM)
{
    MONITORINFO info = {};
    info.cbSize = sizeof(MONITORINFO);
    if (!::GetMonitorInfo(monitor, &info))
        return TRUE;
    ImGuiPlatformMonitor imgui_monitor;
    imgui_monitor.MainPos = ImVec2((float)info.rcMonitor.left, (float)info.rcMonitor.top);
    imgui_monitor.MainSize = ImVec2((float)(info.rcMonitor.right - info.rcMonitor.left), (float)(info.rcMonitor.bottom - info.rcMonitor.top));
    imgui_monitor.WorkPos = ImVec2((float)info.rcWork.left, (float)info.rcWork.top);
    imgui_monitor.WorkSize = ImVec2((float)(info.rcWork.right - info.rcWork.left), (float)(info.rcWork.bottom - info.rcWork.top));
    imgui_monitor.DpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
    imgui_monitor.PlatformHandle = (void*)monitor;
    ImGuiPlatformIO& io = ImGui::GetPlatformIO();
    if (info.dwFlags & MONITORINFOF_PRIMARY)
        io.Monitors.push_front(imgui_monitor);
    else
        io.Monitors.push_back(imgui_monitor);
    return TRUE;
}

static void ImGui_ImplWin32_UpdateMonitors()
{
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    ImGui::GetPlatformIO().Monitors.resize(0);
    ::EnumDisplayMonitors(nullptr, nullptr, ImGui_ImplWin32_UpdateMonitors_EnumFunc, 0);
    bd->WantUpdateMonitors = false;
}

void    ImGui_ImplWin32_NewFrame()
{
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized? Did you call ImGui_ImplWin32_Init()?");
    ImGuiIO& io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    RECT rect = { 0, 0, 0, 0 };
    ::GetClientRect(bd->hWnd, &rect);
    io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
    if (bd->WantUpdateMonitors)
        ImGui_ImplWin32_UpdateMonitors();

    // Setup time step
    INT64 current_time = 0;
    ::QueryPerformanceCounter((LARGE_INTEGER*)&current_time);
    io.DeltaTime = (float)(current_time - bd->Time) / bd->TicksPerSecond;
    bd->Time = current_time;

    // Update OS mouse position
    ImGui_ImplWin32_UpdateMouseData();

    // Process workarounds for known Windows key handling issues
    ImGui_ImplWin32_ProcessKeyEventsWorkarounds();

    // Update OS mouse cursor with the cursor requested by imgui
    ImGuiMouseCursor mouse_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (bd->LastMouseCursor != mouse_cursor)
    {
        bd->LastMouseCursor = mouse_cursor;
        ImGui_ImplWin32_UpdateMouseCursor();
    }

    // Update game controllers (if enabled and available)
    ImGui_ImplWin32_UpdateGamepads();

#ifdef SKIF_Win32
    extern INT64
      SKIF_TimeInMilliseconds;
      SKIF_TimeInMilliseconds =
        bd->Time / (bd->TicksPerSecond / 1000LL);
#endif // SKIF_Win32
}

// There is no distinct VK_xxx for keypad enter, instead it is VK_RETURN + KF_EXTENDED, we assign it an arbitrary value to make code more readable (VK_ codes go up to 255)
#define IM_VK_KEYPAD_ENTER      (VK_RETURN + 256)

// Map VK_xxx to ImGuiKey_xxx.
ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam)
{
    switch (wParam)
    {
        case VK_TAB: return ImGuiKey_Tab;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        case VK_PRIOR: return ImGuiKey_PageUp;
        case VK_NEXT: return ImGuiKey_PageDown;
        case VK_HOME: return ImGuiKey_Home;
        case VK_END: return ImGuiKey_End;
        case VK_INSERT: return ImGuiKey_Insert;
        case VK_DELETE: return ImGuiKey_Delete;
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_OEM_7: return ImGuiKey_Apostrophe;
        case VK_OEM_COMMA: return ImGuiKey_Comma;
        case VK_OEM_MINUS: return ImGuiKey_Minus;
        case VK_OEM_PERIOD: return ImGuiKey_Period;
        case VK_OEM_2: return ImGuiKey_Slash;
        case VK_OEM_1: return ImGuiKey_Semicolon;
        case VK_OEM_PLUS: return ImGuiKey_Equal;
        case VK_OEM_4: return ImGuiKey_LeftBracket;
        case VK_OEM_5: return ImGuiKey_Backslash;
        case VK_OEM_6: return ImGuiKey_RightBracket;
        case VK_OEM_3: return ImGuiKey_GraveAccent;
        case VK_CAPITAL: return ImGuiKey_CapsLock;
        case VK_SCROLL: return ImGuiKey_ScrollLock;
        case VK_NUMLOCK: return ImGuiKey_NumLock;
        case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
        case VK_PAUSE: return ImGuiKey_Pause;
        case VK_NUMPAD0: return ImGuiKey_Keypad0;
        case VK_NUMPAD1: return ImGuiKey_Keypad1;
        case VK_NUMPAD2: return ImGuiKey_Keypad2;
        case VK_NUMPAD3: return ImGuiKey_Keypad3;
        case VK_NUMPAD4: return ImGuiKey_Keypad4;
        case VK_NUMPAD5: return ImGuiKey_Keypad5;
        case VK_NUMPAD6: return ImGuiKey_Keypad6;
        case VK_NUMPAD7: return ImGuiKey_Keypad7;
        case VK_NUMPAD8: return ImGuiKey_Keypad8;
        case VK_NUMPAD9: return ImGuiKey_Keypad9;
        case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
        case VK_DIVIDE: return ImGuiKey_KeypadDivide;
        case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case VK_ADD: return ImGuiKey_KeypadAdd;
        case IM_VK_KEYPAD_ENTER: return ImGuiKey_KeypadEnter;
        case VK_LSHIFT: return ImGuiKey_LeftShift;
        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
        case VK_LMENU: return ImGuiKey_LeftAlt;
        case VK_LWIN: return ImGuiKey_LeftSuper;
        case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_RCONTROL: return ImGuiKey_RightCtrl;
        case VK_RMENU: return ImGuiKey_RightAlt;
        case VK_RWIN: return ImGuiKey_RightSuper;
        case VK_APPS: return ImGuiKey_Menu;
        case '0': return ImGuiKey_0;
        case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4;
        case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6;
        case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;
        case 'A': return ImGuiKey_A;
        case 'B': return ImGuiKey_B;
        case 'C': return ImGuiKey_C;
        case 'D': return ImGuiKey_D;
        case 'E': return ImGuiKey_E;
        case 'F': return ImGuiKey_F;
        case 'G': return ImGuiKey_G;
        case 'H': return ImGuiKey_H;
        case 'I': return ImGuiKey_I;
        case 'J': return ImGuiKey_J;
        case 'K': return ImGuiKey_K;
        case 'L': return ImGuiKey_L;
        case 'M': return ImGuiKey_M;
        case 'N': return ImGuiKey_N;
        case 'O': return ImGuiKey_O;
        case 'P': return ImGuiKey_P;
        case 'Q': return ImGuiKey_Q;
        case 'R': return ImGuiKey_R;
        case 'S': return ImGuiKey_S;
        case 'T': return ImGuiKey_T;
        case 'U': return ImGuiKey_U;
        case 'V': return ImGuiKey_V;
        case 'W': return ImGuiKey_W;
        case 'X': return ImGuiKey_X;
        case 'Y': return ImGuiKey_Y;
        case 'Z': return ImGuiKey_Z;
        case VK_F1: return ImGuiKey_F1;
        case VK_F2: return ImGuiKey_F2;
        case VK_F3: return ImGuiKey_F3;
        case VK_F4: return ImGuiKey_F4;
        case VK_F5: return ImGuiKey_F5;
        case VK_F6: return ImGuiKey_F6;
        case VK_F7: return ImGuiKey_F7;
        case VK_F8: return ImGuiKey_F8;
        case VK_F9: return ImGuiKey_F9;
        case VK_F10: return ImGuiKey_F10;
        case VK_F11: return ImGuiKey_F11;
        case VK_F12: return ImGuiKey_F12;
        case VK_F13: return ImGuiKey_F13;
        case VK_F14: return ImGuiKey_F14;
        case VK_F15: return ImGuiKey_F15;
        case VK_F16: return ImGuiKey_F16;
        case VK_F17: return ImGuiKey_F17;
        case VK_F18: return ImGuiKey_F18;
        case VK_F19: return ImGuiKey_F19;
        case VK_F20: return ImGuiKey_F20;
        case VK_F21: return ImGuiKey_F21;
        case VK_F22: return ImGuiKey_F22;
        case VK_F23: return ImGuiKey_F23;
        case VK_F24: return ImGuiKey_F24;
        case VK_BROWSER_BACK: return ImGuiKey_AppBack;
        case VK_BROWSER_FORWARD: return ImGuiKey_AppForward;
        default: return ImGuiKey_None;
    }
}

// Map ImGuiKey_xxx to VK_xxx.
// SKIF CUSTOM -- just the straight opposite of ImGui_ImplWin32_VirtualKeyToImGuiKey
int ImGui_ImplWin32_ImGuiKeyToVirtualKey(ImGuiKey key)
{
    switch (key)
    {
        case ImGuiKey_Tab: return VK_TAB;
        case ImGuiKey_LeftArrow: return VK_LEFT;
        case ImGuiKey_RightArrow: return VK_RIGHT;
        case ImGuiKey_UpArrow: return VK_UP;
        case ImGuiKey_DownArrow: return VK_DOWN;
        case ImGuiKey_PageUp: return VK_PRIOR;
        case ImGuiKey_PageDown: return VK_NEXT;
        case ImGuiKey_Home: return VK_HOME;
        case ImGuiKey_End: return VK_END;
        case ImGuiKey_Insert: return VK_INSERT;
        case ImGuiKey_Delete: return VK_DELETE;
        case ImGuiKey_Backspace: return VK_BACK;
        case ImGuiKey_Space: return VK_SPACE;
        case ImGuiKey_Enter: return VK_RETURN;
        case ImGuiKey_Escape: return VK_ESCAPE;
        case ImGuiKey_Apostrophe: return VK_OEM_7;
        case ImGuiKey_Comma: return VK_OEM_COMMA;
        case ImGuiKey_Minus: return VK_OEM_MINUS;
        case ImGuiKey_Period: return VK_OEM_PERIOD;
        case ImGuiKey_Slash: return VK_OEM_2;
        case ImGuiKey_Semicolon: return VK_OEM_1;
        case ImGuiKey_Equal: return VK_OEM_PLUS;
        case ImGuiKey_LeftBracket: return VK_OEM_4;
        case ImGuiKey_Backslash: return VK_OEM_5;
        case ImGuiKey_RightBracket: return VK_OEM_6;
        case ImGuiKey_GraveAccent: return VK_OEM_3;
        case ImGuiKey_CapsLock: return VK_CAPITAL;
        case ImGuiKey_ScrollLock: return VK_SCROLL;
        case ImGuiKey_NumLock: return VK_NUMLOCK;
        case ImGuiKey_PrintScreen: return VK_SNAPSHOT;
        case ImGuiKey_Pause: return VK_PAUSE;
        case ImGuiKey_Keypad0: return VK_NUMPAD0;
        case ImGuiKey_Keypad1: return VK_NUMPAD1;
        case ImGuiKey_Keypad2: return VK_NUMPAD2;
        case ImGuiKey_Keypad3: return VK_NUMPAD3;
        case ImGuiKey_Keypad4: return VK_NUMPAD4;
        case ImGuiKey_Keypad5: return VK_NUMPAD5;
        case ImGuiKey_Keypad6: return VK_NUMPAD6;
        case ImGuiKey_Keypad7: return VK_NUMPAD7;
        case ImGuiKey_Keypad8: return VK_NUMPAD8;
        case ImGuiKey_Keypad9: return VK_NUMPAD9;
        case ImGuiKey_KeypadDecimal: return VK_DECIMAL;
        case ImGuiKey_KeypadDivide: return VK_DIVIDE;
        case ImGuiKey_KeypadMultiply: return VK_MULTIPLY;
        case ImGuiKey_KeypadSubtract: return VK_SUBTRACT;
        case ImGuiKey_KeypadAdd: return VK_ADD;
        case ImGuiKey_KeypadEnter: return IM_VK_KEYPAD_ENTER;
        case ImGuiKey_LeftShift: return VK_LSHIFT;
        case ImGuiKey_LeftCtrl: return VK_LCONTROL;
        case ImGuiKey_LeftAlt: return VK_LMENU;
        case ImGuiKey_LeftSuper: return VK_LWIN;
        case ImGuiKey_RightShift: return VK_RSHIFT;
        case ImGuiKey_RightCtrl: return VK_RCONTROL;
        case ImGuiKey_RightAlt: return VK_RMENU;
        case ImGuiKey_RightSuper: return VK_RWIN;
        case ImGuiKey_Menu: return VK_APPS;
        case ImGuiKey_0: return '0';
        case ImGuiKey_1: return '1';
        case ImGuiKey_2: return '2';
        case ImGuiKey_3: return '3';
        case ImGuiKey_4: return '4';
        case ImGuiKey_5: return '5';
        case ImGuiKey_6: return '6';
        case ImGuiKey_7: return '7';
        case ImGuiKey_8: return '8';
        case ImGuiKey_9: return '9';
        case ImGuiKey_A: return 'A';
        case ImGuiKey_B: return 'B';
        case ImGuiKey_C: return 'C';
        case ImGuiKey_D: return 'D';
        case ImGuiKey_E: return 'E';
        case ImGuiKey_F: return 'F';
        case ImGuiKey_G: return 'G';
        case ImGuiKey_H: return 'H';
        case ImGuiKey_I: return 'I';
        case ImGuiKey_J: return 'J';
        case ImGuiKey_K: return 'K';
        case ImGuiKey_L: return 'L';
        case ImGuiKey_M: return 'M';
        case ImGuiKey_N: return 'N';
        case ImGuiKey_O: return 'O';
        case ImGuiKey_P: return 'P';
        case ImGuiKey_Q: return 'Q';
        case ImGuiKey_R: return 'R';
        case ImGuiKey_S: return 'S';
        case ImGuiKey_T: return 'T';
        case ImGuiKey_U: return 'U';
        case ImGuiKey_V: return 'V';
        case ImGuiKey_W: return 'W';
        case ImGuiKey_X: return 'X';
        case ImGuiKey_Y: return 'Y';
        case ImGuiKey_Z: return 'Z';
        case ImGuiKey_F1: return VK_F1;
        case ImGuiKey_F2: return VK_F2;
        case ImGuiKey_F3: return VK_F3;
        case ImGuiKey_F4: return VK_F4;
        case ImGuiKey_F5: return VK_F5;
        case ImGuiKey_F6: return VK_F6;
        case ImGuiKey_F7: return VK_F7;
        case ImGuiKey_F8: return VK_F8;
        case ImGuiKey_F9: return VK_F9;
        case ImGuiKey_F10: return VK_F10;
        case ImGuiKey_F11: return VK_F11;
        case ImGuiKey_F12: return VK_F12;
        case ImGuiKey_F13: return VK_F13;
        case ImGuiKey_F14: return VK_F14;
        case ImGuiKey_F15: return VK_F15;
        case ImGuiKey_F16: return VK_F16;
        case ImGuiKey_F17: return VK_F17;
        case ImGuiKey_F18: return VK_F18;
        case ImGuiKey_F19: return VK_F19;
        case ImGuiKey_F20: return VK_F20;
        case ImGuiKey_F21: return VK_F21;
        case ImGuiKey_F22: return VK_F22;
        case ImGuiKey_F23: return VK_F23;
        case ImGuiKey_F24: return VK_F24;
        case ImGuiKey_AppBack: return VK_BROWSER_BACK;
        case ImGuiKey_AppForward: return VK_BROWSER_FORWARD;
        default: return 0x00;
    }
}

// Allow compilation with old Windows SDK. MinGW doesn't have default _WIN32_WINNT/WINVER versions.
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
#ifndef DBT_DEVNODES_CHANGED
#define DBT_DEVNODES_CHANGED 0x0007
#endif

// Win32 message handler (process Win32 mouse/keyboard inputs, etc.)
// Call from your application's message handler. Keep calling your message handler unless this function returns TRUE.
// When implementing your own backend, you can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if Dear ImGui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to Dear ImGui, and hide them from your application based on those two flags.
// PS: In this Win32 handler, we use the capture API (GetCapture/SetCapture/ReleaseCapture) to be able to read mouse coordinates when dragging mouse outside of our window bounds.
// PS: We treat DBLCLK messages as regular mouse down messages, so this code will work on windows classes that have the CS_DBLCLKS flag set. Our own example app code doesn't set this flag.
#if 0
// Copy this line into your .cpp file to forward declare the function.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// See https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages
// Prefer to call this at the top of the message handler to avoid the possibility of other Win32 calls interfering with this.
static ImGuiMouseSource GetMouseSourceFromMessageExtraInfo()
{
    LPARAM extra_info = ::GetMessageExtraInfo();
    if ((extra_info & 0xFFFFFF80) == 0xFF515700)
        return ImGuiMouseSource_Pen;
    if ((extra_info & 0xFFFFFF80) == 0xFF515780)
        return ImGuiMouseSource_TouchScreen;
    return ImGuiMouseSource_Mouse;
}

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Most backends don't have silent checks like this one, but we need it because WndProc are called early in CreateWindow().
    if (ImGui::GetCurrentContext() == nullptr)
        return 0;

    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplWin32_Init()?");
    ImGuiIO& io = ImGui::GetIO();

    switch (msg)
    {
    case WM_MOUSEMOVE:
    case WM_NCMOUSEMOVE:
    {
        // We need to call TrackMouseEvent in order to receive WM_MOUSELEAVE events
        ImGuiMouseSource mouse_source = GetMouseSourceFromMessageExtraInfo();
        const int area = (msg == WM_MOUSEMOVE) ? 1 : 2;
        bd->MouseHwnd = hwnd;
        if (bd->MouseTrackedArea != area)
        {
            TRACKMOUSEEVENT tme_cancel = { sizeof(tme_cancel), TME_CANCEL, hwnd, 0 };
            TRACKMOUSEEVENT tme_track = { sizeof(tme_track), (DWORD)((area == 2) ? (TME_LEAVE | TME_NONCLIENT) : TME_LEAVE), hwnd, 0 };
            if (bd->MouseTrackedArea != 0)
                ::TrackMouseEvent(&tme_cancel);
            ::TrackMouseEvent(&tme_track);
            bd->MouseTrackedArea = area;
        }
        POINT mouse_pos = { (LONG)GET_X_LPARAM(lParam), (LONG)GET_Y_LPARAM(lParam) };
        bool want_absolute_pos = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
        if (msg == WM_MOUSEMOVE && want_absolute_pos)    // WM_MOUSEMOVE are client-relative coordinates.
            ::ClientToScreen(hwnd, &mouse_pos);
        if (msg == WM_NCMOUSEMOVE && !want_absolute_pos) // WM_NCMOUSEMOVE are absolute coordinates.
            ::ScreenToClient(hwnd, &mouse_pos);
        io.AddMouseSourceEvent(mouse_source);
        io.AddMousePosEvent((float)mouse_pos.x, (float)mouse_pos.y);
        break;
    }
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
    {
        const int area = (msg == WM_MOUSELEAVE) ? 1 : 2;
        if (bd->MouseTrackedArea == area)
        {
            if (bd->MouseHwnd == hwnd)
                bd->MouseHwnd = nullptr;
            bd->MouseTrackedArea = 0;
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }
        break;
    }
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
    {
        ImGuiMouseSource mouse_source = GetMouseSourceFromMessageExtraInfo();
        int button = 0;
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) { button = 0; }
        if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) { button = 1; }
        if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) { button = 2; }
        if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) { button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4; }
        if (bd->MouseButtonsDown == 0 && ::GetCapture() == nullptr)
            ::SetCapture(hwnd);
        bd->MouseButtonsDown |= 1 << button;
        io.AddMouseSourceEvent(mouse_source);
        io.AddMouseButtonEvent(button, true);
        return 0;
    }

#ifdef SKIF_Win32
    case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK: // This is needed to open ImGui menus on right clicks in "non-client" areas (aka draggable areas)
    case WM_NCMBUTTONDOWN: case WM_NCMBUTTONDBLCLK: // This is needed to allow auto-scroll to work properly
    {
        ImGuiMouseSource mouse_source = GetMouseSourceFromMessageExtraInfo();
        int button = 0;
        if (msg == WM_NCRBUTTONDOWN || msg == WM_NCRBUTTONDBLCLK) { button = 1; }
        if (msg == WM_NCMBUTTONDOWN || msg == WM_NCMBUTTONDBLCLK) { button = 2; }
        if (bd->MouseButtonsDown == 0 && ::GetCapture() == nullptr)
          ::SetCapture(hwnd);
        bd->MouseButtonsDown |= 1 << button;
        io.AddMouseSourceEvent(mouse_source);
        io.AddMouseButtonEvent(button, true);
        return 0;
    }
    case WM_NCRBUTTONUP: // This is needed to open ImGui menus on right clicks in "non-client" areas (aka draggable areas)
    case WM_NCMBUTTONUP: // This is needed to allow auto-scroll to work properly
    {
        ImGuiMouseSource mouse_source = GetMouseSourceFromMessageExtraInfo();
        int button = 0;
        if (msg == WM_NCRBUTTONUP) { button = 1; }
        if (msg == WM_NCMBUTTONUP) { button = 2; }
        bd->MouseButtonsDown &= ~(1 << button);
        if (bd->MouseButtonsDown == 0 && ::GetCapture() == hwnd)
            ::ReleaseCapture();
        io.AddMouseSourceEvent(mouse_source);
        io.AddMouseButtonEvent(button, false);
        return 0;
    }
#endif // SKIF_Win32

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    {
        ImGuiMouseSource mouse_source = GetMouseSourceFromMessageExtraInfo();
        int button = 0;
        if (msg == WM_LBUTTONUP) { button = 0; }
        if (msg == WM_RBUTTONUP) { button = 1; }
        if (msg == WM_MBUTTONUP) { button = 2; }
        if (msg == WM_XBUTTONUP) { button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4; }
        bd->MouseButtonsDown &= ~(1 << button);
        if (bd->MouseButtonsDown == 0 && ::GetCapture() == hwnd)
            ::ReleaseCapture();
        io.AddMouseSourceEvent(mouse_source);
        io.AddMouseButtonEvent(button, false);
        return 0;
    }
    case WM_MOUSEWHEEL:
        io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
        return 0;
    case WM_MOUSEHWHEEL:
        io.AddMouseWheelEvent(-(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
        const bool is_key_down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (wParam < 256)
        {
            // Submit modifiers
            ImGui_ImplWin32_UpdateKeyModifiers();

            // Obtain virtual key code
            // (keypad enter doesn't have its own... VK_RETURN with KF_EXTENDED flag means keypad enter, see IM_VK_KEYPAD_ENTER definition for details, it is mapped to ImGuiKey_KeyPadEnter.)
            int vk = (int)wParam;
            if ((wParam == VK_RETURN) && (HIWORD(lParam) & KF_EXTENDED))
                vk = IM_VK_KEYPAD_ENTER;
            const ImGuiKey key = ImGui_ImplWin32_VirtualKeyToImGuiKey(vk);
            const int scancode = (int)LOBYTE(HIWORD(lParam));

            // Special behavior for VK_SNAPSHOT / ImGuiKey_PrintScreen as Windows doesn't emit the key down event.
            if (key == ImGuiKey_PrintScreen && !is_key_down)
                ImGui_ImplWin32_AddKeyEvent(key, true, vk, scancode);

            // Submit key event
            if (key != ImGuiKey_None)
                ImGui_ImplWin32_AddKeyEvent(key, is_key_down, vk, scancode);

            // Submit individual left/right modifier events
            if (vk == VK_SHIFT)
            {
                // Important: Shift keys tend to get stuck when pressed together, missing key-up events are corrected in ImGui_ImplWin32_ProcessKeyEventsWorkarounds()
                if (IsVkDown(VK_LSHIFT) == is_key_down) { ImGui_ImplWin32_AddKeyEvent(ImGuiKey_LeftShift, is_key_down, VK_LSHIFT, scancode); }
                if (IsVkDown(VK_RSHIFT) == is_key_down) { ImGui_ImplWin32_AddKeyEvent(ImGuiKey_RightShift, is_key_down, VK_RSHIFT, scancode); }
            }
            else if (vk == VK_CONTROL)
            {
                if (IsVkDown(VK_LCONTROL) == is_key_down) { ImGui_ImplWin32_AddKeyEvent(ImGuiKey_LeftCtrl, is_key_down, VK_LCONTROL, scancode); }
                if (IsVkDown(VK_RCONTROL) == is_key_down) { ImGui_ImplWin32_AddKeyEvent(ImGuiKey_RightCtrl, is_key_down, VK_RCONTROL, scancode); }
            }
            else if (vk == VK_MENU)
            {
                if (IsVkDown(VK_LMENU) == is_key_down) { ImGui_ImplWin32_AddKeyEvent(ImGuiKey_LeftAlt, is_key_down, VK_LMENU, scancode); }
                if (IsVkDown(VK_RMENU) == is_key_down) { ImGui_ImplWin32_AddKeyEvent(ImGuiKey_RightAlt, is_key_down, VK_RMENU, scancode); }
            }
        }
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        io.AddFocusEvent(msg == WM_SETFOCUS);
        return 0;
    case WM_INPUTLANGCHANGE:
        ImGui_ImplWin32_UpdateKeyboardCodePage();
        return 0;
    case WM_CHAR:
        if (::IsWindowUnicode(hwnd))
        {
            // You can also use ToAscii()+GetKeyboardState() to retrieve characters.
            if (wParam > 0 && wParam < 0x10000)
                io.AddInputCharacterUTF16((unsigned short)wParam);
        }
        else
        {
            wchar_t wch = 0;
            ::MultiByteToWideChar(bd->KeyboardCodePage, MB_PRECOMPOSED, (char*)&wParam, 1, &wch, 1);
            io.AddInputCharacter(wch);
        }
        return 0;
    case WM_SETCURSOR:
        // This is required to restore cursor when transitioning from e.g resize borders to client area.
        if (LOWORD(lParam) == HTCLIENT && ImGui_ImplWin32_UpdateMouseCursor())
            return 1;
        return 0;
    case WM_DEVICECHANGE:
#ifndef IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
        if ((UINT)wParam == DBT_DEVNODES_CHANGED)
            bd->WantUpdateHasGamepad = true;
#endif
        return 0;
    case WM_DISPLAYCHANGE:
        bd->WantUpdateMonitors = true;
        return 0;
    }
    return 0;
}


//--------------------------------------------------------------------------------------------------------
// DPI-related helpers (optional)
//--------------------------------------------------------------------------------------------------------
// - Use to enable DPI awareness without having to create an application manifest.
// - Your own app may already do this via a manifest or explicit calls. This is mostly useful for our examples/ apps.
// - In theory we could call simple functions from Windows SDK such as SetProcessDPIAware(), SetProcessDpiAwareness(), etc.
//   but most of the functions provided by Microsoft require Windows 8.1/10+ SDK at compile time and Windows 8/10+ at runtime,
//   neither we want to require the user to have. So we dynamically select and load those functions to avoid dependencies.
//---------------------------------------------------------------------------------------------------------
// This is the scheme successfully used by GLFW (from which we borrowed some of the code) and other apps aiming to be highly portable.
// ImGui_ImplWin32_EnableDpiAwareness() is just a helper called by main.cpp, we don't call it automatically.
// If you are trying to implement your own backend for your own engine, you may ignore that noise.
//---------------------------------------------------------------------------------------------------------

// Perform our own check with RtlVerifyVersionInfo() instead of using functions from <VersionHelpers.h> as they
// require a manifest to be functional for checks above 8.1. See https://github.com/ocornut/imgui/issues/4200
static BOOL _IsWindowsVersionOrGreater(WORD major, WORD minor, WORD)
{
    typedef LONG(WINAPI* PFN_RtlVerifyVersionInfo)(OSVERSIONINFOEXW*, ULONG, ULONGLONG);
    static PFN_RtlVerifyVersionInfo RtlVerifyVersionInfoFn = nullptr;
	if (RtlVerifyVersionInfoFn == nullptr)
		if (HMODULE ntdllModule = ::GetModuleHandleA("ntdll.dll"))
			RtlVerifyVersionInfoFn = (PFN_RtlVerifyVersionInfo)GetProcAddress(ntdllModule, "RtlVerifyVersionInfo");
    if (RtlVerifyVersionInfoFn == nullptr)
        return FALSE;

    RTL_OSVERSIONINFOEXW versionInfo = { };
    ULONGLONG conditionMask = 0;
    versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
    versionInfo.dwMajorVersion = major;
	versionInfo.dwMinorVersion = minor;
	VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
	return (RtlVerifyVersionInfoFn(&versionInfo, VER_MAJORVERSION | VER_MINORVERSION, conditionMask) == 0) ? TRUE : FALSE;
}

#define _IsWindowsVistaOrGreater()   _IsWindowsVersionOrGreater(HIBYTE(0x0600), LOBYTE(0x0600), 0) // _WIN32_WINNT_VISTA
#define _IsWindows8OrGreater()       _IsWindowsVersionOrGreater(HIBYTE(0x0602), LOBYTE(0x0602), 0) // _WIN32_WINNT_WIN8
#define _IsWindows8Point1OrGreater() _IsWindowsVersionOrGreater(HIBYTE(0x0603), LOBYTE(0x0603), 0) // _WIN32_WINNT_WINBLUE
#define _IsWindows10OrGreater()      _IsWindowsVersionOrGreater(HIBYTE(0x0A00), LOBYTE(0x0A00), 0) // _WIN32_WINNT_WINTHRESHOLD / _WIN32_WINNT_WIN10

#ifndef DPI_ENUMS_DECLARED
typedef enum { PROCESS_DPI_UNAWARE = 0, PROCESS_SYSTEM_DPI_AWARE = 1, PROCESS_PER_MONITOR_DPI_AWARE = 2 } PROCESS_DPI_AWARENESS;
typedef enum { MDT_EFFECTIVE_DPI = 0, MDT_ANGULAR_DPI = 1, MDT_RAW_DPI = 2, MDT_DEFAULT = MDT_EFFECTIVE_DPI } MONITOR_DPI_TYPE;
#endif
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    (DPI_AWARENESS_CONTEXT)-3
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (DPI_AWARENESS_CONTEXT)-4
#endif
typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(PROCESS_DPI_AWARENESS);                     // Shcore.lib + dll, Windows 8.1+
typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);        // Shcore.lib + dll, Windows 8.1+
typedef DPI_AWARENESS_CONTEXT(WINAPI* PFN_SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT); // User32.lib + dll, Windows 10 v1607+ (Creators Update)

// Helper function to enable DPI awareness without setting up a manifest
void ImGui_ImplWin32_EnableDpiAwareness()
{
    // Make sure monitors will be updated with latest correct scaling
    if (ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData())
        bd->WantUpdateMonitors = true;

    if (_IsWindows10OrGreater())
    {
        static HINSTANCE user32_dll = ::LoadLibraryA("user32.dll"); // Reference counted per-process
        if (PFN_SetThreadDpiAwarenessContext SetThreadDpiAwarenessContextFn = (PFN_SetThreadDpiAwarenessContext)::GetProcAddress(user32_dll, "SetThreadDpiAwarenessContext"))
        {
            SetThreadDpiAwarenessContextFn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    if (_IsWindows8Point1OrGreater())
    {
        static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll"); // Reference counted per-process
        if (PFN_SetProcessDpiAwareness SetProcessDpiAwarenessFn = (PFN_SetProcessDpiAwareness)::GetProcAddress(shcore_dll, "SetProcessDpiAwareness"))
        {
            SetProcessDpiAwarenessFn(PROCESS_PER_MONITOR_DPI_AWARE);
            return;
        }
    }
#if _WIN32_WINNT >= 0x0600
    ::SetProcessDPIAware();
#endif
}

#if defined(_MSC_VER) && !defined(NOGDI)
#pragma comment(lib, "gdi32")   // Link with gdi32.lib for GetDeviceCaps(). MinGW will require linking with '-lgdi32'
#endif

float ImGui_ImplWin32_GetDpiScaleForMonitor(void* monitor)
{
    UINT xdpi = 96, ydpi = 96;
    if (_IsWindows8Point1OrGreater())
    {
		static HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll"); // Reference counted per-process
		static PFN_GetDpiForMonitor GetDpiForMonitorFn = nullptr;
		if (GetDpiForMonitorFn == nullptr && shcore_dll != nullptr)
            GetDpiForMonitorFn = (PFN_GetDpiForMonitor)::GetProcAddress(shcore_dll, "GetDpiForMonitor");
		if (GetDpiForMonitorFn != nullptr)
		{
			GetDpiForMonitorFn((HMONITOR)monitor, MDT_EFFECTIVE_DPI, &xdpi, &ydpi);
            IM_ASSERT(xdpi == ydpi); // Please contact me if you hit this assert!
			return xdpi / 96.0f;
		}
    }
#ifndef NOGDI
    const HDC dc = ::GetDC(nullptr);
    xdpi = ::GetDeviceCaps(dc, LOGPIXELSX);
    ydpi = ::GetDeviceCaps(dc, LOGPIXELSY);
    IM_ASSERT(xdpi == ydpi); // Please contact me if you hit this assert!
    ::ReleaseDC(nullptr, dc);
#endif
    return xdpi / 96.0f;
}

float ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd)
{
    HMONITOR monitor = ::MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONEAREST);
    return ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
}

//---------------------------------------------------------------------------------------------------------
// Transparency related helpers (optional)
//--------------------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma comment(lib, "dwmapi")  // Link with dwmapi.lib. MinGW will require linking with '-ldwmapi'
#endif

// [experimental]
// Borrowed from GLFW's function updateFramebufferTransparency() in src/win32_window.c
// (the Dwm* functions are Vista era functions but we are borrowing logic from GLFW)
void ImGui_ImplWin32_EnableAlphaCompositing(void* hwnd)
{
    if (!_IsWindowsVistaOrGreater())
        return;

    BOOL composition;
    if (FAILED(::DwmIsCompositionEnabled(&composition)) || !composition)
        return;

    BOOL opaque;
    DWORD color;
    if (_IsWindows8OrGreater() || (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque)) && !opaque))
    {
        HRGN region = ::CreateRectRgn(0, 0, -1, -1);
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
        bb.hRgnBlur = region;
        bb.fEnable = TRUE;
        ::DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
        ::DeleteObject(region);
    }
    else
    {
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        ::DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
    }
}

//---------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
struct ImGui_ImplWin32_ViewportData
{
    HWND    Hwnd;
    HWND    HwndParent;
    bool    HwndOwned;
    DWORD   DwStyle;
    DWORD   DwExStyle;

#ifdef SKIF_Win32
    bool    RemovedDWMBorders;
#endif

    ImGui_ImplWin32_ViewportData() { Hwnd = HwndParent = nullptr; HwndOwned = false;  DwStyle = DwExStyle = 0;  RemovedDWMBorders = false; }
    ~ImGui_ImplWin32_ViewportData() { IM_ASSERT(Hwnd == nullptr); }
};

#ifndef SKIF_Win32
static void ImGui_ImplWin32_GetWin32StyleFromViewportFlags(ImGuiViewportFlags flags, DWORD* out_style, DWORD* out_ex_style)
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
}
#else
// HWND is used to tell if we're dealing with the main platform window or a child window
static void ImGui_ImplWin32_GetWin32StyleFromViewportFlags(ImGuiViewportFlags flags, DWORD *out_style, DWORD *out_ex_style, HWND owner)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  if (flags & ImGuiViewportFlags_NoDecoration)
    *out_style = WS_POPUP;   // Popups / Tooltips        (alternate look: WS_POPUPWINDOW, or WS_POPUP | WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX)
  else {
    *out_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX; // Main Window (WS_OVERLAPPEDWINDOW)
    
    // WS_MAXIMIZEBOX is necessary for drag/drop snapping to the edges of the monitor to function as expected
  }

  // WS_OVERLAPPEDWINDOW == WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
  // - WS_OVERLAPPED  - The window is an overlapped window          // not really required by SKIF
  // - WS_CAPTION     - The window has a title bar                  // not really required by SKIF
  // - WS_SYSMENU     - Window menu           (WndMenu -> Move      // alt+space, right click window on taskbar
  // - WS_THICKFRAME  - Resize grip on window (WndMenu -> Size)     // enables WinKey+Left/Right snapping
  // - WS_MINIMIZEBOX - Minimize button       (WndMenu -> Minimize) // enables WinKey+Down minimize shortcut
  // - WS_MAXIMIZEBOX - Maximize button       (WndMenu -> Maximize) // enables WinKey+Up maximize shortcut + drag/drop snapping

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
    //if (SKIF_nCmdShow == SW_SHOWMAXIMIZED)
    //  *out_style |= WS_MAXIMIZE;

    if (SKIF_nCmdShow == SW_SHOWMINIMIZED   ||
        SKIF_nCmdShow == SW_SHOWMINNOACTIVE ||
        SKIF_nCmdShow == SW_SHOWNOACTIVATE  ||
        SKIF_nCmdShow == SW_SHOWNA          ||
        SKIF_nCmdShow == SW_HIDE)
      *out_ex_style |= WS_EX_NOACTIVATE;
  }
}
#endif // !SKIF_Win32

static HWND ImGui_ImplWin32_GetHwndFromViewportID(ImGuiID viewport_id)
{
    if (viewport_id != 0)
        if (ImGuiViewport* viewport = ImGui::FindViewportByID(viewport_id))
            return (HWND)viewport->PlatformHandle;
    return nullptr;
}

#ifndef SKIF_Win32
static void ImGui_ImplWin32_CreateWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = IM_NEW(ImGui_ImplWin32_ViewportData)();
    viewport->PlatformUserData = vd;

    // Select style and parent window
    ImGui_ImplWin32_GetWin32StyleFromViewportFlags(viewport->Flags, &vd->DwStyle, &vd->DwExStyle);
    vd->HwndParent = ImGui_ImplWin32_GetHwndFromViewportID(viewport->ParentViewportId);

    // Create window
    RECT rect = { (LONG)viewport->Pos.x, (LONG)viewport->Pos.y, (LONG)(viewport->Pos.x + viewport->Size.x), (LONG)(viewport->Pos.y + viewport->Size.y) };
    ::AdjustWindowRectEx(&rect, vd->DwStyle, FALSE, vd->DwExStyle);
    vd->Hwnd = ::CreateWindowEx(
        vd->DwExStyle, _T("ImGui Platform"), _T("Untitled"), vd->DwStyle,       // Style, class name, window name
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,    // Window area
        vd->HwndParent, nullptr, ::GetModuleHandle(nullptr), nullptr);          // Owner window, Menu, Instance, Param
    vd->HwndOwned = true;
    viewport->PlatformRequestResize = false;
    viewport->PlatformHandle = viewport->PlatformHandleRaw = vd->Hwnd;
}
#else
static void ImGui_ImplWin32_CreateWindow(ImGuiViewport *viewport)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  extern float SKIF_ImGui_GlobalDPIScale;

  ImGui_ImplWin32_ViewportData* vd = IM_NEW(ImGui_ImplWin32_ViewportData)();
  viewport->PlatformUserData = vd;

  // Select style and parent window
  vd->HwndParent = ImGui_ImplWin32_GetHwndFromViewportID(viewport->ParentViewportId);
  ImGui_ImplWin32_GetWin32StyleFromViewportFlags(viewport->Flags, &vd->DwStyle, &vd->DwExStyle, vd->HwndParent);

  // Create window
  RECT rect = { (LONG)viewport->Pos.x, (LONG)viewport->Pos.y, (LONG)(viewport->Pos.x + viewport->Size.x), (LONG)(viewport->Pos.y + viewport->Size.y) };
  //::AdjustWindowRectEx(&rect, vd->DwStyle, FALSE, vd->DwExStyle);
  vd->Hwnd = ::CreateWindowEx(
      vd->DwExStyle, SKIF_ImGui_WindowClass, SKIF_ImGui_WindowTitle, vd->DwStyle,       // Style, class name, window name
      rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,    // Window area
      vd->HwndParent, nullptr, ::GetModuleHandle(nullptr), nullptr);          // Owner window, Menu, Instance, Param
  vd->HwndOwned = true;
  viewport->PlatformRequestResize = false;
  viewport->PlatformHandle = viewport->PlatformHandleRaw = vd->Hwnd;
  
  // We need to store the first window in the backend / globally
  ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
  if (bd->hWnd == nullptr || SKIF_ImGui_hWnd == NULL)
  {
    // Store the handle globally
    bd->hWnd        = vd->Hwnd;
    SKIF_ImGui_hWnd = bd->hWnd;

    // Update the main viewport as well, since that's apparently also required
    //ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    //main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (void*)bd->hWnd;

    // Retrieve the DPI scaling of the current display
    SKIF_ImGui_GlobalDPIScale = (_registry.bDPIScaling) ? ImGui_ImplWin32_GetDpiScaleForHwnd (vd->Hwnd) : 1.0f;

    // Update the style scaling to reflect the current DPI scaling
    ImGuiStyle              newStyle;
    extern void
      SKIF_ImGui_SetStyle (ImGuiStyle * dst = nullptr);
      SKIF_ImGui_SetStyle (&newStyle);
  }

  // Add icons to the window
  extern HICON hIcon;
#define GCL_HICON (-14)

  SendMessage      (vd->Hwnd, WM_SETICON, ICON_BIG,        (LPARAM)hIcon);
  SendMessage      (vd->Hwnd, WM_SETICON, ICON_SMALL,      (LPARAM)hIcon);
  SendMessage      (vd->Hwnd, WM_SETICON, ICON_SMALL2,     (LPARAM)hIcon);
  SetClassLongPtrW (vd->Hwnd, GCL_HICON,         (LONG_PTR)(LPARAM)hIcon);
}
#endif // !SKIF_Win32

static void ImGui_ImplWin32_DestroyWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    if (ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData)
    {
        if (::GetCapture() == vd->Hwnd)
        {
            // Transfer capture so if we started dragging from a window that later disappears, we'll still receive the MOUSEUP event.
            ::ReleaseCapture();
            ::SetCapture(bd->hWnd);
        }

#ifdef SKIF_Win32
        // If this is the main platform window, reset the global handle for it
        if (vd->Hwnd == SKIF_ImGui_hWnd)
        {
          SKIF_ImGui_hWnd = NULL;
          bd->hWnd = NULL;
        }
#endif

        if (vd->Hwnd && vd->HwndOwned)
            ::DestroyWindow(vd->Hwnd);
        vd->Hwnd = nullptr;
        IM_DELETE(vd);
    }
    viewport->PlatformUserData = viewport->PlatformHandle = nullptr;
}

#ifndef SKIF_Win32
static void ImGui_ImplWin32_ShowWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    IM_ASSERT(vd->Hwnd != 0);

    // ShowParent() also brings parent to front, which is not always desirable,
    // so we temporarily disable parenting. (#7354)
    if (vd->HwndParent != NULL)
        ::SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)nullptr);

    if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
        ::ShowWindow(vd->Hwnd, SW_SHOWNA);
    else
        ::ShowWindow(vd->Hwnd, SW_SHOW);

    // Restore
    if (vd->HwndParent != NULL)
        ::SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)vd->HwndParent);
}
#else
static void ImGui_ImplWin32_ShowWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return;
    IM_ASSERT(vd->Hwnd != 0);

    // ShowParent() also brings parent to front, which is not always desirable,
    // so we temporarily disable parenting. (#7354)
    if (vd->HwndParent != NULL)
        ::SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)nullptr);

    PLOG_VERBOSE << "SKIF_nCmdShow is set to: " << SKIF_nCmdShow;

    // Main platform window must respect nCmdShow
    //  in the first ShowWindow() call
    if (SKIF_nCmdShow   != -1   &&
        SKIF_ImGui_hWnd == vd->Hwnd)
    {
      ::ShowWindow (vd->Hwnd, SKIF_nCmdShow);
      SKIF_nCmdShow = -1; // SKIF_nCmdShow has served its purpose by now
    }
    else if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
      ::ShowWindow (vd->Hwnd, SW_SHOWNA);
    else
      ::ShowWindow (vd->Hwnd, SW_SHOW);

    // Restore
    if (vd->HwndParent != NULL)
        ::SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)vd->HwndParent);
}
#endif // !SKIF_Win32

#ifndef SKIF_Win32
static void ImGui_ImplWin32_UpdateWindow(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);

    // Update Win32 parent if it changed _after_ creation
    // Unlike style settings derived from configuration flags, this is more likely to change for advanced apps that are manipulating ParentViewportID manually.
    HWND new_parent = ImGui_ImplWin32_GetHwndFromViewportID(viewport->ParentViewportId);
    if (new_parent != vd->HwndParent)
    {
        // Win32 windows can either have a "Parent" (for WS_CHILD window) or an "Owner" (which among other thing keeps window above its owner).
        // Our Dear Imgui-side concept of parenting only mostly care about what Win32 call "Owner".
        // The parent parameter of CreateWindowEx() sets up Parent OR Owner depending on WS_CHILD flag. In our case an Owner as we never use WS_CHILD.
        // Calling ::SetParent() here would be incorrect: it will create a full child relation, alter coordinate system and clipping.
        // Calling ::SetWindowLongPtr() with GWLP_HWNDPARENT seems correct although poorly documented.
        // https://devblogs.microsoft.com/oldnewthing/20100315-00/?p=14613
        vd->HwndParent = new_parent;
        ::SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)vd->HwndParent);
    }

    // (Optional) Update Win32 style if it changed _after_ creation.
    // Generally they won't change unless configuration flags are changed, but advanced uses (such as manually rewriting viewport flags) make this useful.
    DWORD new_style;
    DWORD new_ex_style;
    ImGui_ImplWin32_GetWin32StyleFromViewportFlags(viewport->Flags, &new_style, &new_ex_style, vd->HwndParent); // vd->HwndParent

    // Only reapply the flags that have been changed from our point of view (as other flags are being modified by Windows)
    if (vd->DwStyle != new_style || vd->DwExStyle != new_ex_style)
    {
        // (Optional) Update TopMost state if it changed _after_ creation
        bool top_most_changed = (vd->DwExStyle & WS_EX_TOPMOST) != (new_ex_style & WS_EX_TOPMOST);
        HWND insert_after = top_most_changed ? ((viewport->Flags & ImGuiViewportFlags_TopMost) ? HWND_TOPMOST : HWND_NOTOPMOST) : 0;
        UINT swp_flag = top_most_changed ? 0 : SWP_NOZORDER;

        // Apply flags and position (since it is affected by flags)
        vd->DwStyle = new_style;
        vd->DwExStyle = new_ex_style;
        ::SetWindowLong(vd->Hwnd, GWL_STYLE, vd->DwStyle);
        ::SetWindowLong(vd->Hwnd, GWL_EXSTYLE, vd->DwExStyle);
        RECT rect = { (LONG)viewport->Pos.x, (LONG)viewport->Pos.y, (LONG)(viewport->Pos.x + viewport->Size.x), (LONG)(viewport->Pos.y + viewport->Size.y) };
        ::AdjustWindowRectEx(&rect, vd->DwStyle, FALSE, vd->DwExStyle); // Client to Screen
        ::SetWindowPos(vd->Hwnd, insert_after, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, swp_flag | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ::ShowWindow(vd->Hwnd, SW_SHOWNA); // This is necessary when we alter the style
        viewport->PlatformRequestMove = viewport->PlatformRequestResize = true;
    }
}
#else
static void ImGui_ImplWin32_UpdateWindow (ImGuiViewport *viewport)
{
  ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
  if (vd->Hwnd == 0) return;
  IM_ASSERT(vd->Hwnd != 0);

  // Update Win32 parent if it changed _after_ creation
  // Unlike style settings derived from configuration flags, this is more likely to change for advanced apps that are manipulating ParentViewportID manually.
  HWND new_parent = ImGui_ImplWin32_GetHwndFromViewportID(viewport->ParentViewportId);
  if (new_parent != vd->HwndParent)
  {
      // Win32 windows can either have a "Parent" (for WS_CHILD window) or an "Owner" (which among other thing keeps window above its owner).
      // Our Dear Imgui-side concept of parenting only mostly care about what Win32 call "Owner".
      // The parent parameter of CreateWindowEx() sets up Parent OR Owner depending on WS_CHILD flag. In our case an Owner as we never use WS_CHILD.
      // Calling ::SetParent() here would be incorrect: it will create a full child relation, alter coordinate system and clipping.
      // Calling ::SetWindowLongPtr() with GWLP_HWNDPARENT seems correct although poorly documented.
      // https://devblogs.microsoft.com/oldnewthing/20100315-00/?p=14613
      vd->HwndParent = new_parent;
      ::SetWindowLongPtr(vd->Hwnd, GWLP_HWNDPARENT, (LONG_PTR)vd->HwndParent);
  }

  DWORD new_style;
  DWORD new_ex_style;

  ImGui_ImplWin32_GetWin32StyleFromViewportFlags (viewport->Flags, &new_style, &new_ex_style, new_parent);

  static bool hasNoRedirectionBitmap = (bool)(vd->DwExStyle & WS_EX_NOREDIRECTIONBITMAP);

  // Only reapply the flags that have been changed from our point of view (as other flags are being modified by Windows)
  if ( vd->DwStyle   != new_style ||
       vd->DwExStyle != new_ex_style )
  {
    // (Optional) Update TopMost state if it changed _after_ creation
    bool top_most_changed = (vd->DwExStyle & WS_EX_TOPMOST) != (new_ex_style & WS_EX_TOPMOST);
    HWND insert_after = top_most_changed ? ((viewport->Flags & ImGuiViewportFlags_TopMost) ? HWND_TOPMOST : HWND_NOTOPMOST) : 0;
    UINT swp_flag = top_most_changed ? 0 : SWP_NOZORDER;

    vd->DwStyle   = new_style;
    vd->DwExStyle = new_ex_style;

    ::SetWindowLongPtrW ( vd->Hwnd, GWL_STYLE,   vd->DwStyle  );
    ::SetWindowLongPtrW ( vd->Hwnd, GWL_EXSTYLE, vd->DwExStyle);

    // Force recreating the window if the NoRedirectionBitmap flag has changed
    if (hasNoRedirectionBitmap != (bool)(vd->DwExStyle & WS_EX_NOREDIRECTIONBITMAP))
    {   hasNoRedirectionBitmap  = (bool)(vd->DwExStyle & WS_EX_NOREDIRECTIONBITMAP);
      ImGuiViewportP* viewportP =
        static_cast <ImGuiViewportP*> (
                          viewport
        );
      viewportP->LastFrameActive = 0;
      
      // RecreateWin32Windows cannot be used here for some reason?
      //extern bool RecreateWin32Windows;
      //RecreateWin32Windows = true;
    }

    // If we are being launched maximized, don't resize ourselves
    if ((vd->DwStyle & WS_MAXIMIZE) != WS_MAXIMIZE)
      swp_flag |= SWP_NOSIZE | SWP_NOMOVE;

    // Respect the visiblity state
    swp_flag |= ((GetWindowLongPtrW (vd->Hwnd, GWL_STYLE) & WS_VISIBLE) == WS_VISIBLE) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;

    RECT rect =
    { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
      (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
    };

    //::AdjustWindowRectEx ( &rect, vd->DwStyle,
    //                       FALSE, vd->DwExStyle ); // Client to Screen

    ::SetWindowPos       ( vd->Hwnd, insert_after,
                                           rect.left,               rect.top,
                              rect.right - rect.left, rect.bottom - rect.top,
                    swp_flag | SWP_NOZORDER     | SWP_NOACTIVATE |
                               SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS );

    // A ShowWindow() call is necessary when we alter the style
    // Note that this cannot be called for hidden windows, as it would end up revealing them
    //::ShowWindow ( vd->Hwnd, SW_SHOWNA );

    viewport->PlatformRequestMove =
      viewport->PlatformRequestResize = true;
  }
  
  // Run only once per window -- to remove the Standard Frame of DWM windows
  ///* 2023-07-31: Not needed any longer as its handled in WM_CREATE instead
  //   CORRECTION: Still needed
  else if ((viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0 &&
          ! vd->RemovedDWMBorders)
  {
    vd->RemovedDWMBorders = true;

    RECT rect =
    { (LONG)  viewport->Pos.x,                      (LONG)  viewport->Pos.y,
      (LONG)( viewport->Pos.x + viewport->Size.x ), (LONG)( viewport->Pos.y + viewport->Size.y )
    };

    ::SetWindowPos       ( vd->Hwnd, nullptr,
                                          rect.left,               rect.top,
                             rect.right - rect.left, rect.bottom - rect.top,
                               SWP_FRAMECHANGED );
  }
  //*/
}
#endif

static ImVec2 ImGui_ImplWin32_GetWindowPos(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return ImVec2{ }; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    POINT pos = { 0, 0 };
    ::ClientToScreen(vd->Hwnd, &pos);
    return ImVec2((float)pos.x, (float)pos.y);
}

static void ImGui_ImplWin32_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    RECT rect = { (LONG)pos.x, (LONG)pos.y, (LONG)pos.x, (LONG)pos.y };
  //::AdjustWindowRectEx(&rect, vd->DwStyle, FALSE, vd->DwExStyle);
    ::SetWindowPos(vd->Hwnd, nullptr, rect.left, rect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

static ImVec2 ImGui_ImplWin32_GetWindowSize(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return ImVec2{ }; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    RECT rect;
    ::GetClientRect(vd->Hwnd, &rect);
    return ImVec2(float(rect.right - rect.left), float(rect.bottom - rect.top));
}

static void ImGui_ImplWin32_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    RECT rect = { 0, 0, (LONG)size.x, (LONG)size.y };
  //::AdjustWindowRectEx(&rect, vd->DwStyle, FALSE, vd->DwExStyle); // Client to Screen
    ::SetWindowPos(vd->Hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
#ifdef SKIF_Win32
    ImGuiViewportP* viewportP = (ImGuiViewportP*)viewport;
    PLOG_VERBOSE << "[" << ImGui::GetFrameCount() << "] Resized window to " << size.x << "x" << size.y;
    if (viewportP->LastPlatformSize != ImVec2(FLT_MAX, FLT_MAX))
      PLOG_VERBOSE << "Last platform window size: " << std::to_string(viewportP->LastPlatformSize.x) << "x" << std::to_string(viewportP->LastPlatformSize.y);
#endif
}

static void ImGui_ImplWin32_SetWindowFocus(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    ::BringWindowToTop(vd->Hwnd);
    ::SetForegroundWindow(vd->Hwnd);
    ::SetFocus(vd->Hwnd);
}

static bool ImGui_ImplWin32_GetWindowFocus(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return false; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    return ::GetForegroundWindow() == vd->Hwnd;
}

static bool ImGui_ImplWin32_GetWindowMinimized(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return false; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    return ::IsIconic(vd->Hwnd) != 0;
}

static void ImGui_ImplWin32_SetWindowTitle(ImGuiViewport* viewport, const char* title)
{
    // ::SetWindowTextA() doesn't properly handle UTF-8 so we explicitely convert our string.
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return; // SKIF CUSTOM
    IM_ASSERT(vd->Hwnd != 0);
    int n = ::MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    ImVector<wchar_t> title_w;
    title_w.resize(n);
    ::MultiByteToWideChar(CP_UTF8, 0, title, -1, title_w.Data, n);
    ::SetWindowTextW(vd->Hwnd, title_w.Data);
}

static void ImGui_ImplWin32_SetWindowAlpha(ImGuiViewport* viewport, float alpha)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return;
    IM_ASSERT(vd->Hwnd != 0);
    IM_ASSERT(alpha >= 0.0f && alpha <= 1.0f);
    if (alpha < 1.0f)
    {
        DWORD style = ::GetWindowLongW(vd->Hwnd, GWL_EXSTYLE) | WS_EX_LAYERED;
        ::SetWindowLongW(vd->Hwnd, GWL_EXSTYLE, style);
        ::SetLayeredWindowAttributes(vd->Hwnd, 0, (BYTE)(255 * alpha), LWA_ALPHA);
    }
    else
    {
        DWORD style = ::GetWindowLongW(vd->Hwnd, GWL_EXSTYLE) & ~WS_EX_LAYERED;
        ::SetWindowLongW(vd->Hwnd, GWL_EXSTYLE, style);
    }
}

static float ImGui_ImplWin32_GetWindowDpiScale(ImGuiViewport* viewport)
{
    ImGui_ImplWin32_ViewportData* vd = (ImGui_ImplWin32_ViewportData*)viewport->PlatformUserData;
    if (vd->Hwnd == 0) return 1.0f;
    IM_ASSERT(vd->Hwnd != 0);
    return ImGui_ImplWin32_GetDpiScaleForHwnd(vd->Hwnd);
}

// FIXME-DPI: Testing DPI related ideas
static void ImGui_ImplWin32_OnChangedViewport(ImGuiViewport* viewport)
{
    (void)viewport;
#if 0
    ImGuiStyle default_style;
    //default_style.WindowPadding = ImVec2(0, 0);
    //default_style.WindowBorderSize = 0.0f;
    //default_style.ItemSpacing.y = 3.0f;
    //default_style.FramePadding = ImVec2(0, 0);
    default_style.ScaleAllSizes(viewport->DpiScale);
    ImGuiStyle& style = ImGui::GetStyle();
    style = default_style;
#endif
}

static LRESULT CALLBACK ImGui_ImplWin32_WndProcHandler_PlatformWindow(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

#ifdef SKIF_Win32

    static SKIF_RegistrySettings&   _registry  = SKIF_RegistrySettings  ::GetInstance ( );
    static SKIF_InjectionContext&   _inject    = SKIF_InjectionContext  ::GetInstance ( );
    static SKIF_GamePadInputHelper& _gamepad   = SKIF_GamePadInputHelper::GetInstance ( );
    static SKIF_DropTargetObject&   _drag_drop = SKIF_DropTargetObject  ::GetInstance ( );

    extern float  SKIF_ImGui_GlobalDPIScale;
    extern float  SKIF_ImGui_GlobalDPIScale_Last;
    extern ImVec2 SKIF_vecRegularModeDefault;  // Does not include the status bar
    extern ImVec2 SKIF_vecRegularModeAdjusted; // Adjusted for status bar and tooltips
    extern ImVec2 SKIF_vecHorizonModeDefault;  // Does not include the status bar
    extern ImVec2 SKIF_vecHorizonModeAdjusted; // Adjusted for status bar and tooltips
    extern ImVec2 SKIF_vecServiceModeDefault;
    extern ImVec2 SKIF_vecServiceMode;
    extern ImVec2 SKIF_vecCurrentMode;
    extern ImVec2 SKIF_vecCurrentModeNext;
    extern HWND   SKIF_Notify_hWnd;

    extern bool msgDontRedraw;

    extern bool KeyWinKey;
    extern int  SnapKeys;

    PLOG_VERBOSE_IF(_registry.isDevLogging()) << std::format("[{:0>5d}] [0x{:<4x}] [{:5d}] [{:20s}]{:s}[0x{:x}, {:d}{:s}] [0x{:x}, {:d}]",
                      ImGui::GetFrameCount(),
                      msg, // Hexadecimal
                      msg, // Decimal
                      SKIF_Util_GetWindowMessageAsStr (msg), // String
                       (hWnd == SKIF_ImGui_hWnd ?  " [SKIF_ImGui_hWnd ] " : " "), // Is the message meant SKIF_ImGui_hWnd ?
                      wParam, wParam,
               ((HWND)wParam == SKIF_ImGui_hWnd ?  ", SKIF_ImGui_hWnd"   : ""),  // Does wParam point to SKIF_ImGui_hWnd ?
                      lParam, lParam);

#define SKIF_MAXIMIZE_POS 27     // 27
#define SWP_STATECHANGED  0x8000 // Undocumented
    static bool moveModal = false;

#endif // SKIF_Win32

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

#ifdef SKIF_Win32

    switch (msg)
    {
    case WM_GETICON: // Work around bug in Task Manager sending this message every time it refreshes its process list
      msgDontRedraw = true;
      break;

    case WM_SYSCOMMAND:
    {
      // Disable the window menu (Alt+Space, etc) when SKIF is focused
      if (wParam == SC_KEYMENU && SKIF_ImGui_ImplWin32_IsFocused ( )) // && (lParam == 0x00 || lParam == 0x20))
        return true;

      break;
    }

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
    case WM_NCCALCSIZE:
      // Removes the Standard Frame of DWM windows
      if (wParam == TRUE)
        return 0;
      break;

      // For some reason this causes issues with focus not being properly gained on launch
    case WM_CREATE:
      SKIF_ImGui_ImplWin32_SetDWMBorders (hWnd);
      break;

    case WM_DESTROY:
      // Unregister any existing drop targets when the window is destroyed
      _drag_drop.Revoke (hWnd);
    }

    if (ImGuiViewport *viewport = ImGui::FindViewportByPlatformHandle ((void *)hWnd))
    {
      switch (msg)
      {

//#define SKIF_Win32_GetDPIScaledSize
#ifdef SKIF_Win32_GetDPIScaledSize
      // Calculate the new suggested size of the viewport on DPI changes
      case WM_GETDPISCALEDSIZE:
      {
        SIZE* size = reinterpret_cast<SIZE*> (lParam);

        // If we have disabled DPI scaling, use our existing size (scale non-linearly)
        if (! _registry.bDPIScaling)
        {
          size->cx = (LONG)viewport->Size.x;
          size->cy = (LONG)viewport->Size.y;

          // We have computed a new size
          return 1;
        }

        // Apply the default linear DPI scaling to the window
        return 0;
      }
#endif

//#define SKIF_Win32_Snapping
#ifdef SKIF_Win32_Snapping

        // Gets fired on window creation, and screws up the positioning
        case WM_WINDOWPOSCHANGING:
        {
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
                ImVec2 tmpAlteredSize  = ImVec2 (0.0f, 0.0f);
                ImVec2 tmpCurrentSize  = (_registry.bMiniMode) ? SKIF_vecServiceModeDefault  :
                                         (_registry.bHorizonMode) ? SKIF_vecHorizonModeAdjusted :
                                                                    SKIF_vecRegularModeAdjusted ;

                float targetDPI = (_registry.bDPIScaling) ? targetMonitor.DpiScale : 1.0f;
                tmpExpectedSize = tmpCurrentSize * targetDPI;

                if (! _registry.bMiniMode)
                {
                  // Needed to account for an altered size on the target display
                  if (tmpCurrentSize.y * targetDPI > targetWorkArea.Max.y)
                    tmpAlteredSize.y = (tmpCurrentSize.y * targetDPI - targetWorkArea.Max.y); // Crop the regular mode(s)

                  tmpExpectedSize.y -= tmpAlteredSize.y;
                }

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

#endif

        // This is used to inform Windows of the min/max size of the viewport, so
        //   that features such as Aero Snap takes the enforced size into account
        case WM_GETMINMAXINFO:
        {
          MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);

          // The minimum tracking size is the smallest window size that can be produced by using the borders to size the window.
          // For SKIF that's the service/mini mode size.
          mmi->ptMinTrackSize.x = static_cast<long> (SKIF_vecServiceMode.x);
          mmi->ptMinTrackSize.y = static_cast<long> (SKIF_vecServiceMode.y);

//#define SKIF_Win32_CenterMaximize
#ifdef SKIF_Win32_CenterMaximize
          static POINT sizeMin, sizeMax, pos;

          // For systems with multiple monitors, the ptMaxSize and ptMaxPosition members describe the maximized size and position of the window on the primary monitor,
          // even if the window ultimately maximizes onto a secondary monitor. In that case, the window manager adjusts these values to compensate for differences between
          // the primary monitor and the monitor that displays the window.
          // 
          // ImGui_ImplWin32_UpdateMonitors_EnumFunc() always pushes the primary monitor to the front of ImGui::GetPlatformIO().Monitors

          sizeMax.x = static_cast<long> (viewport->Size.x);
          sizeMax.y = static_cast<long> (viewport->Size.y);

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

#endif
          //return 0; // We don't return 0 to allow DefWindowProc() the opportunity to also process the message
          break;
        }

        // Windows 10, version 1703+
        // 
        // The function returns a BOOL.
        //   - Returning TRUE indicates that a new size has been computed.
        //   - Returning FALSE indicates that the message will not be handled, and the default linear DPI scaling will apply to the window.
        // 
        // There is no specific default handling of this message in DefWindowProc.
        //   - As for all messages it does not explicitly handle, DefWindowProc will return zero for this message.
        //   - As noted above, this return tells the system to use the default linear behavior.
        case WM_GETDPISCALEDSIZE:
        {
          // Return TRUE if HiDPI is disabled as this allows us to retain our window size
          return (! _registry.bDPIScaling);
          break;
        }

        case WM_DPICHANGED:
        {
          // Only process if we actually follow HiDPI
          if (! _registry.bDPIScaling)
            return 0;

          int g_dpi    = HIWORD (wParam);
          RECT* const prcNewWindow = (RECT*) lParam;

          // Update the style scaling
          SKIF_ImGui_GlobalDPIScale = (float) g_dpi / USER_DEFAULT_SCREEN_DPI;

          ImGuiStyle              newStyle;
          extern void
            SKIF_ImGui_SetStyle (ImGuiStyle * dst = nullptr);
            SKIF_ImGui_SetStyle (&newStyle);

          extern bool
            invalidateFonts;
            invalidateFonts = true;

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

            ImVec2 tmpCurrentSize  = (_registry.bMiniMode) ? SKIF_vecServiceModeDefault  :
                                     (_registry.bHorizonMode) ? SKIF_vecHorizonModeAdjusted :
                                                                SKIF_vecRegularModeAdjusted ;

            //if (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
            //  SKIF_vecAlteredSize.y = (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y)); // (WorkSize.y - 50.0f);

            // Divide the window size with its associated DPI scale to get the base size, then multiply with the new DPI scale
            SKIF_vecCurrentModeNext = (SKIF_vecCurrentMode / SKIF_ImGui_GlobalDPIScale_Last) * SKIF_ImGui_GlobalDPIScale;

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
              prcNewWindow->left = info.rcWork.right - static_cast<long> (tmpCurrentSize.x * SKIF_ImGui_GlobalDPIScale);
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

        case WM_MOUSEACTIVATE:
          if (viewport->Flags & ImGuiViewportFlags_NoFocusOnClick)
            return MA_NOACTIVATE;
          break;

        case WM_ENTERSIZEMOVE:
        case WM_EXITSIZEMOVE:
          moveModal = ! moveModal;
          break;

        case WM_NCHITTEST:
        {
          // Let mouse pass-through the window. This will allow the back-end to set io.MouseHoveredViewport properly (which is OPTIONAL).
          // The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
          // If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
          // your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
          if (viewport->Flags & ImGuiViewportFlags_NoInputs)
            return HTTRANSPARENT;

          LRESULT hitTest =
            DefWindowProc (hWnd, msg, wParam, lParam);

          // Note that the touch input mode never uses the OS native move modal !
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
          //if (! _registry.bMaximizeOnDoubleClick || ! SKIF_Util_GetDragFromMaximized ( ))
            msg = WM_NCLBUTTONDOWN;
          break;
        }

        default:
        {
          extern UINT SHELL_TASKBAR_BUTTON_CREATED;
          // When the taskbar button has been created,
          //   the icon overlay can be set accordingly
          if (msg == SHELL_TASKBAR_BUTTON_CREATED)
            _inject._SetTaskbarOverlay (_inject.bCurrentState);
          break;
        }
      }
    }

#else

    if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle((void*)hWnd))
    {
        switch (msg)
        {
        case WM_CLOSE:
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
            // Let mouse pass-through the window. This will allow the backend to call io.AddMouseViewportEvent() correctly. (which is optional).
            // The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
            // If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
            // your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
            if (viewport->Flags & ImGuiViewportFlags_NoInputs)
                return HTTRANSPARENT;
            break;
        }
    }

#endif // SKIF_Win32

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void ImGui_ImplWin32_InitPlatformInterface(bool platform_has_own_dc)
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | (platform_has_own_dc ? CS_OWNDC : 0);
    wcex.lpfnWndProc = ImGui_ImplWin32_WndProcHandler_PlatformWindow;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = ::GetModuleHandle(nullptr);
    wcex.hIcon = nullptr;
    wcex.hCursor = nullptr;
#ifdef SKIF_Win32
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // COLOR_BACKGROUND is unsupported on Win10+
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = SKIF_ImGui_WindowClass;
    HMODULE hModHost =
      ::GetModuleHandle (nullptr);
     wcex.hIcon         =
       LoadIcon (hModHost, MAKEINTRESOURCE (IDI_SKIF));
     wcex.hIconSm       =
       LoadIcon (hModHost, MAKEINTRESOURCE (IDI_SKIF));
#else
    wcex.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = _T("ImGui Platform");
    wcex.hIconSm = nullptr;
#endif // SKIF_Win32
    ::RegisterClassEx(&wcex);

    ImGui_ImplWin32_UpdateMonitors();

    // Register platform interface (will be coupled with a renderer interface)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_CreateWindow = ImGui_ImplWin32_CreateWindow;
    platform_io.Platform_DestroyWindow = ImGui_ImplWin32_DestroyWindow;
    platform_io.Platform_ShowWindow = ImGui_ImplWin32_ShowWindow;
    platform_io.Platform_SetWindowPos = ImGui_ImplWin32_SetWindowPos;
    platform_io.Platform_GetWindowPos = ImGui_ImplWin32_GetWindowPos;
    platform_io.Platform_SetWindowSize = ImGui_ImplWin32_SetWindowSize;
    platform_io.Platform_GetWindowSize = ImGui_ImplWin32_GetWindowSize;
    platform_io.Platform_SetWindowFocus = ImGui_ImplWin32_SetWindowFocus;
    platform_io.Platform_GetWindowFocus = ImGui_ImplWin32_GetWindowFocus;
    platform_io.Platform_GetWindowMinimized = ImGui_ImplWin32_GetWindowMinimized;
    platform_io.Platform_SetWindowTitle = ImGui_ImplWin32_SetWindowTitle;
    platform_io.Platform_SetWindowAlpha = ImGui_ImplWin32_SetWindowAlpha;
    platform_io.Platform_UpdateWindow = ImGui_ImplWin32_UpdateWindow;
    platform_io.Platform_GetWindowDpiScale = ImGui_ImplWin32_GetWindowDpiScale; // FIXME-DPI
    platform_io.Platform_OnChangedViewport = ImGui_ImplWin32_OnChangedViewport; // FIXME-DPI

    // Register main window handle (which is owned by the main application, not by us)
    // This is mostly for simplicity and consistency, so that our code (e.g. mouse handling etc.) can use same logic for main and secondary viewports.
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui_ImplWin32_Data* bd = ImGui_ImplWin32_GetBackendData();
    ImGui_ImplWin32_ViewportData* vd = IM_NEW(ImGui_ImplWin32_ViewportData)();
    vd->Hwnd = bd->hWnd;
    vd->HwndOwned = false;
    main_viewport->PlatformUserData = vd;
    main_viewport->PlatformHandle = (void*)bd->hWnd;
}

static void ImGui_ImplWin32_ShutdownPlatformInterface()
{
#ifdef SKIF_Win32
    ::UnregisterClass(SKIF_ImGui_WindowClass, ::GetModuleHandle(nullptr));
#else
    ::UnregisterClass(_T("ImGui Platform"), ::GetModuleHandle(nullptr));
#endif // SKIF_Win32

    ImGui::DestroyPlatformWindows();
}

//---------------------------------------------------------------------------------------------------------
// SKIF CUSTOM WIN32 IMPLEMENTATION
//---------------------------------------------------------------------------------------------------------

#include <imgui/imgui_internal.h>

void
SKIF_ImGui_ImplWin32_SetDWMBorders (void* hWnd)
{
  if (! hWnd)
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
  
  if (SKIF_ImGui_hWnd ==       NULL ||
      SKIF_ImGui_hWnd == (HWND)hWnd)
    dwmCornerPreference = DWMWCP_ROUND;      // Main window
  else
    dwmCornerPreference = DWMWCP_ROUNDSMALL; // Popups (spanning outside of the main window)

  ::DwmSetWindowAttribute ((HWND)hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &dwmCornerPreference, sizeof (dwmCornerPreference));
  ::DwmSetWindowAttribute ((HWND)hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE,  &dwmUseDarkMode,      sizeof (dwmUseDarkMode));
  ::DwmSetWindowAttribute ((HWND)hWnd, DWMWA_BORDER_COLOR,             &dwmBorderColor,      sizeof (dwmBorderColor));
}

void
SKIF_ImGui_ImplWin32_UpdateDWMBorders (void)
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
      SKIF_ImGui_ImplWin32_SetDWMBorders (viewport->PlatformHandleRaw);
  }
}

// Peripheral Functions
bool SKIF_ImGui_ImplWin32_IsFocused (void)
{
  // AppFocusLost can not be relied upon as it is not persistent across frames (always set to false; app has focus by ImGui::EndFrame)
  //ImGuiContext& g = *GImGui;
  //return ! g.IO.AppFocusLost;

  static bool g_Focused = false; // Always assume we don't have focus on launch

  // Semi-new workaround introduced in 2024-01-28

  static HWND hWndLastForeground = 0;

  // Execute once per frame
  int newFrame   = ImGui::GetFrameCount ( );
  static int
      lastFrame  = 0;
  if (lastFrame != newFrame)
  {   lastFrame  = newFrame;
    HWND focused_hwnd = ::GetForegroundWindow ();

    // If the focused HWND has not changed, this is just burning CPU cycles for no reason
    if (std::exchange (hWndLastForeground, focused_hwnd) != focused_hwnd)
    {
      DWORD
        dwWindowOwnerPid = 0;

      GetWindowThreadProcessId (
        focused_hwnd,
          &dwWindowOwnerPid
      );

      static DWORD
        dwPidOfMe = GetCurrentProcessId ();

      g_Focused = (dwWindowOwnerPid == dwPidOfMe);
    }
  }

  return g_Focused;
}

static ImGuiPlatformMonitor*
SKIF_ImGui_ImplWin32_GetPlatformMonitor (ImGuiViewport* viewport, bool center)
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

ImGuiPlatformMonitor*
SKIF_ImGui_ImplWin32_GetPlatformMonitorProxy (ImGuiViewport* viewport, bool center)
{
  return SKIF_ImGui_ImplWin32_GetPlatformMonitor (viewport, center);
}

bool
SKIF_ImGui_ImplWin32_WantUpdateMonitors (void)
{
  return ImGui_ImplWin32_GetBackendData()->WantUpdateMonitors;
}

//---------------------------------------------------------------------------------------------------------
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
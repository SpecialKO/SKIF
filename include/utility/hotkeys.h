#pragma once

// Every keyboard hotkey of SKIF, and which window(s) it belongs to
//
//   Scopes:
//     App    - works from any SKIF window (main window, Local Manager, INI editors, Properties, ...), also while a SKIF
//                popup has the focus, unless the focused window handles the same hotkey itself with ImGui::Shortcut ( )
//                (e.g. Ctrl+Q in an INI editor or Properties). SKIF.cpp: ImGui::Shortcut ( ) with ImGuiInputFlags_RouteGlobal
//     Main   - only while SKIF's main window (or a child window of it) has the focus; not while a SKIF popup (e.g. Add Game)
//                or a menu has it. SKIF.cpp: ImGui::Shortcut ( ) called from the main window (focused route).
//                A few are still checked against the OS focus instead, see SKIF_Hotkey_MainWindowFocused
//     Window - only while the window that defines it has the keyboard focus
//     System - registered with Windows (RegisterHotKey), so they work even while SKIF is not focused
//   No hotkey works while a keybinding is being recorded in Settings (g_activeKeybindPopup).
//
//   A window's own ImGui::Shortcut ( ) of the same chord as an App hotkey takes priority over it while that window has
//     the focus; a polled one (IsKeyPressed / IsKeyChordPressed) does not, so such overlaps must use Shortcut ( ).
//   Not every hotkey is checked through these constants yet; where it is not, the comment names where it is handled.
//   When adding a hotkey, add it here as well, and check that it does not clash with one of the same scope.

#include <utility/skif_imgui.h>

// App
constexpr ImGuiKeyChord SKIF_Hotkey_Quit              = ImGuiMod_Ctrl | ImGuiKey_Q;                   // SKIF.cpp: close the app (or minimize to the tray); the INI editor and
                                                                                                       //   Properties handle it themselves, to ask about unsaved changes first
constexpr ImGuiKeyChord SKIF_Hotkey_ToggleDPIScaling  = ImGuiKey_F6;                                  // SKIF.cpp: Appearance: toggle DPI scaling
constexpr ImGuiKeyChord SKIF_Hotkey_CycleTheme        = ImGuiKey_F7;                                  // SKIF.cpp: Appearance: cycle between color themes
constexpr ImGuiKeyChord SKIF_Hotkey_ToggleBorders     = ImGuiKey_F8;                                  // SKIF.cpp: Appearance: toggle UI borders
constexpr ImGuiKeyChord SKIF_Hotkey_CycleColorDepth   = ImGuiKey_F9;                                  // SKIF.cpp: Appearance: cycle between color depths
constexpr ImGuiKeyChord SKIF_Hotkey_OpenIniEditor     = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N;  // SKIF.cpp: open a new INI editor
constexpr ImGuiKeyChord SKIF_Hotkey_OpenLocalManager  = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_M;  // SKIF.cpp: open the Local Manager
constexpr ImGuiKeyChord SKIF_Hotkey_ToggleDebug       = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D;  // SKIF.cpp: Dear ImGui debug log + demo windows

// Main
constexpr ImGuiKeyChord SKIF_Hotkey_CloseMain         = ImGuiMod_Ctrl | ImGuiKey_W;                   // SKIF.cpp: same as Quit, from the main window only
constexpr ImGuiKeyChord SKIF_Hotkey_Refresh           = ImGuiMod_Ctrl | ImGuiKey_R;                   // SKIF.cpp: Library/Hardware/Settings: refresh data
constexpr ImGuiKeyChord SKIF_Hotkey_RefreshAlt        = ImGuiKey_F5;                                  // SKIF.cpp: same as Refresh
constexpr ImGuiKeyChord SKIF_Hotkey_ToggleMiniMode    = ImGuiMod_Ctrl | ImGuiKey_T;                   // SKIF.cpp: toggle mini mode (Library/Service)
constexpr ImGuiKeyChord SKIF_Hotkey_ToggleMiniModeAlt = ImGuiKey_F11;                                 // SKIF.cpp: same as ToggleMiniMode
constexpr ImGuiKeyChord SKIF_Hotkey_AddGame           = ImGuiMod_Ctrl | ImGuiKey_A;                   // SKIF.cpp: Library: add game (not while a text field uses Ctrl+A)
constexpr ImGuiKeyChord SKIF_Hotkey_TabLibrary        = ImGuiMod_Ctrl | ImGuiKey_1;                   // SKIF.cpp: switch to the Library tab
constexpr ImGuiKeyChord SKIF_Hotkey_TabMonitor        = ImGuiMod_Ctrl | ImGuiKey_2;                   // SKIF.cpp: switch to the Monitor tab
constexpr ImGuiKeyChord SKIF_Hotkey_TabHardware       = ImGuiMod_Ctrl | ImGuiKey_3;                   // SKIF.cpp: switch to the Hardware tab
constexpr ImGuiKeyChord SKIF_Hotkey_TabSettings       = ImGuiMod_Ctrl | ImGuiKey_4;                   // SKIF.cpp: switch to the Settings tab
constexpr ImGuiKeyChord SKIF_Hotkey_TabAbout          = ImGuiMod_Ctrl | ImGuiKey_5;                   // SKIF.cpp: switch to the About tab
constexpr ImGuiKeyChord SKIF_Hotkey_ClosePopup        = ImGuiKey_Escape;                              // SKIF.cpp: close the Add Game / Remove Game / category popups
                                                                                                       //   (OS focus check, as it has to work while those popups have the focus)
constexpr ImGuiKeyChord SKIF_Hotkey_SettingsSave      = ImGuiMod_Ctrl | ImGuiKey_S;                   // settings.cpp: save the whitelist / blacklist
//   Also Main: typing letters/digits selects the first matching game in the Library (library.cpp: SearchAppsList),
//              and gamepad L1/R1 switches between the tabs (SKIF.cpp)

// Window: shared by several windows
constexpr ImGuiKeyChord SKIF_Hotkey_CloseWindow       = ImGuiMod_Ctrl | ImGuiKey_W;                   // INI editor, Local Manager, Properties, changelog, updater: close the window
                                                                                                       //   (exact chord; also while typing; unsaved changes are asked about first)
constexpr ImGuiKeyChord SKIF_Hotkey_Filter            = ImGuiMod_Ctrl | ImGuiKey_F;                   // Library, INI editor: select the filter field
constexpr ImGuiKeyChord SKIF_Hotkey_FilterAlt         = ImGuiKey_F3;                                  // Library, INI editor: same as Filter
//   Escape also closes these windows: the INI editor (unless its filter field is active), the Local Manager, Properties,
//     and the changelog and updater windows (updater.cpp; unless a popup is open or a field is active)

// Window: INI editor (ini_editor.cpp)
constexpr ImGuiKeyChord SKIF_Hotkey_IniNew            = ImGuiMod_Ctrl | ImGuiKey_N;                   // new file
constexpr ImGuiKeyChord SKIF_Hotkey_IniNewWindow      = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N;  // new window (same chord as the App hotkey OpenIniEditor)
constexpr ImGuiKeyChord SKIF_Hotkey_IniOpen           = ImGuiMod_Ctrl | ImGuiKey_O;                   // open file
constexpr ImGuiKeyChord SKIF_Hotkey_IniSave           = ImGuiMod_Ctrl | ImGuiKey_S;                   // save
constexpr ImGuiKeyChord SKIF_Hotkey_IniSaveAs         = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S;  // save as
//   Ctrl+Q is handled by the INI editor itself, so unsaved changes are prompted for before the app closes (as is done in Properties)

// Window: Local Manager (local.cpp)
constexpr ImGuiKeyChord SKIF_Hotkey_LocalSelectAll    = ImGuiMod_Ctrl | ImGuiKey_A;                   // select outdated, then all rows
constexpr ImGuiKeyChord SKIF_Hotkey_LocalNew          = ImGuiMod_Ctrl | ImGuiKey_N;                   // install Special K next to a new executable

// System (user-configurable in Settings, see SKIF_Util_RegisterHotKeyHDRToggle / SKIF_Util_RegisterHotKeySVCTemp)
//   Toggle HDR display (default Win+Ctrl+Shift+H) and Start the service (default Win+Shift+Insert); IDs: SKIF_HotKey_HDR / SKIF_HotKey_SVC in SKIF.h

// Whether SKIF's main window has the keyboard focus (as opposed to the Local Manager, an INI editor, Properties, ...)
//   Based on the OS focus; the same check the gamepad tab switching (L1/R1) uses. Thread-safe.
inline bool
SKIF_Hotkey_MainWindowFocused (void)
{
  return (SKIF_ImGui_hWnd != NULL && SKIF_ImGui_GetFocusedWindow ( ) == SKIF_ImGui_hWnd);
}

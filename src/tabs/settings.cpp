
#include <utility/skif_imgui.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <filesystem>
#include <functional>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/hotkeys.h>
#include <utility/injection.h>
#include <utility/updater.h>
#include <utility/gamepad.h>
#include <utility/ini_reader.h>
#include <utility/artwork.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

bool saveIniOSD = false;


struct kb_kv_s
{
  SK_KeybindMultiState*                           _key;
  SKIF_RegistrySettings::KeyValue <std::wstring>* _reg;
  std::function <void (SK_KeybindMultiState*)>    _callback;
  std::function <bool (void)>                     _show;
};

// The keybindings of the Settings tab (SKIF's own global hotkeys)
static std::vector <kb_kv_s>&
SKIF_UI_Settings_GetKeybinds (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // The names of the keybindings in registry.h (where they are also IDs, so they are translated where they are shown)
  [[maybe_unused]] static constexpr const char* keybindNames [] = { N_("Toggle Display HDR"), N_("Start Service") };

  static std::vector <kb_kv_s>
    keybinds = {
    { &_registry.kbToggleHDRDisplay, &_registry.regKVHotkeyToggleHDRDisplay, { [](SK_KeybindMultiState* ptr) { SKIF_Util_RegisterHotKeyHDRToggle (ptr->getKeybind()); } }, { []() { return (SKIF_Util_IsWindows10v1709OrGreater ( ) && SKIF_Util_IsHDRSupported (NULL)); } } },
    { &_registry.kbStartService,     &_registry.regKVHotkeyStartService,     { [](SK_KeybindMultiState* ptr) { SKIF_Util_RegisterHotKeySVCTemp   (ptr->getKeybind()); } }, { []() { return true;                                                                      } } }
  };

  return keybinds;
}

// Cancels the assignment of a keybinding whose dialog is no longer shown (the tab, the window mode or the shown keybindings
//   changed while it was open; e.g. L1/R1 or a launch through SKIF), so its hotkey is not left unregistered until the
//     user returns to the Settings tab. Called once per frame, after all UI.
void
SKIF_UI_Settings_CancelHiddenKeybindDialogs (void)
{
  for (auto& keybind : SKIF_UI_Settings_GetKeybinds ( ))
  {
    // Registers the hotkey again (the saved keybinding is unchanged, so there is nothing to write to the registry)
    if (SK_ImGui_KeybindCancelIfHidden (keybind._key))
      keybind._callback (keybind._key);
  }
}

void
SKIF_UI_Tab_DrawSettings (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static std::vector <kb_kv_s>&
    keybinds = SKIF_UI_Settings_GetKeybinds ( );

  static const std::wstring valvePlugPath = SK_FormatStringW (LR"(%ws\XInput1_4.dll)", _path_cache.steam_install);
  static       bool         valvePlug     = false;
  static       FILETIME     valvePlugLastWriteTime = { }; // Last-write time and size of valvePlugPath as of the last check
  static       uint64_t     valvePlugFileSize      = 0;

  float columnWidth = 0.5f * ImGui::GetContentRegionAvail().x; // Needs to be before the SKIF_ImGui_Columns() call
  bool enableColums = (ImGui::GetContentRegionAvail().x / SKIF_ImGui_GlobalDPIScale >= 750.f);

  // Refresh things when visiting from another tab or when forced
  if (SKIF_Tab_Selected != UITab_Settings                         ||
      RefreshSettingsTab                                          )
  {
    SKIF_Util_IsHDRActive (NULL);
    SKIF_IniReader_ReadOSDIni ( );
    RefreshSettingsTab  = false;

    // Only read the version info of the DLL again if it has changed (e.g. through a Steam update) since the last check,
    //   as a metadata query is cheap while reading the file itself can trigger an antivirus scan
    WIN32_FILE_ATTRIBUTE_DATA valvePlugAttributes = { };

    if (GetFileAttributesExW (valvePlugPath.c_str(), GetFileExInfoStandard, &valvePlugAttributes))
    {
      const uint64_t fileSize = (static_cast<uint64_t> (valvePlugAttributes.nFileSizeHigh) << 32) | valvePlugAttributes.nFileSizeLow;

      if (CompareFileTime (&valvePlugAttributes.ftLastWriteTime, &valvePlugLastWriteTime) != 0 || fileSize != valvePlugFileSize)
      {
        valvePlugLastWriteTime = valvePlugAttributes.ftLastWriteTime;
        valvePlugFileSize      = fileSize;
        valvePlug              = (SKIF_Util_GetProductName (valvePlugPath.c_str()).find(L"Valve Plug") != std::wstring::npos);
      }
    }

    else // The DLL does not exist (anymore)
    {
      valvePlugLastWriteTime = { };
      valvePlugFileSize      = 0;
      valvePlug              = false;
    }

    if (valvePlug && _registry.regKVValvePlug.hasData())
      _registry.iValvePlug = _registry.regKVValvePlug.getData();
  }

  SKIF_Tab_Selected = UITab_Settings;
  if (SKIF_Tab_ChangeTo == UITab_Settings)
      SKIF_Tab_ChangeTo  = UITab_None;

#pragma region Section: Top / General
  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  SKIF_ImGui_Spacing      ( );

  if (enableColums)
  {
    SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_GENERAL", true);
    ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
  }
  else {
    // This is needed to reproduce the same padding on the left side as when using columns
    ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
    ImGui::BeginGroup    ( );
  }

  if ( SKIF_ImGui_Checkbox ( L_("Low bandwidth mode"),                          &_registry.bLowBandwidthMode ) )
    _registry.regKVLowBandwidthMode.putData (                            _registry.bLowBandwidthMode );

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (
    _("For new games/covers, low resolution images will be preferred over high-resolution ones.\n"
    "This only affects new downloads of covers. It does not affect already downloaded covers.\n"
    "This will also disable automatic downloads of new updates to Special K."));

  if ( SKIF_ImGui_Checkbox ( L_("Minimize when launching a game"),            &_registry.bMinimizeOnGameLaunch ) )
    _registry.regKVMinimizeOnGameLaunch.putData (                      _registry.bMinimizeOnGameLaunch );

  if ( SKIF_ImGui_Checkbox ( L_("Restore after closing a game"),              &_registry.bRestoreOnGameExit ) )
    _registry.regKVRestoreOnGameExit.putData    (                      _registry.bRestoreOnGameExit );

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (_("Requires Special K injected to work as intended."));

  if (_registry.bAllowMultipleInstances)
  {
    ImGui::BeginGroup   ( );
    SKIF_ImGui_PushDisableState ( );
  }

  if ( SKIF_ImGui_Checkbox ( L_("Close to the notification area"), &_registry.bCloseToTray ) )
    _registry.regKVCloseToTray.putData (                    _registry.bCloseToTray );

  if (_registry.bAllowMultipleInstances)
  {
    SKIF_ImGui_PopDisableState  ( );
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip (_("Requires 'Allow multiple instances of this app' to be disabled."));
  }

  _inject._StartAtLogonCtrl ( );


  if ( SKIF_ImGui_Checkbox ( L_("Controller support"), &_registry.bControllers ) )
  {
    _registry.regKVControllers.putData (        _registry.bControllers ? 1 : 0);

    // Ensure the gamepad input thread knows what state we are actually in
    static SKIF_GamePadInputHelper& _gamepad =
           SKIF_GamePadInputHelper::GetInstance ( );

    if (_registry.bControllers)
    {
      _gamepad.InvalidateGamePads ( );
      _gamepad.WakeThread  ( );
    } else
      _gamepad.SleepThread ( );
  }

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  // TRANSLATORS: {0} and {1} are the Xbox and PlayStation logos
  SKIF_ImGui_SetHoverTip (F_("Allows the UI to be controlled using {0} or {1} controllers, and adds special features while SKIF is running.", ICON_FA_XBOX, ICON_FA_PLAYSTATION));

  ImGui::SameLine        ( );

  if (! _registry.bControllers)
    SKIF_ImGui_PushDisableState ();

  // At least as wide as before, wider if the translation needs it
  const char* labelGamepadConfig = LI_(ICON_FA_GAMEPAD, "Config###SKInput_GamepadCfgButton");
  if ( ImGui::ButtonEx ( labelGamepadConfig, ImVec2 ( std::max (115 * SKIF_ImGui_GlobalDPIScale, ImGui::CalcTextSize (labelGamepadConfig, NULL, true).x + 2 * ImGui::GetStyle ( ).FramePadding.x),
                                                               25 * SKIF_ImGui_GlobalDPIScale ) ) )
    ImGui::OpenPopup ("Special K Input Config###SKInput_GamepadCfg");

  // The popup opens next to the button: to its right, or to its left if it does not fit there, moved up if needed to
  //   stay within the window. (Without a position, ImGui places a popup at the mouse cursor, or at the keyboard/gamepad
  //     navigation position, as of when it was opened, which could put it far from the button.)
  const ImRect  gamepadCfgButton (ImGui::GetItemRectMin ( ), ImGui::GetItemRectMax ( ));

  if (_registry.bControllers && ImGui::IsPopupOpen ("Special K Input Config###SKInput_GamepadCfg"))
  {
    // The size the popup will have this frame: it sizes itself to its content, as measured in the previous frame. (Not
    //   the size it had in the previous frame, which on its first, hidden frame is not its real size yet, and would have it
    //     placed wrong for a frame.) Unknown on the frame it opens, which ImGui keeps hidden.
    ImGuiContext& g                = *ImGui::GetCurrentContext ( );
    const ImGuiID gamepadCfgId     = ImGui::GetID ("Special K Input Config###SKInput_GamepadCfg");
    ImVec2        gamepadCfgSize   = ImVec2 (0.0f, 0.0f);

    for (const ImGuiPopupData& popup : g.OpenPopupStack)
      if (popup.PopupId == gamepadCfgId && popup.Window != nullptr)
        gamepadCfgSize = ImGui::CalcWindowNextAutoFitSize (popup.Window);

    const ImGuiViewport* vp      = ImGui::GetWindowViewport ( );
    const float          spacing = ImGui::GetStyle ( ).ItemSpacing.x;
    const ImVec2         vpMin   = vp->WorkPos;
    const ImVec2         vpMax   = vp->WorkPos + vp->WorkSize;

    ImVec2 pos (gamepadCfgButton.Max.x + spacing, gamepadCfgButton.Min.y);

    if (pos.x + gamepadCfgSize.x > vpMax.x)
      pos.x = gamepadCfgButton.Min.x - spacing - gamepadCfgSize.x;
    if (pos.y + gamepadCfgSize.y > vpMax.y)
      pos.y = vpMax.y - gamepadCfgSize.y;

    pos.x = std::max (pos.x, vpMin.x);
    pos.y = std::max (pos.y, vpMin.y);

    ImGui::SetNextWindowPos (pos, ImGuiCond_Always);

    // The frame it opens on, ImGui keeps the popup hidden while it measures the content, at the width the popup has then.
    //   Without a width it is a 16 px wide window, where the wrapping labels break after every word: the next frame would
    //     show it far too tall (e.g. 417x1434 instead of 403x252), placed and sized wrong for a frame. So it is measured at
    //       the width of the window it opens from, where nothing needs to wrap. Only for that frame: it then fits its width
    //         to the content as usual (the height is never set, so it keeps fitting the content).
    ImGui::SetNextWindowSize (ImVec2 (vpMax.x - vpMin.x, 0.0f), ImGuiCond_Appearing);
  }

  if (_registry.bControllers)
  {
    if (ImGui::BeginPopup ("Special K Input Config###SKInput_GamepadCfg",
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::SeparatorText (_("General Controller Settings"));
      ImGui::TreePush      ("");
      bool bEnableScreenSaverChord = _registry.skinput.dwScreenSaverChordBehavior != 0 &&
                                     _registry.skinput.dwScreenSaverChord         != 0;
      // TRANSLATORS: {0} are the Xbox and PlayStation logos; the label is followed by "+ <button>", the button to press with them
      if (SKIF_ImGui_Checkbox  ((F_("Activate Screen Saver Using {0}", "  " ICON_FA_XBOX "/" ICON_FA_PLAYSTATION) + "  + ###ScreenSaverChord").c_str ( ), &bEnableScreenSaverChord))
      {
        if (bEnableScreenSaverChord)
        {
          //if (_registry.skinput.dwScreenSaverChordBehavior == 0)
          //    _registry.skinput.dwScreenSaverChordBehavior = 1;

          if (_registry.skinput.dwScreenSaverChord == 0) {
              _registry.skinput.dwScreenSaverChord = XINPUT_GAMEPAD_A;
              _registry.regKVControllerScreenSaverChord.putData (_registry.skinput.dwScreenSaverChord);
          }
        }

        else
        {
          _registry.skinput.dwScreenSaverChord = 0;
          _registry.regKVControllerScreenSaverChord.putData (_registry.skinput.dwScreenSaverChord);
          //_registry.skinput.dwScreenSaverChordBehavior = 0;
        }
      }

      if (bEnableScreenSaverChord)
      {
        // TRANSLATORS: {0} is the logo of one or more controllers (Xbox, PlayStation)
        SKIF_ImGui_SetHoverTip (F_("The controller binding is configurable by clicking the text after {0}", ICON_FA_XBOX " / " ICON_FA_PLAYSTATION));

        ImGui::SameLine ();

        static bool         selected = false;
        static DWORD dwButtonPressed = 0;

        if (! selected)
        {
          dwButtonPressed = 0;
        }

        ImGui::PushID ("ScreenSaverBinding");
        switch (_registry.skinput.dwScreenSaverChord)
        {
          case XINPUT_GAMEPAD_LEFT_SHOULDER:  selected = ImGui::Selectable ("LB / L1",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_TRIGGER:   selected = ImGui::Selectable ("LT / L2",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_THUMB:     selected = ImGui::Selectable ("LS / L3",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_SHOULDER: selected = ImGui::Selectable ("RB / R1",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_TRIGGER:  selected = ImGui::Selectable ("RT / R2",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_THUMB:    selected = ImGui::Selectable ("RS / R3",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case 0x1:                           selected = ImGui::Selectable (CL_("gamepad button", "A / Cross"),     true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case 0x3/*
               XINPUT_GAMEPAD_DPAD_UP*/:      selected = ImGui::Selectable (CL_("gamepad button", "Up"),            true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_DOWN:      selected = ImGui::Selectable (CL_("gamepad button", "Down"),          true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_LEFT:      selected = ImGui::Selectable (CL_("gamepad button", "Left"),          true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_RIGHT:     selected = ImGui::Selectable (CL_("gamepad button", "Right"),         true, ImGuiSelectableFlags_NoAutoClosePopups); break;

          case XINPUT_GAMEPAD_START:          selected = ImGui::Selectable (CL_("gamepad button", "Start"),         true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_BACK:           selected = ImGui::Selectable (CL_("gamepad button", "Back / Select"), true, ImGuiSelectableFlags_NoAutoClosePopups); break;

          case XINPUT_GAMEPAD_Y:              selected = ImGui::Selectable (CL_("gamepad button", "Y / Triangle"),  true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_A:              selected = ImGui::Selectable (CL_("gamepad button", "A / Cross"),     true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_X:              selected = ImGui::Selectable (CL_("gamepad button", "X / Square"),    true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_B:              selected = ImGui::Selectable (CL_("gamepad button", "B / Circle"),    true, ImGuiSelectableFlags_NoAutoClosePopups); break;
        }
        ImGui::PopID ();

        static DWORD dwPopupOpenedTime = 0;
               DWORD dwTimeNow         = SKIF_Util_timeGetTime ();

        static bool open = false;
        if (selected)
        {
          dwPopupOpenedTime = dwTimeNow;
          ImGui::OpenPopup ("ChordBinding_ScreenSaver");
          open = true;
        }

        if (ImGui::BeginPopupModal ("ChordBinding_ScreenSaver", &open, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
        {
          selected = false;

          // Avoid immediately closing the popup again before it has a chance to process the button press.
          //   Only while the main window has the focus: gamepad input also reaches SKIF's other windows
          if (dwTimeNow != dwPopupOpenedTime && SKIF_Hotkey_MainWindowFocused ( ))
          {
                 if (ImGui::IsKeyPressed (ImGuiKey_GamepadL1))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_SHOULDER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL2))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_TRIGGER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL3))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_THUMB;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR1))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_SHOULDER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR2))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_TRIGGER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR3))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_THUMB;

            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadUp))     dwButtonPressed = XINPUT_GAMEPAD_DPAD_UP;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadDown))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_DOWN;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadLeft))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_LEFT;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadRight))  dwButtonPressed = XINPUT_GAMEPAD_DPAD_RIGHT;

            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadStart))      dwButtonPressed = XINPUT_GAMEPAD_START;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadBack))       dwButtonPressed = XINPUT_GAMEPAD_BACK;

            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceUp))     dwButtonPressed = XINPUT_GAMEPAD_Y;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceDown))   dwButtonPressed = XINPUT_GAMEPAD_A;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceLeft))   dwButtonPressed = XINPUT_GAMEPAD_X;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceRight))  dwButtonPressed = XINPUT_GAMEPAD_B;

            switch (dwButtonPressed)
            {
              case XINPUT_GAMEPAD_LEFT_SHOULDER:  _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_LEFT_TRIGGER:   _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_LEFT_THUMB:     _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_RIGHT_SHOULDER: _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_RIGHT_TRIGGER:  _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_RIGHT_THUMB:    _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_DPAD_UP:        _registry.skinput.dwScreenSaverChord = 0x3;             break;
              case XINPUT_GAMEPAD_DPAD_DOWN:      _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_DPAD_LEFT:      _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_DPAD_RIGHT:     _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;

              case XINPUT_GAMEPAD_START:          _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_BACK:           _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;

              case XINPUT_GAMEPAD_Y:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_A:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_X:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_B:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
              default:
                selected = true;
                break;
            }

            if (! selected)
            {
              _registry.regKVControllerScreenSaverChord.putData (_registry.skinput.dwScreenSaverChord);

              ImGui::CloseCurrentPopup ();
              open = false;
            }
          }

          ImGui::EndPopup ();
        }
      }

      bool                                                            bGamepadsDeactivateScreenSaver =
              _registry.skinput.dwGamepadsDeactivateScreenSaver != 0;
      if (SKIF_ImGui_Checkbox (L_("Gamepad Input Deactivates Screen Saver"), &bGamepadsDeactivateScreenSaver))
      {
        _registry.regKVControllerGamepadsDeactivateScreenSaver.putData (
          _registry.skinput.dwGamepadsDeactivateScreenSaver =
                             bGamepadsDeactivateScreenSaver ? 1 : 0
        );
      }

      if (! bGamepadsDeactivateScreenSaver)
        // TRANSLATORS: {0} is the Xbox logo (the Xbox button)
        ImGui::BulletText  ("%s", F_("Pressing {0} on Xbox controllers always deactivates.", ICON_FA_XBOX).c_str ( ));

      ImGui::TreePop       (  );
      ImGui::Spacing       (  );
      ImGui::Spacing       (  );
      ImGui::SeparatorText (_("PlayStation Power Management"));
      ImGui::TreePush      ("");

      bool bEnablePowerOffChord = _registry.skinput.dwPowerOffChordBehavior != 0 &&
                                  _registry.skinput.dwPowerOffChord         != 0;
      // TRANSLATORS: {0} is the PlayStation logo; the label is followed by "+ <button>", the button to press with it
      if (SKIF_ImGui_Checkbox  ((F_("Power Off Controllers Using {0}", "      " ICON_FA_PLAYSTATION) + "   + ###PowerOffChord").c_str ( ), &bEnablePowerOffChord))
      {
        if (bEnablePowerOffChord)
        {
          //if (_registry.skinput.dwScreenSaverChordBehavior == 0)
          //    _registry.skinput.dwScreenSaverChordBehavior = 1;

          if (_registry.skinput.dwPowerOffChord == 0) {
              _registry.skinput.dwPowerOffChord = XINPUT_GAMEPAD_Y;
              _registry.regKVControllerPowerOffChord.putData (_registry.skinput.dwPowerOffChord);
          }
        }

        else
        {
          _registry.skinput.dwPowerOffChord = 0;
          _registry.regKVControllerPowerOffChord.putData (_registry.skinput.dwPowerOffChord);
          //_registry.skinput.dwScreenSaverChordBehavior = 0;
        }
      }

      if (bEnablePowerOffChord)
      {
        // TRANSLATORS: {0} is the logo of one or more controllers (Xbox, PlayStation)
        SKIF_ImGui_SetHoverTip (F_("The controller binding is configurable by clicking the text after {0}", ICON_FA_PLAYSTATION));

        ImGui::SameLine ();

        static bool         selected = false;
        static DWORD dwButtonPressed = 0;

        if (! selected)
        {
          dwButtonPressed = 0;
        }

        ImGui::PushID ("PowerOffBinding");
        switch (_registry.skinput.dwPowerOffChord)
        {
          case XINPUT_GAMEPAD_LEFT_SHOULDER:  selected = ImGui::Selectable ("L1",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_TRIGGER:   selected = ImGui::Selectable ("L2",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_THUMB:     selected = ImGui::Selectable ("L3",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_SHOULDER: selected = ImGui::Selectable ("R1",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_TRIGGER:  selected = ImGui::Selectable ("R2",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_THUMB:    selected = ImGui::Selectable ("R3",       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case 0x1:                           selected = ImGui::Selectable (CL_("gamepad button", "Triangle"), true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case 0x3/*
               XINPUT_GAMEPAD_DPAD_UP*/:      selected = ImGui::Selectable (CL_("gamepad button", "Up"),       true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_DOWN:      selected = ImGui::Selectable (CL_("gamepad button", "Down"),     true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_LEFT:      selected = ImGui::Selectable (CL_("gamepad button", "Left"),     true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_RIGHT:     selected = ImGui::Selectable (CL_("gamepad button", "Right"),    true, ImGuiSelectableFlags_NoAutoClosePopups); break;

          case XINPUT_GAMEPAD_START:          selected = ImGui::Selectable (CL_("gamepad button", "Start"),    true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_BACK:           selected = ImGui::Selectable (CL_("gamepad button", "Select"),   true, ImGuiSelectableFlags_NoAutoClosePopups); break;

          case XINPUT_GAMEPAD_Y:              selected = ImGui::Selectable (CL_("gamepad button", "Triangle"), true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_A:              selected = ImGui::Selectable (CL_("gamepad button", "Cross"),    true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_X:              selected = ImGui::Selectable (CL_("gamepad button", "Square"),   true, ImGuiSelectableFlags_NoAutoClosePopups); break;
          case XINPUT_GAMEPAD_B:              selected = ImGui::Selectable (CL_("gamepad button", "Circle"),   true, ImGuiSelectableFlags_NoAutoClosePopups); break;
        }
        ImGui::PopID ();

        static DWORD dwPopupOpenedTime = 0;
               DWORD dwTimeNow         = SKIF_Util_timeGetTime ();

        static bool open = false;
        if (selected)
        {
          dwPopupOpenedTime = dwTimeNow;
          ImGui::OpenPopup ("ChordBinding_PowerOff");
          open = true;
        }

        if (ImGui::BeginPopupModal ("ChordBinding_PowerOff", &open, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
        {
          selected = false;

          // Avoid immediately closing the popup again before it has a chance to process the button press.
          //   Only while the main window has the focus: gamepad input also reaches SKIF's other windows
          if (dwTimeNow != dwPopupOpenedTime && SKIF_Hotkey_MainWindowFocused ( ))
          {
                 if (ImGui::IsKeyPressed (ImGuiKey_GamepadL1))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_SHOULDER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL2))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_TRIGGER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL3))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_THUMB;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR1))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_SHOULDER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR2))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_TRIGGER;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR3))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_THUMB;

            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadUp))     dwButtonPressed = XINPUT_GAMEPAD_DPAD_UP;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadDown))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_DOWN;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadLeft))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_LEFT;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadRight))  dwButtonPressed = XINPUT_GAMEPAD_DPAD_RIGHT;

            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadStart))      dwButtonPressed = XINPUT_GAMEPAD_START;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadBack))       dwButtonPressed = XINPUT_GAMEPAD_BACK;

            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceUp))     dwButtonPressed = XINPUT_GAMEPAD_Y;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceDown))   dwButtonPressed = XINPUT_GAMEPAD_A;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceLeft))   dwButtonPressed = XINPUT_GAMEPAD_X;
            else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceRight))  dwButtonPressed = XINPUT_GAMEPAD_B;

            switch (dwButtonPressed)
            {
              case XINPUT_GAMEPAD_LEFT_SHOULDER:  _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_LEFT_TRIGGER:   _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_LEFT_THUMB:     _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_RIGHT_SHOULDER: _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_RIGHT_TRIGGER:  _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_RIGHT_THUMB:    _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_DPAD_UP:        _registry.skinput.dwPowerOffChord = 0x3;             break;
              case XINPUT_GAMEPAD_DPAD_DOWN:      _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_DPAD_LEFT:      _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_DPAD_RIGHT:     _registry.skinput.dwPowerOffChord = dwButtonPressed; break;

              case XINPUT_GAMEPAD_START:          _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_BACK:           _registry.skinput.dwPowerOffChord = dwButtonPressed; break;

              case XINPUT_GAMEPAD_Y:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_A:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_X:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              case XINPUT_GAMEPAD_B:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
              default:
                selected = true;
                break;
            }

            if (! selected)
            {
              _registry.regKVControllerPowerOffChord.putData (_registry.skinput.dwPowerOffChord);

              ImGui::CloseCurrentPopup ();
              open = false;
            }
          }

          ImGui::EndPopup ();
        }
      }

      float fIdleMinutes =
        static_cast <float> (_registry.skinput.dwIdleTimeoutInSecs) / 60.0f;

      float fLastIdle = fIdleMinutes;

      const std::string idleFormat = (fIdleMinutes < 0.5f) ? SKIF_L10n_PrintfFormat (_("Never Power Off"), "")
                                                           // TRANSLATORS: {0} is a number of minutes
                                                           : SKIF_L10n_PrintfFormat (_("Power Off After {0} Minutes"), "%.1f");

      if (SKIF_ImGui_SliderFloat (L_("Idle Behavior"), &fIdleMinutes, 0.0f, 30.0f, idleFormat.c_str ( )))
      {
        // Minimum positive step value to allow gamepad to move this slider off of "Never Power Off"
        if (fLastIdle == 0.0f && fIdleMinutes > 0.0f)
            fIdleMinutes = 0.5f;

        _registry.skinput.dwIdleTimeoutInSecs =
          fIdleMinutes < 0.5f ? 0 : static_cast <DWORD> (60.0f * fIdleMinutes);

        _registry.regKVControllerIdlePowerOffTimeOut.putData (_registry.skinput.dwIdleTimeoutInSecs);
      }

      SKIF_ImGui_SetHoverTip (_("This only applies when this app is running and no game is actively using Special K."));

      ImGui::TreePop         (  );

      ImGui::Spacing         (  );
      ImGui::Spacing         (  );
      ImGui::SeparatorText   (_("Third-Party Setup\tXInput Assignment: "));

      auto&                     _gamepad = SKIF_GamePadInputHelper::GetInstance ();
      const auto slots = _gamepad.GetGamePads ( );

      ImGui::SameLine        (  );
      ImGui::BeginGroup      (  );
      for (auto slot : slots){
        ImGui::SameLine      (  );
        if (slot)
          ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), ICON_FA_CHECK);
        else
          ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow ), ICON_FA_XMARK); }
      ImGui::EndGroup        (  );

      ImGui::TreePush        ("");
      ImGui::BeginGroup      (  );
      if (valvePlug)
      {
        static bool valvePlugState = (bool)_registry.iValvePlug;
        extern HANDLE SteamProcessHandle;

        bool disable = (SteamProcessHandle != NULL);

        if (disable)
          SKIF_ImGui_PushDisableState ( );

        // TRANSLATORS: {0} is the Steam logo (as in "Steam Input")
        if ( SKIF_ImGui_Checkbox ( (F_("Disable {0} Input (will restart Steam)", ICON_FA_STEAM) + "###ValvePlug").c_str ( ), &valvePlugState) )
        {
          _registry.regKVValvePlug.putData (              static_cast<int> (valvePlugState));

          PROCESSENTRY32W pe32 = SKIF_Util_FindProcessByName (L"steam.exe");
          // Exits the Steam client if it is running
          if (pe32.th32ProcessID != 0)
          {
            SteamProcessHandle = OpenProcess (SYNCHRONIZE, FALSE, pe32.th32ProcessID);

            if (SteamProcessHandle != NULL)
            {
              // Wait on all tabs as well...
              SKIF_Util_AddWatchHandle (SteamProcessHandle, UITab_ALL);

              // Signal the Steam client to exit
              PLOG_INFO << "Shutting down the Steam client...";
              SKIF_Util_OpenURI (L"steam://exit");
            }
          }
        }

        if (disable)
          SKIF_ImGui_PopDisableState ( );

        if (SteamProcessHandle != NULL && WaitForSingleObject (SteamProcessHandle, 0) == WAIT_TIMEOUT)
        {
          ImGui::SameLine    ( ); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_TRIANGLE_EXCLAMATION);
          ImGui::SameLine    ( ); SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    _("Restarting Steam..."));
        }
      }
      else
      {
        ImGui::BeginGroup    ( );
        ImGui::TextColored   (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
        ImGui::SameLine      ( );
        // TRANSLATORS: {0} is the Steam logo (as in "Steam Input")
        if (ImGui::Button    ((F_("Install {0} Input Kill Switch", ICON_FA_STEAM) + "###InstallValvePlug").c_str ( )))
        {
          SKIF_Util_OpenURI  (L"https://github.com/SpecialKO/ValvePlug/releases");
        }
        ImGui::EndGroup      ( );
        SKIF_ImGui_SetHoverTip
                             (_("Allows Steam Input to be Fully Disabled or Enabled.\n\n"
                                " * Steam Input usually does stuff whether or not it is enabled per-game.\n"
                                " * No registry settings need to be set if you use this config menu."));
      }
      ImGui::EndGroup        ( );
      ImGui::TreePop         ( );
      ImGui::EndPopup        ( );
    }
  }

  else
    SKIF_ImGui_PopDisableState ();


  if (enableColums)
  {
    ImGui::NextColumn    ( );
    ImGui::TreePush        ("RightColumnSectionTop");
  }
  else {
    ImGui::Spacing       ( );
    ImGui::Spacing       ( );
  }

  // New column

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (_("This determines how long the service will remain running when launching a game.\n"
                          "Move the mouse over each option to get more information."));
  ImGui::SameLine        ( );
  SKIF_ImGui_TextColoredWrapped (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      _("Auto-stop behavior when launching a game:"));
  ImGui::TreePush        ("AutoStopBehavior");

  //if (ImGui::RadioButton ("Never",           &_registry.iAutoStopBehavior, 0))
  //  regKVAutoStopBehavior.putData (           _registry.iAutoStopBehavior);
  //
  //ImGui::SameLine        ( );

  if (SKIF_ImGui_RadioButton (L_("Stop on injection"),    &_registry.iAutoStopBehavior, 1))
    _registry.regKVAutoStopBehavior.putData (      _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip (_("The service will be stopped when Special K successfully injects into a game."));

  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Stop on game exit")));
  if (SKIF_ImGui_RadioButton (L_("Stop on game exit"),      &_registry.iAutoStopBehavior, 2))
    _registry.regKVAutoStopBehavior.putData (        _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip (_("The service will be stopped when Special K detects that the game is being closed."));

  ImGui::TreePop         ( );

  ImGui::Spacing         ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (_("This setting has no effect if low bandwidth mode is enabled."));
  ImGui::SameLine        ( );
  SKIF_ImGui_TextColoredWrapped (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      _("Check for updates:"));

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PushDisableState ( );

  ImGui::BeginGroup    ( );

  ImGui::TreePush        ("CheckForUpdates");
  if (SKIF_ImGui_RadioButton (L_("Never"),                 &_registry.iCheckForUpdates, 0))
    _registry.regKVCheckForUpdates.putData (         _registry.iCheckForUpdates);
  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Weekly")));
  if (SKIF_ImGui_RadioButton (L_("Weekly"),                &_registry.iCheckForUpdates, 1))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);
  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("On each launch")));
  if (SKIF_ImGui_RadioButton (L_("On each launch"),        &_registry.iCheckForUpdates, 2))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);

  ImGui::TreePop         ( );

  ImGui::EndGroup      ( );

  static SKIF_Updater& _updater = SKIF_Updater::GetInstance ( );

  bool disableCheckForUpdates = _registry.bLowBandwidthMode;
  bool disableRollbackUpdates = _registry.bLowBandwidthMode;

  if (_updater.IsRunning ( ))
    disableCheckForUpdates = disableRollbackUpdates = true;

  if (! disableCheckForUpdates && _updater.GetChannels ( )->empty( ))
    disableRollbackUpdates = true;

  ImGui::TreePush        ("UpdateChannels");

  ImGui::BeginGroup    ( );

  if (disableRollbackUpdates)
    SKIF_ImGui_PushDisableState ( );

  if (ImGui::BeginCombo ("###SKIF_wzUpdateChannel", _updater.GetChannel( )->second.c_str()))
  {
    for (auto& updateChannel : *_updater.GetChannels ( ))
    {
      bool is_selected = (_updater.GetChannel()->first == updateChannel.first);

      if (ImGui::Selectable (updateChannel.second.c_str(), is_selected) && updateChannel.first != _updater.GetChannel( )->first)
      {
        _updater.SetChannel (&updateChannel); // Update selection
        _updater.SetIgnoredUpdate (L"");      // Clear any ignored updates
        _updater.CheckForUpdates  ( );        // Trigger a new check for updates
      }

      if (is_selected)
        ImGui::SetItemDefaultFocus ( );
    }

    ImGui::EndCombo  ( );
  }

  if (disableRollbackUpdates)
    SKIF_ImGui_PopDisableState  ( );

  ImGui::SameLine        ( );

  if (disableCheckForUpdates)
    SKIF_ImGui_PushDisableState ( );
  else
    ImGui::PushStyleColor       (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success));

  if (ImGui::Button      (ICON_FA_ROTATE))
  {
    _updater.SetIgnoredUpdate (L""); // Clear any ignored updates
    _updater.CheckForUpdates (true); // Trigger a forced check for updates/redownloads of repository.json and patrons.txt
  }

  SKIF_ImGui_SetHoverTip (_("Check for updates"));

  if (disableCheckForUpdates)
    SKIF_ImGui_PopDisableState  ( );
  else
    ImGui::PopStyleColor        ( );

  if (((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older) || _updater.IsRollbackAvailable ( ))
  {
    ImGui::SameLine        ( );

    if (disableRollbackUpdates)
      SKIF_ImGui_PushDisableState ( );
    else
      ImGui::PushStyleColor       (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));

    if (ImGui::Button      (ICON_FA_ROTATE_LEFT))
    {
      extern PopupState UpdatePromptPopup;

      // Ignore the current version
      //_updater.SetIgnoredUpdate (SK_UTF8ToWideChar (_updater.GetResults().version));
      _updater.SetIgnoredUpdate (_inject.SKVer32);

      if ((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older)
        UpdatePromptPopup = PopupState_Open;
      else
        _updater.CheckForUpdates (false, true); // Trigger a rollback
    }

    SKIF_ImGui_SetHoverTip (_("Roll back to the previous version"));

    if (disableRollbackUpdates)
      SKIF_ImGui_PopDisableState  ( );
    else
      ImGui::PopStyleColor        ( );
  }

  ImGui::EndGroup      ( );

  ImGui::TreePop       ( );

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PopDisableState  ( );

  ImGui::Spacing       ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (_("This provides contextual notifications in Windows when the service starts or stops."));
  ImGui::SameLine        ( );
  SKIF_ImGui_TextColoredWrapped (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      _("Show injection service desktop notifications:"));
  ImGui::TreePush        ("Notifications");
  if (SKIF_ImGui_RadioButton (L_("Never"),          &_registry.iNotifications, 0))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Always")));
  if (SKIF_ImGui_RadioButton (L_("Always"),         &_registry.iNotifications, 1))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("When unfocused")));
  if (SKIF_ImGui_RadioButton (L_("When unfocused"), &_registry.iNotifications, 2))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::TreePop         ( );

  if (enableColums)
  {
    ImGui::TreePop       ( );
    ImGui::Columns       (1);
  }
  else
    ImGui::EndGroup      ( );

  ImGui::PopStyleColor();

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Library
  if (ImGui::CollapsingHeader (L_("Library###SKIF_SettingsHeader-1")))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_LIBRARY", true);
      ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    if ( SKIF_ImGui_Checkbox ( L_("Ignore articles when sorting"),                &_registry.bLibraryIgnoreArticles) )
    {
      _registry.regKVLibraryIgnoreArticles.putData (                       _registry.bLibraryIgnoreArticles);
      RepopulateGames = true;
    }

    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Ignore articles like 'THE', 'A', and 'AN' when sorting games."));

    if ( SKIF_ImGui_Checkbox ( L_("Remember the last selected game"),           &_registry.bRememberLastSelected ) )
      _registry.regKVRememberLastSelected.putData (                      _registry.bRememberLastSelected );

    if ( SKIF_ImGui_Checkbox ( L_("Remember category collapsible state"),       &_registry.bRememberCategoryState) )
      _registry.regKVRememberCategoryState.putData (                     _registry.bRememberCategoryState);

    ImGui::Spacing         ( );

    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Prefer covers from PCGamingWiki for these platforms:"));

    ImGui::TreePush      ("PCGWCovers");

    static SKIF_LibraryArtwork& _art = SKIF_LibraryArtwork::Get ( );

    if (ImGui::CheckboxFlags  ("GOG",         &_registry.iPCGWCovers, StoreFlag_GOG))
    {
      _registry.regKVPCGWCovers.putData       (_registry.iPCGWCovers);
      _art.RefreshShownCover ( );
      _art.ResetCoverAlpha   ( );
    }

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::CheckboxFlags  ("Steam",       &_registry.iPCGWCovers, StoreFlag_Steam))
    {
      _registry.regKVPCGWCovers.putData       (_registry.iPCGWCovers);
      _art.RefreshShownCover ( );
      _art.ResetCoverAlpha   ( );
    }

    ImGui::TreePop          ( );

    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Prefer covers from IGDB for these platforms:"));

    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Games are looked up on IGDB by their store ID where possible, otherwise by their exact name.\n"
                            "A cover from PCGamingWiki takes precedence over one from IGDB, as those are typically of\n"
                            "a higher resolution. EA, itch.io, Ubisoft and custom games always use IGDB covers, as those\n"
                            "platforms have no suitable covers of their own."));

    ImGui::TreePush      ("IGDBCovers");

    auto _IGDBCheckbox = [&](const char* label, int flag)
    {
      if (ImGui::CheckboxFlags (label,        &_registry.iIGDBCovers, flag))
      {
        _registry.regKVIGDBCovers.putData     (_registry.iIGDBCovers);
        _art.RefreshShownCover ( );
        _art.ResetCoverAlpha   ( );
      }
    };

    _IGDBCheckbox ("Epic",  StoreFlag_Epic);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    _IGDBCheckbox ("GOG",   StoreFlag_GOG);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    _IGDBCheckbox ("Steam", StoreFlag_Steam);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    _IGDBCheckbox ("Xbox",  StoreFlag_Xbox);

    ImGui::TreePop          ( );

    if (enableColums)
    {
      ImGui::NextColumn    ( );
      ImGui::TreePush         ("RightColumnSectionLibrary");
    }
    else {
      ImGui::Spacing       ( );
      ImGui::Spacing       ( );
    }

    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Include games from these platforms:"));
    ImGui::TreePush      ("Libraries");

    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox          ("EA",     &_registry.bLibraryEA))
    {
      _registry.regKVLibraryEA.putData      (_registry.bLibraryEA);
      RepopulateGames = true;
    }

    if (SKIF_ImGui_Checkbox        ("Epic",     &_registry.bLibraryEpic))
    {
      _registry.regKVLibraryEpic.putData    (_registry.bLibraryEpic);
      RepopulateGames = true;
    }

    if (SKIF_ImGui_Checkbox         ("GOG",     &_registry.bLibraryGOG))
    {
      _registry.regKVLibraryGOG.putData     (_registry.bLibraryGOG);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox         ("itch.io", &_registry.bLibraryitch))
    {
      _registry.regKVLibraryitch.putData    (_registry.bLibraryitch);
      RepopulateGames = true;
    }

    if (SKIF_ImGui_Checkbox         ("Steam",   &_registry.bLibrarySteam))
    {
      _registry.regKVLibrarySteam.putData   (_registry.bLibrarySteam);
      RepopulateGames = true;
    }

    if (SKIF_ImGui_Checkbox         ("Ubisoft", &_registry.bLibraryUbisoft))
    {
      _registry.regKVLibraryUbisoft.putData (_registry.bLibraryUbisoft);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox        ("Xbox",     &_registry.bLibraryXbox))
    {
      _registry.regKVLibraryXbox.putData    (_registry.bLibraryXbox);
      RepopulateGames = true;
    }

    if (SKIF_ImGui_Checkbox        (L_("Custom"),   &_registry.bLibraryCustom))
    {
      _registry.regKVLibraryCustom.putData  (_registry.bLibraryCustom);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::TreePop          ( );

    ImGui::Spacing         ( );

    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
    SKIF_ImGui_SetHoverTip (_("Warning: This skips the regular platform launch process for the game,\n"
                            "including steps like the cloud saves synchronization that usually occurs."));
    ImGui::SameLine    ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Prefer Instant Play for these platforms:"));

    ImGui::TreePush      ("InstantPlay");

    // Three platforms per row: each group is a column of two (EA, GOG, itch.io / Steam, Ubisoft, Xbox)
    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox       ("EA",          &_registry.bInstantPlayEA))
      _registry.regKVInstantPlayEA.putData    (_registry.bInstantPlayEA);

    if (SKIF_ImGui_Checkbox       ("Steam",       &_registry.bInstantPlaySteam))
      _registry.regKVInstantPlaySteam.putData (_registry.bInstantPlaySteam);

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox       ("GOG",         &_registry.bInstantPlayGOG))
      _registry.regKVInstantPlayGOG.putData   (_registry.bInstantPlayGOG);

    if (SKIF_ImGui_Checkbox       ("Ubisoft",     &_registry.bInstantPlayUbisoft))
      _registry.regKVInstantPlayUbisoft.putData (_registry.bInstantPlayUbisoft);

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox       ("itch.io",     &_registry.bInstantPlayitch))
      _registry.regKVInstantPlayitch.putData  (_registry.bInstantPlayitch);

    if (SKIF_ImGui_Checkbox       ("Xbox",        &_registry.bInstantPlayXbox))
      _registry.regKVInstantPlayXbox.putData  (_registry.bInstantPlayXbox);

    ImGui::EndGroup ( );

    ImGui::TreePop          ( );

    // Column end
    if (enableColums)
    {
      ImGui::TreePop          ( );
      ImGui::Columns          (1);
    }
    else
      ImGui::EndGroup      ( );

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();



#pragma endregion


#pragma region Section: Appearances
  if (ImGui::CollapsingHeader (L_("Appearance###SKIF_SettingsHeader-2")))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    extern bool RecreateSwapChains;
    extern bool RecreateWin32Windows;

    //ImGui::Spacing    ( );

    SKIF_ImGui_Spacing      ( );

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_APPEARANCES", true);
      ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    // Language of the UI (applied at the beginning of the next frame)
    {
      static SKIF_L10n& _l10n = SKIF_L10n::GetInstance ( );

      // The pseudo-locale (long, accented text for finding untranslated text and clipped layouts) is listed in developer mode
      const bool showPseudo = _registry.bDeveloperMode || _l10n.GetPreferred ( ) == SKIF_L10n::TagPseudo;

      // The list does not change while SKIF runs (and the native names come from Windows), so it is only built once,
      //   along with the language that following Windows results in
      static std::vector <SKIF_L10n::Language> languages;
      static bool                              languagesPseudo = false;
      static std::string                       windowsName;
      if (languages.empty ( ) || languagesPseudo != showPseudo)
      {
        languages       = _l10n.GetLanguages (showPseudo);
        languagesPseudo = showPseudo;

        const std::string windowsTag = _l10n.ResolveLanguage ("");
        for (auto& lang : languages)
          if (lang.tag == windowsTag)
            windowsName = lang.name;
      }

      auto _DisplayName = [](const SKIF_L10n::Language& lang) -> std::string
      {
        return (lang.completion < 100) ? (lang.name + " (" + std::to_string (lang.completion) + "%)")
                                       :  lang.name;
      };

      const std::string preferred = _l10n.GetPreferred ( );
      const std::string active    = _l10n.GetLanguage  ( );

      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip (_("Translations are contributed by the community. Text that has not been translated yet is shown in English."));
      ImGui::SameLine        ( );
      ImGui::TextColored     (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), "%s",
          _("Language:")
      );
      ImGui::TreePush        ("Language");

      // TRANSLATORS: {0} is the name of the language that Windows uses (or English, if there is no translation for it)
      const std::string previewFollowWindows = F_("Same as Windows ({0})", windowsName);
      std::string       preview              = previewFollowWindows;
      for (auto& lang : languages)
        if (! preferred.empty ( ) && lang.tag == active)
          preview = _DisplayName (lang);

      if (ImGui::BeginCombo ("###SKIF_LanguageCombo", preview.c_str ( )))
      {
        std::string selected  = preferred;
        bool        hasChange = false;

        if (ImGui::Selectable ((previewFollowWindows + "###SKIF_Language-").c_str ( ), preferred.empty ( )))
        {
          selected  = "";
          hasChange = true;
        }
        if (preferred.empty ( ))
          ImGui::SetItemDefaultFocus ( );

        for (auto& lang : languages)
        {
          bool is_selected = (! preferred.empty ( ) && lang.tag == active);

          // Language names are shown in their own language, so they are not translated
          if (ImGui::Selectable ((_DisplayName (lang) + "###SKIF_Language-" + lang.tag).c_str ( ), is_selected))
          {
            selected  = lang.tag;
            hasChange = true;
          }
          if (is_selected)
            ImGui::SetItemDefaultFocus ( );
        }

        ImGui::EndCombo  ( );

        if (hasChange && selected != preferred)
        {
          _registry.wsLanguage = SK_UTF8ToWideChar (selected);

          if (selected.empty ( ))
            _registry.regKVLanguage.deleteData ( );
          else
            _registry.regKVLanguage.putData    (_registry.wsLanguage);

          _l10n.SetLanguage (selected);
        }
      }

      ImGui::TreePop         ( );

      ImGui::Spacing         ( );
    }

    constexpr char* StyleItems[UIStyle_COUNT] =
    { N_("Dynamic"),
      N_("SKIF Dark"),
      N_("SKIF Light"),
      N_("ImGui Classic"),
      N_("ImGui Dark")
    };
    static const char*
      StyleItemsCurrent;
      StyleItemsCurrent = StyleItems[_registry.iStyle]; // Re-apply the value on every frame as it may have changed

    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Color theme:"));
    ImGui::TreePush      ("ColorThemes");

    if (ImGui::BeginCombo ("###_registry.iStyleCombo", T_(StyleItemsCurrent))) // The second parameter is the label previewed before opening the combo.
    {
      for (int n = 0; n < UIStyle_COUNT; n++)
      {
        bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
        ImGui::PushID (n);
        if (ImGui::Selectable (T_(StyleItems[n]), is_selected))
          _registry.iStyleTemp = n;         // We apply the new style at the beginning of the next frame to prevent any PushStyleColor/Var from causing issues
        ImGui::PopID ( );
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
      }
      ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Useful if you find bright white covers an annoyance."));
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Dim game covers by 25%:"));
    ImGui::TreePush        ("DimCovers");
    if (SKIF_ImGui_RadioButton (L_("Never"),                 &_registry.iDimCovers, 0))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Always")));
    if (SKIF_ImGui_RadioButton (L_("Always"),                &_registry.iDimCovers, 1))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Based on mouse cursor")));
    if (SKIF_ImGui_RadioButton (L_("Based on mouse cursor"), &_registry.iDimCovers, 2))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Every time the UI renders a frame, Shelly the Ghost moves a little bit."));
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Show Shelly the Ghost:"));
    ImGui::SameLine        ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION);
    SKIF_ImGui_SetHoverTip (_("A critical rendering optimization will not function properly when Shelly is visibly moving.\nIt is therefor recommended to leave Shelly hidden in favor of a reduced GPU usage."));
    ImGui::TreePush        ("Shelly");
    if (SKIF_ImGui_RadioButton (L_("Never"),                    &_registry.iGhostVisibility, 0))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Always")));
    if (SKIF_ImGui_RadioButton (L_("Always"),                   &_registry.iGhostVisibility, 1))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("While service is running")));
    if (SKIF_ImGui_RadioButton (L_("While service is running"), &_registry.iGhostVisibility, 2))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Move the mouse over each option to get more information."));
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("UI elements:"));
    ImGui::TreePush        ("UIElements");

    ImGui::BeginGroup ( );

    if (SKIF_ImGui_Checkbox (L_("Borders"),    &_registry.bUIBorders))
    {
      _registry.regKVUIBorders.putData (_registry.bUIBorders);

      extern bool
        invalidateStyle;
        invalidateStyle = false;
    }

    SKIF_ImGui_SetHoverTip (_("Use borders around UI elements."));

    if (SKIF_ImGui_Checkbox (L_("Tooltips"),    &_registry.bUITooltips))
    {
      _registry.regKVUITooltips.putData (_registry.bUITooltips);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = _("Info: ");

    SKIF_ImGui_SetHoverText (_("This is instead where additional information will be displayed."));
    SKIF_ImGui_SetHoverTip  (_("If tooltips are disabled the status bar will be used for additional information.\n"
                             "Note that some links cannot be previewed as a result."));

    if (SKIF_ImGui_Checkbox (L_("Status bar"),   &_registry.bUIStatusBar))
    {
      _registry.regKVUIStatusBar.putData (_registry.bUIStatusBar);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    SKIF_ImGui_SetHoverTip (_("Disabling the status bar as well as tooltips will hide all additional information or tips."));

    ImGui::EndGroup ( );

    // New column, on the same row if all of its checkboxes fit
    if (SKIF_ImGui_SameLineIfFits (ImGui::GetStyle ( ).ItemSpacing.x + std::max ({ SKIF_ImGui_CheckboxWidth (L_("Large icons")),
                                                                                   SKIF_ImGui_CheckboxWidth (L_("Fade covers")),
                                                                                   SKIF_ImGui_CheckboxWidth (L_("Win11 corners")) })))
    {
      ImGui::Spacing  ( );
      ImGui::SameLine ( );
    }

    ImGui::BeginGroup ( );

    bool* pActiveBool = (_registry._TouchDevice) ? &_registry._TouchDevice : &_registry.bUILargeIcons;

    if (_registry._TouchDevice)
    {
      SKIF_ImGui_PushDisableState ( );
    }

    if (SKIF_ImGui_Checkbox (L_("Large icons"), pActiveBool))
      _registry.regKVUILargeIcons.putData (_registry.bUILargeIcons);

    if (_registry._TouchDevice)
    {
      SKIF_ImGui_PopDisableState  ( );
      SKIF_ImGui_SetHoverTip      (_("Currently enforced by touch input mode."));
    } else
      SKIF_ImGui_SetHoverTip      (_("Use larger game icons in the library tab."));

    if (SKIF_ImGui_Checkbox (L_("Fade covers"), &_registry.bFadeCovers))
    {
      _registry.regKVFadeCovers.putData (_registry.bFadeCovers);

      SKIF_LibraryArtwork::Get ( ).ResetCoverAlpha ( );
    }

    SKIF_ImGui_SetHoverTip (_("Fade between game covers when switching games."));

    if (SKIF_Util_IsWindows11orGreater ( ))
    {
      if ( SKIF_ImGui_Checkbox ( L_("Win11 corners"), &_registry.bWin11Corners) )
      {
        _registry.regKVWin11Corners.putData (  _registry.bWin11Corners);

        // Force recreating the window on changes
        RecreateWin32Windows = true;
      }

      SKIF_ImGui_SetHoverTip (_("Use rounded window corners."));
    }

    ImGui::EndGroup ( );

    // New column, on the same row if all of its checkboxes fit
    if (SKIF_ImGui_SameLineIfFits (ImGui::GetStyle ( ).ItemSpacing.x + std::max (SKIF_ImGui_CheckboxWidth (L_("Touch input")),
                                                                                  SKIF_ImGui_CheckboxWidth (L_("HiDPI scaling")))))
    {
      ImGui::Spacing  ( );
      ImGui::SameLine ( );
    }

    ImGui::BeginGroup ( );

    if ( SKIF_ImGui_Checkbox ( L_("Touch input"), &_registry.bTouchInput) )
    {
      _registry.regKVTouchInput.putData (  _registry.bTouchInput);

      extern bool
        invalidateStyle;
        invalidateStyle = false;
    }

    SKIF_ImGui_SetHoverTip (_("Make the UI easier to use on touch input capable devices automatically."));

    if (SKIF_ImGui_Checkbox (L_("HiDPI scaling"), &_registry.bDPIScaling))
    {
      extern bool
        changedHiDPIScaling;
        changedHiDPIScaling = true;
    }

    SKIF_ImGui_SetHoverTip (_("Disabling HiDPI scaling will make the application appear smaller on HiDPI displays."));

#if 0
    if ( ImGui::Checkbox ( "Auto-horizon mode", &_registry.bHorizonModeAuto) )
      _registry.regKVHorizonModeAuto.putData (   _registry.bHorizonModeAuto);

    SKIF_ImGui_SetHoverTip ("Switch to the horizontal mode on smaller displays automatically.");
#endif

    ImGui::EndGroup ( );

    if (! _registry.bUITooltips &&
        ! _registry.bUIStatusBar)
    {
      ImGui::BeginGroup  ( );
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::SameLine    ( );
      SKIF_ImGui_TextColoredWrapped (ImColor(0.68F, 0.68F, 0.68F, 1.0f), _("Context based information and tips will not appear!"));
      ImGui::EndGroup    ( );

      SKIF_ImGui_SetHoverTip (_("Restore context based information and tips by enabling tooltips or the status bar."), true);
    }

    ImGui::TreePop       ( );

    if (enableColums)
    {
      ImGui::NextColumn    ( );
      ImGui::TreePush      ("RightColumnSectionAppearance");
    }
    else {
      ImGui::Spacing       ( );
      ImGui::Spacing       ( );
    }

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Move the mouse over each option to get more information"));
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            _("UI mode:"));

    ImGui::TreePush        ("SKIF_iUIMode");
    // Flip VRR Compatibility Mode (only relevant on Windows 10+)
    if (SKIF_Util_IsWindows10OrGreater ( ))
    {
      if (SKIF_ImGui_RadioButton (L_("VRR Compatibility"), &_registry.iUIMode, 2))
      {
        _registry.regKVUIMode.putData (             _registry.iUIMode);
        RecreateSwapChains = true;
      }
      SKIF_ImGui_SetHoverTip (_("Avoids signal loss and flickering on VRR displays."));
      SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Normal")));
    }
    if (SKIF_ImGui_RadioButton (L_("Normal"),              &_registry.iUIMode, 1))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip (_("Improves UI response on low fixed-refresh rate displays."));
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Safe Mode")));
    if (SKIF_ImGui_RadioButton (L_("Safe Mode"),           &_registry.iUIMode, 0))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip (_("Compatibility mode for users experiencing issues with the other two modes."));
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (_("Increases the color depth of the app."));
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Color depth:"));

    static int placeholder = 0;
    static int* ptrSDR = nullptr;

    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive (NULL)))
    {
      SKIF_ImGui_PushDisableState ( );

      ptrSDR = &_registry.iHDRMode;
    }
    else
      ptrSDR = &_registry.iSDRMode;

    ImGui::TreePush        ("iSDRMode");
    if (SKIF_ImGui_RadioButton   ("8 bpc",        ptrSDR, 0))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    // It seems that Windows 10 1709+ (Build 16299) is required to
    // support 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model
    if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    {
      SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth ("10 bpc"));
      if (SKIF_ImGui_RadioButton ("10 bpc",       ptrSDR, 1))
      {
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        RecreateSwapChains = true;
      }
    }
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth ("16 bpc"));
    if (SKIF_ImGui_RadioButton   ("16 bpc",       ptrSDR, 2))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    ImGui::TreePop         ( );

    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive (NULL)))
    {
      SKIF_ImGui_PopDisableState  ( );
    }
    
    ImGui::TreePush        ("bLinearFontAlpha");
    if (*ptrSDR == 2)
    {
      if (SKIF_ImGui_Checkbox (L_("Linear font alpha blending"), &_registry.bLinearFontAlpha))
        _registry.regKVLinearFontAlpha.putData           (_registry.bLinearFontAlpha);

      SKIF_ImGui_SetHoverTip (_("Disables the correction that makes text blend\n"
                              "like they do in 8 bpc and 10 bpc modes."));
    }
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    if (SKIF_Util_IsHDRSupported (NULL)  )
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip (_("Makes the app pop more on HDR displays."));
      ImGui::SameLine        ( );
      SKIF_ImGui_TextColoredWrapped (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          _("High dynamic range (HDR):"));

      ImGui::TreePush        ("iHDRMode");

      if (_registry.iUIMode == 0)
      {
        SKIF_ImGui_TextColoredWrapped   (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("HDR support is disabled while the UI is in Safe Mode."));
      }

      else if (SKIF_Util_IsHDRActive (NULL))
      {
        if (SKIF_ImGui_RadioButton (L_("No"),             &_registry.iHDRMode, 0))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }
// Disable support for HDR10, it looks terrible and I want it gone.
#if 0
        ImGui::SameLine        ( );
        if (ImGui::RadioButton ("HDR10 (10 bpc)", &_registry.iHDRMode, 1))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }
#endif
        SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth ("scRGB (16 bpc)"));
        if (SKIF_ImGui_RadioButton ("scRGB (16 bpc)", &_registry.iHDRMode, 2))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }

        ImGui::Spacing         ( );

        // HDR Brightness

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PushDisableState ( );

        // TRANSLATORS: {0} is the brightness in nits (cd/m2)
        const std::string nitsFormat = SKIF_L10n_PrintfFormat (_("{0} nits"), "%d");

        if (SKIF_ImGui_SliderInt (L_("HDR brightness"), &_registry.iHDRBrightness, 80, 400, nitsFormat.c_str ( )))
        {
          // Reset to 203 nits (default; HDR reference white for BT.2408) if negative or zero
          if (_registry.iHDRBrightness <= 0)
              _registry.iHDRBrightness  = 203;

          // Keep the nits value between 80 and 400
          _registry.iHDRBrightness = std::min (std::max (80, _registry.iHDRBrightness), 400);
          _registry.regKVHDRBrightness.putData (_registry.iHDRBrightness);
        }

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PopDisableState  ( );
      }

      else {
        SKIF_ImGui_TextColoredWrapped   (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Your display(s) supports HDR, but does not use it."));
      }

      if (SKIF_Util_GetHotKeyStateHDRToggle ( ) && _registry.iUIMode != 0)
      {
        ImGui::Spacing         ( );
        /*
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped    ("FYI: Use " ICON_FA_WINDOWS " + Ctrl + Shift + H while this app is running to toggle "
                               "HDR for the display the mouse cursor is currently located on.");
        ImGui::PopStyleColor  ( );
        */

        // TRANSLATORS: {0} is the keyboard shortcut (Ctrl + Windows logo + Shift + H), {1} is a mouse cursor icon
        SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled),
          F_("Use {0} to toggle HDR where the {1} is.", "Ctrl + " ICON_FA_WINDOWS " + Shift + H", ICON_FA_ARROW_POINTER).c_str ( ));
      }

      ImGui::TreePop         ( );
    }

//#define COLORPICKER
#ifdef COLORPICKER
    // 2026-10-13: sRGBtoLinear isn't really relevant any longer after the recent changes in imgui_pix_shader.hlsl
    if (ImGui::Checkbox     ("sRGBtoLinear colors", &_registry._sRGBColors))
    {
      extern bool
        invalidateStyle;
        invalidateStyle = true;
    }

    static float col3[3];
    ImGui::ColorPicker3 ("Colors", col3);
#endif

    if (enableColums)
    {
      ImGui::TreePop     ( );
      ImGui::Columns     (1);
    }
    else
      ImGui::EndGroup    ( );

    ImGui::PopStyleColor ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Keybindings
  if (ImGui::CollapsingHeader (L_("Keybindings###SKIF_SettingsHeader-3"), ImGuiTreeNodeFlags_DefaultOpen))
  {
    SKIF_ImGui_Spacing      ( );

    // The fields start at 200 px, or after the widest (translated) name, so they line up in every language
    float fKeybindFieldX = 200.0f * SKIF_ImGui_GlobalDPIScale;

    for (auto& keybind : keybinds)
    {
      if (keybind._show())
        fKeybindFieldX = std::max (fKeybindFieldX, ImGui::GetCursorPosX ( ) + ImGui::GetStyle ( ).ItemSpacing.x * 2.0f +
                                                   ImGui::CalcTextSize (T_(keybind._key->bind_name.c_str())).x);
    }

    for (auto& keybind : keybinds)
    {
      if (! keybind._show())
        continue;

      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "%s", T_(keybind._key->bind_name.c_str()));
      ImGui::SameLine         ( );
      ImGui::ItemSize         (ImVec2 (fKeybindFieldX - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine         ( );
      // 300 px wide, narrower if the window is
      ImGui::SetNextItemWidth (std::max (ImGui::GetFrameHeight ( ) * 4.0f, std::min (300.0f * SKIF_ImGui_GlobalDPIScale, ImGui::GetContentRegionAvail ( ).x)));
      if (SK_ImGui_Keybinding (keybind._key))
      {
        // Only update the registry if we are done assigning
        if (! keybind._key->assigning)
          keybind._reg->putData (keybind._key->saved.human_readable);

        keybind._callback (keybind._key);
      }
    }
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Keybindings (In-game)
  if (ImGui::CollapsingHeader (L_("Keybindings (In-Game)###SKIF_SettingsHeader-3b")))
  {
    SKIF_ImGui_Spacing      ( );

    auto _DrawKeybindRow = [](IniEntry* trie) -> void
    {
      static std::string section;

      if (section != trie->section)
      {
        section = trie->section;
        SKIF_ImGui_Spacing   ( );
        ImGui::SeparatorText (trie->section);
      }

      ImGui::Spacing         ( );
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), trie->key);
      if (trie->description != nullptr)
        SKIF_ImGui_SetHoverTip (trie->description);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (200.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      if (trie->DrawFunction (trie))
        SKIF_IniReader_SaveOSDIni ( );
    };

    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                            );

    char* order[] = {
      "ImGui.Global",
      "Screenshot.System",
      "Game.HUD"
    };

    extern std::vector <IniEntry>
                      g_vIniReaderOSD;
    for (auto& trie : g_vIniReaderOSD)
    {
      if (trie._type != ParameterKeybind)
        continue;

      if (strcmp (trie.key, "ControlPanelToggle"))
        continue;

      _DrawKeybindRow (&trie);
    }
    for (auto& trie : g_vIniReaderOSD)
    {
      if (trie._type != ParameterKeybind)
        continue;

      if (! strcmp (trie.key, "ControlPanelToggle"))
        continue;

      _DrawKeybindRow (&trie);
    }

    if (g_vIniReaderOSD.empty())
      ImGui::TextWrapped ("%s", _("Launch a game with Special K at least once to populate this section."));

    ImGui::PopStyleColor  ( ); // ImGuiCol_Text
  }

  ImGui::Spacing ();
  ImGui::Spacing ();

#pragma endregion

#pragma region Section: Advanced
  if (ImGui::CollapsingHeader (L_("Advanced###SKIF_SettingsHeader-4"), ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_ADVANCED", true);
      ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    if ( SKIF_ImGui_Checkbox ( L_("Automatically install new updates"),              &_registry.bAutoUpdate ) )
      _registry.regKVAutoUpdate.putData (                                     _registry.bAutoUpdate);

    // TRANSLATORS: {0} is a mouse cursor icon
    if ( SKIF_ImGui_Checkbox ( (F_("Open this app on the same monitor as the {0}", " " ICON_FA_ARROW_POINTER) + "###OpenAtCursorPosition").c_str ( ), &_registry.bOpenAtCursorPosition ) )
      _registry.regKVOpenAtCursorPosition.putData (                           _registry.bOpenAtCursorPosition );

    if ( SKIF_ImGui_Checkbox ( L_("Open this app in the mini service mode view"), &_registry.bOpenInMiniMode) )
      _registry.regKVOpenInMiniMode.putData (                              _registry.bOpenInMiniMode);

    /*
    if (! SKIF_Util_GetDragFromMaximized ( ))
      SKIF_ImGui_PushDisableState ( );

    if (ImGui::Checkbox  ("Reposition this app to the center on double click",
                                                      &_registry.bMaximizeOnDoubleClick))
      _registry.regKVMaximizeOnDoubleClick.putData  (  _registry.bMaximizeOnDoubleClick);

    if (! SKIF_Util_GetDragFromMaximized ( ))
    {
      SKIF_ImGui_PopDisableState ( );
      SKIF_ImGui_SetHoverTip ("Feature is inaccessible due to snapping and/or\n"
                              "drag from maximized being disabled in Windows.");
    }
      */

    if (SKIF_ImGui_Checkbox  (L_("Do not stop the injection service on exit"),
                                                      &_registry.bAllowBackgroundService))
      _registry.regKVAllowBackgroundService.putData (  _registry.bAllowBackgroundService);

    SKIF_ImGui_SetHoverTip (_("This allows the injection service to remain running even after this app has been closed."));

    if (SKIF_ImGui_Checkbox  (L_("Use hardware acceleration"),&_registry.bHardwareAcceleration))
    {
      _registry.regKVHardwareAcceleration.putData   (  _registry.bHardwareAcceleration);
      extern bool RecreateD3DDevice;
      RecreateD3DDevice = true;
    }

    ImGui::SameLine        ( );

    if (SKIF_Util_IsHybridCPU ( ))
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION);
      SKIF_ImGui_SetHoverTip (_("The app will be less responsive if disabled on hybrid CPUs."));
    }

    else {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip (_("The app may be less responsive if disabled."));
    }

    if (SKIF_ImGui_Checkbox  (L_("Use system proxy settings"),  &_registry.bUseSystemProxy))
      _registry.regKVUseSystemProxy.putData         (  _registry.bUseSystemProxy);

    SKIF_ImGui_SetHoverTip (_("Downloads go through the proxy configured in Windows.\n"
                            "Automatic proxy detection and setup scripts are not used.\n"
                            "If disabled, the app always connects directly."));

    if (_registry.bCloseToTray)
    {
      ImGui::BeginGroup   ( );
      SKIF_ImGui_PushDisableState ( );
    }

    if ( SKIF_ImGui_Checkbox (
            L_("Allow multiple instances of this app"),
              &_registry.bAllowMultipleInstances )
        )
    {
      if (! _registry.bAllowMultipleInstances)
      {
        // Immediately close out any duplicate instances, they're undesirables
        EnumWindows ( []( HWND   hWnd,
                          LPARAM lParam ) -> BOOL
        {
          wchar_t                         wszRealWindowClass [64] = { };
          if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
          {
            if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
            {
              if (SKIF_Notify_hWnd != hWnd) // Don't send WM_QUIT to ourselves
                PostMessage (  hWnd, WM_QUIT,
                                0x0, 0x0  );
            }
          }
          return TRUE;
        }, (LPARAM)SKIF_NotifyIcoClass);
      }

      _registry.regKVAllowMultipleInstances.putData (
        _registry.bAllowMultipleInstances
        );
    }

    if (_registry.bCloseToTray)
    {
      SKIF_ImGui_PopDisableState  ( );
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::EndGroup        ( );
      SKIF_ImGui_SetHoverTip (_("Requires 'Close to the notification area' to be disabled."));
    }

    /* Not exposed since its not finalized (and I am still not sure if I want to have this or not...)
    if (ImGui::Checkbox  ("Controller support",
                                                      &_registry.bControllers))
      _registry.regKVControllers.putData              (_registry.bControllers);

    SKIF_ImGui_SetHoverTip  ("Allow using a controller to navigate the UI of this app.");
    */

    if (enableColums)
    {
      ImGui::NextColumn    ( );
      ImGui::TreePush      ("RightColumnSectionAdvanced");
    }
    else {
      ImGui::Spacing       ( );
      ImGui::Spacing       ( );
    }

    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        (std::string (ICON_FA_WRENCH "  ") + _("Troubleshooting:")).c_str ( ));

    SKIF_ImGui_Spacing ( );

    const char* LogSeverity[] = { NC_("log level", "None"),
                                  NC_("log level", "Fatal"),
                                  NC_("log level", "Error"),
                                  NC_("log level", "Warning"),
                                  NC_("log level", "Info"),
                                  NC_("log level", "Debug"),
                                  NC_("log level", "Verbose") };
    static const char* LogSeverityCurrent = LogSeverity[_registry.iLogging];

    const std::string labelLogLevel = std::string (" ") + _("Log level") + "###_registry.iLoggingCombo";
    SKIF_ImGui_SetNextItemWidthForLabel (labelLogLevel.c_str ( ));
    if (ImGui::BeginCombo (labelLogLevel.c_str ( ), CT_("log level", LogSeverityCurrent)))
    {
      for (int n = 0; n < IM_ARRAYSIZE (LogSeverity); n++)
      {
        bool is_selected = (LogSeverityCurrent == LogSeverity[n]);
        ImGui::PushID (n);
        const bool clicked = ImGui::Selectable (CT_("log level", LogSeverity[n]), is_selected);
        ImGui::PopID ( );
        if (clicked)
        {
          _registry.iLogging = n;
          _registry.regKVLogging.putData  (_registry.iLogging);
          LogSeverityCurrent = LogSeverity[_registry.iLogging];
          plog::get()->setMaxSeverity((plog::Severity)_registry.iLogging);

          ImGui::GetCurrentContext()->DebugLogFlags =  ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                    ? (ImGuiDebugLogFlags_EventMask_ & ~ImGuiDebugLogFlags_EventInputRouting)
                                                    :  ImGuiDebugLogFlags_EventViewport);
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    if (_registry.iLogging >= 6 && _registry.bDeveloperMode)
    {
      if (SKIF_ImGui_Checkbox  (L_("Enable excessive development logging"), &_registry.bLoggingDeveloper))
      {
        _registry.regKVLoggingDeveloper.putData                     (_registry.bLoggingDeveloper);

        ImGui::GetCurrentContext()->DebugLogFlags =  ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                  ? (ImGuiDebugLogFlags_EventMask_ & ~ImGuiDebugLogFlags_EventInputRouting)
                                                  :  ImGuiDebugLogFlags_EventViewport);
      }
    }

    SKIF_ImGui_SetHoverTip  (_("Only intended for SKIF developers as this enables excessive logging (e.g. window messages)."));

    SKIF_ImGui_Spacing ( );

    const char* Diagnostics[] = { NC_("diagnostics", "None"),
                                  NC_("diagnostics", "Normal"),
                                  NC_("diagnostics", "Enhanced") };
    static const char* DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];

    const std::string labelDiagnostics = std::string (" ") + _("Diagnostics") + "###_registry.iDiagnostics";
    SKIF_ImGui_SetNextItemWidthForLabel (labelDiagnostics.c_str ( ));
    if (ImGui::BeginCombo (labelDiagnostics.c_str ( ), CT_("diagnostics", DiagnosticsCurrent)))
    {
      for (int n = 0; n < IM_ARRAYSIZE (Diagnostics); n++)
      {
        bool is_selected = (DiagnosticsCurrent == Diagnostics[n]);
        ImGui::PushID (n);
        const bool clicked = ImGui::Selectable (CT_("diagnostics", Diagnostics[n]), is_selected);
        ImGui::PopID ( );
        if (clicked)
        {
          _registry.iDiagnostics = n;
          _registry.regKVDiagnostics.putData (_registry.iDiagnostics);
          DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    ImGui::SameLine    ( );
    ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  (_("Help improve Special K by allowing anonymized diagnostics to be sent.\n"
                             "The data is used to identify issues, highlight common use cases, and\n"
                             "facilitates the continued development of the application."));

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/Privacy");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/Privacy");

    SKIF_ImGui_Spacing ( );

    if (SKIF_ImGui_Checkbox  (L_("Developer mode"),  &_registry.bDeveloperMode))
    {
      _registry.regKVDeveloperMode.putData   (_registry.bDeveloperMode);

      ImGui::GetCurrentContext()->DebugLogFlags =  ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                ? (ImGuiDebugLogFlags_EventMask_ & ~ImGuiDebugLogFlags_EventInputRouting)
                                                :  ImGuiDebugLogFlags_EventViewport);
    }

    SKIF_ImGui_SetHoverTip  (_("Exposes additional information and context menu items that may be of interest for developers."));

    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Efficiency mode")));

    if (SKIF_ImGui_Checkbox  (L_("Efficiency mode"), &_registry.bEfficiencyMode))
      _registry.regKVEfficiencyMode.putData  (_registry.bEfficiencyMode);

    SKIF_ImGui_SetHoverTip  (_("Engage efficiency mode for this app when idle.\n"
                             "Not recommended for Windows 10 and earlier."));

    SKIF_ImGui_Spacing ( );

    ImGui::TextWrapped ("%s", _("Nvidia users: Use the below button to prevent GeForce Experience from mistaking this app for a game."));

    ImGui::Spacing     ( );

    static bool runOnceGFE = false;

    if (runOnceGFE)
      SKIF_ImGui_PushDisableState ( );

    // At least as wide as before, wider if the translation needs it
    const char* labelDisableGFE = LI_(ICON_FA_USER_SHIELD, "Disable GFE notifications");
    if (ImGui::ButtonEx (labelDisableGFE,
                                 ImVec2 (std::max (250 * SKIF_ImGui_GlobalDPIScale, ImGui::CalcTextSize (labelDisableGFE, NULL, true).x + 2 * ImGui::GetStyle ( ).FramePadding.x),
                                          25 * SKIF_ImGui_GlobalDPIScale)))
    {
      runOnceGFE = true;

      PLOG_INFO << "Attempting to disable GeForce Experience / ShadowPlay notifications...";
      wchar_t              wszRunDLL32 [MAX_PATH + 2] = { };
      GetSystemDirectoryW (wszRunDLL32, MAX_PATH);
      PathAppendW         (wszRunDLL32, L"rundll32.exe");
      static std::wstring wsDisableCall = SK_FormatStringW (
        LR"("%ws\%ws",RunDLL_DisableGFEForSKIF)",
          _path_cache.specialk_install,
#ifdef _WIN64
          L"SpecialK64.dll"
#else
          L"SpecialK32.dll"
#endif
        );

      SHELLEXECUTEINFOW
        sexi              = { };
        sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
        sexi.lpVerb       = L"RUNAS";
        sexi.lpFile       = wszRunDLL32;
      //sexi.lpDirectory  = ;
        sexi.lpParameters = wsDisableCall.c_str();
        sexi.nShow        = SW_SHOWNORMAL;
        sexi.fMask        = SEE_MASK_NOASYNC | SEE_MASK_NOZONECHECKS;

      SetLastError (NO_ERROR);

      bool ret = ShellExecuteExW (&sexi);

      if (GetLastError ( ) != NO_ERROR)
        PLOG_ERROR << "An unexpected error occurred: " << SKIF_Util_GetErrorAsWStr();

      if (ret)
        PLOG_INFO  << "The operation was successful.";
      else
        PLOG_ERROR << "The operation was unsuccessful.";
    }

    // Prevent this call from executing on the same frame as the button is pressed
    else if (runOnceGFE)
      SKIF_ImGui_PopDisableState ( );

    ImGui::SameLine         ( );
    ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  (_("This only needs to be used if GeForce Experience notifications\n"
                             "appear on the screen whenever this app is being used."));

    if (enableColums)
    {
      ImGui::TreePop        ( );
      ImGui::Columns        (1);
    }
    else
      ImGui::EndGroup       ( );

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Whitelist / Blacklist
  if (ImGui::CollapsingHeader (L_("Whitelist / Blacklist###SKIF_SettingsHeader-5"))) //, ImGuiTreeNodeFlags_DefaultOpen)) // Disabled auto-open for this section
  {
    static bool white_edited = false,
                black_edited = false,
                white_stored = true,
                black_stored = true;

    // Local copies of the lists that the text boxes edit. Changes are only committed to
    //   _inject.whitelist/blacklist (which flags the regex patterns for recompilation) on save.
    static char whitelist_edit[sizeof (SKIF_InjectionContext::whitelist)] = { },
                blacklist_edit[sizeof (SKIF_InjectionContext::blacklist)] = { };

    // Revisions of the committed lists that the copies were last refreshed from
    static int  whitelist_rev = -1,
                blacklist_rev = -1;

    // Refresh the copies when a new revision has been committed and there are no pending edits
    //   (covers the initial load, saving, and patterns added from the Monitor tab)
    if (! white_edited && whitelist_rev != _inject.whitelistRevision)
    {
      strncpy_s (whitelist_edit, sizeof whitelist_edit, _inject.whitelist, _TRUNCATE);
      whitelist_rev = _inject.whitelistRevision;
    }

    if (! black_edited && blacklist_rev != _inject.blacklistRevision)
    {
      strncpy_s (blacklist_edit, sizeof blacklist_edit, _inject.blacklist, _TRUNCATE);
      blacklist_rev = _inject.blacklistRevision;
    }

    auto _CheckWarnings = [](char* szList)->void
    {
      static int i, count;

      if (strchr (szList, '\"') != nullptr)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_TRIANGLE_EXCLAMATION);
        // TRANSLATORS: {0} is a double quote character
        ImGui::SameLine   (); SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), F_("Please remove all double quotes {0} from the list.", "\"").c_str ( ));
        ImGui::EndGroup   ();
      }

      // Loop through the list, checking the existance of a lone \ not proceeded or followed by other \.
      // i == 0 to prevent szList[i - 1] from executing when at the first character.
      for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 512 * 2; i++)
        count += (szList[i] == '\\' && szList[i + 1] != '\\' && (i == 0 || szList[i - 1] != '\\'));

      if (count > 0)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_LIGHTBULB);
        // TRANSLATORS: {0} is two backslashes, {1} is one backslash
        ImGui::SameLine   (); SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), F_("Folders must be separated using two backslashes {0} instead of one {1} backslash.", R"(\\)", R"(\)").c_str ( ));
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          R"(e.g. C:\\Program Files (x86)\\Uplay\\games)"
        );
      }

      // Loop through the list, counting the number of occurances of a newline
      for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 512 * 2; i++)
        count += (szList[i] == '\n');

      if (count >= 512)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_LIGHTBULB);
        // TRANSLATORS: {0} is the maximum number of lines (512), {1} is the pipe character
        ImGui::SameLine   (); SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), F_("The list can only include {0} lines, though multiple can be combined using a pipe {1} character.", 512, "|").c_str ( ));
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          _("(e.g. \"NieRAutomataPC|Epic Games\" will match any application\n"
          "installed under a NieRAutomataPC or Epic Games folder.)"));
      }
    };

    ImGui::BeginGroup ();
    ImGui::Spacing    ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    ("%s", _("The following lists manage Special K in processes as patterns are matched against the full path of the injected process."));

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::TextWrapped        ("%s", _("Easiest is to use the name of the executable or folder of the game."));
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      _("e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at\n"
      "C:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"));

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::TextWrapped        ("%s", _("Typing the name of a shared parent folder will match all applications below that folder."));
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      _("e.g. a pattern like \"Epic Games\" will match any\n"
      "application installed under the Epic Games folder."));

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::TextWrapped ("%s", _("Note that these lists do not prevent Special K from being injected into processes."));
    ImGui::EndGroup   ();

    if (! _registry.bUITooltips)
    {
      SKIF_ImGui_SetHoverTip (
        _("These lists control whether Special K should be enabled (the whitelist) to hook APIs etc,\n"
        "or remain disabled/idle/inert (the blacklist) within the injected process."));
    }

    else
    {
      SKIF_ImGui_SetHoverTip (
        _("The injection service injects Special K into any process that deals\n"
        "with system input or some sort of window or kb/mouse input activity.\n"
        "\n"
        "These lists control whether Special K should be enabled (the whitelist),\n"
        "or remain idle/inert (the blacklist) within the injected process."));
    }

    ImGui::PopStyleColor  ();

    ImGui::NewLine    ();

    // Whitelist section

    ImGui::BeginGroup ();

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_SQUARE_PLUS);
    ImGui::SameLine    ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Whitelist Patterns:"));

    SKIF_ImGui_Spacing ();

    white_edited |=
      ImGui::InputTextEx ( "###WhitelistPatterns", "SteamApps\nEpic Games\\\\\nGOG Galaxy\\\\Games\nOrigin Games\\\\",
                              whitelist_edit, sizeof whitelist_edit,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                         150 * SKIF_ImGui_GlobalDPIScale ), // 120 // 150
                                  ImGuiInputTextFlags_Multiline );

    if (*whitelist_edit == '\0')
    {
      SKIF_ImGui_SetHoverTip (
        _("These are the patterns used internally to enable Special K for these specific platforms.\n"
        "They are presented here solely as examples of how a potential pattern might look like."));
    }

    _CheckWarnings (whitelist_edit);

    ImGui::EndGroup   ();

    ImGui::SameLine   ();

    ImGui::BeginGroup ();

#if 0
    ImGui::TextColored (ImColor(255, 207, 72), ICON_FA_FOLDER_PLUS);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Add Common Patterns:"
    );

    SKIF_ImGui_Spacing ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    ("Click on an item below to add it to the whitelist, or hover over it "
                            "to display more information about what the pattern covers.");
    ImGui::PopStyleColor  ();

    SKIF_ImGui_Spacing ();
    SKIF_ImGui_Spacing ();

    ImGui::SameLine    ();
    ImGui::BeginGroup  ();
    ImGui::TextColored ((_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_WINDOWS);
  //ImGui::TextColored ((_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_XBOX);
    ImGui::EndGroup    ();

    ImGui::SameLine    ();

    ImGui::BeginGroup  ();
    if (ImGui::Selectable ("Games"))
    {
      white_edited = true;

      _inject.WhitelistPattern ("Games");
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on most platforms, such as Uplay, as\n"
      "most of them have \"games\" in the full path somewhere."
    );

    /*
    if (ImGui::Selectable ("WindowsApps"))
    {
      white_edited = true;

      _inject.AddUserListPattern("WindowsApps", true);
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on the Microsoft Store or Game Pass."
    );
    */

    ImGui::EndGroup ();

#endif

    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Blacklist section

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_SQUARE_MINUS);
    ImGui::SameLine    ( );
    SKIF_ImGui_TextColoredWrapped (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        _("Blacklist Patterns:"));
    ImGui::SameLine    ( );
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), _("(Does not prevent injection! Use this to exclude stuff from being whitelisted)"));

    SKIF_ImGui_Spacing ();

    black_edited |=
      ImGui::InputTextEx ( "###BlacklistPatterns", "launcher.exe",
                              blacklist_edit, sizeof blacklist_edit,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                         120 * SKIF_ImGui_GlobalDPIScale ),
                                  ImGuiInputTextFlags_Multiline );

    _CheckWarnings (blacklist_edit);

    ImGui::Separator ();

    bool bDisabled =
      (white_edited || black_edited) ?
                                false : true;

    bool hotkeyCtrlS = false;

    if (bDisabled)
      SKIF_ImGui_PushDisableState ( );
    else
      hotkeyCtrlS = ImGui::GetIO().KeyCtrl               &&
                    ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f &&
                    SKIF_Hotkey_MainWindowFocused ( ); // Not from an INI editor (Ctrl+S saves the file there)

    // Hotkey: Ctrl+S
    if (ImGui::Button (LI_(ICON_FA_FLOPPY_DISK, "Save Changes")) || hotkeyCtrlS)
    {
      // Clear the active ID to prevent ImGui from holding outdated copies of the variable
      //   if saving succeeds, to allow the copy to be refreshed with the cleaned up list
      ImGui::ClearActiveID();

      if (white_edited)
      {
        white_stored = _inject.SaveWhitelist (whitelist_edit);

        if (white_stored)
          white_edited = false;
      }

      if (black_edited)
      {
        black_stored = _inject.SaveBlacklist (blacklist_edit);

        if (black_stored)
          black_edited = false;
      }
    }

    ImGui::SameLine ();

    if (ImGui::Button (LI_(ICON_FA_ROTATE_LEFT, "Reset")))
    {
      // Discard the edits and force the copies to be refreshed from the committed lists above
      if (white_edited)
      {
        white_edited  = false;
        whitelist_rev = -1;
        white_stored  = true;
      }

      if (black_edited)
      {
        black_edited  = false;
        blacklist_rev = -1;
        black_stored  = true;
      }
    }

    if (bDisabled)
      SKIF_ImGui_PopDisableState  ( );

    ImGui::Spacing();

    if (! white_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), _("The whitelist could not be saved! Please remove any non-Latin characters and try again."));
    }

    if (! black_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), _("The blacklist could not be saved! Please remove any non-Latin characters and try again."));
    }

    ImGui::EndGroup       ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

}


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
#include <utility/injection.h>
#include <utility/updater.h>
#include <utility/gamepad.h>

extern bool allowShortcutCtrlA;

void
SKIF_UI_Tab_DrawSettings (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  
  static bool HDRSupported = false;

  static const std::wstring valvePlugPath = SK_FormatStringW (LR"(%ws\XInput1_4.dll)", _path_cache.steam_install);
  static       bool         valvePlug     = false;

  float columnWidth = 0.5f * ImGui::GetContentRegionAvail().x; // Needs to be before the SKIF_ImGui_Columns() call
  bool enableColums = (ImGui::GetContentRegionAvail().x / SKIF_ImGui_GlobalDPIScale >= 750.f);

  // Refresh things when visiting from another tab or when forced
  if (SKIF_Tab_Selected != UITab_Settings                         ||
      RefreshSettingsTab                                          )
  {
    SKIF_Util_IsHDRSupported (true);
    SKIF_Util_IsHDRActive    (true);
    RefreshSettingsTab  = false;
    valvePlug = (SKIF_Util_GetProductName (valvePlugPath.c_str()).find(L"Valve Plug") != std::wstring::npos);

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

  if ( ImGui::Checkbox ( "Low bandwidth mode",                          &_registry.bLowBandwidthMode ) )
    _registry.regKVLowBandwidthMode.putData (                            _registry.bLowBandwidthMode );
          
  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (
    "For new games/covers, low resolution images will be preferred over high-resolution ones.\n"
    "This only affects new downloads of covers. It does not affect already downloaded covers.\n"
    "This will also disable automatic downloads of new updates to Special K."
  );
            
  if ( ImGui::Checkbox ( "Minimize when launching a game",            &_registry.bMinimizeOnGameLaunch ) )
    _registry.regKVMinimizeOnGameLaunch.putData (                      _registry.bMinimizeOnGameLaunch );
            
  if ( ImGui::Checkbox ( "Restore after closing a game",              &_registry.bRestoreOnGameExit ) )
    _registry.regKVRestoreOnGameExit.putData    (                      _registry.bRestoreOnGameExit );

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("Requires Special K injected to work as intended.");
  
  if (_registry.bAllowMultipleInstances)
  {
    ImGui::BeginGroup   ( );
    SKIF_ImGui_PushDisableState ( );
  }

  if ( ImGui::Checkbox ( "Close to the notification area", &_registry.bCloseToTray ) )
    _registry.regKVCloseToTray.putData (                    _registry.bCloseToTray );

  if (_registry.bAllowMultipleInstances)
  {
    SKIF_ImGui_PopDisableState  ( );
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip ("Requires 'Allow multiple instances of this app' to be disabled.");
  }

  _inject._StartAtLogonCtrl ( );


  if ( ImGui::Checkbox ( "Controller support", &_registry.bControllers ) )
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
  SKIF_ImGui_SetHoverTip ("Allows the UI to be controlled using " ICON_FA_XBOX " or " ICON_FA_PLAYSTATION " controllers, and adds special features while SKIF is running.");

  ImGui::SameLine        ( );

  if (! _registry.bControllers)
    SKIF_ImGui_PushDisableState ();

  if ( ImGui::ButtonEx ( ICON_FA_GAMEPAD "  Config", ImVec2 ( 115 * SKIF_ImGui_GlobalDPIScale,
                                                               25 * SKIF_ImGui_GlobalDPIScale ) ) )
    ImGui::OpenPopup ("Special K Input Config###SKInput_GamepadCfg");

  if (_registry.bControllers)
  {
    if (ImGui::BeginPopup ("Special K Input Config###SKInput_GamepadCfg",
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::SeparatorText ("General Controller Settings");
      ImGui::TreePush      ("");
      bool bEnableScreenSaverChord = _registry.skinput.dwScreenSaverChordBehavior != 0 &&
                                     _registry.skinput.dwScreenSaverChord         != 0;
      if (ImGui::Checkbox  ("Activate Screen Saver Using  " ICON_FA_XBOX "/" ICON_FA_PLAYSTATION "  + ", &bEnableScreenSaverChord))
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
        SKIF_ImGui_SetHoverTip ("The controller binding is configurable by clicking the text after " ICON_FA_XBOX " / " ICON_FA_PLAYSTATION);

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
          case XINPUT_GAMEPAD_LEFT_SHOULDER:  selected = ImGui::Selectable ("LB / L1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_TRIGGER:   selected = ImGui::Selectable ("LT / L2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_THUMB:     selected = ImGui::Selectable ("LS / L3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_SHOULDER: selected = ImGui::Selectable ("RB / R1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_TRIGGER:  selected = ImGui::Selectable ("RT / R2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_THUMB:    selected = ImGui::Selectable ("RS / R3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x1:                           selected = ImGui::Selectable ("A / Cross",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x3/*
               XINPUT_GAMEPAD_DPAD_UP*/:      selected = ImGui::Selectable ("Up",            true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_DOWN:      selected = ImGui::Selectable ("Down",          true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_LEFT:      selected = ImGui::Selectable ("Left",          true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_RIGHT:     selected = ImGui::Selectable ("Right",         true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_START:          selected = ImGui::Selectable ("Start",         true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_BACK:           selected = ImGui::Selectable ("Back / Select", true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_Y:              selected = ImGui::Selectable ("Y / Triangle",  true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_A:              selected = ImGui::Selectable ("A / Cross",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_X:              selected = ImGui::Selectable ("X / Square",    true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_B:              selected = ImGui::Selectable ("B / Circle",    true, ImGuiSelectableFlags_DontClosePopups); break;
        }
        ImGui::PopID ();

        static bool open = false;
        if (selected)
        {
          ImGui::OpenPopup ("ChordBinding_ScreenSaver");
          open = true;
        }

        if (ImGui::BeginPopupModal ("ChordBinding_ScreenSaver", &open, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
        {
          selected = false;

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

          ImGui::EndPopup ();
        }
      }

      bool                                                            bGamepadsDeactivateScreenSaver =
              _registry.skinput.dwGamepadsDeactivateScreenSaver != 0;
      if (ImGui::Checkbox ("Gamepad Input Deactivates Screen Saver", &bGamepadsDeactivateScreenSaver))
      {
        _registry.regKVControllerGamepadsDeactivateScreenSaver.putData (
          _registry.skinput.dwGamepadsDeactivateScreenSaver =
                             bGamepadsDeactivateScreenSaver ? 1 : 0
        );
      }

      if (! bGamepadsDeactivateScreenSaver)
        ImGui::BulletText  ("Pressing " ICON_FA_XBOX " on Xbox controllers always deactivates.");

      ImGui::TreePop       (  );
      ImGui::Spacing       (  );
      ImGui::Spacing       (  );
      ImGui::SeparatorText ("PlayStation Power Management");
      ImGui::TreePush      ("");

      bool bEnablePowerOffChord = _registry.skinput.dwPowerOffChordBehavior != 0 &&
                                  _registry.skinput.dwPowerOffChord         != 0;
      if (ImGui::Checkbox  ("Power Off Controllers Using      " ICON_FA_PLAYSTATION "   + ", &bEnablePowerOffChord))
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
        SKIF_ImGui_SetHoverTip ("The controller binding is configurable by clicking the text after " ICON_FA_PLAYSTATION);

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
          case XINPUT_GAMEPAD_LEFT_SHOULDER:  selected = ImGui::Selectable ("L1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_TRIGGER:   selected = ImGui::Selectable ("L2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_THUMB:     selected = ImGui::Selectable ("L3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_SHOULDER: selected = ImGui::Selectable ("R1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_TRIGGER:  selected = ImGui::Selectable ("R2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_THUMB:    selected = ImGui::Selectable ("R3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x1:                           selected = ImGui::Selectable ("Triangle", true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x3/*
               XINPUT_GAMEPAD_DPAD_UP*/:      selected = ImGui::Selectable ("Up",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_DOWN:      selected = ImGui::Selectable ("Down",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_LEFT:      selected = ImGui::Selectable ("Left",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_RIGHT:     selected = ImGui::Selectable ("Right",    true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_START:          selected = ImGui::Selectable ("Start",    true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_BACK:           selected = ImGui::Selectable ("Select",   true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_Y:              selected = ImGui::Selectable ("Triangle", true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_A:              selected = ImGui::Selectable ("Cross",    true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_X:              selected = ImGui::Selectable ("Square",   true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_B:              selected = ImGui::Selectable ("Circle",   true, ImGuiSelectableFlags_DontClosePopups); break;
        }
        ImGui::PopID ();

        static bool open = false;
        if (selected)
        {
          ImGui::OpenPopup ("ChordBinding_PowerOff");
          open = true;
        }

        if (ImGui::BeginPopupModal ("ChordBinding_PowerOff", &open, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
        {
          selected = false;

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

          ImGui::EndPopup ();
        }
      }

      float fIdleMinutes =
        static_cast <float> (_registry.skinput.dwIdleTimeoutInSecs) / 60.0f;

      float fLastIdle = fIdleMinutes;

      if (ImGui::SliderFloat ("Idle Behavior", &fIdleMinutes, 0.0f, 30.0f,
                                                fIdleMinutes < 0.5f ? "Never Power Off" :
                                                                            "Power Off After %.1f Minutes"))
      {
        // Minimum positive step value to allow gamepad to move this slider off of "Never Power Off"
        if (fLastIdle == 0.0f && fIdleMinutes > 0.0f)
            fIdleMinutes = 0.5f;

        _registry.skinput.dwIdleTimeoutInSecs =
          fIdleMinutes < 0.5f ? 0 : static_cast <DWORD> (60.0f * fIdleMinutes);

        _registry.regKVControllerIdlePowerOffTimeOut.putData (_registry.skinput.dwIdleTimeoutInSecs);
      }

      SKIF_ImGui_SetHoverTip ("This only applies when SKIF or SKIV are running and no game is actively using Special K.");

      ImGui::TreePop         (  );

      ImGui::Spacing         (  );
      ImGui::Spacing         (  );
      ImGui::SeparatorText   ("Third-Party Setup\tXInput Assignment: ");

      auto&                     _gamepad = SKIF_GamePadInputHelper::GetInstance ();
      std::vector<bool> slots = _gamepad.GetGamePads ( );

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

        if ( ImGui::Checkbox ( "Disable " ICON_FA_STEAM " Input (will restart Steam)", &valvePlugState) )
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
              for (auto& vWatchHandle : vWatchHandles)
                vWatchHandle.push_back (SteamProcessHandle);

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
          ImGui::SameLine    ( ); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "Restarting Steam...");
        }
      }
      else
      {
        ImGui::BeginGroup    ( );
        ImGui::TextColored   (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
        ImGui::SameLine      ( );
        if (ImGui::Button    ("Install " ICON_FA_STEAM " Input Kill Switch"))
        {
          SKIF_Util_OpenURI  (L"https://github.com/SpecialKO/ValvePlug/releases");
        }
        ImGui::EndGroup      ( );
        SKIF_ImGui_SetHoverTip
                             ("Allows Steam Input to be Fully Disabled or Enabled.\r\n\r\n"
                              " * Steam Input usually does stuff whether or not it is enabled per-game.\r\n"
                              " * No registry settings need to be set if you use this config menu.");
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
  SKIF_ImGui_SetHoverTip ("This determines how long the service will remain running when launching a game.\n"
                          "Move the mouse over each option to get more information.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Auto-stop behavior when launching a game:"
  );
  ImGui::TreePush        ("AutoStopBehavior");

  //if (ImGui::RadioButton ("Never",           &_registry.iAutoStopBehavior, 0))
  //  regKVAutoStopBehavior.putData (           _registry.iAutoStopBehavior);
  // 
  //ImGui::SameLine        ( );

  if (ImGui::RadioButton ("Stop on injection",    &_registry.iAutoStopBehavior, 1))
    _registry.regKVAutoStopBehavior.putData (      _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K successfully injects into a game.");

  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Stop on game exit",      &_registry.iAutoStopBehavior, 2))
    _registry.regKVAutoStopBehavior.putData (        _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K detects that the game is being closed.");

  ImGui::TreePop         ( );

  ImGui::Spacing         ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("This setting has no effect if low bandwidth mode is enabled.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Check for updates:"
  );

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PushDisableState ( );

  ImGui::BeginGroup    ( );

  ImGui::TreePush        ("CheckForUpdates");
  if (ImGui::RadioButton ("Never",                 &_registry.iCheckForUpdates, 0))
    _registry.regKVCheckForUpdates.putData (         _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Weekly",                &_registry.iCheckForUpdates, 1))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("On each launch",        &_registry.iCheckForUpdates, 2))
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

  SKIF_ImGui_SetHoverTip ("Check for updates");

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

    SKIF_ImGui_SetHoverTip ("Roll back to the previous version");

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
  SKIF_ImGui_SetHoverTip ("This provides contextual notifications in Windows when the service starts or stops.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Show injection service desktop notifications:"
  );
  ImGui::TreePush        ("Notifications");
  if (ImGui::RadioButton ("Never",          &_registry.iNotifications, 0))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Always",         &_registry.iNotifications, 1))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("When unfocused", &_registry.iNotifications, 2))
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
  if (ImGui::CollapsingHeader ("Library###SKIF_SettingsHeader-1"))
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
  
    if ( ImGui::Checkbox ( "Ignore articles when sorting",                &_registry.bLibraryIgnoreArticles) )
    {
      _registry.regKVLibraryIgnoreArticles.putData (                       _registry.bLibraryIgnoreArticles);
      RepopulateGames = true;
    }

    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Ignore articles like 'THE', 'A', and 'AN' when sorting games.");

    if ( ImGui::Checkbox ( "Remember the last selected game",           &_registry.bRememberLastSelected ) )
      _registry.regKVRememberLastSelected.putData (                      _registry.bRememberLastSelected );

    if ( ImGui::Checkbox ( "Remember category collapsible state",       &_registry.bRememberCategoryState) )
      _registry.regKVRememberCategoryState.putData (                     _registry.bRememberCategoryState);

    ImGui::Spacing         ( );

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Prefer covers from PCGamingWiki for these platforms:"
    );

    ImGui::TreePush      ("PCGWCovers");

    extern bool coverRefresh;
    extern float fAlpha;

    if (ImGui::Checkbox       ("GOG",         &_registry.bPCGWCoversGOG))
    {
      _registry.regKVPCGWCoversGOG.putData    (_registry.bPCGWCoversGOG);
      coverRefresh = true;
      fAlpha       = (_registry.bFadeCovers) ? 0.0f : 1.0f;
    }

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox       ("Steam",       &_registry.bPCGWCoversSteam))
    {
      _registry.regKVPCGWCoversSteam.putData  (_registry.bPCGWCoversSteam);
      coverRefresh = true;
      fAlpha       = (_registry.bFadeCovers) ? 0.0f : 1.0f;
    }

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

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Include games from these platforms:"
    );
    ImGui::TreePush      ("Libraries");

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox        ("Epic",   &_registry.bLibraryEpic))
    {
      _registry.regKVLibraryEpic.putData  (_registry.bLibraryEpic);
      RepopulateGames = true;
    }

    if (ImGui::Checkbox         ("GOG",   &_registry.bLibraryGOG))
    {
      _registry.regKVLibraryGOG.putData   (_registry.bLibraryGOG);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox         ("Steam", &_registry.bLibrarySteam))
    {
      _registry.regKVLibrarySteam.putData (_registry.bLibrarySteam);
      RepopulateGames = true;
    }

    if (ImGui::Checkbox        ("Xbox",   &_registry.bLibraryXbox))
    {
      _registry.regKVLibraryXbox.putData  (_registry.bLibraryXbox);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );
    
    if (ImGui::Checkbox        ("Custom", &_registry.bLibraryCustom))
    {
      _registry.regKVLibraryCustom.putData(_registry.bLibraryCustom);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::TreePop          ( );

    ImGui::Spacing         ( );

    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
    SKIF_ImGui_SetHoverTip ("Warning: This skips the regular platform launch process for the game,\n"
                            "including steps like the cloud saves synchronization that usually occurs.");
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Prefer Instant Play for these platforms:"
    );

    ImGui::TreePush      ("InstantPlay");

    if (ImGui::Checkbox       ("GOG",         &_registry.bInstantPlayGOG))
      _registry.regKVInstantPlayGOG.putData   (_registry.bInstantPlayGOG);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox       ("Steam",       &_registry.bInstantPlaySteam))
      _registry.regKVInstantPlaySteam.putData (_registry.bInstantPlaySteam);
    
    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox       ("Xbox",        &_registry.bInstantPlayXbox))
      _registry.regKVInstantPlayXbox.putData  (_registry.bInstantPlayXbox);

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
  if (ImGui::CollapsingHeader ("Appearance###SKIF_SettingsHeader-2"))
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

    constexpr char* StyleItems[UIStyle_COUNT] =
    { "Dynamic",
      "SKIF Dark",
      "SKIF Light",
      "ImGui Classic",
      "ImGui Dark"
    };
    static const char*
      StyleItemsCurrent;
      StyleItemsCurrent = StyleItems[_registry.iStyle]; // Re-apply the value on every frame as it may have changed
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color theme:"
    );
    ImGui::TreePush      ("ColorThemes");

    if (ImGui::BeginCombo ("###_registry.iStyleCombo", StyleItemsCurrent)) // The second parameter is the label previewed before opening the combo.
    {
      for (int n = 0; n < UIStyle_COUNT; n++)
      {
        bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
        if (ImGui::Selectable (StyleItems[n], is_selected))
          _registry.iStyleTemp = n;         // We apply the new style at the beginning of the next frame to prevent any PushStyleColor/Var from causing issues
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
      }
      ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Useful if you find bright white covers an annoyance.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Dim game covers by 25%%:"
    );
    ImGui::TreePush        ("DimCovers");
    if (ImGui::RadioButton ("Never",                 &_registry.iDimCovers, 0))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                &_registry.iDimCovers, 1))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Based on mouse cursor", &_registry.iDimCovers, 2))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Every time the UI renders a frame, Shelly the Ghost moves a little bit.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Show Shelly the Ghost:"
    );
    ImGui::SameLine        ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION);
    SKIF_ImGui_SetHoverTip ("A critical rendering optimization will not function properly when Shelly is visibly moving.\nIt is therefor recommended to leave Shelly hidden in favor of a reduced GPU usage.");
    ImGui::TreePush        ("Shelly");
    if (ImGui::RadioButton ("Never",                    &_registry.iGhostVisibility, 0))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                   &_registry.iGhostVisibility, 1))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("While service is running", &_registry.iGhostVisibility, 2))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information.");
    ImGui::SameLine        ( );
    ImGui::TextColored     (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "UI elements:"
    );
    ImGui::TreePush        ("UIElements");

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox ("Borders",    &_registry.bUIBorders))
    {
      _registry.regKVUIBorders.putData (_registry.bUIBorders);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    SKIF_ImGui_SetHoverTip ("Use borders around UI elements.");

    if (ImGui::Checkbox ("Tooltips",    &_registry.bUITooltips))
    {
      _registry.regKVUITooltips.putData (_registry.bUITooltips);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = "Info: ";

    SKIF_ImGui_SetHoverText ("This is instead where additional information will be displayed.");
    SKIF_ImGui_SetHoverTip  ("If tooltips are disabled the status bar will be used for additional information.\n"
                             "Note that some links cannot be previewed as a result.");

    if (ImGui::Checkbox ("Status bar",   &_registry.bUIStatusBar))
    {
      _registry.regKVUIStatusBar.putData (_registry.bUIStatusBar);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    SKIF_ImGui_SetHoverTip ("Disabling the status bar as well as tooltips will hide all additional information or tips.");

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( ); // New column
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    bool* pActiveBool = (_registry._TouchDevice) ? &_registry._TouchDevice : &_registry.bUILargeIcons;

    if (_registry._TouchDevice)
    {
      SKIF_ImGui_PushDisableState ( );
    }

    if (ImGui::Checkbox ("Large icons", pActiveBool))
      _registry.regKVUILargeIcons.putData (_registry.bUILargeIcons);

    if (_registry._TouchDevice)
    {
      SKIF_ImGui_PopDisableState  ( );
      SKIF_ImGui_SetHoverTip      ("Currently enforced by touch input mode.");
    } else
      SKIF_ImGui_SetHoverTip      ("Use larger game icons in the library tab.");

    if (ImGui::Checkbox ("Fade covers", &_registry.bFadeCovers))
    {
      _registry.regKVFadeCovers.putData (_registry.bFadeCovers);

      extern float fAlpha;
      fAlpha = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
    }

    SKIF_ImGui_SetHoverTip ("Fade between game covers when switching games.");

    if (SKIF_Util_IsWindows11orGreater ( ))
    {
      if ( ImGui::Checkbox ( "Win11 corners", &_registry.bWin11Corners) )
      {
        _registry.regKVWin11Corners.putData (  _registry.bWin11Corners);
        
        // Force recreating the window on changes
        RecreateWin32Windows = true;
      }

      SKIF_ImGui_SetHoverTip ("Use rounded window corners.");
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( ); // New column
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if ( ImGui::Checkbox ( "Touch input", &_registry.bTouchInput) )
    {
      _registry.regKVTouchInput.putData (  _registry.bTouchInput);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    SKIF_ImGui_SetHoverTip ("Make the UI easier to use on touch input capable devices automatically.");

    if (ImGui::Checkbox ("HiDPI scaling", &_registry.bDPIScaling))
    {
      extern bool
        changedHiDPIScaling;
        changedHiDPIScaling = true;
    }

    SKIF_ImGui_SetHoverTip ("Disabling HiDPI scaling will make the application appear smaller on HiDPI displays.");

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
      ImGui::TextColored (ImColor(0.68F, 0.68F, 0.68F, 1.0f), "Context based information and tips will not appear!");
      ImGui::EndGroup    ( );

      SKIF_ImGui_SetHoverTip ("Restore context based information and tips by enabling tooltips or the status bar.", true);
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
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "UI mode:"
    );

    ImGui::TreePush        ("SKIF_iUIMode");
    // Flip VRR Compatibility Mode (only relevant on Windows 10+)
    if (SKIF_Util_IsWindows10OrGreater ( ))
    {
      if (ImGui::RadioButton ("VRR Compatibility", &_registry.iUIMode, 2))
      {
        _registry.regKVUIMode.putData (             _registry.iUIMode);
        RecreateSwapChains = true;
      }
      SKIF_ImGui_SetHoverTip ("Avoids signal loss and flickering on VRR displays.");
      ImGui::SameLine        ( );
    }
    if (ImGui::RadioButton ("Normal",              &_registry.iUIMode, 1))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip ("Improves UI response on low fixed-refresh rate displays.");
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Safe Mode",           &_registry.iUIMode, 0))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip ("Compatibility mode for users experiencing issues with the other two modes.");
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Increases the color depth of the app.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color depth:"
    );
    
    static int placeholder = 0;
    static int* ptrSDR = nullptr;

    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PushDisableState ( );

      ptrSDR = &_registry.iHDRMode;
    }
    else
      ptrSDR = &_registry.iSDRMode;
    
    ImGui::TreePush        ("iSDRMode");
    if (ImGui::RadioButton   ("8 bpc",        ptrSDR, 0))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    // It seems that Windows 10 1709+ (Build 16299) is required to
    // support 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model
    if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    {
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("10 bpc",       ptrSDR, 1))
      {
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        RecreateSwapChains = true;
      }
    }
    ImGui::SameLine        ( );
    if (ImGui::RadioButton   ("16 bpc",       ptrSDR, 2))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    ImGui::TreePop         ( );
    
    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::Spacing         ( );
    
    if (SKIF_Util_IsHDRSupported ( )  )
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip ("Makes the app pop more on HDR displays.");
      ImGui::SameLine        ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "High dynamic range (HDR):"
      );

      ImGui::TreePush        ("iHDRMode");

      if (_registry.iUIMode == 0)
      {
        ImGui::TextDisabled   ("HDR support is disabled while the UI is in Safe Mode.");
      }

      else if (SKIF_Util_IsHDRActive ( ))
      {
        if (ImGui::RadioButton ("No",             &_registry.iHDRMode, 0))
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
        ImGui::SameLine        ( );
        if (ImGui::RadioButton ("scRGB (16 bpc)", &_registry.iHDRMode, 2))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }

        ImGui::Spacing         ( );

        // HDR Brightness

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::SliderInt("HDR brightness", &_registry.iHDRBrightness, 80, 400, "%d nits"))
        {
          // Reset to 203 nits (default; HDR reference white for BT.2408) if negative or zero
          if (_registry.iHDRBrightness <= 0)
              _registry.iHDRBrightness  = 203;

          // Keep the nits value between 80 and 400
          _registry.iHDRBrightness = std::min (std::max (80, _registry.iHDRBrightness), 400);
          _registry.regKVHDRBrightness.putData (_registry.iHDRBrightness);
        }
    
        if (ImGui::IsItemActive    ( ))
          allowShortcutCtrlA = false;

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PopDisableState  ( );
      }

      else {
        ImGui::TextDisabled   ("Your display(s) supports HDR, but does not use it.");
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
        
        ImGui::BeginGroup       ( );
        ImGui::TextDisabled     ("Use");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
          "Ctrl + " ICON_FA_WINDOWS " + Shift + H");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("to toggle HDR where the");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
            ICON_FA_ARROW_POINTER "");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("is.");
        ImGui::EndGroup         ( );
      }

      ImGui::TreePop         ( );
    }

//#define COLORPICKER
#ifdef COLORPICKER
    if (ImGui::Checkbox     ("sRGBtoLinear colors", &_registry._sRGBColors))
    {
      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
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

  if (ImGui::CollapsingHeader ("Keybindings###SKIF_SettingsHeader-3", ImGuiTreeNodeFlags_DefaultOpen))
  {
    SKIF_ImGui_Spacing      ( );

    struct kb_kv_s
    {
      SK_Keybind*                                     _key;
      SKIF_RegistrySettings::KeyValue <std::wstring>* _reg;
      std::function <void (kb_kv_s*)>                 _callback;
      std::function <bool (void)>                     _show;
    };

    static std::vector <kb_kv_s>
      keybinds = {
      { &_registry.kbToggleHDRDisplay, &_registry.regKVHotkeyToggleHDRDisplay, { [](kb_kv_s* ptr) { SKIF_Util_RegisterHotKeyHDRToggle (ptr->_key); } }, { []() { return (SKIF_Util_IsWindows10v1709OrGreater ( ) && SKIF_Util_IsHDRSupported ( )); } } },
      { &_registry.kbStartService,     &_registry.regKVHotkeyStartService,     { [](kb_kv_s* ptr) { SKIF_Util_RegisterHotKeySVCTemp   (ptr->_key); } }, { []() { return true;                                                                      } } }
    };

    ImGui::BeginGroup ();
    for (auto& keybind : keybinds)
    {
      if (! keybind._show())
        continue;

      ImGui::Text          ( "%s:  ",
                            keybind._key->bind_name );
    }
    ImGui::EndGroup   ();
    ImGui::SameLine   ();
    ImGui::BeginGroup ();
    for (auto& keybind : keybinds)
    {
      if (! keybind._show())
        continue;

      ImGui::PushID (keybind._key->bind_name);
      if (SK_ImGui_Keybinding (keybind._key))
      {
        keybind._reg->putData (keybind._key->human_readable);
        keybind._callback    (&keybind);
      }
      ImGui::PopID ();
    }
    ImGui::EndGroup   ();
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Advanced
  if (ImGui::CollapsingHeader ("Advanced###SKIF_SettingsHeader-4", ImGuiTreeNodeFlags_DefaultOpen))
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

    if ( ImGui::Checkbox ( "Automatically install new updates",              &_registry.bAutoUpdate ) )
      _registry.regKVAutoUpdate.putData (                                     _registry.bAutoUpdate);

    if ( ImGui::Checkbox ( "Open this app on the same monitor as the  " ICON_FA_ARROW_POINTER, &_registry.bOpenAtCursorPosition ) )
      _registry.regKVOpenAtCursorPosition.putData (                           _registry.bOpenAtCursorPosition );

    if ( ImGui::Checkbox ( "Open this app in the mini service mode view", &_registry.bOpenInMiniMode) )
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

    if (ImGui::Checkbox  ("Do not stop the injection service on exit",
                                                      &_registry.bAllowBackgroundService))
      _registry.regKVAllowBackgroundService.putData (  _registry.bAllowBackgroundService);

    SKIF_ImGui_SetHoverTip ("This allows the injection service to remain running even after this app has been closed.");

    if (_registry.bCloseToTray)
    {
      ImGui::BeginGroup   ( );
      SKIF_ImGui_PushDisableState ( );
    }

    if ( ImGui::Checkbox (
            "Allow multiple instances of this app",
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
      SKIF_ImGui_SetHoverTip ("Requires 'Close to the notification area' to be disabled.");
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
    
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        ICON_FA_WRENCH "  Troubleshooting:"
    );

    SKIF_ImGui_Spacing ( );

    const char* LogSeverity[] = { "None",
                                  "Fatal",
                                  "Error",
                                  "Warning",
                                  "Info",
                                  "Debug",
                                  "Verbose" };
    static const char* LogSeverityCurrent = LogSeverity[_registry.iLogging];

    if (ImGui::BeginCombo (" Log level###_registry.iLoggingCombo", LogSeverityCurrent))
    {
      for (int n = 0; n < IM_ARRAYSIZE (LogSeverity); n++)
      {
        bool is_selected = (LogSeverityCurrent == LogSeverity[n]);
        if (ImGui::Selectable (LogSeverity[n], is_selected))
        {
          _registry.iLogging = n;
          _registry.regKVLogging.putData  (_registry.iLogging);
          LogSeverityCurrent = LogSeverity[_registry.iLogging];
          plog::get()->setMaxSeverity((plog::Severity)_registry.iLogging);

          ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                    ? ImGuiDebugLogFlags_EventMask_
                                                    : ImGuiDebugLogFlags_EventViewport);
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    if (_registry.iLogging >= 6 && _registry.bDeveloperMode)
    {
      if (ImGui::Checkbox  ("Enable excessive development logging", &_registry.bLoggingDeveloper))
      {
        _registry.regKVLoggingDeveloper.putData                     (_registry.bLoggingDeveloper);

        ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                  ? ImGuiDebugLogFlags_EventMask_
                                                  : ImGuiDebugLogFlags_EventViewport);
      }
    }

    SKIF_ImGui_SetHoverTip  ("Only intended for SKIF developers as this enables excessive logging (e.g. window messages).");

    SKIF_ImGui_Spacing ( );

    const char* Diagnostics[] = { "None",
                                  "Normal",
                                  "Enhanced" };
    static const char* DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];

    if (ImGui::BeginCombo (" Diagnostics###_registry.iDiagnostics", DiagnosticsCurrent))
    {
      for (int n = 0; n < IM_ARRAYSIZE (Diagnostics); n++)
      {
        bool is_selected = (DiagnosticsCurrent == Diagnostics[n]);
        if (ImGui::Selectable (Diagnostics[n], is_selected))
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
    SKIF_ImGui_SetHoverTip  ("Help improve Special K by allowing anonymized diagnostics to be sent.\n"
                             "The data is used to identify issues, highlight common use cases, and\n"
                             "facilitates the continued development of the application.");

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/Privacy");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/Privacy");

    SKIF_ImGui_Spacing ( );

    if (ImGui::Checkbox  ("Developer mode",  &_registry.bDeveloperMode))
    {
      _registry.regKVDeveloperMode.putData   (_registry.bDeveloperMode);

      ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                ? ImGuiDebugLogFlags_EventMask_
                                                : ImGuiDebugLogFlags_EventViewport);
    }

    SKIF_ImGui_SetHoverTip  ("Exposes additional information and context menu items that may be of interest for developers.");

    ImGui::SameLine    ( );

    if (ImGui::Checkbox  ("Efficiency mode", &_registry.bEfficiencyMode))
      _registry.regKVEfficiencyMode.putData  (_registry.bEfficiencyMode);

    SKIF_ImGui_SetHoverTip  ("Engage efficiency mode for this app when idle.\n"
                             "Not recommended for Windows 10 and earlier.");

    SKIF_ImGui_Spacing ( );

    ImGui::TextWrapped ("Nvidia users: Use the below button to prevent GeForce Experience from mistaking this app for a game.");

    ImGui::Spacing     ( );

    static bool runOnceGFE = false;

    if (runOnceGFE)
      SKIF_ImGui_PushDisableState ( );

    if (ImGui::ButtonEx (ICON_FA_USER_SHIELD " Disable GFE notifications",
                                 ImVec2 (250 * SKIF_ImGui_GlobalDPIScale,
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
    SKIF_ImGui_SetHoverTip  ("This only needs to be used if GeForce Experience notifications\n"
                             "appear on the screen whenever this app is being used.");

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
  if (ImGui::CollapsingHeader ("Whitelist / Blacklist###SKIF_SettingsHeader-5")) //, ImGuiTreeNodeFlags_DefaultOpen)) // Disabled auto-open for this section
  {
    static bool white_edited = false,
                black_edited = false,
                white_stored = true,
                black_stored = true;

    auto _CheckWarnings = [](char* szList)->void
    {
      static int i, count;

      if (strchr (szList, '\"') != nullptr)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "Please remove all double quotes");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), R"( " )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "from the list.");
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "Folders must be separated using two backslashes");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), R"( \\ )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "instead of one");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), R"( \ )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "backslash.");
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "The list can only include");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure),   " 512 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "lines, though multiple can be combined using a pipe");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success),   " | ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "character.");
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          "(e.g. \"NieRAutomataPC|Epic Games\" will match any application\n"
          "installed under a NieRAutomataPC or Epic Games folder.)"
        );
      }
    };

    ImGui::BeginGroup ();
    ImGui::Spacing    ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    ("The following lists manage Special K in processes as patterns are matched against the full path of the injected process.");

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text        ("Easiest is to use the name of the executable or folder of the game.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at\n"
      "C:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"
    );

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text        ("Typing the name of a shared parent folder will match all applications below that folder.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Epic Games\" will match any\n"
      "application installed under the Epic Games folder."
    );

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text ("Note that these lists do not prevent Special K from being injected into processes.");
    ImGui::EndGroup   ();

    if (! _registry.bUITooltips)
    {
      SKIF_ImGui_SetHoverTip (
        "These lists control whether Special K should be enabled (the whitelist) to hook APIs etc,\n"
        "or remain disabled/idle/inert (the blacklist) within the injected process."
      );
    }

    else
    {
      SKIF_ImGui_SetHoverTip (
        "The injection service injects Special K into any process that deals\n"
        "with system input or some sort of window or kb/mouse input activity.\n"
        "\n"
        "These lists control whether Special K should be enabled (the whitelist),\n"
        "or remain idle/inert (the blacklist) within the injected process."
      );
    }

    ImGui::PopStyleColor  ();

    ImGui::NewLine    ();

    // Whitelist section

    ImGui::BeginGroup ();

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_SQUARE_PLUS);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Whitelist Patterns:"
    );

    SKIF_ImGui_Spacing ();

    white_edited |=
      ImGui::InputTextEx ( "###WhitelistPatterns", "SteamApps\nEpic Games\\\\\nGOG Galaxy\\\\Games\nOrigin Games\\\\",
                              _inject.whitelist, MAX_PATH * 512 - 1,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                         150 * SKIF_ImGui_GlobalDPIScale ), // 120 // 150
                                  ImGuiInputTextFlags_Multiline );
    
    if (ImGui::IsItemActive    ( ))
      allowShortcutCtrlA = false;

    if (*_inject.whitelist == '\0')
    {
      SKIF_ImGui_SetHoverTip (
        "These are the patterns used internally to enable Special K for these specific platforms.\n"
        "They are presented here solely as examples of how a potential pattern might look like."
      );
    }

    _CheckWarnings (_inject.whitelist);

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
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Blacklist Patterns:"
    );
    ImGui::SameLine    ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "(Does not prevent injection! Use this to exclude stuff from being whitelisted)");

    SKIF_ImGui_Spacing ();

    black_edited |=
      ImGui::InputTextEx ( "###BlacklistPatterns", "launcher.exe",
                              _inject.blacklist, MAX_PATH * 512 - 1,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                         120 * SKIF_ImGui_GlobalDPIScale ),
                                  ImGuiInputTextFlags_Multiline );

    if (ImGui::IsItemActive    ( ))
      allowShortcutCtrlA = false;

    _CheckWarnings (_inject.blacklist);

    ImGui::Separator ();

    bool bDisabled =
      (white_edited || black_edited) ?
                                false : true;

    bool hotkeyCtrlS = false;

    if (bDisabled)
      SKIF_ImGui_PushDisableState ( );
    else
      hotkeyCtrlS = ImGui::GetIO().KeyCtrl               &&
                    ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f;

    // Hotkey: Ctrl+S
    if (ImGui::Button (ICON_FA_FLOPPY_DISK " Save Changes") || hotkeyCtrlS)
    {
      // Clear the active ID to prevent ImGui from holding outdated copies of the variable
      //   if saving succeeds, to allow SaveUserList to update the variable successfully
      ImGui::ClearActiveID();

      if (white_edited)
      {
        white_stored = _inject.SaveWhitelist ( );

        if (white_stored)
          white_edited = false;
      }

      if (black_edited)
      {
        black_stored = _inject.SaveBlacklist ( );

        if (black_stored)
          black_edited = false;
      }
    }

    ImGui::SameLine ();

    if (ImGui::Button (ICON_FA_ROTATE_LEFT " Reset"))
    {
      if (white_edited)
      {
        _inject.LoadWhitelist ( );

        white_edited = false;
        white_stored = true;
      }

      if (black_edited)
      {
        _inject.LoadBlacklist ( );

        black_edited = false;
        black_stored = true;
      }
    }

    if (bDisabled)
      SKIF_ImGui_PopDisableState  ( );

    ImGui::Spacing();

    if (! white_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The whitelist could not be saved! Please remove any non-Latin characters and try again.");
    }

    if (! black_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The blacklist could not be saved! Please remove any non-Latin characters and try again.");
    }

    ImGui::EndGroup       ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

}



#include <tabs/common_ui.h>

#include "../../version.h"

#include <fonts/fa_621.h>

#include <utility/utility.h>
#include <utility/skif_imgui.h>

#include <utility/sk_utility.h>

#include <utility/injection.h>
#include <utility/registry.h>
#include <utility/updater.h>
#include <utility/gamepad.h>

#include <ShlObj.h>
#include <fonts/fa_621b.h>

#include <tabs/hardware.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

void SKIF_UI_DrawComponentVersion (void)
{
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );
  static SKIF_Updater&          _updater  = SKIF_Updater::GetInstance ( );

  extern PopupState UpdatePromptPopup;

  ImGui::BeginGroup       ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "%s", "Special K 32-bit");

#ifdef _WIN64
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "%s", "Special K 64-bit");
#endif

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "%s", _("Frontend (SKIF)"));

  ImGui::EndGroup         ( );
  ImGui::SameLine         ( );
  ImGui::BeginGroup       ( );

  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "%s", _inject.SKVer32_utf8.c_str());

#ifdef _WIN64
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "%s", _inject.SKVer64_utf8.c_str());
#endif

  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "v");
  ImGui::SameLine         ( );
  ImGui::ItemSize         (ImVec2 (0.0f, ImGui::GetTextLineHeight ()));
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), SKIF_VERSION_STR_A " (" __DATE__ ")");

  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
  if (SKIF_ImGui_Selectable (L_("View release notes...")))
    _updater.Show         (SKIF_Updater::WindowType::ChangesFull);
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  ImGui::EndGroup         ( );

  if ((_updater.GetState ( ) & UpdateFlags_Available) == UpdateFlags_Available)
  {
    SKIF_ImGui_Spacing      ( );

    ImGui::ItemSize         (ImVec2 (65.0f, 0.0f));

    ImGui::SameLine         ( );

    const char* btnLabel = ((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older)
                         ? LI_(ICON_FA_WRENCH " ", "Rollback")
                         : LI_(ICON_FA_WRENCH " ", "Update");

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning));
    if (ImGui::Button (btnLabel, ImVec2(std::max (150.0f * SKIF_ImGui_GlobalDPIScale,
                                                  ImGui::CalcTextSize (btnLabel, nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f),
                                                 30.0f * SKIF_ImGui_GlobalDPIScale )))
      UpdatePromptPopup = PopupState_Open;
    ImGui::PopStyleColor ( );
  }
}

void SKIF_UI_DrawPlatformStatus (void)
{
  static SKIF_GamePadInputHelper& _gamepad  = SKIF_GamePadInputHelper::GetInstance ( );
  static SKIF_RegistrySettings&   _registry = SKIF_RegistrySettings  ::GetInstance ( );

  SKIF_Hardware_RefreshVulkanLayers ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );

  static bool isSKIFAdmin = ::IsUserAnAdmin ( );
  if (isSKIFAdmin)
  {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION " ");
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), _("App is running as an administrator!"));
    SKIF_ImGui_SetHoverTip ( _("Running elevated is not recommended as it will inject Special K into system processes.\n"
                             "Please restart this app and the injection service as a regular user."));
  }
  else {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), _("App is running with normal privileges."));
    SKIF_ImGui_SetHoverTip ( _("This is the recommended option as Special K will not be injected\n"
                             "into system processes nor games running as an administrator."));
  }

  ImGui::EndGroup         ( );

  struct Platform {
    std::string     Name;        // English; for the services, translated where shown (GetName)
    std::wstring    ProcessName;
    DWORD           ProcessID   = 0,
                    PreviousPID = 0;
    bool            isRunning   = false,
                    isAdmin     = false;

    Platform (std::string n, std::wstring pn)
    {
      Name        =  n;
      ProcessName = pn;
    }

    bool isService (void) const { return (ProcessName == L"SKIFsvc32.exe" || ProcessName == L"SKIFsvc64.exe"); }

    std::string GetName (void) const { return (isService ( )) ? T_(Name.c_str ( )) : Name; }
  };

  static DWORD    dwLastRefresh = 0;
  static Platform Platforms[]   = {
    {N_("32-bit service"),  L"SKIFsvc32.exe"},
#ifdef _WIN64
    {N_("64-bit service"),  L"SKIFsvc64.exe"},
#endif
    {"Steam",               L"steam.exe"},
    {"Origin",              L"Origin.exe"},
    {"Galaxy",              L"GalaxyClient.exe"},
    {"EA Desktop",          L"EADesktop.exe"},
    {"Epic Games Launcher", L"EpicGamesLauncher.exe"},
    {"itch.io",             L"itch.exe"},
    {"itch.io (canary)",    L"kitch.exe"},
    {"Ubisoft Connect",     L"upc.exe"},
    {"RTSS",                L"RTSS.exe"}
  };

  // Timer has expired, refresh
  if (dwLastRefresh < SKIF_Util_timeGetTime() && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ) ))
  {
    for (auto& p : Platforms)
    {
      p.ProcessID = 0;
      p.isRunning = false;
    }

    PROCESSENTRY32W pe32 = { };

    SK_AutoHandle hProcessSnap (
      CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
    );

    if ((intptr_t)hProcessSnap.m_h > 0)
    {
      pe32.dwSize = sizeof (PROCESSENTRY32W);

      if (Process32FirstW (hProcessSnap, &pe32))
      {
        do
        {
          for (auto& p : Platforms)
          {
            if (wcsstr (pe32.szExeFile, p.ProcessName.c_str()))
            {
              p.ProcessID = pe32.th32ProcessID;
              p.isRunning = true;

              // If it is a new process, check if it is running as an admin
              if (p.ProcessID != p.PreviousPID)
              {
                p.PreviousPID = p.ProcessID;
                p.isAdmin     = SKIF_Util_IsProcessAdmin (p.ProcessID);
              }

              // Skip checking the remaining platforms for this process
              continue;
            }
          }
        } while (Process32NextW (hProcessSnap, &pe32));
      }
    }

    dwLastRefresh = SKIF_Util_timeGetTime () + 1000; // Set timer for next refresh
  }

  for ( auto& p : Platforms )
  {
    if (p.isRunning)
    {
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );

      if (p.isAdmin)
      {
        ImGui::BeginGroup      ( );
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION " ");
        ImGui::SameLine        ( );
        // TRANSLATORS: {0} is the name of an app (e.g. Steam) or of a service ("32-bit service")
        if (p.ProcessName == L"RTSS.exe")
          SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), F_("{0} is running and may conflict with Special K!", p.GetName ( )).c_str() );
        else
          SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), F_("{0} is running as an administrator!", p.GetName ( )).c_str() );
        ImGui::EndGroup        ( );

        if (isSKIFAdmin)
          SKIF_ImGui_SetHoverTip (F_("It is not recommended to run either {0} or this app as an administrator.\n"
                                     "Please restart both as a normal user.", p.GetName ( )));
        else if (p.ProcessName == L"RTSS.exe")
          SKIF_ImGui_SetHoverTip ( _("RivaTuner Statistics Server (RTSS) occasionally conflicts with Special K.\n"
                                   "Try closing it down if Special K does not behave as expected, or enable\n"
                                   "the option 'Use Microsoft Detours API hooking' in the settings of RTSS.\n"
                                   "\n"
                                   "If you use MSI Afterburner, try closing it as well as otherwise it will\n"
                                   "automatically restart RTSS silently in the background."), true);
        else if (p.isService ( ))
          SKIF_ImGui_SetHoverTip ( _("Running elevated is not recommended as it will inject Special K into system processes.\n"
                                   "Please restart the frontend and the injection service as a regular user."));
        else
          SKIF_ImGui_SetHoverTip (F_("Running elevated will prevent injection into these games.\n"
                                     "Please restart {0} as a normal user.", p.GetName ( )));
      }
      else {
        ImGui::BeginGroup      ( );
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
        ImGui::SameLine        ( );
        SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), F_("{0} is running.", p.GetName ( )).c_str());
        ImGui::EndGroup        ( );
      }
    }
    else if (p.isService ( ))
    {
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::ItemSize         (ImVec2 (ImGui::CalcTextSize (ICON_FA_CHECK " ") .x, ImGui::GetTextLineHeight()));
      //ImGui::TextColored      (ImColor (0.68F, 0.68F, 0.68F), " " ICON_FA_MINUS " ");
      ImGui::SameLine         ( );
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), F_("{0} is stopped.", p.GetName ( )).c_str());
    }

#ifdef _WIN64
    if (p.ProcessName == L"SKIFsvc64.exe")
      ImGui::NewLine           ( );
#else
    if (p.ProcessName == L"SKIFsvc32.exe")
      ImGui::NewLine();
#endif
  }

  if (g_VkLayers.first)
  {
    SKIF_ImGui_Spacing      ( );

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), "%s",
        _("Problematic Vulkan layers:")
    );

    ImGui::SameLine         ( );

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
        ICON_FA_LIGHTBULB
    );

    SKIF_ImGui_SetHoverTip (_("The Vulkan layers listed below have known compatibility issues with Special K and may cause issues.\n"
                            "Click on the layers to toggle them on/off. Note however that this will affects all apps/games."));

    SKIF_ImGui_Spacing      ( );

    for (auto& l : g_VkLayers.second)
    {
      if (! l.isIncompatible)
        continue;

      if (l.Matches.empty())
        continue;

      ImGui::SetCursorPosX (ImGui::GetCursorPosX() + ImGui::GetStyle().FramePadding.x);

      bool dontCare = l.isEnabled;
      ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 ((l.isEnabled) ? ImGuiCol_SKIF_Yellow : ImGuiCol_SKIF_Success));
      ImGui::BeginGroup       ( );
      if (SKIF_ImGui_Checkbox     (l.uiWarnLabel.c_str(), &dontCare))
        l.Toggle ( );

      ImGui::EndGroup         ( );
      ImGui::PopStyleColor    ( );

      SKIF_ImGui_SetHoverText (l.uiHoverTxt);
      SKIF_ImGui_SetHoverTip  ((l.isEnabled) ? _("This layer is enabled and may cause issues. Click to disable it.") : _("This layer is disabled. Click to re-enable it."));
    }
  }

  SKIF_ImGui_Spacing      ( );

  ImGui::Text             ("%s", _("Connected XInput gamepads:"));
  ImGui::SameLine         ( );

  if (! _registry.bControllers)
  {
    ImGui::TextDisabled     ("%s", _("Disabled"));
    SKIF_ImGui_SetHoverTip  (_("Controller support can be re-enabled in the Settings tab."));
  }

  else
  {
    const auto slots = _gamepad.GetGamePads ( );

    ImGui::BeginGroup       ( );
    for (auto slot : slots)
    {
      ImGui::SameLine ( );

      if (slot)
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), ICON_FA_CHECK);
      else
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow ), ICON_FA_XMARK);
    }
    ImGui::EndGroup         ( );
    SKIF_ImGui_SetHoverTip  (_("Indicates which Xbox (XInput) controller slots are currently in use/connected."));

    // If the primary slot is false and one of the other are true, show a warning
    if (slots[0] == false &&
       (slots[1] == true  ||
        slots[2] == true  ||
        slots[3] == true  ))
    {
      ImGui::Spacing          ( );

      ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow));
      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::Text             (ICON_FA_TRIANGLE_EXCLAMATION " ");
      ImGui::SameLine         ( );
      ImGui::TextWrapped      ("%s", _("Some games may not detect the gamepad!"));
      ImGui::EndGroup         ( );
      ImGui::PopStyleColor    ( );

      SKIF_ImGui_SetHoverTip  (_("Some games may not be able to detect the controller if it is not connected to the first slot.\n"
                               "Disconnect and reconnect the controller to the system usually takes care of the issue."));
    }
  }
}

void SKIF_UI_DrawShellyTheGhost (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // Prepare Shelly color and Y position
  const  float fGhostTimeStep = 0.01f;
  static float fGhostTime     = 0.0f;

  // Move 0-4px along the Y axis
  float fGhostYPos = (2.0f + 2.0f * (std::sin(6 * (fGhostTime / 2.5f)) + 0.5f)) * SKIF_ImGui_GlobalDPIScale;

  ImVec4 vGhostColor = ImColor::ImColor (
      0.5f * (std::sin(6 * (fGhostTime / 2.5f)) + 1),
      0.5f * (std::sin(6 * (fGhostTime / 2.5f + 1.0f / 3.0f)) + 1),
      0.5f * (std::sin(6 * (fGhostTime / 2.5f + 2.0f / 3.0f)) + 1)
    );

  if (_registry._StyleLightMode)
    vGhostColor = vGhostColor * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f);

  // Non-static as it needs to be updated constantly due to mixed-DPI monitor configs
  float fMaxPos = ImGui::GetContentRegionAvail ( ).x - ImGui::GetCursorPosX ( ) - ImGui::CalcTextSize (ICON_FA_GHOST).x;

  //if (! _registry.bMiniMode)
  //  fMaxPos -= (50.0f * SKIF_ImGui_GlobalDPIScale);

  const  float fRelStep  = 0.33f;  // Each frame takes (at most) a 0.33% step in either direction
  const  float fMaxSpeed = 1.0f * SKIF_ImGui_GlobalDPIScale; // ... but never more than this many pixels per frame
  static float direction = -1.0f;  // -1 = left, 1 = right
  static float fMinPos   =  0.0f;
  static float fRelPos   = 50.0f;  // Percentage based (0% -> 100%)

  // Change direction if we go below 1% or above 99% of the distance
  if (fRelPos <= 1.0f)
    direction =  1.0f;
  else if (fRelPos >= 99.0f)
    direction = -1.0f;

  // Cap the relative step so that wider areas do not result in a faster Shelly
  float fRange = fMaxPos - fMinPos;
  float fStep  = fRelStep;

  if (fRange > 0.0f)
    fStep = std::min (fRelStep, (fMaxSpeed / fRange) * 100.0f);

  // Take a new relative step in the current direction
  fRelPos += direction * fStep;

  // Convert relative position for an actual position
  float fActPos = fRange * (fRelPos / 100.0f);

  ImGui::SameLine      (0.0f, fActPos);

  ImGui::SetCursorPosY (
    ImGui::GetCursorPosY ( ) + fGhostYPos // (ImGui::GetStyle().FrameBorderSize)
                        );

  ImGui::TextColored (vGhostColor, ICON_FA_GHOST);

  // Increase Shelly timestep for next frame
  fGhostTime += fGhostTimeStep;
}

void SKIF_UI_TipsAndTricks (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  // TRANSLATORS: The text in [brackets] is highlighted (keep the brackets); {0} is a keyboard icon
  SKIF_ImGui_TextRichHighlight (F_("Start typing [{0}] the name of a game in the Library tab to quickly find it.", ICON_FA_KEYBOARD).c_str ( ));
  ImGui::EndGroup         ( );


  //ImGui::Spacing          ( );
  //ImGui::Spacing          ( );


  if (SKIF_Util_IsHDRSupported (NULL) && _registry.kbToggleHDRDisplay.getKeybind()->vKey != 0)
  {
    struct {
      SHORT       vKey;
      std::string bind;

    } static _cache;

    if (_cache.vKey != _registry.kbToggleHDRDisplay.getKeybind()->vKey)
    {
      _cache.vKey    = _registry.kbToggleHDRDisplay.getKeybind()->vKey;
      _cache.bind    = _registry.kbToggleHDRDisplay.getKeybind()->human_readable_utf8;

      SKIF_Util_ReplaceAll (_cache.bind, "Windows", ICON_FA_WINDOWS);
      SKIF_Util_ReplaceAll (_cache.bind, "+", " + "); // Add spaces around the plus sign for better readability (although will misfire on Numpad+ I guess...)
    }

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          (const char *)u8"\u2022 ");
    ImGui::SameLine         ( );
    // TRANSLATORS: The text in [brackets] is highlighted (keep the brackets); {0} is a keyboard shortcut, {1} a mouse pointer icon
    SKIF_ImGui_TextRichHighlight (F_("Use [{0}] to toggle HDR where the [{1}] is.", SKIF_ImGui_TextRichEscape (_cache.bind), ICON_FA_ARROW_POINTER).c_str ( ));
    ImGui::EndGroup         ( );
  }


  if (_registry.kbStartService.getKeybind()->vKey != 0)
  {
    struct {
      SHORT       vKey;
      std::string bind;

    } static _cache;

    if (_cache.vKey != _registry.kbStartService.getKeybind()->vKey)
    {
      _cache.vKey    = _registry.kbStartService.getKeybind()->vKey;
      _cache.bind    = _registry.kbStartService.getKeybind()->human_readable_utf8;

      SKIF_Util_ReplaceAll (_cache.bind, "Windows", ICON_FA_WINDOWS);
      SKIF_Util_ReplaceAll (_cache.bind, "+", " + "); // Add spaces around the plus sign for better readability (although will misfire on Numpad+ I guess...)
    }

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          (const char *)u8"\u2022 ");
    ImGui::SameLine         ( );
    // TRANSLATORS: The text in [brackets] is highlighted (keep the brackets); {0} is a keyboard shortcut
    SKIF_ImGui_TextRichHighlight (F_("Use [{0}] to start the injection service.", SKIF_ImGui_TextRichEscape (_cache.bind)).c_str ( ));
    ImGui::EndGroup         ( );
  }


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  // TRANSLATORS: The text in [brackets] is highlighted (keep the brackets); Ctrl and Shift are keys of the keyboard
  SKIF_ImGui_TextRichHighlight (_("Hold down [Ctrl + Shift] when starting a game for compatibility options "
                                  "or perform a local install of the appropriate wrapper DLL for the game."));
  ImGui::EndGroup         ( );
}

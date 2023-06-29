

#include <tabs/common_ui.h>

#include "../../version.h"

#include <fonts/fa_621.h>

#include <utility/utility.h>
#include <utility/skif_imgui.h>

#include <utility/sk_utility.h>

#include <utility/injection.h>
#include <utility/registry.h>
#include <utility/updater.h>

#include <ShlObj.h>
#include <fonts/fa_621b.h>

void SKIF_UI_DrawComponentVersion (void)
{
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  extern PopupState UpdatePromptPopup;
  extern PopupState HistoryPopup;

  ImGui::BeginGroup       ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "Special K 32-bit");

#ifdef _WIN64
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "Special K 64-bit");
#endif
    
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    "Frontend (SKIF)");

  ImGui::EndGroup         ( );
  ImGui::SameLine         ( );
  ImGui::BeginGroup       ( );
    
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    _inject.SKVer32.c_str());

#ifdef _WIN64
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    _inject.SKVer64.c_str());
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
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "View release notes...");
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    HistoryPopup = PopupState_Open;
  ImGui::EndGroup         ( );
  
  static SKIF_Updater& _updater = 
         SKIF_Updater::GetInstance ( );
  
  if ((_updater.GetState ( ) & UpdateFlags_Available) == UpdateFlags_Available)
  {
    SKIF_ImGui_Spacing      ( );
    
    ImGui::ItemSize         (ImVec2 (65.0f, 0.0f));

    ImGui::SameLine         ( );

    std::string btnLabel = ICON_FA_WRENCH "  Update";

    if ((_updater.GetState() & UpdateFlags_Rollback) == UpdateFlags_Rollback)
      btnLabel = ICON_FA_WRENCH "  Rollback";

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning));
    if (ImGui::Button (btnLabel.c_str(), ImVec2(150.0f * SKIF_ImGui_GlobalDPIScale,
                                                 30.0f * SKIF_ImGui_GlobalDPIScale )))
      UpdatePromptPopup = PopupState_Open;
    ImGui::PopStyleColor ( );
  }
}

void SKIF_UI_DrawPlatformStatus (void)
{
  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );

  static bool isSKIFAdmin = IsUserAnAdmin();
  if (isSKIFAdmin)
  {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION " ");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), "App is running as an administrator!");
    SKIF_ImGui_SetHoverTip ( "Running elevated is not recommended as it will inject Special K into system processes.\n"
                             "Please restart this app and the global injector service as a regular user.");
  }
  else {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "App is running with normal privileges.");
    SKIF_ImGui_SetHoverTip ( "This is the recommended option as Special K will not be injected\n"
                             "into system processes nor games running as an administrator.");
  }

  ImGui::EndGroup         ( );

  struct Platform {
    std::string     Name;
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
  };

  static DWORD dwLastRefresh = 0;
  static Platform Platforms[] = {
    {"32-bit service",  L"SKIFsvc32.exe"},
#ifdef _WIN64
    {"64-bit service",  L"SKIFsvc64.exe"},
#endif
    {"Steam",               L"steam.exe"},
    {"Origin",              L"Origin.exe"},
    {"Galaxy",              L"GalaxyClient.exe"},
    {"EA Desktop",          L"EADesktop.exe"},
    {"Epic Games Launcher", L"EpicGamesLauncher.exe"},
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
      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );

      if (p.isAdmin)
      {
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION " ");
        ImGui::SameLine        ( );
        if (p.ProcessName == L"RTSS.exe")
          ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), (p.Name + " is running and might conflict with Special K!").c_str() );
        else
          ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), (p.Name + " is running as an administrator!").c_str() );

        if (isSKIFAdmin)
          SKIF_ImGui_SetHoverTip (("It is not recommended to run either " + p.Name + " or this app as an administrator.\n"
                                   "Please restart both as a normal user.").c_str());
        else if (p.ProcessName == L"RTSS.exe")
          SKIF_ImGui_SetHoverTip ( "RivaTuner Statistics Server is known to occasionally conflict with Special K.\n"
                                   "Please stop it if Special K does not function as expected. You might have\n"
                                   "to stop MSI Afterburner as well if you use that application.");
        else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
          SKIF_ImGui_SetHoverTip ( "Running elevated is not recommended as it will inject Special K into system processes.\n"
                                   "Please restart the frontend and the global injector service as a regular user.");
        else
          SKIF_ImGui_SetHoverTip (("Running elevated will prevent injection into these games.\n"
                                   "Please restart " + p.Name + " as a normal user.").c_str());
      }
      else {
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), (p.Name + " is running.").c_str());
      }

      ImGui::EndGroup          ( );
    }
    else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
    {
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::ItemSize         (ImVec2 (ImGui::CalcTextSize (ICON_FA_CHECK " ") .x, ImGui::GetTextLineHeight()));
      //ImGui::TextColored      (ImColor (0.68F, 0.68F, 0.68F), " " ICON_FA_MINUS " ");
      ImGui::SameLine         ( );
      ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (p.Name + " is stopped.").c_str());
    }

#ifdef _WIN64
    if (p.ProcessName == L"SKIFsvc64.exe")
      ImGui::NewLine           ( );
#else
    if (p.ProcessName == L"SKIFsvc32.exe")
      ImGui::NewLine();
#endif
  }
}

void SKIF_UI_DrawShellyTheGhost (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  if (                        _registry.iGhostVisibility == 1 ||
    (_inject.bCurrentState && _registry.iGhostVisibility == 2) )
  {
    // Required for subsequent GetCursorPosX() calls to get the right pos, as otherwise it resets to 0.0f
    ImGui::SameLine ( );

    // Prepare Shelly color and Y position
    const  float fGhostTimeStep = 0.01f;
    static float fGhostTime     = 0.0f;

    float fGhostYPos = 4.0f * (std::sin(6 * (fGhostTime / 2.5f)) + 0.5f);

    ImVec4 vGhostColor = ImColor::ImColor (
        0.5f * (std::sin(6 * (fGhostTime / 2.5f)) + 1),
        0.5f * (std::sin(6 * (fGhostTime / 2.5f + 1.0f / 3.0f)) + 1),
        0.5f * (std::sin(6 * (fGhostTime / 2.5f + 2.0f / 3.0f)) + 1)
      );

    if (_registry.iStyle == 2)
      vGhostColor = vGhostColor * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    // Non-static as it needs to be updated constantly due to mixed-DPI monitor configs
    float fMaxPos = ImGui::GetContentRegionMax ( ).x - ImGui::GetCursorPosX ( ) - 115.0f * SKIF_ImGui_GlobalDPIScale;

    static float direction = -0.33f; // Each frame takes a 0.33% step in either direction
    static float fMinPos   =  0.0f;
    static float fRelPos   = 50.0f;  // Percentage based (0% -> 100%)

    // Change direction if we go below 1% or above 99% of the distance
    if (fRelPos <= 1.0f || fRelPos >= 99.0f)
      direction = -direction;

    // Take a new relative step in the new direction
    fRelPos += direction;

    // Convert relative position for an actual position
    float fActPos = (fMaxPos - fMinPos) * (fRelPos / 100.0f);

    ImGui::SameLine    (0.0f, fActPos);
  
    ImGui::SetCursorPosY (
      ImGui::GetCursorPosY ( ) + fGhostYPos
                          );

    ImGui::TextColored (vGhostColor, ICON_FA_GHOST);
    
    // Increase Shelly timestep for next frame
    fGhostTime += fGhostTimeStep;
  }
}

void SKIF_UI_TipsAndTricks (void)
{
  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::Text             ("Start typing the name of a game in the library tab to quicky find it.");
  ImGui::EndGroup         ( );


  ImGui::Spacing          ( );
  ImGui::Spacing          ( );


  if (SKIF_Util_IsHDRSupported())
  {
    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          (const char *)u8"\u2022 ");
    ImGui::SameLine         ( );
    ImGui::Text             ("Use");
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      ICON_FA_WINDOWS " + Ctrl + Shift + H");
    ImGui::SameLine         ( );
    ImGui::Text             ("to toggle HDR where the");
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        ICON_FA_ARROW_POINTER);
    ImGui::SameLine         ( );
    ImGui::Text             ("is.");
    ImGui::EndGroup         ( );


    ImGui::Spacing          ( );
    ImGui::Spacing          ( );
  }


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  float fX1 = ImGui::GetCursorPosX();
  ImGui::Text             ("Hold down");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    ICON_FA_KEYBOARD " Ctrl + Shift");
  ImGui::SameLine         ( );
  ImGui::Text             ("when starting a game to access compatibility options");
  ImGui::SetCursorPosX    (fX1);
  ImGui::Text             ("or quickly perform a local install of the appropriate wrapper DLL for the game.");
  ImGui::EndGroup         ( );
}

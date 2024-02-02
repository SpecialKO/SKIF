

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
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    _inject.SKVer32_utf8.c_str());

#ifdef _WIN64
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),    _inject.SKVer64_utf8.c_str());
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
  if (SKIF_ImGui_Selectable ("View release notes..."))
    HistoryPopup = PopupState_Open;
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  ImGui::EndGroup         ( );
  
  static SKIF_Updater& _updater = 
         SKIF_Updater::GetInstance ( );
  
  if ((_updater.GetState ( ) & UpdateFlags_Available) == UpdateFlags_Available)
  {
    SKIF_ImGui_Spacing      ( );
    
    ImGui::ItemSize         (ImVec2 (65.0f, 0.0f));

    ImGui::SameLine         ( );

    std::string btnLabel = ICON_FA_WRENCH "  Update";

    if ((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older)
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

  static bool isSKIFAdmin = ::IsUserAnAdmin ( );
  if (isSKIFAdmin)
  {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION " ");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), "App is running as an administrator!");
    SKIF_ImGui_SetHoverTip ( "Running elevated is not recommended as it will inject Special K into system processes.\n"
                             "Please restart this app and the injection service as a regular user.");
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

  static DWORD    dwLastRefresh = 0;
  static Platform Platforms[]   = {
    {"32-bit service",      L"SKIFsvc32.exe"},
#ifdef _WIN64
    {"64-bit service",      L"SKIFsvc64.exe"},
#endif
    {"Steam",               L"steam.exe"},
    {"Origin",              L"Origin.exe"},
    {"Galaxy",              L"GalaxyClient.exe"},
    {"EA Desktop",          L"EADesktop.exe"},
    {"Epic Games Launcher", L"EpicGamesLauncher.exe"},
    {"Ubisoft Connect",     L"upc.exe"},
    {"RTSS",                L"RTSS.exe"}
  };

  struct VulkanLayer {
    struct reg {
      std::wstring    Key      = L"";
      std::wstring    Value    = L"";
      DWORD           Data     =   0; // 0 = enabled; >0 = disabled
    };
    
    std:: string      Name;
    std::wstring      Pattern   = L"";
    std::vector <reg> Matches   = { };
    bool              isEnabled = false; // Simplified state

    std:: string      uiLabel;
    std:: string      uiHoverTxt; // Status bar text
    std::wstring      regCmd;     // Combined command to toggle

    VulkanLayer (std::string n, std::wstring pn)
    {
      Name    = n;
      Pattern = pn;
    }
  };

  // Unwinder on the topic of Vulkan layer compatibility issues:
  // Ironically, true reason of 99% of compatibility issues with third-party implicit layers are on LunarG.
  // Their sample Vulkan layer code, which virtually everyone used as a template to create own layers,
  //   had a bug with Vulkan instance handle leak. So it was and it is echoed in any layer based on that
  //     source code. I nailed it down myself in my layer when debugging compatibility issues with DXVK.
  // 
  // Version 7.3.0 (published on 28.02.2021)
  // - Fixed Vulkan device and instance handle leak in Vulkan bootstrap layer

  static VulkanLayer Layers[]   = {
  //{ "RTSS",         LR"(RTSSVkLayer)"               }, // Disabled since we seems to have no confirmed compatibility issues for now. // Aemony, 2024-02-02
    { "ReShade",      LR"(ReShade)"                   }, //
    { "OBS Studio",   LR"(obs-vulkan)"                }, //
    { "Action!",      LR"(MirillisActionVulkanLayer)" }  // Causes Borderlands 2 with DXVK to fail to launch. // Aemony, 2024-02-02
  };

  // Timer has expired, refresh
  if (dwLastRefresh < SKIF_Util_timeGetTime() && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ) ))
  {
    for (auto& p : Platforms)
    {
      p.ProcessID = 0;
      p.isRunning = false;
    }

    for (auto& l : Layers)
    {
      l.Matches.clear();
      l.isEnabled  = false;
      l.uiLabel    =  "";
      l.uiHoverTxt =  "";
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

    static const HKEY regHives[] = {
      HKEY_LOCAL_MACHINE,
      HKEY_CURRENT_USER,
    };

    // Do not bother checking explicit layers
    static const std::wstring regKeys[] = {
      LR"(SOFTWARE\Khronos\Vulkan\ImplicitLayers)"
    //LR"(SOFTWARE\Khronos\Vulkan\ExplicitLayers)"
#ifdef _WIN64
     ,LR"(SOFTWARE\WOW6432Node\Khronos\Vulkan\ImplicitLayers)"
    //LR"(SOFTWARE\WOW6432Node\Khronos\Vulkan\ExplicitLayers)"
#endif
    };

    // Identify conflicting Vulkan layers through the registry
    for (auto& hHive : regHives)
    {
      for (auto& wzKey : regKeys)
      {
        HKEY hKey;

        if (RegOpenKeyExW (hHive, wzKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) // Worth using KEY_WOW64_64KEY ?
        {
          DWORD dwIndex           = 0, // A variable that receives the number of values that are associated with the key.
                dwResult          = 0,
                dwMaxValueNameLen = 0, // A pointer to a variable that receives the size of the key's longest value name, in Unicode characters. The size does not include the terminating null character.
                dwMaxValueLen     = 0; // A pointer to a variable that receives the size of the longest data component among the key's values, in bytes.

          if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, NULL, NULL, NULL, &dwIndex, &dwMaxValueNameLen, &dwMaxValueLen, NULL, NULL) == ERROR_SUCCESS)
          {
            while (dwIndex > 0)
            {
              dwIndex--;
          
              DWORD dwValueNameLen =
                    (dwMaxValueNameLen + 2);

              std::unique_ptr <wchar_t []> pValue =
                std::make_unique <wchar_t []> (sizeof (wchar_t) * dwValueNameLen);
          
              DWORD dwValueLen =
                    (dwMaxValueLen);

              BYTE bArray[sizeof(DWORD)];
              memcpy(bArray, &dwValueLen, sizeof(DWORD));

              std::unique_ptr <BYTE> pData =
                std::make_unique <BYTE> (bArray[0]);

              DWORD dwType = REG_NONE;

              dwResult = RegEnumValueW (hKey, dwIndex, (wchar_t *) pValue.get(), &dwValueNameLen, NULL, &dwType, pData.get(), &dwValueLen);

              if (dwResult == ERROR_NO_MORE_ITEMS)
                break;

              if (dwResult == ERROR_SUCCESS && dwType == REG_DWORD)
              {
                for (auto& l : Layers)
                {
                  if (StrStrIW (pValue.get(), l.Pattern.c_str()) != NULL)
                  {
                    VulkanLayer::reg item;

                    item.Key       = ((hHive == HKEY_LOCAL_MACHINE) ? LR"(HKLM\)" : LR"(HKCU\)") + wzKey;
                    item.Value     = pValue.get();
                    item.Data      = (pData.get()[0]) | (pData.get()[1] << 8) | (pData.get()[2] << 16) | (pData.get()[3] << 24);

                    if (item.Data == 0)
                      l.isEnabled = true;

                    l.Matches.push_back (item);
                  }
                }
              }
            }
          }

          RegCloseKey (hKey);
        }
      }
    }

    // Prep the UI / command components
    for (auto& l : Layers)
    {
      if (l.Matches.empty())
        continue;

      l.regCmd     = LR"(/c )";
      l.uiLabel    = (l.isEnabled) ? (l.Name + " may conflict with Special K!") : (l.Name + " has been disabled.");
      l.uiHoverTxt = "";

      for (auto& item : l.Matches)
      {
        if (!item.Value.empty())
        {
          l.regCmd += SK_FormatStringW (LR"(%ws add "%ws" /v "%ws" /t REG_DWORD /d %i /f)",
              ((l.regCmd.length() <= 3) ? L"reg" : L"& reg"),
                item.Key.c_str(),
                item.Value.c_str(),
              ((l.isEnabled) ? 1 : 0)
          );

          if (l.uiHoverTxt.empty())
            l.uiHoverTxt  =         SK_WideCharToUTF8 (item.Key);
          else
            l.uiHoverTxt += " / " + SK_WideCharToUTF8 (item.Key);
        }
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
        if (p.ProcessName == L"RTSS.exe")
          ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), (p.Name + " is running and may conflict with Special K!").c_str() );
        else
          ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), (p.Name + " is running as an administrator!").c_str() );
        ImGui::EndGroup        ( );

        if (isSKIFAdmin)
          SKIF_ImGui_SetHoverTip (("It is not recommended to run either " + p.Name + " or this app as an administrator.\n"
                                   "Please restart both as a normal user.").c_str());
        else if (p.ProcessName == L"RTSS.exe")
          SKIF_ImGui_SetHoverTip ( "RivaTuner Statistics Server (RTSS) occasionally conflicts with Special K.\n"
                                   "Try closing it down if Special K does not behave as expected, or enable\n"
                                   "the option 'Use Microsoft Detours API hooking' in the settings of RTSS.\n"
                                   "\n"
                                   "If you use MSI Afterburner, try closing it as well as otherwise it will\n"
                                   "automatically restart RTSS silently in the background.", true);
        else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
          SKIF_ImGui_SetHoverTip ( "Running elevated is not recommended as it will inject Special K into system processes.\n"
                                   "Please restart the frontend and the injection service as a regular user.");
        else
          SKIF_ImGui_SetHoverTip (("Running elevated will prevent injection into these games.\n"
                                   "Please restart " + p.Name + " as a normal user.").c_str());
      }
      else {
        ImGui::BeginGroup      ( );
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), (p.Name + " is running.").c_str());
        ImGui::EndGroup        ( );
      }
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

  bool header = false;
  for (auto& l : Layers)
  {
    if (l.Matches.empty())
      continue;

    if (! header)
    {
      header = true;

      SKIF_ImGui_Spacing      ( );

      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "Vulkan Layers:"
      );

      SKIF_ImGui_Spacing      ( );
    }

    ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 ((l.isEnabled) ? ImGuiCol_SKIF_Yellow : ImGuiCol_SKIF_Success));

    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::BeginGroup       ( );
    ImGui::Text             ((l.isEnabled) ? ICON_FA_TRIANGLE_EXCLAMATION " " : ICON_FA_CHECK " ");
    ImGui::SameLine         ( );
    if (ImGui::Selectable   (l.uiLabel.c_str()))
      ShellExecuteW (nullptr, L"runas", L"cmd", l.regCmd.c_str(), nullptr, SW_SHOWNORMAL);
    ImGui::EndGroup         ( );
    ImGui::PopStyleColor    ( );

    SKIF_ImGui_SetHoverText (l.uiHoverTxt);
    SKIF_ImGui_SetHoverTip  ("Click to toggle this Vulkan layer.");
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

    float fGhostYPos = (4.0f * (std::sin(6 * (fGhostTime / 2.5f)) + 0.5f)) * SKIF_ImGui_GlobalDPIScale;

    ImVec4 vGhostColor = ImColor::ImColor (
        0.5f * (std::sin(6 * (fGhostTime / 2.5f)) + 1),
        0.5f * (std::sin(6 * (fGhostTime / 2.5f + 1.0f / 3.0f)) + 1),
        0.5f * (std::sin(6 * (fGhostTime / 2.5f + 2.0f / 3.0f)) + 1)
      );

    if (_registry.iStyle == 2)
      vGhostColor = vGhostColor * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f);

    // Non-static as it needs to be updated constantly due to mixed-DPI monitor configs
    float fMaxPos = ImGui::GetContentRegionMax ( ).x - ImGui::GetCursorPosX ( ) - (117.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetStyle().FrameBorderSize * 2);

    if (! _registry.bServiceMode)
      fMaxPos -= (50.0f * SKIF_ImGui_GlobalDPIScale);

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
      ImGui::GetCursorPosY ( ) - (ImGui::GetStyle().FrameBorderSize) + fGhostYPos
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
  ImGui::Text             ("Start typing");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    ICON_FA_KEYBOARD);
  ImGui::SameLine         ( );
  ImGui::Text             ("the name of a game in the library tab to quicky find it.");
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
      "Ctrl + " ICON_FA_WINDOWS " + Shift + H");
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
  ImGui::Text             ("Use");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    ICON_FA_WINDOWS " + Shift + Insert");
  ImGui::SameLine         ( );
  ImGui::Text             ("to start the injection service.");
  ImGui::EndGroup         ( );


  ImGui::Spacing          ( );
  ImGui::Spacing          ( );


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
    "Ctrl + Shift");
  ImGui::SameLine         ( );
  ImGui::Text             ("when starting a game to access compatibility options");
  ImGui::SetCursorPosX    (fX1);
  ImGui::Text             ("or quickly perform a local install of the appropriate wrapper DLL for the game.");
  ImGui::EndGroup         ( );
}

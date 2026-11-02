//
// Copyright 2020 - 2026  Andon "Kaldaien" Coleman and Aemony
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include "library_ui.h"

#include <ctime>
#include <tabs/properties.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

#pragma region PrintInjectionSummary

// Details panel, top frame: Special K DLL / profile folder / config file / platform, and the service toggle (moved verbatim from the former GetInjectionSummary)
static void
DrawGameDetails_Info (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

#pragma region _IsLocalDLLFileOutdated

  auto _IsLocalDLLFileOutdated = [&](void) -> bool
  {
    bool ret = false;

    // Exclude the check for games with known older versions
    if (! pApp->HasLegacySpecialKVersion ( ))
    {
      if (SKIF_Util_CompareVersionStrings (_inject.SKVer32, pApp->specialk.injection.dll.version) > 0)
      {
        ret = true;
      }
    }

    return ret;
  };

#pragma endregion

#pragma region _UpdateLocalDLLFile

  auto _UpdateLocalDLLFile = [&](void) -> void
  {
    int iBinaryType = SKIF_Util_GetBinaryType (pApp->specialk.injection.dll.full_path.c_str());
    if (iBinaryType > PEBitness_Invalid)
    {
      std::wstring  wsPathToGlobalDLL = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, (iBinaryType == PEBitness_64bit) ? L"SpecialK64.dll" : L"SpecialK32.dll");
      if (CopyFile (wsPathToGlobalDLL.c_str(), pApp->specialk.injection.dll.full_path.c_str(), FALSE))
      {
        PLOG_INFO << "Successfully updated " << pApp->specialk.injection.dll.full_path << " from v " << pApp->specialk.injection.dll.version << " to v " << _inject.SKVer32;
      }

      else {
        PLOG_ERROR << "Failed to copy " << wsPathToGlobalDLL << " to " << pApp->specialk.injection.dll.full_path;
        PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
      }
    }

    else {
      PLOG_ERROR << "Failed to retrieve binary type from " << pApp->specialk.injection.dll.full_path << " -- returned: " << iBinaryType;
      PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    }
  };

#pragma endregion

  static constexpr float
        num_lines = 4.0f;
  auto line_ht   =
    ImGui::GetTextLineHeightWithSpacing ();

  auto frame_id =
    ImGui::GetID ("###Injection_Summary_Frame");

  SKIF_ImGui_BeginChildFrame ( frame_id,
                                  ImVec2 ( ImFloor (lib_column2_width - ImGui::GetStyle ().FrameBorderSize * 2.0f),
                                                                            num_lines * line_ht ),
                                    ImGuiChildFlags_AlwaysAutoResize  |
                                    ImGuiChildFlags_AutoResizeY |
                                    ImGuiChildFlags_NavFlattened,
                                    ImGuiWindowFlags_NoScrollbar       |
                                    ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoBackground
                              );

  ImGui::BeginGroup       ();

  // Column 1
  ImGui::BeginGroup       ();
  //ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  //ImGui::NewLine          ();
  //ImGui::TextUnformatted  ("Injection:");
  ImGui::TextUnformatted  ("Special K");
  ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  ImGui::TextUnformatted  (_("Profile folder:"));
  ImGui::TextUnformatted  (_("Config file:"));
  ImGui::TextUnformatted  (_("Platform:"));
  ImGui::PopStyleColor    ();
  ImGui::ItemSize         (ImVec2 (105.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 1 should have min-width 105px (scaled with the DPI)
  ImGui::EndGroup         ();

  ImGui::SameLine         ();

  // Column 3 has content on its first row only: the service quick toggle, or the update button of an outdated local DLL.
  //   Where there is not room enough for it after column 2 (longer translations), it moves left over the rows below of column 2,
  //     and the first row of column 2 (the version) is shortened with an ellipsis to make room for it, down to a minimum width.
  //       Only if that is still not enough is the toggle shortened itself. Its full text is then shown in its tooltip.
  const ImVec2 posColumn2          = ImGui::GetCursorPos       ( );
  const float  fColumnsRight       = posColumn2.x + ImGui::GetContentRegionAvail ( ).x;
  const float  fSpacing            = ImGui::GetStyle ( ).ItemSpacing.x;
  float        fColumn2Row1        = 0.0f; // The width of the first row of column 2, as drawn

  // Column 3: what it shows, and how wide that is
  //   Also show the toggle while a game is loading since it is more likely to be using global than local! (and it's safe to show nowadays!)
  const bool  bQuickToggle     = (/* ! pApp->loading && */ pApp->specialk.injection.injection.type != InjectionType::Local && ! _inject.isPending());
  const bool  bUpdateLocal     = (! pApp->loading && pApp->specialk.injection.injection.type == InjectionType::Local && _IsLocalDLLFileOutdated ( ));
  const char* szQuickToggle    = TL_(_inject.ui_game_summary.text.c_str());
  const char* szQuickToggleId  = strstr (szQuickToggle, "###"); // The end of the text
  const char* szQuickToggleEnd = (szQuickToggleId != nullptr) ? szQuickToggleId : szQuickToggle + strlen (szQuickToggle);

  while (szQuickToggleEnd > szQuickToggle && szQuickToggleEnd [-1] == ' ') // The blank label (stopped) has no width
    szQuickToggleEnd--;

  const float fColumn3Width    = (bQuickToggle) ? ImGui::CalcTextSize (szQuickToggle, szQuickToggleEnd).x
                               : (bUpdateLocal) ? ImGui::CalcTextSize (ICON_FA_ARROW_UP).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f
                                                : 0.0f;

  // The widest the first row of column 2 may be with column 3 after it, though never narrower than about a version number
  const float fColumn2Row1Max  = (fColumn3Width > 0.0f) ? std::max (ImGui::GetFontSize ( ) * 5.0f, fColumnsRight - posColumn2.x - fSpacing - fColumn3Width)
                                                        : FLT_MAX;

  // Column 2
  ImGui::BeginGroup       ();

  // Injection
  if (! pApp->loading && ! pApp->specialk.injection.dll.shorthand.empty ())
  {
    //ImGui::TextUnformatted  (cache.dll.shorthand.c_str  ());
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowOverlap;

    if (pApp->specialk.injection.injection.type == InjectionType::Global)
      flags |= ImGuiSelectableFlags_Disabled;

    bool openLocalMenu = false;

    // Shortened with an ellipsis if column 3 needs the room (the full path is shown in the status bar on hover)
    const float  fVersionWidth = ImGui::CalcTextSize (pApp->ui.label_version.c_str(), nullptr, true).x;
    const bool   bShortened    = (fVersionWidth > fColumn2Row1Max);
    const ImVec2 posVersion    = ImGui::GetCursorScreenPos ( );

    fColumn2Row1 = std::min (fVersionWidth, fColumn2Row1Max);

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    if (ImGui::Selectable ((bShortened) ? "###LabelVersion" : pApp->ui.label_version.c_str(), false, flags, ImVec2 ((bShortened) ? fColumn2Row1 : 0.0f, 0.0f)))
      openLocalMenu = true;

    if (bShortened)
    {
      // As the selectable draws its text: disabled for global injection
      ImGui::PushStyleColor     (ImGuiCol_Text, ImGui::GetStyleColorVec4 ((flags & ImGuiSelectableFlags_Disabled) ? ImGuiCol_TextDisabled : ImGuiCol_Text));
      ImGui::RenderTextEllipsis (ImGui::GetWindowDrawList ( ), posVersion, ImVec2 (posVersion.x + fColumn2Row1, posVersion.y + ImGui::GetTextLineHeight ( )),
                                 posVersion.x + fColumn2Row1, pApp->ui.label_version.c_str(), nullptr, nullptr);
      ImGui::PopStyleColor      ( );
    }
    ImGui::PopStyleColor();

    if (pApp->specialk.injection.injection.type == InjectionType::Local)
    {
      SKIF_ImGui_SetMouseCursorHand ( );

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openLocalMenu = true;
    }

    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.dll.full_path_utf8.c_str());

    if (openLocalMenu && ! ImGui::IsPopupOpen ("LocalDLLMenu"))
      ImGui::OpenPopup    ("LocalDLLMenu");

    if (ImGui::BeginPopup ("LocalDLLMenu", ImGuiWindowFlags_NoMove))
    {
      if (_IsLocalDLLFileOutdated( ))
      {
        if (ImGui::Selectable ((F_("Update to v {0}", _inject.SKVer32_utf8) + "###UpdateLocalDLL").c_str( )))
          _UpdateLocalDLLFile ( );

        ImGui::Separator ( );
      }

      if (ImGui::Selectable (L_("Uninstall")))
      {
        if (DeleteFile (pApp->specialk.injection.dll.full_path.c_str()))
        {
          PLOG_INFO << "Successfully uninstalled local DLL v " << pApp->specialk.injection.dll.version << " from " << pApp->specialk.injection.dll.full_path;

          HKEY     hKey;
          LSTATUS ls = RegOpenKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Local)", &hKey);

          if (ERROR_SUCCESS == ls)
          {
            ls = RegDeleteValueW (hKey, pApp->specialk.injection.dll.full_path.c_str());
            PLOG_ERROR_IF (ls != ERROR_SUCCESS) << "Failed to delete registry value: " << SKIF_Util_GetErrorAsWStr (ls);
            RegCloseKey (hKey);
          }
        }
      }

      ImGui::EndPopup ( );
    }
  }

  else
  {
    // Most will use global injection, so default to this in situations where the data is actually being loaded.
    if (! pApp->specialk.injection.dll.version_utf8.empty())
    {
      ImGui::Text    ("v %s", pApp->specialk.injection.dll.version_utf8.c_str());
      fColumn2Row1 = ImGui::GetItemRectSize ( ).x;
    }
    else
      ImGui::NewLine ( );
  }

  // Config Root
  // Config File
  if (! pApp->loading && ! pApp->specialk.injection.config.shorthand.empty ())
  {
    // Config Root
    const char* szConfigType = T_(pApp->specialk.injection.config.type_utf8.c_str ( ));

    if (ImGui::Selectable         (TL_(pApp->specialk.injection.config.type_utf8.c_str ( )), false, ImGuiSelectableFlags_None, ImVec2 (std::max (95.f * SKIF_ImGui_GlobalDPIScale, ImGui::CalcTextSize (szConfigType).x), 0.f)))
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      SKIF_Util_ExplorePath       (pApp->specialk.injection.config.root_dir);
    }
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.config.root_dir_utf8.c_str ());

    // Config File
    if (ImGui::Selectable         (pApp->specialk.injection.config.shorthand_utf8.c_str (), false, ImGuiSelectableFlags_None, ImVec2 (95.f * SKIF_ImGui_GlobalDPIScale, 0.f))) // 240
    {
      // If the file does not exist, create it
      if (! PathFileExists (pApp->specialk.injection.config.full_path.c_str()))
      {
        std::error_code ec;
        // Create any missing directories
        if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
              std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

        std::wofstream config_file (pApp->specialk.injection.config.full_path.c_str(), std::wofstream::out | std::wofstream::trunc);

        if (config_file.is_open())
        {
          config_file << utf8_bom;
          config_file.close ( );
        }
      }

      // Internal editor
      SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, pApp->specialk.injection.config.full_path, pApp->names.normal);

      // External editor
      //SKIF_Util_OpenURI (pApp->specialk.injection.config.full_path.c_str(), SW_SHOWNORMAL, NULL);
    }

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.config.full_path_utf8.c_str ());

    DrawGameConfigMenu (pApp);
  }

  else if (! pApp->loading)
  {
    ImGui::TextUnformatted (T_(pApp->specialk.injection.config.type_utf8.c_str ()));
    // TRANSLATORS: Not available
    ImGui::TextUnformatted (_("N/A"));
  }

  else {
    // Most will use global injection, so default to this in situations where the data is actually being loaded.
    ImGui::TextUnformatted (T_(pApp->specialk.injection.config.type_utf8.c_str ()));
    ImGui::TextUnformatted (pApp->specialk.injection.config.shorthand_utf8.c_str ());
    //ImGui::TextUnformatted ("Centralized");
    //ImGui::TextUnformatted ("SpecialK.ini");
    //ImGui::NewLine ( );
    //ImGui::NewLine ( );
  }

  // Platform
  ImGui::TextUnformatted  (pApp->store_utf8.c_str());

  ImGui::ItemSize         (ImVec2 (95.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 2 should have min-width 95px (scaled with the DPI) (a bit smaller to allow for some right-hand padding for "Waiting for game...")
  ImGui::EndGroup         ( );

  const float fColumn2Width = ImGui::GetItemRectSize ( ).x; // As wide as its widest row, at least 95 px

  // After column 2 (as wide as its widest row, at least 95 px), else as far right as it fits, but never over the first row of column 2
  float fColumn3X = posColumn2.x + fColumn2Width + fSpacing;

  if (fColumn3X + fColumn3Width > fColumnsRight)
    fColumn3X = std::max (posColumn2.x + fColumn2Row1 + fSpacing, fColumnsRight - fColumn3Width);

  ImGui::SameLine         ( );
  ImGui::SetCursorPosX    (fColumn3X);

  // Column 3
  ImGui::BeginGroup       ( );

  static bool quickServiceHover = false;

  // Service quick toogle / Waiting for game...
  if (bQuickToggle)
  {
    // Still too wide (a narrow window, or a very long text): shortened with an ellipsis, and shown in full in the tooltip.
    //   Drawn from where the toggle starts: it spans all columns, so its item rect starts at the left edge of the frame.
    const bool   bTruncated    = (szQuickToggleId != nullptr && fColumn3X + fColumn3Width > fColumnsRight);
    const ImVec2 posToggle     = ImGui::GetCursorScreenPos ( );
    const float  fToggleRight  = posToggle.x + (fColumnsRight - fColumn3X);

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, (quickServiceHover) ? _inject.ui_game_summary.color_hover.Value
                                                             : _inject.ui_game_summary.color.Value);

    if (ImGui::Selectable ((bTruncated) ? szQuickToggleId : szQuickToggle, false, ImGuiSelectableFlags_SpanAllColumns, ImVec2 (360.0f * SKIF_ImGui_GlobalDPIScale, 0.0f)))
    {
      // Only allow the toggle to work to disable the service while a game is loading
      if (pApp->loading)
      {
        if (_inject.bCurrentState)
          _inject._StartStopInject (true);
      }

      // For non-loading games, allow the full functionality
      else
        _inject._StartStopInject (_inject.bCurrentState, _registry.bStopOnInjection, pApp->launch_configs[0].isElevated ( ), static_cast <int> (SKIF_LaunchBehaviors::Resolve (pApp->launch_configs[0].getExecutableFullPath ( )).autoStop));
    }

    if (bTruncated)
      ImGui::RenderTextEllipsis (ImGui::GetWindowDrawList ( ), posToggle, ImVec2 (fToggleRight, posToggle.y + ImGui::GetTextLineHeight ( )),
                                 fToggleRight, szQuickToggle, szQuickToggleEnd, nullptr);

    ImGui::PopStyleColor (3);

    quickServiceHover = ImGui::IsItemHovered ();

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverTip        ((bTruncated) ? std::string (szQuickToggle, szQuickToggleEnd) + "\n" + T_(_inject.ui_game_summary.hover_tip.c_str ())
                                                : std::string (T_(_inject.ui_game_summary.hover_tip.c_str ())));

    if ( ! ImGui::IsPopupOpen ("ServiceMenu") &&
           ImGui::IsItemClicked (ImGuiMouseButton_Right))
      ServiceMenu = PopupState_Open;
  }

  // The update button is the first item of the column (at fColumn3X, where its width was reserved)
  else if (! bUpdateLocal)
    ImGui::NewLine ( );

  if (! pApp->loading && pApp->specialk.injection.injection.type == InjectionType::Local)
  {
    if (bUpdateLocal)
    {
      ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
      if (ImGui::SmallButton (ICON_FA_ARROW_UP))
      {
        _UpdateLocalDLLFile ( );
      }
      ImGui::PopStyleColor ( );

      SKIF_ImGui_SetHoverTip (F_("The local DLL file is outdated.\n"
                                 "Click to update it to v {0}.", _inject.SKVer32_utf8));
    }
  }

  ImGui::EndGroup         ( );

  // End of columns
  ImGui::EndGroup         ( );

  ImGui::EndChild         ( );
}


// Details panel, middle frame: the Play / install button (moved verbatim from the former GetInjectionSummary)
static void
DrawGameDetails_Play (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  auto frame_id2 =
    ImGui::GetID ("###Injection_Play_Button_Frame");

  /*
  ImGui::PushStyleVar (
    ImGuiStyleVar_FramePadding,
      ImVec2 (((_registry.bUIBorders) ? 104.0f : 105.0f) * SKIF_ImGui_GlobalDPIScale,
                40.0f * SKIF_ImGui_GlobalDPIScale)
  );
  */

  SKIF_ImGui_BeginChildFrame (
    frame_id2, ImVec2 (  0.0f,
                         0.0f), //110.f * SKIF_ImGui_GlobalDPIScale ),
      ImGuiChildFlags_NavFlattened,
      ImGuiWindowFlags_NoScrollbar       |
      ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBackground
  );

  //ImGui::PopStyleVar ();

  std::string      buttonLabel   = LI_(ICON_FA_GAMEPAD " ", "Play");
  bool             buttonInstall = false,
                   buttonPending = false;

  if (! pApp->loading &&
        pApp->HasSpecialKModInstaller ( ) && // Expose installer for those games with better specific mods
        pApp->specialk.injection.injection.type != InjectionType::Local)
  {
    buttonLabel   = LI_(ICON_FA_DOWNLOAD " ", "Install Mod");
    buttonInstall = true;

    uint32_t _modAppId = modAppId.load();

    if (_modAppId == pApp->id)
    {
      if (modInstalling.load())
      {
        buttonLabel = L_("Installing...");
        buttonPending = true;
      }

      else if (modDownloading.load())
      {
        buttonLabel = L_("Downloading...");
        buttonPending = true;
      }
    }

    else if (_modAppId > 0)
    {
      buttonLabel   = L_("Pending...");
      buttonPending = true;
    }
  }

  static bool  loading      = false;
  static DWORD loadingTimer = 0;

  if (pApp->loading)
  {
    if (loadingTimer == 0)
      loadingTimer    = SKIF_Util_timeGetTime ( ) + 32;

    // 32 ms delay before we indicate the game is loading in the background
    if (! loading && loadingTimer < SKIF_Util_timeGetTime ( ))
      loading         = true;
  }

  // Reset once we are no longer loading
  else {
    loading      = false;
    loadingTimer = 0;
  }

  ImVec2 posButton =
     ImGui::GetCursorPos ( );

  if (loading || pApp->_status.running || pApp->IsUpdateBlockingLaunch ( ) || buttonPending)
  {
    buttonLabel =               (loading) ? L_("Loading...") :
                  (pApp->_status.running) ? L_("Running...") :
                                            L_("Updating...");
    ImGui::PushStyleColor (ImGuiCol_Button, ImGui::GetStyleColorVec4 (ImGuiCol_Button) * ImVec4 (0.75f, 0.75f, 0.75f, 1.0f));
    ImGui::BeginDisabled  ( );
  }

  // At least 150 px wide, wider if the (translated) label needs it
  const float fButtonWidth = std::max (150.0f * SKIF_ImGui_GlobalDPIScale,
                                       ImGui::CalcTextSize (buttonLabel.c_str ( ), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f);

  // Horizontal center-align
  ImGui::SetCursorPosX (
     ImGui::GetCursorPosX ( ) +
    (ImGui::GetContentRegionAvail ( ).x - fButtonWidth +
     ImGui::GetStyle ( ).FramePadding.x) / 2
  );

  // Vertical center-align
  ImGui::SetCursorPosY (
     ImGui::GetCursorPosY ( )           +
    (ImGui::GetContentRegionAvail ( ).y -
     ImGui::GetFrameHeightWithSpacing() -
     (50.0f * SKIF_ImGui_GlobalDPIScale)
    ) / 2
  );

  if (_registry.bUIBorders)
    ImGui::SetCursorPosY (
      ImGui::GetCursorPosY () -
      ImGui::GetStyle      ().FramePadding.y);

  if (ImGui::ButtonEx (
              buttonLabel.c_str (),
                  ImVec2 ( fButtonWidth,
                            50.0f * SKIF_ImGui_GlobalDPIScale )))
  {
    if (pApp->loading)
    {
      // Do nothing if we're loading
      loading    = true;
    }

    else if (! buttonInstall)
      launchGame = true;

    else
    {
      static uint32_t appid;
      appid = pApp->id;

      // We're going to asynchronously download the mod installer on this thread
      if (! modDownloading.load())
      {
        HANDLE hWorkerThread = (HANDLE)
        _beginthreadex (nullptr, 0x0, [](void*) -> unsigned
        {
          modDownloading.store (true);

          SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibGameModWorker");

          CoInitializeEx (nullptr, 0x0);

          SKIF_Util_SetThreadPowerThrottling (GetCurrentThread(), 1); // Enable EcoQoS for this thread
          SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

          PLOG_VERBOSE << "SKIF_LibGameModWorker thread started!";

          int _appid = appid;
          modAppId.store (_appid);

          static const std::wstring root = SK_FormatStringW (LR"(%ws\Version\)", _path_cache.specialk_userdata);

          // Create any missing directories
          std::error_code ec;
          if (! std::filesystem::exists (            root, ec))
                std::filesystem::create_directories (root, ec);

          static std::wstring download, filename, path;

          switch (_appid)
          {
          case 359870: // FFX/X-2 HD Remaster
            filename = L"SpecialK_UnX.exe";
            download = L"https://sk-data.special-k.info/UnX/SpecialK_UnX.exe";
            break;
          case 429660: // Tales of Berseria
            filename = L"SpecialK_TBFix.exe";
            download = L"https://sk-data.special-k.info/TBFix/SpecialK_TBFix.exe";
            break;
          case 372360: // Tales of Symphonia
            filename = L"SpecialK_TSFix.exe";
            download = L"https://sk-data.special-k.info/TSFix/SpecialK_TSFix.exe";
            break;
          case 738540: // Tales of Vesperia DE
            filename = L"SpecialK_TVFix.exe";
            download = L"https://sk-data.special-k.info/TVFix/SpecialK_TVFix.exe";
            break;
          default:
            PLOG_ERROR << "Unknown app id: " << _appid;
            break;
          }

          path = root + filename;

          if (! filename.empty() && ! download.empty())
          {
            // Download the installer if it does not exist
            if (! PathFileExists (path.c_str()))
            {
              PLOG_INFO << "Downloading installer: " << download;
              SKIF_Util_GetWebResource (download, path);
            }

            modInstalling.store (true);

            SHELLEXECUTEINFOW
              sexi              = { };
              sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
              sexi.lpVerb       = L"OPEN";
              sexi.lpFile       = path.c_str();
              sexi.lpParameters = NULL;
              sexi.lpDirectory  = NULL;
              sexi.nShow        = SW_SHOWNORMAL;
              sexi.fMask        = SEE_MASK_NOCLOSEPROCESS | // We need the PID of the process that gets started
                                  SEE_MASK_NOASYNC        | // Never async since we execute in short-lived child thread
                                  SEE_MASK_NOZONECHECKS;    // No zone check needs to be performed

            PLOG_INFO                    << "Performing a ShellExecuteEx call...";
            PLOG_INFO_IF(! path.empty()) << "File      : " << path;

            // Attempt to run the downloaded installer,
            //   and delete it on failure
            {
              // Note that any new process will inherit SKIF's environment variables, so remove SteamNoOverlayUIDrawing
              //   (if SKIF set it for itself) for the duration of the call. Scoped, so the environment lock is not
              //     held while waiting for the installer to close below.
              SKIF_Util_ScopedEnvironment scopedEnv (
                (_registry._LoadedSteamOverlay) ? std::map <std::wstring, std::wstring> { { L"SteamNoOverlayUIDrawing", L"" } }
                                                : std::map <std::wstring, std::wstring> { }
              );

              if (! ShellExecuteExW (&sexi))
              {
                PLOG_ERROR << "Something went wrong: "   << SKIF_Util_GetErrorAsWStr ( );
                PLOG_ERROR_IF (DeleteFile(path.c_str())) << "The downloaded installer has been removed!";
              }
            }

            // If the process was started successfully, wait for it to close down...
            if (sexi.hInstApp  > (HINSTANCE)32 &&
                sexi.hProcess != NULL)
            {
              WaitForSingleObject (sexi.hProcess, INFINITE); // == WAIT_OBJECT_0
              CloseHandle         (sexi.hProcess);
            }

            modInstalling.store (false);

            PLOG_INFO << "Finished installing a mod asynchronously...";
          }

          modAppId.store (0);
          modDownloading.store (false);

          // Force a refresh when the game icons have finished being streamed
          //PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

          PLOG_VERBOSE << "SKIF_LibGameModWorker thread stopped!";

          SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

          return 0;
        }, nullptr, 0x0, nullptr);

        if (hWorkerThread != NULL)
          CloseHandle (hWorkerThread);
      }
    }
  }

  if (buttonPending)
    SKIF_ImGui_SetHoverTip (_("Please finish the ongoing mod installation first."));

  // Only built while hovered: the command line of the launch config Play uses (the default one, as for Play in the game's
  //   context menu) in the status bar, and how it is launched in the tooltip
  else if (! buttonInstall && ImGui::IsItemHovered ( ))
  {
    app_record_s::launch_config_s*
      playConfig = pApp->GetDefaultLaunchConfig ( );

    SKIF_ImGui_SetHoverText (SKIF_Lib_GetLaunchCommand   (pApp, playConfig));
    SKIF_ImGui_SetHoverTip  (SKIF_Lib_GetPlayDescription (pApp, playConfig)); // Empty (none) for Custom games
  }

  if (loading || pApp->_status.running || pApp->IsUpdateBlockingLaunch ( ) || buttonPending)
  {
    ImGui::EndDisabled   ( );
    ImGui::PopStyleColor ( );
  }

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right) &&
      GameMenu == PopupState_Closed)
  {
    GameMenu = PopupState_Open;
  }

  // Warnings next to the launch button: VAC, and store-specific ones (itch.io: sandboxed launches; Ubisoft: still installing)
  std::string launchWarning = pApp->GetLaunchWarning ( );

  if (pApp->extended_config.vac.enabled == 1)
    launchWarning = _("Warning: VAC protected game; injection is not recommended!") + ((launchWarning.empty ( )) ? std::string ( ) : "\n\n" + launchWarning);

  if (! launchWarning.empty ( ))
  {
    ImGui::SameLine ( );
    // Vertical center-align
    ImGui::SetCursorPosY (
       ImGui::GetCursorPosY ( ) +
      ((50.0f * SKIF_ImGui_GlobalDPIScale) -
       ImGui::GetFrameHeightWithSpacing()
      ) / 2
    );
    //ImGui::SetCursorPosY (ImGui::GetCursorPosY() + 50.0f + ImGui::GetStyle().FramePadding.y * SKIF_ImGui_GlobalDPIScale);
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
    SKIF_ImGui_SetHoverTip (launchWarning);
  }

  ImGui::SetCursorPos (posButton);

  ImGui::EndChild     ( );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right) &&
      GameMenu       == PopupState_Closed &&
      EmptySpaceMenu == PopupState_Closed &&
    ! SKIF_ImGui_IsAnyPopupOpen ( ))
  {
    EmptySpaceMenu = PopupState_Open;
  }
}


// Whether a blacklist pattern (Settings tab) matches the executable of a launch config. The details panel asks every frame,
//   so the result is kept until the executable or the committed blacklist changes (blacklistRevision): _TestUserList ( )
//     matches the path against every pattern, and the built-in list, each time
static bool
SKIF_Lib_IsBlacklistedByPattern (app_record_s::launch_config_s* lc)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static std::wstring cachedPath;
  static int          cachedRevision = -1;
  static bool         cachedResult   = false;

  if (lc == nullptr || ! lc->isExecutableFullPathValid ( ))
    return false;

  std::wstring path = lc->getExecutableFullPath ( );

  if (cachedRevision != _inject.blacklistRevision || cachedPath != path)
  {
    cachedRevision = _inject.blacklistRevision;
    cachedResult   = _inject._TestUserList (SK_WideCharToUTF8 (path).c_str ( ), false);
    cachedPath     = std::move (path);
  }

  return cachedResult;
}

// Details panel, bottom bar: whether Special K is disabled for the default launch config (changed in Properties), or unavailable
static void
DrawGameDetails_LaunchBar (app_record_s* pApp)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  // Bottom bar: Disable Special K
  ImGui::SetCursorPosY (
    ImGui::GetWindowHeight () - fBottomDist);

  if (_registry.bUIBorders)
    ImGui::SetCursorPosY (
      ImGui::GetCursorPosY () -
      ImGui::GetStyle      ().ItemSpacing.y -
      ImGui::GetStyle      ().WindowBorderSize * 2.0f);

  ImGui::Separator     ( ); // -------------------------------

  SKIF_ImGui_BeginChildFrame  ( ImGui::GetID ("###launch_cfg"),
                                ImVec2 (ImGui::GetContentRegionAvail ().x,
                              std::max (ImGui::GetContentRegionAvail ().y,
                                        ImGui::GetTextLineHeight () + ImGui::GetStyle ().FramePadding.y * 2.0f + ImGui::GetStyle ().ItemSpacing.y * 2
                                        )),
                                ImGuiChildFlags_NavFlattened,
                                ImGuiWindowFlags_NoScrollbar       |
                                ImGuiWindowFlags_NoScrollWithMouse |
                                ImGuiWindowFlags_NoBackground
  );

  // Special K is disabled per executable in Properties (Launch options). A lock shows when the default launch config, the one
  //   Play uses, has it disabled, as it explains why Special K does not load; clicking it opens Properties there.
  app_record_s::launch_config_s*
    defaultLaunchConfig = pApp->GetDefaultLaunchConfig ( );

  // A local install is used instead of the service (the default launch config's own, else the game's; as LaunchGame ( ) resolves it)
  const InjectionType defaultType  = (defaultLaunchConfig != nullptr) ? defaultLaunchConfig->injection.injection.type : InjectionType::Unknown;
  const bool          defaultLocal = (defaultType != InjectionType::Unknown) ? (defaultType                              == InjectionType::Local)
                                                                             : (pApp->specialk.injection.injection.type == InjectionType::Local);

  // The row is laid out from both ends: Properties in the left corner, the state of Special K in the right corner
  const float fRowStartX = ImGui::GetCursorPosX ( );
  const float fRowEndX   = fRowStartX + ImGui::GetContentRegionAvail ( ).x;

  // Selectables are not as tall as a frame, so push the row down to where the text of a frame would be
  ImGui::SetCursorPosY (
    ImGui::GetCursorPosY  ( ) +
    ImGui::GetStyle       ( ).FramePadding.y
  );

  // An icon-only button: a Selectable as wide as the icon, in the given color, with a hand cursor and a tooltip
  auto _IconButton = [](const char* label, const ImVec4& color) -> bool
  {
    const ImVec2 size = ImGui::CalcTextSize (label, nullptr, true);

    ImGui::PushStyleColor (ImGuiCol_Text, color);
    bool clicked = ImGui::Selectable (label, false, ImGuiSelectableFlags_None, ImVec2 (size.x, 0.0f));
    ImGui::PopStyleColor  ( );

    SKIF_ImGui_SetMouseCursorHand ( );

    return clicked;
  };

  // Left corner: Properties (as Manage > Properties), Browse (the game menu's Browse submenu) and Configuration (as
  //   Manage > Configuration); grey, so they don't draw the eye
  if (_IconButton (ICON_FA_GEAR "###SKIFPropertiesShortcut", ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)))
    ModifyGamePopup = PopupState_Open;

  SKIF_ImGui_SetHoverTip (_("Properties"));

  ImGui::SameLine ( );

  if (_IconButton (ICON_FA_FOLDER "###SKIFBrowseShortcut", ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)))
    ImGui::OpenPopup ("###LaunchBarBrowseMenu");

  SKIF_ImGui_SetHoverTip (_("Browse"));

  if (ImGui::BeginPopup ("###LaunchBarBrowseMenu", ImGuiWindowFlags_NoMove))
  {
    // As the game menu: no navigation highlight (white border around items)
    ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

    DrawGameBrowseMenuItems (pApp);

    ImGui::PopStyleColor  ( );
    ImGui::EndPopup       ( );
  }

  ImGui::SameLine ( );

  if (_IconButton (ICON_FA_FILE_LINES "###SKIFConfigShortcut", ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)))
    SKIF_Lib_OpenGameConfig (pApp);

  if (ImGui::IsItemHovered ( ))
    SKIF_ImGui_SetHoverText (pApp->specialk.injection.config.full_path_utf8.c_str ( ));

  SKIF_ImGui_SetHoverTip (_("Configuration"));

  // Right-click: troubleshooting (as Manage > Configuration)
  DrawGameConfigMenu (pApp);

  // Right corner: how Play launches the default launch config, as icons placed from the right corner leftwards
  //   (only shown when it is not the usual: Special K disabled, an elevated service, Instant play)
  float fRightX = fRowEndX;

  auto _RightIconButton = [&](const char* label, const ImVec4& color) -> bool
  {
    const float fWidth = ImGui::CalcTextSize (label, nullptr, true).x;

    ImGui::SameLine      ( );
    ImGui::SetCursorPosX (fRightX - fWidth);

    fRightX -= fWidth + ImGui::GetStyle ( ).ItemSpacing.x;

    return _IconButton (label, color);
  };

  // Opens Properties for the selected game (this one), scrolled to a page; the launch options have the default one selected
  auto _OpenProperties = [](PropertiesPage page) -> void
  {
    SKIF_Properties_RequestPage (page);
    ModifyGamePopup = PopupState_Open;
  };

  if (! defaultLocal && ! _inject.bHasServlet)
  {
    ImGui::SameLine        ( );
    ImGui::BeginGroup      ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
    ImGui::SameLine        ( );
    SKIF_ImGui_TextColoredWrapped    (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Special K is unavailable due to missing files."));
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip (_("Please reinstall Special K using the latest available installer."));
  }

  else if (defaultLaunchConfig != nullptr)
  {
    // Special K disabled, or an elevated service (a blocked executable does not use the service, so never both)
    if (! defaultLocal)
    {
      const bool blacklisted        = defaultLaunchConfig->isBlacklisted ( );             // Cached by the launch config
      const bool blacklistedPattern = SKIF_Lib_IsBlacklistedByPattern (defaultLaunchConfig); // Cached above

      if (blacklisted || blacklistedPattern)
      {
        if (_RightIconButton (ICON_FA_LOCK "###SKDisabledIndicator", ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning)))
          _OpenProperties (PropertiesPage::Launch);

        if (ImGui::IsItemHovered ( ))
          SKIF_ImGui_SetHoverText (defaultLaunchConfig->getExecutableFullPathUTF8 ( ).c_str ( ));

        if (blacklistedPattern)
          SKIF_ImGui_SetHoverTip (_("Special K is disabled through a blacklist pattern in the Settings tab."));
        else
          SKIF_ImGui_SetHoverTip (_("Special K is disabled for the default launch option.\n"
                                    "Click to change it in Properties."));
      }

      else if (defaultLaunchConfig->isElevated ( ))
      {
        if (_RightIconButton (ICON_FA_USER_SHIELD "###SKElevatedIndicator", ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)))
          _OpenProperties (PropertiesPage::Launch);

        if (ImGui::IsItemHovered ( ))
          SKIF_ImGui_SetHoverText (defaultLaunchConfig->getExecutableFullPathUTF8 ( ).c_str ( ));

        SKIF_ImGui_SetHoverTip  (_("The injection service is started elevated for the default launch option.\n"
                                   "Click to change it in Properties."));
      }
    }

    // Instant play: Play starts the executable directly instead of through the platform (as LaunchGame ( ) decides it
    //   for the Play button). Custom games are always started directly, so it says nothing there.
    if (pApp->store != app_record_s::Store::Custom && pApp->PrefersInstantLaunch (false, defaultLaunchConfig))
    {
      // Properties > General > Play explains why (the launch option, or the Start setting)
      if (_RightIconButton (ICON_FA_ROCKET "###InstantPlayIndicator", ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)))
        _OpenProperties (PropertiesPage::General);

      if (ImGui::IsItemHovered ( ))
      {
        SKIF_ImGui_SetHoverText (SKIF_Lib_GetLaunchCommand (pApp, defaultLaunchConfig));
        SKIF_ImGui_SetHoverTip  (std::string (_("Starts the game directly (Instant play)."))
                               + "\n\n" +
                               _("Skips the regular platform launch process for the game,\n"
                                 "including steps such as cloud saves synchronization."));
      }
    }
  }

  ImGui::EndChild ( );
  fBottomDist = ImGui::GetItemRectSize().y;
}


// The details panel of the selected game (was GetInjectionSummary)
void
DrawGameDetails (app_record_s* pApp)
{
  if (pApp == nullptr)
    return;

  // Top frame: Special K DLL, profile folder, config file, platform, service toggle
  DrawGameDetails_Info (pApp);

  ImGui::Separator ();

  // Middle frame: the Play / install button
  DrawGameDetails_Play (pApp);

  // Bottom bar: Special K disabled / unavailable
  DrawGameDetails_LaunchBar (pApp);
}

#pragma endregion


#pragma region UpdateInjectionStrategy

void
UpdateInjectionStrategy (app_record_s* pApp, std::set <std::string> apptickets)
{
  if (pApp == nullptr || pApp->id == SKIF_STEAM_APPID)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  // A local install listed in the registry (SOFTWARE\Kaldaien\Special K\Local) somewhere below the install folder. The
  //   launch configs detect the local installs in their own folders below; this is only used for the game as a whole when
  //     none of its launch configs could be resolved (no valid executable, e.g. Link2EA), as it may be in any subfolder
  bool                             registry_local = false;
  app_record_s::sk_install_state_s registryLocal  = { };

  // Use a registry based method as the primary choice of information for local injections
  HKEY hKey;

  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Local\)", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
  {
    DWORD dwIndex           = 0, // A variable that receives the number of values that are associated with the key.
          dwResult          = 0,
          dwMaxValueNameLen = 0; // A pointer to a variable that receives the size of the key's longest value name, in Unicode characters. The size does not include the terminating null character.

    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, NULL, NULL, NULL, &dwIndex, &dwMaxValueNameLen, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;

        DWORD dwValueNameLen =
              (dwMaxValueNameLen + 2);

        std::unique_ptr <wchar_t []> pValue =
          std::make_unique <wchar_t []> (sizeof (wchar_t) * dwValueNameLen);

        DWORD dwType = REG_NONE;

        dwResult = RegEnumValueW (hKey, dwIndex, (wchar_t *) pValue.get(), &dwValueNameLen, NULL, &dwType, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS && dwType == REG_SZ)
        {
          if (StrStrIW (pValue.get(), (pApp->install_dir + LR"(\)").c_str()) != NULL)
          {
            std::wstring dll_full_path = std::wstring(pValue.get());

            std::wstring dll_ver =
              SKIF_Util_GetSpecialKDLLVersion (dll_full_path.c_str ());

            // Another validation that this is, in fact, a Special K DLL file we're dealing with
            if (! dll_ver.empty ())
            {
              registry_local = true;
              PLOG_VERBOSE << "Found local injection at: " << pValue;

              std::filesystem::path p = dll_full_path;
              std::wstring dll_name   = p.filename().replace_extension();
              std::wstring test_path  = p.parent_path();

              const std::vector <wchar_t *> cleaned = {
                L"DXGI",
                L"D3D11",
                L"D3D9",
                L"OpenGL32",
                L"DInput8",
                L"D3D8",
                L"DDraw"
              };

              for (auto& clean : cleaned)
              {
                if (StrStrIW (dll_name.c_str(), clean) != NULL)
                  dll_name = clean;
              }

              registryLocal.injection.type         = InjectionType::Local;
              registryLocal.dll.shorthand          = dll_name + L".dll";
              registryLocal.dll.shorthand_utf8     = SK_WideCharToUTF8 (registryLocal.dll.shorthand);
              registryLocal.dll.full_path          = test_path + LR"(\)" + dll_name + L".dll"; // This allows us to "clean" the DLL file of the full patch as well
              registryLocal.dll.full_path_utf8     = SK_WideCharToUTF8 (registryLocal.dll.full_path);
              registryLocal.dll.version            = dll_ver;
              registryLocal.dll.version_utf8       = SK_WideCharToUTF8 (registryLocal.dll.version);

              if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str()))
                registryLocal.config.type          = ConfigType::Centralized;
              else
              {
                registryLocal.config.type          = ConfigType::Localized;
                registryLocal.config.root_dir      = test_path;
                registryLocal.config.root_dir_utf8 = SK_WideCharToUTF8 (registryLocal.config.root_dir);
              }

              registryLocal.config.shorthand       = dll_name + L".ini";
              registryLocal.config.shorthand_utf8  = SK_WideCharToUTF8 (registryLocal.config.shorthand);
            }

            else {
              // If it is not, remove the registry value
              LSTATUS ls = RegDeleteValueW (hKey, pValue.get());
              PLOG_ERROR_IF (ls != ERROR_SUCCESS) << "Failed to delete registry value: " << SKIF_Util_GetErrorAsWStr (ls);
            }
          }
        }
      }
    }

    RegCloseKey (hKey);
  }

  int firstValidFound = -1;

  // Whether the cached bitness (db.json "CPU") can be used instead of reading the executables. Decided once, before
  //   the loop: the first launch config that reads its executable sets a new timestamp, which must not make the old
  //     cached value count as fresh for the launch configs after it
  const long long now           = (long long)time (nullptr);
  const bool      cpuCacheFresh = pApp->skif.IsCPUCacheFresh (now);

  for ( auto& launch_cfg : pApp->launch_configs )
  {
    auto& launch = launch_cfg.second;

    if (! launch.valid)
      continue;

    if (! launch.isExecutableFullPathValid ( ))
      continue;

    if (firstValidFound == -1)
      firstValidFound = launch_cfg.first;

    // Assume global (a local install in the folders of this launch config is detected below)
    launch.injection.injection.type        = InjectionType::Global;
    launch.injection.config.type           = ConfigType::Centralized;
    launch.injection.config.shorthand      = L"SpecialK.ini";
    launch.injection.config.shorthand_utf8 =
      SK_WideCharToUTF8 (launch.injection.config.shorthand);

    // Check bitness, in this order:
    //   1. the 'osarch' of the launch config, or of the game as a whole (common_config; Steam)
    //   2. the cached value (db.json "CPU"), while it is less than a week old ("CPUCached")
    //   3. the PE headers of the executable (else an outdated cached value)
    // Launch configs without an executable (e.g. Link2EA games) are skipped above: the primary one gets the game's
    //   'osarch' or cached value after this loop, and an unknown bitness is shown as SKIF's own (see bIs64Bit)
    if (launch.injection.injection.bitness == InjectionBitness::Unknown)
    {
      app_record_s::CPUType
          cpu_type  = pApp->common_config.cpu_type; // We start by using the common config

      if (cpu_type != app_record_s::CPUType::Any)
      {
        if (launch.cpu_type != app_record_s::CPUType::Common)
        {
          cpu_type =
            launch.cpu_type;
        }
      }

      else
      {
        // The Any case will be whatever bitness SKIF is running (32/64-bit).
        //   Thus, ignore launch configs of the opposite bitness (64/32-bit).
#ifdef _WIN64
        if (launch.cpu_type == app_record_s::CPUType::x86)
#else
        if (launch.cpu_type == app_record_s::CPUType::x64)
#endif
        {
          continue;
        }

        else {
          cpu_type =
            launch.cpu_type;
        }
      }

      if (     cpu_type == app_record_s::CPUType::x64)
        launch.injection.injection.bitness = InjectionBitness::SixtyFour;

      else if (cpu_type == app_record_s::CPUType::x86)
        launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;

      // If we still haven't resolved it, use SKIF's cached property, as long as it is less than a week old
      else if (cpuCacheFresh)
        launch.injection.injection.bitness = (InjectionBitness)pApp->skif.cpu_type;

      // In case we still haven't resolved the CPU architecture (or the cached value is a week old),
      //   we need to check the actual arch of the game executable
      else // Common || Any
      {
        if (launch.isExecutableFullPathValid ())
        {
          std::wstring exec_path =
            launch.getExecutableFullPath ( );

          // Only the PE headers are read, unlike GetBinaryTypeW ( ) which maps the whole executable as an image
          //   (which can trigger an antivirus scan that takes seconds for large, freshly installed executables)
          const PEBitness bitness = SKIF_Util_GetBinaryType (exec_path.c_str ());

          if (bitness == PEBitness_64bit)
            launch.injection.injection.bitness = InjectionBitness::SixtyFour;
          else if (bitness == PEBitness_32bit)
            launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;

          // Checked: the cached value is renewed (stored in db.json once the game worker's results are applied)
          if (bitness == PEBitness_64bit || bitness == PEBitness_32bit)
            pApp->skif.cpu_cached = now;
        }

        // The executable could not be checked: an outdated cached value is still better than none
        if (launch.injection.injection.bitness == InjectionBitness::Unknown && pApp->skif.cpu_type != 0)
          launch.injection.injection.bitness = (InjectionBitness)pApp->skif.cpu_type;
      }
    }
    // End checking bitness

    // Local installs in the folders of this launch config (not the game's state of the last refresh, which would skip this)
    {
      // Check for local injections
      struct {
        std::wstring     name;
        std::wstring     path;
      } test_dlls [] = { // The small things matter -- array is sorted in the order of most expected
        { L"DXGI",     L"" },
        { L"D3D11",    L"" },
        { L"D3D9",     L"" },
        { L"OpenGL32", L"" },
        { L"DInput8",  L"" },
        { L"D3D8",     L"" },
        { L"DDraw",    L"" }
      };

      // The folders of this launch config (its executable's, and its working directory), so that a local install next to
      //   a secondary executable is found as well (this used to test the folders of the primary launch config only)
      std::wstring test_paths[] = {
        launch.getExecutableDir ( ),
        launch.working_dir
      };

      if (test_paths[0] == test_paths[1])
        test_paths[1] = L"";

      bool breakOuterLoop = false;
      for ( auto& test_path : test_paths)
      {
        if (test_path.empty())
          continue;

        for ( auto& dll : test_dlls )
        {
          // Check for the exact file names directly instead of enumerating every DLL file in the folder
          dll.path = test_path + LR"(\)" + dll.name + L".dll";

          const DWORD dwAttributes = GetFileAttributesW (dll.path.c_str ());

          if (dwAttributes == INVALID_FILE_ATTRIBUTES || (dwAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;

          std::wstring dll_ver =
            SKIF_Util_GetSpecialKDLLVersion (dll.path.c_str ());

          if (dll_ver.empty ())
            continue;

          launch.injection.injection.type         = InjectionType::Local;

          launch.injection.dll.shorthand          = dll.name + L".dll";
          launch.injection.dll.shorthand_utf8     = SK_WideCharToUTF8 (launch.injection.dll.shorthand);
          launch.injection.dll.full_path          = dll.path;
          launch.injection.dll.full_path_utf8     = SK_WideCharToUTF8 (launch.injection.dll.full_path);
          launch.injection.dll.version            = dll_ver;
          launch.injection.dll.version_utf8       = SK_WideCharToUTF8 (launch.injection.dll.version);

          if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
            launch.injection.config.type          = ConfigType::Centralized;
          else
          {
            launch.injection.config.type          = ConfigType::Localized;
            launch.injection.config.root_dir      = test_path;
            launch.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (launch.injection.config.root_dir);
          }

          launch.injection.config.shorthand       = dll.name + L".ini";
          launch.injection.config.shorthand_utf8  = SK_WideCharToUTF8 (launch.injection.config.shorthand);

          breakOuterLoop = true;
          break;
        }

        if (breakOuterLoop)
          break;
      }
    }

    // Check if the launch config is elevated or blacklisted
    launch.isElevated    ( );
    launch.isBlacklisted ( );
  } // End for ( auto& launch_cfg : pApp->launch_configs )

  // Swap out the first element for the first valid one we found
  if (firstValidFound != -1)
  {
    app_record_s::launch_config_s copy      = pApp->launch_configs.count (0) ?
                                              pApp->launch_configs.at    (0) :
                                      app_record_s::launch_config_s {       };

    if (pApp->launch_configs.count (0))
    {
      pApp->launch_configs [0]               = pApp->launch_configs[firstValidFound];
      pApp->launch_configs [firstValidFound] = copy;
    }
  }

  // If we cannot detect any launch configs, add an empty one
  if (pApp->launch_configs.empty ())
  {
    PLOG_ERROR << "Could not detect any launch configs?! Defaulting to an empty one.";
    pApp->launch_configs.emplace (0, app_record_s::launch_config_s());
  }

  // This is safe because we inserted one above
  auto& launch =
    pApp->launch_configs.begin ()->second;

  // If primary launch config was invalid (e.g. Link2EA games) then set it to use global
  if (launch.injection.injection.type == InjectionType::Unknown)
  {
    // Assume global
    launch.injection.injection.type        =
      InjectionType::Global;
    launch.injection.config.type           =
      ConfigType::Centralized;
    launch.injection.config.shorthand      =
      L"SpecialK.ini";
    launch.injection.config.shorthand_utf8 =
      SK_WideCharToUTF8 (launch.injection.config.shorthand);
  }

  // The primary launch config may have no executable (e.g. Link2EA games), so the loop above could not resolve its
  //   bitness: use what is known about the game as a whole
  if (launch.injection.injection.bitness == InjectionBitness::Unknown)
  {
    if      (pApp->common_config.cpu_type == app_record_s::CPUType::x64)
      launch.injection.injection.bitness = InjectionBitness::SixtyFour;
    else if (pApp->common_config.cpu_type == app_record_s::CPUType::x86)
      launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;
    else if (pApp->skif.cpu_type == 1 || pApp->skif.cpu_type == 2) // Cached; can't be checked again without an executable
      launch.injection.injection.bitness = (InjectionBitness)pApp->skif.cpu_type;
  }

  // Main UI stuff (details, menus, the profile and config, the cached bitness) follows the launch config that Play
  //   launches: the default one (Properties > Launch), else the primary one. Also the primary one when the default one
  //     was not resolved above (no valid executable, e.g. Link2EA, or skipped as a launch config of the other bitness).
  //   Launching itself uses the state of the launch config it launches (library_launch.cpp)
  app_record_s::launch_config_s* shown = pApp->GetDefaultLaunchConfig ( );

  if (shown == nullptr || shown->injection.injection.type == InjectionType::Unknown)
    shown = &launch;

  pApp->specialk.injection = shown->injection;

  // No launch config with a valid executable (e.g. Link2EA), so a registered local install can't be tied to one: it
  //   applies to the game, as Special K would be loaded from it
  if (registry_local && firstValidFound == -1)
  {
    registryLocal.injection.bitness = pApp->specialk.injection.injection.bitness;
    pApp->specialk.injection        = registryLocal;
  }

  if ( InjectionType::Global ==
      pApp->specialk.injection.injection.type )
  {
    // Unknown: assume the bitness of SKIF itself, as for "Any" above (64-bit Special K only runs on 64-bit Windows)
    //   This is only what is shown: the bitness itself stays unknown, so it is not cached in db.json as if detected
    bool bIs64Bit =
#ifdef _WIN64
      (pApp->specialk.injection.injection.bitness != InjectionBitness::ThirtyTwo);
#else
      (pApp->specialk.injection.injection.bitness == InjectionBitness::SixtyFour);
#endif

    pApp->specialk.injection.config.type =
      ConfigType::Centralized;

    pApp->specialk.injection.dll.shorthand      =
                                       bIs64Bit
#ifdef _WIN64
                                                ? L"SpecialK64.dll"
#else
                                                ? L"64-bit required!"
#endif
                                                : L"SpecialK32.dll";
    pApp->specialk.injection.dll.shorthand_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.dll.shorthand);
    pApp->specialk.injection.dll.full_path      = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, pApp->specialk.injection.dll.shorthand.c_str());
    pApp->specialk.injection.dll.full_path_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.dll.full_path);
    pApp->specialk.injection.dll.version        =
#ifdef _WIN64
                                       bIs64Bit ? _inject.SKVer64
#else
                                       bIs64Bit ? L"64-bit required!"
#endif
                                                : _inject.SKVer32;
    pApp->specialk.injection.dll.version_utf8   = SK_WideCharToUTF8 (pApp->specialk.injection.dll.version);
  }

  // For local injections, we need to populate the registry as well (used for managed updates): the one shown, and those
  //   found next to the executables of the other launch configs, so the Local Manager lists and updates them all
  {
    std::set <std::wstring>                               registered;
    std::vector <const app_record_s::sk_install_state_s*> locals;

    if (InjectionType::Local == pApp->specialk.injection.injection.type)
      locals.push_back (&pApp->specialk.injection);

    for (const auto& launch_cfg : pApp->launch_configs)
    {
      if (InjectionType::Local == launch_cfg.second.injection.injection.type)
        locals.push_back (&launch_cfg.second.injection);
    }

    if (! locals.empty ( ))
    {
      LSTATUS lsKey = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Local)", &hKey);

      if (ERROR_SUCCESS == lsKey)
      {
        for (const app_record_s::sk_install_state_s* local : locals)
        {
          // Only those with a known DLL and version
          if (local->dll.full_path.empty ( ) || local->dll.version.empty ( ))
            continue;

          if (! registered.emplace (SKIF_Util_ToLowerW (local->dll.full_path)).second)
            continue;

          if (ERROR_SUCCESS != RegSetValueExW (hKey, local->dll.full_path.c_str(), 0, REG_SZ, (LPBYTE)local->dll.version.data(), (DWORD)local->dll.version.length() * sizeof(wchar_t)))
            PLOG_ERROR << "Failed adding local install (" << local->dll.version << ") to registry value: " << local->dll.full_path;
        }

        RegCloseKey (hKey);
      }
    }
  }

  // Use a localized profile name
  if (pApp->specialk.injection.localized_name.empty())
  {
    pApp->specialk.injection.localized_name =
      pApp->GetLocalizedName ( ); // Steam: name from the app manifest; else the real original

    // Strip any remaining null terminators
    pApp->specialk.injection.localized_name.erase(std::find(pApp->specialk.injection.localized_name.begin(), pApp->specialk.injection.localized_name.end(), '\0'), pApp->specialk.injection.localized_name.end());

    std::wstring name =
      SK_UTF8ToWideChar (pApp->specialk.injection.localized_name);

    name.erase ( std::remove_if ( name.begin (),
                                  name.end   (),

                              [](wchar_t tval)
                              {
                                static
                                const std::set <wchar_t>
                                  invalid_file_char =
                                  {
                                    L'\\', L'/', L':',
                                    L'*',  L'?', L'\"',
                                    L'<',  L'>', L'|',
                                  //L'&',

                                    //
                                    // Obviously a period is not an invalid character,
                                    //   but three of them in a row messes with
                                    //     Windows Explorer and some Steam games use
                                    //       ellipsis in their titles.
                                    //
                                    L'.'
                                  };

                                return
                                  ( invalid_file_char.find (tval) !=
                                    invalid_file_char.end  (    ) );
                              }
                          ),

               name.end ()
         );

    // Strip trailing spaces from name, these are usually the result of
    //   deleting one of the non-useable characters above.
    for (auto it = name.rbegin (); it != name.rend (); ++it)
    {
      if (*it == L' ') *it = L'\0';
      else                   break;
    }

    // Strip any remaining null terminators
    name.erase(std::find(name.begin(), name.end(), '\0'), name.end());

    pApp->specialk.profile_dir      = name;
    pApp->specialk.profile_dir_utf8 = SK_WideCharToUTF8 (pApp->specialk.profile_dir);
  }

  if ( ConfigType::Centralized ==
         pApp->specialk.injection.config.type )
  {
    pApp->specialk.injection.config.root_dir      =
      SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                            _path_cache.specialk_userdata,
                            pApp->specialk.profile_dir.c_str());
    pApp->specialk.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.root_dir);
  }

  pApp->specialk.injection.config.full_path      =
    ( pApp->specialk.injection.config.root_dir + LR"(\)" ) +
      pApp->specialk.injection.config.shorthand;
  pApp->specialk.injection.config.full_path_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.full_path);


  // v-<-<- Shared between games of all platforms ->->-v

  switch (pApp->specialk.injection.config.type)
  {
    case ConfigType::Centralized:
      pApp->specialk.injection.config.type_utf8 = N_("Centralized"); break;
    case ConfigType::Localized:
      pApp->specialk.injection.config.type_utf8 = N_("Localized");   break;
    default:
      pApp->specialk.injection.config.type_utf8 = N_("Unknown");     break;
  }

  // Contemplate this -- should we bother? Why not just use _inject.bCurrentState ?
  /*
  bool service  = (pApp->specialk.injection.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
                  (pApp->specialk.injection.injection.bitness == InjectionBitness::SixtyFour &&  _inject.pid64) ||
                  (pApp->specialk.injection.injection.bitness == InjectionBitness::Unknown   && (_inject.pid32  &&
                                                                                                 _inject.pid64));
  */

  switch (pApp->specialk.injection.injection.type)
  {
    case InjectionType::Local:
      pApp->ui.label_version = SKIF_Util_FormatStringRaw (R"(v %s (%s))", pApp->specialk.injection.dll.version_utf8.c_str(), pApp->specialk.injection.dll.shorthand_utf8.c_str()); // injection.type_utf8.c_str()
      break;

    case InjectionType::Global:
    default: // Unknown injection strategy, but let's assume global would work
      if ( _inject.bHasServlet )
        pApp->ui.label_version = SKIF_Util_FormatStringRaw (R"(v %s)", pApp->specialk.injection.dll.version_utf8.c_str()); // injection.type_utf8.c_str() // We don't actually have SKIF say "Global v XXX" any longer due to space constraints -- Aemony, 2024-01-04
      break;
  }

  // Refresh the context menu cached data

  // Profile + Screenshots
  //pApp->ui.profileFolderExists     = PathFileExists (pApp->specialk.injection.config.root_dir.c_str());
  pApp->ui.wsScreenshotDir         = pApp->specialk.injection.config.root_dir + LR"(\Screenshots)";
  pApp->ui.screenshotsFolderExists = PathFileExists (pApp->ui.wsScreenshotDir.c_str()); // (pApp->ui.profileFolderExists) ?

  // Check how many secondary launch configs are valid
  pApp->ui.numSecondaryLaunchConfigs = 0;
  for (auto& _launch_cfg : pApp->launch_configs)
  {
    if (_launch_cfg.second.owns_dlc == -1)
        _launch_cfg.second.owns_dlc  = (_launch_cfg.second.requires_dlc.empty())
                                     ? 1 // If the launch cfg does not have a DLC requirement, then we "own" it (purely an optimization thing)
                                     : (! apptickets.empty() && apptickets.find (_launch_cfg.second.requires_dlc) != apptickets.end()); // Only show DLC options if the user has an app ticket for it
    // Note that this design hides all DLC launch options until the user has signed into Steam

    if (! _launch_cfg.second.owns_dlc)
      continue;

    if (_launch_cfg.first == 0)
      continue;

    if (! _launch_cfg.second.valid ||
          _launch_cfg.second.duplicate_exe_args)
      continue;

    pApp->ui.numSecondaryLaunchConfigs++;
  }

  // Steam Auto-Cloud
  pApp->ui.cloud_paths.clear();
  if (pApp->cloud_enabled && // If this is false, Steam Auto-Cloud is not enabled
    ! pApp->cloud_saves.empty ())
  {
    std::set <std::wstring> _used_paths;

    for (auto& cloud : pApp->cloud_saves)
    {
      if (cloud.second.valid == 0)
        continue;

      if (app_record_s::supports (cloud.second.platforms, app_record_s::Platform::Windows) &&
          app_record_s::Platform::Unknown != cloud.second.platforms)
        cloud.second.valid = 0;

      if (cloud.second.valid == -1)
        cloud.second.valid = PathFileExistsW (cloud.second.evaluated_dir.c_str());

      if (cloud.second.valid)
      {
        // Filter out duplicate paths
        if (_used_paths.emplace (cloud.second.evaluated_dir).second)
          pApp->ui.cloud_paths.emplace_back (app_record_s::CloudPath (cloud.first, cloud.second.evaluated_dir));
      }
    }
  }

  // PCGamingWiki
  pApp->ui.pcgw      = pApp->GetPCGamingWikiURL ( );

  // SteamGridDB
  pApp->ui.sgdbGrids = pApp->GetSteamGridDBURL  (false);
  pApp->ui.sgdbIcons = pApp->GetSteamGridDBURL  (true);

  // Steam Branches
  pApp->ui.branches.clear ();
  std::set <std::string> used_branches;
  for ( auto& it : pApp->branches )
  {
    if (used_branches.emplace (it.first).second)
    {
      auto& branch =
        it.second;

      // TODO: Maybe split sort in public v. private?

      // Sort in descending order
      pApp->ui.branches.emplace (
        std::make_pair   (-(int64_t)branch.build_id,
          std::make_pair (
            it.first,
            it.second
          )
        )
      );
    }
  }
}

#pragma endregion

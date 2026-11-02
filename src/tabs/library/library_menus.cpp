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

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

#pragma region GameConfigMenu

// This is an unusual popup that needs to be called from two different ImGui popup contexts...
// ... usually this means any existing popup is closed, hence why we instead invoke this directly where relevant!
void
DrawGameConfigMenu (app_record_s* pApp)
{
  if (pApp == nullptr)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
//static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static ImGuiID idGameConfigMenu = ImGui::GetID ("###GameConfigMenu");

  if (! ImGui::IsPopupOpen   (idGameConfigMenu, ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsItemClicked (ImGuiMouseButton_Right))
  {
    ImGui::OpenPopup    (idGameConfigMenu);
  }

  if (ImGui::BeginPopupEx (idGameConfigMenu, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
  {
    ImGui::PushStyleColor  (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

    ImGui::TextColored (
      ImColor::HSV (0.11F, 1.F, 1.F), "%s",
        _("Troubleshooting:")
    );

    ImGui::Separator ( );

    if (ImGui::Selectable (L_("External Editor")))
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
      //SKIF_UI_IniEditor_OpenFile (nullptr, pApp->specialk.injection.config.full_path, pApp->names.normal);

      // External editor
      SKIF_Util_OpenURI (pApp->specialk.injection.config.full_path, SW_SHOWNORMAL, NULL);
    }

    // Apply Preset, Apply Compatibility Config and Reset (shared with the INI editor, see <utility/ini_presets.h>)
    const SKIF_IniPresetChoice preset = SKIF_ImGui_IniPresetItems ( );

    // Writes the config file of the game, creating any missing directories
    auto _WriteConfig = [&](const wchar_t* content) -> void
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      std::wofstream config_file (pApp->specialk.injection.config.full_path.c_str(), std::wofstream::out | std::wofstream::trunc);

      if (config_file.is_open())
      {
        config_file << utf8_bom
                    << content;
        config_file.close ( );
      }
    };

    switch (preset.action)
    {
      case SKIF_IniPresetAction_Apply:
        CopyFile (preset.path.c_str(), pApp->specialk.injection.config.full_path.c_str(), FALSE);
        PLOG_VERBOSE << "Copying " << preset.path << " over to " << pApp->specialk.injection.config.full_path << ", overwriting any existing file in the process.";
        break;

      case SKIF_IniPresetAction_Compatibility:
        _WriteConfig (cfg_compatibility);
        break;

      case SKIF_IniPresetAction_Reset:
        _WriteConfig (L"");
        break;

      case SKIF_IniPresetAction_Edit:
        SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, preset.path, "");
        break;

      case SKIF_IniPresetAction_New:
        SKIF_UI_IniEditor_NewPreset ( );
        break;

      default:
        break;
    }

    ImGui::PopStyleColor ( );

    ImGui::EndPopup ( );
  }
}

#pragma endregion


#pragma region DrawGameContextMenu

std::string
SKIF_Lib_GetLaunchCommand (app_record_s* pApp, app_record_s::launch_config_s* lc)
{
  if (pApp == nullptr || lc == nullptr)
    return "";

  // Built by the same code that launches it (app_record_s::GetLaunchCommand ( ), used by LaunchInstant ( ))
  return SK_WideCharToUTF8 (pApp->GetLaunchCommand (lc).display);
}

// How Play (the button, and Play in the game menu) starts a launch config, as LaunchGame ( ) decides it: through the
//   platform client or directly, and which launch option, if it is not the platform's primary one. Empty for Custom games,
//     which are always started directly (so there is nothing to tell apart)
std::string
SKIF_Lib_GetPlayDescription (app_record_s* pApp, app_record_s::launch_config_s* lc)
{
  if (pApp == nullptr || lc == nullptr || pApp->store == app_record_s::Store::Custom)
    return "";

  std::string desc = (pApp->PrefersInstantLaunch (false, lc))
                   ? std::string (_("Starts the game directly (Instant play)."))
                   : F_("Starts the game through {0}.", pApp->store_utf8);

  if (! pApp->launch_configs.empty ( ) && lc != &pApp->launch_configs.begin ( )->second)
    desc += "\n" + F_("Launch option: {0}", lc->getDescriptionUTF8 ( ).empty ( )
                                               ? lc->getExecutableFileNameUTF8 ( )
                                               : lc->getDescriptionUTF8 ( ));

  return desc;
}

// Whether launching "without Special K" makes a difference for a launch config: otherwise the injection service would inject
//   Special K into it. As LaunchGame ( ) resolves it: a local install (of the launch config itself, else of the game) or a
//     blacklist (the launch config's own, or the global one) already means a launch without the service.
enum class SKIF_Lib_WithoutSK {
  Available,
  NoService,      // The injection service is missing
  LocalInjection, // A local install is used instead of the service
  Blacklisted     // Special K is disabled for the launch config, so a regular launch already does not inject it
};

static SKIF_Lib_WithoutSK
SKIF_Lib_GetWithoutSKState (app_record_s* pApp, app_record_s::launch_config_s* lc)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  if (pApp == nullptr || lc == nullptr || ! _inject.bHasServlet)
    return SKIF_Lib_WithoutSK::NoService;

  const InjectionType lcType = lc->injection.injection.type;

  if ((lcType != InjectionType::Unknown) ? (lcType                                   == InjectionType::Local)
                                         : (pApp->specialk.injection.injection.type == InjectionType::Local))
    return SKIF_Lib_WithoutSK::LocalInjection;

  // The launch behaviors (Properties) can have the service used all the same for an executable Special K is disabled for
  if ((lc->isBlacklisted ( ) || _inject._TestUserList (lc->getExecutableFullPathUTF8 ( ).c_str ( ), false)) && ! lc->isIgnoringBlacklist ( ))
    return SKIF_Lib_WithoutSK::Blacklisted;

  return SKIF_Lib_WithoutSK::Available;
}

// Game context menu: "Play with" submenu, to start a specific launch option: the primary one through the platform client,
//   and every launch option directly (Instant play). "Without Special K" applies to the entry clicked next.
static void
DrawGameContextMenu_PlayWith (app_record_s* pApp, bool playDisabled)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  // Reset whenever the submenu opens, so it only applies to the launch it was ticked for
  static bool withoutSK = false;

  app_record_s::launch_config_s*
    primary = (! pApp->launch_configs.empty ( )) ? &pApp->launch_configs.begin ( )->second : nullptr;

  // The primary launch config through the platform client (Custom games are always started directly)
  const bool platformEntry = (primary != nullptr                                &&
                              pApp->store != app_record_s::Store::Custom        &&
                              pApp->IsLaunchConfigAvailable (*primary)          &&
                            ! pApp->PrefersInstantLaunch    (true, primary)); // As launched from here: only if the platform client can start it

  // The launch configs started directly: if the platform allows it, or there are several (as the former Instant play menu)
  const bool directEntries = (pApp->CanLaunchExecutableDirectly ( ) &&
                             (pApp->ui.numSecondaryLaunchConfigs > 0 || pApp->isInstantPlayCompatible ( )));

  if (! platformEntry && ! directEntries)
    return;

  // Nothing to choose from: only the platform entry, while Play starts that one through the platform client already
  if (! directEntries && pApp->GetDefaultLaunchConfig ( ) == primary && ! pApp->PrefersInstantLaunch (false, primary))
    return;

  ImGui::Separator ( );

  SKIF_ImGui_PushDisableState (playDisabled);
  const bool open = SKIF_ImGui_BeginMenuEx2 (L_("Play with###GameContextMenu_PlayWith"), ICON_FA_PLAY);
  SKIF_ImGui_PopDisableState  (playDisabled);

  SKIF_ImGui_SetHoverTip (_("Starts a specific launch option: through the platform client, or directly (Instant play)."));

  if (! open)
    return;

  if (ImGui::IsWindowAppearing ( ))
    withoutSK = false;

  // Not available when a local install is used, as stopping the injection service does not keep that one out
  auto _LocalTip = [](void) { SKIF_ImGui_SetHoverTip (_("This option is not available due to a local injection being used."), false, true); };

  // The icon on the right: a lock when the launch starts without Special K (disabled for the launch config, or the toggle
  //   below is ticked), else a shield for an elevated service. Nothing when a local install is used with the toggle ticked
  //     (the entry is disabled then)
  auto _RightIcon = [&](app_record_s::launch_config_s* lc, SKIF_Lib_WithoutSK wosk) -> const char*
  {
    if (wosk == SKIF_Lib_WithoutSK::Blacklisted || (withoutSK && wosk == SKIF_Lib_WithoutSK::Available))
      return ICON_FA_LOCK;

    if (wosk == SKIF_Lib_WithoutSK::Available && lc->isElevated ( ))
      return ICON_FA_USER_SHIELD;

    return nullptr;
  };

  SKIF_ImGui_PushDisableState (playDisabled);

  // The primary launch config through the platform client
  if (platformEntry)
  {
    const SKIF_Lib_WithoutSK wosk         = SKIF_Lib_GetWithoutSKState (pApp, primary);
    const bool               localBlocked = (withoutSK && wosk == SKIF_Lib_WithoutSK::LocalInjection);

    SKIF_ImGui_PushDisableState (localBlocked);
    if (SKIF_ImGui_MenuItemEx3 ((pApp->store_utf8 + "###GameContextMenu_PlayWithPlatform").c_str ( ), ICON_FA_GAMEPAD, _RightIcon (primary, wosk),
                                ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Icon), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), ! playDisabled && ! localBlocked))
    {
      launchConfig      = primary;
      launchViaPlatform = true; // Explicit: through the platform client, whatever the Instant Play setting
      launchWithoutSK   = withoutSK;
    }
    SKIF_ImGui_PopDisableState  (localBlocked);

    if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled))
      SKIF_ImGui_SetHoverText (SKIF_Lib_GetLaunchCommand (pApp, primary));

    if (localBlocked)
      _LocalTip ( );
    else if (wosk == SKIF_Lib_WithoutSK::Blacklisted)
      SKIF_ImGui_SetHoverTip (_("Special K has been disabled for this launch configuration."), false, true);
    else
      SKIF_ImGui_SetHoverTip (_("Starts the primary launch option through the platform client."), false, true);
  }

  // Every launch config, directly
  if (directEntries)
  {
    if (platformEntry)
      ImGui::Separator ( );

    bool sepCustomSKIF = true,
         sepCustomUser = true;

    for (auto& _launch_cfg : pApp->launch_configs)
    {
      auto& _launch = _launch_cfg.second;

      // Valid, not a duplicate, and any required DLC is owned
      if (! pApp->IsLaunchConfigAvailable (_launch))
        continue;

      // Separators between official / user / SKIF launch configs
      if (_launch.custom_user && sepCustomUser)
      {
        sepCustomUser = false;
        ImGui::Separator ( );
      }

      else if (_launch.custom_skif && sepCustomSKIF)
      {
        sepCustomSKIF = false;
        ImGui::Separator ( );
      }

      // Blocked launch configs are started as well (without Special K, as always for them)
      const SKIF_Lib_WithoutSK wosk         = SKIF_Lib_GetWithoutSKState (pApp, &_launch);
      const bool               blocked      = (wosk == SKIF_Lib_WithoutSK::Blacklisted);
      const bool               localBlocked = (withoutSK && wosk == SKIF_Lib_WithoutSK::LocalInjection);

      char szButtonLabel [256] = { };

      sprintf_s ( szButtonLabel, 255,
                    "%s###GameContextMenu_PlayWith-%d",
                      _launch.getDescriptionUTF8 ( ).empty ( )
                        ? _launch.getExecutableFileNameUTF8 ( ).c_str ( )
                        : _launch.getDescriptionUTF8 ( ).c_str ( ),
                      _launch.id);

      SKIF_ImGui_PushDisableState (localBlocked);
      if (SKIF_ImGui_MenuItemEx3 (szButtonLabel, ICON_FA_ROCKET, _RightIcon (&_launch, wosk),
                                  ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Icon), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), ! playDisabled && ! localBlocked))
      {
        launchConfig    = &_launch;
        launchInstant   = true;
        launchWithoutSK = withoutSK;
      }
      SKIF_ImGui_PopDisableState  (localBlocked);

      if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled))
        SKIF_ImGui_SetHoverText (SKIF_Lib_GetLaunchCommand (pApp, &_launch));

      if (localBlocked)
        _LocalTip ( );
      else if (blocked)
        SKIF_ImGui_SetHoverTip (_("Special K has been disabled for this launch configuration."), false, true);
    }
  }

  // Applies to the entry clicked next; the submenu stays open when it is toggled
  if (_inject.bHasServlet)
  {
    ImGui::Separator ( );

    // A checkbox as the icon on the left, so it lines up with the icons of the entries above
    ImGui::PushItemFlag   (ImGuiItemFlags_AutoClosePopups, false);
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f));
    if (SKIF_ImGui_MenuItemEx3 (L_("without Special K###GameContextMenu_PlayWithWoSK"),
                                (withoutSK) ? ICON_FA_SQUARE_CHECK : ICON_FA_SQUARE, nullptr,
                                ImGui::GetStyleColorVec4 ((withoutSK) ? ImGuiCol_SKIF_Icon : ImGuiCol_TextDisabled)))
      withoutSK = ! withoutSK;
    ImGui::PopStyleColor  ( ); // ImGuiCol_Text
    ImGui::PopItemFlag    ( ); // ImGuiItemFlags_AutoClosePopups

    SKIF_ImGui_SetHoverTip (_("Starts the launch option clicked next without Special K.\n"
                              "This stops the injection service, so other games launched meanwhile are affected as well."));
  }

  SKIF_ImGui_PopDisableState  (playDisabled);

  ImGui::EndMenu ( );
}


// "Switch to game": brings the main window of a process to the front. That is the window the taskbar shows for it: a visible
//   top-level window that is either marked WS_EX_APPWINDOW, or is not owned and not a tool window. Many games (older ones in
//     particular) do not mark their window WS_EX_APPWINDOW at all, and a process may have other windows marked so that are
//       hidden (e.g. created by a wrapper or overlay), so the first visible WS_EX_APPWINDOW one is preferred, else the first
//         other one the taskbar would show
static void
SKIF_Lib_SwitchToProcessWindow (DWORD pid)
{
  if (pid == 0)
    return;

  struct search_s {
    DWORD pid       = 0;
    HWND  appWindow = nullptr; // Visible, WS_EX_APPWINDOW
    HWND  taskbar   = nullptr; // Visible, not owned, not a tool window
  } search;

  search.pid = pid;

  EnumWindows ([](HWND hWnd, LPARAM lParam) -> BOOL
  {
    search_s* _search = reinterpret_cast <search_s*> (lParam);

    DWORD                            dwPID = 0;
    GetWindowThreadProcessId (hWnd, &dwPID);

    if (dwPID != _search->pid || ! IsWindowVisible (hWnd)) // Minimized windows are still visible
      return TRUE;

    const LONG_PTR exStyle = GetWindowLongPtrW (hWnd, GWL_EXSTYLE);

    if (exStyle & WS_EX_APPWINDOW)
    {
      _search->appWindow = hWnd;
      return FALSE; // The best match
    }

    if (_search->taskbar == nullptr && ! (exStyle & WS_EX_TOOLWINDOW) && GetWindow (hWnd, GW_OWNER) == nullptr)
      _search->taskbar = hWnd;

    return TRUE;
  }, reinterpret_cast <LPARAM> (&search));

  HWND hWnd = (search.appWindow != nullptr) ? search.appWindow : search.taskbar;

  if (hWnd == nullptr)
  {
    PLOG_WARNING << "Could not find a window to switch to for process " << pid;
    return;
  }

  wchar_t wszClass [128] = { };
  GetClassNameW (hWnd, wszClass, 127);

  PLOG_VERBOSE << "Switching to the window " << hWnd << " (class: " << wszClass << ", " << ((search.appWindow != nullptr) ? "app window" : "taskbar window") << ") of process " << pid;

  if (IsIconic (hWnd))
    ShowWindow (hWnd, SW_RESTORE);

  SetForegroundWindow (hWnd);
}


// Game context menu: items for a running game (moved verbatim from DrawGameContextMenu)
static void
DrawGameContextMenu_Running (app_record_s* pApp)
{
  if (pApp->_status.running)
  {
    SKIF_Util_CreateProcess_s* mon_app = nullptr;
    for (auto& monitored_app : SKIF_GamingCollection::Get ( ).GetInstantPlayProcesses ( ))
      if (monitored_app.id == pApp->id && monitored_app.store_id == (int)pApp->store)
        mon_app = &monitored_app;

    auto ResumeGame = [](DWORD pid)
    {
      SKIF_Lib_SwitchToProcessWindow (pid);
    };

    if (mon_app != nullptr)
    {
      HANDLE hProcess = mon_app->hProcess.load();

      ImGui::Separator ( );

      bool bInvalid = (hProcess == NULL);

      if (bInvalid)
        ImGui::BeginDisabled ( );

      if (SKIF_ImGui_MenuItemEx2 (L_("Switch to game"), ICON_FA_WINDOW_RESTORE))
      {
        auto async_switch =
        std::async (
          std::launch::async, [hProcess,ResumeGame](void)
          {
            std::this_thread::sleep_for (std::chrono::milliseconds (125));
            ResumeGame (GetProcessId (hProcess));
          }
        );
      }

      if (SKIF_ImGui_MenuItemEx2 (L_("Terminate game"), ICON_FA_POWER_OFF))
      {
        static_proc.handle = hProcess;
        static_proc.name   = pApp->names.normal;
      }

      if (bInvalid)
        ImGui::EndDisabled ( );
    }

    else if (pApp->_status.running_pid != 0)
    {
      ImGui::Separator ( );

      if (SKIF_ImGui_MenuItemEx2 (L_("Switch to game"), ICON_FA_WINDOW_RESTORE))
      {
        auto async_switch =
        std::async (
          std::launch::async, [pApp,ResumeGame](void)
          {
            std::this_thread::sleep_for (std::chrono::milliseconds (125));
            ResumeGame (pApp->_status.running_pid);
          }
        );
      }

      if (SKIF_ImGui_MenuItemEx2 (L_("Terminate game"), ICON_FA_POWER_OFF))
      {
        static_proc.pid    = pApp->_status.running_pid;
        static_proc.name   = pApp->names.normal;
      }
    }
  }
}


// Opens the game's Special K config file in SKIF's INI editor, creating it (and its folders) first if it does not exist:
//   Manage > Configuration in the game menu, and the Configuration button in the launch bar of the game details
void
SKIF_Lib_OpenGameConfig (app_record_s* pApp)
{
  if (pApp == nullptr)
    return;

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

// The items of the "Browse" menu: the game menu's Browse submenu, and the Browse button in the launch bar of the game details
void
DrawGameBrowseMenuItems (app_record_s* pApp)
{
  {
    if (SKIF_ImGui_MenuItemEx2 (L_("Game folder"), ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
      SKIF_Util_ExplorePath         (pApp->install_dir);

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->install_dir));

    // Config Root
    if (SKIF_ImGui_MenuItemEx2 (L_("Profile folder"), ICON_FA_FOLDER_OPEN, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      SKIF_Util_ExplorePath       (pApp->specialk.injection.config.root_dir);
    }

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->specialk.injection.config.root_dir.c_str()).c_str());

    if (pApp->ui.screenshotsFolderExists)
    {
      // Screenshot Folder
      if (SKIF_ImGui_MenuItemEx2 (L_("Screenshots"), ICON_FA_IMAGES, ImColor(200, 200, 200, 255)))
        SKIF_Util_ExplorePath       (pApp->ui.wsScreenshotDir);

      SKIF_ImGui_SetMouseCursorHand ();
      SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->ui.wsScreenshotDir.data()).c_str());
    }

    // --------------------------------------------

    if (! pApp->ui.cloud_paths.empty())
    {
      ImGui::Separator  ( );

      if (SKIF_ImGui_BeginMenuEx2 (L_("Save/config folders"), ICON_FA_FLOPPY_DISK, ImColor(200, 200, 200, 255)))
      {
        for (auto& folder : pApp->ui.cloud_paths)
        {
          ImGui::PushStyleColor ( ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) // * ImVec4(1.0f, 1.0f, 1.0f, 1.0f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
          );

          if (SKIF_ImGui_MenuItemEx2 (folder.label.c_str(), ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
          {
            SKIF_Util_ExplorePath       (folder.path);
            ImGui::CloseCurrentPopup    ( );
          }

          ImGui::PopStyleColor  ( );

          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (folder.path_utf8);
        }

        ImGui::EndMenu ();
      }
    }
  }
}

// Game context menu: "Browse" submenu
static void
DrawGameContextMenu_Browse (app_record_s* pApp)
{
  if (SKIF_ImGui_BeginMenuEx2 (L_("Browse"), ICON_FA_FOLDER, ImColor(255, 207, 72)))
  {
    DrawGameBrowseMenuItems (pApp);

    ImGui::EndMenu ();
  }
}


// The tooltip of a game that is a favorite because it is one in its store client (Steam): Favorite / Unfavorite in the
//   game menu, and Favorited in Properties
std::string
SKIF_Lib_GetClientFavoriteTip (const std::string& client)
{
  // TRANSLATORS: {0} is the name of a store client, e.g. Steam
  return F_("Favorited in {0}. Changing it here only affects Special K.", client);
}

// Game context menu: "Manage" submenu (moved verbatim from DrawGameContextMenu)
static void
DrawGameContextMenu_Manage (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  // Manage Game
  if (ImGui::BeginMenuEx (L_("Manage"), ICON_FA_GEARS))
  {
    bool desktopShortcutPossible = pApp->CanCreateDesktopShortcut ( );

    if (SKIF_ImGui_MenuItemEx2 (L_("Properties"), ICON_FA_GEAR, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
      ModifyGamePopup = PopupState_Open;

    // Config File (right-click: troubleshooting)
    if (SKIF_ImGui_MenuItemEx2 (L_("Configuration"), ICON_FA_FILE_LINES)) // pApp->specialk.injection.config.shorthand_utf8.c_str ()
      SKIF_Lib_OpenGameConfig (pApp);

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.config.full_path_utf8.c_str ());

    DrawGameConfigMenu (pApp);

    constexpr char* labelPin   =   NL_("Favorite###Manage-Favorite");
    constexpr char* labelUnpin = NL_("Unfavorite###Manage-Favorite");

    bool isFavorite = pApp->IsFavorite ( );
    if (SKIF_ImGui_MenuItemEx2 (TL_(isFavorite ? labelUnpin : labelPin), (isFavorite ? ICON_FA_HEART_CRACK : ICON_FA_HEART), ImColor(245, 66, 66, 255)))
    {
      pApp->skif.pinned = isFavorite ? 0 : 1;

      SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp   (pApp, true);

      SKIF_GamingCollection::SortApps (&_games.GetApps());
      sort_changed = true;
    }

    // A favorite only because the game is one in the store client (Steam), until it is changed here
    else if (pApp->skif.pinned == -1 && pApp->GetClientFavorite ( ) == 1)
      SKIF_ImGui_SetHoverTip (SKIF_Lib_GetClientFavoriteTip (pApp->store_utf8));

    ImGui::Separator ( );

    // Category
    if (SKIF_ImGui_BeginMenuEx2 (L_("Category"), ICON_FA_OBJECT_GROUP, ImColor(200, 200, 200, 255)))
    {
      // Prevent the popup from being closed when selecting a category
      ImGui::PushItemFlag (ImGuiItemFlags_AutoClosePopups, false); // ImGuiItemFlags_SelectableDontClosePopup before ImGui 1.91.0
      ImGui::PushID       ("###ManageCategories");

      // Favorites is not a category of its own here (Favorite / Unfavorite above), but a favorite is only shown there
      if (isFavorite)
      {
        ImGui::TextDisabled ("%s", _("Favorited games are always shown under Favorites."));
        ImGui::Separator    ( );
      }

      static char charCategoryRename[maxCategoryNameLen] = {  };
      static char charCategoryName  [maxCategoryNameLen] = {  };
      static int  isRenaming = -1;
      static bool hasFocused = false;
      static constexpr char* renameLabel = "###ManageCategoriesRename";
      int index = 0;

      std::string categoryName, // Holds the old name
               newCategoryName; // Holds the new name
      bool        newCategory    = false; // New
      bool        activeInput    = false;

      for (auto& category : _registry.vecCategories)
      {
        // Skip the default categories
        if (category.name == "Favorites" || category.name == "Games")
          continue;

        if (index == isRenaming)
        {
          if (ImGui::InputTextEx (renameLabel, category.name.c_str(), charCategoryRename, maxCategoryNameLen,
                          ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, 0.0f), ImGuiInputTextFlags_EnterReturnsTrue))
          {
            newCategoryName = charCategoryRename;
            strncpy (charCategoryRename, "\0", maxCategoryNameLen);

            // Update _registry.mszCategories
            // ... but only if its not trying to rename Favorites or Games
            if (! newCategoryName.empty()        &&
                  newCategoryName != "Favorites" &&
                  newCategoryName != "Games")
            {
              static_category.Name    = category.name;
              static_category.newName = newCategoryName;
              static_category.change  = true;
              static_category.exists  = (std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == newCategoryName; }) != _registry.vecCategories.end());

              ImGui::CloseCurrentPopup ( );
              // It's not safe to iterate in this loop any longer
              break;
            }
          }

          else if (! hasFocused)
          {
            hasFocused  = true;
            activeInput = true;
            ImGui::ActivateItemByID (ImGui::GetID (renameLabel));
          }

          else if (ImGui::IsItemActive ( ))
            activeInput = true;

          else //! ImGui::IsItemActive ( )
          {
            isRenaming = -1;
            hasFocused = false;
          }
        }

        else
        {
          if (ImGui::MenuItem (category.name.c_str(), "", (pApp->skif.category == category.name), ! isFavorite))
          {
            // Delay setting the new category to prevent an one-frame issue from too new data
            newCategoryName = (pApp->skif.category != category.name) ? category.name : "";
            newCategory     = true;
          }
        }

        if (ImGui::IsItemHovered () && ImGui::IsMouseClicked (ImGuiMouseButton_Right))
          ImGui::OpenPopup (SKIF_Util_FormatStringRaw ("###Popup-%i", index));

        if (ImGui::BeginPopup (SKIF_Util_FormatStringRaw ("###Popup-%i", index), ImGuiWindowFlags_NoMove))
        {
          if (ImGui::Selectable ((std::string (_("Rename")) + SKIF_Util_FormatStringRaw ("###PopupRename-%i", index)).c_str ( )))
          {
            isRenaming = index;
            strncpy_s (charCategoryRename, category.name.c_str(), _TRUNCATE); // Always null-terminated
            ImGui::CloseCurrentPopup ( );
          }

          if (ImGui::Selectable ((std::string (_("Remove")) + SKIF_Util_FormatStringRaw ("###PopupRemove-%i", index)).c_str ( )))
          {
            static_category.Name   = category.name;
            static_category.change = true;
            static_category.remove = true;

            ImGui::CloseCurrentPopup ( );
          }

          ImGui::EndPopup ( );
        }

        index++;
      }

      // Renames and removals are handled in the main library loop through static_category and the CategoryManipulation popup

      // Handle adding the game to another existing category
      if (newCategory)
      {
        pApp->skif.category = newCategoryName;
        SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp (pApp, true);

        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true; // Disabled as this causes a noticable flicker on the menu when the game goes out and in of visibility
      }

      if (_registry.vecCategories.size() > 2) // Any other than the defaults: Favorites, Games ?
        ImGui::Separator ( );

      if (isFavorite)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::InputTextEx ("###ManageCategoriesAddNew", _("New category..."), charCategoryName, maxCategoryNameLen,
                      ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, 0.0f), ImGuiInputTextFlags_EnterReturnsTrue))
      {
        std::string szName = charCategoryName;
        strncpy (charCategoryName, "\0", maxCategoryNameLen);

        // Add the new category (shared with Properties)
        SKIF_Lib_AddCategory (szName);

        pApp->skif.category.clear();
        if (szName == "Favorites")
          pApp->skif.pinned = 1;

        else if (szName == "Games")
          pApp->skif.pinned = 0;

        else
          pApp->skif.category = szName;

        SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp (pApp, true);

        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      if (isFavorite)
        SKIF_ImGui_PopDisableState ( );

      activeInput = activeInput || ImGui::IsItemActive ( );

      if (activeInput)
        bCategoryMgnt = true;

      ImGui::PopID       ( ); // ###ManageCategories
      ImGui::PopItemFlag ( ); // ImGuiItemFlags_AutoClosePopups

      ImGui::EndMenu ( );
    }

    ImGui::Separator ( );

    if (desktopShortcutPossible)
    {
      if (SKIF_ImGui_MenuItemEx2 (L_("Create desktop shortcut"), ICON_FA_PAPERCLIP, ImColor(200, 200, 200, 255)))
      {
        std::string name = SKIF_Util_StripInvalidFilenameChars (pApp->names.normal);

        std::wstring linkPath = SK_FormatStringW (LR"(%ws\%ws.lnk)", std::wstring(_path_cache.desktop.path).c_str(), SK_UTF8ToWideChar(name).c_str());
        std::wstring linkArgs = pApp->GetDesktopShortcutArgs ( );
        std::wstring iconPath = pApp->GetDesktopShortcutIcon ( );

        const char* info_title         = _("Create desktop shortcut");
        const char* info_label_success = _("A desktop shortcut has been created.");
        const char* info_label_failure = _("Failed to create a desktop shortcut!");

        if (SKIF_Util_CreateShortcut (
            linkPath.c_str(),
            _path_cache.skif_executable,
            linkArgs.c_str(),
            (pApp->GetDefaultLaunchConfig ( ) != nullptr) ? pApp->GetDefaultLaunchConfig ( )->getWorkOrExeDirectory().c_str() : L"",
            SK_UTF8ToWideChar(name).c_str(),
            iconPath.c_str()
            )
          )
          SKIF_ImGui_InfoMessage (info_title, info_label_success);
        else
          SKIF_ImGui_InfoMessage (info_title, info_label_failure);
      }
    }

    constexpr char* labelHide   =   NL_("Hide");
    constexpr char* labelUnhide = NL_("Unhide");

    bool bUnhide = pApp->IsHidden ( );

    if (SKIF_ImGui_MenuItemEx2 (TL_(bUnhide ? labelUnhide : labelHide), (bUnhide ? ICON_FA_EYE : ICON_FA_EYE_SLASH), ImColor(200, 200, 200, 255)))
    {
      pApp->skif.hidden = bUnhide ? 0 : 1;

      SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp  ( pApp, true);

      RepopulateGames = true;
    }

    if (pApp->IsUserAdded ( ))
    {
      if (SKIF_ImGui_MenuItemEx2 (L_("Remove"), ICON_FA_TRASH, ImColor(200, 200, 200, 255)))
        RemoveGamePopup = PopupState_Open;
    }

    ImGui::EndMenu ( );
  }
}


// Game context menu: "Developer" submenu, developer mode only (moved verbatim from DrawGameContextMenu)
static void
DrawGameContextMenu_Developer (app_record_s* pApp)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  if (_registry.bDeveloperMode)
  {
    ImGui::Separator ( );

    if (SKIF_ImGui_BeginMenuEx2 (L_("Developer"), ICON_FA_TOOLBOX, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning)))
    {
      if (SKIF_ImGui_BeginMenuEx2 ("Application", ICON_FA_FILE_LINES))
      {
        ImGui::PushID         ("#General");
        ImGui::TextDisabled   ("General");
        if (ImGui::MenuItem   ("Name",                    pApp->names.original.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->names.original));
        if (ImGui::MenuItem   ("Name (clean)",            pApp->names.clean.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->names.clean));
        if (ImGui::MenuItem   ("Pseudo ID",   std::to_string (pApp->id).c_str()))
            SKIF_Util_SetClipboardData (      std::to_wstring(pApp->id));
        if (ImGui::MenuItem   ("Store",                       pApp->store_utf8.c_str()))
            SKIF_Util_SetClipboardData (    SK_UTF8ToWideChar(pApp->store_utf8));
        if (ImGui::MenuItem   ("Install Directory", SK_WideCharToUTF8(pApp->install_dir).c_str()))
            SKIF_Util_SetClipboardData (                              pApp->install_dir);
        ImGui::PopID          ( );

        ImGui::Separator    ( );

        ImGui::PushID       ("#SKIF");
        ImGui::TextDisabled ("Custom Data");

        if (! pApp->skif.name.empty ())
        {
          if (ImGui::MenuItem ("Name",                    pApp->skif.name.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->skif.name));
        }

        if (ImGui::MenuItem ("Category",                  pApp->skif.category.c_str()))
          SKIF_Util_SetClipboardData   (SK_UTF8ToWideChar(pApp->skif.category));
        if (ImGui::MenuItem ("Uses",               std::to_string (pApp->skif.uses).c_str()))
          SKIF_Util_SetClipboardData   (           std::to_wstring(pApp->skif.uses));
        SKIF_ImGui_SetHoverTip ("The number of times this game has been launched.");
        if (ImGui::MenuItem ("Last Used",                          pApp->skif.used_formatted.c_str()))
          SKIF_Util_SetClipboardData   (         SK_UTF8ToWideChar(pApp->skif.used));
        if (ImGui::MenuItem ("CPU Architecture", std::to_string (pApp->skif.cpu_type).c_str()))
          SKIF_Util_SetClipboardData (           std::to_wstring(pApp->skif.cpu_type));
        if (ImGui::MenuItem ("Favorited",        std::to_string (pApp->skif.pinned).c_str()))
          SKIF_Util_SetClipboardData   (         std::to_wstring(pApp->skif.pinned));
        if (ImGui::MenuItem ("Hidden",           std::to_string (pApp->skif.hidden).c_str()))
          SKIF_Util_SetClipboardData   (         std::to_wstring(pApp->skif.hidden));

        ImGui::PopID        ( );

        // Store-specific data
        auto storeFields = pApp->GetStoreDebugFields ( );

        if (! storeFields.empty ( ))
        {
          ImGui::Separator ( );

          ImGui::PushID       (("#"   + pApp->store_utf8).c_str ( ));
          ImGui::TextDisabled ((pApp->store_utf8 + " Data").c_str ( ));
          for (auto& field : storeFields)
          {
            if (ImGui::MenuItem (field.first.c_str ( ), field.second.c_str ( )))
              SKIF_Util_SetClipboardData (SK_UTF8ToWideChar (field.second));
          }
          ImGui::PopID        ( );
        }

        ImGui::EndMenu ();
      }

      // Store-specific developer data (e.g. Steam Web API)
      pApp->DrawDeveloperDataMenus ( );

      ImGui::Separator ( );

      if (! pApp->branches.empty ())
      {
        if (SKIF_ImGui_BeginMenuEx2 (SKIF_Util_FormatStringRaw ("Branches (%i)", pApp->ui.branches.size()), ICON_FA_CODE_BRANCH, ImColor(255, 207, 72)))
        {

          for ( auto& it : pApp->ui.branches)
          {
            auto& branch_name = it.second.first;
            auto& branch      = it.second.second;

            ImGui::PushStyleColor (
              ImGuiCol_Text, branch.pwd_required ?
                                ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
                                                  :
                                ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
            );

            bool bExpand =

              ImGui::BeginMenu (branch_name.c_str ());

            ImGui::PopStyleColor ();

            if (bExpand)
            {
              if (ImGui::MenuItem ("Name", branch_name.c_str()))
                SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(branch_name));

              if (! branch.description.empty ())
              {
                if (ImGui::MenuItem ("Description", branch.getDescriptionUTF8().c_str ()))
                  SKIF_Util_SetClipboardData (branch.getDescription());
              }

              if (ImGui::MenuItem ("App build #", std::to_string(branch.build_id).c_str ()))
                SKIF_Util_SetClipboardData (std::to_wstring(branch.build_id));

              if (branch.time_updated > 0)
              {
                if (ImGui::MenuItem ("Last update", branch.getTimeUTF8().c_str()))
                  SKIF_Util_SetClipboardData (branch.getTime());
              }

              if (ImGui::MenuItem ("Accessibility", branch.pwd_required ? "Private (password required)" : "Public"))
                SKIF_Util_SetClipboardData (branch.pwd_required ? L"Private (password required)" : L"Public");

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }

        ImGui::Separator ( );
      }

      if (! pApp->launch_configs.empty ())
      {
        if (SKIF_ImGui_BeginMenuEx2 (SKIF_Util_FormatStringRaw ("Launches (%i)", pApp->launch_configs.size()), ICON_FA_FLASK, ImColor(255, 105, 180)))
        {
          bool sepCustomSKIF = true,
               sepCustomUser = true;

          for ( auto& it : pApp->launch_configs )
          {
            auto& launch = it.second;

            // Separators between official / user / SKIF launch configs
            if (launch.custom_user && sepCustomUser)
            {
              sepCustomUser = false;
              ImGui::Separator ( );
            }

            else if (launch.custom_skif && sepCustomSKIF)
            {
              sepCustomSKIF = false;
              ImGui::Separator ( );
            }

            char        szButtonLabel [256] = { };

            sprintf_s ( szButtonLabel, 255,
                          "%s###LaunchConfig-%d",
                            launch.getDescriptionUTF8().empty()
                              ? launch.getExecutableFileNameUTF8().c_str ()
                              : launch.getDescriptionUTF8().c_str (),
                            launch.id);

            ImGui::PushStyleColor (
              ImGuiCol_Text, ! launch.branches.empty() ? ! launch.requires_dlc.empty()
                                   ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f) // DLC required
                                   : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.5f) // Beta key required
                                : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) // Public
            );

            bool bExpand =
              ImGui::BeginMenu (szButtonLabel);

            ImGui::PopStyleColor ();

            if (bExpand)
            {
              if (ImGui::MenuItem ("ID", std::to_string(launch.id).c_str()))
                  SKIF_Util_SetClipboardData (std::to_wstring(launch.id));

              if (! launch.getExecutableFileNameUTF8().empty())
              {
                if (ImGui::MenuItem ("Executable", launch.getExecutableFileNameUTF8().c_str()))
                  SKIF_Util_SetClipboardData (launch.getExecutableFileName());
              }

              if (! launch.getLaunchOptionsUTF8().empty())
              {
                if (ImGui::MenuItem ("Arguments", launch.getLaunchOptionsUTF8().c_str()))
                  SKIF_Util_SetClipboardData (launch.getLaunchOptions());
              }

              if (! launch.getWorkingDirectoryUTF8().empty())
              {
                if (ImGui::MenuItem ("Working Directory", launch.getWorkingDirectoryUTF8().c_str()))
                  SKIF_Util_SetClipboardData (launch.getWorkingDirectory());
              }

              if (! launch.getExecutableFullPathUTF8().empty())
              {
                if (ImGui::MenuItem ("Executable Path", launch.getExecutableFullPathUTF8().c_str()))
                  SKIF_Util_SetClipboardData (launch.getExecutableFullPath());
              }

              if (ImGui::MenuItem ("Type", std::to_string((int)launch.type).c_str()))
                SKIF_Util_SetClipboardData (std::to_wstring((int)launch.type));

              if (ImGui::MenuItem ("Operating System", std::to_string((int)launch.platforms).c_str()))
                SKIF_Util_SetClipboardData (std::to_wstring((int)launch.platforms));

              if (ImGui::MenuItem ("CPU Architecture", std::to_string((int)launch.cpu_type).c_str()))
                SKIF_Util_SetClipboardData (std::to_wstring((int)launch.cpu_type));

              if (! launch.requires_dlc.empty())
              {
                if (ImGui::MenuItem ("Requires DLC", launch.requires_dlc.c_str()))
                  SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(launch.requires_dlc));
              }

              if (! launch.branches.empty())
              {
                if (ImGui::MenuItem ("Requires Branch", launch.branches_joined.c_str()))
                  SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(launch.branches_joined));
              }

              // Store-specific launch config data (e.g. Steam launch ID, Xbox application ID)
              pApp->DrawDeveloperLaunchConfigItems (launch);

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }

        ImGui::Separator ( );
      }


      // The game's asset folder: SKIF's custom / downloaded covers and icons, and the grid view's thumbnail cache
      std::wstring assetDir = pApp->GetAssetDirectory ( );

      if (SKIF_ImGui_MenuItemEx2 (L_("Asset folder"), ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72), 0, nullptr, ! assetDir.empty ( )))
      {
        std::error_code ec;
        std::filesystem::create_directories (assetDir, ec); // A game without any artwork yet has none

        SKIF_Util_ExplorePath (assetDir);
      }

      else if (! assetDir.empty ( ))
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (assetDir));
      }

      // Store-specific developer actions (e.g. Xbox: Open Terminal)
      pApp->DrawDeveloperActions ( );

      // Epic and Xbox platforms use fake app IDs hashed from their unique text-based platform identifier
      if (SKIF_ImGui_MenuItemEx2 (L_("Copy ID"), ICON_FA_FINGERPRINT))
      {
        SKIF_Util_SetClipboardData (SK_UTF8ToWideChar (pApp->GetPersistentKey ( )));
      }

      SKIF_ImGui_SetHoverText (
        pApp->GetPersistentKey ( )
      );

      ImGui::EndMenu ();
    }
  }
}


void
DrawGameContextMenu (app_record_s* pApp)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  app_record_s::launch_config_s*
    firstLaunchConfig = (pApp != nullptr) ? pApp->GetDefaultLaunchConfig ( ) : nullptr; // The default launch config (Properties > General > Play), else the primary one

  bool playDisabled = (pApp->_status.running || pApp->IsUpdateBlockingLaunch ( ));

  // Push styling for Disabled
  ImGui::PushStyleColor      (ImGuiCol_TextDisabled,
    ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)  * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
  );

  SKIF_ImGui_PushDisableState (playDisabled);
  // Same as the Play button: the default launch option, started as set in Properties (General > Play)
  if (ImGui::MenuItemEx (L_("Play###GameContextMenu_Launch"), ICON_FA_GAMEPAD))
    launchGame = true;
  SKIF_ImGui_PopDisableState  (playDisabled);

  // The command line of the launch config Play uses (the default one)
  const std::string launchCommand = SKIF_Lib_GetLaunchCommand (pApp, firstLaunchConfig);

  SKIF_ImGui_SetHoverText (launchCommand);

  const bool offerWithoutSK = (SKIF_Lib_GetWithoutSKState (pApp, firstLaunchConfig) == SKIF_Lib_WithoutSK::Available);

  // How it starts (as the Play button's tooltip), and whether it starts the injection service as well
  if (ImGui::IsItemHovered ( ))
  {
    std::string tip = SKIF_Lib_GetPlayDescription (pApp, firstLaunchConfig);

    if (offerWithoutSK && ! _inject.bCurrentState)
      tip += ((tip.empty ( )) ? "" : "\n\n") + std::string (_("Starts the injection service as well."));

    SKIF_ImGui_SetHoverTip (tip);
  }

  if (offerWithoutSK)
  {

    ImGui::PushStyleColor      (ImGuiCol_Text,
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
    );

    if (playDisabled)
      ImGui::BeginDisabled ( );

    if (ImGui::MenuItem (LI_("\xe2\x94\x94", "without Special K###GameContextMenu_LaunchWoSK")))
      launchGame = launchWithoutSK = true;

    if (playDisabled)
      ImGui::EndDisabled ( );

    ImGui::PopStyleColor   ( );

    SKIF_ImGui_SetHoverText (launchCommand);

    if (_inject.bCurrentState)
      SKIF_ImGui_SetHoverTip (_("Stops the injection service as well."));
  }

  // A specific launch option, through the platform client or directly
  DrawGameContextMenu_PlayWith (pApp, playDisabled);

  // Items for a running game
  DrawGameContextMenu_Running (pApp);

  // Pop styling for Disabled
  ImGui::PopStyleColor   ( );

  ImGui::Separator ( );
  // ==============================

  // Browse submenu
  DrawGameContextMenu_Browse (pApp);

  // Manage submenu
  DrawGameContextMenu_Manage (pApp);

  // Store-specific submenu (e.g. Steam)
  pApp->DrawStoreMenu ( );

  if (SKIF_ImGui_BeginMenuEx2 (L_("Websites"), ICON_FA_SHARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
  {
    // Store-specific websites (e.g. Steam Community, SteamDB, GOG Database)
    pApp->DrawWebsiteMenuItems ( );

    if (SKIF_ImGui_MenuItemEx2 ("PCGamingWiki", ICON_FA_SCREWDRIVER_WRENCH, ImColor(200, 200, 200, 255)))
      SKIF_Util_OpenURI (SK_UTF8ToWideChar (pApp->ui.pcgw));

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (pApp->ui.pcgw);

    ImGui::EndMenu ( );
  }

  // Developer submenu (developer mode only)
  DrawGameContextMenu_Developer (pApp);
}

void
DrawSpecialKContextMenu (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (! _inject.bCurrentState)
  {
    if (SKIF_ImGui_MenuItemEx2 (L_("Start service"), ICON_FA_TOGGLE_ON,  ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)))
      _inject._StartStopInject (_inject.bCurrentState, true);

    if (SKIF_ImGui_MenuItemEx2 (L_("Start service (manual stop)"), ICON_FA_TOGGLE_ON,  ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)))
      _inject._StartStopInject (_inject.bCurrentState, false);
  }

  else {
    if (SKIF_ImGui_MenuItemEx2 (L_("Stop service"), ICON_FA_TOGGLE_OFF, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning)))
      _inject._StartStopInject (_inject.bCurrentState);
  }

  ImGui::Separator ( ); // ==============================

  if (SKIF_ImGui_BeginMenuEx2 (L_("Browse"), ICON_FA_FOLDER, ImColor(255, 207, 72)))
  {
    if (SKIF_ImGui_MenuItemEx2 (L_("Install folder"), ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
      SKIF_Util_ExplorePath (pApp->install_dir);

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->install_dir));

    if (SKIF_ImGui_MenuItemEx2 (L_("Profile folders"), ICON_FA_FOLDER_OPEN, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
      SKIF_Util_ExplorePath (pApp->specialk.profile_dir);

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText (pApp->specialk.profile_dir_utf8);

    ImGui::EndMenu ( );
  }

  ImGui::Separator ( ); // ==============================

  if (SKIF_ImGui_MenuItemEx2 (L_("Wiki"), ICON_FA_BOOK_BOOKMARK, ImColor(25, 118, 210)))
    SKIF_Util_OpenURI (L"https://wiki.special-k.info/");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/");

  if (SKIF_ImGui_MenuItemEx2 ("Discord", ICON_FA_DISCORD, ImColor(114, 137, 218)))
    SKIF_Util_OpenURI (L"https://discord.special-k.info");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://discord.special-k.info");

  if (SKIF_ImGui_MenuItemEx2 ("Patreon", ICON_FA_PATREON, ImColor(249, 104, 84)))
    SKIF_Util_OpenURI (L"https://www.patreon.com/Kaldaien");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://www.patreon.com/Kaldaien");

  if (SKIF_ImGui_MenuItemEx2 ("GitHub", ICON_FA_GITHUB, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255))) // ImColor (226, 67, 40)
    SKIF_Util_OpenURI (L"https://github.com/SpecialKO");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://github.com/SpecialKO");

  ImGui::Separator ( ); // ==============================

  ImGui::PushStyleColor ( ImGuiCol_Text,
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
  );

  if (SKIF_ImGui_MenuItemEx2 ("PCGamingWiki", ICON_FA_SCREWDRIVER_WRENCH, ImColor(200, 200, 200, 255)))
    SKIF_Util_OpenURI (SK_UTF8ToWideChar (pApp->ui.pcgw));

  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverText       (pApp->ui.pcgw);

  if (SKIF_STEAM_OWNER)
  {
    if (SKIF_ImGui_MenuItemEx2 ("Steam", ICON_FA_STEAM_SYMBOL, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      SKIF_Util_OpenURI (L"steam://nav/games/details/" + std::to_wstring (pApp->id));

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://nav/games/details/%lu", pApp->id));
  }

  ImGui::PopStyleColor ( );

  ImGui::Separator ( ); // ==============================

  constexpr char* labelPin   =   NL_("Favorite");
  constexpr char* labelUnpin = NL_("Unfavorite");

  bool isFavorite = (pApp->skif.pinned > 0);
  if (SKIF_ImGui_MenuItemEx2 (TL_(isFavorite ? labelUnpin : labelPin), (isFavorite ? ICON_FA_HEART_CRACK : ICON_FA_HEART), ImColor(245, 66, 66, 255)))
  {
    // Special K's default (99) sorts it first among the Favorites
    pApp->skif.pinned =  (isFavorite ? 0 : 99);

    SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp  ( pApp, true);

    SKIF_GamingCollection::SortApps (&_games.GetApps());
    sort_changed = true;
  }
}

#pragma endregion

// Change / Reset (Clear, Refresh) of a game's cover: the cover menu, and the game menu of the grid view.
//   isCustom / isManaged: whether its cover is the user's own, or one SKIF manages (both enable Reset)
void
DrawCoverMenuChangeReset (app_record_s* pApp, bool isCustom, bool isManaged)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static bool&                update       = GetLibraryTab ( ).update;

  // Preparations

  bool resetVisible = (pApp->HasStoreCover ( )   || // Not for Special K or custom games, unless they have custom / managed covers
                       isCustom ||
                       isManaged);

  if (SKIF_ImGui_MenuItemEx2 (L_("Change"), ICON_FA_FILE_IMAGE, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
  {
    LPWSTR pwszFilePath = NULL;
    HRESULT hr          =
      SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, SKIF_ImGui_hWnd, { { W_("Images"), L"*.png;*.apng;*.gif;*.jpg;*.jpeg;*.webp;*.psd;*.bmp" } }, FOS_FILEMUSTEXIST, FOLDERID_Pictures);

    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
      // If cancelled, do nothing
    }

    else if (SUCCEEDED(hr))
    {
      std::wstring filePath = std::wstring(pwszFilePath);
      if (SaveGameCover (pApp, filePath))
      {
        // Shows the "..." loading dots, and sets up the current cover to be released
        _art.BeginSavingCover (pApp);
      }
    }
  }

  else
    SKIF_ImGui_SetMouseCursorHand ( );

  if (resetVisible)
  {
    constexpr char* textCustom1 = NL_("Clear");
    constexpr char* textCustom2 = NL_("Reset");
    constexpr char* textDefault = NL_("Refresh");
    char*           textPtr     = textDefault;
    constexpr char* iconCustom  = ICON_FA_ROTATE_LEFT;
    constexpr char* iconDefault = ICON_FA_ROTATE;
    char*           iconPtr     = iconDefault;
    ImColor         iconColor   = (_registry._StyleLightMode) ? ImColor (128, 128, 128) : ImColor (128, 128, 128); // If neither custom nor managed (aka disabled)

    if (isCustom)
    {
      iconColor = (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255);
      iconPtr   = iconCustom;
      textPtr   = ((pApp->IsUserAdded ( )) ? textCustom1 : textCustom2);
    }

    else if (isManaged)
    {
      iconColor = (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255);
    }

    bool enabled = (isCustom || isManaged);
    bool doReset = SKIF_ImGui_MenuItemEx2 (TL_(textPtr), iconPtr, iconColor, 0, nullptr, enabled);

    if (! enabled)
      SKIF_ImGui_SetHoverTip (_("Managed by the platform client."), false, true);

    else
      SKIF_ImGui_SetMouseCursorHand ( );

    if (doReset)
    {
      std::wstring targetPath = L"";

      targetPath = pApp->GetAssetDirectory ( );

      if (PathFileExists (targetPath.c_str()))
      {
        std::wstring fileName = (isCustom) ? L"cover" : L"cover-original";

        // Will fail on read-only marked files
        bool d1 = DeleteFile ((targetPath + fileName + L".png").c_str()),
             d2 = DeleteFile ((targetPath + fileName + L".jpg").c_str()),
             d3 = false;

        // Custom covers can also be GIFs and animated WebPs (SaveGameCover)
        if (isCustom && DeleteFile ((targetPath + fileName + L".gif").c_str()))
          d2 = true;

        if (isCustom && DeleteFile ((targetPath + fileName + L".webp").c_str()))
          d2 = true;

        // Store-managed fallback covers (PCGamingWiki covers for GOG and Steam, Xbox fallback cover)
        if (! isCustom)
          d3 = pApp->DeleteStoreCoverFiles (targetPath);

        // The preferred IGDB cover (Settings), downloaded again on the reload
        bool d4 = (! isCustom && DeleteFile ((targetPath + L"cover-igdb.jpg").c_str()));

        // If any file was removed
        if (d1 || d2 || d3 || d4)
        {
          update    = true;
          _art.ForgetCover ( ); // Needed as otherwise SKIF would not reload the cover
          _art.InvalidateThumbnail (pApp); // The grid view's thumbnail
        }
      }
    }
  }

}

// Context menu for the selected game's cover (moved verbatim from SKIF_UI_Tab_DrawLibrary)
void
DrawCoverMenu (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static app_record_s*&       pApp         = GetLibraryTab ( ).pApp;
  static bool&                update       = GetLibraryTab ( ).update;

  bool isSpecialK = (pApp != nullptr && pApp->IsSpecialK ( )); // pApp only changes once per frame, at the top of SKIF_UI_Tab_DrawLibrary ( )

#pragma region CoverMenu

  // Open the CoverMenu
  if (CoverMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("CoverMenu");
    CoverMenu = PopupState_Closed;
  }


  if (pApp != nullptr)
  {
    if (ImGui::BeginPopup   ("CoverMenu", ImGuiWindowFlags_NoMove))
    {
      ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

      DrawCoverMenuChangeReset (pApp, pApp->tex_cover.isCustom, pApp->tex_cover.isManaged);

      ImGui::Separator ( );

      // Image scaling
      ImGui::PushID ("#ImageScaling");

      if (SKIF_ImGui_BeginMenuEx2 (L_("Scaling"), ICON_FA_PANORAMA))
      {
        static bool bDefault = (_registry.iCoverScaling == 0) ? true : false;
        static bool bFill    = (_registry.iCoverScaling == 1) ? true : false;
        static bool bFit     = (_registry.iCoverScaling == 2) ? true : false;
        static bool bNone    = (_registry.iCoverScaling == 3) ? true : false;
        static bool bStretch = (_registry.iCoverScaling == 4) ? true : false;

        if (ImGui::MenuItem (CL_("cover scaling", "Default"), spaces,  &bDefault, ! bDefault))
        {
          _registry.iCoverScaling = 0;

        //bDefault = false;
          bFill    = false;
          bFit     = false;
          bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        SKIF_ImGui_SetHoverTip (_("Scales the cover to a maximum height of 600px"), false, true);

        if (ImGui::MenuItem (CL_("cover scaling", "Fill"),  spaces, &bFill, ! bFill))
        {
          _registry.iCoverScaling = 1;

          bDefault = false;
        //bFill    = false;
          bFit     = false;
          bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        if (ImGui::MenuItem (CL_("cover scaling", "Fit"),  spaces, &bFit, ! bFit))
        {
          _registry.iCoverScaling = 2;

          bDefault = false;
          bFill    = false;
        //bFit     = false;
          bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        if (ImGui::MenuItem (CL_("cover scaling", "None"),  spaces, &bNone, ! bNone))
        {
          _registry.iCoverScaling = 3;

          bDefault = false;
          bFill    = false;
          bFit     = false;
        //bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        /*
        if (ImGui::MenuItem ("Stretch",  spaces, &bStretch, ! bStretch))
        {
          _registry.iCoverScaling = 4;

          bDefault = false;
          bFill    = false;
          bFit     = false;
          bNone    = false;
        //bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }
        */

        ImGui::EndMenu ( );
      }

      if (isSpecialK)
      {
        if (SKIF_ImGui_MenuItemEx2 ("Patreon", ICON_FA_PATREON, ImColor (249, 104, 84), spaces, &_registry.bPatreon))
          _registry.regKVPatreon.putData (_registry.bPatreon);
      }

      ImGui::PopID ( );

      ImGui::Separator ( );

      if (SKIF_ImGui_MenuItemEx2 (L_("Open SteamGridDB"), ICON_FA_UP_RIGHT_FROM_SQUARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
        SKIF_Util_OpenURI (SK_UTF8ToWideChar (pApp->ui.sgdbGrids));
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (pApp->ui.sgdbGrids);
      }

      ImGui::PopStyleColor ( );
      ImGui::EndPopup      ( );
    }
  }

#pragma endregion
}

// Context menu for the selected game's icon (moved verbatim from SKIF_UI_Tab_DrawLibrary)
void
DrawIconMenu (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static app_record_s*&       pApp         = GetLibraryTab ( ).pApp;

#pragma region GamesList::IconMenu

  // Open the IconMenu
  if (IconMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("IconMenu");
    IconMenu = PopupState_Closed;
  }

  if (pApp != nullptr)
  {
    if (ImGui::BeginPopup   ("IconMenu", ImGuiWindowFlags_NoMove))
    {
      ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

      // Preparations

      bool resetVisible = (! pApp->IsSpecialK ( )    ||
                           pApp->tex_icon.isCustom ||
                           pApp->tex_icon.isManaged);

      if (SKIF_ImGui_MenuItemEx2 (L_("Change"), ICON_FA_FILE_IMAGE, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      {
        LPWSTR pwszFilePath = NULL;
        HRESULT hr          =
          SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, SKIF_ImGui_hWnd, { { W_("Icons"), L"*.exe;*.ico;*.png;*.jpg;*.jpeg" } }, FOS_FILEMUSTEXIST, FOLDERID_Pictures);

        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
          // If cancelled, do nothing
        }

        else if (SUCCEEDED(hr))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = SKIF_Util_ToLowerW (std::filesystem::path(pwszFilePath).extension().wstring());

          targetPath = pApp->GetAssetDirectory ( );

          if (targetPath != L"")
          {
            std::error_code ec;
            // Create any missing directories
            if (! std::filesystem::exists (            targetPath, ec))
                  std::filesystem::create_directories (targetPath, ec);

            targetPath += L"icon";

            DeleteFile ((targetPath + L".png").c_str());
            DeleteFile ((targetPath + L".jpg").c_str());
            DeleteFile ((targetPath + L".ico").c_str());

            if (ext == L".jpeg")
              ext = L".jpg";

            if (ext == L".exe")
              SKIF_Util_SaveExtractExeIcon (pwszFilePath, (targetPath + L".png"));
            else
              CopyFile (pwszFilePath, (targetPath + ext).c_str(), false);

            SetFileAttributes ((targetPath + ext).c_str(),
                    GetFileAttributes ((targetPath + ext).c_str()) & ~FILE_ATTRIBUTE_READONLY);

            // Reload the icon
            _art.ReloadIcon (pApp);
          }
        }
      }

      else
        SKIF_ImGui_SetMouseCursorHand ( );

      if (resetVisible)
      {
        constexpr char* textCustom  = NL_("Reset");
        constexpr char* textDefault = NL_("Refresh");
        char*           textPtr     = textDefault;
        constexpr char* iconCustom  = ICON_FA_ROTATE_LEFT;
        constexpr char* iconDefault = ICON_FA_ROTATE;
        char*           iconPtr     = iconDefault;
        ImColor         iconColor   = (_registry._StyleLightMode) ? ImColor (128, 128, 128) : ImColor (128, 128, 128); // If neither custom nor managed (aka disabled)

        if (pApp->tex_icon.isCustom)
        {
          iconColor = (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255);
          iconPtr   = iconCustom;
          textPtr   = textCustom;
        }

        else if (pApp->tex_icon.isManaged)
        {
          iconColor = (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255);
        }

        bool enabled = (pApp->tex_icon.isCustom || pApp->tex_icon.isManaged);
        bool doReset = SKIF_ImGui_MenuItemEx2 (TL_(textPtr), iconPtr, iconColor, 0, nullptr, enabled);

        if (! enabled)
          SKIF_ImGui_SetHoverTip (_("Managed by the platform client."), false, true);

        else
          SKIF_ImGui_SetMouseCursorHand ( );

        if (doReset)
        {
          std::wstring targetPath = L"";

          targetPath = pApp->GetAssetDirectory ( );

          if (PathFileExists (targetPath.c_str()))
          {
            std::wstring fileName = (pApp->tex_icon.isCustom) ? L"icon" : L"icon-original";

            // Will fail on read-only marked files
            bool d1 = DeleteFile ((targetPath + fileName + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + fileName + L".jpg").c_str()),
                 d3 = DeleteFile ((targetPath + fileName + L".ico").c_str());

            // If any file was removed
            if (d1 || d2 || d3)
            {
              // Store-specific (Steam: refresh the icon hash from appinfo.vdf)
              pApp->PrepareIconSource ( );

              // Reload the icon
              _art.ReloadIcon (pApp);
            }
          }
        }
      }

      ImGui::Separator ( );

      if (SKIF_ImGui_MenuItemEx2 (L_("Open SteamGridDB"), ICON_FA_UP_RIGHT_FROM_SQUARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
        SKIF_Util_OpenURI (SK_UTF8ToWideChar (pApp->ui.sgdbIcons));
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (pApp->ui.sgdbIcons);
      }

      ImGui::Separator      ( );

      ShowLargeIconToggle   ( );

      ImGui::PopStyleColor ( );
      ImGui::EndPopup      ( );
    }
  }

#pragma endregion
}

// Context menu for the empty space in the games list (moved verbatim from SKIF_UI_Tab_DrawLibrary)
void
DrawEmptySpaceMenu (bool showClearBtn)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

#pragma region GamesList::EmptySpaceMenu

  // Open the Empty Space Menu
  if (EmptySpaceMenu == PopupState_Open)
    ImGui::OpenPopup    ("GameListEmptySpaceMenu");

  if (ImGui::BeginPopup   ("GameListEmptySpaceMenu", ImGuiWindowFlags_NoMove))
  {
    EmptySpaceMenu = PopupState_Opened;

    ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

    if (showClearBtn)
    {
      if (SKIF_ImGui_MenuItemEx2 (L_("Clear filter"), ICON_FA_SQUARE_XMARK, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure)))
        ClearCharFilter ( );

      ImGui::Separator   ( );
    }

     if (SKIF_ImGui_MenuItemEx2 (L_("Add game"), ICON_FA_SQUARE_PLUS, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)))
       AddGamePopup = PopupState_Open;

    ImGui::Separator      ( );

    ImGui::PushID ("#LibrarySort");

    if (SKIF_ImGui_BeginMenuEx2 (L_("Sort by"), ICON_FA_SORT))
    {
      static bool bName       = (_registry.iLibrarySort == 0) ? true : false;
      static bool bFrequently = (_registry.iLibrarySort == 1) ? true : false;
      static bool bRecently   = (_registry.iLibrarySort == 2) ? true : false;

      if (ImGui::MenuItem (L_("Alphabetical"), spaces,  &bName     ))
      {
        _registry.iLibrarySort = 0;

        bFrequently = false;
        bRecently   = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      if (ImGui::MenuItem (L_("Most played"),  spaces, &bFrequently))
      {
        _registry.iLibrarySort = 1;

        bName       = false;
        bRecently   = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      SKIF_ImGui_SetHoverTip (_("Sort by the number of launches of the game through this app."));

      if (ImGui::MenuItem (L_("Last played"),  spaces, &bRecently  ))
      {
        _registry.iLibrarySort = 2;

        bName       = false;
        bFrequently = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      ImGui::EndMenu ( );
    }

    ImGui::PopID ( );

    // Prevent the popup from being closed when toggling a platform
    ImGui::PushItemFlag (ImGuiItemFlags_AutoClosePopups, false);
    ImGui::PushID       ("#Platforms");

    if (SKIF_ImGui_BeginMenuEx2 (L_("Platforms"), ICON_FA_GEARS))
    {
      static bool* pbLibraryEA     = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEA;
      static bool* pbLibraryEpic   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEpic;
      static bool* pbLibraryGOG    = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryGOG;
      static bool* pbLibraryitch   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryitch;
      static bool* pbLibrarySteam  = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibrarySteam;
      static bool* pbLibraryUbisoft= (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryUbisoft;
      static bool* pbLibraryXbox   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryXbox;
      static bool* pbLibraryCustom = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryCustom;

      // Not disabled while the library is being populated (it flickered on every toggle): a toggle made meanwhile is
      //   applied by another refresh once the current one is done (see RepopulateGames in library.cpp)
      if (ImGui::MenuItem ("EA",      spaces, pbLibraryEA,     (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryEA.putData    (_registry.bLibraryEA);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Epic",    spaces, pbLibraryEpic,   (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryEpic.putData  (_registry.bLibraryEpic);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("GOG",     spaces, pbLibraryGOG,    (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryGOG.putData   (_registry.bLibraryGOG);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("itch.io", spaces, pbLibraryitch,   (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryitch.putData  (_registry.bLibraryitch);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Steam",   spaces, pbLibrarySteam,  (! _registry._LibraryHidden)))
      {
        _registry.regKVLibrarySteam.putData (_registry.bLibrarySteam);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Ubisoft", spaces, pbLibraryUbisoft,(! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryUbisoft.putData (_registry.bLibraryUbisoft);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Xbox",    spaces, pbLibraryXbox,   (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryXbox.putData  (_registry.bLibraryXbox);
        RepopulateGames = true;
      }

      ImGui::Separator ( );

      // TRANSLATORS: Games added by the user (shown among the platforms: Steam, GOG, ...)
      if (ImGui::MenuItem (CL_("platform", "Custom"),  spaces, pbLibraryCustom, (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryCustom.putData(_registry.bLibraryCustom);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem (L_("Hidden games"),spaces, &_registry._LibraryHidden))
      {
        pbLibraryEA     = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEA;
        pbLibraryEpic   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEpic;
        pbLibraryGOG    = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryGOG;
        pbLibraryitch   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryitch;
        pbLibrarySteam  = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibrarySteam;
        pbLibraryUbisoft= (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryUbisoft;
        pbLibraryXbox   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryXbox;
        pbLibraryCustom = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryCustom;

        RepopulateGames = true;
      }

      ImGui::EndMenu ( );
    }

    ImGui::PopID          ( ); // #Platforms
    ImGui::PopItemFlag    ( ); // ImGuiItemFlags_AutoClosePopups

    ImGui::Separator      ( );

    ShowLibraryViewToggles ( );

    if (SKIF_ImGui_MenuItemEx2 (L_("Refresh"), ICON_FA_ROTATE_RIGHT, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
      RepopulateGames = true;

    ImGui::PopStyleColor  ( );
    ImGui::EndPopup       ( );
  }

  else
    EmptySpaceMenu = PopupState_Closed;

#pragma endregion
}

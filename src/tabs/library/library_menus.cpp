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
      ImColor::HSV (0.11F, 1.F, 1.F),
        "Troubleshooting:"
    );

    ImGui::Separator ( );

    struct Preset
    {
      std::string  Name;
      std::wstring Path;

      Preset (std::wstring n, std::wstring p)
      {
        Name = SK_WideCharToUTF8 (n);
        Path = p;
      };
    };

    // Static stuff :D
    static std::wstring DefaultPresetsFolder = SK_FormatStringW (LR"(%ws\Global\)",        _path_cache.specialk_userdata);
    static std::wstring  CustomPresetsFolder = SK_FormatStringW (LR"(%ws\Global\Custom\)", _path_cache.specialk_userdata);
    static SKIF_DirectoryWatch SKIF_GlobalWatch;
    static SKIF_DirectoryWatch SKIF_CustomWatch;
    static std::vector<Preset> DefaultPresets;
    static std::vector<Preset> DefaultPresetsMissing;
    static std::vector<Preset>  CustomPresets;
    static bool runOnceDefaultPresets = true;
    static bool runOnceCustomPresets  = true;

    // Shared function
    auto _FindPresets = [](std::wstring folder, std::wstring find_pattern) -> std::vector<Preset>
    {
      HANDLE hFind        = INVALID_HANDLE_VALUE;
      WIN32_FIND_DATA ffd = { };;
      std::vector<Preset> tmpPresets;

      hFind =
        FindFirstFileExW ((folder + find_pattern).c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

      if (INVALID_HANDLE_VALUE != hFind)
      {
        do {
          Preset newPreset = { PathFindFileName (ffd.cFileName), folder + ffd.cFileName };
          bool         add = false;
          LONG_PTR   size = 0;

          if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
          {
            struct _stat64 buffer;
            if (0 == _wstat64 (newPreset.Path.c_str(), &buffer))
              size = static_cast<LONG_PTR>(buffer.st_size);
          }

          else
            size = static_cast<LONG_PTR>(((ffd.nFileSizeHigh) * (MAXDWORD + 1)) + ffd.nFileSizeLow);

          // All files larger than 4 bytes should be added
          if (4 < size)
            add = true;

          // All files between 1-4 bytes should be checked for byte order marks (0 byte files are skipped automatically)
          else if (0 < size && size <= 4)
          {
            // When opening an existing file, the CreateFile function performs the following actions:
            // [...] and ignores any file attributes (FILE_ATTRIBUTE_*) specified by dwFlagsAndAttributes.
            CHandle hPreset (
              CreateFileW (newPreset.Path.c_str(),
                              GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,        OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, // GetFileAttributesW (newPreset.Path.c_str())
                                      nullptr ) );

            if (hPreset != INVALID_HANDLE_VALUE)
            {
              DWORD dwRead     = 0;

              auto szPresetData =
                std::make_unique <char []> (
                  std::size_t (size) + std::size_t (1)
                );

              auto preset_data =
                szPresetData.get ();

              if (preset_data)
              {
                const bool bRead =
                  ReadFile (hPreset, preset_data, static_cast<DWORD>(size), &dwRead, nullptr);

                if (bRead && dwRead)
                {
                  std::string string =
                    std::move (preset_data);

                  // Skip files only containing byte order marks
                  if (string != "\xEF\xBB\xBF"     && // UTF-8,  with BOM
                      string != "\xFF\xFE"         && // UTF-16, little endian
                      string != "\xFE\xFF"         && // UTF-16, big endian
                      string != "\xFF\xFE\x00\x00" && // UTF-32, little endian
                      string != "\x00\x00\xFE\xFF"  ) // UTF-32, big endian
                    add = true;
                }
              }
            }
          }

          if (add)
            tmpPresets.push_back (newPreset);

        } while (FindNextFile (hFind, &ffd));

        FindClose (hFind);
      }

      return tmpPresets;
    };

    // Directory watches -- updates the vectors automatically
    if (SKIF_CustomWatch.isSignaled (CustomPresetsFolder)  || runOnceCustomPresets)
    {
      runOnceCustomPresets  = false;
      CustomPresets         = _FindPresets (CustomPresetsFolder,  L"*.ini");
    }

    if (SKIF_GlobalWatch.isSignaled (DefaultPresetsFolder) || runOnceDefaultPresets)
    {
      runOnceDefaultPresets = false;
      DefaultPresets        = _FindPresets (DefaultPresetsFolder, L"default_*.ini");
      DefaultPresetsMissing.clear();

      static std::vector<Preset> default = {
        Preset( L"default_SpecialK.ini", (DefaultPresetsFolder + L"default_SpecialK.ini") ),
        Preset( L"default_OpenGL32.ini", (DefaultPresetsFolder + L"default_OpenGL32.ini") ),
        Preset( L"default_dinput8.ini",  (DefaultPresetsFolder + L"default_dinput8.ini")  ),
        Preset( L"default_dxgi.ini",     (DefaultPresetsFolder + L"default_dxgi.ini")     ),
      //Preset( L"default_d3d12.ini",    (DefaultPresetsFolder + L"default_d3d12.ini")    ), // Apparently Special K doesn't support being loaded as D3D12.dll
        Preset( L"default_d3d11.ini",    (DefaultPresetsFolder + L"default_d3d11.ini")    ),
        Preset( L"default_d3d9.ini",     (DefaultPresetsFolder + L"default_d3d9.ini")     ),
        Preset( L"default_d3d8.ini",     (DefaultPresetsFolder + L"default_d3d8.ini")     ),
        Preset( L"default_ddraw.ini",    (DefaultPresetsFolder + L"default_ddraw.ini")    ),
      };

      for (auto& preset : default)
      {
        auto it = std::find_if (DefaultPresets.begin(), DefaultPresets.end(),
          [&preset](const Preset& p) -> bool
          {
            return (_stricmp (p.Name.c_str(), preset.Name.c_str()) == 0);
          });

        if (it == DefaultPresets.end())
          DefaultPresetsMissing.push_back (preset);
      }
    }

    if (ImGui::Selectable ("External Editor"))
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

    if (ImGui::BeginMenu("Apply Preset"))
    {
      // Custom Presets
      if (! CustomPresets.empty())
      {
        for (auto& preset : CustomPresets)
        {
          ImGui::PushID (preset.Name.c_str());

          if (ImGui::Selectable (preset.Name.c_str()))
          {
            CopyFile (preset.Path.c_str(), pApp->specialk.injection.config.full_path.c_str(), FALSE);
            PLOG_VERBOSE << "Copying " << preset.Path << " over to " << pApp->specialk.injection.config.full_path << ", overwriting any existing file in the process.";
          }

          SKIF_ImGui_SetMouseCursorHand ( );

          ImGuiID editMenu = ImGui::GetID ("###editMenu");

          if (! ImGui::IsPopupOpen   (editMenu, ImGuiPopupFlags_AnyPopupLevel) &&
                ImGui::IsItemClicked (ImGuiMouseButton_Right))
            ImGui::OpenPopup    (editMenu);

          if (ImGui::BeginPopupEx (editMenu, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
          {
            if (ImGui::Selectable ("Edit"))
              SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, preset.Path, "");

            ImGui::EndPopup ( );
          }

          ImGui::PopID ( );
        }

        if (! DefaultPresets.empty())
          ImGui::Separator ( );
      }

      // Default Presets
      if (! DefaultPresets.empty())
      {
        for (auto& preset : DefaultPresets)
        {
          ImGui::PushID (preset.Name.c_str());

          if (ImGui::Selectable (preset.Name.c_str()))
          {
            CopyFile (preset.Path.c_str(), pApp->specialk.injection.config.full_path.c_str(), FALSE);
            PLOG_VERBOSE << "Copying " << preset.Path << " over to " << pApp->specialk.injection.config.full_path << ", overwriting any existing file in the process.";
          }

          SKIF_ImGui_SetMouseCursorHand ();

          ImGuiID editMenu = ImGui::GetID ("###editMenu");

          if (! ImGui::IsPopupOpen   (editMenu, ImGuiPopupFlags_AnyPopupLevel) &&
                ImGui::IsItemClicked (ImGuiMouseButton_Right))
            ImGui::OpenPopup    (editMenu);

          if (ImGui::BeginPopupEx (editMenu, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
          {
            if (ImGui::Selectable ("Edit"))
              SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, preset.Path, "");

            ImGui::EndPopup ( );
          }

          ImGui::PopID ( );
        }
      }

      if (! DefaultPresets.empty() || ! CustomPresets.empty())
        ImGui::Separator ( );

      if (ImGui::BeginMenu ("Create New"))
      {
        if (ImGui::Selectable ("Custom Preset..."))
        {
          std::wstring path = (CustomPresetsFolder + LR"(Untitled.ini)");
          std::error_code ec;
          // Create any missing directories
          if (! std::filesystem::exists             (CustomPresetsFolder, ec))
                std::filesystem::create_directories (CustomPresetsFolder, ec);

          std::wofstream config_file (path.c_str(), std::wofstream::out | std::wofstream::trunc);

          if (config_file.is_open())
          {
            config_file << utf8_bom;
            config_file.close ( );
          }

          SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, path, "");
        }

        SKIF_ImGui_SetMouseCursorHand ( );

        if (! DefaultPresetsMissing.empty())
          ImGui::Separator ( );

        for (auto& preset : DefaultPresetsMissing)
        {
          if (ImGui::Selectable (preset.Name.c_str()))
          {
            // If the file does not exist, create it
            if (! PathFileExists (preset.Path.c_str()))
            {
              std::error_code ec;
              // Create any missing directories
              if (! std::filesystem::exists             (DefaultPresetsFolder, ec))
                    std::filesystem::create_directories (DefaultPresetsFolder, ec);

              std::wofstream config_file (preset.Path.c_str(), std::wofstream::out | std::wofstream::trunc);

              if (config_file.is_open())
              {
                config_file << utf8_bom;
                config_file.close ( );
              }
            }

            SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, preset.Path, "");
          }

          SKIF_ImGui_SetMouseCursorHand ( );
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenu ( );
    }

    ImGui::Separator ( );

    if (ImGui::Selectable ("Apply Compatibility Config"))
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      std::wofstream config_file (pApp->specialk.injection.config.full_path.c_str(), std::wofstream::out | std::wofstream::trunc);

      if (config_file.is_open())
      {
        config_file << utf8_bom
                    << cfg_compatibility;
        config_file.close ( );
      }
    }

    SKIF_ImGui_SetMouseCursorHand ();

    SKIF_ImGui_SetHoverTip ("Known as the \"sledgehammer\" config within the community as it disables\n"
                            "various features of Special K in an attempt to improve compatibility.");

    if (ImGui::Selectable ("Reset"))
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

    SKIF_ImGui_SetMouseCursorHand ();

    ImGui::PopStyleColor ( );

    ImGui::EndPopup ( );
  }
}

#pragma endregion


#pragma region DrawGameContextMenu

// Game context menu: Instant Play launch options (moved verbatim from DrawGameContextMenu)
static void
DrawGameContextMenu_InstantPlay (app_record_s* pApp, bool playDisabled, app_record_s::launch_config_s* firstLaunchConfig)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  // Instant Play options
  if (pApp->CanLaunchExecutableDirectly ( ))
  {
    if (pApp->ui.numSecondaryLaunchConfigs > 0 || pApp->isInstantPlayCompatible ( ))
      ImGui::Separator ( );

    // If there is only one valid launch config (Steam, GOG, Xbox games only)
    if (pApp->ui.numSecondaryLaunchConfigs == 0   &&
        pApp->isInstantPlayCompatible ( ))
    {
      if (playDisabled)
        ImGui::BeginDisabled ( );

      ImGui::PushStyleColor (ImGuiCol_SKIF_Icon, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure));

      if (ImGui::MenuItemEx ("Instant play###GameContextMenu_InstantPlay", ICON_FA_ROCKET))
        launchInstant = true;

      ImGui::PopStyleColor ( );

      if (playDisabled)
        ImGui::EndDisabled ( );

      std::string hoverText = (firstLaunchConfig != nullptr) ? firstLaunchConfig->getExecutableFullPathUTF8() : "";

      if (firstLaunchConfig != nullptr && ! pApp->GetClientLaunchOptions ( ).empty())
      {
        hoverText = pApp->GetClientCommandLine (firstLaunchConfig);
      }

      else {
        if (firstLaunchConfig != nullptr && ! firstLaunchConfig->getLaunchOptionsUTF8().empty())
          hoverText += (" " + firstLaunchConfig->getLaunchOptionsUTF8());

        std::string clientLaunchOptions = pApp->GetClientLaunchOptions ( );

        if (! clientLaunchOptions.empty ( ))
          hoverText += (" " + clientLaunchOptions);
      }

      SKIF_ImGui_SetHoverText (hoverText.c_str());

      SKIF_ImGui_SetHoverTip  ("Skips the regular platform launch process for the game,\n"
                                "including steps such as cloud saves synchronization.");

      if (pApp->specialk.injection.injection.type != InjectionType::Local && _inject.bHasServlet)
      {
        ImGui::PushStyleColor      (ImGuiCol_Text,
          ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
        );

        if (playDisabled)
          ImGui::BeginDisabled ( );

        if (ImGui::MenuItem ("\xe2\x94\x94 without Special K###GameContextMenu_InstantPlayWoSK"))
          launchInstant = launchWithoutSK = true;

        if (playDisabled)
          ImGui::EndDisabled ( );

        ImGui::PopStyleColor   ( );

        SKIF_ImGui_SetHoverText (hoverText.c_str());
      }
    }

    // Multiple launch configs
    else if (pApp->ui.numSecondaryLaunchConfigs > 0)
    {
      if (playDisabled)
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

      if (SKIF_ImGui_BeginMenuEx2 ("Instant play###GameContextMenu_InstantPlayMenu", ICON_FA_ROCKET, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure)))
      {
        bool sepCustomSKIF = true,
             sepCustomUser = true;

        for (auto& _launch_cfg : pApp->launch_configs)
        {
          if (! _launch_cfg.second.valid ||
                _launch_cfg.second.duplicate_exe_args)
            continue;

          // Filter out launch configs requiring not owned DLCs
          if (! _launch_cfg.second.owns_dlc)
            continue;

          auto& _launch = _launch_cfg.second;

          // Separators between official / user / SKIF launch configs
          if (_launch_cfg.second.custom_user && sepCustomUser)
          {
            sepCustomUser = false;
            ImGui::Separator ( );
          }

          else if (_launch_cfg.second.custom_skif && sepCustomSKIF)
          {
            sepCustomSKIF = false;
            ImGui::Separator ( );
          }

          bool blacklisted = (_launch.isBlacklisted ( ) || _inject._TestUserList (_launch.getExecutableFullPathUTF8 ( ).c_str (), false));

          char        szButtonLabel [256] = { };

          sprintf_s ( szButtonLabel, 255,
                        "%s%s%s###GameContextMenu_InstantPlayMenu-%d",
                                  (blacklisted) ? (ICON_FA_LOCK        "  ") : "",
                         (_launch.isElevated()) ? (ICON_FA_USER_SHIELD "  ") : "",
                          _launch.getDescriptionUTF8().empty ()
                            ? _launch.getExecutableFileNameUTF8().c_str ()
                            : _launch.getDescriptionUTF8().c_str (),
                          _launch.id);

          if (playDisabled || blacklisted)
          {
            ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
          }
          else
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

          if (ImGui::Selectable (szButtonLabel))
          {
            launchConfig = &_launch_cfg.second;
            launchInstant = true;
          }

          if (playDisabled || blacklisted)
          {
            ImGui::PopStyleColor  ( );
            ImGui::PopItemFlag    ( );
          }
          else
            ImGui::PopStyleColor  ( );

          std::string hoverText = _launch.getExecutableFullPathUTF8();

          if (! _launch_cfg.second.custom_skif &&
              ! _launch_cfg.second.custom_user &&
              ! pApp->GetClientLaunchOptions ( ).empty())
          {
            hoverText = pApp->GetClientCommandLine (&_launch_cfg.second);
          }

          else
          {
            if (! _launch.getLaunchOptionsUTF8().empty())
              hoverText += (" " + _launch.getLaunchOptionsUTF8());

            // Also add Steam defined launch options, but only if not using custom launch configs
            if (! pApp->GetClientLaunchOptions ( ).empty() &&
                ! _launch_cfg.second.custom_skif && ! _launch_cfg.second.custom_user)
              hoverText += (" " + pApp->GetClientLaunchOptions ( ));
          }

          if (! playDisabled && ! blacklisted)
            SKIF_ImGui_SetMouseCursorHand ( );

          SKIF_ImGui_SetHoverText       (hoverText.c_str());

          if (blacklisted)
            SKIF_ImGui_SetHoverTip      ("Special K has been disabled for this launch configuration.");
        }

        ImGui::EndMenu ();
      }

      if (pApp->isInstantPlayCompatible ())
        SKIF_ImGui_SetHoverTip  ("Skips the regular platform launch process for the game,\n"
                                  "including steps such as cloud saves synchronization.");

      if (_inject.bHasServlet)
      {
        if (! playDisabled)
          ImGui::PushStyleColor      (ImGuiCol_Text,
            ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
          );

        if (ImGui::BeginMenu ("\xe2\x94\x94 without Special K###GameContextMenu_InstantPlayWoSKMenu"))
        {
          bool sepCustomSKIF = true,
               sepCustomUser = true;

          for (auto& _launch_cfg : pApp->launch_configs)
          {
            if (! _launch_cfg.second.valid ||
                  _launch_cfg.second.duplicate_exe_args)
              continue;

            // Filter out launch configs requiring not owned DLCs
            if (! _launch_cfg.second.owns_dlc)
              continue;

            auto& _launch = _launch_cfg.second;

            // Separators between official / user / SKIF launch configs
            if (_launch_cfg.second.custom_user && sepCustomUser)
            {
              sepCustomUser = false;
              ImGui::Separator ( );
            }

            else if (_launch_cfg.second.custom_skif && sepCustomSKIF)
            {
              sepCustomSKIF = false;
              ImGui::Separator ( );
            }

            bool localDisabled = (_launch.injection.injection.type == InjectionType::Local);

            char        szButtonLabel [256] = { };

            sprintf_s ( szButtonLabel, 255,
                          "%s%s%s###GameContextMenu_InstantPlayWoSKMenu-%d",
                                  (localDisabled) ? (ICON_FA_LOCK        "  ") : "",
                           (_launch.isElevated()) ? (ICON_FA_USER_SHIELD "  ") : "",
                            _launch.getDescriptionUTF8().empty ()
                              ? _launch.getExecutableFileNameUTF8().c_str ()
                              : _launch.getDescriptionUTF8().c_str (),
                            _launch.id);

            if (playDisabled || localDisabled)
            {
              ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
              ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
            }
            else
              ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

            if (ImGui::Selectable (szButtonLabel))
            {
              launchConfig = &_launch_cfg.second;
              launchInstant = launchWithoutSK = true;
            }

            if (playDisabled || localDisabled)
            {
              ImGui::PopStyleColor  ( );
              ImGui::PopItemFlag    ( );
            }
            else
              ImGui::PopStyleColor  ( );

            std::string hoverText = _launch.getExecutableFullPathUTF8();

            if (! _launch.getLaunchOptionsUTF8().empty())
              hoverText += (" " + _launch.getLaunchOptionsUTF8());

            if (! playDisabled && ! localDisabled)
              SKIF_ImGui_SetMouseCursorHand ( );

            SKIF_ImGui_SetHoverText       (hoverText.c_str());

            if (localDisabled)
              SKIF_ImGui_SetHoverTip      ("This option is not available due to a local injection being used.");
          }

          ImGui::EndMenu ();
        }

        if (! playDisabled)
          ImGui::PopStyleColor ( );
      }

      if (playDisabled)
        ImGui::PopStyleColor ( );
    }
  }
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
      if (pid == 0)
        return;

      EnumWindows ([](HWND hWnd, LPARAM lParam) -> BOOL
      {
        DWORD                            dwPID = 0;
        GetWindowThreadProcessId (hWnd, &dwPID);

        if (dwPID == (DWORD)lParam)
        {
          if (GetWindowLongPtrW (hWnd, GWL_EXSTYLE) & WS_EX_APPWINDOW)
          {
            if (IsIconic (hWnd))
              ShowWindow (hWnd, SW_RESTORE);

            SetForegroundWindow (hWnd);

            return FALSE;
          }
        }

        return TRUE;
      }, pid);
    };

    if (mon_app != nullptr)
    {
      HANDLE hProcess = mon_app->hProcess.load();

      ImGui::Separator ( );

      bool bInvalid = (hProcess == NULL);

      if (bInvalid)
        ImGui::BeginDisabled ( );

      if (SKIF_ImGui_MenuItemEx2 ("Switch to game", ICON_FA_WINDOW_RESTORE))
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

      if (SKIF_ImGui_MenuItemEx2 ("Terminate game", ICON_FA_POWER_OFF))
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

      if (SKIF_ImGui_MenuItemEx2 ("Switch to game", ICON_FA_WINDOW_RESTORE))
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

      if (SKIF_ImGui_MenuItemEx2 ("Terminate game", ICON_FA_POWER_OFF))
      {
        static_proc.pid    = pApp->_status.running_pid;
        static_proc.name   = pApp->names.normal;
      }
    }
  }
}


// Game context menu: "Browse" submenu (moved verbatim from DrawGameContextMenu)
static void
DrawGameContextMenu_Browse (app_record_s* pApp)
{
  if (SKIF_ImGui_BeginMenuEx2 ("Browse", ICON_FA_FOLDER, ImColor(255, 207, 72)))
  {
    if (SKIF_ImGui_MenuItemEx2 ("Game folder", ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
      SKIF_Util_ExplorePath         (pApp->install_dir);

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->install_dir));

    // Config Root
    if (SKIF_ImGui_MenuItemEx2 ("Profile folder", ICON_FA_FOLDER_OPEN, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
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
      if (SKIF_ImGui_MenuItemEx2 ("Screenshots", ICON_FA_IMAGES, ImColor(200, 200, 200, 255)))
        SKIF_Util_ExplorePath       (pApp->ui.wsScreenshotDir);

      SKIF_ImGui_SetMouseCursorHand ();
      SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->ui.wsScreenshotDir.data()).c_str());
    }

    ImGui::Separator    ( );

    // --------------------------------------------

    // Config File
    if (SKIF_ImGui_MenuItemEx2 ("Configuration", ICON_FA_FILE_LINES)) // pApp->specialk.injection.config.shorthand_utf8.c_str ()
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

    // --------------------------------------------

    if (! pApp->ui.cloud_paths.empty())
    {
      ImGui::Separator  ( );

      if (SKIF_ImGui_BeginMenuEx2 ("Save/config folders", ICON_FA_FLOPPY_DISK, ImColor(200, 200, 200, 255)))
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

    ImGui::EndMenu ();
  }
}


// The number of games pinned on top in the whole library, including filtered and hidden ones
//   (numPinnedOnTop only counts the visible ones, so it cannot be used to enforce the limit of 5)
static int
SKIF_Lib_CountPinnedOnTop (void)
{
  static SKIF_GamingCollection& _games = SKIF_GamingCollection::Get ( );

  int count = 0;

  for (auto& app : _games.GetApps ( ))
    if (app.second->id != 0 && app.second->skif.pinned > 50)
      count++;

  return count;
}

// Game context menu: "Manage" submenu (moved verbatim from DrawGameContextMenu)
static void
DrawGameContextMenu_Manage (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  // Manage Game
  if (ImGui::BeginMenuEx ("Manage", ICON_FA_GEARS))
  {
    bool desktopShortcutPossible = pApp->CanCreateDesktopShortcut ( );

    if (SKIF_ImGui_MenuItemEx2 ("Properties", ICON_FA_GEAR, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
      ModifyGamePopup = PopupState_Open;

    if (51 > pApp->skif.pinned && 5 > SKIF_Lib_CountPinnedOnTop ( ))
    {
      if (SKIF_ImGui_MenuItemEx2 ("Favorite (pinned; max 5)", ICON_FA_STAR, ImColor(255, 207, 72)))
      {
        pApp->skif.pinned =  51;
        numPinnedOnTop++;

        SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp  ( pApp, true);

        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }
    }

    constexpr char* labelPin   =   "Favorite###Manage-Favorite";
    constexpr char* labelUnpin = "Unfavorite###Manage-Favorite";

    bool isFavorite = pApp->IsFavorite ( );
    if (SKIF_ImGui_MenuItemEx2 ((isFavorite ? labelUnpin : labelPin), (isFavorite ? ICON_FA_HEART_CRACK : ICON_FA_HEART), ImColor(245, 66, 66, 255)))
    {
      if (pApp->skif.pinned > 50)
        numPinnedOnTop--;

      pApp->skif.pinned = isFavorite ? 0 : 1;

      SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp   (pApp, true);

      SKIF_GamingCollection::SortApps (&_games.GetApps());
      sort_changed = true;
    }

    ImGui::Separator ( );

    // Category
    if (SKIF_ImGui_BeginMenuEx2 ("Category", ICON_FA_OBJECT_GROUP, ImColor(200, 200, 200, 255)))
    {
      // Prevent the popup from being closed when selecting a category
      ImGui::PushItemFlag (ImGuiItemFlags_AutoClosePopups, false); // ImGuiItemFlags_SelectableDontClosePopup before ImGui 1.91.0
      ImGui::PushID       ("###ManageCategories");

      // TODO: Figure out why unchecking this closes the popup
      if (ImGui::MenuItem ("Favorites###ManageCategoiesFavorites", "", isFavorite))
      {
        if (pApp->skif.pinned > 50)
          numPinnedOnTop--;

        pApp->skif.pinned = isFavorite ? 0 : 1;
        SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp (pApp, true);

        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      ImGui::Separator ( );

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
          if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("Rename###PopupRename-%i", index)))
          {
            isRenaming = index;
            strncpy_s (charCategoryRename, category.name.c_str(), _TRUNCATE); // Always null-terminated
            ImGui::CloseCurrentPopup ( );
          }

          if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("Remove###PopupRemove-%i", index)))
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
        //sort_changed = true; // Disabled as this causes a noticable flicker on the menu when the game goes out and in of visibility
      }

      if (! _registry.vecCategories.empty())
        ImGui::Separator ( );

      if (isFavorite)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::InputTextEx ("###ManageCategoriesAddNew", "New category...", charCategoryName, maxCategoryNameLen,
                      ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, 0.0f), ImGuiInputTextFlags_EnterReturnsTrue))
      {
        std::string szName = charCategoryName;
        strncpy (charCategoryName, "\0", maxCategoryNameLen);

        if (! szName.empty() && std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == szName; }) == _registry.vecCategories.end())
        {
          // Add the new category
          _registry.vecCategories.push_back (SKIF_RegistrySettings::category_s { szName, false });

          // Sort the list of categories
          _registry.vecCategories = _registry.SortCategories (_registry.vecCategories);

          // Update the registry
          std::vector<std::wstring> _inNames, _inBools;
          _inNames.reserve (_registry.vecCategories.size ( ));
          _inBools.reserve (_registry.vecCategories.size ( ));
          for (auto& category : _registry.vecCategories)
          {
            _inNames.push_back (SK_UTF8ToWideChar (category.name));
            _inBools.push_back (std::to_wstring   (category.expanded));
          }

          _registry.regKVCategories.     putDataMultiSZ (_inNames);
          _registry.regKVCategoriesState.putDataMultiSZ (_inBools);
        }

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

      if (bCategoryMgnt)
        allowShortcutCtrlA = false;

      ImGui::PopID       ( );
      ImGui::PopItemFlag ( );

      ImGui::EndMenu ( );
    }

    ImGui::Separator ( );

    if (desktopShortcutPossible)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Create desktop shortcut", ICON_FA_PAPERCLIP, ImColor(200, 200, 200, 255)))
      {
        std::string name = SKIF_Util_StripInvalidFilenameChars (pApp->names.normal);

        std::wstring linkPath = SK_FormatStringW (LR"(%ws\%ws.lnk)", std::wstring(_path_cache.desktop.path).c_str(), SK_UTF8ToWideChar(name).c_str());
        std::wstring linkArgs = pApp->GetDesktopShortcutArgs ( );
        std::wstring iconPath = pApp->GetDesktopShortcutIcon ( );

        constexpr char* info_title         = "Create desktop shortcut";
        constexpr char* info_label_success = "A desktop shortcut has been created.";
        constexpr char* info_label_failure = "Failed to create a desktop shortcut!";

        if (SKIF_Util_CreateShortcut (
            linkPath.c_str(),
            _path_cache.skif_executable,
            linkArgs.c_str(),
            pApp->launch_configs[0].getWorkOrExeDirectory().c_str(),
            SK_UTF8ToWideChar(name).c_str(),
            iconPath.c_str()
            )
          )
          SKIF_ImGui_InfoMessage (info_title, info_label_success);
        else
          SKIF_ImGui_InfoMessage (info_title, info_label_failure);
      }
    }

    constexpr char* labelHide   =   "Hide";
    constexpr char* labelUnhide = "Unhide";

    bool bUnhide = pApp->IsHidden ( );

    if (SKIF_ImGui_MenuItemEx2 ((bUnhide ? labelUnhide : labelHide), (bUnhide ? ICON_FA_EYE : ICON_FA_EYE_SLASH), ImColor(200, 200, 200, 255)))
    {
      pApp->skif.hidden = bUnhide ? 0 : 1;

      SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp  ( pApp, true);

      RepopulateGames = true;
    }

    if (pApp->IsUserAdded ( ))
    {
      if (SKIF_ImGui_MenuItemEx2 ("Remove", ICON_FA_TRASH, ImColor(200, 200, 200, 255)))
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

    if (SKIF_ImGui_BeginMenuEx2 ("Developer", ICON_FA_TOOLBOX, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning)))
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


      // Store-specific developer actions (e.g. Xbox: Open Terminal)
      pApp->DrawDeveloperActions ( );

      // Epic and Xbox platforms use fake app IDs hashed from their unique text-based platform identifier
      if (SKIF_ImGui_MenuItemEx2 ("Copy ID", ICON_FA_FINGERPRINT))
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

  // TODO: Many of the below options needs to be adjusted to indicate if the primary launch option
  //         is even available (or if "without Special K" is the only option available)

  app_record_s::launch_config_s*
    firstLaunchConfig = (pApp != nullptr && ! pApp->launch_configs.empty ()) ? &pApp->launch_configs.begin ()->second : nullptr;

  bool playDisabled = (pApp->_status.running || pApp->IsUpdateBlockingLaunch ( ));

  // Push styling for Disabled
  ImGui::PushStyleColor      (ImGuiCol_TextDisabled,
    ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)  * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
  );

  if (playDisabled)
    ImGui::BeginDisabled ( );

  if (ImGui::MenuItemEx ("Play###GameContextMenu_Launch", ICON_FA_GAMEPAD))
    launchGameMenu = true;

  if (playDisabled)
    ImGui::EndDisabled ( );

  if (pApp->specialk.injection.injection.type != InjectionType::Local && _inject.bHasServlet)
  {
    if (! _inject.bCurrentState)
      SKIF_ImGui_SetHoverText ("Starts the injection service as well.");

    ImGui::PushStyleColor      (ImGuiCol_Text,
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
    );

    if (playDisabled)
      ImGui::BeginDisabled ( );

    if (ImGui::MenuItem ("\xe2\x94\x94 without Special K###GameContextMenu_LaunchWoSK"))
      launchGameMenu = launchWithoutSK = true;

    if (playDisabled)
      ImGui::EndDisabled ( );

    ImGui::PopStyleColor   ( );

    if (_inject.bCurrentState)
      SKIF_ImGui_SetHoverText ("Stops the injection service as well.");
  }

  // Instant Play launch options
  DrawGameContextMenu_InstantPlay (pApp, playDisabled, firstLaunchConfig);

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

  if (SKIF_ImGui_BeginMenuEx2 ("Websites", ICON_FA_SHARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
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
    if (SKIF_ImGui_MenuItemEx2 ("Start service", ICON_FA_TOGGLE_ON,  ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)))
      _inject._StartStopInject (_inject.bCurrentState, true);

    if (SKIF_ImGui_MenuItemEx2 ("Start service (manual stop)", ICON_FA_TOGGLE_ON,  ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)))
      _inject._StartStopInject (_inject.bCurrentState, false);
  }

  else {
    if (SKIF_ImGui_MenuItemEx2 ("Stop service", ICON_FA_TOGGLE_OFF, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning)))
      _inject._StartStopInject (_inject.bCurrentState);
  }

  ImGui::Separator ( ); // ==============================

  if (SKIF_ImGui_BeginMenuEx2 ("Browse", ICON_FA_FOLDER, ImColor(255, 207, 72)))
  {
    if (SKIF_ImGui_MenuItemEx2 ("Install folder", ICON_FA_FOLDER_OPEN, ImColor(255, 207, 72)))
      SKIF_Util_ExplorePath (pApp->install_dir);

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->install_dir));

    if (SKIF_ImGui_MenuItemEx2 ("Profile folders", ICON_FA_FOLDER_OPEN, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
      SKIF_Util_ExplorePath (pApp->specialk.profile_dir);

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText (pApp->specialk.profile_dir_utf8);

    ImGui::EndMenu ( );
  }

  ImGui::Separator ( ); // ==============================

  if (SKIF_ImGui_MenuItemEx2 ("Wiki", ICON_FA_BOOK_BOOKMARK, ImColor(25, 118, 210)))
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

  constexpr char* labelPin   =   "Favorite";
  constexpr char* labelUnpin = "Unfavorite";

  bool isFavorite = (pApp->skif.pinned > 0);
  if (SKIF_ImGui_MenuItemEx2 ((isFavorite ? labelUnpin : labelPin), (isFavorite ? ICON_FA_HEART_CRACK : ICON_FA_HEART), ImColor(245, 66, 66, 255)))
  {
    int old_value = pApp->skif.pinned;

    pApp->skif.pinned =  (isFavorite ? 0 : (5 > SKIF_Lib_CountPinnedOnTop ( ) ? 99 : 50));

    if (pApp->skif.pinned > 50)
      numPinnedOnTop++;
    else if (old_value > 50)
      numPinnedOnTop--;

    SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp  ( pApp, true);

    SKIF_GamingCollection::SortApps (&_games.GetApps());
    sort_changed = true;
  }
}

#pragma endregion

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

      // Preparations

      bool resetVisible = (pApp->HasStoreCover ( )   || // Not for Special K or custom games, unless they have custom / managed covers
                           pApp->tex_cover.isCustom ||
                           pApp->tex_cover.isManaged);

      if (SKIF_ImGui_MenuItemEx2 ("Change", ICON_FA_FILE_IMAGE, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      {
        LPWSTR pwszFilePath = NULL;
        HRESULT hr          =
          SKIF_Util_FileExplorer_BrowseForFile(&pwszFilePath, SKIF_ImGui_hWnd, { { L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp" } }, FOS_FILEMUSTEXIST, FOLDERID_Pictures);

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
        constexpr char* textCustom1 = "Clear";
        constexpr char* textCustom2 = "Reset";
        constexpr char* textDefault = "Refresh";
        char*           textPtr     = textDefault;
        constexpr char* iconCustom  = ICON_FA_ROTATE_LEFT;
        constexpr char* iconDefault = ICON_FA_ROTATE;
        char*           iconPtr     = iconDefault;
        ImColor         iconColor   = (_registry._StyleLightMode) ? ImColor (128, 128, 128) : ImColor (128, 128, 128); // If neither custom nor managed (aka disabled)

        if (pApp->tex_cover.isCustom)
        {
          iconColor = (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255);
          iconPtr   = iconCustom;
          textPtr   = ((pApp->IsUserAdded ( )) ? textCustom1 : textCustom2);
        }

        else if (pApp->tex_cover.isManaged)
        {
          iconColor = (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255);
        }

        bool enabled = (pApp->tex_cover.isCustom || pApp->tex_cover.isManaged);
        bool doReset = SKIF_ImGui_MenuItemEx2 (textPtr, iconPtr, iconColor, 0, nullptr, enabled);

        if (! enabled)
          SKIF_ImGui_SetHoverTip ("Managed by the platform client.", false, true);

        else
          SKIF_ImGui_SetMouseCursorHand ( );

        if (doReset)
        {
          std::wstring targetPath = L"";

          targetPath = pApp->GetAssetDirectory ( );

          if (PathFileExists (targetPath.c_str()))
          {
            std::wstring fileName = (pApp->tex_cover.isCustom) ? L"cover" : L"cover-original";

            // Will fail on read-only marked files
            bool d1 = DeleteFile ((targetPath + fileName + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + fileName + L".jpg").c_str()),
                 d3 = false;

            // Store-managed fallback covers (PCGamingWiki covers for GOG and Steam, Xbox fallback cover)
            if (! pApp->tex_cover.isCustom)
              d3 = pApp->DeleteStoreCoverFiles (targetPath);

            // The preferred IGDB cover (Settings), downloaded again on the reload
            bool d4 = (! pApp->tex_cover.isCustom && DeleteFile ((targetPath + L"cover-igdb.jpg").c_str()));

            // If any file was removed
            if (d1 || d2 || d3 || d4)
            {
              update    = true;
              _art.ForgetCover ( ); // Needed as otherwise SKIF would not reload the cover
            }
          }
        }
      }

      ImGui::Separator ( );

      // Image scaling
      ImGui::PushID ("#ImageScaling");

      if (SKIF_ImGui_BeginMenuEx2 ("Scaling", ICON_FA_PANORAMA))
      {
        static bool bDefault = (_registry.iCoverScaling == 0) ? true : false;
        static bool bFill    = (_registry.iCoverScaling == 1) ? true : false;
        static bool bFit     = (_registry.iCoverScaling == 2) ? true : false;
        static bool bNone    = (_registry.iCoverScaling == 3) ? true : false;
        static bool bStretch = (_registry.iCoverScaling == 4) ? true : false;

        if (ImGui::MenuItem ("Default", spaces,  &bDefault, ! bDefault))
        {
          _registry.iCoverScaling = 0;

        //bDefault = false;
          bFill    = false;
          bFit     = false;
          bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        SKIF_ImGui_SetHoverTip ("Scales the cover to a maximum height of 600px", false, true);

        if (ImGui::MenuItem ("Fill",  spaces, &bFill, ! bFill))
        {
          _registry.iCoverScaling = 1;

          bDefault = false;
        //bFill    = false;
          bFit     = false;
          bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        if (ImGui::MenuItem ("Fit",  spaces, &bFit, ! bFit))
        {
          _registry.iCoverScaling = 2;

          bDefault = false;
          bFill    = false;
        //bFit     = false;
          bNone    = false;
          bStretch = false;

          _registry.regKVCoverScaling.putData (_registry.iCoverScaling);
        }

        if (ImGui::MenuItem ("None",  spaces, &bNone, ! bNone))
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

      if (SKIF_ImGui_MenuItemEx2 ("Open SteamGridDB", ICON_FA_UP_RIGHT_FROM_SQUARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
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

      if (SKIF_ImGui_MenuItemEx2 ("Change", ICON_FA_FILE_IMAGE, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      {
        LPWSTR pwszFilePath = NULL;
        HRESULT hr          =
          SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, SKIF_ImGui_hWnd, { { L"Icons", L"*.exe;*.ico;*.png;*.jpg;*.jpeg" } }, FOS_FILEMUSTEXIST, FOLDERID_Pictures);

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
        constexpr char* textCustom  = "Reset";
        constexpr char* textDefault = "Refresh";
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
        bool doReset = SKIF_ImGui_MenuItemEx2 (textPtr, iconPtr, iconColor, 0, nullptr, enabled);

        if (! enabled)
          SKIF_ImGui_SetHoverTip ("Managed by the platform client.", false, true);

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

      if (SKIF_ImGui_MenuItemEx2 ("Open SteamGridDB", ICON_FA_UP_RIGHT_FROM_SQUARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
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
      if (SKIF_ImGui_MenuItemEx2 ("Clear filter", ICON_FA_SQUARE_XMARK, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure)))
        ClearCharFilter ( );

      ImGui::Separator   ( );
    }

     if (SKIF_ImGui_MenuItemEx2 ("Add game", ICON_FA_SQUARE_PLUS, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)))
       AddGamePopup = PopupState_Open;

    ImGui::Separator      ( );

    ImGui::PushID ("#LibrarySort");

    if (SKIF_ImGui_BeginMenuEx2 ("Sort by", ICON_FA_SORT))
    {
      static bool bName       = (_registry.iLibrarySort == 0) ? true : false;
      static bool bFrequently = (_registry.iLibrarySort == 1) ? true : false;
      static bool bRecently   = (_registry.iLibrarySort == 2) ? true : false;

      if (ImGui::MenuItem ("Alphabetical", spaces,  &bName     ))
      {
        _registry.iLibrarySort = 0;

        bFrequently = false;
        bRecently   = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      if (ImGui::MenuItem ("Most played",  spaces, &bFrequently))
      {
        _registry.iLibrarySort = 1;

        bName       = false;
        bRecently   = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&_games.GetApps());
        sort_changed = true;
      }

      SKIF_ImGui_SetHoverTip ("Sort by the number of launches of the game through this app.");

      if (ImGui::MenuItem ("Last played",  spaces, &bRecently  ))
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

    ImGui::PushID ("#Platforms");

    if (SKIF_ImGui_BeginMenuEx2 ("Platforms", ICON_FA_GEARS))
    {
      static bool* pbLibraryEA     = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEA;
      static bool* pbLibraryEpic   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEpic;
      static bool* pbLibraryGOG    = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryGOG;
      static bool* pbLibraryitch   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryitch;
      static bool* pbLibrarySteam  = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibrarySteam;
      static bool* pbLibraryUbisoft= (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryUbisoft;
      static bool* pbLibraryXbox   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryXbox;
      static bool* pbLibraryCustom = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryCustom;

      if (_games.IsPopulating ( ))
        SKIF_ImGui_PushDisableState ( );

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

      if (ImGui::MenuItem ("Custom",  spaces, pbLibraryCustom, (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryCustom.putData(_registry.bLibraryCustom);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Hidden games",spaces, &_registry._LibraryHidden))
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

      if (_games.IsPopulating ( ))
        SKIF_ImGui_PopDisableState ( );

      ImGui::EndMenu ( );
    }

    ImGui::PopID ( );

    ImGui::Separator      ( );

    ShowLargeIconToggle   ( );

    if (SKIF_ImGui_MenuItemEx2 ("Refresh", ICON_FA_ROTATE_RIGHT, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
      RepopulateGames = true;

    ImGui::PopStyleColor  ( );
    ImGui::EndPopup       ( );
  }

  else
    EmptySpaceMenu = PopupState_Closed;

#pragma endregion
}

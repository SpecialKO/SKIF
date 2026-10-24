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
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

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
      SKIF_Util_OpenURI (pApp->specialk.injection.config.full_path.c_str(), SW_SHOWNORMAL, NULL);
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

void
DrawGameContextMenu (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  // TODO: Many of the below options needs to be adjusted to indicate if the primary launch option
  //         is even available (or if "without Special K" is the only option available)

  app_record_s::launch_config_s*
    firstLaunchConfig = (pApp != nullptr && ! pApp->launch_configs.empty ()) ? &pApp->launch_configs.begin ()->second : nullptr;

  bool playDisabled = (pApp->_status.running || pApp->_status.updating);

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

      bool bInvalid = (hProcess == INVALID_HANDLE_VALUE);

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

  // Pop styling for Disabled
  ImGui::PopStyleColor   ( );

  ImGui::Separator ( );
  // ==============================

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

  // Manage Game
  if (ImGui::BeginMenuEx ("Manage", ICON_FA_GEARS))
  {
    bool desktopShortcutPossible = pApp->CanCreateDesktopShortcut ( );

    if (SKIF_ImGui_MenuItemEx2 ("Properties", ICON_FA_GEAR, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
      ModifyGamePopup = PopupState_Open;

    if (51 > pApp->skif.pinned && 5 > numPinnedOnTop)
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
      ImGui::PushItemFlag (ImGuiItemFlags_SelectableDontClosePopup, true);
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
            strncpy (charCategoryRename, category.name.c_str(), maxCategoryNameLen);
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
          SKIF_RegistrySettings::category_s  new_category{ szName, false };
          _registry.vecCategories.push_back (new_category);

          // Sort the list of categories
          _registry.vecCategories = _registry.SortCategories (_registry.vecCategories);

          // Update the registry
          std::vector<std::wstring> _inNames, _inBools;
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

  // Store-specific submenu (e.g. Steam)
  pApp->DrawStoreMenu ( );

  if (SKIF_ImGui_BeginMenuEx2 ("Websites", ICON_FA_SHARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
  {
    // Store-specific websites (e.g. Steam Community, SteamDB, GOG Database)
    pApp->DrawWebsiteMenuItems ( );

    if (SKIF_ImGui_MenuItemEx2 ("PCGamingWiki", ICON_FA_SCREWDRIVER_WRENCH, ImColor(200, 200, 200, 255)))
      SKIF_Util_OpenURI (SK_UTF8ToWideChar(pApp->ui.pcgw).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (pApp->ui.pcgw);

    ImGui::EndMenu ( );
  }


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
    SKIF_Util_OpenURI (SK_UTF8ToWideChar(pApp->ui.pcgw).c_str());

  SKIF_ImGui_SetMouseCursorHand ( );
  SKIF_ImGui_SetHoverText       (pApp->ui.pcgw);

  if (SKIF_STEAM_OWNER)
  {
    if (SKIF_ImGui_MenuItemEx2 ("Steam", ICON_FA_STEAM_SYMBOL, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      SKIF_Util_OpenURI ((L"steam://nav/games/details/" + std::to_wstring (pApp->id)).c_str());

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

    pApp->skif.pinned =  (isFavorite ? 0 : (5 > numPinnedOnTop ? 99 : 50));

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

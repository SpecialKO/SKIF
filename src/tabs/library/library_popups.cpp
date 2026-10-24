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

// Popups of the library tab (moved verbatim from SKIF_UI_Tab_DrawLibrary)
void
DrawLibraryPopups (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static library_selection_s& selection    = GetLibraryTab ( ).selection;
  static app_record_s*&       pApp         = GetLibraryTab ( ).pApp;
  static bool&                update       = GetLibraryTab ( ).update;

#pragma region Popups

  // Popup: Remove Game
  if (SKIF_Custom_RemoveGame (selection.appid))
  {
    // Hide entry
    pApp->id = 0;

    // Release the icon texture (the cover will be handled by LoadLibraryTexture on next frame
    if (pApp->tex_icon.texture.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pApp->tex_icon.texture.p << " to be released";
      SKIF_ResourcesToFree.push(pApp->tex_icon.texture.p);
      pApp->tex_icon.texture.p = nullptr;
    }

    // Reset selection to Special K
    selection.reset ( );

    update = true;
  }

  // Popup: Add Game
  if (SKIF_Custom_AddGame ( ))
  {
    // Unload any current cover on success
    _art.ReleaseCurrentCover ( );
  }

  // Popup: Properties / Manage Game
  if (ModifyGamePopup != PopupState_Closed)
  {
    const bool failure_induced_popup =
      pApp != nullptr && pApp->launch_failed;

    const char* title =
        failure_induced_popup ? "Standard Game Launch Failed - Instant Play Was Used Instead###ModifyGamePopup"
                              : "Properties###ModifyGamePopup";

    // Always use same size
    float fModifyGamePopupWidth = 560.0f * SKIF_ImGui_GlobalDPIScale;

    if (SKIF_ImGui_BeginPopup (title, nullptr, ImVec2 (fModifyGamePopupWidth, 0.0f)))
    {
      static char charName     [MAX_PATH + 2] = { },
                  hintName     [MAX_PATH + 2] = { },
                  charPath     [MAX_PATH + 2] = { },
                  charWorkDir  [MAX_PATH + 2] = { },
                  charArgs     [    1024 + 2] = { };
      static bool changed_name        = false;

      static bool error               = false,
                  errorWorkDir        = false;
      static int  cached_elevate_load = true;
      static bool cached_elevate      = false;
      static int  cached_hidden_load  = true;
      static bool cached_hidden       = false;
      static int  cached_pinned_load  = true;
      static bool cached_pinned       = false;
      static int  cached_auto_stop    = -1;
      static int  cached_instant_play = -1;

      if (ModifyGamePopup == PopupState_Open)
      {
        if (cached_elevate_load)
        {
          cached_elevate         = pApp->launch_configs[0].isElevated ( );
          cached_elevate_load    = false;
        }

        if (cached_hidden_load)
        {
          cached_hidden          = pApp->IsHidden ( );
          cached_hidden_load     = false;
        }

        if (cached_pinned_load)
        {
          cached_pinned          = pApp->IsFavorite ( );
          cached_pinned_load     = false;
        }

        if (cached_auto_stop    == -1)
          cached_auto_stop       = pApp->skif.auto_stop;

        if (cached_instant_play == -1)
          cached_instant_play    = pApp->skif.instant_play;

        if (pApp->IsUserAdded ( ))
        {
          strncpy (charPath,    pApp->launch_configs[0].getExecutableFullPathUTF8 ( ).c_str(), MAX_PATH);
          strncpy (charWorkDir, pApp->launch_configs[0].getWorkingDirectoryUTF8   ( ).c_str(), MAX_PATH);
          strncpy (charArgs,    pApp->launch_configs[0].getLaunchOptionsUTF8      ( ).c_str(), 1024);
        }

        // Use the appropriate hint for the game name
        if (! pApp->names.clean.empty())
          strncpy (hintName, pApp->names.clean.c_str(), MAX_PATH);

        // Fill out the current one if it is different from the pseudo one
        if (pApp->names.clean != pApp->names.normal)
          strncpy (charName, pApp->names.normal.c_str( ), MAX_PATH);

        // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
        ImGuiWindow* window = ImGui::FindWindowByName ("###ModifyGamePopup");
        if (window != nullptr && ! window->Appearing)
          ModifyGamePopup = PopupState_Opened;
      }

      ImGui::PushStyleColor   (
        ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                                );

      ImGui::TreePush    ("ModifyGameTreePush");

      SKIF_ImGui_Spacing ( );

      ImVec2 vButtonSize      = ImVec2 ( 80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);
      ImVec2 vInputSize       = ImVec2 (275.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);
      bool   disabled         = false;
      float  fInputX          = fModifyGamePopupWidth / 2.0f - vInputSize.x / 2.0f;

      // Custom games has their own manage fields
      if (pApp->IsUserAdded ( ))
      {
        static float fBtnX  = fInputX                                                              // ImGui::InputTextEx (center-aligned)
                            - ImGui::CalcItemSize(vButtonSize, ImGui::CalcTextSize("Browse...").x  // ImGui::Button
                            - ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f).x                     // ImGui::Button
                            - ImGui::GetStyle().ItemSpacing.x;                                     // ImGui::SameLine

        ImGui::SetCursorPosX (fBtnX);

        // Path / Executable
        if (ImGui::Button  ("Browse...###BrowsePath", vButtonSize))
        {
          LPWSTR pwszFilePath = NULL;
          HRESULT hr          =
            SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, SKIF_ImGui_hWnd, { { L"Executables", L"*.exe;*.bat" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST);

          if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
          {
            // If cancelled, do nothing
          }

          else if (SUCCEEDED(hr))
          {
            error = false;
            std::filesystem::path path           = pwszFilePath; // Wide-string std::filesystem::path
            std::filesystem::path pathDiscard    = pwszFilePath; // Wide-string std::filesystem::path which will be discarded
            std::string           pathFullPath   = SK_WideCharToUTF8  (pathDiscard.wstring());
            std::wstring          pathExtension  = SKIF_Util_ToLowerW (pathDiscard.extension().wstring());
            std::string           pathInstallDir = SK_WideCharToUTF8  (pathDiscard.parent_path().lexically_normal());
            std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

            if (pathExtension == L".lnk")
            {
              WCHAR wszTarget    [MAX_PATH + 2] = { };
              WCHAR wszWorkingDir[MAX_PATH + 2] = { };
              WCHAR wszArguments [    1024 + 2] = { };

              SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, wszWorkingDir, MAX_PATH * sizeof (WCHAR));

              if (! PathFileExists (wszTarget))
              {
                error = true;
                strncpy (charPath,    "\0", MAX_PATH);
                strncpy (charWorkDir, "\0", MAX_PATH);
              }

              else {
                //std::wstring productName = SKIF_Util_GetProductName (wszTarget);

                strncpy (charPath,    SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
                strncpy (charWorkDir, SK_WideCharToUTF8 (wszWorkingDir).c_str(),              MAX_PATH);
              }
            }

            else if (pathExtension == L".exe") {
              //std::wstring productName = SKIF_Util_GetProductName (path.c_str());

              strncpy (charPath,    pathFullPath.c_str(),   MAX_PATH);
              strncpy (charWorkDir, pathInstallDir.c_str(), MAX_PATH);
            }

            else if (pathExtension == L".bat") {
              strncpy (charPath,    pathFullPath.c_str(),   MAX_PATH);
              strncpy (charWorkDir, pathInstallDir.c_str(), MAX_PATH);
            }

            else {
              error = true;
              strncpy (charPath,    "\0", MAX_PATH);
              strncpy (charWorkDir, "\0", MAX_PATH);
            }
          }

          else {
            error = true;
            strncpy (charPath,    "\0", MAX_PATH);
            strncpy (charWorkDir, "\0", MAX_PATH);
          }
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::SameLine    ( );

        float fTxtErrorX = ImGui::GetCursorPosX ( );

        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::InputTextEx    ("###GamePath", "", charPath, MAX_PATH, vInputSize, ImGuiInputTextFlags_ReadOnly);
        SKIF_ImGui_DisallowMouseDragMove ( );
        ImGui::PopStyleColor  ( );
        ImGui::SameLine       ( );
        ImGui::Text           ("Path");

        if (error)
        {
          ImGui::SetCursorPosX (fTxtErrorX);
          ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
        }

        ImGui::SetCursorPosX (fBtnX);

        // Working Directory
        if (ImGui::Button  ("Change...###BrowseWorkDir", vButtonSize))
        {
          errorWorkDir = false;
          LPWSTR pwszWorkingDir = NULL;

          HRESULT hr          =
            SKIF_Util_FileExplorer_BrowseForFolder (&pwszWorkingDir, SKIF_ImGui_hWnd);

          if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
          {
            // If cancelled, do nothing
          }

          else if (SUCCEEDED(hr))
          {
            std::string           pathWorkDir = SK_WideCharToUTF8(pwszWorkingDir);
            strncpy (charWorkDir, pathWorkDir.c_str(), MAX_PATH);
          }

          else {
            errorWorkDir = true;
            strncpy (charWorkDir, "\0", MAX_PATH);
          }
        }

        SKIF_ImGui_DisallowMouseDragMove ( );

        ImGui::SameLine    ( );

        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::InputTextEx    ("###GameWorkDir", "", charWorkDir, MAX_PATH, vInputSize, ImGuiInputTextFlags_ReadOnly);
        SKIF_ImGui_DisallowMouseDragMove ( );
        ImGui::PopStyleColor  ( );
        ImGui::SameLine       ( );
        ImGui::Text           ("Working Directory");

        if (errorWorkDir)
        {
          ImGui::SetCursorPosX (fTxtErrorX);
          ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible folder! Please select another folder.");
        } else {
          // Empty line
          ImGui::NewLine   ( );
        }
      }

      ImGui::SetCursorPosX (fInputX);

      ImGui::InputTextEx ("###GameName", hintName, charName, MAX_PATH, vInputSize, ImGuiInputTextFlags_None);
      SKIF_ImGui_DisallowMouseDragMove ( );
      ImGui::SameLine    ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "Name"
      );

      if (charName[0] != '\0')
        changed_name = true;

      if (pApp->IsUserAdded ( ))
      {
        ImGui::SetCursorPosX (fInputX);

        ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 1024, vInputSize, ImGuiInputTextFlags_None);
        SKIF_ImGui_DisallowMouseDragMove ( );
        ImGui::SameLine    ( );
        ImGui::Text        ("Launch Options");

        SKIF_ImGui_Spacing ( );
        SKIF_ImGui_Spacing ( );

        if ((charPath   [0] == '\0' || std::isspace(charPath   [0])) ||
            (charWorkDir[0] == '\0' || std::isspace(charWorkDir[0])))
          disabled = true;
      }

      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );

      // Steam / GOG / Xbox supports Instant Play
      if (pApp->isInstantPlayCompatible ( ))
      {
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
        SKIF_ImGui_SetHoverTip ("Warning: This skips the regular platform launch process for the game,\n"
                                "including steps like the cloud saves synchronization that usually occurs.");
        ImGui::SameLine    ( );
        ImGui::TextColored (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
            "Use Instant Play:"
        );

        if (pApp->launch_failed)
        {
          ImGui::SameLine (); SKIF_ImGui_Spacing ( );
          ImGui::SameLine (); SKIF_ImGui_Spacing ( );
          ImGui::SameLine ();

          ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), "Standard Launch Failed!");
        }

        ImGui::TreePush        ("ManageGame_InstantPlay");
        ImGui::RadioButton     (
          SKIF_Util_FormatStringRaw ("Default (%s)", (pApp->GetInstantPlayDefault ( ))
                                              ? "Always" : "Never"), &cached_instant_play, 0);
        SKIF_ImGui_SetHoverTip ("The game will use the default behavior configured in the Settings tab.");
        ImGui::SameLine        ( );
        ImGui::RadioButton     ("Never",             &cached_instant_play, 2);
        SKIF_ImGui_SetHoverTip ("The game will never use instant play except\nwhen launched through the right click menu.");
        ImGui::SameLine        ( );
        if (pApp->launch_failed && cached_instant_play != 1)
        {
          ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info));
          ImGui::RadioButton   ("Always",            &cached_instant_play, 1);
          ImGui::PopStyleColor ();
          SKIF_ImGui_SetHoverTip ("This game may not launch correctly unless Always is set.");
        } else {
          ImGui::RadioButton     ("Always",            &cached_instant_play, 1);
          SKIF_ImGui_SetHoverTip ("The game will always use instant play except\nwhen launched through the right click menu.");
        }
        ImGui::TreePop         ( );

        SKIF_ImGui_Spacing     ( );
        SKIF_ImGui_Spacing     ( );
      }

      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip ("This determines how long the service will remain running when launching a game.\n"
                              "Move the mouse over each option to get more information.");
      ImGui::SameLine        ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "Auto-stop behavior when launching the game:"
      );

      ImGui::TreePush        ("ManageGame_AutoStopBehavior");
      ImGui::RadioButton     (SKIF_Util_FormatStringRaw ("Default (%s)", (_registry.iAutoStopBehavior == 1) ? "Inject" : "Exit"), &cached_auto_stop, 0);
      SKIF_ImGui_SetHoverTip ("The service will use the default behavior configured in the Settings tab.");
      ImGui::SameLine        ( );
      ImGui::RadioButton     ("On inject", &cached_auto_stop, 1);
      SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K\nsuccessfully injects into a game.");
      ImGui::SameLine        ( );
      ImGui::RadioButton     ("On exit",   &cached_auto_stop, 2);
      SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K\ndetects that the game is being closed.");
      ImGui::SameLine        ( );
      ImGui::BeginGroup      ( );
      ImGui::RadioButton     ("Never",     &cached_auto_stop, 3);
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::EndGroup        ( );
      SKIF_ImGui_SetHoverTip ("Warning: The service will remain even\nafter the game has been closed.");
      ImGui::TreePop         ( );

      SKIF_ImGui_Spacing     ( );
      SKIF_ImGui_Spacing     ( );

      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "Miscellaneous settings:"
      );

      ImGui::TreePush        ("ManageGame_Miscellaneous");
      ImGui::Checkbox        ("Elevated service###ElevatedLaunch", &cached_elevate);
      ImGui::SameLine        ( );
      ImGui::Spacing         ( );
      ImGui::SameLine        ( );
      ImGui::Checkbox        ("Hidden###HideInLibrary", &cached_hidden);
      ImGui::SameLine        ( );
      ImGui::Spacing         ( );
      ImGui::SameLine        ( );
      ImGui::Checkbox        ("Favorited###PinInLibrary", &cached_pinned);
      ImGui::TreePop         ( );

      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );

      if (disabled)
        SKIF_ImGui_PushDisableState ( );

      ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

      if (ImGui::Button  ("Save", vButtonSize)) // Update
      {
        bool repopulate = false;
        bool resort     = false;

        // If the elevate state has been changed, apply the new one
        if (cached_elevate != pApp->launch_configs[0].isElevated ( ))
          pApp->launch_configs[0].setElevated (cached_elevate);

        pApp->skif.auto_stop    = cached_auto_stop;
        pApp->skif.instant_play = cached_instant_play;

        if (cached_hidden != pApp->IsHidden ( ))
        {
          repopulate            = true;
          pApp->skif.hidden     = (int)cached_hidden;
        }

        if (cached_pinned != pApp->IsFavorite ( ))
        {
          resort                = true;
          pApp->skif.pinned     = (int)cached_pinned;
        }

        // Custom games also updates the registry
        if (pApp->IsUserAdded ( ))
        {
          std::wstring wszPath    = SK_UTF8ToWideChar (charPath);
          std::wstring wszWorkDir = SK_UTF8ToWideChar (charWorkDir);
          std::wstring wszArgs    = SK_UTF8ToWideChar (charArgs);

          if (SKIF_ModifyCustomAppID (pApp, wszPath, wszArgs, wszWorkDir))
          {
            // Attempt to extract the icon from the given executable straight away
            std::wstring SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\icon-original.png)", _path_cache.specialk_userdata, pApp->id);
            DeleteFile (SKIFCustomPath.c_str());
            SKIF_Util_SaveExtractExeIcon (wszPath, SKIFCustomPath);
          }
        }

        // If the name has been changed, we need to repopulate the list
        if (charName != pApp->names.normal)
        {
          // If the name is the same as the original name, reset the custom value
          if (charName == pApp->names.clean)
            strncpy (charName, "\0", MAX_PATH);

          pApp->skif.name = charName;

          if (changed_name)
            repopulate      = true;
        }

        // Update any locally stored metadata
        SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp (pApp, true);

        // Clear variables
        changed_name        = false;
        error               = false;
        errorWorkDir        = false;
        strncpy (charName,    "\0", MAX_PATH);
        strncpy (charPath,    "\0", MAX_PATH);
        strncpy (charWorkDir, "\0", MAX_PATH);
        strncpy (charArgs,    "\0", 1024);
        cached_elevate_load = true;
        cached_hidden_load  = true;
        cached_pinned_load  = true;
        cached_auto_stop    = -1;
        cached_instant_play = -1;

        if (repopulate)
        {
          RepopulateGames = true;
          update = true;
        }

        else if (resort)
        {
          SKIF_GamingCollection::SortApps (&_games.GetApps());
          sort_changed = true;
        }

        ModifyGamePopup = PopupState_Closed;
        ImGui::CloseCurrentPopup ( );
      }

      SKIF_ImGui_DisallowMouseDragMove ( );

      if (disabled)
        SKIF_ImGui_PopDisableState  ( );

      ImGui::SameLine    ( );

      ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

      if (ImGui::Button  ("Cancel", vButtonSize))
      {
        // Clear variables
        error               = false;
        errorWorkDir        = false;
        strncpy (charName,    "\0", MAX_PATH);
        strncpy (charPath,    "\0", MAX_PATH);
        strncpy (charWorkDir, "\0", MAX_PATH);
        strncpy (charArgs,    "\0", 1024);
        cached_elevate_load = true;
        cached_hidden_load  = true;
        cached_auto_stop    = -1;
        cached_instant_play = -1;

        ModifyGamePopup = PopupState_Closed;
        ImGui::CloseCurrentPopup ( );

        if (pApp != nullptr)
            pApp->launch_failed = false;
      }

      SKIF_ImGui_DisallowMouseDragMove ( );

      SKIF_ImGui_Spacing ( );

      ImGui::TreePop     ( ); // ModifyGameTreePush

      ImGui::PopStyleColor ( );
    }

    else {
      ModifyGamePopup = PopupState_Closed;

      if (pApp != nullptr)
          pApp->launch_failed = false;
    }

    SKIF_ImGui_EndPopup ( );
  }



  // End Task confirmation prompt

  if (static_category.change)
  {
    ImGui::OpenPopup           ("###PopupCategoryModify");
    ImGui::SetNextWindowSize   (ImVec2 (0.0f, 0.0f));
    ImGui::SetNextWindowPos    (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    constexpr char* titleRemove =  "Remove category###PopupCategoryModify";
    constexpr char* titleMerge  = "Merge categories###PopupCategoryModify";
    constexpr char* titleRename =  "Rename category###PopupCategoryModify";

    if (ImGui::BeginPopupModal ( (static_category.remove) ? titleRemove :
                                 (static_category.exists) ? titleMerge  :
                                                            titleRename,
                                    nullptr,
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove   |
                                    ImGuiWindowFlags_AlwaysAutoResize )
        )
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###PopupCategoryModify");
      if (window != nullptr && ! window->Appearing)
        PopupCategoryModify = PopupState_Opened;

      SKIF_ImGui_Spacing ( );

      static char charCategoryRename[maxCategoryNameLen] = {  };
      static constexpr char* renameInputID = "###PopupCategoryModifyRename";
      static bool hasFocus    = false;
      static bool renamedName = false;

      if (static_category.remove)
      {
        ImGui::Text        ("Are you sure you want to remove the");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_category.Name.c_str());
        ImGui::SameLine    ( );
        ImGui::Text        ("category?");
      }

      else if (static_category.exists)
      {
        ImGui::Text        ("Are you sure you want to merge the");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_category.Name.c_str());
        ImGui::SameLine    ( );
        ImGui::Text        ("category with the");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_category.newName.c_str());
        ImGui::SameLine    ( );
        ImGui::Text        ("category?");
      }

      else if (! static_category.newName.empty())
      {
        ImGui::Text        ("Are you sure you want to rename");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_category.Name.c_str());
        ImGui::SameLine    ( );
        ImGui::Text        ("to");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_category.newName.c_str());
        ImGui::SameLine    ( );
        ImGui::Text        ("?");
      }

      else
      {
        ImGui::Text        ("What do you want to rename the");
        ImGui::SameLine    ( );
        ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_category.Name.c_str());
        ImGui::SameLine    ( );
        ImGui::Text        ("category to?");

        SKIF_ImGui_Spacing ( );

        if (ImGui::InputTextEx (renameInputID, static_category.Name.c_str(), charCategoryRename, maxCategoryNameLen,
                        ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, 0.0f), ImGuiInputTextFlags_None))
        {
          renamedName = (charCategoryRename[0] != '\0' && strncmp (static_category.Name.c_str(), charCategoryRename, maxCategoryNameLen) != 0);
        }

        else if (! hasFocus)
        {
          strncpy (charCategoryRename, static_category.Name.c_str(), maxCategoryNameLen);

          hasFocus    = true;
          ImGui::ActivateItemByID (ImGui::GetID (renameInputID));
        }
      }

      SKIF_ImGui_Spacing ( );
      ImGui::Text        ("This will affect all games that makes use of the category.");
      SKIF_ImGui_Spacing ( );

      float fX = (ImGui::GetContentRegionAvail().x - 200 * SKIF_ImGui_GlobalDPIScale) / 2;

      ImGui::SetCursorPosX (fX);

      if (static_category.rename && ! renamedName)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::Button ("Yes", ImVec2 (  100 * SKIF_ImGui_GlobalDPIScale,
                                           25 * SKIF_ImGui_GlobalDPIScale )))
      {
        if (static_category.rename)
        {
          hasFocus                = false;
          renamedName             = false;
          static_category.newName = charCategoryRename;
          strncpy(charCategoryRename, "\0", maxCategoryNameLen);
          static_category.exists = (std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == static_category.newName; }) != _registry.vecCategories.end());
        }

        if (! static_category.rename || (static_category.rename && ! static_category.exists))
        {
          std::scoped_lock jsonLock (_games.GetMetaDBMutex ( ));

          // Go through the whole metadata DB (db.json) (even entries that is not a part of the app list)
          //   and update/clear out the entries where the current category that we are working with is set
          SKIF_GamingCollection::Get ( ).MetaDB_SetCategoryName (static_category.Name, static_category.newName);

          // Update the app list
          for (auto& app : _games.GetApps())
          {
            if (app.second->skif.category == static_category.Name)
              app.second->skif.category    = static_category.newName;
          }

          SKIF_GamingCollection::SortApps (&_games.GetApps());

          // If the category is being removed, or merged, delete the existing entry
          if (static_category.newName.empty() || static_category.exists)
            std::erase_if(_registry.vecCategories, [&](const SKIF_RegistrySettings::category_s& category) { return category.name == static_category.Name; });

          // If it is being renamed, rename it
          else
          {
            for (auto& category : _registry.vecCategories)
            {
              if (category.name == static_category.Name)
                category.name    = static_category.newName;
            }

            // Sort the list of categories
            _registry.vecCategories = _registry.SortCategories (_registry.vecCategories);
          }

          // Update the registry
          std::vector<std::wstring> _inNames, _inBools;
          for (auto& category : _registry.vecCategories)
          {
            _inNames.push_back (SK_UTF8ToWideChar (category.name));
            _inBools.push_back (std::to_wstring   (category.expanded));
          }

          _registry.regKVCategories.     putDataMultiSZ (_inNames);
          _registry.regKVCategoriesState.putDataMultiSZ (_inBools);

          hasFocus        = false;
          static_category = change_category_s { };
          ImGui::CloseCurrentPopup ( );
        }

        if (static_category.rename)
          static_category.rename = false;
      }

      if (static_category.rename && ! renamedName)
        SKIF_ImGui_PopDisableState ( );

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        hasFocus        = false;
        renamedName     = false;
        static_category = change_category_s { };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::EndPopup ( );
    }
  }
  else {
    PopupCategoryModify = PopupState_Closed;
  }

  // End Task confirmation prompt

  if (static_proc.handle != INVALID_HANDLE_VALUE ||
      static_proc.pid    != 0)
  {
    ImGui::OpenPopup         ("###TaskManagerLibrary");

    static std::string warning = "Any unsaved game data may be lost.";
    float fXwarning = ImGui::CalcTextSize (warning.c_str()).x; // DPI related so cannot be static

    ImGui::SetNextWindowSize (
      ImVec2 (
        std::max (ImGui::CalcTextSize (static_proc.name.c_str()).x + 170.0f * SKIF_ImGui_GlobalDPIScale,
                  fXwarning + ImGui::GetStyle().ItemSpacing.x * 2),
        0.0f
      )
    );
    ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal ( "Task Manager###TaskManagerLibrary", nullptr,
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove   |
                                    ImGuiWindowFlags_AlwaysAutoResize )
        )
    {

      ImGui::Text        ("Do you want to end");
      ImGui::SameLine    ( );
      ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_proc.name.c_str());
      ImGui::SameLine    ( );
      ImGui::Text        ("?");
      SKIF_ImGui_Spacing ( );


      float fX = (ImGui::GetContentRegionAvail().x - fXwarning) / 2 + ImGui::GetStyle().ItemSpacing.x;
      ImGui::SetCursorPosX (fX);
      ImGui::Text        (warning.c_str());

      SKIF_ImGui_Spacing ( );

      fX = (ImGui::GetContentRegionAvail().x - 200 * SKIF_ImGui_GlobalDPIScale) / 2;

      ImGui::SetCursorPosX (fX);

      if (ImGui::Button ("End Process", ImVec2 (  100 * SKIF_ImGui_GlobalDPIScale,
                                                   25 * SKIF_ImGui_GlobalDPIScale )))
      {
        if (static_proc.handle != INVALID_HANDLE_VALUE)
          SKIF_Util_TerminateProcess (static_proc.handle, 0x0);
        else
          SKIF_Util_TerminateProcess (static_proc.pid,    0x0);

        static_proc = terminate_process_s { };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_proc = terminate_process_s { };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::EndPopup ( );
    }
  }

#pragma endregion
}

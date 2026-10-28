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
#include <tabs/properties.h>

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
  if (SKIF_Custom_RemoveGame (selection.game.appid))
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

  // Popup: Properties (library_properties.cpp)
  SKIF_Properties_Draw ( );



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
          strncpy_s (charCategoryRename, static_category.Name.c_str(), _TRUNCATE); // Always null-terminated

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
          _inNames.reserve (_registry.vecCategories.size ( ));
          _inBools.reserve (_registry.vecCategories.size ( ));
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

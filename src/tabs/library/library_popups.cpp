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

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

#pragma region Library View Onboarding

// A clickable card of the library view onboarding popup: a sketch of the view (the games list on the right, the cover or
//   the grid of covers on the left), its name and a description. Returns true when clicked.
static bool
DrawLibraryViewCard (const char* id, bool grid, const char* icon, const char* title, const char* desc, ImVec2 size)
{
  const float       dpi   = SKIF_ImGui_GlobalDPIScale;
  const ImGuiStyle& style = ImGui::GetStyle ( );
  const float       pad   = 10.0f * dpi;
  const float       illH  = 90.0f * dpi;

  const ImVec2 pos     = ImGui::GetCursorScreenPos ( );
  const bool   clicked = ImGui::InvisibleButton (id, size);
  const bool   hovered = ImGui::IsItemHovered   ( ) || ImGui::IsItemFocused ( );

  SKIF_ImGui_BlockDragMoveWhenItemActive ( );

  if (ImGui::IsItemHovered ( ))
    SKIF_ImGui_SetMouseCursorHand ( );

  ImDrawList* dl = ImGui::GetWindowDrawList ( );

  const ImU32 colBg      = ImGui::GetColorU32 ((hovered) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
  const ImU32 colBorder  = (hovered) ? ImGui::GetColorU32 (ImGuiCol_SKIF_TextCaption)
                                     : ImGui::GetColorU32 (ImGuiCol_Border);
  const ImU32 colLine    = ImGui::GetColorU32 (ImGuiCol_SKIF_TextBase,    0.35f);
  const ImU32 colAccent  = ImGui::GetColorU32 (ImGuiCol_SKIF_TextCaption, 0.75f);
  const ImU32 colFill    = ImGui::GetColorU32 (ImGuiCol_SKIF_TextCaption, 0.20f);

  const ImVec2 max = ImVec2 (pos.x + size.x, pos.y + size.y);

  dl->AddRectFilled (pos, max, colBg,     style.FrameRounding);
  dl->AddRect       (pos, max, colBorder, style.FrameRounding, ((hovered) ? 2.0f : 1.0f) * dpi);

  // The sketch: a window with the cover (or the grid) on the left and the games list on the right
  const ImVec2 il0 = ImVec2 (pos.x + pad,          pos.y + pad);
  const ImVec2 il1 = ImVec2 (pos.x + size.x - pad, pos.y + pad + illH);
  const float  ip  = 6.0f * dpi;

  dl->AddRect (il0, il1, colLine, 3.0f * dpi, 1.0f * dpi);

  const float paneX0 = il0.x + ip;
  const float paneX1 = il0.x + (il1.x - il0.x) * 0.62f;
  const float paneY0 = il0.y + ip;
  const float paneY1 = il1.y - ip;
  const float paneH  = paneY1 - paneY0;

  if (! grid)
  {
    // One large cover (2:3)
    const float w = ImMin (paneH * 2.0f / 3.0f, paneX1 - paneX0);
    dl->AddRectFilled (ImVec2 (paneX0, paneY0), ImVec2 (paneX0 + w, paneY1), colFill,   2.0f * dpi);
    dl->AddRect       (ImVec2 (paneX0, paneY0), ImVec2 (paneX0 + w, paneY1), colAccent, 2.0f * dpi, 1.0f * dpi);
  }

  else
  {
    // Two rows of small covers (2:3), the first one selected
    const float th   = (paneH - ip) / 2.0f;
    const float tw   = th * 2.0f / 3.0f;
    const int   cols = ImMax (1, (int)((paneX1 - paneX0 + ip) / (tw + ip)));

    for (int row = 0; row < 2; row++)
    {
      for (int col = 0; col < cols; col++)
      {
        const ImVec2 t0 = ImVec2 (paneX0 + col * (tw + ip), paneY0 + row * (th + ip));
        const ImVec2 t1 = ImVec2 (t0.x + tw, t0.y + th);

        dl->AddRectFilled (t0, t1, colFill, 2.0f * dpi);
        dl->AddRect       (t0, t1, (row == 0 && col == 0) ? colAccent : colLine, 2.0f * dpi, 1.0f * dpi);
      }
    }
  }

  // The games list, the first game selected
  static constexpr float widths[] = { 0.85f, 0.65f, 0.95f, 0.55f, 0.80f, 0.70f, 0.90f, 0.60f };
  const float listX0 = paneX1 + ip * 1.5f;
  const float listW  = il1.x - ip - listX0;
  const float barH   = 4.0f * dpi;
  const float barGap = 6.0f * dpi;

  int bar = 0;
  for (float y = paneY0; y + barH <= paneY1 && bar < IM_ARRAYSIZE (widths); y += barH + barGap, bar++)
    dl->AddRectFilled (ImVec2 (listX0, y), ImVec2 (listX0 + listW * widths [bar], y + barH), (bar == 0) ? colAccent : colLine, barH * 0.5f);

  // The name and description
  ImFont*     font     = ImGui::GetFont     ( );
  const float fontSize = ImGui::GetFontSize ( );

  const std::string label = std::string (icon) + "  " + title;
  ImVec2 txt = ImVec2 (pos.x + pad, il1.y + pad);

  dl->AddText (font, fontSize, txt, ImGui::GetColorU32 (ImGuiCol_SKIF_TextCaption), label.c_str ( ));

  txt.y += ImGui::GetTextLineHeight ( ) + style.ItemSpacing.y;

  dl->AddText (font, fontSize, txt, ImGui::GetColorU32 (ImGuiCol_SKIF_TextBase), desc, nullptr, size.x - pad * 2.0f);

  return clicked;
}

// Popup: the first time the Library tab is shown, explains the two views of the library (Cover view: the games list
//   beside the cover of the selected game; Grid view: beside a grid of all the covers) and asks which one to use.
//   Not shown to those who already picked one through the Filter menu (the Library View value exists; registry.cpp).
static void
DrawLibraryViewOnboardingPopup (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  static bool showing = false;

  if (_registry.bLibraryViewChosen)
    return;

  // Not on the frames the tab is being left (the first launch switches to the About tab), and not over another popup
  //   Not shown while the tab is left: when back, it waits again for any other popup to close first
  if (! SKIF_StartupFinished || SKIF_Tab_Selected != UITab_Library || SKIF_Tab_ChangeTo != UITab_None)
  {
    showing = false;
    return;
  }

  if (! showing && SKIF_ImGui_IsAnyPopupOpen ( ))
    return;

  const float       dpi     = SKIF_ImGui_GlobalDPIScale;
  const ImGuiStyle& style   = ImGui::GetStyle ( );
  const float       pad     = 10.0f * dpi;
  const float       cardW   = 240.0f * dpi;
  const float       gap     = style.ItemSpacing.x * 2.0f;
  const float       totalW  = cardW * 2.0f + gap;

  const char* descCover = _("A large cover of the selected game beside the games list, with its details and launch options. "
                            "Best for focusing on one game at a time.");
  const char* descGrid  = _("The covers of all your games as a grid beside the games list. Best for browsing a large library "
                            "at a glance. The size of the covers, their titles and platform badges can be adjusted.");

  // Both cards as tall as the taller one
  const float descH = ImMax (ImGui::CalcTextSize (descCover, nullptr, false, cardW - pad * 2.0f).y,
                             ImGui::CalcTextSize (descGrid,  nullptr, false, cardW - pad * 2.0f).y);
  const ImVec2 cardSize = ImVec2 (cardW, pad + 90.0f * dpi + pad + ImGui::GetTextLineHeight ( ) + style.ItemSpacing.y + descH + pad);

  int choice = -1;

  showing = SKIF_ImGui_BeginPopup (L_("Choose a library view###PopupLibraryView"));

  if (showing)
  {
    ImGui::PushTextWrapPos (ImGui::GetCursorPosX ( ) + totalW);
    ImGui::TextUnformatted (_("Your games can be shown in one of two views. Which one would you like to use?"));
    ImGui::PopTextWrapPos  ( );

    SKIF_ImGui_Spacing ( );

    if (DrawLibraryViewCard ("###LibraryViewCover", false, ICON_FA_IMAGE,             _("Cover view"), descCover, cardSize))
      choice = 0;

    ImGui::SameLine (0.0f, gap);

    if (DrawLibraryViewCard ("###LibraryViewGrid",  true,  ICON_FA_TABLE_CELLS_LARGE, _("Grid view"),  descGrid,  cardSize))
      choice = 1;

    SKIF_ImGui_Spacing ( );

    // TRANSLATORS: {0} is the icon of the Filter button above the games list of the library
    SKIF_ImGui_TextHighlighted (_("You can switch at any time through the {0} button above the games list."), ICON_FA_FILTER, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));

    // Escape keeps the current view (the Cover view by default), and the popup is not shown again
    if (choice == -1 && ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed (ImGuiKey_Escape, false))
      choice = _registry.iLibraryView;
  }

  SKIF_ImGui_EndPopup ( ); // Must always be called, regardless of the return value of SKIF_ImGui_BeginPopup

  if (choice != -1)
  {
    PLOG_INFO << "Library view chosen through the onboarding popup: " << ((choice == 1) ? "Grid" : "Cover");

    _registry.iLibraryView       = choice;
    _registry.bLibraryViewChosen = true;
    _registry.regKVLibraryView      .putData (_registry.iLibraryView);
    _registry.regKVLibraryViewChosen.putData (_registry.bLibraryViewChosen);

    showing = false;
  }
}

#pragma endregion

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

  // Properties: a window of its own, drawn after the main window (SKIF_Properties_Draw in SKIF.cpp)

  // Popup: Scanning game folder... (a launch waiting for the game's first scan; library_launch.cpp)
  DrawLaunchScanPopup ( );

  // Popup: Choose a library view (the first time the tab is shown)
  DrawLibraryViewOnboardingPopup ( );



  // End Task confirmation prompt

  if (static_category.change)
  {
    ImGui::OpenPopup           ("###PopupCategoryModify");
    ImGui::SetNextWindowSize   (ImVec2 (0.0f, 0.0f));
    ImGui::SetNextWindowPos    (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    const char* titleRemove = L_( "Remove category###PopupCategoryModify");
    const char* titleMerge  = L_("Merge categories###PopupCategoryModify");
    const char* titleRename = L_( "Rename category###PopupCategoryModify");

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
        // TRANSLATORS: {0} is the name of a category of the library
        SKIF_ImGui_TextHighlighted (_("Are you sure you want to remove the {0} category?"), { static_category.Name.c_str() }, ImColor::HSV (0.11F, 1.F, 1.F));
      }

      else if (static_category.exists)
      {
        // TRANSLATORS: {0} and {1} are the names of two categories of the library
        SKIF_ImGui_TextHighlighted (_("Are you sure you want to merge the {0} category with the {1} category?"), { static_category.Name.c_str(), static_category.newName.c_str() }, ImColor::HSV (0.11F, 1.F, 1.F));
      }

      else if (! static_category.newName.empty())
      {
        // TRANSLATORS: {0} is the current name of a category of the library, {1} the new one
        SKIF_ImGui_TextHighlighted (_("Are you sure you want to rename {0} to {1}?"), { static_category.Name.c_str(), static_category.newName.c_str() }, ImColor::HSV (0.11F, 1.F, 1.F));
      }

      else
      {
        // TRANSLATORS: {0} is the name of a category of the library
        SKIF_ImGui_TextHighlighted (_("What do you want to rename the {0} category to?"), { static_category.Name.c_str() }, ImColor::HSV (0.11F, 1.F, 1.F));

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
      ImGui::TextUnformatted (_("This will affect all games that make use of the category."));
      SKIF_ImGui_Spacing ( );

      const float fButtonWidth = std::max ({ 100.0f * SKIF_ImGui_GlobalDPIScale,
                                             ImGui::CalcTextSize (L_("Yes"),    nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                             ImGui::CalcTextSize (L_("Cancel"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f });

      float fX = (ImGui::GetContentRegionAvail().x - fButtonWidth * 2.0f) / 2;

      ImGui::SetCursorPosX (fX);

      if (static_category.rename && ! renamedName)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::Button (L_("Yes"), ImVec2 (  fButtonWidth,
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

      if (ImGui::Button (L_("Cancel"), ImVec2 ( fButtonWidth,
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
    // TRANSLATORS: {0} is the name of a game
    const char* question = _("Do you want to end {0}?");
    const char* warning  = _("Any unsaved game data may be lost.");
    float fXwarning = ImGui::CalcTextSize (warning).x; // DPI related so cannot be static

    const float fButtonWidth = std::max ({ 100.0f * SKIF_ImGui_GlobalDPIScale,
                                           ImGui::CalcTextSize (L_("End Process"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                           ImGui::CalcTextSize (L_("Cancel"),      nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f });

    const float fPopupWidth  = std::max ({ ImGui::CalcTextSize (SKIF_L10n_Format (question, nullptr, static_proc.name).c_str ()).x + 20.0f * SKIF_ImGui_GlobalDPIScale,
                                           fXwarning + ImGui::GetStyle().ItemSpacing.x * 2,
                                           fButtonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x * 4 });

    // Centered with a dimmed background (as the Task Manager of the Monitor tab), and closed by resetting static_proc
    if (SKIF_ImGui_BeginPopup (L_("Task Manager###TaskManagerLibrary"), nullptr, ImVec2 (fPopupWidth, 0.0f)))
    {
      SKIF_ImGui_TextHighlighted (question, { static_proc.name.c_str() }, ImColor::HSV (0.11F, 1.F, 1.F));
      SKIF_ImGui_Spacing ( );


      float fX = (ImGui::GetContentRegionAvail().x - fXwarning) / 2 + ImGui::GetStyle().ItemSpacing.x;
      ImGui::SetCursorPosX (fX);
      ImGui::TextUnformatted (warning);

      SKIF_ImGui_Spacing ( );

      fX = (ImGui::GetContentRegionAvail().x - fButtonWidth * 2.0f) / 2;

      ImGui::SetCursorPosX (fX);

      if (ImGui::Button (L_("End Process"), ImVec2 (  fButtonWidth,
                                                   25 * SKIF_ImGui_GlobalDPIScale )))
      {
        if (static_proc.handle != INVALID_HANDLE_VALUE)
          SKIF_Util_TerminateProcess (static_proc.handle, 0x0);
        else
          SKIF_Util_TerminateProcess (static_proc.pid,    0x0);

        static_proc = terminate_process_s { };
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button (L_("Cancel"), ImVec2 ( fButtonWidth,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_proc = terminate_process_s { };
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );

      // Escape cancels, as it would a regular popup
      if (ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed (ImGuiKey_Escape, false))
        static_proc = terminate_process_s { };
    }

    SKIF_ImGui_EndPopup ( ); // Must always be called, regardless of the return value of SKIF_ImGui_BeginPopup
  }

#pragma endregion
}

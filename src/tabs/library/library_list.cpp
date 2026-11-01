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

// Category headers, shared by the games list and the grid view. In the grid view both draw them in the same frame (the
//   grid first): BeginCategoryHeaders ( ) only starts a frame once, and the list calls EndCategoryHeaders ( ).
static int  s_applyHeaderState = 0;     // Expand all / Collapse all: the frame it was chosen in
static bool s_newHeaderState   = false; // ... and what was chosen
static bool s_categoryMenuOpen = false; // A category's menu was open this frame
static int  s_headersFrame     = -1;    // The frame the headers were begun in

void
BeginCategoryHeaders (void)
{
  const int frame = ImGui::GetFrameCount ( );

  if (frame == s_headersFrame)
    return;

  s_headersFrame     = frame;
  s_newHeaderState   = false;
  s_categoryMenuOpen = false;

  // Expand all / Collapse all is applied to the headers of every view in the frame after it was chosen
  if (s_applyHeaderState > 0 && frame > s_applyHeaderState + 1)
    s_applyHeaderState = 0;
}

// The header of a category, with its menu (Rename, Remove, Expand all, ...). Returns whether the category is expanded.
//   categories: how many headers were drawn before this one (popup IDs)
bool
DrawCategoryHeader (const std::string& tmpCategory, int categories, bool showClearBtn)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static library_selection_s&   selection   = GetLibraryTab ( ).selection;

  bool category_opened = false;

  auto it = std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == tmpCategory; });

  // Always expand a category if the selected game was changed, or type-to-search found a game in it
  bool forceOpen = ((sort_changed && selection.category == tmpCategory) || search_selection.category == tmpCategory);

  // The category's expanded state (vecCategories, which has every category) is what is shown: in the grid view the
  //   grid and the list both draw the header, and each has its own open state in ImGui, so they follow this one.
  //   A header the user toggles updates it below.
  if (it != _registry.vecCategories.end())
  {
    if (forceOpen)
      it->expanded = true;

    ImGui::SetNextItemOpen (it->expanded);
  }

  else if (forceOpen)
    ImGui::SetNextItemOpen (true);

  if (! _registry._StyleLightMode)
  {
    ImGui::PushStyleColor (ImGuiCol_Header, ImGui::GetStyleColorVec4 (ImGuiCol_Header) * ImVec4 (0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::PushStyleColor (ImGuiCol_Text,   ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
  }

  // The built-in categories are kept in English (the registry, comparisons), and translated where shown; the names of
  //   the categories of the user are shown as is
  const char* szCategoryHeader =
    (tmpCategory == "Favorites") ? CL_("category", "Favorites") :
    (tmpCategory == "Games")     ? CL_("category", "Games")     :
                                   tmpCategory.c_str ( );

  category_opened = ImGui::CollapsingHeader (szCategoryHeader, (showClearBtn) ? ImGuiTreeNodeFlags_DefaultOpen : 0); // ImGuiTreeNodeFlags_DefaultOpen

  if (it != _registry.vecCategories.end())
    it->expanded = category_opened;

  if (! _registry._StyleLightMode)
    ImGui::PopStyleColor  (2);

  if (CategoryMenu == PopupState_Closed              &&
      ImGui::IsItemHovered ( )                       &&
      ImGui::IsMouseClicked (ImGuiMouseButton_Right) &&
    ! SKIF_ImGui_IsAnyPopupOpen ( ))
    CategoryMenu = PopupState_Open;

  if (CategoryMenu == PopupState_Open)
    ImGui::OpenPopup (SKIF_Util_FormatStringRaw ("###Popup-%i", categories));

  if (ImGui::BeginPopup (SKIF_Util_FormatStringRaw ("###Popup-%i", categories), ImGuiWindowFlags_NoMove))
  {
    CategoryMenu = PopupState_Opened;
    s_categoryMenuOpen = true;

    ImGui::PushStyleColor  (ImGuiCol_NavCursor, ImVec4(0,0,0,0));

    if (tmpCategory != "Games" && tmpCategory != "Favorites")
    {
      if (ImGui::Selectable ((std::string (_("Rename")) + SKIF_Util_FormatStringRaw ("###PopupRename-%i", categories)).c_str ( )))
      {
        static_category.Name   = tmpCategory;
        static_category.change = true;
        static_category.rename = true;
        ImGui::CloseCurrentPopup ( );
      }

      if (ImGui::Selectable ((std::string (_("Remove")) + SKIF_Util_FormatStringRaw ("###PopupRemove-%i", categories)).c_str ( )))
      {
        static_category.Name   = tmpCategory;
        static_category.change = true;
        static_category.remove = true;

        ImGui::CloseCurrentPopup ( );
      }

      ImGui::Separator ( );
    }

    if (ImGui::Selectable ((std::string (_("Expand all")) + SKIF_Util_FormatStringRaw ("###PopupExpand-%i", categories)).c_str ( )))
    {
      s_applyHeaderState = ImGui::GetFrameCount ( );
      s_newHeaderState = true;

      ImGui::CloseCurrentPopup ( );
    }

    if (ImGui::Selectable ((std::string (_("Collapse all")) + SKIF_Util_FormatStringRaw ("###PopupCollapse-%i", categories)).c_str ( )))
    {
      s_applyHeaderState = ImGui::GetFrameCount ( );
      s_newHeaderState = false;

      ImGui::CloseCurrentPopup ( );
    }

    if (ImGui::MenuItem (L_("Remember collapsible state###PopupRememberState"), "", &_registry.bRememberCategoryState))
      _registry.regKVRememberCategoryState.putData (_registry.bRememberCategoryState);

    ImGui::PopStyleColor ( );

    ImGui::EndPopup ( );
  }

  return category_opened;
}

void
EndCategoryHeaders (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // Expand all / Collapse all was chosen this frame (the headers follow next frame; reset by BeginCategoryHeaders)
  if (s_applyHeaderState > 0 && ImGui::GetFrameCount() == s_applyHeaderState)
  {
    for (auto& category : _registry.vecCategories)
      category.expanded = s_newHeaderState;
  }

  if (! s_categoryMenuOpen)
    CategoryMenu = PopupState_Closed;
}

// The games list: categories, entries, selection, icon workers (moved from SKIF_UI_Tab_DrawLibrary; rows clipped since 2026-10-31)
void
DrawGamesList (DWORD current_time, ImVec2 fTop1, bool showClearBtn, ImVec2& fTop3, ImVec2& fTop3ScreenPos)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static library_selection_s& selection    = GetLibraryTab ( ).selection;
  static library_selection_s& item_clicked = GetLibraryTab ( ).item_clicked;
  static app_record_s*&       pApp         = GetLibraryTab ( ).pApp;
  static bool&                update       = GetLibraryTab ( ).update;
  static ImVec2&              sizeList     = GetLibraryTab ( ).sizeList;

  bool isSpecialK = (pApp != nullptr && pApp->IsSpecialK ( )); // pApp only changes once per frame, at the top of SKIF_UI_Tab_DrawLibrary ( )

#pragma region GamesList

  auto _HandleItemSelection = [&](app_record_s* selected_app, bool isIconMenu = false) -> bool
  {
    extern bool
        bAutoScrollActive;
    if (bAutoScrollActive)
      return false;

    // ImGui::IsItemFocused ( ) cannot be used here as it requires two button inputs to trigger on game change
    bool isFocused = (ImGui::GetFocusID ( ) == ImGui::GetID (selected_app->ImGuiLabelID.c_str()));

    static constexpr float _LONG_INTERVAL = .25f;

    bool _NavLongActivate =
           isFocused && ( ( ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration     >= _LONG_INTERVAL   &&
                            ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDurationPrev <= _LONG_INTERVAL ) ||
                          ( ImGui::GetKeyData (ImGuiKey_Enter          )->DownDuration     >= _LONG_INTERVAL   &&
                            ImGui::GetKeyData (ImGuiKey_Enter          )->DownDurationPrev <= _LONG_INTERVAL ) );

    bool ret =
      ImGui::IsItemActivated (                      ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Left ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Right);

    // If the item is activated, but not visible, scroll to it
    if (ret)
    {
      if (! ImGui::IsItemVisible    (    )) {
        ImGui::SetScrollHereY       (0.5f);
      }
    }

    if (isIconMenu)
    {
      if ( IconMenu == PopupState_Closed &&
           ImGui::IsItemClicked (ImGuiMouseButton_Right))
           IconMenu = PopupState_Open;
    }

    else {
      if (GameMenu == PopupState_Closed)
      {
        if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
             _NavLongActivate)
        {
          GameMenu = PopupState_Open;
        }
      }
    }

    return ret;
  };

  ImGuiChildFlags  flags_cld = ImGuiChildFlags_AlwaysUseWindowPadding;
  ImGuiWindowFlags flags_wnd = ImGuiWindowFlags_None;

  if (! _registry._LibHorizonMode)
    flags_cld |= ImGuiChildFlags_NavFlattened;

  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));

  ImGui::BeginGroup          ( ); // Start GamesList

  fTop3                 = ImGui::GetCursorPos ( );
  fTop3ScreenPos        = ImGui::GetCursorScreenPos ( );

  float _ICON_HEIGHT_BASE  = (_registry._TouchDevice || _registry.bUILargeIcons) ? 32.0f : 24.0f;
//float _ICON_HEIGHT_SCALE = (_registry._TouchDevice ? 1.0f : (ImGui::GetTextLineHeightWithSpacing () / _ICON_HEIGHT_BASE));
//float _ICON_HEIGHT       = std::min (1.0f, std::max (0.1f, _ICON_HEIGHT_SCALE)) * _ICON_HEIGHT_BASE;
  float _ICON_HEIGHT       = _ICON_HEIGHT_BASE * SKIF_ImGui_GlobalDPIScale;

  // Without icons (Show icons), a row is the height of a frame, and the title takes the whole width
  const bool showIcons     = _registry.bLibraryShowIcons;

  if (! showIcons)
    _ICON_HEIGHT           = ImGui::GetFrameHeight ( );

  const float widthIcon    = (showIcons) ? _ICON_HEIGHT + ImGui::GetStyle().ItemSpacing.x : 0.0f;

  float _ICON_HEIGHT_OnTop = _ICON_HEIGHT + ImGui::GetStyle().ItemSpacing.y;

  ImGui::BeginChild          ( "###GameListOnTop",
                                ImFloor (ImVec2 ((sizeList.x - ImGui::GetStyle().WindowPadding.x / 2.0f),
                                        (! _registry._LibHorizonMode && _registry._LibPinnedVisible && numPinnedOnTop > 0 && numRegular > 0)
                                          ? numPinnedOnTop * _ICON_HEIGHT_OnTop + ImGui::GetStyle().WindowPadding.y
                                          : sizeList.y - (fTop3.y - fTop1.y))), //  - (ImGui::GetStyle().FramePadding.x - 2.0f * SKIF_ImGui_GlobalDPIScale)
                                  flags_cld | (_registry.bUIBorders ? ImGuiChildFlags_Borders : ImGuiChildFlags_None),
                                  flags_wnd | ((! _registry._LibHorizonMode && _registry._LibPinnedVisible && numPinnedOnTop > 0 && numRegular > 0) ? (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) : ImGuiWindowFlags_None));

  // Start populating the list

  if (_games.GetApps().empty())
  {
    // Faded in, like the games list once it is loaded
    ImGui::PushStyleVar    (ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * LibraryFadeIn (fadeLoadingList));
    ImGui::Selectable      (L_("Loading games...###GamesCurrentlyLoading"), false, ImGuiSelectableFlags_Disabled);
    ImGui::PopStyleVar     ( );
  }

  std::string current_category = "";
  int         categories       = 0;
  int         pinned_top       = 0;
  bool        resetNumOnTop    = true;


  float maxWidth    = (SKIF_ImGui_GetContentRegionMax ( ).x - widthIcon);

  BeginCategoryHeaders ( );

  // Rows are drawn through ImGuiListClipper, one section at a time: the pinned games on top, and then each expanded
  //   category. The rows of a section are collected while walking the library, and drawn once the section ends (before
  //     the next category header, before switching from the pinned games to the rest, and after the last game). Rows
  //       out of view are not submitted, except the selected game and the game found by type-to-search: their rows act
  //         on their own (scroll to the game, activate its row; see sort_changed and search_selection). The clipper
  //           itself keeps the row under keyboard / gamepad navigation, and the rows a navigation move goes to.
  //  All rows of a section have the same height (the icon), which the clipper measures from the first row.
  static DWORD timeClicked = 0;

  static std::vector <app_record_s*> sectionRows; // Static to keep its memory between frames
  sectionRows.clear ( );

  // One row of the list: the icon and the title (Selectable)
  auto _DrawGameRow = [&](app_record_s* rec)
  {
    bool selected = (selection.game == rec->GetKey ( ));
    bool change   = false;

    //if (_registry._TouchDevice)
    //  ImGui::SetCursorPosY (ImGui::GetCursorPosY() + 15.0f);

    float fOriginalY =
      ImGui::GetCursorPosY ();


    // Start Icon + Selectable row

    ImGui::BeginGroup      ();
    ImGui::PushID          (rec->ImGuiPushID.c_str());

    if (_registry.bFadeCovers)
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, fAlphaList);

    if (showIcons)
    {
      SKIF_ImGui_OptImage    (rec->tex_icon.iWorker == 2 ? rec->tex_icon.texture.p : nullptr,
                                ImVec2 ( _ICON_HEIGHT,
                                         _ICON_HEIGHT )
                              );

      change |=
        _HandleItemSelection (rec, true);

      ImGui::SameLine        ();
    }

    ImVec4 _color =
      ( rec->_status.updating != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.6f, .6f, 1.f)) :
      ( rec->_status.running  != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.3f, 1.f, 1.f)) :
                    ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Game Title
    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::PushStyleColor  (ImGuiCol_NavCursor, ImVec4(0,0,0,0));
    SKIF_ImGui_SelectableVAligned (rec->ImGuiLabelID.c_str(), rec->names.normal.c_str(), &selected, ImGuiSelectableFlags_None, ImVec2(maxWidth, _ICON_HEIGHT));
    ImGui::PopStyleColor   (2);

    if (_registry.bFadeCovers)
      ImGui::PopStyleVar ( ); // -ImGuiStyleVar_Alpha

    // Handle double click on a game row
    if ( ImGui::IsItemHovered ( ) && pApp != nullptr && pApp->id != SKIF_STEAM_APPID && ! pApp->_status.running && ! pApp->IsUpdateBlockingLaunch ( ) )
    {
      if ( ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) &&
           timeClicked != 0 && (item_clicked.game == pApp->GetKey ( )))
      {
        timeClicked       = 0;
        item_clicked.reset ( );
        launchGame = true;
      }

      else if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) )
      {
        timeClicked        = SKIF_Util_timeGetTime ( );
        item_clicked.game  = pApp->GetKey ( );
      }

      else if (timeClicked + 500 < SKIF_Util_timeGetTime ( ))
      {
        // Reset after 500 ms
        timeClicked = 0;
        item_clicked.reset ( );
      }
    }

    // Show full title in tooltip if the title spans longer than the width of the Selectable row
    if (ImGui::IsItemHovered ( ) && ImGui::CalcTextSize  (rec->names.normal.c_str()).x >= (maxWidth - ImGui::GetStyle().ItemSpacing.x))
      SKIF_ImGui_SetHoverTip (rec->names.normal);

    // Handle search input
    if (search_selection.game == rec->GetKey ( ))
    {
      // Set focus on current row
      ImGui::ActivateItemByID (ImGui::GetID(rec->ImGuiLabelID.c_str()));
      ImGui::SetFocusID       (ImGui::GetID(rec->ImGuiLabelID.c_str()), ImGui::GetCurrentWindow());

      // Clear stuff
      selection.game         = { };
      selection.category     = "Games";
      search_selection.game  = { };
      search_selection.category = "";
      change                 = true;
    }

    change |=
      _HandleItemSelection (rec);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::PopID           ();
    ImGui::EndGroup        ();

    // End Icon + Selectable row


    if ( rec->GetKey ( ) == selection.game &&
                   sort_changed )
    {
      sort_changed = false;

      // Special handling for Special K to reset the scroll pos
      if (isSpecialK)
      {
        selection.reset ( );
        selection.game     = { };
        selection.category = "Games";
        change             = true;
      }

      // The list was re-sorted: only keep the selected game in view. Going through `change` would treat it as a newly
      //   selected game, reloading its details (pApp->loading empties an open context menu, closing its submenus) and
      //     activating its row
      else
      {
        selection.category = (rec->skif.pinned > 50) ? "Favorites (pinned)" // Same workaround as below
                                                            : GetEffectiveCategory (rec);

        if (! ImGui::IsItemVisible ( ))
          ImGui::SetScrollHereY (0.5f);
      }
    }

    if (change)
    {
      update = (selection.game != rec->GetKey ( ));

      selection.game               =    rec->GetKey ( );
      selection.category           = (  rec->skif.pinned > 50)
                                   ?   "Favorites (pinned)" // Workaround to not expand Favorites tab on launch
                                   :    GetEffectiveCategory (rec);
      selected                     =    true;

      // Only update the last selected value if we're not in hidden view
      if (! _registry._LibraryHidden)
      {
        SetLastSelectedGame (selection.game);
      }

      if (update)
      {
        timeClicked = SKIF_Util_timeGetTime ( );
        item_clicked.game  = selection.game;

        rec->_status.invalidate ();

        if (! ImGui::IsMouseClicked (ImGuiMouseButton_Right))
        {
          // Activate the row of the current game
          ImGui::ActivateItemByID (ImGui::GetID (rec->ImGuiLabelID.c_str()));

          if (! ImGui::IsItemVisible    (    ))
            ImGui::SetScrollHereY       (0.5f);

          //ImGui::SetKeyboardFocusHere (    ); // Disabled after ImGui update since this set the keyboard focus on the next item

          // This fixes ImGui not allowing the GameContextMenu to be opened on first search
          //   without an additional keyboard input
          ImGuiContext& g = *ImGui::GetCurrentContext();
          g.NavCursorVisible = true;
        }

        //ImGui::SetFocusID(ImGui::GetID(app.first.c_str()), ImGui::GetCurrentWindow());
      }
    }

    if (selected)
    {
      static bool  _horizon = _registry._LibHorizonMode;
      static float _scale   = SKIF_ImGui_GlobalDPIScale; // The DPI scale of the main window, as we are within it

      // This allows the scroll to reset on DPI changes, to keep the selected item on-screen
      if (SKIF_ImGui_GlobalDPIScale != _scale ||
          _registry._LibHorizonMode != _horizon)
      {
        ImGui::SetScrollHereY (0.5f);
        _horizon = _registry._LibHorizonMode;
        _scale   = SKIF_ImGui_GlobalDPIScale;
      }
    }

    // Stream the icon in once the game is visible in the list (up to 8 icon workers at once). Not until SKIF has
    //   started: the icons can't be created before the hardware device exists (created after the first present), and
    //   starting the workers would only delay the first present.
    if (showIcons && SKIF_StartupFinished && ImGui::IsItemVisible ( ) && rec->tex_icon.iWorker == 0 && _art.CanStartIconWorker ( ))
      _art.StartIconWorker (rec);
  };

  auto _DrawSectionRows = [&](void)
  {
    if (sectionRows.empty ( ))
      return;

    ImGuiListClipper clipper;
    clipper.Begin ((int)sectionRows.size ( )); // Row height measured from the first row

    for (int i = 0; i < (int)sectionRows.size ( ); i++)
    {
      const app_key_s key = sectionRows[i]->GetKey ( );

      if (key == selection.game || key == search_selection.game)
        clipper.IncludeItemByIndex (i);
    }

    while (clipper.Step ( ))
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        _DrawGameRow (sectionRows [i]);

    clipper.End ( );

    sectionRows.clear ( );
  };

  // Populate the list of games with all recognized games
  for (auto& app : _games.GetApps())
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second->id == 0)
      continue;

    // Count the number of pinned entries separately from filtered entries
    if (app.second->skif.pinned > 50)
      resetNumOnTop = false;

    // Skips those filtered out by an active search field entry
    if (app.second->filtered)
      continue;

    // Separate always on top (>50) from regular pinned (1-50), but only in regular mode
    if (app.second->skif.pinned > 50 && ! _registry._LibHorizonMode && _registry._LibPinnedVisible) //  // ! _registry._LibHorizonMode
      pinned_top++;
    else if (pinned_top > 0)
    {
      // The pinned games on top are done
      _DrawSectionRows ( );

      if (! _registry.bUIBorders)
      {
        ImGui::SetCursorPosY (ImFloor (
          ImGui::GetCursorPosY ( )
            - ImGui::GetStyle().SeparatorSize   // Separator ( ) takes up layout height since ImGui 1.92.7
        ));

        ImGui::Separator ( );
      }

      ImGui::EndChild             ( );

      ImVec2 fTop4 = ImGui::GetCursorPos ( );

      ImGui::BeginChild           ( "###GameList",
                                     ImFloor (ImVec2 ((sizeList.x -  ImGui::GetStyle().WindowPadding.x / 2.0f),
                                                       sizeList.y - (fTop4.y - fTop1.y))), // - (ImGui::GetStyle().FramePadding.x - 2.0f * SKIF_ImGui_GlobalDPIScale)
                                     flags_cld,
                                     flags_wnd );

      maxWidth    = (SKIF_ImGui_GetContentRegionMax ( ).x - widthIcon);

      pinned_top = 0;
    }

    // If we have passed the pinned on top games, order the rest by group
    if (pinned_top == 0)
    {
      static bool category_opened    = false;

      // All favorited games are grouped as such
      std::string tmpCategory = GetEffectiveCategory (app.second.get ( ));

      if (tmpCategory != current_category)
      {
        // The previous category is done
        _DrawSectionRows ( );

        category_opened = DrawCategoryHeader (tmpCategory, categories, showClearBtn);

        current_category = tmpCategory;
        categories++;
      }

      if (categories > 0 && ! category_opened)
        continue;
    }

    // Its client state (running, updating) is refreshed even while the row is out of view, as before clipping
    app.second->RefreshClientState (current_time);

    sectionRows.push_back (app.second.get ( ));
  }

  // The last section
  _DrawSectionRows ( );

  EndCategoryHeaders ( );

  if (resetNumOnTop)
    numPinnedOnTop = 0;

  // Stop populating the list

  // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
  SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

  ImGui::EndChild        ( );
  ImGui::EndGroup        ( ); // End GamesList
  ImGui::PopStyleColor   ( );

#pragma endregion
}

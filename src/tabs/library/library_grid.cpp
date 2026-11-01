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

#include <cmath>
#include <algorithm>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

// The grid view (IsLibraryGridView): the games as a grid of covers, by category like the games list.
//   It takes the place of the selected game's cover (size: the cover's area); the filter row, the games list and the
//   details stay beside it, and the list stays in charge of type-to-search and of keeping the selection in view after a
//   re-sort (it is drawn after the grid). The covers are thumbnails (SKIF_LibraryArtwork::RequestThumbnail), requested
//   as their tiles are drawn. Each section (each expanded category, Favorites first) is drawn through
//   ImGuiListClipper by rows of tiles, like the list's rows.
void
DrawGamesGrid (DWORD current_time, ImVec2 size, bool showClearBtn)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static library_selection_s& selection    = GetLibraryTab ( ).selection;
  static app_record_s*&       pApp         = GetLibraryTab ( ).pApp;
  static bool&                update       = GetLibraryTab ( ).update;

  extern bool                 coverFadeActive;

  const float  dpi          = SKIF_ImGui_GlobalDPIScale;
  const DWORD  now          = SKIF_Util_timeGetTime ( );

  // A tile: the cover (its size is a setting: the Filter menu's slider), inset so the hover / selection highlight frames
  //   it, and the title beneath it (unless turned off). The spacing and the rounding grow with the covers.
  const float  width        = static_cast <float> (std::clamp (_registry.iLibraryGridSize, 80, 240));
  const ImVec2 sizeCover    = ImFloor (ImVec2 (width, width * 1.5f) * dpi);
  const float  inset        = ImFloor (  3.0f * dpi);
  const float  heightTitle  = (_registry.bLibraryGridTitles) ? ImGui::GetTextLineHeight ( ) + inset : 0.0f;
  const ImVec2 sizeTile     = ImVec2 (sizeCover.x + inset * 2.0f, sizeCover.y + inset * 2.0f + heightTitle);
  const float  spacing      = ImFloor (std::max (6.0f, width * 0.08f) * dpi); // Between the rows, and at least between the columns
  const float  rounding     = ImFloor (std::max (3.0f, width * 0.04f) * dpi);

  // The thumbnails are made at the largest cover size (BC7 keeps them small), and drawn smaller through their mips, so the
  //   size slider never reloads or rebuilds them. Only a change of the DPI scale does (another cache file).
  _art.SetThumbnailSize (ImFloor (ImVec2 (240.0f, 360.0f) * dpi));

  // The first frame of the grid (switched to it, or back to the library tab), or another game was selected (e.g. in the
  //   list, or by type-to-search): show the selected game
  static int       lastFrame    = -2;
  static app_key_s lastSelected;
  bool scrollToSelected = (lastFrame != ImGui::GetFrameCount ( ) - 1 || lastSelected != selection.game || sort_changed);
  lastFrame             = ImGui::GetFrameCount ( );
  lastSelected          = selection.game;

  // DPI changes: keep the selected game in view
  static float lastScale = dpi;
  if (lastScale != dpi)
  {
    lastScale        = dpi;
    scrollToSelected = true;
  }

  // Double-click (and the second click of one) on the tile of the selected game
  static app_key_s tileClicked;
  static DWORD     timeClicked = 0;
  static app_key_s tilePressed; // Enter / A was pressed on this tile

  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));

  ImGui::BeginChild          ( "###GameGrid", ImFloor (size),
                                  ImGuiChildFlags_AlwaysUseWindowPadding | ((! _registry._LibHorizonMode) ? ImGuiChildFlags_NavFlattened : ImGuiChildFlags_None), // As the list
                                  ImGuiWindowFlags_NoBackground ); // As the cover

  // As many columns as fit, with the spare width spread between them
  const float avail   = ImGui::GetContentRegionAvail ( ).x;
  const int   columns = std::max (1, static_cast <int> ((avail + spacing) / (sizeTile.x + spacing)));
  const float gap     = (columns > 1) ? std::floor ((avail - columns * sizeTile.x) / (columns - 1))
                                      : spacing;

  if (_games.GetApps().empty())
  {
    // Faded in, like the games list once it is loaded
    ImGui::PushStyleVar    (ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * LibraryFadeIn (fadeLoadingList));
    ImGui::Selectable      (L_("Loading games...###GamesCurrentlyLoading"), false, ImGuiSelectableFlags_Disabled);
    ImGui::PopStyleVar     ( );
  }

  ImDrawList* draw_list = ImGui::GetWindowDrawList ( );

  // The cover of a tile, in rect (min, max): cropped to fill it, as the thumbnails keep their aspect ratio
  auto _DrawCover = [&](ID3D11ShaderResourceView* srv, ImVec2 res, ImVec2 min, ImVec2 max, float alpha)
  {
    ImVec2 uv0 (0.0f, 0.0f),
           uv1 (1.0f, 1.0f);

    if (res.x > 0.0f && res.y > 0.0f)
    {
      float arTex  = res.x / res.y;
      float arTile = (max.x - min.x) / (max.y - min.y);

      if (arTex > arTile)      // Wider: crop the sides
      {
        float crop = (1.0f - arTile / arTex) * 0.5f;
        uv0.x = crop; uv1.x = 1.0f - crop;
      }

      else if (arTex < arTile) // Taller: crop the top and bottom
      {
        float crop = (1.0f - arTex / arTile) * 0.5f;
        uv0.y = crop; uv1.y = 1.0f - crop;
      }
    }

    draw_list->AddImageRounded ((ImTextureID)(intptr_t)srv, min, max, uv0, uv1,
                                ImGui::ColorConvertFloat4ToU32 (ImVec4 (1.0f, 1.0f, 1.0f, alpha)), rounding); // alpha includes the style's
  };

  // A small icon in a corner of the cover, on a dark background so it shows on any cover
  auto _DrawBadge = [&](const char* icon, ImVec2 pos, ImU32 color)
  {
    ImVec2 size = ImGui::CalcTextSize (icon);
    float  pad  = ImFloor (3.0f * dpi);

    draw_list->AddRectFilled (pos, pos + size + ImVec2 (pad, pad) * 2.0f, IM_COL32 (0, 0, 0, 170), ImFloor (3.0f * dpi));
    draw_list->AddText       (pos + ImVec2 (pad, pad), color, icon);

    return size.x + pad * 2.0f;
  };

  // One tile
  auto _DrawTile = [&](app_record_s* rec)
  {
    const app_key_s key      = rec->GetKey ( );
    bool            selected = (selection.game == key);
    bool            change   = false;
    bool            isSK     = rec->IsSpecialK ( );

    ImGui::PushID          (rec->ImGuiPushID.c_str());

    const ImVec2 posTile = ImGui::GetCursorScreenPos ( );

    if (_registry.bFadeCovers)
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, fAlphaList);

    // The tile itself: hover, selection, navigation (moving onto a tile selects it), and the ID that type-to-search and
    //   the list use (ImGuiLabelID)
    ImGui::PushStyleColor  (ImGuiCol_NavCursor, ImVec4(0,0,0,0));
    bool pressed =
    ImGui::Selectable      (rec->ImGuiLabelID.c_str(), selected,
                            static_cast <ImGuiSelectableFlags> (ImGuiSelectableFlags_SelectOnNav) |
                            static_cast <ImGuiSelectableFlags> (ImGuiSelectableFlags_NoPadWithHalfSpacing), sizeTile); // The latter is internal (another enum)
    ImGui::PopStyleColor   ( );

    const bool hovered   = ImGui::IsItemHovered ( );
    const bool isFocused = (ImGui::GetFocusID ( ) == ImGui::GetID (rec->ImGuiLabelID.c_str()));

    // Selection: a click (either button), navigation, or activation (Enter / A)
    extern bool bAutoScrollActive;

    if (! bAutoScrollActive &&
         (pressed                                         || // Also when navigation moves onto it (SelectOnNav)
          ImGui::IsItemActivated (                      ) ||
          ImGui::IsItemClicked   (ImGuiMouseButton_Left ) ||
          ImGui::IsItemClicked   (ImGuiMouseButton_Right)))
      change = true;

    // The game menu: right-click, or a long press of Enter / A (as in the list)
    static constexpr float _LONG_INTERVAL = .25f;

    bool longPress =
      isFocused && ( ( ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDuration     >= _LONG_INTERVAL   &&
                       ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDurationPrev <= _LONG_INTERVAL ) ||
                     ( ImGui::GetKeyData (ImGuiKey_Enter          )->DownDuration     >= _LONG_INTERVAL   &&
                       ImGui::GetKeyData (ImGuiKey_Enter          )->DownDurationPrev <= _LONG_INTERVAL ) );

    if (! bAutoScrollActive && GameMenu == PopupState_Closed && (ImGui::IsItemClicked (ImGuiMouseButton_Right) || longPress))
      GameMenu = PopupState_Open;

    // Launch: double-click, or a short press of Enter / A, on the selected game (pApp: the menu and the launch act on it)
    const bool canLaunch = (selected && pApp == rec && ! isSK && ! rec->_status.running && ! rec->IsUpdateBlockingLaunch ( ));

    if (hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left))
    {
      if (canLaunch && tileClicked == key && timeClicked + 500 >= now)
      {
        launchGame  = true;
        tileClicked = { };
        timeClicked = 0;
      }

      else
      {
        tileClicked = key;
        timeClicked = now;
      }
    }

    // Enter / A: only a press that started on this tile (not e.g. the one that chose an item of the game menu)
    if (isFocused && (ImGui::IsKeyPressed (ImGuiKey_Enter, false) || ImGui::IsKeyPressed (ImGuiKey_GamepadFaceDown, false)))
      tilePressed = key;

    if (tilePressed == key &&
        ((ImGui::IsKeyReleased (ImGuiKey_Enter)           && ImGui::GetKeyData (ImGuiKey_Enter          )->DownDurationPrev < _LONG_INTERVAL) ||
         (ImGui::IsKeyReleased (ImGuiKey_GamepadFaceDown) && ImGui::GetKeyData (ImGuiKey_GamepadFaceDown)->DownDurationPrev < _LONG_INTERVAL)))
    {
      tilePressed = { };

      if (canLaunch && isFocused && ! ImGui::IsPopupOpen ("", ImGuiPopupFlags_AnyPopupId))
        launchGame = true;
    }

    // Type-to-search found this game (the list, drawn next, selects it), or the selected game should be shown
    if (search_selection.game == key || (selected && scrollToSelected))
    {
      if (! ImGui::IsItemVisible ( ))
        ImGui::SetScrollHereY (0.5f);
    }

    if (change)
    {
      update = (selection.game != key);

      selection.game     =    key;
      selection.category =    GetEffectiveCategory (rec);
      selected           =    true;

      // Only update the last selected value if we're not in hidden view
      if (! _registry._LibraryHidden)
        SetLastSelectedGame (selection.game);

      if (update)
      {
        rec->_status.invalidate ( );

        // The list (drawn next) opens its category and scrolls to it, as after a re-sort. Not for Special K, which the
        //   list resets the scroll position for instead
        if (! isSK)
          sort_changed = true;
      }
    }

    // The cover (Special K: its logo), or a placeholder with the title while there is none
    const ImVec2 minCover = posTile + ImVec2 (inset, inset);
    const ImVec2 maxCover = minCover + sizeCover;

    ID3D11ShaderResourceView* srv   = nullptr;
    ImVec2                    res   = ImVec2 (0, 0);
    float                     alpha = ImGui::GetStyle ( ).Alpha;

    if (isSK)
    {
      srv = _games.GetSKLogoTexture (false);
      res = ImVec2 (600.0f, 900.0f);
    }

    else
    {
      _art.RequestThumbnail (rec);

      if (rec->tex_thumb.texture.p != nullptr)
      {
        srv = rec->tex_thumb.texture.p;
        res = ImVec2 (rec->tex_thumb.width, rec->tex_thumb.height);

        // Fade in once loaded (as the cover does)
        if (_registry.bFadeCovers && rec->tex_thumb.loadedAt != 0)
        {
          float fade = static_cast <float> (now - rec->tex_thumb.loadedAt) / 250.0f;

          if (fade < 1.0f)
          {
            alpha          *= fade;
            coverFadeActive = true; // Keeps the frames coming
          }
        }
      }
    }

    // The placeholder: also shown beneath a cover that fades in
    if (srv == nullptr || alpha < ImGui::GetStyle ( ).Alpha)
    {
      draw_list->AddRectFilled (minCover, maxCover, ImGui::GetColorU32 (ImGuiCol_FrameBg), rounding);

      // The title, centered (also when titles are turned off: the tile would be blank otherwise)
      const char*  text  = rec->names.normal.c_str ( );
      const float  wrap  = sizeCover.x - inset * 4.0f;
      const ImVec2 size  = ImGui::CalcTextSize (text, nullptr, false, wrap);
      const ImVec4 clip  = ImVec4 (minCover.x, minCover.y, maxCover.x, maxCover.y);

      draw_list->AddText (ImGui::GetFont ( ), ImGui::GetFontSize ( ),
                          ImFloor (ImVec2 (minCover.x + (sizeCover.x - size.x) * 0.5f,
                                           minCover.y + std::max (inset, (sizeCover.y - size.y) * 0.5f))),
                          ImGui::GetColorU32 (ImGuiCol_TextDisabled), text, nullptr, wrap, &clip);
    }

    if (srv != nullptr)
      _DrawCover (srv, res, minCover, maxCover, alpha);

    // The selected game: an accent border around the cover, and a faint light line just inside its edge (a "glow")
    if (selected)
    {
      const float  border = std::max (2.0f, ImFloor (2.0f * dpi));
      const ImVec2 out    = ImVec2 (border * 0.5f + 1.0f, border * 0.5f + 1.0f);

      draw_list->AddRect (minCover - out, maxCover + out, ImGui::GetColorU32 (ImGuiCol_SKIF_Info),
                          rounding + out.x, border, ImDrawFlags_None);
      draw_list->AddRect (minCover + ImVec2 (0.5f, 0.5f), maxCover - ImVec2 (0.5f, 0.5f),
                          IM_COL32 (255, 255, 255, 90), rounding, 1.0f, ImDrawFlags_None);
    }

    // Hovered: the faint inner line only
    else if (hovered)
      draw_list->AddRect (minCover + ImVec2 (0.5f, 0.5f), maxCover - ImVec2 (0.5f, 0.5f),
                          IM_COL32 (255, 255, 255, 60), rounding, 1.0f, ImDrawFlags_None);

    // Badges: running or updating (top right). Favorites need none: they have a section of their own
    const float badgeInset = ImFloor (4.0f * dpi);

    if (rec->_status.updating != 0x0 || rec->_status.running != 0x0)
    {
      const char* icon  = (rec->_status.updating != 0x0) ? ICON_FA_DOWNLOAD : ICON_FA_PLAY;
      ImU32       color = (rec->_status.updating != 0x0) ? ImGui::GetColorU32 (ImVec4 (ImColor::HSV (0.6f, .6f, 1.f)))
                                                         : ImGui::GetColorU32 (ImVec4 (ImColor::HSV (0.3f, 1.f, 1.f)));
      float       width = ImGui::CalcTextSize (icon).x + ImFloor (3.0f * dpi) * 2.0f;

      _DrawBadge (icon, ImVec2 (maxCover.x - badgeInset - width, minCover.y + badgeInset), color);
    }

    // The title beneath the cover, shortened with an ellipsis (as the list colors it: updating, running)
    bool titleCut = false;

    if (_registry.bLibraryGridTitles)
    {
      const char*  text   = rec->names.normal.c_str ( );
      const ImVec2 size   = ImGui::CalcTextSize (text);
      const ImVec2 minT   = ImVec2 (minCover.x, maxCover.y + inset * 0.5f);
      const ImVec2 maxT   = ImVec2 (maxCover.x, minT.y + ImGui::GetTextLineHeight ( ));

      titleCut = (size.x > sizeCover.x);

      ImGui::PushStyleColor (ImGuiCol_Text,
        ( rec->_status.updating != 0x0 ) ? ImVec4 (ImColor::HSV (0.6f, .6f, 1.f)) :
        ( rec->_status.running  != 0x0 ) ? ImVec4 (ImColor::HSV (0.3f, 1.f, 1.f)) :
                                           ImGui::GetStyleColorVec4 (ImGuiCol_Text));

      // Centered when it fits
      ImVec2 posText = (titleCut) ? minT : ImFloor (ImVec2 (minT.x + (sizeCover.x - size.x) * 0.5f, minT.y));
      ImGui::RenderTextEllipsis (draw_list, posText, maxT, maxT.x, text, nullptr, &size);

      ImGui::PopStyleColor ( );
    }

    // The full title in a tooltip, when it is not shown in full
    if (hovered && (! _registry.bLibraryGridTitles || titleCut))
      SKIF_ImGui_SetHoverTip (rec->names.normal);

    if (_registry.bFadeCovers)
      ImGui::PopStyleVar ( ); // -ImGuiStyleVar_Alpha

    ImGui::PopID           ( );
  };

  // The games of a section, drawn through the clipper by rows of tiles once the section ends
  static std::vector <app_record_s*> sectionGames; // Static to keep its memory between frames
  sectionGames.clear ( );

  auto _DrawSection = [&](void)
  {
    if (sectionGames.empty ( ))
      return;

    const int count = static_cast <int> (sectionGames.size ( ));
    const int rows  = (count + columns - 1) / columns;

    ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing, ImVec2 (gap, spacing));

    ImGuiListClipper clipper;
    clipper.Begin (rows, sizeTile.y + spacing);

    // The rows of the selected game and the game found by type-to-search act on their own (scroll to them, etc.)
    for (int i = 0; i < count; i++)
    {
      const app_key_s key = sectionGames[i]->GetKey ( );

      if (key == selection.game || key == search_selection.game)
        clipper.IncludeItemByIndex (i / columns);
    }

    while (clipper.Step ( ))
    {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
      {
        for (int col = 0; col < columns; col++)
        {
          const int i = row * columns + col;

          if (i >= count)
            break;

          if (col > 0)
            ImGui::SameLine (0.0f, gap);

          _DrawTile (sectionGames [i]);
        }
      }
    }

    clipper.End ( );

    ImGui::PopStyleVar ( ); // ImGuiStyleVar_ItemSpacing

    sectionGames.clear ( );
  };

  // Walk the library as the list does: the games by category (Favorites first)
  std::string current_category = "";
  int         categories       = 0;
  bool        category_opened  = false;

  // EndCategoryHeaders ( ) is called by the list, drawn after the grid
  BeginCategoryHeaders ( );

  for (auto& app : _games.GetApps())
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second->id == 0)
      continue;

    // Skips those filtered out by an active search field entry
    if (app.second->filtered)
      continue;

    std::string tmpCategory = GetEffectiveCategory (app.second.get ( ));

    if (tmpCategory != current_category)
    {
      // The previous category is done
      _DrawSection ( );

      category_opened  = DrawCategoryHeader (tmpCategory, categories, showClearBtn);
      current_category = tmpCategory;
      categories++;
    }

    if (! category_opened)
      continue;

    // Its client state (running, updating) is refreshed even while the tile is out of view, as in the list
    app.second->RefreshClientState (current_time);

    sectionGames.push_back (app.second.get ( ));
  }

  // The last section
  _DrawSection ( );

  // Right-click on empty space: the same menu as the list's empty space (the Filter menu)
  if (SKIF_ImGui_IsViewportFocused ( )                 &&
      ImGui::IsWindowHovered ( )                       &&
      EmptySpaceMenu == PopupState_Closed              &&
    ! ImGui::IsAnyItemHovered ( )                      &&
      ImGui::IsMouseClicked   (ImGuiMouseButton_Right) &&
    ! SKIF_ImGui_IsAnyPopupOpen ( ))
    EmptySpaceMenu = PopupState_Open;

  // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
  SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

  ImGui::EndChild        ( );
  ImGui::PopStyleColor   ( );
}

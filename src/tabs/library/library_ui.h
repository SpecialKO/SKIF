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

#pragma once

// Internal to the library tab (src/tabs/library.cpp and src/tabs/library/*.cpp).
//   Shared includes, constants and types, the library.cpp globals the other files use, and the functions they share.
//   SKIF_UI_Tab_DrawLibrary ( ) (library.cpp) is the tab itself; include <tabs/library.h> from elsewhere.

#include <tabs/library.h>

#include <wmsdk.h>
#include <filesystem>
#include <SKIF.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <ImGuiNotify.hpp>

#include <utility/injection.h>
#include <utility/launch_behaviors.h>

#include "DirectXTex/DirectXTex.h"

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

#include <stores/GOG/gog_library.h>
#include <stores/epic/epic_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <future>
#include <thread>
#include <chrono>
#include <concurrent_queue.h>
#include <utility/fsutil.h>
#include <atlimage.h>
#include <TlHelp32.h>
#include <shared_mutex>

#include <utility/registry.h>
#include <utility/updater.h>
#include <tabs/ini_editor.h>
#include <utility/ini_presets.h>
#include <stores/Steam/steam_library.h>
#include <utility/artwork.h>


// Constants

// utf8_bom and cfg_compatibility are in <utility/ini_presets.h>

constexpr char         spaces[]          = { "\u0020\u0020\u0020\u0020" };

constexpr int maxCategoryNameLen = 50;

// Types

struct change_category_s {
  std::string  Name = "",    // Holds the old name
            newName = "";    // Holds the new name
  bool       change = false; // Rename + Remove
  bool       remove = false; // Remove
  bool       rename = false; // Rename (through collapsible header)
  bool       exists = false; // If the new category already exists
};

struct terminate_process_s {
  uint32_t      pid = 0;
  HANDLE     handle = INVALID_HANDLE_VALUE;
  std::string  name = "";
};

// Keyboard hint search result
struct library_search_selection_s {
  app_key_s           game;
  std::string         category = "";
};

// A game in the library tab: the selected one, the last one clicked
struct library_selection_s {
  app_key_s                   game  = { app_record_s::Store::Steam, SKIF_STEAM_APPID }; // Special K by default
  SKIF_DirectoryWatch     dir_watch;
  bool                reset_to_skif = true;
  std::string              category = "";

  void reset ()
  {
    game     = (reset_to_skif) ? app_key_s { app_record_s::Store::Steam, SKIF_STEAM_APPID } : app_key_s { };
    category = "";
    dir_watch.reset();
  }
};

// State of SKIF_UI_Tab_DrawLibrary ( ) shared with the functions it calls (GetLibraryTab ( ), library.cpp).
//   Functions bind same-named static references, e.g. static app_record_s*& pApp = GetLibraryTab ( ).pApp;
struct library_tab_s {
  library_selection_s selection;                 // The selected game
  library_selection_s item_clicked;              // The game last clicked in the list
  app_record_s*       pApp     = nullptr;        // Record of the selected game; set once per frame, at the top of SKIF_UI_Tab_DrawLibrary ( )
  bool                update   = true;           // The selection changed; refresh what depends on it
  ImVec2              sizeList = ImVec2 (0, 0);  // Size of the games list (DPI-aware, recalculated when the tab area changes)

  // Cover panel (DPI-aware, recalculated when the tab area changes)
  bool                uiCoverVisible = false;          // The cover is shown at all
  ImVec2              sizeCover      = ImVec2 (0, 0);
  ImVec2              sizeSK         = ImVec2 (0, 0);  // Special K logo
  ImVec2              sizeSK_small   = ImVec2 (0, 0);  // Special K logo with low-res covers
};

library_tab_s& GetLibraryTab (void);

// The last selected game, as remembered in the registry (two values there: game ID and store)
inline app_key_s GetLastSelectedGame (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return { (app_record_s::Store)_registry.uiLastSelectedStore, _registry.uiLastSelectedGame };
}

inline void SetLastSelectedGame (const app_key_s& game)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  _registry.uiLastSelectedGame  =               game.appid;
  _registry.uiLastSelectedStore = (unsigned int)game.store;
}

// Globals defined in library.cpp

extern bool                   SKIF_STEAM_OWNER;
extern std::atomic<bool>      modDownloading;
extern std::atomic<bool>      modInstalling;
extern std::atomic<uint32_t>  modAppId;
extern bool                   bCategoryMgnt;
extern float                  fAlphaList;

// Fade-in of a label shown while something loads (the list's "Loading games...", the cover's "..."). It starts over
//   whenever the label wasn't shown the frame before, and is stepped along with the other fades (see "Fade in/out
//   transition" in library.cpp). Like them, only if covers are set to fade (bFadeCovers).
struct library_fade_in_s {
  float alpha     =  0.0f;
  int   lastFrame = -2;     // Last frame the label was shown
};
extern library_fade_in_s      fadeLoadingList;   // "Loading games..."
extern library_fade_in_s      fadeLoadingCover;  // "..."
float                         LibraryFadeIn (library_fade_in_s& fade); // Call when drawing the label: returns its alpha
extern float                  fTint;             // Cover dimming
extern library_search_selection_s
                              search_selection;
extern PopupState             CoverMenu;
extern PopupState             IconMenu;
extern PopupState             CategoryMenu;
extern PopupState             PopupCategoryModify;
extern bool                   sort_changed;
extern app_record_s::launch_config_s*
                              launchConfig;      // Used to launch games. Defaults to the game's default launch config (GetDefaultLaunchConfig).
extern bool                   launchGame;        // Respects Instant Play preference
extern bool                   launchViaPlatform; // Explicitly through the platform client (Play with > platform): ignores the Instant Play setting
extern bool                   launchInstant;
extern bool                   launchWithoutSK;
extern PopupState             GameMenu;
extern PopupState             EmptySpaceMenu;
extern PopupState             ServiceMenu;
extern PopupState             AddGamePopup;
extern PopupState             RemoveGamePopup;
extern PopupState             ModifyGamePopup;
extern change_category_s      static_category;
extern terminate_process_s    static_proc;
extern float                  lib_column2_width; // Holds the width of column2 (list + details)

// Defined elsewhere

extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// library.cpp
std::string GetEffectiveCategory (const app_record_s* app);
bool SKIF_Lib_CategoryExists (const std::string& name);
bool SKIF_Lib_AddCategory    (const std::string& name);  // Adds the category (sorted) and saves the list in the registry; false if empty or it already exists
void ShowLargeIconToggle     (void);                 // Shared between the Icon menu and the Filter menu
void ShowLibraryViewToggles  (void);                 // The Filter menu's view options: Grid view, then Show titles (grid) or Large icons (list)
bool IsLibraryGridView       (void);                 // The grid view is chosen (Library View); the grid takes the cover's place
void ClearCharFilter         (void);

// library.cpp: images dropped on a game (its tile in the grid view, its row in the list, or the cover of the selected game)
//   change its cover; dropped anywhere else they change nothing
bool RegisterCoverDropTarget (ImVec2 min, ImVec2 max, app_key_s game); // Each frame it is drawn: where (screen coordinates; the part visible in
                                                                       //   the current window is kept). True if an image is dragged over it.
bool GetCoverDropTargetAt    (ImVec2 pos, app_key_s& game);            // The game drawn at pos (screen coordinates) in the latest frame
void DrawCoverDropHighlight  (ImDrawList* draw_list, ImVec2 min, ImVec2 max, float rounding); // An image is dragged over it
void ScrollForCoverDrag      (void);                 // In a scrolling window of drop targets (the grid, the list), each frame: scrolls it
                                                     //   while an image is dragged near its top or bottom edge

// library/library_launch.cpp
void LaunchGame              (app_record_s* pApp);
bool SaveGameCover           (app_record_s* pApp, std::wstring_view path, bool notify = false); // Copies / downloads an image as the game's custom cover (background
                                                     //   thread); notify: a notification once it is saved (one is always shown if it fails)
std::wstring SKIF_Lib_FindPreviousCover    (app_record_s* pApp);          // The previous custom cover (the one replaced or reset last), or empty
bool         SKIF_Lib_KeepPreviousCover    (const std::wstring& assetDir); // The custom cover becomes the previous one (Reset); true if there was one
bool         SKIF_Lib_RestorePreviousCover (app_record_s* pApp);          // Swaps the custom cover and the previous one; true if it did

// library/library_menus.cpp
void DrawCoverMenuChangeReset (app_record_s* pApp, bool isCustom, bool isManaged); // Change / Reset of the cover (cover menu, grid view's game menu)
std::string SKIF_Lib_GetClientFavoriteTip (const std::string& client); // Tooltip of a game favorited in its store client (Steam)
void DrawGameConfigMenu      (app_record_s* pApp);
void DrawGameBrowseMenuItems (app_record_s* pApp); // The items of the Browse menu (game menu, launch bar of the game details)
void SKIF_Lib_OpenGameConfig (app_record_s* pApp); // Opens the game's Special K config file in the INI editor (created if missing)
void DrawGameContextMenu     (app_record_s* pApp);
// The command line a launch config runs: the executable and its arguments, with the launch options set in the store
//   client (Steam) where they apply. Shown in the status bar when hovering Play / Instant play (library_menus.cpp)
std::string SKIF_Lib_GetLaunchCommand (app_record_s* pApp, app_record_s::launch_config_s* lc);
std::string SKIF_Lib_GetPlayDescription (app_record_s* pApp, app_record_s::launch_config_s* lc); // How Play starts the launch config (for its tooltips); empty for Custom games
void DrawSpecialKContextMenu (app_record_s* pApp);
void DrawCoverMenu           (void);
void DrawIconMenu            (void);
void DrawEmptySpaceMenu      (bool showClearBtn);

// library/library_list.cpp
void DrawGamesList           (DWORD current_time, ImVec2 fTop1, bool showClearBtn, ImVec2& fTop3, ImVec2& fTop3ScreenPos);
void BeginCategoryHeaders    (void);                 // Category headers (the list and the grid): before the first one of a frame (once per frame),
bool DrawCategoryHeader      (const std::string& category, int categories, bool showClearBtn); //   each one (returns whether it is expanded),
void EndCategoryHeaders      (void);                 //   and after the last one (Expand all / Collapse all, the category menu); by the list only

// library/library_grid.cpp
void DrawGamesGrid           (DWORD current_time, ImVec2 size, bool showClearBtn); // In place of the cover in the grid view (DrawGameCover)

// library/library_popups.cpp
void DrawLibraryPopups       (void);

// library/library_cover.cpp
void DrawGameCover           (bool sameLine, bool& isCoverHovered, ImVec2& vecPosCover); // isCoverHovered / vecPosCover: set for the code after it
void DrawSpecialKPatreon     (bool& isCoverHovered, ImVec2 vecPosCover);               // isCoverHovered: also set if the panel is hovered

// library/library_details.cpp
void DrawGameDetails         (app_record_s* pApp);                // The details panel (info frame, Play button, launch bar)
void UpdateInjectionStrategy (app_record_s* pApp, std::set <std::string> apptickets);

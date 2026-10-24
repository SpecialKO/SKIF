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
#include <stores/Steam/steam_library.h>
#include <utility/artwork.h>


// Constants

constexpr const wchar_t* utf8_bom        =  L"\xEF\xBB\xBF";
constexpr const wchar_t* cfg_compatibility =
LR"([SpecialK.System]
ShowEULA=false
GlobalInjectDelay=0.0

[API.Hook]
d3d9=true
d3d9ex=true
d3d11=true
OpenGL=true
d3d12=true
Vulkan=true

[Steam.Log]
Silent=true

[Input.libScePad]
Enable=false

[Input.XInput]
Enable=false
UISlot=4

[Input.Gamepad]
EnableDirectInput7=false
EnableDirectInput8=false
EnableHID=false
EnableNativePS4=false
EnableRawInput=true
AllowHapticUI=false

[Input.Keyboard]
CatchAltF4=false
BypassAltF4Handler=false

[Textures.D3D11]
Cache=false)";

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
  uint32_t            id = 0;
  app_record_s::Store store;
  std::string         category = "";
};

// A game in the library tab: the selected one, the last one clicked
struct library_selection_s {
  uint32_t                    appid = SKIF_STEAM_APPID;
  app_record_s::Store         store = app_record_s::Store::Steam;
  SKIF_DirectoryWatch     dir_watch;
  bool                reset_to_skif = true;
  std::string              category = "";

  void reset ()
  {
    appid    = (reset_to_skif) ? SKIF_STEAM_APPID           : 0;
    store    = (reset_to_skif) ? app_record_s::Store::Steam : app_record_s::Store::Unspecified;
    category = "";

    if (dir_watch._hChangeNotification != INVALID_HANDLE_VALUE)
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
};

library_tab_s& GetLibraryTab (void);

// Globals defined in library.cpp

extern bool                   SKIF_STEAM_OWNER;
extern std::atomic<bool>      modDownloading;
extern std::atomic<bool>      modInstalling;
extern std::atomic<uint32_t>  modAppId;
extern bool                   bCategoryMgnt;
extern int                    numRegular;
extern float                  fAlphaList;
extern library_search_selection_s
                              search_selection;
extern PopupState             CoverMenu;
extern PopupState             IconMenu;
extern PopupState             CategoryMenu;
extern PopupState             PopupCategoryModify;
extern bool                   sort_changed;
extern app_record_s::launch_config_s*
                              launchConfig;      // Used to launch games. Defaults to primary launch config.
extern bool                   launchGame;        // Respects Instant Play preference
extern bool                   launchGameMenu;    // Menu is always explicit
extern bool                   launchInstant;
extern bool                   launchWithoutSK;
extern int                    numPinnedOnTop;
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

extern bool                   allowShortcutCtrlA;
extern float                  SKIF_ImGui_GlobalDPIScale;
extern float                  SKIF_ImGui_GlobalDPIScale_Last;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// library.cpp
std::string GetEffectiveCategory (const app_record_s* app);
void ShowLargeIconToggle     (void);                 // Shared between the Icon menu and the Filter menu
void ClearCharFilter         (void);

// library/library_launch.cpp
void LaunchGame              (app_record_s* pApp);
bool SaveGameCover           (app_record_s* pApp, std::wstring_view path); // Copies / downloads an image as the game's custom cover (background thread)

// library/library_menus.cpp
void DrawGameConfigMenu      (app_record_s* pApp);
void DrawGameContextMenu     (app_record_s* pApp);
void DrawSpecialKContextMenu (app_record_s* pApp);
void DrawCoverMenu           (void);
void DrawIconMenu            (void);
void DrawEmptySpaceMenu      (bool showClearBtn);

// library/library_list.cpp
void DrawGamesList           (DWORD current_time, ImVec2 fTop1, bool showClearBtn, ImVec2& fTop3, ImVec2& fTop3ScreenPos);

// library/library_popups.cpp
void DrawLibraryPopups       (void);

// library/library_details.cpp
void GetInjectionSummary     (app_record_s* pApp);
void UpdateInjectionStrategy (app_record_s* pApp, std::set <std::string> apptickets);

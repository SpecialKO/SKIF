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
#include "library/library_ui.h" // Shared with the other library/*.cpp files

bool                   SKIF_STEAM_OWNER  = false;
std::atomic<bool>      modDownloading    = false;
std::atomic<bool>      modInstalling     = false;
std::atomic<bool>      gameWorkerRunning = false;
std::atomic<uint32_t>  modAppId          = 0;
bool                   PopulatedGames    = false;

// Filter field
char                   charFilter    [MAX_PATH + 2] = { };
char                   charFilterTmp [MAX_PATH + 2] = { };
bool                   bFilterActive     = false;
bool                   bCategoryMgnt     = false;
bool                   sort_changed      = false;

app_record_s::launch_config_s*
                       launchConfig      = nullptr; // Used to launch games. Defaults to primary launch config.
bool                   launchGame        = false;   // Respects Instant Play preference
bool                   launchGameMenu    = false;   // Menu is always explicit
bool                   launchInstant     = false;
bool                   launchWithoutSK   = false;

int                    numRegular        = 0;
int                    numPinnedOnTop    = 0;

// Support up to 16 running games at once, lol

struct SKIF_Lib_GameWorkerThread_s {
  std::unique_ptr <app_record_s>
                        app        = nullptr; // Copy of the record being refreshed (same record type)
  std::set<std::string> apptickets = std::set<std::string>( );
  HANDLE                hWorker    = NULL;
  bool                  free       = true;
  int                   cpu_pre    = -1;
};

#define MAX_GAMEWORKER 3
SKIF_Lib_GameWorkerThread_s aGameWorkers[MAX_GAMEWORKER] = { };




const float fTintMin     = 0.75f;
      float fTint        = 1.0f;
      float fAlphaList   = 0.0f;

PopupState GameMenu            = PopupState_Closed;
PopupState EmptySpaceMenu      = PopupState_Closed;
PopupState CoverMenu           = PopupState_Closed;
PopupState IconMenu            = PopupState_Closed;
PopupState ServiceMenu         = PopupState_Closed;
PopupState CategoryMenu        = PopupState_Closed;

PopupState AddGamePopup        = PopupState_Closed;
PopupState RemoveGamePopup     = PopupState_Closed;
PopupState ModifyGamePopup     = PopupState_Closed;
PopupState PopupCategoryModify = PopupState_Closed;
PopupState KeyboardHint        = PopupState_Closed;

change_category_s static_category; // library_ui.h

std::wstring dragDroppedFilePath = L"";
std::wstring dragDroppedFilePathGame = L"";

extern bool            allowShortcutCtrlA;
extern ImVec2          SKIF_vecServiceMode;
extern ImVec2          SKIF_vecHorizonMode;
extern float           SKIF_ImGui_GlobalDPIScale;
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern DWORD           invalidatedDevice;
extern bool            GOGGalaxy_Installed;
extern std::wstring    GOGGalaxy_Path;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// DPI-aware
float tab_scrollbar_width; // Holds the width of the vertical (Y) tab scrollbar (if present)
float lib_column2_width;   // Holds the width of column2 (list + details)

terminate_process_s static_proc; // library_ui.h


// External declaration
extern void SKIF_Shell_AddJumpList (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService);

// Functions / Structs

std::string
GetEffectiveCategory (const app_record_s* app)
{
  return app->IsFavorite ( )
         ?   "Favorites"
         : (! app->skif.category.empty())
         ?    app->skif.category
         :   "Games";
}

// This is shared between the Icon menu and the Filter menu
void
ShowLargeIconToggle (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  bool* pActiveBool = (_registry._TouchDevice) ? &_registry._TouchDevice : &_registry.bUILargeIcons;

  if (_registry._TouchDevice)
  {
    ImGui::BeginGroup           ( ); // Needed for the hover tip to appear
    SKIF_ImGui_PushDisableState ( );
  }

  if (SKIF_ImGui_MenuItemEx2 ("Large icons", ICON_FA_ICONS, ImColor(255, 207, 72), spaces, pActiveBool))
    _registry.regKVUILargeIcons.putData (_registry.bUILargeIcons);

  if (_registry._TouchDevice)
  {
    SKIF_ImGui_PopDisableState  ( );
    ImGui::EndGroup             ( );
    SKIF_ImGui_SetHoverTip      ("Currently enforced by touch input mode.");
  }
}



#pragma region Trie Keyboard Hint Search

library_search_selection_s search_selection; // library_ui.h

Trie labelsFiltered;

static void
SearchAppsList (void)
{
  if (bFilterActive || bCategoryMgnt)
    return;

  static auto
    constexpr _text_chars =
      { 'A','B','C','D','E','F','G','H',
        'I','J','K','L','M','N','O','P',
        'Q','R','S','T','U','V','W','X',
        'Y','Z','0','1','2','3','4','5',
        '6','7','8','9',' ','-','.' }; // ':', // ???

  static char test_ [1024] = {      };
  char        out   [2]    = { 0, 0 };
  bool        bText        = false;

  const  DWORD dwTimeout    = 850UL; // 425UL
  static DWORD dwLastUpdate = MAXDWORD;

  struct {
    std::string text          = "";
    app_record_s::Store store = app_record_s::Store::Unspecified;
    uint32_t    app_id        = 0;
    std::string category      = "";
    size_t      pos           = 0;
    size_t      len           = 0;
  } static result;

  static SKIF_GamingCollection& _games =
         SKIF_GamingCollection::Get ( );

  // We need to select any match and clear variables before we do a search,
  //   as otherwise SKIF's conditional rendering can cause a search query
  //     to not end within the properly allocated time frame
  if (                           dwLastUpdate != MAXDWORD &&
      SKIF_Util_timeGetTime () - dwLastUpdate >
                                 dwTimeout )
  {
    // If we have a result, select it
    if (result.app_id != 0)
    {
      search_selection.game     = { result.store, result.app_id };
      search_selection.category = result.category;
    }

    // Clear any temp data
    result       = { };      // Found match
    *test_       = '\0';     // Search query
    dwLastUpdate = MAXDWORD; // Timer
  }

  // Check input
  for ( auto c : _text_chars )
  {
    if (ImGui::GetKeyData (SKIF_ImGui_CharToImGuiKey (c))->DownDuration == 0.0f &&
        (c != ' ' || strlen (test_) > 0))
    {
      out [0] = c;
      StrCatA (test_, out);
      bText   = true;
    }
  }

  // Do search
  if (bText)
  {
    dwLastUpdate = SKIF_Util_timeGetTime ();

    Trie* searchLabels = (charFilter[0] != '\0') ? &labelsFiltered : &SKIF_GamingCollection::Get ( ).GetLabels ( );

    // Acquire lock before iterating the global apps list
    std::scoped_lock app_lock (_games.GetMutex());

    // Prioritize trie search first
    if (searchLabels->search (test_))
    {
      for (const auto& app : _games.GetApps())
      {
        // Skip invalid/hidden and filtered ones
        if (app.second->id == 0 || app.second->filtered)
          continue;

        if (app.second->names.all_upper_alnum.find (test_) == 0)
        {
          result.text     = app.second->names.normal;
          result.store    = app.second->store;
          result.app_id   = app.second->id;
          result.category = GetEffectiveCategory (app.second.get ( ));
          result.pos      = app.second->names.pre_stripped;
          result.len      = strlen (test_);

          // Handle cases where articles are ignored

          // Add one to the length if the regular all_upper cannot find a match
          // as this indicates a stripped character in the found pattern
          if (app.second->names.all_upper.find (test_) != 0)
            result.len++;

          break;
        }
      }
    }

    // Fall back to using free text search when the trie fails us
    else
    {
      for (auto& app : _games.GetApps())
      {
        // Skip invalid/hidden and filtered ones
        if (app.second->id == 0 || app.second->filtered)
          continue;

        size_t
            pos  = app.second->names.all_upper.find (test_);
        if (pos != std::string::npos ) // == 0
        {
          result.text   = app.second->names.normal;
          result.store  = app.second->store;
          result.app_id = app.second->id;
          result.pos    = pos;
          result.len    = strlen (test_);

          break;
        }
      }
    }
  }

  if (! result.text.empty ())
  {
    size_t len =
        (result.len < result.text.length ( ))
      ? result.len : result.text.length ( );

    std::string preSearch  =  result.text.substr (         0,  result.pos),
                curSearch  =  result.text.substr (result.pos,  len),
                postSearch = (result.pos + len  < result.text.length ( ))
                            ?  result.text.substr (result.pos + len, std::string::npos)
                            :  "";

    KeyboardHint = PopupState_Open;

    if (SKIF_ImGui_BeginPopup ("###KeyboardHint", nullptr, ImVec2(), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
      if (KeyboardHint == PopupState_Open)
      {
        // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
        ImGuiWindow* window = ImGui::FindWindowByName ("###KeyboardHint");
        if (window != nullptr && ! window->Appearing)
          KeyboardHint = PopupState_Opened;
      }

      if (! preSearch.empty ())
      {
        ImGui::TextDisabled ("%s", preSearch.c_str ());
        ImGui::SameLine     (0.0f, 0.0f);
      }

      ImGui::TextColored ( ImColor::HSV(0.0f, 0.0f, 0.75f), // ImColor(53, 255, 3)
                              "%s", curSearch.c_str ()
      );

      if (! postSearch.empty ())
      {
        ImGui::SameLine     (0.0f, 0.0f);
        ImGui::TextDisabled ("%s", postSearch.c_str ());
      }
    }

    else
      KeyboardHint = PopupState_Closed;

    SKIF_ImGui_EndPopup ( );
  }

  else
    KeyboardHint = PopupState_Closed;
}

#pragma endregion

// Clears the search filter and counts the regular / pinned-on-top entries again
void
ClearCharFilter (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  strncpy (charFilter,    "\0", MAX_PATH);
  strncpy (charFilterTmp, "\0", MAX_PATH);

  numPinnedOnTop = 0;
  numRegular     = 0;

  for (auto& app : _games.GetApps())
  {
    app.second->filtered = false;

    numRegular++;

    if (app.second->skif.pinned > 50)
      numPinnedOnTop++;
  }

  numRegular -= numPinnedOnTop;
}


// Library state shared with library/*.cpp (library_ui.h)
library_tab_s&
GetLibraryTab (void)
{
  static library_tab_s tab;
  return tab;
}

// Launching and custom covers:             library/library_launch.cpp
// Game context menus:                      library/library_menus.cpp
// Details panel and injection strategy:    library/library_details.cpp
// Games list:                              library/library_list.cpp
// Cover / icon / empty-space menus:        library/library_menus.cpp
// Popups:                                  library/library_popups.cpp
// Cover panel, Patreon panel:              library/library_cover.cpp



void
SKIF_UI_Tab_DrawLibrary (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static SKIF_EpicCollection&   _epic       = SKIF_EpicCollection  ::GetInstance ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( ); // Cover + icon textures and workers

  static int   activeGameWorkers     = 0; // max: 3
  static int   frameLibraryRefreshed = 0;

#pragma region Initialization

  if (_registry.bLibrarySteam)
  {
    // Set up the registry watch on the Steam ActiveProcess key and ActiveUser value
    SK_RunOnce (
      SKIF_Steam_HasActiveProcessChanged (nullptr, nullptr);
    )
  }

  auto& io =
    ImGui::GetIO ();

  DWORD       current_time = SKIF_Util_timeGetTime ( );
  static bool& update      = GetLibraryTab ( ).update;

  // Shared with the functions in library/*.cpp through GetLibraryTab ( ) (library_ui.h)
  static library_selection_s& selection    = GetLibraryTab ( ).selection;
  static library_selection_s& item_clicked = GetLibraryTab ( ).item_clicked;

  // Check if any monitored platforms have been signaled
  // !!! The signal checks must be before the platform checks,
  //       otherwise SKIF can end up with signaled objects that's never cleared !!!
  if (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ))
  {
    static bool runOnce = true;
    static DWORD time_preprocessing, time_current;

    if (runOnce)
    {
      time_preprocessing = SKIF_Util_timeGetTime1();
      time_current = time_preprocessing;
    }

    // Sets up a wait object on UITab_Library
    if (SKIF_Steam_areLibrariesSignaled () && _registry.bLibrarySteam)
      RepopulateGames = true;

    if (runOnce)
    {
      PLOG_DEBUG << "[Library Pre-Processing] Steam took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      time_current = SKIF_Util_timeGetTime1 ( );
    }

    // Sets up a wait object on UITab_Library
    if (_epic.isSignaled ( ) && _registry.bLibraryEpic)
      RepopulateGames = true;

    if (runOnce)
    {
      PLOG_DEBUG << "[Library Pre-Processing] Epic took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      time_current = SKIF_Util_timeGetTime1 ( );
    }

    // Sets up a wait object on UITab_Library
    if (SKIF_GOG_hasInstalledGamesChanged ( ) && _registry.bLibraryGOG)
      RepopulateGames = true;

    // Does not set up a wait object
    if (SKIF_GOG_hasGalaxySettingsChanged ( ) && _registry.bLibraryGOG)
      SKIF_GOG_UpdateGalaxyUserID ( );

    if (runOnce)
    {
      PLOG_DEBUG << "[Library Pre-Processing] GOG took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      time_current = SKIF_Util_timeGetTime1 ( );
    }

    time_current = SKIF_Util_timeGetTime1 ( );

    // Sets up a wait object on UITab_Library
    if (SKIF_Xbox_hasInstalledGamesChanged ( ) && _registry.bLibraryXbox)
      RepopulateGames = true;

    if (runOnce)
    {
      PLOG_DEBUG << "[Library Pre-Processing] Xbox took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      PLOG_DEBUG << "[Library Pre-Processing] Total process took " << (SKIF_Util_timeGetTime1 ( ) - time_preprocessing) << " ms.";
      runOnce = false;
    }
  }

  // We cannot manipulate the apps array while the game worker thread is running, nor any active icon workers
  if (RepopulateGames && _art.GetActiveIconWorkers ( ) == 0) // ! gameWorkerRunning.load()
  {
    PLOG_VERBOSE << "RepopulateGames && activeIconWorkers == 0";

    RepopulateGames = false;
    //gameWorkerRunning.store(true);

    PopulatedGames = false;
  }

  else if (RepopulateGames)
  {
    PLOG_VERBOSE << "RepopulateGames " << _art.GetActiveIconWorkers ( );
  }


  // Library population is owned by SKIF_GamingCollection; the UI only decides when to (re)populate
  //   and applies UI-side state (selection, filter, pinned counts) once new data has been swapped in.
  if (! PopulatedGames && ! _games.IsPopulating ( ))
    _games.PopulateGames ( );

  // Swapping in frees the old records, so it waits until no cover / icon worker still uses one
  else if (! PopulatedGames && _games.IsPopulateFinished ( ) && ! _art.IsUsingRecords ( ))
  {
    _art.OnIconWorkersReaped (_games.SwapInPopulatedGames ( ));

    std::scoped_lock app_lock (_games.GetMutex());

    labelsFiltered   = { };

    fAlphaList = (_registry.bFadeCovers) ? 0.0f : 1.0f;

    frameLibraryRefreshed = ImGui::GetFrameCount ( );

    // Reset selection to Special K, but only if set to something else than -1
    if (selection.game.appid != 0)
      selection.reset();

    bool resortGames   = false;
    int tmpPinnedOnTop = 0;

    // Do other misc stuff
    for (auto& app : _games.GetApps())
    {
      if (app.second->id == 0)
        continue;

      // Set to last selected if it can be found
      if (app.second->GetKey ( ) == GetLastSelectedGame ( ))
      {
        PLOG_VERBOSE << "Selected app ID " << app.second->id << " from platform ID " << (int)app.second->store << ".";
        selection.game         =    app.second->GetKey ( );
        selection.category     =   (app.second->skif.pinned > 50 && ! _registry._LibHorizonMode && _registry._LibPinnedVisible) // Only when not using horizon mode
                               ?   "Favorites (pinned)" // Workaround to not expand Favorites tab on launch
                               :    GetEffectiveCategory (app.second.get ( ));
        search_selection.game  =    selection.game;
        search_selection.category = selection.category;
        update = true;
      }

      // Prefill all apps with the current version (solves a single frame flicker the first time a game is selected)
      app.second->specialk.injection.dll.version      = _inject.SKVer32;
      app.second->specialk.injection.dll.version_utf8 = SK_WideCharToUTF8 (app.second->specialk.injection.dll.version);

      // Apply the current filter
      app.second->filtered = (charFilter[0] != '\0' && (StrStrIA (app.first.c_str(), charFilter) == NULL &&                // Name
                                                       StrStrIA (app.second->skif.category.c_str(), charFilter) == NULL)); // Category

      // Count the number of pinned entries on top
      if (app.second->skif.pinned > 50)
      {
        // Only allow 5 pinned on top
        if (5 > tmpPinnedOnTop)
          tmpPinnedOnTop++;

        // Reduce all other to normal favorite status
        else
        {
          app.second->skif.pinned = 50;
          resortGames = true;
        }
      }
    }

    if (resortGames)
    {
      SKIF_GamingCollection::SortApps (&_games.GetApps());
      sort_changed = true;
    }

    // Use a separate pass to count the actual figures used for the UI calculations later
    numRegular       = 0;
    numPinnedOnTop   = 0;

    for (auto& app : _games.GetApps())
    {
      if (app.second->id == 0 || app.second->filtered)
        continue;

      InsertTrieKey (&app, &labelsFiltered);

      numRegular++;

      if (app.second->skif.pinned > 50)
        numPinnedOnTop++;
    }

    numRegular -= numPinnedOnTop;

    PopulatedGames = true;
    sort_changed   = true;

    PLOG_VERBOSE << "Swapped in the new library data!";
  }

  // Swap in the covers / icons the background workers finished
  _art.ApplyWorkerResults ( );

  extern bool  coverFadeActive;
  static int   tmp_iDimCovers = _registry.iDimCovers;

  static app_record_s*& pApp = GetLibraryTab ( ).pApp;

  pApp = nullptr;

  // Ensure pApp points to the current selected game
  // This should be the only place where pApp changes during the whole frame!
  pApp = _games.Find (selection.game);

  // Default to primary launch config
  launchConfig = (pApp != nullptr && ! pApp->launch_configs.empty()) ? &pApp->launch_configs.begin()->second : nullptr;

  bool isSpecialK = (pApp != nullptr && pApp->IsSpecialK ( ));

  // Update the injection strategy for the selected game
  // Only do this once per frame to prevent data from "leaking" between pApp's
  // Also don't do it while the library worker is processing (to prevent stale data from surviving the refresh)
  // TODO: We still need to improve this by killing off any outdated workers and prevent them from swapping their
  //         results in. Maybe using an integer indicating the library refresh iteration we're currently on, that
  //           the worker can have a copy of for evaluation once it's finished?
  if (pApp != nullptr && ! _games.IsPopulating ( ))
  {
    if (update)
    {
      if (  pApp->install_dir != selection.dir_watch._path)
        selection.dir_watch.reset ( );

      if (! pApp->install_dir.empty())
        selection.dir_watch.isSignaled (pApp->install_dir, UITab_None, TRUE);
    }

    int availableWorker = -1;
    int idx             = -1;

    for (auto& worker : aGameWorkers)
    {
      idx++;

      if (worker.free)
      {
        availableWorker = idx;
        continue;
      }

      if (WaitForSingleObject (worker.hWorker, 0) == WAIT_OBJECT_0)
      {
        std::scoped_lock app_lock (_games.GetMutex());

        for (auto& app : _games.GetApps())
        {
          if (app.second->id    == worker.app->id &&
              app.second->store == worker.app->store)
          {
            // Backup the texture data (in particular any active worker data)
            app_record_s::tex_registry_s tex_icon  = app.second->tex_icon;
            app_record_s::tex_registry_s tex_cover = app.second->tex_cover;

            // Copy the results over
            app.second->CopyFrom (*worker.app);

            // Restore the texture data (and worker data)
            app.second->tex_icon  = tex_icon;
            app.second->tex_cover = tex_cover;

            app.second->loading = false;

            int cpu_post = (int)app.second->specialk.injection.injection.bitness;

            // If the CPU has changed, we need to update the metadata as well,
            //   but only if it differs from our cached value...
            if (cpu_post != worker.cpu_pre &&
                cpu_post != app.second->skif.cpu_type)
            {
              app.second->skif.cpu_type = cpu_post; // 0 = Common,  1 = x86, 2 = x64, 0xFFFF = Any

              // Update the db.json file with any new values
              SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp (app.second.get ( ), true);
            }
          }
        }

        CloseHandle (worker.hWorker);

        // Reset all values
        worker = SKIF_Lib_GameWorkerThread_s();
      }
    }

    // Only run this block of code if
    if (availableWorker != -1 && ! pApp->loading       && // We require an available worker
        (update                                        ||
         selection.dir_watch.isSignaled ( )            || // TODO: Investigate support for multiple launch configs? Right now only the "main" folder is being monitored
        (_registry.bLibrarySteam && SKIF_Steam_HasActiveProcessChanged (&_games.GetApps(), &_games.GetAppTickets ( ))) || // If Steam user signed in / out
        _inject.libCacheRefresh                       )) // If the global DLL files have been changed
    {
      _inject.libCacheRefresh = false;

      //PLOG_VERBOSE << "CPU PRE : " << cpu_pre;

      // Parse store data for the current game on the main thread (Steam: appinfo.vdf)
      pApp->LoadStoreDetails ( );

      // Only run a worker if we're not dealing with Special K
      if (! isSpecialK)
      {
        pApp->loading = true;

        // Make a copy of pApp that we will use to update all data
        SKIF_Lib_GameWorkerThread_s* worker = &aGameWorkers[availableWorker];
        worker->app        = pApp->Clone ( );
        worker->apptickets = _games.GetAppTickets ( );
        worker->cpu_pre    = (int)pApp->specialk.injection.injection.bitness;

        HANDLE hWorkerThread = (HANDLE)
        _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
        {
          SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_GameWorker");

          // Is this combo really appropriate for this thread?
          //SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
          //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

          //PLOG_VERBOSE << "SKIF_GameWorker thread started!";

          SKIF_Lib_GameWorkerThread_s* _data = static_cast<SKIF_Lib_GameWorkerThread_s*>(var);

          UpdateInjectionStrategy (_data->app.get ( ), _data->apptickets);

          // Force a refresh when the game icons have finished being streamed
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

          //PLOG_VERBOSE << "SKIF_GameWorker thread stopped!";

          //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

          return 0;
        }, worker, 0x0, nullptr);

        bool threadCreated = (hWorkerThread != NULL);

        if (threadCreated)
        {
          worker->hWorker = hWorkerThread;
          worker->free = false;
        }

        else // Someting went wrong during thread creation
        {
          // Reset all values
          *worker = SKIF_Lib_GameWorkerThread_s();
        }
      }
    }

    // Load a new cover (no-op if it is already the one shown)
    _art.SelectCover (pApp);

    // Release old cover after it has faded away, or instantly if we don't fade covers
    _art.ReleaseOldCoverIfFaded ( );
  }

  // Apply changes when the selected game changes
  if (update)
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;
  // Apply changes when the _registry.iDimCovers var has been changed in the Settings tab
  else if (tmp_iDimCovers != _registry.iDimCovers)
  {
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;

    tmp_iDimCovers = _registry.iDimCovers;
  }

  // At this point we're done with the update variable
  update = false;

#pragma endregion

  /* Design thoughts
  *
  * - Library tab should automatically adjust its UI elements
  *     based on the width and height of the app window
  *
  * - Library tab should support 1-3 columns (Cover | List | Details)
  *     based on the size of the app window
  *
  * One column  (low width):
  *   Only column: List + Details below
  *   Random idea: Add low-height scenario where even the details are hidden?
  *
  * Two columns (medium width):
  *   Left  column: Small Cover + Details below
  *   Right column: List
  *
  * Two columns (normal width + height):
  *   Left  column: Large cover
  *   Right column: List + Details below
  *
  * Three columns: (normal width + low height)
  *   Left  column: Small Cover (or list.....?)
  *  Center column: List        (or details..?)
  *   Right column: Details     (or cover....?)
  *
  *
  * The proper layout needs to be decided upon in Library.cpp, and
  *   adjusted on the fly as the app window get resized by the user.
  *
  */

  // DPI-aware, calculate once per frame
  ImVec2 tab_ContentRegionAvail = ImGui::GetContentRegionAvail();
  static ImVec2 tab_ContentRegionAvail_last;

  // These gets calculated anew every time the tab content area changes
  static ImVec2& sizeList = GetLibraryTab ( ).sizeList;
  static ImVec2  sizeDetails;
  static ImVec2& sizeCover      = GetLibraryTab ( ).sizeCover;
  static ImVec2& sizeSK         = GetLibraryTab ( ).sizeSK;
  static ImVec2& sizeSK_small   = GetLibraryTab ( ).sizeSK_small;
  static bool    uiDetailsVisible;
  static bool&   uiCoverVisible = GetLibraryTab ( ).uiCoverVisible;

  // Only calculate new values when the content region has changed
  // All of these calculations are DPI aware

  // SKIF_vecRegularMode is changed on the next frame after the DPI scaling has changed,
  //   so we need to use a change in that value as a trigger to calculate everything anew
  extern ImVec2 SKIF_vecRegularMode;
  static ImVec2 SKIF_vecRegularMode_last;

  if (tab_ContentRegionAvail    != tab_ContentRegionAvail_last    ||
    //SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last || // Not necessary when using SKIF_vecRegularMode
      SKIF_vecRegularMode.x     != SKIF_vecRegularMode_last.x      ) // Only one dimension needs to be checked since both will change when the DPI scaling changes
  {
    tab_ContentRegionAvail_last  = tab_ContentRegionAvail;
    SKIF_vecRegularMode_last     = SKIF_vecRegularMode;

    sizeSK       = ImVec2 (600.0f, 900.0f) * SKIF_ImGui_GlobalDPIScale;
    sizeSK_small = ImVec2 (220.0f, 330.0f) * SKIF_ImGui_GlobalDPIScale;

    static float arCover = 600.0f / 900.0f;

    float horizon_cutoff_height = 550.0f * SKIF_ImGui_GlobalDPIScale; // 750.0f // 650.0f

    _registry._LibPinnedVisible = tab_ContentRegionAvail.y >= horizon_cutoff_height;

    _registry._LibHorizonMode  = (tab_ContentRegionAvail.x > 600.0f * SKIF_ImGui_GlobalDPIScale &&
                                  tab_ContentRegionAvail.y < horizon_cutoff_height);
    _registry._UseLowResCovers = (_registry._LibHorizonMode &&
                                  tab_ContentRegionAvail.y <= SKIF_vecHorizonMode.y);
    _registry._UseLowResCoversHiDPIBypass = ! (tab_ContentRegionAvail.y <= (SKIF_vecHorizonMode.y / SKIF_ImGui_GlobalDPIScale)); // Allow high-res images in HiDPI scenarios (true = use high-res; false = use low-res)

    tab_scrollbar_width    = ImGui::GetCurrentWindowRead()->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f;
    lib_column2_width      = 376.0f * SKIF_ImGui_GlobalDPIScale; // tab_scrollbar_width // SKIF_vecServiceMode (not ready yet) // ((_registry._LibHorizonMode) ? 376.0f : 376.0f) * SKIF_ImGui_GlobalDPIScale

    sizeList        = ImFloor ((_registry._LibHorizonMode || ! _registry._LibPinnedVisible)  ? (_registry.bUIBorders)
                                                                   ? ImVec2 (lib_column2_width, 334.0f * SKIF_ImGui_GlobalDPIScale)   // Horizon + Borders
                                                                   : ImVec2 (lib_column2_width, 332.0f * SKIF_ImGui_GlobalDPIScale)   // Horizon
                                                                   : ImVec2 (lib_column2_width, 620.0f * SKIF_ImGui_GlobalDPIScale)); // Regular
    sizeDetails     = ImFloor ((_registry._LibHorizonMode)  ? (_registry.bUIBorders)
                                                                   ? ImVec2 (lib_column2_width + 2.0f  * SKIF_ImGui_GlobalDPIScale, 332.0f * SKIF_ImGui_GlobalDPIScale)  // Horizon + Borders
                                                                   : ImVec2 (lib_column2_width, 330.0f * SKIF_ImGui_GlobalDPIScale)   // Horizon
                                                                   : ImVec2 (lib_column2_width, 280.0f * SKIF_ImGui_GlobalDPIScale)); // Regular
    sizeCover    = tab_ContentRegionAvail;
    sizeCover.x -= sizeList.x;
    sizeCover.x -= (_registry._LibHorizonMode) ? sizeDetails.x : 0.0f;

    ImVec2 sizeImage       = ImFloor ((_registry._UseLowResCovers) ? sizeSK_small
                                                                   : sizeSK);

    if (sizeCover.y < sizeImage.y)
    {
      sizeImage.y = sizeCover.y;
      sizeImage.x = sizeImage.y * arCover;
    }

    uiDetailsVisible  = (_registry._LibHorizonMode)
                                  ? (tab_ContentRegionAvail.x > ImFloor ((sizeList.x + sizeDetails.x) * 0.5f))
                                  : (tab_ContentRegionAvail.y >= horizon_cutoff_height); // When in regular mode

    uiCoverVisible    = (_registry._LibHorizonMode)
                                  ?                      (tab_ContentRegionAvail.x > (sizeList.x    + sizeDetails.x + sizeSK_small.x * 0.75f))       // When in horizontal mode, large enough to allow 75% of the tiny cover to appear
                                  : (uiDetailsVisible) ? (tab_ContentRegionAvail.x > (SKIF_vecRegularMode.x * 0.75f)) : false; // When in regular mode, large enough to allow 75% of the large cover to appear // sizeDetails.x + sizeSK.x       * 0.75f

    if (! uiCoverVisible)
    {
      float newMaxWidth = tab_ContentRegionAvail.x / ((_registry._LibHorizonMode && uiDetailsVisible) ? 2.0f : 1.0f);
      lib_column2_width = newMaxWidth;
      sizeList.x        = newMaxWidth;
      sizeDetails.x     = newMaxWidth;
    }

    float shrinkDetailsAt = horizon_cutoff_height * 1.5f;

    if (_registry._LibHorizonMode)
    {
      sizeDetails.y  = tab_ContentRegionAvail.y;
    //sizeDetails.y -= (_registry.bUIBorders) ? 2.0f * SKIF_ImGui_GlobalDPIScale : 0.0f;
    }

    else if (tab_ContentRegionAvail.y <= shrinkDetailsAt)
    {
      float shrinkPercentage = tab_ContentRegionAvail.y / shrinkDetailsAt;
      sizeDetails.y          = shrinkPercentage * sizeDetails.y;
    }

    // DPI-aware
    sizeList.y = tab_ContentRegionAvail.y;
    sizeList.y -= (  _registry._LibHorizonMode || ! uiDetailsVisible)   ? 0.0f                             : sizeDetails.y;
  //sizeList.y -= (! _registry._LibHorizonMode && _registry.bUIBorders) ? 2.0f * SKIF_ImGui_GlobalDPIScale : 0.0f;
  }

  // From now on ImGui UI calls starts being made...

#pragma region GameCover

  // Cover panel (library/library_cover.cpp). Also used further down: isCoverHovered (dimming, Patreon panel),
  //   vecPosCover (Patreon panel)
  bool          isCoverHovered     = false;
  ImVec2        vecPosCover        = ImVec2 (0, 0);

  DrawGameCover (true, isCoverHovered, vecPosCover);

#pragma endregion

  //float fZ =
  //ImGui::GetCursorPosX ( );

  ImVec2 fTop1 = ImGui::GetCursorPos ( );

  // LIST + DETAILS START
  ImGui::BeginGroup   ( );

  // Top option: Filter icon + search field

  ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleVar       (ImGuiStyleVar_ChildBorderSize, 0.0f);

  ImGui::SetCursorPosX   (
    ImFloor (ImGui::GetCursorPosX ( ) + 7.0f * SKIF_ImGui_GlobalDPIScale)
  );

  ImGui::SetCursorPosY   (
    ImFloor (ImGui::GetCursorPosY ( ) + 4.0f * SKIF_ImGui_GlobalDPIScale)
  );


  bool showClearBtn      = (charFilter[0] != '\0');
  // Mirrors what ImGui::ButtonEx() does to calculate the height of buttons
  ImVec2 fTopFilterSize  =                           ImGui::CalcTextSize (ICON_FA_FILTER);
  float fTopZoomX        =                           ImGui::CalcTextSize (ICON_FA_MAGNIFYING_GLASS).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
  float fTopClearX       = (! showClearBtn ? 0.0f :  ImGui::CalcTextSize (ICON_FA_XMARK ).x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x);
  float fTopHeight       =                                                 fTopFilterSize.y           + ImGui::GetStyle().FramePadding.y * 2.0f;
  float fTopFilterX      =                                                 fTopFilterSize.x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
  float fTopFilterFieldX = lib_column2_width - fTopZoomX - fTopClearX - fTopFilterX;
  //static bool bFilterHovered = false;

  ImGui::BeginChild          ( "###AppListTopRow",
                                ImVec2 (sizeList.x - ImGui::GetStyle().WindowPadding.x / 2.0f, fTopHeight),
                                (_registry.bUIBorders ? ImGuiChildFlags_Border : ImGuiChildFlags_None),
                                ImGuiWindowFlags_NavFlattened );

  ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4(0,0,0,0));
  ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
  ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));

  ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

  ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
  ImGui::Button         (ICON_FA_MAGNIFYING_GLASS);
  ImGui::PopItemFlag    ( );
  ImGui::SameLine       ( );

  ImGui::InputTextEx ("###AppListFilterField", "", charFilterTmp, MAX_PATH,
                      ImVec2 (fTopFilterFieldX, 0.0f),
                      ImGuiInputTextFlags_AutoSelectAll, 0, nullptr);

  ImGui::PopStyleColor  ( ); // ImGuiCol_Text

  // Ctrl+F should focus filter field
  if (io.KeyCtrl && ImGui::GetKeyData (ImGuiKey_F)->DownDuration == 0.0f)
  {
    ImGui::ActivateItemByID (ImGui::GetID("###AppListFilterField"));
    ImGui::SetFocusID       (ImGui::GetID("###AppListFilterField"), ImGui::GetCurrentWindow());
  }

  // This is required to prevent the InputTextEx from not being deselected when clicking empty space
  SKIF_ImGui_DisallowMouseDragMove ( );

  bFilterActive = ImGui::IsItemActive ( );

  if (bFilterActive)
    allowShortcutCtrlA = false;
  else if (ImGui::IsItemFocused ( ) && ! ImGui::IsItemHovered( ) && ImGui::IsAnyMouseDown( ))
  {
    // Clear highlight from filter box
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.NavDisableHighlight = false;
  }

  ImGui::SameLine ( );

  //static int numPinned   = 0;
  //static int numRegular  = 0;
  //bool       resortGames = false;

  // Preprocess pinned/favorite entries

  // Update some stuff if filter query has changed
  if (strncmp (charFilter, charFilterTmp, MAX_PATH) != 0)
  {
    strncpy (charFilter, charFilterTmp, MAX_PATH);

    labelsFiltered = Trie { };
    numPinnedOnTop = 0;
    numRegular     = 0;

    for (auto& app : _games.GetApps())
    {
      if (app.second->id == 0)
        continue;

      app.second->filtered = (charFilter[0] != '\0' && (StrStrIA (app.first.c_str(), charFilter) == NULL &&                // Name
                                                       StrStrIA (app.second->skif.category.c_str(), charFilter) == NULL)); // Category

      if (app.second->filtered)
        continue;

      InsertTrieKey (&app, &labelsFiltered);

      numRegular++;

      if (app.second->skif.pinned > 50)
        numPinnedOnTop++;
    }

    numRegular -= numPinnedOnTop;
  }

  //PLOG_VERBOSE << "Numbers: " << numRegular << " (regular) -- " << numPinnedOnTop << " (on top)";

  // Clears the search filter: ClearCharFilter ( ) (library.cpp)

  if (showClearBtn)
  {
    // Needed to stop flickering on the same frame as the field is emptied
    bool justCleared = (charFilter[0] == '\0');
    static bool btnClearHover = false;

    if (justCleared)
      ImGui::PushStyleColor (ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    else if (btnClearHover)
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    else
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));

    if (ImGui::Button (ICON_FA_XMARK))
      ClearCharFilter ( );

    ImGui::PopStyleColor ( );

    btnClearHover = ImGui::IsItemHovered() || ImGui::IsItemActive();

    ImGui::SameLine ( );
  }

  static bool btnFilterHover = false;

  if (btnFilterHover)
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
  else
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));

  if (ImGui::Button (ICON_FA_FILTER))
    EmptySpaceMenu = PopupState_Open;

  ImGui::PopStyleColor  ( ); // ImGuiCol_Text

  ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonActive
  ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonHovered
  ImGui::PopStyleColor  ( ); // ImGuiCol_Button

  btnFilterHover = ImGui::IsItemHovered() || ImGui::IsItemActive();

  ImGui::EndChild ( );

  ImGui::PopStyleVar ( ); // ImGuiStyleVar_ChildBorderSize
  ImGui::PopStyleVar ( ); // ImGuiStyleVar_FrameBorderSize

  // Some additional padding between the filter field and the pinned apps
  ImGui::SetCursorPosY   (
    ImFloor (ImGui::GetCursorPosY ( ) + 3.0f * SKIF_ImGui_GlobalDPIScale)
  );

  ImVec2 fTop2 = ImGui::GetCursorPos ( );

  // End top options

  // Games list (library/library_list.cpp); fTop3 / fTop3ScreenPos: where the list starts
  ImVec2 fTop3, fTop3ScreenPos;
  DrawGamesList (current_time, fTop1, showClearBtn, fTop3, fTop3ScreenPos);

  if (_registry._LibHorizonMode)
  {
    //ImGui::SameLine ( );
    //ImGui::SetCursorPosX (ImGui::GetCursorPosX() - ((_registry.bUIBorders) ? 4.0f : 7.0f) * SKIF_ImGui_GlobalDPIScale);

    ImGui::SetCursorPos (ImFloor (ImVec2 (fTop1.x + sizeList.x - ImGui::GetStyle().WindowPadding.x / 2.0f + 3.0f * SKIF_ImGui_GlobalDPIScale,
                                          fTop1.y)));
  }

  else {
    ImGui::SetCursorPosY (ImFloor (fTop2.y + sizeList.y - (fTop3.y - fTop1.y))); // + ImGui::GetStyle().FrameBorderSize * 2.0f // Technically 1 px off when not using borders
  }

  ImRect gamesList = ImRect (fTop3ScreenPos, ImVec2 (fTop3ScreenPos.x + sizeList.x, ImGui::GetCursorScreenPos().y));

  // Open the Empty Space Menu
  //ImGuiContext& g = *ImGui::GetCurrentContext();
  //if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)

  if (SKIF_ImGui_IsViewportFocused ( ) &&
      EmptySpaceMenu == PopupState_Closed              &&
      AddGamePopup   == PopupState_Closed              &&
      ModifyGamePopup== PopupState_Closed              &&
      IconMenu != PopupState_Open                      &&
    ! ImGui::IsAnyItemHovered ( )                      &&
      ImGui::IsMouseClicked   (ImGuiMouseButton_Right) &&
    ! SKIF_ImGui_IsAnyPopupOpen ( )                    &&
      ImGui::IsMouseHoveringRect (gamesList.Min, gamesList.Max, false))
    EmptySpaceMenu = PopupState_Open;

  // This disables OS based drag-move modal on the list of games (required for autoscroll in touch mode)
  if (_registry._TouchDevice && EmptySpaceMenu == PopupState_Closed && ImGui::IsMouseHoveringRect (gamesList.Min, gamesList.Max, false))
  {
    extern bool SKIF_MouseDragMoveAllowed;
    SKIF_MouseDragMoveAllowed = false;
  }

#pragma region GameDetails

  if (uiDetailsVisible)
  {
    if (! _registry.bUIBorders && ! _registry._LibHorizonMode)
    {
      ImGui::Separator ( );

      ImGui::SetCursorPosY (
        ImGui::GetCursorPosY ( )
          - ImGui::GetStyle().ItemInnerSpacing.y
      );
    }

    ImGui::BeginChild (
      "###GameDetails",
        ImVec2 ((sizeDetails.x - ImGui::GetStyle().WindowPadding.x / 2.0f),
                 sizeDetails.y),
          ImGuiChildFlags_AlwaysUseWindowPadding | (_registry.bUIBorders ? ImGuiChildFlags_Border : ImGuiChildFlags_None),
          ImGuiWindowFlags_NoScrollbar       |
          ImGuiWindowFlags_NoScrollWithMouse |
          ImGuiWindowFlags_NavFlattened      |
          (_registry.bUIBorders || _registry._LibHorizonMode ? ImGuiChildFlags_None : ImGuiWindowFlags_NoBackground) // ImGuiWindowFlags_NoBackground apparently disables the outside border
    );
    ImGui::BeginGroup ();

    if (_registry.bFadeCovers)
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, fAlphaList);

    if (pApp != nullptr && ! isSpecialK)
      DrawGameDetails (pApp);
    else
      _inject._GlobalInjectionCtl ( );

    if (_registry.bFadeCovers)
      ImGui::PopStyleVar ( ); // -ImGuiStyleVar_Alpha

    ImGui::EndGroup     ( );
    ImGui::EndChild     ( );
  }

#pragma endregion

  ImGui::EndGroup     ( );
  // LIST + DETAILS STOP

#pragma region SpecialKPatreon

  // Special K's Patreon panel at the bottom of the cover (library/library_cover.cpp)
  DrawSpecialKPatreon (isCoverHovered, vecPosCover);

#pragma endregion

  static HANDLE hRefreshSignal =
    CreateEvent (nullptr, FALSE, FALSE, nullptr);

  static HANDLE hRefreshThread =
    CreateThread (nullptr, 0, [](void*) -> DWORD
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibRefreshWorker");

      while (WaitForSingleObject (hRefreshSignal, INFINITE) == WAIT_OBJECT_0)
      {
        SKIF_GamingCollection::RefreshRunningApps (&_games.GetApps());
      }

      return 0;
    }, nullptr, 0, nullptr
  );

  // Refresh running state of SKIF Custom, Epic, GOG, and Xbox titles
  SetEvent (hRefreshSignal);

#pragma region ServiceMenu

  SKIF_ImGui_ServiceMenu ( );

#pragma endregion

  // Context menu for the cover (library/library_menus.cpp)
  DrawCoverMenu ( );

#pragma region GamesList::GameContextMenu

  if (pApp != nullptr)
  {
    if (ImGui::BeginPopup ("###GameContextMenu", ImGuiWindowFlags_NoMove))
    {
      // Context menu should not have any navigation highlight (white border around items)
      ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

      // Show a loading label if the data is currently being processed
      if (pApp->loading)
        ImGui::TextDisabled ("Loading...");
      else if (pApp->id == SKIF_STEAM_APPID)
        DrawSpecialKContextMenu (pApp);
      else
        DrawGameContextMenu     (pApp);

      //else if (! update)
      //  ImGui::CloseCurrentPopup ();

      ImGui::PopStyleColor   ( );
      ImGui::EndPopup ( );
    }

    // This is below the menu because it allows us to open the menu on the next frame,
    //   after the cache and whatnot has been updated.
    if (GameMenu == PopupState_Open)
    {
      ImGui::OpenPopup    ("###GameContextMenu");
      GameMenu = PopupState_Closed;
    }
  }

#pragma endregion

  // Context menu for a game's icon (library/library_menus.cpp)
  DrawIconMenu ( );

  // Context menu for the empty space in the games list (library/library_menus.cpp)
  DrawEmptySpaceMenu (showClearBtn);

#pragma region GameLaunchLogic

  if ((launchGame || launchGameMenu || launchInstant) &&
       pApp != nullptr && launchConfig != nullptr)
  {
    LaunchGame         (pApp);

    launchConfig     = !pApp->launch_configs.empty () ? &pApp->launch_configs.begin()->second
                                                      : nullptr; // Reset to primary launch config
    launchGame       = false;
    launchGameMenu   = false;
    launchInstant    = false;
    launchWithoutSK  = false;
  }

#pragma endregion

#pragma region SKIF_LibCoverWorker

  // Streams the cover in on a background thread once the cover area is visible (not while a custom cover is being saved for this game)
  if (uiCoverVisible && _art.IsCoverRequested ( ) && PopulatedGames && ! _art.IsSavingCover (pApp))
    _art.StartCoverWorker (pApp);

#pragma endregion

#pragma region Popups::KeyboardSearch

  if (AddGamePopup        == PopupState_Closed &&
      ModifyGamePopup     == PopupState_Closed &&
      PopupCategoryModify == PopupState_Closed &&
      RemoveGamePopup     == PopupState_Closed &&
      ! io.KeyCtrl)
  {
    extern bool
          INIEditorActive;
    if (! INIEditorActive)
      SearchAppsList ( );
  }

#pragma endregion

  // Popups: add / modify / remove game, category management, Steam DLC notice, ... (library/library_popups.cpp)
  DrawLibraryPopups ( );

  auto UpgradeDLL = [&](std::wstring src_path)
  {
    const bool is_64_bit =
      StrStrIW (src_path.c_str (), L"SpecialK64.dll");

    const std::wstring local_path =
      is_64_bit ? L"SpecialK64.dll" :
                  L"SpecialK32.dll";

    const auto orig_ver =
      SK_WideCharToUTF8 (
        SKIF_Util_GetFileVersion (local_path.c_str ())
      );

    const bool bWasRunning =
      (_inject.bHasServlet && _inject.bCurrentState),
               bWasWaiting =
      (_inject.bHasServlet && _inject.bAckInj);

    if (bWasRunning) {
      _inject._StartStopInject (true, bWasWaiting, launchConfig->isElevated (), pApp->skif.auto_stop);
      // XXX: This delay helps but does not completely solve wrong reported versions
      Sleep (500UL);
    }

    if (! DeleteFileW (local_path.c_str ()))
    {
      const std::wstring old_file =
        (local_path + L".old");

      if (PathFileExistsW (old_file.c_str ())) {
        if (! DeleteFileW (old_file.c_str ()))
        {
          const std::wstring older_file =
            (old_file + L".old");

          DeleteFileW (older_file.c_str ());
          MoveFileExW (  old_file.c_str (), older_file.c_str (), MOVEFILE_REPLACE_EXISTING);
        }
      }

      MoveFileExW (local_path.c_str (), old_file.c_str (), MOVEFILE_REPLACE_EXISTING);
    }

    if (PathIsURLW (src_path.c_str ()))
    {
      SKIF_Util_GetWebResource (src_path, local_path.c_str ());
    }

    else
    {
      CopyFile (src_path.c_str (),        local_path.c_str (), FALSE);
    }

    const auto new_ver =
      SK_WideCharToUTF8 (
        SKIF_Util_GetFileVersion (local_path.c_str ())
      );

    static constexpr char* str32 = "New 32-bit DLL Installed";
    static constexpr char* str64 = "New 64-bit DLL Installed";

    ImGui::InsertNotification ({
      ImGuiToastType::Success, 5000,
      (is_64_bit ? str64 : str32),
      "%s -> %s",
       orig_ver.c_str(),
        new_ver.c_str()
    });

    _inject._RefreshSKDLLVersions ();
    _inject._RefreshUIQuickToggle (bWasRunning);
  };

  if (! dragDroppedFilePath.empty())
  {
    PLOG_VERBOSE << "New drop was given: " << dragDroppedFilePath;

    const std::filesystem::path fsPath (dragDroppedFilePath.data());

    std::wstring ext =
      SKIF_Util_ToLowerW (fsPath.extension ().wstring ());

    if (PathIsURLW (dragDroppedFilePath.c_str ()))
    {
      wchar_t    wszExt [MAX_PATH + 1] = {};
      wcsncpy_s (wszExt, MAX_PATH, ext.c_str (), _TRUNCATE);

      // Strip the ? from the extension
      if (wchar_t* url_args_delim  = StrStrW (wszExt, L"?");
                   url_args_delim != nullptr)
      {
        *url_args_delim = L'\0';
        ext             = wszExt;
      }
    }

    PLOG_VERBOSE << "    File extension: " << ext;

    bool isExecutable =
      (ext == L".exe"  ||
       ext == L".lnk"  ||
       ext == L".bat"  );

    // Executables / Shortcuts
    if (isExecutable)
    {
      dragDroppedFilePathGame = dragDroppedFilePath;

      if (PopupMessageInfo != PopupState_Closed)
        SKIF_ImGui_CloseInfoPopup ( );

      if (AddGamePopup == PopupState_Closed)
        AddGamePopup = PopupState_Open;
    }

    // Special K DLLs
    else if (ext == L".dll" && (StrStrIW (dragDroppedFilePath.c_str (), L"SpecialK64.dll") ||
                                StrStrIW (dragDroppedFilePath.c_str (), L"SpecialK32.dll")))
    {
      UpgradeDLL (dragDroppedFilePath);
    }

    else if (ext == L".zip" && (StrStrIW (dragDroppedFilePath.c_str (), L"SpecialK")))
    {
      CreateDirectoryW (L"Version", nullptr);

      std::wstring destPath = SK_FormatStringW (LR"(%ws\Version\)",                         _path_cache.specialk_userdata);
      std::wstring zipPath  = SK_FormatStringW (LR"(%ws\Version\SpecialK_DropInstall.zip)", _path_cache.specialk_userdata);

      if (PathIsURLW (dragDroppedFilePath.c_str ()))
        SKIF_Util_GetWebResource (dragDroppedFilePath, zipPath);

      else
        CopyFile (dragDroppedFilePath.c_str (),        zipPath.c_str(), false);

      std::vector <std::pair <std::wstring, HRESULT>> targets = {
        { L"SpecialK32.dll", S_OK },
        { L"SpecialK64.dll", S_OK }
      };

      if (SUCCEEDED (SKIF_Util_ExtractFromZip (zipPath.c_str(), destPath.c_str(), &targets)))
      {
        // Wait for up to 5 seconds for the archive to extract and files to flush
        int            retries              = 0;
        constexpr auto retry_interval_in_ms = 50UL;

        retries = 0;

        while (! DeleteFileW (zipPath.c_str()))
        {
          Sleep (retry_interval_in_ms);

          if (++retries > 50)
          {
            SetLastError (ERROR_FILE_INVALID);
            break;
          }
        }

        if (GetLastError ( ) != NO_ERROR)
          PLOG_ERROR << "An unexpected error occurred when trying to delete the ZIP archive: " << SKIF_Util_GetErrorAsWStr ( );

        bool failed = true;

        for (auto& target : targets)
        {
          if (target.second == S_OK)
          {
            failed = false;
            PLOG_INFO << "Extraction was successful.";
            std::wstring path = LR"(Version\)" + target.first;

            // Swap in the extracted files
            UpgradeDLL  (path);
            DeleteFileW (path.c_str());
          }
        }

        if (failed)
          ImGui::InsertNotification ({
            ImGuiToastType::Error, 5000,
            "Missing Files",
            "Unable to extract Special K DLL files from the provided .zip file."
          });
      }
    }

    else if (ext == L".7z" && (StrStrIW (dragDroppedFilePath.c_str (), L"SpecialK")))
    {
      int sk64_year = 0, sk64_month = 0,
          sk64_day  = 0, sk64_rev   = 0;

      std::wstring install_helper_dll =
        L"SpecialK64.dll";
      std::wstring install_helper_ver =
        SKIF_Util_GetFileVersion (install_helper_dll.c_str ());

      std::swscanf (
        install_helper_ver.c_str (), L"%d.%d.%d.%d",
          &sk64_year, &sk64_month, &sk64_day, &sk64_rev
      );

      // Make a backup so that rolling-back to old versions still allows this feature.
      if (sk64_year > 25 || (sk64_year == 25 && sk64_month  > 3) ||
                            (sk64_year == 25 && sk64_month == 3 && sk64_day >= 11))
      {
        CopyFileW (L"SpecialK64.dll", LR"(Version\SpecialK_7ZipHelper.dll)", TRUE);
      }

      install_helper_dll      = LR"(Version\SpecialK_7ZipHelper.dll)";
      install_helper_ver      =
        SKIF_Util_GetFileVersion (install_helper_dll.c_str ());
      std::swscanf (
        install_helper_ver.c_str (), L"%d.%d.%d.%d",
          &sk64_year, &sk64_month, &sk64_day, &sk64_rev
      );

      if (sk64_year > 25 || (sk64_year == 25 && sk64_month  > 3) ||
                            (sk64_year == 25 && sk64_month == 3 && sk64_day >= 11))
      {
        const auto orig_ver32 =
          SKIF_Util_GetFileVersion (L"SpecialK32.dll"),
                   orig_ver64 =
          SKIF_Util_GetFileVersion (L"SpecialK64.dll");

        CreateDirectoryW (L"Version", nullptr);

        if (PathIsURLW (dragDroppedFilePath.c_str ()))
        {
          SKIF_Util_GetWebResource (dragDroppedFilePath, LR"(Version\SpecialK_DropInstall.7z)");
        }

        else
        {
          CopyFile (dragDroppedFilePath.c_str (),        LR"(Version\SpecialK_DropInstall.7z)", false);
        }

        wchar_t              wszRunDLL32 [MAX_PATH + 2] = { };
        GetSystemDirectoryW (wszRunDLL32, MAX_PATH);
        PathAppendW         (wszRunDLL32, L"rundll32.exe");
        static std::wstring wszExtractCall = SK_FormatStringW (
          LR"("%ws\%ws",RunDLL_Extract7Zip SpecialK_DropInstall.7z)",
           _path_cache.specialk_install, install_helper_dll.c_str ()
        );

        SHELLEXECUTEINFOW
          sexi              = { };
          sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
          sexi.lpVerb       = L"open";
          sexi.lpFile       = wszRunDLL32;
          sexi.lpParameters = wszExtractCall.c_str ();
          sexi.nShow        = SW_HIDE;
          sexi.fMask        = SEE_MASK_NOASYNC | SEE_MASK_NOZONECHECKS;

        SetLastError (NO_ERROR);

        wchar_t                         wszCurrentDir [MAX_PATH] = {};
        wchar_t                         wszInstallDir [MAX_PATH] = {};
        GetCurrentDirectoryW (MAX_PATH, wszCurrentDir);
        GetCurrentDirectoryW (MAX_PATH, wszInstallDir);

        PathAppendW          (wszInstallDir, L"Version");
        CreateDirectoryW     (wszInstallDir, nullptr);
        SetCurrentDirectoryW (wszInstallDir);

        bool ret =
          ShellExecuteExW (&sexi);

        SetCurrentDirectoryW (wszCurrentDir);

        // Wait for up to 5 seconds for the archive to extract and files to flush
        int            retries              = 0;
        constexpr auto retry_interval_in_ms = 50UL;

        if (ret)
        {
          while (! (PathFileExistsW (LR"(Version\SpecialK64.dll)") &&
                    PathFileExistsW (LR"(Version\SpecialK32.dll)")))
          {
            Sleep (retry_interval_in_ms);

            if (++retries > 50)
            {
              ImGui::InsertNotification ({
                ImGuiToastType::Error, 5000,
                "Missing Files",
                "Unable to extract Special K DLL files from the provided .7z file."
              });
              SetLastError (ERROR_FILE_INVALID);
              ret = false;
              break;
            }
          }

          retries = 0;

          while (! DeleteFileW (LR"(Version\SpecialK_DropInstall.7z)"))
          {
            Sleep (retry_interval_in_ms);

            if (++retries > 50)
            {
              SetLastError (ERROR_FILE_INVALID);
              ret = false;
              break;
            }
          }
        }

        if (GetLastError ( ) != NO_ERROR)
          PLOG_ERROR << "An unexpected error occurred: " << SKIF_Util_GetErrorAsWStr();

        if (ret)
        {
          PLOG_INFO  << "The operation was successful.";

          UpgradeDLL (LR"(Version\SpecialK64.dll)");
          UpgradeDLL (LR"(Version\SpecialK32.dll)");

          DeleteFileW (LR"(Version\SpecialK64.dll)");
          DeleteFileW (LR"(Version\SpecialK32.dll)");
        }
        else
          PLOG_ERROR << "The operation was unsuccessful.";
      }

      else
      {
        SKIF_ImGui_InfoMessage ("Unsupported Feature",
       "SKIF cannot perform a drag-n-drop update because Special K 25.3.11 or newer is required.");
      }
    }

    // Images + URLs
    else if (SaveGameCover (pApp, dragDroppedFilePath))
    {
      // Shows the "..." loading dots, and sets up the current cover to be released
      _art.BeginSavingCover (pApp);

      // A child thread will post WM_SKIF_REFRESHCOVER once done
    }

    else {
      constexpr char* error_title =
        "Unsupported file format";
      constexpr char* error_label =
        "Add a game to the library:\n"
        "   *.exe\n"
        "   *.lnk\n"
        "   *.bat\n"
        "\n"
        "\n"
        "Update the cover for the current selected game:\n"
        "   *.jpg\n"
      //"   *.jxr\n"
        "   *.png\n"
        "   *.webp\n"
      //"   *.tga\n"
        "   *.bmp\n"
        "   *.psd\n"
      //"   *.gif\n"
      //"   *.tif\n"
        "\n"
        "Note that the app has no support for animated images.";

      SKIF_ImGui_InfoMessage (error_title, error_label);
    }

    dragDroppedFilePath.clear();
  }


  // START FADE/DIM LOGIC

  // Every >15 ms, increase/decrease the cover fade effect (makes it frame rate independent)
  static DWORD timeLastTick;
  bool         incTick = false;

  // Fade in/out transition

  if (_registry.bFadeCovers)
  {
    // Cover cross-fade, and the Special K logo
    if (_art.StepFades (isSpecialK, (current_time - timeLastTick > 15), incTick))
      coverFadeActive = true;

    // Fade in the games list (but only on the next frame)
    if (fAlphaList < 1.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaList += 0.05f;
        incTick     = true;
      }

      coverFadeActive = true;
    }
  }

  // Dim covers

  if (_registry.iDimCovers == 2)
  {
    if (isCoverHovered && fTint < 1.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fTint = fTint + 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }

    else if (! isCoverHovered && fTint > fTintMin)
    {
      if (current_time - timeLastTick > 15)
      {
        fTint = fTint - 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }
  }

  // Increment the tick
  if (incTick)
    timeLastTick = current_time;

  // END FADE/DIM LOGIC

  // A custom cover has been saved, or cover settings changed: reload the cover if it is the one shown
  if (_art.ProcessCoverRefresh ( ))
    update = true;

  // If we have changed mode, we need to reload the cover to ensure the proper resolution of it
  static bool
      lastLowResState  = (_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass);
  if (lastLowResState != (_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass) && uiCoverVisible && ! ImGui::IsAnyMouseDown ( ))
  {   lastLowResState  = (_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass);

    update    = true;
    _art.ForgetCover ( ); // Needed as otherwise SKIF would not reload the cover
  }

  extern uint32_t SelectNewSKIFGame;

  if (SelectNewSKIFGame > 0)
  {
    // Change selection to the new game
    selection.game     = { app_record_s::Store::Custom, SelectNewSKIFGame };
    selection.category = "Games";

    update = true;

    SelectNewSKIFGame = 0;
  }

  // In case of a device reset, unload all currently loaded textures
  if (invalidatedDevice == 1)
  {   invalidatedDevice  = 2;

    _art.ReleaseTextures   ( ); // Covers and game icons
    _games.ReleaseTextures ( ); // Patreon and Special K logo

    // TODO: Make away with RepopulateGames = true from here -- we shouldn't have to reload all games just to refresh textures
    // Trigger a refresh of the list of games, which will reload all icons and the Patreon texture
    RepopulateGames = true;
    // Trigger a refresh of the cover
    _art.ReloadCover ( );
  }

  if (_registry.bLibrarySteam)
  {
    if (steamRunning)
      steamFallback = false;

    else if (! steamFallback && appinfo != nullptr)
    {
      SK_RunOnce (PLOG_DEBUG << "[AppInfo Processing] Started processing games...");

      bool fallbackAvailable = true;

      for (auto& app : _games.GetApps())
      {
        // Steam: games whose appinfo.vdf data has not been parsed yet
        if (! app.second->HasPendingStoreDetails ( ))
          continue;

        //PLOG_DEBUG << "[AppInfo Processing] " << "[" << ImGui::GetFrameCount ( ) << "] Processing " << app.second.id << "...";
        app.second->LoadStoreDetails ( );

        fallbackAvailable = false;
        break;
      }

      steamFallback = fallbackAvailable;

      if (steamFallback)
        SK_RunOnce (PLOG_DEBUG << "[AppInfo Processing] Finished processing games!");
    }

#if 0
    if (SKIF_StatusBarHelp.empty() && SKIF_StatusBarText.empty())
    {
      SKIF_StatusBarText = "Debug: ";
      SKIF_StatusBarHelp = (steamRunning) ? "Steam client running" : (steamFallback) ? "Steam process fallback active" : "Steam process fallback disabled";
    }
#endif
  }
}

//
// Copyright 2020 Andon "Kaldaien" Coleman
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

#include <utility/injection.h>

#include "DirectXTex.h"

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
#include <concurrent_queue.h>
#include <utility/fsutil.h>
#include <atlimage.h>
#include <TlHelp32.h>
#include <gsl/gsl_util>

#include <utility/registry.h>
#include <utility/updater.h>
#include <stores/Steam/steam_library.h>

const int              SKIF_STEAM_APPID  = 1157970;
bool                   SKIF_STEAM_OWNER  = false;
bool                   steamRunning      = false;
bool                   steamFallback     = false;
bool                   loadCover         = false;
bool                   tryingToLoadCover = false;
bool                   tryingToSaveCover = false;
std::atomic<bool>      gameCoverLoading  = false;
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

bool                   coverRefresh      = false; // This just triggers a refresh of the cover
uint32_t               coverRefreshAppId = 0;
int                    coverRefreshStore = 0;
int                    numRegular        = 0;
int                    numPinnedOnTop    = 0;

// Support up to 15 running games at once, lol
SKIF_Util_CreateProcess_s iPlayCache[15] = { };

struct SKIF_Lib_GameWorkerThread_s {
  app_record_s          app        = app_record_s (0);
  std::set<std::string> apptickets = std::set<std::string>( );
  HANDLE                hWorker    = NULL;
  bool                  free       = true;
  int                   cpu_pre    = -1;
};

#define MAX_GAMEWORKER 3
SKIF_Lib_GameWorkerThread_s aGameWorkers[MAX_GAMEWORKER] = { };


std::vector <
  std::pair < std::string, app_record_s >
            > g_apps;

std::set    < std::string >
              g_apptickets;

nlohmann::json jsonMetaDB;

const float fTintMin     = 0.75f;
      float fTint        = 1.0f;
      float fAlpha       = 0.0f;
      float fAlphaSK     = 0.0f;
      float fAlphaPrev   = 1.0f;
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

constexpr int maxCategoryNameLen = 50;
struct change_category_s {
  std::string  Name = "",    // Holds the old name
            newName = "";    // Holds the new name
  bool       change = false; // Rename + Remove
  bool       remove = false; // Remove
  bool       rename = false; // Rename (through collapsible header)
  bool       exists = false; // If the new category already exists
} static_category;

std::wstring file_metadata;
std::wstring dragDroppedFilePath = L"";
std::wstring dragDroppedFilePathGame = L"";

extern bool            allowShortcutCtrlA;
extern ImVec2          SKIF_vecServiceMode;
extern ImVec2          SKIF_vecHorizonMode;
extern ImVec2          SKIF_vecAlteredSize;
extern float           SKIF_ImGui_GlobalDPIScale;
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern std::wstring    SKIF_Epic_AppDataPath;
extern DWORD           invalidatedDevice;
extern bool            GOGGalaxy_Installed;
extern std::wstring    GOGGalaxy_Path;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

// DPI-aware
float tab_scrollbar_width; // Holds the width of the vertical (Y) tab scrollbar (if present)
float lib_column2_width;   // Holds the width of column2 (list + details)

//#define _WIDTH_SCROLLBAR (ImGui::GetCurrentWindowRead()->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f) // SKIF_vecAlteredSize.y
//#define _WIDTH           ((_registry.bHorizonMode) ? SKIF_ImGui_GlobalDPIScale * 376.0f : (SKIF_vecServiceMode.x)) - tab_scrollbar_y // GameList, GameDetails, Injection_Summary_Frame (prev. 414.0f) // 378 // 376.0f
// 1038px == 415px
// 1000px == 377px (using 380px)
//#define _HEIGHT  (620.0f * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x - 2.0f) // GameList
//#define _HEIGHT2 (280.0f * SKIF_ImGui_GlobalDPIScale)                                             // GameDetails

struct terminate_process_s {
  uint32_t      pid = 0;
  HANDLE     handle = INVALID_HANDLE_VALUE;
  std::string  name = "";
} static static_proc;

std::atomic<int>  textureLoadQueueLength{ 1 };

static int getTextureLoadQueuePos (void) {
  return textureLoadQueueLength.fetch_add(1) + 1;
}

CComPtr <ID3D11ShaderResourceView> pPatTexSRV;
CComPtr <ID3D11ShaderResourceView> pSKLogoTexSRV;
CComPtr <ID3D11ShaderResourceView> pSKLogoTexSRV_small;

// External declaration
extern void SKIF_Shell_AddJumpList (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService);

// Functions / Structs

static float
AdjustAlpha (float a)
{
  return std::pow (a, 1.0f / 2.2f );
}

static std::string
GetEffectiveCategory (app_record_s* app)
{
  return (    app->skif.pinned > 0 || (app->skif.pinned == -1 && app->steam.shared.favorite == 1))
         ?   "Favorites"
         : (! app->skif.category.empty())
         ?    app->skif.category
         :   "Games";
}

#pragma region Trie Keyboard Hint Search

struct {
  uint32_t            id = 0;
  app_record_s::Store store;
  std::string         category = "";
} static search_selection;

Trie labels;
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
      search_selection.id       = result.app_id;
      search_selection.store    = result.store;
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

    Trie* searchLabels = (charFilter[0] != '\0') ? &labelsFiltered : &labels;
      
    // Prioritize trie search first
    if (searchLabels->search (test_))
    {
      for (auto& app : g_apps)
      {
        // Skip invalid/hidden and filtered ones
        if (app.second.id == 0 || app.second.filtered)
          continue;

        if (app.second.names.all_upper_alnum.find (test_) == 0)
        {
          result.text     = app.second.names.normal;
          result.store    = app.second.store;
          result.app_id   = app.second.id;
          result.category = GetEffectiveCategory (&app.second);
          result.pos      = app.second.names.pre_stripped;
          result.len      = strlen (test_);

          // Handle cases where articles are ignored

          // Add one to the length if the regular all_upper cannot find a match
          // as this indicates a stripped character in the found pattern
          if (app.second.names.all_upper.find (test_) != 0)
            result.len++;

          break;
        }
      }
    }

    // Fall back to using free text search when the trie fails us
    else
    {
      for (auto& app : g_apps)
      {
        // Skip invalid/hidden and filtered ones
        if (app.second.id == 0 || app.second.filtered)
          continue;

        size_t 
            pos  = app.second.names.all_upper.find (test_);
        if (pos != std::string::npos ) // == 0 
        {
          result.text   = app.second.names.normal;
          result.store  = app.second.store;
          result.app_id = app.second.id;
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

    ImGui::OpenPopup         ("###KeyboardHint");

    ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("###KeyboardHint", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
    {
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

      ImGui::EndPopup ( );
    }
  }
}

#pragma endregion


// This writes the Json object to the disk
bool
JsonDB_WriteFile (void)
{
  if (! jsonMetaDB.is_discarded())
  {
    std::ofstream out_file(file_metadata);
    out_file << std::setw(2) << jsonMetaDB << std::endl;
    out_file.close();

    return true;
  }

  else
    PLOG_ERROR << "Could not write metadata to JSON file as the metadata had been discarded.";

  return false;
}

// This both updates the metadata and optionally writes the new object to the disk
bool
JsonDB_UpdateApp (app_record_s* pApp, bool bWriteToDisk)
{
  // Update the db.json file with any new values
  if (! jsonMetaDB.is_discarded())
  {
    std::string item = (pApp->store == app_record_s::Store::Epic)  ? pApp->epic.name_app     :
                       (pApp->store == app_record_s::Store::Xbox)  ? pApp->xbox.package_name :
                                                     std::to_string (pApp->id);

    try {
      auto& key = jsonMetaDB[pApp->store_utf8][item];

      // Update the values in the Json object
      key = {
        { "Name",     pApp->skif.name      },
        { "CPU",      pApp->skif.cpu_type  },
        { "AutoStop", pApp->skif.auto_stop },
        { "Hidden",   pApp->skif.hidden    },
        { "Uses",     pApp->skif.uses      },
        { "Used",     pApp->skif.used      },
        { "Category", pApp->skif.category  },
        { "Pin",      pApp->skif.pinned    }
      };

      if (pApp->store == app_record_s::Store::Steam ||
          pApp->store == app_record_s::Store::GOG   ||
          pApp->store == app_record_s::Store::Xbox)
        key += { "InstantPlay", pApp->skif.instant_play };

      return ! bWriteToDisk || JsonDB_WriteFile ( );
    }
    catch (const std::exception&)
    {
      PLOG_ERROR << "Error occurred when trying to update the JSON metadata for " << item;
    }
  }

  else
    PLOG_ERROR << "Could not write metadata to JSON file as the metadata had been discarded.";

  return false;
}

// This updates the category name of all items in the JSON
bool
JsonDB_SetCategoryName (std::string szOld, std::string szNew)
{
  if (szOld.empty())
    return false;

  if (! jsonMetaDB.is_discarded())
  {
    for (auto& platform : jsonMetaDB)
    {
      for (auto& game : platform)
      {
        if (! game.empty() && game.contains("Category") && game.at("Category") == szOld)
          game.at("Category") = szNew;
      }
    }

    return JsonDB_WriteFile ( );
  }

  else
    PLOG_ERROR << "Could not write metadata to JSON file as the metadata had been discarded.";

  return false;
}

static std::string
GetSteamCommandLaunchOptions (app_record_s* pApp, app_record_s::launch_config_s* pLaunchCfg)
{
  if (! pApp->steam.local.launch_option_parsed.empty())
    return pApp->steam.local.launch_option_parsed;

  // Check if there is a custom launch option set up
  if (pApp->steam.local.launch_option.size() > 0)
  {
    std::string strSteam_Command  = "%command%"; // Only for Steam games
    size_t start_pos = SKIF_Util_ToLower (pApp->steam.local.launch_option).find (strSteam_Command);

    // Check if a %COMMAND% is a part of the launch options
    // %COMMAND% is special in that the original developer-specified launch options are placed at its exact position,
    //   making the user-specified Steam launch options the _primary_ and the developer-specified launch options _secondary_
    if (start_pos != std::string::npos)
    {
      // Base is dev-specified executable
      std::string newCmdLine = pLaunchCfg->getExecutableFullPathUTF8 ( );
      std::string strSteamLO = pApp->steam.local.launch_option;

      // Appended is dev-specified cmd line arguments
      if (! pLaunchCfg->getLaunchOptionsUTF8().empty())
        newCmdLine += " " + pLaunchCfg->getLaunchOptionsUTF8();

      // Replace %command% with the dev-specified exe and args
      strSteamLO.replace (start_pos, strSteam_Command.length(), newCmdLine);

      // Swap in the result and flag to use shell execute
      pApp->steam.local.launch_option_parsed = strSteamLO;
    }
  }

  return pApp->steam.local.launch_option_parsed;
}

#pragma region LaunchGame

static void
LaunchGame (app_record_s* pApp)
{
//static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( ); // Not currently used
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  if (launchConfig == nullptr)
    return;

  DWORD current_time = SKIF_Util_timeGetTime ( );

  if ( pApp->store != app_record_s::Store::Steam && pApp->store != app_record_s::Store::Epic &&
      ! launchConfig->isExecutableFullPathValid ( ))
  {
    SKIF_ImGui_InfoMessage ("Missing executable", "Could not launch game due to missing executable:\n\n" + launchConfig->getExecutableFullPathUTF8 ( ));
  }

  else {
    bool localInjection        = (pApp->specialk.injection.injection.type == InjectionType::Local);
    bool usingSK               = localInjection;

    // Increment the uses count and used timestamp
    time_t ltime;
    time (&ltime);
    pApp->skif.used           = std::to_string(ltime);
    pApp->skif.used_formatted = SK_WideCharToUTF8 (SKIF_Util_timeGetTimeAsWStr (ltime));
    pApp->skif.uses++;


    // SERVICE PREPARATIONS


    // Check if the injection service should be used
    if (! usingSK)
    {
      bool isLocalBlacklisted  = launchConfig->isBlacklisted ( ),
           isGlobalBlacklisted = _inject._TestUserList (launchConfig->getExecutableFullPathUTF8 ( ).c_str (), false);

      usingSK = ! launchWithoutSK       &&
                ! isLocalBlacklisted    &&
                ! isGlobalBlacklisted;

      if (usingSK)
      {
        // Whitelist the path if it haven't been already
        // Instant launches needs to fall back to the regular approach
        if (pApp->store == app_record_s::Store::Xbox && ! launchInstant)
        {
          if (! _inject._TestUserList (SK_WideCharToUTF8 (pApp->xbox.directory_app).c_str(), true))
          {
            if (_inject.WhitelistPattern (pApp->xbox.package_name))
              _inject.SaveWhitelist ( );
          }
        }

        else
        {
          if (launchConfig->isExecutableFullPathValid ( ) &&
              _inject.WhitelistPath (launchConfig->getExecutableFullPathUTF8 ( )))
            _inject.SaveWhitelist ( );
        }

        // Disable the first service notification
        if (_registry.bMinimizeOnGameLaunch)
          _registry._SuppressServiceNotification = true;
      }

      if (_inject.bHasServlet)
      {
        // Kickstart service if it is currently not running
        if (usingSK && ! _inject.bCurrentState)
          _inject._StartStopInject (false, true, launchConfig->isElevated( ), pApp->skif.auto_stop);

        // Stop the service if the user attempts to launch without SK
        else if (! usingSK && _inject.bCurrentState)
          _inject._StartStopInject (true);
      }

      PLOG_ERROR_IF (! _inject.bHasServlet) << "The injection service could not be started due to missing critical files! Please reinstall Special K using the latest installer.";
    }

    // Create the injection acknowledge events in case of a local injection
    else {
      _inject.SetInjectAckEx     (true);
      _inject.SetInjectExitAckEx (true);
    }


    // LAUNCH PREPARATIONS


    if (! launchInstant)
    {
      // Custom games always use instant launch
      if (pApp->store == app_record_s::Store::Custom)
        launchInstant = true;

      // Fallback for GOG games if the Galaxy client is not installed
      else if (pApp->store == app_record_s::Store::GOG && ! GOGGalaxy_Installed)
        launchInstant = true;

      // Convert a few scenarios to an instant launch -- but not when using the game menu as it is always explicit
      else if (! launchGameMenu)
      {
        // Convert all except those where we are dealing with an invalid launch config (e.g. Link2EA)
        if (pApp->store == app_record_s::Store::Steam &&
            (pApp->skif.instant_play == 1 || // Always use Instant Play
            (pApp->skif.instant_play == 0 && _registry.bInstantPlaySteam)) // Use global default (and globally enabled)
        && launchConfig->isExecutableFullPathValid ( ))
          launchInstant = true;

        if (pApp->store == app_record_s::Store::GOG   &&
            (pApp->skif.instant_play == 1 || // Always use Instant Play
            (pApp->skif.instant_play == 0 && _registry.bInstantPlayGOG))) // Use global default (and globally enabled)
          launchInstant = true;
      }
    }


    // LAUNCH PROCEDURES


    // Launch Epic game
    if (pApp->store == app_record_s::Store::Epic)
    {
      PLOG_VERBOSE << "Performing an Epic launch...";

      // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

      std::wstring launchOptions = SK_FormatStringW(LR"(com.epicgames.launcher://apps/%ws?action=launch&silent=true)", launchConfig->getLaunchOptions().c_str());
      if (SKIF_Util_OpenURI (launchOptions) != 0)
      {
        // Don't check the running state for at least 7.5 seconds
        pApp->_status.dwTimeDelayChecks = current_time + 7500;
        pApp->_status.running           = true;

        SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), L"", launchOptions, L"", launchConfig->getExecutableFullPath ( ), (! localInjection && usingSK));
      }
    }

    // Launch Xbox game
    else if (pApp->store == app_record_s::Store::Xbox)
    {
      PLOG_VERBOSE << "Performing an Xbox launch...";

      SKIF_Util_CreateProcess_s* proc = nullptr;
      for (auto& item : iPlayCache)
      {
        if (item.id == 0)
        {
          proc           = &item;
          proc->id       = pApp->id;
          proc->store_id = (int)pApp->store;
          break;
        }
      }

      if (launchInstant)
      {
        SKIF_Util_CreateProcess (
          L"",
          SK_FormatStringW (
            LR"(powershell.exe -Command "$XmlManifest = Select-Xml -Path 'appxmanifest.xml' -XPath '/'; $Applications = $XmlManifest.Node.Package.Applications.Application; $AppId = if ($null -eq $Applications.Count) { $Applications.Id } else { $Applications[%d].Id }; Invoke-CommandInDesktopPackage -AppId $AppId -PackageFamilyName '%ws' -Command '%ws' -PreventBreakaway:$true")",
            launchConfig->id,
            SK_UTF8ToWideChar (pApp->xbox.package_name_family).c_str(),
            launchConfig->getExecutableFullPath().c_str()
          ),
          pApp->install_dir,
          nullptr,
          proc
        );
      }

      else if (SKIF_Util_CreateProcess (launchConfig->executable_helper, L"", L"", nullptr, proc))
      {
        // Don't check the running state for at least 7.5 seconds
        pApp->_status.dwTimeDelayChecks = current_time + 7500;
        pApp->_status.running           = true;

        SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), launchConfig->executable_helper, L"", launchConfig->getExecutableDir(), launchConfig->getExecutableFullPath(), (! localInjection && usingSK));
      }

      if (proc->hWorkerThread.load() == INVALID_HANDLE_VALUE)
      {
        PLOG_DEBUG << "Process creation failed ?!";

        proc->id              =  0;
        proc->store_id        = -1;
      }
    }

    // Instant Play
    // - Custom games
    // - GOG games when Galaxy is not installed
    // - Prefer Instant Play for GOG
    // - Prefer Instant Play for Steam
    else if (launchInstant)
    {
      PLOG_VERBOSE << "Performing an instant launch...";

      bool launchDecision       = true;

      // We need to use proxy variables since we might remove a substring of the launch options
      std::wstring cmdLine = launchConfig->getLaunchOptions();
      bool useShellExecute = false; // This is in case a %COMMAND% Steam call is made

      // Transform to lowercase
      std::wstring cmdLineLower = SKIF_Util_ToLowerW (cmdLine);

      const std::wstring argSKIF_SteamAppID       = L"skif_steamappid=";
      size_t             posSKIF_SteamAppID_start = cmdLineLower.find (argSKIF_SteamAppID);

      // For Steam games, we default to using the pApp->id value
      uint32_t     uiSteamAppID = (pApp->store == app_record_s::Store::Steam) ? pApp->id : 0;
      std::wstring wsSteamAppID = std::to_wstring(uiSteamAppID);

      // Extract the SKIF_SteamAppID cmd line argument, if it exists
      if (posSKIF_SteamAppID_start != std::wstring::npos)
      {
        size_t
          posSKIF_SteamAppID_end    = cmdLineLower.find (L" ", posSKIF_SteamAppID_start);

        if (posSKIF_SteamAppID_end == std::wstring::npos)
          posSKIF_SteamAppID_end    = cmdLineLower.length ( );

        // Length of the substring to remove
        posSKIF_SteamAppID_end -= posSKIF_SteamAppID_start;

        wsSteamAppID = cmdLineLower.substr (posSKIF_SteamAppID_start + argSKIF_SteamAppID.length ( ), posSKIF_SteamAppID_end);

        // Remove substring from the proxy variable
        cmdLine.erase (posSKIF_SteamAppID_start, posSKIF_SteamAppID_end);

        // Try to convert the found string to an unsigned integer
        try {
          uiSteamAppID = std::stoi(wsSteamAppID);
        }

        catch (const std::exception& e)
        {
          UNREFERENCED_PARAMETER(e);
          PLOG_ERROR << "Unable to convert found Steam App ID to integer: " << wsSteamAppID;
          uiSteamAppID = 0;
        }
      }

      std::map<std::wstring, std::wstring> env;
      bool steamOverlay = true;

      if (uiSteamAppID != 0)
      {
        PLOG_DEBUG << "Using Steam App ID : "  << uiSteamAppID;
        env.emplace       (L"SteamAppId",         wsSteamAppID); // Dunno if one is the primary one...
        env.emplace       (L"SteamGameId",        wsSteamAppID); //   ... so let's use both of them...
      //env.emplace       (L"SteamOverlayGameId", wsSteamAppID);
      //env.emplace       (L"EnableConfiguratorSupport", L"0");
          
        steamOverlay = SKIF_Steam_isSteamOverlayEnabled (uiSteamAppID, SKIF_Steam_GetCurrentUser ( ));

        if (! steamOverlay)
        {
          PLOG_DEBUG << "Disabling the Steam Overlay...";
          env.emplace (L"SteamNoOverlayUIDrawing", L"1");
        }

        // If both of these are false, see if there is any custom Steam launch options to append...
        if (! launchConfig->custom_skif &&
            ! launchConfig->custom_user)
        {
          std::string steamLaunchOptions;

          if (pApp->store == app_record_s::Store::Steam)
            steamLaunchOptions = SKIF_Steam_GetLaunchOptions (uiSteamAppID, SKIF_Steam_GetCurrentUser(), pApp);
          
          if (steamLaunchOptions.size() > 0)
          {
            PLOG_DEBUG << "Found additional launch options for this app in Steam: " << steamLaunchOptions;

            std::string steamLaunchOptionsLower =
              SKIF_Util_ToLower (steamLaunchOptions);

            std::string sSteam_Command = "%command%"; // Only for Steam games
            size_t posSteam_Command_start = steamLaunchOptionsLower.find(sSteam_Command);

            // Check if SKIF %COMMAND% is being used, when we're trying to launch without Special K
            if (! usingSK && steamLaunchOptionsLower.find("skif ") == 0)
            {
              launchDecision = false;

              // Escape any percent signs (%) as otherwise ImGui won't be able to show the string properly
              for (auto pos  = steamLaunchOptions.find ('%');          // Find the first occurence
                        pos != std::string::npos;                      // Validate we're still in the string
                        steamLaunchOptions.insert      (pos, R"(%)"),  // Escape the character
                        pos  = steamLaunchOptions.find ('%', pos + 2)) // Find the next occurence
              { }

              SKIF_ImGui_InfoMessage ("Conflicting configuration",
                                      "Could not launch game due to conflicting launch options in Steam:\n"
                                      "\n"
                                    +  steamLaunchOptions + "\n"
                                      "\n"
                                      "Please change the launch options in Steam before trying again.");

              PLOG_WARNING << "Steam game " << pApp->id << " (" << pApp->names.normal << ") was unable to launch due to a conflict with the launch option of Steam!";
            }

            // Check if a %COMMAND% is a part of the launch options
            // %COMMAND% is special in that the original developer-specified launch options are placed at its exact position,
            //   making the user-specified Steam launch options the _primary_ and the developer-specified launch options _secondary_
            else if (posSteam_Command_start != std::string::npos)
            {
              // Base is dev-specified executable
              std::wstring newCmdLine = launchConfig->getExecutableFullPath ( );
              std::wstring wsSteamLO  = SK_UTF8ToWideChar (steamLaunchOptions);

              // Appended is dev-specified cmd line arguments
              if (! cmdLine.empty())
                newCmdLine += L" " + cmdLine;

              // Replace %command% with the dev-specified exe and args
              wsSteamLO.replace(posSteam_Command_start, sSteam_Command.length(), newCmdLine);

              // Swap in the result and flag to use shell execute
              cmdLine = wsSteamLO;
              useShellExecute = true;
            }

            // Append the launch command string with the one from Steam...
            else {
              // If the cmdLine is not empty, add a space as well
              if (! cmdLine.empty())
                cmdLine += L" ";

              cmdLine += SK_UTF8ToWideChar (steamLaunchOptions);
            }
          }
        }
      }

      if (launchDecision)
      {
        SKIF_Util_CreateProcess_s* proc = nullptr;
        for (auto& item : iPlayCache)
        {
          if (item.id == 0)
          {
            proc           = &item;
            proc->id       = pApp->id;
            proc->store_id = (int)pApp->store;
            break;
          }
        }

        std::wstring dirPath = launchConfig->getWorkOrExeDirectory ( );

        // This is a fallback for handling Steam's %COMMAND% scenarios
        if (useShellExecute)
        {
          // Note that any new process will inherit SKIF's environment variables
          if (_registry._LoadedSteamOverlay)
            SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);

          // Set any custom environment variables
          for (auto& var : env)
            SetEnvironmentVariable (var.first.c_str(), var.second.c_str());

          // We need to split cmdLine between executable and parameters
          // Example that we may get: (replace %COMMAND% with game executable and launch options)
          // SKIF %COMMAND%
          // "C:\Windows\System32\Notepad.exe" %COMMAND%
          // and so on and so forth...

          std::wstring exePath;
            
          // First position is a quotation mark, assume executable is surrounded in them...
          if (cmdLine.find(L"\"") == 0)
          {
            exePath = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1);                  // Executable
            cmdLine = cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos); // Parameters
          }

          // Go by spaces instead
          else if (cmdLine.find(L" ") != std::wstring::npos) {
            exePath = cmdLine.substr(0, cmdLine.find(L" "));                      // Executable
            cmdLine = cmdLine.substr(cmdLine.find(L" ") + 1, std::wstring::npos); // Parameters
          }

          // Fallback: cmdLine only has a single non-spaces unquoted path to an executable
          else {
            exePath = cmdLine;
            cmdLine = L"";
          }

          SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = exePath.c_str();
            sexi.lpParameters = cmdLine.c_str();
            sexi.lpDirectory  = dirPath.c_str();
            sexi.nShow        = SW_SHOWNORMAL;
            sexi.fMask        = SEE_MASK_NOCLOSEPROCESS | // We need the PID of the process that gets started
                                SEE_MASK_NOASYNC        | // Never async since we need env variables to be set properly
                                SEE_MASK_NOZONECHECKS;    // No zone check needs to be performed
              
          PLOG_INFO                       << "Performing a ShellExecuteEx call...";
          PLOG_INFO_IF(! exePath.empty()) << "File      : " << exePath;
          PLOG_INFO_IF(! cmdLine.empty()) << "Parameters: " << cmdLine;
          PLOG_INFO_IF(! dirPath.empty()) << "Directory : " << dirPath;

          // Attempt to execute the call
          if (ShellExecuteExW (&sexi))
            proc->hProcess = sexi.hProcess;

          else {
            PLOG_ERROR << "Shell execute failed ?!";
            PLOG_ERROR << SKIF_Util_GetErrorAsWStr ( );

            proc->id       =  0;
            proc->store_id = -1;
          }

          // Remove any custom environment variables
          for (auto& var : env)
            SetEnvironmentVariable (var.first.c_str(), NULL);

          if (_registry._LoadedSteamOverlay)
            SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");
        }

        // Main launcher (uses ShellExecute in case elevation is required)
        else if (SKIF_Util_CreateProcess (launchConfig->getExecutableFullPath ( ),
                            cmdLine.c_str(),
                            dirPath.c_str(),
                              &env,
                              proc
          ))
        {
          if (pApp->store == app_record_s::Store::Steam)
            cmdLine += L" SKIF_SteamAppId=" + std::to_wstring (pApp->id);

          SKIF_Util_TrimTrailingSpacesW (cmdLine);

          SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal),
            launchConfig->getExecutableFullPath ( ),
            cmdLine,
            dirPath,
            launchConfig->getExecutableFullPath ( ),
            (! localInjection && usingSK));
        }
        
        else {
          PLOG_ERROR << "Process worker creation failed ?!";

          proc->id       =  0;
          proc->store_id = -1;
        }
      }
    }

    // Launch GOG Galaxy game (default for GOG games since 2024-01-05)
    else if (pApp->store == app_record_s::Store::GOG) // launchGalaxyGame
    {
      PLOG_VERBOSE << "Performing a GOG Galaxy launch...";

      extern std::wstring GOGGalaxy_Path;
      extern std::wstring GOGGalaxy_Folder;

      // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

      std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

      //SKIF_Util_OpenURI (GOGGalaxy_Path, SW_SHOWDEFAULT, L"OPEN", launchOptions.c_str());
      if (SKIF_Util_CreateProcess (GOGGalaxy_Path, launchOptions.c_str(), L""))
      {
        // Don't check the running state for at least 7.5 seconds
        pApp->_status.dwTimeDelayChecks = current_time + 7500;
        pApp->_status.running           = true;

        SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal + " (Galaxy)"), GOGGalaxy_Path, launchOptions, GOGGalaxy_Folder, launchConfig->getExecutableFullPath(), (! localInjection && usingSK));
      }
    }

    // Launch Steam game (regular)
    else if (pApp->store == app_record_s::Store::Steam)
    {
      PLOG_VERBOSE << "Performing a Steam launch...";

      bool launchDecision = true;

      // Check localconfig.vdf if user is attempting to launch without Special K 
      if (! usingSK && ! localInjection)
      {
        ;

        if (SKIF_Steam_GetLaunchOptions (pApp->id, SKIF_Steam_GetCurrentUser(), pApp).size() > 0)
        {
          if (SKIF_Util_ToLower (pApp->steam.local.launch_option).find("skif ") == 0)
          {
            launchDecision = false;

            std::string confirmCopy = pApp->steam.local.launch_option;

            // Escape any percent signs (%)
            for (auto pos  = confirmCopy.find ('%');          // Find the first occurence
                      pos != std::string::npos;                  // Validate we're still in the string
                      confirmCopy.insert      (pos, R"(%)"),  // Escape the character
                      pos  = confirmCopy.find ('%', pos + 2)) // Find the next occurence
            { }
                          
            SKIF_ImGui_InfoMessage ("Conflicting configuration",
                                    "Could not launch game due to conflicting launch options in Steam:\n"
                                    "\n"
                                  +  confirmCopy + "\n"
                                    "\n"
                                    "Please change the launch options in Steam before trying again.");

            PLOG_WARNING << "Steam game " << pApp->id << " (" << pApp->names.normal << ") was unable to launch due to a conflict with the launch option of Steam: " << confirmCopy;
          }
        }
      }

      if (launchDecision)
      {
        // steam://run/1289310/          <- Always launches using the developer-specified primary launch configuration
        // steam://launch/1289310/dialog <- For games with multiple launch configurations, opens the launch config dialog or uses the user-specified default launch configuration
        std::wstring launchOptions = SK_FormatStringW (LR"(steam://launch/%d/dialog)", pApp->id);
        if (SKIF_Util_OpenURI (launchOptions) != 0)
        {
          // Don't check the running state for at least 7.5 seconds
          pApp->_status.dwTimeDelayChecks = current_time + 7500;
          pApp->_status.running           = true;

          SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), L"", launchOptions, L"", launchConfig->getExecutableFullPath ( ), (! localInjection && usingSK));
        }
      }
    }

    else {
      PLOG_ERROR << "No applicable launch option was found?!";
    }

    // Fallback for minimizing SKIF when not using SK if configured as such
    if (_registry.bMinimizeOnGameLaunch && ! usingSK && SKIF_ImGui_hWnd != NULL)
      ShowWindowAsync (SKIF_ImGui_hWnd, SW_SHOWMINNOACTIVE);

    // Update the db.json file with the new Uses/Used values
    JsonDB_UpdateApp (pApp, true);

    // Ensure the sort order is updated as well
    SKIF_GamingCollection::SortApps (&g_apps);
    sort_changed = true;
  }
}


#pragma endregion


#pragma region UpdateGameCover

static bool
SaveGameCover (app_record_s* pApp, std::wstring_view path)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (path.empty())
    return false;

  std::error_code ec;
  const std::filesystem::path fsPath (path.data());
  std::wstring targetPath = L"";
  std::wstring ext        = SKIF_Util_ToLowerW (fsPath.extension().wstring());
  bool         isURL      = PathIsURL (path.data());
  std::wstring extTarget  = ext;
  bool isImage = false;

#if 0

  if (ext == L".jpeg")
    extTarget = L".jpg";

  if (ext == L".webp")
    extTarget = L".png";

  if (ext == L".bmp")
    extTarget = L".jpg";

  if (ext == L".psd")
    extTarget = L".png";

  bool isImage =
    (ext == L".jpg"  ||
     ext == L".jpeg" ||
   //ext == L".jxr"  ||
     ext == L".png"  ||
     ext == L".webp" ||
   //ext == L".tga"  ||
     ext == L".bmp"  ||
     ext == L".psd"  );
   //ext == L".gif"  ||
   //ext == L".tif"  );

  constexpr char* error_title =
    "Unsupported image format";
  constexpr char* error_label =
    "Please use one of these image formats:\n"
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
#endif

  if (! isURL)
  {
    // For local files, check if they do. in fact, exist and are local files
    if (! std::filesystem::is_regular_file (fsPath, ec))
      return false;

    // Identify file type by reading the file signature
    static const auto types =
    {
      FileSignature { L"image/jpeg",                L".jpg",  { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                              { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
      FileSignature { L"image/png",                 L".png",  { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
      FileSignature { L"image/webp",                L".webp", { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50 },     // 52 49 46 46 ?? ?? ?? ?? 57 45 42 50
                                                              { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF } }, // mask
      FileSignature { L"image/bmp",                 L".bmp",  { 0x42, 0x4D } },
      FileSignature { L"image/vnd.adobe.photoshop", L".psd",  { 0x38, 0x42, 0x50, 0x53 } }
    };

    static size_t
        maxLength  = 0;
    if (maxLength == 0)
    {
      for (auto& type : types)
        if (type.signature.size() > maxLength)
          maxLength = type.signature.size();
    }

    std::ifstream file(fsPath, std::ios::binary);

    if (file)
    {
      std::vector<char> buffer (maxLength);
      file.read (buffer.data(), maxLength);
      file.close();

      for (auto& type : types)
      {
        if (SKIF_Util_HasFileSignature (buffer, type))
        {
          PLOG_INFO << "Detected an " << type.mime_type << " image";

          isImage   = true;
          extTarget = SKIF_Util_ToLowerW (type.file_extension);

          // Shortcut to use a few (incorrect) file extensions for some types
          // TODO: Do away with this cheating, lol
          if (type.file_extension == L".webp")
            extTarget = L".png";

          if (type.file_extension == L".bmp")
            extTarget = L".png";

          if (type.file_extension == L".psd")
            extTarget = L".png";

          break;
        }
      }

      PLOG_ERROR_IF(! isImage) << "Unknown file type!";
    }

    else
      PLOG_ERROR << "Failed to open file!";
  }

  // Unsupported file format
  if (! isImage && ! isURL)
    return false;

  if (pApp->id == SKIF_STEAM_APPID)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
  else if (pApp->store == app_record_s::Store::Custom)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
  else if (pApp->store == app_record_s::Store::Epic)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->epic.name_app).c_str());
  else if (pApp->store == app_record_s::Store::GOG)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
  else if (pApp->store == app_record_s::Store::Xbox)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->xbox.package_name).c_str());
  else if (pApp->store == app_record_s::Store::Steam)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

  if (targetPath == L"")
  {
    PLOG_ERROR << "Could not resolve target path?!";
    return false;
  }

  struct thread_s {
    std::wstring source        = L"";
    std::wstring destination   = L"";
    std::wstring ext_target    = L"";
    std::wstring ext_original  = L"";
    bool         is_url        = false;
    uint32_t     appid         = 0;
    int          store         = 0;
  };
  
  thread_s* data = new thread_s;

  data->source        = path;
  data->destination   = targetPath;
  data->ext_target    = extTarget;
  data->ext_original  = ext;
  data->is_url        = isURL;
  data->appid         = pApp->id;
  data->store         = (int)pApp->store;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_UpdateCoverWorker");

    // Is this combo really appropriate for this thread?
    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    PLOG_DEBUG << "SKIF_UpdateCoverWorker thread started!";

    thread_s* _data = static_cast<thread_s*>(var);

    CoInitializeEx (nullptr, 0x0);

    PLOG_INFO  << "Updating game cover asynchronously...";

    std::error_code ec;
    // Create any missing directories
    if (! std::filesystem::exists (            _data->destination, ec))
          std::filesystem::create_directories (_data->destination, ec);

    _data->destination += L"cover";

    // We store the new file in .tmp first
    std::wstring tmpPath = _data->destination + L".tmp";

    bool success = false;
    bool backup  = false;

    if (_data->source == (_data->destination + _data->ext_original))
    {
      PLOG_WARNING << "Source image and destination image is the same!";
    }

    else
    {
      // Backup any existing copies
      backup =  MoveFileEx((_data->destination + L".jpg").c_str(), (_data->destination + L".jpg.old").c_str(), MOVEFILE_REPLACE_EXISTING)
             || MoveFileEx((_data->destination + L".png").c_str(), (_data->destination + L".png.old").c_str(), MOVEFILE_REPLACE_EXISTING);

      // This both downloads a new image from the internet as well as copies a local file to the destination
      // BMP files are downloaded to .tmp, while all others are downloaded to their intended path
      success = (_data->is_url) ? SKIF_Util_GetWebResource (_data->source,         tmpPath)
                                :                 CopyFile (_data->source.c_str(), tmpPath.c_str(), false);

      // If the file was copied successfully, we also need to ensure it's not marked as read-only
      if (success)
        SetFileAttributes (tmpPath.c_str(),
                GetFileAttributes (tmpPath.c_str()) & ~FILE_ATTRIBUTE_READONLY);

      else
      {
        PLOG_ERROR << "Could not save the source image to the destination path!";
        PLOG_ERROR << "Source:      " << _data->source;
        PLOG_ERROR << "Destination: " << tmpPath;
      }
    }

    DirectX::TexMetadata  meta = { };
    DirectX::ScratchImage  img = { };

    // Try and load the image
    if (success)
    {
      success = FastTextureLoading (tmpPath, meta, img);

      if (! success)
      {
        PLOG_ERROR << "The saved image could not be loaded! It is either corrupt or in an unsupported format.";
        PLOG_ERROR << "Source: " << _data->source;
      }
    }

    // Swap it in
    if (success)
    {
      // BMP images needs to be converted to PNG
      // Extremely basic and rudimentary check -- should really preferably read the metadata instead
      if (_data->ext_original == L".bmp")
      {
        PLOG_DEBUG << "Converting BMP image to PNG...";
        success =
          SUCCEEDED (
              DirectX::SaveToWICFile (
                img.GetImages(),
                img.GetImageCount(),
                DirectX::WIC_FLAGS_FORCE_SRGB,
                GetWICCodec (DirectX::WIC_CODEC_PNG),
                (_data->destination + _data->ext_target).c_str()
              )
          );
      }

      // Rename remaining types
      else
      {
        PLOG_DEBUG << "Swapping in the new file...";
        success =
          MoveFileEx((_data->destination + L".tmp").c_str(), (_data->destination + _data->ext_target).c_str(), MOVEFILE_REPLACE_EXISTING);
      }
    }

    // If everything checks out, remove any backups made
    if (success)
    {
      DeleteFile((_data->destination + L".jpg.old").c_str());
      DeleteFile((_data->destination + L".png.old").c_str());
    }

    // If something failed, restore the backups
    else if (backup)
    {
      PLOG_INFO << "Restoring the original cover...";
      MoveFile((_data->destination + L".jpg.old").c_str(), (_data->destination + L".jpg").c_str());
      MoveFile((_data->destination + L".png.old").c_str(), (_data->destination + L".png").c_str());
    }

    // Delete the temporary file after we are done with it
    DeleteFile(tmpPath.c_str());

    PLOG_ERROR_IF(! success) << "Failed to process the new cover image!";
      
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_REFRESHCOVER, _data->appid, _data->store); // Force a refresh when the cover has been swapped in

    PLOG_INFO  << "Finished updating game cover asynchronously...";
    
    // Free up the memory we allocated
    delete _data;

    PLOG_DEBUG << "SKIF_UpdateCoverWorker thread stopped!";

    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    return 0;
  }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated) // We don't care about how it goes so the handle is unneeded
    CloseHandle (hWorkerThread);
  else // Someting went wrong during thread creation, so free up the memory we allocated earlier
    delete data;

  return threadCreated;
}

#pragma endregion


#pragma region GameConfigMenu

// This is an unusual popup that needs to be called from two different ImGui popup contexts...
// ... usually this means any existing popup is closed, hence why we instead invoke this directly where relevant!
static void
DrawGameConfigMenu (app_record_s* pApp)
{
  if (pApp == nullptr)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
//static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
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
    if (SKIF_GlobalWatch.isSignaled (LR"(Global)") || runOnceDefaultPresets)
    {
      runOnceDefaultPresets = false;
      DefaultPresets        = _FindPresets (DefaultPresetsFolder, L"default_*.ini");
    }

    if (SKIF_CustomWatch.isSignaled (LR"(Global\Custom)") || runOnceCustomPresets)
    {
      runOnceCustomPresets = false;
      CustomPresets        = _FindPresets (CustomPresetsFolder, L"*.ini");
    }
          
    if (! DefaultPresets.empty() || ! CustomPresets.empty())
    {
      if (ImGui::BeginMenu("Apply Preset"))
      {
        // Default Presets
        if (! DefaultPresets.empty())
        {
          for (auto& preset : DefaultPresets)
          {
            if (ImGui::Selectable (preset.Name.c_str()))
            {
              CopyFile (preset.Path.c_str(), pApp->specialk.injection.config.full_path.c_str(), FALSE);
              PLOG_VERBOSE << "Copying " << preset.Path << " over to " << pApp->specialk.injection.config.full_path << ", overwriting any existing file in the process.";
            }

            SKIF_ImGui_SetMouseCursorHand ();
          }

          if (! CustomPresets.empty())
            ImGui::Separator ( );
        }

        // Custom Presets
        if (! CustomPresets.empty())
        {
          for (auto& preset : CustomPresets)
          {
            if (ImGui::Selectable (preset.Name.c_str()))
            {
              CopyFile (preset.Path.c_str(), pApp->specialk.injection.config.full_path.c_str(), FALSE);
              PLOG_VERBOSE << "Copying " << preset.Path << " over to " << pApp->specialk.injection.config.full_path << ", overwriting any existing file in the process.";
            }

            SKIF_ImGui_SetMouseCursorHand ();
          }
        }

        ImGui::EndMenu ( );
      }

      ImGui::Separator ( );
    }

    if (ImGui::Selectable ("Apply Compatibility Config"))
    {
      std::wofstream config_file(pApp->specialk.injection.config.full_path.c_str());

      if (config_file.is_open())
      {
        // Static const as this profile never changes
        static const std::wstring out_text =
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

        config_file.write(out_text.c_str(),
          out_text.length());

        config_file.close();
      }
    }

    SKIF_ImGui_SetMouseCursorHand ();

    SKIF_ImGui_SetHoverTip ("Known as the \"sledgehammer\" config within the community as it disables\n"
                            "various features of Special K in an attempt to improve compatibility.");

    // Don't truncate/reset any existing profile file -- just delete it outright
    if (ImGui::Selectable ("Reset"))
      DeleteFile (pApp->specialk.injection.config.full_path.c_str());

    SKIF_ImGui_SetMouseCursorHand ();

    ImGui::PopStyleColor ( );

    ImGui::EndPopup ( );
  }
}

#pragma endregion


#pragma region DrawGameContextMenu

static void
DrawGameContextMenu (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  // TODO: Many of the below options needs to be adjusted to indicate if the primary launch option
  //         is even available (or if "without Special K" is the only option available)

  // Do not check games that are being updated (aka installed)
  //   nor expose the option for shell execute based games (e.g. Link2EA, which requires the Steam client running)
  bool SteamShortcutPossible = false;

  app_record_s::launch_config_s*
    firstLaunchConfig = (pApp != nullptr && ! pApp->launch_configs.empty()) ? &pApp->launch_configs.begin()->second : nullptr;

  if (firstLaunchConfig != nullptr && pApp->store == app_record_s::Store::Steam && ! pApp->_status.updating)
    SteamShortcutPossible = firstLaunchConfig->isExecutableFullPathValid ( );
    
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
  if (SteamShortcutPossible || pApp->store != app_record_s::Store::Steam)
  {
    if (pApp->ui.numSecondaryLaunchConfigs > 0 || (pApp->store == app_record_s::Store::Steam || pApp->store == app_record_s::Store::GOG))
      ImGui::Separator ( );

    // If there is only one valid launch config (Steam, GOG, Xbox games only)
    if (pApp->ui.numSecondaryLaunchConfigs == 0   &&
       (pApp->store == app_record_s::Store::Steam ||
        pApp->store == app_record_s::Store::GOG   ||
        pApp->store == app_record_s::Store::Xbox))
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

      if (firstLaunchConfig != nullptr && ! pApp->steam.local.launch_option.empty())
      {
        hoverText = GetSteamCommandLaunchOptions (pApp, firstLaunchConfig);
      }

      else {
        if (firstLaunchConfig != nullptr && ! firstLaunchConfig->getLaunchOptionsUTF8().empty())
          hoverText += (" " + firstLaunchConfig->getLaunchOptionsUTF8());

        if (pApp->store == app_record_s::Store::Steam)
          hoverText += (" " + pApp->steam.local.launch_option);
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
              ! pApp->steam.local.launch_option.empty())
          {
            hoverText = GetSteamCommandLaunchOptions (pApp, &_launch_cfg.second);
          }

          else
          {
            if (! _launch.getLaunchOptionsUTF8().empty())
              hoverText += (" " + _launch.getLaunchOptionsUTF8());

            // Also add Steam defined launch options, but only if not using custom launch configs
            if (pApp->store == app_record_s::Store::Steam && ! pApp->steam.local.launch_option.empty() &&
               ! _launch_cfg.second.custom_skif && ! _launch_cfg.second.custom_user)
              hoverText += (" " + pApp->steam.local.launch_option);
          }
          
          if (! playDisabled && ! blacklisted)
            SKIF_ImGui_SetMouseCursorHand ( );
          
          SKIF_ImGui_SetHoverText       (hoverText.c_str());

          if (blacklisted)
            SKIF_ImGui_SetHoverTip      ("Special K has been disabled for this launch configuration.");
        }

        ImGui::EndMenu ();
      }

      if (pApp->store == app_record_s::Store::Steam || pApp->store == app_record_s::Store::GOG)
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
    for (auto& monitored_app : iPlayCache)
      if (monitored_app.id == pApp->id && monitored_app.store_id == (int)pApp->store)
        mon_app = &monitored_app;

    if (mon_app != nullptr)
    {
      HANDLE hProcess = mon_app->hProcess.load();

      ImGui::Separator ( );

      bool bInvalid = (hProcess == INVALID_HANDLE_VALUE);

      if (bInvalid)
        ImGui::BeginDisabled ( );
      
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
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      HANDLE h = CreateFile (pApp->specialk.injection.config.full_path.c_str(),
                      GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                            CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,
                                NULL );

      // We need to close the handle as well, as otherwise apps will think the file
      //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
      if (h != INVALID_HANDLE_VALUE)
        CloseHandle (h);

      SKIF_Util_OpenURI (pApp->specialk.injection.config.full_path.c_str(), SW_SHOWNORMAL, NULL);
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
    bool desktopShortcutPossible =
          (SteamShortcutPossible                     ||
          pApp->store == app_record_s::Store::Custom ||
          pApp->store == app_record_s::Store::GOG);

    if (SKIF_ImGui_MenuItemEx2 ("Properties", ICON_FA_GEAR, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info)))
      ModifyGamePopup = PopupState_Open;

    if (51 > pApp->skif.pinned && 5 > numPinnedOnTop)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Favorite (pinned; max 5)", ICON_FA_STAR, ImColor(255, 207, 72)))
      {
        pApp->skif.pinned =  51;
        numPinnedOnTop++;

        JsonDB_UpdateApp  ( pApp, true);

        SKIF_GamingCollection::SortApps (&g_apps);
        sort_changed = true;
      }
    }

    constexpr char* labelPin   =   "Favorite###Manage-Favorite";
    constexpr char* labelUnpin = "Unfavorite###Manage-Favorite";

    bool isFavorite = (pApp->skif.pinned > 0 || (pApp->skif.pinned == -1 && pApp->steam.shared.favorite == 1));
    if (SKIF_ImGui_MenuItemEx2 ((isFavorite ? labelUnpin : labelPin), (isFavorite ? ICON_FA_HEART_CRACK : ICON_FA_HEART), ImColor(245, 66, 66, 255)))
    {
      if (pApp->skif.pinned > 50)
        numPinnedOnTop--;

      pApp->skif.pinned = isFavorite ? 0 : 1;

      JsonDB_UpdateApp   (pApp, true);

      SKIF_GamingCollection::SortApps (&g_apps);
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
        JsonDB_UpdateApp (pApp, true);

        SKIF_GamingCollection::SortApps (&g_apps);
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
        JsonDB_UpdateApp (pApp, true);

        SKIF_GamingCollection::SortApps (&g_apps);
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

        JsonDB_UpdateApp (pApp, true);

        SKIF_GamingCollection::SortApps (&g_apps);
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
        std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", pApp->launch_configs[0].getExecutableFullPath().c_str(), pApp->launch_configs[0].getLaunchOptions().c_str());

        SKIF_Util_TrimTrailingSpacesW (linkArgs);

        if (pApp->store == app_record_s::Store::Steam)
          linkArgs += L" SKIF_SteamAppId=" + std::to_wstring (pApp->id);

        constexpr char* info_title         = "Create desktop shortcut";
        constexpr char* info_label_success = "A desktop shortcut has been created.";
        constexpr char* info_label_failure = "Failed to create a desktop shortcut!";
        
        if (SKIF_Util_CreateShortcut (
            linkPath.c_str(),
            _path_cache.skif_executable,
            linkArgs.c_str(),
            pApp->launch_configs[0].getWorkOrExeDirectory().c_str(),
            SK_UTF8ToWideChar(name).c_str(),
            pApp->launch_configs[0].getExecutableFullPath().c_str()
            )
          )
          SKIF_ImGui_InfoMessage (info_title, info_label_success);
        else
          SKIF_ImGui_InfoMessage (info_title, info_label_failure);
      }
    }

    constexpr char* labelHide   =   "Hide";
    constexpr char* labelUnhide = "Unhide";

    bool bUnhide = (pApp->skif.hidden == 1 || (pApp->skif.hidden == -1 && pApp->steam.shared.hidden == 1));
    
    if (SKIF_ImGui_MenuItemEx2 ((bUnhide ? labelUnhide : labelHide), (bUnhide ? ICON_FA_EYE : ICON_FA_EYE_SLASH), ImColor(200, 200, 200, 255)))
    {
      pApp->skif.hidden = bUnhide ? 0 : 1;

      JsonDB_UpdateApp  ( pApp, true);

      RepopulateGames = true;
    }

    if (pApp->store == app_record_s::Store::Custom)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Remove", ICON_FA_TRASH, ImColor(200, 200, 200, 255)))
        RemoveGamePopup = PopupState_Open;
    }

    ImGui::EndMenu ( );
  }
  
  if (pApp->store == app_record_s::Store::Steam &&
      ImGui::BeginMenuEx ("Steam", ICON_FA_STEAM))
  {
    if (SKIF_ImGui_MenuItemEx2 ("Details", ICON_FA_BARS_STAGGERED, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      SKIF_Util_OpenURI ((L"steam://nav/games/details/" + std::to_wstring (pApp->id)).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://nav/games/details/%lu", pApp->id));
    
    if (SKIF_ImGui_MenuItemEx2 ("Steam Input", ICON_FA_GAMEPAD, (_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255)))
      SKIF_Util_OpenURI ((L"steam://controllerconfig/" + std::to_wstring (pApp->id)).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverTip        ("A controller must be connected.");
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://controllerconfig/%lu", pApp->id));
    
    if (SKIF_ImGui_MenuItemEx2 ("Game properties", ICON_FA_WRENCH, ImColor(200, 200, 200, 255)))
      SKIF_Util_OpenURI ((L"steam://gameproperties/" + std::to_wstring (pApp->id)).c_str());

    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("steam://gameproperties/%lu", pApp->id));

    ImGui::EndMenu     ( );
  }
  
  if (SKIF_ImGui_BeginMenuEx2 ("Websites", ICON_FA_SHARE, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
  {
    if (pApp->store == app_record_s::Store::GOG)
    {
      if (SKIF_ImGui_MenuItemEx2 ("GOG Database", ICON_FA_DATABASE, ImColor(155, 89, 182, 255)))
        SKIF_Util_OpenURI ((L"https://www.gogdb.org/product/" + std::to_wstring (pApp->id)).c_str());

      SKIF_ImGui_SetMouseCursorHand ( );
      SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("https://www.gogdb.org/product/%lu", pApp->id));
    }

    else if (pApp->store == app_record_s::Store::Steam)
    {
      if (SKIF_ImGui_MenuItemEx2 ("Steam Community", ICON_FA_STEAM_SYMBOL, ((_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255))))
        SKIF_Util_OpenURI ((L"https://steamcommunity.com/app/" + std::to_wstring (pApp->id)).c_str());

      SKIF_ImGui_SetMouseCursorHand ( );
      SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("https://steamcommunity.com/app/%lu", pApp->id));

      if (SKIF_ImGui_MenuItemEx2 ("SteamDB", ICON_FA_DATABASE, ImColor(101, 192, 244, 255)))
        SKIF_Util_OpenURI ((L"https://steamdb.info/app/" + std::to_wstring (pApp->id)).c_str());

      SKIF_ImGui_SetMouseCursorHand ( );
      SKIF_ImGui_SetHoverText       (SKIF_Util_FormatStringRaw ("https://steamdb.info/app/%lu", pApp->id));
    }

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

        if (pApp->store == app_record_s::Store::Steam)
        {
          ImGui::Separator ( );

          ImGui::PushID       ("#Steam");
          ImGui::TextDisabled ("Steam Data");
          if (ImGui::MenuItem ("Branch",                  pApp->steam.branch.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->steam.branch));
          if (ImGui::MenuItem ("Launch Option",           pApp->steam.local.launch_option.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->steam.local.launch_option));
          if (ImGui::MenuItem ("Manifest Path", SK_WideCharToUTF8(pApp->steam.manifest_path).c_str()))
            SKIF_Util_SetClipboardData (                          pApp->steam.manifest_path);
          if (ImGui::MenuItem ("CPU Architecture", std::to_string ((int)pApp->common_config.cpu_type).c_str()))
            SKIF_Util_SetClipboardData (           std::to_wstring((int)pApp->common_config.cpu_type));
          if (ImGui::MenuItem ("Favorited",        std::to_string (pApp->steam.shared.favorite).c_str()))
            SKIF_Util_SetClipboardData   (         std::to_wstring(pApp->steam.shared.favorite));
          if (ImGui::MenuItem ("Hidden",           std::to_string (pApp->steam.shared.hidden).c_str()))
            SKIF_Util_SetClipboardData   (         std::to_wstring(pApp->steam.shared.hidden));
          ImGui::PopID        ( );
        }

        else if (pApp->store == app_record_s::Store::Xbox)
        {
          ImGui::Separator ( );

          ImGui::PushID       ("#Xbox");
          ImGui::TextDisabled ("Xbox Data");
          if (ImGui::MenuItem ("Package",                 pApp->xbox.package_name.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->xbox.package_name));
          if (ImGui::MenuItem ("Full Name",               pApp->xbox.package_name_full.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->xbox.package_name_full));
          if (ImGui::MenuItem ("Family Name",             pApp->xbox.package_name_family.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->xbox.package_name_family));
          if (ImGui::MenuItem ("Store ID",                pApp->xbox.store_id.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->xbox.store_id));
          if (ImGui::MenuItem ("App Directory",    SK_WideCharToUTF8(pApp->xbox.directory_app).c_str()))
            SKIF_Util_SetClipboardData (                             pApp->xbox.directory_app);
          if (ImGui::MenuItem ("PF Directory",     SK_WideCharToUTF8(pApp->xbox.directory_program_files).c_str()))
            SKIF_Util_SetClipboardData (                             pApp->xbox.directory_program_files);
          ImGui::PopID        ( );
        }

        else if (pApp->store == app_record_s::Store::Epic)
        {
          ImGui::Separator ( );

          ImGui::PushID       ("#Epic");
          ImGui::TextDisabled ("Epic Data");
          if (ImGui::MenuItem ("App Name",                pApp->epic.name_app.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->epic.name_app));
          if (ImGui::MenuItem ("Display Name",            pApp->epic.name_display.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->epic.name_display));
          if (ImGui::MenuItem ("Catalog Namespace",       pApp->epic.catalog_namespace.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->epic.catalog_namespace));
          if (ImGui::MenuItem ("Catalog Item ID",         pApp->epic.catalog_item_id.c_str()))
            SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(pApp->epic.catalog_item_id));
          ImGui::PopID        ( );
        }

        ImGui::EndMenu ();
      }

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

              if (pApp->store == app_record_s::Store::Steam && launch.id_steam != -1)
              {
                if (ImGui::MenuItem ("ID (Steam)", std::to_string(launch.id_steam).c_str()))
                  SKIF_Util_SetClipboardData (std::to_wstring(launch.id_steam));
              }

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

              if (pApp->store == app_record_s::Store::Xbox)
              {
                ImGui::Separator ( );
          
                ImGui::PushID       ("#Xbox");
                ImGui::TextDisabled ("Xbox Data");
                if (ImGui::MenuItem ("Application ID",          launch.Xbox_ApplicationId.c_str()))
                  SKIF_Util_SetClipboardData (SK_UTF8ToWideChar(launch.Xbox_ApplicationId));
                ImGui::PopID        ( );
              }

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }

        ImGui::Separator ( );
      }

      
      if (pApp->store == app_record_s::Store::Xbox)
      {
        if (pApp->ui.numSecondaryLaunchConfigs == 0)
        {
          if (SKIF_ImGui_MenuItemEx2 ("Open Terminal###GameContextMenu_TerminalMenu", ICON_FA_TERMINAL))
          {
            SKIF_Util_CreateProcess (
              L"",
              SKIF_Xbox_GetCustomLaunchCommandW (
                pApp->launch_configs.begin()->second.id,
                pApp->xbox.package_name_family,
                "cmd.exe"
              ),
              pApp->install_dir
            );
          }

          SKIF_ImGui_SetMouseCursorHand ( );
        }

        else if (pApp->ui.numSecondaryLaunchConfigs > 0)
        {
          if (SKIF_ImGui_BeginMenuEx2 ("Open Terminal###GameContextMenu_TerminalMenu", ICON_FA_TERMINAL))
          {
            for (auto& _launch_cfg : pApp->launch_configs)
            {
              if (! _launch_cfg.second.valid ||
                    _launch_cfg.second.duplicate_exe_args)
                continue;

              // Filter out launch configs requiring not owned DLCs
              if (! _launch_cfg.second.owns_dlc)
                continue;

              auto& _launch = _launch_cfg.second;

              char        szButtonLabel [256] = { };

              sprintf_s ( szButtonLabel, 255,
                            "%s###GameContextMenu_TerminalMenu-%d",
                              _launch.getDescriptionUTF8().empty ()
                                ? _launch.getExecutableFileNameUTF8().c_str ()
                                : _launch.getDescriptionUTF8().c_str (),
                              _launch.id);

              ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

              if (ImGui::Selectable (szButtonLabel))
              {
                SKIF_Util_CreateProcess (
                  L"",
                  SKIF_Xbox_GetCustomLaunchCommandW (
                    _launch.id,
                    pApp->xbox.package_name_family,
                    "cmd.exe"
                  ),
                  pApp->install_dir
                );
              }

              ImGui::PopStyleColor  ( );

              SKIF_ImGui_SetMouseCursorHand ( );
            }

            ImGui::EndMenu ();
          }
        }
      }

      SKIF_ImGui_SetHoverTip ("This invokes a terminal in the context of the desktop package.");

      // Epic and Xbox platforms use fake app IDs hashed from their unique text-based platform identifier
      if (SKIF_ImGui_MenuItemEx2 ("Copy ID", ICON_FA_FINGERPRINT))
      {
        switch (pApp->store)
        {
        case app_record_s::Store::Epic:
          SKIF_Util_SetClipboardData (SK_UTF8ToWideChar (pApp->epic.name_app));
          break;
        case app_record_s::Store::Xbox:
          SKIF_Util_SetClipboardData (SK_UTF8ToWideChar (pApp->xbox.package_name));
          break;
        default:
          SKIF_Util_SetClipboardData (std::to_wstring   (pApp->id));
        }
      }

      SKIF_ImGui_SetHoverText (
        (pApp->store == app_record_s::Store::Epic) ? pApp->epic.name_app     :
        (pApp->store == app_record_s::Store::Xbox) ? pApp->xbox.package_name :
                                     std::to_string (pApp->id)
      );

      ImGui::EndMenu ();
    }
  }
}

static void
DrawSpecialKContextMenu (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  
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
    SKIF_Util_OpenURI (L"https://discord.gg/specialk");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://discord.gg/specialk");

  if (SKIF_ImGui_MenuItemEx2 ("Forum", ICON_FA_DISCOURSE, (_registry._StyleLightMode) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImVec4(ImColor(247, 241, 169))))
    SKIF_Util_OpenURI (L"https://discourse.differentk.fyi/");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://discourse.differentk.fyi/");

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

    JsonDB_UpdateApp  ( pApp, true);

    SKIF_GamingCollection::SortApps (&g_apps);
    sort_changed = true;
  }
}

#pragma endregion


#pragma region PrintInjectionSummary

static void
GetInjectionSummary (app_record_s* pApp)
{
  if (pApp == nullptr)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

#pragma region _IsLocalDLLFileOutdated

  auto _IsLocalDLLFileOutdated = [&](void) -> bool
  {
    bool ret = false;

    if ((pApp->store != app_record_s::Store::Steam) ||
        (pApp->store == app_record_s::Store::Steam  && // Exclude the check for games with known older versions
         pApp->id    != 405900                      && // Disgaea PC
         pApp->id    != 359870                      && // FFX/X-2 HD Remaster
       //pApp->id    != 578330                      && // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
         pApp->id    != 429660                      && // Tales of Berseria
         pApp->id    != 372360                      && // Tales of Symphonia
       //pApp->id    != 738540                      && // Tales of Vesperia DE // Do not exclude from the updater as mainline SK further improves the game
         pApp->id    != 351970                         // Tales of Zestiria
        ))
    {
      if (SKIF_Util_CompareVersionStrings (_inject.SKVer32, pApp->specialk.injection.dll.version) > 0)
      {
        ret = true;
      }
    }
        
    return ret;
  };

#pragma endregion

#pragma region _UpdateLocalDLLFile

  auto _UpdateLocalDLLFile = [&](void) -> void
  {
    int iBinaryType = SKIF_Util_GetBinaryType (pApp->specialk.injection.dll.full_path.c_str());
    if (iBinaryType > 0)
    {
      std::wstring  wsPathToGlobalDLL = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, (iBinaryType == 2) ? L"SpecialK64.dll" : L"SpecialK32.dll");
      if (CopyFile (wsPathToGlobalDLL.c_str(), pApp->specialk.injection.dll.full_path.c_str(), FALSE))
      {
        PLOG_INFO << "Successfully updated " << pApp->specialk.injection.dll.full_path << " from v " << pApp->specialk.injection.dll.version << " to v " << _inject.SKVer32;
      }

      else {
        PLOG_ERROR << "Failed to copy " << wsPathToGlobalDLL << " to " << pApp->specialk.injection.dll.full_path;
        PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
      }
    }

    else {
      PLOG_ERROR << "Failed to retrieve binary type from " << pApp->specialk.injection.dll.full_path << " -- returned: " << iBinaryType;
      PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    }
  };

#pragma endregion

  static constexpr float
        num_lines = 4.0f;
  auto line_ht   =
    ImGui::GetTextLineHeightWithSpacing ();

  auto frame_id =
    ImGui::GetID ("###Injection_Summary_Frame");

  SKIF_ImGui_BeginChildFrame ( frame_id,
                                  ImVec2 ( ImFloor (lib_column2_width - ImGui::GetStyle ().FrameBorderSize * 2.0f),
                                                                            num_lines * line_ht ),
                                    ImGuiChildFlags_AlwaysAutoResize  |
                                    ImGuiChildFlags_AutoResizeY,
                                    ImGuiWindowFlags_NavFlattened      |
                                    ImGuiWindowFlags_NoScrollbar       |
                                    ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoBackground
                              );

  ImGui::BeginGroup       ();

  // Column 1
  ImGui::BeginGroup       ();
  //ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  //ImGui::NewLine          ();
  //ImGui::TextUnformatted  ("Injection:");
  ImGui::TextUnformatted  ("Special K");
  ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  ImGui::TextUnformatted  ("Profile folder:");
  ImGui::TextUnformatted  ("Config file:");
  ImGui::TextUnformatted  ("Platform:");
  ImGui::PopStyleColor    ();
  ImGui::ItemSize         (ImVec2 (105.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 1 should have min-width 105px (scaled with the DPI)
  ImGui::EndGroup         ();

  ImGui::SameLine         ();

  // Column 2
  ImGui::BeginGroup       ();

  // Injection
  if (! pApp->loading && ! pApp->specialk.injection.dll.shorthand.empty ())
  {
    //ImGui::TextUnformatted  (cache.dll.shorthand.c_str  ());
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowOverlap;

    if (pApp->specialk.injection.injection.type == InjectionType::Global)
      flags |= ImGuiSelectableFlags_Disabled;

    bool openLocalMenu = false;

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    if (ImGui::Selectable (pApp->ui.label_version.c_str(), false, flags))
      openLocalMenu = true;
    ImGui::PopStyleColor();

    if (pApp->specialk.injection.injection.type == InjectionType::Local)
    {
      SKIF_ImGui_SetMouseCursorHand ( );

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openLocalMenu = true;
    }

    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.dll.full_path_utf8.c_str());
        
    if (openLocalMenu && ! ImGui::IsPopupOpen ("LocalDLLMenu"))
      ImGui::OpenPopup    ("LocalDLLMenu");

    if (ImGui::BeginPopup ("LocalDLLMenu", ImGuiWindowFlags_NoMove))
    {
      if (_IsLocalDLLFileOutdated( ))
      {
        if (ImGui::Selectable (("Update to v " + _inject.SKVer32_utf8).c_str( )))
          _UpdateLocalDLLFile ( );

        ImGui::Separator ( );
      }

      if (ImGui::Selectable ("Uninstall"))
      {
        if (DeleteFile (pApp->specialk.injection.dll.full_path.c_str()))
        {
          PLOG_INFO << "Successfully uninstalled local DLL v " << pApp->specialk.injection.dll.version << " from " << pApp->specialk.injection.dll.full_path;

          HKEY     hKey;
          LSTATUS ls = RegOpenKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Local)", &hKey);
      
          if (ERROR_SUCCESS == ls)
          {
            ls = RegDeleteValueW (hKey, pApp->specialk.injection.dll.full_path.c_str());
            PLOG_ERROR_IF (ls != ERROR_SUCCESS) << "Failed to delete registry value: " << SKIF_Util_GetErrorAsWStr (ls);
            RegCloseKey (hKey);
          }
        }
      }

      ImGui::EndPopup ( );
    }
  }

  else
  {
    // Most will use global injection, so default to this in situations where the data is actually being loaded.
    if (! pApp->specialk.injection.dll.version_utf8.empty())
      ImGui::Text    ("v %s", pApp->specialk.injection.dll.version_utf8.c_str());
    else
      ImGui::NewLine ( );
  }

  // Config Root
  // Config File
  if (! pApp->loading && ! pApp->specialk.injection.config.shorthand.empty ())
  {
    // Config Root
    if (ImGui::Selectable         (pApp->specialk.injection.config.type_utf8.c_str (), false, ImGuiSelectableFlags_None, ImVec2 (95.f * SKIF_ImGui_GlobalDPIScale, 0.f)))
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      SKIF_Util_ExplorePath       (pApp->specialk.injection.config.root_dir);
    }
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.config.root_dir_utf8.c_str ());

    // Config File
    if (ImGui::Selectable         (pApp->specialk.injection.config.shorthand_utf8.c_str (), false, ImGuiSelectableFlags_None, ImVec2 (95.f * SKIF_ImGui_GlobalDPIScale, 0.f))) // 240
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (pApp->specialk.injection.config.root_dir, ec))
            std::filesystem::create_directories (pApp->specialk.injection.config.root_dir, ec);

      HANDLE h = CreateFile (pApp->specialk.injection.config.full_path.c_str(),
                      GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                            CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,
                                NULL );

      // We need to close the handle as well, as otherwise apps will think the file
      //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
      if (h != INVALID_HANDLE_VALUE)
        CloseHandle (h);

      SKIF_Util_OpenURI (pApp->specialk.injection.config.full_path.c_str(), SW_SHOWNORMAL, NULL);
    }

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (pApp->specialk.injection.config.full_path_utf8.c_str ());

    DrawGameConfigMenu (pApp);
  }

  else if (! pApp->loading)
  {
    ImGui::TextUnformatted (pApp->specialk.injection.config.type_utf8.c_str ());
    ImGui::TextUnformatted ("N/A");
  }

  else {
    // Most will use global injection, so default to this in situations where the data is actually being loaded.
    ImGui::TextUnformatted (pApp->specialk.injection.config.type_utf8.c_str ());
    ImGui::TextUnformatted (pApp->specialk.injection.config.shorthand_utf8.c_str ());
    //ImGui::TextUnformatted ("Centralized");
    //ImGui::TextUnformatted ("SpecialK.ini");
    //ImGui::NewLine ( );
    //ImGui::NewLine ( );
  }

  // Platform
  ImGui::TextUnformatted  (pApp->store_utf8.c_str());

  ImGui::ItemSize         (ImVec2 (95.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 2 should have min-width 95px (scaled with the DPI) (a bit smaller to allow for some right-hand padding for "Waiting for game...")
  ImGui::EndGroup         ( );
  ImGui::SameLine         ( );

  // Column 3
  ImGui::BeginGroup       ( );

  static bool quickServiceHover = false;

  // Service quick toogle / Waiting for game...
  // Also show this while a game is loading since it is more likely to be using global than local! (and it's safe to show nowadays!)
  if (/* ! pApp->loading && */ pApp->specialk.injection.injection.type != InjectionType::Local && ! _inject.isPending())
  {
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, (quickServiceHover) ? _inject.ui_game_summary.color_hover.Value
                                                             : _inject.ui_game_summary.color.Value);

    if (ImGui::Selectable (_inject.ui_game_summary.text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns, ImVec2 (360.0f * SKIF_ImGui_GlobalDPIScale, 0.0f)))
    {
      // Only allow the toggle to work to disable the service while a game is loading
      if (pApp->loading)
      {
        if (_inject.bCurrentState)
          _inject._StartStopInject (true);
      }

      // For non-loading games, allow the full functionality
      else
        _inject._StartStopInject (_inject.bCurrentState, _registry.bStopOnInjection, pApp->launch_configs[0].isElevated ( ), pApp->skif.auto_stop);
    }

    ImGui::PopStyleColor (3);

    quickServiceHover = ImGui::IsItemHovered ();

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverTip        (_inject.ui_game_summary.hover_tip.c_str ());

    if ( ! ImGui::IsPopupOpen ("ServiceMenu") &&
           ImGui::IsItemClicked (ImGuiMouseButton_Right))
      ServiceMenu = PopupState_Open;
  }

  else
    ImGui::NewLine ( );

  if (! pApp->loading && pApp->specialk.injection.injection.type == InjectionType::Local)
  {
    if (_IsLocalDLLFileOutdated ( ))
    {
      ImGui::SameLine        ( );

      ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
      if (ImGui::SmallButton (ICON_FA_ARROW_UP))
      {
        _UpdateLocalDLLFile ( );
      }
      ImGui::PopStyleColor ( );

      SKIF_ImGui_SetHoverTip (("The local DLL file is outdated.\n"
                                "Click to update it to v " + _inject.SKVer32_utf8 + "."));
    }
  }

  ImGui::EndGroup         ( );

  // End of columns
  ImGui::EndGroup         ( );

  ImGui::EndChild         ( );

  ImGui::Separator ();

  auto frame_id2 =
    ImGui::GetID ("###Injection_Play_Button_Frame");

  /*
  ImGui::PushStyleVar (
    ImGuiStyleVar_FramePadding,
      ImVec2 (((_registry.bUIBorders) ? 104.0f : 105.0f) * SKIF_ImGui_GlobalDPIScale,
                40.0f * SKIF_ImGui_GlobalDPIScale)
  );
  */

  SKIF_ImGui_BeginChildFrame (
    frame_id2, ImVec2 (  0.0f,
                         0.0f), //110.f * SKIF_ImGui_GlobalDPIScale ),
      ImGuiChildFlags_None,
      ImGuiWindowFlags_NavFlattened      |
      ImGuiWindowFlags_NoScrollbar       |
      ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBackground
  );

  //ImGui::PopStyleVar ();

  std::string      buttonLabel   = ICON_FA_GAMEPAD "  Play";
  bool             buttonInstall = false,
                   buttonPending = false;

  if (! pApp->loading &&
        pApp->store == app_record_s::Store::Steam && // Expose installer for those games with better specific mods
     (//pApp->id == 405900       || // Disgaea PC
        pApp->id == 359870       || // FFX/X-2 HD Remaster
      //pApp->id == 578330       || // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
        pApp->id == 429660       || // Tales of Berseria
        pApp->id == 372360       || // Tales of Symphonia
        pApp->id == 738540     //|| // Tales of Vesperia DE
      //pApp->id == 351970          // Tales of Zestiria
      ) && pApp->specialk.injection.injection.type != InjectionType::Local)
  {
    buttonLabel   = ICON_FA_DOWNLOAD "  Install Mod";
    buttonInstall = true;

    uint32_t _modAppId = modAppId.load();

    if (_modAppId == pApp->id)
    {
      if (modInstalling.load())
      {
        buttonLabel = "Installing...";
        buttonPending = true;
      }

      else if (modDownloading.load())
      {
        buttonLabel = "Downloading...";
        buttonPending = true;
      }
    }

    else if (_modAppId > 0)
    {
      buttonLabel   = "Pending...";
      buttonPending = true;
    }
  }

  static bool  loading      = false;
  static DWORD loadingTimer = 0;

  if (pApp->loading)
  {
    if (loadingTimer == 0)
      loadingTimer    = SKIF_Util_timeGetTime ( ) + 32;

    // 32 ms delay before we indicate the game is loading in the background
    if (! loading && loadingTimer < SKIF_Util_timeGetTime ( ))
      loading         = true;
  }

  // Reset once we are no longer loading
  else {
    loading      = false;
    loadingTimer = 0;
  }

  ImVec2 posButton =
     ImGui::GetCursorPos ( );

  if (loading || pApp->_status.running || pApp->_status.updating || buttonPending)
  {
    buttonLabel =               (loading) ? "Loading..." :
                  (pApp->_status.running) ? "Running..." :
                                            "Updating...";
    ImGui::PushStyleColor (ImGuiCol_Button, ImGui::GetStyleColorVec4 (ImGuiCol_Button) * ImVec4 (0.75f, 0.75f, 0.75f, 1.0f));
    ImGui::BeginDisabled  ( );
  }

  // Horizontal center-align
  ImGui::SetCursorPosX (
     ImGui::GetCursorPosX ( ) +
    (ImGui::GetContentRegionAvail ( ).x - (150.0f * SKIF_ImGui_GlobalDPIScale) +
     ImGui::GetStyle ( ).FramePadding.x) / 2
  );

  // Vertical center-align
  ImGui::SetCursorPosY (
     ImGui::GetCursorPosY ( )           +
    (ImGui::GetContentRegionAvail ( ).y -
     ImGui::GetFrameHeightWithSpacing() -
     (50.0f * SKIF_ImGui_GlobalDPIScale)
    ) / 2
  );

  if (_registry.bUIBorders)
    ImGui::SetCursorPosY (
      ImGui::GetCursorPosY () -
      ImGui::GetStyle      ().FramePadding.y);

  if (ImGui::ButtonEx (
              buttonLabel.c_str (),
                  ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                            50.0f * SKIF_ImGui_GlobalDPIScale )))
  {
    if (pApp->loading)
    {
      // Do nothing if we're loading
      loading    = true;
    }

    else if (! buttonInstall)
      launchGame = true;

    else
    {
      static uint32_t appid;
      appid = pApp->id;

      // We're going to asynchronously download the mod installer on this thread
      if (! modDownloading.load())
      {
        HANDLE hWorkerThread = (HANDLE)
        _beginthreadex (nullptr, 0x0, [](void*) -> unsigned
        {
          modDownloading.store (true);

          SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibGameModWorker");

          CoInitializeEx (nullptr, 0x0);

          SKIF_Util_SetThreadPowerThrottling (GetCurrentThread(), 1); // Enable EcoQoS for this thread
          SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

          PLOG_DEBUG << "SKIF_LibGameModWorker thread started!";

          int _appid = appid;
          modAppId.store (_appid);

          static const std::wstring root = SK_FormatStringW (LR"(%ws\Version\)", _path_cache.specialk_userdata);

          // Create any missing directories
          std::error_code ec;
          if (! std::filesystem::exists (            root, ec))
                std::filesystem::create_directories (root, ec);
      
          static std::wstring download, filename, path;

          switch (_appid)
          {
          case 359870: // FFX/X-2 HD Remaster
            filename = L"SpecialK_UnX.exe";
            download = L"https://sk-data.special-k.info/UnX/SpecialK_UnX.exe";
            break;
          case 429660: // Tales of Berseria
            filename = L"SpecialK_TBFix.exe";
            download = L"https://sk-data.special-k.info/TBFix/SpecialK_TBFix.exe";
            break;
          case 372360: // Tales of Symphonia
            filename = L"SpecialK_TSFix.exe";
            download = L"https://sk-data.special-k.info/TSFix/SpecialK_TSFix.exe";
            break;
          case 738540: // Tales of Vesperia DE
            filename = L"SpecialK_TVFix.exe";
            download = L"https://sk-data.special-k.info/TVFix/SpecialK_TVFix.exe";
            break;
          default:
            PLOG_ERROR << "Unknown app id: " << _appid;
            break;
          }

          path = root + filename;

          if (! filename.empty() && ! download.empty())
          {
            // Download the installer if it does not exist
            if (! PathFileExists (path.c_str()))
            {
              PLOG_INFO << "Downloading installer: " << download;
              SKIF_Util_GetWebResource (download, path);
            }
        
            modInstalling.store (true);

            // Note that any new process will inherit SKIF's environment variables
            if (_registry._LoadedSteamOverlay)
              SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);
  
            SHELLEXECUTEINFOW
              sexi              = { };
              sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
              sexi.lpVerb       = L"OPEN";
              sexi.lpFile       = path.c_str();
              sexi.lpParameters = NULL;
              sexi.lpDirectory  = NULL;
              sexi.nShow        = SW_SHOWNORMAL;
              sexi.fMask        = SEE_MASK_NOCLOSEPROCESS | // We need the PID of the process that gets started
                                  SEE_MASK_NOASYNC        | // Never async since we execute in short-lived child thread
                                  SEE_MASK_NOZONECHECKS;    // No zone check needs to be performed
              
            PLOG_INFO                    << "Performing a ShellExecuteEx call...";
            PLOG_INFO_IF(! path.empty()) << "File      : " << path;

            // Attempt to run the downloaded installer,
            //   and delete it on failure
            if (! ShellExecuteExW (&sexi))
            {
              PLOG_ERROR << "Something went wrong: "   << SKIF_Util_GetErrorAsWStr ( );
              PLOG_ERROR_IF (DeleteFile(path.c_str())) << "The downloaded installer has been removed!";
            }

            if (_registry._LoadedSteamOverlay)
              SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");
          
            // If the process was started successfully, wait for it to close down...
            if (sexi.hInstApp  > (HINSTANCE)32 &&
                sexi.hProcess != NULL)
            {
              WaitForSingleObject (sexi.hProcess, INFINITE); // == WAIT_OBJECT_0
              CloseHandle         (sexi.hProcess);
            }

            modInstalling.store (false);

            PLOG_INFO << "Finished installing a mod asynchronously...";
          }
        
          modAppId.store (0);
          modDownloading.store (false);

          // Force a refresh when the game icons have finished being streamed
          //PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

          PLOG_DEBUG << "SKIF_LibGameModWorker thread stopped!";

          SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

          return 0;
        }, nullptr, 0x0, nullptr);

        if (hWorkerThread != NULL)
          CloseHandle (hWorkerThread);
      }
    }
  }

  if (buttonPending)
    SKIF_ImGui_SetHoverTip ("Please finish the ongoing mod installation first.");

  if (loading || pApp->_status.running || pApp->_status.updating || buttonPending)
  {
    ImGui::EndDisabled   ( );
    ImGui::PopStyleColor ( );
  }

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right) &&
      GameMenu == PopupState_Closed)
  {
    GameMenu = PopupState_Open;
  }

  if (pApp->extended_config.vac.enabled == 1)
  {
    ImGui::SameLine ( );
    // Vertical center-align
    ImGui::SetCursorPosY (
       ImGui::GetCursorPosY ( ) +
      ((50.0f * SKIF_ImGui_GlobalDPIScale) -
       ImGui::GetFrameHeightWithSpacing()
      ) / 2
    );
    //ImGui::SetCursorPosY (ImGui::GetCursorPosY() + 50.0f + ImGui::GetStyle().FramePadding.y * SKIF_ImGui_GlobalDPIScale);
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
    SKIF_ImGui_SetHoverTip ("Warning: VAC protected game; injection is not recommended!");
  }

  ImGui::SetCursorPos (posButton);

  ImGui::EndChild     ( );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right) &&
      GameMenu       == PopupState_Closed &&
      EmptySpaceMenu == PopupState_Closed)
  {
    EmptySpaceMenu = PopupState_Open;
  }

  // Bottom bar: Disable Special K
  ImGui::SetCursorPosY (
    ImGui::GetWindowHeight () - fBottomDist);

  if (_registry.bUIBorders)
    ImGui::SetCursorPosY (
      ImGui::GetCursorPosY () -
      ImGui::GetStyle      ().ItemSpacing.y -
      ImGui::GetStyle      ().WindowBorderSize * 2.0f);

  ImGui::Separator     ( ); // -------------------------------

  SKIF_ImGui_BeginChildFrame  ( ImGui::GetID ("###launch_cfg"),
                                ImVec2 (ImGui::GetContentRegionAvail ().x,
                              std::max (ImGui::GetContentRegionAvail ().y,
                                        ImGui::GetTextLineHeight () + ImGui::GetStyle ().FramePadding.y * 2.0f + ImGui::GetStyle ().ItemSpacing.y * 2
                                        )),
                                ImGuiChildFlags_None,
                                ImGuiWindowFlags_NavFlattened      |
                                ImGuiWindowFlags_NoScrollbar       |
                                ImGuiWindowFlags_NoScrollWithMouse |
                                ImGuiWindowFlags_NoBackground
  );

  if ( pApp->specialk.injection.injection.type != InjectionType::Local )
  {
    auto _BlacklistCfg =
    [&](app_record_s::launch_config_s& launch_cfg, bool menu) ->
    void
    {
      bool blacklist =
        launch_cfg.isBlacklisted ( );
      bool blacklist_pattern =
        launch_cfg.isExecutableFullPathValid () &&
        _inject._TestUserList (SK_WideCharToUTF8 (launch_cfg.getExecutableFullPath()).c_str(), false);

      char          szButtonLabel [256] = { };

      if (menu)
        sprintf_s ( szButtonLabel, 255,
                      " for %s###DisableSpecialK-%d",
                        launch_cfg.getDescriptionUTF8().empty()
                          ? launch_cfg.getExecutableFileNameUTF8().c_str ()
                          : launch_cfg.getDescriptionUTF8().c_str (),
                        launch_cfg.id);
      else
        sprintf_s ( szButtonLabel, 255,
                      " Disable Special K###DisableSpecialK%d",
                        launch_cfg.id );

      if (blacklist_pattern)
      {
        blacklist = true;
        SKIF_ImGui_PushDisableState ( );
      }
          
      if (ImGui::Checkbox (szButtonLabel,   &blacklist))
        launch_cfg.setBlacklisted (blacklist);

      if (blacklist_pattern)
      {
        SKIF_ImGui_PopDisableState ( );
        SKIF_ImGui_SetHoverTip ("Special K is disabled through a blacklist pattern in the Settings tab.");
      }

      SKIF_ImGui_SetHoverText (launch_cfg.getExecutableFullPathUTF8 ( ).c_str());
    };

    if ( ! _inject.bHasServlet )
    {
      ImGui::BeginGroup      ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine        ( );
      ImGui::TextDisabled    ("Special K is unavailable due to missing files.");
      ImGui::EndGroup        ( );
      SKIF_ImGui_SetHoverTip ("Please reinstall Special K using the latest available installer.");
    }

    // Only show the bottom options if there's some launch configs, and the first one is valid...
    else if ( ! pApp->launch_configs.empty()) //&&
                //pApp->launch_configs[0].isExecutableFileNameValid())
    {
      /*
      bool elevate =
        pApp->launch_configs[0].isElevated ( );

      if (ImGui::Checkbox ("Elevated service###ElevatedLaunch",   &elevate))
        pApp->launch_configs[0].setElevated (elevate);

      ImGui::SameLine ( );
      */

      // Set horizontal position
      ImGui::SetCursorPosX (
        ImGui::GetCursorPosX  ( ) +
        ImGui::GetColumnWidth ( ) -
        ImGui::CalcTextSize   ("[] Disable Special K").x -
        ImGui::GetStyle       ( ).ItemSpacing.x * 2
      );

      static uint32_t currAppId  = 0;
      static int      numOfItems = 0;

      if (currAppId != pApp->id)
      {   currAppId  = pApp->id;

        numOfItems = 0;

        for ( auto& launch : pApp->launch_configs )
        {
          if (! launch.second.valid ||
                launch.second.duplicate_exe)
            continue;

          numOfItems++;
        }
      }

      // If there is 0-1 valid launch option
      if (numOfItems <= 1)
      {
        _BlacklistCfg          (
              pApp->launch_configs.begin ()->second, false );
      }

      // If there are more than one launch option
      else
      {
        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX  ( ) +
          14.0f * SKIF_ImGui_GlobalDPIScale
        );

        // Selectable is not as tall as Checkbox, so since we disabled 'Elevated Service', we need to push this one down a bit
        ImGui::SetCursorPosY(
          ImGui::GetCursorPosY  ( ) +
            ImGui::GetStyle().FramePadding.y
        );

        if (ImGui::Selectable (ICON_FA_BARS " Disable Special K "))
        {
          ImVec2 pos =
            ImGui::GetCursorPos ( );

          ImGui::SameLine     ( );

          ImGui::SetNextWindowPos (
            ImGui::GetCursorScreenPos ( ) -
            ImVec2 (0.0f, 8.0f * SKIF_ImGui_GlobalDPIScale)
          );

          ImGui::OpenPopup    ("###DisableSK");
          ImGui::SetCursorPos (pos);
        }

        if (ImGui::BeginPopup ("###DisableSK", ImGuiWindowFlags_NoMove))
        {
          //std::set <std::wstring> _used_launches;
          bool sepCustomSKIF = true,
                sepCustomUser = true;

          for ( auto& launch : pApp->launch_configs )
          {
            if (! launch.second.valid || 
                  launch.second.duplicate_exe)
              continue;

            // Separators between official / user / SKIF launch configs

            if (launch.second.custom_user && sepCustomUser)
            {
              sepCustomUser = false;
              ImGui::Separator ( );
            }

            else if (launch.second.custom_skif && sepCustomSKIF)
            {
              sepCustomSKIF = false;
              ImGui::Separator ( );
            }

            _BlacklistCfg (launch.second, true);
          }

          ImGui::EndMenu       ();
        }
      }
    }
  }

  else
    ImGui::NewLine ( );

  ImGui::EndChild ( );
  fBottomDist = ImGui::GetItemRectSize().y;
}

#pragma endregion


#pragma region UpdateInjectionStrategy

static void
UpdateInjectionStrategy (app_record_s* pApp, std::set <std::string> apptickets)
{
  if (pApp == nullptr || pApp->id == SKIF_STEAM_APPID)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  bool registry_local = false;

  // Use a registry based method as the primary choice of information for local injections
  HKEY hKey;

  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Local\)", 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
  {
    DWORD dwIndex           = 0, // A variable that receives the number of values that are associated with the key.
          dwResult          = 0,
          dwMaxValueNameLen = 0; // A pointer to a variable that receives the size of the key's longest value name, in Unicode characters. The size does not include the terminating null character.

    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, NULL, NULL, NULL, &dwIndex, &dwMaxValueNameLen, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;
          
        DWORD dwValueNameLen =
              (dwMaxValueNameLen + 2);

        std::unique_ptr <wchar_t []> pValue =
          std::make_unique <wchar_t []> (sizeof (wchar_t) * dwValueNameLen);

        DWORD dwType = REG_NONE;

        dwResult = RegEnumValueW (hKey, dwIndex, (wchar_t *) pValue.get(), &dwValueNameLen, NULL, &dwType, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS && dwType == REG_SZ)
        {
          if (StrStrIW (pValue.get(), (pApp->install_dir + LR"(\)").c_str()) != NULL)
          {
            std::wstring dll_full_path = std::wstring(pValue.get());
              
            std::wstring dll_ver =
              SKIF_Util_GetSpecialKDLLVersion (dll_full_path.c_str ());

            // Another validation that this is, in fact, a Special K DLL file we're dealing with
            if (! dll_ver.empty ())
            {
              registry_local = true;
              PLOG_VERBOSE << "Found local injection at: " << pValue;

              std::filesystem::path p = dll_full_path;
              std::wstring dll_name   = p.filename().replace_extension();
              std::wstring test_path  = p.parent_path();

              const std::vector <wchar_t *> cleaned = {
                L"DXGI",
                L"D3D11",
                L"D3D9",
                L"OpenGL32",
                L"DInput8",
                L"D3D8",
                L"DDraw"
              };

              for (auto& clean : cleaned)
              {
                if (StrStrIW (dll_name.c_str(), clean) != NULL)
                  dll_name = clean;
              }

              pApp->specialk.injection.injection.type         = InjectionType::Local;
              pApp->specialk.injection.dll.shorthand          = dll_name + L".dll";
              pApp->specialk.injection.dll.shorthand_utf8     = SK_WideCharToUTF8 (pApp->specialk.injection.dll.shorthand);
              pApp->specialk.injection.dll.full_path          = test_path + LR"(\)" + dll_name + L".dll"; // This allows us to "clean" the DLL file of the full patch as well
              pApp->specialk.injection.dll.full_path_utf8     = SK_WideCharToUTF8 (pApp->specialk.injection.dll.full_path);
              pApp->specialk.injection.dll.version            = dll_ver;
              pApp->specialk.injection.dll.version_utf8       = SK_WideCharToUTF8 (pApp->specialk.injection.dll.version);

              if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str()))
                pApp->specialk.injection.config.type          = ConfigType::Centralized;
              else
              {
                pApp->specialk.injection.config.type          = ConfigType::Localized;
                pApp->specialk.injection.config.root_dir      = test_path;
                pApp->specialk.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.root_dir);
              }

              pApp->specialk.injection.config.shorthand       = dll_name + L".ini";
              pApp->specialk.injection.config.shorthand_utf8  = SK_WideCharToUTF8 (pApp->specialk.injection.config.shorthand);
            }

            else {
              // If it is not, remove the registry value
              LSTATUS ls = RegDeleteValueW (hKey, pValue.get());
              PLOG_ERROR_IF (ls != ERROR_SUCCESS) << "Failed to delete registry value: " << SKIF_Util_GetErrorAsWStr (ls);
            }
          }
        }
      }
    }

    RegCloseKey (hKey);
  }

  int firstValidFound = -1;

  for ( auto& launch_cfg : pApp->launch_configs )
  {
    auto& launch = launch_cfg.second;

    if (! launch.valid)
      continue;

    if (! launch.isExecutableFullPathValid ( ))
      continue;

    if (firstValidFound == -1)
      firstValidFound = launch_cfg.first;

    // Assume global
    launch.injection.injection.type        =
      (registry_local) ? InjectionType::Local
                       : InjectionType::Global;
    launch.injection.config.type           =
      (registry_local) ? pApp->specialk.injection.config.type
                       : ConfigType::Centralized;
    launch.injection.config.shorthand      =
      (registry_local) ? pApp->specialk.injection.config.shorthand
                       : L"SpecialK.ini";
    launch.injection.config.shorthand_utf8 =
      SK_WideCharToUTF8 (launch.injection.config.shorthand);

    // TODO: This needs to be gone through and reworked entirely as the logic still gets thrown off by edge cases
    //  - e.g. EA games with "invalid" launch options where the 'osarch' of the common_config ends up never being used

    // Check bitness
    if (launch.injection.injection.bitness == InjectionBitness::Unknown)
    {
      app_record_s::CPUType
          cpu_type  = pApp->common_config.cpu_type; // We start by using the common config

      if (cpu_type != app_record_s::CPUType::Any)
      {
        if (launch.cpu_type != app_record_s::CPUType::Common)
        {
          cpu_type =
            launch.cpu_type;
        }
      }

      else
      {
        // The any case will just be 64-bit for us, since SK only runs on
        //   64-bit systems. Thus, ignore 32-bit launch configs.
#ifdef _WIN64
        if (launch.cpu_type == app_record_s::CPUType::x86)
#else
        if (launch.cpu_type == app_record_s::CPUType::x64)
#endif
        {
          continue;
        }

        else {
          cpu_type =
            launch.cpu_type;
        }
      }
      
      if (     cpu_type == app_record_s::CPUType::x64)
        launch.injection.injection.bitness = InjectionBitness::SixtyFour;

      else if (cpu_type == app_record_s::CPUType::x86)
        launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;

      // If we still haven't resolved it, use SKIF's cached property
      else if (pApp->skif.cpu_type != 0)
        launch.injection.injection.bitness = (InjectionBitness)pApp->skif.cpu_type;

      // In case we still haven't resolved the CPU architecture,
      //   we need to check the actual arch of the game executable
      else // Common || Any
      {
        if (launch.isExecutableFullPathValid ())
        {
          std::wstring exec_path =
            launch.getExecutableFullPath ( );

          DWORD dwBinaryType = MAXDWORD;
          if (GetBinaryTypeW (exec_path.c_str (), &dwBinaryType))
          {
            if (dwBinaryType == SCS_64BIT_BINARY)
              launch.injection.injection.bitness = InjectionBitness::SixtyFour;
            else if (dwBinaryType == SCS_32BIT_BINARY)
              launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;
          }
        }
      }
    }
    // End checking bitness

    // Fall back to the classic local detection code
    if (InjectionType::Local != pApp->specialk.injection.injection.type)
    {
      // Check for local injections
      struct {
        std::wstring     name;
        std::wstring     path;
      } test_dlls [] = { // The small things matter -- array is sorted in the order of most expected
        { L"DXGI",     L"" },
        { L"D3D11",    L"" },
        { L"D3D9",     L"" },
        { L"OpenGL32", L"" },
        { L"DInput8",  L"" },
        { L"D3D8",     L"" },
        { L"DDraw",    L"" }
      };

      std::wstring test_paths[] = { 
        pApp->launch_configs[0].getExecutableDir ( ),
        pApp->launch_configs[0].working_dir
      };

      if (test_paths[0] == test_paths[1])
        test_paths[1] = L"";

      bool breakOuterLoop = false;
      for ( auto& test_path : test_paths)
      {
        if (test_path.empty())
          continue;
      
        std::wstring test_pattern =
          test_path + LR"(\*.dll)";

        WIN32_FIND_DATA ffd         = { };
        HANDLE          hFind       =
          FindFirstFileExW (test_pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

        if (hFind != INVALID_HANDLE_VALUE)
        {
          do
          {
            if ( wcscmp (ffd.cFileName, L"." ) == 0 ||
                 wcscmp (ffd.cFileName, L"..") == 0 )
              continue;

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
              continue;

            for ( auto& dll : test_dlls )
            {
              // Filename + extension
              dll.path = dll.name + L".dll";
            
              if (StrStrIW (ffd.cFileName, dll.path.c_str()) == NULL)
                continue;
          
              // Full path
              dll.path = test_path + LR"(\)" + dll.path;

              std::wstring dll_ver =
                SKIF_Util_GetSpecialKDLLVersion (dll.path.c_str ());

              if (dll_ver.empty ())
                continue;

              launch.injection.injection.type         = InjectionType::Local;

              launch.injection.dll.shorthand          = dll.name + L".dll";
              launch.injection.dll.shorthand_utf8     = SK_WideCharToUTF8 (launch.injection.dll.shorthand);
              launch.injection.dll.full_path          = dll.path;
              launch.injection.dll.full_path_utf8     = SK_WideCharToUTF8 (launch.injection.dll.full_path);
              launch.injection.dll.version            = dll_ver;
              launch.injection.dll.version_utf8       = SK_WideCharToUTF8 (launch.injection.dll.version);

              if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
                launch.injection.config.type          = ConfigType::Centralized;
              else
              {
                launch.injection.config.type          = ConfigType::Localized;
                launch.injection.config.root_dir      = test_path;
                launch.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (launch.injection.config.root_dir);
              }

              launch.injection.config.shorthand       = dll.name + L".ini";
              launch.injection.config.shorthand_utf8  = SK_WideCharToUTF8 (launch.injection.config.shorthand);

              breakOuterLoop = true;
              break;
            }

            if (breakOuterLoop)
              break;

          } while (FindNextFile (hFind, &ffd));

          FindClose (hFind);
        }

        if (breakOuterLoop)
          break;
      }
    }

    // Check if the launch config is elevated or blacklisted
    launch.isElevated    ( );
    launch.isBlacklisted ( );
  } // End for ( auto& launch_cfg : pApp->launch_configs )

  // Swap out the first element for the first valid one we found
  if (firstValidFound != -1)
  {
    app_record_s::launch_config_s copy    = pApp->launch_configs[0];
    pApp->launch_configs[0]               = pApp->launch_configs[firstValidFound];
    pApp->launch_configs[firstValidFound] = copy;
  }

  // TODO: Make the specialk.injection bitness/state/etc stuff bound
  //         to launch_config so it is not universal any longer

  auto& launch = pApp->launch_configs.begin()->second;

  // If primary launch config was invalid (e.g. Link2EA games) then set it to use global
  if (launch.injection.injection.type == InjectionType::Unknown)
  {
    // Assume global
    launch.injection.injection.type        =
      InjectionType::Global;
    launch.injection.config.type           =
      ConfigType::Centralized;
    launch.injection.config.shorthand      =
      L"SpecialK.ini";
    launch.injection.config.shorthand_utf8 =
      SK_WideCharToUTF8 (launch.injection.config.shorthand);
  }

  // Main UI stuff should follow the primary launch config
  if (! registry_local)
    pApp->specialk.injection = launch.injection;

  if ( InjectionType::Global ==
      pApp->specialk.injection.injection.type )
  {
    // Assume Global 32-bit if we don't know otherwise
    bool bIs64Bit =
      (pApp->specialk.injection.injection.bitness ==
                        InjectionBitness::SixtyFour);

    pApp->specialk.injection.config.type =
      ConfigType::Centralized;

    pApp->specialk.injection.dll.shorthand      = 
                                       bIs64Bit ? L"SpecialK64.dll"
                                                : L"SpecialK32.dll";
    pApp->specialk.injection.dll.shorthand_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.dll.shorthand);
    pApp->specialk.injection.dll.full_path      = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, pApp->specialk.injection.dll.shorthand.c_str());
    pApp->specialk.injection.dll.full_path_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.dll.full_path);
    pApp->specialk.injection.dll.version        = 
                                       bIs64Bit ? _inject.SKVer64
                                                : _inject.SKVer32;
    pApp->specialk.injection.dll.version_utf8   = SK_WideCharToUTF8 (pApp->specialk.injection.dll.version);
  }

  // For local injections, we need to populate the registry as well (used for managed updates)
  else if ( InjectionType::Local ==
           pApp->specialk.injection.injection.type )
  {
    LSTATUS lsKey = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Local)", &hKey);
      
    if (ERROR_SUCCESS == lsKey)
    {
      if (ERROR_SUCCESS != RegSetValueExW (hKey, pApp->specialk.injection.dll.full_path.c_str(), 0, REG_SZ, (LPBYTE)pApp->specialk.injection.dll.version.data(), (DWORD)pApp->specialk.injection.dll.version.length() * sizeof(wchar_t)))
        PLOG_ERROR << "Failed adding local install (" << pApp->specialk.injection.dll.version << ") to registry value: " << pApp->specialk.injection.dll.full_path;

      RegCloseKey (hKey);
    }
  }

  // Use a localized profile name
  if (pApp->specialk.injection.localized_name.empty())
  {
    pApp->specialk.injection.localized_name =
      (pApp->store == app_record_s::Store::Steam)
        ? SK_UseManifestToGetAppName (pApp)
        : pApp->names.original; // Use real original

    // Strip any remaining null terminators
    pApp->specialk.injection.localized_name.erase(std::find(pApp->specialk.injection.localized_name.begin(), pApp->specialk.injection.localized_name.end(), '\0'), pApp->specialk.injection.localized_name.end());

    std::wstring name =
      SK_UTF8ToWideChar (pApp->specialk.injection.localized_name);

    name.erase ( std::remove_if ( name.begin (),
                                  name.end   (),

                              [](wchar_t tval)
                              {
                                static
                                const std::set <wchar_t>
                                  invalid_file_char =
                                  {
                                    L'\\', L'/', L':',
                                    L'*',  L'?', L'\"',
                                    L'<',  L'>', L'|',
                                  //L'&',

                                    //
                                    // Obviously a period is not an invalid character,
                                    //   but three of them in a row messes with
                                    //     Windows Explorer and some Steam games use
                                    //       ellipsis in their titles.
                                    //
                                    L'.'
                                  };

                                return
                                  ( invalid_file_char.find (tval) !=
                                    invalid_file_char.end  (    ) );
                              }
                          ),

               name.end ()
         );

    // Strip trailing spaces from name, these are usually the result of
    //   deleting one of the non-useable characters above.
    for (auto it = name.rbegin (); it != name.rend (); ++it)
    {
      if (*it == L' ') *it = L'\0';
      else                   break;
    }

    // Strip any remaining null terminators
    name.erase(std::find(name.begin(), name.end(), '\0'), name.end());

    pApp->specialk.profile_dir      = name;
    pApp->specialk.profile_dir_utf8 = SK_WideCharToUTF8 (pApp->specialk.profile_dir);
  }

  if ( ConfigType::Centralized ==
         pApp->specialk.injection.config.type )
  {
    pApp->specialk.injection.config.root_dir      =
      SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                            _path_cache.specialk_userdata,
                            pApp->specialk.profile_dir.c_str());
    pApp->specialk.injection.config.root_dir_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.root_dir);
  }

  pApp->specialk.injection.config.full_path      =
    ( pApp->specialk.injection.config.root_dir + LR"(\)" ) +
      pApp->specialk.injection.config.shorthand;
  pApp->specialk.injection.config.full_path_utf8 = SK_WideCharToUTF8 (pApp->specialk.injection.config.full_path);


  // v-<-<- Shared between games of all platforms ->->-v

  switch (pApp->specialk.injection.config.type)
  {
    case ConfigType::Centralized:
      pApp->specialk.injection.config.type_utf8 = "Centralized"; break;
    case ConfigType::Localized:
      pApp->specialk.injection.config.type_utf8 = "Localized";   break;
    default:
      pApp->specialk.injection.config.type_utf8 = "Unknown";     break;
  }

  // Contemplate this -- should we bother? Why not just use _inject.bCurrentState ?
  /*
  bool service  = (pApp->specialk.injection.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
                  (pApp->specialk.injection.injection.bitness == InjectionBitness::SixtyFour &&  _inject.pid64) ||
                  (pApp->specialk.injection.injection.bitness == InjectionBitness::Unknown   && (_inject.pid32  &&
                                                                                                 _inject.pid64));
  */

  switch (pApp->specialk.injection.injection.type)
  {
    case InjectionType::Local:
      pApp->ui.label_version = SKIF_Util_FormatStringRaw (R"(v %s (%s))", pApp->specialk.injection.dll.version_utf8.c_str(), pApp->specialk.injection.dll.shorthand_utf8.c_str()); // injection.type_utf8.c_str()
      break;

    case InjectionType::Global:
    default: // Unknown injection strategy, but let's assume global would work
      if ( _inject.bHasServlet )
        pApp->ui.label_version = SKIF_Util_FormatStringRaw (R"(v %s)", pApp->specialk.injection.dll.version_utf8.c_str()); // injection.type_utf8.c_str() // We don't actually have SKIF say "Global v XXX" any longer due to space constraints -- Aemony, 2024-01-04
      break;
  }

  // Refresh the context menu cached data
  
  // Profile + Screenshots
  //pApp->ui.profileFolderExists     = PathFileExists (pApp->specialk.injection.config.root_dir.c_str());
  pApp->ui.wsScreenshotDir         = pApp->specialk.injection.config.root_dir + LR"(\Screenshots)";
  pApp->ui.screenshotsFolderExists = PathFileExists (pApp->ui.wsScreenshotDir.c_str()); // (pApp->ui.profileFolderExists) ? 

  // Check how many secondary launch configs are valid
  pApp->ui.numSecondaryLaunchConfigs = 0;
  for (auto& _launch_cfg : pApp->launch_configs)
  {
    if (_launch_cfg.second.owns_dlc == -1)
        _launch_cfg.second.owns_dlc  = (_launch_cfg.second.requires_dlc.empty())
                                     ? 1 // If the launch cfg does not have a DLC requirement, then we "own" it (purely an optimization thing)
                                     : (! apptickets.empty() && apptickets.find (_launch_cfg.second.requires_dlc) != apptickets.end()); // Only show DLC options if the user has an app ticket for it
    // Note that this design hides all DLC launch options until the user has signed into Steam

    if (! _launch_cfg.second.owns_dlc)
      continue;

    if (_launch_cfg.first == 0)
      continue;

    if (! _launch_cfg.second.valid ||
          _launch_cfg.second.duplicate_exe_args)
      continue;
    
    pApp->ui.numSecondaryLaunchConfigs++;
  }

  // Steam Auto-Cloud
  pApp->ui.cloud_paths.clear();
  if (pApp->cloud_enabled && // If this is false, Steam Auto-Cloud is not enabled
    ! pApp->cloud_saves.empty ())
  {
    std::set <std::wstring> _used_paths;

    for (auto& cloud : pApp->cloud_saves)
    {
      if (cloud.second.valid == 0)
        continue;

      if (app_record_s::supports (cloud.second.platforms, app_record_s::Platform::Windows) && 
          app_record_s::Platform::Unknown != cloud.second.platforms)
        cloud.second.valid = 0;

      if (cloud.second.valid == -1)
        cloud.second.valid = PathFileExistsW (cloud.second.evaluated_dir.c_str());

      if (cloud.second.valid)
      {
        // Filter out duplicate paths
        if (_used_paths.emplace (cloud.second.evaluated_dir).second)
          pApp->ui.cloud_paths.emplace_back (app_record_s::CloudPath (cloud.first, cloud.second.evaluated_dir));
      }
    }
  }

  // PCGamingWiki
  pApp->ui.pcgw  = ((pApp->store == app_record_s::Store::GOG)   ? "https://www.pcgamingwiki.com/api/gog.php?page="
                 :  (pApp->store == app_record_s::Store::Steam) ? "https://www.pcgamingwiki.com/api/appid.php?appid="
                                                                : "https://www.pcgamingwiki.com/w/index.php?search=")
                 + ((pApp->store == app_record_s::Store::Steam  || pApp->store == app_record_s::Store::GOG)
                 ? std::to_string(pApp->id) : pApp->names.clean);

  // SteamGridDB
  pApp->ui.sgdbGrids = (pApp->store == app_record_s::Store::Steam)
                     ? SKIF_Util_FormatStringRaw ("https://www.steamgriddb.com/steam/%lu/grids",      pApp->id)
                     : SKIF_Util_FormatStringRaw ("https://www.steamgriddb.com/search/grids?term=%s", pApp->names.clean.c_str());
  pApp->ui.sgdbIcons = (pApp->store == app_record_s::Store::Steam)
                     ? SKIF_Util_FormatStringRaw ("https://www.steamgriddb.com/steam/%lu/icons",      pApp->id)
                     : SKIF_Util_FormatStringRaw ("https://www.steamgriddb.com/search/icons?term=%s", pApp->names.clean.c_str());

  // Steam Branches
  pApp->ui.branches.clear ();
  std::set <std::string> used_branches;
  for ( auto& it : pApp->branches )
  {
    if (used_branches.emplace (it.first).second)
    {
      auto& branch =
        it.second;

      // TODO: Maybe split sort in public v. private?
      
      // Sort in descending order
      pApp->ui.branches.emplace (
        std::make_pair   (-(int64_t)branch.build_id,
          std::make_pair (
            it.first,
            it.second
          )
        )
      );
    }
  }
}

#pragma endregion



void
SKIF_UI_Tab_DrawLibrary (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::GetInstance  ( );
  
  static SKIF_DirectoryWatch     SKIF_Epic_ManifestWatch;

  static CComPtr <ID3D11ShaderResourceView> pTexSRV;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV_old;

  static ImVec2 vecCoverUv0     = ImVec2 (0, 0),
                vecCoverUv1     = ImVec2 (1, 1),
                vecCoverUv0_old = ImVec2 (0, 0),
                vecCoverUv1_old = ImVec2 (1, 1);

  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };

  // This keeps track of the amount of workers streaming icons that we have active in the background
  static int   activeIconWorkers     = 0; // max: 8
  static int   activeGameWorkers     = 0; // max: 3
  static int   frameLibraryRefreshed = 0;

  // The value needs to be set here since it relies on _path_cache to have been initated
  file_metadata = SK_FormatStringW(LR"(%ws\Assets\db.json)", _path_cache.specialk_userdata);

#pragma region Initialization

  if (_registry.bLibrarySteam)
  {
    // Set up the registry watch on the Steam ActiveProcess key and ActiveUser value
    SK_RunOnce (
      SKIF_Steam_HasActiveProcessChanged (nullptr, nullptr);
    )
  }

  // This populates the g_apps entry with Special K so its not empty even if the worker takes awhile to finish
  // A bit too disruptive atm, so let's ignore it for now
#if 0
  static bool
      lib_init = true;
  if (lib_init)
  {   lib_init = false;
    
    app_record_s SKIF_record (SKIF_STEAM_APPID);

    SKIF_record.id                = SKIF_STEAM_APPID;
    SKIF_record.names.normal      = "Special K";
    SKIF_record.names.all_upper   = "SPECIAL K";
    SKIF_record._status.installed = true;
    SKIF_record.install_dir       = _path_cache.specialk_install;
    SKIF_record.store             = app_record_s::Store::Steam;
    SKIF_record.store_utf8        = "Steam";
    SKIF_record.ImGuiLabelID      = SK_FormatString("%s###%i-%i", SKIF_record.names.normal.c_str(), (int)SKIF_record.store, SKIF_record.id);
    SKIF_record.ImGuiPushID       = SK_FormatString("###%i-%i", (int)SKIF_record.store, SKIF_record.id);

    SKIF_record.specialk.profile_dir      = SK_FormatStringW(LR"(%ws\Profiles)", _path_cache.specialk_userdata);
    SKIF_record.specialk.profile_dir_utf8 = SK_WideCharToUTF8 (SKIF_record.specialk.profile_dir);

    std::pair <std::string, app_record_s>
      SKIF ( "Special K", SKIF_record );

    g_apps.emplace_back (SKIF);
  }
#endif

  auto& io =
    ImGui::GetIO ();

  SK_RunOnce (fAlpha = (_registry.bFadeCovers) ? 0.0f : 1.0f);

  DWORD       current_time = SKIF_Util_timeGetTime ( );
  static bool update       = true;

  struct {
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
  } static selection, item_clicked, lastCover;

  // We need to ensure the lastCover isn't set to SKIF's app ID as that would prevent the cover from loading on launch
  SK_RunOnce (lastCover.reset_to_skif = false; lastCover.reset());

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
      PLOG_INFO << "[Library Pre-Processing] Steam took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      time_current = SKIF_Util_timeGetTime1 ( );
    }

    // Sets up a wait object on UITab_Library
    if (SKIF_Epic_ManifestWatch.isSignaled (SKIF_Epic_AppDataPath) && _registry.bLibraryEpic)
      RepopulateGames = true;

    if (runOnce)
    {
      PLOG_INFO << "[Library Pre-Processing] Epic took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
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
      PLOG_INFO << "[Library Pre-Processing] GOG took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      time_current = SKIF_Util_timeGetTime1 ( );
    }

    time_current = SKIF_Util_timeGetTime1 ( );

    // Sets up a wait object on UITab_Library
    if (SKIF_Xbox_hasInstalledGamesChanged ( ) && _registry.bLibraryXbox)
      RepopulateGames = true;

    if (runOnce)
    {
      PLOG_INFO << "[Library Pre-Processing] Xbox took " << (SKIF_Util_timeGetTime1 ( ) - time_current) << " ms.";
      PLOG_INFO << "[Library Pre-Processing] Total process took " << (SKIF_Util_timeGetTime1 ( ) - time_preprocessing) << " ms.";
      runOnce = false;
    }
  }

  // We cannot manipulate the apps array while the game worker thread is running, nor any active icon workers
  if (RepopulateGames && activeIconWorkers == 0) // ! gameWorkerRunning.load()
  {
    PLOG_VERBOSE << "RepopulateGames && activeIconWorkers == 0";

    RepopulateGames = false;
    //gameWorkerRunning.store(true);

    PopulatedGames = false;
  }
  
  else if (RepopulateGames)
  {
    PLOG_VERBOSE << "RepopulateGames " << activeIconWorkers;
  }


  struct lib_worker_thread_s {
    std::vector <
      std::pair <std::string, app_record_s >
                > apps       = { };
    std::set    < std::string >
                  apptickets = { };
    SteamId3_t    steam_user = 0;
    Trie          labels     = { };
    HANDLE        hWorker    = NULL;
    int           iWorker    = 0;
  };

  static lib_worker_thread_s* library_worker = nullptr;

  if (! PopulatedGames && library_worker == nullptr)
  {
    PLOG_INFO << "Populating library list...";

    // Initialize/reset the Steam appinfo.vdf Reader
    appinfo = std::make_unique <skValveDataFile>(std::wstring(_path_cache.steam_install) + LR"(\appcache\appinfo.vdf)");

    library_worker = new lib_worker_thread_s;
    library_worker->steam_user = SKIF_Steam_GetCurrentUser ( );

    HANDLE hWorkerThread = (HANDLE)
    _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibraryWorker");

      // Is this combo really appropriate for this thread?
      //SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
      //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

      PLOG_DEBUG << "SKIF_LibraryWorker thread started!";
      
      DWORD pre   = 0,
            post  = 0,
            start = SKIF_Util_timeGetTime1 ( );

      lib_worker_thread_s* _data = static_cast<lib_worker_thread_s*>(var);

      size_t games = _data->apps.size();

      // Load Steam titles from disk
      if (_registry.bLibrarySteam || _registry._LibraryHidden)
      {
        pre  = start;

        SKIF_Steam_GetInstalledAppIDs (&_data->apps);

        if (! _registry._LibraryHidden)
        {
          post = SKIF_Util_timeGetTime1 ( );
          PLOG_INFO << "[Library Processing] Processed " << (_data->apps.size() - games) << " Steam games in " << (post - pre) << " ms.";
          pre  = post;
          games = _data->apps.size();
        }

        // Preload user-specific stuff for all Steam games (custom launch options + DLC ownership)
        SKIF_Steam_PreloadUserConfig (_data->steam_user, &_data->apps, &_data->apptickets);

        if (! _registry._LibraryHidden)
        {
          post = SKIF_Util_timeGetTime1 ( );
          PLOG_INFO << "[Library Processing] Processed Steam user configs in " << (post - pre) << " ms.";
          pre  = post;
        }
      }

      if ( ! SKIF_STEAM_OWNER )
      {
        app_record_s SKIF_record (SKIF_STEAM_APPID);

        SKIF_record.id                = SKIF_STEAM_APPID;
        SKIF_record.names.normal      = "Special K";
        SKIF_record.names.all_upper   = "SPECIAL K";
        SKIF_record._status.installed = true;
        SKIF_record.install_dir       = _path_cache.specialk_install;
        SKIF_record.store             = app_record_s::Store::Steam;
        SKIF_record.store_utf8        = "Steam";
        SKIF_record.ImGuiLabelID      = SKIF_Util_FormatStringRaw ("##%i-%i-selectable", (int)SKIF_record.store, SKIF_record.id);
        SKIF_record.ImGuiPushID       = SKIF_Util_FormatStringRaw ("##%i-%i",            (int)SKIF_record.store, SKIF_record.id);

        SKIF_record.specialk.profile_dir      = SK_FormatStringW(LR"(%ws\Profiles)", _path_cache.specialk_userdata);
        SKIF_record.specialk.profile_dir_utf8 = SK_WideCharToUTF8 (SKIF_record.specialk.profile_dir);

        std::pair <std::string, app_record_s>
          SKIF ( "Special K", SKIF_record );

        _data->apps.emplace_back (SKIF);
        games = _data->apps.size();
      }

      // Load GOG titles from registry
      if (_registry.bLibraryGOG || _registry._LibraryHidden)
      {
        SKIF_GOG_GetInstalledAppIDs  (&_data->apps);

        if (! _registry._LibraryHidden)
        {
          post = SKIF_Util_timeGetTime1 ( );
          PLOG_INFO << "[Library Processing] Processed " << (_data->apps.size() - games) << " GOG games in " << (post - pre) << " ms.";
          pre  = post;
          games = _data->apps.size();
        }
      }

      // Load Epic titles from disk
      if (_registry.bLibraryEpic || _registry._LibraryHidden)
      {
        SKIF_Epic_GetInstalledAppIDs (&_data->apps);

        if (! _registry._LibraryHidden)
        {
          post = SKIF_Util_timeGetTime1 ( );
          PLOG_INFO << "[Library Processing] Processed " << (_data->apps.size() - games) << " Epic games in " << (post - pre) << " ms.";
          pre  = post;
          games = _data->apps.size();
        }
      }
    
      if (_registry.bLibraryXbox || _registry._LibraryHidden)
      {
        SKIF_Xbox_GetInstalledAppIDs (&_data->apps);

        if (! _registry._LibraryHidden)
        {
          post = SKIF_Util_timeGetTime1 ( );
          PLOG_INFO << "[Library Processing] Processed " << (_data->apps.size() - games) << " Xbox games in " << (post - pre) << " ms.";
          pre  = post;
          games = _data->apps.size();
        }
      }

      // Load custom SKIF titles from registry
      if (_registry.bLibraryCustom || _registry._LibraryHidden)
      {
        SKIF_GetCustomAppIDs (&_data->apps);

        if (! _registry._LibraryHidden)
        {
          post = SKIF_Util_timeGetTime1 ( );
          PLOG_INFO << "[Library Processing] Processed " << (_data->apps.size() - games) << " custom SKIF titles in " << (post - pre) << " ms.";
          games = _data->apps.size();
        }
      }

      PLOG_INFO << "Loading custom launch configs synchronously...";

      static const std::pair <bool, std::wstring> lc_files[] = {
        { false, SK_FormatStringW (LR"(%ws\Assets\lc_user.json)", _path_cache.specialk_userdata) }, // We load user-specified first
        {  true, SK_FormatStringW (LR"(%ws\Assets\lc.json)",      _path_cache.specialk_userdata) }
      };

      for (auto& lc_file : lc_files)
      {
        std::ifstream file(lc_file.second);
        nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
        file.close();

        if (jf.is_discarded ( ))
        {
          PLOG_ERROR << "Error occurred while trying to parse " << lc_file.second;

          // We are dealing with lc.json and something went wrong
          //   delete the file so a new attempt is performed later
          if (lc_file.first)
            PLOG_INFO_IF(DeleteFile (lc_file.second.c_str())) << "Deleting file so a retry occurs the next time an online check is performed...";

          continue;
        }

        try {
          for (auto& app : _data->apps)
          {
            auto& record     =  app.second;
            auto& append_cfg = (record.store == app_record_s::Store::Steam) ? record.launch_configs_custom
                                                                            : record.launch_configs;

            std::string key  = (record.store == app_record_s::Store::Epic)  ? record.epic.name_app     :
                               (record.store == app_record_s::Store::Xbox)  ? record.xbox.package_name :
                                                              std::to_string (record.id);

            for (auto& launch_config : jf[record.store_utf8][key])
            {
              app_record_s::launch_config_s lc;
              lc.id                       = static_cast<int> (append_cfg.size());
              lc.valid                    = 1;
              lc.description              = SK_UTF8ToWideChar(launch_config.at("Desc"));
              lc.executable               = SK_UTF8ToWideChar(launch_config.at("Exe"));
              std::replace(lc.executable.begin(), lc.executable.end(), '/', '\\'); // Replaces all forward slashes with backslashes
              lc.working_dir              = SK_UTF8ToWideChar(launch_config.at("Dir"));
              lc.launch_options           = SK_UTF8ToWideChar(launch_config.at("Args"));
              lc.executable_path          = record.install_dir + LR"(\)" + lc.executable;
              lc.install_dir              = record.install_dir;
              lc.custom_skif              =   lc_file.first;
              lc.custom_user              = ! lc.custom_skif;

              append_cfg.emplace (lc.id, lc);
            }
          }
        }
        catch (const std::exception&)
        {
          PLOG_ERROR << "Error occurred when trying to parse " << ((lc_file.first) ? "online-based" : "user-specified") << " launch configs";
        }
      }

      PLOG_INFO << "Loading persistent metadata...";

      std::ifstream file(file_metadata);
      if (file.is_open())
      {
        jsonMetaDB = nlohmann::json::parse(file, nullptr, false);
        file.close();

        if (jsonMetaDB.is_discarded ( ))
        {
          PLOG_ERROR << "Error occurred while trying to parse " << file_metadata;
          MoveFileEx (file_metadata.c_str(), (file_metadata + L".bak").c_str(), MOVEFILE_REPLACE_EXISTING);
          jsonMetaDB = nlohmann::json();
        }
      }

      PLOG_INFO << "Processing detected games...";
      pre = SKIF_Util_timeGetTime1 ( );

      bool     newCategories = true;
      HKEY     hKey;
      LSTATUS lsKey = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Profiles)", &hKey);

      // UNIX timestamp for new arrivals
      time_t ltime;
      time (&ltime);
      std::string szTime = std::to_string (ltime);

      // Process the list of apps -- prepare their names, keyboard search, as well as remove any uninstalled entries
      for (auto& app : _data->apps)
      {
        PLOG_VERBOSE << "Working on " << app.second.id << " (" << app.second.store_utf8 << ")";

        bool isSpecialK = (app.second.store == app_record_s::Store::Steam && app.second.id == SKIF_STEAM_APPID);

        // Steam handling...
        // Special handling for non-Steam owners of Special K / SKIF
        if (isSpecialK)
        {
          app.first               = "Special K";

          // Default values that needs to be set
          app.second.skif.pinned  = 99; // Default to pinned
          app.second.ui.pcgw      = SKIF_Util_FormatStringRaw ("https://www.pcgamingwiki.com/api/appid.php?appid=%lu", app.second.id);
          app.second.ui.sgdbGrids = SKIF_Util_FormatStringRaw ("https://www.steamgriddb.com/steam/%lu/grids",          app.second.id);
          app.second.ui.sgdbIcons = SKIF_Util_FormatStringRaw ("https://www.steamgriddb.com/steam/%lu/icons",          app.second.id);
        }

        // Regular handling for the remaining Steam games
        else if (app.second.store == app_record_s::Store::Steam)
        { 
          app.first.clear ();

          if (SKIF_Steam_UpdateAppState (&app.second))
            app.second._status.dwTimeLastChecked = SKIF_Util_timeGetTime1 ( ) + 333UL; // _RefreshInterval

          // Set uninstalled apps id to 0 so that they are filtered out
          if (! app.second._status.installed)
            app.second.id = 0;
        }

        // Only bother opening the application manifest
        //   and looking for a name if the client claims
        //     the app is installed.
        if (app.second._status.installed)
        {
          // Load any custom data
          if (! jsonMetaDB.is_discarded())
          {
            std::string item = (app.second.store == app_record_s::Store::Epic)  ? app.second.epic.name_app     :
                               (app.second.store == app_record_s::Store::Xbox)  ? app.second.xbox.package_name :
                                                                  std::to_string (app.second.id);

            try {
              auto& key = jsonMetaDB[app.second.store_utf8][item];

              std::string keyName         = "";
              int         keyCPU          =  0;
              int         keyAutoStop     =  0;
              int         keyHidden       = -1;   // will be 1 for hidden Steam games; 0 for the rest
              int         keyUses         =  0;
              std::string keyUsed         = "";
              std::string keyGroup        = "";
              int         keyPinned       = -1; // will be 1 for favorited Steam games; 0 for the rest

              // Special K defaults to pinned
              if (isSpecialK)
                keyPinned        = 99; // Needed as otherwise we'd overwrite it below

              if (key != nullptr && ! key.empty())
              {
                if (key.contains("Name"))
                  keyName        = key.at("Name");

                if (key.contains("CPU"))
                  keyCPU         = key.at("CPU");

                if (key.contains("AutoStop"))
                  keyAutoStop    = key.at("AutoStop");

                if (key.contains("Hidden"))
                  keyHidden      = key.at("Hidden");

                if (key.contains("Uses"))
                  keyUses        = key.at("Uses");

                if (key.contains("Used"))
                  keyUsed        = key.at("Used");

                if (key.contains("Pin"))
                  keyPinned      = key.at("Pin");

                if (key.contains("Category"))
                  keyGroup      = key.at("Category");
              }

              // Populate SKIF's custom variables with the values as well
              app.second.skif.name           = keyName;
              app.second.skif.cpu_type       = keyCPU;
              app.second.skif.auto_stop      = keyAutoStop;
              app.second.skif.hidden         = keyHidden;
              app.second.skif.uses           = keyUses;
              app.second.skif.used           = keyUsed;
              app.second.skif.category       = keyGroup;
              app.second.skif.pinned         = keyPinned;

              // For unrecognized categories, add them to SKIF's internal database as well
              if (! keyGroup.empty() && std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == keyGroup; }) == _registry.vecCategories.end())
              {
                SKIF_RegistrySettings::category_s  new_category{ keyGroup, false };
                _registry.vecCategories.push_back (new_category);
                newCategories = true;
              }

              // For newly arrived games, we populate the used parameter with the current time
              //   so they appear on top when sorting by Last Played, mirroring Steam a bit...
              if (app.second.skif.used.empty())
                app.second.skif.used         = szTime;

              // Human-readable time format (local time)
              time_t          last_played    = (time_t)strtol(app.second.skif.used.c_str(), NULL, 10);
              app.second.skif.used_formatted = SK_WideCharToUTF8 (SKIF_Util_timeGetTimeAsWStr (last_played));
                
              if ((app.second.store == app_record_s::Store::Steam ||
                   app.second.store == app_record_s::Store::GOG   ||
                   app.second.store == app_record_s::Store::Xbox) && key.contains("InstantPlay"))
              {
                app.second.skif.instant_play = key.at("InstantPlay");
              }

              // This also populates the JSON object with empty entries for new games
              key = {
                { "Name",     app.second.skif.name      },
                { "CPU",      app.second.skif.cpu_type  },
                { "AutoStop", app.second.skif.auto_stop },
                { "Hidden",   app.second.skif.hidden    },
                { "Uses",     app.second.skif.uses      },
                { "Used",     app.second.skif.used      },
                { "Category", app.second.skif.category  },
                { "Pin",      app.second.skif.pinned    }
              };

              if (app.second.store == app_record_s::Store::Steam ||
                  app.second.store == app_record_s::Store::GOG   ||
                  app.second.store == app_record_s::Store::Xbox)
                key += { "InstantPlay", app.second.skif.instant_play };
            }
            catch (const std::exception&)
            {
              PLOG_ERROR << "Error occurred when trying to parse " << item << " (" << (int)app.second.store << ") from " << file_metadata;
            }
          }

          if (! app.second.names.normal.empty ())
            app.first = app.second.names.normal;

          // Some games have an install state but no name,
          //   for those we have to consult the app manifest
          else if (app.second.store == app_record_s::Store::Steam && ! isSpecialK)
            app.first =
              SK_UseManifestToGetAppName (&app.second);

          // Corrupted app manifest / not known to Steam client; SKIP!
          if (app.first.empty ())
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has no name; ignoring!";

            app.second.id = 0;
            continue;
          }

          // Hide any... uhm... hidden... games...
          if (! _registry._LibraryHidden && (app.second.skif.hidden == 1 || (app.second.skif.hidden == -1 && app.second.steam.shared.hidden == 1)))
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has been hidden; ignoring!";

            app.second.id = 0;
            continue;
          }

          // Hide all non-hidden games if we are in "hidden mode", lol?
          // Except for Special K, obviously
          else if (_registry._LibraryHidden && ! isSpecialK && (app.second.skif.hidden == 0 || (app.second.skif.hidden == -1 && app.second.steam.shared.hidden == 0)))
          {
            app.second.id = 0;
            continue;
          }

          // Strip any remaining null terminators
          app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

          app.second.names.original = app.first;

          // Some games use weird Unicode character combos that ImGui can't handle,
          //  so let's replace those with the normal ones.

          // Replace RIGHT SINGLE QUOTATION MARK (Code: 2019 | UTF-8: E2 80 99)
          //  with a APOSTROPHE (Code: 0027 | UTF-8: 27)
          app.first = std::regex_replace(app.first, std::regex("\xE2\x80\x99"), "\x27");

          // Replace LATIN SMALL LETTER O (Code: 006F | UTF-8: 6F) and COMBINING DIAERESIS (Code: 0308 | UTF-8: CC 88)
          //  with a LATIN SMALL LETTER O WITH DIAERESIS (Code: 00F6 | UTF-8: C3 B6)
          app.first = std::regex_replace(app.first, std::regex("\x6F\xCC\x88"), "\xC3\xB6");

          // Remove COPYRIGHT SIGN (Code: 00A9 | UTF-8: C2 A9)
          app.first = std::regex_replace(app.first, std::regex("\xC2\xA9"), "");

          // Remove REGISTERED SIGN (Code: 00AE | UTF-8: C2 AE)
          app.first = std::regex_replace(app.first, std::regex("\xC2\xAE"), "");

          // Remove TRADE MARK SIGN (Code: 2122 | UTF-8: E2 84 A2)
          app.first = std::regex_replace(app.first, std::regex("\xE2\x84\xA2"), "");

          // Output the name change (use widechar to have them appear correct in the logs)
          if (app.second.names.original != app.first)
            PLOG_DEBUG << R"(Game title was changed: ")" << SK_UTF8ToWideChar(app.second.names.original.c_str()) << R"(" --> ")" << SK_UTF8ToWideChar(app.first.c_str()) << R"(")";

          SKIF_Util_TrimSpaces (app.first);

          // Strip any remaining null terminators
          //app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

          // Store the cleaned copy of the original
          app.second.names.clean = app.first;
          
          // Apply any custom name
          if (! app.second.skif.name.empty())
          {
            app.second.names.normal  = app.second.skif.name;
            app.first                = app.second.names.normal;
          }

          // Update ImGuiLabelID and ImGuiPushID
          app.second.ImGuiLabelID    = SKIF_Util_FormatStringRaw ("###%i-%i-selectable", (int)app.second.store, app.second.id); // ("%s###%i-%i", app.first.c_str(), (int)app.second.store, app.second.id)
          app.second.ImGuiPushID     = SKIF_Util_FormatStringRaw ("###%i-%i",            (int)app.second.store, app.second.id);

#if 0
          PLOG_DEBUG << "\nGame Titles: (" << app.second.id << ")"
                     << "\nOriginal : " << SK_UTF8ToWideChar(app.second.names.original)
                     << "\nCleaned  : " << SK_UTF8ToWideChar(app.second.names.clean)
                     << "\nNormal   : " << SK_UTF8ToWideChar(app.second.names.normal)
                     << "\nImGuiLID : " << SK_UTF8ToWideChar(app.second.ImGuiLabelID);
#endif
        }

        // Check if install folder exists (but not for Xbox games or Special K)
        if (app.second.store != app_record_s::Store::Xbox && ! isSpecialK)
        {
          // This populates install_dir for Steam games
          if (app.second.store == app_record_s::Store::Steam)
            SK_UseManifestToGetInstallDir (&app.second);
          
          if (! PathFileExists (app.second.install_dir.c_str()))
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has non-existent install folder; ignoring!";

            app.second.id = 0;
            continue;
          }

          // Preload active branch for Steam games
          if (app.second.store == app_record_s::Store::Steam)
          {
            app.second.steam.branch = SK_UseManifestToGetCurrentBranch (&app.second);

            if (! app.second.steam.branch.empty())
              PLOG_VERBOSE << "App ID " << app.second.id << " has active branch : " << app.second.steam.branch;
          }
        }

        if (app.second._status.installed && ! isSpecialK)
        {
          // Prepare for the keyboard hint / search/filter functionality
          InsertTrieKey (&app, &_data->labels);

          // Ensure the Profiles registry key is populated properly, but only write it once to ensure
          //   profile folders are not randomly renamed if a game gets renamed on the storefront
          if (ERROR_SUCCESS        == lsKey &&
              ERROR_FILE_NOT_FOUND == RegQueryValueExW (hKey, app.second.install_dir.c_str(), NULL, NULL, NULL, NULL))
          {
            std::wstring wsName = SK_UTF8ToWideChar(app.second.names.original);

            if (ERROR_SUCCESS != RegSetValueExW (hKey, app.second.install_dir.c_str(), 0, REG_SZ, (LPBYTE)wsName.data(), (DWORD)wsName.length() * sizeof(wchar_t)))
              PLOG_ERROR   << "Failed adding profile name (" << wsName << ") to registry value: " << app.second.install_dir;

            if (app.second.store == app_record_s::Store::Xbox)
            {
              if (ERROR_SUCCESS != RegSetValueExW (hKey, app.second.xbox.directory_app.c_str(), 0, REG_SZ, (LPBYTE)wsName.data(), (DWORD)wsName.length() * sizeof(wchar_t)))
                PLOG_ERROR << "Failed adding profile name (" << wsName << ") to registry value: " << app.second.xbox.directory_app;

              if (ERROR_SUCCESS != RegSetValueExW (hKey, app.second.xbox.directory_program_files.c_str(), 0, REG_SZ, (LPBYTE)wsName.data(), (DWORD)wsName.length() * sizeof(wchar_t)))
                PLOG_ERROR << "Failed adding profile name (" << wsName << ") to registry value: " << app.second.xbox.directory_program_files;
            }
          }
        }
      }

      if (ERROR_SUCCESS == lsKey)
        RegCloseKey (hKey);

      // Update the db.json file with any additions and whatnot
      if (! jsonMetaDB.is_discarded())
      {
        std::ofstream out_file(file_metadata);
        out_file << std::setw(2) << jsonMetaDB << std::endl;
        out_file.close();
      }

      // We have detected new categories!
      if (newCategories)
      {
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

      post = SKIF_Util_timeGetTime1 ( );
      games = games - 1; // Do not count Special K as a game
      PLOG_INFO << "Finished processing " << games << " detected games in " << (post - pre) << " ms.";

      SKIF_GamingCollection::SortApps (&_data->apps);

      //PLOG_INFO << "Apps were sorted!";

      PLOG_INFO << "Finished populating the library list.";

      PLOG_INFO_IF(pPatTexSRV.p == nullptr) << "Loading the embedded Patreon texture...";
      ImVec2 dontCare1, dontCare2;
      if (pPatTexSRV.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, pPatTexSRV,          L"patreon.png",         dontCare1, dontCare2);
      if (pSKLogoTexSRV.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, pSKLogoTexSRV,       L"sk_boxart.png",       dontCare1, dontCare2);
      if (pSKLogoTexSRV_small.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, pSKLogoTexSRV_small, L"sk_boxart_small.png", dontCare1, dontCare2);

      // Force a refresh when the game icons have finished being streamed
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

      PLOG_INFO << "Library refresh took " << (SKIF_Util_timeGetTime1 ( ) - start) << " ms.";

      PLOG_DEBUG << "SKIF_LibraryWorker thread stopped!";

      //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

      return 0;
    }, library_worker, 0x0, nullptr);

    bool threadCreated = (hWorkerThread != NULL);

    if (threadCreated)
    {
      library_worker->hWorker = hWorkerThread;
      library_worker->iWorker = 1;
    }
    else // Someting went wrong during thread creation, so free up the memory we allocated earlier
    {
      delete library_worker;
      library_worker = nullptr;
    }
  }

  else if (! PopulatedGames && library_worker != nullptr && library_worker->iWorker == 1 && WaitForSingleObject (library_worker->hWorker, 0) == WAIT_OBJECT_0)
  {

    struct IconCache {
      app_record_s::tex_registry_s tex_icon;
      app_record_s::Store store;
      AppId_t id;
    };

    std::vector<IconCache> icon_cache;

    // Clear up any unacknowledged icon workers
    for (auto& app : g_apps)
    {
      if (app.second.tex_icon.iWorker == 1)
      {
        if (WaitForSingleObject (app.second.tex_icon.hWorker, 200) == WAIT_OBJECT_0) // 200 second timeout (maybe change it?)
        {
          CloseHandle (app.second.tex_icon.hWorker);
          app.second.tex_icon.hWorker = NULL;
          app.second.tex_icon.iWorker = 2;
          activeIconWorkers--;
        }
      }
      
      // Cache any existing icon textures...
      if (app.second.tex_icon.texture.p != nullptr)
      {
        icon_cache.push_back ({
          app.second.tex_icon,
          app.second.store,
          app.second.id
        });

        //SKIF_ResourcesToFree.push(app.second.tex_icon.texture.p);
        //app.second.tex_icon.texture.p = nullptr;
      }
    }

    // Clear current data
    g_apps         = { };
    g_apptickets   = { };
    labels         = { };
    labelsFiltered = { };

    // Insert new data
    g_apps       = library_worker->apps;
    g_apptickets = library_worker->apptickets;
    labels       = library_worker->labels;

    // Move cached icons over
    for (auto& app : g_apps)
    {
      for (auto& icon : icon_cache)
      {
        if (icon.id    == app.second.id
         && icon.store == app.second.store)
        {
          app.second.tex_icon = icon.tex_icon; // Move it over
          app.second.tex_icon.iWorker = 2;
          icon.id = 0; // Mark it _not_ for release
          break;
        }
      }
    }

    // Push unused icons for release
    for (auto& icon : icon_cache)
    {
      if (icon.id == 0)
        continue; // Skip icons marked as 0
      
      SKIF_ResourcesToFree.push(icon.tex_icon.texture.p);
      icon.tex_icon.texture.p = nullptr;
    }

    fAlphaList = (_registry.bFadeCovers) ? 0.0f : 1.0f;

    frameLibraryRefreshed = ImGui::GetFrameCount ( );

    // Reset selection to Special K, but only if set to something else than -1
    if (selection.appid != 0)
      selection.reset();

    bool resortGames   = false;
    int tmpPinnedOnTop = 0;

    // Do other misc stuff
    for (auto& app : g_apps)
    {
      if (app.second.id == 0)
        continue;

      // Set to last selected if it can be found
      if (app.second.id       ==                      _registry.uiLastSelectedGame &&
          app.second.store    == (app_record_s::Store)_registry.uiLastSelectedStore)
      {
        PLOG_VERBOSE << "Selected app ID " << app.second.id << " from platform ID " << (int)app.second.store << ".";
        selection.appid        =    app.second.id;
        selection.store        =    app.second.store;
        selection.category     = (  app.second.skif.pinned > 50)
                               ?   "Favorites (pinned)" // Workaround to not expand Favorites tab on launch
                               :    GetEffectiveCategory (&app.second);
        search_selection.id    =    selection.appid;
        search_selection.store =    selection.store;
        search_selection.category = selection.category;
        update = true;
      }

      // Prefill all apps with the current version (solves a single frame flicker the first time a game is selected)
      app.second.specialk.injection.dll.version      = _inject.SKVer32;
      app.second.specialk.injection.dll.version_utf8 = SK_WideCharToUTF8 (app.second.specialk.injection.dll.version);

      // Apply the current filter
      app.second.filtered = (charFilter[0] != '\0' && (StrStrIA (app.first.c_str(), charFilter) == NULL &&                // Name
                                                       StrStrIA (app.second.skif.category.c_str(), charFilter) == NULL)); // Category

      // Count the number of pinned entries on top
      if (app.second.skif.pinned > 50)
      {
        // Only allow 5 pinned on top
        if (5 > tmpPinnedOnTop)
          tmpPinnedOnTop++;

        // Reduce all other to normal favorite status
        else
        {
          app.second.skif.pinned = 50;
          resortGames = true;
        }
      }
    }

    if (resortGames)
    {
      SKIF_GamingCollection::SortApps (&g_apps);
      sort_changed = true;
    }

    // Use a separate pass to count the actual figures used for the UI calculations later
    numRegular       = 0;
    numPinnedOnTop   = 0;

    for (auto& app : g_apps)
    {
      if (app.second.id == 0 || app.second.filtered)
        continue;

      InsertTrieKey (&app, &labelsFiltered);

      numRegular++;

      if (app.second.skif.pinned > 50)
        numPinnedOnTop++;
    }

    numRegular -= numPinnedOnTop;

    CloseHandle (library_worker->hWorker);
    library_worker->hWorker = NULL;
    library_worker->iWorker = 2;
    library_worker->labels  = Trie { };
    library_worker->apps.clear ();
    library_worker->apptickets.clear();

    delete library_worker;
    library_worker = nullptr;

    PopulatedGames = true;
    sort_changed   = true;

    PLOG_VERBOSE << "Swapped in the new library data!";
  }

  extern bool  coverFadeActive;
  static int   tmp_iDimCovers = _registry.iDimCovers;
  
  static
    app_record_s* pApp;

  pApp = nullptr;

  // Ensure pApp points to the current selected game
  // This should be the only place where pApp changes during the whole frame!
  for (auto& app : g_apps)
    if (app.second.id == selection.appid && app.second.store == selection.store)
      pApp = &app.second;

  // Default to primary launch config
  launchConfig = (pApp != nullptr && ! pApp->launch_configs.empty()) ? &pApp->launch_configs.begin()->second : nullptr;
  
  bool isSpecialK = (pApp != nullptr && pApp->id == SKIF_STEAM_APPID && pApp->store == app_record_s::Store::Steam);

  // Update the injection strategy for the selected game
  // Only do this once per frame to prevent data from "leaking" between pApp's
  // Also don't do it while the library worker is processing (to prevent stale data from surviving the refresh)
  // TODO: We still need to improve this by killing off any outdated workers and prevent them from swapping their
  //         results in. Maybe using an integer indicating the library refresh iteration we're currently on, that
  //           the worker can have a copy of for evaluation once it's finished?
  if (pApp != nullptr && library_worker == nullptr)
  {
    if (update)
    {
      if (  pApp->install_dir != selection.dir_watch._path)
        selection.dir_watch.reset ( );

      if (! pApp->install_dir.empty())
        selection.dir_watch.isSignaled (pApp->install_dir);
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
        for (auto& app : g_apps)
        {
          if (app.second.id    == worker.app.id &&
              app.second.store == worker.app.store)
          {
            // Backup the texture data (in particular any active worker data)
            app_record_s::tex_registry_s tex_icon  = app.second.tex_icon;
            app_record_s::tex_registry_s tex_cover = app.second.tex_cover;

            // Copy the results over
            app.second = worker.app;

            // Restore the texture data (and worker data)
            app.second.tex_icon  = tex_icon;
            app.second.tex_cover = tex_cover;

            app.second.loading = false;

            int cpu_post = (int)pApp->specialk.injection.injection.bitness;

            // If the CPU has changed, we need to update the metadata as well,
            //   but only if it differs from our cached value...
            if (cpu_post != worker.cpu_pre &&
                cpu_post != app.second.skif.cpu_type)
            {
              app.second.skif.cpu_type = cpu_post; // 0 = Common,  1 = x86, 2 = x64, 0xFFFF = Any

              // Update the db.json file with any new values
              JsonDB_UpdateApp (pApp, true);
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
        (_registry.bLibrarySteam && SKIF_Steam_HasActiveProcessChanged (&g_apps, &g_apptickets)) || // If Steam user signed in / out
        _inject.libCacheRefresh                       )) // If the global DLL files have been changed
    {
      _inject.libCacheRefresh = false;

      //PLOG_VERBOSE << "CPU PRE : " << cpu_pre;

      if (pApp->store == app_record_s::Store::Steam)
      {
        // Parse appinfo data for the current game
        // This must be done from the main thread since it also manipulates the g_apps array
        if (! pApp->processed)
          appinfo->getAppInfo ( pApp->id, &g_apps );
      }

      // Only run a worker if we're not dealing with Special K
      if (! isSpecialK)
      {
        pApp->loading = true;

        // Make a copy of pApp that we will use to update all data
        SKIF_Lib_GameWorkerThread_s* worker = &aGameWorkers[availableWorker];
        worker->app        = *pApp;
        worker->apptickets = g_apptickets;
        worker->cpu_pre    = (int)pApp->specialk.injection.injection.bitness;

        HANDLE hWorkerThread = (HANDLE)
        _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
        {
          SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_GameWorker");

          // Is this combo really appropriate for this thread?
          //SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
          //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

          //PLOG_DEBUG << "SKIF_GameWorker thread started!";

          SKIF_Lib_GameWorkerThread_s* _data = static_cast<SKIF_Lib_GameWorkerThread_s*>(var);

          UpdateInjectionStrategy (&_data->app, _data->apptickets);
      
          // Force a refresh when the game icons have finished being streamed
          PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

          //PLOG_DEBUG << "SKIF_GameWorker thread stopped!";

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

    // Load a new cover
    // Ensure we aren't already loading this cover
    if (lastCover.appid != pApp->id   ||
        lastCover.store != pApp->store)
    {
      // Special handling for the Special K logo when fading is disabled
      if (! _registry.bFadeCovers)
      {
        if (isSpecialK)
          fAlphaSK = 1.0f;
        else if (lastCover.appid == SKIF_STEAM_APPID &&
                 lastCover.store == app_record_s::Store::Steam)
          fAlphaSK = 0.0f;
      }

      loadCover       = true;
      lastCover.appid = pApp->id;
      lastCover.store = pApp->store;

      // Hide the current cover and set it up to be unloaded
      if (pTexSRV.p != nullptr)
      {
        // If there already is an old cover, we need to push it for release
        if (pTexSRV_old.p != nullptr)
        {
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV_old.p << " to be released";;
          SKIF_ResourcesToFree.push(pTexSRV_old.p);
          pTexSRV_old.p = nullptr;
        }

        // Set up the current one to be released
        vecCoverUv0_old = vecCoverUv0;
        vecCoverUv1_old = vecCoverUv1;
        pTexSRV_old.p   = pTexSRV.p;
        pTexSRV.p       = nullptr;
        fAlphaPrev      = (_registry.bFadeCovers) ? fAlpha   : 0.0f;
        fAlpha          = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
      }
    }

    // Release old cover after it has faded away, or instantly if we don't fade covers
    if (fAlphaPrev <= 0.0f)
    {
      if (pTexSRV_old.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV_old.p << " to be released";;
        SKIF_ResourcesToFree.push(pTexSRV_old.p);
        pTexSRV_old.p = nullptr;
      }
    }
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
  
  _registry.bHorizonMode = (ImGui::GetContentRegionAvail().x > 600.0f * SKIF_ImGui_GlobalDPIScale &&
                            ImGui::GetContentRegionAvail().y < 750.0f * SKIF_ImGui_GlobalDPIScale);

  // DPI-aware, calculate once per frame
  tab_scrollbar_width    = ImGui::GetCurrentWindowRead()->ScrollbarY ? ImGui::GetStyle().ScrollbarSize : 0.0f;
  lib_column2_width      = ((_registry.bHorizonMode) ? SKIF_ImGui_GlobalDPIScale * 376.0f : (SKIF_vecServiceMode.x)); // tab_scrollbar_width

  // All of these are DPI-aware
  ImVec2 sizeImage       = ImFloor ((_registry.bHorizonMode) ? ImVec2 (220.0f - tab_scrollbar_width, 330.0f - tab_scrollbar_width)     * SKIF_ImGui_GlobalDPIScale
                                                             : ImVec2 (600.0f, 900.0f)     * SKIF_ImGui_GlobalDPIScale);
  ImVec2 sizeList        = ImFloor ((_registry.bHorizonMode) ? (_registry.bUIBorders)
                                                             ? ImVec2 (lib_column2_width, 334.0f * SKIF_ImGui_GlobalDPIScale)  // Horizon + Borders
                                                             : ImVec2 (lib_column2_width, 332.0f * SKIF_ImGui_GlobalDPIScale)  // Horizon
                                                             : ImVec2 (lib_column2_width, 620.0f * SKIF_ImGui_GlobalDPIScale)); // Regular
  ImVec2 sizeDetails     = ImFloor ((_registry.bHorizonMode) ? (_registry.bUIBorders)
                                                             ? ImVec2 (lib_column2_width + 2.0f  * SKIF_ImGui_GlobalDPIScale, 332.0f * SKIF_ImGui_GlobalDPIScale)  // Horizon + Borders
                                                             : ImVec2 (lib_column2_width, 330.0f * SKIF_ImGui_GlobalDPIScale)  // Horizon
                                                             : ImVec2 (lib_column2_width, 280.0f * SKIF_ImGui_GlobalDPIScale)); // Regular

  // DPI-aware
  bool uiCoverVisible    = (_registry.bHorizonMode)
                                     ? (ImGui::GetContentRegionAvail().x > (sizeList.x + sizeDetails.x))
                                     : (ImGui::GetContentRegionAvail().x >  sizeImage.x); // When in regular mode
  bool uiDetailsVisible  = (_registry.bHorizonMode)
                                     ? true
                                     : ImGui::GetContentRegionAvail().y >= 750.0f * SKIF_ImGui_GlobalDPIScale; // When in regular mode

  if (! uiCoverVisible)
  {
    float newMaxWidth = ImGui::GetContentRegionAvail().x / ((_registry.bHorizonMode && uiDetailsVisible) ? 2.0f : 1.0f);
    lib_column2_width = newMaxWidth;
    sizeList.x        = newMaxWidth;
    sizeDetails.x     = newMaxWidth;
  }

  float arCover = sizeImage.x / sizeImage.y;

  // DPI-aware
  ImVec2 sizeCover    = ImGui::GetContentRegionAvail();
         sizeCover.x -= sizeList.x;
         sizeCover.x -= (_registry.bHorizonMode) ? sizeDetails.x : 0.0f;

  if (sizeCover.y < sizeImage.y)
  {
    sizeImage.y = sizeCover.y;
    sizeImage.x = sizeImage.y * arCover;
  }

  // DPI-aware
  sizeList.y = ImGui::GetContentRegionAvail().y;
  sizeList.y -= (_registry.bHorizonMode || ! uiDetailsVisible) ? 0.0f                             : sizeDetails.y;
  sizeList.y -= (_registry.bUIBorders                        ) ? 2.0f * SKIF_ImGui_GlobalDPIScale : 0.0f;

  // From now on ImGui UI calls starts being made...

#pragma region GameCover

  bool          isCoverHovered     = false;

  static int    queuePosGameCover  = 0;
  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelMissing[] = "             Missing cover :(\n"
                                      "Right click to set one manually";
  static char   cstrLabelGOGUser[] = "Please sign in to GOG Galaxy to\n"
                                      "allow the cover to be populated :)";
  char*         pcstrLabel         =  nullptr;
  ImVec2        vecPosCover        = ImVec2 (0, 0);

  // This is the content region of the current tab frame
  if (uiCoverVisible)
  {
    // Special handling for when a cover is being loaded in the background and selection changes from another game back to this one...
    if (tryingToSaveCover && coverRefreshAppId == pApp->id && coverRefreshStore == (int)pApp->store)
      pcstrLabel = cstrLabelLoading;
    
    // A new cover is meant to be loaded, so don't do anything for now...
    else if (loadCover)
    { }

    else if (tryingToLoadCover)
      pcstrLabel = cstrLabelLoading;
  
    else if (textureLoadQueueLength.load() == queuePosGameCover && pTexSRV.p == nullptr)
    {
      if (pApp != nullptr && pApp->id == SKIF_STEAM_APPID && pApp->store == app_record_s::Store::Steam)
      {
        // Special K selected -- do nothing
      }

      else {
        extern std::wstring GOGGalaxy_UserID;
        if (pApp != nullptr && pApp->store == app_record_s::Store::GOG && GOGGalaxy_UserID.empty())
          pcstrLabel = cstrLabelGOGUser;
        else
          pcstrLabel = cstrLabelMissing;
      }
    }

    float fGammaCorrectedTint = 
      ((! _registry._RendererHDREnabled && _registry.iSDRMode == 2) || 
        ( _registry._RendererHDREnabled && _registry.iHDRMode == 2))
          ? AdjustAlpha (fTint)
          : fTint;

    vecPosCover = ImGui::GetCursorPos ( );

    if (ImGui::BeginChild ("###SKIF_COVER", sizeCover, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
      ImVec2 vecPosCoverInner      = ImGui::GetCursorPos();
      ImVec2 vecContentRegionAvail = ImGui::GetContentRegionAvail();

      ImVec2 vecPosImage      = ImFloor (ImVec2 (
            (vecContentRegionAvail.x - sizeImage.x) / 2,
            (vecContentRegionAvail.y - sizeImage.y) / 2));

      ImVec2 sizeCoverFloored = ImFloor (ImVec2 (
             sizeImage.x,
             sizeImage.y));

      if (pcstrLabel != nullptr)
      {
        ImVec2 labelSize = ImGui::CalcTextSize (pcstrLabel);
        ImGui::SetCursorPos (ImFloor (ImVec2 (
            (vecContentRegionAvail.x - labelSize.x) / 2,
            (vecContentRegionAvail.y - labelSize.y) / 2)));
        ImGui::TextDisabled (pcstrLabel);
        ImGui::SetCursorPos (vecPosCoverInner);
      }

      // Display previous fading out cover
      if (pTexSRV_old.p != nullptr && fAlphaPrev > 0.0f)
      {
        ImGui::SetCursorPos  (vecPosImage);
        SKIF_ImGui_OptImage  (pTexSRV_old.p,
                                                          sizeCoverFloored,
                                                          vecCoverUv0_old, // Top Left coordinates
                                                          vecCoverUv1_old, // Bottom Right coordinates
                                        (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlphaPrev))  : ImVec4 (fTint, fTint, fTint, fAlphaPrev), // Alpha transparency
                                           ImVec4 (0.0f, 0.0f, 0.0f, 0.0f) // Never use a border
        );

        ImGui::SetCursorPos (vecPosCoverInner);
      }

      // Display game cover image
      ImGui::SetCursorPos  (vecPosImage);
      SKIF_ImGui_OptImage  (pTexSRV.p,
                                                        sizeCoverFloored,
                                                        vecCoverUv0, // Top Left coordinates
                                                        vecCoverUv1, // Bottom Right coordinates
                                      (_registry._StyleLightMode) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlpha))  : ImVec4 (fTint, fTint, fTint, fAlpha), // Alpha transparency (2024-01-01, removed fGammaCorrectedTint * fAlpha for the light style)
                                     ImVec4 (0.0f, 0.0f, 0.0f, 0.0f) // Never use a border
      );

      ImGui::SetCursorPos (vecPosCoverInner);

      if (isSpecialK ||
       (! isSpecialK && fAlphaSK > 0.0f))
      {
        ImGui::SetCursorPos (ImFloor (ImVec2 (
           (vecContentRegionAvail.x - sizeImage.x) / 2,
           (vecContentRegionAvail.y - sizeImage.y) / 2)));

        ImGui::Image (((! _registry.bHorizonMode) ?   pSKLogoTexSRV.p : pSKLogoTexSRV_small.p),
                                                        sizeCoverFloored,
                                                        ImVec2 (0.0f, 0.0f),                  // Top Left coordinates
                                                        ImVec2 (1.0f, 1.0f),                  // Bottom Right coordinates
                                                        ImVec4 (1.0f, 1.0f, 1.0f, fAlphaSK),  // Tint for Special K's logo
                                                        ImVec4 (0.0f, 0.0f, 0.0f, 0.0f)       // Border
        );
      }

      ImGui::EndChild ( );
    }

    isCoverHovered = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
      CoverMenu = PopupState_Open;

    //float fY =
    //ImGui::GetCursorPosY ( );

    // This sets the same line for the cover as the list + details
    ImGui::SameLine             (0.0f, 3.0f * SKIF_ImGui_GlobalDPIScale); // 0.0f, 0.0f
  }

#pragma endregion
  
  //float fZ =
  //ImGui::GetCursorPosX ( );

  // LIST + DETAILS START
  ImGui::BeginGroup   ( );

  // Top option: Filter icon + search field

  ImVec2 fTop1 = ImGui::GetCursorPos ( );

  ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleVar       (ImGuiStyleVar_ChildBorderSize, 0.0f);

  ImGui::SetCursorPosX   (
    ImGui::GetCursorPosX ( )          +
    7.0f * SKIF_ImGui_GlobalDPIScale
  );

  ImGui::SetCursorPosY   (
    ImGui::GetCursorPosY ( )          +
    3.0f * SKIF_ImGui_GlobalDPIScale
  );
  

  bool showClearBtn      = (charFilter[0] != '\0');
  // Mirrors what ImGui::ButtonEx() does to calculate the height of buttons
  ImVec2 fTopFilterSize  =                           ImGui::CalcTextSize (ICON_FA_FILTER);
  float fTopHeight       =                                                 fTopFilterSize.y           + ImGui::GetStyle().FramePadding.y * 2.0f;
  float fTopClearX       = (! showClearBtn ? 0.0f :  ImGui::CalcTextSize (ICON_FA_XMARK ).x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f);
  float fTopZoomX        =                           ImGui::CalcTextSize (ICON_FA_MAGNIFYING_GLASS).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
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

    for (auto& app : g_apps)
    {
      if (app.second.id == 0)
        continue;
      
      app.second.filtered = (charFilter[0] != '\0' && (StrStrIA (app.first.c_str(), charFilter) == NULL &&                // Name
                                                       StrStrIA (app.second.skif.category.c_str(), charFilter) == NULL)); // Category

      if (app.second.filtered)
        continue;

      InsertTrieKey (&app, &labelsFiltered);

      numRegular++;

      if (app.second.skif.pinned > 50)
        numPinnedOnTop++;
    }

    numRegular -= numPinnedOnTop;
  }

  //PLOG_VERBOSE << "Numbers: " << numRegular << " (regular) -- " << numPinnedOnTop << " (on top)";

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
    {
      strncpy (charFilter,    "\0", MAX_PATH);
      strncpy (charFilterTmp, "\0", MAX_PATH);
      
      numPinnedOnTop = 0;
      numRegular     = 0;

      for (auto& app : g_apps)
      {
        app.second.filtered = false;

        numRegular++;

        if (app.second.skif.pinned > 50)
          numPinnedOnTop++;
      }

      numRegular -= numPinnedOnTop;
    }

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

  ImVec2 fTop2 = ImGui::GetCursorPos ( );

  // End top options

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

  if (! _registry.bHorizonMode)
    flags_wnd |= ImGuiWindowFlags_NavFlattened;

  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));

  ImGui::BeginGroup          ( ); // Start GamesList

  ImVec2 fTop3          = ImGui::GetCursorPos ( );
  ImVec2 fTop3ScreenPos = ImGui::GetCursorScreenPos ( );

  float _ICON_HEIGHT_BASE  = (_registry._TouchDevice || _registry.bUILargeIcons) ? 32.0f : 24.0f;
//float _ICON_HEIGHT_SCALE = (_registry._TouchDevice ? 1.0f : (ImGui::GetTextLineHeightWithSpacing () / _ICON_HEIGHT_BASE));
//float _ICON_HEIGHT       = std::min (1.0f, std::max (0.1f, _ICON_HEIGHT_SCALE)) * _ICON_HEIGHT_BASE;
  float _ICON_HEIGHT       = _ICON_HEIGHT_BASE * SKIF_ImGui_GlobalDPIScale;
  float _ICON_HEIGHT_OnTop = _ICON_HEIGHT + ImGui::GetStyle().ItemSpacing.y;

  ImGui::BeginChild          ( "###GameListOnTop",
                                ImVec2 ( (sizeList.x - ImGui::GetStyle().WindowPadding.x / 2.0f),
                                  (! _registry.bHorizonMode && numPinnedOnTop > 0 && numRegular > 0)
                                  ? numPinnedOnTop * _ICON_HEIGHT_OnTop + ImGui::GetStyle().WindowPadding.y
                                  : sizeList.y - (ImGui::GetStyle().FramePadding.x - 2.0f * SKIF_ImGui_GlobalDPIScale) - (fTop3.y - fTop1.y)),
                                  flags_cld | (_registry.bUIBorders ? ImGuiChildFlags_Border : ImGuiChildFlags_None),
                                  flags_wnd | ((! _registry.bHorizonMode && numPinnedOnTop > 0 && numRegular > 0) ? (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) : ImGuiWindowFlags_None));

  // Start populating the list

  if (g_apps.empty())
    ImGui::Selectable      ("Loading games...###GamesCurrentlyLoading", false, ImGuiSelectableFlags_Disabled);

  std::string current_category = "";
  int         categories       = 0;
  int         pinned_top       = 0;
  bool        resetNumOnTop    = true;
  // TODO: Pinned on top has some large counting issues, e.g. disabling a platform or hiding games allows the user to bypass the 5 limit restriction

  static int  apply_header_state = 0;
  bool        new_header_state   = false;

  float maxWidth    = (ImGui::GetContentRegionMax().x - _ICON_HEIGHT - ImGui::GetStyle().ItemSpacing.x);

  bool categoryMenuOpened = false;

  // Populate the list of games with all recognized games
  for (auto& app : g_apps)
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second.id == 0)
      continue;

    // Count the number of pinned entries separately from filtered entries
    if (app.second.skif.pinned > 50)
      resetNumOnTop = false;

    // Check if there is an icon worker pending that we need to acknowledge the results for...
    //   ... *before* we filter out the game and skips the whole rest of the loop for this one!
    if (app.second.tex_icon.iWorker == 1 && WaitForSingleObject (app.second.tex_icon.hWorker, 0) == WAIT_OBJECT_0)
    {
      CloseHandle (app.second.tex_icon.hWorker);
      app.second.tex_icon.hWorker = NULL;
      app.second.tex_icon.iWorker = 2;
      activeIconWorkers--;
    }

    // Skips those filtered out by an active search field entry
    if (app.second.filtered)
      continue;

    // Separate always on top (>50) from regular pinned (1-50), but only in regular mode
    if (! _registry.bHorizonMode && app.second.skif.pinned > 50)
      pinned_top++;
    else if (pinned_top > 0)
    {
      if (! _registry.bUIBorders)
      {
        ImGui::SetCursorPosY (
          ImGui::GetCursorPosY ( )
          // - (fOffset / 2.0f)
           - 1.0f * SKIF_ImGui_GlobalDPIScale
        );

        ImGui::Separator ( );
      }

      ImGui::EndChild             ( );

      ImVec2 fTop4 = ImGui::GetCursorPos ( );

      ImGui::BeginChild           ( "###GameList",
                                     ImVec2 ( (sizeList.x -  ImGui::GetStyle().WindowPadding.x / 2.0f),
                                               sizeList.y - (ImGui::GetStyle().FramePadding.x - 2.0f * SKIF_ImGui_GlobalDPIScale) - (fTop4.y - fTop1.y)),
                                     flags_cld,
                                     flags_wnd );

      maxWidth    = (ImGui::GetContentRegionMax().x - _ICON_HEIGHT - ImGui::GetStyle().ItemSpacing.x);

      pinned_top = 0;
    }

    // If we have passed the pinned on top games, order the rest by group
    if (pinned_top == 0)
    {
      static bool category_opened    = false;

      // All favorited games are grouped as such
      std::string tmpCategory = GetEffectiveCategory (&app.second);

      if (tmpCategory != current_category)
      {
        auto it = std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == tmpCategory; });

        // Always expand a category if a filter is active or the selected game was changed
        if ((sort_changed && selection.category == tmpCategory) || (it != _registry.vecCategories.end() && it->expanded))
          ImGui::SetNextItemOpen (true);

        if (search_selection.category == tmpCategory)
          ImGui::SetNextItemOpen (true);

        if (apply_header_state > 0 && ImGui::GetFrameCount ( ) > apply_header_state && it != _registry.vecCategories.end())
          ImGui::SetNextItemOpen (it->expanded);

        if (! _registry._StyleLightMode)
        {
          ImGui::PushStyleColor (ImGuiCol_Header, ImGui::GetStyleColorVec4 (ImGuiCol_Header) * ImVec4 (0.7f, 0.7f, 0.7f, 1.0f));
          ImGui::PushStyleColor (ImGuiCol_Text,   ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
        }

        category_opened = ImGui::CollapsingHeader (tmpCategory.c_str(), (showClearBtn) ? ImGuiTreeNodeFlags_DefaultOpen : 0); // ImGuiTreeNodeFlags_DefaultOpen

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
          categoryMenuOpened = true;

          ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

          if (tmpCategory != "Games" && tmpCategory != "Favorites")
          {
            if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("Rename###PopupRename-%i", categories)))
            {
              static_category.Name   = tmpCategory;
              static_category.change = true;
              static_category.rename = true;
              ImGui::CloseCurrentPopup ( );
            }

            if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("Remove###PopupRemove-%i", categories)))
            {
              static_category.Name   = tmpCategory;
              static_category.change = true;
              static_category.remove = true;

              ImGui::CloseCurrentPopup ( );
            }

            ImGui::Separator ( );
          }

          if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("Expand all###PopupExpand-%i", categories)))
          {
            apply_header_state = ImGui::GetFrameCount ( );
            new_header_state = true;

            ImGui::CloseCurrentPopup ( );
          }

          if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("Collapse all###PopupCollapse-%i", categories)))
          {
            apply_header_state = ImGui::GetFrameCount ( );
            new_header_state = false;

            ImGui::CloseCurrentPopup ( );
          }

          if (ImGui::MenuItem ("Remember collapsible state###PopupCollapse-%i", "", &_registry.bRememberCategoryState))
            _registry.regKVRememberCategoryState.putData (_registry.bRememberCategoryState);

          ImGui::PopStyleColor ( );

          ImGui::EndPopup ( );
        }

        current_category = tmpCategory;
        categories++;
      }

      if (categories > 0 && ! category_opened)
        continue;
    }
    
    bool selected = (selection.appid == app.second.id &&
                     selection.store == app.second.store);
    bool change   = false;

    if (app.second.store == app_record_s::Store::Steam && steamRunning)
    {
      if (app.second._status.dwTimeDelayChecks != 0)
      {
        if (app.second._status.dwTimeDelayChecks < current_time)
          app.second._status.dwTimeDelayChecks = 0;
      }

      else
        app.second._status.refresh (&app.second);
    }

    //if (_registry._TouchDevice)
    //  ImGui::SetCursorPosY (ImGui::GetCursorPosY() + 15.0f);

    float fOriginalY =
      ImGui::GetCursorPosY ();


    // Start Icon + Selectable row

    ImGui::BeginGroup      ();
    ImGui::PushID          (app.second.ImGuiPushID.c_str());

    if (_registry.bFadeCovers)
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, fAlphaList);

    SKIF_ImGui_OptImage    (app.second.tex_icon.iWorker == 2 ? app.second.tex_icon.texture.p : nullptr,
                              ImVec2 ( _ICON_HEIGHT,
                                       _ICON_HEIGHT )
                            );

    change |=
      _HandleItemSelection (&app.second, true);

    ImGui::SameLine        ();

    ImVec4 _color =
      ( app.second._status.updating != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.6f, .6f, 1.f)) :
      ( app.second._status.running  != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.3f, 1.f, 1.f)) :
                    ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Game Title
    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
    SKIF_ImGui_SelectableVAligned (app.second.ImGuiLabelID.c_str(), app.second.names.normal.c_str(), &selected, ImGuiSelectableFlags_None, ImVec2(maxWidth, _ICON_HEIGHT));
    ImGui::PopStyleColor   (2);

    if (_registry.bFadeCovers)
      ImGui::PopStyleVar ( ); // -ImGuiStyleVar_Alpha

    static DWORD    timeClicked = 0;

    // Handle double click on a game row
    if ( ImGui::IsItemHovered ( ) && pApp != nullptr && pApp->id != SKIF_STEAM_APPID && ! pApp->_status.running && ! pApp->_status.updating )
    {
      if ( ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) &&
           timeClicked != 0 && (item_clicked.appid == pApp->id && item_clicked.store == pApp->store))
      {
        timeClicked       = 0;
        item_clicked.reset ( );
        launchGame = true;
      }
      
      else if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) )
      {
        timeClicked        = SKIF_Util_timeGetTime ( );
        item_clicked.appid = pApp->id;
        item_clicked.store = pApp->store;
      }

      else if (timeClicked + 500 < SKIF_Util_timeGetTime ( ))
      {
        // Reset after 500 ms
        timeClicked = 0;
        item_clicked.reset ( );
      }
    }

    // Show full title in tooltip if the title spans longer than the width of the Selectable row
    if (ImGui::IsItemHovered ( ) && ImGui::CalcTextSize  (app.second.names.normal.c_str()).x >= (maxWidth - ImGui::GetStyle().ItemSpacing.x))
      SKIF_ImGui_SetHoverTip (app.second.names.normal);

    // Handle search input
    if (search_selection.id    == app.second.id &&
        search_selection.store == app.second.store)
    {
      // Set focus on current row
      ImGui::ActivateItemByID (ImGui::GetID(app.second.ImGuiLabelID.c_str()));
      ImGui::SetFocusID       (ImGui::GetID(app.second.ImGuiLabelID.c_str()), ImGui::GetCurrentWindow());

      // Clear stuff
      selection.appid        = 0;
      selection.store        = app_record_s::Store::Unspecified;
      selection.category     = "Games";
      search_selection.id    = 0;
      search_selection.store = app_record_s::Store::Unspecified;
      search_selection.category = "";
      change                 = true;
    }

    change |=
      _HandleItemSelection (&app.second);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::PopID           ();
    ImGui::EndGroup        ();

    // End Icon + Selectable row


    if ( app.second.id    == selection.appid &&
         app.second.store == selection.store &&
                   sort_changed /* &&
        (! ImGui::IsItemVisible ()) */ )
    {
      sort_changed = false;
      selection.reset ( );

      // Special handling for Special K to reset the scroll pos
      if (isSpecialK)
      {
        selection.appid    = 0;
        selection.store    = app_record_s::Store::Unspecified;
        selection.category = "Games";
      }

      change = true;
    }

    if (change)
    {
      update = (selection.appid != app.second.id ||
                selection.store != app.second.store);

      selection.appid              =    app.second.id;
      selection.store              =    app.second.store;
      selection.category           = (  app.second.skif.pinned > 50)
                                   ?   "Favorites (pinned)" // Workaround to not expand Favorites tab on launch
                                   :    GetEffectiveCategory (&app.second);
      selected                     =    true;

      // Only update the last selected value if we're not in hidden view
      if (! _registry._LibraryHidden)
      {
        _registry.uiLastSelectedGame  =      selection.appid;
        _registry.uiLastSelectedStore = (int)selection.store;
      }

      if (update)
      {
        timeClicked = SKIF_Util_timeGetTime ( );
        item_clicked.appid = selection.appid;
        item_clicked.store = selection.store;

        app.second._status.invalidate ();

        if (! ImGui::IsMouseClicked (ImGuiMouseButton_Right))
        {
          // Activate the row of the current game
          ImGui::ActivateItemByID (ImGui::GetID (app.second.ImGuiLabelID.c_str()));

          if (! ImGui::IsItemVisible    (    ))
            ImGui::SetScrollHereY       (0.5f);
          
          //ImGui::SetKeyboardFocusHere (    ); // Disabled after ImGui update since this set the keyboard focus on the next item

          // This fixes ImGui not allowing the GameContextMenu to be opened on first search
          //   without an additional keyboard input
          ImGuiContext& g = *ImGui::GetCurrentContext();
          g.NavDisableHighlight = false;
        }

        //ImGui::SetFocusID(ImGui::GetID(app.first.c_str()), ImGui::GetCurrentWindow());
      }
    }

    if (selected)
    {
      static bool _horizon = _registry.bHorizonMode;

      // This allows the scroll to reset on DPI changes, to keep the selected item on-screen
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last ||
             _registry.bHorizonMode != _horizon)
      {
        ImGui::SetScrollHereY (0.5f);
        _horizon = _registry.bHorizonMode;
      }
    }

    if (ImGui::IsItemVisible ( ) && app.second.tex_icon.iWorker == 0 && activeIconWorkers < 8)
    {
      activeIconWorkers++;

      std::wstring load_str;
        
      if (app.second.id == SKIF_STEAM_APPID) // SKIF
        load_str = L"sk_icon.jpg";
      else  if (app.second.store == app_record_s::Store::Custom) // SKIF Custom
        load_str = L"icon";
      else  if (app.second.store == app_record_s::Store::Epic)  // Epic
        load_str = L"icon";
      else  if (app.second.store == app_record_s::Store::GOG)   // GOG
        load_str = app.second.install_dir + L"\\goggame-" + std::to_wstring(app.second.id) + L".ico";
      else if (app.second.store  == app_record_s::Store::Steam)  // Steam
        load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i_icon.jpg)", _path_cache.steam_install, app.second.id); //L"_icon.jpg"
      else if (app.second.store  == app_record_s::Store::Xbox)  // Xbox
        load_str = L"icon";

      struct thread_s {
        uint32_t                      appid;
        app_record_s::tex_registry_s* texture;
        app_record_s*                 app;
        std::wstring                  path;
      };
  
      thread_s* data = new thread_s;

      data->path    =  load_str;
      data->appid   =  app.second.id;
      data->texture = &app.second.tex_icon;
      data->app     = &app.second;

      // We're going to stream game icons asynchronously on this thread
      HANDLE hWorkerThread = (HANDLE)
      _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
      {
        SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibIconWorker");

        CoInitializeEx (nullptr, 0x0);

        thread_s* _data = static_cast<thread_s*>(var);

        ImVec2 dontCare1, dontCare2;

        LoadLibraryTexture ( LibraryTexture::Icon,
                                _data->appid,
                                  _data->texture->texture,
                                    _data->path,
                                      dontCare1,
                                        dontCare2,
                                          _data->app );
          
        delete _data;

        // Force a refresh when the game icons have finished being streamed
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

        return 0;
      }, data, 0x0, nullptr);
        
      bool threadCreated = (hWorkerThread != NULL);

      if (threadCreated)
      {
        PLOG_VERBOSE << "An icon worker was spawned successfully!";
        app.second.tex_icon.hWorker = hWorkerThread;
        app.second.tex_icon.iWorker = 1;
      }

      else // Someting went wrong during thread creation, so free up the memory we allocated earlier
      {
        PLOG_VERBOSE << "Something went wrong when spawning an icon worker thread...";

        delete data;
        app.second.tex_icon.iWorker = 2;
        activeIconWorkers--;
      }
    }
  }

  if (apply_header_state > 0)
  {
    if (ImGui::GetFrameCount() == apply_header_state)
    {
      for (auto& category : _registry.vecCategories)
        category.expanded = new_header_state;
    }

    else
    {
      apply_header_state = 0;
    }
  }

  if (resetNumOnTop)
    numPinnedOnTop = 0;

  if (! categoryMenuOpened)
    CategoryMenu = PopupState_Closed;

  // 'Add Game' to the bottom of the list if the status bar is disabled
  /*
  if (! _registry.bUIStatusBar)
  {
    float fOriginalY =
      ImGui::GetCursorPosY ( );

    ImGui::BeginGroup      ( );

    static bool btnHovered = false;
    ImGui::PushStyleColor (ImGuiCol_Header,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

    if (btnHovered)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    else
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));

    ImGui::SetCursorPosY   (fOriginalY + fOffset     + ( 1.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + ( 3.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::Text            (ICON_FA_SQUARE_PLUS);
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + (30.0f * SKIF_ImGui_GlobalDPIScale));

    if (ImGui::Selectable      ("Add Game"))
      AddGamePopup = PopupState_Open;

    btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

    ImGui::PopStyleColor(4);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::EndGroup        ();
  }
  */

  // Stop populating the list

  // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
  SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

  ImGui::EndChild        ( );
  ImGui::EndGroup        ( ); // End GamesList
  ImGui::PopStyleColor   ( );
  
#pragma endregion

  if (_registry.bHorizonMode)
  {
    //ImGui::SameLine ( );
    //ImGui::SetCursorPosX (ImGui::GetCursorPosX() - ((_registry.bUIBorders) ? 4.0f : 7.0f) * SKIF_ImGui_GlobalDPIScale);

    ImGui::SetCursorPos (ImVec2 (fTop1.x + sizeList.x - ImGui::GetStyle().WindowPadding.x / 2.0f +
                                           3.0f * SKIF_ImGui_GlobalDPIScale,
                                 fTop1.y));
  }

  else {
    ImGui::SetCursorPosY (fTop2.y + sizeList.y - (fTop3.y - fTop1.y) + ImGui::GetStyle().FrameBorderSize * 2.0f); // Technically 1 px off when not using borders
  }

  ImRect gamesList = ImRect (fTop3ScreenPos, ImVec2(fTop3ScreenPos.x + sizeList.x, ImGui::GetCursorScreenPos().y));

  // Open the Empty Space Menu
  //ImGuiContext& g = *ImGui::GetCurrentContext();
  //if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)

  if (EmptySpaceMenu == PopupState_Closed              &&
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
    ImGui::BeginChild (
      "###GameDetails",
        ImVec2 ((sizeDetails.x - ImGui::GetStyle().WindowPadding.x / 2.0f),
                 sizeDetails.y),
          ImGuiChildFlags_AlwaysUseWindowPadding | (_registry.bUIBorders ? ImGuiChildFlags_Border : ImGuiChildFlags_None),
          ImGuiWindowFlags_NoScrollbar       |
          ImGuiWindowFlags_NoScrollWithMouse |
          ImGuiWindowFlags_NavFlattened
    );
    ImGui::BeginGroup ();

    if (_registry.bFadeCovers)
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, fAlphaList);

    if (pApp != nullptr && ! isSpecialK)
      GetInjectionSummary (pApp);
    else
      _inject._GlobalInjectionCtl ( );

    if (_registry.bFadeCovers)
      ImGui::PopStyleVar ( ); // -ImGuiStyleVar_Alpha

    ImGui::EndGroup     (                  );
    ImGui::EndChild     (                  );
  }

#pragma endregion

  ImGui::EndGroup     (                  );
  // LIST + DETAILS STOP

#pragma region SpecialKPatreon

  // Special handling at the bottom of the cover for Special K
  if (uiCoverVisible                      && // Only when the cover is visible, and
      ! _registry.bHorizonMode            && //      when not using Horizon mode, and
    ((! isSpecialK && fAlphaSK > 0.0f)    || // either while Special K logo is fading out,
       (pApp        != nullptr            && // or its selected in the list
        pApp->id    == SKIF_STEAM_APPID   &&
        pApp->store == app_record_s::Store::Steam)))
  {
    ImGui::SetCursorPos  (ImVec2 (vecPosCover.x, ImFloor (sizeCover.y - 198.0f * SKIF_ImGui_GlobalDPIScale))); // 198.0f _for some reasom_?!
                          //fY - (204.0f * SKIF_ImGui_GlobalDPIScale) ));

    if (ImGui::BeginChild ("###SKIF_PATREON", ImVec2 (sizeCover.x, ImFloor (200.0f * SKIF_ImGui_GlobalDPIScale)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
      static bool hoveredPatButton  = false,
                  hoveredPatCredits = false;

      if (_registry.bFadeCovers)
        ImGui::PushStyleVar (ImGuiStyleVar_Alpha, fAlphaSK);

      // Set all button styling to transparent
      ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0, 0, 0, 0));
      ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0, 0, 0, 0));
      ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0, 0, 0, 0));

      // Remove borders, paddings, and spacing (some of these are required, but maybe not all, but I can't be bothered figuring it out...)
      SKIF_ImGui_PushDisabledSpacing ( );

      bool        clicked =
      ImGui::ImageButton   ("###PatreonLogo",
                           (ImTextureID)pPatTexSRV.p,
                            ImVec2 (200.0F * SKIF_ImGui_GlobalDPIScale,
                                    200.0F * SKIF_ImGui_GlobalDPIScale),
                            ImVec2 (  0.f,       0.f),
                            ImVec2 (  1.f,       1.f),
                            ImVec4 (  0,     0,     0,    0), // Use a transparent background
         hoveredPatButton ? ImVec4 (  1.0f,  1.0f,  1.0f, 1.00f)
                          : ImVec4 (  0.8f,  0.8f,  0.8f, 0.66f));

      // Restore borders, paddings, and spacing
      SKIF_ImGui_PopDisabledSpacing ( );

      // Restore the custom button styling
      ImGui::PopStyleColor (3);

      hoveredPatButton =
      ImGui::IsItemHovered ( );

      SKIF_ImGui_SetMouseCursorHand ();
      SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");
      //SKIF_ImGui_SetHoverTip  ("Click to help support the project");

      if (clicked)
        SKIF_Util_OpenURI (
          L"https://www.patreon.com/Kaldaien"
        );

      ImGui::SetCursorPos  (ImVec2 (ImGui::GetContentRegionAvail().x - (230.0f * SKIF_ImGui_GlobalDPIScale), 0.0f));

      ImGui::PushStyleColor     (ImGuiCol_ChildBg,        hoveredPatCredits ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
                                                                            : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(.8f, .8f, .8f, .66f));
      ImGui::BeginChild         ("###PatronsChild", ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale,
                                                            200.0f * SKIF_ImGui_GlobalDPIScale),
                                                        ImGuiChildFlags_AlwaysUseWindowPadding, // Never use a border
                                                        ImGuiWindowFlags_NoScrollbar); // ((pApp->tex_cover.isCustom) ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoBackground))

      ImGui::TextColored        (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption) * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f), "Special Kudos to our Patrons:");

      std::string patrons_ = SKIF_Updater::GetInstance ( ).GetPatrons ( );

      ImGui::Spacing            ( );
      ImGui::SameLine           ( );
      ImGui::Spacing            ( );
      ImGui::SameLine           ( );
    
      ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
      ImGui::PushStyleColor     (ImGuiCol_Text,           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4  (0.6f, 0.6f, 0.6f, 1.0f));
      ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImColor (0, 0, 0, 0).Value);
      ImGui::PushStyleColor     (ImGuiCol_ScrollbarBg,    ImColor (0, 0, 0, 0).Value);
      ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImColor (0, 0, 0, 0).Value);
      ImGui::InputTextMultiline ("###Patrons", patrons_.data (), patrons_.length (),
                      ImVec2 (205.0f * SKIF_ImGui_GlobalDPIScale,
                              160.0f * SKIF_ImGui_GlobalDPIScale),
                                      ImGuiInputTextFlags_ReadOnly );
      ImGui::PopStyleColor      (4);
      ImGui::PopStyleVar        (2);

      hoveredPatCredits =
      ImGui::IsItemActive       ( ) ||
      ImGui::IsItemHovered      ( );

      ImGui::EndChild           ( );
      ImGui::PopStyleColor      ( );

      hoveredPatCredits = hoveredPatCredits ||
      ImGui::IsItemHovered      ( );

      if (_registry.bFadeCovers)
        ImGui::PopStyleVar ( );

      ImGui::EndChild           ( );
    }
  }

#pragma endregion

  // Refresh running state of SKIF Custom, Epic, GOG, and Xbox titles
  SKIF_GamingCollection::RefreshRunningApps (&g_apps);

#pragma region ServiceMenu

  SKIF_ImGui_ServiceMenu ( );

#pragma endregion
  
#pragma region CoverMenu

  // Open the CoverMenu
  if (CoverMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("CoverMenu");
    CoverMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup ("CoverMenu", ImGuiWindowFlags_NoMove))
  {
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

    if (pApp != nullptr)
    {
      // Preparations

      bool resetVisible = ((pApp->id    != SKIF_STEAM_APPID            && // Ugly check to exclude the "Special K" entry from being set to true
                            pApp->store != app_record_s::Store::Custom) ||
                            pApp->tex_cover.isCustom                   ||
                            pApp->tex_cover.isManaged);
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize (  ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE          ).x, ImGui::GetTextLineHeight()));
      if (pApp->tex_cover.isCustom)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE_LEFT         ).x, ImGui::GetTextLineHeight()));
      else if (pApp->tex_cover.isManaged)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UP_RIGHT_FROM_SQUARE).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      if (ImGui::Selectable ("Change", false, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        HRESULT hr          =
          SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp;*.psd;*.bmp" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures);
          
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
          // If cancelled, do nothing
        }

        else if (SUCCEEDED(hr))
        {
          std::wstring filePath = std::wstring(pwszFilePath);
          if (SaveGameCover (pApp, filePath))
          {
            // This allows the "..." loading dots to be visible
            tryingToSaveCover = true;
            coverRefreshAppId = pApp->id;
            coverRefreshStore = (int)pApp->store;
      
            // This sets up the current one to be released
            vecCoverUv0_old = vecCoverUv0;
            vecCoverUv1_old = vecCoverUv1;
            pTexSRV_old.p   = pTexSRV.p;
            pTexSRV.p       = nullptr;
            fAlphaPrev      = (_registry.bFadeCovers) ? fAlpha   : 0.0f;
            fAlpha          = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }

      if (resetVisible)
      {
        if (ImGui::Selectable ((pApp->tex_cover.isCustom) ? ((pApp->store == app_record_s::Store::Custom) ? "Clear" : "Reset") : "Refresh", false, ImGuiSelectableFlags_SpanAllColumns | ((pApp->tex_cover.isCustom || pApp->tex_cover.isManaged) ? 0x0 : ImGuiSelectableFlags_Disabled)))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Custom)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->epic.name_app).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->xbox.package_name).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

          if (PathFileExists (targetPath.c_str()))
          {
            std::wstring fileName = (pApp->tex_cover.isCustom) ? L"cover" : L"cover-original";

            // Will fail on read-only marked files
            bool d1 = DeleteFile ((targetPath + fileName + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + fileName + L".jpg").c_str()),
                 d3 = false,
                 d4 = false;

            // For Xbox titles we also store a fallback cover that we must reset
            if (! pApp->tex_cover.isCustom && pApp->store == app_record_s::Store::Xbox)
            {
              fileName = L"cover-fallback.png";
              d3 = DeleteFile ((targetPath + fileName + L".png").c_str()),
              d4 = DeleteFile ((targetPath + fileName + L".jpg").c_str());
            }

            // If any file was removed
            if (d1 || d2 || d3 || d4)
            {
              update    = true;
              lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
            }
          }
        }
        else
        {
          if (pApp->tex_cover.isCustom || pApp->tex_cover.isManaged)
            SKIF_ImGui_SetMouseCursorHand ( );
          else
            SKIF_ImGui_SetHoverTip        ("Managed by the platform client.");
        }
      }

      ImGui::Separator  (  );

      if (ImGui::Selectable ("Open SteamGridDB", false, ImGuiSelectableFlags_SpanAllColumns))
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(pApp->ui.sgdbGrids).c_str());
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (pApp->ui.sgdbGrids);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->tex_cover.isCustom)
        ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE_LEFT
                              );
      else if (pApp->tex_cover.isManaged)
        ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE
                              );
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (128, 128, 128) : ImColor (128, 128, 128),
                  ICON_FA_ROTATE
                              );

      ImGui::Separator  (  );

      ImGui::TextColored (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_UP_RIGHT_FROM_SQUARE
                            );

    }

    ImGui::PopStyleColor   ( );
    ImGui::EndPopup   (  );
  }

#pragma endregion

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

#pragma region GamesList::IconMenu
  
  // Open the IconMenu
  if (IconMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("IconMenu");
    IconMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup ("IconMenu", ImGuiWindowFlags_NoMove))
  {
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

    if (pApp != nullptr)
    {
      // Preparations

      bool resetVisible = (pApp->id    != SKIF_STEAM_APPID            ||
                          (pApp->id    != SKIF_STEAM_APPID            && // Ugly check to exclude the "Special K" entry from being set to true
                           pApp->store != app_record_s::Store::Custom) ||
                           pApp->tex_icon.isCustom                    ||
                           pApp->tex_icon.isManaged);

      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE          ).x, ImGui::GetTextLineHeight()));
      if (pApp->tex_icon.isCustom)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE_LEFT         ).x, ImGui::GetTextLineHeight()));
      else if (pApp->tex_icon.isManaged)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UP_RIGHT_FROM_SQUARE).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      if (ImGui::Selectable ("Change", false, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        HRESULT hr          =
          SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Icons", L"*.exe;*.ico;*.png;*.jpg;*.jpeg" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures);
          
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
          // If cancelled, do nothing
        }

        else if (SUCCEEDED(hr))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = SKIF_Util_ToLowerW (std::filesystem::path(pwszFilePath).extension().wstring());

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Custom)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->epic.name_app).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->xbox.package_name).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

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
            
            ImVec2 dontCare1, dontCare2;

            // Reload the icon
            LoadLibraryTexture (LibraryTexture::Icon,
                                  pApp->id,
                                    pApp->tex_icon.texture,
                                      (pApp->store == app_record_s::Store::GOG)
                                      ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                      : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", _path_cache.steam_install, pApp->id), //L"_icon.jpg",
                                          dontCare1,
                                            dontCare2,
                                              pApp );
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }
      
      if (resetVisible)
      {
        if (ImGui::Selectable ((pApp->tex_icon.isCustom) ? "Reset" : "Refresh", false, ImGuiSelectableFlags_SpanAllColumns | ((pApp->tex_icon.isCustom || pApp->tex_icon.isManaged) ? 0x0 : ImGuiSelectableFlags_Disabled)))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Custom)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->epic.name_app).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->xbox.package_name).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

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
              ImVec2 dontCare1, dontCare2;

              // Reload the icon
              LoadLibraryTexture (LibraryTexture::Icon,
                                    pApp->id,
                                      pApp->tex_icon.texture,
                                        (pApp->store == app_record_s::Store::GOG)
                                        ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                        : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", _path_cache.steam_install, pApp->id), //L"_icon.jpg",
                                            dontCare1,
                                              dontCare2,
                                                pApp );
            }
          }
        }
        else
        {
          if (pApp->tex_icon.isCustom || pApp->tex_icon.isManaged)
            SKIF_ImGui_SetMouseCursorHand ( );
          else
            SKIF_ImGui_SetHoverTip        ("Managed by the platform client.");
        }
      }

      ImGui::Separator();

      if (ImGui::Selectable ("Open SteamGridDB", false, ImGuiSelectableFlags_SpanAllColumns))
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(pApp->ui.sgdbIcons).c_str());
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (pApp->ui.sgdbIcons);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->tex_icon.isCustom)
        ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE_LEFT
                              );

      else if (pApp->tex_icon.isManaged)
        ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE
                              );
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::TextColored (
          (_registry._StyleLightMode) ? ImColor (128, 128, 128) : ImColor (128, 128, 128),
                  ICON_FA_ROTATE
                              );

      ImGui::Separator   ( );

      ImGui::TextColored (
              ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_UP_RIGHT_FROM_SQUARE
                            );
    }

    ImGui::PopStyleColor   ( );
    ImGui::EndPopup      ( );
  }

#pragma endregion

#pragma region GamesList::EmptySpaceMenu
  
  // Open the Empty Space Menu
  if (EmptySpaceMenu == PopupState_Open)
    ImGui::OpenPopup    ("GameListEmptySpaceMenu");

  if (ImGui::BeginPopup   ("GameListEmptySpaceMenu", ImGuiWindowFlags_NoMove))
  {
    EmptySpaceMenu = PopupState_Opened;
    constexpr char spaces[] = { "\u0020\u0020\u0020\u0020" };

    ImGui::PushStyleColor (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));

     if (SKIF_ImGui_MenuItemEx2 ("Add Game", ICON_FA_SQUARE_PLUS, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)))
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
        SKIF_GamingCollection::SortApps (&g_apps);
        sort_changed = true;
      }

      if (ImGui::MenuItem ("Most played",  spaces, &bFrequently))
      {
        _registry.iLibrarySort = 1;

        bName       = false;
        bRecently   = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&g_apps);
        sort_changed = true;
      }

      SKIF_ImGui_SetHoverTip ("Sort by the number of launches of the game through this app.");

      if (ImGui::MenuItem ("Last played",  spaces, &bRecently  ))
      {
        _registry.iLibrarySort = 2;
        
        bName       = false;
        bFrequently = false;

        _registry.regKVLibrarySort.putData (_registry.iLibrarySort);
        SKIF_GamingCollection::SortApps (&g_apps);
        sort_changed = true;
      }

      ImGui::EndMenu ( );
    }

    ImGui::PopID ( );

    ImGui::PushID ("#Platforms");

    if (SKIF_ImGui_BeginMenuEx2 ("Platforms", ICON_FA_GEARS))
    {
      static bool* pbLibraryEpic   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEpic;
      static bool* pbLibraryGOG    = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryGOG;
      static bool* pbLibrarySteam  = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibrarySteam;
      static bool* pbLibraryXbox   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryXbox;
      static bool* pbLibraryCustom = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryCustom;

      if (library_worker != nullptr)
        SKIF_ImGui_PushDisableState ( );

      if (ImGui::MenuItem ("Epic",  spaces, pbLibraryEpic,   (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryEpic.putData  (_registry.bLibraryEpic);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("GOG",   spaces, pbLibraryGOG,    (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryGOG.putData   (_registry.bLibraryGOG);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Steam", spaces, pbLibrarySteam,  (! _registry._LibraryHidden)))
      {
        _registry.regKVLibrarySteam.putData (_registry.bLibrarySteam);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Xbox",  spaces, pbLibraryXbox,   (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryXbox.putData  (_registry.bLibraryXbox);
        RepopulateGames = true;
      }

      ImGui::Separator ( );

      if (ImGui::MenuItem ("Custom",spaces, pbLibraryCustom, (! _registry._LibraryHidden)))
      {
        _registry.regKVLibraryCustom.putData(_registry.bLibraryCustom);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Hidden games",spaces, &_registry._LibraryHidden))
      {
        pbLibraryEpic   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryEpic;
        pbLibraryGOG    = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryGOG;
        pbLibrarySteam  = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibrarySteam;
        pbLibraryXbox   = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryXbox;
        pbLibraryCustom = (_registry._LibraryHidden) ? &_registry._LibraryHidden : &_registry.bLibraryCustom;

        RepopulateGames = true;
      }

      if (library_worker != nullptr)
        SKIF_ImGui_PopDisableState ( );

      ImGui::EndMenu ( );
    }

    ImGui::PopID ( );

    ImGui::Separator      ( );

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

    if (SKIF_ImGui_MenuItemEx2 ("Refresh", ICON_FA_ROTATE_RIGHT, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)))
      RepopulateGames = true;

    ImGui::PopStyleColor  ( );
    ImGui::EndPopup       ( );
  }

  else
    EmptySpaceMenu = PopupState_Closed;

#pragma endregion
  
#pragma region GameLaunchLogic

  if ((launchGame || launchGameMenu || launchInstant) &&
       pApp != nullptr && launchConfig != nullptr)
  {
    LaunchGame         (pApp);

    launchConfig     = &pApp->launch_configs.begin()->second; // Reset to primary launch config
    launchGame       = false;
    launchGameMenu   = false;
    launchInstant    = false;
    launchWithoutSK  = false;
  }

#pragma endregion
  
#pragma region SKIF_LibCoverWorker
  
  if (uiCoverVisible && loadCover && PopulatedGames && ! (tryingToSaveCover && coverRefreshAppId == pApp->id && coverRefreshStore == (int)pApp->store))
  { // Load cover first after the window has been shown -- to fix one copy leaking of the cover 
    // 2023-03-24: Is this even needed any longer after fixing the double-loading that was going on?
    // 2023-03-25: Disabled HiddenFramesCannotSkipItems check to see if it's solved.
    // 2023-10-05: Disabled waiting for the icon thread as well
    loadCover = false;

    // Reset variables used to track whether we're still loading a game cover, or if we're missing one
    gameCoverLoading.store (true);
    tryingToLoadCover = true;
    queuePosGameCover = textureLoadQueueLength.load() + 1;

    // We're going to stream the cover in asynchronously on this thread
    HANDLE hWorkerThread = (HANDLE)
    _beginthreadex (nullptr, 0x0, [](void*) -> unsigned
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverWorker");

      CoInitializeEx (nullptr, 0x0);

      PLOG_DEBUG << "SKIF_LibCoverWorker thread started!";

      PLOG_INFO  << "Streaming game cover asynchronously...";

      if (pApp == nullptr)
      {
        PLOG_ERROR << "Aborting due to pApp being a nullptr!";
        return 0;
      }

      app_record_s* _pApp = pApp;

      int queuePos = getTextureLoadQueuePos();
      //PLOG_VERBOSE << "queuePos = " << queuePos;

      static ImVec2 _vecCoverUv0(vecCoverUv0);
      static ImVec2 _vecCoverUv1(vecCoverUv1);
      static CComPtr <ID3D11ShaderResourceView> _pTexSRV (pTexSRV.p);

      std::wstring load_str;

      // SKIF
      if (_pApp->id == SKIF_STEAM_APPID)
      {
        // No need to change the string in any way
      }

      // SKIF Custom
      else if (_pApp->store == app_record_s::Store::Custom)
      {
        load_str = L"cover";
      }

      // GOG
      else if (_pApp->store == app_record_s::Store::GOG)
      {
        load_str = L"*_glx_vertical_cover.webp";
      }

      // Epic
      else if (_pApp->store == app_record_s::Store::Epic)
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\cover-original.jpg)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(_pApp->epic.name_app).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_Epic_IdentifyAssetNew (_pApp->epic.catalog_namespace, _pApp->epic.catalog_item_id, _pApp->epic.name_app, _pApp->epic.name_display);
        }
        
        else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_Epic_IdentifyAssetNew (_pApp->epic.catalog_namespace, _pApp->epic.catalog_item_id, _pApp->epic.name_app, _pApp->epic.name_display);
            }
          }
        }
      }

      // Xbox
      else if (_pApp->store == app_record_s::Store::Xbox)
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\cover-original.png)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(_pApp->xbox.package_name).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_Xbox_IdentifyAssetNew (_pApp->xbox.package_name, _pApp->xbox.store_id);
        }
        
        else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_Xbox_IdentifyAssetNew (_pApp->xbox.package_name, _pApp->xbox.store_id);
            }
          }
        }
      }

      // Steam
      else if (_pApp->store == app_record_s::Store::Steam)
      {
        std::wstring load_str_2x (
          SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", _path_cache.specialk_userdata, _pApp->id)
        );

        std::error_code ec;
        // Create any missing directories
        if (! std::filesystem::exists (            load_str_2x, ec))
              std::filesystem::create_directories (load_str_2x, ec);

        load_str_2x += L"cover-original.jpg";
        load_str     = _path_cache.steam_install;
        load_str    += LR"(/appcache/librarycache/)" +
          std::to_wstring (_pApp->id)                +
                                  L"_library_600x900.jpg";

        // If horizon mode is being used, we prefer to load the 300x450 image!
        if (! _registry.bHorizonMode)
        {
          std::wstring load_str_final = load_str;

          // Get UNIX-style time
          time_t ltime;
          time (&ltime);

          // Steam typically uses one of two different CDNs:
          // * CloudFlare : https://cdn.cloudflare.steamstatic.com/steam/apps/2673660/library_600x900_2x.jpg
          // * Akamai     :        https://steamcdn-a.akamaihd.net/steam/apps/2673660/library_600x900_2x.jpg
          // Historically the Akamai CDN has been ever so slightly more reliable than the CloudFlare CDN.
          std::wstring url  = L"https://steamcdn-a.akamaihd.net/steam/apps/";
                       url += std::to_wstring (_pApp->id);
                       url += L"/library_600x900_2x.jpg";
                       url += L"?t=";
                       url += std::to_wstring (ltime); // Add UNIX-style timestamp to ensure we don't get anything cached

          // If 600x900 exists but 600x900_x2 cannot be found
          if (  PathFileExistsW (load_str.   c_str ()) &&
              ! PathFileExistsW (load_str_2x.c_str ()) )
          {
            // Load the metadata from 600x900, but only if low bandwidth mode is not enabled
            if ( ! _registry.bLowBandwidthMode &&
                  SUCCEEDED (
                  DirectX::GetMetadataFromWICFile (
                    load_str.c_str (),
                      DirectX::WIC_FLAGS_FILTER_POINT,
                        meta
                    )
                  )
                )
            {
              // If the image is in reality 300x450, which indicates a real cover,
              //   download the real 600x900 cover and store it in _x2
              if (meta.width  == 300 &&
                  meta.height == 450)
              {
                PLOG_DEBUG << "Downloading cover asset: " << url;

                SKIF_Util_GetWebResource (url, load_str_2x);
                load_str_final = load_str_2x;
              }
            }
          }

          // If 600x900_x2 exists, check the last modified time stamps
          else {
            WIN32_FILE_ATTRIBUTE_DATA faX1{}, faX2{};

            // ... but only if low bandwidth mode is disabled
            if (! _registry.bLowBandwidthMode &&
                GetFileAttributesEx (load_str   .c_str (), GetFileExInfoStandard, &faX1) &&
                GetFileAttributesEx (load_str_2x.c_str (), GetFileExInfoStandard, &faX2))
            {
              // If 600x900 has been edited after 600_900_x2,
              //   download new copy of the 600_900_x2 cover
              if (CompareFileTime (&faX1.ftLastWriteTime, &faX2.ftLastWriteTime) == 1)
              {
                DeleteFile (load_str_2x.c_str ());

                PLOG_DEBUG << "Downloading cover asset: " << url;
                SKIF_Util_GetWebResource (url, load_str_2x);
              }
            }
          
            // If 600x900_x2 exists now, load it
            if (PathFileExistsW (load_str_2x.c_str ()))
              load_str_final = load_str_2x;
          }

          load_str = load_str_final;
        }
      }
    
      LoadLibraryTexture ( LibraryTexture::Cover,
                              _pApp->id,
                                _pTexSRV,
                                  load_str,
                                    _vecCoverUv0,
                                      _vecCoverUv1,
                                        _pApp);

      PLOG_VERBOSE << "_pTexSRV = " << _pTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        PLOG_DEBUG << "Texture is live! Swapping it in.";
        vecCoverUv0 = _vecCoverUv0;
        vecCoverUv1 = _vecCoverUv1;
        pTexSRV     = _pTexSRV;

        // Indicate that we have stopped loading the cover
        gameCoverLoading.store (false);

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      }

      else if (_pTexSRV.p != nullptr)
      {
        PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(_pTexSRV.p);
        _pTexSRV.p = nullptr;
      }

      PLOG_INFO  << "Finished streaming game cover asynchronously...";
      PLOG_DEBUG << "SKIF_LibCoverWorker thread stopped!";

      return 0;
    }, nullptr, 0x0, nullptr);

    if (hWorkerThread != NULL)
      CloseHandle (hWorkerThread);
  }

#pragma endregion

#pragma region Popups::KeyboardSearch

  if (AddGamePopup    == PopupState_Closed &&
      ModifyGamePopup == PopupState_Closed &&
      PopupCategoryModify == PopupState_Closed &&
      RemoveGamePopup == PopupState_Closed &&
      ! io.KeyCtrl)
  {
    SearchAppsList ( );
  }

#pragma endregion

#pragma region Popups

  if (RemoveGamePopup == PopupState_Open)
  {
    ImGui::OpenPopup("###RemoveGamePopup");
    RemoveGamePopup = PopupState_Opened;
  }


  float fRemoveGamePopupWidth = 360.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fRemoveGamePopupWidth, 0.0f));
  ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

  if (ImGui::BeginPopupModal ("Remove Game###RemoveGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TreePush    ("RemoveGameTreePush");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        ("Do you want to remove this game from the app?");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Yes", vButtonSize))
    {
      if (SKIF_RemoveCustomAppID(selection.appid))
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

      RemoveGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("No", vButtonSize))
    {
      RemoveGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }
  else {
    RemoveGamePopup = PopupState_Closed;
  }


  if (AddGamePopup == PopupState_Open && ! SKIF_ImGui_IsAnyPopupOpen ( ))
  {
    ImGui::OpenPopup("###AddGamePopup");
    //AddGamePopup = PopupState_Opened; // Set as part of the BeginPopupModal() call below instead
  }

  float fAddGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fAddGamePopupWidth, 0.0f));
  ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

  if (ImGui::BeginPopupModal ("Add Game###AddGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    /*
      name          - String -- Title/Name
      exe           - String -- Full Path to executable
      launchOptions - String -- Cmd line args
      id            - Autogenerated
      installDir    - Autogenerated
      exeFileName   - Autogenerated
    */

    if (AddGamePopup == PopupState_Open)
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###AddGamePopup");
      if (window != nullptr && ! window->Appearing)
        AddGamePopup = PopupState_Opened;
    }

    // TODO: Go through and correct the buf_size of all ImGui::InputText to include the null terminator
    static char charName     [MAX_PATH + 2] = { },
                charPath     [MAX_PATH + 2] = { },
                charArgs     [    1024 + 2] = { };
    static bool error = false;

    auto _ProcessFilePath = [&](LPCWSTR pwszPath) -> bool
    {
      bool __error = false;

      std::filesystem::path path           = pwszPath; // Wide-string std::filesystem::path
      std::filesystem::path pathDiscard    = pwszPath; // Wide-string std::filesystem::path which will be discarded
      std::string           pathFullPath   = SK_WideCharToUTF8  (pathDiscard.wstring());
      std::wstring          pathExtension  = SKIF_Util_ToLowerW (pathDiscard.extension().wstring());
      std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

      if (pathExtension == L".lnk")
      {
        WCHAR wszTarget    [MAX_PATH + 2] = { };
        WCHAR wszArguments [MAX_PATH + 2] = { };

        SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH * sizeof (WCHAR));

        if (! PathFileExists (wszTarget))
        {
          __error = true;
          strncpy (charPath, "\0", MAX_PATH);
        }

        else {
          std::wstring productName = SKIF_Util_GetProductName (wszTarget);
          
          strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
          strncpy (charArgs, SK_WideCharToUTF8 (wszArguments).c_str(),               1024);
          strncpy (charName, (productName != L"")
                              ? SK_WideCharToUTF8 (productName).c_str()
                              : pathFilename.c_str(), MAX_PATH);
        }
      }

      else if (pathExtension == L".exe") {
        std::wstring productName = SKIF_Util_GetProductName (path.c_str());

        strncpy (charPath, pathFullPath.c_str(),    MAX_PATH);
        strncpy (charName, (productName != L"")
                            ? SK_WideCharToUTF8 (productName).c_str()
                            : pathFilename.c_str(), MAX_PATH);
      }

      else if (pathExtension == L".bat") {
        strncpy (charPath, pathFullPath.c_str(),    MAX_PATH);
      }

      else {
        __error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }

      return __error;
    };

    // Process drag-n-dropped executables / shortcuts
    if (! dragDroppedFilePathGame.empty())
    {
      error = _ProcessFilePath (dragDroppedFilePathGame.c_str());
      dragDroppedFilePathGame.clear();
    }

    ImGui::TreePush    ("AddGameTreePush");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      LPWSTR pwszFilePath = NULL;
      HRESULT hr          =
        SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe;*.bat" }, 1, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST);

      if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
      {
        // If cancelled, do nothing
      }

      else if (SUCCEEDED(hr))
      {
        error = _ProcessFilePath (pwszFilePath);
      }

      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    ImGui::SameLine    ( );

    float fAddGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fAddGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 1024, ImVec2(0,0), ImGuiInputTextFlags_None);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      SKIF_ImGui_PushDisableState ( );
    }

    if (ImGui::Button  ("Add Game", vButtonSize))
    {
      int newAppId = SKIF_AddCustomAppID (SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs));

      if (newAppId > 0)
      {
        // Attempt to extract the icon from the given executable straight away
        //std::wstring SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\icon-original.png)", _path_cache.specialk_userdata, newAppId);
        //SKIF_Util_SaveExtractExeIcon (SK_UTF8ToWideChar(charPath), SKIFCustomPath);

        _registry.uiLastSelectedGame  = newAppId;
        _registry.uiLastSelectedStore = (int)app_record_s::Store::Custom;
        _registry.regKVLastSelectedGame .putData (_registry.uiLastSelectedGame);
        _registry.regKVLastSelectedStore.putData (_registry.uiLastSelectedStore);
        RepopulateGames = true; // Rely on the RepopulateGames method instead
      }

      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 1024);

      // Unload any current cover
      if (pTexSRV.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(pTexSRV.p);
        pTexSRV.p = nullptr;
      }

      AddGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    if (disabled)
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 1024);

      AddGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }
  else {
   AddGamePopup = PopupState_Closed;
  }



  if (ModifyGamePopup == PopupState_Open)
    ImGui::OpenPopup ("###ModifyGamePopup");

  float fModifyGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fModifyGamePopupWidth, 0.0f));
  ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

  if (ImGui::BeginPopupModal ("Manage Game###ModifyGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char charName     [MAX_PATH + 2] = { },
                hintName     [MAX_PATH + 2] = { },
                charPath     [MAX_PATH + 2] = { },
                charArgs     [    1024 + 2] = { };
    static bool changed_name        = false;

    static bool error = false;
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
        cached_hidden          = (pApp->skif.hidden == 1 || (pApp->skif.hidden == -1 && pApp->steam.shared.hidden == 1));
        cached_hidden_load     = false;
      }

      if (cached_pinned_load)
      {
        cached_pinned          = (pApp->skif.pinned  > 0 || (pApp->skif.pinned == -1 && pApp->steam.shared.favorite == 1));
        cached_pinned_load     = false;
      }

      if (cached_auto_stop    == -1)
        cached_auto_stop       = pApp->skif.auto_stop;

      if (cached_instant_play == -1)
        cached_instant_play    = pApp->skif.instant_play;

      if (pApp->store == app_record_s::Store::Custom)
      {
        strncpy (charPath, pApp->launch_configs[0].getExecutableFullPathUTF8 ( ).c_str(), MAX_PATH);
        strncpy (charArgs, SK_WideCharToUTF8 (pApp->launch_configs[0].getLaunchOptions()).c_str(), 1024);
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
    if (pApp->store == app_record_s::Store::Custom)
    {
      static float fBtnX  = fInputX                                                              // ImGui::InputTextEx (center-aligned)
                          - ImGui::CalcItemSize(vButtonSize, ImGui::CalcTextSize("Browse...").x  // ImGui::Button
                          - ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f).x                     // ImGui::Button
                          - ImGui::GetStyle().ItemSpacing.x;                                     // ImGui::SameLine

      ImGui::SetCursorPosX (fBtnX);

      if (ImGui::Button  ("Browse...", vButtonSize))
      {
        LPWSTR pwszFilePath = NULL;
        HRESULT hr          =
          SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe;*.bat" }, 1, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST);
          
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
          std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

          if (pathExtension == L".lnk")
          {
            WCHAR wszTarget    [MAX_PATH + 2] = { };
            WCHAR wszArguments [    1024 + 2] = { };

            SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH * sizeof (WCHAR));

            if (! PathFileExists (wszTarget))
            {
              error = true;
              strncpy (charPath, "\0", MAX_PATH);
            }

            else {
              //std::wstring productName = SKIF_Util_GetProductName (wszTarget);
          
              strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
            }
          }

          else if (pathExtension == L".exe") {
            //std::wstring productName = SKIF_Util_GetProductName (path.c_str());

            strncpy (charPath, pathFullPath.c_str(),                                  MAX_PATH);
          }

          else if (pathExtension == L".bat") {
            strncpy (charPath, pathFullPath.c_str(),    MAX_PATH);
          }

          else {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }
        }

        else {
          error = true;
          strncpy (charPath, "\0", MAX_PATH);
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
      else {
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

    if (pApp->store == app_record_s::Store::Custom)
    {
      ImGui::SetCursorPosX (fInputX);

      ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 1024, vInputSize, ImGuiInputTextFlags_None);
      SKIF_ImGui_DisallowMouseDragMove ( );
      ImGui::SameLine    ( );
      ImGui::Text        ("Launch Options");

      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );

      if (charPath[0] == '\0' || std::isspace(charPath[0]))
        disabled = true;
    }

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    // Steam / GOG / Xbox supports Instant Play
    if (pApp->store == app_record_s::Store::Steam ||
        pApp->store == app_record_s::Store::GOG   ||
        pApp->store == app_record_s::Store::Xbox)
    {
      ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
      SKIF_ImGui_SetHoverTip ("Warning: This skips the regular platform launch process for the game,\n"
                              "including steps like the cloud saves synchronization that usually occurs.");
      ImGui::SameLine    ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "Use Instant Play:"
      );

      ImGui::TreePush        ("ManageGame_InstantPlay");
      ImGui::RadioButton     (
        SKIF_Util_FormatStringRaw ("Default (%s)", ((_registry.bInstantPlaySteam && pApp->store == app_record_s::Store::Steam) ||
                                             (_registry.bInstantPlayGOG   && pApp->store == app_record_s::Store::GOG))
                                            ? "Always" : "Never"), &cached_instant_play, 0);
      SKIF_ImGui_SetHoverTip ("The game will use the default behavior configured in the Settings tab.");
      ImGui::SameLine        ( );
      ImGui::RadioButton     ("Never",             &cached_instant_play, 2);
      SKIF_ImGui_SetHoverTip ("The game will never use instant play except\nwhen launched through the right click menu.");
      ImGui::SameLine        ( );
      ImGui::RadioButton     ("Always",            &cached_instant_play, 1);
      SKIF_ImGui_SetHoverTip ("The game will always use instant play except\nwhen launched through the right click menu.");
      ImGui::TreePop         ( );
        
      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );
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
    
    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

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

      if (cached_hidden != (pApp->skif.hidden == 1 || (pApp->skif.hidden == -1 && pApp->steam.shared.hidden == 1)))
      {
        repopulate            = true;
        pApp->skif.hidden     = (int)cached_hidden;
      }

      if (cached_pinned != (pApp->skif.pinned > 0 || (pApp->skif.pinned == -1 && pApp->steam.shared.favorite == 1)))
      {
        resort                = true;
        pApp->skif.pinned     = (int)cached_pinned;
      }

      // Custom games also updates the registry
      if (pApp->store == app_record_s::Store::Custom)
      {
        std::wstring wszPath = SK_UTF8ToWideChar(charPath);
        std::wstring wszArgs = SK_UTF8ToWideChar(charArgs);

        if (SKIF_ModifyCustomAppID (pApp, wszPath, wszArgs))
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
      JsonDB_UpdateApp (pApp, true);
        
      // Clear variables
      changed_name        = false;
      error               = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 1024);
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
        SKIF_GamingCollection::SortApps (&g_apps);
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
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 1024);
      cached_elevate_load = true;
      cached_hidden_load  = true;
      cached_auto_stop    = -1;
      cached_instant_play = -1;

      ModifyGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( ); // ModifyGameTreePush

    ImGui::PopStyleColor ( );

    ImGui::EndPopup    ( );
  }
  else {
    ModifyGamePopup = PopupState_Closed;
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
          // Go through the whole jsonMetaDB object (even entries that is not a part of g_apps)
          //   and update/clear out the entries where the current category that we are working with is set
          JsonDB_SetCategoryName (static_category.Name, static_category.newName);

          // Update g_apps
          for (auto& app : g_apps)
          {
            if (app.second.skif.category == static_category.Name)
              app.second.skif.category    = static_category.newName;
          }

          SKIF_GamingCollection::SortApps (&g_apps);

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

  if (! dragDroppedFilePath.empty())
  {
    PLOG_VERBOSE << "New drop was given: " << dragDroppedFilePath;

    const std::filesystem::path fsPath (dragDroppedFilePath.data());
    std::wstring ext        = SKIF_Util_ToLowerW(fsPath.extension().wstring());
    PLOG_VERBOSE << "    File extension: " << ext;

#if 0
    bool         isURL      = PathIsURL (dragDroppedFilePath.data());

    bool isImage =
      (ext == L".jpg"  ||
       ext == L".jpeg" ||
     //ext == L".jxr"  ||
       ext == L".png"  ||
       ext == L".webp" ||
     //ext == L".tga"  ||
       ext == L".bmp"  ||
       ext == L".psd"  );
     //ext == L".gif"  ||
     //ext == L".tif"  ||
     //ext == L".tiff" ||
     //ext == L".hdr"  ); // Radiance RGBE (.hdr)
#endif

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

    // Images + URLs
    else if (SaveGameCover (pApp, dragDroppedFilePath))
    {
      // This allows the "..." loading dots to be visible
      tryingToSaveCover = true;
      coverRefreshAppId = pApp->id;
      coverRefreshStore = (int)pApp->store;
      
      // This sets up the current one to be released
      vecCoverUv0_old = vecCoverUv0;
      vecCoverUv1_old = vecCoverUv1;
      pTexSRV_old.p   = pTexSRV.p;
      pTexSRV.p       = nullptr;
      fAlphaPrev      = (_registry.bFadeCovers) ? fAlpha   : 0.0f;
      fAlpha          = (_registry.bFadeCovers) ?   0.0f   : 1.0f;

      // A child thread will set refreshCover once done
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
    // Fade in the new cover
    if (fAlpha < 1.0f && pTexSRV.p != nullptr)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlpha += 0.05f;
        incTick = true;
      }

      coverFadeActive = true;
    }

    // Fade out the old one
    if (fAlphaPrev > 0.0f && pTexSRV_old.p != nullptr)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaPrev -= 0.05f;
        incTick     = true;
      }

      coverFadeActive = true;
    }

    // Fade in the SK logo
    if (isSpecialK && fAlphaSK < 1.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaSK += 0.05f;
        incTick   = true;
      }

      coverFadeActive = true;
    }

    // Fade out the SK logo
    if (! isSpecialK && fAlphaSK > 0.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaSK -= 0.05f;
        incTick   = true;
      }

      coverFadeActive = true;
    }

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

  if (coverRefresh)
  {
    coverRefresh      = false;

    if (lastCover.appid == coverRefreshAppId &&
        lastCover.store == (app_record_s::Store) coverRefreshStore)
    {
      update       = true;
      lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
    }

    coverRefreshAppId = 0;
    coverRefreshStore = 0;
  }

  extern uint32_t SelectNewSKIFGame;

  if (SelectNewSKIFGame > 0)
  {
    // Change selection to the new game
    selection.appid    = SelectNewSKIFGame;
    selection.store    = app_record_s::Store::Custom;
    selection.category = "Games";

    update = true;

    SelectNewSKIFGame = 0;
  }

  // If we have changed mode, we need to reload the cover to ensure the proper resolution of it
  static bool lastHorizonMode = _registry.bHorizonMode;
  if (lastHorizonMode != _registry.bHorizonMode)
  {
    lastHorizonMode = _registry.bHorizonMode;

    update    = true;
    lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
  }

  // In case of a device reset, unload all currently loaded textures
  if (invalidatedDevice == 1)
  {   invalidatedDevice  = 2;

    if (pTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pTexSRV.p);
      pTexSRV.p = nullptr;
    }

    if (pTexSRV_old.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pTexSRV_old.p);
      pTexSRV_old.p = nullptr;
    }

    if (pPatTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pPatTexSRV.p);
      pPatTexSRV.p = nullptr;
    }

    if (pSKLogoTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pSKLogoTexSRV.p);
      pSKLogoTexSRV.p = nullptr;
    }

    if (pSKLogoTexSRV_small.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pSKLogoTexSRV_small.p);
      pSKLogoTexSRV_small.p = nullptr;
    }

    for (auto& app : g_apps)
    {
      if (app.second.tex_icon.texture.p != nullptr)
      {
        SKIF_ResourcesToFree.push(app.second.tex_icon.texture.p);
        app.second.tex_icon.texture.p = nullptr;
      }
    }

    // Trigger a refresh of the list of games, which will reload all icons and the Patreon texture
    RepopulateGames = true;
    // Trigger a refresh of the cover
    loadCover = true;
  }
  
  if (_registry.bLibrarySteam)
  {
    if (steamRunning)
      steamFallback = false;
    
    else if (! steamFallback && appinfo != nullptr)
    {
      SK_RunOnce (PLOG_DEBUG << "[AppInfo Processing] Started processing games...");

      bool fallbackAvailable = true;

      for (auto& app : g_apps)
      {
        if (app.second.store != app_record_s::Store::Steam)
          continue;

        if (app.second.id == 0)
          continue;

        if (app.second.id == SKIF_STEAM_APPID)
          continue;

        if (app.second.processed)
          continue;
        
        //PLOG_DEBUG << "[AppInfo Processing] " << "[" << ImGui::GetFrameCount ( ) << "] Processing " << app.second.id << "...";
        appinfo->getAppInfo ( app.second.id, &g_apps );

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

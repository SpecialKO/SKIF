#include <regex>
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
#include <memory>
#include <map>
#include <unordered_map>
#include <thread>
#include <functional>
#include <algorithm>
#include <Shlwapi.h>
#include <process.h>
#include <ctime>
#include <iomanip>

#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include "stores/Steam/apps_ignore.h"
#include <utility/injection.h>
#include <utility/fsutil.h>
#include <stores/GOG/gog_library.h>
#include <stores/epic/epic_library.h>
#include <stores/EA/ea_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

// Registry Settings
#include <utility/registry.h>
#include <stores/Steam/steam_library.h>

// Stuff
#include <utility/skif_imgui.h>

CONDITION_VARIABLE LibRefreshPaused = { };

#pragma region Trie Keyboard Hint Search

// Iterative function to insert a key in the Trie
void
Trie::insert (const std::string& key)
{
  // Characters outside 0-127 have no node (char is signed: they would index outside the node)
  for (const char c : key)
    if (static_cast <unsigned char> (c) >= CHAR_SIZE)
      return;

  // start from root node
  Trie* curr = this;
  for (const char c : key)
  {
    auto& next = curr->character [static_cast <unsigned char> (c)];

    // create a new node if path doesn't exists
    if (next == nullptr)
        next = std::make_unique <Trie> ( );

    // go to next node
    curr = next.get ( );
  }

  // mark current node as leaf
  curr->isLeaf = true;
}

// Iterative function to search a key in Trie. It returns true
// if the key is found in the Trie, else it returns false
bool
Trie::search (const std::string& key) const
{
  const Trie* curr = this;
  for (const char c : key)
  {
    if (static_cast <unsigned char> (c) >= CHAR_SIZE)
      return false;

    // go to next node
    curr = curr->character [static_cast <unsigned char> (c)].get ( );

    // if string is invalid (reached end of path in Trie)
    if (curr == nullptr)
      return false;
  }

  // if current node is a leaf and we have reached the
  // end of the string, return true
  return curr->isLeaf;
}

void
InsertTrieKey (app_entry_t* app, Trie* labels)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  std::string all_upper = SKIF_Util_ToUpper (app->first),
              all_upper_alnum;

  for (const char c : app->first)
  {
    if (! ( isalnum (static_cast <unsigned char> (c)) || isspace (static_cast <unsigned char> (c)) )) // Negative values are undefined for these
      continue;

    all_upper_alnum += (char)toupper (static_cast <unsigned char> (c));
  }

  size_t stripped = 0;

  if (_registry.bLibraryIgnoreArticles)
  {
    static const
      std::string toSkip [] =
      {
        std::string ("A "),
        std::string ("AN "),
        std::string ("THE ")
      };

    for ( auto& skip_ : toSkip )
    {
      if (all_upper_alnum.find (skip_) == 0)
      {
        all_upper_alnum =
          all_upper_alnum.substr (
            skip_.length ()
          );

        stripped = skip_.length ();
        break;
      }
    }
  }

  std::string trie_builder;

  for ( const char c : all_upper_alnum)
  {
    trie_builder += c;

    labels->insert (trie_builder);
  }

  app->second->names.normal          = app->first;
  app->second->names.all_upper       = all_upper;
  app->second->names.all_upper_alnum = all_upper_alnum;
  app->second->names.pre_stripped    = stripped;
}

#pragma endregion



// This sorts the app vector
void
SKIF_GamingCollection::SortApps (app_list_t *apps)
{
  // Not needed for the library worker's own list (see tl_isLibraryWorker)
  std::unique_lock <std::recursive_mutex> app_lock (SKIF_GamingCollection::Get().GetMutex(), std::defer_lock);

  if (! tl_isLibraryWorker)
    app_lock.lock ( );

  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // The base sort is by name
  std::stable_sort ( apps->begin (),
                     apps->end   (),
    []( const app_entry_t& a,
        const app_entry_t& b ) -> int
    {
      return a.second->names.all_upper_alnum.compare(
             b.second->names.all_upper_alnum
      ) < 0;
    }
  );

  // Then we apply any overarching custom sort
  switch (_registry.iLibrarySort)
  {

  case 1: // Sorting by used count
    PLOG_VERBOSE << "Sorting by used count...";
    std::stable_sort ( apps->begin (),
                       apps->end   (),
      []( const app_entry_t& a,
          const app_entry_t& b ) -> int
      {
        return a.second->skif.uses >
               b.second->skif.uses;
      }
    );
    break;

  case 2: // Sorting by last used
    PLOG_VERBOSE << "Sorting by last used...";
    std::stable_sort ( apps->begin (),
                       apps->end   (),
      []( const app_entry_t& a,
          const app_entry_t& b ) -> int
      {
        return a.second->skif.used.compare(
               b.second->skif.used
        ) > 0;
      }
    );
    break;
  }

  // Now we sort by the pinned state
  std::stable_sort ( apps->begin (),
                     apps->end   (),
    []( const app_entry_t& a,
        const app_entry_t& b ) -> int
    {
      // Use the highest value between SKIF's pinned value, or 0 / Steam's pinned value if SKIF's is unset
      return std::max (a.second->skif.pinned, (a.second->skif.pinned == -1) ? a.second->GetClientFavorite ( ) : 0) >
             std::max (b.second->skif.pinned, (b.second->skif.pinned == -1) ? b.second->GetClientFavorite ( ) : 0);
    }
  );

  // We need an iterator at the unpinned entries to sort the rest separately
  auto   it  = apps->begin ();
  while (it != apps->end   ())
  {
    auto& item = *it;
    if (std::max (item.second->skif.pinned, (item.second->skif.pinned == -1) ? item.second->GetClientFavorite ( ) : 0) == 0)
      break;

    it++;
  }

  // Then sort unpinned entires by category
  std::stable_sort ( it,
                     apps->end   (),
    []( const app_entry_t& a,
        const app_entry_t& b ) -> int
    {
      return a.second->skif.category.compare(
             b.second->skif.category) < 0;
    }
  );

  // And move all uncategorized entries last
  std::stable_partition ( it,
                          apps->end   (),
    []( const app_entry_t& a ) -> bool
    {
      return ! a.second->skif.category.empty();
    }
  );
}




#pragma region RefreshRunningApps

void
SKIF_GamingCollection::RefreshRunningApps (app_list_t *apps, bool forced)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  static DWORD lastGameRefresh = 0;
  static std::wstring exeSteam = L"steam.exe";


  DWORD        current_time = SKIF_Util_timeGetTime ( );
  static DWORD last_checked = 0;

  bool any_running      = false;
  bool focused          = SKIF_ImGui_IsFocused ();
  int  focus_multiplier = focused ? 1 : 10;

  // Runs on SKIF_LibRefreshWorker: ImGui is not thread-safe, so the mouse state comes from s_bAnyMouseDownUI (set by the main thread)
  if (forced || (current_time > lastGameRefresh + 666 * focus_multiplier && current_time > last_checked + 333UL * focus_multiplier && (! s_bAnyMouseDownUI.load ( ))))
  {
    // The main thread must hold this lock as well whenever it changes the app list or an app's launch configs
    //   (e.g. LoadStoreDetails ( ), which rebuilds launch_configs), as the loop below reads them
    std::scoped_lock app_lock (SKIF_GamingCollection::Get().GetMutex());

    if (! forced)
      last_checked = current_time;

    bool new_steamRunning = false;

    // Stage the current state, it will be pushed to the actual state at the end of the function.
    //
    //   This avoids issues with the UI thread reading the state while we are in the middle of refreshing.
    for (auto& app : *apps)
    {
      if (app.second->_status.running)
        any_running = true;

      app.second->_staging.running_pid = app.second->_status.running_pid;
      app.second->_staging.running     = app.second->_status.running;

      if (app.second->_status.dwTimeDelayChecks > current_time && (! forced))
        continue;

      app.second->_staging.running_pid = 0;

      // Some store clients keep their own running state (Steam, unless in process fallback without the client)
      if (app.second->KeepsRunningStateDuringScan ( ))
        continue;

      app.second->_staging.running = false;
    }

    PROCESSENTRY32W none = { },
                    pe32 = { };

    SK_AutoHandle hProcessSnap (
      CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
    );

    if ((intptr_t)hProcessSnap.m_h > 0)
    {
      pe32.dwSize = sizeof (PROCESSENTRY32W);
      std::wstring exeFileLast, exeFileNew;

      if (Process32FirstW (hProcessSnap, &pe32))
      {
        do
        {
          SetLastError (NO_ERROR);
          CHandle hProcess (OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID));
          // Use PROCESS_QUERY_LIMITED_INFORMATION since that allows us to retrieve exit code/full process name for elevated processes

          if (hProcess == nullptr)
            continue;

          exeFileNew = pe32.szExeFile;

          // Skip duplicate processes
          // NOTE: Potential bug is that Epic, GOG and SKIF Custom games with two running and identically named executables (launcher + game) may not be detected as such
          if (exeFileLast == exeFileNew)
            continue;

          exeFileLast = exeFileNew;

          // Recognize that the Steam client is running
          if (_wcsnicmp (exeFileLast.c_str(), exeSteam.c_str(), exeSteam.length()) == 0)
          {
            new_steamRunning = true;
            continue;
          }

          bool accessDenied =
            GetLastError ( ) == ERROR_ACCESS_DENIED;

          // Get exit code to filter out zombie processes
          DWORD dwExitCode = 0;
          GetExitCodeProcess (hProcess, &dwExitCode);

          WCHAR szExePath     [MAX_PATH + 2] = { };
          DWORD szExePathLen = MAX_PATH + 2; // Specifies the size of the lpExeName buffer, in characters.

          if (! accessDenied)
          {
            // If the process is not active any longer, skip it (terminated or zombie process)
            if (dwExitCode != STILL_ACTIVE)
              continue;

            // See if we can retrieve the full path of the executable
            if (! QueryFullProcessImageName (hProcess, 0, szExePath, &szExePathLen))
              szExePathLen = 0;
          }

          for (auto& app : *apps)
          {
            if (app.second->_status.dwTimeDelayChecks > current_time && (! forced))
              continue;

            if (! app.second->launch_configs.contains (0))
              continue;

            // Store-specific matching (Xbox: process name; Steam: PID only while the client owns the running state)
            app_record_s::ProcessMatch match =
              app.second->MatchProcess (pe32.szExeFile, szExePath, szExePathLen);

            if (match == app_record_s::ProcessMatch::None)
              continue;

            app.second->_staging.running_pid = pe32.th32ProcessID;

            if (match == app_record_s::ProcessMatch::PidOnly)
              continue;

            app.second->_staging.running     = true;
            break;
          }

          if (! _registry.bWarningRTSS        &&
              ! SKIF_ImGui_IsAnyPopupOpen ( ) &&
              ! wcscmp (pe32.szExeFile, L"RTSS.exe"))
          {
            _registry.bWarningRTSS = true;
            _registry.regKVWarningRTSS.putData (_registry.bWarningRTSS);

            constexpr char* error_title =
              "One-time warning about RTSS.exe";
            constexpr char* error_label =
              "RivaTuner Statistics Server (RTSS) occasionally conflicts with Special K.\n"
              "Try closing it down if Special K does not behave as expected, or enable\n"
              "the option 'Use Microsoft Detours API hooking' in the settings of RTSS.\n"
              "\n"
              "If you use MSI Afterburner, try closing it as well as otherwise it will\n"
              "automatically restart RTSS silently in the background.\n"
              "\n"
              "This warning will not appear again.";

            SKIF_ImGui_InfoMessage (error_title, error_label);
          }

        } while (Process32NextW (hProcessSnap, &pe32));
      }
    }

    steamRunning = new_steamRunning;

    if (! forced)
      lastGameRefresh = current_time;
  }


  // Instant Play monitoring...

  for (auto& monitored_app : SKIF_GamingCollection::Get ( ).GetInstantPlayProcesses ( ))
  {
    if (monitored_app.id != 0)
    {
      HANDLE hProcess      = monitored_app.hProcess.load();
      HANDLE hWorkerThread = monitored_app.hWorkerThread.load();
      int    iReturnCode   = monitored_app.iReturnCode.load();

      for (auto& app : *apps)
      {
        if (monitored_app.id       ==      app.second->id &&
            monitored_app.store_id == (int)app.second->store)
        {
          app.second->_staging.running = 1;

          // Failed start -- let's clean up the wrong data
          if (iReturnCode > 0)
          {
            PLOG_ERROR << "Worker thread for launching app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " failed!";
            app.second->_staging.running =  0;

            monitored_app.id               =  0;
            monitored_app.store_id         = -1;
            monitored_app.iReturnCode.store (-1);

            if (hProcess != NULL)
            {
              CloseHandle (hProcess);
              hProcess = NULL;
              monitored_app.hProcess.store(NULL);
            }

            // Clean up these as well if they haven't been done so yet
            if (hWorkerThread != NULL)
            {
              CloseHandle(hWorkerThread);
              hWorkerThread = NULL;
              monitored_app.hWorkerThread.store(NULL);
            }
          }

          // Monitor the external process primarily
          if (hProcess != NULL)
          {
            if (WAIT_OBJECT_0 == WaitForSingleObject (hProcess, 0))
            {
              PLOG_DEBUG << "Game process for app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " has ended!";
              app.second->_staging.running = 0;
              app.second->_status.running  = 0;

              monitored_app.id              =  0;
              monitored_app.store_id        = -1;

              CloseHandle (hProcess);
              hProcess = NULL;
              monitored_app.hProcess.store(NULL);

              // Clean up these as well if they haven't been done so yet
              if (hWorkerThread != NULL)
              {
                CloseHandle(hWorkerThread);
                hWorkerThread = NULL;
                monitored_app.hWorkerThread.store(NULL);
              }

              PostMessage (SKIF_ImGui_hWnd, WM_NULL, 0x0, 0x0);
            }
          }

          // If we cannot monitor the game process, monitor the worker thread
          if (hWorkerThread != NULL)
          {
            if (WAIT_OBJECT_0 == WaitForSingleObject (hWorkerThread, 0))
            {
              PLOG_DEBUG << "Worker thread for launching app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " has ended!";

              CloseHandle (hWorkerThread);
              hWorkerThread = NULL;
              monitored_app.hWorkerThread.store(NULL);
            }
          }
        }
      }
    }
  }

  {
    std::scoped_lock app_lock (SKIF_GamingCollection::Get().GetMutex());

    // Push staging changes to actual state
    for (auto& app : *apps)
    {
      // The store client owns the running state (Steam: client running, registry monitoring available)
      if (app.second->IsRunningStateFromClient ( ))
        continue;

      if (app.second->_staging.running || app.second->_status.running)
        any_running = true;

      if (app.second->_status.dwTimeDelayChecks > current_time && (! forced))
          continue;

      app.second->_status.running     = app.second->_staging.running;
      app.second->_status.running_pid = app.second->_staging.running_pid;
    }
  }

  // If any game is running, and SKIF is not focused, then trigger a repaint every 2 seconds
  //   to ensure the running state is updated without any user input.
  if (any_running)
  {
    WINDOWINFO                       wndInfo = { .cbSize = sizeof (WINDOWINFO) };
    GetWindowInfo (SKIF_ImGui_hWnd, &wndInfo);

    if ((wndInfo.dwStyle & WS_ACTIVECAPTION) == 0)
    {
      Sleep (2000UL);

      PostMessage (SKIF_ImGui_hWnd, WM_NULL, 0x0, 0x0);
    }
  }
}

#pragma endregion


#pragma region Library Population

// Defined in tabs/library.cpp
extern bool SKIF_STEAM_OWNER;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

struct SKIF_GamingCollection::worker_s {
  AppList                 apps       = { };
  std::set <std::string>  apptickets = { };
  SteamId3_t              steam_user = 0;
  Trie                    labels     = { };
  HANDLE                  hWorker    = NULL;
  int                     iWorker    = 0;
  bool                    pcores     = false; // First refresh of the session (launch): its threads run on the P-cores
  std::unique_ptr <skValveDataFile>
                          appinfo    = nullptr; // Steam's appinfo.vdf, read by the worker (see SwapInPopulatedGames)
};

bool
SKIF_GamingCollection::PopulateGames (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (m_worker != nullptr)
    return false;

  // This relies on _path_cache having been initialized, so it cannot be done in the constructor
  if (m_metadataPath.empty ( ))
    m_metadataPath = SK_FormatStringW(LR"(%ws\Assets\db.json)", _path_cache.specialk_userdata);

  PLOG_INFO << "Populating library list...";

  // The Steam appinfo.vdf reader is reset by the library worker, which reads the file (all of it, which takes several
  //   ms), and SwapInPopulatedGames ( ) hands it over. Until then the current reader (if any) stays in use.

  m_worker = new worker_s;
  m_worker->steam_user = SKIF_Steam_GetCurrentUser ( );

  // The user waits on the library at launch, so that refresh runs on the P-cores; later ones keep the process default
  //   (E-cores, see SKIF_Util_SetProcessPrefersECores)
  static bool firstRefresh = true;
  m_worker->pcores = firstRefresh;
  firstRefresh     = false;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, LibraryWorkerThread, m_worker, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated)
  {
    m_worker->hWorker = hWorkerThread;
    m_worker->iWorker = 1;
  }
  else // Someting went wrong during thread creation, so free up the memory we allocated earlier
  {
    delete m_worker;
    m_worker = nullptr;
  }

  return threadCreated;
}

bool
SKIF_GamingCollection::IsPopulating (void) const
{
  return (m_worker != nullptr);
}

bool
SKIF_GamingCollection::IsPopulateFinished (void) const
{
  return (m_worker != nullptr && m_worker->iWorker == 1 && WaitForSingleObject (m_worker->hWorker, 0) == WAIT_OBJECT_0);
}

app_record_s*
SKIF_GamingCollection::Find (app_record_s::Store store, uint32_t appid)
{
  for (auto& app : m_apps)
  {
    if (app.second->id    != 0     &&
        app.second->id    == appid &&
        app.second->store == store)
      return app.second.get ( );
  }

  return nullptr;
}

int
SKIF_GamingCollection::SwapInPopulatedGames (void)
{
  if (! IsPopulateFinished ( ))
    return 0;

  int reaped = 0;

  struct IconCache {
    app_record_s::tex_registry_s tex_icon;
    app_record_s::Store store;
    AppId_t id;
  };

  std::vector<IconCache> icon_cache;
  icon_cache.reserve (m_apps.size ( ));

  // Clear up any unacknowledged icon workers
  for (auto& app : m_apps)
  {
    if (app.second->tex_icon.iWorker == 1)
    {
      if (WaitForSingleObject (app.second->tex_icon.hWorker, 200) == WAIT_OBJECT_0) // 200 second timeout (maybe change it?)
      {
        CloseHandle (app.second->tex_icon.hWorker);
        app.second->tex_icon.hWorker = NULL;
        app.second->tex_icon.iWorker = 2;
        reaped++;
      }
    }

    // Cache any existing icon textures...
    if (app.second->tex_icon.texture.p != nullptr)
    {
      icon_cache.push_back ({
        app.second->tex_icon,
        app.second->store,
        app.second->id
      });

      //SKIF_ResourcesToFree.push(app.second.tex_icon.texture.p);
      //app.second.tex_icon.texture.p = nullptr;
    }
  }

  std::scoped_lock app_lock (m_apps_mutex);

  // Clear current data
  m_apps.clear ( );       // app_entry_t is move-only, so no brace-list assignment
  m_apptickets = { };
  m_tLabels    = { };

  // Insert new data
  m_apps       = std::move (m_worker->apps);
  m_apptickets = std::move (m_worker->apptickets); // The worker is discarded after the swap
  m_tLabels    = std::move (m_worker->labels); // Trie is move-only (it owns its nodes)

  m_populatedOnce.store (true);

  // The Steam appinfo.vdf reader the worker created
  appinfo = std::move (m_worker->appinfo);

  // Move cached icons over (indexed by store + app ID instead of searching the cache for every game)
  auto _IconKey = [](app_record_s::Store store, AppId_t id) -> uint64_t
  {
    return (static_cast <uint64_t> (store) << 32) | static_cast <uint64_t> (id);
  };

  std::unordered_map <uint64_t, IconCache*> icon_index;
  icon_index.reserve (icon_cache.size ( ));

  for (auto& icon : icon_cache)
    icon_index.emplace (_IconKey (icon.store, icon.id), &icon); // First match wins, as before

  for (auto& app : m_apps)
  {
    if (auto it  = icon_index.find (_IconKey (app.second->store, app.second->id));
             it != icon_index.end ( ))
    {
      IconCache& icon = *it->second;

      app.second->tex_icon = std::move (icon.tex_icon); // Move it over
      app.second->tex_icon.iWorker = 2;
      icon.id = 0; // Mark it _not_ for release
      icon_index.erase (it);
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

  CloseHandle (m_worker->hWorker);
  m_worker->hWorker = NULL;
  m_worker->iWorker = 2;
  m_worker->labels  = Trie { };
  m_worker->apps.clear ();
  m_worker->apptickets.clear();

  delete m_worker;
  m_worker = nullptr;

  PLOG_VERBOSE << "Swapped in the new library data (collection)!";

  return reaped;
}

// Main thread: takes the embedded textures once the library worker's thread has loaded them
void
SKIF_GamingCollection::AdoptEmbeddedTextures (void)
{
  if (m_embeddedState.load ( ) != Embedded_Loaded)
    return;

  std::scoped_lock embedded_lock (m_embeddedMutex);

  m_pPatTexSRV          = m_pPatTexSRV_loaded;
  m_pSKLogoTexSRV       = m_pSKLogoTexSRV_loaded;
  m_pSKLogoTexSRV_small = m_pSKLogoTexSRV_small_loaded;

  m_pPatTexSRV_loaded         .Release ( );
  m_pSKLogoTexSRV_loaded      .Release ( );
  m_pSKLogoTexSRV_small_loaded.Release ( );

  m_embeddedState.store (Embedded_Adopted);
}

void
SKIF_GamingCollection::ReleaseTextures (void)
{
  // Loaded again by the next library refresh; textures still being loaded (on the old device) are discarded
  {
    std::scoped_lock embedded_lock (m_embeddedMutex);

    m_embeddedGeneration++;

    m_pPatTexSRV_loaded         .Release ( );
    m_pSKLogoTexSRV_loaded      .Release ( );
    m_pSKLogoTexSRV_small_loaded.Release ( );

    m_embeddedState.store (Embedded_None);
  }

  if (m_pPatTexSRV.p != nullptr)
  {
    SKIF_ResourcesToFree.push(m_pPatTexSRV.p);
    m_pPatTexSRV.p = nullptr;
  }

  if (m_pSKLogoTexSRV.p != nullptr)
  {
    SKIF_ResourcesToFree.push(m_pSKLogoTexSRV.p);
    m_pSKLogoTexSRV.p = nullptr;
  }

  if (m_pSKLogoTexSRV_small.p != nullptr)
  {
    SKIF_ResourcesToFree.push(m_pSKLogoTexSRV_small.p);
    m_pSKLogoTexSRV_small.p = nullptr;
  }
}

// Main thread: loads the embedded textures again on a background thread (device reset, after ReleaseTextures)
void
SKIF_GamingCollection::ReloadTextures (void)
{
  int embeddedNone = Embedded_None;

  // Already being loaded (on the current device), or loaded
  if (! m_embeddedState.compare_exchange_strong (embeddedNone, Embedded_Loading))
    return;

  try {
    std::thread ([]
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibraryTextures");
      SetThreadPriority              (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);

      CoInitializeEx (nullptr, 0x0);

      SKIF_GamingCollection::Get ( ).LoadEmbeddedTextures ( );

      CoUninitialize ( );
    }).detach ( );
  }
  catch (const std::system_error&)
  {
    // Not started: the next library refresh loads them
    m_embeddedState.store (Embedded_None);
  }
}

// Any thread: loads the embedded textures (Patreon, Special K logo) and hands them to the main thread (AdoptEmbeddedTextures)
//   The caller has set m_embeddedState to Embedded_Loading. A device reset meanwhile (ReleaseTextures) makes it discard them.
void
SKIF_GamingCollection::LoadEmbeddedTextures (void)
{
  const unsigned int generation = m_embeddedGeneration.load ( );

  PLOG_VERBOSE << "Loading the embedded Patreon texture...";

  ImVec2 dontCare;
  CComPtr <ID3D11ShaderResourceView> pPatreon, pLogo, pLogoSmall;

  LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, pPatreon,   L"patreon.png",         dontCare);
  LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, pLogo,      L"sk_boxart.png",       dontCare);
  LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, pLogoSmall, L"sk_boxart_small.png", dontCare);

  {
    std::scoped_lock embedded_lock (m_embeddedMutex);

    int embeddedLoading = Embedded_Loading;

    // Textures loaded across a device reset were (or may have been) created on the old device
    if (generation == m_embeddedGeneration.load ( ) &&
        m_embeddedState.compare_exchange_strong (embeddedLoading, Embedded_Loaded))
    {
      m_pPatTexSRV_loaded          = pPatreon;
      m_pSKLogoTexSRV_loaded       = pLogo;
      m_pSKLogoTexSRV_small_loaded = pLogoSmall;
    }
  }

  // Redraw with them
  PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);
}

#pragma region Store caches

bool
SKIF_Library_GetFileStamp (const std::wstring& path, uint64_t& write, uint64_t& size)
{
  WIN32_FILE_ATTRIBUTE_DATA fad = { };

  if (! GetFileAttributesExW (path.c_str ( ), GetFileExInfoStandard, &fad))
    return false;

  write = (static_cast <uint64_t> (fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
  size  = (static_cast <uint64_t> (fad.nFileSizeHigh)                  << 32) | fad.nFileSizeLow;

  return true;
}

static std::wstring
SKIF_Library_GetCachePath (const wchar_t* name)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\Cache\%ws)", _path_cache.specialk_userdata, name);
}

nlohmann::json
SKIF_Library_ReadCache (const wchar_t* name)
{
  std::ifstream file (SKIF_Library_GetCachePath (name), std::ios::binary);

  if (! file.is_open ( ))
    return nullptr;

  std::string data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));

  nlohmann::json json = nlohmann::json::parse (data, nullptr, false);

  if (json.is_discarded ( ))
  {
    PLOG_WARNING << "Ignoring the invalid cache file " << name;
    return nullptr;
  }

  return json;
}

void
SKIF_Library_WriteCache (const wchar_t* name, const nlohmann::json& data)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  CreateDirectoryW (SK_FormatStringW (LR"(%ws\Assets\Cache)", _path_cache.specialk_userdata).c_str ( ), nullptr);

  // Written to a temporary file first, so a cache file is never left half-written
  std::wstring path = SKIF_Library_GetCachePath (name),
               temp = path + L".tmp";

  {
    std::ofstream file (temp, std::ios::binary | std::ios::trunc);

    if (! file.is_open ( ))
    {
      PLOG_WARNING << "Could not write the cache file " << name;
      return;
    }

    file << data.dump ( );
  }

  if (! MoveFileExW (temp.c_str ( ), path.c_str ( ), MOVEFILE_REPLACE_EXISTING))
    PLOG_WARNING << "Could not replace the cache file " << name;
}

#pragma endregion

// The drive a path is on ("x:\" or "\\server\share\"), lower-case; empty if unknown.
//   The library worker processes the games of each drive on their own thread.
//   Read from the path itself: GetVolumePathNameW queries the file system for every folder in the path, which costs
//   more than the parallel processing saves. (A volume mounted into a folder counts as its host drive; that only
//   affects how the games are split across threads.)
static std::wstring
SKIF_GetVolumeKey (const std::wstring& path)
{
  std::wstring root = path;
  std::replace (root.begin ( ), root.end ( ), L'/', L'\\');

  if      (root.starts_with (LR"(\\?\UNC\)"))
    root = LR"(\\)" + root.substr (8);
  else if (root.starts_with (LR"(\\?\)"))
    root = root.substr (4);

  // Drive letter
  if (root.length ( ) >= 2 && root [1] == L':')
    return SKIF_Util_ToLowerW (root.substr (0, 2)) + L"\\";

  // Network share
  if (root.starts_with (LR"(\\)"))
  {
    size_t server = root.find (L'\\', 2);

    if (server != std::wstring::npos && server > 2)
      return SKIF_Util_ToLowerW (root.substr (0, root.find (L'\\', server + 1))) + L"\\";
  }

  return L"";
}

unsigned __stdcall
SKIF_GamingCollection::LibraryWorkerThread (void* var)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& self        = SKIF_GamingCollection::Get ( );

  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibraryWorker");

  // Is this combo really appropriate for this thread?
  //SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
  //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

  PLOG_VERBOSE << "SKIF_LibraryWorker thread started!";

  // This thread and the ones it starts work on the worker's own app list (moved into the collection by
  //   SwapInPopulatedGames ( ) once this thread has ended), so they don't take the apps lock, and the main thread
  //   isn't held up by the library refresh
  tl_isLibraryWorker = true;

  worker_s* _worker = static_cast <worker_s*> (var);

  // First refresh of the session: this thread and the ones it starts run on the P-cores (see PopulateGames ( ))
  auto _PreferPCores = [pcores = _worker->pcores](void) -> bool
  {
    return (pcores && SKIF_Util_SetThreadPrefersPCores ( ));
  };

  // (Called outside the log macro: plog skips evaluating its condition when verbose logging is off)
  const bool onPCores = _PreferPCores ( ); // Only on hybrid CPUs

  PLOG_VERBOSE_IF (onPCores) << "This refresh runs on the P-cores (first refresh of the session).";

  // Work that runs on a thread of its own, unless this thread gets to it first. Starting a thread can take tens of ms at
  //   launch: a new thread has to wait for the Windows loader lock, which the main thread holds while it loads DLLs to
  //   create and show its window. With the caches the scans take a few ms, so once this thread is done with its own
  //   share it does any work whose thread hasn't started yet (_Steal), and a thread that starts after its work was
  //   taken just ends. The threads are detached so that a late one never holds this thread up: until it claims its
  //   work, it only touches its task (shared), and claimed work (which uses this function's locals) is always waited
  //   for (_Wait) before this function returns.
  struct worker_task_s {
    const wchar_t*              name    = L"";
    std::function <void (void)> work;
    std::atomic <bool>          claimed = false; // Taken by its thread or by this one
    std::atomic <bool>          done    = false;
  };

  using worker_task_t = std::shared_ptr <worker_task_s>;

  // Does the work, unless it was taken already; true if it did
  auto _Claim = [](worker_task_s& task) -> bool
  {
    if (task.claimed.exchange (true))
      return false;

    task.work ( );

    task.done.store      (true);
    task.done.notify_all ( );

    return true;
  };

  auto _Start = [&](const wchar_t* name, std::function <void (void)> work, bool background = false) -> worker_task_t
  {
    auto task  = std::make_shared <worker_task_s> ( );
    task->name = name;
    task->work = std::move (work);

    try {
      std::thread ([task, background, _Claim, _PreferPCores]
      {
        SKIF_Util_SetThreadDescription (GetCurrentThread (), task->name);
        tl_isLibraryWorker = true;
        _PreferPCores ( );

        if (background)
          SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);

        _Claim (*task);
      }).detach ( );
    }
    catch (const std::system_error&) { } // Not started: _Steal ( ) does the work here

    return task;
  };

  auto _Steal = [&](const worker_task_t& task)
  {
    if (task && _Claim (*task))
      PLOG_VERBOSE << "[Library Processing] " << task->name << ": done by the library worker (its thread had not started yet).";
  };

  auto _Wait = [](const worker_task_t& task)
  {
    if (task)
      task->done.wait (false);
  };

  // The embedded textures (loaded once) don't depend on the library, so they load alongside it, below normal priority
  //   (they're only needed once the library shows, so the store scanning gets the CPU first). The library doesn't wait
  //   for them: they're created on the hardware device, which SKIF may still be waiting for (see SKIF_D3D11_GetDevice).
  //   So the work only uses its own locals and the collection (a static), and hands the textures to the main thread
  //   (AdoptEmbeddedTextures); a device reset meanwhile (ReleaseTextures) makes it discard them.
  int embeddedNone = Embedded_None;

  if (self.m_embeddedState.compare_exchange_strong (embeddedNone, Embedded_Loading))
  {
    _Start (L"SKIF_LibraryWorker_Textures", []
    {
      self.LoadEmbeddedTextures ( );
    }, true);
  }

  // Read Steam's appinfo.vdf (the whole file, several ms) alongside the library refresh. Only the main thread uses the
  //   reader, once SwapInPopulatedGames ( ) has handed it over, so the library doesn't need it.
  worker_task_t appinfoTask =
    _Start (L"SKIF_LibraryWorker_AppInfo", [_worker]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );

      _worker->appinfo = std::make_unique <skValveDataFile> (std::wstring (_path_cache.steam_install) + LR"(\appcache\appinfo.vdf)");

      PLOG_DEBUG << "[Library Processing] Read appinfo.vdf in " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms.";
    });

  DWORD pre   = 0,
        post  = 0,
        start = SKIF_Util_timeGetTime1 ( );

  worker_s* _data = static_cast<worker_s*>(var);

  size_t games = _data->apps.size();

  // Load the games of the enabled stores in parallel: each store reads its own sources (client files, registry,
  //   manifests) into its own list. The lists are merged in a fixed order afterwards: Steam, Special K, GOG, Epic, EA, Xbox, custom.
  enum { Scan_Steam, Scan_GOG, Scan_Epic, Scan_EA, Scan_Xbox, Scan_Custom, Scan_Count };

  app_list_t                scanned [Scan_Count];
  worker_task_t             scans   [Scan_Count]; // On threads of their own (see _Start)
  std::vector <std::function <void (void)>>
                            scans_here;           // Quick ones (~1 ms), done on this thread first

  auto _LogScan = [&](const char* what, size_t count, DWORD dwStart)
  {
    if (! _registry._LibraryHidden)
      PLOG_DEBUG << "[Library Processing] Processed " << count << " " << what << " in " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms.";
  };

  // Load Steam titles from disk
  if (_registry.bLibrarySteam || _registry._LibraryHidden)
  {
    scans [Scan_Steam] = _Start (L"SKIF_LibraryWorker_Steam", [&]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );

      SKIF_Steam_GetInstalledAppIDs (&scanned [Scan_Steam]);

      _LogScan ("Steam games", scanned [Scan_Steam].size ( ), dwStart);
      dwStart = SKIF_Util_timeGetTime1 ( );

      // Preload user-specific stuff for all Steam games (custom launch options + DLC ownership)
      SKIF_Steam_PreloadUserConfig (_data->steam_user, &scanned [Scan_Steam], &_data->apptickets);

      if (! _registry._LibraryHidden)
        PLOG_DEBUG << "[Library Processing] Processed Steam user configs in " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms.";
    });
  }

  // Load GOG titles from registry
  if (_registry.bLibraryGOG || _registry._LibraryHidden)
  {
    scans_here.emplace_back ([&]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );
      SKIF_GOG_GetInstalledAppIDs  (&scanned [Scan_GOG]);
      _LogScan ("GOG games", scanned [Scan_GOG].size ( ), dwStart);
    });
  }

  // Load Epic titles from disk
  if (_registry.bLibraryEpic || _registry._LibraryHidden)
  {
    scans [Scan_Epic] = _Start (L"SKIF_LibraryWorker_Epic", [&]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );
      SKIF_EpicCollection::GetInstance ( ).GetInstalledAppIDs (&scanned [Scan_Epic]);
      _LogScan ("Epic games", scanned [Scan_Epic].size ( ), dwStart);
    });
  }

  // Load EA titles from the registry (uninstall entries) and their installerdata.xml manifests
  if (_registry.bLibraryEA || _registry._LibraryHidden)
  {
    scans [Scan_EA] = _Start (L"SKIF_LibraryWorker_EA", [&]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );
      SKIF_EA_GetInstalledAppIDs (&scanned [Scan_EA]);
      _LogScan ("EA games", scanned [Scan_EA].size ( ), dwStart);
    });
  }

  if (_registry.bLibraryXbox || _registry._LibraryHidden)
  {
    scans [Scan_Xbox] = _Start (L"SKIF_LibraryWorker_Xbox", [&]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );
      SKIF_Xbox_GetInstalledAppIDs (&scanned [Scan_Xbox]);
      _LogScan ("Xbox games", scanned [Scan_Xbox].size ( ), dwStart);
    });
  }

  // Load custom SKIF titles from registry
  if (_registry.bLibraryCustom || _registry._LibraryHidden)
  {
    scans_here.emplace_back ([&]
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );
      SKIF_GetCustomAppIDs (&scanned [Scan_Custom]);
      _LogScan ("custom SKIF titles", scanned [Scan_Custom].size ( ), dwStart);
    });
  }

  for (auto& scan : scans_here)
    scan ( );

  // Any scan whose thread hasn't started yet is done here, then the rest are waited for
  for (auto& scan : scans)
    _Steal (scan);

  for (auto& scan : scans)
    _Wait (scan);

  // Reserve for every store's results (+1 for the Special K entry) so the merge does not reallocate
  size_t total_apps = _data->apps.size ( ) + 1;
  for (auto& list : scanned)
    total_apps += list.size ( );
  _data->apps.reserve (total_apps);

  auto _Merge = [&](app_list_t& list)
  {
    for (auto& entry : list)
      _data->apps.emplace_back (std::move (entry));

    list.clear ( );
  };

  _Merge (scanned [Scan_Steam]);
  games = _data->apps.size();

  if ( ! SKIF_STEAM_OWNER )
  {
    app_record_steam_s SKIF_record (SKIF_STEAM_APPID);

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

    app_entry_t
      SKIF ( "Special K", std::make_unique <app_record_steam_s> (std::move (SKIF_record)) );

    _data->apps.emplace_back (std::move (SKIF));
    games = _data->apps.size();
  }

  _Merge (scanned [Scan_GOG]);
  _Merge (scanned [Scan_Epic]);
  _Merge (scanned [Scan_EA]);
  _Merge (scanned [Scan_Xbox]);
  _Merge (scanned [Scan_Custom]);
  games = _data->apps.size();

  post = SKIF_Util_timeGetTime1 ( );
  PLOG_DEBUG << "[Library Processing] Loaded the stores' games (in parallel) in " << (post - start) << " ms.";

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
        auto& record     = *app.second;
        auto& append_cfg = record.GetOnlineLaunchConfigs ( );

        std::string key  = record.GetPersistentKey ( );

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
    catch (const std::exception& e)
    {
      PLOG_ERROR << "Error occurred when trying to parse " << ((lc_file.first) ? "online-based" : "user-specified") << " launch configs: " << e.what ();
    }
  }

  PLOG_INFO << "Loading persistent metadata...";

  std::ifstream file(self.m_metadataPath);
  if (file.is_open())
  {
    std::scoped_lock jsonLock (self.m_jsonMetaDB_mutex);

    self.m_jsonMetaDB = nlohmann::json::parse(file, nullptr, false);
    file.close();

    if (self.m_jsonMetaDB.is_discarded ( ))
    {
      PLOG_ERROR << "Error occurred while trying to parse " << self.m_metadataPath;
      MoveFileEx (self.m_metadataPath.c_str(), (self.m_metadataPath + L".bak").c_str(), MOVEFILE_REPLACE_EXISTING);
      self.m_jsonMetaDB = nlohmann::json();
    }
    else {
      PLOG_INFO << "Successfully read persistent metadata (" << self.MetaDB_CountElements ( ) << " items) from JSON file.";
    }
  }
  else {
    PLOG_ERROR << "Could not open JSON file for reading: " << self.m_metadataPath;
  }

  PLOG_INFO << "Processing detected games...";
  pre = SKIF_Util_timeGetTime1 ( );

  bool     newCategories = true;
  HKEY     hProfilesKey  = 0;
  HKEY     hAppIdsKey    = 0;
  LSTATUS lsKey = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Profiles)", &hProfilesKey);

  if (lsKey == ERROR_SUCCESS)
      lsKey  = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\AppIds)", &hAppIdsKey);
  else if (      hProfilesKey != 0)
    RegCloseKey (hProfilesKey);

  // UNIX timestamp for new arrivals
  time_t ltime;
  time (&ltime);
  std::string szTime = std::to_string (ltime);

  // Process the list of apps -- prepare their names, check their install folders, load their metadata, and remove
  //   any uninstalled entries. This touches each game's files, so the games are grouped by the volume their files are on
  //   and each volume is processed on its own thread: several drives are read at once, but each drive by one thread.
  //   Shared state (vecCategories, the label trie, the Profiles / AppIds registry values) is updated afterwards.
  //   The game threads don't touch the shared metadata (db.json): each game's entry is copied in before and written
  //   back after the parallel part, by this thread.
  struct game_job_s {
    app_entry_t*   app   = nullptr;
    std::string    key;           // GetPersistentKey ( ), taken before the processing (it may set a Steam game's ID to 0)
    nlohmann::json meta;          // The game's db.json entry (null if it has none); updated by the processing
    bool           write = false; // Write meta back to db.json
  };

  DWORD dwPhase = SKIF_Util_timeGetTime1 ( );

  std::vector <game_job_s> jobs (_data->apps.size ( ));
  bool                     metaAvailable = false;

  {
    std::scoped_lock jsonLock (self.m_jsonMetaDB_mutex);

    const nlohmann::json& db = self.m_jsonMetaDB;
    metaAvailable            = ! db.is_discarded ( );

    for (size_t i = 0; i < jobs.size ( ); i++)
    {
      auto& job = jobs [i];
      job.app   = &_data->apps [i];

      if (! metaAvailable)
        continue;

      job.key   = job.app->second->GetPersistentKey ( );

      if (db.is_object ( ))
      {
        auto store = db.find (job.app->second->store_utf8);

        if (store != db.end ( ) && store->is_object ( ))
        {
          auto entry = store->find (job.key);

          if (entry != store->end ( ))
            job.meta = *entry;
        }
      }
    }
  }

  auto _ProcessGame = [&](game_job_s& job) -> void
  {
    app_entry_t& app = *job.app;

    PLOG_VERBOSE << "Working on " << app.second->id << " (" << app.second->store_utf8 << ")";

    bool isSpecialK = app.second->IsSpecialK ( );

    // Steam handling...
    // Special handling for non-Steam owners of Special K / SKIF
    if (isSpecialK)
    {
      app.first               = "Special K";

      // Default values that needs to be set
      app.second->skif.pinned  = 99; // Default to pinned
      app.second->ui.pcgw      = app.second->GetPCGamingWikiURL ( );
      app.second->ui.sgdbGrids = app.second->GetSteamGridDBURL  (false);
      app.second->ui.sgdbIcons = app.second->GetSteamGridDBURL  (true);
    }

    // Regular handling for the remaining games (Steam: queries the client; uninstalled games get id 0)
    else
      app.second->RefreshInstallState ( );

    // Only bother opening the application manifest
    //   and looking for a name if the client claims
    //     the app is installed.
    if (app.second->_status.installed)
    {
      // Load any custom data (the game's own copy of its db.json entry: see game_job_s)
      if (metaAvailable)
      {
        const std::string& item = job.key;

        try {
          auto& key = job.meta;

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
          app.second->skif.name           = keyName;
          app.second->skif.cpu_type       = keyCPU;
          app.second->skif.auto_stop      = keyAutoStop;
          app.second->skif.hidden         = keyHidden;
          app.second->skif.uses           = keyUses;
          app.second->skif.used           = keyUsed;
          app.second->skif.category       = keyGroup;
          app.second->skif.pinned         = keyPinned;

          // Unrecognized categories are added to SKIF's internal database after the parallel part (vecCategories is shared)

          // For newly arrived games, we populate the used parameter with the current time
          //   so they appear on top when sorting by Last Played, mirroring Steam a bit...
          if (app.second->skif.used.empty())
            app.second->skif.used         = szTime;

          // Human-readable time format (local time)
          time_t          last_played    = (time_t)strtol(app.second->skif.used.c_str(), NULL, 10);
          app.second->skif.used_formatted = SK_WideCharToUTF8 (SKIF_Util_timeGetTimeAsWStr (last_played));

          if (app.second->isInstantPlayCompatible () && key.contains ("InstantPlay"))
          {
            app.second->skif.instant_play = key.at ("InstantPlay");
          }

          // This also populates the JSON object with empty entries for new games
          key = {
            { "Name",     app.second->skif.name      },
            { "CPU",      app.second->skif.cpu_type  },
            { "AutoStop", app.second->skif.auto_stop },
            { "Hidden",   app.second->skif.hidden    },
            { "Uses",     app.second->skif.uses      },
            { "Used",     app.second->skif.used      },
            { "Category", app.second->skif.category  },
            { "Pin",      app.second->skif.pinned    }
          };

          if (app.second->isInstantPlayCompatible ())
            key += { "InstantPlay", app.second->skif.instant_play };

          job.write = true;
        }
        catch (const std::exception& e)
        {
          PLOG_ERROR << "Error occurred when trying to parse " << item << " (" << (int)app.second->store << ") from " << self.m_metadataPath << ": " << e.what ();
        }
      }

      if (! app.second->names.normal.empty ())
        app.first = app.second->names.normal;

      // Some games have an install state but no name,
      //   for those we have to consult the store's own data (Steam: the app manifest)
      else if (! isSpecialK)
      {
        std::string fallbackName = app.second->GetFallbackName ( );

        if (! fallbackName.empty ( ))
          app.first = fallbackName;
      }

      // Corrupted app manifest / not known to Steam client; SKIP!
      if (app.first.empty ())
      {
        PLOG_DEBUG << "App ID " << app.second->id << " (" << app.second->store_utf8 << ") has no name; ignoring!";

        app.second->id = 0;
        return;
      }

      // Hide any... uhm... hidden... games...
      if (! _registry._LibraryHidden && app.second->IsHidden ( ))
      {
        PLOG_DEBUG << "App ID " << app.second->id << " (" << app.second->store_utf8 << ") has been hidden; ignoring!";

        app.second->id = 0;
        return;
      }

      // Hide all non-hidden games if we are in "hidden mode", lol?
      // Except for Special K, obviously
      else if (_registry._LibraryHidden && ! isSpecialK && (app.second->skif.hidden == 0 || (app.second->skif.hidden == -1 && app.second->GetClientHidden ( ) == 0)))
      {
        app.second->id = 0;
        return;
      }

      // Strip any remaining null terminators
      app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

      app.second->names.original = app.first;

      // Some games use weird Unicode character combos that ImGui can't handle,
      //  so let's replace those with the normal ones.

      // Replace RIGHT SINGLE QUOTATION MARK (Code: 2019 | UTF-8: E2 80 99)
      //  with a APOSTROPHE (Code: 0027 | UTF-8: 27)
      SKIF_Util_ReplaceAll (app.first, "\xE2\x80\x99", "\x27");

      // Replace LATIN SMALL LETTER O (Code: 006F | UTF-8: 6F) and COMBINING DIAERESIS (Code: 0308 | UTF-8: CC 88)
      //  with a LATIN SMALL LETTER O WITH DIAERESIS (Code: 00F6 | UTF-8: C3 B6)
      SKIF_Util_ReplaceAll (app.first, "\x6F\xCC\x88", "\xC3\xB6");

      // Remove COPYRIGHT SIGN (Code: 00A9 | UTF-8: C2 A9)
      SKIF_Util_ReplaceAll (app.first, "\xC2\xA9", "");

      // Remove REGISTERED SIGN (Code: 00AE | UTF-8: C2 AE)
      SKIF_Util_ReplaceAll (app.first, "\xC2\xAE", "");

      // Remove TRADE MARK SIGN (Code: 2122 | UTF-8: E2 84 A2)
      SKIF_Util_ReplaceAll (app.first, "\xE2\x84\xA2", "");

      // Output the name change (use widechar to have them appear correct in the logs)
      if (app.second->names.original != app.first)
        PLOG_DEBUG << R"(Game title was changed: ")" << SK_UTF8ToWideChar(app.second->names.original.c_str()) << R"(" --> ")" << SK_UTF8ToWideChar(app.first.c_str()) << R"(")";

      SKIF_Util_TrimSpaces (app.first);

      // Strip any remaining null terminators
      //app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

      // Store the cleaned copy of the original
      app.second->names.clean = app.first;

      // Apply any custom name
      if (! app.second->skif.name.empty())
      {
        app.second->names.normal  = app.second->skif.name;
        app.first                = app.second->names.normal;
      }

      // Update ImGuiLabelID and ImGuiPushID
      app.second->ImGuiLabelID    = SKIF_Util_FormatStringRaw ("###%i-%i-selectable", (int)app.second->store, app.second->id); // ("%s###%i-%i", app.first.c_str(), (int)app.second.store, app.second.id)
      app.second->ImGuiPushID     = SKIF_Util_FormatStringRaw ("###%i-%i",            (int)app.second->store, app.second->id);

#if 0
      PLOG_DEBUG << "\nGame Titles: (" << app.second->id << ")"
                 << "\nOriginal   : " << SK_UTF8ToWideChar(app.second->names.original)
                 << "\nCleaned    : " << SK_UTF8ToWideChar(app.second->names.clean)
                 << "\nNormal     : " << SK_UTF8ToWideChar(app.second->names.normal)
                 << "\nImGuiLID   : " << SK_UTF8ToWideChar(app.second->ImGuiLabelID)
                 << "\nInstallDir : " << app.second->install_dir;
#endif
    }

    // Check if install folder exists (but not for Xbox games or Special K)
    if (app.second->ValidatesInstallDirectory ( ) && ! isSpecialK)
    {
      // This populates install_dir for Steam games
      app.second->ResolveInstallDirectory ( );

      if (! PathFileExists (app.second->install_dir.c_str()))
      {
        PLOG_DEBUG << "App ID " << app.second->id << " (" << app.second->store_utf8 << ") has non-existent install folder; ignoring!";

        app.second->id = 0;
        return;
      }

      // Preload store-specific details (Steam: active branch)
      app.second->ResolveStoreDetails ( );
    }
  };

  std::map <std::wstring, std::vector <game_job_s*>> volumes;

  for (auto& job : jobs)
    volumes [SKIF_GetVolumeKey (job.app->second->GetStorageLocation ( ))].push_back (&job);

  // One worker per volume, at most one per CPU thread; with more volumes than that, each volume goes to the worker with the fewest games
  std::vector <std::vector <game_job_s*>> workloads (
    std::min <size_t> (volumes.size ( ), std::max <size_t> (1, std::thread::hardware_concurrency ( )))
  );

  std::vector <std::pair <std::wstring, std::vector <game_job_s*>*>> bySize;
  for (auto& volume : volumes)
    bySize.push_back ({ volume.first, &volume.second });

  std::stable_sort (bySize.begin ( ), bySize.end ( ), [](const auto& a, const auto& b) { return a.second->size ( ) > b.second->size ( ); });

  for (auto& volume : bySize)
  {
    auto& workload =
      *std::min_element (workloads.begin ( ), workloads.end ( ), [](const auto& a, const auto& b) { return a.size ( ) < b.size ( ); });

    workload.insert (workload.end ( ), volume.second->begin ( ), volume.second->end ( ));

    PLOG_VERBOSE << "[Library Processing] Volume " << (volume.first.empty ( ) ? L"(unknown)" : volume.first) << ": " << volume.second->size ( ) << " games";
  }

  PLOG_DEBUG << "[Library Processing] Prepared the games (metadata, drives) in " << (SKIF_Util_timeGetTime1 ( ) - dwPhase) << " ms.";
  dwPhase = SKIF_Util_timeGetTime1 ( );

  // The first (largest) workload is processed on this thread, the others on their own threads: one drive means no
  //   extra threads at all. As with the store scans, this thread does any whose thread hasn't started yet (see _Start).
  std::vector <worker_task_t> drives;

  for (size_t i = 1; i < workloads.size ( ); i++)
  {
    drives.push_back (_Start (L"SKIF_LibraryWorker_Games", [&, i]
    {
      for (auto* job : workloads [i])
        _ProcessGame (*job);
    }));
  }

  if (! workloads.empty ( ))
  {
    for (auto* job : workloads [0])
      _ProcessGame (*job);
  }

  for (auto& drive : drives)
    _Steal (drive);

  for (auto& drive : drives)
    _Wait (drive);

  PLOG_DEBUG << "[Library Processing] Processed games on " << volumes.size ( ) << " drive(s) using " << workloads.size ( ) << " thread(s) in " << (SKIF_Util_timeGetTime1 ( ) - dwPhase) << " ms.";
  dwPhase = SKIF_Util_timeGetTime1 ( );

  // Write the updated metadata back to db.json (in memory; the file is written below)
  if (metaAvailable)
  {
    std::scoped_lock jsonLock (self.m_jsonMetaDB_mutex);

    for (auto& job : jobs)
    {
      if (job.write)
        self.m_jsonMetaDB [job.app->second->store_utf8][job.key] = std::move (job.meta);
    }
  }

  // Shared state, in list order
  for (auto& app : _data->apps)
  {
    // For unrecognized categories, add them to SKIF's internal database as well
    const std::string& keyGroup = app.second->skif.category;

    if (! keyGroup.empty() && std::find_if(_registry.vecCategories.begin(), _registry.vecCategories.end(), [&](const SKIF_RegistrySettings::category_s& category) { return category.name == keyGroup; }) == _registry.vecCategories.end())
    {
      // The categories are shared with the main thread
      std::scoped_lock categories_lock (self.m_apps_mutex);

      _registry.vecCategories.push_back (SKIF_RegistrySettings::category_s { keyGroup, false });
      newCategories = true;
    }

    // Games that made it through the processing above (the others have an ID of 0)
    if (app.second->id != 0 && app.second->_status.installed && ! app.second->IsSpecialK ( ))
    {
      // Prepare for the keyboard hint / search/filter functionality
      InsertTrieKey (&app, &_data->labels);

      std::wstring wsName =
        SK_UTF8ToWideChar (app.second->names.original);

      // Ensure the Profiles registry key is populated properly, but only write it once to ensure
      //   profile folders are not randomly renamed if a game gets renamed on the storefront
      if (ERROR_SUCCESS        == lsKey &&
          ERROR_FILE_NOT_FOUND == RegQueryValueExW (hProfilesKey, app.second->install_dir.c_str(), NULL, NULL, NULL, NULL))
      {
        if (ERROR_SUCCESS != RegSetValueExW (hProfilesKey, app.second->install_dir.c_str(), 0, REG_SZ, (LPBYTE)wsName.data(), (DWORD)wsName.length() * sizeof(wchar_t)))
          PLOG_ERROR   << "Failed adding profile name (" << wsName << ") to registry value: " << app.second->install_dir;
      }

      // Populate a list of Path-to-AppID mappings in the registry for platforms that may not have a manifest that Special K can parse (e.g. Epic)
      std::string appIdMapping = app.second->GetAppIdMapping ( );

      if (ERROR_SUCCESS == lsKey && ! appIdMapping.empty ())
      {
        std::wstring wsAppId =
          SK_UTF8ToWideChar (appIdMapping);

        if (ERROR_SUCCESS != RegSetValueExW (hAppIdsKey, app.second->install_dir.c_str(), 0, REG_SZ, (LPBYTE)wsAppId.data (), (DWORD)wsAppId.length() * sizeof(wchar_t)))
          PLOG_ERROR   << "Failed adding appid mapping (" << wsAppId << ") to registry value : " << appIdMapping;
      }

      // Register profile mappings for all launch configs
      for (auto& lc : app.second->launch_configs)
      {
        auto& exe_path =
          lc.second.executable_path;

        if (ERROR_SUCCESS        == lsKey &&
            ERROR_FILE_NOT_FOUND == RegQueryValueExW (hProfilesKey, exe_path.c_str (), NULL, NULL, NULL, NULL))
        {
          if (ERROR_SUCCESS != RegSetValueExW (hProfilesKey, exe_path.c_str (), 0, REG_SZ, (LPBYTE)wsName.data(), (DWORD)wsName.length() * sizeof(wchar_t)))
            PLOG_ERROR   << "Failed adding profile name (" << wsName << ") to registry value: " << exe_path;
        }
      }

      // Some stores have additional directories (Xbox: app and Program Files folders),
      //   but otherwise the procedure is the same as above: if any is unmapped, map them all.
      std::vector <std::wstring> additionalPaths = app.second->GetAdditionalProfilePaths ( );

      if (! additionalPaths.empty ( ) && ERROR_SUCCESS == lsKey &&
          std::any_of (additionalPaths.begin ( ), additionalPaths.end ( ), [&](const std::wstring& path) {
            return ERROR_FILE_NOT_FOUND == RegQueryValueExW (hProfilesKey, path.c_str(), NULL, NULL, NULL, NULL); }))
      {
        for (auto& path : additionalPaths)
        {
          if (ERROR_SUCCESS != RegSetValueExW (hProfilesKey, path.c_str(), 0, REG_SZ, (LPBYTE)wsName.data(), (DWORD)wsName.length() * sizeof(wchar_t)))
            PLOG_ERROR << "Failed adding profile name (" << wsName << ") to registry value: " << path;
        }
      }
    }
  }

  if (ERROR_SUCCESS == lsKey) {
    RegCloseKey (hProfilesKey);
    RegCloseKey (hAppIdsKey);
  }

  PLOG_DEBUG << "[Library Processing] Updated the metadata, categories, search and registry in " << (SKIF_Util_timeGetTime1 ( ) - dwPhase) << " ms.";

  // Update the db.json file with any additions and whatnot
  self.MetaDB_WriteFile ( );

  // We have detected new categories!
  if (newCategories)
  {
    // The categories are shared with the main thread
    std::scoped_lock categories_lock (self.m_apps_mutex);

    // Sort the list of categories
    _registry.vecCategories = _registry.SortCategories (_registry.vecCategories);

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
  }

  post = SKIF_Util_timeGetTime1 ( );
  games = games - 1; // Do not count Special K as a game
  PLOG_DEBUG << "Finished processing " << games << " detected games in " << (post - pre) << " ms.";

  SKIF_GamingCollection::SortApps (&_data->apps);

  //PLOG_INFO << "Apps were sorted!";

  // The appinfo.vdf reader is handed over with the library
  _Steal (appinfoTask);
  _Wait  (appinfoTask);

  PLOG_INFO << "Finished populating the library list.";

  // Force a refresh when the game icons have finished being streamed
  PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

  PLOG_DEBUG << "Library refresh took " << (SKIF_Util_timeGetTime1 ( ) - start) << " ms.";

  PLOG_VERBOSE << "SKIF_LibraryWorker thread stopped!";

  //SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

  return 0;
}

#pragma endregion

#pragma region Persistent Metadata

// Basic counter used to provide some additional metadata in the logs
size_t
SKIF_GamingCollection::MetaDB_CountElements (void)
{
  size_t items = 0;

  for (auto& platform : m_jsonMetaDB)
  {
    items += platform.size();
  }

  return items;
}

// This writes the Json object to the disk
bool
SKIF_GamingCollection::MetaDB_WriteFile (void)
{
  std::scoped_lock jsonLock (m_jsonMetaDB_mutex);

  if (! m_jsonMetaDB.is_discarded())
  {
    std::ofstream out_file(m_metadataPath);
    if (out_file.is_open())
    {
      out_file << std::setw(2) << m_jsonMetaDB << std::endl;
      out_file.close();

      PLOG_INFO << "Successfully wrote persistent metadata (" << MetaDB_CountElements ( ) << " items) to JSON file.";

      return true;
    }
    else {
      PLOG_ERROR << "Could not open JSON file for writing: " << m_metadataPath;
    }
  }

  else
    PLOG_ERROR << "Could not write metadata to JSON file as the metadata had been discarded.";

  return false;
}

// This both updates the metadata and optionally writes the new object to the disk
bool
SKIF_GamingCollection::MetaDB_UpdateApp (app_record_s* pApp, bool bWriteToDisk)
{
  std::scoped_lock jsonLock (m_jsonMetaDB_mutex);

  // Update the db.json file with any new values
  if (! m_jsonMetaDB.is_discarded())
  {
    std::string item = pApp->GetPersistentKey ( );

    try {
      auto& key = m_jsonMetaDB[pApp->store_utf8][item];

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

      if (pApp->isInstantPlayCompatible ( ))
        key += { "InstantPlay", pApp->skif.instant_play };

      return ! bWriteToDisk || MetaDB_WriteFile ( );
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
SKIF_GamingCollection::MetaDB_SetCategoryName (std::string szOld, std::string szNew)
{
  if (szOld.empty())
    return false;

  std::scoped_lock jsonLock (m_jsonMetaDB_mutex);

  if (! m_jsonMetaDB.is_discarded())
  {
    for (auto& platform : m_jsonMetaDB)
    {
      for (auto& game : platform)
      {
        if (! game.empty() && game.contains("Category") && game.at("Category") == szOld)
          game.at("Category") = szNew;
      }
    }

    return MetaDB_WriteFile ( );
  }

  else
    PLOG_ERROR << "Could not write metadata to JSON file as the metadata had been discarded.";

  return false;
}

#pragma endregion


SKIF_GamingCollection::SKIF_GamingCollection (void)
{
  //static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );


}
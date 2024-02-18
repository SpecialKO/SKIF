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

#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include "stores/Steam/apps_ignore.h"
#include <utility/fsutil.h>
#include <stores/GOG/gog_library.h>
#include <stores/epic/epic_library.h>
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
  // start from root node
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // create a new node if path doesn't exists
    if (curr->character [key [i]] == nullptr)
        curr->character [key [i]]  = new Trie ();

    // go to next node
    curr = curr->character [key [i]];
  }

  // mark current node as leaf
  curr->isLeaf = true;
}

// Iterative function to search a key in Trie. It returns true
// if the key is found in the Trie, else it returns false
bool
Trie::search (const std::string& key)
{
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // go to next node
    curr = curr->character [key [i]];

    // if string is invalid (reached end of path in Trie)
    if (curr == nullptr)
      return false;
  }

  // if current node is a leaf and we have reached the
  // end of the string, return true
  return curr->isLeaf;
}

// returns true if given node has any children
bool
Trie::haveChildren (Trie const* curr)
{
  for (int i = 0; i < CHAR_SIZE; i++)
    if (curr->character [i])
      return true;  // child found

  return false;
}

// Recursive function to delete a key in the Trie
bool
Trie::deletion (Trie*& curr, const std::string& key)
{
  // return if Trie is empty
  if (curr == nullptr)
    return false;

  // if we have not reached the end of the key
  if (key.length ())
  {
    // recur for the node corresponding to next character in the key
    // and if it returns true, delete current node (if it is non-leaf)

    if (        curr                      != nullptr       &&
                curr->character [key [0]] != nullptr       &&
      deletion (curr->character [key [0]], key.substr (1)) &&
                curr->isLeaf == false)
    {
      if (! haveChildren (curr))
      {
        delete curr;
        curr = nullptr;
        return true;
      }

      else {
        return false;
      }
    }
  }

  // if we have reached the end of the key
  if (key.length () == 0 && curr->isLeaf)
  {
    // if current node is a leaf node and don't have any children
    if (! haveChildren (curr))
    {
      // delete current node
      delete curr;
      curr = nullptr;

      // delete non-leaf parent nodes
      return true;
    }

    // if current node is a leaf node and have children
    else
    {
      // mark current node as non-leaf node (DON'T DELETE IT)
      curr->isLeaf = false;

      // don't delete its parent nodes
      return false;
    }
  }

  return false;
}

void
InsertTrieKey (std::pair <std::string, app_record_s>* app, Trie* labels)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  std::string all_upper = SKIF_Util_ToUpper (app->first),
              all_upper_alnum;
          
  for (const char c : app->first)
  {
    if (! ( isalnum (c) || isspace (c) ))
      continue;

    all_upper_alnum += (char)toupper (c);
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
        
  app->second.names.normal          = app->first;
  app->second.names.all_upper       = all_upper;
  app->second.names.all_upper_alnum = all_upper_alnum;
  app->second.names.pre_stripped    = stripped;
}

#pragma endregion




// This sorts the app vector
void
SKIF_GamingCollection::SortApps (std::vector <std::pair <std::string, app_record_s> > *apps)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // The base sort is by name
  std::stable_sort ( apps->begin (),
                     apps->end   (),
    []( const std::pair <std::string, app_record_s>& a,
        const std::pair <std::string, app_record_s>& b ) -> int
    {
      return a.second.names.all_upper_alnum.compare(
             b.second.names.all_upper_alnum
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
      []( const std::pair <std::string, app_record_s>& a,
          const std::pair <std::string, app_record_s>& b ) -> int
      {
        return a.second.skif.uses >
               b.second.skif.uses;
      }
    );
    break;

  case 2: // Sorting by last used
    PLOG_VERBOSE << "Sorting by last used...";
    std::stable_sort ( apps->begin (),
                       apps->end   (),
      []( const std::pair <std::string, app_record_s>& a,
          const std::pair <std::string, app_record_s>& b ) -> int
      {
        return a.second.skif.used.compare(
               b.second.skif.used
        ) > 0;
      }
    );
    break;
  }

  // Now we sort by the pinned state
  std::stable_sort ( apps->begin (),
                     apps->end   (),
    []( const std::pair <std::string, app_record_s>& a,
        const std::pair <std::string, app_record_s>& b ) -> int
    {
      // Use the highest value between SKIF's pinned value, or 0 / Steam's pinned value if SKIF's is unset
      return std::max (a.second.skif.pinned, (a.second.skif.pinned == -1) ? a.second.steam.shared.favorite : 0) >
             std::max (b.second.skif.pinned, (b.second.skif.pinned == -1) ? b.second.steam.shared.favorite : 0);
    }
  );

  // We need an iterator at the unpinned entries to sort the rest separately
  auto   it  = apps->begin ();
  while (it != apps->end   ())
  {
    auto& item = *it;
    if (std::max (item.second.skif.pinned, (item.second.skif.pinned == -1) ? item.second.steam.shared.favorite : 0) == 0)
      break;

    it++;
  }

  // Then sort unpinned entires by category
  std::stable_sort ( it,
                     apps->end   (),
    []( const std::pair <std::string, app_record_s>& a,
        const std::pair <std::string, app_record_s>& b ) -> int
    {
      return a.second.skif.category.compare(
             b.second.skif.category) < 0;
    }
  );

  // And move all uncategorized entries last
  std::stable_partition ( it,
                          apps->end   (),
    []( const std::pair <std::string, app_record_s>& a ) -> bool
    {
      return ! a.second.skif.category.empty();
    }
  );
}




#pragma region RefreshRunningApps

void
SKIF_GamingCollection::RefreshRunningApps (std::vector <std::pair <std::string, app_record_s> > *apps)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  
  static DWORD lastGameRefresh = 0;
  static std::wstring exeSteam = L"steam.exe";

  extern bool steamRunning;
  extern bool steamFallback;
  extern SKIF_Util_CreateProcess_s iPlayCache[15];

  DWORD current_time = SKIF_Util_timeGetTime ( );

  if (current_time > lastGameRefresh + 5000 && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( )))
  {
    bool new_steamRunning = false;

    for (auto& app : *apps)
    {
      if (app.second._status.dwTimeDelayChecks > current_time)
        continue;

      app.second._status.running_pid = 0;

      if (app.second.store == app_record_s::Store::Steam && (steamRunning || ! steamFallback))
        continue;

      app.second._status.running     = false;
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
            if (! app.second.launch_configs.contains (0))
              continue;

            if (app.second._status.dwTimeDelayChecks > current_time)
              continue;

            // Workaround for Xbox games that run under the virtual folder, e.g. H:\Games\Xbox Games\Hades\Content\Hades.exe, by only checking the presence of the process name
            // TODO: Investigate if this is even really needed any longer? // Aemony, 2023-12-31
            if (app.second.store == app_record_s::Store::Xbox && _wcsnicmp (app.second.launch_configs[0].getExecutableFileName ( ).c_str(), pe32.szExeFile, MAX_PATH) == 0)
            {
              app.second._status.running     = true;
              app.second._status.running_pid = pe32.th32ProcessID;
              break;
            }

            else if (szExePathLen != 0)
            {
              if (app.second.store == app_record_s::Store::Steam)
              {
                if (_wcsnicmp (app.second.launch_configs[0].getExecutableFullPath ( ).c_str(), szExePath, szExePathLen) == 0)
                {
                  app.second._status.running_pid = pe32.th32ProcessID;

                  // Only set the running state if the primary registry monitoring is unavailable
                  if (! steamFallback)
                    continue;

                  app.second._status.running     = true;
                  break;
                }
              }

              // Epic, GOG and SKIF Custom should be straight forward
              else if (_wcsnicmp (app.second.launch_configs[0].getExecutableFullPath ( ).c_str(), szExePath, szExePathLen) == 0) // full patch
              {
                app.second._status.running     = true;
                app.second._status.running_pid = pe32.th32ProcessID;
                break;

                // One can also perform a partial match with the below OR clause in the IF statement, however from testing
                //   PROCESS_QUERY_LIMITED_INFORMATION gives us GetExitCodeProcess() and QueryFullProcessImageName() rights
                //     even to elevated processes, meaning the below OR clause is unnecessary.
                // 
                // (fullPath.empty() && ! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str()))
                //
              }
            }
          }

          if (! _registry.bWarningRTSS    &&
              ! ImGui::IsAnyPopupOpen ( ) &&
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

    lastGameRefresh = current_time;
  }

  
  // Instant Play monitoring...

  for (auto& monitored_app : iPlayCache)
  {
    if (monitored_app.id != 0)
    {
      HANDLE hProcess      = monitored_app.hProcess.load();
      HANDLE hWorkerThread = monitored_app.hWorkerThread.load();
      int    iReturnCode   = monitored_app.iReturnCode.load();

      for (auto& app : *apps)
      {
        if (monitored_app.id       ==      app.second.id &&
            monitored_app.store_id == (int)app.second.store)
        {
          app.second._status.running = 1;

          // Failed start -- let's clean up the wrong data
          if (iReturnCode > 0)
          {
            PLOG_ERROR << "Worker thread for launching app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " failed!";
            app.second._status.running     =  0;

            monitored_app.id               =  0;
            monitored_app.store_id         = -1;
            monitored_app.iReturnCode.store (-1);

            if (hProcess != INVALID_HANDLE_VALUE)
            {
              CloseHandle (hProcess);
              hProcess = INVALID_HANDLE_VALUE;
              monitored_app.hProcess.store(INVALID_HANDLE_VALUE);
            }

            // Clean up these as well if they haven't been done so yet
            if (hWorkerThread != INVALID_HANDLE_VALUE)
            {
              CloseHandle(hWorkerThread);
              hWorkerThread = INVALID_HANDLE_VALUE;
              monitored_app.hWorkerThread.store(INVALID_HANDLE_VALUE);
            }
          }

          // Monitor the external process primarily
          if (hProcess != INVALID_HANDLE_VALUE)
          {
            if (WAIT_OBJECT_0 == WaitForSingleObject (hProcess, 0))
            {
              PLOG_DEBUG << "Game process for app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " has ended!";
              app.second._status.running = 0;

              monitored_app.id              =  0;
              monitored_app.store_id        = -1;

              CloseHandle (hProcess);
              hProcess = INVALID_HANDLE_VALUE;
              monitored_app.hProcess.store(INVALID_HANDLE_VALUE);

              // Clean up these as well if they haven't been done so yet
              if (hWorkerThread != INVALID_HANDLE_VALUE)
              {
                CloseHandle(hWorkerThread);
                hWorkerThread = INVALID_HANDLE_VALUE;
                monitored_app.hWorkerThread.store(INVALID_HANDLE_VALUE);
              }
            }
          }
          
          // If we cannot monitor the game process, monitor the worker thread
          if (hWorkerThread != INVALID_HANDLE_VALUE)
          {
            if (WAIT_OBJECT_0 == WaitForSingleObject (hWorkerThread, 0))
            {
              PLOG_DEBUG << "Worker thread for launching app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " has ended!";

              CloseHandle (hWorkerThread);
              hWorkerThread = INVALID_HANDLE_VALUE;
              monitored_app.hWorkerThread.store(INVALID_HANDLE_VALUE);
            }
          }
        }
      }
    }
  }
}

#pragma endregion


SKIF_GamingCollection::SKIF_GamingCollection (void)
{
  //static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );


}
#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <array>
#include <stores/generic_library2.h>
#include <nlohmann/json.hpp>

// define character size
#define CHAR_SIZE 128

// A Class representing a Trie node
class Trie
{
public:
  bool  isLeaf                = false;
  Trie* character [CHAR_SIZE] = {   };

  // Constructor
  Trie (void)
  {
    this->isLeaf = false;

    for (int i = 0; i < CHAR_SIZE; i++)
      this->character [i] = nullptr;
  }

  void insert       (        const std::string&);
  bool deletion     (Trie*&, const std::string&);
  bool search       (        const std::string&);
  bool haveChildren (Trie const*);
};

// Helper functions
void InsertTrieKey (app_entry_t* app, Trie* labels);

// Singleton struct
struct SKIF_GamingCollection {
  using AppList = app_list_t;

  static SKIF_GamingCollection& Get (void)
  {
      static SKIF_GamingCollection instance;
      return instance;
  }
  static void RefreshRunningApps (app_list_t *apps, bool forced = false);
  static void SortApps           (app_list_t *apps);
  SKIF_GamingCollection (SKIF_GamingCollection const&) = delete; // Delete copy constructor
  SKIF_GamingCollection (SKIF_GamingCollection&&)      = delete; // Delete move constructor

  std::recursive_mutex& GetMutex() { return m_apps_mutex; }

  const AppList& GetApps() const { return m_apps; }
        AppList& GetApps()       { return m_apps; }

        // Looks up a game by its identity. Entries with id 0 (filtered out / invalid) are never returned.
        // Returns nullptr if not found. The pointer stays valid until the next SwapInPopulatedGames ( ).
        app_record_s* Find (app_record_s::Store store, uint32_t appid);

        // Library population (call from the main thread).
        //   PopulateGames ( )        starts the background library worker unless one is already running.
        //   IsPopulating ( )         true while a worker exists (running, or finished but not yet swapped in).
        //   IsPopulateFinished ( )   true once the worker has exited and its results can be swapped in.
        //   SwapInPopulatedGames ( ) replaces the app list with the worker's results, carries over loaded
        //                            icon textures, and returns the number of icon workers it reaped.
        bool PopulateGames        (void);
        bool IsPopulating         (void) const;
        bool IsPopulateFinished   (void) const;
        int  SwapInPopulatedGames (void);

        // Steam app tickets and the keyboard-hint label trie produced by the last population
  std::set <std::string>& GetAppTickets (void) { return m_apptickets; }
  Trie&                   GetLabels     (void) { return m_tLabels;    }

        // Library-wide textures loaded by the library worker
  ID3D11ShaderResourceView* GetPatreonTexture (void)       { return m_pPatTexSRV.p; }
  ID3D11ShaderResourceView* GetSKLogoTexture  (bool bSmall) { return (bSmall) ? m_pSKLogoTexSRV_small.p : m_pSKLogoTexSRV.p; }
  void                      ReleaseTextures   (void);      // Pushes the library-wide textures to SKIF_ResourcesToFree (device reset)

        // Persistent per-game metadata (Assets/db.json)
  size_t MetaDB_CountElements   (void);
  bool   MetaDB_WriteFile       (void);
  bool   MetaDB_UpdateApp       (app_record_s* pApp, bool bWriteToDisk);
  bool   MetaDB_SetCategoryName (std::string szOld, std::string szNew);
  std::recursive_mutex& GetMetaDBMutex (void) { return m_jsonMetaDB_mutex; } // Recursive; hold across multi-step metadata changes

  // Processes started by instant launches (up to 15 at once), monitored for their running state
  std::array <SKIF_Util_CreateProcess_s, 15>& GetInstantPlayProcesses (void) { return m_iPlayCache; }


private:
  SKIF_GamingCollection (void);
  struct worker_s; // Library worker state (defined in games.cpp)
  static unsigned __stdcall LibraryWorkerThread (void* var);

  worker_s*                 m_worker = nullptr;
  Trie                      m_tLabels;
  std::set <std::string>    m_apptickets;
  nlohmann::json            m_jsonMetaDB;
  std::recursive_mutex      m_jsonMetaDB_mutex;
  std::wstring              m_metadataPath;

  CComPtr <ID3D11ShaderResourceView> m_pPatTexSRV;
  CComPtr <ID3D11ShaderResourceView> m_pSKLogoTexSRV;
  CComPtr <ID3D11ShaderResourceView> m_pSKLogoTexSRV_small;
  AppList                   m_apps;
  std::array <SKIF_Util_CreateProcess_s, 15>
                            m_iPlayCache = { };
  std::recursive_mutex      m_apps_mutex; // More specifically, this is intended to guard access to the run state of games.
};
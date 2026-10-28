#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <array>
#include <cstdint>
#include <stores/generic_library2.h>
#include <nlohmann/json.hpp>

// Prefix tree of the library's game names (upper-case ASCII letters, digits and spaces), used by the keyboard hint search.
//   Owns its nodes (freed with the tree); move-only. Characters outside 0-127 are never stored or found.
class Trie
{
public:
  Trie            (void)        = default;
  Trie            (Trie&&)      = default;
  Trie& operator= (Trie&&)      = default;
  Trie            (const Trie&) = delete;
  Trie& operator= (const Trie&) = delete;

  void insert (const std::string& key);       // Adds key; a key with a character outside 0-127 is not added
  bool search (const std::string& key) const; // True if key was added

  static constexpr int CHAR_SIZE = 128; // ASCII

private:
  bool                   isLeaf = false;
  std::unique_ptr <Trie> character [CHAR_SIZE];
};

// Helper functions
void InsertTrieKey (app_entry_t* app, Trie* labels);

// Singleton struct
// Caches of store data that rarely changes but is slow to read (used by the library worker so that a restart doesn't
//   parse it again): small JSON files in <SK user data>\Assets\Cache\. Each one records the size and write time of
//   the file it was read from, so it is only used while that file is unchanged.
bool           SKIF_Library_GetFileStamp (const std::wstring& path, uint64_t& write, uint64_t& size); // false if the file doesn't exist
nlohmann::json SKIF_Library_ReadCache    (const wchar_t* name); // null if missing or not valid JSON
void           SKIF_Library_WriteCache   (const wchar_t* name, const nlohmann::json& data);

// db.json "DefaultLaunch" (games.cpp)
nlohmann::json             SKIF_Library_LaunchRefToJson   (const app_record_s::launch_ref_s& ref);
app_record_s::launch_ref_s SKIF_Library_LaunchRefFromJson (const nlohmann::json& j); // Empty if missing or not valid

// User launch configs: <SK user data>\Assets\lc_user.json, same format as lc.json:
//   { "<store>": { "<GetPersistentKey ( )>": [ { "Desc": "", "Exe": "", "Dir": "", "Args": "", "InstantPlay": 0 }, ... ] } }
//   Exe and Dir are relative to the install folder of the game; InstantPlay is optional (0 / missing = the game's setting).
//   The file is read when the library is populated (games.cpp); these read and write one game's entries for Properties.
struct user_launch_config_s {
  std::string    desc, exe, dir, args;           // UTF-8, as stored (Exe with forward slashes)
  int            instant_play = 0;               // "InstantPlay"; only written when not 0
  nlohmann::json extra        = nlohmann::json::object ( ); // Keys SKIF doesn't know, written back unchanged

  bool operator== (const user_launch_config_s& o) const { return (desc == o.desc && exe == o.exe && dir == o.dir && args == o.args && instant_play == o.instant_play && extra == o.extra); }
  bool operator!= (const user_launch_config_s& o) const { return ! (*this == o); }
};

struct SKIF_UserLaunchConfigs {
  static std::wstring GetPath (void);
  static bool         Get     (const app_record_s& app, std::vector <user_launch_config_s>& out);       // false if the file exists but can't be parsed (out is then empty)
  static bool         Set     (const app_record_s& app, const std::vector <user_launch_config_s>& configs); // Replaces the game's entries (removed if empty); false on failure. Refuses to touch a file it can't parse
};

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

  // Set on the library worker's threads. They fill the worker's own app list, which nothing else sees until
  //   SwapInPopulatedGames ( ), so code they call skips GetMutex ( ) (each thread only touches its own games)
  static inline thread_local bool tl_isLibraryWorker = false;

  // Whether a mouse button is held down in the UI. Set by the main thread every frame before it signals
  //   SKIF_LibRefreshWorker, as RefreshRunningApps ( ) runs on that thread and must not call into ImGui.
  static inline std::atomic <bool> s_bAnyMouseDownUI = false;

  const AppList& GetApps() const { return m_apps; }
        AppList& GetApps()       { return m_apps; }

        // Looks up a game by its identity. Entries with id 0 (filtered out / invalid) are never returned.
        // Returns nullptr if not found. The pointer stays valid until the next SwapInPopulatedGames ( ).
        app_record_s* Find (app_record_s::Store store, uint32_t appid);
        app_record_s* Find (const app_key_s& key) { return Find (key.store, key.appid); }

        // Library population (call from the main thread).
        //   PopulateGames ( )        starts the background library worker unless one is already running.
        //   IsPopulating ( )         true while a worker exists (running, or finished but not yet swapped in).
        //   IsPopulateFinished ( )   true once the worker has exited and its results can be swapped in.
        //   SwapInPopulatedGames ( ) replaces the app list with the worker's results and returns the number of
        //                            icon workers it reaped. Current records whose library data is unchanged
        //                            (app_record_s::HasSameLibraryData) are kept instead of the new ones, and
        //                            replaced records carry over their icon and run state. pChanged is set to
        //                            false if every record was kept, in the same order (nothing shown changes).
        bool PopulateGames        (void);
        bool IsPopulating         (void) const;
        bool IsPopulateFinished   (void) const;
        int  SwapInPopulatedGames (bool* pChanged = nullptr);
        bool HasPopulatedOnce     (void) const { return m_populatedOnce.load ( ); } // A library has been swapped in (any thread)

        // Steam app tickets and the keyboard-hint label trie produced by the last population
  std::set <std::string>& GetAppTickets (void) { return m_apptickets; }
  Trie&                   GetLabels     (void) { return m_tLabels;    }

        // Library-wide textures loaded by the library worker
  ID3D11ShaderResourceView* GetPatreonTexture (void)       { AdoptEmbeddedTextures ( ); return m_pPatTexSRV.p; }
  ID3D11ShaderResourceView* GetSKLogoTexture  (bool bSmall) { AdoptEmbeddedTextures ( ); return (bSmall) ? m_pSKLogoTexSRV_small.p : m_pSKLogoTexSRV.p; }
  void                      ReloadTextures    (void);      // Loads the library-wide textures again on a background thread (after ReleaseTextures)
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

  // The embedded textures above are loaded by a thread of the library worker, which doesn't wait for it (the textures
  //   are created on the hardware device, which may not be ready yet), and handed to the main thread by
  //   AdoptEmbeddedTextures ( ), called by the getters.
  enum EmbeddedTextures { Embedded_None, Embedded_Loading, Embedded_Loaded, Embedded_Adopted };
  std::atomic <int>                  m_embeddedState = Embedded_None;
  std::mutex                         m_embeddedMutex;
  CComPtr <ID3D11ShaderResourceView> m_pPatTexSRV_loaded;
  CComPtr <ID3D11ShaderResourceView> m_pSKLogoTexSRV_loaded;
  CComPtr <ID3D11ShaderResourceView> m_pSKLogoTexSRV_small_loaded;
  std::atomic <unsigned int>         m_embeddedGeneration = 0;     // Bumped by ReleaseTextures ( ): loads that started before it were made on the old device
  void                               LoadEmbeddedTextures  (void); // Any thread: loads them (expects Embedded_Loading) and hands them over
  void                               AdoptEmbeddedTextures (void); // Main thread
  CComPtr <ID3D11ShaderResourceView> m_pSKLogoTexSRV_small;
  AppList                   m_apps;
  std::atomic <bool>        m_populatedOnce = false;
  std::array <SKIF_Util_CreateProcess_s, 15>
                            m_iPlayCache = { };
  std::recursive_mutex      m_apps_mutex; // More specifically, this is intended to guard access to the run state of games.
};
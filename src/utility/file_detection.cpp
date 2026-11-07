// Detection of the technology a game uses from the names of its files: the background service (see utility/file_detection.h)
//
//   One worker thread (SKIF_FileDetection) does everything that blocks: downloading rules.ini, reading the cache,
//     checking whether a cached result still applies, and scanning folders. Get ( ) only looks up and queues.
//
//   Cache (Assets\Cache\FileDetection.json):
//     { "version": 1, "rules": "<hash>",
//       "folders": { "<folder, lowercase>": { "stamp": "<hash>", "scanned": <UNIX time>, "entries": n, "truncated": b,
//                                             "detections": [ { "id": "Engine.Unity", "matches": n, "example": "...",
//                                                               "deduced": b } ] } } }
//     The stamp covers the folder's top level (names, sizes, and times of the last change), which changes when a game
//       is updated in most cases. A cached result is used as long as the stamp and the rules are the same, for 30 days.

#include <utility/file_detection.h>

#include <SKIF.h>
#include <utility/utility.h>
#include <utility/sk_utility.h>
#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/games.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <process.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

extern std::atomic<bool> SKIF_Shutdown; // SKIF.cpp

namespace SKIF_FileDetection
{
  const char*
  GetCategoryLabel (Category category)
  {
    switch (category)
    {
      // TRANSLATORS: The game engine detected from a game's files (e.g. Unity)
      case Category::Engine:    return _("Engine");
      case Category::Evidence:  return _("Evidence");
      // TRANSLATORS: Software that wraps a game (e.g. Electron)
      case Category::Container: return _("Container");
      case Category::Emulator:  return _("Emulator");
      case Category::AntiCheat: return _("Anti-cheat");
      // TRANSLATORS: Libraries and SDKs detected from a game's files (e.g. FMOD, NVIDIA DLSS)
      case Category::SDK:       return _("SDKs");
      case Category::Launcher:  return _("Launcher");
      case Category::Other:     break;
    }

    return _("Other");
  }
}

namespace
{
  using namespace SKIF_FileDetection;

  constexpr const wchar_t* RULES_URL       = L"https://raw.githubusercontent.com/SteamDatabase/FileDetectionRuleSets/main/rules.ini";
  constexpr const wchar_t* RULES_FILE      = L"FileDetectionRules.ini"; // Assets\Cache
  constexpr const wchar_t* CACHE_FILE      = L"FileDetection.json";     // Assets\Cache (SKIF_Library_ReadCache ( ))
  constexpr int            CACHE_VERSION   = 1;
  constexpr size_t         MIN_RULES       = 50;                        // A download with fewer rules is not used

  constexpr int64_t        RULES_MAX_AGE   =  7 * 24 * 60 * 60;         // Seconds
  constexpr int64_t        RESULT_MAX_AGE  = 30 * 24 * 60 * 60;
  constexpr DWORD          RULES_RETRY     = 10 * 60 * 1000;            // ms before trying to get the rules again after a failure

  struct entry_s {
    std::wstring  folder;              // As requested (the key is lowercase)
    result_ptr    result;
    uint64_t      stamp     = 0;       // Of the folder when it was scanned (0 = unknown)
    uint64_t      rulesHash = 0;       // Of the rules it was scanned with
    bool          checked   = false;   // Checked against the folder and the rules this session
    bool          queued    = false;
    bool          forced    = false;   // Rescan ( ): scan even if the stamp is the same
    DWORD         noRulesAt = 0;       // SKIF_Util_timeGetTime ( ) of a NoRules result, which is retried after RULES_RETRY
  };

  struct service_s {
    std::mutex                                  mutex;
    std::condition_variable                     wake;        // The worker: something was queued
    std::condition_variable                     done;        // Wait ( ): a result was stored
    std::unordered_map <std::wstring, entry_s>  entries;     // By lowercase folder
    std::deque <std::wstring>                   queue;       // Keys

    std::shared_ptr <RuleSet>                   rules;       // Worker thread only
    bool                                        rulesTried      = false; // Worker thread only
    DWORD                                       rulesFailedAt   = 0;     // Worker thread only
    std::atomic <bool>                          refreshRules    = false;

    bool                                        cacheLoaded     = false; // Under mutex
    uint64_t                                    cacheRulesHash  = 0;     // Under mutex: the rules of the cached results
    bool                                        cacheDirty      = false; // Worker thread only
    bool                                        threadStarted   = false; // Under mutex
  };

  service_s& _Service (void)
  {
    static service_s service;
    return service;
  }

  std::wstring
  _MakeKey (std::wstring folder)
  {
    while (! folder.empty ( ) && (folder.back ( ) == L'\\' || folder.back ( ) == L'/'))
      folder.pop_back ( );

    std::replace (folder.begin ( ), folder.end ( ), L'/', L'\\');

    if (! folder.empty ( ))
      CharLowerBuffW (folder.data ( ), static_cast <DWORD> (folder.size ( )));

    return folder;
  }

  std::wstring
  _ExtendedPath (const std::wstring& path)
  {
    if (path.starts_with (LR"(\\?\)"))
      return path;

    if (path.starts_with (LR"(\\)"))
      return LR"(\\?\UNC\)" + path.substr (2);

    return LR"(\\?\)" + path;
  }

  int64_t
  _Now (void)
  {
    return static_cast <int64_t> (time (nullptr));
  }

  int64_t
  _FileTimeToUnix (const FILETIME& ft)
  {
    ULARGE_INTEGER uli { };
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    return static_cast <int64_t> (uli.QuadPart / 10000000ULL) - 11644473600LL;
  }

  std::string
  _ToHex (uint64_t value)
  {
    return SK_FormatString ("%016llx", static_cast <unsigned long long> (value));
  }

  uint64_t
  _FromHex (const std::string& text)
  {
    try {
      return std::stoull (text, nullptr, 16);
    }
    catch (...) {
      return 0;
    }
  }

  void
  _Mix (uint64_t& hash, const void* data, size_t size)
  {
    const unsigned char* bytes = static_cast <const unsigned char*> (data);

    for (size_t i = 0; i < size; i++)
    {
      hash ^= bytes [i];
      hash *= 1099511628211ULL;
    }
  }

  // The top level of a folder: names, sizes and last write times (0 if it can't be read)
  uint64_t
  _GetFolderStamp (const std::wstring& folder)
  {
    WIN32_FILE_ATTRIBUTE_DATA root { };

    if (! GetFileAttributesExW (_ExtendedPath (folder).c_str ( ), GetFileExInfoStandard, &root) ||
        ! (root.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      return 0;

    uint64_t hash = 14695981039346656037ULL;
    _Mix (hash, &root.ftLastWriteTime, sizeof (FILETIME));

    WIN32_FIND_DATAW ffd { };
    HANDLE hFind = FindFirstFileExW (_ExtendedPath (folder + L"\\*").c_str ( ), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

    if (hFind != INVALID_HANDLE_VALUE)
    {
      do
      {
        if (wcscmp (ffd.cFileName, L".") == 0 || wcscmp (ffd.cFileName, L"..") == 0)
          continue;

        _Mix (hash, ffd.cFileName,        wcslen (ffd.cFileName) * sizeof (wchar_t));
        _Mix (hash, &ffd.ftLastWriteTime, sizeof (FILETIME));
        _Mix (hash, &ffd.nFileSizeLow,    sizeof (DWORD));
        _Mix (hash, &ffd.nFileSizeHigh,   sizeof (DWORD));
      } while (FindNextFileW (hFind, &ffd));

      FindClose (hFind);
    }

    return (hash != 0) ? hash : 1;
  }

  // Walks a folder and matches every file and folder in it
  result_s
  _ScanFolder (const std::wstring& folder, const RuleSet& rules)
  {
    result_s result;
    result.scanned = _Now ( );

    DWORD dwAttr = GetFileAttributesW (_ExtendedPath (folder).c_str ( ));

    if (dwAttr == INVALID_FILE_ATTRIBUTES || ! (dwAttr & FILE_ATTRIBUTE_DIRECTORY))
    {
      result.state = result_s::State::Failed;
      return result;
    }

    RuleSet::Matcher matcher (rules);

    struct pending_s {
      std::wstring path;     // Absolute
      std::string  relative; // UTF-8, / separators
      int          depth;
    };

    std::vector <pending_s> stack;
    stack.push_back ({ folder, "", 0 });

    uint32_t entries  = 0;
    bool     rootRead = false;

    while (! stack.empty ( ) && ! result.truncated)
    {
      pending_s current = std::move (stack.back ( ));
      stack.pop_back ( );

      WIN32_FIND_DATAW ffd { };
      HANDLE hFind = FindFirstFileExW (_ExtendedPath (current.path + L"\\*").c_str ( ), FindExInfoBasic, &ffd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

      if (hFind == INVALID_HANDLE_VALUE)
        continue;

      if (current.depth == 0)
        rootRead = true;

      do
      {
        if (wcscmp (ffd.cFileName, L".") == 0 || wcscmp (ffd.cFileName, L"..") == 0)
          continue;

        if (entries >= kMaxEntries)
        {
          result.truncated = true;
          break;
        }

        const bool isFolder = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::string relative = SK_WideCharToUTF8 (ffd.cFileName);

        if (! current.relative.empty ( ))
          relative = current.relative + "/" + relative;

        matcher.Add (relative, isFolder);
        entries++;

        // Junctions and symbolic links are not followed: they may point outside the game, or loop
        if (isFolder && ! (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && current.depth + 1 < kMaxDepth)
          stack.push_back ({ current.path + L"\\" + ffd.cFileName, std::move (relative), current.depth + 1 });

        if ((entries & 0x3FF) == 0 && SKIF_Shutdown.load ( ))
          break;
      } while (FindNextFileW (hFind, &ffd));

      FindClose (hFind);

      if (SKIF_Shutdown.load ( ))
        break;
    }

    if (! rootRead)
    {
      result.state = result_s::State::Failed;
      return result;
    }

    const bool truncated = result.truncated;
    const int64_t scanned = result.scanned;

    result           = matcher.Finish ( );
    result.entries   = entries;
    result.truncated = truncated;
    result.scanned   = scanned;

    return result;
  }

  // Worker thread. Downloads rules.ini if needed, then loads it; false if there are no rules to use
  bool
  _LoadRules (service_s& svc)
  {
    static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    const bool refresh = svc.refreshRules.exchange (false);

    if (svc.rules != nullptr && ! refresh)
      return true;

    if (svc.rules == nullptr && svc.rulesTried && SKIF_Util_timeGetTime ( ) - svc.rulesFailedAt < RULES_RETRY && ! refresh)
      return false;

    svc.rulesTried = true;

    const std::wstring cacheDir = SK_FormatStringW (LR"(%ws\Assets\Cache)", _path_cache.specialk_userdata);
    const std::wstring path     = cacheDir + L"\\" + RULES_FILE;

    CreateDirectoryW (SK_FormatStringW (LR"(%ws\Assets)", _path_cache.specialk_userdata).c_str ( ), nullptr);
    CreateDirectoryW (cacheDir.c_str ( ), nullptr);

    WIN32_FILE_ATTRIBUTE_DATA attr { };
    const bool exists = GetFileAttributesExW (path.c_str ( ), GetFileExInfoStandard, &attr) != FALSE;
    const bool old    = exists && (_Now ( ) - _FileTimeToUnix (attr.ftLastWriteTime) > RULES_MAX_AGE);

    // A missing file is always downloaded (unless in low bandwidth mode); an old one only if update checks are on
    if (! _registry.bLowBandwidthMode && (! exists || refresh || (old && _registry.iCheckForUpdates != 0)))
    {
      const std::wstring temp = path + L".tmp";

      PLOG_INFO << "Downloading the file detection rules (" << RULES_URL << ")...";

      DeleteFileW (temp.c_str ( ));

      if (SKIF_Util_GetWebResource (RULES_URL, temp))
      {
        std::ifstream file (temp, std::ios::binary);
        std::string   data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));
        file.close ( );

        RuleSet test;

        if (test.Parse (data) && test.GetRuleCount ( ) >= MIN_RULES)
        {
          if (! MoveFileExW (temp.c_str ( ), path.c_str ( ), MOVEFILE_REPLACE_EXISTING))
            PLOG_WARNING << "Could not replace " << path;
        }

        else
          PLOG_ERROR << "The downloaded file detection rules are not usable (" << test.GetRuleCount ( ) << " rules); keeping the previous ones";
      }

      else
        PLOG_ERROR << "Failed to download the file detection rules";

      DeleteFileW (temp.c_str ( ));
    }

    std::ifstream file (path, std::ios::binary);

    if (! file.is_open ( ))
    {
      PLOG_WARNING << "No file detection rules are available";
      svc.rulesFailedAt = SKIF_Util_timeGetTime ( );
      return (svc.rules != nullptr);
    }

    std::string data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));

    auto rules = std::make_shared <RuleSet> ( );
    std::vector <std::string> errors;

    if (! rules->Parse (data, &errors))
    {
      PLOG_ERROR << "Could not parse the file detection rules in " << path;
      svc.rulesFailedAt = SKIF_Util_timeGetTime ( );
      return (svc.rules != nullptr);
    }

    for (const auto& error : errors)
      PLOG_WARNING << "Skipped file detection rule " << error;

    PLOG_INFO << "Loaded " << rules->GetRuleCount ( ) << " file detection rules (" << _ToHex (rules->GetHash ( )) << ")";

    svc.rules = std::move (rules);

    return true;
  }

  nlohmann::json
  _ResultToJson (const entry_s& entry)
  {
    nlohmann::json detections = nlohmann::json::array ( );

    for (const auto& detection : entry.result->detections)
    {
      nlohmann::json item = { { "id", detection.id }, { "matches", detection.matches } };

      if (! detection.example.empty ( ))
        item ["example"] = detection.example;

      if (detection.deduced)
        item ["deduced"] = true;

      detections.push_back (std::move (item));
    }

    return {
      { "folder",     SK_WideCharToUTF8 (entry.folder) },
      { "stamp",      _ToHex (entry.stamp) },
      { "scanned",    entry.result->scanned },
      { "entries",    entry.result->entries },
      { "truncated",  entry.result->truncated },
      { "detections", std::move (detections) }
    };
  }

  // Under the mutex. Reads the cache file once; its results show until they are checked
  void
  _LoadCache (service_s& svc)
  {
    if (svc.cacheLoaded)
      return;

    svc.cacheLoaded = true;

    nlohmann::json json = SKIF_Library_ReadCache (CACHE_FILE);

    if (! json.is_object ( ) || json.value ("version", 0) != CACHE_VERSION || ! json.contains ("folders") || ! json ["folders"].is_object ( ))
      return;

    svc.cacheRulesHash = _FromHex (json.value ("rules", std::string ( )));

    for (const auto& [key, item] : json ["folders"].items ( ))
    {
      if (! item.is_object ( ) || ! item.contains ("detections") || ! item ["detections"].is_array ( ))
        continue;

      auto result = std::make_shared <result_s> ( );
      result->state     = result_s::State::Done;
      result->stale     = true;
      result->scanned   = item.value ("scanned",   int64_t (0));
      result->entries   = item.value ("entries",   uint32_t (0));
      result->truncated = item.value ("truncated", false);

      for (const auto& d : item ["detections"])
      {
        if (! d.is_object ( ) || ! d.contains ("id") || ! d ["id"].is_string ( ))
          continue;

        detection_s detection;
        detection.id       = d ["id"].get <std::string> ( );

        const size_t dot   = detection.id.find ('.');

        if (dot == std::string::npos)
          continue;

        detection.name     = detection.id.substr (dot + 1);
        detection.category = CategoryFromSection (std::string_view (detection.id).substr (0, dot));
        detection.matches  = d.value ("matches", uint32_t (0));
        detection.example  = d.value ("example", std::string ( ));
        detection.deduced  = d.value ("deduced", false);

        result->detections.push_back (std::move (detection));
      }

      entry_s entry;
      entry.folder    = SK_UTF8ToWideChar (item.value ("folder", key));
      entry.result    = std::move (result);
      entry.stamp     = _FromHex (item.value ("stamp", std::string ( )));
      entry.rulesHash = svc.cacheRulesHash;

      svc.entries.emplace (_MakeKey (SK_UTF8ToWideChar (key)), std::move (entry));
    }

    PLOG_VERBOSE << "Loaded " << svc.entries.size ( ) << " cached file detection results";
  }

  // Worker thread
  void
  _WriteCache (service_s& svc)
  {
    nlohmann::json folders = nlohmann::json::object ( );
    uint64_t       rulesHash = 0;

    {
      std::scoped_lock lock (svc.mutex);

      rulesHash = (svc.rules != nullptr) ? svc.rules->GetHash ( ) : svc.cacheRulesHash;

      for (const auto& [key, entry] : svc.entries)
      {
        // Only results of the current rules (others will be scanned again anyway)
        if (entry.result == nullptr || entry.result->state != result_s::State::Done || entry.rulesHash != rulesHash)
          continue;

        folders [SK_WideCharToUTF8 (key)] = _ResultToJson (entry);
      }

      svc.cacheRulesHash = rulesHash;
    }

    SKIF_Library_WriteCache (CACHE_FILE, {
      { "version", CACHE_VERSION },
      { "rules",   _ToHex (rulesHash) },
      { "folders", std::move (folders) }
    });

    svc.cacheDirty = false;
  }

  void
  _WakeMainThread (void)
  {
    if (SKIF_Notify_hWnd != NULL)
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_FILEDETECTION, 0x0, 0x0);
  }

  // Worker thread: one queued folder
  void
  _Process (service_s& svc, const std::wstring& key)
  {
    std::wstring folder;
    bool         forced    = false;
    uint64_t     stamp     = 0,
                 rulesHash = 0;
    result_ptr   previous;

    {
      std::scoped_lock lock (svc.mutex);

      auto it = svc.entries.find (key);

      if (it == svc.entries.end ( ))
        return;

      folder    = it->second.folder;
      forced    = it->second.forced;
      stamp     = it->second.stamp;
      rulesHash = it->second.rulesHash;
      previous  = it->second.result;

      it->second.forced = false;
    }

    auto _Store = [&](result_s&& result, uint64_t newStamp, uint64_t newRulesHash)
    {
      std::scoped_lock lock (svc.mutex);

      auto it = svc.entries.find (key);

      if (it == svc.entries.end ( ))
        return;

      it->second.result    = std::make_shared <const result_s> (std::move (result));
      it->second.stamp     = newStamp;
      it->second.rulesHash = newRulesHash;
      it->second.checked   = true;
      it->second.queued    = false;
      it->second.noRulesAt = (it->second.result->state == result_s::State::NoRules) ? SKIF_Util_timeGetTime ( ) : 0;

      svc.done.notify_all ( );
    };

    if (! _LoadRules (svc))
    {
      result_s result;
      result.state = result_s::State::NoRules;
      _Store (std::move (result), 0, 0);
      return;
    }

    const uint64_t currentRules = svc.rules->GetHash ( );
    const uint64_t currentStamp = _GetFolderStamp (folder);

    // The cached result still applies
    if (! forced && previous != nullptr && previous->state == result_s::State::Done &&
        currentStamp != 0 && currentStamp == stamp && rulesHash == currentRules &&
        _Now ( ) - previous->scanned < RESULT_MAX_AGE)
    {
      result_s result = *previous;
      result.stale    = false;
      _Store (std::move (result), stamp, rulesHash);
      return;
    }

    const DWORD dwStart = SKIF_Util_timeGetTime1 ( );

    result_s result = _ScanFolder (folder, *svc.rules);

    if (result.state == result_s::State::Done)
    {
      PLOG_VERBOSE << "File detection: " << folder << ": " << result.entries << " entries, " << result.detections.size ( )
                   << " detections in " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms" << (result.truncated ? " (truncated)" : "");
      svc.cacheDirty = true;
    }

    else
      PLOG_VERBOSE << "File detection: could not read " << folder;

    _Store (std::move (result), currentStamp, currentRules);
  }

  unsigned __stdcall
  _WorkerThread (void*)
  {
    SKIF_Util_SetThreadDescription     (GetCurrentThread (), L"SKIF_FileDetection");
    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // EcoQoS
    SetThreadPriority                  (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    service_s& svc = _Service ( );

    while (! SKIF_Shutdown.load ( ))
    {
      std::wstring key;

      {
        std::unique_lock lock (svc.mutex);

        // Wakes up now and then to notice the shutdown
        svc.wake.wait_for (lock, std::chrono::seconds (5), [&] { return ! svc.queue.empty ( ) || SKIF_Shutdown.load ( ); });

        if (svc.queue.empty ( ))
          continue;

        key = std::move (svc.queue.front ( ));
        svc.queue.pop_front ( );
      }

      _Process (svc, key);
      _WakeMainThread ( );

      bool idle = false;

      {
        std::scoped_lock lock (svc.mutex);
        idle = svc.queue.empty ( );
      }

      // Written once the queue is done, not after each folder
      if (idle && svc.cacheDirty)
        _WriteCache (svc);
    }

    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    return 0;
  }

  // Under the mutex
  void
  _Queue (service_s& svc, const std::wstring& key, entry_s& entry, bool front)
  {
    if (! entry.queued)
    {
      entry.queued = true;

      if (front)
        svc.queue.push_front (key);
      else
        svc.queue.push_back  (key);
    }

    if (! svc.threadStarted)
    {
      svc.threadStarted = true;

      HANDLE hThread = (HANDLE)_beginthreadex (nullptr, 0x0, _WorkerThread, nullptr, 0x0, nullptr);

      if (hThread != NULL)
        CloseHandle (hThread);
      else
        svc.threadStarted = false;
    }

    svc.wake.notify_one ( );
  }

  result_ptr
  _Pending (void)
  {
    static result_ptr pending = []
    {
      auto r = std::make_shared <result_s> ( );
      r->state = result_s::State::Pending;
      return r;
    } ( );

    return pending;
  }

  result_ptr
  _None (void)
  {
    static result_ptr none = std::make_shared <const result_s> ( );
    return none;
  }
}

namespace SKIF_FileDetection
{
  result_ptr
  Get (const std::wstring& folder, bool request)
  {
    if (folder.empty ( ))
      return _None ( );

    service_s& svc = _Service ( );
    const std::wstring key = _MakeKey (folder);

    std::scoped_lock lock (svc.mutex);

    _LoadCache (svc);

    auto it = svc.entries.find (key);

    if (it == svc.entries.end ( ))
    {
      if (! request)
        return _None ( );

      entry_s entry;
      entry.folder = folder;
      entry.result = _Pending ( );

      it = svc.entries.emplace (key, std::move (entry)).first;
    }

    entry_s& entry = it->second;

    // Not yet checked this session (a cached result), or a NoRules result that may be retried (not on every frame)
    const bool retryNoRules = (entry.result->state == result_s::State::NoRules &&
                               SKIF_Util_timeGetTime ( ) - entry.noRulesAt >= RULES_RETRY);

    if (request && ! entry.queued && (! entry.checked || retryNoRules))
    {
      if (entry.result->state == result_s::State::NoRules)
        entry.result = _Pending ( );

      // The game being looked at goes first
      _Queue (svc, key, entry, true);
    }

    return entry.result;
  }

  result_ptr
  Wait (const std::wstring& folder, uint32_t timeoutMs)
  {
    result_ptr result = Get (folder, true);

    if (result->state != result_s::State::Pending)
      return result;

    service_s& svc = _Service ( );
    const std::wstring key = _MakeKey (folder);

    std::unique_lock lock (svc.mutex);

    auto _Ready = [&](void) -> bool
    {
      auto it = svc.entries.find (key);
      return (it == svc.entries.end ( ) || it->second.result->state != result_s::State::Pending);
    };

    svc.done.wait_for (lock, std::chrono::milliseconds (timeoutMs), _Ready);

    auto it = svc.entries.find (key);

    return (it != svc.entries.end ( )) ? it->second.result : _None ( );
  }

  result_ptr
  FindCached (const std::wstring& path)
  {
    if (path.empty ( ))
      return _None ( );

    service_s& svc = _Service ( );
    const std::wstring key = _MakeKey (path);

    std::scoped_lock lock (svc.mutex);

    _LoadCache (svc);

    result_ptr best;
    size_t     bestLength = 0;

    for (const auto& [folder, entry] : svc.entries)
    {
      if (entry.result == nullptr || entry.result->state != result_s::State::Done || folder.empty ( ) || folder.size ( ) <= bestLength)
        continue;

      // The folder itself, or one of its files or subfolders
      if (key.size ( ) < folder.size ( ) || key.compare (0, folder.size ( ), folder) != 0)
        continue;

      if (key.size ( ) > folder.size ( ) && key [folder.size ( )] != L'\\')
        continue;

      best       = entry.result;
      bestLength = folder.size ( );
    }

    return (best != nullptr) ? best : _None ( );
  }

  void
  Rescan (const std::wstring& folder)
  {
    if (folder.empty ( ))
      return;

    service_s& svc = _Service ( );
    const std::wstring key = _MakeKey (folder);

    std::scoped_lock lock (svc.mutex);

    _LoadCache (svc);

    auto it = svc.entries.find (key);

    if (it == svc.entries.end ( ))
    {
      entry_s entry;
      entry.folder = folder;
      it = svc.entries.emplace (key, std::move (entry)).first;
    }

    it->second.result  = _Pending ( );
    it->second.forced  = true;
    it->second.checked = false;

    _Queue (svc, key, it->second, true);
  }

  void
  RefreshRules (void)
  {
    service_s& svc = _Service ( );

    svc.refreshRules.store (true);

    std::scoped_lock lock (svc.mutex);

    // Everything is checked again the next time it is requested (with the new rules, a scan if they changed)
    for (auto& [key, entry] : svc.entries)
      if (! entry.queued)
        entry.checked = false;
  }

  result_s
  ScanSync (const std::wstring& folder)
  {
    std::shared_ptr <RuleSet> rules;

    {
      // _LoadRules ( ) is meant for the worker thread; a separate copy keeps this call independent of it
      static std::mutex                syncMutex;
      static std::shared_ptr <RuleSet> syncRules;

      std::scoped_lock lock (syncMutex);

      if (syncRules == nullptr)
      {
        static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

        std::ifstream file (SK_FormatStringW (LR"(%ws\Assets\Cache\%ws)", _path_cache.specialk_userdata, RULES_FILE), std::ios::binary);

        if (file.is_open ( ))
        {
          std::string data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));

          auto parsed = std::make_shared <RuleSet> ( );

          if (parsed->Parse (data))
            syncRules = std::move (parsed);
        }
      }

      rules = syncRules;
    }

    if (rules == nullptr)
    {
      result_s result;
      result.state = result_s::State::NoRules;
      return result;
    }

    return _ScanFolder (folder, *rules);
  }
}

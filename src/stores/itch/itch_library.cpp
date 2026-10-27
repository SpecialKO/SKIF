#include <stores/itch/itch_library.h>
#include <nlohmann/json.hpp>

#include <wtypes.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <mutex>
#include <set>

#include <utility/registry.h>
#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include <utility/fsutil.h>
#include <utility/igdb.h>
#include <stores/generic_library2.h>

// See itch_library.h for how itch.io games are found and launched

#pragma region SQLite

namespace
{
  // The subset of the SQLite C API used here, from the copy of SQLite that ships with Windows (winsqlite3.dll).
  //   Its exports use the stdcall calling convention (relevant for 32-bit builds).
  struct sqlite3;
  struct sqlite3_stmt;

  constexpr int SQLITE_OK            = 0;
  constexpr int SQLITE_ROW           = 100;
  constexpr int SQLITE_OPEN_READONLY = 0x00000001;

  struct itch_sqlite_s {
    int                  (__stdcall *open_v2)      (const char* filename, sqlite3** ppDb, int flags, const char* zVfs) = nullptr;
    int                  (__stdcall *close)        (sqlite3*)                                                         = nullptr;
    int                  (__stdcall *busy_timeout) (sqlite3*, int ms)                                                 = nullptr;
    int                  (__stdcall *prepare_v2)   (sqlite3*, const char* zSql, int nByte, sqlite3_stmt** ppStmt, const char** pzTail) = nullptr;
    int                  (__stdcall *step)         (sqlite3_stmt*)                                                    = nullptr;
    int                  (__stdcall *finalize)     (sqlite3_stmt*)                                                    = nullptr;
    const unsigned char* (__stdcall *column_text)  (sqlite3_stmt*, int iCol)                                          = nullptr;
    long long            (__stdcall *column_int64) (sqlite3_stmt*, int iCol)                                          = nullptr;
    const char*          (__stdcall *errmsg)       (sqlite3*)                                                         = nullptr;

    bool valid = false;
  };

  // Loads winsqlite3.dll from System32 on first use (thread-safe); nullptr if unavailable (Windows 8.1 and older)
  const itch_sqlite_s*
  SKIF_itch_GetSQLite (void)
  {
    static const itch_sqlite_s sqlite = [ ]
    {
      itch_sqlite_s api;

      HMODULE hModule = LoadLibraryExW (L"winsqlite3.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

      if (hModule == NULL)
      {
        PLOG_WARNING << "winsqlite3.dll is not available; itch.io games cannot be listed.";
        return api;
      }

#define SKIF_ITCH_SQLITE_FN(fn) \
      api.fn = reinterpret_cast <decltype (api.fn)> (GetProcAddress (hModule, "sqlite3_" #fn))

      SKIF_ITCH_SQLITE_FN (open_v2);
      SKIF_ITCH_SQLITE_FN (close);
      SKIF_ITCH_SQLITE_FN (busy_timeout);
      SKIF_ITCH_SQLITE_FN (prepare_v2);
      SKIF_ITCH_SQLITE_FN (step);
      SKIF_ITCH_SQLITE_FN (finalize);
      SKIF_ITCH_SQLITE_FN (column_text);
      SKIF_ITCH_SQLITE_FN (column_int64);
      SKIF_ITCH_SQLITE_FN (errmsg);

#undef SKIF_ITCH_SQLITE_FN

      api.valid = (api.open_v2    && api.close    && api.busy_timeout && api.prepare_v2 && api.step &&
                   api.finalize   && api.column_text && api.column_int64 && api.errmsg);

      if (! api.valid)
        PLOG_WARNING << "winsqlite3.dll is missing expected exports; itch.io games cannot be listed.";

      return api;
    } ( );

    return (sqlite.valid) ? &sqlite : nullptr;
  }

  // A read-only connection to butler.db, closed when it goes out of scope
  class itch_db_c {
  public:
    explicit itch_db_c (const std::wstring& path)
    {
      // Checked first, so winsqlite3.dll is only loaded when the itch app has a database
      if (! PathFileExistsW (path.c_str ( )))
        return;

      _sql = SKIF_itch_GetSQLite ( );

      if (_sql == nullptr)
        return;

      // Opened read-only, but not immutable: the data is in the WAL, which SQLite applies through butler.db-shm
      if (_sql->open_v2 (SK_WideCharToUTF8 (path).c_str ( ), &_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
      {
        PLOG_WARNING << "Could not open the itch database " << path << ": " << ((_db != nullptr) ? _sql->errmsg (_db) : "out of memory");

        if (_db != nullptr)
          _sql->close (_db);

        _db = nullptr;
        return;
      }

      // butler may be writing (or checkpointing) at the same time
      _sql->busy_timeout (_db, 2000);
    }

   ~itch_db_c (void)
    {
      if (_db != nullptr)
        _sql->close (_db);
    }

    itch_db_c            (const itch_db_c&) = delete;
    itch_db_c& operator= (const itch_db_c&) = delete;

    bool isOpen (void) const { return (_db != nullptr); }

    // Runs a query and calls fn (stmt) for every row; false if the query failed
    bool query (const char* sql, const std::function <void (sqlite3_stmt*)>& fn)
    {
      if (_db == nullptr)
        return false;

      sqlite3_stmt* stmt = nullptr;

      if (_sql->prepare_v2 (_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
      {
        PLOG_WARNING << "itch database query failed: " << _sql->errmsg (_db);
        return false;
      }

      int rc;
      while ((rc = _sql->step (stmt)) == SQLITE_ROW)
        fn (stmt);

      _sql->finalize (stmt);

      return true;
    }

    std::string text (sqlite3_stmt* stmt, int col) const
    {
      const unsigned char* str = _sql->column_text (stmt, col);
      return (str != nullptr) ? reinterpret_cast <const char*> (str) : "";
    }

    int64_t int64 (sqlite3_stmt* stmt, int col) const
    {
      return _sql->column_int64 (stmt, col);
    }

  private:
    const itch_sqlite_s* _sql = nullptr;
    sqlite3*             _db  = nullptr;
  };
}

#pragma endregion

#pragma region Helpers

namespace
{
  std::wstring SKIF_itch_NormalizePath (std::wstring path);
  // The stable app and the canary app (kitch), which can be installed side by side with separate data folders.
  //   Stable comes first, so a game installed in both is listed from the stable app.
  constexpr const char* ITCH_APPS [] = { "itch", "kitch" };

  // %APPDATA%\<app>\db\butler.db
  std::wstring
  SKIF_itch_GetDatabasePath (const std::string& app)
  {
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    return SK_FormatStringW (LR"(%ws\%hs\db\butler.db)", _path_cache.app_data_roaming.path, app.c_str ( ));
  }

  // <install folder>\itch-setup.exe: the folder itch-setup recorded in its uninstall entry
  //   (HKCU\...\Uninstall\<app>\InstallLocation), else its default %LOCALAPPDATA%\<app>
  std::wstring
  SKIF_itch_GetSetupPath (const std::string& app)
  {
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    std::wstring installDir;
    std::wstring subKey = SK_FormatStringW (LR"(Software\Microsoft\Windows\CurrentVersion\Uninstall\%hs)", app.c_str ( ));
    DWORD        dwSize = 0;

    if (RegGetValueW (HKEY_CURRENT_USER, subKey.c_str ( ), L"InstallLocation", RRF_RT_REG_SZ, NULL, NULL, &dwSize) == ERROR_SUCCESS && dwSize > sizeof (wchar_t))
    {
      installDir.resize (dwSize / sizeof (wchar_t));

      if (RegGetValueW (HKEY_CURRENT_USER, subKey.c_str ( ), L"InstallLocation", RRF_RT_REG_SZ, NULL, installDir.data ( ), &dwSize) == ERROR_SUCCESS)
        installDir.resize (wcsnlen (installDir.c_str ( ), installDir.size ( )));
      else
        installDir.clear ( );
    }

    if (installDir.empty ( ))
      installDir = SK_FormatStringW (LR"(%ws\%hs)", _path_cache.app_data_local.path, app.c_str ( ));

    return SKIF_itch_NormalizePath (installDir) + L"\\itch-setup.exe";
  }

  // Uses backslashes, and removes the trailing backslash (except for a drive root)
  std::wstring
  SKIF_itch_NormalizePath (std::wstring path)
  {
    if (path.empty ( ))
      return L"";

    std::replace (path.begin ( ), path.end ( ), L'/', L'\\');

    path = std::filesystem::path (path).lexically_normal ( ).wstring ( );

    while (path.size ( ) > 3 && path.back ( ) == L'\\')
      path.pop_back ( );

    return path;
  }

  // <dir>\<file>, without doubled backslashes
  std::wstring
  SKIF_itch_CombinePath (const std::wstring& dir, std::wstring file)
  {
    std::replace (file.begin ( ), file.end ( ), L'/', L'\\');

    while (! file.empty ( ) && file.front ( ) == L'\\')
      file.erase (0, 1);

    if (dir.empty ( ) || file.empty ( ))
      return SKIF_itch_NormalizePath (dir);

    return SKIF_itch_NormalizePath (dir + L"\\" + file);
  }

  // Classifications that are never launchable games
  bool
  SKIF_itch_IsExcludedClassification (const std::string& classification)
  {
    static const std::set <std::string> excluded = {
      "assets", "game_mod", "physical_game", "soundtrack", "book", "comic"
    };

    return (excluded.count (classification) != 0);
  }

  // %APPDATA%\<app>\preferences.json
  std::wstring
  SKIF_itch_GetPreferencesPath (const std::string& app)
  {
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    return SK_FormatStringW (LR"(%ws\%hs\preferences.json)", _path_cache.app_data_roaming.path, app.c_str ( ));
  }

  // The app's "Run games in a sandbox" preference (isolateApps); a game's own launch setting overrides it
  bool
  SKIF_itch_GetIsolateApps (const std::string& app)
  {
    std::ifstream  file (std::filesystem::path (SKIF_itch_GetPreferencesPath (app)), std::ios::binary);
    nlohmann::json prefs = (file.is_open ( )) ? nlohmann::json::parse (file, nullptr, false) : nlohmann::json ( );

    return (prefs.is_object ( ) && prefs.contains ("isolateApps") && prefs ["isolateApps"].is_boolean ( ) && prefs ["isolateApps"].get <bool> ( ));
  }

  // Whether a game runs sandboxed when launched through the app: its own setting (caves.settings: { "sandbox": bool }),
  //   else the app's preference (as the itch app's launch settings decide it)
  bool
  SKIF_itch_IsSandboxed (const std::string& caveSettings, bool isolateApps)
  {
    nlohmann::json settings = nlohmann::json::parse (caveSettings, nullptr, false);

    if (settings.is_object ( ) && settings.contains ("sandbox") && settings ["sandbox"].is_boolean ( ))
      return settings ["sandbox"].get <bool> ( );

    return isolateApps;
  }

  // Last write time and size of butler.db and butler.db-wal (of both apps), to cheaply tell whether a database may have changed
  std::wstring
  SKIF_itch_GetDatabaseStamp (void)
  {
    std::wstring stamp;

    for (const char* app : ITCH_APPS)
    for (const std::wstring& file : { SKIF_itch_GetDatabasePath (app), SKIF_itch_GetDatabasePath (app) + L"-wal", SKIF_itch_GetPreferencesPath (app) })
    {
      WIN32_FILE_ATTRIBUTE_DATA fad = { };

      if (GetFileAttributesExW (file.c_str ( ), GetFileExInfoStandard, &fad))
        stamp += SK_FormatStringW (L"%lu.%lu.%lu.%lu|", fad.ftLastWriteTime.dwHighDateTime, fad.ftLastWriteTime.dwLowDateTime,
                                                        fad.nFileSizeHigh,                  fad.nFileSizeLow);
      else
        stamp += L"-|";
    }

    return stamp;
  }

  // Identifies the current set of installs. butler writes to the database all the time (cached metadata, play time),
  //   so only the columns that affect the library are included.
  std::string
  SKIF_itch_GetFingerprint (void)
  {
    std::string fingerprint;

    for (const char* app : ITCH_APPS)
    {
      itch_db_c db (SKIF_itch_GetDatabasePath (app));

      fingerprint += std::string (app) + ((SKIF_itch_GetIsolateApps (app)) ? ":sandboxed\n" : ":\n");

      db.query (R"(SELECT c.id, c.build_id, c.install_folder_name, c.custom_install_folder, c.verdict, l.path, c.settings
                   FROM caves c LEFT JOIN install_locations l ON l.id = c.install_location_id
                   ORDER BY c.id)",
        [&](sqlite3_stmt* row)
        {
          for (int col = 0; col < 7; col++)
            fingerprint += db.text (row, col) + "|";

          fingerprint += "\n";
        });
    }

    return fingerprint;
  }
}

#pragma endregion

#pragma region Library

// Adds the installs of one itch app (itch or kitch). keys and ids are shared between the apps.
static void
SKIF_itch_ScanDatabase (const std::string& app, app_list_t *apps, std::set <std::string>& keys, std::set <uint32_t>& ids)
{
  std::wstring dbPath = SKIF_itch_GetDatabasePath (app);

  if (! PathFileExistsW (dbPath.c_str ( )))
  {
    PLOG_VERBOSE << "The " << app.c_str ( ) << " database was not found: " << dbPath;
    return;
  }

  itch_db_c db (dbPath);

  if (! db.isOpen ( ))
    return;

  const bool isolateApps = SKIF_itch_GetIsolateApps (app);

  enum { Col_CaveID, Col_GameID, Col_UploadID, Col_BuildID, Col_FolderName, Col_CustomFolder, Col_Verdict,
         Col_LocationPath, Col_Title, Col_Classification, Col_URL, Col_CoverURL, Col_StillCoverURL, Col_Settings };

  db.query (R"(SELECT c.id, c.game_id, c.upload_id, c.build_id, c.install_folder_name, c.custom_install_folder, c.verdict,
                      l.path, g.title, g.classification, g.url, g.cover_url, g.still_cover_url, c.settings
               FROM caves c
               LEFT JOIN install_locations l ON l.id = c.install_location_id
               LEFT JOIN games             g ON g.id = c.game_id
               ORDER BY c.installed_at, c.id)",
    [&](sqlite3_stmt* row)
  {
    std::string caveID         = db.text  (row, Col_CaveID);
    int64_t     gameID         = db.int64 (row, Col_GameID);
    int64_t     uploadID       = db.int64 (row, Col_UploadID);
    std::string classification = db.text  (row, Col_Classification);
    std::string title          = db.text  (row, Col_Title);

    if (SKIF_itch_IsExcludedClassification (classification))
    {
      PLOG_VERBOSE << "itch cave " << caveID << " (" << title << ") is classified as " << classification << "; skipping...";
      return;
    }

    // The game folder: the custom folder if set, else <install location>\<folder name>
    std::wstring installDir = SK_UTF8ToWideChar (db.text (row, Col_CustomFolder));

    if (installDir.empty ( ))
    {
      std::wstring locationPath = SK_UTF8ToWideChar (db.text (row, Col_LocationPath));
      std::wstring folderName   = SK_UTF8ToWideChar (db.text (row, Col_FolderName));

      if (locationPath.empty ( ) || folderName.empty ( ))
      {
        PLOG_WARNING << "itch cave " << caveID << " has no install location; skipping...";
        return;
      }

      installDir = SKIF_itch_CombinePath (locationPath, folderName);
    }

    installDir = SKIF_itch_NormalizePath (installDir);

    if (title.empty ( ))
      title = SK_WideCharToUTF8 (std::filesystem::path (installDir).filename ( ).wstring ( ));

    // Launch configs: the Windows executables of the verdict, in the verdict's order
    nlohmann::json verdict = nlohmann::json::parse (db.text (row, Col_Verdict), nullptr, false);

    std::wstring basePath = installDir;
    std::vector <app_record_s::launch_config_s> launchConfigs;

    if (! verdict.is_discarded ( ) && verdict.is_object ( ))
    {
      if (verdict.contains ("basePath") && verdict["basePath"].is_string ( ))
      {
        std::wstring verdictBase = SKIF_itch_NormalizePath (SK_UTF8ToWideChar (verdict["basePath"].get <std::string> ( )));

        if (! verdictBase.empty ( ))
          basePath = verdictBase;
      }

      if (verdict.contains ("candidates") && verdict["candidates"].is_array ( ))
      {
        for (auto& candidate : verdict["candidates"])
        {
          if (! candidate.is_object ( ))
            continue;

          std::string flavor = candidate.value ("flavor", "");
          std::string path   = candidate.value ("path",   "");

          // Native Windows executables only (not html, jar, love, msi, windows-script, ...)
          if (flavor != "windows" || path.empty ( ))
            continue;

          // Installers and uninstallers that ended up as candidates
          if (candidate.contains ("windowsInfo") && candidate["windowsInfo"].is_object ( ))
          {
            auto& windowsInfo = candidate["windowsInfo"];

            if ((windowsInfo.contains ("installerType") && windowsInfo["installerType"].is_string ( ) && ! windowsInfo["installerType"].get <std::string> ( ).empty ( )) ||
                (windowsInfo.contains ("uninstaller")   && windowsInfo["uninstaller"]  .is_boolean ( ) &&   windowsInfo["uninstaller"]  .get <bool> ( )))
              continue;
          }

          std::wstring exePath = SKIF_itch_CombinePath (basePath, SK_UTF8ToWideChar (path));

          app_record_s::launch_config_s lc;
          lc.id                   = static_cast <int> (launchConfigs.size ( ));
          lc.valid                = 1;
          lc.executable_path      = exePath;
          lc.executable           = std::filesystem::path (exePath).filename ( ).wstring ( );
          lc.install_dir          = installDir;
          // The CPU type is left as Common: SKIF reads the bitness from the executable itself

          launchConfigs.push_back (lc);
        }
      }
    }

    if (launchConfigs.empty ( ))
    {
      PLOG_WARNING << "itch game " << SK_UTF8ToWideChar (title) << " (" << installDir << ") has no Windows executable; skipping...";
      return;
    }

    // Persistent key: the game ID; additional installs of the same game (other uploads) are told apart by the upload ID
    std::string key = std::to_string (gameID);

    if (gameID == 0 || ! keys.insert (key).second)
    {
      key = std::to_string (gameID) + "-" + std::to_string (uploadID);

      if (! keys.insert (key).second)
      {
        PLOG_VERBOSE << "itch cave " << caveID << " is a duplicate of " << key << "; skipping...";
        return;
      }
    }

    // Internal ID: the game ID when it fits and is free, else a hash of the key (0 is not a valid ID)
    uint32_t appid = (gameID > 0 && gameID <= UINT32_MAX) ? static_cast <uint32_t> (gameID) : 0;

    if (appid == 0 || ids.count (appid) != 0)
    {
      std::hash <std::string> stoi_hasher;
      appid = static_cast <uint32_t> (stoi_hasher (key));

      if (appid == 0)
        appid = 1;
    }

    ids.insert (appid);

    app_record_itch_s record (appid);

    record.store_utf8           = "itch";
    record._status.installed    = true;
    record.install_dir          = installDir;
    record.names.normal         = title;
    record.names.original       = title;

    record.itch.key             = key;
    record.itch.app             = app;
    record.itch.sandboxed       = SKIF_itch_IsSandboxed (db.text (row, Col_Settings), isolateApps);
    record.itch.game_id         = gameID;
    record.itch.upload_id       = uploadID;
    record.itch.build_id        = db.int64 (row, Col_BuildID);
    record.itch.cave_id         = caveID;
    record.itch.classification  = classification;
    record.itch.url             = db.text  (row, Col_URL);
    record.itch.cover_url       = db.text  (row, Col_StillCoverURL);

    // Prefer the still cover (cover_url can be an animated GIF)
    if (record.itch.cover_url.empty ( ))
      record.itch.cover_url     = db.text  (row, Col_CoverURL);

    for (auto& lc : launchConfigs)
      record.launch_configs.emplace (lc.id, lc);

    record.specialk.injection.injection.type = InjectionType::Global;

    // Strip invalid filename characters
    record.specialk.profile_dir_utf8 = SKIF_Util_StripInvalidFilenameChars (title);
    record.specialk.profile_dir      = SK_UTF8ToWideChar (record.specialk.profile_dir_utf8);

    std::string entry_name = record.names.normal; // Copied first: the record is moved from below

    app_entry_t
      itch_entry (std::move (entry_name), std::make_unique <app_record_itch_s> (std::move (record)));

    apps->emplace_back (std::move (itch_entry));
  });
}

void
SKIF_itch_GetInstalledAppIDs (app_list_t *apps)
{
  if (apps == nullptr)
    return;

  std::set <std::string> keys; // Persistent keys already added
  std::set <uint32_t>    ids;  // Internal IDs already added

  for (const char* app : ITCH_APPS)
    SKIF_itch_ScanDatabase (app, apps, keys, ids);
}

bool
SKIF_itch_hasInstalledGamesChanged (void)
{
  static DWORD        dwLastSignalCheck = 0;
  static std::wstring stamp             = SKIF_itch_GetDatabaseStamp ( );
  static std::string  fingerprint       = SKIF_itch_GetFingerprint   ( );

  bool signal = false;
  if (SKIF_Util_timeGetTime ( ) > dwLastSignalCheck + 5000)
  {
    std::wstring currentStamp = SKIF_itch_GetDatabaseStamp ( );

    // The database files changed: only repopulate when the installs changed
    if (currentStamp != stamp)
    {
      stamp = std::move (currentStamp);

      std::string current = SKIF_itch_GetFingerprint ( );

      if (current != fingerprint)
      {
        fingerprint = std::move (current);
        signal      = true;
      }
    }

    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}

bool
SKIF_itch_IsClientInstalled (const std::string& app)
{
  return PathFileExistsW (SKIF_itch_GetSetupPath (app).c_str ( ));
}

#pragma endregion

#pragma region Launching

bool
app_record_itch_s::PrefersInstantLaunch (bool fromGameMenu, launch_config_s* config) const
{
  // Fallback if the itch app is not installed
  if (itch.game_id == 0 || ! SKIF_itch_IsClientInstalled (itch.app))
    return true;

  // The itch app starts its own choice of executable, so other launch configs are run directly
  if (config != nullptr && config->id != 0)
    return true;

  return (! fromGameMenu && UsesInstantPlay (GetInstantPlayDefault ( )));
}

// Launch itch game through the itch app (headless), unless an instant launch was requested
void
app_record_itch_s::Launch (const launch_context_s& ctx)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (ctx.instant || itch.game_id == 0)
  {
    LaunchInstant (ctx);
    return;
  }

  PLOG_VERBOSE << "Performing an itch app launch...";

  // "%LOCALAPPDATA%\itch\itch-setup.exe" --appname itch --run-game 216996   (canary: kitch)
  std::wstring setupPath     = SKIF_itch_GetSetupPath (itch.app);
  std::wstring setupFolder   = std::filesystem::path (setupPath).parent_path ( ).wstring ( );
  std::wstring launchOptions = SK_FormatStringW (LR"(--appname %hs --run-game %lld)", itch.app.c_str ( ), itch.game_id);

  if (SKIF_Util_CreateProcess (setupPath, launchOptions, setupFolder))
  {
    std::scoped_lock app_lock (_games.GetMutex());

    // Don't check the running state for at least 7.5 seconds
    _status.dwTimeDelayChecks = ctx.timeNow + 7500;
    _status.running           = true;

    SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal + " (" + itch.app + ")"), setupPath, launchOptions, setupFolder, ctx.config->getExecutableFullPath ( ), (! ctx.localInjection && ctx.usingSK));
  }
}

#pragma endregion

#pragma region Library UI

std::string
app_record_itch_s::GetLaunchWarning (void) const
{
  if (! itch.sandboxed)
    return "";

  return "The itch app runs this game in its sandbox, as a separate Windows user,\n"
         "which may affect which saves are accessible from within the game.\n"
         "Instant Play launches are not affected as they skip the sandbox.";
}

bool
app_record_itch_s::GetInstantPlayDefault (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return _registry.bInstantPlayitch;
}

std::wstring
app_record_itch_s::GetStoreAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\itch\%ws\)",  _path_cache.specialk_userdata, SKIF_Util_StripInvalidFilenameChars (SK_UTF8ToWideChar (itch.key)).c_str());
}

std::vector <std::pair <std::string, std::string>>
app_record_itch_s::GetStoreDebugFields (void) const
{
  return {
    { "App",               itch.app                        },
    { "Sandboxed",         (itch.sandboxed) ? "Yes" : "No" },
    { "Game ID",           std::to_string (itch.game_id)   },
    { "Upload ID",         std::to_string (itch.upload_id) },
    { "Build ID",          std::to_string (itch.build_id)  },
    { "Cave ID",           itch.cave_id                    },
    { "Classification",    itch.classification             },
    { "Store Page",        itch.url                        }
  };
}

#pragma endregion

#pragma region Artwork

std::wstring
app_record_itch_s::ResolveArtwork (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged)
{
  UNREFERENCED_PARAMETER (source);

  std::wstring assetDir = GetStoreAssetDirectory ( );
  std::wstring load_str = FindCustomArtwork (type);

  isCustom = (! load_str.empty ( ));

  if (! isCustom)
  {
    if      (type == LibraryTexture::Cover &&
             PathFileExistsW ((assetDir + L"cover-original.jpg").c_str ( )))
    {
      load_str  = assetDir + L"cover-original.jpg";
      isManaged = true; // Downloaded from IGDB: the cover menu offers Refresh
    }

    else if (type == LibraryTexture::Icon &&
             SKIF_Util_SaveExtractExeIcon (launch_configs[0].getExecutableFullPath ( ), assetDir + L"icon-original.png"))
      load_str  = assetDir + L"icon-original.png";
  }

  return load_str;
}

// Cover worker thread: the downloaded IGDB cover, else download it
std::wstring
app_record_itch_s::PrepareCoverSource (bool queryPCGW)
{
  UNREFERENCED_PARAMETER (queryPCGW);

  std::wstring coverPath = GetStoreAssetDirectory ( ) + L"cover-original.jpg";

  if (PathFileExistsW (coverPath.c_str ( )) || itch.game_id == 0 || ! FindCustomArtwork (LibraryTexture::Cover).empty ( ))
    return coverPath;

  SKIF_IGDB_DownloadCover (SKIF_IGDB_Source::itch, std::to_string (itch.game_id), coverPath);

  return coverPath;
}

#pragma endregion

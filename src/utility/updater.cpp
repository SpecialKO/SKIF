#include <utility/updater.h>

#include <filesystem>
#include <fstream>
#include <codecvt>
#include <random>

#include <SKIF.h>
#include <utility/utility.h>
#include <utility/sk_utility.h>
#include <nlohmann/json.hpp>
#include <picosha2.h>
#include <TextFlow.hpp>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <utility/games.h>
#include <netlistmgr.h>

#include <imgui/imgui.h>
#include <utility/skif_imgui.h>
#include <utility/hotkeys.h>
#include <../packages_misc/ImGuiNotify.hpp>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

/*

SKIF's auto-updater relies on the JSON file located at https://sk-data.special-k.info/repository.json
This file dynamically reconfigures SKIF's behavior, and defines branches as well as versions of said branches.

  * Pushing a new update is a matter of editing the file, adding a new node beneath "Versions" with the relevant details,
      and then uploading the updated repository.json (and accompanied installer) to the CDN.
    * For information on how to build an installer, see https://github.com/SpecialKO/Installer

  * The updater also refreshes (downloads) patrons.txt and \Assets\lc.json
    * Updates to lc.json is pushed automatically to the CDN through the GitHub workflows
        defined in https://github.com/SpecialKO/SKIF_launch_configs


The update process is performed in this way:

 1. SKIF downloads a new copy of https://sk-data.special-k.info/repository.json and stores it in \Versions\repository.json

 2. SKIF validates the branches the file includes.
    * If it was running on a temporary branch, e.g. a branch called "Discord (WIP)",
        it falls back to using the recognized parent branch (Discord, Website, or Ancient).
    * If no matching branch is found, it falls back to using the "Website" branch.

 3. SKIF checks through the listed versions, and tries to find the first one (the top-most one) of the current branch.

 4. If a relevant version if found, the version number (the "Name" attribute) is compared to the file/product version of the Special K DLL files.

 5. If the found version is newer than the installer version, the installer is downloaded using the provided link.

 6. Once the installer has been downloaded, the SHA256 checksum of the downloaded installer is verified against the one specified in repository.json.

    * If the checksum is not a match, the downloaded file is deleted and a new attempt will be performed during the next launch of SKIF.
    * If the checksum is a match, the updater hands the results over to the frontend UI which then acts upon the data depending on the configured settings.

 7. Once a decision to perform an update is taken, the installer is launched using some normal Inno Setup command line arguments along with some custom ones:
    * /VerySilent /NoRestart /Shortcuts=false /StartService=%d /StartMinimized=%d /DIR="%ws"
      (where DIR defines the current folder of Special K and SKIF)

 8. The actual shutdown (and restart) of SKIF is performed by the installer itself.

*/


CONDITION_VARIABLE UpdaterPaused = { };

SKIF_Updater::SKIF_Updater (void)
{
  InitializeConditionVariable (&UpdaterPaused);
  extern SKIF_Signals _Signal;

  // [Startup] timing (verbose) of the constructor
  const DWORD dwCtorStart = SKIF_Util_timeGetTime1 ( );

  if (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode)
  {
    // Clearing out old installers...
    ClearOldUpdates ( );

    // Initial patrons.txt read...
    results.patrons    = SK_WideCharToUTF8 (ReadPatronsFile ( ));

    // Initial changes.txt read...
    ReadChangesFile ( );
  }

  const DWORD dwCtorFiles = SKIF_Util_timeGetTime1 ( );

  // Start the child thread that is responsible for checking for updates
  static HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void*) -> unsigned
  {
    CRITICAL_SECTION            UpdaterJob = { };
    InitializeCriticalSection (&UpdaterJob);
    EnterCriticalSection      (&UpdaterJob);

    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_UpdaterJob");

    CoInitializeEx       (nullptr, 0x0);

    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
    SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    static SKIF_Updater& parent = SKIF_Updater::GetInstance ( );
    extern SKIF_Signals _Signal;
    extern std::atomic<bool> SKIF_Shutdown;
    bool SKIF_NoInternet = false;

    // Sleep if SKIF is being used as a lancher, exiting, or we have no internet
    while (_Signal.Launcher || _Signal.LauncherURI || _Signal.Quit || _Signal.ServiceMode)
    {
      SleepConditionVariableCS (
        &UpdaterPaused, &UpdaterJob,
          INFINITE
      );
    }

    PLOG_VERBOSE << "SKIF_UpdaterJob thread started!";

    // Wait until the library has been loaded and shown: the first check (downloads, hashing the installer) would
    //   otherwise compete with it for the CPU. At most 5 seconds, for when the library isn't loading (another tab
    //   is open, or the window isn't shown).
    {
      static SKIF_GamingCollection& _games = SKIF_GamingCollection::Get ( );

      DWORD dwStart  = SKIF_Util_timeGetTime1 ( ),
            dwLoaded = 0;

      while (true)
      {
        DWORD dwNow = SKIF_Util_timeGetTime1 ( );

        if (dwLoaded == 0 && _games.HasPopulatedOnce ( ))
            dwLoaded = dwNow;

        if ((dwLoaded != 0 && dwNow - dwLoaded >= 500) || dwNow - dwStart >= 5000 || SKIF_Shutdown.load ( ))
          break;

        Sleep (50);
      }

      PLOG_VERBOSE << "The updater waited " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms for the library to load.";
    }

    do
    {
      static CComPtr <INetworkListManager> pNLM;
      static HRESULT hrNLM  =
        CoCreateInstance (CLSID_NetworkListManager, NULL, CLSCTX_ALL, __uuidof (INetworkListManager), (LPVOID*) &pNLM);

      // Check if we have an internet connection,
      //   and if not, check again every 5 seconds
      if (SUCCEEDED (hrNLM))
      {
        do {
          VARIANT_BOOL connStatus = 0;

          if (SUCCEEDED (pNLM->get_IsConnectedToInternet (&connStatus)))
            SKIF_NoInternet = (VARIANT_FALSE == connStatus);

          // Resume the update thread
          if (! SKIF_NoInternet)
            break;

          Sleep (5000);
        } while (SKIF_NoInternet);
      }

      else {
        SK_RunOnce (PLOG_ERROR << "Failed checking for an internet connection!");
        SKIF_NoInternet = true; // Assume we have an internet connection if something fails
      }

      parent.updater_running.store (1);

      static int lastWritten = 0;
      int currReading        = parent.snapshot_idx_reading.load ( );

      // This is some half-assed attempt of implementing triple-buffering where we don't overwrite our last finished snapshot.
      // If the main thread is currently reading from the next intended target, we skip that one as it means we have somehow
      //   managed to loop all the way around before the main thread started reading our last written result.
      int currWriting = (currReading == (lastWritten + 1) % 3)
                                      ? (lastWritten + 2) % 3  // Jump over very next one as it is currently being read from
                                      : (lastWritten + 1) % 3; // It is fine to write to the very next one

      auto& local =
        parent.snapshots [currWriting].results;

      local = { };    // Reset any existing data
      local.patrons = // Copy existing patrons.txt data
        parent.snapshots [currReading].results.patrons;

      PLOG_INFO << "Checking for updates...";

      // Set a timer so the main UI refreshes every 15 ms
      SetTimer (SKIF_Notify_hWnd, IDT_REFRESH_UPDATER, 15, NULL);

      // Check for updates!
      parent.PerformUpdateCheck (local);

      // Format the changes for the next version
      if (! local.release_notes.empty())
      {
        std::string strNotes = local.release_notes;

        // Ensure the text wraps at every 110 character (longest line used yet, in v0.8.32)
        strNotes = TextFlow::Column(strNotes).width(110).toString();

        // Calc longest line and number of lines
        std::istringstream iss(strNotes);
        for (std::string line; std::getline(iss, line); local.release_notes_formatted.lines++)
          if (line.length() > local.release_notes_formatted.max_length)
            local.release_notes_formatted.max_length = line.length();

        // Populate the vector (single allocation)
        local.release_notes_formatted.notes.reserve   (strNotes.length ( ) + 4);
        local.release_notes_formatted.notes.push_back ('\n');
        local.release_notes_formatted.notes.insert    (local.release_notes_formatted.notes.end ( ), strNotes.begin ( ), strNotes.end ( ));
        local.release_notes_formatted.notes.push_back ('\n');

        // Ensure the vector array is double null terminated
        local.release_notes_formatted.notes.push_back ('\0');
        local.release_notes_formatted.notes.push_back ('\0');

        // Increase NumLines by 3, two from push_back() and
        //  two from ImGui's love of having one and a half empty line below content
        local.release_notes_formatted.lines += 3.5f;
      }

      auto&
        fmt = local.history_formatted;
        fmt.notes.clear();
        fmt.lines      = 0;
        fmt.max_length = 0;

      // Format the historical changes
      if (! local.history.empty ( ))
      {
        // Strip '\r' so CRLF doesn't leak into the buffer or the measurements
        std::string src = local.history;
        src.erase (std::remove (src.begin (), src.end (), '\r'), src.end ());

        // Wrap at 110 columns (longest line used so far, in v0.8.32)
        const std::string wrapped = TextFlow::Column (src).width (110).toString ();

        // One pass: line count and longest line
        size_t line_len = 0;
        for (const char c : wrapped)
        {
          if (c == '\n')
          {
            fmt.max_length = std::max (fmt.max_length, line_len);
            ++fmt.lines;
            line_len = 0;
          }
          else
            ++line_len;
        }
        if (line_len > 0) // last line without a trailing '\n'
        {
          fmt.max_length = std::max (fmt.max_length, line_len);
          ++fmt.lines;
        }

        // Build the buffer with a single allocation
        fmt.notes.reserve   (wrapped.size () + 4);
        fmt.notes.push_back ('\n'); // top padding
        fmt.notes.insert    (fmt.notes.end (), wrapped.begin (), wrapped.end ());
        fmt.notes.push_back ('\n'); // bottom padding
        fmt.notes.push_back ('\0'); // terminator for InputTextEx
        fmt.notes.push_back ('\0'); // second NUL, only if something else needs it

        // Account for the two padding newlines
        fmt.lines += 2;
      }

      // Save the changes in a local file
      if (! local.release_notes_formatted.notes.empty())
      {
        std::wofstream changes_file (L"changes.txt");

        if (changes_file.is_open ())
        {
          // Requires Windows 10 1903+ (Build 18362)
          if (SKIF_Util_IsWindows10v1903OrGreater ( ))
          {
            changes_file.imbue (
                std::locale (".UTF-8")
            );
          }

          else
          {
            // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(disable : 4996)
            changes_file.imbue (std::locale (std::locale (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
          }

          std::wstring out_text =
            SK_UTF8ToWideChar (local.release_notes_formatted.notes.data());

          // Strip all null terminator \0 characters from the string
          out_text.erase(std::find(out_text.begin(), out_text.end(), '\0'), out_text.end());

          changes_file.write(out_text.c_str(),
            out_text.length());

          changes_file.close();
        }

      }

      // Kill the timer once the update process has completed
      KillTimer (SKIF_Notify_hWnd, IDT_REFRESH_UPDATER);

      // Swap in the results
      lastWritten = currWriting;
      parent.snapshot_idx_written.store (lastWritten);

      parent.updater_running.store (2);

      // Signal to the main thread that new results are available
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_UPDATER, local.state, 0x0);

      parent.awake.store (false);

      while (! parent.awake.load ( ))
      {
        SleepConditionVariableCS (
          &UpdaterPaused, &UpdaterJob,
            INFINITE
        );
      }

    } while (! SKIF_Shutdown.load()); // Keep thread alive until exit

    PLOG_VERBOSE << "SKIF_UpdaterJob thread stopped!";

    SetThreadPriority     (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    CoUninitialize        ( );

    LeaveCriticalSection  (&UpdaterJob);
    DeleteCriticalSection (&UpdaterJob);

    return 0;
  }, nullptr, 0x0, nullptr);

  const DWORD dwCtorEnd = SKIF_Util_timeGetTime1 ( );

  PLOG_VERBOSE << "[Startup] Initializing the updater took " << (dwCtorEnd - dwCtorStart) << " ms ("
               << (dwCtorFiles - dwCtorStart) << " ms clearing old installers and reading patrons.txt and changes.txt, "
               << (dwCtorEnd   - dwCtorFiles) << " ms starting the updater thread).";

  if (hWorkerThread != NULL)
  {
    CloseHandle (hWorkerThread);
    hWorkerThread = NULL;
  }
}

void
SKIF_Updater::ClearOldUpdates (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  PLOG_INFO << "Clearing out old installers...";

  auto _isWeekOld = [&](FILETIME ftLastWriteTime) -> bool
  {
    FILETIME ftSystemTime{}, ftAdjustedFileTime{};
    SYSTEMTIME systemTime{};
    GetSystemTime (&systemTime);

    if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
    {
      ULARGE_INTEGER uintLastWriteTime{};

      // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
      uintLastWriteTime.HighPart        = ftLastWriteTime.dwHighDateTime;
      uintLastWriteTime.LowPart         = ftLastWriteTime.dwLowDateTime;

      // Perform 64-bit arithmetic to add 7 days to last modified timestamp
      uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(7 * 24 * 60 * 60 * 1.0e+7);

      // Copy the results to an FILETIME struct
      ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
      ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

      // Compare with system time, and if system time is later (1), then update the local cache
      if (CompareFileTime(&ftSystemTime, &ftAdjustedFileTime) == 1)
      {
        return true;
      }
    }

    return false;
  };

  HANDLE hFind        = INVALID_HANDLE_VALUE;
  WIN32_FIND_DATA ffd = { };

  std::wstring VersionFolder = SK_FormatStringW(LR"(%ws\Version\)", _path_cache.specialk_userdata);

  hFind =
    FindFirstFileExW ((VersionFolder + L"SpecialK_*.exe").c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

  if (INVALID_HANDLE_VALUE != hFind)
  {
    if (_isWeekOld    (ffd.ftLastWriteTime))
      DeleteFile      ((VersionFolder + ffd.cFileName).c_str());

    while (FindNextFile (hFind, &ffd))
      if (_isWeekOld  (ffd.ftLastWriteTime))
        DeleteFile    ((VersionFolder + ffd.cFileName).c_str());

    FindClose (hFind);
  }
}

void
SKIF_Updater::PerformUpdateCheck (results_s& _res)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static const std::wstring root         = SK_FormatStringW (LR"(%ws\Version\)",       _path_cache.specialk_userdata);
  static const std::wstring path_repo    = root + LR"(repository.json)";
  static const std::wstring path_patreon = SK_FormatStringW (LR"(%ws\patrons.txt)",    _path_cache.specialk_userdata);
  static const std::wstring assets       = SK_FormatStringW (LR"(%ws\Assets\)",        _path_cache.specialk_userdata);
  static const std::wstring path_lc_cfgs = assets + LR"(lc.json)";

  // Add UNIX-style timestamp to ensure we don't get anything cached
  time_t ltime;
  time (&ltime);

  // Cannot be static as that would invalidate the whole purpose of the appended timestamp
         const std::wstring url_repo    = L"https://sk-data.special-k.info/repository.json?t=" + std::to_wstring (ltime);
  static const std::wstring url_patreon = L"https://sk-data.special-k.info/patrons.txt";
  static const std::wstring url_lc_cfgs = L"https://sk-data.special-k.info/lc.json";

  // Create any missing directories
  std::error_code ec;
  if (! std::filesystem::exists (            root,   ec))
        std::filesystem::create_directories (root,   ec);
  if (! std::filesystem::exists (            assets, ec))
        std::filesystem::create_directories (assets, ec);

  bool forcedUpdateCheck = forced.load ( );
  bool downloadNewFiles  = forcedUpdateCheck;
  bool downloadLcConfigs = false;
  bool rollbackDesired   = rollback.load();

  if (! forcedUpdateCheck && _registry.iCheckForUpdates != 0 && ! _registry.bLowBandwidthMode)
  {
    // Download files if any does not exist or if we're forcing an update
    if (_registry.iCheckForUpdates == 2 || ! PathFileExists (path_repo.c_str()) || ! PathFileExists (path_patreon.c_str()))
    {
      downloadNewFiles = true;
    }

    else {
      WIN32_FILE_ATTRIBUTE_DATA fileAttributes{};

      if (GetFileAttributesEx (path_repo.c_str(),    GetFileExInfoStandard, &fileAttributes))
      {
        FILETIME ftSystemTime{}, ftAdjustedFileTime{};
        SYSTEMTIME systemTime{};
        GetSystemTime (&systemTime);

        if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
        {
          ULARGE_INTEGER uintLastWriteTime{};

          // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
          uintLastWriteTime.HighPart        = fileAttributes.ftLastWriteTime.dwHighDateTime;
          uintLastWriteTime.LowPart         = fileAttributes.ftLastWriteTime.dwLowDateTime;

          // Perform 64-bit arithmetic to add 7 days to last modified timestamp
          uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(7 * 24 * 60 * 60 * 1.0e+7);

          // Copy the results to an FILETIME struct
          ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
          ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

          // Compare with system time, and if system time is later (1), then update the local cache
          if (CompareFileTime (&ftSystemTime, &ftAdjustedFileTime) == 1)
            downloadNewFiles = true;
        }
      }
    }

    // Check if we should download new launch configs
    // This is not done every single launch because that
    // would refresh the library on every single launch...
    if (! PathFileExists (path_lc_cfgs.c_str()))
    {
      downloadLcConfigs = true;
    }

    else {
      WIN32_FILE_ATTRIBUTE_DATA fileAttributes{};

      if (GetFileAttributesEx (path_lc_cfgs.c_str(),    GetFileExInfoStandard, &fileAttributes))
      {
        FILETIME ftSystemTime{}, ftAdjustedFileTime{};
        SYSTEMTIME systemTime{};
        GetSystemTime (&systemTime);

        if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
        {
          ULARGE_INTEGER uintLastWriteTime{};

          // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
          uintLastWriteTime.HighPart        = fileAttributes.ftLastWriteTime.dwHighDateTime;
          uintLastWriteTime.LowPart         = fileAttributes.ftLastWriteTime.dwLowDateTime;

          // Perform 64-bit arithmetic to add 7 days to last modified timestamp
          uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(7 * 24 * 60 * 60 * 1.0e+7);

          // Copy the results to an FILETIME struct
          ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
          ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

          // Compare with system time, and if system time is later (1), then update the local cache
          if (CompareFileTime (&ftSystemTime, &ftAdjustedFileTime) == 1)
            downloadLcConfigs = true;
        }
      }
    }
  }

  // Update patrons.txt
  if (downloadNewFiles)
  {
    PLOG_INFO << "Downloading patrons.txt...";
    PLOG_ERROR_IF(! SKIF_Util_GetWebResource (url_patreon, path_patreon)) << "Failed to download patrons.txt";
  }

  // Update lc.json
  if (downloadLcConfigs)
  {
    PLOG_INFO << "Downloading lc.json...";

    if (SKIF_Util_GetWebResource (url_lc_cfgs, path_lc_cfgs))
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_REFRESHGAMES, 0x0, 0x0); // Signal to the main thread that it needs to refresh its games
    else
      PLOG_ERROR << "Failed to download lc.json";
  }

  // Read patrons.txt, but only if the existing object is empty
  // This means a restart is required for SKIF to reflect changes to patrons.txt
  if (_res.patrons.empty ( ))
    _res.patrons = SK_WideCharToUTF8 (ReadPatronsFile ( ));

  // Update repository.json
  if (downloadNewFiles)
  {
    PLOG_INFO << "Downloading repository.json...";
    DeleteFile (path_repo.c_str()); // Delete any existing file
    PLOG_ERROR_IF(! SKIF_Util_GetWebResource (url_repo, path_repo)) << "Failed to download repository.json";
  }

  std::ifstream file(path_repo);
  nlohmann::ordered_json jf = nlohmann::ordered_json::parse(file, nullptr, false);
  file.close();

  if (jf.is_discarded ( ))
  {
    PLOG_ERROR << "Parse error for repository.json. Deleting file so a retry occurs the next time a check is performed...";
    DeleteFile (path_repo.c_str()); // Something went wrong -- delete the file so a new attempt is performed later
    return;
  }

         std::wstring wsCurrentBranch  = _registry.wsUpdateChannel;
         std:: string   currentBranch  = SK_WideCharToUTF8 (wsCurrentBranch);
  static std::wstring wsPreviousBranch = wsCurrentBranch;

  bool changedUpdateChannel = (wsPreviousBranch != wsCurrentBranch);
  wsPreviousBranch  = wsCurrentBranch;

  PLOG_INFO << "Update Channel: " << wsCurrentBranch;

  std::string currentVersion = (SK_GetBitness() == 64 ? _inject.SKVer64_utf8 : _inject.SKVer32_utf8);

  PLOG_INFO << "Installed version: " << currentVersion;

  // Populate update channels (only on the first run)
  static bool
        channelsPopulated = false;
  if (! channelsPopulated)
  {     channelsPopulated = true;
    try
    {
      bool detectedBranch = false;
      for (auto& branch : jf["Main"]["Branches"])
      {
        _res.update_channels.emplace_back (branch["Name"].get<std::string>(), branch["Description"].get<std::string>());

        if (branch["Name"].get<std::string_view>()._Equal(currentBranch))
          detectedBranch = true;
      }

      // If we cannot find the branch, move the user over to the closest "parent" branch
      if (! detectedBranch)
      {
        PLOG_ERROR << "Could not find the update channel in repository.json!";

        if (     wsCurrentBranch.find(L"Website")       != std::string::npos
              || wsCurrentBranch.find(L"Release")       != std::string::npos)
                 wsCurrentBranch =    L"Website";
        else if (wsCurrentBranch.find(L"Discord")       != std::string::npos
              || wsCurrentBranch.find(L"Testing")       != std::string::npos)
                 wsCurrentBranch =    L"Discord";
        else if (wsCurrentBranch.find(L"Ancient")       != std::string::npos
              || wsCurrentBranch.find(L"Compatibility") != std::string::npos)
                 wsCurrentBranch =    L"Ancient";
        else
                 wsCurrentBranch =    L"Website";

        PLOG_ERROR << "Using fallback channel: " << wsCurrentBranch;

        _registry.wsIgnoreUpdate = L"";

        PLOG_INFO << "Writing fallback channel to registry...";
        _registry.wsUpdateChannel = wsCurrentBranch;
        _registry.regKVUpdateChannel.putData (_registry.wsUpdateChannel);

        currentBranch = SK_WideCharToUTF8 (wsCurrentBranch);
      }
    }
    catch (const std::exception& e)
    {
      PLOG_ERROR << "Failed when parsing update channels! " << e.what ();
    }
  }

  if (forcedUpdateCheck || (_registry.iCheckForUpdates != 0 && ! _registry.bLowBandwidthMode))
  {
    try
    {
      bool parsedFirstVersion      = false;
      bool parsedRollbackVersion   = false;
      bool detectedRollbackVersion = false;
      bool absoluteLatest          = false;

#pragma region Original method
      // Processes all versions listed in the changelog
      for (auto& version : jf["Main"]["Versions"])
      {
        bool isBranch = false;

        for (auto& branch : version["Branches"])
          if (branch.get<std::string_view>()._Equal(currentBranch))
            isBranch = true;

        if (isBranch)
        { // START IF (isBRANCH)
          std::string branchVersion = version["Name"].get<std::string>();

          // Check if the version of this branch is different from the current one.
          // We don't check if the version is *newer* since we need to support downgrading
          // to other branches as well, which means versions that are older.

          int versionDiff = SKIF_Util_CompareVersionStrings (branchVersion, currentVersion);

          if (parsedFirstVersion)
            _res.history += "\n\n\n"; // Spacing between the previous version and the current one

          if (! absoluteLatest)
          {
            absoluteLatest = true;
            _res.description_latest = version["Description"].get<std::string>();
          }

          if (versionDiff == 0)
            _res.history += version["Description"].get<std::string>() + "  -[ " + _("This is the version currently installed!") + " ]-";
          else if (versionDiff > 0 && ! parsedFirstVersion)
            _res.history += version["Description"].get<std::string>() + "  -[ " + _("Update available!") + " ]-";
          else
            _res.history += version["Description"].get<std::string>();

          _res.history += "\n";
          _res.history += "=================\n";

          if (version["ReleaseNotes"].get<std::string>().empty())
            _res.history += _("No listed changes.");
          else
            _res.history += version["ReleaseNotes"].get<std::string>();

          // Special handling for all newer versions (ensures all missing changes are listed)
          if (versionDiff > 0 && ! currentVersion.empty())
          {
            if (parsedFirstVersion)
              _res.release_notes += "\n\n\n"; // Spacing between the previous version and the current one

            if (! parsedFirstVersion)
              _res.release_notes += version["Description"].get<std::string>() + "  -[ " + _("Newest update available!") + " ]-";
            else
              _res.release_notes += version["Description"].get<std::string>();

            _res.release_notes += "\n";
            _res.release_notes += "=================\n";

            if (version["ReleaseNotes"].get<std::string>().empty())
              _res.release_notes += _("No listed changes.");
            else
              _res.release_notes += version["ReleaseNotes"].get<std::string>();
          }

          else if (versionDiff == 0)
          {
            // Used in the update prompt to show the description of the current version installed
            _res.description_installed = version["Description"].get<std::string>();
          }

          // If an older version was found
          else if (versionDiff < 0 && ! detectedRollbackVersion && ! rollbackDesired)
          {
            detectedRollbackVersion = true;
            _res.rollbackAvailable  = true;
          }

          if (! parsedFirstVersion && ! parsedRollbackVersion)
          {
            if ((versionDiff != 0 && ! rollbackDesired) || (versionDiff < 0 && rollbackDesired))
            {
              PLOG_INFO << "Found version: "    << branchVersion;

              std::wstring branchInstaller    = SK_UTF8ToWideChar(version["Installer"]   .get<std::string>());
              std::wstring filename;

              if (branchInstaller.find_last_of(L"/") < branchInstaller.length())
                filename = branchInstaller.substr (branchInstaller.find_last_of(L"/") + 1);

              _res.version       = branchVersion;
              _res.filename      = filename;
              _res.description   = version["Description"] .get<std::string>();

              // If we didn't populate the release_notes above, do so here, but only for the very latest version
              if (_res.release_notes.empty())
              {
                if (version["ReleaseNotes"].get<std::string>().empty())
                  _res.release_notes += _("No listed changes.");
                else
                  _res.release_notes += version["ReleaseNotes"].get<std::string>();
              }

              if (_res.version == SK_WideCharToUTF8 (_registry.wsIgnoreUpdate))
              {
                PLOG_INFO << "Version is set to be ignored!";
                _res.state |= UpdateFlags_Ignored;
              }

              if (versionDiff > 0)
                _res.state |= UpdateFlags_Newer;
              else
                _res.state |= UpdateFlags_Older;

              if (changedUpdateChannel || rollbackDesired)
              {
                PLOG_VERBOSE << "Update is being forced...";
                _res.state |= UpdateFlags_Forced;
              }

              if (PathFileExists ((root + filename).c_str()))
                _res.state |= UpdateFlags_Downloaded;

              if ((_res.state & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
              {
                PLOG_VERBOSE << "File " << (root + filename) << " has not been downloaded...";
                if (((_res.state & UpdateFlags_Forced ) == UpdateFlags_Forced)    ||
                    ((_res.state & UpdateFlags_Ignored) != UpdateFlags_Ignored    &&
                     (_res.state & UpdateFlags_Older  ) != UpdateFlags_Older))
                {
                  PLOG_INFO << "Downloading installer: " << branchInstaller;
                  if (SKIF_Util_GetWebResource (branchInstaller, root + filename))
                    _res.state |= UpdateFlags_Downloaded;
                }
              }

              // Validate downloaded file
              if ((_res.state & UpdateFlags_Downloaded) == UpdateFlags_Downloaded)
              {
                PLOG_VERBOSE << "File: " << (root + filename);

                bool fallback = false;
                std::string hex_str_expected, hex_str;
                try
                {
                  // If the repository.json file includes a hash, check it
                  hex_str_expected = version["SHA256"].get<std::string>();

                  std::ifstream fileStream (root + filename, std::ios::binary);
                  std::vector<unsigned char> hash (picosha2::k_digest_size);
                  picosha2::hash256 (fileStream, hash.begin(), hash.end());
                  fileStream.close  ();

                  hex_str = picosha2::bytes_to_hex_string(hash.begin(), hash.end());
                }
                catch (const std::exception&)
                {
                }

                if (hex_str_expected.empty())
                {
                  PLOG_WARNING << "No checksum present. Falling back to size validation...";
                  fallback = true;
                }

                else if (hex_str_expected == hex_str)
                {
                  PLOG_INFO << "Installer matched the expected checksum!";
                }

                else {
                  PLOG_ERROR << "Installer did not match the expected checksum!";
                  PLOG_ERROR << "SHA256  : " << hex_str;
                  PLOG_ERROR << "Expected: " << hex_str_expected;
                  _res.state &= ~UpdateFlags_Downloaded;
                }

                // Fallback (check if file is > 0 bytes)
                if (fallback)
                {
                  // When opening an existing file, the CreateFile function performs the following actions:
                  // [...] and ignores any file attributes (FILE_ATTRIBUTE_*) specified by dwFlagsAndAttributes.
                  CHandle hInstaller (
                    CreateFileW ((root + filename).c_str(),
                                    GENERIC_READ,
                                      FILE_SHARE_READ,
                                        nullptr,       OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, // GetFileAttributesW ((root + filename).c_str())
                                            nullptr ) );

                  if (hInstaller != INVALID_HANDLE_VALUE)
                  {
                    LARGE_INTEGER size = { };
                    if (GetFileSizeEx (hInstaller, &size))
                    {
                      if (size.QuadPart > 0)
                        _res.state |= UpdateFlags_Downloaded;
                      else
                      {
                        PLOG_ERROR << "Installer file size was zero (0)!";
                        _res.state &= ~UpdateFlags_Downloaded;
                      }
                    }
                  }
                }

                // Check if something went wrong...
                if ((_res.state & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
                {
                  _res.state |= UpdateFlags_Failed;
                  PLOG_ERROR << "Download failed!";
                  DeleteFile ((root + filename).c_str());
                }
              }

              // If the download looks correct, we set it as available
              if ((_res.state & UpdateFlags_Newer)      == UpdateFlags_Newer      ||
                  (_res.state & UpdateFlags_Forced)     == UpdateFlags_Forced     ||
                  ((_res.state & UpdateFlags_Older)  == UpdateFlags_Older   &&
                  (_res.state & UpdateFlags_Downloaded) == UpdateFlags_Downloaded ))
                _res.state |= UpdateFlags_Available;
            }

            if ((  rollbackDesired && versionDiff < 0) ||
                (! rollbackDesired))
            {
              parsedFirstVersion    = true;
              parsedRollbackVersion = true;
            }
          }
        } // END IF (isBRANCH)
      }

#pragma endregion

#pragma region New method

      // Processes all versions listed in the changelog
#if 0
      for (auto& version : jf["Main"]["Versions"])
      {
        version_s _version = { };
        /*
          std::string version;
          std::string description;
          std::vector <std::string> branches; // Do we really need this ?
          std::string changes;
          std::string url;
          std::string filename;
          std::string checksum;
          bool        is_current;
          bool        is_newer;
          bool        is_older;
          bool        is_branch;
        */

        _version.version     = version["Name"]        .get<std::string>();
        _version.description = version["Description"] .get<std::string>();
        _version.changes     = version["ReleaseNotes"].get<std::string>();
        if (_version.changes.empty()) _version.changes = _("No listed changes.");
        _version.url         = version["Installer"]   .get<std::string>();
        _version.filename    = _version.url.substr (_version.url.find_last_of ("/"));
        _version.checksum    = version["SHA256"]      .get<std::string>();

        int versionDiff = SKIF_Util_CompareVersionStrings (_version.version, currentVersion);
        _version.is_newer    = (versionDiff  > 0);
        _version.is_current  = (versionDiff == 0);
        _version.is_older    = (versionDiff  < 0);

        for (auto& branch : version["Branches"])
          if (branch.get<std::string_view>()._Equal(currentBranch))
            _version.is_branch = true;

        _res.versions.push_back (std::move (_version));
      }
#endif

#pragma endregion

    }
    catch (const std::exception& e)
    {
      PLOG_ERROR << "Failed when parsing versions! " << e.what ();
    }
  }

  // Set the force variable to false
  forced.store (false);
}

std::wstring
SKIF_Updater::ReadPatronsFile (void)
{
  std::wstring full_text;
  std::wifstream
      file (L"patrons.txt");
  if (file.is_open ())
  {
    // Requires Windows 10 1903+ (Build 18362)
    if (SKIF_Util_IsWindows10v1903OrGreater ( ))
    {
      file.imbue (
          std::locale (".UTF-8")
      );
    }

    // Contemplate removing this fallback entirely since neither Win8.1 and Win10 pre-1903 is not supported any longer by Microsoft
    else
    {
      // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
  #pragma warning(disable : 4996)
      file.imbue (std::locale (std::locale (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
    }

    std::vector <std::wstring> lines;
    std::wstring line;

    while (file.good ())
    {
      std::getline (file, line);

      // Skip blank lines
      for (const auto& it : line)
      {
        if (iswalpha(it) != 0)
        {
          lines.push_back (std::move (line));
          break;
        }
      }
    }

    file.close ();

    if (! lines.empty())
    {
      // Shuffle the lines using a random number generator
      auto rd  = std::random_device{};
      auto gen = std::default_random_engine{ rd() };
      std::shuffle(lines.begin(), lines.end(), gen);  // Shuffle the vector

      size_t total_length = 0;
      for (const auto& vline : lines)
        total_length += vline.length ( ) + 1;

      full_text.reserve (total_length);

      for (const auto& vline : lines) {
        full_text += vline;
        full_text += L'\n';
      }

      if (full_text.length() > 0)
        full_text.resize (full_text.length () - 1);
    }
  }

  return full_text;
}

// Only used by the auto-updater
void
SKIF_Updater::ReadChangesFile (void)
{
  auto_updater_formatted = { };

  std::ifstream
      file (L"changes.txt");
  if (file.is_open ())
  {
    // Requires Windows 10 1903+ (Build 18362)
    if (SKIF_Util_IsWindows10v1903OrGreater ( ))
    {
      file.imbue (
          std::locale (".UTF-8")
      );
    }

    // Contemplate removing this fallback entirely since neither Win8.1 and Win10 pre-1903 is not supported any longer by Microsoft
    else
    {
      // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
  #pragma warning(disable : 4996)
      file.imbue (std::locale (std::locale (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
    }

    std::string line;

    while (file.good ())
    {
      std::getline (file, line);

      auto_updater_formatted.notes.insert (auto_updater_formatted.notes.end ( ), line.begin ( ), line.end ( ));

      // Add a newline at the end of the line
      auto_updater_formatted.notes.push_back ('\n');

      if (line.length() > auto_updater_formatted.max_length)
        auto_updater_formatted.max_length = line.length();

      auto_updater_formatted.lines++;
    }

    file.close ();

    if (! auto_updater_formatted.notes.empty())
    {
      // Remove the trailing newline
      auto_updater_formatted.notes.pop_back ( );

      // Be sure to terminate the string properly
      auto_updater_formatted.notes.push_back ('\0');
      auto_updater_formatted.notes.push_back ('\0');

      // Increase the number of lines due to ImGui weirdness
      auto_updater_formatted.lines += 1.5f;
    }
  }
}

void
SKIF_Updater::RefreshResults (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  int lastWritten = snapshot_idx_written.load ( );
  snapshot_idx_reading.store (lastWritten);

  results =
    snapshots [lastWritten].results;

  if (channels.empty ( ) && ! results.update_channels.empty ( ))
  {
    channels = results.update_channels; // copy, because we never populate this ever again

    // Set active channel
    for (auto& _channel : channels)
      if (_channel.first == SK_WideCharToUTF8 (_registry.wsUpdateChannel))
        channel = &_channel;
  }
}

void
SKIF_Updater::CheckForUpdates (bool _forced, bool _rollback)
{
  if (awake.load ( ) == false)
  {
    forced.store   (_forced);
    rollback.store (_rollback);
    awake.store    (true);

    WakeConditionVariable       (&UpdaterPaused);
  }
}

bool
SKIF_Updater::IsRunning (void)
{
  static DWORD dwLastRefresh = 0;
  static int status = 0;

  // Refresh once every 500 ms
  if (dwLastRefresh < SKIF_Util_timeGetTime())
  {
    status = updater_running.load ( );
    dwLastRefresh = SKIF_Util_timeGetTime() + 500;
  }

  return (status != 2);
}

bool
SKIF_Updater::IsRollbackAvailable(void)
{
  return results.rollbackAvailable;
}

std::string
SKIF_Updater::GetPatrons (void)
{
  return results.patrons;
}

std::string
SKIF_Updater::GetHistory (void)
{
  return results.history;
}

SKIF_Updater::changelog_s
SKIF_Updater::GetAutoUpdateNotes (void)
{
  return auto_updater_formatted;
}

std::vector <std::pair<std::string, std::string>>*
SKIF_Updater::GetChannels (void)
{
  return &channels;
}

std::pair<std::string, std::string>*
SKIF_Updater::GetChannel (void)
{
  return channel;
}

void
SKIF_Updater::SetChannel (std::pair<std::string, std::string>* _channel)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  channel = _channel;

  // Update registry
  _registry.wsUpdateChannel = SK_UTF8ToWideChar (channel->first);
  _registry.regKVUpdateChannel.putData (_registry.wsUpdateChannel);
}

void
SKIF_Updater::SetIgnoredUpdate (std::wstring update)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  PLOG_DEBUG << "Ignored version: " << update;
  _registry.wsIgnoreUpdate = update;
  _registry.regKVIgnoreUpdate.putData (_registry.wsIgnoreUpdate);
}

UpdateFlags
SKIF_Updater::GetState (void)
{
  return results.state;
}

SKIF_Updater::results_s&
SKIF_Updater::GetResults(void)
{
  return results;
}

void
SKIF_Updater::Show (WindowType type)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static int id = 0;

  SKIF_Updater::Window* ptr =
    type == WindowType::ChangesFull       ? &wndChangesFull :
    type == WindowType::ChangesAutoUpdate ? &wndChangesAU   :
    type == WindowType::Updater           ? &wndUpdater     :
                                      nullptr;

  if (ptr == nullptr)
    return;

  if (ptr->show && ptr->hwnd)
  {
    if (       IsIconic (       ptr->hwnd))
             ShowWindow (       ptr->hwnd, SW_RESTORE);
    else if (! IsWindowVisible (ptr->hwnd))
             ShowWindow (       ptr->hwnd, SW_SHOW);
    SetForegroundWindow (       ptr->hwnd);
  }

  else
  {
    switch (type)
    {
    case WindowType::ChangesFull:
      ptr->label = _("Changelog")
                 + (! channel->first.empty() ? " (" + channel->first + ")" : "")
                 + "###Changelog-"  + std::to_string (id++);
      break;
    case WindowType::ChangesAutoUpdate:
      ptr->label = _("An update was installed automatically")
                 + (! channel->first.empty() ? " (" + channel->first + ")" : "")
                 + "###AutoUpdate-" + std::to_string (id++);
      break;
    case WindowType::Updater:
      ptr->label = ((SKIF_Util_CompareVersionStrings (results.version, (SK_GetBitness() == 64 ? _inject.SKVer64_utf8 : _inject.SKVer32_utf8)) > 0)
                 ? std::string (_("Version Available")) : std::string (_("Rollback Available")))
                 + (! channel->first.empty() ? " (" + channel->first + ")" : "")
                 + "###Updater-" + std::to_string (id++);
      break;
    }

    ptr->hwnd = nullptr;
    ptr->show = true;
  }
}

static void
DrawInputTextEx (SKIF_Updater::changelog_s* fmt, SKIF_Updater::WindowType type, const ImVec2 avail = ImGui::GetContentRegionAvail ( ))
{
  if (! fmt)
    return;

  if (fmt->notes.empty())
  {
    ImGui::TextDisabled ("%s", (type == SKIF_Updater::WindowType::ChangesFull       ? _("No historical changes detected...")                :
                                type == SKIF_Updater::WindowType::ChangesAutoUpdate ? _("No changes detected...")                           :
                                type == SKIF_Updater::WindowType::Updater           ? _("The update does not contain any release notes...") :
                                                                                      "This should not be poss... What did you do?! ;)"));
    return;
  }

  auto&       cache      = fmt->imgui_cache;
  const float font_size  = ImGui::GetFontSize ( );
  if (  cache.font_size != font_size)
  {
    cache.font_size = font_size;
    cache.max_w     = 0.0f;

    const char* end = fmt->notes.data() + fmt->notes.size();
    for (const char* p   = fmt->notes.data(); p < end; )
    {
      const char* eol = (const char*)memchr (p, '\n', end - p);
      const char* le  = eol ? eol : end;
      cache.max_w = ImMax (cache.max_w, ImGui::CalcTextSize (p, le).x);
      p     = eol ? eol + 1 : end;
    }
  }

  const ImGuiStyle& style= ImGui::GetStyle ( );
  const float         sb = style.ScrollbarSize;
  const float  content_w = cache.max_w + style.FramePadding.x * 2.0f;
  const float  content_h = fmt->lines * ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;

  // Adding a scrollbar only ever removes space, so this converges in two steps
  bool needY = content_h > avail.y;
  bool needX = content_w > avail.x - (needY ? sb : 0.0f);
       needY = content_h > avail.y - (needX ? sb : 0.0f); // an X bar can force a Y bar
       needX = content_w > avail.x - (needY ? sb : 0.0f); // and back

  const ImVec2 input_size (ImMax (avail.x - (needY ? sb : 0.0f), content_w),
                           ImMax (avail.y - (needX ? sb : 0.0f), content_h));

  ImGuiWindowFlags wf  = ImGuiWindowFlags_HorizontalScrollbar;
  if (needX)       wf |= ImGuiWindowFlags_AlwaysHorizontalScrollbar;
  if (needY)       wf |= ImGuiWindowFlags_AlwaysVerticalScrollbar;

  ImGui::BeginChild     ((type == SKIF_Updater::WindowType::ChangesFull       ? "##ScrollChangelog"  :
                          type == SKIF_Updater::WindowType::ChangesAutoUpdate ? "##ScrollAutoUpdate" :
                          type == SKIF_Updater::WindowType::Updater           ? "##ScrollUpdater"    :
                                                                                "##ScrollFallback"),
                         avail, ImGuiChildFlags_None,
                         wf);

  ImGui::InputTextEx    ((type == SKIF_Updater::WindowType::ChangesFull       ? "##ChangesChangelog"  :
                          type == SKIF_Updater::WindowType::ChangesAutoUpdate ? "##ChangesAutoUpdate" :
                          type == SKIF_Updater::WindowType::Updater           ? "##ChangesUpdater"    :
                                                                                "##ChangesFallback"),
                         nullptr,
                         fmt->notes.data(), static_cast<int>(fmt->notes.size()),
                         input_size,
                        (ImGuiInputTextFlags)ImGuiInputTextFlags_ReadOnly  |
                        (ImGuiInputTextFlags)ImGuiInputTextFlags_Multiline |
                        (ImGuiInputTextFlags)ImGuiInputTextFlags_NoHorizontalScroll);

  // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
  SKIF_ImGui_AutoScroll (true, SKIF_ImGuiAxis_Y);

  ImGui::EndChild       ( );
}

// Escape and Ctrl+W close the changelog / updater windows, as in SKIF's other windows. Call right after Begin ( ), every frame.
//   With Shortcut ( ) (include/utility/hotkeys.h): only while the window (or a child of it) has the focus. Escape first
//     closes a popup of the window, or leaves an active field (e.g. after selecting text in the changelog)
static bool
SKIF_Updater_CloseHotkeyPressed (void)
{
  // Both are called every frame, as that is what keeps their routes registered
  const bool hotkeyClose  = ImGui::Shortcut (SKIF_Hotkey_CloseWindow);
  const bool hotkeyEscape = ImGui::Shortcut (ImGuiKey_Escape);

  return hotkeyClose || (hotkeyEscape && ! ImGui::IsAnyItemActive ( ) &&
                         ! ImGui::IsPopupOpen (0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
}

void
SKIF_Updater::DrawWindowChanges (Window* wnd, WindowType type)
{
  if (! wnd)
    return;

  if (! wnd->show)
    return;

  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  ImGuiWindowClass
    wc;
    wc.ClassId = ImGui::GetID ("SKIF_ChangelogClass");
    wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge; // Work around a pesky bug where the viewport tries to merge into the... debug fallback window?
  ImGui::SetNextWindowClass (&wc);
  ImGui::SetNextWindowSizeConstraints (ImVec2 (500.0f, 300.0f) * SKIF_ImGui_GetDPIScaleForWindow (wnd->label.c_str()), ImVec2 (FLT_MAX, FLT_MAX));
  extern ImRect windowRect;
  ImGui::SetNextWindowPos  (windowRect.GetCenter(), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize (ImVec2 (900.0f, 600.0f) * SKIF_ImGui_MainDPIScale, ImGuiCond_Appearing); // Opens centered on the main window

  bool cache_show = wnd->show;

  static float fY = 0.0f;

  if (ImGui::Begin (wnd->label.c_str(), &wnd->show,
                      ImGuiWindowFlags_NoCollapse        |
                      ImGuiWindowFlags_NoSavedSettings   ))
  {
    if (wnd->hwnd == nullptr && ImGui::GetWindowViewport()->PlatformHandleRaw)
      wnd->hwnd = (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw;

    // Escape / Ctrl+W: same as the Close button
    if (SKIF_Updater_CloseHotkeyPressed ( ))
      wnd->show = false;

    // TRANSLATORS: {0} is "Special K v" followed by a version number
    SKIF_ImGui_TextHighlighted (_("You are currently using {0}"),
      ("Special K v " + (SK_GetBitness() == 64 ? _inject.SKVer64_utf8 : _inject.SKVer32_utf8)).c_str(),
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info));

    SKIF_ImGui_Spacing ();

    ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
    // Its own size (0 = keep the current one), rounded *down* to whole pixels at the current DPI scale: ImGui rounds font sizes
    //   half up (e.g. 14 px * 125% = 17.5 -> 18 px), which at 125% and 175% made the 110 columns wide changelog a few pixels
    //     wider than the window (sized 900 px * DPI scale), showing a horizontal scrollbar
    const float fontScale        = ImGui::GetStyle ( ).FontScaleMain * ImGui::GetStyle ( ).FontScaleDpi;
    const float fontConsolasSize = (fontConsolas != nullptr && fontScale > 0.0f) ? ImFloor (fontConsolas->LegacySize * fontScale) / fontScale : 0.0f;
    ImGui::PushFont       (fontConsolas, fontConsolasSize);

    ImVec2 child_size = ImVec2 (ImGui::GetContentRegionAvail().x,
                                ImGui::GetContentRegionAvail().y - fY);
    DrawInputTextEx (type == WindowType::ChangesFull       ? &results.history_formatted :
                     type == WindowType::ChangesAutoUpdate ? &auto_updater_formatted    :
                                                                nullptr,
                     type, child_size);

    fY = ImGui::GetCursorPosY ();

    ImGui::PopFont        ( ); // fontConsolas
    ImGui::PopStyleColor  ( ); // ImGuiCol_Text
    ImGui::PopStyleColor  ( ); // ImGuiCol_NavCursor

    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      ShowWindowAsync ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, SW_MINIMIZE);

    SKIF_ImGui_Spacing ();

    const float fButtonWidth = std::max (100.0f * SKIF_ImGui_GlobalDPIScale, ImGui::CalcTextSize (L_("Close"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f);

    float fX = (ImGui::GetContentRegionAvail().x - fButtonWidth) / 2;

    ImGui::SetCursorPosX (fX);

    if (ImGui::Button (L_("Close"), ImVec2 ( fButtonWidth,
                                          25 * SKIF_ImGui_GlobalDPIScale )))
    {
      wnd->show = false;
    }

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    SKIF_ImGui_Spacing ( );

    fY = ImGui::GetCursorPosY ( ) - fY;
  }

  ImGui::End ( );

  if (type == WindowType::ChangesAutoUpdate)
  {
    // If we closed the window on this frame, update the registry
    if (cache_show == true && cache_show != wnd->show)
      _registry.regKVAutoUpdateVersion.putData (L"");
  }
}

void
SKIF_Updater::DrawWindowUpdater (Window* wnd)
{
  if (! wnd)
    return;

  if (! wnd->show)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  ImGuiWindowClass
    wc;
    wc.ClassId = ImGui::GetID ("SKIF_UpdaterClass");
    wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge; // Work around a pesky bug where the viewport tries to merge into the... debug fallback window?
  ImGui::SetNextWindowClass (&wc);
  ImGui::SetNextWindowSizeConstraints (ImVec2 (500.0f, 300.0f) * SKIF_ImGui_GetDPIScaleForWindow (wnd->label.c_str()), ImVec2 (FLT_MAX, FLT_MAX));
  extern ImRect windowRect;
  ImGui::SetNextWindowPos  (windowRect.GetCenter(), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize (ImVec2 (900.0f, 600.0f) * SKIF_ImGui_MainDPIScale, ImGuiCond_Appearing); // Opens centered on the main window

  bool cache_show = wnd->show;

  if (ImGui::Begin (wnd->label.c_str(), &wnd->show,
                      ImGuiWindowFlags_NoCollapse        |
                      ImGuiWindowFlags_NoSavedSettings   ))
  {
    if (wnd->hwnd == nullptr && ImGui::GetWindowViewport()->PlatformHandleRaw)
      wnd->hwnd = (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw;

    // Escape / Ctrl+W: same as the Cancel button
    if (SKIF_Updater_CloseHotkeyPressed ( ))
      wnd->show = false;

    if (UpdatePromptPopup == PopupState_Open)
        UpdatePromptPopup  = PopupState_Opened;

    std::string currentVersion = (SK_GetBitness() == 64 ? _inject.SKVer64_utf8 : _inject.SKVer32_utf8);
    std::string compareLabel;
    ImVec4      compareColor;
    bool        compareNewer = (SKIF_Util_CompareVersionStrings (results.version, currentVersion) > 0);

    // Centered if it fits on one line, else wrapped
    auto _TextCentered = [](const ImVec4& col, const char* text)
    {
      const float width = ImGui::CalcTextSize (text).x;

      if (width <= ImGui::GetContentRegionAvail ( ).x)
      {
        ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + (ImGui::GetContentRegionAvail ( ).x - width) / 2);
        ImGui::TextColored   (col, "%s", text);
      }

      else
        SKIF_ImGui_TextColoredWrapped (col, text);
    };

    if (compareNewer)
    {
      compareLabel = _("This version is newer than the currently installed.");
      compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
    }
    else
    {
      compareLabel = _("WARNING: You are about to roll back Special K as this version is older than the currently installed!");
      compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
    }

    SKIF_ImGui_Spacing ();

    // TRANSLATORS: {0} is the name of a version of Special K (e.g. "Special K v 26.10.6")
    const char* updateTxt = ((results.state & UpdateFlags_Downloaded) != UpdateFlags_Downloaded)
                          ? _("{0} is available for download.")
                          : _("{0} is ready to be installed.");

    float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize (SKIF_L10n_Format (updateTxt, nullptr, results.description).c_str ( )).x) / 2;

    ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + std::max (0.0f, fX));

    SKIF_ImGui_TextHighlighted (updateTxt, results.description.c_str ( ), compareColor);

    SKIF_ImGui_Spacing ();

    if ((results.state & UpdateFlags_Failed) == UpdateFlags_Failed)
    {
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));
      ImGui::TextWrapped ("%s", _("A failed download was detected! Click Download below to try again."));
      ImGui::TextWrapped ("%s", _("In case of continued errors, consult SKIF.log for more details."));
      ImGui::PopStyleColor  ( );

      SKIF_ImGui_Spacing ();
    }

    ImGui::TextWrapped     ("%s", _("Target Folder:"));
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info));
    ImGui::TextWrapped    ("%s", SK_WideCharToUTF8 (_path_cache.specialk_install).c_str());
    ImGui::PopStyleColor  ( );

    SKIF_ImGui_Spacing ();

    static float fY = 0.0f;

    if (! results.release_notes_formatted.notes.empty())
    {
      if (results.description_installed.empty())
        ImGui::TextWrapped           ("%s", _("Changes:"));

      else {
        // TRANSLATORS: {0} is the name of the installed version of Special K (e.g. "Special K v 26.10.6")
        SKIF_ImGui_TextHighlighted (_("Changes from {0}:"), results.description_installed.c_str ( ), ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
      }

      ImVec2 child_size = ImVec2 (ImGui::GetContentRegionAvail().x,
                                  ImGui::GetContentRegionAvail().y - fY);
      DrawInputTextEx (&results.release_notes_formatted, WindowType::Updater, child_size);

      fY = ImGui::GetCursorPosY ();

      SKIF_ImGui_Spacing ();
    }

    _TextCentered (compareColor, compareLabel.c_str ( ));

    SKIF_ImGui_Spacing ();

    const bool  btnDownload = ((results.state & UpdateFlags_Downloaded) != UpdateFlags_Downloaded);
    const char* btnLabel    = (btnDownload)  ? L_("Download") :
                              (compareNewer) ? L_("Update")   :
                                               L_("Rollback");

    // All buttons as wide as the widest label (at least 100 px)
    const float fButtonWidth = std::max ({ 100.0f * SKIF_ImGui_GlobalDPIScale,
                                           ImGui::CalcTextSize (btnLabel,       nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                           ImGui::CalcTextSize (L_("Ignore"),   nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f,
                                           ImGui::CalcTextSize (L_("Cancel"),   nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f });

    fX = (ImGui::GetContentRegionAvail().x - (((compareNewer) ? 3 : 2) * fButtonWidth) - (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x * 2)) / 2;

    ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + std::max (0.0f, fX));

    if (ImGui::Button (btnLabel, ImVec2 ( fButtonWidth,
                                          25 * SKIF_ImGui_GlobalDPIScale )))
    {
      if (btnDownload)
      {
        _registry.wsIgnoreUpdate = L"";
        _registry.regKVIgnoreUpdate.putData (_registry.wsIgnoreUpdate);

        // Trigger a new check for updates (which should download the installer)
        CheckForUpdates (false, ! compareNewer);
      }

      else {
        std::wstring args = SK_FormatStringW (LR"(/VerySilent /NoRestart /Shortcuts=false /StartService=%d /DIR="%ws")",
                                              (_inject.bCurrentState && ! _inject.bAckInj), _path_cache.specialk_install);

        if (_inject.bCurrentState)
          _inject._StartStopInject (true);

        static std::wstring updateRoot = SK_FormatStringW (LR"(%ws\Version\)", _path_cache.specialk_userdata);
        SKIF_Util_OpenURI ( updateRoot + results.filename, SW_SHOWNORMAL, L"OPEN", args.c_str() );
      }

      wnd->show = false;
    }

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    ImGui::SameLine ();
    ImGui::Spacing  ();
    ImGui::SameLine ();

    if (compareNewer)
    {
      if (ImGui::Button (L_("Ignore"), ImVec2 ( fButtonWidth,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        SetIgnoredUpdate (SK_UTF8ToWideChar (results.version));

        wnd->show = false;
      }

      SKIF_ImGui_SetHoverTip (_("SKIF will not prompt about this version again."));

      ImGui::SameLine ();
      ImGui::Spacing  ();
      ImGui::SameLine ();
    }

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    if (ImGui::Button (L_("Cancel"), ImVec2 ( fButtonWidth,
                                           25 * SKIF_ImGui_GlobalDPIScale )))
    {
      wnd->show = false;
    }

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    SKIF_ImGui_Spacing ( );

    fY = ImGui::GetCursorPosY ( ) - fY;
  }

  ImGui::End ( );

  // If we closed the window on this frame, update the registry
  if (cache_show == true && cache_show != wnd->show)
    UpdatePromptPopup = PopupState_Closed;
}

void
SKIF_Updater::ProcessWindows (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static bool showAutoUpdate = (auto_updater_formatted.max_length > 0 && ! _inject.SKVer32.empty() && _inject.SKVer32 == _registry.wsAutoUpdateVersion);
  
  if (showAutoUpdate && SKIF_ImGui_IsFocused ())
  {
    showAutoUpdate = false;
    Show (WindowType::ChangesAutoUpdate);
  }

  DrawWindowChanges (&wndChangesFull, WindowType::ChangesFull);
  DrawWindowChanges (&wndChangesAU,   WindowType::ChangesAutoUpdate);
  DrawWindowUpdater (&wndUpdater);
}

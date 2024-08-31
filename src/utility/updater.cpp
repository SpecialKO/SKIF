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
#include <netlistmgr.h>

/*

SKIF's auto-updater relies on the JSON file located at https://sk-data.special-k.info/repository.json
This file dynamically reconfigures SKIF's behavior, and defines branches as well as versions of said branches.
Pushing a new update is a matter of editing the file, adding a new node beneath "Versions" with the relevant details,
  and then uploading the updated repository.json (and accompanied installer) to the CDN.

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
  
  if (! _Signal.Launcher && ! _Signal.LauncherURI && ! _Signal.Quit && ! _Signal.ServiceMode)
  {
    // Clearing out old installers...
    ClearOldUpdates ( );

    // Initial patrons.txt read...
    results.patrons    = SK_WideCharToUTF8 (ReadPatronsFile ( ));

    // Initial changes.txt read...
    ReadChangesFile ( );
  }

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

    PLOG_DEBUG << "SKIF_UpdaterJob thread started!";

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

        // Populate the vector
        local.release_notes_formatted.notes.push_back ('\n');

        for (size_t i = 0; i < strNotes.length(); i++)
          local.release_notes_formatted.notes.push_back(strNotes[i]);

        local.release_notes_formatted.notes.push_back ('\n');

        // Ensure the vector array is double null terminated
        local.release_notes_formatted.notes.push_back ('\0');
        local.release_notes_formatted.notes.push_back ('\0');

        // Increase NumLines by 3, two from push_back() and
        //  two from ImGui's love of having one and a half empty line below content
        local.release_notes_formatted.lines += 3.5f;
      }

      // Format the historical changes
      if (! local.history.empty ( ))
      {
        std::string strHistory = local.history;

        // Ensure the text wraps at every 110 character (longest line used yet, in v0.8.32)
        strHistory = TextFlow::Column(strHistory).width(110).toString();

        // Calc longest line and number of lines
        std::istringstream iss(strHistory);
        for (std::string line; std::getline(iss, line); local.history_formatted.lines++)
          if (line.length() > local.history_formatted.max_length)
            local.history_formatted.max_length = line.length();

        // Populate the vector
        local.history_formatted.notes.push_back ('\n');

        for (size_t i = 0; i < strHistory.length(); i++)
          local.history_formatted.notes.push_back(strHistory[i]);

        local.history_formatted.notes.push_back ('\n');

        // Ensure the vector array is double null terminated
        local.history_formatted.notes.push_back ('\0');
        local.history_formatted.notes.push_back ('\0');

        // Increase NumLines by 3, two from vecHistory.push_back and
        //  two from ImGui's love of having one and a half empty line below content
        local.history_formatted.lines += 3.5f;
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
            changes_file.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
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

    PLOG_DEBUG << "SKIF_UpdaterJob thread stopped!";

    SetThreadPriority     (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    CoUninitialize        ( );

    LeaveCriticalSection  (&UpdaterJob);
    DeleteCriticalSection (&UpdaterJob);

    return 0;
  }, nullptr, 0x0, nullptr);

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

#ifdef _WIN64
  std::string currentVersion = _inject.SKVer64_utf8;
#else
  std::string currentVersion = _inject.SKVer32_utf8;
#endif

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
    catch (const std::exception&)
    {
      PLOG_ERROR << "Failed when parsing update channels!";
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
            _res.history += version["Description"].get<std::string>() + "  -[ This is the version currently installed! ]-";
          else if (versionDiff > 0 && ! parsedFirstVersion)
            _res.history += version["Description"].get<std::string>() + "  -[ Update available! ]-";
          else
            _res.history += version["Description"].get<std::string>();

          _res.history += "\n";
          _res.history += "=================\n";

          if (version["ReleaseNotes"].get<std::string>().empty())
            _res.history += "No listed changes.";
          else
            _res.history += version["ReleaseNotes"].get<std::string>();

          // Special handling for all newer versions (ensures all missing changes are listed)
          if (versionDiff > 0 && ! currentVersion.empty())
          {
            if (parsedFirstVersion)
              _res.release_notes += "\n\n\n"; // Spacing between the previous version and the current one

            if (! parsedFirstVersion)
              _res.release_notes += version["Description"].get<std::string>() + "  -[ Newest update available! ]-";
            else
              _res.release_notes += version["Description"].get<std::string>();

            _res.release_notes += "\n";
            _res.release_notes += "=================\n";

            if (version["ReleaseNotes"].get<std::string>().empty())
              _res.release_notes += "No listed changes.";
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
                  _res.release_notes += "No listed changes.";
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
        if (_version.changes.empty()) _version.changes = "No listed changes.";
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

        _res.versions.push_back (_version);
      }
#endif

#pragma endregion

    }
    catch (const std::exception&)
    {
      PLOG_ERROR << "Failed when parsing versions!";
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
      file.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
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
          lines.push_back(line);
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

      for (const auto& vline : lines) {
        full_text += vline + L"\n";
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
      file.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
    }

    std::string line;

    while (file.good ())
    {
      std::getline (file, line);

      for (size_t i = 0; i < line.length(); i++)
        auto_updater_formatted.notes.push_back (line[i]);

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

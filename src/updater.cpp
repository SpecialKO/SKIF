#include "updater.h"

#include <filesystem>
#include <fstream>
#include <codecvt>
#include <random>

#include <SKIF.h>
#include <SKIF_utility.h>
#include <sk_utility/utility.h>
#include <nlohmann/json.hpp>

#include <fsutil.h>
#include <registry.h>
#include <injection.h>

CONDITION_VARIABLE UpdaterPaused = { };

SKIF_Updater::SKIF_Updater (void)
{
  InitializeConditionVariable (&UpdaterPaused);
  
  // Clearing out old installers...
  ClearOldUpdates ( );

  // Start the child thread that is responsible for checking for updates
  static HANDLE hThread =
    CreateThread ( nullptr, 0x0,
      [](LPVOID)
    -> DWORD
    {
      CRITICAL_SECTION            UpdaterJob = { };
      InitializeCriticalSection (&UpdaterJob);
      EnterCriticalSection      (&UpdaterJob);

      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_UpdaterJob");

      SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

      static SKIF_Updater& parent = SKIF_Updater::GetInstance();
      extern SKIF_Signals _Signal;

      // Sleep if SKIF is being used as a lancher
      if (_Signal.Launcher)
      {
        SleepConditionVariableCS (
          &UpdaterPaused, &UpdaterJob,
            INFINITE
        );
      }

      PLOG_DEBUG << "Update Thread Started!";

      do
      {
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

        local = { }; // Reset any existing data

        // Check for updates!
        parent.PerformUpdateCheck (local);

        // Swap in the results
        lastWritten = currWriting;
        parent.snapshot_idx_written.store (lastWritten);
        
        parent.updater_running.store (2);

        // Signal to the main thread that new results are available
        PostMessage (SKIF_hWnd, WM_SKIF_UPDATER, local.state, 0x0);

        SleepConditionVariableCS (
          &UpdaterPaused, &UpdaterJob,
            INFINITE
        );

      } while (IsWindow (SKIF_hWnd)); // Keep thread alive until exit

      PLOG_DEBUG << "Update Thread Stopped!";

      SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

      LeaveCriticalSection  (&UpdaterJob);
      DeleteCriticalSection (&UpdaterJob);

      return 0;
    }, nullptr, 0x0, nullptr
  );
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

  HANDLE hFind = INVALID_HANDLE_VALUE;
  WIN32_FIND_DATA ffd;

  std::wstring VersionFolder = SK_FormatStringW(LR"(%ws\Version\)", _path_cache.specialk_userdata);

  hFind = FindFirstFile ((VersionFolder + L"SpecialK_*.exe").c_str(), &ffd);

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

  static const std::wstring root         = SK_FormatStringW (LR"(%ws\Version\)",    _path_cache.specialk_userdata);
  static const std::wstring path_repo    = root + LR"(repository.json)";
  static const std::wstring path_patreon = SK_FormatStringW (LR"(%ws\patrons.txt)", _path_cache.specialk_userdata);

  // Add UNIX-style timestamp to ensure we don't get anything cached
  time_t ltime;
  time (&ltime); 

  static const std::wstring url_repo    = L"https://sk-data.special-k.info/repository.json?t=" + std::to_wstring (ltime);
  static const std::wstring url_patreon = L"https://sk-data.special-k.info/patrons.txt";

  // Create any missing directories
  std::error_code ec;
  if (! std::filesystem::exists (            root, ec))
        std::filesystem::create_directories (root, ec);

  bool forcedUpdateCheck = forced.load ( );
  bool downloadNewFiles  = forcedUpdateCheck;
  bool rollbackDesired   = rollback.load();

  if (! forcedUpdateCheck && _registry.iCheckForUpdates != 0 && ! _registry.bLowBandwidthMode)
  {
    // Download files if any does not exist or if we're forcing an update
    if (! PathFileExists (path_repo.c_str()) || ! PathFileExists (path_patreon.c_str()) || _registry.iCheckForUpdates == 2)
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
          {
            downloadNewFiles = true;
          }
        }
      }
    }
  }

  // Update patrons.txt
  if (downloadNewFiles)
  {
    PLOG_INFO << "Downloading patrons.txt...";
    SKIF_Util_GetWebResource (url_patreon, path_patreon);
  }

  // Read patrons.txt
  if (_res.patrons.empty( ))
  {
    std::wifstream fPatrons(L"patrons.txt");
    std::vector<std::wstring> lines;
    std::wstring full_text;

    if (fPatrons.is_open ())
    {
      // Requires Windows 10 1903+ (Build 18362)
      if (SKIF_Util_IsWindows10v1903OrGreater ( ))
      {
        fPatrons.imbue (
            std::locale (".UTF-8")
        );
      }
      else
      {
        // Contemplate removing this fallback entirely since neither Win8.1 and Win10 pre-1903 is not supported any longer by Microsoft
        // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(disable : 4996)
        fPatrons.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
      }

      std::wstring line;

      while (fPatrons.good ())
      {
        std::getline (fPatrons, line);

        // Skip blank lines, since they would match everything....
        for (const auto& it : line)
        {
          if (iswalpha(it) != 0)
          {
            lines.push_back(line);
            break;
          }
        }
      }

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

        _res.patrons = SK_WideCharToUTF8 (full_text);
      }

      fPatrons.close ();
    }
  }

  // Update repository.json
  if (downloadNewFiles)
  {
    PLOG_INFO << "Downloading repository.json...";
    SKIF_Util_GetWebResource (url_repo, path_repo);
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
  std::wstring currentVersion = SK_UTF8ToWideChar (_inject.SKVer64);
#else
  std::wstring currentVersion = SK_UTF8ToWideChar (_inject.SKVer32);
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
                  wsCurrentBranch = L"Website";
        else if (wsCurrentBranch.find(L"Discord")       != std::string::npos
              || wsCurrentBranch.find(L"Testing")       != std::string::npos)
                  wsCurrentBranch = L"Discord";
        else if (wsCurrentBranch.find(L"Ancient")       != std::string::npos
              || wsCurrentBranch.find(L"Compatibility") != std::string::npos)
                  wsCurrentBranch = L"Ancient";
        else
                  wsCurrentBranch = L"Website";

        PLOG_ERROR << "Using fallback channel: " << wsCurrentBranch;

        _registry.wsIgnoreUpdate = L"";

        currentBranch = SK_WideCharToUTF8 (wsCurrentBranch);
      }
    }
    catch (const std::exception&)
    {
      PLOG_ERROR << "Failed during parsing when trying to populate update channels";
    }
  }

  if (forcedUpdateCheck || (_registry.iCheckForUpdates != 0 && ! _registry.bLowBandwidthMode))
  {
    try
    {
      bool parsedFirstVersion      = false;
      bool parsedRollbackVersion   = false;
      bool detectedRollbackVersion = false;

      // Processes all versions listed in the changelog
      for (auto& version : jf["Main"]["Versions"])
      {
        bool isBranch = false;

        for (auto& branch : version["Branches"])
          if (branch.get<std::string_view>()._Equal(currentBranch))
            isBranch = true;
        
        if (isBranch)
        {
          std::wstring branchVersion = SK_UTF8ToWideChar(version["Name"].get<std::string>());

          // Check if the version of this branch is different from the current one.
          // We don't check if the version is *newer* since we need to support downgrading
          // to other branches as well, which means versions that are older.

          int versionDiff = SKIF_Util_CompareVersionStrings (branchVersion, currentVersion);
          
          if (parsedFirstVersion)
            _res.history += "\n\n\n"; // Spacing between the previous version and the current one

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
              std::wstring filename           = branchInstaller.substr(branchInstaller.find_last_of(L"/"));

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

              if (_res.description == SK_WideCharToUTF8 (_registry.wsIgnoreUpdate))
                _res.state |= UpdateFlags_Ignored;
                
              if (PathFileExists ((root + filename).c_str()))
                _res.state |= UpdateFlags_Downloaded;

              if (versionDiff > 0)
                _res.state |= UpdateFlags_Newer;
              else
                _res.state |= UpdateFlags_Rollback;

              if (changedUpdateChannel || rollbackDesired)
                _res.state |= UpdateFlags_Forced;

              if ((_res.state & UpdateFlags_Forced)     == UpdateFlags_Forced     ||
                  ((_res.state & UpdateFlags_Downloaded) != UpdateFlags_Downloaded &&
                  (_res.state & UpdateFlags_Ignored   ) != UpdateFlags_Ignored    &&
                  (_res.state & UpdateFlags_Rollback  ) != UpdateFlags_Rollback))
              {
                PLOG_INFO << "Downloading installer: " << branchInstaller;
                if (SKIF_Util_GetWebResource(branchInstaller, root + filename))
                  _res.state |= UpdateFlags_Downloaded;
                else
                  _res.state |= UpdateFlags_Failed;
              }

              if ((_res.state & UpdateFlags_Newer)      == UpdateFlags_Newer      ||
                  (_res.state & UpdateFlags_Forced)     == UpdateFlags_Forced     ||
                  ((_res.state & UpdateFlags_Rollback)   == UpdateFlags_Rollback   &&
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
        }
      }
    }
    catch (const std::exception&)
    {
      PLOG_ERROR << "Failed during parsing when trying to populate update channels";
    }
  }

  // Set the force variable to false
  forced.store (false);
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
  forced.store   (_forced);
  rollback.store (_rollback);

  WakeConditionVariable       (&UpdaterPaused);
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
  _registry.regKVFollowUpdateChannel.putData (_registry.wsUpdateChannel);
}

void
SKIF_Updater::SetIgnoredUpdate (std::wstring update)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

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

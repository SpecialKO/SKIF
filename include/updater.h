#pragma once
#include <vector>
#include <string>
#include <memory>

typedef unsigned int UpdateFlags;  // -> enum UpdateFlags_

enum UpdateFlags_
{
  UpdateFlags_Unknown    = 0,
  UpdateFlags_Available  = 1 << 1, // Indicate a version is available
  UpdateFlags_Newer      = 1 << 2, // Indicate the found version is newer
  UpdateFlags_Rollback   = 1 << 3, // Indicate the found version is older
  UpdateFlags_Downloaded = 1 << 4, // Indicate the found version is available locally
  UpdateFlags_Ignored    = 1 << 5, // Indicate the found version is ignored (registry)
  UpdateFlags_Failed     = 1 << 6, // Indicate the download failed
  UpdateFlags_Forced     = 1 << 7, // Indicate the update check was forced
};

// Singleton struct
struct SKIF_Updater {

  // Used to contain the results of an update check
  struct results_s {
    UpdateFlags state = UpdateFlags_Unknown;
    std::wstring version;
    std::wstring filename;
    std::string  description;
    std::string  release_notes;
    std::string  history;
    std::string  patrons;
    std::string  description_installed;
    std::vector <std::pair<std::string, std::string>> update_channels; // only ever used on the very first run
  };

  // Public variables
  std::atomic<bool> rollbackAvailable = false; // Indicates SKIF can roll back
  
  // Public functions
  void        RefreshResults  (void);
  void        CheckForUpdates (bool _forced = false, bool _rollback = false);
  bool        IsRunning       (void);
  std::string GetPatrons      (void);
  std::string GetHistory      (void);
  std::vector <std::pair<std::string, std::string>>*
              GetChannels     (void);
  std::pair<std::string, std::string>*
              GetChannel      (void);
  void        SetChannel      (std::pair<std::string, std::string>* _channel);
  UpdateFlags GetState        (void);
  results_s&  GetResults      (void);

  static SKIF_Updater& GetInstance (void)
  {
      static SKIF_Updater instance;
      return instance;
  }

  SKIF_Updater (SKIF_Updater const&) = delete; // Delete copy constructor
  SKIF_Updater (SKIF_Updater&&)      = delete; // Delete move constructor

private:
  // Triple-buffer updates so we can go lock-free
  struct result_snapshot_s {
    results_s results;
  } snapshots [3];

  results_s&  results = snapshots[0].results;
  std::pair<std::string, std::string> empty = std::pair("", ""); // dummy (used when there's no match to be found)
  std::pair<std::string, std::string>* channel = &empty;
  std::vector <std::pair<std::string, std::string>>  channels;   // static
  bool        pending = true;
  
  std::atomic<int> snapshot_idx_reading = 0,
                   snapshot_idx_written = 1,
                   updater_running      = 0; // 0 = No update check has run,            1 = Update check is running,       2 = Update check has completed

  std::atomic<bool> forced   = false; // Only used internally for forced updates
  std::atomic<bool> rollback = false; // Only used internally when triggering a rollback

  SKIF_Updater (void);
  void ClearOldUpdates (void);
  void PerformUpdateCheck (results_s& _res);
};
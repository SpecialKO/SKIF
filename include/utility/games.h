#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <stores/generic_library2.h>

// Singleton struct
struct SKIF_GamesCollection {

  // Triple-buffer updates so we can go lock-free
  struct snapshot_s {
    std::vector <std::unique_ptr<app_generic_s>>* apps;
  } snapshots [3];
  
  std::atomic<int>  snapshot_idx_reading = 0,
                    snapshot_idx_written = 1;
  std::atomic<bool> awake                = false; // Used to protect against sporadic wake-ups

  void                         RefreshGames (void);
  std::vector <std::unique_ptr<app_generic_s>>* GetGames (void);

  static SKIF_GamesCollection& GetInstance (void)
  {
      static SKIF_GamesCollection instance;
      return instance;
  }

  SKIF_GamesCollection (SKIF_GamesCollection const&) = delete; // Delete copy constructor
  SKIF_GamesCollection (SKIF_GamesCollection&&)      = delete; // Delete move constructor

private:
  void LoadCustomGames (std::vector <std::unique_ptr<app_generic_s>>* apps); // Load custom SKIF gmaes
  SKIF_GamesCollection (void);
};


#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

#include <utility/utility.h>

// Singleton struct
struct SKIF_LocalManager {

  struct install_s {
    std::wstring path;        // == Name  in registry
    std:: string path_utf8;
    std::wstring version;     // == Value in registry
    std:: string version_utf8;
    bool         outdated = false; // Derived, not stored in registry
    bool         selected = false; // UI state: user wants to perform an action on this install
    bool         deleted  = false;
    bool         exists   = false;
  };

  static SKIF_LocalManager& GetInstance (void)
  {
    static SKIF_LocalManager instance;
    return instance;
  }

         void ShowWindow        (void);
         void Process           (void);

private:
              SKIF_LocalManager (void);
              SKIF_LocalManager (SKIF_LocalManager const&) = delete;
              SKIF_LocalManager (SKIF_LocalManager&&)      = delete;
         void SetAnyOutdated    (void);
         void SetAnySelected    (void);
         void SetLatestVersion  (std::wstring_view v);
         void UpdateInstall     (install_s* install);
         void DeleteInstall     (install_s* install);
         void RefreshInstalls   (void);


  bool                   showWindow  = false;
  bool                   anyOutdated = false;
  bool                   anySelected = false;
  std:: string           label;
  std::wstring           latest_version;
  std::vector<install_s> installs;
  SKIF_RegistryWatch     watch;
};

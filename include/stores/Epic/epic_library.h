#pragma once
#include <utility/fsutil.h>
#include <vector>
#include <wtypes.h>
#include <WinInet.h>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>
#include <utility/utility.h>
#include <stores/generic_library2.h>
#include <stores/Steam/app_record.h>


struct SKIF_EpicCollection {

  bool isSignaled         (void);
  void GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps) const;
  void IdentifyAssetNew   (const app_record_s::epic_s& epic);

  static SKIF_EpicCollection& GetInstance (void)
  {
    static SKIF_EpicCollection instance;
    return instance;
  }

private:
  SKIF_EpicCollection (void) { };
  SKIF_EpicCollection (SKIF_EpicCollection const&) = delete;
  SKIF_EpicCollection (SKIF_EpicCollection&&)      = delete;

  void RefreshInstallState (void);
  bool FindInstalledGames2 (std::vector <std::pair < std::string, app_record_s > > *apps) const;
  bool FindInstalledGames1 (std::vector <std::pair < std::string, app_record_s > > *apps) const;

  SKIF_DirectoryWatch watch;
  std::wstring  appdatapath;
  int               version = -1; // -1 unchecked, 0 uninstalled, 1 v1, 2 v2
};


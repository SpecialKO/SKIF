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
#include <imgui/imgui.h>

// Singleton struct
struct SKIF_LocalManager {

  enum ColumnID_ : ImGuiID {
    Col_Select, Col_Path, Col_Version, Col_Updater, Col_Status, Col_Folder
  };

  struct install_s {
    std::wstring path;        // == Name  in registry
    std:: string path_utf8;
    std::wstring version;     // == Value in registry
    std:: string version_utf8;
    bool         outdated = false; // Derived, not stored in registry
    bool         selected = false; // UI state: user wants to perform an action on this install
    bool         deleted  = false;
    bool         exists   = false;
    bool         exists_parent = false;
  };

  static SKIF_LocalManager& GetInstance (void)
  {
    static SKIF_LocalManager instance;
    return instance;
  }

         void Show              (void);
         void Process           (void);

private:

              SKIF_LocalManager (void);
              SKIF_LocalManager (SKIF_LocalManager const&) = delete;
              SKIF_LocalManager (SKIF_LocalManager&&)      = delete;
         void UpdateAnyStates   (void);
         void SetLatestVersion  (std::wstring_view v);
         bool UpdateInstall     (install_s* install, PEBitness iBinaryType = PEBitness_Invalid);
         void InstallNew        (const std::wstring& dll_path, PEBitness iBinaryType);
         void BrowseNewInstall  (void);
         void DeleteInstall     (install_s* install);
         void RefreshInstalls   (void);
         void SortInstalls      (const ImGuiTableSortSpecs* specs);
         void HandleRowClick    (int idx, bool fromCheckbox);
         void HandleRowRightClick (int idx);
         void CycleSelectAll    (bool allowDeselect);
         int  FindInstall       (const std::wstring& path) const;
         
  HWND                   hwnd                = nullptr;
  install_s*             want_install        = nullptr;
  bool                   needs_sort          =    true;
  bool                   showWindow          =   false;
  bool                   anyMissing          =   false;
  bool                   anySelected         =   false;
  bool                   anyOutdated         =   false;
  bool                   anyOutdatedSelected =   false;
  bool                   want_new            =   false; // File -> New / Ctrl+N: open the file dialog
  bool                   popup_open_prev     =   false; // An ImGui popup was open last frame (keeps Escape from also closing the window)
  bool                   focused             =   false; // Window (or its children) focused this frame
  int                    hovered_row         =      -1; // Row under the mouse last frame (Shift range preview)
  PEBitness              new_bitness         = PEBitness_Invalid; // Bitness of new_exe_path
  std::wstring           new_exe_path;                  // Executable picked via File -> New, awaiting a DLL filename
  std:: string           new_exe_path_utf8;
  std::wstring           new_dll_path;                  // Target DLL path, awaiting a bitness choice
  std::vector<int>       new_dll_states;                // Per LocalDLLs entry: 0 = free, 1 = existing Special K DLL, 2 = other file in the way, 3 = Special K installed under another name
  std:: string           new_uptodate_desc;             // Non-empty: Special K in the picked folder is already up to date, show the info prompt
  std::wstring           anchor_path;                   // Last row clicked without Shift; start of Shift ranges
  std:: string           label;
  std::wstring           latest_version;
  std::vector<install_s> installs;
  SKIF_RegistryWatch     watch;
};

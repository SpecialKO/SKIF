#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include <unordered_set>
#include <optional>

#include <utility/ini_reader.h>
#include <utility/utility.h>

#include <utility/skif_imgui.h>
#include <imgui/imgui.h>

enum EditorAction
{
  None,
  New,
  NewWindow, // Not actually used
  Open,
  Save,
  SaveAs,    // Not actually used
  Close,     // Not actually used?
  Reload,
  Exit,
};

struct IniWindow {
  std::vector <IniEntry>     ini;
  std::vector <IniEntry> history;
  std::wstring           path;
  std:: string           path_utf8;
  std::wstring           path_parent;
  std:: string          title;
  std:: string       doc_name;
  std:: string       wnd_name;
  SKIF_DirectoryWatch   watch;
  DWORD                 watchCD = NULL; // Time
  DWORD                 saved   = NULL; // Time
  IniType                type   = IniType_Unknown;
  PopupState            state   = PopupState_Open;
  std:: string    prompt_name;
  bool            prompt_show   = false;
  HWND                   hwnd   = nullptr;
  EditorAction    want_action   = EditorAction::None;
  int             public_seed   = 0;
  std::unordered_set
    <ImGuiID> collapsed_sections;

  // Filter field
  char          charFilter    [MAX_PATH + 2] = { };
  char          charFilterTmp [MAX_PATH + 2] = { };
  bool          bFilterActive = false;

  // Recent searches, shown below the filter field (the entries are shared across all windows through the registry)
  SKIF_ImGui_FilterHistory filterHistory;

  // Changed anything?
  bool          bChanged = false;
  // What Undo Changes reverts to after a preset was applied (the entries as last saved), as a preset replaces the entries
  std::optional <std::vector <IniEntry>>
                undo_snapshot;
  // Should unused settings be visible
  bool          bViewUnused = false;

  std::string   path_filename = "";

  // Functions
  void ApplyFilter       (void);
  void ClearFilter       (void);
  void UpdateWindowTitle (void);
  void SetPath           (const std::wstring& _p);
       IniWindow         (std::vector<IniEntry>  _i, IniType _ty, const std::wstring& _p = L"", const std::string& _ti = ""); // Pass with std::move to avoid a copy

   IniWindow            (const IniWindow& )          = default;
   IniWindow& operator= (const IniWindow& )          = default;
   IniWindow            (      IniWindow&&) noexcept = default;
   IniWindow& operator= (      IniWindow&&) noexcept = default;

private:
  std::string label;
};

void SKIF_UI_IniEditor_NewFile  (IniWindow* iniWindow);
void SKIF_UI_IniEditor_NewWindow(void);
void SKIF_UI_IniEditor_Save     (IniWindow* iniWindow);
void SKIF_UI_IniEditor_SaveAs   (IniWindow* iniWindow);
void SKIF_UI_IniEditor_OpenFile (IniWindow* iniWindow = nullptr, IniType type = IniType_Unknown, std::wstring path = L"", const std::string& title = "");
void SKIF_UI_IniEditor_Reload   (IniWindow* iniWindow);
void SKIF_UI_IniEditor_Reset    (IniWindow* iniWindow);
void SKIF_UI_IniEditor_ApplyPreset (IniWindow* iniWindow, std::vector<IniEntry> entries);
void SKIF_UI_IniEditor_Process  (void);

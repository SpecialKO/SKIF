#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

#include <utility/ini_reader.h>
#include <utility/utility.h>

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
  std::vector <__INI>     ini;
  std::vector <__INI> history;
  std::wstring           path;
  std:: string           path_utf8;
  std::wstring           path_parent;
  std:: string          title;
  std:: string       doc_name;
  std:: string       wnd_name;
  SKIF_DirectoryWatch   watch;
  DWORD                 watchCD = NULL;
  IniType                type   = IniType_Unknown;
  PopupState            state   = PopupState_Open;
  bool            prompt_save   = false;
  HWND                   hwnd   = nullptr;
  EditorAction    want_action   = EditorAction::None;
  int             public_seed   = 0;

  // Filter field
  char          charFilter    [MAX_PATH + 2] = { };
  char          charFilterTmp [MAX_PATH + 2] = { };
  bool          bFilterActive = false;

  // Focused state
  bool          bFocused = false;
  // Changed anything?
  bool          bChanged = false;

  std::string   path_filename = "";

  // Functions
  void ApplyFilter       (void);
  void ClearFilter       (void);
  void UpdateWindowTitle (void);
  void SetPath           (const std::wstring& _p);
       IniWindow         (std::vector<__INI> _i, IniType _ty, const std::wstring& _p = L"", const std::string& _ti = "");

   IniWindow            (const IniWindow& )          = default;
   IniWindow& operator= (const IniWindow& )          = default;
   IniWindow            (      IniWindow&&) noexcept = default;
   IniWindow& operator= (      IniWindow&&) noexcept = default;

private:
  std::string label;
};

void                             SKIF_ImGui_IniEditor_NewFile  (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_NewWindow(void);
void                             SKIF_ImGui_IniEditor_Save     (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_SaveAs   (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_OpenFile (IniWindow* iniWindow = nullptr, IniType type = IniType_Unknown, std::wstring path = L"", const std::string& title = "");
void                             SKIF_ImGui_IniEditor_Reload   (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_Reset    (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_Process  (void);

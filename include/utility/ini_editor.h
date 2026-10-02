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

struct IniWindow {
  std::vector <__INI> ini;
  std::string        path;
  std::string       title;
  std::string    wnd_name;
  PopupState        state = PopupState_Open;

  // Filter field
  char          charFilter    [MAX_PATH + 2] = { };
  char          charFilterTmp [MAX_PATH + 2] = { };
  bool          bFilterActive = false;

  // Focused state
  bool          bFocused = false;
  // Changed anything?
  bool          bChanged = false;

  // Functions
  IniWindow (std::vector<__INI> _i, const std::string& _p = "", const std::string& _t = "");

  void UpdateWindowTitle (void) {
    wnd_name = (((title.empty()) ? "Unsaved" : title) + " - Editor" + label); // + (bChanged ? "*" : "")
  }

private:
  int         index = 0;
  std::string label;
};

void                             SKIF_ImGui_IniEditor_NewFile  (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_NewWindow(void);
void                             SKIF_ImGui_IniEditor_Save     (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_SaveAs   (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_OpenFile (IniWindow* iniWindow = nullptr, std::wstring path = L"", const std::string& title = "");
void                             SKIF_ImGui_IniEditor_Process  (void);

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

#include <SKIF.h>
#include <utility/sk_utility.h>

typedef unsigned int ParameterType;  // -> enum SettingType_

enum ParameterType_
{
  ParameterType_Unknown   = 0,

  // Core
  ParameterType_Float      = 1 << 0, // float
  ParameterType_Boolean    = 1 << 1, // bool
  ParameterType_Integer    = 1 << 2, // int
  ParameterType_Integer64  = 1 << 3, // int64
  ParameterType_StringW    = 1 << 4, // std::wstring

  // Custom
  ParameterType_DCBooolean    = 1 << 5, // DontCare, true, false
  ParameterType_SyncInterval  = 1 << 6, // -1, 0, 1, 2, 3, 4
};

struct __INI {
  char section[MAX_PATH + 2] = { };
  char key    [MAX_PATH + 2] = { };
  char value  [MAX_PATH + 2] = { };
  char default[MAX_PATH + 2] = { }; // Used when resetting any unsaved changes
  std::string _label_k;
  std::string _label_v;
  bool        _show = true;
  SK_KeybindMultiState _keybind;

  //
  void (*DrawFunction)(__INI* ptr);
  std::vector<std::string> _dditems = { };

  __INI (const std::string& _s, const std::string& _k, const std::string& _v);
  void Reset (void);
};

struct __INIFile {
  int     wnd_index = 0;
  std::string  path = { };
  std::string label = { };
  PopupState  state = PopupState_Open;
  std::vector <__INI> ini;

  // Filter field
  char          charFilter    [MAX_PATH + 2] = { };
  char          charFilterTmp [MAX_PATH + 2] = { };
  bool          bFilterActive = false;

  // Focused state
  bool          bWindowFocused = false;
};

// DERP

std::wstring SKIF_IniHandler_ReadTextFile (const std::wstring& path);

// NO DERP

void                      SKIF_ImGui_IniEditor_OpenFile (const std::wstring& file_path, const std::string& file_path_utf8);
void                      SKIF_ImGui_IniEditor_Process  (void);
std::vector <__INI>       SKIF_IniHandler_ParseIni      (const std::wstring& file_path, const std::string& file_path_utf8);
void                      SKIF_IniHandler_WriteIni      (std::vector <__INI> ini,       const std::string& file_path_utf8);


// CC BY-SA 4.0: https://stackoverflow.com/a/46711735
static constexpr uint32_t
SwitchHash (const std::string_view data) noexcept
{
  uint32_t hash = 5385;

  for (const auto& e : data)
    hash = ((hash << 5) + hash) + e;

  return hash;
}
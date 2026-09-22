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

typedef unsigned int ParameterType;  // -> enum ParameterType_

enum ParameterType_
{
  ParameterType_Unknown   = 0,

  // Core
  ParameterType_Float        = 1 << 0, // float
  ParameterType_Boolean      = 1 << 1, // bool
  ParameterType_Integer      = 1 << 2, // int
  ParameterType_Integer64    = 1 << 3, // int64
  ParameterType_StringW      = 1 << 4, // std::wstring

  // Custom
  ParameterType_SyncInterval = 1 << 6, // -1, 0, 1, 2, 3, 4
  ParameterType_DropDownList = 1 << 7, // DrawDropDown: NotifyCorner, Scaling, ScanlineOrder, ExceptionMode
  ParameterType_Keybinding   = 1 << 8, // DrawKeybinding
};

typedef unsigned int IniFileType; // -> enum IniFile_
typedef unsigned int IniBitness;  // -> enum IniBitness_

enum IniFileType_
{
  IniFile_Unknown  = 0,
  IniFile_DLL      = 1 << 0,
  IniFile_Input    = 1 << 1,
  IniFile_OSD      = 1 << 2,
  IniFile_Notify   = 1 << 3,
  IniFile_Platform = 1 << 4,
};

enum IniBitness_
{
  IniBitness_All   = 0,
  IniBitness_i8086 = 1 << 0,
  IniBitness_AMD64 = 1 << 1,
};

constexpr IniFileType osd_ini      = IniFileType_::IniFile_OSD;
constexpr IniFileType dll_ini      = IniFileType_::IniFile_DLL;
constexpr IniFileType input_ini    = IniFileType_::IniFile_Input;
constexpr IniFileType notify_ini   = IniFileType_::IniFile_Notify;
constexpr IniFileType platform_ini = IniFileType_::IniFile_Platform;

struct ConfigEntry
{
  const char             *description_ = nullptr;
  IniFileType             ini_;
  const char             *section_     = nullptr;
  const char             *key_         = nullptr;
  IniBitness              arch_;
};

struct __INI {
  char section[MAX_PATH + 2] = { };
  char key    [MAX_PATH + 2] = { };
  char value  [MAX_PATH + 2] = { };
  char default[MAX_PATH + 2] = { }; // Used when resetting any unsaved changes
  std::string _label_k;
  std::string _label_v;
  bool        _show = true;
  bool        _ignore_if_unset = false; // Used to prevent empty and unset parameters from being populated on write
  SK_KeybindMultiState _keybind;
  bool (*DrawFunction)(__INI* ptr) = nullptr;
  ParameterType _param_type = ParameterType_Unknown;

  // ParameterType_Boolean
  bool value_b   = false;
  bool default_b = false;

  // ParameterType_DropDownList 
  std::vector<std::string> _dditems = { };

              __INI    (const std::string& _s, const std::string& _k, const std::string& _v);
        void  Reset    (void);
};

struct IniWindow {
  std::vector <__INI> ini;
  std::string        path;
  std::string       title;
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
  IniWindow (std::vector<__INI> _i, const std::string& _p = "");

  void UpdateWindowTitle (void) {
    title = (((path.empty()) ? "Editor" : path) + (bChanged ? "*" : "") + label);
  }

private:
  int         index = 0;
  std::string label;
};

void                             SKIF_ImGui_IniEditor_NewFile  (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_NewWindow(void);
void                             SKIF_ImGui_IniEditor_SaveAs   (IniWindow* iniWindow);
void                             SKIF_ImGui_IniEditor_OpenFile (std::wstring path, IniWindow* iniWindow = nullptr);
void                             SKIF_ImGui_IniEditor_Process  (void);
std::vector <__INI>              SKIF_IniHandler_ParseIni      (const std::wstring& file_path, const std::string& file_path_utf8);
void                             SKIF_IniHandler_WriteIni      (std::vector <__INI> ini,       const std::string& file_path_utf8);
std::vector <const ConfigEntry*> SKIF_IniHandler_GetParams     (IniFileType ini);

// CC BY-SA 4.0: https://stackoverflow.com/a/46711735
static constexpr uint32_t
SwitchHash (const std::string_view data) noexcept
{
  uint32_t hash = 5385;

  for (const auto& e : data)
    hash = ((hash << 5) + hash) + e;

  return hash;
}
#pragma once

#include <SKIF.h>
#include <utility/sk_utility.h>
#include <../packages_misc/ini.h>

#include <vector>
#include <string>

typedef unsigned int IniType;       // -> enum IniType_
typedef unsigned int IniBitness;    // -> enum IniBitness_
typedef unsigned int ParameterType; // -> enum ParameterType_

enum IniType_
{
  IniType_Unknown  = 0,
  IniType_DLL      = 1 << 0,
  IniType_OSD      = 1 << 1,
  IniType_Input    = 1 << 2,
  IniType_Macros   = 1 << 3,
  IniType_Notify   = 1 << 4,
  IniType_Platform = 1 << 5,
};

enum IniBitness_
{
  IniBitness_All   = 0,
  IniBitness_i8086 = 1 << 0,
  IniBitness_AMD64 = 1 << 1,
};

enum ParameterType_ {
  ParameterUnknown = 0,
  ParameterBool    = 1 << 0,
  ParameterInt     = 1 << 1,
  ParameterInt64   = 1 << 2,
  ParameterFloat   = 1 << 3,
  ParameterStringW = 1 << 4,
  ParameterKeybind = 1 << 5,
};

constexpr IniType dll_ini      = IniType_::IniType_DLL;
constexpr IniType osd_ini      = IniType_::IniType_OSD;
constexpr IniType input_ini    = IniType_::IniType_Input;
constexpr IniType macros_ini   = IniType_::IniType_Macros;
constexpr IniType notify_ini   = IniType_::IniType_Notify;
constexpr IniType platform_ini = IniType_::IniType_Platform;

struct ConfigEntry
{
  ParameterType            param_type = ParameterUnknown;
  const char             *description;
  IniType                    ini_type = IniType_Unknown;
  const char                 *section;
  const char                     *key;
  IniBitness                     arch = IniBitness_All;
};

struct __INI {
  char  section[MAX_PATH + 2] = { };
  char  key    [MAX_PATH + 2] = { };
  char  value  [MAX_PATH + 2] = { };
  char  default[MAX_PATH + 2] = { }; // Used when resetting any unsaved changes
  const char* description   = nullptr;
  ParameterType _type = ParameterUnknown;
  std::string _label_k;
  std::string _label_v;
  bool        _show = true;
  bool        _ignore = false; // Used to prevent empty and unset parameters from being populated on write
  SK_KeybindMultiState _keybind;
  bool (*DrawFunction)(__INI* ptr) = nullptr;

  // ParameterType_Boolean
  bool value_b   = false;
  bool default_b = false;

  // ParameterType_DropDownList
  std::vector<std::string> _dditems = { };
  
        void  Reset    (void);
              __INI    (ParameterType _t, std::string _s, std::string _k, std::string _v);
};

std::vector <__INI>              SKIF_IniReader_ParseIni         (const std::wstring& file_path, const std::string& file_path_utf8, IniType ini_type);
void                             SKIF_IniReader_WriteIni         (std::vector <__INI> ini,       const std::string& file_path_utf8);
void                             SKIF_IniReader_ReadOSDIni       (void);
void                             SKIF_IniReader_SaveOSDIni       (void);
std::vector <const ConfigEntry*> SKIF_IniReader_GetDefaultParams (IniType ini_type);
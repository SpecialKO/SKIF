#pragma once

#include <string>

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

void SKIF_ImGui_IniEditor         (const std::wstring& file_path, const std::string& file_path_utf8);
void SKIF_ImGui_IniEditor_Process (void);
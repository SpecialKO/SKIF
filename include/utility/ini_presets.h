#pragma once

#include <string>

// Special K's INI presets, shared by the Library tab (which applies them to the config file of a game) and the INI editor
//   (which applies them to the open document, as unsaved changes):
//
//   - Custom presets:  %specialk_userdata%\Global\Custom\*.ini
//   - Default presets: %specialk_userdata%\Global\default_*.ini (the ones Special K itself applies to new configs)
//   - The compatibility ("sledgehammer") config below
//   - Reset (an empty config)

// Constants

constexpr const wchar_t* utf8_bom          =  L"\xEF\xBB\xBF";
constexpr const wchar_t* cfg_compatibility =
LR"([SpecialK.System]
ShowEULA=false
GlobalInjectDelay=0.0

[API.Hook]
d3d9=true
d3d9ex=true
d3d11=true
OpenGL=true
d3d12=true
Vulkan=true

[Steam.Log]
Silent=true

[Input.libScePad]
Enable=false

[Input.XInput]
Enable=false
UISlot=4

[Input.Gamepad]
EnableDirectInput7=false
EnableDirectInput8=false
EnableHID=false
EnableNativePS4=false
EnableRawInput=true
AllowHapticUI=false

[Input.Keyboard]
CatchAltF4=false
BypassAltF4Handler=false

[Textures.D3D11]
Cache=false)";

// What was picked in the preset items

enum SKIF_IniPresetAction
{
  SKIF_IniPresetAction_None,
  SKIF_IniPresetAction_Apply,         // Apply the preset file in .path
  SKIF_IniPresetAction_Compatibility, // Apply the compatibility ("sledgehammer") config (cfg_compatibility)
  SKIF_IniPresetAction_Reset,         // Apply an empty config
  SKIF_IniPresetAction_Edit,          // Open the preset file in .path in the INI editor (it exists, "Create New" creates it)
};

struct SKIF_IniPresetChoice
{
  SKIF_IniPresetAction action = SKIF_IniPresetAction_None;
  std::wstring         path;          // SKIF_IniPresetAction_Apply and SKIF_IniPresetAction_Edit
};

// Draws the preset items, for a popup or a menu: the "Apply Preset" menu (custom presets, default presets and
//   "Create New"), a separator, "Apply Compatibility Config" and "Reset". Right-clicking a preset offers to edit it.
//   Nothing is applied or opened here, the caller acts on the returned choice (the INI editor cannot open new editor
//     windows in the middle of drawing its own ones). The preset lists are refreshed when their folders change
SKIF_IniPresetChoice SKIF_ImGui_IniPresetItems (bool handCursor = true);

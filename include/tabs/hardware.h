#pragma once

void SKIF_UI_Tab_DrawHardware (void);

// Vulkan layer stuff
#include <string>
#include <vector>
#include <strsafe.h>

enum VkLayerType {
  VkLayerType_Implicit,
  VkLayerType_Explicit,
  VkLayerType_Unknown
};

struct VkLayer {
  struct reg {
    std::wstring    Key         = L"";
    std::wstring    Value       = L"";
    DWORD           Data        =   0; // 0 = enabled; >0 = disabled
    bool            WOW6432Node = false;
  };
    
  std:: string      Name;
  std::wstring      Pattern   = L"";
  std::vector <reg> Matches   = { };
  bool              isEnabled = false; // Simplified state
  bool              isIncompatible = false; // Is the layer flagged as having known compatibility issues?
  VkLayerType       Type      = VkLayerType_Unknown;

  std:: string      uiWarnLabel; // Label used on the Monitor tab
  std:: string      uiHoverTxt;  // Status bar text
  std:: string      uiHoverTip;  // Tooltip text
  std::wstring      regCmd;      // Combined command to toggle

  void Toggle (void);
};

extern std::pair<bool, std::vector<VkLayer>> g_VkLayers;

void SKIF_Hardware_RefreshVulkanLayers (void);
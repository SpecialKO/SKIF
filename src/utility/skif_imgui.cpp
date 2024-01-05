#include <utility/skif_imgui.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <utility/sk_utility.h>
#include <utility/utility.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <fonts/fa_solid_900.ttf.h>
#include <fonts/fa_brands_400.ttf.h>
#include <imgui/D3D11/imgui_impl_dx11.h>

bool SKIF_bFontChineseSimplified   = false,
     SKIF_bFontChineseAll          = false,
     SKIF_bFontCyrillic            = false,
     SKIF_bFontJapanese            = false,
     SKIF_bFontKorean              = false,
     SKIF_bFontThai                = false,
     SKIF_bFontVietnamese          = false;

ImFont* fontConsolas = nullptr;
ImFont* fontFAS      = nullptr;
ImFont* fontFAR      = nullptr;

float
SKIF_ImGui_sRGBtoLinear (float col_srgb)
{
  return (col_srgb <= 0.04045f)
        ? col_srgb / 12.92f
        : pow ((col_srgb + 0.055f) / 1.055f, 2.4f);
}

ImVec4
SKIF_ImGui_sRGBtoLinear (ImVec4 col)
{
    col.x = SKIF_ImGui_sRGBtoLinear (col.x);
    col.y = SKIF_ImGui_sRGBtoLinear (col.y);
    col.z = SKIF_ImGui_sRGBtoLinear (col.z);
    col.w = SKIF_ImGui_sRGBtoLinear (col.w);

    return col;
}

float
SKIF_ImGui_LinearTosRGB (float col_lin)
{
  return (col_lin <= 0.0031308f)
        ? col_lin * 12.92f
        : 1.055f * pow (col_lin, 1.0f / 2.4f) - 0.055f;
}

ImVec4
SKIF_ImGui_LinearTosRGB (ImVec4 col)
{
    col.x = SKIF_ImGui_LinearTosRGB (col.x);
    col.y = SKIF_ImGui_LinearTosRGB (col.y);
    col.z = SKIF_ImGui_LinearTosRGB (col.z);
    col.w = SKIF_ImGui_LinearTosRGB (col.w);

    return col;
}


void
SKIF_ImGui_StyleColorsDark (ImGuiStyle* dst)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
  ImVec4* colors = style->Colors;

  // Text
  colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.30f); //ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

  // Window, Child, Popup
  colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); // ImVec4(0.08f, 0.08f, 0.08f, 0.90f);

  // Borders
  colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

  // Frame [Checkboxes, Radioboxes]
  colors[ImGuiCol_FrameBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Title Background [Popups]
  colors[ImGuiCol_TitleBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  colors[ImGuiCol_TitleBgActive]          = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // Unchanged

  // MenuBar
  colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

  // CheckMark / Radio Button
  colors[ImGuiCol_CheckMark]              = ImVec4(0.45f, 0.45f, 0.45f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f)

  // Slider Grab (used for HDR Brightness slider)
  colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

  // Buttons
  colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_ButtonHovered]          = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
  colors[ImGuiCol_ButtonActive]           = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);

  // Headers [Selectables, CollapsibleHeaders]
  colors[ImGuiCol_Header]                 = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
  colors[ImGuiCol_HeaderHovered]          = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
  colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Scrollbar
  colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_ScrollbarGrab]          = colors[ImGuiCol_Header];
  colors[ImGuiCol_ScrollbarGrabHovered]   = colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_ScrollbarGrabActive]    = colors[ImGuiCol_HeaderActive];

  // Separators
  colors[ImGuiCol_Separator]              = (_registry.bUIBorders) ? colors[ImGuiCol_Border]
                                                                   : ImVec4(0.15f, 0.15f, 0.15f, 1.00f); // colors[ImGuiCol_WindowBg];
  colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
  colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);

  // Resize Grip
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Tabs
  colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);       //ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.80f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
  colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);       //ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
  colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

  // Docking stuff
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Plot
  colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

  // Drag-n-drop
  colors[ImGuiCol_DragDropTarget]         = ImVec4(0.90f, 0.90f, 0.10f, 1.00f);

  // Nav/Modal
  colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 1.00f, 1.00f, 0.70f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // ImVec4(0.80f, 0.80f, 0.80f, 0.20f)
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

  // Custom
  colors[ImGuiCol_SKIF_TextBase]          = ImVec4(0.68f, 0.68f, 0.68f, 1.00f);
  colors[ImGuiCol_SKIF_TextCaption]       = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
  colors[ImGuiCol_SKIF_TextGameTitle]     = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_SKIF_Success]           = ImColor(121, 214, 28);  // 42,  203, 2);  //53,  255, 3);  //ImColor(144, 238, 144);
  colors[ImGuiCol_SKIF_Warning]           = ImColor(255, 124, 3); // ImColor::HSV(0.11F, 1.F, 1.F)
  colors[ImGuiCol_SKIF_Failure]           = ImColor(186, 59, 61, 255);
  colors[ImGuiCol_SKIF_Info]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // colors[ImGuiCol_CheckMark];
  colors[ImGuiCol_SKIF_Yellow]            = ImColor::HSV(0.11F, 1.F, 1.F);
}

void
SKIF_ImGui_StyleColorsLight (ImGuiStyle* dst)
{
  ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
  ImVec4* colors = style->Colors;

  // Text
  colors[ImGuiCol_Text]                   = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
  colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);

  // Window, Child, Popup
  colors[ImGuiCol_WindowBg]               = ImVec4(0.94f, 0.94f, 0.94f, 1.00f); // ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
  colors[ImGuiCol_PopupBg]                = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);

  // Borders
  colors[ImGuiCol_Border]                 = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
  colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  // Frame [Checkboxes, Radioboxes]
  colors[ImGuiCol_FrameBg]                = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.78f, 0.78f, 0.78f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  colors[ImGuiCol_FrameBgActive]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.67f);

  // Title Background [Popups]
  colors[ImGuiCol_TitleBg]                = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
  colors[ImGuiCol_TitleBgActive]          = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);

  // MenuBar
  colors[ImGuiCol_MenuBarBg]              = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);

  // CheckMark / Radio Button
  colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

  // Slider Grab (used for HDR Brightness slider)
  colors[ImGuiCol_SliderGrab]             = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.46f, 0.54f, 0.80f, 0.60f);

  // Buttons
  colors[ImGuiCol_Button]                 = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  colors[ImGuiCol_ButtonHovered]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_ButtonActive]           = ImVec4(0.95f, 0.95f, 0.95f, 1.00f); // ImVec4(0.06f, 0.53f, 0.98f, 1.00f);

  // Headers [Selectables, CollapsibleHeaders]
  colors[ImGuiCol_Header]                 = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
  colors[ImGuiCol_HeaderHovered]          = ImVec4(0.88f, 0.88f, 0.88f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_HeaderActive]           = ImVec4(0.80f, 0.80f, 0.80f, 1.00f); // ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

  // Scrollbar
  colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.98f, 0.98f, 0.98f, 0.00f);
  colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.69f, 0.69f, 0.69f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.49f, 0.49f, 0.49f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);

  // Separators
  colors[ImGuiCol_Separator]              = ImVec4(0.39f, 0.39f, 0.39f, 0.62f);
  colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.14f, 0.44f, 0.80f, 0.78f);
  colors[ImGuiCol_SeparatorActive]        = ImVec4(0.14f, 0.44f, 0.80f, 1.00f);

  // Resize Grip
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.80f, 0.80f, 0.80f, 0.56f);
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Tabs
  colors[ImGuiCol_Tab]                    = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.90f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_TabActive]              = ImVec4(0.80f, 0.80f, 0.80f, 1.00f); // ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
  colors[ImGuiCol_TabUnfocused]           = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);

  // Docking stuff
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_Header] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Plot
  colors[ImGuiCol_PlotLines]              = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.45f, 0.00f, 1.00f);

  // Drag-n-drop
  colors[ImGuiCol_DragDropTarget]         = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Nav/Modal
  colors[ImGuiCol_NavHighlight]           = colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.70f, 0.70f, 0.70f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

  // Custom
  colors[ImGuiCol_SKIF_TextBase]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_SKIF_TextCaption]       = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  colors[ImGuiCol_SKIF_TextGameTitle]     = colors[ImGuiCol_Text];
  colors[ImGuiCol_SKIF_Success]           = ImColor(86, 168, 64); //144, 238, 144) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
  colors[ImGuiCol_SKIF_Warning]           = ImColor(240, 139, 24); // ImColor::HSV(0.11F, 1.F, 1.F)
  colors[ImGuiCol_SKIF_Failure]           = ImColor(186, 59, 61, 255);
  colors[ImGuiCol_SKIF_Info]              = colors[ImGuiCol_CheckMark];
  colors[ImGuiCol_SKIF_Yellow]            = ImColor::HSV(0.11F, 1.F, 1.F);
}

void
SKIF_ImGui_AdjustAppModeSize (HMONITOR monitor)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern ImVec2 SKIF_vecRegularModeDefault;
  extern ImVec2 SKIF_vecRegularModeAdjusted;
  extern ImVec2 SKIF_vecHorizonModeDefault;
  extern ImVec2 SKIF_vecHorizonModeAdjusted;
  extern ImVec2 SKIF_vecServiceModeDefault;
  extern ImVec2 SKIF_vecAlteredSize;
  extern float  SKIF_fStatusBarHeight;
  extern float  SKIF_fStatusBarDisabled;
  extern float  SKIF_fStatusBarHeightTips;

  // Reset reduced height
  SKIF_vecAlteredSize.y = 0.0f;

  // Adjust the large mode size
  SKIF_vecRegularModeAdjusted = SKIF_vecRegularModeDefault;
  SKIF_vecHorizonModeAdjusted = SKIF_vecHorizonModeDefault;

  // Add the status bar if it is not disabled
  if (_registry.bUIStatusBar)
  {
    SKIF_vecRegularModeAdjusted.y += SKIF_fStatusBarHeight;
    SKIF_vecHorizonModeAdjusted.y += SKIF_fStatusBarHeight;

    if (! _registry.bUITooltips)
    {
      SKIF_vecRegularModeAdjusted.y += SKIF_fStatusBarHeightTips;
      SKIF_vecHorizonModeAdjusted.y += SKIF_fStatusBarHeightTips;
    }
  }

  else
  {
    SKIF_vecRegularModeAdjusted.y += SKIF_fStatusBarDisabled;
    SKIF_vecHorizonModeAdjusted.y += SKIF_fStatusBarDisabled;
  }

  // Take the current display into account
  if (monitor == NULL && SKIF_ImGui_hWnd != NULL)
    monitor = ::MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);

  MONITORINFO
    info        = {                  };
    info.cbSize = sizeof (MONITORINFO);

  if (monitor != NULL && ::GetMonitorInfo (monitor, &info))
  {
    ImVec2 WorkSize =
      ImVec2 ( (float)( info.rcWork.right  - info.rcWork.left ),
                (float)( info.rcWork.bottom - info.rcWork.top  ) );

    ImVec2 tmpCurrentSize  = (_registry.bServiceMode) ? SKIF_vecServiceModeDefault  :
                              (_registry.bHorizonMode) ? SKIF_vecHorizonModeAdjusted :
                                                        SKIF_vecRegularModeAdjusted ;

    if (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
      SKIF_vecAlteredSize.y = (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y));
  }
}

bool
SKIF_ImGui_IsFocused (void)
{
  extern bool SKIF_ImGui_ImplWin32_IsFocused (void);
  return SKIF_ImGui_ImplWin32_IsFocused ( );
}

bool
SKIF_ImGui_IsMouseHovered (void)
{
  POINT                 mouse_screen_pos = { };
  if (!::GetCursorPos (&mouse_screen_pos))
    return false;

  // See if we are currently hovering over one of our viewports
  if (HWND hovered_hwnd = ::WindowFromPoint (mouse_screen_pos))
    if (NULL != ImGui::FindViewportByPlatformHandle ((void *)hovered_hwnd))
      return true; // We are in fact hovering over something

  return false;
}

bool
SKIF_ImGui_IsAnyInputDown (void)
{
  ImGuiContext& g = *GImGui;

  for (int n = 0; n < IM_ARRAYSIZE(g.IO.MouseDown); n++)
    if (g.IO.MouseDown[n])
      return true;
    
  for (int n = 0; n < IM_ARRAYSIZE(g.IO.KeysDown); n++)
    if (g.IO.KeysDown[n])
      return true;

  for (int n = 0; n < IM_ARRAYSIZE(g.IO.NavInputs); n++)
    if (g.IO.NavInputs[n])
      return true;

  return false;
}

void
SKIF_ImGui_SetMouseCursorHand (void)
{
  // Only change the cursor if the current item is actually being hovered **and** the cursor is the one hovering it.
  // IsItemHovered() fixes cursor changing for overlapping items, and IsMouseHoveringRect() fixes cursor changing due to keyboard/gamepad selections
  if (ImGui::IsItemHovered ( ) && ImGui::IsMouseHoveringRect (ImGui::GetItemRectMin( ), ImGui::GetItemRectMax( )))
  {
    extern bool SKIF_MouseDragMoveAllowed;
    SKIF_MouseDragMoveAllowed = false;
    ImGui::SetMouseCursor (
      ImGuiMouseCursor_Hand
    );
  }
}

void
SKIF_ImGui_SetHoverTip (const std::string_view& szText, bool ignoreDisabledTooltips)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern bool        HoverTipActive;        // Used to track if an item is being hovered
  extern DWORD       HoverTipDuration;      // Used to track how long the item has been hovered (to delay showing tooltips)
  extern std::string SKIF_StatusBarText;

  if (ImGui::IsItemHovered ())
  {
    if (_registry.bUITooltips || ignoreDisabledTooltips)
    {
      ImGui::PushStyleColor (ImGuiCol_PopupBg, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
      ImGui::PushStyleColor (ImGuiCol_Text,    ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
      HoverTipActive = true;

      if ( HoverTipDuration == 0)
      {
        HoverTipDuration = SKIF_Util_timeGetTime ( );

        // Use a timer to force SKIF to refresh once the duration has passed
        SetTimer (SKIF_Notify_hWnd,
            IDT_REFRESH_TOOLTIP,
            550,
            (TIMERPROC) NULL
        );
      }

      else if ( HoverTipDuration + 500 < SKIF_Util_timeGetTime() )
      {
        ImGui::SetTooltip (
          "%hs", szText.data ()
        );
      }

      ImGui::PopStyleColor  ( );
      ImGui::PopStyleColor  ( );
    }

    else
    {
      SKIF_StatusBarText =
        "Info: ";

      SKIF_ImGui_SetHoverText (
        szText.data (), true
      );
    }
  }
}

void
SKIF_ImGui_SetHoverText ( const std::string_view& szText,
                              bool  overrideExistingText )
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern std::string SKIF_StatusBarHelp;

  if ( ImGui::IsItemHovered ()                                  &&
        ( overrideExistingText || SKIF_StatusBarHelp.empty () ) &&
        (                       ! _registry.bServiceMode         )
     )
  {
    extern ImVec2 SKIF_vecCurrentMode;

    // If the text is wider than the app window is, use a hover tooltip if possible
    if (_registry.bUITooltips && ImGui::CalcTextSize (szText.data()).x > (SKIF_vecCurrentMode.x - 100.0f * SKIF_ImGui_GlobalDPIScale)) // -100px due to the Add Game option
      SKIF_ImGui_SetHoverTip (szText);
    else
      SKIF_StatusBarHelp.assign (szText);
  }
}

void
SKIF_ImGui_Spacing (float multiplier)
{
  ImGui::ItemSize (
    ImVec2 ( ImGui::GetTextLineHeightWithSpacing () * multiplier,
             ImGui::GetTextLineHeightWithSpacing () * multiplier )
  );
}

void
SKIF_ImGui_Spacing (void)
{
  SKIF_ImGui_Spacing (0.25f);
}

// Difference to regular Selectable? Doesn't span further than the width of the label!
bool
SKIF_ImGui_Selectable (const char* label)
{
  return ImGui::Selectable  (label, false, ImGuiSelectableFlags_None, ImGui::CalcTextSize (label, NULL, true));
}

// Difference to regular BeginChildFrame? No ImGuiWindowFlags_NoMove!
bool
SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags)
{
  const ImGuiStyle& style =
    ImGui::GetStyle ();

  //ImGui::PushStyleColor (ImGuiCol_ChildBg,              style.Colors [ImGuiCol_FrameBg]);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding,   style.FrameRounding);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildBorderSize, style.FrameBorderSize);
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,   style.FramePadding);

  bool ret =
    ImGui::BeginChild (id, size, true, ImGuiWindowFlags_AlwaysUseWindowPadding | extra_flags);

  ImGui::PopStyleVar   (3);
  //ImGui::PopStyleColor ( );

  return ret;
}

// Basically like ImGui::Image but, you know, doesn't actually draw the images
void SKIF_ImGui_OptImage (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
{
  // If not a nullptr, run original code
  if (user_texture_id != nullptr)
  {
    ImGui::Image (user_texture_id, size, uv0, uv1, tint_col, border_col);
  }
  
  // If a nullptr, run slightly tweaked code that omitts the image rendering
  else {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    if (border_col.w > 0.0f)
        bb.Max += ImVec2(2, 2);
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, 0))
        return;

    if (border_col.w > 0.0f)
    {
        window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border_col), 0.0f);
        //window->DrawList->AddImage(user_texture_id, bb.Min + ImVec2(1, 1), bb.Max - ImVec2(1, 1), uv0, uv1, ImGui::GetColorU32(tint_col));
    }
    else
    {
        //window->DrawList->AddImage(user_texture_id, bb.Min, bb.Max, uv0, uv1, ImGui::GetColorU32(tint_col));
    }
  }
}

// Difference from regular? Who knows
void
SKIF_ImGui_Columns (int columns_count, const char* id, bool border, bool resizeble)
{
  ImGuiWindow* window = ImGui::GetCurrentWindowRead();
  IM_ASSERT(columns_count >= 1);

  ImGuiColumnsFlags flags = (border ? 0 : ImGuiColumnsFlags_NoBorder);
  if (! resizeble)
    flags |= ImGuiColumnsFlags_NoResize;
  //flags |= ImGuiColumnsFlags_NoPreserveWidths; // NB: Legacy behavior
  ImGuiColumns* columns = window->DC.CurrentColumns;
  if (columns != NULL && columns->Count == columns_count && columns->Flags == flags)
    return;

  if (columns != NULL)
    ImGui::EndColumns();

  if (columns_count != 1)
    ImGui::BeginColumns(id, columns_count, flags);
}

// This is used to set up the main content area for all tabs
void SKIF_ImGui_BeginTabChildFrame (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern ImVec2 SKIF_vecAlteredSize;
  extern float  SKIF_fStatusBarDisabled;  // Status bar disabled

  auto frame_content_area_id =
    ImGui::GetID ("###SKIF_CONTENT_AREA");

  float maxContentHeight = (_registry.bHorizonMode) ? 286.0f : 908.0f; // Default height -- 908 is the absolute minimum height that the Library tab can fit into
        maxContentHeight -= (SKIF_vecAlteredSize.y / SKIF_ImGui_GlobalDPIScale);

  SKIF_ImGui_BeginChildFrame (
    frame_content_area_id,
      ImVec2 (   0.0f,
               maxContentHeight * SKIF_ImGui_GlobalDPIScale ), // 900.0f
        ImGuiWindowFlags_NavFlattened
  );
}

bool SKIF_ImGui_IconButton (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon)
{
  bool ret   = false;
  icon       = " " + icon;
  label      = label + " ";

  ImGui::BeginChildFrame (id, ImVec2 (ImGui::CalcTextSize(icon.c_str())  .x +
                                      ImGui::CalcTextSize(label.c_str()) .x +
                                      ImGui::CalcTextSize("    ").x,
                                      ImGui::GetTextLineHeightWithSpacing() + 2.0f * SKIF_ImGui_GlobalDPIScale),
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NavFlattened
  );

  ImVec2 iconPos = ImGui::GetCursorPos ( );
  ImGui::ItemSize      (ImVec2 (ImGui::CalcTextSize (icon.c_str()) .x, ImGui::GetTextLineHeightWithSpacing()));
  ImGui::SameLine      ( );
  ImGui::Selectable    (label.c_str(), &ret,  ImGuiSelectableFlags_SpanAllColumns | static_cast<int>(ImGuiSelectableFlags_SpanAvailWidth));
  ImGui::SetCursorPos  (iconPos);
  ImGui::TextColored   (colIcon, icon.c_str());

  ImGui::EndChildFrame ( );

  return ret;
}

void SKIF_ImGui_ServiceMenu (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject   = SKIF_InjectionContext::GetInstance ( );

  if (ServiceMenu == PopupState_Open)
  {
    ImGui::OpenPopup ("ServiceMenu");
    ServiceMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup ("ServiceMenu", ImGuiWindowFlags_NoMove))
  {
    ImGui::TextColored (
      ImColor::HSV (0.11F, 1.F, 1.F),
        "Troubleshooting:"
    );

    ImGui::Separator ( );

    if (ImGui::Selectable("Force start service"))
      _inject._StartStopInject (false, _registry.bStopOnInjection);

    if (ImGui::Selectable("Force stop service"))
      _inject._StartStopInject (true);

    ImGui::EndPopup ( );
  }
}


// Fonts

void
SKIF_ImGui_MissingGlyphCallback (wchar_t c)
{
  static UINT acp = GetACP();

  static std::unordered_set <wchar_t>
      unprintable_chars;
  if (unprintable_chars.emplace (c).second)
  {
    using range_def_s =
      std::pair <const ImWchar*, bool *>;

    static       auto pFonts = ImGui::GetIO ().Fonts;

    static const auto ranges =
      { // Sorted from least numer of unique characters to the most
        range_def_s { pFonts->GetGlyphRangesVietnamese              (), &SKIF_bFontVietnamese        },
        range_def_s { pFonts->GetGlyphRangesCyrillic                (), &SKIF_bFontCyrillic          },
        range_def_s { pFonts->GetGlyphRangesThai                    (), &SKIF_bFontThai              },
      ((acp == 932) // Prioritize Japanese for ACP 932
      ? range_def_s { pFonts->GetGlyphRangesJapanese                (), &SKIF_bFontJapanese          }
      : range_def_s { pFonts->GetGlyphRangesChineseSimplifiedCommon (), &SKIF_bFontChineseSimplified }),
      ((acp == 932)
      ? range_def_s { pFonts->GetGlyphRangesChineseSimplifiedCommon (), &SKIF_bFontChineseSimplified }
      : range_def_s { pFonts->GetGlyphRangesJapanese                (), &SKIF_bFontJapanese          }),
        range_def_s { pFonts->GetGlyphRangesKorean                  (), &SKIF_bFontKorean            }
#ifdef _WIN64
      // 32-bit SKIF breaks if too many character sets are
      //   loaded so omit Chinese Full on those versions.
      , range_def_s { pFonts->GetGlyphRangesChineseFull             (), &SKIF_bFontChineseAll        }
#endif
      };

    for ( const auto &[span, enable] : ranges)
    {
      ImWchar const *sp =
        &span [2];

      while (*sp != 0x0)
      {
        if ( c <= (wchar_t)(*sp++) &&
             c >= (wchar_t)(*sp++) )
        {
           sp             = nullptr;
          *enable         = true;

          extern bool invalidateFonts;
          invalidateFonts = true;

          break;
        }
      }

      if (sp == nullptr)
        break;
    }
  }
}

const ImWchar*
SK_ImGui_GetGlyphRangesDefaultEx (void)
{
  static const ImWchar ranges [] =
  {
    0x0020,  0x00FF, // Basic Latin + Latin Supplement
    0x0100,  0x03FF, // Latin, IPA, Greek
    0x2000,  0x206F, // General Punctuation
    0x2100,  0x21FF, // Letterlike Symbols
    0x2600,  0x26FF, // Misc. Characters
    0x2700,  0x27BF, // Dingbats
    0x207f,  0x2090, // N/A (literally, the symbols for N/A :P)
    0xc2b1,  0xc2b3, // ²
    0
  };
  return &ranges [0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesKorean (void)
{
  static const ImWchar ranges[] =
  {
      0x0020, 0x00FF, // Basic Latin + Latin Supplement
      0x3131, 0x3163, // Korean alphabets
//#ifdef _WIN64
      0xAC00, 0xD7A3, // Korean characters (Hangul syllables) -- should not be included on 32-bit OSes due to system limitations
//#endif
      0,
  };
  return &ranges[0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesFontAwesome (void)
{
  static const ImWchar ranges [] =
  {
    ICON_MIN_FA,  ICON_MAX_FA,  // Font Awesome (Solid / Regular)
    0
  };
  return &ranges [0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesFontAwesomeBrands (void)
{
  static const ImWchar ranges [] =
  {
    ICON_MIN_FAB, ICON_MAX_FAB, // Font Awesome (Brands)
    0
  };
  return &ranges [0];
}

ImFont*
SKIF_ImGui_LoadFont ( const std::wstring& filename, float point_size, const ImWchar* glyph_range, ImFontConfig* cfg )
{
  auto& io =
    ImGui::GetIO ();

  wchar_t wszFullPath [MAX_PATH + 2] = { };

  if (GetFileAttributesW (              filename.c_str ()) != INVALID_FILE_ATTRIBUTES)
     wcsncpy_s ( wszFullPath, MAX_PATH, filename.c_str (),
                             _TRUNCATE );

  else
  {
    wchar_t     wszFontsDir [MAX_PATH] = { };
    wcsncpy_s ( wszFontsDir, MAX_PATH,
             SK_GetFontsDir ().c_str (), _TRUNCATE );

    PathCombineW ( wszFullPath,
                   wszFontsDir, filename.c_str () );

    if (GetFileAttributesW (wszFullPath) == INVALID_FILE_ATTRIBUTES)
      *wszFullPath = L'\0';
  }

  if (*wszFullPath != L'\0')
  {
    return
      io.Fonts->AddFontFromFileTTF ( SK_WideCharToUTF8 (wszFullPath).c_str (),
                                       point_size,
                                         cfg,
                                           glyph_range );
  }

  return (ImFont *)nullptr;
}

void
SKIF_ImGui_InitFonts (float fontSize, bool extendedCharsets)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  static UINT acp = GetACP();

  auto& io =
    ImGui::GetIO ();

  extern ImGuiContext *GImGui;

  if (io.Fonts != nullptr)
  {
    if (GImGui->FontAtlasOwnedByContext)
    {
      if (GImGui->Font != nullptr)
      {
        GImGui->Font->ClearOutputData ();

        if (GImGui->Font->ContainerAtlas != nullptr)
            GImGui->Font->ContainerAtlas->Clear ();
      }

      io.FontDefault = nullptr;

      IM_DELETE (io.Fonts);
                 io.Fonts = IM_NEW (ImFontAtlas)();
    }
  }

  ImFontConfig
  font_cfg           = {  };
  
  std::filesystem::path fontDir
          (_path_cache.specialk_userdata);

  fontDir /= L"Fonts";

  bool useDefaultFont = false;
  if  (useDefaultFont)
  {
    font_cfg.SizePixels = 13.0f; // Size of the default font (default: 13.0f)
    io.Fonts->AddFontDefault (&font_cfg); // ProggyClean.ttf
    font_cfg.MergeMode = true;
  }

  std::wstring standardFont = (fontSize >= 18.0F) ? L"Tahoma.ttf" : L"Verdana.ttf"; // L"Tahoma.ttf" : L"Verdana.ttf";

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            fontDir, ec))
        std::filesystem::create_directories (fontDir, ec);

  // Core character set
  SKIF_ImGui_LoadFont     (standardFont, fontSize, SK_ImGui_GetGlyphRangesDefaultEx(), &font_cfg);
  //SKIF_ImGui_LoadFont     ((fontDir / L"NotoSans-Regular.ttf"), fontSize, SK_ImGui_GetGlyphRangesDefaultEx());

  if (! useDefaultFont)
  {
    font_cfg.MergeMode = true;
  }

  // Load extended character sets when SKIF is not used as a launcher
  if (extendedCharsets)
  {
    // Cyrillic character set
    if (SKIF_bFontCyrillic)
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, io.Fonts->GetGlyphRangesCyrillic                 (), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSans-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesCyrillic        (), &font_cfg);
  
    // Japanese character set
    // Load before Chinese for ACP 932 so that the Japanese font is not overwritten
    if (SKIF_bFontJapanese && acp == 932)
    {
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesJapanese        (), &font_cfg);
      ///*
      if (SKIF_Util_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      //*/
    }

    // Simplified Chinese character set
    // Also includes almost all of the Japanese characters except for some Kanjis
    if (SKIF_bFontChineseSimplified)
      SKIF_ImGui_LoadFont   (L"msyh.ttc",     fontSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon (), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansSC-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon        (), &font_cfg);

    // Japanese character set
    // Load after Chinese for the rest of ACP's so that the Chinese font is not overwritten
    if (SKIF_bFontJapanese && acp != 932)
    {
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesJapanese        (), &font_cfg);
      ///*
      if (SKIF_Util_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      //*/
    }
    
    // All Chinese character sets
    if (SKIF_bFontChineseAll)
      SKIF_ImGui_LoadFont   (L"msjh.ttc",     fontSize, io.Fonts->GetGlyphRangesChineseFull             (), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansTC-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesChineseFull        (), &font_cfg);

    // Korean character set
    // On 32-bit builds this does not include Hangul syllables due to system limitaitons
    if (SKIF_bFontKorean)
      SKIF_ImGui_LoadFont   (L"malgun.ttf",   fontSize, SK_ImGui_GetGlyphRangesKorean                   (), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansKR-Regular.ttf"), fontSize, io.Fonts->SK_ImGui_GetGlyphRangesKorean        (), &font_cfg);

    // Thai character set
    if (SKIF_bFontThai)
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, io.Fonts->GetGlyphRangesThai                    (), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSansThai-Regular.ttf"),   fontSize, io.Fonts->GetGlyphRangesThai      (), &font_cfg);

    // Vietnamese character set
    if (SKIF_bFontVietnamese)
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, io.Fonts->GetGlyphRangesVietnamese              (), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSans-Regular.ttf"),   fontSize, io.Fonts->GetGlyphRangesVietnamese    (), &font_cfg);
  }

  static auto
    skif_fs_wb = ( std::ios_base::binary
                 | std::ios_base::out  );

  auto _UnpackFontIfNeeded =
  [&]( const char*   szFont,
       const uint8_t akData [],
       const size_t  cbSize )
  {
    if (! std::filesystem::is_regular_file ( fontDir / szFont, ec)        )
                     std::ofstream ( fontDir / szFont, skif_fs_wb ).
      write ( reinterpret_cast <const char *> (akData),
                                               cbSize);
  };

  auto      awesome_fonts = {
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAS, fa_solid_900_ttf,
                   _ARRAYSIZE (fa_solid_900_ttf) ),
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAB, fa_brands_400_ttf,
                   _ARRAYSIZE (fa_brands_400_ttf) )
                            };

  float fontSizeFA       = fontSize - 2.0f;
  float fontSizeConsolas = fontSize - 4.0f;

  for (auto& font : awesome_fonts)
    _UnpackFontIfNeeded ( std::get <0> (font), std::get <1> (font), std::get <2> (font) );

  // FA Regular is basically useless as it only has 163 icons, so we don't bother using it
  // FA Solid has 1390 icons in comparison
  SKIF_ImGui_LoadFont (fontDir/FONT_ICON_FILE_NAME_FAS, fontSizeFA, SK_ImGui_GetGlyphRangesFontAwesome       (), &font_cfg);
  // FA Brands
  SKIF_ImGui_LoadFont (fontDir/FONT_ICON_FILE_NAME_FAB, fontSizeFA, SK_ImGui_GetGlyphRangesFontAwesomeBrands (), &font_cfg);

  //io.Fonts->AddFontDefault ();

  fontConsolas = SKIF_ImGui_LoadFont (L"Consola.ttf", fontSizeConsolas, SK_ImGui_GetGlyphRangesDefaultEx ( ));
  //fontConsolas = SKIF_ImGui_LoadFont ((fontDir / L"NotoSansMono-Regular.ttf"), fontSize/* - 4.0f*/, SK_ImGui_GetGlyphRangesDefaultEx());
}

void
SKIF_ImGui_SetStyle (ImGuiStyle* dst)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );
  
  if (dst == nullptr)
    dst = &ImGui::GetStyle ( );

  // Setup Dear ImGui style
  switch (_registry.iStyle)
  {
  case 3:
    ImGui::StyleColorsClassic   (dst);
    break;
  case 2:
    SKIF_ImGui_StyleColorsLight (dst);
    break;
  case 1:
    ImGui::StyleColorsDark      (dst);
    break;
  case 0:
  default:
    SKIF_ImGui_StyleColorsDark  (dst);
    _registry.iStyle = 0;
  }

  // Override the style with a few tweaks of our own
  dst->WindowRounding  = 4.0F; // style.ScrollbarRounding;
  dst->ChildRounding   = dst->WindowRounding;
  dst->TabRounding     = dst->WindowRounding;
  dst->FrameRounding   = dst->WindowRounding;
  
  if (! _registry.bUIBorders)
  {
    dst->TabBorderSize   = 0.0F;
    dst->FrameBorderSize = 0.0F;

    // Necessary to hide the 1 px separator shown at the bottom of the tabs row
    dst->Colors[ImGuiCol_TabActive] = dst->Colors[ImGuiCol_WindowBg];
  }

  else {
    // Is not scaled by ScaleAllSizes() so we have to do it here
    dst->TabBorderSize   = 1.0F * SKIF_ImGui_GlobalDPIScale;
    dst->FrameBorderSize = 1.0F * SKIF_ImGui_GlobalDPIScale;
  }

  // Scale the style based on the current DPI factor
  dst->ScaleAllSizes (SKIF_ImGui_GlobalDPIScale);
  
  if (_registry._sRGBColors)
    for (int i=0; i < ImGuiCol_COUNT; i++)
        dst->Colors[i] = SKIF_ImGui_sRGBtoLinear (dst->Colors[i]);

  ImGui::GetStyle ( ) = *dst;
}

void
SKIF_ImGui_PushDisableState (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // Push the states in a specific order
  ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
  //ImGui::PushStyleVar (ImGuiStyleVar_Alpha,     ImGui::GetStyle ().Alpha * 0.5f); // [UNUSED]
  ImGui::PushStyleColor (ImGuiCol_Text,           ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
  ImGui::PushStyleColor (ImGuiCol_SliderGrab,     SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (_registry.iStyle == 2) ? 0.75f : 0.25f));
  ImGui::PushStyleColor (ImGuiCol_CheckMark,      SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (_registry.iStyle == 2) ? 0.75f : 0.25f));
  ImGui::PushStyleColor (ImGuiCol_FrameBg,        SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_FrameBg),      (_registry.iStyle == 2) ? 0.75f : 0.15f));
}

void
SKIF_ImGui_PopDisableState (void)
{
  // Pop the states in the reverse order that we pushed them in
  ImGui::PopStyleColor ( ); // ImGuiCol_FrameBg
  ImGui::PopStyleColor ( ); // ImGuiCol_CheckMark
  ImGui::PopStyleColor ( ); // ImGuiCol_SliderGrab
  ImGui::PopStyleColor ( ); // ImGuiCol_Text
  //ImGui::PopStyleVar ( ); // ImGuiStyleVar_Alpha [UNUSED]
  ImGui::PopItemFlag   ( ); // ImGuiItemFlags_Disabled
}

void
SKIF_ImGui_DisallowMouseDragMove (void)
{
  extern bool SKIF_MouseDragMoveAllowed;

  if (ImGui::IsItemActive ( ))
    SKIF_MouseDragMoveAllowed = false;
}

// Allows moving the window but only in certain circumstances
bool
SKIF_ImGui_CanMouseDragMove (void)
{
  extern bool SKIF_MouseDragMoveAllowed;
  return      SKIF_MouseDragMoveAllowed   &&       // Manually disabled by a few UI elements
            ! ImGui::IsAnyItemHovered ( ) &&       // Disabled if any item is hovered
          ( ! ImGui::IsAnyPopupOpen ( )         || // Disabled if any popup is opened..
          (   AddGamePopup == PopupState_Opened || // ..except for a few standard ones
              ConfirmPopup == PopupState_Opened || //   which are actually aligned to
           ModifyGamePopup == PopupState_Opened || //   the center of the app window
           RemoveGamePopup == PopupState_Opened ||
         UpdatePromptPopup == PopupState_Opened ||
              HistoryPopup == PopupState_Opened ||
           AutoUpdatePopup == PopupState_Opened ));
}

void
SKIF_ImGui_InvalidateFonts (void)
{
  extern float SKIF_ImGui_FontSizeDefault;

  float fontScale = SKIF_ImGui_FontSizeDefault * SKIF_ImGui_GlobalDPIScale;

  SKIF_ImGui_InitFonts (fontScale); // SKIF_FONTSIZE_DEFAULT);
  ImGui::GetIO ().Fonts->Build ();
  ImGui_ImplDX11_InvalidateDeviceObjects ( );
}

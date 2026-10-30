#include <utility/skif_imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <map>

#include <utility/sk_utility.h>
#include <utility/utility.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <fonts/fa_solid_900.ttf.h>
#include <fonts/fa_brands_400.ttf.h>
#include <imgui/imgui_impl_dx11.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

bool                  bAutoScrollActive;
bool                  bScrollbarX;
bool                  bScrollbarY;
ImFont*               fontConsolas = nullptr;
float                 fPosXLMEnd   = 0.0f;

// Extended character sets, loaded on demand (see SKIF_ImGui_MissingGlyphCallback)
enum SKIF_ImGui_ExtFont {
  SKIF_ImGui_ExtFont_Japanese,
  SKIF_ImGui_ExtFont_ChineseSimplified,
  SKIF_ImGui_ExtFont_ChineseFull,
  SKIF_ImGui_ExtFont_Korean,
  SKIF_ImGui_ExtFont_COUNT
};
static bool           SKIF_ImGui_ExtFontWanted [SKIF_ImGui_ExtFont_COUNT] = { };

// The Unicode blocks of the extended character sets (pairs of inclusive ranges): used both to pick the font to add for a
//   missing character, and to keep each regional font to its own script (ImFontConfig::GlyphExcludeRanges)
#define SKIF_IMGUI_GLYPHS_HANGUL                                                                          \
  0x1100, 0x11FF, /* Hangul Jamo                                                                     */  \
  0x3130, 0x318F, /* Hangul Compatibility Jamo                                                       */  \
  0xA960, 0xA97F, /* Hangul Jamo Extended-A                                                          */  \
  0xAC00, 0xD7FF, /* Hangul Syllables, Hangul Jamo Extended-B                                        */  \
  0xFFA0, 0xFFDC  /* Halfwidth Hangul                                                                */
#define SKIF_IMGUI_GLYPHS_CJK                                                                             \
  0x2E80, 0x2FDF, /* CJK Radicals Supplement, Kangxi Radicals                                        */  \
  0x3000, 0x30FF, /* CJK Symbols and Punctuation, Hiragana, Katakana                                 */  \
  0x3190, 0x31FF, /* Kanbun, Bopomofo Extended, CJK Strokes, Katakana Phonetic Extensions            */  \
  0x3200, 0x33FF, /* Enclosed CJK Letters and Months, CJK Compatibility                              */  \
  0x3400, 0x4DBF, /* CJK Unified Ideographs Extension A                                              */  \
  0x4E00, 0x9FFF, /* CJK Unified Ideographs                                                          */  \
  0xF900, 0xFAFF, /* CJK Compatibility Ideographs                                                    */  \
  0xFE30, 0xFE4F, /* CJK Compatibility Forms                                                         */  \
  0xFF00, 0xFF9F, /* Fullwidth Forms, Halfwidth Katakana (Halfwidth Hangul follows)                  */  \
  0xFFE0, 0xFFEF  /* Fullwidth Signs                                                                 */

static const ImWchar  SKIF_ImGui_GlyphsHangul [] = { SKIF_IMGUI_GLYPHS_HANGUL, 0 };
static const ImWchar  SKIF_ImGui_GlyphsCJK    [] = { SKIF_IMGUI_GLYPHS_CJK,    0 };

// Han characters (shared by Chinese and Japanese) are drawn differently in Japanese, Simplified and Traditional Chinese,
//   so the font that provides them first should match the language of the UI (SKIF_ImGui_SetUILanguage), or else the
//     system's ANSI code page (Japanese first for ACP 932, Simplified Chinese otherwise)
enum SKIF_ImGui_HanPreference {
  SKIF_ImGui_Han_Default,
  SKIF_ImGui_Han_Japanese,
  SKIF_ImGui_Han_ChineseSimplified,
  SKIF_ImGui_Han_ChineseTraditional
};
static SKIF_ImGui_HanPreference SKIF_ImGui_HanPreferred = SKIF_ImGui_Han_Default;

// The fonts to try for Han characters, in order (also the order they are added in)
static std::vector <SKIF_ImGui_ExtFont>
SKIF_ImGui_GetHanFontOrder (void)
{
  static UINT acp = GetACP();

  switch (SKIF_ImGui_HanPreferred)
  {
    case SKIF_ImGui_Han_Japanese:           return { SKIF_ImGui_ExtFont_Japanese,          SKIF_ImGui_ExtFont_ChineseSimplified, SKIF_ImGui_ExtFont_ChineseFull };
    case SKIF_ImGui_Han_ChineseSimplified:  return { SKIF_ImGui_ExtFont_ChineseSimplified, SKIF_ImGui_ExtFont_Japanese,          SKIF_ImGui_ExtFont_ChineseFull };
    case SKIF_ImGui_Han_ChineseTraditional: return { SKIF_ImGui_ExtFont_ChineseFull,       SKIF_ImGui_ExtFont_ChineseSimplified, SKIF_ImGui_ExtFont_Japanese    };
    default:
      break;
  }

  // Japanese takes priority for ACP 932, and the Simplified Chinese font (which also has almost all of the Japanese characters) otherwise
  if (acp == 932)
    return { SKIF_ImGui_ExtFont_Japanese,          SKIF_ImGui_ExtFont_ChineseSimplified, SKIF_ImGui_ExtFont_ChineseFull };
  else
    return { SKIF_ImGui_ExtFont_ChineseSimplified, SKIF_ImGui_ExtFont_Japanese,          SKIF_ImGui_ExtFont_ChineseFull };
}

static bool
SKIF_ImGui_IsInGlyphRanges (const ImWchar* ranges, unsigned int cp)
{
  for (; ranges [0] != 0; ranges += 2)
    if (cp >= ranges [0] && cp <= ranges [1])
      return true;
  return false;
}

PopupState PopupMessageInfo = PopupState_Closed;
std::vector <std::pair <std::string, std::string> > vInfoMessages;

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
  colors[ImGuiCol_Text]                   = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
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
  colors[ImGuiCol_CheckboxSelectedBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); // Disable

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
  colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.68f, 0.68f, 0.68f, 1.00f);
  colors[ImGuiCol_SeparatorActive]        = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

  // Resize Grip
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.13f, 0.13f, 0.13f, 1.00f); // 0.26f, 0.59f, 0.98f, 0.25f
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.17f, 0.17f, 0.17f, 1.00f); // 0.26f, 0.59f, 0.98f, 0.67f
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f); // 0.26f, 0.59f, 0.98f, 0.95f

  // Tabs
  colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);       //ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.80f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
  colors[ImGuiCol_TabSelected]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);       //ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
  colors[ImGuiCol_TabDimmed]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

  // Docking stuff
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Plot
  colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

  // Tables
  colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
  colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);

  // Drag-n-drop
  colors[ImGuiCol_DragDropTarget]         = ImVec4(0.90f, 0.90f, 0.10f, 1.00f);

  // Nav/Modal
  // This is the white border that highlights selected items when using keyboard focus etc
  colors[ImGuiCol_NavCursor]           = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
//colors[ImGuiCol_NavCursor]           = ImVec4(0.48f, 0.48f, 0.48f, 1.00f);
//colors[ImGuiCol_NavCursor]           = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // ImVec4(0.80f, 0.80f, 0.80f, 0.20f)
  colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

  // Custom
  colors[ImGuiCol_SKIF_TextBase]          = ImVec4(0.68f, 0.68f, 0.68f, 1.00f);
  colors[ImGuiCol_SKIF_TextCaption]       = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_SKIF_TextGameTitle]     = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_SKIF_Success]           = ImColor(121, 214, 28);  // 42,  203, 2);  //53,  255, 3);  //ImColor(144, 238, 144);
  colors[ImGuiCol_SKIF_Warning]           = ImColor(255, 124, 3); // ImColor::HSV(0.11F, 1.F, 1.F)
  colors[ImGuiCol_SKIF_Failure]           = ImColor(186, 59, 61, 255);
  colors[ImGuiCol_SKIF_Info]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // colors[ImGuiCol_CheckMark];
  colors[ImGuiCol_SKIF_Yellow]            = ImColor::HSV(0.11F, 1.F, 1.F);
  colors[ImGuiCol_SKIF_Icon]              = colors[ImGuiCol_Text];
  colors[ImGuiCol_SKIF_HighlightLineBg]   = ImVec4(1.00f, 1.00f, 1.00f, 0.05f);
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
  colors[ImGuiCol_CheckboxSelectedBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f); // Disable

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
  colors[ImGuiCol_ResizeGrip]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f); // 0.80f, 0.80f, 0.80f, 0.56f
  colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.70f, 0.70f, 0.70f, 1.00f); // 0.26f, 0.59f, 0.98f, 0.67f
  colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.60f, 0.60f, 0.60f, 1.00f); // 0.26f, 0.59f, 0.98f, 0.95f

  // Tabs
  colors[ImGuiCol_Tab]                    = ImVec4(0.85f, 0.85f, 0.85f, 1.00f); // ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.90f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.90f, 0.90f, 0.90f, 1.00f); // colors[ImGuiCol_HeaderHovered];
  colors[ImGuiCol_TabSelected]              = ImVec4(0.80f, 0.80f, 0.80f, 1.00f); // ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
  colors[ImGuiCol_TabDimmed]           = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);

  // Docking stuff
  colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_Header] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

  // Plot
  colors[ImGuiCol_PlotLines]              = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
  colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
  colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.45f, 0.00f, 1.00f);

  // Tables
  colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.78f, 0.87f, 0.98f, 1.00f);
  colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.57f, 0.57f, 0.64f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableBorderLight]       = ImVec4(0.68f, 0.68f, 0.74f, 1.00f);   // Prefer using Alpha=1.0 here
  colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.30f, 0.30f, 0.30f, 0.09f);

  // Drag-n-drop
  colors[ImGuiCol_DragDropTarget]         = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

  // Nav/Modal
  colors[ImGuiCol_NavCursor]           = colors[ImGuiCol_HeaderHovered];
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
  colors[ImGuiCol_SKIF_Icon]              = colors[ImGuiCol_Text];
  colors[ImGuiCol_SKIF_HighlightLineBg]   = ImVec4(0.00f, 0.00f, 0.00f, 0.05f);
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
  extern float  SKIF_fStatusBarHeight;
  extern float  SKIF_fStatusBarDisabled;
  extern float  SKIF_fStatusBarHeightTips;

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

    ImVec2 tmpCurrentSize  = (_registry.bMiniMode)    ? SKIF_vecServiceModeDefault  :
                             (_registry.bHorizonMode) ? SKIF_vecHorizonModeAdjusted :
                                                        SKIF_vecRegularModeAdjusted ;

    //if (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
    //  SKIF_vecAlteredSize.y = (tmpCurrentSize.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y));
  }
}

void
SKIF_ImGui_InfoMessage (const std::string szTitle, const std::string szLabel)
{
  vInfoMessages.emplace_back ( szTitle, szLabel );
}

// Internal; processes any existing info messages
void
SKIF_ImGui_InfoMessage_Process (void)
{
  if (! vInfoMessages.empty())
  {
    if (PopupMessageInfo == PopupState_Closed)
      PopupMessageInfo = PopupState_Open;

    static float fPopupWidth;
    fPopupWidth = ImGui::CalcTextSize (vInfoMessages.back().second.c_str()).x + ImGui::GetStyle().IndentSpacing * 3.0f;

    if (SKIF_ImGui_BeginPopup ((vInfoMessages.back().first + "###PopupMessageInfo").c_str(), nullptr, ImVec2(fPopupWidth, 0.0f)))
    {
      if (PopupMessageInfo == PopupState_Open)
      {
        ImGuiWindow* window = ImGui::FindWindowByName ("###PopupMessageInfo");
        if (window != nullptr && ! window->Appearing)
          PopupMessageInfo = PopupState_Opened;
      }

      ImGui::TreePush    ("PopupMessageInfoTreePush");

      SKIF_ImGui_Spacing ( );

      ImGui::Text        ("%s", vInfoMessages.back().second.c_str()); // Never as a format: the text may be translated

      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );

      ImVec2 vButtonSize = ImVec2(std::max (80.0f * SKIF_ImGui_GlobalDPIScale, ImGui::CalcTextSize (L_("OK"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f), 0.0f);

      ImGui::SetCursorPosX (fPopupWidth / 2 - vButtonSize.x / 2);

      if (ImGui::Button  (L_("OK"), vButtonSize))
      {
        vInfoMessages.pop_back();

        if (vInfoMessages.empty())
          PopupMessageInfo = PopupState_Closed;

        ImGui::CloseCurrentPopup ( );
      }

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );

      SKIF_ImGui_Spacing ( );

      ImGui::TreePop     ( );
    }

    SKIF_ImGui_EndPopup  ( );
  }
}

void
SKIF_ImGui_CloseInfoPopup (void)
{
  if (PopupMessageInfo != PopupState_Closed)
  {
    vInfoMessages.clear();
    PopupMessageInfo = PopupState_Closed;
  }
}

// Whether any of SKIF's windows has the keyboard focus
//   Thread-safe: the state is kept in an atomic updated by the main thread on WM_SETFOCUS / WM_KILLFOCUS
bool
SKIF_ImGui_IsFocused (void)
{
  extern bool
         SKIF_ImGui_ImplWin32_IsFocused (void);
  return SKIF_ImGui_ImplWin32_IsFocused ( );
}

// The SKIF window (main window, Local Manager, INI Editor, ...) that has the keyboard focus, or NULL
//   Thread-safe: kept in an atomic updated by the main thread on WM_SETFOCUS / WM_KILLFOCUS
HWND
SKIF_ImGui_GetFocusedWindow (void)
{
  extern HWND
         SKIF_ImGui_ImplWin32_GetFocusedWindow (void);
  return SKIF_ImGui_ImplWin32_GetFocusedWindow ( );
}

// Whether the given viewport has the keyboard focus
//   Main thread only (the viewport data is owned and deleted by the main thread)
bool
SKIF_ImGui_IsViewportFocused (const ImGuiViewport* vp)
{
  extern bool
         SKIF_ImGui_ImplWin32_IsFocused (const ImGuiViewport* viewport);
  return SKIF_ImGui_ImplWin32_IsFocused (vp);
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
  for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key = (ImGuiKey)(key + 1))
    if (ImGui::IsKeyDown(key))
      return true;

  if (bAutoScrollActive)
    return true;

  return false;
}

bool
SKIF_ImGui_IsAnyPopupOpen (void)
{
  return ImGui::IsPopupOpen ("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
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
SKIF_ImGui_SetHoverTip (const std::string_view& szText, bool ignoreDisabledTooltips, bool allowWhenItemDisabled)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern bool        HoverTipActive;        // Used to track if an item is being hovered
  extern DWORD       HoverTipDuration;      // Used to track how long the item has been hovered (to delay showing tooltips)
  extern std::string SKIF_StatusBarText;

  if (ImGui::IsItemHovered (allowWhenItemDisabled ? ImGuiHoveredFlags_AllowWhenDisabled : ImGuiHoveredFlags_None) && ! szText.empty())
  {
    if (_registry.bUITooltips || ignoreDisabledTooltips)
    {
      ImGui::PushStyleColor (ImGuiCol_PopupBg,  ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
      ImGui::PushStyleColor (ImGuiCol_Text,     ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
      HoverTipActive = true;

      if (HoverTipDuration == 0)
      {
        HoverTipDuration = SKIF_Util_timeGetTime ( );

        // Use a timer to force SKIF to refresh once the duration has passed
        SetTimer (SKIF_Notify_hWnd,
            IDT_REFRESH_TOOLTIP,
            550,
            (TIMERPROC) NULL
        );
      }

      else if (HoverTipDuration + 500 < SKIF_Util_timeGetTime ( ))
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
        _("Info: ");

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
        (                       ! _registry.bMiniMode         )
     )
  {
    extern ImVec2 SKIF_vecCurrentMode;

    // Use a cache so we don't reevaluate the input on every frame.
    struct {
      std::string szInput;
      std::string szOutput;
      float       availWidth = 0.0f;
    } static _cache;

    float availWidth = (SKIF_vecCurrentMode.x - fPosXLMEnd); // -150px due to the Add Game option

    if (_cache.availWidth != availWidth || _cache.szInput != szText)
    {
      _cache.availWidth = availWidth;
      _cache.szInput    = szText;
      _cache.szOutput   = szText;

      // If the text is wider than the app window is...
      if (ImGui::CalcTextSize (szText.data()).x > availWidth)
      {
        // ... use a hover tooltip if enabled and possible
        // if (_registry.bUITooltips)
        //   SKIF_ImGui_SetHoverTip (szText);

        // ... crop the text
        std::string szTrimmed;
        size_t end = szText.length();
        while (end > 3)
        {
          szTrimmed = szText.substr (0, end);
          if (ImGui::CalcTextSize (szTrimmed.c_str()).x <= availWidth)
          {
            szTrimmed.replace (end - 3, end, "...");
            break;
          }
          end--;
        }

        _cache.szOutput = szTrimmed;
      }
    }

    SKIF_StatusBarHelp.assign (_cache.szOutput);
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

// ImGui::TextColored ( ) that wraps like ImGui::TextWrapped ( ): for captions and other single lines of text that may be
//   much longer once translated. The text is not a format string. Can follow a SameLine ( ) (e.g. after an icon).
void
SKIF_ImGui_TextColoredWrapped (const ImVec4& col, const char* text)
{
  ImGui::PushStyleColor (ImGuiCol_Text, col);
  ImGui::TextWrapped    ("%s", text);
  ImGui::PopStyleColor  ( );
}

// Width of a combo (its frame, arrow button and the text) that fits the widest of the items, and is at least min_width wide
float
SKIF_ImGui_ComboWidth (std::initializer_list <const char*> items, float min_width)
{
  float text_width = 0.0f;

  for (const char* item : items)
    if (item != nullptr)
      text_width = std::max (text_width, ImGui::CalcTextSize (item, NULL, true).x);

  return std::max (min_width, text_width + ImGui::GetStyle ( ).FramePadding.x * 2.0f + ImGui::GetFrameHeight ( ));
}

// One line of text with values drawn in their own color where the (translated) text has {0}, {1}, ... (up to {9}), so the
//   sentence stays whole for translators: SKIF_ImGui_TextHighlighted (_("Do you want to end {0}?"), { filename }, colHighlight).
//     "{{" and "}}" are literal braces; placeholders without a value are left out.
void
SKIF_ImGui_TextHighlighted (const char* text, std::initializer_list <const char*> values, const ImVec4& colValue)
{
  std::string_view view ((text != nullptr) ? text : "");

  ImGui::BeginGroup ( );

  bool        first = true;
  std::string plain;

  auto _Draw = [&](const char* str, bool highlighted)
  {
    if (str == nullptr || *str == '\0')
      return;

    if (! first)
      ImGui::SameLine (0.0f, 0.0f);
    first = false;

    if (highlighted)
      ImGui::PushStyleColor (ImGuiCol_Text, colValue);

    ImGui::TextUnformatted (str);

    if (highlighted)
      ImGui::PopStyleColor ( );
  };

  for (size_t i = 0; i < view.size ( ); i++)
  {
    const char ch = view [i];

    // Escaped braces
    if ((ch == '{' || ch == '}') && i + 1 < view.size ( ) && view [i + 1] == ch)
    {
      plain += ch;
      i++;
    }

    // {0} - {9}
    else if (ch == '{' && i + 2 < view.size ( ) && view [i + 1] >= '0' && view [i + 1] <= '9' && view [i + 2] == '}')
    {
      const size_t index = static_cast <size_t> (view [i + 1] - '0');

      _Draw (plain.c_str ( ), false);
      plain.clear ( );

      if (index < values.size ( ))
        _Draw (values.begin ( ) [index], true);

      i += 2;
    }

    else
      plain += ch;
  }

  _Draw (plain.c_str ( ), false);

  // Nothing at all: keep the line
  if (first)
    ImGui::NewLine ( );

  ImGui::EndGroup ( );
}

void
SKIF_ImGui_TextHighlighted (const char* text, const char* value, const ImVec4& colValue)
{
  SKIF_ImGui_TextHighlighted (text, { value }, colValue);
}

// Width of a checkbox or radio button (the box/circle and its label, as ImGui lays them out)
float
SKIF_ImGui_CheckboxWidth (const char* label)
{
  const float label_width = ImGui::CalcTextSize (label, NULL, true).x;

  return ImGui::GetFrameHeight ( ) + ((label_width > 0.0f) ? ImGui::GetStyle ( ).ItemInnerSpacing.x + label_width : 0.0f);
}

// For rows of items (e.g. radio buttons) that may not fit once translated: continues the row with SameLine ( ) if an item of
//   the given width fits the rest of the line, and starts a new line (at the current indentation) otherwise. Returns true if
//     the item is on the same line.
bool
SKIF_ImGui_SameLineIfFits (float width)
{
  ImGui::SameLine ( );

  if (ImGui::GetContentRegionAvail ( ).x >= width)
    return true;

  ImGui::NewLine ( );
  return false;
}

// The label of a checkbox or radio button wider than the space left: the box, followed by the label as wrapped text that
//   can also be clicked (ImGui does not wrap the labels of widgets). Returns true if the label was clicked.
static bool
SKIF_ImGui_WrappedLabel (const char* label)
{
  const char* label_end = ImGui::FindRenderedTextEnd (label);

  ImGui::SameLine    (0.0f, ImGui::GetStyle ( ).ItemInnerSpacing.x);
  ImGui::TextWrapped ("%.*s", static_cast <int> (label_end - label), label);

  // Disabled items are not hovered, so neither is the label of a disabled checkbox clicked
  return ImGui::IsItemClicked ( );
}

// ImGui::Checkbox ( ) whose label wraps when it is wider than the space left (e.g. once translated)
bool
SKIF_ImGui_Checkbox (const char* label, bool* v)
{
  if (SKIF_ImGui_CheckboxWidth (label) <= ImGui::GetContentRegionAvail ( ).x)
    return ImGui::Checkbox (label, v);

  ImGui::PushID      (label);
  ImGui::BeginGroup  ( );
  bool changed = ImGui::Checkbox ("##SKIF_Checkbox", v);

  if (SKIF_ImGui_WrappedLabel (label))
  {
    *v      = ! *v;
    changed = true;
  }

  ImGui::EndGroup    ( );
  ImGui::PopID       ( );

  return changed;
}

// ImGui::RadioButton ( ) whose label wraps when it is wider than the space left (e.g. once translated)
bool
SKIF_ImGui_RadioButton (const char* label, int* v, int v_button)
{
  if (SKIF_ImGui_CheckboxWidth (label) <= ImGui::GetContentRegionAvail ( ).x)
    return ImGui::RadioButton (label, v, v_button);

  ImGui::PushID      (label);
  ImGui::BeginGroup  ( );
  bool pressed = ImGui::RadioButton ("##SKIF_RadioButton", *v == v_button);

  if (SKIF_ImGui_WrappedLabel (label))
    pressed = true;

  ImGui::EndGroup    ( );
  ImGui::PopID       ( );

  if (pressed)
    *v = v_button;

  return pressed;
}

// For items with a label on the right (e.g. combos): narrows the next item, if needed, so the label fits the space left
void
SKIF_ImGui_SetNextItemWidthForLabel (const char* label)
{
  const float label_width = ImGui::CalcTextSize (label, NULL, true).x;
  const float available   = ImGui::GetContentRegionAvail ( ).x - ((label_width > 0.0f) ? ImGui::GetStyle ( ).ItemInnerSpacing.x + label_width : 0.0f);

  ImGui::SetNextItemWidth (std::max (ImGui::GetFrameHeight ( ) * 4.0f, std::min (ImGui::CalcItemWidth ( ), available)));
}

// Wrapped text with links, so sentences stay whole for translation while words within them can be clicked:
//   "Just hop over to the [Library] and launch a game!"
//
// Each [...] is a link, numbered from 0 in order of appearance, and drawn as a selectable (so it can also be reached using
//   the keyboard or a gamepad) in colLink. link_icons optionally has an icon (or nullptr) for each link, drawn in front of
//     it, which keeps the icons out of the translated text. "[[" and "]]" are literal brackets. "\n" starts a new line.
//
// Lines wrap at the right edge of the content region, or at the position set with ImGui::PushTextWrapPos ( ) (e.g. to
//   leave room for an icon after the text). Like ImGui::TextWrapped ( ), the lines after the first start at
//   the position the text started at (so it can follow e.g. a SameLine ( ) after a bullet or a number). The last line can
//     be followed by SameLine ( ). Returns the index of the link that was clicked, or -1.
//
// SKIF_ImGui_TextRichHighlight ( ) is the same, but the [...] parts are only highlighted (drawn in colLink), not clickable.
//   Text put in a [...] from elsewhere (e.g. a key binding) should have its brackets doubled: SKIF_ImGui_TextRichEscape ( ).
static int
SKIF_ImGui_TextRichImpl (const char* text, std::initializer_list <const char*> link_icons, const ImVec4& colLink, bool clickable)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow ( );
  if (window->SkipItems || text == nullptr)
    return -1;

  struct Segment {
    std::string text;
    int         link = -1;
  };

  // Split the text into plain text and links
  std::vector <Segment> segments (1);
  int                   links    = 0;

  for (const char* p = text; *p != '\0'; )
  {
    if ((p [0] == '[' && p [1] == '[') ||
        (p [0] == ']' && p [1] == ']'))
    {
      segments.back ( ).text += *p;
      p += 2;
    }

    else if (p [0] == '[' && segments.back ( ).link < 0 && strchr (p + 1, ']') != nullptr)
    {
      segments.push_back ({ "", links });

      // A no-break space keeps the icon on the same line as the text of the link
      if (static_cast <size_t> (links) < link_icons.size ( ) && link_icons.begin ( ) [links] != nullptr)
        segments.back ( ).text = std::string (link_icons.begin ( ) [links]) + "\xC2\xA0";

      links++;
      p++;
    }

    else if (p [0] == ']' && segments.back ( ).link >= 0)
    {
      segments.push_back ({ });
      p++;
    }

    else
      segments.back ( ).text += *p++;
  }

  // Whether a line may break at pos (when ImGui cut a word that does not fit the rest of the line, it is moved to the next line instead)
  auto _IsBreakOpportunity = [](const char* begin, const char* pos, const char* end) -> bool
  {
    if (pos <= begin || pos >= end)
      return true;

    const char* prev_pos = pos - 1;
    while (prev_pos > begin && (static_cast <unsigned char> (*prev_pos) & 0xC0) == 0x80)
      prev_pos--;

    unsigned int prev = 0, curr = 0;
    ImTextCharFromUtf8 (&prev, prev_pos, end);
    ImTextCharFromUtf8 (&curr, pos,      end);

    return (curr == ' ' || curr == '\t' || prev == ' ' || prev == '\t' ||
           (prev < 0x80 && strchr (".,;:!?\")-/", static_cast <int> (prev)) != nullptr) ||
            prev >= 0x2E80 || curr >= 0x2E80); // CJK: a line may break between characters
  };

  ImFont*     font     = ImGui::GetFont     ( );
  const float size     = ImGui::GetFontSize ( );
  const float start_x  = ImGui::GetCursorPosX ( );
  const float wrap_x   = (window->DC.TextWrapPos > 0.0f) ? window->DC.TextWrapPos                      // PushTextWrapPos ( ), window-local like GetCursorPosX ( )
                                                         : start_x + ImGui::GetContentRegionAvail ( ).x;
  const ImU32 col_link = ImGui::GetColorU32 (colLink);

  bool        has_content = false; // Something has been drawn on the current line
  bool        placed_any  = false; // Something has been drawn at all (the first line starts wherever the cursor is)
  int         clicked     = -1;
  int         piece_id    = 0;

  ImGui::PushID (text);

  for (auto& segment : segments)
  {
    const char* s   = segment.text.c_str ( );
    const char* end = s + segment.text.size ( );

    while (s < end)
    {
      if (*s == '\n')
      {
        if (! has_content)
          ImGui::NewLine ( ); // Empty line
        has_content = false;
        s++;
        continue;
      }

      const char* line_end = static_cast <const char*> (memchr (s, '\n', end - s));
      if (line_end == nullptr)
        line_end = end;

      if (has_content)
        ImGui::SameLine (0.0f, 0.0f);
      else if (placed_any)
        ImGui::SetCursorPosX (start_x);

      const char* e = font->CalcWordWrapPosition (size, s, line_end, wrap_x - ImGui::GetCursorPosX ( ));

      // Does not fit the rest of the line, or only the blanks before a word do: continue on the next one
      if (has_content && (e <= s || ! _IsBreakOpportunity (s, e, line_end) || (e < line_end && strspn (s, " \t") >= static_cast <size_t> (e - s))))
      {
        ImGui::NewLine       ( );
        ImGui::SetCursorPosX (start_x);

        while (s < line_end && (*s == ' ' || *s == '\t'))
          s++;
        if (s >= line_end)
        {
          has_content = false;
          continue;
        }

        e = font->CalcWordWrapPosition (size, s, line_end, wrap_x - start_x);
      }

      if (e <= s)
        e = s + ImTextCountUtf8BytesFromChar (s, line_end);

      if (segment.link < 0)
        ImGui::TextUnformatted (s, e);

      else if (! clickable)
      {
        ImGui::PushStyleColor  (ImGuiCol_Text, colLink);
        ImGui::TextUnformatted (s, e);
        ImGui::PopStyleColor   ( );
      }

      else
      {
        ImVec2 pos = ImGui::GetCursorScreenPos ( );
        pos.y += window->DC.CurrLineTextBaseOffset;

        ImGui::PushID (piece_id++);
        if (ImGui::Selectable ("##SKIF_TextRichLink", false, ImGuiSelectableFlags_None, ImGui::CalcTextSize (s, e)))
          clicked = segment.link;
        ImGui::PopID ( );

        SKIF_ImGui_SetMouseCursorHand ( );
        window->DrawList->AddText (pos, col_link, s, e);
      }

      has_content = true;
      placed_any  = true;

      // Wrapped: the rest goes on the next line, without the blanks it was wrapped at
      if (e < line_end)
      {
        has_content = false;
        while (e < line_end && (*e == ' ' || *e == '\t'))
          e++;
      }

      s = e;
    }
  }

  ImGui::PopID ( );

  return clicked;
}

int
SKIF_ImGui_TextRich (const char* text, std::initializer_list <const char*> link_icons, const ImVec4& colLink)
{
  return SKIF_ImGui_TextRichImpl (text, link_icons, colLink, true);
}

void
SKIF_ImGui_TextRichHighlight (const char* text, std::initializer_list <const char*> icons, const ImVec4& colHighlight)
{
  SKIF_ImGui_TextRichImpl (text, icons, colHighlight, false);
}

// Doubles the brackets of text put in a [...] of SKIF_ImGui_TextRich ( ) / SKIF_ImGui_TextRichHighlight ( ), so they are drawn as is
std::string
SKIF_ImGui_TextRichEscape (const std::string& text)
{
  std::string out;
  out.reserve (text.size ( ));

  for (char ch : text)
  {
    out += ch;
    if (ch == '[' || ch == ']')
      out += ch;
  }

  return out;
}

// Vertical aligned label
// Must be surrounded by ImGui::PushID/PopID
bool
SKIF_ImGui_SelectableVAligned (const char* unique_id, const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg)
{
  ImVec2 cursor_pre    =  ImGui::GetCursorPos ( );
  ImVec2 label_size    =  ImGui::CalcTextSize (label);
  float  label_offset  = (size_arg.y - label_size.y) / 2.0f;
  ImVec2 label_pos     =  ImVec2 (cursor_pre.x + ImGui::GetStyle().ItemSpacing.x, cursor_pre.y + label_offset);

  ImGui::BeginGroup      ( );
  //ImGui::PushID          (unique_id);
  ImGui::PushStyleVar    (ImGuiStyleVar_FrameBorderSize, 0.0f);
  // We use ImGuiSelectableFlags_NoPadWithHalfSpacing to prevent the bounding box from having 0.5 * ItemSpacing padded on top and bottom
  bool   ret           =  ImGui::Selectable (unique_id, p_selected, flags | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_NoPadWithHalfSpacing, size_arg);
  ImVec2 cursor_post   =  ImGui::GetCursorPos ( );
  ImGui::SetCursorPos    (label_pos);
  ImGui::TextUnformatted (label);
  ImGui::PopStyleVar     ( );
  //ImGui::PopID           ( );
  ImGui::EndGroup        ( );

  ImGui::SetCursorPos    (cursor_post);
  return ret;
}

bool
SKIF_ImGui_SeparatorTextClickable (const char* label, const bool* collapsed, bool hand_cursor)
{
  const ImGuiStyle& style = ImGui::GetStyle ();

  // Reserve room for the arrow by prefixing spaces
  //  - the part after "###" is the ID, so it never changes with the arrow state
  char        display [256];
  const char* shown = label;

  if (collapsed != nullptr)
  {
    const float arrow_w = ImGui::GetFontSize ();
    const float gap     = style.ItemInnerSpacing.x;
    const float space_w = ImGui::CalcTextSize (" ").x;
    const int   n       = ImClamp ((int)ImCeil ((arrow_w + gap) / ImMax (space_w, 1.0f)), 1, 16);

    char spaces [17];
    memset (spaces, ' ', n);
    spaces [n] = '\0';

    ImFormatString (display, IM_ARRAYSIZE (display), "%s%s###%s", spaces, label, label);
    shown = display;
  }

  // Mirror SeparatorTextEx()'s layout to find where the text lands
  const ImVec2 label_size = ImGui::CalcTextSize (shown, nullptr, true);
  const ImVec2 pos        = ImGui::GetCursorScreenPos ();
  const ImVec2 padding    = style.SeparatorTextPadding;
  const float  row_w      = ImGui::GetContentRegionAvail ().x;
  const float  row_h      = ImMax (label_size.y + padding.y * 2.0f,
                                   style.SeparatorTextBorderSize);

  const float  avail_w    = ImMax (0.0f, row_w - padding.x * 2.0f);
  const ImVec2 text_pos   (pos.x + padding.x +
                             ImMax (0.0f, (avail_w - label_size.x) * style.SeparatorTextAlign.x),
                           pos.y + ImFloor ((row_h - label_size.y) * style.SeparatorTextAlign.y + 0.5f));

  // Button covering the arrow + text only
  ImGui::PushID (label);
  ImGui::SetCursorScreenPos (text_pos);
  const bool activated = ImGui::InvisibleButton ("##clickable_sep", label_size);
  ImGui::PopID ();

  const ImGuiID id      = ImGui::GetItemID ();
  const ImRect  bb      = ImRect (ImGui::GetItemRectMin (), ImGui::GetItemRectMax ());
  const bool    hovered = ImGui::IsItemHovered ();
  const bool    held    = ImGui::IsItemActive  ();

  if (hovered && hand_cursor)
    ImGui::SetMouseCursor (ImGuiMouseCursor_Hand);

  ImGui::RenderNavCursor (bb, id);

  // Draw the separator row (also advances the cursor by one row)
  ImGui::SetCursorScreenPos (pos);

  const ImVec4 text_col = (hovered || held)
                        ? ImGui::GetStyleColorVec4 (held ? ImGuiCol_SeparatorActive
                                                         : ImGuiCol_SeparatorHovered)
                        : ImGui::GetStyleColorVec4 (ImGuiCol_Text);

  ImGui::PushStyleColor (ImGuiCol_Text, text_col);
  ImGui::SeparatorText  (shown);
  ImGui::PopStyleColor  ();

  // Arrow, drawn as a triangle centered on the horizontal lines
  if (collapsed != nullptr)
  {
    const float font_size = ImGui::GetFontSize ();
    const float r         = font_size * 0.32f;                       // triangle "radius"
    const float cx        = text_pos.x + font_size * 0.5f;           // center of reserved box
    const float cy        = ImFloor (pos.y + row_h * 0.5f + 1.0f);   // Y of the lines

    ImVec2 a, b, c;

    if (*collapsed) // pointing right
    {
      a = ImVec2 (cx + 0.750f * r, cy);
      b = ImVec2 (cx - 0.750f * r, cy + 0.866f * r);
      c = ImVec2 (cx - 0.750f * r, cy - 0.866f * r);
    }
    else            // pointing down
    {
      a = ImVec2 (cx,              cy + 0.750f * r);
      b = ImVec2 (cx - 0.866f * r, cy - 0.750f * r);
      c = ImVec2 (cx + 0.866f * r, cy - 0.750f * r);
    }

    ImGui::GetWindowDrawList ()->AddTriangleFilled (a, b, c, ImGui::GetColorU32 (text_col));
  }

  return activated;
}

// ImGui 1.91.0 removed GetContentRegionMax ( ) and GetWindowContentRegionMax ( ). These return the same values (relative to
//   the window position): the content region's right/bottom edge, the first also within tables and columns.
ImVec2
SKIF_ImGui_GetContentRegionMax (void)
{
  return ImGui::GetContentRegionAvail ( ) + ImGui::GetCursorScreenPos ( ) - ImGui::GetWindowPos ( );
}

ImVec2
SKIF_ImGui_GetWindowContentRegionMax (void)
{
  return ImGui::GetCurrentWindowRead ( )->ContentRegionRect.Max - ImGui::GetWindowPos ( );
}

// Difference to regular BeginChildFrame? No ImGuiWindowFlags_NoMove!
bool
SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
  const ImGuiStyle& style =
    ImGui::GetStyle ();

  //ImGui::PushStyleColor (ImGuiCol_ChildBg,              style.Colors [ImGuiCol_FrameBg]);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildRounding,   style.FrameRounding);
  ImGui::PushStyleVar   (ImGuiStyleVar_ChildBorderSize, style.FrameBorderSize);
  ImGui::PushStyleVar   (ImGuiStyleVar_WindowPadding,   style.FramePadding);

  bool ret =
    ImGui::BeginChild (id, size, ImGuiChildFlags_AlwaysUseWindowPadding | child_flags, window_flags);

  ImGui::PopStyleVar   (3);
  //ImGui::PopStyleColor ( );

  return ret;
}

// Basically like ImGui::Image but, you know, doesn't actually draw the images
//   The texture is an ID3D11ShaderResourceView* (since ImGui 1.91.4 ImTextureID is an integer, so it is converted here)
void SKIF_ImGui_OptImage (void* user_texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
{
  // If not a nullptr, run original code
  if (user_texture_id != nullptr)
  {
    // ImGui 1.91.9 replaced Image ( )'s tint and border parameters with ImageWithBg ( ) and style.ImageBorderSize;
    //   this is what ImGui's own legacy Image ( ) overload does, so the size and border stay as before
    ImGui::PushStyleVar   (ImGuiStyleVar_ImageBorderSize, (border_col.w > 0.0f) ? ImMax (1.0f, ImGui::GetStyle ( ).ImageBorderSize) : 0.0f);
    ImGui::PushStyleColor (ImGuiCol_Border, border_col);
    ImGui::ImageWithBg    ((ImTextureID)(intptr_t)user_texture_id, size, uv0, uv1, ImVec4 (0, 0, 0, 0), tint_col);
    ImGui::PopStyleColor  ( );
    ImGui::PopStyleVar    ( );
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

  ImGuiOldColumnFlags flags = (border ? 0 : ImGuiOldColumnFlags_NoBorder);
  if (! resizeble)
    flags |= ImGuiOldColumnFlags_NoResize;
  //flags |= ImGuiOldColumnFlags_NoPreserveWidths; // NB: Legacy behavior
  ImGuiOldColumns* columns = window->DC.CurrentColumns;
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

  extern ImVec2 SKIF_vecRegularModeDefault;  // Does not include the status bar
  extern ImVec2 SKIF_vecRegularModeAdjusted; // Adjusted for status bar and tooltips
  extern ImVec2 SKIF_vecHorizonModeDefault;  // Does not include the status bar
  extern ImVec2 SKIF_vecHorizonModeAdjusted; // Adjusted for status bar and tooltips

  auto frame_content_area_id =
    ImGui::GetID ("###SKIF_CONTENT_AREA");

  ImVec2 statusbar_size =
     (_registry.bHorizonMode)
      ? SKIF_vecHorizonModeAdjusted - SKIF_vecHorizonModeDefault
      : SKIF_vecRegularModeAdjusted - SKIF_vecRegularModeDefault;

  // DPI-aware
  float maxContentHeight   = ImGui::GetWindowSize().y - ImGui::GetCursorPosY() - (statusbar_size.y * SKIF_ImGui_GlobalDPIScale);

  // Some shitty hack to fix 902px tall covers
  //if (! _registry._LibHorizonMode)
  maxContentHeight -= 2.0f * SKIF_ImGui_GlobalDPIScale;

  maxContentHeight   = ImFloor (maxContentHeight);

  SKIF_ImGui_BeginChildFrame (
    frame_content_area_id,
    ImVec2 (0.0f, maxContentHeight), // 900.0f
    ImGuiChildFlags_NavFlattened, // ImGuiChildFlags_FrameStyle
    ImGuiWindowFlags_None
  );
}

bool
SKIF_ImGui_BeginMenuEx2 (const char* label, const char* icon, const ImVec4& colIcon, bool enabled)
{
  ImGui::PushStyleColor         (ImGuiCol_SKIF_Icon, colIcon);
  bool ret = ImGui::BeginMenuEx (label, icon, enabled);
  ImGui::PopStyleColor          ( );

  return ret;
}

bool
SKIF_ImGui_MenuItemEx2 (const char* label, const char* icon, const ImVec4& colIcon, const char* shortcut, bool* p_selected, bool enabled)
{
  /* If a tooltip is desired when using the enabled parameter,
  *    use the allowWhenItemDisabled parameter of SKIF_ImGui_SetHoverTip()
  *      e.g.
  *
      bool doReset = SKIF_ImGui_MenuItemEx2 (textPtr, iconPtr, iconColor, 0, nullptr, enabled);

      if (! enabled)
        SKIF_ImGui_SetHoverTip ("Managed by the platform client.", false, true);
  *
  */

  ImGui::PushStyleColor        (ImGuiCol_SKIF_Icon, colIcon);
  bool ret = ImGui::MenuItemEx (label, icon, shortcut, p_selected ? *p_selected : false, enabled);
  if (ret && p_selected)
    *p_selected = !*p_selected;
  ImGui::PopStyleColor         ( );

  return ret;
}

bool SKIF_ImGui_IconButton (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon)
{
  bool ret   = false;
  icon       = " " + icon;
  label      = label + " ";

  ImGui::BeginChild (id, ImVec2 (ImGui::CalcTextSize(icon.c_str())  .x +
                                 ImGui::CalcTextSize(label.c_str()) .x +
                                 ImGui::CalcTextSize("    ").x,
                                 ImGui::GetTextLineHeightWithSpacing() + 2.0f * SKIF_ImGui_GlobalDPIScale),
    ImGuiChildFlags_FrameStyle | ImGuiChildFlags_NavFlattened,
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
  );

  ImVec2 iconPos = ImGui::GetCursorPos ( );
  ImGui::ItemSize      (ImVec2 (ImGui::CalcTextSize (icon.c_str()) .x, ImGui::GetTextLineHeightWithSpacing()));
  ImGui::SameLine      ( );
  ImGui::Selectable    (label.c_str(), &ret,  ImGuiSelectableFlags_SpanAllColumns | static_cast<int>(ImGuiSelectableFlags_SpanAvailWidth));
  ImGui::SetCursorPos  (iconPos);
  ImGui::TextColored   (colIcon, icon.c_str());

  ImGui::EndChild ( );

  return ret;
}

// Button to minimize a window, based on ImGui::CloseButton()
// Use SKIF_ImGui_TitleBarMinimizeButton() to draw it on the title bar of the current window.
bool
SKIF_ImGui_MinimizeButton (ImGuiID id, const ImVec2& pos)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* window = g.CurrentWindow;

    // Tweak 1: Shrink hit-testing area if button covers an abnormally large proportion of the visible region. That's in order to facilitate moving the window away. (#3825)
    // This may better be applied as a general hit-rect reduction mechanism for all widgets to ensure the area to move window is always accessible?
    const ImRect bb(pos, pos + ImVec2(g.FontSize, g.FontSize));
    ImRect bb_interact = bb;
    const float area_to_visible_ratio = window->OuterRectClipped.GetArea() / bb.GetArea();
    if (area_to_visible_ratio < 1.5f)
        bb_interact.Expand(ImTrunc(bb_interact.GetSize() * -0.25f));

    // Tweak 2: We intentionally allow interaction when clipped so that a mechanical Alt,Right,Activate sequence can always close a window.
    // (this isn't the common behavior of buttons, but it doesn't affect the user because navigation tends to keep items visible in scrolling layer).
    bool is_clipped = !ImGui::ItemAdd(bb_interact, id);

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb_interact, id, &hovered, &held);
    if (is_clipped)
        return pressed;

    // Render
    // FIXME: Clarify this mess
    ImU32 col = ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered);
    ImVec2 center = bb.GetCenter();
    if (hovered)
        window->DrawList->AddCircleFilled(center, ImMax(2.0f, g.FontSize * 0.5f + 1.0f), col);

    float half_width = g.FontSize * 0.5f * 0.7071f;
    float y_offset = g.FontSize * 0.25f;
    ImU32 line_col = ImGui::GetColorU32(ImGuiCol_Text);
    center -= ImVec2(0.5f, 0.5f);
    window->DrawList->AddLineH(center.x - half_width, center.x + half_width, center.y + y_offset, line_col, 1.0f);

    return pressed;
}

// This is a custom MINIMIZE button that is drawn on the title bar of the current window,
// next to the built-in CLOSE button.
bool
SKIF_ImGui_TitleBarMinimizeButton (void)
{
  bool pressed = false;

  const ImRect title_bar_rect = ImGui::GetCurrentWindowRead()->TitleBarRect();
  ImGui::PushClipRect(title_bar_rect.Min, title_bar_rect.Max, false);

  // Base
  float button_sz = ImGui::GetFontSize();
  float pad_r     =
                  ImGui::GetStyle().FramePadding.x      // Base padding
    + button_sz + ImGui::GetStyle().ItemInnerSpacing.x; // Spacing between the built-in CLOSE button and our custom MINIMIZE button
  ImVec2 minimize_button_pos = ImVec2(title_bar_rect.Max.x - pad_r - button_sz, title_bar_rect.Min.y + ImGui::GetStyle().FramePadding.y);

  if (SKIF_ImGui_MinimizeButton (ImGui::GetCurrentWindow()->GetID("#MINIMIZE"), minimize_button_pos))
    pressed = true;

  ImGui::PopClipRect();

  return pressed;
}

bool
SKIF_ImGui_FrameButton (const char* label, const ImVec2& size)
{
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::PushStyleColor (ImGuiCol_Button,        style.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  style.Colors[ImGuiCol_FrameBgActive]);
    ImGui::PushStyleVar   (ImGuiStyleVar_ButtonTextAlign, ImVec2 (0.0f, 0.0f));

    bool pressed =
      ImGui::Button (label, ImVec2 (size.x > 0 ? size.x : ImGui::CalcItemWidth(), size.y));

    ImGui::PopStyleVar    ( ); // ImGuiStyleVar_ButtonTextAlign
    ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonActive
    ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonHovered
    ImGui::PopStyleColor  ( ); // ImGuiCol_Button

    return pressed;
}

void
SKIF_ImGui_DimBackgroundViewport (const ImGuiViewport* vp)
{
  if (! vp)
    vp = ImGui::GetMainViewport ( );

  const ImGuiWindowFlags dim_flags =
    ImGuiWindowFlags_NoDecoration       |
    ImGuiWindowFlags_NoBackground       |
    ImGuiWindowFlags_NoMove             |
    ImGuiWindowFlags_NoSavedSettings    |
    ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoDocking          |
  //ImGuiWindowFlags_NoInputs           | // Never hovered/focused; clicks pass through
    ImGuiWindowFlags_NoNav;               // Eat input from the background window

  char dim_name[50];
  snprintf (dim_name, sizeof(dim_name), "##SKIFDimBackground_%u", vp->ID);

  ImGui::SetNextWindowPos      (vp->Pos);
  ImGui::SetNextWindowSize     (vp->Size);
  ImGui::SetNextWindowViewport (vp->ID);

  ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_WindowBorderSize, 0.0f);

  if (ImGui::Begin (dim_name, nullptr, dim_flags))
  {
    ImGui::GetWindowDrawList ( )->AddRectFilled (
      vp->Pos, vp->Pos + vp->Size,
      ImGui::GetColorU32 (ImGuiCol_ModalWindowDimBg)
    );

    // Raise above all other windows (display order only, no focus change)
    ImGui::BringWindowToDisplayFront (ImGui::GetCurrentWindow ( ));
  }

  ImGui::End ( );
  ImGui::PopStyleVar ( ); // ImGuiStyleVar_WindowBorderSize
  ImGui::PopStyleVar ( ); // ImGuiStyleVar_WindowPadding
}

// Always call SKIF_ImGui_EndPopup () for both true/false from this function.
// This is a custom, but also simplified, window that acts much like a modal popup
// without actually being one.
// 
// Not compatible with ImGui::SetNextWindowSize() as the dimmed background eats it
bool
SKIF_ImGui_BeginPopup (const char* name, bool* p_open, ImVec2 size, ImGuiWindowFlags flags)
{
  // Use same background color as real model popups
  ImGui::PushStyleColor (ImGuiCol_WindowBg, ImGui::GetStyleColorVec4 (ImGuiCol_PopupBg));

  if (flags == ImGuiWindowFlags_None)
    flags = ImGuiWindowFlags_AlwaysAutoResize;

  flags |= ImGuiWindowFlags_NoCollapse         |
           ImGuiWindowFlags_NoResize           |
         //ImGuiWindowFlags_NoScrollbar        |
           ImGuiWindowFlags_NoDocking          |
           ImGuiWindowFlags_NoSavedSettings;

  const ImGuiViewport* vp = ImGui::GetWindowViewport();

  // Dim the background, also eats up ImGui::SetNextWindowSize()
  SKIF_ImGui_DimBackgroundViewport (vp);

  // Keep the window within the current viewport (disables Win32 window)
  ImGui::SetNextWindowViewport (vp->ID);

  // Center on the current viewport
  ImGui::SetNextWindowPos      (vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  if (size.x != 0.0f || size.y != 0.0f)
    ImGui::SetNextWindowSize (size);

  bool ret =
    ImGui::Begin (name, p_open, flags);

  // Raise above all other windows (display order only, no focus change)
  if (ret)
  {
    ImGuiContext& g      = *ImGui::GetCurrentContext ( );
    ImGuiWindow*  window =  ImGui::GetCurrentWindow  ( );

    ImGui::BringWindowToDisplayFront (window);

    // ... but keep popups opened from within it (combo boxes, context menus) above it, or they end up hidden behind it
    for (const ImGuiPopupData& popup : g.OpenPopupStack)
      if (popup.Window != nullptr && popup.Window != window && ImGui::IsWindowChildOf (popup.Window, window, true, false))
        ImGui::BringWindowToDisplayFront (popup.Window);
  }

  return ret;
}

void
SKIF_ImGui_EndPopup (void)
{
  ImGui::End ( );
  ImGui::PopStyleColor ( ); // ImGuiCol_WindowBg, from SKIF_ImGui_BeginModalWindow()
}

// Call every frame while the prompt is active.
// Returns Choice::None until the user picks something.
DChoice
SKIF_ImGui_DialogPrompt (const char* name, bool* p_open, const char* message, const char* description, std::vector<DChoice> choices)
{
  DChoice result =
    DChoice_None;

  if (! *p_open)
    return result;

  if (choices.empty())
    return result;
  
  if (SKIF_ImGui_BeginPopup (name))
  {
    if (message != nullptr)
      ImGui::Text         ("%s", message); // Never as a format: the text may be translated

    if (description != nullptr)
      ImGui::TextDisabled ("%s", description);

    if (message || description)
    ImGui::Separator ( );

    // The labels are in English (also the IDs of the buttons), translated where shown
    static const std::vector<std::pair<const DChoice, const char*>>
      btnTxtVec = {
        // Generic
        { DChoice_None,     NL_("None")       },
        { DChoice_Cancel,   NL_("Cancel")     },
        // Confirm
        { DChoice_Yes,      NL_("Yes")        },
        { DChoice_No,       NL_("No")         },
        // Save
        { DChoice_Save,     NL_("Save")       },
        { DChoice_DontSave, NL_("Don't Save") },
        // CPU bitness
        {   DChoice_32bit,    "32-bit"        },
        {   DChoice_64bit,    "64-bit"        },
        // Info
        { DChoice_OK,       NL_("OK")         },
    };

    // The bitness is not translated
    auto _Label = [](const std::pair<const DChoice, const char*>& p) -> const char*
    {
      return (p.first == DChoice_32bit || p.first == DChoice_64bit) ? p.second : TL_(p.second);
    };

    // All buttons as wide as the widest (translated) label, at least 110 px
    ImVec2
      btn       = ImVec2(110, 0) * SKIF_ImGui_GlobalDPIScale;

    for (auto& choice : choices)
    {
      auto it = std::find_if (btnTxtVec.begin(), btnTxtVec.end(),
        [choice](const auto& p) { return p.first == choice; });

      if (it != btnTxtVec.end())
        btn.x = std::max (btn.x, ImGui::CalcTextSize (_Label (*it), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f);
    }

    for (auto& choice : choices)
    {
      auto it = std::find_if (btnTxtVec.begin(), btnTxtVec.end(),
        [choice](const auto& p) { return p.first == choice; });

      if (it == btnTxtVec.end())
        continue;

      if (ImGui::Button (_Label (*it), btn))
        result = it->first;

      ImGui::SameLine ( );
    }

    // Empty spacing to eat the trailing SameLine ( )
    ImGui::Spacing ( );
  }

  SKIF_ImGui_EndPopup ( );

  if (result != DChoice_None)
    *p_open = false;

  return result;
}

// Call every frame while the prompt is active.
// Returns Choice::None until the user picks something.
DChoice
SKIF_ImGui_ConfirmPrompt (const char* name, bool* p_open, const char* message, const char* description)
{
  const char* default_message =
    _("Are you sure you want to continue?");

  return SKIF_ImGui_DialogPrompt (
    name,
    p_open,
    (message == nullptr ? default_message : message),
    description,
    { DChoice_Yes, DChoice_No, DChoice_Cancel }
  );
}

// Call every frame while the prompt is active.
// Returns Choice::None until the user picks something.
DChoice
SKIF_ImGui_SaveChangesPrompt (const char* name, bool* p_open, const char* message, const char* description)
{
  const char* default_msg  = _("Do you want to save the changes made?");
  const char* default_desc = _("Your changes will be lost if you don't save them.");

  return SKIF_ImGui_DialogPrompt (
    name,
    p_open,
    (message     == nullptr ? default_msg  : message    ),
    (description == nullptr ? default_desc : description),
    { DChoice_Save, DChoice_DontSave, DChoice_Cancel }
  );
}

// Call every frame while the prompt is active.
// Returns Choice::None until the user acknowledges it.
DChoice
SKIF_ImGui_InfoPrompt (const char* name, bool* p_open, const char* message, const char* description)
{
  return SKIF_ImGui_DialogPrompt (
    name,
    p_open,
    message,
    description,
    { DChoice_OK }
  );
}

DChoice
SKIF_ImGui_6432bitnessPrompt (const char* name, bool* p_open, const char* message, const char* description)
{
  const char* default_msg  = _("Should 64-bit or 32-bit be used?");
  const char* default_desc = _("Processes can only load DLL files of the same bitness.");

  return SKIF_ImGui_DialogPrompt (
    name,
    p_open,
    (message     == nullptr ? default_msg  : message    ),
    (description == nullptr ? default_desc : description),
    { DChoice_64bit, DChoice_32bit, DChoice_Cancel }
  );
}

// Renders a faded checkmark when hovering over the checkbox
// Works best when disabling ImGuiCol_FrameBg, ImGuiCol_FrameBgActive, ImGuiCol_FrameBgHovered
bool SKIF_ImGui_CheckboxHoverPreview (const char* label, bool* v, float preview_alpha)
{
  bool pressed = ImGui::Checkbox(label, v);

  if (!*v && ImGui::IsItemHovered())
  {
    const float  square_sz   = ImGui::GetFrameHeight();
    const ImVec2 check_bbMin = ImGui::GetItemRectMin();
    const float  pad         = ImMax(1.0f, IM_TRUNC(square_sz / 6.0f));
    const ImU32  check_col   = ImGui::ColorConvertFloat4ToU32 (
      ImGui::GetStyleColorVec4(ImGuiCol_CheckMark) * ImVec4(1.0f, 1.0f, 1.0f, ImGui::GetStyle().Alpha * preview_alpha)
    );

    // Mirrors what ImGui::Checkbox() does natively
    ImGui::RenderCheckMark(ImGui::GetWindowDrawList(), check_bbMin + ImVec2(pad, pad), check_col, square_sz - pad * 2.0f);
  }

  return pressed;
}

// Tri-state checkbox with a hover preview of what a click will lead to.
//  - The box shows 'state' (Mixed uses ImGui's own mixed-value mark).
//  - While hovered, the current mark is hidden and 'preview' is drawn faded instead
//    (a faded mixed mark, a faded checkmark, or nothing at all for Unchecked).
//  - After a click the preview is suppressed until the mouse leaves the box, so the
//    new state is visible right away instead of being covered by the next preview.
// Returns true when clicked; the caller decides what the new state is.
bool SKIF_ImGui_CheckboxTriState (const char* label, SKIF_ImGuiCheckState state, SKIF_ImGuiCheckState preview, float preview_alpha)
{
  if (preview == SKIF_ImGuiCheckState_Auto)
    preview = (state == SKIF_ImGuiCheckState_Unchecked) ? SKIF_ImGuiCheckState_Mixed   :
              (state == SKIF_ImGuiCheckState_Mixed)     ? SKIF_ImGuiCheckState_Checked :
                                                          SKIF_ImGuiCheckState_Unchecked;

  const ImGuiID id         = ImGui::GetID (label);
  const ImGuiID suppressId = ImHashStr ("##TriStatePreviewSuppressed", 0, id);
  ImGuiStorage* storage    = ImGui::GetStateStorage ( );

  // Hover state from the previous frame, since the item has not been submitted yet
  const bool hovering      = (ImGui::GetHoveredID ( ) == id);
  const bool showPreview   = hovering && preview != state && ! storage->GetBool (suppressId, false);

  // While previewing, draw an empty box underneath the faded preview mark
  bool v                   = (! showPreview && state == SKIF_ImGuiCheckState_Checked);
  ImGui::PushItemFlag (ImGuiItemFlags_MixedValue, (! showPreview && state == SKIF_ImGuiCheckState_Mixed));
  const bool pressed       = ImGui::Checkbox (label, &v);
  ImGui::PopItemFlag  ( );

  if (pressed)
    storage->SetBool (suppressId, true);
  else if (! ImGui::IsItemHovered ( ))
    storage->SetBool (suppressId, false);

  if (showPreview && ! pressed && preview != SKIF_ImGuiCheckState_Unchecked)
  {
    const float  square_sz = ImGui::GetFrameHeight ( );
    const ImVec2 bbMin     = ImGui::GetItemRectMin ( );
    const ImU32  col       = ImGui::GetColorU32    (ImGuiCol_CheckMark, preview_alpha);
    ImDrawList*  drawList  = ImGui::GetWindowDrawList ( );

    if (preview == SKIF_ImGuiCheckState_Mixed)
    {
      // Mirrors the mixed-value mark ImGui::Checkbox() draws natively
      const float pad = ImMax (1.0f, IM_TRUNC (square_sz / 3.6f));
      drawList->AddRectFilled (bbMin + ImVec2 (pad, pad), bbMin + ImVec2 (square_sz - pad, square_sz - pad), col, ImGui::GetStyle ( ).FrameRounding);
    }

    else
    {
      // Mirrors the checkmark ImGui::Checkbox() draws natively
      const float pad = ImMax (1.0f, IM_TRUNC (square_sz / 6.0f));
      ImGui::RenderCheckMark (drawList, bbMin + ImVec2 (pad, pad), col, square_sz - pad * 2.0f);
    }
  }

  return pressed;
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
    ImGui::PushStyleColor (ImGuiCol_NavCursor, ImVec4(0,0,0,0));
    ImGui::TextColored (
      ImColor::HSV (0.11F, 1.F, 1.F), "%s",
        _("Troubleshooting:")
    );

    ImGui::Separator ( );

    if (ImGui::Selectable(L_("Force start service###ServiceMenuStart")))
      _inject._StartStopInject (false, _registry.bStopOnInjection);

    ImGui::PushStyleColor      (ImGuiCol_Text,
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
    );

    if (ImGui::Selectable(LI_("\xe2\x94\x94", "Elevated###ServiceMenuStartElevated")))
      _inject._StartStopInject (false, _registry.bStopOnInjection, true);

    ImGui::PopStyleColor   ( );

    if (ImGui::Selectable(L_("Force stop service###ServiceMenuStop")))
      _inject._StartStopInject (true);

    ImGui::PopStyleColor ( );
    ImGui::EndPopup ( );
  }
}


// Fonts



// Since ImGui 1.92, glyphs are rasterized the first time they are drawn, and glyph ranges are not used: for every glyph,
//   the merged font sources are searched in the order they were added. So the standard font provides all the
//   characters it has, and an extended character set is a whole font that is added once one of its characters is
//   missing. The order of the sources then decides which font a character comes from.
//
// Called by ImGui (ImFontBaked_BuildLoadGlyph) when none of the font sources has a glyph for the character: once per
//   character and font size, while drawing or measuring text. This only queues the next font to try (if any); the
//   fonts are rebuilt with it before the next frame (invalidateFonts).
void
SKIF_ImGui_MissingGlyphCallback (wchar_t c)
{
  const unsigned int cp = static_cast <unsigned int> (c);

  const bool hangul =          SKIF_ImGui_IsInGlyphRanges (SKIF_ImGui_GlyphsHangul, cp);
  const bool cjk    = ! hangul && SKIF_ImGui_IsInGlyphRanges (SKIF_ImGui_GlyphsCJK,    cp);

  // The fonts to try for the character, in order (for Han characters, see SKIF_ImGui_GetHanFontOrder)
  std::vector <SKIF_ImGui_ExtFont> chain;

  if (hangul)
    chain = { SKIF_ImGui_ExtFont_Korean };
  else if (cjk)
    chain = SKIF_ImGui_GetHanFontOrder ( );

  const size_t chain_size = chain.size ( );

  for (size_t i = 0; i < chain_size; i++)
  {
#ifndef _WIN64
    // 32-bit SKIF breaks if too many character sets are
    //   loaded so omit Chinese Full on those versions.
    if (chain [i] == SKIF_ImGui_ExtFont_ChineseFull)
      continue;
#endif

    if (! SKIF_ImGui_ExtFontWanted [chain [i]])
    {
      SKIF_ImGui_ExtFontWanted [chain [i]] = true;

      PLOG_VERBOSE << "[Fonts] No font has U+" << std::hex << cp << std::dec << ", adding extended character set " << chain [i] << ".";

      extern bool invalidateFonts;
      invalidateFonts = true;

      break;
    }
  }
}

// Applies the UI language to the fonts: the order of the Han fonts (see SKIF_ImGui_GetHanFontOrder), and the regional
//   font of the language is added up front instead of once its first character turns up. Returns true if the fonts
//     need to be rebuilt (call before SKIF_ImGui_InitFonts ( ), or set invalidateFonts).
bool
SKIF_ImGui_SetUILanguage (const std::string& tag)
{
  std::string lower = tag;
  std::transform (lower.begin ( ), lower.end ( ), lower.begin ( ), [](char ch) { return static_cast <char> ((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch); });
  std::string primary = lower.substr (0, lower.find ('-'));

  SKIF_ImGui_HanPreference preferred = SKIF_ImGui_Han_Default;
  SKIF_ImGui_ExtFont       font      = SKIF_ImGui_ExtFont_COUNT;

  if (primary == "ja")
  {
    preferred = SKIF_ImGui_Han_Japanese;
    font      = SKIF_ImGui_ExtFont_Japanese;
  }

  else if (primary == "zh")
  {
    preferred = (SKIF_L10n::IsTraditionalChinese (tag)) ? SKIF_ImGui_Han_ChineseTraditional : SKIF_ImGui_Han_ChineseSimplified;
    font      = (SKIF_L10n::IsTraditionalChinese (tag)) ? SKIF_ImGui_ExtFont_ChineseFull     : SKIF_ImGui_ExtFont_ChineseSimplified;

#ifndef _WIN64
    // 32-bit SKIF omits the Chinese Full character set (see SKIF_ImGui_MissingGlyphCallback)
    if (font == SKIF_ImGui_ExtFont_ChineseFull)
      font = SKIF_ImGui_ExtFont_ChineseSimplified;
#endif
  }

  else if (primary == "ko")
    font = SKIF_ImGui_ExtFont_Korean;

  bool rebuild = false;

  if (SKIF_ImGui_HanPreferred != preferred)
  {
    SKIF_ImGui_HanPreferred = preferred;
    rebuild = true; // The order of fonts that are already loaded may change
  }

  if (font != SKIF_ImGui_ExtFont_COUNT && ! SKIF_ImGui_ExtFontWanted [font])
  {
    SKIF_ImGui_ExtFontWanted [font] = true;
    rebuild = true;
  }

  return rebuild;
}

ImFont*
SKIF_ImGui_LoadFont ( const std::wstring& filename, float point_size, ImFontConfig* cfg )
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
                                         cfg );
  }

  return (ImFont *)nullptr;
}

void
SKIF_ImGui_InitFonts (float fontSize, bool extendedCharsets)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  // Font size should always be rounded down to the nearest integer
  fontSize = floor(fontSize);

  auto& io =
    ImGui::GetIO ();

  // Reset any existing fonts. Since ImGui 1.92 this is allowed at any time (it should be done between frames here, as
  //   pointers such as fontConsolas change); the renderer backend recreates the atlas texture on request.
  //
  // This must be Clear ( ) and not ClearFonts ( ): ClearFonts ( ) discards the glyphs and the packer but keeps the
  //   atlas texture and its pixels, and the next build then packs the new glyphs over the old ones from the top-left
  //     of that same texture. Each new glyph only overwrites its own rectangle, so the padding around it still holds
  //       pieces of the previous glyphs, which texture filtering blends into the edges of the new glyphs (garbage
  //         pixels, seen after a non-Latin character made SKIF reload the fonts with an extended character set).
  //   Clear ( ) also frees the pixels of the atlas textures, so the next build starts on a new, empty texture
  //     (the old one is destroyed by the backend the next frame).
  io.Fonts->Clear ( );
  io.FontDefault = nullptr;

  // SKIF's pixel shader reads the font atlas as an alpha-only texture (ImGui_ImplDX11_UpdateTexture supports both)
  io.Fonts->TexDesiredFormat = ImTextureFormat_Alpha8;

  // Font Awesome's icons are in the Private Use Area: no other font source may provide those codepoints...
  static_assert (ICON_MIN_FA <= ICON_MIN_FAB && ICON_MAX_FAB <= ICON_MAX_FA, "The Font Awesome ranges are expected to overlap");
  static const ImWchar exclude_font_awesome [] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
  // ...and the Font Awesome sources only provide their icons, as with their glyph ranges before
  static const ImWchar only_font_awesome    [] = { 0x0001, ICON_MIN_FA - 1, ICON_MAX_FA + 1, 0xFFFF, 0 };
  // Each regional font only provides its own script, as with the glyph ranges before ImGui 1.92 (since each font is now
  //   added whole, the order of the sources would otherwise decide e.g. whether Hangul comes from a Chinese font)
  static const ImWchar exclude_hangul_and_font_awesome [] = { SKIF_IMGUI_GLYPHS_HANGUL, ICON_MIN_FA, ICON_MAX_FA, 0 }; // Chinese, Japanese
  static const ImWchar exclude_cjk_and_font_awesome    [] = { SKIF_IMGUI_GLYPHS_CJK,    ICON_MIN_FA, ICON_MAX_FA, 0 }; // Korean
  static_assert (IM_COUNTOF (exclude_cjk_and_font_awesome) - 1 <= 64, "ImFontConfig::GlyphExcludeRanges[] must be small");

  ImFontConfig
  font_cfg           = {  };
  font_cfg.GlyphExcludeRanges = exclude_font_awesome;

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

  // Core character set (since 1.92 all of the font's characters, e.g. also Cyrillic, Greek, Thai and Vietnamese)
  SKIF_ImGui_LoadFont     (standardFont, fontSize, &font_cfg);
  //SKIF_ImGui_LoadFont     ((fontDir / L"NotoSans-Regular.ttf"), fontSize, &font_cfg);

  if (! useDefaultFont)
  {
    font_cfg.MergeMode = true;
  }

  // Load extended character sets when SKIF is not used as a launcher, once one of their characters has been missing
  if (extendedCharsets)
  {
    font_cfg.GlyphExcludeRanges = exclude_hangul_and_font_awesome;

    // Japanese and Chinese character sets: Han characters come from the first of these fonts that is loaded,
    //   so the order follows the UI language or the ANSI code page (see SKIF_ImGui_GetHanFontOrder)
    for (auto ext : SKIF_ImGui_GetHanFontOrder ( ))
    {
      if (! SKIF_ImGui_ExtFontWanted [ext])
        continue;

      switch (ext)
      {
        // Japanese character set
        case SKIF_ImGui_ExtFont_Japanese:
          //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, &font_cfg);
          if (SKIF_Util_IsWindows10OrGreater ( ))
            SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, &font_cfg);
          else
            SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, &font_cfg);
          break;

        // Simplified Chinese character set
        // Also includes almost all of the Japanese characters except for some Kanjis
        case SKIF_ImGui_ExtFont_ChineseSimplified:
          SKIF_ImGui_LoadFont   (L"msyh.ttc",     fontSize, &font_cfg);
          //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansSC-Regular.ttf"), fontSize, &font_cfg);
          break;

        // All Chinese character sets
        case SKIF_ImGui_ExtFont_ChineseFull:
          SKIF_ImGui_LoadFont   (L"msjh.ttc",     fontSize, &font_cfg);
          //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansTC-Regular.ttf"), fontSize, &font_cfg);
          break;

        default:
          break;
      }
    }

    // Korean character set
    font_cfg.GlyphExcludeRanges = exclude_cjk_and_font_awesome;

    if (SKIF_ImGui_ExtFontWanted [SKIF_ImGui_ExtFont_Korean])
      SKIF_ImGui_LoadFont   (L"malgun.ttf",   fontSize, &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansKR-Regular.ttf"), fontSize, &font_cfg);
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

    // In January, 2024 various optimization attempts were made to on-demand load Font Awesome characters
    //   as they appeared, but while this shaved down a bit on the initial launch time, it also resulted
    //     in unnecessary ~20ms delays every time a new Font Awesome character appeared on-screen...
    //        ... Meanwhile it only shaved of like 10-15ms on the launch time, lol ! // Aemony
    //   (Since ImGui 1.92 all glyphs are rasterized on demand, as they first appear.)

    font_cfg.GlyphExcludeRanges = only_font_awesome;

    // FA Regular is basically useless as it only has 163 icons, so we don't bother using it
    // FA Solid has 1390 icons in comparison
        SKIF_ImGui_LoadFont (fontDir/FONT_ICON_FILE_NAME_FAS, fontSizeFA, &font_cfg);
    // FA Brands
    SKIF_ImGui_LoadFont (fontDir/FONT_ICON_FILE_NAME_FAB, fontSizeFA, &font_cfg);

    //io.Fonts->AddFontDefault ();

    fontConsolas = SKIF_ImGui_LoadFont (L"Consola.ttf", fontSizeConsolas);
    //fontConsolas = SKIF_ImGui_LoadFont ((fontDir / L"NotoSansMono-Regular.ttf"), fontSize/* - 4.0f*/);

  // The text size used by default, before DPI scaling (applied per window through style.FontScaleDpi).
  //   If it is 0, ImGui falls back to the size of the first font at the start of every frame.
  ImGui::GetStyle ( ).FontSizeBase = fontSize;
}

// Per-monitor DPI
//
// The fonts are loaded once at their base size and the style is built unscaled (SKIF_ImGui_BaseStyle). Every window is
//   then scaled to the DPI of the monitor it is on: ImGui calls SKIF_ImGui_OnChangedViewport from Begin ()/End () when
//     it switches to a window on another viewport, which swaps the style sizes and style.FontScaleDpi (ImGui bakes the
//       glyphs for each font size on demand, so this needs no font reload), and sets SKIF_ImGui_GlobalDPIScale to the
//         scale of the window being built. SKIF_ImGui_MainDPIScale always holds the scale of the main window.

static ImGuiStyle                   SKIF_ImGui_BaseStyle;              // Unscaled, built by SKIF_ImGui_SetStyle ()
static std::map <float, ImGuiStyle> SKIF_ImGui_ScaledStyles;           // SKIF_ImGui_BaseStyle.ScaleAllSizes (key)
static float                        SKIF_ImGui_AppliedDPIScale = 1.0f; // The scale the live style currently carries

static const ImGuiStyle&
SKIF_ImGui_GetScaledStyle (float scale)
{
  auto it = SKIF_ImGui_ScaledStyles.find (scale);

  if (it == SKIF_ImGui_ScaledStyles.end ( ))
  {
    ImGuiStyle style = SKIF_ImGui_BaseStyle;
    style.ScaleAllSizes (scale);

    it = SKIF_ImGui_ScaledStyles.emplace (scale, style).first;
  }

  return it->second;
}

// Rescales the sizes of the live style from the scale it currently carries to a new one.
//   Values nobody has pushed snap to the exact scaled style, while values changed by ImGui::PushStyleVar ()
//     are rescaled by ratio (ImGui::PopStyleVar () restores its own backup, so any rounding is temporary).
void
SKIF_ImGui_ApplyDPIScale (float scale)
{
  if (scale <= 0.0f || scale == SKIF_ImGui_AppliedDPIScale)
    return;

  ImGuiStyle&       cur   = ImGui::GetStyle ( );
  const ImGuiStyle& from  = SKIF_ImGui_GetScaledStyle (SKIF_ImGui_AppliedDPIScale);
  const ImGuiStyle& to    = SKIF_ImGui_GetScaledStyle (scale);
  const float       ratio = scale / SKIF_ImGui_AppliedDPIScale;

  auto F = [&](float ImGuiStyle::* m)
  {
    if (cur.*m == from.*m)
      cur.*m = to.*m;
    else if (cur.*m != FLT_MAX)
      cur.*m = ImFloor (cur.*m * ratio + 0.5f);
  };

  auto V = [&](ImVec2 ImGuiStyle::* m)
  {
    if ((cur.*m).x == (from.*m).x && (cur.*m).y == (from.*m).y)
      cur.*m = to.*m;
    else
      cur.*m = ImVec2 (ImFloor ((cur.*m).x * ratio + 0.5f),
                       ImFloor ((cur.*m).y * ratio + 0.5f));
  };

  // The same fields as ImGuiStyle::ScaleAllSizes () in ImGui 1.93.0.
  // TODO: Re-check this list on every ImGui update!
  V (&ImGuiStyle::WindowPadding);           F (&ImGuiStyle::WindowRounding);           F (&ImGuiStyle::WindowBorderSize);
  V (&ImGuiStyle::WindowMinSize);           F (&ImGuiStyle::WindowBorderHoverPadding);
  F (&ImGuiStyle::ChildRounding);           F (&ImGuiStyle::ChildBorderSize);
  F (&ImGuiStyle::PopupRounding);           F (&ImGuiStyle::PopupBorderSize);
  V (&ImGuiStyle::FramePadding);            F (&ImGuiStyle::FrameBorderSize);          F (&ImGuiStyle::FrameRounding);
  V (&ImGuiStyle::ItemSpacing);             V (&ImGuiStyle::ItemInnerSpacing);         V (&ImGuiStyle::CellPadding);
  V (&ImGuiStyle::TouchExtraPadding);       F (&ImGuiStyle::IndentSpacing);            F (&ImGuiStyle::ColumnsMinSpacing);
  F (&ImGuiStyle::ScrollbarSize);           F (&ImGuiStyle::ScrollbarRounding);        F (&ImGuiStyle::ScrollbarPadding);
  F (&ImGuiStyle::GrabMinSize);             F (&ImGuiStyle::GrabRounding);             F (&ImGuiStyle::LogSliderDeadzone);
  F (&ImGuiStyle::ImageRounding);           F (&ImGuiStyle::ImageBorderSize);
  F (&ImGuiStyle::TabRounding);             F (&ImGuiStyle::TabBorderSize);
  F (&ImGuiStyle::TabMinWidthBase);         F (&ImGuiStyle::TabMinWidthShrink);
  F (&ImGuiStyle::TabCloseButtonMinWidthSelected);
  F (&ImGuiStyle::TabCloseButtonMinWidthUnselected);
  F (&ImGuiStyle::TabBarBorderSize);        F (&ImGuiStyle::TabBarOverlineSize);
  F (&ImGuiStyle::TreeLinesSize);           F (&ImGuiStyle::TreeLinesRounding);
  F (&ImGuiStyle::MenuItemRounding);        F (&ImGuiStyle::SelectableRounding);
  F (&ImGuiStyle::DragDropTargetRounding);  F (&ImGuiStyle::DragDropTargetBorderSize); F (&ImGuiStyle::DragDropTargetPadding);
  F (&ImGuiStyle::ColorMarkerSize);         F (&ImGuiStyle::InputTextCursorSize);
  F (&ImGuiStyle::SeparatorSize);           F (&ImGuiStyle::SeparatorTextBorderSize);  V (&ImGuiStyle::SeparatorTextPadding);
  F (&ImGuiStyle::DockingSeparatorSize);    V (&ImGuiStyle::DisplayWindowPadding);     V (&ImGuiStyle::DisplaySafeAreaPadding);
  F (&ImGuiStyle::MouseCursorScale);

  cur._MainScale   = to._MainScale;
  cur.FontScaleDpi = scale;

  SKIF_ImGui_AppliedDPIScale = scale;

  // ImGui::End () calls SetCurrentWindow (parent), which sizes the font, *before* SetCurrentViewport (parent),
  //   so without this the parent window would continue with the font size of the child window
  if (ImGui::GetCurrentContext ( )->CurrentWindow != nullptr)
    ImGui::UpdateCurrentFontSize (0.0f);
}

// Set as ImGuiPlatformIO::Platform_OnChangedViewport, which Begin ()/End () call when switching to a window on another viewport
void
SKIF_ImGui_OnChangedViewport (ImGuiViewport* viewport)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  const float scale = (_registry.bDPIScaling && viewport != nullptr && viewport->DpiScale > 0.0f) ? viewport->DpiScale : 1.0f;

  SKIF_ImGui_GlobalDPIScale = scale; // What the UI code multiplies its pixel sizes with
  SKIF_ImGui_ApplyDPIScale   (scale); // The style sizes and style.FontScaleDpi
}

// Restores the main window's scale (used between frames, after ImGui::Render ())
void
SKIF_ImGui_RestoreMainDPIScale (void)
{
  SKIF_ImGui_GlobalDPIScale = SKIF_ImGui_MainDPIScale;
  SKIF_ImGui_ApplyDPIScale   (SKIF_ImGui_MainDPIScale);
}

// The scale to use for sizes set up before ImGui::Begin () of a window that can be on another monitor than the main
//   window (SetNextWindowSize (), SetNextWindowSizeConstraints ()), as those are applied before Begin () selects the viewport
float
SKIF_ImGui_GetDPIScaleForWindow (const char* name)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // The monitor the window was on last frame
  if (ImGuiWindow* window = ImGui::FindWindowByName (name))
    if (window->WasActive && window->Viewport != nullptr && window->Viewport->DpiScale > 0.0f)
      return (_registry.bDPIScaling) ? window->Viewport->DpiScale : 1.0f;

  // Not shown yet: it opens centered on the main window
  return SKIF_ImGui_MainDPIScale;
}

// Do not use mid-frame to change the style!
void
SKIF_ImGui_SetStyle (ImGuiStyle* dst)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  if (dst == nullptr)
    dst = &ImGui::GetStyle ( );

  _registry._StyleLightMode = false;

  // Setup Dear ImGui style
  switch (_registry.iStyle)
  {
  case UIStyle_ImGui_Dark:
    ImGui::StyleColorsDark      (dst);
    break;
  case UIStyle_ImGui_Classic:
    ImGui::StyleColorsClassic   (dst);
    break;
  case UIStyle_SKIF_Light:
    SKIF_ImGui_StyleColorsLight (dst);
    _registry._StyleLightMode = true;
    break;
  case UIStyle_SKIF_Dark:
    SKIF_ImGui_StyleColorsDark  (dst);
    break;
  case UIStyle_Dynamic:
  default:
    _registry.iStyle          = 0;
    _registry._StyleLightMode = static_cast<bool> (_registry.regKVWindowUseLightTheme.getData ( ));

    if (_registry._StyleLightMode)
      SKIF_ImGui_StyleColorsLight (dst);
    else
      SKIF_ImGui_StyleColorsDark  (dst);
    break;
  }

  // Set up OS decorations
  SKIF_Util_UpdateAppColorMode ( );

  // Override the style with a few tweaks of our own
  dst->DisabledAlpha   = 1.0f; // Disable the default 60% alpha transparency for disabled items
  dst->WindowRounding  = 4.0F; // style.ScrollbarRounding;
  dst->ChildRounding   = dst->WindowRounding;
  dst->TabRounding     = dst->WindowRounding;
  dst->FrameRounding   = dst->WindowRounding;

  // Touch input adjustment
  if (SKIF_Util_IsTouchCapable ( ))
  {
    _registry._TouchDevice = true;
    dst->ScrollbarSize = 50.0f;
  }

  else
    _registry._TouchDevice = false;

  if (! _registry.bUIBorders)
  {
    dst->TabBorderSize   = 0.0F;
    dst->FrameBorderSize = 0.0F;

    // Necessary to hide the 1 px separator shown at the bottom of the tabs row
    dst->Colors[ImGuiCol_TabSelected] = dst->Colors[ImGuiCol_WindowBg];
  }

  if (_registry._sRGBColors)
    for (int i=0; i < ImGuiCol_COUNT; i++)
        dst->Colors[i] = SKIF_ImGui_sRGBtoLinear (dst->Colors[i]);

  // Store the unscaled style; the DPI scaled copies are made on demand for each DPI in use (see SKIF_ImGui_ApplyDPIScale)
  SKIF_ImGui_BaseStyle = *dst;
  SKIF_ImGui_ScaledStyles.clear ( );

  // The live style starts out at the main window's scale
  ImGui::GetStyle ( )              = SKIF_ImGui_GetScaledStyle (SKIF_ImGui_MainDPIScale);
  ImGui::GetStyle ( ).FontScaleDpi = SKIF_ImGui_MainDPIScale;
  SKIF_ImGui_AppliedDPIScale       = SKIF_ImGui_MainDPIScale;
}

void
SKIF_ImGui_BeginDisabled (bool disabled)
{
  SKIF_ImGui_PushDisableState (disabled);
}

void
SKIF_ImGui_EndDisabled (bool disabled)
{
  SKIF_ImGui_PopDisableState (disabled);
}

void
SKIF_ImGui_PushDisableState (bool disabled)
{
  if (! disabled)
    return;

  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // Push the states in a specific order
  ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
  //ImGui::PushStyleVar (ImGuiStyleVar_Alpha,     ImGui::GetStyle ().Alpha * 0.5f); // [UNUSED]
  ImGui::PushStyleColor (ImGuiCol_Text,           ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
  ImGui::PushStyleColor (ImGuiCol_SliderGrab,     SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (_registry._StyleLightMode) ? 0.75f : 0.25f));
  ImGui::PushStyleColor (ImGuiCol_CheckMark,      SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (_registry._StyleLightMode) ? 0.75f : 0.25f));
  ImGui::PushStyleColor (ImGuiCol_FrameBg,        SKIF_ImGui_ImDerp (ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg), ImGui::GetStyleColorVec4 (ImGuiCol_FrameBg),      (_registry._StyleLightMode) ? 0.75f : 0.15f));
}

void
SKIF_ImGui_PopDisableState (bool disabled)
{
  if (! disabled)
    return;

  // Pop the states in the reverse order that we pushed them in
  ImGui::PopStyleColor (4); // ImGuiCol_FrameBg, ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_Text
  //ImGui::PopStyleVar ( ); // ImGuiStyleVar_Alpha [UNUSED]
  ImGui::PopItemFlag   ( ); // ImGuiItemFlags_Disabled
}

void
SKIF_ImGui_PushDisabledSpacing (void)
{
  // Remove borders, paddings, and spacing
  ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize,  0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_ChildBorderSize,  0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_FrameRounding,    0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_ChildRounding,    0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_FramePadding,     ImVec2 (0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_IndentSpacing,    0.0f);
  ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing,      ImVec2 (0, 0));
  ImGui::PushStyleVar (ImGuiStyleVar_ItemInnerSpacing, ImVec2 (0, 0));
}

void
SKIF_ImGui_PopDisabledSpacing (void)
{
  ImGui::PopStyleVar  (8);
}

void
SKIF_ImGui_BlockDragMove (void)
{
  extern bool SKIF_MouseDragMoveAllowed;

  if (SKIF_ImGui_IsViewportFocused ( ))
    SKIF_MouseDragMoveAllowed = false;
}

void
SKIF_ImGui_BlockDragMoveWhenItemActive (void)
{
  extern bool SKIF_MouseDragMoveAllowed;

  if (ImGui::IsItemActive ( ))
    SKIF_MouseDragMoveAllowed = false;
}

// Allows moving the window but only in certain circumstances
bool
SKIF_ImGui_CanMouseDragMove (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  extern bool SKIF_MouseDragMoveAllowed;
  return    ! _registry._TouchDevice      &&           // Only if we are not on a touch input device
              SKIF_MouseDragMoveAllowed   &&           // Manually disabled by a few UI elements
            ! ImGui::IsAnyItemHovered ( ) &&           // Disabled if any item is hovered
          ( ! SKIF_ImGui_IsAnyPopupOpen ( )         || // Disabled if any popup is opened..
          (PopupMessageInfo    == PopupState_Opened || // ..except for a few standard ones which are
           PopupCategoryModify == PopupState_Opened)); //    aligned to the center of the app window
}

// Based on https://github.com/ocornut/imgui/issues/3379#issuecomment-1678718752
void
SKIF_ImGui_TouchScrollWhenDragging (bool only_on_void, SKIF_ImGuiAxis axis)
{
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* window = g.CurrentWindow;
  bool hovered = false;
  ImGuiID id = window->GetID("##scrolldraggingoverlay");
  ImGui::KeepAliveID(id);
  if (! only_on_void || g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
    ImGui::ButtonBehavior(window->Rect(), id, &hovered, &bAutoScrollActive, ImGuiMouseButton_Left);

  if (bAutoScrollActive)
  {
    ImVec2 delta = ImGui::GetIO().MouseDelta;

    if ((axis & SKIF_ImGuiAxis_X) && delta.x != 0.0f) // Horizontal
      ImGui::SetScrollX (window, window->Scroll.x - delta.x);

    if ((axis & SKIF_ImGuiAxis_Y) && delta.y != 0.0f) // Vertical
      ImGui::SetScrollY (window, window->Scroll.y - delta.y);
  }
}

void
SKIF_ImGui_MouseWheelScroll (SKIF_ImGuiAxis axis)
{
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* window = g.CurrentWindow;
  ImGuiID id = window->GetID("##scrollwheeloverlay");
  ImGui::KeepAliveID(id);

  static ImVec2 position;
  static bool held = false;
  static ImGuiID activeID = NULL;

  if (activeID == id && bAutoScrollActive && ! SKIF_ImGui_IsViewportFocused (g.CurrentViewport))
  {
    bAutoScrollActive = held = false;
    activeID = NULL;
  }

  else if (SKIF_ImGui_IsViewportFocused (g.CurrentViewport))
  {
    if (bAutoScrollActive &&
       (ImGui::GetKeyData (ImGuiKey_MouseLeft  )->DownDuration == 0.0f ||
        ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration == 0.0f ||
        ImGui::GetKeyData (ImGuiKey_MouseRight )->DownDuration == 0.0f))
      bAutoScrollActive = held = false;

    else if (bAutoScrollActive && held &&
        ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration < 0.0f)
      bAutoScrollActive = held = false;

    else if (bAutoScrollActive &&
        ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration > 0.15f) // Special handling if mouse wheel is held down for longer than 150 ms
      held = true;

  //else if (! onVoid || g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
  //else if (ImGui::IsMousePosValid ( ) && ImGui::ButtonBehavior  (window->Rect(), id, &hovered, &held, ImGuiButtonFlags_AllowOverlap | ImGuiButtonFlags_MouseButtonMiddle | static_cast<ImGuiButtonFlags_> (ImGuiButtonFlags_PressedOnClick)))
    else if (ImGui::IsMousePosValid ( ) && ImGui::GetKeyData (ImGuiKey_MouseMiddle)->DownDuration == 0.0f && ImGui::IsMouseHoveringRect (window->Rect().Min, window->Rect().Max) )
    {
      bAutoScrollActive = true;
      position = ImGui::GetMousePos ( );
      activeID = id;
    }

    if (bAutoScrollActive && activeID == id && ImGui::IsMousePosValid ( ))
    {
      ImVec2 delta = position - ImGui::GetMousePos ( );

      switch (axis)
      {
      case SKIF_ImGuiAxis_None:
        break;
      case SKIF_ImGuiAxis_X:
        ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeEW);
        break;
      case SKIF_ImGuiAxis_Y:
        ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeNS);
        break;
      case SKIF_ImGuiAxis_Both:
        ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeAll);
        break;
      default:
        break;
      }

      if ((axis & SKIF_ImGuiAxis_X) && delta.x != 0.0f) // Horizontal
        ImGui::SetScrollX (window, window->Scroll.x - delta.x * 0.1f);

      if ((axis & SKIF_ImGuiAxis_Y) && delta.y != 0.0f) // Vertical
        ImGui::SetScrollY (window, window->Scroll.y - delta.y * 0.1f);

      extern bool SKIF_MouseDragMoveAllowed;
      SKIF_MouseDragMoveAllowed = false;
    }
  }
}

void
SKIF_ImGui_AutoScroll (bool touch_only_on_void, SKIF_ImGuiAxis axis)
{
  if (axis == SKIF_ImGuiAxis_None)
    return;

  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // This disables drag-move on the list of games and should only be enabled on touch devices
  // Allows drag-scrolling using the left mouse button
  if (_registry._TouchDevice)
    SKIF_ImGui_TouchScrollWhenDragging (touch_only_on_void, axis);

  // This allows mouse wheel scroll (aka autoscroll) in non-touch mode
  else
    SKIF_ImGui_MouseWheelScroll (axis);
}

#pragma region Centered Column

static std::vector <ImGuiWindow*> s_centeredColumns; // The column windows currently being drawn (innermost last)
static bool                       bColumnTouchScrollActive = false;

void
SKIF_ImGui_CalcCenteredColumn (float area_width, float max_width, float* offset, float* width)
{
  const float scrollbar = ImGui::GetStyle ( ).ScrollbarSize;
  // The room kept free for the scrollbar on the right is mirrored on the left, so the column is always centered
  const float w         = std::max (1.0f, std::min (max_width, area_width - scrollbar * 2.0f));
  const float o         = std::max (0.0f, (area_width - w) / 2.0f);

  if (offset != nullptr) *offset = ImFloor (o);
  if (width  != nullptr) *width  = ImFloor (w);
}

void
SKIF_ImGui_BeginCenteredColumn (const char* str_id, float max_width)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow ( );

  // The width of the window's content area as if it had no vertical scrollbar
  const float area = window->Size.x - window->WindowPadding.x - ImGui::GetCursorPosX ( );

  float offset = 0.0f,
        width  = 0.0f;

  SKIF_ImGui_CalcCenteredColumn (area, max_width, &offset, &width);

  ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + offset);

  // The mouse wheel scrolls the window the column is in (NoScrollWithMouse forwards it to the parent)
  ImGui::BeginChild (str_id, ImVec2 (width, 0.0f),
                     ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened,
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);

  s_centeredColumns.push_back (ImGui::GetCurrentWindow ( ));
}

void
SKIF_ImGui_EndCenteredColumn (bool touch_scroll, bool touch_only_on_void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ImGuiContext& g      = *ImGui::GetCurrentContext ( );
  ImGuiWindow*  window = g.CurrentWindow;

  // Drag-scrolling on touch devices: SKIF_ImGui_AutoScroll ( ) only reacts while its own window is hovered, which
  //   it isn't over the column, so the same is done here for the window the column is in (with its own held state)
  if (touch_scroll && _registry._TouchDevice && window->ParentWindow != nullptr)
  {
    ImGuiWindow* parent  = window->ParentWindow;
    ImGuiID      id      = window->GetID ("##columnscrolldragging");
    bool         hovered = false;

    ImGui::KeepAliveID (id);

    if (! touch_only_on_void || g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
      ImGui::ButtonBehavior (window->Rect ( ), id, &hovered, &bColumnTouchScrollActive, ImGuiButtonFlags_MouseButtonLeft);

    if (bColumnTouchScrollActive)
    {
      const ImVec2 delta = ImGui::GetIO ( ).MouseDelta;

      if (delta.y != 0.0f)
        ImGui::SetScrollY (parent, parent->Scroll.y - delta.y);
    }
  }

  if (! s_centeredColumns.empty ( ))
    s_centeredColumns.pop_back ( );

  ImGui::EndChild ( );
}

float
SKIF_ImGui_GetColumnAvailHeight (void)
{
  ImGuiWindow* window = ImGui::GetCurrentWindow ( );

  // The column grows with its content, so what is left is measured against the visible area of the window it is in
  if (! s_centeredColumns.empty ( ) && s_centeredColumns.back ( ) == window && window->ParentWindow != nullptr)
  {
    ImGuiWindow* parent = window->ParentWindow;

    return parent->InnerRect.Max.y - parent->WindowPadding.y - window->DC.CursorPos.y;
  }

  return ImGui::GetContentRegionAvail ( ).y;
}

#pragma endregion

void
SKIF_ImGui_UpdateScrollbarState (void)
{
  if (ImGui::GetCurrentWindowRead() != NULL)
  {
    bScrollbarX = ImGui::GetCurrentWindowRead()->ScrollbarX;
    bScrollbarY = ImGui::GetCurrentWindowRead()->ScrollbarY;
  }

  else {
    bScrollbarX = false;
    bScrollbarY = false;
  }
}

bool
SKIF_ImGui_IsScrollbarX (void)
{
  return bScrollbarX;
}

bool
SKIF_ImGui_IsScrollbarY (void)
{
  return bScrollbarY;
}

void
SKIF_ImGui_InvalidateFonts (void)
{
  extern float SKIF_ImGui_FontSizeDefault;

  // Since ImGui 1.92 there is no atlas to build: glyphs are rasterized when first drawn, and the renderer backend
  //   creates and updates the atlas texture on request (so its device objects need not be recreated either)
  // The fonts are loaded at their base size, as the DPI scaling is applied per window through style.FontScaleDpi
  SKIF_ImGui_InitFonts (SKIF_ImGui_FontSizeDefault);
}

struct {
  ImDrawList*        drawList = nullptr;
  ImDrawListSplitter drawListSplitter;
  ImVec2             rowMin;
  float              width;
  ImU32              hoverCol;

  // Asserts
  int _Begin = 0,
      _Push  = 0;
} static _highlightLine;

// ImGui helper to highlight the full line of a list
// 
// Usage:
//        SKIF_ImGui_BeginHighlightLine ( );
//        for (auto& item : array)
//        {
//          SKIF_ImGui_PushHighlightLineLine ( );
//          [...]
//          SKIF_ImGui_PopHighlightLineLine  ( );
//        }
//        SKIF_ImGui_EndHighlightLine ( );
void
SKIF_ImGui_BeginHighlightLine (ImU32 hover_color)
{
  _highlightLine._Begin++;
  _highlightLine.drawList = ImGui::GetWindowDrawList();
  _highlightLine.drawListSplitter.Split(_highlightLine.drawList, 2); // 0 = background, 1 = foreground
  _highlightLine.hoverCol = hover_color;
}

void
SKIF_ImGui_PushHighlightLineLine (void)
{
  IM_ASSERT(_highlightLine._Begin == 1 && _highlightLine.drawList != nullptr);
  _highlightLine._Push++;

  // Row bounds: full width of the content area
  _highlightLine.rowMin = ImGui::GetCursorScreenPos    ( );
  _highlightLine.width  = ImGui::GetContentRegionAvail ( ).x;

  _highlightLine.drawListSplitter.SetCurrentChannel    (_highlightLine.drawList, 1); // foreground: row content
}

void
SKIF_ImGui_PopHighlightLineLine (void)
{
  IM_ASSERT(_highlightLine._Begin == 1 && _highlightLine._Push == 1 && _highlightLine.drawList != nullptr);
  _highlightLine._Push--;

  // Pad the rect by half the item spacing so adjacent rows touch
  float  pad =  ImGui::GetStyle ( ).ItemSpacing.y * 0.5f;
  ImVec2 rMin (_highlightLine.rowMin.x,  _highlightLine.rowMin.y - pad);
  ImVec2 rMax (_highlightLine.rowMin.x + _highlightLine.width, ImGui::GetItemRectMax ( ).y + pad);

  if (ImGui::IsMouseHoveringRect (rMin, rMax) && ImGui::IsWindowHovered ( ))
  {
    _highlightLine.drawListSplitter.SetCurrentChannel  (_highlightLine.drawList, 0); // background
    _highlightLine.drawList->AddRectFilled (rMin, rMax, _highlightLine.hoverCol);
  }
}

void
SKIF_ImGui_EndHighlightLine (void)
{
  IM_ASSERT(_highlightLine._Begin == 1 && _highlightLine.drawList != nullptr);
  _highlightLine._Begin--;
  _highlightLine.drawListSplitter.Merge (_highlightLine.drawList);
  _highlightLine.drawListSplitter.ClearFreeMemory ( );
}

void
SKIF_ImGui_CenterNextItem (const float item_width)
{
  float offset = (ImGui::GetContentRegionAvail ().x - item_width) * 0.5f;

  if (offset > 0.0f)
    ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);
}

// This helper function maps char to ImGuiKey_xxx
// For use with e.g. ImGui::GetKeyData ( )
ImGuiKey
SKIF_ImGui_CharToImGuiKey (char c)
{
  switch (c)
  {
    case ' ':  return ImGuiKey_Space;
    case '\'': return ImGuiKey_Apostrophe;
    case ',':  return ImGuiKey_Comma;
    case '-':  return ImGuiKey_Minus;
    case '.':  return ImGuiKey_Period;
    case '/':  return ImGuiKey_Slash;
  //case ':':  return ImGuiKey_Colon; // ???
    case ';':  return ImGuiKey_Semicolon;
    case '0':  return ImGuiKey_0;
    case '1':  return ImGuiKey_1;
    case '2':  return ImGuiKey_2;
    case '3':  return ImGuiKey_3;
    case '4':  return ImGuiKey_4;
    case '5':  return ImGuiKey_5;
    case '6':  return ImGuiKey_6;
    case '7':  return ImGuiKey_7;
    case '8':  return ImGuiKey_8;
    case '9':  return ImGuiKey_9;
    case 'A':  return ImGuiKey_A;
    case 'B':  return ImGuiKey_B;
    case 'C':  return ImGuiKey_C;
    case 'D':  return ImGuiKey_D;
    case 'E':  return ImGuiKey_E;
    case 'F':  return ImGuiKey_F;
    case 'G':  return ImGuiKey_G;
    case 'H':  return ImGuiKey_H;
    case 'I':  return ImGuiKey_I;
    case 'J':  return ImGuiKey_J;
    case 'K':  return ImGuiKey_K;
    case 'L':  return ImGuiKey_L;
    case 'M':  return ImGuiKey_M;
    case 'N':  return ImGuiKey_N;
    case 'O':  return ImGuiKey_O;
    case 'P':  return ImGuiKey_P;
    case 'Q':  return ImGuiKey_Q;
    case 'R':  return ImGuiKey_R;
    case 'S':  return ImGuiKey_S;
    case 'T':  return ImGuiKey_T;
    case 'U':  return ImGuiKey_U;
    case 'V':  return ImGuiKey_V;
    case 'W':  return ImGuiKey_W;
    case 'X':  return ImGuiKey_X;
    case 'Y':  return ImGuiKey_Y;
    case 'Z':  return ImGuiKey_Z;
    default:   return ImGuiKey_None;
  }
}

bool
SKIF_ImGui_IsViewportHDR (HWND hWnd)
{
  if (ImGuiViewport *vp = ImGui::FindViewportByPlatformHandle ((void *)hWnd))
  {
    extern bool
           SKIF_ImplDX11_ViewPort_IsHDR (ImGuiViewport* viewport);
    return SKIF_ImplDX11_ViewPort_IsHDR (vp);
  }

  return false;
}

bool
SKIF_ImGui_IsViewportHDRCapable (HWND hWnd)
{
  if (ImGuiViewport *vp = ImGui::FindViewportByPlatformHandle ((void *)hWnd))
  {
    extern bool
           SKIF_ImplDX11_ViewPort_IsHDRCapable (ImGuiViewport* viewport);
    return SKIF_ImplDX11_ViewPort_IsHDRCapable (vp);
  }

  return false;
}

// ---------------------------------------------------------------------------
//  April Fools' gag -- fake "nominal flow" activation theater
//
//  On April 1st (local time) it renders a dimmed, centered popup over the main
//  window for a few seconds -- faking the "missing nominal constant" / server-
//  activation dance -- then reveals the joke and dismisses on OK. Fires once
//  per launch.
//
//  The machine fingerprint shown is a throwaway hash of the local MachineGuid;
//  it is only drawn on screen, never stored or transmitted.
// ---------------------------------------------------------------------------
#define SKIF_APRIL_FOOLS_FORCE 0

bool
SKIF_ImGui_AprilFools_Process (void)
{
  static bool  bDismissed = false;  // Once dismissed, stay gone for this launch
  if (bDismissed)
    return false;

  // Decide once per launch whether the gag is armed
  static int   iArmed = -1;         // -1 = undecided, 0 = no, 1 = yes
  if (iArmed == -1)
  {
#if SKIF_APRIL_FOOLS_FORCE
    iArmed = 1;
#else
    SYSTEMTIME st = { };
    GetLocalTime (&st);
    iArmed = (st.wMonth == 4 && st.wDay == 1) ? 1 : 0;
#endif
  }

  if (iArmed == 0)
    return false;

  // Grab a real-looking machine fingerprint once (HKLM MachineGuid -> short hash)
  static std::string sFingerprint;
  if (sFingerprint.empty ())
  {
    wchar_t wszGuid [64] = { };
    DWORD   cbGuid       = sizeof (wszGuid);
    DWORD   hash         = 0x811C9DC5; // FNV-1a seed, purely decorative

    if (RegGetValueW (HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Cryptography",
                      L"MachineGuid", RRF_RT_REG_SZ, nullptr,
                      wszGuid, &cbGuid) == ERROR_SUCCESS)
    {
      for (wchar_t* p = wszGuid; *p != L'\0'; ++p)
      { hash ^= (DWORD) *p; hash *= 0x01000193; }
    }
    else
      hash ^= SKIF_Util_timeGetTime ( );

    char szFp [40] = { };
    snprintf (szFp, sizeof (szFp), "%08X-%04X-NOMINAL", hash, (unsigned)((hash >> 16) & 0xFFFF));
    sFingerprint = szFp;
  }

  // Timeline (ms since first displayed frame)
  static DWORD dwStart = SKIF_Util_timeGetTime ( ),
               dwLast  = dwStart;

  DWORD now = SKIF_Util_timeGetTime ( );

  if (! SKIF_ImGui_IsFocused ( ))
    dwStart += (now - dwLast);
  dwLast = now;

  DWORD elapsed = now - dwStart;

  const DWORD PHASE_DETECT  = 1400;  // "constant missing"
  const DWORD PHASE_CONTACT = 3200;  // "contacting activation server"
  const DWORD PHASE_RECOVER = 5600;  // "transforming / recovering constant"
  // >= PHASE_RECOVER -> reveal

  const ImVec4 colAccent = ImVec4 (1.00f, 0.70f, 0.23f, 1.0f); // amber
  const ImVec4 colWire   = ImVec4 (0.47f, 0.86f, 0.47f, 1.0f); // green

  float fWidth = 520.0f * SKIF_ImGui_GlobalDPIScale;

  // TRANSLATORS: An April Fools' Day joke (a fake activation of Special K, with technobabble), shown on April 1st
  if (SKIF_ImGui_BeginPopup (L_("Special K Activation###PopupAprilFools"), nullptr, ImVec2 (fWidth, 0.0f)))
  {
    const float fontScale        = ImGui::GetStyle ( ).FontScaleMain * ImGui::GetStyle ( ).FontScaleDpi;
    const float fontConsolasSize = (fontConsolas != nullptr && fontScale > 0.0f) ? ImFloor (fontConsolas->LegacySize * fontScale) / fontScale : 0.0f;

    ImGui::TreePush     ("AprilFoolsTreePush");
    SKIF_ImGui_Spacing  ( );

    ImGui::TextColored  (colAccent, "%s", _("Special K  //  Nominal Flow Activation"));
    ImGui::Separator    ( );
    SKIF_ImGui_Spacing  ( );

    ImGui::TextDisabled ("%s", _("Client fingerprint:"));
    ImGui::SameLine     ( );
    if (fontConsolas != nullptr) ImGui::PushFont (fontConsolas, fontConsolasSize);
    ImGui::Text         ("%s", sFingerprint.c_str ());
    if (fontConsolas != nullptr) ImGui::PopFont  ( );

    SKIF_ImGui_Spacing  ( );

    // Scrolling pseudo-hex "wire transcript", refreshed a few times a second
    static char  szHex [5][49] = { };
    static DWORD dwHexTick      = 0;
    static DWORD seed           = 0x5131F001;
    if (SKIF_Util_timeGetTime ( ) - dwHexTick > 70)
    {
      dwHexTick = SKIF_Util_timeGetTime ( );
      for (int r = 0; r < 4; ++r)
        memcpy (szHex [r], szHex [r + 1], sizeof (szHex [0]));
      char* row = szHex [4];
      for (int i = 0; i < 48; ++i)
      {
        seed = seed * 1103515245 + 12345;
        if ((i % 3) == 2) row [i] = ' ';
        else              row [i] = "0123456789ABCDEF" [(seed >> ((i & 7) + 8)) & 0xF];
      }
      row [48] = '\0';
    }

    if (fontConsolas != nullptr) ImGui::PushFont (fontConsolas, fontConsolasSize);
    for (int r = 0; r < 5; ++r)
      ImGui::TextColored (colWire, "  %s", szHex [r]);
    if (fontConsolas != nullptr) ImGui::PopFont  ( );

    SKIF_ImGui_Spacing  ( );

    const char* status = "";
    float       frac   = 0.0f;

    if (elapsed < PHASE_DETECT)
    {
      status = _("This title is missing its nominal constant (0x00000000).");
      frac   = 0.08f + 0.10f * ((float) elapsed / (float) PHASE_DETECT);
    }
    else if (elapsed < PHASE_CONTACT)
    {
      status = _("Contacting activation server  sk-activation.local ...");
      frac   = 0.20f + 0.35f * ((float)(elapsed - PHASE_DETECT) / (float)(PHASE_CONTACT - PHASE_DETECT));
    }
    else if (elapsed < PHASE_RECOVER)
    {
      status = _("Transforming constant against client fingerprint ...");
      frac   = 0.55f + 0.45f * ((float)(elapsed - PHASE_CONTACT) / (float)(PHASE_RECOVER - PHASE_CONTACT));
    }
    else
    {
      status = _("Nominal flow restored.");
      frac   = 1.0f;
    }

    ImGui::ProgressBar  (frac, ImVec2 (-30.0f, 0.0f));
    SKIF_ImGui_Spacing  ( );
    ImGui::TextWrapped  ("%s", status);

    SKIF_ImGui_Spacing  ( );
    SKIF_ImGui_Spacing  ( );

    if (elapsed >= PHASE_RECOVER)
    {
      ImGui::Separator   ( );
      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_TextColoredWrapped (colAccent,
                          _("Just kidding -- Special K needs no such thing. Happy April Fools' Day!"));
      SKIF_ImGui_Spacing ( );

      ImVec2 vButtonSize = ImVec2 (std::max (120.0f * SKIF_ImGui_GlobalDPIScale, ImGui::CalcTextSize (L_("Phew, OK"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f), 0.0f);
      ImGui::SetCursorPosX (fWidth / 2 - vButtonSize.x / 2);
      if (ImGui::Button  (L_("Phew, OK"), vButtonSize))
        bDismissed = true;

      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
    }
    else
    {
      ImGui::TextDisabled ("%s", _("Please wait, this will only take a moment ..."));
    }

    SKIF_ImGui_Spacing  ( );
    ImGui::TreePop      ( );
  }

  SKIF_ImGui_EndPopup ( );

  return true;
}

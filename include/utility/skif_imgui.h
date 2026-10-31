#pragma once

#include <SKIF.h>

#include <string>
#include <vector>

// This file is included mostly everywhere else, so lets define using ImGui's math operators here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

enum SKIF_ImGuiAxis {
  SKIF_ImGuiAxis_None,
  SKIF_ImGuiAxis_X,
  SKIF_ImGuiAxis_Y,

  SKIF_ImGuiAxis_Both =
     SKIF_ImGuiAxis_X | SKIF_ImGuiAxis_Y
};

// State for SKIF_ImGui_CheckboxTriState()
enum SKIF_ImGuiCheckState {
  SKIF_ImGuiCheckState_Auto = -1, // Preview only: next state in the Unchecked -> Mixed -> Checked -> Unchecked cycle
  SKIF_ImGuiCheckState_Unchecked,
  SKIF_ImGuiCheckState_Mixed,
  SKIF_ImGuiCheckState_Checked
};

typedef unsigned int DChoice;  // -> enum DChoice_

enum DChoice_
{
  // Generic
  DChoice_None     = 0,
  DChoice_Cancel   = 1 << 0,
  // Confirm
  DChoice_Yes      = 1 << 1,
  DChoice_No       = 1 << 2,
  // Save
  DChoice_Save     = 1 << 3,
  DChoice_DontSave = 1 << 4,
  // CPU bitness
  DChoice_32bit    = 1 << 5,
  DChoice_64bit    = 1 << 6,
  // Info
  DChoice_OK       = 1 << 7,
};

float    SKIF_ImGui_LinearTosRGB          (float col_lin);
ImVec4   SKIF_ImGui_LinearTosRGB          (ImVec4 col);
float    SKIF_ImGui_sRGBtoLinear          (float col_srgb);
ImVec4   SKIF_ImGui_sRGBtoLinear          (ImVec4 col);
void     SKIF_ImGui_StyleColorsDark       (ImGuiStyle* dst = nullptr);
void     SKIF_ImGui_StyleColorsLight      (ImGuiStyle* dst = nullptr);
void     SKIF_ImGui_AdjustAppModeSize     (HMONITOR monitor);
void     SKIF_ImGui_InfoMessage           (const std::string szTitle, const std::string szLabel);
void     SKIF_ImGui_CloseInfoPopup        (void);
bool     SKIF_ImGui_IsFocused             (void);
HWND     SKIF_ImGui_GetFocusedWindow      (void); // The SKIF window that has the keyboard focus, or NULL (thread-safe)
bool     SKIF_ImGui_IsViewportFocused     (const ImGuiViewport* vp = ImGui::GetWindowViewport ( ));
bool     SKIF_ImGui_IsMouseHovered        (void);
bool     SKIF_ImGui_IsAnyInputDown        (void);
bool     SKIF_ImGui_IsAnyPopupOpen        (void);
void     SKIF_ImGui_SetMouseCursorHand    (void);
void     SKIF_ImGui_SetHoverTip           (const std::string_view& szText, bool ignoreDisabledTooltips = false, bool allowWhenItemDisabled = false);
void     SKIF_ImGui_SetHoverText          (const std::string_view& szText, bool overrideExistingText = false);
bool     SKIF_ImGui_BeginChildFrame       (ImGuiID id, const ImVec2& size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags = ImGuiWindowFlags_None);
ImVec2   SKIF_ImGui_GetContentRegionMax       (void); // Same as ImGui::GetContentRegionMax ( ), removed in ImGui 1.91.0
ImVec2   SKIF_ImGui_GetWindowContentRegionMax (void); // Same as ImGui::GetWindowContentRegionMax ( ), removed in ImGui 1.91.0
void     SKIF_ImGui_BeginTabChildFrame    (void);
bool     SKIF_ImGui_BeginMenuEx2          (const char* label, const char* icon, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_Text), bool enabled = true);
bool     SKIF_ImGui_MenuItemEx2           (const char* label, const char* icon, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_Text), const char* shortcut = NULL, bool* p_selected = nullptr, bool enabled = true);
bool     SKIF_ImGui_IconButton            (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
bool     SKIF_ImGui_MinimizeButton        (ImGuiID id, const ImVec2& pos);
bool     SKIF_ImGui_TitleBarMinimizeButton(void);
bool     SKIF_ImGui_FrameButton           (const char* label, const ImVec2& size = ImVec2 (0, 0));
void     SKIF_ImGui_OptImage              (void* user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
void     SKIF_ImGui_Columns               (int columns_count, const char* id, bool border, bool resizeble = false);
void     SKIF_ImGui_Spacing               (float multiplier);
void     SKIF_ImGui_Spacing               (void);
bool     SKIF_ImGui_Selectable            (const char* label);
void     SKIF_ImGui_TextColoredWrapped    (const ImVec4& col, const char* text); // ImGui::TextColored ( ) that wraps (e.g. translated captions, which may be much longer than in English)
void     SKIF_ImGui_TextColoredEllipsis   (const ImVec4& col, const char* text); // ImGui::TextColored ( ) shortened with an ellipsis if it does not fit the rest of the line (full text in a tooltip)
// Layout helpers for text whose length is not known up front (translations)
float    SKIF_ImGui_CheckboxWidth         (const char* label);                   // Width of a checkbox or radio button with this label
bool     SKIF_ImGui_SameLineIfFits        (float width);                         // SameLine ( ) if an item of this width fits the rest of the line, else a new line; true if on the same line
bool     SKIF_ImGui_Checkbox              (const char* label, bool* v);          // ImGui::Checkbox ( ) whose label wraps if it is wider than the space left
bool     SKIF_ImGui_RadioButton           (const char* label, int* v, int v_button); // ImGui::RadioButton ( ) whose label wraps if it is wider than the space left
void     SKIF_ImGui_SetNextItemWidthForLabel (const char* label);                // Narrows the next item (e.g. a combo) so its label on the right fits
bool     SKIF_ImGui_SliderInt             (const char* label, int*   v, int   v_min, int   v_max, const char* format = "%d",   ImGuiSliderFlags flags = 0); // ImGui::SliderInt ( ), narrowed so its label fits; a label that still does not fit is shortened with an ellipsis (full text in a tooltip)
bool     SKIF_ImGui_SliderFloat           (const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0); // The same for ImGui::SliderFloat ( )
float    SKIF_ImGui_ComboWidth            (std::initializer_list <const char*> items, float min_width = 0.0f); // Width of a combo that fits the widest of these items (at least min_width)
void     SKIF_ImGui_TextHighlighted       (const char* text, const char* value, const ImVec4& colValue); // One line of text where {0} is value, drawn in colValue: "Do you want to end {0}?"
void     SKIF_ImGui_TextHighlighted       (const char* text, std::initializer_list <const char*> values, const ImVec4& colValue); // The same, with {0}, {1}, ...
int      SKIF_ImGui_TextRich              (const char* text, std::initializer_list <const char*> link_icons = { }, const ImVec4& colLink = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption)); // Wrapped text where [words] are links; returns the index of the clicked link, or -1
void     SKIF_ImGui_TextRichHighlight     (const char* text, std::initializer_list <const char*> icons = { }, const ImVec4& colHighlight = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption)); // Wrapped text where [words] are highlighted (not clickable)
std::string SKIF_ImGui_TextRichEscape     (const std::string& text);             // Doubles the brackets of text put in a [...] of the two above
bool     SKIF_ImGui_SelectableVAligned    (const char* unique_id, const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg);
bool     SKIF_ImGui_SeparatorTextClickable(const char* label, const bool* collapsed = nullptr, bool hand_cursor = false);
void     SKIF_ImGui_DimBackgroundViewport (const ImGuiViewport* vp = ImGui::GetWindowViewport ( ));
bool     SKIF_ImGui_BeginPopup            (const char* name, bool* p_open = nullptr, ImVec2 size = ImVec2(0, 0), ImGuiWindowFlags flags = ImGuiWindowFlags_None);
void     SKIF_ImGui_EndPopup              (void);
bool     SKIF_ImGui_AprilFools_Process    (void); // April Fools' gag: fake "nominal flow" activation theater (see skif_imgui.cpp)
DChoice  SKIF_ImGui_DialogPrompt          (const char* name, bool* p_open, const char* message = nullptr, const char* description = nullptr, std::vector<DChoice> choices = { DChoice_None });
DChoice  SKIF_ImGui_ConfirmPrompt         (const char* name, bool* p_open, const char* message = nullptr, const char* description = nullptr);
DChoice  SKIF_ImGui_SaveChangesPrompt     (const char* name, bool* p_open, const char* message = nullptr, const char* description = nullptr);
DChoice  SKIF_ImGui_InfoPrompt            (const char* name, bool* p_open, const char* message = nullptr, const char* description = nullptr);
DChoice  SKIF_ImGui_6432bitnessPrompt     (const char* name, bool* p_open, const char* message = nullptr, const char* description = nullptr);
bool     SKIF_ImGui_CheckboxHoverPreview  (const char* label, bool* v, float preview_alpha = 0.45f);
bool     SKIF_ImGui_CheckboxTriState      (const char* label, SKIF_ImGuiCheckState state, SKIF_ImGuiCheckState preview = SKIF_ImGuiCheckState_Auto, float preview_alpha = 0.45f);
void     SKIF_ImGui_ServiceMenu           (void);
ImFont*  SKIF_ImGui_LoadFont              (const std::wstring& filename, float point_size, ImFontConfig* cfg = nullptr);
void     SKIF_ImGui_InitFonts             (float fontSize, bool extendedCharsets = true);
bool     SKIF_ImGui_SetUILanguage         (const std::string& tag); // Font order and preloading for the UI language; true if the fonts need a rebuild
void     SKIF_ImGui_SetStyle              (ImGuiStyle* dst = nullptr);
void     SKIF_ImGui_BeginDisabled         (bool disabled = true);
void     SKIF_ImGui_EndDisabled           (bool disabled = true);
void     SKIF_ImGui_PushDisableState      (bool disabled = true);
void     SKIF_ImGui_PopDisableState       (bool disabled = true);
void     SKIF_ImGui_PushDisabledSpacing   (void);
void     SKIF_ImGui_PopDisabledSpacing    (void);
void     SKIF_ImGui_BlockDragMove         (void);       // Prevents SKIF from enabling drag move when the current window is focused
void     SKIF_ImGui_BlockDragMoveWhenItemActive (void); // Prevents SKIF from enabling drag move if the last item is active
bool     SKIF_ImGui_CanMouseDragMove      (void); // True if drag move using the mouse is allowed, false if not
void     SKIF_ImGui_AutoScroll            (bool touch_only_on_void, SKIF_ImGuiAxis axis);

// Centered content column: at most max_width wide (pass it DPI-scaled), centered in the current window, so content
//   doesn't stretch across wide windows. Its width and position don't depend on the window's vertical scrollbar, as
//   room for the scrollbar is always kept free at the right (and the same at the left): the content never moves when the scrollbar appears.
//   In a scrolling window (e.g. SKIF_ImGui_BeginTabChildFrame), the column grows with its content and the window scrolls.
void     SKIF_ImGui_CalcCenteredColumn    (float area_width, float max_width, float* offset, float* width); // For rows outside of a column that should line up with one
void     SKIF_ImGui_BeginCenteredColumn   (const char* str_id, float max_width);
void     SKIF_ImGui_EndCenteredColumn     (bool touch_scroll = false, bool touch_only_on_void = true); // touch_scroll: drag-scrolling the window on touch devices (SKIF_ImGui_AutoScroll) also works over the column
float    SKIF_ImGui_GetColumnAvailHeight  (void); // Height left in the visible area of the window the current column scrolls in (GetContentRegionAvail ( ).y outside of a column)
void     SKIF_ImGui_UpdateScrollbarState  (void); // Update the internal state tracking scrollbars
bool     SKIF_ImGui_IsScrollbarX          (void); // Helper function returning true if a horizontal scrollbar is visible
bool     SKIF_ImGui_IsScrollbarY          (void); // Helper function returning true if a vertical scrollbar is visible
void     SKIF_ImGui_InvalidateFonts       (void);
void     SKIF_ImGui_ApplyDPIScale         (float scale);               // Rescales the live style (sizes + FontScaleDpi) to a DPI scale
void     SKIF_ImGui_OnChangedViewport     (ImGuiViewport* viewport);   // ImGuiPlatformIO::Platform_OnChangedViewport: per-monitor DPI
void     SKIF_ImGui_RestoreMainDPIScale   (void);                      // Back to the main window's DPI scale (between frames)
float    SKIF_ImGui_GetDPIScaleForWindow  (const char* name);          // DPI scale for sizes set up before ImGui::Begin () of that window
void     SKIF_ImGui_BeginHighlightLine    (ImU32 hover_color = ImGui::GetColorU32 (ImGuiCol_SKIF_HighlightLineBg));
void     SKIF_ImGui_PushHighlightLineLine (void);
void     SKIF_ImGui_PopHighlightLineLine  (void);
void     SKIF_ImGui_EndHighlightLine      (void);
void     SKIF_ImGui_CenterNextItem        (const float item_width);
ImGuiKey SKIF_ImGui_CharToImGuiKey        (char c);

bool     SKIF_ImGui_IsViewportHDR         (HWND hWnd);
bool     SKIF_ImGui_IsViewportHDRCapable  (HWND hWnd);

// SKIF_ImGui_ImDerp, named as such as it is not a linear interpolation/lerp, is used
//   to among other things force 1.0f for the alpha color channel (w)
static ImVec4 SKIF_ImGui_ImDerp       (const ImVec4& a, const ImVec4& b, float t) { return ImVec4 (a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t), 1.0f /*a.w + (b.w - a.w) * t */); }

// Message popups
extern PopupState  PopupMessageInfo;  // App Mode: show an informational message box with text set through SKIF_ImGui_InfoMessage

// Fonts
extern ImFont* fontConsolas;

// Filter history: the latest searches made in a filter field, shown as a list right below the field while it is selected
//
//   - Clicking an entry, or stepping to it with Up/Down (keyboard, gamepad D-pad or left stick), puts it in the field
//   - Up/Down only steps through the history while the list is shown; once a new search is typed the list hides
//       and Up/Down navigates as usual again (e.g. on to the list being filtered)
//   - A search is added once the user is done with the field (Enter, clicking or navigating elsewhere, picking an entry);
//       Escape reverts the field, so it is not added. Searches differing only in case count as the same one
//
//   The entries are kept in a vector owned by the caller (so several fields can share one history, e.g. through
//     SKIF_RegistrySettings), and are passed to the save function whenever they change (e.g. to store them in the registry)
//
//   Usage:
//     history.InputText ("###Filter", buf, buf_size, size, flags); // Instead of ImGui::InputTextEx ( ); the field remains the last item
//     ...                                                           // Content the list should be drawn on top of
//     history.DrawList  (buf, buf_size);                           // The list (if shown), right below the field
class SKIF_ImGui_FilterHistory
{
public:
  using SaveFn = void (*) (const std::vector <std::wstring>& entries);

  SKIF_ImGui_FilterHistory (std::vector <std::string>* entries, SaveFn save, size_t max_entries = 5)
    : _entries (entries), _save (save), _max (max_entries) { }

  bool InputText (const char* label, char* buf, size_t buf_size, const ImVec2& size_arg = ImVec2 (0, 0), ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
  bool DrawList  (char* buf, size_t buf_size, bool restore_cursor = false); // True if an entry was picked (buf is updated). restore_cursor: only if more items are submitted afterwards
  void Add       (const std::string& filter);                               // Moves (or adds) a search to the top
  bool Contains  (const char* text) const;

private:
  static int  _Callback (ImGuiInputTextCallbackData* data);

  std::vector <std::string>* _entries     = nullptr;
  SaveFn                     _save        = nullptr;
  size_t                     _max         = 5;

  std::string                _onActivate;           // Contents of the field when it was last activated
  int                        _pos         = -1;     // Entry picked using Up/Down (-1 = none)
  int                        _step        =  0;     // Pending step requested by a gamepad (-1 = up, 1 = down)
  bool                       _hovered     = false;  // Whether the mouse was over the list last frame
  bool                       _visible     = false;  // Whether the list was shown last frame
  bool                       _fieldActive = false;
  ImVec2                     _fieldMin, _fieldMax;
};

#pragma once

#include <SKIF.h>

// This file is included mostly everywhere else, so lets define using ImGui's math operators here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

void    SKIF_ImGui_StyleColorsDark    (ImGuiStyle* dst = nullptr);
bool    SKIF_ImGui_IsFocused          (void);
bool    SKIF_ImGui_IsMouseHovered     (void);
bool    SKIF_ImGui_IsAnyInputDown     (void);
void    SKIF_ImGui_SetMouseCursorHand (void);
void    SKIF_ImGui_SetHoverTip        (const std::string_view& szText, bool ignoreDisabledTooltips = false);
void    SKIF_ImGui_SetHoverText       (const std::string_view& szText, bool overrideExistingText = false);
bool    SKIF_ImGui_BeginChildFrame    (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags);
void    SKIF_ImGui_BeginTabChildFrame (void);
bool    SKIF_ImGui_IconButton         (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
void    SKIF_ImGui_OptImage           (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
void    SKIF_ImGui_Columns            (int columns_count, const char* id, bool border, bool resizeble = false);
void    SKIF_ImGui_Spacing            (float multiplier);
void    SKIF_ImGui_Spacing            (void);
void    SKIF_ImGui_ServiceMenu        (void);
ImFont* SKIF_ImGui_LoadFont           (const std::wstring& filename, float point_size, const ImWchar* glyph_range, ImFontConfig* cfg = nullptr);
void    SKIF_ImGui_InitFonts          (float fontSize, bool extendedCharsets = true);
void    SKIF_ImGui_SetStyle           (ImGuiStyle* dst = nullptr);
void    SKIF_ImGui_PushDisableState   (void);
void    SKIF_ImGui_PopDisableState    (void);
void    SKIF_ImGui_DisallowWindowMove (void);
bool    SKIF_ImGui_GetWindowModeState (void);
void    SKIF_ImGui_InvalidateFonts    (void);

// SKIF_ImGui_ImDerp, named as such as it is not a linear interpolation/lerp, is used
//   to among other things force 1.0f for the alpha color channel (w)
static ImVec4 SKIF_ImGui_ImDerp       (const ImVec4& a, const ImVec4& b, float t) { return ImVec4 (a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t), 1.0f /*a.w + (b.w - a.w) * t */); }

// Fonts
extern bool SKIF_bFontChineseSimplified,
            SKIF_bFontChineseAll,
            SKIF_bFontCyrillic,
            SKIF_bFontJapanese,
            SKIF_bFontKorean,
            SKIF_bFontThai,
            SKIF_bFontVietnamese;

extern ImFont* fontConsolas;
extern ImFont* fontFAS;
extern ImFont* fontFAR;
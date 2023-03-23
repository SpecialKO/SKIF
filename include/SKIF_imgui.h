#pragma once

#include <SKIF.h>

// This file is included mostly everywhere else, so lets define using ImGui's math operators here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

void SKIF_ImGui_StyleColorsDark    (ImGuiStyle* dst = nullptr);
bool SKIF_ImGui_IsFocused          (void);
bool SKIF_ImGui_IsMouseHovered     (void);
bool SKIF_ImGui_IsAnyInputDown     (void);
void SKIF_ImGui_SetMouseCursorHand (bool allow_overlap = false);
void SKIF_ImGui_SetHoverTip        (const std::string_view& szText);
void SKIF_ImGui_SetHoverText       (const std::string_view& szText, bool overrideExistingText = false);
bool SKIF_ImGui_BeginChildFrame    (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags);
void SKIF_ImGui_BeginTabChildFrame (void);
bool SKIF_ImGui_IconButton         (ImGuiID id, std::string icon, std::string label, const ImVec4& colIcon = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
void SKIF_ImGui_OptImage           (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
void SKIF_ImGui_Columns            (int columns_count, const char* id, bool border, bool resizeble = false);
void SKIF_ImGui_Spacing            (float multiplier = 0.25f);
void SKIF_ImGui_ServiceMenu        (void);
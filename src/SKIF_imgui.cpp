#include <SKIF_imgui.h>
#include <SKIF_utility.h>

void SKIF_ImGui_StyleColorsDark (ImGuiStyle* dst)
{
    ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    // Text
    colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.30f); //ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    // Window, Child, Popup
    colors[ImGuiCol_WindowBg]               = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.90f);

    // Borders
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    // Frame [Checkboxes, Radioboxes]
    colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.20f, 0.75f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    // Title Background [Popups]
    colors[ImGuiCol_TitleBg]                = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 0.85f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // Unchanged

    // MenuBar
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

    // CheckMark
    colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Slider [UNUSED]
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

    // Buttons
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);

    // Headers [Selectables, CollapsibleHeaders]
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.20f, 0.20f, 0.20f, 0.75f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab]          = colors[ImGuiCol_Header];        //ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = colors[ImGuiCol_HeaderHovered]; //ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = colors[ImGuiCol_HeaderActive];  //ImVec4(0.51f, 0.51f, 0.51f, 1.00f);

    // Separators
    if (SKIF_bDisableBorders)
      colors[ImGuiCol_Separator]            = colors[ImGuiCol_WindowBg];
    else
      colors[ImGuiCol_Separator]            = colors[ImGuiCol_Border];


    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);

    // Resize Grip
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);

    // Tabs
    colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);       //ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.80f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);

    if (SKIF_bDisableBorders)
      colors[ImGuiCol_TabActive]            = colors[ImGuiCol_WindowBg];
    else
      colors[ImGuiCol_TabActive]            = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);       //ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);

    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

    /* Previous:
    colors[ImGuiCol_Tab]                    = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);       //ImLerp(colors[ImGuiCol_Header],       colors[ImGuiCol_TitleBgActive], 0.80f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);       //ImLerp(colors[ImGuiCol_HeaderActive], colors[ImGuiCol_TitleBgActive], 0.60f);
    colors[ImGuiCol_TabUnfocused]           = ImLerp(colors[ImGuiCol_Tab],          colors[ImGuiCol_TitleBg], 0.80f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImLerp(colors[ImGuiCol_TabActive],    colors[ImGuiCol_TitleBg], 0.40f);
    */

    // Docking stuff
    colors[ImGuiCol_DockingPreview]         = colors[ImGuiCol_HeaderActive] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    // Plot
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

    // 
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);

    // 
    colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 1.00f, 1.00f, 0.70f); //ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
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
    colors[ImGuiCol_SKIF_Info]              = colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_SKIF_Yellow]            = ImColor::HSV(0.11F, 1.F, 1.F);
}

bool
SKIF_ImGui_IsFocused (void)
{
  extern bool SKIF_ImGui_ImplWin32_IsFocused (void);
  return SKIF_ImGui_ImplWin32_IsFocused ( );
}

bool
SKIF_ImGui_IsHoverable (void)
{
  if (! SKIF_ImGui_IsFocused ())
    return false;

  return true;
}

void
SKIF_ImGui_SetMouseCursorHand (bool allow_overlap)
{
  // It is not clear what purpose IsMouseHoveringRect() was used for here, but it was implemented back in January 2022,
  //  see the following commit https://github.com/SpecialKO/SKIF/commit/a2eac724aad8f4295dcc1679a7e6e82ce4926c91
  // "allow_overlap" is used to allow that behaviour in the specific scenarios it was intended for (if we ever figure that out)
  if ((allow_overlap && ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())) ||
      ImGui::IsItemHovered ( ) )
  {
    ImGui::SetMouseCursor (
      ImGuiMouseCursor_Hand
    );
  }
}

void
SKIF_ImGui_SetHoverTip (const std::string_view& szText)
{
  extern bool        SKIF_bSmallMode;
  extern bool        SKIF_bDisableTooltips;
  extern bool        HoverTipActive;
  extern DWORD       HoverTipDuration;
  extern std::string SKIF_StatusBarText;

  if ( SKIF_ImGui_IsHoverable () && (! SKIF_bSmallMode) )
  {
    if (ImGui::IsItemHovered ())
    {
      if (! SKIF_bDisableTooltips)
      {
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
        HoverTipActive = true;

        if ( HoverTipDuration == 0)
          HoverTipDuration = SKIF_Util_timeGetTime ( );

        else if ( HoverTipDuration + 500 < SKIF_Util_timeGetTime() )
          ImGui::SetTooltip (
            "%hs", szText.data ()
          );

        ImGui::PopStyleColor  ();
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
}

void
SKIF_ImGui_SetHoverText ( const std::string_view& szText,
                                bool  overrideExistingText )
{
  extern bool SKIF_bSmallMode;
  extern std::string SKIF_StatusBarHelp;

  if ( ImGui::IsItemHovered ()                                  &&
        ( overrideExistingText || SKIF_StatusBarHelp.empty () ) &&
        (                       ! SKIF_bSmallMode             )
     )
  {
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
  ImGuiWindow* window = ImGui::GetCurrentWindow();
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

void SKIF_ImGui_BeginTabChildFrame (void)
{
  auto frame_content_area_id =
    ImGui::GetID ("###SKIF_Content_Area");

  SKIF_ImGui_BeginChildFrame (
    frame_content_area_id,
      ImVec2 (   0.0f,
               900.0f * SKIF_ImGui_GlobalDPIScale ),
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
  if (ServiceMenu == PopupState::Open)
  {
    ImGui::OpenPopup ("ServiceMenu");
    ServiceMenu = PopupState::Closed;
  }

  if (ImGui::BeginPopup ("ServiceMenu"))
  {
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
        "Troubleshooting:"
    );

    ImGui::Separator ( );

    extern bool SKIF_bStopOnInjection;

    if (ImGui::Selectable("Force Start Service"))
      _inject._StartStopInject (false, SKIF_bStopOnInjection);

    if (ImGui::Selectable("Force Stop Service"))
      _inject._StartStopInject (true);

    ImGui::EndPopup ( );
  }
}
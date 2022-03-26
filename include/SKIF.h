//
// Copyright 2019-2021 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#pragma once

#include "../resource.h"

#include <combaseapi.h>
#include <comdef.h>

#include <string>
#include <string_view>

// This file is included mostly everywhere else, so lets define using ImGui's math operators here.
#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

class SK_AutoCOMInit
{
public:
  SK_AutoCOMInit (DWORD dwCoInit = COINIT_MULTITHREADED) :
           init_flags_ (dwCoInit)
  {
    //if (_assert_not_dllmain ())
    {
      const HRESULT hr =
        CoInitializeEx (nullptr, init_flags_);

      if (SUCCEEDED (hr))
        success_ = true;
      else
        init_flags_ = ~init_flags_;
    }
  }

  ~SK_AutoCOMInit (void) noexcept
  {
    if (success_)
      CoUninitialize ();
  }

  bool  isInit       (void) noexcept { return success_;    }
  DWORD getInitFlags (void) noexcept { return init_flags_; }

protected:
  //static bool _assert_not_dllmain (void);

private:
  DWORD init_flags_ = COINIT_MULTITHREADED;
  bool  success_    = false;
};

extern HMODULE hModSKIF;
extern HMODULE hModSpecialK;
void SKIF_Initialize               (void);

bool SKIF_ImGui_IsHoverable        (void);
void SKIF_ImGui_SetMouseCursorHand (void);
void SKIF_ImGui_SetHoverTip        (const std::string_view& szText);
void SKIF_ImGui_SetHoverText       (const std::string_view& szText, bool overrideExistingText = false);
bool SKIF_ImGui_BeginChildFrame    (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags);
void SKIF_ImGui_OptImage           (ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
void SKIF_ImGui_Columns            (int columns_count, const char* id, bool border, bool resizeble = false);
void SKIF_ImGui_Spacing            (float multiplier = 0.25f);

extern float SKIF_ImGui_GlobalDPIScale;
extern float SKIF_ImGui_GlobalDPIScale_Last;

void SKIF_UI_DrawPlatformStatus    (void);
void SKIF_UI_DrawComponentVersion  (void);

void  SKIF_SetHDRWhiteLuma    (float fLuma);
FLOAT SKIF_GetHDRWhiteLuma    (void);
FLOAT SKIF_GetMaxHDRLuminance (bool bAllowLocalRange);
BOOL  SKIF_IsHDR              (void);

extern float fAspect;
extern float fBottomDist;

using  CreateDXGIFactory1_pfn = HRESULT (WINAPI *)(REFIID riid, _COM_Outptr_ void **ppFactory);
extern CreateDXGIFactory1_pfn
  SKIF_CreateDXGIFactory1;

const UINT_PTR IDT_REFRESH_ONDEMAND = 1337;
const UINT_PTR IDT_REFRESH_PENDING  = 1338;
const UINT_PTR IDT_REFRESH_DEBUG    = 1339;
const UINT_PTR IDT_REFRESH_GAMES    = 1340;

enum class PopupState {
  Closed,
  Open,
  Opened
};

extern PopupState IconMenu;
extern PopupState ServiceMenu;

extern PopupState AddGamePopup;
extern PopupState RemoveGamePopup;
extern PopupState ModifyGamePopup;
extern PopupState ConfirmPopup;

enum UITab {
  None,
  Injection,
  Settings,
  Help,
  Debug
};

extern UITab SKIF_Tab_Selected;
extern UITab SKIF_Tab_ChangeTo;
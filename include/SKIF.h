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
void SKIF_Initialize                (void);
std::wstring SKIF_GetLastError      (void);

BOOL SKIF_IsWindows8Point1OrGreater (void);
BOOL SKIF_IsWindows10OrGreater      (void);
BOOL SKIF_IsWindowsVersionOrGreater (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);

bool SKIF_ImGui_IsHoverable        (void);
void SKIF_ImGui_SetMouseCursorHand (void);
void SKIF_ImGui_SetHoverTip        (const std::string_view& szText);
void SKIF_ImGui_SetHoverText       (const std::string_view& szText, bool overrideExistingText = false);
void SKIF_ImGui_Spacing            (float multiplier = 0.25f);
bool SKIF_ImGui_BeginChildFrame    (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags);

void SKIF_UI_DrawPlatformStatus    (void);

void  SKIF_SetHDRWhiteLuma    (float fLuma);
FLOAT SKIF_GetHDRWhiteLuma    (void);
FLOAT SKIF_GetMaxHDRLuminance (bool bAllowLocalRange);
BOOL  SKIF_IsHDR              (void);

HINSTANCE SKIF_Util_OpenURI     (const std::wstring_view& path, DWORD dwAction = SW_SHOWNORMAL, LPCWSTR verb = L"OPEN");
HINSTANCE SKIF_Util_ExplorePath (const std::wstring_view& path);

HINSTANCE SKIF_Util_ExplorePath_Formatted (                const wchar_t* const wszFmt, ...);
HINSTANCE SKIF_Util_OpenURI_Formatted     (DWORD dwAction, const wchar_t* const wszFmt, ...);
void      SKIF_Util_OpenURI_Threaded      (                const LPCWSTR path);

extern float fAspect;
extern float fBottomDist;

using  CreateDXGIFactory1_pfn = HRESULT (WINAPI *)(REFIID riid, _COM_Outptr_ void **ppFactory);
extern CreateDXGIFactory1_pfn
  SKIF_CreateDXGIFactory1;

DWORD   SKIF_timeGetTime   (void);
DWORD64 SKIF_timeGetTimeEx (void);

const UINT_PTR IDT_REFRESH_ONDEMAND = 1337;
const UINT_PTR IDT_REFRESH_PENDING  = 1338;
const UINT_PTR IDT_REFRESH_DEBUG    = 1339;
const UINT_PTR IDT_REFRESH_GAMES    = 1340;

void ResolveIt  (HWND hwnd, LPCSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize);
bool CreateLink (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs = L"\0", LPCWSTR lpszWorkDir = L"\0", LPCWSTR lpszDesc = L"\0", LPCWSTR lpszIconLocation = L"\0", int iIcon = 0);

struct skif_directory_watch_s
{
  ~skif_directory_watch_s (void);

  bool isSignaled (std::wstring path);

  HANDLE hChangeNotification = INVALID_HANDLE_VALUE;
};

enum class PopupState {
  Closed,
  Open,
  Opened
};

extern PopupState ServiceMenu;

extern PopupState AddGamePopup;
extern PopupState RemoveGamePopup;
extern PopupState ModifyGamePopup;
extern PopupState ConfirmPopup;
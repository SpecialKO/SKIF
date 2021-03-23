#pragma once

#include "resource.h"

#include <combaseapi.h>

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

BOOL SKIF_IsWindows8Point1OrGreater (void);
BOOL SKIF_IsWindows10OrGreater      (void);

bool SKIF_ImGui_IsHoverable  (void);
void SKIF_ImGui_SetMouseCursorHand (void);
void SKIF_ImGui_SetHoverTip  (const char* szText);
void SKIF_ImGui_SetHoverText (const char* szText);

void  SKIF_SetHDRWhiteLuma    (float fLuma);
FLOAT SKIF_GetHDRWhiteLuma    (void);
FLOAT SKIF_GetMaxHDRLuminance (bool bAllowLocalRange);
BOOL  SKIF_IsHDR              (void);

extern float sk_global_ctl_x;
extern float fAspect;
extern float fBottomDist;

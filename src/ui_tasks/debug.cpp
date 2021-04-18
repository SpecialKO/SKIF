//
// Copyright 2021 Andon "Kaldaien" Coleman
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

#include <wmsdk.h>
#include <filesystem>

#include <sk_utility/utility.h>

#define MAX_INJECTED_PROCS        16
#define MAX_INJECTED_PROC_HISTORY 64

enum class SK_RenderAPI
{
  Reserved  = 0x0001u,

  // Native API Implementations
  OpenGL    = 0x0002u,
  Vulkan    = 0x0004u,
  D3D9      = 0x0008u,
  D3D9Ex    = 0x0018u,
  D3D10     = 0x0020u, // Don't care
  D3D11     = 0x0040u,
  D3D12     = 0x0080u,

  // These aren't native, but we need the bitmask anyway
  D3D8      = 0x2000u,
  DDraw     = 0x4000u,
  Glide     = 0x8000u,

  // Wrapped APIs (D3D12 Flavor)
//D3D11On12 = 0x00C0u,

  // Wrapped APIs (D3D11 Flavor)
  D3D8On11  = 0x2040u,
  DDrawOn11 = 0x4040u,
  GlideOn11 = 0x8040u,
};

struct SK_InjectionRecord_s
{
  struct {
    wchar_t    name [MAX_PATH + 2] =  { 0 };
    DWORD      id                  =    0;
    __time64_t inject              = 0ULL;
    __time64_t eject               = 0ULL;
    bool       crashed             = false;
  } process;

  struct {
    SK_RenderAPI api    = SK_RenderAPI::Reserved;
    ULONG64      frames = 0ULL;
  } render;

  // Use a bitmask instead of this stupidness
  struct {
    bool xinput       = false;
    bool raw_input    = false;
    bool direct_input = false;
    bool hid          = false;
    bool steam        = false;
  } input;

  ///static __declspec (dllexport) volatile LONG count;
  ///static __declspec (dllexport) volatile LONG rollovers;
};

using SKX_GetInjectedPIDs_pfn = size_t                (__stdcall *)(DWORD *pdwList, size_t capacity);
using SK_Inject_GetRecord_pfn = SK_InjectionRecord_s* (__stdcall *)(int idx);

#include <imgui/imgui.h>

FILE* fStdErr = nullptr;
FILE* fStdIn  = nullptr;
FILE* fStdOut = nullptr;

void
SKIF_SpawnConsole (void)
{
  AllocConsole ();

  static volatile LONG init = FALSE;

  if (! InterlockedCompareExchange (&init, 1, 0))
  {
    fStdIn  = _wfreopen (L"CONIN$",  L"r", stdin);
    fStdOut = _wfreopen (L"CONOUT$", L"w", stdout);
    fStdErr = _wfreopen (L"CONOUT$", L"w", stderr);
  }
}

BOOL
SKIF_CloseConsole (void)
{
  return
    FreeConsole ();
}

std::string   SKIF_PresentDebugStr [2];
volatile LONG SKIF_PresentIdx    =   0;

HRESULT
SKIF_Debug_DrawUI (void)
{
  HMODULE hModSK64 =
    LoadLibraryW (L"SpecialK64.dll");

  if (! hModSK64)
    return E_NOT_VALID_STATE;
  
   SKX_GetInjectedPIDs_pfn
   SKX_GetInjectedPIDs     =
  (SKX_GetInjectedPIDs_pfn)GetProcAddress (hModSK64,
  "SKX_GetInjectedPIDs");
   
   SK_Inject_GetRecord_pfn
   SK_Inject_GetRecord     =
  (SK_Inject_GetRecord_pfn)GetProcAddress (hModSK64,
  "SK_Inject_GetRecord");

  if (SKX_GetInjectedPIDs != nullptr)
  {
    static           DWORD dwPIDs [MAX_INJECTED_PROCS] = { };
    size_t num_pids =
      SKX_GetInjectedPIDs (dwPIDs, MAX_INJECTED_PROCS);
  
    while (num_pids > 0)
    {
      DWORD dwPID = dwPIDs [--num_pids];

      static DWORD dwMonitored = 0;

      ImGui::Text ("PID %x", dwPID);

      if (ImGui::IsItemClicked ())
      {
        static HANDLE hPresentMonThread = 0;

        if (hPresentMonThread != 0)
        {
          TerminateThread (hPresentMonThread, 0x0);
        }

        HWND hWndFocus = GetFocus ();

        SetFocus        (hWndFocus);
        SetActiveWindow (hWndFocus);

        dwMonitored = dwPID;

        hPresentMonThread =
        CreateThread ( nullptr, 0x0, [](LPVOID lpUser)-> DWORD
          {
            extern int SKIF_PresentMon_Main (int argc, char **argv);

            std::string pid =
              std::to_string (PtrToUlong (lpUser));

            //std::string args =
            //  SK_FormatString ("\"-no_csv -process_id %lu -verbose -stop_existing_session\"", PtrToUlong (lpUser));

            const char* argv_ [] = {
              "SKIF.exe", "-no_csv", "-process_id", pid.c_str (), /*"-verbose",*/ "-stop_existing_session"
            };


            SKIF_PresentMon_Main (sizeof (argv_) / sizeof (const char *), (char **)argv_);

            
            return 0;
          }, (LPVOID)ULongToPtr (dwPID), 0x0, nullptr
        );
      }

      if (dwMonitored == dwPID)
      {
        ImGui::TextWrapped ("%hs", SKIF_PresentDebugStr [ReadAcquire (&SKIF_PresentIdx)].c_str ());
      }
    }
  
    if (SK_Inject_GetRecord != nullptr)
    {
      ImGui::Separator ();
      
      for ( int idx = 0 ; idx < MAX_INJECTED_PROC_HISTORY ; ++idx )
      {
        SK_InjectionRecord_s* pInjectRecord =
          SK_Inject_GetRecord (idx);

        if (pInjectRecord->process.id != 0x0)
        {
          ImGui::TextColored ( pInjectRecord->process.crashed       ?
                                 ImColor (1.f, 0.f, 0.f, 1.f).Value :
                                 ImColor (0.f, 1.f, 0.f, 1.f).Value,
            " 64-Bit Process [%x]:  \"%ws\"  -  "
                                     "%llu Frames Drawn (API=%x)",
            pInjectRecord->process.id,
            pInjectRecord->process.name, pInjectRecord->render.frames,
                                         pInjectRecord->render.api
                             );
        }
        //ImGui::Text ("Count: %lu", ReadAcquire (&pInjectRecord->count));
      }
    }
  }

  if (hModSK64 != 0)
    FreeLibrary (hModSK64);

  return
    S_OK;
}
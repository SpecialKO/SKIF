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

#include <SKIF.h>

#include <wmsdk.h>
#include <filesystem>

#include <font_awesome.h>

#include <sk_utility/utility.h>
#include <psapi.h>
#include <appmodel.h>

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

#define MAX_INJECTED_PROCS        32
#define MAX_INJECTED_PROC_HISTORY 128
#define INJECTION_RECORD_ABI_VER  "1.2"

extern "C"
{
struct SK_InjectionRecord_s
{
  struct {
    wchar_t      name  [MAX_PATH + 2] = { 0 };
    DWORD        id                   =   0  ;
    __time64_t   inject               =  0ULL;
    __time64_t   eject                =  0ULL;
    bool         crashed              = false;
    wchar_t      win_title [     128] = { 0 };
  } process;

  struct {
    ULONG64      frames                    =  0ULL;
    SK_RenderAPI api                       = SK_RenderAPI::Reserved;
    bool         fullscreen                = false;
    bool         dpi_aware                 = false;
  } render;

  // Use a bitmask instead of this stupidness
  struct {
    bool         xinput                    = false;
    bool         raw_input                 = false;
    bool         direct_input              = false;
    bool         hid                       = false;
    bool         steam                     = false;
  } input;

  struct {
    uint32_t     steam_appid     =  0 ;
    wchar_t      uwp_full_name [
             PACKAGE_FULL_NAME_MAX_LENGTH
                               ] = { };
    // Others?
  } platform;

  static volatile LONG count;
  static volatile LONG rollovers;
};
};

using SKX_GetInjectedPIDs_pfn   = size_t                (__stdcall *)(DWORD *pdwList, size_t capacity);
using SK_Inject_GetRecord_pfn   = SK_InjectionRecord_s* (__stdcall *)(DWORD  dwPid);
using SK_Inject_AuditRecord_pfn = HRESULT               (__stdcall *)(DWORD  dwPid, SK_InjectionRecord_s* pData, size_t cbSize);

#include <imgui/imgui.h>

BOOL
SKIF_File_GetNameFromHandle ( HANDLE   hFile,
                              wchar_t *pwszFileName,
                        const DWORD    uiMaxLen )
{
  if (uiMaxLen == 0 || pwszFileName == nullptr)
    return FALSE;

  *pwszFileName = L'\0';

  std::vector <uint8_t> fni_buffer (
    sizeof (FILE_NAME_INFO) + sizeof (wchar_t) * _MAX_PATH
  );

  FILE_NAME_INFO* pFni =
    (FILE_NAME_INFO *)fni_buffer.data ();

  pFni->FileNameLength = _MAX_PATH * sizeof (pFni->FileName [0]);

  const BOOL success =
    GetFileInformationByHandleEx ( hFile,
                                     FileNameInfo,
                                       pFni,
                                         sizeof (FILE_NAME_INFO) +
                            (_MAX_PATH * sizeof (wchar_t)) );

  if (success && pFni != nullptr)
  {
    wcsncpy_s (
      pwszFileName,
        std::min ( uiMaxLen,
                    (pFni->FileNameLength /
      (DWORD)sizeof (pFni->FileName [0]))  + 1
                 ),  pFni->FileName,
                      _TRUNCATE
    );
  }

  else
    return FALSE;

  return
    success;
}

#include <iostream>
#include <Windows.h>

#include <set>
#include <map>
#include <unordered_map>
#include <stack>

#pragma pack (push,8)
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004
#define SystemHandleInformationSize 0x200000

static HMODULE _NtDll =
  GetModuleHandle (L"NtDll");

#define SKIF_NameOf(x) #x
#define SKIF_GetNtDllProcAddress(proc) GetProcAddress(_NtDll,proc)

#include <typeindex>

template <typename _T>
__declspec (noinline)
void SKIF_ImportFromNtDll_Impl (_T& pfn, const char* name)
{
  pfn =
    reinterpret_cast <typename _T> (
      SKIF_GetNtDllProcAddress (name)
    );
}

#define SKIF_ImportFromNtDll(proc) \
 SKIF_ImportFromNtDll_Impl(  proc,#proc )

enum SYSTEM_INFORMATION_CLASS
{ SystemBasicInformation                = 0,
  SystemPerformanceInformation          = 2,
  SystemTimeOfDayInformation            = 3,
  SystemProcessInformation              = 5,
  SystemProcessorPerformanceInformation = 8,
  SystemHandleInformation               = 16, // 0x10
  SystemInterruptInformation            = 23,
  SystemExceptionInformation            = 33,
  SystemRegistryQuotaInformation        = 37,
  SystemLookasideInformation            = 45,
  SystemExtendedHandleInformation       = 64, // 0x40
  SystemCodeIntegrityInformation        = 103,
  SystemPolicyInformation               = 134,
};

enum OBJECT_INFORMATION_CLASS
{ ObjectBasicInformation = 0,
  ObjectNameInformation  = 1,
  ObjectTypeInformation  = 2
};

using NtQuerySystemInformation_pfn =
  NTSTATUS (NTAPI *)(
        IN  SYSTEM_INFORMATION_CLASS SystemInformationClass,
        OUT PVOID                    SystemInformation,
        IN  ULONG                    SystemInformationLength,
        OUT PULONG                   ReturnLength OPTIONAL
  );

using NtQueryObject_pfn =
  NTSTATUS (NTAPI *)(
       IN  HANDLE                   Handle       OPTIONAL,
       IN  OBJECT_INFORMATION_CLASS ObjectInformationClass,
       OUT PVOID                    ObjectInformation,
       IN  ULONG                    ObjectInformationLength,
       OUT PULONG                   ReturnLength OPTIONAL
  );

using NtDuplicateObject_pfn =
  NTSTATUS (NTAPI *)(
        IN  HANDLE      SourceProcessHandle,
        IN  HANDLE      SourceHandle,
        OUT HANDLE      TargetProcessHandle,
        OUT PHANDLE     TargetHandle,
        IN  ACCESS_MASK DesiredAccess OPTIONAL,
        IN  ULONG       Attributes    OPTIONAL,
        IN  ULONG       Options       OPTIONAL
  );

NtQuerySystemInformation_pfn NtQuerySystemInformation = nullptr;
NtDuplicateObject_pfn        NtDuplicateObject        = nullptr;
NtQueryObject_pfn            NtQueryObject            = nullptr;

enum _SK_NTDLL_HANDLE_TYPE
{
  Directory             = 0x3,
  Token                 = 0x5,
  Job                   = 0x6,
  Process               = 0x7,
  Thread                = 0x8,
  UserApcReserve        = 0xA,
  IoCompletionReserve   = 0xB,
  EventAlternate        = 0x10, // TODO: Fix whatever is going on here -- on some systems it's 0x10 (16), and on other 0x12 (18) ?
  Event                 = 0x12, // TODO: Fix whatever is going on here -- on some systems it's 0x10 (16), and on other 0x12 (18) ?
  Mutant                = 0x13,
  Semaphore             = 0x15,
  Timer                 = 0x16,
  IRTimer               = 0x17,
  WindowStation         = 0x1A,
  Desktop               = 0x1B,
  TpWorkerFactory       = 0x20,
  IoCompletion          = 0x25,
  WaitCompletionPacket  = 0x26,
  File                  = 0x27,
  Section               = 0x2D,
  Key                   = 0x2F,
  ALPC_Port             = 0x31,
  WmiGuid               = 0x34,
  DxgkSharedResource    = 0x3E,
  DxgkSharedSyncObject  = 0x40,
  DxgkCompositionObject = 0x45,
};

enum _SK_NTDLL_HANDLE_OBJ_ATTRIB
{
  Inherit                     = 0x00000002L,
  Permanent                   = 0x00000010L,
  Exclusive                   = 0x00000020L,
  CaseInsensitive             = 0x00000040L,
  OpenIF /*?*/                = 0x00000080L,
  OpenLink                    = 0x00000100L,
  KernelHandle                = 0x00000200L,
  ForceAccessCheck            = 0x00000400L,
  IgnoreImpersonatedDevicemap	= 0x00000800L,

  // Other, less commonly defined, attribs
  Protected /* Close = ..? */ = 0x00000001L,
  Audited                     = 0x00000004L,

  _ValidAttributeBits         = 0x00000FF2L |
                                       0x1L | // Protect
                                       0x4L   // Audit
};

typedef struct _SK_SYSTEM_HANDLE_TABLE_ENTRY_INFO
{
  USHORT UniqueProcessId;
  USHORT CreatorBackTraceIndex;
  UCHAR  ObjectTypeIndex;
  UCHAR  HandleAttributes;
  USHORT HandleValue;
  PVOID  Object;
  ULONG  GrantedAccess;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO,
*PSYSTEM_HANDLE_TABLE_ENTRY_INFO;

typedef struct _SK_SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX
{
  PVOID       Object;
  union
  {
    ULONG_PTR UniqueProcessId;

    struct
    {
      DWORD   ProcessId;
      DWORD   ThreadId; // ?? ( What are the upper-bits for anyway ? )
    };
  };

  union
  {
    ULONG_PTR HandleValue;
    HANDLE    Handle;
  };

  ULONG       GrantedAccess;
  USHORT      CreatorBackTraceIndex;
  USHORT      ObjectTypeIndex;
  ULONG       HandleAttributes;
  ULONG       Reserved;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX,
*PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SK_SYSTEM_HANDLE_INFORMATION
{ ULONG                          NumberOfHandles;
  SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles     [1];
} SYSTEM_HANDLE_INFORMATION,
 *PSYSTEM_HANDLE_INFORMATION;

typedef struct _SK_SYSTEM_HANDLE_INFORMATION_EX
{ ULONG_PTR                         NumberOfHandles;
  ULONG_PTR                         Reserved;
  SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles     [1];
} SYSTEM_HANDLE_INFORMATION_EX,
*PSYSTEM_HANDLE_INFORMATION_EX;

typedef struct _SK_UNICODE_STRING
{ USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} UNICODE_STRING,
*PUNICODE_STRING;

typedef const UNICODE_STRING 
           *PCUNICODE_STRING;

typedef struct _SK_PUBLIC_OBJECT_TYPE_INFORMATION
{ UNICODE_STRING TypeName;
  ULONG          Reserved [22];    // reserved for internal use
} PUBLIC_OBJECT_TYPE_INFORMATION,
*PPUBLIC_OBJECT_TYPE_INFORMATION;

typedef struct _SK_OBJECT_NAME_INFORMATION
{ UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION,
*POBJECT_NAME_INFORMATION;
#pragma pack (pop)

using _ByteArray = std::vector <uint8_t>;

std::string   SKIF_PresentDebugStr [2];
volatile LONG SKIF_PresentIdx    =   0;



#include <injection.h>
#include <sk_utility/command.h>
#include <imgui/imgui_internal.h>

extern SKIF_InjectionContext _inject;

struct SKIF_Console : SK_IVariableListener
{
  SK_ICommandProcessor cmd_proc;

  char InputBuf [256] = { };

  ImVector <      char *> Items;
  ImVector <const char *> Commands;
  ImVector <const char *> Variables; // XXX: SK has a more sophisticated system

  ImVector <      char *> History;
  int                     HistoryPos; // -1: new line, 0..History.Size-1 browsing history.

  ImGuiTextFilter Filter;
  bool            AutoScroll;
  bool            ScrollToBottom;

  SKIF_Console (void)
  {
    ClearLog ();
    memset (
      InputBuf, 0, sizeof (InputBuf)
    );
    HistoryPos = -1;

    Commands.push_back ("Help");
    Commands.push_back ("Clear");
    Commands.push_back ("Start");
    Commands.push_back ("Stop");

    extern bool SKIF_bDisableTooltips;
    extern bool SKIF_bDisableDPIScaling;
    extern bool SKIF_bDisableExitConfirmation;
    extern bool SKIF_bDisableStatusBar;
    extern bool SKIF_bSmallMode;
    extern bool SKIF_bFirstLaunch;
    extern bool SKIF_bEnableDebugMode;

    cmd_proc.AddVariable (                 "SKIF.UI.DisableTooltips",
       SK_CreateVar ( SK_IVariable::Boolean, &SKIF_bDisableTooltips         ) );
    cmd_proc.AddVariable (                 "SKIF.UI.DisableDPIScaling",
       SK_CreateVar ( SK_IVariable::Boolean, &SKIF_bDisableDPIScaling       ) );
    cmd_proc.AddVariable (                 "SKIF.UI.DisableExitConfirmation",
       SK_CreateVar ( SK_IVariable::Boolean, &SKIF_bDisableExitConfirmation ) );
    cmd_proc.AddVariable (                 "SKIF.UI.DisableStatusBar",
       SK_CreateVar ( SK_IVariable::Boolean, &SKIF_bDisableStatusBar        ) );
    cmd_proc.AddVariable (                 "SKIF.UI.SmallMode",             
       SK_CreateVar ( SK_IVariable::Boolean, &SKIF_bSmallMode               ) );
    cmd_proc.AddVariable (                 "SKIF.UI.DebugMode",             
       SK_CreateVar ( SK_IVariable::Boolean, &SKIF_bEnableDebugMode         ) );

    AutoScroll     = true;
    ScrollToBottom = false;

    AddLog (
      "[Status] Global Injection Status: %s", _inject.bCurrentState ? "Active"
                                                                    : "Inactive" );
  }

  ~SKIF_Console (void)
  {
    ClearLog ();

    for (int i = 0; i < History.Size; i++)
      free (History [i]);
  }

  bool OnVarChange ( SK_IVariable* var,
                             void* val = nullptr ) override
  {
    if (       var->getType          () == SK_IVariable::Boolean)
    {  *(bool *)var->getValuePointer () = *(bool *)val;
      return true;
    }

    return true;
  }

  // Portable helpers
  static int   Stricmp  (const char* s1, const char* s2)        { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
  static int   Strnicmp (const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
  static char* Strdup   (const char* s)                         { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
  static void  Strtrim  (char* s)                               { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

  void ClearLog (void)
  {
    for (int i = 0; i < Items.Size; i++)
      free (Items [i]);
    
    Items.clear ();
  }

  void AddLog (const char* fmt, ...) IM_FMTARGS(2)
  {
    // FIXME-OPT
    char        buf [1024] = { };
    va_list     args;
    va_start  ( args, fmt );
    vsnprintf ( buf,
  IM_ARRAYSIZE (buf), fmt,
                args
              );

    buf [IM_ARRAYSIZE (buf)-1] = 0;

    va_end    ( args );

    Items.push_back (
      Strdup (buf)
    );
  }

  void Draw (const char* title)
  { 
    ImVec2 vConsoleSize =
      ImGui::GetContentRegionAvail ();
           vConsoleSize.y /= 3.0f;

    ImGui::BeginChild (title, vConsoleSize, true);
    
    if (ImGui::SmallButton (ICON_FA_ERASER    " Clear"))
                                              { ClearLog (); }
        ImGui::SameLine    (                          );
    bool copy_to_clipboard =
        ImGui::SmallButton (ICON_FA_CLIPBOARD " Copy" );

    //static float t = 0.0f; if (ImGui::GetTime() - t > 0.02f) { t = ImGui::GetTime(); AddLog("Spam %f", t); }

    ImGui::Separator ();

    // Reserve enough left-over height for 1 separator + 1 input text
    const float footer_height_to_reserve =
      ImGui::GetStyle                  ().ItemSpacing.y +
      ImGui::GetFrameHeightWithSpacing ();

    ImGui::BeginChild ( "ScrollingRegion",
                          ImVec2 (0, -footer_height_to_reserve),
                            false,
                              ImGuiWindowFlags_HorizontalScrollbar
                      );

    if (ImGui::BeginPopupContextWindow ())
    {
      if (ImGui::Selectable (ICON_FA_ERASER " Clear"))
                                              ClearLog ();
      
      ImGui::EndPopup ();
    }

    // Display every line as a separate entry so we can change their color or add custom widgets.
    // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
    // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
    // to only process visible items. The clipper will automatically measure the height of your first item and then
    // "seek" to display only items in the visible area.
    // To use the clipper we can replace your standard loop:
    //      for (int i = 0; i < Items.Size; i++)
    //   With:
    //      ImGuiListClipper clipper;
    //      clipper.Begin(Items.Size);
    //      while (clipper.Step())
    //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
    // - That your items are evenly spaced (same height)
    // - That you have cheap random access to your elements (you can access them given their index,
    //   without processing all the ones before)
    // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
    // We would need random-access on the post-filtered list.
    // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
    // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
    // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
    // to improve this example code!
    // If your items are of variable height:
    // - Split them into same height items would be simpler and facilitate random-seeking into your list.
    // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.

    ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing, ImVec2 (4, 1)); // Tighten spacing

    if (copy_to_clipboard)
      ImGui::LogToClipboard ();

    for (int i = 0; i < Items.Size; i++)
    {
      const char* item =
                  Items [i];

      if (! Filter.PassFilter (item))
        continue;

      // Normally you would store more information in your item than just a string.
      // (e.g. make Items[] an array of structure, store color/type etc.)
      ImVec4   color;
      bool has_color = false;

      if      (strstr  (item, "[Error]"))         { color = ImVec4 (1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
      else if (strstr  (item, "[Failure]"))       { color = ImVec4 (1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }

      else if (strstr  (item, "[OK]"))            { color = ImVec4 (0.2f, 1.0f, 0.2f, 1.0f); has_color = true; }
      else if (strstr  (item, "[Success]"))       { color = ImVec4 (0.2f, 1.0f, 0.2f, 1.0f); has_color = true; }

      else if (strstr  (item, "[Status]"))        { color = ImVec4 (0.4f, 0.4f, 0.8f, 1.0f); has_color = true; }

      else if (strstr  (item, "[Warning]"))       { color = ImVec4 (1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
      else if (strncmp (item, "# ",    2) == 0)   { color = ImVec4 (1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
      
      if (has_color)
        ImGui::PushStyleColor (ImGuiCol_Text, color);

      ImGui::TextUnformatted (item);

      if (has_color)
        ImGui::PopStyleColor ();
    }

    if (copy_to_clipboard)
      ImGui::LogFinish ();

    if (       ScrollToBottom ||
         ( AutoScroll         &&
            ImGui::GetScrollY    () >=
            ImGui::GetScrollMaxY () )
       )
    {
      ImGui::SetScrollHereY (1.0f);
    }

    ScrollToBottom = false;

    ImGui::PopStyleVar ();
    ImGui::EndChild    ();
    ImGui::Separator   ();

    // Command-line
    bool reclaim_focus = false;

    ImGuiInputTextFlags input_text_flags =
      ImGuiInputTextFlags_EnterReturnsTrue   |
      ImGuiInputTextFlags_CallbackCompletion |
      ImGuiInputTextFlags_CallbackHistory;

    if ( ImGui::InputTextEx (
           "Console###InputBuf",
           "Type Help or click Tab to show available commands...",
                            InputBuf,
              IM_ARRAYSIZE (InputBuf), ImVec2(0.0f, 0.0f), input_text_flags,
                &TextEditCallbackStub, (void *)this
         )
       )
    {
      char* s =
        InputBuf;
      
      Strtrim (s);

      if (s [0])
        ExecCommand (s);

      strcpy (s, "");
      
      reclaim_focus = true;
    }

    // Auto-focus on window apparition
    ImGui::SetItemDefaultFocus ();

    if (reclaim_focus)
      ImGui::SetKeyboardFocusHere (-1); // Auto focus previous widget

    ImGui::EndChild ();
  }

  void ExecCommand (const char* command_line)
  {
    AddLog ("# %s\n", command_line);

    // Insert into history. First find match and delete it so it can be pushed to the back.
    // This isn't trying to be smart or optimal.
    HistoryPos = -1;

    for (int i = History.Size - 1; i >= 0; i--)
    {
      if (Stricmp (History [i], command_line) == 0)
      {
        free (History [i]);

        History.erase (
          History.begin () + i
        );

        break;
      }
    }

    History.push_back (
      Strdup (command_line)
    );

    // Process command
    if (Stricmp (command_line, "Clear") == 0)
    {
      ClearLog ();
    }

    else if (Stricmp (command_line, "Help") == 0)
    {
      AddLog ("Commands:");
      
      for (int i = 0; i < Commands.Size; i++)
      {
        AddLog ( "- %s",  Commands [i] );
      }
    }

    else if (Stricmp (command_line, "History") == 0)
    {
      int first =
        History.Size - 10;

      for (int i = first > 0 ? first : 0; i < History.Size; i++)
      {
        AddLog ( "%3d: %s\n", i,
                     History [i] );
      }
    }


    else if (Stricmp (command_line, "Start") == 0)
    {
      if (! _inject.bCurrentState)
      {
        extern bool SKIF_bStopOnInjection;

        AddLog ("Starting Injection... ");

        AddLog ( _inject._StartStopInject (_inject.bCurrentState, SKIF_bStopOnInjection)
                         ? "[Success] Started"
                         : "[Failure] Not Running" );
        AddLog ("\n");
      }

      else
      {
        AddLog ("[Warning] Injection already running...");
      }
    }

    else if (Stricmp (command_line, "Stop") == 0)
    {
      if (_inject.bCurrentState)
      {
        AddLog ("Stopping Injection... ");

        AddLog ( _inject._StartStopInject (_inject.bCurrentState)
                         ? "[Success] Stopped"
                         : "[Failure] Still Running" );
        AddLog ("\n");
      }

      else
      {
        AddLog ("[Warning] Injection not running...");
      }
    }


    else
    {
      auto* pVar = cmd_proc.FindVariable (command_line);
      if (! pVar)
      {
        std::string     split_me (command_line);
        auto split_at = split_me.find (' ');

        if (split_at != std::string::npos)
            split_me.resize (split_at);

            pVar = cmd_proc.FindVariable (split_me.c_str ());
      }

      if (pVar != nullptr)
      {
        auto result =
          cmd_proc.ProcessCommandLine (command_line);

        AddLog ("%hs", result.getResult ());
      }

      else
        AddLog ("Unknown Command: '%s'\n", command_line);
    }

    // On command input, we scroll to bottom even if AutoScroll==false
    ScrollToBottom = true;
  }

  // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
  static int
  TextEditCallbackStub (ImGuiInputTextCallbackData *data)
  {
    SKIF_Console* console =
      (SKIF_Console *)data->UserData;

    return
      console->TextEditCallback (data);
  }

  int
  TextEditCallback (ImGuiInputTextCallbackData *data)
  {
    //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
    switch (data->EventFlag)
    {
      case ImGuiInputTextFlags_CallbackCompletion:
      {
        // Locate beginning of current word
        const char* word_end   = data->Buf +
                                 data->CursorPos;
        const char* word_start = word_end;

        while (word_start > data->Buf)
        {
          const char c =
            word_start [-1];

          if (c == ' ' || c == '\t' || c == ',' || c == ';')
              break;

          word_start--;
        }

        ImVector <const char *> candidates;

        for (int i = 0; i < Commands.Size; i++)
        {
          if (Strnicmp (Commands [i], word_start,
                     (int)(word_end - word_start)) == 0)
          {
            candidates.push_back (
              Commands [i]
            );
          }
        }

        for ( auto& var : cmd_proc.variables_ )
        {
          if ( Strnicmp ( var.first.c_str (), word_start,
                             (int)(word_end - word_start) ) == 0 )
          {
            candidates.push_back (
              var.first.c_str ()
            );
          }
        }

        if (candidates.Size == 0)
        {
          AddLog ( "No match for \"%.*s\"!\n",
                     (int)(word_end - word_start),
                                      word_start );
        }

        else if (candidates.Size == 1)
        {
          // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
          data->DeleteChars (
            (int)(word_start - data->Buf),
            (int)(word_end   - word_start)
          );
          data->InsertChars (data->CursorPos, candidates [0]);
          data->InsertChars (data->CursorPos, " ");
        }

        else
        {
          int match_len =
            (int)(word_end - word_start);

          for (;;)
          {
            int  c                      =    0;
            bool all_candidates_matches = true;

            for ( int i = 0; i < candidates.Size && all_candidates_matches; i++ )
            {
              if (     i == 0)
                                 c  = toupper (candidates [i][match_len]);
              else if (c == 0 || c != toupper (candidates [i][match_len]))
                all_candidates_matches = false;
            }
              
            if (! all_candidates_matches)
              break;
            
            match_len++;
          }

          if (match_len > 0)
          {
            data->DeleteChars (
              (int)(word_start - data->Buf),
              (int)(word_end   - word_start)
            );

            data->InsertChars (
              data->CursorPos, candidates [0],
                               candidates [0] + match_len
            );
          }

          // List matches
          AddLog ("Possible matches:\n");
          for (int i = 0; i < candidates.Size; i++)
          {
            AddLog ("- %s\n", candidates [i]);
          }
        }

        break;
      }

      case ImGuiInputTextFlags_CallbackHistory:
      {
        // Example of HISTORY
        const int prev_history_pos =
                       HistoryPos;

        if (data->EventKey == ImGuiKey_UpArrow)
        {
          if (HistoryPos == -1)
              HistoryPos = History.Size - 1;

          else if (HistoryPos > 0)
                   HistoryPos--;
        }

        else if (data->EventKey == ImGuiKey_DownArrow)
        {
          if (HistoryPos != -1)
            if (++HistoryPos >= History.Size)
                  HistoryPos = -1;
        }

        // A better implementation would preserve the data on the current input line along with cursor position.
        if (prev_history_pos != HistoryPos)
        {
          const char *history_str =
                     (HistoryPos >= 0) ?
                  History [HistoryPos] : "";

          data->DeleteChars (0, data->BufTextLen);
          data->InsertChars (0, history_str);
        }
      }
    }

    return 0;
  }
};

static SKIF_Console console;

void SKIF_ProcessCommandLine (const char* szCmd)
{
  console.ExecCommand (szCmd);
}

SK_Inject_AuditRecord_pfn
SK_Inject_AuditRecord = nullptr;

SK_Inject_GetRecord_pfn
SK_Inject_GetRecord   = nullptr;

// Some string manipulation to assume whether a process is an Xbox app or not
bool SKIF_Debug_IsXboxApp(std::string path, std::string processName)
{
  // Does the string contain "WindowsApps" ?
  bool xbox_app = (path.find("WindowsApps") != std::string::npos);

  // If the string does not contain "WindowsApps", do some string manipulation and assumptions
  if (!xbox_app && path.length() > 0)
  {
    // \Device\HarddiskVolume21\Hades.exe

    std::string p1 = R"(\Device\HarddiskVolume)";
    if (path.find(p1) != std::string::npos)
      path.erase(path.find(p1), p1.length());

    // 21\Hades.exe

    std::string p2 = R"(\)" + processName;
    if (path.find(p2) != std::string::npos)
      path.erase(path.find(p2), p2.length());

    // 21

    if (strlen(path.c_str()) < 3) // 0 - 99
      xbox_app = true;
  }

  return xbox_app;
}

// Some string manipulation to assume whether a process is a Steam app or not
bool SKIF_Debug_IsSteamApp(std::string path, std::string processName)
{
  return (path.find("SteamApps") != std::string::npos);
}

HRESULT
SKIF_Debug_DrawUI (void)
{
  extern HMODULE hModSK64;
  extern float   SKIF_ImGui_GlobalDPIScale;
  
  if (hModSK64 == NULL)
    hModSK64 = LoadLibraryW (L"SpecialK64.dll");

   SKX_GetInjectedPIDs_pfn
   SKX_GetInjectedPIDs     =
  (SKX_GetInjectedPIDs_pfn)GetProcAddress   (hModSK64,
  "SKX_GetInjectedPIDs");

   SK_Inject_GetRecord     =
  (SK_Inject_GetRecord_pfn)GetProcAddress   (hModSK64,
  "SK_Inject_GetRecord");

   SK_Inject_AuditRecord     =
  (SK_Inject_AuditRecord_pfn)GetProcAddress (hModSK64,
  "SK_Inject_AuditRecord");

   static bool active_listing = true;

  if (SKX_GetInjectedPIDs != nullptr)
  {
    std::set <DWORD> _Active32;
    std::set <DWORD> _Active64;

    struct standby_record_s {
      std::string  name;
      std::wstring filename;
      DWORD        pid;
    };

    enum inject_policy {
      Blacklist,
      DontCare,
      Whitelist
    };

    static std::vector <standby_record_s> _Standby32;
    static std::vector <standby_record_s> _Standby64;

    std::set <DWORD> _Used32;
    std::set <DWORD> _Used64;

    static std::map <DWORD,  std::wstring> executables_64;
    static std::map <DWORD, inject_policy>    policies_64;
    static std::map <DWORD,   std::string>    tooltips_64;
    static std::map <DWORD,   std::string>    tooltips_32;
    static std::map <DWORD, inject_policy>    policies_32;
    static std::map <DWORD,  std::wstring> executables_32;


    static           DWORD dwPIDs [MAX_INJECTED_PROCS] = { };
    size_t num_pids =
      SKX_GetInjectedPIDs (dwPIDs, MAX_INJECTED_PROCS);
  
    static DWORD dwMonitored = 0;

    ImGui::Separator (                   );
    console.Draw     ("Prototype Console");
    ImGui::Separator (                   );

    static
      std::once_flag init_ntdll;
    std::call_once ( init_ntdll, [](void)
    {
      SKIF_ImportFromNtDll (NtQuerySystemInformation);
      SKIF_ImportFromNtDll (NtQueryObject);
      SKIF_ImportFromNtDll (NtDuplicateObject);
    }              );

    static std::string handle_dump;
    static std::string standby_list;

    extern void
      SKIF_ImGui_SetHoverTip(const std::string_view& szText);

    if (ImGui::CollapsingHeader("Active 64-bit Global Injections", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f ), " %s", "PID");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 ( 55.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "Type");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (100.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "Process Name");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (350.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "Steam ID");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (450.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "Window Title");
    //ImGui::SameLine    ( );
    //ImGui::ItemSize    (ImVec2 (650.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
    //ImGui::SameLine    ( );
    //ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "UWP Package Name");

      ImGui::Separator   ( );

      if (num_pids == 0)
        ImGui::TextColored (ImVec4 ( .8f, .8f, .8f, 1.f ), "64-bit Special K is currently not active in any injected process.");

      while (num_pids > 0)
      {
        DWORD dwPID =
              dwPIDs [--num_pids];

        _Active64.emplace (dwPID);

        auto *pRecord =
          SK_Inject_GetRecord (dwPID);

        std::string pretty_str       = ICON_FA_WINDOWS,
                    pretty_str_hover = "Windows";

        if (pRecord->platform.steam_appid != 0x0 &&
            pRecord->platform.steam_appid != INT_MAX)
        {
          pretty_str       = ICON_FA_STEAM;
          pretty_str_hover = "Steam";
        }
        else if (pRecord->platform.uwp_full_name[0] != L'\0' ||
                 SKIF_Debug_IsXboxApp(tooltips_64[dwPID], SK_WideCharToUTF8(pRecord->process.name)))
        {
          pretty_str       = ICON_FA_XBOX;
          pretty_str_hover = "Xbox";
        }

        if (executables_64.count (dwPID))
        {
          ImGui::Text     (" %i", pRecord->process.id);
          ImGui::SameLine ( );
          ImGui::ItemSize (ImVec2 ( 60.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
          ImGui::SameLine ( );
          ImGui::Text     (" %s", pretty_str.c_str());
          SKIF_ImGui_SetHoverTip (pretty_str_hover);
          ImGui::SameLine ( );
          ImGui::ItemSize (ImVec2 (100.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
          ImGui::SameLine ( );
          ImGui::Text     ("%s", SK_WideCharToUTF8(pRecord->process.name).c_str());
        //SKIF_ImGui_SetHoverTip (tooltips_64[dwPID]);
          ImGui::SameLine ( );
          ImGui::ItemSize (ImVec2 (350.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
          ImGui::SameLine ( );
          ImGui::Text     ("%d", pRecord->platform.steam_appid);
          ImGui::SameLine ( );
          ImGui::ItemSize (ImVec2 (450.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
          ImGui::SameLine ( );
          ImGui::Text     ("%s", SK_WideCharToUTF8(pRecord->process.win_title).c_str());
        //ImGui::SameLine ( );
        //ImGui::ItemSize (ImVec2 (650.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()) );
        //ImGui::SameLine ( );
        //ImGui::Text     ("%s", SK_WideCharToUTF8(pRecord->platform.uwp_full_name).c_str());
        }
      }

      SKIF_ImGui_Spacing ( );
    }

    static DWORD
         dwLastRefresh = 0;
    if ( dwLastRefresh + 500 < SKIF_timeGetTime () && active_listing )
    {    dwLastRefresh       = SKIF_timeGetTime ();
      standby_list.clear ();
      _Standby32.clear   ();
      _Standby64.clear   ();

      using _PerProcessHandleMap =
        std::map        < DWORD,
            std::vector < SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX >
                                                            >;
      NTSTATUS ntStatusHandles;

      ULONG      handle_info_size ( SystemHandleInformationSize );
      _ByteArray handle_info_buffer;

      do
      {
        handle_info_buffer.resize (
                 handle_info_size );

        ntStatusHandles =
          NtQuerySystemInformation (
            SystemExtendedHandleInformation,
              handle_info_buffer.data (),
              handle_info_size,
             &handle_info_size     );

      } while (ntStatusHandles == STATUS_INFO_LENGTH_MISMATCH);

      if (NT_SUCCESS (ntStatusHandles))
      {
        DWORD  dwPidOfMe   =  GetCurrentProcessId (); // Actual Pid
        HANDLE hProcessDst =  GetCurrentProcess   (); // Pseudo Handle

        _PerProcessHandleMap
          handles_by_process;

        auto handleTableInformationEx =
          PSYSTEM_HANDLE_INFORMATION_EX (
             handle_info_buffer.data ()
          );

        for ( unsigned int i = 0;
                           i < handleTableInformationEx->NumberOfHandles;
                         i++)
        {
          if (handleTableInformationEx->Handles [i].ProcessId == dwPidOfMe)
            continue;

          // This does not work -- evaluates as true on seemingly all handles, including event handles
          if (handleTableInformationEx->Handles[i].ObjectTypeIndex != Event && handleTableInformationEx->Handles[i].ObjectTypeIndex != EventAlternate)
            continue;

          handles_by_process [handleTableInformationEx->Handles [i].ProcessId]
                .emplace_back (handleTableInformationEx->Handles [i]);
        }

        for ( auto& handles : handles_by_process )
        {
          auto dwProcId = handles.first;

          HANDLE hProcessSrc =
              OpenProcess (
                  PROCESS_DUP_HANDLE |
                  PROCESS_QUERY_INFORMATION, FALSE,
                dwProcId  );

          if (! hProcessSrc) continue;

          wchar_t                                wszProcessName [MAX_PATH] = { };
          GetProcessImageFileNameW (hProcessSrc, wszProcessName, MAX_PATH);

          for ( auto& handle : handles.second )
          {
            auto hHandleSrc = handle.Handle;

            HANDLE   hDupHandle;
            NTSTATUS ntStat     =
              NtDuplicateObject (
                hProcessSrc,  hHandleSrc,
                hProcessDst, &hDupHandle,
                        0, 0, 0 );

            if (! NT_SUCCESS (ntStat)) continue;

            std::wstring handle_name = L"";

            ULONG      _ObjectNameLen ( 64 );
            _ByteArray pObjectName;

            do
            {
              pObjectName.resize (
                  _ObjectNameLen );

              ntStat =
                NtQueryObject (
                  hDupHandle,
                       ObjectNameInformation,
                      pObjectName.data (),
                      _ObjectNameLen,
                     &_ObjectNameLen );

            } while (ntStat == STATUS_INFO_LENGTH_MISMATCH);

            if (NT_SUCCESS (ntStat))
            {
              POBJECT_NAME_INFORMATION _pni =
                (POBJECT_NAME_INFORMATION)pObjectName.data ();

              handle_name = _pni != nullptr ?
                      _pni->Name.Length > 0 ?
                      _pni->Name.Buffer     : L""
                                            : L"";
            }

            CloseHandle (hDupHandle);

            if ( std::wstring::npos !=
                   handle_name.find ( L"SK_GlobalHookTeardown64" )
                && _Used64.emplace (dwProcId).second ) {
                _Standby64.emplace_back (
                  standby_record_s {
                   SK_WideCharToUTF8 (wszProcessName),
                    wszProcessName, dwProcId } ), 
                    PathStripPathW (
                      _Standby64.back ().
                        filename.data () );
                break;
            } 
            
            else if ( std::wstring::npos !=
                   handle_name.find ( L"SK_GlobalHookTeardown32" )
                && _Used32.emplace (dwProcId).second ) {
                _Standby32.emplace_back (
                  standby_record_s {
                   SK_WideCharToUTF8 (wszProcessName),
                    wszProcessName, dwProcId } ), 
                    PathStripPathW (
                      _Standby32.back ().
                        filename.data () );

                break;
            }
          }

          CloseHandle (hProcessSrc);
        }
      }

      executables_64.clear ();
         policies_64.clear ();
         policies_32.clear ();
      executables_32.clear ();

      if (! _Standby64.empty ()) for ( auto proc : _Standby64 )
      {
        executables_64 [proc.pid]     =     proc.filename;
        tooltips_64    [proc.pid]     =     proc.name;

        if      (_inject._TestUserList     (proc.name.c_str(), false))
          policies_64  [proc.pid]     =     Blacklist;
        else if (_inject._TestUserList     (proc.name.c_str(),  true))
          policies_64  [proc.pid]     =     Whitelist;
        else
          policies_64  [proc.pid]     =     DontCare;
      }

      if (! _Standby32.empty ()) for ( auto proc : _Standby32 )
      { executables_32 [proc.pid]     =     proc.filename;
        tooltips_32    [proc.pid]     =     proc.name;

        if      (_inject._TestUserList     (proc.name.c_str(), false))
          policies_32  [proc.pid]     =     Blacklist;
        else if (_inject._TestUserList     (proc.name.c_str(),  true))
          policies_32  [proc.pid]     =     Whitelist;
        else
          policies_32  [proc.pid]     =     DontCare;
      }
    }

    std::string header_title = SK_FormatString ( "%s Active Process Monitoring###ActiveProcessMonitoring", ((active_listing) ? ICON_FA_TOGGLE_ON
                                                                          : ICON_FA_TOGGLE_OFF));

    active_listing = ImGui::CollapsingHeader(header_title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    if (active_listing)
    {
      ImGui::BeginChildFrame (0x68992, ImVec2 (ImGui::GetContentRegionAvail ().x,
                                               ImGui::GetContentRegionAvail ().y / 1.3f), ImGuiWindowFlags_AlwaysVerticalScrollbar |
                                                                                          ImGuiWindowFlags_NoBackground);
      
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 ( 20.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "%s", "Actions");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (100.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "%s", "PID");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "%s", "Type");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (195.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "%s", "Arch");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (245.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "%s", "Process Name");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (450.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "%s", "Path");

      ImGui::Separator   ( );

      if (executables_64.size() == 0 && executables_32.size() == 0)
        ImGui::TextColored (ImVec4 (.8f, .8f, .8f, 1.f), "Special K is currently not injected in any process.");

      for ( auto& proc64 : executables_64 )
      {
        if (proc64.second.find (L"rundll32") != std::wstring::npos)
          continue;

        inject_policy policy =
          policies_64 [proc64.first];
        
        std::string pretty_str       = ICON_FA_WINDOWS,
                    pretty_str_hover = "Windows";
        
        if (StrStrIA(tooltips_64[proc64.first].c_str(), "SteamApps") != NULL)
        {
          pretty_str       = ICON_FA_STEAM;
          pretty_str_hover = "Steam";
        }
        else if (SKIF_Debug_IsXboxApp(tooltips_64[proc64.first], SK_WideCharToUTF8(proc64.second)))
        {
          pretty_str       = ICON_FA_XBOX;
          pretty_str_hover = "Xbox";
        }

        ImGui::PushID (proc64.first);

        ImGui::PushStyleColor  (ImGuiCol_Button, policy == Blacklist ? ImVec4 (.9f, .1f, .1f, 1.f)
                                                                     : ImVec4 (.1f, .1f, .1f, .5f));

        if (ImGui::SmallButton (ICON_FA_BAN      "###Ban64")   && ! _inject._TestUserList (SK_WideCharToUTF8(proc64.second).c_str(), false))
          _inject._AddUserList (SK_WideCharToUTF8(proc64.second), false);
        
        ImGui::SameLine        ( );
        ImGui::PushStyleColor  (ImGuiCol_Button, policy == DontCare  ? ImVec4 (.8f, .7f, .0f, 1.f)
                                                                     : ImVec4 (.0f, .0f, .0f, .0f));

        ImGui::SmallButton     (ICON_FA_QUESTION_CIRCLE"###Question64");    
        ImGui::SameLine        ( );

        ImGui::PushStyleColor  (ImGuiCol_Button, policy == Whitelist ? ImVec4 (.1f, .9f, .1f, 1.f)
                                                                     : ImVec4 (.1f, .1f, .1f, .5f));
        
        if (ImGui::SmallButton (ICON_FA_CHECK    "###Check64") && ! _inject._TestUserList (SK_WideCharToUTF8(proc64.second).c_str(), true))
          _inject._AddUserList (SK_WideCharToUTF8(proc64.second), true);

        ImGui::PopStyleColor   (3);

        ImGui::SameLine        ( );        
        ImGui::ItemSize        (ImVec2 (100.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "%i", proc64.first);
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "  %s", pretty_str.c_str());
        SKIF_ImGui_SetHoverTip (pretty_str_hover);
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (195.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "64-bit");
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (245.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (_Active64.count (proc64.first) ? ImVec4 (.2f, 1.f, .2f, 1.f)
                                                               : ImVec4 (.8f, .8f, .8f, 1.f),
                                             "%s", SK_WideCharToUTF8(proc64.second).c_str());
      //SKIF_ImGui_SetHoverTip (tooltips_64 [proc64.first]);
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (450.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", tooltips_64[proc64.first].c_str());
        if (strlen (tooltips_64[proc64.first].c_str()) > 73)
          SKIF_ImGui_SetHoverTip (tooltips_64[proc64.first]);

        ImGui::PopID  ( );
      }

      for ( auto& proc32 : executables_32 )
      {
        if (proc32.second.find (L"rundll32") != std::wstring::npos)
          continue;

        inject_policy policy =
          policies_32 [proc32.first];
        
        std::string pretty_str       = ICON_FA_WINDOWS,
                    pretty_str_hover = "Windows";

        if (StrStrIA(tooltips_32[proc32.first].c_str(), "SteamApps") != NULL)
        {
          pretty_str       = ICON_FA_STEAM;
          pretty_str_hover = "Steam";
        }
        else if (SKIF_Debug_IsXboxApp(tooltips_32[proc32.first], SK_WideCharToUTF8(proc32.second)))
        {
          pretty_str       = ICON_FA_XBOX;
          pretty_str_hover = "Xbox";
        }

        ImGui::PushID (proc32.first);

        ImGui::PushStyleColor  (ImGuiCol_Button, policy == Blacklist ? ImVec4 (.9f, .1f, .1f, 1.f)
                                                                     : ImVec4 (.1f, .1f, .1f, .5f));

        if (ImGui::SmallButton (ICON_FA_BAN      "###Ban32")   && ! _inject._TestUserList (SK_WideCharToUTF8(proc32.second).c_str(), false))
          _inject._AddUserList (SK_WideCharToUTF8(proc32.second), false);
        
        ImGui::SameLine        ( );
        ImGui::PushStyleColor  (ImGuiCol_Button, policy == DontCare  ? ImVec4 (.8f, .7f, .0f, 1.f)
                                                                     : ImVec4 (.0f, .0f, .0f, .0f));

        ImGui::SmallButton     (ICON_FA_QUESTION_CIRCLE"###Question32");    
        ImGui::SameLine        ( );

        ImGui::PushStyleColor  (ImGuiCol_Button, policy == Whitelist ? ImVec4 (.1f, .9f, .1f, 1.f)
                                                                     : ImVec4 (.1f, .1f, .1f, .5f));
        
        if (ImGui::SmallButton (ICON_FA_CHECK    "###Check32") && ! _inject._TestUserList (SK_WideCharToUTF8(proc32.second).c_str(), true))
          _inject._AddUserList (SK_WideCharToUTF8(proc32.second), true);

        ImGui::PopStyleColor   (3);
        
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (100.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "%i", proc32.first);
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "  %s", pretty_str.c_str());
        SKIF_ImGui_SetHoverTip (pretty_str_hover);
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (195.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", "32-bit");
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (245.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (_Active32.count (proc32.first) ? ImVec4 (.2f, 1.f, .2f, 1.f)
                                                               : ImVec4 (.8f, .8f, .8f, 1.f),
                                             "%s", SK_WideCharToUTF8(proc32.second).c_str());
      //SKIF_ImGui_SetHoverTip (tooltips_32 [proc32.first]);
        ImGui::SameLine        ( );
        ImGui::ItemSize        (ImVec2 (450.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImVec4 (.8f, .8f, .8f, 1.f ), "%s", tooltips_32[proc32.first].c_str());
        if (strlen (tooltips_32[proc32.first].c_str()) > 73)
          SKIF_ImGui_SetHoverTip (tooltips_32[proc32.first]);

        ImGui::PopID  ( );
      }
      ImGui::EndChildFrame ();
    }

    if (ImGui::CollapsingHeader ("Windows Nt Handle/Object Monitor", ImGuiTreeNodeFlags_AllowItemOverlap))
    {
      ImGui::PushStyleColor (ImGuiCol_Button, ImVec4 (.5f, .5f, .5f, 1.f));
      ImGui::SameLine       ();
      if (ImGui::Button     ("Analyze Usermode Handles"))
      {
        handle_dump.clear ();

        using _PerProcessHandleMap =
          std::map        < DWORD,
              std::vector < SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX >
                                                              >;
        NTSTATUS ntStatusHandles;
        
        ULONG      handle_info_size ( SystemHandleInformationSize );
        _ByteArray handle_info_buffer;
        
        do
        {
          handle_info_buffer.resize (
                   handle_info_size );
        
          ntStatusHandles =
            NtQuerySystemInformation (
              SystemExtendedHandleInformation,
                handle_info_buffer.data (),
                handle_info_size,
               &handle_info_size     );
        } while (ntStatusHandles == STATUS_INFO_LENGTH_MISMATCH);
        
        if (NT_SUCCESS (ntStatusHandles))
        {
          DWORD  dwPidOfMe   =  GetCurrentProcessId (); // Actual Pid
          HANDLE hProcessDst =  GetCurrentProcess   (); // Pseudo Handle
        
          _PerProcessHandleMap
            handles_by_process;
        
          auto handleTableInformationEx =
            PSYSTEM_HANDLE_INFORMATION_EX (
               handle_info_buffer.data ()
            );
        
          for ( unsigned int i = 0;
                             i < handleTableInformationEx->NumberOfHandles;
                           ++i )
          {
            if (handleTableInformationEx->Handles [i].ProcessId == dwPidOfMe)
              continue;
        
          //USHORT type =
          //    handleTableInformationEx->Handles [i].ObjectTypeIndex; 
            if (handleTableInformationEx->Handles [i].Object == nullptr) continue;
               
            handles_by_process [handleTableInformationEx->Handles [i].ProcessId]
                 .emplace_back (handleTableInformationEx->Handles [i]);
          }
        
          for ( auto& handles : handles_by_process )
          {
            auto dwProcId = handles.first;
        
            HANDLE hProcessSrc =
                OpenProcess (
                    PROCESS_DUP_HANDLE |
                    PROCESS_QUERY_INFORMATION, FALSE,
                  dwProcId  );
        
            if (! hProcessSrc) continue;
        
            wchar_t                                wszProcessName [MAX_PATH] = { };
            GetProcessImageFileNameW (hProcessSrc, wszProcessName, MAX_PATH);

            handle_dump +=
                SK_FormatString (             "\n"
                                 " Process %lu (%ws):\n"
                                 " ------------\n\n",
                                         dwProcId, wszProcessName);

            for ( auto& handle : handles.second )
            {
              auto hHandleSrc = handle.Handle;
        
              ACCESS_MASK mask = 0x0;

              if (handle.ObjectTypeIndex == Thread)       mask = THREAD_QUERY_INFORMATION;
              if (handle.ObjectTypeIndex == Process)      mask = PROCESS_QUERY_INFORMATION;
              if (handle.ObjectTypeIndex == Semaphore)    mask = SEMAPHORE_ALL_ACCESS;
              if (handle.ObjectTypeIndex == Key)          mask = KEY_QUERY_VALUE;
              if (handle.ObjectTypeIndex == Job)          mask = JOB_OBJECT_QUERY;
              if (handle.ObjectTypeIndex == Mutant)       mask = MUTANT_QUERY_STATE;
              if (handle.ObjectTypeIndex == Section)      mask = SECTION_QUERY;
              if (handle.ObjectTypeIndex == Token)        mask = TOKEN_QUERY;
              if (handle.ObjectTypeIndex == Timer)        mask = TIMER_QUERY_STATE;
              if (handle.ObjectTypeIndex == IRTimer)      mask = TIMER_QUERY_STATE;
              if (handle.ObjectTypeIndex == IoCompletion) mask = IO_COMPLETION_ALL_ACCESS;
              
              HANDLE   hDupHandle;
              NTSTATUS ntStat     =
                NtDuplicateObject (
                  hProcessSrc,  hHandleSrc,
                  hProcessDst, &hDupHandle,
                          mask, 0, DUPLICATE_SAME_ACCESS);
        
              if (! NT_SUCCESS (ntStat)) continue;
        
              std::wstring handle_name = L"";

              if (handle.ObjectTypeIndex != File)
              {
                ULONG      _ObjectNameLen (64);
                _ByteArray pObjectName;

                do
                {
                  pObjectName.resize (
                    _ObjectNameLen   );

                  ntStat =
                    NtQueryObject ( hDupHandle,
                           ObjectNameInformation,
                           pObjectName.data (),
                          _ObjectNameLen,
                         &_ObjectNameLen );

                } while (ntStat == STATUS_INFO_LENGTH_MISMATCH);

                if (NT_SUCCESS (ntStat))
                {
                  POBJECT_NAME_INFORMATION _pni =
                    (POBJECT_NAME_INFORMATION)pObjectName.data ();

                  handle_name =
                    _pni != nullptr    &&
                    _pni->Name.Length > 0 ? _pni->Name.Buffer
                                          : L"";
                }

                if ( handle_name.empty () )
                {
                  do
                  {
                    pObjectName.resize (
                      _ObjectNameLen   );

                    ntStat =
                      NtQueryObject ( handle.Handle,
                             ObjectNameInformation,
                             pObjectName.data (),
                            _ObjectNameLen,
                           &_ObjectNameLen );

                  } while (ntStat == STATUS_INFO_LENGTH_MISMATCH);

                  if (NT_SUCCESS (ntStat))
                  {
                    POBJECT_NAME_INFORMATION _pni =
                      (POBJECT_NAME_INFORMATION)pObjectName.data ();

                    handle_name =
                      _pni != nullptr    &&
                      _pni->Name.Length > 0 ? _pni->Name.Buffer
                                            : L"";
                  }
                }
              }

              if ((! NT_SUCCESS (ntStat)) || handle_name.empty ())
              {
                // This is a Windows 10 function, and we still support Windows 8, so... yeah.
                using  GetThreadDescription_pfn = HRESULT (WINAPI *)(HANDLE, PWSTR*);
                static GetThreadDescription_pfn
                      _GetThreadDescriptionWin10 = (GetThreadDescription_pfn)GetProcAddress (GetModuleHandle (L"Kernel32"),
                      "GetThreadDescription"                                                );

                if ( handle.ObjectTypeIndex == Thread && _GetThreadDescriptionWin10 != nullptr )
                {
                  wchar_t *pwszThreadDesc = nullptr;
                           _GetThreadDescriptionWin10 ( hDupHandle,
                          &pwszThreadDesc );

                  if (     pwszThreadDesc != nullptr  ) handle_name =
                           pwszThreadDesc;
                }

                else if ( handle.ObjectTypeIndex == Process )
                {
                  wchar_t                                wszFileName [MAX_PATH] = { };
                  GetProcessImageFileNameW ( hDupHandle, wszFileName, MAX_PATH );
                                           handle_name = wszFileName;
                }

                else if ( handle.ObjectTypeIndex == File )
                {
                  wchar_t                                      wszFileName [MAX_PATH] = { };
                  SKIF_File_GetNameFromHandle ( handle.Handle, wszFileName, MAX_PATH );
                                                 handle_name = wszFileName;
                }
              }

              std::string type =
                  "";// std::to_string (handle.ObjectTypeIndex);

              std::stack <std::string> attrib_stack;
                          std::string  attribs;

              if ( handle.HandleAttributes & Inherit                     ) attrib_stack.push ("Inheritable");
              if ( handle.HandleAttributes & Permanent                   ) attrib_stack.push ("Permanent");
              if ( handle.HandleAttributes & Exclusive                   ) attrib_stack.push ("Exclusive");
              if ( handle.HandleAttributes & CaseInsensitive             ) attrib_stack.push ("CaseInsensitive");
              if ( handle.HandleAttributes & OpenIF                      ) attrib_stack.push ("OpenIF");
              if ( handle.HandleAttributes & OpenLink                    ) attrib_stack.push ("OpenLink");
              if ( handle.HandleAttributes & KernelHandle                ) attrib_stack.push ("Kernel");
              if ( handle.HandleAttributes & ForceAccessCheck            ) attrib_stack.push ("ForceAccessCheck");
              if ( handle.HandleAttributes & IgnoreImpersonatedDevicemap ) attrib_stack.push ("IgnoreImpersonatedDevicemap");

              if ( handle.HandleAttributes & Protected                   ) attrib_stack.push ("Cannot Be Closed");

              if ((handle.HandleAttributes & ~(_ValidAttributeBits))!=0x0)
              {
                attrib_stack.push (
                  SK_FormatString (
                    "Unknown Attrib Bits: 0x%08x",
                      handle.HandleAttributes & ~(_ValidAttributeBits)
                  )
                );
              }

              if (! attrib_stack.empty ())
              {     attribs += "(" ;
                do
                {
                  attribs += attrib_stack.top ();
                             attrib_stack.pop ();

                  if (! attrib_stack.empty ())
                    attribs += ", ";

                } while (! attrib_stack.empty ());
                    attribs += ")" ;
              }

              switch (handle.ObjectTypeIndex)
              {
                case Directory:             type = "Directory";             break;
                case Token:                 type = "Token";                 break;
                case Job:                   type = "Job";                   break;
                case Process:               type = "Process";               break;
                case Thread:                type = "Thread";                break;
                case UserApcReserve:        type = "UserApcReserve";        break;
                case IoCompletionReserve:   type = "IoCompletionReserve";   break;
                case Event:                 type = "Event";                 break;
                case Mutant:                type = "Mutant";                break;
                case Semaphore:             type = "Semaphore";             break;
                case Timer:                 type = "Timer";                 break;
                case IRTimer:               type = "IRTimer";               break;
                case WindowStation:         type = "WindowStation";         break;
                case Desktop:               type = "Desktop";               break;
                case TpWorkerFactory:       type = "TpWorkerFactory";       break;
                case IoCompletion:          type = "IoCompletion";          break;
                case WaitCompletionPacket:  type = "WaitCompletionPacket";  break;
                case File:                  type = "File";                  break;
                case Section:               type = "Section";               break;
                case Key:                   type = "Key";                   break;
                case ALPC_Port:             type = "ALPC Port";             break;
                case WmiGuid:               type = "WmiGuid";               break;
                case DxgkSharedResource:    type = "DxgkSharedResource";    break;
                case DxgkSharedSyncObject:  type = "DxgkSharedSyncObject";  break;
                case DxgkCompositionObject: type = "DxgkCompositionObject"; break;
                default:
                {
                  ULONG      _ObjectTypeLen = 0x1000;
                  _ByteArray pObjectType;
                  
                  NTSTATUS ntStatTypeQuery  = 0x0;

                  do
                  {
                    pObjectType.resize (
                         _ObjectTypeLen);

                    ntStatTypeQuery =
                      NtQueryObject ( hDupHandle,
                             ObjectTypeInformation,
                            pObjectType.data (),
                            _ObjectTypeLen,
                           &_ObjectTypeLen
                                    );
                  } while (ntStatTypeQuery == STATUS_INFO_LENGTH_MISMATCH);

                  if (NT_SUCCESS (ntStatTypeQuery))
                  {
                    PUBLIC_OBJECT_TYPE_INFORMATION _poti =
                      *(PUBLIC_OBJECT_TYPE_INFORMATION *)pObjectType.data ();
                
                    handle_dump +=
                      SK_FormatString (
                        "\t[%04x]  < %wZ, Type=%02x >    %ws    %hs\n",
                          hDupHandle, _poti.TypeName, handle.ObjectTypeIndex,
                                                      handle_name.c_str (),
                                                      attribs.c_str     ()
                      );
                  }
                } break;
              }

              if (! ( type.empty () || handle_name.empty ()))
              {
                handle_dump +=
                  SK_FormatString (
                    "\t[%04x]  %-24hs    %ws    %hs\n",
                      handle.HandleValue,      type.c_str (),
                      handle_name.c_str (), attribs.c_str ()
                  );
              }

              CloseHandle (hDupHandle);
            }
        
            CloseHandle (hProcessSrc);
          }
        }
      }
      ImGui::PopStyleColor  ();
      
      if (! handle_dump.empty ())
      {
        ImGui::Separator ();
        ImGui::PushFont  (ImGui::GetIO ().Fonts->Fonts [1]);
        ImGui::InputTextMultiline (
          "###Handles", (char *)handle_dump.c_str (),
                                handle_dump.size  (),
            ImGui::GetContentRegionAvail (),
            ImGuiInputTextFlags_ReadOnly
        );
        ImGui::PopFont  ();
      }
    }

#if 0
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
                                         pInjectRecord->render.api );

          if (pInjectRecord->render.frames == 0 &&
              pInjectRecord->render.want_analysis)
          {
          }

          if (pInjectRecord->process.id == dwMonitored)
          {
            strncpy ( pInjectRecord->render.swapchain_analysis,
                      SKIF_PresentDebugStr [ReadAcquire (&SKIF_PresentIdx)].c_str (),
                      1024 );
          }
        }
        //ImGui::Text ("Count: %lu", ReadAcquire (&pInjectRecord->count));
      }
    }
#endif
  }

  /*
  if (hModSK64 != 0)
    FreeLibrary (hModSK64);
  */

  return
    S_OK;
}
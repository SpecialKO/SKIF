#include <tabs/ini_editor.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>
#include <algorithm>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/registry.h>
#include <ImGuiNotify.hpp>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

unsigned int window_identifier = 0;
std::vector <IniWindow> vIniWindow;
bool INIEditorActive = false;

// Number of recent searches remembered (shared across all editor windows)
static constexpr size_t INI_FILTER_HISTORY_MAX = 5;

// Moves (or adds) a search to the top of the filter history and stores it in the registry
static void
SKIF_UI_IniEditor_AddFilterHistory (const std::string& filter)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  if (filter.empty())
    return;

  auto& history = _registry.vecIniEditorFilterHistory;

  // Filtering is case-insensitive, so treat searches differing only in case as the same one
  std::erase_if (history, [&](const std::string& entry) {
    return _stricmp (entry.c_str(), filter.c_str()) == 0;
  });

  history.insert (history.begin(), filter);

  if (history.size() > INI_FILTER_HISTORY_MAX)
    history.resize (INI_FILTER_HISTORY_MAX);

  std::vector <std::wstring> wHistory;
  wHistory.reserve (history.size());
  for (const auto& entry : history)
    wHistory.push_back (SK_UTF8ToWideChar (entry));

  _registry.regKVIniEditorFilterHistory.putDataMultiSZ (wHistory);
}

// Whether the given text is one of the entries in the filter history
static bool
SKIF_UI_IniEditor_IsInFilterHistory (const char* text)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  for (const auto& entry : _registry.vecIniEditorFilterHistory)
    if (_stricmp (entry.c_str(), text) == 0)
      return true;

  return false;
}

// Up/Down in the filter field steps through the filter history
//   Keyboard: ImGui reports Up/Down arrows through ImGuiInputTextFlags_CallbackHistory
//   Gamepad:  ImGui does not, so D-pad/left stick presses are queued in iFilterHistoryStep and applied
//               here through ImGuiInputTextFlags_CallbackAlways (the buffer of an active field can only be changed here)
static int
SKIF_UI_IniEditor_FilterCallback (ImGuiInputTextCallbackData* data)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  IniWindow*  window  = static_cast <IniWindow*> (data->UserData);
  const auto& history = _registry.vecIniEditorFilterHistory;

  if (window == nullptr || history.empty())
    return 0;

  int step = 0;

  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    step = (data->EventKey == ImGuiKey_DownArrow) ? 1 : (data->EventKey == ImGuiKey_UpArrow) ? -1 : 0;

  else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways)
  {
    step = window->iFilterHistoryStep;
    window->iFilterHistoryStep = 0;
  }

  if (step == 0)
    return 0;

  const int prevPos = window->iFilterHistoryPos;
  const int maxPos  = static_cast <int> (history.size()) - 1;

  window->iFilterHistoryPos = std::clamp (window->iFilterHistoryPos + step, -1, maxPos);

  if (prevPos != window->iFilterHistoryPos)
  {
    // Stepping back past the first entry restores what was in the field to begin with
    const std::string& text = (window->iFilterHistoryPos == -1) ? window->strFilterOnActivate
                                                                : history [window->iFilterHistoryPos];

    data->DeleteChars (0, data->BufTextLen);
    data->InsertChars (0, text.c_str());
  }

  return 0;
}

void
IniWindow::ApplyFilter (void)
{
  bool default = (strlen (charFilter) == 0);

  for (auto& trie : ini)
  {
    trie._show = default;

    if (! default)
      trie._show = (StrStrIA (trie.section, charFilter) != NULL) ||
                   (StrStrIA (trie.key,     charFilter) != NULL) ||
                   (StrStrIA (trie.value,   charFilter) != NULL);
  }
}

void
IniWindow::ClearFilter (void)
{
  *charFilter    = '\0';
  *charFilterTmp = '\0';
  ApplyFilter ( );
}

void
IniWindow::UpdateWindowTitle (void)
{
  std::string filenameExt = "";

  if (! path_utf8.empty())
    filenameExt = std::filesystem::path(path_utf8).filename().string();

  path_filename = filenameExt;
  // In the language at the time (the title is updated when the file changes)
  doc_name      = (title.empty() ? (path_filename.empty() ? std::string (_("Untitled")) : path_filename) : title);
  // TRANSLATORS: Title of the editor of INI files of Special K; {0} is the name of the file (or of the game)
  wnd_name      = (F_("{0} - Editor", doc_name) + label); // + (bChanged ? "*" : "")
}

void
IniWindow::SetPath (const std::wstring& _p)
{
  path        = _p;
  path_utf8   = SK_WideCharToUTF8 (path);

  if (! path.empty())
    path_parent = std::filesystem::path(path).parent_path().wstring(); // full path to parent folder
  else
    path_parent = L"";
}

IniWindow::IniWindow (std::vector<IniEntry>  _i, IniType _ty, const std::wstring& _p, const std::string& _ti)
{
  ini         = std::move (_i);
  type        = _ty;
  title       = _ti;
  public_seed = SKIF_Util_RandomInteger ( );
  prompt_name = std::string (_("Save Changes?")) + SK_FormatString ("###SavePrompt_%i", public_seed);
  label       = ("###IniEditor-" + std::to_string (window_identifier));
  window_identifier++;
  SetPath           (_p);
  UpdateWindowTitle (  );
}

void
SKIF_UI_IniEditor_NewFile (IniWindow* iniWindow)
{
  std::vector <IniEntry> ini_parsed = SKIF_IniReader_BuildEntries (nullptr, dll_ini);

  iniWindow->watch.reset();
  iniWindow->title       = "";
  iniWindow->bChanged    = false;
  iniWindow->ini         = std::move (ini_parsed);
  iniWindow->want_action = EditorAction::None;
  iniWindow->SetPath           (L"");
  iniWindow->UpdateWindowTitle (   );
  iniWindow->ClearFilter       (   );
}

void
SKIF_UI_IniEditor_NewWindow (void)
{
  std::vector <IniEntry> ini_parsed = SKIF_IniReader_BuildEntries (nullptr, dll_ini);

  vIniWindow.emplace_back (std::move (ini_parsed), dll_ini);
}

void
SKIF_UI_IniEditor_Save (IniWindow* iniWindow)
{
  if (iniWindow->path.empty())
    return SKIF_UI_IniEditor_SaveAs (iniWindow);

  SKIF_IniReader_WriteIni (iniWindow->ini, iniWindow->path);

  for (auto& trie : iniWindow->ini)
  {
    trie.default_b = trie.value_b;
    strncpy (trie.default, trie.value, MAX_PATH);
  }

  iniWindow->saved       = SKIF_Util_timeGetTime ( );
  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_UI_IniEditor_SaveAs (IniWindow* iniWindow)
{
  std::wstring defaultFolderPath = (! iniWindow->path.empty() ? std::filesystem::path (iniWindow->path).parent_path().wstring() : L"");

  LPWSTR pwszFilePath = NULL;
  HRESULT hr          =
    SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { W_("Configuration Files"), L"*.ini" }, { W_("All files"), L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, defaultFolderPath.c_str(), L"ini");

  if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
    return;

  else if (SUCCEEDED (hr))
    iniWindow->SetPath (pwszFilePath);

  else
  {
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, W_("Unknown error attempting to retrieve file path!"), W_("Error"), MB_OK | MB_ICONEXCLAMATION);
    return;
  }

  // Save As should reset any custom titles set
  iniWindow->title.clear();

  SKIF_UI_IniEditor_Save (iniWindow);
}

void
SKIF_UI_IniEditor_OpenFile (IniWindow* iniWindow, IniType type, std::wstring path, const std::string& title)
{
  if (path.empty())
  {
    LPWSTR pwszFilePath = NULL;
    HRESULT hr          =
      SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { W_("Configuration Files"), L"*.ini" }, { W_("All files"), L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST);

    if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
      return;

    else if (SUCCEEDED (hr))
      path = pwszFilePath;

    else
    {
      MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, W_("Unknown error attempting to retrieve file path!"), W_("Error"), MB_OK | MB_ICONEXCLAMATION);
      return;
    }
  }

  std::wstring filename    = SKIF_Util_ToLowerW (std::filesystem::path(path).filename().replace_extension().wstring());
  std::wstring filenameExt = std::filesystem::path(path).filename().wstring();
  std:: string title_final = (title.empty() ? SK_WideCharToUTF8 (filenameExt) : title);

  // Check if the file has already been opened and if so focus that window
  if (iniWindow == nullptr)
  {
    for (auto& window : vIniWindow)
    {
      if (window.hwnd != nullptr &&
          window.path == path)
      {
        if (       IsIconic (       window.hwnd))
                 ShowWindow (       window.hwnd, SW_RESTORE);
        else if (! IsWindowVisible (window.hwnd))
                 ShowWindow (       window.hwnd, SW_SHOW);

        SetForegroundWindow (       window.hwnd);
        return;
      }
    }
  }

  if (type == IniType_Unknown)
  {
    if (     filename.find(L"specialk")      != std::wstring::npos ||
             filename.find(L"opengl32")      != std::wstring::npos ||
             filename.find( L"dinput8")      != std::wstring::npos ||
             filename.find(  L"dxgi"  )      != std::wstring::npos ||
           //filename.find(  L"d3d12" )      != std::wstring::npos || // Apparently Special K doesn't support being loaded as D3D12.dll
             filename.find(  L"d3d11" )      != std::wstring::npos ||
             filename.find(  L"d3d9"  )      != std::wstring::npos ||
             filename.find(  L"d3d8"  )      != std::wstring::npos ||
             filename.find(  L"ddraw" )      != std::wstring::npos)
      type = IniType_DLL;
    else if (filename.find(L"osd")           != std::wstring::npos)
      type = IniType_OSD;
    else if (filename.find(L"input")         != std::wstring::npos)
      type = IniType_Input;
    else if (filename.find(L"notifications") != std::wstring::npos)
      type = IniType_Notify;
    else if (filename.find(L"platform")      != std::wstring::npos)
      type = IniType_Platform;
    else if (filename.find(L"macros")        != std::wstring::npos)
      type = IniType_Macros;
  }

  std::vector <IniEntry> ini_parsed = SKIF_IniReader_ParseIni (path, type);

  if (iniWindow == nullptr)
    vIniWindow.emplace_back (std::move (ini_parsed), type, path, title_final);

  else {
    iniWindow->bChanged    = false;
    iniWindow->ini         = std::move (ini_parsed);
    iniWindow->title       = title_final;
    iniWindow->want_action = EditorAction::None;
    iniWindow->SetPath           (path);
    iniWindow->UpdateWindowTitle (    );
    iniWindow->ClearFilter       (    );
  }
}

void
SKIF_UI_IniEditor_Reload (IniWindow* iniWindow)
{
  iniWindow->ini         = SKIF_IniReader_ParseIni (iniWindow->path, iniWindow->type);
  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->ApplyFilter ( );
}

void
SKIF_UI_IniEditor_Reset (IniWindow* iniWindow)
{
  for (auto& trie : iniWindow->ini)
    trie.Reset();

  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_UI_IniEditor_Process (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  bool cleanup  = false,
       newWnd   = false;

  // Grace period after a user-initiated save (ms)
  static constexpr DWORD SAVE_GRACE_MS = 500;

  for (size_t i = 0; i < vIniWindow.size(); ++i)
  {
    IniWindow& window = vIniWindow[i];

    ImGui::PushID (window.public_seed);

    if (window.state == PopupState_Open)
    {
      ImGui::SetNextWindowSize (ImVec2 (950.0f, 750.0f) * SKIF_ImGui_MainDPIScale); // Opens centered on the main window

      // Further windows open a bit offset (by their position among the open ones), so they don't cover each other entirely
      extern ImRect windowRect;
      const float   fOffset = static_cast <float> ((i % 5) * 24) * SKIF_ImGui_MainDPIScale;
      ImGui::SetNextWindowPos (windowRect.GetCenter() + ImVec2 (fOffset, fOffset), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      window.state = PopupState_Opened;
    }

    bool show     =  true,
         newFile  = false,
         openFile = false,
         save     = false,
         saveAs   = false,
         reload   = false,
         reset    = false,
         minimize = false;

    ImGuiWindowClass
      wc;
      wc.ClassId = ImGui::GetID ("IniEditorClass");
      wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge; // Work around a pesky bug where the viewport tries to merge into the... debug fallback window?
    ImGui::SetNextWindowClass (&wc);
    ImGui::SetNextWindowSizeConstraints (ImVec2 (640.0f, 480.0f) * SKIF_ImGui_GetDPIScaleForWindow (window.wnd_name.c_str()), ImVec2 (FLT_MAX, FLT_MAX));

    ImGui::Begin (window.wnd_name.c_str(),
                      &show,
                    //ImGuiWindowFlags_NoResize          |
                      ImGuiWindowFlags_NoCollapse        |
                    //ImGuiWindowFlags_NoTitleBar        |
                    //ImGuiWindowFlags_NoScrollWithMouse | // Prevent scrolling with the mouse as well
                      ImGuiWindowFlags_NoScrollbar       |
                      ImGuiWindowFlags_NoSavedSettings   |
                      ImGuiWindowFlags_MenuBar           |
 ((window.bChanged) ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None)
    );

    if (window.hwnd == nullptr && ImGui::GetWindowViewport()->PlatformHandleRaw)
      window.hwnd = (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw;

    if (window.hwnd != nullptr && window.watch.getPath().empty() && ! window.path_parent.empty())
      window.watch = SKIF_DirectoryWatch (window.path_parent, UITab_None, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);

    if (window.watch.isSignaled())
    {
      if (! window.bChanged && ! (window.saved != NULL && (SKIF_Util_timeGetTime() - window.saved) < SAVE_GRACE_MS))
        window.watchCD = SKIF_Util_timeGetTime() + 50;
    }

    if (window.watchCD != NULL && window.watchCD < SKIF_Util_timeGetTime())
    {
      window.watchCD = NULL;

      if (! (window.saved != NULL && (SKIF_Util_timeGetTime() - window.saved) < SAVE_GRACE_MS))
      {
        window.ini = SKIF_IniReader_ParseIni (window.path, window.type);

        // Apply the active filter
        window.ApplyFilter ( );

        ImGui::InsertNotification ({
          ImGuiToastType::Info, 1000,
          _("File was reloaded."),
          ""
        });
      }
    }

    window.bFocused = SKIF_ImGui_IsViewportFocused ( );

    bool collapseAll = false;

    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu (L_("File")))
      {
        if (ImGui::MenuItem (L_("New"), "Ctrl+N"))
          newFile  = true;

        if (ImGui::MenuItem (L_("New Window"), "Ctrl+Shift+N"))
          newWnd   = true;

        if (ImGui::MenuItem (L_("Open"), "Ctrl+O"))
          openFile = true;

        if (ImGui::MenuItem (L_("Save"), "Ctrl+S"))
          save     = true;

        if (ImGui::MenuItem (L_("Save As"), "Ctrl+Shift+S"))
          saveAs   = true;

        if (ImGui::MenuItem (L_("Close"), C_("key", "Escape")))
          show     = false;

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu (L_("Edit")))
      {
        if (! window.bChanged)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem (L_("Undo Changes")))
          reset = true;

        if (! window.bChanged)
          SKIF_ImGui_PopDisableState ( );

        ImGui::Separator ( );

        if (window.path.empty())
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem (L_("Reload File")))
          reload = true;

        if (window.path.empty())
          SKIF_ImGui_PopDisableState ( );

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu (L_("View")))
      {
        if (ImGui::MenuItem (L_("Collapse/Expand")))
        {
          if (window.collapsed_sections.empty())
            collapseAll = true;
          else
            window.collapsed_sections.clear();
        }

        ImGui::Separator ( );

        if (ImGui::MenuItem (L_("Unused Settings"), "", window.bViewUnused))
          window.bViewUnused = ! window.bViewUnused;

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu (L_("Tools")))
      {
        if (window.path.empty())
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem (L_("External Editor")))
          SKIF_Util_OpenURI (window.path, SW_SHOWNORMAL, NULL);

        if (window.path.empty())
          SKIF_ImGui_PopDisableState ( );

        ImGui::EndMenu();
      }

      ImGui::EndMenuBar ( );
    }

    // Some additional vertical padding
    ImGui::SetCursorPosY   (
      ImFloor (ImGui::GetCursorPosY ( ) + 3.0f * SKIF_ImGui_GlobalDPIScale)
    );

    // The content is drawn in a centered column with a max width that the scrollbar never moves (see the content area
    //   below); the filter row lines up with that column. The content area is as wide as this row and pads its content by FramePadding.x
    const float fColumnMaxWidth = 900.0f * SKIF_ImGui_GlobalDPIScale;
    float       fColumnOffset   = 0.0f,
                fColumnWidth    = 0.0f;

    SKIF_ImGui_CalcCenteredColumn (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().FramePadding.x * 2.0f, fColumnMaxWidth, &fColumnOffset, &fColumnWidth);

    const float fTopRowX   = ImGui::GetCursorPosX ( );

    bool showClearBtn      = (window.charFilter[0] != '\0');
    // Mirrors what ImGui::ButtonEx() does to calculate the height of buttons
    float fTopZoomX        =                           ImGui::CalcTextSize (ICON_FA_MAGNIFYING_GLASS).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float fTopClearX       = (! showClearBtn ? 0.0f :  ImGui::CalcTextSize (ICON_FA_XMARK ).x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x);
  //float fTopFilterX      =                           ImGui::CalcTextSize (ICON_FA_FILTER).x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float fTopFilterFieldX = std::max (1.0f, fColumnWidth - fTopZoomX - fTopClearX); //  - fTopFilterX
    //static bool bFilterHovered = false;

    ImGui::PushStyleColor (ImGuiCol_Border,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

    ImGui::SetCursorPosX  (fTopRowX + ImGui::GetStyle().FramePadding.x + fColumnOffset);
    ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
    ImGui::Button         (ICON_FA_MAGNIFYING_GLASS);
    ImGui::PopItemFlag    ( );
    ImGui::SameLine       ( );

    // Requesting the history callback makes ImGui stop using up/down for navigation while the field is active, for the
    //   gamepad as well, but only the keyboard arrows are passed on to the callback. Queue gamepad up/down presses
    //     ourselves and apply them in the callback, so a gamepad can step through the history as well
    ImGuiInputTextFlags filterFlags = ImGuiInputTextFlags_AutoSelectAll;

    if (! _registry.vecIniEditorFilterHistory.empty())
    {
      filterFlags |= ImGuiInputTextFlags_CallbackHistory;

      if (ImGui::GetActiveID ( ) == ImGui::GetID ("###AppListFilterField"))
      {
        if (     ImGui::IsKeyPressed (ImGuiKey_GamepadDpadDown) || ImGui::IsKeyPressed (ImGuiKey_GamepadLStickDown))
          window.iFilterHistoryStep =  1;
        else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadUp)   || ImGui::IsKeyPressed (ImGuiKey_GamepadLStickUp))
          window.iFilterHistoryStep = -1;
      }

      if (window.iFilterHistoryStep != 0)
        filterFlags |= ImGuiInputTextFlags_CallbackAlways;
    }

    ImGui::PushStyleColor (ImGuiCol_FrameBg, ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg));
    ImGui::InputTextEx ("###AppListFilterField", "", window.charFilterTmp, MAX_PATH,
                        ImVec2 (fTopFilterFieldX, 0.0f),
                        filterFlags, SKIF_UI_IniEditor_FilterCallback, &window);
    ImGui::PopStyleColor  ( ); // ImGuiCol_FrameBg
    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    // Drop a queued step the callback did not get to (e.g. the field was deactivated on the same frame)
    window.iFilterHistoryStep = 0;

    // Used to show the filter history right below the field
    const ImVec2 vFilterFieldMin    = ImGui::GetItemRectMin ( );
    const ImVec2 vFilterFieldMax    = ImGui::GetItemRectMax ( );
    const bool   bFilterFieldActive = ImGui::IsItemActive   ( );

    if (ImGui::IsItemActivated ( ))
    {
      window.strFilterOnActivate = window.charFilterTmp;
      window.iFilterHistoryPos   = -1;
    }

    // Remember the search once the user is done with the field (Enter, clicking elsewhere, etc.)
    //   Escape reverts the field, so it will not be remembered
    else if (ImGui::IsItemDeactivated ( ) && window.strFilterOnActivate != window.charFilterTmp)
      SKIF_UI_IniEditor_AddFilterHistory (window.charFilterTmp);

    // Ctrl+F should focus filter field
    if (window.bFocused && ImGui::GetIO().KeyCtrl && ImGui::GetKeyData (ImGuiKey_F)->DownDuration == 0.0f)
    {
      ImGui::ActivateItemByID (ImGui::GetID("###AppListFilterField"));
      ImGui::SetFocusID       (ImGui::GetID("###AppListFilterField"), ImGui::GetCurrentWindow());
    }

    // This is required to prevent the InputTextEx from not being deselected when clicking empty space
    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    window.bFilterActive = ImGui::IsItemActive ( ) || ImGui::IsItemFocused ( );

    if (ImGui::IsItemFocused ( ) && ! ImGui::IsItemHovered( ) && ImGui::IsAnyMouseDown( ))
    {
      // Clear highlight from filter box
      ImGuiContext& g = *ImGui::GetCurrentContext();
      g.NavCursorVisible = true;
    }

    // Preprocess filtered entries
    if (strncmp (window.charFilter, window.charFilterTmp, MAX_PATH) != 0)
    {
      if (strlen (window.charFilterTmp) == 0)
        window.ClearFilter ( );
      else {
        strncpy (window.charFilter, window.charFilterTmp, MAX_PATH);
        window.ApplyFilter ( );
      }
    }

    if (showClearBtn)
    {
      ImGui::SameLine       ( );

      // Needed to stop flickering on the same frame as the field is emptied
      bool justCleared = (window.charFilter[0] == '\0');
      static bool btnClearHover = false;

      if (justCleared)
        ImGui::PushStyleColor (ImGuiCol_Text, ImVec4(0, 0, 0, 0));
      else if (btnClearHover)
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
      else
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));

      if (ImGui::Button (ICON_FA_XMARK))
        window.ClearFilter ( );

      ImGui::PopStyleColor ( ); // ImGuiCol_Text

      btnClearHover = ImGui::IsItemHovered() || ImGui::IsItemActive();
    }

    ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonActive
    ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonHovered
    ImGui::PopStyleColor  ( ); // ImGuiCol_Button
    ImGui::PopStyleColor  ( ); // ImGuiCol_Border

    // Some additional vertical padding
    ImGui::SetCursorPosY   (
      ImFloor (ImGui::GetCursorPosY ( ) + 3.0f * SKIF_ImGui_GlobalDPIScale)
    );

    ImVec2 fTop2 = ImGui::GetCursorPos ( );

    // End top options

#if 0
    // Headers
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                            );

    ImGui::ItemSize        (ImVec2 (5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    //ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "[Section]");
    //ImGui::SameLine        ( );
    //ImGui::ItemSize        (ImVec2 (208.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    //ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "Key=");
    ImGui::SameLine        ( );
    //ImGui::ItemSize        (ImVec2 (611.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::ItemSize        (ImVec2 (308.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "Value");

    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    ImGui::Separator   ( );
#endif

    SKIF_ImGui_BeginChildFrame (
      ImGui::GetID ("###SKIE_CONTENT_AREA"),
      ImVec2 (0.0f, ImFloor (ImGui::GetWindowSize().y - ImGui::GetCursorPosY() - 20.0f * SKIF_ImGui_GlobalDPIScale - 2.0f * SKIF_ImGui_GlobalDPIScale)), // 900.0f
      ImGuiChildFlags_NavFlattened, // ImGuiChildFlags_FrameStyle
      ImGuiWindowFlags_None
    );

    SKIF_ImGui_BeginCenteredColumn ("###SKIE_CONTENT_COLUMN", fColumnMaxWidth);

    SKIF_ImGui_BeginHighlightLine ( );

    std::string section;

    for (auto& trie : window.ini)
    {
      if (! trie._show)
        continue;

      if (trie._ignore && ! window.bViewUnused)
        continue;

      const ImGuiID section_id = ImGui::GetID (trie.section);
      const bool    collapsed  = window.collapsed_sections.contains (section_id);

      if (section != trie.section)
      {
        section = trie.section;

        SKIF_ImGui_Spacing                ( );
        if (SKIF_ImGui_SeparatorTextClickable (trie.section, &collapsed))
        {
          if (! window.collapsed_sections.insert (section_id).second)
                window.collapsed_sections.erase  (section_id);
        }
      }

      if (collapseAll && ! collapsed)
        window.collapsed_sections.insert (ImGui::GetID (trie.section));

      if (collapsed)
        continue;

      ImGui::PushID (&trie);

      SKIF_ImGui_PushHighlightLineLine ( );
      
      ImGui::BeginGroup      ( );
      ImGui::Spacing         ( );
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), trie.key);
      if (trie.description != nullptr)
        SKIF_ImGui_SetHoverTip (trie.description);
      ImGui::SameLine        ( );
      //ImGui::ItemSize        (ImVec2 (600.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize        (ImVec2 (300.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      if (trie.DrawFunction != nullptr && trie.DrawFunction (&trie))
      {
        window.bChanged = true;
        window.UpdateWindowTitle();
      }
      ImGui::EndGroup        ( );

      SKIF_ImGui_PopHighlightLineLine ( );

      ImGui::PopID ( );
    }

    SKIF_ImGui_EndHighlightLine ( );

    if (! window.bViewUnused)
    {
      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );

      const char*       szText   = _("Unused settings are hidden by default.");
      const std::string szButton = std::string ("   ") + _("View unused settings") + "   ###ViewUnusedSettings";

      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f));

      SKIF_ImGui_CenterNextItem (ImGui::CalcTextSize (szText).x);
      ImGui::TextUnformatted    (szText);

      ImGui::PopStyleColor ( ); // ImGuiCol_Text

      SKIF_ImGui_Spacing ( );

      SKIF_ImGui_CenterNextItem (ImGui::CalcTextSize (szButton.c_str ( ), nullptr, true).x + ImGui::GetStyle ().FramePadding.x * 2.0f);
      if (ImGui::Button         (szButton.c_str ( )))
        window.bViewUnused = ! window.bViewUnused;

      SKIF_ImGui_Spacing ( );
      SKIF_ImGui_Spacing ( );
    }

    // Drag-scrolling on touch devices also works over the column (anywhere, as below)
    SKIF_ImGui_EndCenteredColumn (true, false);

    // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
    SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

    ImGui::EndChild ( );

    // Filter history
    //   Shown right below the filter field while it is selected (until the user types something new). It is drawn after
    //     the content area so it overlaps it. Clicks are checked by hand, since the active filter field would otherwise
    //       block the entries from reacting on the very frame the user clicks one
    {
      const auto& history = _registry.vecIniEditorFilterHistory;

      const bool bTyped     = (window.charFilterTmp[0] != '\0'                       &&
                               window.strFilterOnActivate != window.charFilterTmp     &&
                               ! SKIF_UI_IniEditor_IsInFilterHistory (window.charFilterTmp));

      // Clicking an entry deactivates the filter field, so keep the list around while the mouse is being pressed on it
      const bool bKeepAlive = (window.bFilterHistoryHovered &&
                              (ImGui::IsMouseClicked (ImGuiMouseButton_Left) || ImGui::IsMouseDown (ImGuiMouseButton_Left)));

      window.bFilterHistoryHovered = false;

      if (! history.empty ( ) && ! bTyped && (bFilterFieldActive || bKeepAlive))
      {
        int iClicked = -1;

        ImGui::SetCursorScreenPos (ImVec2 (vFilterFieldMin.x, vFilterFieldMax.y + 2.0f * SKIF_ImGui_GlobalDPIScale));

        ImGui::PushStyleColor (ImGuiCol_ChildBg, ImGui::GetStyleColorVec4 (ImGuiCol_PopupBg));

        if (ImGui::BeginChild ("###IniFilterHistory", ImVec2 (vFilterFieldMax.x - vFilterFieldMin.x, 0.0f),
                                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav))
        {
          ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing, ImVec2 (ImGui::GetStyle().ItemSpacing.x, 0.0f));

          const ImVec2 vPadding = ImGui::GetStyle().FramePadding;

          for (int iEntry = 0; iEntry < static_cast <int> (history.size()); iEntry++)
          {
            const ImVec2 vRowMin  = ImGui::GetCursorScreenPos ( );
            const ImVec2 vRowSize = ImVec2 (ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight ( ) + vPadding.y * 2.0f);
            const ImVec2 vRowMax  = vRowMin + vRowSize;

            ImGui::PushID (iEntry);
            ImGui::Dummy  (vRowSize);
            ImGui::PopID  ( );

            const bool bHovered  = ImGui::IsMouseHoveringRect (vRowMin, vRowMax);
            const bool bSelected = (iEntry == window.iFilterHistoryPos);

            if (bHovered || bSelected)
              ImGui::GetWindowDrawList()->AddRectFilled (vRowMin, vRowMax,
                ImGui::GetColorU32 (bHovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header), ImGui::GetStyle().FrameRounding);

            // Drawn as plain text so "##" in a search is shown as-is rather than treated as an ID separator
            ImGui::RenderTextClipped (vRowMin + vPadding, vRowMax - vPadding, history[iEntry].c_str(), nullptr, nullptr, ImVec2 (0.0f, 0.5f));

            if (bHovered && ImGui::IsMouseClicked (ImGuiMouseButton_Left))
              iClicked = iEntry;
          }

          ImGui::PopStyleVar ( ); // ImGuiStyleVar_ItemSpacing
        }
        ImGui::EndChild ( );

        ImGui::PopStyleColor ( ); // ImGuiCol_ChildBg

        window.bFilterHistoryHovered = ImGui::IsMouseHoveringRect (ImGui::GetItemRectMin ( ), ImGui::GetItemRectMax ( ));

        // The cursor is intentionally not restored: nothing below relies on it (the minimize button positions itself),
        //   and moving it back past the last item would trip ImGui's "SetCursorPos() to extend boundaries" check

        if (iClicked != -1)
        {
          // Copy first, as adding it to the history reorders the vector
          const std::string filter = history [iClicked];

          strncpy (window.charFilterTmp, filter.c_str(), MAX_PATH);
          window.charFilterTmp[MAX_PATH] = '\0';
          window.strFilterOnActivate     = filter; // Prevents it from being added a second time on deactivation
          window.iFilterHistoryPos       = -1;
          window.bFilterHistoryHovered   = false;  // Close the list

          SKIF_UI_IniEditor_AddFilterHistory (filter);
        }
      }
    }

    // Minimize button

    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      minimize = true;

    // Focus + Hotkeys
    extern bool bKeepWindowAlive;

    if (window.bFocused && ! g_activeKeybindPopup)
    {
           if (! window.bFilterActive &&
              (ImGui::IsKeyPressed (ImGuiKey_Escape) ||
              (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_W)->DownDuration == 0.0f)))           show = false; // Escape / Ctrl+W
           if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_O)->DownDuration == 0.0f)         openFile =  true; // Ctrl+O
           if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::GetKeyData (ImGuiKey_N)->DownDuration == 0.0f)           newWnd =  true; // Ctrl+Shift+N
      else if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_N)->DownDuration == 0.0f)          newFile =  true; // Ctrl+N
           if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f)           saveAs =  true; // Ctrl+Shift+S
      else if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f)             save =  true; // Ctrl+S
           if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_Q)->DownDuration == 0.0f) bKeepWindowAlive = false; // Ctrl+Q
    }

    // Suppress some actions if we have unsaved changes
    if (window.bChanged)
    {
      if (newFile)
      {   newFile = false;
        window.want_action = EditorAction::New;
        window.prompt_show = true;
      }

      if (openFile)
      {   openFile = false;
        window.want_action = EditorAction::Open;
        window.prompt_show = true;
      }

      if (reload)
      {   reload = false;
        window.want_action = EditorAction::Reload;
        window.prompt_show = true;
      }

      if (! show)
      {     show = true;
        window.want_action = EditorAction::Close;
        window.prompt_show = true;
      }

      if (! bKeepWindowAlive)
      {     bKeepWindowAlive = true;
        window.want_action = EditorAction::Exit;
        window.prompt_show = true;
      }

      // Handle unsaved changes
      if (window.want_action != EditorAction::None)
      {
        auto _PerformAction = [&]() -> void
        {
          switch (window.want_action)
          {
            case EditorAction::New:
              newFile  =  true;
              break;

            case EditorAction::Open:
              openFile =  true;
              break;

            case EditorAction::Close:
              show     = false;
              break;

            case EditorAction::Reload:
              reload   =  true;
              break;

            case EditorAction::Exit:
              bKeepWindowAlive = false;
              break;
          }

          window.want_action = EditorAction::None;
        };

        // TRANSLATORS: {0} is the name of a file (or of a game)
        std::string message = F_("Do you want to save changes to {0}?", window.doc_name);
        switch (SKIF_ImGui_SaveChangesPrompt (window.prompt_name.c_str(), &window.prompt_show, message.c_str()))
        {
          case DChoice_Save:
            _PerformAction ( );
            save = true;
            break;
          case DChoice_DontSave:
            _PerformAction ( );
            break;
          case DChoice_Cancel:
            window.want_action = EditorAction::None;
            _PerformAction ( );
            break;
        }
      }
    }

    // Actions

    if (reload)
      SKIF_UI_IniEditor_Reload   (&window);

    if (reset)
      SKIF_UI_IniEditor_Reset    (&window);

    if (save)
      SKIF_UI_IniEditor_Save     (&window);

    if (saveAs)
      SKIF_UI_IniEditor_SaveAs   (&window);

    if (openFile)
      SKIF_UI_IniEditor_OpenFile (&window, IniType_Unknown, L"", "");

    if (newFile)
      SKIF_UI_IniEditor_NewFile  (&window);

    if (minimize)
      ShowWindowAsync ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, SW_MINIMIZE);

    if (! show)
    {
      window.state = PopupState_Closed;
      cleanup = true;
    }

    // Process notifications for this window
    ImGui::RenderNotifications ();

    // End Editor window
    ImGui::End   ( );

    ImGui::PopID ( );
  }

  if (newWnd)
    SKIF_UI_IniEditor_NewWindow ( );

  INIEditorActive = false;
  for (auto& window : vIniWindow)
    INIEditorActive = (INIEditorActive || window.bFocused);

  if (INIEditorActive)
  {
    extern bool allowShortcutCtrlA;
    allowShortcutCtrlA = false;
  }

  // We need to clean up windows after setting INIEditorActive
  //   as otherwise hotkeys such as Ctrl+W will survive the window
  //     being closed and will close the main SKIF window instead
  if (cleanup)
  {
    std::erase_if(vIniWindow, [](const auto& window) {
      return window.state == PopupState_Closed;
    });
  }
}
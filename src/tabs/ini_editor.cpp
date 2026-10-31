#include <tabs/ini_editor.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/registry.h>
#include <utility/hotkeys.h>
#include <utility/ini_presets.h>
#include <ImGuiNotify.hpp>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

unsigned int window_identifier = 0;
std::vector <IniWindow> vIniWindow;

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

// The filter history is shared across all editor windows (and stored in the registry)
static void
SKIF_UI_IniEditor_SaveFilterHistory (const std::vector <std::wstring>& entries)
{
  SKIF_RegistrySettings::GetInstance ( ).regKVIniEditorFilterHistory.putDataMultiSZ (entries);
}

IniWindow::IniWindow (std::vector<IniEntry>  _i, IniType _ty, const std::wstring& _p, const std::string& _ti)
  : filterHistory (&SKIF_RegistrySettings::GetInstance ( ).vecIniEditorFilterHistory, SKIF_UI_IniEditor_SaveFilterHistory)
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
  iniWindow->save_folder.clear ( );
  iniWindow->bChanged    = false;
  iniWindow->ini         = std::move (ini_parsed);
  iniWindow->undo_snapshot.reset ( );
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

// A new custom preset: an empty document whose first save asks for a name in the Custom presets folder (nothing is
//   written until then, so no placeholder file is left behind or reused)
void
SKIF_UI_IniEditor_NewPreset (void)
{
  std::vector <IniEntry> ini_parsed = SKIF_IniReader_BuildEntries (nullptr, dll_ini);

  IniWindow& window  = vIniWindow.emplace_back (std::move (ini_parsed), dll_ini, L"", _("New Custom Preset"));
  window.save_folder = SKIF_IniPresets_GetCustomFolder ( );

  std::error_code ec;
  // Create any missing directories, so Save As can open in the folder
  if (! std::filesystem::exists             (window.save_folder, ec))
        std::filesystem::create_directories (window.save_folder, ec);
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
  iniWindow->undo_snapshot.reset ( );
  iniWindow->want_action = EditorAction::None;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_UI_IniEditor_SaveAs (IniWindow* iniWindow)
{
  std::wstring defaultFolderPath = (! iniWindow->path.empty() ? std::filesystem::path (iniWindow->path).parent_path().wstring() : iniWindow->save_folder);

  LPWSTR pwszFilePath = NULL;
  HRESULT hr          =
    SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { W_("Configuration Files"), L"*.ini" }, { W_("All files"), L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN, FOLDERID_ComputerFolder, defaultFolderPath.c_str(), L"ini");

  if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
    return;

  else if (SUCCEEDED (hr) && pwszFilePath != NULL)
  {
    iniWindow->SetPath (pwszFilePath);
    CoTaskMemFree      (pwszFilePath);
  }

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
    iniWindow->undo_snapshot.reset ( );
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
  iniWindow->undo_snapshot.reset ( );
  iniWindow->ApplyFilter ( );
}

void
SKIF_UI_IniEditor_Reset (IniWindow* iniWindow)
{
  // After a preset was applied, the entries as last saved are restored
  if (iniWindow->undo_snapshot.has_value ( ))
  {
    iniWindow->ini = std::move (*iniWindow->undo_snapshot);
    iniWindow->undo_snapshot.reset ( );
    iniWindow->ApplyFilter ( );
  }

  else
  {
    for (auto& trie : iniWindow->ini)
      trie.Reset();
  }

  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->UpdateWindowTitle ( );
}

// Replaces the entries of the document with the ones of a preset, as unsaved changes (Save writes them to the file,
//   Undo Changes restores the entries as last saved)
void
SKIF_UI_IniEditor_ApplyPreset (IniWindow* iniWindow, std::vector<IniEntry> entries)
{
  // Only kept the first time, so applying another preset (or editing in between) still reverts to the saved state
  if (! iniWindow->undo_snapshot.has_value ( ))
  {
    iniWindow->undo_snapshot = iniWindow->ini;

    // Unsaved edits are not part of the saved state (this is what Undo Changes would have reverted them to)
    if (iniWindow->bChanged)
      for (auto& trie : *iniWindow->undo_snapshot)
        trie.Reset();
  }

  iniWindow->ini      = std::move (entries);
  iniWindow->bChanged = true;
  iniWindow->ApplyFilter       ( );
  iniWindow->UpdateWindowTitle ( );
}

// Saves a copy of the document as a custom preset (the Custom presets folder is suggested). The document itself is left
//   as it is: it keeps its own file, and any unsaved changes stay unsaved
static void
SKIF_UI_IniEditor_SaveAsPreset (IniWindow* iniWindow)
{
  const std::wstring& folder = SKIF_IniPresets_GetCustomFolder ( );

  std::error_code ec;
  // Create any missing directories, so the dialog can open in the folder
  if (! std::filesystem::exists             (folder, ec))
        std::filesystem::create_directories (folder, ec);

  LPWSTR pwszFilePath = NULL;
  HRESULT hr          =
    SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { W_("Configuration Files"), L"*.ini" }, { W_("All files"), L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN, FOLDERID_ComputerFolder, folder.c_str(), L"ini");

  if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
    return;

  else if (FAILED (hr) || pwszFilePath == NULL)
  {
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, W_("Unknown error attempting to retrieve file path!"), W_("Error"), MB_OK | MB_ICONEXCLAMATION);
    return;
  }

  const std::wstring path = pwszFilePath;
  CoTaskMemFree (pwszFilePath);

  // Saving over the document's own file is a regular save
  if (! iniWindow->path.empty() && _wcsicmp (path.c_str(), iniWindow->path.c_str()) == 0)
    return SKIF_UI_IniEditor_Save (iniWindow);

  SKIF_IniReader_WriteIni (iniWindow->ini, path);
  SKIF_IniPresets_Refresh ( );

  ImGui::InsertNotification ({
    ImGuiToastType::Success, 3000,
    // TRANSLATORS: Notification title, at most 63 characters
    _("Custom preset saved"),
    "%s", SK_WideCharToUTF8 (std::filesystem::path (path).filename().wstring()).c_str()
  });
}

// Builds the entries of a preset for a document of the given type. Returns false if the preset could not be read
static bool
SKIF_UI_IniEditor_BuildPreset (const SKIF_IniPresetChoice& preset, IniType type, std::vector<IniEntry>& entries)
{
  switch (preset.action)
  {
    case SKIF_IniPresetAction_Apply:
      entries = SKIF_IniReader_ParseIni (preset.path, type);
      return (! entries.empty ( )); // Parse errors are already reported by the INI reader

    case SKIF_IniPresetAction_Compatibility:
    {
      inih::INIReader reader;

      try {
        reader.ParseContent (SK_WideCharToUTF8 (cfg_compatibility));
      } catch (const std::exception& e) {
        PLOG_ERROR << "Failed to parse the compatibility config! Error: " << e.what ();
        return false;
      };

      entries = SKIF_IniReader_BuildEntries (&reader, type);
      return true;
    }

    case SKIF_IniPresetAction_Reset:
      entries = SKIF_IniReader_BuildEntries (nullptr, type);
      return true;

    default:
      return false;
  }
}

void
SKIF_UI_IniEditor_Process (void)
{
  bool cleanup    = false,
       newWnd     = false,
       newPreset  = false;

  // A preset to open in a new editor window (opened after all windows have been processed, as opening one adds to vIniWindow)
  std::wstring editPreset;

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
         minimize = false,
         saveAsPreset = false;

    SKIF_IniPresetChoice preset;

    // Presets are configs of Special K itself, so not for the other INI files (OSD, input, notifications, ...)
    const bool presetsDisabled = (window.type != IniType_DLL && window.type != IniType_Unknown);

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
        window.undo_snapshot.reset ( );

        // Apply the active filter
        window.ApplyFilter ( );

        ImGui::InsertNotification ({
          ImGuiToastType::Info, 1000,
          _("File was reloaded."),
          ""
        });
      }
    }

    bool collapseAll = false;

    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu (L_("File")))
      {
        // The hotkeys shown are the ones checked further down ("Hotkeys"), see include/utility/hotkeys.h
        if (ImGui::MenuItem (L_("New"),        ImGui::GetKeyChordName (SKIF_Hotkey_IniNew)))
          newFile  = true;

        if (ImGui::MenuItem (L_("New Window"), ImGui::GetKeyChordName (SKIF_Hotkey_IniNewWindow)))
          newWnd   = true;

        if (ImGui::MenuItem (L_("Open"),       ImGui::GetKeyChordName (SKIF_Hotkey_IniOpen)))
          openFile = true;

        if (ImGui::MenuItem (L_("Save"),       ImGui::GetKeyChordName (SKIF_Hotkey_IniSave)))
          save     = true;

        if (ImGui::MenuItem (L_("Save As"),    ImGui::GetKeyChordName (SKIF_Hotkey_IniSaveAs)))
          saveAs   = true;

        if (presetsDisabled)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem (L_("Save as Custom Preset...")))
          saveAsPreset = true;

        if (presetsDisabled)
          SKIF_ImGui_PopDisableState ( );

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

        // Apply Preset, Apply Compatibility Config and Reset (shared with the Library tab, see <utility/ini_presets.h>)
        if (presetsDisabled)
          SKIF_ImGui_PushDisableState ( );

        preset = SKIF_ImGui_IniPresetItems (false);

        if (presetsDisabled)
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

    ImGui::PushStyleColor (ImGuiCol_FrameBg, ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg));
    window.filterHistory.InputText ("###AppListFilterField", window.charFilterTmp, MAX_PATH,
                                    ImVec2 (fTopFilterFieldX, 0.0f),
                                    ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor  ( ); // ImGuiCol_FrameBg
    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    // Ctrl+F or F3 should focus filter field (both checked every frame, which keeps their routes registered)
    const bool hotkeyFilter    = ImGui::Shortcut (SKIF_Hotkey_Filter);
    const bool hotkeyFilterAlt = ImGui::Shortcut (SKIF_Hotkey_FilterAlt);

    if (hotkeyFilter || hotkeyFilterAlt)
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

    // Filter history (drawn after the content area so it overlaps it). The cursor is not restored: nothing below relies
    //   on it (the minimize button positions itself), and ImGui would otherwise warn about extending the window boundaries
    window.filterHistory.DrawList (window.charFilterTmp, MAX_PATH);

    // Minimize button

    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      minimize = true;

    // Hotkeys (include/utility/hotkeys.h)
    //   With Shortcut ( ): only while this window has the focus (not while one of its menus or its Save Changes prompt has
    //     it), and Ctrl+Q / Ctrl+Shift+N take priority over the app-wide hotkeys of the same keys (Ctrl+Q so unsaved changes
    //       are asked about first). Each Shortcut ( ) is called every frame, as that is what keeps its route registered
    extern bool bKeepWindowAlive;

    if (! g_activeKeybindPopup)
    {
      if (ImGui::Shortcut (ImGuiKey_Escape) &&
                 ! window.bFilterActive)                        show             =  false; // Escape (not while the filter field is active, where it reverts the field)
      if (ImGui::Shortcut (SKIF_Hotkey_CloseWindow))            show             =  false; // Ctrl+W (also while the filter field is active)
      if (ImGui::Shortcut (SKIF_Hotkey_IniNew))                 newFile          =   true; // Ctrl+N
      if (ImGui::Shortcut (SKIF_Hotkey_IniNewWindow))           newWnd           =   true; // Ctrl+Shift+N
      if (ImGui::Shortcut (SKIF_Hotkey_IniOpen))                openFile         =   true; // Ctrl+O
      if (ImGui::Shortcut (SKIF_Hotkey_IniSave))                save             =   true; // Ctrl+S
      if (ImGui::Shortcut (SKIF_Hotkey_IniSaveAs))              saveAs           =   true; // Ctrl+Shift+S
      if (ImGui::Shortcut (SKIF_Hotkey_Quit))                   bKeepWindowAlive =  false; // Ctrl+Q
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

    if (preset.action == SKIF_IniPresetAction_Edit)
      editPreset = preset.path;

    else if (preset.action == SKIF_IniPresetAction_New)
      newPreset  = true;

    else if (preset.action != SKIF_IniPresetAction_None)
    {
      std::vector <IniEntry> entries;

      if (SKIF_UI_IniEditor_BuildPreset (preset, window.type, entries))
        SKIF_UI_IniEditor_ApplyPreset  (&window, std::move (entries));
    }

    if (save)
      SKIF_UI_IniEditor_Save     (&window);

    if (saveAs)
      SKIF_UI_IniEditor_SaveAs   (&window);

    if (saveAsPreset)
      SKIF_UI_IniEditor_SaveAsPreset (&window);

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

  if (newPreset)
    SKIF_UI_IniEditor_NewPreset ( );

  if (! editPreset.empty ( ))
    SKIF_UI_IniEditor_OpenFile (nullptr, IniType_DLL, editPreset, "");

  // Closed windows are removed last, after all of them have been processed this frame
  if (cleanup)
  {
    std::erase_if(vIniWindow, [](const auto& window) {
      return window.state == PopupState_Closed;
    });
  }
}
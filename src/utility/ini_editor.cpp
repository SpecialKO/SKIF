#include <utility/ini_editor.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <ImGuiNotify.hpp>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

unsigned int window_identifier = 0;
std::vector <IniWindow> vIniWindow;
bool INIEditorActive = false;

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
  strncpy (charFilter,    "\0", MAX_PATH);
  strncpy (charFilterTmp, "\0", MAX_PATH);
  ApplyFilter ( );
}

void
IniWindow::UpdateWindowTitle (void)
{
  std::string filenameExt = "";

  if (! path_utf8.empty())
    filenameExt = std::filesystem::path(path_utf8).filename().string();

  path_filename = filenameExt;
  doc_name      = (title.empty() ? (path_filename.empty() ? "Untitled" : path_filename) : title);
  wnd_name      = (doc_name + " - Editor" + label); // + (bChanged ? "*" : "")
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

IniWindow::IniWindow (std::vector<__INI> _i, IniType _ty, const std::wstring& _p, const std::string& _ti)
{
  ini         = _i;
  type        = _ty;
  title       = _ti;
  public_seed = SKIF_Util_RandomInteger ( );
  label       = ("###IniEditor-" + std::to_string (window_identifier));
  window_identifier++;
  SetPath           (_p);
  UpdateWindowTitle (  );
}

void
SKIF_ImGui_IniEditor_NewFile (IniWindow* iniWindow)
{
  std::vector<const ConfigEntry*> ini_params = SKIF_IniReader_GetDefaultParams (dll_ini);
  std::vector <__INI> ini_parsed;

  for (auto& default_item : ini_params)
  {
    __INI item = { default_item->param_type, default_item->section, default_item->key, "" };
    item.description = default_item->description;
    item._ignore = true;

    ini_parsed.push_back (item);
  }

  iniWindow->watch.reset();
  iniWindow->title       = "";
  iniWindow->bChanged    = false;
  iniWindow->ini         = ini_parsed;
  iniWindow->want_action = EditorAction::None;
  iniWindow->SetPath           (L"");
  iniWindow->UpdateWindowTitle (   );
  iniWindow->ClearFilter       (   );
}

void
SKIF_ImGui_IniEditor_NewWindow (void)
{
  std::vector<const ConfigEntry*> ini_params = SKIF_IniReader_GetDefaultParams (dll_ini);
  std::vector <__INI> ini_parsed;

  for (auto& default_item : ini_params)
  {
    __INI item = { default_item->param_type, default_item->section, default_item->key, "" };
    item.description = default_item->description;
    item._ignore = true;

    ini_parsed.push_back (item);
  }

  vIniWindow.push_back({ ini_parsed, dll_ini });
}

void
SKIF_ImGui_IniEditor_Save (IniWindow* iniWindow)
{
  if (iniWindow->path.empty())
    return SKIF_ImGui_IniEditor_SaveAs (iniWindow);

  SKIF_IniReader_WriteIni (iniWindow->ini, iniWindow->path);

  for (auto& trie : iniWindow->ini)
  {
    trie.default_b = trie.value_b;
    strncpy (trie.default, trie.value, MAX_PATH);
  }

  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_ImGui_IniEditor_SaveAs (IniWindow* iniWindow)
{
  std::wstring defaultFolderPath = (! iniWindow->path.empty() ? std::filesystem::path (iniWindow->path).parent_path().wstring() : L"");

  LPWSTR pwszFilePath = NULL;
  HRESULT hr          =
    SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { L"Configuration Files", L"*.ini" }, { L"All files", L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, defaultFolderPath.c_str(), L"ini");

  if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
    return;

  else if (SUCCEEDED (hr))
    iniWindow->SetPath (pwszFilePath);

  else
  {
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Unknown error attempting to retrieve file path!", L"Error", MB_OK | MB_ICONEXCLAMATION);
    return;
  }

  // Save As should reset any custom titles set
  iniWindow->title.clear();

  SKIF_ImGui_IniEditor_Save (iniWindow);
}

void
SKIF_ImGui_IniEditor_OpenFile (IniWindow* iniWindow, IniType type, std::wstring path, const std::string& title)
{
  if (path.empty())
  {
    LPWSTR pwszFilePath = NULL;
    HRESULT hr          =
      SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { L"Configuration Files", L"*.ini" }, { L"All files", L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST);

    if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
      return;

    else if (SUCCEEDED (hr))
      path = pwszFilePath;

    else
    {
      MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Unknown error attempting to retrieve file path!", L"Error", MB_OK | MB_ICONEXCLAMATION);
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

  std::vector <__INI> ini_parsed = SKIF_IniReader_ParseIni (path, type);

  if (iniWindow == nullptr)
    vIniWindow.push_back({ ini_parsed, type, path, title_final });

  else {
    iniWindow->bChanged    = false;
    iniWindow->ini         = ini_parsed;
    iniWindow->title       = title_final;
    iniWindow->want_action = EditorAction::None;
    iniWindow->SetPath           (path);
    iniWindow->UpdateWindowTitle (    );
    iniWindow->ClearFilter       (    );
  }
}

void
SKIF_ImGui_IniEditor_Reload (IniWindow* iniWindow)
{
  iniWindow->ini         = SKIF_IniReader_ParseIni (iniWindow->path, iniWindow->type);
  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->ApplyFilter ( );
}

void
SKIF_ImGui_IniEditor_Reset (IniWindow* iniWindow)
{
  for (auto& trie : iniWindow->ini)
    trie.Reset();

  iniWindow->bChanged    = false;
  iniWindow->want_action = EditorAction::None;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_ImGui_IniEditor_Process (void)
{
  bool cleanup  = false,
       newWnd   = false;

  for (size_t i = 0; i < vIniWindow.size(); ++i)
  {
    IniWindow& window = vIniWindow[i];

    ImGui::PushID (window.public_seed);

    if (window.state == PopupState_Open)
    {
      ImGui::SetNextWindowSize (ImVec2 (950.0f, 750.0f) * SKIF_ImGui_GlobalDPIScale);

      extern ImRect windowRect;
      ImGui::SetNextWindowPos (windowRect.GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

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

    ImGui::SetNextWindowSizeConstraints (ImVec2 (800.0f, 600.0f), ImVec2 (FLT_MAX, FLT_MAX));
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

    if (window.hwnd != nullptr && window.watch._path.empty() && ! window.path_parent.empty())
      window.watch = SKIF_DirectoryWatch (window.path_parent, UITab_None, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);

    if (! window.bChanged && window.watch.isSignaled())
      window.watchCD = SKIF_Util_timeGetTime() + 50;

    if (window.watchCD != NULL && window.watchCD < SKIF_Util_timeGetTime())
    {
      window.watchCD = NULL;
      window.ini = SKIF_IniReader_ParseIni (window.path, window.type);

      // Apply the active filter
      window.ApplyFilter ( );

      ImGui::InsertNotification ({
        ImGuiToastType::Info, 1000,
        "File was reloaded.",
        ""
      });
    }

    // Disable blocking for now cuz of weird edge cases...
    //ImGui::DockSpaceOverViewport (ImGui::GetWindowViewport ( ));

    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu ("File"))
      {
        if (ImGui::MenuItem ("New", "Ctrl+N"))
          newFile  = true;

        if (ImGui::MenuItem ("New Window", "Ctrl+Shift+N"))
          newWnd   = true;

        if (ImGui::MenuItem ("Open", "Ctrl+O"))
          openFile = true;

        if (ImGui::MenuItem ("Save", "Ctrl+S"))
          save     = true;

        if (ImGui::MenuItem ("Save As", "Ctrl+Shift+S"))
          saveAs   = true;

        if (ImGui::MenuItem ("Close", "Escape"))
          show     = false;

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu ("Edit"))
      {
        if (! window.bChanged)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem ("Undo Changes"))
          reset = true;

        if (! window.bChanged)
          SKIF_ImGui_PopDisableState ( );

        ImGui::Separator ( );

        if (window.path.empty())
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem ("Reload File"))
          reload = true;

        if (window.path.empty())
          SKIF_ImGui_PopDisableState ( );

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu ("Tools"))
      {
        if (window.path.empty())
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::MenuItem ("External Editor"))
          SKIF_Util_OpenURI (window.path.c_str(), SW_SHOWNORMAL, NULL);

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

    bool showClearBtn      = (window.charFilter[0] != '\0');
    // Mirrors what ImGui::ButtonEx() does to calculate the height of buttons
    ImVec2 fTopFilterSize  =                           ImGui::CalcTextSize (ICON_FA_FILTER);
    float fTopZoomX        =                           ImGui::CalcTextSize (ICON_FA_MAGNIFYING_GLASS).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float fTopClearX       = (! showClearBtn ? 0.0f :  ImGui::CalcTextSize (ICON_FA_XMARK ).x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    float fTopFilterFieldX = ImGui::GetContentRegionAvail().x - fTopZoomX - fTopClearX; //  - fTopFilterX
    //static bool bFilterHovered = false;

    ImGui::PushStyleColor (ImGuiCol_NavHighlight,  ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_Border,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

    ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
    ImGui::Button         (ICON_FA_MAGNIFYING_GLASS);
    ImGui::PopItemFlag    ( );
    ImGui::SameLine       ( );

    ImGui::PushStyleColor (ImGuiCol_FrameBg, ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg));
    ImGui::InputTextEx ("###AppListFilterField", "", window.charFilterTmp, MAX_PATH,
                        ImVec2 (fTopFilterFieldX, 0.0f),
                        ImGuiInputTextFlags_AutoSelectAll, 0, nullptr);

    ImGui::PopStyleColor  ( ); // ImGuiCol_FrameBg
    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    // Ctrl+F should focus filter field
    if (ImGui::GetIO().KeyCtrl && ImGui::GetKeyData(ImGuiKey_F)->DownDuration == 0.0f)
    {
      ImGui::ActivateItemByID (ImGui::GetID("###AppListFilterField"));
      ImGui::SetFocusID       (ImGui::GetID("###AppListFilterField"), ImGui::GetCurrentWindow());
    }

    // This is required to prevent the InputTextEx from not being deselected when clicking empty space
    SKIF_ImGui_DisallowMouseDragMove ( );

    window.bFilterActive = ImGui::IsItemActive ( ) || ImGui::IsItemFocused ( );

    if (ImGui::IsItemFocused ( ) && ! ImGui::IsItemHovered( ) && ImGui::IsAnyMouseDown( ))
    {
      // Clear highlight from filter box
      ImGuiContext& g = *ImGui::GetCurrentContext();
      g.NavDisableHighlight = false;
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
      ImGui::SameLine ( );

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
    ImGui::PopStyleColor  ( ); // ImGuiCol_NavHighlight

    // Some additional vertical padding
    ImGui::SetCursorPosY   (
      ImFloor (ImGui::GetCursorPosY ( ) + 3.0f * SKIF_ImGui_GlobalDPIScale)
    );

    ImVec2 fTop2 = ImGui::GetCursorPos ( );

    // End top options

    // Headers
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                            );

    ImGui::ItemSize        (ImVec2 (5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "[Section]");
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (208.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "Key=");
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (611.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "Value");

    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    ImGui::Separator   ( );

    SKIF_ImGui_BeginChildFrame (
      ImGui::GetID ("###SKIE_CONTENT_AREA"),
      ImVec2 (0.0f, ImFloor (ImGui::GetWindowSize().y - ImGui::GetCursorPosY() - 20.0f * SKIF_ImGui_GlobalDPIScale - 2.0f * SKIF_ImGui_GlobalDPIScale)), // 900.0f
      ImGuiChildFlags_None, // ImGuiChildFlags_FrameStyle
      ImGuiWindowFlags_NavFlattened
    );

    for (auto& trie : window.ini)
    {
      if (! trie._show)
        continue;

      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), trie.section);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (200.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), trie.key);
      if (trie.description != nullptr)
        SKIF_ImGui_SetHoverTip (trie.description);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (600.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );

      if (trie.DrawFunction != nullptr && trie.DrawFunction (&trie))
      {
        window.bChanged = true;
        window.UpdateWindowTitle();
      }
    }

    // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
    SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

    ImGui::EndChild ( );

    // Minimize button

    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      minimize = true;

    // Focus + Hotkeys
    extern bool bKeepWindowAlive;

    //window.bFocused = ImGui::IsWindowFocused (ImGuiFocusedFlags_ChildWindows);
    window.bFocused = SKIF_ImGui_IsViewportFocused (ImGui::GetWindowViewport ( ));
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
        window.prompt_save = true;
      }

      if (openFile)
      {   openFile = false;
        window.want_action = EditorAction::Open;
        window.prompt_save = true;
      }

      if (reload)
      {   reload = false;
        window.want_action = EditorAction::Reload;
        window.prompt_save = true;
      }

      if (! show)
      {     show = true;
        window.want_action = EditorAction::Close;
        window.prompt_save = true;
      }

      if (! bKeepWindowAlive)
      {     bKeepWindowAlive = true;
        window.want_action = EditorAction::Exit;
        window.prompt_save = true;
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

        switch (SKIF_ImGui_SaveChangesPrompt (window.public_seed, &window.prompt_save, window.doc_name.c_str()))
        {
          case SaveChoice::Save:
            _PerformAction ( );
            save = true;
            break;
          case SaveChoice::DontSave:
            _PerformAction ( );
            break;
          case SaveChoice::Cancel:
            window.want_action = EditorAction::None;
            _PerformAction ( );
            break;
        }
      }
    }

    // Actions

    if (reload)
      SKIF_ImGui_IniEditor_Reload   (&window);

    if (reset)
      SKIF_ImGui_IniEditor_Reset    (&window);

    if (save)
      SKIF_ImGui_IniEditor_Save     (&window);

    if (saveAs)
      SKIF_ImGui_IniEditor_SaveAs   (&window);

    if (openFile)
      SKIF_ImGui_IniEditor_OpenFile (&window, IniType_Unknown, L"", "");

    if (newFile)
      SKIF_ImGui_IniEditor_NewFile  (&window);

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
    SKIF_ImGui_IniEditor_NewWindow ( );

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
    std::vector <IniWindow> new_vector;
    for (auto& window : vIniWindow)
    {
      if (window.state != PopupState_Closed)
        new_vector.push_back (window);
    }
    vIniWindow = new_vector;
  }
}
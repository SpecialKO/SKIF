#include <utility/ini_editor.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

int window_identifier = 0;
std::vector <IniWindow> vIniWindow;
bool INIEditorActive = false;

void
IniWindow::UpdateWindowTitle (void)
{
  std::string filenameExt = "";

  if (! path.empty())
    filenameExt = std::filesystem::path(path).filename().string();

  path_filename = filenameExt;
  wnd_name      = ((title.empty() ? (path_filename.empty() ? "Unsaved" : path_filename) : title) + " - Editor" + label); // + (bChanged ? "*" : "")
}

IniWindow::IniWindow (std::vector<__INI> _i, const std::string& _p, const std::string& _t)
{
  ini   = _i;
  path  = _p;
  title = _t;
  label = ("###IniEditor-" + std::to_string (window_identifier));
  window_identifier++;
  UpdateWindowTitle ( );
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

  iniWindow->ini   = ini_parsed;
  iniWindow->path  = "";
  iniWindow->title = "";
  iniWindow->bChanged = false;
  iniWindow->UpdateWindowTitle ( );
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

  vIniWindow.push_back({ ini_parsed });
}

void
SKIF_ImGui_IniEditor_Save (IniWindow* iniWindow)
{
  if (iniWindow->path.empty())
  {
    LPWSTR pwszFilePath = NULL;
    HRESULT hr          =
      SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { L"Configuration Files", L"*.ini" }, { L"All files", L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, nullptr, L"ini");

    if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
      return;

    else if (SUCCEEDED (hr))
    {
      iniWindow->path = SK_WideCharToUTF8 (pwszFilePath);
    }

    else
    {
      MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Unknown error attempting to retrieve file path!", L"Error", MB_OK | MB_ICONEXCLAMATION);
      return;
    }
  }

  SKIF_IniReader_WriteIni (iniWindow->ini, iniWindow->path);

  for (auto& trie : iniWindow->ini)
  {
    trie.default_b = trie.value_b;
    strncpy (trie.default, trie.value, MAX_PATH);
  }

  iniWindow->bChanged = false;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_ImGui_IniEditor_SaveAs (IniWindow* iniWindow)
{
  std::wstring defaultFolderPath = (! iniWindow->path.empty() ? std::filesystem::path (SK_UTF8ToWideChar (iniWindow->path)).parent_path().wstring() : L"");

  LPWSTR pwszFilePath = NULL;
  HRESULT hr          =
    SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { L"Configuration Files", L"*.ini" }, { L"All files", L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, defaultFolderPath.c_str(), L"ini");

  if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
    return;

  else if (SUCCEEDED (hr))
  {
    iniWindow->path = SK_WideCharToUTF8 (pwszFilePath);
    SKIF_IniReader_WriteIni (iniWindow->ini, iniWindow->path);
    iniWindow->bChanged = false;
    iniWindow->UpdateWindowTitle();
  }

  else
  {
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Unknown error attempting to retrieve file path!", L"Error", MB_OK | MB_ICONEXCLAMATION);
    return;
  }
}

void
SKIF_ImGui_IniEditor_OpenFile (IniWindow* iniWindow, std::wstring path, const std::string& title, IniType type)
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

  std::string path_utf8   = SK_WideCharToUTF8 (path);
  std::string filename    = SKIF_Util_ToLower (std::filesystem::path(path).filename().replace_extension().string());
  std::string filenameExt = std::filesystem::path(path).filename().string();
  std::string title_final = (title.empty() ? filenameExt : title);

  if (type == IniType_Unknown)
  {
    if (     filename.find("specialk")      != std::string::npos ||
             filename.find("opengl32")      != std::string::npos ||
             filename.find( "dinput8")      != std::string::npos ||
             filename.find(  "dxgi"  )      != std::string::npos ||
             filename.find(  "d3d12" )      != std::string::npos ||
             filename.find(  "d3d11" )      != std::string::npos ||
             filename.find(  "d3d9"  )      != std::string::npos ||
             filename.find(  "d3d8"  )      != std::string::npos ||
             filename.find(  "ddraw" )      != std::string::npos)
      type = IniType_DLL;
    else if (filename.find("osd")           != std::string::npos)
      type = IniType_OSD;
    else if (filename.find("input")         != std::string::npos)
      type = IniType_Input;
    else if (filename.find("notifications") != std::string::npos)
      type = IniType_Notify;
    else if (filename.find("platform")      != std::string::npos)
      type = IniType_Platform;
    else if (filename.find("macros")        != std::string::npos)
      type = IniType_Macros;
  }

  std::vector <__INI> ini_parsed = SKIF_IniReader_ParseIni (path, path_utf8, type);

  if (iniWindow == nullptr)
    vIniWindow.push_back({ ini_parsed, path_utf8, title_final });

  else {
    iniWindow->ini   = ini_parsed;
    iniWindow->path  = path_utf8;
    iniWindow->title = title_final;
    iniWindow->UpdateWindowTitle ( );
  }
}

void
SKIF_ImGui_IniEditor_Process (void)
{
  bool cleanup  = false,
       newWnd   = false;

  for (auto& window : vIniWindow)
  {
    if (window.state == PopupState_Open)
    {
      ImGui::SetNextWindowSize (ImVec2 (950.0f, 750.0f) * SKIF_ImGui_GlobalDPIScale);

      extern ImRect windowRect;
      ImGui::SetNextWindowPos (windowRect.GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      window.state = PopupState_Opened;
    }

    bool show     = true,
         newFile  = false,
         openFile = false,
         save     = false,
         saveAs   = false,
         reset    = false;

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

    // Disable blocking for now cuz of weird edge cases...
    //ImGui::DockSpaceOverViewport (ImGui::GetWindowViewport ( ));

    if (ImGui::BeginMenuBar())
    {
      if (ImGui::BeginMenu ("File"))
      {
        if (ImGui::MenuItem ("New", "Ctrl+N"))
          newFile = true;

        if (ImGui::MenuItem ("New Window", "Ctrl+Shift+N"))
          newWnd = true;

        if (ImGui::MenuItem ("Open", "Ctrl+O"))
          openFile = true;

        if (ImGui::MenuItem ("Save", "Ctrl+S"))
          save   = true;

        if (ImGui::MenuItem ("Save As", "Ctrl+Shift+S"))
          saveAs = true;

        if (ImGui::MenuItem ("Close", "Escape"))
          show = false;

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu ("Edit"))
      {
        if (ImGui::MenuItem ("Reset"))
          reset = true;

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

    window.bFilterActive = ImGui::IsItemActive ( );

    if (ImGui::IsItemFocused ( ) && ! ImGui::IsItemHovered( ) && ImGui::IsAnyMouseDown( ))
    {
      // Clear highlight from filter box
      ImGuiContext& g = *ImGui::GetCurrentContext();
      g.NavDisableHighlight = false;
    }

    // Preprocess filtered entries

    auto _ClearCharFilter = [&](void) -> void
    {
      strncpy (window.charFilter,    "\0", MAX_PATH);
      strncpy (window.charFilterTmp, "\0", MAX_PATH);
      // do more stuff ?

      for (auto& trie : window.ini)
        trie._show = true;
    };

    // Update some stuff if filter query has changed
    if (strncmp (window.charFilter, window.charFilterTmp, MAX_PATH) != 0)
    {
      if (strlen (window.charFilterTmp) == 0)
        _ClearCharFilter ( );
      else {
        strncpy (window.charFilter, window.charFilterTmp, MAX_PATH);

        for (auto& trie : window.ini)
        {
          trie._show = false;
          trie._show = (trie._show || (StrStrIA (trie.section, window.charFilter) != NULL));
          trie._show = (trie._show || (StrStrIA (trie.key,     window.charFilter) != NULL));
          trie._show = (trie._show || (StrStrIA (trie.value,   window.charFilter) != NULL));
        }
      }
    }

    //PLOG_VERBOSE << "Numbers: " << numRegular << " (regular) -- " << numPinnedOnTop << " (on top)";

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
        _ClearCharFilter ( );

      ImGui::PopStyleColor ( );

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

    //window.bFocused = ImGui::IsWindowFocused (ImGuiFocusedFlags_ChildWindows);
    window.bFocused = SKIF_ImGui_IsViewportFocused (ImGui::GetWindowViewport ( ));
    if (window.bFocused && ! g_activeKeybindPopup)
    {
      extern bool bKeepWindowAlive;
      // Hotkeys
           if (ImGui::IsKeyPressed (ImGuiKey_Escape) ||
              (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_W)->DownDuration == 0.0f))            show = false; // Escape / Ctrl+W
           if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_O)->DownDuration == 0.0f)         openFile =  true; // Ctrl+O
           if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::GetKeyData (ImGuiKey_N)->DownDuration == 0.0f)           newWnd =  true; // Ctrl+Shift+N
      else if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_N)->DownDuration == 0.0f)          newFile =  true; // Ctrl+N
           if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f)           saveAs =  true; // Ctrl+Shift+S
      else if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f)             save =  true; // Ctrl+S
           if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_Q)->DownDuration == 0.0f) bKeepWindowAlive = false; // Ctrl+Q
    }

    if (reset)
    {
      for (auto& trie : window.ini)
        trie.Reset();

      window.bChanged = false;
      window.UpdateWindowTitle();
    }

    if (newFile)
      SKIF_ImGui_IniEditor_NewFile  (&window);

    if (openFile)
      SKIF_ImGui_IniEditor_OpenFile (&window, L"", "");

    if (save)
      SKIF_ImGui_IniEditor_Save     (&window);

    if (saveAs)
      SKIF_ImGui_IniEditor_SaveAs   (&window);

    if (! show)
    {
      window.state = PopupState_Closed;
      cleanup = true;
    }

    // End Editor window
    ImGui::End      ( );
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
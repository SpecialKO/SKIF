#include <utility/ini_editor.h>
#include <../packages_misc/ini.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/ini_reader.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

int window_identifier = 0;
std::vector <IniWindow> vIniWindow;
bool INIEditorActive = false;



__INI::__INI (const std::string& _s, const std::string& _k, const std::string& _v)
{
  strncpy (section, _s.c_str(), MAX_PATH);
  strncpy (key,     _k.c_str(), MAX_PATH);
  strncpy (default, _v.c_str(), MAX_PATH);
  strncpy (value,   default,    MAX_PATH);
  _label_k = ("###" + _s + "-" + _k);
  _label_v = ("###" + _s + "-" + _k + "-" + _v);
}

void
__INI::Reset (void)
{
  value_b = default_b;
  strncpy (value, default, MAX_PATH);
}

IniWindow::IniWindow (std::vector<__INI> _i, const std::string& _p)
{
  ini  = _i;
  path = _p;
  label = ("###IniEditor-" + std::to_string(window_identifier));
  window_identifier++;
  UpdateWindowTitle ( );
}

static bool DrawCheckbox (__INI* ptr)
{
  //ImGui::SetNextItemWidth(300.0f);
  if (ImGui::Checkbox (ptr->_label_v.c_str(), &ptr->value_b))
    strncpy (ptr->value, ((ptr->value_b) ? "true" : "false"), MAX_PATH);

  return (ptr->value_b != ptr->default_b);
}

static bool DrawInputBox (__INI* ptr)
{
  ImGui::SetNextItemWidth(300.0f);
  ImGui::InputText (ptr->_label_v.c_str(), ptr->value, MAX_PATH);

  return (strcmp (ptr->value, ptr->default) != 0);
}

static bool DrawDropDown (__INI* ptr)
{
  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::BeginCombo  (ptr->_label_k.c_str(), ptr->value))
  {
    for (auto& item : ptr->_dditems)
    {
      bool is_selected = (item == ptr->value);
      if (ImGui::Selectable (item.c_str(), is_selected))
        strncpy (ptr->value, item.c_str(), MAX_PATH);
      if (is_selected)
        ImGui::SetItemDefaultFocus ( );
    }
    ImGui::EndCombo  ( );
  }

  return (strcmp (ptr->value, ptr->default) != 0);
}

static bool DrawKeybinding (__INI* ptr)
{
  ImGui::PushID (ptr->section);
  ImGui::PushID (ptr->key);
  if (SK_ImGui_Keybinding (&ptr->_keybind))
  {
    // Only update the label if we are done assigning
    if (! ptr->_keybind.assigning)
    {
      strncpy (ptr->value, ptr->_keybind.getKeybind()->human_readable_utf8.c_str(), MAX_PATH);
    }
  }
  ImGui::PopID ();
  ImGui::PopID ();

  return (strcmp (ptr->value, ptr->default) != 0);
}

std::vector <__INI>
SKIF_IniHandler_ParseIni (const std::wstring& file_path, const std::string& file_path_utf8)
{
  PLOG_VERBOSE << "Parsing INI file: " << file_path_utf8;
  std::string unique_label = ("INI: " + file_path_utf8 + "###IniEditor-" + std::to_string(window_identifier));

  std::vector <__INI> ini_parsed;
  inih::INIReader ini;

  try {
    ini.ParseContent (SK_WideCharToUTF8 (DERP_IniReader_ReadTextFile (file_path)));
  } catch (const std::exception&) {
    PLOG_ERROR << "Failed to parse INI file!";
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Failed to parse INI file!", L"Error", MB_OK | MB_ICONEXCLAMATION);
    return ini_parsed;
  };

  for (auto& section : ini.Sections())
  {
    for (auto& kv : ini.Get (section))
    {
      ini_parsed.push_back ({ section, kv.first, kv.second });
    }
  }

  return ini_parsed;
}

void
SKIF_IniHandler_WriteIni (std::vector<__INI> ini, const std::string& file_path_utf8)
{
  inih::INIReader new_ini = { };

  for (auto& trie : ini)
  {
    if (! trie._ignore_if_unset || (trie.value[0] != '\0'))
      new_ini.InsertEntry (trie.section, trie.key, trie.value);
  }

  inih::INIWriter::write (file_path_utf8, new_ini, true);
}

void
SKIF_ImGui_IniEditor_NewFile (IniWindow* iniWindow)
{
  std::vector<const ConfigEntry*> ini_params = SKIF_IniHandler_GetParams (dll_ini);
  std::vector <__INI> ini_parsed;

  for (auto& default_item : ini_params)
  {
    __INI item = { default_item->section_, default_item->key_, "" };
    item._ignore_if_unset = true;
    item.DrawFunction = DrawInputBox;

    ini_parsed.push_back (item);
  }

  iniWindow->ini  = ini_parsed;
  iniWindow->path = "";
  iniWindow->bChanged = false;
  iniWindow->UpdateWindowTitle ( );
}

void
SKIF_ImGui_IniEditor_NewWindow (void)
{
  std::vector<const ConfigEntry*> ini_params = SKIF_IniHandler_GetParams (dll_ini);
  std::vector <__INI> ini_parsed;

  for (auto& default_item : ini_params)
  {
    __INI item = { default_item->section_, default_item->key_, "" };
    item._ignore_if_unset = true;
    item.DrawFunction = DrawInputBox;

    ini_parsed.push_back (item);
  }

  vIniWindow.push_back({ ini_parsed });
}

void
SKIF_ImGui_IniEditor_SaveAs (IniWindow* iniWindow)
{
  LPWSTR pwszFilePath = NULL;
  HRESULT hr          =
    SKIF_Util_FileExplorer_SaveFile (&pwszFilePath, (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, { { L"Configuration Files", L"*.ini" }, { L"All files", L"*.*" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, nullptr, L"ini");
          
  if (hr == HRESULT_FROM_WIN32 (ERROR_CANCELLED))
    return;

  else if (SUCCEEDED (hr))
  {
    iniWindow->path = SK_WideCharToUTF8 (pwszFilePath);
    SKIF_IniHandler_WriteIni (iniWindow->ini, iniWindow->path);
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
SKIF_ImGui_IniEditor_OpenFile (std::wstring path, IniWindow* iniWindow)
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

  std::string path_utf8 = SK_WideCharToUTF8 (path);
  std::string filename  = SKIF_Util_ToLower (std::filesystem::path(path).filename().replace_extension().string());
  IniFileType type      = IniFile_Unknown;

  if (     filename.find("specialk")      != std::string::npos ||
           filename.find("opengl32")      != std::string::npos ||
           filename.find( "dinput8")      != std::string::npos ||
           filename.find(  "dxgi"  )      != std::string::npos ||
           filename.find(  "d3d11" )      != std::string::npos ||
           filename.find(  "d3d9"  )      != std::string::npos ||
           filename.find(  "d3d9"  )      != std::string::npos ||
           filename.find(  "ddraw" )      != std::string::npos)
    type = IniFile_DLL;
  else if (filename.find("osd")           != std::string::npos)
    type = IniFile_OSD;
  else if (filename.find("input")         != std::string::npos)
    type = IniFile_Input;
  else if (filename.find("notifications") != std::string::npos)
    type = IniFile_Notify;
  else if (filename.find("platform")      != std::string::npos)
    type = IniFile_Platform;
  else if (filename.find("macros")        != std::string::npos)
    type = IniFile_Macros;

  std::vector<const ConfigEntry*> ini_params = SKIF_IniHandler_GetParams (type);

  std::vector <__INI> ini_parsed;
  inih::INIReader ini;

  PLOG_VERBOSE << "Parsing INI file: " << path_utf8;

  try {
    ini.ParseContent (SK_WideCharToUTF8 (DERP_IniReader_ReadTextFile (path)));
  } catch (const std::exception&) {
    PLOG_ERROR << "Failed to parse INI file!";
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Failed to parse INI file!", L"Error", MB_OK | MB_ICONEXCLAMATION);
    return;
  };

  for (auto& section : ini.Sections())
  {
    for (auto& kv : ini.Get (section))
    {
      __INI item = { section, kv.first, kv.second };

      switch (SwitchHash (kv.first))
      {
        case SwitchHash ("NotifyCorner"):
        case SwitchHash ("PopupOrigin"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("TopLeft");
          item._dditems.push_back ("TopRight");
          item._dditems.push_back ("BottomLeft");
          item._dditems.push_back ("BottomRight");
          item.DrawFunction = DrawDropDown;
          break;
        }

        case SwitchHash ("Scaling"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("Unspecified");
          item._dditems.push_back ("Centered");
          item._dditems.push_back ("Stretched");
          item.DrawFunction = DrawDropDown;
          break;
        }

        case SwitchHash ("ScanlineOrder"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("Unspecified");
          item._dditems.push_back ("Progressive");
          item._dditems.push_back ("LowerFieldFirst");
          item._dditems.push_back ("UpperFieldFirst");
          item.DrawFunction = DrawDropDown;
          break;
        }

        case SwitchHash ("ExceptionMode"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("Raise");
          item._dditems.push_back ("Ignore");
          item.DrawFunction = DrawDropDown;
          break;
        }

        // Keybindings (Keyboard)

        // OSD [Game.HUD]
        case SwitchHash ("HUDToggle"):
        // OSD [OSD.System]
        case SwitchHash ("ConsoleToggle"):
        // OSD [Screenshot.System]
        case SwitchHash ("HUDFree"):
        case SwitchHash ("WithoutOSD"):
        case SwitchHash ("InsertOSD"):
        case SwitchHash ("Without3rdParty"):
        case SwitchHash ("ClipboardOnly"):
        case SwitchHash ("Snipping"):
        // OSD [Display.Monitor]
        case SwitchHash ("ToggleADHDMultiMonitor"):
        case SwitchHash ("MoveToPrimaryMonitor"):
        case SwitchHash ("MoveToNextMonitor"):
        case SwitchHash ("MoveToPrevMonitor"):
        case SwitchHash ("ToggleHDR"):
        // OSD [LatentSync.Control]
        case SwitchHash ("MoveTearlineDown"):
        case SwitchHash ("MoveTearlineUp"):
        case SwitchHash ("ManualResync"):
        case SwitchHash ("ToggleFCATBars"):
        // OSD [Sound.Mixing]
        case SwitchHash ("MuteGame"):
        case SwitchHash ("VolumePlus10%"):
        case SwitchHash ("VolumeMinus10%"):
        // OSD [Widgets.Global]
        case SwitchHash ("HideAllWidgets"):
        // OSD [ReShade.AddOn]
        case SwitchHash ("ToggleReShadeOverlay"):
        case SwitchHash ("InjectReShade"):
        // OSD [ImGui.Global]
        case SwitchHash ("ControlPanelToggle"):
        // OSD [HDR.Presets]
        case SwitchHash ("Activate0"):
        case SwitchHash ("Activate1"):
        case SwitchHash ("Activate2"):
        case SwitchHash ("Activate3"):
        // OSD [Widgets]
        case SwitchHash ("ToggleKey"):
        case SwitchHash ("FocusKey"):
        case SwitchHash ("FlashKey"):
        {
          item._keybind = {
            item.key,
            SK_UTF8ToWideChar (item.value)
          };
          item._keybind.pending.human_readable = SK_UTF8ToWideChar (item.value);
          item._keybind.pending.parse();
          item._keybind.applyChanges();

          item.DrawFunction = DrawKeybinding;
          break;
        }

        // Keybindings (Gamepad)
        case SwitchHash ("LeftPaddle"):
        case SwitchHash ("LeftFunction"):
        case SwitchHash ("RightFunction"):
        case SwitchHash ("RightPaddle"):
        case SwitchHash ("TouchpadClick"):
        {
          item.DrawFunction = DrawInputBox;
          break;
        }

        default:
        {
          if (kv.second == "true" ||
              kv.second == "false")
          {
            item.default_b = (kv.second == "true");
            item.value_b   = item.default_b;
            strncpy (item.default, ((item.value_b) ? "true" : "false"), MAX_PATH);
            strncpy (item.value, item.default, MAX_PATH);
            item.DrawFunction = DrawCheckbox;
          }
          else {
            item.DrawFunction = DrawInputBox;
          }
        }
      }

      ini_parsed.push_back (item);
    }
  }

  for (auto& default_item : ini_params)
  {
    bool found = false;

    // See if the parameter has been added...
    for (auto& parsed_item : ini_parsed)
    {
      if ((! _stricmp (default_item->section_, parsed_item.section) &&
          (! _stricmp (default_item->key_,     parsed_item.key))))
      {
        found = true;
        break;
      }
    }

    // Add the parameter if not found...
    if (! found)
    {
      __INI item = { default_item->section_, default_item->key_, "" };
      item._ignore_if_unset = true;
      item.DrawFunction = DrawInputBox;

      ini_parsed.push_back (item);
    }
  }

  if (iniWindow == nullptr)
    vIniWindow.push_back({ ini_parsed, path_utf8 });

  else {
    iniWindow->ini  = ini_parsed;
    iniWindow->path = path_utf8;
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
    ImGui::Begin (window.title.c_str(),
                      &show,
                    //ImGuiWindowFlags_NoResize          |
                      ImGuiWindowFlags_NoCollapse        |
                    //ImGuiWindowFlags_NoTitleBar        |
                    //ImGuiWindowFlags_NoScrollWithMouse | // Prevent scrolling with the mouse as well
                      ImGuiWindowFlags_NoScrollbar       |
                      ImGuiWindowFlags_NoSavedSettings   |
                      ImGuiWindowFlags_MenuBar
    );

    ImGui::DockSpaceOverViewport (ImGui::GetWindowViewport());

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
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (600.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );

      if (trie.DrawFunction != nullptr && trie.DrawFunction (&trie))
      {
        window.bChanged = true;
        window.UpdateWindowTitle();
      }
    }

    ImGui::EndChild ( );

    window.bFocused = ImGui::IsWindowFocused (ImGuiFocusedFlags_ChildWindows);
    if (window.bFocused && ! g_activeKeybindPopup)
    {
      // Hotkey: Escape
      if (ImGui::IsKeyPressed (ImGuiKey_Escape))
        show = false;

      // Hotkeys
           if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_O)->DownDuration == 0.0f) openFile = true; // Ctrl+O
           if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::GetKeyData (ImGuiKey_N)->DownDuration == 0.0f) newWnd   = true; // Ctrl+Shift+N
      else if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_N)->DownDuration == 0.0f) newFile  = true; // Ctrl+N
           if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f) saveAs   = true; // Ctrl+Shift+S
      else if (ImGui::GetIO().KeyCtrl &&                            ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f) save     = true; // Ctrl+S
    }

    if (reset)
    {
      for (auto& trie : window.ini)
        trie.Reset();

      window.bChanged = false;
      window.UpdateWindowTitle();
    }

    if (newFile)
    {
      SKIF_ImGui_IniEditor_NewFile (&window);
    }

    if (openFile)
      SKIF_ImGui_IniEditor_OpenFile (L"", &window);

    if (save)
    {
      SKIF_IniHandler_WriteIni (window.ini, window.path);

      for (auto& trie : window.ini)
      {
        trie.default_b = trie.value_b;
        strncpy (trie.default, trie.value, MAX_PATH);
      }

      window.bChanged = false;
      window.UpdateWindowTitle();
    }

    if (saveAs)
      SKIF_ImGui_IniEditor_SaveAs (&window);

    if (! show)
    {
      window.state = PopupState_Closed;
      cleanup = true;
    }

    ImGui::End      ( );
  }

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
}


#define Keybind ConfigEntry

std::vector <const ConfigEntry*>
SKIF_IniHandler_GetParams (IniFileType ini)
{
  static const std::initializer_list <ConfigEntry> params_to_build
  //// nb: If you want any hope of reading this table, turn line wrapping off.
  //
  {
    ConfigEntry ("How long to display version info at startup, 0=disable)",   osd_ini,         "SpecialK.VersionBanner","Duration"),
    ConfigEntry ("OSD Visibility",                                            osd_ini,         "SpecialK.OSD",          "Show"),

    ConfigEntry ("OSD Color (Red)",                                           osd_ini,         "SpecialK.OSD",          "TextColorRed"),
    ConfigEntry ("OSD Color (Green)",                                         osd_ini,         "SpecialK.OSD",          "TextColorGreen"),
    ConfigEntry ("OSD Color (Blue)",                                          osd_ini,         "SpecialK.OSD",          "TextColorBlue"),

    ConfigEntry ("OSD Position (X)",                                          osd_ini,         "SpecialK.OSD",          "PositionX"),
    ConfigEntry ("OSD Position (Y)",                                          osd_ini,         "SpecialK.OSD",          "PositionY"),

    ConfigEntry ("OSD Scale",                                                 osd_ini,         "SpecialK.OSD",          "Scale"),

    ConfigEntry ("Remember status monitoring state",                          osd_ini,         "SpecialK.OSD",          "RememberMonitoringState"),
    ConfigEntry ("OSD's Luminance (cd.m^-2) in HDR games",                    osd_ini,         "SpecialK.OSD",          "HDRLuminance"),

    ConfigEntry ("Show SLI Monitoring",                                       osd_ini,         "Monitor.SLI",           "Show"),

    ConfigEntry ("Make the uPlay Overlay visible in HDR mode!",               osd_ini,         "uPlay.Overlay",         "Luminance_scRGB"),
    ConfigEntry ("Make the RTSS Overlay visible in HDR mode!",                osd_ini,         "RTSS.Overlay",          "Luminance_scRGB"),
    ConfigEntry ("Make the ReShade Overlay visible in HDR mode!",             osd_ini,         "ReShade.Overlay",       "Luminance_scRGB"),
    ConfigEntry ("Make the Galaxy Overlay visible in HDR mode!",              osd_ini,         "Galaxy.Overlay",        "Luminance_scRGB"),
    ConfigEntry ("Make the Discord Overlay visible in HDR mode!",             osd_ini,         "Discord.Overlay",       "Luminance_scRGB"),
    ConfigEntry ("Allow Discord to composite a Win32 window over the game?",  osd_ini,         "Discord.Overlay",       "AllowWindowedMode"),

    ConfigEntry ("Show Confirmation Dialog when Changing Display Modes",      osd_ini,         "Display.Settings",      "ConfirmChanges"),
    ConfigEntry ("Remember Monitor Preferences for the Current Game",         dll_ini,         "Display.Monitor",       "RememberPreference"),
    ConfigEntry ("Remember Monitor Resolution for the Current Game" ,         dll_ini,         "Display.Monitor",       "RememberResolution"),
    ConfigEntry ("Apply Resolution Override for the Current Game",            dll_ini,         "Display.Monitor",       "ResolutionForMonitor"),
    ConfigEntry ("Apply Refresh Override for the Current Game",               dll_ini,         "Display.Monitor",       "RefreshRateForMonitor"),
    ConfigEntry ("Warn user if Multiplane Overlays support is missing",       dll_ini,         "Display.Monitor",       "WarnIfNoOverlayPlanes"),

    // Performance Monitoring  (Global Settings)
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Show IO Monitoring",                                        osd_ini,         "Monitor.IO",            "Show"),
    ConfigEntry ("IO Monitoring Interval",                                    osd_ini,         "Monitor.IO",            "Interval"),

    ConfigEntry ("Show Disk Monitoring",                                      osd_ini,         "Monitor.Disk",          "Show"),
    ConfigEntry ("Disk Monitoring Interval",                                  osd_ini,         "Monitor.Disk",          "Interval"),
    ConfigEntry ("Disk Monitoring Type (0 = Physical, 1 = Logical)",          osd_ini,         "Monitor.Disk",          "Type"),

    ConfigEntry ("Show CPU Monitoring",                                       osd_ini,         "Monitor.CPU",           "Show"),
    ConfigEntry ("CPU Monitoring Interval (seconds)",                         osd_ini,         "Monitor.CPU",           "Interval"),
    ConfigEntry ("Minimal CPU Info",                                          osd_ini,         "Monitor.CPU",           "Simple"),

    ConfigEntry ("Show GPU Monitoring",                                       osd_ini,         "Monitor.GPU",           "Show"),
    ConfigEntry ("GPU Monitoring Interval (msecs)",                           osd_ini,         "Monitor.GPU",           "Interval"),
    ConfigEntry ("Print GPU Slowdown Reason (NVIDA GPUs)",                    osd_ini,         "Monitor.GPU",           "PrintSlowdown"),

    ConfigEntry ("Show Pagefile Monitoring",                                  osd_ini,         "Monitor.Pagefile",      "Show"),
    ConfigEntry ("Pagefile Monitoring Interval (seconds)",                    osd_ini,         "Monitor.Pagefile",      "Interval"),

    ConfigEntry ("Show DLSS Resolution Information",                          osd_ini,         "Monitor.DLSS",          "Show"),
    ConfigEntry ("Print DLSS Output Resolution",                              osd_ini,         "Monitor.DLSS",          "ShowOutputResolution"),
    ConfigEntry ("Print DLSS Quality Level",                                  osd_ini,         "Monitor.DLSS",          "ShowQuality"),
    ConfigEntry ("Print DLSS Preset",                                         osd_ini,         "Monitor.DLSS",          "ShowPreset"),
    ConfigEntry ("Print DLSS Frame Generation Status",                        osd_ini,         "Monitor.DLSS",          "ShowFrameGeneration"),

    ConfigEntry ("Show Memory Monitoring",                                    osd_ini,         "Monitor.Memory",        "Show"),
    ConfigEntry ("Show Framerate Monitoring",                                 osd_ini,         "Monitor.FPS",           "Show"),
    ConfigEntry ("Show Frametime in Framerate Counter",                       osd_ini,         "Monitor.FPS",           "DisplayFrametime"),
    ConfigEntry ("Show Advanced Statistics in Framerate Counter",             osd_ini,         "Monitor.FPS",           "AdvancedStatistics"),
    ConfigEntry ("Show FRAPS-like ('120') Statistics in Framerate Counter",   osd_ini,         "Monitor.FPS",           "CompactStatistics"),
    ConfigEntry ("Show VRR Status in Compact Mode",                           osd_ini,         "Monitor.FPS",           "CompactIncludesVRR"),
    ConfigEntry ("Show Frame Number",                                         osd_ini,         "Monitor.FPS",           "DisplayFrameNumber"),
    ConfigEntry ("How to measure frame intervals for the framepacing widget.",osd_ini,         "Monitor.FPS",           "FrametimeMethod"),
    ConfigEntry ("Show System Clock",                                         osd_ini,         "Monitor.Time",          "Show"),
    ConfigEntry ("Show Special K Title",                                      osd_ini,         "Monitor.Title",         "Show"),

    ConfigEntry ("Prefer Fahrenheit Units",                                   osd_ini,         "SpecialK.OSD",          "PreferFahrenheit"),

    ConfigEntry ("ImGui Scale",                                               osd_ini,         "ImGui.Global",          "FontScale"),
    ConfigEntry ("Display Playing Time in Config UI",                         osd_ini,         "ImGui.Global",          "ShowPlaytime"),
    ConfigEntry ("Show G-Sync Status on Control Panel",                       osd_ini,         "ImGui.Global",          "ShowGSyncStatus"),
    ConfigEntry ("Use Mac-style Menu Bar",                                    osd_ini,         "ImGui.Global",          "UseMacStyleMenu"),
    ConfigEntry ("Show Input APIs currently in-use",                          osd_ini,         "ImGui.Global",          "ShowActiveInputAPIs"),
    ConfigEntry ("Center the mouse cursor when opening SK's overlay",         osd_ini,         "ImGui.Global",          "CenterCursorOnOverlayToggle"),
    ConfigEntry ("Keyboard/Gamepad selection changes move the mouse cursor",  osd_ini,         "ImGui.Global",          "NavigationMovesMouseCursor"),

    ConfigEntry ("Keep a .PNG compressed copy of each screenshot?",           osd_ini,         "Screenshot.System",     "KeepLosslessPNG"),
    ConfigEntry ("Play a Sound when triggering Screenshot Capture",           osd_ini,         "Screenshot.System",     "PlaySoundOnCapture"),
    ConfigEntry ("Copy an LDR/HDR copy to the Windows Clipboard",             osd_ini,         "Screenshot.System",     "CopyToClipboard"),
    ConfigEntry ("Add Steam/Epic nickname as Author to Screenshot Metadata",  osd_ini,         "Screenshot.System",     "AuthorMetadata"),
    ConfigEntry ("Where to store screenshots (if non-empty)",                 osd_ini,         "Screenshot.System",     "OverridePath"),
    ConfigEntry ("wcsftime format; Non-Standard Specifier: %G = <Game Name>", osd_ini,         "Screenshot.System",     "FilenameFormat"),
    ConfigEntry ("Compression Quality: 0=Worst, 100=Lossless",                osd_ini,         "Screenshot.System",     "Quality"),
    ConfigEntry ("Compression 'Quality' of JPEG: 0=Oh No!, 100=Still Crap...",osd_ini,         "Screenshot.System",     "JPEGNotQuality"),
    ConfigEntry ("Use less advanced encoding in JPEG XR and AVIF for compat.",osd_ini,         "Screenshot.System",     "CompatibilityMode"),
    ConfigEntry ("Use JPEG XL file format for HDR screenshots",               osd_ini,         "Screenshot.System",     "UseJPEGX"),
    ConfigEntry ("Use AVIF file format for HDR screenshots",                  osd_ini,         "Screenshot.System",     "UseAVIF"),
    ConfigEntry ("Chroma Subsampling (444, 422, 420, 400)",                   osd_ini,         "Screenshot.AVIF",       "SubsampleYUV"),
    ConfigEntry ("Bits to use for scRGB to PQ encoded images",                osd_ini,         "Screenshot.AVIF",       "scRGBtoPQBits"),
    ConfigEntry ("Compression Speed: 0=Slowest (Smallest File), 10=Fastest",  osd_ini,         "Screenshot.AVIF",       "Speed"),
    ConfigEntry ("Use HDR PNG file format for HDR screenshots",               osd_ini,         "Screenshot.HDR",        "StorePNG"),
    ConfigEntry ("Use HDR for Windows Clipboard screenshots",                 osd_ini,         "Screenshot.HDR",        "AllowClipboardHDR"),
    ConfigEntry ("Use n-bit Quantization to save Disk Space",                 osd_ini,         "Screenshot.HDR",        "MaxST2084QuantizedBits"),
    ConfigEntry ("Use PNG or AVIF for HDR clipboard screenshots",             osd_ini,         "Screenshot.HDR",        "HDRClipboardFormat"),
    Keybind     ("Toggle Game's HUD",                                         osd_ini,         "Game.HUD",              "HUDToggle"),
    Keybind     ("Toggle SK's Command Console",                               osd_ini,         "OSD.System",            "ConsoleToggle"),
    Keybind     ("Take a screenshot without the HUD",                         osd_ini,         "Screenshot.System",     "HUDFree"),
    Keybind     ("Take a screenshot without SK's OSD",                        osd_ini,         "Screenshot.System",     "WithoutOSD"),
    Keybind     ("Take a screenshot and insert SK's OSD",                     osd_ini,         "Screenshot.System",     "InsertOSD"),
    Keybind     ("Take a screenshot before third-party overlays",             osd_ini,         "Screenshot.System",     "Without3rdParty"),
    Keybind     ("Take a screenshot and copy it to the clipboard only",       osd_ini,         "Screenshot.System",     "ClipboardOnly"),
    Keybind     ("Snip a screenshot and copy it to the clipboard",            osd_ini,         "Screenshot.System",     "Snipping"),

    Keybind     ("Toggle Multi-Monitor Focus Mode",                           osd_ini,         "Display.Monitor",       "ToggleADHDMultiMonitor"),
    Keybind     ("Move Game to Primary Monitor",                              osd_ini,         "Display.Monitor",       "MoveToPrimaryMonitor"),
    Keybind     ("Move Game to Next Monitor",                                 osd_ini,         "Display.Monitor",       "MoveToNextMonitor"),
    Keybind     ("Move Game to Previous Monitor",                             osd_ini,         "Display.Monitor",       "MoveToPrevMonitor"),
    Keybind     ("Toggle HDR on Selected Monitor",                            osd_ini,         "Display.Monitor",       "ToggleHDR"),

    Keybind     ("Move Tear Location Down 1 Scanline",                        osd_ini,         "LatentSync.Control",    "MoveTearlineDown"),
    Keybind     ("Move Tear Location Up 1 Scanline",                          osd_ini,         "LatentSync.Control",    "MoveTearlineUp"),
    Keybind     ("Request a Monitor Timing Resync",                           osd_ini,         "LatentSync.Control",    "ManualResync"),
    Keybind     ("Toggle FCAT Tearing Visualizer",                            osd_ini,         "LatentSync.Control",    "ToggleFCATBars"),

    Keybind     ("Toggle Mute for the Game",                                  osd_ini,         "Sound.Mixing",          "MuteGame"),
    Keybind     ("Increase Game Volume 10%",                                  osd_ini,         "Sound.Mixing",          "VolumePlus10%"),
    Keybind     ("Decrease Game Volume 10%",                                  osd_ini,         "Sound.Mixing",          "VolumeMinus10%"),

    Keybind     ("Temporarily hide all widgets",                              osd_ini,         "Widgets.Global",        "HideAllWidgets"),
    Keybind     ("Toggle ReShade Overlay (Add-On version)",                   osd_ini,         "ReShade.AddOn",         "ToggleReShadeOverlay"),
    Keybind     ("Inject ReShade (6.0+) as a Global PlugIn",                  osd_ini,         "ReShade.AddOn",         "InjectReShade"),

    Keybind     ("Toggle Special K's Control Panel",                          osd_ini,         "ImGui.Global",          "ControlPanelToggle"),


    // Input
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("If the game does not handle Alt+F4, offer a replacement",   dll_ini,         "Input.Keyboard",        "CatchAltF4"),
    ConfigEntry ("Forcefully disable a game's Alt+F4 handler",                dll_ini,         "Input.Keyboard",        "BypassAltF4Handler"),
    ConfigEntry ("Completely stop all keyboard input from reaching the Game", dll_ini,         "Input.Keyboard",        "DisabledToGame"),
    ConfigEntry ("Block, Unblock or use Game Behavior for Alt-Tab key",       dll_ini,         "Input.Keyboard",        "EnableAltTab"),
    ConfigEntry ("Block, Unblock or use Game Behavior for Windows key",       dll_ini,         "Input.Keyboard",        "EnableWinKey"),
    ConfigEntry ("Minimum time, in milliseconds, between Alt-Tab usage",      dll_ini,         "Input.Keyboard",        "AltTabPacing"),
    ConfigEntry ("Disable IME input services for the game",                   dll_ini,         "Input.Keyboard",        "DisableIME"),
    ConfigEntry ("Prevent games from disabling legacy keyboard messages",     dll_ini,         "Input.Keyboard",        "PreventRawInputNoLegacy"),
    ConfigEntry ("Prevent games from disabling hotkeys",                      dll_ini,         "Input.Keyboard",        "PreventRawInputNoHotkeys"),
    ConfigEntry ("Enable ImGui control panel keybinding",                     dll_ini,         "Input.Keyboard",        "EnableImGuiToggle"),

    ConfigEntry ("Completely stop all mouse input from reaching the Game",    dll_ini,         "Input.Mouse",           "DisabledToGame"),
    ConfigEntry ("Prevent games from disabling legacy mouse messages",        dll_ini,         "Input.Mouse",           "PreventRawInputNoLegacy"),
    ConfigEntry ("Prevent games from blocking external window activation",    dll_ini,         "Input.Mouse",           "PreventRawInputCapture"),

    ConfigEntry ("Manage Cursor Visibility (due to inactivity)",              dll_ini,         "Input.Cursor",          "Manage"),
    ConfigEntry ("Keyboard Input Activates Cursor",                           dll_ini,         "Input.Cursor",          "KeyboardActivates"),
    ConfigEntry ("Gamepad Input Deactivates Cursor",                          dll_ini,         "Input.Cursor",          "GamepadDeactivates"),
    ConfigEntry ("Inactivity Timeout (in milliseconds)",                      dll_ini,         "Input.Cursor",          "Timeout"),
    ConfigEntry ("Forcefully Capture Mouse Cursor in UI Mode",                dll_ini,         "Input.Cursor",          "ForceCaptureInUI"),
    ConfigEntry ("Use a Hardware Cursor for Special K's UI Features",         dll_ini,         "Input.Cursor",          "UseHardwareCursor"),
    ConfigEntry ("Block Mouse Input if Hardware Cursor is Invisible",         dll_ini,         "Input.Cursor",          "BlockInvisibleCursorInput"),
    ConfigEntry ("Fix Synaptic Touchpad Scroll",                              dll_ini,         "Input.Cursor",          "FixSynapticsTouchpadScroll"),

    ConfigEntry ("Disable ALL Gamepad Input (across all APIs)",               dll_ini,         "Input.Gamepad",         "DisabledToGame"),
    ConfigEntry ("Disable HID Input (prevent double-input if XInput is used)",dll_ini,         "Input.Gamepad",         "DisableHID"),
    ConfigEntry ("Disable WinMM Joystick Input",                              dll_ini,         "Input.Gamepad",         "DisableWinMM"),
    ConfigEntry ("Hide Windows.Gaming.Input Support from the game",           dll_ini,         "Input.Gamepad",         "HideWindowsGamingInput"),
    ConfigEntry ("Prevent game from seeing RawInput at all, useful in some "
                 " Unity Engine games that get duplicate input otherwise.",   dll_ini,         "Input.Gamepad",         "HideRawInput"),
    ConfigEntry ("Give tactile feedback on gamepads when navigating the UI",  dll_ini,         "Input.Gamepad",         "AllowHapticUI"),
    ConfigEntry ("Install hooks for Windows.Gaming.Input",                    dll_ini,         "Input.Gamepad",         "EnableWindowsGamingInput"),
    ConfigEntry ("Install hooks for RawInput and process WM_INPUT messages",  dll_ini,         "Input.Gamepad",         "EnableRawInput"),
    ConfigEntry ("Install hooks for DirectInput 8",                           dll_ini,         "Input.Gamepad",         "EnableDirectInput8"),
    ConfigEntry ("Install hooks for DirectInput 7",                           dll_ini,         "Input.Gamepad",         "EnableDirectInput7"),
    ConfigEntry ("Install hooks for HID",                                     dll_ini,         "Input.Gamepad",         "EnableHID"),
    ConfigEntry ("Install hooks for GameInput",                               dll_ini,         "Input.Gamepad",         "HookGameInput"),
    ConfigEntry ("Install hooks for joyGet* APIs",                            dll_ini,         "Input.Gamepad",         "HookWinMM"),
    ConfigEntry ("Use Steam-manipulated version of WinMM input",              dll_ini,         "Input.Gamepad",         "AllowSteamWinMM"),
    ConfigEntry ("Disable Rumble from ALL SOURCES (across all APIs)",         dll_ini,         "Input.Gamepad",         "DisableRumble"),
    ConfigEntry ("Gamepad activity will block screensaver activation",        dll_ini,         "Input.Gamepad",         "BlocksScreenSaver"),
    ConfigEntry ("Scale the Right Impulse Triggers in GameInput games",       dll_ini,         "Input.Gamepad",         "RightImpulseStrength"),
    ConfigEntry ("Scale the Left Impulse Triggers in GameInput games",        dll_ini,         "Input.Gamepad",         "LeftImpulseStrength"),
    ConfigEntry ("Apply a constant Right Trigger resistance on DualSense",    dll_ini,         "Input.Gamepad",         "RightTriggerResistance"),
    ConfigEntry ("Apply a constant Left Trigger resistance on DualSense",     dll_ini,         "Input.Gamepad",         "LeftTriggerResistance"),
    ConfigEntry ("Ratio of Right Trigger pull before resistance applies",     dll_ini,         "Input.Gamepad",         "RightTriggerResistsAt"),
    ConfigEntry ("Ratio of Left Trigger pull before resistance applies",      dll_ini,         "Input.Gamepad",         "LeftTriggerResistsAt"),
    ConfigEntry ("Prevent Bluetooth Output (PlayStation DirectInput compat.)",dll_ini,         "Input.Gamepad",         "BluetoothInputOnly"),
    ConfigEntry ("Maximum allowed HID buffers; 32=NS default, 8=SK default,"
                 " this will lower latency at the expense of possibly missed"
                 " inputs...",                                                dll_ini,         "Input.Gamepad",         "MaxHIDPollingBuffers"),

    ConfigEntry ("Install hooks for XInput",                                  dll_ini,         "Input.XInput",          "Enable"),
    ConfigEntry ("Re-install XInput hooks if hookchain is modified",          dll_ini,         "Input.XInput",          "Rehook"),
    ConfigEntry ("XInput Controller that owns the config UI",                 dll_ini,         "Input.XInput",          "UISlot"),
    ConfigEntry ("XInput Controller Slots to Fake Connectivity On",           dll_ini,         "Input.XInput",          "PlaceholderMask"),
    ConfigEntry ("Re-Assign XInput Slots",                                    dll_ini,         "Input.XInput",          "SlotReassignment"),
    ConfigEntry ("Disable Devices Connected to Specific XInput Slots",        dll_ini,         "Input.XInput",          "DisableSlots"),
    ConfigEntry ("Hook vibration; fix third-party created feedback loops",    dll_ini,         "Input.XInput",          "HookSetState"),
    ConfigEntry ("Switch a game hard-coded to use Slot 0 to an active pad",   dll_ini,         "Input.XInput",          "AutoSlotAssign"),
    ConfigEntry ("Prevent game from seeing XInput at all, useful if a game "
                 "supports native SONY input and XInput.",                    dll_ini,         "Input.XInput",          "HideAllDevices"),
    ConfigEntry ("For non-Xbox controllers, translate HID to XInput",         dll_ini,         "Input.XInput",          "EnableEmulation"),
    ConfigEntry ("In HID->XInput, filter analog values below this threshold", dll_ini,         "Input.XInput",          "DeadzonePercent"),
    ConfigEntry ("Invert the X-Axis on the Left Analog Stick",                dll_ini,         "Input.XInput",          "InvertLX"),
    ConfigEntry ("Invert the Y-Axis on the Left Analog Stick",                dll_ini,         "Input.XInput",          "InvertLY"),
    ConfigEntry ("Invert the X-Axis on the Right Analog Stick",               dll_ini,         "Input.XInput",          "InvertRX"),
    ConfigEntry ("Invert the Y-Axis on the Right Analog Stick",               dll_ini,         "Input.XInput",          "InvertRY"),
    ConfigEntry ("Swap Left and Right Analog Stick Input",                    dll_ini,         "Input.XInput",          "SwapSticks"),
    ConfigEntry ("Swap A and B to conform to Nintendo button layout",         dll_ini,         "Input.XInput",          "SwapAB"),
    ConfigEntry ("Swap X and Y to conform to Nintendo button layout",         dll_ini,         "Input.XInput",          "SwapXY"),
    ConfigEntry ("Prevent game from seeing DirectInput gamepads",             dll_ini,         "Input.DInput",          "HideGamepads"),
    ConfigEntry ("Prevent game from seeing DirectInput mice",                 dll_ini,         "Input.DInput",          "HideMice"),
    ConfigEntry ("Prevent game from seeing DirectInput keyboards",            dll_ini,         "Input.DInput",          "HideKeyboards"),
    ConfigEntry ("Prevent Steam Input from utterly destroying performance",   dll_ini,         "Input.DInput",          "PreventEnumDevices"),

    ConfigEntry ("Install hooks for libScePad",                               dll_ini,         "Input.libScePad",       "Enable"),
    ConfigEntry ("Disable Touchpad Input",                                    dll_ini,         "Input.libScePad",       "DisableTouchpad"),
    ConfigEntry ("Share Button can be used as Touchpad Click",                input_ini,       "Input.libScePad",       "ShareClicksTouchpad"),
    ConfigEntry ("Mute Button on DualSense will Mute the Game",               input_ini,       "Input.libScePad",       "MuteButtonAppliesToGame"),
    ConfigEntry ("PlayStation / Home Button activates SK's control panel and "
                 "may be used for special button combos (e.g. trigger sshot)",input_ini,       "Input.libScePad",       "AdvancedPlayStationButton"),
    ConfigEntry ("Reduced power for Audio/Gyro/Touchpad on Bluetooth",        input_ini,       "Input.libScePad",       "EnableBluetoothPowerSaving"),
    ConfigEntry ("Force Red LED Color [0,255] or -1 for No Override",         input_ini,       "Input.libScePad",       "LEDColor_R"),
    ConfigEntry ("Force Green LED Color [0,255] or -1 for No Override",       input_ini,       "Input.libScePad",       "LEDColor_G"),
    ConfigEntry ("Force Blue LED Color [0,255] or -1 for No Override",        input_ini,       "Input.libScePad",       "LEDColor_B"),
    ConfigEntry ("Force LED brightness [0,1,2,3] or -1 for No Override",      input_ini,       "Input.libScePad",       "LEDBrightness"),
    ConfigEntry ("Allow SK to use all available features over Bluetooth",     input_ini,       "Input.libScePad",       "EnableFullBluetoothSupport"),
    ConfigEntry ("Cause games to see DualShock 4 v1 as DualShock 4 v2",       dll_ini,         "Input.libScePad",       "IdentifyDualShock4AsDualShock4v2"),
    ConfigEntry ("Cause games to see DualShock 4 v2 as DualShock 4",          dll_ini,         "Input.libScePad",       "IdentifyDualShock4v2AsDualShock4"),
    ConfigEntry ("Cause games to see DualSense Edge as DualSense",            dll_ini,         "Input.libScePad",       "IdentifyDualSenseEdgeAsDualSense"),
    ConfigEntry ("Keyboard Input to Generate when Left Function is Pressed",  dll_ini,         "Input.libScePad",       "LeftFunction"),
    ConfigEntry ("Keyboard Input to Generate when Right Function is Pressed", dll_ini,         "Input.libScePad",       "RightFunction"),
    ConfigEntry ("Keyboard Input to Generate when Left Paddle is Pressed",    dll_ini,         "Input.libScePad",       "LeftPaddle"),
    ConfigEntry ("Keyboard Input to Generate when Right Paddle is Pressed",   dll_ini,         "Input.libScePad",       "RightPaddle"),
    ConfigEntry ("Keyboard Input to Generate when Touch Pad is Clicked",      dll_ini,         "Input.libScePad",       "TouchpadClick"),
    ConfigEntry ("Intensity of emulated rumble on DualSense controllers",     dll_ini,         "Input.libScePad",       "RumbleStrength"),
    ConfigEntry ("Whether to use SONY's improved rumble on DualSense",        dll_ini,         "Input.libScePad",       "ImproveDualSenseRumble"),

    ConfigEntry ("Show HID Attach Notifications if no Conflicts are Detected",input_ini,       "Input.HID",             "AlwaysShowAttachNotifications"),
    ConfigEntry ("Percentage when SK will warn controller batteries are low", input_ini,       "Input.Battery",         "WarnIfPercentIsBelow"),

 //DEPRECATED   (                                                                               "Input.XInput",          "DisableRumble"),

    ConfigEntry ("Disable Steam Input Always (only works for flat API)",      dll_ini,         "Input.Steam",           "Disable"),

    // Thread Monitoring
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Trace per-Thread Memory Allocation in Threads Widget",      dll_ini,         "Threads.Analyze",       "MemoryAllocation"),
    ConfigEntry ("Trace per-Thread File I/O Activity in Threads Widget",      dll_ini,         "Threads.Analyze",       "FileActivity"),

    // Window Management
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Borderless Window Mode",                                    dll_ini,         "Window.System",         "Borderless"),
    ConfigEntry ("Center the Window",                                         dll_ini,         "Window.System",         "Center"),
    ConfigEntry ("Render While Window is in Background",                      dll_ini,         "Window.System",         "RenderInBackground"),
    ConfigEntry ("Mute While Window is in Background",                        dll_ini,         "Window.System",         "MuteInBackground"),
    ConfigEntry ("X Offset (Percent or Absolute)",                            dll_ini,         "Window.System",         "XOffset"),
    ConfigEntry ("Y Offset (Percent or Absolute)",                            dll_ini,         "Window.System",         "YOffset"),
    ConfigEntry ("Confine the Mouse Cursor to the Game Window",               dll_ini,         "Window.System",         "ConfineCursor"),
    ConfigEntry ("Unconfine the Mouse Cursor from the Game Window",           dll_ini,         "Window.System",         "UnconfineCursor"),
    ConfigEntry ("Prevent the Mouse Cursor from Unhiding the Taskbar",        dll_ini,         "Window.System",         "PreventTaskbarUnhide"),
    ConfigEntry ("Remember where the window is dragged to",                   dll_ini,         "Window.System",         "PersistentDragPos"),
    ConfigEntry ("Make the Game Window Fill the Screen (scale to fit)",       dll_ini,         "Window.System",         "Fullscreen"),
    ConfigEntry ("Force the Client Region to this Size in Windowed Mode",     dll_ini,         "Window.System",         "OverrideRes"),
    ConfigEntry ("Allow Resolution Overrides that Span Multiple Monitors",    dll_ini,         "Window.System",         "MultiMonitorMode"),
    ConfigEntry ("Re-Compute Mouse Coordinates for Resized Windows",          dll_ini,         "Window.System",         "FixMouseCoords"),
    ConfigEntry ("Prevent (0) or Force (1) a game's window Always-On-Top",    dll_ini,         "Window.System",         "AlwaysOnTop"),
    ConfigEntry ("Prevent the Windows Screensaver from activating",           dll_ini,         "Window.System",         "DisableScreensaver"),
    ConfigEntry ("Prevent the Windows Screensaver in (Borderless) Fullscreen",dll_ini,         "Window.System",         "DisableFullscreenSaver"),
    ConfigEntry ("Allow Screensaver to work, by Disabling any Game Overrides",dll_ini,         "Window.System",         "FullyManageScreenSaver"),
    ConfigEntry ("GDI Monitor ID of Preferred Monitor",                       dll_ini,         "Window.System",         "PreferredMonitor"),
    ConfigEntry ("CCD Display Path (invariant) of Preferred Monitor",         dll_ini,         "Window.System",         "PreferredMonitorExact"),
    ConfigEntry ("Disable WndProc / ClassProc hooks (wrap instead of hook)",  dll_ini,         "Window.System",         "DontHookWndProc"),
    ConfigEntry ("Activate window after 15 frames (fixes games that think"
                 " they are running in the background)",                      dll_ini,         "Window.System",         "ActivateAtStart"),
    ConfigEntry ("The game treats the foreground window (rather than focus),"
                 " as the active application [for background render feature]",dll_ini,         "Window.System",         "TreatForegroundAsActive"),
    ConfigEntry ("Automatically re-send key release notifications for keys"
                 " that were released while the game was alt-tab'd",          dll_ini,         "Window.System",         "FixStuckAltTabKeys"),
    ConfigEntry ("Allow Special K to install a drag-n-drop handler for D3D11"
                 " texture mods and INI-related functionality.",              dll_ini,         "Window.System",         "AllowDragNDrop"),
    ConfigEntry ("Allow Special K to handle file drops for the game window.", dll_ini,         "Window.System",         "AllowFileDrops"),
    ConfigEntry ("Controls whether unresponsive apps use Window Ghosting.",   dll_ini,         "Window.System",         "AllowGhosting"),

    // Compatibility
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Disable All NVIDIA BloatWare (GeForce Experience)",         dll_ini,         "Compatibility.General", "DisableBloatWare_NVIDIA"),
    ConfigEntry ("Rehook LoadLibrary When RTSS/Steam/ReShade hook it",        dll_ini,         "Compatibility.General", "RehookLoadLibrary"),
    ConfigEntry ("Disable Functionality Not Compatible With WINE",            dll_ini,         "Compatibility.General", "UsingWINE"),
    ConfigEntry ("Disable Unnecessary DxDiagnostic BLOAT in Some Games",      dll_ini,         "Compatibility.General", "AllowDxDiagn"),
//#ifdef _M_IX86
    ConfigEntry ("Opt-in for Automatic Large Address Aware Patch on Crash",   dll_ini,         "Compatibility.General", "AutoLargeAddressPatch", IniBitness_i8086),
//#endif
    ConfigEntry ("Runs hook initialization on a separate thread; high safety",dll_ini,         "Compatibility.General", "AsyncInit"),
    ConfigEntry ("Initializes hooks in a way that ReShade will not interfere",dll_ini,         "Compatibility.General", "ReShadeMode"),
    ConfigEntry ("Avoid hooks on CreateSwapChainForHwnd",                     dll_ini,         "Compatibility.General", "FSR3Mode"),
    ConfigEntry ("Debug Level (0=Most debug code OFF, >0=Normal behavior)",   dll_ini,         "Compatibility.General", "DebugLevel"),
    ConfigEntry ("Set Default (1) or Override (2) SDL input/window behavior.",dll_ini,         "Compatibility.General", "SDLSanityLevel"),
    // Refer to SDL_hints.h, only the most useful options are exposed here...
    ConfigEntry ("SDL_JOYSTICK_WGI",                                          dll_ini,         "Compatibility.SD",     "SDL_JOYSTICK_WGI"),
    ConfigEntry ("SDL_JOYSTICK_RAWINPUT",                                     dll_ini,         "Compatibility.SD",     "SDL_JOYSTICK_RAWINPUT"),
    ConfigEntry ("SDL_DIRECTINPUT_ENABLED",                                   dll_ini,         "Compatibility.SD",     "SDL_DIRECTINPUT_ENABLED"),
    ConfigEntry ("SDL_XINPUT_ENABLED",                                        dll_ini,         "Compatibility.SD",     "SDL_XINPUT_ENABLED"),
    ConfigEntry ("SDL_JOYSTICK_HIDAPI",                                       dll_ini,         "Compatibility.SD",     "SDL_JOYSTICK_HIDAPI"),
    ConfigEntry ("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE",                            dll_ini,         "Compatibility.SD",     "FullPlayStationBluetoothSupport"),
    ConfigEntry ("SDL_JOYSTICK_HIDAPI_JOYCON_HOME_LED",                       dll_ini,         "Compatibility.SD",     "SDL_JOYSTICK_HIDAPI_JOYCON_HOME_LED"),
    ConfigEntry ("SDL_JOYSTICK_THREAD",                                       dll_ini,         "Compatibility.SD",     "SDL_JOYSTICK_THREAD"),
    ConfigEntry ("SDL_POLL_SENTINEL",                                         dll_ini,         "Compatibility.SD",     "SDL_POLL_SENTINE"),

    ConfigEntry ("Last Known Render API",                                     dll_ini,         "API.Hook",              "LastKnown"),

//#ifdef _M_IX86
    ConfigEntry ("Enable DirectDraw Hooking",                                 dll_ini,         "API.Hook",              "ddraw",    IniBitness_i8086),
    ConfigEntry ("Enable Direct3D 8 Hooking",                                 dll_ini,         "API.Hook",              "d3d8",     IniBitness_i8086),
//#endif

    ConfigEntry ("Enable Direct3D 9 Hooking",                                 dll_ini,         "API.Hook",              "d3d9"),
    ConfigEntry ("Enable Direct3D 9Ex Hooking",                               dll_ini,         "API.Hook",              "d3d9ex"),
    ConfigEntry ("Enable Native DXVK (D3D9)",                                 dll_ini,         "API.Hook",              "dxvk9"),
    ConfigEntry ("Enable Direct3D 11 Hooking",                                dll_ini,         "API.Hook",              "d3d11"),
    ConfigEntry ("Enable Direct3D 12 Hooking",                                dll_ini,         "API.Hook",              "d3d12"),

//#ifdef _M_AMD64
    ConfigEntry ("Enable Vulkan Hooking",                                     dll_ini,         "API.Hook",              "Vulkan",   IniBitness_AMD64),
//#endif

    ConfigEntry ("Enable OpenGL Hooking",                                     dll_ini,         "API.Hook",              "OpenG"),
    ConfigEntry ("Enable OpenGL Debugging",                                   dll_ini,         "OpenGL.System",         "EnableDebug"),

    // Misc.
    //////////////////////////////////////////////////////////////////////////

      // Hidden setting (it will be read, but not written -- setting this is discouraged and I intend to phase it out)
    ConfigEntry ("Memory Reserve Percentage",                                 dll_ini,         "Manage.Memory",         "ReservePercent"),


    // General Mod System Settings
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Log Silence",                                               dll_ini,         "SpecialK.System",       "Silent"),
    ConfigEntry ("Trace DLL Loading (needed for dynamic API detection)",      dll_ini,         "SpecialK.System",       "TraceLoadLibrary"),
    ConfigEntry ("Log Verbosity (0=General, 5=Insane Debug)",                 dll_ini,         "SpecialK.System",       "LogLevel"),
    ConfigEntry ("Use Custom Crash Handler",                                  dll_ini,         "SpecialK.System",       "UseCrashHandler"),
    ConfigEntry ("Disable Crash Sound",                                       dll_ini,         "SpecialK.System",       "NoCrashSound"),
    ConfigEntry ("Try to Recover from Exceptions that Would Cause a Crash",   dll_ini,         "SpecialK.System",       "EnableCrashSuppression"),
    ConfigEntry ("Halt Special K Initialization Until Debugger is Attached",  dll_ini,         "SpecialK.System",       "WaitForDebugger"),
    ConfigEntry ("Print Application's Debug Output in real-time",             dll_ini,         "SpecialK.System",       "DebugOutput"),
    ConfigEntry ("Log Application's Debug Output",                            dll_ini,         "SpecialK.System",       "GameOutput"),
    ConfigEntry ("Delay Global Injection Initialization for x-many Seconds",  dll_ini,         "SpecialK.System",       "GlobalInjectDelay"),
    ConfigEntry ("At Application Exit, make SKIF the new Foreground Window",  dll_ini,         "SpecialK.System",       "ReturnToSKIF"),
    ConfigEntry ("Automatically load .asi files from the game's directory",   dll_ini,         "SpecialK.System",       "AutoLoadASIFiles"),
#ifdef SK_USE_CLEAN_EXIT
    ConfigEntry ("Did the game exit cleanly the last time it ran?",           dll_ini,         "SpecialK.System",       "CleanExit"),
#endif
    ConfigEntry ("The last version that wrote the config file",               dll_ini,         "SpecialK.System",       "Version"),


    ConfigEntry ("Force Fullscreen Mode",                                     dll_ini,         "Display.Output",        "ForceFullscreen"),
    ConfigEntry ("Force Windowed Mode",                                       dll_ini,         "Display.Output",        "ForceWindowed"),
    ConfigEntry ("Force 10-bpc (SDR) Output",                                 dll_ini,         "Display.Output",        "Force10bpcSDR"),
    ConfigEntry ("Fill monitor background (eg. black bars) in windowed mode", dll_ini,         "Display.Output",        "AspectRatioStretch"),
    ConfigEntry ("Displays black background on all except the game's monitor",dll_ini,         "Display.Output",        "MultiMonitorADHDRelief"),
    ConfigEntry ("Whenever the game loses input focus, ADHD mode turns off",  osd_ini,         "Display.Monitor",       "MultiMonitorFocusIsFocused"),
    ConfigEntry ("Allow Current Game to change Refresh Rate",                 dll_ini,         "Display.Output",        "AllowRefreshRateChanges"),
    ConfigEntry ("Dump Raw EDID data from NVAPI if supported",                dll_ini,         "Display.Output",        "DumpRawEDID"),


    // Framerate Limiter
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Framerate Target (negative signed values are non-limiting)",dll_ini,         "Render.FrameRate",      "TargetFPS"),
    ConfigEntry ("Framerate Target (window in background;  0.0 = same as fg)",dll_ini,         "Render.FrameRate",      "BackgroundFPS"),
    ConfigEntry ("Refresh rate the last time framerate limit was configured.",dll_ini,         "Render.FrameRate",      "LastRefreshRate"),
    ConfigEntry ("The monitor the last time framerate limit was configured.", dll_ini,         "Render.FrameRate",      "LastMonitorPath"),
    ConfigEntry ("Limiter Will Wait for VBLANK",                              dll_ini,         "Render.FrameRate",      "WaitForVBLANK"),
    ConfigEntry ("Number of (Back)Buffers in the Swapchain",                  dll_ini,         "Render.FrameRate",      "BackBufferCount"),
    ConfigEntry ("Number of (Back)Buffers in the Swapchain",                  dll_ini,         "Render.FrameRate",      "BufferCount"),
    ConfigEntry ("Presentation Interval (VSYNC)",                             dll_ini,         "Render.FrameRate",      "PresentationInterval"),
    ConfigEntry ("Maximum Sync Interval (Clamp VSYNC)",                       dll_ini,         "Render.FrameRate",      "SyncIntervalClamp"),
    ConfigEntry ("Tearing Mode (Always On/Off or Adaptive)",                  dll_ini,         "Render.FrameRate",      "TearingMode"),
    ConfigEntry ("Latency reduction behavior (Smooth or Aggressive)",         dll_ini,         "Render.FrameRate",      "LatencyMode"),
    ConfigEntry ("Max Render Latency for LowLatency/Adaptive Tearing Mode",   dll_ini,         "Render.FrameRate",      "RenderQueue"),
    ConfigEntry ("Maximum Frames to Render-Ahead",                            dll_ini,         "Render.FrameRate",      "PreRenderLimit"),
    ConfigEntry ("Sleep Free Render Thread",                                  dll_ini,         "Render.FrameRate",      "SleeplessRenderThread"),
    ConfigEntry ("Sleep Free Window Thread",                                  dll_ini,         "Render.FrameRate",      "SleeplessWindowThread"),
    ConfigEntry ("Enable Multimedia Class Scheduling for FPS Limiter Sleep",  dll_ini,         "Render.FrameRate",      "EnableMMCSS"),
    ConfigEntry ("Fullscreen Refresh Rate",                                   dll_ini,         "Render.FrameRate",      "RefreshRate"),
    ConfigEntry ("Fullscreen Rational Scan Rate (precise refresh rate)",      dll_ini,         "Render.FrameRate",      "RescanRatio"),

    ConfigEntry ("Place Framerate Limiter Wait Before/After Present, etc.",   dll_ini,         "Render.FrameRate",      "LimitEnforcementPolicy"),
    ConfigEntry ("Use ETW tracing (PresentMon) for extra latency/flip info",  dll_ini,         "Render.FrameRate",      "EnableETWTracing"),
    ConfigEntry ("Use AMD Power-Saving Instructions for Busy-Wait",           dll_ini,         "Render.FrameRate",      "UseAMDMWAITX"),
    ConfigEntry ("Apply Pacing to Native frames when using DLSS Frame Gen.",  dll_ini,         "Render.FrameRate",      "EnableStreamlinePacing"),
    ConfigEntry ("Level of DLSS Frame Gen pacing latency reduction.",         dll_ini,         "Render.FrameRate",      "StreamlinePacingMode"),
    ConfigEntry ("Ignore environment variable-defined framerate limits.",     dll_ini,         "Render.FrameRate",      "IgnoreEnvironmentVars"),
    ConfigEntry ("Boost Compositor Clock on Windows 11+ (Dynamic Refresh)",   dll_ini,         "Render.FrameRate",      "BoostCompositorClock"),
    ConfigEntry ("Minimum percentage of limiter time spent busy-waiting.",    dll_ini,         "Render.FrameRate",      "BusyWaitPercent"),
    ConfigEntry ("How aggressively (scale of 0-10) to switch to busy-wait"
                 " for wait durations exceeding the scheduler's resolution.", dll_ini,         "Render.FrameRate",      "BusyWaitBias"),

    ConfigEntry ("Maximum number of CPU-side frames to work ahead of GPU.",   dll_ini,         "FrameRate.Engine",      "MaxRenderAheadFrames"),
    ConfigEntry ("Allow the game to use a Latency Waitable SwapChain.",       dll_ini,         "FrameRate.Engine",      "AllowDXGILatencyWait"),
    ConfigEntry ("Number of CPU cores to tell the game about",                dll_ini,         "FrameRate.Engine",      "OverrideCPUCoreCount"),
    ConfigEntry ("Set the process timer resolution to the maximum supported", dll_ini,         "FrameRate.Engine",      "UseMaxTimerResolution"),
    ConfigEntry ("Force Waitable Timer code to use Win10 High-Res Timers",    dll_ini,         "FrameRate.Engine",      "ForceHighResTimers"),
    ConfigEntry ("Pace the game thread in supported engines (i.e. Unity)",    dll_ini,         "FrameRate.Engine",      "PaceGameThread"),
    ConfigEntry ("Offset in Scanlines from Top of Screen to Steer Tearing",   dll_ini,         "FrameRate.LatentSync",  "TearlineOffset"),
    ConfigEntry ("Frequency (in -frames or milliseconds) to Resync Timing",   dll_ini,         "FrameRate.LatentSync",  "ResyncFrequency"),
    ConfigEntry ("Controls Distribution of Idle Time Per-Delayed Frame",      dll_ini,         "FrameRate.LatentSync",  "DelayBias"),
    ConfigEntry ("Automatically Sets Delay Bias For Minimum Latency",         dll_ini,         "FrameRate.LatentSync",  "AutoBias"),
    ConfigEntry ("Target input latency (in milliseconds or %) for auto-bias", dll_ini,         "FrameRate.LatentSync",  "AutoBiasTarget"),
    ConfigEntry ("Maximum percentage to bias towards low input latency",      dll_ini,         "FrameRate.LatentSync",  "MaxAutoBias"),
    ConfigEntry ("Enable __SK_LatentSyncSkip in 2x.. mode",                   dll_ini,         "FrameRate.LatentSync",  "SkipFrames"),

    ConfigEntry ("Force Vulkan to use Mailbox Presentation Mode",             dll_ini,         "Render.Vulkan",         "ForceMailboxPresent"),
    ConfigEntry ("Force Vulkan to use FIFO Relaxed Presentation Mode",        dll_ini,         "Render.Vulkan",         "ForceAdaptiveVSYNC"),
    ConfigEntry ("Enable DWM Tearing (Windows 10+)",                          dll_ini,         "Render.DXGI",           "AllowTearingInDWM"),
    ConfigEntry ("Enable Flip Model to Render (and drop) frames at rates >"
                 "refresh rate with VSYNC enabled (similar to NV Fast Sync).",dll_ini,         "Render.DXGI",           "DropLateFrames"),
    ConfigEntry ("If G-Sync is seen supported, automatically optimize the"
                 "limiter for low-latency.",                                  dll_ini,         "Render.DXGI",           "AutoLowLatency"),
    ConfigEntry ("Indicates that Auto VRR activated and has turned off",      dll_ini,         "Render.DXGI",           "AutoLowLatencyTriggered"),
    ConfigEntry ("Auto Low-Latency Mode may add stutter to get lower latency",input_ini,       "Input.AutoLowLatency",  "UltraLowLatency"),
    ConfigEntry ("Global policy applied when starting a game the first time", input_ini,       "Input.AutoLowLatency",  "DefaultPolicy"),
    ConfigEntry ("Global policy to reapply AutoVRR if display/refresh change",input_ini,       "Input.AutoLowLatency",  "AutoReapply"),

    ConfigEntry ("Enable NVIDIA Reflex Integration w/ SK's limiter",          dll_ini,         "NVIDIA.Reflex",         "Enable"),
    ConfigEntry ("Low Latency Mode",                                          dll_ini,         "NVIDIA.Reflex",         "LowLatency"),
    ConfigEntry ("Reflex Boost (lower-latency power scaling)",                dll_ini,         "NVIDIA.Reflex",         "LowLatencyBoost"),
    ConfigEntry ("Train Reflex using Latency Markers for Optimization",       dll_ini,         "NVIDIA.Reflex",         "OptimizeByMarkers"),
    ConfigEntry ("When to apply Reflex's magic",                              dll_ini,         "NVIDIA.Reflex",         "EngagementPolicy"),
    ConfigEntry ("Use SK's Reflex Mode options instead of the game's",        dll_ini,         "NVIDIA.Reflex",         "OverrideNativeMode"),
    ConfigEntry ("Use Reflex's framerate limiter (SK's target) instead of SK",dll_ini,         "NVIDIA.Reflex",         "UseFramerateLimiter"),
    ConfigEntry ("Use Reflex's framerate limiter (AND SK's)",                 dll_ini,         "NVIDIA.Reflex",         "CombineFramerateLimiters"),
    ConfigEntry ("Disable a game's native Reflex implementation",             dll_ini,         "NVIDIA.Reflex",         "DisableNative"),
    ConfigEntry ("Show detailed stage timing pipeline diagram on widget",     osd_ini,         "NVIDIA.Reflex",         "ShowDetailsInWidget"),

    ConfigEntry ("Force DLAA in games that do not normally support it",       dll_ini,         "NVIDIA.DLSS",           "ForceDLAA"),
    ConfigEntry ("Override DLSS Sharpening Mode",                             dll_ini,         "NVIDIA.DLSS",           "UseSharpening"),
    ConfigEntry ("Sharpness Value to Use",                                    dll_ini,         "NVIDIA.DLSS",           "ForcedSharpness"),
    ConfigEntry ("Always load SK's Plug-In DLSS DLL instead of the game's",   dll_ini,         "NVIDIA.DLSS",           "AutoRedirectDL"),
    ConfigEntry ("Override DLSS Perf/Quality Level's Preset",                 dll_ini,         "NVIDIA.DLSS",           "ForcePreset"),
    ConfigEntry ("Override Ray Reconstruction Perf/Quality Level's Preset",   dll_ini,         "NVIDIA.DLSS",           "ForcePresetRR"),
    ConfigEntry ("Override DLSS Auto Exposure",                               dll_ini,         "NVIDIA.DLSS",           "ForcedAutoExposure"),
    ConfigEntry ("Override DLSS Alpha Upscaling (3.7.0+)",                    dll_ini,         "NVIDIA.DLSS",           "ForceAlphaUpscale"),
    ConfigEntry ("Allow forcing multi-frame generation on in older games.",   dll_ini,         "NVIDIA.DLSS",           "ForcedMaxMultiFrameCount"),
    ConfigEntry ("Override Ray Reconstruction Hardware Depth",                dll_ini,         "NVIDIA.DLSS",           "ForcedHardwareDepth"),
    ConfigEntry ("Custom scale factor (if != 0.0f) to use for Performance",   dll_ini,         "NVIDIA.DLSS",           "CustomPerformanceScale"),
    ConfigEntry ("Custom scale factor (if != 0.0f) to use for Balanced",      dll_ini,         "NVIDIA.DLSS",           "CustomBalancedScale"),
    ConfigEntry ("Custom scale factor (if != 0.0f) to use for Quality",       dll_ini,         "NVIDIA.DLSS",           "CustomQualityScale"),
    ConfigEntry ("Custom scale factor (if != 0.0f) to use for Ultra Perf.",   dll_ini,         "NVIDIA.DLSS",           "CustomUltraPerfScale"),
    ConfigEntry ("Minimum Dynamic Resolution (scale) used by custom scales",  dll_ini,         "NVIDIA.DLSS",           "CustomMinDynamicRes"),
    ConfigEntry ("Maximum Dynamic Resolution (scale) used by custom scales",  dll_ini,         "NVIDIA.DLSS",           "CustomMaxDynamicRes"),
    ConfigEntry ("Spoof AppId for compatibility",                             dll_ini,         "NVIDIA.DLSS",           "OverrideAppId"),
    ConfigEntry ("Add extra pixels when forcing DLAA",                        dll_ini,         "NVIDIA.DLSS",           "ExtraPixelsForDLAA"),
    ConfigEntry ("Disable OTA updates (i.e. NVIDIA phone-home every launch)", dll_ini,         "NVIDIA.DLSS",           "DisableOTAUpdates"),
    ConfigEntry ("Show the in-use features in the DLSS settings tab",         osd_ini,         "NVIDIA.DLSS",           "ShowActiveFeatures"),
    ConfigEntry ("Allow scRGB even if DLSS-G DLLs are detected",              dll_ini,         "NVIDIA.DLSS",           "AllowSCRGBinDLSSG"),
    ConfigEntry ("Allow fake frame pacing stats for fake frames?",            dll_ini,         "NVIDIA.DLSS",           "AllowFlipMetering"),
    ConfigEntry ("Report all NGX (D3D11/D3D12) features supported on all HW.",dll_ini,         "NVIDIA.DLSS",           "SpoofFeatureSupport"),
    ConfigEntry ("Output Streamline Framework's Debug to game_output.log.",   dll_ini,         "NVIDIA.DLSS",           "UseStreamlineDebugLog"),
    ConfigEntry ("Prevent DLSS5 from being used.",                            dll_ini,         "NVIDIA.DLSS",           "SlopStop5000"),

    ConfigEntry ("Experimental - Use 32bpc for HDR",                          dll_ini,         "SpecialK.HDR",          "Enable128BitPipeline"),
    ConfigEntry ("Do not use Floating-Point RTs when re-mastering 8-bpc+ RTs",dll_ini,         "SpecialK.HDR",          "Keep8BpcRemastersUNORM"),
    ConfigEntry ("Do not use FP RTs when re-mastering reduced resolution RTS",dll_ini,         "SpecialK.HDR",          "KeepSubnativeRemastersUNORM"),
    ConfigEntry ("Last Used DXGI Colorspace; auto-enables HDR features...",   dll_ini,         "SpecialK.HDR",          "LastUsedColorSpace"),

    ConfigEntry ("Changes hook order in order to allow recording the OSD.",   dll_ini,         "Render.OSD",            "ShowInVideoCapture"),

    // OpenGL
    //////////////////////////////////////////////////////////////////////////
    ConfigEntry ("Enable OpenGL Debug Output (in legacy OpenGL games)",       dll_ini,         "OpenGL.System",         "EnableDebug"),
    ConfigEntry ("Use 10-bpc in SDR if the game's pixel type allows it",      dll_ini,         "OpenGL.System",         "Prefer10bpc"),
    ConfigEntry ("Use the most precise Z-buffer possible (usually 24-bit)",   dll_ini,         "OpenGL.System",         "UpgradeZBuffer"),

    // D3D9
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Force D3D9Ex Context",                                      dll_ini,         "Render.D3D9",           "ForceD3D9Ex"),
    ConfigEntry ("Force PURE device off",                                     dll_ini,         "Render.D3D9",           "ForceImpure"),
    ConfigEntry ("Enable Texture Modding Support",                            dll_ini,         "Render.D3D9",           "EnableTextureMods"),
    ConfigEntry ("Enable D3D9Ex FlipEx SwapEffect",                           dll_ini,         "Render.D3D9",           "EnableFlipEx"),
    ConfigEntry ("Use D3D9On12 instead of normal driver",                     dll_ini,         "Render.D3D9",           "UseD3D9On12"),


    // D3D10/11/12
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Maximum Frame Delta Time",                                  dll_ini,         "Render.DXGI",           "MaxDeltaTime"),
    ConfigEntry ("Use Flip Discard - Windows 10+",                            dll_ini,         "Render.DXGI",           "UseFlipDiscard"),
    ConfigEntry ("Force Sequential (requires UseFlipDiscard or native Flip)", dll_ini,         "Render.DXGI",           "ForceFlipSequential"),
    ConfigEntry ("Disable Flip Model - Fix AMD Drivers in Yakuza0",           dll_ini,         "Render.DXGI",           "DisableFlipModel"),

    ConfigEntry ("Override DXGI Adapter",                                     dll_ini,         "Render.DXGI",           "AdapterOverride"),
    ConfigEntry ("Maximum Resolution To Report",                              dll_ini,         "Render.DXGI",           "MaxRes"),
    ConfigEntry ("Minimum Resolution To Report",                              dll_ini,         "Render.DXGI",           "MinRes"),
    ConfigEntry ("Maximum Refresh To Report",                                 dll_ini,         "Render.DXGI",           "MaxRefresh"),
    ConfigEntry ("Minimum Refresh To Report",                                 dll_ini,         "Render.DXGI",           "MinRefresh"),

    ConfigEntry ("Time to wait in msec. for SwapChain",                       dll_ini,         "Render.DXGI",           "SwapChainWait"),
    ConfigEntry ("Scaling Preference (DontCare | Centered | Stretched"
                 " | Unspecified)",                                           dll_ini,         "Render.DXGI",           "Scaling"),
    ConfigEntry ("D3D11 Exception Handling (DontCare | Raise | Ignore)",      dll_ini,         "Render.DXGI",           "ExceptionMode"),

    ConfigEntry ("DXGI Debug Layer Support",                                  dll_ini,         "Render.DXGI",           "EnableDebugLayer"),
    ConfigEntry ("Scanline Order (DontCare | Progressive | LowerFieldFirst |"
                 " UpperFieldFirst )",                                        dll_ini,         "Render.DXGI",           "ScanlineOrder"),
    ConfigEntry ("Screen Rotation (DontCare | Identity | 90 | 180 | 270 )",   dll_ini,         "Render.DXGI",           "Rotation"),
    ConfigEntry ("Test SwapChain Presentation Before Actually Presenting",    dll_ini,         "Render.DXGI",           "TestSwapChainPresent"),
    ConfigEntry ("Use 32-bit Depth + 8-bit Stencil + 24-bit Padding",         dll_ini,         "Render.DXGI",           "Use64BitDepthStencil"),
    ConfigEntry ("Isolate D3D11 Deferred Context Queues instead of Tracking"
                 " in Immediate Mode.",                                       dll_ini,         "Render.DXGI",           "IsolateD3D11DeferredContexts"),
    ConfigEntry ("Override ON-SCREEN Multisample Antialiasing Level;-1=None", dll_ini,         "Render.DXGI",           "OverrideMSAA"),
    ConfigEntry ("Nix the swapchain present flag: DXGI_PRESENT_TEST to "
                 "workaround bad third-party software that doesn't handle it"
                 " correctly.",                                               dll_ini,         "Render.DXGI",           "SkipSwapChainPresentTest"),
    ConfigEntry ("How to handle sRGB SwapChains when we have to kill them for"
                 " Flip Model support (-1=Passthrough, 0=Strip, 1=Apply)",    dll_ini,         "Render.DXGI",           "sRGBBypassBehavior"),
    ConfigEntry ("Disable D3D11 Render Mods (for slight perf. increase)",     dll_ini,         "Render.DXGI",           "LowSpecMode"),
    ConfigEntry ("Prevent games from detecting monitor HDR support",          dll_ini,         "Render.DXGI",           "HideHDRSupport"),
    ConfigEntry ("Override the HDR header metadata type set by a game",       dll_ini,         "Render.DXGI",           "HDRMetadataType"),
    ConfigEntry ("Cache DXGI Factories to reduce display mode list overhead", dll_ini,         "Render.DXGI",           "UseFactoryCache"),
    ConfigEntry ("Try to keep resolution setting changes to a minimum",       dll_ini,         "Render.DXGI",           "SkipRedundantModeChanges"),
    ConfigEntry ("Temporarily Enable DWM-based HDR while the game runs",      dll_ini,         "Render.DXGI",           "TemporaryDesktopHDRMode"),
    ConfigEntry ("Disable Dynamic Refresh Rate (VBLANK Virtualization)",      dll_ini,         "Render.DXGI",           "DisableVirtualizedBlanking"),
    ConfigEntry ("Clear the SwapChain Backbuffer every frame",                dll_ini,         "Render.DXGI",           "ClearFlipModelBackbuffers"),
    ConfigEntry ("Warn if VRAM used exceeds this % of available VRAM",        dll_ini,         "Render.DXGI",           "WarnIfUsedVRAMPercentExceeds"),
    ConfigEntry ("Feel like shooting your foot with unsafe d3d12 settings..?",dll_ini,         "Render.DXGI",           "AllowD3D12FootGuns"),
    ConfigEntry ("Lie to games and tell them they're in FSE, all the while, "
                 "they are actually running a fullscreen borderless window.", dll_ini,         "Render.DXGI",           "FakeFullscreenMode"),
    ConfigEntry ("Multiplier for reported VRAM budget for D3D12 era engines.",dll_ini,         "Render.DXGI",           "VRAMBudgetScale"),

    ConfigEntry ("Maximum Anisotropic Filter Level",                          dll_ini,         "Render.D3D12",          "MaxAnisotropy"),
    ConfigEntry ("Forced Anisotropic Filtering",                              dll_ini,         "Render.D3D12",          "ForceAnisotropic"),
    ConfigEntry ("Forced LOD Bias",                                           dll_ini,         "Render.D3D12",          "ForceLODBias"),

    ConfigEntry ("Disable DirectStorage BypassIO",                            dll_ini,         "Render.DStorage",       "DisableBypassIO"),
    ConfigEntry ("Disable DirectStorage Telemetry",                           dll_ini,         "Render.DStorage",       "DisableTelemetry"),
    ConfigEntry ("Disable DirectStorage (1.2) GPU Decompression",             dll_ini,         "Render.DStorage",       "DisableGPUDecompression"),
    ConfigEntry ("Force DirectStorage File Buffering",                        dll_ini,         "Render.DStorage",       "ForceFileBuffering"),
    ConfigEntry ("Override default number of DirectStorage Submit threads",   dll_ini,         "Render.DStorage",       "NumberOfSubmitThreads"),
    ConfigEntry ("Override default number of CPU Decompression threads",      dll_ini,         "Render.DStorage",       "NumberOfCPUDecompThreads"),
    ConfigEntry ("Hook DirectStorage for additional features",                dll_ini,         "Render.DStorage",       "EnableHooks"),
    ConfigEntry ("Create a standalone D3D12 device if a DStorage game tries "
                 "to allocate a DStorage queue with no D3D12 device supplied",dll_ini,         "Render.DStorage",       "UseDummyD3D12DeviceIfNeeded"),
    ConfigEntry ("Clamp Negative LOD Bias",                                   dll_ini,         "Textures.D3D9",         "ClampNegativeLODBias"),
    ConfigEntry ("Cache Textures",                                            dll_ini,         "Textures.D3D11",        "Cache"),
    ConfigEntry ("Adds L3 to Hierarchical Cache;  L3=Fmt,  L2=Mips,  L1=Res", dll_ini,         "Textures.D3D11",        "CacheUsingL3Hash"),
    ConfigEntry ("Precise Hash Generation",                                   dll_ini,         "Textures.D3D11",        "PreciseHash"),

    ConfigEntry ("Inject Textures",                                           dll_ini,         "Textures.D3D11",        "Inject"),
    ConfigEntry ("Allow image format to change during texture injection",     dll_ini,         "Textures.D3D11",        "InjectionKeepsFormat"),
    ConfigEntry ("Create complete mipmap chain for textures without them",    dll_ini,         "Textures.D3D11",        "GenerateMipmaps"),
    ConfigEntry ("Resource Root",                                             dll_ini,         "Textures.General",      "ResourceRoot"),
    ConfigEntry ("Dump Textures while Loading",                               dll_ini,         "Textures.General",      "DumpOnFirstLoad"),
    ConfigEntry ("Minimum Cached Textures",                                   dll_ini,         "Textures.Cache",        "MinEntries"),
    ConfigEntry ("Maximum Cached Textures",                                   dll_ini,         "Textures.Cache",        "MaxEntries"),
    ConfigEntry ("Minimum Textures to Evict",                                 dll_ini,         "Textures.Cache",        "MinEvict"),
    ConfigEntry ("Maximum Textures to Evict",                                 dll_ini,         "Textures.Cache",        "MaxEvict"),
    ConfigEntry ("Minimum Data Size to Evict",                                dll_ini,         "Textures.Cache",        "MinSizeInMiB"),
    ConfigEntry ("Maximum Data Size to Evict",                                dll_ini,         "Textures.Cache",        "MaxSizeInMiB"),

    ConfigEntry ("Ignore textures without mipmaps?",                          dll_ini,         "Textures.Cache",        "IgnoreNonMipmapped"),
    ConfigEntry ("Enable texture caching/dumping/injecting staged textures",  dll_ini,         "Textures.Cache",        "AllowStaging"),
    ConfigEntry ("For games with broken resource reference counting, allow"
                 " textures to be cached anyway (needed for injection).",     dll_ini,         "Textures.Cache",        "AllowUnsafeRefCounting"),
    ConfigEntry ("Actively manage D3D11 teture residency",                    dll_ini,         "Textures.Cache",        "ManageResidency"),


    ConfigEntry ("Disable NvAPI",                                             dll_ini,         "NVIDIA.API",            "Disable"),
    ConfigEntry ("Prevent Game from Using NvAPI HDR Features",                dll_ini,         "NVIDIA.API",            "DisableHDR"),
    ConfigEntry ("Enable/Disable Vulkan DXGI Layer",                          dll_ini,         "NVIDIA.API",            "EnableVulkanBridge"),
    ConfigEntry ("By default, Special K disables Ansel at first launch, but"
                 " users have an option under 'Help|..' to turn it back on.", dll_ini,         "NVIDIA.Bugs",           "AnselSleepsWithFishes"),
    ConfigEntry ("Forcefully block nvcamera{64}.dll",                         dll_ini,         "NVIDIA.Bugs",           "DisableAnselShimLoader"),
    ConfigEntry ("Alternate DXGI/D3D11/D3D12 Hook Implementation for NV BUG", dll_ini,         "NVIDIA.Bugs",           "StreamlineCompatibilityMode"),
    ConfigEntry ("Compat hack for games that use DLSS FrameGen without Sleep",dll_ini,         "NVIDIA.Bugs",           "ReflexNeverSleeps"),
    ConfigEntry ("SLI Compatibility Bits",                                    dll_ini,         "NVIDIA.SLI",            "CompatibilityBits"),
    ConfigEntry ("SLI GPU Count",                                             dll_ini,         "NVIDIA.SLI",            "NumberOfGPUs"),
    ConfigEntry ("SLI Mode",                                                  dll_ini,         "NVIDIA.SLI",            "Mode"),
    ConfigEntry ("Override Driver Defaults",                                  dll_ini,         "NVIDIA.SLI",            "Override"),

    ConfigEntry ("Disable AMD's ADL library",                                 dll_ini,         "AMD.AD",               "Disable"),
    ConfigEntry ("Disable Microsoft's D3DKMT Performance Data",               dll_ini,         "Microsoft.D3DKMT",      "DisablePerfData"),

    ConfigEntry ("Corner to display notifications, 0=Top-Left,1=Top-Right,"
                 "2=Bottom-Left,3=Bottom-Right",                              notify_ini,      "Notification.System",   "Location"),
    ConfigEntry ("Will not draw notifications until user requests them.",     notify_ini,      "Notification.System",   "Silent"),

    ConfigEntry ("Draw ReShade before SK's overlay in AddOn capable versions",dll_ini,         "ReShade.System",        "DrawFirst"),
    ConfigEntry ("Supress warnings for incompatible ReShade AddOns",          dll_ini,         "ReShade.System",        "UnsafeAddOns"),
    ConfigEntry ("Enable Special K's ReShade Add-On when RenoDX is in use.",  dll_ini,         "ReShade.System",        "AllowSKAddOnWithRenoDX"),
    ConfigEntry ("Respond to creation and destruction of ReShade runtimes.",  dll_ini,         "ReShade.System",        "AllowRuntimeTracking"),
    ConfigEntry ("Add a second framerate limiter to compensate for ReShade.", dll_ini,         "ReShade.System",        "RequireFrameGenPacingFix"),

    ConfigEntry ("Show Software EULA",                                        dll_ini,         "SpecialK.System",       "ShowEULA"),
    ConfigEntry ("Disable Alpha Transparency (reduce flicker)",               dll_ini,         "ImGui.Render",          "DisableAlpha"),
    ConfigEntry ("Reduce Aliasing on (but dim) Line Edges",                   dll_ini,         "ImGui.Render",          "AntialiasLines"),
    ConfigEntry ("Reduce Aliasing on (but widen) Window Borders",             dll_ini,         "ImGui.Render",          "AntialiasContours"),

    ConfigEntry ("Disable DPI Scaling",                                       dll_ini,         "DPI.Scaling",           "Disable"),
    ConfigEntry ("Windows 8.1+ Monitor DPI Awareness (needed for UE4)",       dll_ini,         "DPI.Scaling",           "PerMonitorAware"),
    ConfigEntry ("Further fixes required for UE4",                            dll_ini,         "DPI.Scaling",           "MonitorAwareOnAllThreads"),

    ConfigEntry ("Log file read activity to logs/file_read.log",              dll_ini,         "FileIO.Trace",          "LogReads"),
    ConfigEntry ("Don't log activity for files in this list",                 dll_ini,         "FileIO.Trace",          "IgnoreReads"),
    ConfigEntry ("Log file write activity to logs/file_write.log",            dll_ini,         "FileIO.Trace",          "LogWrites"),
    ConfigEntry ("Don't log activity for files in this list",                 dll_ini,         "FileIO.Trace",          "IgnoreWrites"),

    ConfigEntry ("Power Policy (GUID) to Apply At Application Start",         dll_ini,         "CPU.Power",             "PowerSchemeGUID"),

    ConfigEntry ("Always boost process priority to Above Normal",             dll_ini,         "Scheduler.Boost",       "AlwaysRaisePriority"),
    ConfigEntry ("Boost process priority to Above Normal in Background",      dll_ini,         "Scheduler.Boost",       "RaisePriorityInBackground"),
    ConfigEntry ("Boost process priority to Above Normal in Foreground",      dll_ini,         "Scheduler.Boost",       "RaisePriorityInForeground"),
    ConfigEntry ("Boost process priority to High instead of Above Normal",    dll_ini,         "Scheduler.Boost",       "RaisePriorityToHigh"),
    ConfigEntry ("Do not allow third-party apps to change priority",          dll_ini,         "Scheduler.Boost",       "DenyForeignChanges"),
    ConfigEntry ("Minimum priority for a game's render thread",               dll_ini,         "Scheduler.Boost",       "MinimumRenderThreadPriority"),
    ConfigEntry ("Mask of CPU cores the process is eligible for scheduling.", dll_ini,         "Scheduler.System",      "ProcessorAffinityMask"),
    ConfigEntry ("Limit the scheduler to Intel P-Cores",                      dll_ini,         "Scheduler.System",      "PerformanceCoresOnly"),

    ConfigEntry ("Minimize Audio Latency while Game is Running",              dll_ini,         "Sound.Mixing",          "MinimizeLatency"),

    // Control the behavior of SKIF rather than the other way around
    ConfigEntry ("Control when SKIF auto-stops, 0=Never, 1=AtStart, 2=AtExit",platform_ini,    "SKIF.System",           "AutoStopBehavior"),


    // --- Not anymore, now it's global
    //
    // The one odd-ball Steam achievement setting that can be specified per-game
  //ConfigEntry ("Achievement Sound File",                                    dll_ini,         "Platform.Achievements", "SoundFile"),

    // Steam Achievement Enhancements  (Global Settings)
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Achievement Sound File",                                    platform_ini,    "Platform.Achievements", "SoundFile"),
    ConfigEntry ("Silence is Bliss?",                                         platform_ini,    "Platform.Achievements", "PlaySound"),
    ConfigEntry ("Precious Memories",                                         platform_ini,    "Platform.Achievements", "TakeScreenshot"),
    ConfigEntry ("Friendly Competition",                                      platform_ini,    "Platform.Achievements", "FetchFriendStats"),
    ConfigEntry ("Achievement Popup Position",                                platform_ini,    "Platform.Achievements", "PopupOrigin"),
    ConfigEntry ("Achievement Notification Animation",                        platform_ini,    "Platform.Achievements", "AnimatePopup"),
    ConfigEntry ("Achievement Popup Includes Game Title?",                    platform_ini,    "Platform.Achievements", "ShowPopupTitle"),
    ConfigEntry ("Achievement Notification Inset X",                          platform_ini,    "Platform.Achievements", "PopupInset"),
    ConfigEntry ("Achievement Popup Duration (in ms)",                        platform_ini,    "Platform.Achievements", "PopupDuration"),
    ConfigEntry ("Maximum columns to fill if achievement popups do not fit",  platform_ini,    "Platform.Achievements", "MaxPopupColumns"),
    ConfigEntry ("Maximum number of achievement popups visible at once",      platform_ini,    "Platform.Achievements", "MaxPopupsOnScreen"),

    ConfigEntry ("Overlay Notification Position  (non-Big Picture Mode)",     dll_ini,         "Platform.System",       "NotifyCorner"),
    ConfigEntry ("Pause Overlay Aware games when control panel is visible",   dll_ini,         "Platform.System",       "ReuseOverlayPause"),
    ConfigEntry ("The Steam AppID for this non-Steam game if it exists",      dll_ini,         "Platform.System",       "EquivalentSteamApp"),
    ConfigEntry ("String identifying the detected store for this game",       dll_ini,         "Platform.System",       "Type"),

    ConfigEntry ("Steam AppID",                                               dll_ini,         "Steam.System",          "AppID"),
    ConfigEntry ("Delay SteamAPI initialization if the game doesn't do it",   dll_ini,         "Steam.System",          "AutoInitDelay"),
    ConfigEntry ("Should we force the game to run Steam callbacks?",          dll_ini,         "Steam.System",          "AutoPumpCallbacks"),
    ConfigEntry ("Block the User Stats Receipt Callback?",                    dll_ini,         "Steam.System",          "BlockUserStatsCallback"),
    ConfigEntry ("Filter Unrelated Data from the User Stats Receipt Callback",dll_ini,         "Steam.System",          "FilterExternalDataFromCallbacks"),
    ConfigEntry ("Load the Steam Client DLL Early?",                          dll_ini,         "Steam.System",          "PreLoadSteamClient"),
    ConfigEntry ("Load the Steam Overlay Early",                              dll_ini,         "Steam.System",          "PreLoadSteamOverlay"),
    ConfigEntry ("Forcefully load steam_api{64}.dll",                         dll_ini,         "Steam.System",          "ForceLoadSteamAPI"),
    ConfigEntry ("Automatically load steam_api{64}.dll into any game whose "
                 "path includes SteamApps\\common, but doesn't use steam_api",dll_ini,         "Steam.System",          "AutoInjectSteamAPI"),
    ConfigEntry ("Always apply a social state (defined by EPersonaState) at"
                 " application start",                                        dll_ini,         "Steam.Social",          "OnlineStatus"),
    ConfigEntry ("Path to a known-working SteamAPI dll for this game.",       dll_ini,         "Steam.System",          "SteamPipeDL"),
    ConfigEntry ("-1=Unlimited, 0-oo=Upper bound limit to SteamAPI rate",     dll_ini,         "Steam.System",          "CallbackThrottle"),
    ConfigEntry ("Disable Steam Overlay using 'SteamNoOverlayUIDrawing'",     dll_ini,         "Steam.System",          "NoDrawOverlay"),
    ConfigEntry ("Special mode to workaround CAPCOM DRM without memory leaks",dll_ini,         "Steam.System",          "BypassCRAPCOM"),

    // This option is per-game, since it has potential compatibility issues...
    ConfigEntry ("Enhanced screenshot speed and HUD options; D3D11-only.",    dll_ini,         "Steam.Screenshots",     "EnableSmartCapture"),
    ConfigEntry ("Should a screenshot triggered BY Steam include SK's OSD?",  platform_ini,    "Steam.Screenshots",     "DefaultKeybindCapturesOSD"),

    ConfigEntry ("Start GalaxyCommunication.exe if Galaxy is not running.",   dll_ini,         "Galaxy.System",         "SpawnGalaxyCommunication"),
    ConfigEntry ("Enable if the current game is unsuitable for offline-only.",dll_ini,         "Galaxy.System",         "RequireOnlineGalaxyMode"),

    // These are all system-wide for all Steam games
    ConfigEntry ("Make the Steam Overlay visible in HDR mode!",               platform_ini,    "Platform.Overlay",      "Luminance_scRGB"),

    // Swashbucklers pay attention
    //////////////////////////////////////////////////////////////////////////

    ConfigEntry ("Makes steam_api.log go away [DISABLES STEAMAPI FEATURES]",  dll_ini,         "Steam.Log",             "Silent"),
    ConfigEntry ("CSV list of files to block from cloud sync.",               dll_ini,         "Steam.Cloud",           "FilesNotToSync"),
    ConfigEntry ("Fix For Stupid Games That Don't Know How DRM Works",        dll_ini,         "Steam.DRMWorks",        "SpoofBLoggedOn"),
  };

  std::vector<const ConfigEntry*> output;

  for (auto& param : params_to_build)
  {
    if (param.ini_ != ini)
      continue;

    output.push_back (&param);
  }

  return output;
}

#include <tabs/local.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/injection.h>
#include <utility/fsutil.h>
#include <ImGuiNotify.hpp>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

static constexpr wchar_t LocalRegPath[] = LR"(Software\Kaldaien\Special K\Local)";
enum class LocalAction {
  None,
  Update,
  Delete,
  AnySelected,
};

SKIF_LocalManager::SKIF_LocalManager (void)
  : watch (HKEY_CURRENT_USER, LocalRegPath, L"LocalManager")
{
  RefreshInstalls ();
}

void
SKIF_LocalManager::SetAnyOutdated (void)
{
  anyOutdated = false;
  for (auto& install : installs)
    anyOutdated |= install.outdated;
}

void
SKIF_LocalManager::SetAnySelected (void)
{
  anySelected         = false;
  anyOutdatedSelected = false;
  for (auto& install : installs)
  {
    anySelected           |= install.selected;
    if (install.selected)
      anyOutdatedSelected |= install.outdated;
  }
}

void
SKIF_LocalManager::SetLatestVersion (std::wstring_view v)
{
  latest_version = v;

  for (auto& install : installs)
    install.outdated = (! latest_version.empty ()) &&
      SKIF_Util_CompareVersionStrings (install.version, latest_version) < 0;

  SetAnyOutdated ( );
}

void
SKIF_LocalManager::UpdateInstall (install_s* install, PEBitness iBinaryType)
{
  if (install == nullptr)
    return;

  // Make sure the pointer actually refers to an element of our vector
  const install_s* first =         installs.data ();
  const install_s* last  = first + installs.size ();

  if (install < first || install >= last)
    return;

  if (iBinaryType == PEBitness_Invalid && !install->path.empty())
    iBinaryType = SKIF_Util_GetBinaryType (install->path.c_str());

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  if (iBinaryType > PEBitness_Invalid)
  {
    std::wstring folderPath = std::filesystem::path(install->path).parent_path().wstring();

    std::wstring  wsPathToGlobalDLL = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, (iBinaryType == PEBitness_64bit) ? L"SpecialK64.dll" : L"SpecialK32.dll");
    if (CopyFile (wsPathToGlobalDLL.c_str(), install->path.c_str(), FALSE))
    {
      PLOG_INFO << "Successfully updated " << install->path << " from v " << install->version << " to v " << _inject.SKVer32;

      HKEY    hKey = nullptr;
      LSTATUS res  = RegOpenKeyExW (HKEY_CURRENT_USER, LocalRegPath, 0,
                                    KEY_SET_VALUE, &hKey);

      if (res == ERROR_SUCCESS)
      {
        if (ERROR_SUCCESS != RegSetValueExW (hKey, install->path.c_str(), 0, REG_SZ, (LPBYTE)_inject.SKVer32.data(), (DWORD)_inject.SKVer32.length() * sizeof(wchar_t)))
          PLOG_ERROR << "Failed adding local install (" << _inject.SKVer32 << ") to registry value: " << install->path;

        RegCloseKey (hKey);
      }

      install->version      = _inject.SKVer32;
      install->version_utf8 = SK_WideCharToUTF8 (install->version);
    }

    else {
      PLOG_ERROR << "Failed to copy " << wsPathToGlobalDLL << " to " << install->path;
      PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    }
  }

  else {
    PLOG_ERROR << "Failed to retrieve binary type from " << install->path << " -- returned: " << iBinaryType;
    PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
  }
}

void
SKIF_LocalManager::DeleteInstall (install_s* install)
{
  if (install == nullptr)
    return;

  // Make sure the pointer actually refers to an element of our vector
  const install_s* first =         installs.data ();
  const install_s* last  = first + installs.size ();

  if (install < first || install >= last)
    return;

  SetLastError (NO_ERROR);

  BOOL  bDelete   = DeleteFile (install->path.c_str());
  if (! bDelete)
  {
    DWORD dwLastError  = GetLastError ( );
    if (  dwLastError != ERROR_FILE_NOT_FOUND)
    {
      PLOG_ERROR << "Deletion of the local DLL file failed with error: [" << dwLastError << "] " << SKIF_Util_GetErrorAsWStr (dwLastError);
      return;
    }
  }

  HKEY    hKey = nullptr;
  LSTATUS res  = RegOpenKeyExW (HKEY_CURRENT_USER, LocalRegPath, 0,
                                KEY_SET_VALUE, &hKey);

  if (res == ERROR_SUCCESS)
  {
    res = RegDeleteValueW (hKey, install->path.c_str ());
    RegCloseKey (hKey);
  }

  // Treat a registry key that has been deleted as a success
  if (res == ERROR_SUCCESS          ||
      res == ERROR_FILE_NOT_FOUND   ||
      res == ERROR_PATH_NOT_FOUND)
    install->deleted = true;
}

void
SKIF_LocalManager::RefreshInstalls (void)
{
  // Remember which installs were selected
  std::map<std::wstring, bool> wasSelected;
  for (auto& install : installs)
    wasSelected [install.path] = install.selected;

  want_install = nullptr;
  installs.clear ();

  HKEY hKey = nullptr;

  if (RegOpenKeyExW (HKEY_CURRENT_USER, LocalRegPath, 0,
                     KEY_READ, &hKey) != ERROR_SUCCESS)
    return; // Key doesn't exist yet, nothing installed

  DWORD dwValues     = 0,
        dwMaxNameLen = 0, // in characters, excl. null
        dwMaxDataLen = 0; // in bytes

  if (RegQueryInfoKeyW (hKey, nullptr, nullptr, nullptr, nullptr, nullptr,
                        nullptr, &dwValues, &dwMaxNameLen, &dwMaxDataLen,
                        nullptr, nullptr) == ERROR_SUCCESS)
  {
    std::vector<wchar_t> name (static_cast<size_t>(dwMaxNameLen) + 1);
    std::vector<BYTE>    data (static_cast<size_t>(dwMaxDataLen) + sizeof (wchar_t));

    for (DWORD i = 0; i < dwValues; ++i)
    {
      DWORD dwNameLen = static_cast<DWORD>(name.size ()),
            dwDataLen = static_cast<DWORD>(data.size ()),
            dwType    = 0;

      if (RegEnumValueW (hKey, i, name.data (), &dwNameLen, nullptr,
                         &dwType, data.data (), &dwDataLen) != ERROR_SUCCESS)
        continue;

      if (dwType != REG_SZ)
        continue;

      install_s install;
      install.path      = std::wstring (name.data (), dwNameLen);
      install.path_utf8 = SK_WideCharToUTF8 (install.path);

      // Data is in bytes and may or may not include a null terminator
      size_t chars = dwDataLen / sizeof (wchar_t);
      auto*  pData = reinterpret_cast<wchar_t*>(data.data ());
      while (chars > 0 && pData [chars - 1] == L'\0')
        --chars;
      install.version      = std::wstring (pData, chars);
      install.version_utf8 = SK_WideCharToUTF8 (install.version);
      install.outdated     = (! latest_version.empty ()) &&
        SKIF_Util_CompareVersionStrings (install.version, latest_version) < 0;

      auto it = wasSelected.find (install.path);
      install.selected = install.outdated &&
                  (it != wasSelected.end ()) && it->second;
      install.exists        = GetFileAttributesW (install.path.c_str()) != INVALID_FILE_ATTRIBUTES;
      install.exists_parent = GetFileAttributesW (std::filesystem::path(install.path).parent_path().wstring().c_str()) != INVALID_FILE_ATTRIBUTES;

      installs.push_back (std::move (install));
    }
  }

  RegCloseKey (hKey);

  SetAnyOutdated ( );
  needs_sort = true;
}

void
SKIF_LocalManager::Show (void)
{
  static int id = 0;

  if (this->showWindow && this->hwnd)
  {
    if (       IsIconic (       this->hwnd))
             ShowWindow (       this->hwnd, SW_RESTORE);
    else if (! IsWindowVisible (this->hwnd))
             ShowWindow (       this->hwnd, SW_SHOW);

    SetForegroundWindow (       this->hwnd);
  } else {
    this->label      = "Local Installs###LocalManager-" + std::to_string (id++);
    this->hwnd       = nullptr;
    this->showWindow = true;
  }
}

// 0 = exists + outdated, 1 = does not exist, 2 = exists + up to date
static int
EvalStatus (const SKIF_LocalManager::install_s& i)
{
  if (! i.exists_parent) return 2;
  if (! i.exists)        return 1;
  if (  i.outdated)      return 0;
  return 3;
}

void
SKIF_LocalManager::SortInstalls (const ImGuiTableSortSpecs* specs)
{
  std::stable_sort (installs.begin (), installs.end (),
  [specs](const install_s& a, const install_s& b)
  {
    for (int n = 0; n < specs->SpecsCount; ++n)
    {
      const ImGuiTableColumnSortSpecs& s = specs->Specs [n];

      int delta = 0;

      switch (s.ColumnUserID)
      {
        case Col_Path:
          delta = _wcsicmp (a.path.c_str (), b.path.c_str ());
          break;
        case Col_Version:
          delta = SKIF_Util_CompareVersionStrings (a.version, b.version);
          break;
        case Col_Updater:
          delta = static_cast<int>(a.outdated) - static_cast<int>(b.outdated);
          break;
        case Col_Status:
          delta = EvalStatus (a) - EvalStatus (b);
          break;
        default:
          break;
      }

      if (delta != 0)
        return (s.SortDirection == ImGuiSortDirection_Ascending) ? (delta < 0)
                                                                 : (delta > 0);
    }

    return false;
  });
}

void
SKIF_LocalManager::Process (void)
{
  // Refresh if the registry key changed since last frame
  if (watch.isSignaled ())
    RefreshInstalls ();

  if (! showWindow)
    return;

  // Custom TableHeadersRow() that draws a checkbox toggle in one of the columns
  auto _CustomTableHeadersRow = [&](void) -> void
  {
    ImGui::TableNextRow (ImGuiTableRowFlags_Headers);

    for (int column_n = 0; column_n < ImGui::TableGetColumnCount (); ++column_n)
    {
      if (! ImGui::TableSetColumnIndex (column_n))
          continue;

      const char* name = (ImGui::TableGetColumnFlags (column_n) & ImGuiTableColumnFlags_NoHeaderLabel) ? "" : ImGui::TableGetColumnName (column_n);
      ImGui::PushID (column_n);

      if (column_n == SKIF_LocalManager::ColumnID_::Col_Select)
      {
        // Only outdated installs should be selected
        int outdated = 0,
            selected = 0;

        for (auto& i : installs)
        {
          if (! i.outdated || ! i.exists) continue;
          ++outdated;
          if (  i.selected) ++selected;
        }

        bool all = (outdated > 0 && selected == outdated);

        ImGui::BeginDisabled  (outdated == 0);
        ImGui::PushStyleVar   (ImGuiStyleVar_FramePadding, ImVec2 (0.0f, 0.0f)); // Shrinks the checkbox
        ImGui::PushStyleColor (ImGuiCol_FrameBg,           IM_COL32_DISABLE);
        ImGui::PushStyleColor (ImGuiCol_FrameBgActive,     IM_COL32_DISABLE);
        ImGui::PushStyleColor (ImGuiCol_FrameBgHovered,    IM_COL32_DISABLE);

        // Center it, same as the row checkboxes
        float offset = (ImGui::GetContentRegionAvail ().x - ImGui::GetFrameHeight ()) * 0.5f;
        if (offset > 0.0f)
          ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);

        // 'all' holds the new state after a click: check if not all were
        // selected, uncheck if they all were
        if (SKIF_ImGui_CheckboxHoverPreview ("##selectall", &all))
        {
          for (auto& i : installs)
            if (i.outdated && i.exists)
              i.selected = all;
          SetAnySelected ( );
        }

        ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgHovered
        ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgActive
        ImGui::PopStyleColor ( ); // ImGuiCol_FrameBg
        ImGui::PopStyleVar   ( ); // ImGuiStyleVar_FramePadding
        ImGui::EndDisabled   ( );
      }

      else
        ImGui::TableHeader (name);

      ImGui::PopID ();
    }
  };

  static           SKIF_InjectionContext& _inject = SKIF_InjectionContext::GetInstance ( );
  static constexpr ImGuiTableColumnFlags  _colFlagsPath    =
    ImGuiTableColumnFlags_WidthStretch;
  static constexpr ImGuiTableColumnFlags  _colFlagsVersion =
    ImGuiTableColumnFlags_WidthFixed    |
    ImGuiTableColumnFlags_NoResize;
  static constexpr ImGuiTableColumnFlags  _colFlagsStatus  =
    ImGuiTableColumnFlags_WidthFixed    |
    ImGuiTableColumnFlags_NoResize      |
    ImGuiTableColumnFlags_DefaultSort;
  static constexpr ImGuiTableColumnFlags  _colFlagsMisc    =
    ImGuiTableColumnFlags_WidthFixed    |
    ImGuiTableColumnFlags_NoResize      |
    ImGuiTableColumnFlags_NoHeaderLabel |
    ImGuiTableColumnFlags_NoSort;

  if (_inject.SKVer32 != this->latest_version)
    SetLatestVersion (_inject.SKVer32);

  ImGuiWindowClass
    wc;
    wc.ClassId = ImGui::GetID ("LocalManagerClass");
    wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge; // Work around a pesky bug where the viewport tries to merge into the... debug fallback window?
  ImGui::SetNextWindowClass (&wc);
  ImGui::SetNextWindowSizeConstraints (ImVec2 (800.0f, 300.0f) * SKIF_ImGui_GlobalDPIScale, ImVec2 (FLT_MAX, FLT_MAX));
  extern ImRect windowRect;
  ImGui::SetNextWindowPos (windowRect.GetCenter(), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));

  if (ImGui::Begin (this->label.c_str(), &showWindow,
                      ImGuiWindowFlags_NoCollapse        |
                      ImGuiWindowFlags_NoScrollbar       |
                      ImGuiWindowFlags_NoSavedSettings   ))
  {
    static LocalAction action =
           LocalAction::None;

    if (this->hwnd == nullptr && ImGui::GetWindowViewport()->PlatformHandleRaw)
      this->hwnd = (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw;

    if (installs.empty ())
    {
      ImGui::TextDisabled     ("No local installs found.");

      SKIF_ImGui_Spacing      ( );
      SKIF_ImGui_Spacing      ( );

      ImGui::TextDisabled     ("To perform a local install of Special K:");

      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::TextColored      (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                            (const char *)u8"\u2022 ");
      ImGui::SameLine         ( );
      ImGui::TextDisabled     ("While in-game, use");
      ImGui::SameLine         ( );
      ImGui::Text             (ICON_FA_FLOPPY_DISK " File -> Install Wrapper DLL.");
      ImGui::EndGroup         ( );

      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::TextColored      (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                            (const char *)u8"\u2022 ");
      ImGui::SameLine         ( );
      ImGui::TextDisabled     ("During launch, hold down");
      ImGui::SameLine         ( );
      ImGui::Text             ("Ctrl + Shift");
      ImGui::SameLine         ( );
      ImGui::TextDisabled     ("to access the compatibility options.");
      ImGui::EndGroup         ( );
    }

    else if (ImGui::BeginTable ("##LocalInstalls", 6,
                                ImGuiTableFlags_RowBg         |
                                ImGuiTableFlags_ScrollY       |
                                ImGuiTableFlags_Sortable))
    {
      ImGui::TableSetupColumn ("Select",    _colFlagsMisc,     30.0f * SKIF_ImGui_GlobalDPIScale, Col_Select);
      ImGui::TableSetupColumn ("Path",      _colFlagsPath,      0.0f,                             Col_Path);
      ImGui::TableSetupColumn ("Version",   _colFlagsVersion, 100.0f * SKIF_ImGui_GlobalDPIScale, Col_Version);
      ImGui::TableSetupColumn ("Updater",   _colFlagsMisc,     20.0f * SKIF_ImGui_GlobalDPIScale, Col_Updater);
      ImGui::TableSetupColumn ("Status",    _colFlagsStatus,   20.0f * SKIF_ImGui_GlobalDPIScale, Col_Status);
      ImGui::TableSetupColumn ("Folder",    _colFlagsMisc,     40.0f * SKIF_ImGui_GlobalDPIScale, Col_Folder);
      
      ImGui::PushStyleColor   (ImGuiCol_TableHeaderBg, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
      ImGui::PushStyleColor   (ImGuiCol_HeaderHovered, IM_COL32_DISABLE);
      ImGui::PushStyleColor   (ImGuiCol_HeaderActive,  IM_COL32_DISABLE);
      ImGui::PushStyleColor   (ImGuiCol_Text,          ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
    //ImGui::TableHeadersRow  ( );
      _CustomTableHeadersRow  ( );
      ImGui::PopStyleColor    ( ); // ImGuiCol_Text
      ImGui::PopStyleColor    ( ); // ImGuiCol_HeaderActive
      ImGui::PopStyleColor    ( ); // ImGuiCol_HeaderHovered
      ImGui::PopStyleColor    ( ); // ImGuiCol_TableHeaderBg

      if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs ())
      {
        if (specs->SpecsDirty || needs_sort)
        {
          if (specs->SpecsCount > 0)
            SortInstalls (specs);

          specs->SpecsDirty = false;
          needs_sort        = false;
        }
      }

      ImGui::PushStyleColor   (ImGuiCol_Text,          ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
      int id = 0;
      for (auto& install : installs)
      {
        ImGui::PushID (id++);
        ImGui::TableNextRow ();

        // Column 0: Select
        if (ImGui::TableNextColumn ( ))
        {
          float offset = (ImGui::GetContentRegionAvail ().x - ImGui::GetFrameHeight ()) * 0.5f;
          if (offset > 0.0f)
            ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);

          ImGui::PushStyleColor (ImGuiCol_FrameBg,        IM_COL32_DISABLE);
          ImGui::PushStyleColor (ImGuiCol_FrameBgActive,  IM_COL32_DISABLE);
          ImGui::PushStyleColor (ImGuiCol_FrameBgHovered, IM_COL32_DISABLE);

          if (SKIF_ImGui_CheckboxHoverPreview ("##checkbox", &install.selected))
            SetAnySelected ( );

          ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgHovered
          ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgActive
          ImGui::PopStyleColor ( ); // ImGuiCol_FrameBg
        }

        // Column 1: Path
        if (ImGui::TableNextColumn ( ))
        {
          ImGui::TextUnformatted (install.path_utf8.c_str ());
        }

        // Column 2: Version
        if (ImGui::TableNextColumn ( ))
        {
          ImGui::TextUnformatted (install.version_utf8.c_str ());
        }

        // Column 3: Updater
        if (ImGui::TableNextColumn ( ) && install.exists_parent && install.outdated)
        {
          if (install.exists)
          {
            ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
            if (ImGui::SmallButton (ICON_FA_ARROW_UP))
              UpdateInstall (&install);
            ImGui::PopStyleColor ( );

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("The local DLL file is outdated.\n"
                                 "Click to update it to v %s.",
                                 _inject.SKVer32_utf8.c_str());
          }

          // Reinstall
          else
          {
            ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
            if (ImGui::SmallButton (ICON_FA_ARROW_DOWN))
              want_install = &install;
            ImGui::PopStyleColor ( );

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("The local DLL file could not be found.\n"
                                 "Click to reinstall it as v %s.",
                                 _inject.SKVer32_utf8.c_str());
          }
        }

        // Column 4: Status (icon)
        if (ImGui::TableNextColumn ( ))
        {
          if (! install.exists_parent)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure),
                                ICON_FA_CIRCLE_XMARK);

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("Folder no longer exists");
          }

          else if (! install.exists)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure),
                                ICON_FA_CIRCLE_XMARK);

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("Local DLL file no longer exists");
          }

          else if (install.outdated)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow),
                                ICON_FA_TRIANGLE_EXCLAMATION);

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("Outdated (latest: %s)",
                                 _inject.SKVer32_utf8.c_str ());
          }

          else
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success),
                                ICON_FA_CIRCLE_CHECK);
          }
        }

        // Column 5: Folder
        if (ImGui::TableNextColumn ( ) && install.exists_parent)
        {
          if (ImGui::Button (ICON_FA_FOLDER_OPEN))
            if (install.exists)
              SKIF_Util_FileExplorer_SelectFile (install.path.c_str());
            else
              SKIF_Util_ExplorePath (std::filesystem::path(install.path).parent_path().wstring().c_str());
        }

        ImGui::PopID ();
      }
      ImGui::PopStyleColor    ( ); // ImGuiCol_Text

      // Engages auto-scroll mode (left click drag on touch + middle click drag on non-touch)
      SKIF_ImGui_AutoScroll  (false, SKIF_ImGuiAxis_Y);

      ImGui::EndTable ();
    }

    // Right-click anywhere in the window, including inside the scrolling table
    if (ImGui::IsWindowHovered (ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseReleased (ImGuiMouseButton_Right))
    {
      ImGui::OpenPopup ("##LocalInstallsCtx");
    }

    if (ImGui::BeginPopup ("##LocalInstallsCtx"))
    {
      SKIF_ImGui_PushDisableState (! anyOutdated);
      if (ImGui::MenuItem ("Select Outdated"))
      {
        for (auto& install : installs)
          install.selected = install.outdated && install.exists;
        action = LocalAction::AnySelected;
      }
      SKIF_ImGui_PopDisableState  (! anyOutdated);
      
      SKIF_ImGui_PushDisableState (! anySelected);
      if (ImGui::MenuItem ("Deselect All"))
      {
        for (auto& install : installs)
          install.selected = false;
        action = LocalAction::AnySelected;
      }
      SKIF_ImGui_PopDisableState  (! anySelected);

      ImGui::Separator ();

      SKIF_ImGui_PushDisableState (! anyOutdatedSelected);
      if (ImGui::MenuItem ("Update Selected"))
        action = LocalAction::Update;
      SKIF_ImGui_PopDisableState  (! anyOutdatedSelected);

      SKIF_ImGui_PushDisableState (! anySelected);
      if (ImGui::MenuItem ("Delete Selected"))
        action = LocalAction::Delete;
      SKIF_ImGui_PopDisableState  (! anySelected);

      ImGui::Separator ();

      if (ImGui::MenuItem ("Refresh"))
        RefreshInstalls ();

      ImGui::EndPopup ();
    }

    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      ShowWindowAsync ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, SW_MINIMIZE);

    // Process notifications for this window
    ImGui::RenderNotifications ();

    if (want_install)
    {
      bool dontCare = true;
      switch (SKIF_ImGui_6432bitnessPrompt ("Select the bitness to install", &dontCare, nullptr, "Or while ingame, use " ICON_FA_FLOPPY_DISK " File -> Install Wrapper DLL."))
      {
      case DChoice_32bit:
        UpdateInstall (want_install, PEBitness_32bit);
        want_install = nullptr;
        break;
      case DChoice_64bit:
        UpdateInstall (want_install, PEBitness_64bit);
        want_install = nullptr;
        break;
      case DChoice_Cancel:
        want_install = nullptr;
        break;
      }
    }

    else if (action == LocalAction::AnySelected)
    {
      SetAnySelected ( );
      action = LocalAction::None;
    }

    else if (action != LocalAction::None)
    {
      auto _PerformAction = [&]() -> void
      {
        switch (action)
        {
        case LocalAction::Update:
          for (auto& install : installs)
            if (install.selected && install.outdated)
              UpdateInstall (&install);
          break;

        case LocalAction::Delete:
          for (auto& install : installs)
            if (install.selected)
              DeleteInstall (&install);
          installs.erase (std::remove_if (installs.begin(), installs.end(), [](const install_s& i) { return i.deleted; }), installs.end());
          break;
        }
        action = LocalAction::None;
      };

      static constexpr char* titUpdate     = "Update Installs?";
      static constexpr char* titDelete     = "Delete Installs?";
      static constexpr char* msgUpdate     = "Are you sure you want to update all selected installs?";
      static constexpr char* msgDelete     = "Are you sure you want to delete all selected installs?";
      static constexpr char* msgDeleteDesc = "Some leftover files may still be present in the game folder.";

      char* name = (action == LocalAction::Update ? titUpdate     :
                    action == LocalAction::Delete ? titDelete     :
                    nullptr);
      char* text = (action == LocalAction::Update ? msgUpdate     :
                    action == LocalAction::Delete ? msgDelete     :
                    nullptr);
      char* desc = (action == LocalAction::Update ? nullptr       :
                    action == LocalAction::Delete ? msgDeleteDesc :
                    nullptr);

      bool dontCare = true;
      switch (SKIF_ImGui_ConfirmPrompt (name, &dontCare, text, desc))
      {
      case DChoice_Yes:
        _PerformAction ( );
        break;
      case DChoice_No:
      case DChoice_Cancel:
        action = LocalAction::None;
        break;
      }
    }
  }

  ImGui::End ();
}

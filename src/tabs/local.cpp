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
  anySelected  = false;
  for (auto& install : installs)
    anySelected |= install.selected;
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
SKIF_LocalManager::UpdateInstall (install_s* install)
{
  if (install == nullptr)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  int iBinaryType = SKIF_Util_GetBinaryType (install->path.c_str());
  if (iBinaryType > 0)
  {
    std::wstring folderPath = std::filesystem::path(install->path).parent_path().wstring();

    std::wstring  wsPathToGlobalDLL = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, (iBinaryType == 2) ? L"SpecialK64.dll" : L"SpecialK32.dll");
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
      install.exists   = GetFileAttributesW (install.path.c_str()) != INVALID_FILE_ATTRIBUTES;

      installs.push_back (std::move (install));
    }
  }

  RegCloseKey (hKey);

  SetAnyOutdated ( );
}

void
SKIF_LocalManager::ShowWindow (void)
{
  static int id = 0;

  this->label = "Local Installs###LocalManager-" + std::to_string(id++);
  this->showWindow = true;
}

void
SKIF_LocalManager::Process (void)
{
  // Refresh if the registry key changed since last frame
  if (watch.isSignaled ())
    RefreshInstalls ();

  if (! showWindow)
    return;

  static SKIF_InjectionContext& _inject = SKIF_InjectionContext::GetInstance ( );

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

    if (installs.empty ())
    {
      ImGui::TextDisabled ("No local installs found.");
    }

    else if (ImGui::BeginTable ("##LocalInstalls", 6,
                                ImGuiTableFlags_Borders     |
                                ImGuiTableFlags_RowBg       |
                              //ImGuiTableFlags_Resizable   |
                                ImGuiTableFlags_ScrollY))
    {
      ImGui::TableSetupColumn ("Select",  ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_NoResize,    50.0f * SKIF_ImGui_GlobalDPIScale);
      ImGui::TableSetupColumn ("Path",    ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("Version", ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_NoResize,   100.0f * SKIF_ImGui_GlobalDPIScale);
      ImGui::TableSetupColumn ("##Status",ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_NoResize,    20.0f * SKIF_ImGui_GlobalDPIScale);
      ImGui::TableSetupColumn ("##Update",ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_NoResize,    20.0f * SKIF_ImGui_GlobalDPIScale);
      ImGui::TableSetupColumn ("##Folder",ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_NoResize,    20.0f * SKIF_ImGui_GlobalDPIScale);
      ImGui::TableHeadersRow  ();

      int id = 0;
      for (auto& install : installs)
      {
        ImGui::PushID (id++);
        ImGui::TableNextRow ();

        // Column 0: Checkbox
        if (ImGui::TableNextColumn ( ))
        {
          float offset = (ImGui::GetContentRegionAvail ().x - ImGui::GetFrameHeight ()) * 0.5f;
          if (offset > 0.0f)
            ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);

          if (ImGui::Checkbox ("##select", &install.selected))
            SetAnySelected ( );
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
        if (ImGui::TableNextColumn ( ))
        {
          if (install.outdated)
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
        }

        // Column 4: Icon
        if (ImGui::TableNextColumn ( ))
        {
          if (install.outdated)
          {
            ImGui::TextColored (ImVec4 (1.0f, 0.75f, 0.0f, 1.0f),
                                ICON_FA_TRIANGLE_EXCLAMATION);

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("Outdated (latest: %s)",
                                 _inject.SKVer32_utf8.c_str ());
          } else {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success),
                                ICON_FA_CIRCLE_CHECK);
          }
        }

        // Column 5: Folder
        if (ImGui::TableNextColumn ( ))
        {
          ImGui::BeginDisabled (! install.exists);
          if (ImGui::Button (ICON_FA_FOLDER_OPEN))
            SKIF_Util_FileExplorer_SelectFile (install.path.c_str());
          ImGui::EndDisabled ();

          if (! install.exists && ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip ("Path no longer exists");
        }

        ImGui::PopID ();
      }

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
          install.selected = install.outdated;
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

      SKIF_ImGui_PushDisableState (! anySelected);
      if (ImGui::MenuItem ("Update"))
        action = LocalAction::Update;
      SKIF_ImGui_PopDisableState  (! anySelected);

      SKIF_ImGui_PushDisableState (! anySelected);
      if (ImGui::MenuItem ("Delete"))
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

    if (action == LocalAction::AnySelected)
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
            if (install.outdated && install.selected)
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

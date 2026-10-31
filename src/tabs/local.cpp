#include <tabs/local.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/hotkeys.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/injection.h>
#include <utility/fsutil.h>
#include <ImGuiNotify.hpp>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

static constexpr wchar_t LocalRegPath[] = LR"(Software\Kaldaien\Special K\Local)";

// DLL filenames Special K can be installed as, in the order of most expected (same list as UpdateInjectionStrategy)
static const struct {
  const wchar_t* name;
  const char*    name_utf8;
  const char*    hint;      // English, translated where shown
} LocalDLLs [] = {
  { L"DXGI.dll",     "DXGI.dll",     N_("Direct3D 10/11/12 games (most common).")              },
  { L"D3D11.dll",    "D3D11.dll",    N_("Direct3D 11 games where DXGI.dll does not work.")     },
  { L"D3D9.dll",     "D3D9.dll",     N_("Direct3D 9 games.")                                   },
  { L"OpenGL32.dll", "OpenGL32.dll", N_("OpenGL games.")                                       },
  { L"DInput8.dll",  "DInput8.dll",  N_("Games that load DirectInput 8; can work as a fallback.")},
  { L"D3D8.dll",     "D3D8.dll",     N_("Direct3D 8 games.")                                   },
  { L"DDraw.dll",    "DDraw.dll",    N_("DirectDraw games.")                                   },
};
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
SKIF_LocalManager::UpdateAnyStates (void)
{
  anyMissing          = false;
  anySelected         = false;
  anyOutdated         = false;
  anyOutdatedSelected = false;

  for (auto& install : installs)
  {
    anyMissing            |= ! install.exists;
    anySelected           |= install.selected;
    anyOutdated           |= install.outdated;
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

  UpdateAnyStates ( );
}

bool
SKIF_LocalManager::UpdateInstall (install_s* install, PEBitness iBinaryType)
{
  if (install == nullptr)
    return false;

  // Make sure the pointer actually refers to an element of our vector
  const install_s* first =         installs.data ();
  const install_s* last  = first + installs.size ();

  if (install < first || install >= last)
    return false;

  if (iBinaryType == PEBitness_Invalid && !install->path.empty())
    iBinaryType = SKIF_Util_GetBinaryType (install->path.c_str());

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  if (iBinaryType <= PEBitness_Invalid)
  {
    PLOG_ERROR << "Failed to retrieve binary type from " << install->path << " -- returned: " << iBinaryType;
    PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    return false;
  }

  std::wstring  wsPathToGlobalDLL = SK_FormatStringW (LR"(%ws\%ws)", _path_cache.specialk_install, (iBinaryType == PEBitness_64bit) ? L"SpecialK64.dll" : L"SpecialK32.dll");
  if (! CopyFile (wsPathToGlobalDLL.c_str(), install->path.c_str(), FALSE))
  {
    PLOG_ERROR << "Failed to copy " << wsPathToGlobalDLL << " to " << install->path;
    PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    return false;
  }

  PLOG_INFO << "Successfully updated " << install->path << " from v " << install->version << " to v " << _inject.SKVer32;

  // Create the key as well, in case this is the very first local install
  HKEY    hKey = nullptr;
  LSTATUS res  = RegCreateKeyExW (HKEY_CURRENT_USER, LocalRegPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_SET_VALUE, nullptr, &hKey, nullptr);

  if (res == ERROR_SUCCESS)
  {
    if (ERROR_SUCCESS != RegSetValueExW (hKey, install->path.c_str(), 0, REG_SZ, (LPBYTE)_inject.SKVer32.data(), (DWORD)_inject.SKVer32.length() * sizeof(wchar_t)))
      PLOG_ERROR << "Failed adding local install (" << _inject.SKVer32 << ") to registry value: " << install->path;

    RegCloseKey (hKey);
  }

  else
    PLOG_ERROR << "Failed to open the local installs registry key: " << SKIF_Util_GetErrorAsWStr (res);

  install->version      = _inject.SKVer32;
  install->version_utf8 = SK_WideCharToUTF8 (install->version);

  return true;
}

// File -> New: let the user pick a game executable. The DLL filename prompt in Process() takes it from there.
void
SKIF_LocalManager::BrowseNewInstall (void)
{
  LPWSTR  pwszFilePath = nullptr;
  HRESULT hr           =
    SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, hwnd, { { L"Executables", L"*.exe" } },
                                          FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);

  if (SUCCEEDED (hr) && pwszFilePath != nullptr)
  {
    new_exe_path      = pwszFilePath;
    new_exe_path_utf8 = SK_WideCharToUTF8 (new_exe_path);
    new_bitness       = SKIF_Util_GetBinaryType (new_exe_path.c_str ());

    // Check each candidate once up front, rather than every frame while the prompt is shown
    const std::filesystem::path folder = std::filesystem::path (new_exe_path).parent_path ();

    new_dll_states.clear ();

    bool         anyInstalled = false,
                 allUpToDate  = true;
    std::wstring upToDatePath,
                 upToDateVer;

    for (const auto& dll : LocalDLLs)
    {
      const std::wstring target = (folder / dll.name).wstring ();
      int                state  = 0;

      if (GetFileAttributesW (target.c_str ()) != INVALID_FILE_ATTRIBUTES)
      {
        const std::wstring ver = SKIF_Util_GetSpecialKDLLVersion (target.c_str ());

        if (ver.empty ())
          state = 2;

        else
        {
          state        = 1;
          anyInstalled = true;

          // Unknown latest version counts as "might be outdated", so the user can still update
          if (latest_version.empty () || SKIF_Util_CompareVersionStrings (ver, latest_version) < 0)
            allUpToDate  = false;
          else
          {
            upToDatePath = target;
            upToDateVer  = ver;
          }
        }
      }

      new_dll_states.push_back (state);
    }

    if (anyInstalled)
    {
      // Special K is already in this folder: only allow updating that copy
      for (auto& state : new_dll_states)
        if (state == 0)
          state = 3;

      // ... and if it is already current, there is nothing to pick at all
      if (allUpToDate)
      {
        new_uptodate_desc = SK_WideCharToUTF8 (upToDatePath) + " (v " +
                            SK_WideCharToUTF8 (upToDateVer)  + ")";
        new_exe_path.clear      ( );
        new_exe_path_utf8.clear ( );
        new_dll_states.clear    ( );
      }
    }
  }

  if (pwszFilePath != nullptr)
    CoTaskMemFree (pwszFilePath);
}

// Installs (or updates) Special K at dll_path and adds it to the list
void
SKIF_LocalManager::InstallNew (const std::wstring& dll_path, PEBitness iBinaryType)
{
  int idx = -1;

  for (size_t i = 0; i < installs.size (); ++i)
    if (_wcsicmp (installs [i].path.c_str (), dll_path.c_str ()) == 0)
      idx = static_cast<int> (i);

  const bool added = (idx == -1);

  if (added)
  {
    // push_back() may reallocate, so no install_s pointer may be held across this
    want_install = nullptr;

    install_s install;
    install.path          = dll_path;
    install.path_utf8     = SK_WideCharToUTF8 (dll_path);
    install.exists_parent = true;

    installs.push_back (std::move (install));
    idx = static_cast<int> (installs.size ()) - 1;
  }

  install_s& install = installs [idx];

  if (UpdateInstall (&install, iBinaryType))
  {
    install.exists        = true;
    install.exists_parent = true;
    install.outdated      = (! latest_version.empty ()) &&
      SKIF_Util_CompareVersionStrings (install.version, latest_version) < 0;
    needs_sort            = true;

    ImGui::InsertNotification ({
      ImGuiToastType::Success, 5000,
      _("Special K Installed"),
      "%s", install.path_utf8.c_str ()
    });
  }

  else
  {
    ImGui::InsertNotification ({
      ImGuiToastType::Error, 5000,
      _("Install Failed"),
      "%s", F_("Could not install Special K as {0}", install.path_utf8).c_str ()
    });

    if (added)
      installs.erase (installs.begin () + idx);
  }

  UpdateAnyStates ( );
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
    if (  dwLastError != ERROR_FILE_NOT_FOUND &&
          dwLastError != ERROR_PATH_NOT_FOUND)
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

  UpdateAnyStates ( );
  needs_sort = true;
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

int
SKIF_LocalManager::FindInstall (const std::wstring& path) const
{
  if (path.empty ())
    return -1;

  for (size_t i = 0; i < installs.size (); ++i)
    if (installs [i].path == path)
      return static_cast<int> (i);

  return -1;
}

void
SKIF_LocalManager::HandleRowClick (int idx, bool fromCheckbox)
{
  if (idx < 0 || idx >= static_cast<int> (installs.size ()))
    return;

  const ImGuiIO& io     = ImGui::GetIO ();
  const int      anchor = FindInstall (anchor_path);

  // Shift+Click: select everything between the anchor and the clicked row
  if (io.KeyShift && anchor != -1)
  {
    // Plain Shift replaces the selection; Ctrl+Shift or Shift on a checkbox adds to it
    if (! io.KeyCtrl && ! fromCheckbox)
      for (auto& install : installs)
        install.selected = false;

    const int lo = std::min (anchor, idx),
              hi = std::max (anchor, idx);

    for (int i = lo; i <= hi; ++i)
      installs [i].selected = true;

    // Anchor intentionally stays put so the range can be grown/shrunk with further Shift+Clicks
  }

  // Ctrl+Click, or a plain checkbox click: toggle
  else if (io.KeyCtrl || fromCheckbox)
  {
    if (! fromCheckbox) // Checkbox() already flipped the bool
      installs [idx].selected = ! installs [idx].selected;

    anchor_path = installs [idx].path;
  }

  // Plain click on the row: select only this one
  else
  {
    for (auto& install : installs)
      install.selected = false;

    installs [idx].selected = true;
    anchor_path             = installs [idx].path;
  }

  UpdateAnyStates ( );
}

// Select-all cycle shared by the header checkbox and Ctrl+A:
//   1. Select all outdated (existing) rows
//   2. If all outdated are selected (or there are none) -> select all rows
//   3. If all rows are selected -> deselect all (only when allowDeselect)
void
SKIF_LocalManager::CycleSelectAll (bool allowDeselect)
{
  if (installs.empty ())
    return;

  int outdated         = 0,
      outdatedSelected = 0,
      selected         = 0;

  for (const auto& i : installs)
  {
    if (i.selected)
      ++selected;

    if (i.outdated && i.exists)
    {
      ++outdated;
      if (i.selected)
        ++outdatedSelected;
    }
  }

  const bool allRowsSelected     = (selected == static_cast<int> (installs.size ()));
  const bool allOutdatedSelected = (outdated > 0 && outdatedSelected == outdated);

  if (allRowsSelected)
  {
    if (! allowDeselect)
      return; // Ctrl+A: stay on "all rows"

    for (auto& i : installs)
      i.selected = false;

    anchor_path.clear ();
  }

  else if (allOutdatedSelected || outdated == 0)
  {
    for (auto& i : installs)
      i.selected = true;
  }

  else
  {
    for (auto& i : installs)
      i.selected = (i.outdated && i.exists);
  }

  UpdateAnyStates ( );
}

void
SKIF_LocalManager::HandleRowRightClick (int idx)
{
  if (idx < 0 || idx >= static_cast<int> (installs.size ()))
    return;

  int selected = 0;
  for (const auto& install : installs)
    if (install.selected)
      ++selected;

  // With zero or one row selected, behave like a plain left click (replace).
  // With a multi-selection, keep it and just make sure this row is part of it.
  if (selected <= 1)
    for (auto& install : installs)
      install.selected = false;

  installs [idx].selected = true;
  anchor_path             = installs [idx].path;

  UpdateAnyStates ( );
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
    this->label      = NL_("Local Installs###LocalManager-") + std::to_string (id++);
    this->hwnd       = nullptr;
    this->showWindow = true;

    for (auto& i : installs)
      i.selected = false;
  }
}

void
SKIF_LocalManager::Process (void)
{
  // Refresh if the registry key changed since last frame
  if (watch.isSignaled ())
    RefreshInstalls ();

  // Set again below once the window is drawn
  focused = false;

  if (! this->showWindow)
    return;

  static LocalAction action =
          LocalAction::None;

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
        // Three-state cycle:
        //   1. Select all outdated (existing) rows
        //   2. If all outdated are selected  -> select all rows
        //   3. If all rows are selected      -> deselect all rows
        int outdated         = 0,
            outdatedSelected = 0,
            selected         = 0;

        for (auto& i : installs)
        {
          if (i.selected)
            ++selected;

          if (i.outdated && i.exists)
          {
            ++outdated;
            if (i.selected)
              ++outdatedSelected;
          }
        }

        const bool allRowsSelected     = (! installs.empty () && selected == static_cast<int> (installs.size ()));
        const bool allOutdatedSelected = (outdated > 0 && outdatedSelected == outdated);

        // Checked = all rows, mixed = all outdated but not all rows, unchecked = anything else
        const SKIF_ImGuiCheckState state =
          allRowsSelected                        ? SKIF_ImGuiCheckState_Checked :
          allOutdatedSelected                    ? SKIF_ImGuiCheckState_Mixed   :
                                                   SKIF_ImGuiCheckState_Unchecked;

        // What the next click leads to (mirrors CycleSelectAll)
        const SKIF_ImGuiCheckState next  =
          allRowsSelected                        ? SKIF_ImGuiCheckState_Unchecked :
          (allOutdatedSelected || outdated == 0) ? SKIF_ImGuiCheckState_Checked   :
                                                   SKIF_ImGuiCheckState_Mixed;

        SKIF_ImGui_BeginDisabled (installs.empty ());
        ImGui::PushStyleVar   (ImGuiStyleVar_FramePadding, ImVec2 (0.0f, 0.0f)); // Shrinks the checkbox
        ImGui::PushStyleColor (ImGuiCol_FrameBg,           IM_COL32_DISABLE);
        ImGui::PushStyleColor (ImGuiCol_FrameBgHovered,    IM_COL32_DISABLE);
        ImGui::PushStyleColor (ImGuiCol_FrameBgActive,     IM_COL32_DISABLE);

        // Center it, same as the row checkboxes
        float offset = (ImGui::GetContentRegionAvail ().x - ImGui::GetFrameHeight ()) * 0.5f;
        if (offset > 0.0f)
          ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);

        if (SKIF_ImGui_CheckboxTriState ("##selectall", state, next))
          CycleSelectAll (true);

        if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("%s", allRowsSelected                        ? _("Deselect all")    :
                                   (allOutdatedSelected || outdated == 0) ? _("Select all")      :
                                                                            _("Select outdated"));

        ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgActive
        ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgHovered
        ImGui::PopStyleColor ( ); // ImGuiCol_FrameBg
        ImGui::PopStyleVar   ( ); // ImGuiStyleVar_FramePadding
        SKIF_ImGui_EndDisabled (installs.empty());
      }

      else
        ImGui::TableHeader (name);

      ImGui::PopID ();
    }
  };

  auto _SelectionMenu = [&](void) -> void
  {
    SKIF_ImGui_PushDisableState (! anyOutdated);
    if (ImGui::MenuItem (L_("Select Outdated")))
    {
      for (auto& install : installs)
        install.selected = install.outdated && install.exists;
      action = LocalAction::AnySelected;
    }
    SKIF_ImGui_PopDisableState  (! anyOutdated);

    if (anyMissing && ImGui::MenuItem (L_("Select Missing")))
    {
      for (auto& install : installs)
        install.selected = ! install.exists;
      action = LocalAction::AnySelected;
    }

    if (ImGui::MenuItem (L_("Select All")))
    {
      for (auto& install : installs)
        install.selected = true;
      anchor_path.clear ();
      action = LocalAction::AnySelected;
    }

    SKIF_ImGui_PushDisableState (! anySelected);
    if (ImGui::MenuItem (L_("Deselect All")))
    {
      for (auto& install : installs)
        install.selected = false;
      anchor_path.clear ();
      action = LocalAction::AnySelected;
    }
    SKIF_ImGui_PopDisableState  (! anySelected);

    ImGui::Separator ();

    SKIF_ImGui_PushDisableState (! anyOutdatedSelected);
    if (ImGui::MenuItem (L_("Update Selected")))
      action = LocalAction::Update;
    SKIF_ImGui_PopDisableState  (! anyOutdatedSelected);

    SKIF_ImGui_PushDisableState (! anySelected);
    if (ImGui::MenuItem (L_("Delete Selected")))
      action = LocalAction::Delete;
    SKIF_ImGui_PopDisableState  (! anySelected);

    ImGui::Separator ();

    if (ImGui::MenuItem (L_("Refresh")))
      RefreshInstalls ();
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
  ImGui::SetNextWindowSizeConstraints (ImVec2 (800.0f, 300.0f) * SKIF_ImGui_GetDPIScaleForWindow (this->label.c_str()), ImVec2 (FLT_MAX, FLT_MAX));
  extern ImRect windowRect;
  ImGui::SetNextWindowPos (windowRect.GetCenter(), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));

  if (ImGui::Begin (TL_(this->label.c_str()), &showWindow,
                      ImGuiWindowFlags_NoCollapse        |
                      ImGuiWindowFlags_NoScrollbar       |
                      ImGuiWindowFlags_MenuBar           |
                      ImGuiWindowFlags_NoSavedSettings   ))
  {
    if (this->hwnd == nullptr && ImGui::GetWindowViewport()->PlatformHandleRaw)
      this->hwnd = (HWND)ImGui::GetWindowViewport()->PlatformHandleRaw;

    // Read by SKIF.cpp next frame so the global Ctrl+A (Add game) stays out of the way
    focused = ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows);

    // Ctrl+A: select outdated, then all rows; never deselects.
    // Not while a popup/menu of ours has focus or a text field wants the keyboard.
    const ImGuiIO& io = ImGui::GetIO ( );
    if (focused && ! io.WantTextInput &&
        io.KeyCtrl && ! io.KeyShift && ! io.KeyAlt &&
        ImGui::IsKeyPressed (ImGuiKey_A, false))
      CycleSelectAll (false);

    // A prompt of ours is waiting for an answer
    const bool anyPrompt = (want_install != nullptr || want_new ||
                            ! new_exe_path.empty () || ! new_dll_path.empty () ||
                            ! new_uptodate_desc.empty () ||
                            action != LocalAction::None);

    // Ctrl+W: close the window, like Ctrl+W in Properties and the INI editor (also while typing or a prompt is shown,
    //   same as the title bar's close button). The main window's Ctrl+W (close the app) only applies to the main window
    if (focused && ImGui::IsKeyChordPressed (SKIF_Hotkey_CloseWindow))
      showWindow = false;

    if (focused && ! io.WantTextInput && ! anyPrompt && ! io.KeyShift && ! io.KeyAlt)
    {
      // Ctrl+N: install Special K next to a new executable
      if (io.KeyCtrl && ImGui::IsKeyPressed (ImGuiKey_N, false))
        want_new = true;

      // Escape: close the window, unless that Escape just closed a menu or popup
      else if (! io.KeyCtrl && ! popup_open_prev && ImGui::IsKeyPressed (ImGuiKey_Escape, false))
        showWindow = false;
    }

    if (ImGui::BeginMenuBar ( ))
    {
      if (ImGui::BeginMenu (L_("File")))
      {
        if (ImGui::MenuItem (L_("New"), "Ctrl+N", false, ! anyPrompt))
          want_new = true;

        if (ImGui::MenuItem (L_("Close"),
                             // TRANSLATORS: The Escape key of the keyboard
                             C_("key", "Escape")))
          showWindow = false;

        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu (L_("Selection")))
      {
        _SelectionMenu ( );

        ImGui::EndMenu ( );
      }

      ImGui::EndMenuBar ( );
    }

    if (installs.empty ())
    {
      // Disabled text, with the highlighted parts in the regular color
      const ImVec4 colText = ImGui::GetStyleColorVec4 (ImGuiCol_Text);
      ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

      ImGui::TextWrapped      ("%s", _("No local installs found."));

      SKIF_ImGui_Spacing      ( );
      SKIF_ImGui_Spacing      ( );

      ImGui::TextWrapped      ("%s", _("To perform a local install of Special K:"));

      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::TextColored      (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                            (const char *)u8"\u2022 ");
      ImGui::SameLine         ( );
      // TRANSLATORS: The text in [brackets] is highlighted (keep the brackets); {0} is a floppy disk icon.
      //   "File -> Install Wrapper DLL" is a menu of Special K itself (in-game), which is in English.
      SKIF_ImGui_TextRichHighlight (F_("While in-game, use [{0} File -> Install Wrapper DLL].", ICON_FA_FLOPPY_DISK).c_str ( ), { }, colText);
      ImGui::EndGroup         ( );

      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::TextColored      (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                            (const char *)u8"\u2022 ");
      ImGui::SameLine         ( );
      // TRANSLATORS: The text in [brackets] is highlighted (keep the brackets); Ctrl and Shift are keys of the keyboard
      SKIF_ImGui_TextRichHighlight (_("During launch, hold down [Ctrl + Shift] to access the compatibility options."), { }, colText);
      ImGui::EndGroup         ( );

      ImGui::PopStyleColor    ( );
    }

    else if (ImGui::BeginTable ("##LocalInstalls", 6,
                                ImGuiTableFlags_RowBg         |
                                ImGuiTableFlags_ScrollY       |
                                ImGuiTableFlags_Sortable))
    {
      ImGui::TableSetupColumn ("Select",    _colFlagsMisc,     30.0f * SKIF_ImGui_GlobalDPIScale, Col_Select);
      ImGui::TableSetupColumn (C_("column", "Path"), _colFlagsPath,      0.0f,                             Col_Path);
      ImGui::TableSetupColumn (C_("column", "Version"), _colFlagsVersion, 100.0f * SKIF_ImGui_GlobalDPIScale, Col_Version);
      ImGui::TableSetupColumn ("Updater",   _colFlagsMisc,     20.0f * SKIF_ImGui_GlobalDPIScale, Col_Updater);
      ImGui::TableSetupColumn (C_("column", "Status"), _colFlagsStatus,   20.0f * SKIF_ImGui_GlobalDPIScale, Col_Status);
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

      // Shift range preview colour, grabbed before the softened Header colours below are pushed
      const ImU32 previewCol = ImGui::GetColorU32 (ImGuiCol_HeaderHovered, 0.35f);

      ImGui::PushStyleColor   (ImGuiCol_Text,          ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

      // Softer row highlight for the full-row Selectable, so it doesn't overpower RowBg
      ImGui::PushStyleColor   (ImGuiCol_Header,        ImGui::GetStyleColorVec4 (ImGuiCol_Header)        * ImVec4 (1.0f, 1.0f, 1.0f, 0.6f));
      ImGui::PushStyleColor   (ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4 (ImGuiCol_HeaderHovered) * ImVec4 (1.0f, 1.0f, 1.0f, 0.3f));
      ImGui::PushStyleColor   (ImGuiCol_HeaderActive,  ImGui::GetStyleColorVec4 (ImGuiCol_HeaderActive)  * ImVec4 (1.0f, 1.0f, 1.0f, 0.5f));

      // Rows a Shift+Click would select, using last frame's hovered row
      int preview_lo = -1,
          preview_hi = -1;

      if (ImGui::GetIO ().KeyShift && hovered_row != -1)
      {
        const int anchor = FindInstall (anchor_path);
        if (anchor != -1)
        {
          preview_lo = std::min (anchor, hovered_row);
          preview_hi = std::max (anchor, hovered_row);
        }
      }

      int hovered_now = -1;

      for (int idx = 0; idx < static_cast<int> (installs.size ()); ++idx)
      {
        auto& install = installs [idx];

        ImGui::PushID (idx);
        ImGui::TableNextRow ();

        if (idx >= preview_lo && idx <= preview_hi && ! install.selected)
          ImGui::TableSetBgColor (ImGuiTableBgTarget_RowBg1, previewCol);

        // Column 0: Select
        if (ImGui::TableNextColumn ( ))
        {
          const ImVec2 cellPos = ImGui::GetCursorPos ( );

          // Invisible full-row hit target. It is submitted first, so the checkbox and
          // buttons drawn over it with AllowOverlap still get their own clicks.
          if (ImGui::Selectable ("##row", install.selected,
                                 ImGuiSelectableFlags_SpanAllColumns |
                                 ImGuiSelectableFlags_AllowOverlap,
                                 ImVec2 (0.0f, ImGui::GetFrameHeight ())))
            HandleRowClick (idx, false);

          if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenOverlappedByItem))
          {
            hovered_now = idx;

            // Select the row before the context menu opens (the popup itself opens on release further down)
            if (ImGui::IsMouseReleased (ImGuiMouseButton_Right))
              HandleRowRightClick (idx);
          }

          ImGui::SetCursorPos (cellPos);

          float offset = (ImGui::GetContentRegionAvail ().x - ImGui::GetFrameHeight ()) * 0.5f;
          if (offset > 0.0f)
            ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);

          ImGui::PushStyleColor (ImGuiCol_FrameBg,        IM_COL32_DISABLE);
          ImGui::PushStyleColor (ImGuiCol_FrameBgHovered, IM_COL32_DISABLE);
          ImGui::PushStyleColor (ImGuiCol_FrameBgActive,  IM_COL32_DISABLE);

          if (SKIF_ImGui_CheckboxHoverPreview ("##checkbox", &install.selected))
            HandleRowClick (idx, true);

          ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgActive
          ImGui::PopStyleColor ( ); // ImGuiCol_FrameBgHovered
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
              ImGui::SetTooltip ("%s", F_("The local DLL file is outdated.\n"
                                          "Click to update it to v {0}.",
                                          _inject.SKVer32_utf8).c_str());
          }

          // Reinstall
          else
          {
            ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
            if (ImGui::SmallButton (ICON_FA_ARROW_DOWN))
              want_install = &install;
            ImGui::PopStyleColor ( );

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("%s", F_("The local DLL file could not be found.\n"
                                          "Click to reinstall it as v {0}.",
                                          _inject.SKVer32_utf8).c_str());
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
              ImGui::SetTooltip ("%s", _("Folder no longer exists"));
          }

          else if (! install.exists)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure),
                                ICON_FA_CIRCLE_XMARK);

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("%s", _("Local DLL file no longer exists"));
          }

          else if (install.outdated)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow),
                                ICON_FA_TRIANGLE_EXCLAMATION);

            if (ImGui::IsItemHovered ())
              ImGui::SetTooltip ("%s", F_("Outdated (latest: {0})",
                                          _inject.SKVer32_utf8).c_str ());
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
      hovered_row = hovered_now;
      ImGui::PopStyleColor    ( ); // ImGuiCol_HeaderActive
      ImGui::PopStyleColor    ( ); // ImGuiCol_HeaderHovered
      ImGui::PopStyleColor    ( ); // ImGuiCol_Header
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
      _SelectionMenu ( );

      ImGui::EndPopup ();
    }

    // Checked by the Escape hotkey next frame
    popup_open_prev = ImGui::IsPopupOpen (0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      ShowWindowAsync ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, SW_MINIMIZE);

    // Process notifications for this window
    ImGui::RenderNotifications ();

    if (want_install)
    {
      bool dontCare = true;
      switch (SKIF_ImGui_6432bitnessPrompt (L_("Select the bitness to install"), &dontCare, nullptr,
                                            // TRANSLATORS: {0} is a floppy disk icon. "File -> Install Wrapper DLL" is a menu of Special K itself (in-game), which is in English.
                                            F_("Or while in-game, use {0} File -> Install Wrapper DLL.", ICON_FA_FLOPPY_DISK).c_str ( )))
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

    // File -> New, step 1: pick the executable (blocks until the file dialog closes)
    else if (want_new)
    {
      want_new = false;
      BrowseNewInstall ( );
    }

    // File -> New: Special K in the picked folder is already up to date
    else if (! new_uptodate_desc.empty ())
    {
      bool dontCare = true;
      if (DChoice_None != SKIF_ImGui_InfoPrompt (L_("Already Up-to-Date###LocalNewUpToDate"), &dontCare,
                                                 _("Special K is already installed in this folder and is fully up-to-date."),
                                                 new_uptodate_desc.c_str ()))
        new_uptodate_desc.clear ( );
    }

    // File -> New, step 2: pick the DLL filename
    else if (! new_exe_path.empty ())
    {
      int  picked = -1;
      bool cancel = false;

      if (SKIF_ImGui_BeginPopup (L_("Install Special K###LocalNewInstall")))
      {
        ImGui::TextWrapped         ("%s", _("Which DLL filename should Special K be installed as?"));
        ImGui::TextDisabled ("%s", new_exe_path_utf8.c_str ());

        if (new_bitness == PEBitness_64bit)
          SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("64-bit executable"));
        else if (new_bitness == PEBitness_32bit)
          SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("32-bit executable"));
        else
          SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Could not detect the bitness of the executable; you will be asked next."));

        if (std::find (new_dll_states.begin (), new_dll_states.end (), 1) != new_dll_states.end ())
          SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Special K is already installed in this folder; only that file can be updated."));

        ImGui::Separator ( );

        const ImVec2 btn = ImVec2 (std::max (110.0f * SKIF_ImGui_GlobalDPIScale,
                                             ImGui::CalcTextSize (L_("Cancel"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f), 0.0f);

        for (int i = 0; i < static_cast<int> (IM_ARRAYSIZE (LocalDLLs)); ++i)
        {
          const int state = (i < static_cast<int> (new_dll_states.size ())) ? new_dll_states [i] : 0;

          if (i % 4 != 0)
            ImGui::SameLine ( );

          // Never overwrite a file that is not Special K (e.g. another mod using the same name),
          //   and don't add a second copy when Special K is already installed under another name
          SKIF_ImGui_BeginDisabled (state == 2 || state == 3);
          if (ImGui::Button (LocalDLLs [i].name_utf8, btn))
            picked = i;
          SKIF_ImGui_EndDisabled   (state == 2 || state == 3);

          if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip ("%s%s%s", T_(LocalDLLs [i].hint), (state >= 1 && state <= 3) ? "\n\n" : "",
                               (state == 1) ? _("Special K is already installed as this file and will be updated.") :
                               (state == 2) ? _("A different file with this name already exists in the folder.")    :
                               (state == 3) ? _("Special K is already installed in this folder under another name.") : "");
        }

        ImGui::Separator ( );

        cancel = ImGui::Button (L_("Cancel"), btn) ||
                 (ImGui::IsWindowFocused ( ) && ImGui::IsKeyPressed (ImGuiKey_Escape, false));
      }

      SKIF_ImGui_EndPopup ( );

      if (picked != -1)
      {
        const std::wstring target =
          (std::filesystem::path (new_exe_path).parent_path () / LocalDLLs [picked].name).wstring ();

        if (new_bitness > PEBitness_Invalid)
          InstallNew (target, new_bitness);
        else
          new_dll_path = target; // Ask for the bitness first (step 3)

        cancel = true; // Clears the step 2 state below
      }

      if (cancel)
      {
        new_exe_path.clear      ( );
        new_exe_path_utf8.clear ( );
        new_dll_states.clear    ( );
      }
    }

    // File -> New, step 3: the bitness of the executable could not be detected
    else if (! new_dll_path.empty ())
    {
      bool dontCare = true;
      switch (SKIF_ImGui_6432bitnessPrompt (L_("Select the bitness to install###LocalNewBitness"), &dontCare,
                                            _("Could not detect the bitness of the executable.\nShould 64-bit or 32-bit be used?")))
      {
      case DChoice_32bit:
        InstallNew (new_dll_path, PEBitness_32bit);
        new_dll_path.clear ( );
        break;
      case DChoice_64bit:
        InstallNew (new_dll_path, PEBitness_64bit);
        new_dll_path.clear ( );
        break;
      case DChoice_Cancel:
        new_dll_path.clear ( );
        break;
      }
    }

    else if (action == LocalAction::AnySelected)
    {
      UpdateAnyStates ( );
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
          anchor_path.clear ();
          break;
        }
        action = LocalAction::None;
      };

      const char* titUpdate     = L_("Update Installs?");
      const char* titDelete     = L_("Delete Installs?");
      const char* msgUpdate     = _("Are you sure you want to update all selected installs?");
      const char* msgDelete     = _("Are you sure you want to delete all selected installs?");
      const char* msgDeleteDesc = _("Some leftover files may still be present in the game folder.");

      const char* name = (action == LocalAction::Update ? titUpdate     :
                          action == LocalAction::Delete ? titDelete     :
                          nullptr);
      const char* text = (action == LocalAction::Update ? msgUpdate     :
                          action == LocalAction::Delete ? msgDelete     :
                          nullptr);
      const char* desc = (action == LocalAction::Update ? nullptr       :
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

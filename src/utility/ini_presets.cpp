#include <utility/ini_presets.h>

#include <SKIF.h>
#include <utility/fsutil.h>
#include <utility/utility.h>
#include <utility/sk_utility.h>
#include <utility/skif_imgui.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <shlwapi.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

namespace {

struct Preset
{
  std::string  Name;
  std::wstring Path;

  Preset (const std::wstring& n, const std::wstring& p)
  {
    Name = SK_WideCharToUTF8 (n);
    Path = p;
  };
};

} // namespace

// Finds the presets in a folder. Empty files, and files only holding a byte order mark, are skipped
static std::vector <Preset>
SKIF_IniPresets_Find (const std::wstring& folder, const std::wstring& find_pattern)
{
  HANDLE hFind        = INVALID_HANDLE_VALUE;
  WIN32_FIND_DATA ffd = { };
  std::vector <Preset> tmpPresets;

  hFind =
    FindFirstFileExW ((folder + find_pattern).c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

  if (INVALID_HANDLE_VALUE != hFind)
  {
    do {
      Preset newPreset = { PathFindFileName (ffd.cFileName), folder + ffd.cFileName };
      bool         add = false;
      LONG_PTR    size = 0;

      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      {
        struct _stat64 buffer;
        if (0 == _wstat64 (newPreset.Path.c_str(), &buffer))
          size = static_cast<LONG_PTR>(buffer.st_size);
      }

      else
        size = static_cast<LONG_PTR>(((ffd.nFileSizeHigh) * (MAXDWORD + 1)) + ffd.nFileSizeLow);

      // All files larger than 4 bytes should be added
      if (4 < size)
        add = true;

      // All files between 1-4 bytes should be checked for byte order marks (0 byte files are skipped automatically)
      else if (0 < size && size <= 4)
      {
        // When opening an existing file, the CreateFile function performs the following actions:
        // [...] and ignores any file attributes (FILE_ATTRIBUTE_*) specified by dwFlagsAndAttributes.
        CHandle hPreset (
          CreateFileW (newPreset.Path.c_str(),
                          GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,        OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                  nullptr ) );

        if (hPreset != INVALID_HANDLE_VALUE)
        {
          DWORD dwRead      = 0;
          char  data [4]    = { };

          if (ReadFile (hPreset, data, static_cast<DWORD>(size), &dwRead, nullptr) && dwRead)
          {
            const std::string_view content (data, dwRead);

            using namespace std::string_view_literals;

            // Skip files only containing byte order marks
            if (content != "\xEF\xBB\xBF"sv     && // UTF-8,  with BOM
                content != "\xFF\xFE"sv         && // UTF-16, little endian
                content != "\xFE\xFF"sv         && // UTF-16, big endian
                content != "\xFF\xFE\x00\x00"sv && // UTF-32, little endian
                content != "\x00\x00\xFE\xFF"sv  ) // UTF-32, big endian
              add = true;
          }
        }
      }

      if (add)
        tmpPresets.push_back (newPreset);

    } while (FindNextFile (hFind, &ffd));

    FindClose (hFind);
  }

  return tmpPresets;
}

// Creates a preset file holding only a byte order mark, if it does not exist
static void
SKIF_IniPresets_CreateEmpty (const std::wstring& folder, const std::wstring& path)
{
  if (PathFileExists (path.c_str()))
    return;

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists             (folder, ec))
        std::filesystem::create_directories (folder, ec);

  std::wofstream config_file (path.c_str(), std::wofstream::out | std::wofstream::trunc);

  if (config_file.is_open())
  {
    config_file << utf8_bom;
    config_file.close ( );
  }
}

SKIF_IniPresetChoice
SKIF_ImGui_IniPresetItems (bool handCursor)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  static std::wstring DefaultPresetsFolder = SK_FormatStringW (LR"(%ws\Global\)",        _path_cache.specialk_userdata);
  static std::wstring  CustomPresetsFolder = SK_FormatStringW (LR"(%ws\Global\Custom\)", _path_cache.specialk_userdata);
  static SKIF_DirectoryWatch  GlobalWatch;
  static SKIF_DirectoryWatch  CustomWatch;
  static std::vector <Preset> DefaultPresets;
  static std::vector <Preset> DefaultPresetsMissing;
  static std::vector <Preset>  CustomPresets;
  static bool refreshDefaultPresets = true;
  static bool refreshCustomPresets  = true;

  // Directory watches -- updates the vectors automatically
  if (CustomWatch.isSignaled (CustomPresetsFolder)  || refreshCustomPresets)
  {
    refreshCustomPresets  = false;
    CustomPresets         = SKIF_IniPresets_Find (CustomPresetsFolder,  L"*.ini");
  }

  if (GlobalWatch.isSignaled (DefaultPresetsFolder) || refreshDefaultPresets)
  {
    refreshDefaultPresets = false;
    DefaultPresets        = SKIF_IniPresets_Find (DefaultPresetsFolder, L"default_*.ini");
    DefaultPresetsMissing.clear();

    static std::vector <Preset> defaultList = {
      Preset( L"default_SpecialK.ini", (DefaultPresetsFolder + L"default_SpecialK.ini") ),
      Preset( L"default_OpenGL32.ini", (DefaultPresetsFolder + L"default_OpenGL32.ini") ),
      Preset( L"default_dinput8.ini",  (DefaultPresetsFolder + L"default_dinput8.ini")  ),
      Preset( L"default_dxgi.ini",     (DefaultPresetsFolder + L"default_dxgi.ini")     ),
    //Preset( L"default_d3d12.ini",    (DefaultPresetsFolder + L"default_d3d12.ini")    ), // Apparently Special K doesn't support being loaded as D3D12.dll
      Preset( L"default_d3d11.ini",    (DefaultPresetsFolder + L"default_d3d11.ini")    ),
      Preset( L"default_d3d9.ini",     (DefaultPresetsFolder + L"default_d3d9.ini")     ),
      Preset( L"default_d3d8.ini",     (DefaultPresetsFolder + L"default_d3d8.ini")     ),
      Preset( L"default_ddraw.ini",    (DefaultPresetsFolder + L"default_ddraw.ini")    ),
    };

    for (auto& preset : defaultList)
    {
      auto it = std::find_if (DefaultPresets.begin(), DefaultPresets.end(),
        [&preset](const Preset& p) -> bool
        {
          return (_stricmp (p.Name.c_str(), preset.Name.c_str()) == 0);
        });

      if (it == DefaultPresets.end())
        DefaultPresetsMissing.push_back (preset);
    }
  }

  SKIF_IniPresetChoice choice;

  auto _HandCursor = [&](void)
  {
    if (handCursor)
      SKIF_ImGui_SetMouseCursorHand ( );
  };

  // A preset: click applies it, right-click offers to edit it
  auto _PresetItem = [&](const Preset& preset)
  {
    ImGui::PushID (preset.Name.c_str());

    if (ImGui::Selectable (preset.Name.c_str()))
      choice = { SKIF_IniPresetAction_Apply, preset.Path };

    _HandCursor ( );

    ImGuiID editMenu = ImGui::GetID ("###editMenu");

    if (! ImGui::IsPopupOpen   (editMenu, ImGuiPopupFlags_AnyPopupLevel) &&
          ImGui::IsItemClicked (ImGuiMouseButton_Right))
      ImGui::OpenPopup    (editMenu);

    if (ImGui::BeginPopupEx (editMenu, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
    {
      if (ImGui::Selectable (L_("Edit")))
        choice = { SKIF_IniPresetAction_Edit, preset.Path };

      ImGui::EndPopup ( );
    }

    ImGui::PopID ( );
  };

  if (ImGui::BeginMenu (L_("Apply Preset")))
  {
    // Custom Presets
    for (auto& preset : CustomPresets)
      _PresetItem (preset);

    if (! CustomPresets.empty() && ! DefaultPresets.empty())
      ImGui::Separator ( );

    // Default Presets
    for (auto& preset : DefaultPresets)
      _PresetItem (preset);

    if (! DefaultPresets.empty() || ! CustomPresets.empty())
      ImGui::Separator ( );

    if (ImGui::BeginMenu (L_("Create New")))
    {
      if (ImGui::Selectable (L_("Custom Preset...")))
      {
        // An existing Untitled.ini is opened as it is (it is not emptied)
        std::wstring path = (CustomPresetsFolder + LR"(Untitled.ini)");
        SKIF_IniPresets_CreateEmpty (CustomPresetsFolder, path);
        refreshCustomPresets = true; // The watch might only have been set up now that the folder exists

        choice = { SKIF_IniPresetAction_Edit, path };
      }

      _HandCursor ( );

      if (! DefaultPresetsMissing.empty())
        ImGui::Separator ( );

      for (auto& preset : DefaultPresetsMissing)
      {
        if (ImGui::Selectable (preset.Name.c_str()))
        {
          SKIF_IniPresets_CreateEmpty (DefaultPresetsFolder, preset.Path);
          refreshDefaultPresets = true;

          choice = { SKIF_IniPresetAction_Edit, preset.Path };
        }

        _HandCursor ( );
      }

      ImGui::EndMenu ( );
    }

    ImGui::EndMenu ( );
  }

  ImGui::Separator ( );

  if (ImGui::Selectable (L_("Apply Compatibility Config")))
    choice = { SKIF_IniPresetAction_Compatibility, L"" };

  _HandCursor ( );

  SKIF_ImGui_SetHoverTip (_("Known as the \"sledgehammer\" config within the community as it disables\n"
                            "various features of Special K in an attempt to improve compatibility."));

  if (ImGui::Selectable (L_("Reset")))
    choice = { SKIF_IniPresetAction_Reset, L"" };

  _HandCursor ( );

  return choice;
}

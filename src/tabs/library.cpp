//
// Copyright 2020 Andon "Kaldaien" Coleman
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

#include <tabs/library.h>

#include <wmsdk.h>
#include <filesystem>
#include <SKIF.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>

#include <utility/injection.h>

#include "DirectXTex.h"

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>

#include <stores/Steam/apps_list.h>
#include <stores/GOG/gog_library.h>
#include <stores/epic/epic_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <concurrent_queue.h>
#include <utility/fsutil.h>
#include <atlimage.h>
#include <TlHelp32.h>
#include <gsl/gsl_util>

#include <utility/registry.h>
#include <utility/updater.h>
#include <stores/Steam/steam_library.h>

const int              SKIF_STEAM_APPID  = 1157970;
bool                   SKIF_STEAM_OWNER  = false;
bool                   loadCover         = false;
bool                   tryingToLoadCover = false;
std::atomic<bool>      gameCoverLoading  = false;
std::atomic<bool>      modDownloading    = false;
std::atomic<bool>      modInstalling     = false;
std::atomic<bool>      gameWorkerRunning = false;
std::atomic<uint32_t>  modAppId          = 0;

static bool clickedGameLaunch,
            clickedGameLaunchWoSK,
            clickedGalaxyLaunch,
            clickedGalaxyLaunchWoSK = false,
            openedGameContextMenu   = false;
const float fTintMin                = 0.75f;
      float fTint                   = 1.0f;
      float fAlpha                  = 0.0f;

PopupState IconMenu        = PopupState_Closed;
PopupState ServiceMenu     = PopupState_Closed;

PopupState AddGamePopup    = PopupState_Closed;
PopupState RemoveGamePopup = PopupState_Closed;
PopupState ModifyGamePopup = PopupState_Closed;
PopupState ConfirmPopup    = PopupState_Closed;

std::string confirmPopupTitle;
std::string confirmPopupText;

extern ImVec2          SKIF_vecAlteredSize;
extern float           SKIF_ImGui_GlobalDPIScale;
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern std::wstring    SKIF_Epic_AppDataPath;
extern DWORD           invalidatedDevice;
extern bool            GOGGalaxy_Installed;
extern std::wstring    GOGGalaxy_Path;

#define _WIDTH   (415.0f * SKIF_ImGui_GlobalDPIScale) - (SKIF_vecAlteredSize.y > 0.0f ? ImGui::GetStyle().ScrollbarSize : 0.0f) // AppListInset1, AppListInset2, Injection_Summary_Frame (prev. 414.0f)
#define _HEIGHT  (620.0f * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x - 2.0f) // AppListInset1
#define _HEIGHT2 (280.0f * SKIF_ImGui_GlobalDPIScale)                                             // AppListInset2

std::atomic<int>  textureLoadQueueLength{ 1 };

int getTextureLoadQueuePos (void) {
  return textureLoadQueueLength.fetch_add(1) + 1;
}

CComPtr <ID3D11ShaderResourceView> pPatTexSRV;
CComPtr <ID3D11ShaderResourceView> pSKLogoTexSRV;

// Forward declaration
void UpdateInjectionStrategy (app_record_s* pApp);

// External declaration
extern void SKIF_Shell_AddJumpList (std::wstring name, std::wstring path, std::wstring parameters, std::wstring directory, std::wstring icon_path, bool bService);

// Functions / Structs

float
AdjustAlpha (float a)
{
  return std::pow (a, 1.0f / 2.2f );
}

#pragma region Trie Keyboard Hint Search

struct {
  uint32_t            id = 0;
  app_record_s::Store store;
} static manual_selection;

Trie labels;

#pragma endregion

#pragma region SKIF_Lib_SummaryCache

void
SKIF_Lib_SummaryCache::Refresh (app_record_s* pApp)
{
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  sk_install_state_s& sk_install =
    pApp->specialk.injection;

  app_id   = pApp->id;
  running  = pApp->_status.running;
  updating = pApp->_status.updating;
  autostop = _inject.bAckInj;

  service  = (sk_install.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
             (sk_install.injection.bitness == InjectionBitness::SixtyFour &&  _inject.pid64) ||
             (sk_install.injection.bitness == InjectionBitness::Unknown   && (_inject.pid32  &&
                                                                              _inject.pid64));

  wchar_t     wszDLLPath [MAX_PATH];
  wcsncpy_s ( wszDLLPath, MAX_PATH,
                sk_install.injection.dll_path.c_str (),
                        _TRUNCATE );

  dll.full_pathW = wszDLLPath;
  dll.full_path  = SK_WideCharToUTF8 (dll.full_pathW);

  PathStripPathW  (wszDLLPath);
  dll.shorthandW = wszDLLPath;
  dll.shorthand  = SK_WideCharToUTF8 (dll.shorthandW);
  dll.versionW   = sk_install.injection.dll_ver;
  dll.version    = SK_WideCharToUTF8 (dll.versionW);

  wchar_t     wszConfigPath [MAX_PATH];
  wcsncpy_s ( wszConfigPath, MAX_PATH,
                sk_install.config.file.c_str (),
                        _TRUNCATE );
  
  config.root_dirW  = sk_install.config.dir;
  config.root_dir   = SK_WideCharToUTF8 (config.root_dirW);
  config.full_pathW = wszConfigPath;
  config.full_path  = SK_WideCharToUTF8 (config.full_pathW);
  PathStripPathW     (wszConfigPath);
  config.shorthandW = wszConfigPath;
  config.shorthand  = SK_WideCharToUTF8 (config.shorthandW);

  //if (! PathFileExistsW (sk_install.config.file.c_str ()))
  //  config.shorthand.clear ();

  //if (! PathFileExistsA (dll.full_path.c_str ()))
  //  dll.shorthand.clear ();

  injection.type        = "None";
  injection.status.text.clear ();
  injection.hover_text.clear  ();

  switch (sk_install.injection.type)
  {
    case sk_install_state_s::Injection::Type::Local:
      injection.type = "Local";
      injection.type_version = SK_FormatString (R"(%s v %s (%s))", injection.type.c_str(), dll.version.c_str(), dll.shorthand.c_str());
      break;

    case sk_install_state_s::Injection::Type::Global:
    default: // Unknown injection strategy, but let's assume global would work

      if ( _inject.bHasServlet )
      {
        injection.type         = "Global";
        injection.type_version = SK_FormatString (R"(%s v %s)", injection.type.c_str(), dll.version.c_str());
        injection.status.text  = 
                    (service)  ? (_inject.bAckInj) ? "Waiting for game..." : "Running"
                               : "                                "; //"Service Status";

        injection.status.color =
                    (service)  ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)  // HSV (0.3F,  0.99F, 1.F)
                               : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info); // HSV (0.08F, 0.99F, 1.F);
        injection.status.color_hover =
                    (service)  ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                               : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        injection.hover_text   =
                    (service)  ? "Click to stop the service"
                               : "Click to start the service";
      }
      break;
  }

  switch (sk_install.config.type)
  {
    case ConfigType::Centralized:
      config_repo = "Centralized"; break;
    case ConfigType::Localized:
      config_repo = "Localized";   break;
    default:
      config_repo = "Unknown";
      config.shorthand.clear ();   break;
  }
}

#pragma endregion


#pragma region PrintInjectionSummary

void
GetInjectionSummary (app_record_s* pApp)
{
  if (pApp == nullptr || pApp->id == SKIF_STEAM_APPID)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_Lib_SummaryCache& _cache      = SKIF_Lib_SummaryCache::GetInstance ( );


#pragma region _IsLocalDLLFileOutdated

  auto _IsLocalDLLFileOutdated = [&](void) -> bool
  {
    bool ret = false;

    if ((pApp->store != app_record_s::Store::Steam) ||
        (pApp->store == app_record_s::Store::Steam  && // Exclude the check for games with known older versions
          _cache.app_id != 405900       && // Disgaea PC
          _cache.app_id != 359870       && // FFX/X-2 HD Remaster
        //_cache.app_id != 578330       && // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
          _cache.app_id != 429660       && // Tales of Berseria
          _cache.app_id != 372360       && // Tales of Symphonia
        //_cache.app_id != 738540       && // Tales of Vesperia DE // Do not exclude from the updater as mainline SK further improves the game
          _cache.app_id != 351970          // Tales of Zestiria
        ))
    {
      if (SKIF_Util_CompareVersionStrings (_inject.SKVer32, SK_UTF8ToWideChar(_cache.dll.version)) > 0)
      {
        ret = true;
      }
    }
        
    return ret;
  };

#pragma endregion

#pragma region _UpdateLocalDLLFile

  auto _UpdateLocalDLLFile = [&](void) -> void
  {
    int iBinaryType = SKIF_Util_GetBinaryType (SK_UTF8ToWideChar (_cache.dll.full_path).c_str());
    if (iBinaryType > 0)
    {
      wchar_t                       wszPathToGlobalDLL [MAX_PATH + 2] = { };
      GetModuleFileNameW  (nullptr, wszPathToGlobalDLL, MAX_PATH);
      PathRemoveFileSpecW (         wszPathToGlobalDLL);
      PathAppendW         (         wszPathToGlobalDLL, (iBinaryType == 2) ? L"SpecialK64.dll" : L"SpecialK32.dll");

      if (CopyFile (wszPathToGlobalDLL, SK_UTF8ToWideChar (_cache.dll.full_path).c_str(), FALSE))
      {
        PLOG_INFO << "Successfully updated " << _cache.dll.full_path << " from v " << _cache.dll.version << " to v " << _inject.SKVer32;
      }

      else {
        PLOG_ERROR << "Failed to copy " << wszPathToGlobalDLL << " to " << SK_UTF8ToWideChar (_cache.dll.full_path);
        PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
      }
    }

    else {
      PLOG_ERROR << "Failed to retrieve binary type from " << _cache.dll.full_path << " -- returned: " << iBinaryType;
      PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    }
  };

#pragma endregion

  if (_cache.app_id      != pApp->id               ||
      _cache.service     != _inject.bCurrentState  ||
      _cache.running     != pApp->_status.running  ||
      _cache.updating    != pApp->_status.updating ||
      _cache.autostop    != _inject.bAckInj        ||
      _inject.libCacheRefresh   == true            )
  {
    // If the global DLL files have been changed, we also need to update the injection strategy before we refresh the cache
    if (_inject.libCacheRefresh)
    {   _inject.libCacheRefresh  = false;
      UpdateInjectionStrategy (pApp);
    }

    _cache.Refresh    (pApp);
  }

  static constexpr float
        num_lines = 4.0f;
  auto line_ht   =
    ImGui::GetTextLineHeightWithSpacing ();

  auto frame_id =
    ImGui::GetID ("###Injection_Summary_Frame");

  SKIF_ImGui_BeginChildFrame ( frame_id,
                                  ImVec2 ( _WIDTH - ImGui::GetStyle ().FrameBorderSize * 2.0f,
                                                                            num_lines * line_ht ),
                                    ImGuiWindowFlags_NavFlattened      |
                                    ImGuiWindowFlags_NoScrollbar       |
                                    ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_AlwaysAutoResize  |
                                    ImGuiWindowFlags_NoBackground
                              );

  ImGui::BeginGroup       ();

  // Column 1
  ImGui::BeginGroup       ();
  ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  //ImGui::NewLine          ();
  ImGui::TextUnformatted  ("Injection:");
  ImGui::TextUnformatted  ("Config Root:");
  ImGui::TextUnformatted  ("Config File:");
  ImGui::TextUnformatted  ("Platform:");
  ImGui::PopStyleColor    ();
  ImGui::ItemSize         (ImVec2 (110.f * SKIF_ImGui_GlobalDPIScale,
                                      0.f)
                          ); // Column should have min-width 130px (scaled with the DPI)
  ImGui::EndGroup         ();

  ImGui::SameLine         ();

  // Column 2
  ImGui::BeginGroup       ();

  // Injection
  if (!_cache.dll.shorthand.empty ())
  {
    //ImGui::TextUnformatted  (cache.dll.shorthand.c_str  ());
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowItemOverlap;

    if (_cache.injection.type._Equal("Global"))
      flags |= ImGuiSelectableFlags_Disabled;

    bool openLocalMenu = false;

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    if (ImGui::Selectable (_cache.injection.type_version.c_str(), false, flags))
    {
      openLocalMenu = true;
    }
    ImGui::PopStyleColor();

    if (_cache.injection.type._Equal("Local"))
    {
      SKIF_ImGui_SetMouseCursorHand ( );

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openLocalMenu = true;
    }

    SKIF_ImGui_SetHoverText       (_cache.dll.full_path.c_str  ());
        
    if (openLocalMenu && ! ImGui::IsPopupOpen ("LocalDLLMenu"))
      ImGui::OpenPopup    ("LocalDLLMenu");

    if (ImGui::BeginPopup ("LocalDLLMenu", ImGuiWindowFlags_NoMove))
    {
      if (_IsLocalDLLFileOutdated( ))
      {
        if (ImGui::Selectable (("Update to v " + _inject.SKVer32_utf8).c_str( )))
        {
          _UpdateLocalDLLFile ( );
        }

        ImGui::Separator ( );
      }

      if (ImGui::Selectable ("Uninstall"))
      {

        if (DeleteFile (_cache.dll.full_pathW.c_str()))
        {
          PLOG_INFO << "Successfully uninstalled local DLL v " << _cache.dll.version << " from " << _cache.dll.full_path;
        }
      }

      ImGui::EndPopup ( );
    }
  }

  else
    ImGui::TextUnformatted ("N/A");

  // Config Root
  // Config File
  if (!_cache.config.shorthand.empty ())
  {
    // Config Root
    if (ImGui::Selectable         (_cache.config_repo.c_str ()))
    {
      std::wstring wsRootDir = _cache.config.root_dirW;

      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists (            wsRootDir, ec))
            std::filesystem::create_directories (wsRootDir, ec);

      SKIF_Util_ExplorePath       (wsRootDir);
    }
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (_cache.config.root_dir.c_str ());

    // Config File
    if (ImGui::Selectable         (_cache.config.shorthand.c_str ()))
    {
      std::wstring wsRootDir = _cache.config.root_dirW;

      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists (            wsRootDir, ec))
            std::filesystem::create_directories (wsRootDir, ec);

      HANDLE h = CreateFile ( _cache.config.full_pathW.c_str(),
                      GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                            CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,
                                NULL );

      // We need to close the handle as well, as otherwise Notepad will think the file
      //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
      if (h != INVALID_HANDLE_VALUE)
        CloseHandle (h);

      SKIF_Util_OpenURI (_cache.config.full_pathW.c_str(), SW_SHOWNORMAL, NULL);
    }
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (_cache.config.full_path.c_str ());


    if ( ! ImGui::IsPopupOpen ("ConfigFileMenu") &&
            ImGui::IsItemClicked (ImGuiMouseButton_Right))
      ImGui::OpenPopup      ("ConfigFileMenu");

    if (ImGui::BeginPopup ("ConfigFileMenu", ImGuiWindowFlags_NoMove))
    {
      ImGui::TextColored (
        ImColor::HSV (0.11F, 1.F, 1.F),
          "Troubleshooting:"
      );

      ImGui::Separator ( );

      struct Preset
      {
        std::string  Name;
        std::wstring Path;

        Preset (std::wstring n, std::wstring p)
        {
          Name = SK_WideCharToUTF8 (n);
          Path = p;
        };
      };

      // Static stuff :D
      static std::wstring DefaultPresetsFolder = SK_FormatStringW (LR"(%ws\Global\)",        _path_cache.specialk_userdata);
      static std::wstring  CustomPresetsFolder = SK_FormatStringW (LR"(%ws\Global\Custom\)", _path_cache.specialk_userdata);
      static SKIF_DirectoryWatch SKIF_GlobalWatch;
      static SKIF_DirectoryWatch SKIF_CustomWatch;
      static std::vector<Preset> DefaultPresets;
      static std::vector<Preset>  CustomPresets;
      static bool runOnceDefaultPresets = true;
      static bool runOnceCustomPresets  = true;
      
      // Shared function
      auto _FindPresets = [](std::wstring folder, std::wstring find_pattern) -> std::vector<Preset>
      {
        HANDLE hFind = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATA ffd;
        std::vector<Preset> tmpPresets;

        hFind = FindFirstFile ((folder + find_pattern).c_str(), &ffd);

        if (INVALID_HANDLE_VALUE != hFind)
        {
          do {
            Preset newPreset = { PathFindFileName (ffd.cFileName), folder + ffd.cFileName };
            bool   add       = false;

            if (0 < ((ffd.nFileSizeHigh * (MAXDWORD + 1)) + ffd.nFileSizeLow))
              add = true;
            else if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            {
              struct _stat64 buffer;
              if (0 == _wstat64 (newPreset.Path.c_str(), &buffer) && 0 < buffer.st_size)
                add = true;
            }

            if (add)
              tmpPresets.push_back (newPreset);
          } while (FindNextFile (hFind, &ffd));

          FindClose (hFind);
        }

        return tmpPresets;
      };

      // Directory watches -- updates the vectors automatically
      if (SKIF_GlobalWatch.isSignaled (LR"(Global)", false) || runOnceDefaultPresets)
      {
        runOnceDefaultPresets = false;
        DefaultPresets        = _FindPresets (DefaultPresetsFolder, L"default_*.ini");
      }

      if (SKIF_CustomWatch.isSignaled (LR"(Global\Custom)", false) || runOnceCustomPresets)
      {
        runOnceCustomPresets = false;
        CustomPresets        = _FindPresets (CustomPresetsFolder, L"*.ini");
      }
          
      if (! DefaultPresets.empty() || ! CustomPresets.empty())
      {
        if (ImGui::BeginMenu("Apply Preset"))
        {
          // Default Presets
          if (! DefaultPresets.empty())
          {
            for (auto& preset : DefaultPresets)
            {
              if (ImGui::Selectable (preset.Name.c_str()))
              {
                CopyFile (preset.Path.c_str(), _cache.config.full_pathW.c_str(), FALSE);
                PLOG_VERBOSE << "Copying " << preset.Path << " over to " << _cache.config.full_path << ", overwriting any existing file in the process.";
              }

              SKIF_ImGui_SetMouseCursorHand ();
            }

            if (! CustomPresets.empty())
              ImGui::Separator ( );
          }

          // Custom Presets
          if (! CustomPresets.empty())
          {
            for (auto& preset : CustomPresets)
            {
              if (ImGui::Selectable (preset.Name.c_str()))
              {
                CopyFile (preset.Path.c_str(), _cache.config.full_pathW.c_str(), FALSE);
                PLOG_VERBOSE << "Copying " << preset.Path << " over to " << _cache.config.full_path << ", overwriting any existing file in the process.";
              }

              SKIF_ImGui_SetMouseCursorHand ();
            }
          }

          ImGui::EndMenu ( );
        }

        ImGui::Separator ( );
      }

      if (ImGui::Selectable ("Apply Compatibility Config"))
      {
        std::wofstream config_file(_cache.config.full_pathW.c_str());

        if (config_file.is_open())
        {
          // Static const as this profile never changes
          static const std::wstring out_text =
LR"([SpecialK.System]
ShowEULA=false
GlobalInjectDelay=0.0

[API.Hook]
d3d9=true
d3d9ex=true
d3d11=true
OpenGL=true
d3d12=true
Vulkan=true

[Steam.Log]
Silent=true

[Input.libScePad]
Enable=false

[Input.XInput]
Enable=false

[Input.Gamepad]
EnableDirectInput7=false
EnableDirectInput8=false
EnableHID=false
EnableNativePS4=false
EnableRawInput=true
AllowHapticUI=false

[Input.Keyboard]
CatchAltF4=false
BypassAltF4Handler=false

[Textures.D3D11]
Cache=false)";

          config_file.write(out_text.c_str(),
            out_text.length());

          config_file.close();
        }
      }

      SKIF_ImGui_SetMouseCursorHand ();

      SKIF_ImGui_SetHoverTip ("Known as the \"sledgehammer\" config within the community as it disables\n"
                              "various features of Special K in an attempt to improve compatibility.");

      if (ImGui::Selectable ("Reset"))
      {
        HANDLE h = CreateFile ( _cache.config.full_pathW.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                          TRUNCATE_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                              NULL );

        // We need to close the handle as well, as otherwise Notepad will think the file
        //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
        if (h != INVALID_HANDLE_VALUE)
          CloseHandle (h);
      }

      SKIF_ImGui_SetMouseCursorHand ();

      ImGui::EndPopup ( );
    }
  }

  else
  {
    ImGui::TextUnformatted        (_cache.config_repo.c_str ());
    ImGui::TextUnformatted        ("N/A");
  }

  // Platform
  ImGui::TextUnformatted          (pApp->store_utf8.c_str());

  // Column should have min-width 100px (scaled with the DPI)
  ImGui::ItemSize         (
    ImVec2 ( 130.0f * SKIF_ImGui_GlobalDPIScale,
                0.0f
            )                );
  ImGui::EndGroup         ( );
  ImGui::SameLine         ( );

  // Column 3
  ImGui::BeginGroup       ( );

  static bool quickServiceHover = false;

  // Service quick toogle / Waiting for game...
  if (_cache.injection.type._Equal ("Global") && ! _inject.isPending())
  {
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, (quickServiceHover) ? _cache.injection.status.color_hover.Value
                                                             : _cache.injection.status.color.Value);

    if (ImGui::Selectable (_cache.injection.status.text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
    {
      _inject._StartStopInject (_cache.service, _registry.bStopOnInjection, pApp->launch_configs[0].isElevated(pApp->id));

      _cache.app_id = 0;
    }

    ImGui::PopStyleColor (3);

    quickServiceHover = ImGui::IsItemHovered ();

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverTip        (_cache.injection.hover_text.c_str ());

    if ( ! ImGui::IsPopupOpen ("ServiceMenu") &&
            ImGui::IsItemClicked (ImGuiMouseButton_Right))
      ServiceMenu = PopupState_Open;
  }

  else {
    ImGui::NewLine ( );
  }
      

  if (_cache.injection.type._Equal ("Local"))
  {
    if (_IsLocalDLLFileOutdated ( ))
    {
      ImGui::SameLine        ( );

      ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
      if (ImGui::SmallButton (ICON_FA_ARROW_UP))
      {
        _UpdateLocalDLLFile ( );
      }
      ImGui::PopStyleColor ( );

      SKIF_ImGui_SetHoverTip (("The local DLL file is outdated.\n"
                                "Click to update it to v " + _inject.SKVer32_utf8 + "."));
    }
  }

  ImGui::EndGroup         ();

  // End of columns
  ImGui::EndGroup         ();

  ImGui::EndChildFrame    ();

  ImGui::Separator ();

  auto frame_id2 =
    ImGui::GetID ("###Injection_Play_Button_Frame");

  ImGui::PushStyleVar (
    ImGuiStyleVar_FramePadding,
      ImVec2 ( 120.0f * SKIF_ImGui_GlobalDPIScale,
                40.0f * SKIF_ImGui_GlobalDPIScale)
  );

  SKIF_ImGui_BeginChildFrame (
    frame_id2, ImVec2 (  0.0f,
                        110.f * SKIF_ImGui_GlobalDPIScale ),
      ImGuiWindowFlags_NavFlattened      |
      ImGuiWindowFlags_NoScrollbar       |
      ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_AlwaysAutoResize  |
      ImGuiWindowFlags_NoBackground
  );

  ImGui::PopStyleVar ();

  std::string      buttonLabel   = ICON_FA_GAMEPAD "  Launch";
  ImGuiButtonFlags buttonFlags   = ImGuiButtonFlags_None;
  bool             buttonInstall = false,
                   buttonPending = false;

  if (  pApp->store == app_record_s::Store::Steam  && // Expose installer for those games with better specific mods
     (//pApp->id == 405900       || // Disgaea PC
        pApp->id == 359870       || // FFX/X-2 HD Remaster
      //pApp->id == 578330       || // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
        pApp->id == 429660       || // Tales of Berseria
        pApp->id == 372360       || // Tales of Symphonia
        pApp->id == 738540     //|| // Tales of Vesperia DE
      //pApp->id == 351970          // Tales of Zestiria
      ) && ! _cache.injection.type._Equal ("Local"))
  {
    buttonLabel   = ICON_FA_DOWNLOAD "  Install Mod";
    buttonInstall = true;

    uint32_t _modAppId = modAppId.load();

    if (_modAppId == pApp->id)
    {
      if (modInstalling.load())
      {
        buttonLabel = "Installing...";
        buttonPending = true;
      }
      else if (modDownloading.load())
      {
        buttonLabel = "Downloading...";
        buttonPending = true;
      }
    }
    else if (_modAppId > 0) {
      buttonLabel   = "Pending...";
      buttonPending = true;
    }
  }

  if (pApp->_status.running || pApp->_status.updating)
  {
    buttonLabel = (  pApp->_status.running) ? "Running..." : "Updating...";
    buttonFlags = ImGuiButtonFlags_Disabled;
    ImGui::PushStyleColor (ImGuiCol_Button, ImGui::GetStyleColorVec4 (ImGuiCol_Button) * ImVec4 (0.75f, 0.75f, 0.75f, 1.0f));
  }

  // Disable the button for the injection service types if the servlets are missing
  if ( ! _inject.bHasServlet && ! _cache.injection.type._Equal ("Local") || buttonPending )
    SKIF_ImGui_PushDisableState ( );

  if (buttonInstall && 
      ImGui::ButtonEx (
              buttonLabel.c_str (),
                  ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                            50.0f * SKIF_ImGui_GlobalDPIScale ), buttonFlags ))
  {
    static uint32_t appid;
    appid = pApp->id;

    // We're going to asynchronously download the mod installer on this thread
    if (! modDownloading.load())
    {
      _beginthread ([](void*)->void
      {
        modDownloading.store (true);

        SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibGameModWorker");

        CoInitializeEx (nullptr, 0x0);

        PLOG_INFO << "SKIF_LibGameModWorker thread started!";

        int _appid = appid;
        modAppId.store (_appid);

        static const std::wstring root = SK_FormatStringW (LR"(%ws\Version\)", _path_cache.specialk_userdata);

        // Create any missing directories
        std::error_code ec;
        if (! std::filesystem::exists (            root, ec))
              std::filesystem::create_directories (root, ec);
      
        static std::wstring download, filename, path;

        switch (_appid)
        {
        case 359870: // FFX/X-2 HD Remaster
          filename = L"SpecialK_UnX.exe";
          download = L"https://sk-data.special-k.info/UnX/SpecialK_UnX.exe";
          break;
        case 429660: // Tales of Berseria
          filename = L"SpecialK_TBFix.exe";
          download = L"https://sk-data.special-k.info/TBFix/SpecialK_TBFix.exe";
          break;
        case 372360: // Tales of Symphonia
          filename = L"SpecialK_TSFix.exe";
          download = L"https://sk-data.special-k.info/TSFix/SpecialK_TSFix.exe";
          break;
        case 738540: // Tales of Vesperia DE
          filename = L"SpecialK_TVFix.exe";
          download = L"https://sk-data.special-k.info/TVFix/SpecialK_TVFix.exe";
          break;
        default:
          PLOG_ERROR << "Unknown app id: " << _appid;
          break;
        }

        path = root + filename;

        if (! filename.empty() && ! download.empty())
        {
          // Download the installer if it does not exist
          if (! PathFileExists (path.c_str()))
          {
            PLOG_INFO << "Downloading installer: " << download;
            SKIF_Util_GetWebResource (download, path);
          }
        
          modInstalling.store (true);

          // Note that any new process will inherit SKIF's environment variables
          if (_registry._LoadedSteamOverlay)
            SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", NULL);
  
          SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = path.c_str();
            sexi.lpParameters = NULL;
            sexi.lpDirectory  = NULL;
            sexi.nShow        = SW_SHOWNORMAL;
            sexi.fMask        = SEE_MASK_NOCLOSEPROCESS | // We need the PID of the process that gets started
                                SEE_MASK_NOASYNC        | // Never async since we execute in short-lived child thread
                                SEE_MASK_NOZONECHECKS;    // No zone check needs to be performed

          // Attempt to run the downloaded installer,
          //   and delete it on failure
          if (! ShellExecuteExW (&sexi))
          {
            PLOG_ERROR << "Something went wrong: "   << SKIF_Util_GetErrorAsWStr ( );
            PLOG_ERROR_IF (DeleteFile(path.c_str())) << "The downloaded installer has been removed!";
          }

          if (_registry._LoadedSteamOverlay)
            SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");
          
          // If the process was started successfully, wait for it to close down...
          if (sexi.hInstApp  > (HINSTANCE)32 &&
              sexi.hProcess != NULL)
          {
            WaitForSingleObject (sexi.hProcess, INFINITE); // == WAIT_OBJECT_0
            CloseHandle         (sexi.hProcess);
          }

          modInstalling.store (false);

          PLOG_INFO << "Finished installing a mod asynchronously...";
        }
        
        modAppId.store (0);
        modDownloading.store (false);

        // Force a refresh when the game icons have finished being streamed
        //PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

        PLOG_INFO << "SKIF_LibGameModWorker thread stopped!";
      }, 0x0, NULL);
    }
  }

  if (buttonPending)
  {
    SKIF_ImGui_PopDisableState ( );
    SKIF_ImGui_SetHoverTip ("Please finish the ongoing mod installation first.");
  }

  // This captures two events -- launching through context menu + large button
  if (( ! buttonInstall &&
      ImGui::ButtonEx (
              buttonLabel.c_str (),
                  ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                            50.0f * SKIF_ImGui_GlobalDPIScale ), buttonFlags ))
        ||
    clickedGameLaunch
        ||
    clickedGameLaunchWoSK )
  {

    if ( pApp->store != app_record_s::Store::Steam && pApp->store != app_record_s::Store::Epic &&
         pApp->launch_configs[0].getExecutableFullPath(pApp->id).find(L"InvalidPath") != std::wstring::npos )
    {
      confirmPopupText = "Could not launch game due to missing executable:\n\n" + SK_WideCharToUTF8(pApp->launch_configs[0].getExecutableFullPath(pApp->id, false));
      ConfirmPopup     = PopupState_Open;
    }

    else {
      bool localInjection = (_cache.injection.type._Equal ("Local"));
      bool usingSK        = localInjection;

      // Check if the injection service should be used
      if (! usingSK)
      {
        std::string fullPath     = SK_WideCharToUTF8(pApp->launch_configs[0].getExecutableFullPath (pApp->id));
        bool isLocalBlacklisted  = pApp->launch_configs[0].isBlacklisted (pApp->id),
             isGlobalBlacklisted = _inject._TestUserList (fullPath.c_str (), false);

        usingSK = ! clickedGameLaunchWoSK &&
                  ! isLocalBlacklisted    &&
                  ! isGlobalBlacklisted;

        if (usingSK)
        {
          // Whitelist the path if it haven't been already
          if (pApp->store == app_record_s::Store::Xbox)
          {
            if (! _inject._TestUserList (SK_WideCharToUTF8 (pApp->Xbox_AppDirectory).c_str(), true))
            {
              if (_inject.WhitelistPattern (pApp->Xbox_PackageName))
                _inject.SaveWhitelist ( );
            }
          }

          else
          {
            if (_inject.WhitelistPath (fullPath))
              _inject.SaveWhitelist ( );
          }

          // Disable the first service notification
          if (_registry.bMinimizeOnGameLaunch)
            _registry._SuppressServiceNotification = true;
        }

        // Kickstart service if it is currently not running
        if (! _inject.bCurrentState && usingSK )
          _inject._StartStopInject (false, true, pApp->launch_configs[0].isElevated(pApp->id));

        // Stop the service if the user attempts to launch without SK
        else if ( clickedGameLaunchWoSK && _inject.bCurrentState )
          _inject._StartStopInject (true);
      }

      // Create the injection acknowledge events in case of a local injection
      else {
        _inject.SetInjectAckEx     (true);
        _inject.SetInjectExitAckEx (true);
      }

      // Launch GOG Galaxy game
      if (pApp->store == app_record_s::Store::GOG && GOGGalaxy_Installed && _registry.bPreferGOGGalaxyLaunch && ! clickedGameLaunch && ! clickedGameLaunchWoSK)
      {
        extern std::wstring GOGGalaxy_Path;

        // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

        std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

        SKIF_Util_OpenURI (GOGGalaxy_Path, SW_SHOWDEFAULT, L"OPEN", launchOptions.c_str());

        SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), GOGGalaxy_Path, launchOptions, L"", pApp->launch_configs[0].getExecutableFullPath (pApp->id), (! localInjection && usingSK));
      }

      // Launch Epic game
      else if (pApp->store == app_record_s::Store::Epic)
      {
        // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

        std::wstring launchOptions = SK_FormatStringW(LR"(com.epicgames.launcher://apps/%ws?action=launch&silent=true)", pApp->launch_configs[0].launch_options.c_str());
        SKIF_Util_OpenURI (launchOptions);

        SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), L"", launchOptions, L"", pApp->launch_configs[0].getExecutableFullPath (pApp->id), (! localInjection && usingSK));
      }

      // Launch Steam game
      else if (pApp->store == app_record_s::Store::Steam)
      {
        bool launchDecision = true;

        // Check localconfig.vdf if user is attempting to launch without Special K 
        if (clickedGameLaunchWoSK)
        {
          std::string
              launch_options = SKIF_Steam_GetLaunchOptions (pApp->id, SKIF_Steam_GetCurrentUser (true));
          if (launch_options.size() > 0)
          {
            std::string launch_options_lower =
              SKIF_Util_ToLower (launch_options);

            if (launch_options_lower.find("skif ") != std::string::npos)
            {
              launchDecision = false;

              // Escape any percent signs (%)
              for (auto pos  = launch_options.find ('%');          // Find the first occurence
                        pos != std::string::npos;                  // Validate we're still in the string
                        launch_options.insert      (pos, R"(%)"),  // Escape the character
                        pos  = launch_options.find ('%', pos + 2)) // Find the next occurence
              { }
                          
              confirmPopupText = "Could not launch game due to conflicting launch options in Steam:\n"
                                  "\n"
                               +  launch_options + "\n"
                                  "\n"
                                  "Please change the launch options in Steam before trying again.";
              ConfirmPopup     = PopupState_Open;

              PLOG_WARNING << "Steam game " << pApp->id << " (" << pApp->names.normal << ") was unable to launch due to a conflict with the launch option of Steam: " << launch_options;
            }
          }
        }

        if (launchDecision)
        {
          std::wstring launchOptions = SK_FormatStringW (LR"(steam://run/%d)", pApp->id);
          SKIF_Util_OpenURI (launchOptions);
          pApp->_status.invalidate();

          SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), L"", launchOptions, L"", pApp->launch_configs[0].getExecutableFullPath (pApp->id), (! localInjection && usingSK));
        }
      }
       
      // SKIF Custom, GOG without Galaxy, Xbox
      else
      {
        std::wstring wszPath = (pApp->store == app_record_s::Store::Xbox)
                              ? pApp->launch_configs[0].executable_helper
                              : pApp->launch_configs[0].getExecutableFullPath (pApp->id);

        // We need to use a proxy variable since we might remove a substring of the launch options
        std::wstring cmdLine      = pApp->launch_configs[0].launch_options;

        // Transform to lowercase
        std::wstring cmdLineLower = SKIF_Util_ToLowerW (pApp->launch_configs[0].launch_options);

        // Extract the SKIF_SteamAppID cmd line argument
        const std::wstring argSKIF_SteamAppID = L"skif_steamappid=";
        size_t posSKIF_SteamAppID_start       = cmdLineLower.find (argSKIF_SteamAppID);
        std::wstring steamAppId               = L"";

        if (posSKIF_SteamAppID_start != std::wstring::npos)
        {
          size_t
            posSKIF_SteamAppID_end    = cmdLineLower.find (L" ", posSKIF_SteamAppID_start);

          if (posSKIF_SteamAppID_end == std::wstring::npos)
            posSKIF_SteamAppID_end    = cmdLineLower.length ( );

          // Length of the substring to remove
          posSKIF_SteamAppID_end -= posSKIF_SteamAppID_start;

          steamAppId = cmdLineLower.substr (posSKIF_SteamAppID_start + argSKIF_SteamAppID.length ( ), posSKIF_SteamAppID_end);

          // Remove substring from the proxy variable
          cmdLine.erase (posSKIF_SteamAppID_start, posSKIF_SteamAppID_end);
        }

        if (! steamAppId.empty ( ))
        {
          PLOG_INFO << "Using Steam App ID : " << steamAppId;
          SetEnvironmentVariable (L"SteamAppId",  steamAppId.c_str());
        }

        //  Synchronous - Required for the SetEnvironmentVariable() calls to be respected
        SKIF_Util_OpenURI (wszPath, SW_SHOWDEFAULT, L"OPEN", cmdLine.c_str(), pApp->launch_configs[0].working_dir.c_str(), SEE_MASK_NOASYNC | SEE_MASK_NOZONECHECKS);

        if (! steamAppId.empty ( ))
          SetEnvironmentVariable (L"SteamAppId",  NULL);

        SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), wszPath, pApp->launch_configs[0].launch_options, pApp->launch_configs[0].working_dir.c_str(), pApp->launch_configs[0].getExecutableFullPath (pApp->id), (! localInjection && usingSK));
      }
          
      // Fallback for minimizing SKIF when not using SK if configured as such
      if (_registry.bMinimizeOnGameLaunch && ! usingSK && SKIF_ImGui_hWnd != NULL)
        ShowWindowAsync (SKIF_ImGui_hWnd, SW_SHOWMINNOACTIVE);
    }

    clickedGameLaunch = clickedGameLaunchWoSK = false;
  }
      
  // Disable the button for the injection service types if the servlets are missing
  if ( ! _inject.bHasServlet && !_cache.injection.type._Equal ("Local") )
    SKIF_ImGui_PopDisableState  ( );

  if (pApp->_status.running || pApp->_status.updating)
    ImGui::PopStyleColor ( );

  if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
      ! openedGameContextMenu)
  {
    openedGameContextMenu = true;
  }

  ImGui::EndChildFrame ();
}

#pragma endregion


#pragma region UpdateInjectionStrategy

void
UpdateInjectionStrategy (app_record_s* pApp)
{
  if (pApp == nullptr || pApp->id == SKIF_STEAM_APPID)
    return;

  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  
  // Handle Steam games
  if (pApp->store == app_record_s::Store::Steam)
  {
    pApp->specialk.injection =
      SKIF_InstallUtils_GetInjectionStrategy (pApp->id);

    // Scan Special K configuration, etc.
    if (pApp->specialk.profile_dir.empty ())
    {
      pApp->specialk.profile_dir = pApp->specialk.injection.config.dir;

      if (! pApp->specialk.profile_dir.empty ())
      {
        SK_VirtualFS profile_vfs;

        int files =
          SK_VFS_ScanTree ( profile_vfs,
                              pApp->specialk.profile_dir.data (), 2 );

        UNREFERENCED_PARAMETER (files);
      }
    }
  }
  
  // Handle GOG, Epic, and SKIF Custom games
  else {
    DWORD dwBinaryType = MAXDWORD;
    if ( GetBinaryTypeW (pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str (), &dwBinaryType) )
    {
      if (dwBinaryType == SCS_32BIT_BINARY)
        pApp->specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;
      else if (dwBinaryType == SCS_64BIT_BINARY)
        pApp->specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
    }

    std::wstring test_paths[] = { 
      pApp->launch_configs[0].getExecutableDir(pApp->id, false),
      pApp->launch_configs[0].working_dir
    };

    if (test_paths[0] == test_paths[1])
      test_paths[1] = L"";

    struct {
      InjectionBitness bitness;
      InjectionPoint   entry_pt;
      std::wstring     name;
      std::wstring     path;
    } test_dlls [] = {
      { pApp->specialk.injection.injection.bitness, InjectionPoint::D3D9,    L"d3d9",     L"" },
      { pApp->specialk.injection.injection.bitness, InjectionPoint::DXGI,    L"dxgi",     L"" },
      { pApp->specialk.injection.injection.bitness, InjectionPoint::D3D11,   L"d3d11",    L"" },
      { pApp->specialk.injection.injection.bitness, InjectionPoint::OpenGL,  L"OpenGL32", L"" },
      { pApp->specialk.injection.injection.bitness, InjectionPoint::DInput8, L"dinput8",  L"" }
    };

    // Assume Global 32-bit if we don't know otherwise
    bool bIs64Bit =
      ( pApp->specialk.injection.injection.bitness ==
                        InjectionBitness::SixtyFour );

    pApp->specialk.injection.config.type =
      ConfigType::Centralized;

    wchar_t                 wszPathToSelf [MAX_PATH] = { };
    GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
    PathRemoveFileSpecW (   wszPathToSelf);
    PathAppendW         (   wszPathToSelf,
                              bIs64Bit ? L"SpecialK64.dll"
                                       : L"SpecialK32.dll" );

    pApp->specialk.injection.injection.dll_path = wszPathToSelf;
    pApp->specialk.injection.injection.dll_ver  = 
                              bIs64Bit ? _inject.SKVer64
                                       : _inject.SKVer32;
    //SKIF_GetSpecialKDLLVersion (       wszPathToSelf);

    pApp->specialk.injection.injection.type =
      InjectionType::Global;
    pApp->specialk.injection.injection.entry_pt =
      InjectionPoint::CBTHook;
    pApp->specialk.injection.config.file =
      L"SpecialK.ini";

    bool breakOuterLoop = false;
    for ( auto& test_path : test_paths)
    {
      if (test_path.empty())
        continue;

      for ( auto& dll : test_dlls )
      {
        dll.path =
          ( test_path + LR"(\)" ) +
            ( dll.name + L".dll" );

        if (PathFileExistsW (dll.path.c_str ()))
        {
          std::wstring dll_ver =
            SKIF_GetSpecialKDLLVersion (dll.path.c_str ());

          if (! dll_ver.empty ())
          {
            pApp->specialk.injection.injection = {
              dll.bitness,
              dll.entry_pt, InjectionType::Local,
              dll.path,     dll_ver
            };

            if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
            {
              pApp->specialk.injection.config.type =
                ConfigType::Centralized;
            }

            else
            {
              pApp->specialk.injection.config = {
                ConfigType::Localized,
                test_path
              };
            }

            pApp->specialk.injection.config.file =
              dll.name + L".ini";

            breakOuterLoop = true;
            break;
          }

          else
            PLOG_VERBOSE << "Local wrapper was not detected as being Special K: " << dll.path;
        }
      }

      if (breakOuterLoop)
        break;
    }

    if (pApp->specialk.injection.config.type == ConfigType::Centralized)
    {
      pApp->specialk.injection.config.dir =
        SK_FormatStringW(LR"(%ws\Profiles\%ws)",
          _path_cache.specialk_userdata,
          pApp->specialk.profile_dir.c_str());
    }

    pApp->specialk.injection.config.file =
      ( pApp->specialk.injection.config.dir + LR"(\)" ) +
        pApp->specialk.injection.config.file;
  }

}

#pragma endregion



void
SKIF_UI_Tab_DrawLibrary (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_Lib_SummaryCache& _cache      = SKIF_Lib_SummaryCache::GetInstance ( );
  
  static SKIF_DirectoryWatch SKIF_Epic_ManifestWatch;

//static CComPtr <ID3D11Texture2D>          pTex2D;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV;
//static ImVec2                             vecTex2D;

  static ImVec2 vecCoverUv0 = ImVec2 (0, 0), 
                vecCoverUv1 = ImVec2 (1, 1);

  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };

  // Initialize the Steam appinfo.vdf Reader
  if (_registry.bLibrarySteam)
  {
    SK_RunOnce (
      appinfo = std::make_unique <skValveDataFile> (std::wstring(_path_cache.steam_install) + LR"(\appcache\appinfo.vdf)");
    );
  }

#if 0
  SKIF_GamesCollection& _games              = SKIF_GamesCollection::GetInstance  ( );

  // Always read from the last written index
  int nowReading = _games.snapshot_idx_written.load ( );
  _games.snapshot_idx_reading.store (nowReading);

  if (RepopulateGames)
    _games.RefreshGames ( );

  std::vector <std::unique_ptr<app_generic_s>>* apps_new =
    _games.GetGames     ( );

  if (apps_new != nullptr && ! apps_new->empty() && RepopulateGames)
  {
    PLOG_VERBOSE << "New library backend discovered the following games:";
    for (auto const& app : *apps_new) {
      PLOG_VERBOSE << app->names.normal;
    }
  }
#endif

  auto& io =
    ImGui::GetIO ();

  //static volatile LONG icon_thread  = 1;
  //static volatile LONG need_sort    = 0;
  
  bool        sort_changed = false;
  static bool update       = true;
  static bool populated    = false;

  auto _SortApps = [&](void) -> void
  {
    std::sort ( apps.begin (),
                apps.end   (),
      []( const std::pair <std::string, app_record_s>& a,
          const std::pair <std::string, app_record_s>& b ) -> int
      {
        return a.second.names.all_upper_alnum.compare(
               b.second.names.all_upper_alnum
        ) < 0;
      }
    );

    sort_changed = true;
    PLOG_INFO << "Apps were sorted!";
  };

  struct {
    uint32_t            appid = SKIF_STEAM_APPID;
    app_record_s::Store store = app_record_s::Store::Steam;
    SKIF_DirectoryWatch dir_watch;
    bool                reset_to_skif = true;

    void reset ()
    {
      appid = (reset_to_skif) ? SKIF_STEAM_APPID           : 0;
      store = (reset_to_skif) ? app_record_s::Store::Steam : app_record_s::Store::Unspecified;
      
      if (dir_watch._hChangeNotification != INVALID_HANDLE_VALUE)
        dir_watch.reset();
    }
  } static selection, item_clicked, lastCover;

  // We need to ensure the lastCover isn't set to SKIF's app ID as that would prevent the cover from loading on launch
  SK_RunOnce (lastCover.reset_to_skif = false; lastCover.reset());

  if (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ))
  {
    // Temporarily disabled since this gets triggered on game launch/shutdown as well...
    // And generally also breaks SKIF's library view on occasion
    if (_registry.bLibrarySteam && SKIF_Steam_isLibrariesSignaled ())
      RepopulateGames = true;

    if (_registry.bLibraryEpic && SKIF_Epic_ManifestWatch.isSignaled (SKIF_Epic_AppDataPath, true))
      RepopulateGames = true;
    
    if (_registry.bLibraryGOG)
    {
      if (SKIF_GOG_hasInstalledGamesChanged ( ))
        RepopulateGames = true;

      if (SKIF_GOG_hasGalaxySettingsChanged ( ))
        SKIF_GOG_UpdateGalaxyUserID ( );
    }

    if (_registry.bLibraryXbox && SKIF_Xbox_hasInstalledGamesChanged ( ))
      RepopulateGames = true;
  }

  // We cannot manipulate the apps array while the game worker thread is running
  if (RepopulateGames && ! gameWorkerRunning.load())
  {
    RepopulateGames = false;
    gameWorkerRunning.store(true);

    // Clear cached lists
    apps.clear   ();

    // Reset selection to Special K, but only if set to something else than -1
    if (selection.appid != 0)
      selection.reset();

    // Needed as otherwise SKIF would not reload the cover, which would affect
    //   how SKIF handles custom/managed covers and what options are available.
    lastCover.reset();

    update    = true;

    populated = false;
  }

  if (! populated)
  {
    //InterlockedExchange (&icon_thread, 1);

    PLOG_INFO << "Populating library list...";
    
    apps      = SKIF_Steam_GetInstalledAppIDs ();
    
    // Refresh the current Steam user
    if (_registry.bLibrarySteam)
      SKIF_Steam_GetCurrentUser (true);

    // Preload all appinfo.vdf data
    //PLOG_DEBUG << "Loading appinfo.vdf data...";
    for (auto& app : apps)
    {
      if (app.second.id == SKIF_STEAM_APPID)
        SKIF_STEAM_OWNER = true;

      //if (appinfo != nullptr)
      //    appinfo->getAppInfo ( app.second.id );
    }
    //PLOG_DEBUG << "Finished loading appinfo.vdf data!";

    if ( ! SKIF_STEAM_OWNER )
    {
      app_record_s SKIF_record (SKIF_STEAM_APPID);

      SKIF_record.id              = SKIF_STEAM_APPID;
      SKIF_record.names.normal    = "Special K";
      SKIF_record.names.all_upper = "SPECIAL K";
      SKIF_record.install_dir     = _path_cache.specialk_install;
      SKIF_record.store           = app_record_s::Store::Steam;
      SKIF_record.store_utf8      = "Steam";
      SKIF_record.ImGuiLabelAndID = SK_FormatString("%s###%i-%i", SKIF_record.names.normal.c_str(), (int)SKIF_record.store, SKIF_record.id);
      SKIF_record.ImGuiPushID     = SK_FormatString("%i-%i", (int)SKIF_record.store, SKIF_record.id);

      std::pair <std::string, app_record_s>
        SKIF ( "Special K", SKIF_record );

      apps.emplace_back (SKIF);
    }

    // Load GOG titles from registry
    if (_registry.bLibraryGOG)
      SKIF_GOG_GetInstalledAppIDs (&apps);

    // Load Epic titles from disk
    if (_registry.bLibraryEpic)
      SKIF_Epic_GetInstalledAppIDs (&apps);
    
    if (_registry.bLibraryXbox)
      SKIF_Xbox_GetInstalledAppIDs (&apps);

    // Load custom SKIF titles from registry
    SKIF_GetCustomAppIDs (&apps);

    PLOG_INFO << "Loading game names synchronously...";

    // Clear any existing trie
    labels = Trie { };

    // Process the list of apps -- prepare their names, keyboard search, as well as remove any uninstalled entries
    for ( auto& app : apps )
    {
      //PLOG_DEBUG << "Working on " << app.second.id << " (" << app.second.store_utf8 << ")";

      // Special handling for non-Steam owners of Special K / SKIF
      if ( app.second.id == SKIF_STEAM_APPID )
        app.first = "Special K";

      // Regular handling for the remaining Steam games
      else if (app.second.store == app_record_s::Store::Steam) {
        app.first.clear ();

        app.second._status.refresh (&app.second);
      }

      // Only bother opening the application manifest
      //   and looking for a name if the client claims
      //     the app is installed.
      if (app.second._status.installed)
      {
        if (! app.second.names.normal.empty ())
        {
          app.first = app.second.names.normal;
        }

        // Some games have an install state but no name,
        //   for those we have to consult the app manifest
        else if (app.second.store == app_record_s::Store::Steam)
        {
          app.first =
            SK_UseManifestToGetAppName (
                          app.second.id );
        }

        // Corrupted app manifest / not known to Steam client; SKIP!
        if (app.first.empty ())
        {
          PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has no name; ignoring!";

          app.second.id = 0;
          continue;
        }

        /*
        if (app.second.launch_configs.size() == 0)
        {
          PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has no launch config; ignoring!";

          app.second.id = 0;
          continue;
        }
        */

        std::string original_name = app.first;

        // Some games use weird Unicode character combos that ImGui can't handle,
        //  so let's replace those with the normal ones.

        // Replace RIGHT SINGLE QUOTATION MARK (Code: 2019 | UTF-8: E2 80 99)
        //  with a APOSTROPHE (Code: 0027 | UTF-8: 27)
        app.first = std::regex_replace(app.first, std::regex("\xE2\x80\x99"), "\x27");

        // Replace LATIN SMALL LETTER O (Code: 006F | UTF-8: 6F) and COMBINING DIAERESIS (Code: 0308 | UTF-8: CC 88)
        //  with a LATIN SMALL LETTER O WITH DIAERESIS (Code: 00F6 | UTF-8: C3 B6)
        app.first = std::regex_replace(app.first, std::regex("\x6F\xCC\x88"), "\xC3\xB6");

        // Strip game names from special symbols (disabled due to breaking some Chinese characters)
        //const char* chars = (const char *)u8"\u00A9\u00AE\u2122"; // Copyright (c), Registered (R), Trademark (TM)
        //for (unsigned int i = 0; i < strlen(chars); ++i)
          //app.first.erase(std::remove(app.first.begin(), app.first.end(), chars[i]), app.first.end());

        // Remove COPYRIGHT SIGN (Code: 00A9 | UTF-8: C2 A9)
        app.first = std::regex_replace(app.first, std::regex("\xC2\xA9"), "");

        // Remove REGISTERED SIGN (Code: 00AE | UTF-8: C2 AE)
        app.first = std::regex_replace(app.first, std::regex("\xC2\xAE"), "");

        // Remove TRADE MARK SIGN (Code: 2122 | UTF-8: E2 84 A2)
        app.first = std::regex_replace(app.first, std::regex("\xE2\x84\xA2"), "");

        if (original_name != app.first)
        {
          PLOG_DEBUG << "Game title was changed:";
          PLOG_DEBUG << "Old: " << SK_UTF8ToWideChar(original_name.c_str()) << " (" << original_name << ")";
          PLOG_DEBUG << "New: " << SK_UTF8ToWideChar(app.first.c_str())     << " (" << app.first     << ")";
        }

        // Strip any remaining null terminators
        app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

        // Trim leftover spaces
        app.first.erase(app.first.begin(), std::find_if(app.first.begin(), app.first.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        app.first.erase(std::find_if(app.first.rbegin(), app.first.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), app.first.end());
          
        // Update ImGuiLabelAndID and ImGuiPushID
        app.second.ImGuiLabelAndID = SK_FormatString("%s###%i-%i", app.first.c_str(), (int)app.second.store, app.second.id);
        app.second.ImGuiPushID     = SK_FormatString("%i-%i", (int)app.second.store, app.second.id);
      }

      // Check if install folder exists (but not for SKIF)
      if (app.second.id    != SKIF_STEAM_APPID          &&
          app.second.store != app_record_s::Store::Xbox )
      {
        std::wstring install_dir;

        if (app.second.store == app_record_s::Store::Steam)
          install_dir = SK_UseManifestToGetInstallDir(app.second.id);
        else
          install_dir = app.second.install_dir;
          
        if (! PathFileExists(install_dir.c_str()))
        {
          PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store_utf8 << ") has non-existent install folder; ignoring!";

          app.second.id = 0;
          continue;
        }
      }

      // Prepare for the keyboard hint / search/filter functionality
      if ( app.second._status.installed || app.second.id == SKIF_STEAM_APPID)
      {
        InsertTrieKey (&app, &labels);
      }
    }

    PLOG_INFO << "Finished loading game names synchronously...";
    
    _SortApps ( );

    // Set to last selected if it can be found
    // Only do this AFTER we have cleared the list of apps from uninstalled games
    if (selection.appid == SKIF_STEAM_APPID)
    {
      for (auto& app : apps)
      {
        if (app.second.id    ==      _registry.iLastSelectedGame &&
            app.second.store == (app_record_s::Store)_registry.iLastSelectedStore)
        {
          PLOG_VERBOSE << "Selected app ID " << app.second.id << " from platform ID " << (int)app.second.store << ".";
          selection.appid        = app.second.id;
          selection.store        = app.second.store;
          manual_selection.id    = selection.appid;
          manual_selection.store = selection.store;
          update = true;
        }
      }
    }

    // DEBUG ONLY: Causes SKIF to take ages to start up as it preloads
    //   the injection strategy of all games (especially Steam games and the appinfo.vdf file)
    //for (auto& app : apps)
    //  UpdateInjectionStrategy (&app.second);

    PLOG_INFO << "Finished populating the library list.";

    // We're going to stream game icons asynchronously on this thread
    _beginthread ([](void*)->void
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibRefreshWorker");

      CoInitializeEx (nullptr, 0x0);

      PLOG_INFO << "SKIF_LibRefreshWorker thread started!";
      
      PLOG_INFO << "Loading the embedded Patreon texture...";
      ImVec2 dontCare1, dontCare2;
      if (pPatTexSRV.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID,    pPatTexSRV, L"(patreon.png)",   dontCare1, dontCare2);
      if (pSKLogoTexSRV.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Cover,   SKIF_STEAM_APPID, pSKLogoTexSRV, L"(sk_boxart.png)", dontCare1, dontCare2);

      // FIX: This causes the whole apps vector to be resorted by the main thread
      //   which then causes textures being loaded below to end up being assigned to the wrong game
      //InterlockedExchange (&need_sort, 1);

      // Force a refresh when the game names have finished being streamed
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);
      
      // Load icons last
      for ( auto& app : apps )
      {
        if (app.second.id == 0)
          continue;

        std::wstring load_str;
        
        if (app.second.id == SKIF_STEAM_APPID) // SKIF
          load_str = L"_icon.jpg";
        else  if (app.second.store == app_record_s::Store::Other) // SKIF Custom
          load_str = L"icon";
        else  if (app.second.store == app_record_s::Store::Epic)  // Epic
          load_str = L"icon";
        else  if (app.second.store == app_record_s::Store::GOG)   // GOG
          load_str = app.second.install_dir + L"\\goggame-" + std::to_wstring(app.second.id) + L".ico";
        else if (app.second.store  == app_record_s::Store::Steam)  // STEAM
          load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i_icon.jpg)", _path_cache.steam_install, app.second.id); //L"_icon.jpg"

        LoadLibraryTexture ( LibraryTexture::Icon,
                               app.second.id,
                                 app.second.tex_icon.texture,
                                   load_str,
                                     dontCare1,
                                       dontCare2,
                                         &app.second );
      }

      PLOG_INFO << "Finished streaming game icons asynchronously...";
      
      //InterlockedExchange (&icon_thread, 0);
      gameWorkerRunning.store(false);

      // Force a refresh when the game icons have finished being streamed
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

      PLOG_INFO << "SKIF_LibRefreshWorker thread stopped!";
    }, 0x0, NULL);

    populated = true;
  }

  extern bool  coverFadeActive;
  static int   tmp_iDimCovers = _registry.iDimCovers;
  
  static
    app_record_s* pApp = nullptr;

  // Ensure pApp points to the current selected game
  for (auto& app : apps)
    if (app.second.id == selection.appid && app.second.store == selection.store)
      pApp = &app.second;

  // Apply changes when the selected game changes
  if (update)
  {
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;
    
    if (pApp != nullptr)
    {
      if (  pApp->install_dir != selection.dir_watch._path)
        selection.dir_watch.reset ( );

      if (! pApp->install_dir.empty())
        selection.dir_watch.isSignaled (pApp->install_dir, true);

      //UpdateInjectionStrategy (pApp);
      //_cache.Refresh (pApp);
    }
  }

  // Apply changes when the _registry.iDimCovers var has been changed in the Settings tab
  else if (tmp_iDimCovers != _registry.iDimCovers)
  {
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;

    tmp_iDimCovers = _registry.iDimCovers;
  }

  ImGui::BeginGroup    (                                                  );

  static int    queuePosGameCover  = 0;
  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelMissing[] = "Missing cover :(";
  static char   cstrLabelGOGUser[] = "Please sign in to GOG Galaxy to\n"
                                     "allow the cover to be populated :)";

  ImVec2 vecPosCoverImage    = ImGui::GetCursorPos ( );
         vecPosCoverImage.x -= 1.0f * SKIF_ImGui_GlobalDPIScale;

  if (tryingToLoadCover)
  {
    ImGui::SetCursorPos (ImVec2 (
      vecPosCoverImage.x + 300.0F * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelLoading).x / 2,
      vecPosCoverImage.y + 450.0F * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelLoading).y / 2));
    ImGui::TextDisabled (  cstrLabelLoading);
  }
  
  else if (textureLoadQueueLength.load() == queuePosGameCover && pTexSRV.p == nullptr)
  {
    extern std::wstring GOGGalaxy_UserID;
    if (pApp != nullptr && pApp->store == app_record_s::Store::GOG && GOGGalaxy_UserID.empty())
    {
      ImGui::SetCursorPos (ImVec2 (
                  vecPosCoverImage.x + 300.0F * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelGOGUser).x / 2,
                  vecPosCoverImage.y + 450.0F * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelGOGUser).y / 2));
      ImGui::TextDisabled (  cstrLabelGOGUser);
    }
    else {
      ImGui::SetCursorPos (ImVec2 (
                  vecPosCoverImage.x + 300.0F * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).x / 2,
                  vecPosCoverImage.y + 450.0F * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).y / 2));
      ImGui::TextDisabled (  cstrLabelMissing);
    }
  }

  ImGui::SetCursorPos (vecPosCoverImage);

#if 1
  extern bool SKIF_bHDREnabled;

  float fGammaCorrectedTint = 
    ((! SKIF_bHDREnabled && _registry.iSDRMode == 2) || 
     (  SKIF_bHDREnabled && _registry.iHDRMode == 2))
        ? AdjustAlpha (fTint)
        : fTint;
#endif
  
  // Display game cover image
  SKIF_ImGui_OptImage  (pTexSRV.p,
                                                    ImVec2 (600.0F * SKIF_ImGui_GlobalDPIScale,
                                                            900.0F * SKIF_ImGui_GlobalDPIScale),
                                                    vecCoverUv0, // Top Left coordinates
                                                    vecCoverUv1, // Bottom Right coordinates
                                  (_registry.iStyle == 2) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * fAlpha)  : ImVec4 (fTint, fTint, fTint, fAlpha), // Alpha transparency
                                  (_registry.bUIBorders)  ? ImGui::GetStyleColorVec4 (ImGuiCol_Border) : ImVec4 (0.0f, 0.0f, 0.0f, 0.0f)       // Border
  );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ImGui::OpenPopup ("CoverMenu");

  ImGui::SetCursorPos (vecPosCoverImage);

  if (selection.appid == SKIF_STEAM_APPID && selection.store == app_record_s::Store::Steam && pSKLogoTexSRV != nullptr)
  {
    ImGui::Image (pSKLogoTexSRV.p,
                                                    ImVec2 (600.0F * SKIF_ImGui_GlobalDPIScale,
                                                            900.0F * SKIF_ImGui_GlobalDPIScale),
                                                    ImVec2 (0.0f, 0.0f),                // Top Left coordinates
                                                    ImVec2 (1.0f, 1.0f),                // Bottom Right coordinates
                                                    ImVec4 (1.0f,  1.0f,  1.0f, 1.0f),  // Tint for Special K's logo (always full strength)
                                                    ImVec4 (0.0f, 0.0f, 0.0f, 0.0f)     // Border
    );
  }

  // Every >15 ms, increase/decrease the cover fade effect (makes it frame rate independent)
  static DWORD timeLastTick;
  DWORD timeCurr = SKIF_Util_timeGetTime();
  bool isHovered = ImGui::IsItemHovered();
  bool incTick = false;
  extern int startupFadeIn;

  if (startupFadeIn == 0 && pTexSRV.p != nullptr)
    startupFadeIn = 1;

  if (startupFadeIn == 1)
  {
    if (fAlpha < 1.0f && pTexSRV.p != nullptr)
    {
      if (timeCurr - timeLastTick > 15)
      {
        fAlpha += 0.05f;
        incTick = true;
      }
    }

    if (fAlpha >= 1.0f)
      startupFadeIn = 2;
  }

  if (_registry.iDimCovers == 2)
  {
    if (isHovered && fTint < 1.0f)
    {
      if (timeCurr - timeLastTick > 15)
      {
        fTint = fTint + 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }
    else if (! isHovered && fTint > fTintMin)
    {
      if (timeCurr - timeLastTick > 15)
      {
        fTint = fTint - 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }
  }

  // Increment the tick
  if (incTick)
    timeLastTick = timeCurr;

  if (ImGui::BeginPopup ("CoverMenu", ImGuiWindowFlags_NoMove))
  {
    //static
     // app_record_s* pApp = nullptr;

    //for (auto& app : apps)
    //  if (app.second.id == appid)
    //    pApp = &app.second;

    if (pApp != nullptr)
    {
      // Preparations

      bool resetVisible = (pApp->id    != SKIF_STEAM_APPID            ||
                          (pApp->id    != SKIF_STEAM_APPID            && // Ugly check to exclude the "Special K" entry from being set to true
                           pApp->store != app_record_s::Store::Other) ||
                           pApp->tex_cover.isCustom                   ||
                           pApp->tex_cover.isManaged);
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize (  ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE          ).x, ImGui::GetTextLineHeight()));
      if (pApp->tex_cover.isCustom)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE_LEFT         ).x, ImGui::GetTextLineHeight()));
      else if (pApp->tex_cover.isManaged)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UP_RIGHT_FROM_SQUARE).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Change",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Other)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Epic_AppName).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

          if (targetPath != L"")
          {
            std::error_code ec;
            // Create any missing directories
            if (! std::filesystem::exists (            targetPath, ec))
                  std::filesystem::create_directories (targetPath, ec);

            targetPath += L"cover";

            DeleteFile((targetPath + L".jpg").c_str());
            DeleteFile((targetPath + L".png").c_str());

            CopyFile(pwszFilePath, (targetPath + ext).c_str(), false);

            update    = true;
            lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }

      if (resetVisible)
      {
        if (ImGui::Selectable ((pApp->tex_cover.isCustom) ? ((pApp->store == app_record_s::Store::Other) ? "Clear" : "Reset") : "Refresh", dontCare, ImGuiSelectableFlags_SpanAllColumns | ((pApp->tex_cover.isCustom || pApp->tex_cover.isManaged) ? 0x0 : ImGuiSelectableFlags_Disabled)))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Other)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Epic_AppName).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

          if (PathFileExists (targetPath.c_str()))
          {
            std::wstring fileName = (pApp->tex_cover.isCustom) ? L"cover" : L"cover-original";

            bool d1 = DeleteFile ((targetPath + fileName + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + fileName + L".jpg").c_str()),
                 d3 = false,
                 d4 = false;

            // For Xbox titles we also store a fallback cover that we must reset
            if (! pApp->tex_cover.isCustom && pApp->store == app_record_s::Store::Xbox)
            {
              fileName = L"cover-fallback.png";
              d3 = DeleteFile ((targetPath + fileName + L".png").c_str()),
              d4 = DeleteFile ((targetPath + fileName + L".jpg").c_str());
            }

            // If any file was removed
            if (d1 || d2 || d3 || d4)
            {
              update    = true;
              lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
            }
          }
        }
        else
        {
          if (pApp->tex_cover.isCustom || pApp->tex_cover.isManaged)
            SKIF_ImGui_SetMouseCursorHand ( );
          else
            SKIF_ImGui_SetHoverTip        ("Managed by the platform client.");
        }
      }

      ImGui::Separator  (  );

      auto _GetSteamGridDBLink = [&](void) -> std::string
      {
        // Strip (recently added) from the game name
        std::string name = pApp->names.normal;
        try {
          name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
        }
        catch (const std::exception& e)
        {
          UNREFERENCED_PARAMETER(e);
        }

        return (pApp->store == app_record_s::Store::Steam)
                ? SK_FormatString("https://www.steamgriddb.com/steam/%lu/grids", pApp->id)
                : SK_FormatString("https://www.steamgriddb.com/search/grids?term=%s", name.c_str());

      };

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(_GetSteamGridDBLink()).c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (_GetSteamGridDBLink());
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->tex_cover.isCustom)
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE_LEFT
                              );
      else if (pApp->tex_cover.isManaged)
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE
                              );
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (128, 128, 128) : ImColor (128, 128, 128),
                  ICON_FA_ROTATE
                              );

      ImGui::Separator  (  );

      ImGui::TextColored (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_UP_RIGHT_FROM_SQUARE
                            );

    }

    ImGui::EndPopup   (  );
  }

  float fY =
  ImGui::GetCursorPosY (                                                  );

  ImGui::EndGroup             ( );
  ImGui::SameLine             (0.0f, 3.0f * SKIF_ImGui_GlobalDPIScale); // 0.0f, 0.0f
  
  float fZ =
  ImGui::GetCursorPosX (                                                  );

  if (update)
  {
    update      = false;

    if (pApp != nullptr)
    {
      // Ensure we aren't already loading this cover
      if (lastCover.appid != pApp->id || lastCover.store != pApp->store)
      {
        loadCover = true;
        lastCover.appid = pApp->id;
        lastCover.store = pApp->store;
      }
    }
  }

  if (loadCover && populated) // && ! InterlockedExchangeAdd (&icon_thread, 0)) /* && (ImGui::GetCurrentWindowRead()->HiddenFramesCannotSkipItems == 0) */
  { // Load cover first after the window has been shown -- to fix one copy leaking of the cover 
    // 2023-03-24: Is this even needed any longer after fixing the double-loading that was going on?
    // 2023-03-25: Disabled HiddenFramesCannotSkipItems check to see if it's solved.
    // 2023-10-05: Disabled waiting for the icon thread as well
    loadCover = false;

    // Reset variables used to track whether we're still loading a game cover, or if we're missing one
    gameCoverLoading.store (true);
    tryingToLoadCover = true;
    queuePosGameCover = textureLoadQueueLength.load() + 1;

//#define _WRITE_APPID_INI
#ifdef _WRITE_APPID_INI 
    if ( appinfo != nullptr && pApp->store == app_record_s::Store::Steam)
    {
      skValveDataFile::appinfo_s *pAppInfo =
        appinfo->getAppInfo ( pApp->id );

      DBG_UNREFERENCED_LOCAL_VARIABLE (pAppInfo);
    }
#endif

    //PLOG_VERBOSE << "ImGui Frame Counter: " << ImGui::GetFrameCount();

#pragma region SKIF_LibCoverWorker

    // We're going to stream the cover in asynchronously on this thread
    _beginthread ([](void*)->void
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverWorker");

      CoInitializeEx (nullptr, 0x0);

      PLOG_INFO << "SKIF_LibCoverWorker thread started!";
      PLOG_INFO << "Streaming game cover asynchronously...";

      if (pApp == nullptr)
      {
        PLOG_ERROR << "Aborting due to pApp being a nullptr!";
        return;
      }

      app_record_s* _pApp = pApp;

      int queuePos = getTextureLoadQueuePos();
      //PLOG_VERBOSE << "queuePos = " << queuePos;

      static ImVec2 _vecCoverUv0(vecCoverUv0);
      static ImVec2 _vecCoverUv1(vecCoverUv1);
      static CComPtr <ID3D11ShaderResourceView> _pTexSRV (pTexSRV.p);

      // Most textures are pushed to be released by LoadLibraryTexture(),
      //  however the current cover pointer is only updated to the new one
      //   *after* the old cover has been pushed to be released.
      //  
      // This means there's a short thread race where the main thread
      //  can still reference a texture that has already been released.
      //
      // As a result, we preface the whole loading of the new cover texture
      //  by explicitly changing the current cover texture to point to nothing.
      //
      // The only downside is that the cover transition is not seemless;
      //  a black/non-existent cover will be displayed in-between.
      // 
      // But at least SKIF does not run the risk of crashing as often :)
      pTexSRV = nullptr;

      std::wstring load_str;

      // SKIF
      if (_pApp->id == SKIF_STEAM_APPID)
      {
        // No need to change the string in any way
      }

      // SKIF Custom
      else if (_pApp->store == app_record_s::Store::Other)
      {
        load_str = L"cover";
      }

      // GOG
      else if (_pApp->store == app_record_s::Store::GOG)
      {
        load_str = L"*_glx_vertical_cover.webp";
      }

      // Epic
      else if (_pApp->store == app_record_s::Store::Epic)
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\cover-original.jpg)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(_pApp->Epic_AppName).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_Epic_IdentifyAssetNew (_pApp->Epic_CatalogNamespace, _pApp->Epic_CatalogItemId, _pApp->Epic_AppName, _pApp->Epic_DisplayName);
        }
        
        else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_Epic_IdentifyAssetNew (_pApp->Epic_CatalogNamespace, _pApp->Epic_CatalogItemId, _pApp->Epic_AppName, _pApp->Epic_DisplayName);
            }
          }
        }
      }

      // Xbox
      else if (_pApp->store == app_record_s::Store::Xbox)
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\cover-original.png)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(_pApp->Xbox_PackageName).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_Xbox_IdentifyAssetNew (_pApp->Xbox_PackageName, _pApp->Xbox_StoreId);
        }
        
        else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_Xbox_IdentifyAssetNew (_pApp->Xbox_PackageName, _pApp->Xbox_StoreId);
            }
          }
        }
      }

      // Steam
      else if (_pApp->store == app_record_s::Store::Steam)
      {
        std::wstring load_str_2x (
          SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", _path_cache.specialk_userdata, _pApp->id)
        );

        std::error_code ec;
        // Create any missing directories
        if (! std::filesystem::exists (            load_str_2x, ec))
              std::filesystem::create_directories (load_str_2x, ec);

        load_str_2x += L"cover-original.jpg";
        load_str     = _path_cache.steam_install;
        load_str    += LR"(/appcache/librarycache/)" +
          std::to_wstring (_pApp->id)                +
                                  L"_library_600x900.jpg";

        std::wstring load_str_final = load_str;

        // Get UNIX-style time
        time_t ltime;
        time (&ltime);

        std::wstring url  = L"https://steamcdn-a.akamaihd.net/steam/apps/";
                     url += std::to_wstring (_pApp->id);
                     url += L"/library_600x900_2x.jpg";
                     url += L"?t=";
                     url += std::to_wstring (ltime); // Add UNIX-style timestamp to ensure we don't get anything cached

        // If 600x900 exists but 600x900_x2 cannot be found
        if (  PathFileExistsW (load_str.   c_str ()) &&
            ! PathFileExistsW (load_str_2x.c_str ()) )
        {
          // Load the metadata from 600x900, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 300x450, which indicates a real cover,
            //   download the real 600x900 cover and store it in _x2
            if (meta.width  == 300 &&
                meta.height == 450)
            {
              PLOG_DEBUG << "Downloading cover asset: " << url;

              SKIF_Util_GetWebResource (url, load_str_2x);
              load_str_final = load_str_2x;
            }
          }
        }

        // If 600x900_x2 exists, check the last modified time stamps
        else {
          WIN32_FILE_ATTRIBUTE_DATA faX1{}, faX2{};

          // ... but only if low bandwidth mode is disabled
          if (! _registry.bLowBandwidthMode &&
              GetFileAttributesEx (load_str   .c_str (), GetFileExInfoStandard, &faX1) &&
              GetFileAttributesEx (load_str_2x.c_str (), GetFileExInfoStandard, &faX2))
          {
            // If 600x900 has been edited after 600_900_x2,
            //   download new copy of the 600_900_x2 cover
            if (CompareFileTime (&faX1.ftLastWriteTime, &faX2.ftLastWriteTime) == 1)
            {
              DeleteFile (load_str_2x.c_str ());

              PLOG_DEBUG << "Downloading cover asset: " << url;
              SKIF_Util_GetWebResource (url, load_str_2x);
            }
          }
          
          // If 600x900_x2 exists now, load it
          if (PathFileExistsW (load_str_2x.c_str ()))
            load_str_final = load_str_2x;
        }

        load_str = load_str_final;
      }
    
      LoadLibraryTexture ( LibraryTexture::Cover,
                              _pApp->id,
                                _pTexSRV,
                                  load_str,
                                    _vecCoverUv0,
                                      _vecCoverUv1,
                                        _pApp);

      PLOG_VERBOSE << "_pTexSRV = " << _pTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        PLOG_DEBUG << "Texture is live! Swapping it in.";
        vecCoverUv0 = _vecCoverUv0;
        vecCoverUv1 = _vecCoverUv1;
        pTexSRV     = _pTexSRV;

        // Indicate that we have stopped loading the cover
        gameCoverLoading.store (false);

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      }

      else if (_pTexSRV.p != nullptr)
      {
        PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
        extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(_pTexSRV.p);
        _pTexSRV.p = nullptr;
      }

      PLOG_INFO << "Finished streaming game cover asynchronously...";
      PLOG_INFO << "SKIF_LibCoverWorker thread stopped!";

    }, 0x0, NULL);

#pragma endregion
  }

  ImGui::BeginGroup ();

  auto _HandleKeyboardInput = [&](void)
  {
    static auto
      constexpr _text_chars =
        { 'A','B','C','D','E','F','G','H',
          'I','J','K','L','M','N','O','P',
          'Q','R','S','T','U','V','W','X',
          'Y','Z','0','1','2','3','4','5',
          '6','7','8','9',' ','-',':','.' };

    static char test_ [1024] = {      };
    char        out   [2]    = { 0, 0 };
    bool        bText        = false;

    for ( auto c : _text_chars )
    {
      if (io.KeysDownDuration [c] == 0.0f &&
         (c != ' ' || strlen (test_) > 0))
      {
        out [0] = c;
        StrCatA (test_, out);
        bText   = true;
      }
    }

    const  DWORD dwTimeout    = 850UL; // 425UL
    static DWORD dwLastUpdate = SKIF_Util_timeGetTime ();

    struct {
      std::string text          = "";
      app_record_s::Store store = app_record_s::Store::Unspecified;
      uint32_t    app_id        = 0;
      size_t      pos           = 0;
      size_t      len           = 0;
    } static result;

    if (bText)
    {
      dwLastUpdate = SKIF_Util_timeGetTime ();
      
      // Prioritize trie search first
      if (labels.search (test_))
      {
        for (auto& app : apps)
        {
          if (app.second.names.all_upper_alnum.find (test_) == 0)
          {
            result.text   = app.second.names.normal;
            result.store  = app.second.store;
            result.app_id = app.second.id;
            result.pos    = app.second.names.pre_stripped;
            result.len    = strlen (test_);

            // Handle cases where articles are ignored

            // Add one to the length if the regular all_upper cannot find a match
            // as this indicates a stripped character in the found pattern
            if (app.second.names.all_upper.find (test_) != 0)
              result.len++;

            break;
          }
        }
      }

      // Fall back to using free text search when the trie fails us
      else
      {
        //strncpy (test_, result.text.c_str (), 1023);

        for (auto& app : apps)
        {
          size_t 
              pos  = app.second.names.all_upper.find (test_);
          if (pos != std::string::npos ) // == 0 
          {
            result.text   = app.second.names.normal;
            result.store  = app.second.store;
            result.app_id = app.second.id;
            result.pos    = pos;
            result.len    = strlen (test_);

            break;
          }
        }
      }
    }

    if (! result.text.empty ())
    {
      size_t len = 
         (result.len < result.text.length ( ))
        ? result.len : result.text.length ( );

      std::string preSearch  =  result.text.substr (         0,  result.pos),
                  curSearch  =  result.text.substr (result.pos,  len),
                  postSearch = (result.pos + len  < result.text.length ( ))
                             ?  result.text.substr (result.pos + len, std::string::npos)
                             :  "";

      ImGui::OpenPopup         ("###KeyboardHint");

      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

      if (ImGui::BeginPopupModal("###KeyboardHint", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
      {
        if (! preSearch.empty ())
        {
          ImGui::TextDisabled ("%s", preSearch.c_str ());
          ImGui::SameLine     (0.0f, 0.0f);
        }

        ImGui::TextColored ( ImColor::HSV(0.0f, 0.0f, 0.75f), // ImColor(53, 255, 3)
                                "%s", curSearch.c_str ()
        );

        if (! postSearch.empty ())
        {
          ImGui::SameLine     (0.0f, 0.0f);
          ImGui::TextDisabled ("%s", postSearch.c_str ());
        }

        ImGui::EndPopup ( );
      }
    }

    if (                            dwLastUpdate != MAXDWORD &&
         SKIF_Util_timeGetTime () - dwLastUpdate >
                                    dwTimeout )
    {
      if (result.app_id != 0)
      {
        *test_           = '\0';
        dwLastUpdate     = MAXDWORD;
        if (result.app_id != pApp->id || 
            result.store  != pApp->store)
        {
          manual_selection.id    = result.app_id;
          manual_selection.store = result.store;
        }
        result = { };
      }
    }
  };

  if (AddGamePopup    == PopupState_Closed &&
      ModifyGamePopup == PopupState_Closed &&
      RemoveGamePopup == PopupState_Closed &&
      ! io.KeyCtrl)
    _HandleKeyboardInput ();

  // GamesList START
#pragma region GamesList

  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
  ImGui::BeginChild          ( "###AppListInset",
                                ImVec2 ( (_WIDTH - ImGui::GetStyle().WindowPadding.x / 2.0f),
                                         _HEIGHT ), (_registry.bUIBorders),
                                    ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_AlwaysUseWindowPadding ); // | ImGuiWindowFlags_AlwaysUseWindowPadding );
  ImGui::BeginGroup          ( );

  auto _HandleItemSelection = [&](bool isIconMenu = false) ->
  bool
  {
    bool _GamePadRightClick =
      ( ImGui::IsItemFocused ( ) && ( io.NavInputsDownDuration     [ImGuiNavInput_Input] != 0.0f &&
                                      io.NavInputsDownDurationPrev [ImGuiNavInput_Input] == 0.0f &&
                                            ImGui::GetCurrentContext ()->NavInputSource == ImGuiInputSource_NavGamepad ) );

    static constexpr float _LONG_INTERVAL = .15f;

    bool _NavLongActivate =
      ( ImGui::IsItemFocused ( ) && ( io.NavInputsDownDuration     [ImGuiNavInput_Activate] >= _LONG_INTERVAL &&
                                      io.NavInputsDownDurationPrev [ImGuiNavInput_Activate] <= _LONG_INTERVAL ) );

    bool ret =
      ImGui::IsItemActivated (                      ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Left ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Right) ||
      _GamePadRightClick                              ||
      _NavLongActivate;

    // If the item is activated, but not visible, scroll to it
    if (ret)
    {
      if (! ImGui::IsItemVisible    (    )) {
        ImGui::SetScrollHereY       (0.5f);
      }
    }

    if (isIconMenu)
    {
      if ( IconMenu != PopupState_Opened &&
           ImGui::IsItemClicked (ImGuiMouseButton_Right))
           IconMenu = PopupState_Open;
    }

    else {
      if ( ! openedGameContextMenu)
      {
        if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
             _GamePadRightClick                            ||
             _NavLongActivate)
        {
          openedGameContextMenu = true;
        }
      }
    }

    return ret;
  };

  //static constexpr float __ICON_HEIGHT = 32.0f;
  float __ICON_HEIGHT = 32.0f * SKIF_ImGui_GlobalDPIScale;

  bool  dontcare     = false;
  float fScale       =
    ( ImGui::GetTextLineHeightWithSpacing () / __ICON_HEIGHT ),

        _ICON_HEIGHT =
    std::min (1.0f, std::max (0.1f, fScale)) * __ICON_HEIGHT;

  ImVec2 f0 = ImGui::GetCursorPos (  );
    ImGui::Selectable ("###zero", &dontcare, ImGuiSelectableFlags_Disabled);
  ImVec2 f1 = ImGui::GetCursorPos (  );
    ImGui::SameLine (                );
    SKIF_ImGui_OptImage (nullptr, ImVec2 (_ICON_HEIGHT, _ICON_HEIGHT));
  ImVec2 f2 = ImGui::GetCursorPos (  );
    ImGui::SameLine (                );
  ImVec2 f3 = ImGui::GetCursorPos (  );
              ImGui::SetCursorPos (ImVec2 (f2.x, f0.y));

  float fOffset =
    std::floor ( ( std::max (f2.y, f1.y) - std::min (f2.y, f1.y) -
                 ImGui::GetStyle ().ItemSpacing.y / 2.0f ) * SKIF_ImGui_GlobalDPIScale / 2.0f + (1.0f * SKIF_ImGui_GlobalDPIScale) );


  // Start populating the whole list

  for (auto& app : apps)
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second.id == 0)
      continue;
    
    bool selected = (selection.appid == app.second.id &&
                     selection.store == app.second.store);
    bool change   = false;

    app.second._status.refresh (&app.second);

    float fOriginalY =
      ImGui::GetCursorPosY ();


    // Start Icon + Selectable row

    ImGui::BeginGroup      ();
    ImGui::PushID          (app.second.ImGuiPushID.c_str());

    SKIF_ImGui_OptImage    (app.second.tex_icon.texture.p,
                              ImVec2 ( _ICON_HEIGHT,
                                       _ICON_HEIGHT )
                            );

    change |=
      _HandleItemSelection (true);

    ImGui::SameLine        ();

    ImVec4 _color =
      ( app.second._status.updating != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.6f, .6f, 1.f)) :
      ( app.second._status.running  != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.3f, 1.f, 1.f)) :
                    ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Game Title
    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::Selectable      (app.second.ImGuiLabelAndID.c_str(), &selected, ImGuiSelectableFlags_None); // ImGuiSelectableFlags_SpanAvailWidth);
    ImGui::PopStyleColor   (2                    );

    static DWORD    timeClicked = 0;

    // Handle double click on a game row
    if ( ImGui::IsItemHovered ( ) && pApp != nullptr && pApp->id != SKIF_STEAM_APPID && ! pApp->_status.running && ! pApp->_status.updating )
    {
      if ( ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) &&
           timeClicked != 0 && (item_clicked.appid == pApp->id && item_clicked.store == pApp->store))
      {
        timeClicked       = 0;
        item_clicked.reset ( );
        clickedGameLaunch = true;
      }
      
      else if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) )
      {
        timeClicked        = SKIF_Util_timeGetTime ( );
        item_clicked.appid = pApp->id;
        item_clicked.store = pApp->store;
      }

      else if (timeClicked + 500 < SKIF_Util_timeGetTime ( ))
      {
        // Reset after 500 ms
        timeClicked = 0;
        item_clicked.reset ( );
      }
    }

    // Show full title in tooltip if the title spans longer than the width of the Selectable row
    // Old: (app.first.length() > 48)
    // New: ImGui::CalcTextSize  (app.first.c_str()).x > (ImGui::GetContentRegionMax().x - f3.x + f1.x + f1.x)
    if (ImGui::CalcTextSize  (app.first.c_str()).x >= (ImGui::GetContentRegionMax().x - f3.x + f1.x + f1.x))
      SKIF_ImGui_SetHoverTip (app.first);

    // Handle search input
    if (manual_selection.id    == app.second.id &&
        manual_selection.store == app.second.store)
    {
      // Set focus on current row
      ImGui::ActivateItem (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()));
      ImGui::SetFocusID   (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()), ImGui::GetCurrentWindow());

      // Clear stuff
      selection.appid        = 0;
      selection.store        = app_record_s::Store::Unspecified;
      manual_selection.id    = 0;
      manual_selection.store = app_record_s::Store::Unspecified;
      change                 = true;
    }

    change |=
      _HandleItemSelection ();

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::PopID           ();
    ImGui::EndGroup        ();

    // End Icon + Selectable row

    if ( app.second.id    == selection.appid &&
         app.second.store == selection.store &&
                   sort_changed &&
        (! ImGui::IsItemVisible ()) )
    {
      selection.reset ( );
      change = true;
    }

    if (change)
    {
      update = (selection.appid != app.second.id ||
                selection.store != app.second.store);

      selection.appid      = app.second.id;
      selection.store      = app.second.store;
      selected   = true;
      _registry.iLastSelectedGame  = selection.appid;
      _registry.iLastSelectedStore = (int)selection.store;

      if (update)
      {
        timeClicked = SKIF_Util_timeGetTime ( );
        item_clicked.appid = selection.appid;
        item_clicked.store = selection.store;

        app.second._status.invalidate ();

        if (! ImGui::IsMouseDown (ImGuiMouseButton_Right))
        {
          // Activate the row of the current game
          ImGui::ActivateItem (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()));

          if (! ImGui::IsItemVisible    (    ))
            ImGui::SetScrollHereY       (0.5f);
          
          ImGui::SetKeyboardFocusHere (    );

          // This fixes ImGui not allowing the GameContextMenu to be opened on first search
          //   without an additional keyboard input
          ImGuiContext& g = *ImGui::GetCurrentContext();
          g.NavDisableHighlight = false;
        }

        //ImGui::SetFocusID(ImGui::GetID(app.first.c_str()), ImGui::GetCurrentWindow());
      }
    }

    if (selected)
    {
      // This allows the scroll to reset on DPI changes, to keep the selected item on-screen
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last)
        ImGui::SetScrollHereY (0.5f);

      pApp = &app.second;

      if (update)
      {
        UpdateInjectionStrategy (pApp);
        _cache.Refresh          (pApp);
      }
    }
  }

#pragma region GamesList::AddGame

  if (! _registry.bUIStatusBar)
  {
    float fOriginalY =
      ImGui::GetCursorPosY ( );

    ImGui::BeginGroup      ( );

    static bool btnHovered = false;
    ImGui::PushStyleColor (ImGuiCol_Header,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

    if (btnHovered)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    else
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));

    ImGui::SetCursorPosY   (fOriginalY + fOffset     + ( 1.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + ( 3.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::Text            (ICON_FA_SQUARE_PLUS);
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + (30.0f * SKIF_ImGui_GlobalDPIScale));

    if (ImGui::Selectable      ("Add Game"))
      AddGamePopup = PopupState_Open;

    btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

    ImGui::PopStyleColor(4);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::EndGroup        ();
  }

#pragma endregion


  // Stop populating the whole list

  if (pApp != nullptr)
  {
    // Update the injection strategy for the game
    if (selection.dir_watch.isSignaled ( ))
    {
      UpdateInjectionStrategy (pApp);
      _cache.Refresh          (pApp);
    }
  }


#pragma region GamesList::IconMenu

  if (IconMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("IconMenu");
    IconMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup ("IconMenu", ImGuiWindowFlags_NoMove))
  {
    if (pApp != nullptr)
    {
      // Preparations

      bool resetVisible = (pApp->id    != SKIF_STEAM_APPID            ||
                          (pApp->id    != SKIF_STEAM_APPID            && // Ugly check to exclude the "Special K" entry from being set to true
                           pApp->store != app_record_s::Store::Other) ||
                           pApp->tex_icon.isCustom                    ||
                           pApp->tex_icon.isManaged);

      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE          ).x, ImGui::GetTextLineHeight()));
      if (pApp->tex_icon.isCustom)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE_LEFT         ).x, ImGui::GetTextLineHeight()));
      else if (pApp->tex_icon.isManaged)
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::ItemSize (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE              ).x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UP_RIGHT_FROM_SQUARE).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Change",    dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png;*.ico" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Other)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Epic_AppName).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

          if (targetPath != L"")
          {
            std::error_code ec;
            // Create any missing directories
            if (! std::filesystem::exists (            targetPath, ec))
                  std::filesystem::create_directories (targetPath, ec);

            targetPath += L"icon";

            DeleteFile ((targetPath + L".png").c_str());
            DeleteFile ((targetPath + L".jpg").c_str());
            DeleteFile ((targetPath + L".ico").c_str());

            CopyFile(pwszFilePath, (targetPath + ext).c_str(), false);
            
            ImVec2 dontCare1, dontCare2;

            // Reload the icon
            LoadLibraryTexture (LibraryTexture::Icon,
                                  pApp->id,
                                    pApp->tex_icon.texture,
                                      (pApp->store == app_record_s::Store::GOG)
                                      ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                      : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", _path_cache.steam_install, pApp->id), //L"_icon.jpg",
                                          dontCare1,
                                            dontCare2,
                                              pApp );
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }
      
      if (resetVisible)
      {
        if (ImGui::Selectable ((pApp->tex_icon.isCustom) ? "Reset" : "Refresh", dontCare, ImGuiSelectableFlags_SpanAllColumns | ((pApp->tex_icon.isCustom || pApp->tex_icon.isManaged) ? 0x0 : ImGuiSelectableFlags_Disabled)))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Other)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Epic)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Epic\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Epic_AppName).c_str());
          else if (pApp->store == app_record_s::Store::GOG)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    _path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == app_record_s::Store::Xbox)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == app_record_s::Store::Steam)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  _path_cache.specialk_userdata, pApp->id);

          if (PathFileExists(targetPath.c_str()))
          {
            std::wstring fileName = (pApp->tex_icon.isCustom) ? L"icon" : L"icon-original";

            bool d1 = DeleteFile ((targetPath + fileName + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + fileName + L".jpg").c_str()),
                 d3 = DeleteFile ((targetPath + fileName + L".ico").c_str());

            // If any file was removed
            if (d1 || d2 || d3)
            {
              ImVec2 dontCare1, dontCare2;

              // Reload the icon
              LoadLibraryTexture (LibraryTexture::Icon,
                                    pApp->id,
                                      pApp->tex_icon.texture,
                                        (pApp->store == app_record_s::Store::GOG)
                                        ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                        : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", _path_cache.steam_install, pApp->id), //L"_icon.jpg",
                                            dontCare1,
                                              dontCare2,
                                                pApp );
            }
          }
        }
        else
        {
          if (pApp->tex_icon.isCustom || pApp->tex_icon.isManaged)
            SKIF_ImGui_SetMouseCursorHand ( );
          else
            SKIF_ImGui_SetHoverTip        ("Managed by the platform client.");
        }
      }

      ImGui::Separator();

      // Strip (recently added) from the game name
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      std::string linkGridDB = (pApp->store == app_record_s::Store::Steam)
                             ? SK_FormatString("https://www.steamgriddb.com/steam/%lu/icons", pApp->id)
                             : SK_FormatString("https://www.steamgriddb.com/search/icons?term=%s", name.c_str());

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(linkGridDB).c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (linkGridDB);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->tex_icon.isCustom)
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE_LEFT
                              );

      else if (pApp->tex_icon.isManaged)
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_ROTATE
                              );
      else if (resetVisible) // If texture is neither custom nor managed
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (128, 128, 128) : ImColor (128, 128, 128),
                  ICON_FA_ROTATE
                              );

      ImGui::Separator   ( );

      ImGui::TextColored (
              ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_UP_RIGHT_FROM_SQUARE
                            );
    }

    ImGui::EndPopup      ( );
  }
#pragma endregion

  ImGui::EndGroup        ( );
  ImGui::EndChild        ( );
  ImGui::PopStyleColor   ( );
  
#pragma region GamesList::EmptySpaceMenu

  if (! ImGui::IsAnyPopupOpen   ( ) &&
      ! ImGui::IsAnyItemHovered ( ) &&
        ImGui::IsItemClicked    (ImGuiMouseButton_Right))
  {
    ImGui::OpenPopup      ("GameListEmptySpaceMenu");
  }

  if (ImGui::BeginPopup   ("GameListEmptySpaceMenu", ImGuiWindowFlags_NoMove))
  {
    bool dontCare = false;
    
    ImGui::BeginGroup     ( );

    ImVec2 iconPos = ImGui::GetCursorPos();
    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_SQUARE_PLUS).x, ImGui::GetTextLineHeight()));

    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );

    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_GEARS).x, ImGui::GetTextLineHeight()));

    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );

    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_ROTATE_RIGHT).x, ImGui::GetTextLineHeight()));

    ImGui::EndGroup       ( );

    ImGui::SameLine       ( );

    ImGui::BeginGroup     ( );

     if (ImGui::Selectable ("Add Game", dontCare, ImGuiSelectableFlags_SpanAllColumns))
       AddGamePopup = PopupState_Open;

    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );

    if (ImGui::BeginMenu("Platforms"))
    {
      constexpr char spaces[] = { "\u0020\u0020\u0020\u0020" };

      if (ImGui::MenuItem ("Epic",  spaces, &_registry.bLibraryEpic))
      {
        _registry.regKVLibraryEpic.putData  (_registry.bLibraryEpic);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("GOG",   spaces, &_registry.bLibraryGOG))
      {
        _registry.regKVLibraryGOG.putData   (_registry.bLibraryGOG);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Steam", spaces, &_registry.bLibrarySteam))
      {
        _registry.regKVLibrarySteam.putData (_registry.bLibrarySteam);
        RepopulateGames = true;
      }

      if (ImGui::MenuItem ("Xbox",  spaces, &_registry.bLibraryXbox))
      {
        _registry.regKVLibraryXbox.putData  (_registry.bLibraryXbox);
        RepopulateGames = true;
      }

      ImGui::EndMenu ( );
    }

    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );

    if (ImGui::Selectable ("Refresh",  dontCare, ImGuiSelectableFlags_SpanAllColumns))
      RepopulateGames = true;

    ImGui::EndGroup       ( );

    ImGui::SetCursorPos   (iconPos);
    ImGui::Text           (ICON_FA_SQUARE_PLUS);
    ImGui::Separator      ( );
    ImGui::Text           (ICON_FA_GEARS);
    ImGui::Separator      ( );
    ImGui::Text           (ICON_FA_ROTATE_RIGHT);
    ImGui::EndPopup       ( );
  }

#pragma endregion

#pragma endregion
  // GamesList END

  // Applies hover text on the whole AppListInset1
  //if (SKIF_StatusBarText.empty ()) // Prevents the text from overriding the keyboard search hint
    //SKIF_ImGui_SetHoverText ("Right click for more options");

  ImGui::BeginChild (
    "###AppListInset2",
      ImVec2 ( (_WIDTH - ImGui::GetStyle().WindowPadding.x / 2.0f),
               _HEIGHT2 ), (_registry.bUIBorders),
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NavFlattened      |
        ImGuiWindowFlags_AlwaysUseWindowPadding
  );
  ImGui::BeginGroup ();

  if ( pApp     == nullptr       ||
       pApp->id == SKIF_STEAM_APPID )
  {
    _inject._GlobalInjectionCtl ();
  }

  else if (pApp != nullptr)
  {
    GetInjectionSummary (pApp);

    if ( pApp->extended_config.vac.enabled == 1 )
    {
        SKIF_StatusBarText = "Warning: ";
        SKIF_StatusBarHelp = "VAC protected game - Injection is not recommended!";
    }

    if ( pApp->specialk.injection.injection.type != InjectionType::Local )
    {
      ImGui::SetCursorPosY (
        ImGui::GetWindowHeight () - fBottomDist -
        ImGui::GetStyle        ().ItemSpacing.y
      );

      ImGui::Separator     ( );

      SKIF_ImGui_BeginChildFrame  ( ImGui::GetID ("###launch_cfg"),
                                    ImVec2 (ImGui::GetContentRegionAvail ().x,
                                  std::max (ImGui::GetContentRegionAvail ().y,
                                            ImGui::GetTextLineHeight () + ImGui::GetStyle ().FramePadding.y * 2.0f + ImGui::GetStyle ().ItemSpacing.y * 2
                                           )),
                                    ImGuiWindowFlags_NavFlattened      |
                                    ImGuiWindowFlags_NoScrollbar       |
                                    ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoBackground
      );

      auto _BlacklistCfg =
      [&](app_record_s::launch_config_s& launch_cfg, bool menu = false) ->
      void
      {
        /*
        if (pApp->extended_config.vac.enabled == 1)
        {
          launch_cfg.setBlacklisted (pApp->id, true);
        }
        */

        bool blacklist =
          launch_cfg.isBlacklisted (pApp->id);
          //|| _inject._TestUserList(SK_WideCharToUTF8(launch_cfg.getExecutableFullPath(pApp->id)).c_str(), false);

        char          szButtonLabel [256] = { };

        if (menu)
          sprintf_s ( szButtonLabel, 255,
                        " for \"%ws\"###DisableLaunch%d",
                          launch_cfg.description.empty ()
                            ? launch_cfg.executable .c_str ()
                            : launch_cfg.description.c_str (),
                          launch_cfg.id);
        else
          sprintf_s ( szButtonLabel, 255,
                        " Disable Special K###DisableLaunch%d",
                          launch_cfg.id );
          
        if (ImGui::Checkbox (szButtonLabel,   &blacklist))
          launch_cfg.setBlacklisted (pApp->id, blacklist);

        SKIF_ImGui_SetHoverText (
                    SK_FormatString (
                      R"(%ws)",
                      launch_cfg.executable.c_str  ()
                    ).c_str ()
        );
      };

      if ( ! _inject.bHasServlet )
      {
        ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Service is unavailable due to missing files.");
      }

      else if ( ! pApp->launch_configs.empty() )
      {
        bool elevate =
          pApp->launch_configs[0].isElevated (pApp->id);

        if (ImGui::Checkbox ("Elevated service###ElevatedLaunch",   &elevate))
          pApp->launch_configs[0].setElevated (pApp->id, elevate);

        ImGui::SameLine ( );

        // Set horizontal position
        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX  ( ) +
          ImGui::GetColumnWidth ( ) -
          ImGui::CalcTextSize   ("[] Disable Special K").x -
          ImGui::GetStyle       ( ).ItemSpacing.x * 2
        );

        // If there is only one launch option
        if ( pApp->launch_configs.size  () == 1 )
        {
          // Only if it is a valid one
          if ( pApp->launch_configs.begin()->second.valid)
          {
            _BlacklistCfg          (
                 pApp->launch_configs.begin ()->second );
          }
        }

        // If there are more than one launch option
        else
        {
          ImGui::SetCursorPosX (
            ImGui::GetCursorPosX  ( ) +
            14.0f * SKIF_ImGui_GlobalDPIScale
          );

          if (ImGui::Selectable (ICON_FA_BARS " Disable Special K "))
          {
            ImVec2 pos =
              ImGui::GetCursorPos ( );

            ImGui::SameLine     ( );

            ImGui::SetNextWindowPos (
              ImGui::GetCursorScreenPos ( ) -
              ImVec2 (0.0f, 8.0f * SKIF_ImGui_GlobalDPIScale)
            );

            ImGui::OpenPopup    ("###DisableSK");
            ImGui::SetCursorPos (pos);
          }

          if (ImGui::BeginPopup ("###DisableSK", ImGuiWindowFlags_NoMove))
          {
            //std::set <std::wstring> _used_launches;
            for ( auto& launch : pApp->launch_configs )
            {
              // TODO: Secondary-Launch-Options: Need to ensure launch options that share an executable only gets listed once.
              //if (! launch.second.valid ) /* ||
                  //! _used_launches.emplace (launch.second.blacklist_file).second ) */
                //continue;

              _BlacklistCfg (launch.second, true);
            }

            ImGui::EndMenu       ();
          }
        }
      }

      ImGui::EndChildFrame     ();

      fBottomDist = ImGui::GetItemRectSize().y;
    }
  }

  ImGui::EndGroup     (                  );
  ImGui::EndChild     (                  );
  ImGui::EndGroup     (                  );

  

  // Special handling at the bottom for Special K
  if (selection.appid == SKIF_STEAM_APPID && selection.store == app_record_s::Store::Steam) {
    ImGui::SetCursorPos  (                           ImVec2 ( vecPosCoverImage.x + ImGui::GetStyle().FrameBorderSize,
                                                              fY - floorf((204.f * SKIF_ImGui_GlobalDPIScale) + ImGui::GetStyle().FrameBorderSize) ));
    ImGui::BeginGroup    ();
    static bool hoveredPatButton  = false,
                hoveredPatCredits = false;

    // Set all button styling to transparent
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0, 0, 0, 0));

    // Remove frame border
    ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 0.0f);

    bool        clicked =
    ImGui::ImageButton   ((ImTextureID)pPatTexSRV.p, ImVec2 (200.0F * SKIF_ImGui_GlobalDPIScale,
                                                             200.0F * SKIF_ImGui_GlobalDPIScale),
                                                     ImVec2 (0.f,       0.f),
                                                     ImVec2 (1.f,       1.f),     0,
                                                     ImVec4 (0, 0, 0, 0), // Use a transparent background
                                  hoveredPatButton ? ImVec4 (  1.0f,  1.0f,  1.0f, 1.00f)
                                                   : ImVec4 (  0.8f,  0.8f,  0.8f, 0.66f));

    // Restore frame border
    ImGui::PopStyleVar   ( );

    // Restore the custom button styling
    ImGui::PopStyleColor (3);

    hoveredPatButton =
    ImGui::IsItemHovered ( );

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");
    //SKIF_ImGui_SetHoverTip  ("Click to help support the project");

    if (clicked)
      SKIF_Util_OpenURI (
        L"https://www.patreon.com/Kaldaien"
      );

    ImGui::SetCursorPos  (ImVec2 (fZ - (233.0f * SKIF_ImGui_GlobalDPIScale),
                                  fY - (204.0f * SKIF_ImGui_GlobalDPIScale)) );

    ImGui::PushStyleColor     (ImGuiCol_ChildBg,        hoveredPatCredits ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
                                                                          : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(.8f, .8f, .8f, .66f));
    ImGui::BeginChild         ("###PatronsChild", ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale,
                                                          200.0f * SKIF_ImGui_GlobalDPIScale),
                                                                      (_registry.bUIBorders),
                                                      ImGuiWindowFlags_NoScrollbar            |
                                                      ImGuiWindowFlags_AlwaysUseWindowPadding |
                        ((pApp->tex_cover.isCustom) ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoBackground));

    ImGui::TextColored        (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption) * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f), "Special Kudos to our Patrons:");

    std::string patrons_ = SKIF_Updater::GetInstance ( ).GetPatrons ( );

    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    
    ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor     (ImGuiCol_Text,           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4  (0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_ScrollbarBg,    ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImColor (0, 0, 0, 0).Value);
    ImGui::InputTextMultiline ("###Patrons", patrons_.data (), patrons_.length (),
                   ImVec2 (205.0f * SKIF_ImGui_GlobalDPIScale,
                           160.0f * SKIF_ImGui_GlobalDPIScale),
                                    ImGuiInputTextFlags_ReadOnly );
    ImGui::PopStyleColor      (4);
    ImGui::PopStyleVar        ( );

    hoveredPatCredits =
    ImGui::IsItemActive();

    ImGui::EndChild           ( );
    ImGui::PopStyleColor      ( );

    hoveredPatCredits = hoveredPatCredits ||
    ImGui::IsItemHovered      ( );

    ImGui::EndGroup           ( );
  }

  // Refresh running state of SKIF Custom, Epic, GOG, and Xbox titles
  static DWORD lastGameRefresh = 0;

  if (SKIF_Util_timeGetTime() > lastGameRefresh + 5000 && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( )))
  {
    for (auto& app : apps)
    {
      if (app.second.store == app_record_s::Store::Steam)
        continue;
      app.second._status.running = false;
    }

    PROCESSENTRY32W none = { },
                    pe32 = { };

    SK_AutoHandle hProcessSnap (
      CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
    );

    if ((intptr_t)hProcessSnap.m_h > 0)
    {
      pe32.dwSize = sizeof (PROCESSENTRY32W);
      std::wstring exeFileLast, exeFileNew;

      if (Process32FirstW (hProcessSnap, &pe32))
      {
        do
        {
          SetLastError (NO_ERROR);
          CHandle hProcess (OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID));
          // Use PROCESS_QUERY_LIMITED_INFORMATION since that allows us to retrieve exit code/full process name for elevated processes

          if (hProcess == nullptr)
            continue;

          exeFileNew = pe32.szExeFile;

          // Skip duplicate processes
          // NOTE: Potential bug is that Epic, GOG and SKIF Custom games with two running and identically named executables (launcher + game) may not be detected as such
          if (exeFileLast == exeFileNew)
            continue;

          exeFileLast = exeFileNew;

          std::wstring fullPath;

          bool accessDenied =
            GetLastError ( ) == ERROR_ACCESS_DENIED;

          // Get exit code to filter out zombie processes
          DWORD dwExitCode = 0;
          GetExitCodeProcess (hProcess, &dwExitCode);

          if (! accessDenied)
          {
            // If the process is not active any longer, skip it (terminated or zombie process)
            if (dwExitCode != STILL_ACTIVE)
              continue;

            WCHAR szExePath[MAX_PATH];
            DWORD len = MAX_PATH;

            // See if we can retrieve the full path of the executable
            if (QueryFullProcessImageName (hProcess, 0, szExePath, &len))
              fullPath = std::wstring (szExePath);
          }

          for (auto& app : apps)
          {
            // Steam games are covered through separate registry monitoring
            if (app.second.store == app_record_s::Store::Steam)
              continue;

            // Workaround for Xbox games that run under the virtual folder, e.g. H:\Games\Xbox Games\Hades\Content\Hades.exe
            else if (app.second.store == app_record_s::Store::Xbox && (! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str())))
            {
              app.second._status.running = true;
              break;
            }

            // Epic, GOG and SKIF Custom should be straight forward
            else if (fullPath == app.second.launch_configs[0].getExecutableFullPath(app.second.id, false)) // full patch
            {
              app.second._status.running = true;
              break;

              // One can also perform a partial match with the below OR clause in the IF statement, however from testing
              //   PROCESS_QUERY_LIMITED_INFORMATION gives us GetExitCodeProcess() and QueryFullProcessImageName() rights
              //     even to elevated processes, meaning the below OR clause is unnecessary.
              // 
              // (fullPath.empty() && ! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str()))
              //
            }
          }

          if (! _registry.bWarningRTSS    &&
              ! ImGui::IsAnyPopupOpen ( ) &&
              ! wcscmp (pe32.szExeFile, L"RTSS.exe"))
          {
            _registry.bWarningRTSS = true;
            _registry.regKVWarningRTSS.putData (_registry.bWarningRTSS);
            confirmPopupTitle = "One-time warning about RTSS.exe";
            confirmPopupText  = "RivaTuner Statistics Server (RTSS) occasionally conflicts with Special K.\n"
                                "Try closing it down if Special K does not behave as expected, or enable\n"
                                "the option 'Use Microsoft Detours API hooking' in the settings of RTSS.\n"
                                "\n"
                                "If you use MSI Afterburner, try closing it as well as otherwise it will\n"
                                "automatically restart RTSS silently in the background.\n"
                                "\n"
                                "This warning will not appear again.";
            ConfirmPopup      = PopupState_Open;
          }

        } while (Process32NextW (hProcessSnap, &pe32));
      }
    }

    lastGameRefresh = SKIF_Util_timeGetTime();
  }


  extern void SKIF_ImGui_ServiceMenu (void);

  SKIF_ImGui_ServiceMenu ( );



  if (openedGameContextMenu)
  {
    ImGui::OpenPopup    ("GameContextMenu");
    openedGameContextMenu = false;
  }


  if (ImGui::BeginPopup ("GameContextMenu", ImGuiWindowFlags_NoMove))
  {
    if (pApp != nullptr)
    {
      // Hide the Launch option for Special K
      if (pApp->id != SKIF_STEAM_APPID)
      {
        if ( ImGui::Selectable ("Launch", false,
                               ((pApp->_status.running || pApp->_status.updating)
                                 ? ImGuiSelectableFlags_Disabled
                                 : ImGuiSelectableFlags_None)))
          clickedGameLaunch = true;

        if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
        {
          if (! _inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Starts the injection service as well.");

          ImGui::PushStyleColor  ( ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
          );

          if ( ImGui::Selectable ("Launch without Special K", false,
                                 ((pApp->_status.running || pApp->_status.updating)
                                   ? ImGuiSelectableFlags_Disabled
                                   : ImGuiSelectableFlags_None)))
            clickedGameLaunchWoSK = true;

          ImGui::PopStyleColor   ( );
          if (_inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Stops the injection service as well.");
        }

        if (GOGGalaxy_Installed && pApp->store == app_record_s::Store::GOG)
        {
          if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
          {
            ImGui::Separator        ( );

            if (ImGui::BeginMenu    ("Launch using GOG Galaxy"))
            {
              if (ImGui::Selectable ("Launch", false,
                                    ((pApp->_status.running || pApp->_status.updating)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
                clickedGalaxyLaunch = true;

              ImGui::PushStyleColor ( ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
              );

              if (ImGui::Selectable ("Launch without Special K", false,
                                    ((pApp->_status.running || pApp->_status.updating)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
                clickedGalaxyLaunchWoSK = true;

              ImGui::PopStyleColor  ( );
              ImGui::EndMenu        ( );
            }
          }

          else {
            if (ImGui::Selectable   ("Launch using GOG Galaxy", false,
                                    ((pApp->_status.running || pApp->_status.updating)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
              clickedGalaxyLaunch = true;
          }

          if (clickedGalaxyLaunch ||
              clickedGalaxyLaunchWoSK)
          {
            bool localInjection = (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local);
            bool usingSK        = localInjection;

            // Check if the injection service should be used
            if (! usingSK)
            {
              std::string fullPath = SK_WideCharToUTF8 (pApp->launch_configs[0].getExecutableFullPath(pApp->id));
              bool isLocalBlacklisted  = pApp->launch_configs[0].isBlacklisted (pApp->id),
                   isGlobalBlacklisted = _inject._TestUserList (fullPath.c_str (), false);

              usingSK = ! clickedGalaxyLaunchWoSK &&
                        ! isLocalBlacklisted      &&
                        ! isGlobalBlacklisted;

              if (usingSK)
              {
                // Whitelist the path if it haven't been already
                if (_inject.WhitelistPath (fullPath))
                  _inject.SaveWhitelist ( );
              }

              // Kickstart service if it is currently not running
              if (! _inject.bCurrentState && usingSK)
                _inject._StartStopInject (false, true, pApp->launch_configs[0].isElevated(pApp->id));

              // Stop the service if the user attempts to launch without SK
              else if (clickedGalaxyLaunchWoSK && _inject.bCurrentState)
                _inject._StartStopInject (true);
            }

            // Create the injection acknowledge events in case of a local injection
            else {
              _inject.SetInjectAckEx     (true);
              _inject.SetInjectExitAckEx (true);
            }

            // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

            std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

            SKIF_Util_OpenURI (GOGGalaxy_Path, SW_SHOWDEFAULT, L"OPEN", launchOptions.c_str());

            SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), GOGGalaxy_Path, launchOptions, L"", pApp->launch_configs[0].getExecutableFullPath (pApp->id), (! localInjection && usingSK));

            /*
            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = GOGGalaxy_Path.c_str();
            sexi.lpParameters = launchOptions.c_str();
          //sexi.lpDirectory  = NULL;
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);
            */

            // Also minimize SKIF if configured as such
            // Disable the first service notification
            if (_registry.bMinimizeOnGameLaunch)
            {
              _registry._SuppressServiceNotification = true;
          
              // Fallback for minimizing SKIF when not using SK if configured as such
              if (! usingSK && SKIF_ImGui_hWnd != NULL)
                ShowWindowAsync (SKIF_ImGui_hWnd, SW_SHOWMINNOACTIVE);
            }

            clickedGalaxyLaunch = clickedGalaxyLaunchWoSK = false;
          }
        }

        // If Profile Folder Exists
        if (PathFileExists(pApp->specialk.injection.config.dir.c_str()))
        {
          ImGui::Separator   ( );

          std::wstring_view  wsRootDir = pApp->specialk.injection.config.dir;
          std::wstring wsScreenshotDir = pApp->specialk.injection.config.dir + LR"(\Screenshots)";
          bool screenshotsFolderExists = PathFileExists (wsScreenshotDir.c_str());
        
          ImGui::BeginGroup  ( );
          ImVec2 iconPosDummy = ImGui::GetCursorPos();

          ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN) .x, ImGui::GetTextLineHeight()));
          if (screenshotsFolderExists)
            ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_IMAGES)       .x, ImGui::GetTextLineHeight()));
        
          ImGui::EndGroup    ( );
          ImGui::SameLine    ( );
          ImGui::BeginGroup  ( );

          bool dontCare = false;

          // Config Root
          if (ImGui::Selectable         ("Browse Profile Folder", dontCare, ImGuiSelectableFlags_SpanAllColumns))
            SKIF_Util_ExplorePath       (wsRootDir);
          SKIF_ImGui_SetMouseCursorHand ();
          SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (wsRootDir.data()).c_str());
        
          if (screenshotsFolderExists)
          {
            // Screenshot Folder
            if (ImGui::Selectable         ("Browse Screenshots", dontCare, ImGuiSelectableFlags_SpanAllColumns))
              SKIF_Util_ExplorePath       (wsScreenshotDir);
            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (wsScreenshotDir.data()).c_str());
          }

          ImGui::EndGroup  ( );

          ImGui::SetCursorPos  (iconPosDummy);

          ImGui::TextColored (
            ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), // ImColor (255, 207, 72),
                     ICON_FA_FOLDER_OPEN
                               );
          if (screenshotsFolderExists)
            ImGui::TextColored (
                     ImColor   (200, 200, 200, 255),
                       ICON_FA_IMAGES
                                 );
        }
      }

      // Special K is selected -- relevant show quick links
      else
      {
        ImGui::BeginGroup  ( );
        ImVec2 iconPos = ImGui::GetCursorPos();

        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_BOOK_OPEN).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCORD)  .x, ImGui::GetTextLineHeight()));
        ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
        ImGui::Separator  (  );
        ImGui::PopStyleColor (  );
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCOURSE).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_PATREON)  .x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_GITHUB)   .x, ImGui::GetTextLineHeight()));

        ImGui::EndGroup   (  );
        ImGui::SameLine   (  );
        ImGui::BeginGroup (  );
        bool dontCare = false;

        if (ImGui::Selectable ("Wiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://wiki.special-k.info/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/");


        if (ImGui::Selectable ("Discord", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://discord.gg/specialk"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discord.gg/specialk");


        if (ImGui::Selectable ("Forum", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://discourse.differentk.fyi/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discourse.differentk.fyi/");


        if (ImGui::Selectable ("Patreon", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://www.patreon.com/Kaldaien"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://www.patreon.com/Kaldaien");

        if (ImGui::Selectable ("GitHub", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://github.com/SpecialKO"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://github.com/SpecialKO");

        ImGui::EndGroup   ( );

        ImGui::SetCursorPos(iconPos);

        ImGui::TextColored (
               ImColor   (25, 118, 210),
                 ICON_FA_BOOK
                             );
        ImGui::TextColored (
               ImColor   (114, 137, 218),
                 ICON_FA_DISCORD
                             );
        ImGui::TextColored (
               (_registry.iStyle == 2) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImVec4 (ImColor (247, 241, 169)),
                 ICON_FA_DISCOURSE
                             );
        ImGui::TextColored (
               ImColor   (249, 104,  84),
                 ICON_FA_PATREON
                             );
        ImGui::TextColored (
               (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
                 ICON_FA_GITHUB
                             );
      }

      if (pApp->store == app_record_s::Store::Steam &&
         (pApp->id != SKIF_STEAM_APPID || (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
        ImGui::Separator  ( );

      /*
      if (! pApp->specialk.screenshots.empty ())
      {
        if (ImGui::BeginMenu ("Screenshots"))
        {
          for (auto& screenshot : pApp->specialk.screenshots)
          {
            if (ImGui::Selectable (screenshot.c_str ()))
            {
              SKIF_GameManagement_ShowScreenshot (
                SK_UTF8ToWideChar (screenshot)
              );
            }

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText       (screenshot.c_str ());
          }

          ImGui::EndMenu ();
        }
      }
      */
      
      ImGui::PushStyleColor ( ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) // * ImVec4(1.0f, 1.0f, 1.0f, 1.0f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
      );

      if (! pApp->cloud_saves.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_SD_CARD "   Game Saves and Config");

        std::set <std::wstring> used_paths_;

        if (bMenuOpen)
        {
          if (! pApp->cloud_enabled)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
                                   ICON_FA_TRIANGLE_EXCLAMATION " Auto-Cloud is not enabled" );
            ImGui::Separator   ( );
          }

          bool bCloudSaves = false;

          for (auto& cloud : pApp->cloud_saves)
          {
            if (! cloud.second.valid)
              continue;

            if ( app_record_s::Platform::Unknown == cloud.second.platforms ||
                 app_record_s::supports (           cloud.second.platforms,
                 app_record_s::Platform::Windows )
               )
            {
              wchar_t sel_path [MAX_PATH    ] = { };
              char    label    [MAX_PATH * 2] = { };

              swprintf ( sel_path, MAX_PATH, L"%ws",
                           cloud.second.evaluated_dir.c_str () );

              sprintf ( label, "%ws###CloudUFS.%d", sel_path,
                                     cloud.first );

              bool selected = false;

              if (used_paths_.emplace (sel_path).second)
              {
                if (ImGui::Selectable (label, &selected))
                {
                  SKIF_Util_ExplorePath (sel_path);

                  ImGui::CloseCurrentPopup ();
                }
                SKIF_ImGui_SetMouseCursorHand ();
                SKIF_ImGui_SetHoverText       (
                             SK_FormatString ( R"(%ws)",
                                        cloud.second.evaluated_dir.c_str ()
                                             ).c_str ()
                                              );
                bCloudSaves = true;
              }
            }
          }

          if (! bCloudSaves)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "No locations could be found.");
          }

          ImGui::EndMenu ();

        //SKIF_ImGui_SetHoverTip ("Browse files cloud-sync'd by Steam");
        }
      }

      if (! pApp->branches.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_CODE_BRANCH "   Software Branches");

        static
          std::set  < std::string >
                      used_branches_;

        using branch_ptr_t =
          std::pair <          std::string*,
             app_record_s::branch_record_s* >;

        static
          std::multimap <
            int64_t, branch_ptr_t
          > branches;

        // Clear the cache when changing selection
        if ( (! branches.empty ()) &&
                branches.begin ()->second.second->parent != pApp )
        {
          branches.clear       ();
          used_branches_.clear ();
        }

        if (bMenuOpen)
        {
          if (branches.empty ())
          {
            for ( auto& it : pApp->branches )
            {
              if (used_branches_.emplace (it.first).second)
              {
                auto& branch =
                  it.second;

                // Sort in descending order
                branches.emplace (
                  std::make_pair   (-(int64_t)branch.build_id,
                    std::make_pair (
                      const_cast <std::string                   *> (&it.first),
                      const_cast <app_record_s::branch_record_s *> (&it.second)
                    )
                  )
                );
              }
            }
          }

          for ( auto& it : branches )
          {
            auto& branch_name =
                  *(it.second.first);

            auto& branch =
                  *(it.second.second);

            ImGui::PushStyleColor (
              ImGuiCol_Text, branch.pwd_required ?
                               ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
                                                 :
                               ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
            );

            bool bExpand =
              ImGui::BeginMenu (branch_name.c_str ());

            ImGui::PopStyleColor ();

            if (bExpand)
            {
              if (! branch.description.empty ())
              {
                ImGui::MenuItem ( "Description",
                            branch.getDescAsUTF8 ().c_str () );
              }

              ImGui::MenuItem ( "App Build #",
                                  std::to_string (
                                                  branch.build_id
                                                 ).c_str ()
              );

              if (branch.time_updated > 0)
              {
                ImGui::MenuItem ( "Last Update", branch.getTimeAsCStr ().c_str () );
              }

              ImGui::MenuItem ( "Accessibility", branch.pwd_required ?
                                       "Private (Password Required)" :
                                              "Public (No Password)" );

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }
      }

      ImGui::PopStyleColor  ( );

      static uint32_t curAppId;
      static bool     SteamShortcutPossible;

      // Do not check games that are being updated (aka installed)
      if (pApp->store == app_record_s::Store::Steam &&
        ! pApp->_status.updating)
      {
        if (curAppId != pApp->id)
        {   curAppId  = pApp->id;
          SteamShortcutPossible = (pApp->launch_configs[0].getExecutableFullPath(pApp->id) != L"<InvalidPath>");
        }
      }

      else {
        SteamShortcutPossible = false;
      }

      // Manage [Custom] Game
      if (pApp->store == app_record_s::Store::Other || pApp->store == app_record_s::Store::GOG || SteamShortcutPossible)
      {
        ImGui::Separator ( );
        
        if (ImGui::BeginMenu (ICON_FA_GEAR "  Manage"))
        {
          if (pApp->store == app_record_s::Store::Other)
          {
            if (ImGui::Selectable ("Properties"))
              ModifyGamePopup = PopupState_Open;

            ImGui::Separator ( );
          }

          if (ImGui::Selectable ("Create shortcut"))
          {
            std::string name = pApp->names.normal;

            // Strip (recently added) from the desktop shortcuts
            try {
              name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
            }
            catch (const std::exception& e)
            {
              UNREFERENCED_PARAMETER(e);
            }

            /* Old method
            // Strip invalid filename characters
            //const std::string forbidden = "\\/:?\"<>|";
            //std::transform(name.begin(), name.end(), name.begin(), [&forbidden](char c) { return forbidden.find(c) != std::string::npos ? ' ' : c; });

            // Remove double spaces
            //name = std::regex_replace(name, std::regex(R"(  )"), " ");
            */

            name = SKIF_Util_StripInvalidFilenameChars (name);

            std::wstring linkPath = SK_FormatStringW (LR"(%ws\%ws.lnk)", std::wstring(_path_cache.desktop.path).c_str(), SK_UTF8ToWideChar(name).c_str());
            std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str(), pApp->launch_configs[0].launch_options.c_str());

            // Trim spaces at the end
            linkArgs.erase (std::find_if (linkArgs.rbegin(), linkArgs.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), linkArgs.end());

            if (pApp->store == app_record_s::Store::Steam)
              linkArgs += L" SKIF_SteamAppId=" + std::to_wstring (pApp->id);

            confirmPopupTitle = "Create Shortcut";

            if (SKIF_Util_CreateShortcut (
                linkPath.c_str(),
                _path_cache.skif_executable,
                linkArgs.c_str(),
                pApp->launch_configs[0].working_dir.c_str(),
                SK_UTF8ToWideChar(name).c_str(),
                pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str()
                )
              )
              confirmPopupText = "A desktop shortcut has been created.";
            else
              confirmPopupText = "Failed to create a desktop shortcut!";

            ConfirmPopup = PopupState_Open;
          }

          if (pApp->store == app_record_s::Store::Other)
          {
            if (ImGui::Selectable ("Remove"))
              RemoveGamePopup = PopupState_Open;
          }

          ImGui::EndMenu ( );
        }

      }

      ImGui::PushStyleColor ( ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
      );

      ImGui::Separator ( );

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN)       .x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_SCREWDRIVER_WRENCH).x, ImGui::GetTextLineHeight()));

      if (pApp->store == app_record_s::Store::GOG)
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
      }

      else if (pApp->store == app_record_s::Store::Steam && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
      }
      ImGui::EndGroup    ( );
      ImGui::SameLine    ( );
      ImGui::BeginGroup  ( );
      bool dontCare = false;
      if (ImGui::Selectable ("Browse Install Folder", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_ExplorePath (pApp->install_dir);
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (pApp->install_dir)
                                        );
      }
      
      std::wstring pcgwValue =   (pApp->store == app_record_s::Store::Other || pApp->store == app_record_s::Store::Epic || pApp->store == app_record_s::Store::Xbox)
                               ? SK_UTF8ToWideChar (pApp->names.normal)
                               : std::to_wstring   (pApp->id);

      std::wstring pcgwLink =   ((pApp->store == app_record_s::Store::GOG)   ? L"https://www.pcgamingwiki.com/api/gog.php?page="
                               : (pApp->store == app_record_s::Store::Steam) ? L"https://www.pcgamingwiki.com/api/appid.php?appid="
                                                                             : L"https://www.pcgamingwiki.com/w/index.php?search=") + pcgwValue;

      if (ImGui::Selectable  ("Browse PCGamingWiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI (pcgwLink.c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (pcgwLink).c_str()
        );
      }

      if (pApp->store == app_record_s::Store::GOG)
      {
        if (ImGui::Selectable  ("Browse GOG Database", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI ((L"https://www.gogdb.org/product/" + std::to_wstring (pApp->id)).c_str());
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://www.gogdb.org/product/%lu", pApp->id
            )
          );
        }
      }
      else if (pApp->store == app_record_s::Store::Steam &&
        (pApp->id != SKIF_STEAM_APPID || (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        if (ImGui::Selectable  ("Browse Steam", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI ((L"steam://nav/games/details/" + std::to_wstring (pApp->id)).c_str());
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "steam://nav/games/details/%lu", pApp->id
                            )
                                          );
        }

        if (ImGui::Selectable  ("Browse Steam Community", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI ((L"https://steamcommunity.com/app/" + std::to_wstring (pApp->id)).c_str());
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://steamcommunity.com/app/%lu", pApp->id
                            )
                                          );
        }

        if (ImGui::Selectable  ("Browse SteamDB", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI ((L"https://steamdb.info/app/" + std::to_wstring (pApp->id)).c_str());
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://steamdb.info/app/%lu", pApp->id
                            )
                                          );
        }
      }
      ImGui::EndGroup      ( );
      ImGui::PopStyleColor ( );

      ImGui::SetCursorPos  (iconPos);

      ImGui::TextColored (
               ImColor (255, 207, 72),
                 ICON_FA_FOLDER_OPEN
                           );
      ImGui::TextColored (
               ImColor   (200, 200, 200, 255),
                 ICON_FA_SCREWDRIVER_WRENCH
                           );

      if (pApp->store == app_record_s::Store::GOG)
      {
        ImGui::TextColored (
         ImColor   (155, 89, 182, 255),
           ICON_FA_DATABASE );
      }

      else if (pApp->store == app_record_s::Store::Steam &&
        (pApp->id != SKIF_STEAM_APPID || (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {

        ImGui::TextColored (
         (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
           ICON_FA_STEAM_SYMBOL );

        ImGui::TextColored (
         (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
           ICON_FA_STEAM_SYMBOL );

        ImGui::TextColored (
         ImColor   (101, 192, 244, 255).Value,
           ICON_FA_DATABASE );
      }

    }

    else if (! update)
    {
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }

  
  static float fConfirmPopupWidth;
  if (ConfirmPopup == PopupState_Open)
  {
    fConfirmPopupWidth = ImGui::CalcTextSize (confirmPopupText.c_str()).x + ImGui::GetStyle().IndentSpacing * 3.0f; // 60.0f * SKIF_ImGui_GlobalDPIScale
    ImGui::OpenPopup ("###ConfirmPopup");
    ImGui::SetNextWindowSize (ImVec2 (fConfirmPopupWidth, 0.0f));
  }

  ImGui::SetNextWindowPos    (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
  if (ImGui::BeginPopupModal ((confirmPopupTitle + "###ConfirmPopup").c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
  {
    if (ConfirmPopup == PopupState_Open)
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###ConfirmPopup");
      if (window != nullptr && ! window->Appearing)
        ConfirmPopup = PopupState_Opened;
    }

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        (confirmPopupText.c_str());

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fConfirmPopupWidth / 2 - vButtonSize.x / 2);

    if (ImGui::Button  ("OK", vButtonSize))
    {
      confirmPopupText = "";
      ConfirmPopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }


  if (RemoveGamePopup == PopupState_Open)
  {
    ImGui::OpenPopup("###RemoveGamePopup");
    RemoveGamePopup = PopupState_Opened;
  }


  float fRemoveGamePopupWidth = 360.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fRemoveGamePopupWidth, 0.0f));
  ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

  if (ImGui::BeginPopupModal ("Remove Game###RemoveGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        ("Do you want to remove this game from SKIF?");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Yes", vButtonSize))
    {
      if (SKIF_RemoveCustomAppID(selection.appid))
      {
        // Hide entry
        pApp->id = 0;

        // Release the icon texture (the cover will be handled by LoadLibraryTexture on next frame
        if (pApp->tex_icon.texture.p != nullptr)
        {
          extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pApp->tex_icon.texture.p << " to be released";
          SKIF_ResourcesToFree.push(pApp->tex_icon.texture.p);
          pApp->tex_icon.texture.p = nullptr;
        }

        // Reset selection to Special K
        selection.reset ( );

        for (auto& app : apps)
          if (app.second.id == selection.appid && app.second.store == selection.store)
            pApp = &app.second;

        update = true;
      }

      RemoveGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("No", vButtonSize))
    {
      RemoveGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }
  else {
    RemoveGamePopup = PopupState_Closed;
  }


  if (AddGamePopup == PopupState_Open && ! ImGui::IsAnyPopupOpen ( ))
  {
    ImGui::OpenPopup("###AddGamePopup");
    //AddGamePopup = PopupState_Opened; // Set as part of the BeginPopupModal() call below instead
  }

  float fAddGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fAddGamePopupWidth, 0.0f));
  ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

  if (ImGui::BeginPopupModal ("Add Game###AddGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    /*
      name          - String -- Title/Name
      exe           - String -- Full Path to executable
      launchOptions - String -- Cmd line args
      id            - Autogenerated
      installDir    - Autogenerated
      exeFileName   - Autogenerated
    */

    if (AddGamePopup == PopupState_Open)
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###AddGamePopup");
      if (window != nullptr && ! window->Appearing)
        AddGamePopup = PopupState_Opened;
    }

    static char charName     [MAX_PATH],
                charPath     [MAX_PATH],
                charArgs     [500];
    static bool error = false;

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      LPWSTR pwszFilePath = NULL;

      if (SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe" }, 1, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST))
      {
        error = false;
        std::filesystem::path path           = pwszFilePath; // Wide-string std::filesystem::path
        std::filesystem::path pathDiscard    = pwszFilePath; // Wide-string std::filesystem::path which will be discarded
        std::string           pathFullPath   = SK_WideCharToUTF8  (pathDiscard.wstring());
        std::wstring          pathExtension  = SKIF_Util_ToLowerW (pathDiscard.extension().wstring());
        std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

        if (pathExtension == L".lnk")
        {
          WCHAR wszTarget    [MAX_PATH];
          WCHAR wszArguments [MAX_PATH];

          SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH);

          if (! PathFileExists (wszTarget))
          {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }

          else {
            std::wstring productName = SKIF_GetProductName (wszTarget);
            productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
            strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
            strncpy (charArgs, SK_WideCharToUTF8 (wszArguments).c_str(),               500);
            strncpy (charName, (productName != L"")
                                ? SK_WideCharToUTF8 (productName).c_str()
                                : pathFilename.c_str(), MAX_PATH);
          }
        }

        else if (pathExtension == L".exe") {
          std::wstring productName = SKIF_GetProductName (path.c_str());
          productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());

          strncpy (charPath, pathFullPath.c_str(),                                  MAX_PATH);
          strncpy (charName, (productName != L"")
                              ? SK_WideCharToUTF8 (productName).c_str()
                              : pathFilename.c_str(), MAX_PATH);
        }

        else {
          error = true;
          strncpy (charPath, "\0", MAX_PATH);
        }
      }

      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    ImGui::SameLine    ( );

    float fAddGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fAddGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 500, ImVec2(0,0), ImGuiInputTextFlags_None);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      SKIF_ImGui_PushDisableState ( );
    }

    if (ImGui::Button  ("Add Game", vButtonSize))
    {
      int newAppId = SKIF_AddCustomAppID(&apps, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs));

      if (newAppId > 0)
      {
        // Attempt to extract the icon from the given executable straight away
        std::wstring SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\icon-original.png)", _path_cache.specialk_userdata, newAppId);
        SKIF_Util_SaveExtractExeIcon (SK_UTF8ToWideChar(charPath), SKIFCustomPath);

        _registry.iLastSelectedGame  = newAppId;
        _registry.iLastSelectedStore = (int)app_record_s::Store::Other;
        _registry.regKVLastSelectedGame .putData (_registry.iLastSelectedGame);
        _registry.regKVLastSelectedStore.putData (_registry.iLastSelectedStore);
        RepopulateGames = true; // Rely on the RepopulateGames method instead
      }

      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      // Unload any current cover
      if (pTexSRV.p != nullptr)
      {
        extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(pTexSRV.p);
        pTexSRV.p = nullptr;
      }

      AddGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    if (disabled)
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      AddGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }
  else {
   AddGamePopup = PopupState_Closed;
  }



  if (ModifyGamePopup == PopupState_Open)
    ImGui::OpenPopup ("###ModifyGamePopup");

  float fModifyGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fModifyGamePopupWidth, 0.0f));
  ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

  if (ImGui::BeginPopupModal ("Manage Game###ModifyGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char charName     [MAX_PATH],
                charPath     [MAX_PATH],
                charArgs     [500];
                //charProfile  [MAX_PATH];
    static bool error = false;

    if (ModifyGamePopup == PopupState_Open)
    {
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      strncpy (charName, name.c_str( ), MAX_PATH);
      strncpy (charPath, SK_WideCharToUTF8 (pApp->launch_configs[0].getExecutableFullPath(pApp->id, false)).c_str(), MAX_PATH);
      strncpy (charArgs, SK_WideCharToUTF8 (pApp->launch_configs[0].launch_options).c_str(), 500);
      //strncpy (charProfile, SK_WideCharToUTF8 (SK_FormatStringW(LR"(%s\Profiles\%s)", _path_cache.specialk_userdata, pApp->specialk.profile_dir.c_str())).c_str(), MAX_PATH);
      
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###ModifyGamePopup");
      if (window != nullptr && ! window->Appearing)
        ModifyGamePopup = PopupState_Opened;
    }

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      LPWSTR pwszFilePath = NULL;
      if (SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe" }, 1, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST))
      {
        error = false;
        std::filesystem::path path           = pwszFilePath; // Wide-string std::filesystem::path
        std::filesystem::path pathDiscard    = pwszFilePath; // Wide-string std::filesystem::path which will be discarded
        std::string           pathFullPath   = SK_WideCharToUTF8  (pathDiscard.wstring());
        std::wstring          pathExtension  = SKIF_Util_ToLowerW (pathDiscard.extension().wstring());
        std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

        if (pathExtension == L".lnk")
        {
          WCHAR wszTarget    [MAX_PATH];
          WCHAR wszArguments [MAX_PATH];

          SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH);

          if (! PathFileExists (wszTarget))
          {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }

          else {
            std::wstring productName = SKIF_GetProductName (wszTarget);
            productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
            strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
          }
        }

        else if (pathExtension == L".exe") {
          std::wstring productName = SKIF_GetProductName (path.c_str());
          productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());

          strncpy (charPath, pathFullPath.c_str(),                                  MAX_PATH);
        }

        else {
          error = true;
          strncpy (charPath, "\0", MAX_PATH);
        }
      }

      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    ImGui::SameLine    ( );

    float fModifyGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fModifyGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fModifyGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fModifyGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 500, ImVec2(0,0), ImGuiInputTextFlags_None);
    SKIF_ImGui_DisallowMouseDragMove ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      SKIF_ImGui_PushDisableState ( );
    }

    if (ImGui::Button  ("Update", vButtonSize))
    {
      if (SKIF_ModifyCustomAppID (pApp, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs)))
      {
        // Attempt to extract the icon from the given executable straight away
        std::wstring SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\icon-original.png)", _path_cache.specialk_userdata, pApp->id);
        DeleteFile (SKIFCustomPath.c_str());
        SKIF_Util_SaveExtractExeIcon (SK_UTF8ToWideChar(charPath), SKIFCustomPath);
        
        RepopulateGames = true; // Rely on the RepopulateGames method instead

        // Clear variables
        error = false;
        strncpy (charName, "\0", MAX_PATH);
        strncpy (charPath, "\0", MAX_PATH);
        strncpy (charArgs, "\0", 500);

        update = true;

        ModifyGamePopup = PopupState_Closed;
        ImGui::CloseCurrentPopup();
      }
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    if (disabled)
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      ModifyGamePopup = PopupState_Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_DisallowMouseDragMove ( );

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }
  else {
    ModifyGamePopup = PopupState_Closed;
  }

  extern uint32_t SelectNewSKIFGame;

  if (SelectNewSKIFGame > 0)
  {
    // Change selection to the new game
    selection.appid = SelectNewSKIFGame;
    selection.store = app_record_s::Store::Other;
    for (auto& app : apps)
      if (app.second.id == selection.appid && app.second.store == selection.store)
        pApp = &app.second;

    update = true;

    SelectNewSKIFGame = 0;
  }

  // In case of a device reset, unload all currently loaded textures
  if (invalidatedDevice == 1)
  {   invalidatedDevice  = 2;

    extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
    if (pTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pTexSRV.p);
      pTexSRV.p = nullptr;
    }

    if (pPatTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pPatTexSRV.p);
      pPatTexSRV.p = nullptr;
    }

    if (pSKLogoTexSRV.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pSKLogoTexSRV.p);
      pSKLogoTexSRV.p = nullptr;
    }

    for (auto& app : apps)
    {
      if (app.second.tex_icon.texture.p != nullptr)
      {
        SKIF_ResourcesToFree.push(app.second.tex_icon.texture.p);
        app.second.tex_icon.texture.p = nullptr;
      }
    }

    // Trigger a refresh of the list of games, which will reload all icons and the Patreon texture
    RepopulateGames = true;
    // Trigger a refresh of the cover
    loadCover = true;
  }
}

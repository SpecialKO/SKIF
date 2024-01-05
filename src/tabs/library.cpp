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
bool                   steamRunning      = false;
bool                   steamFallback     = false;
bool                   loadCover         = false;
bool                   tryingToLoadCover = false;
std::atomic<bool>      gameCoverLoading  = false;
std::atomic<bool>      modDownloading    = false;
std::atomic<bool>      modInstalling     = false;
std::atomic<bool>      gameWorkerRunning = false;
std::atomic<uint32_t>  modAppId          = 0;

app_record_s::launch_config_s*
                       launchConfig      = nullptr; // Used to launch games. Defaults to primary launch config.
bool                   launchGame        = false; // Respects Instant Play preference
bool                   launchGameMenu    = false; // Menu is always explicit
bool                   launchInstant     = false;
bool                   launchWithoutSK   = false;

// Support up to 15 running games at once, lol
SKIF_Util_CreateProcess_s iPlayCache[15] = { };

const float fTintMin     = 0.75f;
      float fTint        = 1.0f;
      float fAlpha       = 0.0f;
      float fAlphaSK     = 0.0f;
      float fAlphaPrev   = 1.0f;
      
PopupState GameMenu        = PopupState_Closed;
PopupState EmptySpaceMenu  = PopupState_Closed;
PopupState CoverMenu       = PopupState_Closed;
PopupState IconMenu        = PopupState_Closed;
PopupState ServiceMenu     = PopupState_Closed;

PopupState AddGamePopup    = PopupState_Closed;
PopupState RemoveGamePopup = PopupState_Closed;
PopupState ModifyGamePopup = PopupState_Closed;
PopupState ConfirmPopup    = PopupState_Closed;

std::string confirmPopupTitle;
std::string confirmPopupText;

std::wstring dragDroppedFilePath = L"";

extern ImVec2          SKIF_vecAlteredSize;
extern float           SKIF_ImGui_GlobalDPIScale;
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern std::wstring    SKIF_Epic_AppDataPath;
extern DWORD           invalidatedDevice;
extern bool            GOGGalaxy_Installed;
extern std::wstring    GOGGalaxy_Path;
extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

#define _WIDTH   (378.0f * SKIF_ImGui_GlobalDPIScale) - (SKIF_vecAlteredSize.y > 0.0f ? ImGui::GetStyle().ScrollbarSize : 0.0f) // AppListInset1, AppListInset2, Injection_Summary_Frame (prev. 414.0f)
// 1038px == 415px
// 1000px == 377px (using 380px)
//#define _HEIGHT  (620.0f * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x - 2.0f) // AppListInset1
//#define _HEIGHT2 (280.0f * SKIF_ImGui_GlobalDPIScale)                                             // AppListInset2

struct terminate_process_s {
  uint32_t      pid = 0;
  HANDLE     handle = INVALID_HANDLE_VALUE;
  std::string  name = "";
} static static_proc;

std::atomic<int>  textureLoadQueueLength{ 1 };

int getTextureLoadQueuePos (void) {
  return textureLoadQueueLength.fetch_add(1) + 1;
}

CComPtr <ID3D11ShaderResourceView> pPatTexSRV;
CComPtr <ID3D11ShaderResourceView> pSKLogoTexSRV;
CComPtr <ID3D11ShaderResourceView> pSKLogoTexSRV_small;

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

  app_record_s::sk_install_state_s& sk_install =
    pApp->specialk.injection;

  app_id   = pApp->id;
  running  = pApp->_status.running;
  updating = pApp->_status.updating;
  autostop = _inject.bAckInj;

  service  = (sk_install.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
             (sk_install.injection.bitness == InjectionBitness::SixtyFour &&  _inject.pid64) ||
             (sk_install.injection.bitness == InjectionBitness::Unknown   && (_inject.pid32  &&
                                                                              _inject.pid64));

  wchar_t     wszDLLPath [MAX_PATH + 2];
  wcsncpy_s ( wszDLLPath, MAX_PATH,
                sk_install.injection.dll_path.c_str (),
                        _TRUNCATE );

  dll.full_path      = wszDLLPath;
  dll.full_path_utf8 = SK_WideCharToUTF8 (dll.full_path);

  PathStripPathW      (wszDLLPath);
  dll.shorthand      = wszDLLPath;
  dll.shorthand_utf8 = SK_WideCharToUTF8 (dll.shorthand);
  dll.version        = sk_install.injection.dll_ver;
  dll.version_utf8   = SK_WideCharToUTF8 (dll.version);

  wchar_t     wszConfigPath [MAX_PATH + 2];
  wcsncpy_s ( wszConfigPath, MAX_PATH,
                sk_install.config.file.c_str (),
                        _TRUNCATE );
  
  config.root_dir       = sk_install.config.dir;
  config.root_dir_utf8  = SK_WideCharToUTF8 (config.root_dir);
  config.full_path      = wszConfigPath;
  config.full_path_utf8 = SK_WideCharToUTF8 (config.full_path);
  PathStripPathW         (wszConfigPath);
  config.shorthand      = wszConfigPath;
  config.shorthand_utf8 = SK_WideCharToUTF8 (config.shorthand);

  injection.type        = CachedType::Unknown;
  injection.type_utf8   = "None";
  injection.status.text.clear ();
  injection.hover_text.clear  ();

  switch (sk_install.injection.type)
  {
    case InjectionType::Local:
      injection.type           = CachedType::Local;
      injection.type_utf8      = "Local";
      injection.type_version   = SK_FormatString (R"(v %s (%s))", dll.version_utf8.c_str(), dll.shorthand_utf8.c_str()); // injection.type_utf8.c_str()
      break;

    case InjectionType::Global:
    default: // Unknown injection strategy, but let's assume global would work

      if ( _inject.bHasServlet )
      {
        injection.type         = CachedType::Global;
        injection.type_utf8    = "Global";
        injection.type_version = SK_FormatString (R"(v %s)", dll.version_utf8.c_str()); // injection.type_utf8.c_str() // We don't actually have SKIF say "Global v XXX" any longer due to space constraints -- Aemony, 2024-01-04
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

  // Refresh the context menu cached data
  
  // Profile + Screenshots
  menu.profileFolderExists = PathFileExists (pApp->specialk.injection.config.dir.c_str());
  menu.wsScreenshotDir          = pApp->specialk.injection.config.dir + LR"(\Screenshots)";
  menu.screenshotsFolderExists  = (menu.profileFolderExists) ? PathFileExists (menu.wsScreenshotDir.c_str()) : false;

  // Check how many secondary launch configs are valid
  menu.numSecondaryLaunchConfigs = 0;
  for (auto& _launch_cfg : pApp->launch_configs)
  {
    if (_launch_cfg.first == 0)
      continue;

    if (! _launch_cfg.second.valid ||
          _launch_cfg.second.duplicate_exe_args)
      continue;
        
    menu.numSecondaryLaunchConfigs++;
  }

  // Steam Auto-Cloud
  menu.cloud_paths.clear();
  if (pApp->cloud_enabled && // If this is false, Steam Auto-Cloud is not enabled
    ! pApp->cloud_saves.empty ())
  {
    for (auto& cloud : pApp->cloud_saves)
    {
      if (cloud.second.valid == -1)
        cloud.second.valid =
          PathFileExistsW (cloud.second.evaluated_dir.c_str());

      if (cloud.second.valid == 0)
        continue;

      if ( app_record_s::Platform::Unknown == cloud.second.platforms ||
            app_record_s::supports (          cloud.second.platforms,
            app_record_s::Platform::Windows )
          )
        menu.cloud_paths.emplace_back (CloudPath (cloud.first, cloud.second.evaluated_dir));
    }
  }

  // PCGamingWiki
  menu.pcgwValue = (pApp->store == app_record_s::Store::Steam || pApp->store == app_record_s::Store::GOG)
                                            ?   std::to_wstring (pApp->id)
                                            : SK_UTF8ToWideChar (pApp->names.normal);

  menu.pcgwLink  = ((pApp->store == app_record_s::Store::GOG)   ? L"https://www.pcgamingwiki.com/api/gog.php?page="
                 :  (pApp->store == app_record_s::Store::Steam) ? L"https://www.pcgamingwiki.com/api/appid.php?appid="
                                                                : L"https://www.pcgamingwiki.com/w/index.php?search=")
                                                                  + menu.pcgwValue;
}

#pragma endregion


#pragma region UpdateGameCover

bool
UpdateGameCover (app_record_s* pApp, std::wstring_view path)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring targetPath = L"";
  std::wstring ext        = std::filesystem::path (path.data()).extension().wstring();

  if (ext == L".jpeg")
    ext = L".jpg";

  if (ext == L".webp")
    ext = L".png";

  // Unsupported file format
  if (ext != L".jpg" &&
      ext != L".png")
  {
    confirmPopupTitle = "Unsupported file format";
    confirmPopupText  = "Please use a supported image format:\n"
                        "\n"
                        "*.png\n"
                        "*.jpg\n"
                        "*.jpeg\n"
                        "*.webp (no animation)";
    ConfirmPopup = PopupState_Open;

    return false;
  }

  if (pApp->id == SKIF_STEAM_APPID)
    targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
  else if (pApp->store == app_record_s::Store::Custom)
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

    if (CopyFile (path.data(), (targetPath + ext).c_str(), false))
      return true;
  }

  return false;
}

#pragma endregion



#pragma region DrawGameContextMenu

void
DrawGameContextMenu (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_Lib_SummaryCache& _cache      = SKIF_Lib_SummaryCache::GetInstance ( );

  // TODO: Many of the below options needs to be adjusted to indicate if the primary launch option
  //         is even available (or if "without Special K" is the only option available)

  // Do not check games that are being updated (aka installed)
  //   nor expose the option for shell execute based games (e.g. Link2EA, which requires the Steam client running)
  bool SteamShortcutPossible = false;

  if (launchConfig != nullptr && pApp->store == app_record_s::Store::Steam && ! pApp->_status.updating)
    SteamShortcutPossible = launchConfig->isExecutableFullPathValid ( );
  
  // Push styling for Disabled
  ImGui::PushStyleColor      (ImGuiCol_TextDisabled,
    ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled)  * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
  );

  if ( ImGui::Selectable ("Play###GameContextMenu_Launch", false,
                          ((pApp->_status.running || pApp->_status.updating)
                            ? ImGuiSelectableFlags_Disabled
                            : ImGuiSelectableFlags_None)))
    launchGameMenu = true;

  if (pApp->specialk.injection.injection.type != InjectionType::Local)
  {
    if (! _inject.bCurrentState)
      SKIF_ImGui_SetHoverText ("Starts the injection service as well.");

    ImGui::PushStyleColor      (ImGuiCol_Text,
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
    );

    if ( ImGui::Selectable (ICON_FA_TOGGLE_OFF " without Special K###GameContextMenu_LaunchWoSK", false,
                            ((pApp->_status.running || pApp->_status.updating)
                              ? ImGuiSelectableFlags_Disabled
                              : ImGuiSelectableFlags_None)))
      launchGameMenu = launchWithoutSK = true;

    ImGui::PopStyleColor   ( );

    if (_inject.bCurrentState)
      SKIF_ImGui_SetHoverText ("Stops the injection service as well.");
  }
        
  // Instant Play options
  if (SteamShortcutPossible || pApp->store != app_record_s::Store::Steam)
  {
    if (_cache.menu.numSecondaryLaunchConfigs > 0 || (pApp->store == app_record_s::Store::Steam || pApp->store == app_record_s::Store::GOG))
      ImGui::Separator ( );

    // If there is only one valid launch config (Steam and GOG games only)
    if (_cache.menu.numSecondaryLaunchConfigs == 0 && (pApp->store == app_record_s::Store::Steam || pApp->store == app_record_s::Store::GOG))
    {
      if (ImGui::Selectable ("Instant play###GameContextMenu_InstantPlay", false,
                            ((pApp->_status.running || pApp->_status.updating)
                              ? ImGuiSelectableFlags_Disabled
                              : ImGuiSelectableFlags_None)))
        launchInstant = true;

      std::string hoverText = launchConfig->getExecutableFullPathUTF8();

      if (! launchConfig->getLaunchOptionsUTF8().empty())
        hoverText += (" " + launchConfig->getLaunchOptionsUTF8());

      if (pApp->store == app_record_s::Store::Steam && ! pApp->Steam_LaunchOption.empty())
        hoverText += (" " + pApp->Steam_LaunchOption);

      SKIF_ImGui_SetHoverText (hoverText.c_str());

      SKIF_ImGui_SetHoverTip  ("Skips the regular platform launch process for the game,\n"
                                "including steps such as cloud saves synchronization.");
          
      if (pApp->specialk.injection.injection.type != InjectionType::Local)
      {
        ImGui::PushStyleColor      (ImGuiCol_Text,
          ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
        );

        if (ImGui::Selectable (ICON_FA_TOGGLE_OFF " without Special K###GameContextMenu_InstantPlayWoSK", false,
                              ((pApp->_status.running || pApp->_status.updating)
                                ? ImGuiSelectableFlags_Disabled
                                : ImGuiSelectableFlags_None)))
          launchInstant = launchWithoutSK = true;

        ImGui::PopStyleColor   ( );
        
        SKIF_ImGui_SetHoverText (hoverText.c_str());
      }
    }

    // Multiple launch configs
    else if (_cache.menu.numSecondaryLaunchConfigs > 0)
    {
      bool disabled = (pApp->_status.running || pApp->_status.updating);

      if (disabled)
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

      if (ImGui::BeginMenu ("Instant play###GameContextMenu_InstantPlayMenu"))
      {
        bool sepCustomSKIF = true,
             sepCustomUser = true;

        for (auto& _launch_cfg : pApp->launch_configs)
        {
          if (! _launch_cfg.second.valid ||
                _launch_cfg.second.duplicate_exe_args)
            continue;

          auto& _launch = _launch_cfg.second;

          // Separators between official / user / SKIF launch configs
          if (_launch_cfg.second.custom_user && sepCustomUser)
          {
            sepCustomUser = false;
            ImGui::Separator ( );
          }

          else if (_launch_cfg.second.custom_skif && sepCustomSKIF)
          {
            sepCustomSKIF = false;
            ImGui::Separator ( );
          }

          bool blacklisted = (_launch.isBlacklisted ( ) || _inject._TestUserList (_launch.getExecutableFullPathUTF8 ( ).c_str (), false));

          char        szButtonLabel [256] = { };

          sprintf_s ( szButtonLabel, 255,
                        "%s%s%s###GameContextMenu_InstantPlayMenu-%d",
                                  (blacklisted) ? (ICON_FA_LOCK        "  ") : "",
                         (_launch.isElevated()) ? (ICON_FA_USER_SHIELD "  ") : "",
                          _launch.getDescriptionUTF8().empty ()
                            ? _launch.getExecutableFileNameUTF8().c_str ()
                            : _launch.getDescriptionUTF8().c_str (),
                          _launch.id);

          if (disabled || blacklisted)
          {
            ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
          }
          else
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

          if (ImGui::Selectable (szButtonLabel))
          {
            launchConfig = &_launch_cfg.second;
            launchInstant = true;
          }
          
          if (disabled || blacklisted)
          {
            ImGui::PopStyleColor  ( );
            ImGui::PopItemFlag    ( );
          }
          else
            ImGui::PopStyleColor  ( );

          std::string hoverText = _launch.getExecutableFullPathUTF8();

          if (! _launch.getLaunchOptionsUTF8().empty())
            hoverText += (" " + _launch.getLaunchOptionsUTF8());

          // Also add Steam defined launch options, but only if not using custom launch configs
          if (pApp->store == app_record_s::Store::Steam && ! pApp->Steam_LaunchOption.empty() &&
             ! _launch_cfg.second.custom_skif && ! _launch_cfg.second.custom_user)
            hoverText += (" " + pApp->Steam_LaunchOption);
          
          if (! disabled && ! blacklisted)
            SKIF_ImGui_SetMouseCursorHand ( );
          
          SKIF_ImGui_SetHoverText       (hoverText.c_str());

          if (blacklisted)
            SKIF_ImGui_SetHoverTip      ("Special K has been disabled for this launch configuration.");
        }

        ImGui::EndMenu ();
      }

      if (pApp->store == app_record_s::Store::Steam || pApp->store == app_record_s::Store::GOG)
        SKIF_ImGui_SetHoverTip  ("Skips the regular platform launch process for the game,\n"
                                  "including steps such as cloud saves synchronization.");
      
      if (! disabled)
        ImGui::PushStyleColor      (ImGuiCol_Text,
          ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase) * ImVec4 (1.0f, 1.0f, 1.0f, 0.7f)
        );

      if (ImGui::BeginMenu (ICON_FA_TOGGLE_OFF " without Special K###GameContextMenu_InstantPlayWoSKMenu"))
      {
        bool sepCustomSKIF = true,
             sepCustomUser = true;

        for (auto& _launch_cfg : pApp->launch_configs)
        {
          if (! _launch_cfg.second.valid ||
                _launch_cfg.second.duplicate_exe_args)
            continue;

          auto& _launch = _launch_cfg.second;

          // Separators between official / user / SKIF launch configs
          if (_launch_cfg.second.custom_user && sepCustomUser)
          {
            sepCustomUser = false;
            ImGui::Separator ( );
          }

          else if (_launch_cfg.second.custom_skif && sepCustomSKIF)
          {
            sepCustomSKIF = false;
            ImGui::Separator ( );
          }

          bool localDisabled = (_launch.injection.injection.type == InjectionType::Local);

          char        szButtonLabel [256] = { };

          sprintf_s ( szButtonLabel, 255,
                        "%s%s%s###GameContextMenu_InstantPlayWoSKMenu-%d",
                                (localDisabled) ? (ICON_FA_LOCK        "  ") : "",
                         (_launch.isElevated()) ? (ICON_FA_USER_SHIELD "  ") : "",
                          _launch.getDescriptionUTF8().empty ()
                            ? _launch.getExecutableFileNameUTF8().c_str ()
                            : _launch.getDescriptionUTF8().c_str (),
                          _launch.id);
          
          if (disabled || localDisabled)
          {
            ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
          }
          else
            ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

          if (ImGui::Selectable (szButtonLabel))
          {
            launchConfig = &_launch_cfg.second;
            launchInstant = launchWithoutSK = true;
          }
          
          if (disabled || localDisabled)
          {
            ImGui::PopStyleColor  ( );
            ImGui::PopItemFlag    ( );
          }
          else
            ImGui::PopStyleColor  ( );

          std::string hoverText = _launch.getExecutableFullPathUTF8();

          if (! _launch.getLaunchOptionsUTF8().empty())
            hoverText += (" " + _launch.getLaunchOptionsUTF8());

          if (! disabled && ! localDisabled)
            SKIF_ImGui_SetMouseCursorHand ( );
          
          SKIF_ImGui_SetHoverText       (hoverText.c_str());

          if (localDisabled)
            SKIF_ImGui_SetHoverTip      ("This option is not available due to a local injection being used.");
        }

        ImGui::EndMenu ();
      }
      
      if (! disabled)
        ImGui::PopStyleColor ( );
      
      if (disabled)
        ImGui::PopStyleColor ( );
    }
  }

  // GOG launch options
  /*
  if (GOGGalaxy_Installed && pApp->store == app_record_s::Store::GOG)
  {
    if (pApp->specialk.injection.injection.type != InjectionType::Local)
      ImGui::Separator        ( );

    if (ImGui::Selectable ("Play using GOG Galaxy###GameContextMenu_GalaxyLaunch", false,
                          ((pApp->_status.running || pApp->_status.updating)
                            ? ImGuiSelectableFlags_Disabled
                            : ImGuiSelectableFlags_None)))
      launchGalaxyGame = true;

    if (pApp->specialk.injection.injection.type != InjectionType::Local)
    {
      ImGui::PushStyleColor ( ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
      );

      if (ImGui::Selectable (ICON_FA_TOGGLE_OFF " without Special K###GameContextMenu_GalaxyLaunchWoSK", false,
                            ((pApp->_status.running || pApp->_status.updating)
                              ? ImGuiSelectableFlags_Disabled
                              : ImGuiSelectableFlags_None)))
        launchGalaxyGame = launchWithoutSK = true;

      ImGui::PopStyleColor  ( );
    }
  }
  */

  if (pApp->_status.running)
  {
    SKIF_Util_CreateProcess_s* mon_app = nullptr;
    for (auto& monitored_app : iPlayCache)
      if (monitored_app.id == pApp->id && monitored_app.store_id == (int)pApp->store)
        mon_app = &monitored_app;

    if (mon_app != nullptr)
    {
      HANDLE hProcess = mon_app->hProcess.load();

      ImGui::Separator ( );

      if (ImGui::Selectable (ICON_FA_SQUARE_XMARK "  Terminate game", false,
                            ((hProcess == INVALID_HANDLE_VALUE)
                              ? ImGuiSelectableFlags_Disabled
                              : ImGuiSelectableFlags_None)))
      {
        static_proc.pid    = 1337;
        static_proc.handle = hProcess;
        static_proc.name   = pApp->names.normal;
      }
    }

    else if (pApp->_status.running_pid != 0)
    {
      ImGui::Separator ( );

      if (ImGui::Selectable (ICON_FA_SQUARE_XMARK "  Terminate game"))
      {
        static_proc.pid    = pApp->_status.running_pid;
        static_proc.name   = pApp->names.normal;
      }
    }
  }

  // Pop styling for Disabled
  ImGui::PopStyleColor   ( );

  ImGui::Separator ( );
  // ==============================

  if (ImGui::BeginMenu (ICON_FA_FOLDER "  Browse"))
  {
    ImVec2 iconPos = ImGui::GetCursorPos();

    ImGui::BeginGroup  ( );
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN)       .x, ImGui::GetTextLineHeight()));
    ImGui::EndGroup    ( );
    ImGui::SameLine    ( );
    ImGui::BeginGroup  ( );
    if (ImGui::Selectable         ("Game folder", false, ImGuiSelectableFlags_SpanAllColumns))
      SKIF_Util_ExplorePath       (pApp->install_dir);
    SKIF_ImGui_SetMouseCursorHand ( );
    SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->install_dir));
    ImGui::EndGroup    ( );

    ImGui::SetCursorPos  (iconPos);

    ImGui::TextColored (
              ImColor (255, 207, 72),
                ICON_FA_FOLDER_OPEN
                          );

    // If Profile Folder Exists
    if (_cache.menu.profileFolderExists)
    {
      ImGui::BeginGroup  ( );
      ImVec2 iconPosDummy = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN) .x, ImGui::GetTextLineHeight()));
      if (_cache.menu.screenshotsFolderExists)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_IMAGES)       .x, ImGui::GetTextLineHeight()));
        
      ImGui::EndGroup    ( );
      ImGui::SameLine    ( );
      ImGui::BeginGroup  ( );

      // Config Root
      if (ImGui::Selectable         ("Profile folder", false, ImGuiSelectableFlags_SpanAllColumns))
        SKIF_Util_ExplorePath       (pApp->specialk.injection.config.dir);
      SKIF_ImGui_SetMouseCursorHand ();
      SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (pApp->specialk.injection.config.dir.c_str()).c_str());
        
      if (_cache.menu.screenshotsFolderExists)
      {
        // Screenshot Folder
        if (ImGui::Selectable         ("Screenshots", false, ImGuiSelectableFlags_SpanAllColumns))
          SKIF_Util_ExplorePath       (_cache.menu.wsScreenshotDir);
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (SK_WideCharToUTF8 (_cache.menu.wsScreenshotDir.data()).c_str());
      }

      ImGui::EndGroup  ( );

      ImGui::SetCursorPos  (iconPosDummy);

      ImGui::TextColored (
        ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), // ImColor (255, 207, 72),
                  ICON_FA_FOLDER_OPEN
                            );
      if (_cache.menu.screenshotsFolderExists)
        ImGui::TextColored (
                  ImColor   (200, 200, 200, 255),
                    ICON_FA_IMAGES
                              );
    }

    if (! _cache.menu.cloud_paths.empty())
    {
      ImGui::Separator  ( );

      if (ImGui::BeginMenu (ICON_FA_SD_CARD "   Save/config folders"))
      {
        for (auto& folder : _cache.menu.cloud_paths)
        {
      
          ImGui::PushStyleColor ( ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) // * ImVec4(1.0f, 1.0f, 1.0f, 1.0f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
          );

          if (ImGui::Selectable         (folder.label.c_str()))
          {
            SKIF_Util_ExplorePath       (folder.path);

            ImGui::CloseCurrentPopup    ( );
          }

          ImGui::PopStyleColor  ( );

          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (folder.path_utf8);
        }

        ImGui::EndMenu ();
      }
    }

    ImGui::EndMenu ();
  }

#if 0
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
#endif

  // Manage [Custom] Game
  if (pApp->store == app_record_s::Store::Custom || pApp->store == app_record_s::Store::GOG || SteamShortcutPossible)
  {
    if (ImGui::BeginMenu (ICON_FA_GEAR "  Manage"))
    {
      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos ( );

      if (pApp->store == app_record_s::Store::Steam)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));

      else if (pApp->store == app_record_s::Store::Custom)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_GEARS).x, ImGui::GetTextLineHeight()));
      
      ImGui::ItemSize     (ImVec2 (ImGui::CalcTextSize (ICON_FA_PAPERCLIP).x, ImGui::GetTextLineHeight()));

      if (pApp->store == app_record_s::Store::Custom)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TRASH).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup    ( );
      ImGui::SameLine    ( );
      ImGui::BeginGroup  ( );

      if (pApp->store == app_record_s::Store::Steam)
      {
        if (ImGui::Selectable  ("Steam", false, ImGuiSelectableFlags_SpanAllColumns))
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

        ImGui::Separator ( );
      }

      else if (pApp->store == app_record_s::Store::Custom)
      {
        if (ImGui::Selectable ("Properties", false, ImGuiSelectableFlags_SpanAllColumns))
          ModifyGamePopup = PopupState_Open;

        ImGui::Separator ( );
      }

      if (ImGui::Selectable ("Create desktop shortcut", false, ImGuiSelectableFlags_SpanAllColumns))
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

        name = SKIF_Util_StripInvalidFilenameChars (name);

        std::wstring linkPath = SK_FormatStringW (LR"(%ws\%ws.lnk)", std::wstring(_path_cache.desktop.path).c_str(), SK_UTF8ToWideChar(name).c_str());
        std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", pApp->launch_configs[0].getExecutableFullPath().c_str(), pApp->launch_configs[0].getLaunchOptions().c_str());

        // Trim spaces at the end
        linkArgs.erase (std::find_if (linkArgs.rbegin(), linkArgs.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), linkArgs.end());

        if (pApp->store == app_record_s::Store::Steam)
          linkArgs += L" SKIF_SteamAppId=" + std::to_wstring (pApp->id);

        confirmPopupTitle = "Create desktop shortcut";

        if (SKIF_Util_CreateShortcut (
            linkPath.c_str(),
            _path_cache.skif_executable,
            linkArgs.c_str(),
            pApp->launch_configs[0].working_dir.c_str(),
            SK_UTF8ToWideChar(name).c_str(),
            pApp->launch_configs[0].getExecutableFullPath().c_str()
            )
          )
          confirmPopupText = "A desktop shortcut has been created.";
        else
          confirmPopupText = "Failed to create a desktop shortcut!";

        ConfirmPopup = PopupState_Open;
      }

      if (pApp->store == app_record_s::Store::Custom)
      {
        if (ImGui::Selectable ("Remove"))
          RemoveGamePopup = PopupState_Open;
      }
      
      ImGui::EndGroup      ( );

      ImGui::SetCursorPos  (iconPos);

      if (pApp->store == app_record_s::Store::Steam)
      {
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
            ICON_FA_STEAM_SYMBOL );

        ImGui::Separator ( );
      }

      else if (pApp->store == app_record_s::Store::Custom)
      {
        ImGui::TextColored (
                  ImColor   (200, 200, 200, 255),
                    ICON_FA_GEARS
                              );

        ImGui::Separator ( );
      }
      
      ImGui::TextColored (
                  ImColor   (200, 200, 200, 255),
                    ICON_FA_PAPERCLIP
                              );

      if (pApp->store == app_record_s::Store::Custom)
        ImGui::TextColored (
                  ImColor   (200, 200, 200, 255),
                    ICON_FA_TRASH
                              );

      ImGui::EndMenu ( );
    }
  }
  
  if (ImGui::BeginMenu (ICON_FA_SHARE "  Open website"))
  {
    ImGui::BeginGroup  ( );
    ImVec2 iconPos = ImGui::GetCursorPos ( );

    if (pApp->store == app_record_s::Store::GOG)
    {
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
    }

    else if (pApp->store == app_record_s::Store::Steam)
    {
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
    }

    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_SCREWDRIVER_WRENCH).x, ImGui::GetTextLineHeight()));

    ImGui::EndGroup    ( );
    ImGui::SameLine    ( );
    ImGui::BeginGroup  ( );

    if (pApp->store == app_record_s::Store::GOG)
    {
      if (ImGui::Selectable  ("GOG Database", false, ImGuiSelectableFlags_SpanAllColumns))
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

    else if (pApp->store == app_record_s::Store::Steam)
    {
      if (ImGui::Selectable  ("Steam Community", false, ImGuiSelectableFlags_SpanAllColumns))
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

      if (ImGui::Selectable  ("SteamDB", false, ImGuiSelectableFlags_SpanAllColumns))
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

    if (ImGui::Selectable  ("PCGamingWiki", false, ImGuiSelectableFlags_SpanAllColumns))
    {
      SKIF_Util_OpenURI (_cache.menu.pcgwLink.c_str());
    }
    else
    {
      SKIF_ImGui_SetMouseCursorHand ( );
      SKIF_ImGui_SetHoverText       (
        SK_WideCharToUTF8 (_cache.menu.pcgwLink).c_str()
      );
    }

    ImGui::EndGroup      ( );

    ImGui::SetCursorPos  (iconPos);

    if (pApp->store == app_record_s::Store::GOG)
    {
      ImGui::TextColored (
        ImColor   (155, 89, 182, 255),
          ICON_FA_DATABASE );
    }

    else if (pApp->store == app_record_s::Store::Steam)
    {
      ImGui::TextColored (
        (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
          ICON_FA_STEAM_SYMBOL );

      ImGui::TextColored (
        ImColor   (101, 192, 244, 255).Value,
          ICON_FA_DATABASE );
    }

    ImGui::TextColored (
              ImColor   (200, 200, 200, 255),
                ICON_FA_SCREWDRIVER_WRENCH
                          );

    ImGui::EndMenu ( );
  }

  
  if (_registry.bDeveloperMode)
  {
    ImGui::Separator ( );

    if (ImGui::BeginMenu (ICON_FA_TOOLBOX "  Developer"))
    {
      if (! pApp->branches.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_CODE_BRANCH "  Branches");

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
                ImGui::MenuItem ( "Description",
                            branch.getDescAsUTF8 ().c_str () );

              ImGui::MenuItem ( "App Build #",
                                  std::to_string (
                                                  branch.build_id
                                                  ).c_str ()
              );

              if (branch.time_updated > 0)
                ImGui::MenuItem ( "Last Update", branch.getTimeAsCStr ().c_str () );

              ImGui::MenuItem ( "Accessibility", branch.pwd_required ?
                                        "Private (password required)" :
                                              "Public" );

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }

        ImGui::Separator ( );
      }

      // Epic and Xbox platforms use fake app IDs hashed from their unique text-based platform identifier
      if (ImGui::Selectable ("Copy app ID"))
      {
        switch (pApp->store)
        {
        case app_record_s::Store::Epic:
          SKIF_Util_SetClipboardData (SK_UTF8ToWideChar (pApp->Epic_AppName));
          break;
        case app_record_s::Store::Xbox:
          SKIF_Util_SetClipboardData (SK_UTF8ToWideChar (pApp->Xbox_PackageName));
          break;
        default:
          SKIF_Util_SetClipboardData (std::to_wstring   (pApp->id));
        }
      }

      SKIF_ImGui_SetHoverText (
        (pApp->store == app_record_s::Store::Epic) ? pApp->Epic_AppName     :
        (pApp->store == app_record_s::Store::Xbox) ? pApp->Xbox_PackageName :
                                     std::to_string (pApp->id)
      );

      ImGui::EndMenu ();
    }
  }
}

void
DrawSKIFContextMenu (app_record_s* pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  ImVec2 iconPos;

  ImGui::BeginGroup  ( );
  iconPos = ImGui::GetCursorPos();

  if (! _inject.bCurrentState)
  {
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TOGGLE_ON)       .x, ImGui::GetTextLineHeight()));
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TOGGLE_ON)       .x, ImGui::GetTextLineHeight()));
  }

  else {
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TOGGLE_OFF)       .x, ImGui::GetTextLineHeight()));
  }

  ImGui::EndGroup    ( );
  ImGui::SameLine    ( );
  ImGui::BeginGroup  ( );
  
  if (! _inject.bCurrentState)
  {
    if (ImGui::Selectable ("Start service", false, ImGuiSelectableFlags_SpanAllColumns))
    {
      _inject._StartStopInject (_inject.bCurrentState, true);
    }
    if (ImGui::Selectable ("Start service (manual stop)", false, ImGuiSelectableFlags_SpanAllColumns))
    {
      _inject._StartStopInject (_inject.bCurrentState, false);
    }
  }

  else {
    if (ImGui::Selectable("Stop service", false, ImGuiSelectableFlags_SpanAllColumns))
    {
      _inject._StartStopInject (_inject.bCurrentState);
    }
  }

  ImGui::EndGroup    ( );
  ImGui::SetCursorPos  (iconPos);
  
  if (! _inject.bCurrentState)
  {
    ImGui::TextColored (
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success),
                ICON_FA_TOGGLE_ON
                          );

    ImGui::TextColored (
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success),
                ICON_FA_TOGGLE_ON
                          );
  }

  else {
    ImGui::TextColored (
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning),
                ICON_FA_TOGGLE_OFF
                          );
  }

  ImGui::Separator ( ); // ==============================

  if (ImGui::BeginMenu (ICON_FA_FOLDER "  Browse"))
  {
    ImGui::BeginGroup  ( );
    iconPos = ImGui::GetCursorPos();
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN)       .x, ImGui::GetTextLineHeight()));
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN)       .x, ImGui::GetTextLineHeight()));
    ImGui::EndGroup    ( );
    ImGui::SameLine    ( );
    ImGui::BeginGroup  ( );
    if (ImGui::Selectable ("Install folder", false, ImGuiSelectableFlags_SpanAllColumns))
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
    if (ImGui::Selectable ("Profile folders", false, ImGuiSelectableFlags_SpanAllColumns))
    {
      SKIF_Util_ExplorePath (pApp->specialk.profile_dir);
    }
    else
    {
      SKIF_ImGui_SetMouseCursorHand ( );
      SKIF_ImGui_SetHoverText       (
        SK_WideCharToUTF8 (pApp->specialk.profile_dir)
                                      );
    }
    ImGui::EndGroup    ( );

    ImGui::SetCursorPos  (iconPos);

    ImGui::TextColored (
              ImColor (255, 207, 72),
                ICON_FA_FOLDER_OPEN
                          );

    ImGui::TextColored (
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info),
                ICON_FA_FOLDER_OPEN
                          );

    ImGui::EndMenu ( );
  }

  ImGui::Separator ( ); // ==============================
  
  ImGui::BeginGroup (  );
  iconPos = ImGui::GetCursorPos();
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
  if (ImGui::Selectable ("Wiki", false, ImGuiSelectableFlags_SpanAllColumns))
  {
    SKIF_Util_OpenURI (
      L"https://wiki.special-k.info/"
    );
  }
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/");


  if (ImGui::Selectable ("Discord", false, ImGuiSelectableFlags_SpanAllColumns))
  {
    SKIF_Util_OpenURI (
      L"https://discord.gg/specialk"
    );
  }
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://discord.gg/specialk");


  if (ImGui::Selectable ("Forum", false, ImGuiSelectableFlags_SpanAllColumns))
  {
    SKIF_Util_OpenURI (
      L"https://discourse.differentk.fyi/"
    );
  }
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://discourse.differentk.fyi/");


  if (ImGui::Selectable ("Patreon", false, ImGuiSelectableFlags_SpanAllColumns))
  {
    SKIF_Util_OpenURI (
      L"https://www.patreon.com/Kaldaien"
    );
  }
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText       ("https://www.patreon.com/Kaldaien");

  if (ImGui::Selectable ("GitHub", false, ImGuiSelectableFlags_SpanAllColumns))
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
  
  ImGui::Separator ( ); // ==============================

  ImGui::PushStyleColor ( ImGuiCol_Text,
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
  );

  std::wstring pcgwValue =   (pApp->store == app_record_s::Store::Custom || pApp->store == app_record_s::Store::Epic || pApp->store == app_record_s::Store::Xbox)
                            ? SK_UTF8ToWideChar (pApp->names.normal)
                            : std::to_wstring   (pApp->id);

  std::wstring pcgwLink =   ((pApp->store == app_record_s::Store::GOG)   ? L"https://www.pcgamingwiki.com/api/gog.php?page="
                            : (pApp->store == app_record_s::Store::Steam) ? L"https://www.pcgamingwiki.com/api/appid.php?appid="
                                                                          : L"https://www.pcgamingwiki.com/w/index.php?search=") + pcgwValue;

  ImGui::BeginGroup  ( );
  iconPos = ImGui::GetCursorPos ( );

  ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_SCREWDRIVER_WRENCH).x, ImGui::GetTextLineHeight()));

  if (SKIF_STEAM_OWNER)
  {
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
    ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
  }
  ImGui::EndGroup    ( );
  ImGui::SameLine    ( );
  ImGui::BeginGroup  ( );
  if (ImGui::Selectable  ("PCGamingWiki", false, ImGuiSelectableFlags_SpanAllColumns))
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

  if (SKIF_STEAM_OWNER)
  {
    if (ImGui::Selectable  ("Steam", false, ImGuiSelectableFlags_SpanAllColumns))
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
  }
  ImGui::EndGroup      ( );
  ImGui::PopStyleColor ( );

  ImGui::SetCursorPos  (iconPos);

  ImGui::TextColored (
            ImColor   (200, 200, 200, 255),
              ICON_FA_SCREWDRIVER_WRENCH
                        );

  if (SKIF_STEAM_OWNER)
  {
    ImGui::TextColored (
      (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
        ICON_FA_STEAM_SYMBOL );
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
      if (SKIF_Util_CompareVersionStrings (_inject.SKVer32, _cache.dll.version) > 0)
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
    int iBinaryType = SKIF_Util_GetBinaryType (_cache.dll.full_path.c_str());
    if (iBinaryType > 0)
    {
      wchar_t                       wszPathToGlobalDLL [MAX_PATH + 2] = { };
      GetModuleFileNameW  (nullptr, wszPathToGlobalDLL, MAX_PATH);
      PathRemoveFileSpecW (         wszPathToGlobalDLL);
      PathAppendW         (         wszPathToGlobalDLL, (iBinaryType == 2) ? L"SpecialK64.dll" : L"SpecialK32.dll");

      if (CopyFile (wszPathToGlobalDLL, _cache.dll.full_path.c_str(), FALSE))
      {
        PLOG_INFO << "Successfully updated " << _cache.dll.full_path << " from v " << _cache.dll.version << " to v " << _inject.SKVer32;
      }

      else {
        PLOG_ERROR << "Failed to copy " << wszPathToGlobalDLL << " to " << _cache.dll.full_path;
        PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
      }
    }

    else {
      PLOG_ERROR << "Failed to retrieve binary type from " << _cache.dll.full_path << " -- returned: " << iBinaryType;
      PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
    }
  };

#pragma endregion

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
  //ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  //ImGui::NewLine          ();
  //ImGui::TextUnformatted  ("Injection:");
  ImGui::TextUnformatted  ("Special K");
  ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
  ImGui::TextUnformatted  ("Config folder:");
  ImGui::TextUnformatted  ("Config file:");
  ImGui::TextUnformatted  ("Platform:");
  ImGui::PopStyleColor    ();
  ImGui::ItemSize         (ImVec2 (105.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 1 should have min-width 105px (scaled with the DPI)
  ImGui::EndGroup         ();

  ImGui::SameLine         ();

  // Column 2
  ImGui::BeginGroup       ();

  // Injection
  if (! _cache.dll.shorthand.empty ())
  {
    //ImGui::TextUnformatted  (cache.dll.shorthand.c_str  ());
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowItemOverlap;

    if (_cache.injection.type == SKIF_Lib_SummaryCache::CachedType::Global)
      flags |= ImGuiSelectableFlags_Disabled;

    bool openLocalMenu = false;

    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    if (ImGui::Selectable (_cache.injection.type_version.c_str(), false, flags))
      openLocalMenu = true;
    ImGui::PopStyleColor();

    if (_cache.injection.type == SKIF_Lib_SummaryCache::CachedType::Local)
    {
      SKIF_ImGui_SetMouseCursorHand ( );

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openLocalMenu = true;
    }

    SKIF_ImGui_SetHoverText       (_cache.dll.full_path_utf8.c_str());
        
    if (openLocalMenu && ! ImGui::IsPopupOpen ("LocalDLLMenu"))
      ImGui::OpenPopup    ("LocalDLLMenu");

    if (ImGui::BeginPopup ("LocalDLLMenu", ImGuiWindowFlags_NoMove))
    {
      if (_IsLocalDLLFileOutdated( ))
      {
        if (ImGui::Selectable (("Update to v " + _inject.SKVer32_utf8).c_str( )))
          _UpdateLocalDLLFile ( );

        ImGui::Separator ( );
      }

      if (ImGui::Selectable ("Uninstall"))
      {
        if (DeleteFile (_cache.dll.full_path.c_str()))
          PLOG_INFO << "Successfully uninstalled local DLL v " << _cache.dll.version << " from " << _cache.dll.full_path;
      }

      ImGui::EndPopup ( );
    }
  }

  else
    ImGui::TextUnformatted ("N/A");

  // Config Root
  // Config File
  if (! _cache.config.shorthand.empty ())
  {
    // Config Root
    if (ImGui::Selectable         (_cache.config_repo.c_str ()))
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (_cache.config.root_dir, ec))
            std::filesystem::create_directories (_cache.config.root_dir, ec);

      SKIF_Util_ExplorePath       (_cache.config.root_dir);
    }
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (_cache.config.root_dir_utf8.c_str ());

    // Config File
    if (ImGui::Selectable         (_cache.config.shorthand_utf8.c_str ()))
    {
      std::error_code ec;
      // Create any missing directories
      if (! std::filesystem::exists             (_cache.config.root_dir, ec))
            std::filesystem::create_directories (_cache.config.root_dir, ec);

      HANDLE h = CreateFile (_cache.config.full_path.c_str(),
                      GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL,
                            CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,
                                NULL );

      // We need to close the handle as well, as otherwise apps will think the file
      //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
      if (h != INVALID_HANDLE_VALUE)
        CloseHandle (h);

      SKIF_Util_OpenURI (_cache.config.full_path.c_str(), SW_SHOWNORMAL, NULL);
    }
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (_cache.config.full_path_utf8.c_str ());


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
        HANDLE hFind        = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATA ffd = { };;
        std::vector<Preset> tmpPresets;

        hFind = 
          FindFirstFileExW ((folder + find_pattern).c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

        if (INVALID_HANDLE_VALUE != hFind)
        {
          do {
            Preset newPreset = { PathFindFileName (ffd.cFileName), folder + ffd.cFileName };
            bool         add = false;
            LONG_PTR   size = 0;

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
              CHandle hPreset (
                CreateFileW (newPreset.Path.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,        OPEN_EXISTING,
                                    GetFileAttributesW (newPreset.Path.c_str()),
                                      nullptr
                            )
              );

              if (hPreset != INVALID_HANDLE_VALUE)
              {
                DWORD dwRead     = 0;

                auto szPresetData =
                  std::make_unique <char []> (
                    std::size_t (size) + std::size_t (1)
                  );

                auto preset_data =
                  szPresetData.get ();

                if (preset_data)
                {
                  const bool bRead =
                    ReadFile (hPreset, preset_data, static_cast<DWORD>(size), &dwRead, nullptr);

                  if (bRead && dwRead)
                  {
                    std::string string =
                      std::move (preset_data);
                    
                    // Skip files only containing byte order marks
                    if (string != "\xEF\xBB\xBF"     && // UTF-8,  with BOM
                        string != "\xFF\xFE"         && // UTF-16, little endian
                        string != "\xFE\xFF"         && // UTF-16, big endian
                        string != "\xFF\xFE\x00\x00" && // UTF-32, little endian
                        string != "\x00\x00\xFE\xFF"  ) // UTF-32, big endian
                      add = true;
                  }
                }
              }
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
                CopyFile (preset.Path.c_str(), _cache.config.full_path.c_str(), FALSE);
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
                CopyFile (preset.Path.c_str(), _cache.config.full_path.c_str(), FALSE);
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
        std::wofstream config_file(_cache.config.full_path.c_str());

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
        HANDLE h = CreateFile ( _cache.config.full_path.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL,
                          TRUNCATE_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                              NULL );

        // We need to close the handle as well, as otherwise apps will think the file
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
    ImGui::TextUnformatted (_cache.config_repo.c_str ());
    ImGui::TextUnformatted ("N/A");
  }

  // Platform
  ImGui::TextUnformatted  (pApp->store_utf8.c_str());

  ImGui::ItemSize         (ImVec2 (95.f * SKIF_ImGui_GlobalDPIScale, 0.f)); // Column 2 should have min-width 95px (scaled with the DPI) (a bit smaller to allow for some right-hand padding for "Waiting for game...")
  ImGui::EndGroup         ( );
  ImGui::SameLine         ( );

  // Column 3
  ImGui::BeginGroup       ( );

  static bool quickServiceHover = false;

  // Service quick toogle / Waiting for game...
  if (_cache.injection.type == SKIF_Lib_SummaryCache::CachedType::Global && ! _inject.isPending())
  {
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImColor(0, 0, 0, 0).Value);
    ImGui::PushStyleColor(ImGuiCol_Text, (quickServiceHover) ? _cache.injection.status.color_hover.Value
                                                             : _cache.injection.status.color.Value);

    if (ImGui::Selectable (_cache.injection.status.text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
    {
      _inject._StartStopInject (_cache.service, _registry.bStopOnInjection, pApp->launch_configs[0].isElevated ( ));

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
      

  if (_cache.injection.type == SKIF_Lib_SummaryCache::CachedType::Local)
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
      ImVec2 (((_registry.bUIBorders) ? 104.0f : 105.0f) * SKIF_ImGui_GlobalDPIScale,
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

  std::string      buttonLabel   = ICON_FA_GAMEPAD "  Play";
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
      ) && _cache.injection.type != SKIF_Lib_SummaryCache::CachedType::Local)
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
  if ((! _inject.bHasServlet && _cache.injection.type != SKIF_Lib_SummaryCache::CachedType::Local) || buttonPending)
    SKIF_ImGui_PushDisableState ( );

  if (ImGui::ButtonEx (
              buttonLabel.c_str (),
                  ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                            50.0f * SKIF_ImGui_GlobalDPIScale ), buttonFlags ))
  {
    if (! buttonInstall)
      launchGame = true;

    else
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

          SKIF_Util_SetThreadPowerThrottling (GetCurrentThread(), 1); // Enable EcoQoS for this thread
          SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

          PLOG_DEBUG << "SKIF_LibGameModWorker thread started!";

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

          PLOG_DEBUG << "SKIF_LibGameModWorker thread stopped!";

          SetThreadPriority    (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);
        }, 0x0, NULL);
      }
    }
  }
      
  // Disable the button for the injection service types if the servlets are missing
  if ((! _inject.bHasServlet && _cache.injection.type != SKIF_Lib_SummaryCache::CachedType::Local) || buttonPending)
    SKIF_ImGui_PopDisableState  ( );

  if (buttonPending)
    SKIF_ImGui_SetHoverTip ("Please finish the ongoing mod installation first.");

  if (pApp->_status.running || pApp->_status.updating)
    ImGui::PopStyleColor ( );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right) &&
      GameMenu == PopupState_Closed)
  {
    GameMenu = PopupState_Open;
  }

  if (pApp->extended_config.vac.enabled == 1)
  {
    ImGui::SameLine ( );
    ImGui::SetCursorPosY (50.0f + ImGui::GetStyle().FramePadding.y * SKIF_ImGui_GlobalDPIScale);
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
    SKIF_ImGui_SetHoverTip ("Warning: VAC protected game; injection is not recommended!");
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
    SKIF_Steam_GetInjectionStrategy (pApp);

    // Not actually used atm, so no need to scan the profile folder either
#if 0

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
#endif

  }
  
  // Handle GOG, Epic, SKIF Custom, and Xbox games
  else {
    
    auto& launch = pApp->launch_configs.begin()->second;

    // Assume global
    launch.injection.injection.type =
      InjectionType::Global;
    launch.injection.injection.entry_pt =
      InjectionPoint::CBTHook;
    launch.injection.config.type =
      ConfigType::Centralized;
    launch.injection.config.file =
      L"SpecialK.ini";

    if (launch.injection.injection.bitness == InjectionBitness::Unknown)
    {
      DWORD dwBinaryType = MAXDWORD;
      if (GetBinaryTypeW (launch.getExecutableFullPath ( ).c_str (), &dwBinaryType))
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          launch.injection.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          launch.injection.injection.bitness = InjectionBitness::SixtyFour;
      }
    }
    // End checking bitness

    struct {
      InjectionPoint   entry_pt;
      std::wstring     name;
      std::wstring     path;
    } test_dlls [] = {
      { InjectionPoint::DDraw,   L"ddraw",    L"" },
      { InjectionPoint::D3D8,    L"d3d8",     L"" },
      { InjectionPoint::D3D9,    L"d3d9",     L"" },
      { InjectionPoint::DXGI,    L"dxgi",     L"" },
      { InjectionPoint::D3D11,   L"d3d11",    L"" },
      { InjectionPoint::OpenGL,  L"OpenGL32", L"" },
      { InjectionPoint::DInput8, L"dinput8",  L"" }
    };

    std::wstring test_paths[] = { 
      pApp->launch_configs[0].getExecutableDir ( ),
      pApp->launch_configs[0].working_dir
    };

    if (test_paths[0] == test_paths[1])
      test_paths[1] = L"";

    bool breakOuterLoop = false;
    for ( auto& test_path : test_paths)
    {
      if (test_path.empty())
        continue;
      
      std::wstring test_pattern =
        test_path + LR"(\*.dll)";

      WIN32_FIND_DATA ffd         = { };
      HANDLE          hFind       =
        FindFirstFileExW (test_pattern.c_str(), FindExInfoBasic, &ffd, FindExSearchNameMatch, NULL, NULL);

      if (hFind != INVALID_HANDLE_VALUE)
      {
        do
        {
          if ( wcscmp (ffd.cFileName, L".")  == 0 ||
               wcscmp (ffd.cFileName, L"..") == 0 )
            continue;

          if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

          for ( auto& dll : test_dlls )
          {
            // Filename + extension
            dll.path = dll.name + L".dll";

            if (ffd.cFileName != dll.path)
              continue;
          
            // Full path
            dll.path = test_path + LR"(\)" + dll.path;

            std::wstring dll_ver =
              SKIF_Util_GetSpecialKDLLVersion (dll.path.c_str ());

            if (dll_ver.empty ())
              continue;

            launch.injection.injection = {
              launch.injection.injection.bitness,
              dll.entry_pt, InjectionType::Local,
              dll.path,     dll_ver
            };

            if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
              launch.injection.config.type =   ConfigType::Centralized;
            else
              launch.injection.config      = { ConfigType::Localized, test_path };

            launch.injection.config.file =
              dll.name + L".ini";

            breakOuterLoop = true;
            break;
          }

          if (breakOuterLoop)
            break;

        } while (FindNextFile (hFind, &ffd));

        FindClose (hFind);
      }

      if (breakOuterLoop)
        break;
    }

    // Main UI stuff should follow the primary launch config
    pApp->specialk.injection = pApp->launch_configs[0].injection;

    if ( InjectionType::Global ==
        pApp->specialk.injection.injection.type )
    {
      // Assume Global 32-bit if we don't know otherwise
      bool bIs64Bit =
        (launch.injection.injection.bitness ==
                          InjectionBitness::SixtyFour );

      pApp->specialk.injection.config.type =
        ConfigType::Centralized;

      wchar_t                 wszPathToSelf [MAX_PATH + 2] = { };
      GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
      PathRemoveFileSpecW (   wszPathToSelf);
      PathAppendW         (   wszPathToSelf,
                                bIs64Bit ? L"SpecialK64.dll"
                                         : L"SpecialK32.dll" );

      pApp->specialk.injection.injection.dll_path = wszPathToSelf;
      pApp->specialk.injection.injection.dll_ver  = 
                                bIs64Bit ? _inject.SKVer64
                                         : _inject.SKVer32;
    }

    if ( ConfigType::Centralized ==
           pApp->specialk.injection.config.type )
    {
      pApp->specialk.injection.config.dir =
        SK_FormatStringW ( LR"(%ws\Profiles\%ws)",
                             _path_cache.specialk_userdata,
                              pApp->specialk.profile_dir.c_str());
    }

    pApp->specialk.injection.config.file =
      ( pApp->specialk.injection.config.dir + LR"(\)" ) +
        pApp->specialk.injection.config.file;
  }

}

#pragma endregion


#pragma region RefreshRunningApps

void
RefreshRunningApps (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  
  static DWORD lastGameRefresh = 0;
  static std::wstring exeSteam = L"steam.exe";

  DWORD current_time = SKIF_Util_timeGetTime ( );

  if (current_time > lastGameRefresh + 5000 && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( )))
  {
    bool new_steamRunning = false;

    for (auto& app : g_apps)
    {
      if (app.second._status.dwTimeDelayChecks > current_time)
        continue;

      app.second._status.running_pid = 0;

      if (app.second.store == app_record_s::Store::Steam && (steamRunning || ! steamFallback))
        continue;

      app.second._status.running     = false;
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

          // Recognize that the Steam client is running
          if (_wcsnicmp (exeFileLast.c_str(), exeSteam.c_str(), exeSteam.length()) == 0)
          {
            new_steamRunning = true;
            continue;
          }

          bool accessDenied =
            GetLastError ( ) == ERROR_ACCESS_DENIED;

          // Get exit code to filter out zombie processes
          DWORD dwExitCode = 0;
          GetExitCodeProcess (hProcess, &dwExitCode);
          
          WCHAR szExePath     [MAX_PATH + 2] = { };
          DWORD szExePathLen = MAX_PATH + 2; // Specifies the size of the lpExeName buffer, in characters.

          if (! accessDenied)
          {
            // If the process is not active any longer, skip it (terminated or zombie process)
            if (dwExitCode != STILL_ACTIVE)
              continue;

            // See if we can retrieve the full path of the executable
            if (! QueryFullProcessImageName (hProcess, 0, szExePath, &szExePathLen))
              szExePathLen = 0;
          }

          for (auto& app : g_apps)
          {
            if (! app.second.launch_configs.contains (0))
              continue;

            if (app.second._status.dwTimeDelayChecks > current_time)
              continue;

            // Workaround for Xbox games that run under the virtual folder, e.g. H:\Games\Xbox Games\Hades\Content\Hades.exe, by only checking the presence of the process name
            // TODO: Investigate if this is even really needed any longer? // Aemony, 2023-12-31
            if (app.second.store == app_record_s::Store::Xbox && _wcsnicmp (app.second.launch_configs[0].getExecutableFileName ( ).c_str(), pe32.szExeFile, MAX_PATH) == 0)
            {
              app.second._status.running     = true;
              app.second._status.running_pid = pe32.th32ProcessID;
              break;
            }

            else if (szExePathLen != 0)
            {
              if (app.second.store == app_record_s::Store::Steam)
              {
                if (_wcsnicmp (app.second.launch_configs[0].getExecutableFullPath ( ).c_str(), szExePath, szExePathLen) == 0)
                {
                  app.second._status.running_pid = pe32.th32ProcessID;

                  // Only set the running state if the primary registry monitoring is unavailable
                  if (! steamFallback)
                    continue;

                  app.second._status.running     = true;
                  break;
                }
              }

              // Epic, GOG and SKIF Custom should be straight forward
              else if (_wcsnicmp (app.second.launch_configs[0].getExecutableFullPath ( ).c_str(), szExePath, szExePathLen) == 0) // full patch
              {
                app.second._status.running     = true;
                app.second._status.running_pid = pe32.th32ProcessID;
                break;

                // One can also perform a partial match with the below OR clause in the IF statement, however from testing
                //   PROCESS_QUERY_LIMITED_INFORMATION gives us GetExitCodeProcess() and QueryFullProcessImageName() rights
                //     even to elevated processes, meaning the below OR clause is unnecessary.
                // 
                // (fullPath.empty() && ! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str()))
                //
              }
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

    steamRunning = new_steamRunning;

    lastGameRefresh = current_time;
  }

  
  // Instant Play monitoring...

  for (auto& monitored_app : iPlayCache)
  {
    if (monitored_app.id != 0)
    {
      HANDLE hProcess      = monitored_app.hProcess.load();
      HANDLE hWorkerThread = monitored_app.hWorkerThread.load();

      for (auto& app : g_apps)
      {
        if (monitored_app.id       ==      app.second.id &&
            monitored_app.store_id == (int)app.second.store)
        {
          app.second._status.running = 1;

          // Monitor the external process primarily
          if (hProcess != INVALID_HANDLE_VALUE)
          {
            if (WAIT_OBJECT_0 == WaitForSingleObject (hProcess, 0))
            {
              PLOG_DEBUG << "Game process for app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " has ended!";
              app.second._status.running = 0;

              monitored_app.id       =  0;
              monitored_app.store_id = -1;

              CloseHandle (hProcess);
              hProcess = INVALID_HANDLE_VALUE;
              monitored_app.hProcess.store(INVALID_HANDLE_VALUE);

              // Clean up these as well if they haven't been done so yet
              if (hWorkerThread != INVALID_HANDLE_VALUE)
              {
                CloseHandle(hWorkerThread);
                hWorkerThread = INVALID_HANDLE_VALUE;
                monitored_app.hWorkerThread.store(INVALID_HANDLE_VALUE);
              }
            }
          }
          
          // If we cannot monitor the game process, monitor the worker thread
          if (hWorkerThread != INVALID_HANDLE_VALUE)
          {
            if (WAIT_OBJECT_0 == WaitForSingleObject (hWorkerThread, 0))
            {
              PLOG_DEBUG << "Worker thread for launching app ID " << monitored_app.id << " from platform ID " << monitored_app.store_id << " has ended!";

              CloseHandle (hWorkerThread);
              hWorkerThread = INVALID_HANDLE_VALUE;
              monitored_app.hWorkerThread.store(INVALID_HANDLE_VALUE);
            }
          }
        }
      }
    }
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
  
  static SKIF_DirectoryWatch     SKIF_Epic_ManifestWatch;

  static CComPtr <ID3D11ShaderResourceView> pTexSRV;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV_old;

  static ImVec2 vecCoverUv0     = ImVec2 (0, 0),
                vecCoverUv1     = ImVec2 (1, 1),
                vecCoverUv0_old = ImVec2 (0, 0),
                vecCoverUv1_old = ImVec2 (1, 1);

  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };

#pragma region Initialization

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

  SK_RunOnce (fAlpha = (_registry.bFadeCovers) ? 0.0f : 1.0f);

  DWORD       current_time = SKIF_Util_timeGetTime ( );
  bool        sort_changed = false;
  static bool update       = true;
  static bool populated    = false;

  auto _SortApps = [&](void) -> void
  {
    std::sort ( g_apps.begin (),
                g_apps.end   (),
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

  // Check if any monitored platforms have been signaled
  if (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ))
  {
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
    PLOG_INFO << "Populating library list...";
    
    // Clear any existing trie
    labels = Trie { };

    // Clear all games
    g_apps.clear();
    
    // Load Steam titles from disk
    if (_registry.bLibrarySteam)
    {
      SKIF_Steam_GetInstalledAppIDs (&g_apps);

      // Refresh the current Steam user
      SKIF_Steam_GetCurrentUser     (true);
      
      // Preload any custom launch options for all Steam games
      SKIF_Steam_PreloadAllLaunchOptions (SKIF_Steam_GetCurrentUser());

      // Preload all appinfo.vdf data
      // This is needed for SKIF's fallback process tracking
      //   of Steam games that relies on the executable name
      //
      // 2024-01-01: Not actually needed any longer after last weeks
      //   major I/O optimizations, _and_ a new fallback monitoring
      //     method that loads appinfo for one game per frame.

//#define _WRITE_APPID_INI

#ifdef _WRITE_APPID_INI

      if (appinfo != nullptr)
      {
        PLOG_DEBUG << "Loading appinfo.vdf data...";
        for (auto& app : g_apps)
        {
          if (app.second.id == SKIF_STEAM_APPID)
            SKIF_STEAM_OWNER = true;

          if (! app.second.processed)
            appinfo->getAppInfo ( app.second.id );
        }
        PLOG_DEBUG << "Finished loading appinfo.vdf data for " << g_apps.size() << " games!";
        steamFallback = true;
      }

#endif

    }

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

      SKIF_record.specialk.profile_dir =
        SK_FormatStringW(LR"(%ws\Profiles)",
          _path_cache.specialk_userdata);

      std::pair <std::string, app_record_s>
        SKIF ( "Special K", SKIF_record );

      g_apps.emplace_back (SKIF);
    }

    // Load GOG titles from registry
    if (_registry.bLibraryGOG)
      SKIF_GOG_GetInstalledAppIDs (&g_apps);

    // Load Epic titles from disk
    if (_registry.bLibraryEpic)
      SKIF_Epic_GetInstalledAppIDs (&g_apps);
    
    if (_registry.bLibraryXbox)
      SKIF_Xbox_GetInstalledAppIDs (&g_apps);

    // Load custom SKIF titles from registry
    if (_registry.bLibraryCustom)
      SKIF_GetCustomAppIDs (&g_apps);

    PLOG_INFO << "Loading custom launch configs synchronously...";

    static const std::pair <bool, std::wstring> lc_files[] = {
      { false, SK_FormatStringW(LR"(%ws\Assets\lc_user.json)", _path_cache.specialk_userdata) }, // We load user-specified first
      {  true, SK_FormatStringW(LR"(%ws\Assets\lc.json)",      _path_cache.specialk_userdata) }
    };

    for (auto& lc_file : lc_files)
    {
      std::ifstream file(lc_file.second);
      nlohmann::json jf = nlohmann::json::parse(file, nullptr, false);
      file.close();

      if (jf.is_discarded ( ))
      {
        PLOG_ERROR << "Error occurred while trying to parse " << lc_file.second;

        // We are dealing with lc.json and something went wrong
        //   delete the file so a new attempt is performed later
        if (lc_file.first)
          PLOG_INFO_IF(DeleteFile (lc_file.second.c_str())) << "Deleting file so a retry occurs the next time an online check is performed...";

        continue;
      }

      try {
        for (auto& app : g_apps)
        {
          auto& record     =  app.second;
          auto& append_cfg = (record.store == app_record_s::Store::Steam) ? record.launch_configs_custom
                                                                          : record.launch_configs;

          std::string key  = (record.store == app_record_s::Store::Epic)  ? record.Epic_AppName     :
                             (record.store == app_record_s::Store::Xbox)  ? record.Xbox_PackageName :
                                                            std::to_string (record.id);

          for (auto& launch_config : jf[record.store_utf8][key])
          {
            app_record_s::launch_config_s lc;
            lc.id                       = static_cast<int> (append_cfg.size());
            lc.valid                    = 1;
            lc.description              = SK_UTF8ToWideChar(launch_config.at("Desc"));
            lc.executable               = SK_UTF8ToWideChar(launch_config.at("Exe"));
            std::replace(lc.executable.begin(), lc.executable.end(), '/', '\\'); // Replaces all forward slashes with backslashes
            lc.working_dir              = SK_UTF8ToWideChar(launch_config.at("Dir"));
            lc.launch_options           = SK_UTF8ToWideChar(launch_config.at("Args"));
            lc.executable_path          = record.install_dir + L"\\" + lc.executable;
            lc.install_dir              = record.install_dir;
            lc.custom_skif              =   lc_file.first;
            lc.custom_user              = ! lc.custom_skif;

            append_cfg.emplace (lc.id, lc);
          }
        }
      }
      catch (const std::exception&)
      {
        PLOG_ERROR << "Error occurred when trying to parse " << ((lc_file.first) ? "online-based" : "user-specified") << " launch configs";
      }
    }

    PLOG_INFO << "Loading game names synchronously...";

    // Process the list of apps -- prepare their names, keyboard search, as well as remove any uninstalled entries
    for (auto& app : g_apps)
    {
      //PLOG_DEBUG << "Working on " << app.second.id << " (" << app.second.store_utf8 << ")";

      // Steam handling...
      if (app.second.store == app_record_s::Store::Steam)
      { 
        // Special handling for non-Steam owners of Special K / SKIF
        if (app.second.id == SKIF_STEAM_APPID)
          app.first = "Special K";

        // Regular handling for the remaining Steam games
        else {
          app.first.clear ();

          app.second._status.refresh (&app.second);
        }
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
            SK_UseManifestToGetAppName (&app.second);
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
          install_dir = SK_UseManifestToGetInstallDir (&app.second);
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
      for (auto& app : g_apps)
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

    // DEBUG ONLY: Causes SKIF to take quite a long time to start up as it preloads
    //   the injection strategy of all games (especially Steam games and the appinfo.vdf file)
    // 2023-12-22: The massive slowdowns have been resolved, and processing 100 games here takes only 800ms, lol // Aemony
#if 0
    PLOG_DEBUG << "[Injection Strategy] Started processing games...";

    for (auto& app : g_apps)
      UpdateInjectionStrategy (&app.second);

    PLOG_DEBUG << "[Injection Strategy] Finished processing inejction strategy for " << g_apps.size() << " games!";
#endif

    PLOG_INFO << "Finished populating the library list.";

    // We're going to stream game icons asynchronously on this thread
    _beginthread ([](void*)->void
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibRefreshWorker");

      CoInitializeEx (nullptr, 0x0);

      PLOG_DEBUG << "SKIF_LibRefreshWorker thread started!";
      
      PLOG_INFO << "Loading the embedded Patreon texture...";
      ImVec2 dontCare1, dontCare2;
      if (pPatTexSRV.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, pPatTexSRV,          L"patreon.png",         dontCare1, dontCare2);
      if (pSKLogoTexSRV.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, pSKLogoTexSRV,       L"sk_boxart.png",       dontCare1, dontCare2);
      if (pSKLogoTexSRV_small.p == nullptr)
        LoadLibraryTexture (LibraryTexture::Logo,    SKIF_STEAM_APPID, pSKLogoTexSRV_small, L"sk_boxart_small.png", dontCare1, dontCare2);

      // Force a refresh when the Patreon and Cover textures have finished loading
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);
      
      // Load icons last
      for (auto& app : g_apps)
      {
        if (app.second.id == 0)
          continue;

        std::wstring load_str;
        
        if (app.second.id == SKIF_STEAM_APPID) // SKIF
          load_str = L"sk_icon.jpg";
        else  if (app.second.store == app_record_s::Store::Custom) // SKIF Custom
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
      
      gameWorkerRunning.store(false);

      // Force a refresh when the game icons have finished being streamed
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

      PLOG_DEBUG << "SKIF_LibRefreshWorker thread stopped!";
    }, 0x0, NULL);

    populated = true;
  }

  extern bool  coverFadeActive;
  static int   tmp_iDimCovers = _registry.iDimCovers;
  
  static
    app_record_s* pApp      = nullptr;

  // Ensure pApp points to the current selected game
  // This should be the only place where pApp changes during the whole frame!
  for (auto& app : g_apps)
    if (app.second.id == selection.appid && app.second.store == selection.store)
      pApp = &app.second;

  // Default to primary launch config
  launchConfig = (pApp != nullptr && ! pApp->launch_configs.empty()) ? &pApp->launch_configs.begin()->second : nullptr;
  
  bool isSpecialK = (pApp != nullptr && pApp->id == SKIF_STEAM_APPID && pApp->store == app_record_s::Store::Steam);

  // Update the injection strategy and cache for the selected game
  // Only do this once per frame to prevent data from "leaking" between pApp's
  if (pApp != nullptr)
  {
    if (update)
    {
      if (  pApp->install_dir != selection.dir_watch._path)
        selection.dir_watch.reset ( );

      if (! pApp->install_dir.empty())
        selection.dir_watch.isSignaled (pApp->install_dir, true);
    }

    if (update                                       ||
        selection.dir_watch.isSignaled ( )           || // TODO: Investigate support for multiple launch configs? Right now only the "main" folder is being monitored
       _cache.service     != _inject.bCurrentState   ||
       _cache.running     !=  pApp->_status.running  ||
       _cache.updating    !=  pApp->_status.updating ||
       _cache.autostop    != _inject.bAckInj         ||
       _inject.libCacheRefresh                       ) // If the global DLL files have been changed
    {
      _inject.libCacheRefresh = false;

      UpdateInjectionStrategy (pApp);
      _cache.Refresh          (pApp);
    }

    // Load a new cover
    // Ensure we aren't already loading this cover
    if (lastCover.appid != pApp->id   ||
        lastCover.store != pApp->store)
    {
      // Special handling for the Special K logo when fading is disabled
      if (! _registry.bFadeCovers)
      {
        if (isSpecialK)
          fAlphaSK = 1.0f;
        else if (lastCover.appid == SKIF_STEAM_APPID &&
                 lastCover.store == app_record_s::Store::Steam)
          fAlphaSK = 0.0f;
      }

      loadCover       = true;
      lastCover.appid = pApp->id;
      lastCover.store = pApp->store;

      // Hide the current cover and set it up to be unloaded
      if (pTexSRV.p != nullptr)
      {
        // If there already is an old cover, we need to push it for release
        if (pTexSRV_old.p != nullptr)
        {
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV_old.p << " to be released";;
          SKIF_ResourcesToFree.push(pTexSRV_old.p);
          pTexSRV_old.p = nullptr;
        }

        // Set up the current one to be released
        vecCoverUv0_old = vecCoverUv0;
        vecCoverUv1_old = vecCoverUv1;
        pTexSRV_old.p   = pTexSRV.p;
        pTexSRV.p       = nullptr;
        fAlphaPrev      = (_registry.bFadeCovers) ? fAlpha   : 0.0f;
        fAlpha          = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
      }
    }

    // Release old cover after it has faded away, or instantly if we don't fade covers
    if (fAlphaPrev <= 0.0f)
    {
      if (pTexSRV_old.p != nullptr)
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV_old.p << " to be released";;
        SKIF_ResourcesToFree.push(pTexSRV_old.p);
        pTexSRV_old.p = nullptr;
      }
    }
  }

  // Apply changes when the selected game changes
  if (update)
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;
  // Apply changes when the _registry.iDimCovers var has been changed in the Settings tab
  else if (tmp_iDimCovers != _registry.iDimCovers)
  {
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;

    tmp_iDimCovers = _registry.iDimCovers;
  }

  // At this point we're done with the update variable
  update = false;

#pragma endregion

  ImVec2 sizeCover   = (_registry.bHorizonMode) ? ImVec2 (186.67f, 280.0f) : ImVec2 (600.0f, 900.0f);
  ImVec2 sizeList    = (_registry.bHorizonMode) ? ImVec2 (  0.00f, 280.0f) : ImVec2 (  0.0f, 620.0f);
  ImVec2 sizeDetails = (_registry.bHorizonMode) ? ImVec2 (  0.00f, 280.0f) : ImVec2 (  0.0f, 280.0f);

  // From now on ImGui UI calls starts being made...

#pragma region GameCover

  ImGui::BeginGroup    (                                                  );

  static int    queuePosGameCover  = 0;
  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelMissing[] = "Missing cover :(";
  static char   cstrLabelGOGUser[] = "Please sign in to GOG Galaxy to\n"
                                     "allow the cover to be populated :)";

  ImVec2 vecPosCoverImage    = ImGui::GetCursorPos ( );
         vecPosCoverImage.x -= 1.0f * SKIF_ImGui_GlobalDPIScale;

  if (loadCover)
  {
    // A new cover is meant to be loaded, so don't do anything for now...
  }

  else if (tryingToLoadCover)
  {
    ImGui::SetCursorPos (ImVec2 (
      vecPosCoverImage.x + (sizeCover.x / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelLoading).x / 2,
      vecPosCoverImage.y + (sizeCover.y / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelLoading).y / 2));
    ImGui::TextDisabled (  cstrLabelLoading);
  }
  
  else if (textureLoadQueueLength.load() == queuePosGameCover && pTexSRV.p == nullptr)
  {
    if (pApp != nullptr && pApp->id == SKIF_STEAM_APPID && pApp->store == app_record_s::Store::Steam)
    {
      // Special K selected -- do nothing
    }

    else {
      extern std::wstring GOGGalaxy_UserID;
      if (pApp != nullptr && pApp->store == app_record_s::Store::GOG && GOGGalaxy_UserID.empty())
      {
        ImGui::SetCursorPos (ImVec2 (
                    vecPosCoverImage.x + (sizeCover.x / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelGOGUser).x / 2,
                    vecPosCoverImage.y + (sizeCover.y / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelGOGUser).y / 2));
        ImGui::TextDisabled (  cstrLabelGOGUser);
      }
      else {
        ImGui::SetCursorPos (ImVec2 (
                    vecPosCoverImage.x + (sizeCover.x / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).x / 2,
                    vecPosCoverImage.y + (sizeCover.y / 2) * SKIF_ImGui_GlobalDPIScale - ImGui::CalcTextSize (cstrLabelMissing).y / 2));
        ImGui::TextDisabled (  cstrLabelMissing);
      }
    }
  }

  ImGui::SetCursorPos (vecPosCoverImage);

  extern bool SKIF_bHDREnabled;

  float fGammaCorrectedTint = 
    ((! SKIF_bHDREnabled && _registry.iSDRMode == 2) || 
     (  SKIF_bHDREnabled && _registry.iHDRMode == 2))
        ? AdjustAlpha (fTint)
        : fTint;

  // Display previous fading out cover
  if (pTexSRV_old.p != nullptr && fAlphaPrev > 0.0f)
  {
    SKIF_ImGui_OptImage  (pTexSRV_old.p,
                                                      ImVec2 (sizeCover.x * SKIF_ImGui_GlobalDPIScale,
                                                              sizeCover.y * SKIF_ImGui_GlobalDPIScale),
                                                      vecCoverUv0_old, // Top Left coordinates
                                                      vecCoverUv1_old, // Bottom Right coordinates
                                    (_registry.iStyle == 2) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlphaPrev))  : ImVec4 (fTint, fTint, fTint, fAlphaPrev), // Alpha transparency
                                    (_registry.bUIBorders)  ? ImGui::GetStyleColorVec4 (ImGuiCol_Border) : ImVec4 (0.0f, 0.0f, 0.0f, 0.0f)       // Border
    );

    ImGui::SetCursorPos (vecPosCoverImage);
  }

  // Display game cover image
  SKIF_ImGui_OptImage  (pTexSRV.p,
                                                    ImVec2 (sizeCover.x * SKIF_ImGui_GlobalDPIScale,
                                                            sizeCover.y * SKIF_ImGui_GlobalDPIScale),
                                                    vecCoverUv0, // Top Left coordinates
                                                    vecCoverUv1, // Bottom Right coordinates
                                  (_registry.iStyle == 2) ? ImVec4 (1.0f, 1.0f, 1.0f, fGammaCorrectedTint * AdjustAlpha (fAlpha))  : ImVec4 (fTint, fTint, fTint, fAlpha), // Alpha transparency (2024-01-01, removed fGammaCorrectedTint * fAlpha for the light style)
                                  (_registry.bUIBorders)  ? ImGui::GetStyleColorVec4 (ImGuiCol_Border) : ImVec4 (0.0f, 0.0f, 0.0f, 0.0f)       // Border
  );

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    CoverMenu = PopupState_Open;

  ImGui::SetCursorPos (vecPosCoverImage);

  if (  isSpecialK ||
     (! isSpecialK && fAlphaSK > 0.0f))
  {
    ImGui::Image (((! _registry.bHorizonMode) ?   pSKLogoTexSRV.p : pSKLogoTexSRV_small.p),
                                                    ImVec2 (sizeCover.x * SKIF_ImGui_GlobalDPIScale,
                                                            sizeCover.y * SKIF_ImGui_GlobalDPIScale),
                                                    ImVec2 (0.0f, 0.0f),                // Top Left coordinates
                                                    ImVec2 (1.0f, 1.0f),                // Bottom Right coordinates
                                                    ImVec4 (1.0f, 1.0f, 1.0f, fAlphaSK),  // Tint for Special K's logo
                                                    ImVec4 (0.0f, 0.0f, 0.0f, 0.0f)     // Border
    );
  }

  // Every >15 ms, increase/decrease the cover fade effect (makes it frame rate independent)
  static DWORD timeLastTick;
  bool isHovered = ImGui::IsItemHovered();
  bool incTick   = false;

  // Fade in/out transition

  if (_registry.bFadeCovers)
  {
    // Fade in the new cover
    if (fAlpha < 1.0f && pTexSRV.p != nullptr)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlpha += 0.05f;
        incTick = true;
      }

      coverFadeActive = true;
    }

    // Fade out the old one
    if (fAlphaPrev > 0.0f && pTexSRV_old.p != nullptr)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaPrev -= 0.05f;
        incTick     = true;
      }

      coverFadeActive = true;
    }

    // Fade in the SK logo
    if (isSpecialK && fAlphaSK < 1.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaSK += 0.05f;
        incTick   = true;
      }

      coverFadeActive = true;
    }

    // Fade out the SK logo
    if (! isSpecialK && fAlphaSK > 0.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fAlphaSK -= 0.05f;
        incTick   = true;
      }

      coverFadeActive = true;
    }
  }

  // Dim covers

  if (_registry.iDimCovers == 2)
  {
    if (isHovered && fTint < 1.0f)
    {
      if (current_time - timeLastTick > 15)
      {
        fTint = fTint + 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }

    else if (! isHovered && fTint > fTintMin)
    {
      if (current_time - timeLastTick > 15)
      {
        fTint = fTint - 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }
  }

  // Increment the tick
  if (incTick)
    timeLastTick = current_time;

  float fY =
  ImGui::GetCursorPosY (                                                  );

  ImGui::EndGroup             ( );

#pragma endregion

  // This sets the same line for the cover as the list + details
  ImGui::SameLine             (0.0f, 3.0f * SKIF_ImGui_GlobalDPIScale); // 0.0f, 0.0f
  
  float fZ =
  ImGui::GetCursorPosX (                                                  );

  // LIST + DETAILS START
  ImGui::BeginGroup   (                  );

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
        for (auto& app : g_apps)
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

        for (auto& app : g_apps)
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

#pragma region GamesList

  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
  ImGui::BeginChild          ( "###AppListInset",
                                ImVec2 ( (_WIDTH - ImGui::GetStyle().WindowPadding.x / 2.0f),
                                         (sizeList.y * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x - 2.0f) ), (_registry.bUIBorders),
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
      if ( IconMenu == PopupState_Closed &&
           ImGui::IsItemClicked (ImGuiMouseButton_Right))
           IconMenu = PopupState_Open;
    }

    else {
      if (GameMenu == PopupState_Closed)
      {
        if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
             _GamePadRightClick                            ||
             _NavLongActivate)
        {
          GameMenu = PopupState_Open;
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

  // Start populating the list

  // Populate the list of games with all recognized games
  for (auto& app : g_apps)
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second.id == 0)
      continue;
    
    bool selected = (selection.appid == app.second.id &&
                     selection.store == app.second.store);
    bool change   = false;

    if (app.second.store == app_record_s::Store::Steam && steamRunning)
    {
      if (app.second._status.dwTimeDelayChecks != 0)
      {
        if (app.second._status.dwTimeDelayChecks < current_time)
          app.second._status.dwTimeDelayChecks = 0;
      }

      else
        app.second._status.refresh (&app.second);
    }

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
        launchGame = true;
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

      selection.appid              = app.second.id;
      selection.store              = app.second.store;
      selected                     = true;
      _registry.iLastSelectedGame  =      selection.appid;
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
      static bool _horizon = _registry.bHorizonMode;

      // This allows the scroll to reset on DPI changes, to keep the selected item on-screen
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last ||
             _registry.bHorizonMode != _horizon)
      {
        ImGui::SetScrollHereY (0.5f);
        _horizon = _registry.bHorizonMode;
      }
    }
  }

  // 'Add Game' to the bottom of the list if the status bar is disabled
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

  // Stop populating the list

  ImGui::EndGroup        ( );
  ImGui::EndChild        ( );
  ImGui::PopStyleColor   ( );
  
#pragma endregion

  // Open the Empty Space Menu

  if (IconMenu != PopupState_Open &&
    ! ImGui::IsAnyItemHovered ( ) &&
      ImGui::IsItemClicked    (ImGuiMouseButton_Right))
    EmptySpaceMenu = PopupState_Open;

  if (_registry.bHorizonMode)
    ImGui::SameLine ( );

#pragma region GameDetails

  ImGui::BeginChild (
    "###AppListInset2",
      ImVec2 ( (_WIDTH - ImGui::GetStyle().WindowPadding.x / 2.0f),
               sizeDetails.y * SKIF_ImGui_GlobalDPIScale), (_registry.bUIBorders),
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NavFlattened      |
        ImGuiWindowFlags_AlwaysUseWindowPadding
  );
  ImGui::BeginGroup ();

  if ( pApp        == nullptr            ||
      (pApp->id    == SKIF_STEAM_APPID   &&
       pApp->store == app_record_s::Store::Steam))
  {
    _inject._GlobalInjectionCtl ();
  }

  else if (pApp != nullptr)
  {
    GetInjectionSummary (pApp);

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
      [&](app_record_s::launch_config_s& launch_cfg, bool menu) ->
      void
      {
        bool blacklist =
          launch_cfg.isBlacklisted ( );
        bool blacklist_pattern =
          launch_cfg.isExecutableFullPathValid () &&
          _inject._TestUserList (SK_WideCharToUTF8 (launch_cfg.getExecutableFullPath()).c_str(), false);

        char          szButtonLabel [256] = { };

        if (menu)
          sprintf_s ( szButtonLabel, 255,
                        " for %s###DisableSpecialK-%d",
                          launch_cfg.getDescriptionUTF8().empty()
                            ? launch_cfg.getExecutableFileNameUTF8().c_str ()
                            : launch_cfg.getDescriptionUTF8().c_str (),
                          launch_cfg.id);
        else
          sprintf_s ( szButtonLabel, 255,
                        " Disable Special K###DisableSpecialK%d",
                          launch_cfg.id );

        if (blacklist_pattern)
        {
          blacklist = true;
          SKIF_ImGui_PushDisableState ( );
        }
          
        if (ImGui::Checkbox (szButtonLabel,   &blacklist))
          launch_cfg.setBlacklisted (blacklist);

        if (blacklist_pattern)
        {
          SKIF_ImGui_PopDisableState ( );
          SKIF_ImGui_SetHoverTip ("Special K is disabled through a blacklist pattern in the Settings tab.");
        }

        SKIF_ImGui_SetHoverText (launch_cfg.getExecutableFullPathUTF8 ( ).c_str());
      };

      if ( ! _inject.bHasServlet )
      {
        ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Service is unavailable due to missing files.");
      }

      // Only show the bottom options if there's some launch configs, and the first one is valid...
      else if ( ! pApp->launch_configs.empty()) //&&
                  //pApp->launch_configs[0].isExecutableFileNameValid())
      {
        bool elevate =
          pApp->launch_configs[0].isElevated ( );

        if (ImGui::Checkbox ("Elevated service###ElevatedLaunch",   &elevate))
          pApp->launch_configs[0].setElevated (elevate);

        ImGui::SameLine ( );

        // Set horizontal position
        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX  ( ) +
          ImGui::GetColumnWidth ( ) -
          ImGui::CalcTextSize   ("[] Disable Special K").x -
          ImGui::GetStyle       ( ).ItemSpacing.x * 2
        );

        static uint32_t currAppId  = 0;
        static int      numOfItems = 0;

        if (currAppId != pApp->id)
        {   currAppId  = pApp->id;

          numOfItems = 0;

          for ( auto& launch : pApp->launch_configs )
          {
            if (! launch.second.valid ||
                  launch.second.duplicate_exe)
              continue;

            numOfItems++;
          }
        }

        // If there is 0-1 valid launch option
        if (numOfItems <= 1)
        {
          _BlacklistCfg          (
                pApp->launch_configs.begin ()->second, false );
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
            bool sepCustomSKIF = true,
                 sepCustomUser = true;

            for ( auto& launch : pApp->launch_configs )
            {
              if (! launch.second.valid || 
                    launch.second.duplicate_exe)
                continue;

              // Separators between official / user / SKIF launch configs

              if (launch.second.custom_user && sepCustomUser)
              {
                sepCustomUser = false;
                ImGui::Separator ( );
              }

              else if (launch.second.custom_skif && sepCustomSKIF)
              {
                sepCustomSKIF = false;
                ImGui::Separator ( );
              }

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

#pragma endregion

  ImGui::EndGroup     (                  );
  // LIST + DETAILS STOP

#pragma region SpecialKPatreon

  // Special handling at the bottom of the cover for Special K
  if (! _registry.bHorizonMode            &&
        pApp        != nullptr            &&
        pApp->id    == SKIF_STEAM_APPID   &&
        pApp->store == app_record_s::Store::Steam)
  {
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
                                                      ImGuiWindowFlags_None); // ((pApp->tex_cover.isCustom) ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoBackground))

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

#pragma endregion

  // Refresh running state of SKIF Custom, Epic, GOG, and Xbox titles
  RefreshRunningApps ( );

#pragma region ServiceMenu
  
  extern void SKIF_ImGui_ServiceMenu (void);

  SKIF_ImGui_ServiceMenu ( );

#pragma endregion
  
#pragma region CoverMenu

  // Open the CoverMenu
  if (CoverMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("CoverMenu");
    CoverMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup ("CoverMenu", ImGuiWindowFlags_NoMove))
  {
    if (pApp != nullptr)
    {
      // Preparations

      bool resetVisible = ((pApp->id    != SKIF_STEAM_APPID            && // Ugly check to exclude the "Special K" entry from being set to true
                            pApp->store != app_record_s::Store::Custom) ||
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
      if (ImGui::Selectable ("Change", false, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.png;*.jpg;*.jpeg;*.webp" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring filePath = std::wstring(pwszFilePath);
          if (UpdateGameCover (pApp, filePath))
          {
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
        if (ImGui::Selectable ((pApp->tex_cover.isCustom) ? ((pApp->store == app_record_s::Store::Custom) ? "Clear" : "Reset") : "Refresh", false, ImGuiSelectableFlags_SpanAllColumns | ((pApp->tex_cover.isCustom || pApp->tex_cover.isManaged) ? 0x0 : ImGuiSelectableFlags_Disabled)))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Custom)
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

      if (ImGui::Selectable ("Open SteamGridDB", false, ImGuiSelectableFlags_SpanAllColumns))
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

#pragma endregion

#pragma region GamesList::GameContextMenu

  if (GameMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("GameContextMenu");
    GameMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup ("GameContextMenu", ImGuiWindowFlags_NoMove))
  {
    if (pApp != nullptr)
    {
      if (pApp->id == SKIF_STEAM_APPID)
        DrawSKIFContextMenu (pApp);
      else
      {
        DrawGameContextMenu (pApp);
      }
    }

    else if (! update)
      ImGui::CloseCurrentPopup ();

    ImGui::EndPopup ();
  }

#pragma endregion

#pragma region GamesList::IconMenu
  
  // Open the IconMenu
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
                           pApp->store != app_record_s::Store::Custom) ||
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
      if (ImGui::Selectable ("Change", false, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png;*.ico" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Custom)
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
        if (ImGui::Selectable ((pApp->tex_icon.isCustom) ? "Reset" : "Refresh", false, ImGuiSelectableFlags_SpanAllColumns | ((pApp->tex_icon.isCustom || pApp->tex_icon.isManaged) ? 0x0 : ImGuiSelectableFlags_Disabled)))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           _path_cache.specialk_userdata);
          else if (pApp->store == app_record_s::Store::Custom)
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

      if (ImGui::Selectable ("Open SteamGridDB", false, ImGuiSelectableFlags_SpanAllColumns))
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

#pragma region GamesList::EmptySpaceMenu
  
  // Open the Empty Space Menu
  if (EmptySpaceMenu == PopupState_Open)
  {
    ImGui::OpenPopup    ("GameListEmptySpaceMenu");
    EmptySpaceMenu = PopupState_Closed;
  }

  if (ImGui::BeginPopup   ("GameListEmptySpaceMenu", ImGuiWindowFlags_NoMove))
  {
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

     if (ImGui::Selectable ("Add Game", false, ImGuiSelectableFlags_SpanAllColumns))
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

      ImGui::Separator ( );

      if (ImGui::MenuItem ("Custom",spaces, &_registry.bLibraryCustom))
      {
        _registry.regKVLibraryCustom.putData(_registry.bLibraryCustom);
        RepopulateGames = true;
      }

      ImGui::EndMenu ( );
    }

    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );

    if (ImGui::Selectable ("Refresh", false, ImGuiSelectableFlags_SpanAllColumns))
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
  
#pragma region GameLaunchLogic

  if ((launchGame || launchGameMenu || launchInstant) &&
       pApp != nullptr && launchConfig != nullptr)
  {
    if ( pApp->store != app_record_s::Store::Steam && pApp->store != app_record_s::Store::Epic &&
       ! launchConfig->isExecutableFullPathValid ( ))
    {
      confirmPopupText = "Could not launch game due to missing executable:\n\n" + launchConfig->getExecutableFullPathUTF8 ( );
      ConfirmPopup     = PopupState_Open;
    }

    else {
      bool localInjection        = (_cache.injection.type == SKIF_Lib_SummaryCache::CachedType::Local);
      bool usingSK               = localInjection;


      // SERVICE PREPARATIONS


      // Check if the injection service should be used
      if (! usingSK)
      {
        bool isLocalBlacklisted  = launchConfig->isBlacklisted ( ),
             isGlobalBlacklisted = _inject._TestUserList (launchConfig->getExecutableFullPathUTF8 ( ).c_str (), false);

        usingSK = ! launchWithoutSK       &&
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
            if (launchConfig->isExecutableFullPathValid ( ) &&
                _inject.WhitelistPath (launchConfig->getExecutableFullPathUTF8 ( )))
              _inject.SaveWhitelist ( );
          }

          // Disable the first service notification
          if (_registry.bMinimizeOnGameLaunch)
            _registry._SuppressServiceNotification = true;
        }

        // Kickstart service if it is currently not running
        if (usingSK && ! _inject.bCurrentState)
          _inject._StartStopInject (false, true, launchConfig->isElevated( ));

        // Stop the service if the user attempts to launch without SK
        else if (! usingSK && _inject.bCurrentState)
          _inject._StartStopInject (true);
      }

      // Create the injection acknowledge events in case of a local injection
      else {
        _inject.SetInjectAckEx     (true);
        _inject.SetInjectExitAckEx (true);
      }


      // LAUNCH PREPARATIONS


      if (! launchInstant)
      {
        // Custom games always use instant launch
        if (pApp->store == app_record_s::Store::Custom)
          launchInstant = true;

        // Fallback for GOG games if the Galaxy client is not installed
        else if (pApp->store == app_record_s::Store::GOG && ! GOGGalaxy_Installed)
          launchInstant = true;

        // Convert a few scenarios to an instant launch -- but not when using the game menu as it is always explicit
        else if (! launchGameMenu)
        {
          if (pApp->store == app_record_s::Store::Steam && _registry.bInstantPlaySteam)
            launchInstant = true;

          if (pApp->store == app_record_s::Store::GOG   && _registry.bInstantPlayGOG)
            launchInstant = true;
        }
      }


      // LAUNCH PROCEDURES


      // Launch Epic game
      if (pApp->store == app_record_s::Store::Epic)
      {
        PLOG_VERBOSE << "Performing an Epic launch...";

        // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

        std::wstring launchOptions = SK_FormatStringW(LR"(com.epicgames.launcher://apps/%ws?action=launch&silent=true)", launchConfig->getLaunchOptions().c_str());
        if (SKIF_Util_OpenURI (launchOptions) != 0)
        {
          // Don't check the running state for at least 7.5 seconds
          pApp->_status.dwTimeDelayChecks = current_time + 7500;
          pApp->_status.running           = true;

          SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), L"", launchOptions, L"", launchConfig->getExecutableFullPath ( ), (! localInjection && usingSK));
        }
      }

      // Launch Xbox game
      else if (pApp->store == app_record_s::Store::Xbox)
      {
        PLOG_VERBOSE << "Performing an Xbox launch...";

        if (SKIF_Util_CreateProcess (launchConfig->executable_helper))
        {
          // Don't check the running state for at least 7.5 seconds
          pApp->_status.dwTimeDelayChecks = current_time + 7500;
          pApp->_status.running           = true;

          SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), launchConfig->executable_helper, L"", launchConfig->getExecutableDir(), launchConfig->getExecutableFullPath(), (! localInjection && usingSK));
        }
      }

      // Instant Play
      // - Custom games
      // - GOG games when Galaxy is not installed
      // - Prefer Instant Play for GOG
      // - Prefer Instant Play for Steam
      else if (launchInstant)
      {
        PLOG_VERBOSE << "Performing an instant launch...";

        bool launchDecision       = true;

        // We need to use a proxy variable since we might remove a substring of the launch options
        std::wstring cmdLine      = launchConfig->getLaunchOptions();

        // Transform to lowercase
        std::wstring cmdLineLower = SKIF_Util_ToLowerW (cmdLine);

        const std::wstring argSKIF_SteamAppID       = L"skif_steamappid=";
        size_t             posSKIF_SteamAppID_start = cmdLineLower.find (argSKIF_SteamAppID);

        // For Steam games, we default to using the pApp->id value
        uint32_t     uiSteamAppID = (pApp->store == app_record_s::Store::Steam) ? pApp->id : 0;
        std::wstring wsSteamAppID = std::to_wstring(uiSteamAppID);

        // Extract the SKIF_SteamAppID cmd line argument, if it exists
        if (posSKIF_SteamAppID_start != std::wstring::npos)
        {
          size_t
            posSKIF_SteamAppID_end    = cmdLineLower.find (L" ", posSKIF_SteamAppID_start);

          if (posSKIF_SteamAppID_end == std::wstring::npos)
            posSKIF_SteamAppID_end    = cmdLineLower.length ( );

          // Length of the substring to remove
          posSKIF_SteamAppID_end -= posSKIF_SteamAppID_start;

          wsSteamAppID = cmdLineLower.substr (posSKIF_SteamAppID_start + argSKIF_SteamAppID.length ( ), posSKIF_SteamAppID_end);

          // Remove substring from the proxy variable
          cmdLine.erase (posSKIF_SteamAppID_start, posSKIF_SteamAppID_end);

          // Try to convert the found string to an unsigned integer
          try {
            uiSteamAppID = std::stoi(wsSteamAppID);
          }
          catch (const std::exception& e)
          {
            UNREFERENCED_PARAMETER(e);
          }
        }

        std::map<std::wstring, std::wstring> env;

        if (uiSteamAppID != 0)
        {
          PLOG_DEBUG << "Using Steam App ID : " << uiSteamAppID;
          env.emplace       (L"SteamAppId",        wsSteamAppID);
          
          bool steamOverlay = SKIF_Steam_isSteamOverlayEnabled (uiSteamAppID, SKIF_Steam_GetCurrentUser (true));

          if (! steamOverlay)
          {
            PLOG_DEBUG << "Disabling the Steam Overlay...";
            env.emplace (L"SteamNoOverlayUIDrawing", L"1");
          }

          // If both of these are false, see if there is any custom Steam launch options to append...
          if (! launchConfig->custom_skif &&
              ! launchConfig->custom_user)
          {
            std::string steamLaunchOptions = SKIF_Steam_GetLaunchOptions (uiSteamAppID, SKIF_Steam_GetCurrentUser (true), pApp);
          
            if (steamLaunchOptions.size() > 0)
            {
              PLOG_DEBUG << "Found additional launch options for this app in Steam: " << steamLaunchOptions;

              std::string steamLaunchOptionsLower =
                SKIF_Util_ToLower (steamLaunchOptions);

              if (steamLaunchOptionsLower.find("skif ") != std::string::npos)
              {
                launchDecision = false;

                // Escape any percent signs (%)
                for (auto pos  = steamLaunchOptions.find ('%');          // Find the first occurence
                          pos != std::string::npos;                      // Validate we're still in the string
                          steamLaunchOptions.insert      (pos, R"(%)"),  // Escape the character
                          pos  = steamLaunchOptions.find ('%', pos + 2)) // Find the next occurence
                { }
                          
                confirmPopupText = "Could not launch game due to conflicting launch options in Steam:\n"
                                    "\n"
                                 +  steamLaunchOptions + "\n"
                                    "\n"
                                    "Please change the launch options in Steam before trying again.";
                ConfirmPopup     = PopupState_Open;

                PLOG_WARNING << "Steam game " << pApp->id << " (" << pApp->names.normal << ") was unable to launch due to a conflict with the launch option of Steam!";
              }

              // Append the launch command string with the one from Steam...
              else {
                // If the cmdLine is not empty, add a space as well
                if (! cmdLine.empty())
                  cmdLine += L" ";
                
                cmdLine += SK_UTF8ToWideChar (steamLaunchOptions);
              }
            }
          }
        }

        if (launchDecision)
        {
          SKIF_Util_CreateProcess_s* proc = nullptr;
          for (auto& item : iPlayCache)
          {
            if (item.id == 0)
            {
              proc           = &item;
              proc->id       = pApp->id;
              proc->store_id = (int)pApp->store;
              break;
            }
          }

          if (SKIF_Util_CreateProcess (launchConfig->getExecutableFullPath ( ),
                             cmdLine.c_str(),
                          (! launchConfig->working_dir.empty())
                               ? launchConfig->working_dir.c_str()
                               : launchConfig->getExecutableDir().c_str(),
                                &env,
                                proc
            ))
          {
            if (pApp->store == app_record_s::Store::Steam)
              cmdLine += L" SKIF_SteamAppId=" + std::to_wstring (pApp->id);

            // Trim any spaces at the end
            cmdLine.erase (std::find_if (cmdLine.rbegin(), cmdLine.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), cmdLine.end());

            SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal),
              launchConfig->getExecutableFullPath ( ),
              cmdLine,
           (! launchConfig->working_dir.empty())
                ? launchConfig->working_dir.c_str()
                : launchConfig->getExecutableDir().c_str(),
              launchConfig->getExecutableFullPath ( ),
              (! localInjection && usingSK));
          }
        
          else {
            PLOG_DEBUG << "Process creation failed ?!";

            proc->id       =  0;
            proc->store_id = -1;
          }
        }
      }

      // Launch GOG Galaxy game (default for GOG games since 2024-01-05)
      else if (pApp->store == app_record_s::Store::GOG) // launchGalaxyGame
      {
        PLOG_VERBOSE << "Performing a GOG Galaxy launch...";

        extern std::wstring GOGGalaxy_Path;
        extern std::wstring GOGGalaxy_Folder;

        // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

        std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

        //SKIF_Util_OpenURI (GOGGalaxy_Path, SW_SHOWDEFAULT, L"OPEN", launchOptions.c_str());
        if (SKIF_Util_CreateProcess (GOGGalaxy_Path, launchOptions.c_str()))
        {
          // Don't check the running state for at least 7.5 seconds
          pApp->_status.dwTimeDelayChecks = current_time + 7500;
          pApp->_status.running           = true;

          SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal + " (Galaxy)"), GOGGalaxy_Path, launchOptions, GOGGalaxy_Folder, launchConfig->getExecutableFullPath(), (! localInjection && usingSK));
        }
      }

      // Launch Steam game (regular)
      else if (pApp->store == app_record_s::Store::Steam)
      {
        PLOG_VERBOSE << "Performing a Steam launch...";

        bool launchDecision = true;

        // Check localconfig.vdf if user is attempting to launch without Special K 
        if (! usingSK && ! localInjection)
        {
          std::string
              launch_options = SKIF_Steam_GetLaunchOptions (pApp->id, SKIF_Steam_GetCurrentUser (true), pApp);
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
          if (SKIF_Util_OpenURI (launchOptions) != 0)
          {
            // Don't check the running state for at least 7.5 seconds
            pApp->_status.dwTimeDelayChecks = current_time + 7500;
            pApp->_status.running           = true;

            SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (pApp->names.normal), L"", launchOptions, L"", launchConfig->getExecutableFullPath ( ), (! localInjection && usingSK));
          }
        }
      }

      else {
        PLOG_ERROR << "No applicable launch option was found?!";
      }

      // Fallback for minimizing SKIF when not using SK if configured as such
      if (_registry.bMinimizeOnGameLaunch && ! usingSK && SKIF_ImGui_hWnd != NULL)
        ShowWindowAsync (SKIF_ImGui_hWnd, SW_SHOWMINNOACTIVE);
    }

    launchConfig     = &pApp->launch_configs.begin()->second; // Reset to primary launch config
    launchGame       = false;
    launchGameMenu   = false;
    launchInstant    = false;
    launchWithoutSK  = false;
  }

#pragma endregion
  
#pragma region SKIF_LibCoverWorker

  // If we have changed mode, we need to reload the cover to ensure the proper resolution of it
  static bool lastHorizonMode = _registry.bHorizonMode;
  if (lastHorizonMode != _registry.bHorizonMode)
  {
    lastHorizonMode = _registry.bHorizonMode;
    //loadCover       = true;

    update    = true;
    lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
  }
  
  if (loadCover && populated)
  { // Load cover first after the window has been shown -- to fix one copy leaking of the cover 
    // 2023-03-24: Is this even needed any longer after fixing the double-loading that was going on?
    // 2023-03-25: Disabled HiddenFramesCannotSkipItems check to see if it's solved.
    // 2023-10-05: Disabled waiting for the icon thread as well
    loadCover = false;

    // Reset variables used to track whether we're still loading a game cover, or if we're missing one
    gameCoverLoading.store (true);
    tryingToLoadCover = true;
    queuePosGameCover = textureLoadQueueLength.load() + 1;

    // We're going to stream the cover in asynchronously on this thread
    _beginthread ([](void*)->void
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverWorker");

      CoInitializeEx (nullptr, 0x0);

      PLOG_DEBUG << "SKIF_LibCoverWorker thread started!";

      PLOG_INFO  << "Streaming game cover asynchronously...";

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

      std::wstring load_str;

      // SKIF
      if (_pApp->id == SKIF_STEAM_APPID)
      {
        // No need to change the string in any way
      }

      // SKIF Custom
      else if (_pApp->store == app_record_s::Store::Custom)
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

        // If horizon mode is being used, we prefer to load the 300x450 image!
        if (! _registry.bHorizonMode)
        {
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
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(_pTexSRV.p);
        _pTexSRV.p = nullptr;
      }

      PLOG_INFO  << "Finished streaming game cover asynchronously...";
      PLOG_DEBUG << "SKIF_LibCoverWorker thread stopped!";

    }, 0x0, NULL);
  }

#pragma endregion

#pragma region Popups

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

    ImGui::TreePush    ("ConfirmTreePush");

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
    ImGui::TreePush    ("RemoveGameTreePush");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        ("Do you want to remove this game from the app?");

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
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pApp->tex_icon.texture.p << " to be released";
          SKIF_ResourcesToFree.push(pApp->tex_icon.texture.p);
          pApp->tex_icon.texture.p = nullptr;
        }

        // Reset selection to Special K
        selection.reset ( );

        //for (auto& app : g_apps)
        //  if (app.second.id == selection.appid && app.second.store == selection.store)
        //    pApp = &app.second;

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

    // TODO: Go through and correct the buf_size of all ImGui::InputText to include the null terminator
    static char charName     [MAX_PATH + 2] = { },
                charPath     [MAX_PATH + 2] = { },
                charArgs     [     500 + 2] = { };
    static bool error = false;

    ImGui::TreePush    ("AddGameTreePush");

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
          WCHAR wszTarget    [MAX_PATH + 2] = { };
          WCHAR wszArguments [MAX_PATH + 2] = { };

          SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH * sizeof (WCHAR));

          if (! PathFileExists (wszTarget))
          {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }

          else {
            std::wstring productName = SKIF_Util_GetProductName (wszTarget);
            productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
            strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
            strncpy (charArgs, SK_WideCharToUTF8 (wszArguments).c_str(),               500);
            strncpy (charName, (productName != L"")
                                ? SK_WideCharToUTF8 (productName).c_str()
                                : pathFilename.c_str(), MAX_PATH);
          }
        }

        else if (pathExtension == L".exe") {
          std::wstring productName = SKIF_Util_GetProductName (path.c_str());
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

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 502, ImVec2(0,0), ImGuiInputTextFlags_None);
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
      int newAppId = SKIF_AddCustomAppID(&g_apps, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs));

      if (newAppId > 0)
      {
        // Attempt to extract the icon from the given executable straight away
        std::wstring SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\icon-original.png)", _path_cache.specialk_userdata, newAppId);
        SKIF_Util_SaveExtractExeIcon (SK_UTF8ToWideChar(charPath), SKIFCustomPath);

        _registry.iLastSelectedGame  = newAppId;
        _registry.iLastSelectedStore = (int)app_record_s::Store::Custom;
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
    static char charName     [MAX_PATH + 2] = { },
                charPath     [MAX_PATH + 2] = { },
                charArgs     [     500 + 2] = { };
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
      strncpy (charPath, pApp->launch_configs[0].getExecutableFullPathUTF8 ( ).c_str(), MAX_PATH);
      strncpy (charArgs, SK_WideCharToUTF8 (pApp->launch_configs[0].getLaunchOptions()).c_str(), 500);
      
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###ModifyGamePopup");
      if (window != nullptr && ! window->Appearing)
        ModifyGamePopup = PopupState_Opened;
    }

    ImGui::TreePush    ("ModifyGameTreePush");

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
          WCHAR wszTarget    [MAX_PATH + 2] = { };
          WCHAR wszArguments [MAX_PATH + 2] = { };

          SKIF_Util_ResolveShortcut (SKIF_ImGui_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH * sizeof (WCHAR));

          if (! PathFileExists (wszTarget))
          {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }

          else {
            std::wstring productName = SKIF_Util_GetProductName (wszTarget);
            productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
            strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
          }
        }

        else if (pathExtension == L".exe") {
          std::wstring productName = SKIF_Util_GetProductName (path.c_str());
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

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 502, ImVec2(0, 0), ImGuiInputTextFlags_None);
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

  
    
  // Confirm prompt

  if (static_proc.pid != 0)
  {
    ImGui::OpenPopup         ("Task Manager###TaskManagerLibrary");

    static std::string warning = "Any unsaved game data may be lost.";
    float fXwarning = ImGui::CalcTextSize (warning.c_str()).x; // DPI related so cannot be static

    ImGui::SetNextWindowSize (
      ImVec2 (
        std::max (ImGui::CalcTextSize (static_proc.name.c_str()).x + 170.0f * SKIF_ImGui_GlobalDPIScale,
                  fXwarning + ImGui::GetStyle().ItemSpacing.x * 2),
        0.0f
      )
    );
    ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    if (ImGui::BeginPopupModal ( "Task Manager###TaskManagerLibrary", nullptr,
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove   |
                                    ImGuiWindowFlags_AlwaysAutoResize )
        )
    {

      ImGui::Text        ("Do you want to end");
      ImGui::SameLine    ( );
      ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), static_proc.name.c_str());
      ImGui::SameLine    ( );
      ImGui::Text        ("?");
      SKIF_ImGui_Spacing ( );


      float fX = (ImGui::GetContentRegionAvail().x - fXwarning) / 2 + ImGui::GetStyle().ItemSpacing.x;
      ImGui::SetCursorPosX (fX);
      ImGui::Text        (warning.c_str());

      SKIF_ImGui_Spacing ( );

      fX = (ImGui::GetContentRegionAvail().x - 200 * SKIF_ImGui_GlobalDPIScale) / 2;

      ImGui::SetCursorPosX (fX);

      if (ImGui::Button ("End Process", ImVec2 (  100 * SKIF_ImGui_GlobalDPIScale,
                                                   25 * SKIF_ImGui_GlobalDPIScale )))
      {
        if (static_proc.handle != INVALID_HANDLE_VALUE)
          SKIF_Util_TerminateProcess (static_proc.handle, 0x0);
        else
          SKIF_Util_TerminateProcess (static_proc.pid,    0x0);

        static_proc = terminate_process_s { };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                             25 * SKIF_ImGui_GlobalDPIScale )))
      {
        static_proc = terminate_process_s { };
        ImGui::CloseCurrentPopup ( );
      }

      ImGui::EndPopup ( );
    }
  }

#pragma endregion

  if (! dragDroppedFilePath.empty())
  {
    if (UpdateGameCover (pApp, dragDroppedFilePath))
    {
      update    = true;
      lastCover.reset(); // Needed as otherwise SKIF would not reload the cover
    }

    dragDroppedFilePath.clear();
  }

  extern uint32_t SelectNewSKIFGame;

  if (SelectNewSKIFGame > 0)
  {
    // Change selection to the new game
    selection.appid = SelectNewSKIFGame;
    selection.store = app_record_s::Store::Custom;

    //for (auto& app : g_apps)
    //  if (app.second.id == selection.appid && app.second.store == selection.store)
    //    pApp = &app.second;

    update = true;

    SelectNewSKIFGame = 0;
  }

  // In case of a device reset, unload all currently loaded textures
  if (invalidatedDevice == 1)
  {   invalidatedDevice  = 2;

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

    if (pSKLogoTexSRV_small.p != nullptr)
    {
      SKIF_ResourcesToFree.push(pSKLogoTexSRV_small.p);
      pSKLogoTexSRV_small.p = nullptr;
    }

    for (auto& app : g_apps)
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
  
  if (_registry.bLibrarySteam)
  {
    if (steamRunning)
      steamFallback = false;
    
    else if (! steamFallback && appinfo != nullptr)
    {
      SK_RunOnce (PLOG_DEBUG << "[AppInfo Processing] Started processing games...");

      bool fallbackAvailable = true;

      for (auto& app : g_apps)
      {
        if (app.second.store != app_record_s::Store::Steam)
          continue;

        if (app.second.id == 0)
          continue;

        if (app.second.id == SKIF_STEAM_APPID)
          continue;

        if (app.second.processed)
          continue;
        
        PLOG_DEBUG << "[AppInfo Processing] " << "[" << ImGui::GetFrameCount ( ) << "] Processing " << app.second.id << "...";
        appinfo->getAppInfo ( app.second.id );

        fallbackAvailable = false;
        break;
      }

      steamFallback = fallbackAvailable;

      if (steamFallback)
        SK_RunOnce (PLOG_DEBUG << "[AppInfo Processing] Finished processing games!");
    }

#if 0
    if (SKIF_StatusBarHelp.empty() && SKIF_StatusBarText.empty())
    {
      SKIF_StatusBarText = "Debug: ";
      SKIF_StatusBarHelp = (steamRunning) ? "Steam client running" : (steamFallback) ? "Steam process fallback active" : "Steam process fallback disabled";
    }
#endif
  }
}

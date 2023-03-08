#include <SKIF_imgui.h>
#include <font_awesome.h>
#include <sk_utility/utility.h>
#include <SKIF_utility.h>
#include <filesystem>
#include <fsutil.h>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>
#include "../../version.h"

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait);

void
SKIF_UI_Tab_DrawSettings (void)
{
  static std::wstring
            driverBinaryPath    = L"";

  enum Status {
    NotInstalled,
    Installed,
    OtherDriverInstalled
  }

  static driverStatus        = NotInstalled,
         driverStatusPending = NotInstalled;

  
  static bool checkedMPOs = false;
  static D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS MPOcaps = {};

  if (! checkedMPOs)
  {
    checkedMPOs = true;

    LUID adapterLuid = { 0 };
    IDXGIDevice* pDXGIDevice = nullptr;
    if (SUCCEEDED(SKIF_D3D11_GetDevice(false)->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice)))
    {
        IDXGIAdapter* pDXGIAdapter = nullptr;
        if (SUCCEEDED(pDXGIDevice->GetAdapter(&pDXGIAdapter)))
        {
            DXGI_ADAPTER_DESC adapterDesc = {};
            if (SUCCEEDED(pDXGIAdapter->GetDesc(&adapterDesc)))
            {
                adapterLuid = adapterDesc.AdapterLuid;
            }
            pDXGIAdapter->Release();
        }
        pDXGIDevice->Release();
    }

    // Open a handle to the adapter using its LUID
    D3DKMT_OPENADAPTERFROMLUID openAdapter;
    openAdapter.AdapterLuid = adapterLuid;
    if (D3DKMTOpenAdapterFromLuid(&openAdapter) == (NTSTATUS)0x00000000L) // STATUS_SUCCESS
    {
      MPOcaps.hAdapter = openAdapter.hAdapter;
      D3DKMTGetMultiPlaneOverlayCaps(&MPOcaps);

      PLOG_INFO << SKIF_LOG_SEPARATOR;
      PLOG_INFO << "MPO Capabilities:";
      PLOG_INFO << "MPO MaxPlanes: "    << MPOcaps.MaxPlanes;
      PLOG_INFO << "MPO MaxRGBPlanes: " << MPOcaps.MaxRGBPlanes;
      PLOG_INFO << "MPO MaxYUVPlanes: " << MPOcaps.MaxYUVPlanes;
      PLOG_INFO << "MPO Stretch: "      << MPOcaps.MaxStretchFactor << "x - " << MPOcaps.MaxShrinkFactor << "x";
      PLOG_INFO << SKIF_LOG_SEPARATOR;
    }
  }

  // Check if the WinRing0_1_2_0 kernel driver service is installed or not
  auto _CheckDriver = [](Status& _status, bool forced = false)->std::wstring
  {
    std::wstring       binaryPath = L"";
    SC_HANDLE        schSCManager = NULL,
                      svcWinRing0 = NULL;
    LPQUERY_SERVICE_CONFIG   lpsc = {  };
    DWORD                    dwBytesNeeded,
                              cbBufSize {},
                              dwError;

    static DWORD dwLastRefresh = 0;

    // Refresh once every 500 ms
    if (forced || (dwLastRefresh < SKIF_Util_timeGetTime() && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ))))
    {
      dwLastRefresh = SKIF_Util_timeGetTime() + 500;

      // Reset the current status to not installed.
      _status = NotInstalled;

      // Retrieve the install folder.
      static std::wstring dirNameInstall  = std::filesystem::path (path_cache.specialk_install ).filename();
      static std::wstring dirNameUserdata = std::filesystem::path (path_cache.specialk_userdata).filename();

      // Get a handle to the SCM database.
      schSCManager =
        OpenSCManager (
          nullptr,             // local computer
          nullptr,             // servicesActive database
          STANDARD_RIGHTS_READ // enumerate services
        );

      if (nullptr != schSCManager)
      {
        // Get a handle to the service.
        svcWinRing0 =
          OpenService (
            schSCManager,        // SCM database
            L"SK_WinRing0",      // name of service // Old: WinRing0_1_2_0
            SERVICE_QUERY_CONFIG // query config
          );

        if (nullptr != svcWinRing0)
        {
          // Attempt to get the configuration information to get an idea of what buffer size is required.
          if (! QueryServiceConfig (
                  svcWinRing0,
                    nullptr, 0,
                      &dwBytesNeeded )
              )
          {
            dwError =
              GetLastError ();

            if (ERROR_INSUFFICIENT_BUFFER == dwError)
            {
              cbBufSize = dwBytesNeeded;
              lpsc      = (LPQUERY_SERVICE_CONFIG)LocalAlloc (LMEM_FIXED, cbBufSize);

              // Get the configuration information with the necessary buffer size.
              if (lpsc != nullptr && 
                    QueryServiceConfig (
                      svcWinRing0,
                        lpsc, cbBufSize,
                          &dwBytesNeeded )
                 )
              {
                // Store the binary path of the installed driver.
                binaryPath = std::wstring (lpsc->lpBinaryPathName);
                binaryPath = binaryPath.substr(4); // Strip \??\\

                PLOG_INFO << "Found kernel driver SK_WinRing0 installed at: " << binaryPath;

                if (PathFileExists (binaryPath.c_str()))
                {
                  _status = Installed; // File exists, so driver is installed
                }
                else {
                  _status = NotInstalled; // File does not actually exist, so driver has been uninstalled
                }

                /* Old method -- irrelevant now that SK_WinRing0 is the new name
                // Check if the installed driver exists in the install folder
                if (binaryPath.find (dirNameInstall ) != std::wstring::npos || 
                    binaryPath.find (dirNameUserdata) != std::wstring::npos)
                {
                  if (PathFileExists (binaryPath.c_str()))
                  {
                    _status = Installed; // File exists, so driver is installed
                  }
                  else {
                    _status = NotInstalled; // File does not actually exist, so driver has been uninstalled
                  }
                }
                else {
                  _status = OtherDriverInstalled; // Other driver installed
                }
                */
              }
              else {
                PLOG_ERROR << "QueryServiceConfig failed with exception: " << SKIF_Util_GetLastError ( );
              }

              LocalFree (lpsc);
            }
            else {
              PLOG_WARNING << "Unexpected behaviour occurred: " << SKIF_Util_GetLastError ( );
            }
          }
          else {
            PLOG_WARNING << "Unexpected behaviour occurred: " << SKIF_Util_GetLastError();
          }

          CloseServiceHandle (svcWinRing0);
        }
        else {
          PLOG_ERROR << "OpenService failed with exception: " << SKIF_Util_GetLastError();
        }

        CloseServiceHandle (schSCManager);
      }
      else {
        PLOG_ERROR << "OpenSCManager failed with exception: " << SKIF_Util_GetLastError ( );
      }
    }

    return binaryPath;
  };

  // Driver is supposedly getting a new state -- check if its time for an
  //  update on each frame until driverStatus matches driverStatusPending
  if (driverStatusPending != driverStatus)
    driverBinaryPath = _CheckDriver (driverStatus);

  // Reset and refresh things when visiting from another tab
  if (SKIF_Tab_Selected != Settings)
  {
    driverBinaryPath    = _CheckDriver (driverStatus, true);
    driverStatusPending =               driverStatus;

    //_inject._RefreshSKDLLVersions ();
  }

  SKIF_Tab_Selected = Settings;
  if (SKIF_Tab_ChangeTo == Settings)
    SKIF_Tab_ChangeTo = None;

  // SKIF Options
  //if (ImGui::CollapsingHeader ("Frontend v " SKIF_VERSION_STR_A " (" __DATE__ ")###SKIF_SettingsHeader-1", ImGuiTreeNodeFlags_DefaultOpen))
  //{
  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  //ImGui::Spacing    ( );

  SKIF_ImGui_Spacing      ( );

  SKIF_ImGui_Columns      (2, nullptr, true);

  SK_RunOnce(
    ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
  );
          
  if ( ImGui::Checkbox ( "Low bandwidth mode",                          &SKIF_bLowBandwidthMode ) )
    _registry.regKVLowBandwidthMode.putData (                                      SKIF_bLowBandwidthMode );
          
  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip (
    "For new games/covers, low resolution images will be preferred over high-resolution ones.\n"
    "This only affects new downloads of covers. It does not affect already downloaded covers.\n"
    "This will also disable automatic downloads of new updates to Special K."
  );

  if ( ImGui::Checkbox ( "Prefer launching GOG games through Galaxy", &SKIF_bPreferGOGGalaxyLaunch) )
    _registry.regKVPreferGOGGalaxyLaunch.putData (SKIF_bPreferGOGGalaxyLaunch);

  if ( ImGui::Checkbox ( "Remember the last selected game",         &SKIF_bRememberLastSelected ) )
    _registry.regKVRememberLastSelected.putData (                              SKIF_bRememberLastSelected );
            
  if ( ImGui::Checkbox ( "Minimize when launching a game",             &SKIF_bMinimizeOnGameLaunch ) )
    _registry.regKVMinimizeOnGameLaunch.putData (                                      SKIF_bMinimizeOnGameLaunch );
            
  if ( ImGui::Checkbox ( "Close to the notification area", &SKIF_bCloseToTray ) )
    _registry.regKVCloseToTray.putData (                                               SKIF_bCloseToTray );

  _inject._StartAtLogonCtrl ( );

  ImGui::NextColumn    ( );

  // New column
          
  ImGui::BeginGroup    ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip ("This determines how long the service will remain running when launching a game.\n"
                          "Move the mouse over each option to get more information");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Auto-stop behavior when launching a game:"
  );
  ImGui::TreePush        ("SKIF_iAutoStopBehavior");

  //if (ImGui::RadioButton ("Never",           &SKIF_iAutoStopBehavior, 0))
  //  regKVAutoStopBehavior.putData (           SKIF_iAutoStopBehavior);
  // 
  //ImGui::SameLine        ( );

  if (ImGui::RadioButton ("Stop on injection",    &SKIF_iAutoStopBehavior, 1))
    _registry.regKVAutoStopBehavior.putData (             SKIF_iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K successfully injects into a game.");

  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Stop on game exit",      &SKIF_iAutoStopBehavior, 2))
    _registry.regKVAutoStopBehavior.putData (             SKIF_iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K detects that the game is being closed.");

  ImGui::TreePop         ( );

  ImGui::Spacing         ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip ("This setting has no effect if low bandwidth mode is enabled.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Check for updates to Special K:"
  );

  if (SKIF_bLowBandwidthMode)
  {
    // Disable buttons
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
  }

  ImGui::BeginGroup    ( );

  ImGui::TreePush        ("SKIF_iCheckForUpdates");
  if (ImGui::RadioButton ("Never",                 &SKIF_iCheckForUpdates, 0))
    _registry.regKVCheckForUpdates.putData (                  SKIF_iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Weekly",                &SKIF_iCheckForUpdates, 1))
    _registry.regKVCheckForUpdates.putData (                  SKIF_iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("On each launch",        &SKIF_iCheckForUpdates, 2))
    _registry.regKVCheckForUpdates.putData (                  SKIF_iCheckForUpdates);
  ImGui::TreePop         ( );

  ImGui::EndGroup      ( );

  extern std::vector <std::pair<std::string, std::string>> updateChannels;

  if (! updateChannels.empty())
  {
    ImGui::TreePush        ("Push_UpdateChannel");

    ImGui::BeginGroup    ( );

    static std::pair<std::string, std::string>  empty           = std::pair("", "");
    static std::pair<std::string, std::string>* selectedChannel = &empty;

    static bool
        firstRun = true;
    if (firstRun)
    {   firstRun = false;
      for (auto& updateChannel : updateChannels)
        if (updateChannel.first == SK_WideCharToUTF8 (_registry.wsUpdateChannel))
          selectedChannel = &updateChannel;
    }

    if (ImGui::BeginCombo ("##SKIF_wzUpdateChannel", selectedChannel->second.c_str()))
    {
      for (auto& updateChannel : updateChannels)
      {
        bool is_selected = (selectedChannel->first == updateChannel.first);

        if (ImGui::Selectable (updateChannel.second.c_str(), is_selected) && updateChannel.first != selectedChannel->first)
        {
          // Update selection
          selectedChannel = &updateChannel;

          // Update channel
          _registry.wsUpdateChannel = SK_UTF8ToWideChar (selectedChannel->first);
          _registry.wsIgnoreUpdate  = L"";
          _registry.regKVFollowUpdateChannel.putData (_registry.wsUpdateChannel);
          _registry.regKVIgnoreUpdate       .putData (_registry.wsIgnoreUpdate);

          // Trigger a new check for updates
          extern bool changedUpdateChannel, SKIF_UpdateReady, showUpdatePrompt;
          extern volatile LONG update_thread;
          extern SKIF_UpdateCheckResults newVersion;

          changedUpdateChannel = true;
          SKIF_UpdateReady     = showUpdatePrompt = false;
          newVersion.filename.clear();
          newVersion.description.clear();
          InterlockedExchange (&update_thread, 0);
        }

        if (is_selected)
            ImGui::SetItemDefaultFocus ( );
      }

      ImGui::EndCombo  ( );
    }

    ImGui::EndGroup      ( );

    ImGui::TreePop       ( );
  }

  else if (SKIF_iCheckForUpdates > 0) {
    ImGui::TreePush      ("Push_UpdateChannel");
    ImGui::BeginGroup    ( );
    ImGui::TextColored   (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
                          "A restart is required to populate the update channels.");
    ImGui::EndGroup      ( );
    ImGui::TreePop       ( );
  }

  if (SKIF_bLowBandwidthMode)
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
  }

  ImGui::Spacing       ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip ("This provides contextual notifications in Windows when the service starts or stops.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Show Windows notifications:"
  );
  ImGui::TreePush        ("SKIF_iNotifications");
  if (ImGui::RadioButton ("Never",          &SKIF_iNotifications, 0))
    _registry.regKVNotifications.putData (             SKIF_iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Always",         &SKIF_iNotifications, 1))
    _registry.regKVNotifications.putData (             SKIF_iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("When unfocused", &SKIF_iNotifications, 2))
    _registry.regKVNotifications.putData (             SKIF_iNotifications);
  ImGui::TreePop         ( );

  ImGui::Spacing       ( );
            
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Hide games from select platforms:"
  );
  ImGui::TreePush      ("");

  if (ImGui::Checkbox        ("Epic", &SKIF_bDisableEGSLibrary))
  {
    _registry.regKVDisableEGSLibrary.putData    (SKIF_bDisableEGSLibrary);
    RepopulateGames = true;
  }

  ImGui::SameLine ( );
  ImGui::Spacing  ( );
  ImGui::SameLine ( );

  if (ImGui::Checkbox         ("GOG", &SKIF_bDisableGOGLibrary))
  {
    _registry.regKVDisableGOGLibrary.putData    (SKIF_bDisableGOGLibrary);
    RepopulateGames = true;
  }

  ImGui::SameLine ( );
  ImGui::Spacing  ( );
  ImGui::SameLine ( );

  if (ImGui::Checkbox       ("Steam", &SKIF_bDisableSteamLibrary))
  {
    _registry.regKVDisableSteamLibrary.putData  (SKIF_bDisableSteamLibrary);
    RepopulateGames = true;
  }

  ImGui::SameLine ( );
  ImGui::Spacing  ( );
  ImGui::SameLine ( );

  if (ImGui::Checkbox        ("Xbox", &SKIF_bDisableXboxLibrary))
  {
    _registry.regKVDisableXboxLibrary.putData   (SKIF_bDisableXboxLibrary);
    RepopulateGames = true;
  }

  ImGui::TreePop       ( );

  ImGui::Columns    (1);

  ImGui::PopStyleColor();

  ImGui::Spacing ();
  ImGui::Spacing ();

  if (ImGui::CollapsingHeader ("Appearance###SKIF_SettingsHeader-1"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    //ImGui::Spacing    ( );

    SKIF_ImGui_Spacing      ( );

    SKIF_ImGui_Columns      (2, nullptr, true);

    SK_RunOnce(
      ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
    );
            
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip ("Useful if you find bright white covers an annoyance.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Dim game covers by 25%%:"
    );
    ImGui::TreePush        ("SKIF_iDimCovers");
    if (ImGui::RadioButton ("Never",                 &SKIF_iDimCovers, 0))
      _registry.regKVDimCovers.putData (                        SKIF_iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                &SKIF_iDimCovers, 1))
      _registry.regKVDimCovers.putData (                        SKIF_iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Based on mouse cursor", &SKIF_iDimCovers, 2))
      _registry.regKVDimCovers.putData (                        SKIF_iDimCovers);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );
          
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information.");
    ImGui::SameLine        ( );
    ImGui::TextColored     (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Disable UI elements:"
    );
    ImGui::TreePush        ("");

    if (ImGui::Checkbox ("HiDPI scaling", &SKIF_bDisableDPIScaling))
    {
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

      if (SKIF_bDisableDPIScaling)
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleFonts;

      _registry.regKVDisableDPIScaling.putData      (SKIF_bDisableDPIScaling);
    }

    SKIF_ImGui_SetHoverTip (
      "This application will appear smaller on HiDPI monitors."
    );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox ("Tooltips", &SKIF_bDisableTooltips))
      _registry.regKVDisableTooltips.putData (  SKIF_bDisableTooltips);

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = "Info: ";

    SKIF_ImGui_SetHoverText ("This is where the info will be displayed.");
    SKIF_ImGui_SetHoverTip  ("The info will instead be displayed in the status bar at the bottom."
                              "\nNote that some links cannot be previewed as a result.");

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox ("Status bar", &SKIF_bDisableStatusBar))
      _registry.regKVDisableStatusBar.putData (   SKIF_bDisableStatusBar);

    SKIF_ImGui_SetHoverTip (
      "Combining this with disabled UI tooltips will hide all context based information or tips."
    );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox ("Borders", &SKIF_bDisableBorders))
    {
      _registry.regKVDisableBorders.putData (  SKIF_bDisableBorders);
      if (SKIF_bDisableBorders)
      {
        ImGui::GetStyle().TabBorderSize   = 0.0F;
        ImGui::GetStyle().FrameBorderSize = 0.0F;
      }
      else {
        ImGui::GetStyle().TabBorderSize   = 1.0F * SKIF_ImGui_GlobalDPIScale;
        ImGui::GetStyle().FrameBorderSize = 1.0F * SKIF_ImGui_GlobalDPIScale;
      }
      if (SKIF_iStyle == 0)
        SKIF_ImGui_StyleColorsDark ( );
    }

    if (SKIF_bDisableTooltips &&
        SKIF_bDisableStatusBar)
    {
      ImGui::BeginGroup     ( );
      ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
      ImGui::SameLine       ( );
      ImGui::TextColored    (ImColor(0.68F, 0.68F, 0.68F, 1.0f), "Context based information or tips will not appear!");
      ImGui::EndGroup       ( );
    }

    ImGui::TreePop       ( );

    ImGui::NextColumn    ( );
            
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip ("Every time the UI renders a frame, Shelly the Ghost moves a little bit.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Show Shelly the Ghost:"
    );
    ImGui::TreePush        ("SKIF_iGhostVisibility");
    if (ImGui::RadioButton ("Never",                    &SKIF_iGhostVisibility, 0))
      _registry.regKVGhostVisibility.putData (                     SKIF_iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                   &SKIF_iGhostVisibility, 1))
      _registry.regKVGhostVisibility.putData (                     SKIF_iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("While service is running", &SKIF_iGhostVisibility, 2))
      _registry.regKVGhostVisibility.putData (                     SKIF_iGhostVisibility);
    ImGui::TreePop         ( );

    ImGui::Spacing       ( );

    // Only show if OS supports tearing in windowed mode
    if (SKIF_bAllowTearing)
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
      SKIF_ImGui_SetHoverTip ("Controls UI latency for specific types of displays");
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                              "UI Refresh Mode:"
      );

      enum SKIF_SyncModes {
        Sync_VRR_Compat = 0,
        Sync_None       = 1
      };

      int SKIF_iSyncMode =
        SKIF_bDisableVSYNC ? Sync_None
                           : Sync_VRR_Compat;

      ImGui::TreePush        ("SKIF_iSyncMode");
      if (ImGui::RadioButton ("VRR Compatibility", &SKIF_iSyncMode, Sync_VRR_Compat))
        _registry.regKVDisableVSYNC.putData ((SKIF_bDisableVSYNC = false));
      SKIF_ImGui_SetHoverTip (
        "Sluggish UI, but avoids variable-refresh signal loss"
      );
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("VSYNC Off",         &SKIF_iSyncMode, Sync_None))
        _registry.regKVDisableVSYNC.putData ((SKIF_bDisableVSYNC = true));
      SKIF_ImGui_SetHoverTip (
        "Improved UI response on low fixed-refresh rate displays"
      );
      ImGui::TreePop         ( );
      ImGui::Spacing         ( );
    }

    const char* StyleItems[] = { "SKIF Dark",
                                 "ImGui Dark",
                                 "ImGui Light",
                                 "ImGui Classic"
    };
    static const char* StyleItemsCurrent = StyleItems[SKIF_iStyle];
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color theme: (restart required)"
    );
    ImGui::TreePush      ("");

    if (ImGui::BeginCombo ("##SKIF_iStyleCombo", StyleItemsCurrent)) // The second parameter is the label previewed before opening the combo.
    {
        for (int n = 0; n < IM_ARRAYSIZE (StyleItems); n++)
        {
            bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
            if (ImGui::Selectable (StyleItems[n], is_selected))
            {
              SKIF_iStyle = n;
              _registry.regKVStyle.putData  (SKIF_iStyle);
              StyleItemsCurrent = StyleItems[SKIF_iStyle];
              // Apply the new Dear ImGui style
              //SKIF_SetStyle ( );
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
        }
        ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::Columns       (1);

    ImGui::PopStyleColor ( );

  }

  ImGui::Spacing ();
  ImGui::Spacing ();

  if (ImGui::CollapsingHeader ("Advanced###SKIF_SettingsHeader-2"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    SKIF_ImGui_Columns      (2, nullptr, true);

    SK_RunOnce(
      ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
    );

    if ( ImGui::Checkbox ( "Always open this app on the same monitor as the mouse", &SKIF_bOpenAtCursorPosition ) )
      _registry.regKVOpenAtCursorPosition.putData (                                            SKIF_bOpenAtCursorPosition );

    if ( ImGui::Checkbox (
            "Allow multiple instances of this app",
              &SKIF_bAllowMultipleInstances )
        )
    {
      if (! SKIF_bAllowMultipleInstances)
      {
        // Immediately close out any duplicate instances, they're undesirables
        EnumWindows ( []( HWND   hWnd,
                          LPARAM lParam ) -> BOOL
        {
          wchar_t                         wszRealWindowClass [64] = { };
          if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
          {
            if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
            {
              if (SKIF_hWnd != hWnd)
                PostMessage (  hWnd, WM_QUIT,
                                0x0, 0x0  );
            }
          }
          return TRUE;
        }, (LPARAM)SKIF_WindowClass);
      }

      _registry.regKVAllowMultipleInstances.putData (
        SKIF_bAllowMultipleInstances
        );
    }

    ImGui::NextColumn       ( );

    if (ImGui::Checkbox  ("Do not stop the injection service when this app closes",
                                            &SKIF_bAllowBackgroundService))
      _registry.regKVAllowBackgroundService.putData (  SKIF_bAllowBackgroundService);

    const char* LogSeverity[] = { "None",
                                  "Fatal",
                                  "Error",
                                  "Warning",
                                  "Info",
                                  "Debug",
                                  "Verbose" };
    static const char* LogSeverityCurrent = LogSeverity[SKIF_iLogging];
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Logging:"
    );

    ImGui::SameLine();

    if (ImGui::BeginCombo ("##SKIF_iLoggingCombo", LogSeverityCurrent)) // The second parameter is the label previewed before opening the combo.
    {
        for (int n = 0; n < IM_ARRAYSIZE (LogSeverity); n++)
        {
            bool is_selected = (LogSeverityCurrent == LogSeverity[n]); // You can store your selection however you want, outside or inside your objects
            if (ImGui::Selectable (LogSeverity[n], is_selected))
            {
              SKIF_iLogging = n;
              _registry.regKVLogging.putData  (SKIF_iLogging);
              LogSeverityCurrent = LogSeverity[SKIF_iLogging];
              plog::get()->setMaxSeverity((plog::Severity)SKIF_iLogging);
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
        }
        ImGui::EndCombo  ( );
    }

    ImGui::Columns          (1);

    ImGui::PopStyleColor    ( );

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    ImGui::Separator   ();

    // PresentMon prerequisites
    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "SwapChain Presentation Monitor"
    );
            
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

    ImGui::TextWrapped    (
      "Special K can give users an insight into how frames are presented by tracking ETW events and changes as they occur."
    );

    ImGui::Spacing     ();
    ImGui::Spacing     ();

    SKIF_ImGui_Columns      (2, nullptr, true);

            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Tell at a glance whether:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXTERNAL_LINK_ALT);
    ImGui::SameLine    ();
    ImGui::TextWrapped ("DirectFlip optimizations are engaged, and desktop composition (DWM) is bypassed.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip("Appears as 'Hardware: Independent Flip' or 'Hardware Composed: Independent Flip'");
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/en/SwapChain#fse-fso-independent-flip-etc-sorry-but-what");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/en/SwapChain#fse-fso-independent-flip-etc-sorry-but-what");

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXTERNAL_LINK_ALT);
    ImGui::SameLine    ();
    ImGui::TextWrapped ("Legacy Exclusive Fullscreen (FSE) mode has enaged or if Fullscreen Optimizations (FSO) overrides it.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip(
                        "FSE appears as 'Hardware: Legacy Flip' or 'Hardware: Legacy Copy to front buffer'"
                        "\nFSO appears as 'Hardware: Independent Flip' or 'Hardware Composed: Independent Flip'"
    );
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine    ();
    ImGui::TextWrapped ("The game is running in a suboptimal presentation mode.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip("Appears as 'Composed: Flip', 'Composed: Composition Atlas',"
                            "\n'Composed: Copy with CPU GDI', or 'Composed: Copy with GPU GDI'");

    ImGui::Spacing();
    ImGui::Spacing();
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Requirement:"
    );

    static BOOL  pfuAccessToken = FALSE;
    static BYTE  pfuSID[SECURITY_MAX_SID_SIZE];
    static BYTE  intSID[SECURITY_MAX_SID_SIZE];
    static DWORD pfuSize = sizeof(pfuSID);
    static DWORD intSize = sizeof(intSID);

    SK_RunOnce (CreateWellKnownSid   (WELL_KNOWN_SID_TYPE::WinBuiltinPerfLoggingUsersSid, NULL, &pfuSID, &pfuSize));
    SK_RunOnce (CreateWellKnownSid   (WELL_KNOWN_SID_TYPE::WinInteractiveSid,             NULL, &intSID, &intSize));
    SK_RunOnce (CheckTokenMembership (NULL, &pfuSID, &pfuAccessToken));

    enum pfuPermissions {
      Missing,
      Granted,
      Pending
    } static pfuState = (pfuAccessToken) ? Granted : Missing;

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Granted 'Performance Log Users' permission?");
    ImGui::SameLine    ();
    if      (pfuState == Granted)
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Yes");
    else if (pfuState == Missing)
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),    "No");
    else // (pfuState == Pending)
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),    "Restart required"); //"Yes, but a sign out from Windows is needed to allow the changes to take effect.");
    ImGui::EndGroup    ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Disable button for granted + pending states
    if (pfuState != Missing)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    std::string btnPfuLabel = (pfuState == Granted) ?                                ICON_FA_CHECK " Permissions granted!" // Granted
                                                    : (pfuState == Missing) ?   ICON_FA_SHIELD_ALT " Grant permissions"    // Missing
                                                                            : ICON_FA_SIGN_OUT_ALT " Sign out to apply";   // Pending

    if ( ImGui::ButtonEx ( btnPfuLabel.c_str(), ImVec2( 200 * SKIF_ImGui_GlobalDPIScale,
                                                         25 * SKIF_ImGui_GlobalDPIScale)))
    {
      std::wstring exeArgs;

      TCHAR pfuName[MAX_PATH],
            intName[MAX_PATH];
      DWORD pfuNameLength = sizeof(pfuName),
            intNameLength = sizeof(intName);

      // Unused variables
      SID_NAME_USE pfuSnu, intSnu;
      TCHAR pfuDomainName[MAX_PATH], 
            intDomainName[MAX_PATH];
      DWORD pfuDomainNameLength = sizeof(pfuDomainName),
            intDomainNameLength = sizeof(intDomainName);

      // Because non-English languages has localized user and group names, we need to retrieve those first
      if (LookupAccountSid (NULL, pfuSID, pfuName, &pfuNameLength, pfuDomainName, &pfuDomainNameLength, &pfuSnu) &&
          LookupAccountSid (NULL, intSID, intName, &intNameLength, intDomainName, &intDomainNameLength, &intSnu))
      {
        exeArgs = LR"(localgroup ")" + std::wstring(pfuName) + LR"(" ")" + std::wstring(intName) + LR"(" /add)";

        // Use 'net' to grant the proper permissions
        if (ShellExecuteW (nullptr, L"runas", L"net", exeArgs.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32)
          pfuState = Pending;
      }

      /*
      if (SKIF_Util_IsWindows10OrGreater ( )) // On Windows 10, use the native PowerShell cmdlet Add-LocalGroupMember since it supports SIDs
        exeArgs  = LR"(-NoProfile -NonInteractive -WindowStyle Hidden -Command "Add-LocalGroupMember -SID 'S-1-5-32-559' -Member 'S-1-5-4'")";
      else                               // Windows 8.1 lacks Add-LocalGroupMember, so fall back on using WMI (to retrieve the localized names of the group and user) and NET to add the user to the group
        exeArgs  = LR"(-NoProfile -NonInteractive -WindowStyle Hidden -Command "$Group = (Get-WmiObject -Class Win32_Group -Filter 'LocalAccount = True AND SID = \"S-1-5-32-559\"').Name; $User = (Get-WmiObject -Class Win32_SystemAccount -Filter 'LocalAccount = True AND SID = \"S-1-5-4\"').Name; net localgroup \"$Group\" \"$User\" /add")";

      if (ShellExecuteW (nullptr, L"runas", L"powershell", exeArgs.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32) // COM exception is thrown?
        pfuState = Pending;
      */
    }

    // Disable button for granted + pending states
    else if (pfuState != Missing)
    {
      ImGui::PopStyleVar();
      ImGui::PopItemFlag();
    }

    else
    {
      SKIF_ImGui_SetHoverTip(
        "Administrative privileges are required on the system to toggle this."
      );
    }

    ImGui::EndGroup ();

    ImGui::NextColumn  ();

    ImGui::TreePush    ();
            

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_THUMBS_UP);
    ImGui::SameLine    ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Minimal latency:");

    ImGui::TreePush    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Hardware: Independent Flip");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Hardware Composed: Independent Flip");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Hardware: Legacy Flip");

    /* Extremely uncommon so currently not included in the list
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F), "Hardware: Legacy Copy to front buffer");
    */
    ImGui::TreePop     ();

    ImGui::BeginGroup  ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine    ();
    ImGui::Text        ("MPO Planes Supported:");
    ImGui::SameLine    ();
    ImGui::TextColored (
      (MPOcaps.MaxPlanes > 1)
        ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)
        : ImColor::HSV(0.11F, 1.F, 1.F),
      (std::to_string(MPOcaps.MaxPlanes) + " plane" + ((MPOcaps.MaxPlanes > 1) ? "s  " ICON_FA_THUMBS_UP : "  " ICON_FA_THUMBS_DOWN)).c_str()
    );
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip ("Multi-Plane Overlays (MPOs) are additional dedicated hardware scanout planes\n"
                            "enabling the GPU to partially take over composition from the DWM. This allows\n"
                            "games to bypass the DWM in various mixed scenarios or window modes,\n"
                            "eliminating the input latency that would otherwise be incurred, such as when\n"
                            "notifications or window-based overlays (e.g. Game Bar) appear above the game.\n"
                            "\n"
                            "MPOs requires a newer GPU, such as an Nvidia 20-series card or newer."
    );

    SKIF_ImGui_Spacing ();
            
    ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), ICON_FA_THUMBS_DOWN);
    ImGui::SameLine    ();
    ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), "Undesireable latency:");

    ImGui::TreePush    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Flip");

    /* Disabled as PresentMon doesn't detect this any longer as of May 2022.
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Composition Atlas");
    */

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Copy with GPU GDI");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Copy with CPU GDI");
    ImGui::TreePop     ();

    ImGui::TreePop     ();

    ImGui::Columns     (1);

#ifdef _WIN64
    ImGui::Spacing  ();
    ImGui::Spacing  ();

    ImGui::Separator   ();

    // WinRing0
    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Extended CPU Hardware Reporting"
    );

    ImGui::TextWrapped    (
      "Special K can make use of an optional kernel driver to provide additional metrics in the CPU widget."
    );

    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Extends the CPU widget with thermals, energy, and precise clock rate on modern hardware.");
    ImGui::EndGroup    ();

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Requirement:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
    ImGui::SameLine    ();
    ImGui::Text        ("Kernel Driver:");
    ImGui::SameLine    ();

    static std::string btnDriverLabel;
    static std::wstring wszDriverTaskCmd;
    //static LPCSTR szDriverTaskFunc;

    // Status is pending...
    if (driverStatus != driverStatusPending)
    {
      btnDriverLabel = ICON_FA_SPINNER " Please Wait...";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Pending...");
    }

    // Driver is installed
    else if (driverStatus == Installed)
    {
      btnDriverLabel    = ICON_FA_SHIELD_ALT " Uninstall Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Installed");
      wszDriverTaskCmd = L"Uninstall";
    }

    // Other driver is installed
    else if (driverStatus == OtherDriverInstalled)
    {
      btnDriverLabel    = ICON_FA_BAN " Unavailable";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Unsupported");
      ImGui::Spacing     ();
      ImGui::SameLine    ();
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"• ");
      ImGui::SameLine    ();
      ImGui::Text        ("Conflict With:");
      ImGui::SameLine    ();
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), SK_WideCharToUTF8 (driverBinaryPath).c_str ());
    }

    // Driver is not installed
    else {
      btnDriverLabel    = ICON_FA_SHIELD_ALT " Install Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Not Installed");
      wszDriverTaskCmd = L"Install";
    }

    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Disable button if the required files are missing, status is pending, or if another driver is installed
    if (  driverStatusPending != driverStatus ||
          OtherDriverInstalled == driverStatus )
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    // Show button
    bool driverButton =
      ImGui::ButtonEx (btnDriverLabel.c_str(), ImVec2(200 * SKIF_ImGui_GlobalDPIScale,
                                                       25 * SKIF_ImGui_GlobalDPIScale));

    //
    // COM Elevation Moniker will handle this unless UAC level is at maximum,
    //   then user must respond to UAC prompt (probably in a background window).
    //
    SKIF_ImGui_SetHoverTip (
      "Administrative privileges are required on the system to enable this."
    );

    if ( driverButton )
    {
      std::filesystem::path SKIFdrv = std::filesystem::path(std::filesystem::current_path().wstring() + LR"(\Drivers\WinRing0\SKIFdrv.exe)");

      if (ShellExecuteW (nullptr, L"runas", SKIFdrv.c_str(), wszDriverTaskCmd.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32)
        driverStatusPending =
              (driverStatus == Installed) ?
                            NotInstalled  : Installed;

      /* Old method before SKIFdrv.exe
      auto hModWinRing0 =
        LoadLibraryW (L"SpecialK64.dll");

      using DriverTaskFunc_pfn = void (*)(void);
            DriverTaskFunc_pfn DriverTask = (DriverTaskFunc_pfn)
              GetProcAddress (hModWinRing0, szDriverTaskFunc);

      if (DriverTask != nullptr)
      {
        DriverTask ();
        //DriverTask (); // Not needed any longer

        // Batch call succeeded -- change driverStatusPending to the
        //   opposite of driverStatus to signal that a new state is pending.
        driverStatusPending =
              (driverStatus == Installed) ?
                              NotInstalled : Installed;
      }

      FreeLibrary (hModWinRing0);
      */
    }

    // Disabled button
    //   the 'else if' is only to prevent the code from being called on the same frame as the button is pressed
    else if (    driverStatusPending != driverStatus ||
                OtherDriverInstalled == driverStatus )
    {
      ImGui::PopStyleVar ();
      ImGui::PopItemFlag ();
    }

    // Show warning about another driver being installed
    if (OtherDriverInstalled == driverStatus)
    {
      ImGui::SameLine   ();
      ImGui::BeginGroup ();
      ImGui::Spacing    ();
      ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
                                                "Option is unavailable as another application have already installed a copy of the driver."
      );
      ImGui::EndGroup   ();
    }

    ImGui::EndGroup ();
#endif

    ImGui::PopStyleColor ();
  }

  ImGui::Spacing ();
  ImGui::Spacing ();

  // Whitelist/Blacklist
  if (ImGui::CollapsingHeader ("Whitelist / Blacklist###SKIF_SettingsHeader-3", ImGuiTreeNodeFlags_DefaultOpen))
  {
    static bool white_edited = false,
                black_edited = false,
                white_stored = true,
                black_stored = true;

    auto _CheckWarnings = [](char* szList)->void
    {
      static int i, count;

      if (strchr (szList, '\"') != nullptr)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_EXCLAMATION_TRIANGLE);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "Please remove all double quotes");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), R"( " )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "from the list.");
        ImGui::EndGroup   ();
      }

      // Loop through the list, checking the existance of a lone \ not proceeded or followed by other \.
      // i == 0 to prevent szList[i - 1] from executing when at the first character.
      for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 128 * 2; i++)
        count += (szList[i] == '\\' && szList[i + 1] != '\\' && (i == 0 || szList[i - 1] != '\\'));

      if (count > 0)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_EXCLAMATION_CIRCLE);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "Folders must be separated using two backslashes");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), R"( \\ )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "instead of one");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), R"( \ )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "backslash.");
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          R"(e.g. C:\\Program Files (x86)\\Uplay\\games)"
        );
      }

      // Loop through the list, counting the number of occurances of a newline
      for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 128 * 2; i++)
        count += (szList[i] == '\n');

      if (count >= 128)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_EXCLAMATION_CIRCLE);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "The list can only include");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure),   " 128 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "lines, though multiple can be combined using a pipe");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success),   " | ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "character.");
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          R"(e.g. "NieRAutomataPC|Epic Games" will match any application"
                  "installed under a NieRAutomataPC or Epic Games folder.)"
        );
      }
    };

    ImGui::BeginGroup ();
    ImGui::Spacing    ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    ("The following lists manage Special K in processes as patterns are matched against the full path of the injected process.");

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine   (); ImGui::Text        ("Easiest is to use the name of the executable or folder of the game.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at"
        "\nC:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"
    );

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine   (); ImGui::Text        ("Typing the name of a shared parent folder will match all applications below that folder.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Epic Games\" will match any"
        "\napplication installed under the Epic Games folder."
    );

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine   (); ImGui::Text ("Note that these lists do not prevent Special K from being injected into processes.");
    ImGui::EndGroup   ();

    if (SKIF_bDisableTooltips)
    {
      SKIF_ImGui_SetHoverTip (
        "These lists control whether Special K should be enabled (the whitelist) to hook APIs etc,"
        "\nor remain disabled/idle/inert (the blacklist) within the injected process."
      );
    }

    else
    {
      SKIF_ImGui_SetHoverTip (
        "The global injection service injects Special K into any process that deals"
        "\nwith system input or some sort of window or keyboard/mouse input activity."
        "\n\n"


        "These lists control whether Special K should be enabled (the whitelist),"
        "\nor remain idle/inert (the blacklist) within the injected process."
      );
    }

    /*
    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXTERNAL_LINK_ALT);
    ImGui::SameLine   (); ImGui::Text        ("More on the wiki.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

    if (ImGui::IsItemClicked ())
      SKIF_Util_OpenURI (L"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");
    */

    ImGui::PopStyleColor  ();

    ImGui::NewLine    ();

    // Whitelist section

    ImGui::BeginGroup ();

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_PLUS_CIRCLE);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Whitelist Patterns:"
    );

    SKIF_ImGui_Spacing ();

    white_edited |=
      ImGui::InputTextEx ( "###WhitelistPatterns", "SteamApps\nEpic Games\\\\\nGOG Galaxy\\\\Games\nOrigin Games\\\\",
                              _inject.whitelist, MAX_PATH * 128 - 1,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                        150 * SKIF_ImGui_GlobalDPIScale ), // 120 // 150
                                  ImGuiInputTextFlags_Multiline );

    if (*_inject.whitelist == '\0')
    {
      SKIF_ImGui_SetHoverTip (
        "These are the patterns used internally to enable Special K for these specific platforms."
        "\nThey are presented here solely as examples of how a potential pattern might look like."
      );
    }

    _CheckWarnings (_inject.whitelist);

    ImGui::EndGroup   ();

    ImGui::SameLine   ();

    ImGui::BeginGroup ();

    ImGui::TextColored (ImColor(255, 207, 72), ICON_FA_FOLDER_PLUS);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Add Common Patterns:"
    );

    SKIF_ImGui_Spacing ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    ("Click on an item below to add it to the whitelist, or hover over it "
                            "to display more information about what the pattern covers.");
    ImGui::PopStyleColor  ();

    SKIF_ImGui_Spacing ();
    SKIF_ImGui_Spacing ();

    ImGui::SameLine    ();
    ImGui::BeginGroup  ();
    ImGui::TextColored ((SKIF_iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_WINDOWS);
    //ImGui::TextColored ((SKIF_iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_XBOX);
    ImGui::EndGroup    ();

    ImGui::SameLine    ();

    ImGui::BeginGroup  ();
    if (ImGui::Selectable ("Games"))
    {
      white_edited = true;

      _inject._AddUserList("Games", true);
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on most platforms, such as Uplay, as"
      "\nmost of them have 'games' in the full path somewhere."
    );

    /*
    if (ImGui::Selectable ("WindowsApps"))
    {
      white_edited = true;

      _inject._AddUserList("WindowsApps", true);
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on the Microsoft Store or Game Pass."
    );
    */

    ImGui::EndGroup ();
    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Blacklist section

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_MINUS_CIRCLE);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Blacklist Patterns:"
    );
    ImGui::SameLine    ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "(Does not prevent injection! Use this to exclude stuff from being whitelisted)");

    SKIF_ImGui_Spacing ();

    black_edited |=
      ImGui::InputTextEx ( "###BlacklistPatterns", "launcher.exe",
                              _inject.blacklist, MAX_PATH * 128 - 1,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                          120 * SKIF_ImGui_GlobalDPIScale ),
                                  ImGuiInputTextFlags_Multiline );

    _CheckWarnings (_inject.blacklist);

    ImGui::Separator ();

    bool bDisabled =
      (white_edited || black_edited) ?
                                false : true;

    if (bDisabled)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    // Hotkey: Ctrl+S
    if (ImGui::Button (ICON_FA_SAVE " Save Changes") || ((! bDisabled) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeysDown['S']))
    {
      // Clear the active ID to prevent ImGui from holding outdated copies of the variable
      //   if saving succeeds, to allow _StoreList to update the variable successfully
      ImGui::ClearActiveID();

      if (white_edited)
      {
        white_stored = _inject._StoreList(true);

        if (white_stored)
          white_edited = false;
      }

      if (black_edited)
      {
        black_stored = _inject._StoreList (false);

        if (black_stored)
          black_edited = false;
      }
    }

    ImGui::SameLine ();

    if (ImGui::Button (ICON_FA_UNDO " Reset"))
    {
      if (white_edited)
      {
        _inject._LoadList (true);

        white_edited = false;
        white_stored = true;
      }

      if (black_edited)
      {
        _inject._LoadList(false);

        black_edited = false;
        black_stored = true;
      }
    }

    if (bDisabled)
    {
      ImGui::PopItemFlag  ( );
      ImGui::PopStyleVar  ( );
    }

    ImGui::Spacing();

    if (! white_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"• ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The whitelist could not be saved! Please remove any non-Latin characters and try again.");
    }

    if (! black_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"• ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The blacklist could not be saved! Please remove any non-Latin characters and try again.");
    }

    ImGui::EndGroup       ( );
  }
}

#include <utility/skif_imgui.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <filesystem>
#include <functional>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <utility/updater.h>
#include <utility/gamepad.h>

extern bool allowShortcutCtrlA;

struct Monitor_MPO_Support
{
  std::string                    Name;  // EDID names are limited to 13 characters, which is perfect for us
  UINT                           Index; // cloneGroupId -- doesn't really correspond to anything important for us...
  std::string                    DeviceNameGdi;
  std::string                    DevicePath;
  UINT                           MaxPlanes;
  UINT                           MaxRGBPlanes;
  UINT                           MaxYUVPlanes;
  float                          MaxStretchFactor;
  float                          MaxShrinkFactor;
  D3DKMT_MULTIPLANE_OVERLAY_CAPS OverlayCaps;
  std::string                    OverlayCapsAsString;
  bool                           Supported = false; // Pure assumption by us based on various discoveries/experiences
};

enum DrvInstallState {
  NotInstalled,
  Installed,
  OtherDriverInstalled,
  ObsoleteInstalled
};

bool                              MPORegistryDisabled = false;
std::vector <Monitor_MPO_Support> Monitors;
DrvInstallState driverStatus        = NotInstalled,
                driverStatusPending = NotInstalled;

// Check the MPO capabilities of the system
bool
GetMPOSupport (void)
{
  // D3DKMTGetMultiPlaneOverlayCaps (Windows 10+)
  using D3DKMTGetMultiPlaneOverlayCaps_pfn =
    NTSTATUS (WINAPI *)(D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS*);

  static D3DKMTGetMultiPlaneOverlayCaps_pfn
    SKIF_D3DKMTGetMultiPlaneOverlayCaps =
        (D3DKMTGetMultiPlaneOverlayCaps_pfn)GetProcAddress (LoadLibraryEx (L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "D3DKMTGetMultiPlaneOverlayCaps");

  // D3DKMTOpenAdapterFromLuid (Windows 8+)
  using D3DKMTOpenAdapterFromLuid_pfn =
    NTSTATUS (WINAPI *)(D3DKMT_OPENADAPTERFROMLUID*);

  static D3DKMTOpenAdapterFromLuid_pfn
    SKIF_D3DKMTOpenAdapterFromLuid =
        (D3DKMTOpenAdapterFromLuid_pfn)GetProcAddress (LoadLibraryEx (L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "D3DKMTOpenAdapterFromLuid");

  if (SKIF_D3DKMTOpenAdapterFromLuid      == nullptr ||
      SKIF_D3DKMTGetMultiPlaneOverlayCaps == nullptr)
    return false;

  std::vector<DISPLAYCONFIG_PATH_INFO> pathArray;
  std::vector<DISPLAYCONFIG_MODE_INFO> modeArray;
  LONG result = ERROR_SUCCESS;

  Monitors.clear();

  do
  {
    // Determine how many path and mode structures to allocate
    UINT32 pathCount, modeCount;
    result = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "GetDisplayConfigBufferSizes failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // Allocate the path and mode arrays
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // Get all active paths and their modes
    result = QueryDisplayConfig ( QDC_ONLY_ACTIVE_PATHS, &pathCount, pathArray.data(),
                                                         &modeCount, modeArray.data(), nullptr);

    // The function may have returned fewer paths/modes than estimated
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
    // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS)
  {
    PLOG_ERROR << "QueryDisplayConfig failed: " << SKIF_Util_GetErrorAsWStr (result);
    return false;
  }

  // For each active path
  for (auto& path : pathArray)
  {
    // Find the target (monitor) friendly name
    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
    targetName.header.adapterId            = path.targetInfo.adapterId;
    targetName.header.id                   = path.targetInfo.id;
    targetName.header.type                 = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    targetName.header.size                 = sizeof (targetName);

    result = DisplayConfigGetDeviceInfo (&targetName.header);

    std::wstring monitorName = (targetName.flags.friendlyNameFromEdid ? targetName.monitorFriendlyDeviceName
                                                                      : L"Unknown");

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // Find the adapter device name
    DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
    adapterName.header.adapterId           = path.targetInfo.adapterId;
    adapterName.header.type                = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
    adapterName.header.size                = sizeof (adapterName);

    result = DisplayConfigGetDeviceInfo (&adapterName.header);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // Find the source device name
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
    sourceName.header.adapterId            = path.sourceInfo.adapterId;
    sourceName.header.id                   = path.sourceInfo.id;
    sourceName.header.type                 = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sourceName.header.size                 = sizeof (sourceName);

    result = DisplayConfigGetDeviceInfo (&sourceName.header);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    //PLOG_VERBOSE << "Target: "       << path.targetInfo.id;

    // Open a handle to the adapter using its LUID
    D3DKMT_OPENADAPTERFROMLUID openAdapter = {};
    openAdapter.AdapterLuid = adapterName.header.adapterId;
    if (SKIF_D3DKMTOpenAdapterFromLuid (&openAdapter) == (NTSTATUS)0x00000000L) // STATUS_SUCCESS
    {
      D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS caps = {};
      caps.hAdapter      = openAdapter.hAdapter;
      caps.VidPnSourceId = path.sourceInfo.id;

      if (SKIF_D3DKMTGetMultiPlaneOverlayCaps (&caps) == (NTSTATUS)0x00000000L) // STATUS_SUCCESS
      {
        PLOG_DEBUG << "MPO support detected for this display:"
                   << "\n+------------------+-------------------------------------+"
                   << "\n| Monitor Name     | " << monitorName
                   << "\n| Adapter Path     | " << adapterName.adapterDevicePath
                   << "\n| Source Name      | " <<  sourceName.viewGdiDeviceName
                   << "\n+------------------+-------------------------------------+"
                   << "\n| MPO MaxPlanes    | " << caps.MaxPlanes
                   << "\n| MPO MaxRGBPlanes | " << caps.MaxRGBPlanes // MaxRGBPlanes seems to be the number that best corresponds to dxdiag's reporting? Or is it?
                   << "\n| MPO MaxYUVPlanes:| " << caps.MaxYUVPlanes
                   << "\n| MPO Stretch:     | " << caps.MaxStretchFactor << "x - " << caps.MaxShrinkFactor << "x"
                   << "\n+------------------+-------------------------------------+";
          
        Monitor_MPO_Support monitor;
        monitor.Name                = SK_WideCharToUTF8 (monitorName);
        monitor.Index               = path.sourceInfo.cloneGroupId;
        monitor.DevicePath          = SK_WideCharToUTF8 (adapterName.adapterDevicePath);
        monitor.DeviceNameGdi       = SK_WideCharToUTF8 (sourceName.viewGdiDeviceName);
        monitor.MaxPlanes           = caps.MaxPlanes;
        monitor.MaxRGBPlanes        = caps.MaxRGBPlanes;
        monitor.MaxYUVPlanes        = caps.MaxYUVPlanes;
        monitor.MaxStretchFactor    = caps.MaxStretchFactor;
        monitor.MaxShrinkFactor     = caps.MaxShrinkFactor;
        monitor.OverlayCaps         = caps.OverlayCaps;
        monitor.OverlayCapsAsString = "";

        // This is pure assumption from us based on discoveries/experiences and this line in the MSFT docs:
        // "At least one plane must support shrinking and stretching, independent from other planes that might be enabled."
        if (monitor.MaxStretchFactor  !=  monitor.MaxShrinkFactor  &&
            monitor.MaxPlanes  > 1    && (monitor.MaxRGBPlanes > 1 || monitor.MaxYUVPlanes > 1))
          monitor.Supported = true;

        /*
              UINT Rotation                        : 1;    // Full rotation
              UINT RotationWithoutIndependentFlip  : 1;    // Rotation, but without simultaneous IndependentFlip support
              UINT VerticalFlip                    : 1;    // Can flip the data vertically
              UINT HorizontalFlip                  : 1;    // Can flip the data horizontally
              UINT StretchRGB                      : 1;    // Supports stretching RGB formats
              UINT StretchYUV                      : 1;    // Supports stretching YUV formats
              UINT BilinearFilter                  : 1;    // Bilinear filtering
              UINT HighFilter                      : 1;    // Better than bilinear filtering
              UINT Shared                          : 1;    // MPO resources are shared across VidPnSources
              UINT Immediate                       : 1;    // Immediate flip support
              UINT Plane0ForVirtualModeOnly        : 1;    // Stretching plane 0 will also stretch the HW cursor and should only be used for virtual mode support
              UINT Version3DDISupport              : 1;    // Driver supports the 2.2 MPO DDIs
        */

        // "RGB" and "YUV" capabilities seems inferred from the MaxRGBPlanes and MaxYUVPlanes variables
        // The uppercase titles is how the capability seems to be reported through dxdiag.exe / dxdiagn.dll (educated guess)

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ne-dxgiddi-dxgi_ddi_multiplane_overlay_feature_caps

        if (monitor.MaxRGBPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports " + std::to_string(monitor.MaxRGBPlanes) + " planes containing RGB data. [RGB]\n";

        if (monitor.MaxYUVPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports " + std::to_string(monitor.MaxYUVPlanes) + " planes containing YUV data. [YUV]\n";

        if (monitor.OverlayCaps.Rotation)
          monitor.OverlayCapsAsString += "Supports full rotation of the MPO plane with Independent Flip. [ROTATION]\n";

        if (monitor.OverlayCaps.RotationWithoutIndependentFlip)
          monitor.OverlayCapsAsString += "Supports full rotation of the MPO plane, but without Independent Flip. [ROTATION_WITHOUT_INDEPENDENT_FLIP]\n";

        if (monitor.OverlayCaps.VerticalFlip)
          monitor.OverlayCapsAsString += "Supports flipping the data vertically. [VERTICAL_FLIP]\n";

        if (monitor.OverlayCaps.HorizontalFlip)
          monitor.OverlayCapsAsString += "Supports flipping the data horizontally. [HORIZONTAL_FLIP]\n";

        if (monitor.OverlayCaps.StretchRGB && monitor.MaxRGBPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports stretching any plane containing RGB data. [STRETCH_RGB]\n";

        if (monitor.OverlayCaps.StretchYUV && monitor.MaxYUVPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports stretching any plane containing YUV data. [STRETCH_YUV]\n";

        if (monitor.OverlayCaps.BilinearFilter)
          monitor.OverlayCapsAsString += "Supports bilinear filtering. [BILINEAR]\n";

        if (monitor.OverlayCaps.HighFilter)
          monitor.OverlayCapsAsString += "Supports better than bilinear filtering. [HIGH_FILTER]\n";

        if (monitor.OverlayCaps.Shared)
          monitor.OverlayCapsAsString += "MPO resources are shared across video present network (VidPN) sources. [SHARED]\n";

        if (monitor.OverlayCaps.Immediate)
          monitor.OverlayCapsAsString += "Supports immediate flips (allows tearing) of the MPO plane. [IMMEDIATE]\n";
        // When TRUE, the HW supports immediate flips of the MPO plane.
        // If the flip contains changes that cannot be performed as an immediate flip,
        //  the driver can promote the flip to a VSYNC flip using the new HSync completion infrastructure.

        if (monitor.OverlayCaps.Plane0ForVirtualModeOnly)
          monitor.OverlayCapsAsString += "Will always apply the stretch factor of plane 0 to the hardware cursor as well as the plane. [PLANE0_FOR_VIRTUAL_MODE_ONLY]\n";
        // When TRUE, the hardware will always apply the stretch factor of plane 0 to the hardware cursor as well as the plane.
        //  This implies that stretching/shrinking of plane 0 should only occur when plane 0 is the desktop plane and when the
        //   stretching/shrinking is used for virtual mode support.

        // Monitor supports the new DDIs, though we have no idea how to further query that kind of support yet...
        //if (monitor.OverlayCaps.Version3DDISupport)
        //  monitor.OverlayCapsAsString += "Driver supports the WDDM 2.2 MPO (multi-plane overlay) DDIs.";

        Monitors.emplace_back (monitor);
      }
    }
  }

  return true;
}

// Check if the SK_WinRing0 driver service is installed or not
std::wstring
GetDrvInstallState (DrvInstallState& ptrStatus, std::wstring svcName = L"SK_WinRing0")
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring       binaryPath = L"";
  SC_HANDLE        schSCManager = NULL,
                    svcWinRing0 = NULL;
  LPQUERY_SERVICE_CONFIG   lpsc = {  };
  DWORD                    dwBytesNeeded,
                            cbBufSize {};

  // Reset the current status to not installed.
  ptrStatus = NotInstalled;

  // Retrieve the install folder.
  static std::wstring dirNameInstall  = std::filesystem::path (_path_cache.specialk_install ).filename();
  static std::wstring dirNameUserdata = std::filesystem::path (_path_cache.specialk_userdata).filename();

  // Need a try-catch block as this can apparently crash due to some currently unknown reason
  try {
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
          svcName.c_str(),     // name of service - Old: WinRing0_1_2_0, New: SK_WinRing0
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
          if (ERROR_INSUFFICIENT_BUFFER == GetLastError ( ))
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

              PLOG_VERBOSE << "Driver " << svcName << " supposedly installed at : " << binaryPath;

              if (svcName == L"SK_WinRing0" &&
                  PathFileExists (binaryPath.c_str()))
              {
                ptrStatus = Installed; // File exists, so driver is installed
                PLOG_INFO << "Found driver " << svcName << " installed at : " << binaryPath;
              }

              // Method used to detect the old copy
              else {
                PLOG_VERBOSE << "dirNameInstall:  " << dirNameInstall;
                PLOG_VERBOSE << "dirNameUserdata: " << dirNameUserdata;

                // Check if the installed driver exists, and it's in SK's folder
                if (PathFileExists      (binaryPath.c_str()) &&
                  (std::wstring::npos != binaryPath.find (dirNameInstall ) ||
                   std::wstring::npos != binaryPath.find (dirNameUserdata)))
                {
                  ptrStatus = ObsoleteInstalled; // File exists, so obsolete driver is installed
                  PLOG_INFO << "Found obsolete driver " << svcName << " installed at : " << binaryPath;
                }
              }
            }
            else {
              PLOG_ERROR << "QueryServiceConfig failed with exception: " << SKIF_Util_GetErrorAsWStr ( );
            }

            LocalFree (lpsc);
          }
          else {
            PLOG_WARNING << "Unexpected behaviour occurred: " << SKIF_Util_GetErrorAsWStr ( );
          }
        }
        else {
          PLOG_WARNING << "Unexpected behaviour occurred: " << SKIF_Util_GetErrorAsWStr();
        }

        CloseServiceHandle (svcWinRing0);
      }
      else if (ERROR_SERVICE_DOES_NOT_EXIST == GetLastError ( ))
      {
        //PLOG_INFO << "SK_WinRing0 has not been installed.";

        static bool checkObsoleteOnce = true;
        // Check if WinRing0_1_2_0 have been installed, but only on the very first check
        if (checkObsoleteOnce && svcName == L"SK_WinRing0")
        {
          checkObsoleteOnce = false;
          GetDrvInstallState (ptrStatus, L"WinRing0_1_2_0");
        }
      }
      else {
        PLOG_ERROR << "OpenService failed with exception: " << SKIF_Util_GetErrorAsWStr ( );
      }

      CloseServiceHandle (schSCManager);
    }
    else {
      PLOG_ERROR << "OpenSCManager failed with exception: " << SKIF_Util_GetErrorAsWStr ( );
    }
  }

  catch (const std::exception&)
  {
    PLOG_ERROR << "Unexpected exception was thrown!";
  }

  return binaryPath;
};

void
SKIF_UI_Tab_DrawSettings (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static std::wstring
            driverBinaryPath    = L"",
            SKIFdrvFolder = (_registry.wsSKIFdrvLocation.length() > 0)
                           ? _registry.wsSKIFdrvLocation
                           : SK_FormatStringW (LR"(%ws\Drivers\WinRing0\)", _path_cache.specialk_install), // fallback
            SKIFdrv       = SKIFdrvFolder + L"\\SKIFdrv.exe",
            SYSdrv        = SKIFdrvFolder + L"\\WinRing0x64.sys";
  
  static SKIF_DirectoryWatch SKIF_DriverWatch;
  static bool HDRSupported = false;

  static DWORD dwTriggerNewRefresh = 0; // Set when to trigger a new refresh (0 = DISABLED)
  static const std::wstring valvePlugPath = SK_FormatStringW (LR"(%ws\XInput1_4.dll)", _path_cache.steam_install);
  static       bool         valvePlug     = false;

  float columnWidth = 0.5f * ImGui::GetContentRegionAvail().x; // Needs to be before the SKIF_ImGui_Columns() call
  bool enableColums = (ImGui::GetContentRegionAvail().x / SKIF_ImGui_GlobalDPIScale >= 750.f);

  // Driver is supposedly getting a new state -- check if its time for an
  //  update on each frame until driverStatus matches driverStatusPending
  if (driverStatusPending != driverStatus)
  {
    static DWORD dwLastDrvRefresh = 0;

    // Refresh once every 500 ms
    if (dwLastDrvRefresh < SKIF_Util_timeGetTime() && (!ImGui::IsAnyMouseDown() || !SKIF_ImGui_IsFocused()))
    {
      dwLastDrvRefresh = SKIF_Util_timeGetTime() + 500;
      driverBinaryPath = GetDrvInstallState (driverStatus);
    }
  }

  // Refresh things when visiting from another tab or when forced
  if (SKIF_Tab_Selected != UITab_Settings                         ||
      RefreshSettingsTab                                          ||
      SKIF_DriverWatch.isSignaled (SKIFdrvFolder, UITab_Settings) ||
      dwTriggerNewRefresh != 0 && dwTriggerNewRefresh < SKIF_Util_timeGetTime ( )    )
  {
    GetMPOSupport         (    );
    SKIF_Util_IsMPOsDisabledInRegistry (true);
    SKIF_Util_IsHDRSupported (true);
    SKIF_Util_IsHDRActive    (true);
    driverBinaryPath    = GetDrvInstallState (driverStatus);
    driverStatusPending =                     driverStatus;
    RefreshSettingsTab  = false;
    dwTriggerNewRefresh = 0;
    valvePlug = (SKIF_Util_GetProductName (valvePlugPath.c_str()).find(L"Valve Plug") != std::wstring::npos);

    if (valvePlug && _registry.regKVValvePlug.hasData())
      _registry.iValvePlug = _registry.regKVValvePlug.getData();
  }

  SKIF_Tab_Selected = UITab_Settings;
  if (SKIF_Tab_ChangeTo == UITab_Settings)
      SKIF_Tab_ChangeTo  = UITab_None;

#pragma region Section: Top / General
  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  SKIF_ImGui_Spacing      ( );

  if (enableColums)
  {
    SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_GENERAL", true);
    ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
  }
  else {
    // This is needed to reproduce the same padding on the left side as when using columns
    ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
    ImGui::BeginGroup    ( );
  }

  if ( ImGui::Checkbox ( "Low bandwidth mode",                          &_registry.bLowBandwidthMode ) )
    _registry.regKVLowBandwidthMode.putData (                            _registry.bLowBandwidthMode );
          
  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (
    "For new games/covers, low resolution images will be preferred over high-resolution ones.\n"
    "This only affects new downloads of covers. It does not affect already downloaded covers.\n"
    "This will also disable automatic downloads of new updates to Special K."
  );
            
  if ( ImGui::Checkbox ( "Minimize when launching a game",            &_registry.bMinimizeOnGameLaunch ) )
    _registry.regKVMinimizeOnGameLaunch.putData (                      _registry.bMinimizeOnGameLaunch );
            
  if ( ImGui::Checkbox ( "Restore after closing a game",              &_registry.bRestoreOnGameExit ) )
    _registry.regKVRestoreOnGameExit.putData    (                      _registry.bRestoreOnGameExit );

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("Requires Special K injected to work as intended.");
  
  if (_registry.bAllowMultipleInstances)
  {
    ImGui::BeginGroup   ( );
    SKIF_ImGui_PushDisableState ( );
  }

  if ( ImGui::Checkbox ( "Close to the notification area", &_registry.bCloseToTray ) )
    _registry.regKVCloseToTray.putData (                    _registry.bCloseToTray );

  if (_registry.bAllowMultipleInstances)
  {
    SKIF_ImGui_PopDisableState  ( );
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip ("Requires 'Allow multiple instances of this app' to be disabled.");
  }

  _inject._StartAtLogonCtrl ( );


  if ( ImGui::Checkbox ( "Controller support", &_registry.bControllers ) )
  {                                            
    _registry.regKVControllers.putData (        _registry.bControllers ? 1 : 0);

    // Ensure the gamepad input thread knows what state we are actually in
    static SKIF_GamePadInputHelper& _gamepad =
           SKIF_GamePadInputHelper::GetInstance ( );

    if (_registry.bControllers)
    {
      _gamepad.InvalidateGamePads ( );
      _gamepad.WakeThread  ( );
    } else
      _gamepad.SleepThread ( );
  }

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("Allows the UI to be controlled using " ICON_FA_XBOX " or " ICON_FA_PLAYSTATION " controllers, and adds special features while SKIF is running.");

  ImGui::SameLine        ( );

  if (! _registry.bControllers)
    SKIF_ImGui_PushDisableState ();

  if ( ImGui::ButtonEx ( ICON_FA_GAMEPAD "  Config", ImVec2 ( 115 * SKIF_ImGui_GlobalDPIScale,
                                                               25 * SKIF_ImGui_GlobalDPIScale ) ) )
    ImGui::OpenPopup ("Special K Input Config###SKInput_GamepadCfg");

  if (_registry.bControllers)
  {
    if (ImGui::BeginPopup ("Special K Input Config###SKInput_GamepadCfg",
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::SeparatorText ("General Controller Settings");
      ImGui::TreePush      ("");
      bool bEnableScreenSaverChord = _registry.skinput.dwScreenSaverChordBehavior != 0 &&
                                     _registry.skinput.dwScreenSaverChord         != 0;
      if (ImGui::Checkbox  ("Activate Screen Saver Using  " ICON_FA_XBOX "/" ICON_FA_PLAYSTATION "  + ", &bEnableScreenSaverChord))
      {
        if (bEnableScreenSaverChord)
        {
          //if (_registry.skinput.dwScreenSaverChordBehavior == 0)
          //    _registry.skinput.dwScreenSaverChordBehavior = 1;

          if (_registry.skinput.dwScreenSaverChord == 0) {
              _registry.skinput.dwScreenSaverChord = XINPUT_GAMEPAD_A;
              _registry.regKVControllerScreenSaverChord.putData (_registry.skinput.dwScreenSaverChord);
          }
        }

        else
        {
          _registry.skinput.dwScreenSaverChord = 0;
          _registry.regKVControllerScreenSaverChord.putData (_registry.skinput.dwScreenSaverChord);
          //_registry.skinput.dwScreenSaverChordBehavior = 0;
        }
      }

      if (bEnableScreenSaverChord)
      {
        SKIF_ImGui_SetHoverTip ("The controller binding is configurable by clicking the text after " ICON_FA_XBOX " / " ICON_FA_PLAYSTATION);

        ImGui::SameLine ();

        static bool         selected = false;
        static DWORD dwButtonPressed = 0;

        if (! selected)
        {
          dwButtonPressed = 0;
        }

        ImGui::PushID ("ScreenSaverBinding");
        switch (_registry.skinput.dwScreenSaverChord)
        {
          case XINPUT_GAMEPAD_LEFT_SHOULDER:  selected = ImGui::Selectable ("LB / L1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_TRIGGER:   selected = ImGui::Selectable ("LT / L2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_THUMB:     selected = ImGui::Selectable ("LS / L3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_SHOULDER: selected = ImGui::Selectable ("RB / R1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_TRIGGER:  selected = ImGui::Selectable ("RT / R2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_THUMB:    selected = ImGui::Selectable ("RS / R3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x1:                           selected = ImGui::Selectable ("A / Cross",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x3/*
               XINPUT_GAMEPAD_DPAD_UP*/:      selected = ImGui::Selectable ("Up",            true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_DOWN:      selected = ImGui::Selectable ("Down",          true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_LEFT:      selected = ImGui::Selectable ("Left",          true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_RIGHT:     selected = ImGui::Selectable ("Right",         true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_START:          selected = ImGui::Selectable ("Start",         true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_BACK:           selected = ImGui::Selectable ("Back / Select", true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_Y:              selected = ImGui::Selectable ("Y / Triangle",  true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_A:              selected = ImGui::Selectable ("A / Cross",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_X:              selected = ImGui::Selectable ("X / Square",    true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_B:              selected = ImGui::Selectable ("B / Circle",    true, ImGuiSelectableFlags_DontClosePopups); break;
        }
        ImGui::PopID ();

        static bool open = false;
        if (selected)
        {
          ImGui::OpenPopup ("ChordBinding_ScreenSaver");
          open = true;
        }

        if (ImGui::BeginPopupModal ("ChordBinding_ScreenSaver", &open, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
        {
          selected = false;

               if (ImGui::IsKeyPressed (ImGuiKey_GamepadL1))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_SHOULDER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL2))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_TRIGGER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL3))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_THUMB;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR1))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_SHOULDER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR2))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_TRIGGER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR3))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_THUMB;

          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadUp))     dwButtonPressed = XINPUT_GAMEPAD_DPAD_UP;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadDown))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_DOWN;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadLeft))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_LEFT;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadRight))  dwButtonPressed = XINPUT_GAMEPAD_DPAD_RIGHT;

          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadStart))      dwButtonPressed = XINPUT_GAMEPAD_START;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadBack))       dwButtonPressed = XINPUT_GAMEPAD_BACK;

          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceUp))     dwButtonPressed = XINPUT_GAMEPAD_Y;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceDown))   dwButtonPressed = XINPUT_GAMEPAD_A;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceLeft))   dwButtonPressed = XINPUT_GAMEPAD_X;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceRight))  dwButtonPressed = XINPUT_GAMEPAD_B;

          switch (dwButtonPressed)
          {
            case XINPUT_GAMEPAD_LEFT_SHOULDER:  _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_LEFT_TRIGGER:   _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_LEFT_THUMB:     _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_RIGHT_SHOULDER: _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_RIGHT_TRIGGER:  _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_RIGHT_THUMB:    _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_DPAD_UP:        _registry.skinput.dwScreenSaverChord = 0x3;             break;
            case XINPUT_GAMEPAD_DPAD_DOWN:      _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_DPAD_LEFT:      _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_DPAD_RIGHT:     _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;

            case XINPUT_GAMEPAD_START:          _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_BACK:           _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;

            case XINPUT_GAMEPAD_Y:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_A:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_X:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_B:              _registry.skinput.dwScreenSaverChord = dwButtonPressed; break;
            default:
              selected = true;
              break;
          }

          if (! selected)
          {
            _registry.regKVControllerScreenSaverChord.putData (_registry.skinput.dwScreenSaverChord);

            ImGui::CloseCurrentPopup ();
            open = false;
          }

          ImGui::EndPopup ();
        }
      }

      bool                                                            bGamepadsDeactivateScreenSaver =
              _registry.skinput.dwGamepadsDeactivateScreenSaver != 0;
      if (ImGui::Checkbox ("Gamepad Input Deactivates Screen Saver", &bGamepadsDeactivateScreenSaver))
      {
        _registry.regKVControllerGamepadsDeactivateScreenSaver.putData (
          _registry.skinput.dwGamepadsDeactivateScreenSaver =
                             bGamepadsDeactivateScreenSaver ? 1 : 0
        );
      }

      if (! bGamepadsDeactivateScreenSaver)
        ImGui::BulletText  ("Pressing " ICON_FA_XBOX " on Xbox controllers always deactivates.");

      ImGui::TreePop       (  );
      ImGui::Spacing       (  );
      ImGui::Spacing       (  );
      ImGui::SeparatorText ("PlayStation Power Management");
      ImGui::TreePush      ("");

      bool bEnablePowerOffChord = _registry.skinput.dwPowerOffChordBehavior != 0 &&
                                  _registry.skinput.dwPowerOffChord         != 0;
      if (ImGui::Checkbox  ("Power Off Controllers Using  " ICON_FA_XBOX "/" ICON_FA_PLAYSTATION "  + ", &bEnablePowerOffChord))
      {
        if (bEnablePowerOffChord)
        {
          //if (_registry.skinput.dwScreenSaverChordBehavior == 0)
          //    _registry.skinput.dwScreenSaverChordBehavior = 1;

          if (_registry.skinput.dwPowerOffChord == 0) {
              _registry.skinput.dwPowerOffChord = XINPUT_GAMEPAD_Y;
              _registry.regKVControllerPowerOffChord.putData (_registry.skinput.dwPowerOffChord);
          }
        }

        else
        {
          _registry.skinput.dwPowerOffChord = 0;
          _registry.regKVControllerPowerOffChord.putData (_registry.skinput.dwPowerOffChord);
          //_registry.skinput.dwScreenSaverChordBehavior = 0;
        }
      }

      if (bEnablePowerOffChord)
      {
        SKIF_ImGui_SetHoverTip ("The controller binding is configurable by clicking the text after " ICON_FA_XBOX " / " ICON_FA_PLAYSTATION);

        ImGui::SameLine ();

        static bool         selected = false;
        static DWORD dwButtonPressed = 0;

        if (! selected)
        {
          dwButtonPressed = 0;
        }

        ImGui::PushID ("PowerOffBinding");
        switch (_registry.skinput.dwPowerOffChord)
        {
          case XINPUT_GAMEPAD_LEFT_SHOULDER:  selected = ImGui::Selectable ("LB / L1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_TRIGGER:   selected = ImGui::Selectable ("LT / L2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_LEFT_THUMB:     selected = ImGui::Selectable ("LS / L3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_SHOULDER: selected = ImGui::Selectable ("RB / R1",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_TRIGGER:  selected = ImGui::Selectable ("RT / R2",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_RIGHT_THUMB:    selected = ImGui::Selectable ("RS / R3",       true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x1:                           selected = ImGui::Selectable ("Y / Triangle",  true, ImGuiSelectableFlags_DontClosePopups); break;
          case 0x3/*
               XINPUT_GAMEPAD_DPAD_UP*/:      selected = ImGui::Selectable ("Up",            true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_DOWN:      selected = ImGui::Selectable ("Down",          true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_LEFT:      selected = ImGui::Selectable ("Left",          true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_DPAD_RIGHT:     selected = ImGui::Selectable ("Right",         true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_START:          selected = ImGui::Selectable ("Start",         true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_BACK:           selected = ImGui::Selectable ("Back / Select", true, ImGuiSelectableFlags_DontClosePopups); break;

          case XINPUT_GAMEPAD_Y:              selected = ImGui::Selectable ("Y / Triangle",  true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_A:              selected = ImGui::Selectable ("A / Cross",     true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_X:              selected = ImGui::Selectable ("X / Square",    true, ImGuiSelectableFlags_DontClosePopups); break;
          case XINPUT_GAMEPAD_B:              selected = ImGui::Selectable ("B / Circle",    true, ImGuiSelectableFlags_DontClosePopups); break;
        }
        ImGui::PopID ();

        static bool open = false;
        if (selected)
        {
          ImGui::OpenPopup ("ChordBinding_PowerOff");
          open = true;
        }

        if (ImGui::BeginPopupModal ("ChordBinding_PowerOff", &open, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
        {
          selected = false;

               if (ImGui::IsKeyPressed (ImGuiKey_GamepadL1))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_SHOULDER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL2))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_TRIGGER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadL3))         dwButtonPressed = XINPUT_GAMEPAD_LEFT_THUMB;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR1))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_SHOULDER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR2))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_TRIGGER;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadR3))         dwButtonPressed = XINPUT_GAMEPAD_RIGHT_THUMB;

          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadUp))     dwButtonPressed = XINPUT_GAMEPAD_DPAD_UP;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadDown))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_DOWN;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadLeft))   dwButtonPressed = XINPUT_GAMEPAD_DPAD_LEFT;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadDpadRight))  dwButtonPressed = XINPUT_GAMEPAD_DPAD_RIGHT;

          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadStart))      dwButtonPressed = XINPUT_GAMEPAD_START;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadBack))       dwButtonPressed = XINPUT_GAMEPAD_BACK;

          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceUp))     dwButtonPressed = XINPUT_GAMEPAD_Y;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceDown))   dwButtonPressed = XINPUT_GAMEPAD_A;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceLeft))   dwButtonPressed = XINPUT_GAMEPAD_X;
          else if (ImGui::IsKeyPressed (ImGuiKey_GamepadFaceRight))  dwButtonPressed = XINPUT_GAMEPAD_B;

          switch (dwButtonPressed)
          {
            case XINPUT_GAMEPAD_LEFT_SHOULDER:  _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_LEFT_TRIGGER:   _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_LEFT_THUMB:     _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_RIGHT_SHOULDER: _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_RIGHT_TRIGGER:  _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_RIGHT_THUMB:    _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_DPAD_UP:        _registry.skinput.dwPowerOffChord = 0x3;             break;
            case XINPUT_GAMEPAD_DPAD_DOWN:      _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_DPAD_LEFT:      _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_DPAD_RIGHT:     _registry.skinput.dwPowerOffChord = dwButtonPressed; break;

            case XINPUT_GAMEPAD_START:          _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_BACK:           _registry.skinput.dwPowerOffChord = dwButtonPressed; break;

            case XINPUT_GAMEPAD_Y:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_A:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_X:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            case XINPUT_GAMEPAD_B:              _registry.skinput.dwPowerOffChord = dwButtonPressed; break;
            default:
              selected = true;
              break;
          }

          if (! selected)
          {
            _registry.regKVControllerPowerOffChord.putData (_registry.skinput.dwPowerOffChord);

            ImGui::CloseCurrentPopup ();
            open = false;
          }

          ImGui::EndPopup ();
        }
      }

      float fIdleMinutes =
        static_cast <float> (_registry.skinput.dwIdleTimeoutInSecs) / 60.0f;

      float fLastIdle = fIdleMinutes;

      if (ImGui::SliderFloat ("Idle Behavior", &fIdleMinutes, 0.0f, 30.0f,
                                                fIdleMinutes < 0.5f ? "Never Power Off" :
                                                                            "Power Off After %.1f Minutes"))
      {
        // Minimum positive step value to allow gamepad to move this slider off of "Never Power Off"
        if (fLastIdle == 0.0f && fIdleMinutes > 0.0f)
            fIdleMinutes = 0.5f;

        _registry.skinput.dwIdleTimeoutInSecs =
          fIdleMinutes < 0.5f ? 0 : static_cast <DWORD> (60.0f * fIdleMinutes);

        _registry.regKVControllerIdlePowerOffTimeOut.putData (_registry.skinput.dwIdleTimeoutInSecs);
      }

      SKIF_ImGui_SetHoverTip ("This only applies when SKIF or SKIV are running and no game is actively using Special K.");

      ImGui::TreePop         (  );

      ImGui::Spacing         (  );
      ImGui::Spacing         (  );
      ImGui::SeparatorText   ("Third-Party Setup\tXInput Assignment: ");

      auto&                     _gamepad = SKIF_GamePadInputHelper::GetInstance ();
      std::vector<bool> slots = _gamepad.GetGamePads ( );

      ImGui::SameLine        (  );
      ImGui::BeginGroup      (  );
      for (auto slot : slots){
        ImGui::SameLine      (  );
        if (slot)
          ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), ICON_FA_CHECK);
        else
          ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Yellow ), ICON_FA_XMARK); }
      ImGui::EndGroup        (  );

      ImGui::TreePush        ("");
      ImGui::BeginGroup      (  );
      if (valvePlug)
      {
        static bool valvePlugState = (bool)_registry.iValvePlug;
        extern HANDLE SteamProcessHandle;

        bool disable = (SteamProcessHandle != NULL);

        if (disable)
          SKIF_ImGui_PushDisableState ( );

        if ( ImGui::Checkbox ( "Disable " ICON_FA_STEAM " Input (will restart Steam)", &valvePlugState) )
        {
          _registry.regKVValvePlug.putData (              static_cast<int> (valvePlugState));

          PROCESSENTRY32W pe32 = SKIF_Util_FindProcessByName (L"steam.exe");
          // Exits the Steam client if it is running
          if (pe32.th32ProcessID != 0)
          {
            SteamProcessHandle = OpenProcess (SYNCHRONIZE, FALSE, pe32.th32ProcessID);

            if (SteamProcessHandle != NULL)
            {
              // Wait on all tabs as well...
              for (auto& vWatchHandle : vWatchHandles)
                vWatchHandle.push_back (SteamProcessHandle);

              // Signal the Steam client to exit
              PLOG_INFO << "Shutting down the Steam client...";
              SKIF_Util_OpenURI (L"steam://exit");
            }
          }
        }

        if (disable)
          SKIF_ImGui_PopDisableState ( );

        if (SteamProcessHandle != NULL && WaitForSingleObject (SteamProcessHandle, 0) == WAIT_TIMEOUT)
        {
          ImGui::SameLine    ( ); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_TRIANGLE_EXCLAMATION);
          ImGui::SameLine    ( ); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "Restarting Steam...");
        }
      }
      else
      {
        ImGui::BeginGroup    ( );
        ImGui::TextColored   (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
        ImGui::SameLine      ( );
        if (ImGui::Button    ("Install " ICON_FA_STEAM " Input Kill Switch"))
        {
          SKIF_Util_OpenURI  (L"https://github.com/SpecialKO/ValvePlug/releases");
        }
        ImGui::EndGroup      ( );
        SKIF_ImGui_SetHoverTip
                             ("Allows Steam Input to be Fully Disabled or Enabled.\r\n\r\n"
                              " * Steam Input usually does stuff whether or not it is enabled per-game.\r\n"
                              " * No registry settings need to be set if you use this config menu.");
      }
      ImGui::EndGroup        ( );
      ImGui::TreePop         ( );
      ImGui::EndPopup        ( );
    }
  }

  else
    SKIF_ImGui_PopDisableState ();


  if (enableColums)
  {
    ImGui::NextColumn    ( );
    ImGui::TreePush        ("RightColumnSectionTop");
  }
  else {
    ImGui::Spacing       ( );
    ImGui::Spacing       ( );
  }

  // New column

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("This determines how long the service will remain running when launching a game.\n"
                          "Move the mouse over each option to get more information.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Auto-stop behavior when launching a game:"
  );
  ImGui::TreePush        ("AutoStopBehavior");

  //if (ImGui::RadioButton ("Never",           &_registry.iAutoStopBehavior, 0))
  //  regKVAutoStopBehavior.putData (           _registry.iAutoStopBehavior);
  // 
  //ImGui::SameLine        ( );

  if (ImGui::RadioButton ("Stop on injection",    &_registry.iAutoStopBehavior, 1))
    _registry.regKVAutoStopBehavior.putData (      _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K successfully injects into a game.");

  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Stop on game exit",      &_registry.iAutoStopBehavior, 2))
    _registry.regKVAutoStopBehavior.putData (        _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K detects that the game is being closed.");

  ImGui::TreePop         ( );

  ImGui::Spacing         ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("This setting has no effect if low bandwidth mode is enabled.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Check for updates:"
  );

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PushDisableState ( );

  ImGui::BeginGroup    ( );

  ImGui::TreePush        ("CheckForUpdates");
  if (ImGui::RadioButton ("Never",                 &_registry.iCheckForUpdates, 0))
    _registry.regKVCheckForUpdates.putData (         _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Weekly",                &_registry.iCheckForUpdates, 1))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("On each launch",        &_registry.iCheckForUpdates, 2))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);

  ImGui::TreePop         ( );

  ImGui::EndGroup      ( );

  static SKIF_Updater& _updater = SKIF_Updater::GetInstance ( );

  bool disableCheckForUpdates = _registry.bLowBandwidthMode;
  bool disableRollbackUpdates = _registry.bLowBandwidthMode;

  if (_updater.IsRunning ( ))
    disableCheckForUpdates = disableRollbackUpdates = true;

  if (! disableCheckForUpdates && _updater.GetChannels ( )->empty( ))
    disableRollbackUpdates = true;

  ImGui::TreePush        ("UpdateChannels");

  ImGui::BeginGroup    ( );

  if (disableRollbackUpdates)
    SKIF_ImGui_PushDisableState ( );

  if (ImGui::BeginCombo ("###SKIF_wzUpdateChannel", _updater.GetChannel( )->second.c_str()))
  {
    for (auto& updateChannel : *_updater.GetChannels ( ))
    {
      bool is_selected = (_updater.GetChannel()->first == updateChannel.first);

      if (ImGui::Selectable (updateChannel.second.c_str(), is_selected) && updateChannel.first != _updater.GetChannel( )->first)
      {
        _updater.SetChannel (&updateChannel); // Update selection
        _updater.SetIgnoredUpdate (L"");      // Clear any ignored updates
        _updater.CheckForUpdates  ( );        // Trigger a new check for updates
      }

      if (is_selected)
        ImGui::SetItemDefaultFocus ( );
    }

    ImGui::EndCombo  ( );
  }

  if (disableRollbackUpdates)
    SKIF_ImGui_PopDisableState  ( );

  ImGui::SameLine        ( );

  if (disableCheckForUpdates)
    SKIF_ImGui_PushDisableState ( );
  else
    ImGui::PushStyleColor       (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success));

  if (ImGui::Button      (ICON_FA_ROTATE))
  {
    _updater.SetIgnoredUpdate (L""); // Clear any ignored updates
    _updater.CheckForUpdates (true); // Trigger a forced check for updates/redownloads of repository.json and patrons.txt
  }

  SKIF_ImGui_SetHoverTip ("Check for updates");

  if (disableCheckForUpdates)
    SKIF_ImGui_PopDisableState  ( );
  else
    ImGui::PopStyleColor        ( );

  if (((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older) || _updater.IsRollbackAvailable ( ))
  {
    ImGui::SameLine        ( );

    if (disableRollbackUpdates)
      SKIF_ImGui_PushDisableState ( );
    else
      ImGui::PushStyleColor       (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));

    if (ImGui::Button      (ICON_FA_ROTATE_LEFT))
    {
      extern PopupState UpdatePromptPopup;

      // Ignore the current version
      //_updater.SetIgnoredUpdate (SK_UTF8ToWideChar (_updater.GetResults().version));
      _updater.SetIgnoredUpdate (_inject.SKVer32);

      if ((_updater.GetState() & UpdateFlags_Older) == UpdateFlags_Older)
        UpdatePromptPopup = PopupState_Open;
      else
        _updater.CheckForUpdates (false, true); // Trigger a rollback
    }

    SKIF_ImGui_SetHoverTip ("Roll back to the previous version");

    if (disableRollbackUpdates)
      SKIF_ImGui_PopDisableState  ( );
    else
      ImGui::PopStyleColor        ( );
  }

  ImGui::EndGroup      ( );

  ImGui::TreePop       ( );

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PopDisableState  ( );

  ImGui::Spacing       ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip ("This provides contextual notifications in Windows when the service starts or stops.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Show injection service desktop notifications:"
  );
  ImGui::TreePush        ("Notifications");
  if (ImGui::RadioButton ("Never",          &_registry.iNotifications, 0))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("Always",         &_registry.iNotifications, 1))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton ("When unfocused", &_registry.iNotifications, 2))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::TreePop         ( );

  if (enableColums)
  {
    ImGui::TreePop       ( );
    ImGui::Columns       (1);
  }
  else
    ImGui::EndGroup      ( );

  ImGui::PopStyleColor();

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Library
  if (ImGui::CollapsingHeader ("Library###SKIF_SettingsHeader-1"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_LIBRARY", true);
      ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }
  
    if ( ImGui::Checkbox ( "Ignore articles when sorting",                &_registry.bLibraryIgnoreArticles) )
    {
      _registry.regKVLibraryIgnoreArticles.putData (                       _registry.bLibraryIgnoreArticles);
      RepopulateGames = true;
    }

    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Ignore articles like 'THE', 'A', and 'AN' when sorting games.");

    if ( ImGui::Checkbox ( "Remember the last selected game",           &_registry.bRememberLastSelected ) )
      _registry.regKVRememberLastSelected.putData (                      _registry.bRememberLastSelected );

    if ( ImGui::Checkbox ( "Remember category collapsible state",       &_registry.bRememberCategoryState) )
      _registry.regKVRememberCategoryState.putData (                     _registry.bRememberCategoryState);

    if (enableColums)
    {
      ImGui::NextColumn    ( );
      ImGui::TreePush         ("RightColumnSectionLibrary");
    }
    else {
      ImGui::Spacing       ( );
      ImGui::Spacing       ( );
    }

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Include games from these platforms:"
    );
    ImGui::TreePush      ("Libraries");

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox        ("Epic",   &_registry.bLibraryEpic))
    {
      _registry.regKVLibraryEpic.putData  (_registry.bLibraryEpic);
      RepopulateGames = true;
    }

    if (ImGui::Checkbox         ("GOG",   &_registry.bLibraryGOG))
    {
      _registry.regKVLibraryGOG.putData   (_registry.bLibraryGOG);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox         ("Steam", &_registry.bLibrarySteam))
    {
      _registry.regKVLibrarySteam.putData (_registry.bLibrarySteam);
      RepopulateGames = true;
    }

    if (ImGui::Checkbox        ("Xbox",   &_registry.bLibraryXbox))
    {
      _registry.regKVLibraryXbox.putData  (_registry.bLibraryXbox);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );
    
    if (ImGui::Checkbox        ("Custom", &_registry.bLibraryCustom))
    {
      _registry.regKVLibraryCustom.putData(_registry.bLibraryCustom);
      RepopulateGames = true;
    }

    ImGui::EndGroup ( );

    ImGui::TreePop          ( );

    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION); // ImColor::HSV(0.11F, 1.F, 1.F)
    SKIF_ImGui_SetHoverTip ("Warning: This skips the regular platform launch process for the game,\n"
                            "including steps like the cloud saves synchronization that usually occurs.");
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Prefer Instant Play for these platforms:"
    );

    ImGui::TreePush      ("InstantPlay");

    if (ImGui::Checkbox       ("GOG",         &_registry.bInstantPlayGOG))
      _registry.regKVInstantPlayGOG.putData   (_registry.bInstantPlayGOG);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox       ("Steam",       &_registry.bInstantPlaySteam))
      _registry.regKVInstantPlaySteam.putData (_registry.bInstantPlaySteam);
    
    /* NEED MORE TESTING
    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox       ("Xbox",        &_registry.bInstantPlayXbox))
      _registry.regKVInstantPlayXbox.putData  (_registry.bInstantPlayXbox);
    */

    ImGui::TreePop          ( );

    // Column end
    if (enableColums)
    {
      ImGui::TreePop          ( );
      ImGui::Columns          (1);
    }
    else
      ImGui::EndGroup      ( );

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();



#pragma endregion


#pragma region Section: Appearances
  if (ImGui::CollapsingHeader ("Appearance###SKIF_SettingsHeader-2"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    extern bool RecreateSwapChains;
    extern bool RecreateWin32Windows;

    //ImGui::Spacing    ( );

    SKIF_ImGui_Spacing      ( );

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_APPEARANCES", true);
      ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    constexpr char* StyleItems[UIStyle_COUNT] =
    { "Dynamic",
      "SKIF Dark",
      "SKIF Light",
      "ImGui Classic",
      "ImGui Dark"
    };
    static const char*
      StyleItemsCurrent;
      StyleItemsCurrent = StyleItems[_registry.iStyle]; // Re-apply the value on every frame as it may have changed
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color theme:"
    );
    ImGui::TreePush      ("ColorThemes");

    if (ImGui::BeginCombo ("###_registry.iStyleCombo", StyleItemsCurrent)) // The second parameter is the label previewed before opening the combo.
    {
      for (int n = 0; n < UIStyle_COUNT; n++)
      {
        bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
        if (ImGui::Selectable (StyleItems[n], is_selected))
          _registry.iStyleTemp = n;         // We apply the new style at the beginning of the next frame to prevent any PushStyleColor/Var from causing issues
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
      }
      ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Useful if you find bright white covers an annoyance.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Dim game covers by 25%%:"
    );
    ImGui::TreePush        ("DimCovers");
    if (ImGui::RadioButton ("Never",                 &_registry.iDimCovers, 0))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                &_registry.iDimCovers, 1))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Based on mouse cursor", &_registry.iDimCovers, 2))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Every time the UI renders a frame, Shelly the Ghost moves a little bit.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Show Shelly the Ghost:"
    );
    ImGui::SameLine        ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_TRIANGLE_EXCLAMATION);
    SKIF_ImGui_SetHoverTip ("A critical rendering optimization will not function properly when Shelly is visibly moving.\nIt is therefor recommended to leave Shelly hidden in favor of a reduced GPU usage.");
    ImGui::TreePush        ("Shelly");
    if (ImGui::RadioButton ("Never",                    &_registry.iGhostVisibility, 0))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                   &_registry.iGhostVisibility, 1))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("While service is running", &_registry.iGhostVisibility, 2))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information.");
    ImGui::SameLine        ( );
    ImGui::TextColored     (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "UI elements:"
    );
    ImGui::TreePush        ("UIElements");

    ImGui::BeginGroup ( );

    if (ImGui::Checkbox ("Borders",    &_registry.bUIBorders))
    {
      _registry.regKVUIBorders.putData (_registry.bUIBorders);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    SKIF_ImGui_SetHoverTip ("Use borders around UI elements.");

    if (ImGui::Checkbox ("Tooltips",    &_registry.bUITooltips))
    {
      _registry.regKVUITooltips.putData (_registry.bUITooltips);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = "Info: ";

    SKIF_ImGui_SetHoverText ("This is instead where additional information will be displayed.");
    SKIF_ImGui_SetHoverTip  ("If tooltips are disabled the status bar will be used for additional information.\n"
                             "Note that some links cannot be previewed as a result.");

    if (ImGui::Checkbox ("Status bar",   &_registry.bUIStatusBar))
    {
      _registry.regKVUIStatusBar.putData (_registry.bUIStatusBar);

      // Adjust the app mode size
      SKIF_ImGui_AdjustAppModeSize (NULL);
    }

    SKIF_ImGui_SetHoverTip ("Disabling the status bar as well as tooltips will hide all additional information or tips.");

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( ); // New column
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    bool* pActiveBool = (_registry._TouchDevice) ? &_registry._TouchDevice : &_registry.bUILargeIcons;

    if (_registry._TouchDevice)
    {
      SKIF_ImGui_PushDisableState ( );
    }

    if (ImGui::Checkbox ("Large icons", pActiveBool))
      _registry.regKVUILargeIcons.putData (_registry.bUILargeIcons);

    if (_registry._TouchDevice)
    {
      SKIF_ImGui_PopDisableState  ( );
      SKIF_ImGui_SetHoverTip      ("Currently enforced by touch input mode.");
    } else
      SKIF_ImGui_SetHoverTip      ("Use larger game icons in the library tab.");

    if (ImGui::Checkbox ("Fade covers", &_registry.bFadeCovers))
    {
      _registry.regKVFadeCovers.putData (_registry.bFadeCovers);

      extern float fAlpha;
      fAlpha = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
    }

    SKIF_ImGui_SetHoverTip ("Fade between game covers when switching games.");

    if (SKIF_Util_IsWindows11orGreater ( ))
    {
      if ( ImGui::Checkbox ( "Win11 corners", &_registry.bWin11Corners) )
      {
        _registry.regKVWin11Corners.putData (  _registry.bWin11Corners);
        
        // Force recreating the window on changes
        RecreateWin32Windows = true;
      }

      SKIF_ImGui_SetHoverTip ("Use rounded window corners.");
    }

    ImGui::EndGroup ( );

    ImGui::SameLine ( );
    ImGui::Spacing  ( ); // New column
    ImGui::SameLine ( );

    ImGui::BeginGroup ( );

    if ( ImGui::Checkbox ( "Touch input", &_registry.bTouchInput) )
    {
      _registry.regKVTouchInput.putData (  _registry.bTouchInput);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    SKIF_ImGui_SetHoverTip ("Make the UI easier to use on touch input capable devices automatically.");

    if (ImGui::Checkbox ("HiDPI scaling", &_registry.bDPIScaling))
    {
      extern bool
        changedHiDPIScaling;
        changedHiDPIScaling = true;
    }

    SKIF_ImGui_SetHoverTip ("Disabling HiDPI scaling will make the application appear smaller on HiDPI displays.");

#if 0
    if ( ImGui::Checkbox ( "Auto-horizon mode", &_registry.bHorizonModeAuto) )
      _registry.regKVHorizonModeAuto.putData (   _registry.bHorizonModeAuto);

    SKIF_ImGui_SetHoverTip ("Switch to the horizontal mode on smaller displays automatically.");
#endif

    ImGui::EndGroup ( );

    if (! _registry.bUITooltips &&
        ! _registry.bUIStatusBar)
    {
      ImGui::BeginGroup  ( );
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::SameLine    ( );
      ImGui::TextColored (ImColor(0.68F, 0.68F, 0.68F, 1.0f), "Context based information and tips will not appear!");
      ImGui::EndGroup    ( );

      SKIF_ImGui_SetHoverTip ("Restore context based information and tips by enabling tooltips or the status bar.", true);
    }

    ImGui::TreePop       ( );

    if (enableColums)
    {
      ImGui::NextColumn    ( );
      ImGui::TreePush      ("RightColumnSectionAppearance");
    }
    else {
      ImGui::Spacing       ( );
      ImGui::Spacing       ( );
    }

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "UI mode:"
    );

    ImGui::TreePush        ("SKIF_iUIMode");
    // Flip VRR Compatibility Mode (only relevant on Windows 10+)
    if (SKIF_Util_IsWindows10OrGreater ( ))
    {
      if (ImGui::RadioButton ("VRR Compatibility", &_registry.iUIMode, 2))
      {
        _registry.regKVUIMode.putData (             _registry.iUIMode);
        RecreateSwapChains = true;
      }
      SKIF_ImGui_SetHoverTip ("Avoids signal loss and flickering on VRR displays.");
      ImGui::SameLine        ( );
    }
    if (ImGui::RadioButton ("Normal",              &_registry.iUIMode, 1))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip ("Improves UI response on low fixed-refresh rate displays.");
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Safe Mode",           &_registry.iUIMode, 0))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip ("Compatibility mode for users experiencing issues with the other two modes.");
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Increases the color depth of the app.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color depth:"
    );
    
    static int placeholder = 0;
    static int* ptrSDR = nullptr;

    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PushDisableState ( );

      ptrSDR = &_registry.iHDRMode;
    }
    else
      ptrSDR = &_registry.iSDRMode;
    
    ImGui::TreePush        ("iSDRMode");
    if (ImGui::RadioButton   ("8 bpc",        ptrSDR, 0))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    // It seems that Windows 10 1709+ (Build 16299) is required to
    // support 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model
    if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    {
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("10 bpc",       ptrSDR, 1))
      {
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        RecreateSwapChains = true;
      }
    }
    ImGui::SameLine        ( );
    if (ImGui::RadioButton   ("16 bpc",       ptrSDR, 2))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    ImGui::TreePop         ( );
    
    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::Spacing         ( );
    
    if (SKIF_Util_IsHDRSupported ( )  )
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip ("Makes the app pop more on HDR displays.");
      ImGui::SameLine        ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "High dynamic range (HDR):"
      );

      ImGui::TreePush        ("iHDRMode");

      if (_registry.iUIMode == 0)
      {
        ImGui::TextDisabled   ("HDR support is disabled while the UI is in Safe Mode.");
      }

      else if (SKIF_Util_IsHDRActive ( ))
      {
        if (ImGui::RadioButton ("No",             &_registry.iHDRMode, 0))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }
// Disable support for HDR10, it looks terrible and I want it gone.
#if 0
        ImGui::SameLine        ( );
        if (ImGui::RadioButton ("HDR10 (10 bpc)", &_registry.iHDRMode, 1))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }
#endif
        ImGui::SameLine        ( );
        if (ImGui::RadioButton ("scRGB (16 bpc)", &_registry.iHDRMode, 2))
        {
          _registry.regKVHDRMode.putData (         _registry.iHDRMode);
          RecreateSwapChains = true;
        }

        ImGui::Spacing         ( );

        // HDR Brightness

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::SliderInt("HDR brightness", &_registry.iHDRBrightness, 80, 400, "%d nits"))
        {
          // Reset to 203 nits (default; HDR reference white for BT.2408) if negative or zero
          if (_registry.iHDRBrightness <= 0)
              _registry.iHDRBrightness  = 203;

          // Keep the nits value between 80 and 400
          _registry.iHDRBrightness = std::min (std::max (80, _registry.iHDRBrightness), 400);
          _registry.regKVHDRBrightness.putData (_registry.iHDRBrightness);
        }
    
        if (ImGui::IsItemActive    ( ))
          allowShortcutCtrlA = false;

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PopDisableState  ( );
      }

      else {
        ImGui::TextDisabled   ("Your display(s) supports HDR, but does not use it.");
      }

      if (SKIF_Util_GetHotKeyStateHDRToggle ( ) && _registry.iUIMode != 0)
      {
        ImGui::Spacing         ( );
        /*
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped    ("FYI: Use " ICON_FA_WINDOWS " + Ctrl + Shift + H while this app is running to toggle "
                               "HDR for the display the mouse cursor is currently located on.");
        ImGui::PopStyleColor  ( );
        */
        
        ImGui::BeginGroup       ( );
        ImGui::TextDisabled     ("Use");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
          "Ctrl + " ICON_FA_WINDOWS " + Shift + H");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("to toggle HDR where the");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
            ICON_FA_ARROW_POINTER "");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("is.");
        ImGui::EndGroup         ( );
      }

      ImGui::TreePop         ( );
    }

//#define COLORPICKER
#ifdef COLORPICKER
    if (ImGui::Checkbox     ("sRGBtoLinear colors", &_registry._sRGBColors))
    {
      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    static float col3[3];
    ImGui::ColorPicker3 ("Colors", col3);
#endif

    if (enableColums)
    {
      ImGui::TreePop     ( );
      ImGui::Columns     (1);
    }
    else
      ImGui::EndGroup    ( );

    ImGui::PopStyleColor ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Keybindings

  if (ImGui::CollapsingHeader ("Keybindings###SKIF_SettingsHeader-0"))
  {
    SKIF_ImGui_Spacing      ( );

    struct kb_kv_s
    {
      SK_Keybind*                                     _key;
      SKIF_RegistrySettings::KeyValue <std::wstring>* _reg;
      std::function <void (kb_kv_s*)>                 _callback;
      std::function <bool (void)>                     _show;
    };

    static std::vector <kb_kv_s>
      keybinds = {
      { &_registry.kbToggleHDRDisplay, &_registry.regKVHotkeyToggleHDRDisplay, { [](kb_kv_s* ptr) { SKIF_Util_RegisterHotKeyHDRToggle (ptr->_key); } }, { []() { return (SKIF_Util_IsWindows10v1709OrGreater ( ) && SKIF_Util_IsHDRSupported ( )); } } },
      { &_registry.kbStartService,     &_registry.regKVHotkeyStartService,     { [](kb_kv_s* ptr) { SKIF_Util_RegisterHotKeySVCTemp   (ptr->_key); } }, { []() { return true;                                                                      } } }
    };

    ImGui::BeginGroup ();
    for (auto& keybind : keybinds)
    {
      if (! keybind._show())
        continue;

      ImGui::Text          ( "%s:  ",
                            keybind._key->bind_name );
    }
    ImGui::EndGroup   ();
    ImGui::SameLine   ();
    ImGui::BeginGroup ();
    for (auto& keybind : keybinds)
    {
      if (! keybind._show())
        continue;

      if (SK_ImGui_Keybinding (keybind._key))
      {
        keybind._reg->putData (keybind._key->human_readable);
        keybind._callback    (&keybind);
      }
    }
    ImGui::EndGroup   ();
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Advanced
  if (ImGui::CollapsingHeader ("Advanced###SKIF_SettingsHeader-3"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_ADVANCED", true);
      ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    if ( ImGui::Checkbox ( "Automatically install new updates",              &_registry.bAutoUpdate ) )
      _registry.regKVAutoUpdate.putData (                                     _registry.bAutoUpdate);

    if ( ImGui::Checkbox ( "Open this app on the same monitor as the  " ICON_FA_ARROW_POINTER, &_registry.bOpenAtCursorPosition ) )
      _registry.regKVOpenAtCursorPosition.putData (                           _registry.bOpenAtCursorPosition );

    if ( ImGui::Checkbox ( "Open this app in the mini service mode view", &_registry.bOpenInMiniMode) )
      _registry.regKVOpenInMiniMode.putData (                              _registry.bOpenInMiniMode);

    /*
    if (! SKIF_Util_GetDragFromMaximized ( ))
      SKIF_ImGui_PushDisableState ( );

    if (ImGui::Checkbox  ("Reposition this app to the center on double click",
                                                      &_registry.bMaximizeOnDoubleClick))
      _registry.regKVMaximizeOnDoubleClick.putData  (  _registry.bMaximizeOnDoubleClick);
    
    if (! SKIF_Util_GetDragFromMaximized ( ))
    {
      SKIF_ImGui_PopDisableState ( );
      SKIF_ImGui_SetHoverTip ("Feature is inaccessible due to snapping and/or\n"
                              "drag from maximized being disabled in Windows.");
    }
      */

    if (ImGui::Checkbox  ("Do not stop the injection service on exit",
                                                      &_registry.bAllowBackgroundService))
      _registry.regKVAllowBackgroundService.putData (  _registry.bAllowBackgroundService);

    SKIF_ImGui_SetHoverTip ("This allows the injection service to remain running even after this app has been closed.");

    if (_registry.bCloseToTray)
    {
      ImGui::BeginGroup   ( );
      SKIF_ImGui_PushDisableState ( );
    }

    if ( ImGui::Checkbox (
            "Allow multiple instances of this app",
              &_registry.bAllowMultipleInstances )
        )
    {
      if (! _registry.bAllowMultipleInstances)
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
              if (SKIF_Notify_hWnd != hWnd) // Don't send WM_QUIT to ourselves
                PostMessage (  hWnd, WM_QUIT,
                                0x0, 0x0  );
            }
          }
          return TRUE;
        }, (LPARAM)SKIF_NotifyIcoClass);
      }

      _registry.regKVAllowMultipleInstances.putData (
        _registry.bAllowMultipleInstances
        );
    }

    if (_registry.bCloseToTray)
    {
      SKIF_ImGui_PopDisableState  ( );
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::EndGroup        ( );
      SKIF_ImGui_SetHoverTip ("Requires 'Close to the notification area' to be disabled.");
    }

    /* Not exposed since its not finalized (and I am still not sure if I want to have this or not...)
    if (ImGui::Checkbox  ("Controller support",
                                                      &_registry.bControllers))
      _registry.regKVControllers.putData              (_registry.bControllers);

    SKIF_ImGui_SetHoverTip  ("Allow using a controller to navigate the UI of this app.");
    */

    if (enableColums)
    {
      ImGui::NextColumn    ( );
      ImGui::TreePush      ("RightColumnSectionAdvanced");
    }
    else {
      ImGui::Spacing       ( );
      ImGui::Spacing       ( );
    }
    
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        ICON_FA_WRENCH "  Troubleshooting:"
    );

    SKIF_ImGui_Spacing ( );

    const char* LogSeverity[] = { "None",
                                  "Fatal",
                                  "Error",
                                  "Warning",
                                  "Info",
                                  "Debug",
                                  "Verbose" };
    static const char* LogSeverityCurrent = LogSeverity[_registry.iLogging];

    if (ImGui::BeginCombo (" Log level###_registry.iLoggingCombo", LogSeverityCurrent))
    {
      for (int n = 0; n < IM_ARRAYSIZE (LogSeverity); n++)
      {
        bool is_selected = (LogSeverityCurrent == LogSeverity[n]);
        if (ImGui::Selectable (LogSeverity[n], is_selected))
        {
          _registry.iLogging = n;
          _registry.regKVLogging.putData  (_registry.iLogging);
          LogSeverityCurrent = LogSeverity[_registry.iLogging];
          plog::get()->setMaxSeverity((plog::Severity)_registry.iLogging);

          ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                    ? ImGuiDebugLogFlags_EventMask_
                                                    : ImGuiDebugLogFlags_EventViewport);
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    if (_registry.iLogging >= 6 && _registry.bDeveloperMode)
    {
      if (ImGui::Checkbox  ("Enable excessive development logging", &_registry.bLoggingDeveloper))
      {
        _registry.regKVLoggingDeveloper.putData                     (_registry.bLoggingDeveloper);

        ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                  ? ImGuiDebugLogFlags_EventMask_
                                                  : ImGuiDebugLogFlags_EventViewport);
      }
    }

    SKIF_ImGui_SetHoverTip  ("Only intended for SKIF developers as this enables excessive logging (e.g. window messages).");

    SKIF_ImGui_Spacing ( );

    const char* Diagnostics[] = { "None",
                                  "Normal",
                                  "Enhanced" };
    static const char* DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];

    if (ImGui::BeginCombo (" Diagnostics###_registry.iDiagnostics", DiagnosticsCurrent))
    {
      for (int n = 0; n < IM_ARRAYSIZE (Diagnostics); n++)
      {
        bool is_selected = (DiagnosticsCurrent == Diagnostics[n]);
        if (ImGui::Selectable (Diagnostics[n], is_selected))
        {
          _registry.iDiagnostics = n;
          _registry.regKVDiagnostics.putData (_registry.iDiagnostics);
          DiagnosticsCurrent = Diagnostics[_registry.iDiagnostics];
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus ( );
      }
      ImGui::EndCombo  ( );
    }

    ImGui::SameLine    ( );
    ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  ("Help improve Special K by allowing anonymized diagnostics to be sent.\n"
                             "The data is used to identify issues, highlight common use cases, and\n"
                             "facilitates the continued development of the application.");

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/Privacy");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/Privacy");

    SKIF_ImGui_Spacing ( );

    if (ImGui::Checkbox  ("Developer mode",  &_registry.bDeveloperMode))
    {
      _registry.regKVDeveloperMode.putData   (_registry.bDeveloperMode);

      ImGui::GetCurrentContext()->DebugLogFlags = ImGuiDebugLogFlags_OutputToTTY | ((_registry.isDevLogging())
                                                ? ImGuiDebugLogFlags_EventMask_
                                                : ImGuiDebugLogFlags_EventViewport);
    }

    SKIF_ImGui_SetHoverTip  ("Exposes additional information and context menu items that may be of interest for developers.");

    ImGui::SameLine    ( );

    if (ImGui::Checkbox  ("Efficiency mode", &_registry.bEfficiencyMode))
      _registry.regKVEfficiencyMode.putData  (_registry.bEfficiencyMode);

    SKIF_ImGui_SetHoverTip  ("Engage efficiency mode for this app when idle.\n"
                             "Not recommended for Windows 10 and earlier.");

    SKIF_ImGui_Spacing ( );

    ImGui::TextWrapped ("Nvidia users: Use the below button to prevent GeForce Experience from mistaking this app for a game.");

    ImGui::Spacing     ( );

    static bool runOnceGFE = false;

    if (runOnceGFE)
      SKIF_ImGui_PushDisableState ( );

    if (ImGui::ButtonEx (ICON_FA_USER_SHIELD " Disable GFE notifications",
                                 ImVec2 (250 * SKIF_ImGui_GlobalDPIScale,
                                          25 * SKIF_ImGui_GlobalDPIScale)))
    {
      runOnceGFE = true;

      PLOG_INFO << "Attempting to disable GeForce Experience / ShadowPlay notifications...";
      wchar_t              wszRunDLL32 [MAX_PATH + 2] = { };
      GetSystemDirectoryW (wszRunDLL32, MAX_PATH);
      PathAppendW         (wszRunDLL32, L"rundll32.exe");
      static std::wstring wsDisableCall = SK_FormatStringW (
        LR"("%ws\%ws",RunDLL_DisableGFEForSKIF)",
          _path_cache.specialk_install,
#ifdef _WIN64
          L"SpecialK64.dll"
#else
          L"SpecialK32.dll"
#endif
        );

      SHELLEXECUTEINFOW
        sexi              = { };
        sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
        sexi.lpVerb       = L"RUNAS";
        sexi.lpFile       = wszRunDLL32;
      //sexi.lpDirectory  = ;
        sexi.lpParameters = wsDisableCall.c_str();
        sexi.nShow        = SW_SHOWNORMAL;
        sexi.fMask        = SEE_MASK_NOASYNC | SEE_MASK_NOZONECHECKS;
        
      SetLastError (NO_ERROR);

      bool ret = ShellExecuteExW (&sexi);

      if (GetLastError ( ) != NO_ERROR)
        PLOG_ERROR << "An unexpected error occurred: " << SKIF_Util_GetErrorAsWStr();

      if (ret)
        PLOG_INFO  << "The operation was successful.";
      else
        PLOG_ERROR << "The operation was unsuccessful.";
    }
    
    // Prevent this call from executing on the same frame as the button is pressed
    else if (runOnceGFE)
      SKIF_ImGui_PopDisableState ( );
    
    ImGui::SameLine         ( );
    ImGui::TextColored      (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip  ("This only needs to be used if GeForce Experience notifications\n"
                             "appear on the screen whenever this app is being used.");

    if (enableColums)
    {
      ImGui::TreePop        ( );
      ImGui::Columns        (1);
    }
    else
      ImGui::EndGroup       ( );

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Whitelist / Blacklist
  if (ImGui::CollapsingHeader ("Whitelist / Blacklist###SKIF_SettingsHeader-4")) //, ImGuiTreeNodeFlags_DefaultOpen)) // Disabled auto-open for this section
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_TRIANGLE_EXCLAMATION);
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_LIGHTBULB);
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_LIGHTBULB);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "The list can only include");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure),   " 128 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "lines, though multiple can be combined using a pipe");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success),   " | ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "character.");
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          "(e.g. \"NieRAutomataPC|Epic Games\" will match any application\n"
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
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text        ("Easiest is to use the name of the executable or folder of the game.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at\n"
      "C:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"
    );

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text        ("Typing the name of a shared parent folder will match all applications below that folder.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Epic Games\" will match any\n"
      "application installed under the Epic Games folder."
    );

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text ("Note that these lists do not prevent Special K from being injected into processes.");
    ImGui::EndGroup   ();

    if (! _registry.bUITooltips)
    {
      SKIF_ImGui_SetHoverTip (
        "These lists control whether Special K should be enabled (the whitelist) to hook APIs etc,\n"
        "or remain disabled/idle/inert (the blacklist) within the injected process."
      );
    }

    else
    {
      SKIF_ImGui_SetHoverTip (
        "The injection service injects Special K into any process that deals\n"
        "with system input or some sort of window or kb/mouse input activity.\n"
        "\n"
        "These lists control whether Special K should be enabled (the whitelist),\n"
        "or remain idle/inert (the blacklist) within the injected process."
      );
    }

    ImGui::PopStyleColor  ();

    ImGui::NewLine    ();

    // Whitelist section

    ImGui::BeginGroup ();

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_SQUARE_PLUS);
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
    
    if (ImGui::IsItemActive    ( ))
      allowShortcutCtrlA = false;

    if (*_inject.whitelist == '\0')
    {
      SKIF_ImGui_SetHoverTip (
        "These are the patterns used internally to enable Special K for these specific platforms.\n"
        "They are presented here solely as examples of how a potential pattern might look like."
      );
    }

    _CheckWarnings (_inject.whitelist);

    ImGui::EndGroup   ();

    ImGui::SameLine   ();

    ImGui::BeginGroup ();

#if 0
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
    ImGui::TextColored ((_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_WINDOWS);
  //ImGui::TextColored ((_registry._StyleLightMode) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_XBOX);
    ImGui::EndGroup    ();

    ImGui::SameLine    ();

    ImGui::BeginGroup  ();
    if (ImGui::Selectable ("Games"))
    {
      white_edited = true;

      _inject.WhitelistPattern ("Games");
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on most platforms, such as Uplay, as\n"
      "most of them have \"games\" in the full path somewhere."
    );

    /*
    if (ImGui::Selectable ("WindowsApps"))
    {
      white_edited = true;

      _inject.AddUserListPattern("WindowsApps", true);
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on the Microsoft Store or Game Pass."
    );
    */

    ImGui::EndGroup ();

#endif

    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Blacklist section

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_SQUARE_MINUS);
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

    if (ImGui::IsItemActive    ( ))
      allowShortcutCtrlA = false;

    _CheckWarnings (_inject.blacklist);

    ImGui::Separator ();

    bool bDisabled =
      (white_edited || black_edited) ?
                                false : true;

    bool hotkeyCtrlS = false;

    if (bDisabled)
      SKIF_ImGui_PushDisableState ( );
    else
      hotkeyCtrlS = ImGui::GetIO().KeyCtrl               &&
                    ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f;

    // Hotkey: Ctrl+S
    if (ImGui::Button (ICON_FA_FLOPPY_DISK " Save Changes") || hotkeyCtrlS)
    {
      // Clear the active ID to prevent ImGui from holding outdated copies of the variable
      //   if saving succeeds, to allow SaveUserList to update the variable successfully
      ImGui::ClearActiveID();

      if (white_edited)
      {
        white_stored = _inject.SaveWhitelist ( );

        if (white_stored)
          white_edited = false;
      }

      if (black_edited)
      {
        black_stored = _inject.SaveBlacklist ( );

        if (black_stored)
          black_edited = false;
      }
    }

    ImGui::SameLine ();

    if (ImGui::Button (ICON_FA_ROTATE_LEFT " Reset"))
    {
      if (white_edited)
      {
        _inject.LoadWhitelist ( );

        white_edited = false;
        white_stored = true;
      }

      if (black_edited)
      {
        _inject.LoadBlacklist ( );

        black_edited = false;
        black_stored = true;
      }
    }

    if (bDisabled)
      SKIF_ImGui_PopDisableState  ( );

    ImGui::Spacing();

    if (! white_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The whitelist could not be saved! Please remove any non-Latin characters and try again.");
    }

    if (! black_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The blacklist could not be saved! Please remove any non-Latin characters and try again.");
    }

    ImGui::EndGroup       ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion
  
#pragma region Section: Extended CPU Hardware Reporting [64-bit only]
#ifdef _WIN64
  if (ImGui::CollapsingHeader ("Extended CPU Hardware Reporting###SKIF_SettingsHeader-5"))
  {
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

    SKIF_ImGui_Spacing      ( );

    // WinRing0
    ImGui::BeginGroup  ();

    ImGui::TextWrapped    (
      "Special K can make use of an optional kernel driver to provide additional metrics in the CPU widget."
    );

    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
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
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
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
      btnDriverLabel    = ICON_FA_USER_SHIELD " Uninstall Driver";
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
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
      ImGui::SameLine    ();
      ImGui::Text        ("Conflict With:");
      ImGui::SameLine    ();
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), SK_WideCharToUTF8 (driverBinaryPath).c_str ());
    }

    // Obsolete driver is installed
    else if (driverStatus == ObsoleteInstalled)
    {
      btnDriverLabel    = ICON_FA_USER_SHIELD " Migrate Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Obsolete driver installed");
      wszDriverTaskCmd = L"Migrate Install";
    }

    // Driver is not installed
    else {
      btnDriverLabel    = ICON_FA_USER_SHIELD " Install Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Not Installed");
      wszDriverTaskCmd = L"Install";
    }

    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Disable button if the required files are missing, status is pending, or if another driver is installed
    if (  driverStatusPending  != driverStatus ||
          OtherDriverInstalled == driverStatus )
      SKIF_ImGui_PushDisableState ( );

    // Show button
    bool driverButton =
      ImGui::ButtonEx (btnDriverLabel.c_str(), ImVec2(200 * SKIF_ImGui_GlobalDPIScale,
                                                       25 * SKIF_ImGui_GlobalDPIScale));
    SKIF_ImGui_SetHoverTip (
      "Administrative privileges are required on the system to enable this."
    );

    if ( driverButton )
    {
      if (PathFileExists (SKIFdrv.c_str()) && PathFileExists (SYSdrv.c_str()))
      {
        if (ShellExecuteW (nullptr, L"runas", SKIFdrv.c_str(), wszDriverTaskCmd.c_str(), nullptr, SW_SHOWNORMAL) > (HINSTANCE)32)
          driverStatusPending =
                (driverStatus == Installed) ?
                              NotInstalled  : Installed;
      }
      else {
        SKIF_Util_OpenURI (L"https://wiki.special-k.info/en/SpecialK/Tools#extended-hardware-monitoring-driver");
      }
    }

    // Disabled button
    //   the 'else if' is only to prevent the code from being called on the same frame as the button is pressed
    else if (    driverStatusPending != driverStatus ||
                OtherDriverInstalled == driverStatus )
      SKIF_ImGui_PopDisableState  ( );

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

    // Show warning about another driver being installed
    else if (ObsoleteInstalled == driverStatus)
    {
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Spacing();
      ImGui::SameLine(); ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
        "An older version of the driver is installed."
      );
      ImGui::EndGroup();
    }

    ImGui::EndGroup ();

    ImGui::PopStyleColor ();
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#endif
#pragma endregion
  
#pragma region Section: SwapChain Presentation Monitor
  if (ImGui::CollapsingHeader ("SwapChain Presentation Monitor###SKIF_SettingsHeader-6", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

    SKIF_ImGui_Spacing      ( );

    // PresentMon prerequisites
    ImGui::BeginGroup  ();

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_SWAPCHAIN", true);
      //ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Tell at a glance whether:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_UP_RIGHT_FROM_SQUARE);
    ImGui::SameLine    ();
    ImGui::TextWrapped ("DirectFlip has engaged, bypassing desktop composition (DWM).");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip        ("Appears as 'Hardware: Independent Flip' or 'Hardware Composed: Independent Flip'.");

    SKIF_ImGui_SetHoverTip        ("DirectFlip appears as 'Hardware: Independent Flip' or 'Hardware Composed: Independent Flip'.\n"
                                   "Lack of it appears as 'Composed: Flip'.");
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/en/SwapChain#fse-fso-independent-flip-etc-sorry-but-what");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/en/SwapChain#fse-fso-independent-flip-etc-sorry-but-what");

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_UP_RIGHT_FROM_SQUARE);
    ImGui::SameLine    ();
    ImGui::TextWrapped ("Legacy Exclusive Fullscreen (FSE) mode is being used or if Fullscreen Optimizations (FSO) overrides it.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip        ("FSE appears as 'Hardware: Legacy Flip' or 'Hardware: Legacy Copy to front buffer'.\n"
                                   "FSO appears as 'Hardware: Independent Flip' or 'Hardware Composed: Independent Flip'.");
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       ("https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    ImGui::SameLine    ();
    ImGui::TextWrapped ("The game is running in a suboptimal presentation mode.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip        ("Appears as 'Composed: Flip', 'Composed: Composition Atlas',\n"
                                   "'Composed: Copy with GPU GDI', or 'Composed: Copy with CPU GDI'.");

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
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
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
      SKIF_ImGui_PushDisableState ( );

    std::string btnPfuLabel = (pfuState == Granted) ?                         ICON_FA_CHECK              " Permissions granted!" // Granted
                                                    : (pfuState == Missing) ? ICON_FA_USER_SHIELD        " Grant permissions"    // Missing
                                                                            : ICON_FA_RIGHT_FROM_BRACKET " Sign out to apply";   // Pending

    if ( ImGui::ButtonEx ( btnPfuLabel.c_str(), ImVec2( 200 * SKIF_ImGui_GlobalDPIScale,
                                                         25 * SKIF_ImGui_GlobalDPIScale)))
    {
      std::wstring exeArgs;

      TCHAR pfuName        [MAX_PATH + 2] = { },
            intName        [MAX_PATH + 2] = { };
      DWORD pfuNameLength = MAX_PATH,
            intNameLength = MAX_PATH;

      // Unused variables
      SID_NAME_USE pfuSnu, intSnu;
      TCHAR pfuDomainName        [MAX_PATH + 2] = { },
            intDomainName        [MAX_PATH + 2] = { };
      DWORD pfuDomainNameLength = MAX_PATH,
            intDomainNameLength = MAX_PATH;

      // For Windows 10+ we rely on modern PowerShell cmdlets as this 
      //   is the easiest way of handling non-English localizations
      if (SKIF_Util_IsWindows10OrGreater ( ))
      {
        // S-1-5-32-559 == Group : Performance Log Users
        // S-1-5-4      == User  : NT AUTHORITY\INTERACTIVE
        if (ShellExecuteW (nullptr, L"runas", L"powershell.exe", LR"(-NoProfile -NonInteractive -WindowStyle Hidden -Command "Add-LocalGroupMember -SID 'S-1-5-32-559' -Member 'S-1-5-4'")", nullptr, SW_SHOWNORMAL) > (HINSTANCE)32)
          pfuState = Pending;
      }

      // On older versions we need to rely on 'net', but we also need to retrieve the local names first
      //   as non-English languages has localized user and group names...
      else if (LookupAccountSid (NULL, pfuSID, pfuName, &pfuNameLength, pfuDomainName, &pfuDomainNameLength, &pfuSnu) &&
               LookupAccountSid (NULL, intSID, intName, &intNameLength, intDomainName, &intDomainNameLength, &intSnu))
      {
        exeArgs = LR"(localgroup ")" + std::wstring(pfuName) + LR"(" ")" + std::wstring(intName) + LR"(" /add)";

        // Note that this can still fail apparently, as 'net' might be unable to handle some non-Latin characters properly (e.g. Russian localized names)
        if (ShellExecuteW (nullptr, L"runas", L"net", exeArgs.c_str(), nullptr, SW_SHOWNORMAL) > (HINSTANCE)32)
          pfuState = Pending;
      }
    }

    // Disable button for granted + pending states
    else if (pfuState != Missing)
      SKIF_ImGui_PopDisableState  ( );

    else
    {
      SKIF_ImGui_SetHoverTip ("Administrative privileges are required on the system to toggle this.");
    }

    if (enableColums)
    {
      ImGui::NextColumn ( );
      ImGui::TreePush   ("RightColumnSectionSwapChain");
    }
    else {
      ImGui::Spacing    ( );
      ImGui::Spacing    ( );
    }

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_THUMBS_UP);
    ImGui::SameLine    ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Minimal latency:");

    ImGui::TreePush    ("LatencyMinimal");
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Hardware: Independent Flip");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Hardware Composed: Independent Flip");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Hardware: Legacy Flip");

    /* Extremely uncommon but included in the list anyway */
    ImGui::BeginGroup  ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "Hardware: Legacy Copy to front buffer");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip ("Quite uncommon to see compared to the other models listed here.");
    
    ImGui::TreePop     ();

    SKIF_ImGui_Spacing ();
            
    ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), ICON_FA_THUMBS_DOWN);
    ImGui::SameLine    ();
    ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), "Undesirable latency:");

    ImGui::TreePush    ("LatencyUndesirable");
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Flip");

    /* Disabled as PresentMon doesn't detect this any longer as of May 2022.
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Composition Atlas");
    */

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Copy with GPU GDI");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Composed: Copy with CPU GDI");
    ImGui::TreePop     ();
    
    if (enableColums)
    {
      ImGui::TreePop   ( );
      ImGui::Columns   (1);
    }
    else
      ImGui::EndGroup  ( );
    
    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::Separator   ();

    // Multi-Plane Overlay (MPO) section
    ImGui::Spacing     ();

    ImVec2 mpoTop = ImGui::GetCursorScreenPos ( );
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Multi-Plane Overlay (MPO) Support"
    );

    ImGui::TextWrapped    (
      "MPOs are additional dedicated hardware scanout planes"
      " enabling the GPU to partially take over composition from the DWM. This allows"
      " games to bypass the DWM in various mixed scenarios or window modes,"
      " eliminating the presentation latency that would otherwise be incurred."
    );

    SKIF_ImGui_Spacing ();

    if (enableColums)
    {
      SKIF_ImGui_Columns    (2, "SKIF_COLUMN_SETTINGS_MPO", true);
      //ImGui::SetColumnWidth (0, columnWidth); //SKIF_vecCurrentMode.x / 2.0f) // 480.0f * SKIF_ImGui_GlobalDPIScale
    }
    else {
      // This is needed to reproduce the same padding on the left side as when using columns
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
      ImGui::BeginGroup    ( );
    }

    ImGui::BeginGroup  ();

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        "Support among connected displays:"
    );

    if (SKIF_Util_IsWindows10OrGreater ( ))
    {
      ImGui::BeginChild  ("##MPOChild", ImVec2 (0, (Monitors.size() + 1) * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ScrollbarSize), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar); // (Monitors.size() + 1) * ImGui::GetTextLineHeightWithSpacing()

      ImGui::Text        ("Display");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::Text        ("Planes");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (235.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::Text        ("Stretch");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (360.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::Text        ("Capabilities");

      // MPO support is a bloody mystery...
      for (auto& monitor : Monitors)
      {
        std::string stretchFormat = (monitor.MaxStretchFactor < 10.0f) ? "  %.1fx - %.1fx" // two added spaces for sub-10.0x to align them vertically with other displays
                                                                       :   "%.1fx - %.1fx";
        ImVec4 colName            = (monitor.Supported) ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)
                                                        : ImVec4 (ImColor::HSV (0.11F, 1.F, 1.F));
        ImVec4 colCaps            = (monitor.Supported) ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)
                                                        : ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase);

        ImGui::BeginGroup    ( );
        //ImGui::Text        ("%u", monitor.Index);
        //ImGui::SameLine    ( );
        ImGui::TextColored   (colName, monitor.Name.c_str());
        SKIF_ImGui_SetHoverTip (monitor.DeviceNameGdi.c_str());
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (170.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::Text          ("%u", (monitor.Supported) ? monitor.MaxPlanes : 1);
        if (! monitor.Supported && monitor.MaxPlanes > 1)
          SKIF_ImGui_SetHoverTip (std::to_string(monitor.MaxPlanes));
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (235.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::Text          ((monitor.Supported) ? stretchFormat.c_str() : "Not Supported",
                               monitor.MaxStretchFactor, monitor.MaxShrinkFactor);
        if (! monitor.Supported && monitor.MaxStretchFactor != monitor.MaxShrinkFactor)
          SKIF_ImGui_SetHoverTip (SKIF_Util_FormatStringRaw (stretchFormat.c_str(), monitor.MaxStretchFactor, monitor.MaxShrinkFactor));
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (((monitor.Supported) ? 390.0f : 360.0f) * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::TextColored   (colCaps, (monitor.Supported) ? ICON_FA_LIGHTBULB : "Not Supported");
        if (monitor.Supported)
          SKIF_ImGui_SetHoverTip (monitor.OverlayCapsAsString.c_str(), true);
        ImGui::EndGroup      ( );
      }

      ImGui::EndChild ( ); // ##MPOChild

      if (SKIF_Util_IsMPOsDisabledInRegistry ( ))
      {
        // Move up the line 2 pixels as otherwise a scroll would appear...
        //ImGui::SetCursorPosY    (ImGui::GetCursorPosY ( ) - (2.0f * SKIF_ImGui_GlobalDPIScale));
        ImGui::PushStyleColor     (ImGuiCol_Text, ImColor::HSV (0.11F,   1.F, 1.F).Value);
        if (ImGui::Selectable (ICON_FA_TRIANGLE_EXCLAMATION "  Disabled through the registry! Click to reset."))
        {
          std::wstring cmd = L"";

          switch (MessageBox (NULL,
            L"Restart the display drivers to apply the changes.\n"
            L"\n"
            L"Some games and applications (e.g. Steam, Discord) may close or crash when the drivers are restarted.\n"
            L"\n"
            L"Do you want to restart the display drivers now?",
            L"Restart the display drivers?", MB_ICONQUESTION | MB_YESNOCANCEL))
          {

          // When YES is used, we reset the registry keys and restart the display drivers
          case IDYES:
            cmd = L"ResetOverlayMode RestartDisplDrv";
            break;

          // When NO is used, we only reset the registry keys.
          case IDNO:
            cmd = L"ResetOverlayMode";
            break;

          // When CANCEL is used, we do nothing.
          case IDCANCEL:
            break;
          }

          //  ShellExecuteW (nullptr, L"runas", L"cmd", LR"(/c REG DELETE HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers /v DisableOverlays /f && REG DELETE HKLM\SOFTWARE\Microsoft\Windows\Dwm /v OverlayTestMode /f)", nullptr, SW_SHOWNORMAL)
          if (! cmd.empty() && ShellExecuteW (nullptr, L"runas", _path_cache.skif_executable, cmd.c_str(), nullptr, SW_SHOWNORMAL) > (HINSTANCE)32)
          {
            dwTriggerNewRefresh = SKIF_Util_timeGetTime ( ) + 5000; // Trigger a refresh in 500ms
          }
        }
        ImGui::PopStyleColor          ( );
        SKIF_ImGui_SetHoverTip        ("A restart of the display drivers is required for the changes to be applied.");
        SKIF_ImGui_SetMouseCursorHand ( );
      }
    }
    else {
      ImGui::Text      ("Reporting MPO capabilities requires Windows 10 or newer.");
    }

    ImGui::EndGroup    ();

    if (enableColums)
    {
      ImGui::NextColumn ( );
      ImGui::TreePush   ("MinimumRequirements");
    }
    else {
      ImGui::Spacing    ( );
      ImGui::Spacing    ( );
    }

    ImGui::PushStyleColor (ImGuiCol_Text,
      ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

    ImGui::TextColored (
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption),
                        "Minimum requirement:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("AMD: Radeon RX Vega + Adrenalin Edition 22.5.2 drivers");
    ImGui::EndGroup    ();
    // Exact hardware models are unknown, but a bunch of dxdiag.txt files dropped online suggests Radeon RX Vega and newer had MPO support.
    // ID3D13Sylveon on the DirectX Discord mentioned that driver support was added in 22.20, so AMD Software: Adrenalin Edition 22.5.2.

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Intel: HD Graphics 510-515 (Core 6th gen)");
    ImGui::EndGroup    ();
    // From https://www.intel.com/content/www/us/en/developer/articles/training/tutorial-migrating-your-apps-to-directx-12-part-4.html

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        ("Nvidia: GTX 16/RTX 20 series (Turing) + R460 drivers");
    ImGui::EndGroup    ();
    // Official Nvidia requirement from their driver release notes is Volta and later GPUs and the Release 460 driver and later
    // As Volta only had the Titan V and Quadro GV100 models we can just say GTX 16/RTX 20 series
    
    ImGui::Spacing     ();

    ImGui::TextWrapped ("Support depends on the GPU and display configuration. Unusual driver "
                        "or display configurations might disable MPO support, such as by using "
                        "10 bpc in SDR mode or custom GPU scaling (NIS/RSR/integer etc).");

    ImGui::PopStyleColor ();

    if (enableColums)
    {
      ImGui::TreePop   ( );
      ImGui::Columns   (1);
    }
    else
    {
      ImGui::NewLine   ( );
      ImGui::EndGroup  ( );
    }

    ImRect mpoRect = ImRect (mpoTop, mpoTop + ImGui::GetContentRegionMax ( ));

    bool openMenu = false;

    if (ImGui::IsMousePosValid ( ) && ImGui::IsMouseHoveringRect (mpoRect.Min, mpoRect.Max) && ImGui::IsMouseClicked(ImGuiMouseButton_Right, false))
      openMenu = true;

    if (openMenu)
      ImGui::OpenPopup ("DisplayDriverMenu");

    if (ImGui::BeginPopup ("DisplayDriverMenu", ImGuiWindowFlags_NoMove))
    {
      if (ImGui::Selectable (ICON_FA_ROTATE " Refresh"))
        RefreshSettingsTab = true;

      ImGui::Separator ( );

      if (ImGui::Selectable (ICON_FA_ROTATE_RIGHT " Restart display driver"))
      {
        PLOG_DEBUG << "Restarting the display driver...";
        ShellExecuteW (nullptr, L"runas", _path_cache.skif_executable, L"RestartDisplDrv", nullptr, SW_SHOWNORMAL);
      }

      ImGui::EndPopup ( );
    }

    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::EndGroup    ();

    ImGui::PopStyleColor ();
  }
#pragma endregion

}

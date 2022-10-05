#include <registry.h>

SKIF_WindowsRegistry _registry;

SKIF_WindowsRegistry::SKIF_WindowsRegistry (void)
{
  // Settings
  extern int SKIF_iNotifications;
  extern int SKIF_iGhostVisibility;
  extern int SKIF_iStyle;
  extern int SKIF_iDimCovers;
  extern int SKIF_iCheckForUpdates;
  extern int SKIF_iAutoStopBehavior;
  extern int SKIF_iLogging;

  //XXX: These are all defined extern in SKIF.h, consider including that header instead?
  extern uint32_t SKIF_iLastSelected;
  extern bool SKIF_bRememberLastSelected;
  extern bool SKIF_bDisableDPIScaling;
  extern bool SKIF_bDisableTooltips;
  extern bool SKIF_bDisableStatusBar;
  extern bool SKIF_bDisableBorders;
  extern bool SKIF_bDisableSteamLibrary;
  extern bool SKIF_bDisableEGSLibrary;
  extern bool SKIF_bDisableGOGLibrary;
  extern bool SKIF_bDisableXboxLibrary;
  extern bool SKIF_bSmallMode;
  extern bool SKIF_bFirstLaunch;
  extern bool SKIF_bEnableDebugMode;
  extern bool SKIF_bAllowMultipleInstances;
  extern bool SKIF_bAllowBackgroundService;
  extern bool SKIF_bEnableHDR;
  extern bool SKIF_bDisableVSYNC;
  extern bool SKIF_bOpenAtCursorPosition;
  extern bool SKIF_bStopOnInjection;
  extern bool SKIF_bCloseToTray;
  extern bool SKIF_bLowBandwidthMode;
  extern bool SKIF_bPreferGOGGalaxyLaunch;
  extern bool SKIF_bMinimizeOnGameLaunch;

  SKIF_bLowBandwidthMode        =   regKVLowBandwidthMode.getData        ( );
  SKIF_bPreferGOGGalaxyLaunch   =   regKVPreferGOGGalaxyLaunch.getData   ( );
  
  if (regKVRememberLastSelected.hasData())
    SKIF_bRememberLastSelected  =   regKVRememberLastSelected.getData    ( );

  SKIF_bDisableDPIScaling       =   regKVDisableDPIScaling.getData       ( );
  SKIF_bDisableTooltips         =   regKVDisableTooltips.getData         ( );
  SKIF_bDisableStatusBar        =   regKVDisableStatusBar.getData        ( );
  SKIF_bDisableSteamLibrary     =   regKVDisableSteamLibrary.getData     ( );
  SKIF_bDisableEGSLibrary       =   regKVDisableEGSLibrary.getData       ( );
  SKIF_bDisableGOGLibrary       =   regKVDisableGOGLibrary.getData       ( );

  if (regKVDisableXboxLibrary.hasData())
    SKIF_bDisableXboxLibrary    =   regKVDisableXboxLibrary.getData      ( );

  SKIF_bEnableDebugMode         =   regKVEnableDebugMode.getData         ( );
  SKIF_bSmallMode               =   regKVSmallMode.getData               ( );
  SKIF_bFirstLaunch             =   regKVFirstLaunch.getData             ( );
  SKIF_bAllowMultipleInstances  =   regKVAllowMultipleInstances.getData  ( );
  SKIF_bAllowBackgroundService  =   regKVAllowBackgroundService.getData  ( );
//SKIF_bEnableHDR               =   regKVEnableHDR.getData               ( );
  SKIF_bDisableVSYNC            =   regKVDisableVSYNC.getData            ( );
  SKIF_bOpenAtCursorPosition    =   regKVOpenAtCursorPosition.getData    ( );
  
  // If the legacy key has data, but not the new key, move the data over to respect existing user's choices
  if (!regKVDisableStopOnInjection.hasData() && regKVLegacyDisableStopOnInjection.hasData())
    regKVDisableStopOnInjection.putData (regKVLegacyDisableStopOnInjection.getData());

  SKIF_bStopOnInjection         = ! regKVDisableStopOnInjection.getData  ( );
  SKIF_bMinimizeOnGameLaunch    =   regKVMinimizeOnGameLaunch.getData    ( );
  SKIF_bCloseToTray             =   regKVCloseToTray.getData             ( );

  if (regKVDisableBorders.hasData())
    SKIF_bDisableBorders        =   regKVDisableBorders.getData          ( );

  if (regKVAutoStopBehavior.hasData())
    SKIF_iAutoStopBehavior      =   regKVAutoStopBehavior.getData        ( );

  if (regKVNotifications.hasData())
    SKIF_iNotifications         =   regKVNotifications.getData           ( );

  if (regKVGhostVisibility.hasData())
    SKIF_iGhostVisibility       =   regKVGhostVisibility.getData         ( );

  if (regKVStyle.hasData())
    SKIF_iStyle                 =   regKVStyle.getData                   ( );

  if (regKVLogging.hasData())
    SKIF_iLogging               =   regKVLogging.getData                 ( );

  if (regKVDimCovers.hasData())
    SKIF_iDimCovers             =   regKVDimCovers.getData               ( );

  if (regKVCheckForUpdates.hasData())
    SKIF_iCheckForUpdates       =   regKVCheckForUpdates.getData         ( );

  if (regKVIgnoreUpdate.hasData())
    wsIgnoreUpdate              =   regKVIgnoreUpdate.getWideString      ( );

  if (regKVFollowUpdateChannel.hasData())
    wsUpdateChannel             = regKVFollowUpdateChannel.getWideString ( );

  if (SKIF_bRememberLastSelected && regKVLastSelected.hasData())
    SKIF_iLastSelected          =   regKVLastSelected.getData            ( );

  // App registration
  if (regKVAppRegistration.hasData())
    wsAppRegistration           = regKVAppRegistration.getWideString ( );

  if (regKVPath.hasData())
    wsPath                      = regKVPath.getWideString ( );
}
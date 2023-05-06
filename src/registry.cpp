#include <registry.h>

SKIF_RegistrySettings::SKIF_RegistrySettings (void)
{
  // Default settings (multiple options)
  iNotifications           = 2; // 0 = Never,                       1 = Always,                 2 = When unfocused
  iGhostVisibility         = 0; // 0 = Never,                       1 = Always,                 2 = While service is running
  iStyle                   = 0; // 0 = SKIF Dark,                   1 = ImGui Dark,             2 = ImGui Light,                 3 = ImGui Classic
  iDimCovers               = 0; // 0 = Never,                       1 = Always,                 2 = On mouse hover
  iCheckForUpdates         = 1; // 0 = Never,                       1 = Weekly,                 2 = On each launch
  iAutoStopBehavior        = 1; // 0 = Never [not implemented],     1 = Stop on Injection,      2 = Stop on Game Exit
  iLogging                 = 4; // 0 = None,                        1 = Fatal,                  2 = Error,                       3 = Warning,                        4 = Info,       5 = Debug,       6 = Verbose
  iProcessSort             = 0; // 0 = Status,                      1 = PID,                    2 = Arch,                        3 = Admin,                          4 = Name
  iProcessRefreshInterval  = 2; // 0 = Paused,                      1 = Slow (5s),              2 = Normal (1s),                [3 = High (0.5s; not implemented)]
  iHDRMode                 = 1; // 0 = Never,                       1 = Always,                 2 = On mouse hover
  iHDRBrightness           = 203;

  // Default settings (booleans)
  bRememberLastSelected    = false;
  bDisableDPIScaling       = false;
  bDisableTooltips         = false;
  bDisableStatusBar        = false;
  bDisableBorders          =  true; // default to true
  bDisableSteamLibrary     = false;
  bDisableEGSLibrary       = false;
  bDisableGOGLibrary       = false;
  bDisableXboxLibrary      = false;
  bSmallMode               = false;
  bFirstLaunch             = false;
  bEnableDebugMode         = false;
  bAllowMultipleInstances  = false;
  bAllowBackgroundService  = false;
  bOpenAtCursorPosition    = false;
  bStopOnInjection         = false;
  bCloseToTray             = false;
  bLowBandwidthMode        = false;
  bPreferGOGGalaxyLaunch   = false;
  bMinimizeOnGameLaunch    = false;
  bDisableVSYNC            = false;
  bDisableCFAWarning       = false; // Controlled Folder Access warning
  bProcessSortAscending    = true;
  bProcessIncludeAll       = false;

  
  iProcessSort             =   regKVProcessSort.getData             ( );
  if (regKVProcessIncludeAll.hasData())
    bProcessIncludeAll     =   regKVProcessIncludeAll.getData       ( );
  if (regKVProcessSortAscending.hasData())
    bProcessSortAscending  =   regKVProcessSortAscending.getData    ( );
  if (regKVProcessRefreshInterval.hasData())
    iProcessRefreshInterval=   regKVProcessRefreshInterval.getData    ( );

  bLowBandwidthMode        =   regKVLowBandwidthMode.getData        ( );
  bPreferGOGGalaxyLaunch   =   regKVPreferGOGGalaxyLaunch.getData   ( );

  bDisableDPIScaling       =   regKVDisableDPIScaling.getData       ( );
  bDisableTooltips         =   regKVDisableTooltips.getData         ( );
  bDisableStatusBar        =   regKVDisableStatusBar.getData        ( );
  bDisableSteamLibrary     =   regKVDisableSteamLibrary.getData     ( );
  bDisableEGSLibrary       =   regKVDisableEGSLibrary.getData       ( );
  bDisableGOGLibrary       =   regKVDisableGOGLibrary.getData       ( );

  if (regKVDisableXboxLibrary.hasData())
    bDisableXboxLibrary    =   regKVDisableXboxLibrary.getData      ( );

  bEnableDebugMode         =   regKVEnableDebugMode.getData         ( );
  bSmallMode               =   regKVSmallMode.getData               ( );
  bFirstLaunch             =   regKVFirstLaunch.getData             ( );
  bAllowMultipleInstances  =   regKVAllowMultipleInstances.getData  ( );
  bAllowBackgroundService  =   regKVAllowBackgroundService.getData  ( );

  if (regKVHDRMode.hasData())
    iHDRMode               =   regKVHDRMode.getData                 ( );
  if (regKVHDRBrightness.hasData())
    iHDRBrightness         =   regKVHDRBrightness.getData           ( );

  bDisableVSYNC            =   regKVDisableVSYNC.getData            ( );
  bDisableCFAWarning       =   regKVDisableCFAWarning.getData       ( );
  bOpenAtCursorPosition    =   regKVOpenAtCursorPosition.getData    ( );
  
  // If the legacy key has data, but not the new key, move the data over to respect existing user's choices
  if (!regKVDisableStopOnInjection.hasData() && regKVLegacyDisableStopOnInjection.hasData())
    regKVDisableStopOnInjection.putData (regKVLegacyDisableStopOnInjection.getData());

  bStopOnInjection         = ! regKVDisableStopOnInjection.getData  ( );
  bMinimizeOnGameLaunch    =   regKVMinimizeOnGameLaunch.getData    ( );
  bCloseToTray             =   regKVCloseToTray.getData             ( );

  if (regKVDisableBorders.hasData())
    bDisableBorders        =   regKVDisableBorders.getData          ( );

  if (regKVAutoStopBehavior.hasData())
    iAutoStopBehavior      =   regKVAutoStopBehavior.getData        ( );

  if (regKVNotifications.hasData())
    iNotifications         =   regKVNotifications.getData           ( );

  if (regKVGhostVisibility.hasData())
    iGhostVisibility       =   regKVGhostVisibility.getData         ( );

  if (regKVStyle.hasData())
    iStyle                 =   regKVStyle.getData                   ( );

  if (regKVLogging.hasData())
    iLogging               =   regKVLogging.getData                 ( );

  if (regKVDimCovers.hasData())
    iDimCovers             =   regKVDimCovers.getData               ( );

  if (regKVCheckForUpdates.hasData())
    iCheckForUpdates       =   regKVCheckForUpdates.getData         ( );

  if (regKVIgnoreUpdate.hasData())
    wsIgnoreUpdate              =   regKVIgnoreUpdate.getWideString      ( );

  if (regKVFollowUpdateChannel.hasData())
    wsUpdateChannel             = regKVFollowUpdateChannel.getWideString ( );

  /*
  if (bRememberLastSelected && regKVLastSelected.hasData())
    iLastSelected          =   regKVLastSelected.getData            ( );

  iLastSelectedStored = iLastSelected;
  */
  
  // Remember Last Selected Game
  const int STEAM_APPID = 1157970;
  iLastSelectedGame   = STEAM_APPID; // Default selected game
  wsLastSelectedStore = L"Steam";         // Default selected store

  if (regKVRememberLastSelected.hasData())
    bRememberLastSelected  =   regKVRememberLastSelected.getData    ( );

  if (bRememberLastSelected)
  {
    if (regKVLastSelectedGame.hasData())
      iLastSelectedGame         =   regKVLastSelectedGame.getData        ( );

    if (regKVLastSelectedStore.hasData())
      wsLastSelectedStore       =   regKVLastSelectedStore.getWideString ( );
  }

  // App registration
  if (regKVAppRegistration.hasData())
    wsAppRegistration           = regKVAppRegistration.getWideString ( );

  if (regKVPath.hasData())
    wsPath                      = regKVPath.getWideString ( );
}
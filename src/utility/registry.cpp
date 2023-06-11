#include <utility/registry.h>

extern bool SKIF_Util_IsWindows10OrGreater      (void);
extern bool SKIF_Util_IsWindows10v1709OrGreater (void);

template<class _Tp>
bool
SKIF_RegistrySettings::KeyValue<_Tp>::hasData (void)
{
  _Tp   out = _Tp ( );
  DWORD dwOutLen;

  auto type_idx =
    std::type_index (typeid (_Tp));;

  if ( type_idx == std::type_index (typeid (std::wstring)) )
  {
    _desc.dwFlags  = RRF_RT_REG_SZ;
    _desc.dwType   = REG_SZ;

    // Two null terminators are stored at the end of REG_SZ, so account for those
    return (_SizeOfData ( ) > 4);
  }

  if ( type_idx == std::type_index (typeid (bool)) )
  {
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (bool);
  }

  if ( type_idx == std::type_index (typeid (int)) )
  {
    _desc.dwType   = REG_DWORD;
          dwOutLen = sizeof (int);
  }

  if ( type_idx == std::type_index (typeid (float)) )
  {
    _desc.dwFlags  = RRF_RT_REG_BINARY;
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (float);
  }

  if ( ERROR_SUCCESS == _GetValue (&out, &dwOutLen) )
    return true;

  return false;
};

template<class _Tp>
_Tp
SKIF_RegistrySettings::KeyValue<_Tp>::getData (void)
{
  _Tp   out = _Tp ( );
  DWORD dwOutLen;

  auto type_idx =
    std::type_index (typeid (_Tp));

  if ( type_idx == std::type_index (typeid (std::wstring)) )
  {
    _desc.dwFlags  = RRF_RT_REG_SZ;
    _desc.dwType   = REG_SZ;
          dwOutLen = _SizeOfData ( );

    std::wstring _out(dwOutLen, '\0');

    if ( ERROR_SUCCESS != 
      RegGetValueW ( _desc.hKey,
                        _desc.wszSubKey,
                          _desc.wszKeyValue,
                          _desc.dwFlags,
                            &_desc.dwType,
                              _out.data(), &dwOutLen)) out = _Tp ();
    
    // Strip null terminators
    //_out.erase (std::find (_out.begin(), _out.end(), '\0'), _out.end());

    // Convert std::wstring to _Tp
    // TODO: Doesn't work properly (ends at the first space)
    _Tp tmp = _Tp();
    std::wstringstream wss(_out);
    while (wss >> std::noskipws >> tmp)
    {
      out = out + tmp;
    }

    return out;
  }

  if ( type_idx == std::type_index (typeid (bool)) )
  {
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (bool);
  }

  if ( type_idx == std::type_index (typeid (int)) )
  {
    _desc.dwType   = REG_DWORD;
          dwOutLen = sizeof (int);
  }

  if ( type_idx == std::type_index (typeid (float)) )
  {
    _desc.dwFlags  = RRF_RT_REG_BINARY;
    _desc.dwType   = REG_BINARY;
          dwOutLen = sizeof (float);
  }

  if ( ERROR_SUCCESS !=
          _GetValue (&out, &dwOutLen) ) out = _Tp ();

  return out;
};

template<class _Tp>
std::wstring
SKIF_RegistrySettings::KeyValue<_Tp>::getWideString (void)
{
  auto type_idx =
    std::type_index (typeid (_Tp));

  if ( type_idx == std::type_index (typeid (std::wstring)) )
  {
    _desc.dwFlags  = RRF_RT_REG_SZ;
    _desc.dwType   = REG_SZ;
    DWORD dwOutLen = _SizeOfData ( );

    std::wstring out(dwOutLen, '\0');

    if ( ERROR_SUCCESS != 
      RegGetValueW ( _desc.hKey,
                        _desc.wszSubKey,
                          _desc.wszKeyValue,
                          _desc.dwFlags,
                            &_desc.dwType,
                              out.data(), &dwOutLen)) return std::wstring();

    // Strip null terminators
    out.erase (std::find (out.begin(), out.end(), '\0'), out.end());

    return out;
  }

  return std::wstring();
};


template<class _Tp>
SKIF_RegistrySettings::KeyValue<_Tp>
SKIF_RegistrySettings::KeyValue<_Tp>::MakeKeyValue (const wchar_t* wszSubKey, const wchar_t* wszKeyValue, HKEY hKey, LPDWORD pdwType, DWORD dwFlags)
{
  KeyValue <_Tp> kv;

  wcsncpy_s ( kv._desc.wszSubKey,  MAX_PATH,
                        wszSubKey, _TRUNCATE );

  wcsncpy_s ( kv._desc.wszKeyValue,  MAX_PATH,
                        wszKeyValue, _TRUNCATE );

  kv._desc.hKey    = hKey;
  kv._desc.dwType  = ( pdwType != nullptr ) ?
                                    *pdwType : REG_NONE;
  kv._desc.dwFlags = dwFlags;

  return kv;
};


SKIF_RegistrySettings::SKIF_RegistrySettings (void)
{
  // iSDRMode defaults to 0, meaning 8 bpc (DXGI_FORMAT_R8G8B8A8_UNORM) 
  // but it seems that Windows 10 1709+ (Build 16299) also supports
  // 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model.
  if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    iSDRMode = 1; // Default to 10 bpc on Win10 1709+

  // iUIMode defaults to 1 on Win7 and 8.1, but 2 on 10+
  if (SKIF_Util_IsWindows10OrGreater ( ))
    iUIMode  = 2;
  
  iProcessSort             =   regKVProcessSort            .getData ( );
  if (regKVProcessIncludeAll   .hasData())
    bProcessIncludeAll     =   regKVProcessIncludeAll      .getData ( );
  if (regKVProcessSortAscending.hasData())
    bProcessSortAscending  =   regKVProcessSortAscending   .getData ( );
  if (regKVProcessRefreshInterval.hasData())
    iProcessRefreshInterval=   regKVProcessRefreshInterval .getData ( );

  bLibraryIgnoreArticles   =   regKVLibraryIgnoreArticles  .getData ( );

  bLowBandwidthMode        =   regKVLowBandwidthMode       .getData ( );
  bPreferGOGGalaxyLaunch   =   regKVPreferGOGGalaxyLaunch  .getData ( );

  bDisableDPIScaling       =   regKVDisableDPIScaling      .getData ( );
  bDisableTooltips         =   regKVDisableTooltips        .getData ( );
  bDisableStatusBar        =   regKVDisableStatusBar       .getData ( );
  bDisableSteamLibrary     =   regKVDisableSteamLibrary    .getData ( );
  bDisableEGSLibrary       =   regKVDisableEGSLibrary      .getData ( );
  bDisableGOGLibrary       =   regKVDisableGOGLibrary      .getData ( );

  if (regKVDisableXboxLibrary.hasData())
    bDisableXboxLibrary    =   regKVDisableXboxLibrary     .getData ( );

  bEnableDebugMode         =   regKVEnableDebugMode        .getData ( );
  bSmallMode               =   regKVSmallMode              .getData ( );
  bFirstLaunch             =   regKVFirstLaunch            .getData ( );
  bAllowMultipleInstances  =   regKVAllowMultipleInstances .getData ( );
  bAllowBackgroundService  =   regKVAllowBackgroundService .getData ( );
  
  if (regKVSDRMode.hasData())
    iSDRMode               =   regKVSDRMode                .getData ( );

  if (regKVHDRMode.hasData())
    iHDRMode               =   regKVHDRMode                .getData ( );
  if (regKVHDRBrightness.hasData())
    iHDRBrightness         =   regKVHDRBrightness          .getData ( );
  
  if (regKVUIMode.hasData())
    iUIMode                =   regKVUIMode                 .getData ( );

  bDisableCFAWarning       =   regKVDisableCFAWarning      .getData ( );
  bOpenAtCursorPosition    =   regKVOpenAtCursorPosition   .getData ( );
  
  /* 2023-05-06: Disabled as it probably does not serve any purpose any longer
  // If the legacy key has data, but not the new key, move the data over to respect existing user's choices
  if (!regKVDisableStopOnInjection.hasData() && regKVLegacyDisableStopOnInjection.hasData())
    regKVDisableStopOnInjection.putData (regKVLegacyDisableStopOnInjection.getData());
  */

  bStopOnInjection         = ! regKVDisableStopOnInjection .getData ( );
  bMinimizeOnGameLaunch    =   regKVMinimizeOnGameLaunch   .getData ( );
  bCloseToTray             =   regKVCloseToTray            .getData ( );

  // Do not allow AllowMultipleInstances and CloseToTray at the same time
  if (  bAllowMultipleInstances && bCloseToTray)
  {     bAllowMultipleInstances = false;
    regKVAllowMultipleInstances .putData (bAllowMultipleInstances);
  }

  if (regKVDisableBorders.hasData())
    bDisableBorders        =   regKVDisableBorders         .getData ( );

  if (regKVAutoStopBehavior.hasData())
    iAutoStopBehavior      =   regKVAutoStopBehavior       .getData ( );

  if (regKVNotifications.hasData())
    iNotifications         =   regKVNotifications          .getData ( );

  if (regKVGhostVisibility.hasData())
    iGhostVisibility       =   regKVGhostVisibility        .getData ( );

  if (regKVStyle.hasData())
    iStyle                 =   regKVStyle                  .getData ( );

  if (regKVLogging.hasData())
    iLogging               =   regKVLogging                .getData ( );

  if (regKVDimCovers.hasData())
    iDimCovers             =   regKVDimCovers              .getData ( );

  if (regKVCheckForUpdates.hasData())
    iCheckForUpdates       =   regKVCheckForUpdates        .getData ( );

  if (regKVIgnoreUpdate.hasData())
    wsIgnoreUpdate         =   regKVIgnoreUpdate           .getWideString ( );

  if (regKVUpdateChannel.hasData())
    wsUpdateChannel        =   regKVUpdateChannel          .getWideString ( );
  
  // Remember Last Selected Game
  const int STEAM_APPID = 1157970;
  iLastSelectedGame     = STEAM_APPID; // Default selected game
  wsLastSelectedStore   = L"Steam";    // Default selected store

  if (regKVRememberLastSelected.hasData())
    bRememberLastSelected  =   regKVRememberLastSelected   .getData ( );

  if (bRememberLastSelected)
  {
    if (regKVLastSelectedGame.hasData())
      iLastSelectedGame         =   regKVLastSelectedGame  .getData ( );

    if (regKVLastSelectedStore.hasData())
      wsLastSelectedStore       =   regKVLastSelectedStore .getWideString ( );
  }

  // App registration
  if (regKVAppRegistration.hasData())
    wsAppRegistration           = regKVAppRegistration     .getWideString ( );

  if (regKVPath.hasData())
    wsPath                      = regKVPath                .getWideString ( );
}
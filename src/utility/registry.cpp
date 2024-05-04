#include <utility/registry.h>
#include <algorithm>
#include <utility/sk_utility.h>
#include <utility/utility.h>

extern bool SKIF_Util_IsWindows10OrGreater      (void);
extern bool SKIF_Util_IsWindows10v1709OrGreater (void);

template<class _Tp>
bool
SKIF_RegistrySettings::KeyValue<_Tp>::hasData (HKEY* hKey)
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
    return (_SizeOfData (hKey) > 4);
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

  if ( ERROR_SUCCESS == _GetValue (&out, &dwOutLen, hKey) )
    return true;

  return false;
}

std::vector <std::wstring>
SKIF_RegistrySettings::KeyValue<std::vector <std::wstring>>::getData (HKEY* hKey)
{
  _desc.dwFlags  = RRF_RT_REG_MULTI_SZ;
  _desc.dwType   = REG_MULTI_SZ;
  DWORD dwOutLen = _SizeOfData (hKey);

  std::wstring out(dwOutLen, '\0');

  if ( ERROR_SUCCESS != 
    RegGetValueW ( (hKey != nullptr) ? *hKey : _desc.hKey,
                   (hKey != nullptr) ?  NULL : _desc.wszSubKey,
                        _desc.wszKeyValue,
                        _desc.dwFlags,
                          &_desc.dwType,
                            out.data(), &dwOutLen)) return std::vector <std::wstring>();

  std::vector <std::wstring> vector;

  const wchar_t* currentItem = (const wchar_t*)out.data();

  // Parse the given wstring into a vector
  while (*currentItem)
  {
    vector.push_back (currentItem);
    currentItem = currentItem + _tcslen(currentItem) + 1;
  }

  /*
  // Strip null terminators
  for (auto& item : vector)
  {
    item.erase (std::find (item.begin(), item.end(), '\0'), item.end());
    OutputDebugStringW (L"Found: ");
    OutputDebugStringW (item.c_str());
    OutputDebugStringW (L"\n");
  }
  */

  return vector;
}

std::wstring
SKIF_RegistrySettings::KeyValue<std::wstring>::getData (HKEY* hKey)
{
  _desc.dwFlags  = RRF_RT_REG_SZ;
  _desc.dwType   = REG_SZ;
  DWORD dwOutLen = _SizeOfData (hKey);

  std::wstring out(dwOutLen, '\0');

  if ( ERROR_SUCCESS != 
    RegGetValueW ( (hKey != nullptr) ? *hKey : _desc.hKey,
                   (hKey != nullptr) ?  NULL : _desc.wszSubKey,
                        _desc.wszKeyValue,
                        _desc.dwFlags,
                          &_desc.dwType,
                            out.data(), &dwOutLen)) return std::wstring();

  // Strip null terminators
  out.erase (std::find (out.begin(), out.end(), '\0'), out.end());

  return out;
}

bool
SKIF_RegistrySettings::KeyValue<std::vector <std::wstring>>::putDataMultiSZ (std::vector<std::wstring> _in)
{
  LSTATUS lStat         = STATUS_INVALID_DISPOSITION;
  HKEY    hKeyToSet     = 0;
  DWORD   dwDisposition = 0;
  size_t  stDataSize    = 0;

  lStat =
    RegCreateKeyExW (
      _desc.hKey,
        _desc.wszSubKey,
          0x00, nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, nullptr,
              &hKeyToSet, &dwDisposition );

  _desc.dwType     = REG_MULTI_SZ;

  std::wstring wzData;

  // Serialize into std::wstring
  for (const auto& item : _in)
  {
    wzData     += item + L'\0';
    stDataSize += item.length ( ) + 1;
  }

  wzData    += L'\0';
  stDataSize++;

  lStat =
    RegSetKeyValueW ( hKeyToSet,
                        nullptr,
                        _desc.wszKeyValue,
                        _desc.dwType,
                  (LPBYTE) wzData.data ( ), (DWORD) stDataSize * sizeof(wchar_t));
            
  RegCloseKey (hKeyToSet);

  return (ERROR_SUCCESS == lStat);
}

template<class _Tp>
_Tp
SKIF_RegistrySettings::KeyValue<_Tp>::getData (HKEY* hKey)
{
  _Tp   out = _Tp ( );
  DWORD dwOutLen;

  auto type_idx =
    std::type_index (typeid (_Tp));

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
          _GetValue (&out, &dwOutLen, hKey) ) out = _Tp ();

  return out;
}

template<class _Tp>
SKIF_RegistrySettings::KeyValue<_Tp>
SKIF_RegistrySettings::KeyValue<_Tp>::MakeKeyValue (const wchar_t* wszSubKey, const wchar_t* wszKeyValue, HKEY hKey, LPDWORD pdwType, DWORD dwFlags)
{
  KeyValue <_Tp> kv;

  wcsncpy_s ( kv._desc.wszSubKey,   MAX_PATH,
                        wszSubKey, _TRUNCATE );

  wcsncpy_s ( kv._desc.wszKeyValue,   MAX_PATH,
                        wszKeyValue, _TRUNCATE );

  kv._desc.hKey    = hKey;
  kv._desc.dwType  = ( pdwType != nullptr ) ?
                                    *pdwType : REG_NONE;
  kv._desc.dwFlags = dwFlags;

  return kv;
}


std::vector <SKIF_RegistrySettings::category_s>
SKIF_RegistrySettings::SortCategories (std::vector <SKIF_RegistrySettings::category_s>& categories)
{
  category_s favorites = { "Favorites", false },
                 games = { "Games",     false };
  std::vector <category_s> _new;

  // Move all exiting items over to a new vector
  for (auto& category : categories)
  {
    if (! category.name.empty())
    {
      if (category.name == "Favorites")
        favorites = category;
      else if (category.name == "Games")
        games     = category;
      else
        _new.push_back (category);
    }
  }

  // Sort the new vector in alphabetical order (without Favorites or Games added to it)
  std::stable_sort (_new.begin (),
                    _new.end   (),
    []( const SKIF_RegistrySettings::category_s& a,
        const SKIF_RegistrySettings::category_s& b ) -> int
    {
      return a.name.compare(b.name) < 0;
    }
  );

  // Add the favorites and games categories
  _new.insert    (_new.begin(), favorites);
  _new.push_back (games);

  // Return the new reordered vector
  return _new;
}

bool
SKIF_RegistrySettings::isDevLogging (void) const
{
  return (bLoggingDeveloper && bDeveloperMode && iLogging >= 6);
}

SKIF_RegistrySettings::SKIF_RegistrySettings (void)
{
  // iSDRMode defaults to 0, meaning 8 bpc (DXGI_FORMAT_R8G8B8A8_UNORM) 
  // but it seems that Windows 10 1709+ (Build 16299) also supports
  // 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model.
  if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    iSDRMode               =   1; // Default to 10 bpc on Win10 1709+

  // iUIMode defaults to 1 on Win7 and 8.1, but 2 on 10+
  if (SKIF_Util_IsWindows10OrGreater ( ))
    iUIMode                =   2;

  HKEY hKey = nullptr;
  
  LSTATUS lsKey = RegCreateKeyW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\)", &hKey);

  if (lsKey != ERROR_SUCCESS)
    hKey = nullptr;
  
  iProcessSort             =   regKVProcessSort            .getData (&hKey);
  if (regKVProcessIncludeAll   .hasData(&hKey))
    bProcessIncludeAll     =   regKVProcessIncludeAll      .getData (&hKey);
  if (regKVProcessSortAscending.hasData(&hKey))
    bProcessSortAscending  =   regKVProcessSortAscending   .getData (&hKey);
  if (regKVProcessRefreshInterval.hasData(&hKey))
    iProcessRefreshInterval=   regKVProcessRefreshInterval .getData (&hKey);

  bLibraryIgnoreArticles   =   regKVLibraryIgnoreArticles  .getData (&hKey);

  bLowBandwidthMode        =   regKVLowBandwidthMode       .getData (&hKey);
  bInstantPlayGOG          =   regKVInstantPlayGOG         .getData (&hKey);
  bInstantPlaySteam        =   regKVInstantPlaySteam       .getData (&hKey);
  
  // UI elements that can be toggled

  if (regKVUIBorders.hasData(&hKey))
    bUIBorders             =   regKVUIBorders              .getData (&hKey);
  if (regKVUITooltips.hasData(&hKey))
    bUITooltips            =   regKVUITooltips             .getData (&hKey);
  if (regKVUIStatusBar.hasData(&hKey))
    bUIStatusBar           =   regKVUIStatusBar            .getData (&hKey);
  if (regKVDPIScaling.hasData(&hKey))
    bDPIScaling            =   regKVDPIScaling             .getData (&hKey);
  if (regKVWin11Corners.hasData(&hKey))
    bWin11Corners          =   regKVWin11Corners           .getData (&hKey);
  if (regKVUILargeIcons.hasData(&hKey))
    bUILargeIcons          =   regKVUILargeIcons           .getData (&hKey);
  if (regKVTouchInput.hasData(&hKey))
    bTouchInput            =   regKVTouchInput             .getData (&hKey);

  // Store libraries

  iLibrarySort             =   regKVLibrarySort            .getData (&hKey);
  
  if (regKVLibrarySteam.hasData(&hKey))
    bLibrarySteam          =   regKVLibrarySteam           .getData (&hKey);

  if (regKVLibraryEpic.hasData(&hKey))
    bLibraryEpic           =   regKVLibraryEpic            .getData (&hKey);

  if (regKVLibraryGOG.hasData(&hKey))
    bLibraryGOG            =   regKVLibraryGOG             .getData (&hKey);

  if (regKVLibraryXbox.hasData(&hKey))
    bLibraryXbox           =   regKVLibraryXbox            .getData (&hKey);

  if (regKVLibraryCustom.hasData(&hKey))
    bLibraryCustom         =   regKVLibraryCustom          .getData (&hKey);

  uiSteamUser              =   regKVSteamUser              .getData (&hKey);

//bServiceMode             =   regKVServiceMode            .getData (&hKey);
  bHorizonMode             =   regKVHorizonMode            .getData (&hKey);

  if (regKVHorizonModeAuto.hasData (&hKey))
    bHorizonModeAuto       =   regKVHorizonModeAuto        .getData (&hKey);

  bServiceMode = bOpenInServiceMode = regKVOpenInServiceMode.getData(&hKey);
  bFirstLaunch             =   regKVFirstLaunch            .getData (&hKey);
  bAllowMultipleInstances  =   regKVAllowMultipleInstances .getData (&hKey);
  bAllowBackgroundService  =   regKVAllowBackgroundService .getData (&hKey);
  bAutoUpdate              =   regKVAutoUpdate             .getData (&hKey);
  
  if (regKVSDRMode.hasData(&hKey))
    iSDRMode               =   regKVSDRMode                .getData (&hKey);

  if (regKVHDRMode.hasData(&hKey))
    iHDRMode               =   regKVHDRMode                .getData (&hKey);
  if (regKVHDRBrightness.hasData(&hKey))
  {
    iHDRBrightness         =   regKVHDRBrightness          .getData (&hKey);
    
    // Reset to 203 nits (the default) if outside of the acceptable range of 80-400 nits
    if (iHDRBrightness < 80 || 400 < iHDRBrightness)
      iHDRBrightness       =   203;
  }
  
  if (regKVUIMode.hasData(&hKey))
    iUIMode                =   regKVUIMode                 .getData (&hKey);
  
  if (regKVDiagnostics.hasData(&hKey))
    iDiagnostics           =   regKVDiagnostics            .getData (&hKey);

  bDisableCFAWarning       =   regKVDisableCFAWarning      .getData (&hKey);
  bOpenAtCursorPosition    =   regKVOpenAtCursorPosition   .getData (&hKey);
  bStopOnInjection         = ! regKVDisableStopOnInjection .getData (&hKey);

  /*
  bMaximizeOnDoubleClick   = 
    SKIF_Util_GetDragFromMaximized ( )         // IF the OS prerequisites are enabled
    ? regKVMaximizeOnDoubleClick.hasData (&hKey)   // AND we have data in the registry
      ? regKVMaximizeOnDoubleClick.getData (&hKey) // THEN use the data,
      : false                                  // otherwise default to false,
    : false;                                   // and false if OS prerequisites are disabled
  */

  bMinimizeOnGameLaunch    =   regKVMinimizeOnGameLaunch   .getData (&hKey);
  bRestoreOnGameExit       =   regKVRestoreOnGameExit      .getData (&hKey);
  bCloseToTray             =   regKVCloseToTray            .getData (&hKey);

  // Do not allow AllowMultipleInstances and CloseToTray at the same time
  if (  bAllowMultipleInstances && bCloseToTray)
  {     bAllowMultipleInstances = false;
    regKVAllowMultipleInstances .putData (bAllowMultipleInstances);
  }

  if (regKVAutoStopBehavior.hasData(&hKey))
    iAutoStopBehavior      =   regKVAutoStopBehavior       .getData (&hKey);

  if (regKVNotifications.hasData(&hKey))
    iNotifications         =   regKVNotifications          .getData (&hKey);

  if (regKVGhostVisibility.hasData(&hKey))
    iGhostVisibility       =   regKVGhostVisibility        .getData (&hKey);

  if (regKVStyle.hasData(&hKey))
    iStyle  =  iStyleTemp  =   regKVStyle                  .getData (&hKey);

  if (regKVLogging.hasData(&hKey))
    iLogging               =   regKVLogging                .getData (&hKey);

  if (regKVDimCovers.hasData(&hKey))
    iDimCovers             =   regKVDimCovers              .getData (&hKey);

  if (regKVCheckForUpdates.hasData(&hKey))
    iCheckForUpdates       =   regKVCheckForUpdates        .getData (&hKey);

  if (regKVIgnoreUpdate.hasData(&hKey))
    wsIgnoreUpdate         =   regKVIgnoreUpdate           .getData (&hKey);

  if (regKVUpdateChannel.hasData(&hKey))
    wsUpdateChannel        =   regKVUpdateChannel          .getData (&hKey);
  
  // Remember Last Selected Game
  const int STEAM_APPID    =   1157970;
  uiLastSelectedGame       =   STEAM_APPID; // Default selected game
  uiLastSelectedStore      =   0;

  if (regKVRememberLastSelected.hasData(&hKey))
    bRememberLastSelected  =   regKVRememberLastSelected   .getData (&hKey);

  if (regKVRememberCategoryState.hasData(&hKey))
    bRememberCategoryState =   regKVRememberCategoryState  .getData (&hKey);

  if (bRememberLastSelected)
  {
    if (regKVLastSelectedGame.hasData(&hKey))
      uiLastSelectedGame   =   regKVLastSelectedGame       .getData (&hKey);

    if (regKVLastSelectedStore.hasData(&hKey))
      uiLastSelectedStore  =   regKVLastSelectedStore      .getData (&hKey);
  }

  if (regKVPath.hasData(&hKey))
    wsPath                 =   regKVPath                   .getData (&hKey);

  if (regKVAutoUpdateVersion.hasData(&hKey))
    wsAutoUpdateVersion    =   regKVAutoUpdateVersion      .getData (&hKey);

  std::vector <std::wstring>
    mwzCategories          = regKVCategories               .getData (&hKey);

  std::vector <std::wstring>
    mwzCategoriesState     = regKVCategoriesState          .getData (&hKey);

  for (size_t i = 0; i < mwzCategories.size(); i++)
  {
    auto& wzCategory      = mwzCategories     [i];

    if (! wzCategory.empty() && wzCategory[0] != L'\0')
    {
      category_s category;
      category.name     = SK_WideCharToUTF8 (wzCategory);

      if (bRememberCategoryState && i < mwzCategoriesState.size())
        category.expanded = std::stoi (mwzCategoriesState[i]);

      vecCategories.push_back (category);
    }
  }

  // Sort categories in alphabetical order + add Favorites and Games
  vecCategories            =   SortCategories (vecCategories);

  bDeveloperMode           =   regKVDeveloperMode          .getData (&hKey);

  if (regKVEfficiencyMode.hasData(&hKey))
    bEfficiencyMode        =   regKVEfficiencyMode         .getData (&hKey);
  else
    bEfficiencyMode        =   SKIF_Util_IsWindows11orGreater ( ); // Win10 and below: false, Win11 and above: true
  
  if (regKVFadeCovers.hasData(&hKey))
    bFadeCovers            =   regKVFadeCovers             .getData (&hKey);

  if (regKVControllers.hasData(&hKey))
    bControllers           =   regKVControllers            .getData (&hKey);

  bLoggingDeveloper        =   regKVLoggingDeveloper       .getData (&hKey);

  // Warnings
  bWarningRTSS             =   regKVWarningRTSS            .getData (&hKey);

  if (hKey != nullptr)
    RegCloseKey (hKey);

  // Windows stuff

  // SKIFdrv install location
  if (regKVSKIFdrvLocation.hasData())
    wsSKIFdrvLocation      =   regKVSKIFdrvLocation        .getData ( );

  // App registration
  if (regKVAppRegistration.hasData())
    wsAppRegistration      =   regKVAppRegistration        .getData ( );

  // Notification duration
  if (regKVNotificationsDuration.hasData())
    iNotificationsDuration =   regKVNotificationsDuration  .getData ( );
  iNotificationsDuration *= 1000; // Convert from seconds to milliseconds
}
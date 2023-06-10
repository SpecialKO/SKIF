#pragma once
#include <string>
#include <windows.h>
#include <typeindex>
#include <sstream>

struct SKIF_RegistrySettings {

  // TODO: Rework this whole thing to not only hold a registry path but
  //       also hold the actual current value as well, allowing us to
  //       move away from ugly stuff like
  //  _registry.iLastSelectedGame = newValue;
  //  _registry.regKVLastSelectedGame.putData  (_registry.iLastSelectedGame);
  //       and instead do things like
  //  _registry.iLastSelectedGame.putData (newValue);
  //       and have it automatically get stored in the registry as well.

  template <class _Tp>
    class KeyValue
    {
      struct KeyDesc {
        HKEY         hKey                 = HKEY_CURRENT_USER;
        wchar_t    wszSubKey   [MAX_PATH] =               L"";
        wchar_t    wszKeyValue [MAX_PATH] =               L"";
        DWORD        dwType               =          REG_NONE;
        DWORD        dwFlags              =        RRF_RT_ANY;
      };

    public:
      bool hasData (void)
      {
        auto _GetValue =
        [&]( _Tp*     pVal,
               DWORD* pLen = nullptr ) ->
        LSTATUS
        {
          LSTATUS lStat =
            RegGetValueW ( _desc.hKey,
                             _desc.wszSubKey,
                               _desc.wszKeyValue,
                               _desc.dwFlags,
                                 &_desc.dwType,
                                   pVal, pLen );

          return lStat;
        };

        auto _SizeofData =
        [&](void) ->
        DWORD
        {
          DWORD len = 0;

          if ( ERROR_SUCCESS ==
                 _GetValue ( nullptr, &len )
             ) return len;

          return 0;
        };

        _Tp   out;
        DWORD dwOutLen;

        auto type_idx =
          std::type_index (typeid (_Tp));;

        if ( type_idx == std::type_index (typeid (std::wstring)) )
        {
          // Two null terminators are stored at the end of REG_SZ, so account for those
          return (_SizeofData() > 4);
        }

        if ( type_idx == std::type_index (typeid (bool)) )
        {
          _desc.dwType = REG_BINARY;
              dwOutLen = sizeof (bool);
        }

        if ( type_idx == std::type_index (typeid (int)) )
        {
          _desc.dwType = REG_DWORD;
              dwOutLen = sizeof (int);
        }

        if ( type_idx == std::type_index (typeid (float)) )
        {
          _desc.dwFlags = RRF_RT_REG_BINARY;
          _desc.dwType  = REG_BINARY;
               dwOutLen = sizeof (float);
        }

        if ( ERROR_SUCCESS == _GetValue (&out, &dwOutLen) )
          return true;

        return false;
      };

      std::wstring getWideString (void)
      {
        auto _GetValue =
        [&]( _Tp*     pVal,
               DWORD* pLen = nullptr ) ->
        LSTATUS
        {
          LSTATUS lStat =
            RegGetValueW ( _desc.hKey,
                             _desc.wszSubKey,
                               _desc.wszKeyValue,
                               _desc.dwFlags,
                                 &_desc.dwType,
                                   pVal, pLen );

          return lStat;
        };

        auto _SizeofData =
        [&](void) ->
        DWORD
        {
          DWORD len = 0;

          if ( ERROR_SUCCESS ==
                 _GetValue ( nullptr, &len )
             ) return len;

          return 0;
        };

        _Tp   out;
        DWORD dwOutLen = _SizeofData();
        std::wstring _out(dwOutLen, '\0');

        auto type_idx =
          std::type_index (typeid (_Tp));

        if ( type_idx == std::type_index (typeid (std::wstring)) )
        {
          _desc.dwFlags = RRF_RT_REG_SZ;
          _desc.dwType  = REG_SZ;

          if ( ERROR_SUCCESS != 
            RegGetValueW ( _desc.hKey,
                             _desc.wszSubKey,
                               _desc.wszKeyValue,
                               _desc.dwFlags,
                                 &_desc.dwType,
                                   _out.data(), &dwOutLen)) return std::wstring();

          // Strip null terminators
          _out.erase(std::find(_out.begin(), _out.end(), '\0'), _out.end());
        }

        return _out;
      };

      _Tp getData (void)
      {
        auto _GetValue =
        [&]( _Tp*     pVal,
               DWORD* pLen = nullptr ) ->
        LSTATUS
        {
          LSTATUS lStat =
            RegGetValueW ( _desc.hKey,
                             _desc.wszSubKey,
                               _desc.wszKeyValue,
                               _desc.dwFlags,
                                 &_desc.dwType,
                                   pVal, pLen );

          return lStat;
        };

        auto _SizeofData =
        [&](void) ->
        DWORD
        {
          DWORD len = 0;

          if ( ERROR_SUCCESS ==
                 _GetValue ( nullptr, &len )
             ) return len;

          return 0;
        };

        _Tp   out;
        DWORD dwOutLen;

        auto type_idx =
          std::type_index (typeid (_Tp));

        /*
        if ( type_idx == std::type_index (typeid (std::wstring)) )
        {
          dwOutLen = _SizeofData();
          std::wstring _out(dwOutLen, '\0');
          
          _desc.dwFlags = RRF_RT_REG_SZ;
          _desc.dwType  = REG_SZ;

          if ( ERROR_SUCCESS != 
            RegGetValueW ( _desc.hKey,
                             _desc.wszSubKey,
                               _desc.wszKeyValue,
                               _desc.dwFlags,
                                 &_desc.dwType,
                                   _out.data(), &dwOutLen)) return _Tp();

          // Strip null terminators
          //_out.erase(std::find(_out.begin(), _out.end(), '\0'), _out.end());

          // Convert std::wstring to _Tp
          std::wstringstream wss(_out);

          while (false)
          {
            _Tp tmp{};
            wss >> std::noskipws >> tmp;
            out = out + tmp;
          }

          return _Tp();
        }
        */

        if ( type_idx == std::type_index (typeid (bool)) )
        {
          _desc.dwType = REG_BINARY;
              dwOutLen = sizeof (bool);
        }

        if ( type_idx == std::type_index (typeid (int)) )
        {
          _desc.dwType = REG_DWORD;
              dwOutLen = sizeof (int);
        }

        if ( type_idx == std::type_index (typeid (float)) )
        {
          _desc.dwFlags = RRF_RT_REG_BINARY;
          _desc.dwType  = REG_BINARY;
               dwOutLen = sizeof (float);
        }

        if ( ERROR_SUCCESS !=
               _GetValue (&out, &dwOutLen) ) out = _Tp ();

        return out;
      };

      bool putData (_Tp in)
      {
        auto _SetValue =
        [&]( _Tp*    pVal,
             LPDWORD pLen = nullptr ) ->
        LSTATUS
        {
          LSTATUS lStat         = STATUS_INVALID_DISPOSITION;
          HKEY    hKeyToSet     = 0;
          DWORD   dwDisposition = 0;
          DWORD   dwDataSize    = 0;

          lStat =
            RegCreateKeyExW (
              _desc.hKey,
                _desc.wszSubKey,
                  0x00, nullptr,
                    REG_OPTION_NON_VOLATILE,
                    KEY_ALL_ACCESS, nullptr,
                      &hKeyToSet, &dwDisposition );

          auto type_idx =
            std::type_index (typeid (_Tp));

            //case std::type_index (std::wstring)
            //{
            //  auto& _out =
            //    dynamic_cast <std::wstring> (out);
            //
            //  _out.resize (_SizeofData () + 1);
            //
            //  if ( ERROR_SUCCESS !=
            //         _GetValue   (_out) ) out = _Tp ();
            //
            //  return out;
            //} break;

          if ( type_idx == std::type_index (typeid (std::wstring)) )
          {
            std::wstring _in = std::wstringstream(in).str();

            _desc.dwType     = REG_SZ;
                  dwDataSize = (DWORD) _in.size ( ) * sizeof(wchar_t);

            lStat =
              RegSetKeyValueW ( hKeyToSet,
                                  nullptr,
                                  _desc.wszKeyValue,
                                  _desc.dwType,
                           (LPBYTE) _in.data(), dwDataSize);
            
            RegCloseKey (hKeyToSet);

            return lStat;
          }

          if ( type_idx == std::type_index (typeid (bool)) )
          {
            _desc.dwType     = REG_BINARY;
                  dwDataSize = sizeof (bool);
          }

          if ( type_idx == std::type_index (typeid (int)) )
          {
            _desc.dwType     = REG_DWORD;
                  dwDataSize = sizeof (int);
          }

          if ( type_idx == std::type_index (typeid (float)) )
          {
            _desc.dwFlags    = RRF_RT_DWORD;
            _desc.dwType     = REG_BINARY;
                  dwDataSize = sizeof (float);
          }

          lStat =
            RegSetKeyValueW ( hKeyToSet,
                                nullptr,
                                _desc.wszKeyValue,
                                _desc.dwType,
                                  pVal, dwDataSize );

          RegCloseKey (hKeyToSet);

          UNREFERENCED_PARAMETER (pLen);

          return lStat;
        };

        if ( ERROR_SUCCESS == _SetValue (&in) )
          return true;

        return false;
      };

      static KeyValue <typename _Tp>
         MakeKeyValue ( const wchar_t *wszSubKey,
                        const wchar_t *wszKeyValue,
                        HKEY           hKey    = HKEY_CURRENT_USER,
                        LPDWORD        pdwType = nullptr,
                        DWORD          dwFlags = RRF_RT_ANY )
      {
        KeyValue <_Tp> kv;

        wcsncpy_s ( kv._desc.wszSubKey, MAX_PATH,
                             wszSubKey, _TRUNCATE );

        wcsncpy_s ( kv._desc.wszKeyValue, MAX_PATH,
                             wszKeyValue, _TRUNCATE );

        kv._desc.hKey    = hKey;
        kv._desc.dwType  = ( pdwType != nullptr ) ?
                                         *pdwType : REG_NONE;
        kv._desc.dwFlags = dwFlags;

        return kv;
      }

    protected:
    private:
      KeyDesc _desc;
  };

#define SKIF_MakeRegKeyF  KeyValue <float>       ::MakeKeyValue
#define SKIF_MakeRegKeyB  KeyValue <bool>        ::MakeKeyValue
#define SKIF_MakeRegKeyI  KeyValue <int>         ::MakeKeyValue
#define SKIF_MakeRegKeyWS KeyValue <std::wstring>::MakeKeyValue
  
  // Booleans

  // Changed name to avoid the forced behaviour change in SK that broke the
  //   intended user experience as well as launcher mode of SKIF in early 2022
  KeyValue <bool> regKVDisableStopOnInjection =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                        LR"(Disable Auto-Stop)" );

  // Retain legacy key, but only to allow users to disable it
  //KeyValue <bool> regKVLegacyDisableStopOnInjection =
  //  SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
  //                        LR"(Disable Stop On Injection)" );

  KeyValue <bool> regKVLowBandwidthMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Low Bandwidth Mode)" );

  KeyValue <bool> regKVPreferGOGGalaxyLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Prefer GOG Galaxy Launch)" );

  KeyValue <bool> regKVRememberLastSelected =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Remember Last Selected)" );

  KeyValue <bool> regKVDisableExitConfirmation =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Exit Confirmation)" );

  KeyValue <bool> regKVDisableDPIScaling =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable DPI Scaling)" );

  KeyValue <bool> regKVEnableDebugMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Enable Debug Mode)" );

  KeyValue <bool> regKVDisableTooltips =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Tooltips)" );

  KeyValue <bool> regKVDisableStatusBar =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Status Bar)" );

  KeyValue <bool> regKVDisableBorders =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable UI Borders)" );

  KeyValue <bool> regKVDisableSteamLibrary =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Steam Library)" );

  KeyValue <bool> regKVDisableEGSLibrary =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable EGS Library)" );

  KeyValue <bool> regKVDisableGOGLibrary =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable GOG Library)" );

  KeyValue <bool> regKVDisableXboxLibrary =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Xbox Library)" );

  KeyValue <bool> regKVSmallMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Small Mode)" );

  KeyValue <bool> regKVFirstLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(First Launch)" );

  KeyValue <bool> regKVAllowMultipleInstances =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Allow Multiple SKIF Instances)" );

  KeyValue <bool> regKVAllowBackgroundService =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Allow Background Service)" );

  KeyValue <bool> regKVDisableVSYNC =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable VSYNC)" );

  KeyValue <bool> regKVDisableCFAWarning =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable CFA Warning)" );

  KeyValue <bool> regKVOpenAtCursorPosition =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Open At Cursor Position)" );

  KeyValue <bool> regKVAlwaysShowGhost =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Always Show Ghost)" );

  KeyValue <bool> regKVMinimizeOnGameLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Minimize On Game Launch)" );

  KeyValue <bool> regKVCloseToTray =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Minimize To Notification Area On Close)" );

  KeyValue <bool> regKVProcessIncludeAll =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Process Include All)" );

  KeyValue <bool> regKVProcessSortAscending =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Process Sort Ascending)" );

  KeyValue <bool> regKVLibraryIgnoreArticles =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library Ignore Articles)" );

  // Integers

  KeyValue <int> regKVProcessRefreshInterval =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Process Refresh Interval)" );

  KeyValue <int> regKVProcessSort =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Process Sort)" );

  KeyValue <int> regKVLastSelectedGame =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Last Selected)" );

  KeyValue <int> regKVNotifications =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Notifications)" );

  KeyValue <int> regKVGhostVisibility =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Ghost Visibility)" );

  KeyValue <int> regKVStyle =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Style)" );

  KeyValue <int> regKVLogging =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Logging)" );

  KeyValue <int> regKVDimCovers =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Dim Covers)" );

  KeyValue <int> regKVCheckForUpdates =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Check For Updates)" );

  KeyValue <int> regKVAutoStopBehavior =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Auto-Stop Behavior)" );

  KeyValue <int> regKVSDRMode =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(SDR)" );

  KeyValue <int> regKVHDRMode =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(HDR)" );

  KeyValue <int> regKVHDRBrightness =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(HDR Brightness)" );

  KeyValue <int> regKVUIMode =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(UI Mode)" );

  // Wide Strings

  KeyValue <std::wstring> regKVLastSelectedStore =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                          LR"(Last Selected Store)" );

  KeyValue <std::wstring> regKVIgnoreUpdate =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Ignore Update)" );

  KeyValue <std::wstring> regKVFollowUpdateChannel =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Follow Update Channel)" );

  // App registration
  KeyValue <std::wstring> regKVAppRegistration =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\SKIF.exe)",
                         L"" );

  KeyValue <std::wstring> regKVPath =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Path)" );

  // Default settings (multiple options)
  int iNotifications           = 2;   // 0 = Never,                       1 = Always,                 2 = When unfocused
  int iGhostVisibility         = 0;   // 0 = Never,                       1 = Always,                 2 = While service is running
  int iStyle                   = 0;   // 0 = SKIF Dark,                   1 = ImGui Dark,             2 = ImGui Light,                 3 = ImGui Classic
  int iDimCovers               = 0;   // 0 = Never,                       1 = Always,                 2 = On mouse hover
  int iCheckForUpdates         = 1;   // 0 = Never,                       1 = Weekly,                 2 = On each launch
  int iAutoStopBehavior        = 1;   // 0 = Never [not implemented],     1 = Stop on Injection,      2 = Stop on Game Exit
  int iLogging                 = 4;   // 0 = None,                        1 = Fatal,                  2 = Error,                       3 = Warning,                        4 = Info,       5 = Debug,       6 = Verbose
  int iProcessSort             = 0;   // 0 = Status,                      1 = PID,                    2 = Arch,                        3 = Admin,                          4 = Name
  int iProcessRefreshInterval  = 2;   // 0 = Paused,                      1 = Slow (5s),              2 = Normal (1s),                [3 = High (0.5s; not implemented)]
  int iSDRMode                 = 0;   // 0 = 8 bpc,                       1 = 10 bpc,                 2 = 16 bpc
  int iHDRMode                 = 1;   // 0 = Disabled,                    1 = HDR10 (10 bpc),         2 = scRGB (16 bpc)
  int iHDRBrightness           = 203; // HDR reference white for BT.2408
  int iUIMode                  = 1;   // 0 = Safe Mode (BitBlt),          1 = Normal,                 2 = VRR Compatibility

  // Default settings (booleans)
  bool bRememberLastSelected    = false;
  bool bDisableDPIScaling       = false;
  bool bDisableTooltips         = false;
  bool bDisableStatusBar        = false;
  bool bDisableBorders          =  true; // default to true
  bool bDisableSteamLibrary     = false;
  bool bDisableEGSLibrary       = false;
  bool bDisableGOGLibrary       = false;
  bool bDisableXboxLibrary      = false;
  bool bSmallMode               = false;
  bool bFirstLaunch             = false;
  bool bEnableDebugMode         = false;
  bool bAllowMultipleInstances  = false;
  bool bAllowBackgroundService  = false;
  bool bOpenAtCursorPosition    = false;
  bool bStopOnInjection         = false;
  bool bCloseToTray             = false;
  bool bLowBandwidthMode        = false;
  bool bPreferGOGGalaxyLaunch   = false;
  bool bMinimizeOnGameLaunch    = false;
  //bool bDisableVSYNC            = false;
  bool bDisableCFAWarning       = false; // Controlled Folder Access warning
  bool bProcessSortAscending    = true;
  bool bProcessIncludeAll       = false;
  bool bLibraryIgnoreArticles   = false;
  
  // No default settings (misc)
  std::wstring wsUpdateChannel;
  std::wstring wsIgnoreUpdate;
  std::wstring wsAppRegistration;
  std::wstring wsPath;
  std::wstring wsLastSelectedStore;
  unsigned int iLastSelectedGame;

  // Ephemeral settings that doesn't stick around
  bool _LastSelectedWritten;
  bool _LoadedSteamOverlay = false;

  // Functions
  static SKIF_RegistrySettings& GetInstance (void)
  {
      static SKIF_RegistrySettings instance;
      return instance;
  }

  SKIF_RegistrySettings (SKIF_RegistrySettings const&) = delete; // Delete copy constructor
  SKIF_RegistrySettings (SKIF_RegistrySettings&&)      = delete; // Delete move constructor

private:
  SKIF_RegistrySettings (void);
};
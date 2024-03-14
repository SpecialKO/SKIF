#pragma once
#include <string>
#include <windows.h>
#include <typeindex>
#include <sstream>
#include <vector>



struct SKIF_RegistrySettings {

  // TODO: Rework this whole thing to not only hold a registry path but
  //       also hold the actual current value as well, allowing us to
  //       move away from ugly stuff like
  // 
  //  _registry.uiLastSelectedGame = newValue;
  //  _registry.regKVLastSelectedGame.putData  (_registry.uiLastSelectedGame);
  // 
  //       and instead do things like
  // 
  //  _registry.uiLastSelectedGame.putData (newValue);
  // 
  //       and have it automatically get stored in the registry as well.

  struct category_s {
    std::string name     = "";
    bool        expanded = false;
  };
  
  std::vector<SKIF_RegistrySettings::category_s>
    SortCategories (std::vector<SKIF_RegistrySettings::category_s>& categories);

  template <class _Tp>
    class KeyValue
    {
      struct KeyDesc {
        HKEY         hKey                 = HKEY_CURRENT_USER;
        wchar_t    wszSubKey   [MAX_PATH] =               { };
        wchar_t    wszKeyValue [MAX_PATH] =               { };
        DWORD        dwType               =          REG_NONE;
        DWORD        dwFlags              =        RRF_RT_ANY;
      };

    public:
      bool         hasData        (HKEY* hKey = nullptr);
      _Tp          getData        (HKEY* hKey = nullptr);
      bool         putDataMultiSZ (std::vector<std::wstring> in);
      bool         putData        (_Tp in)
      {
        if ( ERROR_SUCCESS == _SetValue (&in) )
          return true;

        return false;
      };

      static KeyValue <typename _Tp>
         MakeKeyValue ( const wchar_t *wszSubKey,
                        const wchar_t *wszKeyValue,
                        HKEY           hKey    = HKEY_CURRENT_USER,
                        LPDWORD        pdwType = nullptr,
                        DWORD          dwFlags = RRF_RT_ANY );

    protected:
    private:
      KeyDesc _desc;
      
      LSTATUS _SetValue (_Tp * pVal)
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

        if ( type_idx == std::type_index (typeid (std::wstring)) )
        {
          std::wstring _in = std::wstringstream(*pVal).str();

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

        return lStat;
      };
      
      LSTATUS _GetValue (_Tp* pVal, DWORD* pLen = nullptr, HKEY* hKey = nullptr)
      {
        LSTATUS lStat =
          RegGetValueW ( (hKey != nullptr) ? *hKey : _desc.hKey,
                         (hKey != nullptr) ?  NULL : _desc.wszSubKey,
                              _desc.wszKeyValue,
                              _desc.dwFlags,
                                &_desc.dwType,
                                  pVal, pLen );

        return lStat;
      };

      DWORD _SizeOfData (HKEY* hKey = nullptr)
      {
        DWORD len = 0;

        if ( ERROR_SUCCESS ==
                _GetValue ( nullptr, &len, hKey)
            ) return len;

        return 0;
      };
  };

#define SKIF_MakeRegKeyF   KeyValue <float>       ::MakeKeyValue
#define SKIF_MakeRegKeyB   KeyValue <bool>        ::MakeKeyValue
#define SKIF_MakeRegKeyI   KeyValue <int>         ::MakeKeyValue
#define SKIF_MakeRegKeyWS  KeyValue <std::wstring>::MakeKeyValue
#define SKIF_MakeRegKeyVEC KeyValue <std::vector <std::wstring>>::MakeKeyValue
  
  // Booleans

  // Changed name to avoid the forced behaviour change in SK that broke the
  //   intended user experience as well as launcher mode of SKIF in early 2022
  KeyValue <bool> regKVDisableStopOnInjection =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                        LR"(Disable Auto-Stop)" );

  KeyValue <bool> regKVLowBandwidthMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Low Bandwidth Mode)" );

  KeyValue <bool> regKVInstantPlayGOG =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Instant Play GOG)" );

  KeyValue <bool> regKVInstantPlaySteam =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Instant Play Steam)" );

  KeyValue <bool> regKVRememberLastSelected =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Remember Last Selected)" );

  KeyValue <bool> regKVDisableExitConfirmation =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable Exit Confirmation)" );

  // UI elements that can be toggled

  KeyValue <bool> regKVUIBorders =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Borders)" );

  KeyValue <bool> regKVUITooltips =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Tooltips)" );

  KeyValue <bool> regKVUIStatusBar =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Status Bar)" );

  KeyValue <bool> regKVDPIScaling =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(DPI Scaling)" );

  KeyValue <bool> regKVWin11Corners =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Win11 Corners)" );

  KeyValue <bool> regKVUILargeIcons =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(UI Large Icons)" );

  KeyValue <bool> regKVTouchInput =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(UI Touch Input)" );

  // Store libraries

  KeyValue <bool> regKVLibrarySteam =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library Steam)" );

  KeyValue <bool> regKVLibraryEpic =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library Epic)" );

  KeyValue <bool> regKVLibraryGOG =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library GOG)" );

  KeyValue <bool> regKVLibraryXbox =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library Xbox)" );

  KeyValue <bool> regKVLibraryCustom =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library Custom)" );

// 2023-07-31: Disabled since I believe this isn't actually used much,
//               and the intention is to eventually have cmd line args
//                 trigger service mode automatically when interacting
//                   with the service on launch.
//KeyValue <bool> regKVServiceMode =
//  SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
//                       LR"(Service Mode)" ); // Small Mode

  KeyValue <bool> regKVHorizonMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Horizon Mode)" );

  KeyValue <bool> regKVHorizonModeAuto =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Horizon Mode Auto)" );

  KeyValue <bool> regKVFirstLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(First Launch)" );

  KeyValue <bool> regKVAllowMultipleInstances =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Allow Multiple SKIF Instances)" );

  KeyValue <bool> regKVAllowBackgroundService =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Allow Background Service)" );

  KeyValue <bool> regKVDisableCFAWarning =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Disable CFA Warning)" );

  KeyValue <bool> regKVOpenAtCursorPosition =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Open At Cursor Position)" );

  KeyValue <bool> regKVOpenInServiceMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Open In Service Mode)" );

  KeyValue <bool> regKVAlwaysShowGhost =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Always Show Ghost)" );

  KeyValue <bool> regKVMaximizeOnDoubleClick =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Maximize On Double Click)" );

  KeyValue <bool> regKVMinimizeOnGameLaunch =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Minimize On Game Launch)" );

  KeyValue <bool> regKVRestoreOnGameExit =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Restore On Game Exit)" );

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

  KeyValue <bool> regKVWarningRTSS =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Warning RTSS)" );

  KeyValue <bool> regKVAutoUpdate =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Auto-Update)" );

  KeyValue <bool> regKVDeveloperMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Developer Mode)" );

  KeyValue <bool> regKVEfficiencyMode =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Efficiency Mode)" );

  KeyValue <bool> regKVFadeCovers =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Fade Covers)" );

  KeyValue <bool> regKVControllers =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Controllers)" );

  KeyValue <bool> regKVLoggingDeveloper =
    SKIF_MakeRegKeyB ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Logging Developer)" );

  // Integers (DWORDs)

  KeyValue <int> regKVLibrarySort =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Library Sort)" );

  KeyValue <int> regKVProcessRefreshInterval =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Process Refresh Interval)" );

  KeyValue <int> regKVProcessSort =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Process Sort)" );

  KeyValue <int> regKVLastSelectedGame =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Last Selected)" );

  KeyValue <int> regKVLastSelectedStore =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                          LR"(Last Selected Platform)" );

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

  KeyValue <int> regKVDiagnostics =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Diagnostics)" );

  KeyValue <int> regKVSteamUser =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Steam User)" );

  KeyValue <int> regKVValvePlug =
    SKIF_MakeRegKeyI ( LR"(SOFTWARE\Kaldaien\ValvePlug\)",
                         LR"(FillTheSwamp)" );

  // Wide Strings

  KeyValue <std::wstring> regKVIgnoreUpdate =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Ignore Update)" );

  KeyValue <std::wstring> regKVUpdateChannel =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Follow Update Channel)" );

  KeyValue <std::wstring> regKVPath =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Path)" );

  KeyValue <std::wstring> regKVAutoUpdateVersion =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Auto-Update Version)" );

  // Multi wide Strings

  KeyValue <std::vector<std::wstring>> regKVCategories =
    SKIF_MakeRegKeyVEC ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Categories)" );

  KeyValue <std::vector<std::wstring>> regKVCategoriesState =
    SKIF_MakeRegKeyVEC ( LR"(SOFTWARE\Kaldaien\Special K\)",
                         LR"(Categories State)" );

  // Windows stuff

  // SKIFdrv install location
  KeyValue <std::wstring> regKVSKIFdrvLocation =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{A459BBFA-0819-49C4-8BF7-5BDF1559ED0C}_is1\)",
                         LR"(InstallLocation)", HKEY_LOCAL_MACHINE );

  // App registration
  KeyValue <std::wstring> regKVAppRegistration =
    SKIF_MakeRegKeyWS ( LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\SKIF.exe)",
                         L"" ); // Default value

  // Notification duration
  KeyValue <int> regKVNotificationsDuration =
    SKIF_MakeRegKeyI  ( LR"(Control Panel\Accessibility\)",
                         LR"(MessageDuration)" );

  // Notification duration
  KeyValue <int> regKVWindowUseLightTheme =
    SKIF_MakeRegKeyI  ( LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\)",
                         LR"(AppsUseLightTheme)" );

  // Default settings (multiple options)
  int iNotifications           = 2;   // 0 = Never,                       1 = Always,                 2 = When unfocused
  int iGhostVisibility         = 0;   // 0 = Never,                       1 = Always,                 2 = While service is running
  int iStyle                   = 0;   // 0 = Dynamic,                     1 = SKIF Dark,              2 = SKIF Light,                  3 = ImGui Classic,                  4 = ImGui Dark
  int iStyleTemp               = 0;   // Used to temporary hold changes in the style during the current session
  int iDimCovers               = 0;   // 0 = Never,                       1 = Always,                 2 = On mouse hover
  int iCheckForUpdates         = 1;   // 0 = Never,                       1 = Weekly,                 2 = On each launch
  int iAutoStopBehavior        = 1;   // 0 = Never [not implemented],     1 = Stop on Injection,      2 = Stop on Game Exit
  int iLogging                 = 4;   // 0 = None,                        1 = Fatal,                  2 = Error,                       3 = Warning,                        4 = Info,       5 = Debug,       6 = Verbose
  int iLibrarySort             = 0;   // 0 = Name,                        1 = Uses (frequently),      2 = Last Used (recently)
  int iProcessSort             = 0;   // 0 = Status,                      1 = PID,                    2 = Arch,                        3 = Admin,                          4 = Name
  int iProcessRefreshInterval  = 2;   // 0 = Paused,                      1 = Slow (5s),              2 = Normal (1s),                [3 = High (0.5s; not implemented)]
  int iSDRMode                 = 0;   // 0 = 8 bpc,                       1 = 10 bpc,                 2 = 16 bpc
  int iHDRMode                 = 1;   // 0 = Disabled,                    1 = HDR10 (10 bpc),         2 = scRGB (16 bpc)
  int iHDRBrightness           = 203; // HDR reference white for BT.2408
  int iUIMode                  = 1;   // 0 = Safe Mode (BitBlt),          1 = Normal,                 2 = VRR Compatibility
  int iDiagnostics             = 1;   // 0 = None,                        1 = Normal,                 2 = Enhanced (not actually used yet)
  int iValvePlug               = 1;   // 0 = Disabled,                    1 = Enabled (default)

  // Default settings (booleans)
  bool bRememberLastSelected    =  true; // 2024-02-18: Enabled by default

  bool bUIBorders               = false;
  bool bUITooltips              =  true;
  bool bUIStatusBar             =  true;
  bool bUILargeIcons            = false; // 32x32 icons instead of 24x24
  bool bDPIScaling              =  true;
  bool bWin11Corners            =  true; // 2023-08-28: Enabled by default
  bool bTouchInput              =  true; // Automatically make the UI more optimized for touch input on capable devices

  bool bLibrarySteam            =  true;
  bool bLibraryEpic             =  true;
  bool bLibraryGOG              =  true;
  bool bLibraryXbox             =  true;
  bool bLibraryCustom           =  true;

  bool bServiceMode             = false;
  bool bHorizonMode             = false; // 1038 x 325 -- covers are 186.67 x 280
  bool bHorizonModeAuto         =  true;

  bool bFirstLaunch             = false;
  bool bAllowMultipleInstances  = false;
  bool bAllowBackgroundService  = false;
  bool bOpenAtCursorPosition    = false;
  bool bOpenInServiceMode       = false;
  bool bStopOnInjection         = false;
  bool bCloseToTray             = false;
  bool bLowBandwidthMode        = false;
  bool bInstantPlayGOG          = false;
  bool bInstantPlaySteam        = false;
  bool bMaximizeOnDoubleClick   = false;
  bool bMinimizeOnGameLaunch    = false;
  bool bRestoreOnGameExit       = false;
  bool bDisableCFAWarning       = false; // Controlled Folder Access warning
  bool bProcessSortAscending    =  true; // default to true
  bool bProcessIncludeAll       = false;
  bool bLibraryIgnoreArticles   = false;
  bool bAutoUpdate              = false; // Automatically runs downloaded installers
  bool bDeveloperMode           = false;
  bool bEfficiencyMode          =  true; // Should the main thread try to engage EcoQoS / Efficiency Mode on Windows 11 ?
  bool bFadeCovers              =  true;
  bool bControllers             =  true; // Should SKIF support controller input ?
  bool bLoggingDeveloper        = false; // This is a log level "above" verbose logging that also includes stuff like window messages. Only useable for SKIF developers

  // Warnings
  bool bWarningRTSS             = false;
  
  // Wide strings
  std::wstring wsUpdateChannel  = L"Website"; // Default to stable channel
  std::wstring wsIgnoreUpdate;
  std::wstring wsPath;
  std::wstring wsAutoUpdateVersion; // Holds the version the auto-updater is trying to install

  // Vectors
  std::vector <category_s> vecCategories;

  // Misc settings
  unsigned int uiLastSelectedStore;
  unsigned int uiLastSelectedGame;
  unsigned int uiSteamUser;         // This holds the last read ActiveUser from the Steam client. It's used to have something to fall back on when the Steam client is not running.

  // Windows stuff
  std::wstring wsSKIFdrvLocation;
  std::wstring wsAppRegistration;
  int  iNotificationsDuration       = 5; // Defaults to 5 seconds in case Windows is not set to something else

  // Ephemeral settings that doesn't stick around
  bool _LastSelectedWritten         = false;
  bool _LoadedSteamOverlay          = false; // This is used to indicate whether we manually loaded the steam_appid64.dll file into SKIF or not
  bool _SuppressServiceNotification = false; // This is used in conjunction with _registry.bMinimizeOnGameLaunch to suppress the "Please start game" notification
  bool _ExitOnInjection             = false; // Used to exit SKIF on a successful injection if it's used merely as a launcher
  bool _sRGBColors                  = false;
  bool _LibraryHidden               = false; // Do not show hidden games by default
  bool _EfficiencyMode              = false;
  bool _StyleLightMode              = false; // Indicates whether we are currently using a light theme or not
  bool _RendererCanWaitSwapchain    = false; // Waitable Swapchain            Windows 8.1+
  bool _RendererCanAllowTearing     = false; // DWM Tearing                   Windows 10+
  bool _RendererCanHDR              = false; // High Dynamic Range            Windows 10 1709+ (Build 16299)
  bool _RendererHDREnabled          = false; // HDR Enabled
  bool _TouchDevice                 = false;

  // Functions
  bool isDevLogging (void) const;

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

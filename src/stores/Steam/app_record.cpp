#pragma once

#include <utility/utility.h>
#include <stores/Steam/steam_library.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <utility/skif_imgui.h>
#include <SKIF.h>
#include <utility/fsutil.h>
#include <utility/games.h>
#include <utility/igdb.h>
#include <utility/launch_behaviors.h>
#include <filesystem>
#include <algorithm>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

// The registry's platform flags (SKIF_StoreFlag) are derived from the store IDs: see GetStoreFlag ( )
static_assert (StoreFlag_Steam  == 1 << (static_cast <int> (app_record_s::Store::Steam)  - 1));
static_assert (StoreFlag_GOG    == 1 << (static_cast <int> (app_record_s::Store::GOG)    - 1));
static_assert (StoreFlag_Custom == 1 << (static_cast <int> (app_record_s::Store::Custom) - 1));
static_assert (StoreFlag_Epic   == 1 << (static_cast <int> (app_record_s::Store::Epic)   - 1));
static_assert (StoreFlag_Xbox   == 1 << (static_cast <int> (app_record_s::Store::Xbox)   - 1));
static_assert (StoreFlag_EA     == 1 << (static_cast <int> (app_record_s::Store::EA)     - 1));
static_assert (StoreFlag_itch   == 1 << (static_cast <int> (app_record_s::Store::itch)   - 1));
static_assert (StoreFlag_Ubisoft== 1 << (static_cast <int> (app_record_s::Store::Ubisoft)- 1));

// Shorthand, because these are way too long
using app_launch_config_s =
      app_record_s::launch_config_s;
using app_branch_record_s =
      app_record_s::branch_record_s;

std::wstring
app_launch_config_s::getExecutableFileName (void)
{
  if (! executable.empty())
    return executable;

  // Launch configs without an executable (e.g. EA games launched through the link2ea:// or steam2ea:// protocol
  //  handlers) must not end up testing the installation folder instead (since this has
  //   bearing on whether a launch config is deemed valid or not as part of the blacklist check)
  executable_valid = 0;
  executable = L"<InvalidPath>";

  return executable;
}

std::string
app_launch_config_s::getExecutableFileNameUTF8 (void)
{
  if (! executable_utf8.empty())
    return executable_utf8;

  executable_utf8 = SK_WideCharToUTF8 (getExecutableFileName ( ));

  return executable_utf8;
}

bool
app_launch_config_s::isExecutableFileNameValid (void)
{
  if (executable_valid != -1)
    return executable_valid;

  // A protocol link (e.g. link2ea://, or steam2ea:// as used by Mass Effect Legendary Edition) is not an executable:
  //   no path is built from it (see vdf.cpp), so it is not shown or tested as if it were a file in the install folder
  executable_valid = (! executable.empty( )                                    &&
                        executable.find (L"InvalidPath") == std::wstring::npos &&
                        executable.find (L"://")         == std::wstring::npos);

  if (executable_valid == 0)
  {
    executable_path_valid = 0;
    valid                 = 0;
  //executable            = L"<InvalidPath>";
  }

  return executable_valid;
}

std::wstring
app_launch_config_s::getExecutableFullPath (void) const
{
  return executable_path;
}

std::string
app_launch_config_s::getExecutableFullPathUTF8 (void)
{
  if (! executable_path_utf8.empty())
    return executable_path_utf8;

  executable_path_utf8 = SK_WideCharToUTF8 (getExecutableFullPath ( ));

  return executable_path_utf8;
}

bool
app_launch_config_s::isExecutableFullPathValid (void)
{
  if (executable_path_valid != -1)
    return executable_path_valid;

  std::wstring full_path =
    getExecutableFullPath ( );

  executable_path_valid =
                  (! full_path.empty( )                                    &&
                     full_path.find (L"InvalidPath") == std::wstring::npos &&
    PathFileExistsW (full_path.c_str()) == TRUE);

  valid = executable_path_valid;

  return executable_path_valid;
}

std::wstring
app_launch_config_s::getExecutableDir (void) const
{
  std::wstring exec_path =
    getExecutableFullPath ( );

  wchar_t  wszExecutableBase [MAX_PATH + 2] = { };
  StrCatW (wszExecutableBase, exec_path.c_str ());

  PathRemoveFileSpecW (wszExecutableBase);

  return wszExecutableBase;
}

std:: string
app_launch_config_s::getExecutableDirUTF8 (void) const
{
  return SK_WideCharToUTF8 (getExecutableDir ( ));
}

bool
app_launch_config_s::isExecutableDirValid (void) const
{
  return PathFileExistsW (getExecutableDir ( ).c_str());
}

std::wstring
app_launch_config_s::getDescription (void)
{
  if (! description.empty())
    return description;

  description = (isExecutableFileNameValid ( )) ? executable : L"<InvalidDescription>";

  return description;
}

std::string
app_launch_config_s::getDescriptionUTF8 (void)
{
  if (! description_utf8.empty())
    return description_utf8;

  description_utf8 = SK_WideCharToUTF8 (getDescription ( ));

  return description_utf8;
}

std::wstring
app_launch_config_s::getLaunchOptions (void) const
{
  return launch_options;
}

std::string
app_launch_config_s::getLaunchOptionsUTF8 (void)
{
  if (! launch_options_utf8.empty())
    return launch_options_utf8;

  launch_options_utf8 = SK_WideCharToUTF8 (getLaunchOptions ( ));

  return launch_options_utf8;
}

std::wstring
app_launch_config_s::getWorkingDirectory (void) const
{
  return working_dir;
}

std::string
app_launch_config_s::getWorkingDirectoryUTF8 (void)
{
  if (! working_dir_utf8.empty())
    return working_dir_utf8;

  working_dir_utf8 = SK_WideCharToUTF8 (getWorkingDirectory ( ));

  return working_dir_utf8;
}

std::wstring
app_record_s::launch_config_s::getWorkOrExeDirectory (void) const
{
  return (! getWorkingDirectory    ( ).empty())
            ? getWorkingDirectory  ( )
            : isExecutableDirValid ( )
              ? getExecutableDir   ( )
              : std::wstring();
}

std::string
app_record_s::launch_config_s::getWorkOrExeDirectoryUTF8 (void)
{
  return (! getWorkingDirectoryUTF8   ( ).empty())
            ? getWorkingDirectoryUTF8 ( )
            : isExecutableDirValid    ( )
              ? getExecutableDirUTF8  ( )
              : std::string();
}

std::wstring
app_launch_config_s::getBlacklistFilename (void)
{
  if (! blacklist_file.empty () && executable_valid != -1)
    return blacklist_file;

  std::wstring full_path =
    getExecutableFullPath ( );

  if (isExecutableFileNameValid ( ))
  {
    wchar_t wszExecutableBase [MAX_PATH + 2] = { };
    wchar_t wszBlacklistPath  [MAX_PATH + 2] = { };

    StrCatW (wszExecutableBase, executable.c_str ());
    StrCatW (wszBlacklistPath,  full_path.c_str  ());

    PathRemoveFileSpecW  ( wszBlacklistPath  );
    PathStripPathW       ( wszExecutableBase );
    PathRemoveExtensionW ( wszExecutableBase );

    blacklist_file =
      SK_FormatStringW (
        L"%ws\\SpecialK.deny.%ws",
          wszBlacklistPath, wszExecutableBase
                       );
  }

  else
  {
    blacklist_file =
      SK_FormatStringW (
        L"%ws\\SpecialK.deny",
          install_dir.c_str()
                       );

    /* Not used any longer to support shell execute based "executables"
    blacklisted    = 1;
    blacklist_file =
      L"InvalidLaunchConfig.NeverInject";
    */
  }

  return
    blacklist_file;
}

// Where a launch config's behaviors are stored: the executable itself, or the
//   install folder for launch configs without one (URIs, shell execute, etc).
//     This mirrors the legacy SpecialK.deny.<exe> / SpecialK.deny file locations.
static std::wstring
_GetLaunchBehaviorsPath (app_launch_config_s& config)
{
  return (config.isExecutableFileNameValid ( ))
          ? config.getExecutableFullPath   ( )
          : config.install_dir;
}

bool
app_launch_config_s::setBlacklisted (bool blacklist)
{
  using Service = SKIF_LaunchBehaviors::Service;

  if (blacklist == isBlacklisted ( ))
    return blacklisted;

  std::wstring behaviors_path =
    _GetLaunchBehaviorsPath (*this);

  // Special K itself reads this file to remain disabled for the executable, so it is still maintained
  std::wstring blacklist_path =
    getBlacklistFilename ( );

  if (blacklist)
  {
    // The elevation is kept: it still applies if the service is used all the same (Ignore blacklist)
    SKIF_LaunchBehaviors::SetService (behaviors_path, Service::Blocked);

    FILE* fBlacklist =
      _wfopen (blacklist_path.c_str (), L"w+");

    if (fBlacklist != nullptr)
    {
      fputws (L"This file tells Special K to remain disabled for the executable.", fBlacklist);
      fclose (           fBlacklist);
    }
  }

  else
  {
    SKIF_LaunchBehaviors::SetService (behaviors_path, Service::Inherit);

    // Still blocked through a parent folder -- override it for this executable
    if (SKIF_LaunchBehaviors::Resolve (behaviors_path).service == Service::Blocked)
      SKIF_LaunchBehaviors::SetService (behaviors_path, Service::Normal);

    if (PathFileExistsW (blacklist_path.c_str ()))
      DeleteFileW       (blacklist_path.c_str ());
  }

  return isBlacklisted (true);
}

bool
app_launch_config_s::isBlacklisted (bool refresh)
{
  using Service = SKIF_LaunchBehaviors::Service;

  if (blacklisted != -1 && ! refresh)
    return blacklisted;

  std::wstring behaviors_path =
    _GetLaunchBehaviorsPath (*this);

  Service service =
    SKIF_LaunchBehaviors::Resolve (behaviors_path).service;

  // Special K honors the legacy file regardless of the registry, so treat it as blocked
  //   and record it in the registry so the launcher does not need to look for the file
  if (service != Service::Blocked && PathFileExistsW (getBlacklistFilename ( ).c_str ()))
  {
    SKIF_LaunchBehaviors::SetService (behaviors_path, Service::Blocked);
    service  = Service::Blocked;
  }

  blacklisted =
    (service == Service::Blocked) ? 1 : 0;

  return blacklisted;
}

std::wstring
app_launch_config_s::getElevatedFilename (void)
{
  if (! elevated_file.empty () && executable_valid != -1)
    return elevated_file;

  std::wstring full_path =
    getExecutableFullPath ( );

  if (isExecutableFileNameValid ( ))
  {
    wchar_t wszExecutableBase [MAX_PATH + 2] = { };
    wchar_t wszElevatedPath   [MAX_PATH + 2] = { };

    StrCatW (wszExecutableBase, executable.c_str ());
    StrCatW (wszElevatedPath,   full_path.c_str  ());

    PathRemoveFileSpecW  ( wszElevatedPath   );
    PathStripPathW       ( wszExecutableBase );
    PathRemoveExtensionW ( wszExecutableBase );

    elevated_file =
      SK_FormatStringW (
        L"%ws\\SpecialK.admin.%ws",
          wszElevatedPath, wszExecutableBase
                       );
  }

  else
  {
    elevated_file =
      SK_FormatStringW (
        L"%ws\\SpecialK.admin",
          install_dir.c_str()
                       );

    /* Not used any longer to support shell execute based "executables"
    elevated      = 0;
    elevated_file =
      L"InvalidLaunchConfig.NeverInject";
    */
  }

  return
    elevated_file;
}

// The elevation of the service for the executable, whenever it is used: also for a blocked executable that uses the
//   service all the same (Ignore blacklist), so it is kept independently of setBlacklisted ( )
bool
app_launch_config_s::setElevated (bool elevate)
{
  using Elevation = SKIF_LaunchBehaviors::Elevation;

  if (elevate == isElevated ( ))
    return elevated;

  std::wstring behaviors_path =
    _GetLaunchBehaviorsPath (*this);

  if (elevate)
    SKIF_LaunchBehaviors::SetElevation (behaviors_path, Elevation::Elevated);

  else
  {
    SKIF_LaunchBehaviors::SetElevation (behaviors_path, Elevation::Inherit);

    // Still elevated through a parent folder -- override it for this executable
    if (SKIF_LaunchBehaviors::Resolve (behaviors_path).elevation == Elevation::Elevated)
      SKIF_LaunchBehaviors::SetElevation (behaviors_path, Elevation::Normal);

    // The legacy file is only read by SKIF, and has been superseded by the registry
    std::wstring elevated_path =
      getElevatedFilename ( );

    if (PathFileExistsW (elevated_path.c_str ()))
      DeleteFileW       (elevated_path.c_str ());
  }

  return isElevated (true);
}

bool
app_launch_config_s::isElevated (bool refresh)
{
  using Elevation = SKIF_LaunchBehaviors::Elevation;

  if (elevated != -1 && ! refresh)
    return elevated;

  std::wstring behaviors_path =
    _GetLaunchBehaviorsPath (*this);

  Elevation elevation =
    SKIF_LaunchBehaviors::Resolve (behaviors_path).elevation;

  // Migrate the legacy file to the registry if the registry has no elevation setting yet
  if (elevation == Elevation::Inherit && PathFileExistsW (getElevatedFilename ( ).c_str ()))
  {
    SKIF_LaunchBehaviors::SetElevation (behaviors_path, Elevation::Elevated);
    elevation = Elevation::Elevated;
  }

  elevated =
    (elevation == Elevation::Elevated) ? 1 : 0;

  return elevated;
}

bool
app_launch_config_s::setIgnoreBlacklist (bool ignore)
{
  using Blacklist = SKIF_LaunchBehaviors::Blacklist;

  if (ignore == isIgnoringBlacklist ( ))
    return ignore_blacklist;

  std::wstring behaviors_path =
    _GetLaunchBehaviorsPath (*this);

  if (ignore)
    SKIF_LaunchBehaviors::SetBlacklist (behaviors_path, Blacklist::Ignore);

  else
  {
    SKIF_LaunchBehaviors::SetBlacklist (behaviors_path, Blacklist::Inherit);

    // Still ignored through a parent folder -- override it for this executable
    if (SKIF_LaunchBehaviors::Resolve (behaviors_path).blacklist == Blacklist::Ignore)
      SKIF_LaunchBehaviors::SetBlacklist (behaviors_path, Blacklist::Respect);
  }

  return isIgnoringBlacklist (true);
}

bool
app_launch_config_s::isIgnoringBlacklist (bool refresh)
{
  using Blacklist = SKIF_LaunchBehaviors::Blacklist;

  if (ignore_blacklist != -1 && ! refresh)
    return ignore_blacklist;

  ignore_blacklist =
    (SKIF_LaunchBehaviors::Resolve (_GetLaunchBehaviorsPath (*this)).blacklist == Blacklist::Ignore) ? 1 : 0;

  return ignore_blacklist;
}

std::wstring
app_branch_record_s::getTime (void)
{
  if (! time_string.empty())
    return time_string;

  time_string = SKIF_Util_timeGetTimeAsWStr (time_updated);

  return time_string;
}

std::string
app_branch_record_s::getTimeUTF8 (void)
{
  if (! time_string_utf8.empty())
    return time_string_utf8;

  time_string_utf8 = SK_WideCharToUTF8 (getTime());

  return time_string_utf8;
}

std::wstring
app_branch_record_s::getDescription (void) const
{
  return description;
}

std::string
app_branch_record_s::getDescriptionUTF8 (void)
{
  if (! description_utf8.empty())
    return description_utf8;

  description_utf8 = SK_WideCharToUTF8 (description);

  return description_utf8;
}

DWORD app_record_s::client_state_s::_TimeLastNotified = 0UL;


bool
app_record_s::client_state_s::refresh (app_record_s *pApp)
{
  static constexpr
    DWORD _RefreshInterval = 333UL;

  // This cannot be called from a child thread, as the registry watch is not thread agnostic and will break
  static SKIF_RegistryWatch
    _appWatch ( HKEY_CURRENT_USER,
                  LR"(SOFTWARE\Valve\Steam\Apps)",
                    L"SteamAppNotify", TRUE, REG_NOTIFY_CHANGE_LAST_SET ); // UITab_Library

  DWORD dwTimeNow = SKIF_Util_timeGetTime ( );

  static DWORD
       dwLastSignalCheck = 0UL;
  if ( dwLastSignalCheck < dwTimeNow - _RefreshInterval )
  {    dwLastSignalCheck = dwTimeNow;
    if (_appWatch.isSignaled ())
      app_record_s::client_state_s::_TimeLastNotified =
                           dwTimeNow;
    else
       dwLastSignalCheck += ( _RefreshInterval / 3 );
  }

  if ( dwTimeLastChecked <= app_record_s::client_state_s::_TimeLastNotified )
  {
    if (SKIF_Steam_UpdateAppState (pApp))
      dwTimeLastChecked = dwTimeNow + _RefreshInterval;
    else
      invalidate ();
  }

  return true;
}


#pragma region Launching


// Adds the launch config's executable to the injection whitelist, if not already covered
void
app_record_s::WhitelistForInjection (launch_config_s* config)
{
  static SKIF_InjectionContext& _inject = SKIF_InjectionContext::GetInstance ( );

  if (! _inject._TestUserList (config->getExecutableFullPathUTF8 ( ).c_str (), true))
  {
    if (config->isExecutableFullPathValid ( ) &&
        _inject.WhitelistPath (config->getExecutableFullPathUTF8 ( )))
      _inject.SaveWhitelist ( );
  }
}

// Default launch: only an instant launch is possible
void
app_record_s::Launch (const launch_context_s& ctx)
{
  if (ctx.instant)
    LaunchInstant (ctx);

  else
    PLOG_ERROR << "No applicable launch option was found?!";
}

// The command line of a launch config (see app_record.h): used by LaunchInstant ( ) and shown in the UI
app_record_s::launch_command_s
app_record_s::GetLaunchCommand (launch_config_s* config, bool forLaunch)
{
  launch_command_s ret;

  if (config == nullptr)
    return ret;

  // We need to use proxy variables since we might remove a substring of the launch options
  std::wstring cmdLine = config->getLaunchOptions();

  // Transform to lowercase
  std::wstring cmdLineLower = SKIF_Util_ToLowerW (cmdLine);

  const std::wstring argSKIF_SteamAppID       = L"skif_steamappid=";
  size_t             posSKIF_SteamAppID_start = cmdLineLower.find (argSKIF_SteamAppID);

  // For Steam games, we default to using the id value
  uint32_t     uiSteamAppID = GetSteamAppID ( );
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
      if (forLaunch)
        PLOG_ERROR << "Unable to convert found Steam App ID to integer: " << wsSteamAppID;
      uiSteamAppID = 0;
    }
  }

  // The launch options set in the store client (Steam) apply to the store's own launch configs only, not to SKIF's or the
  //   user's. Read again for a launch; the UI uses the ones read last (only those of the game itself are kept)
  if (uiSteamAppID != 0 &&
      ! config->custom_skif &&
      ! config->custom_user)
  {
    std::string steamLaunchOptions = (forLaunch) ? GetStoreLaunchOptions (uiSteamAppID)
                                   : (uiSteamAppID == GetSteamAppID ( )) ? GetClientLaunchOptions ( ) : std::string ( );

    if (steamLaunchOptions.size() > 0)
    {
      ret.clientOptions = steamLaunchOptions;

      std::string steamLaunchOptionsLower =
        SKIF_Util_ToLower (steamLaunchOptions);

      std::string sSteam_Command = "%command%"; // Only for Steam games
      size_t posSteam_Command_start = steamLaunchOptionsLower.find(sSteam_Command);

      // Check if a %COMMAND% is a part of the launch options
      // %COMMAND% is special in that the original developer-specified launch options are placed at its exact position,
      //   making the user-specified Steam launch options the _primary_ and the developer-specified launch options _secondary_
      if (posSteam_Command_start != std::string::npos)
      {
        // Base is dev-specified executable
        std::wstring newCmdLine = config->getExecutableFullPath ( );
        std::wstring wsSteamLO  = SK_UTF8ToWideChar (steamLaunchOptions);

        // Appended is dev-specified cmd line arguments
        if (! cmdLine.empty())
          newCmdLine += L" " + cmdLine;

        // Replace %command% with the dev-specified exe and args
        wsSteamLO.replace(posSteam_Command_start, sSteam_Command.length(), newCmdLine);

        // Swap in the result and flag to use shell execute
        cmdLine          = wsSteamLO;
        ret.shellExecute = true;
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

  ret.cmdLine      = cmdLine;
  ret.steamAppID   = uiSteamAppID;
  ret.wsSteamAppID = wsSteamAppID;
  ret.display      = (ret.shellExecute) ? cmdLine
                                        : config->getExecutableFullPath ( ) + ((cmdLine.empty ( )) ? L"" : L" " + cmdLine);

  return ret;
}

// Instant Play: launches the executable of the launch config directly
// - Custom games
// - GOG games when Galaxy is not installed
// - Prefer Instant Play for GOG
// - Prefer Instant Play for Steam
void
app_record_s::LaunchInstant (const launch_context_s& ctx)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  PLOG_VERBOSE << "Performing an instant launch...";

  bool launchDecision       = true;

  // The command line, with the launch options set in the store client (Steam) applied (GetLaunchCommand ( ), which also
  //   builds what the UI shows). They are read again for the launch
  launch_command_s command = GetLaunchCommand (ctx.config, true);

  std::wstring cmdLine         = command.cmdLine;
  bool         useShellExecute = command.shellExecute; // This is in case a %COMMAND% Steam call is made
  uint32_t     uiSteamAppID    = command.steamAppID;
  std::wstring wsSteamAppID    = command.wsSteamAppID;

  std::map<std::wstring, std::wstring> env;
  bool steamOverlay = true;

  if (uiSteamAppID != 0)
  {
    PLOG_DEBUG << "Using Steam App ID : "  << uiSteamAppID;
    env.emplace       (L"SteamAppId",         wsSteamAppID); // Dunno if one is the primary one...
    env.emplace       (L"SteamGameId",        wsSteamAppID); //   ... so let's use both of them...
  //env.emplace       (L"SteamOverlayGameId", wsSteamAppID);
  //env.emplace       (L"EnableConfiguratorSupport", L"0");

    steamOverlay = SKIF_Steam_isSteamOverlayEnabled (uiSteamAppID, SKIF_Steam_GetCurrentUser ( ));

    if (! steamOverlay)
    {
      PLOG_DEBUG << "Disabling the Steam Overlay...";
      env.emplace (L"SteamNoOverlayUIDrawing", L"1");
    }

    if (! command.clientOptions.empty ( ))
    {
      PLOG_DEBUG << "Found additional launch options for this app in Steam: " << command.clientOptions;

      // Check if SKIF %COMMAND% is being used, when we're trying to launch without Special K
      if (! ctx.usingSK && SKIF_Util_ToLower (command.clientOptions).find ("skif ") == 0)
      {
        launchDecision = false;

        // The message is shown as is (not as a format), so percent signs (%) need no escaping
        SKIF_ImGui_InfoMessage (_("Conflicting configuration"),
                                std::string (_("Could not launch game due to conflicting launch options in Steam:")) + "\n"
                                "\n"
                              +  command.clientOptions + "\n"
                                "\n"
                              + _("Please change the launch options in Steam before trying again."));

        PLOG_WARNING << "Steam game " << id << " (" << names.normal << ") was unable to launch due to a conflict with the launch option of Steam!";
      }
    }
  }

  if (launchDecision)
  {
    SKIF_Util_CreateProcess_s* proc = nullptr;
    for (auto& item : SKIF_GamingCollection::Get ( ).GetInstantPlayProcesses ( ))
    {
      if (item.id == 0)
      {
        proc           = &item;
        proc->id       = id;
        proc->store_id = (int)store;
        break;
      }
    }

    std::wstring dirPath = ctx.config->getWorkOrExeDirectory ( );

    // This is a fallback for handling Steam's %COMMAND% scenarios
    if (useShellExecute)
    {
      // Note that any new process will inherit SKIF's environment variables, so apply the custom variables
      //   (and remove SteamNoOverlayUIDrawing if SKIF set it for itself, unless the launch sets it explicitly)
      //     for the duration of the ShellExecuteEx ( ) call below; restored when this scope ends
      std::map <std::wstring, std::wstring> overrides = env;

      if (_registry._LoadedSteamOverlay)
        overrides.try_emplace (L"SteamNoOverlayUIDrawing", L"");

      SKIF_Util_ScopedEnvironment scopedEnv (overrides);

      // We need to split cmdLine between executable and parameters
      // Example that we may get: (replace %COMMAND% with game executable and launch options)
      // SKIF %COMMAND%
      // "C:\Windows\System32\Notepad.exe" %COMMAND%
      // and so on and so forth...

      std::wstring exePath;

      // First position is a quotation mark, assume executable is surrounded in them...
      if (cmdLine.find(L"\"") == 0)
      {
        exePath = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1);                  // Executable
        cmdLine = cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos); // Parameters
      }

      // Go by spaces instead
      else if (cmdLine.find(L" ") != std::wstring::npos) {
        exePath = cmdLine.substr(0, cmdLine.find(L" "));                      // Executable
        cmdLine = cmdLine.substr(cmdLine.find(L" ") + 1, std::wstring::npos); // Parameters
      }

      // Fallback: cmdLine only has a single non-spaces unquoted path to an executable
      else {
        exePath = cmdLine;
        cmdLine = L"";
      }

      SHELLEXECUTEINFOW
        sexi              = { };
        sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
        sexi.lpVerb       = L"OPEN";
        sexi.lpFile       = exePath.c_str();
        sexi.lpParameters = cmdLine.c_str();
        sexi.lpDirectory  = dirPath.c_str();
        sexi.nShow        = SW_SHOWNORMAL;
        sexi.fMask        = SEE_MASK_NOCLOSEPROCESS | // We need the PID of the process that gets started
                            SEE_MASK_NOASYNC        | // Never async since we need env variables to be set properly
                            SEE_MASK_NOZONECHECKS;    // No zone check needs to be performed

      PLOG_INFO                       << "Performing a ShellExecuteEx call...";
      PLOG_INFO_IF(! exePath.empty()) << "File      : " << exePath;
      PLOG_INFO_IF(! cmdLine.empty()) << "Parameters: " << cmdLine;
      PLOG_INFO_IF(! dirPath.empty()) << "Directory : " << dirPath;

      // Attempt to execute the call
      if (ShellExecuteExW (&sexi))
      {
        DWORD dwProcessId = (sexi.hProcess != NULL) ? GetProcessId (sexi.hProcess) : 0;

        PLOG_INFO << "The operation was successful (PID " << dwProcessId << ").";

        if (proc != nullptr)
        {
          proc->hProcess    = sexi.hProcess;
          proc->dwProcessId = dwProcessId;
        }

        // No free instant play slot to monitor the process with, so the handle is unneeded
        else if (sexi.hProcess != NULL)
          CloseHandle (sexi.hProcess);
      }

      else {
        PLOG_ERROR << "Shell execute failed ?!";
        PLOG_ERROR << SKIF_Util_GetErrorAsWStr ( );

        if (proc != nullptr)
        {
          proc->id       =  0;
          proc->store_id = -1;
        }
      }

      // (scopedEnv restores the environment variables when this scope ends)
    }

    // Main launcher (uses ShellExecute in case elevation is required)
    else if (SKIF_Util_CreateProcess (ctx.config->getExecutableFullPath ( ),
                        cmdLine.c_str(),
                        dirPath.c_str(),
                          &env,
                          proc
      ))
    {
      if (GetSteamAppID ( ) != 0)
        cmdLine += L" SKIF_SteamAppId=" + std::to_wstring (GetSteamAppID ( ));

      SKIF_Util_TrimTrailingSpacesW (cmdLine);

      std::wstring iconPath = GetJumpListIconPath (ctx.config);

      // Launch options other than the one the store client starts (lc.json, lc_user.json, secondary ones) are told apart by their description
      std::wstring jumpListName = SK_UTF8ToWideChar (names.normal);

      if (! ClientCanLaunch (*ctx.config) && ! ctx.config->description.empty ( ))
        jumpListName += L" (" + ctx.config->description + L")";

      SKIF_Shell_AddJumpList (jumpListName,
        ctx.config->getExecutableFullPath ( ),
        cmdLine,
        dirPath,
        iconPath,
        (! ctx.localInjection && ctx.usingSK));
    }

    else {
      PLOG_ERROR << "Process worker creation failed ?!";

      proc->id       =  0;
      proc->store_id = -1;
    }
  }
}

#pragma endregion

#pragma region Library UI

std::wstring
app_record_s::GetAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (id == SKIF_STEAM_APPID)
    return SK_FormatStringW (LR"(%ws\Assets\)", _path_cache.specialk_userdata);

  return GetStoreAssetDirectory ( );
}

// The downloaded IGDB cover, when the user prefers IGDB covers for this store and has no custom cover.
//   A PCGamingWiki cover (also preferred) takes precedence, as those are typically of a higher resolution.
std::wstring
app_record_s::GetPreferredIGDBCover (void) const
{
  if (! WantsIGDBCover ( ) || ! FindCustomArtwork (LibraryTexture::Cover).empty ( ))
    return L"";

  if (WantsPCGWCover ( ) && PathFileExistsW ((GetAssetDirectory ( ) + L"cover-pcgw.png").c_str ( )))
    return L"";

  std::wstring path = GetAssetDirectory ( ) + L"cover-igdb.jpg";

  return (PathFileExistsW (path.c_str ( ))) ? path : L"";
}

// Cover worker thread: by the cleaned name (without (TM), (R) and (C) signs), exact matches only
bool
app_record_s::DownloadIGDBCover (const std::wstring& targetPath)
{
  const std::string& title = (! names.clean.empty ( )) ? names.clean : names.original;

  return SKIF_IGDB_DownloadCoverByName (title, targetPath);
}

std::wstring
app_record_s::GetDesktopShortcutArgs (void)
{
  // The shortcut starts the default launch config through SKIF's launcher mode (a snapshot: later changes don't update it)
  launch_config_s* lc = GetDefaultLaunchConfig ( );

  if (lc == nullptr)
    return L"";

  std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", lc->getExecutableFullPath().c_str(), lc->getLaunchOptions().c_str());

  SKIF_Util_TrimTrailingSpacesW (linkArgs);

  return linkArgs;
}

#pragma endregion

#pragma region Default Launch Config

std::wstring
app_record_s::GetPathRelativeTo (const std::wstring& base, const std::wstring& path)
{
  if (base.empty ( ) || path.empty ( ))
    return L"";

  std::wstring b = std::filesystem::path (base).lexically_normal ( ).wstring ( ),
               p = std::filesystem::path (path).lexically_normal ( ).wstring ( );

  std::replace (b.begin ( ), b.end ( ), L'/', L'\\');
  std::replace (p.begin ( ), p.end ( ), L'/', L'\\');

  while (! b.empty ( ) && b.back ( ) == L'\\')
    b.pop_back ( );

  if (b.empty ( ) || p.length ( ) <= b.length ( ) + 1 || p [b.length ( )] != L'\\')
    return L"";

  if (_wcsnicmp (p.c_str ( ), b.c_str ( ), b.length ( )) != 0)
    return L"";

  return p.substr (b.length ( ) + 1);
}

// Same filter as the launch configs listed in the game menu
bool
app_record_s::IsLaunchConfigAvailable (const launch_config_s& lc) const
{
  return (lc.valid != 0 && ! lc.duplicate_exe_args && lc.owns_dlc != 0);
}

std::string
app_record_s::GetRelativeExecutable (const launch_config_s& lc) const
{
  std::wstring full = lc.getExecutableFullPath ( );
  std::wstring rel  = GetPathRelativeTo (install_dir, full);
  std::string  ret  = SK_WideCharToUTF8 ((rel.empty ( )) ? full : rel);

  std::replace (ret.begin ( ), ret.end ( ), '\\', '/');

  return ret;
}

app_record_s::launch_ref_s
app_record_s::MakeLaunchRef (const launch_config_s& lc) const
{
  launch_ref_s ref;

  switch (GetLaunchSource (lc))
  {
    case LaunchSource::User:     ref.source = "User";     break;
    case LaunchSource::SKIF:     ref.source = "SKIF";     break;
    case LaunchSource::Platform:
    default:                     ref.source = "Platform"; break;
  }

  ref.exe  = GetRelativeExecutable (lc);
  ref.args = SK_WideCharToUTF8     (lc.getLaunchOptions ( ));
  ref.desc = SK_WideCharToUTF8     (lc.description);

  return ref;
}

// Matches on source, executable (case-insensitive) and arguments; the description only decides between several matches
app_record_s::launch_config_s*
app_record_s::FindLaunchConfig (const launch_ref_s& ref)
{
  if (ref.empty ( ))
    return nullptr;

  launch_config_s* match = nullptr;

  for (auto& it : launch_configs)
  {
    launch_config_s& lc = it.second;

    if (! IsLaunchConfigAvailable (lc))
      continue;

    launch_ref_s cur = MakeLaunchRef (lc);

    if (cur.source != ref.source ||
        cur.args   != ref.args   ||
        _stricmp (cur.exe.c_str ( ), ref.exe.c_str ( )) != 0)
      continue;

    if (cur.desc == ref.desc)
      return &lc;

    if (match == nullptr)
      match = &lc;
  }

  return match;
}

app_record_s::launch_config_s*
app_record_s::GetDefaultLaunchConfig (void)
{
  if (launch_configs.empty ( ))
    return nullptr;

  if (launch_config_s* lc = FindLaunchConfig (skif.default_launch))
    return lc;

  return &launch_configs.begin ( )->second;
}

#pragma endregion

#pragma region Run State

// Whether the process is the executable of one of the launch configs: the primary one, or another that may have been
//   launched instead (the default launch config, Instant play, the user's own). Matched by the full path.
//   Called by SKIF_LibRefreshWorker, under the apps lock: only reads the launch configs (no operator [ ], and not the
//     is...Valid ( ) checks, which cache their result)
bool
app_record_s::MatchesLaunchExecutable (const wchar_t* wszExePath, DWORD dwExePathLen) const
{
  if (wszExePath == nullptr || dwExePathLen == 0)
    return false;

  for (const auto& launch_cfg : launch_configs)
  {
    const launch_config_s& lc = launch_cfg.second;

    // Not a launch config of this game (e.g. Steam's of another branch), or without an executable (e.g. Link2EA)
    if (lc.valid == 0)
      continue;

    const std::wstring path = lc.getExecutableFullPath ( );

    if (! path.empty ( ) && _wcsnicmp (path.c_str ( ), wszExePath, dwExePathLen) == 0) // full path
      return true;
  }

  return false;
}

// Epic, GOG and SKIF Custom should be straight forward: match the full path of a launch config's executable
app_record_s::ProcessMatch
app_record_s::MatchProcess (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen)
{
  UNREFERENCED_PARAMETER (wszExeFile);

  if (MatchesLaunchExecutable (wszExePath, dwExePathLen))
    return ProcessMatch::Running;

  // One can also perform a partial match with the below OR clause in the IF statement, however from testing
  //   PROCESS_QUERY_LIMITED_INFORMATION gives us GetExitCodeProcess() and QueryFullProcessImageName() rights
  //     even to elevated processes, meaning the below OR clause is unnecessary.
  //
  // (fullPath.empty() && ! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str()))
  //

  return ProcessMatch::None;
}

#pragma endregion

#pragma region Artwork

// The user's custom cover / icon: <asset dir>cover|icon .png, .jpg (and .ico for icons)
std::wstring
app_record_s::FindCustomArtwork (LibraryTexture type) const
{
  std::wstring SKIFCustomPath = GetAssetDirectory ( ) + ((type == LibraryTexture::Cover) ? L"cover" : L"icon");

  if      (PathFileExistsW ((SKIFCustomPath + L".png").c_str()))
    return                   SKIFCustomPath + L".png";
  else if (PathFileExistsW ((SKIFCustomPath + L".jpg").c_str()))
    return                   SKIFCustomPath + L".jpg";
  else if (type == LibraryTexture::Icon &&
           PathFileExistsW ((SKIFCustomPath + L".ico").c_str()))
    return                   SKIFCustomPath + L".ico";

  return L"";
}

// SKIF Custom: custom artwork, else an icon extracted from the executable
std::wstring
app_record_s::ResolveArtwork (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged)
{
  UNREFERENCED_PARAMETER (source);
  UNREFERENCED_PARAMETER (isManaged);

  std::wstring load_str = FindCustomArtwork (type);

  isCustom = (! load_str.empty ( ));

  if (! isCustom)
  {
    std::wstring SKIFCustomPath = GetAssetDirectory ( ) + L"icon";

    if      (type == LibraryTexture::Icon &&
             SKIF_Util_SaveExtractExeIcon (launch_configs[0].getExecutableFullPath ( ), SKIFCustomPath + L"-original.png"))
      load_str =                SKIFCustomPath + L"-original.png";
  }

  return load_str;
}

#pragma endregion

#pragma region Library Refresh

// The launch config fields the library worker fills in (executable_valid and the other cached values are filled in
//   later, and the getters used here don't change anything)
bool
app_record_s::HasSameLaunchConfigs (const std::map <int, launch_config_s>& a,
                                    const std::map <int, launch_config_s>& b)
{
  if (a.size ( ) != b.size ( ))
    return false;

  for (auto itA = a.begin ( ), itB = b.begin ( ); itA != a.end ( ); ++itA, ++itB)
  {
    const launch_config_s& lcA = itA->second;
    const launch_config_s& lcB = itB->second;

    if (itA->first                  != itB->first                  ||
        lcA.id                      != lcB.id                      ||
        lcA.id_steam                != lcB.id_steam                ||
        lcA.type                    != lcB.type                    ||
        lcA.cpu_type                != lcB.cpu_type                ||
        lcA.platforms               != lcB.platforms               ||
        lcA.getExecutableFullPath ( ) != lcB.getExecutableFullPath ( ) ||
        lcA.getLaunchOptions ( )    != lcB.getLaunchOptions ( )    ||
        lcA.getWorkingDirectory ( ) != lcB.getWorkingDirectory ( ) ||
        lcA.install_dir             != lcB.install_dir             ||
        lcA.executable_helper       != lcB.executable_helper       ||
        lcA.Xbox_ApplicationId      != lcB.Xbox_ApplicationId      ||
        lcA.branches                != lcB.branches                ||
        lcA.requires_dlc            != lcB.requires_dlc            ||
        lcA.custom_skif             != lcB.custom_skif             ||
        lcA.custom_user             != lcB.custom_user)
      return false;
  }

  return true;
}

bool
app_record_s::HasSameLibraryData (const app_record_s& other) const
{
  // Store overrides cast other to their own record type
  if (typeid (*this) != typeid (other))
    return false;

  return (id                == other.id                &&
          store             == other.store             &&
          install_dir       == other.install_dir       &&
          cloud_enabled     == other.cloud_enabled     &&
          ImGuiLabelID      == other.ImGuiLabelID      &&
          names.original    == other.names.original    &&
          names.clean       == other.names.clean       &&
          names.normal      == other.names.normal      &&
          skif.name         == other.skif.name         &&
          skif.cpu_type     == other.skif.cpu_type     &&
          skif.cpu_cached   == other.skif.cpu_cached   &&
          skif.instant_play == other.skif.instant_play &&
          skif.default_launch == other.skif.default_launch &&
          skif.uses         == other.skif.uses         &&
          skif.used         == other.skif.used         &&
          skif.category     == other.skif.category     &&
          skif.hidden       == other.skif.hidden       &&
          skif.pinned       == other.skif.pinned       &&
          HasSameLaunchConfigs (GetPopulatedLaunchConfigs ( ), other.GetPopulatedLaunchConfigs ( )));
}

#pragma endregion

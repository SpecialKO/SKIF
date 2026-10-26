#pragma once

#include <utility/utility.h>
#include <stores/Steam/steam_library.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <utility/skif_imgui.h>
#include <SKIF.h>
#include <utility/fsutil.h>
#include <utility/games.h>

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

  // EA games using link2ea:// protocol handlers to launch games does not have an executable,
  //  so this ensures we do not end up testing the installation folder instead (since this has
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

  executable_valid = (! executable.empty( )                                    &&
                        executable.find (L"InvalidPath") == std::wstring::npos &&
                        executable.find (L"link2ea")     == std::wstring::npos);

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

bool
app_launch_config_s::setBlacklisted (bool blacklist)
{
  std::wstring blacklist_path =
    getBlacklistFilename ( );

  auto _Blacklist =
  [&](bool set) -> bool
  {
    blacklisted =
      PathFileExistsW (blacklist_path.c_str ()) ?
                                              1 : 0;

    assert (set == (bool)blacklisted);

    UNREFERENCED_PARAMETER (set);

    return blacklisted;
  };

  if (blacklist != isBlacklisted ( ))
  {
    if (blacklist)
    {
      FILE* fBlacklist =
        _wfopen (blacklist_path.c_str (), L"w+");

      if (fBlacklist != nullptr)
      {
        fputws (L"This file tells Special K to remain disabled for the executable.", fBlacklist);
        fclose (           fBlacklist);

        return _Blacklist (blacklist);
      }

      return _Blacklist (false);
    }

    else
    {
      if (DeleteFileW (blacklist_path.c_str ()))
        return _Blacklist (blacklist);
      else
        return _Blacklist (true);
    }
  }

  return _Blacklist (blacklist);
}

bool
app_launch_config_s::isBlacklisted (bool refresh)
{
  if (blacklisted != -1 && ! refresh)
    return blacklisted;

  std::wstring full_path =
    getBlacklistFilename ( );

  // getBlacklistFilename ( ) can set blacklisted == 1 for
  //   invalid launch configs, requiring no duplicate testing
  if (blacklisted == -1 || refresh)
    blacklisted =
      PathFileExistsW (full_path.c_str ());

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

bool
app_launch_config_s::setElevated (bool elevate)
{
  std::wstring elevated_path =
    getElevatedFilename ( );

  auto _Elevate =
  [&](bool set) -> bool
  {
    elevated =
      PathFileExistsW (elevated_path.c_str ()) ?
                                              1 : 0;

    assert (set == (bool)elevated);

    UNREFERENCED_PARAMETER (set);

    return elevated;
  };

  if (elevate != isElevated ( ))
  {
    if (elevate)
    {
      FILE* fElevate =
        _wfopen (elevated_path.c_str (), L"w+");

      if (fElevate != nullptr)
      {
        fputws (L"This file tells Special K Injection Frontend (SKIF) to elevate the injection services for the executable.", fElevate);
        fclose (           fElevate);

        return _Elevate (elevate);
      }

      return _Elevate (false);
    }

    else
    {
      if (DeleteFileW (elevated_path.c_str ()))
        return _Elevate (elevate);
      else
        return _Elevate (true);
    }
  }

  return _Elevate (elevate);
}

bool
app_launch_config_s::isElevated (bool refresh)
{
  if (elevated != -1 && ! refresh)
    return elevated;

  std::wstring full_path =
    getElevatedFilename ( );

  // getElevatedFilename ( ) can set elevated == 1 for
  //   invalid launch configs, requiring no duplicate testing
  if (elevated == -1 || refresh)
    elevated =
      PathFileExistsW (full_path.c_str ());

  return elevated;
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

  // We need to use proxy variables since we might remove a substring of the launch options
  std::wstring cmdLine = ctx.config->getLaunchOptions();
  bool useShellExecute = false; // This is in case a %COMMAND% Steam call is made

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
      PLOG_ERROR << "Unable to convert found Steam App ID to integer: " << wsSteamAppID;
      uiSteamAppID = 0;
    }
  }

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

    // If both of these are false, see if there is any custom Steam launch options to append...
    if (! ctx.config->custom_skif &&
        ! ctx.config->custom_user)
    {
      std::string steamLaunchOptions = GetStoreLaunchOptions (uiSteamAppID); // Launch options set in the store client (Steam only)

      if (steamLaunchOptions.size() > 0)
      {
        PLOG_DEBUG << "Found additional launch options for this app in Steam: " << steamLaunchOptions;

        std::string steamLaunchOptionsLower =
          SKIF_Util_ToLower (steamLaunchOptions);

        std::string sSteam_Command = "%command%"; // Only for Steam games
        size_t posSteam_Command_start = steamLaunchOptionsLower.find(sSteam_Command);

        // Check if SKIF %COMMAND% is being used, when we're trying to launch without Special K
        if (! ctx.usingSK && steamLaunchOptionsLower.find("skif ") == 0)
        {
          launchDecision = false;

          // Escape any percent signs (%) as otherwise ImGui won't be able to show the string properly
          for (auto pos  = steamLaunchOptions.find ('%');          // Find the first occurence
                    pos != std::string::npos;                      // Validate we're still in the string
                    steamLaunchOptions.insert      (pos, R"(%)"),  // Escape the character
                    pos  = steamLaunchOptions.find ('%', pos + 2)) // Find the next occurence
          { }

          SKIF_ImGui_InfoMessage ("Conflicting configuration",
                                  "Could not launch game due to conflicting launch options in Steam:\n"
                                  "\n"
                                +  steamLaunchOptions + "\n"
                                  "\n"
                                  "Please change the launch options in Steam before trying again.");

          PLOG_WARNING << "Steam game " << id << " (" << names.normal << ") was unable to launch due to a conflict with the launch option of Steam!";
        }

        // Check if a %COMMAND% is a part of the launch options
        // %COMMAND% is special in that the original developer-specified launch options are placed at its exact position,
        //   making the user-specified Steam launch options the _primary_ and the developer-specified launch options _secondary_
        else if (posSteam_Command_start != std::string::npos)
        {
          // Base is dev-specified executable
          std::wstring newCmdLine = ctx.config->getExecutableFullPath ( );
          std::wstring wsSteamLO  = SK_UTF8ToWideChar (steamLaunchOptions);

          // Appended is dev-specified cmd line arguments
          if (! cmdLine.empty())
            newCmdLine += L" " + cmdLine;

          // Replace %command% with the dev-specified exe and args
          wsSteamLO.replace(posSteam_Command_start, sSteam_Command.length(), newCmdLine);

          // Swap in the result and flag to use shell execute
          cmdLine = wsSteamLO;
          useShellExecute = true;
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
        proc->hProcess = sexi.hProcess;

      else {
        PLOG_ERROR << "Shell execute failed ?!";
        PLOG_ERROR << SKIF_Util_GetErrorAsWStr ( );

        proc->id       =  0;
        proc->store_id = -1;
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

      SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal),
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

std::wstring
app_record_s::GetDesktopShortcutArgs (void)
{
  std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", launch_configs[0].getExecutableFullPath().c_str(), launch_configs[0].getLaunchOptions().c_str());

  SKIF_Util_TrimTrailingSpacesW (linkArgs);

  return linkArgs;
}

#pragma endregion

#pragma region Run State

// Epic, GOG and SKIF Custom should be straight forward: match the full path of the primary launch config
app_record_s::ProcessMatch
app_record_s::MatchProcess (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen)
{
  UNREFERENCED_PARAMETER (wszExeFile);

  // Called by SKIF_LibRefreshWorker: only read the launch configs (operator [] would insert a missing entry)
  auto primary = launch_configs.find (0);

  if (primary == launch_configs.end ( ))
    return ProcessMatch::None;

  if (dwExePathLen != 0 && _wcsnicmp (primary->second.getExecutableFullPath ( ).c_str(), wszExePath, dwExePathLen) == 0) // full path
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

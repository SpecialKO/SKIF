#pragma once

#include <utility/utility.h>
#include <stores/Steam/steam_library.h>

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

std::string
app_branch_record_s::getTimeAsCStr (void) const
{
  if (! time_string.empty ())
    return time_string;

  wchar_t    wszSystemTime [64]  = { };
  wchar_t    wszSystemDate [64]  = { };
  wchar_t    wszBranchTime [128] = { };
  SYSTEMTIME stBranchTime        = { };
  FILETIME   ftBranchTime        =
    SK_Win32_time_t_to_FILETIME (time_updated);

  FileTimeToSystemTime ( &ftBranchTime, &stBranchTime );

  /* Old method
  GetDateFormat (LOCALE_USER_DEFAULT, DATE_AUTOLAYOUT,
    &stBranchTime, nullptr, wszSystemTime, 63);
  GetTimeFormat (LOCALE_USER_DEFAULT, TIME_NOSECONDS,
    &stBranchTime, nullptr, wszSystemDate, 63);
  */

  // New method -- Uses Ex functions since Microsoft recommends this
  // DATE_SHORTDATE solves | character in the date format caused by LTR / RTL markers that ImGui cannot handle properly
  GetDateFormatEx (LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE,
    &stBranchTime, NULL, wszSystemDate, 63, NULL);
  GetTimeFormatEx (LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS,
    &stBranchTime, NULL, wszSystemTime, 63);

  StringCchCatW (wszBranchTime, 127, wszSystemDate);
  StringCchCatW (wszBranchTime, 127, L" ");
  StringCchCatW (wszBranchTime, 127, wszSystemTime);

  const_cast <std::string&> (time_string) =
    SK_WideCharToUTF8 (wszBranchTime);

  return time_string;
}

std::string&
app_branch_record_s::getDescAsUTF8 (void)
{
  if (desc_utf8.empty ())
  {
    desc_utf8 =
      SK_WideCharToUTF8 (description);
  }

  return desc_utf8;
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
                    L"SteamAppNotify", TRUE, REG_NOTIFY_CHANGE_LAST_SET, true );

  extern INT64 current_time_ms;

  DWORD dwTimeNow =
    static_cast <DWORD>
      ( current_time_ms & 0xFFFFFFFFLL );

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
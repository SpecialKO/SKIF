#pragma once

#include <stores/Steam/app_record.h>

// Shorthand, because these are way too long
using app_launch_config_s =
      app_record_s::launch_config_s;
using app_branch_record_s =
      app_record_s::branch_record_s;

std::wstring
app_launch_config_s::getExecutableFullPath (int32_t appid)
{
  std::wstring exec_path =
    SK_UseManifestToGetInstallDir (appid);

  exec_path.append (L"\\");
  exec_path.append (executable);

  if (PathFileExistsW (exec_path.c_str ()))
    return exec_path;

  return L"<InvalidPath>";
}

std::wstring
app_launch_config_s::getExecutableDir (int32_t appid)
{
  std::wstring exec_path =
    getExecutableFullPath (appid);

  valid =
    PathFileExistsW (exec_path.c_str ());

  if (valid)
  {
    wchar_t  wszExecutableBase [MAX_PATH] = { };
    StrCatW (wszExecutableBase, exec_path.c_str ());

    PathRemoveFileSpecW (wszExecutableBase);

    return wszExecutableBase;
  }

  return L"<InvalidDir>";
}

std::wstring
app_launch_config_s::getBlacklistFilename (int32_t appid)
{
  if (! blacklist_file.empty ())
    return blacklist_file;

  std::wstring full_path =
    getExecutableFullPath (appid);

  valid =
    PathFileExistsW (full_path.c_str ());

  if (valid)
  {
    wchar_t wszExecutableBase [MAX_PATH] = { };
    wchar_t wszBlacklistPath  [MAX_PATH] = { };

    StrCatW (wszExecutableBase, executable.c_str ());
    StrCatW (wszBlacklistPath,  full_path.c_str  ());

    PathRemoveFileSpecW  ( wszBlacklistPath  );
    PathStripPathW       ( wszExecutableBase );
    PathRemoveExtensionW ( wszExecutableBase );

    swprintf_s ( wszBlacklistPath, MAX_PATH,
                   L"%ws\\SpecialK.deny.%ws",
                     wszBlacklistPath, wszExecutableBase );

    blacklist_file =
      wszBlacklistPath;
  }

  else
    blacklist_file =
      L"InvalidLaunchConfig.NeverInject";

  return blacklist_file;
}

bool
app_launch_config_s::setBlacklisted ( int32_t appid,
                                      bool    blacklist )
{
  std::wstring blacklist_path =
    getBlacklistFilename (appid);

  auto _Blacklist =
  [&](bool set) -> bool
  {
    blacklisted =
      PathFileExistsW (blacklist_path.c_str ()) ?
                                              1 : 0;

    assert (set == blacklisted);

    UNREFERENCED_PARAMETER (set);

    return blacklisted;
  };

  if (blacklist != isBlacklisted (appid))
  {
    if (blacklist)
    {
      FILE* fBlacklist =
        _wfopen (blacklist_path.c_str (), L"w+");

      if (fBlacklist != nullptr)
      {
        fputws (L"Dummy!", fBlacklist);
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
app_launch_config_s::isBlacklisted (int32_t appid)
{
  if (blacklisted != -1)
    return (blacklisted != 0);

  bool black =
    PathFileExistsW (
      getBlacklistFilename (appid).c_str ()
    );

  blacklisted = black ?
                    1 : 0;

  return black;
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

  GetDateFormat (LOCALE_USER_DEFAULT, DATE_AUTOLAYOUT,
    &stBranchTime, nullptr, wszSystemTime, 63);
  GetTimeFormat (LOCALE_USER_DEFAULT, TIME_NOSECONDS,
    &stBranchTime, nullptr, wszSystemDate, 63);

  StringCchCatW (wszBranchTime, 127, wszSystemTime);
  StringCchCatW (wszBranchTime, 127, L"  ");
  StringCchCatW (wszBranchTime, 127, wszSystemDate);

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


struct SKIF_RegistryWatch {
  SKIF_RegistryWatch ( HKEY   hRootKey,
              const wchar_t* wszSubKey )
  {
    _init.root    = hRootKey;
    _init.sub_key = wszSubKey;

    hEvent.m_h =
        CreateEvent ( nullptr, TRUE,
                               FALSE, L"SteamAppNotify" );

    reset ();
  }

  void registerNotify (DWORD dwMask)
  {
    hKeyBase.NotifyChangeKeyValue (
      TRUE, dwMask, hEvent.m_h
    );
  }

  void reset (void)
  {
    hKeyBase.Close ();

    if ((intptr_t)hEvent.m_h > 0)
      ResetEvent (hEvent.m_h);

    LSTATUS lStat =
      hKeyBase.Open ( _init.root,
                      _init.sub_key.c_str () );

    if (lStat == ERROR_SUCCESS)
    {
      registerNotify (
        _init.filter_mask
      );
    }
  }

  struct {
    HKEY         root;
    std::wstring sub_key;
    DWORD        filter_mask =
      REG_NOTIFY_CHANGE_LAST_SET;
  } _init;

  CRegKey hKeyBase;
  CHandle hEvent;

  bool isSignaled (void)
  {
    bool signaled =
      WaitForSingleObjectEx (
        hEvent.m_h, 0UL, FALSE
      ) == WAIT_OBJECT_0;

    if (signaled)
    {
      reset ();
    }

    return signaled;
  }
};

std::wstring
SKIF_Steam_GetAppStateString (       AppId_t  appid,
                               const wchar_t *wszStateKey )
{
  std::wstring str ( MAX_PATH, L'\0' );
  DWORD     len    = MAX_PATH;
  LSTATUS   status =
    RegGetValueW ( HKEY_CURRENT_USER,
      SK_FormatStringW ( LR"(SOFTWARE\Valve\Steam\Apps\%lu)",
                           appid ).c_str (),
                     wszStateKey,
                       RRF_RT_REG_SZ,
                         nullptr,
                           str.data (),
                             &len );

  if (status == ERROR_SUCCESS)
    return str;
  else
    return L"";
}

wchar_t
SKIF_Steam_GetAppStateDWORD (       AppId_t  appid,
                              const wchar_t *wszStateKey,
                                    DWORD   *pdwStateVal )
{
  DWORD     len    = sizeof (DWORD);
  LSTATUS   status =
    RegGetValueW ( HKEY_CURRENT_USER,
      SK_FormatStringW ( LR"(SOFTWARE\Valve\Steam\Apps\%lu)",
                           appid ).c_str (),
                     wszStateKey,
                       RRF_RT_DWORD,
                         nullptr,
                           pdwStateVal,
                             &len );

  if (status == ERROR_SUCCESS)
    return TRUE;
  else
    return FALSE;
}

bool
SKIF_Steam_UpdateAppState (app_record_s *pApp)
{
  if (! pApp)
    return false;

  SKIF_Steam_GetAppStateDWORD (
     pApp->id,   L"Installed",
    &pApp->_status.installed );

  if (pApp->_status.installed != 0x0)
  {   pApp->_status.running    = 0x0;

    SKIF_Steam_GetAppStateDWORD (
       pApp->id,   L"Running",
      &pApp->_status.running );

    if (! pApp->_status.running)
    {
      SKIF_Steam_GetAppStateDWORD (
         pApp->id,   L"Updating",
        &pApp->_status.updating );
    }

    else
    {
      pApp->_status.updating = 0x0;
    }

    if (pApp->names.normal.empty ())
    {
      std::wstring wide_name =
        SKIF_Steam_GetAppStateString (
          pApp->id,   L"Name"
        );

      if (! wide_name.empty ())
      {
        pApp->names.normal =
          SK_WideCharToUTF8 (wide_name);
      }
    }
  }

  return true;
}

DWORD app_record_s::client_state_s::_TimeLastNotified = 0UL;


bool
app_record_s::client_state_s::refresh (app_record_s *pApp)
{
  static constexpr
    DWORD _RefreshInterval = 333UL;

  static SKIF_RegistryWatch
    _appWatch ( HKEY_CURRENT_USER,
                  LR"(SOFTWARE\Valve\Steam\Apps)" );

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
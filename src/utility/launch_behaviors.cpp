#include <utility/launch_behaviors.h>

namespace SKIF_LaunchBehaviors
{
  static bool
  _ReadValue (HKEY hKey, const std::wstring& name, DWORD& out)
  {
    DWORD data = 0,
          size = sizeof (DWORD),
          type = REG_NONE;

    if (ERROR_SUCCESS != RegQueryValueExW (hKey, name.c_str ( ), nullptr, &type, reinterpret_cast <LPBYTE> (&data), &size) ||
        type          != REG_DWORD)
      return false;

    out = data;
    return true;
  }

  static bool
  _IsSupportedVersion (DWORD raw)
  {
    DWORD version = (raw & MaskVersion) >> ShiftVersion;

    return (version == 0 || version == CurrentVersion);
  }

  static bool
  _SetField (const std::wstring& path, DWORD mask, DWORD shift, DWORD value)
  {
    std::wstring name = NormalizePath (path);

    if (name.empty ( ))
      return false;

    HKEY hKey = nullptr;

    if (ERROR_SUCCESS != RegCreateKeyExW (HKEY_CURRENT_USER, RegistryPath, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &hKey, nullptr))
      return false;

    DWORD raw    = 0;
    bool  exists = _ReadValue (hKey, name, raw);
    bool  ret    = true;

    // Written by a newer version of SKIF -- leave it alone so we do not destroy fields we do not know about
    if (exists && ! _IsSupportedVersion (raw))
      ret = false;

    else
    {
      DWORD fields = ((raw & MaskFields) & ~mask) | ((value << shift) & mask);

      if (fields == 0)
      {
        if (exists)
          ret = (ERROR_SUCCESS == RegDeleteValueW (hKey, name.c_str ( )));
      }

      else
      {
        DWORD data = fields | (CurrentVersion << ShiftVersion);

        if (! exists || data != raw)
          ret = (ERROR_SUCCESS == RegSetValueExW (hKey, name.c_str ( ), 0, REG_DWORD, reinterpret_cast <const BYTE*> (&data), sizeof (DWORD)));
      }
    }

    RegCloseKey (hKey);

    return ret;
  }

  std::wstring
  NormalizePath (const std::wstring& path)
  {
    if (path.empty ( ))
      return L"";

    std::wstring full (MAX_PATH, L'\0');

    DWORD len =
      GetFullPathNameW (path.c_str ( ), static_cast <DWORD> (full.size ( )), full.data ( ), nullptr);

    // Buffer was too small -- len is the required size including the null terminator
    if (len >= full.size ( ))
    {
      full.resize (len);
      len =
        GetFullPathNameW (path.c_str ( ), static_cast <DWORD> (full.size ( )), full.data ( ), nullptr);
    }

    if (len == 0 || len >= full.size ( ))
      full = path;
    else
      full.resize (len);

    for (auto& ch : full)
      if (ch == L'/')
          ch =  L'\\';

    // Strip the \\?\ prefix of long local paths so they match their regular form
    if (full.rfind (LR"(\\?\)", 0) == 0 && full.length ( ) > 6 && full [5] == L':')
      full.erase (0, 4);

    if (! full.empty ( ))
      CharLowerBuffW (full.data ( ), static_cast <DWORD> (full.length ( )));

    while (full.length ( ) > 1 && full.back ( ) == L'\\')
      full.pop_back ( );

    return full;
  }

  static Resolved
  _FromRaw (DWORD raw)
  {
    Resolved ret;

    ret.autoStop      = static_cast <AutoStop>      ((raw & MaskAutoStop)      >> ShiftAutoStop);
    ret.service       = static_cast <Service>       ((raw & MaskService)       >> ShiftService);
    ret.steamAppIDtxt = static_cast <SteamAppIDtxt> ((raw & MaskSteamAppIDtxt) >> ShiftSteamAppIDtxt);

    return ret;
  }

  static Resolved
  _Resolve (const std::wstring& path, bool skipSelf)
  {
    Resolved ret;
    HKEY     hKey = nullptr;

    if (ERROR_SUCCESS != RegOpenKeyExW (HKEY_CURRENT_USER, RegistryPath, 0, KEY_QUERY_VALUE, &hKey))
      return ret;

    std::wstring current  = NormalizePath (path);
    DWORD        resolved = 0,
                 filled   = 0;

    // Start at the parent folder: what the path inherits if it has no entry of its own
    if (skipSelf)
    {
      size_t pos = current.find_last_of (L'\\');

      if (pos == std::wstring::npos || pos == 0)
        current.clear ( );
      else
        current.resize (pos);
    }

    static constexpr DWORD fieldMasks [] = { MaskAutoStop, MaskService, MaskSteamAppIDtxt };

    // The executable itself first, then each parent folder; the most specific non-zero value of each field wins
    while (! current.empty ( ) && filled != MaskKnownFields)
    {
      DWORD raw = 0;

      if (_ReadValue (hKey, current, raw) && _IsSupportedVersion (raw))
      {
        for (DWORD mask : fieldMasks)
        {
          if (! (filled & mask) && (raw & mask))
          {
            resolved |= (raw & mask);
            filled   |=        mask;
          }
        }
      }

      size_t pos = current.find_last_of (L'\\');

      if (pos == std::wstring::npos || pos == 0)
        break;

      current.resize (pos);
    }

    RegCloseKey (hKey);

    return _FromRaw (resolved);
  }

  Resolved
  Resolve (const std::wstring& path)
  {
    return _Resolve (path, false);
  }

  Resolved
  ResolveParent (const std::wstring& path)
  {
    return _Resolve (path, true);
  }

  bool
  ReadExact (const std::wstring& path, Resolved& out)
  {
    out = Resolved { };

    std::wstring name = NormalizePath (path);

    if (name.empty ( ))
      return false;

    HKEY hKey = nullptr;

    if (ERROR_SUCCESS != RegOpenKeyExW (HKEY_CURRENT_USER, RegistryPath, 0, KEY_QUERY_VALUE, &hKey))
      return false;

    DWORD raw    = 0;
    bool  exists = _ReadValue (hKey, name, raw) && _IsSupportedVersion (raw);

    RegCloseKey (hKey);

    if (exists)
      out = _FromRaw (raw);

    return exists;
  }

  bool
  SetAutoStop (const std::wstring& path, AutoStop value)
  {
    return _SetField (path, MaskAutoStop,      ShiftAutoStop,      static_cast <DWORD> (value));
  }

  bool
  SetService (const std::wstring& path, Service value)
  {
    return _SetField (path, MaskService,       ShiftService,       static_cast <DWORD> (value));
  }

  bool
  SetSteamAppIDtxt (const std::wstring& path, SteamAppIDtxt value)
  {
    return _SetField (path, MaskSteamAppIDtxt, ShiftSteamAppIDtxt, static_cast <DWORD> (value));
  }
};

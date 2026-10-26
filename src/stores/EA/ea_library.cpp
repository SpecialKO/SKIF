#include <stores/EA/ea_library.h>
#include <pugixml/src/pugixml.hpp>

#include <wtypes.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <mutex>
#include <set>

#include <utility/registry.h>
#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include <utility/fsutil.h>

// See ea_library.h for how EA app games are found and launched

#pragma region Helpers

namespace
{
  // An uninstall entry created by the EAInstaller
  struct ea_uninstall_entry_s {
    std::wstring key;          // Uninstall subkey name
    std::wstring install_dir;  // InstallLocation (normalized, not backslash-terminated)
    std::wstring display_name; // DisplayName
    std::wstring display_icon; // DisplayIcon (usually the game's executable)
    std::wstring installer;    // Folder name under Common Files\EAInstaller\ (from UninstallString)
  };

  constexpr const wchar_t* UNINSTALL_KEY   = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall)";
  constexpr const wchar_t* EA_INSTALLER    = LR"(\EAInstaller\)";

  // Reads a string value (REG_EXPAND_SZ values are expanded); empty if missing
  std::wstring
  SKIF_EA_GetRegString (HKEY hKey, const wchar_t* wszSubKey, const wchar_t* wszValue)
  {
    DWORD dwSize = 0;

    if (RegGetValueW (hKey, wszSubKey, wszValue, RRF_RT_REG_SZ, NULL, NULL, &dwSize) != ERROR_SUCCESS || dwSize < sizeof (wchar_t))
      return L"";

    std::wstring data;
    LSTATUS      ls = ERROR_MORE_DATA;

    // The size can change between the calls (expansion, or the value being written to)
    for (int attempt = 0; attempt < 4 && ls == ERROR_MORE_DATA; attempt++)
    {
      data.resize (dwSize / sizeof (wchar_t) + 1);
      dwSize = static_cast <DWORD> (data.size ( ) * sizeof (wchar_t));
      ls     = RegGetValueW (hKey, wszSubKey, wszValue, RRF_RT_REG_SZ, NULL, data.data ( ), &dwSize);
    }

    if (ls != ERROR_SUCCESS)
      return L"";

    data.resize (wcsnlen (data.c_str ( ), data.size ( )));

    return data;
  }

  std::string
  SKIF_EA_Trim (std::string str)
  {
    const char* whitespace = " \t\r\n";

    size_t first = str.find_first_not_of (whitespace);

    if (first == std::string::npos)
      return "";

    return str.substr (first, str.find_last_not_of (whitespace) - first + 1);
  }

  // Strips surrounding quotes and spaces, uses backslashes, and removes the trailing backslash (except for a drive root)
  std::wstring
  SKIF_EA_NormalizePath (std::wstring path)
  {
    const wchar_t* trim = L" \t\"";

    size_t first = path.find_first_not_of (trim);

    if (first == std::wstring::npos)
      return L"";

    path = path.substr (first, path.find_last_not_of (trim) - first + 1);

    std::replace (path.begin ( ), path.end ( ), L'/', L'\\');

    path = std::filesystem::path (path).lexically_normal ( ).wstring ( );

    while (path.size ( ) > 3 && path.back ( ) == L'\\')
      path.pop_back ( );

    return path;
  }

  // <dir>\<file>, without doubled backslashes
  std::wstring
  SKIF_EA_CombinePath (std::wstring dir, std::wstring file)
  {
    dir = SKIF_EA_NormalizePath (dir);

    if (dir.empty ( ))
      return L"";

    std::replace (file.begin ( ), file.end ( ), L'/', L'\\');

    while (! file.empty ( ) && file.front ( ) == L'\\')
      file.erase (0, 1);

    if (file.empty ( ))
      return dir;

    if (dir.back ( ) != L'\\')
      dir += L'\\';

    return SKIF_EA_NormalizePath (dir + file);
  }

  // Resolves a path from installerdata.xml. They are usually a registry reference followed by a relative path:
  //   [HKEY_LOCAL_MACHINE\SOFTWARE\JoMei\SeaOfSolitude\Install Dir]SeaOfSolitude.exe
  // The referenced value is the game folder. If it is missing, or points somewhere the file does not exist
  //   while it does exist in the install folder (e.g. the game was moved), the install folder is used instead.
  std::wstring
  SKIF_EA_ResolveManifestPath (const std::wstring& path, const std::wstring& install_dir)
  {
    if (path.empty ( ))
      return L"";

    if (path.front ( ) != L'[')
    {
      if (PathIsRelativeW (path.c_str ( )))
        return SKIF_EA_CombinePath (install_dir, path);

      return SKIF_EA_NormalizePath (path);
    }

    size_t close = path.find (L']');

    if (close == std::wstring::npos)
      return L"";

    std::wstring reference = path.substr (1, close - 1),
                 file      = path.substr (close + 1),
                 fromRegistry;

    size_t hiveEnd    = reference.find          (L'\\'),
           valueStart = reference.find_last_of  (L'\\');

    if (hiveEnd != std::wstring::npos && valueStart > hiveEnd)
    {
      std::wstring hive   = reference.substr (0, hiveEnd),
                   subKey = reference.substr (hiveEnd + 1, valueStart - hiveEnd - 1),
                   value  = reference.substr (valueStart + 1);

      HKEY hRoot = (_wcsicmp (hive.c_str ( ), L"HKEY_LOCAL_MACHINE") == 0 || _wcsicmp (hive.c_str ( ), L"HKLM") == 0) ? HKEY_LOCAL_MACHINE
                 : (_wcsicmp (hive.c_str ( ), L"HKEY_CURRENT_USER")  == 0 || _wcsicmp (hive.c_str ( ), L"HKCU") == 0) ? HKEY_CURRENT_USER
                 : NULL;

      // The EAInstaller is 32-bit, so try the 32-bit view first
      for (REGSAM view : { KEY_WOW64_32KEY, KEY_WOW64_64KEY })
      {
        if (hRoot == NULL || ! fromRegistry.empty ( ))
          break;

        HKEY hKey = nullptr;

        if (RegOpenKeyExW (hRoot, subKey.c_str ( ), 0, KEY_QUERY_VALUE | view, &hKey) == ERROR_SUCCESS)
        {
          fromRegistry = SKIF_EA_GetRegString (hKey, NULL, value.c_str ( ));
          RegCloseKey (hKey);
        }
      }
    }

    std::wstring candidate = (fromRegistry.empty ( )) ? L"" : SKIF_EA_CombinePath (fromRegistry, file),
                 fallback  =                                  SKIF_EA_CombinePath (install_dir,  file);

    if (! candidate.empty ( ) && (PathFileExistsW (candidate.c_str ( )) || ! PathFileExistsW (fallback.c_str ( ))))
      return candidate;

    return fallback;
  }

  // The value of the child element with locale en_US, else the first non-empty one.
  //   valueChild: read the value from this child of the element instead (<localeInfo locale="en_US"><title>)
  std::string
  SKIF_EA_GetLocalized (pugi::xml_node parent, const char* name, const char* valueChild = nullptr)
  {
    std::string first;

    for (pugi::xml_node node = parent.child (name); node; node = node.next_sibling (name))
    {
      std::string value = SKIF_EA_Trim ((valueChild != nullptr) ? node.child_value (valueChild) : node.child_value ( ));

      if (value.empty ( ))
        continue;

      if (_stricmp (node.attribute ("locale").value ( ), "en_US") == 0)
        return value;

      if (first.empty ( ))
        first = value;
    }

    return first;
  }

  bool
  SKIF_EA_Is64BitOS (void)
  {
#ifdef _WIN64
    return true;
#else
    BOOL bWow64 = FALSE;
    return (IsWow64Process (GetCurrentProcess ( ), &bWow64) && bWow64);
#endif
  }

  // All uninstall entries whose UninstallString runs a tool in Common Files\EAInstaller\, from both registry views
  std::vector <ea_uninstall_entry_s>
  SKIF_EA_GetUninstallEntries (void)
  {
    std::vector <ea_uninstall_entry_s> entries;
    std::set    <std::wstring>         seen; // Lower-cased install folders

    for (REGSAM view : { KEY_WOW64_32KEY, KEY_WOW64_64KEY })
    {
      HKEY hKey = nullptr;

      if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, KEY_READ | view, &hKey) != ERROR_SUCCESS)
        continue;

      wchar_t szSubKey [256] = { };

      for (DWORD dwIndex = 0; ; dwIndex++)
      {
        DWORD   dwSize = _countof (szSubKey);
        LSTATUS ls     = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (ls == ERROR_NO_MORE_ITEMS)
          break;

        if (ls != ERROR_SUCCESS)
          continue;

        std::wstring uninstall = SKIF_EA_GetRegString (hKey, szSubKey, L"UninstallString");
        const wchar_t* pos     = StrStrIW (uninstall.c_str ( ), EA_INSTALLER);

        if (pos == nullptr)
          continue;

        ea_uninstall_entry_s entry;
        entry.key          = szSubKey;
        entry.install_dir  = SKIF_EA_NormalizePath (SKIF_EA_GetRegString (hKey, szSubKey, L"InstallLocation"));
        entry.display_name = SKIF_EA_GetRegString (hKey, szSubKey, L"DisplayName");
        entry.display_icon = SKIF_EA_GetRegString (hKey, szSubKey, L"DisplayIcon");

        // "...\EAInstaller\Reckoning\Cleanup.exe" -> Reckoning
        std::wstring installer = pos + wcslen (EA_INSTALLER);
        entry.installer        = installer.substr (0, installer.find (L'\\'));

        if (entry.install_dir.empty ( ))
        {
          PLOG_WARNING << "EA uninstall entry " << entry.key << " has no InstallLocation; skipping...";
          continue;
        }

        if (seen.insert (SKIF_Util_ToLowerW (entry.install_dir)).second)
          entries.emplace_back (std::move (entry));
      }

      RegCloseKey (hKey);
    }

    return entries;
  }

  // Identifies the current set of EA uninstall entries, to tell EA installs apart from other software changing the Uninstall key
  std::wstring
  SKIF_EA_GetFingerprint (void)
  {
    std::wstring fingerprint;

    for (auto& entry : SKIF_EA_GetUninstallEntries ( ))
      fingerprint += entry.key + L"|" + entry.install_dir + L"\n";

    return fingerprint;
  }

  // A shortcut (.lnk) to an executable, from the desktops
  struct ea_shortcut_s {
    std::wstring name;        // File name without .lnk
    std::wstring target;      // Executable (normalized)
    std::wstring working_dir;
    std::wstring arguments;
  };

  // ANSI (system code page) string to UTF-16
  std::wstring
  SKIF_EA_AnsiToWide (const char* str, size_t len)
  {
    if (len == 0)
      return L"";

    int chars = MultiByteToWideChar (CP_ACP, 0, str, static_cast <int> (len), nullptr, 0);

    if (chars <= 0)
      return L"";

    std::wstring wide (static_cast <size_t> (chars), L'\0');
    MultiByteToWideChar (CP_ACP, 0, str, static_cast <int> (len), wide.data ( ), chars);

    return wide;
  }

  // [lnk-parser-begin]
  // The fields of a shortcut file needed here, as stored (environment variables not expanded)
  struct ea_lnk_s {
    std::wstring target;      // LinkInfo local path, else the environment variable block's target
    std::wstring relative;    // Relative path (from the shortcut's folder)
    std::wstring working_dir;
    std::wstring arguments;
  };

  // Parses a shell link (.lnk) file as specified by [MS-SHLLINK], without the shell (no COM, no I/O beyond the read).
  //   Returns false if the data is not a shell link or is truncated.
  bool
  SKIF_EA_ParseShortcut (const std::vector <uint8_t>& data, ea_lnk_s& lnk)
  {
    static const uint8_t
      LINK_CLSID [16] = { 0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }; // 00021401-0000-0000-C000-000000000046

    enum : uint32_t {
      HasLinkTargetIDList = 0x00000001,
      HasLinkInfo         = 0x00000002,
      HasName             = 0x00000004,
      HasRelativePath     = 0x00000008,
      HasWorkingDir       = 0x00000010,
      HasArguments        = 0x00000020,
      HasIconLocation     = 0x00000040,
      IsUnicode           = 0x00000080,
      ForceNoLinkInfo     = 0x00000100,
      HasExpString        = 0x00000200
    };

    const size_t size = data.size ( );

    auto _U16 = [&](size_t off, uint32_t& value) -> bool
    {
      if (off + 2 > size) return false;
      value = static_cast <uint32_t> (data [off] | (data [off + 1] << 8));
      return true;
    };

    auto _U32 = [&](size_t off, uint32_t& value) -> bool
    {
      if (off + 4 > size) return false;
      value = static_cast <uint32_t> (data [off]) | (static_cast <uint32_t> (data [off + 1]) << 8) | (static_cast <uint32_t> (data [off + 2]) << 16) | (static_cast <uint32_t> (data [off + 3]) << 24);
      return true;
    };

    // UTF-16LE: up to `chars` characters, or up to a NUL within [off, end) when chars is SIZE_MAX
    auto _Wide = [&](size_t off, size_t end, size_t chars) -> std::wstring
    {
      std::wstring str;

      for (size_t i = off; i + 1 < end && i + 1 < size && str.size ( ) < chars; i += 2)
      {
        wchar_t c = static_cast <wchar_t> (data [i] | (data [i + 1] << 8));

        if (c == L'\0' && chars == SIZE_MAX)
          break;

        str += c;
      }

      return str;
    };

    // ANSI: NUL-terminated within [off, end)
    auto _Ansi = [&](size_t off, size_t end) -> std::wstring
    {
      end = (std::min) (end, size);

      if (off >= end)
        return L"";

      size_t len = 0;
      while (off + len < end && data [off + len] != 0)
        len++;

      return SKIF_EA_AnsiToWide (reinterpret_cast <const char*> (&data [off]), len);
    };

    // ShellLinkHeader
    uint32_t headerSize = 0, flags = 0;

    if (! _U32 (0, headerSize) || headerSize != 0x4C || size < 0x4C || memcmp (&data [4], LINK_CLSID, 16) != 0)
      return false;

    _U32 (0x14, flags);

    size_t off = 0x4C;

    // LinkTargetIDList (the target as a shell item ID list; not needed when LinkInfo has the path)
    if (flags & HasLinkTargetIDList)
    {
      uint32_t idListSize = 0;

      if (! _U16 (off, idListSize))
        return false;

      off += 2 + idListSize;
    }

    // LinkInfo
    if (flags & HasLinkInfo)
    {
      uint32_t infoSize = 0, infoHeaderSize = 0, infoFlags = 0;

      if (! _U32 (off, infoSize) || infoSize < 0x1C || off + infoSize > size)
        return false;

      _U32 (off + 0x04, infoHeaderSize);
      _U32 (off + 0x08, infoFlags);

      const size_t infoEnd = off + infoSize;

      // VolumeIDAndLocalBasePath (a local target; network targets are not used for games)
      if (! (flags & ForceNoLinkInfo) && (infoFlags & 0x1))
      {
        uint32_t baseOffset = 0, suffixOffset = 0, baseOffsetUnicode = 0, suffixOffsetUnicode = 0;

        _U32 (off + 0x10, baseOffset);
        _U32 (off + 0x18, suffixOffset);

        if (infoHeaderSize >= 0x24)
        {
          _U32 (off + 0x1C, baseOffsetUnicode);
          _U32 (off + 0x20, suffixOffsetUnicode);
        }

        std::wstring base   = (baseOffsetUnicode   != 0) ? _Wide (off + baseOffsetUnicode,   infoEnd, SIZE_MAX)
                            : (baseOffset          != 0) ? _Ansi (off + baseOffset,          infoEnd) : L"";
        std::wstring suffix = (suffixOffsetUnicode != 0) ? _Wide (off + suffixOffsetUnicode, infoEnd, SIZE_MAX)
                            : (suffixOffset        != 0) ? _Ansi (off + suffixOffset,        infoEnd) : L"";

        lnk.target = base + suffix;
      }

      off = infoEnd;
    }

    // StringData: counted strings, in this order
    const bool unicode = (flags & IsUnicode) != 0;

    auto _String = [&](std::wstring* out) -> bool
    {
      uint32_t chars = 0;

      if (! _U16 (off, chars))
        return false;

      off += 2;

      size_t bytes = (unicode) ? chars * 2 : chars;

      if (off + bytes > size)
        return false;

      if (out != nullptr)
        *out = (unicode) ? _Wide (off, off + bytes, chars)
                         : SKIF_EA_AnsiToWide (reinterpret_cast <const char*> (&data [off]), chars);

      off += bytes;

      return true;
    };

    if ((flags & HasName)         && ! _String (nullptr))          return false;
    if ((flags & HasRelativePath) && ! _String (&lnk.relative))    return false;
    if ((flags & HasWorkingDir)   && ! _String (&lnk.working_dir)) return false;
    if ((flags & HasArguments)    && ! _String (&lnk.arguments))   return false;
    if ((flags & HasIconLocation) && ! _String (nullptr))          return false;

    // ExtraData: the EnvironmentVariableDataBlock holds the target when it was given with environment variables
    if (lnk.target.empty ( ) && (flags & HasExpString))
    {
      uint32_t blockSize = 0, signature = 0;

      while (_U32 (off, blockSize) && blockSize >= 8 && off + blockSize <= size)
      {
        _U32 (off + 4, signature);

        if (signature == 0xA0000001 && blockSize >= 0x314)
        {
          lnk.target = _Wide (off + 8 + 260, off + 8 + 260 + 520, SIZE_MAX); // TargetUnicode

          if (lnk.target.empty ( ))
            lnk.target = _Ansi (off + 8, off + 8 + 260);                     // TargetAnsi

          break;
        }

        off += blockSize;
      }
    }

    return true;
  }
  // [lnk-parser-end]

  std::wstring
  SKIF_EA_ExpandEnvironment (const std::wstring& str)
  {
    if (str.find (L'%') == std::wstring::npos)
      return str;

    DWORD chars = ExpandEnvironmentStringsW (str.c_str ( ), nullptr, 0);

    if (chars == 0)
      return str;

    std::wstring expanded (chars, L'\0');

    if (ExpandEnvironmentStringsW (str.c_str ( ), expanded.data ( ), chars) == 0)
      return str;

    expanded.resize (wcsnlen (expanded.c_str ( ), expanded.size ( )));

    return expanded;
  }

  // Reads the shortcuts to executables on the public and the user's desktop (not their subfolders).
  //   The EA app creates desktop shortcuts for all users (C:\Users\Public\Desktop), named after the game.
  //   The Start menus are not read: they hold far more shortcuts (100+ is common).
  //   The .lnk files are parsed directly: loading them through the shell (IShellLinkW) cost ~5 ms each.
  std::vector <ea_shortcut_s>
  SKIF_EA_GetShortcuts (void)
  {
    std::vector <ea_shortcut_s> shortcuts;
    std::set    <std::wstring>  seen; // Lower-cased targets

    {
      auto _Load = [&](const std::filesystem::path& file)
      {
        if (_wcsicmp (file.extension ( ).c_str ( ), L".lnk") != 0)
          return;

        std::error_code ec;
        auto fileSize = std::filesystem::file_size (file, ec);

        if (ec || fileSize < 0x4C || fileSize > 1024 * 1024) // Shortcuts are a few KB
          return;

        std::ifstream stream (file, std::ios::binary);

        if (! stream)
          return;

        std::vector <uint8_t> data (static_cast <size_t> (fileSize));

        if (! stream.read (reinterpret_cast <char*> (data.data ( )), static_cast <std::streamsize> (data.size ( ))))
          return;

        ea_lnk_s lnk;

        if (! SKIF_EA_ParseShortcut (data, lnk))
        {
          PLOG_VERBOSE << "Could not parse the shortcut " << file.wstring ( );
          return;
        }

        // Same order as the shell: the stored target, else the relative path from the shortcut's folder
        std::wstring target = SKIF_EA_ExpandEnvironment (lnk.target);

        if (target.empty ( ) && ! lnk.relative.empty ( ))
          target = SKIF_EA_CombinePath (file.parent_path ( ).wstring ( ), SKIF_EA_ExpandEnvironment (lnk.relative));

        ea_shortcut_s shortcut;
        shortcut.target = SKIF_EA_NormalizePath (target);

        if (shortcut.target.size ( ) <= 4 || _wcsicmp (shortcut.target.c_str ( ) + shortcut.target.size ( ) - 4, L".exe") != 0)
          return;

        // The EAInstaller's own tools (touchup.exe, Cleanup.exe)
        if (StrStrIW (shortcut.target.c_str ( ), LR"(\__Installer\)") != nullptr ||
            StrStrIW (shortcut.target.c_str ( ), EA_INSTALLER)        != nullptr)
          return;

        if (! seen.insert (SKIF_Util_ToLowerW (shortcut.target)).second)
          return;

        shortcut.working_dir = SKIF_EA_NormalizePath (SKIF_EA_ExpandEnvironment (lnk.working_dir));
        shortcut.arguments   = lnk.arguments;

        shortcut.name = file.stem ( ).wstring ( );

        shortcuts.emplace_back (std::move (shortcut));
      };

      const KNOWNFOLDERID* folders [] = {
        &FOLDERID_PublicDesktop, &FOLDERID_Desktop
      };

      for (auto folder : folders)
      {
        PWSTR pwszPath = nullptr;

        if (SUCCEEDED (SHGetKnownFolderPath (*folder, KF_FLAG_DEFAULT, nullptr, &pwszPath)))
        {
          std::error_code ec;

          for (auto it  = std::filesystem::directory_iterator (pwszPath, std::filesystem::directory_options::skip_permission_denied, ec);
                    it != std::filesystem::directory_iterator ( ) && ! ec;
                    it.increment (ec))
          {
            if (! it->is_directory (ec))
              _Load (it->path ( ));
          }
        }

        CoTaskMemFree (pwszPath);
      }
    }

    return shortcuts;
  }

  // The shortcut whose executable is inside the install folder; one named after the game is preferred
  const ea_shortcut_s*
  SKIF_EA_FindShortcut (const std::vector <ea_shortcut_s>& shortcuts, const std::wstring& install_dir, const std::vector <std::wstring>& names)
  {
    std::wstring prefix = SKIF_Util_ToLowerW (install_dir);

    if (prefix.empty ( ))
      return nullptr;

    if (prefix.back ( ) != L'\\')
      prefix += L'\\';

    const ea_shortcut_s* found = nullptr;

    for (auto& shortcut : shortcuts)
    {
      if (SKIF_Util_ToLowerW (shortcut.target).rfind (prefix, 0) != 0 || ! PathFileExistsW (shortcut.target.c_str ( )))
        continue;

      for (auto& name : names)
        if (! name.empty ( ) && _wcsicmp (shortcut.name.c_str ( ), name.c_str ( )) == 0)
          return &shortcut;

      if (found == nullptr)
        found = &shortcut;
    }

    return found;
  }
}

#pragma endregion

#pragma region Library

void
SKIF_EA_GetInstalledAppIDs (app_list_t *apps)
{
  if (apps == nullptr)
    return;

  const bool is64BitOS = SKIF_EA_Is64BitOS ( );

  std::set <std::string> keys; // Persistent keys already added

  std::vector <ea_shortcut_s> shortcuts;     // Read when a game first needs them (see below)
  bool                        shortcutsRead = false;

  for (auto& entry : SKIF_EA_GetUninstallEntries ( ))
  {
    // EA games installed through Steam are listed by the Steam scan
    if (StrStrIW (entry.install_dir.c_str ( ), LR"(\steamapps\)") != nullptr)
    {
      PLOG_VERBOSE << "EA uninstall entry " << entry.key << " is a Steam install; skipping...";
      continue;
    }

    pugi::xml_document manifest;
    std::wstring       manifestPath = entry.install_dir + LR"(\__Installer\installerdata.xml)";
    pugi::xml_node     root;

    if (manifest.load_file (manifestPath.c_str ( )))
      root = manifest.document_element ( );
    else
      PLOG_WARNING << "Could not read the EA manifest: " << manifestPath;

    std::vector <std::string> contentIDs;
    std::string               title;

    if (root)
    {
      for (pugi::xml_node contentID = root.child ("contentIDs").child ("contentID"); contentID; contentID = contentID.next_sibling ("contentID"))
      {
        std::string id = SKIF_EA_Trim (contentID.child_value ( ));

        if (! id.empty ( ) && std::find (contentIDs.begin ( ), contentIDs.end ( ), id) == contentIDs.end ( ))
          contentIDs.push_back (id);
      }

      title = SKIF_EA_GetLocalized (root.child ("gameTitles"), "gameTitle");

      // Older manifests (<game manifestVersion="3.0">): <metadata><localeInfo locale="en_US"><title>
      if (title.empty ( ))
        title = SKIF_EA_GetLocalized (root.child ("metadata"), "localeInfo", "title");
    }

    if (title.empty ( ))
      title = SK_WideCharToUTF8 (entry.display_name);

    if (title.empty ( ))
      title = SK_WideCharToUTF8 (std::filesystem::path (entry.install_dir).filename ( ).wstring ( ));

    std::string key = (! contentIDs.empty ( )) ? contentIDs.front ( ) : SK_WideCharToUTF8 (entry.key);

    if (! keys.insert (key).second)
    {
      PLOG_VERBOSE << "EA uninstall entry " << entry.key << " is a duplicate of " << key << "; skipping...";
      continue;
    }

    // Hash the key into a unique integer we use for internal tracking purposes (0 is not a valid ID)
    std::hash <std::string> stoi_hasher;
    uint32_t appid = static_cast <uint32_t> (stoi_hasher (key));

    if (appid == 0)
      appid = 1;

    app_record_ea_s record (appid);

    record.store_utf8           = "EA";
    record._status.installed    = true;
    record.install_dir          = entry.install_dir;
    record.names.normal         = title;
    record.names.original       = title;

    record.ea.key               = key;
    record.ea.uninstall_key     = SK_WideCharToUTF8 (entry.key);
    record.ea.installer         = SK_WideCharToUTF8 (entry.installer);
    record.ea.manifest          = (! root) ? "" : (root.attribute ("version")) ? root.attribute ("version").value ( ) : root.attribute ("manifestVersion").value ( );

    for (auto& id : contentIDs)
      record.ea.offer_ids      += ((record.ea.offer_ids.empty ( )) ? "" : ",") + id;

    // One launch config per launcher in the manifest, in the manifest's order
    int lcId = 0;

    if (root)
    {
      for (pugi::xml_node launcher = root.child ("runtime").child ("launcher"); launcher; launcher = launcher.next_sibling ("launcher"))
      {
        if (launcher.child ("trial").text ( ).as_bool ( ))
          continue;

        if (launcher.child ("requires64BitOS").text ( ).as_bool ( ) && ! is64BitOS)
          continue;

        std::wstring exePath =
          SKIF_EA_ResolveManifestPath (SK_UTF8ToWideChar (SKIF_EA_Trim (launcher.child_value ("filePath"))), record.install_dir);

        if (exePath.empty ( ))
          continue;

        app_record_s::launch_config_s lc;
        lc.id                   = lcId;
        lc.valid                = 1;
        lc.executable_path      = exePath;
        lc.executable           = std::filesystem::path (exePath).filename ( ).wstring ( );
        lc.install_dir          = record.install_dir;
        lc.launch_options       = SK_UTF8ToWideChar (SKIF_EA_Trim (launcher.child_value ("parameters")));
        lc.description          = SK_UTF8ToWideChar (SKIF_EA_GetLocalized (launcher, "name"));

        record.launch_configs.emplace (lcId++, lc);
      }
    }

    // No usable launcher in the manifest (older manifests have none): use the shortcut the EA app created,
    //   i.e. one whose executable is inside the install folder (preferably named after the game)
    if (record.launch_configs.empty ( ))
    {
      if (! shortcutsRead)
      {
        DWORD dwStart = SKIF_Util_timeGetTime1 ( );

        shortcuts     = SKIF_EA_GetShortcuts ( );
        shortcutsRead = true;

        PLOG_VERBOSE << "Read " << shortcuts.size ( ) << " shortcuts to executables in " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms.";
      }

      if (const ea_shortcut_s* shortcut = SKIF_EA_FindShortcut (shortcuts, record.install_dir, { SK_UTF8ToWideChar (title), entry.display_name }))
      {
        PLOG_VERBOSE << "Using the shortcut \"" << shortcut->name << "\" for EA game " << SK_UTF8ToWideChar (title) << ": " << shortcut->target;

        app_record_s::launch_config_s lc;
        lc.id                   = 0;
        lc.valid                = 1;
        lc.executable_path      = shortcut->target;
        lc.executable           = std::filesystem::path (shortcut->target).filename ( ).wstring ( );
        lc.install_dir          = record.install_dir;
        lc.working_dir          = shortcut->working_dir;
        lc.launch_options       = shortcut->arguments;

        record.launch_configs.emplace (0, lc);
      }
    }

    // Still nothing: the uninstall entry's icon is sometimes the game's executable
    if (record.launch_configs.empty ( ))
    {
      std::wstring icon = entry.display_icon;

      // "C:\Games\Game\Game.exe",0 -> C:\Games\Game\Game.exe
      size_t comma = icon.find_last_of (L',');
      if (comma != std::wstring::npos && icon.find (L'.', comma) == std::wstring::npos)
        icon.erase (comma);

      icon = SKIF_EA_NormalizePath (icon);

      if (icon.size ( ) > 4 && _wcsicmp (icon.c_str ( ) + icon.size ( ) - 4, L".exe") == 0 &&
          StrStrIW (icon.c_str ( ), EA_INSTALLER) == nullptr)
      {
        app_record_s::launch_config_s lc;
        lc.id                   = 0;
        lc.valid                = 1;
        lc.executable_path      = icon;
        lc.executable           = std::filesystem::path (icon).filename ( ).wstring ( );
        lc.install_dir          = record.install_dir;

        record.launch_configs.emplace (0, lc);
      }
    }

    if (record.launch_configs.empty ( ))
    {
      PLOG_WARNING << "Could not find the executable of EA game " << SK_UTF8ToWideChar (title) << " (" << entry.install_dir << "); skipping...";
      continue;
    }

    record.specialk.injection.injection.type = InjectionType::Global;

    // Strip invalid filename characters
    record.specialk.profile_dir_utf8 = SKIF_Util_StripInvalidFilenameChars (title);
    record.specialk.profile_dir      = SK_UTF8ToWideChar (record.specialk.profile_dir_utf8);

    std::string entry_name = record.names.normal; // Copied first: the record is moved from below

    app_entry_t
      EA_entry (std::move (entry_name), std::make_unique <app_record_ea_s> (std::move (record)));

    apps->emplace_back (std::move (EA_entry));
  }
}

bool
SKIF_EA_hasInstalledGamesChanged (void)
{
  static DWORD        dwLastSignalCheck = 0;
  static std::wstring fingerprint       = SKIF_EA_GetFingerprint ( );

  bool signal = false;
  if (SKIF_Util_timeGetTime ( ) > dwLastSignalCheck + 5000)
  {
    // The EAInstaller is 32-bit, but watch both registry views
    static SKIF_RegistryWatch
      appWatch32 ( HKEY_LOCAL_MACHINE, UNINSTALL_KEY,
                     L"EAInstallNotify32", TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, UITab_None, true,  false), // UITab_Library
      appWatch64 ( HKEY_LOCAL_MACHINE, UNINSTALL_KEY,
                     L"EAInstallNotify64", TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, UITab_None, false, true);  // UITab_Library

    // Both are checked so that both are re-armed
    bool changed32 = appWatch32.isSignaled ( );
    bool changed64 = appWatch64.isSignaled ( );

    // The Uninstall key changes whenever any software is installed or updated: only repopulate when the EA games changed
    if (changed32 || changed64)
    {
      std::wstring current = SKIF_EA_GetFingerprint ( );

      if (current != fingerprint)
      {
        fingerprint = std::move (current);
        signal      = true;
      }
    }

    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}

bool
SKIF_EA_IsClientInstalled (void)
{
  HKEY hKey = nullptr;

  if (RegOpenKeyExW (HKEY_CLASSES_ROOT, LR"(origin2\shell\open\command)", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return false;

  RegCloseKey (hKey);

  return true;
}

#pragma endregion

#pragma region Launching

bool
app_record_ea_s::PrefersInstantLaunch (bool fromGameMenu, launch_config_s* config) const
{
  // Fallback if the EA app is not installed, or there is no offer ID to launch through it
  if (ea.offer_ids.empty ( ) || ! SKIF_EA_IsClientInstalled ( ))
    return true;

  // The EA app always starts the default launcher, so the manifest's other launchers are run directly
  if (config != nullptr && config->id != 0)
    return true;

  return (! fromGameMenu && UsesInstantPlay (GetInstantPlayDefault ( )));
}

// Launch EA game through the EA app, unless an instant launch was requested
void
app_record_ea_s::Launch (const launch_context_s& ctx)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (ctx.instant || ea.offer_ids.empty ( ))
  {
    LaunchInstant (ctx);
    return;
  }

  PLOG_VERBOSE << "Performing an EA app launch...";

  // origin2://game/launch?offerIds=196173
  std::wstring launchOptions = SK_FormatStringW (LR"(origin2://game/launch?offerIds=%ws)", SK_UTF8ToWideChar (ea.offer_ids).c_str ( ));

  if (SKIF_Util_OpenURI (launchOptions) != 0)
  {
    std::scoped_lock app_lock (_games.GetMutex());

    // Don't check the running state for at least 7.5 seconds
    _status.dwTimeDelayChecks = ctx.timeNow + 7500;
    _status.running           = true;

    SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal), L"", launchOptions, L"", ctx.config->getExecutableFullPath ( ), (! ctx.localInjection && ctx.usingSK));
  }
}

#pragma endregion

#pragma region Library UI

bool
app_record_ea_s::GetInstantPlayDefault (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return _registry.bInstantPlayEA;
}

std::wstring
app_record_ea_s::GetStoreAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\EA\%ws\)",    _path_cache.specialk_userdata, SKIF_Util_StripInvalidFilenameChars (SK_UTF8ToWideChar (ea.key)).c_str());
}

std::vector <std::pair <std::string, std::string>>
app_record_ea_s::GetStoreDebugFields (void) const
{
  return {
    { "Content IDs",       ea.offer_ids     },
    { "Uninstall Key",     ea.uninstall_key },
    { "EAInstaller",       ea.installer     },
    { "Manifest Version",  ea.manifest      }
  };
}

#pragma endregion

#include <stores/Ubisoft/ubisoft_library.h>

#include <wtypes.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cctype>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>
#include <set>

#include <utility/registry.h>
#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include <utility/fsutil.h>
#include <utility/igdb.h>
#include <stores/generic_library2.h>

// See ubisoft_library.h for how Ubisoft Connect games are found and launched

#pragma region Helpers

namespace
{
  // The client is 32-bit: its keys are in the 32-bit registry view (WOW6432Node)
  constexpr const wchar_t* LAUNCHER_KEY      = LR"(SOFTWARE\Ubisoft\Launcher)";
  constexpr const wchar_t* INSTALLS_KEY      = LR"(SOFTWARE\Ubisoft\Launcher\Installs)";
  constexpr const wchar_t* UNINSTALL_KEY     = LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Uplay Install )"; // + product ID
  constexpr const wchar_t* CONFIGURATIONS    = LR"(cache\configuration\configurations)";                                 // In the client's folder

  // Reads a string value (REG_EXPAND_SZ values are expanded); empty if missing
  std::wstring
  SKIF_Ubisoft_GetRegString (HKEY hKey, const wchar_t* wszSubKey, const wchar_t* wszValue)
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

  // A string value of HKLM\<subKey> in the 32-bit registry view
  std::wstring
  SKIF_Ubisoft_GetRegString32 (const std::wstring& subKey, const wchar_t* wszValue)
  {
    std::wstring value;
    HKEY         hKey = nullptr;

    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, subKey.c_str ( ), 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
    {
      value = SKIF_Ubisoft_GetRegString (hKey, NULL, wszValue);
      RegCloseKey (hKey);
    }

    return value;
  }

  // Strips surrounding quotes and spaces, uses backslashes, and removes the trailing backslash (except for a drive root)
  std::wstring
  SKIF_Ubisoft_NormalizePath (std::wstring path)
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

  // <dir>\<file>, without doubled backslashes; "." and ".." in the file are resolved (.\bin\Game.exe)
  std::wstring
  SKIF_Ubisoft_CombinePath (std::wstring dir, std::wstring file)
  {
    dir = SKIF_Ubisoft_NormalizePath (dir);

    if (dir.empty ( ))
      return L"";

    std::replace (file.begin ( ), file.end ( ), L'/', L'\\');

    while (! file.empty ( ) && file.front ( ) == L'\\')
      file.erase (0, 1);

    if (file.empty ( ))
      return dir;

    if (dir.back ( ) != L'\\')
      dir += L'\\';

    return SKIF_Ubisoft_NormalizePath (dir + file);
  }

  // Reads a registry value given as <hive>\<key>\<value>, as the configurations reference them:
  //   HKEY_LOCAL_MACHINE\SOFTWARE\Ubisoft\Launcher\Installs\2010\InstallDir
  //   The client is 32-bit, so the 32-bit view is tried first.
  std::wstring
  SKIF_Ubisoft_ReadRegisterReference (const std::string& reference)
  {
    std::wstring ref = SK_UTF8ToWideChar (reference);

    size_t hiveEnd    = ref.find         (L'\\'),
           valueStart = ref.find_last_of (L'\\');

    if (hiveEnd == std::wstring::npos || valueStart <= hiveEnd)
      return L"";

    std::wstring hive   = ref.substr (0, hiveEnd),
                 subKey = ref.substr (hiveEnd + 1, valueStart - hiveEnd - 1),
                 value  = ref.substr (valueStart + 1);

    HKEY hRoot = (_wcsicmp (hive.c_str ( ), L"HKEY_LOCAL_MACHINE") == 0 || _wcsicmp (hive.c_str ( ), L"HKLM") == 0) ? HKEY_LOCAL_MACHINE
               : (_wcsicmp (hive.c_str ( ), L"HKEY_CURRENT_USER")  == 0 || _wcsicmp (hive.c_str ( ), L"HKCU") == 0) ? HKEY_CURRENT_USER
               : NULL;

    if (hRoot == NULL)
      return L"";

    for (REGSAM view : { KEY_WOW64_32KEY, KEY_WOW64_64KEY })
    {
      HKEY hKey = nullptr;

      if (RegOpenKeyExW (hRoot, subKey.c_str ( ), 0, KEY_QUERY_VALUE | view, &hKey) == ERROR_SUCCESS)
      {
        std::wstring data = SKIF_Ubisoft_GetRegString (hKey, NULL, value.c_str ( ));
        RegCloseKey (hKey);

        if (! data.empty ( ))
          return data;
      }
    }

    return L"";
  }

  // The client's folder (normalized), or empty if Ubisoft Connect is not installed
  std::wstring
  SKIF_Ubisoft_GetClientFolder (void)
  {
    return SKIF_Ubisoft_NormalizePath (SKIF_Ubisoft_GetRegString32 (LAUNCHER_KEY, L"InstallDir"));
  }

  bool
  SKIF_Ubisoft_HasExtension (const std::wstring& path, const wchar_t* extension)
  {
    size_t len = wcslen (extension);
    return (path.size ( ) > len && _wcsicmp (path.c_str ( ) + path.size ( ) - len, extension) == 0);
  }

  // [ubisoft-config-begin]
  // Reading the client's configurations cache: a protobuf container of per-product YAML documents.
  //   Only standard C++ is used here, so this part can be tested on its own.

  // The products of the configurations file: product ID -> YAML configuration
  //   Returns false if the data is not a valid container (the products read until then are kept).
  bool
  SKIF_Ubisoft_ParseConfigurations (const std::string& data, std::map <uint32_t, std::string>& products)
  {
    const size_t size = data.size ( );

    auto _Varint = [&](size_t& off, uint64_t& value) -> bool
    {
      value = 0;

      for (int shift = 0; shift < 64; shift += 7)
      {
        if (off >= size)
          return false;

        uint8_t byte = static_cast <uint8_t> (data [off++]);
        value |= static_cast <uint64_t> (byte & 0x7F) << shift;

        if ((byte & 0x80) == 0)
          return true;
      }

      return false;
    };

    // Reads one field; for length-delimited fields, [start, end) is its payload
    auto _Field = [&](size_t& off, size_t end, uint32_t& number, uint32_t& wire, uint64_t& value, size_t& start) -> bool
    {
      uint64_t tag = 0;

      if (! _Varint (off, tag) || off > end)
        return false;

      number = static_cast <uint32_t> (tag >> 3);
      wire   = static_cast <uint32_t> (tag &  7);

      switch (wire)
      {
        case 0: // varint
          return _Varint (off, value) && off <= end;
        case 1: // 64-bit
          off += 8;
          return off <= end;
        case 2: // length-delimited
          if (! _Varint (off, value) || value > end - off)
            return false;
          start = off;
          off  += static_cast <size_t> (value);
          return true;
        case 5: // 32-bit
          off += 4;
          return off <= end;
        default:
          return false;
      }
    };

    size_t off = 0;

    while (off < size)
    {
      uint32_t number = 0, wire = 0;
      uint64_t value  = 0;
      size_t   start  = 0;

      if (! _Field (off, size, number, wire, value, start))
        return false;

      if (number != 1 || wire != 2)
        continue;

      // A product
      size_t      end = start + static_cast <size_t> (value), inner = start;
      uint64_t    id  = 0;
      std::string yaml;

      while (inner < end)
      {
        uint32_t n = 0, w = 0;
        uint64_t v = 0;
        size_t   s = 0;

        if (! _Field (inner, end, n, w, v, s))
          return false;

        if      (n == 1 && w == 0)
          id   = v;
        else if (n == 3 && w == 2)
          yaml = data.substr (s, static_cast <size_t> (v));
      }

      if (id != 0 && id <= UINT32_MAX && ! yaml.empty ( ))
        products [static_cast <uint32_t> (id)] = std::move (yaml);
    }

    return true;
  }

  // A node of a YAML document: a scalar, a mapping or a sequence (null: an empty scalar)
  struct ubisoft_yaml_s {
    enum class Type { Scalar, Map, Sequence } type = Type::Scalar;

    std::string                  scalar;
    std::vector <std::string>    keys;     // Mapping: the keys, in document order (a repeated key keeps the first value)
    std::vector <ubisoft_yaml_s> values;   // Mapping: the value of each key (vectors may hold this incomplete type, pairs may not)
    std::vector <ubisoft_yaml_s> sequence;

    // The value of a key of a mapping (nullptr if this is not a mapping or the key is missing)
    const ubisoft_yaml_s* get (const char* key) const
    {
      if (type != Type::Map)
        return nullptr;

      for (size_t i = 0; i < keys.size ( ) && i < values.size ( ); i++)
        if (keys [i] == key)
          return &values [i];

      return nullptr;
    }

    // get ("a") -> get ("b") -> ...
    const ubisoft_yaml_s* path (std::initializer_list <const char*> names) const
    {
      const ubisoft_yaml_s* node = this;

      for (const char* key : names)
        if (node == nullptr || (node = node->get (key)) == nullptr)
          return nullptr;

      return node;
    }

    // The scalar value of a key (empty if missing or not a scalar)
    std::string str (const char* key) const
    {
      const ubisoft_yaml_s* node = get (key);
      return (node != nullptr && node->type == Type::Scalar) ? node->scalar : "";
    }
  };

  // A parser for the subset of YAML (block style) the client's configurations use:
  //   mappings, sequences (also "- key: value" items and items at the parent key's indentation),
  //   plain, single- and double-quoted scalars, empty flow collections ({}, []), block scalars (| and >),
  //   comments. Other flow collections are kept as their text. Tabs are not used for indentation.
  class SKIF_Ubisoft_YAMLParser
  {
  public:
    ubisoft_yaml_s parse (const std::string& text)
    {
      lines.clear ( );

      size_t pos = 0;

      while (pos <= text.size ( ))
      {
        size_t eol = text.find ('\n', pos);

        if (eol == std::string::npos)
          eol = text.size ( );

        std::string line = text.substr (pos, eol - pos);

        if (! line.empty ( ) && line.back ( ) == '\r')
          line.pop_back ( );

        lines.push_back (std::move (line));
        pos = eol + 1;
      }

      current = 0;

      ubisoft_yaml_s root;
      root.type = ubisoft_yaml_s::Type::Map;

      if (skip ( ))
      {
        // The document starts at the first content line's indentation
        root = block (indentOf (lines [current]));
      }

      return root;
    }

  private:
    std::vector <std::string> lines;
    size_t                    current = 0;

    static size_t indentOf (const std::string& line)
    {
      size_t indent = 0;

      while (indent < line.size ( ) && line [indent] == ' ')
        indent++;

      return indent;
    }

    // Comments and blank lines carry no content (outside of block scalars)
    static bool isContent (const std::string& line)
    {
      size_t first = line.find_first_not_of (" \t");

      if (first == std::string::npos || line [first] == '#')
        return false;

      // Document markers
      if (line.compare (first, 3, "---") == 0 || line.compare (first, 3, "...") == 0)
        return (line.size ( ) > first + 3 && line [first + 3] != ' ');

      return true;
    }

    static bool isSequenceItem (const std::string& line, size_t indent)
    {
      return (line.size ( ) > indent && line [indent] == '-' &&
             (line.size ( ) == indent + 1 || line [indent + 1] == ' '));
    }

    // Moves to the next content line; false at the end
    bool skip (void)
    {
      while (current < lines.size ( ) && ! isContent (lines [current]))
        current++;

      return (current < lines.size ( ));
    }

    static std::string trim (const std::string& str)
    {
      size_t first = str.find_first_not_of (" \t");

      if (first == std::string::npos)
        return "";

      return str.substr (first, str.find_last_not_of (" \t") - first + 1);
    }

    // The position of the key separator (": " or a trailing ':') outside quotes, or npos
    static size_t findSeparator (const std::string& str)
    {
      char quote = 0;

      for (size_t i = 0; i < str.size ( ); i++)
      {
        char c = str [i];

        if (quote != 0)
        {
          if (c == quote)
          {
            // '' is an escaped quote in single-quoted scalars
            if (quote == '\'' && i + 1 < str.size ( ) && str [i + 1] == '\'')
              i++;
            else
              quote = 0;
          }

          else if (c == '\\' && quote == '"')
            i++;
        }

        else if ((c == '"' || c == '\'') && i == 0)
          quote = c;

        else if (c == ':' && (i + 1 == str.size ( ) || str [i + 1] == ' '))
          return i;

        else if (c == '#' && i > 0 && str [i - 1] == ' ')
          return std::string::npos; // A comment
      }

      return std::string::npos;
    }

    // Removes a trailing comment (" #") outside quotes
    static std::string stripComment (const std::string& str)
    {
      char quote = 0;

      for (size_t i = 0; i < str.size ( ); i++)
      {
        char c = str [i];

        if (quote != 0)
        {
          if (c == quote)
          {
            if (quote == '\'' && i + 1 < str.size ( ) && str [i + 1] == '\'')
              i++;
            else
              quote = 0;
          }

          else if (c == '\\' && quote == '"')
            i++;
        }

        else if ((c == '"' || c == '\'') && (i == 0 || str [i - 1] == ' '))
          quote = c;

        else if (c == '#' && (i == 0 || str [i - 1] == ' ' || str [i - 1] == '\t'))
          return str.substr (0, i);
      }

      return str;
    }

    static std::string unquote (const std::string& value)
    {
      std::string str = trim (value);

      if (str.size ( ) >= 2 && str.front ( ) == '\'' && str.back ( ) == '\'')
      {
        std::string out;

        for (size_t i = 1; i + 1 < str.size ( ); i++)
        {
          out += str [i];

          if (str [i] == '\'' && str [i + 1] == '\'')
            i++;
        }

        return out;
      }

      if (str.size ( ) >= 2 && str.front ( ) == '"' && str.back ( ) == '"')
      {
        std::string out;

        for (size_t i = 1; i + 1 < str.size ( ); i++)
        {
          char c = str [i];

          if (c != '\\' || i + 2 >= str.size ( ))
          {
            out += c;
            continue;
          }

          switch (str [++i])
          {
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            case 'r':  out += '\r'; break;
            case '0':  out += '\0'; break;
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            default:   out += '\\'; out += str [i]; break; // \x, \u, ... are kept as written
          }
        }

        return out;
      }

      return str;
    }

    // A value written after "key:" or "- " on the same line
    ubisoft_yaml_s inlineValue (const std::string& raw, size_t parentIndent)
    {
      ubisoft_yaml_s node;
      std::string    value = trim (stripComment (raw));

      if (value == "{}")
        node.type = ubisoft_yaml_s::Type::Map;

      else if (value == "[]")
        node.type = ubisoft_yaml_s::Type::Sequence;

      // Block scalars: the following lines indented deeper than the parent
      else if (! value.empty ( ) && (value [0] == '|' || value [0] == '>'))
      {
        const bool folded = (value [0] == '>');
        size_t     indent = std::string::npos;

        while (current < lines.size ( ))
        {
          const std::string& line = lines [current];
          size_t             lineIndent = indentOf (line);

          if (trim (line).empty ( ))
          {
            node.scalar += '\n';
            current++;
            continue;
          }

          if (lineIndent <= parentIndent)
            break;

          if (indent == std::string::npos)
            indent = lineIndent;

          if (lineIndent < indent)
            break;

          if (! node.scalar.empty ( ) && node.scalar.back ( ) != '\n')
            node.scalar += (folded) ? ' ' : '\n';

          node.scalar += line.substr (indent);
          current++;
        }

        while (! node.scalar.empty ( ) && node.scalar.back ( ) == '\n')
          node.scalar.pop_back ( );
      }

      else
        node.scalar = unquote (value);

      return node;
    }

    // The value of "key:" with nothing after it: a deeper block, a sequence at the key's indentation, or null
    ubisoft_yaml_s nestedValue (size_t keyIndent)
    {
      if (skip ( ))
      {
        const std::string& next   = lines [current];
        size_t             indent = indentOf (next);

        if (indent > keyIndent || (indent == keyIndent && isSequenceItem (next, indent)))
          return block (indent);
      }

      return ubisoft_yaml_s ( ); // null
    }

    // A mapping or a sequence whose lines start at `indent`
    ubisoft_yaml_s block (size_t indent)
    {
      if (skip ( ) && isSequenceItem (lines [current], indent))
        return sequence (indent);

      return mapping (indent);
    }

    ubisoft_yaml_s sequence (size_t indent)
    {
      ubisoft_yaml_s node;
      node.type = ubisoft_yaml_s::Type::Sequence;

      while (skip ( ))
      {
        std::string& line = lines [current];

        if (indentOf (line) != indent || ! isSequenceItem (line, indent))
          break;

        std::string content = line.substr (indent + 1);
        size_t      offset  = content.find_first_not_of (' ');

        if (offset == std::string::npos || content [offset] == '#')
        {
          // "-" alone: the item is the following deeper block
          current++;

          ubisoft_yaml_s item;

          if (skip ( ) && indentOf (lines [current]) > indent)
            item = block (indentOf (lines [current]));

          node.sequence.push_back (std::move (item));
          continue;
        }

        size_t itemIndent = indent + 1 + offset;

        // "- key: value": the item is a mapping whose keys are at the column after "- "
        if (content [offset] != '"' && content [offset] != '\'' && content [offset] != '{' && content [offset] != '[' &&
            findSeparator (content.substr (offset)) != std::string::npos)
        {
          line.replace (indent, 1, " "); // The line now reads as the item's first key
          node.sequence.push_back (mapping (itemIndent));
          continue;
        }

        // "- scalar"
        current++;
        node.sequence.push_back (inlineValue (content, indent));
      }

      return node;
    }

    ubisoft_yaml_s mapping (size_t indent)
    {
      ubisoft_yaml_s node;
      node.type = ubisoft_yaml_s::Type::Map;

      while (skip ( ))
      {
        const std::string line       = lines [current];
        size_t            lineIndent = indentOf (line);

        if (lineIndent < indent)
          break;

        if (lineIndent == indent && isSequenceItem (line, indent))
          break; // A sequence at the indentation of this mapping's keys belongs to the parent

        std::string content   = line.substr (lineIndent);
        size_t      separator = findSeparator (content);

        current++;

        // A deeper line that is not a key (a continuation of a plain multi-line scalar): appended to the last value
        if (lineIndent > indent || separator == std::string::npos)
        {
          if (! node.values.empty ( ) && node.values.back ( ).type == ubisoft_yaml_s::Type::Scalar)
          {
            std::string& scalar = node.values.back ( ).scalar;
            scalar += (scalar.empty ( ) ? "" : " ") + trim (stripComment (content));
          }

          continue;
        }

        std::string    key  = unquote (content.substr (0, separator));
        std::string    rest = content.substr (separator + 1);
        ubisoft_yaml_s value;

        if (trim (stripComment (rest)).empty ( ))
          value = nestedValue (indent);
        else
          value = inlineValue (rest, indent);

        if (std::find (node.keys.begin ( ), node.keys.end ( ), key) == node.keys.end ( ))
        {
          node.keys  .push_back (std::move (key));
          node.values.push_back (std::move (value));
        }
      }

      return node;
    }
  };
  // A localized string: values like "l1" or "EXE_DESCRIPTION_1" are keys of the localizations (the install language's,
  //   else the default ones); anything else is returned as is
  std::string
  SKIF_Ubisoft_Localize (const ubisoft_yaml_s& document, const std::string& language, const std::string& value)
  {
    if (value.empty ( ))
      return value;

    if (const ubisoft_yaml_s* localizations = document.get ("localizations"))
    {
      for (const std::string& locale : { language, std::string ("default") })
      {
        const ubisoft_yaml_s* strings = (locale.empty ( )) ? nullptr : localizations->get (locale.c_str ( ));

        if (strings == nullptr)
          continue;

        std::string localized = strings->str (value.c_str ( ));

        if (! localized.empty ( ))
          return localized;
      }
    }

    return value;
  }

  // An executable of start_game, as written in the configuration (paths and registry references not resolved yet)
  struct ubisoft_executable_s {
    std::string label;              // shortcut_name, else description
    std::string relative;           // path: relative: (relative to the install folder)
    std::string path_register;      // path: register: (a registry value holding the executable)
    std::string working_register;   // working_directory: register: (a registry value holding the folder)
    std::string working_append;     // working_directory: append:
    std::string arguments;          // play_arguments, else arguments
    std::string arguments_register; // A registry value holding (more) arguments
    std::string updater;            // evil_updater: path: relative: (run by the client before the game; Far Cry 3's starts the game itself)
  };

  // The launchable executables of a product, from start_game's online section (else offline), in order.
  //   Labels are localized for the install language.
  std::vector <ubisoft_executable_s>
  SKIF_Ubisoft_GetExecutables (const ubisoft_yaml_s& document, const std::string& language)
  {
    std::vector <ubisoft_executable_s> executables;

    const ubisoft_yaml_s* start_game = document.path ({ "root", "start_game" });

    if (start_game == nullptr)
      return executables;

    for (const char* section : { "online", "offline" })
    {
      const ubisoft_yaml_s* list = start_game->path ({ section, "executables" });

      if (list == nullptr || list->type != ubisoft_yaml_s::Type::Sequence || list->sequence.empty ( ))
        continue;

      for (auto& item : list->sequence)
      {
        if (item.type != ubisoft_yaml_s::Type::Map)
          continue;

        ubisoft_executable_s exe;

        if (const ubisoft_yaml_s* path = item.get ("path"))
        {
          exe.relative      = path->str ("relative");
          exe.path_register = path->str ("register");
        }

        if (const ubisoft_yaml_s* working = item.get ("working_directory"))
        {
          exe.working_register = working->str ("register");
          exe.working_append   = working->str ("append");
        }

        exe.label              = SKIF_Ubisoft_Localize (document, language, item.str ("shortcut_name"));
        if (exe.label.empty ( ))
          exe.label            = SKIF_Ubisoft_Localize (document, language, item.str ("description"));

        exe.arguments          = item.str ("play_arguments");
        if (exe.arguments.empty ( ))
          exe.arguments        = item.str ("arguments");

        exe.arguments_register = item.str ("arguments_register");

        if (const ubisoft_yaml_s* updater = item.path ({ "evil_updater", "path" }))
          exe.updater          = updater->str ("relative");

        if (! exe.relative.empty ( ) || ! exe.path_register.empty ( ))
          executables.push_back (std::move (exe));
      }

      break; // The offline section is only used when there is no online one
    }

    return executables;
  }

  // The product's name (root: name:)
  std::string
  SKIF_Ubisoft_GetName (const ubisoft_yaml_s& document, const std::string& language)
  {
    const ubisoft_yaml_s* root = document.get ("root");

    return (root != nullptr) ? SKIF_Ubisoft_Localize (document, language, root->str ("name")) : "";
  }

  // The product is the Steam edition (start_game: steam:), launched through Steam
  bool
  SKIF_Ubisoft_IsSteamProduct (const ubisoft_yaml_s& document)
  {
    return (document.path ({ "root", "start_game", "steam" }) != nullptr);
  }
  // [ubisoft-config-end]

  // The parsed configurations file, kept until the file changes (it is ~700 KB with ~2000 products, most of them DLC)
  struct ubisoft_configurations_s {
    std::mutex                        mutex;
    std::wstring                      path;
    uint64_t                          stamp = 0; // Last write time
    uint64_t                          size  = 0;
    std::map <uint32_t, std::string>  products;
  };

  ubisoft_configurations_s&
  SKIF_Ubisoft_GetConfigurationsCache (void)
  {
    static ubisoft_configurations_s cache;
    return cache;
  }

  // The YAML configuration of a product ("" if unknown); re-reads the file if it changed. Any thread.
  std::string
  SKIF_Ubisoft_GetConfiguration (const std::wstring& clientFolder, uint32_t productID)
  {
    ubisoft_configurations_s& cache = SKIF_Ubisoft_GetConfigurationsCache ( );
    std::scoped_lock          lock (cache.mutex);

    std::wstring              path  = SKIF_Ubisoft_CombinePath (clientFolder, CONFIGURATIONS);
    WIN32_FILE_ATTRIBUTE_DATA attrs = { };

    if (path.empty ( ) || ! GetFileAttributesExW (path.c_str ( ), GetFileExInfoStandard, &attrs))
    {
      cache.path.clear ( );
      cache.products.clear ( );
      cache.stamp = cache.size = 0;
      return "";
    }

    uint64_t stamp = (static_cast <uint64_t> (attrs.ftLastWriteTime.dwHighDateTime) << 32) | attrs.ftLastWriteTime.dwLowDateTime;
    uint64_t size  = (static_cast <uint64_t> (attrs.nFileSizeHigh)                  << 32) | attrs.nFileSizeLow;

    if (path != cache.path || stamp != cache.stamp || size != cache.size)
    {
      DWORD dwStart = SKIF_Util_timeGetTime1 ( );

      cache.path  = path;
      cache.stamp = stamp;
      cache.size  = size;
      cache.products.clear ( );

      std::ifstream file (path, std::ios::binary);

      if (file)
      {
        std::stringstream buffer;
        buffer << file.rdbuf ( );

        if (! SKIF_Ubisoft_ParseConfigurations (buffer.str ( ), cache.products))
          PLOG_WARNING << "The Ubisoft Connect configurations file is not in the expected format; read " << cache.products.size ( ) << " products until the error: " << path;
        else
          PLOG_VERBOSE << "Read " << cache.products.size ( ) << " Ubisoft Connect product configurations in " << (SKIF_Util_timeGetTime1 ( ) - dwStart) << " ms.";
      }

      else
        PLOG_WARNING << "Could not read the Ubisoft Connect configurations file: " << path;
    }

    auto it = cache.products.find (productID);

    return (it != cache.products.end ( )) ? it->second : "";
  }

  // A game listed under Launcher\Installs
  struct ubisoft_install_s {
    uint32_t     product_id = 0;
    std::wstring install_dir; // Normalized, not backslash-terminated
    std::wstring language;    // en-US
  };

  std::vector <ubisoft_install_s>
  SKIF_Ubisoft_GetInstalls (void)
  {
    std::vector <ubisoft_install_s> installs;
    HKEY                            hKey = nullptr;

    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, INSTALLS_KEY, 0, KEY_READ | KEY_WOW64_32KEY, &hKey) != ERROR_SUCCESS)
      return installs;

    wchar_t szSubKey [256] = { };

    for (DWORD dwIndex = 0; ; dwIndex++)
    {
      DWORD   dwSize = _countof (szSubKey);
      LSTATUS ls     = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

      if (ls == ERROR_NO_MORE_ITEMS)
        break;

      if (ls != ERROR_SUCCESS)
        continue;

      // The subkeys are product IDs
      wchar_t*      end = nullptr;
      unsigned long long id  = wcstoull (szSubKey, &end, 10);

      if (end == szSubKey || *end != L'\0' || id == 0 || id > UINT32_MAX)
        continue;

      ubisoft_install_s install;
      install.product_id  = static_cast <uint32_t> (id);
      install.install_dir = SKIF_Ubisoft_NormalizePath (SKIF_Ubisoft_GetRegString (hKey, szSubKey, L"InstallDir"));
      install.language    = SKIF_Ubisoft_GetRegString (hKey, szSubKey, L"Language");

      if (install.install_dir.empty ( ))
      {
        PLOG_WARNING << "Ubisoft Connect product " << install.product_id << " has no install folder; skipping...";
        continue;
      }

      installs.emplace_back (std::move (install));
    }

    RegCloseKey (hKey);

    return installs;
  }

  // The client is downloading (or updating) the game: its files are staged in <install folder>\uplay_download\<product ID>\.
  //   The uplay_download folder itself remains, empty, after the download has finished.
  bool
  SKIF_Ubisoft_IsDownloading (const std::wstring& install_dir, uint32_t productID)
  {
    return (! install_dir.empty ( ) && productID != 0 &&
            PathIsDirectoryW ((install_dir + LR"(\uplay_download\)" + std::to_wstring (productID)).c_str ( )));
  }

  // The current values of the registry references a product's executables are read from (path: register:,
  //   arguments_register:), which the client writes on the game's first launch (Far Cry 3 Blood Dragon).
  //   Main thread only: the references are kept per product until its configuration changes.
  std::string
  SKIF_Ubisoft_GetRegisterValues (uint32_t productID, const std::string& yaml)
  {
    struct references_s {
      size_t                    hash = 0;
      std::vector <std::string> references;
    };

    static std::map <uint32_t, references_s> cache;

    std::hash <std::string> hasher;
    size_t                  hash = hasher (yaml);
    references_s&           entry = cache [productID];

    if (entry.hash != hash || hash == 0)
    {
      entry.hash = hash;
      entry.references.clear ( );

      if (! yaml.empty ( ))
      {
        SKIF_Ubisoft_YAMLParser parser;

        for (auto& exe : SKIF_Ubisoft_GetExecutables (parser.parse (yaml), ""))
        {
          if (! exe.path_register.empty ( ))
            entry.references.push_back (exe.path_register);

          if (! exe.arguments_register.empty ( ))
            entry.references.push_back (exe.arguments_register);
        }
      }
    }

    std::string values;

    for (auto& reference : entry.references)
      values += SK_WideCharToUTF8 (SKIF_Ubisoft_ReadRegisterReference (reference)) + ";";

    return values;
  }

  // Identifies the installed games and their configurations, to tell when the library has to be repopulated
  std::string
  SKIF_Ubisoft_GetFingerprint (void)
  {
    std::wstring clientFolder = SKIF_Ubisoft_GetClientFolder ( );
    std::string  fingerprint  = SK_WideCharToUTF8 (clientFolder) + "\n";

    std::hash <std::string> hasher;

    // A download finishing (or starting) is part of it: games are only listed once their executable exists.
    //   So are the registry values executables are read from, which the client writes on a game's first launch.
    for (auto& install : SKIF_Ubisoft_GetInstalls ( ))
    {
      std::string yaml = SKIF_Ubisoft_GetConfiguration (clientFolder, install.product_id);

      fingerprint += std::to_string (install.product_id) + "|" + SK_WideCharToUTF8 (install.install_dir) + "|" + SK_WideCharToUTF8 (install.language) + "|" +
                     std::to_string (hasher (yaml)) + "|" +
                     ((SKIF_Ubisoft_IsDownloading (install.install_dir, install.product_id)) ? "downloading" : "") + "|" +
                     SKIF_Ubisoft_GetRegisterValues (install.product_id, yaml) + "\n";
    }

    return fingerprint;
  }

  // Letters and digits only, lower-cased: "RAYMAN® LEGENDS" -> "raymanlegends"
  std::string
  SKIF_Ubisoft_Simplify (const std::string& str)
  {
    std::string out;

    for (unsigned char c : str)
      if (c < 0x80 && isalnum (c))
        out += static_cast <char> (tolower (c));

    return out;
  }

  bool
  SKIF_Ubisoft_HasLowercase (const std::string& str)
  {
    return std::any_of (str.begin ( ), str.end ( ), [](unsigned char c) { return (c >= 'a' && c <= 'z'); });
  }
}

#pragma endregion

#pragma region Library

void
SKIF_Ubisoft_GetInstalledAppIDs (app_list_t *apps)
{
  if (apps == nullptr)
    return;

  std::wstring clientFolder = SKIF_Ubisoft_GetClientFolder ( );

  if (clientFolder.empty ( ))
    return;

  std::set <uint32_t> ids; // Product IDs already added

  for (auto& install : SKIF_Ubisoft_GetInstalls ( ))
  {
    const uint32_t productID = install.product_id;

    // Games that take long to scan are logged (the scan only reads the registry and checks that files exist,
    //   so this points at a slow drive, or a file system filter such as an antivirus)
    struct slow_scan_s {
      uint32_t product_id;
      DWORD    dwStart;

     ~slow_scan_s (void)
      {
        DWORD dwTime = SKIF_Util_timeGetTime1 ( ) - dwStart;

        if (dwTime > 100)
          PLOG_DEBUG << "Ubisoft Connect product " << product_id << " took " << dwTime << " ms to scan.";
      }
    } slowScan { productID, SKIF_Util_timeGetTime1 ( ) };

    if (! ids.insert (productID).second)
      continue;

    // Ubisoft games bought on Steam or Epic are listed by those scans
    if (StrStrIW (install.install_dir.c_str ( ), LR"(\steamapps\)") != nullptr ||
        PathIsDirectoryW ((install.install_dir + LR"(\.egstore)").c_str ( )))
    {
      PLOG_VERBOSE << "Ubisoft Connect product " << productID << " is a Steam or Epic install; skipping...";
      continue;
    }

    if (! PathIsDirectoryW (install.install_dir.c_str ( )))
    {
      PLOG_WARNING << "The install folder of Ubisoft Connect product " << productID << " does not exist (" << install.install_dir << "); skipping...";
      continue;
    }

    // The uninstall entry: the name (not always a proper title) and the client's icon for the game
    std::wstring uninstallKey = UNINSTALL_KEY + std::to_wstring (productID);
    std::string  displayName  = SK_WideCharToUTF8 (SKIF_Ubisoft_GetRegString32 (uninstallKey, L"DisplayName"));
    std::wstring displayIcon  = SKIF_Ubisoft_NormalizePath (SKIF_Ubisoft_GetRegString32 (uninstallKey, L"DisplayIcon"));

    if (! SKIF_Ubisoft_HasExtension (displayIcon, L".ico") || ! PathFileExistsW (displayIcon.c_str ( )))
      displayIcon.clear ( );

    // The product's configuration
    std::string    language = SK_WideCharToUTF8 (install.language);
    std::string    yaml     = SKIF_Ubisoft_GetConfiguration (clientFolder, productID);
    ubisoft_yaml_s document;

    if (! yaml.empty ( ))
    {
      SKIF_Ubisoft_YAMLParser parser;
      document = parser.parse (yaml);
    }

    else
      PLOG_WARNING << "Ubisoft Connect product " << productID << " has no configuration in the client's cache";

    std::vector <ubisoft_executable_s> executables = SKIF_Ubisoft_GetExecutables (document, language);

    if (executables.empty ( ) && SKIF_Ubisoft_IsSteamProduct (document))
    {
      PLOG_VERBOSE << "Ubisoft Connect product " << productID << " is the Steam edition; skipping...";
      continue;
    }

    // The title: the configuration's name, else the uninstall entry's, else the folder name.
    //   Names written in capitals (RAYMAN® LEGENDS) use the uninstall entry's spelling when it is the same name.
    std::string title = SKIF_Ubisoft_GetName (document, language);

    if (title.empty ( ) || (! SKIF_Ubisoft_HasLowercase (title) && ! displayName.empty ( ) &&
                            SKIF_Ubisoft_Simplify (title) == SKIF_Ubisoft_Simplify (displayName)))
      title = displayName;

    if (title.empty ( ))
      title = SK_WideCharToUTF8 (std::filesystem::path (install.install_dir).filename ( ).wstring ( ));

    app_record_ubisoft_s record (productID);

    record.store_utf8             = "Ubisoft";
    record._status.installed      = true;
    record.install_dir            = install.install_dir;
    record.names.normal           = title;
    record.names.original         = title;

    record.ubisoft.key            = std::to_string (productID);
    record.ubisoft.product_id     = productID;
    record.ubisoft.display_name   = displayName;
    record.ubisoft.language       = language;
    record.ubisoft.icon           = displayIcon;
    record.ubisoft.configuration  = (! yaml.empty ( ));

    // A download in progress is shown as updating (the game can still be launched, see CanLaunchWhileUpdating). The game
    //   is only listed if its executable exists: an update, or a progressive install that is already playable.
    record._status.updating       = (SKIF_Ubisoft_IsDownloading (install.install_dir, productID)) ? 0x1 : 0x0;

    PLOG_VERBOSE_IF (record._status.updating) << "Ubisoft Connect product " << productID << " is being downloaded";

    // One launch config per executable of the configuration, in its order (the first one is the game).
    //   As for the other platforms, the game is only installed if its executable exists: Ubisoft Connect registers
    //   a game (Launcher\Installs, uninstall entry) as soon as it is queued for download. Other executables that
    //   are missing (an optional component) are left out.
    std::set <std::wstring> seen;               // Lower-cased executable + arguments
    int                     lcId         = 0;
    bool                    checkedMain  = false; // The first executable (the game itself) was found
    std::wstring            missingMain;          // The game's executable, when it is missing

    for (auto& exe : executables)
    {
      // The working folder: the referenced registry value (often the install folder itself), else the install folder
      std::wstring workingDir;

      if (! exe.working_register.empty ( ))
      {
        workingDir = SKIF_Ubisoft_ReadRegisterReference (exe.working_register);

        if (! workingDir.empty ( ))
          workingDir = SKIF_Ubisoft_CombinePath (workingDir, SK_UTF8ToWideChar (exe.working_append));
      }

      if (workingDir.empty ( ) || ! PathIsDirectoryW (workingDir.c_str ( )))
        workingDir = install.install_dir;

      // The executable: relative to the install folder, or read from the registry (written by the game's installer)
      std::wstring exePath;

      if (! exe.relative.empty ( ))
      {
        std::wstring relative = SK_UTF8ToWideChar (exe.relative);

        exePath = SKIF_Ubisoft_CombinePath (install.install_dir, relative);

        // Relative to the working folder instead, if only found there
        if (! PathFileExistsW (exePath.c_str ( )) && _wcsicmp (workingDir.c_str ( ), install.install_dir.c_str ( )) != 0)
        {
          std::wstring candidate = SKIF_Ubisoft_CombinePath (workingDir, relative);

          if (PathFileExistsW (candidate.c_str ( )))
            exePath = candidate;
        }
      }

      else
      {
        std::wstring value = SKIF_Ubisoft_ReadRegisterReference (exe.path_register);

        // The value is written by the client on the game's first launch (Far Cry 3 Blood Dragon: ExecutableName =
        //   bin\fc3_blooddragon_d3d11.exe). Until then, the executable's updater is used: the client runs it before
        //   the game, and Far Cry 3's updaters start the game themselves (bin\fc3_blooddragon_d3d11_b.exe -language=...).
        std::wstring updater = (! value.empty ( ) || exe.updater.empty ( )) ? L""
                             : SKIF_Ubisoft_CombinePath (workingDir, SK_UTF8ToWideChar (exe.updater));

        if (! updater.empty ( ) && PathFileExistsW (updater.c_str ( )))
        {
          PLOG_VERBOSE << "Ubisoft Connect product " << productID << ": the executable's registry value is missing (" << SK_UTF8ToWideChar (exe.path_register)
                       << "), it is written on the first launch through the client; using its updater: " << updater;

          if (! checkedMain)
            record.ubisoft.updater_fallback = true;

          exePath = updater;
        }

        else if (value.empty ( ))
        {
          PLOG_VERBOSE << "Ubisoft Connect product " << productID << ": the executable's registry value is missing (" << SK_UTF8ToWideChar (exe.path_register) << ")";

          if (! checkedMain)
          {
            missingMain = SK_UTF8ToWideChar (exe.path_register);
            break;
          }

          continue;
        }

        else
          exePath = (PathIsRelativeW (value.c_str ( ))) ? SKIF_Ubisoft_CombinePath (workingDir, value)
                                                        : SKIF_Ubisoft_NormalizePath (value);
      }

      // Native executables only (the Settlers History Editions also list .bat files for their map editors)
      if (! SKIF_Ubisoft_HasExtension (exePath, L".exe"))
      {
        PLOG_VERBOSE << "Ubisoft Connect product " << productID << ": skipping the non-executable " << exePath;
        continue;
      }

      if (! PathFileExistsW (exePath.c_str ( )))
      {
        if (! checkedMain)
        {
          missingMain = exePath;
          break;
        }

        PLOG_VERBOSE << "Ubisoft Connect product " << productID << ": leaving out the missing executable " << exePath;
        continue;
      }

      checkedMain = true;

      std::wstring arguments = SK_UTF8ToWideChar (exe.arguments);

      if (! exe.arguments_register.empty ( ))
      {
        std::wstring more = SKIF_Ubisoft_ReadRegisterReference (exe.arguments_register);

        if (! more.empty ( ))
          arguments += ((arguments.empty ( )) ? L"" : L" ") + more;
      }

      if (! seen.insert (SKIF_Util_ToLowerW (exePath + L"|" + arguments)).second)
        continue;

      app_record_s::launch_config_s lc;
      lc.id                   = lcId;
      lc.valid                = 1;
      lc.executable_path      = exePath;
      lc.executable           = std::filesystem::path (exePath).filename ( ).wstring ( );
      lc.install_dir          = record.install_dir;
      lc.working_dir          = workingDir;
      lc.launch_options       = arguments;
      lc.description          = SK_UTF8ToWideChar (exe.label);

      record.launch_configs.emplace (lcId++, lc);
    }

    if (! missingMain.empty ( ))
    {
      PLOG_VERBOSE << "Ubisoft Connect game " << SK_UTF8ToWideChar (title) << " is not installed" << ((record._status.updating) ? " (still downloading)" : "")
                   << ": its executable is missing (" << missingMain << "); skipping...";
      continue;
    }

    if (record.launch_configs.empty ( ))
    {
      PLOG_WARNING << "Could not find the executable of Ubisoft Connect game " << SK_UTF8ToWideChar (title) << " (" << install.install_dir << "); skipping...";
      continue;
    }

    record.specialk.injection.injection.type = InjectionType::Global;

    // Strip invalid filename characters
    record.specialk.profile_dir_utf8 = SKIF_Util_StripInvalidFilenameChars (title);
    record.specialk.profile_dir      = SK_UTF8ToWideChar (record.specialk.profile_dir_utf8);

    std::string entry_name = record.names.normal; // Copied first: the record is moved from below

    app_entry_t
      Ubisoft_entry (std::move (entry_name), std::make_unique <app_record_ubisoft_s> (std::move (record)));

    apps->emplace_back (std::move (Ubisoft_entry));
  }
}

bool
SKIF_Ubisoft_hasInstalledGamesChanged (void)
{
  static DWORD       dwLastSignalCheck = 0;
  static std::string fingerprint       = SKIF_Ubisoft_GetFingerprint ( );

  bool signal = false;
  if (SKIF_Util_timeGetTime ( ) > dwLastSignalCheck + 5000)
  {
    // A few registry reads; the configurations file is only re-read when it changed
    std::string current = SKIF_Ubisoft_GetFingerprint ( );

    if (current != fingerprint)
    {
      fingerprint = std::move (current);
      signal      = true;
    }

    dwLastSignalCheck = SKIF_Util_timeGetTime ( );
  }

  return signal;
}

bool
SKIF_Ubisoft_IsClientInstalled (void)
{
  std::wstring clientFolder = SKIF_Ubisoft_GetClientFolder ( );

  if (clientFolder.empty ( ) || ! PathFileExistsW (SKIF_Ubisoft_CombinePath (clientFolder, L"upc.exe").c_str ( )))
    return false;

  HKEY hKey = nullptr;

  if (RegOpenKeyExW (HKEY_CLASSES_ROOT, LR"(uplay\shell\open\command)", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return false;

  RegCloseKey (hKey);

  return true;
}

#pragma endregion

#pragma region Launching

bool
app_record_ubisoft_s::PrefersInstantLaunch (bool fromGameMenu, launch_config_s* config) const
{
  // Fallback if Ubisoft Connect is not installed
  if (ubisoft.product_id == 0 || ! SKIF_Ubisoft_IsClientInstalled ( ))
    return true;

  // Ubisoft Connect always starts the first executable, so the others (editors, expansions) are run directly
  if (config != nullptr && config->id != 0)
    return true;

  return (! fromGameMenu && UsesInstantPlay (GetInstantPlayDefault ( )));
}

// Launch Ubisoft game through Ubisoft Connect, unless an instant launch was requested
void
app_record_ubisoft_s::Launch (const launch_context_s& ctx)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (ctx.instant || ubisoft.product_id == 0)
  {
    LaunchInstant (ctx);
    return;
  }

  PLOG_VERBOSE << "Performing a Ubisoft Connect launch...";

  // uplay://launch/2010/0
  std::wstring launchOptions = SK_FormatStringW (LR"(uplay://launch/%u/0)", ubisoft.product_id);

  if (SKIF_Util_OpenURI (launchOptions) != 0)
  {
    std::scoped_lock app_lock (_games.GetMutex());

    // Don't check the running state for at least 7.5 seconds
    _status.dwTimeDelayChecks = ctx.timeNow + 7500;
    _status.running           = true;

    SKIF_Shell_AddJumpList (SK_UTF8ToWideChar (names.normal), L"", launchOptions, L"", ctx.config->getExecutableFullPath ( ), (! ctx.localInjection && ctx.usingSK));
  }
}

// List row, main thread: a download in progress is shown as updating, which also keeps the constant changes in the
//   install folder from refreshing the selected game over and over. The game can still be launched meanwhile
//   (CanLaunchWhileUpdating): with a progressive install, Ubisoft Connect lets a game be played before it has finished.
void
app_record_ubisoft_s::RefreshClientState (DWORD dwTimeNow)
{
  if (dwTimeNow < dwNextDownloadCheck)
    return;

  dwNextDownloadCheck = dwTimeNow + 2000;

  _status.updating    = (SKIF_Ubisoft_IsDownloading (install_dir, ubisoft.product_id)) ? 0x1 : 0x0;
}

// Process scan: the primary executable, or another executable in its folder. Ubisoft games are often started through
//   a stub that runs the actual game next to it: Ubisoft Connect runs Far Cry 3 Blood Dragon as
//   bin\fc3_blooddragon_d3d11_b.exe, while its launch target is bin\fc3_blooddragon_d3d11.exe (or the updater).
app_record_s::ProcessMatch
app_record_ubisoft_s::MatchProcess (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen)
{
  ProcessMatch match = app_record_s::MatchProcess (wszExeFile, wszExePath, dwExePathLen);

  if (match != ProcessMatch::None || wszExePath == nullptr || dwExePathLen == 0)
    return match;

  auto primary = launch_configs.find (0);

  if (primary == launch_configs.end ( ))
    return ProcessMatch::None;

  std::wstring folder = primary->second.getExecutableDir ( );

  if (folder.empty ( ))
    return ProcessMatch::None;

  if (folder.back ( ) != L'\\')
    folder += L'\\';

  std::wstring_view path (wszExePath, wcsnlen (wszExePath, dwExePathLen));

  // Directly in that folder (not in a subfolder), and an executable
  if (path.size ( ) > folder.size ( ) + 4 &&
      _wcsnicmp (path.data ( ), folder.c_str ( ), folder.size ( )) == 0 &&
      path.find (L'\\', folder.size ( )) == std::wstring_view::npos &&
      _wcsnicmp (path.data ( ) + path.size ( ) - 4, L".exe", 4) == 0)
    return ProcessMatch::Running;

  return ProcessMatch::None;
}

#pragma endregion

#pragma region Library UI

std::string
app_record_ubisoft_s::GetLaunchWarning (void) const
{
  if (! _status.updating)
    return "";

  return "Ubisoft Connect is still installing this game.\n"
         "It can be played already if Ubisoft Connect allows it (a progressive install), but\n"
         "Instant Play may run it before all of its files are in place.";
}

bool
app_record_ubisoft_s::GetInstantPlayDefault (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return _registry.bInstantPlayUbisoft;
}

std::wstring
app_record_ubisoft_s::GetStoreAssetDirectory (void) const
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  return SK_FormatStringW (LR"(%ws\Assets\Ubisoft\%ws\)", _path_cache.specialk_userdata, SKIF_Util_StripInvalidFilenameChars (SK_UTF8ToWideChar (ubisoft.key)).c_str());
}

std::vector <std::pair <std::string, std::string>>
app_record_ubisoft_s::GetStoreDebugFields (void) const
{
  return {
    { "Product ID",        ubisoft.key                                    },
    { "Uninstall Name",    ubisoft.display_name                           },
    { "Launch Target",     (ubisoft.updater_fallback) ? "Updater (registry value not written yet)" : "Executable" },
    { "Language",          ubisoft.language                               },
    { "Configuration",     (ubisoft.configuration) ? "Found" : "Missing"  },
    { "Client Icon",       SK_WideCharToUTF8 (ubisoft.icon)               }
  };
}

#pragma endregion

#pragma region Artwork

std::wstring
app_record_ubisoft_s::ResolveArtwork (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged)
{
  UNREFERENCED_PARAMETER (source);

  std::wstring assetDir = GetStoreAssetDirectory ( );
  std::wstring load_str = FindCustomArtwork (type);

  isCustom = (! load_str.empty ( ));

  if (! isCustom)
  {
    if      (type == LibraryTexture::Cover &&
             PathFileExistsW ((assetDir + L"cover-original.jpg").c_str ( )))
    {
      load_str  = assetDir + L"cover-original.jpg";
      isManaged = true; // Downloaded from IGDB: the cover menu offers Refresh
    }

    else if (type == LibraryTexture::Icon &&
             SKIF_Util_SaveExtractExeIcon (launch_configs[0].getExecutableFullPath ( ), assetDir + L"icon-original.png"))
      load_str  = assetDir + L"icon-original.png";

    // The client's icon for the game
    else if (type == LibraryTexture::Icon && ! ubisoft.icon.empty ( ))
    {
      load_str  = ubisoft.icon;
      isManaged = false;
    }
  }

  return load_str;
}

// Cover worker thread: the downloaded IGDB cover, else look the game up on IGDB by name
std::wstring
app_record_ubisoft_s::PrepareCoverSource (bool queryPCGW)
{
  UNREFERENCED_PARAMETER (queryPCGW);

  std::wstring coverPath = GetStoreAssetDirectory ( ) + L"cover-original.jpg";

  if (PathFileExistsW (coverPath.c_str ( )) || ! FindCustomArtwork (LibraryTexture::Cover).empty ( ))
    return coverPath;

  // The names to look up, in this order (each name once):
  //   1. The cleaned title (without (TM), (R) and (C) signs, which Ubisoft titles often have)
  //   2. The name shown in SKIF, if the user renamed the game
  //   3. The game executable's product name, which often has the subtitle IGDB knows the game by where Ubisoft numbers
  //        the games, with the title's edition: The Settlers 5 - History Edition is THE SETTLERS - Heritage of Kings,
  //        and IGDB's The Settlers: Heritage of Kings - History Edition. Then the product name alone.
  //   4. The uninstall entry's name
  //   A fuzzy match is accepted when it is the same name written differently (SKIF_IGDB_DownloadCoverByName).
  const std::string& title = (! names.clean.empty ( )) ? names.clean : names.original;

  std::vector <std::string> tried;

  auto _Try = [&](const std::string& name) -> bool
  {
    if (SKIF_Ubisoft_Simplify (name).empty ( ))
      return false;

    for (auto& previous : tried)
      if (SKIF_Ubisoft_Simplify (previous) == SKIF_Ubisoft_Simplify (name))
        return false;

    tried.push_back (name);

    return SKIF_IGDB_DownloadCoverByName (name, coverPath);
  };

  if (_Try (title) || _Try (names.normal))
    return coverPath;

  // The executable is only opened now, when the names did not find a cover: opening it can take seconds for a large,
  //   freshly installed executable, which an antivirus scans on first access (Immortals Fenyx Rising: 495 MB)
  auto primary = launch_configs.find (0);

  std::wstring productNameW = (primary != launch_configs.end ( )) ? SKIF_Util_GetProductName (primary->second.getExecutableFullPath ( ).c_str ( )) : L"";
  SKIF_Util_TrimSpacesW (productNameW);
  std::string  productName  = SK_WideCharToUTF8 (productNameW);

  PLOG_VERBOSE << "No IGDB cover found by the title of Ubisoft Connect product " << ubisoft.product_id
               << "; the product name of its executable is: " << ((productName.empty ( )) ? L"(none)" : productNameW);

  if (! productName.empty ( ))
  {
    // "The Settlers 5 - History Edition" -> "History Edition"
    size_t      dash    = title.rfind (" - ");
    std::string edition = (dash != std::string::npos) ? title.substr (dash + 3) : "";

    if (! edition.empty ( ) && StrStrIA (edition.c_str ( ), "Edition") != nullptr &&
        SKIF_Ubisoft_Simplify (productName).find (SKIF_Ubisoft_Simplify (edition)) == std::string::npos &&
        _Try (productName + " - " + edition))
      return coverPath;

    if (_Try (productName))
      return coverPath;
  }

  _Try (ubisoft.display_name);

  return coverPath;
}

#pragma endregion

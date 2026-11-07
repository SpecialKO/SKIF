// SteamDB's file detection rule sets: parsing rules.ini and matching paths (see utility/file_detection.h)
//
//   Portable on purpose (no Windows API, no SKIF headers), so the matching can be tested on its own.
//
//   Mirrors tests/FileDetector.php of https://github.com/SteamDatabase/FileDetectionRuleSets:
//     - each path is lowercased, and so is each expression (PHP lowercases both and matches case-sensitively),
//     - each path counts for at most one rule of each section (the first that matches, in the order of rules.ini),
//     - once all paths are in, TryDeduceEngine ( ) adds at most one engine deduced from Evidence rules, and
//     - Evidence rules are then left out of the result.
//
//   std::regex is slow, so two filters skip most expressions before they run: an expression that ends with a file
//     extension (\.dll$ or \.(?:dylib|dll)$) only runs on paths with that extension, and an expression with a
//     substring every match has to contain (see GetRequiredLiteral ( )) only runs on paths that contain it.

#include <utility/file_detection.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace SKIF_FileDetection
{
#pragma region Helpers

  static constexpr std::string_view whitespace = " \t\r\n";

  static std::string_view
  _Trim (std::string_view str)
  {
    const size_t first = str.find_first_not_of (whitespace);

    if (first == std::string_view::npos)
      return { };

    const size_t last  = str.find_last_not_of  (whitespace);

    return str.substr (first, last - first + 1);
  }

  static char
  _Lower (char ch)
  {
    return (ch >= 'A' && ch <= 'Z') ? static_cast <char> (ch - 'A' + 'a') : ch;
  }

  static bool
  _IsAlnum (char ch)
  {
    return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'));
  }

  static bool
  _IsDigit (char ch)
  {
    return (ch >= '0' && ch <= '9');
  }

  static bool
  _EndsWith (std::string_view str, std::string_view suffix)
  {
    return (str.size ( ) >= suffix.size ( ) && str.substr (str.size ( ) - suffix.size ( )) == suffix);
  }

  // Lowercase, / as the separator, no leading ./ or /
  static std::string
  _NormalizePath (std::string_view path)
  {
    std::string out;
    out.reserve (path.size ( ));

    for (char ch : path)
      out += (ch == '\\') ? '/' : _Lower (ch);

    while (out.starts_with ("./"))
      out.erase (0, 2);

    while (out.starts_with ('/'))
      out.erase (0, 1);

    while (out.ends_with ('/'))
      out.pop_back ( );

    return out;
  }

  // The extension of the last part of a path, without the dot ("" if none)
  static std::string_view
  _GetExtension (std::string_view path)
  {
    const size_t slash = path.rfind ('/');
    const std::string_view name = (slash == std::string_view::npos) ? path : path.substr (slash + 1);
    const size_t dot   = name.rfind ('.');

    if (dot == std::string_view::npos)
      return { };

    return name.substr (dot + 1);
  }

  // A {n}, {n,} or {n,m} quantifier at pos; its length, or 0 if there is none. min receives n.
  static size_t
  _ParseBraceQuantifier (std::string_view pattern, size_t pos, int* min = nullptr)
  {
    if (pos >= pattern.size ( ) || pattern [pos] != '{')
      return 0;

    size_t i = pos + 1;
    int    n = 0;

    if (i >= pattern.size ( ) || ! _IsDigit (pattern [i]))
      return 0;

    while (i < pattern.size ( ) && _IsDigit (pattern [i]))
      n = std::min (n * 10 + (pattern [i++] - '0'), 100000);

    if (i < pattern.size ( ) && pattern [i] == ',')
    {
      i++;

      while (i < pattern.size ( ) && _IsDigit (pattern [i]))
        i++;
    }

    if (i >= pattern.size ( ) || pattern [i] != '}')
      return 0;

    if (min != nullptr)
      *min = n;

    return i - pos + 1;
  }

  // Skips a [...] class that starts at pos; the position after it
  static size_t
  _SkipClass (std::string_view pattern, size_t pos)
  {
    size_t i = pos + 1;

    if (i < pattern.size ( ) && pattern [i] == '^')
      i++;

    if (i < pattern.size ( ) && pattern [i] == ']') // A ] right at the start is part of the class
      i++;

    while (i < pattern.size ( ) && pattern [i] != ']')
      i += (pattern [i] == '\\') ? 2 : 1;

    return std::min (i + 1, pattern.size ( ));
  }

  // Skips a (...) group that starts at pos; the position after it
  static size_t
  _SkipGroup (std::string_view pattern, size_t pos)
  {
    size_t i     = pos + 1;
    int    depth = 1;

    while (i < pattern.size ( ) && depth > 0)
    {
      const char ch = pattern [i];

      if      (ch == '\\') i += 2;
      else if (ch == '[')  i  = _SkipClass (pattern, i);
      else if (ch == '(') { depth++; i++; }
      else if (ch == ')') { depth--; i++; }
      else                 i++;
    }

    return std::min (i, pattern.size ( ));
  }

  // An | outside of any group or class: the expression is a choice of several, so nothing is required of all matches
  static bool
  _HasTopLevelAlternation (std::string_view pattern)
  {
    size_t i = 0;

    while (i < pattern.size ( ))
    {
      const char ch = pattern [i];

      if      (ch == '\\') i += 2;
      else if (ch == '[')  i  = _SkipClass (pattern, i);
      else if (ch == '(')  i  = _SkipGroup (pattern, i);
      else if (ch == '|')  return true;
      else                 i++;
    }

    return false;
  }

  // The extensions an expression is limited to: \.ext$ or \.(?:ext1|ext2)$ at its end (empty if it isn't)
  static std::vector <std::string>
  _GetExtensions (std::string_view pattern)
  {
    std::vector <std::string> out;

    if (_HasTopLevelAlternation (pattern) || ! pattern.ends_with ('$') || pattern.ends_with ("\\$"))
      return out;

    std::string_view body = pattern.substr (0, pattern.size ( ) - 1);

    auto _IsExtChar = [](char ch) { return (_IsAlnum (ch) || ch == '_'); };

    // \.(?:a|b)
    if (body.ends_with (')'))
    {
      const size_t open = body.rfind ("\\.(?:");

      if (open == std::string_view::npos)
        return out;

      std::string_view list = body.substr (open + 5, body.size ( ) - open - 6);

      if (list.empty ( ))
        return out;

      std::string current;

      for (char ch : list)
      {
        if (ch == '|')
        {
          if (current.empty ( ))
            return { };

          out.push_back (current);
          current.clear ( );
        }

        else if (_IsExtChar (ch))
          current += ch;

        else
          return { };
      }

      if (current.empty ( ))
        return { };

      out.push_back (current);
      return out;
    }

    // \.ext
    size_t i = body.size ( );

    while (i > 0 && _IsExtChar (body [i - 1]))
      i--;

    if (i == body.size ( ) || i < 2 || body.substr (i - 2, 2) != "\\.")
      return out;

    // Not \\.ext (an escaped backslash, then any character)
    size_t backslashes = 0;

    for (size_t j = i - 2; j > 0 && body [j - 1] == '\\'; j--)
      backslashes++;

    if (backslashes % 2 != 0)
      return out;

    out.emplace_back (body.substr (i));
    return out;
  }

  static uint64_t
  _HashFNV1a (std::string_view data)
  {
    uint64_t hash = 14695981039346656037ULL;

    for (unsigned char ch : data)
    {
      hash ^= ch;
      hash *= 1099511628211ULL;
    }

    return hash;
  }

#pragma endregion

#pragma region Patterns

  std::string
  ConvertPattern (std::string_view pcre)
  {
    // Characters that keep their escape outside of a class (ECMAScript syntax characters); others are unescaped
    static constexpr std::string_view keepEscaped      = "^$\\.*+?()[]{}|";
    static constexpr std::string_view keepEscapedClass = "\\]^-";

    std::string out;
    out.reserve (pcre.size ( ) + 8);

    bool   inClass        = false;
    bool   afterQuantifier = false; // The last thing written was a quantifier, so + makes it possessive and ? lazy
    size_t i              = 0;

    while (i < pcre.size ( ))
    {
      const char ch = pcre [i];

      if (ch == '\\' && i + 1 < pcre.size ( ))
      {
        const char next = pcre [i + 1];

        // \d, \W, \x41, ... keep their case
        if (_IsAlnum (next))
        {
          out += '\\';
          out += next;
        }

        else if ((inClass ? keepEscapedClass : keepEscaped).find (next) != std::string_view::npos)
        {
          out += '\\';
          out += next;
        }

        else
          out += next; // \_ \/ \- ...

        i += 2;
        afterQuantifier = false;
        continue;
      }

      if (inClass)
      {
        out += _Lower (ch);

        if (ch == ']')
          inClass = false;

        i++;
        continue;
      }

      if (ch == '[')
      {
        inClass = true;
        out    += '[';
        i++;

        if (i < pcre.size ( ) && pcre [i] == '^')
        {
          out += '^';
          i++;
        }

        // In PCRE a ] right at the start is part of the class; in ECMAScript [] is an empty class
        if (i < pcre.size ( ) && pcre [i] == ']')
        {
          out += "\\]";
          i++;
        }

        afterQuantifier = false;
        continue;
      }

      if (ch == '(' && i + 2 < pcre.size ( ) && pcre [i + 1] == '?')
      {
        // Atomic group: a plain group matches the same paths here, as the rules never rely on it to fail a match
        if (pcre [i + 2] == '>')
          out += "(?:";
        else
          out.append (pcre.substr (i, 3)); // (?: (?= (?! and anything std::regex will refuse ((?<= (?i) ...)

        i += 3;
        afterQuantifier = false;
        continue;
      }

      if (ch == '*' || ch == '+' || ch == '?')
      {
        if (afterQuantifier && ch == '+') // Possessive: the same matches as the plain quantifier for these rules
        {
          i++;
          afterQuantifier = false;
          continue;
        }

        if (afterQuantifier && ch == '?') // Lazy
        {
          out += '?';
          i++;
          afterQuantifier = false;
          continue;
        }

        out += ch;
        i++;
        afterQuantifier = true;
        continue;
      }

      if (ch == '{')
      {
        const size_t len = _ParseBraceQuantifier (pcre, i);

        if (len != 0)
        {
          out.append (pcre.substr (i, len));
          i += len;
          afterQuantifier = true;
          continue;
        }

        out += "\\{"; // A literal brace (PCRE allows it unescaped)
        i++;
        afterQuantifier = false;
        continue;
      }

      if (ch == '}' || ch == ']')
      {
        out += '\\';
        out += ch;
        i++;
        afterQuantifier = false;
        continue;
      }

      out += _Lower (ch);
      i++;
      afterQuantifier = false;
    }

    return out;
  }

  std::string
  GetRequiredLiteral (std::string_view pattern)
  {
    if (_HasTopLevelAlternation (pattern))
      return "";

    std::string best,
                current;

    auto _Flush = [&](void)
    {
      if (current.size ( ) > best.size ( ))
        best = current;

      current.clear ( );
    };

    size_t i = 0;

    while (i < pattern.size ( ))
    {
      const char ch      = pattern [i];
      bool       literal = false;
      char       value   = 0;

      if (ch == '\\' && i + 1 < pattern.size ( ))
      {
        const char next = pattern [i + 1];
        i += 2;

        if (_IsAlnum (next))
        {
          // \d \w \b ... match a set or nothing; \xhh \uhhhh \cX are skipped as a whole
          if      (next == 'x') i += 2;
          else if (next == 'u') i += 4;
          else if (next == 'c') i += 1;
        }

        else
        {
          literal = true;
          value   = next;
        }
      }

      else if (ch == '[')
        i = _SkipClass (pattern, i);

      else if (ch == '(')
        i = _SkipGroup (pattern, i);

      else if (ch == '.' || ch == '^' || ch == '$')
        i++;

      else
      {
        literal = true;
        value   = ch;
        i++;
      }

      // The quantifier of the atom, if any
      bool optional = false,
           repeated = false;

      if (i < pattern.size ( ))
      {
        int min = 0;

        if (pattern [i] == '*' || pattern [i] == '?')
        {
          optional = true;
          i++;
        }

        else if (pattern [i] == '+')
        {
          repeated = true;
          i++;
        }

        else if (const size_t len = _ParseBraceQuantifier (pattern, i, &min); len != 0)
        {
          optional = (min == 0);
          repeated = ! optional;
          i       += len;
        }

        if ((optional || repeated) && i < pattern.size ( ) && pattern [i] == '?')
          i++;
      }

      if (optional || ! literal)
        _Flush ( );

      else
      {
        current += value;

        if (repeated)
          _Flush ( );
      }
    }

    _Flush ( );

    return best;
  }

#pragma endregion

#pragma region Categories

  Category
  CategoryFromSection (std::string_view section)
  {
    if (section == "Engine")    return Category::Engine;
    if (section == "Evidence")  return Category::Evidence;
    if (section == "Container") return Category::Container;
    if (section == "Emulator")  return Category::Emulator;
    if (section == "AntiCheat") return Category::AntiCheat;
    if (section == "SDK")       return Category::SDK;
    if (section == "Launcher")  return Category::Launcher;

    return Category::Other;
  }

  const char*
  CategoryToSection (Category category)
  {
    switch (category)
    {
      case Category::Engine:    return "Engine";
      case Category::Evidence:  return "Evidence";
      case Category::Container: return "Container";
      case Category::Emulator:  return "Emulator";
      case Category::AntiCheat: return "AntiCheat";
      case Category::SDK:       return "SDK";
      case Category::Launcher:  return "Launcher";
      case Category::Other:     break;
    }

    return "";
  }

  std::string
  GetDisplayName (std::string_view name)
  {
    std::string out (name);

    for (size_t pos = out.find ("_OR_"); pos != std::string::npos; pos = out.find ("_OR_", pos + 3))
      out.replace (pos, 4, " / ");

    std::replace (out.begin ( ), out.end ( ), '_', ' ');

    return out;
  }

#pragma endregion

#pragma region result_s

  const detection_s*
  result_s::Find (std::string_view id) const
  {
    for (const auto& detection : detections)
      if (detection.id == id)
        return &detection;

    return nullptr;
  }

  bool
  result_s::Has (std::string_view id) const
  {
    return (Find (id) != nullptr);
  }

  bool
  result_s::Has (Category category) const
  {
    for (const auto& detection : detections)
      if (detection.category == category)
        return true;

    return false;
  }

  std::vector <const detection_s*>
  result_s::Get (Category category) const
  {
    std::vector <const detection_s*> out;

    for (const auto& detection : detections)
      if (detection.category == category)
        out.push_back (&detection);

    return out;
  }

  std::string
  result_s::GetNames (Category category, const char* separator) const
  {
    std::string out;

    for (const auto& detection : detections)
    {
      if (detection.category != category)
        continue;

      if (! out.empty ( ))
        out += separator;

      out += GetDisplayName (detection.name);
    }

    return out;
  }

#pragma endregion

#pragma region RuleSet

  struct rule_s {
    std::string section;  // As in rules.ini, e.g. AntiCheat
    std::string name;     // e.g. EasyAntiCheat
    std::string id;       // section.name
    Category    category = Category::Other;
    size_t      sectionIndex = 0;
    std::string literal;  // Required substring ("" if none)
    std::regex  regex;
  };

  struct RuleSet::impl_s {
    std::vector <rule_s>       rules;          // In the order of rules.ini
    std::vector <std::string>  sections;       // Indexed by rule_s::sectionIndex
    std::unordered_map <std::string, std::vector <uint32_t>>
                               byExtension;    // Rules to try on paths with an extension: its own and anyExtension, in order
    std::vector <uint32_t>     anyExtension;   // Rules not limited to an extension
    uint64_t                   hash = 0;

    const std::vector <uint32_t>& GetCandidates (std::string_view ext) const
    {
      if (! ext.empty ( ))
      {
        auto it = byExtension.find (std::string (ext));

        if (it != byExtension.end ( ))
          return it->second;
      }

      return anyExtension;
    }
  };

  RuleSet::RuleSet  (void) : impl (std::make_unique <impl_s> ( )) { }
  RuleSet::~RuleSet (void) = default;

  size_t
  RuleSet::GetRuleCount (void) const
  {
    return impl->rules.size ( );
  }

  uint64_t
  RuleSet::GetHash (void) const
  {
    return impl->hash;
  }

  bool
  RuleSet::Parse (std::string_view ini, std::vector <std::string>* errors)
  {
    auto next = std::make_unique <impl_s> ( );

    next->hash = _HashFNV1a (ini);

    if (ini.starts_with ("\xEF\xBB\xBF")) // UTF-8 BOM
      ini.remove_prefix (3);

    std::string section;
    std::vector <std::vector <std::string>> ruleExtensions; // By rule index

    size_t pos = 0;

    while (pos < ini.size ( ))
    {
      size_t end = ini.find ('\n', pos);

      if (end == std::string_view::npos)
        end = ini.size ( );

      std::string_view line = _Trim (ini.substr (pos, end - pos));
      pos = end + 1;

      if (line.empty ( ) || line.front ( ) == ';' || line.front ( ) == '#')
        continue;

      if (line.front ( ) == '[')
      {
        const size_t close = line.find (']');

        if (close != std::string_view::npos)
          section = std::string (_Trim (line.substr (1, close - 1)));

        continue;
      }

      const size_t equals = line.find ('=');

      if (equals == std::string_view::npos || section.empty ( ))
        continue;

      std::string_view key   = _Trim (line.substr (0, equals));
      std::string_view value = line.substr (equals + 1);

      // An inline comment: a ; after whitespace ("ARC = \.arc$ ; Apex Engine")
      for (size_t i = 1; i < value.size ( ); i++)
      {
        if (value [i] == ';' && (value [i - 1] == ' ' || value [i - 1] == '\t'))
        {
          value = value.substr (0, i);
          break;
        }
      }

      value = _Trim (value);

      if (value.size ( ) >= 2 && value.front ( ) == '"' && value.back ( ) == '"')
        value = value.substr (1, value.size ( ) - 2);

      if (key.ends_with ("[]"))
        key = _Trim (key.substr (0, key.size ( ) - 2));

      if (key.empty ( ) || value.empty ( ))
        continue;

      rule_s rule;
      rule.section  = section;
      rule.name     = std::string (key);
      rule.id       = section + "." + rule.name;
      rule.category = CategoryFromSection (section);

      const std::string converted = ConvertPattern (value);

      try {
        rule.regex = std::regex (converted, std::regex::ECMAScript | std::regex::nosubs | std::regex::optimize);
      }

      catch (const std::regex_error& e)
      {
        if (errors != nullptr)
          errors->push_back (rule.id + ": " + std::string (value) + " (" + e.what ( ) + ")");

        continue;
      }

      rule.literal = GetRequiredLiteral (converted);

      auto it = std::find (next->sections.begin ( ), next->sections.end ( ), section);

      rule.sectionIndex = static_cast <size_t> (it - next->sections.begin ( ));

      if (it == next->sections.end ( ))
        next->sections.push_back (section);

      ruleExtensions.push_back (_GetExtensions (converted));
      next->rules.push_back (std::move (rule));
    }

    if (next->rules.empty ( ))
      return false;

    // Buckets: each extension gets its own rules plus those for any extension, in the order of rules.ini
    for (uint32_t index = 0; index < next->rules.size ( ); index++)
    {
      if (ruleExtensions [index].empty ( ))
        next->anyExtension.push_back (index);
      else
        for (const auto& ext : ruleExtensions [index])
          next->byExtension [ext]; // Created here, filled below
    }

    for (auto& [ext, list] : next->byExtension)
    {
      for (uint32_t index = 0; index < next->rules.size ( ); index++)
      {
        const auto& exts = ruleExtensions [index];

        if (exts.empty ( ) || std::find (exts.begin ( ), exts.end ( ), ext) != exts.end ( ))
          list.push_back (index);
      }
    }

    impl = std::move (next);

    return true;
  }

#pragma endregion

#pragma region Matcher

  struct match_s {
    uint32_t    count = 0;
    std::string example;
  };

  struct RuleSet::Matcher::impl_s {
    const RuleSet::impl_s&               rules;
    std::map <std::string, match_s>      matches;       // By rule ID
    std::vector <uint32_t>               sectionStamp;  // Index of the last path that matched a rule of each section
    uint32_t                             pathIndex = 0;

    // IsEngineGodot ( ) of FileDetector.php, collected as the paths come in
    std::vector <std::string>            pcks;          // .pck files
    std::unordered_set <std::string>     pckForExe;     // The .pck an executable would load (game.exe -> game.pck)

    explicit impl_s (const RuleSet::impl_s& r) : rules (r), sectionStamp (r.sections.size ( ), UINT32_MAX) { }

    void Add (const std::string& path, bool isFolder)
    {
      if (path.empty ( ))
        return;

      const uint32_t index = pathIndex++;
      const std::string_view ext = _GetExtension (path);

      for (uint32_t ruleIndex : rules.GetCandidates (ext))
      {
        const rule_s& rule = rules.rules [ruleIndex];

        if (sectionStamp [rule.sectionIndex] == index) // This path already counts for a rule of this section
          continue;

        if (! rule.literal.empty ( ) && path.find (rule.literal) == std::string::npos)
          continue;

        if (! std::regex_search (path, rule.regex))
          continue;

        sectionStamp [rule.sectionIndex] = index;

        match_s& match = matches [rule.id];

        if (match.count++ == 0)
          match.example = path;
      }

      if (! isFolder)
        _AddGodotCandidate (path, ext);
    }

    void _AddGodotCandidate (const std::string& path, std::string_view ext)
    {
      if (ext == "pck")
      {
        pcks.push_back (path);
        return;
      }

      if (ext.empty ( ))
      {
        // Executables on Linux and macOS have no extension; on macOS the .pck is in Resources
        std::string file = path;

        const size_t slash = file.rfind ('/');

        if (slash != std::string::npos && _EndsWith (std::string_view (file).substr (0, slash), "/macos"))
        {
          for (size_t pos = file.find ("/macos/"); pos != std::string::npos; pos = file.find ("/macos/", pos + 11))
            file.replace (pos, 7, "/resources/");
        }

        pckForExe.insert (file + ".pck");
      }

      else if (ext == "exe" || ext == "x86" || ext == "x86_32" || ext == "x86_64")
        pckForExe.insert (path.substr (0, path.size ( ) - ext.size ( )) + "pck");
    }

    bool _IsEngineGodot (void) const
    {
      if (pcks.empty ( ))
        return false;

      bool onlyDataPck = true;

      for (const auto& pck : pcks)
      {
        const size_t slash = pck.rfind ('/');
        const std::string_view name = (slash == std::string::npos) ? std::string_view (pck) : std::string_view (pck).substr (slash + 1);

        if (name != "data.pck")
          onlyDataPck = false;

        if (pckForExe.count (pck) != 0)
          return true;
      }

      return onlyDataPck;
    }

    // TryDeduceEngine ( ) of FileDetector.php: at most one engine from combinations of evidence
    std::string _DeduceEngine (void) const
    {
      auto _has = [&](const char* id) { return (matches.count (id) != 0); };
      auto _not = [&](const char* id) { return (matches.count (id) == 0); };

      if (_has ("Evidence.ARC") && _has ("Evidence.TAB"))
        return "Engine.ApexEngine";

      if (_has ("Evidence.RPF") && _has ("Evidence.METADATA_DAT"))
        return "Engine.RAGE";

      if (_has ("Evidence.HDLL") && _not ("Engine.Lime_OR_OpenFL"))
        return "Engine.Heaps";

      if (_has ("Emulator.DOSBOX"))
      {
        if (_has ("Evidence.Build"))
          return "Engine.Build";

        if (_has ("Evidence.VSWAP") || (_has ("Evidence.CFG") && _has ("Evidence.WAD")))
          return "Engine.idTech";
      }

      if (_has ("Evidence.U") && _not ("Emulator.DOSBOX"))
        return "Engine.Unreal";

      if (_has ("Evidence.BIF") && _has ("Evidence.TLK"))
      {
        if (_has ("Evidence.RIM") || _has ("Evidence.TGA"))
          return "Engine.Aurora";

        return "Engine.Infinity";
      }

      if ((_has ("Evidence.OPTIONS_INI") ? 1 : 0) + (_has ("Evidence.DATA_WIN") ? 1 : 0) + (_has ("Evidence.SND_OGG") ? 1 : 0) > 1)
        return "Engine.GameMaker";

      if (_has ("Evidence.SIERRA_EXE") && _has ("Evidence.SCR"))
        return "Engine.SCI";

      if (_has ("Evidence.PCK") && _IsEngineGodot ( ))
        return "Engine.Godot";

      if (_has ("Evidence.PK3"))
        return "Engine.idTech";

      return "";
    }

    result_s Finish (void)
    {
      result_s result;
      result.state = result_s::State::Done;

      for (const auto& [id, match] : matches)
      {
        const size_t dot = id.find ('.');

        detection_s detection;
        detection.id       = id;
        detection.name     = id.substr (dot + 1);
        detection.category = CategoryFromSection (std::string_view (id).substr (0, dot));
        detection.matches  = match.count;
        detection.example  = match.example;

        if (detection.category != Category::Evidence)
          result.detections.push_back (std::move (detection));
      }

      if (! matches.empty ( ))
      {
        const std::string deduced = _DeduceEngine ( );

        if (! deduced.empty ( ) && ! result.Has (deduced))
        {
          detection_s detection;
          detection.id       = deduced;
          detection.name     = deduced.substr (deduced.find ('.') + 1);
          detection.category = Category::Engine;
          detection.deduced  = true;

          result.detections.push_back (std::move (detection));
        }
      }

      std::sort (result.detections.begin ( ), result.detections.end ( ),
        [](const detection_s& a, const detection_s& b)
        {
          if (a.category != b.category)
            return (static_cast <int> (a.category) < static_cast <int> (b.category));

          return std::lexicographical_compare (a.name.begin ( ), a.name.end ( ), b.name.begin ( ), b.name.end ( ),
            [](char x, char y) { return (_Lower (x) < _Lower (y)); });
        });

      return result;
    }
  };

  RuleSet::Matcher::Matcher  (const RuleSet& rules) : impl (std::make_unique <impl_s> (*rules.impl)) { }
  RuleSet::Matcher::~Matcher (void) = default;

  void
  RuleSet::Matcher::Add (std::string_view path, bool isFolder)
  {
    impl->Add (_NormalizePath (path), isFolder);
  }

  result_s
  RuleSet::Matcher::Finish (void)
  {
    return impl->Finish ( );
  }

  result_s
  RuleSet::MatchPaths (const std::vector <std::string>& paths) const
  {
    Matcher matcher (*this);

    for (const auto& path : paths)
      matcher.Add (path, false);

    result_s result = matcher.Finish ( );
    result.entries  = static_cast <uint32_t> (paths.size ( ));

    return result;
  }

#pragma endregion
}

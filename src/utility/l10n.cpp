#include <utility/l10n.h>

#include <windows.h>

#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>

// The resource type of the embedded .po files (l10n.rc)
static constexpr const wchar_t* SKIF_L10N_RESOURCE_TYPE = L"SKIF_PO";

#pragma region Helpers

// Transparent hash so std::string_view lookups need no allocation
struct SKIF_L10n_StringHash {
  using is_transparent = void;
  size_t operator() (std::string_view sv)   const { return std::hash <std::string_view> { } (sv); }
  size_t operator() (const std::string& s)  const { return std::hash <std::string_view> { } (s);  }
  size_t operator() (const char* s)         const { return std::hash <std::string_view> { } (s);  }
};

template <class _Tp>
using SKIF_L10n_StringMap = std::unordered_map <std::string, _Tp, SKIF_L10n_StringHash, std::equal_to <>>;

static std::string
SKIF_L10n_ToLower (std::string_view in)
{
  std::string out (in);
  for (auto& ch : out)
    if (ch >= 'A' && ch <= 'Z')
      ch = ch - 'A' + 'a';
  return out;
}

static std::wstring
SKIF_L10n_ToWide (std::string_view in)
{
  if (in.empty ( ))
    return std::wstring ( );

  int len = MultiByteToWideChar (CP_UTF8, 0, in.data ( ), static_cast <int> (in.size ( )), nullptr, 0);
  std::wstring out (static_cast <size_t> (std::max (len, 0)), L'\0');
  if (len > 0)
    MultiByteToWideChar (CP_UTF8, 0, in.data ( ), static_cast <int> (in.size ( )), out.data ( ), len);
  return out;
}

static std::string
SKIF_L10n_ToUTF8 (std::wstring_view in)
{
  if (in.empty ( ))
    return std::string ( );

  int len = WideCharToMultiByte (CP_UTF8, 0, in.data ( ), static_cast <int> (in.size ( )), nullptr, 0, nullptr, nullptr);
  std::string out (static_cast <size_t> (std::max (len, 0)), '\0');
  if (len > 0)
    WideCharToMultiByte (CP_UTF8, 0, in.data ( ), static_cast <int> (in.size ( )), out.data ( ), len, nullptr, nullptr);
  return out;
}

static std::string_view
SKIF_L10n_Trim (std::string_view sv)
{
  while (! sv.empty ( ) && (sv.front ( ) == ' ' || sv.front ( ) == '\t' || sv.front ( ) == '\r' || sv.front ( ) == '\n'))
    sv.remove_prefix (1);
  while (! sv.empty ( ) && (sv.back  ( ) == ' ' || sv.back  ( ) == '\t' || sv.back  ( ) == '\r' || sv.back  ( ) == '\n'))
    sv.remove_suffix (1);
  return sv;
}

// gettext language names (pt_BR, sr@latin) to BCP 47 tags (pt-BR, sr-Latn)
static std::string
SKIF_L10n_NormalizeTag (std::string_view in)
{
  std::string tag (SKIF_L10n_Trim (in));

  size_t at = tag.find ('@');
  if (at != std::string::npos)
  {
    std::string modifier = SKIF_L10n_ToLower (tag.substr (at + 1));
    tag.resize (at);
    if (modifier == "latin")
      tag += "-Latn";
    else if (modifier == "cyrillic")
      tag += "-Cyrl";
  }

  std::replace (tag.begin ( ), tag.end ( ), '_', '-');
  return tag;
}

static std::string
SKIF_L10n_PrimaryLanguage (const std::string& tag)
{
  return SKIF_L10n_ToLower (tag.substr (0, tag.find ('-')));
}

// Chinese: Simplified (Hans) or Traditional (Hant), from the script or the region of the tag
static bool
SKIF_L10n_IsTraditionalChinese (const std::string& tag)
{
  std::string lower = SKIF_L10n_ToLower (tag);

  if (lower.find ("-hant") != std::string::npos)
    return true;
  if (lower.find ("-hans") != std::string::npos)
    return false;

  return (lower.find ("-tw") != std::string::npos ||
          lower.find ("-hk") != std::string::npos ||
          lower.find ("-mo") != std::string::npos);
}

#pragma endregion

#pragma region Plural forms

// Evaluates the C expression of a Plural-Forms header, e.g. "nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : ...);"
class SKIF_L10n_PluralRule
{
public:
  int nplurals = 2;

  bool Parse (std::string_view header_value)
  {
    _nodes.clear ( );
    _root = -1;

    std::string_view rest = header_value;
    std::string_view expr;

    while (! rest.empty ( ))
    {
      size_t           semicolon = rest.find (';');
      std::string_view part      = SKIF_L10n_Trim (rest.substr (0, semicolon));
      rest = (semicolon == std::string_view::npos) ? std::string_view ( ) : rest.substr (semicolon + 1);

      size_t eq = part.find ('=');
      if (eq == std::string_view::npos)
        continue;

      std::string_view key   = SKIF_L10n_Trim (part.substr (0, eq));
      std::string_view value = SKIF_L10n_Trim (part.substr (eq + 1));

      if (key == "nplurals")
        nplurals = std::clamp (std::atoi (std::string (value).c_str ( )), 1, 6);
      else if (key == "plural")
        expr = value;
    }

    if (expr.empty ( ))
      return false;

    _src = expr;
    _pos = 0;
    _root = ParseTernary ( );
    SkipBlanks ( );

    if (_root < 0 || _pos != _src.size ( ))
    {
      _nodes.clear ( );
      _root = -1;
      return false;
    }

    return true;
  }

  int Evaluate (unsigned long long n) const
  {
    long long index = (_root < 0) ? ((n == 1) ? 0 : 1)
                                  :  Eval (_root, n);
    return static_cast <int> (std::clamp (index, 0LL, static_cast <long long> (nplurals - 1)));
  }

private:
  struct Node {
    char      op    = 0;   // 'n', '#' (number), '!', '?', or a binary operator: | & = x (!=) < > l (<=) g (>=) + - * / %
    long long value = 0;
    int       a     = -1, b = -1, c = -1;
  };

  std::vector <Node> _nodes;
  int                _root = -1;
  std::string_view   _src;
  size_t             _pos  = 0;

  int Add (Node node) { _nodes.push_back (node); return static_cast <int> (_nodes.size ( )) - 1; }

  void SkipBlanks (void)
  {
    while (_pos < _src.size ( ) && (_src [_pos] == ' ' || _src [_pos] == '\t'))
      _pos++;
  }

  bool Accept (std::string_view token)
  {
    SkipBlanks ( );
    if (_src.substr (_pos, token.size ( )) == token)
    {
      // Do not take the first character of "<=" as "<", "!=" as "!", "==" as "=", ...
      if (token.size ( ) == 1 && _pos + 1 < _src.size ( ) && _src [_pos + 1] == '=' && (token == "<" || token == ">" || token == "!"))
        return false;
      _pos += token.size ( );
      return true;
    }
    return false;
  }

  int ParseTernary (void)
  {
    int cond = ParseOr ( );
    if (cond >= 0 && Accept ("?"))
    {
      int a = ParseTernary ( );
      if (a < 0 || ! Accept (":"))
        return -1;
      int b = ParseTernary ( );
      if (b < 0)
        return -1;
      return Add ({ '?', 0, cond, a, b });
    }
    return cond;
  }

  int ParseBinary (int (SKIF_L10n_PluralRule::*next) (void), std::initializer_list <std::pair <std::string_view, char>> ops)
  {
    int lhs = (this->*next) ( );
    while (lhs >= 0)
    {
      char op = 0;
      for (auto& candidate : ops)
        if (Accept (candidate.first)) { op = candidate.second; break; }
      if (op == 0)
        break;
      int rhs = (this->*next) ( );
      if (rhs < 0)
        return -1;
      lhs = Add ({ op, 0, lhs, rhs });
    }
    return lhs;
  }

  int ParseOr         (void) { return ParseBinary (&SKIF_L10n_PluralRule::ParseAnd,        { { "||", '|' } }); }
  int ParseAnd        (void) { return ParseBinary (&SKIF_L10n_PluralRule::ParseEquality,   { { "&&", '&' } }); }
  int ParseEquality   (void) { return ParseBinary (&SKIF_L10n_PluralRule::ParseRelational, { { "==", '=' }, { "!=", 'x' } }); }
  int ParseRelational (void) { return ParseBinary (&SKIF_L10n_PluralRule::ParseAdditive,   { { "<=", 'l' }, { ">=", 'g' }, { "<", '<' }, { ">", '>' } }); }
  int ParseAdditive   (void) { return ParseBinary (&SKIF_L10n_PluralRule::ParseMultiply,   { { "+",  '+' }, { "-",  '-' } }); }
  int ParseMultiply   (void) { return ParseBinary (&SKIF_L10n_PluralRule::ParseUnary,      { { "*",  '*' }, { "/",  '/' }, { "%", '%' } }); }

  int ParseUnary (void)
  {
    if (Accept ("!"))
    {
      int a = ParseUnary ( );
      return (a < 0) ? -1 : Add ({ '!', 0, a });
    }
    return ParsePrimary ( );
  }

  int ParsePrimary (void)
  {
    SkipBlanks ( );

    if (Accept ("("))
    {
      int a = ParseTernary ( );
      return (a >= 0 && Accept (")")) ? a : -1;
    }

    if (Accept ("n"))
      return Add ({ 'n' });

    if (_pos < _src.size ( ) && _src [_pos] >= '0' && _src [_pos] <= '9')
    {
      long long value = 0;
      while (_pos < _src.size ( ) && _src [_pos] >= '0' && _src [_pos] <= '9')
        value = value * 10 + (_src [_pos++] - '0');
      return Add ({ '#', value });
    }

    return -1;
  }

  long long Eval (int index, unsigned long long n) const
  {
    const Node& node = _nodes [index];

    switch (node.op)
    {
      case 'n': return static_cast <long long> (n);
      case '#': return node.value;
      case '!': return ! Eval (node.a, n);
      case '?': return Eval (node.a, n) ? Eval (node.b, n) : Eval (node.c, n);
      case '|': return Eval (node.a, n) || Eval (node.b, n);
      case '&': return Eval (node.a, n) && Eval (node.b, n);
    }

    long long a = Eval (node.a, n),
              b = Eval (node.b, n);

    switch (node.op)
    {
      case '=': return a == b;
      case 'x': return a != b;
      case '<': return a <  b;
      case '>': return a >  b;
      case 'l': return a <= b;
      case 'g': return a >= b;
      case '+': return a +  b;
      case '-': return a -  b;
      case '*': return a *  b;
      case '/': return (b != 0) ? a / b : 0;
      case '%': return (b != 0) ? a % b : 0;
    }

    return 0;
  }
};

#pragma endregion

#pragma region Catalog

struct SKIF_L10n::Catalog
{
  enum Kind { Kind_English, Kind_Pseudo, Kind_Translation };

  Kind                    kind       = Kind_Translation;
  std::string             tag;
  std::string             source;     // Where it was loaded from (for the log)
  int                     total      = 0;
  int                     translated = 0; // At startup, counted by SKIF_L10n_ScanPO ( ); exact once loaded
  SKIF_L10n_PluralRule    plural;

  // The entries are only read when the language is first used (at startup, or when switching to it): until then, only the
  //   header and the counts are known. Main thread only (SKIF_L10n::Initialize ( ) / ApplyPendingLanguage ( )).
  bool                    loaded     = true;
  const char*             resData    = nullptr; // Embedded: the resource, mapped for the lifetime of the process
  size_t                  resSize    = 0;
  std::wstring            path;                 // Loose file (read again when loaded)

  // Key: msgid, or msgctxt '\x04' msgid (as with gettext). Value: msgstr, or msgstr[0..nplurals)
  SKIF_L10n_StringMap <std::vector <std::string>> entries;

  // Generated on demand (node-based containers, so references remain valid as they grow)
  std::mutex                              lockPseudo, lockLabels, lockWide;
  SKIF_L10n_StringMap <std::string>       pseudo;
  SKIF_L10n_StringMap <std::string>       labels;
  SKIF_L10n_StringMap <std::wstring>      wide;
};

// Placeholders ({0}, {1:>3}, {steam}, ...) in a pattern; false if the braces do not pair up
static bool
SKIF_L10n_GetPlaceholders (std::string_view text, std::multiset <std::string_view>* out)
{
  for (size_t i = 0; i < text.size ( ); i++)
  {
    if (text [i] == '{')
    {
      if (i + 1 < text.size ( ) && text [i + 1] == '{') { i++; continue; } // "{{"

      size_t close = text.find ('}', i);
      if (close == std::string_view::npos)
        return false;
      if (out != nullptr)
        out->insert (text.substr (i, close - i + 1));
      i = close;
    }

    else if (text [i] == '}')
    {
      if (i + 1 < text.size ( ) && text [i + 1] == '}') { i++; continue; } // "}}"
      return false;
    }
  }
  return true;
}

// A translation may only use placeholders that the English text has (it may omit or repeat them)
static bool
SKIF_L10n_ValidatePlaceholders (const std::string& msgid, const std::string& msgid_plural, const std::string& msgstr)
{
  std::multiset <std::string_view> allowed;
  if (! SKIF_L10n_GetPlaceholders (msgid,        &allowed) ||
      ! SKIF_L10n_GetPlaceholders (msgid_plural, &allowed))
    return true; // The English text has stray braces, so it is not a pattern

  std::multiset <std::string_view> used;
  if (! SKIF_L10n_GetPlaceholders (msgstr, &used))
    return false;

  for (auto& placeholder : used)
    if (allowed.find (placeholder) == allowed.end ( ))
      return false;

  return true;
}

// Appends the content of the quoted string at pos (the opening quote) to out, and returns the position after its closing
//   quote, or std::string_view::npos on syntax errors
static size_t
SKIF_L10n_ParseQuotedOne (std::string_view line, size_t pos, std::string& out)
{
  for (size_t i = pos + 1; i < line.size ( ); i++)
  {
    char ch = line [i];

    if (ch == '"')
      return i + 1;

    if (ch != '\\')
    {
      out += ch;
      continue;
    }

    if (++i >= line.size ( ))
      return false;

    switch (line [i])
    {
      case 'n':  out += '\n'; break;
      case 't':  out += '\t'; break;
      case 'r':  out += '\r'; break;
      case 'a':  out += '\a'; break;
      case 'b':  out += '\b'; break;
      case 'f':  out += '\f'; break;
      case 'v':  out += '\v'; break;
      case '"':  out += '"';  break;
      case '\'': out += '\''; break;
      case '?':  out += '?';  break;
      case '\\': out += '\\'; break;
      case 'x':
      {
        unsigned int value  = 0;
        int          digits = 0;
        while (i + 1 < line.size ( ) && isxdigit (static_cast <unsigned char> (line [i + 1])))
        {
          char d = line [++i];
          value  = value * 16 + ((d <= '9') ? (d - '0') : ((d | 0x20) - 'a' + 10));
          digits++;
        }
        if (digits == 0)
          return std::string_view::npos;
        out += static_cast <char> (value & 0xFF);
        break;
      }
      default:
        if (line [i] >= '0' && line [i] <= '7')
        {
          unsigned int value = line [i] - '0';
          for (int k = 0; k < 2 && i + 1 < line.size ( ) && line [i + 1] >= '0' && line [i + 1] <= '7'; k++)
            value = value * 8 + (line [++i] - '0');
          out += static_cast <char> (value & 0xFF);
        }
        else
          return std::string_view::npos;
    }
  }

  return std::string_view::npos; // No closing quote
}

// Appends the content of the quoted strings from pos on (one or more: "a" "b") to out; false on syntax errors
static bool
SKIF_L10n_ParseQuoted (std::string_view line, size_t pos, std::string& out)
{
  bool any = false;

  while (pos < line.size ( ))
  {
    pos = line.find_first_not_of (" \t", pos);
    if (pos == std::string_view::npos)
      break;

    if (line [pos] != '"')
      return false;

    pos = SKIF_L10n_ParseQuotedOne (line, pos, out);
    if (pos == std::string_view::npos)
      return false;

    any = true;
  }

  return any;
}

// The header of a .po file (the msgstr of the entry with an empty msgid): its language, and its plural forms (if plural is
//   not nullptr). True if it has a Plural-Forms line.
static bool
SKIF_L10n_ParseHeader (std::string_view header, const SKIF_L10n::Catalog& cat, std::string& language, SKIF_L10n_PluralRule* plural)
{
  bool has_plural_forms = false;

  while (! header.empty ( ))
  {
    size_t           nl   = header.find ('\n');
    std::string_view line = header.substr (0, nl);
    header = (nl == std::string_view::npos) ? std::string_view ( ) : header.substr (nl + 1);

    size_t colon = line.find (':');
    if (colon == std::string_view::npos)
      continue;

    std::string      key   = SKIF_L10n_ToLower (SKIF_L10n_Trim (line.substr (0, colon)));
    std::string_view value = SKIF_L10n_Trim (line.substr (colon + 1));

    if (key == "language")
      language = SKIF_L10n_NormalizeTag (value);
    else if (key == "plural-forms")
    {
      has_plural_forms = true;
      if (plural != nullptr && ! plural->Parse (value))
        PLOG_WARNING << "[L10n] " << cat.source << ": could not parse the Plural-Forms header \"" << std::string (value) << "\"; using n != 1.";
    }
  }

  return has_plural_forms;
}

// Reads only what is needed to list a .po file in the language picker, without building its entries (fast, at startup):
//   the language of its header, and how many entries it has and has translated. The count is exact but for translations
//     that SKIF_L10n_ParsePO ( ) skips for their placeholders or missing plural forms (it updates the count when loaded).
static std::string
SKIF_L10n_ScanPO (std::string_view data, SKIF_L10n::Catalog& cat)
{
  std::string language;

  // UTF-8 BOM
  if (data.size ( ) >= 3 && data.substr (0, 3) == "\xEF\xBB\xBF")
    data.remove_prefix (3);

  enum Field { Field_None, Field_Ctx, Field_Id, Field_Plural, Field_Str };

  struct {
    bool        fuzzy   = false;
    bool        has_ctx = false, has_id = false, id_text = false, has_str = false;
    Field       field   = Field_None;
    bool        str_text = false;          // The current msgstr[n] has text
    int         strs = 0, strs_text = 0;   // msgstr[n] seen, and with text
    std::string header;                    // The msgstr of the header entry (only that one is unescaped)
  } e;

  // Whether quoted strings ("..." "...") have text
  auto _HasText = [](std::string_view quoted) -> bool
  {
    return (quoted.find_first_not_of ("\" \t") != std::string_view::npos);
  };

  auto _EndStr = [&](void)
  {
    if (e.field == Field_Str)
    {
      e.strs++;
      if (e.str_text)
        e.strs_text++;
    }
  };

  auto _Finish = [&](void)
  {
    _EndStr ( );

    if (e.has_id)
    {
      if (! e.has_ctx && ! e.id_text)
        SKIF_L10n_ParseHeader (e.header, cat, language, nullptr);

      else
      {
        cat.total++;

        if (! e.fuzzy && e.strs > 0 && e.strs_text == e.strs)
          cat.translated++;
      }
    }

    e = { };
  };

  while (! data.empty ( ))
  {
    size_t           nl   = data.find ('\n');
    std::string_view line = data.substr (0, nl);
    data = (nl == std::string_view::npos) ? std::string_view ( ) : data.substr (nl + 1);

    line = SKIF_L10n_Trim (line);

    if (line.empty ( ))
    {
      _Finish ( );
      continue;
    }

    if (line [0] == '#')
    {
      if (e.has_str)
        _Finish ( );

      if (line.size ( ) > 1 && line [1] == ',' && line.find ("fuzzy") != std::string_view::npos)
        e.fuzzy = true;
      continue;
    }

    const bool header_entry = (! e.has_ctx && ! e.id_text);

    if (line [0] == '"')
    {
      const bool text = _HasText (line);

      if      (e.field == Field_Id  && text) e.id_text  = true;
      else if (e.field == Field_Str && text) e.str_text = true;

      if (e.field == Field_Str && header_entry)
        SKIF_L10n_ParseQuoted (line, 0, e.header);
      continue;
    }

    size_t           space   = line.find_first_of (" \t");
    std::string_view keyword = line.substr (0, space);
    std::string_view rest    = (space == std::string_view::npos) ? std::string_view ( ) : line.substr (space);

    if ((keyword == "msgctxt" || keyword == "msgid") && e.has_str)
      _Finish ( );

    _EndStr ( );

    if (keyword == "msgctxt")
    {
      e.has_ctx = true;
      e.field   = Field_Ctx;
    }

    else if (keyword == "msgid")
    {
      e.has_id  = true;
      e.id_text = _HasText (rest);
      e.field   = Field_Id;
    }

    else if (keyword == "msgid_plural")
      e.field   = Field_Plural;

    else if (keyword == "msgstr" || keyword.substr (0, 7) == "msgstr[")
    {
      e.has_str  = true;
      e.field    = Field_Str;
      e.str_text = _HasText (rest);

      if (! e.has_ctx && ! e.id_text && space != std::string_view::npos)
        SKIF_L10n_ParseQuoted (line, space, e.header);
    }

    else
      e.field = Field_None;
  }

  _Finish ( );

  return language;
}

// Parses a gettext .po file. Untranslated and fuzzy entries are skipped (they show in English), as are translations
//   that use placeholders the English text does not have. Returns the language of the header ("Language:"), if any.
static std::string
SKIF_L10n_ParsePO (std::string_view data, SKIF_L10n::Catalog& cat)
{
  std::string language;
  bool        has_plural_forms = false;

  // UTF-8 BOM
  if (data.size ( ) >= 3 && data.substr (0, 3) == "\xEF\xBB\xBF")
    data.remove_prefix (3);

  struct {
    bool                       fuzzy   = false;
    bool                       has_ctx = false, has_id = false, has_plural = false, has_str = false;
    std::string                ctx, id, plural;
    std::vector <std::string>  str;
    std::string*               field   = nullptr; // Target of continuation lines
  } e;

  int line_no      = 0;
  int syntax_error = 0; // Line of the first syntax error

  std::vector <std::string> plural_keys; // Checked against nplurals once the header has been read

  auto _Finish = [&](void)
  {
    if (e.has_id)
    {
      if (! e.has_ctx && e.id.empty ( ))
      {
        // Header
        if (SKIF_L10n_ParseHeader ((e.str.empty ( )) ? std::string_view ( ) : std::string_view (e.str [0]), cat, language, &cat.plural))
          has_plural_forms = true;
      }

      else
      {
        cat.total++;

        bool complete = ! e.fuzzy && ! e.str.empty ( );
        for (size_t i = 0; complete && i < e.str.size ( ); i++)
          complete = ! e.str [i].empty ( ) && SKIF_L10n_ValidatePlaceholders (e.id, e.plural, e.str [i]);

        if (complete && ! e.has_plural && e.str.size ( ) != 1)
          complete = false;

        if (complete)
        {
          std::string key = (e.has_ctx) ? (e.ctx + '\x04' + e.id) : e.id;
          if (e.has_plural)
            plural_keys.push_back (key);
          cat.entries [key] = std::move (e.str);
          cat.translated++;
        }

        else if (! e.fuzzy && ! e.str.empty ( ) && ! e.str [0].empty ( ))
          PLOG_WARNING << "[L10n] " << cat.source << ": skipped the translation of \"" << e.id << "\" (its {placeholders} do not match the English text).";
      }
    }

    e = { };
  };

  while (! data.empty ( ))
  {
    size_t           nl   = data.find ('\n');
    std::string_view line = data.substr (0, nl);
    data = (nl == std::string_view::npos) ? std::string_view ( ) : data.substr (nl + 1);
    line_no++;

    line = SKIF_L10n_Trim (line);

    if (line.empty ( ))
    {
      _Finish ( );
      continue;
    }

    if (line [0] == '#')
    {
      // A comment after a translation starts the next entry
      if (e.has_str)
        _Finish ( );

      // Flags (fuzzy); obsolete entries (#~) and other comments are ignored
      if (line.size ( ) > 1 && line [1] == ',' && line.find ("fuzzy") != std::string_view::npos)
        e.fuzzy = true;
      continue;
    }

    if (line [0] == '"')
    {
      if (e.field == nullptr || ! SKIF_L10n_ParseQuoted (line, 0, *e.field))
        syntax_error = (syntax_error) ? syntax_error : line_no;
      continue;
    }

    size_t           space   = line.find_first_of (" \t");
    std::string_view keyword = line.substr (0, space);

    if (space == std::string_view::npos)
    {
      syntax_error = (syntax_error) ? syntax_error : line_no;
      continue;
    }

    // A new msgctxt/msgid after a translation starts the next entry (without a blank line or comment in between)
    if ((keyword == "msgctxt" || keyword == "msgid") && e.has_str)
      _Finish ( );

    if (keyword == "msgctxt")
    {
      e.has_ctx = true;
      e.field   = &e.ctx;
    }

    else if (keyword == "msgid")
    {
      e.has_id = true;
      e.field  = &e.id;
    }

    else if (keyword == "msgid_plural")
    {
      e.has_plural = true;
      e.field      = &e.plural;
    }

    else if (keyword == "msgstr" || keyword.substr (0, 7) == "msgstr[")
    {
      size_t index = 0;
      if (keyword.size ( ) > 6)
        index = static_cast <size_t> (std::atoi (std::string (keyword.substr (7)).c_str ( )));

      if (index > 16)
      {
        syntax_error = (syntax_error) ? syntax_error : line_no;
        e.field = nullptr;
        continue;
      }

      if (e.str.size ( ) <= index)
        e.str.resize (index + 1);

      e.has_str = true;
      e.field   = &e.str [index];
    }

    else
    {
      syntax_error = (syntax_error) ? syntax_error : line_no;
      e.field = nullptr;
      continue;
    }

    if (! SKIF_L10n_ParseQuoted (line, space, *e.field))
      syntax_error = (syntax_error) ? syntax_error : line_no;
  }

  _Finish ( );

  if (syntax_error)
    PLOG_WARNING << "[L10n] " << cat.source << ": syntax error on line " << syntax_error << " (entries before it are used, the one it is in may be missing).";

  if (! has_plural_forms)
    cat.plural.Parse ("nplurals=2; plural=(n != 1);");

  // Plural entries with fewer forms than the language has are incomplete
  for (auto& key : plural_keys)
  {
    auto it = cat.entries.find (key);
    if (it != cat.entries.end ( ) && static_cast <int> (it->second.size ( )) < cat.plural.nplurals)
    {
      cat.entries.erase (it);
      cat.translated--;
    }
  }

  return language;
}

#pragma endregion

#pragma region Pseudo-locale

// Accented look-alikes (Latin-1 and Latin Extended-A, covered by Tahoma and Verdana); letters without one stay as is
static const char* SKIF_L10n_PseudoLower [26] = {
  "\xC3\xA1", "b",        "\xC3\xA7", "\xC4\x8F", "\xC3\xA9", "\xC6\x92", "\xC4\x9D", "\xC4\xA5", "\xC3\xAD", "\xC4\xB5", "\xC4\xB7", "\xC4\xBA", "m",
  "\xC3\xB1", "\xC3\xB3", "p",        "q",        "\xC5\x95", "\xC5\xA1", "\xC5\xA3", "\xC3\xBA", "v",        "\xC5\xB5", "x",        "\xC3\xBD", "\xC5\xBE"
};
static const char* SKIF_L10n_PseudoUpper [26] = {
  "\xC3\x81", "B",        "\xC3\x87", "\xC4\x8E", "\xC3\x89", "F",        "\xC4\x9C", "\xC4\xA4", "\xC3\x8D", "\xC4\xB4", "\xC4\xB6", "\xC4\xB9", "M",
  "\xC3\x91", "\xC3\x93", "P",        "Q",        "\xC5\x94", "\xC5\xA0", "\xC5\xA2", "\xC3\x9A", "V",        "\xC5\xB4", "X",        "\xC3\x9D", "\xC5\xBD"
};

// Accents letters, keeps placeholders ({0}), printf specifiers (%d), escapes and rich text markup ([link]) intact,
//   and makes the text ~35% longer: "Save {0} files" becomes the same text with accented letters, followed by " ~~~~",
//     between guillemets
static std::string
SKIF_L10n_PseudoLocalize (std::string_view in)
{
  std::string out;
  out.reserve (in.size ( ) * 2 + 16);
  out += "\xC2\xAB"; // Left-pointing guillemet

  size_t letters = 0;

  for (size_t i = 0; i < in.size ( ); i++)
  {
    char ch = in [i];

    if (ch == '{')
    {
      size_t close = in.find ('}', i);
      if (close != std::string_view::npos)
      {
        out += in.substr (i, close - i + 1);
        i = close;
        continue;
      }
    }

    if (ch == '%' && i + 1 < in.size ( ))
    {
      size_t j = i + 1;
      while (j < in.size ( ) && strchr ("-+ #0123456789.*hlLzjtI", in [j]) != nullptr)
        j++;
      out += in.substr (i, std::min (j + 1, in.size ( )) - i);
      i = j;
      continue;
    }

    if (ch >= 'a' && ch <= 'z') { out += SKIF_L10n_PseudoLower [ch - 'a']; letters++; }
    else if (ch >= 'A' && ch <= 'Z') { out += SKIF_L10n_PseudoUpper [ch - 'A']; letters++; }
    else
      out += ch;
  }

  // Nothing to translate (blank labels, numbers, symbols): as is
  if (letters == 0)
    return std::string (in);

  size_t padding = (letters * 35 + 99) / 100;
  if (padding > 0)
    out += ' ' + std::string (padding, '~');

  out += "\xC2\xBB"; // Right-pointing guillemet
  return out;
}

#pragma endregion

#pragma region Language detection

// The display languages of the user, in order of preference
static std::vector <std::string>
SKIF_L10n_GetWindowsLanguages (void)
{
  std::vector <std::string> languages;

  ULONG num = 0, len = 0;
  if (GetUserPreferredUILanguages (MUI_LANGUAGE_NAME, &num, nullptr, &len) && len > 0)
  {
    std::wstring buffer (len, L'\0');
    if (GetUserPreferredUILanguages (MUI_LANGUAGE_NAME, &num, buffer.data ( ), &len))
    {
      for (const wchar_t* lang = buffer.c_str ( ); *lang != L'\0'; lang += wcslen (lang) + 1)
        languages.push_back (SKIF_L10n_ToUTF8 (lang));
    }
  }

  return languages;
}

static std::string
SKIF_L10n_GetNativeName (const std::string& tag)
{
  wchar_t wszName [LOCALE_NAME_MAX_LENGTH * 2] = { };

  if (GetLocaleInfoEx (SKIF_L10n_ToWide (tag).c_str ( ), LOCALE_SNATIVEDISPLAYNAME, wszName, static_cast <int> (std::size (wszName))) > 0)
  {
    CharUpperBuffW (wszName, 1); // e.g. "francais (France)" -> "Francais (France)"
    return SKIF_L10n_ToUTF8 (wszName);
  }

  return tag;
}

#pragma endregion

#pragma region SKIF_L10n

// The whole content of a file (in one read, not character by character); false if it could not be read
static bool
SKIF_L10n_ReadFile (const std::filesystem::path& path, std::string& out)
{
  std::ifstream stream (path, std::ios::binary | std::ios::ate);

  if (! stream)
    return false;

  const std::streamoff size = stream.tellg ( );

  if (size < 0)
    return false;

  out.resize (static_cast <size_t> (size));
  stream.seekg (0);

  return static_cast <bool> (stream.read (out.data ( ), static_cast <std::streamsize> (size))) || size == 0;
}

// Reads the entries of a catalog, the first time its language is used. Main thread only, before it becomes the active one.
static void
SKIF_L10n_LoadCatalog (SKIF_L10n::Catalog& cat)
{
  if (cat.loaded)
    return;

  cat.loaded = true;

  const auto start = std::chrono::steady_clock::now ( );

  std::string      file; // A loose file (an embedded one is read where it is mapped)
  std::string_view data;

  if (cat.resData != nullptr)
    data = std::string_view (cat.resData, cat.resSize);

  else
  {
    if (! SKIF_L10n_ReadFile (cat.path, file))
    {
      PLOG_WARNING << "[L10n] " << cat.source << ": could not be read again; the language is shown in English.";
      cat.translated = 0;
      return;
    }

    data = file;
  }

  // The counts from the scan at startup are replaced by the exact ones
  cat.total      = 0;
  cat.translated = 0;

  SKIF_L10n_ParsePO (data, cat);

  PLOG_INFO << "[L10n] Loaded " << cat.tag << " from " << cat.source << ": " << cat.translated << " of " << cat.total << " translated, "
            << cat.plural.nplurals << " plural forms (" << std::chrono::duration <double, std::milli> (std::chrono::steady_clock::now ( ) - start).count ( ) << " ms).";
}

SKIF_L10n::Catalog*
SKIF_L10n::FindCatalog (const std::string& tag) const
{
  std::string lower = SKIF_L10n_ToLower (tag);

  for (auto cat : _catalogs)
    if (SKIF_L10n_ToLower (cat->tag) == lower)
      return cat;

  return nullptr;
}

SKIF_L10n::Catalog*
SKIF_L10n::MakeCatalog (const std::string& tag)
{
  Catalog* cat = new Catalog;
  cat->tag     = tag;
  cat->kind    = (tag == TagPseudo) ? Catalog::Kind_Pseudo : Catalog::Kind_English;
  cat->source  = "built-in";
  cat->plural.Parse ("nplurals=2; plural=(n != 1);");
  _catalogs.push_back (cat);
  return cat;
}

void
SKIF_L10n::Initialize (const std::wstring& languages_folder, const std::string& preferred)
{
  if (! _catalogs.empty ( ))
    return;

  MakeCatalog (TagEnglish);
  MakeCatalog (TagPseudo);

  // Only the header and the counts of each catalog are read here; its entries when the language is first used (LoadCatalog)
  auto _AddCatalog = [&](std::string_view data, const std::string& source, const std::string& fallback_tag,
                         const char* resData, size_t resSize, const std::wstring& path)
  {
    Catalog* cat = new Catalog;
    cat->source  = source;
    cat->loaded  = false;
    cat->resData = resData;
    cat->resSize = resSize;
    cat->path    = path;

    std::string tag = SKIF_L10n_ScanPO (data, *cat);
    cat->tag = (tag.empty ( )) ? SKIF_L10n_NormalizeTag (fallback_tag) : tag;

    if (cat->tag.empty ( ) || SKIF_L10n_ToLower (cat->tag) == TagEnglish || cat->tag == TagPseudo)
    {
      PLOG_WARNING << "[L10n] " << source << ": ignored (no usable language tag: \"" << cat->tag << "\").";
      delete cat;
      return;
    }

    // Loose files are loaded last and take priority over the embedded catalog of the same language
    //   (nothing refers to a catalog yet, so the replaced one can be freed)
    for (auto& existing : _catalogs)
    {
      if (SKIF_L10n_ToLower (existing->tag) == SKIF_L10n_ToLower (cat->tag))
      {
        PLOG_INFO << "[L10n] " << source << " replaces " << existing->source << ".";
        delete existing;
        existing = cat;
        cat = nullptr;
        break;
      }
    }

    if (cat != nullptr)
      _catalogs.push_back (cat);
  };

  // Embedded catalogs (l10n.rc)
  struct EnumContext { decltype (_AddCatalog)* add; } ctx { &_AddCatalog };

  EnumResourceNamesW (nullptr, SKIF_L10N_RESOURCE_TYPE,
    [](HMODULE hModule, LPCWSTR lpType, LPWSTR lpName, LONG_PTR lParam) -> BOOL
    {
      auto*   context = reinterpret_cast <EnumContext*> (lParam);
      HRSRC   hRes    = FindResourceW (hModule, lpName, lpType);
      HGLOBAL hData   = (hRes != nullptr) ? LoadResource (hModule, hRes) : nullptr;
      void*   pData   = (hData != nullptr) ? LockResource (hData) : nullptr;

      if (pData != nullptr)
      {
        std::string name = (IS_INTRESOURCE (lpName)) ? std::to_string (reinterpret_cast <uintptr_t> (lpName))
                                                     : SKIF_L10n_ToLower (SKIF_L10n_ToUTF8 (lpName));
        const char* data = static_cast <const char*> (pData);
        const DWORD size = SizeofResource (hModule, hRes);

        (*context->add) (std::string_view (data, size), "embedded:" + name, name, data, size, std::wstring ( ));
      }

      return TRUE;
    }, reinterpret_cast <LONG_PTR> (&ctx));

  // Loose files (<Special K folder>\Languages\*.po)
  std::error_code ec;
  if (! languages_folder.empty ( ) && std::filesystem::is_directory (languages_folder, ec))
  {
    for (auto& file : std::filesystem::directory_iterator (languages_folder, ec))
    {
      if (! file.is_regular_file (ec) || SKIF_L10n_ToLower (file.path ( ).extension ( ).string ( )) != ".po")
        continue;

      std::string data;
      if (! SKIF_L10n_ReadFile (file.path ( ), data))
      {
        PLOG_WARNING << "[L10n] " << SKIF_L10n_ToUTF8 (file.path ( ).wstring ( )) << ": could not be read.";
        continue;
      }
      _AddCatalog (data, SKIF_L10n_ToUTF8 (file.path ( ).wstring ( )), SKIF_L10n_ToUTF8 (file.path ( ).stem ( ).wstring ( )),
                   nullptr, 0, file.path ( ).wstring ( ));
    }
  }

  for (auto cat : _catalogs)
    if (cat->kind == Catalog::Kind_Translation)
      PLOG_INFO << "[L10n] Found " << cat->tag << " in " << cat->source << ": " << cat->translated << " of " << cat->total << " translated.";

  _preferred = preferred;
  Catalog* active = FindCatalog (ResolveLanguage (preferred));
  SKIF_L10n_LoadCatalog (*active);
  _active.store (active);
  _generation++;

  PLOG_INFO << "[L10n] Language: " << active->tag << " (setting: " << ((preferred.empty ( )) ? std::string ("follow Windows") : preferred) << ").";
}

std::string
SKIF_L10n::ResolveLanguage (const std::string& preferred) const
{
  auto _Match = [&](const std::string& wanted) -> Catalog*
  {
    if (Catalog* exact = FindCatalog (wanted))
      return exact;

    std::string primary = SKIF_L10n_PrimaryLanguage (wanted);
    Catalog*    same_primary = nullptr;

    for (auto cat : _catalogs)
    {
      if (cat->kind != Catalog::Kind_Translation || SKIF_L10n_PrimaryLanguage (cat->tag) != primary)
        continue;

      // Chinese: Simplified and Traditional are different written languages
      if (primary == "zh" && SKIF_L10n_IsTraditionalChinese (cat->tag) == SKIF_L10n_IsTraditionalChinese (wanted))
        return cat;

      // The catalog of the language itself ("de") before regional ones ("de-CH")
      if (same_primary == nullptr || SKIF_L10n_ToLower (cat->tag) == primary)
        same_primary = cat;
    }

    return same_primary;
  };

  if (! preferred.empty ( ))
  {
    if (SKIF_L10n_ToLower (preferred) == TagEnglish || preferred == TagPseudo)
      return SKIF_L10n_ToLower (preferred);

    if (Catalog* cat = _Match (preferred))
      return cat->tag;
  }

  // Follow Windows: the first display language that is English or has a catalog
  for (auto& lang : SKIF_L10n_GetWindowsLanguages ( ))
  {
    if (SKIF_L10n_PrimaryLanguage (lang) == TagEnglish)
      break;

    if (Catalog* cat = _Match (lang))
      return cat->tag;
  }

  return TagEnglish;
}

void
SKIF_L10n::SetLanguage (const std::string& preferred)
{
  _pending = preferred;
  _hasPending.store (true);
}

bool
SKIF_L10n::ApplyPendingLanguage (void)
{
  if (! _hasPending.exchange (false) || _catalogs.empty ( ))
    return false;

  _preferred = _pending;

  Catalog* cat = FindCatalog (ResolveLanguage (_preferred));
  if (cat == nullptr || cat == _active.load ( ))
    return false;

  // Read its entries before it becomes active (nothing else uses a catalog that is not active)
  SKIF_L10n_LoadCatalog (*cat);

  _active.store (cat);
  _generation++;

  PLOG_INFO << "[L10n] Language changed to " << cat->tag << " (setting: " << ((_preferred.empty ( )) ? std::string ("follow Windows") : _preferred) << ").";
  return true;
}

bool
SKIF_L10n::IsTraditionalChinese (const std::string& tag)
{
  return SKIF_L10n_IsTraditionalChinese (tag);
}

std::string
SKIF_L10n::GetPreferred (void) const
{
  return _preferred;
}

std::string
SKIF_L10n::GetLanguage (void) const
{
  Catalog* cat = _active.load ( );
  return (cat != nullptr) ? cat->tag : TagEnglish;
}

std::vector <SKIF_L10n::Language>
SKIF_L10n::GetLanguages (bool include_pseudo) const
{
  std::vector <Language> languages;
  languages.push_back ({ TagEnglish, "English", 100, false });

  std::vector <Language> translations;
  for (auto cat : _catalogs)
    if (cat->kind == Catalog::Kind_Translation)
      translations.push_back ({ cat->tag, SKIF_L10n_GetNativeName (cat->tag), (cat->total > 0) ? (cat->translated * 100 / cat->total) : 0, false });

  std::sort (translations.begin ( ), translations.end ( ), [](const Language& a, const Language& b) { return a.tag < b.tag; });
  languages.insert (languages.end ( ), translations.begin ( ), translations.end ( ));

  if (include_pseudo)
    languages.push_back ({ TagPseudo, "Pseudo-locale (" + std::string (TagPseudo) + ")", 100, true });

  return languages;
}

// The key of an entry: msgid, or msgctxt '\x04' msgid. Uses a buffer per thread, so the result is valid until the next call.
static std::string_view
SKIF_L10n_MakeKey (const char* ctx, const char* msgid)
{
  if (ctx == nullptr)
    return std::string_view (msgid);

  thread_local std::string buffer;
  buffer.assign (ctx);
  buffer += '\x04';
  buffer += msgid;
  return buffer;
}

const char*
SKIF_L10n::Get (const char* ctx, const char* msgid)
{
  if (msgid == nullptr)
    return "";

  Catalog* cat = _active.load (std::memory_order_acquire);

  if (cat == nullptr || cat->kind == Catalog::Kind_English || *msgid == '\0')
    return msgid;

  std::string_view key = SKIF_L10n_MakeKey (ctx, msgid);

  if (cat->kind == Catalog::Kind_Pseudo)
  {
    std::lock_guard <std::mutex> lock (cat->lockPseudo);

    auto it = cat->pseudo.find (key);
    if (it == cat->pseudo.end ( ))
      it = cat->pseudo.emplace (std::string (key), SKIF_L10n_PseudoLocalize (msgid)).first;
    return it->second.c_str ( );
  }

  auto it = cat->entries.find (key);
  return (it != cat->entries.end ( )) ? it->second [0].c_str ( ) : msgid;
}

const char*
SKIF_L10n::GetPlural (const char* ctx, const char* singular, const char* plural, unsigned long long n)
{
  if (singular == nullptr || plural == nullptr)
    return "";

  Catalog* cat = _active.load (std::memory_order_acquire);

  if (cat == nullptr || cat->kind != Catalog::Kind_Translation)
    return Get (ctx, (n == 1) ? singular : plural); // English, or the pseudo-locale of the English form

  auto it = cat->entries.find (SKIF_L10n_MakeKey (ctx, singular));
  if (it == cat->entries.end ( ))
    return (n == 1) ? singular : plural;

  const auto& forms = it->second;
  size_t      index = static_cast <size_t> (cat->plural.Evaluate (n));
  return forms [std::min (index, forms.size ( ) - 1)].c_str ( );
}

const wchar_t*
SKIF_L10n::GetW (const char* ctx, const char* msgid)
{
  if (msgid == nullptr)
    return L"";

  Catalog* cat = _active.load (std::memory_order_acquire);

  // Before Initialize ( ): the English text, kept for the lifetime of the process as well
  if (cat == nullptr)
  {
    static std::mutex                         lockEarly;
    static SKIF_L10n_StringMap <std::wstring> early;

    std::string key (SKIF_L10n_MakeKey (ctx, msgid));

    std::lock_guard <std::mutex> lock (lockEarly);

    auto it = early.find (key);
    if (it == early.end ( ))
      it = early.emplace (key, SKIF_L10n_ToWide (msgid)).first;
    return it->second.c_str ( );
  }

  std::string key (SKIF_L10n_MakeKey (ctx, msgid));

  std::lock_guard <std::mutex> lock (cat->lockWide);

  auto it = cat->wide.find (key);
  if (it == cat->wide.end ( ))
    it = cat->wide.emplace (key, SKIF_L10n_ToWide (Get (ctx, msgid))).first;
  return it->second.c_str ( );
}

const char*
SKIF_L10n::GetLabel (const char* ctx, const char* icon, const char* label)
{
  if (label == nullptr)
    return "";

  Catalog* cat = _active.load (std::memory_order_acquire);
  if (cat == nullptr)
    return label;

  thread_local std::string key;
  key.assign ((ctx  != nullptr) ? ctx  : "");
  key += '\x04';
  key += (icon != nullptr) ? icon : "";
  key += '\x04';
  key += label;

  std::lock_guard <std::mutex> lock (cat->lockLabels);

  auto it = cat->labels.find (key);
  if (it != cat->labels.end ( ))
    return it->second.c_str ( );

  // "Text###id" -> "<translated>###id"; "Text##id" and "Text" -> "<translated>###Text##id" / "<translated>###Text"
  std::string_view view (label);
  std::string_view text = view;
  std::string      id;

  if (size_t pos = view.find ("###"); pos != std::string_view::npos)
  {
    text = view.substr (0, pos);
    id   = view.substr (pos);
  }

  else
  {
    if (size_t pos2 = view.find ("##"); pos2 != std::string_view::npos)
      text = view.substr (0, pos2);
    id = "###" + std::string (view);
  }

  std::string out = (icon != nullptr) ? (std::string (icon) + " ") : std::string ( );

  if (text.empty ( ))
    out += label; // Nothing to translate ("##id", "###id")

  else
  {
    std::string msgid (text);
    out += Get (ctx, msgid.c_str ( )); // Copied before msgid goes out of scope
    out += id;
  }

  return cat->labels.emplace (key, std::move (out)).first->second.c_str ( );
}

#pragma endregion

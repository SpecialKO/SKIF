#pragma once

// Localization (l10n) of the UI
//
//   The English text is the key ("source-string keys", as with gettext): untranslated text simply shows in English.
//   Translations are gettext .po files, read as is (no compilation step): embedded in the executable as SKIF_PO
//     resources (l10n.rc), and optionally loaded from <Special K folder>\Languages\*.po, which take priority over
//       the embedded ones (lets translators test their work without a build).
//
//   Text shown in the UI is wrapped in one of the macros below, which also marks it for the extractor
//     (tools\l10n_extract.ps1) that generates the template (resources\l10n\SKIF.pot):
//
//     _("text")                     Display text: Text (), TextWrapped (), tooltips, prompts.
//     C_("context", "text")         The same, with a context for when one English text needs different translations.
//     L_("label")                   Widget labels (Button, MenuItem, Selectable, CollapsingHeader, Begin, ...):
//                                     returns "<translated>###<id>", where <id> is the part after "###" in the label,
//                                       or the whole label, so the ImGui ID is the same in every language.
//                                       Calls that refer to the same ID (OpenPopup/BeginPopup) must both use L_ ().
//     LI_(ICON_FA_..., "label")     L_ () with an icon in front: the icon stays out of the translated text.
//     CL_("context", "label")       L_ () with a context.
//     W_("text")                    Wide display text for Win32 (MessageBox, tray menu, jump list, notifications).
//     CW_("context", "text")        W_ ( ) with a context.
//     N_("text")                    Marks text for translation without translating it (static arrays);
//                                     translate the entry where it is shown with T_ ().
//     T_(variable)                  Translates text that is not a literal, but was marked with N_ ( ) elsewhere
//                                     (not extracted itself).
//     NC_("context", "text")        N_ ( ) with a context; translate the entry with CT_("context", variable).
//     NL_("label")                  N_ ( ) for a widget label (as with L_, only the text before "##" is translated);
//                                     show the entry with TL_(variable).
//     P_("singular", "plural", n)   Plural forms, following the Plural-Forms header of the language.
//     F_("{0} of {1}", a, b)        Translates, then formats with std::vformat (translators can reorder {0}, {1}, ...).
//     FP_("{0} game", "{0} games", n, ...)
//                                   Plural forms + formatting; {0} is n, any further arguments are {1}, {2}, ...
//
//   ImGui functions that take a printf-style format (Text, TextWrapped, TextColored, SetTooltip, ...) must get the
//     translation as an argument, never as the format: ImGui::TextWrapped ("%s", _("100% free")). A translation
//     may contain any character, and "%s" is also the fastest path in ImGui.
//
//   Keep icons and other macros out of the text (the extractor can't expand them, and translators can't see them):
//     prefix icons go outside (LI_, or ICON_FA_X " " + text), others are formatted in with F_ ().
//   Keep sentences whole: use SKIF_ImGui_TextRich ( ) for clickable or highlighted words within a sentence.
//
//   All lookups are thread-safe, and the returned pointers remain valid for the lifetime of the process (catalogs are
//     loaded once and never freed). The language is switched between frames (ApplyPendingLanguage) on the main thread.

#include <string>
#include <vector>
#include <format>
#include <atomic>
#include <cstdint>
#include <cstring>

class SKIF_L10n
{
public:
  struct Language {
    std::string  tag;         // BCP 47 language tag, e.g. "de", "pt-BR", "zh-Hans"
    std::string  name;        // Native name of the language, e.g. "Deutsch"
    int          completion;  // Translated entries in percent
    bool         pseudo;      // Pseudo-locale used for testing
  };

  static constexpr const char* TagEnglish = "en";
  static constexpr const char* TagPseudo  = "qps-ploc";

  static SKIF_L10n& GetInstance (void)
  {
    static SKIF_L10n instance;
    return instance;
  }

  // Loads the available catalogs and applies the preferred language (UTF-8 tag; empty = follow Windows). Main thread, once at startup.
  void         Initialize           (const std::wstring& languages_folder, const std::string& preferred);

  // Requests a language (empty = follow Windows), applied by ApplyPendingLanguage ( ) at the start of the next frame
  void         SetLanguage          (const std::string& preferred);
  bool         ApplyPendingLanguage (void);                     // Main thread, between frames. True if the active language changed
  std::string  GetPreferred         (void) const;               // The setting as given (empty = follow Windows)
  std::string  GetLanguage          (void) const;               // The active language (TagEnglish if none)
  std::string  ResolveLanguage      (const std::string& preferred) const; // The language a setting results in
  uint32_t     GetGeneration        (void) const { return _generation.load ( ); } // Incremented on every change of the active language

  // Chinese written in Traditional characters (zh-Hant, zh-TW, zh-HK, zh-MO) rather than Simplified ones
  static bool  IsTraditionalChinese (const std::string& tag);

  // English first, then the translations sorted by tag; the pseudo-locale last (only if include_pseudo)
  std::vector <Language>
               GetLanguages         (bool include_pseudo = false) const;

  // Lookups (use the macros)
  const char*    Get                (const char* ctx, const char* msgid);
  const char*    GetPlural          (const char* ctx, const char* singular, const char* plural, unsigned long long n);
  const wchar_t* GetW               (const char* ctx, const char* msgid);
  const char*    GetLabel           (const char* ctx, const char* icon, const char* label);

  struct Catalog; // l10n.cpp

private:
  SKIF_L10n (void) = default;
  SKIF_L10n (SKIF_L10n const&) = delete;
  SKIF_L10n (SKIF_L10n&&)      = delete;
  SKIF_L10n& operator= (SKIF_L10n const&) = delete;
  SKIF_L10n& operator= (SKIF_L10n&&)      = delete;

  Catalog*     FindCatalog          (const std::string& tag) const;
  Catalog*     MakeCatalog          (const std::string& tag);  // English / pseudo-locale

  std::vector <Catalog*>  _catalogs;                            // All catalogs, never freed (including English and the pseudo-locale)
  std::atomic <Catalog*>  _active     = nullptr;
  std::atomic <uint32_t>  _generation = 0;
  std::string             _preferred;
  std::string             _pending;
  std::atomic <bool>      _hasPending = false;
};

// Macros (see the top of this file)

#define _(s)                SKIF_L10n::GetInstance ( ).Get       (nullptr, s)
#define C_(ctx, s)          SKIF_L10n::GetInstance ( ).Get       (ctx,     s)
#define L_(s)               SKIF_L10n::GetInstance ( ).GetLabel  (nullptr, nullptr, s)
#define LI_(icon, s)        SKIF_L10n::GetInstance ( ).GetLabel  (nullptr, icon,    s)
#define CL_(ctx, s)         SKIF_L10n::GetInstance ( ).GetLabel  (ctx,     nullptr, s)
#define W_(s)               SKIF_L10n::GetInstance ( ).GetW      (nullptr, s)
#define CW_(ctx, s)         SKIF_L10n::GetInstance ( ).GetW      (ctx,     s)
#define P_(s, p, n)         SKIF_L10n::GetInstance ( ).GetPlural (nullptr, s, p, static_cast <unsigned long long> (n))
#define N_(s)               (s)
#define T_(s)               SKIF_L10n::GetInstance ( ).Get       (nullptr, s)
#define NC_(ctx, s)         (s)
#define CT_(ctx, s)         SKIF_L10n::GetInstance ( ).Get       (ctx,     s)
#define NL_(s)              (s)
#define TL_(s)              SKIF_L10n::GetInstance ( ).GetLabel  (nullptr, nullptr, s)

// Formats a translated pattern; falls back to the English pattern if the translation does not format (and to the
//   pattern itself if neither does), so a bad translation never takes SKIF down
template <class... Args>
std::string
SKIF_L10n_Format (const char* pattern, const char* fallback, const Args&... args)
{
  try {
    return std::vformat (pattern,  std::make_format_args (args...));
  } catch (...) { }

  if (fallback != nullptr && fallback != pattern)
  {
    try {
      return std::vformat (fallback, std::make_format_args (args...));
    } catch (...) { }
  }

  return std::string (pattern);
}

// A printf format for ImGui widgets that take one (SliderInt, SliderFloat, DragFloat, ...), from a translated pattern with
//   {0} for the value, which becomes spec (e.g. "%d" or "%.1f"). Any other "%" in the translation is escaped, so a
//     translation never acts as a printf format of its own:
//
//   ImGui::SliderInt (L_("Brightness"), &value, 80, 400, SKIF_L10n_PrintfFormat (_("{0} nits"), "%d").c_str ( ));
inline std::string
SKIF_L10n_PrintfFormat (const char* translated, const char* spec)
{
  std::string out;
  bool        placed = false;

  for (const char* p = translated; p != nullptr && *p != '\0'; p++)
  {
    if (*p == '%')
      out += "%%";
    else if (! placed && strncmp (p, "{0}", 3) == 0)
    {
      out   += spec;
      placed = true;
      p     += 2;
    }
    else if ((p [0] == '{' && p [1] == '{') || (p [0] == '}' && p [1] == '}'))
      out += *p++;
    else
      out += *p;
  }

  return out;
}

template <class... Args>
std::string
F_ (const char* msgid, const Args&... args)
{
  return SKIF_L10n_Format (_(msgid), msgid, args...);
}

template <class... Args>
std::string
FP_ (const char* singular, const char* plural, unsigned long long n, const Args&... args)
{
  return SKIF_L10n_Format (P_(singular, plural, n), (n == 1) ? singular : plural, n, args...);
}

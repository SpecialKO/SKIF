#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// Environment variables, in the two places SKIF lets them be set:
//
//   LaunchConfig  Launch options of your own (Properties > Launch, lc_user.json "Env"). Set by SKIF when it starts the
//                   executable, so the game sees them from the start. Only applies to launches SKIF starts itself.
//                   lc_user.json: "Env": { "NAME": "value", "OTHER": null }   A null value removes the variable (also
//                     one inherited from the user's environment). An empty value is the same as null.
//
//   SpecialK      [Compatibility.General] EnvironmentVars of Special K (INI editor): "A=1|B=2", with || for a literal
//                   pipe. Set by Special K once it is loaded into the game, whichever way the game was started: meant for
//                     mods and overlays that load after it (e.g. ReShade), as it is usually too late for the game itself.
//                   Parsed the same way as SK_Win32_ParseEnvVarsFromStr ( ) of Special K: split at each single |, then
//                     at the first =, then the name and value are trimmed. Entries without an = or a name are ignored.
//                   Braces are not part of the format: {A=1} sets "{A" to "1}".

struct SKIF_EnvVar {
  std::string name;  // UTF-8
  std::string value; // UTF-8; LaunchConfig: empty = remove the variable

  bool operator== (const SKIF_EnvVar& o) const { return (name == o.name && value == o.value); }
  bool operator!= (const SKIF_EnvVar& o) const { return ! (*this == o); }
};

namespace SKIF_EnvVars
{
  using List = std::vector <SKIF_EnvVar>;

  enum class Target {
    LaunchConfig,
    SpecialK
  };

  // A problem with one variable of a list (for the editor, and Properties > Launch's validation)
  struct issue_s {
    enum class Level {
      None,
      Info,    // Works, but worth knowing (e.g. SKIF also sets the variable)
      Warning, // Saved, but likely not what was meant (Special K ignores or changes it)
      Error    // Can't be saved (LaunchConfig)
    };

    Level       level = Level::None;
    std::string text;
  };

  issue_s       GetIssue    (const List& vars, size_t index, Target target);
  bool          HasErrors   (const List& vars, Target target);

  // Case-insensitive, as names are on Windows
  bool          SameName    (std::string_view a, std::string_view b);

  // Special K's format. dropped receives the parts Special K ignores (no = or no name), as written
  List          ParseSK     (std::string_view input, std::vector <std::string>* dropped = nullptr);
  std::string   SerializeSK (const List& vars); // Variables without a name are left out
  bool          HasBraces   (std::string_view input); // Wrapped in { }, which Special K does not strip
  std::string   StripBraces (std::string_view input);

  // lc_user.json "Env". FromJson ( ) returns false if it is not an object (out is then empty); values that are not
  //   strings or null are converted to text (numbers, booleans) or skipped (arrays, objects)
  bool           FromJson   (const nlohmann::json& env, List& out);
  nlohmann::json ToJson     (const List& vars); // Variables without a name are left out; empty values are written as null

  // Pasted text: one NAME=value per line, or Special K's format on a single line (braces are stripped). Lines without an
  //   = are skipped, as are empty lines and lines starting with # or ;
  List          ParseText   (std::string_view text);
  std::string   ToText      (const List& vars); // One NAME=value per line

  // Adds the variables to a launch's environment (SKIF_Util_CreateProcess ( ): an empty value removes the variable),
  //   replacing a variable of the same name in any letter case
  void          ApplyTo     (const List& vars, std::map <std::wstring, std::wstring>& env);

  // SKIF's command line (launcher mode, e.g. jump list items): SKIF_Env=<base64url of the lc_user.json form>, which has
  //   no spaces, quotes or dots. ToCommandLine ( ) returns the whole argument, or nothing if there are no variables
  std::wstring  ToCommandLine   (const List& vars);
  bool          FromCommandLine (std::wstring_view encoded, List& out); // The part after SKIF_Env=

  // Part of the key telling identical launch options apart (Steam's duplicate detection): empty for no variables
  std::wstring  GetKey      (const List& vars);
}

// The editor of a list of variables (Properties > Launch, and the INI editor): a row per variable, Add and Paste.
//   Returns true if the list was changed. The width is that of the available space, at most max_width (0 = no limit)
bool SKIF_ImGui_EnvVarsEditor (const char* str_id, SKIF_EnvVars::List& vars, SKIF_EnvVars::Target target, float max_width = 0.0f);

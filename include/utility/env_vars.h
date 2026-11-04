#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// Configurable environment variables!
//
//   LaunchConfig  Launch options of your own (Properties > Launch, lc_user.json "Env"). Set by SKIF when it starts an
//                   executable so the game sees the variables from the start. Only applies to launches handled by SKIF.
// 
//                     lc_user.json: "Env": { "NAME": "value", "OTHER": null }
// 
//                   A null value removes a variable (including ones inherited from the user's environment).
//                   An empty value is the same as null.
//
//   SpecialK      Set by Special K once it has been loaded into the game. Meant for mods and overlays that load after
//                 it (e.g. ReShade), as by that point in time it is usually too late for the game to act upon them.
//                 Configure this parameter through SKIF's Ini Editor.
//
//                   [Compatibility.General]
//                   EnvironmentVars=A=1|B=2
//                     (use || for a literal pipe)
//
//                   Parsed the same way as SK_Win32_ParseEnvVarsFromStr ( ) of Special K:
//                     - split at each single |,
//                     - then at the first =,
//                     - then the name and value are trimmed.
//                     - Entries without an = or a name are ignored.
//                     - Note that braces are not part of the format so {A=1} sets "{A" to "1}".

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

  // Used to indicate an issue with one variable of a list
  struct issue_s {
    enum class Level {
      None,
      Info,    // Works, but worth knowing (e.g. duplicate assignment by SKIF and Special K)
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

  // Special K's format.
  //   The 'dropped' parameter receives the parts Special K ignores (no = or no name)
  List          ParseSK     (std::string_view input, std::vector <std::string>* dropped = nullptr);
  std::string   SerializeSK (const List& vars);       // Variables without a name are left out
  bool          HasBraces   (std::string_view input); // Wrapped in { }, which Special K does not strip
  std::string   StripBraces (std::string_view input);

  // lc_user.json "Env".
  //   FromJson ( ) returns false if the input is not an object.
  //   Values that are not strings or null are converted to text (numbers, booleans) or skipped (arrays, objects)
  bool                   FromJson (const nlohmann::json&         env, List& out);
  bool                   FromJson (const nlohmann::ordered_json& env, List& out); // Keeps the order of the file
  nlohmann::ordered_json ToJson   (const List& vars); // In the order of the list. Variables without a name are left out; empty values are written as null

  // Pasted text: one NAME=value per line, or Special K's format on a single line.
  // Lines without an = are skipped, as are empty lines and lines starting with # or ;
  List          ParseText   (std::string_view text);
  std::string   ToText      (const List& vars); // One NAME=value per line

  // Adds the variables to a launch's environment
  //   - case insensitive replacement of a variable
  //   - an empty value removes the variable
  void          ApplyTo     (const List& vars, std::map <std::wstring, std::wstring>& env);

  // SKIF's command line (launcher mode, e.g. jump list items):
  //   SKIF_Env=<base64url of the lc_user.json form>, which has no spaces, quotes or dots.
  //   ToCommandLine ( ) returns the whole argument, or nothing if there are no variables.
  std::wstring  ToCommandLine   (const List& vars);
  bool          FromCommandLine (std::wstring_view encoded, List& out); // The part after SKIF_Env=

  // Part of the key telling identical launch options apart (Steam's duplicate detection): empty for no variables
  std::wstring  GetKey      (const List& vars);
}

// The editor of a list of variables (Properties > Launch, and the INI editor): a row per variable, Add and Paste.
//   Returns true if the list was changed. The width is that of the available space, at most max_width (0 = no limit)
bool SKIF_ImGui_EnvVarsEditor (const char* str_id, SKIF_EnvVars::List& vars, SKIF_EnvVars::Target target, float max_width = 0.0f);

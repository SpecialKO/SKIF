// Environment variables of launch options (lc_user.json "Env") and of Special K (EnvironmentVars): see utility/env_vars.h

#include <utility/env_vars.h>

#include <windows.h>

#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <fonts/fa_621.h>

#include <algorithm>
#include <cctype>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

namespace SKIF_EnvVars
{
  // The whitespace Special K trims (SK_Win32_ParseEnvVarsFromStr ( ))
  static constexpr std::string_view whitespace = " \t\r\n";

  static std::string_view
  _Trim (std::string_view str)
  {
    const size_t first = str.find_first_not_of (whitespace);

    if (first == std::string_view::npos)
      return { };

    return str.substr (first, str.find_last_not_of (whitespace) - first + 1);
  }

  static bool
  _HasSurroundingSpace (std::string_view str)
  {
    return (! str.empty ( ) && (whitespace.find (str.front ( )) != std::string_view::npos ||
                                whitespace.find (str.back  ( )) != std::string_view::npos));
  }

  static bool
  _HasLineBreak (std::string_view str)
  {
    return (str.find_first_of ("\r\n") != std::string_view::npos);
  }

  static std::string
  _EscapePipes (std::string_view str)
  {
    std::string out;
    out.reserve (str.size ( ));

    for (const char c : str)
    {
      out.push_back (c);

      if (c == '|')
        out.push_back ('|');
    }

    return out;
  }

  bool
  SameName (std::string_view a, std::string_view b)
  {
    if (a == b)
      return true;

    // The same comparison as Windows uses for the names of environment variables
    const std::wstring wa = SK_UTF8ToWideChar (std::string (a)),
                       wb = SK_UTF8ToWideChar (std::string (b));

    return (CompareStringOrdinal (wa.c_str ( ), static_cast <int> (wa.size ( )),
                                  wb.c_str ( ), static_cast <int> (wb.size ( )), TRUE) == CSTR_EQUAL);
  }

  issue_s
  GetIssue (const List& vars, size_t index, Target target)
  {
    using Level = issue_s::Level;

    if (index >= vars.size ( ))
      return { };

    const SKIF_EnvVar& var = vars [index];

    // A blank row is not saved
    if (var.name.empty ( ) && var.value.empty ( ))
      return { };

    if (var.name.empty ( ))
      return (target == Target::SpecialK) ? issue_s { Level::Warning, _("Special K ignores variables without a name.") }
                                          : issue_s { Level::Error,   _("A name is required.") };

    if (var.name.find ('=') != std::string::npos)
      return { Level::Error, _("The name can't contain an equals sign (=).") };

    if (_HasLineBreak (var.name) || _HasLineBreak (var.value))
      return { Level::Error, _("Line breaks are not supported.") };

    for (size_t i = 0; i < index; i++)
    {
      if (SameName (vars [i].name, var.name))
        return (target == Target::SpecialK) ? issue_s { Level::Warning, F_("{0} is also set above. The last one is used.", var.name) }
                                            : issue_s { Level::Error,   F_("{0} is already set above.", var.name) };
    }

    if (target == Target::SpecialK)
    {
      if (_HasSurroundingSpace (var.name) || _HasSurroundingSpace (var.value))
        return { Level::Warning, _("Special K removes spaces at the start and end of the name and the value.") };
    }

    else
    {
      if (_HasSurroundingSpace (var.name))
        return { Level::Warning, _("The name starts or ends with a space.") };

      // Set by LaunchInstant ( ) for Steam games, before the variables of the launch option are applied
      static constexpr const char* managed [] = { "SteamAppId", "SteamGameId", "SteamNoOverlayUIDrawing" };

      for (const char* name : managed)
      {
        if (SameName (var.name, name))
          return { Level::Info, _("SKIF sets this itself for Steam games. This value is used instead.") };
      }
    }

    return { };
  }

  bool
  HasErrors (const List& vars, Target target)
  {
    for (size_t i = 0; i < vars.size ( ); i++)
    {
      if (GetIssue (vars, i, target).level == issue_s::Level::Error)
        return true;
    }

    return false;
  }

  List
  ParseSK (std::string_view input, std::vector <std::string>* dropped)
  {
    List        out;
    std::string entry;

    auto _Flush = [&](void)
    {
      const std::string_view view (entry);
      const size_t           sep = view.find ('=');

      if (sep != std::string_view::npos && ! _Trim (view.substr (0, sep)).empty ( ))
        out.push_back ({ std::string (_Trim (view.substr (0, sep))), std::string (_Trim (view.substr (sep + 1))) });

      // Entries without an = or a name are ignored by Special K (an empty or blank part is not worth reporting)
      else if (dropped != nullptr && ! _Trim (view).empty ( ))
        dropped->push_back (entry);

      entry.clear ( );
    };

    for (size_t i = 0; i < input.size ( ); ++i)
    {
      if (input [i] != '|')
        entry.push_back (input [i]);

      // Two pipes in a row is an escaped pipe character
      else if (i + 1 < input.size ( ) && input [i + 1] == '|')
      {
        entry.push_back ('|');
        ++i;
      }

      else
        _Flush ( );
    }

    _Flush ( );

    return out;
  }

  std::string
  SerializeSK (const List& vars)
  {
    std::string out;

    for (const auto& var : vars)
    {
      if (var.name.empty ( ))
        continue;

      if (! out.empty ( ))
        out.push_back ('|');

      out += _EscapePipes (var.name) + "=" + _EscapePipes (var.value);
    }

    return out;
  }

  bool
  HasBraces (std::string_view input)
  {
    const std::string_view trimmed = _Trim (input);

    return (trimmed.size ( ) >= 2 && trimmed.front ( ) == '{' && trimmed.back ( ) == '}');
  }

  std::string
  StripBraces (std::string_view input)
  {
    const std::string_view trimmed = _Trim (input);

    if (! HasBraces (trimmed))
      return std::string (input);

    return std::string (_Trim (trimmed.substr (1, trimmed.size ( ) - 2)));
  }

  bool
  FromJson (const nlohmann::json& env, List& out)
  {
    out.clear ( );

    if (! env.is_object ( ))
      return false;

    for (const auto& [name, value] : env.items ( ))
    {
      if (value.is_string ( ))
        out.push_back ({ name, value.get <std::string> ( ) });

      else if (value.is_null ( ))
        out.push_back ({ name, "" });

      else if (value.is_primitive ( ))
        out.push_back ({ name, value.dump ( ) });
    }

    return true;
  }

  nlohmann::json
  ToJson (const List& vars)
  {
    nlohmann::json env = nlohmann::json::object ( );

    for (const auto& var : vars)
    {
      if (var.name.empty ( ))
        continue;

      if (var.value.empty ( ))
        env [var.name] = nullptr;
      else
        env [var.name] = var.value;
    }

    return env;
  }

  List
  ParseText (std::string_view text)
  {
    // A single line: Special K's format (which also covers a single NAME=value)
    if (_Trim (text).find_first_of ("\r\n") == std::string_view::npos)
      return ParseSK (StripBraces (text));

    List out;

    while (! text.empty ( ))
    {
      const size_t     eol  = text.find ('\n');
      std::string_view line = _Trim (text.substr (0, eol));

      text.remove_prefix ((eol == std::string_view::npos) ? text.size ( ) : eol + 1);

      if (line.empty ( ) || line.front ( ) == '#' || line.front ( ) == ';')
        continue;

      const size_t sep = line.find ('=');

      if (sep == std::string_view::npos)
        continue;

      const std::string_view name  = _Trim (line.substr (0, sep));
      std::string_view       value = _Trim (line.substr (sep + 1));

      if (name.empty ( ))
        continue;

      // NAME="value" (as in .env files)
      if (value.size ( ) >= 2 && value.front ( ) == '"' && value.back ( ) == '"')
        value = value.substr (1, value.size ( ) - 2);

      out.push_back ({ std::string (name), std::string (value) });
    }

    return out;
  }

  std::string
  ToText (const List& vars)
  {
    std::string out;

    for (const auto& var : vars)
    {
      if (var.name.empty ( ))
        continue;

      out += var.name + "=" + var.value + "\r\n";
    }

    return out;
  }

  void
  ApplyTo (const List& vars, std::map <std::wstring, std::wstring>& env)
  {
    for (const auto& var : vars)
    {
      if (var.name.empty ( ))
        continue;

      for (auto it = env.begin ( ); it != env.end ( ); )
      {
        if (SameName (SK_WideCharToUTF8 (it->first), var.name))
          it = env.erase (it);
        else
          ++it;
      }

      env [SK_UTF8ToWideChar (var.name)] = SK_UTF8ToWideChar (var.value);
    }
  }

  std::wstring
  ToCommandLine (const List& vars)
  {
    const nlohmann::json env = ToJson (vars);

    if (env.empty ( ))
      return L"";

    return L"SKIF_Env=" + SK_UTF8ToWideChar (SKIF_Util_Base64UrlEncode (env.dump ( )));
  }

  bool
  FromCommandLine (std::wstring_view encoded, List& out)
  {
    out.clear ( );

    std::string decoded;

    if (! SKIF_Util_Base64UrlDecode (SK_WideCharToUTF8 (std::wstring (encoded)), decoded))
      return false;

    const nlohmann::json env = nlohmann::json::parse (decoded, nullptr, false);

    return (! env.is_discarded ( ) && FromJson (env, out));
  }

  std::wstring
  GetKey (const List& vars)
  {
    std::vector <std::string> parts;

    for (const auto& var : vars)
    {
      if (var.name.empty ( ))
        continue;

      std::string name = var.name;
      std::transform (name.begin ( ), name.end ( ), name.begin ( ), [](unsigned char c) { return static_cast <char> (std::tolower (c)); });

      parts.push_back (name + "=" + var.value);
    }

    if (parts.empty ( ))
      return L"";

    std::sort (parts.begin ( ), parts.end ( ));

    std::string key;

    for (const auto& part : parts)
      key += part + '\x1F';

    return SK_UTF8ToWideChar (key);
  }
}

// InputText on a std::string, which grows as needed (imgui_stdlib is not part of the project)
static int
_EnvVars_InputTextResize (ImGuiInputTextCallbackData* data)
{
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
  {
    std::string* str = static_cast <std::string*> (data->UserData);
    str->resize (data->BufTextLen);
    data->Buf = str->data ( );
  }

  return 0;
}

static bool
_EnvVars_InputString (const char* id, std::string& value, const char* hint)
{
  ImGui::SetNextItemWidth (-FLT_MIN);

  const bool changed =
    ImGui::InputTextWithHint (id, hint, value.data ( ), value.capacity ( ) + 1, ImGuiInputTextFlags_CallbackResize, _EnvVars_InputTextResize, &value);

  SKIF_ImGui_BlockDragMoveWhenItemActive ( );

  return changed;
}

bool
SKIF_ImGui_EnvVarsEditor (const char* str_id, SKIF_EnvVars::List& vars, SKIF_EnvVars::Target target, float max_width)
{
  using Level = SKIF_EnvVars::issue_s::Level;

  bool changed = false;

  ImGui::PushID (str_id);

  // The row whose name gets the keyboard focus (a new one, from Add)
  ImGuiStorage* storage  = ImGui::GetStateStorage ( );
  const ImGuiID focusKey = ImGui::GetID ("###EnvVarsFocus");
  const int     focusRow = storage->GetInt (focusKey, -1);

  if (focusRow != -1)
    storage->SetInt (focusKey, -1);

  float width = ImGui::GetContentRegionAvail ( ).x;

  if (max_width > 0.0f)
    width = std::min (width, max_width);

  int remove = -1;

  if (! vars.empty ( ) && ImGui::BeginTable ("###EnvVarsTable", 3, ImGuiTableFlags_SizingStretchProp, ImVec2 (width, 0.0f)))
  {
    ImGui::TableSetupColumn ("###EnvVarsName",   ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableSetupColumn ("###EnvVarsValue",  ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupColumn ("###EnvVarsRemove", ImGuiTableColumnFlags_WidthFixed,   ImGui::GetFrameHeight ( ));

    const char* valueHint = (target == SKIF_EnvVars::Target::LaunchConfig) ? _("(removes the variable)") : _("(empty)");

    for (int i = 0; i < static_cast <int> (vars.size ( )); i++)
    {
      SKIF_EnvVar& var = vars [i];

      ImGui::PushID (i);
      ImGui::TableNextRow ( );

      // Name, after an icon if something is wrong with the variable
      ImGui::TableSetColumnIndex (0);

      const SKIF_EnvVars::issue_s issue = SKIF_EnvVars::GetIssue (vars, i, target);

      if (issue.level != Level::None)
      {
        const ImVec4 color = (issue.level == Level::Error)   ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Failure)
                           : (issue.level == Level::Warning) ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning)
                                                             : ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info);

        ImGui::AlignTextToFramePadding ( );
        ImGui::TextColored     (color, "%s", (issue.level == Level::Info) ? ICON_FA_CIRCLE_EXCLAMATION : ICON_FA_TRIANGLE_EXCLAMATION);
        SKIF_ImGui_SetHoverTip (issue.text);
        ImGui::SameLine        ( );
      }

      if (i == focusRow)
        ImGui::SetKeyboardFocusHere ( );

      changed |= _EnvVars_InputString ("###EnvVarName", var.name, _("Name"));

      // Value
      ImGui::TableSetColumnIndex (1);
      changed |= _EnvVars_InputString ("###EnvVarValue", var.value, valueHint);

      // Remove
      ImGui::TableSetColumnIndex (2);
      if (ImGui::Button (ICON_FA_XMARK "###EnvVarRemove", ImVec2 (ImGui::GetFrameHeight ( ), 0.0f)))
        remove = i;
      SKIF_ImGui_SetHoverTip (_("Removes the variable."));

      ImGui::PopID ( );
    }

    ImGui::EndTable ( );
  }

  if (remove != -1)
  {
    vars.erase (vars.begin ( ) + remove);
    changed = true;
  }

  if (ImGui::Button (L_("Add variable###EnvVarsAdd")))
  {
    vars.push_back ({ });
    storage->SetInt (focusKey, static_cast <int> (vars.size ( )) - 1);
    changed = true;
  }

  ImGui::SameLine ( );

  if (ImGui::Button (L_("Paste###EnvVarsPaste")))
  {
    const char* clipboard = ImGui::GetClipboardText ( );

    if (clipboard != nullptr)
    {
      // A variable that is already listed gets the new value
      for (auto& pasted : SKIF_EnvVars::ParseText (clipboard))
      {
        auto it = std::find_if (vars.begin ( ), vars.end ( ), [&](const SKIF_EnvVar& var) { return SKIF_EnvVars::SameName (var.name, pasted.name); });

        if (it != vars.end ( ))
          it->value = pasted.value;
        else
          vars.push_back (pasted);

        changed = true;
      }
    }
  }

  SKIF_ImGui_SetHoverTip (_("Adds the variables in the clipboard: one NAME=value per line,\n"
                            "or Special K's format on a single line (A=1|B=2).\n"
                            "Variables that are already listed get the new value."));

  ImGui::SameLine ( );

  const bool hasVars = std::any_of (vars.begin ( ), vars.end ( ), [](const SKIF_EnvVar& var) { return ! var.name.empty ( ); });

  SKIF_ImGui_PushDisableState (! hasVars);
  if (ImGui::Button (L_("Copy###EnvVarsCopy")) && hasVars)
    ImGui::SetClipboardText (SKIF_EnvVars::ToText (vars).c_str ( ));
  SKIF_ImGui_PopDisableState  (! hasVars);
  SKIF_ImGui_SetHoverTip (_("Copies the variables to the clipboard, one NAME=value per line."), false, true);

  ImGui::PopID ( );

  return changed;
}

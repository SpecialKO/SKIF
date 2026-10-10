#include <utility/ini_reader.h>
#include <utility/fsutil.h>

#include <imgui/imgui.h>

#include <stdexcept>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stringapiset.h>

std::vector <__INI> g_vIniReaderOSD;

enum class Encoding
{
  Utf8,
  Utf16LE,
  Utf16BE,
  Utf32LE,
  Utf32BE
};

static Encoding
DetectEncoding (const std::vector<std::uint8_t>& data, size_t& bomSize)
{
  bomSize = 0;

  // UTF-8 BOM: EF BB BF
  if (data.size() >= 3 &&
      data[0] == 0xEF &&
      data[1] == 0xBB &&
      data[2] == 0xBF)
  {
    bomSize = 3;
    return Encoding::Utf8;
  }

  // UTF-32 LE BOM: FF FE 00 00
  if (data.size() >= 4 &&
      data[0] == 0xFF &&
      data[1] == 0xFE &&
      data[2] == 0x00 &&
      data[3] == 0x00)
  {
    bomSize = 4;
    return Encoding::Utf32LE;
  }

  // UTF-32 BE BOM: 00 00 FE FF
  if (data.size() >= 4 &&
      data[0] == 0x00 &&
      data[1] == 0x00 &&
      data[2] == 0xFE &&
      data[3] == 0xFF)
  {
    bomSize = 4;
    return Encoding::Utf32BE;
  }

  // UTF-16 LE BOM: FF FE
  if (data.size() >= 2 &&
      data[0] == 0xFF &&
      data[1] == 0xFE)
  {
    bomSize = 2;
    return Encoding::Utf16LE;
  }

  // UTF-16 BE BOM: FE FF
  if (data.size() >= 2 &&
      data[0] == 0xFE &&
      data[1] == 0xFF)
  {
    bomSize = 2;
    return Encoding::Utf16BE;
  }

  // No BOM: assume UTF-8.
  return Encoding::Utf8;
}

static std::wstring
DecodeUtf8 (const std::uint8_t* data, const size_t size)
{
  if (size == 0)
    return {};

  if (size > static_cast<size_t>(INT_MAX))
    throw std::runtime_error("UTF-8 data is too large");

  const int inputSize = static_cast<int>(size);

  const int outputSize = MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    reinterpret_cast<const char*>(data),
    inputSize,
    nullptr,
    0);

  if (outputSize <= 0)
    throw std::runtime_error("Invalid UTF-8");

  std::wstring result(outputSize, L'\0');

  const int converted = MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    reinterpret_cast<const char*>(data),
    inputSize,
    result.data(),
    outputSize);

  if (converted != outputSize)
    throw std::runtime_error("UTF-8 conversion failed");

  return result;
}

static std::wstring
DecodeUtf16 (const std::uint8_t* data, const size_t size, const Encoding encoding)
{
  if (size % 2 != 0)
    throw std::runtime_error("Invalid UTF-16 byte count");

  const size_t count = size / 2;

  std::wstring result;
  result.reserve(count);

  auto read16 = [&](size_t i) -> std::uint16_t
    {
      const std::uint8_t a = data[i * 2];
      const std::uint8_t b = data[i * 2 + 1];

      return (encoding == Encoding::Utf16LE)
        ? static_cast<std::uint16_t>( a | (b << 8))
        : static_cast<std::uint16_t>((a << 8) | b );
    };

  for (size_t i = 0; i < count; ++i)
  {
    const std::uint16_t ch = read16(i);

    // High surrogate.
    if (ch >= 0xD800 && ch <= 0xDBFF)
    {
      if (i + 1 >= count)
        throw std::runtime_error(
          "Invalid UTF-16 surrogate pair");

      const std::uint16_t low = read16(i + 1);

      if (low < 0xDC00 || low > 0xDFFF)
        throw std::runtime_error(
          "Invalid UTF-16 surrogate pair");

      result.push_back(static_cast<wchar_t>(ch));
      result.push_back(static_cast<wchar_t>(low));

      ++i;
    }
    // Unpaired low surrogate.
    else if (ch >= 0xDC00 && ch <= 0xDFFF)
    {
      throw std::runtime_error(
        "Invalid UTF-16 surrogate");
    }
    else
    {
      result.push_back(static_cast<wchar_t>(ch));
    }
  }

  return result;
}

static std::wstring
DecodeUtf32 (const std::uint8_t* data, const size_t size, const Encoding encoding)
{
  if (size % 4 != 0)
    throw std::runtime_error("Invalid UTF-32 byte count");

  const size_t count = size / 4;

  std::wstring result;
  result.reserve(count);

  auto read32 = [&](size_t i) -> std::uint32_t
  {
    const std::uint8_t* p = data + i * 4;

    if (encoding == Encoding::Utf32LE)
    {
      return
           static_cast<std::uint32_t>(p[0])        |
          (static_cast<std::uint32_t>(p[1]) << 8 ) |
          (static_cast<std::uint32_t>(p[2]) << 16) |
          (static_cast<std::uint32_t>(p[3]) << 24);
    }

    return
          (static_cast<std::uint32_t>(p[0]) << 24) |
          (static_cast<std::uint32_t>(p[1]) << 16) |
          (static_cast<std::uint32_t>(p[2]) << 8 ) |
           static_cast<std::uint32_t>(p[3]);
  };

  for (size_t i = 0; i < count; ++i)
  {
    const std::uint32_t cp = read32(i);

    // Invalid Unicode scalar values.
    if (cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF))
    {
      throw std::runtime_error(
        "Invalid UTF-32 code point");
    }

    // BMP character.
    if (cp <= 0xFFFF)
    {
      result.push_back(static_cast<wchar_t>(cp));
    }
    else
    {
      // Convert Unicode scalar value to UTF-16 surrogate pair.
      const std::uint32_t value = cp - 0x10000;

      const wchar_t high = static_cast<wchar_t>(
        0xD800 + (value >> 10));

      const wchar_t low = static_cast<wchar_t>(
        0xDC00 + (value & 0x3FF));

      result.push_back(high);
      result.push_back(low);
    }
  }

  return result;
}

static std::vector<std::uint8_t>
ReadAllBytes (const std::wstring& path)
{
  FILE* file = _wfopen(path.c_str(), L"rb");

  if (!file)
    throw std::runtime_error("_wfopen failed");

  if (_fseeki64(file, 0, SEEK_END) != 0)
  {
    fclose(file);
    throw std::runtime_error("_fseeki64 failed");
  }

  const __int64 size = _ftelli64(file);

  if (size < 0)
  {
    fclose(file);
    throw std::runtime_error("_ftelli64 failed");
  }

  if (_fseeki64(file, 0, SEEK_SET) != 0)
  {
    fclose(file);
    throw std::runtime_error("_fseeki64 failed");
  }

  std::vector<std::uint8_t> data(
    static_cast<size_t>(size));

  size_t offset = 0;

  while (offset < data.size())
  {
    const size_t remaining = data.size() - offset;

    // fread's size parameter is size_t, but use reasonably
    // sized chunks to avoid implementation-specific issues.
    const size_t chunk =
      (remaining > 1024 * 1024)
      ? 1024 * 1024
      : remaining;

    const size_t n = fread(
      data.data() + offset,
      1,
      chunk,
      file);

    if (n != chunk)
    {
      fclose(file);
      throw std::runtime_error("fread failed");
    }

    offset += n;
  }

  fclose(file);
  return data;
}

static std::wstring
ReadTextFile (const std::wstring& path)
{
  const std::vector<std::uint8_t> data = ReadAllBytes (path);

  size_t bomSize = 0;
  const Encoding encoding = DetectEncoding (data, bomSize);

  const std::uint8_t* content =
    data.data() + bomSize;

  const size_t contentSize =
    data.size() - bomSize;

  switch (encoding)
  {
  case Encoding::Utf8:
    return DecodeUtf8  (content, contentSize);

  case Encoding::Utf16LE:
  case Encoding::Utf16BE:
    return DecodeUtf16 (content, contentSize, encoding);

  case Encoding::Utf32LE:
  case Encoding::Utf32BE:
    return DecodeUtf32 (content, contentSize, encoding);
  }

  throw std::runtime_error("Unknown encoding");
}

// CC BY-SA 4.0: https://stackoverflow.com/a/46711735
static constexpr uint32_t
SwitchHash (const std::string_view data) noexcept
{
  uint32_t hash = 5385;

  for (const auto& e : data)
    hash = ((hash << 5) + hash) + e;

  return hash;
}

static bool
DrawCheckbox (__INI* ptr)
{
  //ImGui::SetNextItemWidth(300.0f);
  if (ImGui::Checkbox (ptr->_label_v.c_str(), &ptr->value_b))
  {
    strncpy (ptr->value, ((ptr->value_b) ? "true" : "false"), MAX_PATH);
    ptr->_ignore = false;
  }

  return (ptr->value_b != ptr->default_b);
}

static bool
DrawInputBox (__INI* ptr)
{
  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::InputText (ptr->_label_v.c_str(), ptr->value, MAX_PATH))
    ptr->_ignore = false;

  return (strcmp (ptr->value, ptr->default) != 0);
}

static bool
DrawDropDown (__INI* ptr)
{
  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::BeginCombo  (ptr->_label_k.c_str(), ptr->value))
  {
    for (auto& item : ptr->_dditems)
    {
      bool is_selected = (item == ptr->value);
      if (ImGui::Selectable (item.c_str(), is_selected))
      {
        strncpy (ptr->value, item.c_str(), MAX_PATH);
        ptr->_ignore = false;
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus ( );
    }
    ImGui::EndCombo  ( );
  }

  return (strcmp (ptr->value, ptr->default) != 0);
}

static bool
DrawKeybinding (__INI* ptr)
{
  SHORT vKey = ptr->_keybind.saved.vKey;

  ImGui::PushID (ptr->section);
  ImGui::PushID (ptr->key);
  if (SK_ImGui_Keybinding (&ptr->_keybind))
  {
    // Only update the label if we are done assigning
    if (! ptr->_keybind.assigning)
    {
      strncpy (ptr->value, ptr->_keybind.getKeybind()->human_readable_utf8.c_str(), MAX_PATH);
      ptr->_ignore = false;
    }
  }
  ImGui::PopID ();
  ImGui::PopID ();

  // Return true when a new keybind has been assigned (so we can save it to the INI file)
  return (vKey != ptr->_keybind.saved.vKey);
}

void
__INI::Reset (void)
{
  value_b = default_b;
  strncpy (value, default, MAX_PATH);
}

__INI::__INI (ParameterType _t, std::string _s, std::string _k, std::string _v)
{
  // Build helpers

  auto _BuildBoolean = [&](void) -> void
  {
    default_b = (_v == "true");
    value_b   = default_b;
    strncpy (default, ((value_b) ? "true" : "false"), MAX_PATH);
    strncpy (value, default, MAX_PATH);
    DrawFunction = DrawCheckbox;
  };

  auto _BuildKeybind = [&](void) -> void
  {
    _keybind = {
      _k,
      SK_UTF8ToWideChar (_v)
    };
    _keybind.pending.human_readable = SK_UTF8ToWideChar (_v);
    _keybind.pending.parse();
    _keybind.applyChanges();

    DrawFunction = DrawKeybinding;
  };

  // Variable assignments

  _type = _t;
  strncpy (section, _s.c_str(), MAX_PATH);
  strncpy (key,     _k.c_str(), MAX_PATH);
  strncpy (default, _v.c_str(), MAX_PATH);
  strncpy (value,   default,    MAX_PATH);
  _label_k = ("###" + _s + "-" + _k);
  _label_v = ("###" + _s + "-" + _k + "-" + _v);

  // Do things!

  switch (_t)
  {
    case ParameterInt:
    case ParameterInt64:
    case ParameterFloat:
    case ParameterStringW:
    {
      DrawFunction = DrawInputBox;
      break;
    }

    case ParameterBool:
    {
      _BuildBoolean ( );
      break;
    }

    case ParameterKeybind:
    {
      _BuildKeybind ( );
      break;
    }

    // ParameterUnknown
    default:
    {
      switch (SwitchHash (_k))
      {
        case SwitchHash ("NotifyCorner"):
        case SwitchHash ("PopupOrigin"):
        {
          _dditems.push_back ("DontCare");
          _dditems.push_back ("TopLeft");
          _dditems.push_back ("TopRight");
          _dditems.push_back ("BottomLeft");
          _dditems.push_back ("BottomRight");
          DrawFunction = DrawDropDown;
          break;
        }

        case SwitchHash ("Scaling"):
        {
          _dditems.push_back ("DontCare");
          _dditems.push_back ("Unspecified");
          _dditems.push_back ("Centered");
          _dditems.push_back ("Stretched");
          DrawFunction = DrawDropDown;
          break;
        }

        case SwitchHash ("ScanlineOrder"):
        {
          _dditems.push_back ("DontCare");
          _dditems.push_back ("Unspecified");
          _dditems.push_back ("Progressive");
          _dditems.push_back ("LowerFieldFirst");
          _dditems.push_back ("UpperFieldFirst");
          DrawFunction = DrawDropDown;
          break;
        }

        case SwitchHash ("ExceptionMode"):
        {
          _dditems.push_back ("DontCare");
          _dditems.push_back ("Raise");
          _dditems.push_back ("Ignore");
          DrawFunction = DrawDropDown;
          break;
        }

        // Keybindings (Keyboard)

        // OSD [Game.HUD]
        case SwitchHash ("HUDToggle"):
        // OSD [OSD.System]
        case SwitchHash ("ConsoleToggle"):
        // OSD [Screenshot.System]
        case SwitchHash ("HUDFree"):
        case SwitchHash ("WithoutOSD"):
        case SwitchHash ("InsertOSD"):
        case SwitchHash ("Without3rdParty"):
        case SwitchHash ("ClipboardOnly"):
        case SwitchHash ("Snipping"):
        // OSD [Display.Monitor]
        case SwitchHash ("ToggleADHDMultiMonitor"):
        case SwitchHash ("MoveToPrimaryMonitor"):
        case SwitchHash ("MoveToNextMonitor"):
        case SwitchHash ("MoveToPrevMonitor"):
        case SwitchHash ("ToggleHDR"):
        // OSD [LatentSync.Control]
        case SwitchHash ("MoveTearlineDown"):
        case SwitchHash ("MoveTearlineUp"):
        case SwitchHash ("ManualResync"):
        case SwitchHash ("ToggleFCATBars"):
        // OSD [Sound.Mixing]
        case SwitchHash ("MuteGame"):
        case SwitchHash ("VolumePlus10%"):
        case SwitchHash ("VolumeMinus10%"):
        // OSD [Widgets.Global]
        case SwitchHash ("HideAllWidgets"):
        // OSD [ReShade.AddOn]
        case SwitchHash ("ToggleReShadeOverlay"):
        case SwitchHash ("InjectReShade"):
        // OSD [ImGui.Global]
        case SwitchHash ("ControlPanelToggle"):
        // OSD [HDR.Presets]
        case SwitchHash ("Activate0"):
        case SwitchHash ("Activate1"):
        case SwitchHash ("Activate2"):
        case SwitchHash ("Activate3"):
        // OSD [Widgets]
        case SwitchHash ("ToggleKey"):
        case SwitchHash ("FocusKey"):
        case SwitchHash ("FlashKey"):
        {
          _BuildKeybind ( );
          break;
        }

        // Keybindings (Gamepad)
        case SwitchHash ("LeftPaddle"):
        case SwitchHash ("LeftFunction"):
        case SwitchHash ("RightFunction"):
        case SwitchHash ("RightPaddle"):
        case SwitchHash ("TouchpadClick"):
        {
          DrawFunction = DrawInputBox;
          break;
        }

        default:
        {
          if (_v == "true" || _v == "false")
            _BuildBoolean ( );
          else
            DrawFunction = DrawInputBox;
        }
      }
    }
  }
}

std::vector <__INI>
SKIF_IniReader_ParseIni (const std::wstring& file_path, IniType ini_type)
{
  PLOG_VERBOSE << "Parsing INI file: " << file_path;

  inih::INIReader ini;

  try {
    ini.ParseContent (SK_WideCharToUTF8 (ReadTextFile (file_path)));
  } catch (const std::exception&) {
    PLOG_ERROR << "Failed to parse INI file!";
    MessageBoxW ((HWND)ImGui::GetWindowViewport()->PlatformHandleRaw, L"Failed to parse INI file!", L"Error", MB_OK | MB_ICONEXCLAMATION);
    return std::vector <__INI>();
  };

  std::vector <__INI> ini_parsed;
  std::vector<const ConfigEntry*> default_params = SKIF_IniReader_GetDefaultParams (ini_type);

  // Add all default parameters first...

  for (auto& default_item : default_params)
  {
    bool found = false;
    std::string value = "";

    for (auto& section : ini.Sections())
    {
      if (found)
        break;

      for (auto& kv : ini.Get (section))
      {
        if ((! _stricmp (default_item->section, section.c_str()) &&
            (! _stricmp (default_item->key,    kv.first.c_str()))))
        {
          value = kv.second;
          found = true;
          break;
        }
      }
    }

    //PLOG_VERBOSE << "INI item [DEFAULT]: [" << default_item->section << "] " << default_item->key << " = " << value;

    __INI item = { default_item->param_type, default_item->section, default_item->key, value };
    item.description = default_item->description;
    item._ignore = ! found;

    ini_parsed.push_back (item);
  }

  // Add any unrecognized parameters...

  for (auto& section : ini.Sections())
  {
    for (auto& kv : ini.Get (section))
    {
      bool found = false;

      for (auto& item : ini_parsed)
      {
        if ((! _stricmp (item.section, section.c_str()) &&
            (! _stricmp (item.key,    kv.first.c_str()))))
        {
          found = true;
          break;
        }
      }

      if (! found)
      {
        //PLOG_VERBOSE << "INI item [UNKNOWN]: [" << section << "] " << kv.first << " = " << kv.second;
        __INI item = { ParameterUnknown, section, kv.first, kv.second };
        item._ignore = false;
        ini_parsed.push_back(item);
      }
    }
  }

  // Sort it all!

  std::sort (ini_parsed.begin(), ini_parsed.end(), [](const __INI& a, const __INI& b)
    {
      return (_stricmp (a.section, b.section)  < 0  ||
             (_stricmp (a.section, b.section) == 0 &&
             (_stricmp (a.key,     b.key)      < 0)));
    }
  );

  return ini_parsed;
}

void
SKIF_IniReader_WriteIni (std::vector<__INI> ini, const std::wstring& file_path)
{
  inih::INIReader new_ini = { };

  for (auto& trie : ini)
  {
    if (! trie._ignore)
    {
      //PLOG_VERBOSE << "Writing INI item: [" << trie.section << "] " << trie.key << " = " << trie.value;
      try
      {
        new_ini.InsertEntry (trie.section, trie.key, trie.value);
      }
      catch (const std::exception& e)
      {
        // Probably caused by a duplicate key, so just log it and continue
        PLOG_ERROR << "Error writing INI item: [" << trie.section << "] " << trie.key << " = " << trie.value;
        PLOG_ERROR << "Error: " << e.what();
      }
    }
  }

  constexpr wchar_t* utf8_bom =  L"\xEF\xBB\xBF";

  std::wofstream out{ file_path };
  if (! out.is_open())
  {
    PLOG_FATAL << "cannot open output file: " << file_path;
    return;
  }

  out << utf8_bom;

  for (const auto& section : new_ini.Sections ( ))
  {
    out << L"[" << SK_UTF8ToWideChar (section) << L"]\n";

    for (const auto& key : new_ini.Keys (section))
      out << SK_UTF8ToWideChar (key) << L"=" << SK_UTF8ToWideChar (new_ini.Get (section, key)) << L"\n";

    out << L"\n";
  }

  out.close ( );
}

void
SKIF_IniReader_ReadOSDIni (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static std::wstring pathOSDIni = SK_FormatStringW (LR"(%ws\Global\osd.ini)", _path_cache.specialk_userdata);

  g_vIniReaderOSD = SKIF_IniReader_ParseIni (pathOSDIni, osd_ini);
}

void
SKIF_IniReader_SaveOSDIni (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static std::wstring pathOSDIni = SK_FormatStringW (LR"(%s\Global\osd.ini)", _path_cache.specialk_userdata);

  SKIF_IniReader_WriteIni (g_vIniReaderOSD, pathOSDIni);
}

#define Keybind ConfigEntry

std::vector <const ConfigEntry*>
SKIF_IniReader_GetDefaultParams (IniType ini_type)
{
  static const std::initializer_list <ConfigEntry> params_to_build
  //// nb: If you want any hope of reading this table, turn line wrapping off.
  //
  {
    ConfigEntry (ParameterFloat,    "How long to display version info at startup, 0=disable)",                                                                        osd_ini,       "SpecialK.VersionBanner",  "Duration"                            ),
    ConfigEntry (ParameterBool,     "OSD Visibility",                                                                                                                 osd_ini,       "SpecialK.OSD",            "Show"                                ),
    ConfigEntry (ParameterInt,      "OSD Color (Red)",                                                                                                                osd_ini,       "SpecialK.OSD",            "TextColorRed"                        ),
    ConfigEntry (ParameterInt,      "OSD Color (Green)",                                                                                                              osd_ini,       "SpecialK.OSD",            "TextColorGreen"                      ),
    ConfigEntry (ParameterInt,      "OSD Color (Blue)",                                                                                                               osd_ini,       "SpecialK.OSD",            "TextColorBlue"                       ),
    ConfigEntry (ParameterInt,      "OSD Position (X)",                                                                                                               osd_ini,       "SpecialK.OSD",            "PositionX"                           ),
    ConfigEntry (ParameterInt,      "OSD Position (Y)",                                                                                                               osd_ini,       "SpecialK.OSD",            "PositionY"                           ),
    ConfigEntry (ParameterFloat,    "OSD Scale",                                                                                                                      osd_ini,       "SpecialK.OSD",            "Scale"                               ),
    ConfigEntry (ParameterBool,     "Remember status monitoring state",                                                                                               osd_ini,       "SpecialK.OSD",            "RememberMonitoringState"             ),
    ConfigEntry (ParameterFloat,    "OSD's Luminance (cd.m^-2) in HDR games",                                                                                         osd_ini,       "SpecialK.OSD",            "HDRLuminance"                        ),
    ConfigEntry (ParameterBool,     "Show SLI Monitoring",                                                                                                            osd_ini,       "Monitor.SLI",             "Show"                                ),
    ConfigEntry (ParameterFloat,    "Make the uPlay Overlay visible in HDR mode!",                                                                                    osd_ini,       "uPlay.Overlay",           "Luminance_scRGB"                     ),
    ConfigEntry (ParameterFloat,    "Make the RTSS Overlay visible in HDR mode!",                                                                                     osd_ini,       "RTSS.Overlay",            "Luminance_scRGB"                     ),
    ConfigEntry (ParameterFloat,    "Make the ReShade Overlay visible in HDR mode!",                                                                                  osd_ini,       "ReShade.Overlay",         "Luminance_scRGB"                     ),
    ConfigEntry (ParameterFloat,    "Make the Galaxy Overlay visible in HDR mode!",                                                                                   osd_ini,       "Galaxy.Overlay",          "Luminance_scRGB"                     ),
    ConfigEntry (ParameterFloat,    "Make the Discord Overlay visible in HDR mode!",                                                                                  osd_ini,       "Discord.Overlay",         "Luminance_scRGB"                     ),
    ConfigEntry (ParameterBool,     "Allow Discord to composite a Win32 window over the game?",                                                                       osd_ini,       "Discord.Overlay",         "AllowWindowedMode"                   ),
    ConfigEntry (ParameterBool,     "Show Confirmation Dialog when Changing Display Modes",                                                                           osd_ini,       "Display.Settings",        "ConfirmChanges"                      ),
    ConfigEntry (ParameterBool,     "Remember Monitor Preferences for the Current Game",                                                                              dll_ini,       "Display.Monitor",         "RememberPreference"                  ),
    ConfigEntry (ParameterBool,     "Remember Monitor Resolution for the Current Game" ,                                                                              dll_ini,       "Display.Monitor",         "RememberResolution"                  ),
    ConfigEntry (ParameterBool,     "Apply Resolution Override for the Current Game",                                                                                 dll_ini,       "Display.Monitor",         "ResolutionForMonitor"                ),
    ConfigEntry (ParameterBool,     "Apply Refresh Override for the Current Game",                                                                                    dll_ini,       "Display.Monitor",         "RefreshRateForMonitor"               ),
    ConfigEntry (ParameterBool,     "Warn user if Multiplane Overlays support is missing",                                                                            dll_ini,       "Display.Monitor",         "WarnIfNoOverlayPlanes"               ),
    ConfigEntry (ParameterBool,     "Show IO Monitoring",                                                                                                             osd_ini,       "Monitor.IO",              "Show"                                ),
    ConfigEntry (ParameterFloat,    "IO Monitoring Interva",                                                                                                          osd_ini,       "Monitor.IO",              "Interval"                            ),
    ConfigEntry (ParameterBool,     "Show Disk Monitoring",                                                                                                           osd_ini,       "Monitor.Disk",            "Show"                                ),
    ConfigEntry (ParameterFloat,    "Disk Monitoring Interval",                                                                                                       osd_ini,       "Monitor.Disk",            "Interval"                            ),
    ConfigEntry (ParameterInt,      "Disk Monitoring Type (0 = Physical, 1 = Logical)",                                                                               osd_ini,       "Monitor.Disk",            "Type"                                ),
    ConfigEntry (ParameterBool,     "Show CPU Monitoring",                                                                                                            osd_ini,       "Monitor.CPU",             "Show"                                ),
    ConfigEntry (ParameterFloat,    "CPU Monitoring Interval (seconds)",                                                                                              osd_ini,       "Monitor.CPU",             "Interval"                            ),
    ConfigEntry (ParameterBool,     "Minimal CPU Info",                                                                                                               osd_ini,       "Monitor.CPU",             "Simple"                              ),
    ConfigEntry (ParameterBool,     "Show GPU Monitoring",                                                                                                            osd_ini,       "Monitor.GPU",             "Show"                                ),
    ConfigEntry (ParameterFloat,    "GPU Monitoring Interval (msecs)",                                                                                                osd_ini,       "Monitor.GPU",             "Interval"                            ),
    ConfigEntry (ParameterBool,     "Print GPU Slowdown Reason (NVIDA GPUs)",                                                                                         osd_ini,       "Monitor.GPU",             "PrintSlowdown"                       ),
    ConfigEntry (ParameterBool,     "Show Pagefile Monitoring",                                                                                                       osd_ini,       "Monitor.Pagefile",        "Show"                                ),
    ConfigEntry (ParameterFloat,    "Pagefile Monitoring Interval (seconds)",                                                                                         osd_ini,       "Monitor.Pagefile",        "Interval"                            ),
    ConfigEntry (ParameterBool,     "Show DLSS Resolution Information",                                                                                               osd_ini,       "Monitor.DLSS",            "Show"                                ),
    ConfigEntry (ParameterBool,     "Print DLSS Output Resolution",                                                                                                   osd_ini,       "Monitor.DLSS",            "ShowOutputResolution"                ),
    ConfigEntry (ParameterBool,     "Print DLSS Quality Level",                                                                                                       osd_ini,       "Monitor.DLSS",            "ShowQuality"                         ),
    ConfigEntry (ParameterBool,     "Print DLSS Preset",                                                                                                              osd_ini,       "Monitor.DLSS",            "ShowPreset"                          ),
    ConfigEntry (ParameterBool,     "Print DLSS Frame Generation Status",                                                                                             osd_ini,       "Monitor.DLSS",            "ShowFrameGeneration"                 ),
    ConfigEntry (ParameterBool,     "Show Memory Monitoring",                                                                                                         osd_ini,       "Monitor.Memory",          "Show"                                ),
    ConfigEntry (ParameterBool,     "Show Framerate Monitoring",                                                                                                      osd_ini,       "Monitor.FPS",             "Show"                                ),
    ConfigEntry (ParameterBool,     "Show Frametime in Framerate Counter",                                                                                            osd_ini,       "Monitor.FPS",             "DisplayFrametime"                    ),
    ConfigEntry (ParameterBool,     "Show Advanced Statistics in Framerate Counter",                                                                                  osd_ini,       "Monitor.FPS",             "AdvancedStatistics"                  ),
    ConfigEntry (ParameterBool,     "Show FRAPS-like ('120') Statistics in Framerate Counter",                                                                        osd_ini,       "Monitor.FPS",             "CompactStatistics"                   ),
    ConfigEntry (ParameterBool,     "Show VRR Status in Compact Mode",                                                                                                osd_ini,       "Monitor.FPS",             "CompactIncludesVRR"                  ),
    ConfigEntry (ParameterBool,     "Show Frame Number",                                                                                                              osd_ini,       "Monitor.FPS",             "DisplayFrameNumber"                  ),
    ConfigEntry (ParameterInt,      "How to measure frame intervals for the framepacing widget.",                                                                     osd_ini,       "Monitor.FPS",             "FrametimeMethod"                     ),
    ConfigEntry (ParameterBool,     "Show System Clock",                                                                                                              osd_ini,       "Monitor.Time",            "Show"                                ),
    ConfigEntry (ParameterBool,     "Show Special K Title",                                                                                                           osd_ini,       "Monitor.Title",           "Show"                                ),
    ConfigEntry (ParameterBool,     "Prefer Fahrenheit Units",                                                                                                        osd_ini,       "SpecialK.OSD",            "PreferFahrenheit"                    ),
    ConfigEntry (ParameterFloat,    "ImGui Scale",                                                                                                                    osd_ini,       "ImGui.Global",            "FontScale"                           ),
    ConfigEntry (ParameterBool,     "Display Playing Time in Config UI",                                                                                              osd_ini,       "ImGui.Global",            "ShowPlaytime"                        ),
    ConfigEntry (ParameterBool,     "Show G-Sync Status on Control Panel",                                                                                            osd_ini,       "ImGui.Global",            "ShowGSyncStatus"                     ),
    ConfigEntry (ParameterBool,     "Use Mac-style Menu Bar",                                                                                                         osd_ini,       "ImGui.Global",            "UseMacStyleMenu"                     ),
    ConfigEntry (ParameterBool,     "Show Input APIs currently in-use",                                                                                               osd_ini,       "ImGui.Global",            "ShowActiveInputAPIs"                 ),
    ConfigEntry (ParameterBool,     "Center the mouse cursor when opening SK's overlay",                                                                              osd_ini,       "ImGui.Global",            "CenterCursorOnOverlayToggle"         ),
    ConfigEntry (ParameterBool,     "Keyboard/Gamepad selection changes move the mouse cursor",                                                                       osd_ini,       "ImGui.Global",            "NavigationMovesMouseCursor"          ),
    ConfigEntry (ParameterBool,     "Keep a .PNG compressed copy of each screenshot?",                                                                                osd_ini,       "Screenshot.System",       "KeepLosslessPNG"                     ),
    ConfigEntry (ParameterBool,     "Play a Sound when triggering Screenshot Capture",                                                                                osd_ini,       "Screenshot.System",       "PlaySoundOnCapture"                  ),
    ConfigEntry (ParameterBool,     "Copy an LDR/HDR copy to the Windows Clipboard",                                                                                  osd_ini,       "Screenshot.System",       "CopyToClipboard"                     ),
    ConfigEntry (ParameterBool,     "Add Steam/Epic nickname as Author to Screenshot Metadata",                                                                       osd_ini,       "Screenshot.System",       "AuthorMetadata"                      ),
    ConfigEntry (ParameterStringW,  "Where to store screenshots (if non-empty)",                                                                                      osd_ini,       "Screenshot.System",       "OverridePath"                        ),
    ConfigEntry (ParameterStringW,  "wcsftime format,. Non-Standard Specifier: %G = <Game Name>",                                                                     osd_ini,       "Screenshot.System",       "FilenameFormat"                      ),
    ConfigEntry (ParameterInt,      "Compression Quality: 0=Worst, 100=Lossless",                                                                                     osd_ini,       "Screenshot.System",       "Quality"                             ),
    ConfigEntry (ParameterInt,      "Compression 'Quality' of JPEG: 0=Oh No!, 100=Still Crap...",                                                                     osd_ini,       "Screenshot.System",       "JPEGNotQuality"                      ),
    ConfigEntry (ParameterBool,     "Use less advanced encoding in JPEG XR and AVIF for compat.",                                                                     osd_ini,       "Screenshot.System",       "CompatibilityMode"                   ),
    ConfigEntry (ParameterBool,     "Use JPEG XL file format for HDR screenshots",                                                                                    osd_ini,       "Screenshot.System",       "UseJPEGXl"                           ),
    ConfigEntry (ParameterBool,     "Use AVIF file format for HDR screenshots",                                                                                       osd_ini,       "Screenshot.System",       "UseAVIF"                             ),
    ConfigEntry (ParameterInt,      "Chroma Subsampling (444, 422, 420, 400)",                                                                                        osd_ini,       "Screenshot.AVIF",         "SubsampleYUV"                        ),
    ConfigEntry (ParameterInt,      "Bits to use for scRGB to PQ encoded images",                                                                                     osd_ini,       "Screenshot.AVIF",         "scRGBtoPQBits"                       ),
    ConfigEntry (ParameterInt,      "Compression Speed: 0=Slowest (Smallest File), 10=Fastest",                                                                       osd_ini,       "Screenshot.AVIF",         "Speed"                               ),
    ConfigEntry (ParameterBool,     "Use HDR PNG file format for HDR screenshots",                                                                                    osd_ini,       "Screenshot.HDR",          "StorePNG"                            ),
    ConfigEntry (ParameterBool,     "Use HDR for Windows Clipboard screenshots",                                                                                      osd_ini,       "Screenshot.HDR",          "AllowClipboardHDR"                   ),
    ConfigEntry (ParameterInt,      "Use n-bit Quantization to save Disk Space",                                                                                      osd_ini,       "Screenshot.HDR",          "MaxST2084QuantizedBits"              ),
    ConfigEntry (ParameterInt,      "Use PNG or AVIF for HDR clipboard screenshots",                                                                                  osd_ini,       "Screenshot.HDR",          "HDRClipboardFormat"                  ),
        Keybind (ParameterKeybind,  "Toggle Game's HUD",                                                                                                              osd_ini,       "Game.HUD",                "HUDToggle"                           ),
        Keybind (ParameterKeybind,  "Toggle SK's Command Console",                                                                                                    osd_ini,       "OSD.System",              "ConsoleToggle"                       ),
        Keybind (ParameterKeybind,  "Take a screenshot without the HUD",                                                                                              osd_ini,       "Screenshot.System",       "HUDFree"                             ),
        Keybind (ParameterKeybind,  "Take a screenshot without SK's OSD",                                                                                             osd_ini,       "Screenshot.System",       "WithoutOSD"                          ),
        Keybind (ParameterKeybind,  "Take a screenshot and insert SK's OSD",                                                                                          osd_ini,       "Screenshot.System",       "InsertOSD"                           ),
        Keybind (ParameterKeybind,  "Take a screenshot before third-party overlays",                                                                                  osd_ini,       "Screenshot.System",       "Without3rdParty"                     ),
        Keybind (ParameterKeybind,  "Take a screenshot and copy it to the clipboard only",                                                                            osd_ini,       "Screenshot.System",       "ClipboardOnly"                       ),
        Keybind (ParameterKeybind,  "Snip a screenshot and copy it to the clipboard",                                                                                 osd_ini,       "Screenshot.System",       "Snipping"                            ),
        Keybind (ParameterKeybind,  "Toggle Multi-Monitor Focus Mode",                                                                                                osd_ini,       "Display.Monitor",         "ToggleADHDMultiMonitor"              ),
        Keybind (ParameterKeybind,  "Move Game to Primary Monitor",                                                                                                   osd_ini,       "Display.Monitor",         "MoveToPrimaryMonitor"                ),
        Keybind (ParameterKeybind,  "Move Game to Next Monitor",                                                                                                      osd_ini,       "Display.Monitor",         "MoveToNextMonitor"                   ),
        Keybind (ParameterKeybind,  "Move Game to Previous Monitor",                                                                                                  osd_ini,       "Display.Monitor",         "MoveToPrevMonitor"                   ),
        Keybind (ParameterKeybind,  "Toggle HDR on Selected Monitor",                                                                                                 osd_ini,       "Display.Monitor",         "ToggleHDR"                           ),
        Keybind (ParameterKeybind,  "Move Tear Location Down 1 Scanline",                                                                                             osd_ini,       "LatentSync.Control",      "MoveTearlineDown"                    ),
        Keybind (ParameterKeybind,  "Move Tear Location Up 1 Scanline",                                                                                               osd_ini,       "LatentSync.Control",      "MoveTearlineUp"                      ),
        Keybind (ParameterKeybind,  "Request a Monitor Timing Resync",                                                                                                osd_ini,       "LatentSync.Control",      "ManualResync"                        ),
        Keybind (ParameterKeybind,  "Toggle FCAT Tearing Visualizer",                                                                                                 osd_ini,       "LatentSync.Control",      "ToggleFCATBars"                      ),
        Keybind (ParameterKeybind,  "Toggle Mute for the Game",                                                                                                       osd_ini,       "Sound.Mixing",            "MuteGame"                            ),
        Keybind (ParameterKeybind,  "Increase Game Volume 10%",                                                                                                       osd_ini,       "Sound.Mixing",            "VolumePlus10%"                       ),
        Keybind (ParameterKeybind,  "Decrease Game Volume 10%",                                                                                                       osd_ini,       "Sound.Mixing",            "VolumeMinus10%"                      ),
        Keybind (ParameterKeybind,  "Temporarily hide all widgets",                                                                                                   osd_ini,       "Widgets.Global",          "HideAllWidgets"                      ),
        Keybind (ParameterKeybind,  "Toggle ReShade Overlay (Add-On version)",                                                                                        osd_ini,       "ReShade.AddOn",           "ToggleReShadeOverlay"                ),
        Keybind (ParameterKeybind,  "Inject ReShade (6.0+) as a Global PlugIn",                                                                                       osd_ini,       "ReShade.AddOn",           "InjectReShade"                       ),
        Keybind (ParameterKeybind,  "Toggle Special K's Control Panel",                                                                                               osd_ini,       "ImGui.Global",            "ControlPanelToggle"                  ),
    ConfigEntry (ParameterBool,     "If the game does not handle Alt+F4, offer a replacement",                                                                        dll_ini,       "Input.Keyboard",          "CatchAltF4"                          ),
    ConfigEntry (ParameterBool,     "Forcefully disable a game's Alt+F4 handler",                                                                                     dll_ini,       "Input.Keyboard",          "BypassAltF4Handler"                  ),
    ConfigEntry (ParameterBool,     "Completely stop all keyboard input from reaching the Game",                                                                      dll_ini,       "Input.Keyboard",          "DisabledToGame"                      ),
    ConfigEntry (ParameterBool,     "Block, Unblock or use Game Behavior for Alt-Tab key",                                                                            dll_ini,       "Input.Keyboard",          "EnableAltTab"                        ),
    ConfigEntry (ParameterBool,     "Block, Unblock or use Game Behavior for Windows key",                                                                            dll_ini,       "Input.Keyboard",          "EnableWinKey"                        ),
    ConfigEntry (ParameterInt,      "Minimum time, in milliseconds, between Alt-Tab usage",                                                                           dll_ini,       "Input.Keyboard",          "AltTabPacing"                        ),
    ConfigEntry (ParameterBool,     "Disable IME input services for the game",                                                                                        dll_ini,       "Input.Keyboard",          "DisableIME"                          ),
    ConfigEntry (ParameterBool,     "Prevent games from disabling legacy keyboard messages",                                                                          dll_ini,       "Input.Keyboard",          "PreventRawInputNoLegacy"             ),
    ConfigEntry (ParameterBool,     "Prevent games from disabling hotkeys",                                                                                           dll_ini,       "Input.Keyboard",          "PreventRawInputNoHotkeys"            ),
    ConfigEntry (ParameterBool,     "Enable ImGui control panel keybinding",                                                                                          dll_ini,       "Input.Keyboard",          "EnableImGuiToggle"                   ),
    ConfigEntry (ParameterBool,     "Completely stop all mouse input from reaching the Game",                                                                         dll_ini,       "Input.Mouse",             "DisabledToGame"                      ),
    ConfigEntry (ParameterBool,     "Prevent games from disabling legacy mouse messages",                                                                             dll_ini,       "Input.Mouse",             "PreventRawInputNoLegacy"             ),
    ConfigEntry (ParameterBool,     "Prevent games from blocking external window activation",                                                                         dll_ini,       "Input.Mouse",             "PreventRawInputCapture"              ),
    ConfigEntry (ParameterBool,     "Manage Cursor Visibility (due to inactivity)",                                                                                   dll_ini,       "Input.Cursor",            "Manage"                              ),
    ConfigEntry (ParameterBool,     "Keyboard Input Activates Cursor",                                                                                                dll_ini,       "Input.Cursor",            "KeyboardActivates"                   ),
    ConfigEntry (ParameterBool,     "Gamepad Input Deactivates Cursor",                                                                                               dll_ini,       "Input.Cursor",            "GamepadDeactivates"                  ),
    ConfigEntry (ParameterInt,      "Inactivity Timeout (in milliseconds)",                                                                                           dll_ini,       "Input.Cursor",            "Timeout"                             ),
    ConfigEntry (ParameterBool,     "Forcefully Capture Mouse Cursor in UI Mode",                                                                                     dll_ini,       "Input.Cursor",            "ForceCaptureInUI"                    ),
    ConfigEntry (ParameterBool,     "Use a Hardware Cursor for Special K's UI Features",                                                                              dll_ini,       "Input.Cursor",            "UseHardwareCursor"                   ),
    ConfigEntry (ParameterBool,     "Block Mouse Input if Hardware Cursor is Invisible",                                                                              dll_ini,       "Input.Cursor",            "BlockInvisibleCursorInput"           ),
    ConfigEntry (ParameterBool,     "Fix Synaptic Touchpad Scroll",                                                                                                   dll_ini,       "Input.Cursor",            "FixSynapticsTouchpadScroll"          ),
    ConfigEntry (ParameterBool,     "Disable ALL Gamepad Input (across all APIs)",                                                                                    dll_ini,       "Input.Gamepad",           "DisabledToGame"                      ),
    ConfigEntry (ParameterBool,     "Disable HID Input (prevent double-input if XInput is used)",                                                                     dll_ini,       "Input.Gamepad",           "DisableHID"                          ),
    ConfigEntry (ParameterBool,     "Disable WinMM Joystick Input",                                                                                                   dll_ini,       "Input.Gamepad",           "DisableWinMM"                        ),
    ConfigEntry (ParameterBool,     "Hide Windows.Gaming.Input Support from the game",                                                                                dll_ini,       "Input.Gamepad",           "HideWindowsGamingInput"              ),
    ConfigEntry (ParameterBool,     "Prevent game from seeing RawInput at all, useful in some  Unity Engine games that get duplicate input otherwise.",               dll_ini,       "Input.Gamepad",           "HideRawInput"                        ),
    ConfigEntry (ParameterBool,     "Give tactile feedback on gamepads when navigating the UI",                                                                       dll_ini,       "Input.Gamepad",           "AllowHapticUI"                       ),
    ConfigEntry (ParameterBool,     "Install hooks for Windows.Gaming.Input",                                                                                         dll_ini,       "Input.Gamepad",           "EnableWindowsGamingInput"            ),
    ConfigEntry (ParameterBool,     "Install hooks for RawInput and process WM_INPUT messages",                                                                       dll_ini,       "Input.Gamepad",           "EnableRawInput"                      ),
    ConfigEntry (ParameterBool,     "Install hooks for DirectInput 8",                                                                                                dll_ini,       "Input.Gamepad",           "EnableDirectInput8"                  ),
    ConfigEntry (ParameterBool,     "Install hooks for DirectInput 7",                                                                                                dll_ini,       "Input.Gamepad",           "EnableDirectInput7"                  ),
    ConfigEntry (ParameterBool,     "Install hooks for HID",                                                                                                          dll_ini,       "Input.Gamepad",           "EnableHID"                           ),
    ConfigEntry (ParameterBool,     "Install hooks for GameInput",                                                                                                    dll_ini,       "Input.Gamepad",           "HookGameInput"                       ),
    ConfigEntry (ParameterBool,     "Install hooks for joyGet* APIs",                                                                                                 dll_ini,       "Input.Gamepad",           "HookWinMM"                           ),
    ConfigEntry (ParameterBool,     "Use Steam-manipulated version of WinMM input",                                                                                   dll_ini,       "Input.Gamepad",           "AllowSteamWinMM"                     ),
    ConfigEntry (ParameterBool,     "Disable Rumble from ALL SOURCES (across all APIs)",                                                                              dll_ini,       "Input.Gamepad",           "DisableRumble"                       ),
    ConfigEntry (ParameterBool,     "Gamepad activity will block screensaver activation",                                                                             dll_ini,       "Input.Gamepad",           "BlocksScreenSaver"                   ),
    ConfigEntry (ParameterFloat,    "Scale the Right Impulse Triggers in GameInput games",                                                                            dll_ini,       "Input.Gamepad",           "RightImpulseStrength"                ),
    ConfigEntry (ParameterFloat,    "Scale the Left Impulse Triggers in GameInput games",                                                                             dll_ini,       "Input.Gamepad",           "LeftImpulseStrength"                 ),
    ConfigEntry (ParameterFloat,    "Apply a constant Right Trigger resistance on DualSense",                                                                         dll_ini,       "Input.Gamepad",           "RightTriggerResistance"              ),
    ConfigEntry (ParameterFloat,    "Apply a constant Left Trigger resistance on DualSense",                                                                          dll_ini,       "Input.Gamepad",           "LeftTriggerResistance"               ),
    ConfigEntry (ParameterFloat,    "Ratio of Right Trigger pull before resistance applies",                                                                          dll_ini,       "Input.Gamepad",           "RightTriggerResistsAt"               ),
    ConfigEntry (ParameterFloat,    "Ratio of Left Trigger pull before resistance applies",                                                                           dll_ini,       "Input.Gamepad",           "LeftTriggerResistsAt"                ),
    ConfigEntry (ParameterBool,     "Prevent Bluetooth Output (PlayStation DirectInput compat.)",                                                                     dll_ini,       "Input.Gamepad",           "BluetoothInputOnly"                  ),
    ConfigEntry (ParameterInt,      "Maximum allowed HID buffers,. 32=NS default, 8=SK default, this will lower latency at the expense of possibly missed inputs...", dll_ini,       "Input.Gamepad",           "MaxHIDPollingBuffers"                ),
    ConfigEntry (ParameterBool,     "Install hooks for XInput",                                                                                                       dll_ini,       "Input.XInput",            "Enable"                              ),
    ConfigEntry (ParameterBool,     "Re-install XInput hooks if hookchain is modified",                                                                               dll_ini,       "Input.XInput",            "Rehook"                              ),
    ConfigEntry (ParameterInt,      "XInput Controller that owns the config UI",                                                                                      dll_ini,       "Input.XInput",            "UISlot"                              ),
    ConfigEntry (ParameterInt,      "XInput Controller Slots to Fake Connectivity On",                                                                                dll_ini,       "Input.XInput",            "PlaceholderMask"                     ),
    ConfigEntry (ParameterStringW,  "Re-Assign XInput Slots",                                                                                                         dll_ini,       "Input.XInput",            "SlotReassignment"                    ),
    ConfigEntry (ParameterStringW,  "Disable Devices Connected to Specific XInput Slots",                                                                             dll_ini,       "Input.XInput",            "DisableSlots"                        ),
    ConfigEntry (ParameterBool,     "Hook vibration,. fix third-party created feedback loops",                                                                        dll_ini,       "Input.XInput",            "HookSetState"                        ),
    ConfigEntry (ParameterBool,     "Switch a game hard-coded to use Slot 0 to an active pad",                                                                        dll_ini,       "Input.XInput",            "AutoSlotAssign"                      ),
    ConfigEntry (ParameterBool,     "Prevent game from seeing XInput at all, useful if a game supports native SONY input and XInput.",                                dll_ini,       "Input.XInput",            "HideAllDevices"                      ),
    ConfigEntry (ParameterBool,     "For non-Xbox controllers, translate HID to XInput",                                                                              dll_ini,       "Input.XInput",            "EnableEmulation"                     ),
    ConfigEntry (ParameterFloat,    "In HID->XInput, filter analog values below this threshold",                                                                      dll_ini,       "Input.XInput",            "DeadzonePercent"                     ),
    ConfigEntry (ParameterBool,     "Invert the X-Axis on the Left Analog Stick",                                                                                     dll_ini,       "Input.XInput",            "InvertLX"                            ),
    ConfigEntry (ParameterBool,     "Invert the Y-Axis on the Left Analog Stick",                                                                                     dll_ini,       "Input.XInput",            "InvertLY"                            ),
    ConfigEntry (ParameterBool,     "Invert the X-Axis on the Right Analog Stick",                                                                                    dll_ini,       "Input.XInput",            "InvertRX"                            ),
    ConfigEntry (ParameterBool,     "Invert the Y-Axis on the Right Analog Stick",                                                                                    dll_ini,       "Input.XInput",            "InvertRY"                            ),
    ConfigEntry (ParameterBool,     "Swap Left and Right Analog Stick Input",                                                                                         dll_ini,       "Input.XInput",            "SwapSticks"                          ),
    ConfigEntry (ParameterBool,     "Swap A and B to conform to Nintendo button layout",                                                                              dll_ini,       "Input.XInput",            "SwapAB"                              ),
    ConfigEntry (ParameterBool,     "Swap X and Y to conform to Nintendo button layout",                                                                              dll_ini,       "Input.XInput",            "SwapXY"                              ),
    ConfigEntry (ParameterBool,     "Prevent game from seeing DirectInput gamepads",                                                                                  dll_ini,       "Input.DInput",            "HideGamepads"                        ),
    ConfigEntry (ParameterBool,     "Prevent game from seeing DirectInput mice",                                                                                      dll_ini,       "Input.DInput",            "HideMice"                            ),
    ConfigEntry (ParameterBool,     "Prevent game from seeing DirectInput keyboards",                                                                                 dll_ini,       "Input.DInput",            "HideKeyboards"                       ),
    ConfigEntry (ParameterBool,     "Prevent Steam Input from utterly destroying performance",                                                                        dll_ini,       "Input.DInput",            "PreventEnumDevices"                  ),
    ConfigEntry (ParameterBool,     "Install hooks for libScePad",                                                                                                    dll_ini,       "Input.libScePad",         "Enable"                              ),
    ConfigEntry (ParameterBool,     "Disable Touchpad Input",                                                                                                         dll_ini,       "Input.libScePad",         "DisableTouchpad"                     ),
    ConfigEntry (ParameterBool,     "Share Button can be used as Touchpad Click",                                                                                     input_ini,     "Input.libScePad",         "ShareClicksTouchpad"                 ),
    ConfigEntry (ParameterBool,     "Mute Button on DualSense will Mute the Game",                                                                                    input_ini,     "Input.libScePad",         "MuteButtonAppliesToGame"             ),
    ConfigEntry (ParameterBool,     "PlayStation / Home Button activates SK's control panel and may be used for special button combos (e.g. trigger sshot)",          input_ini,     "Input.libScePad",         "AdvancedPlayStationButton"           ),
    ConfigEntry (ParameterBool,     "Reduced power for Audio/Gyro/Touchpad on Bluetooth",                                                                             input_ini,     "Input.libScePad",         "EnableBluetoothPowerSaving"          ),
    ConfigEntry (ParameterInt,      "Force Red LED Color [0,255] or -1 for No Override",                                                                              input_ini,     "Input.libScePad",         "LEDColor_R"                          ),
    ConfigEntry (ParameterInt,      "Force Green LED Color [0,255] or -1 for No Override",                                                                            input_ini,     "Input.libScePad",         "LEDColor_G"                          ),
    ConfigEntry (ParameterInt,      "Force Blue LED Color [0,255] or -1 for No Override",                                                                             input_ini,     "Input.libScePad",         "LEDColor_B"                          ),
    ConfigEntry (ParameterInt,      "Force LED brightness [0,1,2,3] or -1 for No Override",                                                                           input_ini,     "Input.libScePad",         "LEDBrightness"                       ),
    ConfigEntry (ParameterBool,     "Allow SK to use all available features over Bluetooth",                                                                          input_ini,     "Input.libScePad",         "EnableFullBluetoothSupport"          ),
    ConfigEntry (ParameterInt,      "Cause games to see DualShock 4 v1 as DualShock 4 v2",                                                                            dll_ini,       "Input.libScePad",         "IdentifyDualShock4AsDualShock4v2"    ),
    ConfigEntry (ParameterInt,      "Cause games to see DualShock 4 v2 as DualShock 4",                                                                               dll_ini,       "Input.libScePad",         "IdentifyDualShock4v2AsDualShock4"    ),
    ConfigEntry (ParameterInt,      "Cause games to see DualSense Edge as DualSense",                                                                                 dll_ini,       "Input.libScePad",         "IdentifyDualSenseEdgeAsDualSense"    ),
    ConfigEntry (ParameterStringW,  "Keyboard Input to Generate when Left Function is Pressed",                                                                       dll_ini,       "Input.libScePad",         "LeftFunction"                        ),
    ConfigEntry (ParameterStringW,  "Keyboard Input to Generate when Right Function is Pressed",                                                                      dll_ini,       "Input.libScePad",         "RightFunction"                       ),
    ConfigEntry (ParameterStringW,  "Keyboard Input to Generate when Left Paddle is Pressed",                                                                         dll_ini,       "Input.libScePad",         "LeftPaddle"                          ),
    ConfigEntry (ParameterStringW,  "Keyboard Input to Generate when Right Paddle is Pressed",                                                                        dll_ini,       "Input.libScePad",         "RightPaddle"                         ),
    ConfigEntry (ParameterStringW,  "Keyboard Input to Generate when Touch Pad is Clicked",                                                                           dll_ini,       "Input.libScePad",         "TouchpadClick"                       ),
    ConfigEntry (ParameterFloat,    "Intensity of emulated rumble on DualSense controllers",                                                                          dll_ini,       "Input.libScePad",         "RumbleStrength"                      ),
    ConfigEntry (ParameterBool,     "Use Advanced Rumble on DualSense controllers (PS5 Compat)",                                                                      dll_ini,       "Input.libScePad",         "UseEmuRumbleImprovement"             ),
    ConfigEntry (ParameterBool,     "Install hooks for Steam Input",                                                                                                  dll_ini,       "Input.Steam",             "Enable"                              ),
    ConfigEntry (ParameterBool,     "Block Steam Input from interfering with other input APIs",                                                                       dll_ini,       "Input.Steam",             "BlockSteamInput"                     ),
    ConfigEntry (ParameterBool,     "Install hooks for HID (Human Interface Device)",                                                                                 dll_ini,       "Input.HID",               "Enable"                              ),
    ConfigEntry (ParameterInt,      "Limit the maximum number of simultaneous HID connections",                                                                       dll_ini,       "Input.HID",               "MaxAllowedBuffers"                   ),
    ConfigEntry (ParameterBool,     "Always show attach/detach notifications for HID devices",                                                                        dll_ini,       "Input.HID",               "AlwaysShowAttachNotif"               ),
    ConfigEntry (ParameterBool,     "Trace per-Thread Memory Allocation in Threads Widget",                                                                           dll_ini,       "Threads.Analyze",         "MemoryAllocation"                    ),
    ConfigEntry (ParameterBool,     "Trace per-Thread File I/O Activity in Threads Widget",                                                                           dll_ini,       "Threads.Analyze",         "FileActivity"                        ),
    ConfigEntry (ParameterBool,     "Borderless Window Mode",                                                                                                         dll_ini,       "Window.System",           "Borderless"                          ),
    ConfigEntry (ParameterBool,     "Center the Window",                                                                                                              dll_ini,       "Window.System",           "Center"                              ),
    ConfigEntry (ParameterBool,     "Render While Window is in Background",                                                                                           dll_ini,       "Window.System",           "RenderInBackground"                  ),
    ConfigEntry (ParameterBool,     "Mute While Window is in Background",                                                                                             dll_ini,       "Window.System",           "MuteInBackground"                    ),
    ConfigEntry (ParameterStringW,  "X Offset (Percent or Absolute)",                                                                                                 dll_ini,       "Window.System",           "XOffset"                             ),
    ConfigEntry (ParameterStringW,  "Y Offset (Percent or Absolute)",                                                                                                 dll_ini,       "Window.System",           "YOffset"                             ),
    ConfigEntry (ParameterBool,     "Confine the Mouse Cursor to the Game Window",                                                                                    dll_ini,       "Window.System",           "ConfineCursor"                       ),
    ConfigEntry (ParameterBool,     "Unconfine the Mouse Cursor from the Game Window",                                                                                dll_ini,       "Window.System",           "UnconfineCursor"                     ),
    ConfigEntry (ParameterBool,     "Prevent the Mouse Cursor from Unhiding the Taskbar",                                                                             dll_ini,       "Window.System",           "PreventTaskbarUnhide"                ),
    ConfigEntry (ParameterBool,     "Remember where the window is dragged to",                                                                                        dll_ini,       "Window.System",           "PersistentDragPos"                   ),
    ConfigEntry (ParameterBool,     "Make the Game Window Fill the Screen (scale to fit)",                                                                            dll_ini,       "Window.System",           "Fullscreen"                          ),
    ConfigEntry (ParameterStringW,  "Force the Client Region to this Size in Windowed Mode",                                                                          dll_ini,       "Window.System",           "OverrideRes"                         ),
    ConfigEntry (ParameterBool,     "Allow Resolution Overrides that Span Multiple Monitors",                                                                         dll_ini,       "Window.System",           "MultiMonitorMode"                    ),
    ConfigEntry (ParameterBool,     "Re-Compute Mouse Coordinates for Resized Windows",                                                                               dll_ini,       "Window.System",           "FixMouseCoords"                      ),
    ConfigEntry (ParameterInt,      "Prevent (0) or Force (1) a game's window Always-On-Top",                                                                         dll_ini,       "Window.System",           "AlwaysOnTop"                         ),
    ConfigEntry (ParameterBool,     "Prevent the Windows Screensaver from activating",                                                                                dll_ini,       "Window.System",           "DisableScreensaver"                  ),
    ConfigEntry (ParameterBool,     "Prevent the Windows Screensaver in (Borderless) Fullscreen",                                                                     dll_ini,       "Window.System",           "DisableFullscreenSaver"              ),
    ConfigEntry (ParameterBool,     "Allow Screensaver to work, by Disabling any Game Overrides",                                                                     dll_ini,       "Window.System",           "FullyManageScreenSaver"              ),
    ConfigEntry (ParameterInt,      "GDI Monitor ID of Preferred Monitor",                                                                                            dll_ini,       "Window.System",           "PreferredMonitor"                    ),
    ConfigEntry (ParameterStringW,  "CCD Display Path (invariant) of Preferred Monitor",                                                                              dll_ini,       "Window.System",           "PreferredMonitorExact"               ),
    ConfigEntry (ParameterBool,     "Disable WndProc / ClassProc hooks (wrap instead of hook)",                                                                       dll_ini,       "Window.System",           "DontHookWndProc"                     ),
    ConfigEntry (ParameterBool,     "Activate window after 15 frames (fixes games that think they are running in the background)",                                    dll_ini,       "Window.System",           "ActivateAtStart"                     ),
    ConfigEntry (ParameterBool,     "The game treats the foreground window (rather than focus), as the active application [for background render feature]",           dll_ini,       "Window.System",           "TreatForegroundAsActive"             ),
    ConfigEntry (ParameterBool,     "Automatically re-send key release notifications for keys that were released while the game was alt-tab'd",                       dll_ini,       "Window.System",           "FixStuckAltTabKeys"                  ),
    ConfigEntry (ParameterBool,     "Allow Special K to install a drag-n-drop handler for D3D11 texture mods and INI-related functionality.",                         dll_ini,       "Window.System",           "AllowDragNDrop"                      ),
    ConfigEntry (ParameterBool,     "Allow Special K to handle file drops for the game window.",                                                                      dll_ini,       "Window.System",           "AllowFileDrops"                      ),
    ConfigEntry (ParameterBool,     "Controls whether unresponsive apps use Window Ghosting.",                                                                        dll_ini,       "Window.System",           "AllowGhosting"                       ),
    ConfigEntry (ParameterBool,     "Disable All NVIDIA BloatWare (GeForce Experience)",                                                                              dll_ini,       "Compatibility.General",   "DisableBloatWare_NVIDIA"             ),
    ConfigEntry (ParameterBool,     "Rehook LoadLibrary When RTSS/Steam/ReShade hook it",                                                                             dll_ini,       "Compatibility.General",   "RehookLoadLibrary"                   ),
    ConfigEntry (ParameterBool,     "Disable Functionality Not Compatible With WINE",                                                                                 dll_ini,       "Compatibility.General",   "UsingWINE"                           ),
    ConfigEntry (ParameterBool,     "Disable Unnecessary DxDiagnostic BLOAT in Some Games",                                                                           dll_ini,       "Compatibility.General",   "AllowDxDiagn"                        ),
    ConfigEntry (ParameterBool,     "Opt-in for Automatic Large Address Aware Patch on Crash",                                                                        dll_ini,       "Compatibility.General",   "AutoLargeAddressPatch",              IniBitness_i8086 ),
    ConfigEntry (ParameterBool,     "Runs hook initialization on a separate thread,. high safety",                                                                    dll_ini,       "Compatibility.General",   "AsyncInit"                           ),
    ConfigEntry (ParameterBool,     "Initializes hooks in a way that ReShade will not interfere",                                                                     dll_ini,       "Compatibility.General",   "ReShadeMode"                         ),
    ConfigEntry (ParameterBool,     "Avoid hooks on CreateSwapChainForHwnd",                                                                                          dll_ini,       "Compatibility.General",   "FSR3Mode"                            ),
    ConfigEntry (ParameterInt,      "Debug Level (0=Most debug code OFF, >0=Normal behavior)",                                                                        dll_ini,       "Compatibility.General",   "DebugLevel"                          ),
    ConfigEntry (ParameterInt,      "Set Default (1) or Override (2) SDL input/window behavior.",                                                                     dll_ini,       "Compatibility.General",   "SDLSanityLevel"                      ),
    ConfigEntry (ParameterBool,     "SDL_JOYSTICK_WGI",                                                                                                               dll_ini,       "Compatibility.SDL",       "SDL_JOYSTICK_WGI"                    ),
    ConfigEntry (ParameterBool,     "SDL_JOYSTICK_RAWINPUT",                                                                                                          dll_ini,       "Compatibility.SDL",       "SDL_JOYSTICK_RAWINPUT"               ),
    ConfigEntry (ParameterBool,     "SDL_DIRECTINPUT_ENABLED",                                                                                                        dll_ini,       "Compatibility.SDL",       "SDL_DIRECTINPUT_ENABLED"             ),
    ConfigEntry (ParameterBool,     "SDL_XINPUT_ENABLED",                                                                                                             dll_ini,       "Compatibility.SDL",       "SDL_XINPUT_ENABLED"                  ),
    ConfigEntry (ParameterBool,     "SDL_JOYSTICK_HIDAPI",                                                                                                            dll_ini,       "Compatibility.SDL",       "SDL_JOYSTICK_HIDAPI"                 ),
    ConfigEntry (ParameterBool,     "SDL_JOYSTICK_HIDAPI_PS4_RUMBLE",                                                                                                 dll_ini,       "Compatibility.SDL",       "FullPlayStationBluetoothSupport"     ),
    ConfigEntry (ParameterBool,     "SDL_JOYSTICK_HIDAPI_JOYCON_HOME_LED",                                                                                            dll_ini,       "Compatibility.SDL",       "SDL_JOYSTICK_HIDAPI_JOYCON_HOME_LED" ),
    ConfigEntry (ParameterBool,     "SDL_JOYSTICK_THREAD",                                                                                                            dll_ini,       "Compatibility.SDL",       "SDL_JOYSTICK_THREAD"                 ),
    ConfigEntry (ParameterBool,     "SDL_POLL_SENTINEL",                                                                                                              dll_ini,       "Compatibility.SDL",       "SDL_POLL_SENTINEL"                   ),
    ConfigEntry (ParameterStringW,  "Last Known Render API",                                                                                                          dll_ini,       "API.Hook",                "LastKnown"                           ),
    ConfigEntry (ParameterBool,     "Enable DirectDraw Hooking",                                                                                                      dll_ini,       "API.Hook",                "ddraw",                              IniBitness_i8086 ),
    ConfigEntry (ParameterBool,     "Enable Direct3D 8 Hooking",                                                                                                      dll_ini,       "API.Hook",                "d3d8",                               IniBitness_i8086 ),
    ConfigEntry (ParameterBool,     "Enable Direct3D 9 Hooking",                                                                                                      dll_ini,       "API.Hook",                "d3d9"                                ),
    ConfigEntry (ParameterBool,     "Enable Direct3D 9Ex Hooking",                                                                                                    dll_ini,       "API.Hook",                "d3d9ex"                              ),
    ConfigEntry (ParameterInt,      "Enable Native DXVK (D3D9)",                                                                                                      dll_ini,       "API.Hook",                "dxvk9"                               ),
    ConfigEntry (ParameterBool,     "Enable Direct3D 11 Hooking",                                                                                                     dll_ini,       "API.Hook",                "d3d11"                               ),
    ConfigEntry (ParameterBool,     "Enable Direct3D 12 Hooking",                                                                                                     dll_ini,       "API.Hook",                "d3d12"                               ),
    ConfigEntry (ParameterBool,     "Enable Vulkan Hooking",                                                                                                          dll_ini,       "API.Hook",                "Vulkan",                             IniBitness_AMD64 ),
    ConfigEntry (ParameterBool,     "Enable OpenGL Hooking",                                                                                                          dll_ini,       "API.Hook",                "OpenGL"                              ),
  //ConfigEntry (ParameterBool,     "Enable OpenGL Debugging",                                                                                                        dll_ini,       "OpenGL.System",           "EnableDebug"                         ), // Duplicate key
    ConfigEntry (ParameterFloat,    "Memory Reserve Percentage",                                                                                                      dll_ini,       "Manage.Memory",           "ReservePercent"                      ),
    ConfigEntry (ParameterBool,     "Log Silence",                                                                                                                    dll_ini,       "SpecialK.System",         "Silent"                              ),
    ConfigEntry (ParameterBool,     "Trace DLL Loading (needed for dynamic API detection)",                                                                           dll_ini,       "SpecialK.System",         "TraceLoadLibrary"                    ),
    ConfigEntry (ParameterInt,      "Log Verbosity (0=General, 5=Insane Debug)",                                                                                      dll_ini,       "SpecialK.System",         "LogLevel"                            ),
    ConfigEntry (ParameterBool,     "Use Custom Crash Handler",                                                                                                       dll_ini,       "SpecialK.System",         "UseCrashHandler"                     ),
    ConfigEntry (ParameterBool,     "Disable Crash Sound",                                                                                                            dll_ini,       "SpecialK.System",         "NoCrashSound"                        ),
    ConfigEntry (ParameterBool,     "Try to Recover from Exceptions that Would Cause a Crash",                                                                        dll_ini,       "SpecialK.System",         "EnableCrashSuppression"              ),
    ConfigEntry (ParameterBool,     "Halt Special K Initialization Until Debugger is Attached",                                                                       dll_ini,       "SpecialK.System",         "WaitForDebugger"                     ),
    ConfigEntry (ParameterBool,     "Print Application's Debug Output in real-time",                                                                                  dll_ini,       "SpecialK.System",         "DebugOutput"                         ),
    ConfigEntry (ParameterBool,     "Log Application's Debug Output",                                                                                                 dll_ini,       "SpecialK.System",         "GameOutput"                          ),
    ConfigEntry (ParameterFloat,    "Delay Global Injection Initialization for x-many Seconds",                                                                       dll_ini,       "SpecialK.System",         "GlobalInjectDelay"                   ),
    ConfigEntry (ParameterBool,     "At Application Exit, make SKIF the new Foreground Window",                                                                       dll_ini,       "SpecialK.System",         "ReturnToSKIF"                        ),
    ConfigEntry (ParameterBool,     "Load ASI Plug-ins \u2013 Restart the Game After Changing",                                                                       dll_ini,       "SpecialK.System",         "AutoLoadASIGlobal"                   ),
    ConfigEntry (ParameterBool,     "Perform Clean Exit from Application",                                                                                            dll_ini,       "SpecialK.System",         "CleanExit"                           ),
    ConfigEntry (ParameterInt,      "Show Tearing & Latency Measurements",                                                                                            osd_ini,       "Render.Latency",          "ShowLatencyMonitor"                  ),
    ConfigEntry (ParameterFloat,    "Target Framerate (0.0 = Unlimited)",                                                                                             dll_ini,       "Render.FrameRate",        "TargetFPS"                           ),
    ConfigEntry (ParameterFloat,    "Target Framerate (Background)",                                                                                                  dll_ini,       "Render.FrameRate",        "TargetFPS_Background"                ),
    ConfigEntry (ParameterInt,      "Present Interval (0 = Unfreeze Clock)\u0022",                                                                                    dll_ini,       "Render.FrameRate",        "PresentInterval"                     ),
    ConfigEntry (ParameterInt,      "Sync Interval Clamping (minimum if vsync enabled)",                                                                              dll_ini,       "Render.FrameRate",        "SyncIntervalClamp"                   ),
    ConfigEntry (ParameterInt,      "Tearing Mode (Always On/Off or Adaptive)",                                                                                       dll_ini,       "Render.FrameRate",        "TearingMode"                         ),
    ConfigEntry (ParameterInt,      "Latency reduction behavior (Smooth or Aggressive)",                                                                              dll_ini,       "Render.FrameRate",        "LatencyMode"                         ),
    ConfigEntry (ParameterInt,      "Max Render Latency for LowLatency/Adaptive Tearing Mode",                                                                        dll_ini,       "Render.FrameRate",        "RenderQueue"                         ),
    ConfigEntry (ParameterInt,      "Maximum Frames to Render-Ahead",                                                                                                 dll_ini,       "Render.FrameRate",        "PreRenderLimit"                      ),
    ConfigEntry (ParameterBool,     "Sleep Free Render Thread",                                                                                                       dll_ini,       "Render.FrameRate",        "SleeplessRenderThread"               ),
    ConfigEntry (ParameterBool,     "Sleep Free Window Thread",                                                                                                       dll_ini,       "Render.FrameRate",        "SleeplessWindowThread"               ),
    ConfigEntry (ParameterBool,     "Enable Multimedia Class Scheduling for FPS Limiter Sleep",                                                                       dll_ini,       "Render.FrameRate",        "EnableMMCSS"                         ),
    ConfigEntry (ParameterFloat,    "Fullscreen Refresh Rate",                                                                                                        dll_ini,       "Render.FrameRate",        "RefreshRate"                         ),
    ConfigEntry (ParameterStringW,  "Fullscreen Rational Scan Rate (precise refresh rate)",                                                                           dll_ini,       "Render.FrameRate",        "RescanRatio"                         ),
    ConfigEntry (ParameterInt,      "Place Framerate Limiter Wait Before/After Present, etc.",                                                                        dll_ini,       "Render.FrameRate",        "LimitEnforcementPolicy"              ),
    ConfigEntry (ParameterBool,     "Use ETW tracing (PresentMon) for extra latency/flip info",                                                                       dll_ini,       "Render.FrameRate",        "EnableETWTracing"                    ),
    ConfigEntry (ParameterBool,     "Use AMD Power-Saving Instructions for Busy-Wait",                                                                                dll_ini,       "Render.FrameRate",        "UseAMDMWAITX"                        ),
    ConfigEntry (ParameterBool,     "Apply Pacing to Native frames when using DLSS Frame Gen.",                                                                       dll_ini,       "Render.FrameRate",        "EnableStreamlinePacing"              ),
    ConfigEntry (ParameterInt,      "Level of DLSS Frame Gen pacing latency reduction.",                                                                              dll_ini,       "Render.FrameRate",        "StreamlinePacingMode"                ),
    ConfigEntry (ParameterBool,     "Ignore environment variable-defined framerate limits.",                                                                          dll_ini,       "Render.FrameRate",        "IgnoreEnvironmentVars"               ),
    ConfigEntry (ParameterBool,     "Boost Compositor Clock on Windows 11+ (Dynamic Refresh)",                                                                        dll_ini,       "Render.FrameRate",        "BoostCompositorClock"                ),
    ConfigEntry (ParameterFloat,    "Minimum percentage of limiter time spent busy-waiting.",                                                                         dll_ini,       "Render.FrameRate",        "BusyWaitPercent"                     ),
    ConfigEntry (ParameterFloat,    "How aggressively (scale of 0-10) to switch to busy-wait for wait durations exceeding the scheduler's resolution.",               dll_ini,       "Render.FrameRate",        "BusyWaitBias"                        ),
    ConfigEntry (ParameterInt,      "Maximum number of CPU-side frames to work ahead of GPU.",                                                                        dll_ini,       "FrameRate.Engine",        "MaxRenderAheadFrames"                ),
    ConfigEntry (ParameterBool,     "Allow the game to use a Latency Waitable SwapChain.",                                                                            dll_ini,       "FrameRate.Engine",        "AllowDXGILatencyWait"                ),
    ConfigEntry (ParameterInt,      "Number of CPU cores to tell the game about",                                                                                     dll_ini,       "FrameRate.Engine",        "OverrideCPUCoreCount"                ),
    ConfigEntry (ParameterBool,     "Set the process timer resolution to the maximum supported",                                                                      dll_ini,       "FrameRate.Engine",        "UseMaxTimerResolution"               ),
    ConfigEntry (ParameterBool,     "Force Waitable Timer code to use Win10 High-Res Timers",                                                                         dll_ini,       "FrameRate.Engine",        "ForceHighResTimers"                  ),
    ConfigEntry (ParameterBool,     "Pace the game thread in supported engines (i.e. Unity)",                                                                         dll_ini,       "FrameRate.Engine",        "PaceGameThread"                      ),
    ConfigEntry (ParameterInt,      "Offset in Scanlines from Top of Screen to Steer Tearing",                                                                        dll_ini,       "FrameRate.LatentSync",    "TearlineOffset"                      ),
    ConfigEntry (ParameterInt,      "Frequency (in -frames or milliseconds) to Resync Timing",                                                                        dll_ini,       "FrameRate.LatentSync",    "ResyncFrequency"                     ),
    ConfigEntry (ParameterFloat,    "Controls Distribution of Idle Time Per-Delayed Frame",                                                                           dll_ini,       "FrameRate.LatentSync",    "DelayBias"                           ),
    ConfigEntry (ParameterBool,     "Automatically Sets Delay Bias For Minimum Latency",                                                                              dll_ini,       "FrameRate.LatentSync",    "AutoBias"                            ),
    ConfigEntry (ParameterStringW,  "Target input latency (in milliseconds or %) for auto-bias",                                                                      dll_ini,       "FrameRate.LatentSync",    "AutoBiasTarget"                      ),
    ConfigEntry (ParameterFloat,    "Maximum percentage to bias towards low input latency",                                                                           dll_ini,       "FrameRate.LatentSync",    "MaxAutoBias"                         ),
    ConfigEntry (ParameterBool,     "Enable __SK_LatentSyncSkip in 2x.. mode",                                                                                        dll_ini,       "FrameRate.LatentSync",    "SkipFrames"                          ),
    ConfigEntry (ParameterBool,     "Force Vulkan to use Mailbox Presentation Mode",                                                                                  dll_ini,       "Render.Vulkan",           "ForceMailboxPresent"                 ),
    ConfigEntry (ParameterBool,     "Force Vulkan to use FIFO Relaxed Presentation Mode",                                                                             dll_ini,       "Render.Vulkan",           "ForceAdaptiveVSYNC"                  ),
    ConfigEntry (ParameterBool,     "Enable DWM Tearing (Windows 10+)",                                                                                               dll_ini,       "Render.DXGI",             "AllowTearingInDWM"                   ),
    ConfigEntry (ParameterBool,     "Enable Flip Model to Render (and drop) frames at rates >refresh rate with VSYNC enabled (similar to NV Fast Sync).",             dll_ini,       "Render.DXGI",             "DropLateFrames"                      ),
    ConfigEntry (ParameterBool,     "If G-Sync is seen supported, automatically optimize thelimiter for low-latency.",                                                dll_ini,       "Render.DXGI",             "AutoLowLatency"                      ),
    ConfigEntry (ParameterBool,     "Indicates that Auto VRR activated and has turned off",                                                                           dll_ini,       "Render.DXGI",             "AutoLowLatencyTriggered"             ),
    ConfigEntry (ParameterBool,     "Auto Low-Latency Mode may add stutter to get lower latency",                                                                     input_ini,     "Input.AutoLowLatency",    "UltraLowLatency"                     ),
    ConfigEntry (ParameterBool,     "Global policy applied when starting a game the first time",                                                                      input_ini,     "Input.AutoLowLatency",    "DefaultPolicy"                       ),
    ConfigEntry (ParameterBool,     "Global policy to reapply AutoVRR if display/refresh change",                                                                     input_ini,     "Input.AutoLowLatency",    "AutoReapply"                         ),
    ConfigEntry (ParameterBool,     "Enable NVIDIA Reflex Integration w/ SK's limiter",                                                                               dll_ini,       "NVIDIA.Reflex",           "Enable"                              ),
    ConfigEntry (ParameterBool,     "Low Latency Mode",                                                                                                               dll_ini,       "NVIDIA.Reflex",           "LowLatency"                          ),
    ConfigEntry (ParameterBool,     "Reflex Boost (lower-latency power scaling)",                                                                                     dll_ini,       "NVIDIA.Reflex",           "LowLatencyBoost"                     ),
    ConfigEntry (ParameterBool,     "Train Reflex using Latency Markers for Optimization",                                                                            dll_ini,       "NVIDIA.Reflex",           "OptimizeByMarkers"                   ),
    ConfigEntry (ParameterInt,      "When to apply Reflex's magic",                                                                                                   dll_ini,       "NVIDIA.Reflex",           "EngagementPolicy"                    ),
    ConfigEntry (ParameterBool,     "Use SK's Reflex Mode options instead of the game's",                                                                             dll_ini,       "NVIDIA.Reflex",           "OverrideNativeMode"                  ),
    ConfigEntry (ParameterBool,     "Use Reflex's framerate limiter (SK's target) instead of SK",                                                                     dll_ini,       "NVIDIA.Reflex",           "UseFramerateLimiter"                 ),
    ConfigEntry (ParameterBool,     "Use Reflex's framerate limiter (AND SK's)",                                                                                      dll_ini,       "NVIDIA.Reflex",           "CombineFramerateLimiters"            ),
    ConfigEntry (ParameterBool,     "Disable a game's native Reflex implementation",                                                                                  dll_ini,       "NVIDIA.Reflex",           "DisableNative"                       ),
    ConfigEntry (ParameterBool,     "Show detailed stage timing pipeline diagram on widget",                                                                          osd_ini,       "NVIDIA.Reflex",           "ShowDetailsInWidget"                 ),
    ConfigEntry (ParameterBool,     "Allow game to use new undocumented Reflex Sync API",                                                                             dll_ini,       "NVIDIA.Reflex",           "AllowReflexSync"                     ),
    ConfigEntry (ParameterBool,     "Force DLAA in games that do not normally support it",                                                                            dll_ini,       "NVIDIA.DLSS",             "ForceDLAA"                           ),
    ConfigEntry (ParameterInt,      "Override DLSS Sharpening Mode",                                                                                                  dll_ini,       "NVIDIA.DLSS",             "UseSharpening"                       ),
    ConfigEntry (ParameterFloat,    "Sharpness Value to Use",                                                                                                         dll_ini,       "NVIDIA.DLSS",             "ForcedSharpness"                     ),
    ConfigEntry (ParameterBool,     "Always load SK's Plug-In DLSS DLL instead of the game's",                                                                        dll_ini,       "NVIDIA.DLSS",             "AutoRedirectDLL"                     ),
    ConfigEntry (ParameterInt,      "Override DLSS Perf/Quality Level's Preset",                                                                                      dll_ini,       "NVIDIA.DLSS",             "ForcePreset"                         ),
    ConfigEntry (ParameterInt,      "Override Ray Reconstruction Perf/Quality Level's Preset",                                                                        dll_ini,       "NVIDIA.DLSS",             "ForcePresetRR"                       ),
    ConfigEntry (ParameterInt,      "Override DLSS Auto Exposure",                                                                                                    dll_ini,       "NVIDIA.DLSS",             "ForcedAutoExposure"                  ),
    ConfigEntry (ParameterInt,      "Override DLSS Alpha Upscaling (3.7.0+)",                                                                                         dll_ini,       "NVIDIA.DLSS",             "ForceAlphaUpscale"                   ),
    ConfigEntry (ParameterInt,      "Allow forcing multi-frame generation on in older games.",                                                                        dll_ini,       "NVIDIA.DLSS",             "ForcedMaxMultiFrameCount"            ),
    ConfigEntry (ParameterInt,      "Override Ray Reconstruction Hardware Depth",                                                                                     dll_ini,       "NVIDIA.DLSS",             "ForcedHardwareDepth"                 ),
    ConfigEntry (ParameterFloat,    "Custom scale factor (if != 0.0f) to use for Performance",                                                                        dll_ini,       "NVIDIA.DLSS",             "CustomPerformanceScale"              ),
    ConfigEntry (ParameterFloat,    "Custom scale factor (if != 0.0f) to use for Balanced",                                                                           dll_ini,       "NVIDIA.DLSS",             "CustomBalancedScale"                 ),
    ConfigEntry (ParameterFloat,    "Custom scale factor (if != 0.0f) to use for Quality",                                                                            dll_ini,       "NVIDIA.DLSS",             "CustomQualityScale"                  ),
    ConfigEntry (ParameterFloat,    "Custom scale factor (if != 0.0f) to use for Ultra Perf.",                                                                        dll_ini,       "NVIDIA.DLSS",             "CustomUltraPerfScale"                ),
    ConfigEntry (ParameterFloat,    "Minimum Dynamic Resolution (scale) used by custom scales",                                                                       dll_ini,       "NVIDIA.DLSS",             "CustomMinDynamicRes"                 ),
    ConfigEntry (ParameterFloat,    "Maximum Dynamic Resolution (scale) used by custom scales",                                                                       dll_ini,       "NVIDIA.DLSS",             "CustomMaxDynamicRes"                 ),
    ConfigEntry (ParameterInt,      "Spoof AppId for compatibility",                                                                                                  dll_ini,       "NVIDIA.DLSS",             "OverrideAppId"                       ),
    ConfigEntry (ParameterInt,      "Add extra pixels when forcing DLAA",                                                                                             dll_ini,       "NVIDIA.DLSS",             "ExtraPixelsForDLAA"                  ),
    ConfigEntry (ParameterBool,     "Disable OTA updates (i.e. NVIDIA phone-home every launch)",                                                                      dll_ini,       "NVIDIA.DLSS",             "DisableOTAUpdates"                   ),
    ConfigEntry (ParameterBool,     "Show the in-use features in the DLSS settings tab",                                                                              osd_ini,       "NVIDIA.DLSS",             "ShowActiveFeatures"                  ),
    ConfigEntry (ParameterBool,     "Allow scRGB even if DLSS-G DLLs are detected",                                                                                   dll_ini,       "NVIDIA.DLSS",             "AllowSCRGBinDLSSG"                   ),
    ConfigEntry (ParameterBool,     "Allow fake frame pacing stats for fake frames?",                                                                                 dll_ini,       "NVIDIA.DLSS",             "AllowFlipMetering"                   ),
    ConfigEntry (ParameterBool,     "Report all NGX (D3D11/D3D12) features supported on all HW.",                                                                     dll_ini,       "NVIDIA.DLSS",             "SpoofFeatureSupport"                 ),
    ConfigEntry (ParameterBool,     "Output Streamline Framework's Debug to game_output.log.",                                                                        dll_ini,       "NVIDIA.DLSS",             "UseStreamlineDebugLog"               ),
    ConfigEntry (ParameterBool,     "Prevent DLSS5 from being used.",                                                                                                 dll_ini,       "NVIDIA.DLSS",             "SlopStop5000"                        ),
    ConfigEntry (ParameterBool,     "Experimental - Use 32bpc for HDR",                                                                                               dll_ini,       "SpecialK.HDR",            "Enable128BitPipeline"                ),
    ConfigEntry (ParameterBool,     "Do not use Floating-Point RTs when re-mastering 8-bpc+ RTs",                                                                     dll_ini,       "SpecialK.HDR",            "Keep8BpcRemastersUNORM"              ),
    ConfigEntry (ParameterBool,     "Do not use FP RTs when re-mastering reduced resolution RTS",                                                                     dll_ini,       "SpecialK.HDR",            "KeepSubnativeRemastersUNORM"         ),
    ConfigEntry (ParameterStringW,  "Last Used DXGI Colorspace,. auto-enables HDR features...",                                                                       dll_ini,       "SpecialK.HDR",            "LastUsedColorSpace"                  ),
    ConfigEntry (ParameterBool,     "Changes hook order in order to allow recording the OSD.",                                                                        dll_ini,       "Render.OSD",              "ShowInVideoCapture"                  ),
    ConfigEntry (ParameterBool,     "Enable OpenGL Debug Output (in legacy OpenGL games)",                                                                            dll_ini,       "OpenGL.System",           "EnableDebug"                         ),
    ConfigEntry (ParameterBool,     "Use 10-bpc in SDR if the game's pixel type allow",                                                                               dll_ini,       "OpenGL.System",           "Prefer10BitColor"                    ),
    ConfigEntry (ParameterBool,     "Force PURE device off",                                                                                                          dll_ini,       "Render.D3D9",             "ForceImpure"                         ),
    ConfigEntry (ParameterBool,     "Enable Texture Modding Support",                                                                                                 dll_ini,       "Render.D3D9",             "EnableTextureMods"                   ),
    ConfigEntry (ParameterBool,     "Enable D3D9Ex FlipEx SwapEffect",                                                                                                dll_ini,       "Render.D3D9",             "EnableFlipEx"                        ),
    ConfigEntry (ParameterBool,     "Use D3D9On12 instead of normal driver",                                                                                          dll_ini,       "Render.D3D9",             "UseD3D9On12"                         ),
    ConfigEntry (ParameterFloat,    "Maximum Frame Delta Time",                                                                                                       dll_ini,       "Render.DXGI",             "MaxDeltaTime"                        ),
    ConfigEntry (ParameterBool,     "Use Flip Discard - Windows 10+",                                                                                                 dll_ini,       "Render.DXGI",             "UseFlipDiscard"                      ),
    ConfigEntry (ParameterBool,     "Force Sequential (requires UseFlipDiscard or native Flip)",                                                                      dll_ini,       "Render.DXGI",             "ForceFlipSequential"                 ),
    ConfigEntry (ParameterBool,     "Disable Flip Model - Fix AMD Drivers in Yakuza0",                                                                                dll_ini,       "Render.DXGI",             "DisableFlipModel"                    ),
    ConfigEntry (ParameterInt,      "Override DXGI Adapter",                                                                                                          dll_ini,       "Render.DXGI",             "AdapterOverride"                     ),
    ConfigEntry (ParameterStringW,  "Maximum Resolution To Report",                                                                                                   dll_ini,       "Render.DXGI",             "MaxRes"                              ),
    ConfigEntry (ParameterStringW,  "Minimum Resolution To Report",                                                                                                   dll_ini,       "Render.DXGI",             "MinRes"                              ),
    ConfigEntry (ParameterFloat,    "Maximum Refresh To Report",                                                                                                      dll_ini,       "Render.DXGI",             "MaxRefresh"                          ),
    ConfigEntry (ParameterFloat,    "Minimum Refresh To Report",                                                                                                      dll_ini,       "Render.DXGI",             "MinRefresh"                          ),
    ConfigEntry (ParameterInt,      "Time to wait in msec. for SwapChain",                                                                                            dll_ini,       "Render.DXGI",             "SwapChainWait"                       ),
    ConfigEntry (ParameterStringW,  "Scaling Preference (DontCare | Centered | Stretched | Unspecified)",                                                             dll_ini,       "Render.DXGI",             "Scaling"                             ),
    ConfigEntry (ParameterStringW,  "D3D11 Exception Handling (DontCare | Raise | Ignore)",                                                                           dll_ini,       "Render.DXGI",             "ExceptionMode"                       ),
    ConfigEntry (ParameterBool,     "DXGI Debug Layer Support",                                                                                                       dll_ini,       "Render.DXGI",             "EnableDebugLayer"                    ),
    ConfigEntry (ParameterStringW,  "Scanline Order (DontCare | Progressive | LowerFieldFirst | UpperFieldFirst )",                                                   dll_ini,       "Render.DXGI",             "ScanlineOrder"                       ),
    ConfigEntry (ParameterStringW,  "Screen Rotation (DontCare | Identity | 90 | 180 | 270 )",                                                                        dll_ini,       "Render.DXGI",             "Rotation"                            ),
    ConfigEntry (ParameterBool,     "Test SwapChain Presentation Before Actually Presenting",                                                                         dll_ini,       "Render.DXGI",             "TestSwapChainPresent"                ),
    ConfigEntry (ParameterBool,     "Use 32-bit Depth + 8-bit Stencil + 24-bit Padding",                                                                              dll_ini,       "Render.DXGI",             "Use64BitDepthStencil"                ),
    ConfigEntry (ParameterBool,     "Isolate D3D11 Deferred Context Queues instead of Tracking in Immediate Mode.",                                                   dll_ini,       "Render.DXGI",             "IsolateD3D11DeferredContexts"        ),
    ConfigEntry (ParameterInt,      "Override ON-SCREEN Multisample Antialiasing Level,.-1=None",                                                                     dll_ini,       "Render.DXGI",             "OverrideMSAA"                        ),
    ConfigEntry (ParameterBool,     "Nix the swapchain present flag: DXGI_PRESENT_TEST to workaround bad third-party software that doesn't handle it correctly.",     dll_ini,       "Render.DXGI",             "SkipSwapChainPresentTest"            ),
    ConfigEntry (ParameterInt,      "How to handle sRGB SwapChains when we have to kill them for Flip Model support (-1=Passthrough, 0=Strip, 1=Apply)",              dll_ini,       "Render.DXGI",             "sRGBBypassBehavior"                  ),
    ConfigEntry (ParameterBool,     "Disable D3D11 Render Mods (for slight perf. increase)",                                                                          dll_ini,       "Render.DXGI",             "LowSpecMode"                         ),
    ConfigEntry (ParameterBool,     "Prevent games from detecting monitor HDR support",                                                                               dll_ini,       "Render.DXGI",             "HideHDRSupport"                      ),
    ConfigEntry (ParameterInt,      "Override the HDR header metadata type set by a game",                                                                            dll_ini,       "Render.DXGI",             "HDRMetadataType"                     ),
    ConfigEntry (ParameterBool,     "Cache DXGI Factories to reduce display mode list overhead",                                                                      dll_ini,       "Render.DXGI",             "UseFactoryCache"                     ),
    ConfigEntry (ParameterBool,     "Try to keep resolution setting changes to a minimum",                                                                            dll_ini,       "Render.DXGI",             "SkipRedundantModeChanges"            ),
    ConfigEntry (ParameterBool,     "Temporarily Enable DWM-based HDR while the game runs",                                                                           dll_ini,       "Render.DXGI",             "TemporaryDesktopHDRMode"             ),
    ConfigEntry (ParameterBool,     "Disable Dynamic Refresh Rate (VBLANK Virtualization)",                                                                           dll_ini,       "Render.DXGI",             "DisableVirtualizedBlanking"          ),
    ConfigEntry (ParameterBool,     "Clear the SwapChain Backbuffer every frame",                                                                                     dll_ini,       "Render.DXGI",             "ClearFlipModelBackbuffers"           ),
    ConfigEntry (ParameterInt,      "Warn if VRAM used exceeds this % of available VRAM",                                                                             dll_ini,       "Render.DXGI",             "WarnIfUsedVRAMPercentExceeds"        ),
    ConfigEntry (ParameterBool,     "Feel like shooting your foot with unsafe d3d12 settings..?",                                                                     dll_ini,       "Render.DXGI",             "AllowD3D12FootGuns"                  ),
    ConfigEntry (ParameterBool,     "Lie to games and tell them they're in FSE, all the while, they are actually running a fullscreen borderless window.",            dll_ini,       "Render.DXGI",             "FakeFullscreenMode"                  ),
    ConfigEntry (ParameterFloat,    "Multiplier for reported VRAM budget for D3D12 era engines.",                                                                     dll_ini,       "Render.DXGI",             "VRAMBudgetScale"                     ),
    ConfigEntry (ParameterInt,      "Maximum Anisotropic Filter Level",                                                                                               dll_ini,       "Render.D3D12",            "MaxAnisotropy"                       ),
    ConfigEntry (ParameterBool,     "Forced Anisotropic Filtering",                                                                                                   dll_ini,       "Render.D3D12",            "ForceAnisotropic"                    ),
    ConfigEntry (ParameterFloat,    "Forced LOD Bias",                                                                                                                dll_ini,       "Render.D3D12",            "ForceLODBias"                        ),
    ConfigEntry (ParameterBool,     "Disable DirectStorage BypassIO",                                                                                                 dll_ini,       "Render.DStorage",         "DisableBypassIO"                     ),
    ConfigEntry (ParameterBool,     "Disable DirectStorage Telemetry",                                                                                                dll_ini,       "Render.DStorage",         "DisableTelemetry"                    ),
    ConfigEntry (ParameterBool,     "Disable DirectStorage (1.2) GPU Decompression",                                                                                  dll_ini,       "Render.DStorage",         "DisableGPUDecompression"             ),
    ConfigEntry (ParameterBool,     "Force DirectStorage File Buffering",                                                                                             dll_ini,       "Render.DStorage",         "ForceFileBuffering"                  ),
    ConfigEntry (ParameterInt,      "Override default number of DirectStorage Submit threads",                                                                        dll_ini,       "Render.DStorage",         "NumberOfSubmitThreads"               ),
    ConfigEntry (ParameterInt,      "Override default number of CPU Decompression threads",                                                                           dll_ini,       "Render.DStorage",         "NumberOfCPUDecompThreads"            ),
    ConfigEntry (ParameterBool,     "Hook DirectStorage for additional features",                                                                                     dll_ini,       "Render.DStorage",         "EnableHooks"                         ),
    ConfigEntry (ParameterBool,     "Create a standalone D3D12 device if a DStorage game tries to allocate a DStorage queue with no D3D12 device supplied",           dll_ini,       "Render.DStorage",         "UseDummyD3D12DeviceIfNeeded"         ),
    ConfigEntry (ParameterBool,     "Clamp Negative LOD Bias",                                                                                                        dll_ini,       "Textures.D3D9",           "ClampNegativeLODBias"                ),
    ConfigEntry (ParameterBool,     "Cache Textures",                                                                                                                 dll_ini,       "Textures.D3D11",          "Cache"                               ),
    ConfigEntry (ParameterBool,     "Adds L3 to Hierarchical Cache: L3=Fmt, L2=Mips, L1=Res",                                                                         dll_ini,       "Textures.D3D11",          "CacheUsingL3Hash"                    ),
    ConfigEntry (ParameterBool,     "Precise Hash Generation",                                                                                                        dll_ini,       "Textures.D3D11",          "PreciseHash"                         ),
    ConfigEntry (ParameterBool,     "Inject Textures",                                                                                                                dll_ini,       "Textures.D3D11",          "Inject"                              ),
    ConfigEntry (ParameterBool,     "Allow image format to change during texture injection",                                                                          dll_ini,       "Textures.D3D11",          "InjectionKeepsFormat"                ),
    ConfigEntry (ParameterBool,     "Create complete mipmap chain for textures without them",                                                                         dll_ini,       "Textures.D3D11",          "GenerateMipmaps"                     ),
    ConfigEntry (ParameterStringW,  "Resource Root",                                                                                                                  dll_ini,       "Textures.General",        "ResourceRoot"                        ),
    ConfigEntry (ParameterBool,     "Dump Textures while Loading",                                                                                                    dll_ini,       "Textures.General",        "DumpOnFirstLoad"                     ),
    ConfigEntry (ParameterInt,      "Minimum Cached Textures",                                                                                                        dll_ini,       "Textures.Cache",          "MinEntries"                          ),
    ConfigEntry (ParameterInt,      "Maximum Cached Textures",                                                                                                        dll_ini,       "Textures.Cache",          "MaxEntries"                          ),
    ConfigEntry (ParameterInt,      "Minimum Textures to Evict",                                                                                                      dll_ini,       "Textures.Cache",          "MinEvict"                            ),
    ConfigEntry (ParameterInt,      "Maximum Textures to Evict",                                                                                                      dll_ini,       "Textures.Cache",          "MaxEvict"                            ),
    ConfigEntry (ParameterInt,      "Minimum Data Size to Evict",                                                                                                     dll_ini,       "Textures.Cache",          "MinSizeInMiB"                        ),
    ConfigEntry (ParameterInt,      "Maximum Data Size to Evict",                                                                                                     dll_ini,       "Textures.Cache",          "MaxSizeInMiB"                        ),
    ConfigEntry (ParameterBool,     "Ignore textures without mipmaps?",                                                                                               dll_ini,       "Textures.Cache",          "IgnoreNonMipmapped"                  ),
    ConfigEntry (ParameterBool,     "Enable texture caching/dumping/injecting staged textures",                                                                       dll_ini,       "Textures.Cache",          "AllowStaging"                        ),
    ConfigEntry (ParameterBool,     "For games with broken resource reference counting, allow textures to be cached anyway (needed for injection).",                  dll_ini,       "Textures.Cache",          "AllowUnsafeRefCounting"              ),
    ConfigEntry (ParameterBool,     "Actively manage D3D11 teture residency",                                                                                         dll_ini,       "Textures.Cache",          "ManageResidency"                     ),
    ConfigEntry (ParameterBool,     "Disable NvAPI",                                                                                                                  dll_ini,       "NVIDIA.API",              "Disable"                             ),
    ConfigEntry (ParameterBool,     "Prevent Game from Using NvAPI HDR Features",                                                                                     dll_ini,       "NVIDIA.API",              "DisableHDR"                          ),
    ConfigEntry (ParameterBool,     "Enable/Disable Vulkan DXGI Layer",                                                                                               dll_ini,       "NVIDIA.API",              "EnableVulkanBridge"                  ),
    ConfigEntry (ParameterBool,     "By default, Special K disables Ansel at first launch, but users have an option under 'Help|..' to turn it back on.",             dll_ini,       "NVIDIA.Bugs",             "AnselSleepsWithFishes"               ),
    ConfigEntry (ParameterBool,     "Forcefully block nvcamera{64}.dll",                                                                                              dll_ini,       "NVIDIA.Bugs",             "DisableAnselShimLoader"              ),
    ConfigEntry (ParameterBool,     "Alternate DXGI/D3D11/D3D12 Hook Implementation for NV BUG",                                                                      dll_ini,       "NVIDIA.Bugs",             "StreamlineCompatibilityMode"         ),
    ConfigEntry (ParameterBool,     "Compat hack for games that use DLSS FrameGen without Reflex",                                                                    dll_ini,       "NVIDIA.Bugs",             "DisableReflexSleep"                  ),
    ConfigEntry (ParameterBool,     "Will not draw notifications until user requests them.",                                                                          notify_ini,    "Notification.System",     "Silent"                              ),
    ConfigEntry (ParameterBool,     "Draw ReShade before SK's overlay in AddOn capable versions",                                                                     dll_ini,       "ReShade.System",          "DrawFirst"                           ),
    ConfigEntry (ParameterBool,     "Supress warnings for incompatible ReShade AddOns",                                                                               dll_ini,       "ReShade.System",          "UnsafeAddOns"                        ),
    ConfigEntry (ParameterBool,     "Enable Special K's ReShade Add-On when RenoDX is in use.",                                                                       dll_ini,       "ReShade.System",          "AllowSKAddOnWithRenoDX"              ),
    ConfigEntry (ParameterBool,     "Respond to creation and destruction of ReShade runtimes.",                                                                       dll_ini,       "ReShade.System",          "AllowRuntimeTracking"                ),
    ConfigEntry (ParameterBool,     "Add a second framerate limiter to compensate for ReShade.",                                                                      dll_ini,       "ReShade.System",          "RequireFrameGenPacingFix"            ),
    ConfigEntry (ParameterBool,     "Show Software EULA",                                                                                                             dll_ini,       "SpecialK.System",         "ShowEULA"                            ),
    ConfigEntry (ParameterBool,     "Disable Alpha Transparency (reduce flicker)",                                                                                    dll_ini,       "ImGui.Render",            "DisableAlpha"                        ),
    ConfigEntry (ParameterBool,     "Reduce Aliasing on (but dim) Line Edges",                                                                                        dll_ini,       "ImGui.Render",            "AntialiasLines"                      ),
    ConfigEntry (ParameterBool,     "Reduce Aliasing on (but widen) Window Borders",                                                                                  dll_ini,       "ImGui.Render",            "AntialiasContours"                   ),
    ConfigEntry (ParameterBool,     "Disable DPI Scaling",                                                                                                            dll_ini,       "DPI.Scaling",             "Disable"                             ),
    ConfigEntry (ParameterBool,     "Windows 8.1+ Monitor DPI Awareness (needed for UE4)",                                                                            dll_ini,       "DPI.Scaling",             "PerMonitorAware"                     ),
    ConfigEntry (ParameterBool,     "Further fixes required for UE4",                                                                                                 dll_ini,       "DPI.Scaling",             "MonitorAwareOnAllThreads"            ),
    ConfigEntry (ParameterBool,     "Log file read activity to logs/file_read.log",                                                                                   dll_ini,       "FileIO.Trace",            "LogReads"                            ),
    ConfigEntry (ParameterStringW,  "Don't log activity for files in this list",                                                                                      dll_ini,       "FileIO.Trace",            "IgnoreReads"                         ),
    ConfigEntry (ParameterBool,     "Log file write activity to logs/file_write.log",                                                                                 dll_ini,       "FileIO.Trace",            "LogWrites"                           ),
    ConfigEntry (ParameterStringW,  "Don't log activity for files in this list",                                                                                      dll_ini,       "FileIO.Trace",            "IgnoreWrites"                        ),
    ConfigEntry (ParameterStringW,  "Power Policy (GUID) to Apply At Application Start",                                                                              dll_ini,       "CPU.Power",               "PowerSchemeGUID"                     ),
    ConfigEntry (ParameterBool,     "Always boost process priority to Above Normal",                                                                                  dll_ini,       "Scheduler.Boost",         "AlwaysRaisePriority"                 ),
    ConfigEntry (ParameterBool,     "Boost process priority to Above Normal in Background",                                                                           dll_ini,       "Scheduler.Boost",         "RaisePriorityInBackground"           ),
    ConfigEntry (ParameterBool,     "Boost process priority to Above Normal in Foreground",                                                                           dll_ini,       "Scheduler.Boost",         "RaisePriorityInForeground"           ),
    ConfigEntry (ParameterBool,     "Boost process priority to High instead of Above Normal",                                                                         dll_ini,       "Scheduler.Boost",         "RaisePriorityToHigh"                 ),
    ConfigEntry (ParameterBool,     "Do not allow third-party apps to change priority",                                                                               dll_ini,       "Scheduler.Boost",         "DenyForeignChanges"                  ),
    ConfigEntry (ParameterInt,      "Minimum priority for a game's render thread",                                                                                    dll_ini,       "Scheduler.Boost",         "MinimumRenderThreadPriority"         ),
    ConfigEntry (ParameterInt64,    "Mask of CPU cores the process is eligible for scheduling.",                                                                      dll_ini,       "Scheduler.System",        "ProcessorAffinityMask"               ),
    ConfigEntry (ParameterBool,     "Limit the scheduler to Intel P-Cores",                                                                                           dll_ini,       "Scheduler.System",        "PerformanceCoresOnly"                ),
    ConfigEntry (ParameterBool,     "Minimize Audio Latency while Game is Running",                                                                                   dll_ini,       "Sound.Mixing",            "MinimizeLatency"                     ),
    ConfigEntry (ParameterInt,      "Control when SKIF auto-stops, 0=Never, 1=AtStart, 2=AtExit",                                                                     platform_ini,  "SKIF.System",             "AutoStopBehavior"                    ),
    ConfigEntry (ParameterStringW,  "Achievement Sound File",                                                                                                         platform_ini,  "Platform.Achievements",   "SoundFile"                           ),
    ConfigEntry (ParameterBool,     "Silence is Bliss?",                                                                                                              platform_ini,  "Platform.Achievements",   "PlaySound"                           ),
    ConfigEntry (ParameterBool,     "Precious Memories",                                                                                                              platform_ini,  "Platform.Achievements",   "TakeScreenshot"                      ),
    ConfigEntry (ParameterBool,     "Friendly Competition",                                                                                                           platform_ini,  "Platform.Achievements",   "FetchFriendStats"                    ),
    ConfigEntry (ParameterStringW,  "Achievement Popup Position",                                                                                                     platform_ini,  "Platform.Achievements",   "PopupOrigin"                         ),
    ConfigEntry (ParameterBool,     "Achievement Notification Animation",                                                                                             platform_ini,  "Platform.Achievements",   "AnimatePopup"                        ),
    ConfigEntry (ParameterBool,     "Achievement Popup Includes Game Title?",                                                                                         platform_ini,  "Platform.Achievements",   "ShowPopupTitle"                      ),
    ConfigEntry (ParameterFloat,    "Achievement Notification Inset X",                                                                                               platform_ini,  "Platform.Achievements",   "PopupInset"                          ),
    ConfigEntry (ParameterInt,      "Achievement Popup Duration (in ms)",                                                                                             platform_ini,  "Platform.Achievements",   "PopupDuration"                       ),
    ConfigEntry (ParameterInt,      "Maximum columns to fill if achievement popups do not fit",                                                                       platform_ini,  "Platform.Achievements",   "MaxPopupColumns"                     ),
    ConfigEntry (ParameterInt,      "Maximum number of achievement popups visible at once",                                                                           platform_ini,  "Platform.Achievements",   "MaxPopupsOnScreen"                   ),
    ConfigEntry (ParameterStringW,  "Overlay Notification Position (non-Big Picture Mode)",                                                                           dll_ini,       "Platform.System",         "NotifyCorner"                        ),
    ConfigEntry (ParameterBool,     "Pause Overlay Aware games when control panel is visible",                                                                        dll_ini,       "Platform.System",         "ReuseOverlayPause"                   ),
    ConfigEntry (ParameterInt,      "The Steam AppID for this non-Steam game if it exists",                                                                           dll_ini,       "Platform.System",         "EquivalentSteamApp"                  ),
    ConfigEntry (ParameterStringW,  "String identifying the detected store for this game",                                                                            dll_ini,       "Platform.System",         "Type"                                ),
    ConfigEntry (ParameterInt64,    "Steam AppID",                                                                                                                    dll_ini,       "Steam.System",            "AppID"                               ),
    ConfigEntry (ParameterInt,      "Delay SteamAPI initialization if the game doesn't do it",                                                                        dll_ini,       "Steam.System",            "AutoInitDelay"                       ),
    ConfigEntry (ParameterBool,     "Should we force the game to run Steam callbacks?",                                                                               dll_ini,       "Steam.System",            "AutoPumpCallbacks"                   ),
    ConfigEntry (ParameterBool,     "Block the User Stats Receipt Callback?",                                                                                         dll_ini,       "Steam.System",            "BlockUserStatsCallback"              ),
    ConfigEntry (ParameterBool,     "Filter Unrelated Data from the User Stats Receipt Callback",                                                                     dll_ini,       "Steam.System",            "FilterExternalDataFromCallbacks"     ),
    ConfigEntry (ParameterBool,     "Load the Steam Client DLL Early?",                                                                                               dll_ini,       "Steam.System",            "PreLoadSteamClient"                  ),
    ConfigEntry (ParameterBool,     "Load the Steam Overlay Early",                                                                                                   dll_ini,       "Steam.System",            "PreLoadSteamOverlay"                 ),
    ConfigEntry (ParameterBool,     "Forcefully load steam_api{64}.dll",                                                                                              dll_ini,       "Steam.System",            "ForceLoadSteamAPI"                   ),
    ConfigEntry (ParameterBool,     "Automatically load steam_api{64}.dll into any game whose path includes SteamApps\\common, but doesn't use steam_api",            dll_ini,       "Steam.System",            "AutoInjectSteamAPI"                  ),
    ConfigEntry (ParameterInt,      "Always apply a social state (defined by EPersonaState) at application start",                                                    dll_ini,       "Steam.Social",            "OnlineStatus"                        ),
    ConfigEntry (ParameterStringW,  "Path to a known-working SteamAPI dll for this game.",                                                                            dll_ini,       "Steam.System",            "SteamPipeDLL"                        ),
    ConfigEntry (ParameterInt,      "-1=Unlimited, 0-oo=Upper bound limit to SteamAPI rate",                                                                          dll_ini,       "Steam.System",            "CallbackThrottle"                    ),
    ConfigEntry (ParameterBool,     "Disable Steam Overlay using 'SteamNoOverlayUIDrawing'",                                                                          dll_ini,       "Steam.System",            "NoDrawOverlay"                       ),
    ConfigEntry (ParameterBool,     "Special mode to workaround CAPCOM DRM without memory leaks",                                                                     dll_ini,       "Steam.System",            "BypassCRAPCOM"                       ),
    ConfigEntry (ParameterBool,     "Enhanced screenshot speed and HUD options,. D3D11-only.",                                                                        dll_ini,       "Steam.Screenshots",       "EnableSmartCapture"                  ),
    ConfigEntry (ParameterBool,     "Should a screenshot triggered BY Steam include SK's OSD?",                                                                       platform_ini,  "Steam.Screenshots",       "DefaultKeybindCapturesOSD"           ),
    ConfigEntry (ParameterBool,     "Start GalaxyCommunication.exe if Galaxy is not running.",                                                                        dll_ini,       "Galaxy.System",           "SpawnGalaxyCommunication"            ),
    ConfigEntry (ParameterBool,     "Enable if the current game is unsuitable for offline-only.",                                                                     dll_ini,       "Galaxy.System",           "RequireOnlineGalaxyMode"             ),
    ConfigEntry (ParameterFloat,    "Make the Steam Overlay visible in HDR mode!",                                                                                    platform_ini,  "Platform.Overlay",        "Luminance_scRGB"                     ),
    ConfigEntry (ParameterBool,     "Makes steam_api.log go away [DISABLES STEAMAPI FEATURES]",                                                                       dll_ini,       "Steam.Log",               "Silent"                              ),
    ConfigEntry (ParameterStringW,  "CSV list of files to block from cloud sync.",                                                                                    dll_ini,       "Steam.Cloud",             "FilesNotToSync"                      ),
    ConfigEntry (ParameterBool,     "Fix For Stupid Games That Don't Know How DRM Works",                                                                             dll_ini,       "Steam.DRMWorks",          "SpoofBLoggedOn"                      ),
  };

  std::vector<const ConfigEntry*> output;

  for (auto& param : params_to_build)
  {
    if (param.ini_type != ini_type)
      continue;

    output.push_back (&param);
  }

  return output;
}

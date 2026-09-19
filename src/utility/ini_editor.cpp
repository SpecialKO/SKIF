#include <utility/ini_editor.h>
#include <../packages_misc/ini.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <functional>

#include <utility/skif_imgui.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>

#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>


// Derp

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

namespace
{
  enum class Encoding
  {
    Utf8,
    Utf16LE,
    Utf16BE,
    Utf32LE,
    Utf32BE
  };

  std::vector<std::uint8_t> ReadAllBytes(const std::wstring& path)
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

  Encoding DetectEncoding(
    const std::vector<std::uint8_t>& data,
    size_t& bomSize)
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

  std::wstring DecodeUtf8(
    const std::uint8_t* data,
    size_t size)
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

  std::wstring DecodeUtf16(
    const std::uint8_t* data,
    size_t size,
    bool littleEndian)
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

        return littleEndian
          ? static_cast<std::uint16_t>(a | (b << 8))
          : static_cast<std::uint16_t>((a << 8) | b);
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

  std::wstring DecodeUtf32(
    const std::uint8_t* data,
    size_t size,
    bool littleEndian)
  {
    if (size % 4 != 0)
      throw std::runtime_error("Invalid UTF-32 byte count");

    const size_t count = size / 4;

    std::wstring result;
    result.reserve(count);

    auto read32 = [&](size_t i) -> std::uint32_t
      {
        const std::uint8_t* p = data + i * 4;

        if (littleEndian)
        {
          return
            static_cast<std::uint32_t>(p[0]) |
            (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) |
            (static_cast<std::uint32_t>(p[3]) << 24);
        }

        return
          (static_cast<std::uint32_t>(p[0]) << 24) |
          (static_cast<std::uint32_t>(p[1]) << 16) |
          (static_cast<std::uint32_t>(p[2]) << 8) |
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
}

std::wstring ReadTextFile(const std::wstring& path)
{
  const std::vector<std::uint8_t> data = ReadAllBytes(path);

  size_t bomSize = 0;
  const Encoding encoding = DetectEncoding(data, bomSize);

  const std::uint8_t* content =
    data.data() + bomSize;

  const size_t contentSize =
    data.size() - bomSize;

  switch (encoding)
  {
  case Encoding::Utf8:
    return DecodeUtf8(content, contentSize);

  case Encoding::Utf16LE:
    return DecodeUtf16(content, contentSize, true);

  case Encoding::Utf16BE:
    return DecodeUtf16(content, contentSize, false);

  case Encoding::Utf32LE:
    return DecodeUtf32(content, contentSize, true);

  case Encoding::Utf32BE:
    return DecodeUtf32(content, contentSize, false);
  }

  throw std::runtime_error("Unknown encoding");
}

// CC BY-SA 4.0: https://stackoverflow.com/a/46711735
static constexpr uint32_t hash(const std::string_view data) noexcept
{
  uint32_t hash = 5385;

  for (const auto& e : data)
    hash = ((hash << 5) + hash) + e;

  return hash;
}

// NO DERP

struct __INI {
  char section[MAX_PATH + 2] = { };
  char key    [MAX_PATH + 2] = { };
  char value  [MAX_PATH + 2] = { };
  char default[MAX_PATH + 2] = { }; // Used when resetting any unsaved changes
  std::string _label_k;
  std::string _label_v;
  bool        _show = true;
  SK_KeybindMultiState _keybind;

  //
  void (*DrawFunction)(__INI* ptr);
  std::vector<std::string> _dditems = { };

  __INI (const std::string& _s, const std::string& _k, const std::string& _v)
  {
    strncpy (section, _s.c_str(), MAX_PATH);
    strncpy (key,     _k.c_str(), MAX_PATH);
    strncpy (default, _v.c_str(), MAX_PATH);
    strncpy (value,   default,    MAX_PATH);
    _label_k = ("###" + _s + "-" + _k);
    _label_v = ("###" + _s + "-" + _k + "-" + _v);
  }

  void Reset (void)
  {
    strncpy (value, default, MAX_PATH);
  }
};

static void DrawInputBox (__INI* ptr)
{
  ImGui::SetNextItemWidth(300.0f);
  ImGui::InputText       (ptr->_label_v.c_str(), ptr->value, MAX_PATH);
}

static void DrawDropDown (__INI* ptr)
{
  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::BeginCombo  (ptr->_label_k.c_str(), ptr->value))
  {
    for (auto& item : ptr->_dditems)
    {
      bool is_selected = (item == ptr->value);
      if (ImGui::Selectable (item.c_str(), is_selected))
        strncpy (ptr->value, item.c_str(), MAX_PATH);
      if (is_selected)
        ImGui::SetItemDefaultFocus ( );
    }
    ImGui::EndCombo  ( );
  }
}

static void DrawKeybinding (__INI* ptr)
{
  ImGui::PushID (ptr->key);
  ImGui::PushID (ptr->_keybind.bind_name.c_str());
  if (SK_ImGui_Keybinding (&ptr->_keybind))
  {
    strncpy (ptr->value, ptr->_keybind.getKeybind()->human_readable_utf8.c_str(), MAX_PATH);
  }
  ImGui::PopID ();
  ImGui::PopID ();
}

struct __INIFile {
  int     wnd_index = 0;
  std::string  path = { };
  std::string label = { };
  PopupState  state = PopupState_Open;
  std::vector <__INI> ini;

  // Filter field
  char          charFilter    [MAX_PATH + 2] = { };
  char          charFilterTmp [MAX_PATH + 2] = { };
  bool          bFilterActive = false;

  // Focused state
  bool          bWindowFocused = false;
};

int window_identifier = 0;
std::vector <__INIFile> vIniEditor_Files;
bool INIEditorActive = false;

void
SKIF_ImGui_IniEditor (const std::wstring& file_path, const std::string& file_path_utf8)
{
  PLOG_VERBOSE << "Pushing INI editor: " << file_path_utf8;
  std::string unique_label = ("INI: " + file_path_utf8 + "###IniEditor-" + std::to_string(window_identifier));

  inih::INIReader ini;
  ini.ParseContent (SK_WideCharToUTF8 (ReadTextFile (file_path)));

  std::vector <__INI> ini_parsed;

  for (auto& section : ini.Sections())
  {
    for (auto& kv : ini.Get (section))
    {
      __INI item = { section, kv.first, kv.second };

      switch (hash(kv.first))
      {
        case hash("NotifyCorner"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("TopLeft");
          item._dditems.push_back ("TopRight");
          item._dditems.push_back ("BottomLeft");
          item._dditems.push_back ("BottomRight");
          item.DrawFunction = DrawDropDown;
          break;
        }

        case hash("Scaling"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("Unspecified");
          item._dditems.push_back ("Centered");
          item._dditems.push_back ("Stretched");
          item.DrawFunction = DrawDropDown;
          break;
        }

        case hash("ScanlineOrder"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("Unspecified");
          item._dditems.push_back ("Progressive");
          item._dditems.push_back ("LowerFieldFirst");
          item._dditems.push_back ("UpperFieldFirst");
          item.DrawFunction = DrawDropDown;
          break;
        }

        case hash("ExceptionMode"):
        {
          item._dditems.push_back ("DontCare");
          item._dditems.push_back ("Raise");
          item._dditems.push_back ("Ignore");
          item.DrawFunction = DrawDropDown;
          break;
        }

        // Keybindings (Keyboard)
        case hash("Activate0"):
        case hash("Activate1"):
        case hash("Activate2"):
        case hash("Activate3"):
        {
          item._keybind = {
            item.key,
            SK_UTF8ToWideChar (item.value)
          };
          item._keybind.pending.human_readable = SK_UTF8ToWideChar (item.value);
          item._keybind.pending.parse();
          item._keybind.applyChanges();

          item.DrawFunction = DrawKeybinding;
          break;
        }

        // Keybindings (Gamepad)
        case hash("LeftPaddle"):
        case hash("LeftFunction"):
        case hash("RightFunction"):
        case hash("RightPaddle"):
        case hash("TouchpadClick"):
        {
          item.DrawFunction = DrawInputBox;
          break;
        }

        default:
        {
          if (kv.second == "true" ||
              kv.second == "false")
          {
            item._dditems.push_back ("true");
            item._dditems.push_back ("false");
            item.DrawFunction = DrawDropDown;
          }
          else {
            item.DrawFunction = DrawInputBox;
          }
        }
      }

      ini_parsed.push_back (item);
    }
  }

  vIniEditor_Files.push_back({ window_identifier, file_path_utf8, unique_label, PopupState_Open, ini_parsed });
  window_identifier++;
}

void
SKIF_ImGui_IniEditor_Process (void)
{
  bool cleanup = false;

  for (auto& file : vIniEditor_Files)
  {
    if (file.state == PopupState_Open)
    {
      ImGui::SetNextWindowSize (ImVec2 (950.0f, 750.0f) * SKIF_ImGui_GlobalDPIScale);

      extern ImRect windowRect;
      ImGui::SetNextWindowPos (windowRect.GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      file.state = PopupState_Opened;
    }

    bool show  = true;
    bool save  = false;
    bool reset = false;

    ImGui::SetNextWindowSizeConstraints (ImVec2 (640.0f, 480.0f), ImVec2 (FLT_MAX, FLT_MAX));
    ImGui::Begin (file.label.c_str(),
                      &show,
                    //ImGuiWindowFlags_NoResize          |
                      ImGuiWindowFlags_NoCollapse        |
                    //ImGuiWindowFlags_NoTitleBar        |
                    //ImGuiWindowFlags_NoScrollWithMouse | // Prevent scrolling with the mouse as well
                      ImGuiWindowFlags_NoScrollbar       |
                      ImGuiWindowFlags_NoSavedSettings
    );

    if (ImGui::Button ("Close"))
      show = false;

    ImGui::SameLine ( );

    if (ImGui::Button ("Save"))
      save = true;

    ImGui::SameLine ( );

    if (ImGui::Button ("Reset"))
      reset = true;

    // Some additional vertical padding
    ImGui::SetCursorPosY   (
      ImFloor (ImGui::GetCursorPosY ( ) + 3.0f * SKIF_ImGui_GlobalDPIScale)
    );

    bool showClearBtn      = (file.charFilter[0] != '\0');
    // Mirrors what ImGui::ButtonEx() does to calculate the height of buttons
    ImVec2 fTopFilterSize  =                           ImGui::CalcTextSize (ICON_FA_FILTER);
    float fTopZoomX        =                           ImGui::CalcTextSize (ICON_FA_MAGNIFYING_GLASS).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float fTopClearX       = (! showClearBtn ? 0.0f :  ImGui::CalcTextSize (ICON_FA_XMARK ).x           + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    float fTopFilterFieldX = ImGui::GetContentRegionAvail().x - fTopZoomX - fTopClearX; //  - fTopFilterX
    //static bool bFilterHovered = false;
  
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

    ImGui::PushItemFlag   (ImGuiItemFlags_Disabled, true);
    ImGui::Button         (ICON_FA_MAGNIFYING_GLASS);
    ImGui::PopItemFlag    ( );
    ImGui::SameLine       ( );

    ImGui::PushStyleColor (ImGuiCol_FrameBg, ImGui::GetStyleColorVec4 (ImGuiCol_ChildBg));
    ImGui::InputTextEx ("###AppListFilterField", "", file.charFilterTmp, MAX_PATH,
                        ImVec2 (fTopFilterFieldX, 0.0f),
                        ImGuiInputTextFlags_AutoSelectAll, 0, nullptr);
    
    ImGui::PopStyleColor  ( ); // ImGuiCol_FrameBg
    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    // Ctrl+F should focus filter field
    if (ImGui::GetIO().KeyCtrl && ImGui::GetKeyData(ImGuiKey_F)->DownDuration == 0.0f)
    {
      ImGui::ActivateItemByID (ImGui::GetID("###AppListFilterField"));
      ImGui::SetFocusID       (ImGui::GetID("###AppListFilterField"), ImGui::GetCurrentWindow());
    }

    // This is required to prevent the InputTextEx from not being deselected when clicking empty space
    SKIF_ImGui_DisallowMouseDragMove ( );

    file.bFilterActive = ImGui::IsItemActive ( );

    if (ImGui::IsItemFocused ( ) && ! ImGui::IsItemHovered( ) && ImGui::IsAnyMouseDown( ))
    {
      // Clear highlight from filter box
      ImGuiContext& g = *ImGui::GetCurrentContext();
      g.NavDisableHighlight = false;
    }

    // Preprocess filtered entries

    auto _ClearCharFilter = [&](void) -> void
    {
      strncpy (file.charFilter,    "\0", MAX_PATH);
      strncpy (file.charFilterTmp, "\0", MAX_PATH);
      // do more stuff ?

      for (auto& trie : file.ini)
        trie._show = true;
    };

    // Update some stuff if filter query has changed
    if (strncmp (file.charFilter, file.charFilterTmp, MAX_PATH) != 0)
    {
      if (strlen (file.charFilterTmp) == 0)
        _ClearCharFilter ( );
      else {
        strncpy (file.charFilter, file.charFilterTmp, MAX_PATH);

        for (auto& trie : file.ini)
        {
          trie._show = false;
          trie._show = (trie._show || (StrStrIA (trie.section, file.charFilter) != NULL));
          trie._show = (trie._show || (StrStrIA (trie.key,     file.charFilter) != NULL));
          trie._show = (trie._show || (StrStrIA (trie.value,   file.charFilter) != NULL));
        }
      }
    }

    //PLOG_VERBOSE << "Numbers: " << numRegular << " (regular) -- " << numPinnedOnTop << " (on top)";

    if (showClearBtn)
    {
      ImGui::SameLine ( );

      // Needed to stop flickering on the same frame as the field is emptied
      bool justCleared = (file.charFilter[0] == '\0');
      static bool btnClearHover = false;

      if (justCleared)
        ImGui::PushStyleColor (ImGuiCol_Text, ImVec4(0, 0, 0, 0));
      else if (btnClearHover)
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
      else
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));

      if (ImGui::Button (ICON_FA_XMARK))
        _ClearCharFilter ( );

      ImGui::PopStyleColor ( );

      btnClearHover = ImGui::IsItemHovered() || ImGui::IsItemActive();
    }

    ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonActive
    ImGui::PopStyleColor  ( ); // ImGuiCol_ButtonHovered
    ImGui::PopStyleColor  ( ); // ImGuiCol_Button

    // Some additional vertical padding
    ImGui::SetCursorPosY   (
      ImFloor (ImGui::GetCursorPosY ( ) + 3.0f * SKIF_ImGui_GlobalDPIScale)
    );

    ImVec2 fTop2 = ImGui::GetCursorPos ( );

    // End top options

    // Headers
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                            );

    ImGui::ItemSize        (ImVec2 (5.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "[Section]");
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (208.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "Key=");
    ImGui::SameLine        ( );
    ImGui::ItemSize        (ImVec2 (611.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), "Value");

    ImGui::PopStyleColor  ( ); // ImGuiCol_Text

    ImGui::Separator   ( );

    SKIF_ImGui_BeginChildFrame (
      ImGui::GetID ("###SKIE_CONTENT_AREA"),
      ImVec2 (0.0f, ImFloor (ImGui::GetWindowSize().y - ImGui::GetCursorPosY() - 20.0f * SKIF_ImGui_GlobalDPIScale - 2.0f * SKIF_ImGui_GlobalDPIScale)), // 900.0f
      ImGuiChildFlags_None, // ImGuiChildFlags_FrameStyle
      ImGuiWindowFlags_NavFlattened
    );

    for (auto& trie : file.ini)
    {
      if (! trie._show)
        continue;

      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), trie.section);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (200.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase), trie.key);
      ImGui::SameLine        ( );
      ImGui::ItemSize        (ImVec2 (600.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine        ( );
      trie.DrawFunction      (&trie);
    }

    ImGui::EndChild ( );

    file.bWindowFocused = ImGui::IsWindowFocused (ImGuiFocusedFlags_ChildWindows);
    if (file.bWindowFocused && ! g_activeKeybindPopup)
    {
      // Hotkey: Escape
      if (ImGui::IsKeyPressed (ImGuiKey_Escape))
        show = false;

      // Hotkey: Ctrl+S
      if (ImGui::GetIO().KeyCtrl && ImGui::GetKeyData (ImGuiKey_S)->DownDuration == 0.0f)
        save = true;
    }

    ImGui::End      ( );

    if (reset)
    {
      for (auto& trie : file.ini)
        trie.Reset();
    }

    if (save)
    {
      inih::INIReader new_ini = { };

      for (auto& trie : file.ini)
        new_ini.InsertEntry (trie.section, trie.key, trie.value);

      inih::INIWriter::write (file.path, new_ini, true);
    }

    if (! show)
    {
      file.state = PopupState_Closed;
      cleanup = true;
    }
  }

  if (cleanup)
  {
    std::vector <__INIFile> new_vector;
    for (auto& file : vIniEditor_Files)
    {
      if (file.state != PopupState_Closed)
        new_vector.push_back (file);
    }
    vIniEditor_Files = new_vector;
  }

  INIEditorActive = false;
  for (auto& file : vIniEditor_Files)
    INIEditorActive = (INIEditorActive || file.bWindowFocused);

  if (INIEditorActive)
  {
    extern bool allowShortcutCtrlA;
    allowShortcutCtrlA = false;

  }
}

//
// Copyright 2020 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <Windows.h>

#ifndef SECURITY_WIN32 
#define SECURITY_WIN32 
#endif

#include <Security.h>
#include <secext.h>
#include <userenv.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <unordered_map>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Userenv.lib")

std::string
SK_WideCharToUTF8 (const std::wstring& in)
{
  // CC BY-SA 4.0: https://stackoverflow.com/a/59617138
  int count = 
    WideCharToMultiByte (CP_UTF8, 0, in.c_str(), static_cast <int> (in.length()), NULL, 0, NULL, NULL);
  std::string out       (count, 0);
  WideCharToMultiByte   (CP_UTF8, 0, in.c_str(), -1, &out[0], count, NULL, NULL);


  return out;
}

std::wstring
SK_UTF8ToWideChar (const std::string& in)
{
  // CC BY-SA 4.0: https://stackoverflow.com/a/59617138
  int count = 
    MultiByteToWideChar (CP_UTF8, 0, in.c_str(), static_cast <int> (in.length()), NULL, 0);
  std::wstring out      (count, 0);
  MultiByteToWideChar   (CP_UTF8, 0, in.c_str(), static_cast <int> (in.length()), &out[0], count);

  return out;
}

void
SK_StripLeadingSlashesA (char *szInOut)
{
  auto IsSlash = [](char a) -> bool {
    return (a == '\\' || a == '/');
  };

  size_t      len = strlen (szInOut);
  size_t  new_len = len;

  char* szStart = szInOut;

  while (          *szStart  != '\0' &&
        *CharNextA (szStart) != '\0' )
  {
    if (IsSlash (*szStart))
    {
      szStart =
        CharNextA (szStart);

      --new_len;
    }

    else
      break;
  }

  if (len != new_len)
  {
    char *szOut = szInOut;
    char *szIn  = szStart;

    for ( size_t i = 0 ; i < new_len ; ++i )
    {
      *szOut =           *szIn;

      if (*szOut == '\0')
        break;

       szIn  = CharNextA (szIn);
       szOut = CharNextA (szOut);
    }
  }
}

void
SK_StripTrailingSlashesA (char* szInOut)
{
  auto IsSlash = [](char a) -> bool {
    return (a == '\\' || a == '/');
  };

  char* szNextUnique = szInOut + 1;
  char* szNext       = szInOut;

  while (*szNext != '\0')
  {
    if (*szNextUnique == '\0')
    {
      *CharNextA (szNext) = '\0';
      break;
    }

    if (IsSlash (*szNext))
    {
      if (IsSlash (*szNextUnique))
      {
        szNextUnique =
          CharNextA (szNextUnique);

        continue;
      }
    }

    ++szNext;
     *szNext = *szNextUnique;
                szNextUnique =
     CharNextA (szNextUnique);
  }
}

void
SK_FixSlashesA (char* szInOut)
{
  if (szInOut == nullptr)
    return;

  char*   pszInOut  = szInOut;
  while (*pszInOut != '\0')
  {
    if (*pszInOut == '/')
        *pszInOut = '\\';

    pszInOut =
      CharNextA (pszInOut);
  }
}

void
SK_StripLeadingSlashesW (wchar_t *wszInOut)
{
  auto IsSlash = [](wchar_t a) -> bool {
    return (a == L'\\' || a == L'/');
  };

  size_t      len = wcslen (wszInOut);
  size_t  new_len = len;

  wchar_t* wszStart = wszInOut;

  while (          *wszStart  != L'\0' &&
        *CharNextW (wszStart) != L'\0' )
  {
    if (IsSlash (*wszStart))
    {
      wszStart =
        CharNextW (wszStart);

      --new_len;
    }

    else
      break;
  }

  if (len != new_len)
  {
    wchar_t *wszOut = wszInOut;
    wchar_t *wszIn  = wszStart;

    for ( size_t i = 0 ; i < new_len ; ++i )
    {
      *wszOut =           *wszIn;

      if (*wszOut == L'\0')
        break;

       wszIn  = CharNextW (wszIn);
       wszOut = CharNextW (wszOut);
    }
  }

  // Else:
}
//
// In-place version of the old code that had to
//   make a copy of the string and then copy-back
//
void
SK_StripTrailingSlashesW (wchar_t* wszInOut)
{
  //wchar_t* wszValidate = wcsdup (wszInOut);

  auto IsSlash = [](wchar_t a) -> bool {
    return (a == L'\\' || a == L'/');
  };

  wchar_t* wszNextUnique = CharNextW (wszInOut);
  wchar_t* wszNext       = wszInOut;

  while (*wszNext != L'\0')
  {
    if (*wszNextUnique == L'\0')
    {
      *CharNextW (wszNext) = L'\0';
      break;
    }

    if (IsSlash (*wszNext))
    {
      if (IsSlash (*wszNextUnique))
      {
        wszNextUnique =
          CharNextW (wszNextUnique);

        continue;
      }
    }

    wszNext = CharNextW (wszNext);
   *wszNext = *wszNextUnique;
    wszNextUnique =
      CharNextW (wszNextUnique);
  }
}

void
SK_FixSlashesW (wchar_t* wszInOut)
{
  if (wszInOut == nullptr)
    return;

  wchar_t* pwszInOut  = wszInOut;
  while ( *pwszInOut != L'\0' )
  {
    if (*pwszInOut == L'/')
        *pwszInOut = L'\\';

    pwszInOut =
      CharNextW (pwszInOut);
  }
}

std::string
__cdecl
SK_FormatString (char const* const _Format, ...)
{
  size_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( nullptr, 0, _Format, _ArgList ) + 1ui64;
  }
  va_end   (_ArgList);

  size_t alloc_size =
    sizeof (char) * (len + 2);

  std::unique_ptr <char []> pData =
    std::make_unique <char []> (alloc_size);

  if (! pData)
    return std::string ();

  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( pData.get (), len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    pData.get ();
}

std::wstring
__cdecl
SK_FormatStringW (wchar_t const* const _Format, ...)
{
  size_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ( nullptr, 0, _Format, _ArgList ) + 1ui64;
  }
  va_end   (_ArgList);

  size_t alloc_size =
    sizeof (wchar_t) * (len + 2);

  std::unique_ptr <wchar_t []> pData =
    std::make_unique <wchar_t []> (alloc_size);

  if (! pData)
    return std::wstring ();

  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ( (wchar_t *)pData.get (), len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    pData.get ();
}

size_t
SK_RemoveTrailingDecimalZeros (char* szNum, size_t bufLen)
{
  if (szNum == nullptr)
    return 0;

  // Remove trailing 0's after the .
  size_t len = bufLen == 0 ?
                  strlen (szNum) :
        std::min (strlen (szNum), bufLen);

  for (size_t i = (len - 1); i > 1; i--)
  {
    if (szNum [i] == '0' && szNum [i - 1] != '.')
      len--;

    if (szNum [i] != '0' && szNum [i] != '\0')
      break;
  }

  szNum [len] = '\0';

  return len;
}


// Keybindings

using wstring_hash = size_t;

std::unordered_map <wstring_hash, BYTE> humanKeyNameToVirtKeyCode;
std::unordered_map <BYTE, wchar_t [32]> virtKeyCodeToHumanKeyName;
std::unordered_map <BYTE, wchar_t [32]> virtKeyCodeToFullyLocalizedKeyName;

static auto& humanToVirtual = humanKeyNameToVirtKeyCode;
static auto& virtualToHuman = virtKeyCodeToHumanKeyName;
static auto& virtualToLocal = virtKeyCodeToFullyLocalizedKeyName;

constexpr UINT
SK_MakeKeyMask ( const SHORT vKey,
                 const bool  ctrl,
                 const bool  shift,
                 const bool  alt,
                 const bool  super )
{
  return
    static_cast <UINT> (
      ( vKey | ( (ctrl  != 0) <<  9 ) |
               ( (shift != 0) << 10 ) |
               ( (alt   != 0) << 11 ) |
               ( (super != 0) << 12 ))
    );
}
 
char* SK_CharNextA (const char *szInput, int n = 1);

static inline wchar_t*
SK_CharNextW (const wchar_t *wszInput, size_t n = 1)
{
  if (n <= 0 || wszInput == nullptr) [[unlikely]]
    return nullptr;

  return
    const_cast <wchar_t *> (wszInput + n);
};

static inline wchar_t*
SK_CharPrevW (const wchar_t *start, const wchar_t *x)
{
  if (x > start) return const_cast <wchar_t *> (x - 1);
  else           return const_cast <wchar_t *> (x);
}

void
SK_KeyMap_StandardizeNames (wchar_t* wszNameToFormalize)
{
  if (wszNameToFormalize == nullptr)
    return;

  wchar_t*                  pwszName = wszNameToFormalize;
                CharUpperW (pwszName);
   pwszName = SK_CharNextW (pwszName);

  bool lower = true;

  while (*pwszName != L'\0')
  {
    if (lower) CharLowerW (pwszName);
    else       CharUpperW (pwszName);

    lower =
      (! iswspace (*pwszName));

    pwszName =
      SK_CharNextW (pwszName);
  }
}

void
SK_Keybind::update (void)
{
  init();

  human_readable     .clear ();
  human_readable_utf8.clear ();

  wchar_t* key_name =
    (virtKeyCodeToHumanKeyName)[(BYTE)(vKey & 0xFF)];

  if (*key_name == L'\0')
  {
    ctrl                = false;
    alt                 = false;
    shift               = false;
    super               = false;
    masked_code         = 0x0;
    human_readable      = L"<Not Bound>";
    human_readable_utf8 =  "<Not Bound>";

    return;
  }

  std::queue <std::wstring> words;

  if (ctrl)
    words.emplace (L"Ctrl");

  if (super)
    words.emplace (L"Windows");

  if (alt)
    words.emplace (L"Alt");

  if (shift)
    words.emplace (L"Shift");

  words.emplace (key_name);

  while (! words.empty ())
  {
    human_readable += words.front ();
    words.pop ();

    if (! words.empty ())
      human_readable += L"+";
  }

  masked_code =
    SK_MakeKeyMask (vKey & 0xFFU, ctrl, shift, alt, super);

  human_readable_utf8 = SK_WideCharToUTF8 (human_readable);
}

void
SK_Keybind::parse (void)
{
  init();

  vKey  = 0x0;
  ctrl  = false;
  alt   = false;
  shift = false;
  super = false;

  wchar_t   wszKeyBind [128] = { };
  lstrcatW (wszKeyBind, human_readable.c_str ());

  wchar_t* wszBuf = nullptr;
  wchar_t* wszTok = std::wcstok (wszKeyBind, L"+", &wszBuf);

  if (wszTok == nullptr)
  {
    if (*wszKeyBind != L'\0')
    {
      SK_KeyMap_StandardizeNames (wszKeyBind);

      if (*wszKeyBind != L'\0')
      {
        vKey =
          humanToVirtual [hash_string (wszKeyBind)];
      }
    }
  }

  while (wszTok != nullptr)
  {
    SK_KeyMap_StandardizeNames (wszTok);

    if (*wszTok != L'\0')
    {
      BYTE vKey_ =
        humanToVirtual [hash_string (wszTok)];

      if (     vKey_ == VK_CONTROL || vKey_ == VK_LCONTROL || vKey_ == VK_RCONTROL)
        ctrl  = true;
      else if (vKey_ == VK_SHIFT   || vKey_ == VK_LSHIFT   || vKey_ == VK_RSHIFT)
        shift = true;
      else if (vKey_ == VK_MENU    || vKey_ == VK_LMENU    || vKey_ == VK_RMENU)
        alt   = true;
      else if (vKey_ == VK_LWIN    || vKey_ == VK_RWIN)
        super = true;
      else
        vKey = vKey_;
    }

    wszTok =
      std::wcstok (nullptr, L"+", &wszBuf);
  }

  masked_code =
    SK_MakeKeyMask (vKey & 0xFFU, ctrl, shift, alt, super);

  human_readable_utf8 = SK_WideCharToUTF8 (human_readable);
}

void
SK_Keybind::init (void)
{
  static bool init = false;

  if (init)
    return;

  init = true;

  static const auto _PushVirtualToHuman =
  [] (BYTE vKey_, const wchar_t* wszHumanName)
  {
    if (! wszHumanName)
      return;

    auto& pair_builder =
      virtualToHuman [vKey_];

    wcsncpy_s ( pair_builder, 32,
                wszHumanName, _TRUNCATE );
  };

  static const auto _PushVirtualToLocal =
  [] (BYTE vKey_, const wchar_t* wszHumanName)
  {
    if (! wszHumanName)
      return;

    auto& pair_builder =
      virtualToLocal [vKey_];

    wcsncpy_s ( pair_builder, 32,
                wszHumanName, _TRUNCATE );
  };

  static const auto _PushHumanToVirtual =
  [] (const wchar_t* wszHumanName, BYTE vKey_)
  {
    if (! wszHumanName)
      return;

    humanToVirtual.emplace (
      hash_string (wszHumanName),
        vKey_
    );
  };

  for (int i = 0; i < 0xFF; i++)
  {
    wchar_t name [32] = { };

    switch (i)
    {
      case VK_F1:          wcscat (name, L"F1");           break;
      case VK_F2:          wcscat (name, L"F2");           break;
      case VK_F3:          wcscat (name, L"F3");           break;
      case VK_F4:          wcscat (name, L"F4");           break;
      case VK_F5:          wcscat (name, L"F5");           break;
      case VK_F6:          wcscat (name, L"F6");           break;
      case VK_F7:          wcscat (name, L"F7");           break;
      case VK_F8:          wcscat (name, L"F8");           break;
      case VK_F9:          wcscat (name, L"F9");           break;
      case VK_F10:         wcscat (name, L"F10");          break;
      case VK_F11:         wcscat (name, L"F11");          break;
      case VK_F12:         wcscat (name, L"F12");          break;
      case VK_F13:         wcscat (name, L"F13");          break;
      case VK_F14:         wcscat (name, L"F14");          break;
      case VK_F15:         wcscat (name, L"F15");          break;
      case VK_F16:         wcscat (name, L"F16");          break;
      case VK_F17:         wcscat (name, L"F17");          break;
      case VK_F18:         wcscat (name, L"F18");          break;
      case VK_F19:         wcscat (name, L"F19");          break;
      case VK_F20:         wcscat (name, L"F20");          break;
      case VK_F21:         wcscat (name, L"F21");          break;
      case VK_F22:         wcscat (name, L"F22");          break;
      case VK_F23:         wcscat (name, L"F23");          break;
      case VK_F24:         wcscat (name, L"F24");          break;
      case VK_SNAPSHOT:    wcscat (name, L"Print Screen"); break;
      case VK_SCROLL:      wcscat (name, L"Scroll Lock");  break;
      case VK_PAUSE:       wcscat (name, L"Pause Break");  break;

      default:
      {
        unsigned int scanCode =
          ( MapVirtualKey (i, 0) & 0xFFU );
        unsigned short int temp      =  0;

        bool asc = (i <= 32);

        if (! asc && i != VK_DIVIDE)
        {
                                    BYTE buf [256] = { };
            asc = ToAscii ( i, scanCode, buf, &temp, 1 );
        }

        scanCode             <<= 16U;
        scanCode   |= ( 0x1U <<  25U  );

        if (! asc)
          scanCode |= ( 0x1U << 24U   );

        GetKeyNameText ( scanCode,
                            name,
                              32 );

        SK_KeyMap_StandardizeNames (name);
      } break;
    }


    if ( i != VK_CONTROL  && i != VK_MENU     &&
          i != VK_SHIFT    && i != VK_OEM_PLUS && i != VK_OEM_MINUS &&
          i != VK_LSHIFT   && i != VK_RSHIFT   &&
          i != VK_LCONTROL && i != VK_RCONTROL &&
          i != VK_LMENU    && i != VK_RMENU    && i != VK_ADD   && // Num Plus
          i != VK_BACK     && i != VK_HOME     && i != VK_END   &&
          i != VK_DELETE   && i != VK_INSERT   && i != VK_PRIOR &&
          i != VK_NEXT     && i != VK_LWIN     && i != VK_RWIN
        )
    {
      _PushHumanToVirtual (name, static_cast <BYTE> (i));
      _PushVirtualToHuman (      static_cast <BYTE> (i), name);
    }

    _PushVirtualToLocal   (      static_cast <BYTE> (i), name);
  }

  _PushHumanToVirtual (L"Plus",          static_cast <BYTE> (VK_OEM_PLUS));
  _PushHumanToVirtual (L"Minus",         static_cast <BYTE> (VK_OEM_MINUS));
  _PushHumanToVirtual (L"Ctrl",          static_cast <BYTE> (VK_CONTROL));
  _PushHumanToVirtual (L"Alt",           static_cast <BYTE> (VK_MENU));
  _PushHumanToVirtual (L"Shift",         static_cast <BYTE> (VK_SHIFT));
  _PushHumanToVirtual (L"Left Shift",    static_cast <BYTE> (VK_LSHIFT));
  _PushHumanToVirtual (L"Right Shift",   static_cast <BYTE> (VK_RSHIFT));
  _PushHumanToVirtual (L"Left Alt",      static_cast <BYTE> (VK_LMENU));
  _PushHumanToVirtual (L"Right Alt",     static_cast <BYTE> (VK_RMENU));
  _PushHumanToVirtual (L"Left Ctrl",     static_cast <BYTE> (VK_LCONTROL));
  _PushHumanToVirtual (L"Right Ctrl",    static_cast <BYTE> (VK_RCONTROL));
  _PushHumanToVirtual (L"Backspace",     static_cast <BYTE> (VK_BACK));
  _PushHumanToVirtual (L"Home",          static_cast <BYTE> (VK_HOME));
  _PushHumanToVirtual (L"End",           static_cast <BYTE> (VK_END));
  _PushHumanToVirtual (L"Insert",        static_cast <BYTE> (VK_INSERT));
  _PushHumanToVirtual (L"Delete",        static_cast <BYTE> (VK_DELETE));
  _PushHumanToVirtual (L"Page Up",       static_cast <BYTE> (VK_PRIOR));
  _PushHumanToVirtual (L"Page Down",     static_cast <BYTE> (VK_NEXT));
  _PushHumanToVirtual (L"Windows",       static_cast <BYTE> (VK_LWIN)); // Left Windows (Super), but just refer to it as Windows
  _PushHumanToVirtual (L"Right Windows", static_cast <BYTE> (VK_RWIN));

  _PushVirtualToHuman (static_cast <BYTE> (VK_CONTROL),   L"Ctrl");
  _PushVirtualToHuman (static_cast <BYTE> (VK_MENU),      L"Alt");
  _PushVirtualToHuman (static_cast <BYTE> (VK_SHIFT),     L"Shift");
  _PushVirtualToHuman (static_cast <BYTE> (VK_OEM_PLUS),  L"Plus");
  _PushVirtualToHuman (static_cast <BYTE> (VK_OEM_MINUS), L"Minus");
  _PushVirtualToHuman (static_cast <BYTE> (VK_LSHIFT),    L"Left Shift");
  _PushVirtualToHuman (static_cast <BYTE> (VK_RSHIFT),    L"Right Shift");
  _PushVirtualToHuman (static_cast <BYTE> (VK_LMENU),     L"Left Alt");
  _PushVirtualToHuman (static_cast <BYTE> (VK_RMENU),     L"Right Alt");
  _PushVirtualToHuman (static_cast <BYTE> (VK_LCONTROL),  L"Left Ctrl");
  _PushVirtualToHuman (static_cast <BYTE> (VK_RCONTROL),  L"Right Ctrl");
  _PushVirtualToHuman (static_cast <BYTE> (VK_BACK),      L"Backspace");
  _PushVirtualToHuman (static_cast <BYTE> (VK_HOME),      L"Home");
  _PushVirtualToHuman (static_cast <BYTE> (VK_END),       L"End");
  _PushVirtualToHuman (static_cast <BYTE> (VK_INSERT),    L"Insert");
  _PushVirtualToHuman (static_cast <BYTE> (VK_DELETE),    L"Delete");
  _PushVirtualToHuman (static_cast <BYTE> (VK_PRIOR),     L"Page Up");
  _PushVirtualToHuman (static_cast <BYTE> (VK_NEXT),      L"Page Down");
  _PushVirtualToHuman (static_cast <BYTE> (VK_LWIN),      L"Windows"); // Left Windows (Super), but just refer to it as Windows
  _PushVirtualToHuman (static_cast <BYTE> (VK_RWIN),      L"Right Windows");

  _PushHumanToVirtual (L"Num Plus", static_cast <BYTE> (VK_ADD));
  _PushVirtualToHuman (             static_cast <BYTE> (VK_ADD), L"Num Plus");
}

bool
SK_ImGui_KeybindSelect (SK_Keybind* keybind, const char* szLabel)
{
  std::ignore = keybind;

  bool ret = false;

  ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.667f, 0.667f, 0.667f, 1.0f));
  ImGui::PushItemWidth  (ImGui::GetContentRegionAvail ().x);

  ret =
    ImGui::Selectable (szLabel, false);

  ImGui::PopItemWidth  ();
  ImGui::PopStyleColor ();

  return ret;
}

//SK_API
void
__stdcall
SK_ImGui_KeybindDialog (SK_Keybind* keybind)
{
  if (! keybind)
    return;

  auto& io =
    ImGui::GetIO ();

  const  float font_size = ImGui::GetFont ()->FontSize * io.FontGlobalScale;

  if (ImGui::IsPopupOpen (keybind->bind_name))
  {
    ImGui::SetNextWindowSizeConstraints ( ImVec2 (font_size *  9.0f, font_size * 3.0f),
                                          ImVec2 (font_size * 30.0f, font_size * 6.0f) );

    extern ImRect windowRect;
    ImGui::SetNextWindowPos  (windowRect.GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

    // Render over all other windows
    //ImGui::SetNextWindowFocus ( );
  }

  if (ImGui::BeginPopupModal (keybind->bind_name, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_Tooltip | // ImGuiWindowFlags_Tooltip is required to work around a pesky z-order issue on first appearance
                                                           ImGuiWindowFlags_NoCollapse       | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
  {
		// Render over all other windows
		//ImGui::BringWindowToDisplayFront (ImGui::GetCurrentWindow ( ));

    int  i = 0;
    for (i = 0x08; i < 256; i++)
    {
      if ( i == VK_LCONTROL || i == VK_RCONTROL || i == VK_CONTROL ||
           i == VK_LSHIFT   || i == VK_RSHIFT   || i == VK_SHIFT   ||
           i == VK_LMENU    || i == VK_RMENU    || i == VK_MENU    ||
           i == VK_LWIN     || i == VK_RWIN )
        continue;

      extern
          ImGuiKey       ImGui_ImplWin32_VirtualKeyToImGuiKey (WPARAM wParam);
      if (ImGuiKey key = ImGui_ImplWin32_VirtualKeyToImGuiKey (i))
      {
        if (ImGui::IsKeyPressed (key, false))
          break;
      }
    }

#if 0
    ImGuiKey key = ImGuiKey_None;
    for (key = ImGuiKey_KeysData_OFFSET; key < ImGuiKey_COUNT; key = (ImGuiKey)(key + 1))
    {

      if ( key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl  ||
           key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
           key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt   ||
           key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper )
        /* key == ImGuiKey_Menu */ // VK_APPS
        continue;

      if (ImGui::IsKeyDown (key))
        break;
    }
#endif

    bool bEscape    =
      ImGui::IsKeyPressed (ImGuiKey_Escape,    false),
         bBackspace =
      ImGui::IsKeyPressed (ImGuiKey_Backspace, false);

    ImGui::Text        ("Keybinding:  %hs (0x%02X)", keybind->human_readable_utf8.c_str (), keybind->vKey);
    ImGui::Separator   (                            );

    ImGui::TextColored (ImVec4 (0.6f, 0.6f, 0.6f, 1.f),
                        "Press BACKSPACE to clear, or ESC to cancel.");

    // Update the key binding after printing out the current one, to prevent a one-frame graphics glitch

    if (bBackspace)
    {
      keybind->vKey  = 0;
      keybind->ctrl  = false;
      keybind->shift = false;
      keybind->alt   = false;
      keybind->super = false;

      keybind->update ();
    }

    else if (i != 256 && ! bEscape) // key != ImGuiKey_COUNT
    {
      keybind->vKey  = static_cast <SHORT> (i);

      keybind->ctrl  = io.KeyCtrl;
      keybind->shift = io.KeyShift;
      keybind->alt   = io.KeyAlt;
      keybind->super = io.KeySuper;

      keybind->update ();
    }

    if (bEscape || bBackspace)
      ImGui::CloseCurrentPopup ();

    ImGui::EndPopup ();
  }
}

bool
SK_ImGui_Keybinding (SK_Keybind* binding)
{
  if (SK_ImGui_KeybindSelect (binding, binding->human_readable_utf8.c_str ()))
    ImGui::OpenPopup (        binding->bind_name);

  std::string original_binding = binding->human_readable_utf8;

  SK_ImGui_KeybindDialog (binding);

  return (original_binding != binding->human_readable_utf8);
};
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

#pragma once

#include <string>

struct sk_install_state_s {
  struct Injection {
    enum class Bitness {
      ThirtyTwo = 0x1,
      SixtyFour = 0x2,
      Unknown   = 0x0
    }            bitness = Bitness::Unknown;
    enum class EntryPoint {
      D3D9    = 0x1,
      D3D11   = 0x2,
      DXGI    = 0x4,
      OpenGL  = 0x8,
      DInput8 = 0x10,
      CBTHook = 0x20,
      Unknown = 0x0
    }            entry_pt = EntryPoint::Unknown;
    enum class Type {
      Global  = 0x1,
      Local   = 0x2,
      Unknown = 0x0
    }            type     = Type::Unknown;
    std::wstring dll_path = L"";
    std::wstring dll_ver  = L"";
  } injection;

  struct Config {
    enum class Type {
      Centralized = 0x1,
      Localized   = 0x2,
      Unknown     = 0x0
    }            type = Type::Unknown;
    std::wstring dir  = L"";
    std::wstring file = L"";
  } config;

  std::string    localized_name; // UTF-8
};

using InjectionBitness =
  sk_install_state_s::Injection::Bitness;
using InjectionPoint =
  sk_install_state_s::Injection::EntryPoint;
using InjectionType =
  sk_install_state_s::Injection::Type;
using ConfigType =
  sk_install_state_s::Config::Type;

std::wstring
SKIF_GetSpecialKDLLVersion (const wchar_t* wszName);

sk_install_state_s
SKIF_InstallUtils_GetInjectionStrategy (uint32_t appid);
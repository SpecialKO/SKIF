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

#include <utility/games.h>
#include <stores/Steam/app_record.h>

void SKIF_UI_Tab_DrawLibrary (void);


// Cached struct used to hold calculated data across frames
struct SKIF_Lib_SummaryCache
{
  struct {
    std::string   type;
    std::string   type_version;
    struct {
      std::string text;
      ImColor     color;
      ImColor     color_hover;
    } status;
    std::string   hover_text;
  } injection;

  std::string config_repo;

  struct {
    std::wstring shorthandW;
    std::string  shorthand; // Converted to utf-8 from utf-16
    std::wstring root_dirW;
    std::string  root_dir;  // Converted to utf-8 from utf-16
    std::wstring full_pathW;
    std::string  full_path; // Converted to utf-8 from utf-16
  } config;

  struct {
    std::wstring shorthandW;
    std::string  shorthand; // Converted to utf-8 from utf-16
    std::wstring versionW;
    std::string  version;   // Converted to utf-8 from utf-16
    std::wstring full_pathW;
    std::string  full_path; // Converted to utf-8 from utf-16
  } dll;

  AppId_t     app_id   = 0;
  DWORD       running  = 0;
  bool        service  = false;
  bool        autostop = false;

  void Refresh (app_record_s* pApp);
  
  // Functions
  static SKIF_Lib_SummaryCache& GetInstance (void)
  {
      static SKIF_Lib_SummaryCache instance;
      return instance;
  }

  SKIF_Lib_SummaryCache (SKIF_Lib_SummaryCache const&) = delete; // Delete copy constructor
  SKIF_Lib_SummaryCache (SKIF_Lib_SummaryCache&&)      = delete; // Delete move constructor

private:
  SKIF_Lib_SummaryCache (void) { }; // Do nothing
};
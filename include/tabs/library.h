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
  // Cached data
  struct CloudPath
  {
    std::wstring path;
    std:: string path_utf8;
    std:: string label;

    CloudPath (int i, std::wstring p)
    {
      path      = p;
      path_utf8 = SK_WideCharToUTF8 (p);
      label     =
        SK_FormatString ("%s###CloudUFS.%d", path_utf8.c_str(), i);
    };
  };

  using branch_ptr_t =
    std::pair <          std::string*,
        app_record_s::branch_record_s* >;

  // This struct holds the cache for the right click context menu
  struct {
    int  numSecondaryLaunchConfigs = 0; // Secondary launch options
    bool profileFolderExists       = false;
    bool screenshotsFolderExists   = false;
    std::wstring wsScreenshotDir   = L"";
    std::vector   <CloudPath> cloud_paths; // Steam Auto-Cloud
    std::multimap <int64_t, branch_ptr_t> branches; // Steam Branches

    // PCGamingWiki
    std::wstring pcgwValue;
    std::wstring pcgwLink;
  } menu;

  enum class CachedType {
    Global  = 0x1,
    Local   = 0x2,
    Unknown = 0x0
  };

  struct {
    CachedType    type = CachedType::Unknown;
    std::string   type_utf8;
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
    std::wstring shorthand;
    std::string  shorthand_utf8; // Converted to utf-8 from utf-16
    std::wstring root_dir;
    std::string  root_dir_utf8;  // Converted to utf-8 from utf-16
    std::wstring full_path;
    std::string  full_path_utf8; // Converted to utf-8 from utf-16
  } config;

  struct {
    std::wstring shorthand;
    std::string  shorthand_utf8; // Converted to utf-8 from utf-16
    std::wstring version;
    std::string  version_utf8;   // Converted to utf-8 from utf-16
    std::wstring full_path;
    std::string  full_path_utf8; // Converted to utf-8 from utf-16
  } dll;

  AppId_t     app_id   = 0;
  DWORD       running  = 0;
  DWORD       updating = 0;
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
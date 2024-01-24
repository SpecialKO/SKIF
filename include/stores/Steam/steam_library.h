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

#include <Windows.h>
#include <vector>
#include <stack>

#include <stores/Steam/app_record.h>
#include "utility/sk_utility.h"

//#include "steam/steam_api.h"
#include <utility/vfs.h>
#include <stores/Steam/vdf.h>


extern
  std::unique_ptr <skValveDataFile> appinfo;

using steam_library_t = wchar_t [MAX_PATH + 2];

struct SK_Steam_Depot {
  std::string  name; // Has to be queried w/ WebAPI
  DepotId_t    depot;
  ManifestId_t manifest;
};


// Barely functional Steam Key/Value Parser
//   -> Does not handle unquoted kv pairs.
//   -> Does also not handle a hanging { that lacks a closing } (worked around by counting the depth we end up on)
class SK_Steam_KeyValues
{
public:
  static
  std::vector <std::string>
  getKeys ( const std::string               &input,
            const std::deque  <std::string> &sections,
                  std::vector <std::string> *values = nullptr )
  {
    std::vector <std::string> ret;

    if (sections.empty () || input.empty ())
      return ret;

    // TODO: Fix proper solution to the below
    // This is a halfassed way of ensuring there's a matching set of { and }
    // ---
    int depth = 0;

    for (auto c : input)
    {
      if (c == '{')
        depth++;
      if (c == '}' && depth > 0)
        depth--;
    }

    if (depth != 0)
      return ret;
    // ---

    struct {
      std::deque <std::string> path;

      struct {
        std::string actual;
        std::string test;
      } heap;

      void heapify (std::deque <std::string> const *sections = nullptr)
      {
        int i = 0;

        auto& in  = (sections == nullptr) ? path        : *sections;
        auto& out = (sections == nullptr) ? heap.actual : heap.test;

        out = "";

        for ( auto& str : in )
        {
          if (i++ > 0)
            out += "\x01";

          out += str;
        }
      }
    } search_tree;

    search_tree.heapify (&sections);

    std::string name   = "";
    std::string value  = "";
    int         quotes = 0;

    const auto _clear = [&](void)
    {
      name.clear  ();
      value.clear ();
      quotes = 0;
    };

    for (auto c : input)
    {
      if (c == '"')
        ++quotes;

      else if (c != '{')
      {
        if (quotes == 1)
        {
          name += c;
        }

        if (quotes == 3)
        {
          value += c;
        }
      }

      if (quotes == 4)
      {
        if (! _stricmp ( search_tree.heap.test.c_str   (),
                         search_tree.heap.actual.c_str () ) )
        {
          ret.emplace_back (name);

          if (values != nullptr)
            values->emplace_back (value);
        }

        _clear ();
      }

      if (c == '{')
      {
        search_tree.path.push_back (name);
        search_tree.heapify        (    );

        _clear ();
      }

      else if (c == '}')
      {
        if (! search_tree.path.empty())
          search_tree.path.pop_back ();
        else
          PLOG_ERROR << "Corrupt manifest detected!";

        _clear ();
      }

      else { search_tree.heapify (); } // Needed to be able to handle key/value pairs placed after { } objects
    }

    return ret;
  }

  static
  std::string
  getValue ( const std::string              &input,
             const std::deque <std::string> &sections,
             const std::string              &key )
  {
    std::vector <std::string> values;
    std::vector <std::string> keys (
      SK_Steam_KeyValues::getKeys (input, sections, &values)
    );

    int idx = 0;

    for ( auto& it : keys )
    {
      if (it._Equal (key))
        return values [idx];

      ++idx;
    }

    return "";
  }

  static
  std::wstring
  getValueAsUTF16 ( const std::string              &input,
                    const std::deque <std::string> &sections,
                    const std::string              &key )
  {
    std::vector <std::string> values;
    std::vector <std::string> keys (
      SK_Steam_KeyValues::getKeys (input, sections, &values)
    );

    int idx = 0;

    for ( auto& it : keys )
    {
      if (it._Equal (key))
      {
        return
          SK_UTF8ToWideChar (
            values [idx]
          );
      }

      ++idx;
    }

    return L"";
  }
};

int                          SK_VFS_ScanTree (SK_VirtualFS::vfsNode* pVFSRoot,
                                                            wchar_t* wszDir,
                                                            wchar_t* wszPattern        = L"*",
                                                                int  max_depth         = 1,
                                                                int      depth         = 0,
                                              SK_VirtualFS::vfsNode* pVFSImmutableRoot = nullptr
);
int                          SK_Steam_GetLibraries               (steam_library_t **ppLibraries);
std::vector <AppId_t>        SK_Steam_GetInstalledAppIDs         (void);
std::wstring                 SK_Steam_GetApplicationManifestPath (app_record_s *app);
std::wstring                 SK_Steam_GetLocalConfigPath         (SteamId3_t userid);
std::string                  SK_GetManifestContentsForAppID      (app_record_s *app);
std::string                  SK_GetLocalConfigForSteamUser       (SteamId3_t userid);
const wchar_t *              SK_GetSteamDir                      (void);
std::wstring                 SK_UseManifestToGetInstallDir       (app_record_s *app);
std::string                  SK_UseManifestToGetAppName          (app_record_s *app);
std::string                  SK_UseManifestToGetCurrentBranch    (app_record_s *app);
std::string                  SK_UseManifestToGetAppOwner         (app_record_s *app);
std::vector <SK_Steam_Depot> SK_UseManifestToGetDepots           (app_record_s *app);
ManifestId_t                 SK_UseManifestToGetDepotManifest    (app_record_s *app, DepotId_t depot);
std::string                  SKIF_Steam_GetLaunchOptions         (AppId_t appid, SteamId3_t userid, app_record_s *app = nullptr);
bool                         SKIF_Steam_PreloadUserLocalConfig   (SteamId3_t userid, std::vector <std::pair <std::string, app_record_s> > *apps, std::set <std::string> *apptickets);
bool                         SKIF_Steam_isSteamOverlayEnabled    (AppId_t appid, SteamId3_t userid);
bool                         SKIF_Steam_areLibrariesSignaled     (void);
void                         SKIF_Steam_GetInstalledAppIDs       (std::vector <std::pair < std::string, app_record_s > > *apps);
bool                         SKIF_Steam_HasActiveProcessChanged  (std::vector <std::pair < std::string, app_record_s > > *apps, std::set <std::string> *apptickets);
SteamId3_t                   SKIF_Steam_GetCurrentUser           (void);
DWORD                        SKIF_Steam_GetActiveProcess         (void);
void                         SKIF_Steam_GetInjectionStrategy     (app_record_s* pApp);
std::wstring                 SKIF_Steam_GetAppStateString        (AppId_t  appid, const wchar_t *wszStateKey);
wchar_t                      SKIF_Steam_GetAppStateDWORD         (AppId_t  appid, const wchar_t *wszStateKey, DWORD *pdwStateVal);
bool                         SKIF_Steam_UpdateAppState           (app_record_s *pApp);
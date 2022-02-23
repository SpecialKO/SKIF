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

#include "sk_utility/utility.h"

#include "steam/steam_api.h"
#include "vfs.h"

#include <vector>

using steam_library_t = wchar_t *[MAX_PATH * 2];

std::string
SK_UseManifestToGetAppName (AppId_t appid = 0);

struct SK_Steam_Depot {
  std::string  name; // Has to be queried w/ WebAPI
  DepotId_t    depot;
  ManifestId_t manifest;
};

std::vector <SK_Steam_Depot>
SK_UseManifestToGetDepots (AppId_t appid = 0);

ManifestId_t
SK_UseManifestToGetDepotManifest (AppId_t appid, DepotId_t depot);

extern const GUID IID_VFS_SteamUGC;

namespace SK_VFS_Steam
{
  class UGCFile : public SK_VirtualFS::File
  {
  public:
    AppId_t  target_app;
    uint64_t owner_id;
  };

  class WorkshopFile : public UGCFile
  {
  public:
    void *getSubclass (REFIID iid) override;

    std::vector <std::shared_ptr <WorkshopFile>>
      getRequiredFiles (void);

  protected:
    PublishedFileId_t              published_id;

    struct {
      std::set <PublishedFileId_t> files;
      std::set <AppId_t>           apps;
    } depends;

  private:
  };

  class UGC_RootFS : public SK_VirtualFS
  {
  public:
    std::shared_ptr <WorkshopFile> getPublishedFile (PublishedFileId_t id);

    std::shared_ptr <UGCFile> getUGCFile (UGCHandle_t handle);

  protected:
    std::unordered_map <PublishedFileId_t, std::shared_ptr <WorkshopFile>> pub_id_to_file;
    std::unordered_map <UGCHandle_t,       std::shared_ptr <UGCFile>>      ugc_handle_to_file;

  private:
  };

  //std::vector <std::shared_ptr <WorkshopFile>>
  //  WorkshopFile::getRequiredFiles (void);

  extern UGC_RootFS ugc_root;
};

extern SK_VirtualFS manifest_vfs;

int
SK_VFS_ScanTree (SK_VirtualFS::vfsNode *pVFSRoot,
  wchar_t *wszDir,
  int  max_depth = 1,
  int      depth = 0,
  SK_VirtualFS::vfsNode *pVFSImmutableRoot = nullptr);

int
SK_Steam_GetLibraries (steam_library_t **ppLibraries);

std::vector <AppId_t>
SK_Steam_GetInstalledAppIDs (void);

std::wstring
SK_Steam_GetApplicationManifestPath (AppId_t appid);

std::string
SK_GetManifestContentsForAppID (AppId_t appid);

#include <stack>

// Barely functional Steam Key/Value Parser
//   -> Does not handle unquoted kv pairs.
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
        search_tree.path.pop_back ();

        _clear ();
      }
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

    for ( auto it : keys )
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

    for ( auto it : keys )
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

const wchar_t *
SK_GetSteamDir (void);

std::string
SK_UseManifestToGetAppName (AppId_t appid);

std::wstring
SK_UseManifestToGetInstallDir (AppId_t appid);

std::string
SK_UseManifestToGetAppOwner (AppId_t appid)

std::vector <SK_Steam_Depot>
SK_UseManifestToGetDepots (AppId_t appid);

ManifestId_t
SK_UseManifestToGetDepotManifest (AppId_t appid, DepotId_t depot);
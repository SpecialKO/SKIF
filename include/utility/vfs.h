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

#include <memory>
#include <cwchar>
#include <string>
#include <set>
#include <stack>
#include <unordered_map>
#include <map>

// Steamworks API definitions
typedef unsigned __int32 uint32;
typedef unsigned __int64 uint64;
typedef uint32 AppId_t;
typedef uint32 SteamId3_t;
typedef uint32 DepotId_t;
typedef uint64 PublishedFileId_t;
typedef uint64 ManifestId_t;
typedef uint64 UGCHandle_t;

int SK_CountWChars (const wchar_t *s, wchar_t c);

class SK_VirtualFS
{
public:
  enum class type {
    Local,
    Remote,
    Mirror  // Auto-sync
  } type_ = type::Local;

  class File;
  class Directory;

  class vfsNode
  {
  public:
    enum class type {
      File,
      Directory,
      Alias,
      Unknown
    } type_ = type::Unknown;

    struct {
      FILETIME modified;
      FILETIME created;
    } time = { };

    std::wstring        name   = L"";
    size_t              size   =   0;

    vfsNode*            parent = nullptr;
    std::unordered_map <std::wstring, vfsNode*> children;

    vfsNode (const wchar_t* wszName, vfsNode::type type) :
                      name (wszName),       type_ (type)
    {
    };

    bool containsFile (const wchar_t* wszName)
    {
      if ( children.count (wszName)        != 0 &&
                 children [wszName]->type_ == type::File )
      {
        return true;
      }

      return false;
    }

    File* addFile      (const wchar_t* wszName)
    {
      if (containsFile (wszName))
        return static_cast <File *> (children [wszName]);

      int dirs =
        SK_CountWChars (wszName, L'\\') +
        SK_CountWChars (wszName,  L'/');

      vfsNode* pDir = this;

      if (dirs > 0)
      {
        int dir = 0;

        auto name_to_chop =
          std::make_unique <wchar_t []> (
            std::wcslen (wszName) + 1
          );

        std::wcscpy ( name_to_chop.get (),
                        wszName );

        wchar_t*  tok_ctx = nullptr;
        wchar_t* wtok     =
          std::wcstok (
            name_to_chop.get (),
              LR"(\/)",
                &tok_ctx
          );

        while (wtok != nullptr)
        {
          if (++dir <= dirs)
          {
            auto pParent = pDir;

            pDir =
              pDir->addDirectory (wtok);

            pDir->parent = pParent;
          }

          else
          {
            return
              pDir->addFile (wtok);
          }

          wtok =
            std::wcstok (nullptr, LR"(\/)", &tok_ctx);
        }
      }

      //for ( auto& file : children )
      //{
      //  if ( file->type_ == type::File &&
      //       file->name._Equal (name) )
      //  {
      //    return
      //      static_cast <File *> (file);
      //  }
      //}

      File* vfsNewFile =
        new File (wszName);

      vfsNewFile->parent = this;

      children [wszName] = vfsNewFile;

      return
        vfsNewFile;
    }

    bool containsDirectory (const wchar_t* wszName)
    {
      if ( children.count (wszName)        &&
           children       [wszName]->type_ == type::Directory )
      {
        return true;
      }

      return false;
    }

    Directory* addDirectory (const wchar_t* wszName)
    {
      if (containsDirectory (wszName))
      {
        return
          static_cast <Directory *> (children [wszName]);
      }

      //for ( auto& dir : children )
      //{
      //  if ( dir->type_ == type::Directory &&
      //       dir->name._Equal (name) )
      //  {
      //    return
      //      static_cast <Directory *> (dir);
      //  }
      //}

      Directory* vfsNewDir =
        new Directory (wszName);

      children [wszName] = vfsNewDir;
      //vfsNewDir->parent = this;

      return
        vfsNewDir;
    }

    std::wstring getFullPath (void)
    {
      SK_VirtualFS::vfsNode* pParent =
        this;

      std::stack <std::wstring> dirs;

      while (pParent != nullptr)
      {
        dirs.push  (pParent->name);
          pParent = pParent->parent;
      }

      std::wstring full_path;

      while (! dirs.empty ())
      {
        full_path += dirs.top ();
                     dirs.pop ();

        if (! dirs.empty ())
        {
          full_path += LR"(\)";
        }
      }

      return full_path;
    }

    virtual void* getSubclass (REFIID iid)
    {
      UNREFERENCED_PARAMETER (iid);

      return nullptr;
    }
  };

  class File : public vfsNode
  {
  public:
    File (const wchar_t* name) :
                vfsNode (name, vfsNode::type::File) { }
  };

  class Directory : public vfsNode
  {
  public:
    Directory (const wchar_t* name) :
                     vfsNode (name, vfsNode::type::Directory) { }
  };

  virtual ~SK_VirtualFS (void)
  {
    delete root;
           root = nullptr;
  }

  // Resets the virtual filesystem
  virtual void clear (void)
  {
    delete root;
           root =
    new Directory (L"( VFS Root )");
           name =  L"Uninitialized VFS";
  }

  operator vfsNode* (void) { return root; };

protected:
  std::wstring name = L"Uninitialized VFS";

  vfsNode*     root =
    new Directory (L"( VFS Root )");

private:
};
//
// Copyright 2020-2021 Andon "Kaldaien" Coleman
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

#include <stores/generic_library.h>
#include <stores/Steam/library.h>

#include <map>
#include <string>
#include <strsafe.h>
#include <assert.h>
#include <Shlwapi.h>

#include <install_utils.h>

#include <sk_utility/utility.h>

//
// TODO: Get this specialization stuff the hell out of here...
//
using d3d11_tex_ref_s =
            tex_ref_s <ID3D11ShaderResourceView>;

#include <d3d11.h>

template <>
struct tex_ref_s <ID3D11ShaderResourceView> :
         CComPtr <ID3D11ShaderResourceView>
{
  D3D11_TEXTURE2D_DESC& getDesc (void)
  {
    if (texDesc.Width == 0)
    {
      if (p != nullptr)
      {
        CComPtr         <ID3D11Texture2D>   pTex;
        p->GetResource ((ID3D11Resource **)&pTex.p);

        if (pTex.p != nullptr)
            pTex->GetDesc (&texDesc);
      }
    }

    return texDesc;
  }

  float getAspectRatio (void)
  {
    return
      static_cast <float> (getDesc ().Width) /
      static_cast <float> (getDesc ().Height);
  }

private:
  D3D11_TEXTURE2D_DESC texDesc = { };
};


struct app_record_s {
  app_record_s (uint32_t id_) : id (id_) { };

  uint32_t     id;
  bool         cloud_enabled = true; // hidecloudui=false
  std::wstring install_dir;
  std::string  type  =  "Game";  // TODO: Proper enum
  std::string  store = "Steam";  // maybe enum?
  std::string  EGS_InternalAppName = "";

  struct client_state_s {
    bool refresh    (app_record_s *pApp);

    DWORD running   = 0;
    DWORD installed = 0;
    DWORD updating  = 0;

    // For Registry Key Watch
    static DWORD
      _TimeLastNotified;

    DWORD                    dwTimeLastChecked = 0;
    void invalidate (void) { dwTimeLastChecked = 0; }
  } _status;

  struct names_s {
    std::string all_upper;
    std::string normal;
  } names;

  struct tex_registry_s {
    d3d11_tex_ref_s icon;
    //d3d11_tex_ref_s logo;
    //d3d11_tex_ref_s hero;
    //d3d11_tex_ref_s header;
    //d3d11_tex_ref_s six_by_nine;
    bool            isCustomIcon  = false;
    bool            isCustomCover = false;
  } textures;

  enum class Platform {
    Unknown = 0x0,
    Windows = 0x1,
    Linux   = 0x2,
    Mac     = 0x4,
    All     = 0xffff
  };

  enum class AppType {
    Default     = 0x1,
    Config      = 0x2,
    Unspecified = 0xffff
  };

  enum class CPUType {
    Common = 0x0, // Check the common config if encountered
    x86    = 0x1,
    x64    = 0x2,
    Any    = 0xffff
  };

  struct common_config_s {
    uint32_t     appid     = 0;
    CPUType      cpu_type  = CPUType::x86;
  };

  struct extended_config_s {
    struct vac_config_s    {
      uint32_t    vacmacmodulecache =     0;
      uint32_t    vacmodulecache    =     0;
      std::string vacmodulefilename =    "";
      int         enabled           =    -1;
    } vac;

    struct developer_config_s {
      std::string publisher;
      std::string developer;
      std::string homepage;
    } dev;
  } extended_config;

  struct launch_config_s {
    int          id        = 0;

    AppType      app_type  = AppType::Default;
    CPUType      cpu_type  = CPUType::Common;
    Platform     platforms = Platform::All;

    std::wstring store = L"Steam"; // Used by getExecutableFullPath
    std::wstring executable;
    std::wstring description;
    std::wstring launch_options; // Used by GOG and EGS
    std::wstring working_dir;
    std::wstring blacklist_file;
    std::wstring type;

    bool         valid       = false; // Path points to a real directory
    int          blacklisted = -1;

    std::wstring getBlacklistFilename  (int32_t appid);
    bool         setBlacklisted        (int32_t appid, bool blacklist);
    bool         isBlacklisted         (int32_t appid);
    std::wstring getExecutableDir      (int32_t appid);
    std::wstring getExecutableFullPath (int32_t appid);
  };

  struct cloud_save_record_s {
    Platform     platforms = Platform::Unknown;

    std::wstring root;
    std::wstring path;
    std::wstring evaluated_dir;
    std::wstring pattern;

    bool        valid = false; // Path points to a real directory
  };

  struct branch_record_s {
    app_record_s *parent;
    std::wstring  description;
    uint32_t      build_id;
    uint32_t      pwd_required;
    time_t        time_updated;
    std::string   time_string; // Cached text representation
    std::string   desc_utf8;   // For non-Latin characters to print in ImGui

    std::string  getTimeAsCStr (void) const;
    std::string& getDescAsUTF8 (void);
  };

  struct specialk_config_s {
    std::wstring           profile_dir;
    std::set <std::string> screenshots; // utf8 path
    sk_install_state_s     injection;
  } specialk;

  std::map <std::string, branch_record_s    > branches;
  std::map <int,         cloud_save_record_s> cloud_saves;
  std::map <int,         launch_config_s    > launch_configs;
  common_config_s                             common_config;

  template <class _Tp> static
    constexpr bool
      supports (_Tp bitset, _Tp test)
      {
        return
          ( static_cast <int> (bitset) &
            static_cast <int> (test)   ) ==
            static_cast <int> (test);
      }

  template <class _Tp> static
    constexpr _Tp
      addSupportFor (_Tp bitset, _Tp instance)
      {
        bitset =
          static_cast <_Tp> (
            ( static_cast <int> (bitset)   |
              static_cast <int> (instance) )
          );

        return bitset;
      }
};
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
//#include <stores/Steam/steam_library.h>

#include <map>
#include <string>
#include <strsafe.h>
#include <assert.h>
#include <Shlwapi.h>

#include <utility/sk_utility.h>

#include <Windows.h>

#include <utility/utility.h>

//#include "steam/steam_api.h"
#include <utility/vfs.h>

#include <vector>

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

  struct client_state_s {
    bool refresh    (app_record_s *pApp);

    DWORD running     = 0;
    DWORD installed   = 0;
    DWORD updating    = 0;

    DWORD running_pid = 0;

    // For Registry Key Watch
    static DWORD
      _TimeLastNotified;
    
    DWORD                    dwTimeDelayChecks = 0; // Used to prevent the status from changing for X number of milliseconds.
    DWORD                    dwTimeLastChecked = 0;
    void invalidate (void) { dwTimeLastChecked = 0; }
  } _status;

  struct names_s {
    std::string all_upper;
    std::string all_upper_alnum;
    std::string normal;   // Name used in SKIF (custom names are applied on this)
    std::string original; // Holds the original name before any custom name was applied
    size_t      pre_stripped = 0;
  } names;

  struct tex_registry_s {
    d3d11_tex_ref_s texture;
    bool            isCustom  = false;
    bool            isManaged = false; // Indicates whether the texture is managed by SKIF or not
    int             iWorker   = 0;     // 0 = worker not started, 1 = worker active, 2 = worker done
    HANDLE          hWorker   = NULL;
  } tex_icon, tex_cover;
  
  enum class Store {
    Steam       = 0x1,   // Initial commit
    GOG         = 0x2,   // Sep 17, 2021
    Custom      = 0x3,   // Oct  2, 2021 - SKIF custom games
    Epic        = 0x4,   // Dec 27, 2021
    Xbox        = 0x5,   // Mar  6, 2022
    Unspecified = 0xffff
  };

  enum class Platform {
    Unknown = 0x0,
    Windows = 0x1,
    Linux   = 0x2,
    Mac     = 0x4,
    All     = 0xffff
  };

  enum class CPUType {
    Common = 0x0, // Check the common config if encountered
    x86    = 0x1,
    x64    = 0x2,
    Any    = 0xffff
  };

  struct sk_install_state_s {
    struct Injection {
      enum class Bitness {
        ThirtyTwo = 0x1,
        SixtyFour = 0x2,
        Unknown   = 0x0
      }            bitness = Bitness::Unknown;
      enum class EntryPoint {
        DDraw   = 0x1,
        D3D8    = 0x2,
        D3D9    = 0x4,
        D3D11   = 0x8,
        DXGI    = 0x10,
        OpenGL  = 0x20,
        DInput8 = 0x40,
        CBTHook = 0x80,
        Unknown = 0x0
      }            entry_pt = EntryPoint::Unknown;
      enum class Type {
        Global  = 0x1,
        Local   = 0x2,
        Unknown = 0x0
      }            type     = Type::Unknown;
    } injection;

    struct Config {
      enum class Type {
        Centralized = 0x1,
        Localized   = 0x2,
        Unknown     = 0x0
      }            type = Type::Unknown;
      std:: string type_utf8 = "Unknown";

      std::wstring shorthand;
      std:: string shorthand_utf8; // Converted to utf-8 from utf-16
      std::wstring root_dir;
      std:: string root_dir_utf8;  // Converted to utf-8 from utf-16
      std::wstring full_path;
      std:: string full_path_utf8; // Converted to utf-8 from utf-16
    } config;

    struct {
      std::wstring shorthand;
      std:: string shorthand_utf8; // Converted to utf-8 from utf-16
      std::wstring version;
      std:: string version_utf8;   // Converted to utf-8 from utf-16
      std::wstring full_path;
      std:: string full_path_utf8; // Converted to utf-8 from utf-16
    } dll;

    std::string    localized_name; // UTF-8
  };

  struct common_config_s {

    enum class AppType {   // Used by Steam to indicate the type of "app" we're dealing with
      Unknown     = 0x0,   // Default in SKIF
      Game        = 0x1,   // game
      Application = 0x2,   // application
      Tool        = 0x3,   // tool
      Music       = 0x4,   // music
      Demo        = 0x5,   // demo
      Unspecified = 0xffff // not expected to be seen
    };

    uint32_t     appid     = 0;
    CPUType      cpu_type  = CPUType::Any;     // This is an utterly useless key on Steam -- do not expect it to contain anything important! It's either not set, empty, or set to 64 -- nothing else!
    AppType      type      = AppType::Unknown; // Steam
  };

  // Struct used to hold custom SKIF metadata about the game
  struct custom_metadata_s {
    std::string        name;
    int            cpu_type =  0; // 0 = Common,             1 = x86,                 2 = x64,                0xFFFF = Any
    int        instant_play =  0; // 0 = use global default, 1 = always instant play, 2 = never instant play
    int           auto_stop =  0; // 0 = use global default, 1 = stop on injection,   2 = stop on game exit,  3 = never stop
    int                uses =  0; // Number of times game has been launched
    std::string        used = ""; // Unix timestamp (in string) of when the game was last used
    std::string        used_formatted = ""; // Friendly human-readable representation of the Unix timestamp
    int              hidden =  0; // Visibility
  } skif;

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

    enum class Type {
      Default     = 0x0,   // default - Launch (Default)
      Option1     = 0x1,   // option1 - Play %%description%% (previously Config)
      Option2     = 0x2,   // option2 - Play %%description%%
      Option3     = 0x3,   // option3 - Play %%description%%
      Unspecified = 0xffff // none    - Unspecified
    };

    int          id        = 0; // SKIF's internal launch identifier
    int          id_steam  = 0; // Steam's internal launch identifer
    sk_install_state_s     injection;

    Type         type      = Type::Unspecified;
    CPUType      cpu_type  = CPUType::Common;
    Platform     platforms = Platform::All;

    std::wstring getBlacklistFilename       (void);
    bool         setBlacklisted             (bool blacklist);
    bool         isBlacklisted              (bool refresh = false);

    std::wstring getElevatedFilename        (void);
    bool         setElevated                (bool elevated);
    bool         isElevated                 (bool refresh = false);

    std::wstring getExecutableFileName      (void);
    std:: string getExecutableFileNameUTF8  (void);
    bool          isExecutableFileNameValid (void);

    std::wstring getExecutableDir           (void) const;
    bool          isExecutableDirValid      (void) const;

    std::wstring getExecutableFullPath      (void) const;
    std:: string getExecutableFullPathUTF8  (void);
    bool          isExecutableFullPathValid (void);
    
    std::wstring getDescription             (void);
    std:: string getDescriptionUTF8         (void);
    
    std::wstring getLaunchOptions           (void) const;
    std:: string getLaunchOptionsUTF8       (void);
    
    std::wstring getWorkingDirectory        (void) const;
    std:: string getWorkingDirectoryUTF8    (void);

  //private:
  //app_record_s* parent = nullptr;

    std::wstring executable;
    std:: string executable_utf8;
    int          executable_valid      = -1;

    std::wstring executable_path;
    std:: string executable_path_utf8;
    int          executable_path_valid = -1;

    std::wstring install_dir;

    std::wstring description;
    std:: string description_utf8;
    std::wstring launch_options;
    std:: string launch_options_utf8;
    std::wstring working_dir;
    std:: string working_dir_utf8;
    std::wstring blacklist_file;
    std::wstring elevated_file;
    std::wstring executable_helper;  // Used by Xbox to hold gamelaunchhelper.exe
    std::set <std::string>
                 branches;           // Steam: Only show launch option if one of these beta branches are active
    std:: string branches_joined;
    std:: string requires_dlc;       // Steam: Only show launch option if this DLC is owned

    int          valid              = -1;    // Launch config is valid (what does this actually mean?)
    bool         duplicate_exe      = false; // Used for Steam games indicating that a launch option is a duplicate (shares the same executable as another)
    bool         duplicate_exe_args = false; // Used for Steam games indicating that a launch option is a duplicate (shares the same executable and arguments as another)
    bool         custom_skif        = false; // Is the launch config an online-based custom one populated by SKIF ?
    bool         custom_user        = false; // Is the launch config a user-specied custom one?
    int          owns_dlc           = -1;    // Does the user own the required DLC ?
    int          blacklisted        = -1;
    int          elevated           = -1;
  };

  struct cloud_save_record_s {
    Platform     platforms = Platform::Unknown;

    std::wstring root;
    std::wstring path;
    std::wstring evaluated_dir;
    std::wstring pattern;

    int          valid = -1; // Path points to a real directory
  };

  struct branch_record_s {
    app_record_s *parent;
    uint32_t      build_id;
    uint32_t      pwd_required;
    time_t        time_updated;
    std::wstring  description;
    std:: string  description_utf8; // For non-Latin characters to print in ImGui
    std::wstring  time_string;      // Cached text representation
    std:: string  time_string_utf8; // Cached text representation

    std::wstring getDescription     (void);
    std:: string getDescriptionUTF8 (void);
    std::wstring getTime            (void);
    std:: string getTimeUTF8        (void);
  };

  
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

  using branch_t =
    std::pair <std::string, branch_record_s>;

  // This struct holds the cache for the right click context menu
  struct {
    int                                   numSecondaryLaunchConfigs = 0; // Secondary launch options
    bool                                  profileFolderExists       = false;
    bool                                  screenshotsFolderExists   = false;
    std::wstring                          wsScreenshotDir           = L"";
    std::vector   <CloudPath>             cloud_paths;   // Steam Auto-Cloud
    std::multimap <int64_t, branch_t>     branches;      // Steam Branches

    // PCGamingWiki
    std::wstring pcgwValue;
    std::wstring pcgwLink;

    std::string   label_version; // type_version
  } ui;

  struct specialk_config_s {
    std::wstring           profile_dir;
    std::set <std::string> screenshots; // utf8 path
    sk_install_state_s     injection;
  } specialk;
  
  std::map <std::string, branch_record_s    > branches;
  std::map <int,         cloud_save_record_s> cloud_saves;
  std::map <int,         launch_config_s    > launch_configs;
  std::map <int,         launch_config_s    > launch_configs_custom; // Workaround for Steam games parsing original launch configs on selection
  common_config_s                             common_config;
  
  uint32_t     id;
  bool         processed             =  false; // indicates if we have processed appinfo
  bool         loading               =  false; // indicates if we are processing in a background thread
  bool         cloud_enabled         =   true; // hidecloudui=false
  std::wstring install_dir;
  Store        store                 =  Store::Unspecified;
  std::string  store_utf8            =  "";

  std::string  ImGuiPushID           =  "";
  std::string  ImGuiLabelAndID       =  "";

  std::string  Steam_ManifestData    =  "";
  std::wstring Steam_ManifestPath    = L"";
  std::string  Steam_LaunchOption    =  "";       // Holds the custom launch option set in the Steam client
  std::string  Steam_LaunchOption1   =  "";       // Holds a cached parsed value of the launch option set in the Steam client
  std::string  branch                =  "public"; // Holds the current "beta" branch set in the Steam client (default: public)

  std::string  Epic_CatalogNamespace =  "";
  std::string  Epic_CatalogItemId    =  "";
  std::string  Epic_AppName          =  "";
  std::string  Epic_DisplayName      =  ""; // Might be removable, replaced with normal.original ??

  std::string  Xbox_PackageName      =  "";
  std::string  Xbox_PackageFullName  =  "";
  std::string  Xbox_PackageFamilyName=  "";
  std::string  Xbox_StoreId          =  "";
  std::wstring Xbox_AppDirectory     = L""; // Holds the :\WindowsApps\<package-name>\ path for the game

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

using InjectionBitness =
  app_record_s::sk_install_state_s::Injection::Bitness;
using InjectionPoint =
  app_record_s::sk_install_state_s::Injection::EntryPoint;
using InjectionType =
  app_record_s::sk_install_state_s::Injection::Type;
using ConfigType =
  app_record_s::sk_install_state_s::Config::Type;

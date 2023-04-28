#pragma once
#include <cstdint>
#include <string>
#include <wtypes.h>
#include <set>
#include <install_utils.h>
#include <map>
#include "Steam/app_record.h"


struct app_generic_s {
  //app_generic_s () { };
  //app_generic_s (uint32_t id_) : id (id_) { };
  virtual ~app_generic_s() = default;

  uint32_t     id;
  std::wstring install_dir;
  std::string  ImGuiLabelAndID = "";

  struct client_state_s {
    bool refresh    (app_generic_s *pApp);

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
    d3d11_tex_ref_s cover;
    bool            isCustomIcon  = false;
    bool            isCustomCover = false;
  } textures;

  enum class Store {
    Epic        = 0x1,
    GOG         = 0x2,
    Steam       = 0x3,
    Xbox        = 0x4,
    Unspecified = 0xffff
  };

  enum class CPUType {
    Common = 0x0, // Check the common config if encountered
    x86    = 0x1,
    x64    = 0x2,
    Any    = 0xffff
  };

  struct launch_config_s {
    app_generic_s *parent;
    int          id        = 0;

    CPUType      cpu_type  = CPUType::Common;

    std::wstring executable;
    std::wstring executable_path;
    std::wstring description;
    std::wstring launch_options;      // Used by GOG and Epic
    std::wstring working_dir;
    std::wstring blacklist_file;
    std::wstring elevated_file;
    std::wstring type;
    std::wstring executable_helper;   // Used by Xbox to hold gamelaunchhelper.exe

    bool         valid       = false; // Path points to a real directory
    int          blacklisted = -1;
    int          elevated    = -1;

    launch_config_s (app_generic_s* parent_) : parent (parent_) { };

    bool         useConfig             (void);
    std::wstring getBlacklistFilename  (int32_t appid);
    bool         setBlacklisted        (int32_t appid, bool blacklist);
    bool         isBlacklisted         (int32_t appid);
    std::wstring getElevatedFilename   (int32_t appid);
    bool         setElevated           (int32_t appid, bool elevated);
    bool         isElevated            (int32_t appid);
    std::wstring getExecutableDir      (int32_t appid, bool validate = true);
    std::wstring getExecutableFullPath (int32_t appid, bool validate = true);
  };

  struct specialk_config_s {
    std::wstring           profile_dir;
    std::set <std::string> screenshots; // utf8 path
    sk_install_state_s     injection;
  } specialk;

  Store store = Store::Unspecified;
  std::map <int, launch_config_s>
        launch_configs;

  virtual void launchGame (void) = 0;
  virtual ID3D11ShaderResourceView* getCover (void) = 0;
  virtual ID3D11ShaderResourceView* getIcon  (void) = 0;

  bool loadCoverAsFile (std::wstring path);
  bool loadIconAsFile  (std::wstring path);
};

// GOG entries
struct app_gog_s : app_generic_s {
  
  void                      launchGame (void) override;
  ID3D11ShaderResourceView* getCover   (void) override;
  ID3D11ShaderResourceView* getIcon    (void) override;
};

// Steam entries
struct app_steam_s : app_generic_s {
  bool         cloud_enabled = true; // hidecloudui=false

  app_steam_s ( ) { };

  struct cloud_save_record_s {
    std::wstring root;
    std::wstring path;
    std::wstring evaluated_dir;
    std::wstring pattern;

    bool        valid = false; // Path points to a real directory
  };

  struct branch_record_s {
    app_steam_s *parent;
    std::wstring  description;
    uint32_t      build_id;
    uint32_t      pwd_required;
    time_t        time_updated;
    std::string   time_string; // Cached text representation
    std::string   desc_utf8;   // For non-Latin characters to print in ImGui

    std::string  getTimeAsCStr (void) const;
    std::string& getDescAsUTF8 (void);
  };

  std::map <std::string, branch_record_s    > branches;
  std::map <int,         cloud_save_record_s> cloud_saves;
  
  void                      launchGame (void) override;
  ID3D11ShaderResourceView* getCover   (void) override;
  ID3D11ShaderResourceView* getIcon    (void) override;
};

// Xbox entries
struct app_xbox_s : app_generic_s {
  std::string  Xbox_PackageName = "";
  std::string  Xbox_StoreId = "";
  std::wstring Xbox_AppDirectory; // Holds the :\WindowsApps\<package-name>\ path for the game
  
  void                      launchGame (void) override;
  ID3D11ShaderResourceView* getCover   (void) override;
  ID3D11ShaderResourceView* getIcon    (void) override;
};
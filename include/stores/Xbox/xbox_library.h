#pragma once
#include <stores/Steam/app_record.h>
#include <utility/fsutil.h>
#include <vector>

void         SKIF_Xbox_GetInstalledAppIDs       (app_list_t *apps);
void         SKIF_Xbox_IdentifyAssetNew         (std::string PackageName, std::string StoreID);
bool         SKIF_Xbox_hasInstalledGamesChanged (void);
std::wstring SKIF_Xbox_GetCustomLaunchCommandW  (int appId, std::string_view packageFamilyName, std::string_view customCommand);

// Library record for an Xbox / Microsoft Store game
struct app_record_xbox_s : app_record_store_s <app_record_xbox_s, app_record_s::Store::Xbox>
{
  using app_record_store_s::app_record_store_s;

  // Xbox-specific data
  struct xbox_s {
    std::string  package_name            =  ""; // PackageName
    std::string  package_name_full       =  ""; // PackageFullName
    std::string  package_name_family     =  ""; // PackageFamilyName
    std::string  store_id                =  ""; // StoreId
    std::wstring directory_app           = L""; // AppDirectory; Holds the x:\WindowsApps\<package-full-name> path
    std::wstring directory_program_files = L""; // Program Files directory; Holds the C:\Program Files\WindowsApps\<package-full-name> path
  } xbox;

  // Library refresh (app_record.h)
  bool        HasSameLibraryData      (const app_record_s& other) const override
  {
    if (! app_record_s::HasSameLibraryData (other))
      return false;

    const xbox_s& o = static_cast <const app_record_xbox_s&> (other).xbox;

    return (xbox.package_name            == o.package_name            &&
            xbox.package_name_full       == o.package_name_full       &&
            xbox.package_name_family     == o.package_name_family     &&
            xbox.store_id                == o.store_id                &&
            xbox.directory_app           == o.directory_app           &&
            xbox.directory_program_files == o.directory_program_files);
  }

  bool        isInstantPlayCompatible (void) const override { return true; }
  std::string GetPersistentKey        (void) const override { return xbox.package_name; }

  // Launching (xbox_library.cpp)
  void        WhitelistForInjection   (launch_config_s* config) override;
  bool        PrefersInstantLaunch    (bool fromGameMenu, launch_config_s* config) const override;
  void        Launch                  (const launch_context_s& ctx) override;

  // Library UI (xbox_library.cpp)
  bool         GetInstantPlayDefault   (void) const override;
  std::wstring GetStoreAssetDirectory  (void) const override;
  bool         CanCreateDesktopShortcut(void)       override { return true; }
  std::wstring GetDesktopShortcutArgs  (void)       override;
  std::wstring GetDesktopShortcutIcon  (void)       override { return GetStoreAssetDirectory ( ) + L"icon-original.ico"; }
  std::vector <std::pair <std::string, std::string>>
               GetStoreDebugFields     (void) const override;

  // Context menu hooks (xbox_library.cpp)
  void         DrawDeveloperLaunchConfigItems (launch_config_s& launch) override;
  void         DrawDeveloperActions           (void) override;

  // Library population: packaged apps are not checked for an install folder, and have two folders to map to the profile
  bool         ValidatesInstallDirectory  (void) const override { return false; }
  std::vector <std::wstring>
               GetAdditionalProfilePaths  (void) const override { return { xbox.directory_app, xbox.directory_program_files }; }

  // Run-state detection (xbox_library.cpp)
  ProcessMatch MatchProcess                (const wchar_t* wszExeFile, const wchar_t* wszExePath, DWORD dwExePathLen) override;

  // Artwork (xbox_library.cpp)
  std::wstring ResolveArtwork          (LibraryTexture type, const std::wstring& source, bool& isCustom, bool& isManaged) override;
  std::wstring PrepareCoverSource      (bool queryPCGW) override;
  bool         DeleteStoreCoverFiles   (const std::wstring& assetDir) override;
  bool         WantsIGDBCover          (void) const override;
  bool         DownloadIGDBCover       (const std::wstring& targetPath) override;
};

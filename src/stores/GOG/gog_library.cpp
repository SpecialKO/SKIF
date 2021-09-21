
#include <stores/gog/gog_library.h>
#include <wtypes.h>

/*
GOG Galaxy / Offline Installers shared registry struture


Root Key: HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\GOG.com\Games\

Each game is stored in a separate key beneath, named after the Game ID/Product ID of the game.
Each key have a bunch of values with some basic data of the game, its location, and launch options.

Registry values and the data they contain:

	exe					 				-- Full path to game executable
	exeFile			 				-- Filename of game executable
	gameID				 			-- App ID of the game
	gameName			 			-- Title of the game
	launchCommand 			-- Default launch full path and parameter of the game
	launchParam	 				-- Default launch parameter of the game
	path								-- Path to the game folder
	productID						-- Same as App ID of the game ?
	uninstallCommand		-- Full path to the uninstaller of the game
	workingDir					-- Working directory of the game

There are more values, but they aren't listed here.


GOG Galaxy Custom Default Launch Option:
To launch a game using the Galaxy user's customized launch option,
it's enough to launch the game through Galaxy like the start menu shortcuts does, like this:
		
	"D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

*/


void
SKIF_GOG_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
{
	HKEY hKey;
  DWORD dwIndex = 0, dwResult, dwSize;
  WCHAR szSubKey[MAX_PATH];
  WCHAR szData[MAX_PATH];

  /* Load GOG titles from registry */
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\WOW6432Node\GOG.com\Games\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {

    if (RegQueryInfoKeyW(hKey, NULL, NULL, NULL, &dwResult, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {

      do
      {
        dwSize   = sizeof(szSubKey) / sizeof(WCHAR);
        dwResult = RegEnumKeyExW(hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          dwSize = sizeof(szData) / sizeof(WCHAR);

          if (RegGetValueW(hKey, szSubKey, L"dependsOn", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS &&
            wcslen(szData) == 0) // Only handles items without a dependency (skips DLCs)
          {
            dwSize = sizeof(szData) / sizeof(WCHAR);

            if (RegGetValueW(hKey, szSubKey, L"GameID", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
            {
              app_record_s GOG_record(_wtoi(szData));

              GOG_record.id = _wtoi(szData);
              GOG_record.store = "GOG";
              GOG_record.type  = "Game";
              //GOG_record.extended_config.vac.enabled = false;
              GOG_record._status.installed = true;

              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW(hKey, szSubKey, L"GameName", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
                GOG_record.names.normal = SK_WideCharToUTF8(szData);

              // Strip null terminators
              GOG_record.names.normal.erase(std::find(GOG_record.names.normal.begin(), GOG_record.names.normal.end(), '\0'), GOG_record.names.normal.end());

              // Add (GOG) at the end of the name
              //GOG_record.names.normal = GOG_record.names.normal + " (GOG)";

              GOG_record.names.all_upper = GOG_record.names.normal;
              std::for_each(GOG_record.names.all_upper.begin(), GOG_record.names.all_upper.end(), ::toupper);

              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW(hKey, szSubKey, L"path", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
                GOG_record.install_dir = szData;

              dwSize = sizeof(szData) / sizeof(WCHAR);
              if (RegGetValueW(hKey, szSubKey, L"exe", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
              {
                app_record_s::launch_config_s lc;
                lc.id = 0;
                lc.store = L"GOG";
                lc.executable = szData;
                lc.working_dir = GOG_record.install_dir;

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW(hKey, szSubKey, L"launchParam", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
                  lc.launch_options = szData;

                GOG_record.launch_configs[0] = lc;

                dwSize = sizeof(szData) / sizeof(WCHAR);
                if (RegGetValueW(hKey, szSubKey, L"exeFile", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
                  GOG_record.specialk.profile_dir = szData;

                GOG_record.specialk.injection.injection.type = sk_install_state_s::Injection::Type::Global;

                std::pair <std::string, app_record_s>
                  GOG(GOG_record.names.normal, GOG_record);

                apps->emplace_back(GOG);
              }
            }
          }
        }

        dwIndex++;

      } while (1);
    }

    RegCloseKey(hKey);

    // If an item was read, see if we can detect GOG Galaxy as well
    if (dwIndex > 0)
    {
      if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\WOW6432Node\GOG.com\GalaxyClient\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
      {
        dwSize = sizeof(szData) / sizeof(WCHAR);
        if (RegGetValueW(hKey, NULL, L"clientExecutable", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
        {
          extern std::wstring GOGGalaxy_Path;
          extern std::wstring GOGGalaxy_Folder;
          extern bool GOGGalaxy_Installed;

          GOGGalaxy_Folder = szData;

          dwSize = sizeof(szData) / sizeof(WCHAR);
          if (RegGetValueW(hKey, L"paths", L"client", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
          {
            GOGGalaxy_Path = SK_FormatStringW(LR"(%ws\%ws)", szData, GOGGalaxy_Folder.c_str());

            if (PathFileExistsW(GOGGalaxy_Path.c_str()))
              GOGGalaxy_Installed = true;
          }
        }

        RegCloseKey(hKey);
      }
    }
  }
}
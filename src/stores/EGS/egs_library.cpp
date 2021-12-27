
#include <stores/egs/egs_library.h>
//#include <stores/generic_app.h>
#include <wtypes.h>
#include <json.hpp>
#include <fstream>
#include <filesystem>

/*
EGS registry / folder struture


Root Key:						HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Epic Games\EpicGamesLauncher\AppDataPath
Root Folder:				C:\ProgramData\Epic\EpicGamesLauncher\Data\
Manifest Folder:		C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests

Each game is stored in a separate .item file, named after the InstallationGuid of the install.
Each file have a bunch of values with detailed data of the game, its location, and launch options.

Parameters and the data they contain:

	InstallLocation			-- Full path of the install folder
	LaunchExecutable		-- Filename of executable
	AppCategories				-- Categories for the app ("games" indicates a game)
	DisplayName					-- Title of the app
	InstallationGuid		-- Installation guid, same as the file name

	CatalogNamespace		-- Used to construct the launch command
	CatalogItemId				-- Used to construct the launch command
	AppName							-- Used to construct the launch command

There are more values, but they aren't listed here.

EGS Custom Default Launch Option:
To launch a game using the user's customized launch option in the client,
it's enough to launch the game through EGS like the start menu shortcuts does, like this:
		
	com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true

*/


void
SKIF_EGS_GetInstalledAppIDs (std::vector <std::pair < std::string, app_record_s > > *apps)
{
	HKEY hKey;
	DWORD dwSize;
	WCHAR szData[MAX_PATH];
	std::wstring EGS_AppDataPath; // C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests

	/* Load EGS appdata path from registry */	
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Epic Games\EpicGamesLauncher\)", 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
	{
		dwSize = sizeof(szData) / sizeof(WCHAR);
		if (RegGetValueW(hKey, NULL, L"AppDataPath", RRF_RT_REG_SZ, NULL, szData, &dwSize) == ERROR_SUCCESS)
		{
			EGS_AppDataPath = szData;
			EGS_AppDataPath += LR"(\Manifests\)";

			RegCloseKey(hKey);

			//OutputDebugString(L"Reading Epic Games Store manifests\n");
			//OutputDebugString(EGS_AppDataPath.c_str());
			//OutputDebugString(L"\n");

			for (const auto& entry : std::filesystem::directory_iterator(EGS_AppDataPath))
			{
				//OutputDebugString(entry.path().wstring().c_str());
				//OutputDebugString(L"\n");

				if (entry.is_directory() == false &&
					entry.path().extension().string() == ".item" )
				{
					// Read file to JSON object
					std::ifstream file(entry.path().string());
					nlohmann::json app;
					file >> app;
					file.close();

					// Parse file
					//OutputDebugString(SK_UTF8ToWideChar(app.dump()).c_str());
					//OutputDebugString(L"\n");

					bool isGame = false;

					for (auto& categories : app["AppCategories"])
					{
						if (categories.dump() == R"("games")")
							isGame = true;
						//OutputDebugString(SK_UTF8ToWideChar(categories.dump()).c_str());
						//OutputDebugString(L"\n");
					}

					if (isGame)
					{
						app_record_s EGS_record(app.at("InstallSize"));

						EGS_record.store = "EGS";
						EGS_record.type = "Game";
						EGS_record._status.installed = true;
						EGS_record.install_dir = SK_UTF8ToWideChar(app.at("InstallLocation"));
						EGS_record.install_dir.erase(std::find(EGS_record.install_dir.begin(), EGS_record.install_dir.end(), '\0'), EGS_record.install_dir.end());

						EGS_record.names.normal = app.at("DisplayName");

						EGS_record.names.all_upper = EGS_record.names.normal;
						std::for_each(EGS_record.names.all_upper.begin(), EGS_record.names.all_upper.end(), ::toupper);

						app_record_s::launch_config_s lc;
						lc.id = 0;
						lc.store = L"EGS";
						lc.executable = EGS_record.install_dir + L"\\" + SK_UTF8ToWideChar(app.at("LaunchExecutable"));
						//lc.executable.erase(std::find(lc.executable.begin(), lc.executable.end(), '\0'), lc.executable.end());

						//OutputDebugString(lc.executable.c_str());
						//OutputDebugString(L"\n");

						lc.working_dir = EGS_record.install_dir;
						//lc.launch_options = SK_UTF8ToWideChar(app.at("LaunchCommand"));

						std::string CatalogNamespace = app.at("CatalogNamespace"),
							CatalogItemId = app.at("CatalogItemId"),
							AppName = app.at("AppName");

						// com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
						lc.launch_options = SK_UTF8ToWideChar(CatalogNamespace + "%3A" + CatalogItemId + "%3A" + AppName);
						lc.launch_options.erase(std::find(lc.launch_options.begin(), lc.launch_options.end(), '\0'), lc.launch_options.end());

						EGS_record.launch_configs[0] = lc;

						EGS_record.EGS_InternalAppName = AppName;

						EGS_record.specialk.profile_dir = lc.executable;
						EGS_record.specialk.injection.injection.type = sk_install_state_s::Injection::Type::Global;


						std::pair <std::string, app_record_s>
							EGS(EGS_record.names.normal, EGS_record);

						apps->emplace_back(EGS);
					}
				}
			}
		}
	}
}


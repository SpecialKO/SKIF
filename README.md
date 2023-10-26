# <img src="https://sk-data.special-k.info/artwork/blahblah/skif_eclipse_sticker.png" width="24" alt="Animated eclipse icon for Special K Injection Frontend (SKIF)"> Special K Injection Frontend (SKIF)
![Screenshot of the app](https://sk-data.special-k.info/artwork/screens/skif_75percent.png)

The modern frontend used for managing Special K and its global injection service as well as quickly and easily inject Special K into games launched through the frontend.

New versions are distributed through the packaged Special K installer available at https://special-k.info/

## Features

- Update Special K automatically
- Update local injections of Special K
- Manage the global injection service (autostart/autostop, stop behaviour, start with Windows)
- List installed games on supported platforms and quickly inject Special K into them
- Reset game profile or apply the compatibility profile
- Disable/blacklist Special K for games or processes
- Detect injected games or processes as well as common conflicts that may cause issues
- *and various other features..*

## Platforms

SKIF supports detecting and launching games from the following platforms:

- Epic Games Launcher
- GOG (both standalone installers and GOG Galaxy)
- Steam
- Xbox / Microsoft Store (**Win32 games only**)

## Command line arguments

| Argument&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `<empty>`                 | Launches SKIF. |
| `Start`                   | Launches SKIF and starts an instance of the injection service. |
| `Temp`                    | Used with `Start` to indicate the injection service should auto-stop after a successful injection. |
| `Stop`                    | Stops running instances of the injection service. Can also be used to attempt to force-eject leftover injections of Special K.  |
| `Quit`                    | Closes running instances of SKIF. |
| `Minimize`                | Launches SKIF minimized *or* minimizes any running instances of SKIF, to the taskbar or notification area depending on configuration. |
| `"<path>.exe"`            | Uses SKIF as a launcher to start the injection service, launch another application, and then stop the service. Any arguments specified after the path is proxied to the launched application. Called through `SKIF %COMMAND%` from within the Steam client. |
| `SKIF_SteamAppID=<int>` | Sets a specific Steam App ID as the environment variable when used as a launcher or launching a custom game. This can replace the use of a special `steam_appid.txt` file in the game folder. |
| `AddGame="<path>"`        | **Experimental!** Adds the specified application to the library of SKIF as a custom game. |
| `RestartDisplDrv`         | **Requires elevation!** Restarts the display driver (useful as this can sometimes fix MPOs). |

## Third-party code

* Uses [Dear ImGui](https://github.com/ocornut/imgui), licensed under [MIT](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).
* Uses [DirectX Texture Library](http://go.microsoft.com/fwlink/?LinkId=248926), licensed under [MIT](https://github.com/microsoft/DirectXTex/blob/main/LICENSE).
* Uses [Font Awesome Free v6.2.1](https://fontawesome.com/v6/download), licensed under [SIL OFL 1.1 License](https://scripts.sil.org/OFL).
* Uses [JSON for Modern C++](https://github.com/nlohmann/json), licensed under [MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT).
* Uses [PicoSHA2](https://github.com/okdshin/PicoSHA2), licensed under [MIT](https://github.com/okdshin/PicoSHA2/blob/master/LICENSE).
* Uses [PLOG](https://github.com/SergiusTheBest/plog), licensed under [MIT](https://github.com/SergiusTheBest/plog/blob/master/LICENSE).
* Uses [pugixml](https://pugixml.org/), licensed under [MIT](https://pugixml.org/license.html).
* Uses [GSL](https://github.com/microsoft/GSL), licensed under [MIT](https://github.com/microsoft/GSL/blob/main/LICENSE).
* Uses [TextFlowCpp](https://github.com/catchorg/textflowcpp), licensed under [BSL-1.0](https://github.com/catchorg/textflowcpp/blob/master/LICENSE.txt).
* Uses code from [Custom Resolution Utility (CRU)](https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU), licensed under a [custom license](https://github.com/SpecialKO/SKIF/blob/master/src/drvreset.cpp).
* Includes various snippets of code from [Stack Overflow](https://stackoverflow.com/), licensed under [Creative Commons Attribution-ShareAlike](https://stackoverflow.com/help/licensing).

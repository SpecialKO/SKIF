# <img src="https://sk-data.special-k.info/artwork/blahblah/skif_eclipse_sticker.png" width="24" alt="Animated eclipse icon for Special K Injection Frontend (SKIF)"> Special K Injection Frontend (SKIF)
![Screenshot of the app](https://sk-data.special-k.info/artwork/screens/skif_75percent.png)

The modern frontend used for managing Special K and its global injection service as well as quickly and easily inject Special K into games launched through the frontend.

New versions are distributed through the packaged Special K installer available at https://www.special-k.info/

## Features

- Update Special K automatically
- Update local injections of Special K
- Manage the global injection service (autostart/autostop, stop behaviour, start with Windows)
- List installed games on supported platforms and quickly inject Special K into them
- Browse the library as a list or as a grid of covers, with support for animated covers (APNG and WebP)
- Download covers automatically from the store, IGDB, or PCGamingWiki, or drag and drop your own
- Organize games with favorites and custom categories, and change several games at once
- Allow for quicker "Instant Play" launch options that bypasses the default launch process of store platforms (PSA: also stuff like cloud saves sync)
- Adds custom launch configurations (crowdsourced through [this repository](https://github.com/SpecialKO/SKIF_launch_configs))
- Set environment variables and a working directory for launches
- Detect the technology a game uses (engine, anti-cheat, etc.) from its files, and disable Special K by default for games with anti-cheat
- Edit Special K's config files through the built-in INI Editor
- Manage local installs of Special K through the Local Install Manager
- Reset game profile or apply the compatibility profile
- Disable/blacklist Special K for games or processes
- Detect injected games or processes as well as common conflicts that may cause issues
- Shows active multiplane overlay (MPO) support of the system, and manage Vulkan layers
- Available in English and German
- *and various other features..*

## Platforms

SKIF supports detecting and launching games from the following platforms:

- EA app
- Epic Games Launcher
- GOG (both standalone installers and GOG Galaxy)
- itch.io
- Steam
- Ubisoft Connect
- Xbox / Microsoft Store (**Win32 games only**)

Other games can be added to the library manually as custom games.

## Command line arguments

| Argument&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `<empty>`                 | Launches SKIF. |
| `Start`                   | Launches SKIF and starts an instance of the injection service. |
| `Temp`                    | Used with `Start` to indicate the injection service should auto-stop after a successful injection. |
| `Stop`                    | Stops running instances of the injection service. Can also be used to attempt to force-eject leftover injections of Special K.  |
| `ServiceMode`             | Starts the app in its service mode. Useful in combination with `Start` and `Temp`. |
| `Quit`                    | Closes running instances of SKIF. |
| `Minimize`                | Launches SKIF minimized *or* minimizes any running instances of SKIF, to the taskbar or notification area depending on configuration. |
| `"<path>.exe"`            | Uses SKIF as a launcher to start the injection service, launch another application, and then stop the service. Any arguments specified after the path is proxied to the launched application. Called through `SKIF %COMMAND%` from within the Steam client. A `.bat` file can be used as well. |
| `SKIF_SteamAppID=<int>`   | Sets a specific Steam App ID as the environment variable when used as a launcher or launching a custom game. This can replace the use of a special `steam_appid.txt` file in the game folder. |
| `SKIF_SteamAppIDtxt=0`    | Detects and removes `steam_appid.txt` file from the working directory. Useful if the Steam client recreates the file when using the `SKIF %COMMAND` launch option. |
| `SKIF_URI=<string>`       | Uses SKIF as a launcher to call a specific command, as a replacement of the "<path>.exe" cmd line argument. This is used for Steam and Epic games in the app jump list.<br>Combine with `Start Temp` to also start the service temporarily.  |
| `SKIF_XboxApp=<string>`   | Launches an Xbox / Microsoft Store game through Instant Play. Used for Xbox games in the app jump list. |
| `AddGame="<path>"`        | Adds the specified application to the library of SKIF as a custom game. |
| `RestartDisplDrv`         | **Requires elevation!** Restarts the display driver (useful as this can sometimes fix MPOs). |
| `RestartDWM`              | **Requires elevation!** Restarts the Desktop Window Manager (dwm.exe). |
| `DisableOverlayMode`      | **Requires elevation!** Sets registry values that disable MPOs on the system. |
| `ResetOverlayMode`        | **Requires elevation!** Resets registry values that controls the state of MPOs on the system. |
| `RunUpdater`              | Triggers a check for update. Used internally by the "Check for Updates" taskbar jump list action. |

## Keyboard shortcuts

| Shortcut&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `F5` *or* `Ctrl+R`        | Forces a refresh of data on the Library, Hardware, and Settings tabs. |
| `F6`                      | Appearance: Toggles DPI scaling.                        |
| `F7`                      | Appearance: Cycles between available color themes.      |
| `F8`                      | Appearance: Toggles borders.                            |
| `F9`                      | Appearance: Toggles color depth.                        |
| `F11` *or* `Ctrl+T`       | Appearance: Toggles between regular (large) UI mode and service (small) UI mode. |
| `Ctrl+Q` *or* `Ctrl+W`    | Closes the app. `Ctrl+W` also closes the INI Editor, Local Install Manager, Properties, and other secondary windows. |
| `Ctrl+Shift+N`            | Opens a new INI Editor window.                          |
| `Ctrl+Shift+M`            | Opens the Local Install Manager.                        |
| `Ctrl+1`                  | Switch to the Library tab.                              |
| `Ctrl+2`                  | Switch to the Hardware tab.                             |
| `Ctrl+3`                  | Switch to the Monitor tab.                              |
| `Ctrl+4`                  | Switch to the Settings tab.                             |
| `Ctrl+5`                  | Switch to the About tab.                                |
| `Esc`                     | Closes the open popup.                                  |

### Library

| Shortcut&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `Ctrl+A`                  | Opens the add game prompt.                              |
| `Ctrl+F` *or* `F3`        | Selects the filter field.                               |
| `Enter` *or* gamepad `A`  | Launches the selected game. Hold to open its context menu instead. |
| `Ctrl+Click` / `Shift+Click` | Selects multiple games, to change their category or favorite state at once. |
| `Esc`                     | Clears the multi-selection.                             |

### INI Editor

| Shortcut&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `Ctrl+N`                  | New file.                                               |
| `Ctrl+Shift+N`            | New window.                                             |
| `Ctrl+O`                  | Open file.                                              |
| `Ctrl+S`                  | Save.                                                   |
| `Ctrl+Shift+S`            | Save as.                                                |
| `Ctrl+F` *or* `F3`        | Selects the filter field.                               |

### Local Install Manager

| Shortcut&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `Ctrl+A`                  | Selects the outdated installs, then all of them.        |
| `Ctrl+N`                  | Installs Special K next to a new executable.            |

### Global hotkeys

These work system-wide, even when SKIF is minimized, and can be rebound on the Settings tab.

| Default shortcut&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `Ctrl+Windows+Shift+H`    | Toggles HDR on the display. Requires Windows 10 1709 or newer and an HDR-capable display. |
| `Windows+Shift+Insert`    | Starts the injection service temporarily, and whitelists the game in the foreground. |

## Third-party code

* Uses [Dear ImGui](https://github.com/ocornut/imgui), licensed under [MIT](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).
* Uses [ImGuiNotify](https://github.com/TyomaVader/ImGuiNotify), licensed under [MIT](https://github.com/TyomaVader/ImGuiNotify/blob/Dev/LICENSE).
* Uses [libwebp](https://chromium.googlesource.com/webm/libwebp), licensed under [BSD-3-Clause](https://chromium.googlesource.com/webm/libwebp/+/refs/heads/main/COPYING).
* Uses [ini-cpp](https://github.com/SSARCandy/ini-cpp), licensed under [BSD-3-Clause](https://github.com/SSARCandy/ini-cpp/blob/master/LICENSE.txt).
* Uses [MinHook](https://github.com/TsudaKageyu/minhook), licensed under [BSD-2-Clause](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt).
* Uses [stb_image](https://github.com/nothings/stb), licensed under [MIT or public domain](https://github.com/nothings/stb/blob/master/LICENSE).
* Uses the [SteamDB File Detection Rule Sets](https://github.com/SteamDatabase/FileDetectionRuleSets), licensed under [MIT](https://github.com/SteamDatabase/FileDetectionRuleSets/blob/main/LICENSE).
* Uses [DirectX Texture Library](http://go.microsoft.com/fwlink/?LinkId=248926), licensed under [MIT](https://github.com/microsoft/DirectXTex/blob/main/LICENSE).
* Uses [Font Awesome Free v6.2.1](https://fontawesome.com/v6/download), licensed under [SIL OFL 1.1 License](https://scripts.sil.org/OFL).
* Uses [JSON for Modern C++](https://github.com/nlohmann/json), licensed under [MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT).
* Uses [PicoSHA2](https://github.com/okdshin/PicoSHA2), licensed under [MIT](https://github.com/okdshin/PicoSHA2/blob/master/LICENSE).
* Uses [Plog](https://github.com/SergiusTheBest/plog), licensed under [MIT](https://github.com/SergiusTheBest/plog/blob/master/LICENSE).
* Uses [pugixml](https://pugixml.org/), licensed under [MIT](https://pugixml.org/license.html).
* Uses [ValveFileVDF](https://github.com/TinyTinni/ValveFileVDF), licensed under [MIT](https://github.com/TinyTinni/ValveFileVDF/blob/master/LICENSE).
* Uses [TextFlowCpp](https://github.com/catchorg/textflowcpp), licensed under [BSL-1.0](https://github.com/catchorg/textflowcpp/blob/master/LICENSE.txt).
* Uses [HybridDetect](https://github.com/GameTechDev/HybridDetect/), licensed under [MIT](https://github.com/GameTechDev/HybridDetect/blob/main/LICENSE.md).
* Uses code from [win32-darkmode](https://github.com/ysc3839/win32-darkmode/), licensed under [MIT](https://github.com/ysc3839/win32-darkmode/blob/master/LICENSE).
* Uses code from [Custom Resolution Utility (CRU)](https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU), licensed under a [custom license](https://github.com/SpecialKO/SKIF/blob/master/src/utility/drvreset.cpp).
* Uses code from [ImHex](https://github.com/WerWolv/ImHex), licensed under [GPL-2.0](https://github.com/WerWolv/ImHex/blob/master/LICENSE).
* Includes various snippets of code from [Stack Overflow](https://stackoverflow.com/), licensed under [Creative Commons Attribution-ShareAlike](https://stackoverflow.com/help/licensing).

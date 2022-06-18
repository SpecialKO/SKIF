# Special K Injection Frontend (SKIF)
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
| -------------: | ------------- |
| `<empty>`  | Launches SKIF.  |
| `Start`  | Launches SKIF and starts an instance of the injection service. |
| `Temp` | Used with `Start` to indicate the injection service should auto-stop after a successful injection. |
| `Stop`  | Launches SKIF stops any running instances of the injection service. Can also be used to attempt to force-eject leftover injections of Special K.  |
| `Quit`  | Closes any running instances of SKIF. |
| `Minimize`  | Launches SKIF minimized *or* minimizes any running instances of SKIF, to the taskbar or notification area depending on configuration. |
| `"<path>.exe"` | Uses SKIF as a launcher to start the injection service, launch another application, and then stop the service. Any arguments specified after the path is proxied to the launched application. Called through `SKIF %COMMAND%` from within the Steam client. |
| `AddGame="<path>"`  | **Experimental!** Adds the specified application to the library of SKIF as a custom game. |

#pragma once

#include <windows.h>
#include <string>

// Per-executable and per-folder launch behaviors (SpecialKO/SKIF#28)
//
// Stored in the registry so they can be resolved from nothing but an executable path,
//   without parsing the library or relying on files placed next to the game:
//
//   HKCU\SOFTWARE\Kaldaien\Special K\Launch Behaviors
//     <normalized path of an executable or folder>   REG_DWORD   <fields>
//
// The value name is the full path, lowercased, without a trailing backslash (see NormalizePath).
//
// Every field uses 0 for "inherit". Resolve ( ) reads the executable's own entry first and then
//   each parent folder up to the root, and for each field keeps the first non-zero value it finds.
//   Fields that remain 0 fall back to the global settings.
//
//   Bits  0- 1  Auto-stop       0 = inherit, 1 = on injection, 2 = on game exit,     3 = never
//   Bits  2- 3  Service         0 = inherit, 1 = use service,  2 = (unused),         3 = never use service (blocked)
//   Bits  4- 5  steam_appid.txt 0 = inherit, 1 = remove,       2 = keep
//   Bits  6- 7  Blacklist       0 = inherit, 1 = ignore,       2 = respect   (Service = blocked, and the blacklist patterns)
//   Bits  8- 9  Elevation       0 = inherit, 1 = normal,       2 = elevated  (of the service, whenever it is used)
//   Bits 10-23  Reserved for future fields (preserved when writing)
//   Bits 24-31  Format version  (0 is accepted for hand-written values; newer versions are left untouched)

namespace SKIF_LaunchBehaviors
{
  enum class AutoStop : DWORD {
    Inherit      = 0,
    OnInjection  = 1,
    OnGameExit   = 2,
    Never        = 3
  };

  // Whether the executable uses the injection service.
  //   Also applies when Blacklist::Ignore is set.
  enum class Service : DWORD {
    Inherit      = 0,
    Normal       = 1, // Use the service (also clears an inherited block)
    Blocked      = 3  // 2 is unused (it used to be the elevated service, before Elevation)
  };

  // Whether to use an elevated service.
  enum class Elevation : DWORD {
    Inherit      = 0,
    Normal       = 1, // The service is not elevated (also clears an inherited elevation)
    Elevated     = 2
  };

  // Should SKIF remove steam_appid.txt next to the executable?
  enum class SteamAppIDtxt : DWORD {
    Inherit      = 0,
    Remove       = 1,
    Keep         = 2
  };

  // Whether the service is kept from being used for an executable that Special K is disabled for: a block of the
  //   executable itself (Service::Blocked, "Disable Special K"), or a blacklist pattern of the Settings tab (and the
  //     built-in ones). Ignoring it only starts the service: Special K stays disabled for the executable itself, which is
  //       then not whitelisted. Meant for a launcher that is blocked, so that the game it starts gets Special K.
  enum class Blacklist : DWORD {
    Inherit      = 0,
    Ignore       = 1, // Use the service even if Special K is disabled for the executable
    Respect      = 2  // Also clears an inherited Ignore
  };

  constexpr DWORD ShiftAutoStop      =  0;
  constexpr DWORD MaskAutoStop       =  0x3u << ShiftAutoStop;
  constexpr DWORD ShiftService       =  2;
  constexpr DWORD MaskService        =  0x3u << ShiftService;
  constexpr DWORD ShiftSteamAppIDtxt =  4;
  constexpr DWORD MaskSteamAppIDtxt  =  0x3u << ShiftSteamAppIDtxt;
  constexpr DWORD ShiftBlacklist     =  6;
  constexpr DWORD MaskBlacklist      =  0x3u << ShiftBlacklist;
  constexpr DWORD ShiftElevation     =  8;
  constexpr DWORD MaskElevation      =  0x3u << ShiftElevation;

  constexpr DWORD MaskKnownFields    =  MaskAutoStop | MaskService | MaskSteamAppIDtxt | MaskBlacklist | MaskElevation;
  constexpr DWORD MaskFields         =  0x00FFFFFFu;
  constexpr DWORD ShiftVersion       = 24;
  constexpr DWORD MaskVersion        =  0xFFu << ShiftVersion;
  constexpr DWORD CurrentVersion     =  1;

  constexpr const wchar_t* RegistryPath = LR"(SOFTWARE\Kaldaien\Special K\Launch Behaviors)";

  struct Resolved {
    AutoStop      autoStop      = AutoStop::Inherit;
    Service       service       = Service::Inherit;
    SteamAppIDtxt steamAppIDtxt = SteamAppIDtxt::Inherit;
    Blacklist     blacklist     = Blacklist::Inherit;
    Elevation     elevation     = Elevation::Inherit;
  };

  // Full path, backslashes only, lowercased, no trailing backslash
  std::wstring NormalizePath    (const std::wstring& path);

  // Resolves the effective behaviors for an executable (or folder) path
  Resolved     Resolve          (const std::wstring& path);

  // What the path inherits from its parent folders (its own entry is skipped); fields that remain 0 use the global settings
  Resolved     ResolveParent    (const std::wstring& path);

  // The path's own entry, without inheritance. Returns false if it has none (out is then all 0)
  bool         ReadExact        (const std::wstring& path, Resolved& out);

  // Sets one field of the exact entry for the path. The entry is removed once all its fields are 0.
  //   Returns false if the registry could not be written, or if the entry uses a newer format.
  bool         SetAutoStop      (const std::wstring& path, AutoStop      value);
  bool         SetService       (const std::wstring& path, Service       value);
  bool         SetSteamAppIDtxt (const std::wstring& path, SteamAppIDtxt value);
  bool         SetBlacklist     (const std::wstring& path, Blacklist     value);
  bool         SetElevation     (const std::wstring& path, Elevation     value);
};

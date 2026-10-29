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
//   Bits  2- 3  Service         0 = inherit, 1 = use service,  2 = elevated service, 3 = never use service (blocked)
//   Bits  4- 5  steam_appid.txt 0 = inherit, 1 = remove,       2 = keep
//   Bits  6-23  Reserved for future fields (preserved when writing)
//   Bits 24-31  Format version  (0 is accepted for hand-written values; newer versions are left untouched)

namespace SKIF_LaunchBehaviors
{
  enum class AutoStop : DWORD {
    Inherit      = 0,
    OnInjection  = 1,
    OnGameExit   = 2,
    Never        = 3
  };

  enum class Service : DWORD {
    Inherit      = 0,
    Normal       = 1, // Use the service, not elevated (also clears an inherited block or elevation)
    Elevated     = 2,
    Blocked      = 3
  };

  enum class SteamAppIDtxt : DWORD {
    Inherit      = 0,
    Remove       = 1,
    Keep         = 2
  };

  constexpr DWORD ShiftAutoStop      =  0;
  constexpr DWORD MaskAutoStop       =  0x3u << ShiftAutoStop;
  constexpr DWORD ShiftService       =  2;
  constexpr DWORD MaskService        =  0x3u << ShiftService;
  constexpr DWORD ShiftSteamAppIDtxt =  4;
  constexpr DWORD MaskSteamAppIDtxt  =  0x3u << ShiftSteamAppIDtxt;

  constexpr DWORD MaskKnownFields    =  MaskAutoStop | MaskService | MaskSteamAppIDtxt;
  constexpr DWORD MaskFields         =  0x00FFFFFFu;
  constexpr DWORD ShiftVersion       = 24;
  constexpr DWORD MaskVersion        =  0xFFu << ShiftVersion;
  constexpr DWORD CurrentVersion     =  1;

  constexpr const wchar_t* RegistryPath = LR"(SOFTWARE\Kaldaien\Special K\Launch Behaviors)";

  struct Resolved {
    AutoStop      autoStop      = AutoStop::Inherit;
    Service       service       = Service::Inherit;
    SteamAppIDtxt steamAppIDtxt = SteamAppIDtxt::Inherit;
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
};

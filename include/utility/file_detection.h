#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Detection of the technology a game uses (engine, anti-cheat, SDKs, ...) from the names of its files
//
//   Uses SteamDB's file detection rule sets: https://github.com/SteamDatabase/FileDetectionRuleSets (MIT license)
//
//     rules.ini holds one regular expression per rule, grouped into sections (Engine, Evidence, Container, Emulator,
//       AntiCheat, SDK, Launcher). A rule is known by its section and name, e.g. Engine.Unity or AntiCheat.BattlEye.
//     The expressions run on each path relative to the game's folder: lowercase, / as the separator, folders
//       included (e.g. "game/bin/win64/dota2.exe"). Each file or folder counts for at most one rule of each section.
//     Evidence rules are weak hints that are never reported on their own: a second pass (DeduceEngine ( )) turns
//       combinations of them into an engine, as SteamDB's tests/FileDetector.php does.
//     The rules are guesses from file names only, with false positives and misses: use them as hints, never as facts.
//
//   rules.ini is downloaded to Assets\Cache\FileDetectionRules.ini and refreshed once a week (not in low bandwidth
//     mode). Results are cached per folder in Assets\Cache\FileDetection.json, and a folder is scanned again when its
//     top level changes, the rules change, or after 30 days.
//
//   Scans run on a background thread of their own. Results are looked up by folder, so anything with a folder can be
//     scanned, not only games of the library. MatchPaths ( ) works on any list of paths (e.g. a depot's file list).

namespace SKIF_FileDetection
{
  // The sections of rules.ini
  enum class Category {
    Engine,
    Evidence,  // Weak hints for the second pass; never part of a result
    Container, // e.g. Electron
    Emulator,  // e.g. DOSBox
    AntiCheat,
    SDK,
    Launcher,
    Other      // A section added to rules.ini after this was written
  };

  Category         CategoryFromSection (std::string_view section); // "AntiCheat" -> Category::AntiCheat
  const char*      CategoryToSection   (Category category);        // Category::AntiCheat -> "AntiCheat" ("" for Other)
  const char*      GetCategoryLabel    (Category category);        // Translated, for the UI ("Anti-cheat")

  // "AMD_FidelityFX_FSR2" -> "AMD FidelityFX FSR2", "Lime_OR_OpenFL" -> "Lime / OpenFL"
  std::string      GetDisplayName      (std::string_view name);

  struct detection_s {
    std::string id;               // "Engine.Unity"
    std::string name;             // "Unity" (the part after the section)
    Category    category = Category::Other;
    uint32_t    matches  = 0;     // Files and folders that matched; 0 for an engine deduced from evidence
    std::string example;          // The first path that matched (relative, lowercase); empty if deduced
    bool        deduced  = false; // Deduced from Evidence rules (second pass)
  };

  struct result_s {
    enum class State {
      None,      // Never requested
      Pending,   // Queued, or being scanned
      Done,
      Failed,    // The folder could not be read (missing, access denied)
      NoRules    // rules.ini is not available (never downloaded and the download failed)
    };

    State                     state     = State::None;
    std::vector <detection_s> detections; // Sorted by category, then name; Evidence rules are left out
    uint32_t                  entries   = 0;     // Files and folders scanned
    bool                      truncated = false; // The folder had more entries than are scanned (see kMaxEntries)
    int64_t                   scanned   = 0;     // When the scan was done (UNIX time)
    bool                      stale     = false; // From the cache and not yet checked against the folder; a new scan may follow

    bool                       Has      (std::string_view id)  const; // Has ("AntiCheat.EasyAntiCheat")
    bool                       Has      (Category category)    const;
    const detection_s*         Find     (std::string_view id)  const;
    std::vector <const detection_s*>
                               Get      (Category category)    const;
    std::string                GetNames (Category category, const char* separator = ", ") const; // Display names
  };

  using result_ptr = std::shared_ptr <const result_s>;

  // Limits of a scan
  constexpr uint32_t kMaxEntries = 250000; // Files and folders; the rest are left out (result_s::truncated)
  constexpr int      kMaxDepth   = 32;

  // Any thread. The result for a folder (no trailing backslash); never null. A folder that has no current result is
  //   queued for a scan unless request is false: the result is then Pending until the scan is done, which wakes the
  //   main thread with WM_SKIF_FILEDETECTION. A cached result is returned right away and checked in the background.
  result_ptr       Get                 (const std::wstring& folder, bool request = true);

  // Any thread. Scans the folder again even if the cached result is current (e.g. a Rescan button)
  void             Rescan              (const std::wstring& folder);

  // Any thread. Downloads rules.ini again (unless in low bandwidth mode) and rescans the folders scanned so far as
  //   they are requested again
  void             RefreshRules        (void);

  // Worker threads only (blocking). Scans a folder right away, without the cache or the queue. Uses the rules file
  //   as already downloaded (NoRules if there is none yet), and reads it once per session.
  result_s         ScanSync            (const std::wstring& folder);

  // rules.ini, parsed and compiled. Portable (no Windows API), so it can also be used on file lists from elsewhere.
  class RuleSet
  {
  public:
    RuleSet  (void);
    ~RuleSet (void);

    RuleSet  (const RuleSet&)            = delete;
    RuleSet& operator= (const RuleSet&)  = delete;

    // Replaces the rules. Rules whose expression can't be used are skipped and listed in errors ("Engine.Foo: ...").
    //   False if no rule could be used.
    bool         Parse        (std::string_view ini, std::vector <std::string>* errors = nullptr);

    size_t       GetRuleCount (void) const;
    uint64_t     GetHash      (void) const; // Of the text that was parsed (FNV-1a), to tell when cached results are outdated

    // Incremental matching of one folder: Add ( ) every path (relative, / or \ separators, any case), then Finish ( )
    class Matcher
    {
    public:
      explicit Matcher (const RuleSet& rules);
      ~Matcher         (void);

      void      Add    (std::string_view path, bool isFolder);
      result_s  Finish (void); // state is Done; entries, truncated and scanned are left to the caller

    private:
      struct impl_s;
      std::unique_ptr <impl_s> impl;
    };

    // Matches a whole list of paths at once (files only)
    result_s     MatchPaths   (const std::vector <std::string>& paths) const;

  private:
    struct impl_s;
    std::unique_ptr <impl_s> impl;

    friend class Matcher;
  };

  // Internal, exposed for tests: converts the PCRE syntax rules.ini uses to ECMAScript (std::regex): possessive
  //   quantifiers (\w++) and atomic groups (?>...) become their plain forms, and escaped characters that need no
  //   escaping (\_) are unescaped. The result is lowercase, apart from escape sequences (\W stays \W).
  std::string      ConvertPattern      (std::string_view pcre);

  // Internal, exposed for tests: a substring that every match of a converted pattern contains ("" if none is known),
  //   used to skip running the expression on paths that can't match
  std::string      GetRequiredLiteral  (std::string_view pattern);
};

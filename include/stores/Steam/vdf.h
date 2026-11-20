#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <wtypes.h>
//#include <steam/isteamuser.h>
#include "app_record.h"

// Steamworks API definitions
typedef unsigned __int32 uint32;
typedef unsigned __int64 uint64;
typedef uint32 AppId_t;
typedef uint32 DepotId_t;
typedef uint64 PublishedFileId_t;
typedef uint64 ManifestId_t;
typedef uint64 UGCHandle_t;

#pragma pack(push)
#pragma pack(1)
// Steam's app info cache, in either of its two formats:
//  - appcache\appinfo.vdf:             a single file (versions 0x27 - 0x29)
//  - appcache\appinfo\appinfo_XX.vdf:  split into 256 files (version 0x2a, Steam beta of November 2026), each with
//                                        an index, its own string table and the apps' data (see bucket_header_s)
// If both are present (e.g. after leaving the beta), the most recently written one is used.
class skValveDataFile
{
public:
  skValveDataFile (std::wstring appcache); // Steam's appcache folder

  static constexpr
    uint32_t _LastSteamApp = 0;

  static
    uint32_t vdf_version;

  // The split format (0x2a): the files' version, and the header of each file
  static constexpr
    uint32_t _SplitVersion = 0x2a;

  struct bucket_header_s
  {
    DWORD     version;      // 0x07564400 | version (0x2a)
    DWORD     universe;
    uint32_t  bucket;       // XX of appinfo_XX.vdf
    uint32_t  num_buckets;  // 256
    uint32_t  num_apps;
    uint32_t  index_size;   // The index follows the header: per app, a bucket_entry_s and its entry_size bytes of metadata
    uint32_t  num_strings;  // The string table follows the index: num_strings null-terminated strings
    uint32_t  strings_size;
    uint32_t  data_size;    // The apps' data follows the string table: per app, a SHA-1 (20 bytes) and its key-values
    uint64_t  checksum;
  };

  struct bucket_entry_s
  {
     AppId_t  appid;
    uint32_t  offset;       // Of the app's data, from the start of the data
    uint32_t  size;
    uint32_t  entry_size;   // Of the metadata that follows (change number, last update, access token, SHA-1s, ...)
  };

  struct appinfo27_s
  {
       AppId_t appid;
      uint32_t size;
      uint32_t state;
    __time32_t last_update;
      uint64_t access_token;
       uint8_t sha1sum [20];
      uint32_t change_num;
  };

  struct appinfo28_s : appinfo27_s
  {
       uint8_t sha1_sec [20]; // Added December 2022
  };

  struct appinfo_s : appinfo28_s
  {
    struct section_desc_s {
      void*  blob;
      size_t size;
    };

    struct section_s {
      enum _TokenOp  {
        SectionBegin = 0x0,
        String       = 0x1,
        Int32        = 0x2,
        Int64        = 0x7,
        SectionEnd   = 0x8
      };

      using _kv_pair =
        std::pair <const char*, std::pair <_TokenOp, void *>>;

      struct section_data_s {
        std::string            name;
        section_desc_s         desc;
        std::vector <_kv_pair> keys;
      };

      std::vector <
        section_data_s
      > finished_sections;

      // strs: the string table the key names index (nullptr: the names are inline, before June 2024)
      void parse (section_desc_s& desc, const std::vector <char *>* strs);
    };

    void*      getRootSection (size_t* pSize = nullptr);
  };

  // Parses the app's data into its record (or a dummy one, if not in apps). false: not in the file, or already processed
  bool       getAppInfo ( uint32_t     appid, app_list_t *apps );

  // Whether any app info was read
  bool       isLoaded   (void) const { return ! apps_index.empty ( ); }

  struct header_s
  {
    DWORD     version;
    DWORD     universe;
    // String table pointer (not shown here) added in 0x29
    appinfo_s head;
  };

  // Added in June 2024 (version 0x29)
  struct str_tbl_s
  {
    DWORD     num_strings;
    uint8_t   strings [1];
  };

  // The single file (appinfo.vdf)
  header_s*  base  = nullptr;
  appinfo_s* root  = nullptr;
  str_tbl_s* table = nullptr;

protected:
private:
  // An app's key-values, and the string table their names index (nullptr: inline names)
  struct app_entry_s
  {
    void*                       blob;
    size_t                      size;
    const std::vector <char *>* strs;
  };

  // A file of the split format: its contents, and its string table
  struct bucket_s
  {
    std::vector <BYTE>   data;
    std::vector <char *> strs;
  };

  bool _ReadFile        (const std::wstring& file, std::vector <BYTE>& data);
  bool _LoadSingleFile  (const std::wstring& file);
  bool _LoadSplitFiles  (const std::wstring& folder, const std::vector <std::wstring>& files);

  std::wstring          path;
  std::vector <BYTE>   _data;
  std::vector <char *>  strs; // Preparsed array of pointers
  std::vector <std::unique_ptr <bucket_s>>
                        buckets;    // The split format: one per file (unique_ptr, as the entries point into them)
  std::unordered_map <AppId_t, app_entry_s>
                        apps_index; // App ID -> its data, built once so lookups do not walk the file(s)
};

#pragma pack(pop)
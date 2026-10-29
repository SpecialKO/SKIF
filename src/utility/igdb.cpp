#include <utility/igdb.h>
#include <nlohmann/json.hpp>

#include <wtypes.h>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <set>
#include <atomic>
#include <iterator>

#include <utility/registry.h>
#include <utility/utility.h>

// See igdb.h

namespace
{
  constexpr const wchar_t* IGDB_IMAGE_BASE = L"https://images.igdb.com/igdb/image/upload/";

  // When a lookup fails for any other reason than a miss (no response, rate limited, proxy error), lookups to that
  //   proxy pause for a while instead of the game being remembered as a miss, so a temporary failure does not hide
  //   covers until a restart
  constexpr DWORD FAILURE_PAUSE = 60000; // ms

  // A cover proxy (both have the same API: GET /v1/{store}/{id} and, IGDB only, GET /v1/search/{name})
  struct cover_proxy_s {
    const wchar_t*          base;          // e.g. https://igdb.special-k.info/v1/
    const char*             name;          // For the log

    // Lookups (paths below base) that found no cover this session, so a game is not queried again on every selection
    //   (the proxies cache misses too, but this also avoids the request). Retried on the next launch of SKIF.
    std::mutex              missesMutex;
    std::set <std::wstring> misses;

    std::atomic <DWORD>     dwLastFailure; // SKIF_Util_timeGetTime ( ) of the last failure; 0 = none

    cover_proxy_s (const wchar_t* base_, const char* name_) : base (base_), name (name_), dwLastFailure (0) { }
  };

  cover_proxy_s igdb (L"https://igdb.special-k.info/v1/", "IGDB");
  cover_proxy_s pcgw (L"https://pcgw.special-k.info/v1/", "PCGW");

  // Percent-encodes a UTF-8 string for a URL path segment or query value (a "/" becomes %2F)
  std::wstring
  SKIF_IGDB_UrlEncode (const std::string& value)
  {
    std::string out;

    for (unsigned char ch : value)
    {
      if (std::isalnum (ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
        out += static_cast <char> (ch);
      else
        out += SK_FormatString ("%%%02X", ch);
    }

    return SK_UTF8ToWideChar (out);
  }

  // A string value of a JSON object, or an empty string if it is missing or not a string (e.g. null)
  std::string
  SKIF_CoverProxy_GetString (const nlohmann::json& object, const char* key)
  {
    if (! object.is_object ( ) || ! object.contains (key) || ! object[key].is_string ( ))
      return "";

    return object[key].get <std::string> ( );
  }

  // Remembers a lookup that found no (usable) cover, so it is not repeated this session; always false
  bool
  SKIF_CoverProxy_RememberMiss (cover_proxy_s& proxy, const std::wstring& path)
  {
    std::scoped_lock lock (proxy.missesMutex);
    proxy.misses.insert (path);
    return false;
  }

  // Looks up path on a proxy (e.g. steam/220 or search/Celeste) and parses its response into json.
  //   True for a 200 response with a cover object. A 404 / 400 or an unexpected response is remembered as a miss;
  //   any other failure pauses lookups to that proxy for FAILURE_PAUSE.
  bool
  SKIF_CoverProxy_Lookup (cover_proxy_s& proxy, const std::wstring& path, nlohmann::json& json)
  {
    {
      std::scoped_lock lock (proxy.missesMutex);

      if (proxy.misses.count (path) != 0)
        return false;
    }

    DWORD dwFailure = proxy.dwLastFailure.load ( );

    if (dwFailure != 0 && SKIF_Util_timeGetTime ( ) - dwFailure < FAILURE_PAUSE)
      return false;

    std::wstring url = std::wstring (proxy.base) + path;
    std::string  response;

    DWORD dwStatus = 0;

    if (! SKIF_Util_GetWebResource (url, L"", L"GET", L"", "", L"", &response, &dwStatus) || response.empty ( ))
    {
      // 404: the source has no such game, or no cover for it. 400: a lookup the proxy refuses. Both are final for this session.
      if (dwStatus == 404 || dwStatus == 400)
      {
        PLOG_VERBOSE << "No " << proxy.name << " cover found for " << path;
        return SKIF_CoverProxy_RememberMiss (proxy, path);
      }

      // Anything else is temporary (no response, 429 rate limited, 5xx): pause lookups, then try again
      PLOG_WARNING << proxy.name << " cover lookup failed (HTTP status " << dwStatus << ") for " << path << "; retrying in " << (FAILURE_PAUSE / 1000) << " seconds at the earliest.";
      proxy.dwLastFailure.store (SKIF_Util_timeGetTime ( ));
      return false;
    }

    proxy.dwLastFailure.store (0);

    json = nlohmann::json::parse (response, nullptr, false);

    if (json.is_discarded ( ) || ! json.is_object ( ) || ! json.contains ("cover") || ! json["cover"].is_object ( ))
    {
      PLOG_WARNING << "Unexpected response from the " << proxy.name << " proxy for " << path;
      return SKIF_CoverProxy_RememberMiss (proxy, path);
    }

    return true;
  }

  // The proxy's name for an IGDB external game source (any other source is sent by number)
  std::wstring
  SKIF_IGDB_GetStorePath (SKIF_IGDB_Source source)
  {
    switch (source)
    {
      case SKIF_IGDB_Source::Steam:           return L"steam";
      case SKIF_IGDB_Source::GOG:             return L"gog";
      case SKIF_IGDB_Source::Microsoft:       return L"microsoft";
      case SKIF_IGDB_Source::EpicGamesStore:  return L"epic";
      case SKIF_IGDB_Source::itch:            return L"itch";
      case SKIF_IGDB_Source::XboxMarketplace: return L"xbox";
    }

    return std::to_wstring (static_cast <int> (source));
  }

  // [igdb-names-begin]
  // A name reduced to what identifies the game, to tell whether a fuzzy match is the same game:
  //   lower-cased words of letters and digits (punctuation, ® ™ © and other symbols dropped, apostrophes removed),
  //   "&" read as "and", and Roman numerals II to XX read as numbers.
  //   "The Settlers II: History Edition" and "The Settlers 2 - History Edition" -> "the settlers 2 history edition"
  std::string
  SKIF_IGDB_NormalizeName (const std::string& name)
  {
    static const char* numerals [] = {
      "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x",
      "xi", "xii", "xiii", "xiv", "xv", "xvi", "xvii", "xviii", "xix", "xx"
    };

    std::vector <std::string> words;
    std::string               word;

    auto _EndWord = [&](void)
    {
      if (word.empty ( ))
        return;

      for (size_t i = 0; i < std::size (numerals); i++)
      {
        if (word == numerals [i])
        {
          word = std::to_string (i + 2);
          break;
        }
      }

      words.push_back (word);
      word.clear ( );
    };

    for (size_t i = 0; i < name.size ( ); i++)
    {
      const unsigned char ch = static_cast <unsigned char> (name [i]);

      // Apostrophes do not split words: Assassin's, Assassin’s (U+2019) and Assassins are the same
      if (ch == '\'')
        continue;

      if (ch == 0xE2 && name.compare (i, 3, "\xE2\x80\x99") == 0)
      {
        i += 2;
        continue;
      }

      if (ch < 0x80 && std::isalnum (ch))
        word += static_cast <char> (std::tolower (ch));

      else
      {
        _EndWord ( );

        if (ch == '&')
          words.push_back ("and");
      }
    }

    _EndWord ( );

    std::string normalized;

    for (auto& w : words)
      normalized += ((normalized.empty ( )) ? "" : " ") + w;

    return normalized;
  }

  // Whether two names are the same game name, written differently (see SKIF_IGDB_NormalizeName)
  bool
  SKIF_IGDB_IsSameName (const std::string& a, const std::string& b)
  {
    std::string na = SKIF_IGDB_NormalizeName (a);

    return (! na.empty ( ) && na == SKIF_IGDB_NormalizeName (b));
  }
  // [igdb-names-end]

  // Looks up path on the IGDB proxy and downloads the cover it names; true if a cover was downloaded.
  //   searchedName: the name looked up (name searches only); a fuzzy match of the same name written differently
  //   (The Settlers 2 - History Edition / The Settlers II: History Edition) is accepted as an exact one.
  bool
  SKIF_IGDB_QueryAndDownload (const std::wstring& path, const std::wstring& targetPath, bool allowFuzzy, const std::string& searchedName = "")
  {
    static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

    nlohmann::json json;

    if (! SKIF_CoverProxy_Lookup (igdb, path, json))
      return false;

    // id: by store ID. exact: a name search with the same name. base: a name search that matched once a trailing
    //   edition part was removed (the main game's cover). fuzzy: IGDB's top search result, which can be another game.
    std::string match    = SKIF_CoverProxy_GetString (json,         "match");
    std::string name     = SKIF_CoverProxy_GetString (json["game"], "name");
    std::string image_id = SKIF_CoverProxy_GetString (json["cover"], "image_id");

    if (match == "fuzzy" && ! allowFuzzy && ! searchedName.empty ( ) && SKIF_IGDB_IsSameName (searchedName, name))
    {
      PLOG_VERBOSE << "Accepting the fuzzy IGDB match for " << path << " as the same name: " << SK_UTF8ToWideChar (name);
      match = "same name";
    }

    if (match == "fuzzy" && ! allowFuzzy)
    {
      PLOG_VERBOSE << "Ignoring the fuzzy IGDB match for " << path << ": " << SK_UTF8ToWideChar (name);
      return SKIF_CoverProxy_RememberMiss (igdb, path);
    }

    // The image ID goes into a URL and a file is written from it: only accept what IGDB uses (e.g. co27c8)
    if (image_id.empty ( ) || image_id.size ( ) > 64 ||
        ! std::all_of (image_id.begin ( ), image_id.end ( ), [](unsigned char ch) { return std::isalnum (ch) || ch == '_'; }))
    {
      PLOG_WARNING << "Invalid IGDB image ID for " << path;
      return SKIF_CoverProxy_RememberMiss (igdb, path);
    }

    // t_1080p fits the image within 1920x1080 (a 3:4 cover becomes 810x1080); t_cover_big_2x is 528x748
    //   (the proxy's cover.url is always t_cover_big_2x, so the URL is built from the image ID instead)
    std::wstring size     = (_registry.bLowBandwidthMode) ? L"t_cover_big_2x" : L"t_1080p";
    std::wstring imageUrl = std::wstring (IGDB_IMAGE_BASE) + size + L"/" + SK_UTF8ToWideChar (image_id) + L".jpg";

    std::error_code ec;
    std::filesystem::create_directories (std::filesystem::path (targetPath).parent_path ( ), ec);

    if (! SKIF_Util_GetWebResource (imageUrl, targetPath))
    {
      PLOG_WARNING << "Failed to download the IGDB cover " << imageUrl;
      return false;
    }

    PLOG_INFO << "Downloaded the IGDB cover of " << SK_UTF8ToWideChar (name) << " (" << match.c_str ( ) << " match) to " << targetPath;

    return true;
  }
}

bool
SKIF_IGDB_DownloadCover (SKIF_IGDB_Source source, const std::string& uid, const std::wstring& targetPath)
{
  if (uid.empty ( ) || targetPath.empty ( ))
    return false;

  return SKIF_IGDB_QueryAndDownload (SKIF_IGDB_GetStorePath (source) + L"/" + SKIF_IGDB_UrlEncode (uid), targetPath, false);
}

bool
SKIF_IGDB_DownloadCoverByName (const std::string& name, const std::wstring& targetPath, bool allowFuzzy)
{
  if (name.empty ( ) || targetPath.empty ( ))
    return false;

  // "." and ".." are path segments of their own, so a name of only dots would not reach the search
  if (std::all_of (name.begin ( ), name.end ( ), [](char ch) { return ch == '.'; }))
    return false;

  return SKIF_IGDB_QueryAndDownload (L"search/" + SKIF_IGDB_UrlEncode (name), targetPath, allowFuzzy, name);
}

bool
SKIF_PCGW_DownloadCover (SKIF_PCGW_Store store, uint32_t id, const std::wstring& targetPath)
{
  if (id == 0 || targetPath.empty ( ))
    return false;

  std::wstring   path = ((store == SKIF_PCGW_Store::GOG) ? L"gog/" : L"steam/") + std::to_wstring (id);
  nlohmann::json json;

  if (! SKIF_CoverProxy_Lookup (pcgw, path, json))
    return false;

  std::string name = SKIF_CoverProxy_GetString (json["game"],  "name");
  std::string url  = SKIF_CoverProxy_GetString (json["cover"], "url");

  if (url.rfind ("https://", 0) != 0)
  {
    PLOG_WARNING << "Invalid PCGW cover URL for " << path;
    return SKIF_CoverProxy_RememberMiss (pcgw, path);
  }

  std::wstring imageUrl = SK_UTF8ToWideChar (url);

  std::error_code ec;
  std::filesystem::create_directories (std::filesystem::path (targetPath).parent_path ( ), ec);

  PLOG_DEBUG << "Downloading the PCGW cover " << imageUrl;

  if (! SKIF_Util_GetWebResource (imageUrl, targetPath))
  {
    PLOG_WARNING << "Failed to download the PCGW cover " << imageUrl;
    return false;
  }

  PLOG_INFO << "Downloaded the PCGW cover of " << SK_UTF8ToWideChar (name) << " to " << targetPath;

  return true;
}

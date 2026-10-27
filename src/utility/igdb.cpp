#include <utility/igdb.h>
#include <nlohmann/json.hpp>

#include <wtypes.h>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <set>
#include <atomic>

#include <utility/registry.h>
#include <utility/utility.h>

// See igdb.h

namespace
{
  constexpr const wchar_t* IGDB_PROXY      = L"https://igdb.specialk.aemony.se/v1/cover";
  constexpr const wchar_t* IGDB_IMAGE_BASE = L"https://images.igdb.com/igdb/image/upload/";

  // Lookups that found no cover this session, so a game is not queried again on every selection
  //   (the proxy caches misses too, but this also avoids the request). Retried on the next launch of SKIF.
  std::mutex              missesMutex;
  std::set <std::wstring> misses;

  // When a lookup fails for any other reason (no response, rate limited, proxy error), lookups pause for a while
  //   instead of the game being remembered as a miss, so a temporary failure does not hide covers until a restart
  constexpr DWORD         FAILURE_PAUSE = 60000; // ms
  std::atomic <DWORD>     dwLastFailure = 0;     // SKIF_Util_timeGetTime ( ) of the last failure; 0 = none

  // Percent-encodes a UTF-8 string for a URL query value
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

  // Queries the proxy and downloads the cover it names; true if a cover was downloaded
  bool
  SKIF_IGDB_QueryAndDownload (const std::wstring& query, const std::wstring& targetPath, bool allowFuzzy)
  {
    static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

    {
      std::scoped_lock lock (missesMutex);

      if (misses.count (query) != 0)
        return false;
    }

    DWORD dwFailure = dwLastFailure.load ( );

    if (dwFailure != 0 && SKIF_Util_timeGetTime ( ) - dwFailure < FAILURE_PAUSE)
      return false;

    auto _Miss = [&](void) -> bool
    {
      std::scoped_lock lock (missesMutex);
      misses.insert (query);
      return false;
    };

    std::wstring url = std::wstring (IGDB_PROXY) + L"?" + query;
    std::string  response;

    DWORD dwStatus = 0;

    if (! SKIF_Util_GetWebResource (url, L"", L"GET", L"", "", L"", &response, &dwStatus) || response.empty ( ))
    {
      // 404: IGDB has no such game, or no cover for it. 400: a lookup the proxy refuses. Both are final for this session.
      if (dwStatus == 404 || dwStatus == 400)
      {
        PLOG_VERBOSE << "No IGDB cover found for " << query;
        return _Miss ( );
      }

      // Anything else is temporary (no response, 429 rate limited, 5xx): pause lookups, then try again
      PLOG_WARNING << "IGDB cover lookup failed (HTTP status " << dwStatus << ") for " << query << "; retrying in " << (FAILURE_PAUSE / 1000) << " seconds at the earliest.";
      dwLastFailure.store (SKIF_Util_timeGetTime ( ));
      return false;
    }

    dwLastFailure.store (0);

    nlohmann::json json = nlohmann::json::parse (response, nullptr, false);

    if (json.is_discarded ( ) || ! json.is_object ( ) || ! json.contains ("cover") || ! json["cover"].is_object ( ))
    {
      PLOG_WARNING << "Unexpected response from the IGDB proxy for " << query;
      return _Miss ( );
    }

    std::string match    = json.value ("match", "");
    std::string name     = (json.contains ("game") && json["game"].is_object ( )) ? json["game"].value ("name", "") : "";
    std::string image_id = json["cover"].value ("image_id", "");

    if (match == "fuzzy" && ! allowFuzzy)
    {
      PLOG_VERBOSE << "Ignoring the fuzzy IGDB match for " << query << ": " << SK_UTF8ToWideChar (name);
      return _Miss ( );
    }

    // The image ID goes into a URL and a file is written from it: only accept what IGDB uses (e.g. co27c8)
    if (image_id.empty ( ) || image_id.size ( ) > 64 ||
        ! std::all_of (image_id.begin ( ), image_id.end ( ), [](unsigned char ch) { return std::isalnum (ch) || ch == '_'; }))
    {
      PLOG_WARNING << "Invalid IGDB image ID for " << query;
      return _Miss ( );
    }

    // t_1080p fits the image within 1920x1080 (a 3:4 cover becomes 810x1080); t_cover_big_2x is 528x748
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

  std::wstring query = SK_FormatStringW (L"source=%d&uid=", static_cast <int> (source)) + SKIF_IGDB_UrlEncode (uid);

  return SKIF_IGDB_QueryAndDownload (query, targetPath, false);
}

bool
SKIF_IGDB_DownloadCoverByName (const std::string& name, const std::wstring& targetPath, bool allowFuzzy)
{
  if (name.empty ( ) || targetPath.empty ( ))
    return false;

  return SKIF_IGDB_QueryAndDownload (L"search=" + SKIF_IGDB_UrlEncode (name), targetPath, allowFuzzy);
}

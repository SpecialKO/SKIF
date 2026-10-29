#pragma once
#include <string>
#include <cstdint>

/*
Covers through the SKIF cover proxies (Cloudflare Workers that hold the IGDB / PCGamingWiki credentials).
Both have the same API and response format:

  https://igdb.special-k.info/v1/{store}/{id}     By store ID (IGDB external_games), e.g. /v1/itch/216996
  https://igdb.special-k.info/v1/search/{name}    By name (IGDB only), e.g. /v1/search/Sea%20of%20Solitude
  https://pcgw.special-k.info/v1/{store}/{id}     By store ID (Cargo table Game; steam and gog only), e.g. /v1/steam/1245620

  200: { "game": { "id", "name" }, "cover": { "url", "url_original", "width", "height", "animated", "image_id" (IGDB) },
         "match": "id" | "exact" | "base" | "fuzzy" }
  404 not_found, 400 bad request, 502 upstream failure, 500 misconfigured: { "error": "<code>" }

The proxies only return the cover's location; the image itself is downloaded from images.igdb.com / images.pcgamingwiki.com.
IGDB covers are portrait (usually 3:4).
*/

// IGDB external game sources (IGDB's external_game_sources endpoint)
enum class SKIF_IGDB_Source : int {
  Steam           =  1,
  GOG             =  5,
  Microsoft       = 11,
  EpicGamesStore  = 26,
  itch            = 30,
  XboxMarketplace = 31
};

// Worker threads only (blocking network calls). Download the cover of a game to targetPath (created along with
//   its folder); true if a cover was downloaded. The image is a JPEG: targetPath should end in .jpg.
//   A lookup that finds no cover is not repeated until SKIF is restarted. After a failed request (no response,
//   rate limited, proxy error), lookups pause for a minute and are then tried again.
bool SKIF_IGDB_DownloadCover       (SKIF_IGDB_Source source, const std::string& uid, const std::wstring& targetPath);

// By name, for stores IGDB has no source for. Only exact name matches are used unless allowFuzzy is set; a fuzzy match
//   with the same name written differently counts as exact (case, punctuation, ® ™ ©, "&" / "and", and Roman numerals
//   are ignored: "The Settlers 2 - History Edition" matches "The Settlers II: History Edition").
bool SKIF_IGDB_DownloadCoverByName (const std::string& name, const std::wstring& targetPath, bool allowFuzzy = false);

// PCGamingWiki stores the PCGW cover proxy can look up by ID
enum class SKIF_PCGW_Store : int {
  Steam,
  GOG
};

// Worker threads only (blocking network calls). Download the PCGamingWiki cover of a game to targetPath (created along
//   with its folder); true if a cover was downloaded. Misses and failed requests are handled as for IGDB covers.
bool SKIF_PCGW_DownloadCover       (SKIF_PCGW_Store store, uint32_t id, const std::wstring& targetPath);

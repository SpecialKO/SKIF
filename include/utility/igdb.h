#pragma once
#include <string>

/*
IGDB covers, through the SKIF IGDB cover proxy (a Cloudflare Worker that holds the Twitch credentials):

  https://igdb.special-k.info/v1/cover?source=<IGDB external game source>&uid=<store ID>
  https://igdb.special-k.info/v1/cover?search=<game name>

The proxy only returns the cover's IGDB image ID; the image itself is downloaded from images.igdb.com.
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

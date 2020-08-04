#include <stores/Steam/asset_fetch.h>

#include <comdef.h>
#include <process.h>

#include <gsl/gsl>

struct {
  bool reload = true;

  bool signalReload (void)
  {
    bool ret =
      reload;

    reload = true;

    return ret;
  }

  bool isReloadSignaled (bool reset = false)
  {
    bool ret =
      reload;

    if (reset) reload = false;

    return ret;
  }
} static asset_mgr;

bool
SKIF_LibraryAssets_CheckForUpdates (bool reset)
{
  return
    asset_mgr.isReloadSignaled (reset);
}

DWORD
WINAPI
SKIF_Steam_FetchBoxArt (skif_box_art_get_t* get)
{
  ULONG ulTimeout = 5000UL;

  PCWSTR rgpszAcceptTypes [] = { L"*/*", nullptr };
  HINTERNET hInetHTTPGetReq  = nullptr,
            hInetHost        = nullptr,
  hInetRoot                  =
    InternetOpen (
      L"Special K - Asset Crawler",
        INTERNET_OPEN_TYPE_DIRECT,
          nullptr, nullptr,
            0x00
    );

  // (Cleanup On Error)
  auto CLEANUP = [&](bool clean = false) ->
  DWORD
  {
    if (! clean)
    {
      DWORD dwLastError =
           GetLastError ();

      OutputDebugStringW (
        ( std::wstring (L"WinInet Failure (") +
              std::to_wstring (dwLastError)   +
          std::wstring (L"): ")               +
                 _com_error   (dwLastError).ErrorMessage ()
        ).c_str ()
      );
    }

    if (hInetHTTPGetReq != nullptr) InternetCloseHandle (hInetHTTPGetReq);
    if (hInetHost       != nullptr) InternetCloseHandle (hInetHost);
    if (hInetRoot       != nullptr) InternetCloseHandle (hInetRoot);

    skif_box_art_get_t* to_delete = nullptr;
    std::swap   (get,   to_delete);
    delete              to_delete;

    return 0;
  };

  if (hInetRoot == nullptr)
    return CLEANUP ();

  DWORD_PTR dwInetCtx = 0;

  hInetHost =
    InternetConnect ( hInetRoot,
                        get->wszHostName,
                          INTERNET_DEFAULT_HTTP_PORT,
                            nullptr, nullptr,
                              INTERNET_SERVICE_HTTP,
                                0x00,
                                  (DWORD_PTR)&dwInetCtx );

  if (hInetHost == nullptr)
  {
    return CLEANUP ();
  }

  hInetHTTPGetReq =
    HttpOpenRequest ( hInetHost,
                        nullptr,
                          get->wszHostPath,
                            L"HTTP/1.1",
                              nullptr,
                                rgpszAcceptTypes,
                                                                    INTERNET_FLAG_IGNORE_CERT_DATE_INVALID |
                                  INTERNET_FLAG_CACHE_IF_NET_FAIL | INTERNET_FLAG_IGNORE_CERT_CN_INVALID   |
                                  INTERNET_FLAG_RESYNCHRONIZE     | INTERNET_FLAG_CACHE_ASYNC,
                                    (DWORD_PTR)&dwInetCtx );


  // Wait 2500 msecs for a dead connection, then give up
  //
  InternetSetOptionW ( hInetHTTPGetReq, INTERNET_OPTION_RECEIVE_TIMEOUT,
                         &ulTimeout,    sizeof (ULONG) );


  if (hInetHTTPGetReq == nullptr)
  {
    return CLEANUP ();
  }

  if ( HttpSendRequestW ( hInetHTTPGetReq,
                            nullptr,
                              0,
                                nullptr,
                                  0 ) )
  {
    DWORD dwStatusCode        = 0;
    DWORD dwStatusCode_Len    = sizeof (DWORD);

    DWORD dwContentLength     = 0;
    DWORD dwContentLength_Len = sizeof (DWORD);
    DWORD dwSizeAvailable;

    HttpQueryInfo ( hInetHTTPGetReq,
                     HTTP_QUERY_STATUS_CODE |
                     HTTP_QUERY_FLAG_NUMBER,
                       &dwStatusCode,
                         &dwStatusCode_Len,
                           nullptr );

    if (dwStatusCode == 200)
    {
      HttpQueryInfo ( hInetHTTPGetReq,
                        HTTP_QUERY_CONTENT_LENGTH |
                        HTTP_QUERY_FLAG_NUMBER,
                          &dwContentLength,
                            &dwContentLength_Len,
                              nullptr );

      std::vector <char> http_chunk;
      std::vector <char> concat_buffer;

      while ( InternetQueryDataAvailable ( hInetHTTPGetReq,
                                             &dwSizeAvailable,
                                               0x00, NULL )
        )
      {
        if (dwSizeAvailable > 0)
        {
          DWORD dwSizeRead = 0;

          if (http_chunk.size () < dwSizeAvailable)
              http_chunk.resize   (dwSizeAvailable);

          if ( InternetReadFile ( hInetHTTPGetReq,
                                    http_chunk.data (),
                                      dwSizeAvailable,
                                        &dwSizeRead )
             )
          {
            if (dwSizeRead == 0)
              break;

            concat_buffer.insert ( concat_buffer.cend   (),
                                    http_chunk.cbegin   (),
                                      http_chunk.cbegin () + dwSizeRead );

            if (dwSizeRead < dwSizeAvailable)
              break;
          }
        }

        else
          break;
      }

      FILE *fOut =
        _wfopen ( get->wszLocalPath, L"wb+" );

      if (fOut != nullptr)
      {
        fwrite (concat_buffer.data (), concat_buffer.size (), 1, fOut);
        fclose (fOut);
      }

      asset_mgr.signalReload ();
    }
  }

  CLEANUP (true);

  return 1;
}

void
SKIF_HTTP_GetAppLibImg (uint32_t app_id, std::wstring_view path)
{
  auto* get =
    new skif_box_art_get_t { };

  URL_COMPONENTSW urlcomps = { };

  urlcomps.dwStructSize     = sizeof (URL_COMPONENTSW);

  urlcomps.lpszHostName     = get->wszHostName;
  urlcomps.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;

  urlcomps.lpszUrlPath      = get->wszHostPath;
  urlcomps.dwUrlPathLength  = INTERNET_MAX_PATH_LENGTH;

  std::wstring url  = L"https://steamcdn-a.akamaihd.net/steam/apps/";
               url += std::to_wstring (app_id);
               url += L"/library_600x900_2x.jpg";

  if ( InternetCrackUrl (          url.c_str  (),
         gsl::narrow_cast <DWORD> (url.length ()),
                            0x00,
                              &urlcomps
                        )
     )
  {
    wcsncpy ( get->wszLocalPath,
                           path.data (),
                       MAX_PATH );

    _beginthreadex (
       nullptr, 0, (_beginthreadex_proc_type)SKIF_Steam_FetchBoxArt,
           get, 0x0, nullptr
                   );
  }
}
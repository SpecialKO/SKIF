#include <utility/artwork.h>
#include <utility/games.h>
#include <utility/registry.h>
#include <utility/utility.h>
#include <stores/generic_library2.h>
#include <SKIF.h>
#include <concurrent_queue.h>
#include <process.h>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <utility/sk_utility.h>

extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

SKIF_LibraryArtwork::SKIF_LibraryArtwork (void)
{
  ResetCoverAlpha ( );
}

#pragma region Cover Selection

void
SKIF_LibraryArtwork::SelectCover (app_record_s* pApp)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // Ensure we aren't already loading this cover
  if (m_cover == pApp->GetKey ( ))
    return;

  // Special handling for the Special K logo when fading is disabled
  if (! _registry.bFadeCovers)
  {
    if (pApp->IsSpecialK ( ))
      m_fAlphaSK = 1.0f;
    else if (m_cover == app_key_s { app_record_s::Store::Steam, SKIF_STEAM_APPID })
      m_fAlphaSK = 0.0f;
  }

  m_loadCover  = true;
  m_cover      = pApp->GetKey ( );

  // Not known to be animated until the new cover has loaded
  m_coverAnimSource.clear ( );

  // Hide the current cover and set it up to be unloaded
  if (m_pTexSRV.p != nullptr)
    RetireCover ( );
}

void
SKIF_LibraryArtwork::ForgetCover (void)
{
  m_cover = { };
}

// Current cover → previous one, which fades out (or is released right away when fading is disabled)
void
SKIF_LibraryArtwork::RetireCover (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // The old cover fades out as a still cover (its frames stay loaded, for its tile in the grid view)
  m_coverAnimSource.clear ( );

  // If there already is an old cover, we need to push it for release
  if (m_pTexSRV_old.p != nullptr)
  {
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << m_pTexSRV_old.p << " to be released";;
    SKIF_ResourcesToFree.push(m_pTexSRV_old.p);
    m_pTexSRV_old.p = nullptr;
  }

  // Set up the current one to be released
  m_vecCoverRes_old = m_vecCoverRes;
  m_vecCoverRes     = ImVec2 (0, 0);
  m_pTexSRV_old.p   = m_pTexSRV.p;
  m_pTexSRV.p       = nullptr;
  m_fAlphaPrev      = (_registry.bFadeCovers) ? m_fAlpha : 0.0f;
  m_fAlpha          = (_registry.bFadeCovers) ?   0.0f   : 1.0f;
}

// Release old cover after it has faded away, or instantly if we don't fade covers
void
SKIF_LibraryArtwork::ReleaseOldCoverIfFaded (void)
{
  if (m_fAlphaPrev <= 0.0f)
  {
    if (m_pTexSRV_old.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << m_pTexSRV_old.p << " to be released";;
      SKIF_ResourcesToFree.push(m_pTexSRV_old.p);
      m_pTexSRV_old.p = nullptr;
    }
  }
}

#pragma endregion

#pragma region Cover Worker

// The cover pipeline of the cover worker and the thumbnail builds (worker threads): the store's cover (downloaded when
//   missing), the IGDB cover when preferred, then the texture (thumb: as a thumbnail, saved to the thumbnail cache)
static bool
_LoadCover (app_record_s* pApp, bool queryPCGW, library_texture_s& out, const library_thumbnail_s* thumb)
{
  // Store-specific: fetch or download the store's cover (Steam, Epic, Xbox, PCGamingWiki)
  std::wstring load_str = pApp->PrepareCoverSource (queryPCGW);

  // The IGDB cover, when preferred for this store (Settings): downloaded once and kept as cover-igdb.jpg.
  //   Not needed when there is a PCGamingWiki cover (just prepared above), which takes precedence.
  if (pApp->WantsIGDBCover ( ) && pApp->FindCustomArtwork (LibraryTexture::Cover).empty ( ))
  {
    std::wstring assetDir  = pApp->GetAssetDirectory ( );
    std::wstring igdbCover = assetDir + L"cover-igdb.jpg";

    if (! PathFileExistsW (igdbCover.c_str ( )) &&
        ! (pApp->WantsPCGWCover ( ) && PathFileExistsW ((assetDir + L"cover-pcgw.png").c_str ( ))))
      pApp->DownloadIGDBCover (igdbCover);
  }

  return LoadLibraryTextureEx ( LibraryTexture::Cover,
                                  pApp->id,
                                    load_str,
                                      pApp,
                                        out,
                                          thumb );
}

void
SKIF_LibraryArtwork::StartCoverWorker (app_record_s* pApp)
{
  // Load cover first after the window has been shown -- to fix one copy leaking of the cover
  // 2023-03-24: Is this even needed any longer after fixing the double-loading that was going on?
  // 2023-03-25: Disabled HiddenFramesCannotSkipItems check to see if it's solved.
  // 2023-10-05: Disabled waiting for the icon thread as well
  m_loadCover = false;

  // Reset variables used to track whether we're still loading a game cover, or if we're missing one
  m_coverLoading.store (true);
  m_tryingToLoadCover = true;
  m_queuePosCover     = m_queueLength.load() + 1;

  struct thread_s {
    std::shared_ptr <app_record_s>
          app;       // Kept alive until the result is applied, even if a library refresh replaces it
    bool  queryPCGW; // Look up a PCGamingWiki cover: decided (and marked on the record) here, on the main thread
    app_key_s
          game;      // The game, also when its record could not be found
  };

  thread_s* data = new thread_s { (pApp != nullptr) ? SKIF_GamingCollection::Get ( ).FindShared (pApp->GetKey ( )) : nullptr,
                                  false,
                                  (pApp != nullptr) ? pApp->GetKey ( ) : app_key_s { } };

  if (pApp != nullptr && pApp->WantsPCGWCover ( ) && ! pApp->tex_cover.queriedPCGW)
  {
    pApp->tex_cover.queriedPCGW = true; // Reset by a cover reset (DeleteStoreCoverFiles), which re-queries
    data->queryPCGW             = true;
  }

  // We're going to stream the cover in asynchronously on this thread
  //   The record is passed in, rather than read from the selection once the thread runs
  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverWorker");

    CoInitializeEx (nullptr, 0x0);

    PLOG_VERBOSE << "SKIF_LibCoverWorker thread started!";

    PLOG_DEBUG  << "Streaming game cover asynchronously...";

    thread_s*            _data      = static_cast <thread_s*> (var);
    SKIF_LibraryArtwork& self       = SKIF_LibraryArtwork::Get ( );

    worker_result_s result;
    result.type       = LibraryTexture::Cover;
    result.record     = std::move (_data->app); // Keeps the record alive; released on the main thread (ApplyWorkerResults)
    result.game       = _data->game;
    bool  _queryPCGW  = _data->queryPCGW;

    delete _data;

    app_record_s*        _pApp      = result.record.get ( );

    // Not in the library (a record ApplyWorkerResults would not find either)
    if (_pApp == nullptr)
    {
      PLOG_ERROR << "Aborting due to pApp being a nullptr!";
      result.queuePos   = self.NextQueuePos ( );
      result.generation = self.m_generation.load ( );
      self.m_results.push (result); // Ends the cover load (no texture)
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      return 0;
    }

    result.queuePos   = self.NextQueuePos ( );
    result.generation = self.m_generation.load ( );
    //PLOG_VERBOSE << "queuePos = " << result.queuePos;

    _LoadCover (_pApp, _queryPCGW, result.texture, nullptr);

    PLOG_VERBOSE << "_pTexSRV = " << result.texture.srv.p;

    // The main thread swaps it in if this is still the latest load (ApplyWorkerResults)
    self.m_results.push (result);
    result.record.reset ( ); // The main thread releases it (the copy in the queue)

    PLOG_DEBUG << "Finished streaming game cover asynchronously...";
    PLOG_VERBOSE << "SKIF_LibCoverWorker thread stopped!";

    // Force a frame, so the result is swapped in
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);

    return 0;
  }, data, 0x0, nullptr);

  if (hWorkerThread != NULL)
    CloseHandle (hWorkerThread);

  else // Something went wrong during thread creation: nothing is loading
  {
    PLOG_ERROR << "Failed to start the cover worker thread!";
    delete data;
    m_coverLoading.store (false);
    m_tryingToLoadCover = false;
  }
}

// Applies the results the cover / icon workers finished. Main thread only, once per frame.
void
SKIF_LibraryArtwork::ApplyWorkerResults (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  auto _Release = [](CComPtr <ID3D11ShaderResourceView>& srv)
  {
    if (srv.p != nullptr)
    {
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << srv.p << " to be released";;
      SKIF_ResourcesToFree.push (srv.p);
      srv.p = nullptr;
    }
  };

  worker_result_s result;

  while (m_results.try_pop (result))
  {
    // The game's current record: the worker's own, or the one a library refresh replaced it with.
    //   nullptr if the game is gone. (The worker's record, result.record, is released with the next result.)
    app_record_s* pApp = _games.Find (result.game);

    // The worker is done: icon workers are counted, and their thread handle is no longer needed
    if (result.type == LibraryTexture::Icon)
    {
      ForgetThread (LibraryTexture::Icon, result.game);
      m_activeIconWorkers--;

      if (pApp != nullptr && pApp->tex_icon.iWorker == 1)
        pApp->tex_icon.iWorker = 2;
    }

    // Thumbnails: only the request the game is waiting for (a newer one replaced it if it was unloaded or invalidated)
    else if (result.type == LibraryTexture::Thumbnail)
    {
      ForgetThread (LibraryTexture::Thumbnail, result.game, result.queuePos);

      if (result.build)
        m_activeThumbBuilds--;
      else
        m_activeThumbLoads--;

      bool live = (pApp != nullptr && pApp->tex_thumb.request == result.queuePos && result.generation == m_generation.load ( ) &&
                  (pApp->tex_thumb.iWorker == SKIF_Thumbnail_Loading || pApp->tex_thumb.iWorker == SKIF_Thumbnail_Building));

      if (! live)
      {
        _Release (result.texture.srv);
        continue;
      }

      auto& thumb = pApp->tex_thumb;

      // Its tile went out of view while it loaded: not loaded, so it loads again once drawn again
      if (result.dropped)
      {
        thumb.iWorker = SKIF_Thumbnail_None;
        thumb.request = 0;
      }

      // No usable cache: build it once the tile is drawn again (RequestThumbnail)
      else if (result.noCache)
        thumb.iWorker = SKIF_Thumbnail_NoCache;

      else
      {
        thumb.iWorker   = SKIF_Thumbnail_Done;
        thumb.loadedAt  = SKIF_Util_timeGetTime ( );
        thumb.isCustom  = result.texture.isCustom;
        thumb.isManaged = result.texture.isManaged;
        thumb.animatedSource = result.texture.animatedSource; // Plays while its tile is hovered (UpdateAnimatedCover)

        if (thumb.texture.p != nullptr) // Not expected: a thumbnail is released before it loads again
        {
          _Release (thumb.texture);
          m_thumbsLoaded--;
        }

        if (result.texture.srv.p != nullptr)
        {
          thumb.width  = result.texture.resolution.x;
          thumb.height = result.texture.resolution.y;
          thumb.texture.Attach (result.texture.srv.Detach ( ));
          m_thumbsLoaded++;
        }

        // A build that found no cover: the game may have none, or a lookup failed for the moment (e.g. a cover proxy
        //   answered 429 and pauses lookups for 60 seconds, see igdb.cpp). Try once more after that, if it is drawn then.
        else if (result.build && ! thumb.retried)
        {
          thumb.retried = true;
          thumb.retryAt = SKIF_Util_timeGetTime ( ) + 65000;
        }
      }

      _Release (result.texture.srv);
      continue;
    }

    // Animated covers: only the latest request, for the cover still shown, made on the current device
    else if (result.type == LibraryTexture::AnimatedCover)
    {
      m_animWorkerBusy = false;

      const bool live = (result.queuePos   == m_animRequest.load ( ) &&
                         result.generation == m_generation.load  ( ) &&
                         result.game       == m_animGame             &&
                         m_animState       == AnimatedCoverState::Loading);

      if (live && result.frames.size ( ) >= 2 && result.frames.size ( ) == result.delays.size ( ))
      {
        DWORD end = 0;

        for (size_t i = 0; i < result.frames.size ( ); i++)
        {
          m_animFrames.emplace_back ( );
          m_animFrames.back ( ).Attach (result.frames [i].srv.Detach ( ));

          end += result.delays [i];
          m_animFrameEnds.push_back (end);
        }

        m_animDuration   = end;
        m_animResolution = result.frames [0].resolution;
        m_animState      = AnimatedCoverState::Ready;
      }

      // Not animated after all, too large, or not decodable: it stays a still cover
      else if (live)
        m_animState    = AnimatedCoverState::Failed;

      for (auto& frame : result.frames)
        _Release (frame.srv);

      result.frames.clear ( );
      result.delays.clear ( );
      continue;
    }

    // Made on the device from before a device reset
    if (result.generation != m_generation.load ( ))
    {
      _Release (result.texture.srv);

      // Load the icon again (ReleaseTextures left icons that were loading alone, as their result was still to come)
      if (result.type == LibraryTexture::Icon && pApp != nullptr)
        pApp->tex_icon.iWorker = 0;

      continue;
    }

    if (result.type == LibraryTexture::Cover)
    {
      int currentQueueLength = m_queueLength.load();

      // Only the latest load, and only for the game whose cover is shown (a newer load may not have its queue position yet)
      if (currentQueueLength != result.queuePos || result.game != m_cover)
      {
        PLOG_DEBUG << "Texture is late! (" << result.queuePos << " vs " << currentQueueLength << ")";
        _Release (result.texture.srv);
        continue;
      }

      PLOG_DEBUG << "Texture is live! Swapping it in.";
      _Release (m_pTexSRV);
      m_vecCoverRes = result.texture.resolution;
      m_pTexSRV     = result.texture.srv;

      // An animated cover (APNG, GIF): its frames load once it is hovered (UpdateAnimatedCover)
      m_coverAnimSource = (m_pTexSRV.p != nullptr) ? result.texture.animatedSource : L"";

      if (pApp != nullptr)
      {
        pApp->tex_cover.isCustom  = result.texture.isCustom;
        pApp->tex_cover.isManaged = result.texture.isManaged;
      }

      // Indicate that we have stopped loading the cover
      m_coverLoading.store (false);
      m_tryingToLoadCover = false;

      // Empty working set after the cover has finished loading
      SKIF_Util_CompactWorkingSet ( );
    }

    else if (result.type == LibraryTexture::Icon)
    {
      if (pApp == nullptr)
      {
        _Release (result.texture.srv);
        continue;
      }

      // tex_ref_s's own assignment operators hide CComPtr's, so hand the reference over instead
      _Release (pApp->tex_icon.texture);
      pApp->tex_icon.texture.Attach (result.texture.srv.Detach ( ));
      pApp->tex_icon.isCustom  = result.texture.isCustom;
      pApp->tex_icon.isManaged = result.texture.isManaged;
    }

    _Release (result.texture.srv); // Our reference (the cover / icon holds its own)
  }

  // The worker's record, if this was the last reference: released here, on the main thread
  result.record.reset ( );
}

HANDLE
SKIF_LibraryArtwork::FindThread (LibraryTexture type, app_key_s game) const
{
  for (auto& thread : m_threads)
    if (thread.type == type && thread.game == game)
      return thread.hThread;

  return NULL;
}

void
SKIF_LibraryArtwork::ForgetThread (LibraryTexture type, app_key_s game, int request)
{
  for (auto it = m_threads.begin ( ); it != m_threads.end ( ); ++it)
  {
    if (it->type == type && it->game == game && it->request == request)
    {
      CloseHandle (it->hThread);
      m_threads.erase (it);
      return;
    }
  }
}

bool
SKIF_LibraryArtwork::IsCoverMissing (void) const
{
  return (m_queueLength.load() == m_queuePosCover && m_pTexSRV.p == nullptr);
}

#pragma endregion

#pragma region Animated Covers

// How long a cover is hovered before its frames load (so moving the mouse across covers loads nothing), and how long they
//   are kept once it is no longer hovered
static constexpr DWORD s_animHoverDelay  =   250; // ms
static constexpr DWORD s_animIdleRelease = 30000; // ms

void
SKIF_LibraryArtwork::ResetAnimatedCover (void)
{
  ReleaseAnimatedCover ( );

  m_animGame   = { };
  m_animSource.clear ( );
  m_animState  = AnimatedCoverState::None;
}

void
SKIF_LibraryArtwork::ResetAnimatedCover (app_key_s game)
{
  if (m_animGame == game)
    ResetAnimatedCover ( );
}

bool
SKIF_LibraryArtwork::ReleaseAnimatedCover (void)
{
  const bool released = (! m_animFrames.empty ( ));

  for (auto& frame : m_animFrames)
  {
    if (frame.p != nullptr)
    {
      SKIF_ResourcesToFree.push (frame.p);
      frame.p = nullptr;
    }
  }

  m_animFrames   .clear ( );
  m_animFrameEnds.clear ( );
  m_animDuration     = 0;
  m_animResolution   = ImVec2 (0, 0);
  m_animPlayingSince = 0;

  // A load still running is no longer wanted (it stops, and its result is dropped)
  m_animRequest++;

  // Not loaded (a cover that could not be loaded is not tried again, until another cover has been hovered)
  if (m_animState != AnimatedCoverState::Failed)
    m_animState = AnimatedCoverState::None;

  StopAnimatedCoverTimer ( );

  return released;
}

void
SKIF_LibraryArtwork::HoverAnimatedCover (void)
{
  // Only a cover that is animated, shown, and not about to be replaced
  if (m_coverAnimSource.empty ( ) || m_pTexSRV.p == nullptr || m_loadCover)
    return;

  m_hoverSet  = true;
  m_hoverGame = m_cover;

  if (m_hoverSource != m_coverAnimSource)
    m_hoverSource = m_coverAnimSource;
}

void
SKIF_LibraryArtwork::HoverAnimatedThumbnail (app_record_s* pApp)
{
  // Only a thumbnail that is shown, of an animated cover
  if (pApp == nullptr || pApp->tex_thumb.texture.p == nullptr || pApp->tex_thumb.animatedSource.empty ( ))
    return;

  m_hoverSet  = true;
  m_hoverGame = pApp->GetKey ( );

  if (m_hoverSource != pApp->tex_thumb.animatedSource)
    m_hoverSource = pApp->tex_thumb.animatedSource;
}

void
SKIF_LibraryArtwork::StartAnimatedCoverWorker (void)
{
  struct thread_s {
    std::wstring path;
    app_key_s    game;
    int          request    = 0;
    unsigned int generation = 0;
  };

  thread_s* data = new thread_s { m_animSource, m_animGame, ++m_animRequest, m_generation.load ( ) };

  m_animState      = AnimatedCoverState::Loading;
  m_animWorkerBusy = true;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibAnimCoverWorker");

    CoInitializeEx (nullptr, 0x0); // WIC: scaling the frames

    PLOG_VERBOSE << "SKIF_LibAnimCoverWorker thread started!";

    thread_s*            _data = static_cast <thread_s*> (var);
    SKIF_LibraryArtwork& self  = SKIF_LibraryArtwork::Get ( );

    worker_result_s result;
    result.type       = LibraryTexture::AnimatedCover;
    result.game       = _data->game;
    result.queuePos   = _data->request;
    result.generation = _data->generation;

    const int          request = _data->request;
    const std::wstring path    = _data->path;

    delete _data;

    // Stops between frames once a newer request or a release made this one stale
    LoadAnimatedLibraryTexture (path, result.frames, result.delays,
      [&self, request](void) -> bool { return self.m_animRequest.load ( ) == request; });

    // The main thread swaps them in if this is still the latest request (ApplyWorkerResults)
    self.m_results.push (std::move (result));

    PLOG_VERBOSE << "SKIF_LibAnimCoverWorker thread stopped!";

    // Force a frame, so the result is swapped in
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);

    return 0;
  }, data, 0x0, nullptr);

  if (hWorkerThread != NULL)
    CloseHandle (hWorkerThread);

  else
  {
    PLOG_ERROR << "Failed to start the animated cover worker thread!";
    delete data;
    m_animState      = AnimatedCoverState::Failed;
    m_animWorkerBusy = false;
  }
}

void
SKIF_LibraryArtwork::UpdateAnimatedCover (void)
{
  const DWORD now    = SKIF_Util_timeGetTime ( );
  DWORD       wakeIn = 0; // When the next frame is due (ms), 0 if none

  // What was hovered this frame (HoverAnimatedCover / HoverAnimatedThumbnail)
  const bool hovered = m_hoverSet;
  m_hoverSet         = false;

  // Another cover: its frames replace those of the one hovered before, and its hover starts now
  if (hovered && (m_hoverGame != m_animGame || m_hoverSource != m_animSource))
  {
    ResetAnimatedCover ( );

    m_animGame         = m_hoverGame;
    m_animSource       = m_hoverSource;
    m_animHoveredSince = 0;
  }

  if (hovered)
  {
    m_animLastHovered = now;

    if (m_animHoveredSince == 0)
      m_animHoveredSince = now;

    // Load the frames once it has been hovered for a moment (one worker at a time: a stale one still has to finish)
    if (m_animState == AnimatedCoverState::None && ! m_animWorkerBusy)
    {
      const DWORD hoveredFor = now - m_animHoveredSince;

      if (hoveredFor >= s_animHoverDelay)
        StartAnimatedCoverWorker ( );
      else
        wakeIn = s_animHoverDelay - hoveredFor;
    }

    // Play: from the first frame, every time the cover is hovered again
    if (m_animState == AnimatedCoverState::Ready && m_animDuration > 0)
    {
      if (m_animPlayingSince == 0)
        m_animPlayingSince = now;

      const DWORD position = (now - m_animPlayingSince) % m_animDuration;
      const auto  frameEnd = std::upper_bound (m_animFrameEnds.begin ( ), m_animFrameEnds.end ( ), position);

      wakeIn = (frameEnd != m_animFrameEnds.end ( )) ? (*frameEnd - position) : 1;
    }
  }

  else
  {
    m_animHoveredSince = 0;
    m_animPlayingSince = 0; // Back to the still cover

    if (m_animState == AnimatedCoverState::Ready && now - m_animLastHovered > s_animIdleRelease)
      ReleaseAnimatedCover ( );
  }

  // Wake SKIF when the next frame is due (re-armed only when that changes). Once it fires, the frames it gives re-arm it.
  if (wakeIn > 0)
  {
    const DWORD due = now + wakeIn;

    if (! m_animTimer || due != m_animTimerDue)
    {
      m_animTimer    = (SetTimer (SKIF_Notify_hWnd, cIDT_TIMER_COVERANIM, std::max (wakeIn, static_cast <DWORD> (USER_TIMER_MINIMUM)), NULL) != 0);
      m_animTimerDue = due;
    }
  }

  else
    StopAnimatedCoverTimer ( );
}

ID3D11ShaderResourceView*
SKIF_LibraryArtwork::GetAnimatedCoverFrame (app_key_s game, ImVec2* resolution) const
{
  if (game != m_animGame || m_animState != AnimatedCoverState::Ready || m_animPlayingSince == 0 || m_animDuration == 0)
    return nullptr;

  const DWORD position = (SKIF_Util_timeGetTime ( ) - m_animPlayingSince) % m_animDuration;
  const auto  frameEnd = std::upper_bound (m_animFrameEnds.begin ( ), m_animFrameEnds.end ( ), position);
  const auto  frame    = static_cast <size_t> (std::distance (m_animFrameEnds.begin ( ), frameEnd));

  if (frame >= m_animFrames.size ( ))
    return nullptr;

  if (resolution != nullptr)
    *resolution = m_animResolution;

  return m_animFrames [frame].p;
}

void
SKIF_LibraryArtwork::StopAnimatedCoverTimer (void)
{
  if (m_animTimer)
  {
    KillTimer (SKIF_Notify_hWnd, cIDT_TIMER_COVERANIM);
    m_animTimer = false;
  }
}

void
SKIF_LibraryArtwork::OnAnimatedCoverTimer (void)
{
  // A one-shot: the frames it wakes SKIF for re-arm it (UpdateAnimatedCover), if the cover still plays
  StopAnimatedCoverTimer ( );
}

#pragma endregion

#pragma region Fades

void
SKIF_LibraryArtwork::ResetCoverAlpha (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  m_fAlpha = (_registry.bFadeCovers) ? 0.0f : 1.0f;
}

// Called only while covers are faded. tick: more than 15 ms since the last step
bool
SKIF_LibraryArtwork::StepFades (bool isSpecialK, bool tick, bool& ticked)
{
  bool active = false;

  // Fade in the new cover
  if (m_fAlpha < 1.0f && m_pTexSRV.p != nullptr)
  {
    if (tick)
    {
      m_fAlpha += 0.05f;
      ticked = true;
    }

    active = true;
  }

  // Fade out the old one
  if (m_fAlphaPrev > 0.0f && m_pTexSRV_old.p != nullptr)
  {
    if (tick)
    {
      m_fAlphaPrev -= 0.05f;
      ticked        = true;
    }

    active = true;
  }

  // Fade in the SK logo
  if (isSpecialK && m_fAlphaSK < 1.0f)
  {
    if (tick)
    {
      m_fAlphaSK += 0.05f;
      ticked      = true;
    }

    active = true;
  }

  // Fade out the SK logo
  if (! isSpecialK && m_fAlphaSK > 0.0f)
  {
    if (tick)
    {
      m_fAlphaSK -= 0.05f;
      ticked      = true;
    }

    active = true;
  }

  return active;
}

#pragma endregion

#pragma region Custom Covers

void
SKIF_LibraryArtwork::BeginSavingCover (app_record_s* pApp)
{
  // This allows the "..." loading dots to be visible
  m_savingCover  = true;
  m_refresh      = pApp->GetKey ( );

  // This sets up the current one to be released
  RetireCover ( );

  // A child thread will post WM_SKIF_REFRESHCOVER once done
}

bool
SKIF_LibraryArtwork::IsSavingCover (const app_record_s* pApp) const
{
  return (pApp != nullptr &&
          m_savingCover   &&
          m_refresh == pApp->GetKey ( ));
}

void
SKIF_LibraryArtwork::OnCoverSaved (app_key_s game)
{
  m_refreshPending = true;
  m_refresh        = game;

  // Its thumbnail shows the old cover (a custom cover was saved)
  if (app_record_s* pApp = SKIF_GamingCollection::Get ( ).Find (game); pApp != nullptr)
    InvalidateThumbnail (pApp);
}

void
SKIF_LibraryArtwork::RefreshShownCover (void)
{
  m_refreshPending = true;
  m_refresh        = m_cover;

  // The cover sources changed (Settings): every thumbnail may come from another source now. Their cache files are
  //   named after the sources (GetThumbnailCachePath), so they are only unloaded here.
  ReleaseThumbnails ( );

  // The cover that played (if any) may not be the one shown any longer either
  ResetAnimatedCover ( );
}

bool
SKIF_LibraryArtwork::ProcessCoverRefresh (void)
{
  if (! m_refreshPending)
    return false;

  m_refreshPending = false;

  bool reload = (m_cover == m_refresh);

  // Needed as otherwise SKIF would not reload the cover
  if (reload)
    ForgetCover ( );

  m_savingCover  = false;
  m_refresh      = { };

  return reload;
}

#pragma endregion

#pragma region Icons

void
SKIF_LibraryArtwork::StartIconWorker (app_record_s* pApp)
{
  m_activeIconWorkers++;

  // Store-specific icon source (Steam: parses appinfo.vdf for the icon hash first)
  pApp->PrepareIconSource ( );

  std::wstring load_str = pApp->GetIconSource ( );

  struct thread_s {
    app_key_s                     game;
    unsigned int                  generation;
    std::shared_ptr <app_record_s>
                                  app;  // Used by ResolveArtwork only; kept alive until the result is applied
    std::wstring                  path;
  };

  thread_s* data = new thread_s;

  data->path       = load_str;
  data->game       = pApp->GetKey ( );
  data->generation = m_generation.load ( );
  data->app        = SKIF_GamingCollection::Get ( ).FindShared (data->game); // nullptr: not in the library (ResolveArtwork is skipped)

  // We're going to stream game icons asynchronously on this thread
  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibIconWorker");

    CoInitializeEx (nullptr, 0x0);

    thread_s* _data = static_cast<thread_s*>(var);

    worker_result_s result;
    result.type       = LibraryTexture::Icon;
    result.game       = _data->game;
    result.generation = _data->generation;

    LoadLibraryTextureEx ( LibraryTexture::Icon,
                              _data->game.appid,
                                _data->path,
                                  _data->app.get ( ),
                                    result.texture );

    // The record goes back with the result, so it is released on the main thread
    result.record     = std::move (_data->app);

    delete _data;

    // The main thread puts it on the game (ApplyWorkerResults). Always pushed: it also ends the worker's bookkeeping.
    SKIF_LibraryArtwork::Get ( ).m_results.push (result);
    result.record.reset ( );

    // Force a refresh when the game icons have finished being streamed
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

    return 0;
  }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated)
  {
    PLOG_VERBOSE << "An icon worker was spawned successfully!";
    m_threads.push_back ({ LibraryTexture::Icon, pApp->GetKey ( ), hWorkerThread }); // Closed once its result is applied
    pApp->tex_icon.iWorker = 1;
  }

  else // Someting went wrong during thread creation, so free up the memory we allocated earlier
  {
    PLOG_VERBOSE << "Something went wrong when spawning an icon worker thread...";

    delete data;
    pApp->tex_icon.iWorker = 2;
    m_activeIconWorkers--;
  }
}

void
SKIF_LibraryArtwork::ReloadIcon (app_record_s* pApp)
{
  // An icon worker still running for this game would deliver the old icon after this one:
  //   let it finish, and apply its result first
  if (HANDLE hThread = FindThread (LibraryTexture::Icon, pApp->GetKey ( )); hThread != NULL)
    WaitForSingleObject (hThread, 2000);

  // Workers push their result before their thread ends, so it is queued by now (unless the wait timed out)
  ApplyWorkerResults ( );

  ImVec2 dontCare;

  LoadLibraryTexture (LibraryTexture::Icon,
                        pApp->id,
                          pApp->tex_icon.texture,
                            pApp->GetIconSource ( ),
                              dontCare,
                                pApp );
}

#pragma endregion

#pragma region Textures

void
SKIF_LibraryArtwork::ReleaseCurrentCover (void)
{
  m_coverAnimSource.clear ( );

  if (m_pTexSRV.p != nullptr)
  {
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << m_pTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push(m_pTexSRV.p);
    m_pTexSRV.p = nullptr;
  }
}

void
SKIF_LibraryArtwork::ReleaseTextures (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  // Results still in flight were made on the old device
  m_generation++;

  // The animated cover's frames load again once it is hovered (the cover itself is loaded again, ReloadCover)
  ReleaseAnimatedCover ( );

  if (m_pTexSRV.p != nullptr)
  {
    SKIF_ResourcesToFree.push(m_pTexSRV.p);
    m_pTexSRV.p = nullptr;
  }

  if (m_pTexSRV_old.p != nullptr)
  {
    SKIF_ResourcesToFree.push(m_pTexSRV_old.p);
    m_pTexSRV_old.p = nullptr;
  }

  for (auto& app : _games.GetApps())
  {
    if (app.second->tex_icon.texture.p != nullptr)
    {
      SKIF_ResourcesToFree.push(app.second->tex_icon.texture.p);
      app.second->tex_icon.texture.p = nullptr;
    }

    // Load the icon again once it is drawn: right away if its worker is done, otherwise once its result (made on
    //   the old device) has been dropped (ApplyWorkerResults)
    if (app.second->tex_icon.iWorker == 2)
      app.second->tex_icon.iWorker = 0;

    // Thumbnails load again once drawn (results in flight are dropped: they were made on the old device)
    ReleaseThumbnail (app.second.get ( ));
  }
}

#pragma endregion

#pragma region Thumbnails

// Worker threads: the time of the newest cover in the game's asset folder (custom covers and the ones SKIF downloaded),
//   which a thumbnail must be newer than to be current. A cover SKIF reads from elsewhere (e.g. the Steam client's library
//     cache) is not checked. False if there is none.
static bool
_GetNewestCoverTime (const std::filesystem::path& folder, std::filesystem::file_time_type& newest)
{
  std::error_code ec;
  bool            found = false;

  for (auto& entry : std::filesystem::directory_iterator (folder, ec))
  {
    std::wstring name = SKIF_Util_ToLowerW (entry.path ( ).filename ( ).wstring ( ));

    if (name.rfind (L"cover", 0) != 0 || name.rfind (L"cover-thumb", 0) == 0)
      continue;

    std::error_code ec2;
    if (! entry.is_regular_file (ec2))
      continue;

    auto time = entry.last_write_time (ec2);

    if (! ec2 && (! found || time > newest))
    {
      newest = time;
      found  = true;
    }
  }

  return found;
}

// Worker threads: whether the thumbnail cache is newer than every cover in the game's asset folder
static bool
_IsThumbnailCacheCurrent (const std::wstring& cachePath)
{
  std::error_code ec;

  const std::filesystem::path fsCache (cachePath);
  auto cacheTime = std::filesystem::last_write_time (fsCache, ec);

  if (ec)
    return false;

  std::filesystem::file_time_type newest;

  return (! _GetNewestCoverTime (fsCache.parent_path ( ), newest) || newest <= cacheTime);
}

// Removes thumbnail cache files of the game. all: every one (its cover changed). Otherwise (a build, which replaces
//   cachePath itself) only those no longer of use: outdated ones (older than the newest cover), and leftovers from older
//     versions (PNG) or interrupted builds (.tmp). Current ones made for another DPI scale or other cover sources (the
//       name says which) are kept, as they are used again when SKIF goes back to them.
static void
_DeleteThumbnails (const std::wstring& cachePath, bool all)
{
  std::error_code ec;

  const std::filesystem::path fsCache (cachePath);
  const std::wstring          keep = SKIF_Util_ToLowerW (fsCache.filename ( ).wstring ( ));

  std::filesystem::file_time_type newest;
  const bool hasCover = (! all && _GetNewestCoverTime (fsCache.parent_path ( ), newest));

  std::vector <std::filesystem::path> remove;

  for (auto& entry : std::filesystem::directory_iterator (fsCache.parent_path ( ), ec))
  {
    std::wstring name = SKIF_Util_ToLowerW (entry.path ( ).filename ( ).wstring ( ));

    if (name.rfind (L"cover-thumb", 0) != 0 || (! all && name == keep))
      continue;

    bool outdated = all;

    if (! outdated)
    {
      std::error_code ec2;
      auto time = entry.last_write_time (ec2);

      outdated = (entry.path ( ).extension ( ) != L".dds" || ec2 || (hasCover && time < newest));
    }

    if (outdated)
      remove.push_back (entry.path ( ));
  }

  for (auto& path : remove)
    std::filesystem::remove (path, ec);
}

std::wstring
SKIF_LibraryArtwork::GetThumbnailCachePath (app_record_s* pApp) const
{
  std::wstring assetDir = pApp->GetAssetDirectory ( );

  if (assetDir.empty ( ) || m_thumbSize.x <= 0.0f)
    return L"";

  // Named after what it is made from: its size (one per DPI scale), and the cover sources preferred for the store
  //   (Settings). A change of either uses another file; the others are kept while current (_DeleteThumbnails).
  return SK_FormatStringW (L"%wscover-thumb-%ix%i%ws%ws.dds", assetDir.c_str ( ),
                             static_cast <int> (m_thumbSize.x), static_cast <int> (m_thumbSize.y),
                               (pApp->WantsPCGWCover ( )) ? L"-pcgw" : L"",
                               (pApp->WantsIGDBCover ( )) ? L"-igdb" : L"");
}

void
SKIF_LibraryArtwork::SetThumbnailSize (ImVec2 size)
{
  size = ImVec2 (std::floor (size.x), std::floor (size.y));

  if (size.x == m_thumbSize.x && size.y == m_thumbSize.y)
    return;

  m_thumbSize = size;

  // Made at another size: load them again
  ReleaseThumbnails ( );
}

void
SKIF_LibraryArtwork::RequestThumbnail (app_record_s* pApp)
{
  constexpr int maxLoads  = 16; // From the cache: local only
  constexpr int maxBuilds = 16; // Mostly waiting on downloads (Steam's store API, the PCGamingWiki / IGDB cover proxies)

  auto& thumb = pApp->tex_thumb;

  thumb.lastUsed  = SKIF_Util_timeGetTime ( );
  std::atomic_ref <int> (thumb.lastFrame).store (ImGui::GetFrameCount ( ), std::memory_order_relaxed); // Read by its worker (wanted)

  // The one retry of a build that found no cover (see ApplyWorkerResults)
  if (thumb.iWorker == SKIF_Thumbnail_Done && thumb.texture.p == nullptr &&
      thumb.retryAt != 0 && static_cast <LONG> (thumb.lastUsed - thumb.retryAt) >= 0)
  {
    thumb.retryAt = 0;
    thumb.iWorker = SKIF_Thumbnail_NoCache;
  }

  // Not until SKIF has started (the hardware device is created after the first present), nor before a size is set
  if (! SKIF_StartupFinished || m_thumbSize.x <= 0.0f)
    return;

  if      (thumb.iWorker == SKIF_Thumbnail_None    && m_activeThumbLoads  < maxLoads)
    StartThumbnailWorker (pApp, false);

  else if (thumb.iWorker == SKIF_Thumbnail_NoCache && m_activeThumbBuilds < maxBuilds)
    StartThumbnailWorker (pApp, true);
}

void
SKIF_LibraryArtwork::StartThumbnailWorker (app_record_s* pApp, bool build)
{
  struct thread_s {
    std::shared_ptr <app_record_s>
                        app;        // Kept alive until the result is applied, even if a library refresh replaces it
    app_key_s           game;
    int                 request    = 0;
    unsigned int        generation = 0;
    bool                build      = false;
    bool                queryPCGW  = false;
    library_thumbnail_s thumb;
  };

  thread_s* data = new thread_s;

  data->app        = SKIF_GamingCollection::Get ( ).FindShared (pApp->GetKey ( ));
  data->game       = pApp->GetKey ( );
  data->request    = ++m_thumbRequests;
  data->generation = m_generation.load ( );
  data->build      = build;
  data->thumb      = { m_thumbSize.x, m_thumbSize.y, GetThumbnailCachePath (pApp) };

  // Still needed: its tile was drawn in the grid's latest frame (or the one being drawn). The worker's record is kept
  //   alive by data->app; its lastFrame is written by the main thread (RequestThumbnail), hence atomic_ref.
  if (app_record_s* rec = data->app.get ( ); rec != nullptr)
  {
    data->thumb.wanted = [rec](void) -> bool
    {
      return std::atomic_ref <int> (rec->tex_thumb.lastFrame).load (std::memory_order_relaxed) >=
             SKIF_LibraryArtwork::Get ( ).m_gridFrame.load (std::memory_order_relaxed);
    };
  }

  // Not in the library (nothing to keep alive): show it without a cover
  if (data->app == nullptr)
  {
    delete data;
    pApp->tex_thumb.iWorker = SKIF_Thumbnail_Done;
    return;
  }

  // A build needs what selecting the game would have loaded (Steam: appinfo.vdf, which says where the Steam client keeps
  //   the game's cover; without it the cover can only be looked up online). Main thread, under the lock, as on selection.
  if (build && pApp->HasPendingStoreDetails ( ))
  {
    std::scoped_lock app_lock (SKIF_GamingCollection::Get ( ).GetMutex ( ));
    pApp->LoadStoreDetails ( );
  }

  // As the cover worker: a PCGamingWiki cover is looked up once per record (decided here, on the main thread)
  if (build && pApp->WantsPCGWCover ( ) && ! pApp->tex_cover.queriedPCGW)
  {
    pApp->tex_cover.queriedPCGW = true;
    data->queryPCGW             = true;
  }

  pApp->tex_thumb.request = data->request;
  pApp->tex_thumb.iWorker = (build) ? SKIF_Thumbnail_Building : SKIF_Thumbnail_Loading;

  if (build)
    m_activeThumbBuilds++;
  else
    m_activeThumbLoads++;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    thread_s* _data = static_cast <thread_s*> (var);

    SKIF_Util_SetThreadDescription (GetCurrentThread (), (_data->build) ? L"SKIF_LibThumbnailBuilder"
                                                                        : L"SKIF_LibThumbnailWorker");

    CoInitializeEx (nullptr, 0x0); // WIC: decoding, and saving the thumbnail cache

    SKIF_LibraryArtwork& self = SKIF_LibraryArtwork::Get ( );

    worker_result_s result;
    result.type       = LibraryTexture::Thumbnail;
    result.game       = _data->game;
    result.queuePos   = _data->request;
    result.generation = _data->generation;
    result.build      = _data->build;

    const std::wstring& cachePath = _data->thumb.cachePath;

    // Whether its tile is still in view (drawn in the grid's latest frame). One that went out of view while this worker
    //   waited or worked makes no texture: one finished while SKIF is asleep would otherwise stay in memory until SKIF
    //     wakes up and applies it. It loads again once the tile is drawn again (the main thread sets it back to not loaded).
    auto _Wanted = [&](void) -> bool { return (! _data->thumb.wanted || _data->thumb.wanted ( )); };

    if (! _Wanted ( ))
      result.dropped = true;

    // From the cache, if it is current; otherwise the main thread schedules a build (once the tile is drawn again)
    else if (! _data->build)
    {
      if (cachePath.empty ( ) || ! _IsThumbnailCacheCurrent (cachePath))
        result.noCache = true;

      else if (! LoadLibraryThumbnailFromCache (cachePath, result.texture, &_data->thumb))
      {
        if (_Wanted ( ))
          result.noCache = true; // It doesn't decode: build it again
        else
          result.dropped = true;
      }

      // An animated custom cover plays while its tile is hovered. The cache does not say which file it was made from, but
      //   a custom cover always comes first. (A build knows the file, see LoadLibraryTextureEx; the covers stores offer
      //     are not animated in practice, so only custom covers are checked here.)
      else if (std::wstring custom = _data->app->FindCustomArtwork (LibraryTexture::Cover); ! custom.empty ( ) && IsAnimatedImageFile (custom))
        result.texture.animatedSource = std::move (custom);
    }

    // The cover pipeline (may download the cover), scaled down and saved to the cache (LoadLibraryTextureEx; the cache
    //   is saved even if the texture is no longer wanted by then)
    else
    {
      if (! cachePath.empty ( ))
      {
        std::error_code ec;
        std::filesystem::create_directories (std::filesystem::path (cachePath).parent_path ( ), ec);
        std::filesystem::remove             (cachePath, ec); // A build replaces it
        _DeleteThumbnails (cachePath, false); // Only outdated ones and leftovers
      }

      _LoadCover (_data->app.get ( ), _data->queryPCGW, result.texture, &_data->thumb);

      if (result.texture.srv.p == nullptr && ! _Wanted ( ))
        result.dropped = true;
    }

    // It went out of view just as the texture was made: release it here (it was never drawn)
    if (result.texture.srv.p != nullptr && ! _Wanted ( ))
    {
      result.texture.srv.Release ( );
      result.dropped = true;

      // Destroyed once the main thread flushes (it does before it sleeps)
      SKIF_D3D11_FlushPending.store (true);
    }

    // The record goes back with the result, so it is released on the main thread
    result.record = std::move (_data->app);

    delete _data;

    self.m_results.push (result);
    result.record.reset ( );

    // Force a frame, so the result is applied
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

    return 0;
  }, data, 0x0, nullptr);

  if (hWorkerThread != NULL)
    m_threads.push_back ({ LibraryTexture::Thumbnail, pApp->GetKey ( ), hWorkerThread, pApp->tex_thumb.request }); // Closed once its result is applied

  else
  {
    PLOG_ERROR << "Failed to start a thumbnail worker thread!";

    delete data;

    pApp->tex_thumb.iWorker = SKIF_Thumbnail_Done; // Shown without a cover

    if (build)
      m_activeThumbBuilds--;
    else
      m_activeThumbLoads--;
  }
}

void
SKIF_LibraryArtwork::ReleaseThumbnail (app_record_s* pApp)
{
  auto& thumb = pApp->tex_thumb;

  if (thumb.texture.p != nullptr)
  {
    SKIF_ResourcesToFree.push (thumb.texture.p);
    thumb.texture.p = nullptr;

    if (m_thumbsLoaded > 0)
      m_thumbsLoaded--;
  }

  // A load in flight is dropped when it comes back (another request, or none)
  thumb.iWorker  = SKIF_Thumbnail_None;
  thumb.request  = 0;
  thumb.loadedAt = 0;
  thumb.retryAt  = 0;
  thumb.retried  = false;
  thumb.animatedSource.clear ( );
}

void
SKIF_LibraryArtwork::ReleaseThumbnails (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  for (auto& app : _games.GetApps ( ))
    if (app.second != nullptr)
      ReleaseThumbnail (app.second.get ( ));

  m_thumbsLoaded = 0;
}

void
SKIF_LibraryArtwork::InvalidateThumbnail (app_record_s* pApp)
{
  ReleaseThumbnail (pApp);

  // Its cover changed (also when it plays from the same file name, e.g. a new custom cover.png)
  ResetAnimatedCover (pApp->GetKey ( ));

  // Deleted here, so a load started before the cover changed can't pick it up again
  std::wstring cachePath = GetThumbnailCachePath (pApp);

  if (! cachePath.empty ( ))
    _DeleteThumbnails (cachePath, true); // Every cache file of the game, this one included
}

// Thumbnails out of view are unloaded once they have been for a while (s_thumbUnloadAfter). In view means drawn in the
//   grid's last frame. SKIF stops rendering while nothing happens, so this can't rely on frames: while some loaded
//     thumbnails are out of view, a timer (cIDT_TIMER_THUMBNAILS) runs this every second, also while SKIF is asleep.
//       The tiles in view aren't drawn then either, which is why it goes by the grid's last frame rather than by how long
//         ago a tile was drawn.
static constexpr DWORD s_thumbUnloadAfter = 5000; // ms out of view
static constexpr DWORD s_thumbTimerPeriod = 1000; // ms between the timer's checks
static constexpr int   s_thumbBudget      = 300;  // Thumbnails kept loaded at most

// SetTimer, asking Windows not to coalesce it: in efficiency mode (idle priority + EcoQoS) Windows otherwise delays the
//   timers of SKIF by several seconds. SetCoalescableTimer is Windows 8+ (the headers target Windows 7), so it is looked up.
static UINT_PTR
_SetThumbnailTimer (HWND hWnd, UINT_PTR id, UINT elapse)
{
  using SetCoalescableTimer_pfn = UINT_PTR (WINAPI *)(HWND, UINT_PTR, UINT, TIMERPROC, ULONG);

  static SetCoalescableTimer_pfn
    _SetCoalescableTimer = reinterpret_cast <SetCoalescableTimer_pfn> (GetProcAddress (GetModuleHandleW (L"user32.dll"), "SetCoalescableTimer"));

  constexpr ULONG NoCoalescing = 0xFFFFFFFE; // TIMERV_NO_COALESCING

  if (_SetCoalescableTimer != nullptr)
    return _SetCoalescableTimer (hWnd, id, elapse, NULL, NoCoalescing);

  return SetTimer (hWnd, id, elapse, NULL);
}

int
SKIF_LibraryArtwork::UnloadThumbnailsOutOfView (bool now_)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  const DWORD now      = SKIF_Util_timeGetTime ( );
  int         unloaded = 0;
  int         waiting  = 0; // Out of view, but not long enough yet

  static std::vector <app_record_s*> loaded; // Static to keep its memory between frames
  loaded.clear ( );

  for (auto& app : _games.GetApps ( ))
  {
    if (app.second == nullptr || app.second->tex_thumb.texture.p == nullptr)
      continue;

    auto& thumb = app.second->tex_thumb;

    if (thumb.lastFrame != m_gridFrame.load ( )) // Out of view
    {
      if (now_ || now - thumb.lastUsed >= s_thumbUnloadAfter)
      {
        ReleaseThumbnail (app.second.get ( ));
        unloaded++;
        continue;
      }

      waiting++;
    }

    loaded.push_back (app.second.get ( ));
  }

  // Counted anew (a library refresh frees the thumbnails of the games it removes)
  m_thumbsLoaded = static_cast <int> (loaded.size ( ));

  // Over the budget: the ones drawn the longest ago go first (the ones in view were drawn last)
  if (static_cast <int> (loaded.size ( )) > s_thumbBudget)
  {
    std::sort (loaded.begin ( ), loaded.end ( ), [](const app_record_s* a, const app_record_s* b) { return a->tex_thumb.lastUsed < b->tex_thumb.lastUsed; });

    for (size_t i = 0; i < loaded.size ( ) - s_thumbBudget; i++)
    {
      ReleaseThumbnail (loaded [i]);
      unloaded++;
    }

    m_thumbsLoaded = s_thumbBudget;
  }

  if (unloaded > 0)
    PLOG_VERBOSE << "Unloaded " << unloaded << " thumbnail(s) out of view; " << waiting << " more out of view, " << m_thumbsLoaded << " loaded.";

  // The timer runs while some are out of view, and stops once none are
  if (waiting > 0 && ! m_thumbTimer && SKIF_Notify_hWnd != NULL)
    m_thumbTimer = (_SetThumbnailTimer (SKIF_Notify_hWnd, cIDT_TIMER_THUMBNAILS, s_thumbTimerPeriod) != 0);

  else if (waiting == 0 && m_thumbTimer)
  {
    KillTimer (SKIF_Notify_hWnd, cIDT_TIMER_THUMBNAILS);
    m_thumbTimer = false;
  }

  return unloaded;
}

// Once per frame of the library tab (after the grid)
void
SKIF_LibraryArtwork::UpdateThumbnails (bool gridShown)
{
  // Not shown: unload all of them (once)
  if (! gridShown)
  {
    if (m_thumbsLoaded > 0)
      ReleaseThumbnails ( );

    if (m_thumbTimer)
    {
      KillTimer (SKIF_Notify_hWnd, cIDT_TIMER_THUMBNAILS);
      m_thumbTimer = false;
    }

    return;
  }

  // The tiles in view were drawn in this frame
  m_gridFrame.store (ImGui::GetFrameCount ( ));

  // Every frame, so the timer runs for whatever went out of view before SKIF stops rendering (cheap: it only does
  //   anything for the loaded thumbnails, at most the budget)
  UnloadThumbnailsOutOfView ( );
}

// WM_TIMER cIDT_TIMER_THUMBNAILS (SKIF.cpp), every second while some are out of view: runs while SKIF is asleep too.
//   Only what was unloaded needs frames (that is when the textures are released, SKIF_ResourcesToFree).
bool
SKIF_LibraryArtwork::OnThumbnailTimer (void)
{
  return (UnloadThumbnailsOutOfView ( ) > 0); // Stops the timer once none are out of view
}

#pragma endregion

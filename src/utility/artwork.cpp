#include <utility/artwork.h>
#include <utility/games.h>
#include <utility/registry.h>
#include <utility/utility.h>
#include <stores/generic_library2.h>
#include <stores/Steam/steam_library.h>
#include <SKIF.h>
#include <concurrent_queue.h>
#include <process.h>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <utility/sk_utility.h>
#include <utility/fsutil.h>
#include <unordered_set>
#include <map>
#include <mutex>
#include <dxgi1_4.h>

extern concurrency::concurrent_queue <IUnknown *> SKIF_ResourcesToFree;

static bool _IsThumbnailCacheCurrent (const std::wstring& cachePath); // Thumbnails

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

// Main thread: once per library (swap-in), tells the Steam cover lookups which games to look up ahead (in library order),
//   so a cover that is downloaded doesn't wait on a store API request of its own (SKIF_Steam_GetCoverURI)
static void
_UpdateSteamCoverURICandidates (void)
{
  static uint32_t lastSwap = UINT32_MAX;

  SKIF_GamingCollection& games = SKIF_GamingCollection::Get ( );

  if (games.GetSwapCount ( ) == lastSwap)
    return;

  lastSwap = games.GetSwapCount ( );

  std::vector <uint32_t> ids;

  {
    std::scoped_lock app_lock (games.GetMutex ( ));

    for (const auto& app : games.GetApps ( ))
    {
      const app_record_s* rec = app.second.get ( );

      if (rec != nullptr && rec->store == app_record_s::Store::Steam && rec->id != 0 && ! rec->IsSpecialK ( ))
        ids.push_back (rec->id);
    }
  }

  SKIF_Steam_SetCoverURICandidates (std::move (ids));
}

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

  if (pApp != nullptr && pApp->store == app_record_s::Store::Steam)
    _UpdateSteamCoverURICandidates ( );

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
      {
        thumb.iWorker = SKIF_Thumbnail_NoCache;

        // Shown until the build replaces it: the thumbnail of the other size
        if (result.placeholder && result.texture.srv.p != nullptr)
        {
          if (thumb.texture.p != nullptr)
          {
            _Release (thumb.texture);
            m_thumbsLoaded--;
          }

          thumb.width    = result.texture.resolution.x;
          thumb.height   = result.texture.resolution.y;
          thumb.loadedAt = SKIF_Util_timeGetTime ( );
          thumb.texture.Attach (result.texture.srv.Detach ( ));
          m_thumbsLoaded++;
        }
      }

      else
      {
        thumb.iWorker   = SKIF_Thumbnail_Done;
        thumb.isCustom  = result.texture.isCustom;
        thumb.isManaged = result.texture.isManaged;
        thumb.animatedSource = result.texture.animatedSource; // Plays while its tile is hovered (UpdateAnimatedCover)

        // Replacing a placeholder (the other size's thumbnail, shown while this one was built) keeps its fade-in time, so
        //   the cover does not fade in a second time; otherwise a thumbnail is released before it loads again
        const bool hadPlaceholder = (thumb.texture.p != nullptr);

        if (! hadPlaceholder || result.texture.srv.p != nullptr)
          thumb.loadedAt = (hadPlaceholder) ? thumb.loadedAt : SKIF_Util_timeGetTime ( );

        if (hadPlaceholder && result.texture.srv.p != nullptr)
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
      // A replacement comes after the worker's first result, which ended its turn
      if (! result.replace)
        m_animWorkerBusy = false;

      const bool live = (result.queuePos   == m_animRequest.load ( ) &&
                         result.generation == m_generation.load  ( ) &&
                         result.game       == m_animGame             &&
                         m_animState       == ((result.replace) ? AnimatedCoverState::Ready : AnimatedCoverState::Loading));

      if (live && result.frames.size ( ) >= 2 && result.frames.size ( ) == result.delays.size ( ))
      {
        // Its compressed frames, replacing the uncompressed ones: the same animation, so it plays on from where it is
        if (result.replace)
        {
          for (auto& frame : m_animFrames)
            _Release (frame);

          m_animFrames   .clear ( );
          m_animFrameEnds.clear ( );
        }

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

      // Not animated after all, too large, or not decodable: it stays a still cover (a replacement that failed: it plays on
      //   as it is)
      else if (live && ! result.replace)
        m_animState    = AnimatedCoverState::Failed;

      for (auto& frame : result.frames)
        _Release (frame.srv);

      result.frames.clear ( );
      result.delays.clear ( );
      continue;
    }

    // Animated thumbnails (the grid view): only the latest request of the game, made on the current device
    else if (result.type == LibraryTexture::AnimatedThumbnail)
    {
      if (result.build)
        m_animThumbBuilds--;
      else
        m_animThumbLoads--;

      anim_thumb_s* thumb = FindAnimatedThumbnail (result.game);

      const bool live = (thumb != nullptr                         &&
                         thumb->request    == result.queuePos     &&
                         thumb->state      == anim_thumb_s::State::Working &&
                         result.generation == m_generation.load ( ));

      if (live)
      {
        thumb->building = false;
        thumb->cached   = _IsThumbnailCacheCurrent (thumb->cachePath); // A build saved it (unless that failed)

        if (result.ok && result.frames.size ( ) >= 2 && result.frames.size ( ) == result.delays.size ( ))
        {
          thumb->bytes = result.bytes;

          // Room for them: unloads those out of view, or (hovered) those hovered least recently
          if (MakeAnimatedThumbnailRoom (thumb->bytes, thumb, thumb->hoveredFrame >= ImGui::GetFrameCount ( ) - 1))
          {
            DWORD end = 0;

            for (size_t i = 0; i < result.frames.size ( ); i++)
            {
              thumb->frames.emplace_back ( );
              thumb->frames.back ( ).Attach (result.frames [i].srv.Detach ( ));
              thumb->frameUVs.push_back (ImVec4 (result.frames [i].uv0.x, result.frames [i].uv0.y,
                                                 result.frames [i].uv1.x, result.frames [i].uv1.y));

              end += result.delays [i];
              thumb->frameEnds.push_back (end);
            }

            thumb->duration     = end;
            thumb->resolution   = result.frames [0].resolution;
            thumb->position     = 0;
            thumb->playingSince = 0;
            thumb->state        = anim_thumb_s::State::Loaded;
            m_animThumbBytes   += thumb->bytes;
          }

          // No room: loaded again once there is (or it is hovered). Without a cache it would be built again: not usable.
          else
            thumb->state = (thumb->cached) ? anim_thumb_s::State::Cached : anim_thumb_s::State::Failed;
        }

        // Too large for the limit, or not decodable: plays as the cover panel does (full size, when hovered)
        else
          thumb->state = anim_thumb_s::State::Failed;
      }

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

// How long the frames are kept once the cover is no longer hovered
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
  m_animPosition     = 0; // Starts from the first frame once loaded again

  // A load still running is no longer wanted (it stops, and its result is dropped)
  m_animRequest++;

  // Not loaded (a cover that could not be loaded is not tried again, until another cover has been hovered)
  if (m_animState != AnimatedCoverState::Failed)
    m_animState = AnimatedCoverState::None;

  StopAnimatedCoverTimer ( );

  return released;
}

bool
SKIF_LibraryArtwork::AreCoverAnimationsOn (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  if (_registry.iLibraryAnimatedCovers == 0)
    return false;

  // Play when hovered, or always
  if (_registry.iLibraryAnimatedCovers == 2 || _registry.iLibraryAnimatedCovers == 3)
    return true;

  // As Windows' "Animation effects" (Settings > Accessibility > Visual effects; on Windows 10: "Show animations in Windows").
  //   Read once every ten seconds, so a change applies while SKIF runs.
  const DWORD now = SKIF_Util_timeGetTime ( );

  if (m_animsCheckedAt == 0 || now - m_animsCheckedAt > 10000)
  {
    BOOL animations = TRUE;

    if (SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
      m_animsWindows = (animations != FALSE);

    m_animsCheckedAt = now;
  }

  return m_animsWindows;
}

void
SKIF_LibraryArtwork::HoverAnimatedCover (void)
{
  // Only a cover that is animated, shown, and not about to be replaced; and only while animations are on
  if (m_coverAnimSource.empty ( ) || m_pTexSRV.p == nullptr || m_loadCover || ! AreCoverAnimationsOn ( ))
    return;

  m_hoverSet  = true;
  m_hoverGame = m_cover;

  if (m_hoverSource != m_coverAnimSource)
    m_hoverSource = m_coverAnimSource;
}

void
SKIF_LibraryArtwork::HoverAnimatedThumbnail (app_record_s* pApp)
{
  // Only a thumbnail that is shown, of an animated cover; and only while animations are on
  if (pApp == nullptr || pApp->tex_thumb.texture.p == nullptr || pApp->tex_thumb.animatedSource.empty ( ) || ! AreCoverAnimationsOn ( ))
    return;

  m_hoverSet  = true;
  m_hoverGame = pApp->GetKey ( );

  if (m_hoverSource != pApp->tex_thumb.animatedSource)
    m_hoverSource = pApp->tex_thumb.animatedSource;
}

void
SKIF_LibraryArtwork::StartAnimatedCoverWorker (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  struct thread_s {
    std::wstring path;
    app_key_s    game;
    int          request    = 0;
    unsigned int generation = 0;
    std::wstring assetFolder; // Where its compressed frames are cached (empty: not cached)
  };

  thread_s* data = new thread_s { m_animSource, m_animGame, ++m_animRequest, m_generation.load ( ) };

  // Special K's asset folder is the Assets folder itself, which the cleanup of cover caches leaves alone: not cached there
  if (app_record_s* pApp = _games.Find (m_animGame); pApp != nullptr && ! pApp->IsSpecialK ( ))
    data->assetFolder = pApp->GetAssetDirectory ( );

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

    const int          request     = _data->request;
    const std::wstring path        = _data->path;
    const std::wstring assetFolder = _data->assetFolder;

    delete _data;

    // Its frames compressed (BC1 / BC3, as the grid view's are: a fraction of the video memory), cached in the game's asset
    //   folder. All the frames together are kept below this (as LoadAnimatedLibraryTexture keeps the uncompressed ones).
    constexpr size_t budget = 192ull * 1024 * 1024;

    auto _Wanted = [&self, request](void) -> bool { return self.m_animRequest.load ( ) == request; };

    library_thumbnail_s compressed;
    const bool          cacheable = GetAnimatedLibraryCoverCachePath (path, assetFolder, compressed);
    size_t              bytes     = 0;
    bool                tooLarge  = false;

    // The main thread swaps them in if this is still the latest request (ApplyWorkerResults). Forces a frame for that.
    auto _Push = [&self](worker_result_s&& done)
    {
      self.m_results.push (std::move (done));
      PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);
    };

    // Compressed, from its cache
    if (cacheable && _IsThumbnailCacheCurrent (compressed.cachePath))
    {
      compressed.wanted = _Wanted;

      if (LoadAnimatedLibraryThumbnail (path, compressed, true, budget, true, result.frames, result.delays, bytes, tooLarge))
      {
        _Push (std::move (result));

        PLOG_VERBOSE << "SKIF_LibAnimCoverWorker thread stopped!";
        return 0;
      }

      result.frames.clear ( );
      result.delays.clear ( );
    }

    // Not cached yet: uncompressed, right away (it plays meanwhile). Stops between frames once a newer request or a release
    //   made this one stale.
    LoadAnimatedLibraryTexture (path, result.frames, result.delays, _Wanted);

    const bool playable   = (result.frames.size ( ) >= 2);
    const auto generation = result.generation;
    const auto game       = result.game;

    // Builds its compressed frames (to the cache), for next time: to the end even if no longer shown (so a short hover makes
    //   them too), one at a time (CPU heavy: every frame is decoded, scaled and compressed, the latter on several threads),
    //     at below normal priority. Not in efficiency mode: on a hybrid CPU that keeps it on the slower cores, and the
    //       cover is being looked at.
    auto _Build = [&](void) -> bool
    {
      static std::mutex buildMutex;
      std::scoped_lock  lock (buildMutex);

      // Another worker may have built it while this one waited
      if (_IsThumbnailCacheCurrent (compressed.cachePath))
        return true;

      const int priority = GetThreadPriority (GetCurrentThread ( ));
      SetThreadPriority (GetCurrentThread ( ), THREAD_PRIORITY_BELOW_NORMAL);

      std::vector <library_texture_s> none;
      std::vector <DWORD>             delays;

      compressed.wanted = nullptr;
      LoadAnimatedLibraryThumbnail (path, compressed, false, budget, false, none, delays, bytes, tooLarge);

      SetThreadPriority (GetCurrentThread ( ), priority);

      return _IsThumbnailCacheCurrent (compressed.cachePath);
    };

    // Its compressed frames, from the cache just built, if it is still shown
    auto _LoadBuilt = [&](worker_result_s& into) -> bool
    {
      compressed.wanted = _Wanted;

      return (_Wanted ( ) && LoadAnimatedLibraryThumbnail (path, compressed, true, budget, true, into.frames, into.delays, bytes, tooLarge));
    };

    // Too large to play uncompressed (or not decodable): compressed it may fit. It shows still until they are built.
    if (! playable && cacheable && _Wanted ( ))
    {
      result.frames.clear ( );
      result.delays.clear ( );

      if (! _Build ( ) || ! _LoadBuilt (result))
      {
        result.frames.clear ( );
        result.delays.clear ( );
      }

      _Push (std::move (result));
    }

    else
    {
      _Push (std::move (result));

      // If it is still shown once they are built, its compressed frames replace the uncompressed ones (it plays on from where
      //   it is)
      if (playable && cacheable && _Build ( ))
      {
        worker_result_s replacement;
        replacement.type       = LibraryTexture::AnimatedCover;
        replacement.game       = game;
        replacement.queuePos   = request;
        replacement.generation = generation;
        replacement.replace    = true;

        if (_LoadBuilt (replacement))
          _Push (std::move (replacement));
      }
    }

    PLOG_VERBOSE << "SKIF_LibAnimCoverWorker thread stopped!";

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

  // Animations off (Settings, or Windows' "Animation effects"): every cover shows still, and the frames loaded go
  if (! AreCoverAnimationsOn ( ))
  {
    if (m_animGame != app_key_s { } || ! m_animSource.empty ( ))
      ResetAnimatedCover ( );

    if (! m_animThumbs.empty ( ))
      ResetAnimatedThumbnails ( );

    StopAnimatedCoverTimer ( );
    return;
  }

  // Another cover: its frames replace those of the one hovered before, and its hover starts now
  if (hovered && (m_hoverGame != m_animGame || m_hoverSource != m_animSource))
  {
    ResetAnimatedCover ( );

    m_animGame         = m_hoverGame;
    m_animSource       = m_hoverSource;
  }

  if (hovered)
  {
    m_animLastHovered = now;

    // Load the frames right away. One worker at a time: a stale one (the mouse moved on to another cover) stops at its next
    //   frame, and once its result is back the cover hovered by then loads (its result wakes SKIF, WM_SKIF_COVER)
    if (m_animState == AnimatedCoverState::None && ! m_animWorkerBusy)
      StartAnimatedCoverWorker ( );

    // Play: from where it paused (the first frame the first time), as grid tiles do
    if (m_animState == AnimatedCoverState::Ready && m_animDuration > 0)
    {
      if (m_animPlayingSince == 0)
        m_animPlayingSince = now;

      const DWORD position = (m_animPosition + (now - m_animPlayingSince)) % m_animDuration;
      const auto  frameEnd = std::upper_bound (m_animFrameEnds.begin ( ), m_animFrameEnds.end ( ), position);

      wakeIn = (frameEnd != m_animFrameEnds.end ( )) ? (*frameEnd - position) : 1;
    }
  }

  else
  {
    // Pause on the current frame (it stays shown, and the animation resumes from there)
    if (m_animPlayingSince != 0 && m_animDuration > 0)
    {
      m_animPosition     = (m_animPosition + (now - m_animPlayingSince)) % m_animDuration;
      m_animPlayingSince = 0;
    }

    if (m_animState == AnimatedCoverState::Ready && now - m_animLastHovered > s_animIdleRelease)
      ReleaseAnimatedCover ( );
  }

  // The grid view's animated covers (their hovered tile plays)
  if (const DWORD wakeGrid = UpdateAnimatedThumbnails (now); wakeGrid > 0 && (wakeIn == 0 || wakeGrid < wakeIn))
    wakeIn = wakeGrid;

  // Wake SKIF when the next frame is due (re-armed only when that changes). Once it fires, the frames it gives re-arm it.
  //   While anything plays, SKIF also keeps rendering (IsAnimatedCoverPlaying): the timer alone is not enough, as Windows
  //     coalesces and delays SKIF's timers (by seconds in efficiency mode, e.g. while its window is not focused).
  if (wakeIn > 0)
  {
    m_animPlayingFrame = ImGui::GetFrameCount ( );

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
  if (game != m_animGame || m_animState != AnimatedCoverState::Ready || m_animDuration == 0)
    return nullptr;

  // Never played: the cover itself (its first frame, or an APNG's default image)
  if (m_animPlayingSince == 0 && m_animPosition == 0)
    return nullptr;

  // Playing, or paused
  const DWORD position = (m_animPlayingSince != 0) ? (m_animPosition + (SKIF_Util_timeGetTime ( ) - m_animPlayingSince)) % m_animDuration
                                                   :  m_animPosition;
  const auto  frameEnd = std::upper_bound (m_animFrameEnds.begin ( ), m_animFrameEnds.end ( ), position);
  const auto  frame    = static_cast <size_t> (std::distance (m_animFrameEnds.begin ( ), frameEnd));

  if (frame >= m_animFrames.size ( ))
    return nullptr;

  if (resolution != nullptr)
    *resolution = m_animResolution;

  return m_animFrames [frame].p;
}

bool
SKIF_LibraryArtwork::IsAnimatedCoverPlaying (void) const
{
  // Only for the frame it was set in: once the library tab is no longer drawn (another tab, SKIF minimized), it is not
  return (m_animPlayingFrame == ImGui::GetFrameCount ( ));
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
SKIF_LibraryArtwork::OnCoverSaved (app_key_s game, bool notify, bool success)
{
  m_refreshPending = true;
  m_refresh        = game;

  // How it went, if a notification was asked for (an image dropped on a game), and always if it failed
  if (notify || ! success)
    m_coverSavedNotices.push_back ({ game, success });

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
SKIF_LibraryArtwork::PopCoverSavedNotice (app_key_s& game, bool& success)
{
  if (m_coverSavedNotices.empty ( ))
    return false;

  game    = m_coverSavedNotices.front ( ).first;
  success = m_coverSavedNotices.front ( ).second;

  m_coverSavedNotices.erase (m_coverSavedNotices.begin ( ));

  return true;
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
  // Store-specific icon source (Steam: parses appinfo.vdf for the icon hash first). Not possible yet (Steam: the early
  //   library is shown before appinfo.vdf is read): tex_icon.iWorker stays 0, so the row asks again on a later frame
  if (! pApp->PrepareIconSource ( ))
    return;

  m_activeIconWorkers++;

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

  // The animated cover's frames load again once it is hovered (the cover itself is loaded again, ReloadCover), and those
  //   of the grid view once their tiles are shown
  ReleaseAnimatedCover    ( );
  ResetAnimatedThumbnails ( );

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

      const auto ext = entry.path ( ).extension ( );

      outdated = ((ext != L".dds" && ext != L".anim") || ec2 || (hasCover && time < newest)); // .anim: animated covers (grid view)
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
  return GetThumbnailCachePath (pApp, m_thumbSize);
}

std::wstring
SKIF_LibraryArtwork::GetThumbnailCachePath (app_record_s* pApp, ImVec2 size) const
{
  std::wstring assetDir = pApp->GetAssetDirectory ( );

  if (assetDir.empty ( ) || size.x <= 0.0f)
    return L"";

  // Named after what it is made from: its size (one per DPI scale), and the cover sources preferred for the store
  //   (Settings). A change of either uses another file; the others are kept while current (_DeleteThumbnails).
  return SK_FormatStringW (L"%wscover-thumb-%ix%i%ws%ws.dds", assetDir.c_str ( ),
                             static_cast <int> (size.x), static_cast <int> (size.y),
                               (pApp->WantsPCGWCover ( )) ? L"-pcgw" : L"",
                               (pApp->WantsIGDBCover ( )) ? L"-igdb" : L"");
}

void
SKIF_LibraryArtwork::SetThumbnailSize (ImVec2 size, ImVec2 placeholderSize)
{
  size = ImVec2 (std::floor (size.x), std::floor (size.y));

  // Only used for loads started from now on
  m_thumbPlaceholderSize = ImVec2 (std::floor (placeholderSize.x), std::floor (placeholderSize.y));

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
  constexpr int maxBuilds = 32; // Mostly waiting on cover downloads (Steam's CDN; the PCGamingWiki / IGDB cover proxies). The Steam
                                //   store API lookups are batched (SKIF_Steam_GetCoverURI), so they no longer hold builds up for long

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
    std::wstring        placeholderPath; // Loads: the cache of the placeholder size, shown if this size has none yet
  };

  thread_s* data = new thread_s;

  data->app        = SKIF_GamingCollection::Get ( ).FindShared (pApp->GetKey ( ));
  data->game       = pApp->GetKey ( );
  data->request    = ++m_thumbRequests;
  data->generation = m_generation.load ( );
  data->build      = build;
  data->thumb      = { m_thumbSize.x, m_thumbSize.y, GetThumbnailCachePath (pApp) };

  if (! build && m_thumbPlaceholderSize.x > 0.0f && m_thumbPlaceholderSize.x != m_thumbSize.x)
    data->placeholderPath = GetThumbnailCachePath (pApp, m_thumbPlaceholderSize);

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

  if (build && pApp->store == app_record_s::Store::Steam)
    _UpdateSteamCoverURICandidates ( );

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

      // To be built: until then, the thumbnail of the other size (e.g. just after the cover size slider crossed into the
      //   other size tier), drawn scaled to the tile. Not checked for animation: the built one is.
      if (result.noCache && ! _data->placeholderPath.empty ( ) && _IsThumbnailCacheCurrent (_data->placeholderPath))
      {
        result.texture = { };

        if (LoadLibraryThumbnailFromCache (_data->placeholderPath, result.texture, &_data->thumb))
          result.placeholder = true;
      }
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

  // Their animated covers too (made at the same size, from the same sources)
  ResetAnimatedThumbnails ( );
}

void
SKIF_LibraryArtwork::InvalidateThumbnail (app_record_s* pApp)
{
  ReleaseThumbnail (pApp);

  // Its cover changed (also when it plays from the same file name, e.g. a new custom cover.png)
  ResetAnimatedCover     (pApp->GetKey ( ));
  ResetAnimatedThumbnail (pApp->GetKey ( ));

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

#pragma region Animated Thumbnails

// SKIF's video memory in use (bytes): the local memory of the device's adapter used by this process, which is what Task
//   Manager shows as "Dedicated GPU memory". 0 if unknown (no device, or the hardware device is still being switched to).
extern ID3D11Device* SKIF_pd3dDevice; // SKIF.cpp

static uint64_t
_QueryVideoMemoryInUse (void)
{
  if (SKIF_pd3dDevice == nullptr)
    return 0;

  CComQIPtr <IDXGIDevice> pDXGIDevice (SKIF_pd3dDevice);
  CComPtr   <IDXGIAdapter> pAdapter;

  if (pDXGIDevice == nullptr || FAILED (pDXGIDevice->GetAdapter (&pAdapter.p)))
    return 0;

  CComQIPtr <IDXGIAdapter3> pAdapter3 (pAdapter);
  DXGI_QUERY_VIDEO_MEMORY_INFO info = { };

  if (pAdapter3 == nullptr || FAILED (pAdapter3->QueryVideoMemoryInfo (0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
    return 0;

  return info.CurrentUsage;
}

// What a texture takes: its format at its size, for each mip and array slice. Shared textures are counted once (seen).
static size_t
_TextureBytes (ID3D11ShaderResourceView* pSRV, std::unordered_set <ID3D11Resource*>& seen)
{
  if (pSRV == nullptr)
    return 0;

  CComPtr <ID3D11Resource> pResource;
  pSRV->GetResource (&pResource.p);

  if (pResource == nullptr || ! seen.insert (pResource.p).second)
    return 0;

  CComQIPtr <ID3D11Texture2D> pTexture (pResource);

  if (pTexture == nullptr)
    return 0;

  D3D11_TEXTURE2D_DESC desc = { };
  pTexture->GetDesc (&desc);

  size_t bytes = 0;

  for (UINT mip = 0; mip < desc.MipLevels; mip++)
  {
    size_t rowPitch   = 0,
           slicePitch = 0;

    if (SUCCEEDED (DirectX::ComputePitch (desc.Format, std::max (1u, desc.Width >> mip), std::max (1u, desc.Height >> mip), rowPitch, slicePitch)))
      bytes += slicePitch;
  }

  return bytes * desc.ArraySize;
}

SKIF_LibraryArtwork::video_memory_s
SKIF_LibraryArtwork::GetVideoMemory (bool fresh) const
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  const DWORD now = SKIF_Util_timeGetTime ( );

  if (! fresh && m_vramQueriedAt != 0 && now - m_vramQueriedAt < 1000)
    return m_vram;

  video_memory_s                      out;
  std::unordered_set <ID3D11Resource*> seen;

  out.inUse = _QueryVideoMemoryInUse ( );

  // The grid view's animated covers: their texture arrays' data
  for (auto& thumb : m_animThumbs)
  {
    if (thumb->state != anim_thumb_s::State::Loaded)
      continue;

    for (auto& frame : thumb->frames)
      out.animBytes += _TextureBytes (frame.p, seen);

    out.animFrames += thumb->frames.size ( );
    out.animLoaded++;
  }

  // The full-size animated cover
  for (auto& frame : m_animFrames)
    out.animCoverBytes += _TextureBytes (frame.p, seen);

  out.animCoverFrames = m_animFrames.size ( );

  // The cover panel's cover, and the one fading out
  out.coverBytes = _TextureBytes (m_pTexSRV.p, seen) + _TextureBytes (m_pTexSRV_old.p, seen);

  // The games' thumbnails and icons
  for (auto& app : _games.GetApps ( ))
  {
    if (app.second == nullptr)
      continue;

    if (const size_t bytes = _TextureBytes (app.second->tex_thumb.texture.p, seen); bytes > 0)
    {
      out.thumbBytes += bytes;
      out.thumbs++;
    }

    if (const size_t bytes = _TextureBytes (app.second->tex_icon.texture.p, seen); bytes > 0)
    {
      out.iconBytes += bytes;
      out.icons++;
    }
  }

  m_vram          = out;
  m_vramQueriedAt = now;

  return out;
}

// Logs SKIF's video memory in use, and what its textures take, once the animated covers (the grid view's and the full-size
//   one) or the cover changed, and then stayed the same for two seconds with no worker running (released textures are
//     freed some frames later). Comparing two lines tells how much video memory each really takes. Called every frame on
//       any tab, so it also logs once they are all unloaded (e.g. turned off in Settings).
void
SKIF_LibraryArtwork::LogVideoMemory (DWORD now)
{
  // What is loaded (cheap to tell every frame): the grid view's animated covers, the full-size one, and the cover
  const uint64_t state = static_cast <uint64_t> (m_animThumbBytes) ^ (static_cast <uint64_t> (m_animFrames.size ( )) << 40) ^
                         static_cast <uint64_t> (reinterpret_cast <uintptr_t> (m_pTexSRV.p)) ^
                        (static_cast <uint64_t> (reinterpret_cast <uintptr_t> (m_pTexSRV_old.p)) << 1);

  if (state != m_vramPendingState)
  {
    m_vramPendingState = state;
    m_vramChangedAt    = now;
    return;
  }

  if (state == m_vramLoggedState || now - m_vramChangedAt < 2000 || m_animThumbBuilds > 0 || m_animThumbLoads > 0 || m_animWorkerBusy)
    return;

  // Measured now (not the figure Settings shows, which may be from before the last textures were freed)
  const video_memory_s vram = GetVideoMemory (true);

  if (vram.inUse == 0)
    return;

  constexpr double MB = 1024.0 * 1024.0;

  const size_t known = vram.Known ( );
  const double other = (static_cast <double> (vram.inUse) - static_cast <double> (known)) / MB;

  std::string change;

  // Since the last line: what SKIF's textures and its video memory changed by
  if (m_vramLoggedState != UINT64_MAX && m_vramLogged.inUse != 0)
  {
    const double dataChange = (static_cast <double> (known)      - static_cast <double> (m_vramLogged.Known ( ))) / MB,
                 usedChange = (static_cast <double> (vram.inUse) - static_cast <double> (m_vramLogged.inUse))   / MB;

    change = SK_FormatString ("; since the last measurement: %+.1f MB of textures, %+.1f MB of video memory", dataChange, usedChange);

    if (std::abs (dataChange) >= 1.0)
      change += SK_FormatString (" (%.2fx)", usedChange / dataChange);
  }

  PLOG_DEBUG << "[Video memory] SKIF's video memory in use: " << SK_FormatString ("%.1f", vram.inUse / MB) << " MB. "
             << SK_FormatString ("Animated covers of the grid view: %.1f MB (%d, %zu frames); ", vram.animBytes / MB, vram.animLoaded, vram.animFrames)
             << SK_FormatString ("the full-size animated cover: %.1f MB (%zu frames); ",          vram.animCoverBytes / MB, vram.animCoverFrames)
             << SK_FormatString ("covers: %.1f MB; thumbnails: %.1f MB (%d); icons: %.1f MB (%d); ", vram.coverBytes / MB, vram.thumbBytes / MB, vram.thumbs,
                                                                                                vram.iconBytes / MB, vram.icons)
             << SK_FormatString ("other (the swap chain, fonts, the driver): %.1f MB", other) << change << ".";

  m_vramLoggedState = state;
  m_vramLogged      = vram;
}

size_t
SKIF_LibraryArtwork::GetAnimatedThumbnailBudget (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return static_cast <size_t> (std::clamp (_registry.iLibraryGridAnimMemory, 0, 1024)) * 1024 * 1024;
}

SKIF_LibraryArtwork::anim_thumb_s*
SKIF_LibraryArtwork::FindAnimatedThumbnail (app_key_s game) const
{
  for (auto& thumb : m_animThumbs)
    if (thumb->game == game)
      return thumb.get ( );

  return nullptr;
}

void
SKIF_LibraryArtwork::ReleaseAnimatedThumbnail (anim_thumb_s& thumb)
{
  if (thumb.state != anim_thumb_s::State::Loaded)
    return;

  for (auto& frame : thumb.frames)
  {
    if (frame.p != nullptr)
    {
      SKIF_ResourcesToFree.push (frame.p);
      frame.p = nullptr;
    }
  }

  thumb.frames   .clear ( );
  thumb.frameEnds.clear ( );
  thumb.frameUVs .clear ( );
  thumb.duration     = 0;
  thumb.position     = 0; // Back to the still thumbnail
  thumb.playingSince = 0;
  thumb.state        = (thumb.cached) ? anim_thumb_s::State::Cached : anim_thumb_s::State::Unknown;

  m_animThumbBytes  -= std::min (m_animThumbBytes, thumb.bytes);
}

void
SKIF_LibraryArtwork::ResetAnimatedThumbnail (app_key_s game)
{
  for (auto it = m_animThumbs.begin ( ); it != m_animThumbs.end ( ); ++it)
  {
    if ((*it)->game == game)
    {
      // A worker still running has its result dropped (the game is not found, or has a newer request)
      ReleaseAnimatedThumbnail (**it);
      m_animThumbs.erase (it);
      return;
    }
  }
}

void
SKIF_LibraryArtwork::ResetAnimatedThumbnails (void)
{
  for (auto& thumb : m_animThumbs)
    ReleaseAnimatedThumbnail (*thumb);

  m_animThumbs.clear ( );
  m_animThumbBytes = 0;
}

bool
SKIF_LibraryArtwork::ReleaseAnimatedThumbnails (void)
{
  bool released = false;

  for (auto& thumb : m_animThumbs)
  {
    if (thumb->state == anim_thumb_s::State::Loaded)
    {
      ReleaseAnimatedThumbnail (*thumb);
      released = true;
    }
  }

  return released;
}

bool
SKIF_LibraryArtwork::AreCoverAnimationsAutomatic (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return (_registry.iLibraryAnimatedCovers == 1);
}

bool
SKIF_LibraryArtwork::AreCoverAnimationsAlways (void) const
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  return (_registry.iLibraryAnimatedCovers == 3);
}

void
SKIF_LibraryArtwork::OnAnimatedThumbnailMemoryChanged (void)
{
  // Known again the next time the grid view is shown
  m_animThumbsDidNotFit = false;

  // Off: as before (they play when hovered, see HoverAnimatedThumbnail)
  if (GetAnimatedThumbnailBudget ( ) == 0)
  {
    ResetAnimatedThumbnails ( );
    return;
  }

  // Lowered: unload the least recently used ones until the rest fit
  MakeAnimatedThumbnailRoom (0, nullptr, true);

  // Raised: those too large before may fit now
  for (auto& thumb : m_animThumbs)
    if (thumb->state == anim_thumb_s::State::Failed)
      thumb->state = (thumb->cached) ? anim_thumb_s::State::Cached : anim_thumb_s::State::Unknown;
}

bool
SKIF_LibraryArtwork::MakeAnimatedThumbnailRoom (size_t bytes, const anim_thumb_s* keep, bool hovered)
{
  const size_t budget = GetAnimatedThumbnailBudget ( );
  const int    frame  = ImGui::GetFrameCount ( );

  if (bytes > budget)
    return false;

  while (m_animThumbBytes + bytes > budget)
  {
    anim_thumb_s* victim = nullptr;

    // Out of view: the least recently shown
    for (auto& thumb : m_animThumbs)
    {
      if (thumb.get ( ) == keep || thumb->state != anim_thumb_s::State::Loaded || thumb->lastFrame >= frame - 1)
        continue;

      if (victim == nullptr || thumb->lastUsed < victim->lastUsed)
        victim = thumb.get ( );
    }

    // In view, only for a hovered tile: the least recently hovered (never hovered first)
    if (victim == nullptr && hovered)
    {
      for (auto& thumb : m_animThumbs)
      {
        if (thumb.get ( ) == keep || thumb->state != anim_thumb_s::State::Loaded || thumb->hoveredFrame >= frame - 1)
          continue;

        if (victim == nullptr || thumb->lastHovered < victim->lastHovered)
          victim = thumb.get ( );
      }
    }

    if (victim == nullptr)
      return false;

    ReleaseAnimatedThumbnail (*victim);
  }

  return true;
}

void
SKIF_LibraryArtwork::RequestAnimatedThumbnail (app_record_s* pApp, bool hovered)
{
  // Only a thumbnail that is shown, of an animated cover; and only while animations are on
  if (pApp == nullptr || pApp->tex_thumb.texture.p == nullptr || pApp->tex_thumb.animatedSource.empty ( ) || ! AreCoverAnimationsOn ( ))
    return;

  // Off (Settings): it plays when hovered, as the cover panel does
  if (GetAnimatedThumbnailBudget ( ) == 0)
  {
    if (hovered)
      HoverAnimatedThumbnail (pApp);

    return;
  }

  const app_key_s game  = pApp->GetKey ( );
  anim_thumb_s*   thumb = FindAnimatedThumbnail (game);

  // Its cover is another file now
  if (thumb != nullptr && thumb->source != pApp->tex_thumb.animatedSource)
  {
    ResetAnimatedThumbnail (game);
    thumb = nullptr;
  }

  if (thumb == nullptr)
  {
    const std::wstring cachePath = GetThumbnailCachePath (pApp);

    // No asset folder: nowhere to cache it
    if (cachePath.empty ( ))
    {
      if (hovered)
        HoverAnimatedThumbnail (pApp);

      return;
    }

    auto created = std::make_unique <anim_thumb_s> ( );
    created->game      = game;
    created->source    = pApp->tex_thumb.animatedSource;
    created->cachePath = std::filesystem::path (cachePath).replace_extension (L".anim").wstring ( );
    created->cached    = _IsThumbnailCacheCurrent (created->cachePath);

    thumb = created.get ( );
    m_animThumbs.push_back (std::move (created));
  }

  const int   frame = ImGui::GetFrameCount   ( );
  const DWORD now   = SKIF_Util_timeGetTime ( );

  thumb->lastFrame = frame;
  thumb->lastUsed  = now;

  if (hovered)
  {
    thumb->hoveredFrame = frame;
    thumb->lastHovered  = now;

    // Until its frames are built (which takes a while), or if they cannot be used: as the cover panel does
    if (thumb->state == anim_thumb_s::State::Failed || (! thumb->cached && thumb->state != anim_thumb_s::State::Loaded))
      HoverAnimatedThumbnail (pApp);
  }
}

ID3D11ShaderResourceView*
SKIF_LibraryArtwork::GetAnimatedThumbnailFrame (app_key_s game, ImVec2* resolution, ImVec2* uv0, ImVec2* uv1) const
{
  const anim_thumb_s* thumb = FindAnimatedThumbnail (game);

  if (thumb == nullptr || thumb->state != anim_thumb_s::State::Loaded || thumb->duration == 0)
    return nullptr;

  // Never played: the thumbnail itself (the first frame, or an APNG's default image)
  if (thumb->playingSince == 0 && thumb->position == 0)
    return nullptr;

  const DWORD position = (thumb->playingSince != 0) ? (thumb->position + (SKIF_Util_timeGetTime ( ) - thumb->playingSince)) % thumb->duration
                                                    :  thumb->position;
  const auto  frameEnd = std::upper_bound (thumb->frameEnds.begin ( ), thumb->frameEnds.end ( ), position);
  const auto  index    = static_cast <size_t> (std::distance (thumb->frameEnds.begin ( ), frameEnd));

  if (index >= thumb->frames.size ( ))
    return nullptr;

  if (resolution != nullptr)
    *resolution = thumb->resolution;

  const ImVec4 uv = (index < thumb->frameUVs.size ( )) ? thumb->frameUVs [index] : ImVec4 (0.0f, 0.0f, 1.0f, 1.0f);

  if (uv0 != nullptr)
    *uv0 = ImVec2 (uv.x, uv.y);

  if (uv1 != nullptr)
    *uv1 = ImVec2 (uv.z, uv.w);

  return thumb->frames [index].p;
}

void
SKIF_LibraryArtwork::StartAnimatedThumbnailWorker (anim_thumb_s& thumb, bool build)
{
  struct thread_s {
    std::wstring        source;
    library_thumbnail_s thumb;
    app_key_s           game;
    int                 request    = 0;
    unsigned int        generation = 0;
    bool                build      = false;
    size_t              maxBytes   = 0;
  };

  thumb.request  = ++m_animThumbRequests;
  thumb.state    = anim_thumb_s::State::Working;
  thumb.building = build;

  thread_s* data   = new thread_s;
  data->source     = thumb.source;
  data->thumb      = { m_thumbSize.x, m_thumbSize.y, thumb.cachePath };
  data->game       = thumb.game;
  data->request    = thumb.request;
  data->generation = m_generation.load ( );
  data->build      = build;
  data->maxBytes   = GetAnimatedThumbnailBudget ( );

  if (build)
    m_animThumbBuilds++;
  else
    m_animThumbLoads++;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    thread_s* _data = static_cast <thread_s*> (var);

    SKIF_Util_SetThreadDescription (GetCurrentThread (), (_data->build) ? L"SKIF_LibAnimThumbnailBuilder"
                                                                        : L"SKIF_LibAnimThumbnailWorker");

    // A build decodes and compresses every frame: in the background
    if (_data->build)
    {
      SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // EcoQoS
      SetThreadPriority                  (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);
    }

    CoInitializeEx (nullptr, 0x0); // WIC: scaling the frames

    SKIF_LibraryArtwork& self = SKIF_LibraryArtwork::Get ( );

    worker_result_s result;
    result.type       = LibraryTexture::AnimatedThumbnail;
    result.game       = _data->game;
    result.queuePos   = _data->request;
    result.generation = _data->generation;
    result.build      = _data->build;

    // From the cache, else built (and saved to the cache). A load whose cache turns out unusable builds it too.
    result.ok = LoadAnimatedLibraryThumbnail (_data->source, _data->thumb, ! _data->build, _data->maxBytes, true,
                                              result.frames, result.delays, result.bytes, result.tooLarge);

    if (_data->build)
      SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    delete _data;

    // The main thread swaps them in if this is still the game's latest request (ApplyWorkerResults)
    self.m_results.push (std::move (result));

    // Force a frame, so the result is applied
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);

    return 0;
  }, data, 0x0, nullptr);

  if (hWorkerThread != NULL)
    CloseHandle (hWorkerThread);

  else
  {
    PLOG_ERROR << "Failed to start the animated thumbnail worker thread!";
    delete data;

    if (build)
      m_animThumbBuilds--;
    else
      m_animThumbLoads--;

    thumb.building = false;
    thumb.state    = anim_thumb_s::State::Failed;
  }
}

DWORD
SKIF_LibraryArtwork::UpdateAnimatedThumbnails (DWORD now)
{
  constexpr int maxBuilds = 1; // Decoding and compressing every frame: CPU heavy
  constexpr int maxLoads  = 2; // From the cache: quick

  const size_t budget = GetAnimatedThumbnailBudget ( );
  const int    frame  = ImGui::GetFrameCount ( );
  DWORD        wakeIn = 0;

  if (budget == 0)
    return 0;

  anim_thumb_s* hoveredThumb = nullptr;

  // Always (Settings): every one in view plays, not only the hovered one. Those that do not fit in the video memory stay
  //   still (built, but not loaded) until hovered: a tile in view only unloads others for itself while hovered.
  const bool always   = AreCoverAnimationsAlways ( );
  bool       gridShown = false;
  bool       notFit    = false;

  // Play while hovered (or in view); pause on the current frame once not
  for (auto& thumb : m_animThumbs)
  {
    const bool hovered = (thumb->hoveredFrame == frame);
    const bool inView  = (thumb->lastFrame    == frame);

    if (hovered)
      hoveredThumb = thumb.get ( );

    if (inView)
    {
      gridShown = true;

      if (always && ! hovered && thumb->state == anim_thumb_s::State::Cached)
        notFit = true;
    }

    if (thumb->state != anim_thumb_s::State::Loaded || thumb->duration == 0)
      continue;

    if (hovered || (always && inView))
    {
      if (thumb->playingSince == 0)
        thumb->playingSince = now;

      const DWORD position = (thumb->position + (now - thumb->playingSince)) % thumb->duration;
      const auto  frameEnd = std::upper_bound (thumb->frameEnds.begin ( ), thumb->frameEnds.end ( ), position);
      const DWORD due      = (frameEnd != thumb->frameEnds.end ( )) ? (*frameEnd - position) : 1;

      // The soonest frame of all those playing
      if (wakeIn == 0 || due < wakeIn)
        wakeIn = due;
    }

    else if (thumb->playingSince != 0)
    {
      thumb->position     = (thumb->position + (now - thumb->playingSince)) % thumb->duration;
      thumb->playingSince = 0;
    }
  }

  // Load (or build) those of tiles in view: the hovered one first. One already built loads when there is room for it,
  //   or when hovered (it then makes room); one not known yet is loaded to find out what it takes.
  auto _Start = [&](anim_thumb_s& thumb, bool hovered)
  {
    if (thumb.state == anim_thumb_s::State::Unknown)
    {
      if (! thumb.cached)
      {
        if (m_animThumbBuilds < maxBuilds)
          StartAnimatedThumbnailWorker (thumb, true);
      }

      else if (m_animThumbLoads < maxLoads)
        StartAnimatedThumbnailWorker (thumb, false);
    }

    else if (thumb.state == anim_thumb_s::State::Cached && m_animThumbLoads < maxLoads &&
             (hovered || m_animThumbBytes + thumb.bytes <= budget))
      StartAnimatedThumbnailWorker (thumb, false);
  };

  if (hoveredThumb != nullptr)
    _Start (*hoveredThumb, true);

  for (auto& thumb : m_animThumbs)
    if (thumb->lastFrame == frame && thumb.get ( ) != hoveredThumb)
      _Start (*thumb, false);

  // Known while the grid view is shown (Settings shows it)
  if (gridShown)
    m_animThumbsDidNotFit = (always && notFit);

  return wakeIn;
}

#pragma endregion

#pragma region Cover Caches

// A folder's path as compared: normalized, lowercase, backslashes, without a trailing one
static std::wstring
_NormalizeAssetFolder (const std::filesystem::path& folder)
{
  std::wstring path = SKIF_Util_ToLowerW (folder.lexically_normal ( ).wstring ( ));

  std::replace (path.begin ( ), path.end ( ), L'/', L'\\');

  while (! path.empty ( ) && path.back ( ) == L'\\')
    path.pop_back ( );

  return path;
}

// The asset folders: Special K's (<Special K>\Assets\ itself) and each game's (Assets\<store>\<game>\). Worker threads.
static std::vector <std::filesystem::path>
_GetAssetFolders (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::vector <std::filesystem::path> folders;
  std::error_code                     ec;

  const std::filesystem::path root = std::filesystem::path (_path_cache.specialk_userdata) / L"Assets";

  if (! std::filesystem::is_directory (root, ec))
    return folders;

  folders.push_back (root);

  for (auto& store : std::filesystem::directory_iterator (root, ec))
  {
    std::error_code ec2;

    if (! store.is_directory (ec2))
      continue;

    for (auto& game : std::filesystem::directory_iterator (store.path ( ), ec2))
    {
      std::error_code ec3;

      if (game.is_directory (ec3))
        folders.push_back (game.path ( ));
    }
  }

  return folders;
}

// Deletes the cover cache files of a folder (cover-thumb-*). Returns how many it deleted (one in use stays).
static int
_DeleteCoverCacheFiles (const std::filesystem::path& folder)
{
  int             deleted = 0;
  std::error_code ec;
  std::vector <std::filesystem::path> files;

  for (auto& entry : std::filesystem::directory_iterator (folder, ec))
  {
    std::error_code ec2;

    if (entry.is_regular_file (ec2) && SKIF_Util_ToLowerW (entry.path ( ).filename ( ).wstring ( )).rfind (L"cover-thumb", 0) == 0)
      files.push_back (entry.path ( ));
  }

  for (auto& file : files)
  {
    std::error_code ec2;

    if (std::filesystem::remove (file, ec2))
      deleted++;
  }

  return deleted;
}

void
SKIF_LibraryArtwork::CleanUpOrphanedCoverCaches (const std::vector <std::wstring>& assetDirs)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // One at a time (it runs once a session)
  if (m_coverCacheBusy.exchange (true))
    return;

  struct cleanup_s {
    std::vector <std::wstring>        keep;   // The asset folders of the games in the library
    std::unordered_set <std::wstring> stores; // The store folders (Assets\<store>\, lowercase) of the stores enabled in Settings
  };

  cleanup_s* cleanup = new cleanup_s ( );
  cleanup->keep.reserve (assetDirs.size ( ));

  for (auto& dir : assetDirs)
    if (! dir.empty ( ))
      cleanup->keep.push_back (dir); // Compared normalized (_NormalizeAssetFolder)

  // As named by each store's GetStoreAssetDirectory ( ). A disabled store's games are not in the library, but are not
  //   gone either, so its folders are left alone (as are folders of no store known here).
  const std::pair <bool, const wchar_t*> stores [] = {
    { _registry.bLibrarySteam,   L"steam"   },
    { _registry.bLibraryEA,      L"ea"      },
    { _registry.bLibraryEpic,    L"epic"    },
    { _registry.bLibraryitch,    L"itch"    },
    { _registry.bLibraryGOG,     L"gog"     },
    { _registry.bLibraryUbisoft, L"ubisoft" },
    { _registry.bLibraryXbox,    L"xbox"    },
    { _registry.bLibraryCustom,  L"custom"  },
  };

  for (auto& store : stores)
    if (store.first)
      cleanup->stores.insert (store.second);

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverCacheWorker");
    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // EcoQoS
    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    cleanup_s* _cleanup = static_cast <cleanup_s*> (var);

    std::unordered_set <std::wstring> keep;

    for (auto& dir : _cleanup->keep)
      keep.insert (_NormalizeAssetFolder (dir));

    const std::unordered_set <std::wstring> enabled = std::move (_cleanup->stores);

    delete _cleanup;

    // The games' folders (the first is Special K's own, the Assets folder itself: never cleaned up), of the enabled stores
    //   only. A store none of whose folders is a game in the library is skipped: it failed to load (or was enabled while
    //     the library was loading), or the paths are not comparable (spelled differently). Either way nothing there is
    //       known to be gone.
    const std::vector <std::filesystem::path>          folders = _GetAssetFolders ( );
    std::map <std::wstring, std::vector <size_t>>      orphaned; // Per store
    std::unordered_set <std::wstring>                  found;    // Stores with at least one game in the library

    for (size_t i = 1; i < folders.size ( ); i++)
    {
      const std::wstring store = SKIF_Util_ToLowerW (folders [i].parent_path ( ).filename ( ).wstring ( ));

      if (enabled.count (store) == 0)
        continue;

      if (keep.count (_NormalizeAssetFolder (folders [i])) == 0)
        orphaned [store].push_back (i);
      else
        found.insert (store);
    }

    int files   = 0,
        cleaned = 0;

    for (auto& [store, indexes] : orphaned)
    {
      if (found.count (store) == 0)
      {
        PLOG_WARNING << "No asset folder of a game in the library was found for " << store << "; its cover caches are not cleaned up.";
        continue;
      }

      for (size_t i : indexes)
      {
        if (const int deleted = _DeleteCoverCacheFiles (folders [i]); deleted > 0)
        {
          files += deleted;
          cleaned++;
        }
      }
    }

    PLOG_INFO << "Deleted " << files << " cover cache files in " << cleaned << " asset folders of games no longer in the library.";

    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    SKIF_LibraryArtwork::Get ( ).m_coverCacheBusy.store (false);

    return 0;
  }, cleanup, 0x0, nullptr);

  if (hWorkerThread != NULL)
    CloseHandle (hWorkerThread);

  else
  {
    PLOG_ERROR << "Failed to start the cover cache worker thread!";
    delete cleanup;
    m_coverCacheBusy.store (false);
  }
}

#pragma endregion

#include <utility/artwork.h>
#include <utility/games.h>
#include <utility/registry.h>
#include <utility/utility.h>
#include <stores/generic_library2.h>
#include <SKIF.h>
#include <concurrent_queue.h>
#include <process.h>
#include <algorithm>

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

    // Store-specific: fetch or download the store's cover (Steam, Epic, Xbox, PCGamingWiki)
    std::wstring load_str = _pApp->PrepareCoverSource (_queryPCGW);

    // The IGDB cover, when preferred for this store (Settings): downloaded once and kept as cover-igdb.jpg.
    //   Not needed when there is a PCGamingWiki cover (just prepared above), which takes precedence.
    if (_pApp->WantsIGDBCover ( ) && _pApp->FindCustomArtwork (LibraryTexture::Cover).empty ( ))
    {
      std::wstring assetDir  = _pApp->GetAssetDirectory ( );
      std::wstring igdbCover = assetDir + L"cover-igdb.jpg";

      if (! PathFileExistsW (igdbCover.c_str ( )) &&
          ! (_pApp->WantsPCGWCover ( ) && PathFileExistsW ((assetDir + L"cover-pcgw.png").c_str ( ))))
        _pApp->DownloadIGDBCover (igdbCover);
    }

    LoadLibraryTextureEx ( LibraryTexture::Cover,
                              _pApp->id,
                                load_str,
                                  _pApp,
                                    result.texture );

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
SKIF_LibraryArtwork::ForgetThread (LibraryTexture type, app_key_s game)
{
  for (auto it = m_threads.begin ( ); it != m_threads.end ( ); ++it)
  {
    if (it->type == type && it->game == game)
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
}

void
SKIF_LibraryArtwork::RefreshShownCover (void)
{
  OnCoverSaved (m_cover);
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
  }
}

#pragma endregion

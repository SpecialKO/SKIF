#include <utility/artwork.h>
#include <utility/games.h>
#include <utility/registry.h>
#include <utility/utility.h>
#include <stores/generic_library2.h>
#include <SKIF.h>
#include <concurrent_queue.h>
#include <process.h>

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
  if (m_coverAppId == pApp->id &&
      m_coverStore == pApp->store)
    return;

  // Special handling for the Special K logo when fading is disabled
  if (! _registry.bFadeCovers)
  {
    if (pApp->IsSpecialK ( ))
      m_fAlphaSK = 1.0f;
    else if (m_coverAppId == SKIF_STEAM_APPID &&
             m_coverStore == app_record_s::Store::Steam)
      m_fAlphaSK = 0.0f;
  }

  m_loadCover  = true;
  m_coverAppId = pApp->id;
  m_coverStore = pApp->store;

  // Hide the current cover and set it up to be unloaded
  if (m_pTexSRV.p != nullptr)
    RetireCover ( );
}

void
SKIF_LibraryArtwork::ForgetCover (void)
{
  m_coverAppId = 0;
  m_coverStore = app_record_s::Store::Unspecified;
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

  // Counted until the thread no longer uses the record (IsUsingRecords)
  m_coverWorkers++;

  // We're going to stream the cover in asynchronously on this thread
  //   The record is passed in, rather than read from the selection once the thread runs
  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverWorker");

    CoInitializeEx (nullptr, 0x0);

    PLOG_VERBOSE << "SKIF_LibCoverWorker thread started!";

    PLOG_DEBUG  << "Streaming game cover asynchronously...";

    app_record_s*        _pApp = static_cast <app_record_s*> (var);
    SKIF_LibraryArtwork& self  = SKIF_LibraryArtwork::Get ( );

    if (_pApp == nullptr)
    {
      PLOG_ERROR << "Aborting due to pApp being a nullptr!";
      self.m_coverWorkers--;
      return 0;
    }

    int queuePos = self.NextQueuePos ( );
    //PLOG_VERBOSE << "queuePos = " << queuePos;

    CComPtr <ID3D11ShaderResourceView> _pTexSRV (self.m_pTexSRV.p);
    std::wstring load_str;
    ImVec2 _resolution = ImVec2 (0, 0);

    // Store-specific: fetch or download the store's cover (Steam, Epic, Xbox, PCGamingWiki)
    load_str = _pApp->PrepareCoverSource ( );

    LoadLibraryTexture ( LibraryTexture::Cover,
                            _pApp->id,
                              _pTexSRV,
                                load_str,
                                  _resolution,
                                    _pApp);

    PLOG_VERBOSE << "_pTexSRV = " << _pTexSRV;

    int currentQueueLength = self.m_queueLength.load();

    if (currentQueueLength == queuePos)
    {
      PLOG_DEBUG << "Texture is live! Swapping it in.";
      self.m_vecCoverRes = _resolution;
      self.m_pTexSRV     = _pTexSRV;

      // Indicate that we have stopped loading the cover
      self.m_coverLoading.store (false);
    }

    else if (_pTexSRV.p != nullptr)
    {
      PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
      PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _pTexSRV.p << " to be released";;
      SKIF_ResourcesToFree.push(_pTexSRV.p);
      _pTexSRV.p = nullptr;
    }

    PLOG_DEBUG << "Finished streaming game cover asynchronously...";
    PLOG_VERBOSE << "SKIF_LibCoverWorker thread stopped!";

    // The record is no longer used from here on (a deferred library swap-in may now free it)
    self.m_coverWorkers--;

    // Force a refresh when the cover has been swapped in, and wake a library swap-in waiting on this worker
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_COVER, 0x0, 0x0);

    return 0;
  }, pApp, 0x0, nullptr);

  if (hWorkerThread != NULL)
    CloseHandle (hWorkerThread);

  else // Something went wrong during thread creation: nothing is loading
  {
    PLOG_ERROR << "Failed to start the cover worker thread!";
    m_coverWorkers--;
    m_coverLoading.store (false);
    m_tryingToLoadCover = false;
  }
}

// Records must outlive the workers using them: the library swap-in is deferred while this is true
bool
SKIF_LibraryArtwork::IsUsingRecords (void) const
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (m_coverWorkers.load ( ) > 0)
    return true;

  // Icon workers are reaped by the games list, but the thread itself may already be done
  for (auto& app : _games.GetApps ( ))
  {
    if (app.second->tex_icon.iWorker == 1 && WaitForSingleObject (app.second->tex_icon.hWorker, 0) != WAIT_OBJECT_0)
      return true;
  }

  return false;
}

bool
SKIF_LibraryArtwork::OnCoverWorkerFinished (void)
{
  m_tryingToLoadCover = m_coverLoading.load();

  return (! m_tryingToLoadCover);
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
  m_refreshAppId = pApp->id;
  m_refreshStore = (int)pApp->store;

  // This sets up the current one to be released
  RetireCover ( );

  // A child thread will post WM_SKIF_REFRESHCOVER once done
}

bool
SKIF_LibraryArtwork::IsSavingCover (const app_record_s* pApp) const
{
  return (pApp         != nullptr  &&
          m_savingCover            &&
          m_refreshAppId == pApp->id &&
          m_refreshStore == (int)pApp->store);
}

void
SKIF_LibraryArtwork::OnCoverSaved (uint32_t appid, int store)
{
  m_refreshPending = true;
  m_refreshAppId   = appid;
  m_refreshStore   = store;
}

void
SKIF_LibraryArtwork::RefreshShownCover (void)
{
  OnCoverSaved (m_coverAppId, (int)m_coverStore);
}

bool
SKIF_LibraryArtwork::ProcessCoverRefresh (void)
{
  if (! m_refreshPending)
    return false;

  m_refreshPending = false;

  bool reload = (m_coverAppId == m_refreshAppId &&
                 m_coverStore == (app_record_s::Store) m_refreshStore);

  // Needed as otherwise SKIF would not reload the cover
  if (reload)
    ForgetCover ( );

  m_savingCover  = false;
  m_refreshAppId = 0;
  m_refreshStore = 0;

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
    uint32_t                      appid;
    app_record_s::tex_registry_s* texture;
    app_record_s*                 app;
    std::wstring                  path;
  };

  thread_s* data = new thread_s;

  data->path    =  load_str;
  data->appid   =  pApp->id;
  data->texture = &pApp->tex_icon;
  data->app     =  pApp;

  // We're going to stream game icons asynchronously on this thread
  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibIconWorker");

    CoInitializeEx (nullptr, 0x0);

    thread_s* _data = static_cast<thread_s*>(var);

    ImVec2 dontCare;

    LoadLibraryTexture ( LibraryTexture::Icon,
                            _data->appid,
                              _data->texture->texture,
                                _data->path,
                                  dontCare,
                                    _data->app );

    delete _data;

    // Force a refresh when the game icons have finished being streamed
    PostMessage (SKIF_Notify_hWnd, WM_SKIF_ICON, 0x0, 0x0);

    return 0;
  }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated)
  {
    PLOG_VERBOSE << "An icon worker was spawned successfully!";
    pApp->tex_icon.hWorker = hWorkerThread;
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
SKIF_LibraryArtwork::ReapIconWorker (app_record_s* pApp)
{
  if (pApp->tex_icon.iWorker == 1 && WaitForSingleObject (pApp->tex_icon.hWorker, 0) == WAIT_OBJECT_0)
  {
    CloseHandle (pApp->tex_icon.hWorker);
    pApp->tex_icon.hWorker = NULL;
    pApp->tex_icon.iWorker = 2;
    m_activeIconWorkers--;
  }
}

void
SKIF_LibraryArtwork::ReloadIcon (app_record_s* pApp)
{
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
  }
}

#pragma endregion

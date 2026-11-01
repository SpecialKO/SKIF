#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <atlbase.h>
#include <d3d11.h>
#include <imgui/imgui.h>
#include <stores/Steam/app_record.h>
#include <stores/generic_library2.h>
#include <concurrent_queue.h>

// Library artwork: the selected game's cover and the per-game icon workers.
//
//   library.cpp decides what is selected and draws; this owns the cover textures (current + previous
//   for the cross-fade), the Special K logo fade, the background workers and their bookkeeping.
//   The covers themselves come from the record types (PrepareCoverSource / ResolveArtwork, step 4a).
//   Everything is main thread, except the worker threads it starts. Workers only produce results (texture,
//   resolution, flags), which ApplyWorkerResults ( ) applies on the main thread, finding the game by its key.
//
//   Cover flow: SelectCover (pApp) when the selection changes → IsCoverRequested ( ) → StartCoverWorker (pApp)
//   → ApplyWorkerResults ( ) (main thread) swaps it in. A custom cover being saved (SaveGameCover in library.cpp)
//   uses BeginSavingCover (pApp) → WM_SKIF_REFRESHCOVER → OnCoverSaved ( ) → ProcessCoverRefresh ( ).
// Thumbnails (the grid view): app_record_s::tex_thumb.iWorker
enum SKIF_ThumbnailState {
  SKIF_Thumbnail_None     = 0, // Not loaded
  SKIF_Thumbnail_Loading  = 1, // Loading from the thumbnail cache
  SKIF_Thumbnail_Done     = 2, // Loaded (no texture: the game has no cover)
  SKIF_Thumbnail_NoCache  = 3, // No usable cache: waits for a build (started once its tile is drawn)
  SKIF_Thumbnail_Building = 4  // Building: the cover pipeline (may download), scaled down and saved to the cache
};

class SKIF_LibraryArtwork
{
public:
  static SKIF_LibraryArtwork& Get (void)
  {
    static SKIF_LibraryArtwork instance;
    return instance;
  }

  // Cover selection
  void SelectCover            (app_record_s* pApp);  // Switches to this game's cover (no-op if it is already the one shown) and retires the current one
  void ForgetCover            (void);                // Forgets which game's cover is shown, so the next SelectCover ( ) loads it again
  void ReloadCover            (void) { m_loadCover = true; } // Loads the shown game's cover again (device reset)
  void ReleaseOldCoverIfFaded (void);                // Releases the previous cover once it has faded out (instantly when fading is disabled)
  bool IsCoverRequested       (void) const { return m_loadCover; }
  void StartCoverWorker       (app_record_s* pApp);  // Streams the requested cover in on a background thread (SKIF_LibCoverWorker)
  void ApplyWorkerResults     (void);                // Once per frame (SKIF_UI_Tab_DrawLibrary): swaps in the textures the workers finished

  // Workers keep the game record they were started for alive (SKIF_GamingCollection::FindShared), so a library
  //   refresh never waits for them. The reference comes back with the result and is released on the main thread.

  // Cover state, for drawing
  bool IsCoverLoading         (void) const { return m_tryingToLoadCover; }
  bool IsCoverMissing         (void) const;          // The latest cover load finished without a texture
  ID3D11ShaderResourceView*
       GetCoverTexture        (void) const { return m_pTexSRV.p;       }
  ID3D11ShaderResourceView*
       GetOldCoverTexture     (void) const { return m_pTexSRV_old.p;   }
  ImVec2 GetCoverResolution   (void) const { return m_vecCoverRes;     }
  ImVec2 GetOldCoverResolution(void) const { return m_vecCoverRes_old; }
  float  GetCoverAlpha        (void) const { return m_fAlpha;          }
  float  GetOldCoverAlpha     (void) const { return m_fAlphaPrev;      }
  float  GetSpecialKAlpha     (void) const { return m_fAlphaSK;        }
  void   ResetCoverAlpha      (void);                // Restarts the cover fade-in (fade setting changed)
  bool   StepFades            (bool isSpecialK, bool tick, bool& ticked); // Cover cross-fade and Special K logo, 0.05 per tick; true while any is active

  // Custom covers (SaveGameCover in library.cpp)
  void BeginSavingCover       (app_record_s* pApp);  // Shows the "..." loading label for this game and retires the current cover
  bool IsSavingCover          (const app_record_s* pApp) const;
  void OnCoverSaved           (app_key_s game);      // WM_SKIF_REFRESHCOVER
  void RefreshShownCover      (void);                // Reloads the shown cover (cover settings changed)
  bool ProcessCoverRefresh    (void);                // Once per frame; true if the shown cover must be reloaded (caller sets update)

  // Icons (one background worker per game, up to 8 at once). tex_icon.iWorker: 0 → 1 (StartIconWorker) → 2 once its
  //   result has been applied (ApplyWorkerResults), to whichever record the game has by then
  int  GetActiveIconWorkers   (void) const { return m_activeIconWorkers; }
  bool CanStartIconWorker     (void) const { return m_activeIconWorkers < 8; }
  void StartIconWorker        (app_record_s* pApp);  // tex_icon.iWorker 0 → 1 (SKIF_LibIconWorker)
  void ReloadIcon             (app_record_s* pApp);  // Loads the icon again on this thread (icon changed or reset); waits for its icon worker first

  // Thumbnails (the grid view). Loaded from the thumbnail cache (<asset folder>cover-thumb-<size>-<sources>.dds, BC7) by up to
  //   16 workers; a game without a usable cache is built by up to 16 workers: the same cover pipeline as the cover worker
  //   (which may download the cover), scaled down and saved to the cache. Only tiles that are drawn request one.
  void SetThumbnailSize       (ImVec2 size);         // The size thumbnails are made at (pixels); a change reloads them all
  ImVec2 GetThumbnailSize     (void) const { return m_thumbSize; }
  void RequestThumbnail       (app_record_s* pApp);  // Its tile is drawn: marks it as used, and starts a load or build when there is room
  void UpdateThumbnails       (bool gridShown);      // Once per frame: unloads thumbnails out of view for a while (all of them when the grid is not shown)
  bool OnThumbnailTimer       (void);                // WM_TIMER cIDT_TIMER_THUMBNAILS (SKIF.cpp): unloads those due, also while SKIF is asleep; true if any was
  int  UnloadThumbnailsOutOfView (bool now = false); // Unloads those out of view long enough (now: all of them, e.g. as SKIF goes idle), and
                                                     //   runs the timer while others wait; returns how many it unloaded
  void InvalidateThumbnail    (app_record_s* pApp);  // Its cover changed (custom cover saved, cover reset): deletes its cache and loads it again
  void ReleaseThumbnails      (void);                // Unloads all of them (cover sources changed in Settings, size changed)
  bool IsThumbnailWorkActive  (void) const { return (m_activeThumbLoads + m_activeThumbBuilds) > 0; }

  // Textures
  void ReleaseCurrentCover    (void);                // Pushes the current cover to SKIF_ResourcesToFree
  void ReleaseTextures        (void);                // Covers and all game icons (device reset); results still in flight are dropped, and the icons load again as they are drawn

private:
  SKIF_LibraryArtwork (void);
  SKIF_LibraryArtwork (SKIF_LibraryArtwork const&) = delete;
  SKIF_LibraryArtwork (SKIF_LibraryArtwork&&)      = delete;

  void RetireCover    (void);                        // Current cover → previous (released once faded out)
  int  NextQueuePos   (void) { return m_queueLength.fetch_add (1) + 1; }

  // Worker threads, by game, until their result has been applied (ApplyWorkerResults closes the handle)
  struct worker_thread_s {
    LibraryTexture          type       = LibraryTexture::Icon;
    app_key_s               game;
    HANDLE                  hThread    = NULL;
    int                     request    = 0;  // Thumbnails: the request it serves
  };
  std::vector <worker_thread_s>
                            m_threads;
  HANDLE FindThread   (LibraryTexture type, app_key_s game) const;                  // NULL if none
  void   ForgetThread (LibraryTexture type, app_key_s game, int request = 0);       // Closes the handle

  // Thumbnails
  void   StartThumbnailWorker (app_record_s* pApp, bool build);
  void   ReleaseThumbnail     (app_record_s* pApp);  // Pushes its texture for release; back to SKIF_Thumbnail_None
  std::wstring GetThumbnailCachePath (app_record_s* pApp) const; // Empty if the game has no asset folder
  ImVec2                    m_thumbSize         = ImVec2 (0, 0);
  int                       m_activeThumbLoads  = 0;
  int                       m_activeThumbBuilds = 0;
  int                       m_thumbRequests     = 0;  // The last request ID handed out
  int                       m_thumbsLoaded      = 0;  // Thumbnails with a texture
  std::atomic<int>          m_gridFrame         { 0 }; // The last frame the grid was drawn in (UpdateThumbnails); read by the workers
  bool                      m_thumbTimer        = false; // cIDT_TIMER_THUMBNAILS is running (every second, while some are out of view)

  // Cover textures (written by the cover worker when its result is still current)
  CComPtr <ID3D11ShaderResourceView> m_pTexSRV;
  CComPtr <ID3D11ShaderResourceView> m_pTexSRV_old;
  ImVec2                    m_vecCoverRes       = ImVec2 (0, 0);
  ImVec2                    m_vecCoverRes_old   = ImVec2 (0, 0);

  // Fades
  float                     m_fAlpha            = 0.0f;
  float                     m_fAlphaPrev        = 1.0f;
  float                     m_fAlphaSK          = 0.0f;

  // The game whose cover is shown, (Unspecified, 0) if none
  app_key_s                 m_cover;

  // Cover loading. Each load takes a queue position; only the latest one gets swapped in.
  bool                      m_loadCover         = false;
  bool                      m_tryingToLoadCover = false;
  std::atomic<bool>         m_coverLoading      = false;
  std::atomic<int>          m_queueLength       { 1 };
  int                       m_queuePosCover     = 0;

  // Custom cover being saved, and the refresh it triggers
  bool                      m_savingCover       = false;
  bool                      m_refreshPending    = false;
  app_key_s                 m_refresh;

  int                       m_activeIconWorkers = 0;

  // Finished worker results, applied on the main thread (ApplyWorkerResults)
  struct worker_result_s {
    LibraryTexture          type       = LibraryTexture::Cover;
    app_key_s               game;
    int                     queuePos   = 0;  // Covers: the load's queue position; only the latest load is shown. Thumbnails: the request
    bool                    build      = false; // Thumbnails: made by a build (else loaded from the cache)
    bool                    noCache    = false; // Thumbnails: the cache was missing or outdated (a build is needed)
    bool                    dropped    = false; // Thumbnails: its tile went out of view while it loaded, so it made no texture
    unsigned int            generation = 0;  // m_generation when the worker started
    library_texture_s       texture;
    std::shared_ptr <app_record_s>
                            record;          // The record the worker used, kept alive until here (released on the main thread)
  };
  concurrency::concurrent_queue <worker_result_s>
                            m_results;
  std::atomic<unsigned int> m_generation        { 0 }; // Bumped by ReleaseTextures (device reset): older results were made on the old device
};

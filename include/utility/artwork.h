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
//   An animated cover (APNG, GIF) shows its first frame; hovered (the cover panel, or its tile in the grid view),
//     UpdateAnimatedCover ( ) loads and plays the rest.
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
  void OnCoverSaved           (app_key_s game, bool notify = false, bool success = true); // WM_SKIF_REFRESHCOVER (notify: one was asked for)
  bool PopCoverSavedNotice    (app_key_s& game, bool& success); // A save to notify of (asked for, or failed): shown by the library tab,
                                                     //   during a frame (a toast needs its window; not from the window procedure)
  void RefreshShownCover      (void);                // Reloads the shown cover (cover settings changed)
  bool ProcessCoverRefresh    (void);                // Once per frame; true if the shown cover must be reloaded (caller sets update)

  // Animated covers (APNG, GIF): a cover shows its first frame, and plays while it is hovered: the shown cover (the cover
  //   panel), or a game's thumbnail (its tile in the grid view). One cover plays at a time: the one hovered. Its frames are
  //     loaded by a background worker (SKIF_LibAnimCoverWorker) as soon as it is hovered, are shared by the
  //       cover panel and the game's tile, and are released once another cover is hovered, on a device reset, when SKIF
  //         goes idle, when the game's cover changes, or once it has not been hovered for a while. A timer
  //           (cIDT_TIMER_COVERANIM) wakes SKIF for each new frame, so it does not draw more frames than that.
  bool AreCoverAnimationsOn   (void) const;          // Settings (iLibraryAnimatedCovers): hovered, always, never, or while Windows' "Animation effects" is on.
                                                     //   While off, no cover plays (and the frames loaded are released): they show still.
  void HoverAnimatedCover     (void);                // The cover panel is hovered this frame, or shown while they always play (plays
                                                     //   the shown cover, if animated)
  void HoverAnimatedThumbnail (app_record_s* pApp);  // A grid tile is hovered this frame (plays its thumbnail's cover, if animated)
  void UpdateAnimatedCover    (void);                // Once per frame of the library tab, after the cover / grid is drawn
  bool IsAnimatedCoverPlaying (void) const;          // An animated cover (the cover panel's or a grid tile's) played this frame:
                                                     //   SKIF keeps rendering frames while it does (SKIF.cpp)
  ID3D11ShaderResourceView*
       GetAnimatedCoverFrame  (app_key_s game, ImVec2* resolution = nullptr) const; // The frame to draw in place of the game's cover or
                                                     //   thumbnail while it plays (and its size), else nullptr
  ID3D11ShaderResourceView*
       GetAnimatedCoverFrame  (void) const { return GetAnimatedCoverFrame (m_cover); } // The shown cover's
  bool ReleaseAnimatedCover   (void);                // Pushes its frames for release (true if there were any); they load again once hovered
  void OnAnimatedCoverTimer   (void);                // WM_TIMER cIDT_TIMER_COVERANIM (SKIF.cpp): stops the timer (the frames re-arm it)

  // Animated covers in the grid view, kept loaded (Settings: iLibraryGridAnimMemory MB; 0: off, they play as above when
  //   hovered). Once a tile of an animated cover is shown, a background worker makes its frames tile-size (as thumbnails:
  //     3 mips, BC1 / BC3; SKIF_LibAnimThumbnailBuilder, one at a time), caches them next to the thumbnail (<asset folder>
  //       cover-thumb-<size>-<sources>.anim) and loads them (SKIF_LibAnimThumbnailWorker, two at a time). They stay loaded
  //         up to the limit: those of tiles out of view are unloaded first (the least recently shown), and a hovered tile
  //           may unload those of other tiles in view (the least recently hovered). A tile plays while hovered, and pauses
  //             on its current frame once the mouse moves on. Until its frames are built, or if they are too large for the
  //               limit, a hovered tile plays as above (HoverAnimatedThumbnail).
  void RequestAnimatedThumbnail (app_record_s* pApp, bool hovered); // Its tile is drawn this frame (with its thumbnail)
  ID3D11ShaderResourceView*
       GetAnimatedThumbnailFrame (app_key_s game, ImVec2* resolution) const; // Its frame to draw (playing, or paused), else nullptr
  void OnAnimatedThumbnailMemoryChanged (void);      // Settings changed the limit
  bool AreCoverAnimationsAutomatic (void) const;     // Settings (iLibraryAnimatedCovers 1): custom animation behavior across SKIF
  bool AreCoverAnimationsAlways    (void) const;     // Settings (iLibraryAnimatedCovers 3): every animated cover in view plays, not only
                                                     //   the hovered one (in the grid view: those that fit in its video memory)
  bool DidAnimatedThumbnailsNotFit (void) const { return m_animThumbsDidNotFit; } // While they always play: the last time the grid
                                                     //   view was shown, some in view did not fit in its video memory (they play when hovered)
  bool ReleaseAnimatedThumbnails (void);             // Unloads all of them (SKIF goes idle); true if any were. They load again once shown.

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

  // Cover caches: the thumbnails of the grid view and their animations (<asset folder>cover-thumb-*: .dds, .anim, and
  //   .tmp left behind), in the asset folders (Assets\<store>\<game>\). Those of games no longer in the library are
  //     deleted once a session, in the background (SKIF_LibCoverCacheWorker). Only the stores enabled in Settings are
  //       looked at, and only those with at least one game in the library (a disabled store's games, or those of a store
  //         that failed to load, are not in it, but are not gone). Custom covers, previous covers and downloaded covers
  //           are never touched, nor anything of Special K's own (the Assets folder itself) or of unknown folders.
  void CleanUpOrphanedCoverCaches (const std::vector <std::wstring>& assetDirs); // Once the library is loaded (assetDirs: the asset
                                                     //   folders of the games in it, in full)

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

  // Animated cover (UpdateAnimatedCover). Its frames are for m_animGame's cover m_animSource (the cover hovered last); a load
  //   serves m_animRequest (a newer request or a release makes it stale, so its result is dropped)
  enum class AnimatedCoverState {
    None,     // Not loaded
    Loading,  // Its worker is running
    Ready,    // Loaded: plays while hovered
    Failed    // Could not be loaded (too large, not decodable): stays a still cover
  };
  void ResetAnimatedCover       (void);                       // Releases the frames, and forgets which cover they were for
  void ResetAnimatedCover       (app_key_s game);             // ... if they are for this game (its cover changed)
  void StartAnimatedCoverWorker (void);
  void StopAnimatedCoverTimer   (void);
  std::wstring              m_coverAnimSource;           // The shown cover's file, if it is animated (from the cover worker)
  bool                      m_hoverSet          = false; // Hovered this frame (HoverAnimated*): what
  app_key_s                 m_hoverGame;
  std::wstring              m_hoverSource;
  app_key_s                 m_animGame;                  // The cover the frames are for (the one hovered last)
  std::wstring              m_animSource;
  AnimatedCoverState        m_animState         = AnimatedCoverState::None;
  std::atomic<int>          m_animRequest       { 0 };   // Read by the worker (whether it is still wanted)
  bool                      m_animWorkerBusy    = false; // One worker at a time
  std::vector <CComPtr <ID3D11ShaderResourceView>>
                            m_animFrames;
  std::vector <DWORD>       m_animFrameEnds;             // When each frame ends (ms into the animation)
  ImVec2                    m_animResolution    = ImVec2 (0, 0); // The frames' size (may be smaller than the cover)
  DWORD                     m_animDuration      = 0;     // ms
  DWORD                     m_animPlayingSince  = 0;     // 0: not playing (paused, or not started)
  DWORD                     m_animPosition      = 0;     // ms into the animation where it paused (it resumes from there)
  mutable DWORD             m_animsCheckedAt    = 0;     // AreCoverAnimationsOn: when Windows' setting was last read (once a second) ...
  mutable bool              m_animsWindows      = true;  //   ... and what it said
  DWORD                     m_animLastHovered   = 0;
  DWORD                     m_animTimerDue      = 0;     // When the timer fires, while it runs
  bool                      m_animTimer         = false; // cIDT_TIMER_COVERANIM is running
  int                       m_animPlayingFrame  = -1;    // The last frame (ImGui::GetFrameCount) an animated cover played in

  // Animated covers in the grid view (RequestAnimatedThumbnail), by game. Owned here, so the pointers stay valid.
  struct anim_thumb_s {
    enum class State {
      Unknown,  // Not loaded; its cache is to be checked or built
      Working,  // Its worker is running (building: a build)
      Loaded,   // Its frames are loaded
      Cached,   // Built, but not loaded (no room)
      Failed    // Cannot be used (too large for the limit, not decodable): plays as the cover panel does
    };
    app_key_s               game;
    std::wstring            source;              // The animated cover (tex_thumb.animatedSource)
    std::wstring            cachePath;           // <thumbnail cache path>.anim
    State                   state        = State::Unknown;
    bool                    cached       = false; // Its cache is current
    bool                    building     = false;
    int                     request      = 0;     // Its worker's (results of an older one are dropped)
    std::vector <CComPtr <ID3D11ShaderResourceView>>
                            frames;
    std::vector <DWORD>     frameEnds;           // When each frame ends (ms into the animation)
    DWORD                   duration     = 0;     // ms
    ImVec2                  resolution   = ImVec2 (0, 0);
    size_t                  bytes        = 0;     // What its frames take (known once loaded or built)
    DWORD                   position     = 0;     // ms into the animation where it paused
    DWORD                   playingSince = 0;     // 0: paused
    DWORD                   lastUsed     = 0;     // When its tile was last drawn
    DWORD                   lastHovered  = 0;     // When its tile was last hovered (0: never)
    int                     lastFrame    = -1;    // The frame its tile was last drawn in
    int                     hoveredFrame = -1;    // The frame its tile was last hovered in
  };
  std::vector <std::unique_ptr <anim_thumb_s>>
                            m_animThumbs;
  size_t                    m_animThumbBytes    = 0;     // What the loaded ones take
  int                       m_animThumbRequests = 0;
  int                       m_animThumbBuilds   = 0;     // Workers running
  int                       m_animThumbLoads    = 0;
  bool                      m_animThumbsDidNotFit = false; // See DidAnimatedThumbnailsNotFit ( )
  size_t        GetAnimatedThumbnailBudget   (void) const;                  // Bytes (Settings)
  anim_thumb_s* FindAnimatedThumbnail        (app_key_s game) const;        // nullptr if none
  void          ReleaseAnimatedThumbnail     (anim_thumb_s& thumb);         // Unloads its frames (Loaded → Cached)
  void          ResetAnimatedThumbnail       (app_key_s game);              // Forgets it (its cover changed)
  void          ResetAnimatedThumbnails      (void);                        // Forgets all of them (device reset, thumbnails reloaded)
  bool          MakeAnimatedThumbnailRoom    (size_t bytes, const anim_thumb_s* keep, bool hovered); // Unloads others until bytes fit
  void          StartAnimatedThumbnailWorker (anim_thumb_s& thumb, bool build);
  DWORD         UpdateAnimatedThumbnails     (DWORD now);                   // UpdateAnimatedCover: play / pause, start workers; ms until a frame is due (0: none)

  // Cover caches (CleanUpOrphanedCoverCaches): its worker is running
  std::atomic<bool>         m_coverCacheBusy    { false };

  // Saves to notify of (OnCoverSaved → PopCoverSavedNotice): the game, and whether it succeeded
  std::vector <std::pair <app_key_s, bool>>
                            m_coverSavedNotices;

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
    std::vector <library_texture_s>
                            frames;          // Animated covers: the frames (queuePos: the request)
    std::vector <DWORD>     delays;          // Animated covers: how long each frame is shown (ms)
    size_t                  bytes      = 0;     // Animated thumbnails: what the frames take
    bool                    tooLarge   = false; // Animated thumbnails: too large for the limit
    bool                    ok         = false; // Animated thumbnails: loaded (or built)
    std::shared_ptr <app_record_s>
                            record;          // The record the worker used, kept alive until here (released on the main thread)
  };
  concurrency::concurrent_queue <worker_result_s>
                            m_results;
  std::atomic<unsigned int> m_generation        { 0 }; // Bumped by ReleaseTextures (device reset): older results were made on the old device
};

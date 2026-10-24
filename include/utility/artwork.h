#pragma once
#include <atomic>
#include <cstdint>
#include <atlbase.h>
#include <d3d11.h>
#include <imgui/imgui.h>
#include <stores/Steam/app_record.h>

// Library artwork: the selected game's cover and the per-game icon workers.
//
//   library.cpp decides what is selected and draws; this owns the cover textures (current + previous
//   for the cross-fade), the Special K logo fade, the background workers and their bookkeeping.
//   The covers themselves come from the record types (PrepareCoverSource / ResolveArtwork, step 4a).
//   Everything is main thread, except the worker threads it starts.
//
//   Cover flow: SelectCover (pApp) when the selection changes → IsCoverRequested ( ) → StartCoverWorker (pApp)
//   → WM_SKIF_COVER → OnCoverWorkerFinished ( ). A custom cover being saved (SaveGameCover in library.cpp)
//   uses BeginSavingCover (pApp) → WM_SKIF_REFRESHCOVER → OnCoverSaved ( ) → ProcessCoverRefresh ( ).
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
  bool OnCoverWorkerFinished  (void);                // WM_SKIF_COVER; returns true once no cover is loading any longer

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
  void OnCoverSaved           (uint32_t appid, int store); // WM_SKIF_REFRESHCOVER
  void RefreshShownCover      (void);                // Reloads the shown cover (cover settings changed)
  bool ProcessCoverRefresh    (void);                // Once per frame; true if the shown cover must be reloaded (caller sets update)

  // Icons (one background worker per game, up to 8 at once)
  int  GetActiveIconWorkers   (void) const { return m_activeIconWorkers; }
  bool CanStartIconWorker     (void) const { return m_activeIconWorkers < 8; }
  void StartIconWorker        (app_record_s* pApp);  // tex_icon.iWorker 0 → 1 (SKIF_LibIconWorker)
  void ReapIconWorker         (app_record_s* pApp);  // tex_icon.iWorker 1 → 2 once the worker has finished
  void OnIconWorkersReaped    (int count) { m_activeIconWorkers -= count; } // Workers reaped by SKIF_GamingCollection::SwapInPopulatedGames ( )
  void ReloadIcon             (app_record_s* pApp);  // Loads the icon again on this thread (icon changed or reset)

  // Textures
  void ReleaseCurrentCover    (void);                // Pushes the current cover to SKIF_ResourcesToFree
  void ReleaseTextures        (void);                // Covers and all game icons (device reset)

private:
  SKIF_LibraryArtwork (void);
  SKIF_LibraryArtwork (SKIF_LibraryArtwork const&) = delete;
  SKIF_LibraryArtwork (SKIF_LibraryArtwork&&)      = delete;

  void RetireCover    (void);                        // Current cover → previous (released once faded out)
  int  NextQueuePos   (void) { return m_queueLength.fetch_add (1) + 1; }

  // Cover textures (written by the cover worker when its result is still current)
  CComPtr <ID3D11ShaderResourceView> m_pTexSRV;
  CComPtr <ID3D11ShaderResourceView> m_pTexSRV_old;
  ImVec2                    m_vecCoverRes       = ImVec2 (0, 0);
  ImVec2                    m_vecCoverRes_old   = ImVec2 (0, 0);

  // Fades
  float                     m_fAlpha            = 0.0f;
  float                     m_fAlphaPrev        = 1.0f;
  float                     m_fAlphaSK          = 0.0f;

  // The game whose cover is shown, (0, Unspecified) if none
  uint32_t                  m_coverAppId        = 0;
  app_record_s::Store       m_coverStore        = app_record_s::Store::Unspecified;

  // Cover loading. Each load takes a queue position; only the latest one gets swapped in.
  bool                      m_loadCover         = false;
  bool                      m_tryingToLoadCover = false;
  std::atomic<bool>         m_coverLoading      = false;
  std::atomic<int>          m_queueLength       { 1 };
  int                       m_queuePosCover     = 0;

  // Custom cover being saved, and the refresh it triggers
  bool                      m_savingCover       = false;
  bool                      m_refreshPending    = false;
  uint32_t                  m_refreshAppId      = 0;
  int                       m_refreshStore      = 0;

  int                       m_activeIconWorkers = 0;
};

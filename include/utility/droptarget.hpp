#pragma once

#include <windows.h>
#include <shlobj.h>
#include <objidl.h>
#include <shlwapi.h>
#include <vector>
#include <filesystem>
#include <utility/sk_utility.h>

class DropTarget : public IDropTarget {
public:
  DropTarget (HWND hWnd)
  {
    m_hWnd              = hWnd;
    m_ulRefCount        = 1;
    
    // CLSCTX_INPROC_SERVER
    if (FAILED (CoCreateInstance (CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                                   IID_IDropTargetHelper, reinterpret_cast<LPVOID *>(&m_pDropTargetHelper))))
      m_pDropTargetHelper = nullptr;

    // Images (files) from e.g. File Explorer
    m_fmtSupported.push_back ({ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL });

    // ANSI text, e.g. URL to images from a web browser
    m_fmtSupported.push_back ({ CF_TEXT,  nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL });

    // NOT CURRENTLY IMPLEMENTED
    // Unicode text, e.g. URL to images from a web browser
    //formatEtc_.push_back ({ CF_UNICODETEXT,  nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL });
  }

  ~DropTarget ()
  {
    if (m_pDropTargetHelper != nullptr)
      m_pDropTargetHelper->Release();
  }

  // IUnknown methods
  STDMETHODIMP QueryInterface (REFIID riid, void** ppvObject) override
  {
    if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, IID_IDropTarget))
    {
      *ppvObject = this;
      AddRef();

      return S_OK;
    }

    *ppvObject = nullptr;

    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef (void) override
  {
    return InterlockedIncrement (&m_ulRefCount);
  }

  STDMETHODIMP_(ULONG) Release (void) override
  {
    ULONG newRefCount = InterlockedDecrement (&m_ulRefCount);

    if (newRefCount == 0)
    {
      delete this;
    }

    return newRefCount;
  }

  // IDropTarget methods
  STDMETHODIMP DragEnter (IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
  {
    UNREFERENCED_PARAMETER (grfKeyState);

    // Reset stuff
    m_bAllowDrop  = false;
    m_fmtDropping = nullptr;

    if (pdwEffect == NULL)
      return E_INVALIDARG;

    if (pDataObj == nullptr)
      return E_UNEXPECTED;

    // We are only interested in copy operations (for now)
    if ((*pdwEffect & DROPEFFECT_COPY) == DROPEFFECT_COPY)
    {
      for (auto& fmt : m_fmtSupported)
      {
        if (SUCCEEDED (pDataObj->QueryGetData (&fmt)))
        {
          m_bAllowDrop  = true;
          m_fmtDropping = &fmt;
          *pdwEffect    = DROPEFFECT_COPY;

          if (m_pDropTargetHelper != nullptr)
            m_pDropTargetHelper->DragEnter (m_hWnd, pDataObj, reinterpret_cast<LPPOINT>(&pt), *pdwEffect);

          return S_OK;
        }
      }
    }

    *pdwEffect = DROPEFFECT_NONE;
    return S_FALSE;
  }

  STDMETHODIMP DragOver (DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
  {
    UNREFERENCED_PARAMETER (grfKeyState);

    if (pdwEffect == NULL)
      return E_INVALIDARG;

    // Here we could theoretically check if an ImGui component that supports the text is being hovered
    if (m_bAllowDrop && (*pdwEffect & DROPEFFECT_COPY) == DROPEFFECT_COPY)
    {
      *pdwEffect = DROPEFFECT_COPY;

      if (m_pDropTargetHelper != nullptr)
        m_pDropTargetHelper->DragOver (reinterpret_cast<LPPOINT>(&pt), *pdwEffect);

      return S_OK;
    }

    *pdwEffect = DROPEFFECT_NONE;
    return S_FALSE;
  }

  STDMETHODIMP DragLeave (void) override
  {
    if (m_pDropTargetHelper != nullptr)
      m_pDropTargetHelper->DragLeave ( );

    m_bAllowDrop  = false;
    m_fmtDropping = nullptr;

    return S_OK;
  }

  STDMETHODIMP Drop (IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
  {
    UNREFERENCED_PARAMETER (grfKeyState);

    if (pdwEffect == NULL)
      return E_INVALIDARG;

    if (pDataObj == nullptr || m_fmtDropping == nullptr)
      return E_UNEXPECTED;

    if ((*pdwEffect & DROPEFFECT_COPY) == DROPEFFECT_COPY)
    {
      *pdwEffect = DROPEFFECT_COPY;

      if (m_pDropTargetHelper != nullptr)
        m_pDropTargetHelper->Drop (pDataObj, reinterpret_cast<LPPOINT>(&pt), *pdwEffect);

      extern std::wstring dragDroppedFilePath;
      STGMEDIUM medium = { };

      // Files from e.g. File Explorer
      if (m_fmtDropping->cfFormat == CF_HDROP && SUCCEEDED (pDataObj->GetData (m_fmtDropping, &medium)))
      {
        HDROP hDrop = static_cast<HDROP> (GlobalLock(medium.hGlobal));

        if (hDrop != nullptr)
        {
          UINT numFiles = DragQueryFile (hDrop, 0xFFFFFFFF, nullptr, 0);

          if (numFiles > 0)
          {
            TCHAR filePath [MAX_PATH];
            DragQueryFile (hDrop, 0, filePath, MAX_PATH);
            dragDroppedFilePath = std::wstring(filePath);
          }

          GlobalUnlock (medium.hGlobal);
        }

        ReleaseStgMedium (&medium);

        m_bAllowDrop  = false;
        m_fmtDropping = nullptr;

        return S_OK;
      }

      // ANSI text
      else if (m_fmtDropping->cfFormat == CF_TEXT && SUCCEEDED (pDataObj->GetData (m_fmtDropping, &medium)))
      {
        char *pszData = static_cast<char *> (GlobalLock (medium.hGlobal));

        if (pszData != nullptr)
        {
          dragDroppedFilePath = SK_UTF8ToWideChar (pszData);

          GlobalUnlock (medium.hGlobal);
        }

        ReleaseStgMedium (&medium);

        m_bAllowDrop  = false;
        m_fmtDropping = nullptr;

        return S_OK;
      }
    }

    *pdwEffect = DROPEFFECT_NONE;
    return S_FALSE;
  }

private:
  HWND                   m_hWnd              =    NULL; // Required by m_pDropTargetHelper->DragEnter
  ULONG                  m_ulRefCount        =       0;
  bool                   m_bAllowDrop        =   false;
  IDropTargetHelper*     m_pDropTargetHelper = nullptr; // Drag image/thumbnail helper
  FORMATETC*             m_fmtDropping       = nullptr;
  std::vector<FORMATETC> m_fmtSupported;
};

// Singleton wrapper of the above
struct SKIF_DropTargetObject
{
  static SKIF_DropTargetObject& GetInstance (void)
  {
      static SKIF_DropTargetObject instance;
      return instance;
  }

  void Revoke (HWND hWnd)
  {
    if (m_hWnd == NULL)
      return;

    if (m_hWnd != hWnd)
      return;

    RevokeDragDrop (m_hWnd);
    delete m_DropTarget;

    m_DropTarget = nullptr;
    m_hWnd       = NULL;
  }

  bool Register (HWND hWnd)
  {
    if (hWnd == NULL)
      return false;

    if (m_DropTarget != nullptr && m_hWnd != hWnd)
      Revoke (m_hWnd);

    if (m_DropTarget == nullptr)
    {
      m_hWnd       = hWnd;
      m_DropTarget = new DropTarget (m_hWnd);
            
      if (FAILED (RegisterDragDrop (m_hWnd, m_DropTarget)))
        Revoke (m_hWnd);
    }

    return (m_DropTarget != nullptr);
  }

  SKIF_DropTargetObject (SKIF_DropTargetObject const&) = delete; // Delete copy constructor
  SKIF_DropTargetObject (SKIF_DropTargetObject&&)      = delete; // Delete move constructor

private:
  SKIF_DropTargetObject (void) { };
  DropTarget* m_DropTarget = nullptr;
  HWND        m_hWnd       = NULL;
};
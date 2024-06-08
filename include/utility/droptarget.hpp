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

    // Initialize our supported clipboard formats
    m_fmtSupported = {
      { CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }, // Unicode text, e.g. URL to images from a web browser
      { CF_HDROP,       nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }, // Images (files) from e.g. File Explorer or a local Bitmap image from Firefox
      { CF_TEXT,        nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }  //   ANSI  text, e.g. URL to images from a web browser
    };
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
      IEnumFORMATETC* pEnumFormatEtc = nullptr;
      if (SUCCEEDED (pDataObj->EnumFormatEtc (DATADIR_GET, &pEnumFormatEtc)))
      {
        FORMATETC s_fmtSupported = { }; // FormatEtc supported by the source
        ULONG fetched;

        // We need to find a matching format that both we and the source supports
        while (pEnumFormatEtc->Next (1, &s_fmtSupported, &fetched) == S_OK)
        {

#ifdef _DEBUG
          TCHAR szFormatName[256];
          GetClipboardFormatName (s_fmtSupported.cfFormat, szFormatName, 256);
          PLOG_VERBOSE << "Supported format: " << s_fmtSupported.cfFormat << " - " << szFormatName;
#endif

          for (auto& fmt : m_fmtSupported)
          {
            if (fmt.cfFormat == s_fmtSupported.cfFormat && // Are we dealing with the same format type?
                SUCCEEDED (pDataObj->QueryGetData (&fmt))) // Does it accept our format specification?
            {
              m_bAllowDrop  = true;
              m_fmtDropping = &fmt;
              *pdwEffect    = DROPEFFECT_COPY;

              if (m_pDropTargetHelper != nullptr)
                m_pDropTargetHelper->DragEnter (m_hWnd, pDataObj, reinterpret_cast<LPPOINT>(&pt), *pdwEffect);
              
              pEnumFormatEtc->Release();
              return S_OK;
            }
          }
        }
        pEnumFormatEtc->Release();
      }

      else
        PLOG_VERBOSE << "Failed to enumerate formats!";
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
      STGMEDIUM medium;

      auto ReturnAndCleanUp = [&](void)
      {
        ReleaseStgMedium (&medium);

        m_bAllowDrop  = false;
        m_fmtDropping = nullptr;

        return S_OK;
      };

      // Unicode text to e.g. URL to images from a Chromium web browser
      if (m_fmtDropping->cfFormat == CF_UNICODETEXT && SUCCEEDED (pDataObj->GetData (m_fmtDropping, &medium)))
      {
        PLOG_VERBOSE << "Detected a drop of type CF_UNICODETEXT";

        const wchar_t* pszSource = static_cast<const wchar_t*> (GlobalLock (medium.hGlobal));

        if (pszSource != nullptr)
        {
          dragDroppedFilePath = std::wstring (pszSource);

          GlobalUnlock (medium.hGlobal);
        }

        ReturnAndCleanUp ( );
      }

      // Files from e.g. File Explorer or a local Bitmap image from Firefox
      else if (m_fmtDropping->cfFormat == CF_HDROP && SUCCEEDED (pDataObj->GetData (m_fmtDropping, &medium)))
      {
        PLOG_VERBOSE << "Detected a drop of type CF_HDROP";

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

        ReturnAndCleanUp ( );
      }

      // ANSI text to e.g. URL to images from a Chromium web browser
      else if (m_fmtDropping->cfFormat == CF_TEXT && SUCCEEDED (pDataObj->GetData (m_fmtDropping, &medium)))
      {
        PLOG_VERBOSE << "Detected a drop of type CF_TEXT";

        const char* pszSource = static_cast<const char *> (GlobalLock (medium.hGlobal));

        if (pszSource != nullptr)
        {
          dragDroppedFilePath = SK_UTF8ToWideChar (pszSource);

          GlobalUnlock (medium.hGlobal);
        }

        ReturnAndCleanUp ( );
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
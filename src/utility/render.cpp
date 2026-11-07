#include <utility/render.h>

HRESULT
SKIF_DXGI_CreateDXGIFactory1 (REFIID riid, void **ppFactory)
{
  using CreateDXGIFactory1_pfn =
           HRESULT (WINAPI *)(REFIID, void **);

  static CreateDXGIFactory1_pfn
         CreateDXGIFactory1 =
        (CreateDXGIFactory1_pfn)GetProcAddress (LoadLibraryEx (L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "CreateDXGIFactory1");

  if (CreateDXGIFactory1 == nullptr)
    return E_FAIL;

  return CreateDXGIFactory1 (riid, ppFactory);
}

HRESULT
SKIF_DXGI_D3D11CreateDevice (
  IDXGIAdapter            *pAdapter,
  D3D_DRIVER_TYPE          DriverType,
  HMODULE                  Software,
  UINT                     Flags,
  const D3D_FEATURE_LEVEL *pFeatureLevels,
  UINT                     FeatureLevels,
  UINT                     SDKVersion,
  ID3D11Device           **ppDevice,
  D3D_FEATURE_LEVEL       *pFeatureLevel,
  ID3D11DeviceContext    **ppImmediateContext )
{
  using D3D11CreateDevice_pfn =
           HRESULT (WINAPI *)( IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                         const D3D_FEATURE_LEVEL *, UINT, UINT,
                               ID3D11Device **, D3D_FEATURE_LEVEL *,
                               ID3D11DeviceContext ** );

  static D3D11CreateDevice_pfn
         D3D11CreateDevice =
        (D3D11CreateDevice_pfn)GetProcAddress (LoadLibraryEx (L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "D3D11CreateDevice");

  if (D3D11CreateDevice == nullptr)
    return E_FAIL;

  return D3D11CreateDevice ( pAdapter, DriverType, Software, Flags,
                             pFeatureLevels, FeatureLevels, SDKVersion,
                             ppDevice, pFeatureLevel, ppImmediateContext );
}
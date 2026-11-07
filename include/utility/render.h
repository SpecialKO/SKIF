#pragma once

#include <dxgi.h>
#include <d3d11.h>

// Direct3D 11 / DirectX 11 stuff

HRESULT      SKIF_DXGI_CreateDXGIFactory1 (REFIID riid, void **ppFactory);
HRESULT      SKIF_DXGI_D3D11CreateDevice  (IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);

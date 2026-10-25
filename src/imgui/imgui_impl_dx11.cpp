// dear imgui: Renderer Backend for DirectX11
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as texture identifier. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access with ImGui_ImplDX11_GetRenderState().
//  [X] Renderer: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
//  2026-04-23: DirectX11: Added support for standard draw callbacks (in platform_io): DrawCallback_ResetRenderState, DrawCallback_SetSamplerLinear, DrawCallback_SetSamplerNearest. Obsoleting samplers from ImGui_ImplDX11_RenderState. (#9378)
//  2026-01-19: DirectX11: Added 'SamplerNearest' in ImGui_ImplDX11_RenderState. Renamed 'SamplerDefault' to 'SamplerLinear'.
//  2025-09-18: Call platform_io.ClearRendererHandlers() on shutdown.
//  2025-06-11: DirectX11: Added support for ImGuiBackendFlags_RendererHasTextures, for dynamic font atlas.
//  2025-05-07: DirectX11: Honor draw_data->FramebufferScale to allow for custom backends and experiment using it (consistently with other renderer backends, even though in normal condition it is not set under Windows).
//  2025-02-24: [Docking] Added undocumented ImGui_ImplDX11_SetSwapChainDescs() to configure swap chain creation for secondary viewports.
//  2025-01-06: DirectX11: Expose VertexConstantBuffer in ImGui_ImplDX11_RenderState. Reset projection matrix in ImDrawCallback_ResetRenderState handler.
//  2024-10-07: DirectX11: Changed default texture sampler to Clamp instead of Repeat/Wrap.
//  2024-10-07: DirectX11: Expose selected render state in ImGui_ImplDX11_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: DirectX11: Replaced direct access to ImDrawCmd::TextureId with a call to ImDrawCmd::GetTexID(). (will become a requirement)
//  2021-02-18: DirectX11: Change blending equation to preserve alpha in output buffer.
//  2019-08-01: DirectX11: Fixed code querying the Geometry Shader state (would generally error with Debug layer enabled).
//  2019-07-21: DirectX11: Backup, clear and restore Geometry Shader is any is bound when calling ImGui_ImplDX11_RenderDrawData. Clearing Hull/Domain/Compute shaders without backup/restore.
//  2019-05-29: DirectX11: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: DirectX11: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2018-12-03: Misc: Added #pragma comment statement to automatically link with d3dcompiler.lib when using D3DCompile().
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-08-01: DirectX11: Querying for IDXGIFactory instead of IDXGIFactory1 to increase compatibility.
//  2018-07-13: DirectX11: Fixed unreleased resources in Init and Shutdown functions.
//  2018-06-08: Misc: Extracted imgui_impl_dx11.cpp/.h away from the old combined DX11+Win32 example.
//  2018-06-08: DirectX11: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplDX11_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2016-05-07: DirectX11: Disabling depth-write.

#include "imgui/imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui/imgui_impl_dx11.h"

// DirectX
#include <stdio.h>
#include <dxgi1_6.h> // SKIF CUSTOM
#include <d3d11.h>
#include <d3dcompiler.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#endif

#define SKIF_D3D11

#ifdef SKIF_D3D11

#include "imgui/imgui_internal.h"
#include <format>
#include <atlbase.h>
#include <utility/registry.h>
#include <plog/Log.h>

#include <shaders/imgui_pix.h>
#include <shaders/imgui_vtx.h>

// External declarations
extern DWORD   SKIF_Util_timeGetTime1                (void);
extern bool    SKIF_Util_IsWindows8Point1OrGreater   (void);
extern bool    SKIF_Util_IsWindows10OrGreater        (void);
extern bool    SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
extern bool    SKIF_Util_IsHDRSupported              (HMONITOR hMonitor);
extern bool    SKIF_Util_IsHDRActive                 (HMONITOR hMonitor);
extern float   SKIF_Util_GetSDRWhiteLevel            (HMONITOR hMonitor);
extern bool  RecreateD3DDevice;
extern bool  RecreateSwapChains;
extern bool  RecreateSwapChainsPending;

struct ImGui_ImplDX11_ViewportData;

#endif // SKIF_D3D11

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wold-style-cast"         // warning: use of old-style cast                            // yes, they are more terse.
#pragma clang diagnostic ignored "-Wsign-conversion"        // warning: implicit conversion changes signedness
#endif

// DirectX11 data
struct ImGui_ImplDX11_Texture
{
    ID3D11Texture2D*            pTexture;
    ID3D11ShaderResourceView*   pTextureView;
};

struct ImGui_ImplDX11_Data
{
    ID3D11Device*               pd3dDevice;
    ID3D11DeviceContext*        pd3dDeviceContext;
#ifndef SKIF_D3D11
    IDXGIFactory*               pFactory;
#else
    IDXGIFactory2*              pFactory;
#endif
    ID3D11Buffer*               pVB;
    ID3D11Buffer*               pIB;
    ID3D11VertexShader*         pVertexShader;
    ID3D11InputLayout*          pInputLayout;
    ID3D11Buffer*               pVertexConstantBuffer;
#ifdef SKIF_D3D11
    ID3D11Buffer*               pPixelConstantBuffer;
    ID3D11Buffer*               pFontConstantBuffer;
#endif
    ID3D11PixelShader*          pPixelShader;
    ID3D11SamplerState*         pTexSamplerLinear;
    ID3D11SamplerState*         pTexSamplerNearest;
    ID3D11RasterizerState*      pRasterizerState;
    ID3D11BlendState*           pBlendState;
    ID3D11DepthStencilState*    pDepthStencilState;
    int                         VertexBufferSize;
    int                         IndexBufferSize;
    ImGui_ImplDX11_RenderState* RenderState;            // == (ImGui_ImplDX11_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState during rendering.
    ImVector<DXGI_SWAP_CHAIN_DESC> SwapChainDescsForViewports;

    ImGui_ImplDX11_Data()       { memset((void*)this, 0, sizeof(*this)); VertexBufferSize = 5000; IndexBufferSize = 10000; }
};

struct VERTEX_CONSTANT_BUFFER_DX11 {
  float mvp [4][4];

#ifdef SKIF_D3D11
  // scRGB allows values > 1.0, sRGB (SDR) simply clamps them
  // x = Luminance/Brightness -- For HDR displays, 1.0 = 80 Nits, For SDR displays, >= 1.0 = 80 Nits
  // y = isHDR
  // z = is10bpc
  // w = is16bpc
  float luminance_scale [4];
#endif
};

#ifdef SKIF_D3D11
struct PIXEL_CONSTANT_BUFFER_DX11 {
  float font_dims [4];
};
#endif

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplDX11_Data* ImGui_ImplDX11_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

ImGui_ImplDX11_RenderState* ImGui_ImplDX11_GetRenderState()
{
    return (ImGui_ImplDX11_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState;
}

// Forward Declarations
static void ImGui_ImplDX11_InitMultiViewportSupport();
static void ImGui_ImplDX11_ShutdownMultiViewportSupport();

// SKIF CUSTOM Forward Declarations
#ifdef SKIF_D3D11
static void ImGui_ImplDX11_RenderWindow  (ImGuiViewport* viewport, void*);
static void ImGui_ImplDX11_SwapBuffers   (ImGuiViewport* viewport, void*);
static void ImGui_ImplDX11_CreateWindow  (ImGuiViewport* viewport       );
static void ImGui_ImplDX11_DestroyWindow (ImGuiViewport* viewport       );
       void ImGui_ImplDX11_InvalidateDevice (void);

static DXGI_FORMAT SKIF_ImplDX11_ViewPort_GetDXGIFormat    (ImGuiViewport* viewport);
       bool        SKIF_ImplDX11_ViewPort_IsHDR            (ImGuiViewport* viewport);
static int         SKIF_ImplDX11_ViewPort_GetHDRMode       (ImGuiViewport* viewport);
static FLOAT       SKIF_ImplDX11_ViewPort_GetSDRWhiteLevel (ImGuiViewport* viewport);
static FLOAT       SKIF_ImplDX11_ViewPort_GetHDRLuma       (ImGuiViewport* viewport);
#endif

// Functions
#ifdef SKIF_D3D11
// SKIF: the luminance constants for the viewport being rendered (VERTEX_CONSTANT_BUFFER_DX11::luminance_scale)
static void SKIF_ImGui_ImplDX11_SetLuminanceScale (const ImDrawData* draw_data, VERTEX_CONSTANT_BUFFER_DX11* constant_buffer)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // Defaults
  constant_buffer->luminance_scale [0] = 1.0f; // x - White Level
  constant_buffer->luminance_scale [1] = 0.0f; // y - isHDR
  constant_buffer->luminance_scale [2] = 0.0f; // z - is10bpc
  constant_buffer->luminance_scale [3] = 0.0f; // w - is16bpc

  ImGuiViewport* vp = draw_data->OwnerViewport;

  if (vp != nullptr && vp->RendererUserData != nullptr)
  {
    if (SKIF_ImplDX11_ViewPort_IsHDR (vp))
    {
      constant_buffer->luminance_scale [1] = 1.0f;

      // scRGB HDR 16 bpc
      if (SKIF_ImplDX11_ViewPort_GetHDRMode (vp) == 2)
      {
        constant_buffer->luminance_scale [0] =       ( _registry.iHDRBrightness / 80.0f);   // Org: data->SKIF_GetHDRWhiteLuma    ( ) / 80.0f
        // 1 = corrected alpha (base) | +1 = linear glyph coverage | +2 = light background
        constant_buffer->luminance_scale [3] =
          1.0f + (_registry.bLinearFontAlpha ? 1.0f : 0.0f)
               + (_registry._StyleLightMode  ? 2.0f : 0.0f);
      }

      // HDR10
      else {
        constant_buffer->luminance_scale [0] = float (-_registry.iHDRBrightness);           // Org: -data->SKIF_GetHDRWhiteLuma ( )
        constant_buffer->luminance_scale [2] = 1.0f;
      }
    }

    // scRGB 16 bpc special handling
    else if (SKIF_ImplDX11_ViewPort_GetDXGIFormat (vp) == DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
      // SDR 16 bpc on HDR display
      if (SKIF_ImplDX11_ViewPort_GetSDRWhiteLevel (vp) > 80.0f)
        constant_buffer->luminance_scale [0] = (SKIF_ImplDX11_ViewPort_GetSDRWhiteLevel (vp) / 80.0f);

      // SDR 16 bpc on SDR display
      // 1 = corrected alpha (base) | +1 = linear glyph coverage | +2 = light background
      constant_buffer->luminance_scale [3] =
        1.0f + (_registry.bLinearFontAlpha ? 1.0f : 0.0f)
             + (_registry._StyleLightMode  ? 2.0f : 0.0f);
    }

    else if (SKIF_ImplDX11_ViewPort_GetDXGIFormat (vp) == DXGI_FORMAT_R10G10B10A2_UNORM)
    {
      // SDR 10 bpc on SDR display
      constant_buffer->luminance_scale [2] = 1.0f;
    }
  }
}
#endif // SKIF_D3D11

static void ImGui_ImplDX11_SetupRenderState(const ImDrawData* draw_data, ID3D11DeviceContext* device_ctx)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    // Setup viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)(int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x + 0.5f);
    vp.Height = (float)(int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y + 0.5f);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0;
    device_ctx->RSSetViewports(1, &vp);

    // Setup orthographic projection matrix into our constant buffer
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    if (device_ctx->Map(bd->pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource) == S_OK)
    {
        VERTEX_CONSTANT_BUFFER_DX11* constant_buffer = (VERTEX_CONSTANT_BUFFER_DX11*)mapped_resource.pData;
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
        };
        memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
#ifdef SKIF_D3D11
        static_assert((sizeof(VERTEX_CONSTANT_BUFFER_DX11) % 16) == 0, "Constant Buffer size must be 16-byte aligned");
        SKIF_ImGui_ImplDX11_SetLuminanceScale(draw_data, constant_buffer);
#endif
        device_ctx->Unmap(bd->pVertexConstantBuffer, 0);
    }

    // Setup shader and vertex buffers
    unsigned int stride = sizeof(ImDrawVert);
    unsigned int offset = 0;
    device_ctx->IASetInputLayout(bd->pInputLayout);
    device_ctx->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    device_ctx->IASetIndexBuffer(bd->pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_ctx->VSSetShader(bd->pVertexShader, nullptr, 0);
    device_ctx->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    device_ctx->PSSetShader(bd->pPixelShader, nullptr, 0);
    device_ctx->PSSetSamplers(0, 1, &bd->pTexSamplerLinear);
    device_ctx->GSSetShader(nullptr, nullptr, 0);
    device_ctx->HSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    device_ctx->DSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    device_ctx->CSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..

    // Setup render state
    const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    device_ctx->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
    device_ctx->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    device_ctx->RSSetState(bd->pRasterizerState);
}

// Draw callbacks
static void ImGui_ImplDX11_DrawCallback_ResetRenderState(const ImDrawList*, const ImDrawCmd*)       {} // Intentionally empty. Used as an identifier for rendering loop to call its code. Simpler to implement this way.
static void ImGui_ImplDX11_DrawCallback_SetSamplerLinear(const ImDrawList*, const ImDrawCmd*)       { ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData(); bd->RenderState->DeviceContext->PSSetSamplers(0, 1, &bd->pTexSamplerLinear); }
static void ImGui_ImplDX11_DrawCallback_SetSamplerNearest(const ImDrawList*, const ImDrawCmd*)      { ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData(); bd->RenderState->DeviceContext->PSSetSamplers(0, 1, &bd->pTexSamplerNearest); }

// Render function
#ifndef SKIF_D3D11
void ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ID3D11DeviceContext* device = bd->pd3dDeviceContext;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplDX11_UpdateTexture(tex);

    // Create and grow vertex/index buffers if needed
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = (UINT)bd->VertexBufferSize * sizeof(ImDrawVert);
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        if (bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pVB) < 0)
            return;
    }
    if (!bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (bd->pIB) { bd->pIB->Release(); bd->pIB = nullptr; }
        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = (UINT)bd->IndexBufferSize * sizeof(ImDrawIdx);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pIB) < 0)
            return;
    }

    // Upload vertex/index data into a single contiguous GPU buffer
    D3D11_MAPPED_SUBRESOURCE vtx_resource, idx_resource;
    if (device->Map(bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource) != S_OK)
        return;
    if (device->Map(bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource) != S_OK)
        return;
    ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource.pData;
    ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource.pData;
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += draw_list->VtxBuffer.Size;
        idx_dst += draw_list->IdxBuffer.Size;
    }
    device->Unmap(bd->pVB, 0);
    device->Unmap(bd->pIB, 0);

    // Backup DX state that will be modified to restore it afterwards (unfortunately this is very ugly looking and verbose. Close your eyes!)
    struct BACKUP_DX11_STATE
    {
        UINT                        ScissorRectsCount, ViewportsCount;
        D3D11_RECT                  ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D11_VIEWPORT              Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11RasterizerState*      RS;
        ID3D11BlendState*           BlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        UINT                        StencilRef;
        ID3D11DepthStencilState*    DepthStencilState;
        ID3D11ShaderResourceView*   PSShaderResource;
        ID3D11SamplerState*         PSSampler;
        ID3D11PixelShader*          PS;
        ID3D11VertexShader*         VS;
        ID3D11GeometryShader*       GS;
        UINT                        PSInstancesCount, VSInstancesCount, GSInstancesCount;
        ID3D11ClassInstance         *PSInstances[256], *VSInstances[256], *GSInstances[256];   // 256 is max according to PSSetShader documentation
        D3D11_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
        ID3D11Buffer*               IndexBuffer, *VertexBuffer, *VSConstantBuffer;
        UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT                 IndexBufferFormat;
        ID3D11InputLayout*          InputLayout;
    };
    BACKUP_DX11_STATE old = {};
    old.ScissorRectsCount = old.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    device->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
    device->RSGetViewports(&old.ViewportsCount, old.Viewports);
    device->RSGetState(&old.RS);
    device->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
    device->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
    device->PSGetShaderResources(0, 1, &old.PSShaderResource);
    device->PSGetSamplers(0, 1, &old.PSSampler);
    old.PSInstancesCount = old.VSInstancesCount = old.GSInstancesCount = 256;
    device->PSGetShader(&old.PS, old.PSInstances, &old.PSInstancesCount);
    device->VSGetShader(&old.VS, old.VSInstances, &old.VSInstancesCount);
    device->VSGetConstantBuffers(0, 1, &old.VSConstantBuffer);
    device->GSGetShader(&old.GS, old.GSInstances, &old.GSInstancesCount);

    device->IAGetPrimitiveTopology(&old.PrimitiveTopology);
    device->IAGetIndexBuffer(&old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset);
    device->IAGetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset);
    device->IAGetInputLayout(&old.InputLayout);

    // Setup desired DX state
    ImGui_ImplDX11_SetupRenderState(draw_data, device);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplDX11_RenderState render_state;
    render_state.Device = bd->pd3dDevice;
    render_state.DeviceContext = bd->pd3dDeviceContext;
    render_state.VertexConstantBuffer = bd->pVertexConstantBuffer;
    platform_io.Renderer_RenderState = bd->RenderState = &render_state;

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_idx_offset = 0;
    int global_vtx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                if (pcmd->UserCallback == ImGui_ImplDX11_DrawCallback_ResetRenderState)
                    ImGui_ImplDX11_SetupRenderState(draw_data, device);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                const D3D11_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                device->RSSetScissorRects(1, &r);

                // Bind texture, Draw
                ID3D11ShaderResourceView* texture_srv = (ID3D11ShaderResourceView*)pcmd->GetTexID();
                device->PSSetShaderResources(0, 1, &texture_srv);
                device->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }
    platform_io.Renderer_RenderState = bd->RenderState = nullptr;

    // Restore modified DX state
    device->RSSetScissorRects(old.ScissorRectsCount, old.ScissorRects);
    device->RSSetViewports(old.ViewportsCount, old.Viewports);
    device->RSSetState(old.RS); if (old.RS) old.RS->Release();
    device->OMSetBlendState(old.BlendState, old.BlendFactor, old.SampleMask); if (old.BlendState) old.BlendState->Release();
    device->OMSetDepthStencilState(old.DepthStencilState, old.StencilRef); if (old.DepthStencilState) old.DepthStencilState->Release();
    device->PSSetShaderResources(0, 1, &old.PSShaderResource); if (old.PSShaderResource) old.PSShaderResource->Release();
    device->PSSetSamplers(0, 1, &old.PSSampler); if (old.PSSampler) old.PSSampler->Release();
    device->PSSetShader(old.PS, old.PSInstances, old.PSInstancesCount); if (old.PS) old.PS->Release();
    for (UINT i = 0; i < old.PSInstancesCount; i++) if (old.PSInstances[i]) old.PSInstances[i]->Release();
    device->VSSetShader(old.VS, old.VSInstances, old.VSInstancesCount); if (old.VS) old.VS->Release();
    device->VSSetConstantBuffers(0, 1, &old.VSConstantBuffer); if (old.VSConstantBuffer) old.VSConstantBuffer->Release();
    device->GSSetShader(old.GS, old.GSInstances, old.GSInstancesCount); if (old.GS) old.GS->Release();
    for (UINT i = 0; i < old.VSInstancesCount; i++) if (old.VSInstances[i]) old.VSInstances[i]->Release();
    device->IASetPrimitiveTopology(old.PrimitiveTopology);
    device->IASetIndexBuffer(old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset); if (old.IndexBuffer) old.IndexBuffer->Release();
    device->IASetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset); if (old.VertexBuffer) old.VertexBuffer->Release();
    device->IASetInputLayout(old.InputLayout); if (old.InputLayout) old.InputLayout->Release();
}
#else
void ImGui_ImplDX11_RenderDrawData (ImDrawData *draw_data)
{
  // Avoid rendering when minimized
  if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
      return;

  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
  ID3D11DeviceContext* ctx = bd->pd3dDeviceContext;

  // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
  // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
  if (draw_data->Textures != nullptr)
    for (ImTextureData* tex : *draw_data->Textures)
      if (tex->Status != ImTextureStatus_OK)
        ImGui_ImplDX11_UpdateTexture (tex);

  // Create and grow vertex/index buffers if needed
  if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
  {
    if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }

    bd->VertexBufferSize =
      draw_data->TotalVtxCount + 5000;

    D3D11_BUFFER_DESC
    buffer_desc                = { };
    buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth      = bd->VertexBufferSize * sizeof (ImDrawVert);
    buffer_desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags      = 0;

    if (FAILED (bd->pd3dDevice->CreateBuffer (&buffer_desc, nullptr, &bd->pVB)))
      return;
  }

  if (! bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount)
  {
    if (bd->pIB) { bd->pIB->Release(); bd->pIB = nullptr; }

    bd->IndexBufferSize =
      draw_data->TotalIdxCount + 10000;

    D3D11_BUFFER_DESC
    buffer_desc                = { };
    buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth      = bd->IndexBufferSize * sizeof (ImDrawIdx);
    buffer_desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED (bd->pd3dDevice->CreateBuffer (&buffer_desc, nullptr, &bd->pIB)))
      return;
  }

  // Upload vertex/index data into a single contiguous GPU buffer
  D3D11_MAPPED_SUBRESOURCE
    vtx_resource = { },
    idx_resource = { };

  if (FAILED (ctx->Map (bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource)))
    return;
  if (FAILED (ctx->Map (bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource)))
  {
    ctx->Unmap (bd->pVB, 0);
    return;
  }

  ImDrawVert *vtx_dst =
    static_cast <ImDrawVert *> (vtx_resource.pData);
  ImDrawIdx  *idx_dst =
    static_cast <ImDrawIdx  *> (idx_resource.pData);

  for (const ImDrawList* draw_list : draw_data->CmdLists)
  {
    memcpy ( vtx_dst, draw_list->VtxBuffer.Data,
                      draw_list->VtxBuffer.Size * sizeof (ImDrawVert) );
    memcpy ( idx_dst, draw_list->IdxBuffer.Data,
                      draw_list->IdxBuffer.Size * sizeof (ImDrawIdx)  );

    vtx_dst += draw_list->VtxBuffer.Size;
    idx_dst += draw_list->IdxBuffer.Size;
  }

  ctx->Unmap (bd->pVB, 0);
  ctx->Unmap (bd->pIB, 0);

  // Pixel shader constants: font_dims != 0 tells the pixel shader that it samples the (alpha-only) font atlas
  {
    D3D11_MAPPED_SUBRESOURCE
          mapped_resource = { };

    // Assert that the constant buffer remains 16-byte aligned.
    static_assert((sizeof(PIXEL_CONSTANT_BUFFER_DX11) % 16) == 0, "Constant Buffer size must be 16-byte aligned");

    if (FAILED (ctx->Map (bd->pFontConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
      return;

    PIXEL_CONSTANT_BUFFER_DX11 *pix_constant_buffer =
        static_cast <PIXEL_CONSTANT_BUFFER_DX11 *> (
                              mapped_resource.pData
        );

    // The font atlas texture is now created and resized by ImGui on demand (ImTextureData)
    const ImTextureData* font_tex = ImGui::GetIO ().Fonts->TexData;

    pix_constant_buffer->font_dims [0] = (font_tex != nullptr) ? (float)ImMax (1, font_tex->Width)  : 1.0f;
    pix_constant_buffer->font_dims [1] = (font_tex != nullptr) ? (float)ImMax (1, font_tex->Height) : 1.0f;
    pix_constant_buffer->font_dims [2] = 0.0f;
    pix_constant_buffer->font_dims [3] = 0.0f;

    ctx->Unmap ( bd->pFontConstantBuffer, 0 );

    if (FAILED (ctx->Map (bd->pPixelConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
      return;

    pix_constant_buffer =
      static_cast <PIXEL_CONSTANT_BUFFER_DX11 *> (
        mapped_resource.pData
      );

    pix_constant_buffer->font_dims [0] = 0.0f;
    pix_constant_buffer->font_dims [1] = 0.0f;
    pix_constant_buffer->font_dims [2] = 0.0f;
    pix_constant_buffer->font_dims [3] = 0.0f;

    ctx->Unmap ( bd->pPixelConstantBuffer, 0 );
  }

  // Setup desired DX state (this also writes the projection matrix and SKIF's luminance constants)
  ImGui_ImplDX11_SetupRenderState (draw_data, ctx);

  // Setup render state structure (for callbacks and custom texture bindings)
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  ImGui_ImplDX11_RenderState render_state;
  render_state.Device               = bd->pd3dDevice;
  render_state.DeviceContext        = bd->pd3dDeviceContext;
  render_state.VertexConstantBuffer = bd->pVertexConstantBuffer;
  platform_io.Renderer_RenderState  = bd->RenderState = &render_state;

  // Render command lists
  // (Because we merged all buffers into a single one, we maintain our own offset into them)
  int global_idx_offset = 0;
  int global_vtx_offset = 0;

  ImVec2 clip_off   = draw_data->DisplayPos;
  ImVec2 clip_scale = draw_data->FramebufferScale;

  const ImTextureData* font_tex = ImGui::GetIO ().Fonts->TexData;

  for (const ImDrawList* draw_list : draw_data->CmdLists)
  {
    for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
    {
      ctx->PSSetConstantBuffers ( 0, 1, &bd->pPixelConstantBuffer );

      const ImDrawCmd *pcmd =
        &draw_list->CmdBuffer [cmd_i];

      if (pcmd->UserCallback != nullptr)
      {
        // User callback, registered via ImDrawList::AddCallback()
        if (pcmd->UserCallback == ImGui_ImplDX11_DrawCallback_ResetRenderState)
          ImGui_ImplDX11_SetupRenderState (draw_data, ctx);
        else
          pcmd->UserCallback (draw_list, pcmd);
      }

      else
      {
        // Project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_min ((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
        ImVec2 clip_max ((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
        if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
          continue;

        // Apply scissor/clipping rectangle
        const D3D11_RECT r =
          { (LONG)clip_min.x, (LONG)clip_min.y,
            (LONG)clip_max.x, (LONG)clip_max.y };

        ctx->RSSetScissorRects (1, &r);

        // Bind texture, Draw
        ID3D11ShaderResourceView *texture_srv =
          (ID3D11ShaderResourceView *)(intptr_t)pcmd->GetTexID ();

        // The font atlas is the one texture managed by ImGui (ImTextureData); covers, icons, etc. are plain IDs.
        //   It may be recreated when it grows, so compare against the atlas' current texture.
        if (pcmd->TexRef._TexData != nullptr && pcmd->TexRef._TexData == font_tex)
        ctx->PSSetConstantBuffers ( 0, 1, &bd->pFontConstantBuffer );
        ctx->PSSetShaderResources ( 0, 1, &texture_srv);
        ctx->DrawIndexed          ( pcmd->ElemCount,
                                    pcmd->IdxOffset + global_idx_offset,
                                    pcmd->VtxOffset + global_vtx_offset );
      }
    }

    global_idx_offset += draw_list->IdxBuffer.Size;
    global_vtx_offset += draw_list->VtxBuffer.Size;
  }

  platform_io.Renderer_RenderState = bd->RenderState = nullptr;
}
#endif // !SKIF_D3D11

static void ImGui_ImplDX11_DestroyTexture(ImTextureData* tex)
{
    if (ImGui_ImplDX11_Texture* backend_tex = (ImGui_ImplDX11_Texture*)tex->BackendUserData)
    {
        IM_ASSERT(backend_tex->pTextureView == (ID3D11ShaderResourceView*)(intptr_t)tex->TexID);
        backend_tex->pTextureView->Release();
        backend_tex->pTexture->Release();
        IM_DELETE(backend_tex);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(ImTextureID_Invalid);
        tex->BackendUserData = nullptr;
    }
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGui_ImplDX11_UpdateTexture(ImTextureData* tex)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (tex->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //IMGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
#ifndef SKIF_D3D11
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        const DXGI_FORMAT dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
#else
        // SKIF: Alpha8 is preferred for the font atlas (SKIF's pixel shader supplies RGB = 1 for font draws)
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32 || tex->Format == ImTextureFormat_Alpha8);
        const DXGI_FORMAT dxgi_format = (tex->Format == ImTextureFormat_Alpha8) ? DXGI_FORMAT_A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
        void* pixels = tex->GetPixels();
        ImGui_ImplDX11_Texture* backend_tex = IM_NEW(ImGui_ImplDX11_Texture)();

        // Create texture
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = (UINT)tex->Width;
        desc.Height = (UINT)tex->Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = dxgi_format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = pixels;
        subResource.SysMemPitch = desc.Width * tex->BytesPerPixel;
        subResource.SysMemSlicePitch = 0;
        bd->pd3dDevice->CreateTexture2D(&desc, &subResource, &backend_tex->pTexture);
#ifdef SKIF_D3D11
        if (backend_tex->pTexture == nullptr)
        {
            PLOG_FATAL << "Failed creating texture " << tex->Width << "x" << tex->Height << " (format " << (int)tex->Format << ")!";
        }
#endif
        IM_ASSERT(backend_tex->pTexture != nullptr && "Backend failed to create texture!");

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = dxgi_format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        bd->pd3dDevice->CreateShaderResourceView(backend_tex->pTexture, &srvDesc, &backend_tex->pTextureView);
        IM_ASSERT(backend_tex->pTextureView != nullptr && "Backend failed to create texture!");

        // Store identifiers
        tex->SetTexID((ImTextureID)(intptr_t)backend_tex->pTextureView);
        tex->SetStatus(ImTextureStatus_OK);
        tex->BackendUserData = backend_tex;
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        ImGui_ImplDX11_Texture* backend_tex = (ImGui_ImplDX11_Texture*)tex->BackendUserData;
        IM_ASSERT(backend_tex->pTextureView == (ID3D11ShaderResourceView*)(intptr_t)tex->TexID);
        for (ImTextureRect& r : tex->Updates)
        {
            D3D11_BOX box = { (UINT)r.x, (UINT)r.y, (UINT)0, (UINT)(r.x + r.w), (UINT)(r.y + r .h), (UINT)1 };
            bd->pd3dDeviceContext->UpdateSubresource(backend_tex->pTexture, 0, &box, tex->GetPixelsAt(r.x, r.y), (UINT)tex->GetPitch(), 0);
        }
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        ImGui_ImplDX11_DestroyTexture(tex);
}

#ifndef SKIF_D3D11
bool    ImGui_ImplDX11_CreateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return false;
    ImGui_ImplDX11_InvalidateDeviceObjects();

    // By using D3DCompile() from <d3dcompiler.h> / d3dcompiler.lib, we introduce a dependency to a given version of d3dcompiler_XX.dll (see D3DCOMPILER_DLL_A)
    // If you would like to use this DX11 sample code but remove this dependency you can:
    //  1) compile once, save the compiled shader blobs into a file or source code and pass them to CreateVertexShader()/CreatePixelShader() [preferred solution]
    //  2) use code to detect any version of the DLL and grab a pointer to D3DCompile from the DLL.
    // See https://github.com/ocornut/imgui/pull/638 for sources and details.

    // Create the vertex shader
    {
        static const char* vertexShader =
            "cbuffer vertexBuffer : register(b0) \
            {\
              float4x4 ProjectionMatrix; \
            };\
            struct VS_INPUT\
            {\
              float2 pos : POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            struct PS_INPUT\
            {\
              float4 pos : SV_POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            PS_INPUT main(VS_INPUT input)\
            {\
              PS_INPUT output;\
              output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));\
              output.col = input.col;\
              output.uv  = input.uv;\
              return output;\
            }";

        ID3DBlob* vertexShaderBlob;
        if (FAILED(D3DCompile(vertexShader, strlen(vertexShader), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, nullptr)))
            return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
        if (bd->pd3dDevice->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &bd->pVertexShader) != S_OK)
        {
            vertexShaderBlob->Release();
            return false;
        }

        // Create the input layout
        D3D11_INPUT_ELEMENT_DESC local_layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (bd->pd3dDevice->CreateInputLayout(local_layout, 3, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &bd->pInputLayout) != S_OK)
        {
            vertexShaderBlob->Release();
            return false;
        }
        vertexShaderBlob->Release();

        // Create the constant buffer
        {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER_DX11);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            desc.MiscFlags = 0;
            bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pVertexConstantBuffer);
        }
    }

    // Create the pixel shader
    {
        static const char* pixelShader =
            "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
            return out_col; \
            }";

        ID3DBlob* pixelShaderBlob;
        if (FAILED(D3DCompile(pixelShader, strlen(pixelShader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr)))
            return false; // NB: Pass ID3DBlob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
        if (bd->pd3dDevice->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &bd->pPixelShader) != S_OK)
        {
            pixelShaderBlob->Release();
            return false;
        }
        pixelShaderBlob->Release();
    }

    // Create the blending setup
    {
        D3D11_BLEND_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.AlphaToCoverageEnable = false;
        desc.RenderTarget[0].BlendEnable = true;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        bd->pd3dDevice->CreateBlendState(&desc, &bd->pBlendState);
    }

    // Create the rasterizer state
    {
        D3D11_RASTERIZER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.ScissorEnable = true;
        desc.DepthClipEnable = true;
        bd->pd3dDevice->CreateRasterizerState(&desc, &bd->pRasterizerState);
    }

    // Create depth-stencil State
    {
        D3D11_DEPTH_STENCIL_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.DepthEnable = false;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        desc.StencilEnable = false;
        desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        desc.BackFace = desc.FrontFace;
        bd->pd3dDevice->CreateDepthStencilState(&desc, &bd->pDepthStencilState);
    }

    // Create texture sampler
    {
        D3D11_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MipLODBias = 0.0f;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = 0.0f;
        bd->pd3dDevice->CreateSamplerState(&desc, &bd->pTexSamplerLinear);
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        bd->pd3dDevice->CreateSamplerState(&desc, &bd->pTexSamplerNearest);
    }

    return true;
}
#else
bool ImGui_ImplDX11_CreateDeviceObjects (void)
{

  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
  if (!bd->pd3dDevice)
      return false;
  if (bd->pVertexShader)
      ImGui_ImplDX11_InvalidateDeviceObjects();

  // Create the vertex shader
  if (FAILED (
    bd->pd3dDevice->CreateVertexShader (
      (DWORD *)imgui_vs_bytecode,
       sizeof (imgui_vs_bytecode    ) /
       sizeof (imgui_vs_bytecode [0]),
         nullptr, &bd->pVertexShader )
     )) return false;

  // Create the input layout
  D3D11_INPUT_ELEMENT_DESC
    local_layout [] = {
      { "POSITION",
          0,       DXGI_FORMAT_R32G32_FLOAT, 0,
            offsetof (ImDrawVert,      pos),
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "TEXCOORD",
          0,       DXGI_FORMAT_R32G32_FLOAT, 0,
            offsetof (ImDrawVert,       uv),
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
      { "COLOR",
          0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
            offsetof (ImDrawVert,      col),
                D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

  if (FAILED (bd->pd3dDevice->CreateInputLayout (local_layout, 3, imgui_vs_bytecode, sizeof (imgui_vs_bytecode) / sizeof (imgui_vs_bytecode[0]), &bd->pInputLayout)))
    return false;

  // Create the constant buffers
  D3D11_BUFFER_DESC
  buffer_desc                = { };
  buffer_desc.ByteWidth      = sizeof (VERTEX_CONSTANT_BUFFER_DX11);
  buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.MiscFlags      = 0;

  if (FAILED (bd->pd3dDevice->CreateBuffer (&buffer_desc, nullptr, &bd->pVertexConstantBuffer)))
    return false;

  //* Pixel / Font constant buffer
  buffer_desc                = { };
  buffer_desc.ByteWidth      = sizeof (PIXEL_CONSTANT_BUFFER_DX11);
  buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.MiscFlags      = 0;

  if (FAILED (bd->pd3dDevice->CreateBuffer (&buffer_desc, nullptr, &bd->pPixelConstantBuffer)))
    return false;

  if (FAILED (bd->pd3dDevice->CreateBuffer (&buffer_desc, nullptr, &bd->pFontConstantBuffer)))
    return false;

  buffer_desc                = { };
  buffer_desc.ByteWidth      = sizeof (float) * 4;
  buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.MiscFlags      = 0;
  //*/

  // Create the pixel shader
  if (FAILED (bd->pd3dDevice->CreatePixelShader ((DWORD *)imgui_ps_bytecode, sizeof (imgui_ps_bytecode) / sizeof (imgui_ps_bytecode[0]), nullptr, &bd->pPixelShader)))
    return false;

  // Create the blending setup
  D3D11_BLEND_DESC
  blend_desc                                        = { };
  blend_desc.AlphaToCoverageEnable                  = false;
  blend_desc.RenderTarget [0].BlendEnable           = true;
  blend_desc.RenderTarget [0].SrcBlend              = D3D11_BLEND_ONE; // Required to prevent alpha transparency issues. Original: D3D11_BLEND_SRC_ALPHA
  blend_desc.RenderTarget [0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget [0].BlendOp               = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget [0].SrcBlendAlpha         = D3D11_BLEND_ONE;
  blend_desc.RenderTarget [0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA; //D3D11_BLEND_ZERO;
  blend_desc.RenderTarget [0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget [0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  if (FAILED (bd->pd3dDevice->CreateBlendState (&blend_desc, &bd->pBlendState)))
    return false;

  // Create the rasterizer state
  D3D11_RASTERIZER_DESC
  raster_desc                 = { };
  raster_desc.FillMode        = D3D11_FILL_SOLID;
  raster_desc.CullMode        = D3D11_CULL_NONE;
  raster_desc.ScissorEnable   = true;
  raster_desc.DepthClipEnable = true;

  if (FAILED (bd->pd3dDevice->CreateRasterizerState (&raster_desc, &bd->pRasterizerState)))
    return false;

  // Create depth-stencil State
  D3D11_DEPTH_STENCIL_DESC
  depth_stencil_desc                              = { };
  depth_stencil_desc.DepthEnable                  = false;
  depth_stencil_desc.DepthWriteMask               = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_stencil_desc.DepthFunc                    = D3D11_COMPARISON_ALWAYS;
  depth_stencil_desc.StencilEnable                = false;
  depth_stencil_desc.FrontFace.StencilFailOp      =
  depth_stencil_desc.FrontFace.StencilDepthFailOp =
  depth_stencil_desc.FrontFace.StencilPassOp      = D3D11_STENCIL_OP_KEEP;
  depth_stencil_desc.FrontFace.StencilFunc        = D3D11_COMPARISON_ALWAYS;
  depth_stencil_desc.BackFace                     =
  depth_stencil_desc.FrontFace;

  if (FAILED (bd->pd3dDevice->CreateDepthStencilState (&depth_stencil_desc, &bd->pDepthStencilState)))
    return false;

  // Create the texture samplers (until 1.92 this was done with the font texture). SKIF draws with the linear one.
  D3D11_SAMPLER_DESC
    sampler_desc                    = { };
    sampler_desc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MipLODBias         = 0.f;
    sampler_desc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD             = 0.f;
    sampler_desc.MaxLOD             = 0.f;

  if (FAILED (bd->pd3dDevice->CreateSamplerState (&sampler_desc, &bd->pTexSamplerLinear)))
    return false;

  sampler_desc.Filter               = D3D11_FILTER_MIN_MAG_MIP_POINT;

  if (FAILED (bd->pd3dDevice->CreateSamplerState (&sampler_desc, &bd->pTexSamplerNearest)))
    return false;

  // The font atlas texture is created by ImGui_ImplDX11_UpdateTexture ( ) on request

  return true;
}
#endif // !SKIF_D3D11

void    ImGui_ImplDX11_InvalidateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return;

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            ImGui_ImplDX11_DestroyTexture(tex);

    if (bd->pTexSamplerLinear)      { bd->pTexSamplerLinear->Release(); bd->pTexSamplerLinear = nullptr; }
    if (bd->pTexSamplerNearest)     { bd->pTexSamplerNearest->Release(); bd->pTexSamplerNearest = nullptr; }
#ifdef SKIF_D3D11
    if (bd->pFontConstantBuffer)    { bd->pFontConstantBuffer->Release(); bd->pFontConstantBuffer = nullptr; }   // SKIF CUSTOM
    if (bd->pPixelConstantBuffer)   { bd->pPixelConstantBuffer->Release(); bd->pPixelConstantBuffer = nullptr; } // SKIF CUSTOM
#endif
    if (bd->pIB)                    { bd->pIB->Release(); bd->pIB = nullptr; }
    if (bd->pVB)                    { bd->pVB->Release(); bd->pVB = nullptr; }
    if (bd->pBlendState)            { bd->pBlendState->Release(); bd->pBlendState = nullptr; }
    if (bd->pDepthStencilState)     { bd->pDepthStencilState->Release(); bd->pDepthStencilState = nullptr; }
    if (bd->pRasterizerState)       { bd->pRasterizerState->Release(); bd->pRasterizerState = nullptr; }
    if (bd->pPixelShader)           { bd->pPixelShader->Release(); bd->pPixelShader = nullptr; }
    if (bd->pVertexConstantBuffer)  { bd->pVertexConstantBuffer->Release(); bd->pVertexConstantBuffer = nullptr; }
    if (bd->pInputLayout)           { bd->pInputLayout->Release(); bd->pInputLayout = nullptr; }
    if (bd->pVertexShader)          { bd->pVertexShader->Release(); bd->pVertexShader = nullptr; }
}

#ifdef SKIF_D3D11
// SKIF: the largest texture the device supports (8192 on feature level 10_x, 16384 on 11_x); the font atlas stays within it
static void SKIF_ImGui_ImplDX11_SetTextureMaxSize (ID3D11Device* device)
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    const int max_size = (device != nullptr && device->GetFeatureLevel ( ) < D3D_FEATURE_LEVEL_11_0)
                       ? D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION   // 8192
                       : D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;  // 16384
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = max_size;
}
#endif // SKIF_D3D11

bool    ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplDX11_Data* bd = IM_NEW(ImGui_ImplDX11_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_dx11";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // We can honor ImGuiPlatformIO::Textures[] requests during render.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    platform_io.DrawCallback_ResetRenderState = ImGui_ImplDX11_DrawCallback_ResetRenderState;
    platform_io.DrawCallback_SetSamplerLinear = ImGui_ImplDX11_DrawCallback_SetSamplerLinear;
    platform_io.DrawCallback_SetSamplerNearest = ImGui_ImplDX11_DrawCallback_SetSamplerNearest;

    // Get factory from device
    IDXGIDevice* pDXGIDevice = nullptr;
    IDXGIAdapter* pDXGIAdapter = nullptr;
#ifndef SKIF_D3D11
    IDXGIFactory* pFactory = nullptr;
#else
    IDXGIFactory2* pFactory = nullptr;
#endif

    if (device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice)) == S_OK)
        if (pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter)) == S_OK)
            if (pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory)) == S_OK)
            {
                bd->pd3dDevice = device;
                bd->pd3dDeviceContext = device_context;
                bd->pFactory = pFactory;
            }
    if (pDXGIDevice) pDXGIDevice->Release();
    if (pDXGIAdapter) pDXGIAdapter->Release();
    bd->pd3dDevice->AddRef();
    bd->pd3dDeviceContext->AddRef();

#ifdef SKIF_D3D11
    SKIF_ImGui_ImplDX11_SetTextureMaxSize(bd->pd3dDevice);
#endif

    ImGui_ImplDX11_InitMultiViewportSupport();

    return true;
}

void ImGui_ImplDX11_Shutdown()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    ImGui_ImplDX11_ShutdownMultiViewportSupport();
    ImGui_ImplDX11_InvalidateDeviceObjects();
    if (bd->pFactory)             { bd->pFactory->Release(); }
    if (bd->pd3dDevice)           { bd->pd3dDevice->Release(); }
    if (bd->pd3dDeviceContext)    { bd->pd3dDeviceContext->Release(); }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}

#ifndef SKIF_D3D11
void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    if (!bd->pVertexShader)
        if (!ImGui_ImplDX11_CreateDeviceObjects())
            IM_ASSERT(0 && "ImGui_ImplDX11_CreateDeviceObjects() failed!");
}
#else
// SKIF: makes the initialized backend use another device (the hardware device replacing the WARP one SKIF starts on),
//   after ImGui_ImplDX11_InvalidateDevice ( ). Does the device part of ImGui_ImplDX11_Init ( ), which must not run twice.
static void ImGui_ImplDX11_SetDevice (ID3D11Device* device, ID3D11DeviceContext* device_context)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    IDXGIDevice*   pDXGIDevice  = nullptr;
    IDXGIAdapter*  pDXGIAdapter = nullptr;
    IDXGIFactory2* pFactory     = nullptr;

    if (device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice)) == S_OK)
        if (pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter)) == S_OK)
            pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory));
    if (pDXGIDevice) pDXGIDevice->Release();
    if (pDXGIAdapter) pDXGIAdapter->Release();

    bd->pd3dDevice        = device;
    bd->pd3dDeviceContext = device_context;
    bd->pFactory          = pFactory;
    bd->pd3dDevice->AddRef();
    bd->pd3dDeviceContext->AddRef();

    SKIF_ImGui_ImplDX11_SetTextureMaxSize(bd->pd3dDevice);
}

void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

    ImGuiContext& g = *ImGui::GetCurrentContext();

    // External declarations
    extern HRESULT SKIF_CreateDXGIFactory1 (REFIID riid, void **ppFactory);
    extern bool    CreateDeviceD3D         (HWND hWnd);
    extern void    CleanupDeviceD3D        (void);
    extern HWND                    SKIF_Notify_hWnd;
    extern ID3D11Device*           SKIF_pd3dDevice;
    extern ID3D11DeviceContext*    SKIF_pd3dDeviceContext;
    extern DWORD                   invalidatedDevice;
    extern bool    SKIF_D3D11_IsPendingDeviceReady (void);
    extern bool    SKIF_D3D11_AdoptPendingDevice   (void);

    // Check if the device have been removed for any reason
    RecreateD3DDevice = (RecreateD3DDevice ||
      FAILED (bd->pd3dDevice->GetDeviceRemovedReason ( )));

    // SKIF started on WARP and the hardware device is ready: switch to it (see CreateDeviceD3D)
    bool SwitchD3DDevice =
      (! RecreateD3DDevice && SKIF_D3D11_IsPendingDeviceReady ( ));

    // Only bother checking if the factory needs to be recreated if the device hasn't been removed
    bool  RecreateFactory =
                ( ! RecreateD3DDevice       &&
                    bd->pFactory != nullptr &&
                  ! bd->pFactory->IsCurrent ( ) );

    RecreateSwapChainsPending = false;

    if (RecreateSwapChains ||
        RecreateFactory    ||
        RecreateD3DDevice  ||
        SwitchD3DDevice    )
    {
      RecreateSwapChains        = false;
      RecreateSwapChainsPending = true;

      PLOG_DEBUG << "Destroying any existing swapchains and their wait objects...";
      for (int i = 0; i < g.Viewports.Size; i++)
        ImGui_ImplDX11_DestroyWindow (g.Viewports [i]);

      ImGui_ImplDX11_InvalidateDeviceObjects ( );

      if (bd->pd3dDeviceContext != nullptr)
      {
        PLOG_DEBUG << "Clearing ID3D11DeviceContext state and flushing...";
        bd->pd3dDeviceContext->ClearState ( );
        bd->pd3dDeviceContext->Flush      ( );
      }

      if (RecreateFactory || RecreateD3DDevice || SwitchD3DDevice)
      {
        PLOG_DEBUG << "Recreating factory...";
        bd->pFactory->Release();
        bd->pFactory = nullptr;

        if (! RecreateD3DDevice && ! SwitchD3DDevice)
          SKIF_CreateDXGIFactory1 (__uuidof (IDXGIFactory2), (void **)&bd->pFactory);
      }

      if (SwitchD3DDevice)
      {
        PLOG_DEBUG << "Switching from the WARP device to the hardware device...";
        ImGui_ImplDX11_InvalidateDevice ( ); // Releases the backend's references to the WARP device, context and factory

        if (SKIF_D3D11_AdoptPendingDevice ( ))
          ImGui_ImplDX11_SetDevice (SKIF_pd3dDevice, SKIF_pd3dDeviceContext); // Also gets the hardware adapter's factory
      }

      if (RecreateD3DDevice)
      {
        RecreateD3DDevice = false;

        PLOG_DEBUG << "Recreating the D3D11 device...";
        ImGui_ImplDX11_InvalidateDevice ( );
        CleanupDeviceD3D                ( );

        // At this point all traces of the previous device should have been cleared

        // The backend is still initialized, so only its device part is redone (ImGui_ImplDX11_Init ( ) must not run twice)
        if (CreateDeviceD3D             (SKIF_Notify_hWnd))                        // This creates a new device and context
          ImGui_ImplDX11_SetDevice      (SKIF_pd3dDevice, SKIF_pd3dDeviceContext); // This gets the new device's factory

        // This is used to flag that rendering should not occur until
        // any loaded textures and such also have been unloaded
        invalidatedDevice = 1;
      }

      _registry._RendererCanHDR = SKIF_Util_IsHDRActive (NULL); // true

      PLOG_DEBUG << "Recreating any necessary swapchains and their wait objects...";
      for (ImGuiViewportP* viewport : g.Viewports)
        if (viewport->PlatformWindowCreated && viewport->PlatformHandle != nullptr) // Skip viewports without a Win32 window (e.g. the hidden Debug##Default)
          ImGui_ImplDX11_CreateWindow (viewport);
    }

    // Textures destroyed by ImGui_ImplDX11_InvalidateDeviceObjects ( ) are recreated by ImGui from their CPU copy
    if (!bd->pVertexShader)
        ImGui_ImplDX11_CreateDeviceObjects();
}
#endif // !SKIF_D3D11

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
#ifndef SKIF_D3D11
struct ImGui_ImplDX11_ViewportData
{
    IDXGISwapChain*                 SwapChain;
    ID3D11RenderTargetView*         RTView;

    ImGui_ImplDX11_ViewportData()   { SwapChain = nullptr; RTView = nullptr; }
    ~ImGui_ImplDX11_ViewportData()  { IM_ASSERT(SwapChain == nullptr && RTView == nullptr); }
};
#else
struct ImGui_ImplDX11_ViewportData
{
    IDXGISwapChain1*        SwapChain;
    ID3D11RenderTargetView* RTView;
    UINT                    PresentCount;
    HANDLE                  WaitHandle;
    int                     SDRMode;       // 0 = 8 bpc,   1 = 10 bpc,      2 = 16 bpc scRGB
    FLOAT                   SDRWhiteLevel; // SDR white level in nits for the display
    bool                    HDR;
    bool                    HDRCapable;
    int                     HDRMode;       // 0 = No HDR,  1 = 10 bpc HDR,  2 = 16 bpc scRGB HDR
    FLOAT                   HDRLuma;
    FLOAT                   HDRMinLuma;
    DXGI_OUTPUT_DESC1       DXGIDesc;
    DXGI_FORMAT             DXGIFormat;

     ImGui_ImplDX11_ViewportData (void) {            SwapChain  = nullptr;   RTView  = nullptr;   WaitHandle  = 0;  PresentCount = 0; SDRMode = 0; SDRWhiteLevel = 80.0f; HDRMode = 0; HDR = false; HDRCapable = false; HDRLuma = 0.0f; HDRMinLuma = 0.0f; DXGIDesc = {   }; DXGIFormat = DXGI_FORMAT_UNKNOWN; }
    ~ImGui_ImplDX11_ViewportData (void) { IM_ASSERT (SwapChain == nullptr && RTView == nullptr && WaitHandle == 0); }
};
#endif

// Multi-Viewports: configure templates used when creating swapchains for secondary viewports. Will try them in order.
// This is intentionally not declared in the .h file yet, so you will need to copy this declaration:
void ImGui_ImplDX11_SetSwapChainDescs(const DXGI_SWAP_CHAIN_DESC* desc_templates, int desc_templates_count);
void ImGui_ImplDX11_SetSwapChainDescs(const DXGI_SWAP_CHAIN_DESC* desc_templates, int desc_templates_count)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    bd->SwapChainDescsForViewports.resize(desc_templates_count);
    memcpy(bd->SwapChainDescsForViewports.Data, desc_templates, sizeof(DXGI_SWAP_CHAIN_DESC));
}

#ifndef SKIF_D3D11
static void ImGui_ImplDX11_CreateWindow(ImGuiViewport* viewport)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ImGui_ImplDX11_ViewportData* vd = IM_NEW(ImGui_ImplDX11_ViewportData)();
    viewport->RendererUserData = vd;

    // PlatformHandleRaw should always be a HWND, whereas PlatformHandle might be a higher-level handle (e.g. GLFWWindow*, SDL's WindowID).
    // Some backends will leave PlatformHandleRaw == 0, in which case we assume PlatformHandle will contain the HWND.
    HWND hwnd = viewport->PlatformHandleRaw ? (HWND)viewport->PlatformHandleRaw : (HWND)viewport->PlatformHandle;
    IM_ASSERT(hwnd != 0);
    IM_ASSERT(vd->SwapChain == nullptr && vd->RTView == nullptr);

    // Create swap chain
    HRESULT hr = DXGI_ERROR_UNSUPPORTED;
    for (const DXGI_SWAP_CHAIN_DESC& sd_template : bd->SwapChainDescsForViewports)
    {
        IM_ASSERT(sd_template.BufferDesc.Width == 0 && sd_template.BufferDesc.Height == 0 && sd_template.OutputWindow == nullptr);
        DXGI_SWAP_CHAIN_DESC sd = sd_template;
        sd.BufferDesc.Width = (UINT)viewport->Size.x;
        sd.BufferDesc.Height = (UINT)viewport->Size.y;
        sd.OutputWindow = hwnd;
        hr = bd->pFactory->CreateSwapChain(bd->pd3dDevice, &sd, &vd->SwapChain);
        if (SUCCEEDED(hr))
            break;
    }
    IM_ASSERT(SUCCEEDED(hr));
    bd->pFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES); // Disable e.g. Alt+Enter

    // Create the render target
    if (vd->SwapChain != nullptr)
    {
        ID3D11Texture2D* pBackBuffer;
        vd->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        bd->pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &vd->RTView);
        pBackBuffer->Release();
    }
}
#endif

#ifndef SKIF_D3D11
static void ImGui_ImplDX11_DestroyWindow(ImGuiViewport* viewport)
{
    // The main viewport (owned by the application) will always have RendererUserData == nullptr since we didn't create the data for it.
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
    {
        if (vd->SwapChain)
            vd->SwapChain->Release();
        vd->SwapChain = nullptr;
        if (vd->RTView)
            vd->RTView->Release();
        vd->RTView = nullptr;
        IM_DELETE(vd);
    }
    viewport->RendererUserData = nullptr;
}
#endif

#ifndef SKIF_D3D11
static void ImGui_ImplDX11_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;
    if (vd->RTView)
    {
        vd->RTView->Release();
        vd->RTView = nullptr;
    }
    if (vd->SwapChain)
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        vd->SwapChain->ResizeBuffers(0, (UINT)size.x, (UINT)size.y, DXGI_FORMAT_UNKNOWN, 0);
        vd->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer == nullptr) { fprintf(stderr, "ImGui_ImplDX11_SetWindowSize() failed creating buffers.\n"); return; }
        bd->pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &vd->RTView);
        pBackBuffer->Release();
    }
}
#else
static void
ImGui_ImplDX11_SetWindowSize ( ImGuiViewport *viewport,
                               ImVec2         size )
{
  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
  ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;

  if (vd != nullptr)
  {
    if (vd->RTView)
    {
      vd->RTView->Release ();
      vd->RTView = nullptr;
    }

    if (vd->SwapChain)
    {
      ID3D11Texture2D* pBackBuffer = nullptr;

      DXGI_SWAP_CHAIN_DESC1       swap_desc = { };
      vd->SwapChain->GetDesc1 (&swap_desc);

      vd->SwapChain->ResizeBuffers (
        0, (UINT)size.x,
           (UINT)size.y,
            swap_desc.Format,
            swap_desc.Flags
      );
      PLOG_VERBOSE << "[" << ImGui::GetFrameCount() << "] Resized swapchain to " << size.x << "x" << size.y;

      vd->SwapChain->GetBuffer (
        0, IID_PPV_ARGS (
                    &pBackBuffer
                        )
      );

      if (pBackBuffer == nullptr) {
        PLOG_FATAL << "ImGui_ImplDX11_SetWindowSize() failed creating buffers";
        return;
      }

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = { };
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

#ifdef _SRGB
    if (swap_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
      rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
#endif

      bd->pd3dDevice->CreateRenderTargetView ( pBackBuffer, &rtvDesc, &vd->RTView );
      pBackBuffer->Release();
    }
  }
}
#endif // !SKIF_D3D11

#ifndef SKIF_D3D11
static void ImGui_ImplDX11_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;
    if (vd->RTView == nullptr) // Swap chain creation or resize failed (e.g. device removed)
        return;
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bd->pd3dDeviceContext->OMSetRenderTargets(1, &vd->RTView, nullptr);
    if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear))
        bd->pd3dDeviceContext->ClearRenderTargetView(vd->RTView, (float*)&clear_color);
    ImGui_ImplDX11_RenderDrawData(viewport->DrawData);
}

static void ImGui_ImplDX11_SwapBuffers(ImGuiViewport* viewport, void*)
{
    ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;
    if (vd->SwapChain)
        vd->SwapChain->Present(0, 0); // Present without vsync
}
#endif

static void ImGui_ImplDX11_InitMultiViewportSupport()
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow = ImGui_ImplDX11_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGui_ImplDX11_DestroyWindow;
    platform_io.Renderer_SetWindowSize = ImGui_ImplDX11_SetWindowSize;
    platform_io.Renderer_RenderWindow = ImGui_ImplDX11_RenderWindow;
    platform_io.Renderer_SwapBuffers = ImGui_ImplDX11_SwapBuffers;

    // Default swapchain format
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;
    ImGui_ImplDX11_SetSwapChainDescs(&sd, 1);
}

static void ImGui_ImplDX11_ShutdownMultiViewportSupport()
{
    ImGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE


//--------------------------------------------------------------------------------------------------------
// SKIF CUSTOM D3D11 FUNCTIONS
//--------------------------------------------------------------------------------------------------------


// Render functions

static void
SKIF_ImGui_ImplDX11_LogSwapChainFormat (ImGuiViewport *viewport)
{
  ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;

  if (vd == nullptr || vd->SwapChain == nullptr)
    return;

  static
    DXGI_SWAP_CHAIN_DESC1     swap_prev;
    DXGI_SWAP_CHAIN_DESC1     swap_desc;
  vd->SwapChain->GetDesc1 (&swap_desc);

  if (swap_prev.Height     != swap_desc.Height   ||
      swap_prev.Flags      != swap_desc.Flags    ||
      swap_prev.Format     != swap_desc.Format   ||
      swap_prev.SwapEffect != swap_desc.SwapEffect)
  {
    swap_prev = swap_desc;

    PLOG_INFO   << "The swapchain format has been changed:"
                << "\n+------------------+-------------------------------------+"
                << "\n| Resolution       | " <<   swap_desc.Width << "x" << swap_desc.Height
                << "\n| Dynamic Range    | " << ((vd->HDR) ? "HDR" : "SDR")
                << "\n| SDR White Level  | " <<   vd->SDRWhiteLevel
                << "\n| Format           | " << ((swap_desc.Format     == DXGI_FORMAT_R16G16B16A16_FLOAT)
                                            ?                            "DXGI_FORMAT_R16G16B16A16_FLOAT"
                                            :    (swap_desc.Format     == DXGI_FORMAT_R10G10B10A2_UNORM)
                                            ?                            "DXGI_FORMAT_R10G10B10A2_UNORM"
                                            :    (swap_desc.Format     == DXGI_FORMAT_R8G8B8A8_UNORM)
                                            ?                            "DXGI_FORMAT_R8G8B8A8_UNORM"
                                            :                            "Unexpected format")
                << "\n| Buffers          | " <<   swap_desc.BufferCount
                << "\n| Flags            | " << std::format("{:#x}", swap_desc.Flags)
                << "\n| Swap Effect      | " << ((swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)
                                            ?                            "DXGI_SWAP_EFFECT_FLIP_DISCARD"
                                            :    (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
                                            ?                            "DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL"
                                            :    (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
                                            ?                            "DXGI_SWAP_EFFECT_DISCARD"
                                            :    (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
                                            ?                            "DXGI_SWAP_EFFECT_SEQUENTIAL"
                                            :                            "Unexpected swap effect")
                << "\n+------------------+-------------------------------------+";
  }
}

static void ImGui_ImplDX11_RenderWindow(ImGuiViewport* viewport, void*)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;

#ifndef SKIF_D3D11
    if (vd == nullptr || vd->RTView == nullptr)
      return;

    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
#else

    if (vd            == nullptr || // Win32 window was destroyed
        vd->SwapChain == nullptr || // Swapchain was destroyed
        vd->RTView    == nullptr)   // Render target view was destroyed
    {
      if (! RecreateSwapChainsPending && ImGui::GetFrameCount() > 4)
        RecreateSwapChains = true;
      return;
    }

    ImVec4 clear_color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg]; // Use the current window bg color to clear the RTV with
#endif // !SKIF_D3D11

    bd->pd3dDeviceContext->OMSetRenderTargets(1, &vd->RTView, nullptr);
    if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear))
        bd->pd3dDeviceContext->ClearRenderTargetView(vd->RTView, (float*)&clear_color);
    ImGui_ImplDX11_RenderDrawData(viewport->DrawData);

#ifdef SKIF_D3D11
    bd->pd3dDeviceContext->OMSetRenderTargets (0,nullptr, nullptr);
#endif // SKIF_D3D11
}

static void ImGui_ImplDX11_SwapBuffers(ImGuiViewport* viewport, void*)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

    ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;

  if (vd            != nullptr && // Win32 window was destroyed
      vd->SwapChain != nullptr)   // Swapchain was destroyed
  {
    DXGI_SWAP_CHAIN_DESC1       swap_desc = { };

    if (FAILED (vd->SwapChain->GetDesc1 (&swap_desc)))
      return;

    UINT Interval = 1;

    if (      _registry.iUIMode == 2)  // VRR Compatibility Mode
      Interval    = 2; // Half Refresh Rate V-Sync

    else if (_registry.iUIMode == 0 || //              Safe Mode (BitBlt)
             _registry.iUIMode == 1  ) //            Normal Mode
      Interval    = 1; // V-Sync ON

    // Overrides
    //Interval = 0; // V-Sync OFF

    UINT PresentFlags = 0x0;

    if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD   ||
        swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL )
    {
      PresentFlags   = DXGI_PRESENT_RESTART;

      // If V-Sync is OFF (not possible at all, but keep for possible future use)
      if (Interval == 0 && _registry._RendererCanAllowTearing)
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    extern DWORD SKIF_startupTime;
    extern bool  SKIF_FirstPresentDone;
    extern DWORD SKIF_Util_timeGetTime1 (void);

    const DWORD   dwPresentStart = (! SKIF_FirstPresentDone) ? SKIF_Util_timeGetTime1 ( ) : 0;
    const HRESULT hrPresent      = vd->SwapChain->Present (Interval, PresentFlags);

    // [Startup] timing (verbose): each Present ( ) until the first one has succeeded
    if (! SKIF_FirstPresentDone)
      PLOG_VERBOSE << "[Startup] Present ( ) took " << (SKIF_Util_timeGetTime1 ( ) - dwPresentStart) << " ms, "
                   << (dwPresentStart - SKIF_startupTime) << " ms after init (HRESULT 0x" << std::hex << static_cast <unsigned long> (hrPresent) << std::dec
                   << ", window " << (IsWindowVisible (static_cast <HWND> (viewport->PlatformHandle)) ? "visible" : "hidden") << ").";

    if (SUCCEEDED (hrPresent))
    {
      vd->PresentCount++;

      static bool runOnce = true;
      if (runOnce)
      {
        runOnce = false;

        DWORD current_time = SKIF_Util_timeGetTime1();

        PLOG_INFO << "Presented first frame! Init -> Present took " << (current_time - SKIF_startupTime) << " ms.";

        SKIF_FirstPresentDone = true;
      }
    }
  }
}

static void
ImGui_ImplDX11_CreateWindow (ImGuiViewport *viewport)
{
  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

  if (! bd->pd3dDevice)
    return;

  if (! bd->pFactory)
    return;

  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ImGui_ImplDX11_ViewportData* vd = IM_NEW(ImGui_ImplDX11_ViewportData)();
  viewport->RendererUserData = vd;

  static DXGI_SWAP_EFFECT prev_swapEffect;

  // DXGI WARNING: IDXGIFactory::CreateSwapChain/IDXGISwapChain::ResizeBuffers: The buffer width  inferred from the output window is zero. Taking 8 as a reasonable default instead [ MISCELLANEOUS WARNING #1: ]
  // DXGI WARNING: IDXGIFactory::CreateSwapChain/IDXGISwapChain::ResizeBuffers: The buffer height inferred from the output window is zero. Taking 8 as a reasonable default instead [ MISCELLANEOUS WARNING #2: ]
  if (viewport->Size.x == 0.0f || viewport->Size.y == 0.0f)
    return;

  // PlatformHandleRaw should always be a HWND, whereas PlatformHandle might be a higher-level handle (e.g. GLFWWindow*, SDL_Window*).
  // Some back-end will leave PlatformHandleRaw NULL, in which case we assume PlatformHandle will contain the HWND.
  HWND hWnd =
    ( viewport->PlatformHandleRaw != nullptr ?
        (HWND)viewport->PlatformHandleRaw    :
        (HWND)viewport->PlatformHandle );

  //IM_ASSERT (hWnd != nullptr);

  // DXGI ERROR: IDXGIFactory::CreateSwapChain: No target window specified in DXGI_SWAP_CHAIN_DESC, and no window associated with owning factory. [ MISCELLANEOUS ERROR #6: ]
  if (hWnd == nullptr)
    return;

  // Occurs when using Ctrl+Tab and closing a standalone popup...
  // Apparently the viewport sticks around for another frame despite
  //   the window having been terminated already
  if (! ::IsWindow (hWnd))
    return;

  IM_ASSERT ( vd->SwapChain == nullptr &&
              vd->RTView    == nullptr );

  DXGI_FORMAT dxgi_format;

  // HDR formats
  if (_registry._RendererCanHDR && _registry.iHDRMode > 0)
  {
    if      (_registry.iHDRMode == 2)
      dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // scRGB (16 bpc)
    else
      dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // HDR10 (10 bpc)
  }

  // SDR formats
  else {
    if      (_registry.iSDRMode == 2)
      dxgi_format = DXGI_FORMAT_R16G16B16A16_FLOAT; // 16 bpc
    else if (_registry.iSDRMode == 1 && SKIF_Util_IsWindowsVersionOrGreater (10, 0, 16299))
      dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // 10 bpc (apparently only supported for flip on Win10 1709+)
    else
      dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;     // 8 bpc;
  }

  // Create the swapchain for the viewport
  DXGI_SWAP_CHAIN_DESC1
    swap_desc                  = { };
  swap_desc.Width              = (UINT)viewport->Size.x;
  swap_desc.Height             = (UINT)viewport->Size.y;
  swap_desc.Format             = dxgi_format;
  swap_desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_desc.Scaling            = (_registry.iUIMode > 0) ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;
  swap_desc.Flags              = 0x0;
  swap_desc.SampleDesc.Count   = 1;
  swap_desc.SampleDesc.Quality = 0;

  // Assume flip by default
  swap_desc.BufferCount  = 3; // Must be 2-16 for flip model // 2 to prevent SKIF from rendering x2 the refresh rate

  if (_registry._RendererCanWaitSwapchain)
    swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  if (_registry._RendererCanAllowTearing)
    swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

  // Normal (1) / VRR Compatibility Mode (2)
  if (_registry.iUIMode > 0)
  {
    for (auto  _swapEffect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_DISCARD}) // DXGI_SWAP_EFFECT_FLIP_DISCARD
    {
      swap_desc.SwapEffect = _swapEffect;

      // In case flip failed, fall back to using BitBlt
      if (_swapEffect == DXGI_SWAP_EFFECT_DISCARD)
      {
        swap_desc.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.Scaling         = DXGI_SCALING_STRETCH;
        swap_desc.BufferCount     = 1;
        swap_desc.Flags           = 0x0;
        _registry._RendererCanHDR = false;
        _registry.iUIMode         = 0;
      }

      if (SUCCEEDED (bd->pFactory->CreateSwapChainForHwnd (bd->pd3dDevice, hWnd, &swap_desc, NULL, NULL, &vd->SwapChain)))
        break;
      else if (FAILED (bd->pd3dDevice->GetDeviceRemovedReason ( )) || ! bd->pFactory->IsCurrent ( ))
        return;
    }
  }

  // Safe Mode (BitBlt Discard)
  else {
    //swap_desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SwapEffect  = DXGI_SWAP_EFFECT_DISCARD;
    swap_desc.BufferCount = 1;
    swap_desc.Flags       = 0x0;

    bd->pFactory->CreateSwapChainForHwnd (bd->pd3dDevice, hWnd, &swap_desc, NULL, NULL,
               &vd->SwapChain );
  }

  // Do we have a swapchain?
  if (vd->SwapChain != nullptr)
  {
    // Disable e.g. Alt+Enter (as upstream does since 1.92.4), which does not work with multiple viewports
    bd->pFactory->MakeWindowAssociation (hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    _registry._RendererHDREnabled = false;

    vd->DXGIFormat = swap_desc.Format;

    CComQIPtr <IDXGISwapChain3>
        pSwapChain3 (vd->SwapChain);

    if (pSwapChain3 != nullptr)
    {
      // Are we on a flip based model?
      if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD   ||
          swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL )
      {
        // Retrieve the current default adapter.
        CComPtr   <IDXGIOutput>   pOutput;
        CComPtr   <IDXGIAdapter1> pAdapter;
        CComQIPtr <IDXGIFactory2> pFactory1 (bd->pFactory);

        if (SUCCEEDED (pFactory1->EnumAdapters1 (0, &pAdapter)))
        {
          auto _ComputeIntersectionArea =
                [&](long ax1, long ay1, long ax2, long ay2,
                    long bx1, long by1, long bx2, long by2) -> int
          {
            return std::max(0l, std::min(ax2, bx2) - std::max(ax1, bx1)) * std::max(0l, std::min(ay2, by2) - std::max(ay1, by1));
          };

          UINT i = 0;
          IDXGIOutput* currentOutput;
          float bestIntersectArea = -1;

          RECT m_windowBounds;
          GetWindowRect (hWnd, &m_windowBounds);

          // Iterate through the DXGI outputs associated with the DXGI adapter,
          // and find the output whose bounds have the greatest overlap with the
          // app window (i.e. the output for which the intersection area is the
          // greatest).
          while (pAdapter->EnumOutputs (i, &currentOutput) != DXGI_ERROR_NOT_FOUND)
          {
            // Get the retangle bounds of the app window
            int ax1 = m_windowBounds.left;
            int ay1 = m_windowBounds.top;
            int ax2 = m_windowBounds.right;
            int ay2 = m_windowBounds.bottom;

            // Get the rectangle bounds of current output
            DXGI_OUTPUT_DESC desc;
            if (FAILED (currentOutput->GetDesc (&desc)))
              continue;

            RECT r  = desc.DesktopCoordinates;
            int bx1 = r.left;
            int by1 = r.top;
            int bx2 = r.right;
            int by2 = r.bottom;

            // Compute the intersection
            int intersectArea = _ComputeIntersectionArea (ax1, ay1, ax2, ay2, bx1, by1, bx2, by2);
            if (intersectArea > bestIntersectArea)
            {
              pOutput = currentOutput;
              bestIntersectArea = static_cast<float>(intersectArea);
            }

            i++;
          }
        }

        // Having determined the output (display) upon which the app is primarily being
        // rendered, retrieve the HDR capabilities of that display by checking the color space.
        CComQIPtr <IDXGIOutput6>
            pOutput6 (pOutput);

        if (pOutput6 != nullptr)
        {
          UINT uiHdrFlags = 0x0;

          pOutput6->GetDesc1 (&vd->DXGIDesc);

          vd->HDRCapable    = SKIF_Util_IsHDRSupported   (vd->DXGIDesc.Monitor);
          vd->SDRWhiteLevel = SKIF_Util_GetSDRWhiteLevel (vd->DXGIDesc.Monitor);
          vd->HDRLuma       = vd->DXGIDesc.MaxLuminance;

  #pragma region Enable HDR
          // DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709    - SDR display with no Advanced Color capabilities
          // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709    - Standard definition for scRGB, and is usually used with 16 bit integer, 16 bit floating point, or 32 bit floating point color channels.
          // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 - HDR display with all Advanced Color capabilities

          if (vd->HDRCapable && _registry.iHDRMode > 0)
          {
            DXGI_COLOR_SPACE_TYPE dxgi_cst =
              (_registry.iHDRMode == 2)
                ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709     // scRGB (FP16 only)
                : DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; // HDR10

            pSwapChain3->CheckColorSpaceSupport (dxgi_cst, &uiHdrFlags);

            if ( DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT ==
                  ( uiHdrFlags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT )
                )
            {
              // Is the output display in HDR mode?
              if (vd->DXGIDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
              {
                vd->HDR     = true;
                vd->HDRMode = _registry.iHDRMode;

                pSwapChain3->SetColorSpace1 (dxgi_cst);

                _registry._RendererHDREnabled = true;
              }
            }
          }
        }
#pragma endregion
      }
    }

    if (! vd->HDR)
      vd->SDRMode = _registry.iSDRMode;

    CComPtr <
      ID3D11Texture2D
    >              pBackBuffer;
    vd->SwapChain->GetBuffer ( 0, IID_PPV_ARGS (
                  &pBackBuffer.p                 )
                               );

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = { };
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

#ifdef _SRGB
    if (swap_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
      rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
#endif

    bd->pd3dDevice->CreateRenderTargetView ( pBackBuffer,
                 &rtvDesc, &vd->RTView );

    // Only print swapchain info for the main swapchain
    extern HWND SKIF_ImGui_hWnd;
    if (hWnd == SKIF_ImGui_hWnd)
    {
      SKIF_ImGui_ImplDX11_LogSwapChainFormat (viewport);
    }

    if (_registry._RendererCanWaitSwapchain &&
          (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD    ||
           swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ))
    {
      CComQIPtr <IDXGISwapChain2>
          pSwapChain2 (vd->SwapChain);

      if (pSwapChain2 != nullptr)
      {
        // The maximum number of back buffer frames that will be queued for the swap chain. This value is 1 by default.
        // This method is only valid for use on swap chains created with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT.
        if (SUCCEEDED (pSwapChain2->SetMaximumFrameLatency (1)))
        {
          if (hWnd == SKIF_ImGui_hWnd)
          {
            vd->WaitHandle =
              pSwapChain2->GetFrameLatencyWaitableObject  ( );

            if (vd->WaitHandle)
            {
              // One-time wait to align the thread for minimum latency (reduces latency by half in testing)
              WaitForSingleObjectEx (vd->WaitHandle, 1000, true);
              // Block this thread until the swap chain is ready for presenting. Note that it is
              // important to call this before the first Present in order to minimize the latency
              // of the swap chain.
            }
          }
        }
      }
    }
  }
  else {
    PLOG_WARNING << "Swapchain is a nullptr!";
  }
}

static void ImGui_ImplDX11_DestroyWindow(ImGuiViewport* viewport)
{
    // The main viewport (owned by the application) will always have RendererUserData == nullptr since we didn't create the data for it.
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
    {
        if (vd->WaitHandle) {
          CloseHandle (
            vd->WaitHandle
          );
        }
        vd->WaitHandle = 0;

        if (vd->SwapChain)
            vd->SwapChain->Release();
        vd->SwapChain = nullptr;

        if (vd->RTView)
            vd->RTView->Release();
        vd->RTView = nullptr;

        IM_DELETE(vd);
    }

    viewport->RendererUserData = nullptr;
}

void    ImGui_ImplDX11_InvalidateDevice()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    if (bd->pFactory)             { bd->pFactory->Release(); }
        bd->pFactory              = nullptr;
    if (bd->pd3dDevice)           { bd->pd3dDevice->Release(); }
        bd->pd3dDevice            = nullptr;
    if (bd->pd3dDeviceContext)    { bd->pd3dDeviceContext->Release(); }
        bd->pd3dDeviceContext     = nullptr;
}

// SKIF Custom helper functions

static DXGI_FORMAT SKIF_ImplDX11_ViewPort_GetDXGIFormat(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->DXGIFormat;

    return DXGI_FORMAT_UNKNOWN;
}

bool SKIF_ImplDX11_ViewPort_IsHDR(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->HDRMode;

    return 0;
}

bool SKIF_ImplDX11_ViewPort_IsHDRCapable(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->HDRCapable;

    return false;
}

static int SKIF_ImplDX11_ViewPort_GetHDRMode(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->HDRMode;

    return 0;
}

static FLOAT SKIF_ImplDX11_ViewPort_GetSDRWhiteLevel(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->SDRWhiteLevel;

    return 80.0f;
}

UINT SKIF_ImplDX11_ViewPort_GetPresentCount(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->PresentCount;

    return 0;
}

HANDLE SKIF_ImplDX11_ViewPort_GetWaitHandle(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->WaitHandle;

    return NULL;
}

static FLOAT SKIF_ImplDX11_ViewPort_GetHDRLuma(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->HDRLuma;

    return 80.0f;
}

void SKIF_ImplDX11_ViewPort_ClearAndPresent (ImGuiViewport* viewport)
{
  // I have no idea why SKIF requires this stupid workaround but if I don't do this it keeps throwing
  //   LNK2001: unresolved external symbol IID_ID3D11Device
  // which makes no bloody sense since d3d11.lib is included and VS can resolve it... Stupid thing
  // works in SKIV so I dunno why it fails to work as expected in SKIF...
  static const GUID IID_ID3D11DeviceWorkaround = {
    0xdb6f6ddb,0xac77,0x4e88,0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40
  };

  if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
  {
    CComPtr <ID3D11Device> pDev;
    if (SUCCEEDED (vd->SwapChain->GetDevice (IID_ID3D11DeviceWorkaround, (void **)&pDev.p)))
    {
      CComPtr <ID3D11DeviceContext> pDevCtx;
      pDev->GetImmediateContext   (&pDevCtx.p);

      FLOAT fClearColor [4] =
        { 0.0f, 0.0f, 0.0f, 0.0f };

      pDevCtx->ClearRenderTargetView (vd->RTView, fClearColor);

#if 1
      vd->SwapChain->Present (0,0);

      // We must wait for the Present to complete if SKIV is using a
      //   latency waitable SwapChain, or this would be permanent!
      CComQIPtr <IDXGISwapChain2>
          pSwapChain2 (vd->SwapChain);
      if (pSwapChain2 != nullptr)
      {
        HANDLE hWait =
          pSwapChain2->GetFrameLatencyWaitableObject ();

        if (hWait != 0)
        {
          WaitForSingleObject (hWait, 500UL);
          CloseHandle         (hWait);
        }
      }
#endif
    }
  }
}

// dear imgui: Renderer Backend for DirectX11
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
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
//  2024-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: DirectX11: Replaced direct access to ImDrawCmd::TextureId with a call to ImDrawCmd::GetTexID(). (will become a requirement)
//  2021-02-18: DirectX11: Change blending equation to preserve alpha in output buffer.
//  2019-08-01: DirectX11: Fixed code querying the Geometry Shader state (would generally error with Debug layer enabled).
//  2019-07-21: DirectX11: Backup, clear and restore Geometry Shader is any is bound when calling ImGui_ImplDX10_RenderDrawData. Clearing Hull/Domain/Compute shaders without backup/restore.
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
extern DWORD SKIF_Util_timeGetTime1                (void);
extern bool  SKIF_Util_IsWindows8Point1OrGreater   (void);
extern bool  SKIF_Util_IsWindows10OrGreater        (void);
extern bool  SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
extern bool  SKIF_Util_IsHDRSupported              (bool refresh = false);
extern bool  SKIF_Util_IsHDRActive                 (bool refresh = false);
extern float SKIF_Util_GetSDRWhiteLevelForHMONITOR (HMONITOR hMonitor);
extern std::vector<HANDLE> vSwapchainWaitHandles;
extern bool  RecreateSwapChains;
extern bool  RecreateSwapChainsPending;

struct ImGui_ImplDX11_ViewportData;

#endif // SKIF_D3D11

// DirectX11 data
struct ImGui_ImplDX11_Data
{
    ID3D11Device*               pd3dDevice;
    ID3D11DeviceContext*        pd3dDeviceContext;
    IDXGIFactory2*              pFactory;
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
    ID3D11SamplerState*         pFontSampler;
    ID3D11ShaderResourceView*   pFontTextureView;
    ID3D11RasterizerState*      pRasterizerState;
    ID3D11BlendState*           pBlendState;
    ID3D11DepthStencilState*    pDepthStencilState;
    int                         VertexBufferSize;
    int                         IndexBufferSize;

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

// Forward Declarations
static void ImGui_ImplDX11_InitPlatformInterface();
static void ImGui_ImplDX11_ShutdownPlatformInterface();

// SKIF CUSTOM Forward Declarations
#ifdef SKIF_D3D11
static void ImGui_ImplDX11_RenderWindow  (ImGuiViewport* viewport, void*);
static void ImGui_ImplDX11_SwapBuffers   (ImGuiViewport* viewport, void*);
static void ImGui_ImplDX11_CreateWindow  (ImGuiViewport* viewport       );
static void ImGui_ImplDX11_DestroyWindow (ImGuiViewport* viewport       );
       void ImGui_ImplDX11_InvalidateDevice (void);
       
static DXGI_FORMAT SKIF_ImplDX11_ViewPort_GetDXGIFormat    (ImGuiViewport* viewport);
static bool        SKIF_ImplDX11_ViewPort_IsHDR            (ImGuiViewport* viewport);
static int         SKIF_ImplDX11_ViewPort_GetHDRMode       (ImGuiViewport* viewport);
static FLOAT       SKIF_ImplDX11_ViewPort_GetSDRWhiteLevel (ImGuiViewport* viewport);
#endif

// Functions
static void ImGui_ImplDX11_SetupRenderState(ImDrawData* draw_data, ID3D11DeviceContext* ctx)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    // Setup viewport
    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(D3D11_VIEWPORT));
    vp.Width = draw_data->DisplaySize.x;
    vp.Height = draw_data->DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0;
    ctx->RSSetViewports(1, &vp);

    // Setup shader and vertex buffers
    unsigned int stride = sizeof(ImDrawVert);
    unsigned int offset = 0;
    ctx->IASetInputLayout(bd->pInputLayout);
    ctx->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    ctx->IASetIndexBuffer(bd->pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(bd->pVertexShader, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    ctx->PSSetShader(bd->pPixelShader, nullptr, 0);
    ctx->PSSetSamplers(0, 1, &bd->pFontSampler);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    ctx->DSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    ctx->CSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..

    // Setup blend state
    const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
    ctx->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    ctx->RSSetState(bd->pRasterizerState);
}

// Render function
#ifndef SKIF_D3D11
void ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ID3D11DeviceContext* ctx = bd->pd3dDeviceContext;

    // Create and grow vertex/index buffers if needed
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC desc;
        memset(&desc, 0, sizeof(D3D11_BUFFER_DESC));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->VertexBufferSize * sizeof(ImDrawVert);
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
        D3D11_BUFFER_DESC desc;
        memset(&desc, 0, sizeof(D3D11_BUFFER_DESC));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->IndexBufferSize * sizeof(ImDrawIdx);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pIB) < 0)
            return;
    }

    // Upload vertex/index data into a single contiguous GPU buffer
    D3D11_MAPPED_SUBRESOURCE vtx_resource, idx_resource;
    if (ctx->Map(bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource) != S_OK)
        return;
    if (ctx->Map(bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource) != S_OK)
        return;
    ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource.pData;
    ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource.pData;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    ctx->Unmap(bd->pVB, 0);
    ctx->Unmap(bd->pIB, 0);

    // Setup orthographic projection matrix into our constant buffer
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    {
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        if (ctx->Map(bd->pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource) != S_OK)
            return;
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
    
        // Defaults
        /*
        constant_buffer->luminance_scale [0] = 1.0f; // x - White Level
        constant_buffer->luminance_scale [1] = 0.0f; // y - isHDR
        constant_buffer->luminance_scale [2] = 0.0f; // z - is10bpc
        constant_buffer->luminance_scale [3] = 0.0f; // w - is16bpc
        */

        ctx->Unmap(bd->pVertexConstantBuffer, 0);
    }

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
    ctx->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
    ctx->RSGetViewports(&old.ViewportsCount, old.Viewports);
    ctx->RSGetState(&old.RS);
    ctx->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
    ctx->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
    ctx->PSGetShaderResources(0, 1, &old.PSShaderResource);
    ctx->PSGetSamplers(0, 1, &old.PSSampler);
    old.PSInstancesCount = old.VSInstancesCount = old.GSInstancesCount = 256;
    ctx->PSGetShader(&old.PS, old.PSInstances, &old.PSInstancesCount);
    ctx->VSGetShader(&old.VS, old.VSInstances, &old.VSInstancesCount);
    ctx->VSGetConstantBuffers(0, 1, &old.VSConstantBuffer);
    ctx->GSGetShader(&old.GS, old.GSInstances, &old.GSInstancesCount);

    ctx->IAGetPrimitiveTopology(&old.PrimitiveTopology);
    ctx->IAGetIndexBuffer(&old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset);
    ctx->IAGetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset);
    ctx->IAGetInputLayout(&old.InputLayout);

    // Setup desired DX state
    ImGui_ImplDX11_SetupRenderState(draw_data, ctx);

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_idx_offset = 0;
    int global_vtx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplDX11_SetupRenderState(draw_data, ctx);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                const D3D11_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                ctx->RSSetScissorRects(1, &r);

                // Bind texture, Draw
                ID3D11ShaderResourceView* texture_srv = (ID3D11ShaderResourceView*)pcmd->GetTexID();
                ctx->PSSetShaderResources(0, 1, &texture_srv);
                ctx->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }

    // Restore modified DX state
    ctx->RSSetScissorRects(old.ScissorRectsCount, old.ScissorRects);
    ctx->RSSetViewports(old.ViewportsCount, old.Viewports);
    ctx->RSSetState(old.RS); if (old.RS) old.RS->Release();
    ctx->OMSetBlendState(old.BlendState, old.BlendFactor, old.SampleMask); if (old.BlendState) old.BlendState->Release();
    ctx->OMSetDepthStencilState(old.DepthStencilState, old.StencilRef); if (old.DepthStencilState) old.DepthStencilState->Release();
    ctx->PSSetShaderResources(0, 1, &old.PSShaderResource); if (old.PSShaderResource) old.PSShaderResource->Release();
    ctx->PSSetSamplers(0, 1, &old.PSSampler); if (old.PSSampler) old.PSSampler->Release();
    ctx->PSSetShader(old.PS, old.PSInstances, old.PSInstancesCount); if (old.PS) old.PS->Release();
    for (UINT i = 0; i < old.PSInstancesCount; i++) if (old.PSInstances[i]) old.PSInstances[i]->Release();
    ctx->VSSetShader(old.VS, old.VSInstances, old.VSInstancesCount); if (old.VS) old.VS->Release();
    ctx->VSSetConstantBuffers(0, 1, &old.VSConstantBuffer); if (old.VSConstantBuffer) old.VSConstantBuffer->Release();
    ctx->GSSetShader(old.GS, old.GSInstances, old.GSInstancesCount); if (old.GS) old.GS->Release();
    for (UINT i = 0; i < old.VSInstancesCount; i++) if (old.VSInstances[i]) old.VSInstances[i]->Release();
    ctx->IASetPrimitiveTopology(old.PrimitiveTopology);
    ctx->IASetIndexBuffer(old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset); if (old.IndexBuffer) old.IndexBuffer->Release();
    ctx->IASetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset); if (old.VertexBuffer) old.VertexBuffer->Release();
    ctx->IASetInputLayout(old.InputLayout); if (old.InputLayout) old.InputLayout->Release();
}
#else
//#if 0
void ImGui_ImplDX11_RenderDrawData (ImDrawData *draw_data)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  // Avoid rendering when minimized
  if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
      return;

  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
  ID3D11DeviceContext* ctx = bd->pd3dDeviceContext;

  // Create and grow vertex/index buffers if needed
  if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
  {
    bd->pVB = nullptr;

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
    bd->pIB = nullptr;

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
    return;

  ImDrawVert *vtx_dst =
    static_cast <ImDrawVert *> (vtx_resource.pData);
  ImDrawIdx  *idx_dst =
    static_cast <ImDrawIdx  *> (idx_resource.pData);

  for (int n = 0; n < draw_data->CmdListsCount; n++)
  {
    const ImDrawList *cmd_list =
      draw_data->CmdLists [n];

    memcpy ( vtx_dst, cmd_list->VtxBuffer.Data,
                      cmd_list->VtxBuffer.Size * sizeof (ImDrawVert) );
    memcpy ( idx_dst, cmd_list->IdxBuffer.Data,
                      cmd_list->IdxBuffer.Size * sizeof (ImDrawIdx)  );

    vtx_dst += cmd_list->VtxBuffer.Size;
    idx_dst += cmd_list->IdxBuffer.Size;
  }

  ctx->Unmap (bd->pVB, 0);
  ctx->Unmap (bd->pIB, 0);

  // Setup orthographic projection matrix into our constant buffer
  // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
  {
    D3D11_MAPPED_SUBRESOURCE
          mapped_resource = { };

    if (FAILED (ctx->Map (bd->pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
      return;

    VERTEX_CONSTANT_BUFFER_DX11 *constant_buffer =
        static_cast <VERTEX_CONSTANT_BUFFER_DX11 *> (
                              mapped_resource.pData
        );

    // Assert that the constant buffer remains 16-byte aligned.
    static_assert((sizeof(VERTEX_CONSTANT_BUFFER_DX11) % 16) == 0, "Constant Buffer size must be 16-byte aligned");

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

    float mvp [4][4] = {
      {    2.0f   / ( R - L ),     0.0f,               0.0f, 0.0f },
      {    0.0f,                   2.0f   / ( T - B ), 0.0f, 0.0f },
      {    0.0f,                   0.0f,               0.5f, 0.0f },
      { ( R + L ) / ( L - R ),  ( T + B ) / ( B - T ), 0.5f, 1.0f } };

    memcpy ( &constant_buffer->mvp,
                               mvp,
                       sizeof (mvp) );
    
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
          constant_buffer->luminance_scale [0] =       (_registry.iHDRBrightness / 80.0f); // Org: data->SKIF_GetHDRWhiteLuma    ( ) / 80.0f
          constant_buffer->luminance_scale [3] = 1.0f;
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
        constant_buffer->luminance_scale [3] = 1.0f;
      }

      else if (SKIF_ImplDX11_ViewPort_GetDXGIFormat (vp) == DXGI_FORMAT_R10G10B10A2_UNORM)
      {
        // SDR 10 bpc on SDR display
        constant_buffer->luminance_scale [2] = 1.0f;
      }
    }

    ctx->Unmap ( bd->pVertexConstantBuffer, 0 );

    if (FAILED (ctx->Map (bd->pFontConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
      return;

    PIXEL_CONSTANT_BUFFER_DX11 *pix_constant_buffer =
        static_cast <PIXEL_CONSTANT_BUFFER_DX11 *> (
                              mapped_resource.pData
        );

    // Assert that the constant buffer remains 16-byte aligned.
    static_assert((sizeof(PIXEL_CONSTANT_BUFFER_DX11) % 16) == 0, "Constant Buffer size must be 16-byte aligned");

    pix_constant_buffer->font_dims [0] = (float)ImGui::GetIO ().Fonts->TexWidth;
    pix_constant_buffer->font_dims [1] = (float)ImGui::GetIO ().Fonts->TexHeight;

    ctx->Unmap ( bd->pFontConstantBuffer, 0 );

    if (FAILED (ctx->Map (bd->pPixelConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
      return;

    pix_constant_buffer =
      static_cast <PIXEL_CONSTANT_BUFFER_DX11 *> (
        mapped_resource.pData
      );

    // Assert that the constant buffer remains 16-byte aligned.
    static_assert((sizeof(PIXEL_CONSTANT_BUFFER_DX11) % 16) == 0, "Constant Buffer size must be 16-byte aligned");

    pix_constant_buffer->font_dims [0] = 0.0f;
    pix_constant_buffer->font_dims [1] = 0.0f;

    ctx->Unmap ( bd->pPixelConstantBuffer, 0 );
  }

  // Setup desired DX state
  ImGui_ImplDX11_SetupRenderState (draw_data, ctx);

  // Render command lists
  // (Because we merged all buffers into a single one, we maintain our own offset into them)
  int global_idx_offset = 0;
  int global_vtx_offset = 0;

  ImVec2 clip_off =
    draw_data->DisplayPos;

  for (int n = 0; n < draw_data->CmdListsCount; n++)
  {
    const ImDrawList *cmd_list =
      draw_data->CmdLists [n];

    for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
    {
      ctx->PSSetConstantBuffers ( 0, 1, &bd->pPixelConstantBuffer );

      const ImDrawCmd *pcmd =
        &cmd_list->CmdBuffer [cmd_i];

      if (pcmd->UserCallback != nullptr)
      {
        // User callback, registered via ImDrawList::AddCallback()
        // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
        if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
          ImGui_ImplDX11_SetupRenderState (draw_data, ctx);
        else
          pcmd->UserCallback (cmd_list, pcmd);
      }

      else
      {
        // Apply scissor/clipping rectangle
        const D3D11_RECT r =
          { (LONG)( pcmd->ClipRect.x - clip_off.x ),
            (LONG)( pcmd->ClipRect.y - clip_off.y ),
            (LONG)( pcmd->ClipRect.z - clip_off.x ),
            (LONG)( pcmd->ClipRect.w - clip_off.y ) };

        ctx->RSSetScissorRects (1, &r);

        // Bind texture, Draw
        ID3D11ShaderResourceView *texture_srv =
          static_cast <ID3D11ShaderResourceView *> (
                                    pcmd->TextureId
          );

        if (pcmd->TextureId == ImGui::GetIO ().Fonts->TexID)
        ctx->PSSetConstantBuffers ( 0, 1, &bd->pFontConstantBuffer );
        ctx->PSSetShaderResources ( 0, 1, &texture_srv);
        ctx->DrawIndexed          ( pcmd->ElemCount,
                                    pcmd->IdxOffset + global_idx_offset,
                                    pcmd->VtxOffset + global_vtx_offset );
      }
    }

    global_idx_offset += cmd_list->IdxBuffer.Size;
    global_vtx_offset += cmd_list->VtxBuffer.Size;
  }
}

#endif // !SKIF_D3D11

#ifndef SKIF_D3D11
static void ImGui_ImplDX11_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Upload texture to graphics system
    {
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D* pTexture = nullptr;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = pixels;
        subResource.SysMemPitch = desc.Width * 4;
        subResource.SysMemSlicePitch = 0;
        bd->pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
        IM_ASSERT(pTexture != nullptr);

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        bd->pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &bd->pFontTextureView);
        pTexture->Release();
    }

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)bd->pFontTextureView);

    // Create texture sampler
    // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
    {
        D3D11_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MipLODBias = 0.f;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.MinLOD = 0.f;
        desc.MaxLOD = 0.f;
        bd->pd3dDevice->CreateSamplerState(&desc, &bd->pFontSampler);
    }
}
#else
static void ImGui_ImplDX11_CreateFontsTexture()
{
  // Build texture atlas
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

  unsigned char* pixels = nullptr;
  int            width  = 0,
                  height = 0;

  if (io.Fonts->TexPixelsAlpha8 == NULL)
  {
    DWORD temp_time = SKIF_Util_timeGetTime1();
    io.Fonts->Build ( );
    PLOG_DEBUG << "Operation [Fonts->Build] took " << (SKIF_Util_timeGetTime1() - temp_time) << " ms.";
  }

  io.Fonts->GetTexDataAsAlpha8 ( &pixels,
                                  &width, &height );

  D3D_FEATURE_LEVEL  featureLevel =
    bd->pd3dDevice->GetFeatureLevel ();

  extern bool failedLoadFonts;

  switch (featureLevel)
  {
    case D3D_FEATURE_LEVEL_10_0:
    case D3D_FEATURE_LEVEL_10_1:
    if (width > 8192 || height > 8192) // Warn User
      failedLoadFonts = true;
      width  = std::min (8192, width);
      height = std::min (8192, height);
      // Max Texture Resolution = 8192x8192
      break;
    case D3D_FEATURE_LEVEL_11_0:
    case D3D_FEATURE_LEVEL_11_1:
    if (width > 16384 || height > 16384) // Warn User
      failedLoadFonts = true;
      width  = std::min (16384, width);
      height = std::min (16384, height);
      // Max Texture Resolution = 16384X16384
      break;
  }

  // Upload texture to graphics system
  D3D11_TEXTURE2D_DESC
    staging_desc                  = { };
    staging_desc.Width            = width;
    staging_desc.Height           = height;
    staging_desc.MipLevels        = 1;
    staging_desc.ArraySize        = 1;
    staging_desc.Format           = DXGI_FORMAT_A8_UNORM;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage            = D3D11_USAGE_STAGING;
    staging_desc.BindFlags        = 0;
    staging_desc.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;

  D3D11_TEXTURE2D_DESC
    tex_desc                      = staging_desc;
    tex_desc.Usage                = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;
    tex_desc.CPUAccessFlags       = 0;

  CComPtr <ID3D11Texture2D>       pStagingTexture = nullptr;
  CComPtr <ID3D11Texture2D>       pFontTexture    = nullptr;

  // These two CreateTexture2D are extremely costly operations
  //   on a VMware-based virtual Windows 7 machine.  VMware bug?
  DWORD temp_time = SKIF_Util_timeGetTime1();

  if (FAILED (bd->pd3dDevice->CreateTexture2D (&staging_desc, nullptr, &pStagingTexture.p)))
    PLOG_ERROR << "Failed creating staging texture!";

  if (FAILED (bd->pd3dDevice->CreateTexture2D (&tex_desc, nullptr, &pFontTexture.p)))
    PLOG_ERROR << "Failed creating font texture!";

  PLOG_DEBUG << "Operation [CreateTexture2D] took " << (SKIF_Util_timeGetTime1() - temp_time) << " ms.";

  CComPtr   <ID3D11DeviceContext> pDevCtx;
  bd->pd3dDevice->GetImmediateContext     (&pDevCtx);

  D3D11_MAPPED_SUBRESOURCE
        mapped_tex = { };

  if (FAILED (pDevCtx->Map (pStagingTexture.p, 0, D3D11_MAP_WRITE, 0, &mapped_tex)))
    PLOG_ERROR << "Failed mapping staging texture!";

  for (int y = 0; y < height; y++)
  {
    ImU8  *pDst =
      (ImU8 *)((uintptr_t)mapped_tex.pData +
                          mapped_tex.RowPitch * y);
    ImU8  *pSrc =              pixels + width * y;

    for (int x = 0; x < width; x++)
    {
      *pDst++ =
        *pSrc++;
    }
  }

  pDevCtx->Unmap        ( pStagingTexture, 0 );
  pDevCtx->CopyResource (    pFontTexture,
                          pStagingTexture    );

  // Create texture view
  D3D11_SHADER_RESOURCE_VIEW_DESC
    srvDesc = { };
    srvDesc.Format                    = DXGI_FORMAT_A8_UNORM;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = tex_desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

  if (FAILED (bd->pd3dDevice->CreateShaderResourceView (pFontTexture, &srvDesc, &bd->pFontTextureView)))
    PLOG_ERROR << "Failed creating SRV of the font texture!";

  // Store our identifier
  io.Fonts->TexID =
    bd->pFontTextureView;

  // Create texture sampler
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

  if (FAILED (bd->pd3dDevice->CreateSamplerState (&sampler_desc, &bd->pFontSampler)))
    PLOG_ERROR << "Failed creating sampler-state object of the font sampler!";

  io.Fonts->ClearTexData ();
}

#endif // !SKIF_D3D11

#ifndef SKIF_D3D11
bool    ImGui_ImplDX11_CreateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return false;
    if (bd->pFontSampler)
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
              output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
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
            D3D11_BUFFER_DESC desc;
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

    ImGui_ImplDX11_CreateFontsTexture();

    return true;
}
#else
bool ImGui_ImplDX11_CreateDeviceObjects (void)
{

  ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
  if (!bd->pd3dDevice)
      return false;
  if (bd->pFontSampler)
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
#ifdef SKIF_ImDrawVert
      { "COLOR",
          0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
            offsetof (ImDrawVert,      col),
                D3D11_INPUT_PER_VERTEX_DATA, 0 }
#else
      { "COLOR",
          0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
            offsetof (ImDrawVert,      col),
                D3D11_INPUT_PER_VERTEX_DATA, 0 }

#endif // SKIF_ImDrawVert
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

  ImGui_ImplDX11_CreateFontsTexture ();

  return true;
}

#endif // !SKIF_D3D11

void    ImGui_ImplDX11_InvalidateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return;

    if (bd->pFontSampler)           { bd->pFontSampler->Release(); bd->pFontSampler = nullptr; }
    if (bd->pFontTextureView)       { bd->pFontTextureView->Release(); bd->pFontTextureView = nullptr; ImGui::GetIO().Fonts->SetTexID(0); } // We copied data->pFontTextureView to io.Fonts->TexID so let's clear that as well.
    if (bd->pFontConstantBuffer)    { bd->pFontConstantBuffer->Release(); bd->pFontConstantBuffer = nullptr; }   // SKIF CUSTOM
    if (bd->pIB)                    { bd->pIB->Release(); bd->pIB = nullptr; }
    if (bd->pVB)                    { bd->pVB->Release(); bd->pVB = nullptr; }
    if (bd->pBlendState)            { bd->pBlendState->Release(); bd->pBlendState = nullptr; }
    if (bd->pDepthStencilState)     { bd->pDepthStencilState->Release(); bd->pDepthStencilState = nullptr; }
    if (bd->pRasterizerState)       { bd->pRasterizerState->Release(); bd->pRasterizerState = nullptr; }
    if (bd->pPixelConstantBuffer)   { bd->pPixelConstantBuffer->Release(); bd->pPixelConstantBuffer = nullptr; } // SKIF CUSTOM
    if (bd->pPixelShader)           { bd->pPixelShader->Release(); bd->pPixelShader = nullptr; }
    if (bd->pVertexConstantBuffer)  { bd->pVertexConstantBuffer->Release(); bd->pVertexConstantBuffer = nullptr; }
    if (bd->pInputLayout)           { bd->pInputLayout->Release(); bd->pInputLayout = nullptr; }
    if (bd->pVertexShader)          { bd->pVertexShader->Release(); bd->pVertexShader = nullptr; }
}

bool    ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplDX11_Data* bd = IM_NEW(ImGui_ImplDX11_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_dx11";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)

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

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        ImGui_ImplDX11_InitPlatformInterface();

    return true;
}

void ImGui_ImplDX11_Shutdown()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplDX11_ShutdownPlatformInterface();
    ImGui_ImplDX11_InvalidateDeviceObjects();
    if (bd->pFactory)             { bd->pFactory->Release(); }
    if (bd->pd3dDevice)           { bd->pd3dDevice->Release(); }
    if (bd->pd3dDeviceContext)    { bd->pd3dDeviceContext->Release(); }
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);
    IM_DELETE(bd);
}

#ifndef SKIF_D3D11
void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    if (!bd->pFontSampler)
        ImGui_ImplDX11_CreateDeviceObjects();
}
#else
void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

    ImGuiContext& g = *ImGui::GetCurrentContext();

    // External declarations
    extern bool CreateDeviceD3D    (HWND hWnd);
    extern void CleanupDeviceD3D   (void);
    extern HWND                    SKIF_Notify_hWnd;
    extern ID3D11Device*           SKIF_pd3dDevice;
    extern ID3D11DeviceContext*    SKIF_pd3dDeviceContext;
    extern DWORD                   invalidatedDevice;

    // Check if the device have been removed for any reason
    bool  RecreateDevice  =
      FAILED (bd->pd3dDevice->GetDeviceRemovedReason ( ));

    // Only bother checking if the factory needs to be recreated if the device hasn't been removed
    bool  RecreateFactory =
                ( ! RecreateDevice          &&
                    bd->pFactory != nullptr &&
                  ! bd->pFactory->IsCurrent ( ) );

    RecreateSwapChainsPending = false;

    if (RecreateSwapChains ||
        RecreateFactory    ||
        RecreateDevice     )
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

      if (RecreateFactory || RecreateDevice)
      {
        PLOG_DEBUG << "Recreating factory...";
        bd->pFactory->Release();
        bd->pFactory = nullptr;

        if (! RecreateDevice)
          CreateDXGIFactory1 (__uuidof (IDXGIFactory2), (void **)&bd->pFactory);
      }

      if (RecreateDevice)
      {
        PLOG_DEBUG << "Recreating the D3D11 device...";
        ImGui_ImplDX11_InvalidateDevice ( );
        CleanupDeviceD3D                ( );

        // At this point all traces of the previous device should have been cleared

        if (CreateDeviceD3D             (SKIF_Notify_hWnd))                        // This creates a new device and context
          ImGui_ImplDX11_Init           (SKIF_pd3dDevice, SKIF_pd3dDeviceContext); // This creates a new factory

        // This is used to flag that rendering should not occur until
        // any loaded textures and such also have been unloaded
        invalidatedDevice = 1;
      }

      _registry._RendererCanHDR = SKIF_Util_IsHDRActive (true);
    
      PLOG_DEBUG << "Recreating any necessary swapchains and their wait objects...";
      for (int i = 0; i < g.Viewports.Size; i++)
        ImGui_ImplDX11_CreateWindow  (g.Viewports [i]);
    }

    if (!bd->pFontSampler)
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
    int                     HDRMode;       // 0 = No HDR,  1 = 10 bpc HDR,  2 = 16 bpc scRGB HDR
    bool                    HDR;
    FLOAT                   HDRLuma;
    FLOAT                   HDRMinLuma;
    DXGI_OUTPUT_DESC1       DXGIDesc;
    DXGI_FORMAT             DXGIFormat;

     ImGui_ImplDX11_ViewportData (void) {            SwapChain  = nullptr;   RTView  = nullptr;   WaitHandle  = 0;  PresentCount = 0; SDRMode = 0; SDRWhiteLevel = 80.0f; HDRMode = 0; HDR = false; HDRLuma = 0.0f; HDRMinLuma = 0.0f; DXGIDesc = {   }; DXGIFormat = DXGI_FORMAT_UNKNOWN; }
    ~ImGui_ImplDX11_ViewportData (void) { IM_ASSERT (SwapChain == nullptr && RTView == nullptr && WaitHandle == 0); }
};
#endif

#ifndef SKIF_D3D11
static void ImGui_ImplDX11_CreateWindow(ImGuiViewport* viewport)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ImGui_ImplDX11_ViewportData* vd = IM_NEW(ImGui_ImplDX11_ViewportData)();
    viewport->RendererUserData = vd;

    // PlatformHandleRaw should always be a HWND, whereas PlatformHandle might be a higher-level handle (e.g. GLFWWindow*, SDL_Window*).
    // Some backends will leave PlatformHandleRaw == 0, in which case we assume PlatformHandle will contain the HWND.
    HWND hwnd = viewport->PlatformHandleRaw ? (HWND)viewport->PlatformHandleRaw : (HWND)viewport->PlatformHandle;
    IM_ASSERT(hwnd != 0);

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferDesc.Width = (UINT)viewport->Size.x;
    sd.BufferDesc.Height = (UINT)viewport->Size.y;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;

    IM_ASSERT(vd->SwapChain == nullptr && vd->RTView == nullptr);
    bd->pFactory->CreateSwapChain(bd->pd3dDevice, &sd, &vd->SwapChain);

    // Create the render target
    if (vd->SwapChain)
    {
        ID3D11Texture2D* pBackBuffer;
        vd->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        bd->pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &vd->RTView);
        pBackBuffer->Release();
    }
}

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
        PLOG_ERROR << "ImGui_ImplDX11_SetWindowSize() failed creating buffers";
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
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bd->pd3dDeviceContext->OMSetRenderTargets(1, &vd->RTView, nullptr);
    if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear))
        bd->pd3dDeviceContext->ClearRenderTargetView(vd->RTView, (float*)&clear_color);
    ImGui_ImplDX11_RenderDrawData(viewport->DrawData);
}

static void ImGui_ImplDX11_SwapBuffers(ImGuiViewport* viewport, void*)
{
    ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData;
    vd->SwapChain->Present(0, 0); // Present without vsync
}
#endif

static void ImGui_ImplDX11_InitPlatformInterface()
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_CreateWindow = ImGui_ImplDX11_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGui_ImplDX11_DestroyWindow;
    platform_io.Renderer_SetWindowSize = ImGui_ImplDX11_SetWindowSize;
    platform_io.Renderer_RenderWindow = ImGui_ImplDX11_RenderWindow;
    platform_io.Renderer_SwapBuffers = ImGui_ImplDX11_SwapBuffers;
}

static void ImGui_ImplDX11_ShutdownPlatformInterface()
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

    PLOG_INFO   << "The swapchain format has been changed..."
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

    UINT PresentFlags = 0x0;

    if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD   ||
        swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL )
    {
      PresentFlags   = DXGI_PRESENT_RESTART;

      // If V-Sync is OFF (not possible at all, but keep for possible future use)
      if (Interval == 0 && _registry._RendererCanAllowTearing)
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    //if (vd->WaitHandle)
    //  WaitForSingleObject (vd->WaitHandle, INFINITE);

    if (SUCCEEDED (vd->SwapChain->Present (Interval, PresentFlags)))
    {
      vd->PresentCount++;

      static bool runOnce = true;
      if (runOnce)
      {
        runOnce = false;
        extern DWORD SKIF_startupTime;
        extern DWORD SKIF_Util_timeGetTime1 (void);

        DWORD current_time = SKIF_Util_timeGetTime1();

        PLOG_INFO << "Presented first frame! Init -> Present took " << (current_time - SKIF_startupTime) << " ms.";
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
    
          vd->SDRWhiteLevel = SKIF_Util_GetSDRWhiteLevelForHMONITOR (vd->DXGIDesc.Monitor);

  #pragma region Enable HDR
          // DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709    - SDR display with no Advanced Color capabilities
          // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709    - Standard definition for scRGB, and is usually used with 16 bit integer, 16 bit floating point, or 32 bit floating point color channels.
          // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 - HDR display with all Advanced Color capabilities

          if (_registry._RendererCanHDR          && // Does the system support HDR?
              _registry.iHDRMode  > 0) // HDR support is not disabled, is it?
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
              pOutput6->GetDesc1 (&vd->DXGIDesc);

              // Is the output display in HDR mode?
              if (vd->DXGIDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
              {
                vd->HDR     = true;
                vd->HDRMode = _registry.iHDRMode;

                pSwapChain3->SetColorSpace1 (dxgi_cst);

                pOutput6->GetDesc1 (&vd->DXGIDesc);

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
          vd->WaitHandle =
            pSwapChain2->GetFrameLatencyWaitableObject  ( );

          if (vd->WaitHandle)
          {
            vSwapchainWaitHandles.push_back (vd->WaitHandle);

            // One-time wait to align the thread for minimum latency (reduces latency by half in testing)
            WaitForSingleObjectEx (vd->WaitHandle, 1000, true);
            //WaitForSingleObject (vd->WaitHandle, 1000);
            // Block this thread until the swap chain is ready for presenting. Note that it is
            // important to call this before the first Present in order to minimize the latency
            // of the swap chain.
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
          if (! vSwapchainWaitHandles.empty())
            vSwapchainWaitHandles.erase(std::remove(vSwapchainWaitHandles.begin(), vSwapchainWaitHandles.end(), vd->WaitHandle), vSwapchainWaitHandles.end());

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

static bool SKIF_ImplDX11_ViewPort_IsHDR(ImGuiViewport* viewport)
{
    if (ImGui_ImplDX11_ViewportData* vd = (ImGui_ImplDX11_ViewportData*)viewport->RendererUserData)
        return vd->HDRMode;

    return 0;
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
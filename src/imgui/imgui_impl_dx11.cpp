// dear imgui: Renderer Backend for DirectX11
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
//  [X] Renderer: Multi-viewport support (multiple windows). Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2023-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
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
#include "imgui/imgui_impl_dx11.h"

// DirectX
#include <stdio.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#endif

#define SKIF_CUSTOM

// DirectX11 data
struct ImGui_ImplDX11_Data
{
    ID3D11Device*               pd3dDevice;
    ID3D11DeviceContext*        pd3dDeviceContext;
    IDXGIFactory*               pFactory;
    ID3D11Buffer*               pVB;
    ID3D11Buffer*               pIB;
    ID3D11VertexShader*         pVertexShader;
    ID3D11InputLayout*          pInputLayout;
    ID3D11Buffer*               pVertexConstantBuffer;
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

struct VERTEX_CONSTANT_BUFFER_DX11
{
    float   mvp[4][4];
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplDX11_Data* ImGui_ImplDX11_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// Forward Declarations
static void ImGui_ImplDX11_InitPlatformInterface();
static void ImGui_ImplDX11_ShutdownPlatformInterface();
#pragma region SKIF_CUSTOM
static void SKIF_ImGui_ImplDX11_CreateWindow  (ImGuiViewport* viewport);
static void SKIF_ImGui_ImplDX11_DestroyWindow (ImGuiViewport* viewport);
static void SKIF_ImGui_ImplDX11_SetWindowSize (ImGuiViewport* viewport, ImVec2 size);
static void SKIF_ImGui_ImplDX11_RenderWindow  (ImGuiViewport* viewport, void*);
static void SKIF_ImGui_ImplDX11_SwapBuffers   (ImGuiViewport* viewport, void*);
#pragma endregion

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
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)IM_OFFSETOF(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)IM_OFFSETOF(ImDrawVert, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)IM_OFFSETOF(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

void    ImGui_ImplDX11_InvalidateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return;

    if (bd->pFontSampler)           { bd->pFontSampler->Release(); bd->pFontSampler = nullptr; }
    if (bd->pFontTextureView)       { bd->pFontTextureView->Release(); bd->pFontTextureView = nullptr; ImGui::GetIO().Fonts->SetTexID(0); } // We copied data->pFontTextureView to io.Fonts->TexID so let's clear that as well.
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
    IDXGIFactory* pFactory = nullptr;

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

void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplDX11_Init()?");

    if (!bd->pFontSampler)
        ImGui_ImplDX11_CreateDeviceObjects();
}

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
struct ImGui_ImplDX11_ViewportData
{
    IDXGISwapChain*                 SwapChain;
    ID3D11RenderTargetView*         RTView;

    ImGui_ImplDX11_ViewportData()   { SwapChain = nullptr; RTView = nullptr; }
    ~ImGui_ImplDX11_ViewportData()  { IM_ASSERT(SwapChain == nullptr && RTView == nullptr); }
};

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
    vd->SwapChain->Present(1, 0); // Present without vsync
}

static void ImGui_ImplDX11_InitPlatformInterface()
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
#ifdef SKIF_CUSTOM
    platform_io.Renderer_CreateWindow = SKIF_ImGui_ImplDX11_CreateWindow;
    platform_io.Renderer_DestroyWindow = SKIF_ImGui_ImplDX11_DestroyWindow;
    platform_io.Renderer_SetWindowSize = SKIF_ImGui_ImplDX11_SetWindowSize;
    platform_io.Renderer_RenderWindow = SKIF_ImGui_ImplDX11_RenderWindow;
    platform_io.Renderer_SwapBuffers = SKIF_ImGui_ImplDX11_SwapBuffers;
#else
    platform_io.Renderer_CreateWindow = ImGui_ImplDX11_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGui_ImplDX11_DestroyWindow;
    platform_io.Renderer_SetWindowSize = ImGui_ImplDX11_SetWindowSize;
    platform_io.Renderer_RenderWindow = ImGui_ImplDX11_RenderWindow;
    platform_io.Renderer_SwapBuffers = ImGui_ImplDX11_SwapBuffers;
#endif
}

static void ImGui_ImplDX11_ShutdownPlatformInterface()
{
    ImGui::DestroyPlatformWindows();
}



//---------------------------------------------------------------------------------------------------------
// CUSTOM SKIF STUFF
//---------------------------------------------------------------------------------------------------------
//
// The intention is to keep as few changes as possible from the original source code, to facilitate easier
//   updates to newer versions of ImGui.
//

// Includes
#include <atlbase.h>
// DirectX
#include <dxgi.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <d3d11.h>
#include <format>

// PLOG
#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"

// External Variables
extern bool SKIF_bCanAllowTearing;
extern bool SKIF_bCanFlip;
extern bool SKIF_bCanFlipDiscard;
extern bool SKIF_bCanWaitSwapchain;
extern bool SKIF_bCanHDR;
extern std::vector<HANDLE> vSwapchainWaitHandles;

// External functions
extern bool SKIF_Util_IsWindows8Point1OrGreater   (void);
extern bool SKIF_Util_IsWindows10OrGreater        (void);
extern bool SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
extern bool SKIF_Util_IsHDRSupported              (bool refresh = false);
extern int  SKIF_Util_GetSDRWhiteLevelForHMONITOR (HMONITOR hMonitor);

// Registry Settings
#include <registry.h>

static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance( );

// Objects

struct SKIF_ImGui_ImplDX11_ViewportData {
  IDXGISwapChain1        *SwapChain;
  ID3D11RenderTargetView *RTView;
  UINT                    PresentCount;
  HANDLE                  WaitHandle;
  int                     SDRMode;
  int                     SDRWhiteLevel; // SDR white level in nits for the display
  int                     HDRMode;
  bool                    HDR;
  FLOAT                   HDRLuma;
  FLOAT                   HDRMinLuma;
  DXGI_OUTPUT_DESC1       DXGIDesc;
  DXGI_FORMAT             DXGIFormat;

   SKIF_ImGui_ImplDX11_ViewportData (void) {            SwapChain  = nullptr;   RTView  = nullptr;   WaitHandle  = 0;  PresentCount = 0; SDRMode = 0; SDRWhiteLevel = 80; HDRMode = 0; HDR = false; HDRLuma = 0.0f; HDRMinLuma = 0.0f; DXGIDesc = {   }; DXGIFormat = DXGI_FORMAT_UNKNOWN; }
  ~SKIF_ImGui_ImplDX11_ViewportData (void) { IM_ASSERT (SwapChain == nullptr && RTView == nullptr && WaitHandle == 0);                                }

  FLOAT SKIF_GetMaxHDRLuminance (bool bAllowLocalRange)
  {
    if (! HDR)
      return 0.0f;

    return
      bAllowLocalRange ? DXGIDesc.MaxLuminance
                       : DXGIDesc.MaxFullFrameLuminance;
  }

  FLOAT SKIF_GetMinHDRLuminance (void)
  {
    if (! HDR && SDRWhiteLevel == 80)
      return 0.0f;

    if (         DXGIDesc.MinLuminance > DXGIDesc.MaxFullFrameLuminance)
      std::swap (DXGIDesc.MinLuminance,  DXGIDesc.MaxFullFrameLuminance);

    return
      DXGIDesc.MinLuminance;
  }

  void  SKIF_SetHDRWhiteLuma (float fLuma)
  {
    HDRLuma = fLuma;
  }

  FLOAT SKIF_GetHDRWhiteLuma (void)
  {
    if (HDRLuma == 0.0f)
    {
      SKIF_SetHDRWhiteLuma ( 
        SKIF_GetMaxHDRLuminance (false) / 2.0f
      );
    }

    return HDRLuma;
  }
};

// Functions
static void
SKIF_ImGui_ImplDX11_CreateWindow  (ImGuiViewport* viewport)
{
  ImGui_ImplDX11_Data* bd =
    ImGui_ImplDX11_GetBackendData ( );

  SKIF_ImGui_ImplDX11_ViewportData *data =
    IM_NEW (SKIF_ImGui_ImplDX11_ViewportData)( );

  viewport->RendererUserData = data;

  // DXGI WARNING: IDXGIFactory::CreateSwapChain/IDXGISwapChain::ResizeBuffers: The buffer width  inferred from the output window is zero. Taking 8 as a reasonable default instead [ MISCELLANEOUS WARNING #1: ]
  // DXGI WARNING: IDXGIFactory::CreateSwapChain/IDXGISwapChain::ResizeBuffers: The buffer height inferred from the output window is zero. Taking 8 as a reasonable default instead [ MISCELLANEOUS WARNING #2: ]
  if (viewport->Size.x == 0.0f || viewport->Size.y == 0.0f)
  {
    //PLOG_WARNING << "DXGI WARNING: IDXGIFactory::CreateSwapChain/IDXGISwapChain::ResizeBuffers: The buffer height/width inferred from the output window is zero.";
    return;
  }

  // PlatformHandleRaw should always be a HWND, whereas PlatformHandle might be a higher-level handle (e.g. GLFWWindow*, SDL_Window*).
  // Some back-end will leave PlatformHandleRaw NULL, in which case we assume PlatformHandle will contain the HWND.
  HWND hWnd =
    ( viewport->PlatformHandleRaw != nullptr ?
        (HWND)viewport->PlatformHandleRaw    :
        (HWND)viewport->PlatformHandle );

  //IM_ASSERT (hWnd != nullptr);

  // DXGI ERROR: IDXGIFactory::CreateSwapChain: No target window specified in DXGI_SWAP_CHAIN_DESC, and no window associated with owning factory. [ MISCELLANEOUS ERROR #6: ]
  if (hWnd == nullptr)
  {
    //PLOG_ERROR << "DXGI ERROR: IDXGIFactory::CreateSwapChain: No target window specified in DXGI_SWAP_CHAIN_DESC, and no window associated with owning factory.";
    return;
  }

  CComQIPtr <IDXGIFactory2>
                 pFactory2
              (bd->pFactory);

  IM_ASSERT ( data->SwapChain == nullptr &&
              data->RTView    == nullptr );

  DXGI_FORMAT dxgi_format;

  // HDR formats
  if (SKIF_bCanHDR && _registry.iHDRMode > 0)
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
      dxgi_format = DXGI_FORMAT_R10G10B10A2_UNORM;  // 10 bpc (apparently only supported for flip on Win10 1709+
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
  swap_desc.Flags              = 0x0;
  swap_desc.SampleDesc.Count   = 1;
  swap_desc.SampleDesc.Quality = 0;

  // Assume flip by default
  swap_desc.BufferCount  = 3; // Must be 2-16 for flip model

  if (SKIF_bCanWaitSwapchain)
    swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  if (SKIF_bCanAllowTearing)
    swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;



  for (auto  _swapEffect : {DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_EFFECT_DISCARD})
  {
    swap_desc.SwapEffect = _swapEffect;

    // In case flip failed, fall back to using BitBlt
    if (_swapEffect == DXGI_SWAP_EFFECT_DISCARD)
    {
      swap_desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
      swap_desc.BufferCount = 1;
      swap_desc.Flags       = 0x0;
      SKIF_bCanFlip         = false;
      SKIF_bCanHDR          = false;
    }

    if (SUCCEEDED (pFactory2->CreateSwapChainForHwnd ( bd->pd3dDevice, hWnd, &swap_desc, NULL, NULL,
                              &data->SwapChain ))) break;
  }

  // Do we have a swapchain?
  if (data->SwapChain != nullptr)
  {
    data->DXGIFormat = swap_desc.Format;

    // Retrieve the current default adapter.
    CComPtr   <IDXGIOutput>   pOutput;
    CComPtr   <IDXGIAdapter1> pAdapter;
    
    pFactory2->EnumAdapters1 (0, &pAdapter);

    auto _ComputeIntersectionArea =
          [&](long ax1, long ay1, long ax2, long ay2,
              long bx1, long by1, long bx2, long by2) -> int
    {
      return std::max(0l, std::min(ax2, bx2) - std::max(ax1, bx1)) * std::max(0l, std::min(ay2, by2) - std::max(ay1, by1));
    };

    UINT i = 0;
    CComPtr <IDXGIOutput> currentOutput;
    float bestIntersectArea = -1;

    RECT m_windowBounds;
    GetWindowRect (hWnd, &m_windowBounds);
    
    // Iterate through the DXGI outputs associated with the DXGI adapter,
    // and find the output whose bounds have the greatest overlap with the
    // app window (i.e. the output for which the intersection area is the
    // greatest).
    while (pAdapter->EnumOutputs(i, &currentOutput) != DXGI_ERROR_NOT_FOUND)
    {
      // Get the retangle bounds of the app window
      int ax1 = m_windowBounds.left;
      int ay1 = m_windowBounds.top;
      int ax2 = m_windowBounds.right;
      int ay2 = m_windowBounds.bottom;

      // Get the rectangle bounds of current output
      DXGI_OUTPUT_DESC desc;
      currentOutput->GetDesc(&desc);

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

    // Having determined the output (display) upon which the app is primarily being 
    // rendered, retrieve the HDR capabilities of that display by checking the color space.
    CComQIPtr <IDXGIOutput6> pOutput6 (pOutput);

    if (pOutput6 != nullptr)
    {
      UINT uiHdrFlags = 0x0;

      pOutput6->GetDesc1 (&data->DXGIDesc);
    
      data->SDRWhiteLevel = SKIF_Util_GetSDRWhiteLevelForHMONITOR (data->DXGIDesc.Monitor);

#pragma region Enable HDR
      // DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709    - SDR display with no Advanced Color capabilities
      // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709    - Standard definition for scRGB, and is usually used with 16 bit integer, 16 bit floating point, or 32 bit floating point color channels.
      // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 - HDR display with all Advanced Color capabilities

      if (SKIF_bCanHDR          && // Does the system support HDR?
          _registry.iHDRMode  > 0) // HDR support is not disabled, is it?
      {
        DXGI_COLOR_SPACE_TYPE dxgi_cst =
          (_registry.iHDRMode == 2)
            ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709     // scRGB (FP16 only)
            : DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; // HDR10
      
        CComQIPtr <IDXGISwapChain3>
            pSwapChain3 (data->SwapChain);

        if (pSwapChain3 != nullptr)
        {
          pSwapChain3->CheckColorSpaceSupport (dxgi_cst, &uiHdrFlags);

          if ( DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT ==
                ( uiHdrFlags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT )
             )
          {
            pOutput6->GetDesc1 (&data->DXGIDesc);

            // Is the display in HDR mode?
            if (data->DXGIDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
            {
              data->HDR     = true;
              data->HDRMode = _registry.iHDRMode;

              pSwapChain3->SetColorSpace1 (dxgi_cst);

              pOutput6->GetDesc1 (&data->DXGIDesc);
            }
          }
        }
      }
#pragma endregion
    }

    if (! data->HDR)
      data->SDRMode = _registry.iSDRMode;

    CComPtr <
      ID3D11Texture2D
    >              pBackBuffer;
    data->SwapChain->GetBuffer ( 0, IID_PPV_ARGS (
                  &pBackBuffer.p                 )
                               );
    bd->pd3dDevice->CreateRenderTargetView ( pBackBuffer, nullptr,
                           &data->RTView );
    
    PLOG_INFO   << "+-----------------+-------------------------------------+";
    PLOG_INFO   << "| Resolution      | " << swap_desc.Width << "x" << swap_desc.Height;
    PLOG_INFO   << "| Dynamic Range   | " << ((data->HDR) ? "HDR" : "SDR");
    PLOG_INFO   << "| SDR White Level | " << data->SDRWhiteLevel;
    if (     swap_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
      PLOG_INFO << "| Format          | DXGI_FORMAT_R16G16B16A16_FLOAT";
    else if (swap_desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
      PLOG_INFO << "| Format          | DXGI_FORMAT_R10G10B10A2_UNORM";
    else if (swap_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
      PLOG_INFO << "| Format          | DXGI_FORMAT_R8G8B8A8_UNORM";
    else
      PLOG_INFO << "| Format          | Unexpected format";
    PLOG_INFO   << "| Buffers         | " << swap_desc.BufferCount;
    PLOG_INFO   << "| Flags           | " << std::format("{:#x}", swap_desc.Flags);
    if (     swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)
      PLOG_INFO << "| Swap Effect     | DXGI_SWAP_EFFECT_FLIP_DISCARD";
    else if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
      PLOG_INFO << "| Swap Effect     | DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL";
    else if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
      PLOG_INFO << "| Swap Effect     | DXGI_SWAP_EFFECT_DISCARD";
    else if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
      PLOG_INFO << "| Swap Effect     | DXGI_SWAP_EFFECT_SEQUENTIAL";
    else 
      PLOG_INFO << "| Swap Effect     | Unexpected swap effect";
    PLOG_INFO   << "+-----------------+-------------------------------------+";

    if (SKIF_bCanWaitSwapchain && 
          (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD    ||
           swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ))
    {
      CComQIPtr <IDXGISwapChain2>
          pSwapChain2 (data->SwapChain);

      if (pSwapChain2 != nullptr)
      {
        // The maximum number of back buffer frames that will be queued for the swap chain. This value is 1 by default.
        // This method is only valid for use on swap chains created with DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT.
        if (S_OK == pSwapChain2->SetMaximumFrameLatency (1))
        {
          data->WaitHandle =
            pSwapChain2->GetFrameLatencyWaitableObject  ( );

          if (data->WaitHandle)
          {
            vSwapchainWaitHandles.push_back (data->WaitHandle);

            // One-time wait to align the thread for minimum latency (reduces latency by half in testing)
            WaitForSingleObjectEx (data->WaitHandle, INFINITE, true);
            // Block this thread until the swap chain is finished presenting. Note that it is
            // important to call this before the first Present in order to minimize the latency
            // of the swap chain.
          }
        }
      }
    }
  }
}

static void
SKIF_ImGui_ImplDX11_DestroyWindow (ImGuiViewport* viewport)
{
  SKIF_ImGui_ImplDX11_ViewportData *data =
    static_cast <SKIF_ImGui_ImplDX11_ViewportData *> (
                      viewport->RendererUserData
  );

  // The main viewport (owned by the application) will always have RendererUserData == NULL since we didn't create the data for it.
  if (data != nullptr)
  {
    if (data->WaitHandle) {
      if (! vSwapchainWaitHandles.empty())
        vSwapchainWaitHandles.erase(std::remove(vSwapchainWaitHandles.begin(), vSwapchainWaitHandles.end(), data->WaitHandle), vSwapchainWaitHandles.end());

      CloseHandle (
        data->WaitHandle
      );
    }
    data->WaitHandle = 0;

    if (data->SwapChain)
        data->SwapChain->Release ();
        data->SwapChain = nullptr;

    if (data->RTView)
        data->RTView->Release ();
        data->RTView = nullptr;

    IM_DELETE (data);
  }

  viewport->RendererUserData = nullptr;

}

static void
SKIF_ImGui_ImplDX11_SetWindowSize (ImGuiViewport* viewport, ImVec2 size)
{
  ImGui_ImplDX11_Data* bd =
    ImGui_ImplDX11_GetBackendData ( );

  SKIF_ImGui_ImplDX11_ViewportData *data =
    static_cast <SKIF_ImGui_ImplDX11_ViewportData *> (
                      viewport->RendererUserData
  );

  if (data != nullptr)
  {
    if (data->RTView)
    {
      data->RTView->Release ();
      data->RTView = nullptr;
    }

    if (data->SwapChain)
    {
      CComPtr <ID3D11Texture2D> pBackBuffer;

      DXGI_SWAP_CHAIN_DESC1       swap_desc = { };
      data->SwapChain->GetDesc1 (&swap_desc);

      data->SwapChain->ResizeBuffers (
        0, (UINT)size.x,
           (UINT)size.y,
            swap_desc.Format,
            swap_desc.Flags
      );

      data->SwapChain->GetBuffer (
        0, IID_PPV_ARGS (
                    &pBackBuffer.p
                        )
      );

      if (pBackBuffer == nullptr) {
        PLOG_ERROR << "ImGui_ImplDX11_SetWindowSize() failed creating buffers";
        return;
      }

      bd->pd3dDevice->CreateRenderTargetView ( pBackBuffer,
                    nullptr, &data->RTView );
    }
  }
}

static void
SKIF_ImGui_ImplDX11_RenderWindow  (ImGuiViewport* viewport, void*)
{
  ImGui_ImplDX11_Data* bd =
    ImGui_ImplDX11_GetBackendData ( );

  SKIF_ImGui_ImplDX11_ViewportData *data =
    static_cast <SKIF_ImGui_ImplDX11_ViewportData *> (
                      viewport->RendererUserData
  );

  ImVec4 clear_color =
    ImVec4 (0.0f, 0.0f, 0.0f, 1.0f);

  bd->pd3dDeviceContext->OMSetRenderTargets ( 1,
                &data->RTView,    nullptr );

  if (! (viewport->Flags & ImGuiViewportFlags_NoRendererClear))
  {
    bd->pd3dDeviceContext->ClearRenderTargetView (
                                  data->RTView,
                         (float *)&clear_color );
  }

  ImGui_ImplDX11_RenderDrawData (viewport->DrawData);
}

static void
SKIF_ImGui_ImplDX11_SwapBuffers ( ImGuiViewport *viewport,
                                      void * )
{
  SKIF_ImGui_ImplDX11_ViewportData *data =
    static_cast <SKIF_ImGui_ImplDX11_ViewportData *> (
                      viewport->RendererUserData
    );

  UINT Interval = 1; // BitBlt Mode

  if (SKIF_bCanFlip && SKIF_Util_IsWindows10OrGreater ( ))
    Interval    = 2; // Flip VRR Compatibility Mode (only relevant on Windows 10+)

  if (_registry.bDisableVSYNC)
    Interval    = 1; // Normal Mode (V-Sync ON)

  UINT PresentFlags = 0x0;

  if (SKIF_bCanFlip)
  {
    PresentFlags   = DXGI_PRESENT_RESTART;

    if (Interval == 0 && SKIF_bCanAllowTearing)
      PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
  }

  if (data->SwapChain != nullptr)
  {
    //if (data->WaitHandle)
    //  WaitForSingleObject (data->WaitHandle, INFINITE);

    data->SwapChain->Present ( Interval, PresentFlags );
    //data->PresentCount++;

    /* 2023-04-30: Does not actually seem to make a difference? We don't use dirty rectangles of Present1() at all
    if (data->WaitHandle)
    {
      CComQIPtr <IDXGISwapChain2>
        pSwap2 (data->SwapChain);
      
      // if (WAIT_OBJECT_0 == WaitForSingleObject (data->WaitHandle, INFINITE))

      // For waitable swapchains we wait in the main loop, so present immediately here
      DXGI_PRESENT_PARAMETERS       pparams = { };
      pSwap2->Present1 ( Interval, PresentFlags,
                                    &pparams );
      data->PresentCount++;
    }
    */
  }
  else {
    PLOG_WARNING << "Swapchain is a nullptr!";
  }
}
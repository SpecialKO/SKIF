// dear imgui: Renderer for DirectX11
// This needs to be used along with a Platform Binding (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID. Read the FAQ about ImTextureID in imgui.cpp.
//  [X] Renderer: Multi-viewport support. Enable with 'io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable'.
//  [X] Renderer: Support for large meshes (64k+ vertices) with 16-bits indices.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp
// https://github.com/ocornut/imgui

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2019-XX-XX: Platform: Added support for multiple windows via the ImGuiPlatformIO interface.
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


#include <Windows.h>
#include <array>
#include <xutility>
#include <exception>
#include <atlbase.h>
#include <vector>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/d3d11/imgui_impl_dx11.h>

#include <shaders/imgui_pix.h>
#include <shaders/imgui_vtx.h>

// PLOG
#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"

// Registry Settings
#include <utility/registry.h>

// DirectX
#include <dxgi1_6.h>
#include <stdio.h>
#include <d3d11.h>
#include <format>

// External Variables
extern bool SKIF_bCanAllowTearing;
extern bool SKIF_bCanWaitSwapchain;
extern bool SKIF_bCanHDR;
extern bool RecreateSwapChains;

extern std::vector<HANDLE> vSwapchainWaitHandles;

// External functions
extern bool SKIF_Util_IsWindows8Point1OrGreater   (void);
extern bool SKIF_Util_IsWindows10OrGreater        (void);
extern bool SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
extern bool SKIF_Util_IsHDRSupported              (bool refresh = false);
extern bool SKIF_Util_IsHDRActive                 (bool refresh = false);
extern int  SKIF_Util_GetSDRWhiteLevelForHMONITOR (HMONITOR hMonitor);

// DirectX data
static CComPtr <ID3D11Device>             g_pd3dDevice;
static CComPtr <ID3D11DeviceContext>      g_pd3dDeviceContext;
static CComPtr <IDXGIFactory2>            g_pFactory;
static CComPtr <ID3D11Buffer>             g_pVB;
static CComPtr <ID3D11Buffer>             g_pIB;
static CComPtr <ID3D11VertexShader>       g_pVertexShader;
static CComPtr <ID3D11InputLayout>        g_pInputLayout;
static CComPtr <ID3D11Buffer>             g_pVertexConstantBuffer;
static CComPtr <ID3D11Buffer>             g_pPixelConstantBuffer;
static CComPtr <ID3D11PixelShader>        g_pPixelShader;
static CComPtr <ID3D11SamplerState>       g_pFontSampler;
static CComPtr <ID3D11ShaderResourceView> g_pFontTextureView;
static CComPtr <ID3D11RasterizerState>    g_pRasterizerState;
static CComPtr <ID3D11BlendState>         g_pBlendState;
static CComPtr <ID3D11DepthStencilState>  g_pDepthStencilState;
static int                                g_VertexBufferSize = 5000,
                                          g_IndexBufferSize  = 10000;

#define D3D11_SHADER_MAX_INSTANCES_PER_CLASS 256
#define D3D11_MAX_SCISSOR_AND_VIEWPORT_ARRAYS \
  D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE

template <typename _Tp, size_t n> using CComPtrArray = std::array <CComPtr <_Tp>, n>;

template <typename _Type>
struct D3D11ShaderState
{
  CComPtr <_Type>                     Shader;
  CComPtrArray <
    ID3D11Buffer,             D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
  >                                   Constants;
  CComPtrArray <
    ID3D11ShaderResourceView, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
  >                                   Resources;
  CComPtrArray <
    ID3D11SamplerState,       D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT
  >                                   Samplers;
  struct {
    UINT                              Count;
    CComPtrArray <
      ID3D11ClassInstance,    D3D11_SHADER_MAX_INSTANCES_PER_CLASS
    >                                 Array;
  } Instances;
};

using _VS = D3D11ShaderState <ID3D11VertexShader>;
using _PS = D3D11ShaderState <ID3D11PixelShader>;
using _GS = D3D11ShaderState <ID3D11GeometryShader>;
using _DS = D3D11ShaderState <ID3D11DomainShader>;
using _HS = D3D11ShaderState <ID3D11HullShader>;
using _CS = D3D11ShaderState <ID3D11ComputeShader>;

// Backup DX state that will be modified to restore it afterwards (unfortunately this is very ugly looking and verbose. Close your eyes!)
struct SK_IMGUI_D3D11StateBlock {
  struct {
    UINT                              RectCount;
    D3D11_RECT                        Rects [D3D11_MAX_SCISSOR_AND_VIEWPORT_ARRAYS];
  } Scissor;

  struct {
    UINT                              ArrayCount;
    D3D11_VIEWPORT                    Array [D3D11_MAX_SCISSOR_AND_VIEWPORT_ARRAYS];
  } Viewport;

  struct {
    CComPtr <ID3D11RasterizerState>   State;
  } Rasterizer;

  struct {
    CComPtr <ID3D11BlendState>        State;
    FLOAT                             Factor [4];
    UINT                              SampleMask;
  } Blend;

  struct {
    UINT                              StencilRef;
    CComPtr <ID3D11DepthStencilState> State;
  } DepthStencil;

  struct {
    _VS                               Vertex;
    _PS                               Pixel;
    _GS                               Geometry;
    _DS                               Domain;
    _HS                               Hull;
    _CS                               Compute;
  } Shaders;

  struct {
    struct {
      CComPtr <ID3D11Buffer>          Pointer;
      DXGI_FORMAT                     Format;
      UINT                            Offset;
    } Index;

    struct {
      CComPtrArray <ID3D11Buffer,               D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT>
                                      Pointers;
      UINT                            Strides  [D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
      UINT                            Offsets  [D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    } Vertex;
  } Buffers;

  struct {
    CComPtrArray <
      ID3D11RenderTargetView, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT
    >                                 RenderTargetViews;
    CComPtr <
      ID3D11DepthStencilView
    >                                 DepthStencilView;
  } RenderTargets;

  D3D11_PRIMITIVE_TOPOLOGY            PrimitiveTopology;
  CComPtr <ID3D11InputLayout>         InputLayout;

  enum _StateMask : DWORD {
    VertexStage         = 0x0001,
    PixelStage          = 0x0002,
    GeometryStage       = 0x0004,
    HullStage           = 0x0008,
    DomainStage         = 0x0010,
    ComputeStage        = 0x0020,
    RasterizerState     = 0x0040,
    BlendState          = 0x0080,
    OutputMergeState    = 0x0100,
    DepthStencilState   = 0x0200,
    InputAssemblerState = 0x0400,
    ViewportState       = 0x0800,
    ScissorState        = 0x1000,
    RenderTargetState   = 0x2000,
    _StateMask_All      = 0xffffffff
  };

  void Capture ( ID3D11DeviceContext* pDevCtx,
                 DWORD                iStateMask = _StateMask_All )
  {
    if (iStateMask & ScissorState)        Scissor.RectCount   = D3D11_MAX_SCISSOR_AND_VIEWPORT_ARRAYS;
    if (iStateMask & ViewportState)       Viewport.ArrayCount = D3D11_MAX_SCISSOR_AND_VIEWPORT_ARRAYS;

    if (iStateMask & ScissorState)
       pDevCtx->RSGetScissorRects      ( &Scissor.RectCount,
                                          Scissor.Rects    );
    if (iStateMask & ViewportState)
       pDevCtx->RSGetViewports         ( &Viewport.ArrayCount,
                                          Viewport.Array   );
    if (iStateMask & RasterizerState)
      pDevCtx->RSGetState              ( &Rasterizer.State );

    if (iStateMask & BlendState)
       pDevCtx->OMGetBlendState        ( &Blend.State,
                                          Blend.Factor,
                                         &Blend.SampleMask );

    if (iStateMask & DepthStencilState)
       pDevCtx->OMGetDepthStencilState ( &DepthStencil.State,
                                         &DepthStencil.StencilRef );

    if (iStateMask & RenderTargetState)
    {
      pDevCtx->OMGetRenderTargets ( D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                    &RenderTargets.RenderTargetViews [0].p,
                                    &RenderTargets.DepthStencilView.p );
    }

#define        STAGE_INPUT_RESOURCE_SLOT_COUNT \
  D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
#define        STAGE_CONSTANT_BUFFER_API_SLOT_COUNT \
  D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT
#define        STAGE_SAMPLER_SLOT_COUNT \
  D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT

#define _Stage(StageName) Shaders.##StageName
#define Stage_Get(_Tp)    pDevCtx->##_Tp##Get
#define Stage_Set(_Tp)    pDevCtx->##_Tp##Set

#define _BackupStage(_Tp,StageName)                                               \
  if (iStateMask & StageName##Stage) {                                            \
    Stage_Get(_Tp)ShaderResources   ( 0, STAGE_INPUT_RESOURCE_SLOT_COUNT,         \
                                       &_Stage(StageName).Resources       [0].p );\
    Stage_Get(_Tp)ConstantBuffers   ( 0, STAGE_CONSTANT_BUFFER_API_SLOT_COUNT,    \
                                       &_Stage(StageName).Constants       [0].p );\
    Stage_Get(_Tp)Samplers          ( 0, STAGE_SAMPLER_SLOT_COUNT,                \
                                       &_Stage(StageName).Samplers        [0].p );\
    Stage_Get(_Tp)Shader            (  &_Stage(StageName).Shader,                 \
                                       &_Stage(StageName).Instances.Array [0].p,  \
                                       &_Stage(StageName).Instances.Count       );\
  }

    _BackupStage ( VS, Vertex   );
    _BackupStage ( PS, Pixel    );
    _BackupStage ( GS, Geometry );
    _BackupStage ( DS, Domain   );
    _BackupStage ( HS, Hull     );
    _BackupStage ( CS, Compute  );

    if (iStateMask & InputAssemblerState)
    {
      pDevCtx->IAGetPrimitiveTopology ( &PrimitiveTopology );
      pDevCtx->IAGetIndexBuffer       ( &Buffers.Index.Pointer,
                                        &Buffers.Index.Format,
                                        &Buffers.Index.Offset );
      pDevCtx->IAGetVertexBuffers     ( 0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
                                         &Buffers.Vertex.Pointers [0].p,
                                          Buffers.Vertex.Strides,
                                          Buffers.Vertex.Offsets );
      pDevCtx->IAGetInputLayout       ( &InputLayout );
    }
  }

  void Apply ( ID3D11DeviceContext* pDevCtx,
               DWORD                iStateMask = _StateMask_All )
  {

    if (iStateMask & ScissorState)
       pDevCtx->RSSetScissorRects      ( Scissor.RectCount,   Scissor.Rects  );

    if (iStateMask & ViewportState)
       pDevCtx->RSSetViewports         ( Viewport.ArrayCount, Viewport.Array );

    if (iStateMask & RasterizerState)
    {
      pDevCtx->RSSetState              ( Rasterizer.State );
                                         Rasterizer.State   = nullptr;
    }

    if (iStateMask & BlendState)
    {  pDevCtx->OMSetBlendState        ( Blend.State,
                                         Blend.Factor,
                                         Blend.SampleMask);
                                         Blend.State        = nullptr;
    }

    if (iStateMask & DepthStencilState)
    {  pDevCtx->OMSetDepthStencilState ( DepthStencil.State,
                                         DepthStencil.StencilRef );
                                         DepthStencil.State = nullptr;
    }

    if (iStateMask & RenderTargetState)
    {
      pDevCtx->OMSetRenderTargets ( D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                    &RenderTargets.RenderTargetViews [0].p,
                                     RenderTargets.DepthStencilView.p );

      std::fill ( RenderTargets.RenderTargetViews.begin (),
                  RenderTargets.RenderTargetViews.end   (),
                    nullptr );
    }

#define _RestoreStage(_Tp,StageName)                                              \
  if (iStateMask & StageName##Stage) {                                            \
    Stage_Set(_Tp)ShaderResources  ( 0, STAGE_INPUT_RESOURCE_SLOT_COUNT,          \
                                      &_Stage(StageName).Resources       [0].p ); \
                           std::fill ( _Stage(StageName).Resources.begin ( ),     \
                                       _Stage(StageName).Resources.end   ( ),     \
                                         nullptr );                               \
    Stage_Set(_Tp)ConstantBuffers  ( 0, STAGE_CONSTANT_BUFFER_API_SLOT_COUNT,     \
                                      &_Stage(StageName).Constants       [0].p ); \
                           std::fill ( _Stage(StageName).Constants.begin ( ),     \
                                       _Stage(StageName).Constants.end   ( ),     \
                                         nullptr );                               \
    Stage_Set(_Tp)Samplers         ( 0, STAGE_SAMPLER_SLOT_COUNT,                 \
                                      &_Stage(StageName).Samplers        [0].p ); \
                           std::fill ( _Stage(StageName).Samplers.begin  ( ),     \
                                       _Stage(StageName).Samplers.end    ( ),     \
                                         nullptr );                               \
    Stage_Set(_Tp)Shader           ( _Stage(StageName).Shader,                    \
                                    &_Stage(StageName).Instances.Array  [0].p,    \
                                     _Stage(StageName).Instances.Count );         \
                         std::fill ( _Stage(StageName).Instances.Array.begin ( ), \
                                     _Stage(StageName).Instances.Array.end   ( ), \
                                      nullptr );                                  \
  }

    _RestoreStage ( PS, Pixel    );
    _RestoreStage ( VS, Vertex   );
    _RestoreStage ( GS, Geometry );
    _RestoreStage ( HS, Hull     );
    _RestoreStage ( DS, Domain   );
    _RestoreStage ( CS, Compute  );

    if (iStateMask & InputAssemblerState)
    {
      pDevCtx->IASetPrimitiveTopology ( PrimitiveTopology );
      pDevCtx->IASetIndexBuffer       ( Buffers.Index.Pointer,
                                        Buffers.Index.Format,
                                        Buffers.Index.Offset );
                                        Buffers.Index.Pointer = nullptr;
      pDevCtx->IASetVertexBuffers     ( 0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,
                                       &Buffers.Vertex.Pointers [0].p,
                                        Buffers.Vertex.Strides,
                                        Buffers.Vertex.Offsets );
                            std::fill ( Buffers.Vertex.Pointers.begin (),
                                        Buffers.Vertex.Pointers.end   (),
                                          nullptr );

      pDevCtx->IASetInputLayout       ( InputLayout );
                                        InputLayout = nullptr;
    }
  }
};

//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the back-end to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

struct ImGuiViewportDataDx11 {
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

   ImGuiViewportDataDx11 (void) {            SwapChain  = nullptr;   RTView  = nullptr;   WaitHandle  = 0;  PresentCount = 0; SDRMode = 0; SDRWhiteLevel = 80; HDRMode = 0; HDR = false; HDRLuma = 0.0f; HDRMinLuma = 0.0f; DXGIDesc = {   }; DXGIFormat = DXGI_FORMAT_UNKNOWN; }
  ~ImGuiViewportDataDx11 (void) { IM_ASSERT (SwapChain == nullptr && RTView == nullptr && WaitHandle == 0);                                }

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


struct VERTEX_CONSTANT_BUFFER {
  float   mvp [4][4];

  // scRGB allows values > 1.0, sRGB (SDR) simply clamps them
  float luminance_scale [4]; // For HDR displays,    1.0 = 80 Nits
                             // For SDR displays, >= 1.0 = 80 Nits
  float padding         [4];
};

// Forward Declarations
static void ImGui_ImplDX11_InitPlatformInterface     (void);
static void ImGui_ImplDX11_ShutdownPlatformInterface (void);

static void
ImGui_ImplDX11_SetupRenderState ( ImDrawData          *draw_data,
                                  ID3D11DeviceContext *ctx )
{
    // Setup viewport
  D3D11_VIEWPORT vp = { };

  vp.Width    = draw_data->DisplaySize.x;
  vp.Height   = draw_data->DisplaySize.y;
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  vp.TopLeftX = vp.TopLeftY = 0;

  ctx->RSSetViewports (1, &vp);

  // Setup shader and vertex buffers
  unsigned int stride = sizeof (ImDrawVert);
  unsigned int offset = 0;

  ctx->IASetInputLayout       ( g_pInputLayout                 );
  ctx->IASetVertexBuffers     ( 0, 1, &g_pVB.p, &stride, &offset );
  ctx->IASetIndexBuffer       ( g_pIB, sizeof (ImDrawIdx) == 2 ?
                                          DXGI_FORMAT_R16_UINT :
                                          DXGI_FORMAT_R32_UINT, 0     );
  ctx->IASetPrimitiveTopology ( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

  ctx->VSSetShader            (        g_pVertexShader, nullptr, 0 );
  ctx->VSSetConstantBuffers   ( 0, 1, &g_pVertexConstantBuffer.p     );
  ctx->PSSetShader            (        g_pPixelShader,  nullptr, 0 );
  ctx->PSSetConstantBuffers   ( 0, 1, &g_pPixelConstantBuffer.p      );
  ctx->PSSetSamplers          ( 0, 1, &g_pFontSampler.p              );

  // Setup blend state
  static constexpr float
    blend_factor [4] =
      { 0.f, 0.f, 0.f, 0.f };

  ctx->OMSetBlendState        ( g_pBlendState,
                                   blend_factor,      0xffffffff );
  ctx->OMSetDepthStencilState ( g_pDepthStencilState, 0          );
  ctx->RSSetState             ( g_pRasterizerState               );
}

// Render function
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
void
ImGui_ImplDX11_RenderDrawData (ImDrawData *draw_data)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ///if (! g_pVertexShader)
  ///{
  ///  if (! ImGui_ImplDX11_CreateDeviceObjects ())
  ///    return;
  ///}

  // Avoid rendering when minimized
  if ( draw_data->DisplaySize.x <= 0.0f ||
       draw_data->DisplaySize.y <= 0.0f    ) return;

  ID3D11DeviceContext *ctx =
    g_pd3dDeviceContext;

  // Create and grow vertex/index buffers if needed
  if (! g_pVB || g_VertexBufferSize < draw_data->TotalVtxCount)
  {
    g_pVB = nullptr;

    g_VertexBufferSize =
      draw_data->TotalVtxCount + 5000;

    D3D11_BUFFER_DESC
    buffer_desc                = { };
    buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth      = g_VertexBufferSize * sizeof (ImDrawVert);
    buffer_desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer_desc.MiscFlags      = 0;

    if ( FAILED ( g_pd3dDevice->CreateBuffer ( &buffer_desc,
                        nullptr, &g_pVB      )
       )        ) return;
  }

  if (! g_pIB || g_IndexBufferSize < draw_data->TotalIdxCount)
  {
    g_pIB = nullptr;

    g_IndexBufferSize =
      draw_data->TotalIdxCount + 10000;

    D3D11_BUFFER_DESC
    buffer_desc                = { };
    buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth      = g_IndexBufferSize * sizeof (ImDrawIdx);
    buffer_desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if ( FAILED ( g_pd3dDevice->CreateBuffer ( &buffer_desc,
                        nullptr, &g_pIB      )
       )        ) return;
  }

  // Upload vertex/index data into a single contiguous GPU buffer
  D3D11_MAPPED_SUBRESOURCE
    vtx_resource = { },
    idx_resource = { };

  if (FAILED (ctx->Map (g_pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource)))
    return;
  if (FAILED (ctx->Map (g_pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource)))
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

  ctx->Unmap (g_pVB, 0);
  ctx->Unmap (g_pIB, 0);

  // Setup orthographic projection matrix into our constant buffer
  // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
  {
    D3D11_MAPPED_SUBRESOURCE
          mapped_resource = { };

    if ( FAILED (ctx->Map (
           g_pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD,
                                    0, &mapped_resource
                    ))
       ) return;

    VERTEX_CONSTANT_BUFFER *constant_buffer =
        static_cast <VERTEX_CONSTANT_BUFFER *> (
                              mapped_resource.pData
        );

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
    constant_buffer->luminance_scale [1] = 0.0f; // y - HDR (1.0f) / SDR (0.0f)
    constant_buffer->luminance_scale [2] = 0.0f; // z - Black level (unused)
    constant_buffer->luminance_scale [3] = 0.0f; // w - isSRGB

    ImGuiViewportDataDx11 *data =
      static_cast <ImGuiViewportDataDx11 *> (
                      draw_data->OwnerViewport->RendererUserData
      );

    if (data == nullptr)
      return;

    if (data->HDR)
    {
      constant_buffer->luminance_scale [1] = 1.0f;

      // scRGB HDR 16 bpc
      if (data->HDRMode == 2)
      {
        constant_buffer->luminance_scale [0] = (_registry.iHDRBrightness          / 80.0f); // Org: data->SKIF_GetHDRWhiteLuma    ( ) / 80.0f
      //constant_buffer->luminance_scale [2] = (data->SKIF_GetMinHDRLuminance ( ) / 80.0f);
      }

      // HDR10
      else {
        constant_buffer->luminance_scale [0] = float (-_registry.iHDRBrightness);           // Org: -data->SKIF_GetHDRWhiteLuma ( )
      }
    }

    // scRGB 16 bpc special handling
    else if (data->DXGIFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
      // SDR 16 bpc on HDR display
      if (data->SDRWhiteLevel > 80)
      {
        constant_buffer->luminance_scale [0] = (data->SDRWhiteLevel               / 80.0f);
      //constant_buffer->luminance_scale [2] = (data->SKIF_GetMinHDRLuminance ( ) / 80.0f);
      }

      // SDR 16 bpc on SDR display
      constant_buffer->luminance_scale [3] = 1.0f;
    }

         ctx->Unmap ( g_pVertexConstantBuffer, 0 );
    if ( FAILED (ctx->Map (
           g_pPixelConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD,
                                   0, &mapped_resource
                    ))
       ) return;

    if (data->HDR || data->DXGIFormat == DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
      static_cast <  float *> (mapped_resource.pData)[0] = 0.0f;
      static_cast <  float *> (mapped_resource.pData)[1] = 0.0f;
      static_cast <  float *> (mapped_resource.pData)[2] =
      static_cast <  float  > (draw_data->DisplaySize.x);
      static_cast <  float *> (mapped_resource.pData)[3] =
      static_cast <  float  > (draw_data->DisplaySize.y);
    }

    ctx->Unmap ( g_pPixelConstantBuffer, 0 );
  }

  ////auto _CaptureMask =
  ////  SK_IMGUI_D3D11StateBlock::_StateMask_All &
  ////    ~( SK_IMGUI_D3D11StateBlock::GeometryStage |
  ////       SK_IMGUI_D3D11StateBlock::DomainStage   |
  ////       SK_IMGUI_D3D11StateBlock::HullStage     |
  ////       SK_IMGUI_D3D11StateBlock::ComputeStage );
  ////
  ////SK_IMGUI_D3D11StateBlock
  ////  sb                       = { };
  ////  sb.Capture (ctx, _CaptureMask);

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

        ctx->PSSetShaderResources ( 0, 1, &texture_srv);
        ctx->DrawIndexed          ( pcmd->ElemCount,
                                    pcmd->IdxOffset + global_idx_offset,
                                    pcmd->VtxOffset + global_vtx_offset );
      }
    }

    global_idx_offset += cmd_list->IdxBuffer.Size;
    global_vtx_offset += cmd_list->VtxBuffer.Size;
  }

////sb.Apply (ctx, _CaptureMask);
}

class SK_ComException : public
       std::exception
{
public:
  SK_ComException (
    HRESULT hr
  ) : __hr (hr) { }

  const char*
  what (void) const override
  {
    static char
      s_str [64] = { };

    sprintf_s (
      s_str, "Failure with HRESULT of %08X",
                    (int)__hr
              );
    return
      s_str;
  }

private:
  HRESULT
    __hr;
};

inline void
ThrowIfFailed (HRESULT hr)
{
  if (SUCCEEDED (hr))
    return;

  throw
    SK_ComException (hr);
}

static void ImGui_ImplDX11_CreateFontsTexture (void)
{
  auto
  _BuildForSlot =
  [&](UINT slot)
  {
    std::ignore = slot;

    auto pDev = g_pd3dDevice;

    // Build texture atlas
    ImGuiIO& io (
      ImGui::GetIO ()
    );

    unsigned char* pixels = nullptr;
    int            width  = 0,
                   height = 0;

    io.Fonts->GetTexDataAsAlpha8 ( &pixels,
                                   &width, &height );

    D3D_FEATURE_LEVEL  featureLevel =
      pDev->GetFeatureLevel ();

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
      staging_desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    ThrowIfFailed (
      pDev->CreateTexture2D ( &staging_desc, nullptr,
                                     &pStagingTexture.p ));
    ThrowIfFailed (
      pDev->CreateTexture2D ( &tex_desc,     nullptr,
                                     &pFontTexture.p ));

    CComPtr   <ID3D11DeviceContext> pDevCtx;
    pDev->GetImmediateContext     (&pDevCtx);

    D3D11_MAPPED_SUBRESOURCE
          mapped_tex = { };

    ThrowIfFailed (
      pDevCtx->Map ( pStagingTexture.p, 0, D3D11_MAP_WRITE, 0,
                     &mapped_tex ));

    for (int y = 0; y < height; y++)
    {
      ImU32  *pDst =
        (ImU32 *)((uintptr_t)mapped_tex.pData +
                             mapped_tex.RowPitch * y);
      ImU8   *pSrc =              pixels + width * y;

      for (int x = 0; x < width; x++)
      {
        *pDst++ =
          IM_COL32 (255, 255, 255, (ImU32)(*pSrc++));
      }
    }

    pDevCtx->Unmap        ( pStagingTexture, 0 );
    pDevCtx->CopyResource (    pFontTexture,
                            pStagingTexture    );

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC
      srvDesc = { };
      srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
      srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels       = tex_desc.MipLevels;
      srvDesc.Texture2D.MostDetailedMip = 0;

    ThrowIfFailed (
      pDev->CreateShaderResourceView ( pFontTexture, &srvDesc,
                                    &g_pFontTextureView.p ));

    // Store our identifier
    io.Fonts->TexID =
      g_pFontTextureView;

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

    ThrowIfFailed (
      pDev->CreateSamplerState ( &sampler_desc,
                         &g_pFontSampler.p ));

    io.Fonts->ClearTexData ();
  };

  try
  {
    _BuildForSlot (0);
  }

  catch (const SK_ComException&)
  {
  }
}

bool
ImGui_ImplDX11_CreateDeviceObjects (void)
{
  if (! g_pd3dDevice)
    return false;

  if (g_pFontSampler)
    ImGui_ImplDX11_InvalidateDeviceObjects ();

  // Create the vertex shader
  if ( FAILED (
    g_pd3dDevice->CreateVertexShader (
      (DWORD *)imgui_vs_bytecode,
       sizeof (imgui_vs_bytecode    ) /
       sizeof (imgui_vs_bytecode [0]),
         nullptr,
                    &g_pVertexShader )
     )        ) return false;

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
          0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
            offsetof (ImDrawVert,      col),
                D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

  if ( FAILED (
        g_pd3dDevice->CreateInputLayout (
                           local_layout, 3,
              imgui_vs_bytecode,
      sizeof (imgui_vs_bytecode    ) /
      sizeof (imgui_vs_bytecode [0]),
                        &g_pInputLayout )
     )        ) return false;

  // Create the constant buffers
  D3D11_BUFFER_DESC
  buffer_desc                = { };
  buffer_desc.ByteWidth      = sizeof (VERTEX_CONSTANT_BUFFER);
  buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.MiscFlags      = 0;

  g_pd3dDevice->CreateBuffer ( &buffer_desc, nullptr,
    &g_pVertexConstantBuffer );

  buffer_desc                = { };
  buffer_desc.ByteWidth      = sizeof (float) * 4;
  buffer_desc.Usage          = D3D11_USAGE_DYNAMIC;
  buffer_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  buffer_desc.MiscFlags      = 0;

  g_pd3dDevice->CreateBuffer ( &buffer_desc, nullptr,
     &g_pPixelConstantBuffer );

  // Create the pixel shader
  if ( FAILED (
         g_pd3dDevice->CreatePixelShader (
           (DWORD *)imgui_ps_bytecode,
            sizeof (imgui_ps_bytecode    ) /
            sizeof (imgui_ps_bytecode [0]),
              nullptr,   &g_pPixelShader )
     )        ) return false;

  // Create the blending setup
  D3D11_BLEND_DESC
  blend_desc                                        = { };
  blend_desc.AlphaToCoverageEnable                  = false;
  blend_desc.RenderTarget [0].BlendEnable           = true;
  blend_desc.RenderTarget [0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
  blend_desc.RenderTarget [0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
  blend_desc.RenderTarget [0].BlendOp               = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget [0].SrcBlendAlpha         = D3D11_BLEND_ONE;
  blend_desc.RenderTarget [0].DestBlendAlpha        = D3D11_BLEND_ZERO;
  blend_desc.RenderTarget [0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
  blend_desc.RenderTarget [0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  g_pd3dDevice->CreateBlendState ( &blend_desc,
                  &g_pBlendState );


  // Create the rasterizer state
  D3D11_RASTERIZER_DESC
  raster_desc                 = { };
  raster_desc.FillMode        = D3D11_FILL_SOLID;
  raster_desc.CullMode        = D3D11_CULL_NONE;
  raster_desc.ScissorEnable   = true;
  raster_desc.DepthClipEnable = true;

  g_pd3dDevice->CreateRasterizerState ( &raster_desc,
                  &g_pRasterizerState );

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

  g_pd3dDevice->CreateDepthStencilState ( &depth_stencil_desc,
                  &g_pDepthStencilState );

  ImGui_ImplDX11_CreateFontsTexture ();

  return true;
}

void
ImGui_ImplDX11_InvalidateDeviceObjects (void)
{
  if (! g_pd3dDevice)
    return;

  ImGuiIO& io =
    ImGui::GetIO ();

  g_pFontSampler          = nullptr;
  g_pFontTextureView      = nullptr;

  io.Fonts->TexID         = nullptr; // We copied g_pFontTextureView to io.Fonts->TexID so let's clear that as well.

  g_pIB                   = nullptr;
  g_pVB                   = nullptr;

  g_pBlendState           = nullptr;
  g_pDepthStencilState    = nullptr;
  g_pRasterizerState      = nullptr;
  g_pPixelShader          = nullptr;
  g_pVertexConstantBuffer = nullptr;
  g_pPixelConstantBuffer  = nullptr;
  g_pInputLayout          = nullptr;
  g_pVertexShader         = nullptr;
}

void
ImGui_ImplDX11_LogSwapChainFormat (ImGuiViewport *viewport)
{
  ImGuiViewportDataDx11 *data =
    static_cast <ImGuiViewportDataDx11 *> (
                      viewport->RendererUserData
  );

  static
    DXGI_SWAP_CHAIN_DESC1     swap_prev;
    DXGI_SWAP_CHAIN_DESC1     swap_desc;
  data->SwapChain->GetDesc1 (&swap_desc);

  if (swap_prev.Height     != swap_desc.Height   ||
      swap_prev.Flags      != swap_desc.Flags    ||
      swap_prev.Format     != swap_desc.Format   ||
      swap_prev.SwapEffect != swap_desc.SwapEffect)
  {
    swap_prev = swap_desc;

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
  }
}

bool
ImGui_ImplDX11_Init ( ID3D11Device        *device,
                      ID3D11DeviceContext *device_context )
{
  // Setup back-end capabilities flags
  ImGuiIO &io =
    ImGui::GetIO ();

  io.BackendRendererName = "imgui_impl_dx11";
  io.BackendFlags       |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
  io.BackendFlags       |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)

  g_pd3dDevice        = device;
  g_pd3dDeviceContext = device_context;

  CreateDXGIFactory1 (__uuidof (IDXGIFactory2), (void **)&g_pFactory.p);

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    ImGui_ImplDX11_InitPlatformInterface ();

  return true;
}

void ImGui_ImplDX11_Shutdown (void)
{
  ImGui_ImplDX11_ShutdownPlatformInterface ();
  ImGui_ImplDX11_InvalidateDeviceObjects   ();
  ImGui_ImplDX11_InvalidateDevice          ();
}

void ImGui_ImplDX11_InvalidateDevice (void)
{
  g_pFactory         .Release ( );
  g_pd3dDevice       .Release ( );
  g_pd3dDeviceContext.Release ( );
  g_pFactory          = nullptr;
  g_pd3dDevice        = nullptr;
  g_pd3dDeviceContext = nullptr;
}

extern ImGuiContext* GImGui;

static void
ImGui_ImplDX11_CreateWindow (ImGuiViewport *viewport);

static void
ImGui_ImplDX11_DestroyWindow (ImGuiViewport *viewport);

void ImGui_ImplDX11_NewFrame (void)
{
  ImGuiContext& g = *GImGui;
  CComQIPtr <IDXGIFactory2>
                 pFactory2
              (g_pFactory);

  // Force DXGIFactory and swapchain recreation every time we move monitor
  //   this solves issues with DirectFlip/Independent Flip not engaging
  //   based on the monitor the app launched initially on.
  bool   RecreateFactory =
              ( pFactory2.p != nullptr &&
              ! pFactory2->IsCurrent () );

#if 0
  if (RecreateSwapChains)
    OutputDebugString(L"RecreateSwapChains == true\n");

  if (RecreateFactory)
    OutputDebugString(L"RecreateFactory == true\n");
#endif

  if (RecreateSwapChains || RecreateFactory)
  {   RecreateSwapChains = false;

    PLOG_DEBUG << "Destroying any existing swapchains and their wait objects...";
    for (int i = 0; i < g.Viewports.Size; i++)
      ImGui_ImplDX11_DestroyWindow (g.Viewports [i]);

    ImGui_ImplDX11_InvalidateDeviceObjects ( );

    PLOG_DEBUG << "Clearing ID3D11DeviceContext state and flushing...";
    g_pd3dDeviceContext->ClearState ( );
    g_pd3dDeviceContext->Flush      ( );

    if (RecreateFactory)
    {
      PLOG_DEBUG << "Recreating factory...";

       pFactory2.Release ();
      g_pFactory.Release ();

      CreateDXGIFactory1 (__uuidof (IDXGIFactory2), (void **)&g_pFactory.p);
    }

    SKIF_bCanHDR = SKIF_Util_IsHDRActive (true);
    
    PLOG_DEBUG << "Recreating any necessary swapchains and their wait objects...";
    for (int i = 0; i < g.Viewports.Size; i++)
      ImGui_ImplDX11_CreateWindow  (g.Viewports [i]);
  }

  if (! g_pFontSampler)
    ImGui_ImplDX11_CreateDeviceObjects ();
}



static void
ImGui_ImplDX11_CreateWindow (ImGuiViewport *viewport)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ImGuiViewportDataDx11 *data =
    IM_NEW (ImGuiViewportDataDx11)( );

  viewport->RendererUserData = data;

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
  swap_desc.Flags              = 0x0;
  swap_desc.SampleDesc.Count   = 1;
  swap_desc.SampleDesc.Quality = 0;

  // Assume flip by default
  swap_desc.BufferCount  = 3; // Must be 2-16 for flip model // 2 to prevent SKIF from rendering x2 the refresh rate

  if (SKIF_bCanWaitSwapchain)
    swap_desc.Flags     |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  if (SKIF_bCanAllowTearing)
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
        swap_desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.BufferCount = 1;
        swap_desc.Flags       = 0x0;
        SKIF_bCanHDR          = false;
        _registry.iUIMode     = 0;
      }

      if (SUCCEEDED (g_pFactory->CreateSwapChainForHwnd ( g_pd3dDevice, hWnd, &swap_desc, NULL, NULL,
                                &data->SwapChain ))) break;
    }
  }

  // Safe Mode (BitBlt Discard)
  else {
    //swap_desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SwapEffect  = DXGI_SWAP_EFFECT_DISCARD;
    swap_desc.BufferCount = 1;
    swap_desc.Flags       = 0x0;

    g_pFactory->CreateSwapChainForHwnd ( g_pd3dDevice, hWnd, &swap_desc, NULL, NULL,
               &data->SwapChain );
  }

  // Do we have a swapchain?
  if (data->SwapChain != nullptr)
  {
    extern bool
      SKIF_bHDREnabled;
      SKIF_bHDREnabled = false;

    data->DXGIFormat = swap_desc.Format;

    CComQIPtr <IDXGISwapChain3>
        pSwapChain3 (data->SwapChain);

    if (pSwapChain3 != nullptr)
    {
      // Are we on a flip based model?
      if (swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD   ||
          swap_desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL )
      {
        // Retrieve the current default adapter.
        CComPtr   <IDXGIOutput>   pOutput;
        CComPtr   <IDXGIAdapter1> pAdapter;
        CComQIPtr <IDXGIFactory2> pFactory1 (g_pFactory);
        ThrowIfFailed (pFactory1->EnumAdapters1 (0, &pAdapter));

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
        while (pAdapter->EnumOutputs(i, &currentOutput) != DXGI_ERROR_NOT_FOUND)
        {
          // Get the retangle bounds of the app window
          int ax1 = m_windowBounds.left;
          int ay1 = m_windowBounds.top;
          int ax2 = m_windowBounds.right;
          int ay2 = m_windowBounds.bottom;

          // Get the rectangle bounds of current output
          DXGI_OUTPUT_DESC desc;
          ThrowIfFailed(currentOutput->GetDesc(&desc));
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
        CComQIPtr <IDXGIOutput6>
            pOutput6 (pOutput);

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

            pSwapChain3->CheckColorSpaceSupport (dxgi_cst, &uiHdrFlags);

            if ( DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT ==
                  ( uiHdrFlags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT )
                )
            {
              pOutput6->GetDesc1 (&data->DXGIDesc);

              // Is the output display in HDR mode?
              if (data->DXGIDesc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
              {
                data->HDR     = true;
                data->HDRMode = _registry.iHDRMode;

                pSwapChain3->SetColorSpace1 (dxgi_cst);

                pOutput6->GetDesc1 (&data->DXGIDesc);

                SKIF_bHDREnabled = true;
              }
            }
          }

          else {
            OutputDebugString(L"Derp\n");
            // AMD flickering bug fix?
            if (swap_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
              pSwapChain3->SetColorSpace1 (DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
            else
              pSwapChain3->SetColorSpace1 (DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
          }
        }
#pragma endregion
      }
    }

    if (! data->HDR)
      data->SDRMode = _registry.iSDRMode;

    CComPtr <
      ID3D11Texture2D
    >              pBackBuffer;
    data->SwapChain->GetBuffer ( 0, IID_PPV_ARGS (
                  &pBackBuffer.p                 )
                               );
    g_pd3dDevice->CreateRenderTargetView ( pBackBuffer, nullptr,
                           &data->RTView );

    // Only print swapchain info for the main swapchain
    extern HWND SKIF_ImGui_hWnd;
    if (hWnd == SKIF_ImGui_hWnd)
    {
      ImGui_ImplDX11_LogSwapChainFormat (viewport);
    }

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
        if (SUCCEEDED (pSwapChain2->SetMaximumFrameLatency (1)))
        {
          data->WaitHandle =
            pSwapChain2->GetFrameLatencyWaitableObject  ( );

          if (data->WaitHandle)
          {
            vSwapchainWaitHandles.push_back (data->WaitHandle);

            // One-time wait to align the thread for minimum latency (reduces latency by half in testing)
            //WaitForSingleObjectEx (data->WaitHandle, 1000, true);
            WaitForSingleObject (data->WaitHandle, 1000);
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

static void
ImGui_ImplDX11_DestroyWindow (ImGuiViewport *viewport)
{
  // The main viewport (owned by the application) will always have RendererUserData == NULL since we didn't create the data for it.
  ImGuiViewportDataDx11 *data =
    static_cast <ImGuiViewportDataDx11 *> (
                      viewport->RendererUserData
    );

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
ImGui_ImplDX11_SetWindowSize ( ImGuiViewport *viewport,
                               ImVec2         size )
{
  ImGuiViewportDataDx11 *data =
    static_cast <ImGuiViewportDataDx11 *> (
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

      g_pd3dDevice->CreateRenderTargetView ( pBackBuffer,
                    nullptr, &data->RTView );
    }
  }
}

static void
ImGui_ImplDX11_RenderWindow ( ImGuiViewport *viewport,
                                       void * )
{
  ImGuiViewportDataDx11 *data =
    static_cast <ImGuiViewportDataDx11 *> (
                      viewport->RendererUserData
    );
  
  if (data            == nullptr || // Win32 window was destroyed
      data->SwapChain == nullptr || // Swapchain was destroyed
      data->RTView    == nullptr)   // Render target view was destroyed
  {
    RecreateSwapChains = true;
    return;
  }

  ImVec4 clear_color =
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg]; // Use the current window bg color to clear the RTV with
  //ImVec4 (0.0f, 0.0f, 0.0f, 1.0f);

  g_pd3dDeviceContext->OMSetRenderTargets ( 1,
                &data->RTView,    nullptr );

  if (! (viewport->Flags & ImGuiViewportFlags_NoRendererClear))
  {
    g_pd3dDeviceContext->ClearRenderTargetView (
                                  data->RTView,
                         (float *)&clear_color );
  }

  ImGui_ImplDX11_RenderDrawData (viewport->DrawData);

  g_pd3dDeviceContext->OMSetRenderTargets ( 0,
                      nullptr,    nullptr );
}

static void
ImGui_ImplDX11_SwapBuffers ( ImGuiViewport *viewport,
                                      void * )
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  ImGuiViewportDataDx11 *data =
    static_cast <ImGuiViewportDataDx11 *> (
                      viewport->RendererUserData
    );

  if (data            != nullptr && // Win32 window was destroyed
      data->SwapChain != nullptr)   // Swapchain was destroyed
  {
    DXGI_SWAP_CHAIN_DESC1       swap_desc = { };
    data->SwapChain->GetDesc1 (&swap_desc);

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
      if (Interval == 0 && SKIF_bCanAllowTearing)
        PresentFlags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    //if (data->WaitHandle)
    //  WaitForSingleObject (data->WaitHandle, INFINITE);

    if (SUCCEEDED (data->SwapChain->Present (Interval, PresentFlags)))
      data->PresentCount++;

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
}

static void
ImGui_ImplDX11_InitPlatformInterface (void)
{
  ImGuiPlatformIO &platform_io =
    ImGui::GetPlatformIO ();

  platform_io.Renderer_CreateWindow  = ImGui_ImplDX11_CreateWindow;
  platform_io.Renderer_DestroyWindow = ImGui_ImplDX11_DestroyWindow;
  platform_io.Renderer_SetWindowSize = ImGui_ImplDX11_SetWindowSize;
  platform_io.Renderer_RenderWindow  = ImGui_ImplDX11_RenderWindow;
  platform_io.Renderer_SwapBuffers   = ImGui_ImplDX11_SwapBuffers;
}

static void
ImGui_ImplDX11_ShutdownPlatformInterface (void)
{
  ImGui::DestroyPlatformWindows ();
}

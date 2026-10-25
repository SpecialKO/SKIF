#pragma once

#include <combaseapi.h>
#include <atlbase.h>
#include <d3d11.h>

template <class _Tp>
struct tex_ref_s {
  float getAspectRatio (void);
};

// A D3D11 texture (shader resource view) that caches the description of its texture
template <>
struct tex_ref_s <ID3D11ShaderResourceView> :
         CComPtr <ID3D11ShaderResourceView>
{
  D3D11_TEXTURE2D_DESC& getDesc (void)
  {
    if (texDesc.Width == 0)
    {
      if (p != nullptr)
      {
        CComPtr         <ID3D11Texture2D>   pTex;
        p->GetResource ((ID3D11Resource **)&pTex.p);

        if (pTex.p != nullptr)
            pTex->GetDesc (&texDesc);
      }
    }

    return texDesc;
  }

  float getAspectRatio (void)
  {
    return
      static_cast <float> (getDesc ().Width) /
      static_cast <float> (getDesc ().Height);
  }

private:
  D3D11_TEXTURE2D_DESC texDesc = { };
};

using d3d11_tex_ref_s =
            tex_ref_s <ID3D11ShaderResourceView>;

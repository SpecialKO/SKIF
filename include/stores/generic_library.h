#pragma once

#include <combaseapi.h>

interface ID3D11ShaderResourceView;

template <class _Tp>
struct tex_ref_s {
  float getAspectRatio (void);
};
#pragma warning ( disable : 3571 )
struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
  float2 uv2 : TEXCOORD1;
  float4 uv3 : TEXCOORD2; // constant_buffer->luminance_scale ?
};

cbuffer viewportDims  : register (b0)
{
  float4 viewport;
};

sampler   sampler0    : register (s0);

Texture2D texture0    : register (t0);
Texture2D hdrUnderlay : register (t1);
Texture2D hdrHUD      : register (t2);

#define FLT_EPSILON     1.192092896e-07 // Smallest positive number, such that 1.0 + FLT_EPSILON != 1.0

float
RemoveSRGBAlpha (float a)
{
  return        ( a < 0.04045f ) ?
                  a / 12.92f     :
          pow ( ( a + 0.055f   ) / 1.055f,
                                   2.4f );
}

float3
RemoveSRGBCurve (float3 x)
{
  // Negative values can come back negative after gamma, unlike pow (...).
  x = /* High-Pass filter the input to clip negative values to 0 */
    max ( 0.0, isfinite (x) ? x : 0.0 );

  // Piecewise is more accurate, but the fitted power-law curve will not
  //   create near-black noise when expanding LDR -> HDR
#define ACCURATE_AND_NOISY
#ifdef  ACCURATE_AND_NOISY
  return ( x < 0.04045f ) ?
          (x / 12.92f)    : // High-pass filter x or gamma will return negative!
    pow ( (x + 0.055f) / 1.055f, 2.4f );
#else
  // This suffers the same problem as piecewise; x * x * x allows negative color.
  return max (0.0, x * (x * (x * 0.305306011 + 0.682171111) + 0.012522878));
#endif
}

float3 ApplyREC709Curve (float3 x)
{
  return x < 0.0181 ? 4.5 * x : 1.0993 * pow(x, 0.45) - 0.0993;
}

float Luma (float3 color)
{
  return
    dot (color, float3 (0.299f, 0.587f, 0.114f));
}

float3 ApplyREC2084Curve (float3 L, float maxLuminance)
{
  float m1 = 2610.0 / 4096.0 / 4;
  float m2 = 2523.0 / 4096.0 * 128;
  float c1 = 3424.0 / 4096.0;
  float c2 = 2413.0 / 4096.0 * 32;
  float c3 = 2392.0 / 4096.0 * 32;

  float maxLuminanceScale = maxLuminance / 10000.0f;
  L *= maxLuminanceScale;

  float3 Lp = pow (L, m1);

  return pow ((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 RemoveREC2084Curve (float3 N)
{
  float  m1 = 2610.0 / 4096.0 / 4;
  float  m2 = 2523.0 / 4096.0 * 128;
  float  c1 = 3424.0 / 4096.0;
  float  c2 = 2413.0 / 4096.0 * 32;
  float  c3 = 2392.0 / 4096.0 * 32;
  float3 Np = pow (N, 1 / m2);

  return
    pow (max (Np - c1, 0) / (c2 - c3 * Np), 1 / m1);
}

// Apply the ST.2084 curve to normalized linear values and outputs normalized non-linear values
float3 LinearToST2084 (float3 normalizedLinearValue)
{
    return pow((0.8359375f + 18.8515625f * pow(abs(normalizedLinearValue), 0.1593017578f)) / (1.0f + 18.6875f * pow(abs(normalizedLinearValue), 0.1593017578f)), 78.84375f);
}

// ST.2084 to linear, resulting in a linear normalized value
float3 ST2084ToLinear (float3 ST2084)
{
    return pow(max(pow(abs(ST2084), 1.0f / 78.84375f) - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * pow(abs(ST2084), 1.0f / 78.84375f)), 1.0f / 0.1593017578f);
}

float3 REC709toREC2020 (float3 RGB709)
{
  static const float3x3 ConvMat =
  {
    0.627401924722236,  0.329291971755002,  0.0433061035227622,
    0.0690954897392608, 0.919544281267395,  0.0113602289933443,
    0.0163937090881632, 0.0880281623979006, 0.895578128513936
  };
  return mul (ConvMat, RGB709);
}

float3 REC2020toREC709 (float3 RGB2020)
{
  static const float3x3 ConvMat =
  {
     1.66049621914783,   -0.587656444131135, -0.0728397750166941,
    -0.124547095586012,   1.13289510924730,  -0.00834801366128445,
    -0.0181536813870718, -0.100597371685743,  1.11875105307281
  };
  return mul (ConvMat, RGB2020);
}

float4 main (PS_INPUT input) : SV_Target
{
  float4 out_col  =
    texture0.Sample (sampler0, input.uv);
  float4 orig_col = out_col;

  bool isHDR    = input.uv3.y > 0.0; // 10 bpc or 16 bpc HDR
  bool isHDR10  = input.uv3.x < 0.0; // 10 bpc HDR
  bool isSRGB   = input.uv3.w > 0.0; // scRGB 16 bpc SDR
  
#if 0 // Test crap
  
  // Convert sRGB to Linear
  //if (isSRGB)
  //  out_col = pow (out_col, 2.2f);
  
  //out_col.rgb = (1 - out_col.rgb) * input.col.rgb;
  
  //out_col.rgb *= out_col.a;
  //out_col.r = pow(out_col.r, 2.2f);
  //out_col.g = pow(out_col.g, 2.2f);
  //out_col.b = pow(out_col.b, 2.2f);
  
  // 16 bpc scRGB SDR
  if (isSRGB)
  {
    out_col.rgb *= out_col.a;
    /*
    out_col =
      float4 ( RemoveSRGBCurve (input.col.rgb) *
               RemoveSRGBCurve (  out_col.rgb),
               RemoveSRGBAlpha (  out_col.a) *
               RemoveSRGBAlpha (input.col.a)
              );
    
    out_col.a = 1.0 - pow (1.0 - out_col.a, 2.4);
    */
  }
  
  // 8-10 bpc SDR
  else
  {
    out_col =
      float4 (                            input.col.rgb *
                                            out_col.rgb,
                            pow (saturate (  out_col.a) *
                                 saturate (input.col.a), 0.8)
              );
  }

  
  return out_col;
  
#endif
  
  
  
#if 1 // Live
  
  if (viewport.z > 0.f)
  {
    float4 orig_col = out_col;
    
    if (input.uv2.x > 0.0f && input.uv2.y > 0.0f)
    {
      out_col.rgb =
        pow (
          RemoveSRGBCurve (out_col.rgb),
                input.uv2.yyy
            ) * input.uv2.xxx;
      out_col.a   = 1.0f;
    }
    
    else
    {
      out_col =
        float4 ( RemoveSRGBCurve (          input.col.rgb) *
                 RemoveSRGBCurve (            out_col.rgb),
                             pow (saturate (  out_col.a) *
                                  saturate (input.col.a), 0.8)
               );
    }

    float hdr_scale = isHDR10 ? (-input.uv3.x / 10000.0)
                             :    input.uv3.x;

    // Do not use; EDID minimum black level is -ALWAYS- wrong...
    float hdr_offset = 0.0f; // isHDR10 ? 0.0f : input.uv3.z / 80.0;

    hdr_scale -= hdr_offset;

    out_col.rgba =
      float4 (   ( isHDR10 ?
        LinearToST2084 (
          REC709toREC2020 ( saturate (out_col.rgb) ) * hdr_scale
                       ) :  saturate (out_col.rgb)   * hdr_scale
                 )                                   + hdr_offset,
                            saturate (out_col.a  ) );

    out_col.r = (orig_col.r < 0.000001f) ? 0.0f : out_col.r;
    out_col.g = (orig_col.g < 0.000001f) ? 0.0f : out_col.g;
    out_col.b = (orig_col.b < 0.000001f) ? 0.0f : out_col.b;

    return
      out_col.rgba;
  }
  
  float4 return_col = isSRGB
      ? float4 (RemoveSRGBCurve (input.col.rgb), input.col.a) * float4 (RemoveSRGBCurve (out_col.rgb), out_col.a)
      :                        ( input.col                    *                          out_col );
  
  //if (isSRGB)
  //  return_col.a = pow (return_col.a, 1 / 2.2f);

  return return_col;
#endif
  
};
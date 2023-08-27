#pragma warning ( disable : 3571 )
struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
  float2 uv2 : TEXCOORD1;
  float4 uv3 : TEXCOORD2;
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

float3
ApplySRGBCurve (float3 x)
{
  return ( x < 0.0031308f ? 12.92f * x :
                            1.055f * pow ( x, 1.0 / 2.4f ) - 0.55f );
}

float
ApplySRGBAlpha (float a)
{
    return ( a < 0.0031308f ? 12.92f * a :
                              1.055f * pow ( a, 1.0 / 2.4f ) - 0.55f );
}

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

  return ( x < 0.04045f ) ?
          (x / 12.92f)    : // High-pass filter x or gamma will return negative!
    pow ( (x + 0.055f) / 1.055f, 2.4f );
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

float3 REC2020toREC709 (float3 RGB2020)
{
  static const float3x3 ConvMat =
  {
     1.66096379471340,   -0.588112737547978, -0.0728510571654192,
    -0.124477196529907,   1.13281946828499,  -0.00834227175508652,
    -0.0181571579858552, -0.100666415661988,  1.11882357364784
  };
  return mul (ConvMat, RGB2020);
}

float3
REC709toREC2020 (float3 RGB709)
{
  static const float3x3 ConvMat =
  {
    0.627225305694944,  0.329476882715808,  0.0432978115892484,
    0.0690418812810714, 0.919605681354755,  0.0113524373641739,
    0.0163911702607078, 0.0880887513437058, 0.895520078395586
  };

  return mul (ConvMat, RGB709);
}

// Expand bright saturated colors outside the sRGB (REC.709) gamut to fake wide gamut rendering (BT.2020).
// Inspired by Unreal Engine 4/5 (ACES).
// Input (and output) needs to be in sRGB linear space.
// Calling this with a fExpandGamut value of 0 still results in changes (it's actually an edge case, don't call it).
// Calling this with fExpandGamut values above 1 yields diminishing returns.
float3
expandGamut (float3 vHDRColor, float fExpandGamut = 1.0f)
{
  // Some similar constants are defined above
  const float3x3 XYZ_2_sRGB_MAT =
  {
     3.2409699419, -1.5373831776, -0.4986107603,
    -0.9692436363,  1.8759675015,  0.0415550574,
     0.0556300797, -0.2039769589,  1.0569715142,
  };
  const float3x3 sRGB_2_XYZ_MAT =
  {
    0.4124564, 0.3575761, 0.1804375,
    0.2126729, 0.7151522, 0.0721750,
    0.0193339, 0.1191920, 0.9503041,
  };
  const float3x3 XYZ_2_AP1_MAT =
  {
     1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
     0.0117218943, -0.0082844420,  0.9883948585,
  };
  const float3x3 D65_2_D60_CAT =
  {
     1.01303,    0.00610531, -0.014971,
     0.00769823, 0.998165,   -0.00503203,
    -0.00284131, 0.00468516,  0.924507,
  };
  const float3x3 D60_2_D65_CAT =
  {
     0.987224,   -0.00611327, 0.0159533,
    -0.00759836,  1.00186,    0.00533002,
     0.00307257, -0.00509595, 1.08168,
  };
  const float3x3 AP1_2_XYZ_MAT =
  {
     0.6624541811, 0.1340042065, 0.1561876870,
     0.2722287168, 0.6740817658, 0.0536895174,
    -0.0055746495, 0.0040607335, 1.0103391003,
  };  
  const float3 AP1_RGB2Y =
  {
    0.2722287168, //AP1_2_XYZ_MAT[0][1],
    0.6740817658, //AP1_2_XYZ_MAT[1][1],
    0.0536895174, //AP1_2_XYZ_MAT[2][1]
  };
  // Bizarre matrix but this expands sRGB to between P3 and AP1
  // CIE 1931 chromaticities:    x        y
  //                Red:        0.6965    0.3065
  //                Green:        0.245    0.718
  //                Blue:        0.1302    0.0456
  //                White:        0.3127    0.329
  const float3x3 Wide_2_XYZ_MAT =
  {
     0.5441691, 0.2395926, 0.1666943,
     0.2394656, 0.7021530, 0.0583814,
    -0.0023439, 0.0361834, 1.0552183,
  };
  const float3x3 sRGB_2_AP1 = mul (XYZ_2_AP1_MAT,  mul (D65_2_D60_CAT, sRGB_2_XYZ_MAT));
  const float3x3 AP1_2_sRGB = mul (XYZ_2_sRGB_MAT, mul (D60_2_D65_CAT, AP1_2_XYZ_MAT));
  const float3x3 Wide_2_AP1 = mul (XYZ_2_AP1_MAT, Wide_2_XYZ_MAT);
  const float3x3 ExpandMat  = mul (Wide_2_AP1,    AP1_2_sRGB);

  float3 ColorAP1  = mul (sRGB_2_AP1, vHDRColor);

  float  LumaAP1   = dot (ColorAP1, AP1_RGB2Y);
  float3 ChromaAP1 =      ColorAP1 / LumaAP1;

  float ChromaDistSqr = dot (ChromaAP1 - 1, ChromaAP1 - 1);
  float ExpandAmount  = (1 - exp2 (-4 * ChromaDistSqr)) * (1 - exp2 (-4 * fExpandGamut * LumaAP1 * LumaAP1));

  float3 ColorExpand = mul (ExpandMat, ColorAP1);
  
  ColorAP1 = lerp (ColorAP1, ColorExpand, ExpandAmount);

  vHDRColor =
    mul (AP1_2_sRGB, ColorAP1);
  
  return vHDRColor;
}

bool IsFinite (float x)
{
  return
    (asuint (x) & 0x7F800000) != 0x7F800000;
}

bool IsInf (float x)
{
  return
    (asuint (x) & 0x7FFFFFFF) == 0x7F800000;
}

// NaN checker
bool IsNan (float x)
{
  return
    (asuint (x) & 0x7fffffff) > 0x7f800000;
}

bool AnyIsNan (float2 x)
{
  return IsNan (x.x) ||
         IsNan (x.y);
}

bool AnyIsNan (float3 x)
{
  return IsNan (x.x) ||
         IsNan (x.y) ||
         IsNan (x.z);
}

bool AnyIsNan (float4 x)
{
  return IsNan (x.x) ||
         IsNan (x.y) ||
         IsNan (x.z) ||
         IsNan (x.w);
}

#define FP16_MIN 0.00000009

float3 Clamp_scRGB (float3 c)
{
  // Clamp to Rec 2020
  return
    REC2020toREC709 (
      max (REC709toREC2020 (c), FP16_MIN)
    );
}

float3 Clamp_scRGB_StripNaN (float3 c)
{
  // Remove special floating-point bit patterns, clamping is the
  //   final step before output and outputting NaN or Infinity would
  //     break color blending!
  c =
    float3 ( (! IsNan (c.r)) * (! IsInf (c.r)) * c.r,
             (! IsNan (c.g)) * (! IsInf (c.g)) * c.g,
             (! IsNan (c.b)) * (! IsInf (c.b)) * c.b );
   
  return Clamp_scRGB (c);
}

float4 main (PS_INPUT input) : SV_Target
{
  float4 out_col =
    texture0.Sample (sampler0, input.uv);
  
  out_col =
    float4 ( RemoveSRGBCurve ( saturate (input.col.rgb) ) *
             RemoveSRGBCurve ( saturate (  out_col.rgb) ) * out_col.a * input.col.a,
                                                            out_col.a * input.col.a
           );
  
  out_col.a = 1.0 - RemoveSRGBAlpha (1.0 - out_col.a);

  bool hdr10      = ( input.uv3.x < 0.0 );
  bool linear_sdr =   input.uv3.w > 0;

  // HDR
  if (viewport.z > 0.f)
  {
    float4 orig_col  = out_col;
    float hdr_scale  = hdr10 ? ( -input.uv3.x / 10000.0 )
                             :    input.uv3.x;

    // Do not use; EDID minimum black level is -ALWAYS- wrong...
    float hdr_offset = 0.0f; // hdr10 ? 0.0f : input.uv3.z / 80.0;

    hdr_scale -= hdr_offset;

    out_col.rgba =
      float4 (   ( hdr10 ?
        LinearToST2084 (
          REC709toREC2020 (              saturate (out_col.rgb) ) * hdr_scale
                       ) :
     Clamp_scRGB_StripNaN ( expandGamut (saturate (out_col.rgb)   * hdr_scale, 0.05) )
                 )                                   + hdr_offset,
                                         saturate (out_col.a) );

    out_col.r *= (orig_col.r >= FLT_EPSILON);
    out_col.g *= (orig_col.g >= FLT_EPSILON);
    out_col.b *= (orig_col.b >= FLT_EPSILON);
    out_col.a *= (orig_col.a >= FLT_EPSILON);

    return
      out_col.rgba;
  }
  
  return out_col;
};
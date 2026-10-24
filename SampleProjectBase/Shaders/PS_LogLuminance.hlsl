// Auto exposure, step 1: the log of each pixel's brightness (AutoExposure.cpp).
// The mip chain of this image then averages it down to 1 pixel = the average of log(luminance),
// i.e. the GEOMETRIC mean brightness of the frame. The log average is the standard choice
// (Reinhard 2002): one tiny very bright spot (the window, the forge mouth) does not dominate the
// average the way it would with a plain mean.
// ASCII comments only so this file compiles regardless of code page.
Texture2D    sceneTex : register(t0);
SamplerState samp     : register(s0);
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };

static const float3 LUMA_709 = float3(0.2126, 0.7152, 0.0722);   // Rec.709 luminance weights
static const float  MIN_LUMA = 0.0001;                           // log(0) guard (pure black pixels)

float4 main(PIN i) : SV_TARGET
{
    float luma = dot(sceneTex.SampleLevel(samp, i.uv, 0).rgb, LUMA_709);
    return float4(log(max(luma, MIN_LUMA)), 0, 0, 1);
}

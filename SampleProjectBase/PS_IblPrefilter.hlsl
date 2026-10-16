// IBL step 2a: prefiltered environment map (one-time bake, ReflectionProbe::Prefilter).
// Writes ONE face of ONE mip level of the prefiltered cube. Each mip = one roughness:
//   mip 0 = roughness 0 (mirror: the captured photo as-is) ... last mip = roughness 1 (very blurry).
// Every texel averages the captured cube over the directions a GGX lobe of that roughness would
// reflect -> the same blur a rough metal really shows (sharp center, soft tail), not a box blur.
// Karis 2013 split-sum, with N = V = R (the standard simplification: the lobe is assumed to look
// straight back along the normal; costs a little accuracy at grazing angles, makes it pre-computable).
// ASCII comments only so this file compiles regardless of code page.
#include "IblCommon.hlsli"

TextureCube  src  : register(t0);   // captured cube (with plain mips for the lookup below)
SamplerState samp : register(s1);   // IBL sampler: trilinear, clamp, no LOD bias
cbuffer Bake : register(b0)
{
    float roughness;   // this mip's roughness (perceptual, same as the material table)
    float face;        // which cube face (0..5 = +X -X +Y -Y +Z -Z)
    float srcSize;     // captured face size in pixels
    float srcMips;     // captured mip count
};
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };

static const uint  SAMPLE_COUNT = 256;   // one-time bake: quality over speed (still a few ms)
static const float MIN_WEIGHT   = 0.0001;

// Direction through texel uv of a cube face, D3D layout (matches the capture's look/up per face).
float3 FaceDirection(float2 uv, int f)
{
    float a = uv.x * 2 - 1, b = uv.y * 2 - 1;
    if (f == 0) return float3( 1, -b, -a);
    if (f == 1) return float3(-1, -b,  a);
    if (f == 2) return float3( a,  1,  b);
    if (f == 3) return float3( a, -1, -b);
    if (f == 4) return float3( a, -b,  1);
    return            float3(-a, -b, -1);
}

float4 main(PIN i) : SV_TARGET
{
    float3 n = normalize(FaceDirection(i.uv, (int)face));
    if (roughness <= 0.0) return float4(src.SampleLevel(samp, n, 0).rgb, 1);   // mirror = the photo

    float a = roughness * roughness;
    float3 sum = 0;
    float  weight = 0;
    // Solid angle of one captured texel; each sample reads the captured mip whose texel covers about
    // the same solid angle as the sample ("filtered importance sampling", GPU Gems 3 ch.20). Without
    // it, a few bright texels (the forge mouth) show up as speckles in the blurred levels.
    float texelSolidAngle = 4.0 * IBL_PI / (6.0 * srcSize * srcSize);
    for (uint k = 0; k < SAMPLE_COUNT; ++k)
    {
        float3 h = ImportanceSampleGGX(Hammersley(k, SAMPLE_COUNT), n, a);
        float3 l = normalize(2.0 * dot(n, h) * h - n);   // reflect the view (= n) about h
        float nl = saturate(dot(n, l));
        if (nl <= 0) continue;
        float nh = saturate(dot(n, h));
        float d = a * a / (IBL_PI * pow(nh * nh * (a * a - 1) + 1, 2));
        float pdf = d * nh / (4.0 * nh) + MIN_WEIGHT;   // (v.h = n.h because v = n)
        float sampleSolidAngle = 1.0 / (SAMPLE_COUNT * pdf + MIN_WEIGHT);
        float mip = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0, srcMips - 1);
        sum += src.SampleLevel(samp, l, mip).rgb * nl;
        weight += nl;
    }
    return float4(sum / max(weight, MIN_WEIGHT), 1);
}

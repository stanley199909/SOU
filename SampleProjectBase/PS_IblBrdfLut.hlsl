// IBL step 2b: BRDF lookup table (one-time bake, ReflectionProbe::BakeBrdfLut).
// x = n.v (viewing angle), y = roughness. Stores (A, B) so that at run time
//     specular reflection = prefiltered environment x (F0 * A + B)
// i.e. the "how much of the environment comes back" half of the split-sum, integrated for every
// angle/roughness once. It does not depend on the scene, only on the BRDF.
// Karis 2013 (black box: the standard formula, copied, not tuned). The image should look like the
// well-known red/green gradient (search "BRDF LUT split sum").
// ASCII comments only so this file compiles regardless of code page.
#include "IblCommon.hlsli"

struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
static const uint  SAMPLE_COUNT = 512;
static const float MIN_NV = 0.0001;

// Smith-Schlick geometry term with the IBL remapping k = alpha / 2 (Karis 2013).
float GeometrySchlick(float nx, float k) { return nx / (nx * (1 - k) + k); }

float4 main(PIN i) : SV_TARGET
{
    float nv = max(i.uv.x, MIN_NV);
    float rough = i.uv.y;
    float a = rough * rough;
    float k = a / 2.0;
    float3 v = float3(sqrt(1 - nv * nv), 0, nv);   // tangent space, n = +z
    float3 n = float3(0, 0, 1);
    float A = 0, B = 0;
    for (uint s = 0; s < SAMPLE_COUNT; ++s)
    {
        float3 h = ImportanceSampleGGX(Hammersley(s, SAMPLE_COUNT), n, a);
        float3 l = normalize(2 * dot(v, h) * h - v);
        float nl = saturate(l.z), nh = saturate(h.z), vh = saturate(dot(v, h));
        if (nl <= 0) continue;
        float g = GeometrySchlick(nv, k) * GeometrySchlick(nl, k);
        float gVis = g * vh / max(nh * nv, MIN_NV);
        float fc = pow(1 - vh, 5);
        A += (1 - fc) * gVis;
        B += fc * gVis;
    }
    return float4(A / SAMPLE_COUNT, B / SAMPLE_COUNT, 0, 1);
}

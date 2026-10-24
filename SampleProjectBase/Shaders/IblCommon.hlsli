// IblCommon.hlsli -- helpers shared by the two one-time IBL bakes (PS_IblPrefilter, PS_IblBrdfLut).
// Standard formulas from Karis 2013 "Real Shading in Unreal Engine 4" (black box: copy, do not tune).
// ASCII comments only so this file compiles regardless of code page.

static const float IBL_PI = 3.14159265;

// Hammersley point set: N well-spread 2D points in [0,1)^2 (low-discrepancy sequence).
// Spreads the samples evenly instead of clumping like random numbers -> less noise for the same count.
float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;   // / 2^32
}
float2 Hammersley(uint i, uint n) { return float2(float(i) / float(n), RadicalInverseVdC(i)); }

// GGX importance sampling: turn a 2D point into a micro-facet normal (half vector) around n,
// picked with the same probability as the GGX distribution (more samples where the highlight is).
// a = alpha = roughness^2 (the same convention as BRDF() in Lighting.hlsli).
float3 ImportanceSampleGGX(float2 xi, float3 n, float a)
{
    float phi = 2.0 * IBL_PI * xi.x;
    float cosT = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    float3 h = float3(sinT * cos(phi), sinT * sin(phi), cosT);   // tangent space (z = n)
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tx = normalize(cross(up, n));
    float3 ty = cross(n, tx);
    return normalize(tx * h.x + ty * h.y + n * h.z);
}

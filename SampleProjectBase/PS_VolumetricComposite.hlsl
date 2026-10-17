// Volumetric sunlight, pass 2 of 2: blur the quarter-resolution result a little and ADD it onto the
// scene (additive blend: scattered light is extra light reaching the eye, it never darkens anything).
// The 3x3 tent blur hides the per-pixel step jitter of the march pass.
// ASCII comments only so this file compiles regardless of code page.
Texture2D    volTex : register(t0);
SamplerState samp   : register(s0);
cbuffer Composite : register(b0) { float4 texel; };   // xy = one texel of volTex in UV, z = strength
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };

static const float TENT[3] = { 0.25, 0.5, 0.25 };   // 1-2-1 weights per axis (sum 1)

float4 main(PIN i) : SV_TARGET
{
    float3 sum = 0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += volTex.SampleLevel(samp, i.uv + float2(x, y) * texel.xy, 0).rgb * TENT[x + 1] * TENT[y + 1];
    return float4(sum * texel.z, 1);
}

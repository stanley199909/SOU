// Debug mirror ball: a perfect mirror that shows what the reflection probe captured.
// Reflect the view ray off the sphere and look the direction up in the cube.
// ASCII comments only so this file compiles regardless of code page.
TextureCube  envCube : register(t0);
SamplerState samp    : register(s1);   // IBL sampler (trilinear, clamp, no LOD bias)
cbuffer Ball : register(b0) { float4 eye; };   // xyz camera position, w mip to show (= roughness level)
struct PIN { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float3 wp:TEXCOORD1; };
float4 main(PIN i) : SV_TARGET
{
    float3 n = normalize(i.nrm);
    float3 v = normalize(i.wp - eye.xyz);   // camera -> surface
    float3 r = reflect(v, n);
    return float4(envCube.SampleLevel(samp, r, eye.w).rgb, 1);
}

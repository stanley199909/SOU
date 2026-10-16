// Debug mirror ball at the reflection probe (ReflectionProbe::DrawDebugBall).
// ASCII comments only so this file compiles regardless of code page.
cbuffer Cam : register(b0) { float4x4 world; float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float3 nrm:NORMAL0; };
struct VOUT { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float3 wp:TEXCOORD1; };
VOUT main(VIN v)
{
    VOUT o;
    float4 w = mul(float4(v.pos, 1), world);
    o.wp  = w.xyz;
    o.pos = mul(mul(w, view), proj);
    o.nrm = mul(v.nrm, (float3x3)world);
    return o;
}

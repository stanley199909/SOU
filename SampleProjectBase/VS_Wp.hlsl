// Blade being forged (morphing mesh). Vertices are already in WORLD space (the CPU morphs and
// transforms them every frame), so only view/proj are applied here.
// Used by PS_Wp (the blade) and PS_Ghost (the target outline).
// ASCII comments only so this file compiles regardless of code page.
cbuffer Cam : register(b0) { float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float3 nrm:NORMAL0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; float sharp:TEXCOORD2; float work:TEXCOORD3; float temper:TEXCOORD4; };
struct VOUT { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; float sharp:TEXCOORD4; float work:TEXCOORD5; float temper:TEXCOORD6; };
VOUT main(VIN v)
{
    VOUT o;
    o.pos = mul(mul(float4(v.pos, 1), view), proj);
    o.nrm = v.nrm;
    o.uv = v.uv;
    o.col = v.col;      // rgb = heat color (HeatRGB), a = temperature 0..1
    o.wp = v.pos;
    o.sharp = v.sharp;  // 0 = as forged .. 1 = ground to an edge
    o.work = v.work;    // forging progress of this face (scale falls off as it rises)
    o.temper = v.temper; // tempering: oxide color position 0..1 (silver -> straw -> bronze -> purple -> blue)
    return o;
}

// Vertex shader for the trough water: a grid mesh lifted by the wave simulation.
// Physics/WaterSim solves the 2D wave equation on the CPU; its height field is uploaded
// every frame as a small R32_FLOAT texture. Each grid vertex moves up/down by the height
// at its UV, so the surface really bends (the pixel shader takes the normal from the same
// heights). Same vertex layout as VS_Coal: SceneForge::Vertex { float3 pos; float2 uv; float4 col; }.
struct VS_IN
{
    float3 pos : POSITION0;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;   // unused here (shared vertex struct)
};

struct VS_OUT
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

cbuffer WVP : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 proj;
};

Texture2D    heightTex : register(t0);   // WaterSim heights (m), u = length, v = width
SamplerState samp      : register(s0);

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    float4 p = mul(float4(vin.pos, 1.0f), world);                 // flat surface -> world
    p.y += heightTex.SampleLevel(samp, vin.uv, 0).r;              // SampleLevel: no derivatives in a VS
    vout.worldPos = p.xyz;
    vout.pos = mul(mul(p, view), proj);
    vout.uv  = vin.uv;
    return vout;
}

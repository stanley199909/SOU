// Vertex shader for glowing billboard particles (forge sparks and coal embers).
// The CPU builds camera-facing quads, so the vertex positions already arrive in
// WORLD space -> there is no world matrix here, only view and projection.
// Input order/types must match SceneForge::Vertex { float3 pos; float2 uv; float4 col; }.
struct VS_IN
{
    float3 pos : POSITION0;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;
};

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;
};

cbuffer Cam : register(b0)
{
    float4x4 view;
    float4x4 proj;
};

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    float4 p = float4(vin.pos, 1.0f);   // already world space
    p = mul(p, view);
    vout.pos = mul(p, proj);
    vout.uv  = vin.uv;
    vout.col = vin.col;                 // per-particle color (carries the life fade)
    return vout;
}

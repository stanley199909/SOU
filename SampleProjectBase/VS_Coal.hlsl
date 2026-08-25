// Vertex shader for the glowing coal bed (and later the water surface).
// The input layout is auto-generated from this VS_IN signature by D3DReflect
// (see Shader.cpp), so the field ORDER and TYPES must match the CPU-side
// vertex struct: SceneForge::Vertex { float3 pos; float2 uv; float4 col; }.
struct VS_IN
{
    float3 pos : POSITION0;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;   // unused here, kept so the layout matches the vertex buffer
};

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// World / View / Projection, written from the CPU via WriteBuffer(0, mat).
cbuffer WVP : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 proj;
};

VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    float4 p = float4(vin.pos, 1.0f);   // local space
    p = mul(p, world);                  // -> world space
    p = mul(p, view);                   // -> view space
    vout.pos = mul(p, proj);            // -> clip / screen space
    vout.uv  = vin.uv;                  // pass UV to the pixel shader (interpolated)
    return vout;
}

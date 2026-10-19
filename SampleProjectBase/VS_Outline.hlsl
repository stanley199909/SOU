// Outline of one object by the inverted hull method ("inverted hull outline").
// The same mesh is pushed out along its normals by a fixed distance (world metres) and only its
// BACK faces are drawn (front-face culling on the CPU side). Where the real object covers it, the
// depth test hides it; only the thin rim that sticks out around the silhouette stays = the outline.
// Used to point at the next station (SceneForge: station highlight).
// ASCII comments only so this file compiles regardless of code page.
struct VS_IN
{
    float3 pos    : POSITION0;
    float3 normal : NORMAL0;
    float2 uv     : TEXCOORD0;
};

cbuffer OutlineVS : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 proj;
    float4   params;   // x = how far the hull is pushed out (world metres) = outline thickness
};

float4 main(VS_IN vin) : SV_POSITION
{
    float4 wp = mul(float4(vin.pos, 1), world);
    float3 wn = normalize(mul(vin.normal, (float3x3)world));   // uniform scale only, so no inverse-transpose needed
    wp.xyz += wn * params.x;
    return mul(mul(wp, view), proj);
}

// Stone wall vertex shader.
// Same world/view/proj transform as VS_Object, but ALSO outputs the world-space
// position, which the triplanar pixel shader needs to project the texture by
// world coordinates (bypassing the mesh's broken UVs).
// ASCII comments only so this file compiles regardless of code page.
struct VS_IN
{
	float3 pos    : POSITION0;
	float3 normal : NORMAL0;
	float2 uv     : TEXCOORD0;
};

struct VS_OUT
{
	float4 pos      : SV_POSITION0;
	float2 uv       : TEXCOORD0;
	float3 normal   : NORMAL0;
	float3 worldPos : TEXCOORD1;	// world-space position for triplanar
};

cbuffer WVP : register(b0)
{
	float4x4 world;
	float4x4 view;
	float4x4 proj;
};

VS_OUT main(VS_IN vin)
{
	VS_OUT vout;

	// local -> world (keep the world position for triplanar), then view, proj.
	float4 wp = mul(float4(vin.pos, 1.0f), world);
	vout.worldPos = wp.xyz;
	vout.pos = mul(mul(wp, view), proj);

	vout.uv = vin.uv;

	// Rotate the normal by the world matrix (rotation part only).
	vout.normal = mul(vin.normal, (float3x3)world);

	return vout;
}

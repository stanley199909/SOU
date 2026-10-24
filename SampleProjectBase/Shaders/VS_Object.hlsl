// Object vertex shader (world/view/proj transform).
// ASCII comments only so this file compiles regardless of code page.
struct VS_IN
{
	float3 pos    : POSITION0;
	float3 normal : NORMAL0;
	float2 uv     : TEXCOORD0;
};

struct VS_OUT
{
	float4 pos    : SV_POSITION0;
	float2 uv     : TEXCOORD0;
	float3 normal : NORMAL0;
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

	// local -> world -> view -> projection
	vout.pos = float4(vin.pos, 1.0f);
	vout.pos = mul(vout.pos, world);
	vout.pos = mul(vout.pos, view);
	vout.pos = mul(vout.pos, proj);

	vout.uv = vin.uv;

	// Rotate the normal by the world matrix (rotation part only).
	vout.normal = mul(vin.normal, (float3x3)world);

	return vout;
}

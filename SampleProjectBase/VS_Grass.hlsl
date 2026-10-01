// Grass vertex shader: VS_Wall + bending away from the player (interactive grass).
// Output layout is identical to VS_Wall, so the same PS_Wall draws it.
// ASCII comments only so this file compiles regardless of code page.
//
// The InteractionMap (t0) holds "pressure" 0..1 in world XZ, written from above by an
// orthographic camera. For each vertex:
//   - where to lean: DOWNHILL on the pressure = minus its gradient (slope). Around the
//     player the pressure is a dome, so downhill always points AWAY from the player.
//   - how far to lean: the gradient's size (steep in a ring around the feet) ...
//   - ... and pressed down by the pressure itself (flat right under the feet).
//   - only the upper part moves: weight = height of the vertex above the blade's root,
//     so roots stay planted and tips move the most.
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
	float3 worldPos : TEXCOORD1;
};

cbuffer WVP : register(b0)
{
	float4x4 world;
	float4x4 view;
	float4x4 proj;
};

cbuffer Interact : register(b1)
{
	float4 area;	// xy = map min world XZ, z = 1 / map size (world), w = one texel in uv
	float4 bend;	// x = sideways lean at the steepest slope (world), y = press-down at full pressure (world),
					// z = grass root height (world Y), w = blade height (world)
	float4 slope;	// x = stamp radius (world): makes the lean independent of the stamp size
					//     (the dome's steepest slope is then ~1.5, at ~0.58 of the radius)
};

Texture2D    interactMap : register(t0);
SamplerState samp        : register(s0);

float Pressure(float2 uv)
{
	return interactMap.SampleLevel(samp, uv, 0).r;	// SampleLevel: vertex shaders have no mip derivatives
}

VS_OUT main(VS_IN vin)
{
	VS_OUT vout;
	float4 wp = mul(float4(vin.pos, 1.0f), world);

	// world XZ -> map uv (v flipped: the map's "up" is world +Z, texture v grows downward)
	float2 uv = float2((wp.x - area.x) * area.z, 1.0f - (wp.z - area.y) * area.z);
	float  h  = area.w;										// one texel
	float  p  = Pressure(uv);
	// Central differences = gradient in uv units. Convert to "per world unit" (/ size),
	// then scale by the stamp radius so the lean does not depend on the stamp size.
	float  gx = (Pressure(uv + float2(h, 0)) - Pressure(uv - float2(h, 0))) / (2.0f * h);
	float  gz = -(Pressure(uv + float2(0, h)) - Pressure(uv - float2(0, h))) / (2.0f * h);	// v is flipped vs Z
	float2 grad = float2(gx, gz) * area.z * slope.x;

	float tip = saturate((wp.y - bend.z) / max(bend.w, 1e-4f));	// 0 at the root .. 1 at the tip
	wp.xz -= grad * bend.x * tip;		// lean downhill = away from the player
	wp.y  -= p * bend.y * tip;			// pressed down under the feet

	vout.worldPos = wp.xyz;
	vout.pos = mul(mul(wp, view), proj);
	vout.uv = vin.uv;
	vout.normal = mul(vin.normal, (float3x3)world);
	return vout;
}

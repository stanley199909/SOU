// Stone wall pixel shader -- step (b-1): triplanar sampling of the BaseColor.
//
// Triplanar == Blender's "Box projection": instead of trusting the mesh UVs
// (which are broken on this AI-generated wall), we sample the texture from the
// three world axes (X/Y/Z planes) and blend the three samples by the surface
// normal. A face pointing mostly +Z takes the Z-plane sample, etc.
//
// No lighting yet -- this step only proves the color/tiling is right. Lighting
// (Cook-Torrance GGX + window/forge lights) comes in step (c).
// ASCII comments only.
struct PS_IN
{
	float4 pos      : SV_POSITION0;
	float2 uv       : TEXCOORD0;
	float3 normal   : NORMAL0;
	float3 worldPos : TEXCOORD1;
};

Texture2D    tex  : register(t0);	// Poly Haven BaseColor (linear, sRGB texture)
SamplerState samp : register(s0);

cbuffer WallParam : register(b0)
{
	float4 param;	// x = world meters covered by one texture tile; yzw = spare
};

float4 main(PS_IN pin) : SV_TARGET
{
	float s = max(param.x, 0.001f);			// tile size in world meters (avoid /0)

	// Weight each axis by how much the face points that way; sum to 1.
	float3 n = abs(normalize(pin.normal));
	n /= (n.x + n.y + n.z);

	// Project world position onto the three planes to get three sets of UVs.
	float2 uvX = pin.worldPos.zy / s;		// X-facing -> sample (z,y)
	float2 uvY = pin.worldPos.xz / s;		// Y-facing -> sample (x,z)
	float2 uvZ = pin.worldPos.xy / s;		// Z-facing -> sample (x,y)

	float3 cX = tex.Sample(samp, uvX).rgb;
	float3 cY = tex.Sample(samp, uvY).rgb;
	float3 cZ = tex.Sample(samp, uvZ).rgb;

	// Blend the three by the normal weights = the triplanar result.
	float3 col = cX * n.x + cY * n.y + cZ * n.z;

	return float4(col, 1.0f);
}

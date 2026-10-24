// Pixel shader that tints a texture by a color.
// Used to draw the player/enemy cow models in blue / red / yellow.

struct PS_IN
{
	float4 pos : SV_POSITION0;
	float2 uv : TEXCOORD0;
};

// texture
Texture2D tex : register(t0);
SamplerState samp : register(s0);

// tint color passed from CPU (WriteBuffer slot 0)
cbuffer Tint : register(b0)
{
	float4 tintColor;
};

float4 main(PS_IN pin) : SV_TARGET
{
	float4 color = tex.Sample(samp, pin.uv);
	// multiply the texture color by the tint color
	color.rgb *= tintColor.rgb;
	return color;
}

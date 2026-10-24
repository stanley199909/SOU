// Pixel shader for glowing billboard particles (sparks and embers).
// tex is a soft radial dot (bright center -> transparent edge). Multiplying it
// by the per-particle color - which the CPU fades toward black as the particle
// dies - yields a soft glowing point that dims out over its life.
// Drawn with additive blending so overlapping particles build up light (Bloom).
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;
};

float4 main(PS_IN pin) : SV_TARGET
{
    return tex.Sample(samp, pin.uv) * pin.col;
}

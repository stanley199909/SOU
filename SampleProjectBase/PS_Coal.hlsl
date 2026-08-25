// Pixel shader for the glowing coal bed (texture-driven, cheap).
//
// Approach: the coal texture (ash baked dark, embers baked orange) is the base.
// We read how "hot" each texel is from its color, then make ONLY the hot cracks
// smolder - brighten and dim over time following a slow drifting noise, like air
// being blown through the coals. The dark ash stays dark. No big loop, so it is
// far cheaper than a fully procedural fire and reads as CHARCOAL, not lava.
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

cbuffer Tint : register(b0)
{
    float4 tintColor;   // rgb = base brightness (m_coalGlow), a = time (seconds)
};

struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// --- cheap value noise (a few hashes, no loop) -------------------------------
float hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}
float vnoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);                 // smooth interpolation
    float a = hash21(i);
    float b = hash21(i + float2(1, 0));
    float c = hash21(i + float2(0, 1));
    float d = hash21(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float4 main(PS_IN pin) : SV_TARGET
{
    float  t    = tintColor.a;
    float3 texc = tex.Sample(samp, pin.uv).rgb;

    // How hot is this texel? Embers are red/orange (high R, low B); ash is grey.
    // So "redness above the blue level" isolates the glowing cracks (ash -> ~0).
    float heat = saturate((texc.r - texc.b * 1.1 - 0.06) * 3.0);

    // Slow organic smolder: two octaves of value noise drifting slowly over time.
    float2 p = pin.uv * 6.0 + float2(t * 0.05, t * 0.03);
    float  n = vnoise(p) * 0.65 + vnoise(p * 2.3 + 3.1) * 0.35;   // 0..1
    float  pulse = 0.35 + 1.30 * n;              // some cracks flare, some dim

    // global breathing: the whole bed pulses gently like firelight
    float breath = 0.90 + 0.10 * sin(t * 3.1) + 0.05 * sin(t * 7.7 + 1.3);

    float3 ash   = texc * 0.55;                              // dark charcoal base
    float3 ember = heat * pulse * float3(1.0, 0.42, 0.12);   // glowing cracks only
    float3 col   = (ash + ember) * tintColor.rgb * breath;
    return float4(col, 1.0);
}

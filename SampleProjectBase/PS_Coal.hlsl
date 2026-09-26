// Pixel shader for the charcoal bed (CoalBedMesh geometry).
// The geometry carries the shape: dark charcoal lumps sit on a glowing floor quad,
// so only the gaps between lumps emit strong light. coalShade.a tells which is which
// (1 = glowing gap, 0 = charcoal facet); coalShade.rgb = baked facet brightness.
cbuffer Tint : register(b0)
{
    float4 tintColor;   // rgb = glow brightness (m_coalGlow), a = time (seconds)
};

struct PS_IN
{
    float4 pos       : SV_POSITION;
    float2 uv        : TEXCOORD0;   // 0..1 position on the bed
    float3 worldPos  : TEXCOORD1;
    float4 coalShade : TEXCOORD2;
};

static const float3 CHARCOAL_COLOR  = float3(0.035, 0.026, 0.021);
static const float3 EMBER_COLOR     = float3(1.0, 0.12, 0.008);
static const float  EMBER_INTENSITY = 2.4;    // gap brightness (>1 so Bloom picks it up)
static const float  SURFACE_GLOW    = 0.012;  // faint glow on the charcoal facets themselves
static const float  PULSE_BASE      = 0.80;   // smolder = base + amplitude * sin(...)
static const float  PULSE_AMPLITUDE = 0.16;
static const float  PULSE_SPEED     = 1.4;    // rad/s
static const float2 PULSE_FREQUENCY = float2(31, 47);   // spatial frequency across the bed
static const float  EDGE_START      = 0.82;   // fade the glow out toward the bed border
static const float  EDGE_END        = 1.0;

float4 main(PS_IN p) : SV_TARGET
{
    // Slow smolder that travels across the bed.
    float pulse = PULSE_BASE + PULSE_AMPLITUDE * sin(dot(p.uv, PULSE_FREQUENCY) + tintColor.a * PULSE_SPEED);

    // 1 in the middle, 0 at the square border (so the bed doesn't end in a hard glowing line).
    float2 centered = abs(p.uv * 2 - 1);
    float  edge = 1 - smoothstep(EDGE_START, EDGE_END, max(centered.x, centered.y));

    float glow = lerp(SURFACE_GLOW, pulse * EMBER_INTENSITY, p.coalShade.a) * edge;
    float3 col = CHARCOAL_COLOR * p.coalShade.rgb + EMBER_COLOR * glow * tintColor.rgb;
    return float4(col, 1);
}

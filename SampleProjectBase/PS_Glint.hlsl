// Star glint (search: "procedural star glint shader", "sparkle billboard"): the short twinkle of
// light on freshly ground steel, shown when a grinding segment is finished.
// Drawn on a camera-facing quad (built on the CPU, VS_Particle) with ADDITIVE blending into the
// HDR scene, so Bloom spreads it. No texture: the shape is a formula of the quad's uv.
//   core      = round bright dot in the middle
//   main rays = a thin cross (horizontal + vertical), long
//   side rays = the same cross turned 45 degrees, shorter and dimmer
// col = color x intensity (CPU animates it: grows, turns a little, fades).
// ASCII comments only so this file compiles regardless of code page.
struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;
};

static const float CORE_SHARPNESS = 30.0;   // higher = smaller bright dot
static const float RAY_THINNESS   = 60.0;   // higher = thinner rays (across the ray)
static const float RAY_FALLOFF    = 3.0;    // higher = shorter main rays (along the ray)
static const float SIDE_FALLOFF   = 6.0;    // the 45-degree rays fade faster = shorter
static const float SIDE_STRENGTH  = 0.5;    // the 45-degree rays are dimmer
static const float INV_SQRT2      = 0.70710678;

// One cross: a thin ray along x and one along y. falloff = how fast each ray fades with length.
float Cross(float2 p, float falloff)
{
    float rayX = exp(-abs(p.y) * RAY_THINNESS) * exp(-abs(p.x) * falloff);
    float rayY = exp(-abs(p.x) * RAY_THINNESS) * exp(-abs(p.y) * falloff);
    return rayX + rayY;
}

float4 main(PS_IN pin) : SV_TARGET
{
    float2 p = pin.uv * 2 - 1;                  // -1..1, center = 0
    float  r = length(p);
    float  core = exp(-r * r * CORE_SHARPNESS);
    float2 q = float2(p.x + p.y, p.x - p.y) * INV_SQRT2;   // turned 45 degrees
    float  star = core + Cross(p, RAY_FALLOFF) + SIDE_STRENGTH * Cross(q, SIDE_FALLOFF);
    star *= saturate(1 - r);                    // nothing at the quad's border (no visible square edge)
    return float4(pin.col.rgb * star, 1);
}

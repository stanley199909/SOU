// Weapon trail (the slash's light streak). Drawn with VS_Particle (world-space vertices),
// additive blending. The shape is computed here, no texture:
//   uv.x = along the streak: 0 = the tail (oldest) .. 1 = the head (the blade now)
//   uv.y = across: 0 = near the guard .. 1 = the tip
// Bright at the head and toward the tip, fading to nothing at the tail and at the inner edge.
// Search: "weapon trail shader", "sword trail additive".
// ASCII comments only so this file compiles regardless of code page.
struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;   // rgb = color, a = overall strength (fades after the swing)
};

static const float ALONG_POWER  = 1.8f;   // >1: the streak thins out quickly behind the blade
static const float INNER_FADE   = 0.55f;  // across: transparent at the inner edge, full from here outward
static const float TIP_SOFT     = 0.08f;  // across: soften the outermost sliver (no hard line at the tip)
static const float CORE_BOOST   = 1.6f;   // the bright core just behind the blade (hot white)
static const float CORE_WIDTH   = 0.12f;  // along: how long that core is

float4 main(PS_IN p) : SV_TARGET
{
    float along  = saturate(p.uv.x);
    float across = saturate(p.uv.y);
    float a = pow(along, ALONG_POWER);
    a *= smoothstep(0.0f, INNER_FADE, across) * (1.0f - smoothstep(1.0f - TIP_SOFT, 1.0f, across));
    float core = smoothstep(1.0f - CORE_WIDTH, 1.0f, along) * CORE_BOOST;
    a *= p.col.a;
    float3 rgb = p.col.rgb * a * (1.0f + core);
    return float4(rgb, a);
}

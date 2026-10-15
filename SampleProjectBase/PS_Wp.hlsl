// Blade being forged = PBR steel (same lighting model as every other object: Lighting.hlsli)
// + light EMITTED by the hot steel. Reflection and emission are two independent things, so they ADD:
//     final = light reflected from the sun / fire / room   (cold or hot, steel always reflects)
//           + glow of the hot steel, driven by the game's live temperature (col.a)
// Before 2026-10-15 the blade had its own fake light direction and a fake sky/ground "environment",
// unrelated to the scene's real sun and forge. The forge scale (black oxide) and the ground edge
// are kept exactly as tuned.
// ASCII comments only so this file compiles regardless of code page.
Texture2D tex : register(t0);                 // steel base color
Texture2D scaleMask : register(t1);           // forge-scale thickness (grey: white = thick, black = bare steel), tiled on UV
Texture2D<float> sunDepth : register(t3);
SamplerState samp : register(s0);
cbuffer WpParam : register(b0)
{
    float4   eye;        // --- scene lights (LitRender::Lights, same values as the props) ---
    float4   sun;
    float4   ambient;
    float4   fire;
    float4x4 lightVP;
    float4   shadow;
    float4   material;   // x roughness, y metallic (GameData/PropMaterials "Weapon")
    float4   hot;        // x shade on the side facing away from the light, y rim strength, z rim sharpness, w glow gain
    float4   scale;      // x tiling, y soft edge, z opacity, w glow
    float4   scale2;     // x already fallen off at progress 0
};
#include "SunShadow.hlsli"
#include "Lighting.hlsli"
struct PIN { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; float sharp:TEXCOORD4; float work:TEXCOORD5; };

static const float3 SCALE_COLD_COL = float3(0.06, 0.055, 0.05);   // cold forge scale = sooty near-black
static const float  SCALE_ROUGH    = 1.0;                         // forge scale is matte
static const float3 SCALE_EMBER    = float3(1.0, 0.30, 0.08);     // dull dark-red glow of hot scale (not the steel's yellow-white)
static const float3 LUMA_WEIGHTS   = float3(0.2126, 0.7152, 0.0722); // Rec.709 luminance: take only the brightness of the heat color
static const float3 GROUND_STEEL   = float3(0.78, 0.80, 0.83);   // ground (sharpened) steel color
static const float  GROUND_ROUGH   = 0.08;                        // ground face is nearly mirror smooth
static const float  HOT_START = 0.06, HOT_FULL = 0.45;            // temperature where the glow starts / is full
static const float  HOT_BASE_TINT  = 0.35;                        // glow = heat color x (this + (1-this) x steel texture): keeps the texture visible in the glow

// Reflected light of a surface, from the scene's lights (sun with shadow, forge fire, room).
float3 Reflected(float3 base, float rough, float metal, float3 n, float3 v, float3 l, float3 wp, float sunVisible)
{
    float3 c = AmbientLight(base, rough, metal, n, v, ambient.rgb * ambient.w);
    c += BRDF(base, rough, metal, n, v, l) * sun.w * sunVisible;
    c += FireLight(base, rough, metal, n, v, wp, fire.xyz, fire.w);
    return c;
}

float4 main(PIN i) : SV_TARGET
{
    float3 n = normalize(i.nrm);
    float3 v = normalize(eye.xyz - i.wp);
    float3 l = sun.xyz / max(length(sun.xyz), EPSILON);
    float  ground = saturate(i.sharp);
    float  sunVisible = shadow.x > .5 ? SunVisibility(i.wp, n) : 1;

    // --- steel: PBR reflection (the ground edge becomes bright and smooth) ---
    float3 steel = lerp(tex.Sample(samp, i.uv).rgb, GROUND_STEEL, ground);
    float  rough = clamp(lerp(material.x, GROUND_ROUGH, ground), MIN_ROUGHNESS, 1);
    float3 col = Reflected(steel, rough, material.y, n, v, l, i.wp, sunVisible);

    // --- glow of the hot steel (emission) ---
    //   Half-Lambert shading toward the real sun + a Fresnel-like rim, so the bevels, the edge and
    //   the two faces still read while glowing (a flat glow flattened the shape).
    float  nv   = saturate(dot(n, v));
    float  halfLambert = saturate(dot(n, l) * 0.5 + 0.5);
    float  shade = lerp(hot.x, 1.0, halfLambert);
    float  rim   = hot.y * pow(1.0 - nv, hot.z);
    float  heat  = smoothstep(HOT_START, HOT_FULL, i.col.a);
    float3 glow  = (i.col.rgb * (HOT_BASE_TINT + (1 - HOT_BASE_TINT) * steel) * shade + i.col.rgb * rim) * hot.w;
    col += glow * heat;

    // --- forge scale (black oxide): the more a face was hammered, the more it has flaked off ---
    //   mask grey = thickness. The face's forging progress is the threshold: thin scale goes first,
    //   thick scale last (it never vanishes all at once). Ground areas have none. Cold scale stays.
    float  m       = scaleMask.Sample(samp, i.uv * scale.x).r;
    float  thr     = lerp(scale2.x, 1.0 + scale.y, saturate(i.work));
    float  covered = smoothstep(thr - scale.y, thr, m) * (1.0 - ground);
    float3 scaleCol = Reflected(SCALE_COLD_COL, SCALE_ROUGH, 0, n, v, l, i.wp, sunVisible);   // matte black, lit like everything else
    float  heatLuma = dot(i.col.rgb, LUMA_WEIGHTS);
    scaleCol += SCALE_EMBER * heatLuma * scale.w * shade * heat;                                // hot scale glows dull red
    col = lerp(col, scaleCol, covered * scale.z);
    return float4(col, 1.0);
}

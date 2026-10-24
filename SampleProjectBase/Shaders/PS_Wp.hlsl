// Blade being forged = PBR steel (same lighting model as every other object: Lighting.hlsli)
// + light EMITTED by the hot steel. Reflection and emission are two independent things, so they ADD:
//     final = light reflected from the sun / fire / room (IBL)   (cold or hot, steel always reflects)
//           + glow of the hot steel, driven by the game's live temperature (col.a)
// 2026-10-16 rework (user: "the hot blade loses all its texture and bumps"):
//   - the reflection is no longer faded out by heat: real glowing steel still shows highlights and
//     the room (it was faded only because an over-bright forge pushed the sum to white)
//   - the fake shading of the glow (half-Lambert toward the sun + rim) is gone. Emitted light does not
//     depend on where the lights are. What it DOES depend on is the viewing angle (Kirchhoff, below).
//   - the pack's blade normal map is used, so the surface detail reads both cold and hot.
// ASCII comments only so this file compiles regardless of code page.
Texture2D tex : register(t0);                 // steel base color
Texture2D scaleMask : register(t1);           // forge-scale thickness (grey: white = thick, black = bare steel), tiled on UV
Texture2D normalMap : register(t2);           // blade normal map (OpenGL convention: +green = +V)
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
    float4   material;   // x roughness, y metallic (GameData/PropMaterials "Weapon"), z polished 0..1 (before tempering the smith polishes the blade so the temper colors can be seen)
    float4   hot;        // x share of the reflection kept while hot (1 = physical), y glow base tint (low = steel texture shows more in the glow), z emissivity floor (low = normal map / angle shapes the glow more), w glow gain
    float4   scale;      // x tiling, y soft edge, z opacity, w glow
    float4   scale2;     // x already fallen off at progress 0, y draw reflection (F1), z draw glow (F1), w has normal map
    float4   temperKey[8];   // tempering color table from the CPU (SceneForge::TEMPER_COLOR_TABLE): xyz color, w position
    float4   temperFilm;     // x position where the oxide film counts as fully formed
};
#include "SunShadow.hlsli"
#include "Lighting.hlsli"
struct PIN { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; float sharp:TEXCOORD4; float work:TEXCOORD5; float temper:TEXCOORD6; };

static const float3 SCALE_COLD_COL = float3(0.06, 0.055, 0.05);   // cold forge scale = sooty near-black
static const float  SCALE_ROUGH    = 1.0;                         // forge scale is matte
static const float3 SCALE_EMBER    = float3(1.0, 0.30, 0.08);     // dull dark-red the hot scale leans toward (not the steel's yellow-white)
static const float  SCALE_REDDEN   = 0.7;                         // how far the scale's glow leans from the steel's heat color toward SCALE_EMBER (scale surface is a little cooler)
static const float3 LUMA_WEIGHTS   = float3(0.2126, 0.7152, 0.0722); // Rec.709 luminance: take only the brightness of the heat color
static const float3 GROUND_STEEL   = float3(0.78, 0.80, 0.83);   // ground (sharpened) steel color
static const float  GROUND_ROUGH   = 0.08;                        // ground face is nearly mirror smooth
static const float  POLISHED_ROUGH = 0.30;                        // polished (not mirror) face: catches the fire and room light broadly, so the temper colors read
static const float  FILM_METALLIC  = 0.35;                        // metallic where the temper film has fully formed (lower = the film color shows more under any light)
static const float  HOT_START = 0.06, HOT_FULL = 0.45;            // temperature where the glow starts / is full

// Reflected light of a surface, from the scene's lights (sun with shadow, forge fire, room).
float3 Reflected(float3 base, float rough, float metal, float3 n, float3 v, float3 l, float3 wp, float sunVisible)
{
    float3 c = AmbientLight(base, rough, metal, n, v, wp, ambient.rgb * ambient.w, ambient.rgb);
    c += BRDF(base, rough, metal, n, v, l) * sun.w * sunVisible;
    c += FireLight(base, rough, metal, n, v, wp, fire.xyz, fire.w);
    return c;
}

// Kirchhoff's law of thermal radiation: a surface emits exactly as much as it fails to reflect
// (emissivity = 1 - reflectance). Metals reflect more at grazing angles (Fresnel), so glowing steel
// is a little DIMMER toward its silhouette and brightest where it faces you -> the shape reads from
// the light itself, without any fake light direction. Normalized so a surface facing you = 1.
// Search: "Kirchhoff's law of thermal radiation", "emissivity fresnel" / JP: kirchhoff no housha housoku
float Emissivity(float3 f0, float nv)
{
    float3 f = f0 + (1 - f0) * pow(1 - nv, 5);
    float e = 1 - dot(f, LUMA_WEIGHTS);
    float eFacing = 1 - dot(f0, LUMA_WEIGHTS);
    return saturate(e / max(eFacing, EPSILON));
}

// Tempering colors (search: "temper colors steel", JP: yakimodoshi-iro / tempa-kara-).
// Warming hardened steel grows a thin oxide film on the surface; its thickness (= how far the
// tempering went) shows as interference colors. The table follows the real order:
//   silver -> light straw -> dark straw -> bronze/brown -> purple -> dark blue -> light blue -> grey.
// t = position along that order (0..1). The color TINTS the steel's base color (multiply):
// the film colors the light the steel reflects. t = 0 (never tempered) = no tint.
// The table itself lives on the CPU (SceneForge::TEMPER_COLOR_TABLE -> temperKey), so the HUD color chart
// and the blade always show the same colors.
static const int TEMPER_KEYS = 8;
float3 TemperTint(float t)
{
    t = saturate(t);
    [unroll] for (int k = 0; k < TEMPER_KEYS - 1; ++k)
        if (t <= temperKey[k + 1].w)
            return lerp(temperKey[k].rgb, temperKey[k + 1].rgb, (t - temperKey[k].w) / max(temperKey[k + 1].w - temperKey[k].w, EPSILON));
    return temperKey[TEMPER_KEYS - 1].rgb;
}

float4 main(PIN i) : SV_TARGET
{
    float3 v = normalize(eye.xyz - i.wp);
    float3 geometryN = normalize(i.nrm);
    // Two-sided: the blade is a thin plate seen from either face (and it is flipped over while forging).
    // A normal pointing AWAY from the eye gives nv = 0 -> Kirchhoff emissivity 0 -> no glow (suspected cause of
    // 2026-10-17 "only the scale glows": the scale does not use emissivity). Turn it toward the eye.
    if (dot(geometryN, v) < 0) geometryN = -geometryN;
    float3 n = geometryN;
    if (scale2.w > 0.5)
        n = PerturbNormal(geometryN, i.wp, i.uv, normalMap.Sample(samp, i.uv).xyz * 2 - 1);
    float3 l = sun.xyz / max(length(sun.xyz), EPSILON);
    float  ground = saturate(i.sharp);
    float  sunVisible = shadow.x > .5 ? SunVisibility(i.wp, geometryN) : 1;
    float  heat = smoothstep(HOT_START, HOT_FULL, i.col.a);
    float  reflectKeep = lerp(1.0, hot.x, heat);   // 1 = the reflection stays fully while hot (physical)

    // --- steel: PBR reflection (the ground edge becomes bright and smooth) ---
    float  polish = material.z;
    float3 steel = lerp(tex.Sample(samp, i.uv).rgb, GROUND_STEEL, max(ground, polish));   // polished = bright bare steel (the dark forged surface would hide the colors)
    steel *= TemperTint(i.temper);   // tempering oxide film (0 = none). The CPU makes the spine run ahead of the edges
    float  rough = clamp(lerp(lerp(material.x, POLISHED_ROUGH, polish), GROUND_ROUGH, ground), MIN_ROUGHNESS, 1);
    // The oxide film is a thin non-metal layer: where it has formed, part of the light is scattered by the film
    // (diffuse) instead of mirrored by the metal. A pure metal only shows what it reflects -> in the dark smithy
    // the colors were invisible (2026-10-19). Lowering the metallic where the film is gives the colors body.
    float  film  = saturate(i.temper / max(temperFilm.x, EPSILON));
    float  metal = lerp(material.y, FILM_METALLIC, film);
    float3 col = Reflected(steel, rough, metal, n, v, l, i.wp, sunVisible) * scale2.y * reflectKeep;

    // --- glow of the hot steel (emission), ADDED to the reflection ---
    float  nv = saturate(dot(n, v));
    float  emissivity = Emissivity(SurfaceF0(steel, material.y), nv);
    // floor (hot.z): hot forged steel is oxidized (real emissivity ~0.6-0.9) so it never goes black at grazing angles;
    // lower = the normal map and the angle shape the glow more (metal surface reads while glowing).
    // base tint (hot.y): glow = heat color x (tint + (1-tint) x steel texture); lower = more texture in the glow.
    float3 glow = i.col.rgb * (hot.y + (1 - hot.y) * steel) * max(emissivity, hot.z) * hot.w;
    col += glow * heat * scale2.z;

    // --- forge scale (black oxide): the more a face was hammered, the more it has flaked off ---
    //   mask grey = thickness. The face's forging progress is the threshold: thin scale goes first,
    //   thick scale last (it never vanishes all at once). Ground areas have none.
    //   Scale is a poor heat conductor sitting on the steel, so it glows a dull dark red (cooler than the
    //   steel under it) -> hot scale stays darker than the bare steel = the hammered areas still read.
    float  m       = scaleMask.Sample(samp, i.uv * scale.x).r;
    float  thr     = lerp(scale2.x, 1.0 + scale.y, saturate(i.work));
    float  covered = smoothstep(thr - scale.y, thr, m) * (1.0 - ground);
    float3 scaleCol = Reflected(SCALE_COLD_COL, SCALE_ROUGH, 0, n, v, l, i.wp, sunVisible) * scale2.y * reflectKeep;   // matte black, lit like everything else
    float  heatLuma = dot(i.col.rgb, LUMA_WEIGHTS);
    // The scale is a thin skin at the SAME temperature as the steel -> it glows with the same heat color and the
    // same gain (hot.w), only a little dimmer/redder (insulating skin = slightly cooler surface).
    // 2026-10-17: before, it glowed a fixed dim red without the gain -> most of the blade (still scaled) looked
    // the same at every temperature.
    float3 scaleHeatCol = lerp(i.col.rgb, SCALE_EMBER * heatLuma, SCALE_REDDEN);
    scaleCol += scaleHeatCol * scale.w * hot.w * heat * scale2.z;
    col = lerp(col, scaleCol, covered * scale.z);
    return float4(col, 1.0);
}

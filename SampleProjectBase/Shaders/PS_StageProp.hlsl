// Props (anvil, stump, grindstone, trough, buckets, tools, held hammer/tongs, forge ...).
// Same lighting model as the cottage walls (Lighting.hlsli: GGX + ambient + fire), driven by a
// data-driven material (GameData/PropMaterials: roughness / metallic / maps / triplanar).
// ASCII comments only so this file compiles regardless of code page.
struct PS_IN {float4 pos:SV_POSITION0;float2 uv:TEXCOORD0;float3 normal:NORMAL0;float3 worldPos:TEXCOORD1;};
Texture2D baseMap:register(t0);
Texture2D normalMap:register(t1);           // optional (maps.x)
Texture2D ormMap:register(t2);              // optional (maps.y): R = ambient occlusion, G = roughness, B = metallic
Texture2D<float> sunDepth:register(t3);
SamplerState samp:register(s0);
cbuffer PropParam:register(b0){
    float4 tint;
    float4 eye;
    float4 sun;                             // xyz direction toward the sun, w intensity
    float4 ambient;                         // rgb sky fill color, w fill strength
    float4 fire;                            // xyz forge position, w intensity
    float4x4 lightVP;
    float4 shadow;                          // enabled, texel size, depth bias, receiver offset
    float4 material;                        // x triplanar (forge stone), y prop scale, z roughness, w metallic
    float4x4 worldToLocal;
    float4 maps;                            // x has normal map, y has ORM map, z normal map is DirectX convention (green down), w highlight strength 0..1 (next station)
    float4 hotSpot;                         // xyz a hot point on the object (world), w its radius (m). The tongs' jaws held over the coals
    float4 hotGlow;                         // x how hot it is 0..1 (0 = off)
};
#include "SunShadow.hlsli"
#include "Lighting.hlsli"
// Triplanar (object-space) projection: the stone size stays the same across UV/material seams.
static const float TILE_METERS=2.0;          // one texture repeat = 2m
static const float PROJECTION_SHARPNESS=4;   // higher = harder blend between the 3 projection axes
static const float SOOT_RADIUS=.80;          // soot darkens stone within this distance of the fire (m)
static const float SOOT_DARKENING=.65;       // darkening right at the fire (0..1)
// Highlight of the next station (maps.w): a warm glow ADDED on top of the lit color, stronger at the silhouette
// (rim, Fresnel-like) so the shape lights up without washing out the texture. Pulse/fade are done on the CPU.
static const float3 HIGHLIGHT_COL=float3(1.0,.62,.25);   // warm forge-light orange (matches the UI marker)
static const float HIGHLIGHT_BASE=.25;       // share of the glow on faces that look at the camera (rest goes to the rim)
static const float HIGHLIGHT_RIM_POW=2;      // how tight the rim is (higher = thinner)
static const float3 HOT_GLOW_COL=float3(1.0,.28,.06);   // dull red of steel just hot enough to glow faintly
static const float HOT_GLOW_GAIN=3.0;        // brightness of the hot spot at full heat (HDR). 1.5 was lost under the fire light on the jaws
static const float HOT_FALLOFF_POW=2;        // higher = the glow stays closer to the jaws

// Sample the texture three times (projected along X, Y, Z) and blend by how much the
// surface faces each axis. No UVs needed, so seams in the mesh's UV layout disappear.
// pos/normal are in the prop's OBJECT space, already scaled to metres (see main).
float4 SampleTriplanar(float3 pos,float3 normal) {
    float3 weights=pow(abs(normalize(normal)),PROJECTION_SHARPNESS);
    weights/=dot(weights,float3(1,1,1));     // weights sum to 1
    float3 coord=pos/TILE_METERS;
    return baseMap.Sample(samp,coord.zy)*weights.x
         + baseMap.Sample(samp,coord.xz)*weights.y
         + baseMap.Sample(samp,coord.xy)*weights.z;
}

float4 main(PS_IN p):SV_TARGET {
    float4 tex=baseMap.Sample(samp,p.uv);
    if(material.x>.5) {
        // Object space (not world space): the projection moves/rotates WITH the prop, so the stones
        // stay glued to it. Scaled by the prop's world scale so TILE_METERS is still in metres.
        float3 localPos=mul(float4(p.worldPos,1),worldToLocal).xyz*material.y;
        float3 localNrm=mul(p.normal,(float3x3)worldToLocal);
        tex=SampleTriplanar(localPos,localNrm);
        // Soot belongs to the fire's position in the room, so it stays in world space.
        float soot=1-saturate(length(p.worldPos-fire.xyz)/SOOT_RADIUS);
        tex.rgb*=1-soot*SOOT_DARKENING;
    }
    float3 base=tex.rgb*tint.rgb;

    // Material: constants from the table, or per-texel from the ORM map when the prop has one.
    float rough=material.z, metal=material.w, ao=1;
    if(maps.y>.5) {
        float3 orm=ormMap.Sample(samp,p.uv).rgb;
        ao=orm.r; rough=orm.g; metal=orm.b;
    }
    rough=clamp(rough,MIN_ROUGHNESS,1);

    float3 geometryNormal=normalize(p.normal);
    // Computed OUTSIDE any if: PerturbNormal uses ddx/ddy (screen-space derivatives, taken across a 2x2
    // pixel quad). Inside a branch the compiler does not guarantee them, and they came out 0 -> the
    // normal map had no effect at all (2026-10-16, measured with Tools/pbr_material_probe). Only the
    // RESULT is chosen by the flag.
    float3 mapN=normalMap.Sample(samp,p.uv).xyz*2-1;
    mapN.y=maps.z>.5?-mapN.y:mapN.y;          // DirectX-convention map -> OpenGL convention
    float3 perturbed=PerturbNormal(geometryNormal,p.worldPos,p.uv,mapN);
    float3 n=maps.x>.5?perturbed:geometryNormal;
    float3 v=normalize(eye.xyz-p.worldPos);
    float3 l=sun.xyz/max(length(sun.xyz),EPSILON);

    float visible=shadow.x>.5?SunVisibility(p.worldPos,geometryNormal):1;
    float3 col=AmbientLight(base,rough,metal,n,v,p.worldPos,ambient.rgb*ambient.w,ambient.rgb)*ao;   // AO only darkens the indirect light
    [branch] if(visible>0 && sun.w>0)
        col+=BRDF(base,rough,metal,n,v,l)*sun.w*visible;
    col+=FireLight(base,rough,metal,n,v,p.worldPos,fire.xyz,fire.w);
    float rim=pow(1-saturate(dot(n,v)),HIGHLIGHT_RIM_POW);
    col+=HIGHLIGHT_COL*maps.w*(HIGHLIGHT_BASE+(1-HIGHLIGHT_BASE)*rim);
    // Hot spot (tongs' jaws held over the coals while tempering): a dull red glow that fades with distance from the jaws.
    // Temporary: the CPU cools it down again once the tongs are away from the fire.
    float hot=hotGlow.x*pow(saturate(1-length(p.worldPos-hotSpot.xyz)/max(hotSpot.w,EPSILON)),HOT_FALLOFF_POW);
    col+=HOT_GLOW_COL*HOT_GLOW_GAIN*hot;
    return float4(col,tex.a);
}

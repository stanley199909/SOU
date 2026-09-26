// Depth-aware refractive water for the quench basin.
//
// Not a fluid simulation: a surface shader that reads what is BEHIND it.
//   * shape      : the quad is trimmed to a capsule so the ends match the rounded basin
//   * thickness  : water depth = scene depth behind - water surface depth (depth buffer)
//   * occlusion  : if something is in front of the surface (the rim), discard
//   * refraction : sample the scene snapshot, offset by the ripple slope
//   * absorption : deeper water tints the background more
//   * fresnel    : grazing angles reflect the (dim) indoor ambient, not a bright sky
//   * contact    : a faint line where the water touches the basin walls
Texture2D    sceneTex : register(t0);   // scene behind the water (refraction source)
Texture2D    depthTex : register(t1);   // scene depth (R32_FLOAT view of the DSV)
SamplerState samp     : register(s0);

cbuffer WaterCB : register(b0)
{
    float4 params;    // x = time (s), yz = render target size (px), w = ripple strength
    float4 params2;   // x = proj._33 (A), y = proj._43 (B), z = contact width, w = absorption distance
    float4 eye;       // xyz = camera position, w = surface aspect (length / width)
    float4 room;      // rgb = indoor ambient colour, a = intensity
};

struct PS_IN
{
    float4 pos      : SV_POSITION;   // .xy = pixel, .z = this pixel's NDC depth
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

// Two crossing sine waves (world XZ). Their derivative = the surface slope.
static const float2 WAVE_A = float2(9, 6);
static const float2 WAVE_B = float2(-7, 13);
static const float  SPEED_A = 1.1, SPEED_B = 1.7;
static const float  SECONDARY_WEIGHT = 0.45;          // wave B strength relative to A
static const float  RIPPLE_SLOPE = 0.018;             // slope -> normal tilt
static const float  REFRACTION_OFFSET = 0.003;        // slope -> screen UV offset
static const float  MIN_DISTANCE = 0.00001;           // avoid divide by zero
static const float  WATER_F0 = 0.02;                  // water reflectance at normal incidence
static const float  FRESNEL_POWER = 5;                // Schlick approximation
static const float3 ABSORPTION_TINT = float3(0.10, 0.14, 0.12);
static const float  MAX_ABSORPTION = 0.32;
static const float  CONTACT_STRENGTH = 0.04;
static const float  INDOOR_REFLECTION_SCALE = 0.08;   // diffuse fill is not a bright sky reflection

// NDC depth (0..1) -> linear eye-space Z.   ndcZ = A + B/viewZ  =>  viewZ = B/(ndcZ - A)
float LinearZ(float ndcZ) { return params2.y / (ndcZ - params2.x); }

float4 main(PS_IN p) : SV_TARGET
{
    // --- Capsule trim: straight sides, round ends (radius = half width). -------
    float2 local  = p.uv * 2 - 1;                       // -1..1 across the quad
    float  aspect = max(eye.w, 1);
    float2 fromEnd = float2(max(abs(local.x) * aspect - (aspect - 1), 0), local.y);
    clip(1 - dot(fromEnd, fromEnd));

    // --- Water thickness from the depth buffer. --------------------------------
    // Load (one exact pixel), not Sample: filtering across the rim would blend the
    // rim depth with the floor depth and invent water where there is none.
    float2 screenUV = p.pos.xy / params.yz;
    float  surfaceZ = LinearZ(p.pos.z);
    float  thick = LinearZ(depthTex.Load(int3(int2(p.pos.xy), 0)).r) - surfaceZ;
    if (thick <= 0) discard;                            // something is in front of the water

    // --- Ripple normal. ----------------------------------------------------------
    float2 slope = (WAVE_A * cos(dot(p.worldPos.xz, WAVE_A) + params.x * SPEED_A)
                  + SECONDARY_WEIGHT * WAVE_B * cos(dot(p.worldPos.xz, WAVE_B) + params.x * SPEED_B))
                  * RIPPLE_SLOPE * params.w;
    float3 n = normalize(float3(-slope.x, 1, -slope.y));
    float3 v = normalize(eye.xyz - p.worldPos);
    float  depth01 = saturate(thick / max(params2.w, MIN_DISTANCE));

    // --- Refraction (deeper water bends more). ----------------------------------
    float2 refrUV = clamp(screenUV + slope * REFRACTION_OFFSET * depth01, 0, 1);
    int2   pixel  = clamp(int2(refrUV * params.yz), int2(0, 0), int2(params.yz) - 1);
    // Halo guard: never pull colour from an object IN FRONT of the water.
    if (LinearZ(depthTex.Load(int3(pixel, 0)).r) < surfaceZ) refrUV = screenUV;
    float3 behind = sceneTex.Sample(samp, refrUV).rgb;

    // --- Absorption, fresnel reflection, contact line. ---------------------------
    float3 col = lerp(behind, behind * ABSORPTION_TINT, depth01 * MAX_ABSORPTION);
    float  fresnel = WATER_F0 + (1 - WATER_F0) * pow(1 - saturate(dot(n, v)), FRESNEL_POWER);
    col = lerp(col, room.rgb * room.a * INDOOR_REFLECTION_SCALE, fresnel);
    float contact = 1 - saturate(thick / max(params2.z, MIN_DISTANCE));
    col += room.rgb * room.a * contact * CONTACT_STRENGTH;

    // The refraction sample already contains the background: output opaque,
    // alpha-blending on top would add the background a second time.
    return float4(col, 1);
}

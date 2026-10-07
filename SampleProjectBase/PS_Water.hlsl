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
//   * waves      : the surface height comes from Physics/WaterSim (2D wave equation, solved
//                  on the CPU, uploaded as a texture). The vertex shader lifts the grid; here
//                  the normal is taken from the same heights, so refraction and the fresnel
//                  follow the real waves. Steep, churned water turns milky (foam).
Texture2D    sceneTex : register(t0);   // scene behind the water (refraction source)
Texture2D    depthTex : register(t1);   // scene depth (R32_FLOAT view of the DSV)
Texture2D    heightTex : register(t2);  // WaterSim heights (m). u = quad length, v = width
SamplerState samp     : register(s0);

cbuffer WaterCB : register(b0)
{
    float4 params;    // x = time (s), yz = render target size (px), w = ripple strength
    float4 params2;   // x = proj._33 (A), y = proj._43 (B), z = contact width, w = absorption distance
    float4 eye;       // xyz = camera position, w = surface aspect (length / width)
    float4 room;      // rgb = indoor ambient colour, a = intensity
    float4 sim;       // xy = full world size of the quad (length, width), zw = cos/sin of its yaw
    float4 simTexel;  // xy = one height texel in UV (1/NX, 1/NZ)
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
static const float3 FOAM_COLOUR       = float3(0.80, 0.83, 0.85);
static const float  FOAM_SLOPE_START  = 0.25;  // wave slope where the water starts to look churned
static const float  FOAM_SLOPE_GAIN   = 2.0;   // how quickly it turns milky above that slope

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
    // --- Simulated waves: slope = height difference across one texel (central difference). ---
    // Measured along the quad's own axes (local x = length, local z = width), then turned by
    // the quad's yaw into world X/Z, the same frame as the ambient ripple above.
    float2 tx = float2(simTexel.x, 0), tz = float2(0, simTexel.y);
    float dhdx = (heightTex.Sample(samp, p.uv + tx).r - heightTex.Sample(samp, p.uv - tx).r) / max(2 * simTexel.x * sim.x, MIN_DISTANCE);
    float dhdz = (heightTex.Sample(samp, p.uv + tz).r - heightTex.Sample(samp, p.uv - tz).r) / max(2 * simTexel.y * sim.y, MIN_DISTANCE);
    float2 axisX = float2(sim.z, -sim.w), axisZ = float2(sim.w, sim.z);   // RotationY(yaw): local X, local Z in world XZ
    float2 simSlope = dhdx * axisX + dhdz * axisZ;
    slope += simSlope;
    float foam = saturate((length(simSlope) - FOAM_SLOPE_START) * FOAM_SLOPE_GAIN);

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
    col = lerp(col, FOAM_COLOUR * (room.rgb * room.a + INDOOR_REFLECTION_SCALE), foam);   // churned water, lit by the room

    // The refraction sample already contains the background: output opaque,
    // alpha-blending on top would add the background a second time.
    return float4(col, 1);
}

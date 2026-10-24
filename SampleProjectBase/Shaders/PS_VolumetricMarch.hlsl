// Volumetric sunlight (light shafts / god rays), pass 1 of 2: ray marching through the room's air.
// Runs at quarter resolution (VolumetricLight.cpp). For every pixel, walk from the camera toward the
// surface it sees and, at each step, ask the SUN SHADOW MAP whether that point of air is in sunlight.
// Lit air scatters a little sunlight toward the camera (dust and forge smoke), so the summed result is a
// beam shaped exactly by the window opening: the window bars cut the beam too.
//   - single scattering: light is scattered once (sun -> air -> eye), the standard real-time model
//   - Henyey-Greenstein phase function: dust scatters mostly FORWARD, so the beam is brightest when you
//     look toward the sun (against the light) and faint when the sun is behind you
//   - Beer-Lambert transmittance exp(-density x distance): far air is dimmed by the air in front of it
// Search: "volumetric lighting shadow map ray marching", "god rays", "Henyey-Greenstein"
// ASCII comments only so this file compiles regardless of code page.
Texture2D<float> sceneDepth : register(t0);   // the scene depth (where the ray stops)
Texture2D<float> sunDepth   : register(t3);   // the sun shadow map (SunStage)
cbuffer Volume : register(b0)
{
    float4x4 invViewProj;   // screen -> world (to rebuild the 3D point each pixel sees)
    float4x4 lightVP;       // world -> sun shadow map (same as the lit shaders)
    float4   eye;           // xyz camera position, w time (s)
    float4   sunDir;        // xyz direction TOWARD the sun, w sun radiance (0 = sun switched off)
    float4   shadow;        // x shadow map valid, y 1/size, z depth bias, w receiver offset (SunShadow.hlsli)
    float4   medium;        // x density (scattering per metre), y anisotropy g, z steps, w max distance (m)
    float4   boxMin;        // xyz room box min (air exists only inside the room), w depth buffer width
    float4   boxMax;        // xyz room box max, w depth buffer height
    float4   airColor;      // rgb color of sunlight scattered by the air, w unused
};
#include "SunShadow.hlsli"
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };

static const float PI_VOL    = 3.14159265;
static const int   MAX_STEPS = 64;          // shader loop bound (the F2 setting is clamped to this)
static const float RAY_EPS   = 0.0001;

// Henyey-Greenstein phase function: share of light scattered at angle theta. g > 0 = forward.
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1 - g2) / (4 * PI_VOL * pow(max(1 + g2 - 2 * g * cosTheta, RAY_EPS), 1.5));
}

// Interleaved gradient noise (Jimenez 2014): a per-pixel offset for the first step. Neighbouring pixels
// start their steps at different depths, so 24 steps look like many more after the blur (no banding).
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 main(PIN i) : SV_TARGET
{
    if (sunDir.w <= 0 || shadow.x < 0.5) return 0;   // no sun, or no shadow map to tell lit air from shade

    // The 3D point this pixel sees (from the depth buffer).
    int2   depthPixel = int2(i.uv * float2(boxMin.w, boxMax.w));
    float  z   = sceneDepth.Load(int3(depthPixel, 0));
    float2 ndc = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2);
    float4 h   = mul(float4(ndc, z, 1), invViewProj);
    float3 wp  = h.xyz / h.w;

    float3 ro = eye.xyz;
    float3 rd = wp - ro;
    float  sceneDist = length(rd);
    rd /= max(sceneDist, RAY_EPS);

    // Only the part of the ray inside the room box (slab test, same math as AimSystem::Raycast).
    float3 inv = 1 / (rd + (abs(rd) < RAY_EPS) * RAY_EPS);
    float3 t0 = (boxMin.xyz - ro) * inv, t1 = (boxMax.xyz - ro) * inv;
    float tNear = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), min(t0.z, t1.z));
    float tFar  = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), max(t0.z, t1.z));
    tNear = max(tNear, 0);
    tFar  = min(tFar, min(sceneDist, medium.w));
    if (tFar <= tNear) return 0;

    int   steps   = clamp((int)medium.z, 1, MAX_STEPS);
    float stepLen = (tFar - tNear) / steps;
    float jitter  = InterleavedGradientNoise(i.pos.xy);
    float lit = 0;
    [loop]
    for (int k = 0; k < steps; ++k)
    {
        float  t = tNear + (k + jitter) * stepLen;
        float3 p = ro + rd * t;
        float  visible = SunVisibility(p, float3(0, 0, 0));   // 1 = this bit of air is in the sunbeam
        lit += visible * exp(-medium.x * t);                  // Beer-Lambert: dimmed by the air in front
    }
    float scattered = lit * stepLen * medium.x;               // density x length of lit air
    float phase = PhaseHG(dot(rd, normalize(sunDir.xyz)), medium.y);
    return float4(airColor.rgb * sunDir.w * phase * scattered, 1);
}

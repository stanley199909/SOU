// Depth-aware refractive water for the quench trough.
//
// This is how real games (Genshin/Wuthering Waves style) make water read as a
// body contained in something -- NOT fluid simulation, but a surface shader that
// reads the scene DEPTH behind it:
//   * refraction : sample the scene snapshot, offset by the wave normal (see-through)
//   * water depth: distance from the water surface to whatever is behind it, from
//                  the depth buffer -> deep water is richer/opaquer, shallow is clear
//   * shore foam : a bright line where the water meets geometry (depth ~ 0). This is
//                  what makes the water look like it FILLS the trough and hugs walls.
//   * occlusion  : anything closer than the water (the near rim wall) clips it away,
//                  so a plane bigger than the opening is auto-trimmed to the basin.
//   * fresnel/glint: sky reflection at grazing angles + a moving sun highlight.
Texture2D    sceneTex : register(t0);   // scene behind the water (refraction source)
Texture2D    depthTex : register(t1);   // scene depth (R32_FLOAT view of the DSV)
SamplerState samp     : register(s0);

cbuffer WaterCB : register(b0)
{
    float4 params;    // x = time (s), y = screen W, z = screen H, w = bump strength
    float4 params2;   // x = proj._33 (A), y = proj._43 (B), z = foam thickness, w = depth fade
};

struct PS_IN
{
    float4 pos : SV_POSITION;   // .xy = pixel pos (screen UV), .z = this pixel's NDC depth
    float2 uv  : TEXCOORD0;     // surface UV -> drives the ripples
};

float waveH(float2 p, float t)
{
    float h  =        sin(dot(p, float2( 6.0,  4.0)) + t * 1.5);
    h += 0.6 * sin(dot(p, float2(-5.0,  9.0)) + t * 2.1);
    h += 0.4 * sin(dot(p, float2(11.0, -3.0)) + t * 2.7);
    h += 0.3 * sin(dot(p, float2( 3.0, 14.0)) + t * 3.3);
    return h;
}

// NDC depth (0..1) -> linear eye-space Z, from the projection coefficients.
// clip.z = viewZ*A + B, clip.w = viewZ  =>  ndcZ = A + B/viewZ  =>  viewZ = B/(ndcZ - A)
float LinearEyeZ(float ndcZ, float A, float B)
{
    return B / (ndcZ - A);
}

float4 main(PS_IN pin) : SV_TARGET
{
    float  t    = params.x;
    float2 res  = float2(params.y, params.z);
    float  bump = (params.w > 0.0001) ? params.w : 1.0;
    float  A    = params2.x;
    float  B    = params2.y;
    float  foamDist  = (params2.z > 0.0001) ? params2.z : 0.06;   // eye-units of foam band
    float  depthFade = (params2.w > 0.0001) ? params2.w : 0.35;   // eye-units to full colour

    float2 uv = pin.uv * float2(4.0, 2.0);   // stretch ripples along the long trough

    // Wave normal from a small height field (finite differences).
    float e  = 0.01;
    float h  = waveH(uv,                t);
    float hx = waveH(uv + float2(e, 0), t);
    float hy = waveH(uv + float2(0, e), t);
    float2 grad = float2(hx - h, hy - h) / e;
    float3 n = normalize(float3(-grad * 0.06 * bump, 1.0));

    float2 screenUV = pin.pos.xy / res;

    // --- Water thickness from the depth buffer -------------------------------
    float sceneNdc = depthTex.Sample(samp, screenUV).r;   // depth of what's behind
    float zScene   = LinearEyeZ(sceneNdc, A, B);
    float zWater   = LinearEyeZ(pin.pos.z, A, B);
    float thick    = zScene - zWater;    // >0: water over floor.  <=0: object in front

    // Occlusion: something is in front of the water surface here -> not water.
    if (thick <= 0.0) discard;

    float depth01 = saturate(thick / depthFade);   // 0 at shore .. 1 deep

    // --- Refraction (deeper water bends more; shore barely bends) -------------
    float2 refrUV = screenUV + n.xy * 0.03 * bump * saturate(depth01 + 0.15);
    refrUV = clamp(refrUV, 0.001, 0.999);
    // Don't pull colour from in front of the water (halo guard): if the refracted
    // sample is actually closer than the water, fall back to the straight sample.
    float refrNdc = depthTex.Sample(samp, refrUV).r;
    if (LinearEyeZ(refrNdc, A, B) < zWater) refrUV = screenUV;
    float3 behind = sceneTex.Sample(samp, refrUV).rgb;

    // --- Depth absorption: clear at the shore, rich blue in the deep ----------
    float3 shallowTint = float3(0.16, 0.40, 0.46);
    float3 deepTint    = float3(0.02, 0.12, 0.22);
    float3 waterCol = lerp(shallowTint, deepTint, depth01);
    float  absorb   = saturate(0.15 + 0.75 * depth01);
    float3 col = lerp(behind, waterCol, absorb);

    // --- Shore foam: bright band where water meets geometry (thin thickness) ---
    float foam = 1.0 - saturate(thick / foamDist);
    foam = foam * foam;                                   // tighten to a line
    col = lerp(col, float3(0.85, 0.9, 0.95), foam * 0.6);

    // --- Fresnel sky reflection + moving sun glint ----------------------------
    float  fres = pow(1.0 - saturate(n.z), 3.0);
    float3 sky  = float3(0.55, 0.68, 0.80);
    col = lerp(col, sky, saturate(fres * 0.8));

    float3 L = normalize(float3(0.35, 0.75, 0.55));
    float3 V = float3(0.0, 1.0, 0.0);
    float3 Hh = normalize(L + V);
    float  spec = pow(saturate(dot(n, Hh)), 90.0) * saturate(depth01 + 0.2);
    col += spec * 1.4;

    // Thin water (shore) is more transparent; deep water and foam are opaque.
    float alpha = max(saturate(0.45 + 0.55 * depth01), foam);
    return float4(col, alpha);
}

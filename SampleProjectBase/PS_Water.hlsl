// Refractive water for the quench trough.
//
// Real-water look on the cheap: we snapshot the scene rendered behind the water
// (PostProcess::CaptureScene -> t0), then SEE THROUGH the surface by sampling
// that scene with screen UVs perturbed by the water's wave normal. On top of the
// refracted image we add a bluish depth-absorption tint, a fresnel rim that
// reflects a pale sky colour, and a moving sun glint. No reflection pass, no
// raymarch -> friendly to an integrated GPU, but it now reads as a body of water
// you can look INTO rather than a flat blue decal.
Texture2D    sceneTex : register(t0);   // scene behind the water (refraction source)
SamplerState samp     : register(s0);

cbuffer WaterCB : register(b0)
{
    float4 params;   // x = time (s), y = screen width, z = screen height, w = bump strength
};

struct PS_IN
{
    float4 pos : SV_POSITION;   // pixel position -> screen UV for refraction
    float2 uv  : TEXCOORD0;     // surface UV -> drives the ripples
};

// Sum of moving sine waves -> a ripple height field. Mixed directions/speeds
// make the interference look organic; "+ t*speed" scrolls each layer.
float waveH(float2 p, float t)
{
    float h  =        sin(dot(p, float2( 6.0,  4.0)) + t * 1.5);
    h += 0.6 * sin(dot(p, float2(-5.0,  9.0)) + t * 2.1);
    h += 0.4 * sin(dot(p, float2(11.0, -3.0)) + t * 2.7);
    h += 0.3 * sin(dot(p, float2( 3.0, 14.0)) + t * 3.3);
    return h;
}

float4 main(PS_IN pin) : SV_TARGET
{
    float  t    = params.x;
    float2 res  = float2(params.y, params.z);
    float  bump = (params.w > 0.0001) ? params.w : 1.0;

    float2 quv = pin.uv;                     // raw 0..1 across the surface (for depth)
    float2 uv  = quv * float2(4.0, 2.0);      // stretch ripples along the long trough

    // --- Fake water DEPTH from the surface UV --------------------------------
    // The trough is a simple basin, so "how far a point is from the rim" is a good
    // stand-in for water depth: deep in the middle, shallow at the edges. This is
    // what turns a flat sheet into a body of water CONTAINED in the trough,
    // without needing to read the depth buffer.
    float2 d2   = min(quv, 1.0 - quv);        // distance to nearest edge (0 at rim)
    float  edge = min(d2.x, d2.y);            // 0 at rim -> 0.5 at center
    float  depth = saturate(edge / 0.18);     // 0 shallow rim .. 1 deep interior
    float  rim   = 1.0 - depth;               // strong near the container walls

    // Height field + surface normal from finite differences (the slope).
    // Ripples fade out near the rim (water is calmer where it meets the wall).
    float e  = 0.01;
    float h  = waveH(uv,                t);
    float hx = waveH(uv + float2(e, 0), t);
    float hy = waveH(uv + float2(0, e), t);
    float2 grad = float2(hx - h, hy - h) / e;
    float  waveAmp = 0.06 * bump * (0.3 + 0.7 * depth);
    float3 n = normalize(float3(-grad * waveAmp, 1.0));   // wave normal

    // --- Refraction: sample the scene behind, offset by the wave slope --------
    // Shallow water (rim) barely distorts; deep water distorts more.
    float2 screenUV = pin.pos.xy / res;
    float2 refrUV   = screenUV + n.xy * 0.03 * bump * depth;
    refrUV = clamp(refrUV, 0.001, 0.999);
    float3 behind = sceneTex.Sample(samp, refrUV).rgb;

    // --- Depth absorption (Beer-Lambert-ish): deeper water = more colour, less
    // see-through. Rim stays clear so you can see the wall/lip of the trough. ---
    float3 shallowTint = float3(0.20, 0.42, 0.48);
    float3 deepTint    = float3(0.03, 0.14, 0.24);
    float3 waterCol = lerp(shallowTint, deepTint, depth);
    float  absorb   = saturate(0.25 + 0.65 * depth);      // clear rim -> opaque core
    float3 col = lerp(behind, waterCol, absorb);

    // --- Soft edge line where the water meets the container wall (meniscus) ----
    float foam = smoothstep(0.9, 1.0, rim);               // only the last sliver
    col = lerp(col, float3(0.55, 0.62, 0.65), foam * 0.35);

    // --- Fresnel: grazing angles reflect the sky, head-on stays transparent ---
    float  fres = pow(1.0 - saturate(n.z), 3.0);
    float3 sky  = float3(0.55, 0.68, 0.80);
    col = lerp(col, sky, saturate(fres * 0.9));

    // --- Moving sun glint (Blinn-Phong highlight on the wave normal) ----------
    float3 L = normalize(float3(0.35, 0.75, 0.55));
    float3 V = float3(0.0, 1.0, 0.0);          // fixed top-down-ish view
    float3 Hh = normalize(L + V);
    float  spec = pow(saturate(dot(n, Hh)), 90.0) * depth;   // no glare on the rim
    col += spec * 1.4;

    // Edge is more transparent (see the lip through it), interior is fuller.
    float alpha = lerp(0.55, 0.97, depth);
    return float4(col, alpha);
}

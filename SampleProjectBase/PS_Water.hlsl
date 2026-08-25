// Procedural water for the quench trough (no reflection map available, so faked).
// Upgrade over the flat sine version: we build a small wave HEIGHT field, derive a
// surface NORMAL from it (finite differences), and light that normal to get moving
// specular sparkles (波光) - the thing that actually reads as "water". Plus a soft
// deep/shallow color and a fresnel-like rim brighten. Still loop-free = cheap.
Texture2D    tex  : register(t0);   // unused (procedural); kept for layout compatibility
SamplerState samp : register(s0);

cbuffer WaterCB : register(b0)
{
    float4 params;   // params.x = time (seconds)
};

struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// Sum of a few moving sine waves -> a ripple height field. Different directions
// and speeds make the interference look organic. "+ t*speed" scrolls them.
float waveH(float2 p, float t)
{
    float h  = sin(dot(p, float2( 6.0,  4.0)) + t * 1.5);
    h += 0.6 * sin(dot(p, float2(-5.0,  9.0)) + t * 2.1);
    h += 0.4 * sin(dot(p, float2(11.0, -3.0)) + t * 2.7);
    return h;
}

float4 main(PS_IN pin) : SV_TARGET
{
    float  t  = params.x;
    float2 uv = pin.uv * float2(4.0, 2.0);   // stretch so ripples suit the long trough

    // Height field + normal from finite differences (slope of the water surface).
    float e  = 0.008;
    float h  = waveH(uv,               t);
    float hx = waveH(uv + float2(e,0), t);
    float hy = waveH(uv + float2(0,e), t);
    float2 grad = float2(hx - h, hy - h) / e;
    float3 n = normalize(float3(-grad * 0.05, 1.0));   // .05 = bump strength

    // Base color: deeper in troughs, lighter on crests.
    float3 deep    = float3(0.03, 0.14, 0.26);
    float3 shallow = float3(0.11, 0.44, 0.64);
    float3 col = lerp(deep, shallow, saturate(h * 0.22 + 0.5));

    // Moving specular sparkle: light the wave normal (Blinn-Phong highlight).
    float3 L = normalize(float3(0.35, 0.55, 0.75));
    float3 V = float3(0.0, 0.0, 1.0);
    float3 Hh = normalize(L + V);
    float spec = pow(saturate(dot(n, Hh)), 64.0);
    col += spec * 1.3;                                  // bright glints ride the waves

    // Fresnel-like rim: steeper slopes look more reflective/bright.
    float fres = pow(1.0 - saturate(n.z), 3.0);
    col += fres * 0.12;

    return float4(col, 0.92);                           // slightly translucent
}

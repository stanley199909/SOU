struct PS_IN {float4 pos:SV_POSITION0;float2 uv:TEXCOORD0;float3 normal:NORMAL0;float3 worldPos:TEXCOORD1;};
Texture2D baseMap:register(t0);
Texture2D<float> sunDepth:register(t3);
SamplerState samp:register(s0);
cbuffer PropParam:register(b0){float4 tint;float4 eye;float4 sun;float4 ambient;float4 fire;float4x4 lightVP;float4 shadow;};
#include "SunShadow.hlsli"
static const float PI=3.14159265;
static const float FIRE_SOFTENING=1;
static const float3 FIRE_COLOR=float3(1,.30,.065);
static const float SPECULAR_POWER=24, SPECULAR_STRENGTH=.03;
float4 main(PS_IN p):SV_TARGET {
 float4 tex=baseMap.Sample(samp,p.uv);float3 base=tex.rgb*tint.rgb;
 float3 n=normalize(p.normal),v=normalize(eye.xyz-p.worldPos),l=normalize(sun.xyz+float3(0,.00001,0));
 float visible=shadow.x>.5?SunVisibility(p.worldPos,n):1;
 float3 col=base*ambient.rgb*ambient.w;
 col+=base/PI*saturate(dot(n,l))*sun.w*visible;
 col+=pow(saturate(dot(n,normalize(v+l))),SPECULAR_POWER)*SPECULAR_STRENGTH*sun.w*visible;
 float3 f=fire.xyz-p.worldPos;float dist2=max(dot(f,f),.0001);
 col+=base/PI*saturate(dot(n,f*rsqrt(dist2)))*FIRE_COLOR*fire.w/(FIRE_SOFTENING+dist2);
 return float4(col,tex.a);
}

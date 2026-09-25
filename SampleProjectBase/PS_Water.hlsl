Texture2D sceneTex:register(t0);
Texture2D depthTex:register(t1);
SamplerState samp:register(s0);
cbuffer WaterCB:register(b0) {
    float4 params; // time, target width/height, ripple strength
    float4 params2; // projection A/B, contact width, absorption distance
    float4 eye; // camera position, basin half-length / half-width
    float4 room; // indoor ambient RGB, intensity
};
struct PS_IN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float3 worldPos:TEXCOORD1; };
static const float2 WaveA=float2(9,6),WaveB=float2(-7,13);
static const float SpeedA=1.1,SpeedB=1.7,SecondaryWeight=.45;
static const float RippleSlope=.018,RefractionOffset=.003;
static const float MinimumDistance=.00001,WaterF0=.02,FresnelPower=5;
static const float3 AbsorptionTint=float3(.10,.14,.12);
static const float MaximumAbsorption=.32,ContactStrength=.04;
static const float IndoorReflectionScale=.08; // Diffuse fill is not a bright sky reflection.
float LinearZ(float d) { return params2.y/(d-params2.x); }
float4 main(PS_IN p):SV_TARGET {
    // Capsule trims the exposed rectangle at the rounded ends of the basin.
    float2 local=p.uv*2-1;
    float aspect=max(eye.w,1);
    float2 end=float2(max(abs(local.x)*aspect-(aspect-1),0),local.y);
    clip(1-dot(end,end));
    float2 uv=p.pos.xy/params.yz;
    // Point-load depth: filtering across the rim creates false water thickness.
    float thick=LinearZ(depthTex.Load(int3(int2(p.pos.xy),0)).r)-LinearZ(p.pos.z);
    if(thick<=0) discard;
    float2 slope=(WaveA*cos(dot(p.worldPos.xz,WaveA)+params.x*SpeedA)
        +SecondaryWeight*WaveB*cos(dot(p.worldPos.xz,WaveB)+params.x*SpeedB))*RippleSlope*params.w;
    float3 n=normalize(float3(-slope.x,1,-slope.y));
    float3 v=normalize(eye.xyz-p.worldPos);
    float depth=saturate(thick/max(params2.w,MinimumDistance));
    float2 refrUV=clamp(uv+slope*RefractionOffset*depth,0,1);
    int2 pixel=clamp(int2(refrUV*params.yz),int2(0,0),int2(params.yz)-1);
    if(LinearZ(depthTex.Load(int3(pixel,0)).r)<LinearZ(p.pos.z)) refrUV=uv;
    float3 behind=sceneTex.Sample(samp,refrUV).rgb;
    float3 col=lerp(behind,behind*AbsorptionTint,depth*MaximumAbsorption);
    float fresnel=WaterF0+(1-WaterF0)*pow(1-saturate(dot(n,v)),FresnelPower);
    col=lerp(col,room.rgb*room.w*IndoorReflectionScale,fresnel);
    float contact=1-saturate(thick/max(params2.z,MinimumDistance));
    col+=room.rgb*room.w*contact*ContactStrength;
    // Refraction already contains the background; don't alpha-blend it twice.
    return float4(col,1);
}

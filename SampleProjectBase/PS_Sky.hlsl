struct PS_IN { float4 pos:SV_POSITION0;float2 uv:TEXCOORD0;float3 normal:NORMAL0;float3 worldPos:TEXCOORD1; };
cbuffer SkyParam:register(b0) {float4 eye;float4 sun;float4 sky;};
static const float SUN_RADIUS_COS=.9999892;
static const float CLOUD_SCALE=1.4;
float hash(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float noise(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);}
float4 main(PS_IN p):SV_TARGET {
 float3 d=normalize(p.worldPos-eye.xyz);float height=saturate(d.y);
 float3 horizon=float3(.58,.66,.72);float3 zenith=sky.rgb*float3(.30,.48,.75);
 float3 color=lerp(horizon,zenith,pow(height,.45));
 float2 uv=d.xz/max(d.y,.15)*CLOUD_SCALE;
 float n=noise(uv)*.57+noise(uv*2.03)*.28+noise(uv*4.11)*.15;
 float cloud=smoothstep(.57,.77,n)*smoothstep(.02,.18,d.y)*.65;
 color=lerp(color,float3(.85,.87,.89),cloud);
 float3 light=normalize(sun.xyz+float3(0,.00001,0));float mu=dot(d,light);
 float halo=pow(saturate(mu),180)*.18;
 float disk=smoothstep(SUN_RADIUS_COS-.000005,SUN_RADIUS_COS,mu)*step(0,light.y);
 color+=float3(1,.87,.67)*(halo+disk*sun.w*4);
 return float4(color,1);
}

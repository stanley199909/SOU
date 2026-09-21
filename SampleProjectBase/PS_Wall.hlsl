// Linear-space cottage PBR. Normal/Roughness textures are linear data.
struct PS_IN
{
    float4 pos : SV_POSITION0;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPos : TEXCOORD1;
};
Texture2D baseMap : register(t0);
Texture2D normalMap : register(t1);
Texture2D roughMap : register(t2);
Texture2D<float> sunDepth : register(t3);
Texture2D grassMap : register(t4);
SamplerState samp : register(s0);
cbuffer WallParam : register(b0)
{
    float4 material; // tile meters, roughness multiplier, metal, glass
    float4 tint;
    float4 eye;
    float4 sun;
    float4 ambient;
    float4 fire;
    float4 window;
    float4 windowExtra;
    float4x4 worldToLocal;
    float4x4 lightVP;
    float4 shadow; // enabled, texel size, depth bias, world normal offset
};
static const float PI=3.14159265;
static const float EPSILON=0.0001;
static const float MIN_ROUGHNESS=0.08;
static const float DIELECTRIC_F0=0.04;

// Cotangent frame from the projected UV derivatives: no mesh tangent stream needed.
float3 DetailNormal(float3 n,float3 position,float2 uv)
{
    float3 dp1=ddx(position), dp2=ddy(position);
    float2 duv1=ddx(uv), duv2=ddy(uv);
    float3 p2=cross(dp2,n),p1=cross(n,dp1);
    float3 t=p2*duv1.x+p1*duv2.x;
    float3 b=p2*duv1.y+p1*duv2.y;
    float invScale=rsqrt(max(max(dot(t,t),dot(b,b)),EPSILON));
    float3 map=normalMap.Sample(samp,uv).xyz*2-1;
    // nor_gl: +green follows increasing V in this derivative frame.
    return normalize(t*invScale*map.x+b*invScale*map.y+n*map.z);
}
float3 BRDF(float3 base,float rough,float metal,float3 n,float3 v,float3 l)
{
    float nl=saturate(dot(n,l)), nv=max(saturate(dot(n,v)),EPSILON);
    float3 h=normalize(v+l);
    float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
    float a=rough*rough, a2=a*a;
    float d=a2/max(PI*pow(nh*nh*(a2-1)+1,2),EPSILON);
    float k=(rough+1)*(rough+1)/8;
    float g=(nv/(nv*(1-k)+k))*(nl/max(nl*(1-k)+k,EPSILON));
    float3 f0=lerp(DIELECTRIC_F0.xxx,base,metal);
    float3 f=f0+(1-f0)*pow(1-vh,5);
    return ((1-f)*(1-metal)*base/PI+f*d*g/max(4*nv*nl,EPSILON))*nl;
}
// Low-cost fallback if the sun shadow resource is unavailable.
float WindowVisibility(float3 position,float3 towardSun)
{
    float3 p=mul(float4(position,1),worldToLocal).xyz;
    float3 d=mul(float4(towardSun,0),worldToLocal).xyz;
    if (abs(d.z)<EPSILON) return 0;
    float t=(window.z-p.z)/d.z;
    if (t<=0) return 0;
    float2 hit=p.xy+d.xy*t-window.xy;
    float2 halfSize=float2(window.w,windowExtra.x);
    float2 edge=halfSize-abs(hit);
    float visible=step(0, min(edge.x,edge.y));
    if (windowExtra.y>0) visible*=step(windowExtra.y,abs(hit.x));
    if (windowExtra.z>0) visible*=step(windowExtra.z,abs(hit.y));
    return visible;
}
#include "SunShadow.hlsli"
float4 main(PS_IN pin, bool front:SV_IsFrontFace) : SV_TARGET
{
    float3 n=normalize(pin.normal);
    float3 v=normalize(eye.xyz-pin.worldPos);
    if (material.w>0.5 && material.w<1.5)
    {
        // Clear, lightly tinted pane. Alpha blending preserves the scene behind it.
        float fresnel=DIELECTRIC_F0+(1-DIELECTRIC_F0)*pow(1-abs(dot(n,v)),5);
        float alpha=saturate(tint.a+fresnel*tint.a);
        return float4(tint.rgb*ambient.rgb,alpha);
    }
    // House UVs are pre-baked in source meters. eye.w follows the house scale.
    float2 uv=pin.uv*eye.w/max(material.x,EPSILON);
    float3 base=baseMap.Sample(samp,uv).rgb*tint.rgb;
    float rough=clamp(roughMap.Sample(samp,uv).r*material.y,MIN_ROUGHNESS,1);
    float3 geometryNormal=n;
    if(material.w>3.5) {
        n=front?n:-n;
        float variation=.8+.2*sin(pin.worldPos.x*3.7+pin.worldPos.z*2.9);
        base=tint.rgb*variation;rough=1;
    } else n=DetailNormal(n,pin.worldPos,uv);
    if(material.w>2.5 && material.w<3.5) {
        // Single ground surface: blend a worn route and yard into patchy grass.
        float2 yard=pin.worldPos.xz;
        float path=abs(yard.x-(sin(yard.y*.17)*1.8));
        float clearing=length(yard/float2(11,10));
        float patches=sin(yard.x*.72)*sin(yard.y*.61)*.20+sin(yard.x*1.7+yard.y*.8)*.10;
        float cover=saturate((clearing-.7)*1.7+patches)*smoothstep(1.1,2.5,path);
        float3 grass=grassMap.Sample(samp,uv*.8).rgb*float3(.65,.76,.48);
        base=lerp(base,grass,cover);
    }
    float3 l=sun.xyz/max(length(sun.xyz),EPSILON);
    float3 local=mul(float4(pin.worldPos,1),worldToLocal).xyz;
    const float ROOM_HALF_WIDTH_CM=255, ROOM_HALF_DEPTH_CM=255, ROOM_RIDGE_CM=414;
    bool indoors=material.w<1.5 && abs(local.x)<ROOM_HALF_WIDTH_CM && abs(local.z)<ROOM_HALF_DEPTH_CM && local.y<ROOM_RIDGE_CM;
    float visibility=shadow.x>0.5?SunVisibility(pin.worldPos,geometryNormal):(indoors?WindowVisibility(pin.worldPos,l):1);
    float fill=indoors?windowExtra.w:.8;
    float3 col=base*ambient.rgb*fill*(1-material.z);
    [branch] if (visibility>0 && sun.w>0)
        col+=BRDF(base,rough,material.z,n,v,l)*sun.w*visibility;
    float3 toFire=fire.xyz-pin.worldPos;
    float dist2=max(dot(toFire,toFire),EPSILON);
    const float3 FIRE_COLOR=float3(1.0,0.30,0.065);
    const float FIRE_SOFTENING=1.0;
    if(material.w<1.5)
        col+=BRDF(base,rough,material.z,n,v,toFire*rsqrt(dist2))*FIRE_COLOR*fire.w/(FIRE_SOFTENING+dist2);
    if(material.w>3.5) col+=base*sun.w*visibility*saturate(dot(-n,l))*.12;
    if(material.w>1.5) {
        const float FOG_START=35, FOG_END=160;
        float fog=saturate((length(eye.xyz-pin.worldPos)-FOG_START)/(FOG_END-FOG_START));
        col=lerp(col,float3(.58,.66,.72),fog);
    }
    // PostProcess applies the project's ACES output transform once.
    return float4(col,1);
}

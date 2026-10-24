// Shared four-tap depth comparison; lightVP, shadow and sunDepth supplied by caller.
float SunVisibility(float3 p,float3 n) {
    float4 clip=mul(float4(p+n*shadow.w,1),lightVP);
    float3 q=clip.xyz/clip.w;
    float2 uv=q.xy*float2(.5,-.5)+.5;
    if(any(uv<=0)||any(uv>=1)||q.z<=0||q.z>=1)return 1;
    float2 texel=uv/shadow.y-.5;int2 cell=int2(floor(texel));float2 w=frac(texel);
    int size=int(1/shadow.y);float compare=q.z-shadow.z;
    float a=step(compare,sunDepth.Load(int3(clamp(cell,int2(0,0),int2(size-1,size-1)),0)));
    float b=step(compare,sunDepth.Load(int3(clamp(cell+int2(1,0),int2(0,0),int2(size-1,size-1)),0)));
    float c=step(compare,sunDepth.Load(int3(clamp(cell+int2(0,1),int2(0,0),int2(size-1,size-1)),0)));
    float d=step(compare,sunDepth.Load(int3(clamp(cell+int2(1,1),int2(0,0),int2(size-1,size-1)),0)));
    return lerp(lerp(a,b,w.x),lerp(c,d,w.x),w.y);
}

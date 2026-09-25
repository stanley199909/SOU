// Geometry carries charcoal facets; only the gaps emit strong light.
cbuffer Tint : register(b0) { float4 tintColor; };
struct PS_IN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0;
    float3 worldPos:TEXCOORD1; float4 coalShade:TEXCOORD2; };
static const float3 CharcoalColor=float3(.035,.026,.021);
static const float3 EmberColor=float3(1,.12,.008);
static const float EmberIntensity=2.4,SurfaceGlow=.012;
static const float PulseBase=.80,PulseAmplitude=.16,PulseSpeed=1.4;
static const float2 SpatialFrequency=float2(31,47);
static const float EdgeStart=.82,EdgeEnd=1;
float4 main(PS_IN p):SV_TARGET {
    float pulse=PulseBase+PulseAmplitude*sin(dot(p.uv,SpatialFrequency)+tintColor.a*PulseSpeed);
    float edge=1-smoothstep(EdgeStart,EdgeEnd,max(abs(p.uv.x*2-1),abs(p.uv.y*2-1)));
    float glow=lerp(SurfaceGlow,pulse*EmberIntensity,p.coalShade.a)*edge;
    return float4(CharcoalColor*p.coalShade.rgb+EmberColor*glow*tintColor.rgb,1);
}

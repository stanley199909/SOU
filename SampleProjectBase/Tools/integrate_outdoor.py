from pathlib import Path
root=Path.cwd()
def patch(path,old,new,count=1):
 p=root/path;b=p.read_bytes();assert b.count(old)==count,(path,b.count(old));p.write_bytes(b.replace(old,new))
# Shared material definitions and shadow uniforms.
p=root/'CottageRender.h';s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include "Model.h"','#include "Model.h"\n#include "SunStage.h"')
s=s.replace('std::array<Material, 8>','std::array<Material, 12>')
s=s.replace('{"glass", "rock_surface", 1.0f, 0.1f, 0.0f, {0.65f,0.76f,0.80f,0.06f}}','''{"glass", "rock_surface", 1.0f, 0.1f, 0.0f, {0.65f,0.76f,0.80f,0.06f}},
        {"yard", "brown_mud_02", 1.3f, 1.0f, 0.0f, {0.85f,0.80f,0.72f,1}},
        {"grass", "grass_path_2", 1.2f, 1.0f, 0.0f, {0.23f,0.30f,0.085f,1}},
        {"bark", "wood_planks", 0.8f, 1.0f, 0.0f, {0.30f,0.27f,0.22f,1}},
        {"leaves", "grass_path_2", 1.0f, 1.0f, 0.0f, {0.14f,0.25f,0.055f,1}}''')
s=s.replace('if (!name) return m[0];','''if (!name) return m[0];
    if (strstr(name,"OutdoorGround")) return m[8];
    if (strstr(name,"OutdoorGrass")) return m[9];
    if (strstr(name,"OutdoorBark")) return m[10];
    if (strstr(name,"OutdoorLeaves")) return m[11];''')
s=s.replace('XMFLOAT4X4 worldToLocal;','XMFLOAT4X4 worldToLocal;\n    XMFLOAT4X4 lightVP;\n    XMFLOAT4 shadow;')
s=s.replace('sizeof(Params)==192','sizeof(Params)==272')
s=s.replace('bool glassPass)','bool glassPass, bool outdoor=false)')
s=s.replace('params.window=settings.window;', '''params.lightVP=SunStage::Data().lightVP;
    params.shadow={SunStage::Data().hasShadow?1.0f:0.0f,1.0f/SunStage::ShadowSize,0.00008f,SunStage::ReceiverOffset};
    params.window=settings.window;''')
s=s.replace('params.tint=material.tint;', '''params.tint=material.tint;
        if(outdoor) {
            params.material.w=2.0f;
            if(name && strstr(name,"OutdoorGround")) params.material.w=3.0f;
            if(name && (strstr(name,"OutdoorLeaves") || strstr(name,"OutdoorGrass"))) params.material.w=4.0f;
        }''')
s=s.replace('ps->Bind();\n        mesh->mesh->Draw();','''ps->SetTexture(4,settings.materials[9].base.get());
        ps->Bind();
        SunStage::BindShadow();
        mesh->mesh->Draw();''')
s=s.replace('ps->SetTexture(1,nullptr); ps->SetTexture(2,nullptr);','ps->SetTexture(1,nullptr); ps->SetTexture(2,nullptr); ps->SetTexture(4,nullptr);')
s=s.replace('Window patch uses an aperture test. Prop shadows and volumetric beams are not implemented.','Sun shadows follow geometry (2048px cached map). Glass transmits sunlight. No volumetric beams.')
p.write_text(s)
# Lit surface shader.
p=root/'PS_Wall.hlsl';s=p.read_text();s=s.replace('SamplerState samp', 'Texture2D<float> sunDepth : register(t3);\nTexture2D grassMap : register(t4);\nSamplerState samp')
s=s.replace('float4x4 worldToLocal;','float4x4 worldToLocal;\n    float4x4 lightVP;\n    float4 shadow; // enabled, texel size, depth bias, world normal offset')
s=s.replace('float4 main(PS_IN pin) : SV_TARGET','''float SunVisibility(float3 p,float3 n) {
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
float4 main(PS_IN pin, bool front:SV_IsFrontFace) : SV_TARGET''')
s=s.replace('if (material.w>0.5)','if (material.w>0.5 && material.w<1.5)')
s=s.replace('n=DetailNormal(n,pin.worldPos,uv);','''float3 geometryNormal=n;
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
    }''')
s=s.replace('float visibility=WindowVisibility(pin.worldPos,l);\n    float3 col=base*ambient.rgb*windowExtra.w*(1-material.z);','''float3 local=mul(float4(pin.worldPos,1),worldToLocal).xyz;
    bool indoors=material.w<1.5 && abs(local.x)<255 && abs(local.z)<255 && local.y<414;
    float visibility=shadow.x>0.5?SunVisibility(pin.worldPos,geometryNormal):(indoors?WindowVisibility(pin.worldPos,l):1);
    float fill=indoors?windowExtra.w:.8;
    float3 col=base*ambient.rgb*fill*(1-material.z);''')
s=s.replace('col+=BRDF(base,rough,material.z,n,v,toFire*rsqrt(dist2))*FIRE_COLOR*fire.w/(FIRE_SOFTENING+dist2);','''if(material.w<1.5)
        col+=BRDF(base,rough,material.z,n,v,toFire*rsqrt(dist2))*FIRE_COLOR*fire.w/(FIRE_SOFTENING+dist2);
    if(material.w>3.5) col+=base*sun.w*visibility*saturate(dot(-n,l))*.12;
    if(material.w>1.5) {
        const float FOG_START=35, FOG_END=160;
        float fog=saturate((length(eye.xyz-pin.worldPos)-FOG_START)/(FOG_END-FOG_START));
        col=lerp(col,float3(.58,.66,.72),fog);
    }''')
s=s.replace('// Analytic aperture, not a shadow map: walls gate the sun through the measured\n// window rectangle. Props and lattice geometry need a future shadow-map pass.','// Low-cost fallback if the sun shadow resource is unavailable.')
p.write_text(s)
# Both scene initializers consume the same manifest; editor overrides raw transform.
for name,is_editor in [('StageEditor.cpp',True),('SceneForge/SceneForge.cpp',False)]:
 p=root/name;b=p.read_bytes();b=b.replace(b'#include "CottageRender.h"',b'#include "CottageRender.h"\r\n#include "OutdoorStage.h"')
 if b'#include "OutdoorStage.h"' not in b:b=b'#include "OutdoorStage.h"\r\n'+b
 marker=b'\tLoadLayout();';assert b.count(marker)==1
 code='\tfor (const auto& e : OutdoorStage::Read()) {\r\n'
 code+=('\t\tLoadProp(e.key.c_str(),e.path.c_str(),"",1.0f,e.x,e.z,e.yaw);\r\n' if is_editor else '\t\tLoadProp(e.key.c_str(),e.path.c_str(),"",1.0f,e.x,e.y,e.z,e.yaw,false);\r\n')
 code+='\t\tfor(auto& p:m_props) if(p.key==e.key) {p.scale=e.scale;p.pos[1]=e.y;p.groundSnap=false;}\r\n\t}\r\n'
 b=b.replace(marker,code.encode()+marker);p.write_bytes(b)
# Draw a shared sky and cached geometry shadow before rendering the props.
for name,is_editor in [('StageEditor.cpp',True),('SceneForge/Scenery.cpp',False)]:
 p=root/name;b=p.read_bytes()
 if b'#include "OutdoorStage.h"' not in b:b=b.replace(b'#include "CottageRender.h"',b'#include "CottageRender.h"\r\n#include "OutdoorStage.h"')
 marker=(b'void SceneStageEditor::DrawScenery()\r\n{' if is_editor else b'void SceneForge::DrawScenery()\r\n{');assert b.count(marker)==1
 code='''
    std::vector<SunStage::Item> casters;
    XMFLOAT3 shadowCenter(0,3,0);
    for(auto& p:m_props) {
        if(p.key=="StGround") continue;
        auto world=PropWorld(p);
        if(p.key=="StCottage") XMStoreFloat3(&shadowCenter,XMVector3TransformCoord(XMVectorSet(0,150,0,1),world));
        casters.push_back(SunStage::MakeItem(GetObj<Model>(p.key.c_str()),world));
    }
    SunStage::Prepare(casters,CottageRender::Data().sun,shadowCenter);
    SunStage::Sky(GetObj<CameraBase>("Camera"),CottageRender::Data().sun,CottageRender::Data().exteriorSky);
'''.replace('\n','\r\n')
 b=b.replace(marker,marker+code.encode())
 # The existing house branch can render outdoors with the same PBR path.
 if is_editor:
  b=b.replace(b'if (p.key == "StCottage")\r\n        {',b'if (p.key == "StCottage" || OutdoorStage::IsOutdoor(p.key))\r\n        {')
  old=b'GetObj<VertexShader>("StWallVS"), GetObj<PixelShader>("StWallPS"), m_coalPos, false);'
  assert b.count(old)==1;b=b.replace(old,old.replace(b'false);',b'false,OutdoorStage::IsOutdoor(p.key));'))
 else:
  needle=b'if (p.key == "StCottage") { DrawWall(m, PropWorld(p)); continue; }'
  assert b.count(needle)==1
  b=b.replace(needle,needle+b'\r\n        if(OutdoorStage::IsOutdoor(p.key)) {\r\n            CottageRender::Draw(m,PropWorld(p),GetObj<CameraBase>("Camera"),GetObj<VertexShader>("VS_Wall"),GetObj<PixelShader>("PS_Wall"),m_coalPos,false,true);\r\n            continue;\r\n        }')
 # Old teaching plane is below the new continuous yard/foundation; skip duplicate draw.
 needle=b'\t\tModel* m = GetObj<Model>(p.key.c_str());'
 # Only draw loop occurrence? insert skip in DrawScenery portion only.
 start=b.index(marker);tail=b[start:];assert needle in tail;tail=tail.replace(needle,b'\t\tif(p.key=="StGround") continue;\r\n'+needle,1);b=b[:start]+tail
 if is_editor:b=b.replace(b'if (m_props[i].key == "StGround") continue;',b'if (m_props[i].key == "StGround" || m_props[i].key == "StOutdoorGround") continue;')
 p.write_bytes(b)
# New shader compiles through the existing .hlsl -> .cso pipeline.
p=root/'SampleProjectBase.vcxproj';b=p.read_bytes();marker=b'    <FxCompile Include="PS_Wall.hlsl">';pos=b.index(marker);end=b.index(b'    </FxCompile>',pos)+len(b'    </FxCompile>');block=b[pos:end].replace(b'PS_Wall.hlsl',b'PS_Sky.hlsl');b=b[:end]+b'\r\n'+block+b[end:];p.write_bytes(b)
# Make direct sunlight visible against a readable but lower ambient fill.
p=root/'Assets/cottage_materials.txt';s=p.read_text();lines=s.splitlines();lines=['fill 0.95' if l.startswith('fill ') else l for l in lines];p.write_text('\n'.join(lines)+'\n')
print('Integrated manifest, sky, shadows, and outdoor PBR into both scenes')

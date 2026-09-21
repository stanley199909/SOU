// Headless renderer probe: uses production mesh, texture and shader paths, no window/UI capture.
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <memory>
#include <vector>
#include "CottageRender.h"
#include "OutdoorStage.h"
#include "DirectXTex/TextureLoad.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
using namespace DirectX;using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
ID3D11Device* GetDevice(){return dev.Get();} ID3D11DeviceContext* GetContext(){return ctx.Get();}
IDXGISwapChain* GetSwapChain(){return nullptr;}
void SetDepthTest(DepthState mode){D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=mode!=DEPTH_DISABLE;d.DepthWriteMask=mode==DEPTH_ENABLE_WRITE_TEST?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;ComPtr<ID3D11DepthStencilState> s;dev->CreateDepthStencilState(&d,&s);ctx->OMSetDepthStencilState(s.Get(),0);}
void SetBlendMode(BlendMode mode){D3D11_BLEND_DESC d{};auto& t=d.RenderTarget[0];t.RenderTargetWriteMask=15;t.BlendEnable=mode==BLEND_ALPHA;t.SrcBlend=D3D11_BLEND_SRC_ALPHA;t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlendAlpha=D3D11_BLEND_ZERO;ComPtr<ID3D11BlendState> b;dev->CreateBlendState(&d,&b);ctx->OMSetBlendState(b.Get(),nullptr,~0u);}
bool GetVSyncEnabled(){return true;}void SetVSyncEnabled(bool){}
struct Camera:CameraBase {void Update() override {}};
void check(HRESULT hr){if(FAILED(hr)){printf("HRESULT %08x\n",unsigned(hr));exit(2);}}
int main(){
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);D3D_FEATURE_LEVEL fl;
 check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx));
 constexpr UINT W=960,H=540;
 D3D11_TEXTURE2D_DESC d{};d.Width=W;d.Height=H;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target;check(dev->CreateTexture2D(&d,nullptr,&target));ComPtr<ID3D11RenderTargetView> rt;check(dev->CreateRenderTargetView(target.Get(),nullptr,&rt));
 d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;ComPtr<ID3D11Texture2D> depth;check(dev->CreateTexture2D(&d,nullptr,&depth));ComPtr<ID3D11DepthStencilView> ds;check(dev->CreateDepthStencilView(depth.Get(),nullptr,&ds));
 ID3D11RenderTargetView* raw=rt.Get();ctx->OMSetRenderTargets(1,&raw,ds.Get());float bg[4]={.1,.2,.3,1};ctx->ClearRenderTargetView(raw,bg);ctx->ClearDepthStencilView(ds.Get(),D3D11_CLEAR_DEPTH,1,0);
 D3D11_VIEWPORT vp{0,0,float(W),float(H),0,1};ctx->RSSetViewports(1,&vp);
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;check(dev->CreateRasterizerState(&rd,&rs));ctx->RSSetState(rs.Get());
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;check(dev->CreateSamplerState(&sd,&sampler));ID3D11SamplerState* sp=sampler.Get();ctx->PSSetSamplers(0,1,&sp);
 Camera cam;cam.SetPos(XMFLOAT3(-20,7,24));cam.SetLook(XMFLOAT3(0,3,0));
 if(getenv("PROBE_INTERIOR")){cam.SetPos(XMFLOAT3(-3,2.26405f,0));cam.SetLook(XMFLOAT3(0,2.3f,5.8f));}
 CottageRender::Load();for(auto& m:CottageRender::Data().materials)printf("material %s: %d %d %d\n",m.key,bool(m.base),bool(m.normal),bool(m.rough));
 VertexShader vs;PixelShader ps;check(vs.Load("Assets/Shader/VS_Wall.cso"));check(ps.Load(getenv("PROBE_PS")?getenv("PROBE_PS"):"Assets/Shader/PS_Wall.cso"));
 std::vector<std::unique_ptr<Model>> models;std::vector<SunStage::Item> items;
 auto house=std::make_unique<Model>();if(!house->Load("Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx",1,false,true))return 3;
 items.push_back(SunStage::MakeItem(house.get(),XMMatrixScaling(.02143f,.02143f,.02143f)*XMMatrixTranslation(0,.51432f,0)));models.push_back(std::move(house));
 for(auto& e:OutdoorStage::Read()){auto m=std::make_unique<Model>();if(!m->Load(e.path.c_str(),1,false,true))return 3;items.push_back(SunStage::MakeItem(m.get(),XMMatrixScaling(e.scale,e.scale,e.scale)*XMMatrixRotationY(e.yaw)*XMMatrixTranslation(e.x,e.y,e.z)));models.push_back(std::move(m));}
 const size_t outdoorEnd=items.size();
 struct Prop {const char* path;const char* tex;float x,y,z,scale;};
 const Prop props[]={
 {"Anvil/SM_Stump.fbx","Anvil/Textures/T_Anvil_BaseColor.png",0,.66405f,0,.01098f},
 {"Anvil/SM_Anvil.fbx","Anvil/Textures/T_Anvil_BaseColor.png",0,1.20215f,.0073f,.00653f},
 {"Forges/SM_BS_Forge_2_.fbx","Forges/Textures/T_Forge_1_UV1_BaseColor.PNG",1.8f,.66405f,.6f,.00530f}};
 for(auto& p:props){auto m=std::make_unique<Model>();std::string prefix="Assets/MM_Blacksmith_Pack/";if(!m->Load((prefix+p.path).c_str(),1,false,true))return 3;auto tex=TextureCache::Get((prefix+p.tex).c_str());printf("PROP texture %s %d\n",p.tex,bool(tex));m->SetTexture(tex);XMFLOAT3 lo,hi;m->GetLocalAABB(lo,hi);items.push_back(SunStage::MakeItem(m.get(),XMMatrixScaling(p.scale,p.scale,p.scale)*XMMatrixTranslation(p.x,p.y-lo.y*p.scale,p.z)));models.push_back(std::move(m));}
 for(size_t mi=0;mi<models.size();++mi){unsigned zero=0,total=0;for(unsigned j=0;j<models[mi]->GetMeshNum();++j){auto desc=models[mi]->GetMesh(j)->mesh->GetDesc();for(unsigned k=0;k<desc.vtxCount;++k){auto v=reinterpret_cast<const float*>(static_cast<const char*>(desc.pVtx)+k*desc.vtxSize);float len=v[3]*v[3]+v[4]*v[4]+v[5]*v[5];if(!(len>.000001f))++zero;++total;}}printf("NORMALS model %zu zero=%u total=%u\n",mi,zero,total);}
 SunStage::Prepare(items,CottageRender::Data().sun,XMFLOAT3(0,3,0));printf("Sun stage ready=%d shadow=%d\n",SunStage::Data().ready,SunStage::Data().hasShadow);
 SunStage::Sky(&cam,CottageRender::Data().sun,CottageRender::Data().exteriorSky);float fire[3]={1.82f,1.21405f,.54f};
 for(size_t i=0;i<items.size();++i){
 if(i>=outdoorEnd)SunStage::LitProp(items[i].model,XMLoadFloat4x4(&items[i].world),&cam,XMFLOAT4(1,1,1,1),CottageRender::Data().sun,CottageRender::Data().ambient,fire,CottageRender::Data().windowExtra.w);
 else CottageRender::Draw(items[i].model,XMLoadFloat4x4(&items[i].world),&cam,&vs,&ps,fire,false,i>0);}

 CottageRender::Draw(items[0].model,XMLoadFloat4x4(&items[0].world),&cam,&vs,&ps,fire,true);
 ScratchImage image;check(CaptureTexture(dev.Get(),ctx.Get(),target.Get(),image));ScratchImage output;check(output.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,W,H,1,1));
 unsigned nonfinite=0,black=0;double total=0;
 for(UINT y=0;y<H;++y){auto src=reinterpret_cast<const float*>(image.GetPixels()+y*image.GetImage(0,0,0)->rowPitch);auto dst=output.GetPixels()+y*output.GetImage(0,0,0)->rowPitch;
  for(UINT x=0;x<W;++x){float lum=0;for(int c=0;c<3;++c){float v=src[x*4+c];if(!std::isfinite(v)){++nonfinite;v=0;}lum+=v;v=(v*(2.51f*v+.03f))/(v*(2.43f*v+.59f)+.14f);v=powf(fmaxf(0,fminf(1,v)),1/2.2f);dst[x*4+c]=static_cast<unsigned char>(v*255);}dst[x*4+3]=255;if(lum<.001f)++black;total+=lum;}
 }
 check(SaveToWICFile(*output.GetImage(0,0,0),WIC_FLAGS_NONE,GetWICCodec(WIC_CODEC_PNG),L"Tools/outdoor_probe.png"));
 printf("PROBE nonfinite=%u black=%u meanRGBsum=%f\n",nonfinite,black,total/(W*H));return nonfinite?4:0;
}

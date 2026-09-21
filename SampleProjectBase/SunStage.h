#pragma once
#include "Model.h"
#include "Shader.h"
#include "CameraBase.h"
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <cstring>
namespace SunStage {
using namespace DirectX;
using Microsoft::WRL::ComPtr;
constexpr UINT ShadowSize=2048;
constexpr float ShadowWidth=48.0f, LightDistance=60.0f, ShadowNear=.1f, ShadowFar=120.0f;
constexpr float ShadowDepthBias=.00008f, SunDirectionMinLengthSq=.0001f, VerticalSunThreshold=.98f;
constexpr float ReceiverOffset=.012f, SkyRadius=180.0f, FbxUnits=100.0f;
struct Item { Model* model; XMFLOAT4X4 world; };
struct Resources {
 ComPtr<ID3D11Texture2D> depth;
 ComPtr<ID3D11DepthStencilView> dsv;
 ComPtr<ID3D11ShaderResourceView> srv;
 ComPtr<ID3D11RasterizerState> raster;
 VertexShader vs; PixelShader skyPS; PixelShader propPS; Model sky;
 XMFLOAT4X4 lightVP{};
 uint64_t signature=0; bool initialized=false,ready=false,hasShadow=false;
};
inline Resources& Data() {static Resources r;return r;}
inline bool Init() {
 auto& r=Data(); if(r.initialized)return r.ready;r.initialized=true;
 if(FAILED(r.vs.Load("Assets/Shader/VS_Wall.cso")) || FAILED(r.skyPS.Load("Assets/Shader/PS_Sky.cso")))return false;
 if(FAILED(r.propPS.Load("Assets/Shader/PS_StageProp.cso")))return false;
 if(!r.sky.Load("Assets/Outdoor/sky.fbx",1.0f,false,true))return false;
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=ShadowSize;desc.MipLevels=desc.ArraySize=1;
 desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
 desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
 if(FAILED(GetDevice()->CreateTexture2D(&desc,nullptr,&r.depth)))return false;
 D3D11_DEPTH_STENCIL_VIEW_DESC dd{};dd.Format=DXGI_FORMAT_D32_FLOAT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
 if(FAILED(GetDevice()->CreateDepthStencilView(r.depth.Get(),&dd,&r.dsv)))return false;
 D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32_FLOAT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
 if(FAILED(GetDevice()->CreateShaderResourceView(r.depth.Get(),&sd,&r.srv)))return false;
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
 // Receiver normal offset handles acne without detaching thin window-frame shadows.
 if(FAILED(GetDevice()->CreateRasterizerState(&rd,&r.raster)))return false;
 r.ready=true;return true;
}
inline Item MakeItem(Model* m, FXMMATRIX world) {Item i{};i.model=m;XMStoreFloat4x4(&i.world,world);return i;}
inline void Hash(uint64_t& h,const void* bytes,size_t count) {
 constexpr uint64_t FnvPrime=1099511628211ull;
 const auto* p=static_cast<const unsigned char*>(bytes);for(size_t i=0;i<count;++i){h^=p[i];h*=FnvPrime;}
}
inline void Prepare(const std::vector<Item>& items,const XMFLOAT4& sun,const XMFLOAT3& center) {
 if(!Init())return;auto& r=Data();
 uint64_t h=14695981039346656037ull;Hash(h,&sun,sizeof(sun));Hash(h,&center,sizeof(center));
 for(const auto& i:items){Hash(h,&i.model,sizeof(i.model));Hash(h,&i.world,sizeof(i.world));}
 if(r.hasShadow && h==r.signature)return;
 XMVECTOR dir=XMVectorSet(sun.x,sun.y,sun.z,0);
 if(XMVectorGetX(XMVector3LengthSq(dir))<SunDirectionMinLengthSq)dir=XMVectorSet(0,1,0,0);
 dir=XMVector3Normalize(dir);XMVECTOR at=XMLoadFloat3(&center);
 XMVECTOR up=fabsf(XMVectorGetY(dir))>VerticalSunThreshold?XMVectorSet(0,0,1,0):XMVectorSet(0,1,0,0);
 XMMATRIX view=XMMatrixLookAtLH(at+dir*LightDistance,at,up),proj=XMMatrixOrthographicLH(ShadowWidth,ShadowWidth,ShadowNear,ShadowFar);
 XMStoreFloat4x4(&r.lightVP,XMMatrixTranspose(view*proj));
 auto* ctx=GetContext();ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> oldDepth;ComPtr<ID3D11RasterizerState> oldRaster;
 ctx->OMGetRenderTargets(1,&rt,&oldDepth);ctx->RSGetState(&oldRaster);
 UINT count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;D3D11_VIEWPORT oldVP[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];ctx->RSGetViewports(&count,oldVP);
 ID3D11ShaderResourceView* nullSRV=nullptr;ctx->PSSetShaderResources(3,1,&nullSRV);
 ctx->OMSetRenderTargets(0,nullptr,r.dsv.Get());ctx->ClearDepthStencilView(r.dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
 D3D11_VIEWPORT vp{0,0,float(ShadowSize),float(ShadowSize),0,1};ctx->RSSetViewports(1,&vp);ctx->RSSetState(r.raster.Get());
 SetDepthTest(DEPTH_ENABLE_WRITE_TEST);SetBlendMode(BLEND_NONE);PixelShader::Unbind();
 XMFLOAT4X4 matrices[3];XMStoreFloat4x4(&matrices[1],XMMatrixTranspose(view));XMStoreFloat4x4(&matrices[2],XMMatrixTranspose(proj));
 for(const auto& i:items) {if(!i.model)continue;XMStoreFloat4x4(&matrices[0],XMMatrixTranspose(XMLoadFloat4x4(&i.world)));
  r.vs.WriteBuffer(0,matrices);r.vs.Bind();
  for(unsigned k=0;k<i.model->GetMeshNum();++k){auto* mesh=i.model->GetMesh(k);const char* name=i.model->GetMaterialName(mesh->materialID);
   if(name && strstr(name,"Glass"))continue;mesh->mesh->Draw();}
 }
 ID3D11RenderTargetView* target=rt.Get();ctx->OMSetRenderTargets(1,&target,oldDepth.Get());ctx->RSSetViewports(count,oldVP);ctx->RSSetState(oldRaster.Get());
 SetBlendMode(BLEND_ALPHA);r.signature=h;r.hasShadow=true;
}
inline void BindShadow() {auto* srv=Data().srv.Get();GetContext()->PSSetShaderResources(3,1,&srv);}
inline void Sky(CameraBase* camera,const XMFLOAT4& sun,const XMFLOAT4& color) {
 if(!camera || !Init())return;auto& r=Data();auto eye=camera->GetPos();
 XMFLOAT4X4 mat[3];XMStoreFloat4x4(&mat[0],XMMatrixTranspose(XMMatrixScaling(SkyRadius/FbxUnits,SkyRadius/FbxUnits,SkyRadius/FbxUnits)*XMMatrixTranslation(eye.x,eye.y,eye.z)));
 mat[1]=camera->GetView();mat[2]=camera->GetProj();r.vs.WriteBuffer(0,mat);
 XMFLOAT4 params[3]={{eye.x,eye.y,eye.z,0},sun,color};r.skyPS.WriteBuffer(0,params);
 ComPtr<ID3D11RasterizerState> old;GetContext()->RSGetState(&old);GetContext()->RSSetState(r.raster.Get());
 SetDepthTest(DEPTH_DISABLE);SetBlendMode(BLEND_NONE);r.vs.Bind();r.skyPS.Bind();
 for(unsigned i=0;i<r.sky.GetMeshNum();++i)r.sky.GetMesh(i)->mesh->Draw();
 GetContext()->RSSetState(old.Get());SetDepthTest(DEPTH_ENABLE_WRITE_TEST);SetBlendMode(BLEND_ALPHA);
}
struct PropParams {XMFLOAT4 tint,eye,sun,ambient,fire;XMFLOAT4X4 lightVP;XMFLOAT4 shadow;};
static_assert(sizeof(PropParams)==160,"PS_StageProp constants");
inline void LitProp(Model* model,FXMMATRIX world,CameraBase* camera,const XMFLOAT4& tint,
                    const XMFLOAT4& sun,const XMFLOAT4& ambient,const float* fire,float fill) {
 if(!model || !camera || !Init())return;auto& r=Data();auto eye=camera->GetPos();
 XMFLOAT4X4 mat[3];XMStoreFloat4x4(&mat[0],XMMatrixTranspose(world));mat[1]=camera->GetView();mat[2]=camera->GetProj();r.vs.WriteBuffer(0,mat);
 PropParams p{};p.tint=tint;p.eye={eye.x,eye.y,eye.z,0};p.sun=sun;p.ambient={ambient.x,ambient.y,ambient.z,fill};p.fire={fire[0],fire[1],fire[2],ambient.w};p.lightVP=r.lightVP;
 p.shadow={r.hasShadow?1.0f:0.0f,1.0f/ShadowSize,ShadowDepthBias,ReceiverOffset};
 r.propPS.WriteBuffer(0,&p);r.vs.Bind();SetDepthTest(DEPTH_ENABLE_WRITE_TEST);SetBlendMode(BLEND_NONE);
 for(unsigned i=0;i<model->GetMeshNum();++i){auto* mesh=model->GetMesh(i);r.propPS.SetTexture(0,model->GetTextureAt(mesh->materialID));r.propPS.Bind();BindShadow();mesh->mesh->Draw();}
 SetBlendMode(BLEND_ALPHA);
}

}

// Headless check of the prop PBR material path (2026-10-16).
// Renders the anvil + stump close up three times with the production shaders and LitRender:
//   normal map ON (table setting) / normal map OFF / normal green FLIPPED
// and prints how much the images differ, so "does the normal map do anything?" is measured, not guessed.
// Also renders a bright steel material (the blade's) on the anvil to check over-exposure.
// Build: Tools\build_pbr_material_probe.cmd   Run from SampleProjectBase.
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
#include <string>
#include "CottageRender.h"
#include "LitRender.h"
#include "PropMaterials.h"
#include "DirectXTex/TextureLoad.h"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
using namespace DirectX; using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
ID3D11Device* GetDevice() { return dev.Get(); } ID3D11DeviceContext* GetContext() { return ctx.Get(); }
IDXGISwapChain* GetSwapChain() { return nullptr; }
void SetDepthTest(DepthState mode) { D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable = mode != DEPTH_DISABLE; d.DepthWriteMask = mode == DEPTH_ENABLE_WRITE_TEST ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO; d.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; ComPtr<ID3D11DepthStencilState> s; dev->CreateDepthStencilState(&d, &s); ctx->OMSetDepthStencilState(s.Get(), 0); }
void SetBlendMode(BlendMode mode) { D3D11_BLEND_DESC d{}; auto& t = d.RenderTarget[0]; t.RenderTargetWriteMask = 15; t.BlendEnable = mode == BLEND_ALPHA; t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = t.BlendOpAlpha = D3D11_BLEND_OP_ADD; t.SrcBlendAlpha = D3D11_BLEND_ONE; t.DestBlendAlpha = D3D11_BLEND_ZERO; ComPtr<ID3D11BlendState> b; dev->CreateBlendState(&d, &b); ctx->OMSetBlendState(b.Get(), nullptr, ~0u); }
bool GetVSyncEnabled() { return true; } void SetVSyncEnabled(bool) {}
struct Camera : CameraBase { void Update() override {} };
void check(HRESULT hr, const char* what) { if (FAILED(hr)) { printf("FAILED %s %08x\n", what, unsigned(hr)); exit(2); } }

constexpr UINT W = 640, H = 480;
ComPtr<ID3D11Texture2D> target, depth; ComPtr<ID3D11RenderTargetView> rt; ComPtr<ID3D11DepthStencilView> ds;

std::vector<float> Render(const std::vector<std::pair<Model*, XMMATRIX>>& items, Camera& cam, const PropMaterial& anvilMat, const PropMaterial& stumpMat)
{
	ID3D11RenderTargetView* raw = rt.Get(); ctx->OMSetRenderTargets(1, &raw, ds.Get());
	float bg[4] = { 0, 0, 0, 1 }; ctx->ClearRenderTargetView(raw, bg); ctx->ClearDepthStencilView(ds.Get(), D3D11_CLEAR_DEPTH, 1, 0);
	D3D11_VIEWPORT vp{ 0, 0, float(W), float(H), 0, 1 }; ctx->RSSetViewports(1, &vp);
	LitRender::Draw(items[0].first, items[0].second, &cam, stumpMat);
	LitRender::Draw(items[1].first, items[1].second, &cam, anvilMat);
	ScratchImage image; check(CaptureTexture(dev.Get(), ctx.Get(), target.Get(), image), "capture");
	std::vector<float> px(W * H * 3);
	for (UINT y = 0; y < H; ++y) { auto src = reinterpret_cast<const float*>(image.GetPixels() + y * image.GetImage(0, 0, 0)->rowPitch); for (UINT x = 0; x < W; ++x) for (int c = 0; c < 3; ++c) px[(y * W + x) * 3 + c] = src[x * 4 + c]; }
	return px;
}
void Save(const std::vector<float>& px, const wchar_t* path)
{
	ScratchImage out; check(out.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, 1), "init");
	for (UINT y = 0; y < H; ++y) { auto dst = out.GetPixels() + y * out.GetImage(0, 0, 0)->rowPitch; for (UINT x = 0; x < W; ++x) { for (int c = 0; c < 3; ++c) { float v = px[(y * W + x) * 3 + c]; v = (v * (2.51f * v + .03f)) / (v * (2.43f * v + .59f) + .14f); v = powf(fmaxf(0, fminf(1, v)), 1 / 2.2f); dst[x * 4 + c] = (unsigned char)(v * 255); } dst[x * 4 + 3] = 255; } }
	check(SaveToWICFile(*out.GetImage(0, 0, 0), WIC_FLAGS_NONE, GetWICCodec(WIC_CODEC_PNG), path), "save");
}
double MeanAbsDiff(const std::vector<float>& a, const std::vector<float>& b) { double s = 0; for (size_t i = 0; i < a.size(); ++i) s += fabs(a[i] - b[i]); return s / a.size(); }
double Mean(const std::vector<float>& a) { double s = 0; for (float v : a) s += v; return s / a.size(); }
double FractionOver(const std::vector<float>& a, float t) { size_t n = 0; for (size_t i = 0; i < a.size(); i += 3) if ((a[i] + a[i + 1] + a[i + 2]) / 3 > t) ++n; return double(n) / (a.size() / 3); }

int main()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED); D3D_FEATURE_LEVEL fl;
	check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx), "device");
	D3D11_TEXTURE2D_DESC d{}; d.Width = W; d.Height = H; d.MipLevels = d.ArraySize = 1; d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_RENDER_TARGET;
	check(dev->CreateTexture2D(&d, nullptr, &target), "rt"); check(dev->CreateRenderTargetView(target.Get(), nullptr, &rt), "rtv");
	d.Format = DXGI_FORMAT_D32_FLOAT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL; check(dev->CreateTexture2D(&d, nullptr, &depth), "depth"); check(dev->CreateDepthStencilView(depth.Get(), nullptr, &ds), "dsv");
	D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; ComPtr<ID3D11RasterizerState> rs; check(dev->CreateRasterizerState(&rd, &rs), "rs"); ctx->RSSetState(rs.Get());
	D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = D3D11_FLOAT32_MAX; ComPtr<ID3D11SamplerState> sampler; check(dev->CreateSamplerState(&sd, &sampler), "sampler"); ID3D11SamplerState* sp = sampler.Get(); ctx->PSSetSamplers(0, 1, &sp);

	CottageRender::Load();
	const std::string P = "Assets/MM_Blacksmith_Pack/";
	auto base = TextureCache::Get((P + "Anvil/Textures/T_Anvil_BaseColor.png").c_str());
	auto stump = std::make_unique<Model>(); auto anvil = std::make_unique<Model>();
	if (!stump->Load((P + "Anvil/SM_Stump.fbx").c_str(), 1, false, true) || !anvil->Load((P + "Anvil/SM_Anvil.fbx").c_str(), 1, false, true)) { printf("model load failed\n"); return 3; }
	stump->SetTexture(base); anvil->SetTexture(base);
	const float S1 = .01098f, S2 = .00653f;
	XMFLOAT3 lo, hi; stump->GetLocalAABB(lo, hi); XMMATRIX ws = XMMatrixScaling(S1, S1, S1) * XMMatrixTranslation(0, -lo.y * S1, 0);
	float top = (hi.y - lo.y) * S1;
	anvil->GetLocalAABB(lo, hi); XMMATRIX wa = XMMatrixScaling(S2, S2, S2) * XMMatrixTranslation(0, top - lo.y * S2, 0);
	std::vector<std::pair<Model*, XMMATRIX>> items = { { stump.get(), ws }, { anvil.get(), wa } };

	Camera cam; cam.SetPos(XMFLOAT3(0.9f, top + 0.7f, -0.9f)); cam.SetLook(XMFLOAT3(0, top + 0.1f, 0));
	float fire[3] = { 1.8f, 0.6f, 0.6f }; LitRender::SetFire(fire);

	bool hasN = false; { auto t = TextureCache::Get(PropMaterials::Find("StAnvil").normalTex, false); hasN = (t != nullptr); }
	printf("anvil normal map path: %s  loaded=%d\n", PropMaterials::Find("StAnvil").normalTex, hasN);

	{ SceneLightParams L = LitRender::Lights(&cam);
	  printf("LIGHTS sun=(%.2f %.2f %.2f) w=%.2f  ambient=(%.2f %.2f %.2f) fill=%.2f  fire=(%.2f %.2f %.2f) w=%.2f shadow=%.0f\n",
	         L.sun.x, L.sun.y, L.sun.z, L.sun.w, L.ambient.x, L.ambient.y, L.ambient.z, L.ambient.w, L.fire.x, L.fire.y, L.fire.z, L.fire.w, L.shadow.x); }
	PropMaterial a = PropMaterials::Find("StAnvil"), s = PropMaterials::Find("StStump");
	PropMaterial aOff = a, sOff = s; aOff.normalTex = sOff.normalTex = nullptr;
	PropMaterial aFlip = a, sFlip = s; aFlip.normalDirectX = !a.normalDirectX; sFlip.normalDirectX = !s.normalDirectX;
	PropMaterial aSteel = PropMaterials::Find("Weapon"), sSame = s;

	auto on = Render(items, cam, a, s);       Save(on, L"Tools/pbr_normal_on.png");
	auto off = Render(items, cam, aOff, sOff); Save(off, L"Tools/pbr_normal_off.png");
	auto flip = Render(items, cam, aFlip, sFlip); Save(flip, L"Tools/pbr_normal_flip.png");
	auto steel = Render(items, cam, aSteel, sSame); Save(steel, L"Tools/pbr_weapon_material.png");
	// direct light only: switch the room fill off, keep sun + fire
	const float keepFill = CottageRender::Data().windowExtra.w;
	CottageRender::Data().windowExtra.w = 0.0f;
	auto directOn  = Render(items, cam, a, s);       Save(directOn, L"Tools/pbr_direct_on.png");
	auto directOff = Render(items, cam, aOff, sOff);
	PropMaterial aNoOrm = a, sNoOrm = s; aNoOrm.ormTex = sNoOrm.ormTex = nullptr;
	auto directNoOrm = Render(items, cam, aNoOrm, sNoOrm);
	printf("DIRECT ONLY: |ORM on-off|=%.5f\n", MeanAbsDiff(directOn, directNoOrm));
	CottageRender::Data().windowExtra.w = keepFill;
	printf("DIRECT ONLY: mean=%.5f  |ON-OFF|=%.5f\n", Mean(directOn), MeanAbsDiff(directOn, directOff));
	if (GetFileAttributesA("Tools/PS_PropDebug.cso") != INVALID_FILE_ATTRIBUTES)
	{
		SunStage::Data().propPS.Load("Tools/PS_PropDebug.cso");	// debug variant: outputs the shading normal as color
		auto nOn = Render(items, cam, a, s);  Save(nOn, L"Tools/pbr_debug_normal_on.png");
		auto nOff = Render(items, cam, aOff, sOff);
		printf("DEBUG NORMAL: |ON-OFF|=%.5f\n", MeanAbsDiff(nOn, nOff));
	}
	printf("mean |ON-OFF|  = %.5f\n", MeanAbsDiff(on, off));
	printf("mean |ON-FLIP| = %.5f\n", MeanAbsDiff(on, flip));
	printf("mean brightness ON=%.4f  steel(Weapon material)=%.4f  fraction>1.0: ON=%.3f steel=%.3f\n", Mean(on), Mean(steel), FractionOver(on, 1.0f), FractionOver(steel, 1.0f));
	return 0;
}

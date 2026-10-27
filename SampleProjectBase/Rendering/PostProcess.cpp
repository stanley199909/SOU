#include "PostProcess.h"
#include "DirectX.h"
#include "Sprite.h"
#include "Defines.h"	// SSAA_SCALE
#include "AutoExposure.h"	// 自動露出(目の慣れ)

using namespace DirectX;

// ブルーム用ピクセルシェーダー(高輝度抽出＋分離ガウスぼかし)
static const char* g_bloomShaderCode = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
cbuffer B : register(b0)
{
	int    mode;		// 0=高輝度抽出 / 1=ぼかし
	float  threshold;
	float2 dir;			// ぼかし方向
	float2 texel;		// 1テクセルの大きさ
	float2 _pad;
};
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 color:TEXCOORD1; };
float4 main(PIN i) : SV_TARGET
{
	if (mode == 0)		// 明るい部分だけ取り出す
	{
		float3 c = tex.Sample(samp, i.uv).rgb;
		float  l = max(c.r, max(c.g, c.b));
		float  k = saturate((l - threshold) / (1.0 - threshold));
		return float4(c * k, 1.0);
	}
	// 分離ガウスぼかし(9タップ)
	float2 o = texel * dir;
	float w[5] = { 0.227, 0.194, 0.121, 0.054, 0.016 };
	float3 s = tex.Sample(samp, i.uv).rgb * w[0];
	[unroll] for (int k = 1; k < 5; ++k)
	{
		s += tex.Sample(samp, i.uv + o * k).rgb * w[k];
		s += tex.Sample(samp, i.uv - o * k).rgb * w[k];
	}
	return float4(s, 1.0);
}
)EOT";

// FXAA (Fast Approximate Anti-Aliasing, Lottes/NVIDIA, 簡略版)。テクスチャ混叠はmipで
// 解決済みなので、ここが受け持つのは「幾何のエッジ(石の輪郭/稜線)がカメラ移動で階段状に
// 這うチラつき」。輝度でエッジを検出しエッジ沿いに混ぜて均す。1パスで軽い(核显向け)。
static const char* g_fxaaShaderCode = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
cbuffer FXAA : register(b0) { float2 rcpFrame; float2 _pad; };
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 color:TEXCOORD1; };
float FxLuma(float3 c){ return dot(c, float3(0.299,0.587,0.114)); }

//公式 Narkowicz ACES
float3 ACESFilm(float3 x){
    float a=2.51, b=0.03, c=2.43, d=0.59, e=0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float4 main(PIN i):SV_TARGET
{
	float2 uv=i.uv;
	float3 m =tex.Sample(samp,uv).rgb;
	float3 nw=tex.Sample(samp,uv+float2(-1,-1)*rcpFrame).rgb;
	float3 ne=tex.Sample(samp,uv+float2( 1,-1)*rcpFrame).rgb;
	float3 sw=tex.Sample(samp,uv+float2(-1, 1)*rcpFrame).rgb;
	float3 se=tex.Sample(samp,uv+float2( 1, 1)*rcpFrame).rgb;
	float lm=FxLuma(m),lnw=FxLuma(nw),lne=FxLuma(ne),lsw=FxLuma(sw),lse=FxLuma(se);
	float lmin=min(lm,min(min(lnw,lne),min(lsw,lse)));
	float lmax=max(lm,max(max(lnw,lne),max(lsw,lse)));
	float e=i.color.r;	// exposure (auto exposure, AutoExposure.cpp): applied to the HDR colour right before ACES
	if((lmax-lmin) < max(0.0625, lmax*0.125))
	{
		// Not an edge: sharpen the fine detail a little (the 3D scene is drawn at a lower internal
		// resolution and stretched up, which softens the metal texture). Unsharp mask = push the pixel
		// away from the average of its neighbours, then CLAMP it inside the neighbours' min..max so it
		// can never overshoot into a bright/dark halo -- the core idea of AMD FidelityFX CAS
		// (contrast adaptive sharpening). Search: "contrast adaptive sharpening", "unsharp mask".
		const float SHARPEN = 0.8;	// strength (0 = off)
		float3 avg = (nw + ne + sw + se) * 0.25;
		float3 lo = min(m, min(min(nw, ne), min(sw, se)));
		float3 hi = max(m, max(max(nw, ne), max(sw, se)));
		float3 sharp = clamp(m + (m - avg) * SHARPEN, lo, hi);
		return float4(ACESFilm(sharp*e),1.0);
	}
	float2 dir;
	dir.x=-((lnw+lne)-(lsw+lse));
	dir.y= ((lnw+lsw)-(lne+lse));
	float red=max((lnw+lne+lsw+lse)*0.25*(1.0/8.0),1.0/128.0);
	float rcp=1.0/(min(abs(dir.x),abs(dir.y))+red);
	dir=clamp(dir*rcp,float2(-8,-8),float2(8,8))*rcpFrame;
	float3 a=0.5*(tex.Sample(samp,uv+dir*(1.0/3.0-0.5)).rgb+tex.Sample(samp,uv+dir*(2.0/3.0-0.5)).rgb);
	float3 b=a*0.5+0.25*(tex.Sample(samp,uv+dir*-0.5).rgb+tex.Sample(samp,uv+dir*0.5).rgb);
	float lb=FxLuma(b);
	if(lb<lmin||lb>lmax) return float4(ACESFilm(a*e),1.0);
	return float4(ACESFilm(b*e),1.0);
}
)EOT";

// トーンマッピング専用PS: HDR(リニア空間, 1超えあり)を ACES のS字曲線で 0..1 へ優雅に圧縮する。
// 以前 ACES は FXAA シェーダーの中だけにあり、FXAA を切ると収光されず亮部が白飛びした。
// これを独立させ「画面へ出す最後に必ず1回収光する」形にするための最小のパス。
static const char* g_tonemapShaderCode = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 color:TEXCOORD1; };
// Narkowicz 2015 ACES fitted curve (業界標準の近似式。係数の導出は黒箱でよい)
float3 ACESFilm(float3 x){
    float a=2.51, b=0.03, c=2.43, d=0.59, e=0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}
float4 main(PIN i):SV_TARGET
{
    float3 c = tex.Sample(samp, i.uv).rgb;
    return float4(ACESFilm(c * i.color.r), 1.0);   // r = exposure (auto exposure)
}
)EOT";

void PostProcess::Init(UINT width, UINT height)
{
	m_width  = width;
	m_height = height;

	// シーン描画用のオフスクリーンRTを作成(SSAA: 画面のSSAA_SCALE倍で描き、合成時に縮小)
	m_sceneRT.Create(DXGI_FORMAT_R16G16B16A16_FLOAT, RenderPixels(width), RenderPixels(height));
	// water refraction snapshot (sceneRTと同解像度でコピー)
	m_refractRT.Create(DXGI_FORMAT_R16G16B16A16_FLOAT, RenderPixels(width), RenderPixels(height));
	// ブルーム用は半解像度(軽くて柔らかくなる)
	m_brightRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width / 2, height / 2);
	m_blurRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width / 2, height / 2);

	// 効果シェーダーをコンパイル
	m_bloomPS = std::make_shared<PixelShader>();
	m_bloomPS->Compile(g_bloomShaderCode);
	m_fxaaPS = std::make_shared<PixelShader>();
	m_fxaaPS->Compile(g_fxaaShaderCode);
	m_tonemapPS = std::make_shared<PixelShader>();
	m_tonemapPS->Compile(g_tonemapShaderCode);
}

void PostProcess::Resize(UINT width, UINT height)
{
	m_width  = width;
	m_height = height;
	m_brightRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width / 2, height / 2);
	m_blurRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width / 2, height / 2);
}

void PostProcess::Uninit()
{
	AutoExposure::Uninit();
	m_fxaaPS.reset();
	m_tonemapPS.reset();
	m_bloomPS.reset();
}

void PostProcess::Update(float tick)
{
	AutoExposure::Update(tick);	// 目の慣れ: 露出を目標へ少しずつ近づける
}

void PostProcess::Begin(DepthStencil* pDSV)
{
	// 内部解像度(設定の「画質」)が変わっていたら、場面の描画先と深度を新しい大きさで作り直す
	const UINT rw = RenderPixels(m_width), rh = RenderPixels(m_height);
	if (m_sceneRT.GetWidth() != rw || m_sceneRT.GetHeight() != rh)
	{
		m_sceneRT.Create(DXGI_FORMAT_R16G16B16A16_FLOAT, rw, rh);
		m_refractRT.Create(DXGI_FORMAT_R16G16B16A16_FLOAT, rw, rh);
		if (pDSV) pDSV->Create(rw, rh, false);
	}
	// 描画先をオフスクリーンRTへ
	RenderTarget* pRT = &m_sceneRT;
	SetRenderTargets(1, &pRT, pDSV);

	// クリア(暗い背景でパーティクルの光を映えさせる)
	float clear[4] = { 0.02f, 0.02f, 0.04f, 1.0f };
	m_sceneRT.Clear(clear);
	if (pDSV) pDSV->Clear();

	// 3D描画用の標準ステートに戻す(InitDirectXの初期値に合わせる)
	SetCullingMode(D3D11_CULL_NONE);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	SetBlendMode(BLEND_ALPHA);
	SetSamplerState(SAMPLER_LINEAR);
}

// Copy the scene rendered so far into m_refractRT and hand it to the water PS.
// CopyResource does not change the current render target, so m_sceneRT stays
// bound and the water is still drawn into the scene (and gets bloomed later).
Texture* PostProcess::CaptureScene()
{
	GetContext()->CopyResource(m_refractRT.GetTexture2D(), m_sceneRT.GetTexture2D());
	return &m_refractRT;
}

void PostProcess::End(RenderTarget* pScreen)
{
	// ブルーム → 露出 + ACES(+FXAA)で画面へ。
	// (授業課題05 の「6種デモモード」と B/←/→/V キーは 2026-10-25 に削除。ゲーム中に B で白黒等へ切り替わっていた)
	AutoExposure::Measure(&m_sceneRT);	// 今の絵の平均の明るさを測る(読み取りは数フレーム後=待たない)
	SetDepthTest(DEPTH_DISABLE);
	SetSamplerState(SAMPLER_LINEAR);

	float hx = 1.0f / (m_width  * 0.5f);	// 半解像度の1テクセル
	float hy = 1.0f / (m_height * 0.5f);

	// 1) 高輝度抽出: sceneRT → brightRT
	RenderTarget* rt = &m_brightRT;
	SetRenderTargets(1, &rt, nullptr);
	SetBlendMode(BLEND_NONE);
	DrawBloom(0, &m_sceneRT, m_bloomThreshold, 0.0f, 0.0f, 0.0f, 0.0f);

	// 2) 横ぼかし: brightRT → blurRT
	rt = &m_blurRT;
	SetRenderTargets(1, &rt, nullptr);
	DrawBloom(1, &m_brightRT, 0.0f, 1.0f, 0.0f, hx, hy);

	// 3) 縦ぼかし: blurRT → brightRT
	rt = &m_brightRT;
	SetRenderTargets(1, &rt, nullptr);
	DrawBloom(1, &m_blurRT, 0.0f, 0.0f, 1.0f, hx, hy);

	// 4) 合成: 画面 = sceneRT + brightRT(加算)
	SetRenderTargets(1, &pScreen, nullptr);
	SetBlendMode(BLEND_NONE);
	const float e = AutoExposure::Exposure();				// 自動露出(目の慣れ)。色の r で ACES の直前に掛ける
	if (m_fxaa) DrawFXAA(&m_sceneRT, e);					// 元の絵(エッジを均す)
	else        DrawFull(&m_sceneRT, m_tonemapPS.get(), XMFLOAT4(e, e, e, 1));	// FXAA無しでも必ずACESで収光
	SetBlendMode(BLEND_ADD);
	float b = m_bloomStrength * e;							// にじむ光も同じ露出で(窓を見て目が慣れたら光のにじみも弱まる)
	DrawFull(&m_brightRT, nullptr, XMFLOAT4(b, b, b, 1));	// にじむ光を加算

	SetBlendMode(BLEND_ALPHA);
	ID3D11ShaderResourceView* pNull0 = nullptr;
	GetContext()->PSSetShaderResources(0, 1, &pNull0);
}

// FXAAの1パス(全画面)。rcpFrame=1テクセル。エッジ(幾何の輪郭)のチラつきを均す。
void PostProcess::DrawFXAA(Texture* src, float exposure)
{
	struct FParam { float rcp[2]; float _pad[2]; } p = {};
	p.rcp[0] = 1.0f / (float)RenderPixels(m_width);	// FXAA は内部解像度の 1 画素単位で隣を見る
	p.rcp[1] = 1.0f / (float)RenderPixels(m_height);
	m_fxaaPS->WriteBuffer(0, &p);
	DrawFull(src, m_fxaaPS.get(), XMFLOAT4(exposure, exposure, exposure, 1));	// r = 露出(シェーダーが ACES の前に掛ける)
}

// ブルームの1パス(全画面)を描画
void PostProcess::DrawBloom(int mode, Texture* src, float threshold, float dx, float dy, float tx, float ty)
{
	// パラメータ(HLSLのcbuffer Bと同じ並び)
	struct BParam
	{
		int   mode;
		float threshold;
		float dir[2];
		float texel[2];
		float _pad[2];
	} p = {};
	p.mode = mode;
	p.threshold = threshold;
	p.dir[0] = dx;   p.dir[1] = dy;
	p.texel[0] = tx; p.texel[1] = ty;
	m_bloomPS->WriteBuffer(0, &p);

	DrawFull(src, m_bloomPS.get(), XMFLOAT4(1, 1, 1, 1));
}

// 全画面にスプライトを描画(ps=nullptrならSprite標準シェーダー)
void PostProcess::DrawFull(Texture* src, Shader* ps, DirectX::XMFLOAT4 color)
{
	XMFLOAT4X4 ident;
	XMStoreFloat4x4(&ident, XMMatrixIdentity());
	Sprite::SetWorld(ident);
	Sprite::SetView(ident);
	Sprite::SetProjection(ident);
	Sprite::SetOffset(XMFLOAT2(0.0f, 0.0f));
	Sprite::SetSize(XMFLOAT2(2.0f, 2.0f));
	Sprite::SetColor(color);
	Sprite::SetTexture(src);
	Sprite::SetPixelShader(ps);	// nullptrならSprite標準PS(tex*color)
	Sprite::Draw();
}

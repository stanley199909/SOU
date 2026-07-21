#include "PostProcess.h"
#include "DirectX.h"
#include "Sprite.h"
#include "Input.h"
#include "DebugUI.h"
#include <math.h>

using namespace DirectX;

// ポストプロセス用ピクセルシェーダー(実行時コンパイル)
// Sprite の頂点シェーダー出力(pos/uv/color)を受け取る
static const char* g_ppShaderCode = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);

cbuffer Param : register(b0)
{
	int    effect;
	float  intensity;
	float  time;
	float  _pad0;
	float2 resolution;
	float2 _pad1;
};

struct PS_IN
{
	float4 pos   : SV_POSITION;
	float2 uv    : TEXCOORD0;
	float4 color : TEXCOORD1;
};

// 疑似乱数
float rand(float2 co)
{
	return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float4 main(PS_IN pin) : SV_TARGET
{
	float2 uv  = pin.uv;
	float4 src = tex.Sample(samp, uv);
	float3 col = src.rgb;
	float3 outc = col;

	if (effect == 0)			// モノクローム
	{
		float g = dot(col, float3(0.299, 0.587, 0.114));
		outc = float3(g, g, g);
	}
	else if (effect == 1)		// セピア
	{
		float g = dot(col, float3(0.299, 0.587, 0.114));
		outc = float3(g * 1.07, g * 0.74, g * 0.43);
	}
	else if (effect == 2)		// モザイク
	{
		float block = 12.0;
		float2 bs = block / resolution;
		float2 muv = (floor(uv / bs) + 0.5) * bs;
		outc = tex.Sample(samp, muv).rgb;
	}
	else if (effect == 3)		// ポスタリゼーション
	{
		float levels = 5.0;
		outc = floor(col * levels) / levels;
	}
	else if (effect == 4)		// 色収差(RGB Split)
	{
		float2 dir = uv - 0.5;
		float amount = 0.008;
		float r = tex.Sample(samp, uv + dir * amount).r;
		float g = tex.Sample(samp, uv).g;
		float b = tex.Sample(samp, uv - dir * amount).b;
		outc = float3(r, g, b);
	}
	else if (effect == 5)		// ノイズ
	{
		float n = rand(uv + frac(time));
		outc = saturate(col + (n - 0.5) * 0.4);
	}
	else						// CRT(授業外・おまけ:走査線＋周辺減光)
	{
		float scan = 0.75 + 0.25 * sin(uv.y * resolution.y * 3.14159);
		float2 d = uv - 0.5;
		float vig = smoothstep(0.8, 0.15, dot(d, d) * 2.0);
		outc = col * scan * vig;
	}

	// 強度でフェード(intensityはCPU側で時間変化させる)
	float3 fin = lerp(col, outc, saturate(intensity));
	return float4(fin, src.a);
}
)EOT";

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

void PostProcess::Init(UINT width, UINT height)
{
	m_width  = width;
	m_height = height;

	// シーン描画用のオフスクリーンRTを作成
	m_sceneRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
	// ブルーム用は半解像度(軽くて柔らかくなる)
	m_brightRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width / 2, height / 2);
	m_blurRT.Create(DXGI_FORMAT_R8G8B8A8_UNORM, width / 2, height / 2);

	// 効果シェーダーをコンパイル
	m_ppPS = std::make_shared<PixelShader>();
	m_ppPS->Compile(g_ppShaderCode);
	m_bloomPS = std::make_shared<PixelShader>();
	m_bloomPS->Compile(g_bloomShaderCode);
}

void PostProcess::Uninit()
{
	m_ppPS.reset();
	m_bloomPS.reset();
}

void PostProcess::Update(float tick)
{
	m_time += tick;

	// SHIFTはシーン切り替えに使われているので、押していない時だけ操作を受け付ける
	if (IsKeyPress(VK_SHIFT)) return;

	// B … ブルーム(パーティクル用) ↔ 6種デモ(課題05) の切り替え
	if (IsKeyTrigger('B')) m_mode = (m_mode == MODE_BLOOM) ? MODE_DEMO : MODE_BLOOM;

	if (IsKeyTrigger(VK_LEFT))  m_current = (m_current + EFFECT_MAX - 1) % EFFECT_MAX;
	if (IsKeyTrigger(VK_RIGHT)) m_current = (m_current + 1) % EFFECT_MAX;
	if (IsKeyTrigger('V')) m_split = !m_split;	// 分割一括表示の切り替え
	if (IsKeyTrigger('F')) m_fade  = !m_fade;	// 時間による強度変化の切り替え
}

void PostProcess::DrawUI()
{
	static const char* effectNames[] = {
		"Monochrome", "Sepia", "Mosaic", "Posterization",
		"Chromatic Aberration", "Noise", "CRT (bonus)"
	};

	ImGui::SetNextWindowPos(ImVec2(12, 290), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Post Process");

	ImGui::RadioButton("Bloom", &m_mode, MODE_BLOOM);
	ImGui::SameLine();
	ImGui::RadioButton("Effects Demo", &m_mode, MODE_DEMO);
	ImGui::Separator();

	if (m_mode == MODE_BLOOM)
	{
		ImGui::SliderFloat("Threshold", &m_bloomThreshold, 0.0f, 0.95f);
		ImGui::SliderFloat("Strength", &m_bloomStrength, 0.0f, 3.0f);
	}
	else
	{
		ImGui::Checkbox("Split view (6 effects)", &m_split);
		if (!m_split)
		{
			ImGui::Combo("Effect", &m_current, effectNames, IM_ARRAYSIZE(effectNames));
		}
	}
	ImGui::Checkbox("Time fade", &m_fade);

	ImGui::End();
}

void PostProcess::Begin(DepthStencil* pDSV)
{
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

void PostProcess::End(RenderTarget* pScreen)
{
	// 時間による強度(フェード)
	float intensity = m_fade ? (0.5f + 0.5f * sinf(m_time * 2.0f)) : 1.0f;

	// ==== ブルームモード(パーティクル向け・既定) ====
	if (m_mode == MODE_BLOOM)
	{
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
		DrawFull(&m_sceneRT, nullptr, XMFLOAT4(1, 1, 1, 1));		// 元の絵
		SetBlendMode(BLEND_ADD);
		float b = m_bloomStrength * intensity;
		DrawFull(&m_brightRT, nullptr, XMFLOAT4(b, b, b, 1));	// にじむ光を加算

		SetBlendMode(BLEND_ALPHA);
		ID3D11ShaderResourceView* pNull0 = nullptr;
		GetContext()->PSSetShaderResources(0, 1, &pNull0);
		return;
	}

	// ==== 6種デモモード(課題05) ====
	// 描画先を画面へ(深度は使わない)
	SetRenderTargets(1, &pScreen, nullptr);
	SetDepthTest(DEPTH_DISABLE);
	SetBlendMode(BLEND_NONE);
	SetSamplerState(SAMPLER_LINEAR);

	if (m_split)
	{
		// 画面を黒でクリアしてセル間に黒い区切りを見せる
		float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		pScreen->Clear(black);

		// 3列×2行で6種類を一括表示
		const float cw = 2.0f / 3.0f;	// セル幅(クリップ空間)
		const float ch = 2.0f / 2.0f;	// セル高さ
		const float gap = 0.014f;		// セル間の隙間(黒い枠)
		for (int i = 0; i < SPLIT_COUNT; ++i)
		{
			int c = i % 3;
			int r = i / 3;
			float ox = -1.0f + cw * (c + 0.5f);
			float oy =  1.0f - ch * (r + 0.5f);
			DrawEffect(i, ox, oy, cw - gap, ch - gap, intensity);
		}
	}
	else
	{
		// 全画面に1種類
		DrawEffect(m_current, 0.0f, 0.0f, 2.0f, 2.0f, intensity);
	}

	// 次フレームでRTとして使うため、SRVの割り当てを外しておく
	ID3D11ShaderResourceView* pNull = nullptr;
	GetContext()->PSSetShaderResources(0, 1, &pNull);
}

void PostProcess::DrawEffect(int effect, float ox, float oy, float sx, float sy, float intensity)
{
	// パラメータをGPUへ
	Param p = {};
	p.effect = effect;
	p.intensity = intensity;
	p.time = m_time;
	p.resolution[0] = (float)m_width;
	p.resolution[1] = (float)m_height;
	m_ppPS->WriteBuffer(0, &p);

	// 行列は単位行列(スクリーン空間で直接配置する)
	XMFLOAT4X4 ident;
	XMStoreFloat4x4(&ident, XMMatrixIdentity());
	Sprite::SetWorld(ident);
	Sprite::SetView(ident);
	Sprite::SetProjection(ident);

	// 全画面(または分割セル)にスプライトを配置して効果を掛ける
	Sprite::SetOffset(XMFLOAT2(ox, oy));
	Sprite::SetSize(XMFLOAT2(sx, sy));
	Sprite::SetColor(XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
	Sprite::SetTexture(&m_sceneRT);
	Sprite::SetPixelShader(m_ppPS.get());
	Sprite::Draw();
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

#include "SceneForge.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "CameraBase.h"
#include "Input.h"
#include "DebugUI.h"
#include "Defines.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

using namespace DirectX;

//--- パーティクル用シェーダー(炎と同じ考え方) ---
static const char* g_vsCode = R"EOT(
cbuffer Cam : register(b0){ float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.uv=v.uv; o.col=v.col; return o; }
)EOT";
static const char* g_psCode = R"EOT(
Texture2D tex:register(t0); SamplerState samp:register(s0);
struct PIN{ float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
float4 main(PIN i):SV_TARGET{ return tex.Sample(samp,i.uv) * i.col; }
)EOT";

static float frand()                 { return (float)rand() / (float)RAND_MAX; }
static float frand(float a, float b)  { return a + (b - a) * frand(); }

void SceneForge::Init()
{
	m_sparks.reserve(MAX_SPARKS);
	m_vtx.resize(MAX_SPARKS * 6);

	VertexShader* vs = CreateObj<VertexShader>("VS_Forge");
	vs->Compile(g_vsCode);
	PixelShader* ps = CreateObj<PixelShader>("PS_Forge");
	ps->Compile(g_psCode);

	// 光の粒テクスチャ(中心が明るい)
	const int S = 64;
	std::vector<unsigned char> pix(S * S * 4);
	for (int y = 0; y < S; ++y)
	for (int x = 0; x < S; ++x)
	{
		float dx = (x + 0.5f) / S * 2 - 1;
		float dy = (y + 0.5f) / S * 2 - 1;
		float f = 1.0f - sqrtf(dx * dx + dy * dy);
		if (f < 0) f = 0;
		f = f * f;
		unsigned char c = (unsigned char)(f * 255);
		int idx = (y * S + x) * 4;
		pix[idx] = pix[idx + 1] = pix[idx + 2] = pix[idx + 3] = c;
	}
	m_glow = std::make_shared<Texture>();
	m_glow->Create(DXGI_FORMAT_R8G8B8A8_UNORM, S, S, pix.data());

	MeshBuffer::Description desc = {};
	desc.pVtx = m_vtx.data();
	desc.vtxSize = sizeof(Vertex);
	desc.vtxCount = (UINT)m_vtx.size();
	desc.isWrite = true;
	desc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_mesh = std::make_shared<MeshBuffer>(desc);

	// 鉄条プロファイルを一様な太さで初期化
	for (int i = 0; i < SEG; ++i) m_th[i] = 1.0f;

	Strike();	// 開始直後から火花を出す
}

void SceneForge::Uninit()
{
	DestroyObj("VS_Forge");
	DestroyObj("PS_Forge");
	m_mesh.reset();
	m_glow.reset();
	m_sparks.clear();
}

void SceneForge::Strike()
{
	// 1回叩くと火花をまとめて発生(バースト)
	const int N = m_burst;
	XMFLOAT3 origin = XMFLOAT3(0.0f, 1.0f, 0.0f);	// 金床の位置(カメラ注視点の高さ)
	for (int i = 0; i < N; ++i)
	{
		if ((int)m_sparks.size() >= MAX_SPARKS) break;
		Spark s = {};
		s.pos = origin;
		float a = frand(0.0f, 6.2832f);		// 水平方向の角度
		float elev = frand(0.25f, 1.4f);	// 上向きの仰角
		float speed = frand(2.5f, 7.0f) * m_power;
		float h = cosf(elev) * speed;
		s.vel = XMFLOAT3(cosf(a) * h, sinf(elev) * speed + frand(1.0f, 3.0f), sinf(a) * h);
		s.maxLife = frand(0.5f, 1.1f);
		s.life = s.maxLife;
		s.size = frand(0.16f, 0.30f);
		m_sparks.push_back(s);
	}
}

//====================================================================
//  ゲーム進行
//====================================================================
void SceneForge::StartGame()
{
	m_state    = GAME_PLAY;
	m_progress = 0.0f;
	m_score    = 0;
	m_heat     = 0.0f;
	for (int i = 0; i < SEG; ++i) m_th[i] = 1.0f;	// 鉄条を初期状態に戻す
}

void SceneForge::FinishGame()
{
	m_state = GAME_RESULT;
}

//--- タイトル: 雰囲気で自動的に火花を出しつつ、SPACEで開始
void SceneForge::UpdateTitle(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE))
	{
		StartGame();
		return;		// 開始したフレームでは叩かない
	}
}

//--- 鍛造中
void SceneForge::UpdatePlay(float tick)
{
	// 加熱: F長押しで風箱を煽る / 常に自然冷却
	if (IsKeyPress('F')) m_heat += HEAT_RATE * tick;
	m_heat -= COOL_RATE * tick;
	if (m_heat < 0.0f) m_heat = 0.0f;
	if (m_heat > 1.0f) m_heat = 1.0f;

	// (段階2の暫定) Enterで結果画面へ。段階4で「目標形状の達成」に置き換える
	if (IsKeyTrigger(VK_RETURN)) FinishGame();
}

//--- 結果: SPACEでタイトルへ戻る
void SceneForge::UpdateResult(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE))
	{
		m_state = GAME_TITLE;
	}
}

void SceneForge::Update(float tick)
{
	m_time += tick;

	// タイトル中は雰囲気用に自動で火花を出す
	if (m_state == GAME_TITLE)
	{
		m_autoTimer += tick;
		if (m_autoTimer >= TITLE_INTERVAL) { Strike(); m_autoTimer = 0.0f; }
	}

	// 状態ごとの処理
	switch (m_state)
	{
	case GAME_TITLE:  UpdateTitle(tick);  break;
	case GAME_PLAY:   UpdatePlay(tick);   break;
	case GAME_RESULT: UpdateResult(tick); break;
	}

	// 火花シミュレーション(重力＋地面バウンド)はどの状態でも動かす
	for (size_t i = 0; i < m_sparks.size(); )
	{
		Spark& s = m_sparks[i];
		s.life -= tick;
		if (s.life <= 0.0f)
		{
			s = m_sparks.back();
			m_sparks.pop_back();
			continue;
		}
		s.vel.y -= GRAVITY * tick;				// 重力
		s.pos.x += s.vel.x * tick;
		s.pos.y += s.vel.y * tick;
		s.pos.z += s.vel.z * tick;
		if (s.pos.y < 0.0f && s.vel.y < 0.0f)	// 地面で弾む
		{
			s.pos.y = 0.0f;
			s.vel.y = -s.vel.y * 0.3f;
			s.vel.x *= 0.6f;
			s.vel.z *= 0.6f;
		}
		++i;
	}
}

//====================================================================
//  UI (HUD)
//====================================================================

// 画面中央にウィンドウ枠なしのメッセージを出す小道具
static void CenterText(const char* text, float yRatio, float scale = 1.0f,
                       ImU32 col = IM_COL32(255, 255, 255, 255))
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 sz = ImGui::CalcTextSize(text);
	sz.x *= scale; sz.y *= scale;
	float x = (SCREEN_WIDTH  - sz.x) * 0.5f;
	float y =  SCREEN_HEIGHT * yRatio - sz.y * 0.5f;
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(x, y), col, text);
}

//--- 温度(0..1)を鋼の色に変換する(暗赤→赤→橙→黄→白熱)
static ImU32 HeatColor(float h, float alpha = 1.0f)
{
	static const float stopH[] = { 0.00f, 0.20f, 0.40f, 0.55f, 0.70f, 0.85f, 1.00f };
	static const float stopR[] = { 0.15f, 0.45f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f };
	static const float stopG[] = { 0.05f, 0.06f, 0.15f, 0.45f, 0.65f, 0.88f, 1.00f };
	static const float stopB[] = { 0.05f, 0.02f, 0.02f, 0.05f, 0.15f, 0.45f, 0.92f };
	const int N = 7;
	if (h <= stopH[0])   h = stopH[0];
	if (h >= stopH[N-1]) h = stopH[N-1];
	int i = 0;
	while (i < N - 1 && h > stopH[i + 1]) ++i;
	float t = (h - stopH[i]) / (stopH[i + 1] - stopH[i]);
	float r = stopR[i] + (stopR[i + 1] - stopR[i]) * t;
	float g = stopG[i] + (stopG[i + 1] - stopG[i]) * t;
	float b = stopB[i] + (stopB[i + 1] - stopB[i]) * t;
	return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(alpha * 255));
}

//--- 2D側面図で光る鉄条(＋簡単な金床)を描く
void SceneForge::DrawBillet()
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();

	const float left     = SCREEN_WIDTH * 0.25f;
	const float right    = SCREEN_WIDTH * 0.75f;
	const float cy       = SCREEN_HEIGHT * 0.50f;
	const float baseHalf = 30.0f;	// 初期の半分の太さ(px)

	// --- 金床(鉄条の下の台) ---
	ImU32 anvilCol = IM_COL32(45, 45, 52, 255);
	dl->AddRectFilled(ImVec2(left - 40, cy + 34), ImVec2(right + 40, cy + 120), anvilCol, 6.0f);
	dl->AddRectFilled(ImVec2(left + 60, cy + 110), ImVec2(right - 60, cy + 200),
		IM_COL32(32, 32, 38, 255), 4.0f);

	// --- 鉄条本体 ---
	ImU32 col  = HeatColor(m_heat);
	ImU32 glow = HeatColor(m_heat, 0.35f);	// 外側のにじみ(発光感)

	auto nodeX = [&](int i) { return left + (right - left) * (i / (float)(SEG - 1)); };

	for (int i = 0; i < SEG - 1; ++i)
	{
		float x0 = nodeX(i),     x1 = nodeX(i + 1);
		float h0 = baseHalf * m_th[i], h1 = baseHalf * m_th[i + 1];

		// 発光(少し大きめ・薄く)
		dl->AddQuadFilled(
			ImVec2(x0, cy - h0 - 6), ImVec2(x1, cy - h1 - 6),
			ImVec2(x1, cy + h1 + 6), ImVec2(x0, cy + h0 + 6), glow);
		// 本体
		dl->AddQuadFilled(
			ImVec2(x0, cy - h0), ImVec2(x1, cy - h1),
			ImVec2(x1, cy + h1), ImVec2(x0, cy + h0), col);
	}
}

//--- 温度ゲージ(HUD)
void SceneForge::DrawHeatGauge()
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	const float x0 = SCREEN_WIDTH * 0.25f;
	const float x1 = SCREEN_WIDTH * 0.75f;
	const float y  = SCREEN_HEIGHT * 0.80f;
	const float hgt = 20.0f;
	auto lerpX = [&](float t) { return x0 + (x1 - x0) * t; };

	// トラック
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(30, 30, 34, 220), 4.0f);
	// 最適温度帯(緑の帯)
	dl->AddRectFilled(ImVec2(lerpX(IDEAL_MIN), y), ImVec2(lerpX(IDEAL_MAX), y + hgt),
		IM_COL32(60, 160, 70, 150));
	// 過熱帯(赤の帯)
	dl->AddRectFilled(ImVec2(lerpX(OVERHEAT), y), ImVec2(x1, y + hgt),
		IM_COL32(180, 40, 40, 160));
	// 現在温度の塗り
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(lerpX(m_heat), y + hgt), HeatColor(m_heat), 4.0f);
	// マーカー
	dl->AddLine(ImVec2(lerpX(m_heat), y - 5), ImVec2(lerpX(m_heat), y + hgt + 5),
		IM_COL32(255, 255, 255, 255), 2.0f);
	// 枠
	dl->AddRect(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(200, 200, 200, 120), 4.0f);

	// ラベルと状態
	dl->AddText(ImVec2(x0, y - 22), IM_COL32(230, 230, 230, 255), "TEMPERATURE");
	const char* st; ImU32 stc;
	if      (m_heat < IDEAL_MIN) { st = "COLD - heat it up! (hold F)"; stc = IM_COL32(120, 170, 255, 255); }
	else if (m_heat > OVERHEAT)  { st = "OVERHEAT!";                   stc = IM_COL32(255, 120, 120, 255); }
	else                          { st = "GOOD HEAT";                   stc = IM_COL32(150, 255, 150, 255); }
	dl->AddText(ImVec2(x1 - 220, y - 22), stc, st);
}

void SceneForge::DrawTitleUI()
{
	CenterText("- FORGE -",            0.34f, 3.0f, IM_COL32(255, 200, 120, 255));
	CenterText("Timing Blacksmith",    0.44f, 1.4f, IM_COL32(255, 235, 210, 255));
	CenterText("PRESS  SPACE  TO  START", 0.62f, 1.6f, IM_COL32(255, 255, 255, 255));
}

void SceneForge::DrawPlayUI()
{
	// 光る鉄条(背景レイヤー)
	DrawBillet();

	// 温度ゲージ
	DrawHeatGauge();

	// 操作ガイド(段階2: 加熱の確認まで)
	CenterText("Hold  F : Bellows (heat)      Enter : Finish (temp)", 0.93f, 1.0f,
		IM_COL32(255, 255, 255, 170));
}

void SceneForge::DrawResultUI()
{
	CenterText("FORGED!",              0.34f, 3.0f, IM_COL32(255, 220, 140, 255));
	char buf[64];
	sprintf_s(buf, sizeof(buf), "SCORE  %d", m_score);
	CenterText(buf,                    0.50f, 2.0f, IM_COL32(255, 255, 255, 255));
	CenterText("PRESS  SPACE  TO  RETURN", 0.68f, 1.4f, IM_COL32(255, 255, 255, 220));
}

void SceneForge::DrawUI()
{
	switch (m_state)
	{
	case GAME_TITLE:  DrawTitleUI();  break;
	case GAME_PLAY:   DrawPlayUI();   break;
	case GAME_RESULT: DrawResultUI(); break;
	}
}

void SceneForge::Draw()
{
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	VertexShader* vs = GetObj<VertexShader>("VS_Forge");
	PixelShader*  ps = GetObj<PixelShader>("PS_Forge");
	if (!pCamera || !vs || !ps || !m_mesh) return;

	XMFLOAT3 camPos = pCamera->GetPos();
	XMVECTOR vcam = XMLoadFloat3(&camPos);

	XMFLOAT4X4 cam[2];
	cam[0] = pCamera->GetView();
	cam[1] = pCamera->GetProj();
	vs->WriteBuffer(0, cam);

	// 速度方向に伸びたストリーク(火花の線)を作る
	int v = 0;
	for (const Spark& s : m_sparks)
	{
		float t = s.life / s.maxLife;			// 1→0

		// 色：白黄 → 橙 → 赤、消えるほど暗く
		XMFLOAT4 col;
		float br = t * t;
		if (t > 0.5f) col = XMFLOAT4(1.0f, 0.9f * br + 0.1f, 0.5f * br, 1.0f);
		else          col = XMFLOAT4(1.0f * br, 0.35f * br, 0.05f * br, 1.0f);

		XMVECTOR c = XMLoadFloat3(&s.pos);
		XMVECTOR vel = XMLoadFloat3(&s.vel);
		float speed = XMVectorGetX(XMVector3Length(vel));

		XMVECTOR dir = (speed > 0.001f) ? XMVector3Normalize(vel) : XMVectorSet(0, 1, 0, 0);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));	// カメラへ向く
		XMVECTOR side = XMVector3Cross(dir, toCam);
		if (XMVectorGetX(XMVector3Length(side)) < 0.001f) side = XMVectorSet(1, 0, 0, 0);
		side = XMVector3Normalize(side);

		float halfLen = s.size * (0.6f + speed * 0.12f);	// 速いほど長い線に
		float halfWid = s.size * 0.35f;
		XMVECTOR L = XMVectorScale(dir, halfLen);
		XMVECTOR W = XMVectorScale(side, halfWid);

		XMFLOAT3 tl, tr, bl, br3;
		XMStoreFloat3(&tl,  XMVectorSubtract(XMVectorAdd(c, L), W));
		XMStoreFloat3(&tr,  XMVectorAdd(XMVectorAdd(c, L), W));
		XMStoreFloat3(&bl,  XMVectorSubtract(XMVectorSubtract(c, L), W));
		XMStoreFloat3(&br3, XMVectorAdd(XMVectorSubtract(c, L), W));

		Vertex* q = &m_vtx[v];
		q[0] = { tl,  XMFLOAT2(0,0), col };
		q[1] = { tr,  XMFLOAT2(1,0), col };
		q[2] = { bl,  XMFLOAT2(0,1), col };
		q[3] = { bl,  XMFLOAT2(0,1), col };
		q[4] = { tr,  XMFLOAT2(1,0), col };
		q[5] = { br3, XMFLOAT2(1,1), col };
		v += 6;
		if (v + 6 > (int)m_vtx.size()) break;
	}

	if (v == 0) return;

	// 加算合成・深度書き込みなしで描画
	SetBlendMode(BLEND_ADD);
	SetDepthTest(DEPTH_ENABLE_TEST);
	ps->SetTexture(0, m_glow.get());
	m_mesh->Write(m_vtx.data());
	vs->Bind();
	ps->Bind();
	m_mesh->Draw(v);

	// 状態を戻す
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

#include "SceneForge.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "CameraBase.h"
#include "LightBase.h"
#include "Model.h"
#include "Geometory.h"
#include "Input.h"
#include "DebugUI.h"
#include "Defines.h"
#include "Audio.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

//--- 3D鉄条用シェーダー(頂点色をそのまま出す=発光する熱い金属) ---
static const char* g_barVS = R"EOT(
cbuffer Cam : register(b0){ float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float4 col:TEXCOORD1; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.col=v.col; return o; }
)EOT";
static const char* g_barPS = R"EOT(
struct PIN{ float4 pos:SV_POSITION; float4 col:TEXCOORD1; };
float4 main(PIN i):SV_TARGET{ return i.col; }
)EOT";

//--- 温度(0..1)を鋼の色(float4)に変換。冷たいときも暗い金属色で見える
static DirectX::XMFLOAT4 HeatRGB(float h, float dmg)
{
	static const float sH[] = { 0.00f, 0.20f, 0.40f, 0.55f, 0.70f, 0.85f, 1.00f };
	static const float sR[] = { 0.15f, 0.45f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f };
	static const float sG[] = { 0.05f, 0.06f, 0.15f, 0.45f, 0.65f, 0.88f, 1.00f };
	static const float sB[] = { 0.05f, 0.02f, 0.02f, 0.05f, 0.15f, 0.45f, 0.92f };
	const int N = 7;
	if (h < sH[0]) h = sH[0];
	if (h > sH[N - 1]) h = sH[N - 1];
	int i = 0; while (i < N - 1 && h > sH[i + 1]) ++i;
	float t = (h - sH[i]) / (sH[i + 1] - sH[i]);
	float r = sR[i] + (sR[i + 1] - sR[i]) * t;
	float g = sG[i] + (sG[i + 1] - sG[i]) * t;
	float b = sB[i] + (sB[i + 1] - sB[i]) * t;
	// 冷たくても暗い金属として見えるよう下限を設ける
	if (r < 0.22f) r = 0.22f;
	if (g < 0.20f) g = 0.20f;
	if (b < 0.20f) b = 0.20f;
	float d = 1.0f - 0.7f * dmg;	// 損傷で暗くなる
	return DirectX::XMFLOAT4(r * d, g * d, b * d, 1.0f);
}

//--- マウス位置をクライアント座標(ピクセル)で取得。ImGuiのタイミングに依存しない。
//    cw/ch はクライアント(=描画)の実サイズ。どのフレーム段階でも同じ値になる。
static void GetMouseClient(float& mx, float& my, float& cw, float& ch)
{
	POINT p; GetCursorPos(&p);
	HWND hwnd = GetActiveWindow();
	RECT rc = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	if (hwnd) { ScreenToClient(hwnd, &p); GetClientRect(hwnd, &rc); }
	mx = (float)p.x; my = (float)p.y;
	cw = (float)(rc.right - rc.left); ch = (float)(rc.bottom - rc.top);
	if (cw < 1.0f) cw = (float)SCREEN_WIDTH;
	if (ch < 1.0f) ch = (float)SCREEN_HEIGHT;
}

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

	// 鉄条プロファイルを一様な太さ・無傷で初期化
	for (int i = 0; i < SEG; ++i) { m_th[i] = 1.0f; m_dmg[i] = 0.0f; }
	BuildTarget();

	// --- 【3D化テスト】鍛冶素材モデルの読み込み ---
	VertexShader* mvs = CreateObj<VertexShader>("VS_ForgeObj");
	if (FAILED(mvs->Load("Assets/Shader/VS_Object.cso")))
		MessageBox(nullptr, "VS_Object.cso", "Shader Error", MB_OK);
	PixelShader* mps = CreateObj<PixelShader>("PS_ForgeObj");
	if (FAILED(mps->Load("Assets/Shader/PS_TexTint.cso")))
		MessageBox(nullptr, "PS_TexTint.cso", "Shader Error", MB_OK);

	// --- 3D鉄条メッシュ用シェーダーと動的メッシュ ---
	VertexShader* bvs = CreateObj<VertexShader>("VS_Bar");
	bvs->Compile(g_barVS);
	PixelShader* bps = CreateObj<PixelShader>("PS_Bar");
	bps->Compile(g_barPS);

	m_barVtx.resize(SEG * 48 + 24);	// 両面描画分の頂点を確保
	MeshBuffer::Description bdesc = {};
	bdesc.pVtx     = m_barVtx.data();
	bdesc.vtxSize  = sizeof(Vertex);
	bdesc.vtxCount = (UINT)m_barVtx.size();
	bdesc.isWrite  = true;
	bdesc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_barMesh = std::make_shared<MeshBuffer>(bdesc);

	Model* anvil = CreateObj<Model>("MdlAnvil");
	anvil->Load("Assets/MM_Blacksmith_Pack/Anvil/SM_Anvil.fbx", 1.0f, false, true);
	// FBXがテクスチャを持たないので、BaseColorを手動で割り当てる
	{
		auto tex = std::make_shared<Texture>();
		if (SUCCEEDED(tex->Create("Assets/MM_Blacksmith_Pack/Anvil/Textures/T_Anvil_BaseColor.png")))
			anvil->SetTexture(tex);
	}
	// 金床の境界箱を一度だけ計算してキャッシュ(砧面の高さ算出に使う)
	anvil->GetLocalAABB(m_anvilMin, m_anvilMax);

	// 3Dハンマー
	Model* hammer = CreateObj<Model>("MdlHammer");
	hammer->Load("Assets/MM_Blacksmith_Pack/Tools/SM_BS_Hammer_1.fbx", 1.0f, false, true);
	{
		auto tex = std::make_shared<Texture>();
		if (SUCCEEDED(tex->Create("Assets/MM_Blacksmith_Pack/Tools/Textures/1024x512/T_BS_Tools_BaseColor.png")))
			hammer->SetTexture(tex);
	}

	Strike();	// 開始直後から火花を出す
}

void SceneForge::Uninit()
{
	DestroyObj("VS_Forge");
	DestroyObj("PS_Forge");
	DestroyObj("VS_ForgeObj");
	DestroyObj("PS_ForgeObj");
	DestroyObj("VS_Bar");
	DestroyObj("PS_Bar");
	DestroyObj("MdlAnvil");
	DestroyObj("MdlHammer");
	m_barMesh.reset();
	m_mesh.reset();
	m_glow.reset();
	m_sparks.clear();
	if (!m_cursorShown) { ShowCursor(TRUE); m_cursorShown = true; }	// カーソルを戻す
}

void SceneForge::Strike(float scale)
{
	// 1回叩くと火花をまとめて発生(バースト)。scaleで量と勢いを変える
	const int N = (int)(m_burst * scale);
	XMFLOAT3 origin = XMFLOAT3(0.0f, 1.0f, 0.0f);	// 金床の位置(カメラ注視点の高さ)
	for (int i = 0; i < N; ++i)
	{
		if ((int)m_sparks.size() >= MAX_SPARKS) break;
		Spark s = {};
		s.pos = origin;
		float a = frand(0.0f, 6.2832f);		// 水平方向の角度
		float elev = frand(0.25f, 1.4f);	// 上向きの仰角
		float speed = frand(2.5f, 7.0f) * m_power * scale;
		float h = cosf(elev) * speed;
		s.vel = XMFLOAT3(cosf(a) * h, sinf(elev) * speed + frand(1.0f, 3.0f), sinf(a) * h);
		s.maxLife = frand(0.5f, 1.1f);
		s.life = s.maxLife;
		s.size = frand(0.16f, 0.30f);
		m_sparks.push_back(s);
	}
}

//====================================================================
//  目標形状(剣のシルエット)
//====================================================================
void SceneForge::BuildTarget()
{
	// 左=柄側(タング), 右=切先。鉄条(一様1.0)から削って剣形に近づける
	for (int i = 0; i < SEG; ++i)
	{
		float u = i / (float)(SEG - 1);	// 0..1
		float th;
		if (u < 0.20f)
			th = 0.50f;					// タング(柄)は細め
		else
		{
			float v = (u - 0.20f) / 0.80f;	// 0..1(刃の付け根→切先)
			th = 0.85f + (0.08f - 0.85f) * v;	// 付け根0.85 → 切先0.08 へテーパー
		}
		m_target[i] = th;
	}
}

//--- 現在の形状と目標の一致度(0..1)。平均誤差が小さいほど高い
float SceneForge::ShapeMatch() const
{
	float err = 0.0f;
	for (int i = 0; i < SEG; ++i) err += fabsf(m_th[i] - m_target[i]);
	err /= SEG;
	float m = 1.0f - err / 0.40f;	// 平均誤差0.40で0%
	if (m < 0.0f) m = 0.0f;
	if (m > 1.0f) m = 1.0f;
	return m;
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
	for (int i = 0; i < SEG; ++i) { m_th[i] = 1.0f; m_dmg[i] = 0.0f; }	// 鉄条を初期状態に戻す
	BuildTarget();
	m_match = 0.0f;

	m_charging    = false;
	m_charge      = 0.0f;
	m_strikeCD    = 0.0f;
	m_strikeAnim  = 0.0f;
	m_hammerLift  = HAMMER_REST_LIFT;
	m_strikeSeg   = SEG / 2;
	m_canStrike   = false;		// SPACEを一度離すまで蓄力しない
	m_shake        = 0.0f;
	m_popupLife    = 0.0f;
	m_sinceStrike  = 999.0f;	// 最初の一打はリズム対象外
	m_rhythmStreak = 0;
	m_sizzleTimer  = 0.0f;
	m_qualitySum   = 0.0f;
	m_strikeCount  = 0;
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
	// F1(デバッグUI)を開いている間はゲーム入力を凍結する。閉じていれば通常通り。
	bool inputOn = !DebugUI::IsVisible();

	// --- 加熱: R長押しで炉で加熱 / 常にゆっくり自然冷却 ---
	if (inputOn)
	{
		if (IsKeyPress('R')) m_heat += HEAT_RATE * tick;
		m_heat -= m_coolRate * tick;
		if (m_heat < 0.0f) m_heat = 0.0f;
		if (m_heat > 1.0f) m_heat = 1.0f;
	}

	// --- 過熱で放置すると鋼全体が焼けていく(損傷が蓄積)＋ジュー音 ---
	if (m_heat > OVERHEAT)
	{
		for (int i = 0; i < SEG; ++i)
		{
			m_dmg[i] += BURN_RATE * tick;
			if (m_dmg[i] > 1.0f) m_dmg[i] = 1.0f;
		}
		m_sizzleTimer -= tick;
		if (m_sizzleTimer <= 0.0f) { Audio::Play(Audio::SE_SIZZLE); m_sizzleTimer = 0.22f; }
	}
	else m_sizzleTimer = 0.0f;

	// --- 打撃テンポの計測(前回打撃からの経過時間) ---
	m_sinceStrike += tick;
	// 長く止まっていたらリズムはリセット(遅すぎ)
	if (m_sinceStrike > CADENCE_MAX) m_rhythmStreak = 0;

	// --- 照準(シンプル方式): マウスの画面上の縦位置を鉄条のセグメントに割り当てる ---
	// 3D投影を使わないので絶対にズレない。鉄条が画面に映る縦範囲(上端/下端の割合)に対応させる。
	if (inputOn)
	{
		float mx, my, cw, ch; GetMouseClient(mx, my, cw, ch);
		float t = (my / ch - m_aimTop) / (m_aimBottom - m_aimTop);	// 鉄条範囲内で0..1
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		m_strikeSeg = (int)((1.0f - t) * (SEG - 1) + 0.5f);	// 画面上=切先(奥), 下=柄(手前)
	}

	// --- 蓄力ハンマー: 左クリック押しっぱなしで蓄力、離すと打撃。打撃後はクールダウン ---
	if (m_strikeCD > 0.0f) m_strikeCD -= tick;	// クールダウン消化

	if (!inputOn)
	{
		// F1操作中は蓄力をキャンセル(暴発しないように)
		m_charging = false;
		m_charge   = 0.0f;
	}
	// 開始直後の誤爆防止(一度ボタンを離すまで蓄力しない)
	else if (!m_canStrike)
	{
		if (!IsKeyPress(VK_LBUTTON)) m_canStrike = true;
	}
	else if (m_strikeCD <= 0.0f && IsKeyPress(VK_LBUTTON))
	{
		m_charging = true;
		m_charge  += CHARGE_RATE * tick;
		if (m_charge > 1.0f) m_charge = 1.0f;
	}
	else if (m_charging)
	{
		DoStrike();
		m_charging = false;
		m_charge   = 0.0f;
		m_strikeCD = m_strikeCDMax;	// 腕を戻す時間=すぐには次を打てない
	}

	// --- 目標形状との一致度を更新 ---
	m_match = ShapeMatch();

	// --- ハンマーの上下: 蓄力で上がる / 打撃で振り下ろして戻る ---
	if (m_strikeAnim > 0.0f)
	{
		m_strikeAnim -= tick / STRIKE_ANIM_TIME;
		if (m_strikeAnim < 0.0f) m_strikeAnim = 0.0f;
		m_hammerLift = HAMMER_REST_LIFT * (1.0f - m_strikeAnim);	// 1(接触=低い)→0(元の高さ)
	}
	else if (m_charging)
	{
		m_hammerLift = HAMMER_REST_LIFT + m_charge * HAMMER_CHARGE_RAISE;	// 蓄力で持ち上がる
	}
	else
	{
		float k = tick * 8.0f; if (k > 1.0f) k = 1.0f;
		m_hammerLift += (HAMMER_REST_LIFT - m_hammerLift) * k;	// 待機高さへ緩やかに戻る
	}

	// --- フィードバックの減衰 ---
	if (m_shake > 0.0f)     { m_shake -= tick * 3.0f; if (m_shake < 0.0f) m_shake = 0.0f; }
	if (m_popupLife > 0.0f) m_popupLife -= tick;

	// --- 淬火(仕上げ): Qでいつでも完成にできる。一致度と損傷で品質が決まる ---
	if (inputOn && IsKeyTrigger('Q')) FinishGame();
}

//--- 蓄力を解放して1打: 変形＋フィードバック
void SceneForge::DoStrike()
{
	float power = m_charge;			// 0..1
	bool cold = (m_heat < COLD_LIMIT);
	bool over = (m_heat > OVERHEAT);

	// --- リズム判定: 前回打撃からの間隔が「速すぎず遅すぎず」なら良いテンポ ---
	float interval = m_sinceStrike;
	m_sinceStrike = 0.0f;
	bool goodTempo = (interval >= CADENCE_MIN && interval <= CADENCE_MAX) && !cold;
	if (goodTempo) ++m_rhythmStreak;
	else           m_rhythmStreak = 0;
	bool inGroove = (m_rhythmStreak >= GROOVE_HITS);	// テンポが乗ると効率アップ
	float grooveMult = inGroove ? 1.3f : 1.0f;			// 変形効率の上昇

	// 温度係数(冷たい→ほぼ効かない, 過熱→効くが品質悪, 適温→最大)
	float heatFactor = cold ? 0.10f : (over ? 0.7f : 1.0f);
	float deform = DEFORM_MAX * power * heatFactor * grooveMult;

	// 打撃位置を中心に鉄を薄くする(近傍にもなだらかに)。
	// 冷打=割れ / 過熱打=焼け として、その場に損傷を刻む
	float dmgAdd = cold ? 0.35f : (over ? 0.25f : 0.0f);
	for (int i = 0; i < SEG; ++i)
	{
		int d = abs(i - m_strikeSeg);
		if (d > 3) continue;
		float falloff = 1.0f - d / 4.0f;
		m_th[i] -= deform * falloff;
		if (m_th[i] < 0.05f) m_th[i] = 0.05f;
		if (dmgAdd > 0.0f)
		{
			m_dmg[i] += dmgAdd * falloff;
			if (m_dmg[i] > 1.0f) m_dmg[i] = 1.0f;
		}
	}

	// 温度が下がる / 火花 / 振動
	m_heat -= STRIKE_COOL;
	if (m_heat < 0.0f) m_heat = 0.0f;
	float sparkScale = (0.4f + power * 1.2f) * (over ? 0.7f : 1.0f);
	if (!cold) Strike(sparkScale);

	// 打撃音: 冷打は鈍い音、それ以外は力量に応じた「カン」
	if (cold) Audio::Play(Audio::SE_COLD, 0.9f);
	else      Audio::Play(Audio::SE_HAMMER, 0.35f + power * 0.65f);
	// 冷打は「ガツン」と大きく揺れる(手応えが悪い=衝撃だけ大きい)
	m_shake = cold ? (0.6f + power * 0.6f) : (0.3f + power * 0.7f);
	m_strikeAnim = 1.0f;	// ハンマーを振り下ろすアニメ開始

	// 評価ラベル & スコア
	const char* label; unsigned int col; float quality;
	if      (cold)          { label = "CRACK!";   col = IM_COL32(120, 170, 255, 255); quality = 0.0f; }
	else if (over)          { label = "BURNT!";   col = IM_COL32(255, 120, 120, 255); quality = 0.1f; }
	else if (power > 0.85f) { label = "PERFECT!"; col = IM_COL32(255, 220, 120, 255); quality = 1.0f; }
	else if (power > 0.50f) { label = "GOOD";     col = IM_COL32(180, 255, 150, 255); quality = 0.7f; }
	else                    { label = "WEAK";     col = IM_COL32(200, 200, 200, 255); quality = 0.4f; }

	// 良いテンポが乗っているときは効率・品質アップ＋主人公が口笛を吹く
	if (inGroove)
	{
		quality += 0.2f;
		Audio::Play(Audio::SE_WHISTLE, 0.5f);
	}

	m_qualitySum += quality;
	m_strikeCount++;
	m_score += (int)(quality * 100);

	// ポップアップ表示(ノリに乗っている間は印を付ける)
	if (inGroove) sprintf_s(m_popupText, sizeof(m_popupText), "%s  (in rhythm)", label);
	else          strcpy_s(m_popupText, sizeof(m_popupText), label);
	m_popupLife = 0.8f;
	m_popupCol  = col;
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
	ApplyCamera();		// SceneRootのDCCが動かしたカメラを先に固定
	UpdateBarAnchor();	// 金床の砧面の高さに鉄条を自動配置

	// PLAY中はOSカーソルを隠す(照準は光るセグメントで示す)。デバッグUI表示中は出す
	bool wantCursor = (m_state != GAME_PLAY) || DebugUI::IsVisible();
	if (wantCursor != m_cursorShown) { ShowCursor(wantCursor); m_cursorShown = wantCursor; }

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
	ImVec2 disp = ImGui::GetIO().DisplaySize;	// 実際の画面サイズ(解像度非依存)
	ImVec2 sz = ImGui::CalcTextSize(text);
	sz.x *= scale; sz.y *= scale;
	float x = (disp.x - sz.x) * 0.5f;
	float y =  disp.y * yRatio - sz.y * 0.5f;
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

	// 打撃時の揺れ(鉄条だけを揺らして衝撃感を出す)
	float sx = 0.0f, sy = 0.0f;
	if (m_shake > 0.0f)
	{
		sx = frand(-1.0f, 1.0f) * m_shake * 8.0f;
		sy = frand(-1.0f, 1.0f) * m_shake * 8.0f;
	}
	const float cyb = cy + sy;

	auto nodeX = [&](int i) { return left + (right - left) * (i / (float)(SEG - 1)) + sx; };

	for (int i = 0; i < SEG - 1; ++i)
	{
		float x0 = nodeX(i),     x1 = nodeX(i + 1);
		float h0 = baseHalf * m_th[i], h1 = baseHalf * m_th[i + 1];

		// 発光(少し大きめ・薄く)
		dl->AddQuadFilled(
			ImVec2(x0, cyb - h0 - 6), ImVec2(x1, cyb - h1 - 6),
			ImVec2(x1, cyb + h1 + 6), ImVec2(x0, cyb + h0 + 6), glow);
		// 本体
		dl->AddQuadFilled(
			ImVec2(x0, cyb - h0), ImVec2(x1, cyb - h1),
			ImVec2(x1, cyb + h1), ImVec2(x0, cyb + h0), col);

		// 損傷(冷打の割れ / 過熱の焼け)を黒い染みで表現
		float dmg = (m_dmg[i] > m_dmg[i + 1]) ? m_dmg[i] : m_dmg[i + 1];
		if (dmg > 0.01f)
		{
			ImU32 dc = IM_COL32(15, 8, 5, (int)(dmg * 210));
			dl->AddQuadFilled(
				ImVec2(x0, cyb - h0), ImVec2(x1, cyb - h1),
				ImVec2(x1, cyb + h1), ImVec2(x0, cyb + h0), dc);
		}
	}

	// 目標形状(剣)のゴースト輪郭。これに近づくように削る
	ImU32 tcol = IM_COL32(120, 220, 255, 150);
	for (int i = 0; i < SEG - 1; ++i)
	{
		float tx0 = left + (right - left) * (i / (float)(SEG - 1));
		float tx1 = left + (right - left) * ((i + 1) / (float)(SEG - 1));
		float t0 = baseHalf * m_target[i], t1 = baseHalf * m_target[i + 1];
		dl->AddLine(ImVec2(tx0, cy - t0), ImVec2(tx1, cy - t1), tcol, 2.0f);
		dl->AddLine(ImVec2(tx0, cy + t0), ImVec2(tx1, cy + t1), tcol, 2.0f);
	}

	// 過熱中は赤い警告枠を点滅させる(鋼が焼けている合図)
	if (m_heat > OVERHEAT)
	{
		float p = 0.5f + 0.5f * sinf(m_time * 12.0f);
		ImU32 wc = IM_COL32(255, 60, 40, (int)(110 + p * 130));
		dl->AddRect(ImVec2(left - 50, cyb - 74), ImVec2(right + 50, cyb + 74), wc, 6.0f, 0, 4.0f);
	}
}

//--- 温度ゲージ(HUD)
void SceneForge::DrawHeatGauge()
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	const float x0 = disp.x * 0.25f;
	const float x1 = disp.x * 0.75f;
	const float y  = disp.y * 0.80f;
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
	if      (m_heat < IDEAL_MIN) { st = "COLD - heat it up! (hold R)"; stc = IM_COL32(120, 170, 255, 255); }
	else if (m_heat > OVERHEAT)  { st = "OVERHEAT!";                   stc = IM_COL32(255, 120, 120, 255); }
	else                          { st = "GOOD HEAT";                   stc = IM_COL32(150, 255, 150, 255); }
	dl->AddText(ImVec2(x1 - 220, y - 22), stc, st);
}

//--- ハンマー・打撃カーソル・蓄力メーター・拍
void SceneForge::DrawHammer()
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	const float left     = SCREEN_WIDTH * 0.25f;
	const float right    = SCREEN_WIDTH * 0.75f;
	const float cy       = SCREEN_HEIGHT * 0.50f;
	const float baseHalf = 30.0f;

	float x    = left + (right - left) * (m_strikeSeg / (float)(SEG - 1));
	float topY = cy - baseHalf * m_th[m_strikeSeg];

	// 打撃位置マーカー(下向き三角)
	float my = topY - 14;
	dl->AddTriangleFilled(ImVec2(x - 8, my - 14), ImVec2(x + 8, my - 14), ImVec2(x, my),
		IM_COL32(255, 240, 180, 230));

	// ハンマー(蓄力するほど高く持ち上がる)
	float raise = 34.0f + m_charge * 70.0f;
	float hy    = topY - raise;
	dl->AddLine(ImVec2(x, hy), ImVec2(x, topY - 8), IM_COL32(120, 90, 60, 255), 4.0f);	// 柄
	dl->AddRectFilled(ImVec2(x - 24, hy - 16), ImVec2(x + 24, hy + 8),
		IM_COL32(95, 95, 105, 255), 3.0f);	// ハンマー頭

	// 蓄力メーター
	if (m_charging)
	{
		float bw = 70.0f, bx = x - bw * 0.5f, by = cy - baseHalf - 120.0f;
		dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 9), IM_COL32(40, 40, 40, 220), 2.0f);
		ImU32 cc = (m_charge > 0.85f) ? IM_COL32(255, 220, 120, 255) : IM_COL32(255, 180, 70, 255);
		dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * m_charge, by + 9), cc, 2.0f);
	}
	// 拍(リズム)は視覚では出さず、音で伝える予定(段階: 音響)
}

void SceneForge::DrawTitleUI()
{
	CenterText("- FORGE -",            0.34f, 3.0f, IM_COL32(255, 200, 120, 255));
	CenterText("Timing Blacksmith",    0.44f, 1.4f, IM_COL32(255, 235, 210, 255));
	CenterText("PRESS  SPACE  TO  START", 0.62f, 1.6f, IM_COL32(255, 255, 255, 255));
}

void SceneForge::DrawPlayUI()
{
	// 鉄条とハンマーは3Dで描画するので、2Dの鉄条(DrawBillet/DrawHammer)は使わない

	// 温度ゲージ
	DrawHeatGauge();

	// 照準は鉄条の光るセグメントで示す(クロスヘアは使わない)

	// 過熱の警告(点滅)
	if (m_heat > OVERHEAT)
	{
		float p = 0.5f + 0.5f * sinf(m_time * 12.0f);
		CenterText("!!  OVERHEAT  !!", 0.20f, 1.6f, IM_COL32(255, 70, 50, (int)(150 + p * 105)));
	}

	// 打撃フィードバックのポップアップ(鉄条の上でフェード)
	if (m_popupLife > 0.0f)
	{
		float a = m_popupLife / 0.8f;
		if (a > 1.0f) a = 1.0f;
		unsigned int c = (m_popupCol & 0x00FFFFFF) | ((unsigned int)(a * 255) << 24);
		CenterText(m_popupText, 0.36f, 2.0f, c);
	}

	// スコアと形状一致度(左上)
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	char sb[48];
	sprintf_s(sb, sizeof(sb), "SCORE  %d", m_score);
	dl->AddText(ImVec2(40, 40), IM_COL32(255, 235, 200, 255), sb);
	sprintf_s(sb, sizeof(sb), "SHAPE MATCH  %d%%", (int)(m_match * 100));
	dl->AddText(ImVec2(40, 60), IM_COL32(150, 220, 255, 255), sb);

	// 一致度バー(上部中央)
	{
		ImVec2 disp = ImGui::GetIO().DisplaySize;
		float bx0 = disp.x * 0.30f, bx1 = disp.x * 0.70f, by = 30.0f, bh = 14.0f;
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(30, 30, 34, 220), 3.0f);
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * m_match, by + bh),
			IM_COL32(90, 200, 255, 255), 3.0f);
		dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(200, 200, 200, 120), 3.0f);
	}

	// 淬火の準備ができたら促す
	if (m_match >= 0.85f)
		CenterText("Shape looks good!  Press  Q  to Quench", 0.86f, 1.2f,
			IM_COL32(150, 255, 180, 230));

	// 操作ガイド
	CenterText("Mouse : Aim    Hold L-MOUSE : Hammer    Hold R : Heat    Q : Quench",
		0.93f, 1.0f, IM_COL32(255, 255, 255, 170));
}

void SceneForge::DrawResultUI()
{
	CenterText("FORGED!",              0.30f, 3.0f, IM_COL32(255, 220, 140, 255));
	char buf[64];
	sprintf_s(buf, sizeof(buf), "SHAPE MATCH  %d%%", (int)(m_match * 100));
	CenterText(buf,                    0.46f, 2.0f, IM_COL32(150, 220, 255, 255));
	sprintf_s(buf, sizeof(buf), "SCORE  %d", m_score);
	CenterText(buf,                    0.56f, 2.0f, IM_COL32(255, 255, 255, 255));
	CenterText("PRESS  SPACE  TO  RETURN", 0.70f, 1.4f, IM_COL32(255, 255, 255, 220));
}

void SceneForge::DrawUI()
{
	// 【3D化テスト】モデル配置調整パネル(F1でデバッグUIを出したとき)
	if (DebugUI::IsVisible())
	{
		ImGui::SetNextWindowPos(ImVec2(12, 360), ImGuiCond_FirstUseEver);
		ImGui::Begin("3D Model Test");
		Model* a = GetObj<Model>("MdlAnvil");
		ImGui::Text("Anvil loaded: %s", a ? "YES" : "NO");
		ImGui::Checkbox("Show 3D", &m_show3D);
		ImGui::Text("-- Anvil --");
		ImGui::DragFloat("Scale", &m_mScale, 0.001f, 0.0001f, 10.0f, "%.4f");
		ImGui::DragFloat3("Pos", m_mPos, 0.05f);
		ImGui::SliderFloat("Yaw", &m_mYaw, -3.1416f, 3.1416f);
		ImGui::Text("-- Bar (Y is auto: sits on anvil) --");
		ImGui::Text("Bar Y (auto) = %.2f", m_barY);
		ImGui::DragFloat("Surface Off", &m_barLift, 0.01f, -1.0f, 1.0f, "%.2f");
		ImGui::DragFloat("Bar Len",   &m_barLen,   0.05f,  0.2f, 6.0f, "%.2f");
		ImGui::DragFloat("Bar Thick", &m_barThick, 0.01f,  0.02f, 1.0f, "%.3f");
		ImGui::DragFloat("Bar Width", &m_barWidth, 0.01f,  0.02f, 1.0f, "%.3f");
		ImGui::Text("-- Hammer --");
		ImGui::DragFloat ("Hmr Scale", &m_hammerScale, 0.001f, 0.0001f, 1.0f, "%.4f");
		ImGui::DragFloat3("Hmr Rot",   m_hammerRot,    0.02f);
		ImGui::DragFloat3("Hmr Off",   m_hammerOff,    0.02f);
		ImGui::Text("-- Camera --");
		ImGui::DragFloat3("Cam Pos",  m_camPos,  0.05f);
		ImGui::DragFloat3("Cam Look", m_camLook, 0.05f);
		ImGui::SliderFloat("Cam Sway", &m_camSway, 0.0f, 1.0f, "%.2f");
		ImGui::Text("-- Tuning --");
		ImGui::SliderFloat("Strike CD (s)", &m_strikeCDMax, 0.1f, 3.0f, "%.2f");
		ImGui::SliderFloat("Cool Rate /s",  &m_coolRate,    0.0f, 0.3f, "%.3f");
		ImGui::SliderFloat("Aim Top",       &m_aimTop,    0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Aim Bottom",    &m_aimBottom, 0.0f, 1.0f, "%.2f");
		ImGui::End();
	}

	switch (m_state)
	{
	case GAME_TITLE:  DrawTitleUI();  break;
	case GAME_PLAY:   DrawPlayUI();   break;
	case GAME_RESULT: DrawResultUI(); break;
	}
}

//--- ゲーム用の固定カメラを毎フレーム適用(ドラッグで動かされても上書きして固定する)
void SceneForge::ApplyCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;

	// マウス位置に応じて注視点をわずかにずらし、視点を軽く揺らす
	// (一人称の"生きてる"感。カーソルはロックせず自由なまま)
	float nx = 0.0f, ny = 0.0f;
	if (!DebugUI::IsVisible())	// F1(デバッグUI)を開いている間は視点を揺らさない
	{
		float mx, my, cw, ch; GetMouseClient(mx, my, cw, ch);
		nx = (mx / cw - 0.5f) * 2.0f;	// -1(左) .. +1(右)
		ny = (my / ch - 0.5f) * 2.0f;	// -1(上) .. +1(下)
	}
	XMFLOAT3 look = {
		m_camLook[0] + nx * m_camSway,
		m_camLook[1] - ny * m_camSway * 0.7f,
		m_camLook[2],
	};
	cam->SetPos (XMFLOAT3(m_camPos[0], m_camPos[1], m_camPos[2]));
	cam->SetLook(look);
	cam->SetUp  (XMFLOAT3(0.0f, 1.0f, 0.0f));
}

//--- 金床のAABBを現在のワールド変換で評価し、砧面(上面)の高さに鉄条を乗せる
//    アンカー方式: 金床のスケール/位置/回転を変えても鉄条が自動で追従する。
//    別の金床モデルに差し替えてもAABBが変わるだけで再調整不要。
void SceneForge::UpdateBarAnchor()
{
	Model* anvil = GetObj<Model>("MdlAnvil");
	if (!anvil) return;

	// DrawModelsTest と同じワールド行列を作る
	XMMATRIX world =
		XMMatrixScaling(m_mScale, m_mScale, m_mScale) *
		XMMatrixRotationY(m_mYaw) *
		XMMatrixTranslation(m_mPos[0], m_mPos[1], m_mPos[2]);
	world = anvil->GetScaleBaseMatrix() * world;

	// AABBの8隅をワールドへ変換し、一番高いY(砧面)と、X/Zの中心を求める
	float topY = -1e18f;
	float minX = 1e18f, maxX = -1e18f, minZ = 1e18f, maxZ = -1e18f;
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 p(
			(i & 1) ? m_anvilMax.x : m_anvilMin.x,
			(i & 2) ? m_anvilMax.y : m_anvilMin.y,
			(i & 4) ? m_anvilMax.z : m_anvilMin.z);
		XMVECTOR wv = XMVector3TransformCoord(XMLoadFloat3(&p), world);
		float x = XMVectorGetX(wv), y = XMVectorGetY(wv), z = XMVectorGetZ(wv);
		if (y > topY) topY = y;
		if (x < minX) minX = x; if (x > maxX) maxX = x;
		if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
	}
	// 砧面中心(X/Z)＋上面の高さ。鉄条の下面がここに接するよう厚み分持ち上げる
	m_barAnchor.x = (minX + maxX) * 0.5f;
	m_barAnchor.z = (minZ + maxZ) * 0.5f;
	m_barAnchor.y = topY + m_barThick + m_barLift;
	m_barY = m_barAnchor.y;	// F1表示用
}

//--- デバッグ: 8隅の点から箱の12辺を線で描く
static void DrawBoxEdges(const XMFLOAT3 c[8])
{
	// ビット: 1=x, 2=y, 4=z。1ビットだけ違う隅同士が辺
	for (int i = 0; i < 8; ++i)
		for (int b = 1; b <= 4; b <<= 1)
			if (!(i & b))
				Geometory::AddLine(c[i], c[i | b]);
}

//--- デバッグ: 金床AABBと鉄条の箱を線で可視化(F1中のみ)
void SceneForge::DrawDebugBoxes()
{
	CameraBase* cam   = GetObj<CameraBase>("Camera");
	Model*      anvil = GetObj<Model>("MdlAnvil");
	if (!cam || !anvil) return;

	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());

	// 金床のワールドAABB(緑)
	XMMATRIX world =
		XMMatrixScaling(m_mScale, m_mScale, m_mScale) *
		XMMatrixRotationY(m_mYaw) *
		XMMatrixTranslation(m_mPos[0], m_mPos[1], m_mPos[2]);
	world = anvil->GetScaleBaseMatrix() * world;
	XMFLOAT3 ac[8];
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 p(
			(i & 1) ? m_anvilMax.x : m_anvilMin.x,
			(i & 2) ? m_anvilMax.y : m_anvilMin.y,
			(i & 4) ? m_anvilMax.z : m_anvilMin.z);
		XMStoreFloat3(&ac[i], XMVector3TransformCoord(XMLoadFloat3(&p), world));
	}
	Geometory::SetColor(XMFLOAT4(0.2f, 1.0f, 0.3f, 1.0f));
	DrawBoxEdges(ac);

	// 鉄条の箱(黄) + アンカー点
	float hl = m_barLen * 0.5f, w = m_barWidth, th = m_barThick;
	float ax = m_barAnchor.x, ay = m_barAnchor.y, az = m_barAnchor.z;
	XMFLOAT3 bc[8] = {
		{ ax - w, ay - th, az - hl }, { ax + w, ay - th, az - hl },
		{ ax - w, ay + th, az - hl }, { ax + w, ay + th, az - hl },
		{ ax - w, ay - th, az + hl }, { ax + w, ay - th, az + hl },
		{ ax - w, ay + th, az + hl }, { ax + w, ay + th, az + hl },
	};
	Geometory::SetColor(XMFLOAT4(1.0f, 0.9f, 0.2f, 1.0f));
	DrawBoxEdges(bc);

	Geometory::DrawLines();
}

//--- 共通のモデル描画(ワールド行列を渡すだけ)
void SceneForge::DrawModelWorld(Model* m, const XMMATRIX& world)
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_ForgeObj");
	PixelShader*  ps  = GetObj<PixelShader>("PS_ForgeObj");
	if (!m || !cam || !vs || !ps) return;

	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView();
	mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	XMFLOAT4 color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);	// テクスチャそのまま
	vs->WriteBuffer(0, mat);
	ps->WriteBuffer(0, &color);

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	m->SetVertexShader(vs);
	m->SetPixelShader(ps);
	m->Draw();
}

//--- 鍛冶素材の3Dモデルを描画(金床＋ハンマー)
void SceneForge::DrawModelsTest()
{
	if (!m_show3D) return;
	Model* anvil = GetObj<Model>("MdlAnvil");
	if (anvil)
	{
		XMMATRIX world =
			XMMatrixScaling(m_mScale, m_mScale, m_mScale) *
			XMMatrixRotationY(m_mYaw) *
			XMMatrixTranslation(m_mPos[0], m_mPos[1], m_mPos[2]);
		world = anvil->GetScaleBaseMatrix() * world;
		DrawModelWorld(anvil, world);
	}
	DrawHammer3D();
}

//--- 3Dハンマー: 打撃位置の真上に置き、蓄力で上がり打撃で振り下ろす
void SceneForge::DrawHammer3D()
{
	Model* hammer = GetObj<Model>("MdlHammer");
	if (!hammer) return;

	// 打撃セグメントの世界位置
	int seg = m_strikeSeg;
	float zc = m_barAnchor.z - m_barLen * 0.5f + m_barLen * (seg / (float)(SEG - 1));
	float barTop = m_barAnchor.y + m_barThick * m_th[seg];
	XMFLOAT3 pos = {
		m_barAnchor.x + m_hammerOff[0],
		barTop + m_hammerLift + m_hammerOff[1],
		zc + m_hammerOff[2],
	};

	XMMATRIX world =
		XMMatrixScaling(m_hammerScale, m_hammerScale, m_hammerScale) *
		XMMatrixRotationRollPitchYaw(m_hammerRot[0], m_hammerRot[1], m_hammerRot[2]) *
		XMMatrixTranslation(pos.x, pos.y, pos.z);
	world = hammer->GetScaleBaseMatrix() * world;
	DrawModelWorld(hammer, world);
}

//--- m_th[] から3D鉄条の頂点を生成(両面。戻り値=頂点数)
int SceneForge::BuildBarMesh()
{
	int v = 0;
	const float half = m_barLen * 0.5f;
	const float ax   = m_barAnchor.x;	// 砧面中心に合わせる
	const float az   = m_barAnchor.z;
	const float cy   = m_barAnchor.y;

	auto tri = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT4& col)
	{
		m_barVtx[v++] = { a, XMFLOAT2(0,0), col };
		m_barVtx[v++] = { b, XMFLOAT2(0,0), col };
		m_barVtx[v++] = { c, XMFLOAT2(0,0), col };
	};
	auto quad = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT3& d, const XMFLOAT4& col)
	{
		tri(a, b, c, col); tri(a, c, d, col);	// 表
		tri(a, c, b, col); tri(a, d, c, col);	// 裏(両面)
	};

	// 鉄条は Z 方向(奥行き)に伸びる。x=幅, y=厚み, z=長さ
	const float w = m_barWidth;	// 半分の幅(X方向)
	for (int i = 0; i < SEG - 1; ++i)
	{
		float z0 = az - half + m_barLen * (i       / (float)(SEG - 1));
		float z1 = az - half + m_barLen * ((i + 1) / (float)(SEG - 1));
		float y0 = m_barThick * m_th[i], y1 = m_barThick * m_th[i + 1];
		XMFLOAT4 col = HeatRGB(m_heat, m_dmg[i]);

		// 打撃位置(照準)のセグメントを明るくして見えるようにする
		if (abs(i - m_strikeSeg) <= 1)
		{
			col.x = (col.x + 0.4f > 1.0f) ? 1.0f : col.x + 0.4f;
			col.y = (col.y + 0.4f > 1.0f) ? 1.0f : col.y + 0.4f;
			col.z = (col.z + 0.4f > 1.0f) ? 1.0f : col.z + 0.4f;
		}

		XMFLOAT3 a_tL = { ax - w, cy + y0, z0 }, a_tR = { ax + w, cy + y0, z0 };
		XMFLOAT3 a_bL = { ax - w, cy - y0, z0 }, a_bR = { ax + w, cy - y0, z0 };
		XMFLOAT3 b_tL = { ax - w, cy + y1, z1 }, b_tR = { ax + w, cy + y1, z1 };
		XMFLOAT3 b_bL = { ax - w, cy - y1, z1 }, b_bR = { ax + w, cy - y1, z1 };

		quad(a_tL, a_tR, b_tR, b_tL, col);	// 上面
		quad(a_bR, a_bL, b_bL, b_bR, col);	// 下面
		quad(a_tR, a_bR, b_bR, b_tR, col);	// 右(X+)
		quad(a_bL, a_tL, b_tL, b_bL, col);	// 左(X-)
	}
	// 端の蓋
	{
		float y = m_barThick * m_th[0]; float zc = az - half; XMFLOAT4 c = HeatRGB(m_heat, m_dmg[0]);
		quad({ ax - w,cy + y,zc }, { ax + w,cy + y,zc }, { ax + w,cy - y,zc }, { ax - w,cy - y,zc }, c);
	}
	{
		float y = m_barThick * m_th[SEG - 1]; float zc = az + half; XMFLOAT4 c = HeatRGB(m_heat, m_dmg[SEG - 1]);
		quad({ ax - w,cy + y,zc }, { ax + w,cy + y,zc }, { ax + w,cy - y,zc }, { ax - w,cy - y,zc }, c);
	}
	return v;
}

//--- 3Dの光る鉄条を描画
void SceneForge::Draw3DBillet()
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Bar");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Bar");
	if (!cam || !vs || !ps || !m_barMesh) return;

	XMFLOAT4X4 cb[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, cb);

	int n = BuildBarMesh();
	if (n <= 0) return;
	m_barMesh->Write(m_barVtx.data());

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind();
	ps->Bind();
	m_barMesh->Draw(n);
}

void SceneForge::Draw()
{
	ApplyCamera();		// 固定カメラを適用(GetViewの前に)
	DrawModelsTest();	// 先に不透明な3Dモデル(金床)を描く
	Draw3DBillet();		// 3Dの光る鉄条
	if (DebugUI::IsVisible()) DrawDebugBoxes();	// F1中はAABB/箱を線で表示

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

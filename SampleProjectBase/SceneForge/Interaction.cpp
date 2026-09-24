// Part of SceneForge: 走動中の互動(インタラクト)。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// 「E」提示が出る条件 = ①範囲 かつ ②視線(Detroit: Become Human 等の互動提示と同じ考え方)。
//   ①範囲 : 玩家の足元が「互動範囲の箱」の中。箱=物件のワールドAABBを水平に reach だけ広げたもの。
//            物理の衝突(押し返し)は不要な「重なり判定(トリガー)」だけなので、衝突システムとは独立。
//   ②視線 : 目線の射線が物件の箱に当たる。鍛造の照準と同じ AimSystem::Raycast(スラブ法)を再利用。
//            角度(内積)で判定するより、物件の「大きさ」がそのまま効く=大きい物は狙いやすく小さい物は正確に。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Geometory.h"
#include "DebugUI.h"
#include "AimSystem.h"
#include "Lerp.h"
#include "imgui.h"
#include <cfloat>
#include <cmath>

using namespace DirectX;

//--- 互動できる物件の表(データ)。新しい互動は1行足す+DoInteract に行為を書くだけ。
//    reach(単位) = 物件の箱の縁から水平にどこまで離れても手が届くか。
const SceneForge::Interactable SceneForge::INTERACTABLES[] = {
	{ "StAnvil",  SceneForge::InteractAction::EnterForge, 1.0f },	// 金床 → 工位(鍛造)へ
	{ "StPliers", SceneForge::InteractAction::TakeTongs,  0.9f },	// 作業台の火钳 → 取って翻面へ
};
const int SceneForge::NUM_INTERACTABLES = _countof(SceneForge::INTERACTABLES);

//--- プロップのワールドAABB(モデル空間AABBの8隅を配置変換で運び、その外接箱を取る)。
bool SceneForge::PropWorldBox(Prop& p, XMFLOAT3& mn, XMFLOAT3& mx)
{
	XMMATRIX world = PropWorld(p);
	mn = XMFLOAT3( FLT_MAX,  FLT_MAX,  FLT_MAX);
	mx = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 c(
			(i & 1) ? p.aabbMax.x : p.aabbMin.x,
			(i & 2) ? p.aabbMax.y : p.aabbMin.y,
			(i & 4) ? p.aabbMax.z : p.aabbMin.z);
		XMFLOAT3 w; XMStoreFloat3(&w, XMVector3TransformCoord(XMLoadFloat3(&c), world));
		mn.x = fminf(mn.x, w.x); mn.y = fminf(mn.y, w.y); mn.z = fminf(mn.z, w.z);
		mx.x = fmaxf(mx.x, w.x); mx.y = fmaxf(mx.y, w.y); mx.z = fmaxf(mx.z, w.z);
	}
	return mn.x <= mx.x;
}

//--- 今この互動ができる状況か(物件ごとの前提条件)。
bool SceneForge::InteractEnabled(InteractAction a) const
{
	switch (a)
	{
	case InteractAction::EnterForge: return true;
	case InteractAction::TakeTongs:
		// 台の火钳は工程に関係なく取れる(ユーザー決定)。既に手に持っている時だけ取れない。
		//   ※工位で F で取るのは鍛打工程だけ(UpdateFlip 側)。
		return !m_tongsInHand;
	}
	return false;
}

//--- ①範囲 ②視線 の2判定で「今 E で互動できる物件」m_focus を決める。複数当たれば視線上で一番手前。
void SceneForge::UpdateInteract(float tick)
{
	int best = -1;
	float bestT = FLT_MAX;
	XMFLOAT3 bestCenter(0, 0, 0);

	if (m_state == GAME_PLAY && m_walkMode && !Transitioning())
	{
		// 目線の射線(ApplyWalkCamera と同じ規約で、玩家の状態から直接作る=カメラ適用順に依存しない)
		XMFLOAT3 foot = m_player.GetPosition();
		XMFLOAT3 eye(foot.x, foot.y + m_walkEyeH, foot.z);
		float cp = cosf(m_walkPitch), yaw = m_player.GetYaw();
		XMFLOAT3 fwd(sinf(yaw) * cp, sinf(m_walkPitch), cosf(yaw) * cp);

		for (int i = 0; i < NUM_INTERACTABLES; ++i)
		{
			const Interactable& it = INTERACTABLES[i];
			if (!InteractEnabled(it.action)) continue;
			Prop* p = GetProp(it.propKey);
			if (!p || p->hidden) continue;
			XMFLOAT3 mn, mx;
			if (!PropWorldBox(*p, mn, mx)) continue;

			// ① 範囲: 足元(XZ)が、物件の箱を水平に reach 広げた箱の中か(高さは問わない=床の上に立つ前提)
			bool inRange = foot.x >= mn.x - it.reach && foot.x <= mx.x + it.reach
			            && foot.z >= mn.z - it.reach && foot.z <= mx.z + it.reach;
			if (!inRange) continue;

			// ② 視線: 目線の射線が、少し膨らませた物件の箱に当たるか(区域1つ=箱1つの Raycast)
			XMFLOAT3 pmn(mn.x - m_lookPad, mn.y - m_lookPad, mn.z - m_lookPad);
			XMFLOAT3 pmx(mx.x + m_lookPad, mx.y + m_lookPad, mx.z + m_lookPad);
			AimSystem::Hit h = AimSystem::Raycast(eye, fwd, XMMatrixIdentity(), pmn, pmx, 1);
			if (!h.valid) continue;

			if (h.t < bestT)
			{
				bestT = h.t;
				best  = i;
				bestCenter = XMFLOAT3((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
			}
		}
	}

	m_focus = best;
	if (best >= 0) m_promptPoint = bestCenter;	// 消える時は最後の位置のままフェードアウト
	m_promptAlpha = Lerp::Damp(m_promptAlpha, best >= 0 ? 1.0f : 0.0f, m_promptLambda, tick);
	m_player.SetCanInteract(best >= 0);			// Player::WantInteract の前提(両判定 true の時だけ E が効く)
}

//--- E を押された物件の行為。
void SceneForge::DoInteract(InteractAction a)
{
	switch (a)
	{
	case InteractAction::EnterForge:
		BeginEnterForge();
		break;
	case InteractAction::TakeTongs:
		// 火钳を手に取る(台上のモデルを消す)→ 工位へ移動 → 着いたら翻面の「火钳待命」から始める。
		//   翻面中は E で工位から出られない規則なので、火钳を持って離れる状態は生まれない。
		m_tongsInHand = true;
		if (Prop* pl = GetProp("StPliers")) pl->hidden = true;
		m_pendingFlip = true;
		BeginEnterForge();
		break;
	}
}

//--- 物件の上に「E」ボタンを描く。物件の中心をスクリーンへ投影し、そこに丸いキーアイコンを置く。
//    CPU 側の投影は転置しない行列(GetView(false)/GetProj(false))を使う(shader 用の既定は転置済み)。
void SceneForge::DrawInteractPrompt()
{
	if (m_promptAlpha < 0.01f) return;
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;

	XMFLOAT4X4 v4 = cam->GetView(false), p4 = cam->GetProj(false);
	XMVECTOR clip = XMVector4Transform(
		XMVectorSet(m_promptPoint.x, m_promptPoint.y, m_promptPoint.z, 1.0f),
		XMLoadFloat4x4(&v4) * XMLoadFloat4x4(&p4));
	float w = XMVectorGetW(clip);
	if (w <= 0.0f) return;	// カメラの後ろ

	ImVec2 disp = ImGui::GetIO().DisplaySize;	// 解像度非依存(画面比で配置)
	float sx = ( XMVectorGetX(clip) / w * 0.5f + 0.5f) * disp.x;
	float sy = (-XMVectorGetY(clip) / w * 0.5f + 0.5f) * disp.y;

	// 見た目の寸法は画面の高さに対する比(=ウィンドウサイズが変わっても同じ見え方)
	const float R_RATIO    = 0.030f;	// 丸の半径
	const float RING_RATIO = 0.004f;	// 外枠の太さ
	const float TEXT_RATIO = 0.034f;	// 「E」の文字の高さ
	const float r    = disp.y * R_RATIO;
	const float ring = disp.y * RING_RATIO;
	const int   a    = (int)(255 * m_promptAlpha);

	ImDrawList* dl = ImGui::GetForegroundDrawList();
	dl->AddCircleFilled(ImVec2(sx, sy), r, IM_COL32(15, 12, 10, (int)(a * 0.85f)));	// 暗い円盤
	dl->AddCircle(ImVec2(sx, sy), r, IM_COL32(240, 225, 200, a), 0, ring);			// 明るい外枠

	ImFont* f  = DebugUI::FontBody() ? DebugUI::FontBody() : ImGui::GetFont();
	float   px = disp.y * TEXT_RATIO;
	ImVec2  ts = f->CalcTextSizeA(px, FLT_MAX, 0.0f, "E");
	dl->AddText(f, px, ImVec2(sx - ts.x * 0.5f, sy - ts.y * 0.5f), IM_COL32(255, 200, 120, a), "E");	// 暖色=鍛冶の火
}

//--- F1: 互動範囲の箱(①)を線で表示。
//    赤=今は互動できない状況(例: 火钳は鍛打工程だけ) / 灰=範囲外 / 緑=範囲内 / 黄=注視中(E が出ている)。
void SceneForge::DrawInteractBoxes()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());

	XMFLOAT3 foot = m_player.GetPosition();
	for (int i = 0; i < NUM_INTERACTABLES; ++i)
	{
		const Interactable& it = INTERACTABLES[i];
		Prop* p = GetProp(it.propKey);
		if (!p) continue;
		XMFLOAT3 mn, mx;
		if (!PropWorldBox(*p, mn, mx)) continue;
		mn.x -= it.reach; mn.z -= it.reach; mx.x += it.reach; mx.z += it.reach;
		bool inRange = foot.x >= mn.x && foot.x <= mx.x && foot.z >= mn.z && foot.z <= mx.z;

		XMFLOAT4 col = !InteractEnabled(it.action) ? XMFLOAT4(1.0f, 0.25f, 0.2f, 1.0f)
		             : (i == m_focus)              ? XMFLOAT4(1.0f, 0.9f, 0.2f, 1.0f)
		             : inRange                     ? XMFLOAT4(0.2f, 1.0f, 0.3f, 1.0f)
		                                           : XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f);
		Geometory::SetColor(col);
		for (int c = 0; c < 8; ++c)			// 12辺: 1ビットだけ違う隅同士を結ぶ
			for (int b = 1; b <= 4; b <<= 1)
				if (!(c & b))
				{
					int d = c | b;
					XMFLOAT3 pc((c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z);
					XMFLOAT3 pd((d & 1) ? mx.x : mn.x, (d & 2) ? mx.y : mn.y, (d & 4) ? mx.z : mn.z);
					Geometory::AddLine(pc, pd);
				}
	}
	Geometory::DrawLines();
}

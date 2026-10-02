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
	{ "StAnvil",  SceneForge::InteractAction::EnterStation, Station::Anvil,      1.0f },	// 金床 → 鍛打
	{ "StForge",  SceneForge::InteractAction::EnterStation, Station::Hearth,     1.0f },	// 炉 → 加熱(いつでも再加熱できる)
	{ "StGrind",  SceneForge::InteractAction::EnterStation, Station::Grindstone, 0.9f },	// 砥石 → 研磨
	{ "StTrough", SceneForge::InteractAction::EnterStation, Station::Trough,     1.0f },	// 水槽 → 淬火
	{ SceneForge::IRON_KEY,  SceneForge::InteractAction::GripIron,   Station::Anvil, 1.2f },	// 置いてある鉄 → 火钳で掴んで運ぶ(station は使わない)
	{ CottageDoor::DOOR_KEY, SceneForge::InteractAction::ToggleDoor, Station::Anvil, 1.0f },	// 裏口の扉 → 開閉(station は使わない)
};
const int SceneForge::NUM_INTERACTABLES = _countof(SceneForge::INTERACTABLES);

//--- モデル空間AABBの8隅を world で運び、その外接箱(ワールドAABB)を取る。
static bool TransformAABB(const XMFLOAT3& lmn, const XMFLOAT3& lmx, const XMMATRIX& world, XMFLOAT3& mn, XMFLOAT3& mx)
{
	mn = XMFLOAT3( FLT_MAX,  FLT_MAX,  FLT_MAX);
	mx = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 c((i & 1) ? lmx.x : lmn.x, (i & 2) ? lmx.y : lmn.y, (i & 4) ? lmx.z : lmn.z);
		XMFLOAT3 w; XMStoreFloat3(&w, XMVector3TransformCoord(XMLoadFloat3(&c), world));
		mn.x = fminf(mn.x, w.x); mn.y = fminf(mn.y, w.y); mn.z = fminf(mn.z, w.z);
		mx.x = fmaxf(mx.x, w.x); mx.y = fmaxf(mx.y, w.y); mx.z = fmaxf(mx.z, w.z);
	}
	return mn.x <= mx.x;
}

//--- プロップのワールドAABB。
bool SceneForge::PropWorldBox(Prop& p, XMFLOAT3& mn, XMFLOAT3& mx)
{
	return TransformAABB(p.aabbMin, p.aabbMax, PropWorld(p), mn, mx);
}

//--- 互動物件のワールドAABB。普通はプロップの箱。扉は家の部品(プロップではない)なので、
//    扉モデルの箱を「今の角度の扉の行列」で運ぶ=開いた扉は開いた位置で狙える。
bool SceneForge::InteractBox(const Interactable& it, XMFLOAT3& mn, XMFLOAT3& mx)
{
	if (it.action == InteractAction::ToggleDoor)
	{
		Model* door = GetObj<Model>(CottageDoor::DOOR_KEY);
		if (!door || !GetProp(CottageDoor::HOUSE_KEY)) return false;
		XMFLOAT3 lmn, lmx;
		door->GetLocalAABB(lmn, lmx);
		return TransformAABB(lmn, lmx, DoorWorld(), mn, mx);
	}
	if (it.action == InteractAction::GripIron)
	{
		// 鉄もプロップではない: 武器モデルの箱を「今置いてある所」の行列で運ぶ(描画/照準と同じ WeaponWorld)。
		if (!m_wpOk) return false;
		return TransformAABB(m_wpMin, m_wpMax, WeaponWorld(), mn, mx);
	}
	Prop* p = GetProp(it.propKey);
	if (!p || p->hidden) return false;
	return PropWorldBox(*p, mn, mx);
}

//--- 今この互動ができる状況か(物件ごとの前提条件)。
bool SceneForge::InteractEnabled(const Interactable& it) const
{
	switch (it.action)
	{
	case InteractAction::EnterStation:
		// 工位は工程では縛らない(玩家の自由)。E はいつでも押せる。ただし鉄がそこに無く、手にも持っていなければ
		// 入らずに鉄の在り処を言うだけ(DoInteract)。=縛るのは「工程」でなく「鉄が物理的にそこにあるか」。
		return true;
	case InteractAction::GripIron:
		return !m_carrying;		// 既に手に持っていれば掴む対象は無い
	case InteractAction::ToggleDoor:
		// 扉はいつでも開閉できる(回っている途中でも押し直せば引き返す)。
		return true;
	}
	return false;
}

//--- 視線が複数の箱に当たった時の優先度(大きい方が勝つ。同じなら手前)。
//    鉄は工位の箱の「中」に置かれている(炉の炭床、金床の上)ので、手前優先だけだと工位の箱に隠れて掴めない。
//    → 鉄に視線が当たっていれば鉄を優先する。
int SceneForge::InteractPriority(const Interactable& it) const
{
	return it.action == InteractAction::GripIron ? 1 : 0;
}

//--- 「E」の下に出す一言。押すと何が起きるかを言う(宏観の案内。KCD 未プレイの人でも流れが分かる様に)。
const char* SceneForge::PromptLabel(const Interactable& it) const
{
	switch (it.action)
	{
	case InteractAction::EnterStation:
		if (m_carrying)             return (const char*)u8"ここに鉄を置く";
		if (m_workAt == it.station) return (const char*)u8"ここで作業する";
		return (const char*)u8"鉄はここに無い";
	case InteractAction::GripIron:   return (const char*)u8"火ばさみで鉄を掴む";
	case InteractAction::ToggleDoor: return m_door.IsOpen() ? (const char*)u8"扉を閉める" : (const char*)u8"扉を開ける";
	}
	return "";
}

//--- ①範囲 ②視線 の2判定で「今 E で互動できる物件」m_focus を決める。複数当たれば優先度が高い物、同じなら視線上で一番手前。
void SceneForge::UpdateInteract(float tick)
{
	int best = -1;
	int bestPri = -1;
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
			if (!InteractEnabled(it)) continue;
			XMFLOAT3 mn, mx;
			if (!InteractBox(it, mn, mx)) continue;

			// ① 範囲: 足元(XZ)が、物件の箱を水平に reach 広げた箱の中か(高さは問わない=床の上に立つ前提)
			bool inRange = foot.x >= mn.x - it.reach && foot.x <= mx.x + it.reach
			            && foot.z >= mn.z - it.reach && foot.z <= mx.z + it.reach;
			if (!inRange) continue;

			// ② 視線: 目線の射線が、少し膨らませた物件の箱に当たるか(区域1つ=箱1つの Raycast)
			XMFLOAT3 pmn(mn.x - m_lookPad, mn.y - m_lookPad, mn.z - m_lookPad);
			XMFLOAT3 pmx(mx.x + m_lookPad, mx.y + m_lookPad, mx.z + m_lookPad);
			AimSystem::Hit h = AimSystem::Raycast(eye, fwd, XMMatrixIdentity(), pmn, pmx, 1);
			if (!h.valid) continue;

			const int pri = InteractPriority(it);
			if (pri > bestPri || (pri == bestPri && h.t < bestT))
			{
				bestPri = pri;
				bestT = h.t;
				best  = i;
				// 提示を出す点: 工位は「実際に作業する点」(炉=炭床 / 金床=砧面 / 砥石=輪の上 / 水槽=水面)。
				//   箱の中心だと、煙突の高い炉では中心が視界の上に外れて提示が見えなくなる。
				//   その他(火钳/扉)は箱の中心。
				if (it.action == InteractAction::EnterStation) bestCenter = StationBase(it.station);
				else bestCenter = XMFLOAT3((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
			}
		}
	}

	m_focus = best;
	if (best >= 0)	// 消える時は最後の位置/文言のままフェードアウト
	{
		m_promptPoint = bestCenter;
		m_promptLabel = PromptLabel(INTERACTABLES[best]);
	}
	m_promptAlpha = Lerp::Damp(m_promptAlpha, best >= 0 ? 1.0f : 0.0f, m_promptLambda, tick);
	m_player.SetCanInteract(best >= 0);			// Player::WantInteract の前提(両判定 true の時だけ E が効く)
}

//--- E を押された物件の行為。
void SceneForge::DoInteract(const Interactable& it)
{
	switch (it.action)
	{
	case InteractAction::EnterStation:
		// 鉄は瞬間移動しない:
		//   手に持っている     → その工位に置いて入る
		//   その工位に置いてある → そのまま入る
		//   別の所にある       → 入らない。鉄の在り処を言うだけ(玩家が自分で取りに行く。自動では動かない)
		if (m_carrying)                   { PutIronAt(it.station); BeginEnterStation(it.station); }
		else if (m_workAt == it.station)  BeginEnterStation(it.station);
		else                              SayWhereIronIs();
		break;
	case InteractAction::GripIron:
		GripIron();		// 腰の火钳で掴む → 以後、鉄は手の前に付いて来る(Carry.cpp)
		break;
	case InteractAction::ToggleDoor:
		// 開閉を切り替えるだけ。回転・衝突(扉の凸包)・提示の位置は m_door の角度に全部追従する。
		m_door.Toggle();
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

	// 丸の下に「押すと何が起きるか」を一言(鉄を掴む/ここに置く/ここで作業/鉄はここに無い…)。
	if (m_promptLabel && m_promptLabel[0])
	{
		const float LABEL_RATIO = 0.026f;	// 文言の文字の高さ
		const float LABEL_GAP   = 0.010f;	// 丸との間
		ImFont* jp = DebugUI::FontJP();		// 文言は日本語(游明朝)。「E」だけ英字フォント
		float  lpx = disp.y * LABEL_RATIO;
		ImVec2 ls  = jp->CalcTextSizeA(lpx, FLT_MAX, 0.0f, m_promptLabel);
		ImVec2 lp(sx - ls.x * 0.5f, sy + r + disp.y * LABEL_GAP);
		dl->AddText(jp, lpx, ImVec2(lp.x + 1.0f, lp.y + 1.0f), IM_COL32(0, 0, 0, (int)(a * 0.8f)), m_promptLabel);	// 影(明るい背景でも読める)
		dl->AddText(jp, lpx, lp, IM_COL32(240, 225, 200, a), m_promptLabel);
	}
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
		XMFLOAT3 mn, mx;
		if (!InteractBox(it, mn, mx)) continue;
		mn.x -= it.reach; mn.z -= it.reach; mx.x += it.reach; mx.z += it.reach;
		bool inRange = foot.x >= mn.x && foot.x <= mx.x && foot.z >= mn.z && foot.z <= mx.z;

		XMFLOAT4 col = !InteractEnabled(it) ? XMFLOAT4(1.0f, 0.25f, 0.2f, 1.0f)
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

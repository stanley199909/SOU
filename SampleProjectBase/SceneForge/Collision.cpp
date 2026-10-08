// Part of SceneForge: 走動中の衝突(玩家が道具・壁・閉じた扉を通り抜けない)。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// 考え方(鳴潮/UE と同じ「描画メッシュとは別の、簡略化した衝突形状」):
//   ・玩家は床の上しか歩かない → 3D を上から見た 2D(XZ)に潰して解く。
//   ・玩家 = 足元の円(カプセルを上から見た形)。角が無いので斜めの辺や角を滑らかに擦り抜ける。
//   ・形で衝突形状を使い分ける(Physics/Collision2D):
//       道具 = モデルの全頂点を床へ投影した「2D 凸包」。凸な物にぴったり、しかも軽い。
//       建物 = 凹んだ形(部屋)なので凸包は不可(室内が塞がる)。腰の高さで水平に切った「断面の線分」。
//              戸口はその高さで空いている=線分が出ない=自動で通路になる。
//       扉   = 扉板の凸包を、蝶番の回転ごと運ぶ。閉=戸口を塞ぐ / 開=戸口の脇へ退く。
//   ・応答 = 押し出し(Player::ResolveCollision)。重なった分だけ接触法線の方向へ外へ出す。
// 粗判定(AABB で候補を絞る)は省略。道具十数個+壁線数千本なら全部と直接判定しても十分軽い。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Geometory.h"
#include "Model.h"
#include "PropParts.h"
#include "DirectX.h"	// SetDepthTest(可視化を壁越しに見せる)
#include <cmath>
#include <cstring>

using namespace DirectX;

//--- ぶつかる道具の表(データ, キーの前方一致)。新しい道具をぶつかる様にするには1行足すだけ。
//    前方一致なので "StOutdoorTree" 1行で木 A/B/C… が全部入る(配置ファイルで本数を増やしてもコード不要)。
//    入れない物: 小物(火钳/火かき棒/廃鉄=台の上にある・手に取る物)、床 StGround / StOutdoorGround、草。
//    整屋 StCottage は凸包ではなく断面の壁線で扱う(下の UpdateWallSlice)。
//    Whole = 凸包1つ / PerPiece = 繋がった塊ごとに凸包(1モデルに離れた物が複数入っている時)。
using HS = SceneForge::HullShape;
const SceneForge::Collider SceneForge::COLLIDERS[] = {
	{ "StStump",        HS::Whole },	// 樹桩(金床の台)
	{ "StAnvil",        HS::Whole },	// 金床
	{ "StForge",        HS::Whole },	// 炉
	{ "StStand",        HS::Whole },	// 風箱の支架
	{ "StBellows",      HS::Whole },	// 風箱
	{ "StWorktable",    HS::Whole },	// 作業台
	{ "StTrough",       HS::Whole },	// 水槽
	{ "StBucket",       HS::Whole },	// 水桶
	{ "StGrind",        HS::Whole },	// 砥石(輪は動く部品=別モデル。凸包には含める)
	{ "StOutdoorTree",  HS::Whole },	// 屋外の木(背丈以下=幹だけ。葉は2千以上の塊なので Whole)
	{ "StOutdoorRocks", HS::PerPiece },	// 屋外の石: 離れた4個の石 → 石ごと(間は通れる)
	{ "StOutdoorLogs",  HS::Whole },	// 屋外の丸太の山: 積み重なった12本=山全体で1つ
	// 草(StOutdoorGrass)は入れない: 踏み込める。代わりに草が玩家を避けて倒れる表現にする(ユーザー決定)。
};
const int SceneForge::NUM_COLLIDERS = _countof(SceneForge::COLLIDERS);

namespace
{
	// 頂点群を床へ投影して凸包にする(.y に world Z を入れる規約)。
	Collision2D::Hull FlatHull(const std::vector<XMFLOAT3>& verts)
	{
		std::vector<XMFLOAT2> flat;
		flat.reserve(verts.size());
		for (const XMFLOAT3& v : verts) flat.push_back(XMFLOAT2(v.x, v.z));
		return Collision2D::ConvexHull(std::move(flat));
	}

	// モデル空間の XZ 点をワールドの XZ へ運ぶ。
	//   配置は「一様スケール × Y軸回転 × 平行移動」(扉は更に Y軸まわりの蝶番回転)だけなので、
	//   「床へ投影」と「配置」は順番を入れ替えても同じ=高さ 0 の点として運べばよい。
	XMFLOAT2 ToWorldXZ(const XMFLOAT2& q, const XMMATRIX& world)
	{
		XMFLOAT3 w;
		XMStoreFloat3(&w, XMVector3TransformCoord(XMVectorSet(q.x, 0.0f, q.y, 1.0f), world));
		return XMFLOAT2(w.x, w.z);
	}
}

const SceneForge::Collider* SceneForge::FindCollider(const std::string& key) const
{
	for (int i = 0; i < NUM_COLLIDERS; ++i)
		if (key.compare(0, strlen(COLLIDERS[i].keyPrefix), COLLIDERS[i].keyPrefix) == 0) return &COLLIDERS[i];	// 前方一致
	return nullptr;
}

//--- モデル空間の凸包を作る。
//    ・背丈(床 + PLAYER_BODY_HEIGHT)より上の頂点は捨てる: 木の樹冠・張り出した梁は頭上を通るので
//      ぶつからない。全頂点で凸包を作ると樹冠の広さの「見えない壁」になってしまう。
//    ・凸包はモデル空間で一度だけ作り、毎フレームは凸包の点(数十個)だけ運ぶ
//      (数万頂点を毎フレーム運ぶより遥かに軽く、エディタで水平に動かしても自動で追従する)。
void SceneForge::BuildPropHull(Prop& p)
{
	p.hullsLocal.clear();
	const Collider* col = FindCollider(p.key);
	if (!col) return;
	Model* m = GetObj<Model>(p.key.c_str());
	if (!m) return;

	std::vector<XMFLOAT3> tris;
	m->AppendLocalTriangles(tris);
	if (const PropPart* part = FindPropPart(p.key.c_str()))	// 砥石の輪などの動く部品も足元を占める
		if (Model* pm = GetObj<Model>(part->partKey))
			pm->AppendLocalTriangles(tris);

	// 塊ごと(PerPiece)なら、繋がった三角形の塊に分けてから、それぞれ凸包にする
	std::vector<std::vector<XMFLOAT3>> pieces;
	if (col->shape == HullShape::PerPiece) pieces = Collision2D::SplitConnected(tris);
	else                                   pieces.push_back(std::move(tris));

	// 背丈以下の頂点だけ残す(高さの判定は今の配置のワールド座標で)
	const XMMATRIX world = PropWorld(p);
	const float headY = m_walkFloorY + PLAYER_BODY_HEIGHT;
	for (const std::vector<XMFLOAT3>& piece : pieces)
	{
		std::vector<XMFLOAT3> body;
		body.reserve(piece.size());
		for (const XMFLOAT3& v : piece)
			if (XMVectorGetY(XMVector3TransformCoord(XMLoadFloat3(&v), world)) <= headY)
				body.push_back(v);
		Collision2D::Hull h = FlatHull(body);
		if (!h.empty()) p.hullsLocal.push_back(std::move(h));
	}
}

void SceneForge::BuildPropHulls()
{
	for (Prop& p : m_props) BuildPropHull(p);
}

//--- 家の三角形と扉の蝶番/凸包を用意する(Init で1回。モデルは SceneRoot が共有で読み済み)。
void SceneForge::InitBuildingCollision()
{
	m_houseTris.clear();
	m_wallSegLocal.clear();
	m_wallSliceLocalY = -1e30f;	// 次の UpdateWallSlice で必ず切る
	m_wallSliceStepUsed = -1.0f;
	Model* house = GetObj<Model>(CottageDoor::HOUSE_KEY);
	Model* door  = GetObj<Model>(CottageDoor::DOOR_KEY);
	if (house) house->AppendLocalTriangles(m_houseTris);	// 扉は SceneRoot で除いてある=戸口は空いている

	m_doorHinge = CottageDoor::ComputeHinge(door, house);
	m_doorHullLocal.clear();
	if (door)
	{
		std::vector<XMFLOAT3> verts;
		door->AppendLocalVertices(verts);
		m_doorHullLocal = FlatHull(verts);
	}
	m_door.Reset();
}

//--- 扉のワールド行列 = 蝶番の回転(家のモデル空間) × 家の配置。
XMMATRIX SceneForge::DoorWorld()
{
	Prop* house = GetProp(CottageDoor::HOUSE_KEY);
	if (!house) return XMMatrixIdentity();
	return CottageDoor::HingeMatrix(m_doorHinge, m_door.Angle()) * PropWorld(*house);
}

//--- 壁線 = 体が占める高さ(床+m_wallSliceLowHeight 〜 床+PLAYER_BODY_HEIGHT)を m_wallSliceStep ごとに切った線を全部合わせた物。
//    1つの高さだけだと、その高さに無い物を素通りした: 壁から張り出した窓枠の上の横木に、目(1.6m)が入り込んだ(2026-10-08 F5)。
//    体の高さ全体を切れば、体のどこかに当たる物は全部ぶつかる。戸口は扉を除いてあるので、どの高さでも空いている。
//    家の配置(大きさ/高さ)か切る設定が変わった時だけ切り直す(6万三角形 × 数枚でも一度きり。毎フレームは無駄)。
void SceneForge::UpdateWallSlice(const XMMATRIX& houseWorld)
{
	// 家の行列は 一様スケール×Y回転×平行移動 → 高さは localY*scale + originY。これを逆算する。
	const float scale   = XMVectorGetX(XMVector3Length(houseWorld.r[0]));
	const float originY = XMVectorGetY(houseWorld.r[3]);
	if (scale <= 0.0f) return;
	const float MIN_STEP = 0.05f;	// 0 刻みで無限ループしない様に
	const float step     = fmaxf(m_wallSliceStep, MIN_STEP);
	const float localLow = (m_walkFloorY + m_wallSliceLowHeight - originY) / scale;
	if (fabsf(localLow - m_wallSliceLocalY) < WALL_RESLICE_EPS && fabsf(step - m_wallSliceStepUsed) < WALL_RESLICE_EPS) return;

	m_wallSegLocal.clear();
	for (float h = m_wallSliceLowHeight; h <= PLAYER_BODY_HEIGHT + WALL_RESLICE_EPS; h += step)
		Collision2D::SliceTriangles(m_houseTris, (m_walkFloorY + h - originY) / scale, m_wallSegLocal);	// 追記(SliceTriangles は out に足す)
	m_wallSliceLocalY   = localLow;
	m_wallSliceStepUsed = step;
}

//--- 凸包/壁線/扉を今の配置でワールド XZ へ運ぶ(毎フレーム)。
void SceneForge::BuildCollisionWorld()
{
	m_collision.Clear();

	// 道具の凸包
	for (Prop& p : m_props)
	{
		if (p.hidden || p.hullsLocal.empty()) continue;
		XMMATRIX world = PropWorld(p);
		for (const Collision2D::Hull& local : p.hullsLocal)
		{
			Collision2D::Hull h;
			h.reserve(local.size());
			for (const XMFLOAT2& q : local) h.push_back(ToWorldXZ(q, world));
			m_collision.hulls.push_back(std::move(h));
		}
	}

	// 建物の壁線
	if (Prop* house = GetProp(CottageDoor::HOUSE_KEY))
	{
		XMMATRIX world = PropWorld(*house);
		UpdateWallSlice(world);
		m_collision.segments.reserve(m_wallSegLocal.size());
		for (const Collision2D::Segment& s : m_wallSegLocal)
			m_collision.segments.push_back(Collision2D::Segment{ ToWorldXZ(s.a, world), ToWorldXZ(s.b, world) });

		// 扉(今の角度で回した凸包)。開いていれば戸口の脇にあり、通路を塞がない。
		if (!m_doorHullLocal.empty())
		{
			XMMATRIX dw = DoorWorld();
			Collision2D::Hull h;
			h.reserve(m_doorHullLocal.size());
			for (const XMFLOAT2& q : m_doorHullLocal) h.push_back(ToWorldXZ(q, dw));
			m_collision.hulls.push_back(std::move(h));
		}
	}
}

//--- 可視化: 凸包を床から背丈までの「柱」、壁線を切った高さの線、玩家の足元の円で描く。
//    水色=道具/扉の凸包 / 橙=壁線 / 赤=玩家の円が触れている / 黄=玩家の円。
void SceneForge::DrawCollision()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	BuildCollisionWorld();	// F1 中(更新が止まる)やエディタでの移動中も今の配置で描く

	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());

	const float y0 = m_walkFloorY + COLLISION_DRAW_LIFT;
	const float y1 = m_walkFloorY + PLAYER_BODY_HEIGHT;
	const float ys = m_walkFloorY + m_wallSliceLowHeight;	// 壁線は全部の高さの和なので、一番低い所に描く
	const XMFLOAT3 foot = m_player.GetPosition();
	const float r = m_player.GetRadius();
	const float probeR = r + COLLISION_TOUCH_MARGIN;	// 少し大きい円で試す=触れているだけでも赤
	const XMFLOAT4 RED(1.0f, 0.25f, 0.2f, 1.0f);

	for (const Collision2D::Hull& h : m_collision.hulls)
	{
		// 接触の判定は押し出しと同じ関数で(コピーに対して)。
		XMFLOAT2 probe(foot.x, foot.z);
		bool touching = Collision2D::PushCircleOut(probe, probeR, h);
		Geometory::SetColor(touching ? RED : XMFLOAT4(0.2f, 0.85f, 1.0f, 1.0f));

		const size_t n = h.size();
		for (size_t i = 0; i < n; ++i)
		{
			const XMFLOAT2& a = h[i];
			const XMFLOAT2& b = h[(i + 1) % n];
			Geometory::AddLine(XMFLOAT3(a.x, y0, a.y), XMFLOAT3(b.x, y0, b.y));	// 底の輪郭
			Geometory::AddLine(XMFLOAT3(a.x, y1, a.y), XMFLOAT3(b.x, y1, b.y));	// 上の輪郭
			Geometory::AddLine(XMFLOAT3(a.x, y0, a.y), XMFLOAT3(a.x, y1, a.y));	// 縦の辺
		}
	}

	for (const Collision2D::Segment& s : m_collision.segments)
	{
		XMFLOAT2 probe(foot.x, foot.z);
		bool touching = Collision2D::PushCircleOut(probe, probeR, s);
		Geometory::SetColor(touching ? RED : XMFLOAT4(1.0f, 0.6f, 0.15f, 1.0f));
		Geometory::AddLine(XMFLOAT3(s.a.x, ys, s.a.y), XMFLOAT3(s.b.x, ys, s.b.y));	// 切った高さ(実際に判定している線)
		Geometory::AddLine(XMFLOAT3(s.a.x, y0, s.a.y), XMFLOAT3(s.b.x, y0, s.b.y));	// 床にも同じ輪郭(足元で見やすい様に)
	}

	// 玩家の足元の円(多角形で近似)
	Geometory::SetColor(XMFLOAT4(1.0f, 0.9f, 0.2f, 1.0f));
	for (int i = 0; i < PLAYER_CIRCLE_SEGMENTS; ++i)
	{
		float a0 = XM_2PI * i / PLAYER_CIRCLE_SEGMENTS;
		float a1 = XM_2PI * (i + 1) / PLAYER_CIRCLE_SEGMENTS;
		Geometory::AddLine(XMFLOAT3(foot.x + cosf(a0) * r, y0, foot.z + sinf(a0) * r),
		                   XMFLOAT3(foot.x + cosf(a1) * r, y0, foot.z + sinf(a1) * r));
	}
	// 手の鉄のめり込み回避のレイ(Carry.cpp UpdateCarryAvoid)。紫=当たるまでの距離、先端に縦線。運んでいる時だけ。
	if (m_carrying)
	{
		const XMFLOAT4 PURPLE(0.85f, 0.3f, 1.0f, 1.0f);
		const XMFLOAT2 o = m_carryAvoidOrigin, d = m_carryAvoidDir;
		const XMFLOAT2 hit(o.x + d.x * m_carryAvoidFree, o.y + d.y * m_carryAvoidFree);
		Geometory::SetColor(PURPLE);
		Geometory::AddLine(XMFLOAT3(o.x, ys, o.y), XMFLOAT3(hit.x, ys, hit.y));
		Geometory::AddLine(XMFLOAT3(hit.x, y0, hit.y), XMFLOAT3(hit.x, y1, hit.y));
	}

	// 深度テストを切って描く(=透視。デバッグ表示の定石)。壁線は壁の表面を切った線なので
	// 表面と同じ深さにあり、深度テスト有りだと凸凹の石に隠れて見えない。道具の中の凸包も同じ。
	SetDepthTest(DEPTH_DISABLE);
	Geometory::DrawLines();	// 色は AddLine の時点で頂点ごとに記録される=最後に一度だけ流せばよい
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);	// 既定へ戻す(以降の不透明描画のため)
}

// Part of SceneForge: 鉄の運搬(火钳で掴んで歩いて運ぶ)。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// 設計(ユーザー決定 2026-09-30):
//   ・鉄は工位から工位へ瞬間移動しない。玩家が掴んで、自分の足で運ぶ。
//   ・火钳は普段は左腰に掛けてある。鉄を掴む時に抜き、鉄を置くと腰へ戻る(作業台へ取りに行かない)。
//   ・鉄の無い工位で E → 入れない。主人公が鉄の在り処を言うだけ(自動では動かない)。
// 「運んでいる」は玩家の状態(m_carrying)であって工程ではない: 配方(何をするか)と運搬(どうやるか)は別。
// 見た目は Anchor 方式: 鉄の置き場所 WorkAnchor() が「手の前の点」を返すだけで、描画/火花/照準は同じ経路を通る。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Model.h"
#include "AimSystem.h"
#include "imgui.h"
#include "Lerp.h"
#include "DirectX.h"
#include <cmath>

using namespace DirectX;

//--- 工位で体が居る所 = その工位の「既定の」カメラ(金床 = m_camPos/m_camLook、他 = StationView)。
//    今のカメラでなく既定値を使う: 翻面の運鏡/研ぎの視点でカメラが振り向いても、体(=腰の道具)は動かない。
//    旧: 工位なら常に金床カメラを使っていた → 炉/砥石/水槽では腰の道具が金床の所に浮いていた(2026-10-08 F5)。
void SceneForge::StationBodyPose(XMFLOAT3& eye, XMFLOAT3& target)
{
	if (m_station == Station::Anvil) { eye = XMFLOAT3(m_camPos[0], m_camPos[1], m_camPos[2]); target = XMFLOAT3(m_camLook[0], m_camLook[1], m_camLook[2]); }
	else StationView(m_station, eye, target);
}

//--- 体の水平前方。走動=玩家の向き / 工位=その工位の既定カメラの水平視線。
XMFLOAT3 SceneForge::BodyForward()
{
	if (SequencePlaying()) return m_seqBodyFwd;	// 拍子表の再生中は首だけ回る(体=腰の位置は動かない)
	if (m_walkMode) return m_player.GetForward();
	XMFLOAT3 eye, target; StationBodyPose(eye, target);
	float dx = target.x - eye.x, dz = target.z - eye.z, len = sqrtf(dx * dx + dz * dz);
	if (len < 1e-4f) return XMFLOAT3(0, 0, 1);
	return XMFLOAT3(dx / len, 0.0f, dz / len);
}

//--- 体の位置。走動=玩家の足元 / 工位=その工位の既定カメラの真下。
XMFLOAT3 SceneForge::BodyPosition()
{
	XMFLOAT3 body = m_player.GetPosition();
	if (m_walkMode) return body;
	XMFLOAT3 eye, target; StationBodyPose(eye, target);
	body.x = eye.x; body.z = eye.z;
	return body;
}

//--- 最短回転(from → to): 回転軸 = 外積、角度 = 内積の acos。真逆なら from に直交する軸で半回転。
XMMATRIX RotationFromTo(FXMVECTOR from, FXMVECTOR to)
{
	XMVECTOR f = XMVector3Normalize(from), t = XMVector3Normalize(to);
	XMVECTOR cr = XMVector3Cross(f, t);
	float dot = XMVectorGetX(XMVector3Dot(f, t));
	dot = dot > 1.0f ? 1.0f : (dot < -1.0f ? -1.0f : dot);
	const float PARALLEL_EPS = 1e-10f;
	if (XMVectorGetX(XMVector3LengthSq(cr)) > PARALLEL_EPS) return XMMatrixRotationAxis(cr, acosf(dot));
	if (dot > 0.0f) return XMMatrixIdentity();
	// 真逆: f に直交する軸(f が X 寄りなら Y、そうでなければ X との外積)で半回転
	XMVECTOR helper = fabsf(XMVectorGetX(f)) < 0.9f ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 1, 0, 0);
	return XMMatrixRotationAxis(XMVector3Cross(f, helper), XM_PI);
}

//--- ビューモデルを置く基底(位置=カメラ、向き=少し遅れて追う前方 m_vmFwd)。
//    位置はカメラそのもの(遅らせると手が体から離れて見える)、向きだけ遅らせる=武器の揺れ(weapon sway)。
bool SceneForge::CameraBasis(XMVECTOR& eye, XMVECTOR& fwd, XMVECTOR& right, XMVECTOR& up)
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return false;
	XMFLOAT3 e = cam->GetPos();
	eye   = XMLoadFloat3(&e);
	fwd   = XMVector3Normalize(XMLoadFloat3(&m_vmFwd));
	right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd));	// 左手系: up × fwd = 右
	up    = XMVector3Cross(fwd, right);
	return true;
}

//--- 武器の揺れ: ビューモデルの前方 m_vmFwd を、カメラの前方へ Damp で追わせる(フレームレート非依存)。
//    視点を振ると数フレーム遅れて付いて来て、止めると追い付く。持っていない時は常にカメラに一致させておく
//    (掴んだ瞬間に横から振り込んで来ない様に)。
void SceneForge::UpdateViewmodelSway(float tick)
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	XMFLOAT3 e = cam->GetPos(), l = cam->GetLook();
	XMFLOAT3 camFwd; XMStoreFloat3(&camFwd, XMVector3Normalize(XMLoadFloat3(&l) - XMLoadFloat3(&e)));
	if (!m_carrying || !m_vmFwdInit) { m_vmFwd = camFwd; m_vmFwdInit = true; return; }
	const XMFLOAT3 damped = Lerp::Damp(m_vmFwd, camFwd, m_vmSwayLambda, tick);
	XMStoreFloat3(&m_vmFwd, XMVector3Normalize(XMLoadFloat3(&damped)));	// 長さ1に戻す(成分ごとの補間で短くなる分)
}

//--- 手に持った鉄と火钳を最後に描く。深度範囲を [0, VIEWMODEL_DEPTH_RANGE] に詰める=深度値が常に場面より
//    小さくなる → 床/壁にめり込んでも手前に見える(depth range hack)。場面の深度バッファは消さないので、
//    この後の火花なども普通に前後判定される。
void SceneForge::DrawViewmodel()
{
	if (!m_carrying || m_state != GAME_PLAY) return;
	if (SequencePlaying()) return;	// 置く拍子表の再生中: 鉄は Draw の通常パス、火钳は DrawCarry が描く
	ID3D11DeviceContext* ctx = GetContext();
	UINT n = 1; D3D11_VIEWPORT old; ctx->RSGetViewports(&n, &old);
	D3D11_VIEWPORT vp = old;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = VIEWMODEL_DEPTH_RANGE;
	ctx->RSSetViewports(1, &vp);

	if (m_wpOk) DrawWeapon(); else Draw3DBillet();	// 鉄(位置/向きは WorkAnchor→HeldPoint)

	// 火钳: 画面の下の外(手元 m_tongsBase)から伸びて、口が挟む点(HeldGrip)に届く。口の向き = 根元 → 挟む点。
	XMVECTOR eye, fwd, right, up;
	if (Model* tongs = GetObj<Model>("StPliers"); tongs && CameraBasis(eye, fwd, right, up))
	{
		// 柄の根元も同じだけ揺らす=火钳と鉄がひと塊のまま揺れる(根元だけ止めると火钳が首を振って見える)
		XMVECTOR base = eye + right * m_tongsBase[0] + up * m_tongsBase[1] + fwd * m_tongsBase[2] + ViewmodelBob(right, up);
		const XMFLOAT3 grip = HeldGrip();
		XMFLOAT3 dir; XMStoreFloat3(&dir, XMLoadFloat3(&grip) - base);
		DrawModelWorld(tongs, TongsWorld(dir, m_heldDir, grip));	// 鉄(m_heldDir)が口の輪を通る
	}
	ctx->RSSetViewports(1, &old);	// 深度範囲を元に戻す
}

//--- 歩きの揺れ(view bobbing)を進める。walked = このフレームに歩いた水平距離。
//    位相を距離で進める=速さに比例して揺れが速くなり、止まれば位相も止まる(時間で進めると足踏みでも揺れる)。
void SceneForge::UpdateCarryBob(float tick, float walked)
{
	const bool moving = m_carrying && walked > BOB_MOVE_EPS;
	if (moving) m_bobPhase += walked * m_bobPerMeter;
	m_bobWeight = Lerp::Damp(m_bobWeight, moving ? 1.0f : 0.0f, BOB_FADE_LAMBDA, tick);	// 歩き出し/止まりは滑らかに
}

//--- 今の揺れ(カメラの右/上方向のずれ)。上下 = 1歩に1回(sin の絶対値で「着地で下がる」形)、
//    左右 = その半分の周波数(右足/左足で振れる)。合わせて小さな∞字。
XMVECTOR SceneForge::ViewmodelBob(FXMVECTOR right, FXMVECTOR up) const
{
	const float vert = -fabsf(sinf(m_bobPhase * XM_PI));			// 0 → 下がる → 0(1歩ごとに着地で沈む)
	const float side =  sinf(m_bobPhase * XM_PI * 0.5f);			// 2歩で1往復
	return (up * (vert * m_bobAmp) + right * (side * m_bobSideAmp)) * m_bobWeight;
}

//--- 火钳が鉄を挟む点 = カメラの前(右/上/前へ m_gripOff) + 歩きの揺れ。カメラ基準=どこを見ても画面の同じ所。
XMFLOAT3 SceneForge::HeldGrip()
{
	XMVECTOR eye, fwd, right, up;
	if (!CameraBasis(eye, fwd, right, up)) return m_barAnchor;
	// 道具/壁に近い時は鉄の水平方向の逆(手元側)へ引き寄せる(UpdateCarryAvoid)
	const XMVECTOR pull = XMVectorSet(m_carryAvoidDir.x, 0.0f, m_carryAvoidDir.y, 0.0f) * -m_carryPull;
	XMFLOAT3 g; XMStoreFloat3(&g, eye + right * m_gripOff[0] + up * m_gripOff[1] + fwd * m_gripOff[2] + ViewmodelBob(right, up) + pull);
	return g;
}

//--- 手に持った鉄の中心(と長軸の向き m_heldDir)。
//    向き: 視線から右へ m_carryYaw、上へ m_carryPitch 振った方向(カメラ基準)=画面の左下から右奥へ斜めに伸びる。
//    位置: 挟む点は鉄の手前の端から m_gripAlong 割の所 → 中心 = 挟む点 + 向き × 全長 × (0.5 − m_gripAlong)。
XMFLOAT3 SceneForge::HeldPoint()
{
	XMVECTOR eye, fwd, right, up;
	if (!CameraBasis(eye, fwd, right, up)) return m_barAnchor;
	const float pitch = m_carryPitch + m_carryRaise;	// 道具/壁に近い時は起こす(UpdateCarryAvoid)
	const float cp = cosf(pitch);
	XMVECTOR dir = XMVector3Normalize(fwd * (cosf(m_carryYaw) * cp) + right * (sinf(m_carryYaw) * cp) + up * sinf(pitch));
	XMStoreFloat3(&m_heldDir, dir);

	const XMFLOAT3 g = HeldGrip();
	const float len = m_barLen * m_wpScale;			// 鉄の全長(world)
	XMFLOAT3 out;
	XMStoreFloat3(&out, XMLoadFloat3(&g) + dir * (len * (0.5f - m_gripAlong)));
	return out;
}

//--- 起こし角 raise・引き寄せ 0 の時、鉄の先端(挟む点から遠い方の端)が、目から視線の水平方向 dirH へ何 m 先に届くか。
//    HeldPoint と同じ式で先端を求め、目からの差を dirH へ射影する。
//    (引き寄せは挟む点を -dirH へ動かすだけ=届く距離がちょうどその分だけ減る。だから引き寄せ 0 で測れば足りる)
float SceneForge::HeldReach(float raise, FXMVECTOR eye, const XMFLOAT2& dirH)
{
	const float keepRaise = m_carryRaise, keepPull = m_carryPull;
	m_carryRaise = raise; m_carryPull = 0.0f;
	const XMFLOAT3 c = HeldPoint();		// 中心(m_heldDir も更新)
	m_carryRaise = keepRaise; m_carryPull = keepPull;
	const float half = m_barLen * m_wpScale * 0.5f;
	XMFLOAT3 tip; XMStoreFloat3(&tip, XMLoadFloat3(&c) + XMLoadFloat3(&m_heldDir) * half);
	XMFLOAT3 e; XMStoreFloat3(&e, eye);
	return (tip.x - e.x) * dirH.x + (tip.z - e.z) * dirH.y;
}

//--- 手の鉄のめり込み回避。先端が「道具/壁までの距離 − 隙間」より先へ出ない様にする。
//    はみ出す量 excess を ①引き寄せで消す(上限 m_carryAvoidMaxPull。届く距離は引き寄せ量だけ線形に減る=引き算で済む)
//    ②残りを起こし角で消す(届く距離は起こすほど短くなる=単調なので、二分探索で解ける。
//      式を解かなくても、カメラの傾きや m_carryYaw があっても正しい)。
void SceneForge::UpdateCarryAvoid(float tick)
{
	float targetPull = 0.0f, targetRaise = 0.0f;
	XMVECTOR eye, fwd, right, up;
	if (m_carrying && CameraBasis(eye, fwd, right, up))
	{
		// 鉄の水平の向き(起こす前)。起こしても水平の向きはほぼ変わらない=レイはこの向きに1本だけ
		HeldReach(0.0f, eye, XMFLOAT2(0.0f, 1.0f));
		XMFLOAT2 dirH(m_heldDir.x, m_heldDir.z);
		const float lenH = sqrtf(dirH.x * dirH.x + dirH.y * dirH.y);
		const float HORIZONTAL_EPS = 1e-4f;
		if (lenH > HORIZONTAL_EPS)
		{
			dirH.x /= lenH; dirH.y /= lenH;
			XMFLOAT3 e; XMStoreFloat3(&e, eye);
			const float reach0 = HeldReach(0.0f, eye, dirH);
			const float free = Collision2D::RayCast(m_collision, XMFLOAT2(e.x, e.z), dirH, reach0 + m_carryAvoidMargin);
			const float limit = free - m_carryAvoidMargin;
			m_carryAvoidOrigin = XMFLOAT2(e.x, e.z); m_carryAvoidDir = dirH; m_carryAvoidFree = free;

			const float excess = reach0 - limit;				// 先端がはみ出す量(m)
			if (excess > 0.0f)
			{
				// ① 引き寄せ
				targetPull = fminf(excess, m_carryAvoidMaxPull);
				// ② 引き寄せで足りない分 → 起こし角。引き寄せた分だけ許される届く距離が延びる
				if (excess > m_carryAvoidMaxPull)
				{
					const float limitAfterPull = limit + m_carryAvoidMaxPull;
					float lo = 0.0f, hi = m_carryAvoidMaxRaise;
					if (HeldReach(hi, eye, dirH) > limitAfterPull) lo = hi;	// 上限まで起こしても当たる=上限で諦める
					else
						for (int i = 0; i < CARRY_AVOID_ITERATIONS; ++i)
						{
							const float mid = 0.5f * (lo + hi);
							if (HeldReach(mid, eye, dirH) > limitAfterPull) lo = mid; else hi = mid;
						}
					targetRaise = (lo == m_carryAvoidMaxRaise) ? lo : hi;
				}
			}
		}
		HeldPoint();	// m_heldDir を今の起こし角の値へ戻す(上の探索で書き換えた)
	}
	m_carryPull  = Lerp::Damp(m_carryPull,  targetPull,  m_carryAvoidLambda, tick);
	m_carryRaise = Lerp::Damp(m_carryRaise, targetRaise, m_carryAvoidLambda, tick);
}

//--- 左腰の点。体の位置(走動=玩家の足元 / 工位=カメラの真下)から、左・床からの高さ・前へ m_hipOff。
XMFLOAT3 SceneForge::HipPoint()
{
	XMFLOAT3 f = BodyForward();
	XMFLOAT3 r(f.z, 0.0f, -f.x);	// 右(前方を右へ90度)
	const XMFLOAT3 body = BodyPosition();
	return XMFLOAT3(body.x - r.x * m_hipOff[0] + f.x * m_hipOff[2],
	                m_walkFloorY + m_hipOff[1],
	                body.z - r.z * m_hipOff[0] + f.z * m_hipOff[2]);
}

//--- 火钳モデルの形を読む(Init で1回)。向きを手で合わせなくて済む様に、全部モデルから求める。
//    この火钳(やっとこ)は「鋲 + 2本の腕」の3部品。2本の腕は鋲で交差し、口の側は外へ膨らんで輪を作る。
//    鉄はその輪を、開閉の平面に垂直に(=薄い軸の向きに)通る=現実の挟み方。
void SceneForge::InitTongsGeometry()
{
	m_tongsGeomOk = false;
	Prop* pl = GetProp("StPliers");
	Model* m = GetObj<Model>("StPliers");
	if (!pl || !m) return;

	// 軸: 最長 = 火钳の軸 / 2番目 = 開閉の平面(腕が開く向き) / 最短 = 薄い軸(輪を鉄が通る向き)
	const float ext[3] = { pl->aabbMax.x - pl->aabbMin.x, pl->aabbMax.y - pl->aabbMin.y, pl->aabbMax.z - pl->aabbMin.z };
	int order[3] = { 0, 1, 2 };
	for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) if (ext[order[j]] > ext[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
	m_tongsLongAx = order[0];
	m_tongsThinAx = order[2];

	// 鋲 = 一番小さい部品(繋がった三角形の塊)。その中心の長軸座標が「要(かなめ)」。
	std::vector<XMFLOAT3> tris;
	m->AppendLocalTriangles(tris);
	std::vector<std::vector<XMFLOAT3>> pieces = Collision2D::SplitConnected(tris);
	if (pieces.size() < 2) return;	// 1部品しか無い=鋲が分からない(違うモデル)
	size_t smallest = 0;
	for (size_t i = 1; i < pieces.size(); ++i) if (pieces[i].size() < pieces[smallest].size()) smallest = i;
	float pivot = 0.0f;
	for (const XMFLOAT3& v : pieces[smallest]) pivot += (&v.x)[m_tongsLongAx];
	pivot /= (float)pieces[smallest].size();

	// 口 = 長軸の両端のうち、鋲に近い方(やっとこは要から口までが短く、柄が長い)
	const float lo = (&pl->aabbMin.x)[m_tongsLongAx], hi = (&pl->aabbMax.x)[m_tongsLongAx];
	m_tongsJawSign = (pivot - lo < hi - pivot) ? -1.0f : 1.0f;
	const float tip = (m_tongsJawSign < 0.0f) ? lo : hi;

	// 挟む点 = 口の先端と鋲の間(輪の中心)。他の2軸は箱の中心。
	XMFLOAT3 c((pl->aabbMin.x + pl->aabbMax.x) * 0.5f, (pl->aabbMin.y + pl->aabbMax.y) * 0.5f, (pl->aabbMin.z + pl->aabbMax.z) * 0.5f);
	(&c.x)[m_tongsLongAx] = tip + (pivot - tip) * TONGS_JAW_CENTER_FRAC;
	m_tongsGripLocal = c;
	m_tongsGeomOk = true;
}

//--- 火钳のワールド行列。2つの向きを同時に合わせる(1つの向きだけだと長軸まわりの回転が決まらない):
//      ① 柄→口 の向き(長軸)を approach へ
//      ② 薄い軸(輪を鉄が通る向き)を barDir へ(approach に直交する成分)
//    → 鉄が口の輪をちょうど通る。挟む点(輪の中心)を gripAt に置く。
//    実装: モデル側とワールド側でそれぞれ正規直交基底(3本の直交する単位ベクトル)を作り、
//          「モデルの基底 → ワールドの基底」へ写す回転行列 = 転置(モデル基底) × ワールド基底。
XMMATRIX SceneForge::TongsWorld(const XMFLOAT3& approach, const XMFLOAT3& barDir, const XMFLOAT3& gripAt)
{
	Prop* pl = GetProp("StPliers");
	if (!pl || !m_tongsGeomOk) return XMMatrixIdentity();

	// モデル側の基底: u = 柄→口(長軸), v = 薄い軸, w = u×v
	auto unit = [](int axis, float s) { return XMVectorSet(axis == 0 ? s : 0.0f, axis == 1 ? s : 0.0f, axis == 2 ? s : 0.0f, 0.0f); };
	XMVECTOR u = unit(m_tongsLongAx, m_tongsJawSign);
	XMVECTOR v = unit(m_tongsThinAx, 1.0f);
	XMVECTOR w = XMVector3Cross(u, v);

	// ワールド側の基底: U = approach, V = barDir から U 成分を除いた向き(グラム・シュミット), W = U×V
	XMVECTOR U = XMVector3Normalize(XMLoadFloat3(&approach));
	XMVECTOR B = XMLoadFloat3(&barDir);
	XMVECTOR V = B - U * XMVector3Dot(B, U);
	const float PARALLEL_EPS = 1e-6f;	// 鉄が火钳の軸と平行=輪の向きが決まらない→適当な直交軸で代用
	if (XMVectorGetX(XMVector3LengthSq(V)) < PARALLEL_EPS)
		V = XMVector3Cross(U, fabsf(XMVectorGetY(U)) < 0.9f ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0));
	V = XMVector3Normalize(V);
	XMVECTOR W = XMVector3Cross(U, V);

	// 行ベクトル規約: ローカル座標 p → (p·u, p·v, p·w) が基底での成分 → それを U,V,W で組み直す
	XMMATRIX local; local.r[0] = u; local.r[1] = v; local.r[2] = w; local.r[3] = XMVectorSet(0, 0, 0, 1);
	XMMATRIX world; world.r[0] = U; world.r[1] = V; world.r[2] = W; world.r[3] = XMVectorSet(0, 0, 0, 1);
	const XMMATRIX rot = XMMatrixTranspose(local) * world;

	const float s = pl->scale * m_tongsScale;
	const XMFLOAT3& g = m_tongsGripLocal;
	return XMMatrixTranslation(-g.x, -g.y, -g.z) *		// 挟む点を原点へ
	       XMMatrixScaling(s, s, s) *
	       rot *
	       XMMatrixTranslation(gripAt.x, gripAt.y, gripAt.z);
}

void SceneForge::GripIron()
{
	m_carrying = true;	// 以後 WorkAnchor() は手の前の点を返す=鉄は手に付いて来る。火の外なので熱は入らない。
}

void SceneForge::PutIronAt(Station s)
{
	m_workAt   = s;
	m_carrying = false;
}

//--- 鉄の無い工位で E を押した: 入らず、鉄の在り処を言う(KCD式の負向フィードバック。自動では動かない)。
void SceneForge::SayWhereIronIs()
{
	const char* line = "";
	switch (m_workAt)
	{
	case Station::Anvil:      line = (const char*)u8"鉄は金床の上だ";   break;
	case Station::Hearth:     line = (const char*)u8"鉄は炉の中だ";     break;
	case Station::Grindstone: line = (const char*)u8"鉄は砥石の所だ";   break;
	case Station::Trough:     line = (const char*)u8"鉄は水槽の所だ";   break;
	}
	Say(line, IM_COL32(255, 200, 90, 255));
}

//--- 火钳を描く。運んでいる時=手(鉄の手前の端を咥える) / そうでない時=左腰(口を下に)。
//    工位では金床だけ腰の火钳を出す(翻面で腰から抜く運鏡が見える)。翻面で手に取っている間は描かない。
void SceneForge::DrawCarry()
{
	if (m_state != GAME_PLAY) return;
	Model* tongs = GetObj<Model>("StPliers");
	if (!tongs) return;

	if (SequencePlaying())	// 拍子表の再生中: 補間中の火钳の姿勢で描く(Sequence.cpp)
	{
		DrawModelWorld(tongs, TongsWorld(m_seqTongs.approach, m_seqTongs.barDir, m_seqTongs.grip));
		return;
	}
	if (m_carrying) return;	// 手の火钳は DrawViewmodel が最後に描く(めり込み防止)
	if (HoldingAtTrough())
	{
		// 水槽: 火钳で挟んだまま構える/淬火する。鉄の今の姿勢(立てる・沈める・揺する)から挟む姿勢を求める=口が鉄から離れない。
		// 柄の向きだけは手元(カメラの下)からでなく、「口から上へ・手前へ」立ち上がる向きにする:
		//   鉄は槽の中にあるので、手元から伸ばすと柄が槽の手前の壁を突き抜ける(2026-10-06 F5)。実際も柄は水から斜め上へ出て縁を越える。
		TongsPose t = TongsOnIron(WeaponWorld());
		const XMFLOAT3 handle(t.grip.x + m_stationViewDir.x * TROUGH_TONGS_OUT, t.grip.y + TROUGH_TONGS_RISE,
		                      t.grip.z + m_stationViewDir.z * TROUGH_TONGS_OUT);
		XMStoreFloat3(&t.approach, XMVector3Normalize(XMLoadFloat3(&t.grip) - XMLoadFloat3(&handle)));	// 柄 → 口
		DrawModelWorld(tongs, TongsWorld(t.approach, t.barDir, t.grip));
		return;
	}
	const bool atAnvil = !m_walkMode && !Transitioning() && m_station == Station::Anvil;
	if ((m_walkMode || atAnvil) && !m_tongsInHand)
	{
		// 口を下にして腰に下げる。腰(ベルト)には柄の中ほどが掛かる=口の輪はそこから半分の長さ下。
		// 扁平な面は体の外側を向ける(薄い軸=輪の通る向きを体の左右へ)。
		XMFLOAT3 grip = HipPoint();
		if (Prop* pl = GetProp("StPliers"))
		{
			const float len = (&pl->aabbMax.x)[m_tongsLongAx] - (&pl->aabbMin.x)[m_tongsLongAx];
			grip.y -= len * 0.5f * pl->scale * m_tongsScale;
		}
		const XMFLOAT3 f = BodyForward();
		DrawModelWorld(tongs, TongsWorld(XMFLOAT3(0.0f, -1.0f, 0.0f), XMFLOAT3(f.z, 0.0f, -f.x), grip));
	}
}

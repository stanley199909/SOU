// Part of SceneForge: 拍子表(スクリプトシーケンス)の再生器。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// 目的(ユーザー指定 2026-10-02): 動作の過渡は「視線が対象へ向く → その物で起きる事を最後まで演じる → 次の対象へ向く」。
//   瞬間的に手に現れる/置かれるのは不可。
// 仕組み: 1つの動作 = 拍(ビート)の表(データ)。拍 = カメラの注視先 + 火钳の行き先 + 鉄の行き先 + 秒数。
//   再生器は1つだけ: 拍の開始時の姿勢を記録し、拍の目標へイージングで補間する。
//   目標は毎フレーム解決する(手元の位置はカメラ基準なので、カメラが回れば目標も動く)。
//   新しい動作を足す = 表を1つ書くだけ(再生器は変えない)= データ駆動。
// 姿勢の補間:
//   カメラ = 視線ベクトルの正規化線形補間(nlerp) → yaw/pitch に戻して走動カメラへ渡す(終わった時に操作へ自然に戻る)。
//   火钳   = 挟む点は線形補間、2本の向きは nlerp(TongsWorld がそこから正規直交基底を組み直す)。
//   鉄     = ワールド行列を 位置/回転(クォータニオン)/拡縮 に分解し、回転は球面線形補間(Slerp)。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Model.h"
#include "AimSystem.h"
#include "Lerp.h"
#include <cmath>

using namespace DirectX;

//====================================================================
//  拍子表(データ)
//====================================================================

// 鉄を掴む: 腰に目を落とす → 火钳を抜いて手元へ → 鉄へ目を向けて火钳を伸ばす → 挟む間 → 持ち上げて前を向く。
//   秒数はユーザー要望の「重さのある、ゆっくりした」テンポ。合わなければここだけ直す。
const SceneForge::SeqBeat SceneForge::GRIP_IRON_BEATS[] = {
	//  注視先       火钳           鉄           秒
	{ SeqLook::Hip,  SeqTongs::Hip,  SeqIron::Rest, 0.7f },	// ① 左腰の火钳に目を落とす
	{ SeqLook::Hip,  SeqTongs::Held, SeqIron::Rest, 0.8f },	// ② 火钳を腰から抜いて手元へ構える
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest, 1.1f },	// ③ 鉄へ目を向け、火钳を伸ばす
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest, 0.4f },	// ④ 挟む(止めの間=重さ)
	{ SeqLook::Home, SeqTongs::Held, SeqIron::Held, 1.0f },	// ⑤ 持ち上げ、元の方向へ向き直る
};
const SceneForge::Sequence SceneForge::SEQ_GRIP_IRON = { GRIP_IRON_BEATS, _countof(GRIP_IRON_BEATS), SceneForge::SeqEnd::GripIron };

//====================================================================
//  補間の小道具
//====================================================================
namespace {
	// イーズインアウト(smoothstep): 動き出しと止まり際がゆっくり=手で物を扱う重さ。
	float EaseInOut(float t) { return t * t * (3.0f - 2.0f * t); }

	XMFLOAT3 LerpVec(const XMFLOAT3& a, const XMFLOAT3& b, float t)
	{
		return XMFLOAT3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
	}

	// 向きの補間(nlerp): 線形補間して長さ1に戻す。向きが真逆の時は途中で長さ0になり得るので、その時は to を返す。
	XMFLOAT3 NlerpDir(const XMFLOAT3& a, const XMFLOAT3& b, float t)
	{
		XMVECTOR v = XMVectorLerp(XMLoadFloat3(&a), XMLoadFloat3(&b), t);
		const float ZERO_EPS = 1e-8f;
		if (XMVectorGetX(XMVector3LengthSq(v)) < ZERO_EPS) return b;
		XMFLOAT3 o; XMStoreFloat3(&o, XMVector3Normalize(v)); return o;
	}

	XMFLOAT3 Normalized(const XMFLOAT3& v)
	{
		XMFLOAT3 o; XMStoreFloat3(&o, XMVector3Normalize(XMLoadFloat3(&v))); return o;
	}
}

//====================================================================
//  再生
//====================================================================
void SceneForge::PlaySequence(const Sequence& seq)
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!m_wpOk || !cam)
	{
		// 武器モデルが無い(旧メッシュ)時は演出できない → 結果だけ即実行
		if (seq.onEnd == SeqEnd::GripIron) GripIron();
		return;
	}
	XMFLOAT3 e = cam->GetPos(), l = cam->GetLook();
	m_seqHomeFwd = Normalized(XMFLOAT3(l.x - e.x, l.y - e.y, l.z - e.z));
	m_seqBodyFwd = BodyForward();		// 体の向きを固定(m_seq を立てる前に読む=今の玩家の向き)

	// 手に持った時の鉄の向き: 火钳が掴む端(玩家に近い端)が手元側になる様に、必要なら前後を入れ替える
	// (入れ替えないと、持ち上げる途中で鉄が半回転してしまう)。置いてある鉄の長軸の両端のうち近い方を調べる。
	{
		const XMMATRIX rest = SeqIronWorld(SeqIron::Rest);
		const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
		XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
		XMFLOAT3 plus = c; (&plus.x)[la] = (&m_wpMax.x)[la];
		XMFLOAT3 minus = c; (&minus.x)[la] = (&m_wpMin.x)[la];
		XMVECTOR pPlus  = XMVector3TransformCoord(XMLoadFloat3(&plus), rest);
		XMVECTOR pMinus = XMVector3TransformCoord(XMLoadFloat3(&minus), rest);
		XMVECTOR eye = XMLoadFloat3(&e);
		m_heldFlip = XMVectorGetX(XMVector3LengthSq(pPlus - eye)) < XMVectorGetX(XMVector3LengthSq(pMinus - eye));
	}

	m_seq      = &seq;
	m_seqBeat  = 0;
	m_seqTimer = 0.0f;
	m_tongsInHand = true;				// 腰の火钳は描かない(拍子表の火钳を描く)
	m_seqTongs = SeqTongsPose(SeqTongs::Hip);	// 最初は腰に下がっている
	BeginSeqBeat();
}

void SceneForge::StopSequence()
{
	m_seq = nullptr;
	m_seqIronOverride = false;
	m_tongsInHand = false;
}

//--- 拍の開始姿勢を記録(視線/火钳/鉄)。補間はここから拍の目標へ向かう。
void SceneForge::BeginSeqBeat()
{
	if (CameraBase* cam = GetObj<CameraBase>("Camera"))
	{
		XMFLOAT3 e = cam->GetPos(), l = cam->GetLook();
		m_seqFromFwd = Normalized(XMFLOAT3(l.x - e.x, l.y - e.y, l.z - e.z));
	}
	m_seqFromTongs = m_seqTongs;
	XMStoreFloat4x4(&m_seqFromIron, m_seqIronOverride ? XMLoadFloat4x4(&m_seqIronWorld) : SeqIronWorld(SeqIron::Rest));
}

void SceneForge::UpdateSequence(float tick)
{
	if (!m_seq) return;
	const SeqBeat& b = m_seq->beats[m_seqBeat];
	m_seqTimer += tick;
	const float t = (b.seconds > 0.0f) ? fminf(m_seqTimer / b.seconds, 1.0f) : 1.0f;
	const float e = EaseInOut(t);

	// --- カメラ: 視線を補間し、走動カメラの yaw/pitch に戻す(ApplyWalkCamera がそれで描く) ---
	{
		const XMFLOAT3 foot = m_player.GetPosition();
		const XMFLOAT3 eye(foot.x, foot.y + m_walkEyeH, foot.z);
		XMFLOAT3 to = m_seqHomeFwd;
		if (b.look != SeqLook::Home)
		{
			const XMFLOAT3 p = SeqLookTarget(b.look);
			to = Normalized(XMFLOAT3(p.x - eye.x, p.y - eye.y, p.z - eye.z));
		}
		const XMFLOAT3 d = NlerpDir(m_seqFromFwd, to, e);
		m_player.SetYaw(atan2f(d.x, d.z));
		m_walkPitch = asinf(d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y));
	}

	// --- 火钳: 挟む点は線形、向きは nlerp。輪を通る向きは符号が反対だと途中で潰れるので揃える ---
	{
		const TongsPose to = SeqTongsPose(b.tongs);
		XMFLOAT3 fromBar = m_seqFromTongs.barDir;
		if (fromBar.x * to.barDir.x + fromBar.y * to.barDir.y + fromBar.z * to.barDir.z < 0.0f)
			fromBar = XMFLOAT3(-fromBar.x, -fromBar.y, -fromBar.z);	// 輪の向きは ± どちらでも同じ見た目
		m_seqTongs.grip     = LerpVec(m_seqFromTongs.grip, to.grip, e);
		m_seqTongs.approach = NlerpDir(m_seqFromTongs.approach, to.approach, e);
		m_seqTongs.barDir   = NlerpDir(fromBar, to.barDir, e);
	}

	// --- 鉄: 行列を分解して 位置=線形 / 回転=Slerp / 拡縮=線形 で補間 ---
	{
		const XMMATRIX to = SeqIronWorld(b.iron);
		XMVECTOR s0, r0, p0, s1, r1, p1;
		XMMatrixDecompose(&s0, &r0, &p0, XMLoadFloat4x4(&m_seqFromIron));
		XMMatrixDecompose(&s1, &r1, &p1, to);
		const XMVECTOR s = XMVectorLerp(s0, s1, e);
		const XMVECTOR r = XMQuaternionSlerp(r0, r1, e);
		const XMVECTOR p = XMVectorLerp(p0, p1, e);
		const XMMATRIX rot = XMMatrixRotationQuaternion(r);
		XMStoreFloat4x4(&m_seqIronRot, rot);
		XMStoreFloat4x4(&m_seqIronWorld, XMMatrixScalingFromVector(s) * rot * XMMatrixTranslationFromVector(p));
		m_seqIronOverride = true;
	}

	if (t < 1.0f) return;

	// --- 拍の終わり: 次の拍へ / 全部終わったら行為を実行 ---
	m_seqTimer = 0.0f;
	if (++m_seqBeat < m_seq->count) { BeginSeqBeat(); return; }

	const SeqEnd onEnd = m_seq->onEnd;
	StopSequence();
	switch (onEnd)
	{
	case SeqEnd::GripIron:
		GripIron();				// 以後はビューモデル(DrawViewmodel)が手の鉄と火钳を描く。最後の拍の姿勢と同じ計算=継ぎ目無し
		m_vmFwdInit = false;	// 武器の揺れの追従をカメラの今の向きから始める
		break;
	}
}

//====================================================================
//  拍の目標(すべて今の配置/カメラから毎フレーム求める=座標のベタ書き無し)
//====================================================================
XMFLOAT3 SceneForge::SeqLookTarget(SeqLook l)
{
	switch (l)
	{
	case SeqLook::Hip:
		return HipPoint();
	case SeqLook::Iron:
	{
		// 置いてある鉄の中心 = モデル箱の中心をワールドへ(WeaponWorld は箱の中心を原点へ寄せてから置く)
		XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
		XMStoreFloat3(&c, XMVector3TransformCoord(XMLoadFloat3(&c), SeqIronWorld(SeqIron::Rest)));
		return c;
	}
	default:
		return XMFLOAT3(0, 0, 0);	// Home は方向で扱う(UpdateSequence)
	}
}

//--- 火钳の行き先の姿勢。腰/手元は既存の描画(DrawCarry/DrawViewmodel)と同じ式=拍子表の前後で継ぎ目が出ない。
SceneForge::TongsPose SceneForge::SeqTongsPose(SeqTongs t)
{
	TongsPose o = {};
	XMVECTOR eye, fwd, right, up;
	const bool haveCam = CameraBasis(eye, fwd, right, up);
	switch (t)
	{
	case SeqTongs::Hip:
	{
		// DrawCarry と同じ: 口を下に、柄の中ほどがベルトに掛かる。扁平な面は体の外側
		o.grip = HipPoint();
		if (Prop* pl = GetProp("StPliers"))
		{
			const float len = (&pl->aabbMax.x)[m_tongsLongAx] - (&pl->aabbMin.x)[m_tongsLongAx];
			o.grip.y -= len * 0.5f * pl->scale * m_tongsScale;
		}
		const XMFLOAT3 f = BodyForward();
		o.approach = XMFLOAT3(0.0f, -1.0f, 0.0f);
		o.barDir   = XMFLOAT3(f.z, 0.0f, -f.x);
		break;
	}
	case SeqTongs::Held:
	{
		// DrawViewmodel と同じ: 手元(m_tongsBase)から、手に持った鉄を挟む点(HeldGrip)へ
		const bool was = m_carrying;
		m_carrying = true; HeldPoint(); m_carrying = was;	// m_heldDir(手に持った時の鉄の向き)を更新
		o.grip   = HeldGrip();
		o.barDir = m_heldDir;
		if (haveCam)
		{
			XMVECTOR base = eye + right * m_tongsBase[0] + up * m_tongsBase[1] + fwd * m_tongsBase[2];
			XMStoreFloat3(&o.approach, XMVector3Normalize(XMLoadFloat3(&o.grip) - base));
		}
		break;
	}
	case SeqTongs::Iron:
	{
		// 置いてある鉄の、玩家に近い端から m_gripAlong の所を挟む(手に持った時と同じ割合=持ち上げても滑らない)
		const XMMATRIX rest = SeqIronWorld(SeqIron::Rest);
		const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
		XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
		XMFLOAT3 plus = c;  (&plus.x)[la]  = (&m_wpMax.x)[la];
		XMFLOAT3 minus = c; (&minus.x)[la] = (&m_wpMin.x)[la];
		XMVECTOR nearEnd = XMVector3TransformCoord(XMLoadFloat3(m_heldFlip ? &plus : &minus), rest);
		XMVECTOR farEnd  = XMVector3TransformCoord(XMLoadFloat3(m_heldFlip ? &minus : &plus), rest);
		XMStoreFloat3(&o.grip, nearEnd + (farEnd - nearEnd) * m_gripAlong);
		XMStoreFloat3(&o.barDir, XMVector3Normalize(farEnd - nearEnd));
		if (haveCam)
		{
			XMVECTOR base = eye + right * m_tongsBase[0] + up * m_tongsBase[1] + fwd * m_tongsBase[2];	// 手元から伸ばす
			XMStoreFloat3(&o.approach, XMVector3Normalize(XMLoadFloat3(&o.grip) - base));
		}
		break;
	}
	}
	return o;
}

//--- 鉄の行き先のワールド行列。拍子表の上書き(m_seqIronOverride)を一時的に外し、
//    既存の配置計算(WeaponWorld: 置いてある=WorkAnchor / 手に持った=HeldPoint)をそのまま使う。
XMMATRIX SceneForge::SeqIronWorld(SeqIron i)
{
	const bool wasOverride = m_seqIronOverride, wasCarrying = m_carrying;
	m_seqIronOverride = false;
	m_carrying = (i == SeqIron::Held);
	const XMMATRIX w = WeaponWorld();
	m_carrying = wasCarrying;
	m_seqIronOverride = wasOverride;
	return w;
}

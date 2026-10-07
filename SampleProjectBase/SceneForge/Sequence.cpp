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

// 腰は見ない(ユーザー判断 2026-10-04): ポケットの物を取る時に人は下を向かない。火钳は視線と同時に腰から出て来る。
//   秒数はユーザー要望の「重さのある、ゆっくりした」テンポ。合わなければここだけ直す。

// 鉄を掴む: 鉄へ目を向けつつ火钳を抜く → 火钳を伸ばす → 挟む間 → 持ち上げて前を向く。
//   曲線: 伸ばす/置く = Out(速く出て、当たる所でゆっくり=手が狙って止める) / それ以外 = InOut。
const SceneForge::SeqBeat SceneForge::GRIP_IRON_BEATS[] = {
	//  注視先       火钳            鉄            秒     手の曲線
	{ SeqLook::Iron, SeqTongs::Held, SeqIron::Rest, 0.8f, SeqEase::InOut },	// ① 鉄へ目を向けつつ、火钳を腰から抜いて手元へ構える
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest, 1.0f, SeqEase::Out   },	// ② 火钳を伸ばす
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest, 0.4f, SeqEase::InOut },	// ③ 挟む(止めの間=重さ)
	{ SeqLook::Home, SeqTongs::Held, SeqIron::Held, 1.0f, SeqEase::InOut },	// ④ 持ち上げ、元の方向へ向き直る
};
//                                                 表                 拍数                       終わりの行為               中継点(使わない)
const SceneForge::Sequence SceneForge::SEQ_GRIP_IRON = { GRIP_IRON_BEATS, _countof(GRIP_IRON_BEATS), SceneForge::SeqEnd::GripIron, 0.0f, 0.0f, false };

// 鉄を置く: 掴むの逆。置き場所へ目を向ける → 中継点へ運ぶ → 置く → 火钳を開く間 → 火钳を腰へ戻す(腰は見ない)。
//   手から置き場所へ直に補間すると、炉の壁/フードを突き抜ける。手前の中継点を通す=「置く」動きにもなる。
const SceneForge::SeqBeat SceneForge::PUT_IRON_BEATS[] = {
	//  注視先       火钳            鉄                秒     手の曲線
	{ SeqLook::Iron, SeqTongs::Held, SeqIron::Held,     0.8f, SeqEase::InOut },	// ① 鉄を持ったまま、置き場所へ目を向ける
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Approach, 0.9f, SeqEase::InOut },	// ② 置き場所の手前(上)の中継点へ運ぶ
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest,     0.8f, SeqEase::Out   },	// ③ 置く(炉は斜めに差し込む)
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest,     0.4f, SeqEase::InOut },	// ④ 火钳を開く(止めの間=重さ)
	{ SeqLook::Iron, SeqTongs::Hip,  SeqIron::Rest,     0.7f, SeqEase::InOut },	// ⑤ 火钳を腰へ戻す。終わったら工位へ入る
};
// 工位ごとの違いは中継点だけ(同じ表を共有)。                                           中継点: 戻す(m) 上へ(m) 鉄の長軸に沿って戻す
const SceneForge::Sequence SceneForge::SEQ_PUT_HEARTH = { PUT_IRON_BEATS, _countof(PUT_IRON_BEATS), SceneForge::SeqEnd::PutIron, 0.50f, 0.00f, true  };	// 炉: 斜めの鉄を長軸のまま差し込む
const SceneForge::Sequence SceneForge::SEQ_PUT_ANVIL  = { PUT_IRON_BEATS, _countof(PUT_IRON_BEATS), SceneForge::SeqEnd::PutIron, 0.10f, 0.30f, false };	// 金床: 真上から下ろす
const SceneForge::Sequence SceneForge::SEQ_PUT_GRIND  = { PUT_IRON_BEATS, _countof(PUT_IRON_BEATS), SceneForge::SeqEnd::PutIron, 0.15f, 0.25f, false };	// 砥石: 砥輪の上から当てる

// 水槽: 淬火は火钳で持ったまま行う → 離さない(火钳を開く間 ④ と腰へ戻す ⑤ が無い)。
//   最後の姿勢は「挟んだまま水面の上に構える」= 工位に入った後も火钳は鉄を挟み続ける(HoldingAtTrough)。
const SceneForge::SeqBeat SceneForge::HOLD_IRON_BEATS[] = {
	//  注視先       火钳            鉄                秒     手の曲線
	{ SeqLook::Iron, SeqTongs::Held, SeqIron::Held,     0.8f, SeqEase::InOut },	// ① 鉄を持ったまま、水面へ目を向ける
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Approach, 0.9f, SeqEase::InOut },	// ② 水面の上の中継点へ運ぶ
	{ SeqLook::Iron, SeqTongs::Iron, SeqIron::Rest,     0.8f, SeqEase::Out   },	// ③ 構える位置へ下ろす。終わったら工位へ入る
};
const SceneForge::Sequence SceneForge::SEQ_PUT_TROUGH = { HOLD_IRON_BEATS, _countof(HOLD_IRON_BEATS), SceneForge::SeqEnd::PutIron, 0.10f, 0.30f, false };

const SceneForge::Sequence& SceneForge::PutIronSequence(Station s)
{
	switch (s)
	{
	case Station::Hearth:     return SEQ_PUT_HEARTH;
	case Station::Grindstone: return SEQ_PUT_GRIND;
	case Station::Trough:     return SEQ_PUT_TROUGH;
	default:                  return SEQ_PUT_ANVIL;
	}
}

//====================================================================
//  補間の小道具
//====================================================================
namespace {
	// イーズインアウト(smoothstep): 動き出しと止まり際がゆっくり=手で物を扱う重さ。
	float EaseInOut(float t) { return t * t * (3.0f - 2.0f * t); }
	// イーズアウト(3次): 速く動き出し、着く所でゆっくり止まる=狙った所へ手を伸ばす・そっと置く。
	float EaseOutCubic(float t) { const float u = 1.0f - t; return 1.0f - u * u * u; }

	// 2点の水平距離(弧の高さを決める。上下だけの移動=真上から下ろす時は弧を付けない)
	float HorizontalDist(FXMVECTOR a, FXMVECTOR b)
	{
		const XMVECTOR d = (b - a) * XMVectorSet(1.0f, 0.0f, 1.0f, 0.0f);
		return XMVectorGetX(XMVector3Length(d));
	}

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

	float DistSq(FXMVECTOR p, const XMFLOAT3& q)
	{
		return XMVectorGetX(XMVector3LengthSq(p - XMLoadFloat3(&q)));
	}
}

//--- 鉄の長軸の両端をワールドへ。gripped = 火钳が掴む端(m_heldFlip で決まる) / other = 反対の端。
void SceneForge::IronEnds(FXMMATRIX world, XMVECTOR& gripped, XMVECTOR& other) const
{
	const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
	XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
	XMFLOAT3 plus = c;  (&plus.x)[la]  = (&m_wpMax.x)[la];
	XMFLOAT3 minus = c; (&minus.x)[la] = (&m_wpMin.x)[la];
	gripped = XMVector3TransformCoord(XMLoadFloat3(m_heldFlip ? &plus : &minus), world);
	other   = XMVector3TransformCoord(XMLoadFloat3(m_heldFlip ? &minus : &plus), world);
}

//====================================================================
//  再生
//====================================================================
//--- 拍子表が終わった時の行為(演出できない時は再生せずにこれだけ実行する)
void SceneForge::RunSeqEnd(SeqEnd onEnd, Station target)
{
	switch (onEnd)
	{
	case SeqEnd::GripIron:
		GripIron();				// 以後はビューモデル(DrawViewmodel)が手の鉄と火钳を描く。最後の拍の姿勢と同じ計算=継ぎ目無し
		m_vmFwdInit = false;	// 武器の揺れの追従をカメラの今の向きから始める
		break;
	case SeqEnd::PutIron:
		PutIronAt(target);			// 置いた所 = 最後の拍の鉄の姿勢と同じ計算=継ぎ目無し
		BeginEnterStation(target);	// 今の視線(置き場所を見ている)から工位の視点へ過渡
		break;
	}
}

void SceneForge::PlaySequence(const Sequence& seq, Station target)
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!m_wpOk || !cam)
	{
		// 武器モデルが無い(旧メッシュ)時は演出できない → 結果だけ即実行
		RunSeqEnd(seq.onEnd, target);
		return;
	}
	XMFLOAT3 e = cam->GetPos(), l = cam->GetLook();
	m_seqHomeFwd = Normalized(XMFLOAT3(l.x - e.x, l.y - e.y, l.z - e.z));
	m_seqBodyFwd = BodyForward();		// 体の向きを固定(m_seq を立てる前に読む=今の玩家の向き)
	m_seqStation = target;
	// 置き先の向き(StationRight)は工位の視点方向で決まる → 先に決めておく(入る時にも同じ値になる)
	if (seq.onEnd == SeqEnd::PutIron)
	{
		SetupStationView(target);
		// 置く向き: 火钳は掴んだ端を離さない → 置いた時も掴んだ端が玩家側でないと、空中で鉄が半回転してしまう
		// (2026-10-04 F5: 金床の北側から置くと発生)。反転無しで置いた時、掴んだ端の方が遠ければ半回転して置く。
		m_restFlip = false;
		XMVECTOR gripped, other; IronEnds(SeqIronWorld(SeqIron::Rest), gripped, other);
		m_restFlip = DistSq(gripped, e) > DistSq(other, e);
	}

	// 手に持った時の鉄の向き: 火钳が掴む端(玩家に近い端)が手元側になる様に、必要なら前後を入れ替える
	// (入れ替えないと、持ち上げる途中で鉄が半回転してしまう)。置いてある鉄の長軸の両端のうち近い方を調べる。
	// 既に手に持っている(置く時)は掴んだ端を変えない=置いても同じ所を挟んだまま。
	if (!m_carrying)
	{
		m_heldFlip = false;
		XMVECTOR gripped, other; IronEnds(SeqIronWorld(SeqIron::Rest), gripped, other);
		m_heldFlip = DistSq(other, e) < DistSq(gripped, e);	// 反転無しの「掴む端」より反対の端が近い → そっちを掴む
	}

	m_seq      = &seq;
	m_seqBeat  = 0;
	m_seqTimer = 0.0f;
	m_seqPrevGrip = m_carrying;			// 手に持っていれば最初から火钳は鉄を挟んでいる
	m_tongsInHand = true;				// 腰の火钳は描かない(拍子表の火钳を描く)
	// 火钳の最初の姿勢: 手に持っている=手元で鉄を挟んでいる / そうでない=腰に下がっている
	m_seqTongs = m_carrying ? SeqTongsPose(SeqTongs::Held, SeqIron::Held) : SeqTongsPose(SeqTongs::Hip, SeqIron::Rest);
	BeginSeqBeat();
}

void SceneForge::StopSequence()
{
	m_seq = nullptr;
	m_seqIronOverride = false;
	m_tongsInHand = false;
}

//--- 拍の開始姿勢を記録(視線/火钳/鉄)。補間はここから拍の目標へ向かう。
//    再生ごとの揺らぎ(長さ/弧の高さ)もここで決める=拍の途中で値が変わらない。
void SceneForge::BeginSeqBeat()
{
	const SeqBeat& b = m_seq->beats[m_seqBeat];
	if (CameraBase* cam = GetObj<CameraBase>("Camera"))
	{
		XMFLOAT3 e = cam->GetPos(), l = cam->GetLook();
		m_seqFromFwd = Normalized(XMFLOAT3(l.x - e.x, l.y - e.y, l.z - e.z));
	}
	m_seqFromTongs = m_seqTongs;
	// 鉄の開始姿勢 = 今描いている姿勢(最初の拍は上書き前の WeaponWorld = 置いてある所 or 手の中)
	XMStoreFloat4x4(&m_seqFromIron, m_seqIronOverride ? XMLoadFloat4x4(&m_seqIronWorld) : WeaponWorld());

	m_seqBeatDur = b.seconds * (1.0f + frand(-m_seqTimeJitter, m_seqTimeJitter));
	m_seqArcK    = m_seqArcLift * (1.0f + frand(-m_seqArcJitter, m_seqArcJitter));
	// 前の拍の終わりに挟んでいて、この拍も腰へ戻さない → 火钳は鉄に付いたまま動く
	m_seqAttached = m_seqPrevGrip && b.tongs != SeqTongs::Hip;
	// この拍の終わりに挟んでいるか: 鉄を挟みに行った / 手に持った鉄を手元で挟んでいる
	m_seqPrevGrip = (b.tongs == SeqTongs::Iron) || (b.tongs == SeqTongs::Held && b.iron == SeqIron::Held);
}

void SceneForge::UpdateSequence(float tick)
{
	if (!m_seq) return;
	const SeqBeat& b = m_seq->beats[m_seqBeat];
	m_seqTimer += tick;
	const float t = (m_seqBeatDur > 0.0f) ? fminf(m_seqTimer / m_seqBeatDur, 1.0f) : 1.0f;

	// フォロースルーとオーバーラップ: 目が先に動き出して先に着き、手(火钳/鉄)は遅れて動き出し拍の終わりに着く。
	const float MAX_LAG = 0.9f;	// 遅れが拍の全部になると手が動く時間が無くなる
	const float lag   = fminf(fmaxf(m_seqHandLag, 0.0f), MAX_LAG);
	const float tEye  = fminf(t / (1.0f - lag), 1.0f);
	const float tHand = fmaxf((t - lag) / (1.0f - lag), 0.0f);
	const float eEye  = EaseInOut(tEye);
	const float eHand = (b.ease == SeqEase::Out) ? EaseOutCubic(tHand) : EaseInOut(tHand);
	const float arc   = 4.0f * eHand * (1.0f - eHand);	// 弧の持ち上げ: 始めと終わりで 0、真ん中で 1

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
		const XMFLOAT3 d = NlerpDir(m_seqFromFwd, to, eEye);
		m_player.SetYaw(atan2f(d.x, d.z));
		m_walkPitch = asinf(d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y));
	}

	// --- 鉄: 行列を分解して 位置=線形+弧 / 回転=Slerp / 拡縮=線形 で補間 ---
	//     弧は宙を運ぶ時だけ(置く所=Rest へ向かう拍はまっすぐ。炉に差し込む/金床に下ろす途中で持ち上がると道具に当たる)。
	{
		const XMMATRIX to = SeqIronWorld(b.iron);
		XMVECTOR s0, r0, p0, s1, r1, p1;
		XMMatrixDecompose(&s0, &r0, &p0, XMLoadFloat4x4(&m_seqFromIron));
		XMMatrixDecompose(&s1, &r1, &p1, to);
		const XMVECTOR s = XMVectorLerp(s0, s1, eHand);
		const XMVECTOR r = XMQuaternionSlerp(r0, r1, eHand);
		XMVECTOR p = XMVectorLerp(p0, p1, eHand);
		if (b.iron != SeqIron::Rest) p += XMVectorSet(0.0f, arc * m_seqArcK * HorizontalDist(p0, p1), 0.0f, 0.0f);
		const XMMATRIX rot = XMMatrixRotationQuaternion(r);
		XMStoreFloat4x4(&m_seqIronRot, rot);
		XMStoreFloat4x4(&m_seqIronWorld, XMMatrixScalingFromVector(s) * rot * XMMatrixTranslationFromVector(p));
		m_seqIronOverride = true;
	}

	// --- 火钳 ---
	if (m_seqAttached)
	{
		// 鉄を挟んだまま: 今の鉄の姿勢から挟む姿勢を求める(火钳と鉄を別々に補間すると、弧や回転でずれて口が鉄から離れる)
		m_seqTongs = TongsOnIron(XMLoadFloat4x4(&m_seqIronWorld));
	}
	else
	{
		// 火钳だけが動く: 挟む点は線形+弧、向きは nlerp。輪を通る向きは符号が反対だと途中で潰れるので揃える。
		// 弧は宙を動く時だけ(鉄を挟みに行く拍はまっすぐ狙う)。
		const TongsPose to = SeqTongsPose(b.tongs, b.iron);
		XMFLOAT3 fromBar = m_seqFromTongs.barDir;
		if (fromBar.x * to.barDir.x + fromBar.y * to.barDir.y + fromBar.z * to.barDir.z < 0.0f)
			fromBar = XMFLOAT3(-fromBar.x, -fromBar.y, -fromBar.z);	// 輪の向きは ± どちらでも同じ見た目
		m_seqTongs.grip = LerpVec(m_seqFromTongs.grip, to.grip, eHand);
		if (b.tongs != SeqTongs::Iron)
			m_seqTongs.grip.y += arc * m_seqArcK * HorizontalDist(XMLoadFloat3(&m_seqFromTongs.grip), XMLoadFloat3(&to.grip));
		m_seqTongs.approach = NlerpDir(m_seqFromTongs.approach, to.approach, eHand);
		m_seqTongs.barDir   = NlerpDir(fromBar, to.barDir, eHand);
	}

	if (t < 1.0f) return;

	// --- 拍の終わり: 次の拍へ / 全部終わったら行為を実行 ---
	m_seqTimer = 0.0f;
	if (++m_seqBeat < m_seq->count) { BeginSeqBeat(); return; }

	const SeqEnd onEnd = m_seq->onEnd;
	StopSequence();
	RunSeqEnd(onEnd, m_seqStation);
}

//--- 再生中だけ走動カメラに手持ちの呼吸を乗せる。重みは指数減衰で追従(フレームレート非依存)=始まり/終わりで視点が跳ばない。
void SceneForge::UpdateSeqBreath(float tick)
{
	const float target = SequencePlaying() ? 1.0f : 0.0f;
	m_seqBreathW += (target - m_seqBreathW) * (1.0f - expf(-tick / SEQ_BREATH_FADE_TIME));
}

//====================================================================
//  拍の目標(すべて今の配置/カメラから毎フレーム求める=座標のベタ書き無し)
//====================================================================
XMFLOAT3 SceneForge::SeqLookTarget(SeqLook l)
{
	switch (l)
	{
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
SceneForge::TongsPose SceneForge::SeqTongsPose(SeqTongs t, SeqIron iron)
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
		// その拍の鉄の行き先(置いた所/中継点)を挟む
		o = TongsOnIron(SeqIronWorld(iron));
		break;
	}
	return o;
}

//--- その姿勢の鉄を、掴んだ端から m_gripAlong の所で挟む火钳
//    (手に持った時と同じ割合=持ち上げても置いても挟む所が滑らない)。柄は手元(m_tongsBase)から伸びる。
SceneForge::TongsPose SceneForge::TongsOnIron(FXMMATRIX ironWorld)
{
	TongsPose o = {};
	XMVECTOR nearEnd, farEnd; IronEnds(ironWorld, nearEnd, farEnd);
	XMStoreFloat3(&o.grip, nearEnd + (farEnd - nearEnd) * m_gripAlong);
	XMStoreFloat3(&o.barDir, XMVector3Normalize(farEnd - nearEnd));
	XMVECTOR eye, fwd, right, up;
	if (CameraBasis(eye, fwd, right, up))
	{
		XMVECTOR base = eye + right * m_tongsBase[0] + up * m_tongsBase[1] + fwd * m_tongsBase[2];
		XMStoreFloat3(&o.approach, XMVector3Normalize(XMLoadFloat3(&o.grip) - base));
	}
	return o;
}

//--- 鉄の行き先のワールド行列。拍子表の上書き(m_seqIronOverride)を一時的に外し、
//    既存の配置計算(WeaponWorld: 置いてある=WorkAnchor / 手に持った=HeldPoint)をそのまま使う。
//    置いた所 = 目標工位 m_seqStation に置いたと仮定して計算(置く時は置き先、掴む時は今ある工位)。
//    中継点   = 置いた所を、玩家の方へ水平に(または鉄の長軸に沿って)approachBack・上へ approachUp ずらした所(向きは置いた所と同じ)。
XMMATRIX SceneForge::SeqIronWorld(SeqIron i)
{
	const bool wasOverride = m_seqIronOverride, wasCarrying = m_carrying;
	const Station wasAt = m_workAt;
	m_seqIronOverride = false;
	m_carrying = (i == SeqIron::Held);
	if (i != SeqIron::Held) m_workAt = m_seqStation;
	XMMATRIX w = WeaponWorld();
	m_workAt = wasAt;
	m_carrying = wasCarrying;
	m_seqIronOverride = wasOverride;

	if (i == SeqIron::Approach && m_seq)
	{
		XMFLOAT3 at; XMStoreFloat3(&at, w.r[3]);		// 置いた所の位置(行列の平行移動成分)
		const XMFLOAT3 foot = m_player.GetPosition();
		XMVECTOR back = XMVectorSet(foot.x - at.x, 0.0f, foot.z - at.z, 0.0f);
		if (m_seq->approachAlongIron)
		{
			// 鉄の長軸に沿って、掴んだ端(=玩家側。m_restFlip で揃えてある)の方へ戻す=斜めの鉄をそのまま差し込む動き
			XMVECTOR gripped, other; IronEnds(w, gripped, other);
			back = gripped - other;
		}
		const float ZERO_EPS = 1e-8f;
		back = (XMVectorGetX(XMVector3LengthSq(back)) > ZERO_EPS) ? XMVector3Normalize(back) : XMVectorZero();
		const XMVECTOR off = back * m_seq->approachBack + XMVectorSet(0.0f, m_seq->approachUp, 0.0f, 0.0f);
		w = w * XMMatrixTranslationFromVector(off);
	}
	return w;
}

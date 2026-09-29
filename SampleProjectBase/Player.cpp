#include "Player.h"
#include "Input.h"
#include <math.h>

using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
	// 互動キー: 可互動状態のとき、これを押すと工程状態への遷移を要求する。
	// KCDのF互動に倣う。既存の Q=淬火 / R=加熱 / 左クリック=打撃 と衝突しない様に E を選択(調整可)。
	constexpr BYTE INTERACT_KEY = 'E';

	// プレイヤ当たり判定ボックスの基準サイズ(単位)。人一人ぶんの占有(幅0.6/高1.7/奥0.6)。
	const XMFLOAT3 PLAYER_BOX_SIZE = XMFLOAT3(0.6f, 1.7f, 0.6f);

	// 押し出しの最大繰り返し回数。道具と道具の隙間(角)で「Aから出したらBに入った」を解くため。
	// 普段は1回で抜ける(=途中で break)。角でも数回で収まる。
	constexpr int PUSH_ITERATIONS = 4;
}

Player::Player()
{
	// 当たり判定ボックス(見た目モデルは別途Scene側が描画。色は使わない)
	m_box = Box(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), PLAYER_BOX_SIZE);
	m_radius = PLAYER_BOX_SIZE.x * 0.5f;	// 足元の円=人の幅の半分(上から見た胴体)
}

void Player::Init(DirectX::XMFLOAT3 pos, float yaw)
{
	m_box = Box(pos, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), PLAYER_BOX_SIZE);
	m_yaw = yaw;
}

void Player::Update(float tick, const Collision2D::World& world)
{
	Move(tick);					// ① まず入力どおりに動く(めり込んでもよい)
	ResolveCollision(world);	// ② めり込んだ分だけ道具/壁の外へ押し出す(入力が無くても毎フレーム=
								//    工位から出た直後の立ち位置が道具に重なっていても抜ける)
}

void Player::Move(float tick)
{
	// --- 入力から移動方向を求める(一人称の前後左右) ---
	// f=前後(W/S), s=左右ストレイフ(A/D)。向き m_yaw を基準にするので、
	// 相機/マウスで向きを変えれば「見ている方向へ歩く」になる(向きの接続はStep2)。
	float f = 0.0f;
	float s = 0.0f;
	if (IsKeyPress('W')) f += 1.0f;	// 前
	if (IsKeyPress('S')) f -= 1.0f;	// 後
	if (IsKeyPress('D')) s += 1.0f;	// 右
	if (IsKeyPress('A')) s -= 1.0f;	// 左

	if (f == 0.0f && s == 0.0f) return;	// 移動入力なし

	// m_yaw から前方ベクトルと右方ベクトルを作る
	XMFLOAT3 fwd   = GetForward();					// (sin,0,cos)
	XMFLOAT3 right = XMFLOAT3(fwd.z, 0.0f, -fwd.x);	// 前方を右へ90度回した向き

	// 前後・左右を合成 → 正規化 → 速度×時間
	Vector3 v = Vector3(fwd) * f + Vector3(right) * s;
	if (v.LengthSquared() <= 0.0f) return;
	v.Normalize();
	XMFLOAT3 vel = v * m_moveSpeed * tick;

	m_box.m_pos.x += vel.x;
	m_box.m_pos.z += vel.z;
	m_box.UpdateMinMaxPos();
}

void Player::ResolveCollision(const Collision2D::World& world)
{
	// 足元を上から見た「円」として、重なった凸包(道具)と壁線(建物)の外へ押し出す(Physics/Collision2D)。
	// 押し出すと移動のうち「壁へ向かう成分」だけが消え、壁に沿う成分は残る=壁に沿って滑る。
	// 2つの物の隙間(角)では、片方から押し出すともう片方へ入ることがあるので数回繰り返す。
	XMFLOAT2 c(m_box.m_pos.x, m_box.m_pos.z);
	for (int it = 0; it < PUSH_ITERATIONS; ++it)
	{
		bool pushed = false;
		for (const Collision2D::Hull& h : world.hulls)
			pushed |= Collision2D::PushCircleOut(c, m_radius, h);
		for (const Collision2D::Segment& s : world.segments)
			pushed |= Collision2D::PushCircleOut(c, m_radius, s);
		if (!pushed) break;		// もうどこにも重なっていない
	}
	m_box.m_pos.x = c.x;
	m_box.m_pos.z = c.y;
	m_box.UpdateMinMaxPos();
}

DirectX::XMFLOAT3 Player::GetPosition() const
{
	return m_box.m_pos;
}

float Player::GetYaw() const
{
	return m_yaw;
}

DirectX::XMFLOAT3 Player::GetForward() const
{
	// yaw=0 で +Z を向く。GetPosition基準の前方(XZ平面)。
	return XMFLOAT3(sinf(m_yaw), 0.0f, cosf(m_yaw));
}

Box& Player::GetBox()
{
	return m_box;
}

bool Player::WantInteract() const
{
	// 可互動 かつ 互動キーが「押された瞬間」(edge)。長押しで連発しない様 IsKeyTrigger を使う。
	return m_canInteract && IsKeyTrigger(INTERACT_KEY);
}

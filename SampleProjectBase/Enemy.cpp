#include "Enemy.h"
#include "math.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

Enemy::Enemy()
{
	// 当たり判定ボックス(見た目は牛モデルなので色は使わない)
	m_box = Box(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f));
}

void Enemy::Init(DirectX::XMFLOAT3 pos)
{
	m_box = Box(pos, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f));
}

void Enemy::Update(float tick, DirectX::XMFLOAT3 target, std::vector<Box>& walls)
{
	// --- ① プレイヤ方向を毎フレーム計算して追尾する ---
	Vector3 dir = Vector3(target) - Vector3(m_box.GetPosition());
	dir.y = 0.0f;	// 高さは無視して平面上で追う

	// プレイヤの方へ牛の向きを合わせる(停止中も向きは合わせる)
	if (dir.LengthSquared() > 0.0001f)
	{
		m_angleY = atan2f(dir.x, dir.z);
	}

	// 一定距離まで近づいたら停止する
	// ※毎フレーム追いかけると目標を通り過ぎて前後に振動するため
	if (dir.Length() <= m_stopDist) return;

	dir.Normalize();
	XMFLOAT3 vel = dir * m_speed * tick;

	// --- ②③ 壁と当たり判定しつつ移動。当たっても方向は毎回計算し直すので追尾は継続 ---
	MoveWithWall(vel, walls);
}

void Enemy::MoveWithWall(DirectX::XMFLOAT3 vel, std::vector<Box>& walls)
{
	// X軸方向に移動して判定 → ぶつかったら戻す(Z方向へは動けるので壁沿いに回り込む)
	m_box.m_pos.x += vel.x;
	m_box.UpdateMinMaxPos();
	for (Box& w : walls)
	{
		if (m_box.HitAABB(w))
		{
			m_box.m_pos.x -= vel.x;
			m_box.UpdateMinMaxPos();
			break;
		}
	}

	// Z軸方向に移動して判定 → ぶつかったら戻す
	m_box.m_pos.z += vel.z;
	m_box.UpdateMinMaxPos();
	for (Box& w : walls)
	{
		if (m_box.HitAABB(w))
		{
			m_box.m_pos.z -= vel.z;
			m_box.UpdateMinMaxPos();
			break;
		}
	}
}

DirectX::XMFLOAT3 Enemy::GetPosition()
{
	return m_box.GetPosition();
}

float Enemy::GetAngleY()
{
	return m_angleY;
}

float Enemy::GetDrawScale()
{
	return m_drawScale;
}

Box& Enemy::GetBox()
{
	return m_box;
}

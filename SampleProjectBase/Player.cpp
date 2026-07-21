#include "Player.h"
#include "Input.h"
#include "math.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

Player::Player()
{
	// 当たり判定ボックス(見た目は牛モデルなので色は使わない)
	m_box = Box(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f));
}

void Player::Init(DirectX::XMFLOAT3 pos)
{
	m_box = Box(pos, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 1.0f, 1.0f));
}

void Player::Update(float tick, std::vector<Box>& walls)
{
	// カメラ操作中(CTRL/ALT/SHIFT/右クリック)はプレイヤのWASDを無効化する
	// ※カメラもWASDで移動するため、一緒に動いてしまうのを防ぐ
	if (IsKeyPress(VK_CONTROL) || IsKeyPress(VK_MENU) ||
		IsKeyPress(VK_SHIFT) || IsKeyPress(VK_RBUTTON))
	{
		return;
	}

	// --- 入力から移動方向を求める(WASD) ---
	XMFLOAT3 dir = XMFLOAT3(0.0f, 0.0f, 0.0f);
	if (IsKeyPress('W')) dir.z += 1.0f;	// 奥
	if (IsKeyPress('S')) dir.z -= 1.0f;	// 手前
	if (IsKeyPress('A')) dir.x -= 1.0f;	// 左
	if (IsKeyPress('D')) dir.x += 1.0f;	// 右

	// 移動が無ければ何もしない
	Vector3 v = dir;
	if (v.LengthSquared() <= 0.0f) return;

	// 進行方向へ牛の向きを合わせる
	m_angleY = atan2f(dir.x, dir.z);

	// 正規化して速度・時間を掛ける
	v.Normalize();
	XMFLOAT3 vel = v * m_speed * tick;

	// 壁と衝突しないように移動(④ 壁に当たって止まる)
	MoveWithWall(vel, walls);
}

void Player::MoveWithWall(DirectX::XMFLOAT3 vel, std::vector<Box>& walls)
{
	// X軸方向に移動して判定 → ぶつかったら戻す
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

DirectX::XMFLOAT3 Player::GetPosition()
{
	return m_box.GetPosition();
}

float Player::GetAngleY()
{
	return m_angleY;
}

float Player::GetDrawScale()
{
	return m_drawScale;
}

Box& Player::GetBox()
{
	return m_box;
}

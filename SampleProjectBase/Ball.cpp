
#include <DirectXMath.h>

#include "Ball.h"
#include "math.h"
#include "Geometory.h"
#include "DebugLog.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

Ball::Ball()
{
	m_pos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_basePos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z)));
}

Ball::Ball(DirectX::XMFLOAT3 pos)
{
	m_pos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_basePos = pos;
	m_color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z)));
	m_mat = m_mat * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
}

Ball::Ball(DirectX::XMFLOAT3 pos, float scale)
{
	m_pos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_basePos = pos;
	m_color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_scale = DirectX::XMFLOAT3(scale, scale, scale);
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z)));
	m_mat = m_mat * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
}

void Ball::InitPositon(DirectX::XMFLOAT3 pos)
{
	m_basePos = pos;
	m_mat = m_mat * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
}

void Ball::AddPos(DirectX::XMFLOAT3 pos)
{
	m_pos = m_pos + pos;
}

void Ball::SetColor(DirectX::XMFLOAT4 color)
{
	m_color = color;
}

void Ball::SetSpeed(float speed)
{
	m_speed = speed;
}

void Ball::SetAcceleration(float acceleration)
{
	m_acceleration = acceleration;
}

void Ball::Update(float tick)
{
	//m_time += tick;
}

void Ball::Drow()
{
	// 球の座標行列作成
	DirectX::XMFLOAT4X4 mat;
	DirectX::XMStoreFloat4x4(&mat, DirectX::XMMatrixTranspose(
		DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z) *	// 球のスケール
		DirectX::XMMatrixTranslation(m_pos.x + m_basePos.x, m_pos.y + m_basePos.y, m_pos.z + m_basePos.z)	// 球の座標
	));

	Geometory::SetWorld(mat);		// 行列セット
	Geometory::SetColor(m_color);	// 球の色設定 RGBA
	Geometory::DrawSphere();		// 球描画
}

bool Ball::HitSphere(Ball b)
{
	if (isHit || b.isHit) return false;
	DirectX::XMFLOAT3 pos1 = GetPosition();
	DirectX::XMFLOAT3 pos2 = b.GetPosition();

	float outp = (pos2.x - pos1.x) * (pos2.x - pos1.x) +
				 (pos2.y - pos1.y) * (pos2.y - pos1.y) +
				 (pos2.z - pos1.z) * (pos2.z - pos1.z);

	float r1 = GetRadius();
	float r2 = b.GetRadius();
	float outr = (r1 + r2)* (r1 + r2);
	bool hit = outp <= outr;
	if (hit){
		isHit = true;
		b.isHit = true;
	}
	return hit;
}

float Ball::GetRadius()
{
	float scale = (m_scale.x + m_scale.y + m_scale.z) / 3;
	return m_baseRadius * scale;
}

DirectX::XMFLOAT3 Ball::GetPosition()
{
	return m_pos + m_basePos;
}
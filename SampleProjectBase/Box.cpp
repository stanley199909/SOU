
#include <DirectXMath.h>

#include "Box.h"
#include "math.h"
#include "Geometory.h"
#include "DebugLog.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

Box::Box()
{
	m_pos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	m_angle = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z)));
	UpdateMinMaxPos();
}

Box::Box(DirectX::XMFLOAT3 pos)
{
	m_pos = pos;
	m_color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	m_angle = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z)));
	m_mat = m_mat * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
	UpdateMinMaxPos();
}

Box::Box(DirectX::XMFLOAT3 pos, DirectX::XMFLOAT4 color, DirectX::XMFLOAT3 scale)
{
	m_pos = pos;
	m_color = color;
	m_scale = scale;
	m_angle = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z)));
	m_mat = m_mat * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
	UpdateMinMaxPos();
}
void Box::InitPositon(DirectX::XMFLOAT3 pos)
{
	m_mat = m_mat * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
}

void Box::SetColor(DirectX::XMFLOAT4 color)
{
	m_color = color;
}

void Box::Update(float tick){}

void Box::Drow()
{
	// 球の座標行列作成
	DirectX::XMStoreFloat4x4(&m_mat_r, 
		DirectX::XMMatrixRotationX(m_angle.x) *
		DirectX::XMMatrixRotationY(m_angle.y) *
		DirectX::XMMatrixRotationZ(m_angle.z));
	DirectX::XMStoreFloat4x4(&m_mat_s, DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z));
	DirectX::XMStoreFloat4x4(&m_mat, DirectX::XMMatrixTranspose(
		m_mat_s * m_mat_r * DirectX::XMMatrixTranslation(m_pos.x, m_pos.y, m_pos.z)	// 球の座標
	));
	UpdateMinMaxPos();
	Geometory::SetWorld(m_mat);		// 行列セット
	Geometory::SetColor(m_color);	// 球の色設定 RGBA
	Geometory::DrawBox();			// 箱描画
}

void Box::UpdateMinMaxPos() {
	max_x = m_pos.x + (m_scale.x * 0.5f);
	min_x = m_pos.x - (m_scale.x * 0.5f);
	max_y = m_pos.y + (m_scale.y * 0.5f);
	min_y = m_pos.y - (m_scale.y * 0.5f);
	max_z = m_pos.z + (m_scale.z * 0.5f);
	min_z = m_pos.z - (m_scale.z * 0.5f);
}

bool Box::HitSphere(Box b)
{
	if (isHit || b.isHit) return false;
	DirectX::XMFLOAT3 pos1 = GetPosition();
	DirectX::XMFLOAT3 pos2 = b.GetPosition();

	float outp = (pos2.x - pos1.x) * (pos2.x - pos1.x) +
				 (pos2.y - pos1.y) * (pos2.y - pos1.y) +
				 (pos2.z - pos1.z) * (pos2.z - pos1.z);

	float r1 = GetRadius();
	float r2 = b.GetRadius();
	float outr = (r1 + r2) * (r1 + r2);
	bool hit = outp <= outr;
	if (hit){
		isHit = true;
		b.isHit = true;
	}
	return hit;
}

bool Box::HitAABB(Box b)
{
	// 課題範囲-------------------------------------------------------------
	if (max_x < b.min_x || b.max_x < min_x) return false;
	if (max_y < b.min_y || b.max_y < min_y) return false;
	if (max_z < b.min_z || b.max_z < min_z) return false;
	return true;
	// 課題範囲-------------------------------------------------------------
}

bool Box::HitOBB(Box b)
{
	// 課題範囲-------------------------------------------------------------
	
// 各方向ベクトルの確保
	DirectX::XMFLOAT3 NAe1 = GetXaxis(), Ae1 = NAe1 * GetScaleX() * 0.5f;
	DirectX::XMFLOAT3 NAe2 = GetYaxis(), Ae2 = NAe2 * GetScaleY() * 0.5f;
	DirectX::XMFLOAT3 NAe3 = GetZaxis(), Ae3 = NAe3 * GetScaleZ() * 0.5f;
	DirectX::XMFLOAT3 NBe1 = b.GetXaxis(), Be1 = NBe1 * b.GetScaleX() * 0.5f;
	DirectX::XMFLOAT3 NBe2 = b.GetYaxis(), Be2 = NBe2 * b.GetScaleY() * 0.5f;
	DirectX::XMFLOAT3 NBe3 = b.GetZaxis(), Be3 = NBe3 * b.GetScaleZ() * 0.5f;
	DirectX::XMFLOAT3 Interval = GetPosition() - b.GetPosition();

	// 分離軸 : Ae1
	FLOAT rA = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&Ae1)));
	FLOAT rB = LenSegOnSeparateAxis(&NAe1, &Be1, &Be2, &Be3);
	FLOAT L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), DirectX::XMLoadFloat3(&NAe1))));
	if (L > rA + rB)
		return false; // 衝突していない

	// 分離軸 : Ae2
	rA = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&Ae2)));
	rB = LenSegOnSeparateAxis(&NAe2, &Be1, &Be2, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), DirectX::XMLoadFloat3(&NAe2))));
	if (L > rA + rB)
		return false; // 衝突していない

	// 分離軸 : Ae3
	rA = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&Ae3)));
	rB = LenSegOnSeparateAxis(&NAe3, &Be1, &Be2, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), DirectX::XMLoadFloat3(&NAe3))));
	if (L > rA + rB)
		return false; // 衝突していない

	// 分離軸 : Be1
	rA = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&Be1)));
	rB = LenSegOnSeparateAxis(&NBe1, &Ae1, &Ae2, &Ae3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), DirectX::XMLoadFloat3(&NBe1))));
	if (L > rA + rB)
		return false; // 衝突していない

	// 分離軸 : Be2
	rA = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&Be2)));
	rB = LenSegOnSeparateAxis(&NBe2, &Ae1, &Ae2, &Ae3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), DirectX::XMLoadFloat3(&NBe2))));
	if (L > rA + rB)
		return false; // 衝突していない

	// 分離軸 : Be3
	rA = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&Be3)));
	rB = LenSegOnSeparateAxis(&NBe3, &Ae1, &Ae2, &Ae3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), DirectX::XMLoadFloat3(&NBe3))));
	if (L > rA + rB)
		return false; // 衝突していない

	// 分離軸 : C11
	DirectX::XMVECTOR Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe1), DirectX::XMLoadFloat3(&NBe1));
	DirectX::XMFLOAT3 cro;
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae2, &Ae3);
	rB = LenSegOnSeparateAxis(&cro, &Be2, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C12
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe1), DirectX::XMLoadFloat3(&NBe2));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae2, &Ae3);
	rB = LenSegOnSeparateAxis(&cro, &Be1, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C13
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe1), DirectX::XMLoadFloat3(&NBe3));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae2, &Ae3);
	rB = LenSegOnSeparateAxis(&cro, &Be1, &Be2);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C21
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe2), DirectX::XMLoadFloat3(&NBe1));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae1, &Ae3);
	rB = LenSegOnSeparateAxis(&cro, &Be2, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C22
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe2), DirectX::XMLoadFloat3(&NBe2));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae1, &Ae3);
	rB = LenSegOnSeparateAxis(&cro, &Be1, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C23
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe2), DirectX::XMLoadFloat3(&NBe3));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae1, &Ae3);
	rB = LenSegOnSeparateAxis(&cro, &Be1, &Be2);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C31
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe3), DirectX::XMLoadFloat3(&NBe1));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae1, &Ae2);
	rB = LenSegOnSeparateAxis(&cro, &Be2, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C32
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe3), DirectX::XMLoadFloat3(&NBe2));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae1, &Ae2);
	rB = LenSegOnSeparateAxis(&cro, &Be1, &Be3);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離軸 : C33
	Cross = DirectX::XMVector3Cross(DirectX::XMLoadFloat3(&NAe3), DirectX::XMLoadFloat3(&NBe3));
	XMStoreFloat3(&cro, Cross);
	rA = LenSegOnSeparateAxis(&cro, &Ae1, &Ae2);
	rB = LenSegOnSeparateAxis(&cro, &Be1, &Be2);
	L = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(&Interval), Cross)));
	if (L > rA + rB)
		return false;

	// 分離平面が存在しないので「衝突している」
	return true;
	// 課題範囲-------------------------------------------------------------
}

float Box::GetRadius()
{
	float scale = (m_scale.x + m_scale.y + m_scale.z) / 3;
	return m_baseRadius * scale;
}

DirectX::XMFLOAT3 Box::GetPosition()
{
	return m_pos;
}
DirectX::XMFLOAT3 Box::GetXaxis()
{
	DirectX::XMVECTOR vec = XMVectorSet(m_mat_r._11, m_mat_r._12, m_mat_r._13, 0.0f);
	vec = DirectX::XMVector3Normalize(vec);
	DirectX::XMFLOAT3 float3 = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMStoreFloat3(&float3, vec);
	return float3;
}
DirectX::XMFLOAT3 Box::GetYaxis()
{
	DirectX::XMVECTOR vec = XMVectorSet(m_mat_r._21, m_mat_r._22, m_mat_r._23, 0.0f);
	vec = DirectX::XMVector3Normalize(vec);
	DirectX::XMFLOAT3 float3 = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMStoreFloat3(&float3, vec);
	return float3;
}
DirectX::XMFLOAT3 Box::GetZaxis()
{
	DirectX::XMVECTOR vec = XMVectorSet(m_mat_r._31, m_mat_r._32, m_mat_r._33, 0.0f);
	vec = DirectX::XMVector3Normalize(vec);
	DirectX::XMFLOAT3 float3 = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	XMStoreFloat3(&float3, vec);
	return float3;
}

float Box::GetScaleX()
{
	DirectX::XMVECTOR vec = XMVectorSet(m_mat_s._11, m_mat_s._12, m_mat_s._13, 0.0f);
	return DirectX::XMVectorGetX(DirectX::XMVector3Length(vec));
}
float Box::GetScaleY()
{
	DirectX::XMVECTOR vec = XMVectorSet(m_mat_s._21, m_mat_s._22, m_mat_s._23, 0.0f);
	return DirectX::XMVectorGetX(DirectX::XMVector3Length(vec));
}
float Box::GetScaleZ()
{
	DirectX::XMVECTOR vec = XMVectorSet(m_mat_s._31, m_mat_s._32, m_mat_s._33, 0.0f);
	return DirectX::XMVectorGetX(DirectX::XMVector3Length(vec));
}

// 分離軸に投影された軸成分から投影線分長を算出
FLOAT Box::LenSegOnSeparateAxis(DirectX::XMFLOAT3* Sep, DirectX::XMFLOAT3* e1, DirectX::XMFLOAT3* e2, DirectX::XMFLOAT3* e3)
{
	// 3つの内積の絶対値の和で投影線分長を計算
	// 分離軸Sepは標準化されていること
	FLOAT r1 = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(Sep), DirectX::XMLoadFloat3(e1))));
	FLOAT r2 = fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(Sep), DirectX::XMLoadFloat3(e2))));
	FLOAT r3 = e3 ? (fabs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMLoadFloat3(Sep), DirectX::XMLoadFloat3(e3))))) : 0;
	return r1 + r2 + r3;
}
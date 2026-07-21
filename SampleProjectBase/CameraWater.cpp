#include "CameraWater.h"

CameraWater::CameraWater()
	: m_pTarget(nullptr)
	, m_waterHeight(0.0f)
{
}
CameraWater::~CameraWater()
{
}
void CameraWater::Update()
{
	if(!m_pTarget) { return; }

	m_pos = m_pTarget->GetPos();
	// 参照しているカメラの水面からの高さと
	// 水面下のカメラの高さが等しくなるように計算
	m_pos.y = m_waterHeight - (m_pos.y - m_waterHeight);

	// 水面に映った絵は上下反転しているので、
	// カメラの絵が上下反転するように、アップベクトルを反転させる
	m_up = m_pTarget->GetUp();
	m_up.x *= -1.0f;
	m_up.y *= -1.0f;
	m_up.z *= -1.0f;

}
void CameraWater::SetTargetCamera(CameraBase* pCamera)
{
	m_pTarget = pCamera;
}
void CameraWater::SetWaterPlaneHeight(float height)
{
	m_waterHeight = height;
}
#include "SpotLight.h"

SpotLight::SpotLight()
	: m_range(0.0f)
	, m_angle(0.0f)
{
}
SpotLight::~SpotLight()
{
}
void SpotLight::Update()
{
}
void SpotLight::SetRange(float range)
{
	m_range = range;
}
float SpotLight::GetRange()
{
	return m_range;
}
void SpotLight::SetLightAngle(float angle)
{
	m_angle = angle;
}
float SpotLight::GetLightAngle()
{
	return m_angle;
}
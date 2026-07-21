#include "PointLight.h"

PointLight::PointLight()
	: m_range(0.0f)
{
}
PointLight::~PointLight()
{
}
void PointLight::Update()
{
}
void PointLight::SetRange(float range)
{
	m_range = range;
}
float PointLight::GetRange()
{
	return m_range;
}
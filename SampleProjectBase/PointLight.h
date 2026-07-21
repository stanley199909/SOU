#ifndef __POINT_LIGHT_H__
#define __POINT_LIGHT_H__

#include "LightBase.h"

class PointLight : public LightBase
{
public:
	PointLight();
	~PointLight();

	void Update() final;

	void SetRange(float range);
	float GetRange();

private:
	float m_range;
};

#endif // __POINT_LIGHT_H__

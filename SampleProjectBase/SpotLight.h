#ifndef __SPOT_LIGHT_H__
#define __SPOT_LIGHT_H__

#include "LightBase.h"

class SpotLight : public LightBase
{
public:
	SpotLight();
	~SpotLight();

	void Update() final;

	void SetRange(float range);
	float GetRange();
	
	void SetLightAngle(float angle);
	float GetLightAngle();

private:
	float m_range;
	float m_angle;
};

#endif // __SPOT_LIGHT_H__

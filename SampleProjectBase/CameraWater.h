#ifndef __CAMERA_WATER_H__
#define __CAMERA_WATER_H__

#include "CameraBase.h"
class CameraWater : public CameraBase {
public:
	CameraWater();
	~CameraWater();
	void Update() final;

	void SetTargetCamera(CameraBase* pCamera);
	void SetWaterPlaneHeight(float height);

private:
	CameraBase* m_pTarget;
	float m_waterHeight;
};
#endif // __CAMERA_WATER_H__
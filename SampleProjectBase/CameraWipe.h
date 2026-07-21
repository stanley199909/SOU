#ifndef __CAMERA_WIPE_H__
#define __CAMERA_WIPE_H__

#include "CameraBase.h"
class CameraWipe : public CameraBase {
public:
	CameraWipe();
	~CameraWipe();
	void Update() final;
};
#endif // __CAMERA_WIPE_H__
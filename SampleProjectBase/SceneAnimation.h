#ifndef __SCENE_ANIMATION_H__
#define __SCENE_ANIMATION_H__

#include <DirectXMath.h>
#include "SceneBase.hpp"
#include "CameraBase.h"
#include "DirectXTex/SimpleMath.h"
#include "DebugLog.h"

class SceneAnimation : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
private:
	int m_Frame = 0;
	CameraBase* pCamera;
	DirectX::XMFLOAT3 modelPos = { 0.0f, 0.0f, 0.0f };
};

#endif // __SCENE_ANIMATION_H__
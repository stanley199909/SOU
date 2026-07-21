#ifndef __SCENE_VISUAL_H__
#define __SCENE_VISUAL_H__

#include "SceneBase.hpp"

class SceneVisual : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
private:
	float m_time; // Œo‰ßŽžŠÔ
};

#endif // __SCENE_VISUAL_H__
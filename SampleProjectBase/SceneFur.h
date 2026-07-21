#ifndef __SCENE_FUR_H__
#define __SCENE_FUR_H__

#include "SceneBase.hpp"

class SceneFur : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
};

#endif // __SCENE_FUR_H__
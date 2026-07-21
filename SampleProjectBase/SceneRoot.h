#ifndef __SCENE_ROOT_H__
#define __SCENE_ROOT_H__

#include "SceneBase.hpp"

class SceneRoot : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();		// シーン選択パネル＋サブシーンのUI
	bool isSceneChange();
	std::string GetSceneName();
	
private:
	void ChangeScene();

private:
	int m_index = 0;
	std::string m_sceneName;
	bool m_isChangeScene = false;
};

#endif // __SCENE_ROOT_H__
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
	// 鍛冶場の共有プロップ(St...)モデルを「App生存期間ずっと生きる SceneRoot」が所有して
	// 一度だけ読み込む。子シーン(SceneForge/StageEditor)は GetObj で参照するだけなので、
	// シーンを切り替えても各シーンの破棄で消えず、FBX 再インポート(数秒)が起きない。
	void LoadSharedProps();

private:
	int m_index = 0;
	std::string m_sceneName;
	bool m_isChangeScene = false;
};

#endif // __SCENE_ROOT_H__
#ifndef __WALL_SCENE_H__
#define __WALL_SCENE_H__

#include "SceneBase.hpp"
#include "Box.h"
#include "Player.h"
#include "Enemy.h"
#include <DirectXMath.h>
#include <vector>

class Model;
class Shader;

// 壁・プレイヤ・敵の追尾サンプルシーン
// プレイヤと敵は牛モデル(spot)で描画する
class WallScene : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();

private:
	// 牛モデルを指定座標・向き・スケール・色で描画する
	void DrawModel(Model* model, Shader* vs, Shader* ps,
		DirectX::XMFLOAT3 pos, float angleY, float scale, DirectX::XMFLOAT4 color,
		DirectX::XMFLOAT4X4* mat, DirectX::XMFLOAT4* light, DirectX::XMFLOAT4* camera);

private:
	Player           m_player;	// プレイヤ(牛)
	Enemy            m_enemy;	// 敵(牛)
	std::vector<Box> m_walls;	// ⑤ 複数の壁
	bool  m_isHit = false;		// 敵がプレイヤを捕まえた(攻撃判定成功)
	float m_attackDist = 1.8f;	// 攻撃判定が成立する距離(少し広め)
};

#endif // __WALL_SCENE_H__

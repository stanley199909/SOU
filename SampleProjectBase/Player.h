#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "DirectXTex/SimpleMath.h"
#include "Box.h"
#include <vector>

// プレイヤクラス
// WASDキーで移動し、壁(Box)に当たると止まる
// 見た目は牛モデル(spot)で描画する(描画はWallSceneが行う)
class Player
{
public:
	Player();

	void Init(DirectX::XMFLOAT3 pos);
	// walls : 当たり判定を取る壁の配列
	void Update(float tick, std::vector<Box>& walls);

	DirectX::XMFLOAT3 GetPosition();	// 現在座標
	float GetAngleY();					// 向き(Y軸回転)
	float GetDrawScale();				// 描画スケール
	Box&  GetBox();						// 当たり判定用ボックス

private:
	// 指定した移動量を、壁と衝突しない範囲だけ適用する(軸ごとに処理)
	void MoveWithWall(DirectX::XMFLOAT3 vel, std::vector<Box>& walls);

private:
	Box   m_box;				// 当たり判定 & 座標保持
	float m_speed = 4.0f;		// 移動速度(単位/秒)
	float m_angleY = 0.0f;		// 向き(ラジアン)
	float m_drawScale = 1.0f;	// 牛モデルの描画倍率
};

#endif // __PLAYER_H__

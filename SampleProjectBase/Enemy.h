#ifndef __ENEMY_H__
#define __ENEMY_H__

#include "DirectXTex/SimpleMath.h"
#include "Box.h"
#include <vector>

// 敵クラス
// プレイヤを追尾する。壁(Box)に当たっても止まらず追い続ける
// 見た目は牛モデル(spot)で描画する(描画はWallSceneが行う)
class Enemy
{
public:
	Enemy();

	void Init(DirectX::XMFLOAT3 pos);
	// target : 追尾するプレイヤ座標 / walls : 当たり判定を取る壁の配列
	void Update(float tick, DirectX::XMFLOAT3 target, std::vector<Box>& walls);

	DirectX::XMFLOAT3 GetPosition();	// 現在座標
	float GetAngleY();					// 向き(Y軸回転)
	float GetDrawScale();				// 描画スケール
	Box&  GetBox();						// 当たり判定用ボックス

private:
	// 指定した移動量を、壁と衝突しない範囲だけ適用する(軸ごとに処理)
	void MoveWithWall(DirectX::XMFLOAT3 vel, std::vector<Box>& walls);

private:
	Box   m_box;				// 当たり判定 & 座標保持
	float m_speed = 2.5f;		// 移動速度(単位/秒)プレイヤより遅め
	float m_angleY = 0.0f;		// 向き(ラジアン)
	float m_drawScale = 1.8f;	// プレイヤと区別するため大きめ
	float m_stopDist = 1.5f;	// この距離まで近づいたら停止(前後のガタつき防止)
};

#endif // __ENEMY_H__

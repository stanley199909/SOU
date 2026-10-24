#ifndef __BOX_H__
#define __BOX_H__

#include "DirectXTex/SimpleMath.h"
#include <vector>

#pragma once
// 箱クラス
class Box
{
public:

	Box();													// 普通に初期化
	Box(DirectX::XMFLOAT3 pos);								// 座標指定初期化
	// 座標+色+サイズ指定初期化
	Box(DirectX::XMFLOAT3 pos, DirectX::XMFLOAT4 color, DirectX::XMFLOAT3 scale);
	void SetColor(DirectX::XMFLOAT4 color);
	void InitPositon(DirectX::XMFLOAT3 pos);

	void Update(float tick);
	void Drow();
	void UpdateMinMaxPos();

	bool HitSphere(Box b);			// 球*球の判定
	bool HitAABB(Box b);			// AABB
	bool HitOBB(Box b);				// OBB
	float GetRadius();				// 球の半径を取得
	DirectX::XMFLOAT3 GetPosition();
	DirectX::XMFLOAT3 GetXaxis();
	DirectX::XMFLOAT3 GetYaxis();
	DirectX::XMFLOAT3 GetZaxis();
	float GetScaleX();
	float GetScaleY();
	float GetScaleZ();
	FLOAT LenSegOnSeparateAxis(DirectX::XMFLOAT3* Sep, DirectX::XMFLOAT3* e1, DirectX::XMFLOAT3* e2, DirectX::XMFLOAT3* e3 = 0);
public:
	bool isInput = false;
	DirectX::XMFLOAT3 m_pos;
	DirectX::XMFLOAT3 m_angle;		// 箱の角度
	bool isHit = false;

	// AABB用の座標
	float max_x;
	float min_x;
	float max_y;
	float min_y;
	float max_z;
	float min_z;

	
private:
	DirectX::XMFLOAT4X4 m_mat;		// ワールド行列
	DirectX::XMFLOAT4X4 m_mat_r;	// 回転行列
	DirectX::XMFLOAT4X4 m_mat_s;	// 拡縮行列
	DirectX::XMFLOAT4 m_color;		// 箱の色
	DirectX::XMFLOAT3 m_scale;		// 箱のサイズ
	float m_baseRadius = 0.5f;		// 半径
};

#endif // __BOX_H__
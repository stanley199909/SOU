#ifndef __SCENE_FORGE_H__
#define __SCENE_FORGE_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <memory>
#include <vector>

class MeshBuffer;
class Texture;

// 「鋼を叩いた時の火花」エフェクト
//  ・スペースキー(または一定間隔)で火花をバースト発生
//  ・火花は重力で落ち、速度方向に伸びた線(ストリーク)で描く
//  ・加算合成＋ブルームで金属的に光る
class SceneForge : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

private:
	struct Spark
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 vel;
		float life;
		float maxLife;
		float size;
	};
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT2 uv;
		DirectX::XMFLOAT4 col;
	};

	void Strike();	// 1回叩く(火花をまとめて発生)

private:
	std::vector<Spark>  m_sparks;
	std::vector<Vertex> m_vtx;
	std::shared_ptr<MeshBuffer> m_mesh;
	std::shared_ptr<Texture>    m_glow;

	float m_time      = 0.0f;
	float m_autoTimer = 0.0f;	// 自動で叩く間隔用
	bool  m_autoStrike = true;	// 自動で叩くか
	float m_interval   = 1.0f;	// 叩く間隔(秒)
	int   m_burst      = 140;	// 1回の火花数
	float m_power      = 1.0f;	// 飛び散る勢い

	static const int MAX_SPARKS = 3000;
	static constexpr float GRAVITY = 9.8f;
};

#endif // __SCENE_FORGE_H__

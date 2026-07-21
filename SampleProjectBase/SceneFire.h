#ifndef __SCENE_FIRE_H__
#define __SCENE_FIRE_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <memory>
#include <vector>

class MeshBuffer;
class Texture;

// 自作パーティクルシステムによる「炎と煙」エフェクト
//  ・炎 … 加算合成で光る粒子(下→上へ揺らめく)
//  ・煙 … アルファ合成の粒子(立ち上って広がる)
//  ・ビルボード(常にカメラを向く四角形)で描画
class SceneFire : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

private:
	// 1粒子
	struct Particle
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 vel;
		float life;		// 残り寿命
		float maxLife;	// 最大寿命
		float size;		// 大きさ
		bool  smoke;	// true=煙 / false=炎
	};
	// 頂点(ビルボード用)
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT2 uv;
		DirectX::XMFLOAT4 col;
	};

	void Emit(bool smoke);	// 粒子を1つ発生
	void BuildVertices(bool smoke, const DirectX::XMFLOAT3& right, const DirectX::XMFLOAT3& up, int& outCount);

private:
	std::vector<Particle> m_particles;
	std::vector<Vertex>   m_vtx;			// 頂点作業用バッファ
	std::shared_ptr<MeshBuffer> m_mesh;		// 動的メッシュ
	std::shared_ptr<Texture>    m_glow;		// 光の粒テクスチャ

	float m_time     = 0.0f;
	float m_fireAcc  = 0.0f;	// 炎の発生蓄積
	float m_smokeAcc = 0.0f;	// 煙の発生蓄積
	float m_fireRate  = 700.0f;	// 炎の発生数/秒
	float m_smokeRate = 40.0f;	// 煙の発生数/秒
	float m_riseSpeed = 1.0f;	// 上昇の勢い(倍率)

	static const int MAX_PARTICLES = 4000;
};

#endif // __SCENE_FIRE_H__

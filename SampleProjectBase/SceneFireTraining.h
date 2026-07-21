#pragma once
#include <DirectXMath.h>
#include <vector>
class SceneFireTraining
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

	void Emit();	
private:
	struct Particle
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 vel;
		float life;		
		float maxLife;
		float size;
	};

	std::vector <Particle> m_particles;
	static const int MAX_PARTICLES = 4000;
};
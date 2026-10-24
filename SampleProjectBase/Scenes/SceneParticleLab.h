#ifndef __SCENE_PARTICLE_LAB_H__
#define __SCENE_PARTICLE_LAB_H__

#include "SceneBase.hpp"
#include "Particles.h"		// Physics/Particles: the game's spark + ember system (self-made physics)
#include <DirectXMath.h>
#include <memory>
#include <vector>

class MeshBuffer;
class Texture;

// SCENE_PARTICLE_LAB : an isolated stage to SEE and TUNE the forge particle system.
//  - Sparks: hammer-strike bursts (gravity + ground bounce).
//  - Embers: coal fire (buoyancy + shimmer + fade).
//  Every Particles::tune value plus the emitter/trigger controls get a slider.
//  F8 / a button restores everything to the startup snapshot (memory only, no file).
//  Weapon shapes are now made by AI as FBX morphs, so the old weapon sculpt editor
//  was replaced by this lab.
class SceneParticleLab : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

private:
	struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 col; };

	Particles                   m_particles;	// the actual game system, driven here
	std::vector<Vertex>         m_vtx;			// billboard vertices (rebuilt each frame)
	std::shared_ptr<MeshBuffer> m_mesh;			// dynamic buffer for the billboards
	std::shared_ptr<Texture>    m_glow;			// soft radial dot (center bright)

	float m_time = 0.0f;

	// The list of particle effects the lab knows. Add a new effect (fog, burning, ...) by
	// extending this enum + kEffectNames + the Update/DrawUI switch = one place each.
	enum Effect { EFF_SPARKS, EFF_EMBERS, EFF_COUNT };
	bool m_active[EFF_COUNT] = { true, true };	// which effects are PLAYING now (multi-select; can stack)
	int  m_editSel = EFF_SPARKS;				// which effect's parameters the panel shows (dropdown)

	// Emitter + trigger controls (the lab's own knobs; Particles::tune holds the physics).
	struct Controls
	{
		// -- spark trigger --
		bool  autoStrike     = true;			// auto-fire a burst on an interval
		float strikeInterval = 0.9f;			// seconds between auto bursts
		int   sparkCount     = 60;				// sparks per burst
		float sparkPower     = 1.0f;			// burst energy (x speed)
		float sparkScale     = 1.0f;			// burst scale (x speed)
		float strikeHeight   = 0.6f;			// burst origin Y (anvil face height)
		// -- ember emitter --
		float emberPos[3] = { 0.0f, 0.30f, 0.0f };
		float emberArea[2] = { 0.35f, 0.35f };	// spawn radius (X,Z)
		float emberRate  = 45.0f;				// embers per second
		float emberRise  = 0.8f;				// upward launch speed
	};
	Controls m_ctl;

	float m_strikeTimer = 0.0f;					// counts up to strikeInterval

	void SetupCamera();							// initial framing (once, in Init; then DCC mouse takes over)
	void StrikeOnce();							// spawn one spark burst at the anvil point
	void DrawParticles();						// build billboards (sparks + embers) + additive draw

	// startup snapshot (memory only; never writes a file).
	Particles::Tune m_tuneStartup;				// physics constants at Init
	Controls        m_ctlStartup;				// emitter/trigger controls at Init
	bool            m_haveStartup = false;
	void SnapshotAll();							// current -> startup backup
	void RestoreAll();							// startup backup -> current
};

#endif // __SCENE_PARTICLE_LAB_H__

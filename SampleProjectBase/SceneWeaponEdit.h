#ifndef __SCENE_WEAPON_EDIT_H__
#define __SCENE_WEAPON_EDIT_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <memory>
#include <vector>

class MeshBuffer;

// SCENE_FORGE_WEAPON : a very low-spec "shape editor" (think tiny Blender).
//  Purpose: design ONE weapon as a few STAGE models (raw -> stage1 -> ... -> finished).
//  The block is a cell grid; pull each cell up/down (mouse wheel / drag) to sculpt it,
//  like stretching a cube. Color shows height: dark = thin, bright = thick.
//  Switch stage slots and edit each; "copy from previous" seeds the next stage.
//  Saves all stages to Assets/weapon.txt. The game plays these stages back in order.
//  NOTE: NL/NW MUST match SceneForge's grid.
class SceneWeaponEdit : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

private:
	static const int NL = 20;		// length cells (keep in sync with SceneForge::NL)
	static const int NW = 6;		// width cells  (keep in sync with SceneForge::NW)
	static const int MAX_STAGE = 6;	// up to 6 stage models per weapon

	struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 col; };

	static const int NLEV = 5;		// height pen levels: 0..NLEV (0=gone, NLEV=thickest)

	float m_hw[MAX_STAGE][NL][NW];	// designed heights, relative 0..1 (0=flash/gone, 1=thick)
	int   m_stageCount = 4;			// how many stages this weapon uses
	int   m_stage = 0;				// active stage being edited
	int   m_pen   = 3;				// current pen height level (0..NLEV)
	bool  m_mirror = true;			// auto-mirror painting across the center line (L/R symmetry)

	float m_time    = 0.0f;
	float m_viewYaw = 0.4f;			// spin the 3D preview (does not affect data)

	// grid geometry in the world (centered at origin, on plane y=0)
	float m_len   = 1.8f;			// length along Z
	float m_halfW = 0.30f;			// half width along X
	float m_dispScale = 0.12f;		// height multiplier: keep it THIN so the blade outline reads

	std::vector<Vertex>         m_vtx;
	std::shared_ptr<MeshBuffer> m_mesh;

	void  ApplyCamera();			// fixed 3/4 overhead (spun by m_viewYaw) so the grid reads well
	int   BuildMesh();				// smooth heightfield of the active stage (returns vtx count)
	DirectX::XMFLOAT3 CellPos(int i, int j, float h);	// world pos of a grid point

	void  PaintGridUI();			// top-down 2D pixel-art grid: click/drag to paint cells
	void  DefaultShapes();			// author a raw-bar -> finished-dagger progression (demo)
	void  CopyFromPrev();			// copy previous stage into the active one
	void  SaveWeapon();
	void  LoadWeapon();
};

#endif // __SCENE_WEAPON_EDIT_H__

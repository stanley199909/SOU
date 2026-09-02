#ifndef __STAGE_EDITOR_H__
#define __STAGE_EDITOR_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <memory>

class Model;
class Texture;
class MeshBuffer;

// SCENE_STAGE_EDITOR : the blacksmith stage-setting editor.
//  - Loads the same props as the game so you can arrange them.
//  - Drag objects on the ground with the mouse (Unity-like).
//  - Tabs: Objects / Coal / Camera. Read the final numbers here and bake into SceneForge.
//  - Free camera (ALT+drag) works because this scene does not lock the camera.
class SceneStageEditor : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

private:
	struct Vertex { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 col; };

	struct Prop
	{
		std::string       key, label;
		float             scale = 0.02f;
		float             pos[3] = { 0,0,0 };
		float             yaw = 0.0f;
		bool              groundSnap = true;	// on: pos[1]=height above floor; off: pos[1]=absolute Y
		DirectX::XMFLOAT3 aabbMin{ 0,0,0 }, aabbMax{ 0,0,0 };
	};
	std::vector<Prop> m_props;
	float m_time = 0.0f;

	void LoadProp(const char* key, const char* fbx, const char* tex,
	              float targetSize, float px, float pz, float yaw);
	DirectX::XMMATRIX PropWorld(Prop& p);
	void DrawModelWorld(Model* m, const DirectX::XMMATRIX& world,
	                    const DirectX::XMFLOAT4& tint = DirectX::XMFLOAT4(1, 1, 1, 1));
	void DrawScenery();

	//--- forge material auto-assign (by FBX material name) so the forge looks right
	std::vector<std::shared_ptr<Texture>> m_forgeTex;
	std::vector<int> m_forgeMatPick;
	void ApplyForgeTextures();

	//--- glowing coal bed (self-made)
	std::shared_ptr<MeshBuffer> m_coalMesh;
	std::shared_ptr<Texture>    m_coalTex;
	bool  m_coalOn = true;
	float m_coalPos[3] = { 1.80f, 0.55f, 0.60f };	// aligned with StForge initial pos
	float m_coalYaw = 0.0f;
	float m_coalSize[2] = { 0.55f, 0.75f };
	float m_coalGlow = 1.8f;
	void  DrawCoalBed();

	//--- coal embers (rising sparks) - same idea as the game's particle system
	struct Ember { DirectX::XMFLOAT3 pos, vel; float life, maxLife, size; };
	std::vector<Ember>          m_embers;
	std::vector<Vertex>         m_emberVtx;		// billboard vertices (rebuilt each frame)
	std::shared_ptr<MeshBuffer> m_emberMesh;	// dynamic buffer for the billboards
	std::shared_ptr<Texture>    m_emberGlow;	// soft radial dot texture
	float m_emberSpawn = 0.0f;					// fractional spawn carry-over
	static const int MAX_EMBERS = 500;
	//--- adjustable emitter (edited here, saved to stage_layout.txt, read by the game)
	float m_emberPos[3]  = { 1.80f, 0.60f, 0.60f };	// emitter center (defaults near coal)
	float m_emberArea[2] = { 0.45f, 0.60f };		// spawn radius (X,Z)
	float m_emberRate    = 45.0f;					// particles per second
	float m_emberRise    = 0.8f;					// upward launch speed
	void  UpdateEmbers(float tick);				// spawn + rise + fade (CPU)
	void  DrawEmbers();							// camera-facing glowing dots (additive)

	//--- water surface (for the quench trough)
	std::shared_ptr<Texture> m_waterTex;
	bool  m_waterOn = true;
	float m_waterPos[3] = { -1.4f, 0.32f, -0.9f };
	float m_waterYaw = 0.0f;
	float m_waterSize[2] = { 0.55f, 0.20f };
	void  DrawWater();

	//--- drag-to-place editor (click a prop in the viewport to select + drag it)
	int   m_editSel = -1;
	bool  m_editDragging = false;
	bool  m_lmbPrev = false;
	int   m_gizmoAxis = -1;	// axis being dragged via the gizmo arrow (0=X,1=Y,2=Z, -1=none)
	float m_editPrevX = 0.0f, m_editPrevY = 0.0f;
	void  UpdateEditorDrag();
	bool  WorldToScreen(const DirectX::XMFLOAT3& world, float& sx, float& sy);	// project to screen px
	int   PickProp(float mx, float my);		// nearest prop under the cursor (-1 = none)
	bool  GizmoFrame(DirectX::XMFLOAT3& origin, float& axisLen);	// selected prop's gizmo origin+size
	int   PickGizmoAxis(float mx, float my);	// which arrow is under the cursor (-1 = none)
	void  DrawSelectionHighlight();			// yellow box around the selected prop
	void  DrawGizmo();						// XYZ arrows on the selected prop

	//--- persistence: save/load the whole layout to a text file (survives restart)
	void  SaveLayout();
	void  LoadLayout();
};

#endif // __STAGE_EDITOR_H___

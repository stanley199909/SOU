#include "math.h"
#include "SceneBlank.h"
#include "Geometory.h"
#include "DebugLog.h"
#include "Model.h"
#include "Texture.h"
#include "Shader.h"
#include "CameraBase.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Input.h"
#include "imgui/imgui.h"
#include "PostProcess.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace DirectX;
using namespace DirectX::SimpleMath;

// 水面の屈折/深度用に、シーンのスナップショットを撮る PostProcess を参照(Mainのグローバル)
extern std::shared_ptr<PostProcess> g_pPost;

static float frand()               { return (float)rand() / (float)RAND_MAX; }
static float frand(float a, float b){ return a + (b - a) * frand(); }

// coal/water shaders are now the shared .cso files compiled from
// VS_Coal.hlsl / PS_Coal.hlsl (same ones the game SceneForge uses).
// Coal uses PS_Coal (flicker); water reuses PS_TexTint (flat, no flicker).

static void GetMouseClient(float& mx, float& my)
{
	POINT p; GetCursorPos(&p);
	HWND hwnd = GetActiveWindow();
	if (hwnd) ScreenToClient(hwnd, &p);
	mx = (float)p.x; my = (float)p.y;
}

//--- load a draggable prop: auto scale so largest AABB extent == targetSize
void SceneBlank::LoadProp(const char* key, const char* fbx, const char* tex,
                          float targetSize, float px, float pz, float yaw)
{
	Model* m = CreateObj<Model>(key);
	if (!m->Load(fbx, 1.0f, false, true)) return;
	if (tex && tex[0])
	{
		auto t = std::make_shared<Texture>();
		if (SUCCEEDED(t->Create(tex))) m->SetTexture(t);
	}
	Prop p;
	p.key = key; p.label = key;
	p.pos[0] = px; p.pos[1] = 0.0f; p.pos[2] = pz;
	p.yaw = yaw;
	m->GetLocalAABB(p.aabbMin, p.aabbMax);
	float ex = p.aabbMax.x - p.aabbMin.x, ey = p.aabbMax.y - p.aabbMin.y, ez = p.aabbMax.z - p.aabbMin.z;
	float mx = ex; if (ey > mx) mx = ey; if (ez > mx) mx = ez;
	p.scale = (mx > 1e-4f) ? (targetSize / mx) : targetSize;
	m_props.push_back(p);
}

void SceneBlank::Init()
{
	VertexShader* vs = CreateObj<VertexShader>("StVS");
	if (FAILED(vs->Load("Assets/Shader/VS_Object.cso")))
		MessageBox(nullptr, "VS_Object.cso", "Shader Error", MB_OK);
	PixelShader* ps = CreateObj<PixelShader>("StPS");
	if (FAILED(ps->Load("Assets/Shader/PS_TexTint.cso")))
		MessageBox(nullptr, "PS_TexTint.cso", "Shader Error", MB_OK);
	VertexShader* cvs = CreateObj<VertexShader>("StCoalVS");
	if (FAILED(cvs->Load("Assets/Shader/VS_Coal.cso")))
		MessageBox(nullptr, "VS_Coal.cso", "Shader Error", MB_OK);
	PixelShader*  cps = CreateObj<PixelShader>("StCoalPS");
	if (FAILED(cps->Load("Assets/Shader/PS_Coal.cso")))
		MessageBox(nullptr, "PS_Coal.cso", "Shader Error", MB_OK);
	PixelShader*  wps = CreateObj<PixelShader>("StWaterPS");
	if (FAILED(wps->Load("Assets/Shader/PS_Water.cso")))
		MessageBox(nullptr, "PS_Water.cso", "Shader Error", MB_OK);

	const std::string P = "Assets/MM_Blacksmith_Pack/";
	const std::string kAnvilTex = P + "Anvil/Textures/T_Anvil_BaseColor.png";
	const std::string kWood     = P + "Ballows/Textures/T_Wood_BaseColor.png";
	const std::string kTable    = P + "Worktable/Textures/T_BS_Worktable_V1_BaseColor.png";
	const std::string kBucket   = P + "Buckets/Textures/T_Buckets_V1_BaseColor.png";
	const std::string kSharp    = P + "Sharpner/Textures/T_Sharpner_V1_BaseColor.png";
	const std::string kTools    = P + "Tools/Textures/1024x512/T_BS_Tools_BaseColor.png";
	const std::string kMetal    = P + "Metal Parts/Textures/T_Metal_parts_BaseColor.png";
	const std::string kForgeStone = P + "Forges/Textures/T_Forge_1_UV1_BaseColor.PNG";

	// KCD layout (anvil at origin, others around). args = (targetSize, X, Z, Yaw)
	LoadProp("StGround",   "Assets/Model/plane/plane.fbx", "Assets/Model/field/wooden-plank-textured-background-material.jpg", 12.0f, 0.0f, 0.0f, 0.0f);
	LoadProp("StStump",    (P+"Anvil/SM_Stump.fbx").c_str(),          kAnvilTex.c_str(), 0.60f, 0.0f,  0.0f, 0.0f);
	LoadProp("StAnvil",    (P+"Anvil/SM_Anvil.fbx").c_str(),          kAnvilTex.c_str(), 0.70f, 0.0f,  0.0f, 0.0f);
	LoadProp("StForge",    (P+"Forges/SM_BS_Forge_1.fbx").c_str(),    kForgeStone.c_str(), 2.6f, 1.8f, 0.6f, 0.0f);
	LoadProp("StStand",    (P+"Ballows/SM_Bellows_stand_1.fbx").c_str(), kWood.c_str(), 1.2f, 3.2f,  1.0f, 0.0f);
	LoadProp("StBellows",  (P+"Ballows/SM_Bellows.fbx").c_str(),      kWood.c_str(),   1.4f,  3.2f,  1.0f, 0.0f);
	LoadProp("StWorktable",(P+"Worktable/SM_BS_Worktable.fbx").c_str(),kTable.c_str(),  2.2f, -2.6f, 0.6f, 0.0f);
	LoadProp("StTrough",   (P+"Buckets/SM_Trough.fbx").c_str(),       kBucket.c_str(), 1.6f, -1.4f,-0.9f, 0.0f);
	LoadProp("StBucket",   (P+"Buckets/SM_B_Bucket_1.fbx").c_str(),   kBucket.c_str(), 0.7f,  0.9f,-0.9f, 0.0f);
	LoadProp("StGrind",    (P+"Sharpner/SM_Sharpner.fbx").c_str(),    kSharp.c_str(),  1.4f, -4.2f,-1.8f, 0.0f);
	LoadProp("StPoker",    (P+"Tools/SM_BS_Poker.fbx").c_str(),       kTools.c_str(),  1.2f,  1.8f, 0.3f, 0.0f);
	LoadProp("StPliers",   (P+"Tools/SM_BS_Pliers_1.fbx").c_str(),    kTools.c_str(),  0.6f, -2.4f, 0.6f, 0.0f);
	LoadProp("StMetal1",   (P+"Metal Parts/SM_Metal_part_1.fbx").c_str(), kMetal.c_str(), 0.4f, -2.8f, 0.6f, 0.0f);
	LoadProp("StMetal2",   (P+"Metal Parts/SM_Metal_part_2.fbx").c_str(), kMetal.c_str(), 0.4f, -3.1f, 0.7f, 0.0f);

	// placement pass: stack things where they belong (anvil on stump, bellows on stand, tools on table)
	{
		auto getP = [&](const char* k)->Prop* { for (auto& p : m_props) if (p.key == k) return &p; return nullptr; };
		auto worldH = [](Prop* p)->float { return (p->aabbMax.y - p->aabbMin.y) * p->scale; };
		Prop* stump = getP("StStump"); Prop* anvil = getP("StAnvil");
		if (stump && anvil) { anvil->pos[0] = stump->pos[0]; anvil->pos[2] = stump->pos[2]; anvil->pos[1] = worldH(stump); }
		Prop* stand = getP("StStand"); Prop* bel = getP("StBellows");
		if (stand && bel) { bel->pos[0] = stand->pos[0]; bel->pos[2] = stand->pos[2]; bel->pos[1] = worldH(stand) * 0.55f; }
		Prop* table = getP("StWorktable");
		float tableH = table ? worldH(table) : 0.0f;
		if (Prop* pl = getP("StPliers")) if (table) { pl->pos[0] = table->pos[0] + 0.3f; pl->pos[2] = table->pos[2]; pl->pos[1] = tableH; }
		if (Prop* m1 = getP("StMetal1")) if (table) { m1->pos[0] = table->pos[0] - 0.3f; m1->pos[2] = table->pos[2] + 0.1f; m1->pos[1] = tableH; }
		if (Prop* m2 = getP("StMetal2")) if (table) { m2->pos[0] = table->pos[0] + 0.0f; m2->pos[2] = table->pos[2] - 0.2f; m2->pos[1] = tableH; }
	}

	// forge material auto-assign textures (UV1 stone / UV2 / UV3 firebox / UV4 / ember / combined)
	const char* forgeTexFiles[] = {
		"T_Forge_1_UV1_BaseColor.PNG", "T_Forge_1_UV2_BaseColor.PNG",
		"T_Forge_1_UV3_BaseColor.PNG", "T_Forge_1_UV4_BaseColor.PNG",
		"T_Forge_1_UV3_Emissive.PNG",  "T_Forge_1_UV3_Combined.PNG",
	};
	for (auto* f : forgeTexFiles)
	{
		auto tex = std::make_shared<Texture>();
		if (SUCCEEDED(tex->Create((P + "Forges/Textures/" + f).c_str())))
			m_forgeTex.push_back(tex);
	}
	if (Model* forge = GetObj<Model>("StForge"))
	{
		size_t mc = forge->GetMaterialCount();
		m_forgeMatPick.assign(mc, 0);
		for (size_t i = 0; i < mc; ++i)
		{
			std::string n = forge->GetMaterialName(i);
			if      (n.find("UV1") != std::string::npos) m_forgeMatPick[i] = 0;
			else if (n.find("UV2") != std::string::npos) m_forgeMatPick[i] = 1;
			else if (n.find("UV3") != std::string::npos) m_forgeMatPick[i] = 2;
			else if (n.find("UV4") != std::string::npos) m_forgeMatPick[i] = 3;
		}
	}

	// coal bed mesh (two-sided horizontal quad) + combined coal texture
	{
		float h = 1.0f;
		Vertex a{ {-h,0,-h},{0,0},{1,1,1,1} }, b{ {h,0,-h},{1,0},{1,1,1,1} };
		Vertex c{ {-h,0, h},{0,1},{1,1,1,1} }, d{ {h,0, h},{1,1},{1,1,1,1} };
		Vertex q[12] = { a,c,b, b,c,d,  a,b,c, b,d,c };
		MeshBuffer::Description cd = {};
		cd.pVtx = q; cd.vtxSize = sizeof(Vertex); cd.vtxCount = 12;
		cd.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		m_coalMesh = std::make_shared<MeshBuffer>(cd);
		if (!m_forgeTex.empty()) m_coalTex = m_forgeTex.back();
	}
	// water surface texture (subtle stone tinted blue = water in the trough)
	{
		auto t = std::make_shared<Texture>();
		if (SUCCEEDED(t->Create("Assets/Model/plane/stone.png"))) m_waterTex = t;
	}

	// --- coal ember particles: same system as the game (VS_Particle/PS_Particle) ---
	VertexShader* pvs = CreateObj<VertexShader>("StPartVS");
	if (FAILED(pvs->Load("Assets/Shader/VS_Particle.cso")))
		MessageBox(nullptr, "VS_Particle.cso", "Shader Error", MB_OK);
	PixelShader*  pps = CreateObj<PixelShader>("StPartPS");
	if (FAILED(pps->Load("Assets/Shader/PS_Particle.cso")))
		MessageBox(nullptr, "PS_Particle.cso", "Shader Error", MB_OK);
	// soft radial dot texture (bright center -> transparent edge)
	{
		const int S = 64;
		std::vector<unsigned char> pix(S * S * 4);
		for (int y = 0; y < S; ++y)
		for (int x = 0; x < S; ++x)
		{
			float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1;
			float f = 1.0f - sqrtf(dx * dx + dy * dy); if (f < 0) f = 0; f = f * f;
			unsigned char c = (unsigned char)(f * 255);
			int idx = (y * S + x) * 4; pix[idx] = pix[idx+1] = pix[idx+2] = pix[idx+3] = c;
		}
		m_emberGlow = std::make_shared<Texture>();
		m_emberGlow->Create(DXGI_FORMAT_R8G8B8A8_UNORM, S, S, pix.data());
	}
	// dynamic billboard mesh
	m_emberVtx.resize(MAX_EMBERS * 6);
	{
		MeshBuffer::Description d = {};
		d.pVtx = m_emberVtx.data(); d.vtxSize = sizeof(Vertex); d.vtxCount = (UINT)m_emberVtx.size();
		d.isWrite = true; d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		m_emberMesh = std::make_shared<MeshBuffer>(d);
	}

	// place the camera to see the stage (then ALT+drag is free)
	if (CameraBase* cam = GetObj<CameraBase>("Camera"))
	{
		cam->SetPos (XMFLOAT3(0.0f, 4.0f, -6.5f));
		cam->SetLook(XMFLOAT3(0.0f, 0.8f,  0.5f));
		cam->SetUp  (XMFLOAT3(0.0f, 1.0f,  0.0f));
	}

	LoadLayout();	// override defaults with the saved arrangement if it exists
}

//--- coal embers: spawn from the coal bed center, rise with buoyancy, fade out
void SceneBlank::UpdateEmbers(float tick)
{
	if (m_coalOn)
	{
		m_emberSpawn += tick * m_emberRate;
		int n = (int)m_emberSpawn; m_emberSpawn -= n;
		for (int k = 0; k < n && (int)m_embers.size() < MAX_EMBERS; ++k)
		{
			Ember e = {};
			float rx = frand(-1.0f, 1.0f) * frand(0.0f, 1.0f) * m_emberArea[0];
			float rz = frand(-1.0f, 1.0f) * frand(0.0f, 1.0f) * m_emberArea[1];
			e.pos = XMFLOAT3(m_emberPos[0] + rx, m_emberPos[1] + 0.05f, m_emberPos[2] + rz);
			e.vel = XMFLOAT3(frand(-0.15f, 0.15f), m_emberRise * frand(0.7f, 1.3f), frand(-0.15f, 0.15f));
			e.maxLife = frand(1.2f, 2.6f); e.life = e.maxLife;
			e.size = frand(0.02f, 0.05f);
			m_embers.push_back(e);
		}
	}
	for (size_t i = 0; i < m_embers.size(); )
	{
		Ember& e = m_embers[i];
		e.life -= tick;
		if (e.life <= 0.0f) { e = m_embers.back(); m_embers.pop_back(); continue; }
		e.vel.y += 0.4f * tick;
		e.vel.x += sinf(m_time * 3.0f + e.pos.y * 8.0f) * 0.10f * tick;
		e.vel.z += cosf(m_time * 2.3f + e.pos.x * 8.0f) * 0.10f * tick;
		e.pos.x += e.vel.x * tick; e.pos.y += e.vel.y * tick; e.pos.z += e.vel.z * tick;
		++i;
	}
}

//--- draw embers as camera-facing glowing dots (additive), reusing the particle shader
void SceneBlank::DrawEmbers()
{
	if (m_embers.empty()) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("StPartVS");
	PixelShader*  ps  = GetObj<PixelShader>("StPartPS");
	if (!cam || !vs || !ps || !m_emberMesh) return;

	XMFLOAT3 camPos = cam->GetPos();
	XMVECTOR vcam = XMLoadFloat3(&camPos), worldUp = XMVectorSet(0, 1, 0, 0);
	XMFLOAT4X4 camMat[2]; camMat[0] = cam->GetView(); camMat[1] = cam->GetProj();
	vs->WriteBuffer(0, camMat);

	int v = 0;
	for (const Ember& e : m_embers)
	{
		float t = e.life / e.maxLife;
		float fl = 0.70f + 0.30f * sinf(m_time * 25.0f + e.pos.x * 10.0f);
		float br = t * fl;
		XMFLOAT4 col(1.0f * br, (0.5f * t + 0.1f) * br, 0.12f * t * br, 1.0f);

		XMVECTOR c = XMLoadFloat3(&e.pos);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));
		XMVECTOR right = XMVector3Cross(worldUp, toCam);
		if (XMVectorGetX(XMVector3Length(right)) < 0.001f) right = XMVectorSet(1, 0, 0, 0);
		right = XMVector3Normalize(right);
		XMVECTOR up = XMVector3Normalize(XMVector3Cross(toCam, right));
		float sz = e.size * (0.6f + 0.6f * t);
		XMVECTOR R = XMVectorScale(right, sz), U = XMVectorScale(up, sz);

		XMFLOAT3 tl, tr, bl, br3;
		XMStoreFloat3(&tl,  XMVectorAdd(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&tr,  XMVectorAdd(XMVectorAdd(c, R), U));
		XMStoreFloat3(&bl,  XMVectorSubtract(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&br3, XMVectorSubtract(XMVectorAdd(c, R), U));

		Vertex* q = &m_emberVtx[v];
		q[0] = { tl, XMFLOAT2(0,0), col }; q[1] = { tr, XMFLOAT2(1,0), col };
		q[2] = { bl, XMFLOAT2(0,1), col }; q[3] = { bl, XMFLOAT2(0,1), col };
		q[4] = { tr, XMFLOAT2(1,0), col }; q[5] = { br3, XMFLOAT2(1,1), col };
		v += 6;
		if (v + 6 > (int)m_emberVtx.size()) break;
	}
	if (v == 0) return;

	SetBlendMode(BLEND_ADD);
	SetDepthTest(DEPTH_ENABLE_TEST);
	ps->SetTexture(0, m_emberGlow.get());
	m_emberMesh->Write(m_emberVtx.data());
	vs->Bind(); ps->Bind();
	m_emberMesh->Draw(v);
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

static const char* STAGE_FILE = "Assets/stage_layout.txt";

//--- save the whole layout to a text file (like the teacher's setting.dat, but text)
void SceneBlank::SaveLayout()
{
	FILE* fp = nullptr;
	fopen_s(&fp, STAGE_FILE, "w");
	if (!fp) return;
	for (auto& p : m_props)
		fprintf(fp, "P %s %.4f %.4f %.4f %.4f %.5f %d\n",
			p.key.c_str(), p.pos[0], p.pos[1], p.pos[2], p.yaw, p.scale, p.groundSnap ? 1 : 0);
	fprintf(fp, "C %.4f %.4f %.4f %.4f %.4f %.4f %.3f %d\n",
		m_coalPos[0], m_coalPos[1], m_coalPos[2], m_coalYaw, m_coalSize[0], m_coalSize[1], m_coalGlow, m_coalOn ? 1 : 0);
	fprintf(fp, "W %.4f %.4f %.4f %.4f %.4f %.4f %d\n",
		m_waterPos[0], m_waterPos[1], m_waterPos[2], m_waterYaw, m_waterSize[0], m_waterSize[1], m_waterOn ? 1 : 0);
	fprintf(fp, "E %.4f %.4f %.4f %.4f %.4f %.2f %.3f\n",
		m_emberPos[0], m_emberPos[1], m_emberPos[2], m_emberArea[0], m_emberArea[1], m_emberRate, m_emberRise);
	fclose(fp);
}

//--- load the saved layout (applies to matching props; missing file = keep defaults)
void SceneBlank::LoadLayout()
{
	FILE* fp = nullptr;
	fopen_s(&fp, STAGE_FILE, "r");
	if (!fp) return;
	char line[256];
	while (fgets(line, sizeof(line), fp))
	{
		if (line[0] == 'P')
		{
			char key[64]; float x, y, z, yaw, sc; int snap;
			if (sscanf_s(line, "P %63s %f %f %f %f %f %d", key, (unsigned)sizeof(key), &x, &y, &z, &yaw, &sc, &snap) == 7)
				for (auto& p : m_props) if (p.key == key)
				{ p.pos[0] = x; p.pos[1] = y; p.pos[2] = z; p.yaw = yaw; p.scale = sc; p.groundSnap = (snap != 0); }
		}
		else if (line[0] == 'C')
		{
			float x, y, z, yaw, sx, sy, g; int on;
			if (sscanf_s(line, "C %f %f %f %f %f %f %f %d", &x, &y, &z, &yaw, &sx, &sy, &g, &on) == 8)
			{ m_coalPos[0]=x; m_coalPos[1]=y; m_coalPos[2]=z; m_coalYaw=yaw; m_coalSize[0]=sx; m_coalSize[1]=sy; m_coalGlow=g; m_coalOn=(on!=0); }
		}
		else if (line[0] == 'W')
		{
			float x, y, z, yaw, sx, sy; int on;
			if (sscanf_s(line, "W %f %f %f %f %f %f %d", &x, &y, &z, &yaw, &sx, &sy, &on) == 7)
			{ m_waterPos[0]=x; m_waterPos[1]=y; m_waterPos[2]=z; m_waterYaw=yaw; m_waterSize[0]=sx; m_waterSize[1]=sy; m_waterOn=(on!=0); }
		}
		else if (line[0] == 'E')
		{
			float x, y, z, sx, sy, rt, ri;
			if (sscanf_s(line, "E %f %f %f %f %f %f %f", &x, &y, &z, &sx, &sy, &rt, &ri) == 7)
			{ m_emberPos[0]=x; m_emberPos[1]=y; m_emberPos[2]=z; m_emberArea[0]=sx; m_emberArea[1]=sy; m_emberRate=rt; m_emberRise=ri; }
		}
	}
	fclose(fp);
}

void SceneBlank::Uninit()
{
	SaveLayout();	// persist the arrangement on leaving/closing the scene
	DestroyObj("StVS"); DestroyObj("StPS");
	DestroyObj("StCoalVS"); DestroyObj("StCoalPS"); DestroyObj("StWaterPS");
	DestroyObj("StPartVS"); DestroyObj("StPartPS");
	for (auto& p : m_props) DestroyObj(p.key.c_str());
	m_props.clear();
	m_coalMesh.reset(); m_coalTex.reset();
	m_emberMesh.reset(); m_emberGlow.reset(); m_embers.clear();
}

XMMATRIX SceneBlank::PropWorld(Prop& p)
{
	Model* m = GetObj<Model>(p.key.c_str());
	XMMATRIX base = m ? (XMMATRIX)m->GetScaleBaseMatrix() : XMMatrixIdentity();
	// build with Y=0; pos[1] is applied last so it means "height above floor" (snap) or absolute (no snap)
	XMMATRIX world = base *
		XMMatrixScaling(p.scale, p.scale, p.scale) *
		XMMatrixRotationY(p.yaw) *
		XMMatrixTranslation(p.pos[0], 0.0f, p.pos[2]);
	if (p.groundSnap)
	{
		float minY = 1e18f;
		for (int i = 0; i < 8; ++i)
		{
			XMFLOAT3 c(
				(i & 1) ? p.aabbMax.x : p.aabbMin.x,
				(i & 2) ? p.aabbMax.y : p.aabbMin.y,
				(i & 4) ? p.aabbMax.z : p.aabbMin.z);
			float y = XMVectorGetY(XMVector3TransformCoord(XMLoadFloat3(&c), world));
			if (y < minY) minY = y;
		}
		world = world * XMMatrixTranslation(0.0f, -minY, 0.0f);	// bottom to floor
	}
	world = world * XMMatrixTranslation(0.0f, p.pos[1], 0.0f);	// pos.y = lift above floor / absolute
	return world;
}

void SceneBlank::DrawModelWorld(Model* m, const XMMATRIX& world, const XMFLOAT4& tint)
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("StVS");
	PixelShader*  ps  = GetObj<PixelShader>("StPS");
	if (!m || !cam || !vs || !ps) return;
	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView(); mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	XMFLOAT4 color = tint;
	vs->WriteBuffer(0, mat);
	ps->WriteBuffer(0, &color);
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	m->SetVertexShader(vs);
	m->SetPixelShader(ps);
	m->Draw();
}

void SceneBlank::ApplyForgeTextures()
{
	Model* forge = GetObj<Model>("StForge");
	if (!forge || m_forgeTex.empty()) return;
	for (size_t i = 0; i < m_forgeMatPick.size(); ++i)
	{
		int pick = m_forgeMatPick[i];
		if (pick >= 0 && pick < (int)m_forgeTex.size())
			forge->SetTextureAt(i, m_forgeTex[pick]);
	}
}

void SceneBlank::DrawCoalBed()
{
	if (!m_coalOn || !m_coalMesh || !m_coalTex) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("StCoalVS");
	PixelShader*  ps  = GetObj<PixelShader>("StCoalPS");
	if (!cam || !vs || !ps) return;
	XMMATRIX world =
		XMMatrixScaling(m_coalSize[0], 1.0f, m_coalSize[1]) *
		XMMatrixRotationY(m_coalYaw) *
		XMMatrixTranslation(m_coalPos[0], m_coalPos[1], m_coalPos[2]);
	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView(); mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	vs->WriteBuffer(0, mat);
	// flicker now runs in PS_Coal on the GPU; CPU only sends brightness + time
	XMFLOAT4 tint(m_coalGlow, m_coalGlow, m_coalGlow, m_time);	// rgb=brightness, a=time
	ps->WriteBuffer(0, &tint);
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind(); ps->Bind();
	ps->SetTexture(0, m_coalTex.get());
	m_coalMesh->Draw();
}

//--- water surface for the trough: procedural moving water (PS_Water), not a flat texture
void SceneBlank::DrawWater()
{
	if (!m_waterOn || !m_coalMesh || !g_pPost) return;
	CameraBase*   cam   = GetObj<CameraBase>("Camera");
	VertexShader* vs    = GetObj<VertexShader>("StCoalVS");	// generic pos/uv/col VS
	PixelShader*  ps    = GetObj<PixelShader>("StWaterPS");	// PS_Water: refraction + depth
	DepthStencil* depth = GetObj<DepthStencil>("DSV");
	RenderTarget* scene = g_pPost->GetSceneRT();
	if (!cam || !vs || !ps || !depth || !scene) return;

	// Snapshot the opaque scene (refraction source); depth buffer holds scene depth.
	Texture* refr = g_pPost->CaptureScene();
	if (!refr) return;

	XMMATRIX world =
		XMMatrixScaling(m_waterSize[0], 1.0f, m_waterSize[1]) *
		XMMatrixRotationY(m_waterYaw) *
		XMMatrixTranslation(m_waterPos[0], m_waterPos[1], m_waterPos[2]);
	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView(); mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	vs->WriteBuffer(0, mat);

	XMFLOAT4X4 projNT = cam->GetProj(false);
	XMFLOAT4 cb[2];
	cb[0] = XMFLOAT4(m_time, (float)refr->GetWidth(), (float)refr->GetHeight(), 1.0f);
	cb[1] = XMFLOAT4(projNT._33, projNT._43, 0.06f, 0.35f);	// A, B, foam, depthFade
	ps->WriteBuffer(0, cb);

	// Unbind the depth buffer from OM so we can read it as a texture; occlusion is
	// done in the water PS via depth compare + discard (same as the game scene).
	SetRenderTargets(1, &scene, nullptr);
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_DISABLE);
	vs->Bind(); ps->Bind();
	ps->SetTexture(0, refr);	// t0 = scene behind (refraction)
	ps->SetTexture(1, depth);	// t1 = scene depth
	m_coalMesh->Draw();

	ID3D11ShaderResourceView* pNull[2] = { nullptr, nullptr };
	GetContext()->PSSetShaderResources(0, 2, pNull);
	SetRenderTargets(1, &scene, depth);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

void SceneBlank::DrawScenery()
{
	ApplyForgeTextures();
	for (auto& p : m_props)
	{
		Model* m = GetObj<Model>(p.key.c_str());
		if (!m) continue;
		XMFLOAT4 tint = (p.key == "StForge") ? XMFLOAT4(0.80f, 0.76f, 0.72f, 1.0f) : XMFLOAT4(1, 1, 1, 1);
		DrawModelWorld(m, PropWorld(p), tint);
	}
	DrawCoalBed();
	DrawWater();
}

//--- project a world point to screen pixels (ImGui display space). false if behind camera.
bool SceneBlank::WorldToScreen(const XMFLOAT3& world, float& sx, float& sy)
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return false;
	// GetView()/GetProj() default to transposed (for the shader). CPU row-convention
	// (XMVector3Transform = pos_row * M) needs the NON-transposed math matrices.
	XMFLOAT4X4 v = cam->GetView(false), pr = cam->GetProj(false);
	XMMATRIX vp = XMLoadFloat4x4(&v) * XMLoadFloat4x4(&pr);
	XMVECTOR clip = XMVector3Transform(XMLoadFloat3(&world), vp);	// (x,y,z,1) row * VP
	float w = XMVectorGetW(clip);
	if (w <= 0.001f) return false;	// behind camera
	float ndcX = XMVectorGetX(clip) / w;
	float ndcY = XMVectorGetY(clip) / w;
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	sx = (ndcX * 0.5f + 0.5f) * disp.x;
	sy = (0.5f - ndcY * 0.5f) * disp.y;
	return true;
}

//--- world-space center of a prop (AABB center through its world matrix)
static XMFLOAT3 PropCenter(SceneBlank* /*self*/, const XMMATRIX& world, const XMFLOAT3& mn, const XMFLOAT3& mx)
{
	XMFLOAT3 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
	XMFLOAT3 out; XMStoreFloat3(&out, XMVector3TransformCoord(XMLoadFloat3(&c), world));
	return out;
}

//--- pick the prop whose screen center is nearest the cursor (within a pixel radius)
int SceneBlank::PickProp(float mx, float my)
{
	int best = -1; float bestD = 70.0f;	// selection radius in px
	for (int i = 0; i < (int)m_props.size(); ++i)
	{
		if (m_props[i].key == "StGround") continue;	// don't pick the floor
		XMFLOAT3 c = PropCenter(this, PropWorld(m_props[i]), m_props[i].aabbMin, m_props[i].aabbMax);
		float sx, sy;
		if (!WorldToScreen(c, sx, sy)) continue;
		float d = sqrtf((sx - mx) * (sx - mx) + (sy - my) * (sy - my));
		if (d < bestD) { bestD = d; best = i; }
	}
	return best;
}

//--- 2D distance from point p to segment a-b (for hit-testing gizmo arrows)
static float DistPtSeg(float px, float py, float ax, float ay, float bx, float by)
{
	float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
	float c1 = vx * wx + vy * wy;
	if (c1 <= 0) return sqrtf(wx * wx + wy * wy);
	float c2 = vx * vx + vy * vy;
	if (c2 <= c1) return sqrtf((px - bx) * (px - bx) + (py - by) * (py - by));
	float t = c1 / c2, qx = ax + t * vx, qy = ay + t * vy;
	return sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy));
}

//--- gizmo origin (world center of the selected prop) and axis length (constant on screen)
bool SceneBlank::GizmoFrame(XMFLOAT3& origin, float& axisLen)
{
	if (m_editSel < 0 || m_editSel >= (int)m_props.size()) return false;
	Prop& p = m_props[m_editSel];
	origin = PropCenter(this, PropWorld(p), p.aabbMin, p.aabbMax);
	axisLen = 0.8f;	// fixed world length (does not grow/shrink with camera distance)
	return true;
}

//--- which gizmo arrow (X/Y/Z) is under the cursor
int SceneBlank::PickGizmoAxis(float mx, float my)
{
	XMFLOAT3 o; float len;
	if (!GizmoFrame(o, len)) return -1;
	float ox, oy; if (!WorldToScreen(o, ox, oy)) return -1;
	XMFLOAT3 dir[3] = { {1,0,0},{0,1,0},{0,0,1} };
	int best = -1; float bestD = 12.0f;	// grab radius in px
	for (int a = 0; a < 3; ++a)
	{
		XMFLOAT3 e(o.x + dir[a].x * len, o.y + dir[a].y * len, o.z + dir[a].z * len);
		float ex, ey; if (!WorldToScreen(e, ex, ey)) continue;
		float d = DistPtSeg(mx, my, ox, oy, ex, ey);
		if (d < bestD) { bestD = d; best = a; }
	}
	return best;
}

//--- XYZ arrows on the selected prop (X red, Y green, Z blue; grabbed axis = yellow)
void SceneBlank::DrawGizmo()
{
	XMFLOAT3 o; float len;
	if (!GizmoFrame(o, len)) return;
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());
	XMFLOAT3 dir[3] = { {1,0,0},{0,1,0},{0,0,1} };
	XMFLOAT4 col[3] = { {1,0.25f,0.25f,1},{0.3f,1,0.3f,1},{0.35f,0.55f,1,1} };
	for (int a = 0; a < 3; ++a)
	{
		XMFLOAT3 e(o.x + dir[a].x * len, o.y + dir[a].y * len, o.z + dir[a].z * len);
		Geometory::SetColor(a == m_gizmoAxis ? XMFLOAT4(1, 1, 0.2f, 1) : col[a]);
		Geometory::AddLine(o, e);
	}
	Geometory::DrawLines();
}

//--- yellow wireframe box around the selected prop (uses the reliable Geometory line path)
void SceneBlank::DrawSelectionHighlight()
{
	if (m_editSel < 0 || m_editSel >= (int)m_props.size()) return;
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	Prop& p = m_props[m_editSel];
	XMMATRIX world = PropWorld(p);
	XMFLOAT3 c[8];
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 v(
			(i & 1) ? p.aabbMax.x : p.aabbMin.x,
			(i & 2) ? p.aabbMax.y : p.aabbMin.y,
			(i & 4) ? p.aabbMax.z : p.aabbMin.z);
		XMStoreFloat3(&c[i], XMVector3TransformCoord(XMLoadFloat3(&v), world));
	}
	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());
	Geometory::SetColor(XMFLOAT4(1.0f, 0.9f, 0.15f, 1.0f));
	for (int i = 0; i < 8; ++i)
		for (int b = 1; b <= 4; b <<= 1)
			if (!(i & b)) Geometory::AddLine(c[i], c[i | b]);
	Geometory::DrawLines();
}

//--- Unity-like: click a prop to select it, then LMB-drag to move on the ground (ALT=camera)
void SceneBlank::UpdateEditorDrag()
{
	ImGuiIO& io = ImGui::GetIO();
	bool overUI = io.WantCaptureMouse;
	bool alt = IsKeyPress(VK_MENU);
	bool lmb = IsKeyPress(VK_LBUTTON);
	bool canEdit = !overUI && !alt;

	float mx = io.MousePos.x, my = io.MousePos.y;

	// rising edge = click. Priority: grab a gizmo arrow > re-click selected (free drag) > select.
	if (canEdit && lmb && !m_lmbPrev)
	{
		int axis = (m_editSel >= 0) ? PickGizmoAxis(mx, my) : -1;
		if (axis >= 0)
		{
			m_gizmoAxis = axis; m_editDragging = true;	// grab an arrow -> axis-constrained drag
		}
		else
		{
			int hit = PickProp(mx, my);
			m_gizmoAxis = -1;
			if (hit >= 0 && hit == m_editSel)
				m_editDragging = true;			// re-click selected -> free ground drag
			else
			{
				m_editSel = hit;				// select (or deselect on empty), no drag yet
				m_editDragging = false;
			}
		}
		m_editPrevX = mx; m_editPrevY = my;
	}
	// held = drag the selected prop
	else if (canEdit && lmb && m_editDragging && m_editSel >= 0 && m_editSel < (int)m_props.size())
	{
		float dx = mx - m_editPrevX, dy = my - m_editPrevY;
		if (dx != 0.0f || dy != 0.0f)
		{
			Prop& p = m_props[m_editSel];
			if (m_gizmoAxis >= 0)
			{
				// axis-constrained: move along the grabbed world axis by the mouse motion
				// projected onto that axis's screen direction
				XMFLOAT3 o; float len; GizmoFrame(o, len);
				XMFLOAT3 dir[3] = { {1,0,0},{0,1,0},{0,0,1} };
				XMFLOAT3 e(o.x + dir[m_gizmoAxis].x * len, o.y + dir[m_gizmoAxis].y * len, o.z + dir[m_gizmoAxis].z * len);
				float ox, oy, ex, ey;
				if (WorldToScreen(o, ox, oy) && WorldToScreen(e, ex, ey))
				{
					float sax = ex - ox, say = ey - oy, sl = sqrtf(sax * sax + say * say);
					if (sl > 1.0f)
					{
						float pix = (dx * sax + dy * say) / sl;	// mouse motion along the arrow
						p.pos[m_gizmoAxis] += pix * (len / sl);	// pixels -> world units
					}
				}
			}
			else
			{
				// free drag on the ground plane (X/Z)
				CameraBase* cam = GetObj<CameraBase>("Camera");
				if (cam)
				{
					XMFLOAT3 cp = cam->GetPos(), cl = cam->GetLook();
					XMVECTOR vp = XMLoadFloat3(&cp), vl = XMLoadFloat3(&cl);
					XMVECTOR fwd = XMVector3Normalize(XMVectorSubtract(vl, vp));
					XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), fwd));
					XMVECTOR fwdG = XMVector3Normalize(XMVectorSetY(fwd, 0.0f));
					float dist; XMStoreFloat(&dist, XMVector3Length(XMVectorSubtract(vl, vp)));
					float k = dist * 0.0016f;
					XMVECTOR move = XMVectorAdd(XMVectorScale(right, dx * k), XMVectorScale(fwdG, -dy * k));
					p.pos[0] += XMVectorGetX(move);
					p.pos[2] += XMVectorGetZ(move);
				}
			}
		}
		m_editPrevX = mx; m_editPrevY = my;
	}
	else { m_editDragging = false; m_gizmoAxis = -1; }

	m_lmbPrev = lmb;
}

void SceneBlank::Update(float tick)
{
	m_time += tick;
	UpdateEditorDrag();
	UpdateEmbers(tick);	// coal embers rise even while editing
}

void SceneBlank::Draw()
{
	DrawScenery();
	DrawEmbers();				// coal embers rising from the forge
	DrawSelectionHighlight();	// yellow box around the selected prop
	DrawGizmo();				// XYZ move arrows on the selected prop
}

void SceneBlank::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(340, 420), ImGuiCond_FirstUseEver);
	ImGui::Begin("Forge Stage Setting");
	ImGui::TextDisabled("Free cam: ALT+LMB orbit / MMB pan / RMB zoom");

	if (ImGui::BeginTabBar("tabs"))
	{
		//--- Objects tab: pick + drag + fine tune
		if (ImGui::BeginTabItem("Objects"))
		{
			ImGui::TextWrapped("Click = select (yellow box + XYZ arrows). Drag an arrow = move on that axis. Drag the body = move on ground. ALT+drag = camera.");
			std::vector<const char*> pn; pn.push_back("(none)");
			for (auto& p : m_props) pn.push_back(p.label.c_str());
			int sel = m_editSel + 1;
			if (ImGui::Combo("Object", &sel, pn.data(), (int)pn.size())) m_editSel = sel - 1;
			if (m_editSel >= 0 && m_editSel < (int)m_props.size())
			{
				Prop& p = m_props[m_editSel];
				ImGui::Separator();
				ImGui::Text("%s", p.label.c_str());
				ImGui::DragFloat3("Pos XYZ", p.pos, 0.02f);
				ImGui::SliderFloat("Yaw", &p.yaw, -3.1416f, 3.1416f);
				ImGui::DragFloat("Scale", &p.scale, 0.001f, 0.0001f, 20.0f, "%.4f");
				ImGui::Checkbox("Ground snap (Pos Y = height above floor)", &p.groundSnap);
			}
			// persistence: layout auto-saves on exit; button forces an immediate save
			ImGui::Separator();
			if (ImGui::Button("Save layout now")) SaveLayout();
			ImGui::TextDisabled("Auto-saves on exit to Assets/stage_layout.txt");
			ImGui::EndTabItem();
		}
		//--- Coal tab
		if (ImGui::BeginTabItem("Coal / Water"))
		{
			ImGui::Text("Coal (forge fire)");
			ImGui::Checkbox("Coal on", &m_coalOn);
			ImGui::DragFloat3("Coal Pos", m_coalPos, 0.02f);
			ImGui::SliderFloat("Coal Yaw", &m_coalYaw, -3.1416f, 3.1416f);
			ImGui::DragFloat2("Coal Size", m_coalSize, 0.01f, 0.05f, 3.0f);
			ImGui::SliderFloat("Coal Glow", &m_coalGlow, 0.5f, 4.0f, "%.2f");
			ImGui::Separator();
			ImGui::Text("Embers (rising fire sparks)");
			ImGui::DragFloat3("Ember Pos", m_emberPos, 0.02f);
			ImGui::DragFloat2("Ember Area", m_emberArea, 0.01f, 0.02f, 3.0f);
			ImGui::SliderFloat("Ember Rate", &m_emberRate, 0.0f, 120.0f, "%.0f/s");
			ImGui::SliderFloat("Ember Rise", &m_emberRise, 0.2f, 2.5f, "%.2f");
			if (ImGui::Button("Ember = Coal pos")) { m_emberPos[0]=m_coalPos[0]; m_emberPos[1]=m_coalPos[1]; m_emberPos[2]=m_coalPos[2]; m_emberArea[0]=m_coalSize[0]*0.85f; m_emberArea[1]=m_coalSize[1]*0.85f; }
			ImGui::Separator();
			ImGui::Text("Water (quench trough)");
			ImGui::Checkbox("Water on", &m_waterOn);
			ImGui::DragFloat3("Water Pos", m_waterPos, 0.02f);
			ImGui::SliderFloat("Water Yaw", &m_waterYaw, -3.1416f, 3.1416f);
			ImGui::DragFloat2("Water Size", m_waterSize, 0.01f, 0.05f, 3.0f);
			ImGui::EndTabItem();
		}
		//--- Camera tab: read the live free-camera to bake into the game
		if (ImGui::BeginTabItem("Camera"))
		{
			if (CameraBase* cam = GetObj<CameraBase>("Camera"))
			{
				XMFLOAT3 cp = cam->GetPos(), cl = cam->GetLook();
				ImGui::Text("Move with ALT+drag to a nice angle,");
				ImGui::Text("then tell me these numbers to bake:");
				ImGui::Separator();
				ImGui::Text("Cam Pos : X%.2f  Y%.2f  Z%.2f", cp.x, cp.y, cp.z);
				ImGui::Text("Cam Look: X%.2f  Y%.2f  Z%.2f", cl.x, cl.y, cl.z);
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

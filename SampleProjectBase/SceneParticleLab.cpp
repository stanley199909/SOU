// SCENE_PARTICLE_LAB : isolated stage to see + tune the forge particle system.
// ASCII comments only (this file has no BOM; non-ASCII would break MSVC: C2601/C1075).
#include "SceneParticleLab.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "CameraBase.h"
#include "Input.h"
#include "imgui/imgui.h"
#include <cmath>
#include <string>

using namespace DirectX;

void SceneParticleLab::Init()
{
	// Reuse the game's particle shaders (.hlsl -> fxc -> .cso). Unique object names so
	// the shared CreateObj/GetObj map does not collide with SceneForge's "VS_Forge".
	VertexShader* vs = CreateObj<VertexShader>("PLabVS");
	if (FAILED(vs->Load("Assets/Shader/VS_Particle.cso")))
		MessageBox(nullptr, "VS_Particle.cso", "Shader Error", MB_OK);
	PixelShader* ps = CreateObj<PixelShader>("PLabPS");
	if (FAILED(ps->Load("Assets/Shader/PS_Particle.cso")))
		MessageBox(nullptr, "PS_Particle.cso", "Shader Error", MB_OK);

	// Soft round glow texture (center bright -> edges transparent). Same as the game's.
	const int S = 64;
	std::vector<unsigned char> pix(S * S * 4);
	for (int y = 0; y < S; ++y)
	for (int x = 0; x < S; ++x)
	{
		float dx = (x + 0.5f) / S * 2 - 1;
		float dy = (y + 0.5f) / S * 2 - 1;
		float f = 1.0f - sqrtf(dx * dx + dy * dy);
		if (f < 0) f = 0;
		f = f * f;
		unsigned char c = (unsigned char)(f * 255);
		int idx = (y * S + x) * 4;
		pix[idx] = pix[idx + 1] = pix[idx + 2] = pix[idx + 3] = c;
	}
	m_glow = std::make_shared<Texture>();
	m_glow->Create(DXGI_FORMAT_R8G8B8A8_UNORM, S, S, pix.data());

	// One buffer big enough for both pools (6 verts per particle).
	m_vtx.resize((Particles::MAX_SPARKS + Particles::MAX_EMBERS) * 6);
	MeshBuffer::Description d = {};
	d.pVtx = m_vtx.data();
	d.vtxSize = sizeof(Vertex);
	d.vtxCount = (UINT)m_vtx.size();
	d.isWrite = true;
	d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_mesh = std::make_shared<MeshBuffer>(d);

	SetupCamera();	// frame the scene ONCE; after this the DCC camera (mouse) owns the view
	SnapshotAll();	// remember startup values (F8 / button restores them)
}

void SceneParticleLab::Uninit() {}

//--- Initial framing only. We set the camera ONCE here and never again, so the shared
//    CameraDCC (RMB fly / ALT+LMB orbit / ALT+MMB pan / ALT+RMB dolly) can move freely.
//    (The old bug: calling this every frame overwrote the mouse camera = snapped back.)
void SceneParticleLab::SetupCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	cam->SetPos(XMFLOAT3(1.6f, 2.0f, -2.2f));
	cam->SetLook(XMFLOAT3(0.0f, 0.5f, 0.0f));
	cam->SetUp(XMFLOAT3(0, 1, 0));
}

void SceneParticleLab::StrikeOnce()
{
	XMFLOAT3 origin(0.0f, m_ctl.strikeHeight, 0.0f);	// anvil face point
	m_particles.SpawnSparks(origin, m_ctl.sparkCount, m_ctl.sparkPower, m_ctl.sparkScale);
}

void SceneParticleLab::Update(float tick)
{
	m_time += tick;
	// Camera is NOT forced here (see SetupCamera) so mouse look works.

	// F8 = restore every tunable to the startup snapshot (undo any mess).
	if (IsKeyTrigger(VK_F8)) RestoreAll();

	// Sparks effect: manual (Space) + auto burst, only while this effect is active.
	if (m_active[EFF_SPARKS])
	{
		if (IsKeyTrigger(VK_SPACE)) StrikeOnce();
		if (m_ctl.autoStrike)
		{
			m_strikeTimer += tick;
			if (m_strikeTimer >= m_ctl.strikeInterval)
			{
				m_strikeTimer = 0.0f;
				StrikeOnce();
			}
		}
	}

	// Embers effect: stream from the emitter box while active (stacks on top of sparks).
	if (m_active[EFF_EMBERS])
		m_particles.EmitEmbers(XMFLOAT3(m_ctl.emberPos[0], m_ctl.emberPos[1], m_ctl.emberPos[2]),
		                       m_ctl.emberArea[0], m_ctl.emberArea[1],
		                       m_ctl.emberRate, m_ctl.emberRise, tick);

	m_particles.Update(tick, m_time);	// advance both pools (self-made physics)
}

void SceneParticleLab::Draw()
{
	DrawParticles();
}

//--- Build billboards for both pools into one buffer, then draw additively in one pass.
//    Sparks = velocity-stretched streaks; embers = round glowing dots. Same look as the game.
void SceneParticleLab::DrawParticles()
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("PLabVS");
	PixelShader*  ps  = GetObj<PixelShader>("PLabPS");
	if (!cam || !vs || !ps || !m_mesh) return;

	XMFLOAT3 camPos = cam->GetPos();
	XMVECTOR vcam    = XMLoadFloat3(&camPos);
	XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);

	XMFLOAT4X4 camMat[2];
	camMat[0] = cam->GetView();
	camMat[1] = cam->GetProj();
	vs->WriteBuffer(0, camMat);

	int v = 0;

	// -- Sparks: streaks stretched along the velocity direction --
	for (const Particles::Particle& s : m_particles.Sparks())
	{
		float t = s.life / s.maxLife;			// 1 -> 0
		XMFLOAT4 col;
		float br = t * t;						// fade to dark
		if (t > 0.5f) col = XMFLOAT4(1.0f, 0.9f * br + 0.1f, 0.5f * br, 1.0f);	// white-yellow
		else          col = XMFLOAT4(1.0f * br, 0.35f * br, 0.05f * br, 1.0f);	// orange-red

		XMVECTOR c   = XMLoadFloat3(&s.pos);
		XMVECTOR vel = XMLoadFloat3(&s.vel);
		float speed  = XMVectorGetX(XMVector3Length(vel));
		XMVECTOR dir   = (speed > 0.001f) ? XMVector3Normalize(vel) : XMVectorSet(0, 1, 0, 0);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));
		XMVECTOR side  = XMVector3Cross(dir, toCam);
		if (XMVectorGetX(XMVector3Length(side)) < 0.001f) side = XMVectorSet(1, 0, 0, 0);
		side = XMVector3Normalize(side);

		float halfLen = s.size * (0.6f + speed * 0.12f);	// faster = longer streak
		float halfWid = s.size * 0.35f;
		XMVECTOR L = XMVectorScale(dir, halfLen);
		XMVECTOR W = XMVectorScale(side, halfWid);

		XMFLOAT3 tl, tr, bl, br3;
		XMStoreFloat3(&tl,  XMVectorSubtract(XMVectorAdd(c, L), W));
		XMStoreFloat3(&tr,  XMVectorAdd(XMVectorAdd(c, L), W));
		XMStoreFloat3(&bl,  XMVectorSubtract(XMVectorSubtract(c, L), W));
		XMStoreFloat3(&br3, XMVectorAdd(XMVectorSubtract(c, L), W));

		Vertex* q = &m_vtx[v];
		q[0] = { tl,  XMFLOAT2(0,0), col };
		q[1] = { tr,  XMFLOAT2(1,0), col };
		q[2] = { bl,  XMFLOAT2(0,1), col };
		q[3] = { bl,  XMFLOAT2(0,1), col };
		q[4] = { tr,  XMFLOAT2(1,0), col };
		q[5] = { br3, XMFLOAT2(1,1), col };
		v += 6;
		if (v + 6 > (int)m_vtx.size()) break;
	}

	// -- Embers: camera-facing round dots (warm orange, flickering) --
	for (const Particles::Particle& e : m_particles.Embers())
	{
		if (v + 6 > (int)m_vtx.size()) break;
		float t  = e.life / e.maxLife;
		float fl = 0.70f + 0.30f * sinf(m_time * 25.0f + e.pos.x * 10.0f);
		float br = t * fl;
		XMFLOAT4 col(1.0f * br, (0.5f * t + 0.1f) * br, 0.12f * t * br, 1.0f);

		XMVECTOR c     = XMLoadFloat3(&e.pos);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));
		XMVECTOR right = XMVector3Cross(worldUp, toCam);
		if (XMVectorGetX(XMVector3Length(right)) < 0.001f) right = XMVectorSet(1, 0, 0, 0);
		right = XMVector3Normalize(right);
		XMVECTOR up = XMVector3Normalize(XMVector3Cross(toCam, right));

		float sz = e.size * (0.6f + 0.6f * t);
		XMVECTOR R = XMVectorScale(right, sz);
		XMVECTOR U = XMVectorScale(up,    sz);

		XMFLOAT3 tl, tr, bl, br3;
		XMStoreFloat3(&tl,  XMVectorAdd(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&tr,  XMVectorAdd(XMVectorAdd(c, R), U));
		XMStoreFloat3(&bl,  XMVectorSubtract(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&br3, XMVectorSubtract(XMVectorAdd(c, R), U));

		Vertex* q = &m_vtx[v];
		q[0] = { tl,  XMFLOAT2(0,0), col };
		q[1] = { tr,  XMFLOAT2(1,0), col };
		q[2] = { bl,  XMFLOAT2(0,1), col };
		q[3] = { bl,  XMFLOAT2(0,1), col };
		q[4] = { tr,  XMFLOAT2(1,0), col };
		q[5] = { br3, XMFLOAT2(1,1), col };
		v += 6;
	}

	if (v == 0) return;

	SetBlendMode(BLEND_ADD);				// additive = glowing fire
	SetDepthTest(DEPTH_ENABLE_TEST);		// read depth but do not write (transparent light)
	ps->SetTexture(0, m_glow.get());
	m_mesh->Write(m_vtx.data());
	vs->Bind();
	ps->Bind();
	m_mesh->Draw(v);

	SetBlendMode(BLEND_ALPHA);				// restore
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

//====================================================================================
//  startup snapshot: all Particles::tune physics + the lab's Controls, memory only.
//====================================================================================
void SceneParticleLab::SnapshotAll()
{
	m_tuneStartup = m_particles.tune;
	m_ctlStartup  = m_ctl;
	m_haveStartup = true;
}

void SceneParticleLab::RestoreAll()
{
	if (!m_haveStartup) return;
	m_particles.tune = m_tuneStartup;
	m_ctl            = m_ctlStartup;
}

// Display names for the effect list (indexed by Effect enum). Add new effects here.
static const char* kEffectNames[] = { "Sparks (strike)", "Embers (coal)" };

void SceneParticleLab::DrawUI()
{
	Particles::Tune& tn = m_particles.tune;	// short alias for the sliders

	ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360, 560), ImGuiCond_FirstUseEver);
	ImGui::Begin("Particle Lab");

	ImGui::Text("Sparks: %d   Embers: %d",
	            (int)m_particles.Sparks().size(), (int)m_particles.Embers().size());
	ImGui::TextDisabled("Cam: RMB fly (WASD) / ALT+LMB orbit / ALT+MMB pan / ALT+RMB zoom");

	// --- Active effects = a multi-select dropdown. Tick several to run them at once
	//     (e.g. later: fog + burning together). This is the "composite" control. ---
	{
		std::string sum;	// build the closed-combo preview, e.g. "Sparks (strike), Embers (coal)"
		for (int i = 0; i < EFF_COUNT; ++i)
			if (m_active[i]) { if (!sum.empty()) sum += ", "; sum += kEffectNames[i]; }
		if (sum.empty()) sum = "(none)";
		if (ImGui::BeginCombo("Active", sum.c_str()))
		{
			for (int i = 0; i < EFF_COUNT; ++i)
				ImGui::Checkbox(kEffectNames[i], &m_active[i]);	// tick multiple = stack them
			ImGui::EndCombo();
		}
	}

	// --- Edit = pick ONE effect to tune; only its parameters show below. ---
	ImGui::Combo("Edit", &m_editSel, kEffectNames, EFF_COUNT);

	if (ImGui::Button("Strike!  (Space)")) StrikeOnce();
	ImGui::SameLine();
	if (ImGui::Button("Reset ALL  (F8)")) RestoreAll();
	ImGui::Separator();

	// Show only the selected effect's controls (independent per effect).
	switch (m_editSel)
	{
	case EFF_SPARKS:
		ImGui::TextDisabled("-- Trigger --");
		ImGui::Checkbox("Auto strike", &m_ctl.autoStrike);
		ImGui::SliderFloat("Interval (s)", &m_ctl.strikeInterval, 0.1f, 3.0f, "%.2f");
		ImGui::SliderInt("Count / burst", &m_ctl.sparkCount, 1, 300);
		ImGui::SliderFloat("Power",  &m_ctl.sparkPower, 0.2f, 3.0f, "%.2f");
		ImGui::SliderFloat("Scale",  &m_ctl.sparkScale, 0.2f, 3.0f, "%.2f");
		ImGui::SliderFloat("Height", &m_ctl.strikeHeight, 0.0f, 2.0f, "%.2f");
		ImGui::TextDisabled("-- Spawn --");
		ImGui::SliderFloat("Speed min", &tn.sparkSpeedMin, 0.0f, 12.0f, "%.2f");
		ImGui::SliderFloat("Speed max", &tn.sparkSpeedMax, 0.0f, 12.0f, "%.2f");
		ImGui::SliderFloat("Elev min",  &tn.sparkElevMin, 0.0f, 1.57f, "%.2f");
		ImGui::SliderFloat("Elev max",  &tn.sparkElevMax, 0.0f, 1.57f, "%.2f");
		ImGui::SliderFloat("Up bonus min", &tn.sparkUpBonusMin, 0.0f, 6.0f, "%.2f");
		ImGui::SliderFloat("Up bonus max", &tn.sparkUpBonusMax, 0.0f, 6.0f, "%.2f");
		ImGui::SliderFloat("Life min", &tn.sparkLifeMin, 0.1f, 3.0f, "%.2f");
		ImGui::SliderFloat("Life max", &tn.sparkLifeMax, 0.1f, 3.0f, "%.2f");
		ImGui::SliderFloat("Size min", &tn.sparkSizeMin, 0.02f, 0.6f, "%.3f");
		ImGui::SliderFloat("Size max", &tn.sparkSizeMax, 0.02f, 0.6f, "%.3f");
		ImGui::TextDisabled("-- Motion --");
		ImGui::SliderFloat("Gravity",     &tn.sparkGravity, 0.0f, 30.0f, "%.2f");
		ImGui::SliderFloat("Restitution", &tn.sparkRestitution, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Friction",    &tn.sparkFriction, 0.0f, 1.0f, "%.2f");
		break;

	case EFF_EMBERS:
		ImGui::TextDisabled("-- Emitter --");
		ImGui::DragFloat3("Pos",  m_ctl.emberPos, 0.02f);
		ImGui::DragFloat2("Area", m_ctl.emberArea, 0.01f, 0.02f, 3.0f);
		ImGui::SliderFloat("Rate (/s)", &m_ctl.emberRate, 0.0f, 200.0f, "%.0f");
		ImGui::SliderFloat("Rise",      &m_ctl.emberRise, 0.1f, 3.0f, "%.2f");
		ImGui::TextDisabled("-- Spawn / Motion --");
		ImGui::SliderFloat("Life min", &tn.emberLifeMin, 0.2f, 5.0f, "%.2f");
		ImGui::SliderFloat("Life max", &tn.emberLifeMax, 0.2f, 5.0f, "%.2f");
		ImGui::SliderFloat("Size min", &tn.emberSizeMin, 0.005f, 0.2f, "%.3f");
		ImGui::SliderFloat("Size max", &tn.emberSizeMax, 0.005f, 0.2f, "%.3f");
		ImGui::SliderFloat("Drift",    &tn.emberDrift, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Rise jitter min", &tn.emberRiseJitterMin, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat("Rise jitter max", &tn.emberRiseJitterMax, 0.0f, 2.0f, "%.2f");
		ImGui::SliderFloat("Buoyancy", &tn.emberBuoyancy, 0.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Shimmer",  &tn.emberShimmer, 0.0f, 1.0f, "%.2f");
		break;
	}

	ImGui::End();
}

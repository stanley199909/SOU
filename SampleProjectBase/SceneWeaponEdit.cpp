#include "SceneWeaponEdit.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "CameraBase.h"
#include "Input.h"
#include "DebugUI.h"
#include "imgui/imgui.h"
#include <cmath>
#include <cstdio>

using namespace DirectX;

// Vertex-color shader (same idea as the game's glowing bar): output the vertex color.
static const char* g_wVS = R"EOT(
cbuffer Cam : register(b0){ float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float4 col:TEXCOORD1; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.col=v.col; return o; }
)EOT";
static const char* g_wPS = R"EOT(
struct PIN{ float4 pos:SV_POSITION; float4 col:TEXCOORD1; };
float4 main(PIN i):SV_TARGET{ return i.col; }
)EOT";

static const char* g_weaponFile = "Assets/weapon.txt";

void SceneWeaponEdit::Init()
{
	VertexShader* vs = CreateObj<VertexShader>("WpVS"); vs->Compile(g_wVS);
	PixelShader*  ps = CreateObj<PixelShader>("WpPS");  ps->Compile(g_wPS);

	m_vtx.resize(NL * NW * 60 + 64);	// per cell: top(12) + up to 4 two-sided walls(48)
	MeshBuffer::Description d = {};
	d.pVtx = m_vtx.data();
	d.vtxSize = sizeof(Vertex);
	d.vtxCount = (UINT)m_vtx.size();
	d.isWrite = true;
	d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_mesh = std::make_shared<MeshBuffer>(d);

	DefaultShapes();
	LoadWeapon();	// overwrite with saved design if present
}

void SceneWeaponEdit::Uninit()
{
	SaveWeapon();	// keep the design on exit
}

//--- fixed 3/4 overhead camera, spun by m_viewYaw (view only; data unaffected)
void SceneWeaponEdit::ApplyCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	// more top-down so the blade OUTLINE (the thing that makes it read as a dagger) is clear
	const float r = 1.3f, h = 3.0f;
	XMFLOAT3 pos(r * sinf(m_viewYaw), h, -r * cosf(m_viewYaw));
	cam->SetPos(pos);
	cam->SetLook(XMFLOAT3(0.0f, 0.0f, 0.0f));
	cam->SetUp(XMFLOAT3(0, 1, 0));
}

//--- Author a demo weapon: a straight steel bar (raw) forged into a dagger (finished),
//    with two in-between stages. i = length (0=tang butt .. NL-1=tip), j = width (center=spine).
//    This is just an example the user can look at and then repaint.
void SceneWeaponEdit::DefaultShapes()
{
	m_stageCount = 4;
	m_stage = 0;

	// stage 0: raw bar = a uniform straight rod (KCD starts from one steel bar). NO handle.
	float raw[NL][NW];
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j)
	{
		float cv = fabsf((j + 0.5f) / NW - 0.5f) * 2.0f;	// 0=center .. 1=edge
		raw[i][j] = (cv <= 0.55f) ? 0.7f : 0.0f;			// a straight bar down the middle, flat
	}

	// finished: a BLADE only (no tang/handle - KCD forges just the blade).
	// wide-ish blade the whole length, tapering to a point near the tip; center spine higher.
	float fin[NL][NW];
	for (int i = 0; i < NL; ++i)
	{
		float u = (i + 0.5f) / NL;					// 0 = base (butt), 1 = tip
		const float taper = 0.70f;					// only the last part narrows to a point
		float wfrac = (u < taper) ? 0.85f : 0.85f * (1.0f - powf((u - taper) / (1.0f - taper), 2.0f));
		float ridge = 0.72f * (1.0f - 0.35f * u);	// slightly thinner toward the tip
		for (int j = 0; j < NW; ++j)
		{
			float cv = fabsf((j + 0.5f) / NW - 0.5f) * 2.0f;	// 0=spine .. 1=edge
			if (cv <= wfrac) { float e = cv / (wfrac > 1e-4f ? wfrac : 1.0f);
			                   fin[i][j] = ridge * (1.0f - 0.55f * e); }	// spine high, edges thin
			else fin[i][j] = 0.0f;
		}
	}

	// stages: 0=raw, last=finished, middles = blend. Quantize to pen levels for a clean pixel look.
	for (int s = 0; s < m_stageCount; ++s)
	{
		float t = (m_stageCount > 1) ? s / (float)(m_stageCount - 1) : 1.0f;
		for (int i = 0; i < NL; ++i)
		for (int j = 0; j < NW; ++j)
		{
			float h = raw[i][j] + (fin[i][j] - raw[i][j]) * t;
			int lev = (int)(h * NLEV + 0.5f);			// snap to a pen level
			if (lev < 0) lev = 0; if (lev > NLEV) lev = NLEV;
			m_hw[s][i][j] = lev / (float)NLEV;
		}
	}
}

DirectX::XMFLOAT3 SceneWeaponEdit::CellPos(int i, int j, float h)
{
	float x = -m_halfW + 2.0f * m_halfW * (j / (float)NW);
	float z = -m_len * 0.5f + m_len * (i / (float)NL);
	return XMFLOAT3(x, h * m_dispScale, z);
}

void SceneWeaponEdit::CopyFromPrev()
{
	if (m_stage <= 0) return;
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j)
		m_hw[m_stage][i][j] = m_hw[m_stage - 1][i][j];
}

void SceneWeaponEdit::Update(float tick)
{
	m_time += tick;
	ApplyCamera();		// editing happens in the 2D paint grid (DrawUI), not in the 3D view
}

//--- Build a THIN solid blade: only cells with metal (h>thr) exist, so the footprint
//    IS the weapon outline (true silhouette, not a rectangle). Present cells get a smooth
//    top (averaged among present neighbors) and vertical side walls wherever they border an
//    empty cell or the grid edge = crisp dagger outline.
int SceneWeaponEdit::BuildMesh()
{
	const float thr = 0.03f;	// below this a cell counts as "no metal"
	auto has = [&](int i, int j) -> bool
	{
		return (i >= 0 && i < NL && j >= 0 && j < NW && m_hw[m_stage][i][j] > thr);
	};
	// corner height = average of the PRESENT cells touching this corner (empty cells excluded),
	// so the blade keeps its full thickness right up to a vertical edge.
	auto cornerH = [&](int i, int j) -> float
	{
		float s = 0; int n = 0;
		for (int di = -1; di <= 0; ++di)
		for (int dj = -1; dj <= 0; ++dj)
			if (has(i + di, j + dj)) { s += m_hw[m_stage][i + di][j + dj]; ++n; }
		return n ? s / n : 0.0f;
	};
	auto colOf = [&](float h) -> XMFLOAT4
	{
		if (h < 0) h = 0; if (h > 1) h = 1;
		return XMFLOAT4(0.20f + 0.80f * h, 0.15f + 0.60f * h, 0.16f + 0.22f * h, 1.0f);
	};
	auto CP = [&](int i, int j) -> XMFLOAT3 { return CellPos(i, j, cornerH(i, j)); };
	auto CC = [&](int i, int j) -> XMFLOAT4 { return colOf(cornerH(i, j)); };
	int v = 0;
	auto tri = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c,
	               const XMFLOAT4& ca, const XMFLOAT4& cb, const XMFLOAT4& cc)
	{
		m_vtx[v++] = { a, XMFLOAT2(0,0), ca };
		m_vtx[v++] = { b, XMFLOAT2(0,0), cb };
		m_vtx[v++] = { c, XMFLOAT2(0,0), cc };
	};
	auto quad2 = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT3& d,
	                 const XMFLOAT4& col)
	{
		tri(a, b, c, col, col, col); tri(a, c, d, col, col, col);
		tri(a, c, b, col, col, col); tri(a, d, c, col, col, col);	// two-sided
	};

	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j)
	{
		if (!has(i, j)) continue;	// empty cell: leave a hole so the outline shows
		// top face (smoothed via corner heights)
		XMFLOAT3 p00 = CP(i, j), p10 = CP(i + 1, j), p01 = CP(i, j + 1), p11 = CP(i + 1, j + 1);
		XMFLOAT4 c00 = CC(i, j), c10 = CC(i + 1, j), c01 = CC(i, j + 1), c11 = CC(i + 1, j + 1);
		tri(p00, p01, p11, c00, c01, c11); tri(p00, p11, p10, c00, c11, c10);
		tri(p00, p11, p01, c00, c11, c01); tri(p00, p10, p11, c00, c10, c11);

		// side walls only where the neighbor is empty (= the weapon's edge)
		XMFLOAT4 side = colOf(m_hw[m_stage][i][j] * 0.7f);
		XMFLOAT3 b00(p00.x, 0, p00.z), b10(p10.x, 0, p10.z), b01(p01.x, 0, p01.z), b11(p11.x, 0, p11.z);
		if (!has(i - 1, j)) quad2(p00, p01, b01, b00, side);	// -Z edge
		if (!has(i + 1, j)) quad2(p10, p11, b11, b10, side);	// +Z edge
		if (!has(i, j - 1)) quad2(p00, p10, b10, b00, side);	// -X edge
		if (!has(i, j + 1)) quad2(p01, p11, b11, b01, side);	// +X edge
	}
	return v;
}

void SceneWeaponEdit::Draw()
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("WpVS");
	PixelShader*  ps  = GetObj<PixelShader>("WpPS");
	if (!cam || !vs || !ps || !m_mesh) return;

	XMFLOAT4X4 cb[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, cb);

	int n = BuildMesh();
	if (n <= 0) return;
	m_mesh->Write(m_vtx.data());

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind(); ps->Bind();
	m_mesh->Draw(n);
}

//--- warm color for a height level (0=dark .. NLEV=bright), used by the 2D grid.
static ImU32 LevelColor(int lev, int nlev)
{
	float h = (nlev > 0) ? lev / (float)nlev : 0.0f;
	int r = (int)((0.12f + 0.88f * h) * 255);
	int g = (int)((0.10f + 0.62f * h) * 255);
	int b = (int)((0.12f + 0.20f * h) * 255);
	return IM_COL32(r, g, b, 255);
}

//--- top-down 2D pixel-art grid: pick a pen level, click/drag cells to paint precisely.
void SceneWeaponEdit::PaintGridUI()
{
	ImGui::Text("Paint (top-down). Tip at top. Left = pen, Right = erase(0).");
	ImGui::Checkbox("Mirror L/R (auto symmetry)", &m_mirror);

	// pen level palette
	ImGui::Text("Pen:");
	for (int lev = 0; lev <= NLEV; ++lev)
	{
		ImGui::SameLine();
		bool sel = (lev == m_pen);
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(LevelColor(lev, NLEV)));
		char lbl[8]; sprintf_s(lbl, sizeof(lbl), "%d", lev);
		if (ImGui::Button(lbl, ImVec2(28, 0))) m_pen = lev;
		ImGui::PopStyleColor();
		if (sel) { ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
		           ImGui::GetWindowDrawList()->AddRect(mn, mx, IM_COL32(255,255,255,255), 0, 0, 2.0f); }
	}

	// the grid: rows = length i (tip i=NL-1 at top), cols = width j
	const float cs = 22.0f;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 o = ImGui::GetCursorScreenPos();
	float gw = NW * cs, gh = NL * cs;
	ImGuiIO& io = ImGui::GetIO();

	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j)
	{
		int row = (NL - 1 - i);	// tip (large i) at the top
		ImVec2 a(o.x + j * cs, o.y + row * cs);
		ImVec2 b(a.x + cs - 1, a.y + cs - 1);
		int lev = (int)(m_hw[m_stage][i][j] * NLEV + 0.5f);
		dl->AddRectFilled(a, b, LevelColor(lev, NLEV));
		dl->AddRect(a, b, IM_COL32(0, 0, 0, 90));
	}
	// center spine guide line
	dl->AddLine(ImVec2(o.x + gw * 0.5f, o.y), ImVec2(o.x + gw * 0.5f, o.y + gh), IM_COL32(255,255,255,40));

	// make the grid an interactive area, then hit-test the mouse against it
	ImGui::InvisibleButton("paintgrid", ImVec2(gw, gh));
	if (ImGui::IsItemHovered() && (io.MouseDown[0] || io.MouseDown[1]))
	{
		int col = (int)((io.MousePos.x - o.x) / cs);
		int row = (int)((io.MousePos.y - o.y) / cs);
		if (col >= 0 && col < NW && row >= 0 && row < NL)
		{
			int i = NL - 1 - row, j = col;
			float val = io.MouseDown[1] ? 0.0f : (m_pen / (float)NLEV);
			m_hw[m_stage][i][j] = val;
			if (m_mirror) m_hw[m_stage][i][NW - 1 - j] = val;	// keep L/R symmetric
		}
	}
}

void SceneWeaponEdit::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(230, 0), ImGuiCond_Once);
	ImGui::Begin("Weapon Editor");

	// stage selector
	ImGui::SliderInt("Stages", &m_stageCount, 2, MAX_STAGE);
	if (m_stage >= m_stageCount) m_stage = m_stageCount - 1;
	ImGui::Text("Stage:");
	for (int s = 0; s < m_stageCount; ++s)
	{
		char lbl[8]; sprintf_s(lbl, sizeof(lbl), "%d", s + 1);
		if (s > 0) ImGui::SameLine();
		bool sel = (s == m_stage);
		if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
		if (ImGui::Button(lbl, ImVec2(28, 0))) m_stage = s;
		if (sel) ImGui::PopStyleColor();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", (m_stage == 0) ? "raw" :
	                          (m_stage == m_stageCount - 1) ? "finished" : "wip");

	if (ImGui::Button("Copy prev")) CopyFromPrev();
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
		for (int i = 0; i < NL; ++i) for (int j = 0; j < NW; ++j) m_hw[m_stage][i][j] = 0.0f;
	ImGui::SliderFloat("View spin", &m_viewYaw, -3.14f, 3.14f, "%.2f");
	if (ImGui::Button("SAVE")) SaveWeapon();
	ImGui::SameLine();
	if (ImGui::Button("Reload")) LoadWeapon();
	ImGui::SameLine();
	if (ImGui::Button("Load DEMO")) DefaultShapes();	// my authored raw->dagger example

	ImGui::Separator();
	PaintGridUI();
	ImGui::End();
}

void SceneWeaponEdit::SaveWeapon()
{
	FILE* fp = nullptr;
	if (fopen_s(&fp, g_weaponFile, "w") != 0 || !fp) return;
	fprintf(fp, "WEAPON %d %d %d\n", NL, NW, m_stageCount);
	for (int s = 0; s < m_stageCount; ++s)
	{
		fprintf(fp, "STAGE %d\n", s);
		for (int i = 0; i < NL; ++i)
		{
			for (int j = 0; j < NW; ++j) fprintf(fp, "%.3f ", m_hw[s][i][j]);
			fprintf(fp, "\n");
		}
	}
	fclose(fp);
}

void SceneWeaponEdit::LoadWeapon()
{
	FILE* fp = nullptr;
	if (fopen_s(&fp, g_weaponFile, "r") != 0 || !fp) return;
	int nl = 0, nw = 0, sc = 0;
	if (fscanf_s(fp, "WEAPON %d %d %d", &nl, &nw, &sc) != 3 || nl != NL || nw != NW)
	{
		fclose(fp); return;	// dims must match this editor's grid
	}
	if (sc < 1) sc = 1; if (sc > MAX_STAGE) sc = MAX_STAGE;
	m_stageCount = sc;
	for (int s = 0; s < sc; ++s)
	{
		int idx = 0;
		fscanf_s(fp, " STAGE %d", &idx);
		for (int i = 0; i < NL; ++i)
		for (int j = 0; j < NW; ++j)
		{
			float val = 0.0f;
			fscanf_s(fp, " %f", &val);
			if (val < 0) val = 0; if (val > 1) val = 1;
			m_hw[s][i][j] = val;
		}
	}
	fclose(fp);
	if (m_stage >= m_stageCount) m_stage = m_stageCount - 1;
}

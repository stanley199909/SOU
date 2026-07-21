#include "SceneFire.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "CameraBase.h"
#include "DebugUI.h"
#include <cstdlib>
#include <cmath>

using namespace DirectX;

//========================================================
// パーティクル用シェーダー(実行時コンパイル)
//========================================================
static const char* g_vsCode = R"EOT(
cbuffer Cam : register(b0)
{
	float4x4 view;
	float4x4 proj;
};
struct VIN  { float3 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
VOUT main(VIN v)
{
	VOUT o;
	o.pos = mul(float4(v.pos, 1.0f), view);
	o.pos = mul(o.pos, proj);
	o.uv  = v.uv;
	o.col = v.col;
	return o;
}
)EOT";

static const char* g_psCode = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
struct PIN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
float4 main(PIN i) : SV_TARGET
{
	return tex.Sample(samp, i.uv) * i.col;	// 光の粒 × 粒子色
}
)EOT";

//--- 乱数ヘルパー
static float frand()             { return (float)rand() / (float)RAND_MAX; }
static float frand(float a, float b) { return a + (b - a) * frand(); }

//--- 色の線形補間
static XMFLOAT4 lerpColor(const XMFLOAT4& a, const XMFLOAT4& b, float t)
{
	return XMFLOAT4(
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.z + (b.z - a.z) * t,
		a.w + (b.w - a.w) * t);
}

void SceneFire::Init()
{
	m_particles.reserve(MAX_PARTICLES);
	m_vtx.resize(MAX_PARTICLES * 6);

	// シェーダー作成
	VertexShader* vs = CreateObj<VertexShader>("VS_Fire");
	vs->Compile(g_vsCode);
	PixelShader* ps = CreateObj<PixelShader>("PS_Fire");
	ps->Compile(g_psCode);

	// 光の粒テクスチャ(中心が明るい放射状グラデ)を生成
	const int S = 64;
	std::vector<unsigned char> pix(S * S * 4);
	for (int y = 0; y < S; ++y)
	for (int x = 0; x < S; ++x)
	{
		float dx = (x + 0.5f) / S * 2.0f - 1.0f;
		float dy = (y + 0.5f) / S * 2.0f - 1.0f;
		float d = sqrtf(dx * dx + dy * dy);
		float f = 1.0f - d;
		if (f < 0.0f) f = 0.0f;
		f = f * f;					// 中心を強調した柔らかい減衰
		unsigned char c = (unsigned char)(f * 255.0f);
		int idx = (y * S + x) * 4;
		pix[idx + 0] = c;
		pix[idx + 1] = c;
		pix[idx + 2] = c;
		pix[idx + 3] = c;
	}
	m_glow = std::make_shared<Texture>();
	m_glow->Create(DXGI_FORMAT_R8G8B8A8_UNORM, S, S, pix.data());

	// 動的頂点バッファ作成
	MeshBuffer::Description desc = {};
	desc.pVtx = m_vtx.data();
	desc.vtxSize = sizeof(Vertex);
	desc.vtxCount = (UINT)m_vtx.size();
	desc.isWrite = true;
	desc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_mesh = std::make_shared<MeshBuffer>(desc);
}

void SceneFire::Uninit()
{
	DestroyObj("VS_Fire");
	DestroyObj("PS_Fire");
	m_mesh.reset();
	m_glow.reset();
	m_particles.clear();
}

void SceneFire::Emit(bool smoke)
{
	if ((int)m_particles.size() >= MAX_PARTICLES) return;

	Particle p = {};
	p.smoke = smoke; 
	if (!smoke) //true=煙 / false=炎
	{
		// 炎：足元の小さな円から上へ
		float a = frand(0.0f, 6.2832f); 
		float r = frand(0.0f, 0.18f); 
		p.pos = XMFLOAT3(cosf(a) * r, 0.0f, sinf(a) * r); 
		p.vel = XMFLOAT3(frand(-0.4f, 0.4f), frand(2.2f, 3.4f), frand(-0.4f, 0.4f)); 
		p.maxLife = frand(0.8f, 1.4f); 
		p.size = frand(0.35f, 0.6f); 
	}
	else
	{
		// 煙：炎の少し上から立ち上る
		float a = frand(0.0f, 6.2832f);
		float r = frand(0.0f, 0.3f);
		p.pos = XMFLOAT3(cosf(a) * r, frand(1.0f, 1.4f), sinf(a) * r);
		p.vel = XMFLOAT3(frand(-0.3f, 0.3f), frand(0.6f, 1.0f), frand(-0.3f, 0.3f));
		p.maxLife = frand(2.0f, 3.0f);
		p.size = frand(0.6f, 1.0f);
	}
	p.life = p.maxLife; 
	m_particles.push_back(p); 
}

void SceneFire::Update(float tick)
{
	m_time += tick;

	// --- 発生(フレーム時間に依らず一定量) ---
	m_fireAcc += tick * m_fireRate;
	while (m_fireAcc >= 1.0f) { Emit(false); m_fireAcc -= 1.0f; }
	m_smokeAcc += tick * m_smokeRate;
	while (m_smokeAcc >= 1.0f) { Emit(true); m_smokeAcc -= 1.0f; }

	// --- シミュレーション ---
	for (size_t i = 0; i < m_particles.size(); )
	{
		Particle& p = m_particles[i];
		p.life -= tick;
		if (p.life <= 0.0f)
		{
			// 死んだら末尾と入れ替えて削除(高速)
			p = m_particles.back();
			m_particles.pop_back();
			continue;
		}

		if (!p.smoke)
		{
			// 炎：上昇＋横揺れ(乱流)＋徐々に加速
			p.vel.x += sinf(m_time * 6.0f + p.pos.y * 5.0f) * tick * 1.6f;
			p.vel.z += cosf(m_time * 5.0f + p.pos.x * 5.0f) * tick * 1.6f;
			p.vel.y += tick * 1.2f * m_riseSpeed;	// 浮力
		}
		else
		{
			// 煙：ゆっくり揺れながら膨らむ
			p.vel.x += sinf(m_time * 1.5f + p.pos.y * 2.0f) * tick * 0.5f;
			p.size += tick * 0.9f;					// 広がる
		}
		p.pos.x += p.vel.x * tick;
		p.pos.y += p.vel.y * tick;
		p.pos.z += p.vel.z * tick;
		++i;
	}
}

void SceneFire::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(12, 480), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Fire Particles");

	ImGui::Text("Particles: %d", (int)m_particles.size());
	ImGui::Separator();
	ImGui::SliderFloat("Fire rate",  &m_fireRate,  0.0f, 2000.0f, "%.0f /s");
	ImGui::SliderFloat("Smoke rate", &m_smokeRate, 0.0f, 200.0f,  "%.0f /s");
	ImGui::SliderFloat("Rise", &m_riseSpeed, 0.0f, 3.0f);

	ImGui::End();
}

void SceneFire::BuildVertices(bool smoke, const XMFLOAT3& right, const XMFLOAT3& up, int& outCount)
{
	int v = 0;
	for (const Particle& p : m_particles)
	{
		if (p.smoke != smoke) continue;

		float t = p.life / p.maxLife;	// 1(誕生)→0(消滅)
		float hs = p.size * 0.5f;

		XMFLOAT4 col;
		if (!smoke)
		{
			// 炎の色：白黄 → 橙 → 赤 と変化し、消えるほど暗く
			XMFLOAT4 hot = XMFLOAT4(1.0f, 0.95f, 0.6f, 1.0f);	// 芯(白黄)
			XMFLOAT4 mid = XMFLOAT4(1.0f, 0.45f, 0.08f, 1.0f);	// 橙
			XMFLOAT4 cool = XMFLOAT4(0.7f, 0.06f, 0.02f, 1.0f);	// 赤
			if (t > 0.6f) col = lerpColor(mid, hot, (t - 0.6f) / 0.4f);
			else          col = lerpColor(cool, mid, t / 0.6f);
			float bright = t * t;			// 消えるほど急に暗く
			col.x *= bright; col.y *= bright; col.z *= bright;
			col.w = 1.0f;					// 加算合成なのでアルファは未使用
		}
		else
		{
			// 煙：灰色。寿命の中間で最も濃く、出現と消滅でフェード
			float u = 1.0f - t;				// 0→1
			float a = sinf(u * 3.14159f) * 0.28f;
			float g = 0.22f;
			col = XMFLOAT4(g, g, g, a);
		}

		// ビルボードの四隅
		XMFLOAT3 c = p.pos;
		XMFLOAT3 rx = XMFLOAT3(right.x * hs, right.y * hs, right.z * hs);
		XMFLOAT3 uy = XMFLOAT3(up.x * hs, up.y * hs, up.z * hs);
		XMFLOAT3 tl = XMFLOAT3(c.x - rx.x + uy.x, c.y - rx.y + uy.y, c.z - rx.z + uy.z);
		XMFLOAT3 tr = XMFLOAT3(c.x + rx.x + uy.x, c.y + rx.y + uy.y, c.z + rx.z + uy.z);
		XMFLOAT3 bl = XMFLOAT3(c.x - rx.x - uy.x, c.y - rx.y - uy.y, c.z - rx.z - uy.z);
		XMFLOAT3 br = XMFLOAT3(c.x + rx.x - uy.x, c.y + rx.y - uy.y, c.z + rx.z - uy.z);

		Vertex* q = &m_vtx[v];
		q[0] = { tl, XMFLOAT2(0,0), col };
		q[1] = { tr, XMFLOAT2(1,0), col };
		q[2] = { bl, XMFLOAT2(0,1), col };
		q[3] = { bl, XMFLOAT2(0,1), col };
		q[4] = { tr, XMFLOAT2(1,0), col };
		q[5] = { br, XMFLOAT2(1,1), col };
		v += 6;
		if (v + 6 > (int)m_vtx.size()) break;
	}
	outCount = v;
}

void SceneFire::Draw()
{
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	VertexShader* vs = GetObj<VertexShader>("VS_Fire");
	PixelShader*  ps = GetObj<PixelShader>("PS_Fire");
	if (!pCamera || !vs || !ps || !m_mesh) return;

	// カメラ基準のビルボード軸を作成
	XMFLOAT3 cp = pCamera->GetPos();
	XMFLOAT3 cl = pCamera->GetLook();
	XMVECTOR fwd = XMVector3Normalize(XMVectorSet(cl.x - cp.x, cl.y - cp.y, cl.z - cp.z, 0));
	XMVECTOR wup = XMVectorSet(0, 1, 0, 0);
	XMVECTOR rgt = XMVector3Normalize(XMVector3Cross(wup, fwd));
	XMVECTOR up  = XMVector3Normalize(XMVector3Cross(fwd, rgt));
	XMFLOAT3 right, upv;
	XMStoreFloat3(&right, rgt);
	XMStoreFloat3(&upv, up);

	// カメラ行列をシェーダーへ
	XMFLOAT4X4 cam[2];
	cam[0] = pCamera->GetView();
	cam[1] = pCamera->GetProj();
	vs->WriteBuffer(0, cam);

	// パーティクルは深度書き込みなしで描画
	SetDepthTest(DEPTH_ENABLE_TEST);
	ps->SetTexture(0, m_glow.get());

	// --- 煙(アルファ合成) → 炎(加算合成) の順に描画 ---
	int count = 0;

	SetBlendMode(BLEND_ALPHA);
	BuildVertices(true, right, upv, count);
	if (count > 0) { m_mesh->Write(m_vtx.data()); vs->Bind(); ps->Bind(); m_mesh->Draw(count); }

	SetBlendMode(BLEND_ADD);
	BuildVertices(false, right, upv, count);
	if (count > 0) { m_mesh->Write(m_vtx.data()); vs->Bind(); ps->Bind(); m_mesh->Draw(count); }

	// 状態を戻す
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

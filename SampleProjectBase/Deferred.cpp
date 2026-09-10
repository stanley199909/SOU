// GBuffer : minimal deferred-shading pipeline for opaque lit props.
// See Deferred.h for the design rationale. ASCII comments only (no BOM).
#include "Deferred.h"
#include "DirectX.h"
#include "Sprite.h"
#include "LightBase.h"
#include "CameraBase.h"

using namespace DirectX;

// -----------------------------------------------------------------------------
// Geometry pass shaders. Same vertex layout and same WVP convention as
// VS_Object.hlsl (world is transposed on the CPU, row-vector mul order), so
// props can feed the geometry pass with no change to how their matrices are
// written. The pixel shader writes 3 render targets instead of one color.
// -----------------------------------------------------------------------------
static const char* g_geomVS = R"EOT(
cbuffer WVP : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 proj;
};
struct VS_IN  { float3 pos:POSITION0; float3 normal:NORMAL0; float2 uv:TEXCOORD0; };
struct VS_OUT {
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 wnormal  : TEXCOORD1;
    float3 wpos     : TEXCOORD2;
};
VS_OUT main(VS_IN vin)
{
    VS_OUT vout;
    float4 wp = mul(float4(vin.pos, 1.0f), world);  // world position (row-vector)
    vout.wpos = wp.xyz;
    vout.pos  = mul(mul(wp, view), proj);
    vout.uv   = vin.uv;
    // rotation-only transform of the normal (same as VS_Object)
    vout.wnormal = mul(vin.normal, (float3x3)world);
    return vout;
}
)EOT";

static const char* g_geomPS = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
cbuffer Tint : register(b0) { float4 tint; };   // BaseColor multiplier (=white by default)
struct PS_IN {
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 wnormal  : TEXCOORD1;
    float3 wpos     : TEXCOORD2;
};
struct PS_OUT {
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 wpos     : SV_Target2;
};
PS_OUT main(PS_IN pin)
{
    PS_OUT o;
    float4 c   = tex.Sample(samp, pin.uv) * tint;
    o.albedo   = float4(c.rgb, 1.0f);
    o.normal   = float4(normalize(pin.wnormal), 0.0f);
    o.wpos     = float4(pin.wpos, 1.0f);
    return o;
}
)EOT";

// -----------------------------------------------------------------------------
// Deferred lighting pass. Fullscreen quad (Sprite) samples the 3 G-buffer
// targets and applies one directional light + ambient (Lambert). Specular /
// SSR come in a later stage. t0=albedo, t1=normal, t2=worldPos.
// -----------------------------------------------------------------------------
static const char* g_lightPS = R"EOT(
Texture2D    gAlbedo : register(t0);
Texture2D    gNormal : register(t1);
Texture2D    gWPos   : register(t2);
SamplerState samp    : register(s0);
cbuffer Light : register(b0)
{
    float3 lightDir;   float _p0;   // direction the light travels (world)
    float3 lightCol;   float _p1;
    float3 ambient;    float _p2;
    float3 camPos;     float _p3;
};
struct PS_IN { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 color:TEXCOORD1; };
float4 main(PS_IN pin) : SV_TARGET
{
    float4 alb = gAlbedo.Sample(samp, pin.uv);
    float4 nrm = gNormal.Sample(samp, pin.uv);

    // Pixels the geometry pass never wrote (normal.w == 0 and normal is zero):
    // pass the albedo through untouched so the cleared background stays as-is.
    float len = length(nrm.xyz);
    if (len < 0.001f) return float4(alb.rgb, 1.0f);

    float3 N = nrm.xyz / len;
    float3 L = normalize(-lightDir);           // from surface toward the light
    float  ndl = saturate(dot(N, L));
    float3 lit = alb.rgb * (ambient + lightCol * ndl);
    return float4(lit, 1.0f);
}
)EOT";

void GBuffer::Init(UINT width, UINT height)
{
	m_width  = width;
	m_height = height;

	m_albedo.Create(DXGI_FORMAT_R8G8B8A8_UNORM,     width, height);
	m_normal.Create(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height);
	m_worldPos.Create(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height);

	m_geomVS = std::make_shared<VertexShader>();
	m_geomVS->Compile(g_geomVS);
	m_geomPS = std::make_shared<PixelShader>();
	m_geomPS->Compile(g_geomPS);
	m_lightPS = std::make_shared<PixelShader>();
	m_lightPS->Compile(g_lightPS);
}

void GBuffer::Uninit()
{
	m_geomVS.reset();
	m_geomPS.reset();
	m_lightPS.reset();
}

void GBuffer::BeginGeometry(DepthStencil* pDSV)
{
	RenderTarget* rts[3] = { &m_albedo, &m_normal, &m_worldPos };
	SetRenderTargets(3, rts, pDSV);

	// Clear albedo to the same dark background PostProcess uses, normal/worldPos
	// to zero so the lighting pass can detect "never written" pixels.
	float bg[4]   = { 0.02f, 0.02f, 0.04f, 1.0f };
	float zero[4] = { 0.0f,  0.0f,  0.0f,  0.0f };
	m_albedo.Clear(bg);
	m_normal.Clear(zero);
	m_worldPos.Clear(zero);
	if (pDSV) pDSV->Clear();

	SetCullingMode(D3D11_CULL_NONE);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	SetBlendMode(BLEND_NONE);
	SetSamplerState(SAMPLER_LINEAR);
}

void GBuffer::Lighting(RenderTarget* pDst, LightBase* pLight, CameraBase* pCam)
{
	// GPU-side light parameters (16-byte aligned, matches cbuffer Light).
	struct LightParam
	{
		float dir[3];  float _p0;
		float col[3];  float _p1;
		float amb[3];  float _p2;
		float cam[3];  float _p3;
	} lp = {};

	if (pLight)
	{
		XMFLOAT3 d = pLight->GetDirection();
		XMFLOAT4 c = pLight->GetDiffuse();
		XMFLOAT4 a = pLight->GetAmbient();
		lp.dir[0] = d.x; lp.dir[1] = d.y; lp.dir[2] = d.z;
		lp.col[0] = c.x; lp.col[1] = c.y; lp.col[2] = c.z;
		lp.amb[0] = a.x; lp.amb[1] = a.y; lp.amb[2] = a.z;
	}
	else
	{
		// sane fallback so the pass still lights something if no light exists
		lp.dir[0] = 0.3f; lp.dir[1] = -0.8f; lp.dir[2] = 0.4f;
		lp.col[0] = lp.col[1] = lp.col[2] = 0.9f;
		lp.amb[0] = lp.amb[1] = lp.amb[2] = 0.25f;
	}
	if (pCam) { XMFLOAT3 e = pCam->GetPos(); lp.cam[0] = e.x; lp.cam[1] = e.y; lp.cam[2] = e.z; }
	m_lightPS->WriteBuffer(0, &lp);

	// Bind destination, no depth (fullscreen composite).
	SetRenderTargets(1, &pDst, nullptr);
	SetDepthTest(DEPTH_DISABLE);
	SetBlendMode(BLEND_NONE);
	SetSamplerState(SAMPLER_LINEAR);

	// t1/t2 must be bound manually (Sprite only sets t0). Same idiom the water
	// pass uses to read the depth SRV on t1.
	ID3D11ShaderResourceView* extra[2] = { m_normal.GetResource(), m_worldPos.GetResource() };
	GetContext()->PSSetShaderResources(1, 2, extra);

	// Fullscreen quad in clip space with the deferred-light PS; t0 = albedo.
	XMFLOAT4X4 ident; XMStoreFloat4x4(&ident, XMMatrixIdentity());
	Sprite::SetWorld(ident); Sprite::SetView(ident); Sprite::SetProjection(ident);
	Sprite::SetOffset(XMFLOAT2(0.0f, 0.0f));
	Sprite::SetSize(XMFLOAT2(2.0f, 2.0f));
	Sprite::SetColor(XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
	Sprite::SetTexture(&m_albedo);
	Sprite::SetPixelShader(m_lightPS.get());
	Sprite::Draw();

	// Release G-buffer SRVs so they can be render targets again next frame.
	ID3D11ShaderResourceView* nulls[3] = { nullptr, nullptr, nullptr };
	GetContext()->PSSetShaderResources(0, 3, nulls);
}

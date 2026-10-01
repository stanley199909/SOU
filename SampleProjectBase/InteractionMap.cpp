#include "InteractionMap.h"
#include "Sprite.h"
#include "DirectX.h"
#include <cmath>

using namespace DirectX;

namespace
{
    // The top-down camera floats above the area and looks down. Orthographic projection
    // has no perspective, so the height only has to keep everything between near and far.
    constexpr float CAMERA_HEIGHT = 100.0f;
    constexpr float CAMERA_NEAR   = 1.0f;
    constexpr float CAMERA_FAR    = 200.0f;

    ID3D11BlendState* MakeBlend(D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND_OP op)
    {
        D3D11_BLEND_DESC d = {};
        d.RenderTarget[0].BlendEnable           = TRUE;
        d.RenderTarget[0].SrcBlend              = src;
        d.RenderTarget[0].DestBlend             = dst;
        d.RenderTarget[0].BlendOp               = op;
        d.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        d.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
        d.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_MAX;
        d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ID3D11BlendState* state = nullptr;
        GetDevice()->CreateBlendState(&d, &state);
        return state;
    }
}

bool InteractionMap::Init(float minX, float minZ, float size, UINT resolution)
{
    Uninit();
    m_minX = minX; m_minZ = minZ; m_size = size; m_res = resolution;

    if (FAILED(m_rt.Create(DXGI_FORMAT_R16_FLOAT, resolution, resolution))) return false;
    const float zero[4] = { 0, 0, 0, 0 };
    m_rt.Clear(zero);

    m_stampPS = std::make_shared<PixelShader>();
    if (FAILED(m_stampPS->Load("Assets/Shader/PS_InteractStamp.cso"))) return false;

    // MAX: overlapping stamps keep the strongest pressure (no build-up while standing still).
    m_blendMax = MakeBlend(D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_MAX);
    // Multiply: result = src * 0 + dst * srcColor  ->  dst *= fade factor.
    m_blendMultiply = MakeBlend(D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_OP_ADD);
    if (!m_blendMax || !m_blendMultiply) return false;

    // The top-down orthographic camera: centred over the area, looking straight down (-Y).
    // "Up" on the texture is world +Z, so world +X is to the right.
    const float cx = minX + size * 0.5f, cz = minZ + size * 0.5f;
    XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(cx, CAMERA_HEIGHT, cz, 0),
                                     XMVectorSet(cx, 0.0f, cz, 0),
                                     XMVectorSet(0, 0, 1, 0));
    // Orthographic: a box size x size wide, no perspective divide effect.
    XMMATRIX proj = XMMatrixOrthographicLH(size, size, CAMERA_NEAR, CAMERA_FAR);
    XMStoreFloat4x4(&m_view, XMMatrixTranspose(view));   // shaders expect transposed matrices
    XMStoreFloat4x4(&m_proj, XMMatrixTranspose(proj));

    m_ready = true;
    return true;
}

void InteractionMap::Uninit()
{
    SAFE_RELEASE(m_blendMax);
    SAFE_RELEASE(m_blendMultiply);
    m_ready = false;
}

XMFLOAT4 InteractionMap::AreaParams() const
{
    return XMFLOAT4(m_minX, m_minZ, 1.0f / m_size, 1.0f / (float)m_res);
}

void InteractionMap::BeginPass()
{
    RenderTarget* rt = &m_rt;
    SetRenderTargets(1, &rt, nullptr);      // also sets the viewport to the map's size
    SetDepthTest(DEPTH_DISABLE);
    SetCullingMode(D3D11_CULL_NONE);
}

void InteractionMap::Fade(float dt)
{
    if (!m_ready || dt <= 0.0f) return;
    BeginPass();
    // Exact exponential decay over dt: value *= exp(-dt / T). Same result at 30 or 144 fps.
    const float keep = (recoverTime > 0.0f) ? expf(-dt / recoverTime) : 0.0f;

    // A full-screen quad (identity matrices, size 2 = clip space -1..1) of colour "keep",
    // drawn with the multiply blend.
    XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
    Sprite::SetWorld(id); Sprite::SetView(id); Sprite::SetProjection(id);
    Sprite::SetOffset(XMFLOAT2(0, 0));
    Sprite::SetSize(XMFLOAT2(2.0f, 2.0f));
    Sprite::SetColor(XMFLOAT4(keep, keep, keep, keep));
    Sprite::SetTexture(nullptr);            // default white texture: output = colour
    Sprite::SetPixelShader(nullptr);
    GetContext()->OMSetBlendState(m_blendMultiply, nullptr, 0xffffffff);
    Sprite::Draw();
}

void InteractionMap::Stamp(float x, float z, float radius)
{
    if (!m_ready || radius <= 0.0f) return;
    BeginPass();
    // Sprite's quad lies in the XY plane. Lay it flat on the ground (rotate +90 deg about X:
    // local y -> world z), size it to the stamp diameter and move it to (x, z).
    XMMATRIX world = XMMatrixRotationX(XM_PIDIV2) * XMMatrixTranslation(x, 0.0f, z);
    XMFLOAT4X4 w; XMStoreFloat4x4(&w, XMMatrixTranspose(world));
    Sprite::SetWorld(w); Sprite::SetView(m_view); Sprite::SetProjection(m_proj);
    Sprite::SetOffset(XMFLOAT2(0, 0));
    Sprite::SetSize(XMFLOAT2(radius * 2.0f, radius * 2.0f));
    Sprite::SetColor(XMFLOAT4(1, 1, 1, 1));
    Sprite::SetTexture(nullptr);
    Sprite::SetPixelShader(m_stampPS.get());
    GetContext()->OMSetBlendState(m_blendMax, nullptr, 0xffffffff);
    Sprite::Draw();
    Sprite::SetPixelShader(nullptr);        // Sprite is shared: give the default shader back
}

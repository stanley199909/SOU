#include "VolumetricLight.h"
#include "DirectX.h"
#include "Texture.h"
#include "Shader.h"
#include "CameraBase.h"
#include "LitRender.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace DirectX;

namespace
{
    const UINT        DOWNSAMPLE   = 4;                                // march at 1/4 width and height = 1/16 of the pixels
    const DXGI_FORMAT VOL_FORMAT   = DXGI_FORMAT_R16G16B16A16_FLOAT;   // HDR light
    const XMFLOAT3    AIR_SUN_COLOR(1.0f, 0.95f, 0.85f);               // sunlight scattered by dusty, smoky air: slightly warm

    struct State
    {
        VolumetricLight::Settings settings;
        std::unique_ptr<RenderTarget> volRT;
        UINT volW = 0, volH = 0;
        ID3D11VertexShader* fullscreenVS = nullptr;   // raw: it has no vertex input (the Shader class needs one)
        std::unique_ptr<PixelShader> marchPS;
        std::unique_ptr<PixelShader> compositePS;
        bool tried = false;
    } s;

    bool CreateShaders()
    {
        if (s.tried) return s.fullscreenVS && s.marchPS && s.compositePS;
        s.tried = true;
        std::ifstream f("Assets/Shader/VS_Fullscreen.cso", std::ios::binary);
        std::vector<char> code((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (code.empty() || FAILED(GetDevice()->CreateVertexShader(code.data(), code.size(), nullptr, &s.fullscreenVS))) return false;
        s.marchPS = std::make_unique<PixelShader>();
        s.compositePS = std::make_unique<PixelShader>();
        if (FAILED(s.marchPS->Load("Assets/Shader/PS_VolumetricMarch.cso")) ||
            FAILED(s.compositePS->Load("Assets/Shader/PS_VolumetricComposite.cso")))
        {
            s.marchPS.reset(); s.compositePS.reset();
            return false;
        }
        return true;
    }

    // The quarter-resolution target follows the scene size (window resize).
    bool EnsureTarget(UINT sceneW, UINT sceneH)
    {
        const UINT w = sceneW / DOWNSAMPLE > 0 ? sceneW / DOWNSAMPLE : 1;
        const UINT h = sceneH / DOWNSAMPLE > 0 ? sceneH / DOWNSAMPLE : 1;
        if (s.volRT && s.volW == w && s.volH == h) return true;
        s.volRT = std::make_unique<RenderTarget>();
        if (FAILED(s.volRT->Create(VOL_FORMAT, w, h))) { s.volRT.reset(); return false; }
        s.volW = w; s.volH = h;
        return true;
    }

    void DrawFullscreenTriangle()
    {
        ID3D11DeviceContext* ctx = GetContext();
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(s.fullscreenVS, nullptr, 0);
        ctx->Draw(3, 0);
    }
}

namespace VolumetricLight
{
    Settings& Get() { return s.settings; }

    void Render(CameraBase* camera, RenderTarget* sceneRT, DepthStencil* depth, const XMFLOAT3& roomMin, const XMFLOAT3& roomMax)
    {
        if (!camera || !sceneRT || !depth || !CreateShaders()) return;
        if (!EnsureTarget(sceneRT->GetWidth(), sceneRT->GetHeight())) return;
        const SceneLightParams light = LitRender::Lights(camera);   // sun = F2-switch aware (off -> no beam)
        if (light.sun.w <= 0.0f) return;

        // --- pass 1: march (quarter resolution) ---
        struct MarchParams
        {
            XMFLOAT4X4 invViewProj, lightVP;
            XMFLOAT4 eye, sunDir, shadow, medium, boxMin, boxMax, airColor;
        } p{};
        static_assert(sizeof(MarchParams) % 16 == 0, "cbuffer size must be a multiple of 16 bytes");
        const XMFLOAT4X4 view = camera->GetView(false), proj = camera->GetProj(false);
        const XMMATRIX viewProj = XMLoadFloat4x4(&view) * XMLoadFloat4x4(&proj);
        XMStoreFloat4x4(&p.invViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, viewProj)));
        p.lightVP = light.lightVP;
        const XMFLOAT3 eye = camera->GetPos();
        p.eye      = XMFLOAT4(eye.x, eye.y, eye.z, 0.0f);
        p.sunDir   = light.sun;
        p.shadow   = light.shadow;
        p.medium   = XMFLOAT4(s.settings.density, s.settings.anisotropy, (float)s.settings.steps, s.settings.maxDistance);
        p.boxMin   = XMFLOAT4(roomMin.x, roomMin.y, roomMin.z, (float)depth->GetWidth());
        p.boxMax   = XMFLOAT4(roomMax.x, roomMax.y, roomMax.z, (float)depth->GetHeight());
        p.airColor = XMFLOAT4(AIR_SUN_COLOR.x, AIR_SUN_COLOR.y, AIR_SUN_COLOR.z, 0.0f);

        RenderTarget* vol = s.volRT.get();
        SetRenderTargets(1, &vol, nullptr);   // depth is READ below, so it must not stay bound as the depth target
        SetBlendMode(BLEND_NONE);
        SetDepthTest(DEPTH_DISABLE);
        s.marchPS->WriteBuffer(0, &p);
        s.marchPS->SetTexture(0, depth);
        s.marchPS->Bind();
        LitRender::BindShadow();              // t3 = sun shadow map
        DrawFullscreenTriangle();
        s.marchPS->SetTexture(0, nullptr);    // stop reading depth before it becomes the depth target again

        // --- pass 2: blur + add onto the scene ---
        const XMFLOAT4 c(1.0f / s.volW, 1.0f / s.volH, s.settings.strength, 0.0f);
        SetRenderTargets(1, &sceneRT, nullptr);
        SetBlendMode(BLEND_ADD);
        s.compositePS->WriteBuffer(0, (void*)&c);
        s.compositePS->SetTexture(0, vol);
        s.compositePS->Bind();
        DrawFullscreenTriangle();
        s.compositePS->SetTexture(0, nullptr);

        // back to the normal state for the particles drawn next
        SetRenderTargets(1, &sceneRT, depth);
        SetBlendMode(BLEND_ALPHA);
        SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
    }

    void Uninit()
    {
        s.volRT.reset();
        s.marchPS.reset();
        s.compositePS.reset();
        if (s.fullscreenVS) { s.fullscreenVS->Release(); s.fullscreenVS = nullptr; }
        s.volW = s.volH = 0;
        s.tried = false;
    }
}

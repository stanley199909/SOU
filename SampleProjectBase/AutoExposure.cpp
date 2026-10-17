#include "AutoExposure.h"
#include "DirectX.h"
#include "Texture.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    const UINT        LUM_W = 256, LUM_H = 128;      // small copy of the frame: enough for an average, cheap to make
    const DXGI_FORMAT LUM_FORMAT = DXGI_FORMAT_R16_FLOAT;   // one number per pixel (log luminance can be negative)
    const UINT        READBACK_FRAMES = 3;           // ring of CPU-readable copies: read the one the GPU finished long ago
    const float       MIN_TIME_CONSTANT = 0.001f;    // guard against dividing by zero

    template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct State
    {
        AutoExposure::Settings settings;
        ID3D11Texture2D*          lumTex = nullptr;
        ID3D11RenderTargetView*   lumRTV = nullptr;
        ID3D11ShaderResourceView* lumSRV = nullptr;
        UINT                      lumMips = 1;
        ID3D11Texture2D*          readback[READBACK_FRAMES] = {};
        bool                      written[READBACK_FRAMES] = {};
        UINT                      frame = 0;
        ID3D11VertexShader*       fullscreenVS = nullptr;
        ID3D11PixelShader*        logLumPS = nullptr;
        bool  tried = false;
        bool  hasMeasure = false;
        float measuredLum = 0.18f;   // last average brightness read back
        float exposure = 1.0f;
    } s;

    bool LoadShader(const char* path, std::vector<char>& code)
    {
        std::ifstream f(path, std::ios::binary);
        code.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !code.empty();
    }

    bool Create()
    {
        if (s.tried) return s.lumTex && s.fullscreenVS && s.logLumPS;
        s.tried = true;
        ID3D11Device* dev = GetDevice();
        std::vector<char> code;
        if (!LoadShader("Assets/Shader/VS_Fullscreen.cso", code) ||
            FAILED(dev->CreateVertexShader(code.data(), code.size(), nullptr, &s.fullscreenVS))) return false;
        if (!LoadShader("Assets/Shader/PS_LogLuminance.cso", code) ||
            FAILED(dev->CreatePixelShader(code.data(), code.size(), nullptr, &s.logLumPS))) return false;

        D3D11_TEXTURE2D_DESC d = {};
        d.Width = LUM_W; d.Height = LUM_H;
        d.MipLevels = 0;   // full chain, down to 1x1 = the average
        d.ArraySize = 1;
        d.Format = LUM_FORMAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &s.lumTex))) return false;
        s.lumTex->GetDesc(&d);
        s.lumMips = d.MipLevels;
        D3D11_RENDER_TARGET_VIEW_DESC r = {};
        r.Format = LUM_FORMAT;
        r.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateRenderTargetView(s.lumTex, &r, &s.lumRTV))) return false;
        if (FAILED(dev->CreateShaderResourceView(s.lumTex, nullptr, &s.lumSRV))) return false;

        D3D11_TEXTURE2D_DESC rb = {};
        rb.Width = rb.Height = 1;
        rb.MipLevels = rb.ArraySize = 1;
        rb.Format = LUM_FORMAT;
        rb.SampleDesc.Count = 1;
        rb.Usage = D3D11_USAGE_STAGING;
        rb.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (UINT i = 0; i < READBACK_FRAMES; ++i)
            if (FAILED(dev->CreateTexture2D(&rb, nullptr, &s.readback[i]))) return false;
        return true;
    }
}

namespace AutoExposure
{
    Settings& Get() { return s.settings; }

    void Measure(Texture* hdrScene)
    {
        if (!s.settings.enabled || !hdrScene || !hdrScene->GetResource() || !Create()) return;
        ID3D11DeviceContext* ctx = GetContext();

        // remember the bound target (PostProcess draws on afterwards)
        ID3D11RenderTargetView* prevRTV = nullptr;
        ID3D11DepthStencilView* prevDSV = nullptr;
        ctx->OMGetRenderTargets(1, &prevRTV, &prevDSV);
        UINT vpCount = 1;
        D3D11_VIEWPORT prevVP = {};
        ctx->RSGetViewports(&vpCount, &prevVP);

        // 1) log luminance into the small image
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)LUM_W, (float)LUM_H, 0.0f, 1.0f };
        ctx->OMSetRenderTargets(1, &s.lumRTV, nullptr);
        ctx->RSSetViewports(1, &vp);
        SetBlendMode(BLEND_NONE);
        SetDepthTest(DEPTH_DISABLE);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(s.fullscreenVS, nullptr, 0);
        ctx->PSSetShader(s.logLumPS, nullptr, 0);
        ID3D11ShaderResourceView* src = hdrScene->GetResource();
        ctx->PSSetShaderResources(0, 1, &src);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(0, 1, &none);

        // 2) mip chain -> the 1x1 level is the average
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ctx->GenerateMips(s.lumSRV);

        // 3) copy the 1x1 to this frame's readback slot; read the oldest slot if the GPU is done with it
        const UINT write = s.frame % READBACK_FRAMES;
        ctx->CopySubresourceRegion(s.readback[write], 0, 0, 0, 0, s.lumTex, s.lumMips - 1, nullptr);
        s.written[write] = true;
        const UINT read = (s.frame + 1) % READBACK_FRAMES;   // written READBACK_FRAMES-1 frames ago
        if (s.written[read])
        {
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (ctx->Map(s.readback[read], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) == S_OK)
            {
                const float logAvg = DirectX::PackedVector::XMConvertHalfToFloat(*(const DirectX::PackedVector::HALF*)m.pData);
                ctx->Unmap(s.readback[read], 0);
                s.measuredLum = expf(logAvg);
                s.hasMeasure = true;
            }
        }
        ++s.frame;

        ctx->OMSetRenderTargets(1, &prevRTV, prevDSV);
        ctx->RSSetViewports(1, &prevVP);
        if (prevRTV) prevRTV->Release();
        if (prevDSV) prevDSV->Release();
    }

    void Update(float dt)
    {
        const Settings& st = s.settings;
        if (!st.enabled) { s.exposure = 1.0f; return; }
        if (!s.hasMeasure) return;
        float target = st.key / fmaxf(s.measuredLum, 1e-4f);
        target = fminf(fmaxf(target, st.minExposure), st.maxExposure);
        // adapt in log space (the eye responds to ratios: 1 -> 2 feels like 2 -> 4)
        const float tau = target < s.exposure ? st.toBrightSec : st.toDarkSec;   // lower exposure = the scene got brighter
        const float k = 1.0f - expf(-dt / fmaxf(tau, MIN_TIME_CONSTANT));         // frame-rate independent
        s.exposure = expf(logf(s.exposure) + (logf(target) - logf(s.exposure)) * k);
    }

    float Exposure() { return s.settings.enabled ? s.exposure : 1.0f; }

    void Uninit()
    {
        for (auto& r : s.readback) SafeRelease(r);
        SafeRelease(s.lumRTV);
        SafeRelease(s.lumSRV);
        SafeRelease(s.lumTex);
        SafeRelease(s.fullscreenVS);
        SafeRelease(s.logLumPS);
        for (auto& w : s.written) w = false;
        s.tried = false; s.hasMeasure = false; s.frame = 0; s.exposure = 1.0f;
    }
}

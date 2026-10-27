#include "ReflectionProbe.h"
#include "DirectX.h"
#include "CameraBase.h"
#include "Geometory.h"
#include "LitRender.h"   // LightToggles (F2 light switches)
#include <DirectXPackedVector.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

using namespace DirectX;

namespace
{
    template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    // The 6 faces in the order D3D11 stores them (+X, -X, +Y, -Y, +Z, -Z), with the "up" each
    // face must use so that sampling the cube with a world direction returns that direction.
    // (Left-handed world, like the rest of the engine: XMMatrixLookAtLH.)
    const XMFLOAT3 FACE_DIR[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const XMFLOAT3 FACE_UP[6]  = { { 0, 1, 0 }, {  0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1,  0 } };
    const float    FACE_FOV    = XM_PIDIV2;   // 90 degrees: the 6 faces together cover every direction exactly once
    const float    FACE_ASPECT = 1.0f;        // square faces

    const DXGI_FORMAT CUBE_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;   // HDR, same as the scene render target
    const DXGI_FORMAT LUT_FORMAT  = DXGI_FORMAT_R16G16_FLOAT;         // two numbers per texel (A, B)
    const UINT        LUT_SIZE    = 128;                              // n.v x roughness; smooth, so small is enough

    // Data texture read by the lit shaders (Lighting.hlsli iblBox): 0 probe, 1 box min, 2 box max, 3..11 SH
    const UINT SH_FIRST_TEXEL = 3;
    const UINT BOX_TEXELS     = SH_FIRST_TEXEL + 9;

    struct State
    {
        // step 1: the photo (captured cube, plain box-filtered mips used as the bake's source)
        ID3D11Texture2D*          cube = nullptr;
        ID3D11RenderTargetView*   faceRTV[6] = {};
        ID3D11ShaderResourceView* srv = nullptr;
        ID3D11Texture2D*          depth = nullptr;
        ID3D11DepthStencilView*   dsv = nullptr;
        UINT                      mipCount = 1;
        // step 2a: prefiltered cube (mip m = roughness m / (PREFILTER_MIPS-1))
        ID3D11Texture2D*          pre = nullptr;
        ID3D11RenderTargetView*   preRTV[6][ReflectionProbe::PREFILTER_MIPS] = {};
        ID3D11ShaderResourceView* preSRV = nullptr;
        // step 2b: BRDF LUT (scene independent -> baked once per run)
        ID3D11Texture2D*          lut = nullptr;
        ID3D11RenderTargetView*   lutRTV = nullptr;
        ID3D11ShaderResourceView* lutSRV = nullptr;
        bool                      lutBaked = false;
        // bake shaders (raw D3D objects: the fullscreen VS has no vertex input, which the
        // engine's Shader class cannot build an input layout for)
        ID3D11VertexShader*       fullscreenVS = nullptr;
        ID3D11PixelShader*        prefilterPS = nullptr;
        ID3D11PixelShader*        lutPS = nullptr;
        ID3D11Buffer*             bakeCB = nullptr;
        ID3D11SamplerState*       iblSampler = nullptr;
        // step 4: box projection data for the shaders (3 texels, see Lighting.hlsli iblBox)
        ID3D11Texture2D*          boxTex = nullptr;
        ID3D11ShaderResourceView* boxSRV = nullptr;
        XMFLOAT3 boxMin{}, boxMax{};
        bool     hasBox        = false;
        bool     boxDirty      = true;
        bool     boxProjection = true;   // off = look the reflection up from the probe as-is (was an F1 comparison switch)
        bool     showBox       = false;  // F4 visualize section: draw the room box as lines

        bool     useInLighting = true;   // off = the old flat room color (was an F1 comparison switch)
        // diffuse IBL (irradiance SH)
        XMFLOAT3 sh[9] = {};
        bool     hasSH           = false;
        bool     useDiffuse      = true;   // off = the flat "Indoor fill" diffuse (was an F1 comparison switch)
        float    diffuseStrength = 1.0f;   // artist lift (1 = physical). The room is dark, so this may be raised
        bool     uploadedDirection = true, uploadedReflections = true;   // F2 switches as last sent to the GPU
        bool     capturing       = false;  // while capturing, lit shaders get NO IBL (each capture must not include the previous one)
        bool     captured  = false;
        bool     recapture = false;
        XMFLOAT3 pos{};
    } s;

    bool ReadFile(const char* path, std::vector<char>& out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out.empty();
    }

    bool CreateBakeShaders()
    {
        if (s.fullscreenVS && s.prefilterPS && s.lutPS) return true;
        ID3D11Device* dev = GetDevice();
        std::vector<char> code;
        if (!ReadFile("Assets/Shader/VS_Fullscreen.cso", code) ||
            FAILED(dev->CreateVertexShader(code.data(), code.size(), nullptr, &s.fullscreenVS))) return false;
        if (!ReadFile("Assets/Shader/PS_IblPrefilter.cso", code) ||
            FAILED(dev->CreatePixelShader(code.data(), code.size(), nullptr, &s.prefilterPS))) return false;
        if (!ReadFile("Assets/Shader/PS_IblBrdfLut.cso", code) ||
            FAILED(dev->CreatePixelShader(code.data(), code.size(), nullptr, &s.lutPS))) return false;

        D3D11_BUFFER_DESC b = {};
        b.ByteWidth = sizeof(XMFLOAT4);
        b.Usage = D3D11_USAGE_DEFAULT;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev->CreateBuffer(&b, nullptr, &s.bakeCB))) return false;

        // Own sampler for the cubes / LUT: trilinear (blends between blur levels), clamp, and NO LOD
        // bias -- the engine's default sampler adds +1 mip bias against texture shimmer, which would
        // make every reflection one roughness step blurrier than the material says.
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        return SUCCEEDED(dev->CreateSamplerState(&sd, &s.iblSampler));
    }

    bool CreateResources()
    {
        if (s.cube) return true;
        ID3D11Device* dev = GetDevice();

        // --- captured cube ---
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = ReflectionProbe::FACE_SIZE;
        d.MipLevels = 0;                                  // full mip chain (lets the bake read "averaged" texels)
        d.ArraySize = 6;
        d.Format = CUBE_FORMAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &s.cube))) return false;
        s.cube->GetDesc(&d);
        s.mipCount = d.MipLevels;

        for (UINT i = 0; i < 6; ++i)
        {
            D3D11_RENDER_TARGET_VIEW_DESC r = {};
            r.Format = CUBE_FORMAT;
            r.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            r.Texture2DArray.MipSlice = 0;
            r.Texture2DArray.FirstArraySlice = i;
            r.Texture2DArray.ArraySize = 1;
            if (FAILED(dev->CreateRenderTargetView(s.cube, &r, &s.faceRTV[i]))) return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
        v.Format = CUBE_FORMAT;
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        v.TextureCube.MostDetailedMip = 0;
        v.TextureCube.MipLevels = (UINT)-1;
        if (FAILED(dev->CreateShaderResourceView(s.cube, &v, &s.srv))) return false;

        D3D11_TEXTURE2D_DESC z = {};
        z.Width = z.Height = ReflectionProbe::FACE_SIZE;
        z.MipLevels = 1;
        z.ArraySize = 1;
        z.Format = DXGI_FORMAT_D32_FLOAT;
        z.SampleDesc.Count = 1;
        z.Usage = D3D11_USAGE_DEFAULT;
        z.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(dev->CreateTexture2D(&z, nullptr, &s.depth))) return false;
        if (FAILED(dev->CreateDepthStencilView(s.depth, nullptr, &s.dsv))) return false;

        // --- prefiltered cube: one render target per (face, mip) ---
        D3D11_TEXTURE2D_DESC p = d;
        p.MipLevels = ReflectionProbe::PREFILTER_MIPS;
        p.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (FAILED(dev->CreateTexture2D(&p, nullptr, &s.pre))) return false;
        for (UINT f = 0; f < 6; ++f)
            for (UINT m = 0; m < ReflectionProbe::PREFILTER_MIPS; ++m)
            {
                D3D11_RENDER_TARGET_VIEW_DESC r = {};
                r.Format = CUBE_FORMAT;
                r.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                r.Texture2DArray.MipSlice = m;
                r.Texture2DArray.FirstArraySlice = f;
                r.Texture2DArray.ArraySize = 1;
                if (FAILED(dev->CreateRenderTargetView(s.pre, &r, &s.preRTV[f][m]))) return false;
            }
        if (FAILED(dev->CreateShaderResourceView(s.pre, &v, &s.preSRV))) return false;

        // --- BRDF LUT ---
        D3D11_TEXTURE2D_DESC l = {};
        l.Width = l.Height = LUT_SIZE;
        l.MipLevels = 1;
        l.ArraySize = 1;
        l.Format = LUT_FORMAT;
        l.SampleDesc.Count = 1;
        l.Usage = D3D11_USAGE_DEFAULT;
        l.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&l, nullptr, &s.lut))) return false;
        if (FAILED(dev->CreateRenderTargetView(s.lut, nullptr, &s.lutRTV))) return false;
        if (FAILED(dev->CreateShaderResourceView(s.lut, nullptr, &s.lutSRV))) return false;
        return CreateBakeShaders();
    }

    // Common state for the fullscreen-triangle bakes.
    void BeginBake()
    {
        ID3D11DeviceContext* ctx = GetContext();
        SetBlendMode(BLEND_NONE);
        SetDepthTest(DEPTH_DISABLE);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(s.fullscreenVS, nullptr, 0);
        ctx->PSSetSamplers(1, 1, &s.iblSampler);
    }
    void DrawFullscreen(ID3D11RenderTargetView* rtv, UINT size)
    {
        ID3D11DeviceContext* ctx = GetContext();
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)size, (float)size, 0.0f, 1.0f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->Draw(3, 0);
    }

    // Step 2a: blur the photo once per roughness level (GGX-shaped blur, PS_IblPrefilter).
    void Prefilter()
    {
        ID3D11DeviceContext* ctx = GetContext();
        BeginBake();
        ctx->PSSetShader(s.prefilterPS, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &s.srv);
        ctx->PSSetConstantBuffers(0, 1, &s.bakeCB);
        for (UINT m = 0; m < ReflectionProbe::PREFILTER_MIPS; ++m)
        {
            const UINT size = ReflectionProbe::FACE_SIZE >> m;
            for (UINT f = 0; f < 6; ++f)
            {
                XMFLOAT4 cb(ReflectionProbe::MipToRoughness((float)m), (float)f, (float)ReflectionProbe::FACE_SIZE, (float)s.mipCount);
                ctx->UpdateSubresource(s.bakeCB, 0, nullptr, &cb, 0, 0);
                DrawFullscreen(s.preRTV[f][m], size);
            }
        }
        ID3D11ShaderResourceView* none[1] = { nullptr };
        ctx->PSSetShaderResources(0, 1, none);
    }

    // Step 2b: the BRDF half of the split-sum (scene independent, PS_IblBrdfLut).
    void BakeBrdfLut()
    {
        BeginBake();
        GetContext()->PSSetShader(s.lutPS, nullptr, 0);
        DrawFullscreen(s.lutRTV, LUT_SIZE);
        s.lutBaked = true;
    }

    // Step 4: write probe position + room box into the 3-texel texture the shaders read (t8).
    // ---- Diffuse IBL: irradiance as 9 spherical-harmonics coefficients (Ramamoorthi & Hanrahan 2001) ----
    // A rough/diffuse surface receives light from its WHOLE hemisphere, so the result changes very slowly
    // with the normal. 9 numbers per color (SH order 2) reproduce it within ~1-3%: no blurred cube needed.
    // Projection = sum over every texel of the captured cube: radiance x basis function x texel solid angle.
    // Search: "spherical harmonics irradiance", "Ramamoorthi Hanrahan" / JP: kyumen chowa kansu
    const UINT SH_SOURCE_MIP = 2;   // 64 -> 16 px per face: plenty for something this smooth, cheap to read back

    // Direction through texel (u,v in -1..1) of a face, D3D layout (same table as PS_IblPrefilter).
    XMFLOAT3 FaceDirection(float a, float b, int f)
    {
        switch (f)
        {
        case 0:  return XMFLOAT3( 1, -b, -a);
        case 1:  return XMFLOAT3(-1, -b,  a);
        case 2:  return XMFLOAT3( a,  1,  b);
        case 3:  return XMFLOAT3( a, -1, -b);
        case 4:  return XMFLOAT3( a, -b,  1);
        default: return XMFLOAT3(-a, -b, -1);
        }
    }

    void ComputeIrradianceSH()
    {
        ID3D11Device* dev = GetDevice();
        ID3D11DeviceContext* ctx = GetContext();
        const UINT size = ReflectionProbe::FACE_SIZE >> SH_SOURCE_MIP;
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = d.Height = size;
        d.MipLevels = 1;
        d.ArraySize = 6;
        d.Format = CUBE_FORMAT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;   // CPU-readable copy
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging = nullptr;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &staging))) return;
        for (UINT f = 0; f < 6; ++f)
            ctx->CopySubresourceRegion(staging, D3D11CalcSubresource(0, f, 1), 0, 0, 0,
                                       s.cube, D3D11CalcSubresource(SH_SOURCE_MIP, f, s.mipCount), nullptr);

        // SH basis constants (real spherical harmonics, bands 0..2)
        const float Y0 = 0.282095f, Y1 = 0.488603f, Y2 = 1.092548f, Y20 = 0.315392f, Y22 = 0.546274f;
        XMFLOAT3 sh[9] = {};
        float totalWeight = 0.0f;
        for (UINT f = 0; f < 6; ++f)
        {
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (FAILED(ctx->Map(staging, D3D11CalcSubresource(0, f, 1), D3D11_MAP_READ, 0, &m))) continue;
            for (UINT y = 0; y < size; ++y)
            {
                const PackedVector::HALF* row = (const PackedVector::HALF*)((const char*)m.pData + y * m.RowPitch);
                for (UINT x = 0; x < size; ++x)
                {
                    const float a = ((x + 0.5f) / size) * 2 - 1, b = ((y + 0.5f) / size) * 2 - 1;
                    const float dw = 1.0f / powf(1.0f + a * a + b * b, 1.5f);   // texel solid angle (up to a constant)
                    XMFLOAT3 dir = FaceDirection(a, b, (int)f);
                    XMStoreFloat3(&dir, XMVector3Normalize(XMLoadFloat3(&dir)));
                    const float basis[9] = { Y0, Y1 * dir.y, Y1 * dir.z, Y1 * dir.x, Y2 * dir.x * dir.y, Y2 * dir.y * dir.z,
                                             Y20 * (3 * dir.z * dir.z - 1), Y2 * dir.x * dir.z, Y22 * (dir.x * dir.x - dir.y * dir.y) };
                    const float r = PackedVector::XMConvertHalfToFloat(row[x * 4 + 0]);
                    const float g = PackedVector::XMConvertHalfToFloat(row[x * 4 + 1]);
                    const float bl = PackedVector::XMConvertHalfToFloat(row[x * 4 + 2]);
                    for (int k = 0; k < 9; ++k)
                    {
                        sh[k].x += r * basis[k] * dw;
                        sh[k].y += g * basis[k] * dw;
                        sh[k].z += bl * basis[k] * dw;
                    }
                    totalWeight += dw;
                }
            }
            ctx->Unmap(staging, D3D11CalcSubresource(0, f, 1));
        }
        staging->Release();
        if (totalWeight <= 0.0f) return;
        const float norm = 4.0f * XM_PI / totalWeight;   // the solid angles of the whole sphere add up to 4 pi
        for (int k = 0; k < 9; ++k) { s.sh[k].x = sh[k].x * norm; s.sh[k].y = sh[k].y * norm; s.sh[k].z = sh[k].z * norm; }
        s.hasSH = true;
    }

    void UploadBox()
    {
        if (!s.boxTex)
        {
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = BOX_TEXELS; d.Height = 1; d.MipLevels = 1; d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(GetDevice()->CreateTexture2D(&d, nullptr, &s.boxTex))) return;
            if (FAILED(GetDevice()->CreateShaderResourceView(s.boxTex, nullptr, &s.boxSRV))) return;
        }
        const float on = (s.boxProjection && s.hasBox) ? 1.0f : 0.0f;
        const LightToggles& toggles = LitRender::Toggles();
        const float diffuse = (s.useDiffuse && s.hasSH && toggles.iblDirection) ? s.diffuseStrength : 0.0f;   // 0 = shaders keep the flat fill
        const float specular = toggles.reflections ? 1.0f : 0.0f;                                          // F2 "reflections" switch
        s.uploadedDirection = toggles.iblDirection; s.uploadedReflections = toggles.reflections;
        XMFLOAT4 data[BOX_TEXELS] = { { s.pos.x, s.pos.y, s.pos.z, on },
                                      { s.boxMin.x, s.boxMin.y, s.boxMin.z, diffuse },
                                      { s.boxMax.x, s.boxMax.y, s.boxMax.z, specular } };
        for (int k = 0; k < 9; ++k) data[SH_FIRST_TEXEL + k] = XMFLOAT4(s.sh[k].x, s.sh[k].y, s.sh[k].z, 0.0f);
        GetContext()->UpdateSubresource(s.boxTex, 0, nullptr, data, sizeof(data), 0);
        s.boxDirty = false;
    }
}

namespace ReflectionProbe
{
    float MipToRoughness(float mip) { return mip / (float)(PREFILTER_MIPS - 1); }

    bool Capture(CameraBase* camera, const XMFLOAT3& pos, const std::function<void()>& drawScene)
    {
        if (!camera || !drawScene || !CreateResources()) return false;
        ID3D11DeviceContext* ctx = GetContext();

        // Remember what was bound (the scene render target) and the camera, to put them back.
        ID3D11RenderTargetView* prevRTV = nullptr;
        ID3D11DepthStencilView* prevDSV = nullptr;
        ctx->OMGetRenderTargets(1, &prevRTV, &prevDSV);
        UINT vpCount = 1;
        D3D11_VIEWPORT prevVP = {};
        ctx->RSGetViewports(&vpCount, &prevVP);
        const XMFLOAT3 camPos = camera->GetPos(), camLook = camera->GetLook(), camUp = camera->GetUp();
        const float camFov = camera->GetFovY(), camAspect = camera->GetAspect();

        // Never sample a cube while drawing into it (step 3 binds the cubes to every lit shader).
        ID3D11ShaderResourceView* none[2] = { nullptr, nullptr };
        ctx->PSSetShaderResources(0, 1, none);
        ctx->PSSetShaderResources(IBL_SLOT_ENV, 2, none);

        s.capturing = true;   // the scene is drawn with the plain (non-IBL) ambient into the photo
        camera->SetFovY(FACE_FOV);
        camera->SetAspect(FACE_ASPECT);
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)FACE_SIZE, (float)FACE_SIZE, 0.0f, 1.0f };
        const float CLEAR[4] = { 0, 0, 0, 1 };
        for (int i = 0; i < 6; ++i)
        {
            ctx->OMSetRenderTargets(1, &s.faceRTV[i], s.dsv);
            ctx->RSSetViewports(1, &vp);
            ctx->ClearRenderTargetView(s.faceRTV[i], CLEAR);
            ctx->ClearDepthStencilView(s.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
            camera->SetPos(pos);
            camera->SetLook(XMFLOAT3(pos.x + FACE_DIR[i].x, pos.y + FACE_DIR[i].y, pos.z + FACE_DIR[i].z));
            camera->SetUp(FACE_UP[i]);
            drawScene();
        }
        camera->SetPos(camPos); camera->SetLook(camLook); camera->SetUp(camUp);
        camera->SetFovY(camFov); camera->SetAspect(camAspect);
        s.capturing = false;

        // Unbind the last face BEFORE reading the cube as a texture. D3D11 refuses to bind a resource
        // as an input while it is still bound as an output: it silently sets the input to null, and
        // the bake then read a null cube = an all-black prefiltered map (2026-10-16 F5).
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ctx->PSSetShaderResources(IBL_SLOT_ENV, 2, none);   // the capture draws bound the previous bake again
        // Plain mips of the photo = the bake's source for "average of many texels" lookups.
        ctx->GenerateMips(s.srv);
        ComputeIrradianceSH();   // diffuse IBL: 9 numbers per color from the photo
        Prefilter();
        if (!s.lutBaked) BakeBrdfLut();

        ctx->OMSetRenderTargets(1, &prevRTV, prevDSV);
        ctx->RSSetViewports(1, &prevVP);
        SafeRelease(prevRTV);
        SafeRelease(prevDSV);
        SetDepthTest(DEPTH_ENABLE_WRITE_TEST);   // back to the default for the opaque pass
        s.pos = pos;
        s.boxDirty = true;   // the probe moved: the shaders need the new position
        s.captured = true;
        return true;
    }

    bool HasCapture() { return s.captured; }
    bool ConsumeRecaptureRequest() { const bool r = s.recapture; s.recapture = false; return r; }
    void RequestRecapture() { s.recapture = true; }
    ID3D11ShaderResourceView* CubeSRV() { return s.captured ? s.preSRV : nullptr; }
    ID3D11ShaderResourceView* BrdfLutSRV() { return s.lutBaked ? s.lutSRV : nullptr; }
    ID3D11SamplerState* Sampler() { return s.iblSampler; }
    XMFLOAT3 Position() { return s.pos; }

    void BindForLighting()
    {
        const bool on = s.useInLighting && s.captured && s.lutBaked && !s.capturing;
        ID3D11DeviceContext* ctx = GetContext();
        const LightToggles& toggles = LitRender::Toggles();
        if (toggles.iblDirection != s.uploadedDirection || toggles.reflections != s.uploadedReflections) s.boxDirty = true;   // F2 switch flipped
        if (on && s.boxDirty) UploadBox();
        ID3D11ShaderResourceView* srv[3] = { on ? s.preSRV : nullptr, on ? s.lutSRV : nullptr, on ? s.boxSRV : nullptr };
        ctx->PSSetShaderResources(IBL_SLOT_ENV, 3, srv);   // t6 env, t7 LUT, t8 box
        if (s.iblSampler) ctx->PSSetSamplers(1, 1, &s.iblSampler);
    }

    void SetRoomBox(const XMFLOAT3& mn, const XMFLOAT3& mx)
    {
        if (s.hasBox && mn.x == s.boxMin.x && mn.y == s.boxMin.y && mn.z == s.boxMin.z &&
            mx.x == s.boxMax.x && mx.y == s.boxMax.y && mx.z == s.boxMax.z) return;   // unchanged: no upload
        s.boxMin = mn; s.boxMax = mx; s.hasBox = true; s.boxDirty = true;
    }

    bool RoomBox(XMFLOAT3& boxMin, XMFLOAT3& boxMax)
    {
        boxMin = s.boxMin; boxMax = s.boxMax;
        return s.hasBox;
    }

    void DrawRoomBox(CameraBase* camera)
    {
        if (!s.showBox || !s.hasBox || !camera) return;
        XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
        Geometory::SetWorld(id);
        Geometory::SetView(camera->GetView());
        Geometory::SetProjection(camera->GetProj());
        const XMFLOAT4 BOX_COLOR(1.0f, 0.55f, 0.1f, 1.0f);   // orange = the probe's room
        Geometory::SetColor(BOX_COLOR);
        const XMFLOAT3 a = s.boxMin, b = s.boxMax;
        const XMFLOAT3 c[8] = { { a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, a.y, b.z }, { a.x, a.y, b.z },
                                { a.x, b.y, a.z }, { b.x, b.y, a.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z } };
        for (int i = 0; i < 4; ++i)
        {
            Geometory::AddLine(c[i], c[(i + 1) % 4]);           // floor rectangle
            Geometory::AddLine(c[i + 4], c[(i + 1) % 4 + 4]);   // ceiling rectangle
            Geometory::AddLine(c[i], c[i + 4]);                 // vertical edges
        }
        SetDepthTest(DEPTH_DISABLE);   // seen through the walls (debug view)
        Geometory::DrawLines();
        SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
    }

    bool& ShowRoomBox() { return s.showBox; }

    void Uninit()
    {
        for (auto& r : s.faceRTV) SafeRelease(r);
        for (auto& face : s.preRTV) for (auto& r : face) SafeRelease(r);
        SafeRelease(s.srv);
        SafeRelease(s.cube);
        SafeRelease(s.preSRV);
        SafeRelease(s.pre);
        SafeRelease(s.lutRTV);
        SafeRelease(s.lutSRV);
        SafeRelease(s.lut);
        SafeRelease(s.dsv);
        SafeRelease(s.depth);
        SafeRelease(s.fullscreenVS);
        SafeRelease(s.prefilterPS);
        SafeRelease(s.lutPS);
        SafeRelease(s.bakeCB);
        SafeRelease(s.iblSampler);
        SafeRelease(s.boxSRV);
        SafeRelease(s.boxTex);
        s.hasBox = false; s.hasSH = false; s.boxDirty = true;
        s.captured = false; s.lutBaked = false;
    }
}

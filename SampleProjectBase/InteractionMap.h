#pragma once
#include "Texture.h"
#include "Shader.h"
#include <DirectXMath.h>
#include <memory>

// ---------------------------------------------------------------------------
// InteractionMap: a world-space "something pressed here recently" map.
//
// A square area of the world (XZ) is covered by a render texture, seen by a camera
// looking STRAIGHT DOWN with an ORTHOGRAPHIC projection. Orthographic = no
// perspective: one texel always covers the same world size, wherever it is, so a
// world XZ position maps to a texel with a simple linear formula:
//     u = (x - minX) / size          v = 1 - (z - minZ) / size
// (v is flipped because the texture's v grows downward while the camera's "up" is +Z.)
//
// Every frame:
//   1) Fade(dt):  every texel is multiplied by exp(-dt / recoverTime)
//                 (multiply blend; frame-rate independent exponential decay).
//   2) Stamp(pos): a smooth dome of "pressure" (1 at the centre, 0 at the rim) is drawn
//                 at pos with MAX blend, so overlapping stamps keep the strongest value
//                 instead of adding up.
// Readers (the grass vertex shader first; later e.g. coal or floor shaders) sample the
// map by world position with AreaParams(). The map knows nothing about its readers.
//
// Search keywords: "grass trail render texture", "interactive grass render texture",
// "snow deformation render texture".
// ---------------------------------------------------------------------------
class InteractionMap
{
public:
    // Cover the square [minX, minX+size] x [minZ, minZ+size] with resolution x resolution texels.
    bool Init(float minX, float minZ, float size, UINT resolution);
    void Uninit();

    // GPU passes. They change the bound render target; the caller restores its own afterwards.
    void Fade(float dt);
    void Stamp(float x, float z, float radius);

    Texture* GetTexture() { return &m_rt; }
    // For shaders: (minX, minZ, 1/size, texel size in uv).
    DirectX::XMFLOAT4 AreaParams() const;
    bool IsReady() const { return m_ready; }

    float recoverTime = 1.0f;   // seconds for a mark to fade to ~37% (1/e). Grass springs back over ~2-3x this

private:
    void BeginPass();           // bind the map as render target, depth off
    RenderTarget m_rt;          // R16_FLOAT: float so the slow per-frame fade does not get stuck (8-bit would round to the same value)
    std::shared_ptr<PixelShader> m_stampPS;
    ID3D11BlendState* m_blendMax      = nullptr;   // dst = max(src, dst)
    ID3D11BlendState* m_blendMultiply = nullptr;   // dst = dst * src
    DirectX::XMFLOAT4X4 m_view = {}, m_proj = {};  // top-down orthographic camera (stored transposed for shaders)
    float m_minX = 0.0f, m_minZ = 0.0f, m_size = 1.0f;
    UINT  m_res = 1;
    bool  m_ready = false;
};

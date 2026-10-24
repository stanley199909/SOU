#ifndef __DEFERRED_H__
#define __DEFERRED_H__

#include <DirectXMath.h>
#include <memory>
#include "Texture.h"   // RenderTarget / DepthStencil
#include "Shader.h"    // VertexShader / PixelShader

class LightBase;
class CameraBase;

// -----------------------------------------------------------------------------
// GBuffer : minimal deferred-shading pipeline for the OPAQUE, lit geometry only
// (anvil, tools, worktable, ... the props drawn via VS_Object/PS_TexTint).
//
// Emissive geometry (glowing bar / weapon / coal) and transparent geometry
// (water / spark particles) stay on the existing forward path and are drawn
// AFTER the lighting pass -- that is correct, deferred cannot shade them anyway.
//
// Flow per frame:
//   1) BeginGeometry(dsv)  -> bind the 3 G-buffer render targets + shared depth
//      draw every opaque prop with GetGeomVS()/GetGeomPS()  (writes, no light)
//   2) Lighting(dst, ...)  -> one fullscreen pass reads the 3 targets + the
//      scene light and writes the lit result into dst (PostProcess sceneRT).
//
// World position is stored in its own target instead of being reconstructed
// from depth: reconstruction needs the inverse view-projection and this project
// has a documented history of transpose-convention bugs, so we trade a little
// bandwidth (fine at 720p on the integrated GPU) for robustness.
// -----------------------------------------------------------------------------
class GBuffer
{
public:
	void Init(UINT width, UINT height);
	void Uninit();

	// --- geometry pass ---
	// Bind the 3 G-buffer targets (albedo / normal / worldPos) with the shared
	// depth buffer and clear them. Sets standard opaque 3D render states.
	void BeginGeometry(DepthStencil* pDSV);

	// Shaders that opaque props must use during the geometry pass. Caller writes
	// cbuffer b0 = { world, view, proj } on the VS and b0 = tint color on the PS,
	// exactly like the existing DrawModelWorld does for VS_Object/PS_TexTint.
	VertexShader* GetGeomVS() { return m_geomVS.get(); }
	PixelShader*  GetGeomPS() { return m_geomPS.get(); }

	// --- lighting pass ---
	// Fullscreen: reads the 3 targets + the light, writes the lit color to pDst.
	// Depth test is disabled; pDst must already be bound-safe (we bind it here).
	void Lighting(RenderTarget* pDst, LightBase* pLight, CameraBase* pCam);

	// Expose targets for debugging / later SSR.
	RenderTarget* GetAlbedo()   { return &m_albedo; }
	RenderTarget* GetNormal()   { return &m_normal; }
	RenderTarget* GetWorldPos() { return &m_worldPos; }

private:
	RenderTarget m_albedo;    // R8G8B8A8   : rgb = base color (albedo)
	RenderTarget m_normal;    // R16G16B16A16F : xyz = world-space normal
	RenderTarget m_worldPos;  // R16G16B16A16F : xyz = world-space position

	std::shared_ptr<VertexShader> m_geomVS;   // writes G-buffer
	std::shared_ptr<PixelShader>  m_geomPS;
	std::shared_ptr<PixelShader>  m_lightPS;  // fullscreen deferred lighting

	UINT m_width  = 0;
	UINT m_height = 0;
};

#endif // __DEFERRED_H__

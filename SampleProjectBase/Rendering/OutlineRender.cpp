// Inverted hull outline. See OutlineRender.h.
// ASCII comments only (file created without BOM).
#include "OutlineRender.h"
#include "Model.h"
#include "Shader.h"
#include "CameraBase.h"
#include "DirectX.h"
#include <memory>

using namespace DirectX;

namespace
{
	struct Shaders
	{
		std::unique_ptr<VertexShader> vs;
		std::unique_ptr<PixelShader>  ps;
		bool tried = false;
	};
	Shaders s_sh;

	// Loaded on first use (only scenes that draw an outline pay for it). A missing .cso = no outline, no crash.
	bool Ready()
	{
		if (s_sh.tried) return s_sh.vs && s_sh.ps;
		s_sh.tried = true;
		s_sh.vs = std::make_unique<VertexShader>();
		s_sh.ps = std::make_unique<PixelShader>();
		if (FAILED(s_sh.vs->Load("Assets/Shader/VS_Outline.cso")) || FAILED(s_sh.ps->Load("Assets/Shader/PS_Outline.cso")))
		{
			s_sh.vs.reset(); s_sh.ps.reset();
			return false;
		}
		return true;
	}
}

namespace OutlineRender
{
	void Draw(Model* model, FXMMATRIX world, CameraBase* camera, const XMFLOAT4& color, float thickness)
	{
		const float MIN_VISIBLE_ALPHA = 0.01f;	// fully faded = skip the draw
		if (!model || !camera || color.w < MIN_VISIBLE_ALPHA || !Ready()) return;

		// Same layout as cbuffer OutlineVS (world is transposed like every other shader here; view/proj come transposed)
		struct VsParams { XMFLOAT4X4 world, view, proj; XMFLOAT4 params; } v{};
		XMStoreFloat4x4(&v.world, XMMatrixTranspose(world));
		v.view   = camera->GetView();
		v.proj   = camera->GetProj();
		v.params = { thickness, 0.0f, 0.0f, 0.0f };
		s_sh.vs->WriteBuffer(0, &v);
		XMFLOAT4 c = color;
		s_sh.ps->WriteBuffer(0, &c);

		// Back faces only (cull the front) = the pushed-out shell shows only behind / around the object.
		// No depth write: the outline never hides anything drawn after it.
		SetCullingMode(D3D11_CULL_FRONT);
		SetDepthTest(DEPTH_ENABLE_TEST);
		SetBlendMode(BLEND_ALPHA);
		s_sh.vs->Bind();
		s_sh.ps->Bind();
		for (unsigned i = 0; i < model->GetMeshNum(); ++i)
			model->GetMesh(i)->mesh->Draw();
		SetCullingMode(D3D11_CULL_NONE);	// the engine default (DirectX.cpp)
		SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	}

	void Uninit()
	{
		s_sh = Shaders{};
	}
}

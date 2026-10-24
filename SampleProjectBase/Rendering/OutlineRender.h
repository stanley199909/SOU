#pragma once
#include <DirectXMath.h>

class Model;
class CameraBase;

//====================================================================
//  Outline of one object: inverted hull method (JP: haimen-hou / search "inverted hull outline").
//    Draw the same mesh again, pushed out along its normals, back faces only, in one flat color.
//    Cheap: one extra draw of one object, no extra render target (good for the iGPU).
//  Used to point at the next station (SceneForge station highlight).
//====================================================================
namespace OutlineRender
{
	// color.w = opacity (fade). thickness = how far the hull is pushed out, in world metres.
	void Draw(Model* model, DirectX::FXMMATRIX world, CameraBase* camera, const DirectX::XMFLOAT4& color, float thickness);
	void Uninit();
}

#pragma once
#include <DirectXMath.h>

class CameraBase;
class RenderTarget;
class DepthStencil;

//====================================================================
//  Volumetric sunlight (light shafts / god rays through the window).
//
//  Pass 1 (PS_VolumetricMarch, quarter resolution): ray march from the camera to each pixel's surface
//         and add up the air that the sun shadow map says is in sunlight.
//  Pass 2 (PS_VolumetricComposite): small blur + ADD onto the scene.
//  Uses the sun shadow map that already exists for the sunlight on surfaces, so the beam has exactly
//  the shape of the window opening, from any angle, and people/objects in it block it correctly.
//  Air exists only inside the room box (same box as the reflection probe's box projection).
//  Search: "volumetric lighting shadow map ray marching" / JP: boryumetorikku raito, gozzu rei
//====================================================================
namespace VolumetricLight
{
    struct Settings
    {
        float density    = 0.06f;   // how much sunlight 1 m of air scatters (dust + smoke). More = thicker beam
        float anisotropy = 0.6f;    // Henyey-Greenstein g: 0 = same in every direction, 0.6 = mostly forward (dust)
        int   steps      = 24;      // ray-march samples per pixel (the jitter + blur make 24 look smooth)
        float maxDistance = 12.0f;  // metres: air farther than this is not marched (the room is smaller)
        float strength   = 1.0f;    // final multiplier (artist control)
    };
    Settings& Get();

    // After the opaque scene (and the water), before particles. sceneRT stays the render target afterwards.
    void Render(CameraBase* camera, RenderTarget* sceneRT, DepthStencil* depth,
                const DirectX::XMFLOAT3& roomMin, const DirectX::XMFLOAT3& roomMax);
    void Uninit();
}

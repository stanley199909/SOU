#pragma once
#include <DirectXMath.h>
#include <functional>

struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
class CameraBase;

//====================================================================
//  Reflection probe (IBL step 1 of 4) -- a 360-degree HDR photo of the room.
//
//  Capture() renders the static scene 6 times from one point, one render per face of a
//  cube (90-degree field of view each), into a cubemap. Taken once (scene start / F1 button),
//  not every frame: the room does not move, and an iGPU cannot afford 6 extra scene renders
//  per frame.
//  The cube holds LINEAR HDR radiance (before tonemapping): the forge mouth really is brighter
//  than 1 in there, exactly like the light the shaders compute.
//  Next steps: blur it per roughness (prefiltered environment map) + BRDF LUT, then every metal
//  looks it up instead of the flat "room color" placeholder.
//  Search: "reflection probe", "cubemap capture", "image based lighting"
//====================================================================
namespace ReflectionProbe
{
    static const unsigned FACE_SIZE = 128;   // pixels per face: reflections are blurred anyway, and it keeps the capture cheap
    // Prefiltered cube: mip m holds the reflection of roughness m / (PREFILTER_MIPS-1).
    // 6 levels = 128, 64, 32, 16, 8, 4 px: the roughest level is so blurry that 4 px per face is plenty.
    static const unsigned PREFILTER_MIPS = 6;
    // Where the lit shaders find the IBL (step 3): t6 = prefiltered cube, t7 = BRDF LUT, s1 = IBL sampler.
    // Above the slots every lit shader already uses (t0..t4).
    static const unsigned IBL_SLOT_ENV = 6;
    static const unsigned IBL_SLOT_LUT = 7;
    static const unsigned IBL_SLOT_BOX = 8;   // probe position + room box (box projection)
    float MipToRoughness(float mip);

    // Render the scene into the cube from pos. drawScene draws the static scene with "camera"
    // (Capture points the camera at each face, then restores it and the bound render target).
    bool Capture(CameraBase* camera, const DirectX::XMFLOAT3& pos, const std::function<void()>& drawScene);
    bool HasCapture();
    bool ConsumeRecaptureRequest();          // true once after the F1 "Recapture" button
    void RequestRecapture();                 // retake the photo next frame (e.g. a light was switched on/off)
    ID3D11ShaderResourceView* CubeSRV();     // the PREFILTERED cube (what materials read), null before the first capture
    ID3D11ShaderResourceView* BrdfLutSRV();
    ID3D11SamplerState*       Sampler();
    // Step 3: put the IBL on t6/t7/s1 for the lit pixel shaders (Lighting.hlsli). Call right after
    // the shader's Bind() (Bind resets every texture slot the shader declares). Binds null when there
    // is no capture yet or F1 "Use IBL in lighting" is off -> the shaders fall back to the flat ambient.
    void BindForLighting();
    // Step 4: the room as an axis-aligned box in world space (box projection, Lighting.hlsli).
    // Cheap to call every frame: uploads only when the box changed.
    void SetRoomBox(const DirectX::XMFLOAT3& boxMin, const DirectX::XMFLOAT3& boxMax);
    void DrawRoomBox(CameraBase* camera);    // F1 "Show room box": orange lines, seen through walls
    DirectX::XMFLOAT3 Position();

    // Debug view (F1): a mirror ball at the probe position. It reflects the captured cube, and is
    // drawn into the normal scene, so it goes through the same tonemap/bloom as everything else.
    void DrawDebugBall(CameraBase* camera);
    bool Controls();                         // F1 section; returns true while it is open (callers append their own rows)
    void Uninit();
}

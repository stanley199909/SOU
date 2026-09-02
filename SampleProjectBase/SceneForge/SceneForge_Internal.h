#ifndef __SCENE_FORGE_INTERNAL_H__
#define __SCENE_FORGE_INTERNAL_H__

// Shared file-local helpers for the SceneForge translation units.
// SceneForge.cpp is split across several .cpp files (Weapon/StageSetting/UI/Debug);
// these free helpers were static in the old single cpp. They are now declared here
// (defined once in SceneForge.cpp) so every split unit can use them.
// NOTE: keep comments ASCII only (files here have no BOM; MSVC misreads non-ASCII).

#include <DirectXMath.h>
#include <memory>

class PostProcess;

// Steel color from temperature (0..1). dmg darkens it. Defined in SceneForge.cpp.
DirectX::XMFLOAT4 HeatRGB(float h, float dmg);

// Random helpers. Defined in SceneForge.cpp.
float frand();
float frand(float a, float b);

// Mouse position in client pixels + client size. Defined in SceneForge.cpp.
void GetMouseClient(float& mx, float& my, float& cw, float& ch);

// Scene-snapshot post process (global owned by Main), used by the water refraction pass.
extern std::shared_ptr<PostProcess> g_pPost;

#endif // __SCENE_FORGE_INTERNAL_H__

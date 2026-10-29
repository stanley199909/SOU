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
struct ImDrawList;
struct ImFont;

// Subtitle look for any text drawn straight over the 3D world (monologue, flip beats, lesson lines,
// interact labels, key-hint row): a translucent dark rounded box + an outlined glyph.
// The parchment strip is reserved for "the game tells you what to do"; this box = readable overlay text.
// Defined in HUD.cpp.
void DrawSubtitleBox(ImDrawList* dl, float x0, float y0, float x1, float y1, float textPx, float alpha01);	// box around a content rect (padding added inside)
void DrawOutlinedText(ImDrawList* dl, ImFont* font, float px, float x, float y, unsigned int col, const char* text);	// dark 8-way outline, then the text

// The draw list for the game HUD (prompts, tracker, gauge, menus, fade). It is ImGui's BACKGROUND list:
// still drawn over the 3D picture, but under every ImGui window and the foreground list, so the debug
// panels (F2/F3 on the foreground list, F4/F5 as windows) always stay on top of the game UI.
// Layers inside the HUD follow call order (draw an underlay before its text). Defined in HUD.cpp.
ImDrawList* HudDrawList();

// Sample the shared 7-stop steel-heat gradient at h(0..1) -> r,g,b(0..1).
// Both HeatRGB (3D weapon/bar) and the HUD's HeatColor use this one table.
// Defined in SceneForge.cpp.
void HeatSampleRGB(float h, float& r, float& g, float& b);

// Steel color from temperature (0..1). dmg darkens it. Defined in SceneForge.cpp.
DirectX::XMFLOAT4 HeatRGB(float h, float dmg);

// Random helpers. Defined in SceneForge.cpp.
float frand();
float frand(float a, float b);

// Mouse position in client pixels + client size. Defined in SceneForge.cpp.
void GetMouseClient(float& mx, float& my, float& cw, float& ch);

// Shortest-arc rotation that turns direction 'from' into direction 'to' (both any length).
// Rotation axis = cross(from, to), angle = acos(dot). Opposite directions -> half turn about
// any axis perpendicular to 'from'. Defined in Carry.cpp (tongs and held iron use it).
DirectX::XMMATRIX RotationFromTo(DirectX::FXMVECTOR from, DirectX::FXMVECTOR to);

// Scene-snapshot post process (global owned by Main), used by the water refraction pass.
extern std::shared_ptr<PostProcess> g_pPost;

// Smooth non-repeating wobble in about -1..1 (sum of sines with non-integer frequency ratios).
// Camera hand-held feel (CameraAim.cpp) and the breath sway of the test-cut guard (TestCut.cpp).
float OrganicNoise(float t, float seed);

#endif // __SCENE_FORGE_INTERNAL_H__

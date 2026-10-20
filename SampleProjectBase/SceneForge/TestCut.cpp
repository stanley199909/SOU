// Part of SceneForge: the test cut (tameshigiri), stage 1 (2026-10-20).
// ASCII comments only (new file without BOM).
//
// Stage 1 = box stand-in dummy + real plane slicing + falling pieces.
// Not a recipe step yet: while walking, look at the dummy and press Use (left click / RT)
// to slash. Each slash alternates the diagonal (upper-left -> lower-right, then the mirror),
// like a light-attack combo. The swing animation, hit-stop, durability and the weapon's
// quality come in the next stages.
//
// Split of responsibilities:
//   here (SceneForge)  = HOW the player swings: input, which plane the swing makes;
//   CutTarget          = WHAT gets cut and how it falls (shape, slicing, rigid bodies).
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Audio.h"
#include "InputActions.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX;

namespace
{
    // Base color images of the stand-in parts (the real model will bring its own).
    #define COTTAGE_TEX "Assets/Medieval_Blacksmith_Cottage_Production/Medieval_Blacksmith_Cottage_Production/07_Textures/"
    const char* const WOOD_TEX  = COTTAGE_TEX "Wood_Dark_Aged_BaseColor_2048.png";
    const char* const IRON_TEX  = COTTAGE_TEX "Iron_Rusted_BaseColor_2048.png";
    const char* const STRAW_TEX = "Assets/Cottage_Materials/wood_planks_base.jpg";   // tinted toward straw by the material table
    #undef COTTAGE_TEX

    const float PLACE_DISTANCE = 1.6f;    // "Place in front of me": metres ahead of the player
    const float SWING_VOLUME   = 0.8f;
    const float SWING_PITCH    = 1.15f;   // a little sharper than the hammer's whiff

    // Stand-in dummy = boxes STACKED along y, never overlapping (2026-10-20 F5: the first
    // version nested them -- armor around the straw, post into the helmet -- so every cut made
    // two caps on the SAME plane, one inside the other = z-fighting, the faces flickered/black).
    // Parts touching on a face would z-fight there too, so each starts PART_GAP above the last.
    const float PART_GAP    = 0.002f;     // metres: invisible, but no two faces share a plane
    const float POST_TOP    = 0.75f;      // post: ground .. here
    const float WAIST_TOP   = 1.00f;      // bare straw (hips): post .. here
    const float TORSO_TOP   = 1.52f;      // armored torso: waist .. here
    const float HELMET_TOP  = 1.80f;      // helmet: torso .. here
    const float POST_HALF_W = 0.06f;      // half widths (x = shoulders, z = chest depth)
    const XMFLOAT2 WAIST_HALF(0.20f, 0.14f);
    const XMFLOAT2 TORSO_HALF(0.23f, 0.17f);
    const float HELMET_HALF_W = 0.13f;

    // Box spanning y0..y1 (with the gap at the bottom).
    CutTarget::PartDef Stacked(const char* material, const char* image, float y0, float y1, float halfX, float halfZ)
    {
        const float b = y0 + PART_GAP;
        return { material, image, XMFLOAT3(0.0f, (b + y1) * 0.5f, 0.0f), XMFLOAT3(halfX, (y1 - b) * 0.5f, halfZ) };
    }
}

// The dummy, local space: the foot of the post at the origin, y up, metres.
const CutTarget::PartDef SceneForge::CUT_PARTS[] = {
    Stacked("CutPost",   WOOD_TEX,  0.0f,       POST_TOP,   POST_HALF_W,   POST_HALF_W),     // post
    Stacked("CutStraw",  STRAW_TEX, POST_TOP,   WAIST_TOP,  WAIST_HALF.x,  WAIST_HALF.y),    // bare straw (hips)
    Stacked("CutArmor",  IRON_TEX,  WAIST_TOP,  TORSO_TOP,  TORSO_HALF.x,  TORSO_HALF.y),    // armored torso
    Stacked("CutHelmet", IRON_TEX,  TORSO_TOP,  HELMET_TOP, HELMET_HALF_W, HELMET_HALF_W),   // helmet
};
const int SceneForge::NUM_CUT_PARTS = _countof(SceneForge::CUT_PARTS);

void SceneForge::ResetTestCut()
{
    // Only marks it: the meshes are rebuilt on the next update. ResetRunState also runs at
    // shutdown, where creating GPU buffers would be wasted (or unsafe).
    m_cutNeedsBuild = true;
    m_cutFromLeft = true;
}

void SceneForge::UpdateTestCut(float tick)
{
    if (m_cutNeedsBuild) { m_cutTarget.Build(CUT_PARTS, NUM_CUT_PARTS); m_cutNeedsBuild = false; }
    m_cutTarget.SetPlacement(XMFLOAT3(m_cutPosXZ[0], m_walkFloorY, m_cutPosXZ[1]), m_cutYaw);   // follows the F1 sliders
    m_cutTarget.Update(tick);
}

void SceneForge::SlashTestCut()
{
    // the view ray, built from the player state (same rule as UpdateInteract)
    const XMFLOAT3 foot = m_player.GetPosition();
    const XMFLOAT3 eye(foot.x, foot.y + m_walkEyeH, foot.z);
    const float yaw = m_player.GetYaw(), cp = cosf(m_walkPitch);
    const XMVECTOR fwd   = XMVectorSet(sinf(yaw) * cp, sinf(m_walkPitch), cosf(yaw) * cp, 0);
    const XMVECTOR right = XMVectorSet(cosf(yaw), 0, -sinf(yaw), 0);   // cross(world up, flat forward)
    const XMVECTOR up    = XMVector3Cross(fwd, right);

    Audio::Play(Audio::SE_SWING, SWING_VOLUME, SWING_PITCH);
    XMFLOAT3 fwdF; XMStoreFloat3(&fwdF, fwd);
    float t = 0.0f;
    if (!m_cutTarget.RayHit(eye, fwdF, t) || t > m_cutReach) return;   // out of reach: a whiff

    // The blade travels across the view, tilted: from the upper left down to the lower right
    // (or mirrored). The cut plane contains the view ray and that travel direction, so the cut
    // line goes through the crosshair.
    const float side = m_cutFromLeft ? 1.0f : -1.0f;
    const XMVECTOR swing = XMVector3Normalize(XMVectorSubtract(XMVectorScale(right, side * cosf(m_cutSlashTilt)),
                                                               XMVectorScale(up, sinf(m_cutSlashTilt))));
    const XMVECTOR normal = XMVector3Normalize(XMVector3Cross(fwd, swing));
    XMFLOAT3 hit, n, s;
    XMStoreFloat3(&hit, XMVectorAdd(XMLoadFloat3(&eye), XMVectorScale(fwd, t)));
    XMStoreFloat3(&n, normal);
    XMStoreFloat3(&s, swing);
    m_cutTarget.Cut(hit, n, s);
    m_cutFromLeft = !m_cutFromLeft;   // the next slash comes from the other side (combo)
}

void SceneForge::DrawTestCut()
{
    CameraBase* cam = GetObj<CameraBase>("Camera");
    m_cutTarget.Draw(cam);
    if (m_showCutDebug) m_cutTarget.DrawDebug(cam);
}

void SceneForge::TestCutControls()
{
    if (!ImGui::CollapsingHeader("Test cut")) return;
    ImGui::TextDisabled("Walk up to the dummy, look at it, press Use (LMB / RT).");
    if (ImGui::Button("Place in front of me"))
    {
        const XMFLOAT3 foot = m_player.GetPosition();
        const float yaw = m_player.GetYaw();
        m_cutPosXZ[0] = foot.x + sinf(yaw) * PLACE_DISTANCE;
        m_cutPosXZ[1] = foot.z + cosf(yaw) * PLACE_DISTANCE;
        m_cutYaw = yaw + XM_PI;   // facing the player
        ResetTestCut();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset dummy")) ResetTestCut();
    ImGui::SliderFloat2("Position XZ", m_cutPosXZ, -20.0f, 20.0f, "%.2f");
    ImGui::SliderAngle("Yaw", &m_cutYaw, -180.0f, 180.0f);
    ImGui::SliderFloat("Reach (m)", &m_cutReach, 0.5f, 5.0f, "%.2f");
    ImGui::SliderAngle("Slash tilt", &m_cutSlashTilt, 0.0f, 90.0f);
    ImGui::TextDisabled("-- falling pieces --");
    ImGui::SliderFloat("Push speed (m/s)", &m_cutTarget.pushSpeed, 0.0f, 6.0f, "%.2f");
    ImGui::SliderFloat("Separate speed (m/s)", &m_cutTarget.sepSpeed, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Restitution", &m_cutTarget.physics.restitution, 0.0f, 0.9f, "%.2f");
    ImGui::SliderFloat("Friction", &m_cutTarget.physics.friction, 0.0f, 1.5f, "%.2f");
    ImGui::SliderFloat("Spin drag", &m_cutTarget.physics.angularDamp, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Density (kg/m3)", &m_cutTarget.density, 50.0f, 1500.0f, "%.0f");
    ImGui::Checkbox("Show stump faces (orange)", &m_showCutDebug);
    ImGui::Text("fallen pieces: %d", m_cutTarget.DebrisCount());
}

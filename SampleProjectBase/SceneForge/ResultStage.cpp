// Part of SceneForge: the result stage (direction C, 2026-11-09).
// ASCII comments only (new file without BOM).
//
// One shot in the smithy, about 12 s, skippable with Confirm:
//   fade in on the finished sword lying on the anvil; the camera slides from the tip to the hilt and
//   draws back to the whole blade -> the forging record is written on parchment line by line
//   (HUD.cpp DrawResultUI) -> a beat with the music low -> a glint runs along the blade -> the title
//   slams in with a screen shake -> hold for a key. Times are shown only, never scored.
//
// The timeline is just m_resultTime compared against fixed moments (ResultLinesAt ... ResultEnd),
// so skipping = jumping m_resultTime to the end; everything drawn from it is then in its final state.
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Audio.h"
#include "AimSystem.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include <cmath>

using namespace DirectX;

namespace
{
    // --- the camera (m: distances from the blade) ---
    // Looked at from well above: the blade lies flat, so a low camera sees only its thin edge
    // (2026-11-09 F5: the cold blade on the anvil could not be seen at all).
    const float CAM_NEAR_DIST    = 0.20f;   // the slide: this far beside the blade ...
    const float CAM_NEAR_HEIGHT  = 0.35f;   // ... and this far above it
    const float CAM_FAR_DIST_K   = 0.55f;   // the wide shot: x the blade's length beside it ...
    const float CAM_FAR_HEIGHT_K = 0.95f;   // ... and above it
    const float CAM_LOOK_SHIFT_K = 0.25f;   // the wide shot looks this share of the blade to the right,
                                            // so the sword sits left of the parchment (HUD: right half)

    // --- sound ---
    const float BGM_BEAT_SHARE  = 0.25f;    // the music during the beat (share of RESULT_BGM_VOLUME)
    const float QUILL_VOLUME    = 0.6f;
    const float GLINT_SE_VOLUME = 0.8f;
    const float SLAM_VOLUME     = 1.0f;
    const float LEGENDARY_VOLUME = 0.9f;    // the choir, S only
    const float TITLE_SHAKE     = 1.0f;     // m_shake when the title lands
    const float SHAKE_DECAY     = 3.0f;     // 1/s (the same fall-off as while playing)

    // --- the glint running along the blade (a star billboard, PS_Glint) ---
    const float GLINT_SIZE   = 0.12f;       // m
    const float GLINT_BRIGHT = 3.0f;        // HDR: Bloom makes it shine
    const XMFLOAT4 GLINT_COL(1.0f, 0.95f, 0.85f, 1.0f);

    float Clamp01(float t) { return fminf(fmaxf(t, 0.0f), 1.0f); }
    float Ease(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }   // smoothstep
}

//====================================================================
//  timeline
//====================================================================
//--- A step that took no time is left off the record: the first heat is already done when the game
//    starts (START_HEAT), and "0:00" read like a bug (2026-11-09 F5).
bool SceneForge::ResultRowShown(int step) const
{
    return m_stepTimes[step].total >= RESULT_ROW_MIN_SEC;
}

int SceneForge::ResultLineCount() const   // the steps that are shown + the total
{
    int n = 1;
    for (int i = 0; i < (int)m_stepTimes.size(); ++i) if (ResultRowShown(i)) ++n;
    return n;
}
float SceneForge::ResultLinesAt() const   { return RESULT_SLIDE_SEC + RESULT_PULL_SEC; }
float SceneForge::ResultGlintAt() const   { return ResultLinesAt() + ResultLineCount() * RESULT_LINE_GAP + RESULT_BEAT_SEC; }
float SceneForge::ResultTitleAt() const   { return ResultGlintAt() + RESULT_GLINT_SEC; }
float SceneForge::ResultEnd() const       { return ResultTitleAt() + RESULT_SLAM_SEC; }

//--- From FinishGame: the finished sword is laid on the anvil (wherever the last step left it, e.g.
//    the test cut outside) and the clock starts. The screen starts black (HUD fades it in), so the
//    jump of the camera and the sword is never seen.
void SceneForge::StartResultStage()
{
    m_resultHilt = (m_workAt == Station::TestCut);   // the hilt was fitted for the test cut
    m_workAt   = Station::Anvil;
    m_station  = Station::Anvil;
    m_carrying = false;
    m_resultTime = 0.0f;
    m_resultLinesWritten = 0;
    m_resultGlintPlayed = m_resultTitleLanded = false;
    m_shake = 0.0f;
}

void SceneForge::SkipResultStage()
{
    m_resultTime = ResultEnd();
    m_resultLinesWritten = ResultLineCount();   // written all at once: no burst of quill sounds
    m_resultGlintPlayed = true;
    // the title lands on the next update (its slam and shake still play once)
}

void SceneForge::UpdateResultStage(float tick)
{
    const float before = m_resultTime;
    m_resultTime += tick;
    if (m_shake > 0.0f) m_shake = fmaxf(m_shake - tick * SHAKE_DECAY, 0.0f);

    // one quill stroke per line as it is written
    const int lines = ResultLineCount();
    while (m_resultLinesWritten < lines && m_resultTime >= ResultLinesAt() + m_resultLinesWritten * RESULT_LINE_GAP)
    {
        ++m_resultLinesWritten;
        Audio::Play(Audio::SE_QUILL, QUILL_VOLUME);
    }
    // the beat: the music drops after the last line ...
    const float linesEnd = ResultLinesAt() + lines * RESULT_LINE_GAP;
    if (before < linesEnd && m_resultTime >= linesEnd && !m_resultTitleLanded)
        Audio::SetLoop(Audio::BGM_RESULT, RESULT_BGM_VOLUME * BGM_BEAT_SHARE, 1.0f);
    if (!m_resultGlintPlayed && m_resultTime >= ResultGlintAt())
    {
        m_resultGlintPlayed = true;
        Audio::Play(Audio::SE_RESULT_GLINT, GLINT_SE_VOLUME);
    }
    // ... and comes back as the title lands
    if (!m_resultTitleLanded && m_resultTime >= ResultEnd())
    {
        m_resultTitleLanded = true;
        m_shake = TITLE_SHAKE;
        Audio::Play(Audio::SE_RESULT_SLAM, SLAM_VOLUME);
        if (GradeLetter() == 'S') Audio::Play(Audio::SE_RESULT_LEGENDARY, LEGENDARY_VOLUME);
        Audio::SetLoop(Audio::BGM_RESULT, RESULT_BGM_VOLUME, 1.0f);
    }
}

//====================================================================
//  the sword and the camera
//====================================================================
//--- Both ends of the blade on the anvil: the tang end (where the hilt sits) and the tip.
bool SceneForge::ResultSwordEnds(XMFLOAT3& grip, XMFLOAT3& tip)
{
    if (!m_wpOk) return false;
    const int sign = SwordTipSign();
    const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
    const XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
    XMFLOAT3 g = c, t = c;
    (&g.x)[la] = (sign > 0) ? (&m_wpFinMin.x)[la] : (&m_wpFinMax.x)[la];
    (&t.x)[la] = (sign > 0) ? (&m_wpFinMax.x)[la] : (&m_wpFinMin.x)[la];
    const XMMATRIX w = WeaponWorld();
    XMStoreFloat3(&grip, XMVector3TransformCoord(XMLoadFloat3(&g), w));
    XMStoreFloat3(&tip,  XMVector3TransformCoord(XMLoadFloat3(&t), w));
    return true;
}

//--- One shot: close beside the blade, sliding from the tip to the hilt; then drawing back to the whole
//    sword. The camera stays on the side of the forge fire, so it sees the face the fire lights.
void SceneForge::ApplyResultCamera()
{
    CameraBase* cam = GetObj<CameraBase>("Camera");
    XMFLOAT3 gripF, tipF;
    if (!cam || !ResultSwordEnds(gripF, tipF)) { ApplyCamera(); return; }
    const XMVECTOR grip = XMLoadFloat3(&gripF), tip = XMLoadFloat3(&tipF);
    const XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    const XMVECTOR mid = XMVectorLerp(grip, tip, 0.5f);
    const float len = XMVectorGetX(XMVector3Length(XMVectorSubtract(tip, grip)));
    const XMVECTOR along = XMVector3Normalize(XMVectorSubtract(tip, grip));
    XMVECTOR side = XMVector3Cross(up, along);
    const float FLAT_EPS = 1e-6f;   // a blade standing straight up has no sideways direction
    side = (XMVectorGetX(XMVector3LengthSq(side)) > FLAT_EPS) ? XMVector3Normalize(side) : XMVectorSet(1, 0, 0, 0);
    float fire[3]; FireLightPos(fire);
    if (XMVectorGetX(XMVector3Dot(side, XMVectorSubtract(XMVectorSet(fire[0], fire[1], fire[2], 0), mid))) < 0.0f) side = XMVectorNegate(side);

    // the slide: tip -> hilt
    const XMVECTOR p = XMVectorLerp(tip, grip, Ease(m_resultTime / RESULT_SLIDE_SEC));
    const XMVECTOR nearEye = p + side * CAM_NEAR_DIST + up * CAM_NEAR_HEIGHT;
    // the wide shot (left-handed: up x forward = right)
    const XMVECTOR farEye = mid + side * (len * CAM_FAR_DIST_K) + up * (len * CAM_FAR_HEIGHT_K);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, XMVectorSubtract(mid, farEye)));
    const XMVECTOR farLook = mid + right * (len * CAM_LOOK_SHIFT_K);
    const float w = Ease((m_resultTime - RESULT_SLIDE_SEC) / RESULT_PULL_SEC);
    XMFLOAT3 eye, look;
    XMStoreFloat3(&eye,  XMVectorLerp(nearEye, farEye, w));
    XMStoreFloat3(&look, XMVectorLerp(p, farLook, w));
    eye.y += ShakeOffsetY();
    cam->SetFovY(m_camFov);
    cam->SetPos(eye);
    cam->SetLook(look);
    cam->SetUp(XMFLOAT3(0.0f, 1.0f, 0.0f));
}

//--- The glint: a star that runs from the hilt to the tip, swelling and fading (additive, after the opaque scene).
void SceneForge::DrawResultGlint()
{
    if (m_state != GAME_RESULT || m_resultTime < ResultGlintAt() || m_resultTime >= ResultTitleAt()) return;
    CameraBase* cam = GetObj<CameraBase>("Camera");
    VertexShader* vs = GetObj<VertexShader>("VS_Forge");
    PixelShader*  ps = GetObj<PixelShader>("PS_Glint");
    XMFLOAT3 gripF, tipF;
    if (!cam || !vs || !ps || !ResultSwordEnds(gripF, tipF)) return;
    const int QUAD_VERTS = 6;
    if (!m_resultGlintMesh)
    {
        m_resultGlintVtx.resize(QUAD_VERTS);
        MeshBuffer::Description d = {};
        d.pVtx = m_resultGlintVtx.data(); d.vtxSize = sizeof(Vertex); d.vtxCount = (UINT)m_resultGlintVtx.size();
        d.isWrite = true; d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        m_resultGlintMesh = std::make_shared<MeshBuffer>(d);
    }
    const float g = (m_resultTime - ResultGlintAt()) / RESULT_GLINT_SEC;
    const float env = sinf(g * XM_PI);
    const XMVECTOR at = XMVectorLerp(XMLoadFloat3(&gripF), XMLoadFloat3(&tipF), Ease(g));
    const XMFLOAT3 camPos = cam->GetPos();
    const XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&camPos), at));
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), toCam));
    const XMVECTOR upv = XMVector3Cross(toCam, right);
    const float sz = GLINT_SIZE * env, br = GLINT_BRIGHT * env;
    const XMFLOAT4 col(GLINT_COL.x * br, GLINT_COL.y * br, GLINT_COL.z * br, 1.0f);
    XMFLOAT3 tl, tr, bl, brc;
    XMStoreFloat3(&tl,  at - right * sz + upv * sz);
    XMStoreFloat3(&tr,  at + right * sz + upv * sz);
    XMStoreFloat3(&bl,  at - right * sz - upv * sz);
    XMStoreFloat3(&brc, at + right * sz - upv * sz);
    m_resultGlintVtx[0] = { tl, XMFLOAT2(0, 0), col }; m_resultGlintVtx[1] = { tr, XMFLOAT2(1, 0), col }; m_resultGlintVtx[2] = { bl, XMFLOAT2(0, 1), col };
    m_resultGlintVtx[3] = { bl, XMFLOAT2(0, 1), col }; m_resultGlintVtx[4] = { tr, XMFLOAT2(1, 0), col }; m_resultGlintVtx[5] = { brc, XMFLOAT2(1, 1), col };
    m_resultGlintMesh->Write(m_resultGlintVtx.data());
    XMFLOAT4X4 camM[2] = { cam->GetView(), cam->GetProj() };
    vs->WriteBuffer(0, camM);
    SetBlendMode(BLEND_ADD);
    SetDepthTest(DEPTH_ENABLE_TEST);
    vs->Bind();
    ps->Bind();
    m_resultGlintMesh->Draw(QUAD_VERTS);
    SetBlendMode(BLEND_ALPHA);
    SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

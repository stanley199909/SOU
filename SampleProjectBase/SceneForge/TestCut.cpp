// Part of SceneForge: the test cut (tameshigiri). 2026-10-20 stage 1, 10-21 made a recipe step,
// 10-22 one focused strike instead of a combo.
// This file carries a UTF-8 BOM so the Japanese text compiles correctly under MSVC.
//
// The step (data: WeaponRecipe, state: TestCutStep):
//   tempering done -> the screen fades and the hilt is fitted (FitHilt); from now on the sword is
//   held in the hand, also while walking (low guard). At the dummy, E = stand ready (feet fixed,
//   free mouse look). Then:
//     hold  = raise the sword over the right shoulder (Vom Tag) and FOCUS: the aim drifts with the
//             breath; at the end of the out-breath it is stillest (a glint on the tip) -- release then
//     release = the strike (Zornhau): one diagonal cut from the upper right to the lower left
//   One strike's strength = base x the whole-forge grade (GradeScore) x how still the aim was.
//
// The dummy is three kinds of part (user's design 2026-10-21): the post only holds it up, the straw
// body only gives the armor its shape, the ARMOR is what tests the sword. So:
//   - a slash that crosses armor is judged by durability (bounces until the armor gives);
//   - a slash that misses the armor (post / straw only) still counts as a stroke, the dummy is cut,
//     and after a moment a fresh dummy is stood up ("aim at the armor") -- no shortcut through straw;
//   - cutting the torso armor in two = the step is done. Fewer strokes = a better sword.
//
// Feel (user 2026-10-21: "fast is not the same as satisfying" -- God of War (2018) / Capcom swords):
//   the weight comes from CONTRAST and from the target's reaction, not from raw speed:
//   a visible wind-up that cocks the blade on screen -> a swing that accelerates into the hit ->
//   a heavy, decelerating follow-through; the body leans into it (camera), the hit freezes for a
//   moment scaled by power (hit stop), sparks fly off the armor, the cut part is thrown along the
//   swing; a clean cut in two hangs a beat before the half slides off, in slow motion, with the
//   "一刀両断" brush stroke slammed onto the screen.
//
// Split of responsibilities:
//   here (SceneForge) = HOW the player swings: input, the swing animation, the strength, feedback;
//   CutTarget         = WHAT gets cut: shape, kinds, durability, slicing, falling pieces.
//
// The swing is a procedural animation (no clips): a sword pose = where the guard (the hand) is +
// where the blade points + where the edge faces, built from the camera's axes along an arc.
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Audio.h"
#include "InputActions.h"
#include "Haptics.h"
#include "AimSystem.h"
#include "LitRender.h"
#include "PropMaterials.h"
#include "TextureCache.h"
#include "Texture.h"
#include "MeshBuffer.h"
#include "DebugUI.h"
#include "Lerp.h"
#include "imgui.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "assimp/config.h"
#include <cmath>
#include <cstring>
#include <cctype>

using namespace DirectX;

namespace
{
    // --- assets (ChatGPT-made models 2026-10-21, see CREDITS.txt). Missing files fall back to boxes. ---
    // Model files without the extension: the first of MODEL_EXTENSIONS that assimp can read is used.
    //   (2026-10-21: assimp could not parse the ChatGPT FBX -- "FBX-Parser ... unexpected data type" --
    //    so an OBJ / glTF export of the same model can simply be dropped next to it.)
    const char* const DUMMY_MODEL  = "Assets/Model/TestCut/Armored_Straw_Dummy";
    const char* const HILT_MODEL   = "Assets/Model/TestCut/Medieval_Sword_Hilt";
    const char* const MODEL_EXTENSIONS[] = { ".obj", ".glb", ".gltf", ".fbx" };
    const char* const SEVER_BRUSH  = "Assets/Model/TestCut/Ichitou_Ryoudan.png";   // 「一刀両断」 brush calligraphy
    const char* const HILT_GUARD_MESH = "Guard", * const HILT_GRIP_MESH = "Grip", * const HILT_POMMEL_MESH = "Pommel";
    const char* const HILT_METAL_KEY = "HiltMetal";   // PropMaterials: guard + pommel
    const char* const HILT_GRIP_KEY  = "HiltGrip";    // PropMaterials: the grip
    // A model in centimetres instead of metres would be ~100x too tall; anything taller than this
    // many times the expected height is taken to be in centimetres.
    const float CM_DETECT_FACTOR = 10.0f;
    const float CM_TO_M = 0.01f;

    // Base color images of the parts (the models have UVs but no textures).
    #define COTTAGE_TEX "Assets/Medieval_Blacksmith_Cottage_Production/Medieval_Blacksmith_Cottage_Production/07_Textures/"
    const char* const WOOD_TEX  = COTTAGE_TEX "Wood_Dark_Aged_BaseColor_2048.png";
    const char* const IRON_TEX  = COTTAGE_TEX "Iron_Rusted_BaseColor_2048.png";
    const char* const STRAW_TEX = "Assets/Cottage_Materials/wood_planks_base.jpg";   // tinted toward straw by the material table
    #undef COTTAGE_TEX

    const float PLACE_DISTANCE = 1.6f;    // F4 "Place in front of me": metres ahead of the player

    // Layout of the box stand-in (and the expected heights of the model): parts STACKED along y,
    // never overlapping (2026-10-20 F5: nested parts made two caps on the same plane = z-fighting),
    // each starting PART_GAP above the last (touching faces would z-fight too).
    const float PART_GAP    = 0.002f;
    const float POST_TOP    = 0.75f;      // post: ground .. here
    const float WAIST_TOP   = 1.00f;      // straw body (hips): post .. here
    const float TORSO_TOP   = 1.52f;      // armored torso: waist .. here
    const float HELMET_TOP  = 1.80f;      // helmet: torso .. here (= the dummy's height)
    const float POST_HALF_W = 0.06f;
    const XMFLOAT2 WAIST_HALF(0.20f, 0.14f);
    const XMFLOAT2 TORSO_HALF(0.23f, 0.17f);
    const float HELMET_HALF_W = 0.13f;
    // Durability = the damage one part must take before a slash goes through it.
    //   Balance: damage = move base x grade. Heavy (1.10) x an S-grade sword (>= 0.9) >= 0.95 =
    //   the armored torso in ONE slash; anything weaker needs two or more. The post and the straw
    //   are soft (cheap materials), but a slash that cuts only them does not count (see the header).
    const float POST_DURABILITY   = 0.35f;
    const float STRAW_DURABILITY  = 0.30f;
    const float ARMOR_DURABILITY  = 0.95f;
    const float HELMET_DURABILITY = 0.95f;

    using Kind = CutTarget::PartKind;
    // Box spanning y0..y1 (with the gap at the bottom).
    CutTarget::PartDef Stacked(const char* mesh, const char* material, const char* image, float y0, float y1,
                               float halfX, float halfZ, float durability, Kind kind, bool core)
    {
        const float b = y0 + PART_GAP;
        return { mesh, material, image, XMFLOAT3(0.0f, (b + y1) * 0.5f, 0.0f), XMFLOAT3(halfX, (y1 - b) * 0.5f, halfZ),
                 durability, kind, core };
    }

    // --- the strike (seconds). One decisive cut from the guard -- no combo (user 2026-10-22: the theme is
    //     "cut in two in one stroke"). Its weight comes from the contrast: a long, tense guard, then a
    //     swing that accelerates into the hit and drags through, then a slow return. ---
    const float RAISE_SEC          = 0.22f;   // pressing: the sword rises to the guard over the shoulder in this time
    const float RAISE_OVERSHOOT    = 1.4f;    // ease-out-back: the raise overshoots a little and settles (a "snap")
    const float SWING_SEC          = 0.16f;   // guard -> follow-through
    const float SWING_HIT_FRAC     = 0.5f;    // share of the swing before the hit (accelerating) ...
    const float SWING_IN_POW       = 1.6f;    // ... ease-in into the hit (fastest at the hit)
    const float SWING_OUT_POW      = 3.0f;    // ... then ease-out: the follow-through drags to a stop (weight)
    const float SWING_BLEND_FRAC   = 0.25f;   // share of the swing spent blending from the start pose into the arc
    const float RECOIL_SEC         = 0.16f;   // bounced: the blade springs back
    const float RECOIL_THETA       = XMConvertToRadians(-40.0f);   // ... to this angle of the arc
    const float RECOVER_SEC        = 0.60f;   // back to the low guard (slow = the sword is heavy, and the moment is over)
    const float HITSTOP_CUT_SEC    = 0.09f;   // the blade "catches" as it goes through (hit stop)
    const float HITSTOP_BOUNCE_SEC = 0.12f;
    const float STRIKE_CONE = XMConvertToRadians(35.0f);   // the dummy counts as "in front" within this angle of the view (the blade sweeps wide)

    // --- focus (holding the guard): the aim drifts with the breath ---
    // Holding a raised sword, the aim sways with each breath; at the END of the out-breath the body is
    // stillest -- that is when to release (archery and swordsmanship both teach this). sway 1 = breath
    // drawn in (moving), 0 = breath let out (still). Held too long, the arms tire and the focus breaks.
    const float BREATH_SEC       = 2.6f;      // one breath (real seconds; starts drawn in)
    const float SWAY_MIN_RAD     = 0.002f;    // aim drift at the stillest moment
    const float SWAY_MAX_RAD     = 0.035f;    // ... and at its most restless
    const float SWAY_SPEED       = 1.3f;      // how fast the drift wanders (noise time scale)
    const float SWAY_SEED_X = 3.7f, SWAY_SEED_Y = 8.1f;   // two independent drifts for the two axes (any distinct values)
    const float TIRE_START_SEC   = 2.0f * BREATH_SEC;   // after two breaths the arms begin to tire ...
    const float TIRE_RAMP_SEC    = 1.5f;      // ... and the sway climbs to the maximum over this time ...
    const float BREAK_SEC        = TIRE_START_SEC + TIRE_RAMP_SEC + 1.0f;   // ... and the focus breaks here
    const float FOCUS_POWER_MIN  = 0.6f;      // strength x this at the worst moment, x 1 at the stillest
    const float STILL_RELEASE    = 0.7f;      // a release at least this still counts as "focused" (else the lesson reminds how)
    const float STILL_GLINT_SWAY = 0.08f;     // below this sway the tip glints once (the cue: now)
    const float HEARTBEAT_SEC    = 0.85f;     // a heartbeat while focusing
    const float HEARTBEAT_VOLUME = 0.35f, HEARTBEAT_PITCH = 0.45f;   // a dull, low thump
    const float FOCUS_FADE_LAMBDA = 6.0f;     // how fast the focus look (dark edges, narrow view) comes and goes
    const float FOCUS_FOV_NARROW  = 0.12f;    // share the view narrows while focusing (the world closes in)
    const float RETICLE_R_MIN = 0.006f, RETICLE_R_GROW = 0.030f;   // aim ring radius (screen-height ratio): still .. restless
    const float RETICLE_W = 0.0025f;          // ring line width (screen-height ratio)
    const ImU32 RETICLE_COL = IM_COL32(255, 240, 220, 200);
    const float VIGNETTE_DEPTH = 0.22f;       // dark edge band (share of the screen from each side)
    const int   VIGNETTE_ALPHA = 170;         // darkness at the very edge (0..255)

    // --- the swing arc ---
    // The blade turns in the plane spanned by the line of sight F and the swing direction s:
    //   dir(theta) = cos(theta) F + sin(theta) s
    // theta < 0 = raised on the side it comes from, 0 = straight at the target (the hit), > 0 = followed
    // through to the other side. The strike: Zornhau ("wrath strike") from the guard over the right
    // shoulder (Vom Tag) diagonally down to the lower left. Search: "Vom Tag longsword", "Zornhau".
    const float WIND_THETA = XMConvertToRadians(-80.0f);   // the guard: blade up over the right shoulder
    const float ARC_FWD    = XMConvertToRadians(100.0f);   // follow-through angle
    const float HAND_RADIUS = 0.24f;   // the hand travels a smaller arc than the blade (m)
    const float HAND_FWD    = 0.30f;   // the arc's center is this far ahead of the eye
    const float HAND_DROP   = -0.20f;  // ... and this much below it
    const float HAND_SIDE   = 0.10f;   // ... and a little to the right (a right-handed swing)
    const float BLADE_ROLL  = XMConvertToRadians(35.0f);   // turn the flat a little toward the camera (else
                                                           // the blade is seen edge-on = a thin stick). Feel > exact edge alignment.

    // --- low guard (walking / not raised; user's sketch 2026-10-22): the sword held low in front,
    //     pointing forward and a little up to the right ---
    const float LOW_FWD = 0.42f, LOW_RIGHT = -0.04f, LOW_UP = -0.40f;           // guard (hand)
    const float LOW_DIR_FWD = 1.0f, LOW_DIR_RIGHT = 0.60f, LOW_DIR_UP = 0.45f;   // blade direction weights
    const int   AVOID_STEPS = 6;       // low guard / recover: lift the blade toward upright in this many steps while it would enter the dummy

    // --- the body behind the blade (camera) ---
    const float LEAN = 0.08f;          // rad: the view turns along the swing as the body commits
    const float LUNGE = 0.10f;         // m: the eye moves forward into the cut
    const float COIL = 0.35f;          // while in the guard, the body turns the other way by this share (the coil before the release)
    const float LEAN_LAMBDA = 22.0f;   // 1/s: how fast the lean follows (frame-rate independent damping)
    const float FOV_PUNCH_CUT = 0.07f, FOV_PUNCH_SEVER = 0.10f;   // share the view narrows on impact
    const float FOV_LAMBDA = 9.0f;

    // --- camera kick when the blade lands (rad, decays back) ---
    const float KICK_CUT    = 0.06f;   // along the swing (follows through)
    const float KICK_BOUNCE = 0.06f;   // against the swing (thrown back)
    const float KICK_LAMBDA = 16.0f;

    // --- the cut part is thrown along the swing (x CutTarget::pushSpeed) ---
    const float PUSH_SCALE = 1.8f;

    // --- sparks off the armor ---
    const int   SPARKS_CUT = 34, SPARKS_BOUNCE = 24;
    const float SPARK_SPREAD = 0.7f, SPARK_SPEED_MIN = 2.0f, SPARK_SPEED_MAX = 6.5f;

    // --- weapon trail ---
    const int   TRAIL_SEGMENTS = 24;          // samples along the arc
    const float TRAIL_ARC      = XMConvertToRadians(130.0f);   // longest streak behind the blade
    const float TRAIL_INNER    = 0.30f;       // the streak starts this share of the blade out from the guard
    const float TRAIL_FADE_SEC = 0.20f;       // after the swing it fades out in this time
    const XMFLOAT4 TRAIL_COLOR(1.0f, 0.88f, 0.70f, 1.0f);

    // --- the stillest moment of the breath: a glint at the tip ---
    const float CHARGE_GLINT_SEC = 0.30f, CHARGE_GLINT_SIZE = 0.16f, CHARGE_GLINT_BRIGHT = 3.0f;
    const XMFLOAT4 CHARGE_GLINT_COL(1.0f, 0.95f, 0.85f, 1.0f);

    // --- a slash that missed the armor: a fresh dummy after a moment ---
    const float RETRY_DELAY_SEC = 1.6f;

    // --- feedback ---
    const float SWING_VOLUME = 0.8f, SWING_PITCH = 0.85f;      // the whoosh (low = heavy)
    const float CUT_VOLUME = 0.7f, CUT_PITCH = 1.1f;            // a thud as it goes through
    const float CLANG_VOLUME = 0.9f, CLANG_PITCH = 1.35f;       // bounced off metal
    const float THUD_VOLUME = 0.8f, THUD_PITCH = 1.0f;          // bounced off straw / wood
    const float CUT_SHAKE = 0.55f, BOUNCE_SHAKE = 0.8f;         // camera shake (m_shake)
    const Rumble RUMBLE_CUT    = { 0.7f, 0.8f, 0.0f, 0.9f };
    const Rumble RUMBLE_BOUNCE = { 0.9f, 0.4f, 0.0f, 1.0f };
    const float RUMBLE_CUT_SEC = 0.22f, RUMBLE_BOUNCE_SEC = 0.25f;
    const unsigned int VOICE_COL = IM_COL32(255, 200, 90, 255);   // the smith's mutter (same color as the other lines)

    // --- cut in two: the payoff, by how many strokes it took (data; the first row that fits) ---
    struct SeverTier
    {
        int   maxStrokes;     // this row applies up to this many strokes
        float holdSec;        // the cut-off half hangs this long before it slides (the "beat")
        float slowmoSec;      // real seconds of slow motion
        float slowmoScale;    // time scale during it
        bool  brush;          // the 「一刀両断」 brush image (else the "N太刀で両断" text)
        float slamFrom;       // the banner slams in from this scale ...
        float slamShake;      // ... and shakes the camera this much when it lands (0 = it just fades in)
        float hitstopSec;     // the freeze at the moment of the cut
    };
    const SeverTier SEVER_TIERS[] = {
        //  strokes   hold   slow-mo  scale  brush  slam  shake  hitstop
        {  1,        0.40f, 0.80f,   0.25f, true,  2.6f, 1.0f,  0.16f },   // 一刀両断: the special one
        {  3,        0.20f, 0.00f,   1.00f, false, 1.8f, 0.45f, 0.11f },   // two or three strokes: a lesser slam
        { INT_MAX,   0.00f, 0.00f,   1.00f, false, 1.0f, 0.0f,  0.09f },   // more: just shown
    };
    const SeverTier& TierFor(int strokes)
    {
        for (const SeverTier& t : SEVER_TIERS) if (strokes <= t.maxStrokes) return t;
        return SEVER_TIERS[_countof(SEVER_TIERS) - 1];
    }
    const float BANNER_SLAM_SEC  = 0.18f;   // scale-down time of the slam
    const float BANNER_SLAM_BACK = 1.7f;    // ease-out-back: overshoots smaller then settles (the "thud")
    const float BANNER_HOLD_SEC  = 2.0f;    // then stays ...
    const float BANNER_FADE_SEC  = 0.4f;    // ... and fades
    const float BANNER_Y         = 0.38f;   // vertical position (screen-height ratio)
    const float BANNER_W         = 0.62f;   // brush image width (screen-width ratio)
    const float BANNER_TEXT_H    = 0.075f;  // text height (screen-height ratio)
    const float BANNER_SLAM_VOLUME = 1.0f, BANNER_SLAM_PITCH = 0.55f;   // a deep "dong" as it lands

    // Share of the blade length searched at each end to tell the tip (narrow) from the tang end (wide).
    const float TIP_END_FRAC = 0.1f;

    XMVECTOR Orthonormal(FXMVECTOR v, FXMVECTOR against)   // v minus its part along 'against', length 1
    {
        XMVECTOR o = XMVectorSubtract(v, XMVectorScale(against, XMVectorGetX(XMVector3Dot(v, against))));
        const float SMALL = 1e-8f;
        if (XMVectorGetX(XMVector3LengthSq(o)) < SMALL) return XMVector3Orthogonal(against);
        return XMVector3Normalize(o);
    }

    std::shared_ptr<MeshBuffer> UploadMesh(const MeshSlicer::Mesh& m)
    {
        MeshBuffer::Description d = {};
        d.pVtx = m.verts.data(); d.vtxSize = sizeof(MeshSlicer::Vertex); d.vtxCount = (UINT)m.verts.size();
        d.pIdx = m.indices.data(); d.idxSize = sizeof(uint32_t); d.idxCount = (UINT)m.indices.size();
        d.isWrite = false; d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        return std::make_shared<MeshBuffer>(d);
    }

    void Append(MeshSlicer::Mesh& dst, const MeshSlicer::Mesh& src)
    {
        const uint32_t base = (uint32_t)dst.verts.size();
        dst.verts.insert(dst.verts.end(), src.verts.begin(), src.verts.end());
        for (uint32_t i : src.indices) dst.indices.push_back(base + i);
    }

    bool ContainsNoCase(const char* text, const char* word)
    {
        const size_t n = strlen(word);
        for (const char* p = text; *p; ++p)
        {
            size_t k = 0;
            while (k < n && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)word[k])) ++k;
            if (k == n) return true;
        }
        return false;
    }

    // Read every mesh of a model into slicer meshes, keyed by mesh name (left-handed like the other
    // models, vertices pre-transformed, identical vertices joined = an indexed closed surface).
    // basePath has no extension: MODEL_EXTENSIONS are tried in order. error = why nothing was read.
    // expectedHeight: the unit check (see CM_DETECT_FACTOR); 0 = no check.
    struct NamedMesh { std::string name; MeshSlicer::Mesh mesh; };
    std::vector<NamedMesh> LoadMeshes(const char* basePath, float expectedHeight, std::string& error)
    {
        std::vector<NamedMesh> out;
        Assimp::Importer imp;
        // Pre-transforming would MERGE meshes that share a material (all 4 parts came out as one mesh,
        // 2026-10-22). Keeping the hierarchy keeps each part its own mesh.
        imp.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
        const unsigned int flags = aiProcess_Triangulate | aiProcess_ConvertToLeftHanded | aiProcess_PreTransformVertices
                                 | aiProcess_JoinIdenticalVertices;
        const aiScene* sc = nullptr;
        error.clear();
        for (const char* ext : MODEL_EXTENSIONS)
        {
            const std::string path = std::string(basePath) + ext;
            FILE* fp = nullptr;
            if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) continue;   // no such file
            fclose(fp);
            sc = imp.ReadFile(path, flags);
            if (sc) break;
            error += std::string(ext) + ": " + imp.GetErrorString() + "  ";
        }
        if (!sc) { if (error.empty()) error = "no model file"; return out; }
        float minY = FLT_MAX, maxY = -FLT_MAX;
        for (unsigned int m = 0; m < sc->mNumMeshes; ++m)
        {
            const aiMesh* me = sc->mMeshes[m];
            NamedMesh nm;
            nm.name = me->mName.C_Str();
            for (unsigned int j = 0; j < me->mNumVertices; ++j)
            {
                MeshSlicer::Vertex v;
                v.pos = XMFLOAT3(me->mVertices[j].x, me->mVertices[j].y, me->mVertices[j].z);
                const aiVector3D n = me->HasNormals() ? me->mNormals[j] : aiVector3D(0, 1, 0);
                v.normal = XMFLOAT3(n.x, n.y, n.z);
                const aiVector3D uv = me->HasTextureCoords(0) ? me->mTextureCoords[0][j] : aiVector3D(0, 0, 0);
                v.uv = XMFLOAT2(uv.x, uv.y);
                nm.mesh.verts.push_back(v);
                minY = fminf(minY, v.pos.y); maxY = fmaxf(maxY, v.pos.y);
            }
            for (unsigned int f = 0; f < me->mNumFaces; ++f)
            {
                const aiFace& fa = me->mFaces[f];
                if (fa.mNumIndices != 3) continue;
                for (int k = 0; k < 3; ++k) nm.mesh.indices.push_back(fa.mIndices[k]);
            }
            out.push_back(std::move(nm));
        }
        if (expectedHeight > 0.0f && maxY - minY > expectedHeight * CM_DETECT_FACTOR)
            for (NamedMesh& nm : out)
                for (MeshSlicer::Vertex& v : nm.mesh.verts) v.pos = XMFLOAT3(v.pos.x * CM_TO_M, v.pos.y * CM_TO_M, v.pos.z * CM_TO_M);
        return out;
    }
}

// The dummy, local space: the foot of the post at the origin, y up, metres. Mesh names = the model's.
const CutTarget::PartDef SceneForge::CUT_PARTS[] = {
    //       mesh      material     image      bottom      top         half x         half z         durability         kind          core
    Stacked("Post",   "CutPost",   WOOD_TEX,  0.0f,       POST_TOP,   POST_HALF_W,   POST_HALF_W,   POST_DURABILITY,   Kind::Stand,  false),   // holds it up
    Stacked("Straw",  "CutStraw",  STRAW_TEX, POST_TOP,   WAIST_TOP,  WAIST_HALF.x,  WAIST_HALF.y,  STRAW_DURABILITY,  Kind::Filler, false),   // shapes it
    Stacked("Armor",  "CutArmor",  IRON_TEX,  WAIST_TOP,  TORSO_TOP,  TORSO_HALF.x,  TORSO_HALF.y,  ARMOR_DURABILITY,  Kind::Armor,  true ),   // the torso armor = the goal
    Stacked("Helmet", "CutHelmet", IRON_TEX,  TORSO_TOP,  HELMET_TOP, HELMET_HALF_W, HELMET_HALF_W, HELMET_DURABILITY, Kind::Armor,  false),   // armor, but not the torso
};
const int SceneForge::NUM_CUT_PARTS = _countof(SceneForge::CUT_PARTS);

//====================================================================
//  models
//====================================================================
//--- The dummy's parts from the model, matched to CUT_PARTS by mesh name. Done once; a part the
//    model does not have stays a box.
void SceneForge::LoadCutModels()
{
    if (m_cutModelsLoaded) return;
    m_cutModelsLoaded = true;
    m_cutModelParts.assign(NUM_CUT_PARTS, MeshSlicer::Mesh());
    for (NamedMesh& nm : LoadMeshes(DUMMY_MODEL, HELMET_TOP, m_cutModelError))
        for (int i = 0; i < NUM_CUT_PARTS; ++i)
            if (m_cutModelParts[i].Empty() && ContainsNoCase(nm.name.c_str(), CUT_PARTS[i].meshName))
                m_cutModelParts[i] = std::move(nm.mesh);
    m_cutModelFound = 0;
    for (const MeshSlicer::Mesh& m : m_cutModelParts) if (!m.Empty()) ++m_cutModelFound;
}

//--- The hilt: guard + pommel (metal) and grip, from the model or boxes.
void SceneForge::BuildHilt()
{
    MeshSlicer::Mesh metal, grip;
    for (NamedMesh& nm : LoadMeshes(HILT_MODEL, 0.0f, m_hiltModelError))
    {
        if (ContainsNoCase(nm.name.c_str(), HILT_GRIP_MESH)) Append(grip, nm.mesh);
        else if (ContainsNoCase(nm.name.c_str(), HILT_GUARD_MESH) || ContainsNoCase(nm.name.c_str(), HILT_POMMEL_MESH)) Append(metal, nm.mesh);
    }
    if (metal.Empty() || grip.Empty())   // no model: boxes in the same frame (origin = guard, +y = toward the tip)
    {
        const XMFLOAT3 GUARD_HALF(0.11f, 0.015f, 0.022f);
        const float GRIP_LEN = 0.20f, GRIP_HALF_W = 0.018f, POMMEL_HALF = 0.03f;
        metal = MakeBoxMesh(XMFLOAT3(0.0f, -GUARD_HALF.y, 0.0f), GUARD_HALF);
        Append(metal, MakeBoxMesh(XMFLOAT3(0.0f, -(2.0f * GUARD_HALF.y + GRIP_LEN + POMMEL_HALF), 0.0f), XMFLOAT3(POMMEL_HALF, POMMEL_HALF, POMMEL_HALF)));
        grip = MakeBoxMesh(XMFLOAT3(0.0f, -(2.0f * GUARD_HALF.y + GRIP_LEN * 0.5f), 0.0f), XMFLOAT3(GRIP_HALF_W, GRIP_LEN * 0.5f, GRIP_HALF_W));
    }
    m_hiltMetal = UploadMesh(metal);
    m_hiltGrip  = UploadMesh(grip);
}

//====================================================================
//  reset / per-frame
//====================================================================
void SceneForge::ResetTestCut()
{
    // The meshes are only marked for rebuilding (next update). ResetRunState also runs at
    // shutdown, where creating GPU buffers would be wasted (or unsafe).
    m_cutNeedsBuild  = true;
    m_cutStrikes     = 0;
    m_focusTime = m_focusSway = m_focusQuality = m_focusFade = 0.0f;
    m_focusAim       = XMFLOAT2(0.0f, 0.0f);
    m_cutHits        = 0;
    m_cutSevered     = false;
    m_cutSeveredHits = 0;
    m_cutLookYaw = m_cutLookPitch = 0.0f;
    m_slashPhase   = SlashPhase::Idle;
    m_slashT = m_slashHitstop = 0.0f;
    m_slashStruck  = false;
    m_trailAlpha   = 0.0f;
    m_cutKick      = XMFLOAT2(0.0f, 0.0f);
    m_cutLean = m_cutFovPunch = m_cutSlowmo = 0.0f;
    m_cutRetryAt   = -1.0f;
    m_cutBannerAt  = -1.0f;
    m_chargeGlintAt = -1.0f;
}

//--- Time scale of the test cut (slow motion after a clean cut). Real seconds keep the slow-mo timer.
float SceneForge::TestCutTimeScale() const
{
    return (m_cutSlowmo > 0.0f) ? m_cutSlowmoScale : 1.0f;
}

void SceneForge::UpdateTestCut(float tick)
{
    if (m_cutNeedsBuild)
    {
        LoadCutModels();
        m_cutTarget.Build(CUT_PARTS, NUM_CUT_PARTS, &m_cutModelParts);
        m_cutNeedsBuild = false;
    }
    m_cutTarget.SetPlacement(XMFLOAT3(m_cutPosXZ[0], m_walkFloorY, m_cutPosXZ[1]), m_cutYaw);   // follows F4 "place in front of me"
    m_cutTarget.Update(tick * TestCutTimeScale());
    if (m_cutSlowmo > 0.0f) m_cutSlowmo -= tick;
}

//====================================================================
//  the hilt: fitted while the screen is black, as the test-cut step begins
//====================================================================
//--- Tempering is done and the blade is still in the tongs: fit the hilt behind a fade. From then on
//    the sword is held in the hand (no more heating = no more tongs, user 2026-10-22). Called by
//    TestCutStep on entry; does nothing when the sword is already in the hand (F4 jump / lesson start).
void SceneForge::FitHilt()
{
    if (!m_carrying || m_workAt == Station::TestCut) return;
    m_fade.Transition([this] {
        m_carrying  = false;                    // the tongs let go (they go back to the hip)
        m_workAt    = Station::TestCut;         // the sword now belongs to the test cut: in the hand
        m_slashPhase = SlashPhase::Idle;
        m_vmFwdInit = false;                    // the sword's sway starts from the current view
    });
}

//====================================================================
//  the sword in the hand
//====================================================================
//--- The hilted sword is in the hand: from FitHilt on, walking and at the dummy alike.
bool SceneForge::SwordInHand() const
{
    return m_state == GAME_PLAY && m_workAt == Station::TestCut && !m_carrying;
}

//--- Standing ready at the dummy (feet fixed): the guard and the strike can be used.
bool SceneForge::SwordAtStation() const
{
    return SwordInHand() && AtStation(Station::TestCut);
}

//--- How good a release right now would be: 1 at the stillest moment of the breath, 0 at the most
//    restless; still low while the sword is being raised.
float SceneForge::FocusQualityNow() const
{
    const float raised = fminf(m_focusTime / RAISE_SEC, 1.0f);
    return (1.0f - m_focusSway) * raised;
}

//--- Which end of the blade is the tip: the narrower end (the tang end stays wide). Looked up once
//    on the finished shape; the morph never changes which end is which.
int SceneForge::SwordTipSign()
{
    if (m_swordTipSign != 0) return m_swordTipSign;
    m_swordTipSign = 1;
    if (m_wpStage.empty() || m_wpThickAxis < 0) return m_swordTipSign;
    const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
    const int wa = 3 - la - m_wpThickAxis;
    const std::vector<XMFLOAT3>& pos = m_wpStage.back().pos;
    const float lo = (&m_wpFinMin.x)[la], hi = (&m_wpFinMax.x)[la];
    const float band = (hi - lo) * TIP_END_FRAC;
    float minW[2] = { FLT_MAX, FLT_MAX }, maxW[2] = { -FLT_MAX, -FLT_MAX };   // [0] = low end, [1] = high end
    for (const XMFLOAT3& p : pos)
    {
        const float a = (&p.x)[la], w = (&p.x)[wa];
        const int end = (a <= lo + band) ? 0 : (a >= hi - band) ? 1 : -1;
        if (end < 0) continue;
        minW[end] = fminf(minW[end], w); maxW[end] = fmaxf(maxW[end], w);
    }
    const float widthLow = maxW[0] - minW[0], widthHigh = maxW[1] - minW[1];
    m_swordTipSign = (widthHigh <= widthLow) ? 1 : -1;
    return m_swordTipSign;
}

//--- Rotation of the held sword: the blade's local axes onto the pose's world axes.
//    long axis (toward the tip) -> pose.dir, width axis -> pose.edge, thickness -> the third axis.
//    Rows of a DirectX matrix are the images of the local x, y, z axes (row vectors: v * M).
XMMATRIX SceneForge::SwordRotation() const
{
    const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
    const int ta = (m_wpThickAxis >= 0) ? m_wpThickAxis : (la + 1) % 3;
    const int wa = 3 - la - ta;
    const XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&m_swordPose.dir));
    const XMVECTOR e = Orthonormal(XMLoadFloat3(&m_swordPose.edge), d);
    XMVECTOR rows[3];
    rows[la] = XMVectorScale(d, (float)(m_swordTipSign != 0 ? m_swordTipSign : 1));
    rows[wa] = e;
    rows[ta] = XMVector3Cross(d, e);
    XMMATRIX m(rows[0], rows[1], rows[2], XMVectorSet(0, 0, 0, 1));
    if (XMVectorGetX(XMMatrixDeterminant(m)) < 0.0f)   // keep it a rotation (no mirror)
    {
        rows[ta] = XMVectorNegate(rows[ta]);
        m = XMMATRIX(rows[0], rows[1], rows[2], XMVectorSet(0, 0, 0, 1));
    }
    return m;
}

//--- Where the blade's box center must be so that its tang end sits at the guard (the hand).
//    WeaponWorld maps local p to ((p - c) * fit) * R + a, so a = guard - ((grip - c) * fit) * R.
XMFLOAT3 SceneForge::SwordCenter()
{
    const int sign = SwordTipSign();
    const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
    const XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
    XMFLOAT3 grip = c;
    (&grip.x)[la] = (sign > 0) ? (&m_wpFinMin.x)[la] : (&m_wpFinMax.x)[la];
    const float ex = m_wpMax.x - m_wpMin.x, ey = m_wpMax.y - m_wpMin.y, ez = m_wpMax.z - m_wpMin.z;
    const float MIN_EXTENT = 1e-5f;
    const float fit = (m_barLen / fmaxf(fmaxf(ex, fmaxf(ey, ez)), MIN_EXTENT)) * m_wpScale;
    const XMVECTOR off = XMVector3TransformNormal(XMVectorSet((grip.x - c.x) * fit, (grip.y - c.y) * fit, (grip.z - c.z) * fit, 0), SwordRotation());
    XMFLOAT3 a; XMStoreFloat3(&a, XMVectorSubtract(XMLoadFloat3(&m_swordPose.guard), off));
    return a;
}

//--- Length from the guard to the tip (m): the finished blade along its long axis, at the fit scale.
float SceneForge::SwordLength()
{
    const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
    const float ex = m_wpMax.x - m_wpMin.x, ey = m_wpMax.y - m_wpMin.y, ez = m_wpMax.z - m_wpMin.z;
    const float MIN_EXTENT = 1e-5f;
    const float fit = (m_barLen / fmaxf(fmaxf(ex, fmaxf(ey, ez)), MIN_EXTENT)) * m_wpScale;
    return ((&m_wpFinMax.x)[la] - (&m_wpFinMin.x)[la]) * fit;
}

//====================================================================
//  poses (built from the view-model camera axes: the sword follows the view like the tongs)
//====================================================================
//--- Low guard (walking, or standing without raising): the sword low in front, pointing forward and a
//    little up to the right. The flat faces up (the edge sideways) so the blade reads as a blade.
SceneForge::SwordPose SceneForge::LowGuardPose()
{
    SwordPose p = m_swordPose;
    XMVECTOR E, F, R, U;
    if (!CameraBasis(E, F, R, U)) return p;
    const XMVECTOR guard = E + F * LOW_FWD + R * LOW_RIGHT + U * LOW_UP;
    const XMVECTOR dir = XMVector3Normalize(F * LOW_DIR_FWD + R * LOW_DIR_RIGHT + U * LOW_DIR_UP);
    XMStoreFloat3(&p.guard, guard);
    XMStoreFloat3(&p.dir, dir);
    XMStoreFloat3(&p.edge, Orthonormal(XMVector3Cross(U, dir), dir));   // horizontal, across the blade
    return p;
}

//--- A point of the swing arc. theta in radians, s2 = swing direction on screen (right x / up y).
SceneForge::SwordPose SceneForge::SwingPose(float theta, XMFLOAT2 s2)
{
    SwordPose p = m_swordPose;
    XMVECTOR E, F, R, U;
    if (!CameraBasis(E, F, R, U)) return p;
    const XMVECTOR s = XMVector3Normalize(R * s2.x + U * s2.y);   // the swing direction (world)
    const float c = cosf(theta), sn = sinf(theta);
    const XMVECTOR dir = XMVector3Normalize(F * c + s * sn);        // the blade on the arc
    const XMVECTOR tangent = XMVector3Normalize(F * -sn + s * c);   // where the blade is moving = the leading edge
    // The hand's offset (below / right of the eye) is kept only along the swing plane: the arc then lies
    // IN the plane the hit is judged with (the one through the crosshair). 2026-10-22 F5: with the full
    // offset the blade swept a plane ~0.2 m beside the judged one -- it looked like a hit and was not.
    const XMVECTOR n = XMVector3Normalize(XMVector3Cross(F, s));
    XMVECTOR handOff = U * HAND_DROP + R * HAND_SIDE;
    handOff = XMVectorSubtract(handOff, XMVectorScale(n, XMVectorGetX(XMVector3Dot(handOff, n))));
    const XMVECTOR guard = E + F * HAND_FWD + handOff + dir * HAND_RADIUS;
    // roll the blade a little about its own axis so its flat catches the eye
    const XMVECTOR edge = tangent * cosf(BLADE_ROLL) + XMVector3Cross(dir, tangent) * sinf(BLADE_ROLL);
    XMStoreFloat3(&p.guard, guard);
    XMStoreFloat3(&p.dir, dir);
    XMStoreFloat3(&p.edge, Orthonormal(edge, dir));
    return p;
}

//--- Ready / recovering: if the blade would go into the dummy, lift it toward upright until it is clear
//    (the same idea as lifting a carried iron away from walls, Carry.cpp UpdateCarryAvoid).
void SceneForge::AvoidDummy(SwordPose& p)
{
    XMVECTOR E, F, R, U;
    if (!CameraBasis(E, F, R, U)) return;
    const float len = SwordLength();
    const XMVECTOR start = XMLoadFloat3(&p.dir);
    for (int k = 0; k <= AVOID_STEPS; ++k)
    {
        const XMVECTOR d = XMVector3Normalize(XMVectorLerp(start, U, (float)k / AVOID_STEPS));
        XMFLOAT3 df; XMStoreFloat3(&df, d);
        float t = 0.0f;
        if (!m_cutTarget.RayHit(p.guard, df, t) || t > len)
        {
            if (k > 0) { p.dir = df; XMStoreFloat3(&p.edge, Orthonormal(XMLoadFloat3(&p.edge), d)); }
            return;
        }
    }
}

namespace
{
    float Clamp01(float t) { return fminf(fmaxf(t, 0.0f), 1.0f); }
    float Ease(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }   // smoothstep
    // ease-out-back: passes 1 by a little and settles back (a snap). k = how far it overshoots.
    float EaseOutBack(float t, float k) { t = Clamp01(t) - 1.0f; return 1.0f + (k + 1.0f) * t * t * t + k * t * t; }
    // The swing's angle over its time: accelerate into the hit, then drag to a stop.
    float SwingTheta(float t)
    {
        t = Clamp01(t);
        if (t < SWING_HIT_FRAC)
        {
            const float u = t / SWING_HIT_FRAC;
            return WIND_THETA * (1.0f - powf(u, SWING_IN_POW));            // WIND_THETA -> 0, fastest at the hit
        }
        const float u = (t - SWING_HIT_FRAC) / (1.0f - SWING_HIT_FRAC);
        return ARC_FWD * (1.0f - powf(1.0f - u, SWING_OUT_POW));          // 0 -> ARC_FWD, slowing down
    }
}

//--- Blend two poses: positions linearly, directions by nlerp (lerp, then length 1).
SceneForge::SwordPose SceneForge::LerpPose(const SwordPose& a, const SwordPose& b, float t)
{
    SwordPose o;
    XMStoreFloat3(&o.guard, XMVectorLerp(XMLoadFloat3(&a.guard), XMLoadFloat3(&b.guard), t));
    const XMVECTOR d = XMVector3Normalize(XMVectorLerp(XMLoadFloat3(&a.dir), XMLoadFloat3(&b.dir), t));
    XMStoreFloat3(&o.dir, d);
    XMStoreFloat3(&o.edge, Orthonormal(XMVectorLerp(XMLoadFloat3(&a.edge), XMLoadFloat3(&b.edge), t), d));
    return o;
}

void SceneForge::BeginSlashPhase(SlashPhase p)
{
    m_swordFrom  = m_swordPose;
    m_slashPhase = p;
    m_slashT     = 0.0f;
}

//====================================================================
//  the guard, the focus and the strike (UpdatePlay, also while walking)
//====================================================================
void SceneForge::UpdateTestCutStation(float tick, bool inputOn)
{
    // Effects that settle in real time, also during the hit stop
    const float kickKeep = expf(-KICK_LAMBDA * tick);
    m_cutKick = XMFLOAT2(m_cutKick.x * kickKeep, m_cutKick.y * kickKeep);
    m_cutFovPunch *= expf(-FOV_LAMBDA * tick);
    m_focusFade = Lerp::Damp(m_focusFade, (m_slashPhase == SlashPhase::Focus) ? 1.0f : 0.0f, FOCUS_FADE_LAMBDA, tick);

    // A strike that missed the armor: stand a fresh dummy up (behind a fade). The strokes keep counting.
    if (m_cutRetryAt >= 0.0f && m_time >= m_cutRetryAt && !m_fade.IsBusy())
    {
        m_cutRetryAt = -1.0f;
        m_fade.Transition([this] { m_cutNeedsBuild = true; m_cutLookYaw = m_cutLookPitch = 0.0f; });
    }

    if (!SwordInHand()) { m_slashPhase = SlashPhase::Idle; m_trailAlpha = 0.0f; m_cutLean = 0.0f; return; }
    const float dt = tick * TestCutTimeScale();   // the swing slows down with the slow motion too
    if (m_slashPhase != SlashPhase::Swing && m_trailAlpha > 0.0f)
    {
        m_trailAlpha = fmaxf(m_trailAlpha - dt / TRAIL_FADE_SEC, 0.0f);
        m_trailTail  = Lerp::Linear(m_trailHead, m_trailTail, m_trailAlpha);   // the tail catches up with the head as it fades
    }
    // the body: coils back in the guard, commits along the swing, settles otherwise
    const float leanTarget = (m_slashPhase == SlashPhase::Focus) ? -COIL
                           : (m_slashPhase == SlashPhase::Swing) ? sinf(XM_PI * Clamp01(m_slashT)) : 0.0f;
    m_cutLean = Lerp::Damp(m_cutLean, leanTarget, LEAN_LAMBDA, dt);

    const bool canInput = inputOn && SwordAtStation() && !m_cutSevered && !m_clearDecided && m_cutRetryAt < 0.0f;
    m_slashDir = XMFLOAT2(-cosf(m_cutSlashTilt), -sinf(m_cutSlashTilt));   // Zornhau: upper right -> lower left
    // hit stop: the pose holds still for a moment (the camera keeps moving)
    if (m_slashHitstop > 0.0f) { m_slashHitstop -= tick; return; }

    switch (m_slashPhase)
    {
    case SlashPhase::Idle:
        m_swordPose = LowGuardPose();
        AvoidDummy(m_swordPose);
        if (canInput && InputActions::Pressed(Action::Use))
        {
            m_focusTime = 0.0f;
            m_heartbeatAt = m_time;
            BeginSlashPhase(SlashPhase::Focus);
        }
        break;

    case SlashPhase::Focus:
    {
        // raise to the guard over the shoulder (Vom Tag) with a little snap, then hold it there
        m_focusTime += tick;   // real time: the breath does not slow down
        m_swordPose = LerpPose(m_swordFrom, SwingPose(WIND_THETA, m_slashDir), EaseOutBack(m_focusTime / RAISE_SEC, RAISE_OVERSHOOT));
        // the breath: drawn in = restless (1), let out = still (0); tiring arms push it back up
        const float breath = 0.5f + 0.5f * cosf(XM_2PI * m_focusTime / BREATH_SEC);
        const float tire   = Clamp01((m_focusTime - TIRE_START_SEC) / TIRE_RAMP_SEC);
        const float before = m_focusSway;
        m_focusSway = fmaxf(breath, tire);
        if (before >= STILL_GLINT_SWAY && m_focusSway < STILL_GLINT_SWAY) m_chargeGlintAt = m_time;   // the cue: now
        const float amp = Lerp::Linear(SWAY_MIN_RAD, SWAY_MAX_RAD, m_focusSway);
        m_focusAim = XMFLOAT2(OrganicNoise(m_time * SWAY_SPEED, SWAY_SEED_X) * amp, OrganicNoise(m_time * SWAY_SPEED, SWAY_SEED_Y) * amp);
        if (m_time - m_heartbeatAt >= HEARTBEAT_SEC)
        {
            m_heartbeatAt = m_time;
            Audio::Play(Audio::SE_COLD, HEARTBEAT_VOLUME, HEARTBEAT_PITCH);
        }
        if (!canInput) { m_focusAim = XMFLOAT2(0, 0); BeginSlashPhase(SlashPhase::Recover); break; }   // e.g. the pause menu: lower the sword
        if (m_focusTime >= BREAK_SEC)
        {
            // held too long: the arms give out, the focus breaks -- lower the sword and start again
            Say((const char*)u8"集中が切れた…", VOICE_COL);
            m_focusAim = XMFLOAT2(0, 0);
            BeginSlashPhase(SlashPhase::Recover);
            break;
        }
        if (InputActions::Held(Action::Use)) break;
        // release: the strike
        m_focusQuality = FocusQualityNow();
        m_slashStruck  = false;
        ++m_cutStrikes;
        Audio::Play(Audio::SE_SWING, SWING_VOLUME, SWING_PITCH);
        m_trailDir   = m_slashDir;
        m_trailHead  = m_trailTail = WIND_THETA;
        m_trailAlpha = 1.0f;
        BeginSlashPhase(SlashPhase::Swing);
        break;
    }

    case SlashPhase::Swing:
    {
        m_slashT += dt / SWING_SEC;
        const float theta = SwingTheta(m_slashT);
        m_swordPose = LerpPose(m_swordFrom, SwingPose(theta, m_slashDir), Ease(m_slashT / SWING_BLEND_FRAC));
        m_trailHead = theta;
        m_trailTail = fmaxf(WIND_THETA, theta - TRAIL_ARC);
        if (!m_slashStruck && theta >= 0.0f)   // the blade passes straight in front: the hit
        {
            m_slashStruck = true;
            StrikeAtCrosshair();                       // may switch to Recoil (bounced)
            m_focusAim = XMFLOAT2(0, 0);               // the aim was spent on this strike
            if (m_slashPhase != SlashPhase::Swing) break;
        }
        if (m_slashT < 1.0f) break;
        BeginSlashPhase(SlashPhase::Recover);
        break;
    }

    case SlashPhase::Recoil:
        m_slashT += dt / RECOIL_SEC;
        m_swordPose = LerpPose(m_swordFrom, SwingPose(RECOIL_THETA, m_slashDir), Ease(m_slashT));
        if (m_slashT < 1.0f) break;
        BeginSlashPhase(SlashPhase::Recover);
        break;

    case SlashPhase::Recover:
    {
        // back to the low guard; the next strike needs a new focus (no combo)
        m_slashT += dt / RECOVER_SEC;
        SwordPose low = LowGuardPose();
        AvoidDummy(low);
        m_swordPose = LerpPose(m_swordFrom, low, Ease(m_slashT));
        if (m_slashT >= 1.0f) m_slashPhase = SlashPhase::Idle;
        break;
    }
    }
}

//--- The blade passes straight in front: cut along the crosshair with a plane that contains the line
//    of sight and the swing direction (so the cut runs through the crosshair, along the swing).
void SceneForge::StrikeAtCrosshair()
{
    CameraBase* cam = GetObj<CameraBase>("Camera");
    if (!cam) return;
    const XMFLOAT3 eye = cam->GetPos(), look = cam->GetLook();
    const XMVECTOR F = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&look), XMLoadFloat3(&eye)));
    const XMVECTOR R = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), F));   // left-handed: up x fwd = right
    const XMVECTOR U = XMVector3Cross(F, R);
    XMFLOAT3 fwd; XMStoreFloat3(&fwd, F);
    // Did the swing reach the dummy? The blade sweeps a long arc, not a thin line: the crosshair ray
    // hitting the dummy counts, and so does the dummy being within reach and roughly in front
    // (inside STRIKE_CONE of the view). 2026-10-22 F5: judging by the thin ray alone, a swing that
    // visibly went through the dummy missed when the crosshair was just beside it.
    float t = 0.0f;
    if (!m_cutTarget.RayHit(eye, fwd, t) || t > m_cutReach)
    {
        XMFLOAT3 mn, mx;
        if (!m_cutTarget.WorldBox(mn, mx)) return;
        const XMVECTOR center = XMVectorSet((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f, 0.0f);
        const XMVECTOR toC = XMVectorSubtract(center, XMLoadFloat3(&eye));
        const float along = XMVectorGetX(XMVector3Dot(toC, F));
        const float dist  = XMVectorGetX(XMVector3Length(toC));
        if (along <= 0.0f || dist > m_cutReach || along < dist * cosf(STRIKE_CONE)) return;   // a whiff: nothing in reach
        t = along;   // the point straight ahead at the dummy's depth (it lies on the cut plane)
    }

    const XMVECTOR s = R * m_slashDir.x + U * m_slashDir.y;
    XMFLOAT3 hit, n, sw;
    XMStoreFloat3(&hit, XMLoadFloat3(&eye) + F * t);
    XMStoreFloat3(&n, XMVector3Normalize(XMVector3Cross(F, s)));
    XMStoreFloat3(&sw, s);
    // strength = the move x how well the sword was made (the whole-forge grade)
    const float quality = fmaxf(GradeScore(), CUT_MIN_QUALITY);
    const float focus   = Lerp::Linear(FOCUS_POWER_MIN, 1.0f, m_focusQuality);   // a still release cuts deeper
    const float damage  = m_cutStrikeDamage * quality * focus;
    const SeverTier& tier = TierFor(m_cutHits + 1);   // what a clean cut in two would look like on this stroke
    const CutTarget::StrikeResult r = m_cutTarget.Strike(hit, n, sw, damage, PUSH_SCALE, tier.holdSec);
    if (!r.hit) return;                          // the plane passed beside the dummy: a whiff
    ++m_cutHits;                                 // a stroke that touched the dummy

    if (r.armorHit)   // sparks fly off the armor whether it gives or not
        m_particles.SpawnSparksDir(hit, sw, SPARK_SPREAD, r.cut ? SPARKS_CUT : SPARKS_BOUNCE,
                                   SPARK_SPEED_MIN, SPARK_SPEED_MAX);

    if (r.cut)
    {
        m_slashHitstop = HITSTOP_CUT_SEC;
        m_shake = fmaxf(m_shake, CUT_SHAKE);
        const float kick = KICK_CUT;
        m_cutKick = XMFLOAT2(m_slashDir.x * kick, m_slashDir.y * kick);   // the view follows the blade through
        m_cutFovPunch = fmaxf(m_cutFovPunch, FOV_PUNCH_CUT);
        Haptics::Pulse(RUMBLE_CUT, RUMBLE_CUT_SEC);
        Audio::PlayAt(Audio::SE_COLD, hit, CUT_VOLUME, CUT_PITCH);
        if (r.severed && !m_cutSevered)
        {
            // cut in two: the payoff, by how many strokes it took
            m_cutSevered     = true;
            m_cutSeveredHits = m_cutHits;
            m_cutBannerAt    = m_time;
            m_cutBannerBrush = tier.brush;
            m_cutBannerSlam  = tier.slamFrom;
            m_cutBannerShake = tier.slamShake;
            m_cutBannerLanded = false;
            m_slashHitstop   = fmaxf(m_slashHitstop, tier.hitstopSec);
            m_cutSlowmo      = tier.slowmoSec;
            m_cutSlowmoScale = tier.slowmoScale;
            m_cutFovPunch    = fmaxf(m_cutFovPunch, FOV_PUNCH_SEVER);
        }
    }
    else
    {
        // stopped: the blade bounces back
        const CutTarget::PartDef& part = m_cutTarget.Part(r.stoppedBy);
        const bool metal = (part.kind == CutTarget::PartKind::Armor);
        m_slashHitstop = HITSTOP_BOUNCE_SEC;
        m_shake = fmaxf(m_shake, BOUNCE_SHAKE);
        m_cutKick = XMFLOAT2(-m_slashDir.x * KICK_BOUNCE, -m_slashDir.y * KICK_BOUNCE);  // thrown back against the swing
        Haptics::Pulse(RUMBLE_BOUNCE, RUMBLE_BOUNCE_SEC);
        if (metal) Audio::PlayAt(Audio::SE_ANVIL1, hit, CLANG_VOLUME, CLANG_PITCH);
        else       Audio::PlayAt(Audio::SE_COLD,   hit, THUD_VOLUME,  THUD_PITCH);
        m_trailHead = 0.0f;                          // the streak stops where the blade did
        BeginSlashPhase(SlashPhase::Recoil);
        if (metal && m_focusQuality < STILL_RELEASE) NoteLessonMistake(LessonId::Slash);   // bounced off the armor after a restless release -> remind how to focus
        // The tempering shows on metal (decided 2026-10-20): too hard -> the edge chips, too soft -> it rolls.
        //   Well tempered: nothing to say.
        if (metal)
        {
            const float temper = TemperAvg();
            if (temper < TEMPER_IDEAL_MIN)      Say((const char*)u8"刃が欠けた…硬すぎて脆い", VOICE_COL);
            else if (temper > TEMPER_IDEAL_MAX) Say((const char*)u8"刃がまくれた…柔らかすぎる", VOICE_COL);
        }
    }

    // Missed the armor (only the post / straw): it counts as a stroke, but it does not test the sword.
    //   A fresh dummy is stood up after a moment.
    if (!r.armorHit && !m_cutSevered)
    {
        m_cutRetryAt = m_time + RETRY_DELAY_SEC;
        Say((const char*)u8"胴を狙え", VOICE_COL);
    }
}

//====================================================================
//  camera (feet fixed, free look)
//====================================================================
void SceneForge::UpdateTestCutLook()
{
    float dx, dy;
    if (!ReadMouseDelta(dx, dy)) return;
    float sx, sy; InputActions::LookDelta(sx, sy);   // + right stick (look)
    dx += sx; dy += sy;
    m_cutLookYaw   += dx * m_walkSens;
    m_cutLookPitch -= dy * m_walkSens;
    m_cutLookYaw   = fminf(fmaxf(m_cutLookYaw,   -CUT_LOOK_YAW_LIM),   CUT_LOOK_YAW_LIM);
    m_cutLookPitch = fminf(fmaxf(m_cutLookPitch, -CUT_LOOK_PITCH_LIM), CUT_LOOK_PITCH_LIM);
}

void SceneForge::ApplyTestCutCamera()
{
    CameraBase* cam = GetObj<CameraBase>("Camera");
    if (!cam) return;
    // the view narrows while focusing (the world closes in) and for a moment on impact
    cam->SetFovY(m_camFov * (1.0f - m_cutFovPunch - FOCUS_FOV_NARROW * m_focusFade));
    XMFLOAT3 eye, target; StationView(Station::TestCut, eye, target);
    const float dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
    // + the body leaning into the swing + the kick on impact
    const float lean = m_cutLean * LEAN;
    // + the breath drifting the aim while focusing (the crosshair is where the strike goes)
    const float yaw   = atan2f(dx, dz) + m_cutLookYaw + m_cutKick.x + m_slashDir.x * lean + m_focusAim.x;
    float pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) + m_cutLookPitch + m_cutKick.y + m_slashDir.y * lean + m_focusAim.y;
    pitch = fminf(fmaxf(pitch, -m_walkPitchLim), m_walkPitchLim);
    const float cp = cosf(pitch);
    const XMFLOAT3 f(sinf(yaw) * cp, sinf(pitch), cosf(yaw) * cp);
    // the eye moves into the cut (only forward: the coil does not pull the player back)
    const float lunge = fmaxf(m_cutLean, 0.0f) * LUNGE;
    eye = XMFLOAT3(eye.x + f.x * lunge, eye.y + ShakeOffsetY(), eye.z + f.z * lunge);
    m_camFwd = f;
    cam->SetPos(eye);
    cam->SetLook(XMFLOAT3(eye.x + f.x, eye.y + f.y, eye.z + f.z));
    cam->SetUp(XMFLOAT3(0.0f, 1.0f, 0.0f));
}

//====================================================================
//  drawing
//====================================================================
//--- The weapon trail: a strip along the arc from the tail angle to the head angle, from TRAIL_INNER
//    of the blade out to the tip. Built from the arc formula (not from past frames), so it is smooth
//    even though a swing spans only a few frames. Additive, depth-tested, no depth write.
//    Plus the charge glint at the tip when the heavy slash becomes ready.
//    Search: "weapon trail", "sword trail mesh".
void SceneForge::DrawSwordTrail()
{
    if (!SwordInHand()) return;
    CameraBase* cam = GetObj<CameraBase>("Camera");
    VertexShader* vs = GetObj<VertexShader>("VS_Forge");
    PixelShader*  ps = GetObj<PixelShader>("PS_Trail");
    PixelShader*  glintPS = GetObj<PixelShader>("PS_Glint");
    if (!cam || !vs || !ps) return;

    const int N = TRAIL_SEGMENTS;
    if (!m_trailMesh)
    {
        m_trailVtx.resize(N * 6);
        MeshBuffer::Description d = {};
        d.pVtx = m_trailVtx.data(); d.vtxSize = sizeof(Vertex); d.vtxCount = (UINT)m_trailVtx.size();
        d.isWrite = true; d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        m_trailMesh = std::make_shared<MeshBuffer>(d);
    }
    XMFLOAT4X4 camM[2] = { cam->GetView(), cam->GetProj() };
    vs->WriteBuffer(0, camM);
    SetBlendMode(BLEND_ADD);
    SetDepthTest(DEPTH_ENABLE_TEST);
    const float len = SwordLength();

    const float VISIBLE = 0.01f;
    if (m_trailAlpha >= VISIBLE && m_trailHead > m_trailTail)
    {
        XMFLOAT4 col = TRAIL_COLOR;
        col.w *= m_trailAlpha;
        // inner (near the guard) and outer (tip) edge points at sample i; uv.x = along (0 tail .. 1 head), uv.y = across
        auto edgePoints = [&](int i, XMFLOAT3& inner, XMFLOAT3& outer) {
            const float th = Lerp::Linear(m_trailTail, m_trailHead, (float)i / N);
            const SwordPose p = SwingPose(th, m_trailDir);
            const XMVECTOR g = XMLoadFloat3(&p.guard), d = XMLoadFloat3(&p.dir);
            XMStoreFloat3(&inner, g + d * (len * TRAIL_INNER));
            XMStoreFloat3(&outer, g + d * len);
        };
        int v = 0;
        XMFLOAT3 in0, out0; edgePoints(0, in0, out0);
        for (int i = 1; i <= N; ++i)
        {
            XMFLOAT3 in1, out1; edgePoints(i, in1, out1);
            const float u0 = (float)(i - 1) / N, u1 = (float)i / N;
            m_trailVtx[v++] = { in0,  XMFLOAT2(u0, 0.0f), col };
            m_trailVtx[v++] = { out0, XMFLOAT2(u0, 1.0f), col };
            m_trailVtx[v++] = { in1,  XMFLOAT2(u1, 0.0f), col };
            m_trailVtx[v++] = { in1,  XMFLOAT2(u1, 0.0f), col };
            m_trailVtx[v++] = { out0, XMFLOAT2(u0, 1.0f), col };
            m_trailVtx[v++] = { out1, XMFLOAT2(u1, 1.0f), col };
            in0 = in1; out0 = out1;
        }
        m_trailMesh->Write(m_trailVtx.data());
        vs->Bind();
        ps->Bind();
        m_trailMesh->Draw(v);
    }

    // charge glint: a star at the tip, swelling and fading (PS_Glint draws the star by formula)
    const float g = (m_chargeGlintAt >= 0.0f) ? (m_time - m_chargeGlintAt) / CHARGE_GLINT_SEC : 1.0f;
    if (glintPS && g >= 0.0f && g < 1.0f)
    {
        const float env = sinf(g * XM_PI);
        const XMVECTOR tip = XMLoadFloat3(&m_swordPose.guard) + XMLoadFloat3(&m_swordPose.dir) * len;
        const XMFLOAT3 camPos = cam->GetPos();
        const XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&camPos), tip));
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), toCam));
        const XMVECTOR up = XMVector3Cross(toCam, right);
        const float sz = CHARGE_GLINT_SIZE * env, br = CHARGE_GLINT_BRIGHT * env;
        const XMFLOAT4 col(CHARGE_GLINT_COL.x * br, CHARGE_GLINT_COL.y * br, CHARGE_GLINT_COL.z * br, 1.0f);
        XMFLOAT3 tl, tr, bl, brc;
        XMStoreFloat3(&tl,  tip - right * sz + up * sz);
        XMStoreFloat3(&tr,  tip + right * sz + up * sz);
        XMStoreFloat3(&bl,  tip - right * sz - up * sz);
        XMStoreFloat3(&brc, tip + right * sz - up * sz);
        m_trailVtx[0] = { tl, XMFLOAT2(0, 0), col }; m_trailVtx[1] = { tr, XMFLOAT2(1, 0), col }; m_trailVtx[2] = { bl, XMFLOAT2(0, 1), col };
        m_trailVtx[3] = { bl, XMFLOAT2(0, 1), col }; m_trailVtx[4] = { tr, XMFLOAT2(1, 0), col }; m_trailVtx[5] = { brc, XMFLOAT2(1, 1), col };
        m_trailMesh->Write(m_trailVtx.data());
        vs->Bind();
        glintPS->Bind();
        m_trailMesh->Draw(6);
    }
    SetBlendMode(BLEND_ALPHA);
    SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

//--- Cut in two: the banner slams onto the screen (scale from big down to normal with a little
//    overshoot), shakes the view as it lands, holds, fades. 1 stroke = the 「一刀両断」 brush image,
//    more = "N太刀で両断". Drawn over everything (also during the finale's letterbox).
void SceneForge::DrawSeverBanner()
{
    if (m_cutBannerAt < 0.0f) return;
    const float age = m_time - m_cutBannerAt;
    if (age > BANNER_SLAM_SEC + BANNER_HOLD_SEC + BANNER_FADE_SEC) return;
    const float slam = Clamp01(age / BANNER_SLAM_SEC);
    if (!m_cutBannerLanded && slam >= 1.0f)
    {
        m_cutBannerLanded = true;
        if (m_cutBannerShake > 0.0f)
        {
            m_shake = fmaxf(m_shake, m_cutBannerShake);
            Audio::Play(Audio::SE_ANVIL2, BANNER_SLAM_VOLUME, BANNER_SLAM_PITCH);
        }
    }
    const float scale = Lerp::Linear(m_cutBannerSlam, 1.0f, EaseOutBack(slam, BANNER_SLAM_BACK));
    const float fade  = 1.0f - Clamp01((age - BANNER_SLAM_SEC - BANNER_HOLD_SEC) / BANNER_FADE_SEC);
    const float alpha = fade * Clamp01(slam * 2.0f);   // appears as it slams in
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 c(disp.x * 0.5f, disp.y * BANNER_Y);
    if (m_cutBannerBrush)
    {
        std::shared_ptr<Texture> tex = TextureCache::Get(SEVER_BRUSH);
        if (tex && tex->GetResource())
        {
            const float w = disp.x * BANNER_W * scale;
            const float h = w * (float)tex->GetHeight() / (float)tex->GetWidth();
            dl->AddImage((ImTextureID)tex->GetResource(), ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f),
                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(255 * alpha)));
            return;
        }
    }
    char buf[64];
    sprintf_s(buf, sizeof(buf), (const char*)u8"%d太刀で両断", m_cutSeveredHits);
    const float px = disp.y * BANNER_TEXT_H * scale;
    ImFont* jp = DebugUI::FontJPFor(px);
    const ImVec2 ts = jp->CalcTextSizeA(px, FLT_MAX, 0.0f, buf);
    DrawOutlinedText(dl, jp, px, c.x - ts.x * 0.5f, c.y - ts.y * 0.5f, IM_COL32(255, 240, 215, (int)(255 * alpha)), buf);
}

//--- Focus: the edges of the screen darken (the world falls away) and a small aim ring shows where the
//    strike will land. The ring is as wide as the breath's sway -- it tightens to a point at the stillest
//    moment. Only while focusing (fades in and out with m_focusFade).
void SceneForge::DrawFocusOverlay()
{
    const float VISIBLE = 0.01f;
    if (m_focusFade < VISIBLE || !SwordAtStation()) return;
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // vignette: four gradient bands, dark at the edge -> clear inward
    const ImU32 dark  = IM_COL32(0, 0, 0, (int)(VIGNETTE_ALPHA * m_focusFade));
    const ImU32 clear = IM_COL32(0, 0, 0, 0);
    const float bx = disp.x * VIGNETTE_DEPTH, by = disp.y * VIGNETTE_DEPTH;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(disp.x, by), dark, dark, clear, clear);                       // top
    dl->AddRectFilledMultiColor(ImVec2(0, disp.y - by), ImVec2(disp.x, disp.y), clear, clear, dark, dark);         // bottom
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(bx, disp.y), dark, clear, clear, dark);                       // left
    dl->AddRectFilledMultiColor(ImVec2(disp.x - bx, 0), ImVec2(disp.x, disp.y), clear, dark, dark, clear);         // right
    // aim ring: the strike goes through the screen center (the camera carries the breath's drift)
    if (m_shotMode != ShotMode::Normal) return;   // shot mode (H): keep the vignette (an effect), hide the ring (UI)
    const ImVec2 c(disp.x * 0.5f, disp.y * 0.5f);
    const float r = disp.y * (RETICLE_R_MIN + RETICLE_R_GROW * m_focusSway);
    const int a = (int)(((RETICLE_COL >> IM_COL32_A_SHIFT) & 0xFF) * m_focusFade);
    const ImU32 col = (RETICLE_COL & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
    dl->AddCircle(c, r, col, 0, disp.y * RETICLE_W);
    dl->AddCircleFilled(c, disp.y * RETICLE_W, col);
}

void SceneForge::DrawTestCut()
{
    CameraBase* cam = GetObj<CameraBase>("Camera");
    m_cutTarget.Draw(cam, StationHighlight(StationPropKey(Station::TestCut)));
    if (m_showCutDebug) m_cutTarget.DrawDebug(cam);

    // The hilt: fitted when the blade reached the dummy (m_workAt == TestCut), wherever the blade is.
    // Not on the result screen: the blade is not drawn there either (it would lie across the view).
    if (!cam || !m_wpOk || m_workAt != Station::TestCut || m_carrying || m_state == GAME_RESULT) return;
    if (!m_hiltMetal) BuildHilt();
    // the hilt's axes from the blade's own world matrix: tang end, toward the tip, along the edge
    const int sign = SwordTipSign();
    const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
    const int ta = (m_wpThickAxis >= 0) ? m_wpThickAxis : (la + 1) % 3;
    const int wa = 3 - la - ta;
    const XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
    XMFLOAT3 grip = c;
    (&grip.x)[la] = (sign > 0) ? (&m_wpFinMin.x)[la] : (&m_wpFinMax.x)[la];
    auto axis = [](int a) { return XMVectorSet(a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f, 0.0f); };
    const XMMATRIX W = WeaponWorld();
    const XMVECTOR o = XMVector3TransformCoord(XMLoadFloat3(&grip), W);
    const XMVECTOR d = XMVector3Normalize(XMVector3TransformNormal(XMVectorScale(axis(la), (float)sign), W));
    const XMVECTOR e = Orthonormal(XMVector3TransformNormal(axis(wa), W), d);
    const XMVECTOR t = XMVector3Cross(e, d);
    const XMMATRIX hilt(e, d, t, XMVectorSetW(o, 1.0f));
    std::shared_ptr<Texture> iron = TextureCache::Get(IRON_TEX), wood = TextureCache::Get(WOOD_TEX);
    LitRender::DrawMesh(m_hiltMetal.get(), iron.get(), hilt, cam, PropMaterials::Find(HILT_METAL_KEY));
    LitRender::DrawMesh(m_hiltGrip.get(),  wood.get(), hilt, cam, PropMaterials::Find(HILT_GRIP_KEY));
}

//====================================================================
//  F4 "Place in front of me": move the dummy ahead of the player, facing them
//====================================================================
void SceneForge::PlaceCutTargetInFront()
{
    const XMFLOAT3 foot = m_player.GetPosition();
    const float yaw = m_player.GetYaw();
    m_cutPosXZ[0] = foot.x + sinf(yaw) * PLACE_DISTANCE;
    m_cutPosXZ[1] = foot.z + cosf(yaw) * PLACE_DISTANCE;
    m_cutYaw = yaw + XM_PI;   // facing the player
    ResetTestCut();
}

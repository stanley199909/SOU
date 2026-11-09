#pragma once
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include "MeshSlicer.h"
#include "RigidBody.h"

class CameraBase;
class MeshBuffer;
class Texture;

// A closed box mesh (6 faces, wound for the D3D front face). Used by the dummy's stand-in
// parts and the sword's stand-in hilt.
MeshSlicer::Mesh MakeBoxMesh(const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& halfSize);

// The test-cut target (tameshigiri): a straw dummy made of separate closed parts
// (post, straw hips, torso, head) that the player's slash really cuts. Only a slash through the
// MIDDLE of the torso counts: its power adds up, and when enough has gathered the dummy is cut in
// two (Strike). Until then the blade stops inside a WOUND that really opens (see OpenWound).
//
// Owns the geometry only; WHO swings and HOW (input, animation, weapon quality)
// is decided outside (SceneForge/TestCut.cpp). That keeps the attack system free to
// change later (e.g. a free-moving action mode) without touching this class.
//
// A cut (Cut) = one plane through the target:
//   - every standing part the plane crosses is sliced (MeshSlicer, with capped faces);
//   - the side that still holds the post's foot stays standing ("the stump");
//   - everything on the other side becomes ONE falling rigid body (RigidBody), which
//     lands on the stump's cut face and slides off it (a diagonal cut slides, a level
//     cut rests on top), then drops to the ground.
// Fallen pieces are not cut again (decided 2026-10-20: keeps the cost and the rules simple).
//
// Geometry is kept in the target's LOCAL space, also for fallen pieces (they are drawn
// with "undo the center of mass, then the body's motion"). So the object-space texture
// projection does not jump at the moment of the cut.
class CutTarget
{
public:
    // What a part is there for. 2026-10-29 (user): no armor any more -- a sack-and-straw dummy on a
    // post. The torso (core) is what has to be cut in two.
    enum class PartKind
    {
        Stand,    // the post: only holds the dummy up
        Body,     // straw / sack: what the blade bites into
    };

    // One part of the dummy: a mesh from the model (meshName) or, if the model is missing, a box.
    struct PartDef
    {
        const char*       meshName;     // name of the mesh in the model file (FBX)
        const char*       materialKey;  // PropMaterials key (roughness, metallic, tint, triplanar)
        const char*       texture;      // base color image
        DirectX::XMFLOAT3 center;       // box stand-in: local, metres, y up, foot of the post at the origin
        DirectX::XMFLOAT3 halfSize;
        PartKind          kind;
        bool              core;         // the torso: cutting it in two = the goal
        MeshSlicer::CapUv capUv;        // where the cut face samples the texture (the part's inside)
    };

    // How a slash is aimed (all world space). The cut plane goes through 'point' with 'normal';
    // the blade travels along 'swingDir'; 'eye' + 'viewDir' = the line of sight it was aimed along
    // (the slit on the dummy's surface is found by looking along it).
    struct Aim
    {
        DirectX::XMFLOAT3 point, normal, swingDir, eye, viewDir;
    };

    // What one slash did / would do (Judge / Strike).
    struct StrikeResult
    {
        bool hit       = false;   // the plane crossed at least one standing part
        bool coreHit   = false;   // ... through the middle of the torso (within coreZone): only these slashes count
        bool cut       = false;   // ... and enough has gathered: the dummy was cut in two
        // otherwise the blade stops INSIDE (2026-10-29, user's design)
        bool  stuck    = false;
        float depth    = 0.0f;    // share of the cross-section's AREA it can open, 0..1 (from gathered / needed; offBite off the middle)
        float reach    = 0.0f;    // where that puts the blade: 0..1 of the way along the cut (decided by the mesh's shape)
        // the slit on the surface facing the eye, along the swing (world). Valid when hit.
        DirectX::XMFLOAT3 entry{ 0, 0, 0 };   // where the blade comes in
        DirectX::XMFLOAT3 stop{ 0, 0, 0 };    // where it stops (stuck) / the far side (cut)
        DirectX::XMFLOAT3 through{ 0, 0, 0 }; // the far side
        float entryOffset = 0.0f, stopOffset = 0.0f;   // the same two points as offsets along swingDir from the line of sight (m)
        float depthAlongView = 0.0f;                   // distance from the eye to the surface along the line of sight (m)
    };

    // Tunables (strike strength = F5; the rest = forge_tuning.txt / defaults).
    RigidBody::Params physics;
    float density   = 250.0f;   // kg/m^3 of a fallen chunk's bounding box (straw + wood, rough)
    float pushSpeed = 1.6f;     // m/s the blade gives the cut-off part along the swing (x the slash's push scale)
    float sepSpeed  = 0.4f;     // m/s the cut-off part moves away from the cut face
    // Passing the test (2026-11-01, user): a slash counts when its plane passes within coreZone of the
    // torso's center. The cut only ever happens on such a slash, so it always goes through the middle
    // (never a chip off the side).
    float coreZone = 0.12f;     // m (F5)
    float offBite  = 0.15f;     // how deep the blade goes (0..1) on a slash off the middle
    float woundOpen = 0.35f;    // rad: how far the upper part would tip at full depth (F5; x depth squared)
    // The standing dummy sways on its post (2026-10-29, user: "the dummy should react like a real one"):
    // a spring-damper about the foot of the post, the same model as the forging hammer
    //   I * phi'' = -k * phi - c * phi' + (blade's torque),  phi = the tilt (rad, a rotation vector)
    // integrated with semi-implicit (symplectic) Euler. A strike gives it an angular impulse
    // r x J (r = foot -> where the blade hit). Search: "spring damper", "inverted pendulum".
    struct Sway
    {
        float inertia   = 15.0f;    // kg m^2 about the foot (~15 kg of straw and sack, centre ~1 m up)
        float stiffness = 2400.0f;  // N m / rad: the post's spring (sqrt(k / I) ~ 2 Hz)
        float damping   = 56.0f;    // N m s / rad (~0.15 of critical: it rocks a few times and settles)
        float maxTilt   = 0.25f;    // rad: never leans further than this (a hard knock cannot topple it)
    };
    Sway sway;

    // (Re)build the intact dummy. meshes[i] (local space, same order as defs) replaces part i's box
    // when given and not empty.
    void Build(const PartDef* defs, int count, const std::vector<MeshSlicer::Mesh>* meshes = nullptr);
    void SetPlacement(const DirectX::XMFLOAT3& foot, float yaw);
    DirectX::XMMATRIX World() const;               // local -> world of the standing part (with its sway)
    // The blade pushes the standing dummy: impulse J (N s, world) at a point (world). It sways on its post.
    void Push(const DirectX::XMFLOAT3& impulse, const DirectX::XMFLOAT3& at);

    // Cut with the plane through 'point' (world) with normal 'normal'. 'swingDir' = the
    // direction the blade travels (pushes the cut-off part, x pushScale). The cut-off part
    // hangs in place for holdSec before it falls (the "beat" of a clean cut in sword films).
    // Returns false if the plane misses every standing part (nothing changes).
    bool Cut(const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& swingDir,
             float pushScale = 1.0f, float holdSec = 0.0f);

    // One slash with the player's power. needed = the power that must gather to cut the dummy in two.
    //   gathered += power, only for a slash through the middle of the torso (coreHit).
    //   gathered >= needed -> cut in two: the upper part falls.
    //   Not enough -> the blade stops inside (stuck), deeper the more has gathered, and the wound
    //   opens to that depth. There is one wound: once it exists, a slash that counts goes into it.
    //   Off the middle -> it stops at offBite; nothing gathers, nothing is left behind.
    //   holdSevered: how long the falling half hangs when this slash cuts the dummy in two.
    StrikeResult Strike(const Aim& aim, float power, float needed, float pushScale, float holdSevered);
    // The same judgement without changing anything (the swing asks at the release where it will stop).
    StrikeResult Judge(const Aim& aim, float power, float needed) const;
    float Gathered() const { return m_gathered; }   // the power gathered so far
    bool  HasWound() const { return m_wound.open; }
    Aim   WoundAim() const;                         // the aim of the slash that opened the wound (world)
    // A falling piece touched the ground since the last call (once per piece; the sound of it landing).
    bool  TakeLanded(DirectX::XMFLOAT3& at);
    const PartDef& Part(int i) const { return m_defs[i]; }
    // World bounding box of the standing parts (interaction prompt / range).
    bool WorldBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;

    void Update(float dt);
    void Draw(CameraBase* camera, float highlight = 0.0f);   // highlight 0..1 = the "go here next" glow (LitRender)
    void DrawDebug(CameraBase* camera);            // F4: the stump's cut-face discs (where fallen pieces rest) + the middle of the torso

    // Ray (world) vs the standing parts' bounding box. t = distance along the ray.
    bool RayHit(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir, float& t) const;
    int  DebrisCount() const { return (int)m_debris.size(); }

    // Walking collision: one world-space point set per solid (the standing dummy, then each
    // fallen piece), only the points from footY up to headY (what the player's body can bump).
    // A fallen piece whose top is below stepY is skipped: the player steps over small chunks.
    void CollisionPoints(float footY, float stepY, float headY, std::vector<std::vector<DirectX::XMFLOAT3>>& out) const;

private:
    struct Piece
    {
        MeshSlicer::Mesh            mesh;      // local space of the target
        int                         part = 0;  // index into m_defs (material/texture)
        std::shared_ptr<MeshBuffer> gpu;
    };
    // The wound (2026-11-01, user: not a drawn line, the straw really opens). The first slash that does
    // not get through already slices the plane, but the upper side keeps hanging on the dummy: it tips
    // about the point where the blade stopped, so a wedge opens and the cut faces show inside.
    //   This is a visual approximation of a partial cut: beyond that point the two sides are in fact
    //   apart and merely overlap (a real partial cut would need CSG). From the front it does not show.
    // Local space of the target. The hanging side is the last of m_debris.
    struct Wound
    {
        bool open = false;
        Aim  aim{};                        // the slash that opened it
        DirectX::XMFLOAT3 entry{ 0, 0, 0 }, through{ 0, 0, 0 };   // where the cut starts and would end, on the surface
        DirectX::XMFLOAT3 side{ 0, 0, 0 }; // out of the stump, toward the hanging side
        float target = 0.0f;               // how far along the cut the blade got, 0..1
        float depth  = 0.0f;               // what is shown: follows the target on a spring, so the upper part sags and rocks
        float speed  = 0.0f;
    };
    struct Debris
    {
        std::vector<Piece> pieces;
        RigidBody          body;
        DirectX::XMFLOAT3  com{ 0, 0, 0 };     // center of mass, local space of the target
        std::vector<RigidBody::PlaneCollider> planes;   // ground + the stump's cut face
        float              hold = 0.0f;        // seconds left hanging in place before it falls
        DirectX::XMFLOAT3  impulse{ 0, 0, 0 }; // the blade's push, given when it starts to fall
        DirectX::XMFLOAT3  impulseAt{ 0, 0, 0 };
        bool               landed = false;     // has touched the ground
    };

    static void Upload(Piece& p);
    // Every part still on the dummy: the standing ones and the side hanging in the wound.
    template<class F> void ForIntact(F visit) const;
    // Ray (local) vs those parts' triangles: the nearest hit distance (and the farthest: tFar - t = thickness).
    bool RayMesh(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir, float& t, float* tFar = nullptr) const;
    void OpenWound(const Aim& aim, const StrikeResult& r, float pushScale);
    void ReleaseWound();                           // the hanging side lets go and becomes a falling body
    DirectX::XMMATRIX WoundWorld() const;          // local -> world of the hanging side
    DirectX::XMMATRIX DebrisWorld(const Debris& d) const;   // local -> world of a piece's meshes
    void StandingBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;
    RigidBody::PlaneCollider Ground() const;

    std::vector<PartDef>  m_defs;
    Wound                 m_wound;
    DirectX::XMFLOAT3     m_coreCenter{ 0, 0, 0 };   // center of the intact torso's bounding box (local)
    float                 m_gathered = 0.0f;   // the power of every slash that counted so far
    bool                  m_landedNow = false; // TakeLanded: a piece touched the ground ...
    DirectX::XMFLOAT3     m_landedAt{ 0, 0, 0 };   // ... here
    std::vector<Piece>    m_standing;
    std::vector<Debris>   m_debris;
    DirectX::XMFLOAT3     m_foot{ 0, 0, 0 };
    float                 m_yaw = 0.0f;
    DirectX::XMFLOAT3     m_tilt{ 0, 0, 0 };      // the sway: rotation vector about the foot (rad, world, horizontal)
    DirectX::XMFLOAT3     m_tiltVel{ 0, 0, 0 };   // its angular velocity (rad/s)
};

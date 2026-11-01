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
// (post, straw hips, torso, head) that the player's slash really cuts. A slash that is not strong
// enough stops inside and leaves a notch; later slashes in the SAME notch add up (Strike).
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
        float             durability;   // damage one notch must gather to cut through (see Strike)
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
        bool coreHit   = false;   // ... and the torso was one of them (only these slashes count)
        bool cut       = false;   // ... and went through (the parts were sliced)
        bool coreCut   = false;   // ... and one of them was the torso
        float coreSplit = 0.0f;   // ... the smaller side of the torso / the torso standing before the cut (0..0.5)
        bool severed   = false;   // ... coreSplit >= severMinShare: cut IN TWO (not a chip off the side)
        // not enough strength: the blade stops INSIDE (2026-10-29, user's design)
        bool  stuck    = false;
        float depth    = 0.0f;    // how far in, 0..1 of the way through (the notch's damage / what it needs)
        bool  sameNotch = false;  // the slash landed in an existing notch (its damage adds up)
        // the slit on the surface facing the eye, along the swing (world). Valid when hit.
        DirectX::XMFLOAT3 entry{ 0, 0, 0 };   // where the blade comes in
        DirectX::XMFLOAT3 stop{ 0, 0, 0 };    // where it stops (stuck) / the far side (cut)
        float entryOffset = 0.0f, stopOffset = 0.0f;   // the same two points as offsets along swingDir from the line of sight (m)
        float depthAlongView = 0.0f;                   // distance from the eye to the surface along the line of sight (m)
    };

    // Tunables (strike strength = F5; the rest = forge_tuning.txt / defaults).
    RigidBody::Params physics;
    float density   = 250.0f;   // kg/m^3 of a fallen chunk's bounding box (straw + wood, rough)
    float pushSpeed = 1.6f;     // m/s the blade gives the cut-off part along the swing (x the slash's push scale)
    float sepSpeed  = 0.4f;     // m/s the cut-off part moves away from the cut face
    float severMinShare = 0.30f; // "cut in two" = the smaller side keeps at least this share of the torso
    // "The same notch" (2026-10-29, user: damage adds up only in the same cut -- harder but real):
    //   a slash whose plane passes within notchTolerance of a notch's plane (and turns no more than
    //   notchAngle from it) continues that notch. Any other slash starts a new one from zero.
    float notchTolerance = 0.03f;                                   // m (F5)
    float notchAngle     = DirectX::XMConvertToRadians(10.0f);      // rad
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

    // One slash with the given damage (the swing's strength x the weapon's quality).
    //   The slash lands in a NOTCH: an existing one if its plane is the same (see notchTolerance),
    //   else a new one. The notch gathers the damage; it needs the durability of the toughest part
    //   the plane crosses. Enough -> the plane really cuts (Cut) and every notch is forgotten.
    //   Not enough -> the blade stops inside at depth = gathered / needed (stuck), and the notch's
    //   slit is drawn on the surface (a decal) up to that depth.
    //   holdSevered: how long the falling half hangs when this slash cuts the torso in two.
    StrikeResult Strike(const Aim& aim, float damage, float pushScale, float holdSevered);
    // The same judgement without changing anything (the swing asks at the release where it will stop).
    StrikeResult Judge(const Aim& aim, float damage) const;
    const PartDef& Part(int i) const { return m_defs[i]; }
    // World bounding box of the standing parts (interaction prompt / range).
    bool WorldBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;

    void Update(float dt);
    void Draw(CameraBase* camera, float highlight = 0.0f);   // highlight 0..1 = the "go here next" glow (LitRender)
    void DrawDebug(CameraBase* camera);            // F4: the stump's cut-face discs (where fallen pieces rest) + each notch's "same notch" band

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
    // A notch: where slashes that did not get through have bitten in (local space of the target).
    struct Notch
    {
        DirectX::XMFLOAT3 point, normal;   // its cut plane
        float damage = 0.0f;               // gathered by every slash that landed in it
        std::shared_ptr<MeshBuffer> slit;  // the decal strip along the surface, entry .. current depth
        DirectX::XMFLOAT3 entry{ 0, 0, 0 }, stop{ 0, 0, 0 };   // the slit's two ends on the surface (F4 debug)
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
    };

    static void Upload(Piece& p);
    // The judgement shared by Judge (const) and Strike. notch = index of the matching notch, -1 = a new one.
    StrikeResult Evaluate(const Aim& aim, float damage, int& notch) const;
    // Ray (local) vs the standing parts' triangles: the nearest hit distance.
    bool RayMesh(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir, float& t) const;
    // The slit: surface points seen along the line of sight, from the entry along the swing to 'upTo' (offset, m).
    void BuildSlit(Notch& n, const Aim& aim, float entryOffset, float upTo) const;
    void StandingBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;
    RigidBody::PlaneCollider Ground() const;

    std::vector<PartDef>  m_defs;
    std::vector<Notch>    m_notches;
    float StandingVolume(bool coreOnly) const;
    std::vector<Piece>    m_standing;
    std::vector<Debris>   m_debris;
    DirectX::XMFLOAT3     m_foot{ 0, 0, 0 };
    float                 m_yaw = 0.0f;
    DirectX::XMFLOAT3     m_tilt{ 0, 0, 0 };      // the sway: rotation vector about the foot (rad, world, horizontal)
    DirectX::XMFLOAT3     m_tiltVel{ 0, 0, 0 };   // its angular velocity (rad/s)
};

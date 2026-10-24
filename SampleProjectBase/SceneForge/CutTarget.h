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
// (post, straw body, breastplate, helmet) that the player's slash really cuts.
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
    // What a part is there for (2026-10-21, user's design). A real test-cut dummy is cheap straw
    // on a wooden post; only the armor on it tests the sword.
    enum class PartKind
    {
        Stand,    // the post: only holds the dummy up
        Filler,   // the straw body: only gives the armor its shape
        Armor,    // the armor / helmet: what the test cut is about (a slash that stops here bounces off metal)
    };

    // One part of the dummy: a mesh from the model (meshName) or, if the model is missing, a box.
    struct PartDef
    {
        const char*       meshName;     // name of the mesh in the model file (FBX)
        const char*       materialKey;  // PropMaterials key (roughness, metallic, tint, triplanar)
        const char*       texture;      // base color image
        DirectX::XMFLOAT3 center;       // box stand-in: local, metres, y up, foot of the post at the origin
        DirectX::XMFLOAT3 halfSize;
        float             durability;   // damage a slash must reach to cut through (see Strike)
        PartKind          kind;
        bool              core;         // the torso armor: cutting it in two = the goal
    };

    // What one slash did (Strike).
    struct StrikeResult
    {
        bool hit       = false;   // the plane crossed at least one standing part
        bool armorHit  = false;   // ... and one of them was armor (only these slashes test the sword)
        bool cut       = false;   // ... and went through (the parts were sliced)
        bool coreCut   = false;   // ... and one of them was the torso
        float coreSplit = 0.0f;   // ... the smaller side of the torso / the torso standing before the cut (0..0.5)
        bool severed   = false;   // ... coreSplit >= severMinShare: cut IN TWO (not a chip off the side)
        int  stoppedBy = -1;      // not cut: the part that held (most durability left), else -1
    };

    // Tunables (F1 "Test cut").
    RigidBody::Params physics;
    float density   = 250.0f;   // kg/m^3 of a fallen chunk's bounding box (straw + wood, rough)
    float pushSpeed = 1.6f;     // m/s the blade gives the cut-off part along the swing (x the slash's push scale)
    float sepSpeed  = 0.4f;     // m/s the cut-off part moves away from the cut face
    float severMinShare = 0.30f; // "cut in two" = the smaller side keeps at least this share of the torso

    // (Re)build the intact dummy. meshes[i] (local space, same order as defs) replaces part i's box
    // when given and not empty.
    void Build(const PartDef* defs, int count, const std::vector<MeshSlicer::Mesh>* meshes = nullptr);
    void SetPlacement(const DirectX::XMFLOAT3& foot, float yaw);
    DirectX::XMMATRIX World() const;               // local -> world of the standing part

    // Cut with the plane through 'point' (world) with normal 'normal'. 'swingDir' = the
    // direction the blade travels (pushes the cut-off part, x pushScale). The cut-off part
    // hangs in place for holdSec before it falls (the "beat" of a clean cut in sword films).
    // Returns false if the plane misses every standing part (nothing changes).
    bool Cut(const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& swingDir,
             float pushScale = 1.0f, float holdSec = 0.0f);

    // One slash with the given damage (the swing's strength x the weapon's quality).
    //   Every standing part the plane crosses takes the damage (it adds up over slashes:
    //   the blade bites deeper each time). The slash goes through only if EVERY crossed
    //   part has taken at least its durability -> then the plane really cuts (Cut) and those
    //   parts start fresh. Otherwise it stops at the toughest part (a bounce).
    //   holdSevered: how long the falling half hangs when this slash cuts the torso in two.
    StrikeResult Strike(const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal,
                        const DirectX::XMFLOAT3& swingDir, float damage, float pushScale, float holdSevered);
    const PartDef& Part(int i) const { return m_defs[i]; }
    // World bounding box of the standing parts (interaction prompt / range).
    bool WorldBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;

    void Update(float dt);
    void Draw(CameraBase* camera, float highlight = 0.0f);   // highlight 0..1 = the "go here next" glow (LitRender)
    void DrawDebug(CameraBase* camera);            // F1: the stump's cut-face discs (where fallen pieces rest)

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
    void StandingBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;
    RigidBody::PlaneCollider Ground() const;

    std::vector<PartDef>  m_defs;
    std::vector<float>    m_damage;    // per part: damage taken since it was last cut
    float StandingVolume(bool coreOnly) const;
    std::vector<Piece>    m_standing;
    std::vector<Debris>   m_debris;
    DirectX::XMFLOAT3     m_foot{ 0, 0, 0 };
    float                 m_yaw = 0.0f;
};

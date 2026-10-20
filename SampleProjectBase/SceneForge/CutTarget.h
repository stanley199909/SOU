#pragma once
#include <DirectXMath.h>
#include <vector>
#include <memory>
#include "MeshSlicer.h"
#include "RigidBody.h"

class CameraBase;
class MeshBuffer;
class Texture;

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
    // One part of the dummy. For now a box stand-in (the real model comes later).
    struct PartDef
    {
        const char*       materialKey;  // PropMaterials key (roughness, metallic, tint, triplanar)
        const char*       texture;      // base color image
        DirectX::XMFLOAT3 center;       // local, metres, y up, foot of the post at the origin
        DirectX::XMFLOAT3 halfSize;
    };

    // Tunables (F1 "Test cut").
    RigidBody::Params physics;
    float density   = 250.0f;   // kg/m^3 of a fallen chunk's bounding box (straw + wood, rough)
    float pushSpeed = 1.6f;     // m/s the blade gives the cut-off part along the swing
    float sepSpeed  = 0.4f;     // m/s the cut-off part moves away from the cut face

    void Build(const PartDef* defs, int count);   // (re)build the intact dummy
    void SetPlacement(const DirectX::XMFLOAT3& foot, float yaw);
    DirectX::XMMATRIX World() const;               // local -> world of the standing part

    // Cut with the plane through 'point' (world) with normal 'normal'. 'swingDir' = the
    // direction the blade travels (pushes the cut-off part). Returns false if the plane
    // misses every standing part (nothing changes).
    bool Cut(const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal, const DirectX::XMFLOAT3& swingDir);

    void Update(float dt);
    void Draw(CameraBase* camera);
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
    };

    static void Upload(Piece& p);
    void StandingBox(DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx) const;
    RigidBody::PlaneCollider Ground() const;

    std::vector<PartDef>  m_defs;
    std::vector<Piece>    m_standing;
    std::vector<Debris>   m_debris;
    DirectX::XMFLOAT3     m_foot{ 0, 0, 0 };
    float                 m_yaw = 0.0f;
};

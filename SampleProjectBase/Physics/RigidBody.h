#pragma once
#include <DirectXMath.h>
#include <vector>

// A minimal 3D rigid body (self-built physics) for the pieces cut off the test-cut target.
// Search keywords: "rigid body impulse", "impulse-based collision response", "inertia tensor".
//
// State: position of the center of mass, orientation (quaternion), linear velocity v and
// angular velocity w (world space). Integration = semi-implicit Euler (same as the hammer):
//   v += g * dt;  x += v * dt;  q = q rotated by |w| * dt about w.
// It collides only with planes (the ground, and the stump's cut face it slides off).
//   Contact = a body point that went behind a plane. At that point
//   (offset r from the center of mass) the velocity is v + w x r. If it moves into the
//   ground, an IMPULSE j along the ground normal n stops it (and bounces it by 'restitution'):
//     j = -(1 + e) (v_point . n) / (1/m + n . ((I^-1 (r x n)) x r))
//   The second term in the denominator is why a box landing on a corner starts to tip over:
//   the impulse also changes w. Friction is the same formula along the sliding direction,
//   limited to mu * j (Coulomb friction).
// Inertia tensor = that of the piece's bounding box (good enough for a falling chunk).
// The body falls asleep (stops integrating) once it has been still on the ground for a while.
class RigidBody
{
public:
    struct Params
    {
        float gravity     = 9.8f;   // m/s^2
        float restitution = 0.2f;   // e: 0 = no bounce, 1 = perfect bounce
        float bounceMin   = 0.6f;   // m/s: slower impacts do not bounce (stops endless micro-bouncing)
        float friction    = 0.7f;   // mu (straw/wood on soil)
        float linearDamp  = 0.05f;  // 1/s: air drag on v
        float angularDamp = 0.6f;   // 1/s: drag on w (stops it spinning forever on the ground)
        float sleepSpeed  = 0.08f;  // below this speed (m/s and rad/s) the body counts as still
        float sleepTime   = 0.5f;   // seconds of stillness on the ground before sleeping
        int   contactIters = 10;    // solver passes per frame over all contacts (more = settles more firmly)
        float slop        = 0.005f; // m: overlap allowed with a plane (keeps resting contacts steady)
        float baumgarte   = 0.2f;   // 0..1: share of the overlap beyond the slop removed per frame
    };

    // Something the body cannot pass through: the half-space behind a plane.
    // radius > 0 limits it to a disc around 'point' (the stump's cut face: once the
    // falling half slides past its edge, it drops to the ground). radius <= 0 = infinite (ground).
    struct PlaneCollider
    {
        DirectX::XMFLOAT3 point;
        DirectX::XMFLOAT3 normal;   // points out of the solid side
        float radius;
    };

    // points: the body's vertices relative to its center of mass (body space).
    // halfExtents: half size of the body's bounding box (for the inertia tensor).
    void Init(const std::vector<DirectX::XMFLOAT3>& points, float mass, const DirectX::XMFLOAT3& halfExtents,
              const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT4& orientation);
    void ApplyImpulse(const DirectX::XMFLOAT3& impulse, const DirectX::XMFLOAT3& worldPoint);
    void Update(float dt, const std::vector<PlaneCollider>& planes, const Params& p);
    void Wake() { m_sleeping = false; m_stillTime = 0.0f; }

    DirectX::XMMATRIX World() const;   // body space -> world
    bool Sleeping() const { return m_sleeping; }

private:
    DirectX::XMVECTOR ApplyInvInertia(DirectX::FXMVECTOR worldVec) const;   // I^-1 * x in world space
    float EffectiveMassInv(DirectX::FXMVECTOR r, DirectX::FXMVECTOR dir) const;
    void ApplyImpulseAt(DirectX::FXMVECTOR impulse, DirectX::FXMVECTOR r);   // r = point - center (world)

    std::vector<DirectX::XMFLOAT3> m_points;
    DirectX::XMFLOAT3 m_pos{ 0, 0, 0 };
    DirectX::XMFLOAT4 m_rot{ 0, 0, 0, 1 };
    DirectX::XMFLOAT3 m_vel{ 0, 0, 0 };
    DirectX::XMFLOAT3 m_angVel{ 0, 0, 0 };
    float m_invMass = 1.0f;
    DirectX::XMFLOAT3 m_invInertia{ 1, 1, 1 };   // body space, diagonal (box)
    float m_stillTime = 0.0f;
    bool  m_sleeping  = false;
};

#include "RigidBody.h"
#include <cmath>

using namespace DirectX;

namespace
{
    const float MIN_MASS       = 1e-4f;   // guards 1/m
    const float MIN_INERTIA    = 1e-6f;   // guards 1/I for a flat sliver
    const float BOX_INERTIA_K  = 1.0f / 3.0f;   // box: I_x = m/12 * ((2hy)^2 + (2hz)^2) = m/3 * (hy^2 + hz^2)
    const float MIN_SPIN       = 1e-6f;   // rad/s: below this the orientation is not updated
    const float SAME_POINT     = 1e-4f;   // m: points closer than this are one point
}

void RigidBody::Init(const std::vector<XMFLOAT3>& points, float mass, const XMFLOAT3& h,
                     const XMFLOAT3& position, const XMFLOAT4& orientation)
{
    // Merge points at the same position (a cut mesh repeats each corner once per face):
    // fewer contacts to solve, and no corner counted three times.
    m_points.clear();
    for (const XMFLOAT3& q : points)
    {
        bool dup = false;
        for (const XMFLOAT3& e : m_points)
            if (fabsf(e.x - q.x) < SAME_POINT && fabsf(e.y - q.y) < SAME_POINT && fabsf(e.z - q.z) < SAME_POINT) { dup = true; break; }
        if (!dup) m_points.push_back(q);
    }
    m_pos     = position;
    m_rot     = orientation;
    m_vel     = XMFLOAT3(0, 0, 0);
    m_angVel  = XMFLOAT3(0, 0, 0);
    if (mass < MIN_MASS) mass = MIN_MASS;
    m_invMass = 1.0f / mass;
    const float ix = mass * BOX_INERTIA_K * (h.y * h.y + h.z * h.z);
    const float iy = mass * BOX_INERTIA_K * (h.x * h.x + h.z * h.z);
    const float iz = mass * BOX_INERTIA_K * (h.x * h.x + h.y * h.y);
    m_invInertia = XMFLOAT3(1.0f / fmaxf(ix, MIN_INERTIA), 1.0f / fmaxf(iy, MIN_INERTIA), 1.0f / fmaxf(iz, MIN_INERTIA));
    m_stillTime = 0.0f;
    m_sleeping  = false;
}

XMMATRIX RigidBody::World() const
{
    return XMMatrixRotationQuaternion(XMLoadFloat4(&m_rot)) * XMMatrixTranslation(m_pos.x, m_pos.y, m_pos.z);
}

// The inertia tensor is diagonal in body space. In world space: I^-1 = R * diag * R^T,
// i.e. rotate the vector into body space, scale per axis, rotate back.
XMVECTOR RigidBody::ApplyInvInertia(FXMVECTOR worldVec) const
{
    XMVECTOR q = XMLoadFloat4(&m_rot);
    XMVECTOR local = XMVector3InverseRotate(worldVec, q);
    local = XMVectorMultiply(local, XMLoadFloat3(&m_invInertia));
    return XMVector3Rotate(local, q);
}

void RigidBody::ApplyImpulseAt(FXMVECTOR impulse, FXMVECTOR r)
{
    XMVECTOR v = XMVectorAdd(XMLoadFloat3(&m_vel), XMVectorScale(impulse, m_invMass));
    XMVECTOR w = XMVectorAdd(XMLoadFloat3(&m_angVel), ApplyInvInertia(XMVector3Cross(r, impulse)));
    XMStoreFloat3(&m_vel, v);
    XMStoreFloat3(&m_angVel, w);
}

void RigidBody::ApplyImpulse(const XMFLOAT3& impulse, const XMFLOAT3& worldPoint)
{
    XMVECTOR r = XMVectorSubtract(XMLoadFloat3(&worldPoint), XMLoadFloat3(&m_pos));
    ApplyImpulseAt(XMLoadFloat3(&impulse), r);
    m_sleeping  = false;
    m_stillTime = 0.0f;
}

// Effective mass along direction d at offset r: how much the point's velocity along d changes
// per unit impulse is 1/k, with k = 1/m + d . ((I^-1 (r x d)) x r).
float RigidBody::EffectiveMassInv(FXMVECTOR r, FXMVECTOR d) const
{
    return m_invMass + XMVectorGetX(XMVector3Dot(d, XMVector3Cross(ApplyInvInertia(XMVector3Cross(r, d)), r)));
}

void RigidBody::Update(float dt, const std::vector<PlaneCollider>& planes, const Params& p)
{
    if (m_sleeping || dt <= 0.0f) return;

    // Order (the usual one for rigid bodies with contacts):
    //   1. forces -> velocity (gravity, drag)
    //   2. find contacts at the CURRENT position
    //   3. fix the velocity with contact impulses (so the body cannot move into the planes)
    //   4. move with the fixed velocity (semi-implicit Euler)
    // Moving first and fixing after (the first version) left the body inside the ground every
    // frame; pushing it out completely then made it fall back in = it trembled on the ground.

    // --- 1. forces ---
    m_vel.y -= p.gravity * dt;
    const float linKeep = expf(-p.linearDamp * dt), angKeep = expf(-p.angularDamp * dt);   // frame-rate independent drag
    m_vel    = XMFLOAT3(m_vel.x * linKeep, m_vel.y * linKeep, m_vel.z * linKeep);
    m_angVel = XMFLOAT3(m_angVel.x * angKeep, m_angVel.y * angKeep, m_angVel.z * angKeep);

    // --- 2. contacts: body points behind a plane (ground, the stump's cut face) ---
    struct Contact
    {
        XMVECTOR r, n, t1, t2;      // offset from the center of mass, plane normal, two friction axes
        float    depth;
        float    targetVn;          // separating speed the solver aims for (bounce + depth correction)
        float    accN = 0, accT1 = 0, accT2 = 0;   // impulses accumulated over the passes
    };
    std::vector<Contact> contacts;
    {
        XMMATRIX world = World();
        const XMVECTOR com = XMLoadFloat3(&m_pos);
        for (const PlaneCollider& pc : planes)
        {
            const XMVECTOR n  = XMVector3Normalize(XMLoadFloat3(&pc.normal));
            const XMVECTOR p0 = XMLoadFloat3(&pc.point);
            const XMVECTOR helper = (fabsf(XMVectorGetY(n)) < 0.9f) ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
            const XMVECTOR t1 = XMVector3Normalize(XMVector3Cross(helper, n));
            const XMVECTOR t2 = XMVector3Cross(n, t1);
            for (const XMFLOAT3& lp : m_points)
            {
                XMVECTOR wp = XMVector3TransformCoord(XMLoadFloat3(&lp), world);
                XMVECTOR rel = XMVectorSubtract(wp, p0);
                float depth = -XMVectorGetX(XMVector3Dot(rel, n));
                if (depth <= 0.0f) continue;
                if (pc.radius > 0.0f)                          // finite plane: only inside the disc
                {
                    XMVECTOR inPlane = XMVectorAdd(rel, XMVectorScale(n, depth));
                    if (XMVectorGetX(XMVector3Length(inPlane)) > pc.radius) continue;
                }
                Contact c;
                c.r = XMVectorSubtract(wp, com);
                c.n = n; c.t1 = t1; c.t2 = t2;
                c.depth = depth;
                // Bounce only on a hard landing. Otherwise push out of the plane slowly
                // (Baumgarte stabilization): only the depth beyond the allowed slop, a fraction
                // per frame. A little overlap is allowed on purpose: the contact then stays
                // in place frame after frame instead of switching on and off.
                XMVECTOR vp = XMVectorAdd(XMLoadFloat3(&m_vel), XMVector3Cross(XMLoadFloat3(&m_angVel), c.r));
                const float vn = XMVectorGetX(XMVector3Dot(vp, n));
                const float bounce = (-vn > p.bounceMin) ? -p.restitution * vn : 0.0f;
                const float correct = p.baumgarte * fmaxf(depth - p.slop, 0.0f) / dt;
                c.targetVn = fmaxf(bounce, correct);
                contacts.push_back(c);
            }
        }
    }

    // --- 3. sequential impulses (Erin Catto / Box2D) ---
    // Visit EVERY contact in each pass and nudge its impulse; repeat a few passes so the
    // contacts settle together (a flat face resting on 4 corners). The impulse is clamped
    // as a running total, not per nudge: a later pass may take back part of an earlier one,
    // but the total never pulls the body into the plane (accN >= 0) and friction never
    // exceeds mu * accN (Coulomb). Search: "sequential impulses", "accumulated impulse clamping".
    for (int it = 0; it < p.contactIters; ++it)
        for (Contact& c : contacts)
        {
            XMVECTOR vp = XMVectorAdd(XMLoadFloat3(&m_vel), XMVector3Cross(XMLoadFloat3(&m_angVel), c.r));
            // normal
            const float vn = XMVectorGetX(XMVector3Dot(vp, c.n));
            float dj = (c.targetVn - vn) / EffectiveMassInv(c.r, c.n);
            const float newN = fmaxf(c.accN + dj, 0.0f);
            dj = newN - c.accN;
            c.accN = newN;
            ApplyImpulseAt(XMVectorScale(c.n, dj), c.r);
            // friction along the two plane axes
            const float maxT = p.friction * c.accN;
            vp = XMVectorAdd(XMLoadFloat3(&m_vel), XMVector3Cross(XMLoadFloat3(&m_angVel), c.r));
            float* acc[2] = { &c.accT1, &c.accT2 };
            const XMVECTOR axes[2] = { c.t1, c.t2 };
            for (int k = 0; k < 2; ++k)
            {
                const float vt = XMVectorGetX(XMVector3Dot(vp, axes[k]));
                float djt = -vt / EffectiveMassInv(c.r, axes[k]);
                const float newT = fminf(fmaxf(*acc[k] + djt, -maxT), maxT);
                djt = newT - *acc[k];
                *acc[k] = newT;
                ApplyImpulseAt(XMVectorScale(axes[k], djt), c.r);
            }
        }
    const bool touching = !contacts.empty();

    // --- 4. move with the corrected velocity ---
    m_pos = XMFLOAT3(m_pos.x + m_vel.x * dt, m_pos.y + m_vel.y * dt, m_pos.z + m_vel.z * dt);
    XMVECTOR w = XMLoadFloat3(&m_angVel);
    const float spin = XMVectorGetX(XMVector3Length(w));
    if (spin > MIN_SPIN)
    {
        XMVECTOR dq = XMQuaternionRotationAxis(XMVectorScale(w, 1.0f / spin), spin * dt);
        XMStoreFloat4(&m_rot, XMQuaternionNormalize(XMQuaternionMultiply(XMLoadFloat4(&m_rot), dq)));   // q, then the world-space turn dq
    }

    // --- sleep ---
    const float speed = sqrtf(m_vel.x * m_vel.x + m_vel.y * m_vel.y + m_vel.z * m_vel.z);
    const float turn  = sqrtf(m_angVel.x * m_angVel.x + m_angVel.y * m_angVel.y + m_angVel.z * m_angVel.z);
    if (touching && speed < p.sleepSpeed && turn < p.sleepSpeed) m_stillTime += dt;
    else m_stillTime = 0.0f;
    if (m_stillTime >= p.sleepTime) { m_sleeping = true; m_vel = XMFLOAT3(0, 0, 0); m_angVel = XMFLOAT3(0, 0, 0); }
}

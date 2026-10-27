#include "HammerPhysics.h"
#include <cmath>

namespace
{
    const float MIN_MASS = 0.0001f;  // guard against a zero/negative slider value
    const float MAX_DT   = 0.033f;   // clamp big frame drops so the integration can't diverge
    const float MIN_SPAN = 0.0001f;  // ImpactPower: impact speeds of no charge and full charge are (almost) equal
}

void HammerPhysics::Reset()
{
    m_lift = restLift;
    m_vel  = 0.0f;
    m_swinging = false;
    m_contact  = false;
}

void HammerPhysics::Hold(float charge)
{
    // While charging the head is held by hand: raised with charge, no velocity.
    m_lift = restLift + charge * chargeRaise;
    m_vel  = 0.0f;
    m_swinging = false;
}

void HammerPhysics::Release(float charge)
{
    // The arm throws the head down from where it is held.
    m_vel = -(swingSpeedMin + charge * swingSpeedGain);
    m_swinging = true;
    m_contact  = false;
}

void HammerPhysics::Update(float dt)
{
    if (dt > MAX_DT) dt = MAX_DT;

    if (m_swinging)
    {
        // Phase 2: constant downward acceleration (no spring while the arm drives it).
        m_vel  -= swingAccel * dt;
        m_lift += m_vel * dt;
        if (m_lift <= 0.0f)   // reached the steel
        {
            m_lift     = 0.0f;
            m_impact   = -m_vel;
            m_vel      = 0.0f;   // stopped by the steel; Rebound() gives it its upward speed
            m_swinging = false;
            m_contact  = true;
        }
        return;
    }

    // Phase 3: spring-damper back to rest.
    float tension    = -stiffness * (m_lift - restLift); // restoring force (Hooke's law)
    float resistance = -damping   *  m_vel;              // damping (velocity-proportional)
    float accel      = (mass > MIN_MASS) ? (tension + resistance) / mass : 0.0f;

    m_vel  += accel * dt;  // semi-implicit Euler: update velocity first (stable)
    m_lift += m_vel * dt;  // then move by the new velocity
}

bool HammerPhysics::ConsumeContact(float& impactSpeed)
{
    if (!m_contact) return false;
    m_contact   = false;
    impactSpeed = m_impact;
    return true;
}

void HammerPhysics::Rebound(float restitution)
{
    m_rebound = restitution * m_impact;
    m_vel     = m_rebound;
}

float HammerPhysics::ImpactSpeedFor(float charge) const
{
    // Uniform acceleration over the fall height h: v^2 = v0^2 + 2 a h
    const float v0 = swingSpeedMin + charge * swingSpeedGain;
    const float h  = restLift + charge * chargeRaise;
    return sqrtf(v0 * v0 + 2.0f * swingAccel * h);
}

float HammerPhysics::ImpactPower(float impactSpeed) const
{
    const float lo = ImpactSpeedFor(0.0f), hi = ImpactSpeedFor(1.0f);
    if (hi - lo < MIN_SPAN) return 1.0f;
    const float p = (impactSpeed - lo) / (hi - lo);
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

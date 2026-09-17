#include "HammerPhysics.h"

void HammerPhysics::Reset()
{
    m_lift = restLift;
    m_vel  = 0.0f;
}

void HammerPhysics::Hold(float charge)
{
    // While charging the head is held by hand: raised with charge, no velocity.
    m_lift = restLift + charge * chargeRaise;
    m_vel  = 0.0f;
}

void HammerPhysics::Strike()
{
    // Swing: the head is at the contact point (lift 0) and rebounds upward with
    // launch speed v0 = J/m. From here Update() lets the spring bounce it back.
    m_lift = 0.0f;
    m_vel  = (mass > 0.0001f) ? impulse / mass : 0.0f;
}

void HammerPhysics::Update(float dt)
{
    if (dt > 0.033f) dt = 0.033f; // clamp big frame drops so the integration can't diverge

    float tension    = -stiffness * (m_lift - restLift); // restoring force (Hooke's law)
    float resistance = -damping   *  m_vel;              // damping (velocity-proportional)
    float accel      = (mass > 0.0001f) ? (tension + resistance) / mass : 0.0f;

    m_vel  += accel * dt;  // semi-implicit Euler: update velocity first (stable)
    m_lift += m_vel * dt;  // then move by the new velocity
}

float HammerPhysics::LaunchSpeed() const
{
    return (mass > 0.0001f) ? impulse / mass : 0.0f;
}

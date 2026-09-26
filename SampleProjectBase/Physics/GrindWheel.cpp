#include "GrindWheel.h"
#include <cmath>

void GrindWheel::Reset()
{
    m_speed = 0.0f;
    m_angle = 0.0f;
    m_sincePedal = PEDAL_MIN_INTERVAL;
}

bool GrindWheel::Pedal()
{
    // Rate limit = the leg's recovery time. Mashing faster than this does nothing,
    // so the best play is a steady rhythm, not spamming the button.
    if (m_sincePedal < PEDAL_MIN_INTERVAL) return false;
    m_sincePedal = 0.0f;
    m_speed += pedalImpulse;
    if (m_speed > maxSpeed) m_speed = maxSpeed;
    return true;
}

void GrindWheel::Update(float dt, bool bladePressed)
{
    m_sincePedal += dt;
    // Exact solution of d(omega)/dt = -k*omega over dt. Unlike "omega -= k*omega*dt"
    // it never overshoots below zero and gives the same result at any frame rate.
    const float k = friction + (bladePressed ? bladeDrag : 0.0f);
    m_speed *= expf(-k * dt);
    m_angle += m_speed * dt;
}

float GrindWheel::Speed01() const
{
    return (maxSpeed > 0.0f) ? m_speed / maxSpeed : 0.0f;
}

#include "HingedDoor.h"
#include "Lerp.h"

void HingedDoor::Reset()
{
    m_open = false;
    m_progress = 0.0f;
}

void HingedDoor::Toggle()
{
    m_open = !m_open;
}

void HingedDoor::Update(float dt)
{
    // Constant speed toward the target (MoveTowards really arrives, unlike Damp).
    const float target = m_open ? 1.0f : 0.0f;
    const float rate   = (swingTime > 0.0f) ? dt / swingTime : 1.0f;
    m_progress = Lerp::MoveTowards(m_progress, target, rate);
}

float HingedDoor::Angle() const
{
    // SmoothStep = starts slowly (pushing a heavy door), eases into its stop.
    return openAngle * Lerp::SmoothStep(m_progress);
}

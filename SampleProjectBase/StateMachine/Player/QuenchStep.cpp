#include "QuenchStep.h"
#include "SceneForge/SceneForge.h"
#include "Input.h"

void QuenchStep::OnEntry()
{
    m_phase = Phase::Hold;
    m_timer = 0.0f;
}

void QuenchStep::OnUpdate(float dt)
{
    switch (m_phase)
    {
    case Phase::Hold:
        // Plunge only when standing at the trough. TryQuench checks the temperature
        // (too cold -> refused with a line from the smith) and fires steam + sound.
        if (m_forge->AtStation(Station::Trough) && IsKeyTrigger(VK_LBUTTON) && m_forge->TryQuench())
        {
            m_phase = Phase::Plunge;
            m_timer = 0.0f;
        }
        break;

    case Phase::Plunge:
        m_timer += dt;
        m_forge->SetPlunge(m_timer / PLUNGE_TIME);
        if (m_timer >= PLUNGE_TIME) { m_phase = Phase::Settle; m_timer = 0.0f; }
        break;

    case Phase::Settle:
        m_timer += dt;
        m_forge->SetLetterbox(m_timer / LETTERBOX_TIME);
        if (m_timer >= LETTERBOX_TIME + HOLD_TIME)
            m_forge->AdvanceStep();   // last step -> SceneForge::FinishGame
        break;
    }
}

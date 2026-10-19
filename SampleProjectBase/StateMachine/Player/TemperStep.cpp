#include "TemperStep.h"
#include "SceneForge/SceneForge.h"

void TemperStep::OnUpdate(float dt)
{
    switch (m_phase)
    {
    case Phase::Wait:
        if (!m_forge->TemperTakenOut()) break;
        if (!m_forge->IsLastStep()) { m_forge->AdvanceStep(); break; }
        m_forge->BeginFinale();          // nothing is left to do -> HUD off
        m_phase = Phase::Settle;
        m_timer = 0.0f;
        break;

    case Phase::Settle:                  // same ending as the quench used to have (now shared by the last step)
        m_timer += dt;
        m_forge->SetLetterbox(m_timer / SceneForge::FINALE_LETTERBOX_SEC);
        if (m_timer >= SceneForge::FINALE_LETTERBOX_SEC + SceneForge::FINALE_HOLD_SEC)
            m_forge->AdvanceStep();      // past the last step -> SceneForge::FinishGame
        break;
    }
}

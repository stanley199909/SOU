#include "TestCutStep.h"
#include "SceneForge/SceneForge.h"

void TestCutStep::OnEntry()
{
    m_phase = Phase::Wait;
    m_timer = 0.0f;
    m_forge->FitHilt();   // tempered blade in the tongs -> hilt fitted behind a fade, held in the hand
}

void TestCutStep::OnUpdate(float dt)
{
    switch (m_phase)
    {
    case Phase::Wait:
        if (!m_forge->TestCutSevered()) break;
        m_phase = Phase::Watch;
        m_timer = 0.0f;
        break;

    case Phase::Watch:                   // the halves fall; nothing else happens
        m_timer += dt;
        if (m_timer < WATCH_FALL_SEC) break;
        if (!m_forge->IsLastStep()) { m_forge->AdvanceStep(); break; }
        m_forge->BeginFinale();          // nothing is left to do -> HUD off
        m_phase = Phase::Settle;
        m_timer = 0.0f;
        break;

    case Phase::Settle:                  // same ending as TemperStep (shared by whichever step is last)
        m_timer += dt;
        m_forge->SetLetterbox(m_timer / SceneForge::FINALE_LETTERBOX_SEC);
        if (m_timer >= SceneForge::FINALE_LETTERBOX_SEC + SceneForge::FINALE_HOLD_SEC)
            m_forge->AdvanceStep();      // past the last step -> SceneForge::FinishGame
        break;
    }
}

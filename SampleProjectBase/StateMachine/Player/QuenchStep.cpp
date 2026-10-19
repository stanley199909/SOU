#include "QuenchStep.h"
#include "SceneForge/SceneForge.h"
#include "InputActions.h"

void QuenchStep::OnEntry()
{
    Next(Phase::Hold);
}

void QuenchStep::OnUpdate(float dt)
{
    switch (m_phase)
    {
    case Phase::Hold:
        // Quench only when standing at the trough. TryQuench checks the temperature window
        // (refused with a line from the smith). Sound + steam come later, at water contact.
        if (m_forge->AtStation(Station::Trough) && InputActions::Pressed(Action::Use) && m_forge->TryQuench())
            Next(Phase::Turn);
        break;

    case Phase::Turn:
        m_timer += dt;
        m_forge->SetQuenchTurn(m_timer / TURN_TIME);
        if (m_timer >= TURN_TIME) Next(Phase::Plunge);
        break;

    case Phase::Plunge:
        m_timer += dt;
        m_forge->SetPlunge(m_timer / PLUNGE_TIME);
        if (m_forge->QuenchStirring()) m_forge->StirQuench(dt);   // the player may start moving it as soon as it is wet
        if (m_timer >= PLUNGE_TIME) Next(Phase::Stir);
        break;

    case Phase::Stir:
        m_forge->StirQuench(dt);
        if (m_forge->QuenchProgress() >= 1.0f) Next(Phase::Full);
        break;

    case Phase::Full:
        m_timer += dt;
        if (m_timer >= FULL_SHOW_TIME)
        {
            // Not the last step (tempering follows) -> simply go on. The ending belongs to the last step.
            if (!m_forge->IsLastStep()) { m_forge->EndQuenchPose(); m_forge->AdvanceStep(); break; }
            m_forge->BeginFinale();   // the game is now certain to end and nothing is left to do -> HUD off
            Next(Phase::Settle);
        }
        break;

    case Phase::Settle:
        m_timer += dt;
        m_forge->SetLetterbox(m_timer / SceneForge::FINALE_LETTERBOX_SEC);
        if (m_timer >= SceneForge::FINALE_LETTERBOX_SEC + SceneForge::FINALE_HOLD_SEC)
            m_forge->AdvanceStep();   // last step -> SceneForge::FinishGame
        break;
    }
}

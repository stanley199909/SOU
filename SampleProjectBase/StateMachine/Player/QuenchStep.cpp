#include "QuenchStep.h"
#include "SceneForge/SceneForge.h"
#include "Input.h"

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
        if (m_forge->AtStation(Station::Trough) && IsKeyTrigger(VK_LBUTTON) && m_forge->TryQuench())
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
        if (m_forge->QuenchProgress() >= 1.0f)
        {
            m_forge->BeginFinale();   // the game is now certain to end and nothing is left to do -> HUD off
            Next(Phase::Settle);
        }
        break;

    case Phase::Settle:
        m_timer += dt;
        m_forge->SetLetterbox(m_timer / LETTERBOX_TIME);
        if (m_timer >= LETTERBOX_TIME + HOLD_TIME)
            m_forge->AdvanceStep();   // last step -> SceneForge::FinishGame
        break;
    }
}

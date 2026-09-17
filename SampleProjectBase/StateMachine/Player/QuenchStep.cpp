#include "QuenchStep.h"
#include "SceneForge/SceneForge.h"
#include "Input.h"

void QuenchStep::OnUpdate(float /*dt*/)
{
    // Wait for the quench input. AdvanceStep past the last step finishes.
    if (IsKeyTrigger('Q'))
        m_forge->AdvanceStep();
}

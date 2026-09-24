#include "ForgeStep.h"
#include "SceneForge/SceneForge.h"

void ForgeStep::OnUpdate(float /*dt*/)
{
    // The actual hammering (aim/charge/strike/morph) runs in SceneForge, gated to
    // this step. The player flips the piece at will while forging; the step ends
    // only when BOTH faces are fully shaped, then advances (to quench).
    if (m_forge->BothSidesDone())
        m_forge->AdvanceStep();
}

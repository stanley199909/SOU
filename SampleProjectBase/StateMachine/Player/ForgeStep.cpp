#include "ForgeStep.h"
#include "SceneForge/SceneForge.h"

void ForgeStep::OnUpdate(float /*dt*/)
{
    // The actual hammering (aim/charge/strike/morph) runs in SceneForge, gated
    // to this step. When all segments are shaped, move on (to quench).
    if (m_forge->AllSegmentsDone())
        m_forge->AdvanceStep();
}

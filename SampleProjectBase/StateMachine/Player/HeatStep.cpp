#include "HeatStep.h"
#include "SceneForge/SceneForge.h"

void HeatStep::OnUpdate(float /*dt*/)
{
    // Complete when the steel burns (throws sparks) = hot enough to work.
    // The heating itself (fire + bellows at the hearth) lives in SceneForge;
    // advancing to the next step is decided by the recipe (SceneForge::AdvanceStep).
    if (m_forge->IsBurning())
        m_forge->AdvanceStep();
}

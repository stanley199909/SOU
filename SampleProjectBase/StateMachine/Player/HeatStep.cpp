#include "HeatStep.h"
#include "SceneForge/SceneForge.h"

void HeatStep::OnEntry()
{
    // Nothing to set up: the HUD reads the current step's instruction, and the
    // heating mechanic itself lives in SceneForge (active while playing).
}

void HeatStep::OnUpdate(float /*dt*/)
{
    // Complete when the iron is hot enough to shape. Advancing to the next step
    // is decided by the recipe, not hardcoded here (see SceneForge::AdvanceStep).
    if (m_forge->HeatValue() >= TARGET_TEMP)
        m_forge->AdvanceStep();
}

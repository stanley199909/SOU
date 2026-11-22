#include "HeatStep.h"
#include "SceneForge/SceneForge.h"

void HeatStep::OnUpdate(float /*dt*/)
{
    // Complete when the steel is hot enough to work (bright orange = SceneForge::FORGE_FULL_TEMP).
    // Sparks now mean overheating (burning steel), so they are no longer the "ready" signal.
    // The heating itself (fire + bellows at the hearth) lives in SceneForge;
    // advancing to the next step is decided by the recipe (SceneForge::AdvanceStep).
    if (m_forge->HeatReady())
        m_forge->AdvanceStep();
}

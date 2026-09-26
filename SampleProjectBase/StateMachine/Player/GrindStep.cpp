#include "GrindStep.h"
#include "SceneForge/SceneForge.h"

void GrindStep::OnUpdate(float /*dt*/)
{
    // Finished when the whole edge (every length segment) is ground sharp.
    if (m_forge->AllSharp())
        m_forge->AdvanceStep();
}

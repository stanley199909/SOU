#pragma once
#include "StateBase.h"

class SceneForge; // owner (forward declared; we only store a pointer)

// Heat step: the player puts the iron into the forge fire (hearth station) and
// heats it (the fire alone is slow; holding R pumps the bellows). Completes when
// the steel starts to burn (throws sparks) = ForgingSim::BURN_TEMP, the visible
// "ready" signal a smith reads. The recipe uses this step twice (before forging
// and before quenching); the data decides the order, this class stays the same.
class HeatStep : public StateBase
{
    SceneForge* m_forge;
public:
    explicit HeatStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Heat"; }
    void OnUpdate(float dt) override;
};

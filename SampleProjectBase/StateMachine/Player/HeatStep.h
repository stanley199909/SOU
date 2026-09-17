#pragma once
#include "StateBase.h"

class SceneForge; // owner (forward declared; we only store a pointer)

// Heat step: player holds R to heat the iron in the forge. Completes once the
// temperature reaches TARGET_TEMP, then the machine advances to the next step.
class HeatStep : public StateBase
{
    SceneForge* m_forge;
public:
    explicit HeatStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Heat"; }
    void OnEntry() override;
    void OnUpdate(float dt) override;

    // Temperature the iron must reach before it can be forged. Step-specific,
    // so it lives here as a named constant instead of in the shared recipe.
    static constexpr float TARGET_TEMP = 0.60f;
};

#pragma once
#include "StateBase.h"

class SceneForge; // owner

// Grind step: at the grindstone the player taps RMB to pedal the wheel, holds LMB
// to press the blade on it and slides the blade with the mouse. The grinding
// itself (wheel physics, edge sharpness, sparks) runs in SceneForge while this
// step is active; this state only decides when the step is finished.
class GrindStep : public StateBase
{
    SceneForge* m_forge;
public:
    explicit GrindStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Grind"; }
    void OnUpdate(float dt) override;
};

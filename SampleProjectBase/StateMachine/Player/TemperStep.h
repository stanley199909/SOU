#pragma once
#include "StateBase.h"

class SceneForge; // owner (forward declared; we only store a pointer)

// Temper step (search: "tempering steel colors", JP: yakimodoshi).
// After quenching the steel is hard but brittle. The player lays the blade ABOVE the
// forge coals (hearth station) and warms it gently: the surface oxide film runs
// silver -> straw -> bronze -> purple -> blue, flowing from the spine to the edges.
// The player judges the color at the EDGE and takes the blade out with the tongs (Q)
// to stop. SceneForge owns the color physics (how fast it runs, bellows) and the look;
// this class only decides WHEN the step is over, and plays the ending if it is the last
// step of the recipe (the data decides the order).
//   Wait   : until the blade has been taken out after it started to change color.
//   Settle : last step only -> letterbox bars slide in, hold the shot, then advance.
class TemperStep : public StateBase
{
    enum class Phase { Wait, Settle };

    SceneForge* m_forge;
    Phase m_phase = Phase::Wait;
    float m_timer = 0.0f;

public:
    explicit TemperStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Temper"; }
    void OnEntry() override { m_phase = Phase::Wait; m_timer = 0.0f; }
    void OnUpdate(float dt) override;
};

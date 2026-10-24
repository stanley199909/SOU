#pragma once
#include "StateBase.h"

class SceneForge; // owner (forward declared; we only store a pointer)

// Test cut step (search: "tameshigiri", JP: tameshigiri / shi-giri).
// The finished blade gets a hilt and the player cuts an armored straw dummy outside.
// The forged quality (the whole-forge grade) is the sword's cutting power: a good
// sword cuts the torso in two in one slash, a poor one needs several.
// SceneForge owns the slashing (input, swing, damage, the dummy); this class only
// decides WHEN the step is over, like TemperStep:
//   Wait   : until the torso has been cut in two.
//   Watch  : let the halves fall for a moment (the payoff shot).
//   Settle : last step only -> letterbox bars slide in, hold the shot, then advance.
class TestCutStep : public StateBase
{
    enum class Phase { Wait, Watch, Settle };

    SceneForge* m_forge;
    Phase m_phase = Phase::Wait;
    float m_timer = 0.0f;

public:
    // Seconds the cut-off half is watched falling before the step moves on.
    static constexpr float WATCH_FALL_SEC = 2.6f;   // long enough for the "cut in two" banner slam + the slow-motion beat

    explicit TestCutStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "TestCut"; }
    void OnEntry() override;            // fits the hilt (SceneForge::FitHilt): from here the sword is in the hand
    void OnUpdate(float dt) override;
};

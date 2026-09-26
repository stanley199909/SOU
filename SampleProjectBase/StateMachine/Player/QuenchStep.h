#pragma once
#include "StateBase.h"

class SceneForge; // owner

// Quench step (the last one = the climax). A small sequence of its own:
//   Hold    : at the trough the burning blade is held over the water. LMB = plunge.
//             If the blade has cooled too much, the owner refuses (the smith says so)
//             and the player must re-heat it at the hearth.
//   Plunge  : the blade goes into the water -> steam burst + hiss (owner reacts).
//   Settle  : the steam keeps rising while letterbox bars slide in (cinematic end).
// Then the step advances; being the last step, that finishes the game.
// This class owns the ORDER and TIMING of the sequence; SceneForge owns what it
// looks/sounds like (blade depth, steam, bars) and is told the progress 0..1.
class QuenchStep : public StateBase
{
    enum class Phase { Hold, Plunge, Settle };

    SceneForge* m_forge;
    Phase m_phase = Phase::Hold;
    float m_timer = 0.0f;

public:
    explicit QuenchStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Quench"; }
    void OnEntry() override;
    void OnUpdate(float dt) override;

    static constexpr float PLUNGE_TIME    = 0.6f;  // blade travels into the water (s)
    static constexpr float LETTERBOX_TIME = 1.8f;  // bars slide in (s)
    static constexpr float HOLD_TIME      = 1.2f;  // stay on the finished shot before the result (s)
};

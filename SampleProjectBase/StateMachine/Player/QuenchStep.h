#pragma once
#include "StateBase.h"

class SceneForge; // owner

// Quench step (the last one = the climax). A small sequence of its own:
//   Hold    : at the trough the burning blade is held over the water with the tongs. LMB = quench.
//             If the blade is too cold / too hot, the owner refuses (the smith says so).
//   Turn    : the blade is turned edge-down (rolled 90 deg about its long axis) and lifted a
//             little first (anticipation).
//   Plunge  : the blade cuts into the water edge-first -> steam burst + hiss at the moment the
//             edge touches the surface (owner detects the contact).
//   Stir    : the PLAYER moves the blade up and down (mouse Y). Real reason: right after
//             entering, steel is wrapped in a vapor blanket (film boiling, Leidenfrost effect)
//             that insulates it; moving it breaks the film so it reaches nucleate boiling (the
//             fastest cooling) sooner and cools evenly. The step has a progress like the others
//             (how far the blade has cooled); at 100% the game is certain to end -> the owner
//             hides the HUD (BeginFinale). Not stirring never blocks: the film still breaks once
//             the steel cools enough, only slower (and the grade is a little lower).
//   Settle  : letterbox bars slide in while the steam and waves calm down, then hold the shot.
// Then the step advances; being the last step, that finishes the game.
// This class owns the ORDER and TIMING of the sequence; SceneForge owns what it
// looks/sounds like (blade angle, depth, stroke, steam, waves, bars) and the boiling physics.
class QuenchStep : public StateBase
{
    enum class Phase { Hold, Turn, Plunge, Stir, Settle };

    SceneForge* m_forge;
    Phase m_phase = Phase::Hold;
    float m_timer = 0.0f;

    void Next(Phase p) { m_phase = p; m_timer = 0.0f; }

public:
    explicit QuenchStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Quench"; }
    void OnEntry() override;
    void OnUpdate(float dt) override;

    static constexpr float TURN_TIME      = 0.5f;  // blade turns edge-down (s)
    static constexpr float PLUNGE_TIME    = 1.0f;  // blade travels into the water (s)
    static constexpr float LETTERBOX_TIME = 1.8f;  // bars slide in (s)
    static constexpr float HOLD_TIME      = 2.5f;  // stay on the finished shot while it calms, before the result (s)
};

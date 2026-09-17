#pragma once
#include "StateBase.h"

class SceneForge; // owner

// Quench step: player presses Q to plunge the blade into water and finish.
// It is the last step, so advancing past it ends the game (SceneForge::FinishGame).
class QuenchStep : public StateBase
{
    SceneForge* m_forge;
public:
    explicit QuenchStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Quench"; }
    void OnUpdate(float dt) override;
};

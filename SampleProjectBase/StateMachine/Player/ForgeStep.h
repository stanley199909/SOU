#pragma once
#include "StateBase.h"

class SceneForge; // owner

// Forge (hammer) step: this is the game's only pre-existing action. While it is
// active SceneForge lets the player charge and strike (shaping m_segProg).
// Completes once every segment has reached its finished shape.
class ForgeStep : public StateBase
{
    SceneForge* m_forge;
public:
    explicit ForgeStep(SceneForge* forge) : m_forge(forge) {}
    std::string GetStateName() const override { return "Forge"; }
    void OnUpdate(float dt) override;
};

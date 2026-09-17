#include "StateBase.h"

void StateBase::RegisterState(StateMachine& machine)
{
    machine.AddState(
        GetStateName(),
        [this]() { this->OnEntry(); },
        [this](float dt) { this->OnUpdate(dt); },
        [this]() { this->OnExit(); }
    );
}

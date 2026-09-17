#pragma once
#include "StateMachine.h"

// Base class for a state. Each concrete state (one forging step here) derives
// from this and implements the lifecycle hooks. RegisterState wires the three
// hooks into a StateMachine as lambdas capturing 'this'.
// (Reused from the statemachinechase project.)
class StateBase
{
public:
    virtual ~StateBase() = default;

    virtual std::string GetStateName() const = 0; // state name (= machine key)

    virtual void OnEntry() {}          // entering the state
    virtual void OnUpdate(float dt) {} // per-frame while active
    virtual void OnExit() {}           // leaving the state

    // Register this state's hooks into the machine under GetStateName().
    void RegisterState(StateMachine& machine);
};

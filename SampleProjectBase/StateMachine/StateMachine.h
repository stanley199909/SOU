#pragma once
#include <string>
#include <iostream>
#include <functional>
#include <map>

// One state: its Enter/Update/Exit callbacks and name.
// (Reused verbatim from the statemachinechase project's FSM framework.)
struct State
{
    std::function<void()> onEnter;          // called when entering the state
    std::function<void(float)> onUpdate;    // called every frame while active
    std::function<void()> onExit;           // called when leaving the state
    std::string name;                       // state name (used as the map key)
};

// String-keyed finite state machine. States are registered by name and
// switched by name. It does NOT decide when to transition; the active state
// (or its owner) requests ChangeState.
class StateMachine
{
protected:
    std::map<std::string, State> stateList; // all registered states
    State* currentState = nullptr;          // the currently active state

public:
    StateMachine() = default;
    virtual ~StateMachine() = default;

    // Register a state under a name.
    void AddState(
        const std::string& name,
        std::function<void()> enterFunc,
        std::function<void(float)> updateFunc,
        std::function<void()> exitFunc);

    // Switch to the named state (Exit old -> Enter new).
    void ChangeState(const std::string& name);

    // Tick the active state.
    void Update(float dt);

    // Name of the active state ("None" if there is none).
    std::string GetCurrentStateName() const;
};

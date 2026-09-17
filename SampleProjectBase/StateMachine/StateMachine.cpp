#include "StateMachine.h"

void StateMachine::AddState(const std::string& name,
    std::function<void()> enterFunc,
    std::function<void(float)> updateFunc,
    std::function<void()> exitFunc)
{
    State s;
    s.name = name;
    s.onEnter = enterFunc;
    s.onUpdate = updateFunc;
    s.onExit = exitFunc;
    stateList[name] = s;
}

void StateMachine::ChangeState(const std::string& name)
{
    if (currentState && currentState->onExit)
        this->currentState->onExit();
    const auto& it = stateList.find(name);
    if (it != stateList.end())
    {
        currentState = &it->second;
        if (currentState->onEnter)
            this->currentState->onEnter();
    }
    else
    {
        std::cerr << "State not found: " << name << std::endl;
        currentState = nullptr;
    }
}

void StateMachine::Update(float dt)
{
    if (currentState && currentState->onUpdate)
    {
        this->currentState->onUpdate(dt);
    }
}

std::string StateMachine::GetCurrentStateName() const
{
    return currentState ? currentState->name : "None";
}

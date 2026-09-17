#pragma once
#include <vector>

// ---------------------------------------------------------------------------
// Data-driven forging content. This file is DATA, not machinery: it describes
// which steps a weapon is made of and in what order. Adding a new weapon means
// adding a WeaponRecipe here -- no state-machine code changes.
// The StateMachine framework does not know about this file; only SceneForge
// (the owner) reads a recipe and drives the step state machine from it.
// ---------------------------------------------------------------------------

// The kinds of forging step that exist. Each value doubles as the string key
// used to switch the step state machine (see StepKey()).
enum class StepName
{
    Heat,     // heat the iron in the forge
    Forge,    // hammer it into shape while hot
    Quench,   // cool it in water to finish
    Move,     // (future) reposition the workpiece
    Grind,    // (future) sharpen on the grindstone
};

// One entry in a recipe = one step. Holds only fields common to every step.
// Step-specific numbers (e.g. Heat's target temperature) live inside that
// step's own class as named constants, so this shared struct stays lean.
struct StepSetting
{
    StepName    type;        // which kind of step
    const char* instruction; // macro tutorial line shown on the HUD this step
};

// A weapon = an ordered list of steps. Pure data.
struct WeaponRecipe
{
    const char*              name;
    std::vector<StepSetting> steps;
};

// Map a StepName to the string key the step machine registers under.
// Kept here (owner-side vocabulary); each step class returns the same literal
// from GetStateName(), which keeps StateMachine independent of this file.
const char* StepKey(StepName n);

// The first recipe. More weapons = more of these.
extern const WeaponRecipe ShortSword;

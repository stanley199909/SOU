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
    Heat,     // heat the iron in the forge fire until it burns (sparks)
    Forge,    // hammer it into shape while hot (the player may flip the piece at will here)
    Grind,    // sharpen the edge on the grindstone (rough grind, before hardening)
    Quench,   // plunge the burning blade into water to harden it
    Temper,   // warm the hardened blade gently above the coals; stop when the edge shows the right oxide color
};

// Work places in the smithy. The player walks to one and presses E to work there;
// the blade is carried along and put down at that place.
enum class Station
{
    Anvil,       // hammering
    Hearth,      // the forge fire (heating). Always usable: re-heat whenever the iron cools
    Grindstone,  // pedal-driven sharpening wheel
    Trough,      // water trough (quenching)
};

// One entry in a recipe = one step. Holds only fields common to every step.
// Step-specific numbers (e.g. Quench's minimum temperature) live inside that
// step's own class as named constants, so this shared struct stays lean.
struct StepSetting
{
    StepName    type;        // which kind of step
    const char* label;       // short step name for the HUD step tracker (UTF-8)
    const char* instruction; // macro tutorial line shown on the HUD this step (UTF-8)
};

// One tutorial course (lesson) = a slice of the recipe's steps, played on its own.
// The player picks it from the course list; the game starts at firstStep (earlier
// steps are treated as already done) and the course is complete once lastStep is
// finished. The slice is data, so a new weapon brings its own courses.
struct TutorialCourse
{
    const char* label;     // name in the course list (UTF-8)
    const char* desc;      // one line shown under the list while it is selected (UTF-8)
    int         firstStep; // index into steps: where the course starts
    int         lastStep;  // index into steps: the course ends when this step is done
};

// A weapon = an ordered list of steps. Pure data.
// The same step type may appear more than once (e.g. Heat before forging AND
// before quenching): one state class, reused by the data.
struct WeaponRecipe
{
    const char*                 name;
    std::vector<StepSetting>    steps;
    std::vector<TutorialCourse> courses;   // tutorial mode: the lesson list for this weapon
};

// Map a StepName to the string key the step machine registers under.
// Kept here (owner-side vocabulary); each step class returns the same literal
// from GetStateName(), which keeps StateMachine independent of this file.
const char* StepKey(StepName n);

// Where a step is performed. Fixed by the kind of work (you always heat at the
// hearth), so it is a property of the step type, not per-recipe data.
Station StepStation(StepName n);

// The first recipe. More weapons = more of these.
extern const WeaponRecipe ShortSword;

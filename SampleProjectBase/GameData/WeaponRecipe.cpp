#include "WeaponRecipe.h"

const char* StepKey(StepName n)
{
    switch (n)
    {
    case StepName::Heat:   return "Heat";
    case StepName::Forge:  return "Forge";
    case StepName::Grind:  return "Grind";
    case StepName::Quench: return "Quench";
    }
    return "None";
}

Station StepStation(StepName n)
{
    switch (n)
    {
    case StepName::Heat:   return Station::Hearth;
    case StepName::Forge:  return Station::Anvil;
    case StepName::Grind:  return Station::Grindstone;
    case StepName::Quench: return Station::Trough;
    }
    return Station::Anvil;
}

// Short sword, following the real order of work:
//   heat -> forge (both faces) -> rough grind (steel is still soft) -> heat again -> quench.
// Grinding comes BEFORE quenching: hardened steel is hard to grind, and quenching is
// the dramatic "moment of truth" that ends the game. Flipping is NOT a recipe step:
// the player turns the piece over at will during the Forge step (press F).
// Instructions are English placeholders until the tutorial HUD is rebuilt.
const WeaponRecipe ShortSword = {
    "Short Sword",
    {
        { StepName::Heat,   "Put the iron in the forge fire (E) and heat it until it burns" },
        { StepName::Forge,  "Take it to the anvil and strike while it glows. Press F to flip." },
        { StepName::Grind,  "Sharpen the edge on the grindstone" },
        { StepName::Heat,   "Heat the blade once more until it burns" },
        { StepName::Quench, "Plunge the burning blade into the water trough" },
    }
};

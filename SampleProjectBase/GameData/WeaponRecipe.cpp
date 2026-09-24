#include "WeaponRecipe.h"

const char* StepKey(StepName n)
{
    switch (n)
    {
    case StepName::Heat:   return "Heat";
    case StepName::Forge:  return "Forge";
    case StepName::Quench: return "Quench";
    case StepName::Grind:  return "Grind";
    }
    return "None";
}

// Short sword: heat -> forge -> quench. Flipping is NOT a recipe step: the player
// turns the piece over at will during the Forge step (press F), and Forge finishes
// only when BOTH faces are shaped. Instructions are English placeholders until the
// tutorial HUD is rebuilt.
const WeaponRecipe ShortSword = {
    "Short Sword",
    {
        { StepName::Heat,   "Heat the iron in the forge (hold R)" },
        { StepName::Forge,  "Strike while it glows (hold LMB). Press F to flip." },
        { StepName::Quench, "Quench in water to finish (press Q)" },
    }
};

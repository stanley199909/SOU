#include "WeaponRecipe.h"

const char* StepKey(StepName n)
{
    switch (n)
    {
    case StepName::Heat:   return "Heat";
    case StepName::Forge:  return "Forge";
    case StepName::Quench: return "Quench";
    case StepName::Move:   return "Move";
    case StepName::Grind:  return "Grind";
    }
    return "None";
}

// Short sword: heat -> forge -> quench. Instructions are English placeholders
// for now; they move to Japanese when the tutorial HUD is rebuilt.
const WeaponRecipe ShortSword = {
    "Short Sword",
    {
        { StepName::Heat,   "Heat the iron in the forge (hold R)" },
        { StepName::Forge,  "Strike while it glows (hold LMB, release)" },
        { StepName::Quench, "Quench in water to finish (press Q)" },
    }
};

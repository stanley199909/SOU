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
// 文言は日本語(展示/就職先が日本)。このファイルは BOM 付き UTF-8 = u8 リテラルが正しく読まれる。
// label = HUD 左の工程リストに出す短い名前 / instruction = 画面上部の「今やること」。
const WeaponRecipe ShortSword = {
    "Short Sword",
    {
        { StepName::Heat,   (const char*)u8"加熱",   (const char*)u8"鉄を炉に入れ、火花が散るまで熱する" },
        { StepName::Forge,  (const char*)u8"鍛造",   (const char*)u8"金床で、赤いうちに叩いて形を作る（F で裏返す）" },
        { StepName::Grind,  (const char*)u8"研ぎ",   (const char*)u8"砥石で刃を研ぐ" },
        { StepName::Heat,   (const char*)u8"再加熱", (const char*)u8"もう一度、火花が散るまで熱する" },
        { StepName::Quench, (const char*)u8"焼入れ", (const char*)u8"燃える刃を水槽に沈める" },
    }
};

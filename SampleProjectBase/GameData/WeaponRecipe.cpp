#include "WeaponRecipe.h"

const char* StepKey(StepName n)
{
    switch (n)
    {
    case StepName::Heat:   return "Heat";
    case StepName::Forge:  return "Forge";
    case StepName::Grind:  return "Grind";
    case StepName::Quench: return "Quench";
    case StepName::Temper: return "Temper";
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
    case StepName::Temper: return Station::Hearth;   // tempering is done above the forge coals
    }
    return Station::Anvil;
}

// Short sword, following the real order of work:
//   heat -> forge (both faces) -> rough grind (steel is still soft) -> heat again -> quench -> temper.
// Grinding comes BEFORE quenching: hardened steel is hard to grind. Quenching leaves the steel hard
// but brittle, so it is tempered (gently reheated, judged by color). Flipping is NOT a recipe step:
// the player turns the piece over at will during the Forge step (hold F).
// 文言は日本語(展示/就職先が日本)。このファイルは BOM 付き UTF-8 = u8 リテラルが正しく読まれる。
// label = HUD 左の工程リストに出す短い名前 / instruction = 画面上部の「今やること」。
const WeaponRecipe ShortSword = {
    "Short Sword",
    {
        { StepName::Heat,   (const char*)u8"加熱",   (const char*)u8"鉄を炉に入れ、火花が散るまで熱する" },
        { StepName::Forge,  (const char*)u8"鍛造",   (const char*)u8"金床で、赤いうちに叩いて形を作る（{Flip}長押しで裏返す）" },
        { StepName::Grind,  (const char*)u8"研ぎ",   (const char*)u8"砥石で刃を研ぐ" },
        { StepName::Heat,   (const char*)u8"再加熱", (const char*)u8"もう一度、火花が散るまで熱する" },
        { StepName::Quench, (const char*)u8"焼入れ", (const char*)u8"燃える刃を水槽に沈める" },
        // 焼き戻し(2026-10-19 追加): 焼入れした刃は硬いが脆い → 炉の上で軽く温め、刃先の色で止め時を見る(中世の鍛冶の方法)。
        { StepName::Temper, (const char*)u8"焼き戻し", (const char*)u8"刃を炭の上で動かして温め、刃先全体が麦わら〜茶色になったら{Grab}で取り出す" },
    },
    // チュートリアルの課程(工程の区切り)。番号は上の steps の並び(0=加熱 1=鍛造 2=研ぎ 3=再加熱 4=焼入れ 5=焼き戻し)。
    //   鍛造   = 熱い鉄を金床に置いた所から(最初の加熱は済み)。途中で冷えたら炉へ戻る事も覚える
    //   研ぎ   = 鍛え終わった刃を砥石に置いた所から
    //   焼入れ = 研ぎ終わった刃を炉に置いた所から(火花が散るまで熱して → 水槽へ)
    {
        { (const char*)u8"鍛造",   (const char*)u8"金床で叩いて形を作る。冷えたら炉で熱し直し、裏返して両面を仕上げる", 1, 1 },
        { (const char*)u8"研ぎ",   (const char*)u8"足踏みの砥石で、刃の角度を合わせて両側を研ぐ",                       2, 2 },
        { (const char*)u8"焼入れ", (const char*)u8"刃を火花が散るまで熱し、水槽に沈めて揺する",                         3, 4 },
        { (const char*)u8"焼き戻し", (const char*)u8"焼入れした刃を炉の上で温め、刃先の色を見て取り出す",               5, 5 },
    }
};

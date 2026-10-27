#include "PropMaterials.h"

namespace
{
	// 素材パック(Unreal 用)の法線 / ORM。Tools/pbr_prescale.py で 1024 に縮めた _1k 版を使う(原寸は計 268MB = git にも内蔵 GPU にも重い)。
	//   ORM = R: AO(環境光の遮蔽)/ G: 粗さ / B: 金属度。画像がある物は粗さ・金属度を画素ごとに読む
	//     = 鎚の頭(鉄)と柄(木)、金床と台が1枚の画像でも、場所ごとに正しく分かれる。下の定数は画像が無い時の代わり。
	//   法線は Unreal 用 = DirectX 式(緑が下向き)。編集シーンの「Prop materials」で逆にして見比べられる。
	#define PACK "Assets/MM_Blacksmith_Pack/"
	#define ANVIL_N  PACK "Anvil/Textures/T_Anvil_Normal_1k.png"
	#define ANVIL_O  PACK "Anvil/Textures/T_Anvil_ORM_1k.png"
	#define WOOD_N   PACK "Ballows/Textures/T_Wood_Normal_1k.png"
	#define WOOD_O   PACK "Ballows/Textures/T_Wood_ORM_1k.png"
	#define BUCKET_N PACK "Buckets/Textures/T_Buckets_V1_Normal_1k.png"
	#define BUCKET_O PACK "Buckets/Textures/T_Buckets_V1_ORM_1k.png"
	#define METAL_N  PACK "Metal Parts/Textures/T_Metal_parts_Normal_1k.png"
	#define METAL_O  PACK "Metal Parts/Textures/T_Metal_parts_ORM_1k.png"
	#define SHARP_N  PACK "Sharpner/Textures/T_Sharpner_V1_Normal_1k.png"
	#define SHARP_O  PACK "Sharpner/Textures/T_Sharpner_V1_ORM_1k.png"
	#define TOOLS_N  PACK "Tools/Textures/1024x512/T_BS_Tools_Normal_1k.png"
	#define TOOLS_O  PACK "Tools/Textures/1024x512/T_BS_Tools_ORM_1k.png"
	#define TABLE_N  PACK "Worktable/Textures/T_BS_Worktable_V1_Normal_1k.png"
	#define TABLE_O  PACK "Worktable/Textures/T_BS_Worktable_V1_ORM_1k.png"
	const PropMaterial TABLE[] =
	{
		//  key            粗さ   金属  三平面  tint                    法線      ORM       DX
		{ "StGround",     0.80f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, nullptr,  nullptr,  false },	// 床板(写真の貼图。法線/ORM 無し)
		{ "StStump",      0.85f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, ANVIL_N,  ANVIL_O,  true  },	// 金床の台(木)。金床と同じ画像に入っている
		{ "StAnvil",      0.45f, 1.0f, false, { 1.00f, 1.00f, 1.00f }, ANVIL_N,  ANVIL_O,  true  },	// 金床(鋼)
		{ "StForge",      0.90f, 0.0f, true,  { 0.80f, 0.76f, 0.72f }, nullptr,  nullptr,  false },	// 炉(石)。三平面投影なので UV 用の法線/ORM は使わない。煤けて見える暗い暖色
		{ "StStand",      0.80f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, WOOD_N,   WOOD_O,   true  },	// ふいごの台(木)
		{ "StBellows",    0.75f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, WOOD_N,   WOOD_O,   true  },	// ふいご(木と革)
		{ "StWorktable",  0.80f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, TABLE_N,  TABLE_O,  true  },	// 作業台(木)
		{ "StTrough",     0.85f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, BUCKET_N, BUCKET_O, true  },	// 水槽(木)
		{ "StBucket",     0.80f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, BUCKET_N, BUCKET_O, true  },	// 水桶(木)
		{ "StGrind",      0.90f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, SHARP_N,  SHARP_O,  true  },	// 砥石(石と木)。回る輪も同じ材質
		{ "StPoker",      0.50f, 1.0f, false, { 1.00f, 1.00f, 1.00f }, TOOLS_N,  TOOLS_O,  true  },	// 火かき棒(鉄)
		{ "StPliers",     0.50f, 1.0f, false, { 0.40f, 0.38f, 0.36f }, TOOLS_N,  TOOLS_O,  true  },	// 火ばさみ(鉄)。手に持った時も同じ。火にかざし続けて黒ずんだ鉄=暗く(明るい鋼だと炉の火を照り返して、柄まで熱そうに光って見えた 2026-10-20)
		{ "StMetal1",     0.55f, 1.0f, false, { 1.00f, 1.00f, 1.00f }, METAL_N,  METAL_O,  true  },	// 廃鉄
		{ "StMetal2",     0.55f, 1.0f, false, { 1.00f, 1.00f, 1.00f }, METAL_N,  METAL_O,  true  },	// 廃鉄
		{ "MdlHammer",    0.55f, 0.0f, false, { 1.00f, 1.00f, 1.00f }, TOOLS_N,  TOOLS_O,  true  },	// 鎚(頭=鉄 / 柄=木 は ORM の金属度で分かれる)
		{ "Weapon",       0.35f, 1.0f, false, { 1.00f, 1.00f, 1.00f }, nullptr,  nullptr,  false },	// 鍛えている刀身(鋼)。描き方は専用(変形する)、材質と光はここと同じ
		// 試し切りの藁人形(仮の箱。2026-10-20)。切ると断面ができる = UV が無い面 → 三平面投影(物の空間)で貼る
		{ "CutPost",      0.85f, 0.0f, true,  { 1.00f, 1.00f, 1.00f }, nullptr,  nullptr,  false },	// 柱(木)
		{ "CutStraw",     0.95f, 0.0f, true,  { 1.25f, 1.05f, 0.55f }, nullptr,  nullptr,  false },	// 藁の胴体(仮: 木目の画像を藁色に寄せる)
		// 胸当て/兜 = 錆びた鉄。錆(酸化鉄)は金属ではない → 金属度は低め。
		//   金属度 1 だと拡散反射が無く、色は映り込みだけになる。反射プローブは小屋の中で撮った物なので、
		//   屋外では「暗い部屋」を映して真っ黒になった(2026-10-20 F5。PBR の「金属が黒いプラスチックに見える」落とし穴と同じ)
		{ "CutArmor",     0.75f, 0.2f, true,  { 1.00f, 1.00f, 1.00f }, nullptr,  nullptr,  false },	// 胸当て(錆びた鉄)
		{ "CutHelmet",    0.70f, 0.2f, true,  { 1.00f, 1.00f, 1.00f }, nullptr,  nullptr,  false },	// 兜(錆びた鉄)
		// 試し切りで刃に付ける仮の柄(2026-10-21)。鍔・柄頭 = 黒ずんだ鉄 / 握り = 木(革巻きの代わり)
		{ "HiltMetal",    0.55f, 0.3f, true,  { 0.55f, 0.52f, 0.50f }, nullptr,  nullptr,  false },
		{ "HiltGrip",     0.85f, 0.0f, true,  { 0.70f, 0.55f, 0.40f }, nullptr,  nullptr,  false },
	};
	#undef PACK
	#undef ANVIL_N
	#undef ANVIL_O
	#undef WOOD_N
	#undef WOOD_O
	#undef BUCKET_N
	#undef BUCKET_O
	#undef METAL_N
	#undef METAL_O
	#undef SHARP_N
	#undef SHARP_O
	#undef TOOLS_N
	#undef TOOLS_O
	#undef TABLE_N
	#undef TABLE_O
	const PropMaterial DEFAULT_MATERIAL = { "(default)", 0.80f, 0.0f, false, { 1.0f, 1.0f, 1.0f }, nullptr, nullptr, false };	// 表に無い物 = 木の様な非金属
}

namespace PropMaterials
{
	const PropMaterial& Find(const std::string& key)
	{
		for (const PropMaterial& m : TABLE)
			if (key == m.key) return m;
		return DEFAULT_MATERIAL;
	}
}

#pragma once
#include <string>

//====================================================================
//  物体の材質表(データ駆動)。2026-10-15 PBR 統一
//
//  「この物は何でできているか」を1行で書く。描画は LitRender::Draw が全部この表を見て決める。
//    旧: 描画の途中に if (key == "StForge") の様な特例が散らばっていた(炉だけ暗い色・三平面投影…)
//    新: 特例は全部この表の値 = 物を足す/質感を変える = 表を1行直すだけ(描画のコードは触らない)
//  入力の割り当て表(InputActions の BINDINGS)と同じ考え方 = 「信頼できる唯一の情報源」。
//
//  roughness(粗さ)  0 = 鏡の様につるつる .. 1 = ざらざら(光が広く散る)
//  metallic(金属度)  0 = 非金属(木・石・革)/ 1 = 金属。中間の値は現実にはほぼ無い(錆・汚れの境目くらい)
//  法線/ORM の画像があれば、その画素ごとの値が上の定数より優先される(R=AO / G=粗さ / B=金属度)
//====================================================================
struct PropMaterial
{
	const char* key;			// 物の名前(stage_layout.txt / GetObj のキーと同じ)
	float       roughness;
	float       metallic;
	bool        triplanar;		// 基本色をメッシュの UV でなく三平面投影で貼る(炉の石。UV の継ぎ目で石の大きさが揃わない対策)
	float       tint[3];		// 基本色に掛ける色(1,1,1 = そのまま)
	const char* normalTex;		// 法線マップ(nullptr = 無し)
	const char* ormTex;			// ORM マップ(nullptr = 無し)
	bool        normalDirectX;	// 法線マップが DirectX 式(緑が下向き。Unreal 用の素材はこちら)
};

namespace PropMaterials
{
	// キーで材質を引く。表に無い物は「既定の材質」(木の様な非金属)を返す=描けなくはならない
	const PropMaterial& Find(const std::string& key);
}

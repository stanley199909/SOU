#pragma once
#include <DirectXMath.h>

class Model;
class CameraBase;
struct PropMaterial;

//====================================================================
//  光を受ける物体を描く、ただ1つの入口。2026-10-15 PBR 統一
//
//  道具(金床・砥石・水槽…)・手に持った物(鎚・火ばさみ)・編集シーン、全部ここを通る。
//    光の式   = Lighting.hlsli(壁と同じ GGX)
//    光の情報 = CottageRender::Data()(太陽・環境光・炉の火の強さ。F1 で調整・保存)+ 炉の位置(毎フレーム1回 SetFirePosition)
//    材質     = GameData/PropMaterials の表
//  旧: SunStage::LitProp(Lambert)・SceneForge::DrawModelWorld(光なし)・編集シーンの複製、と描き方が3通りあった。
//====================================================================
// シーンの光の情報(全員が同じ値を使う)。並びは各シェーダーの cbuffer の先頭と同じ
struct SceneLightParams
{
	DirectX::XMFLOAT4   eye;		// xyz カメラ位置
	DirectX::XMFLOAT4   sun;		// xyz 太陽の方向(向かう側), w 強さ
	DirectX::XMFLOAT4   ambient;	// rgb 環境光の色, w 屋内の環境光の強さ
	DirectX::XMFLOAT4   fire;		// xyz 炉の火の位置, w 強さ
	DirectX::XMFLOAT4X4 lightVP;	// 太陽の影の行列
	DirectX::XMFLOAT4   shadow;		// 影: 有効 / 1テクセル / 深度バイアス / 受け側のずらし
};

// 光源ごとの ON/OFF(設計者用 F2 パネル)。見比べる為だけの切り替え=保存しない。光の強さ(調整値)は変えず、使うかどうかだけ。
//   描く全員(道具・壁・刀身・反射プローブ)がここを読む=どこか1つだけ光が残る、が起きない。
struct LightToggles
{
	bool fire         = true;	// 炉の火の直接光(点光源)
	bool sun          = true;	// 太陽の直接光(窓から差す光・屋外)
	bool ambient      = true;	// 環境光の拡散(部屋全体を照らす間接光)
	bool iblDirection = true;	// 環境光の拡散に向きと色を付ける(IBL)。OFF = 一様な補光
	bool reflections  = true;	// 環境の映り込み(IBL の鏡面反射)
	bool volumetric   = true;	// 光の筋(窓から差す太陽光が空気中の塵・煙で見える。体積光)
};

namespace LitRender
{
	LightToggles& Toggles();
	// 炉の火(ワールドの位置 + 明るさの倍率)。描く前に毎フレーム1回。倍率はゲームの状態から決める(揺らぎ・ふいご)。1 = 調整値のまま
	void SetFire(const float pos[3], float intensityScale = 1.0f);
	SceneLightParams Lights(CameraBase* camera);	// 光の情報(CottageRender::Data() + 炉の位置)。独自の描き方をする物(刀身)もここから読む
	// 太陽(時刻で動く)。毎フレーム1回、SunCycle の向きと昼の明るさ(0..1)を渡す。描く全員が Sun() を読む
	void SetSun(const DirectX::XMFLOAT3& towardSun, float daylight);
	DirectX::XMFLOAT4 Sun();	// xyz 太陽の方向, w 今の強さ(昼の強さ × 昼の明るさ × F2 の ON/OFF)
	float FillScale();			// 環境光(補光)の倍率 = F2 の ON/OFF × 昼夜(夜も少し残る)
	float SkyScale();			// 窓の外の空の明るさの倍率(夜は暗い)
	float FireIntensityScale();					// 炉の火の明るさの倍率(揺らぎ・ふいご)。壁(CottageRender)もこれを掛ける
	void BindShadow();							// 太陽の影の深度を t3 へ
	// highlight 0..1 = 次の工位を指す暖色の光(縁ほど強い)。0 = 普段どおり
	void Draw(Model* model, DirectX::FXMMATRIX world, CameraBase* camera, const PropMaterial& material, float highlight = 0.0f);
	bool NormalMapsEnabled();	// F1「Normal maps」(刀身も同じ切り替えに従う)
	void Controls();	// F1: 法線/ORM の画像の有無を切り替えて見比べる・法線の向き(DirectX/OpenGL式)を確かめる
}

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

namespace LitRender
{
	// 炉の火(ワールドの位置 + 明るさの倍率)。描く前に毎フレーム1回。倍率はゲームの状態から決める(揺らぎ・ふいご)。1 = 調整値のまま
	void SetFire(const float pos[3], float intensityScale = 1.0f);
	SceneLightParams Lights(CameraBase* camera);	// 光の情報(CottageRender::Data() + 炉の位置)。独自の描き方をする物(刀身)もここから読む
	void BindShadow();							// 太陽の影の深度を t3 へ
	void Draw(Model* model, DirectX::FXMMATRIX world, CameraBase* camera, const PropMaterial& material);
	void Controls();	// F1: 法線/ORM の画像の有無を切り替えて見比べる・法線の向き(DirectX/OpenGL式)を確かめる
}

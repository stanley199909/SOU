#ifndef __SCENE_FORGE_H__
#define __SCENE_FORGE_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <memory>
#include <vector>

class MeshBuffer;
class Texture;

// 鍛造ミニゲーム《Timing Forge》
//  ・鉄を熱して(加熱)、力を溜めて(蓄力)、叩いて成形する「打鉄」の手触りを楽しむゲーム
//  ・叩いた瞬間に火花がバーストする(既存のパーティクル)
//  ・状態遷移: TITLE(タイトル) → PLAY(鍛造中) → RESULT(結果)
class SceneForge : public SceneBase
{
public:
	void Init();
	void Uninit();
	void Update(float tick);
	void Draw();
	void DrawUI();

private:
	//--- ゲーム状態
	enum GameState
	{
		GAME_TITLE,		// タイトル画面
		GAME_PLAY,		// 鍛造中
		GAME_RESULT,	// 結果表示
	};

	struct Spark
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT3 vel;
		float life;
		float maxLife;
		float size;
	};
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT2 uv;
		DirectX::XMFLOAT4 col;
	};

	//--- 火花
	void Strike();	// 1回叩く(火花をまとめて発生)

	//--- 状態ごとの更新
	void UpdateTitle(float tick);
	void UpdatePlay(float tick);
	void UpdateResult(float tick);

	//--- 状態ごとのUI
	void DrawTitleUI();
	void DrawPlayUI();
	void DrawResultUI();

	//--- ゲーム進行
	void StartGame();	// タイトル → 鍛造開始
	void FinishGame();	// 鍛造完了 → 結果へ

	//--- 鍛造(鉄塊)
	void DrawBillet();		// 2D側面図で光る鉄条を描く
	void DrawHeatGauge();	// 温度ゲージ(HUD)

private:
	//--- 火花シミュレーション
	std::vector<Spark>  m_sparks;
	std::vector<Vertex> m_vtx;
	std::shared_ptr<MeshBuffer> m_mesh;
	std::shared_ptr<Texture>    m_glow;

	float m_time      = 0.0f;
	float m_autoTimer = 0.0f;	// タイトルの雰囲気用に自動で火花を出す間隔
	int   m_burst     = 140;	// 1回の火花数
	float m_power     = 1.0f;	// 飛び散る勢い

	//--- ゲーム状態
	GameState m_state    = GAME_TITLE;
	float     m_progress = 0.0f;	// 鍛造進度 0..100 (段階4で本実装)
	int       m_score    = 0;		// スコア    (段階4で本実装)

	//--- 温度(0=冷たい 〜 1=白熱)
	float m_heat = 0.0f;

	//--- 鉄条の側面プロファイル(中心線からの半分の厚み, 正規化 1.0=初期の太さ)
	static const int SEG = 24;		// 長手方向の分割数
	float m_th[SEG];				// 各セグメントの半厚み(段階3で叩くと減る)

	static const int MAX_SPARKS = 3000;
	static constexpr float GRAVITY      = 9.8f;
	static constexpr float TITLE_INTERVAL = 1.0f;	// タイトルで自動的に叩く間隔(秒)

	//--- 温度パラメータ
	static constexpr float HEAT_RATE = 0.55f;	// 加熱速度(F長押し, /秒)
	static constexpr float COOL_RATE = 0.12f;	// 自然冷却速度(/秒)
	static constexpr float IDEAL_MIN = 0.55f;	// 最適温度帯(下限)
	static constexpr float IDEAL_MAX = 0.85f;	// 最適温度帯(上限)
	static constexpr float OVERHEAT  = 0.92f;	// これ以上は過熱(鋼を痛める)
};

#endif // __SCENE_FORGE_H__

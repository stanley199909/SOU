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
	void Strike(float scale = 1.0f);	// 1回叩く(火花をまとめて発生, scaleで量と勢いを調整)

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
	void DrawHammer();		// ハンマーと打撃カーソル
	void DoStrike();		// 蓄力を解放して1打(変形＋フィードバック)

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
	float m_th[SEG];				// 各セグメントの半厚み(叩くと減る)

	//--- 打撃(蓄力ハンマー)
	bool  m_charging  = false;		// 蓄力中か
	float m_charge    = 0.0f;		// 蓄力(0..1)
	int   m_strikeSeg = SEG / 2;	// 打撃位置(セグメント)
	bool  m_canStrike = false;		// 開始直後の誤爆防止(SPACEを一度離すまで無効)
	float m_shake     = 0.0f;		// 打撃時の揺れ

	//--- 打撃フィードバック(ポップアップ文字)
	char         m_popupText[24] = "";
	float        m_popupLife = 0.0f;
	unsigned int m_popupCol  = 0;

	//--- リズム(メトロノーム)
	float m_beat = 0.0f;			// 拍の位相 0..1

	//--- 評価用の集計(段階5で使用)
	float m_qualitySum  = 0.0f;
	int   m_strikeCount = 0;

	static const int MAX_SPARKS = 3000;
	static constexpr float GRAVITY      = 9.8f;
	static constexpr float TITLE_INTERVAL = 1.0f;	// タイトルで自動的に叩く間隔(秒)

	//--- 温度パラメータ
	static constexpr float HEAT_RATE = 0.55f;	// 加熱速度(F長押し, /秒)
	static constexpr float COOL_RATE = 0.12f;	// 自然冷却速度(/秒)
	static constexpr float IDEAL_MIN = 0.55f;	// 最適温度帯(下限)
	static constexpr float IDEAL_MAX = 0.85f;	// 最適温度帯(上限)
	static constexpr float OVERHEAT  = 0.92f;	// これ以上は過熱(鋼を痛める)

	//--- 打撃パラメータ
	static constexpr float CHARGE_RATE = 1.6f;	// 蓄力速度(/秒, 満蓄力まで約0.6秒)
	static constexpr float STRIKE_COOL = 0.08f;	// 1打ごとに下がる温度
	static constexpr float DEFORM_MAX  = 0.22f;	// 満蓄力・最適温度での最大変形量
	static constexpr float COLD_LIMIT  = 0.35f;	// これ未満は冷たすぎ(ほぼ変形せず割れる)
	static constexpr float BEAT_PERIOD = 0.70f;	// メトロノーム周期(秒)
};

#endif // __SCENE_FORGE_H__

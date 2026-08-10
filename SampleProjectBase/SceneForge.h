#ifndef __SCENE_FORGE_H__
#define __SCENE_FORGE_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <memory>
#include <vector>

class MeshBuffer;
class Texture;
class Model;

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
	void  BuildTarget();		// 目標形状(剣)を生成
	float ShapeMatch() const;	// 現在の形状と目標の一致度(0..1)
	void  ApplyCamera();		// ゲーム用の固定カメラ(KCD風の見下ろし)を毎フレーム適用
	void  UpdateBarAnchor();	// 金床のAABBから砧面(上面中心)を求め、鉄条をその上に自動配置
	void  DrawDebugBoxes();	// デバッグ: 金床AABBと鉄条の箱を線で可視化
	void  DrawModelsTest();		// 鍛冶素材の3Dモデルを描画
	void  DrawModelWorld(Model* m, const DirectX::XMMATRIX& world);	// 共通のモデル描画
	void  DrawHammer3D();		// 3Dハンマー(蓄力で上がり打撃で振り下ろす)
	int   BuildBarMesh();		// m_th[]から3D鉄条の頂点を生成(戻り値=頂点数)
	void  Draw3DBillet();		// 3Dの光る鉄条を描画
	void  DrawBillet();		// 2D側面図で光る鉄条を描く(旧, 参考用)
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
	float m_dmg[SEG];				// 各セグメントの損傷(冷打の割れ/過熱の焼け, 0..1)
	float m_target[SEG];			// 目標形状(剣のシルエット)の半厚み
	float m_match = 0.0f;			// 目標形状との一致度 0..1

	//--- 3Dモデル配置の調整用(F1デバッグでスライダ操作)
	bool  m_show3D  = true;
	float m_mScale  = 0.02f;	// 素材パックは大きいことが多いので小さめから
	float m_mPos[3] = { 0.0f, 0.0f, 0.0f };
	float m_mYaw    = 0.0f;

	//--- ゲーム用固定カメラ(KCD風の見下ろし)
	float m_camPos[3]  = { 0.0f, 3.4f, -1.6f };	// 頭の高さから見下ろす(やや急角度)
	float m_camLook[3] = { 0.0f, 1.4f,  0.2f };	// 金床の上面(鉄条)を見る
	float m_camSway    = 0.30f;					// マウスに応じた視点の揺れ幅
	bool  m_cursorShown = true;					// OSカーソルの表示状態(PLAY中は隠す)

	//--- 調整用パラメータ(F1デバッグでスライダ変更可)
	float m_strikeCDMax = 1.25f;	// 打撃後クールダウン(秒)
	float m_coolRate    = 0.03f;	// 自然冷却速度(/秒)
	float m_aimTop      = 0.12f;	// 鉄条が画面に映る上端(高さ割合)。照準の対応範囲
	float m_aimBottom   = 0.82f;	// 鉄条が画面に映る下端(高さ割合)

	//--- 3D鉄条メッシュ
	std::vector<Vertex> m_barVtx;
	std::shared_ptr<MeshBuffer> m_barMesh;
	float m_barY     = 2.32f;	// 鉄条の中心の高さ(UpdateBarAnchorが金床の砧面から自動算出)
	float m_barLen   = 1.8f;	// 長さ
	float m_barThick = 0.18f;	// 初期の半分の厚み
	float m_barWidth = 0.12f;	// 半分の幅
	float m_barLift  = 0.0f;	// 砧面からの微調整オフセット(F1)

	//--- 金床のモデル空間AABB(アンカー計算用。Initで一度求めてキャッシュ)
	DirectX::XMFLOAT3 m_anvilMin = { 0,0,0 };
	DirectX::XMFLOAT3 m_anvilMax = { 0,0,0 };
	//--- 砧面のアンカー(鉄条を乗せる点。UpdateBarAnchorが毎フレーム算出)
	DirectX::XMFLOAT3 m_barAnchor = { 0, 2.32f, 0 };

	//--- 3Dハンマー(F1で向き調整。蓄力で上がり打撃で振り下ろす)
	float m_hammerScale  = 0.02f;
	float m_hammerRot[3] = { 3.14f, -0.20f, -1.58f };	// 向き(ラジアン)。調整済み既定
	float m_hammerOff[3] = { 0.06f, 0.0f, 0.0f };		// 打撃点からの位置微調整
	float m_hammerLift   = 0.40f;					// 頭の高さ(アニメで変化)
	float m_strikeAnim   = 0.0f;					// 振り下ろしアニメ(1→0)
	static constexpr float HAMMER_REST_LIFT    = 0.40f;
	static constexpr float HAMMER_CHARGE_RAISE = 0.55f;
	static constexpr float STRIKE_ANIM_TIME    = 0.16f;

	//--- 打撃(蓄力ハンマー)
	bool  m_charging  = false;		// 蓄力中か
	float m_charge    = 0.0f;		// 蓄力(0..1)
	float m_strikeCD  = 0.0f;		// 打撃後のクールダウン残り(連打防止)
	int   m_strikeSeg = SEG / 2;	// 打撃位置(セグメント)
	bool  m_canStrike = false;		// 開始直後の誤爆防止(SPACEを一度離すまで無効)
	float m_shake     = 0.0f;		// 打撃時の揺れ

	//--- 打撃フィードバック(ポップアップ文字)
	char         m_popupText[24] = "";
	float        m_popupLife = 0.0f;
	unsigned int m_popupCol  = 0;

	//--- リズム(自分の打撃テンポ。速すぎ遅すぎない一定リズムで効率アップ)
	float m_sinceStrike  = 0.0f;	// 前回打撃からの経過時間
	int   m_rhythmStreak = 0;		// 良いテンポが続いている回数
	float m_sizzleTimer  = 0.0f;	// 過熱時のジュー音の再生間隔

	//--- 評価用の集計(段階5で使用)
	float m_qualitySum  = 0.0f;
	int   m_strikeCount = 0;

	static const int MAX_SPARKS = 3000;
	static constexpr float GRAVITY      = 9.8f;
	static constexpr float TITLE_INTERVAL = 1.0f;	// タイトルで自動的に叩く間隔(秒)

	//--- 温度パラメータ
	static constexpr float HEAT_RATE = 0.55f;	// 加熱速度(R長押しで炉で加熱, /秒)
	// 自然冷却速度と打撃CDは調整しやすいようメンバー変数(m_coolRate / m_strikeCDMax)にした
	static constexpr float IDEAL_MIN = 0.55f;	// 最適温度帯(下限)
	static constexpr float IDEAL_MAX = 0.85f;	// 最適温度帯(上限)
	static constexpr float OVERHEAT  = 0.92f;	// これ以上は過熱(鋼を痛める)

	//--- 打撃パラメータ
	static constexpr float CHARGE_RATE = 1.6f;	// 蓄力速度(/秒, 満蓄力まで約0.6秒)
	static constexpr float STRIKE_COOL = 0.08f;	// 1打ごとに下がる温度
	static constexpr float DEFORM_MAX  = 0.22f;	// 満蓄力・最適温度での最大変形量
	static constexpr float COLD_LIMIT  = 0.35f;	// これ未満は冷たすぎ(ほぼ変形せず割れる)
	static constexpr float CADENCE_MIN = 0.45f;	// 良い打撃間隔の下限(これより速いと駄目)
	static constexpr float CADENCE_MAX = 1.00f;	// 良い打撃間隔の上限(これより遅いと駄目)
	static constexpr int   GROOVE_HITS = 2;		// この回数だけ良いテンポが続くと効率アップ
	static constexpr float BURN_RATE   = 0.18f;	// 過熱で放置したとき鋼が焼ける速度(/秒)
};

#endif // __SCENE_FORGE_H__

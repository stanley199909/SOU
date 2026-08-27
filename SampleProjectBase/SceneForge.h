#ifndef __SCENE_FORGE_H__
#define __SCENE_FORGE_H__

#include "SceneBase.hpp"
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <string>

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
	struct Prop;	// シーン装飾プロップ(定義は後方)

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

	//--- 炭火から立ち上る余燼(火の粉)。持続発生・上昇・淡出
	void UpdateEmbers(float tick);	// 発生＋上昇＋消滅のシミュレーション(CPU)
	void DrawEmbers();				// 余燼をカメラ向きビルボードで加算描画(火花と同じシェーダー)

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
	void  DrawModelWorld(Model* m, const DirectX::XMMATRIX& world,
	                     const DirectX::XMFLOAT4& tint = DirectX::XMFLOAT4(1,1,1,1));	// 共通のモデル描画(tintで色掛け)
	//--- シーン装飾(炉/風箱/作業台/水桶/床)
	void  LoadProp(const char* key, const char* fbx, const char* tex,
	               float targetSize, float px, float py, float pz, float yaw, bool groundSnap);
	Prop* GetProp(const char* key);			// m_props からキーで検索(無ければnullptr)
	void  LoadLayout();						// stage_layout.txt を読み、プロップ/炭の配置を上書き(編集シーンと共有)
	DirectX::XMMATRIX PropWorld(Prop& p);	// アンカー方式のワールド行列(地面に自動設置)
	void  DrawScenery();		// 装飾モデルをまとめて描画
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
	std::vector<Spark>  m_embers;		// 炭火の余燼(火の粉)。上昇して淡出
	float               m_emberSpawn = 0.0f;	// 余燼発生の端数(1未満を持ち越す)
	static const int    MAX_EMBERS = 500;
	//--- 余燼発生器の調整値(編集シーンで調整→stage_layout.txtの E 行で読む)
	float m_emberPos[3]  = { 3.20f, 0.60f, 1.80f };	// 発生中心(既定は炭付近)
	float m_emberArea[2] = { 0.45f, 0.60f };		// 発生半径(X,Z)
	float m_emberRate    = 45.0f;					// 1秒あたりの発生数
	float m_emberRise    = 0.8f;					// 上昇初速
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

	//--- シーン装飾のプロップ(炉/風箱/作業台/水桶/床)。F1スライダで配置調整→焼込む
	struct Prop
	{
		std::string       key;			// CreateObj/GetObj のキー
		std::string       label;		// F1パネル表示名
		float             scale = 0.02f;
		float             pos[3] = { 0,0,0 };
		float             yaw   = 0.0f;
		bool              groundSnap = true;	// AABB下面を床の高さに合わせる
		DirectX::XMFLOAT3 aabbMin = { 0,0,0 };	// モデル空間AABB(Loadでキャッシュ)
		DirectX::XMFLOAT3 aabbMax = { 0,0,0 };
	};
	std::vector<Prop> m_props;
	float m_groundY = 0.0f;	// 床の高さ(金床のワールドAABB下面から算出)
	bool  m_showScenery = true;

	//--- Unity風のドラッグ配置エディタ(F1中に選択したプロップを地面上でLMBドラッグ移動)
	int   m_editSel     = -1;		// 選択中のプロップindex(-1=なし)
	bool  m_editDragging = false;
	float m_editPrevX = 0.0f, m_editPrevY = 0.0f;
	void  UpdateEditorDrag();	// LMBドラッグで選択プロップを地面移動

	//--- 炉のマテリアル別テクスチャ(4UVタイル: 石/レンガ/火室/金属)。
	//    F1で各マテリアルにどのテクスチャを当てるか選び、正解の割当を焼き込む。
	std::vector<std::shared_ptr<Texture>> m_forgeTex;	// 候補テクスチャ
	std::vector<std::string>              m_forgeTexName;// F1表示名
	std::vector<int>                      m_forgeMatPick;// マテリアルindex -> m_forgeTex index
	void ApplyForgeTextures();	// m_forgeMatPickに従って炉のマテリアルへ割当

	//--- 自作の光る炭ベッド(FBXに頼らず、狙った位置に確実に炭火を出す。明滅する)
	std::shared_ptr<MeshBuffer> m_coalMesh;	// 水平の板(2枚=両面)
	std::shared_ptr<Texture>    m_coalTex;	// 合成した炭テクスチャ
	bool  m_coalOn     = true;
	float m_coalPos[3] = { 3.20f, 0.55f, 1.80f };	// 炉の火床の位置(F1で合わせる)
	float m_coalYaw    = 0.0f;
	float m_coalSize[2]= { 0.55f, 0.75f };	// 板の半径(X,Z)
	float m_coalGlow   = 1.8f;				// 明るさ(Bloomで光る)
	void  DrawCoalBed();	// 光る炭ベッドを描画

	//--- 水槽の水面(真の屈折。背後のシーンをスナップショットして透ける)
	//    メッシュは m_coalMesh(±1の水平板)を流用。シェーダーだけ PS_Water に差し替える。
	bool  m_waterOn     = true;
	float m_waterPos[3] = { -1.40f, 0.55f, 0.0f };	// 水槽の位置(stage_layout.txtで上書き)
	float m_waterYaw    = 0.0f;
	float m_waterSize[2]= { 0.45f, 0.55f };			// 板の半径(X,Z)
	float m_waterBump   = 1.0f;						// さざ波の強さ(屈折/法線)
	void  DrawWater();	// 水面を描画(屈折)

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
	bool  m_hammerAlt = false;		// 金床打撃音の交互再生(false→SE_ANVIL1, true→SE_ANVIL2)
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

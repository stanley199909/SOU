#ifndef __SCENE_FORGE_H__
#define __SCENE_FORGE_H__

#include "SceneBase.hpp"
#include "ScreenFade.h"	// 画面フェード(場面/状態遷移の黒幕。GameLogic)
#include "StateMachine.h"	// 工程(step)FSM の枠組み(statemachinechase から再利用)
#include "HeatStep.h"		// 各工程の状態(StateMachine/Player)
#include "ForgeStep.h"
#include "QuenchStep.h"
#include "WeaponRecipe.h"	// GameData: StepName / StepSetting / WeaponRecipe(データ)
#include "HammerPhysics.h"	// Physics: 鎚の弾簧-阻尼運動(自作物理)
#include "ForgingSim.h"		// Physics: 鍛造される鉄の状態と変形(自作物理)
#include "Particles.h"		// Physics: 火花・余燼の粒子シミュ(自作物理)
#include "Player.h"			// 鍛冶場を歩き回るプレイヤ(一人称の走動)
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

	//--- 工程(step)状態が呼び出す窓口(StateMachine/Player の各 Step から使う) ---
	float HeatValue() const { return m_heat; }	// 現在の温度 0..1(HeatStep が完了判定に使う)
	bool  AllSegmentsDone() const;				// 全区域が成形完了したか(ForgeStep の完了条件)
	void  AdvanceStep();						// 次の工程へ進む(配方の順序で遷移。無ければ完成)
	const StepSetting& CurrentStep() const;		// 今実行中の工程設定(HUD が instruction を表示)

private:
	struct Prop;	// シーン装飾プロップ(定義は後方)

	//--- ゲーム状態
	enum GameState
	{
		GAME_TITLE,		// タイトル画面
		GAME_PLAY,		// 鍛造中
		GAME_RESULT,	// 結果表示(成功)
		GAME_OVER,		// 廃件(失敗)
	};

	// 火花/余燼の粒子型は Physics/Particles が持つ(Particles::Particle)。
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;
		DirectX::XMFLOAT2 uv;
		DirectX::XMFLOAT4 col;
	};

	//--- 火花
	void Strike(float scale = 1.0f);	// 1回叩く(火花をまとめて発生, scaleで量と勢いを調整)

	//--- 炭火から立ち上る余燼(火の粉)。シミュは Particles、描画はここ。
	void DrawEmbers();				// 余燼をカメラ向きビルボードで加算描画(火花と同じシェーダー)

	//--- 状態ごとの更新
	void UpdateTitle(float tick);
	void UpdatePlay(float tick);
	void UpdateResult(float tick);
	void UpdateGameOver(float tick);

	//--- 状態ごとのUI
	void DrawTitleUI();
	void DrawPlayUI();
	void DrawResultUI();
	void DrawGameOverUI();
	void DrawParchmentPanel(float yCenter, float heightRatio);	// 結果/失敗の下地(羊皮紙)

	//--- ゲーム進行
	void StartGame();	// タイトル → 鍛造開始
	void FinishGame();	// 鍛造完了 → 結果へ(成功)
	void GameOverGame();// 廃件 → GameOverへ(失敗)

	//--- 廃件槽(失误で満ちると失敗)。KCD式: 「叩く場所」は指示しない。誤打だけ負向で知らせる。
	//    廃件率 = 不可逆。良い打撃でも減らない。満(1.0)で武器が廃棄=GameOver。
	float m_spoil   = 0.0f;			// 廃件率 0..1(過熱/冷打/完成済みの段を叩く=無用打撃で増える。減らない)
	static constexpr float SPOIL_BURN  = 0.10f;	// 過熱打(焼け)      … 1打で+10%
	static constexpr float SPOIL_COLD  = 0.05f;	// 冷打(赤くない鋼)  … 1打で+5%(開始時は冷たいので軽め)
	static constexpr float SPOIL_WASTE = 0.08f;	// 無用打撃(完成段をまた叩く) … 1打で+8%
	// 瞄準区域の可視化(デバッグ用)。既定OFF。Pキーでトグル。
	bool  m_showAimHi = false;

	//--- 鍛造(鉄塊)
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
	int   BuildBarMesh();		// 高さ場 m_h[][] から3D鉄板の頂点を生成(戻り値=頂点数)
	void  Draw3DBillet();		// 3Dの光る鉄板を描画
	void DrawHeatGauge();	// 温度ゲージ(HUD)
	void DoStrike();		// 蓄力を解放して1打(変形＋フィードバック)

private:
	//--- 火花・余燼の粒子シミュ(状態と運動は Physics/Particles が所有)。描画はこのクラス。
	Particles m_particles;
	//--- 余燼発生器の調整値(編集シーンで調整→stage_layout.txtの E 行で読む)
	float m_emberPos[3]  = { 3.20f, 0.60f, 1.80f };	// 発生中心(既定は炭付近)
	float m_emberArea[2] = { 0.45f, 0.60f };		// 発生半径(X,Z)
	float m_emberRate    = 45.0f;					// 1秒あたりの発生数
	float m_emberRise    = 0.8f;					// 上昇初速
	std::vector<Vertex> m_vtx;
	std::shared_ptr<MeshBuffer> m_mesh;
	std::shared_ptr<Texture>    m_glow;
	std::shared_ptr<Texture>    m_uiParchment;	// UI: 羊皮紙パネル(結果/失敗画面の下地。抠いた透明PNG)

	float m_time      = 0.0f;
	float m_autoTimer = 0.0f;	// タイトルの雰囲気用に自動で火花を出す間隔
	int   m_burst     = 140;	// 1回の火花数
	float m_power     = 1.0f;	// 飛び散る勢い

	//--- ゲーム状態
	GameState m_state    = GAME_TITLE;
	int       m_score    = 0;		// スコア
	ScreenFade m_fade;				// 画面フェード(起動時の淡入・状態遷移の黒幕)

	//--- 工程(step)状態機: PLAY 内部の下位 FSM。上位=GameState(TITLE/PLAY/RESULT)=階層型(HSM)。
	//    枠組みは statemachinechase の StateMachine を再利用。SceneForge が owner(chase の Enemy 役)。
	//    「どの工程か」は状態機、「工程の順序」は配方(データ)。両者は SceneForge だけが繋ぐ。
	StateMachine        m_stepMachine;				// 工程 FSM
	const WeaponRecipe* m_recipe  = &ShortSword;	// 製作中の武器配方(データ駆動)
	int                 m_stepIdx = 0;				// m_recipe->steps 内の現在位置
	std::unique_ptr<HeatStep>   m_heatStep;			// 各工程状態(owner=this を渡して生成)
	std::unique_ptr<ForgeStep>  m_forgeStep;
	std::unique_ptr<QuenchStep> m_quenchStep;
	void SetupSteps();								// 各状態を生成し m_stepMachine へ登録(Init で一度)

	//--- 温度(0=冷たい 〜 1=白熱)
	float m_heat = 0.0f;

	//--- 鍛造される鉄の状態＋物理は Physics/ForgingSim に切り出した。
	//    (格子・高さ場・目標形・成形進捗・損傷、および打撃による変形演算を全て所有)
	//    SceneForge は m_forging へ「打撃を適用/状態を読む」だけ。玩家の動作は ForgeStep。
	ForgingSim m_forging;
	float m_match  = 0.0f;			// 成形の進捗(平均) 0..1。表示用に SceneForge が保持
	//--- FPS式照準: 画面中心の準心から射線を飛ばし、板に当たったセルを求める
	int   m_aimI = ForgingSim::NL / 2, m_aimJ = ForgingSim::NW / 2;	// 現在照準しているセル
	bool  m_aimValid = false;					// 準心が板の上にあるか
	DirectX::XMFLOAT3 m_aimWorld = { 0, 0, 0 };	// 準心が当たった板上のワールド座標
	//--- FPS式の受限環視カメラ(マウスで視角を回す。準心は常に画面中心=カメラ正前方)
	float m_lookYaw = 0.0f, m_lookPitch = 0.0f;	// 基準視線からのマウス累積回転(夹住)
	DirectX::XMFLOAT3 m_camFwd = { 0, 0, 1 };	// 現在のカメラ正前方(照準射線に使う)
	void  UpdateMouseLook();		// マウス移動を視角(yaw/pitch)へ累積(再センタリング方式)
	void  UpdateAim();				// 準心射線を板と交差させ m_aimI/J/World を更新

	//--- 一人称の「走動モード」(工位に着く前に工坊を歩く) ---
	// 二模式切替: 走動中=ApplyWalkCamera(玩家目線), 工位=ApplyCamera(調校済みの鍛造framing)。
	Player m_player;					// 歩き回るプレイヤ本体(位置/向き/速度/可互動を持つ)
	bool   m_walkMode   = false;		// true=走動モード / false=工位(鍛造)モード。E互動で工位へ入る
	float  m_walkPitch  = 0.0f;			// 走動カメラの上下(pitch)累積。左右(yaw)は m_player が持つ
	float  m_walkEyeH   = 1.6f;			// 目線の高さ(玩家足元からカメラまで, 単位)
	float  m_walkSens   = 0.0017f;		// 走動時マウス感度(rad/px)。UpdateMouseLook と同値で統一
	float  m_walkPitchLim = 1.3f;		// 上下視角の制限(rad)≒74°。真上/真下でひっくり返るのを防ぐ
	float  m_walkSpeed  = 3.0f;			// 走動速度(単位/秒)。毎フレーム m_player へ渡す(F1で調整)
	void   UpdateWalkLook();			// 走動時: マウスを玩家yaw(左右)とカメラpitch(上下)へ
	void   ApplyWalkCamera();			// 走動時: カメラを玩家の目線に置く一人称カメラ

	//--- F1調整値の永続化(Assets/forge_tuning.txt)。Initで読み, Uninit/Saveボタンで書く。
	void  LoadTuning();
	void  SaveTuning();

	//--- 3Dモデル描画のON/OFF(Scenery のガード)
	bool  m_show3D  = true;

	//--- ゲーム用固定カメラ(KCD風の見下ろし)
	// KCD2の一人称に寄せる: 目線の高さから砧・炉を見下ろし、工件と炉膛が視界を占める。
	float m_camPos[3]  = { 0.0f, 2.30f, -0.95f };	// 低く・近く(鉄匠の頭の位置)
	float m_camLook[3] = { 0.0f, 1.15f,  0.55f };	// 強めに見下ろす(砧・炉膛が画面に入る)
	float m_camFov     = 0.8901f;					// 縦画角(rad)≒51°。KCDの画角に近い(狭すぎない)
	// --- 刃の長手に沿った「狙い位置」追従カメラ(KCDの視角移動) ---
	// マウス縦で m_aimRail(0=手前/near, 1=奥/far)を動かし、注視点とカメラをZ方向に寄せる。
	// これで刃の下半段(手前)も準心に入り、視角と錘が一緒に付いてくる。
	float m_aimRail       = 0.5f;					// 目標の狙い位置(0..1)。連続=ハンマー/打撃はこれ
	float m_aimRailSmooth = 0.5f;					// 平滑後の「カメラ用」rail(3段の停位へ吸着)
	static const int NVIEW = 3;						// KCDの固定カメラ段数(刃を3分割)
	int   m_viewSeg       = 1;						// 現在のカメラ段(0=手前,1=中,2=奥)
	float m_camFollowZ    = 0.55f;					// カメラ本体がZ追従する割合(0=注視点だけ動く)
	float m_camPanGain    = 1.0f;					// 追従量の倍率(狙える範囲を微調整)
	float m_camLerpRate   = 4.0f;					// 3段カメラ切替の速さ(小=ゆっくり重い,大=機敏)
	float m_camSway    = 0.30f;					// マウスに応じた視点の揺れ幅
	bool  m_cursorShown = true;					// OSカーソルの表示状態(PLAY中は隠す)

	//--- 調整用パラメータ(F1デバッグでスライダ変更可)
	float m_strikeCDMax = 1.25f;	// 打撃後クールダウン(秒)
	float m_coolRate    = 0.03f;	// 自然冷却速度(/秒)

	//--- 武器モーフ(Blenderで作った同拓扑の各段FBXを頂点補間して成形する) ---
	// uv は真の鋼テクスチャ採样用。morphでUVは不変なので stage0 の値を全段で使う。
	// フィールド順は VS_Wp の VIN 宣言順(pos→nrm→uv→col)と一致させること(入力レイアウトが宣言順で焼かれる)。
	struct WpVtx { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 nrm; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 col; };
	struct WpStage { std::vector<DirectX::XMFLOAT3> pos, nrm; std::vector<DirectX::XMFLOAT2> uv; };	// 1段分の生頂点(ローカル)
	std::vector<WpStage>        m_wpStage;		// stage_0 .. stage_final
	std::vector<unsigned int>   m_wpIdx;		// インデックス(全段共通)
	std::vector<WpVtx>          m_wpVtx;		// 補間後の頂点(毎フレーム再構築)
	std::shared_ptr<MeshBuffer> m_wpMesh;
	std::shared_ptr<Texture>    m_wpTex;		// 真の鋼テクスチャ(BaseColor=冷鋼の地色)。発光は m_heat 駆動
	int   m_wpN = 0;							// 1段の頂点数
	bool  m_wpOk = false;						// 読み込み成功&段間で頂点数一致
	float m_forgeProg = 0.0f;					// 全体進捗 0..1(=各区域の平均。F1のプレビュー用)
	//--- 分区域進度は m_forging が所有(ForgingSim::NSEG / SegProg / SegDone / AllSegmentsDone)。
	int   m_aimSeg = 0;							// 現在照準している区域(AimSystemが更新)
	// 照準している区域番号。AimSystem(射線×区域ボックス)が決めた値をそのまま返す。
	int   AimSeg() const { return m_aimSeg; }
	DirectX::XMFLOAT3 m_wpMin = { 0,0,0 }, m_wpMax = { 0,0,0 };	// stage0のローカルAABB(配置用)
	//--- 配置調整(F1スライダ。向き/大きさをここで合わせて焼き込む)
	float m_wpScale = 1.0f;						// 追加スケール倍率(AABBフィットにさらに掛ける)
	float m_wpYaw = 0.0f, m_wpPitch = 0.0f, m_wpRoll = 0.0f;	// 向き(0=前後/屏幕奥行き。90°で左右横向き)
	float m_wpOff[3] = { 0.0f, 0.0f, 0.0f };	// 砧面アンカーからの微調整
	//--- 金属の質感パラメータ(PS_Wpへ渡す。廉価IBL=高光+環境反射+菲涅尔。核显向けにGPU負荷は低く抑える)
	//    UE5のPBR質感の主因は「環境反射」。HDRIを読まず、反射向きで空/地の2色を補間する擬似環境で代用する。
	float m_wpRough   = 0.35f;					// 粗さ0..1(小=鏡面的で高光が鋭い/大=拡散的)
	float m_wpMetal   = 0.85f;					// 金属度0..1(大=反射が地色に色付き、拡散が弱まる=金属らしく)
	float m_wpSpec    = 0.6f;					// 直接光の高光(鏡面ハイライト)の強さ
	float m_wpEnv     = 0.5f;					// 擬似環境反射の強さ(金属が「周囲を映す」度合い)
	float m_wpFresnel = 1.0f;					// 縁の反射増強(菲涅尔)の強さ
	float m_wpSky[3]    = { 0.55f, 0.62f, 0.75f };	// 擬似環境の上方向(空)の色
	float m_wpGround[3] = { 0.18f, 0.15f, 0.12f };	// 擬似環境の下方向(地面/炉床)の色
	void  LoadWeaponStages();					// Assets/Model/weapon/stage_*.fbx を読む
	DirectX::XMMATRIX WeaponWorld() const;		// 武器ローカル→ワールドのフィット変換(照準/描画で共用)
	void  BuildWeaponMorph();					// m_forgeProgから補間頂点を作る
	void  DrawWeapon();							// 武器を描画(発光+簡易ライティング)
	//--- 目標ゴースト: stage_final の形を半透明で重ねて「完成形」を示す(KCD2には無い自作要素)。
	//    実体が到位した区域では実体とゴーストが重なり見えなくなる=進むほど自然に「埋まる」。
	std::vector<WpVtx>          m_ghostVtx;		// ゴースト頂点(stage_finalを変換して毎フレーム作る)
	std::shared_ptr<MeshBuffer> m_ghostMesh;
	bool  m_showGhost = false;					// 目標ゴースト表示(既定OFF。Gキーで切替。冗長なので任意)
	bool  m_hideCoalTest = false;				// 【診断】Kキーで炭床を隠す。炉のtexture跳動が炭のz-fighting由来か切り分ける用
	void  BuildGhostMesh();						// stage_final を WeaponWorld で変換してm_ghostVtxへ
	void  DrawGhostTarget();					// 半透明で完成形の輪郭を重ねる

	//--- 3D鉄条メッシュ
	std::vector<Vertex> m_barVtx;
	std::shared_ptr<MeshBuffer> m_barMesh;
	float m_barY     = 2.32f;	// 鉄条の中心の高さ(UpdateBarAnchorが金床の砧面から自動算出)
	float m_barLen   = 1.8f;	// 長さ
	float m_barThick = 0.18f;	// 初期の半分の厚み
	float m_barWidth = 0.22f;	// 鉄坯(進捗0)の一様な半幅。目標プロファイルより広く=削って武器へ
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
	float m_waterFoam   = 0.06f;					// 岸の泡(容器壁との交差)の帯幅(視空間)
	float m_waterDepthFade = 0.35f;					// 水深で色が濃くなる距離(視空間)
	void  DrawWater();	// 水面を描画(屈折+深度)

	//--- 金床のモデル空間AABB(アンカー計算用。Initで一度求めてキャッシュ)
	DirectX::XMFLOAT3 m_anvilMin = { 0,0,0 };
	DirectX::XMFLOAT3 m_anvilMax = { 0,0,0 };
	//--- 砧面のアンカー(鉄条を乗せる点。UpdateBarAnchorが毎フレーム算出)
	DirectX::XMFLOAT3 m_barAnchor = { 0, 2.32f, 0 };

	//--- 3Dハンマー(F1で向き調整。蓄力で上がり打撃で振り下ろす)
	float m_hammerScale  = 0.02f;
	float m_hammerRot[3] = { 3.14f, -0.20f, -1.58f };	// 向き(ラジアン)。調整済み既定
	float m_hammerOff[3] = { 0.06f, 0.0f, 0.0f };		// 打撃点からの位置微調整
	// 鎚の縦運動(高さ/速度)と弾簧-阻尼の係数は Physics/HammerPhysics に切り出した。
	//   ForgeStep が Hold/Strike で駆動し、Update で静止高へ収束する。係数は F1 で調整・tuning に保存。
	HammerPhysics m_hammer;
	// 表示用の平滑化した横位置(XZ)。準心が格子単位で跳ぶのを Lerp::Damp で吸収する。
	// Draw はこれを読む。y は m_hammer.Lift() のアニメをそのまま使う。
	DirectX::XMFLOAT3 m_hammerPos = { 0, 0, 0 };
	bool m_hammerPosInit = false;					// 初回だけ瞬間セット(起動時に飛んでこない)
	float m_hammerFollow = 12.0f;	// 錘のXZ追従の速さ(小=遅れて重い, 大=機敏)。F1「Hammer follow」
	float m_aimSens      = 0.0020f;	// 照準感度(小=重い/慎重, 大=軽快)。F1「Aim sens」
	// 弾簧-阻尼の係数(restLift/chargeRaise/stiffness/damping/mass/impulse)は m_hammer が持つ。
	//   F1「Hammer」窓と forge_tuning.txt は m_hammer.* を直接指す。
	// 反冲(後座)の見た目: 打撃の反作用で錘が「奥行き(手前へ後退)＋錘頭の上翻り」する。
	// 位相 rp は上記バネの縦速度から直接求める(接触直後=最大→上昇で減衰)=物理と一致。
	float HAMMER_RECOIL_BACK  = 0.60f;	// 手前(-Z)へ後退する量(world)
	float HAMMER_RECOIL_TILT  = 1.30f;	// 錘頭が上へ翻る回転(rad, ~75°)
	float CAM_SHAKE_AMP       = 0.055f;	// 打撃時のカメラ縦揺れ(反冲がプレイヤーに伝わる)
	// 手持ち感(有機な運鏡): 正弦一本=工整に見えるので、無理数比の正弦を重ねた非周期ノイズで
	// 「呼吸(常駐の微漂移)」と「蓄力の微顫」をカメラに乗せる。振幅は極小=気付かないが手応えが出る。
	float m_camBreathAmp   = 0.018f;	// 呼吸の振幅(機位のゆっくりした漂移)
	float m_camBreathSpeed = 0.70f;	// 呼吸の速さ(低頻)
	float m_camTremorAmp   = 0.0f;		// 蓄力の微顫: 既定OFF(ユーザー判断で不要)。滑块で試せるが常用は0
	float m_camTremorSpeed = 22.0f;	// 微顫の速さ(高頻)
	float m_camTremorRamp  = 3.0f;		// 微顫の立ち上がり指数(charge^rate)。大=満蓄直前まで殆ど震えない
	float m_camLookNoise   = 0.45f;	// 注視点への伝達(機位より小さく揺れる=視線は概ね工件に残る)

	//--- 打撃(蓄力ハンマー)
	bool  m_charging  = false;		// 蓄力中か
	float m_charge    = 0.0f;		// 蓄力(0..1)
	float m_strikeCD  = 0.0f;		// 打撃後のクールダウン残り(連打防止)
	bool  m_canStrike = false;		// 開始直後の誤爆防止(SPACEを一度離すまで無効)
	bool  m_hammerAlt = false;		// 金床打撃音の交互再生(false→SE_ANVIL1, true→SE_ANVIL2)
	bool  m_heatSndOn = false;		// 加熱(R長押し)の持続音が鳴っているか(ループ開始/停止の管理用)
	float m_shake     = 0.0f;		// 打撃時の揺れ

	//--- 打撃フィードバック(ポップアップ文字)
	char         m_popupText[96] = "";	// 日本語(UTF-8)の主人公セリフも入るよう余裕を持たせる
	float        m_popupLife = 0.0f;
	unsigned int m_popupCol  = 0;

	//--- リズム(自分の打撃テンポ。速すぎ遅すぎない一定リズムで効率アップ)
	float m_sinceStrike  = 0.0f;	// 前回打撃からの経過時間
	int   m_rhythmStreak = 0;		// 良いテンポが続いている回数
	float m_sizzleTimer  = 0.0f;	// 過熱時のジュー音の再生間隔

	//--- 評価用の集計(段階5で使用)
	float m_qualitySum  = 0.0f;
	int   m_strikeCount = 0;

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
	static constexpr float COLD_LIMIT  = 0.35f;	// これ未満は冷たすぎ(ほぼ変形せず割れる)
	static constexpr float CADENCE_MIN = 0.45f;	// 良い打撃間隔の下限(これより速いと駄目)
	static constexpr float CADENCE_MAX = 1.00f;	// 良い打撃間隔の上限(これより遅いと駄目)
	static constexpr int   GROOVE_HITS = 2;		// この回数だけ良いテンポが続くと効率アップ
	static constexpr float BURN_RATE   = 0.18f;	// 過熱で放置したとき鋼が焼ける速度(/秒)

	//--- 打撃品質・評価・フィードバックの調整値(旧: DoStrike にベタ書きだった係数群)
	static constexpr float HEAT_EFF_COLD = 0.10f;	// 冷たい鋼での変形効率(ほぼ効かない)
	static constexpr float HEAT_EFF_OVER = 0.70f;	// 過熱鋼での変形効率(効くが品質悪)
	static constexpr float GROOVE_MULT   = 1.30f;	// リズムが乗ったときの変形効率倍率
	static constexpr float POWER_PERFECT = 0.85f;	// この蓄力以上でPERFECT判定
	static constexpr float POWER_GOOD    = 0.50f;	// この蓄力以上でGOOD判定
	static constexpr float QUALITY_PERFECT = 1.0f;	// PERFECT打の品質
	static constexpr float QUALITY_GOOD    = 0.7f;	// GOOD打の品質
	static constexpr float QUALITY_WEAK    = 0.4f;	// WEAK打の品質
	static constexpr float GROOVE_QUALITY_BONUS = 0.20f;	// リズム時の品質ボーナス
	static constexpr int   SCORE_PER_QUALITY = 100;	// 品質1.0あたりの得点
	static constexpr float POPUP_LIFE = 0.8f;		// 打撃フィードバック文字の表示時間(秒)

	//--- 結果評価(S/A/B/C): 完成度・打撃品質・廃件率を1本の「出来栄え」0..1へ合成し、閾値で等級化。
	//    「注定成形」ゲームなので形は必ず完成に近づく→評価は「どれだけ綺麗に打てたか」を主にする。
	float GradeScore() const;		// 出来栄え 0..1(=形の一致・打撃品質・廃件率の合成)
	char  GradeLetter() const;		// GradeScore を S/A/B/C に量子化
	static constexpr float GRADE_W_MATCH   = 0.45f;	// 出来栄えに占める「形の一致度」の重み
	static constexpr float GRADE_W_QUALITY = 0.55f;	// 同「打撃品質の平均」の重み(綺麗な打鉄を主に評価)
	static constexpr float GRADE_SPOIL_PEN = 0.60f;	// 廃件率1.0あたりで出来栄えから差し引く量(罰)
	static constexpr float GRADE_S = 0.90f;	// この出来栄え以上で S
	static constexpr float GRADE_A = 0.75f;	// 〃 A
	static constexpr float GRADE_B = 0.55f;	// 〃 B (未満は C)
};

#endif // __SCENE_FORGE_H__

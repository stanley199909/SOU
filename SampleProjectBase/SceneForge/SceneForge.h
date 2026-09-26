#ifndef __SCENE_FORGE_H__
#define __SCENE_FORGE_H__

#include "SceneBase.hpp"
#include "ScreenFade.h"	// 画面フェード(場面/状態遷移の黒幕。GameLogic)
#include "StateMachine.h"	// 工程(step)FSM の枠組み(statemachinechase から再利用)
#include "HeatStep.h"		// 各工程の状態(StateMachine/Player)
#include "ForgeStep.h"
#include "QuenchStep.h"
#include "GrindStep.h"
#include "WeaponRecipe.h"	// GameData: StepName / Station / StepSetting / WeaponRecipe(データ)
#include "HammerPhysics.h"	// Physics: 鎚の弾簧-阻尼運動(自作物理)
#include "GrindWheel.h"		// Physics: 足踏み砥石の回転(力積+指数減衰)
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
	float HeatValue() const { return m_forging.Heat(); }	// 現在の温度 0..1
	bool  IsBurning() const { return m_forging.IsBurning(); }	// 鋼が燃えている(火花を噴く)=加熱工程の完了条件
	bool  BothSidesDone() const;				// 両面とも成形完了したか(鍛打工程の完了条件)
	bool  AllSharp() const { return m_forging.AllSharp(); }	// 刃が全区域研ぎ上がったか(研磨工程の完了条件)
	bool  AtStation(Station s) const;			// 玩家が今その工位で作業中か(移動アニメ中は false)
	bool  TryQuench();							// 淬火を試みる。冷めすぎなら独白で断り false。OKなら蒸気+音を出し true
	void  SetPlunge(float t01);					// 淬火: 刃が水へ沈む進み 0..1(QuenchStep が時間で駆動)
	void  SetLetterbox(float t01);				// 終幕: 上下の黒帯が入る進み 0..1(同上)
	void  AdvanceStep();						// 次の工程へ進む(配方の順序で遷移。無ければ完成)
	const StepSetting& CurrentStep() const;		// 今実行中の工程設定(HUD が instruction を表示)

private:
	struct Prop;	// シーン装飾プロップ(定義は後方)

	//--- ゲーム状態
	enum GameState
	{
		GAME_TITLE,		// タイトル画面
		GAME_PLAY,		// 鍛造中
		GAME_RESULT,	// 結果表示(完成)
		// ※失敗(廃件/GAME_OVER)は就職版では設けない=全員が最後まで鍛造を体験できる様に(§8 決定事項5)。
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

	//--- 状態ごとのUI
	void DrawTitleUI();
	void DrawPlayUI();
	void DrawResultUI();
	void DrawParchmentPanel(float yCenter, float heightRatio);	// 結果画面の下地(羊皮紙)

	//--- ゲーム進行
	void StartGame();	// タイトル → 鍛造開始
	void FinishGame();	// 鍛造完了 → 結果へ

	// KCD式: 「叩く場所」は指示しない。誤打(冷打/過熱/完成済みの段)は「検知して独白で知らせる」だけ。
	//   罰(廃件率の累積→失敗)は無し。検知ロジックは DoStrike の StrikeOutcome 分岐に残す。
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
	std::unique_ptr<GrindStep>  m_grindStep;
	std::unique_ptr<QuenchStep> m_quenchStep;
	void SetupSteps();								// 各状態を生成し m_stepMachine へ登録(Init で一度)

	//--- 工位(Station): 金床/炉/砥石/水槽。走動中に物件を見て E で入る。工程ごとに使う工位は
	//    StepStation(工程) が決める(データ)。刃は「最後に入った工位」に置かれ、工位を出ると手に持つ。
	Station m_station  = Station::Anvil;			// 今(または移動アニメの到着先で)作業している工位
	Station m_workAt   = Station::Anvil;			// 刃が置かれている工位(開始時は金床の上)
	bool    m_carrying = false;						// 工位を出て刃を手に持って歩いている(=描かない・炉で熱されない)
	DirectX::XMFLOAT3 m_stationViewDir = { 0, 0, -1 };	// 工位(金床以外)の視点方向=入った時に玩家が居た側(水平単位)
	float m_stationCamDist   = 1.00f;				// 工位カメラ: 作業点から手前へ離れる水平距離
	float m_stationCamHeight = 0.75f;				// 工位カメラ: 作業点からの目の高さ
	float m_stationLookLift  = 0.00f;				// 工位カメラ: 注視点の高さ補正
	float m_hearthLift  = 0.04f;					// 炉: 炭床の上に刃を置く高さ
	float m_grindLift   = 0.02f;					// 砥石: プロップ上端から刃を置く高さ
	float m_troughHover = 0.30f;					// 水槽: 水面の上に刃を構える高さ
	DirectX::XMFLOAT3 StationBase(Station s);		// 工位の作業点(刃を置く点。砥石の滑り/淬火の沈みは含まない)
	DirectX::XMFLOAT3 StationRight() const;			// 工位カメラから見た右方向(刃の長軸をこれに揃える)
	float StationAlignYaw() const;					// 刃の長軸を StationRight に揃える追加 yaw(金床では 0)
	void  StationView(Station s, DirectX::XMFLOAT3& eye, DirectX::XMFLOAT3& target);	// 工位の視点と注視点
	void  ApplyWorkCamera();						// 金床以外の工位の固定カメラ

	//--- 炉(加熱)。ニュートンの冷却(加熱)則: 鉄の温度は「火の温度」へ指数的に近づく。
	//      dT/dt = k * (T_fire - T)   →  1フレームの厳密解: T += (T_fire - T) * (1 - exp(-k*dt))
	//    炭火だけ = 燃える温度(BURN_TEMP)と過熱(OVERHEAT)の間で止まる=放っておいても焼けない。
	//    風箱(R長押し) = 火が熱くなり(T_fire↑)、速く(k↑)近づく=早いが、踏みすぎると過熱する。
	static constexpr float COAL_FIRE_TEMP    = 0.87f;	// 炭火だけの火の温度(燃える〜過熱の間)
	static constexpr float BELLOWS_FIRE_TEMP = 1.00f;	// 風箱で煽った火の温度(白熱。過熱を越える)
	static constexpr float COAL_HEAT_K       = 0.25f;	// 炭火だけの熱の入り方(1/秒)。0→燃えるまで約10秒
	static constexpr float BELLOWS_HEAT_K    = 0.60f;	// 風箱を踏んでいる時(1/秒)。0→燃えるまで約3秒
	bool  m_overheatWarned = false;					// 過熱の独白を一度だけ出す(冷めたら再武装)

	//--- 鋼が燃える(火花を噴く)演出。温度 >= ForgingSim::BURN_TEMP の間、刃の表面から火花を出す。
	float m_burnSparkAcc = 0.0f;					// 端数の火花数を次フレームへ持ち越す
	bool  m_burnSndOn    = false;					// 燃焼ループ音が鳴っているか
	static constexpr float BURN_SPARK_RATE  = 30.0f;	// 1秒あたりの火花数
	static constexpr float BURN_SPARK_POWER = 0.30f;	// 火花の勢い(打撃の火花より弱い=表面から弾ける程度)
	static constexpr float BURN_SPARK_SCALE = 0.45f;
	void  UpdateBurnFx(float tick);					// 燃焼の火花+音
	DirectX::XMFLOAT3 RandomBladePoint() const;		// 刃の上のランダムな点(ワールド。火花の発生点)

	//--- 研磨(砥石)。右クリックを「点按」=足踏み1回。左長押し=刃を押し当てる。マウス左右=刃を滑らす。
	GrindWheel m_wheel;								// 足踏み砥石の回転物理(力積+指数減衰)
	float m_grindU      = 0.5f;						// 砥石に当たっている刃の長手位置 0..1(区域 = U*NSEG)
	float m_grindPress  = 0.0f;						// 押し当ての見た目 0..1(Damp)
	float m_grindSens   = 0.0012f;					// マウス1pxあたりの滑り量(長手の割合)
	float m_grindSparkAcc = 0.0f;
	bool  m_grindSndOn  = false;
	static constexpr float GRIND_RATE         = 0.35f;	// 全速で押し当てた時の研ぎ進み(/秒)
	static constexpr float GRIND_PRESS_DROP   = 0.02f;	// 押し当てで刃が砥石へ沈む量
	static constexpr float GRIND_PRESS_LAMBDA = 14.0f;	// 押し当ての追従の速さ(Damp率)
	static constexpr float GRIND_SPARK_RATE   = 90.0f;	// 全速時の研ぎ火花(個/秒)
	static constexpr float GRIND_SPARK_POWER  = 0.55f;
	static constexpr float GRIND_SPARK_SCALE  = 0.35f;
	void  UpdateGrind(float tick, bool inputOn);	// 研磨の入力・物理・火花・音

	//--- 淬火と終幕(QuenchStep が進みを渡し、ここが見た目/音を担当)
	float m_plunge    = 0.0f;						// 刃が水へ沈んだ割合 0..1
	float m_letterbox = 0.0f;						// 上下黒帯の入り具合 0..1
	float m_steamTimer = 0.0f;						// 蒸気の残り時間(秒)
	static constexpr float QUENCH_MIN_TEMP  = 0.55f;	// これ未満では淬火できない(焼きが入らない)
	static constexpr float QUENCH_COOL_RATE = 1.2f;		// 水中での急冷(/秒)
	static constexpr float PLUNGE_DEPTH     = 0.40f;	// 構え位置から沈む深さ(水面の下まで)
	static constexpr float STEAM_DURATION   = 3.0f;		// 蒸気が出続ける時間(秒。だんだん弱まる)
	static constexpr float STEAM_RATE       = 140.0f;	// 淬火直後の蒸気の発生数(個/秒)
	static constexpr float STEAM_RADIUS     = 0.18f;	// 蒸気が湧く水面の円の半径
	static constexpr float LETTERBOX_RATIO  = 0.12f;	// 黒帯1本の最大の高さ(画面高さ比)
	void  DrawSteam();								// 蒸気を柔らかいビルボードで描く
	void  DrawLetterbox();							// 上下の黒帯(映画的な終幕)

	//--- 主人公の独白(負向フィードバック)。打撃ポップアップと同じ枠を使う。
	void  Say(const char* text, unsigned int col);

	//--- 温度(0=冷たい 〜 1=白熱)は鉄そのものの状態なので m_forging(ForgingSim)が所有する。
	//    読むのは m_forging.Heat()。玩家がどこに居ても(走動中も)自然冷却が進む。

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
	bool  ReadMouseDelta(float& dx, float& dy);	// 光標のクライアント中心からの偏移を読み、中心へ戻す(look/翻面で共用)
	void  UpdateAim();				// 準心射線を板と交差させ m_aimI/J/World を更新

	//--- 一人称の「走動モード」(工位に着く前に工坊を歩く) ---
	// 二模式切替: 走動中=ApplyWalkCamera(玩家目線), 工位=ApplyCamera(調校済みの鍛造framing)。
	Player m_player;					// 歩き回るプレイヤ本体(位置/向き/速度/可互動を持つ)
	bool   m_walkMode   = false;		// true=走動モード / false=工位(鍛造)モード。E互動で工位へ入る
	float  m_walkPitch  = 0.0f;			// 走動カメラの上下(pitch)累積。左右(yaw)は m_player が持つ
	float  m_walkFloorY = 0.0f; // Stage floor elevation, loaded from forge_tuning.txt.
	float  m_walkEyeH   = 1.6f;			// 目線の高さ(玩家足元からカメラまで, 単位)
	float  m_walkSens   = 0.0017f;		// 走動時マウス感度(rad/px)。UpdateMouseLook と同値で統一
	float  m_walkPitchLim = 1.3f;		// 上下視角の制限(rad)≒74°。真上/真下でひっくり返るのを防ぐ
	float  m_walkSpeed  = 3.0f;			// 走動速度(単位/秒)。毎フレーム m_player へ渡す(F1で調整)
	void   UpdateWalkLook();			// 走動時: マウスを玩家yaw(左右)とカメラpitch(上下)へ
	void   ApplyWalkCamera();			// 走動時: カメラを玩家の目線に置く一人称カメラ

	//--- 走動 ⇔ 工位 の過渡(移動アニメ)。E で入る/E・ESC で出る。取り消し不可(入力は捨てる)。
	//    カメラを「開始時に画面に映っていた視点」から「到着先の視点」へ補間する。
	//    位置は線形補間、向きは yaw/pitch 角で補間(ベクトルの線形補間だと真後ろ向き時に潰れて跳ぶ)。
	//    到着先は毎フレーム実際のカメラ関数(ApplyCamera/ApplyWalkCamera)から取る=着いた瞬間に画が跳ばない。
	enum class ModeTrans { None, Enter, Exit };
	ModeTrans m_modeTrans = ModeTrans::None;
	float  m_transTimer = 0.0f;				// 経過秒
	float  m_transDur   = 1.0f;				// 今回の長さ(秒)=距離 / m_transSpeed を MIN..MAX に収める
	DirectX::XMFLOAT3 m_transFromEye = { 0,0,0 }, m_transFromFwd = { 0,0,1 };	// 開始時の視点(スナップショット)
	float  m_transSpeed    = 2.0f;			// 過渡の移動速さ(単位/秒)。遠いほど長くなる
	float  m_exitStepBack  = 0.6f;			// 退出時、工位の視点から金床と反対側へ下がる距離(=一歩)
	static constexpr float TRANS_MIN_TIME = 1.0f;	// 過渡の最短(秒。近くても一瞬で飛ばない)
	static constexpr float TRANS_MAX_TIME = 2.0f;	// 過渡の最長(秒。遠くても待たせすぎない)
	bool   Transitioning() const { return m_modeTrans != ModeTrans::None; }
	void   BeginEnterStation(Station s);	// 走動→工位の過渡を開始(刃もその工位へ置く)
	void   BeginExitStation();			// 工位→走動の過渡を開始(玩家を工位の一歩手前へ置き、刃を手に持つ)
	void   UpdateModeTrans(float tick);	// 計時を進め、終わったら walkMode を切り替える
	void   ApplyViewCamera();			// 今の状態のカメラを適用(過渡中は補間、他は走動/工位)
	void   ApplyTransCamera();			// 過渡中のカメラ(開始視点→到着先の補間)
	void   TargetViewPose(DirectX::XMFLOAT3& eye, DirectX::XMFLOAT3& fwd);	// 過渡の到着先の視点を計算
	float  TransDuration();				// 開始視点→到着先の距離から過渡の長さを決める

	//--- 走動中の互動(インタラクト, SceneForge/Interaction.cpp)。
	//    2つの判定が両方 true の物件だけ「E」提示を出し、E で互動できる:
	//      ①範囲: 玩家の足元が物件の「互動範囲の箱」(物件のワールドAABBを水平に reach だけ広げた箱)の中
	//      ②視線: 目線の射線が物件の箱(m_lookPad だけ膨らませた)に当たる(照準と同じ AimSystem::Raycast)
	//    物件と行為の対応は表 INTERACTABLES(データ)。新しい互動は表に1行足す+行為を1つ書くだけ。
	enum class InteractAction { EnterStation, TakeTongs };
	struct Interactable
	{
		const char*    propKey;	// どのプロップか(配置は stage_layout.txt に従う=箱も自動で追従)
		InteractAction action;	// E で何をするか
		Station        station;	// EnterStation の時に入る工位(TakeTongs では金床)
		float          reach;	// 互動範囲: 物件の箱から水平にどこまで離れても届くか(単位)
	};
	static const Interactable INTERACTABLES[];
	static const int          NUM_INTERACTABLES;
	int   m_focus = -1;							// 今 E で互動できる物件(INTERACTABLES の番号)。-1=無し
	DirectX::XMFLOAT3 m_promptPoint = { 0,0,0 };	// 提示を出すワールド点(最後に注視した物件の中心)
	float m_promptAlpha  = 0.0f;				// 提示のフェード 0..1(出る/消えるをなめらかに)
	float m_promptLambda = 12.0f;				// フェードの速さ(Damp率, 1/秒)
	float m_lookPad      = 0.12f;				// 視線判定の箱を膨らませる量(細い火钳でも狙える様に)
	bool  m_pendingFlip  = false;				// 火钳を取って工位へ移動中=着いたら翻面(火钳待命)から始める
	bool  InteractEnabled(const Interactable& it) const;	// 今この互動ができる状況か(例: 砥石は研磨工程だけ)
	bool  PropWorldBox(Prop& p, DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx);	// プロップのワールドAABB
	void  UpdateInteract(float tick);			// 2判定→m_focus を決める(走動中のみ)
	void  DoInteract(const Interactable& it);	// E を押された物件の行為を実行
	void  DrawInteractPrompt();					// 物件の上に「E」ボタンを描く(HUD)
	void  DrawInteractBoxes();					// F1: 互動範囲の箱を線で表示(範囲内=緑)

	//--- F1調整値の永続化(Assets/forge_tuning.txt)。Initで読み, Uninit/Saveボタンで書く。
	void  LoadTuning();
	void  SaveTuning();

	//--- 起動時スナップショット(メモリのみ・ファイルは触らない)。
	// 全ての可調値の「アドレス」を一覧にし(TuningRefs=列挙は1箇所だけ)、
	// Init 完了時に現在値を m_tuneStartup へコピー(Snapshot)。F8/ボタンでコピーし戻す(Restore)。
	// 目的: 調整で滅茶苦茶にしても起動時の状態へ一発で戻せる(存档は汚さない)。
	void  TuningRefs(std::vector<float*>& out);	// 可調値のアドレス表(唯一の列挙点)
	void  SnapshotTuning();						// 現在値 → m_tuneStartup へ保存
	void  RestoreTuning();						// m_tuneStartup → 現在値へ復元
	std::vector<float> m_tuneStartup;			// 起動時の全可調値のコピー

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
	// 自然冷却速度は鉄の物理なので m_forging.coolRate に移した。

	//--- 武器モーフ(Blenderで作った同拓扑の各段FBXを頂点補間して成形する) ---
	// uv は真の鋼テクスチャ採样用。morphでUVは不変なので stage0 の値を全段で使う。
	// フィールド順は VS_Wp の VIN 宣言順(pos→nrm→uv→col→sharp)と一致させること(入力レイアウトが宣言順で焼かれる)。
	// sharp = 研いだ刃先の度合い 0..1(=その区域の鋭さ × 刃先への近さ)。PS が研ぎ面の見た目に使う。
	struct WpVtx { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 nrm; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 col; float sharp; };
	struct WpStage { std::vector<DirectX::XMFLOAT3> pos, nrm; std::vector<DirectX::XMFLOAT2> uv; };	// 1段分の生頂点(ローカル)
	std::vector<WpStage>        m_wpStage;		// stage_0 .. stage_final
	std::vector<unsigned int>   m_wpIdx;		// インデックス(全段共通)
	std::vector<WpVtx>          m_wpVtx;		// 補間後の頂点(毎フレーム再構築)
	std::shared_ptr<MeshBuffer> m_wpMesh;
	std::shared_ptr<Texture>    m_wpTex;		// 真の鋼テクスチャ(BaseColor=冷鋼の地色)。発光は温度(m_forging.Heat())駆動
	int   m_wpN = 0;							// 1段の頂点数
	bool  m_wpOk = false;						// 読み込み成功&段間で頂点数一致
	float m_forgeProg = 0.0f;					// 全体進捗 0..1(=各区域の平均。F1のプレビュー用)
	//--- 分区域進度は m_forging が所有(ForgingSim::NSEG / SegProg / SegDone / AllSegmentsDone)。
	int   m_aimSeg = 0;							// 現在照準している区域(AimSystemが更新)
	// 照準している区域番号。AimSystem(射線×区域ボックス)が決めた値をそのまま返す。
	int   AimSeg() const { return m_aimSeg; }
	DirectX::XMFLOAT3 m_wpMin = { 0,0,0 }, m_wpMax = { 0,0,0 };	// stage0のローカルAABB(配置用)
	DirectX::XMFLOAT3 m_wpFinMin = { 0,0,0 }, m_wpFinMax = { 0,0,0 };	// 完成形のローカルAABB(刃先=幅方向の端の判定用)
	//--- 研ぎの形: 刃先(幅方向の端)の頂点ほど、鋭さに応じて厚みを中心面へ寄せる=刃が薄く立つ。
	static constexpr float EDGE_BAND_START = 0.55f;	// 幅の正規化座標でここから外側を「刃先」とみなす(0=中心,1=端)
	static constexpr float EDGE_THIN       = 0.65f;	// 研ぎ上がりで刃先の厚みを減らす割合
	//--- 両面の形の分解(軸分解モーフ): 輪郭(長さ/幅)は両面で共有=両面進捗の平均、
	//    厚み方向だけは各面が自分の進捗で動く(叩いた面だけ刃の斜面が付く)。
	int   m_wpThickAxis = -1;					// 刃の表裏を貫くローカル軸(0=x,1=y,2=z)。Loadで完成形から判定
	static constexpr float FACE_BLEND_BAND = 1.0f;	// 表/裏の面の混ぜ幅(厚みの正規化座標)。小=境目が急
	//--- 配置調整(F1スライダ。向き/大きさをここで合わせて焼き込む)
	float m_wpScale = 1.0f;						// 追加スケール倍率(AABBフィットにさらに掛ける)
	float m_wpYaw = 0.0f, m_wpPitch = 0.0f, m_wpRoll = 0.0f;	// 向き(0=前後/屏幕奥行き。90°で左右横向き)
	float m_wpOff[3] = { 0.0f, 0.0f, 0.0f };	// 砧面アンカーからの微調整
	//--- 翻面(裏返し)の見た目: 長軸まわりに 0→180°を回転。
	float m_flipAngle = 0.0f;					// 現在の回転角(rad)。0=表が上, π=裏が上
	static constexpr float FLIP_TURN_LAMBDA = 8.0f;	// 定面後、清潔な角(0/π)へ落ち着く速さ(Damp率, 1/秒)

	//--- 翻面の子状態機(鍛打工程の内部。玩家が随時 F で起動)。命名 enum + switch(軽量な下層FSM)。
	//    None=鍛打中 / TongsOut=火钳を取り出す運鏡(約2.5s) / Ready=火钳待命(F=戻す, 左键=夹む)
	//    Gripping=铁を夹む運鏡(約1s) / Flipping=マウス左右で铁を翻す(左键=面を確定) / PutBack=火钳を戻す運鏡
	//    入力の規則: 運鏡ビート(TongsOut/Gripping/PutBack)は「再生中の動画」=取り消し不可。
	//    その間のキー/マウスは読んで捨てる(後で暴発しない)。入力を受けるのは Ready/Flipping だけ。
	enum class FlipPhase { None, TongsOut, Ready, Gripping, Flipping, PutBack };
	FlipPhase m_flipPhase = FlipPhase::None;
	float m_flipTimer = 0.0f;					// 運鏡ビート(TongsOut/Gripping/PutBack)の経過秒
	void  UpdateFlip(float tick, bool inputOn);	// 翻面子状態機を駆動(運鏡ビート＋マウス翻し)
	bool  FlipIsCutscene() const;				// 今が取り消し不可の運鏡ビートか(入力を捨てる区間)
	static constexpr float FLIP_TONGS_OUT_TIME = 2.5f;	// 火钳を取り出す運鏡の長さ(秒。慢=映画的)
	static constexpr float FLIP_GRIP_TIME      = 1.0f;	// 铁を夹む運鏡の長さ(秒)
	static constexpr float FLIP_PUTBACK_TIME   = 2.0f;	// 火钳を戻す運鏡の長さ(秒)
	//--- 翻す手感(F1「Flip」で調整・forge_tuning.txt に保存)
	float m_flipSens     = 0.0002f;				// マウス横移動→刃の回転(1pxあたり何半回転, 1:1 raw)。小=大きく振らないと回らない=重さ

	//--- 翻面の運鏡(火钳アニメの代替)。カメラは2つの「寄り」を重み付きで混ぜる:
	//    tongs=火钳(StPliers プロップ)の方へ振り向く / grip=刃(砧面アンカー)へ寄って見下ろす。
	//    目標点はプロップ/アンカーから取る=配置を変えても運鏡が自動で追従(座標のベタ書き無し)。
	float m_camTongsW = 0.0f;					// 火钳方向への重み 0..1(TongsOut/PutBack の曲線で決まる)
	float m_camGripW  = 0.0f;					// 刃への寄りの重み 0..1(Gripping で上がり、Ready で戻る)
	bool  m_tongsInHand = false;				// 火钳を手に取っている(=台上の火钳モデルを隠す)
	float m_tongsLean = 0.20f;					// 火钳へ振り向く時、カメラ本体も寄る割合(体を傾ける)
	float m_gripDolly = 0.30f;					// 夹む時、カメラ本体が刃へ寄る割合
	float m_gripLambda = 5.0f;					// 寄り→元の視点へ戻る速さ(Damp率, 1/秒)
	//--- 翻面中はハンマーを置く(火钳に持ち替える)。重み 0=構え / 1=置いた。
	//    TongsOut の「振り向く」区間で置き、PutBack の「戻る」区間で取り上げる(火钳運鏡と同じ時間割)。
	float m_hammerStowW = 0.0f;
	float m_hammerStowOff[3] = { 0.45f, -0.55f, -0.25f };	// 置いた位置=構え位置からのずれ(F1「Flip」で調整)
	float m_hammerStowTilt = 1.2f;				// 置く時に寝かせる角度(rad。錘頭の pitch に加算)
	// 火钳運鏡の時間割(ビート長に対する比率 0..1): [0,REACH)=振り向く / [REACH,RETURN)=手に取る(静止) / [RETURN,1]=戻る
	static constexpr float FLIP_REACH_FRAC  = 0.35f;
	static constexpr float FLIP_RETURN_FRAC = 0.55f;
	void  UpdateFlipCamera(float tick);			// 翻面の段階から運鏡の重みを更新(UpdateFlipの後)
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
	DirectX::XMMATRIX WeaponWorld();			// 武器ローカル→ワールドのフィット変換(照準/描画で共用)
	DirectX::XMMATRIX WeaponSpin() const;		// 翻面回転(長軸まわりに m_flipAngle)。WeaponWorld と法線変換で共用
	DirectX::XMMATRIX WeaponRot() const;		// 刃の回転部分(翻面×向き付け×工位の揃え)。法線変換用
	DirectX::XMFLOAT3 WorkAnchor();				// 刃を置く点(工位の作業点 + 砥石の滑り/押し当て + 淬火の沈み)
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
		bool              hidden = false;		// 一時的に描かない(例: 火钳を手に取っている間の台上の火钳)
		DirectX::XMFLOAT3 aabbMin = { 0,0,0 };	// モデル空間AABB(Loadでキャッシュ)
		DirectX::XMFLOAT3 aabbMax = { 0,0,0 };
	};
	std::vector<Prop> m_props;
	float m_groundY = 0.0f;	// 床の高さ(金床のワールドAABB下面から算出)
	bool  m_showScenery = true;
	//--- 石墙(AI生成FBX)専用PBR: UVが壊れているため triplanar(Box投影)で Poly Haven の実PBR貼图を worldPos から投影する
	void  DrawWall(Model* m, const DirectX::XMMATRIX& world);	// 石墙専用描画(VS_Wall/PS_Wall)

	//--- Unity風のドラッグ配置エディタ(F1中に選択したプロップを地面上でLMBドラッグ移動)
	int   m_editSel     = -1;		// 選択中のプロップindex(-1=なし)
	bool  m_editDragging = false;
	float m_editPrevX = 0.0f, m_editPrevY = 0.0f;
	void  UpdateEditorDrag();	// LMBドラッグで選択プロップを地面移動

	//--- 自作の光る炭ベッド(FBXに頼らず、狙った位置に確実に炭火を出す。明滅する)
	std::shared_ptr<MeshBuffer> m_coalBedMesh;	// 低ポリの炭塊群(CoalBedMesh::Create)。隙間だけ発光
	bool  m_coalOn     = true;
	float m_coalPos[3] = { 3.20f, 0.55f, 1.80f };	// 炉の火床の位置(F1で合わせる)
	float m_coalYaw    = 0.0f;
	float m_coalSize[2]= { 0.55f, 0.75f };	// 板の半径(X,Z)
	float m_coalGlow   = 1.8f;				// 明るさ(Bloomで光る)
	void  DrawCoalBed();	// 光る炭ベッドを描画

	//--- 水槽の水面(真の屈折。背後のシーンをスナップショットして透ける)
	//    メッシュは ±1 の水平板(両面)。world で位置/大きさを与え、PS_Water で描く。
	std::shared_ptr<MeshBuffer> m_waterMesh;
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

	//--- 温度パラメータ(加熱速度は上の COAL_HEAT_RATE / BELLOWS_HEAT_RATE)
	// 打撃CDは調整しやすいようメンバー変数(m_strikeCDMax)。自然冷却速度は m_forging.coolRate
	static constexpr float IDEAL_MIN = 0.55f;	// 最適温度帯(下限)
	static constexpr float IDEAL_MAX = 0.85f;	// 最適温度帯(上限)
	static constexpr float OVERHEAT  = 0.92f;	// これ以上は過熱(鋼を痛める)
	// 炭火だけの温度は「燃える」と「過熱」の間でなければならない(=放置で燃え始め、しかし焼けない)。
	// 調整で崩したらコンパイルエラーで気付ける様にする。
	static_assert(COAL_FIRE_TEMP > ForgingSim::BURN_TEMP && COAL_FIRE_TEMP < OVERHEAT,
	              "COAL_FIRE_TEMP must lie between BURN_TEMP and OVERHEAT");

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

	//--- 結果評価(S/A/B/C): 完成度・打撃品質を1本の「出来栄え」0..1へ合成し、閾値で等級化。
	//    「注定成形」ゲームなので形は必ず完成に近づく→評価は「どれだけ綺麗に打てたか」を主にする。
	float GradeScore() const;		// 出来栄え 0..1(=形の一致・打撃品質の合成)
	char  GradeLetter() const;		// GradeScore を S/A/B/C に量子化
	static constexpr float GRADE_W_MATCH   = 0.45f;	// 出来栄えに占める「形の一致度」の重み
	static constexpr float GRADE_W_QUALITY = 0.55f;	// 同「打撃品質の平均」の重み(綺麗な打鉄を主に評価)
	static constexpr float GRADE_S = 0.90f;	// この出来栄え以上で S
	static constexpr float GRADE_A = 0.75f;	// 〃 A
	static constexpr float GRADE_B = 0.55f;	// 〃 B (未満は C)
};

#endif // __SCENE_FORGE_H__

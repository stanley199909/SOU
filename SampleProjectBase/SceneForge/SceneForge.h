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
#include "WaterSim.h"		// Physics: 水槽の水面の波(2D 波動方程式。自作物理)
#include "Player.h"			// 鍛冶場を歩き回るプレイヤ(一人称の走動)
#include "Collision2D.h"	// Physics: 走動の衝突(上から見た2D。円×凸包/壁線)
#include "HingedDoor.h"		// GameLogic: 蝶番で開閉する扉(状態と角度)
#include "CottageDoor.h"	// 整屋の裏口の扉(データ+蝶番の幾何)
#include "InteractionMap.h"	// 真上から正射影で見た「最近押された所」の貼图(草の倒れ等)
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
	void  StirQuench(float dt);					// 淬火: 揺する入力(マウス上下)を読み、刃を動かす(QuenchStep の揺する段階)
	float QuenchProgress() const;				// 淬火の完成度 0..1 = 入水時の温度から沸騰が止む温度までの冷え具合(100% で終幕)
	bool  QuenchStirring() const;				// 今、揺する段階か(入水〜100%。マウスを視点でなく揺すりに使う)
	void  SetQuenchTurn(float t01);				// 淬火: 刃を立てる(刃を下へ)進み 0..1(QuenchStep が時間で駆動)
	void  SetPlunge(float t01);					// 淬火: 刃が水へ沈む進み 0..1(同上)。刃が水面に触れた瞬間に音と蒸気
	void  BeginFinale();						// 終幕へ: 今の工程が配方の最後なら m_clearDecided を立てる(QuenchStep が揺すり始めに呼ぶ)
	bool  m_clearDecided = false;				// クリアが確定した(最後の工程の取り消せない演出に入った)。true の間 HUD を全部消す
	void  SetLetterbox(float t01);				// 終幕: 上下の黒帯が入る進み 0..1(同上)
	void  AdvanceStep();						// 次の工程へ進む(配方の順序で遷移。無ければ完成)
	void  DebugJumpToStep(int idx);				// 【デバッグ】配方の idx 番目の工程から始める(前の工程は済ませた状態で、その工位に立つ。F1 最上段)
	int   m_debugJumpIdx = 0;					// F1 で選んでいる工程
	const StepSetting& CurrentStep() const;		// 今実行中の工程設定(HUD が instruction を表示)
	bool  StepReached(StepName type) const;		// 配方でその種類の工程まで進んだか(今その工程 or もう済んだ)

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
	void  DrawHammer3D();		// 3Dハンマー(蓄力で上がり打撃で振り下ろす。金床を離れたら右腰)
	DirectX::XMMATRIX HammerWorld(Model* hammer, bool withRecoil = true);	// 金床で構える/打つハンマーの行列(描画と火花で共用。火花は反冲無しの落ちた所)
	DirectX::XMMATRIX HammerHipWorld(Model* hammer);	// 右腰に下げたハンマーの行列
	DirectX::XMFLOAT3 HammerStrikePoint();				// ハンマーの頭の真下の鉄の上面(打撃の火花の出る所)
	DirectX::XMFLOAT3 m_lastStrikeOrigin = { 0, 0, 0 };	// 最後に打撃の火花が出た点(F1 で十字を描く)
	DirectX::XMFLOAT3 m_hammerHeadLocal = { 0, 0, 0 };	// ハンマーの頭の中心(モデル空間。初回に頂点から求めて覚える)
	bool  m_hammerHeadReady = false;
	DirectX::XMFLOAT3 FindHammerHeadLocal(Model* hammer);	// 長軸の両端で断面の広い方 = 頭
	bool  HammerOnHip() const;							// 右腰に下げているか(金床で作業していない時)
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
	std::shared_ptr<Texture>    m_uiFrame;		// UI: 飾り枠付き羊皮紙(工程リストの下地。ナインスライスで描く)
	std::shared_ptr<Texture>    m_gaugeFrame;	// UI: 温度ゲージの金属外框(槽の暗い地を含む=一番下の層)
	std::shared_ptr<Texture>    m_gaugeOverlay;	// UI: 槽をくり抜いた外框(一番上の層。金の縁が範囲の紋理の端を覆う)
	std::shared_ptr<Texture>    m_gaugeMarker;	// UI: 温度ゲージの指針(表示サイズへ縮小済み)
	std::shared_ptr<Texture>    m_gaugeIdeal;	// UI: 適温帯の紋理(範囲は IDEAL_MIN..IDEAL_MAX を程序で切り出す)
	std::shared_ptr<Texture>    m_gaugeOver;	// UI: 過熱帯の紋理(OVERHEAT..1)

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
	bool    m_carrying = false;						// 刃を火钳で掴んで手に持っている(カメラの前に描く・炉で熱されない)。Carry.cpp
	DirectX::XMFLOAT3 m_stationViewDir = { 0, 0, -1 };	// 工位(金床以外)の視点方向=入った時に玩家が居た側(水平単位)
	float m_stationCamDist   = 1.00f;				// 工位カメラ: 作業点から手前へ離れる水平距離
	float m_stationCamHeight = 0.75f;				// 工位カメラ: 作業点からの目の高さ
	float m_stationLookLift  = 0.00f;				// 工位カメラ: 注視点の高さ補正
	float m_hearthLift  = 0.04f;					// 炉: 炭床の上に刃を置く高さ(差し込んだ切っ先の高さ)
	// 炉は炭床に平らに、斜めに寝かせる(先端は炭床の奥、掴んだ端は炉口から斜め手前へはみ出す。ユーザー指定 2026-10-05)。
	float m_hearthYaw      = 0.27925f;					// 炉: 炭床の長辺から奥へ振る角(rad, 水平面)。F1 Stations
	float m_hearthTipSide  = 0.80f; 					// 炉: 先端が炭床の中心から長辺方向へずれる距離(m)
	float m_hearthTipDepth = 0.28f;					// 炉: 先端が炭床の中心から奥へ入る距離(m)
	DirectX::XMFLOAT3 HearthDir() const;			// 炉に寝かせた鉄の長軸(水平。+側=奥の先端)
	float m_grindLift   = 0.0f;						// 砥石: 刃の一番低い点と砥石の上端の隙間(m)。刃の高さ自体は BladeDepthBelowCentre で自動
	float BladeDepthBelowCentre() const;			// 刃の中心から一番低い点までの深さ(今の回転で。砥石に食い込まない高さを出す)
	float m_troughHover = 0.30f;					// 水槽: 水面の上に刃を構える高さ
	DirectX::XMFLOAT3 StationBase(Station s);		// 工位の作業点(刃を置く点。砥石の滑り/淬火の沈みは含まない)
	DirectX::XMFLOAT3 StationRight() const;			// 工位カメラから見た右方向(刃の長軸をこれに揃える。炉だけは奥へ向ける)
	DirectX::XMFLOAT3 StationInto() const { return DirectX::XMFLOAT3(-m_stationViewDir.x, 0.0f, -m_stationViewDir.z); }	// 工位の奥へ向かう水平方向
	// 砥石だけは「近づいた側」でなく砥石自身の向きから視点を決める(どこから入っても刃の置き方が同じ)。
	//   視点 = 輪の軸に直交する水平方向 → 刃の長軸(StationRight)は輪の軸に沿う=刃が輪の縁を横切る置き方。
	bool  m_hearthFrontFlip = false;				// 炉の正面の自動判定(SetupStationView)を逆にする。F1 Stations
	bool  m_troughFrontFlip = false;				// 水槽の〃(前後対称なら近づいた側。それを逆にする)
	static constexpr float STATION_FRONT_MIN_OFFSET = 0.05f;	// 道具の中心と作業点がこれ(m)以上ずれていたら「正面がある」とみなす
	float m_grindViewYaw = 0.0f;					// その視点方向に足す回転(rad)。反対側から見たい時は π。F1 Stations
	DirectX::XMFLOAT3 GrindViewDir();				// 砥石の固定視点方向(水平単位)
	float StationAlignYaw() const;					// 刃の長軸を StationRight に揃える追加 yaw(金床では 0)
	void  StationView(Station s, DirectX::XMFLOAT3& eye, DirectX::XMFLOAT3& target);	// 工位の視点と注視点
	void  ApplyWorkCamera();						// 金床以外の工位の固定カメラ
	float ShakeOffsetY() const;						// 衝撃の縦揺れ(m_shake の減衰振動。金床カメラと工位カメラで共用)

	//--- 炉(加熱)。ニュートンの冷却(加熱)則: 鉄の温度は「火の温度」へ指数的に近づく。
	//      dT/dt = k * (T_fire - T)   →  1フレームの厳密解: T += (T_fire - T) * (1 - exp(-k*dt))
	//    炭火だけでも火は過熱(OVERHEAT)より熱い=炉に置いたままなら上がり続け、いずれ過熱する(現実と同じ。取り出すのは玩家)。
	//      旧: 炭火を 0.87(過熱の手前)にしていた → 炉が勝手に温度を保つ様に見えた(2026-10-08 ユーザー指摘で修正)。
	//    風箱(R長押し) = 火が更に熱くなり(T_fire↑)、速く(k↑)近づく=早いが、すぐ過熱する。
	static constexpr float COAL_FIRE_TEMP    = 0.98f;	// 炭火だけの火の温度(過熱より上)
	static constexpr float BELLOWS_FIRE_TEMP = 1.00f;	// 風箱で煽った火の温度(白熱)
	static constexpr float COAL_HEAT_K       = 0.12f;	// 炭火だけの熱の入り方(1/秒)。0→燃える(0.80)まで約14秒、→過熱(0.92)まで約23秒
	static constexpr float BELLOWS_HEAT_K    = 0.60f;	// 風箱を踏んでいる時(1/秒)。0→燃えるまで約3秒
	bool  m_overheatWarned = false;					// 過熱の独白を一度だけ出す(冷めたら再武装)

	//--- 鋼が燃える(火花を噴く)演出。温度 >= ForgingSim::BURN_TEMP の間、刃の表面から火花を出す。
	float m_burnSparkAcc = 0.0f;					// 端数の火花数を次フレームへ持ち越す
	bool  m_burnSndOn    = false;					// 燃焼ループ音が鳴っているか
	static constexpr float BURN_SPARK_RATE  = 30.0f;	// 1秒あたりの火花数
	void  UpdateBurnFx(float tick);					// 燃焼の火花+音
	DirectX::XMFLOAT3 RandomBladePoint() const;		// 刃の上のランダムな点(ワールド。火花の発生点)

	//--- 研磨(砥石)。右クリックを「点按」=足踏み1回。左長押し=刃を押し当てる。マウス左右=刃を滑らす。
	GrindWheel m_wheel;								// 足踏み砥石の回転物理(力積+指数減衰)
	float m_grindU      = 0.5f;						// 砥石に当たっている刃の長手位置 0..1(区域 = U*NSEG)
	float m_grindSens   = 0.0012f;					// マウス1pxあたりの滑り量(長手の割合)
	//--- 研ぎ角(bevel angle。ユーザー同意 2026-10-07): マウスの上下で刃を長軸まわりに傾ける。
	//    実際の刃付け: 刃は決まった角度で砥石に当てる。寝かせすぎ = 刃でなく平らな面を削る / 立てすぎ = 刃先が丸まる。
	//    角度が正しいほど速く研げ、火花も多い(正しい角度は画面に出さない。火花・研げ具合・独白で分かる)。
	//    研ぐ面は F で裏返して選ぶ(鍛造の F と同じ操作。ユーザー指定 2026-10-07)。両方研ぎ上がって研磨完了。
	float m_grindAngle       = 0.0f;				// 今の傾き(rad, 0..MAX)。0 = 平らに寝ている
	int   m_grindFace        = 0;					// 今研いでいる面(0 = 表 / 1 = 裏)。F で切り替え
	bool  m_grindFaceInit    = false;				// この回で砥石に置いた最初に「表を砥石へ」を済ませたか(StartGame で false)
	float m_grindFlipRoll    = 0.0f;				// 裏返しの見た目の回転(rad。m_grindFace × π へ追従)
	static constexpr float GRIND_FLIP_LAMBDA = 10.0f;	// 裏返しの速さ(Damp率, 1/秒)
	float m_grindAngleTarget = 0.0f;				// マウスで決めた傾きの目標(刃は少し遅れて追う)
	static constexpr float GRIND_ANGLE_SENS   = 0.004f;	// マウス 1px → 傾き(rad)
	static constexpr float GRIND_ANGLE_MAX    = 0.80f;	// 傾けられる限界(rad, 約46°)
	static constexpr float GRIND_ANGLE_FOLLOW = 14.0f;	// 刃が目標を追う速さ(Damp率, 1/秒)
	static constexpr float GRIND_IDEAL_ANGLE  = 0.35f;	// 正しい研ぎ角(rad, 約20°)
	static constexpr float GRIND_ANGLE_TOL    = 0.20f;	// 正しい角からこれだけ外れると研げなくなる(rad, 約11°)
	float GrindAngleEfficiency() const;				// 研ぎの効率 0..1(正しい角で 1、TOL 外れで 0)
	int   GrindSide() const;							// 今研いでいる刃の面(0 = 表 / 1 = 裏)= 砥石へ向いている面(WeaponRender.cpp)
	bool  PlusIsFront() const;						// モデルの厚み軸の + 側の面が表か
	static constexpr float SHARP_HOLD_MAX = 0.55f;	// 研ぎ上がるまでの刃の見た目の上限(0..1)。仕上がった瞬間に 1 へ跳ぶ=区域の完成が見える
	float m_grindSparkAcc = 0.0f;
	bool  m_grindSndOn  = false;						// 研磨ループが流れているか(砥石の工位にいる間は流しっぱなし)
	float m_grindVol    = 0.0f;						// 研磨音の現在の大きさ 0..1(Damp で目標へ)
	static constexpr float GRIND_SND_VOLUME = 2.0f;		// 全速で押し当てた時の音量(1超=素材を増幅。素材が小さめのため)
	static constexpr float GRIND_SND_MIN_LEVEL = 0.4f;	// 押し当てて回っていれば、遅くてもこの割合は鳴る
	static constexpr float GRIND_PITCH_MIN  = 0.7f;		// 回転がほぼ止まっている時の音程(再生速度倍率)
	static constexpr float GRIND_PITCH_MAX  = 1.4f;		// 全速の時の音程(XAudio2 の上限 2.0 未満)
	static constexpr float GRIND_SND_LAMBDA = 10.0f;	// 音量の追従の速さ(Damp率。大=機敏)
	static constexpr float GRIND_SND_OFF    = 0.01f;	// これ未満まで消えたらループを止めてよい
	static constexpr float GRIND_WORK_MIN_SPEED = 0.3f;	// 砥石の回転(0..1)がこれ以上で研げる。未満は当てても削れない(踏んで回す意味)
	float GrindLenCoord();							// 砥石の接点 → 刃のローカル長手位置(セル単位 0..NL)
	float m_grindPress = 0.0f;						// 押し当て 0..1(Damp)。0 = 砥石の少し上に構える / 1 = 砥石に当てる
	static constexpr float GRIND_HOVER        = 0.03f;	// 押し当てていない時、刃の一番低い点を砥石から浮かせる高さ(m)
	static constexpr float GRIND_PRESS_LAMBDA = 14.0f;	// 押し当て/持ち上げの速さ(Damp率, 1/秒)
	static constexpr float GRIND_SPARK_RATE   = 90.0f;	// 全速時の研ぎ火花(個/秒)
	static constexpr float GRIND_DONE_SPARK_FACTOR = 0.15f;	// 研ぎ上がった所を研ぐと火花はこの割合まで細る(削る金属がもう無い)
	static constexpr float GRIND_DONE_VOL_MUL      = 0.5f;	// 〃 研ぐ音の大きさの倍率(手応えが無い)
	static constexpr float GRIND_DONE_PITCH_MUL    = 1.3f;	// 〃 研ぐ音の高さの倍率(上滑りする軽い音)
	static constexpr float GRIND_SPARK_DIP       = -0.25f;	// 火花の向きの下向き成分(接線の少し下へ。砥石の縁に沿って落ちる)
	static constexpr float GRIND_SPARK_SPREAD    = 0.30f;	// 火花が広がる円錐の半角(rad)
	static constexpr float GRIND_SPARK_SPEED_MIN = 2.0f;	// 火花の速さ(m/秒)
	static constexpr float GRIND_SPARK_SPEED_MAX = 4.5f;
	void  UpdateGrind(float tick, bool inputOn);	// 研磨の入力・物理・火花・音

	//--- 淬火と終幕(QuenchStep が進みを渡し、ここが見た目/音を担当)
	// 淬火の動き(ユーザー同意 2026-10-06): ①刃を下へ立てる(長軸まわりに90°) ②刃から水へ切り込む ③水中で刃の向きに上下に揺する(左右に振ると刃が曲がる=上下が正しい。2026-10-06 訂正: 長手の前後は槽の両端に当たった)。
	//   ③の理由: 入った直後の鋼は蒸気の膜に包まれて冷えにくい(蒸気膜段階 / vapor blanket。ライデンフロスト現象)。
	//   揺すって膜を破ると、泡立って一気に冷える段階(核沸騰)へ早く進み、冷え方が均一になる。
	float m_quenchTurn = 0.0f;						// 刃を立てた割合 0..1(①)
	float m_plunge    = 0.0f;						// 刃が水へ沈んだ割合 0..1(②)
	float m_agitate   = 0.0f;						// 上下の揺すりの今のずれ(m)(③)
	bool  m_quenchContact = false;					// 刃が水面に触れたか(音/蒸気/急冷はここから)
	static constexpr float QUENCH_TURN_ANGLE   = DirectX::XM_PIDIV2;	// 立てる角(長軸まわり)。90° = 刃が真下
	static constexpr float QUENCH_TURN_LIFT    = 0.06f;	// 立てる時に少し持ち上げる高さ(m。予備動作 / anticipation)
	//--- ③ 揺するのは玩家(ユーザー決定 2026-10-07): マウスの上下 = 刃の上下。持続して揺すると蒸気の膜が破れ核沸騰へ。
	//    揺すらなくても膜は温度が下がれば自然に破れる=止まらない(失敗の仕組みは作らない)。ただ遅く、評価が少し下がる。
	float m_stirTarget = 0.0f;						// マウスで動かした刃の高さの目標(m)。m_agitate は水の抵抗でこれを追う
	float m_filmBreak  = 0.0f;						// 膜沸騰中に刃が動いた道のりの合計(m)。FILM_BREAK_WORK で膜が破れる
	float m_filmTime   = 0.0f;						// 膜沸騰が続いた秒(評価: 早く破るほど良い)
	float m_stir01     = 0.0f;						// 今の揺する速さ 0..1(核沸騰の激しさと冷えの速さに効く)
	float m_quenchStartHeat = 0.0f;					// 入水した時の温度(淬火の完成度 % の起点)
	static constexpr float QUENCH_STIR_RANGE   = 0.04f;	// 刃を上下できる幅(m。片側)。上げても刃が水から出ない程度
	static constexpr float QUENCH_STIR_SENS    = 0.0006f;	// マウス 1px → 刃の高さ(m)
	static constexpr float QUENCH_STIR_FOLLOW  = 12.0f;	// 刃が目標を追う速さ(Damp率, 1/秒。水の抵抗で少し遅れる)
	static constexpr float QUENCH_STIR_RETURN  = 1.5f;	// 手を止めると目標が中央へ戻る速さ(Damp率, 1/秒)
	static constexpr float FILM_BREAK_WORK     = 0.25f;	// 膜を破るのに要る刃の道のり(m)
	static constexpr float STIR_FULL_SPEED     = 0.25f;	// この速さ(m/秒)で揺すると m_stir01 = 1
	static constexpr float STIR_SPEED_LAMBDA   = 8.0f;	// m_stir01 の追従(Damp率, 1/秒)
	float QuenchScore() const;						// 淬火の出来 0..1(膜を早く破るほど高い。出来栄えに小さく効く)
	static constexpr float QUENCH_FILM_TIME_BEST  = 1.0f;	// 膜沸騰がこれ以下で破れたら満点(秒)
	static constexpr float QUENCH_FILM_TIME_WORST = 6.0f;	// これ以上(=放置で自然に破れた位)なら 0 点(秒)
	float IronLowestY();							// 今の鉄の一番低い点の高さ(箱の8隅をワールドへ。水面に触れたかの判定)
	bool  HoldingAtTrough() const;					// 水槽で火钳が鉄を挟んで構えている(水槽の工位、または入る途中)
	// 水槽での火钳の柄の向き = 口から「上へ RISE・手前へ OUT」の点へ(比 = 傾き。約60°で手前の縁を越える)
	static constexpr float TROUGH_TONGS_RISE = 0.60f;
	static constexpr float TROUGH_TONGS_OUT  = 0.35f;
	float m_letterbox = 0.0f;						// 上下黒帯の入り具合 0..1
	static constexpr float QUENCH_MIN_TEMP  = 0.55f;	// これ未満では淬火できない(焼きが入らない)
	static constexpr float PLUNGE_DEPTH     = 0.40f;	// 構え位置から沈む深さ(水面の下まで)

	//--- 淬火の沸騰(ユーザー要望 2026-10-07:「熱い物を水に入れた爆発感」)。見た目は全部「鉄の温度と沸騰の段階」から出す
	//    (アニメのフレームに紐付けない=状態駆動の VFX)。実際の焼入れの冷え方の3段階:
	//      蒸気膜段階(膜沸騰 / film boiling): 入った直後、鋼が蒸気の膜に包まれて冷えにくい(ライデンフロスト現象)。
	//                                       揺すり始める(膜が破れる)か、温度が LEIDENFROST_TEMP を下回るまで。
	//      核沸騰(nucleate boiling)          : 膜が破れ、表面で泡が激しく沸く。一番速く冷え、蒸気も一番多い。
	//      対流(convection)                  : BOIL_END_TEMP を下回ると沸騰が止み、水の流れでゆっくり冷える。
	enum class BoilStage { None, Film, Nucleate, Convection };
	BoilStage m_boilStage = BoilStage::None;
	float m_boil         = 0.0f;					// 沸き立ちの強さ 0..1(蒸気の量・水面の泡立ちが読む)
	DirectX::XMFLOAT3 m_bladeLineA = { 0, 0, 0 };	// 刃が水面を切る線の両端(蒸気/水しぶき/泡立ちの位置)
	DirectX::XMFLOAT3 m_bladeLineB = { 0, 0, 0 };
	static constexpr float LEIDENFROST_TEMP     = 0.45f;	// これを下回ると蒸気の膜が保てない(温度は 0..1 の正規化値)
	static constexpr float BOIL_END_TEMP        = 0.15f;	// これを下回ると沸騰が止む
	static constexpr float FILM_COOL_RATE       = 0.04f;	// 膜沸騰中の冷え(/秒。膜が断熱するので遅い)
	static constexpr float NUCLEATE_COOL_RATE   = 0.08f;	// 核沸騰中の冷え(/秒。揺すらない時)
	static constexpr float NUCLEATE_STIR_BONUS  = 1.0f;		// 全力で揺すると核沸騰の冷えがこの割合だけ速くなる(新しい冷水が刃に当たる)
	static constexpr float NUCLEATE_CALM_BOIL   = 0.6f;		// 揺すらない時の核沸騰の激しさ(全力で揺すると 1)
	static constexpr float CONVECTION_COOL_RATE = 0.05f;	// 対流の冷え(/秒)
	static constexpr float FILM_BOIL            = 0.35f;	// 膜沸騰の沸き立ち(核沸騰の最大 = 1 に対して。静かに「シュー」)
	static constexpr float BOIL_FOLLOW_LAMBDA   = 6.0f;		// m_boil が段階の目標へ追従する速さ(Damp率, 1/秒)
	static constexpr float STEAM_RATE           = 220.0f;	// 沸き立ち 1 の時の蒸気(個/秒)
	static constexpr float STEAM_SPREAD         = 0.06f;	// 刃の線から蒸気が湧く幅(m)
	static constexpr int   STEAM_BURST          = 160;		// 入水の瞬間に一度に噴く蒸気(個)
	static constexpr float STEAM_BURST_SPEED    = 2.2f;		// その噴き出しの速さ(通常の蒸気に対する倍率)
	static constexpr int   SPLASH_COUNT         = 120;		// 入水の瞬間に跳ねる水しぶき(個)
	static constexpr float SPLASH_SPEED         = 2.5f;		// 水しぶきの速さ(m/秒)
	static constexpr float QUENCH_SHAKE         = 0.6f;		// 入水の瞬間のカメラの揺れ(m_shake。打撃と同じ減衰振動)
	void  UpdateBoil(float tick);					// 段階の遷移・段階ごとの冷え・沸き立ちの追従(UpdatePlay)
	void  UpdateBladeWaterline();					// 刃の長軸の両端を水面の高さへ(m_bladeLineA/B)
	void  OnQuenchContact();						// 刃が水面に触れた瞬間: 音・噴き出す蒸気・水しぶき・揺れ・水面の大波

	//--- 水槽の水面 = 2D 波動方程式の高さ場(Physics/WaterSim。ユーザー要望 2026-10-07:「本物の水の波」)。
	//    刃が入る/揺する/沸騰の泡 が水面を押し、波は自分で広がって槽の壁で跳ね返る。高さは毎フレーム
	//    小さな貼图(R32_FLOAT)で GPU へ送り、VS_Water が格子の頂点を上下させ、PS_Water が法線を求める。
	WaterSim m_waterSim;
	std::unique_ptr<Texture> m_waterHeightTex;		// その高さ(WaterSim::NX × NZ)
	float m_bubbleAcc    = 0.0f;					// 沸騰の泡の端数(フレームをまたいで持ち越す)
	float m_prevAgitate  = 0.0f;					// 前フレームの揺すりのずれ(刃が上下に動いた量で水を押す)
	static constexpr float WATER_ENTRY_PUSH    = -0.035f;	// 刃が入った瞬間に刃の線で水面を押し下げる量(m)
	static constexpr float WATER_PUSH_RADIUS   = 0.05f;		// 刃が押す幅(m)
	static constexpr float WATER_STROKE_PUSH   = 0.8f;		// 揺すり: 刃が上下に動いた量(m) → 水面を押す量(m)の比(刃が下がると周りの水が押し上がる)
	static constexpr float WATER_BUBBLE_RATE   = 70.0f;		// 沸き立ち 1 の時の泡(個/秒)
	static constexpr float WATER_BUBBLE_PUSH   = 0.006f;	// 泡1つが水面を持ち上げる量(m)
	static constexpr float WATER_BUBBLE_RADIUS = 0.025f;	// 泡1つの大きさ(m)
	static constexpr float WATER_BUBBLE_SPREAD = 0.04f;		// 泡が湧く、刃の線からの幅(m)
	void  UpdateWaterSim(float tick);				// 刃/泡で水面を押し、波を進める(Update。どの状態でも)
	void  WorldToWaterLocal(const DirectX::XMFLOAT3& p, float& u, float& v) const;	// ワールド → 水面の板のローカル(-1..1)
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

	//--- 走動 ⇔ 工位 の過渡(移動アニメ)。E で入る/E で出る。取り消し不可(入力は捨てる)。
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
	static DirectX::XMFLOAT3 BedLongAxis(float yaw, const float size[2]);	// 炭床/水面の四角の長辺の水平方向
	void   SetupStationView(Station s);		// 工位の視点方向などを玩家の今の位置から決める(置く拍子表が置き場所の向きを先に知る為にも使う)
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
	enum class InteractAction { EnterStation, GripIron, ToggleDoor };
	struct Interactable
	{
		const char*    propKey;	// どのプロップか(配置は stage_layout.txt に従う=箱も自動で追従)。扉は家の部品なので DOOR_KEY
		InteractAction action;	// E で何をするか
		Station        station;	// EnterStation の時に入る工位(他の行為では使わない)
		float          reach;	// 互動範囲: 物件の箱から水平にどこまで離れても届くか(単位)
	};
	static const Interactable INTERACTABLES[];
	static const int          NUM_INTERACTABLES;
	int   m_focus = -1;							// 今 E で互動できる物件(INTERACTABLES の番号)。-1=無し
	DirectX::XMFLOAT3 m_promptPoint = { 0,0,0 };	// 提示を出すワールド点(最後に注視した物件の中心)
	float m_promptAlpha  = 0.0f;				// 提示のフェード 0..1(出る/消えるをなめらかに)
	const char* m_promptLabel = "";				// 「E」の下の一言(最後に注視した物件の文言。PromptLabel)
	float m_promptLambda = 12.0f;				// フェードの速さ(Damp率, 1/秒)
	float m_lookPad      = 0.12f;				// 視線判定の箱を膨らませる量(細い鉄でも狙える様に)
	bool  InteractEnabled(const Interactable& it) const;	// 今この互動ができる状況か(例: 鉄は手に持っていない時だけ掴める)
	int   InteractPriority(const Interactable& it) const;	// 視線が複数に当たった時の優先度(鉄は工位の箱の中にあるので鉄を優先)
	const char* PromptLabel(const Interactable& it) const;	// 「E」の下に出す一言(何が起きるか)
	bool  PropWorldBox(Prop& p, DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx);	// プロップのワールドAABB
	bool  InteractBox(const Interactable& it, DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx);	// 互動物件のワールドAABB(扉は今の角度で)
	void  UpdateInteract(float tick);			// 2判定→m_focus を決める(走動中のみ)
	void  DoInteract(const Interactable& it);	// E を押された物件の行為を実行
	void  DrawInteractPrompt();					// 物件の上に「E」ボタンを描く(HUD)
	void  DrawInteractBoxes();					// F1: 互動範囲の箱を線で表示(範囲内=緑)

	//--- 走動中の衝突(SceneForge/Collision.cpp)。玩家=足元の円。ぶつかる相手は形で2種類(Physics/Collision2D):
	//      道具=上から見た2Dの凸包 / 建物(整屋)=腰の高さで水平に切った断面の線分(壁線)。
	//    どちらも読込時にモデルから自動で作る(手で箱を置かない)=モデル差替え/配置変更で再調整不要。
	//    どの道具がぶつかるかは表 COLLIDERS(データ, キーの前方一致)。小物(火钳/鎚/廃鉄)は入れない。
	enum class HullShape { Whole, PerPiece };	// 凸包1つで包む / 繋がった塊ごとに1つずつ(石の山=石の間を塞がない)
	struct Collider
	{
		const char* keyPrefix;	// プロップキーの前方一致
		HullShape   shape;
	};
	static const Collider COLLIDERS[];
	static const int      NUM_COLLIDERS;
	Collision2D::World m_collision;				// 今フレームのワールドの衝突形状(XZ)。Player::Update に渡す
	bool  m_showCollision = false;				// 衝突の可視化(F1 最上段のチェック。F1を閉じても表示し続ける。壁越しでも見える)
	static constexpr float PLAYER_BODY_HEIGHT = 1.7f;	// 人の背丈。これより高い部分(樹冠/梁)はぶつからない=凸包から外す。可視化の柱の高さも同じ
	static constexpr float COLLISION_DRAW_LIFT   = 0.02f;	// 可視化: 床と重ならない様に少し浮かせる(Zファイト防止)
	static constexpr float COLLISION_TOUCH_MARGIN = 0.02f;	// 可視化: 玩家の円がこの距離まで近ければ「接触中」(赤)
	static constexpr int   PLAYER_CIRCLE_SEGMENTS = 32;		// 可視化: 玩家の円を何角形で近似して描くか
	const Collider* FindCollider(const std::string& key) const;	// 表 COLLIDERS の行(載っていなければ nullptr=ぶつからない)
	void  BuildPropHull(Prop& p);				// 背丈以下の頂点(+動く部品)を床へ投影し、モデル空間の凸包を作る
	void  BuildPropHulls();						// 全プロップの凸包を作る(配置が決まった後=LoadLayout の後に1回)
	void  BuildCollisionWorld();				// 凸包/壁線/扉を今の配置でワールドへ運ぶ(毎フレーム。点が少ないので軽い)
	void  DrawCollision();						// 凸包の柱・壁線・玩家の円を線で描く(m_showCollision の時)
	//--- 建物の壁線: 家の三角形(扉を除く)を、体の高さ(床+m_wallSliceLowHeight〜背丈)の間で m_wallSliceStep ごとに切って合わせた線。
	//    切る高さ(家のモデル空間)は家の配置で変わるので、変わった時だけ切り直す(三角形は保持)。
	std::vector<DirectX::XMFLOAT3>     m_houseTris;		// 家の三角形(モデル空間, 3頂点ずつ)。Init で1回取得
	std::vector<Collision2D::Segment>  m_wallSegLocal;	// 断面の線分(家のモデル空間 XZ)
	float m_wallSliceLocalY = -1e30f;				// 一番低い切り口(家のモデル空間)。変化検出用
	float m_wallSliceStepUsed = -1.0f;				// 切った時の刻み。変化検出用
	float m_wallSliceLowHeight = 0.3f;				// 一番低い切り口(床から, m)。床の段差/敷居は拾わない高さ(F1)
	float m_wallSliceStep      = 0.3f;				// 切り口の間隔(m)。これより薄い張り出しは間をすり抜け得る(F1)
	static constexpr float WALL_RESLICE_EPS = 1e-3f;	// 切る高さがこれ以上変わったら切り直す(家のモデル空間)
	void  UpdateWallSlice(const DirectX::XMMATRIX& houseWorld);	// 必要なら壁線を切り直す
	void  InitBuildingCollision();					// 家の三角形・扉の蝶番/凸包を用意する(Init で1回)

	//--- 鉄の運搬(SceneForge/Carry.cpp)。鉄は「置かれた工位」から瞬間移動しない: 玩家が火钳で掴んで運ぶ。
	//    状態は m_carrying の1つだけ(手に持っている/いない)。火钳は普段は左腰に掛けてあり(ユーザーの設定)、
	//    鉄を掴む時に抜き、鉄を置くと腰へ戻る。
	//      掴む : 走動中に鉄を見て E(火钳は腰から自動で抜く)
	//      運ぶ : 鉄と火钳をカメラの前に描く(HeldPoint)
	//      置く : 工位を見て E → その工位に置いて入る
	//      鉄が無い工位で E → 入れない。主人公が鉄の在り処を言う(自動では動かない=玩家が自分で歩く)
	static constexpr const char* IRON_KEY = "Iron";	// 互動の表での鉄のキー(プロップではない)
	//    手に持った時の見た目 = 一人称のビューモデル(first-person viewmodel)。鉄も火钳もカメラ基準で置く
	//    =どこを見ても画面上の構図が同じ(ユーザーの絵コンテ: 鉄は左下から右奥へ斜めに伸び、火钳は画面下から伸びて
	//    鉄の手前寄りを挟む)。
	float m_gripOff[3]    = { -0.20f, -0.30f, 0.70f };	// 火钳が鉄を挟む点(カメラから 右/上/前, world)
	float m_carryYaw      = 0.70f;					// 鉄の向き: 視線から右へ振る角(rad)。0=真っ直ぐ前
	float m_carryPitch    = 0.10f;					// 鉄の向き: 上へ起こす角(rad)
	float m_gripAlong     = 0.20f;					// 挟む位置: 鉄の手前の端から何割の所か(0=端 .. 1=奥の端)
	float m_tongsBase[3]  = { 0.10f, -0.85f, 0.20f };	// 火钳の柄の根元(カメラから 右/上/前)。画面の下の外=手元
	DirectX::XMFLOAT3 m_heldDir = { 0, 0, 1 };		// 手に持った鉄の長軸の向き(HeldPoint が毎回計算。WeaponRot が読む)
	//    ① 武器の揺れ(weapon sway): ビューモデルの「前方」はカメラの前方を Damp で少し遅れて追う
	//       =視点を振ると手の物が一瞬遅れて付いて来る(腕の重さ)。値が大きいほど機敏(遅れが小さい)。
	//    ② めり込み防止(depth range hack): ビューモデルは深度範囲を [0, VIEWMODEL_DEPTH_RANGE] に詰めて最後に描く
	//       =深度が常に場面より手前になり、床や壁にめり込まない(Quake/Source 系の古典的手法)。
	DirectX::XMFLOAT3 m_vmFwd = { 0, 0, 1 };		// ビューモデル用の(遅れて追う)前方
	bool  m_vmFwdInit = false;						// 初回/掴んだ瞬間はカメラの前方へ即合わせる
	float m_vmSwayLambda = 14.0f;					// 揺れの追従の速さ(Damp率, 1/秒。小=重く遅れる)
	static constexpr float VIEWMODEL_DEPTH_RANGE = 0.02f;	// ビューモデルの深度の上限(0..1 の手前 2%)
	void  UpdateViewmodelSway(float tick);			// m_vmFwd をカメラの前方へ追わせる(Update で毎フレーム)
	//    ③ 運んでいる時の歩き: 重い鉄を持つので遅くなる + 歩みに合わせて手の物が上下に揺れる(view bobbing)。
	//       揺れの位相は「時間」でなく「歩いた距離」で進める=速く歩けば速く揺れ、止まれば止まる(足取りと一致)。
	//       上下は1歩に1回、左右はその半分の周波数=小さな∞字を描く(日本のゲームでよく見る歩きの手応え)。
	float m_carrySpeedMul = 0.6f;					// 運んでいる時の歩く速さの倍率(1=普段通り)
	float m_bobAmp        = 0.015f;					// 上下の揺れ幅(world)
	float m_bobSideAmp    = 0.008f;					// 左右の揺れ幅(world)
	float m_bobPerMeter   = 1.6f;					// 1m 歩く間の揺れ(上下)の回数=歩幅の逆数
	float m_bobPhase      = 0.0f;					// 揺れの位相(回数。歩いた距離 × m_bobPerMeter)
	float m_bobWeight     = 0.0f;					// 揺れの強さ 0..1(歩き出し/止まりで Damp=急に始まらない)
	static constexpr float BOB_FADE_LAMBDA = 8.0f;	// 揺れの出入りの速さ(Damp率, 1/秒)
	static constexpr float BOB_MOVE_EPS    = 1e-4f;	// 1フレームにこれ未満しか動いていなければ「止まっている」
	void  UpdateCarryBob(float tick, float walked);	// 歩いた距離から揺れの位相と強さを進める
	//--- 手の鉄の「めり込み回避」(FPS の武器の壁めり込み回避 / weapon wall-clipping avoidance と同じ考え方)。
	//    視線の水平方向へ 2D レイを飛ばし(Collision2D::RayCast = 歩きの衝突と同じ凸包/壁線)、
	//    鉄の先端がそこへ届かない様に ①まず手元へ引き寄せる(m_carryPull) ②それでも足りなければ上へ起こす(m_carryRaise)。
	//    (ユーザー選択 2026-10-05: 起こすだけだと歩く度に鉄が上下に揺れた。FPS の「壁際で銃を引く」と同じ順序)
	//    起こす角は二分探索で求め、どちらも Damp で滑らかに追従。
	float m_carryPull          = 0.0f;				// 今の引き寄せ量(m。挟む点を鉄の水平方向の逆へ)
	float m_carryAvoidMaxPull  = 0.35f;				// 引き寄せの上限(m)。これを超える分だけ起こす
	float m_carryRaise         = 0.0f;				// 今の追加の起こし角(rad。m_carryPitch に足す)
	float m_carryAvoidMaxRaise = 1.2f;				// 起こす角の上限(rad)。これでも当たる時は諦める(壁に張り付いている時)
	float m_carryAvoidMargin   = 0.08f;				// 道具/壁の手前に空ける隙間(m)
	float m_carryAvoidLambda   = 10.0f;				// 追従の速さ(Damp率, 1/秒)
	static constexpr int CARRY_AVOID_ITERATIONS = 12;	// 二分探索の回数(角度の誤差 = 上限 / 2^回数)
	DirectX::XMFLOAT2 m_carryAvoidOrigin = { 0, 0 };	// 最後のレイ(デバッグ表示用): 始点 XZ
	DirectX::XMFLOAT2 m_carryAvoidDir    = { 0, 1 };	//   向き XZ
	float             m_carryAvoidFree   = 0.0f;	//   当たるまでの距離
	void  UpdateCarryAvoid(float tick);				// 起こし角の目標を求めて追従(走動中、運んでいる時)
	float HeldReach(float raise, DirectX::FXMVECTOR eye, const DirectX::XMFLOAT2& dirH);	// その起こし角で鉄の先端が視線の水平方向へ届く距離
	DirectX::XMVECTOR ViewmodelBob(DirectX::FXMVECTOR right, DirectX::FXMVECTOR up) const;	// 今の揺れ(カメラ基準のずれ)
	void  DrawViewmodel();							// 手に持った鉄と火钳を、深度範囲を詰めて最後に描く
	float m_tongsScale    = 1.0f;					// 手/腰の火钳の大きさ(台上のプロップの大きさに対する倍率)
	//    火钳モデルの形はモデルから自動で読む(手で向きを合わせない。InitTongsGeometry):
	//      長軸 = 箱の最長辺 / 開閉の平面 = 長軸と2番目に長い辺 / 薄い軸 = 残り(鉄はこの向きに輪を通る)
	//      要(かなめ)の鋲 = 一番小さい部品(繋がった三角形の塊)。口 = 長軸の端のうち鋲に近い方。
	//      挟む点 = 口の先端と鋲の間(TONGS_JAW_CENTER_FRAC)= 口が作る輪の中心。
	bool  m_tongsGeomOk   = false;
	int   m_tongsLongAx   = 2, m_tongsThinAx = 1;	// 長軸 / 薄い軸(0=x,1=y,2=z)
	float m_tongsJawSign  = -1.0f;					// 口がある端(長軸の -端=-1 / +端=+1)
	DirectX::XMFLOAT3 m_tongsGripLocal = { 0, 0, 0 };	// 挟む点(火钳モデル空間)
	static constexpr float TONGS_JAW_CENTER_FRAC = 0.5f;	// 挟む点 = 口の先端→鋲 のこの割合の所
	void  InitTongsGeometry();						// 上の値を火钳モデルから求める(Init で1回)
	float m_hipOff[3]     = { 0.28f, 0.95f, 0.12f };	// 腰の火钳の位置(体から 左/床からの高さ/前, world)
	// 右腰のハンマー(ユーザー要望 2026-10-08: 金床を離れてもハンマーが金床に残って見えた → 左腰=火钳 / 右腰=ハンマー)。F1「Carry」で合わせる
	float m_hammerHipOff[3] = { 0.28f, 0.95f, 0.0f };	// 腰帯の輪(頭が掛かる所): 体から 右/床からの高さ/前(m)。前 0 = 体の真横=見下ろしても視野に入らない
	float m_hammerHipYaw    = 0.0f;						// 垂れた柄の軸まわりの向き(rad)。頭の打つ面を前後/左右どちらへ向けるか(見た目だけ)
	DirectX::XMFLOAT3 HeldPoint();					// 手に持った鉄の中心(挟む点から m_heldDir へずらした所)。m_heldDir も更新
	DirectX::XMFLOAT3 HeldGrip();					// 火钳が鉄を挟む点(カメラ基準)
	bool  CameraBasis(DirectX::XMVECTOR& eye, DirectX::XMVECTOR& fwd, DirectX::XMVECTOR& right, DirectX::XMVECTOR& up);	// 今のカメラの位置と向き
	DirectX::XMFLOAT3 BodyForward();				// 体の水平前方(走動=玩家の向き / 工位=その工位の既定カメラの水平視線)
	DirectX::XMFLOAT3 BodyPosition();				// 体の位置(走動=玩家の足元 / 工位=その工位の既定カメラの真下)。腰の道具はここ基準
	void  StationBodyPose(DirectX::XMFLOAT3& eye, DirectX::XMFLOAT3& target);	// 今の工位の既定カメラ(金床=m_camPos/Look、他=StationView)
	DirectX::XMFLOAT3 HipPoint();					// 左腰の点(火钳を掛ける所。翻面の運鏡もここを見る)
	DirectX::XMMATRIX TongsWorld(const DirectX::XMFLOAT3& approach, const DirectX::XMFLOAT3& barDir,
	                             const DirectX::XMFLOAT3& gripAt);	// 挟む点を gripAt に、柄→口を approach へ、輪を barDir が通る様に
	void  GripIron();								// 鉄を火钳で掴む(手に持つ)

	//--- 拍子表(スクリプトシーケンス, Sequence.cpp)。過渡動画を「拍(ビート)の表=データ」で書き、1つの再生器で流す。
	//    1拍 = 「カメラがどこを見るか」+「火钳がどこへ行くか」+「鉄がどこへ行くか」+ 秒数。
	//    拍の始めの姿勢から、拍の目標へイージングで補間する。目標は毎フレーム解決する(カメラが動いても追従)。
	//    再生中は入力を受けない(移動アニメと同じ)。最後の拍が終わると onEnd の行為(例: 鉄を手に持つ)を実行。
	//    新しい動作 = 表を1つ足すだけ(Unity の Timeline / UE の Sequencer と同じ考え方の最小版)。
	//    腰は見ない(ユーザー判断 2026-10-04: 腰の物を取る時に人は下を向かない。遊びのテンポを削るだけ)。
	enum class SeqLook  { Iron, Home };				// カメラの注視先: 鉄の置き場所 / 再生開始時に見ていた方向
	enum class SeqTongs { Hip, Iron, Held };		// 火钳の行き先: 腰に下げた所 / その拍の鉄を挟む所 / 手元(ビューモデルの位置)
	enum class SeqIron  { Rest, Approach, Held };	// 鉄の行き先: 目標工位に置いた所 / その手前の中継点(ウェイポイント) / 手に持った所
	enum class SeqEnd   { GripIron, PutIron };		// 再生し終えた時の行為: 手に持つ / 目標工位に置いてその工位へ入る
	// 拍の補間曲線(スローイン・スローアウト / slow in & slow out)。動作ごとに速さの付き方を変える。
	enum class SeqEase  { InOut, Out };				// InOut = 出だしも止まりもゆっくり(smoothstep) / Out = 速く出て、ゆっくり着く(置く・挟む)
	struct SeqBeat
	{
		SeqLook  look;
		SeqTongs tongs;
		SeqIron  iron;
		float    seconds;	// この拍の長さ(再生ごとに m_seqTimeJitter だけ揺らぐ)
		SeqEase  ease;		// 手(火钳/鉄)の補間曲線
	};
	struct Sequence
	{
		const SeqBeat* beats;
		int            count;
		SeqEnd         onEnd;
		float          approachBack;	// 中継点(SeqIron::Approach) = 置き場所から玩家側へ水平に戻した距離(m)
		float          approachUp;		// 〃 置き場所より上へ持ち上げた高さ(m)。金床=真上から下ろす(高い)
		bool           approachAlongIron;	// true = 中継点を「玩家側へ水平」でなく「鉄の長軸に沿って手元側」へ戻す(炉: 斜めの鉄をそのまま差し込む)
	};
	static const SeqBeat  GRIP_IRON_BEATS[];		// その拍の表(Sequence.cpp)
	static const Sequence SEQ_GRIP_IRON;			// 火钳を抜きつつ鉄を見る → 挟む → 持ち上げる
	static const SeqBeat  PUT_IRON_BEATS[];			// 置く拍の表(炉/金床/砥石で共通。工位ごとの違いは中継点だけ)
	static const SeqBeat  HOLD_IRON_BEATS[];		// 水槽: 置かずに火钳で挟んだまま水面の上に構える(淬火は手で持ったまま行う)
	static const Sequence SEQ_PUT_HEARTH, SEQ_PUT_ANVIL, SEQ_PUT_GRIND, SEQ_PUT_TROUGH;	// 炉/金床/砥石/水槽へ置く
	static const Sequence& PutIronSequence(Station s);	// 工位 → その工位へ置く拍子表
	bool  m_heldFlip = false;						// 手に持った鉄の前後を入れ替える(掴んだ端=手元側。掴んだ時に決める)
	bool  m_restFlip = false;						// 工位に置いた鉄を水平に半回転(掴んだ端=玩家側。置く時に決める。火钳を離さずに置ける向き)
	struct TongsPose { DirectX::XMFLOAT3 grip, approach, barDir; };	// TongsWorld の3引数(挟む点/柄→口/輪を通る向き)
	const Sequence* m_seq = nullptr;				// 再生中の拍子表(nullptr=再生していない)
	Station m_seqStation = Station::Anvil;			// SeqIron::Rest の工位(掴む=今鉄がある工位 / 置く=置き先)
	int   m_seqBeat  = 0;							// 今の拍
	float m_seqTimer = 0.0f;						// 今の拍の経過秒
	float m_seqBeatDur = 0.0f;						// 今の拍の長さ(表の秒数 × 揺らぎ)
	float m_seqArcK    = 0.0f;						// 今の拍の弧の高さ(水平移動距離に対する比。m_seqArcLift × 揺らぎ)
	bool  m_seqAttached = false;					// 今の拍で火钳が鉄を挟んだまま(火钳の姿勢を鉄から求める=ずれない)
	bool  m_seqPrevGrip = false;					// 前の拍の終わりに火钳が鉄を挟んでいたか
	// 人の動きらしさ(ディズニーの12原則のうち4つ。F1 Carry → Sequence feel)
	float m_seqHandLag    = 0.18f;	// フォロースルーとオーバーラップ: 目が先、手は拍の長さのこの割合だけ遅れて動き出す
	float m_seqArcLift    = 0.25f;	// アーク: 手で運ぶ物は弧を描く。中間で水平移動距離のこの割合だけ持ち上がる
	float m_seqTimeJitter = 0.10f;	// 揺らぎ: 拍の長さが再生ごとに ±この割合ばらつく(毎回同じ機械的な動きにしない)
	float m_seqArcJitter  = 0.30f;	// 揺らぎ: 弧の高さの ±ばらつき
	float m_seqBreathW    = 0.0f;	// 再生中だけ走動カメラに手持ちの呼吸を乗せる重み(0..1。急に付いたり消えたりしない様に追従)
	static constexpr float SEQ_BREATH_FADE_TIME = 0.35f;	// その重みが追従する時定数(秒)
	void  UpdateSeqBreath(float tick);				// m_seqBreathW を再生中=1 / それ以外=0 へ近づける
	DirectX::XMFLOAT3 m_seqHomeFwd  = { 0, 0, 1 };	// 再生開始時の視線(SeqLook::Home)
	DirectX::XMFLOAT3 m_seqBodyFwd  = { 0, 0, 1 };	// 再生中の体の向き(固定)。腰の点は体の向きから決まるので、
													// 視線で体まで回すと腰が逃げて視線が追い続けてしまう→再生中は首だけ回す
	DirectX::XMFLOAT3 m_seqFromFwd  = { 0, 0, 1 };	// 今の拍の開始時の視線
	TongsPose         m_seqFromTongs = {};			// 今の拍の開始時の火钳
	TongsPose         m_seqTongs     = {};			// 今フレームの火钳(描画用)
	DirectX::XMFLOAT4X4 m_seqFromIron = {};			// 今の拍の開始時の鉄のワールド行列
	bool  m_seqIronOverride = false;				// true の間 WeaponWorld/WeaponRot は下の値を返す(鉄が拍子表で動いている)
	DirectX::XMFLOAT4X4 m_seqIronWorld = {};
	DirectX::XMFLOAT4X4 m_seqIronRot   = {};
	bool  SequencePlaying() const { return m_seq != nullptr; }
	void  PlaySequence(const Sequence& seq, Station target);	// 再生開始(走動中に呼ぶ)。target = SeqIron::Rest の工位
	void  UpdateSequence(float tick);				// 拍を進め、カメラ/火钳/鉄を補間する
	void  StopSequence();							// 途中で打ち切る(状態遷移時の後始末。onEnd は実行しない)
	void  BeginSeqBeat();							// 拍の開始姿勢を記録
	void  RunSeqEnd(SeqEnd onEnd, Station target);	// 再生し終えた時の行為を実行
	void  IronEnds(DirectX::FXMMATRIX world, DirectX::XMVECTOR& gripped, DirectX::XMVECTOR& other) const;	// 鉄の長軸の両端(火钳が掴む端/反対の端)をワールドへ
	DirectX::XMFLOAT3 SeqLookTarget(SeqLook l);		// 注視先のワールド点
	TongsPose SeqTongsPose(SeqTongs t, SeqIron iron);	// 火钳の行き先の姿勢(Iron = 鉄の行き先 iron の所を挟む)
	TongsPose TongsOnIron(DirectX::FXMMATRIX ironWorld);	// その姿勢の鉄を、掴んだ端から m_gripAlong の所で挟む火钳
	DirectX::XMMATRIX SeqIronWorld(SeqIron i);		// 鉄の行き先のワールド行列(置いた所/中継点/手に持った)
	void  PutIronAt(Station s);						// 手の鉄を工位に置く
	void  SayWhereIronIs();							// 鉄の在り処を独白で知らせる(鉄の無い工位で E)
	void  DrawCarry();								// 火钳(手/腰)を描く

	//--- インタラクティブ草(SceneForge/InteractiveGrass.cpp)。草は玩家を避けて倒れ、離れると戻る。
	//    InteractionMap(真上から正射影で見た「最近押された所」の貼图)に毎フレーム玩家の足元を押し、
	//    草の頂点シェーダー(VS_Grass)がそれを読んで葉先を動かす。草に衝突は無い(踏み込める)。
	InteractionMap m_grassMap;
	float m_grassMapDt       = 0.0f;			// Update で溜めた経過時間 → Draw の Fade へ(GPU の処理は Draw で行う)
	float m_grassStampRadius = 0.7f;			// 足元で草を押し分ける円の半径(world)。体(円 0.3)より広い=周りの草も避ける
	float m_grassLean        = 0.6f;			// 横へ倒れる量(草の高さに対する割合。坂が最も急な所で約1.5倍)
	float m_grassPress       = 0.5f;			// 足元で葉先が沈む量(草の高さに対する割合)
	bool  m_showGrassMap     = false;			// F1: 貼图そのものを画面に表示(押した跡が見える)
	static constexpr UINT GRASS_MAP_RESOLUTION = 512;	// 貼图の解像度(一辺)。範囲は屋外の地面から自動
	static constexpr const char* GRASS_KEY_PREFIX = "StOutdoorGrass";	// このキーで始まるプロップを草として描く
	struct GrassParams { DirectX::XMFLOAT4 area, bend, slope; };	// VS_Grass の cbuffer b1 と同じ並び
	void  InitGrassMap();						// 屋外の地面の範囲で貼图を作る(Init で1回)
	void  UpdateGrassMap();						// Draw の最初: 薄める → 足元を押す → 描画先を元に戻す
	bool  IsGrass(const std::string& key) const;
	void  BindGrassParams(VertexShader* vs, Prop& p);	// 草1株ぶんの cbuffer と貼图を VS へ
	void  DrawGrassMapPreview();				// F1: 貼图を ImGui で表示

	//--- 整屋の裏口の扉(開閉する独立物件。CottageDoor.h / GameLogic/HingedDoor)。E で開閉。
	//    絵(DoorWorld で描く)・衝突(扉の凸包を同じ行列で運ぶ)・互動(扉の箱)が全部同じ角度を読む。
	HingedDoor         m_door;					// 開閉の状態と角度
	CottageDoor::Hinge m_doorHinge;				// 蝶番の軸と「室内へ開く」回転方向(読込時に自動計算)
	Collision2D::Hull  m_doorHullLocal;			// 扉板の凸包(家のモデル空間 XZ。閉じた姿勢)
	DirectX::XMMATRIX  DoorWorld();				// 扉のワールド行列 = 蝶番の回転 × 家の配置

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
	//--- 一時停止メニュー(ESC。2026-10-08 ユーザー要望)。開いている間は世界の更新を止める。
	enum class PauseItem { Resume, Title, Quit, Count, None = -1 };	// 並び順 = メニューの上から
	bool      m_paused       = false;
	int       m_pauseSel     = 0;					// 今選んでいる項目(マウスのホバー / 上下キー)
	PauseItem m_pauseRequest = PauseItem::None;	// 決定された項目(描画側のクリック or キー)。次の UpdatePause で実行
	bool  CanPause() const;
	void  OpenPause();
	void  ClosePause();
	void  UpdatePause();
	void  DrawPauseMenu();						// HUD.cpp

	//--- 調整用パラメータ(F1デバッグでスライダ変更可)
	float m_strikeCDMax = 1.25f;	// 打撃後クールダウン(秒)
	// 自然冷却速度は鉄の物理なので m_forging.coolRate に移した。

	//--- 武器モーフ(Blenderで作った同拓扑の各段FBXを頂点補間して成形する) ---
	// uv は真の鋼テクスチャ採样用。morphでUVは不変なので stage0 の値を全段で使う。
	// フィールド順は VS_Wp の VIN 宣言順(pos→nrm→uv→col→sharp)と一致させること(入力レイアウトが宣言順で焼かれる)。
	// sharp = 研いだ刃先の度合い 0..1(=その区域の鋭さ × 刃先への近さ)。PS が研ぎ面の見た目に使う。
	// work = この頂点が属する面(表/裏)の鍛造進捗 0..1。PS が氧化皮(黒皮)を「叩いた分だけ」剥がすのに使う。
	//   ※並びは VS の入力(POSITION/NORMAL/TEXCOORD0..3)と一致させる(入力レイアウトは D3DReflect で順に詰めて作る)。
	struct WpVtx { DirectX::XMFLOAT3 pos; DirectX::XMFLOAT3 nrm; DirectX::XMFLOAT2 uv; DirectX::XMFLOAT4 col; float sharp; float work; };
	struct WpStage { std::vector<DirectX::XMFLOAT3> pos, nrm; std::vector<DirectX::XMFLOAT2> uv; };	// 1段分の生頂点(ローカル)
	std::vector<WpStage>        m_wpStage;		// stage_0 .. stage_final
	std::vector<unsigned int>   m_wpIdx;		// インデックス(全段共通)
	std::vector<WpVtx>          m_wpVtx;		// 補間後の頂点(毎フレーム再構築)
	std::shared_ptr<MeshBuffer> m_wpMesh;
	std::shared_ptr<Texture>    m_wpTex;		// 真の鋼テクスチャ(BaseColor=冷鋼の地色)。発光は温度(m_forging.Heat())駆動
	std::shared_ptr<Texture>    m_wpScaleMask;	// 氧化皮(黒皮)の厚みマスク(灰度: 白=厚い / 黒=地金)。無縫で UV に繰り返し貼る
	//--- 氧化皮(黒皮)。熱い鋼の表面にできる黒い酸化膜。叩いた面ほど剥がれる=表と裏が見て分かる(UI に頼らない)
	float m_scaleTiling  = 10.0f;				// マスクを UV に何回繰り返すか(大=細かい皮)
	float m_scaleSoft    = 0.08f;				// 剥がれ際のぼかし幅(マスクの灰度単位)
	float m_scaleOpacity = 0.9f;				// 黒皮の不透明度(1=地金を完全に隠す)
	float m_scaleGlow    = 0.10f;				// 熱い時、黒皮が暗い赤でどれだけ光るか(温度の明るさに対する比。皮は断熱層で地金より暗い)
	float m_scaleStart   = 0.35f;				// 叩く前から剥がれている薄い皮(マスク灰度)。大=開局から地金が多く見える=斑な黒皮
	float m_scaleHoldMax = 0.70f;				// 長手セルが完成するまでの剥がれ具合の上限(0..1)。小=未完成の所に皮が多く残る=見分けやすい
	//--- 打撃の跡の光(2026-10-08): 叩いた所だけが一瞬明るくなる=「今どこを打ったか」を鉄の上で見せる(UI でない)。
	//    物理の根拠: 塑性変形の仕事は熱に変わる=叩いた所はわずかに温度が上がる(見かけだけ。m_forging の温度は変えない)。
	float m_impactCoord = 0.0f;					// 最後に打った長手位置(セル単位 0..NL。ForgingSim/描画と同じローカル長手規約)
	float m_impactTime  = -1000.0f;				// 最後に打った時刻(m_time)
	float m_impactFlashTime   = 0.35f;			// 光が消えるまでの秒数(F1 Forge)
	float m_impactFlashHeat   = 0.25f;			// 光る所の見かけの温度の上乗せ(HeatRGB の温度単位。大=白く光る)
	float m_impactFlashSpread = 1.0f;			// 光の長手方向の広がり(セル単位の標準偏差)
	float StrikeLenCoord();						// ハンマーの頭が落ちる所 → 刃のローカル長手位置(セル単位 0..NL)
	static constexpr float FACE_DONE_VOLUME      = 0.8f;	// 面(表/裏)が仕上がった瞬間の「完成」の合図(SE_FACE_DONE)の音量
	static constexpr float FACE_DONE_FINAL_PITCH = 1.12f;	// 両面とも済んだ(工程の完了)時は一段高く鳴らす(連続撃破音の様に上がっていく)
	static constexpr float GROOVE_RING_PITCH = 1.12f;		// リズムに乗った打撃の金床音の音程倍率(少し高い=澄んだ「キン」)
	float m_wpHotGain    = 0.85f;				// 熱い鋼の発光全体の明るさ(白飛びで黒皮や形が消えるのを防ぐ)
	int   m_wpN = 0;							// 1段の頂点数
	bool  m_wpOk = false;						// 読み込み成功&段間で頂点数一致
	float m_forgeProg = 0.0f;					// 全体進捗 0..1(=各区域の平均。F1のプレビュー用)
	//--- 成形進度は m_forging が所有(長手セルごとの連続値。区域 NSEG はその集計=HUD/完成判定用)。
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
	float m_wpScale = 0.70f;					// 追加スケール倍率(AABBフィットにさらに掛ける)。ユーザーが F1 で決めた値(2026-10-07: 水槽に収まる+短剣らしい長さ)
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
	//    tongs=左腰に掛けた火钳(HipPoint)の方へ振り向く / grip=刃(砧面アンカー)へ寄って見下ろす。
	//    目標点は体の位置/アンカーから取る=配置を変えても運鏡が自動で追従(座標のベタ書き無し)。
	float m_camTongsW = 0.0f;					// 火钳方向への重み 0..1(TongsOut/PutBack の曲線で決まる)
	float m_camGripW  = 0.0f;					// 刃への寄りの重み 0..1(Gripping で上がり、Ready で戻る)
	bool  m_tongsInHand = false;				// 翻面で火钳を手に取っている(=腰の火钳モデルを描かない)
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
	//--- 熱い鋼の見え方(発光していても形が読める様に。F1「Weapon」→ Hot steel)
	float m_wpHotShade = 0.35f;					// 光の当たらない面の明るさ(0..1。小=明暗が強い=倒角/表裏がはっきり)
	float m_wpRimK     = 0.6f;					// 縁(輪郭/稜線)を温度色で明るくする強さ
	float m_wpRimPow   = 3.0f;					// 縁の明るさがどれだけ縁だけに寄るか(大=細い線)
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
		bool              hidden = false;		// 一時的に描かない(描画/互動/衝突から外す)
		DirectX::XMFLOAT3 aabbMin = { 0,0,0 };	// モデル空間AABB(Loadでキャッシュ)
		DirectX::XMFLOAT3 aabbMax = { 0,0,0 };
		std::vector<Collision2D::Hull> hullsLocal;	// 衝突用: モデル空間の頂点を床(XZ)へ投影した凸包(塊ごとなら複数)。空=ぶつからない
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
	static constexpr float TITLE_WINDUP   = 0.45f;	// 叩く前に鎚を振りかぶる時間(秒。間隔の終わりのこの間だけ持ち上げる)
	static constexpr float TITLE_STRIKE_VOLUME = 0.6f;	// タイトルの自動打撃の金床音(ゲーム中の打撃より控えめ)

	//--- タイトル画面 = ゲーム世界そのもの(別の絵ではない)。固定の標題カメラから、金床で鎚が打ち続ける様子を映す。
	//    SPACE → ロゴが淡出しながら、カメラが標題の位置から金床の工位へゆっくり移る(導入運鏡)→ 鍛打から開始。
	float m_titleCamPos[3]  = { 0.2f, 1.9f, -4.8f };	// 標題カメラの位置(F1「Title」で取景→forge_tuning.txt に保存)
	float m_titleCamLook[3] = { 0.2f, 1.1f,  0.3f };	// 標題カメラの注視点
	float m_titleIntroTime  = 3.0f;					// ロゴが消えた後、標題→金床へカメラが移る時間(秒。長い=ゆっくり)
	float m_logoFadeTime    = 1.0f;					// SPACE 後、ロゴが線形に淡出する時間(秒)。カメラはこの後に動き出す
	//    導入の段階(ユーザーの演出指示: 一つ終わってから次へ):
	//      LogoFade   : ロゴ(と開始プロンプト)が淡出 + 効果音。まだタイトル(鎚は打ち続ける)。
	//      CameraMove : カメラが金床へ移る。タイトル BGM はカメラの進みに合わせて音量 1→0。HUD はまだ出さない。
	//      到着       : タイトル BGM を止め、ゲーム BGM を開始 → None(鍛打の操作開始)。
	enum class IntroPhase { None, LogoFade, CameraMove };
	IntroPhase m_introPhase = IntroPhase::None;
	float m_introTimer = 0.0f;						// LogoFade の経過秒
	static constexpr float TITLE_BGM_VOLUME     = 0.40f;	// タイトル BGM の音量(淡出はここから 0 へ)
	static constexpr float PLAY_BGM_VOLUME      = 0.45f;	// ゲーム中 BGM の音量
	static constexpr float TITLE_FADE_SE_VOLUME = 1.0f;		// ロゴ淡出の効果音の音量
	void  UpdateIntro(float tick);					// 導入の段階を進める(LogoFade→CameraMove、BGM の淡出)
	void  FinishIntro();							// 金床に着いた: BGM を切り替えて導入を終える
	void  ApplyTitleCamera();						// 標題カメラを適用
	void  ResetTitleStage();						// タイトルの舞台: 金床の上に新しい熱い鉄(起動時/結果→タイトル)
	void  UpdateTitleHammer(float tick);			// タイトル: 鎚が一定間隔で振りかぶって打つ(火花+金床音)
	void  UpdateHammerFollow(float tick);			// 鎚の横位置(XZ)を照準点へ平滑追従(タイトル/鍛造で共用)
	void  DrawTitleLogo(float alpha);				// ロゴ「FORGE」(左上。alpha で淡出)

	//--- 指引 UI(宏観チュートリアル, HUD.cpp)。「今どの工程か / 次にどこへ行くか」だけを示す。
	//    「どこを叩け」等の微観の指示は出さない(KCD式: 誤りだけ主人公の独白で知らせる)。
	//    文言・工程名は配方(WeaponRecipe)が持つ=換武器で自動的に変わる。
	float m_stepChangedAt = -1000.0f;				// 工程が変わった時刻(m_time)。工程リストの強調に使う
	//    案内文は「工程」だけでなく「今の状況」で変わる(コンテキストヒント): 鍛造中に鉄が冷めた→炉へ、
	//    片面が仕上がった→裏返せ、炉で熱くなった→金床へ…。工程(配方)より細かい「今やること」を示す。
	const char* m_guideText = "";					// 今の案内文(UpdateGuide が決める)
	Station     m_guideGoal = Station::Anvil;		// 今向かうべき工位(目印の行き先)
	float       m_guideChangedAt = -1000.0f;		// 案内文が変わった時刻(m_time)。淡入に使う
	void  UpdateGuide();							// 状況から m_guideText / m_guideGoal を決める(毎フレーム, Update)
	const char* GuideFor(Station& goal) const;		// その判定本体(優先度の高い状況から順に見る)
	void  DrawStepTracker();						// 画面左: 工程リスト(済=●/今=強調/未=○)。クエストトラッカー
	//    工程リストの各行の「済」状態。済は一度きりではなく「今も成り立っているか」で毎フレーム判定する
	//    (例: 鍛造中に鉄が冷めた → 「加熱」の行は済でなくなる)。
	//    前フレームと比べて切り替わった瞬間(エッジ検出)の時刻を覚え、取り消し線を引く/消すアニメに使う。
	struct TrackerRow
	{
		bool  completed = false;		// 今この行は済か
		float changedAt = -1000.0f;		// completed が切り替わった時刻(m_time)。アニメの起点
	};
	std::vector<TrackerRow> m_trackerRows;			// 配方の工程と同じ数(UpdateTracker が合わせる)
	void  UpdateTracker();							// 各行の completed を判定し、切り替わりを検出(毎フレーム)
	bool  RowCompleted(int i, bool wasCompleted) const;	// i 行目は今「済」か(加熱の行は温度で生きている)
	float ReadyTemp(StepName next) const;			// その工程を始めるのに十分な温度(加熱の行が「済」になる)
	float MinWorkTemp(StepName next) const;		// その工程ができる最低温度(下回ると加熱の行が「済」でなくなる)
	//    操作説明=キーアイコン(Kenney Input Prompts, CC0)+一言。アイコン名は表(データ)で持つ。
	struct KeyHint
	{
		static const int MAX_ICONS = 4;			// 1つの説明に並べるアイコンの最大数(WASD=4)
		const char* icons[MAX_ICONS];			// アイコンのファイル名(拡張子なし, 例 "mouse_left")。nullptr で終わり
		const char* label;						// 何が起きるか(UTF-8)
	};
	void  DrawKeyHints(const KeyHint* hints, int count, float yRatio, float alpha = 1.0f);	// 中央揃えで1行に並べる
	//--- 淬火で揺する操作の大きな動く案内: マウスの絵が上下に動き、上下の矢印が動く向きに合わせて光る(動画型の操作案内)。
	//    蒸気の膜を破るまで(=操作を覚えるまで)だけ出し、破れたら淡出する。小さな操作ガイド(画面下)は残る。
	float m_stirPromptAlpha = 0.0f;
	void  DrawStirPrompt();
	enum class MouseAxis { Vertical, Horizontal };
	void  DrawMousePrompt(MouseAxis axis, int towards, float alpha);	// 動くマウスの案内(towards: 0 往復 / −1 上・左 / +1 下・右)
	//--- 研ぎの自適応の案内(ユーザー要望 2026-10-07): 同じ誤りを m_hintAfterMistakes 回くり返したら直し方を見せる。
	//    上手な人には出ない=邪魔をしない。正しくできた(研げた)瞬間に消え、回数も 0 に戻る。
	enum class GrindHint { None, TiltUp, TiltDown, Slide };
	GrindHint m_grindHint      = GrindHint::None;	// 今出す案内
	GrindHint m_grindHintShown = GrindHint::None;	// 淡出中も描く為に、最後に出した案内を覚えておく
	float m_grindHintAlpha     = 0.0f;
	int   m_grindFlatMistakes  = 0;					// 「寝かせすぎ」をくり返した回数
	int   m_grindSteepMistakes = 0;					// 「立てすぎ」〃
	int   m_grindDoneMistakes  = 0;					// 「研ぎ上がった所を研ぐ」〃
	int   m_hintAfterMistakes  = 2;					// 何回くり返したら案内を出すか(F1 Stations。tuning キー hintafter)
	void  DrawGrindHint();
	void  CountGrindMistake(int& count, GrindHint hint);	// 誤りを1回数え、規定回数に達したら案内を出す
	void  ClearGrindHint();							// 正しくできた: 案内を消し、回数を 0 に戻す
	bool  GuideTarget(DirectX::XMFLOAT3& pos, const char*& label);	// 走動中に次に向かう点(鉄 or 工位)。無ければ false
	void  DrawObjectiveMarker();					// 向かう点の上に目印。画面外なら画面端に矢印(オフスクリーンインジケーター)

	//--- 温度パラメータ(加熱速度は上の COAL_HEAT_RATE / BELLOWS_HEAT_RATE)
	// 打撃CDは調整しやすいようメンバー変数(m_strikeCDMax)。自然冷却速度は m_forging.coolRate
	static constexpr float IDEAL_MIN = 0.55f;	// 最適温度帯(下限)
	static constexpr float IDEAL_MAX = 0.85f;	// 最適温度帯(上限)
	static constexpr float OVERHEAT  = 0.92f;	// これ以上は過熱(鋼を痛める)
	static constexpr float FORGE_READY_MARGIN = 0.05f;	// 加熱の行が「済」になるのは緑帯の下端よりこれだけ上(済/未済のチラつき防止)
	// 淬火できる温度の上限。過熱した鋼を急冷すると結晶が粗く脆くなり、淬割れ(焼き割れ)する。
	//   → 淬火は「QUENCH_MIN_TEMP 以上、QUENCH_MAX_TEMP 以下」の窓の中だけ(ユーザー同意 2026-10-02)。
	static constexpr float QUENCH_MAX_TEMP = OVERHEAT;
	// 開局の鉄の温度 = 適温帯の上限(ユーザー決定)。タイトルで打ち続けていた「熱い鉄」をそのまま受け継ぐ。
	//   BURN_TEMP 以上なので配方の最初の「加熱」工程は即完了=鍛打から始まる(配方は変えない)。
	static constexpr float START_HEAT = IDEAL_MAX;
	// 炭火だけの温度は過熱より上でなければならない(=炉に置いたままなら上がり続けて過熱する。炉が勝手に温度を保たない)。
	// 調整で崩したらコンパイルエラーで気付ける様にする。
	static_assert(COAL_FIRE_TEMP > OVERHEAT && COAL_FIRE_TEMP <= BELLOWS_FIRE_TEMP,
	              "COAL_FIRE_TEMP must be above OVERHEAT (iron left in the fire keeps heating) and not above the bellows fire");
	// 開局の温度が燃える温度を下回ると、最初の「加熱」工程が即完了せず「鍛打から開始」が崩れる。
	static_assert(START_HEAT >= ForgingSim::BURN_TEMP && START_HEAT < OVERHEAT,
	              "START_HEAT must be burning-hot (skips the first Heat step) but not overheated");

	//--- 打撃パラメータ
	static constexpr float CHARGE_RATE = 1.6f;	// 蓄力速度(/秒, 満蓄力まで約0.6秒)
	static constexpr float STRIKE_COOL = 0.02f;	// 1打ごとに下がる温度(燃える温度から約25打で冷たくなる)
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
	static constexpr float GRADE_W_MATCH   = 0.40f;	// 出来栄えに占める「形の一致度」の重み
	static constexpr float GRADE_W_QUALITY = 0.50f;	// 同「打撃品質の平均」の重み(綺麗な打鉄を主に評価)
	static constexpr float GRADE_W_QUENCH  = 0.10f;	// 同「淬火の出来」の重み(小さく。ユーザー決定 2026-10-07。3つの和 = 1)
	static constexpr float GRADE_S = 0.90f;	// この出来栄え以上で S
	static constexpr float GRADE_A = 0.75f;	// 〃 A
	static constexpr float GRADE_B = 0.55f;	// 〃 B (未満は C)
};

#endif // __SCENE_FORGE_H__

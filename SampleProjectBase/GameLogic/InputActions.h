#pragma once
#include <Windows.h>
#undef max
#undef min

//====================================================================
//  入力の抽象化(アクションマッピング / input action mapping)。2026-10-12
//
//  ゲームのコードは「どのキーが押されたか」でなく「どの動作(Action)をしたか」を聞く:
//      旧: IsKeyPress('R')                 → 新: InputActions::Held(Action::Bellows)
//  どのキー/ボタンがどの動作かは下の BINDINGS 表(データ)だけが知っている。
//  利点:
//    ・キー配置を変える = 表を1行直すだけ(ゲームのコードは触らない)
//    ・手柄(ゲームパッド)対応 = 表の右の列に手柄のボタンを書くだけ(阶段 D-1。読み取りは Gamepad.h)
//    ・操作案内のアイコンも同じ表から引ける=案内と実際のキーが食い違わない
//  Unity の Input System / UE の Enhanced Input と同じ考え方。検索: "input action mapping"
//
//  対象はゲームの操作だけ。開発用のキー(F2〜F5 の設計者用パネル / P G K 等の診断)は Input.h を直接使う。
//  2軸の操作(視点/狙い/研ぎ/揺する): マウスは SceneForge::ReadMouseDelta、スティックは下の LookDelta/HandDelta・〜StickY。
//  どの操作をどちらのスティックにするかは使う側(工位ごと)が決める(左 = 手 / 右 = 視点・回す)。
//====================================================================

enum class PadButton;	// Gamepad.h(ここでは名前だけ使う)

// ゲームの動作。名前は「何をするか」(キーの名前ではない)。
enum class Action
{
	Use,			// 手で行う主動作: 叩く(長押し=溜める) / 刃を水に入れる
	Interact,		// 工位に入る・鉄を置く・扉 / 工位から離れる
	Grab,			// 火ばさみで鉄を掴む(2026-10-17 試遊: 「使う」と同じキーだと、狙いが少しずれただけで工位に入ってしまった)
	Bellows,		// ふいごを踏む(炉。長押し)
	Pedal,			// 砥石のペダルを踏む(連打)
	GrindPress,		// 刃を砥石に押し当てる(長押し)。手柄では Use と別の指(ペダルの連打を利き指の RT へ回すため)
	Flip,			// 刃を裏返す(長押しで回る)
	Pause,			// 一時停止メニューを開く/閉じる
	Back,			// メニュー: 戻る(手柄の B。ゲーム中は何もしない)
	MenuUp,			// メニュー: 上へ
	MenuDown,		// メニュー: 下へ
	MenuLeft,		// メニュー: 値を減らす(設定の ◀)
	MenuRight,		// メニュー: 値を増やす(設定の ▶)
	Confirm,		// メニュー/タイトル: 決定
	MoveForward,	// 歩く: 前
	MoveBack,		// 歩く: 後ろ
	MoveLeft,		// 歩く: 左
	MoveRight,		// 歩く: 右
	Count
};

namespace InputActions
{
	bool  Held(Action a);		// 今押されている(長押しの判定)
	bool  Pressed(Action a);	// このフレームで押された瞬間(1回だけ true)
	bool  Released(Action a);	// このフレームで離された瞬間(割り当てたキーが全部離れた時)
	float MoveX();				// 歩く: 左右 -1..1(右が +)
	float MoveY();				// 歩く: 前後 -1..1(前が +)
	// 手柄のスティックの役割(手柄の約束事): 左 = 手(自分が動かす物: 歩く・狙う・刃を滑らせる・揺する) / 右 = 視点・回す(見回す・刃の角度)
	// ① 速度制御(rate control): 倒している間ずっと動く。「このフレームにマウスを動かした量(px)」に換算して返す
	//    = マウス用の感度(視点/狙い/研ぎ)をそのまま使える。狙い・滑らせる・見回す の様に「位置を少しずつ動かす」操作用
	void  LookDelta(float& dx, float& dy);	// 右スティック
	void  HandDelta(float& dx, float& dy);	// 左スティック
	// ② 位置制御(position control): 倒した深さ -1..1 がそのまま位置。離すと中央へ戻る(スティックのバネ)
	//    = 「刃をこの角度に保つ」「上下に揺する」の様に、手の位置そのものを表す操作用。上が +
	float LookStickY();						// 右スティックの上下
	float HandStickY();						// 左スティックの上下

	// 割り当て表を読む(操作案内の絵/呼び名を表から作る=案内と実際のキーが食い違わない。InputPrompts が使う)
	int   BoundKeys(Action a, BYTE out[], int max);			// キー(VK_*)。戻り値=個数
	int   BoundPads(Action a, PadButton out[], int max);		// 手柄のボタン。戻り値=個数
}

#pragma once
#include "InputActions.h"

//====================================================================
//  操作案内の「絵と呼び名」を、今使っている機器に合わせて出す。2026-10-13 阶段 D-1b
//  動的ボタンプロンプト(dynamic button prompts)。検索: "dynamic input prompts last used device"
//
//  ・最後に触った機器を覚える: キー/マウスを触ればキーボード・マウス、手柄を触れば手柄の絵に切り替わる
//    (Steam のゲームと同じ。途中で持ち替えてもその場で切り替わる)
//  ・絵は Action から引く: 表に "keyboard_e" と書かず Action::Interact と書く。
//    どのキー/ボタンの絵かは InputActions の割り当て表から作る = 案内と実際のキーが食い違わない
//  ・文の中のキー名は {Interact} の様な印で書き、出す時に今の機器の呼び名(「Eキー」/「Aボタン」)へ置き換える
//
//  役割の分け方: InputActions =「どの動作をしたか」(ゲームが聞く) / InputPrompts =「それをどう見せるか」(UI が聞く)
//====================================================================

enum class InputDevice
{
	KeyboardMouse,
	Gamepad,
};

// ボタンではない2軸の操作(スティック/マウス移動)。案内の絵だけに使う。
// 手柄では「左 = 手(自分が動かす物)/ 右 = 視点・回す」に分ける。マウスはどれもマウス。
enum class AxisPrompt
{
	Move,			// 歩く: WASD / 左スティック
	Look,			// 見回す: マウス移動 / 右スティック
	Aim,			// 金床で狙う(刃の長手に沿って): マウス移動 / 左スティック上下
	HandVertical,	// 手を上下(水の中で揺する): マウス上下 / 左スティック上下
	HandHorizontal,	// 手を左右(刃を砥石の上で滑らせる): マウス左右 / 左スティック左右
	TiltVertical,	// 刃を傾ける(研ぎの角度): マウス上下 / 右スティック上下
};

// 案内の1項目が指す物 = ボタンの動作(Action) か 2軸の操作(AxisPrompt)。どちらからでも暗黙に作れる。
struct InputGlyph
{
	bool       isAxis;
	Action     action;
	AxisPrompt axis;
	constexpr InputGlyph(Action a)     : isAxis(false), action(a), axis(AxisPrompt::Move) {}
	constexpr InputGlyph(AxisPrompt x) : isAxis(true),  action(Action::Count), axis(x) {}
};

namespace InputPrompts
{
	void        Update();			// 毎フレーム1回(入力の更新の後)。最後に触った機器を判定する
	InputDevice Device();			// 今の案内に使う機器
	void        SyncCursor();		// ゲームがカーソルを動かした(SetCursorPos)直後に呼ぶ = それを「マウスを触った」と数えない

	// 絵(アイコン)。返すのはファイルのパスから ".png" を除いた物(例 ".../xbox_rt")。押していない絵は + "_outline"。
	int         Icons(InputGlyph g, const char* out[], int max);	// 並べる全部(例 ペダル=右クリック+Space / 歩く=WASD)。戻り値=個数
	const char* Icon(InputGlyph g);									// 1枚目(教学の大きな絵)
	const char* PointerIcon(AxisPrompt a);	// その操作の「本体」の絵(マウス本体 / 左か右のスティック)。動かして見せる教学に使う
	const char* ShortName(Action a);// 一番短い呼び名(物の上の丸に入れる1文字: 「E」/「A」)

	// 文の中の {Use} {Interact} {Bellows} {Pedal} {GrindPress} {Flip} {Look} {Move} {Hand} {Tilt} を、今の機器の呼び名へ置き換える。
	//   {Hand} = 手で動かす物(マウス / 左スティック)、{Tilt} = 回す物(マウス / 右スティック)
	// 印の無い文はそのまま返す。返す文字列は次に同じ文を同じ機器で頼まれるまで有効(中で覚えておく)。
	const char* Format(const char* text);
}

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
//    ・手柄(ゲームパッド)対応 = 表にボタンを足し、入力元を1つ増やすだけ(阶段 D)
//    ・操作案内のアイコンも同じ表から引ける=案内と実際のキーが食い違わない
//  Unity の Input System / UE の Enhanced Input と同じ考え方。検索: "input action mapping"
//
//  対象はゲームの操作だけ。開発用のキー(F1 デバッグ / P G K 等の診断)は Input.h を直接使う。
//  マウスの移動量(視点/狙い/研ぎ/揺する)は今まで通り SceneForge::ReadMouseDelta。手柄のスティックは阶段 D で足す。
//====================================================================

// ゲームの動作。名前は「何をするか」(キーの名前ではない)。
enum class Action
{
	Use,			// 手で行う主動作: 叩く(長押し=溜める) / 刃を砥石に当てる / 刃を水に入れる
	Interact,		// 物を使う・掴む・置く / 工位から離れる
	Bellows,		// ふいごを踏む(炉。長押し)
	Pedal,			// 砥石のペダルを踏む(連打)
	Flip,			// 刃を裏返す(長押しで回る)
	Pause,			// 一時停止メニューを開く/閉じる
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
}

#pragma once

//====================================================================
//  ゲームパッド(手柄)の読み取り。2026-10-13 阶段 D-1
//
//  中身は SDL3 の Gamepad API(SDL_gamepad.h)。このヘッダーには SDL を出さない
//  = SDL を知っているのは Gamepad.cpp だけ。InputActions はボタンの名前(PadButton)と
//  -1..1 の軸だけを見る。ライブラリを替えても直すのは Gamepad.cpp だけで済む。
//
//  なぜ XInput でなく SDL3 か(選定理由):
//    ・XInput は Xbox 系しか読めず、陀螺仪(ジャイロ)も扳机の震動(インパルストリガー)も取れない
//    ・SDL3 は Xbox / PS / Switch を同じコードで読め、ジャイロ・扳机震動も同じ API で取れる
//    → 今は Xbox だけでも、研ぎの「手首で角度」(ジャイロ)と打撃の扳机震動を後で足せる
//
//  ボタン名は位置で呼ぶ(A=下 / B=右 / X=左 / Y=上)。SDL も同じ考え(SOUTH/EAST/WEST/NORTH)。
//====================================================================

enum class PadButton
{
	None = 0,		// 空き(割り当て表の空欄)
	A, B, X, Y,		// 右の4ボタン(Xbox の表記)
	LB, RB,			// 肩ボタン
	LT, RT,			// 扳机(アナログ)。しきい値でボタンとしても読む
	Back, Start,	// ビュー / メニュー
	LStick, RStick,	// スティック押し込み
	DpadUp, DpadDown, DpadLeft, DpadRight,
	LStickUp, LStickDown, LStickLeft, LStickRight,	// 左スティックを倒した方向(メニュー用)。しきい値でボタンとして読む
	Count
};

namespace Gamepad
{
	bool  Init();				// SDL の Gamepad を起動(失敗しても false を返すだけ=キーボードで遊べる)
	void  Uninit();
	void  Update(float dt);		// 毎フレーム1回(Main の UpdateInput の直後)

	bool  Connected();			// 手柄が1台つながっているか
	bool  Held(PadButton b);	// 今押されている
	bool  Pressed(PadButton b);	// このフレームで押された瞬間
	bool  Released(PadButton b);// このフレームで離された瞬間

	// スティック: デッドゾーンを除いて -1..1。上が +(画面の y とは逆)
	float LeftX();
	float LeftY();
	float RightX();
	float RightY();
	// 扳机: 0(離す)..1(奥まで)
	float LeftTrigger();
	float RightTrigger();

	float LastDt();				// 直前の Update の dt(スティックの「秒あたり」を「このフレームの量」に直す時に使う)

	// 震動のモーターの強さ 0..1(low=左の重い錘 / high=右の軽い錘 / 扳机の中のモーター)。
	// 毎フレーム呼んでよい(変わった時と、一定間隔でだけ実際に送る)。混ぜるのは Haptics の仕事。
	void  SetRumble(float low, float high, float leftTrigger, float rightTrigger);
}

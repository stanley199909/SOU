#include "Gamepad.h"
#include <Windows.h>
#undef max
#undef min
#include <cmath>
#include <SDL3/SDL.h>
#pragma comment(lib, "ThirdParty/SDL3/lib/x64/SDL3.lib")

namespace
{
	//--- 調整値
	const float STICK_DEADZONE     = 0.20f;	// スティックの遊び(円形デッドゾーン)。中立でも少しずれて返るので、この半径以下は 0 とみなす
	const float DIGITAL_PRESS_AT   = 0.50f;	// 扳机/スティックを「ボタン」として読む: ここを越えたら押した
	const float DIGITAL_RELEASE_AT = 0.30f;	// ここを下回ったら離した。押す側より低くしてチラつき(連打扱い)を防ぐ=ヒステリシス
	const float RESCAN_INTERVAL    = 1.0f;	// 手柄が無い時、何秒ごとに接続を探すか(毎フレームだと配列の確保が無駄)
	// 震動の送り方: SDL の震動は「この長さだけ震えて止まる」命令。毎回少し長めに送り、途中で送り直して途切れさせない。
	//   ゲームが固まった(ブレークポイント等)時は送り直しが止まるので、RUMBLE_SEND_MS 後に手柄も自然に止まる=震えっぱなしにならない
	const Uint32 RUMBLE_SEND_MS    = 200;	// 1回の命令で震える長さ(ms)
	const float  RUMBLE_RESEND_SEC = 0.1f;	// 同じ強さでも、この秒ごとに送り直す(上の長さより短く)
	const float  RUMBLE_CHANGE_EPS = 0.01f;	// これより小さい変化は送らない(毎フレームの微小な揺れで命令を出し続けない)
	const float  MOTOR_MAX         = 65535.0f;	// SDL のモーターの強さの最大(Uint16)

	SDL_Gamepad* s_pad = nullptr;			// 今使っている手柄(1台だけ。2台目以降は使わない)
	bool  s_ok = false;						// SDL の起動に成功したか
	float s_rescanTimer = 0.0f;
	float s_dt = 0.0f;

	// 軸の値(デッドゾーン処理後)
	float s_lx = 0, s_ly = 0, s_rx = 0, s_ry = 0, s_lt = 0, s_rt = 0;

	// ボタンの今/前フレーム
	bool s_now[(int)PadButton::Count] = {};
	bool s_old[(int)PadButton::Count] = {};

	// 最後に送った震動(4つのモーター)と、送ってからの秒
	float s_sentRumble[4] = {};
	float s_sinceRumble   = 0.0f;

	// PadButton → SDL のボタン(物理ボタンのある物だけ)
	SDL_GamepadButton ToSDL(PadButton b)
	{
		switch (b)
		{
		case PadButton::A:         return SDL_GAMEPAD_BUTTON_SOUTH;
		case PadButton::B:         return SDL_GAMEPAD_BUTTON_EAST;
		case PadButton::X:         return SDL_GAMEPAD_BUTTON_WEST;
		case PadButton::Y:         return SDL_GAMEPAD_BUTTON_NORTH;
		case PadButton::LB:        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
		case PadButton::RB:        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
		case PadButton::Back:      return SDL_GAMEPAD_BUTTON_BACK;
		case PadButton::Start:     return SDL_GAMEPAD_BUTTON_START;
		case PadButton::LStick:    return SDL_GAMEPAD_BUTTON_LEFT_STICK;
		case PadButton::RStick:    return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
		case PadButton::DpadUp:    return SDL_GAMEPAD_BUTTON_DPAD_UP;
		case PadButton::DpadDown:  return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
		case PadButton::DpadLeft:  return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
		case PadButton::DpadRight: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
		default:                   return SDL_GAMEPAD_BUTTON_INVALID;	// 扳机・スティック方向は軸から作る
		}
	}

	// SDL の軸(-32768..32767)→ -1..1
	float AxisRaw(SDL_GamepadAxis a)
	{
		return (float)SDL_GetGamepadAxis(s_pad, a) / (float)SDL_JOYSTICK_AXIS_MAX;
	}

	// 円形デッドゾーン: 2軸をまとめて長さで判定し、遊びの外側を 0..1 に引き伸ばす。
	//   軸ごとに切る(四角いデッドゾーン)と、斜めに倒した時に片方の軸だけ 0 になり「十字にくっつく」。
	//   引き伸ばさないと、遊びを抜けた瞬間に 0 → 0.2 へ跳ぶ。
	void RadialDeadzone(float& x, float& y)
	{
		const float len = sqrtf(x * x + y * y);
		if (len <= STICK_DEADZONE) { x = y = 0.0f; return; }
		const float scaled = fminf((len - STICK_DEADZONE) / (1.0f - STICK_DEADZONE), 1.0f);
		x = x / len * scaled;
		y = y / len * scaled;
	}

	// アナログ値をボタンとして読む(ヒステリシス付き)。wasDown = 前フレームの結果
	bool AnalogAsButton(float v, bool wasDown)
	{
		return wasDown ? (v > DIGITAL_RELEASE_AT) : (v > DIGITAL_PRESS_AT);
	}

	void ClearState()
	{
		s_lx = s_ly = s_rx = s_ry = s_lt = s_rt = 0.0f;
		for (bool& b : s_now) b = false;
	}

	// 手柄が無ければ探してつなぐ。抜かれていたら閉じる。
	void UpdateConnection(float dt)
	{
		if (s_pad && !SDL_GamepadConnected(s_pad))
		{
			SDL_CloseGamepad(s_pad);
			s_pad = nullptr;
		}
		if (s_pad) return;

		s_rescanTimer -= dt;
		if (s_rescanTimer > 0.0f) return;
		s_rescanTimer = RESCAN_INTERVAL;

		int count = 0;
		SDL_JoystickID* ids = SDL_GetGamepads(&count);
		if (ids && count > 0) s_pad = SDL_OpenGamepad(ids[0]);
		for (float& v : s_sentRumble) v = 0.0f;	// つなぎ直した手柄は止まっている
		SDL_free(ids);
	}
}

namespace Gamepad
{
	bool Init()
	{
		// SDL のウィンドウを使わない(描画は自前の DX11)。SDL は自分のウィンドウが無いと
		// 「アプリが前面か」を判断できないので、背景でも入力を受ける設定にし、前面かどうかは下の Update で自分で見る。
		SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
		// 抜き差しの検知を SDL 専用のスレッドで行う(こちらのメッセージループに頼らない)
		SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");

		s_ok = SDL_Init(SDL_INIT_GAMEPAD);
		if (!s_ok) return false;
		// イベントは使わず、毎フレーム状態を読む(ポーリング)。イベントを有効のままだと誰も取り出さないキューが溜まる。
		// 無効にした場合は SDL_UpdateGamepads を自分で呼ぶ必要がある(Update で呼ぶ)。
		SDL_SetGamepadEventsEnabled(false);
		s_rescanTimer = 0.0f;	// 起動直後にすぐ探す
		return true;
	}

	void Uninit()
	{
		if (s_pad) { SDL_CloseGamepad(s_pad); s_pad = nullptr; }
		if (s_ok)  { SDL_QuitSubSystem(SDL_INIT_GAMEPAD); SDL_Quit(); s_ok = false; }
	}

	void Update(float dt)
	{
		s_dt = dt;
		for (int i = 0; i < (int)PadButton::Count; ++i) s_old[i] = s_now[i];
		if (!s_ok) { ClearState(); return; }

		SDL_UpdateGamepads();
		UpdateConnection(dt);

		// 手柄が無い / このゲームのウィンドウが前面でない(別アプリを操作中)なら、何も押していない扱い
		if (!s_pad || !GetActiveWindow()) { ClearState(); return; }

		// --- 軸 ---
		s_lx =  AxisRaw(SDL_GAMEPAD_AXIS_LEFTX);
		s_ly = -AxisRaw(SDL_GAMEPAD_AXIS_LEFTY);	// SDL は下が +。こちらは上を + に揃える
		s_rx =  AxisRaw(SDL_GAMEPAD_AXIS_RIGHTX);
		s_ry = -AxisRaw(SDL_GAMEPAD_AXIS_RIGHTY);
		RadialDeadzone(s_lx, s_ly);
		RadialDeadzone(s_rx, s_ry);
		s_lt = AxisRaw(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);	// 扳机は 0..1(遊びは無いので切らない)
		s_rt = AxisRaw(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

		// --- ボタン ---
		for (int i = 1; i < (int)PadButton::Count; ++i)
		{
			const PadButton b = (PadButton)i;
			const SDL_GamepadButton sb = ToSDL(b);
			if (sb != SDL_GAMEPAD_BUTTON_INVALID) s_now[i] = SDL_GetGamepadButton(s_pad, sb);
		}
		auto analog = [](PadButton b, float v) { s_now[(int)b] = AnalogAsButton(v, s_old[(int)b]); };
		analog(PadButton::LT,          s_lt);
		analog(PadButton::RT,          s_rt);
		analog(PadButton::LStickUp,     s_ly);
		analog(PadButton::LStickDown,  -s_ly);
		analog(PadButton::LStickRight,  s_lx);
		analog(PadButton::LStickLeft,  -s_lx);
	}

	bool  Connected()            { return s_pad != nullptr; }
	bool  Held(PadButton b)      { return b != PadButton::None && s_now[(int)b]; }
	bool  Pressed(PadButton b)   { return b != PadButton::None && s_now[(int)b] && !s_old[(int)b]; }
	bool  Released(PadButton b)  { return b != PadButton::None && !s_now[(int)b] && s_old[(int)b]; }
	float LeftX()                { return s_lx; }
	float LeftY()                { return s_ly; }
	float RightX()               { return s_rx; }
	float RightY()               { return s_ry; }
	float LeftTrigger()          { return s_lt; }
	float RightTrigger()         { return s_rt; }
	float LastDt()               { return s_dt; }

	void SetRumble(float low, float high, float leftTrigger, float rightTrigger)
	{
		if (!s_pad) return;
		// 前面でない(別のアプリを操作中)なら止める
		if (!GetActiveWindow()) low = high = leftTrigger = rightTrigger = 0.0f;
		const float want[4] = { low, high, leftTrigger, rightTrigger };

		s_sinceRumble += s_dt;
		bool changed = false;
		for (int i = 0; i < 4; ++i)
			if (fabsf(want[i] - s_sentRumble[i]) > RUMBLE_CHANGE_EPS) changed = true;
		const bool anyOn = want[0] > 0.0f || want[1] > 0.0f || want[2] > 0.0f || want[3] > 0.0f;
		const bool keepAlive = anyOn && s_sinceRumble >= RUMBLE_RESEND_SEC;	// 震えている間は途切れない様に送り直す
		if (!changed && !keepAlive) return;

		auto motor = [](float v) { return (Uint16)(fminf(fmaxf(v, 0.0f), 1.0f) * MOTOR_MAX); };
		SDL_RumbleGamepad(s_pad, motor(low), motor(high), RUMBLE_SEND_MS);
		SDL_RumbleGamepadTriggers(s_pad, motor(leftTrigger), motor(rightTrigger), RUMBLE_SEND_MS);	// 扳机のモーターが無い手柄では何もしない(失敗が返るだけ)
		for (int i = 0; i < 4; ++i) s_sentRumble[i] = want[i];
		s_sinceRumble = 0.0f;
	}
}

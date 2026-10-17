#include "InputActions.h"
#include "Input.h"
#include "Gamepad.h"
#include <cmath>

namespace
{
	//--- 動作 → キー/手柄ボタン の割り当て表(データ)。1つの動作に複数を割り当てられる(どれを押しても同じ)。
	//    keys = Windows の仮想キーコード(VK_*)。文字キーは大文字の文字('E' 等)。0 = 空き。
	//    pads = 手柄のボタン(Gamepad.h の PadButton)。None = 空き。
	struct Binding
	{
		static const int MAX_KEYS = 2;
		static const int MAX_PADS = 2;
		Action    action;
		BYTE      keys[MAX_KEYS];
		PadButton pads[MAX_PADS];
	};

	//    手柄の考え方: 右手の扳机(RT) = 力・リズムを出す動作(叩く・ペダルを踏んで砥石を回す)/
	//                  左手の扳机(LT) = 押さえ続ける動作(ふいごを踏み続ける・刃を砥石に押し当て続ける)。
	//    スティック(アナログ)はこの表ではなく下の MoveX/MoveY・LookDelta/HandDelta・〜StickY が読む。
	using P = PadButton;
	constexpr Binding BINDINGS[] =
	{
		{ Action::Use,         { VK_LBUTTON },           { P::RT } },
		{ Action::Interact,    { 'E' },                  { P::A } },
		{ Action::Grab,        { 'Q' },                  { P::X } },
		{ Action::Bellows,     { 'R' },                  { P::LT } },
		{ Action::Pedal,       { VK_RBUTTON, VK_SPACE }, { P::RT } },
		{ Action::GrindPress,  { VK_LBUTTON },           { P::LT } },
		{ Action::Flip,        { 'F' },                  { P::Y } },
		{ Action::Pause,       { VK_ESCAPE },            { P::Start } },
		{ Action::Back,        { VK_ESCAPE },            { P::B } },
		{ Action::MenuUp,      { VK_UP,     'W' },       { P::DpadUp,    P::LStickUp } },
		{ Action::MenuDown,    { VK_DOWN,   'S' },       { P::DpadDown,  P::LStickDown } },
		{ Action::MenuLeft,    { VK_LEFT,   'A' },       { P::DpadLeft,  P::LStickLeft } },
		{ Action::MenuRight,   { VK_RIGHT,  'D' },       { P::DpadRight, P::LStickRight } },
		{ Action::Confirm,     { VK_RETURN, VK_SPACE },  { P::A } },
		{ Action::MoveForward, { 'W' } },
		{ Action::MoveBack,    { 'S' } },
		{ Action::MoveLeft,    { 'A' } },
		{ Action::MoveRight,   { 'D' } },
	};
	static_assert(_countof(BINDINGS) == (size_t)Action::Count, "every Action needs exactly one row in BINDINGS");

	// 表の行は Action の順に並べる=添字(Action の番号)でそのまま引ける。並びが崩れたらコンパイルエラーにする。
	constexpr bool BindingsInOrder()
	{
		for (int i = 0; i < (int)Action::Count; ++i)
			if (BINDINGS[i].action != (Action)i) return false;
		return true;
	}
	static_assert(BindingsInOrder(), "BINDINGS rows must be in the same order as enum Action");
	const Binding& GetBinding(Action a) { return BINDINGS[(int)a]; }

	// 割り当てたキーのどれかが条件を満たすか
	template <class Pred>
	bool AnyKey(Action a, Pred pred)
	{
		const Binding& b = GetBinding(a);
		for (BYTE k : b.keys)
			if (k != 0 && pred(k)) return true;
		return false;
	}
	// 割り当てた手柄ボタンのどれかが条件を満たすか
	template <class Pred>
	bool AnyPad(Action a, Pred pred)
	{
		const Binding& b = GetBinding(a);
		for (PadButton p : b.pads)
			if (p != PadButton::None && pred(p)) return true;
		return false;
	}

	// キーボードだけの -1/0/+1(歩く)
	float KeyAxis(Action plus, Action minus)
	{
		auto held = [](Action a) { return AnyKey(a, [](BYTE k) { return IsKeyPress(k); }); };
		return (held(plus) ? 1.0f : 0.0f) - (held(minus) ? 1.0f : 0.0f);
	}
	float Clamp1(float v) { return fminf(fmaxf(v, -1.0f), 1.0f); }

	//--- スティック → マウスの移動量(px)に換算する時の調整値(速度制御)
	const float PAD_LOOK_PX_PER_SEC = 900.0f;	// 奥まで倒した時、1秒でマウスを何 px 動かしたのと同じにするか(=スティックの速さ)
	const float PAD_LOOK_EXPONENT   = 2.0f;		// 応答曲線(レスポンスカーブ)の指数。2乗=少し倒すと遅く精密、奥まで倒すと速い

	// スティックの倒し(x, y: 上が +)→ このフレームのマウス移動量(px: 下が +)
	void StickToPixels(float x, float y, float& dx, float& dy)
	{
		dx = dy = 0.0f;
		const float len = sqrtf(x * x + y * y);
		if (len <= 0.0f) return;
		// 応答曲線は倒した長さにだけ掛ける(向きは変えない)=斜めに倒しても狙った向きへ動く
		const float curved = powf(fminf(len, 1.0f), PAD_LOOK_EXPONENT);
		const float px = PAD_LOOK_PX_PER_SEC * Gamepad::LastDt() * curved / len;
		dx =  x * px;
		dy = -y * px;	// スティック上(+) = マウスを上へ動かした(画面の y は下が +)
	}
}

namespace InputActions
{
	bool Held(Action a)
	{
		return AnyKey(a, [](BYTE k) { return IsKeyPress(k); })
		    || AnyPad(a, [](PadButton p) { return Gamepad::Held(p); });
	}
	bool Pressed(Action a)
	{
		return AnyKey(a, [](BYTE k) { return IsKeyTrigger(k); })
		    || AnyPad(a, [](PadButton p) { return Gamepad::Pressed(p); });
	}

	// 「離された」= どれかのキー/ボタンが今離れ、かつ他も押されていない(2つ押していて1つ離しただけでは離していない)
	bool Released(Action a)
	{
		const bool any = AnyKey(a, [](BYTE k) { return IsKeyRelease(k); })
		              || AnyPad(a, [](PadButton p) { return Gamepad::Released(p); });
		return any && !Held(a);
	}

	// 歩く: キーボード(-1/0/+1) + 左スティック(アナログ)。両方同時でも -1..1 に収める
	float MoveX() { return Clamp1(KeyAxis(Action::MoveRight,   Action::MoveLeft) + Gamepad::LeftX()); }
	float MoveY() { return Clamp1(KeyAxis(Action::MoveForward, Action::MoveBack) + Gamepad::LeftY()); }

	void  LookDelta(float& dx, float& dy) { StickToPixels(Gamepad::RightX(), Gamepad::RightY(), dx, dy); }
	void  HandDelta(float& dx, float& dy) { StickToPixels(Gamepad::LeftX(),  Gamepad::LeftY(),  dx, dy); }
	float LookStickY() { return Gamepad::RightY(); }
	float HandStickY() { return Gamepad::LeftY(); }

	int BoundKeys(Action a, BYTE out[], int max)
	{
		int n = 0;
		for (BYTE k : GetBinding(a).keys)
			if (k != 0 && n < max) out[n++] = k;
		return n;
	}
	int BoundPads(Action a, PadButton out[], int max)
	{
		int n = 0;
		for (PadButton p : GetBinding(a).pads)
			if (p != PadButton::None && n < max) out[n++] = p;
		return n;
	}
}

#include "InputActions.h"
#include "Input.h"

namespace
{
	//--- 動作 → キー の割り当て表(データ)。1つの動作に複数のキーを割り当てられる(どれを押しても同じ)。
	//    キーは Windows の仮想キーコード(VK_*)。文字キーは大文字の文字('E' 等)。0 = 空き。
	struct Binding
	{
		static const int MAX_KEYS = 2;
		Action action;
		BYTE   keys[MAX_KEYS];
	};

	constexpr Binding BINDINGS[] =
	{
		{ Action::Use,         { VK_LBUTTON } },
		{ Action::Interact,    { 'E' } },
		{ Action::Bellows,     { 'R' } },
		{ Action::Pedal,       { VK_RBUTTON, VK_SPACE } },
		{ Action::Flip,        { 'F' } },
		{ Action::Pause,       { VK_ESCAPE } },
		{ Action::MenuUp,      { VK_UP,     'W' } },
		{ Action::MenuDown,    { VK_DOWN,   'S' } },
		{ Action::MenuLeft,    { VK_LEFT,   'A' } },
		{ Action::MenuRight,   { VK_RIGHT,  'D' } },
		{ Action::Confirm,     { VK_RETURN, VK_SPACE } },
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
}

namespace InputActions
{
	bool Held(Action a)    { return AnyKey(a, [](BYTE k) { return IsKeyPress(k); }); }
	bool Pressed(Action a) { return AnyKey(a, [](BYTE k) { return IsKeyTrigger(k); }); }

	// 「離された」= どれかのキーが今離れ、かつ他のキーも押されていない(2つ押していて1つ離しただけでは離していない)
	bool Released(Action a)
	{
		return AnyKey(a, [](BYTE k) { return IsKeyRelease(k); }) && !Held(a);
	}

	float MoveX() { return (Held(Action::MoveRight) ? 1.0f : 0.0f) - (Held(Action::MoveLeft) ? 1.0f : 0.0f); }
	float MoveY() { return (Held(Action::MoveForward) ? 1.0f : 0.0f) - (Held(Action::MoveBack) ? 1.0f : 0.0f); }
}

#include "InputPrompts.h"
#include "Input.h"
#include "Gamepad.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <cstring>

namespace
{
	//--- アイコンの置き場所(Kenney Input Prompts, CC0)
	#define KBM_DIR "Assets/UI/Keyboard & Mouse/Default/"
	#define PAD_DIR "Assets/UI/Xbox Series/Default/"

	//--- 「触った」とみなす量
	const float PAD_WAKE_AXIS = 0.5f;	// スティック/扳机をここまで倒したら手柄を触った(置いた時のわずかなずれで切り替わらない様に)
	const LONG  MOUSE_WAKE_PX = 4;		// マウスがこの px 以上動いたらマウスを触った(机の振動などで切り替わらない様に)

	InputDevice s_device = InputDevice::KeyboardMouse;	// 起動時はキーボード・マウス(手柄を触れば切り替わる)
	POINT s_cursor = {};
	bool  s_cursorValid = false;
	bool  s_padWasConnected = false;	// 前フレームに手柄がつながっていたか(つながった瞬間を見つける)

	//--- キー(VK_*) → 絵の名前 / 呼び名。文字キーは "keyboard_e" / 「Eキー」を作る
	struct KeyInfo { BYTE vk; const char* icon; const char* name; };
	const KeyInfo SPECIAL_KEYS[] =
	{
		{ VK_LBUTTON, KBM_DIR "mouse_left",          (const char*)u8"左クリック" },
		{ VK_RBUTTON, KBM_DIR "mouse_right",         (const char*)u8"右クリック" },
		{ VK_SPACE,   KBM_DIR "keyboard_space",      "Space" },
		{ VK_ESCAPE,  KBM_DIR "keyboard_escape",     "Esc" },
		{ VK_RETURN,  KBM_DIR "keyboard_enter",      "Enter" },
		{ VK_UP,      KBM_DIR "keyboard_arrow_up",   (const char*)u8"↑" },
		{ VK_DOWN,    KBM_DIR "keyboard_arrow_down", (const char*)u8"↓" },
		{ VK_LEFT,    KBM_DIR "keyboard_arrow_left", (const char*)u8"←" },
		{ VK_RIGHT,   KBM_DIR "keyboard_arrow_right",(const char*)u8"→" },
	};

	// 文字キーの絵/呼び名は文字から作る。作った文字列は覚えておく(返したポインタがずっと有効な様に)
	struct LetterKey { std::string icon, name, shortName; };
	const LetterKey* Letter(BYTE vk)
	{
		static LetterKey table[26];
		static bool built = false;
		if (!built)
		{
			for (int i = 0; i < 26; ++i)
			{
				const char upper = (char)('A' + i), lower = (char)('a' + i);
				table[i].icon      = std::string(KBM_DIR "keyboard_") + lower;
				table[i].name      = std::string(1, upper) + (const char*)u8"キー";
				table[i].shortName = std::string(1, upper);
			}
			built = true;
		}
		return (vk >= 'A' && vk <= 'Z') ? &table[vk - 'A'] : nullptr;
	}
	const char* KeyIcon(BYTE vk)
	{
		for (const KeyInfo& k : SPECIAL_KEYS) if (k.vk == vk) return k.icon;
		const LetterKey* l = Letter(vk);
		return l ? l->icon.c_str() : nullptr;
	}
	const char* KeyName(BYTE vk)
	{
		for (const KeyInfo& k : SPECIAL_KEYS) if (k.vk == vk) return k.name;
		const LetterKey* l = Letter(vk);
		return l ? l->name.c_str() : "?";
	}

	//--- 手柄のボタン → 絵の名前 / 呼び名 / 短い呼び名(行の並びは自由。Pad() が探す)
	struct PadInfo { PadButton b; const char* icon; const char* name; const char* shortName; };
	const PadInfo PAD_INFO[] =
	{
		{ PadButton::None,        nullptr,                    "",                                  "" },
		{ PadButton::A,           PAD_DIR "xbox_button_a",    (const char*)u8"Aボタン",            "A" },
		{ PadButton::B,           PAD_DIR "xbox_button_b",    (const char*)u8"Bボタン",            "B" },
		{ PadButton::X,           PAD_DIR "xbox_button_x",    (const char*)u8"Xボタン",            "X" },
		{ PadButton::Y,           PAD_DIR "xbox_button_y",    (const char*)u8"Yボタン",            "Y" },
		{ PadButton::LB,          PAD_DIR "xbox_lb",          "LB",                                "LB" },
		{ PadButton::RB,          PAD_DIR "xbox_rb",          "RB",                                "RB" },
		{ PadButton::LT,          PAD_DIR "xbox_lt",          "LT",                                "LT" },
		{ PadButton::RT,          PAD_DIR "xbox_rt",          "RT",                                "RT" },
		{ PadButton::Back,        PAD_DIR "xbox_button_view", (const char*)u8"ビューボタン",       "View" },
		{ PadButton::Start,       PAD_DIR "xbox_button_menu", (const char*)u8"メニューボタン",     "Menu" },
		{ PadButton::LStick,      PAD_DIR "xbox_ls",          (const char*)u8"左スティック押し込み", "LS" },
		{ PadButton::RStick,      PAD_DIR "xbox_rs",          (const char*)u8"右スティック押し込み", "RS" },
		{ PadButton::DpadUp,      PAD_DIR "xbox_dpad_up",     (const char*)u8"十字キー↑",          (const char*)u8"↑" },
		{ PadButton::DpadDown,    PAD_DIR "xbox_dpad_down",   (const char*)u8"十字キー↓",          (const char*)u8"↓" },
		{ PadButton::DpadLeft,    PAD_DIR "xbox_dpad_left",   (const char*)u8"十字キー←",          (const char*)u8"←" },
		{ PadButton::DpadRight,   PAD_DIR "xbox_dpad_right",  (const char*)u8"十字キー→",          (const char*)u8"→" },
		{ PadButton::LStickUp,    PAD_DIR "xbox_stick_l_up",  (const char*)u8"左スティック↑",      (const char*)u8"↑" },
		{ PadButton::LStickDown,  PAD_DIR "xbox_stick_l_down",(const char*)u8"左スティック↓",      (const char*)u8"↓" },
		{ PadButton::LStickLeft,  PAD_DIR "xbox_stick_l_left",(const char*)u8"左スティック←",      (const char*)u8"←" },
		{ PadButton::LStickRight, PAD_DIR "xbox_stick_l_right",(const char*)u8"左スティック→",     (const char*)u8"→" },
	};
	static_assert(_countof(PAD_INFO) == (size_t)PadButton::Count, "every PadButton needs one row in PAD_INFO");
	// 表からボタンの行を探す(u8 の文字列を含む表は constexpr にできず、並び順をコンパイル時に確かめられないので、添字でなく探す)
	const PadInfo& Pad(PadButton b)
	{
		for (const PadInfo& p : PAD_INFO) if (p.b == b) return p;
		return PAD_INFO[0];	// 見つからない = 空き(None)
	}

	//--- 2軸の操作の絵と呼び名(機器ごと)。body = 動かして見せる教学の「本体」の絵
	struct AxisInfo { AxisPrompt axis; const char* icons[4]; const char* name; const char* body; };
	const char* const MOUSE_NAME = (const char*)u8"マウス";
	const char* const LSTICK     = (const char*)u8"左スティック";
	const char* const RSTICK     = (const char*)u8"右スティック";
	const AxisInfo AXIS_KBM[] =
	{
		{ AxisPrompt::Move,           { KBM_DIR "keyboard_w", KBM_DIR "keyboard_a", KBM_DIR "keyboard_s", KBM_DIR "keyboard_d" }, (const char*)u8"WASDキー", KBM_DIR "mouse" },
		{ AxisPrompt::Look,           { KBM_DIR "mouse_move" },       MOUSE_NAME, KBM_DIR "mouse" },
		{ AxisPrompt::Aim,            { KBM_DIR "mouse_move" },       MOUSE_NAME, KBM_DIR "mouse" },
		{ AxisPrompt::HandVertical,   { KBM_DIR "mouse_vertical" },   MOUSE_NAME, KBM_DIR "mouse" },
		{ AxisPrompt::HandHorizontal, { KBM_DIR "mouse_horizontal" }, MOUSE_NAME, KBM_DIR "mouse" },
	};
	const AxisInfo AXIS_PAD[] =
	{
		{ AxisPrompt::Move,           { PAD_DIR "xbox_stick_l" },            LSTICK, PAD_DIR "xbox_stick_l" },
		{ AxisPrompt::Look,           { PAD_DIR "xbox_stick_r" },            RSTICK, PAD_DIR "xbox_stick_r" },
		{ AxisPrompt::Aim,            { PAD_DIR "xbox_stick_l_vertical" },   LSTICK, PAD_DIR "xbox_stick_l" },
		{ AxisPrompt::HandVertical,   { PAD_DIR "xbox_stick_l_vertical" },   LSTICK, PAD_DIR "xbox_stick_l" },
		{ AxisPrompt::HandHorizontal, { PAD_DIR "xbox_stick_l_horizontal" }, LSTICK, PAD_DIR "xbox_stick_l" },
	};
	const AxisInfo& Axis(AxisPrompt a)
	{
		const bool pad = (s_device == InputDevice::Gamepad);
		const AxisInfo* table = pad ? AXIS_PAD : AXIS_KBM;
		const size_t n = pad ? _countof(AXIS_PAD) : _countof(AXIS_KBM);
		for (size_t i = 0; i < n; ++i) if (table[i].axis == a) return table[i];
		return table[0];
	}

	//--- 文の中の印 → 置き換える物
	struct Token { const char* tag; InputGlyph glyph; };
	const Token TOKENS[] =
	{
		{ "{Use}",      Action::Use },
		{ "{Interact}", Action::Interact },
		{ "{Grab}",     Action::Grab },
		{ "{Bellows}",  Action::Bellows },
		{ "{Pedal}",    Action::Pedal },
		{ "{GrindPress}", Action::GrindPress },
		{ "{Flip}",     Action::Flip },
		{ "{Look}",     AxisPrompt::Look },
		{ "{Move}",     AxisPrompt::Move },
		{ "{Hand}",     AxisPrompt::HandVertical },	// 呼び名だけ使う(マウス / 左スティック)
	};

	// 動作の呼び名: 1つ目のキー + 残りを括弧で(例 ペダル = 「右クリック(Space)」)。手柄は1つ目のボタンだけ
	std::string ActionName(Action a)
	{
		if (s_device == InputDevice::Gamepad)
		{
			PadButton pads[2];
			return InputActions::BoundPads(a, pads, 2) > 0 ? Pad(pads[0]).name : "?";
		}
		BYTE keys[2];
		const int n = InputActions::BoundKeys(a, keys, 2);
		if (n == 0) return "?";
		std::string s = KeyName(keys[0]);
		if (n > 1) { s += "("; s += KeyName(keys[1]); s += ")"; }
		return s;
	}

	std::string Expand(const char* text)
	{
		std::string s = text;
		for (const Token& t : TOKENS)
		{
			const std::string name = t.glyph.isAxis ? std::string(Axis(t.glyph.axis).name) : ActionName(t.glyph.action);
			for (size_t at = s.find(t.tag); at != std::string::npos; at = s.find(t.tag, at + name.size()))
				s.replace(at, strlen(t.tag), name);
		}
		return s;
	}
}

namespace InputPrompts
{
	void Update()
	{
		// 手柄がつながった瞬間(起動した時に既につながっていた場合も、最初のこの瞬間に当たる)= 手柄で遊ぶつもりと見なす。
		//   外れても損が小さい方へ倒す: 手柄の人にキーボードの案内を出す方が、その逆より困る(マウスを動かせばすぐ戻る)
		const bool padConnected = Gamepad::Connected();
		if (padConnected && !s_padWasConnected) s_device = InputDevice::Gamepad;
		s_padWasConnected = padConnected;

		// 手柄を触った: ボタンを押した瞬間 / スティック・扳机を大きく倒した
		bool pad = false;
		for (int i = 1; i < (int)PadButton::Count; ++i)
			if (Gamepad::Pressed((PadButton)i)) pad = true;
		const float axes[] = { Gamepad::LeftX(), Gamepad::LeftY(), Gamepad::RightX(), Gamepad::RightY(),
		                       Gamepad::LeftTrigger(), Gamepad::RightTrigger() };
		for (float v : axes)
			if (fabsf(v) > PAD_WAKE_AXIS) pad = true;

		// キーボード・マウスを触った: どれかのキー/マウスボタンを押した瞬間 / マウスが動いた
		bool kbm = false;
		for (int vk = 1; vk < 255; ++vk)
			if (IsKeyTrigger((BYTE)vk)) { kbm = true; break; }
		POINT p; GetCursorPos(&p);
		if (s_cursorValid && (labs(p.x - s_cursor.x) >= MOUSE_WAKE_PX || labs(p.y - s_cursor.y) >= MOUSE_WAKE_PX)) kbm = true;
		s_cursor = p; s_cursorValid = true;

		// 両方同時(片手で手柄・片手でマウス等)は切り替えない=案内がチラつかない
		if (pad && !kbm) s_device = InputDevice::Gamepad;
		if (kbm && !pad) s_device = InputDevice::KeyboardMouse;
	}

	InputDevice Device() { return s_device; }

	void SyncCursor()
	{
		GetCursorPos(&s_cursor);
		s_cursorValid = true;
	}

	int Icons(InputGlyph g, const char* out[], int max)
	{
		int n = 0;
		if (g.isAxis)
		{
			for (const char* icon : Axis(g.axis).icons)
				if (icon && n < max) out[n++] = icon;
			return n;
		}
		if (s_device == InputDevice::Gamepad)
		{
			PadButton pads[2];
			if (InputActions::BoundPads(g.action, pads, 2) > 0 && n < max) out[n++] = Pad(pads[0]).icon;	// 手柄は1つ目だけ(十字キーと左スティックを並べても煩い)
			return n;
		}
		BYTE keys[2];
		const int nk = InputActions::BoundKeys(g.action, keys, 2);
		for (int i = 0; i < nk; ++i)
		{
			const char* icon = KeyIcon(keys[i]);
			if (icon && n < max) out[n++] = icon;
		}
		return n;
	}

	const char* Icon(InputGlyph g)
	{
		const char* out[1] = { nullptr };
		Icons(g, out, 1);
		return out[0];
	}

	const char* PointerIcon(AxisPrompt a) { return Axis(a).body; }

	const char* ShortName(Action a)
	{
		if (s_device == InputDevice::Gamepad)
		{
			PadButton pads[2];
			return InputActions::BoundPads(a, pads, 2) > 0 ? Pad(pads[0]).shortName : "?";
		}
		BYTE keys[2];
		if (InputActions::BoundKeys(a, keys, 2) == 0) return "?";
		const LetterKey* l = Letter(keys[0]);
		return l ? l->shortName.c_str() : KeyName(keys[0]);
	}

	const char* Format(const char* text)
	{
		if (!text || !strchr(text, '{')) return text;	// 印の無い文はそのまま(ほとんどの文)
		// 文(リテラルなのでポインタが変わらない)× 機器 ごとに、置き換えた文を1度だけ作って覚える
		static std::unordered_map<const char*, std::string> cache[2];
		std::unordered_map<const char*, std::string>& c = cache[(int)s_device];
		auto it = c.find(text);
		if (it == c.end()) it = c.emplace(text, Expand(text)).first;
		return it->second.c_str();
	}
}

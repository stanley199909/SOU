#include "Haptics.h"
#include "Gamepad.h"
#include "InputPrompts.h"
#include <cmath>
#include <vector>

namespace
{
	const float PULSE_DECAY_EXPONENT = 2.0f;	// 一発の弱まり方: (1−経過/長さ)^2 = 最初が強く、すぐ引く(打撃の「ドン」の形)
	// モーターの不感帯(デッドバンド)の補償: 震動モーターは弱い指令では回らない/手に感じない。
	//   0 より大きい値は MOTOR_MIN_FELT..1 へ引き上げる(0 は 0 のまま=止める)。MOTOR_OFF_BELOW 未満は 0 扱い
	//   (消えかけの小さな値が MIN_FELT に引き上げられて、いつまでもかすかに震え続けない様に)
	const float MOTOR_MIN_FELT  = 0.15f;
	const float MOTOR_OFF_BELOW = 0.02f;
	float CompensateDeadBand(float v)
	{
		if (v < MOTOR_OFF_BELOW) return 0.0f;
		return MOTOR_MIN_FELT + (1.0f - MOTOR_MIN_FELT) * fminf(v, 1.0f);
	}

	struct ActivePulse
	{
		Rumble r;
		float  duration;
		float  age;
	};
	std::vector<ActivePulse> s_pulses;
	Rumble s_hold;					// 今フレームに頼まれた持続の震動(Update で使い、0 に戻す)
	float  s_strength = 1.0f;

	void MaxInto(Rumble& dst, const Rumble& src, float k)
	{
		dst.low          = fmaxf(dst.low,          src.low          * k);
		dst.high         = fmaxf(dst.high,         src.high         * k);
		dst.leftTrigger  = fmaxf(dst.leftTrigger,  src.leftTrigger  * k);
		dst.rightTrigger = fmaxf(dst.rightTrigger, src.rightTrigger * k);
	}
}

namespace Haptics
{
	void Pulse(const Rumble& r, float seconds)
	{
		if (seconds <= 0.0f) return;
		s_pulses.push_back({ r, seconds, 0.0f });
	}

	void Hold(const Rumble& r) { MaxInto(s_hold, r, 1.0f); }

	void Update(float dt)
	{
		// 1) 混ぜる: モーターごとに一番強い物(足すと、重なった時に振り切れて何が起きたか分からなくなる)
		Rumble mix = s_hold;
		for (size_t i = 0; i < s_pulses.size(); )
		{
			ActivePulse& p = s_pulses[i];
			p.age += dt;
			if (p.age >= p.duration) { s_pulses[i] = s_pulses.back(); s_pulses.pop_back(); continue; }
			MaxInto(mix, p.r, powf(1.0f - p.age / p.duration, PULSE_DECAY_EXPONENT));
			++i;
		}
		s_hold = Rumble();	// 持続は「頼まれたフレームだけ」= 呼ぶのを止めれば次のフレームで止まる(一時停止中も自然に止まる)

		// 2) 手柄で遊んでいない時(キーボード・マウスで操作中)は震わせない。机に置いた手柄が勝手に震えない様に
		const float k = (InputPrompts::Device() == InputDevice::Gamepad) ? s_strength : 0.0f;
		Gamepad::SetRumble(CompensateDeadBand(mix.low * k),         CompensateDeadBand(mix.high * k),
		                   CompensateDeadBand(mix.leftTrigger * k), CompensateDeadBand(mix.rightTrigger * k));
	}

	void StopAll()
	{
		s_pulses.clear();
		s_hold = Rumble();
		Gamepad::SetRumble(0.0f, 0.0f, 0.0f, 0.0f);
	}

	void  SetStrength(float s) { s_strength = fminf(fmaxf(s, 0.0f), 1.0f); }
	float Strength()           { return s_strength; }
}

#pragma once

//====================================================================
//  手柄の震動(ハプティクス / haptic feedback)。2026-10-13 阶段 D-2
//
//  ゲームは「何を感じさせたいか」だけを言う:
//      Haptics::Pulse(強さ, 秒)  … 一発(打撃・入水の様に一瞬。時間で弱まって消える)
//      Haptics::Hold(強さ)       … そのフレームだけの持続(研ぎ・ふいごの様に続く物。毎フレーム呼ぶ。呼ばなければ止まる)
//  同時に鳴っている震動を毎フレーム1つに混ぜて(モーターごとに一番強い物=最大値)、手柄へ送る。
//  混ぜるのはここだけ=呼ぶ側は他の震動を気にしなくてよい。検索: "haptic feedback mixing rumble"
//
//  Xbox の手柄のモーターは4つ:
//    low  = 左のグリップ。重い錘=低い「ドン」(重さ・柔らかさ)
//    high = 右のグリップ。軽い錘=高い「ビリッ」(硬さ・細かさ)
//    leftTrigger / rightTrigger = 扳机の中の小さなモーター(インパルストリガー)。押している指先に直接
//====================================================================

struct Rumble
{
	float low          = 0.0f;	// 0..1
	float high         = 0.0f;
	float leftTrigger  = 0.0f;
	float rightTrigger = 0.0f;
};

// 強さを k 倍した震動(研ぎ音の量・沸き立ちの量など、物理の状態に比例させる時)
inline Rumble ScaleRumble(const Rumble& r, float k)
{
	return { r.low * k, r.high * k, r.leftTrigger * k, r.rightTrigger * k };
}

namespace Haptics
{
	void  Pulse(const Rumble& r, float seconds);	// 一発。seconds で 0 まで弱まる
	void  Hold(const Rumble& r);					// このフレームだけの持続(毎フレーム呼ぶ)
	void  Update(float dt);							// 毎フレーム1回、ゲームの更新の後(混ぜて送る)
	void  StopAll();								// 全部止める(タイトルへ戻る時など)

	void  SetStrength(float s);	// 全体の強さ 0..1(設定の「震動」)。0 = オフ
	float Strength();
}

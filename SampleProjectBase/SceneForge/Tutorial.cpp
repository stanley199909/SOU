// Part of SceneForge: 操作の案内(チュートリアル / 救急)。2026-10-12 阶段 C で HUD.cpp から分離。
// 課(Lesson)の表 LESSONS = データ。状況(LessonActive)と「できた」量(LessonMetric)= コード。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "Texture.h"
#include "TextureCache.h"
#include "InputActions.h"
#include "DebugUI.h"
#include "Lerp.h"
#include "Audio.h"
#include <cmath>
#include <string>

using namespace DirectX;

//====================================================================
//  課の表(データ)。順番 = 優先順位(同時に要る課が複数あれば上の物を出す)。
//    need           = 「できた」とみなす LessonMetric の増え量
//    rescueIdle     = 救急: 状況が続いて何秒進まなければ出すか(0 = 使わない)
//    rescueMistakes = 救急: 誤りを何回重ねたら出すか(0 = 使わない)
//  救急の秒数/回数は 2026-10-12 ユーザー決定(叩く 10 秒 / ふいご 3 秒 / 空振り・完成済みを叩く 3 回)。
//====================================================================
static const Action NO_KEY = Action::Count;	// 表の「キー」欄の空き(絵がキーを使わない課)

const SceneForge::LessonDef SceneForge::LESSONS[] =
{
	//  id                  絵                        キー             一言(キー名は {印} で書く=今の機器の呼び名に置き換わる)                                           need   idle   誤り
	// --- 金床 ---
	{ LessonId::Flip,      LessonAnim::FlipHold,      NO_KEY,           (const char*)u8"{Flip}を長押しして、刃を裏返す",                1.0f,  0.0f,  3 },	// 片面が済んだら(打ち続けるより先に)
	{ LessonId::LeaveCold, LessonAnim::PressKey,      Action::Grab,     (const char*)u8"鉄が冷えた。{Grab}で鉄を掴み、炉へ運んで熱し直す", 1.0f,  0.0f,  3 },	// 救急: 冷打を重ねたら
	{ LessonId::Aim,       LessonAnim::MouseVertical, NO_KEY,           (const char*)u8"{Hand}を上下に動かして、叩く場所を狙う",       0.5f,  0.0f,  3 },
	{ LessonId::Strike,    LessonAnim::StrikeHold,    NO_KEY,           (const char*)u8"{Use}を長押しして力を溜め、離して叩く",   1.0f, 10.0f,  0 },
	{ LessonId::ReadGauge, LessonAnim::PointGauge,    NO_KEY,           (const char*)u8"温度ゲージの緑の間だけ、叩くと形が変わる",     3.0f,  0.0f,  0 },
	{ LessonId::HitScale,  LessonAnim::StrikeHold,    NO_KEY,           (const char*)u8"{Use}で、表面の黒い皮が残っている所を叩く",   1.0f,  0.0f,  0 },	// 術語(黒皮/酸化スケール/金肌)でなく見た目で言う=時代設定に依らない(2026-10-14)
	// --- 炉 ---
	{ LessonId::TakeOut,   LessonAnim::PressKey,      Action::Grab,     (const char*)u8"十分に熱くなった。{Grab}で鉄を掴んで炉から出す", 1.0f,  0.0f,  0 },
	{ LessonId::Bellows,   LessonAnim::PressKey,      Action::Bellows,  (const char*)u8"{Bellows}を押し続けて、ふいごで風を送る",           2.0f,  3.0f,  0 },	// need = 押していた秒(2026-10-22 長押しへ)
	{ LessonId::WaitHot,   LessonAnim::PointGauge,    NO_KEY,           (const char*)u8"温度ゲージの印が、囲んだ所に入るまで待つ",     1.0f,  0.0f,  0 },
	// --- 走動・搬運(通常モードの救急は無し: 行き先の ▼ が点滅する) ---
	{ LessonId::GripIron,  LessonAnim::PressKey,      Action::Grab,     (const char*)u8"鉄を見て、{Grab}で火ばさみで掴む",           1.0f,  0.0f,  0 },
	{ LessonId::PutDown,   LessonAnim::PressKey,      Action::Interact, (const char*)u8"{Interact}で鉄を置く",                               1.0f,  0.0f,  0 },
	{ LessonId::WalkTo,    LessonAnim::Walk,          NO_KEY,           (const char*)u8"{Move}で、▼の印へ歩く",                     1.0f,  0.0f,  0 },
	// --- 砥石(救急の回数 10 回 = 誤りの独白 0.8 秒ごとに1回なので、約 8 秒続けて間違えたら。2026-10-12 ユーザー決定。旧 2 回は探っている途中に出た) ---
	{ LessonId::GrindFlip, LessonAnim::GrindFlipHold, NO_KEY,           (const char*)u8"{Flip}を長押しして、刃を裏返す",                1.0f,  0.0f, 10 },	// 片面が研ぎ上がったら
	// 研ぐ基本: 左の長押し(刃を当てる)と右の連打(踏んで回す)は「同時に・ずっと」続ける操作 → 1つの課で同時に教える。
	//   2026-10-12 ユーザー: 別々に教えると「一緒に押さなくてよい」と思わせ、踏むのを止めて砥石が止まり、案内が行き来した。
	//   覚えた後は「思い出させる」課(remind): 研ぐのが止まった時だけ出る。通常モードは 3 秒止まったら。
	{ LessonId::GrindRun,  LessonAnim::GrindRun,      NO_KEY,           (const char*)u8"{GrindPress}を長押ししたまま、{Pedal}を連打して砥石を回す", 1.5f, 3.0f, 0, true },
	{ LessonId::GrindAngle,LessonAnim::GrindAngle,    NO_KEY,           (const char*)u8"{Tilt}を上下に動かし、火花が一番多い角度で研ぎ続ける", 1.0f, 0.0f, 10 },	// GRIND_ANGLE_HOLD_SEC 途切れずに 1回
	{ LessonId::GrindSlide,LessonAnim::GrindSlide,    NO_KEY,           (const char*)u8"{GrindPress}長押し・{Pedal}連打のまま、{Hand}を左右に動かして刃全体を研ぐ", 0.5f, 0.0f, 10 },	// 2026-10-19: 旧は左右の絵だけ=押すのを止めてよいと誤解された
	// --- 水槽(救急: 3 秒、2026-10-12 ユーザー決定) ---
	{ LessonId::Plunge,    LessonAnim::PressKey,      Action::Use,      (const char*)u8"{Use}で、刃を水に入れる",                 1.0f,  3.0f,  0 },
	{ LessonId::Stir,      LessonAnim::MouseVertical, NO_KEY,           (const char*)u8"{Hand}を上下に動かして、水の中で刃を揺する",   0.15f, 3.0f,  0 },
	// --- 試し切り(2026-10-22 構えと集中の一太刀)。救急: 斬らずに 6 秒 / 落ち着かないまま離して鎧に 2 回弾かれたら ---
	{ LessonId::Slash,     LessonAnim::StrikeHold,    NO_KEY,           (const char*)u8"{Use}長押しで構えて胴を狙い、照準の揺れが止まった瞬間に離して斬る", 1.0f, 6.0f, 2 },
};

//====================================================================
//  状況と「できた」量(コード)。新しい課はここに case を足す。
//====================================================================

//--- 炉で目指す温度: 研ぎまで進んでいれば焼入れ用(火花が散る)、まだなら「十分に熱い」(FORGE_FULL_TEMP。案内文と同じ基準)。
float SceneForge::HearthTargetTemp() const
{
	return StepReached(StepName::Grind) ? ForgingSim::BURN_TEMP : FORGE_FULL_TEMP;
}

//--- 今の状況でその課が意味を持つか。チュートリアルでも救急でも同じ条件(=同じ場面で同じ案内)。
bool SceneForge::LessonActive(LessonId id) const
{
	if (m_state != GAME_PLAY || m_clearDecided || SequencePlaying()) return false;
	const bool atAnvil    = AtStation(Station::Anvil)  && m_workAt == Station::Anvil  && !m_carrying;
	const bool inFireHere = AtStation(Station::Hearth) && m_workAt == Station::Hearth && !m_carrying && !Tempering();	// 炉の工位で鉄が火の中(焼き戻しは温度でなく色を見る工程=加熱の課は出さない)
	const bool walking    = m_walkMode && !Transitioning();
	const bool atGrind    = AtStation(Station::Grindstone) && m_workAt == Station::Grindstone && !m_carrying
	                     && StepReached(StepName::Grind) && !m_forging.AllSharp();	// 砥石で研ぐ事が残っている
	const float heat = m_forging.Heat();
	switch (id)
	{
	case LessonId::Aim:
	case LessonId::Strike:
		// 金床で、叩ける(緑の温度・まだ叩く面が残っている・裏返し中でない)時
		return atAnvil && m_flipPhase == FlipPhase::None && !m_forging.SideDone(m_forging.Side())
		    && heat >= IDEAL_MIN && heat <= IDEAL_MAX;
	case LessonId::Flip:
		// 押している最中も出し続ける(=「そのまま押し続けて」)。火钳を戻す完成の動作(PutBack)では出さない
		if (!atAnvil || m_flipPhase == FlipPhase::PutBack) return false;
		return m_flipPhase == FlipPhase::Holding || (m_forging.SideDone(m_forging.Side()) && !m_forging.BothSidesDone());
	case LessonId::ReadGauge:
	case LessonId::HitScale:
		return atAnvil && m_flipPhase == FlipPhase::None && !m_forging.SideDone(m_forging.Side())
		    && heat >= IDEAL_MIN && heat <= IDEAL_MAX;
	case LessonId::LeaveCold:
		// 金床で鉄が緑より冷えた(=もう叩いても形が変わらない)。鍛造がまだ残っている時
		return atAnvil && m_flipPhase == FlipPhase::None && heat < IDEAL_MIN && !m_forging.BothSidesDone();
	case LessonId::Bellows:
	case LessonId::WaitHot:
		// 炉の工位で、鉄が火の中にあり、まだ目指す温度に届いていない時
		return inFireHere && heat < HearthTargetTemp();
	case LessonId::TakeOut:
		return inFireHere && heat >= HearthTargetTemp();
	case LessonId::GripIron:	// 走動中、置いてある鉄を見ていて E で掴める(柄を付けた剣は手に持っている=掴む物が無い)
		return walking && !m_carrying && !SwordInHand() && FocusOnGuideTarget();
	case LessonId::PutDown:		// 走動中、鉄を持って行き先の工位を見ていて E で置ける
		return walking &&  m_carrying && FocusOnGuideTarget();
	case LessonId::WalkTo:		// 走動中、行き先がまだ遠い(E が効かない)
		return walking && m_walkGoalLabel != nullptr && !FocusOnGuideTarget();	// 行き先がある(UpdateLessons が GuideTarget から取る)
	case LessonId::GrindFlip:	// 砥石で、今の面が研ぎ上がって裏が残っている / 裏返している最中
		return atGrind && (m_grindFlipping || (m_forging.SharpProgress(GrindSide()) >= 1.0f && !m_forging.AllSharp()));
	case LessonId::GrindRun:	// 砥石で、今の面がまだ残っている(研いでいる最中も含む。止まったかは remind の仕組みが LessonMetric で見る)
		return atGrind && !m_grindFlipping && m_forging.SharpProgress(GrindSide()) < 1.0f;
	case LessonId::GrindAngle:
	case LessonId::GrindSlide:
	{
		// 研いでいる最中だけ(左を押していて砥石が回っている)。止まったら GrindRun が「思い出させる」。
		//   ヒステリシス: 出始めは「研げる速さ」以上、消えるのはその半分を下回ってから(踏む間の減速で点滅しない様に)。
		const float GRIND_LESSON_SPIN_HYST = 0.5f;	// 消える速さ = 研げる速さ × これ
		const float minSpeed = (m_lessonShown == (int)id) ? GRIND_WORK_MIN_SPEED * GRIND_LESSON_SPIN_HYST : GRIND_WORK_MIN_SPEED;
		return atGrind && !m_grindFlipping && InputActions::Held(Action::GrindPress) && m_wheel.Speed01() >= minSpeed
		    && m_forging.SharpProgress(GrindSide()) < 1.0f;
	}
	case LessonId::Plunge:		// 水槽で、焼きが入る熱さの刃を持っていて、まだ入れていない
		return AtStation(Station::Trough) && m_workAt == Station::Trough && !m_carrying && CurrentStep().type == StepName::Quench
		    && m_quenchTurn <= 0.0f && !QuenchStirring() && heat >= QUENCH_MIN_TEMP && heat <= QUENCH_MAX_TEMP;
	case LessonId::Stir:		// 水の中で、蒸気の膜がまだ破れていない
		return QuenchStirring() && m_boilStage == BoilStage::Film;
	case LessonId::Slash:		// 藁人形の前に立っていて、まだ両断していない
		return SwordAtStation() && !m_cutSevered;
	default: return false;
	}
}

//--- 走動中、行き先(鉄を取りに行く時は鉄 / 持っている時は工位)を見ていて E が効くか。
bool SceneForge::FocusOnGuideTarget() const
{
	if (!m_carrying && m_workAt != m_guideGoal) return m_grabFocus >= 0;	// 鉄を取りに行く所(掴む方は別に選ぶ=m_grabFocus)
	if (m_focus < 0) return false;
	const Interactable& it = INTERACTABLES[m_focus];
	return it.action == InteractAction::EnterStation && it.station == m_guideGoal;
}

//--- 「ゲージを指す」課が囲む温度の範囲。金床=緑(叩ける) / 炉=目指す温度から過熱の手前まで(取り出す頃合い)。
bool SceneForge::LessonGaugeRange(LessonId id, float& lo, float& hi) const
{
	switch (id)
	{
	case LessonId::ReadGauge: lo = IDEAL_MIN;          hi = IDEAL_MAX; return true;
	case LessonId::WaitHot:   lo = HearthTargetTemp(); hi = OVERHEAT;  return true;
	default: return false;
	}
}

//--- その課の「正しくできた」量。単調に増える(増えた=進んだ)。
float SceneForge::LessonMetric(LessonId id) const
{
	switch (id)
	{
	case LessonId::Aim:     return m_aimTravel;
	case LessonId::Strike:  return (float)m_validStrikes;
	case LessonId::Bellows: return m_bellowsHeldSec;	// 押していた秒
	case LessonId::Flip:    return (float)m_flipCount;
	case LessonId::ReadGauge: return (float)m_validStrikes;
	case LessonId::HitScale:  return (float)m_validStrikes;	// 黒皮の残る所(未完成の区域)を適温で叩いた回数=有効打。1回で「できた」(2026-10-19 ユーザー: 旧「両面の進み 30%」は何回叩いても済まない様に見えた)
	case LessonId::LeaveCold:
	case LessonId::TakeOut:   return (float)m_leaveCount;
	case LessonId::WaitHot:   return (float)m_heatReadyCount;
	case LessonId::GripIron:  return (float)m_gripCount;
	case LessonId::PutDown:   return (float)m_putCount;
	case LessonId::WalkTo:    return (float)m_reachCount;
	case LessonId::GrindFlip: return (float)m_grindFlipCount;
	case LessonId::GrindRun:  return m_grindRunTime;
	case LessonId::GrindAngle:return (float)m_grindAngleHolds;
	case LessonId::GrindSlide:return m_grindSlideTravel;
	case LessonId::Plunge:    return (float)m_plungeCount;
	case LessonId::Stir:      return m_stirTravel;
	case LessonId::Slash:     return (float)m_cutStrikes;	// 構えて離した回数
	default: return 0.0f;
	}
}

//--- 遊びのコードが「その課の誤り」を報告する(空振り=狙いの誤り / 完成済みの所を叩く=裏返すべき)。
void SceneForge::NoteLessonMistake(LessonId id)
{
	LessonState& l = m_lessons[(int)id];
	if (m_time - l.lastMistakeAt > LESSON_MISTAKE_FORGET_SEC) l.mistakes = 0;	// 間が空いた=続けて間違えてはいない
	++l.mistakes;
	l.lastMistakeAt = m_time;
}

//--- 「できた」: 弾ける演出と確認音を少し後に予約する(見えていない課は黙って済ます)。
void SceneForge::PopLesson(LessonState& l)
{
	const float POP_MIN_ALPHA = 0.15f;	// これより見えていれば「見てできた」とみなす
	l.popAt      = m_time + LESSON_POP_DELAY;
	l.popVisible = l.alpha > POP_MIN_ALPHA;
	l.soundDone  = !l.popVisible;
}

//====================================================================
//  毎フレーム(Update): 進み/放置/救急/覚えた/確認音、出す課を決める
//====================================================================
void SceneForge::UpdateLessons(float dt)
{
	static_assert(_countof(LESSONS) == (size_t)LessonId::Count, "every LessonId needs exactly one row in LESSONS");
	const float LEARNED_VOLUME = 0.8f;
	const float METRIC_EPS     = 1e-5f;

	// 狙いを動かした量(Aim の「できた」量)。前フレームとの差の累計
	if (m_lastAimRail >= 0.0f) m_aimTravel += fabsf(m_aimRail - m_lastAimRail);
	m_lastAimRail = m_aimRail;

	// 「着いた」「熱くなった」の瞬間を数える(立ち上がりのエッジ検出)= WalkTo / WaitHot の「できた」量
	const bool focusGoal = m_walkMode && FocusOnGuideTarget();
	if (focusGoal && !m_prevFocusGoal) ++m_reachCount;
	m_prevFocusGoal = focusGoal;
	const bool heatReady = !m_carrying && m_workAt == Station::Hearth && m_forging.Heat() >= HearthTargetTemp();
	if (heatReady && !m_prevHeatReady) ++m_heatReadyCount;
	m_prevHeatReady = heatReady;

	// 走動の行き先: 同じ行き先へ向かっている秒(行き先が変わる・工位に入ると計り直し)
	{
		XMFLOAT3 pos; const char* label = nullptr;
		if (GuideTarget(pos, label))
		{
			if (label != m_walkGoalLabel) { m_walkGoalLabel = label; m_walkGoalTime = 0.0f; }
			else m_walkGoalTime += dt;
		}
		else { m_walkGoalLabel = nullptr; m_walkGoalTime = 0.0f; }
	}

	// 出す課: チュートリアルなら「まだ覚えていない課」を表の順で、無ければ救急の課を表の順で。1つだけ。
	int teach = -1, rescue = -1;
	const int prevShown = m_lessonShown;
	for (const LessonDef& d : LESSONS)
	{
		LessonState& l = m_lessons[(int)d.id];
		const bool active = LessonActive(d.id);
		if (active) l.lastActiveAt = m_time;
		// 今出している課は、場面が一瞬外れても LESSON_HOLD_SEC は残す(閾値の前後を行き来する値で案内が点滅しない様に)
		const bool held = !active && prevShown == (int)d.id && (m_time - l.lastActiveAt) < LESSON_HOLD_SEC;
		const float metric = LessonMetric(d.id);
		const bool progressed = metric > l.lastMetric + METRIC_EPS;
		l.lastMetric = metric;
		// 教えている課は、できた瞬間に場面が終わる事が多い(E で離れる → もう金床の課ではない)ので、場面の外でも「できた」を見る
		if (m_tutorial && !l.learned && l.based && metric - l.base >= d.need) { l.learned = true; l.mistakes = 0; PopLesson(l); }
		if (!active && !held) { l.idle = 0.0f; l.rescue = false; continue; }	// 場面を離れたら救急も解く(教え始めの量は残す)
		if (active) l.idle = progressed ? 0.0f : l.idle + dt;	// 残している間は放置の時間を数えない

		if (m_tutorial && !l.learned)
		{
			if (teach < 0)
			{
				teach = (int)d.id;
				if (!l.based) { l.base = metric; l.based = true; }	// 教え始めの量
				if (metric - l.base >= d.need) { l.learned = true; l.mistakes = 0; PopLesson(l); teach = -1; }	// 教え始めたこのフレームに、もうできていた
			}
			continue;	// 覚えるまでは救急の対象にしない(教えている最中)
		}

		// 「思い出させる」課: 進みが止まって少し経ったら出す / 進めば黙って消える(弾ける・確認音・待ち時間なし)
		if (d.remind)
		{
			const float REMIND_AFTER_SEC = 0.5f;	// チュートリアル(覚えた後): 止まってからこの秒で出す
			l.rescue = active && l.idle >= (m_tutorial ? REMIND_AFTER_SEC : d.rescueIdle);
			if (l.rescue && rescue < 0) rescue = (int)d.id;
			continue;
		}

		// 救急(覚えた後 / 通常モード): 誤りの重なり or 放置で出し、出した後に need だけできたら消える
		if (l.rescue)
		{
			if (metric - l.base >= d.need) { l.rescue = false; l.mistakes = 0; l.rescueClearedAt = m_time; PopLesson(l); }
		}
		else if (active && (m_time - l.rescueClearedAt) >= LESSON_RESCUE_COOLDOWN
		      && ((d.rescueIdle > 0.0f && l.idle >= d.rescueIdle) || (d.rescueMistakes > 0 && l.mistakes >= d.rescueMistakes)))
		{
			l.rescue = true; l.base = metric; l.idle = 0.0f;
		}
		if (l.rescue && rescue < 0) rescue = (int)d.id;
	}
	m_lessonShown       = (teach >= 0) ? teach : rescue;
	m_lessonShownRescue = (teach < 0 && rescue >= 0);

	// 確認音: 弾ける時刻が来た課だけ1回(音は Draw でなく Update で鳴らす)
	for (LessonState& l : m_lessons)
	{
		if (l.soundDone || m_time < l.popAt) continue;
		Audio::Play(Audio::SE_LEARNED, LEARNED_VOLUME);
		l.soundDone = true;
	}
}

//====================================================================
//  描画: 動く絵 + 一言(全ての課で同じ位置・同じ見た目・同じ確認音)
//  動く絵は m_lessonClock(設定の「教学アニメの速さ」を掛けた時計)で動かす。押した長さの輪など「実際の進み」は速さに関係しない
//====================================================================
static const float LESSON_CENTER_X  = 0.70f;	// 画面の横位置(画面幅比)。中央は金床と刃=作業する所なので右へ寄せる(2026-10-17 試遊: 中央の案内が視界を塞ぐ)
static const float LESSON_CENTER_Y  = 0.60f;	// 画面の縦位置(刃の少し下)=淬火/研ぎの動く案内と同じ
static const float LESSON_ICON_RATIO = 0.085f;	// アイコンの大きさ(画面高さ比)=同上
static const float LESSON_SHADOW_PX  = 2.0f;
static const float LESSON_RING_GAP   = 0.72f;	// 溜まる輪の半径(アイコンの大きさに対する比)
static const float LESSON_RING_W     = 0.06f;	// 輪の太さ(〃)
static const int   LESSON_RING_SEGS  = 48;		// 輪を描く折れ線の数(滑らかさ)
static const ImU32 LESSON_RING_COL   = IM_COL32(255, 200, 110, 255);	// 溜まる輪 = 熱い鉄の橙(ゲームの色)
static const ImU32 LESSON_RING_BG    = IM_COL32(  0,   0,   0, 110);	// 輪の溝(空でも「ここに溜まる」と分かる)
static const ImU32 LESSON_TEXT_COL   = IM_COL32(255, 236, 196, 255);	// 一言の文字(案内文と同じ色)
static const float LESSON_TEXT_RATIO = 0.024f;	// 一言の文字の高さ(画面高さ比)
static const float LESSON_TEXT_GAP   = 1.10f;	// アイコン中心から一言までの距離(アイコンの大きさに対する比)。輪・矢印の外
static const float LESSON_PRESS_SINK = 0.05f;	// 押している間に沈む量(アイコンの大きさに対する比)

//--- アイコン1枚(影 → 本体)。name は InputPrompts が返すパス(".png" 抜き)。tint は色の掛け算(白=そのまま)。
static void DrawLessonIcon(ImDrawList* dl, const char* name, ImVec2 c, float size, float alpha01, ImU32 tint)
{
	if (!name) return;
	const std::string path = std::string(name) + ".png";
	std::shared_ptr<Texture> tex = TextureCache::Get(path.c_str());
	if (!tex || !tex->GetResource()) return;
	ImTextureID id = (ImTextureID)tex->GetResource();
	const int a = (int)(255 * alpha01 * ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
	const ImU32 col = (tint & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
	const ImVec2 p0(c.x - size * 0.5f, c.y - size * 0.5f), p1(c.x + size * 0.5f, c.y + size * 0.5f);
	dl->AddImage(id, ImVec2(p0.x + LESSON_SHADOW_PX, p0.y + LESSON_SHADOW_PX), ImVec2(p1.x + LESSON_SHADOW_PX, p1.y + LESSON_SHADOW_PX),
	             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, (int)(a * 0.7f)));
	dl->AddImage(id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), col);
}

//--- 溜まる輪: 12時から時計回りに fill01 だけ。下に溝(全周)を敷く。
static void DrawLessonRing(ImDrawList* dl, ImVec2 c, float size, float fill01, float alpha01)
{
	const float r = size * LESSON_RING_GAP, w = size * LESSON_RING_W;
	const float START = -XM_PIDIV2;	// 12時
	auto withAlpha = [&](ImU32 col) { return (col & ~IM_COL32_A_MASK) | ((ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha01) << IM_COL32_A_SHIFT); };
	dl->AddCircle(c, r, withAlpha(LESSON_RING_BG), LESSON_RING_SEGS, w);
	if (fill01 <= 0.0f) return;
	dl->PathArcTo(c, r, START, START + XM_2PI * fminf(fill01, 1.0f), LESSON_RING_SEGS);
	dl->PathStroke(withAlpha(LESSON_RING_COL), 0, w);
}

//--- キーの絵の上の小さな一言(「長押し」「連打」「左右」= どの押し方/動かし方か)。
static void DrawLessonLabel(ImDrawList* dl, const char* text, ImVec2 iconCenter, float size, float alpha01)
{
	const float LABEL_RATIO = 0.020f;	// 文字の高さ(画面高さ比)
	const float LABEL_ABOVE = 0.95f;	// 絵の中心から文字までの上への距離(キーの絵の大きさに対する比)。輪の上
	const float lpx = ImGui::GetIO().DisplaySize.y * LABEL_RATIO;
	ImFont* f = DebugUI::FontJPFor(lpx);
	const ImVec2 sz = f->CalcTextSizeA(lpx, FLT_MAX, 0.0f, text);
	const ImVec2 pos(iconCenter.x - sz.x * 0.5f, iconCenter.y - size * LABEL_ABOVE - sz.y);
	const int a = (int)(255 * alpha01);
	dl->AddText(f, lpx, ImVec2(pos.x + LESSON_SHADOW_PX, pos.y + LESSON_SHADOW_PX), IM_COL32(0, 0, 0, (int)(a * 0.7f)), text);
	dl->AddText(f, lpx, pos, (LESSON_TEXT_COL & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT), text);
}

//--- 弾ける輪: 0..1 の進みで外へ広がりながら消える(叩いた瞬間 / できた瞬間)。
static void DrawLessonBurst(ImDrawList* dl, ImVec2 c, float size, float p01, float alpha01, ImU32 col, float growTo)
{
	const float r = size * LESSON_RING_GAP * (1.0f + (growTo - 1.0f) * p01);
	const int   a = (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha01 * (1.0f - p01));
	dl->AddCircle(c, r, (col & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT), LESSON_RING_SEGS, size * LESSON_RING_W * 1.5f);
}

//--- 一言(アイコンの下、中央揃え、字幕の箱 + 縁取り)
static void DrawLessonText(ImDrawList* dl, const char* text, ImVec2 c, float size, float alpha01)
{
	if (!text || !text[0]) return;
	text = InputPrompts::Format(text);	// {Interact} 等 → 今の機器の呼び名(「Eキー」/「Aボタン」)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	const float px = disp.y * LESSON_TEXT_RATIO;
	ImFont* f = DebugUI::FontJPFor(px);
	const ImVec2 sz = f->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
	const ImVec2 p(c.x - sz.x * 0.5f, c.y + size * LESSON_TEXT_GAP);
	const int a = (int)(255 * alpha01);
	DrawSubtitleBox(dl, p.x, p.y, p.x + sz.x, p.y + sz.y, px, alpha01);	// 字幕の箱(3D の上でも読める)
	DrawOutlinedText(dl, f, px, p.x, p.y, (LESSON_TEXT_COL & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT), text);
}

//--- マウスを上下/左右に動かす。towards = 0: 往復(動いている向きの矢印が明るい)
//    −1 / +1: 上(左) / 下(右)へだけ動かして見せ、その向きの矢印だけを明るくする(直す向きを教える)。
void SceneForge::DrawAxisLesson(AxisPrompt axisPrompt, int towards, float alpha, float scale, unsigned int tint, float xShift, const char* label)
{
	const bool vertical = (axisPrompt != AxisPrompt::HandHorizontal);	// 左右に動かすのは「刃を滑らせる」だけ
	const float SWING_SEC  = 1.4f;		// 1往復の秒
	const float SWING      = 0.30f;		// 動く幅(アイコンの大きさに対する比。片側)
	const float ARROW_GAP  = 0.95f, ARROW_HALF_W = 0.22f, ARROW_H = 0.18f, ARROW_DIM = 0.30f;	// 矢印
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * scale;
	const ImVec2 c(disp.x * LESSON_CENTER_X + xShift, disp.y * LESSON_CENTER_Y);
	if (label) DrawLessonLabel(dl, label, c, size, alpha);	// 絵の上の一言(「長押し」「連打」と同じ見た目)
	const ImVec2 axis = vertical ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 0.0f);	// + = 下 / 右
	const float t01 = fmodf(m_lessonClock, SWING_SEC) / SWING_SEC;
	float off, movePlus;	// 中心からのずれ / 動いている向き(+ = 下・右)
	if (towards == 0) { off = sinf(t01 * XM_2PI) * size * SWING; movePlus = cosf(t01 * XM_2PI); }
	else              { off = (float)towards * t01 * size * SWING * 2.0f; movePlus = (float)towards; }	// 中心からその向きへ動き、また中心から
	DrawLessonIcon(dl, InputPrompts::PointerIcon(axisPrompt), ImVec2(c.x + axis.x * off, c.y + axis.y * off), size, alpha, tint);	// マウス本体 / 左(手)か右(回す)のスティック
	for (float dir = -1.0f; dir <= 1.0f; dir += 2.0f)
	{
		const ImVec2 n(axis.x * dir, axis.y * dir), perp(n.y, -n.x);
		const float hw = size * ARROW_HALF_W;
		const ImVec2 base(c.x + n.x * size * ARROW_GAP, c.y + n.y * size * ARROW_GAP);
		const ImVec2 tip(base.x + n.x * size * ARROW_H, base.y + n.y * size * ARROW_H);
		const ImVec2 l(base.x - perp.x * hw, base.y - perp.y * hw), r(base.x + perp.x * hw, base.y + perp.y * hw);
		const int aa = (int)(255 * alpha * (ARROW_DIM + (1.0f - ARROW_DIM) * fmaxf(dir * movePlus, 0.0f)));
		const float s = LESSON_SHADOW_PX;
		dl->AddTriangleFilled(ImVec2(tip.x + s, tip.y + s), ImVec2(l.x + s, l.y + s), ImVec2(r.x + s, r.y + s), IM_COL32(0, 0, 0, (int)(aa * 0.7f)));
		dl->AddTriangleFilled(tip, l, r, IM_COL32(245, 235, 215, aa));
	}
}

//--- 叩く: 左ボタンを押し続けて輪が溜まる → 離す(輪が弾ける)を繰り返す。実際の操作の順番そのまま。
void SceneForge::DrawStrikeHoldLesson(float alpha, float scale, unsigned int tint)
{
	const float HOLD_SEC   = 1.1f;		// 押し続けて輪が溜まる
	const float STRIKE_SEC = 0.6f;		// 離す=叩く(輪が弾ける)
	const float BURST_GROW = 1.8f;		// 弾ける輪が広がる大きさ(元の半径の倍率)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * scale;
	const ImVec2 c(disp.x * LESSON_CENTER_X, disp.y * LESSON_CENTER_Y);
	const float t = fmodf(m_lessonClock, HOLD_SEC + STRIKE_SEC);
	const char* key = InputPrompts::Icon(Action::Use);			// 左クリック / RT
	const std::string outline = std::string(key ? key : "") + "_outline";	// 離している絵
	if (t < HOLD_SEC)
	{
		DrawLessonIcon(dl, key, ImVec2(c.x, c.y + size * LESSON_PRESS_SINK), size, alpha, tint);	// 塗りの絵(押している)
		DrawLessonRing(dl, c, size, t / HOLD_SEC, alpha);
	}
	else
	{
		DrawLessonIcon(dl, outline.c_str(), c, size, alpha, tint);
		DrawLessonBurst(dl, c, size, (t - HOLD_SEC) / STRIKE_SEC, alpha, LESSON_RING_COL, BURST_GROW);
	}
}

//--- 動作の絵を今の機器で引く: 戻り値 = 押している絵(塗り) / outline = 押していない絵(輪郭)。Kenney: 塗り=押している / 輪郭=押していない
static const char* LessonKeyIcon(Action action, std::string& outline)
{
	const char* key = InputPrompts::Icon(action);
	outline = std::string(key ? key : "") + "_outline";
	return key;
}

//--- キーを押し続ける(炉のふいご=R): 押した絵(塗り)が沈み、輪が溜まる → 離した絵(輪郭)で一呼吸 → 繰り返し。
void SceneForge::DrawHoldKeyLesson(Action action, float alpha, float scale, unsigned int tint)
{
	std::string outline;
	const char* key = LessonKeyIcon(action, outline);
	const float HOLD_SEC = 1.4f;		// 押し続けて輪が溜まる
	const float REST_SEC = 0.5f;		// 離して一呼吸(押す/離すの区別が付く様に)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * scale;
	const ImVec2 c(disp.x * LESSON_CENTER_X, disp.y * LESSON_CENTER_Y);
	const float t = fmodf(m_lessonClock, HOLD_SEC + REST_SEC);
	if (t < HOLD_SEC)
	{
		DrawLessonIcon(dl, key, ImVec2(c.x, c.y + size * LESSON_PRESS_SINK), size, alpha, tint);
		DrawLessonRing(dl, c, size, t / HOLD_SEC, alpha);
	}
	else
	{
		DrawLessonIcon(dl, outline.c_str(), c, size, alpha, tint);
		DrawLessonRing(dl, c, size, 0.0f, alpha);
	}
}

//--- 裏返しの「押し続ける」(『鬼武者』の剛力の様に、ボタンの絵 + 押した長さで溜まる輪)。
//    押している最中(Holding)= 輪は実際の再生位置。離すと減っていく=「戻っている」と見える。
//    完成後(PutBack)= 満タン。まだ押していない(None)= 押し続ける見本の繰り返し。
void SceneForge::DrawFlipHoldPrompt(float alpha, float scale, unsigned int tint, const char* label)
{
	if (m_flipPhase == FlipPhase::None) { DrawHoldKeyLesson(Action::Flip, alpha, scale, tint); return; }
	const bool  pressed = (m_flipPhase == FlipPhase::Holding) && InputActions::Held(Action::Flip);
	const float fill    = (m_flipPhase == FlipPhase::PutBack) ? 1.0f : m_flipHold / FlipHoldTotal();
	DrawHoldProgressPrompt(Action::Flip, fill, pressed, label, alpha, scale, tint);
}

//--- 長押しの進み表示(共通): キーの絵(押している=塗り+沈む / 離している=輪郭) + 進みの輪 + 下に一言(nullptr で無し)。
//    金床の裏返し・砥石の裏返しで同じ見た目を使う=同じ操作は同じ見た目(操作の一貫性)。
void SceneForge::DrawHoldProgressPrompt(Action action, float fill01, bool pressed, const char* label, float alpha, float scale, unsigned int tint)
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * scale;
	const ImVec2 c(disp.x * LESSON_CENTER_X, disp.y * LESSON_CENTER_Y);
	std::string outline;
	const char* key = LessonKeyIcon(action, outline);
	DrawLessonIcon(dl, pressed ? key : outline.c_str(), ImVec2(c.x, c.y + (pressed ? size * LESSON_PRESS_SINK : 0.0f)), size, alpha, tint);
	DrawLessonRing(dl, c, size, fill01, alpha);
	DrawLessonText(dl, label, c, size, alpha);
}

//--- キーを1回押す: 短く押す(塗り・沈む・小さく弾ける)→ 離す(輪郭)を繰り返す=「ここで1回押す」。
void SceneForge::DrawTapKeyLesson(Action action, float alpha, float scale, unsigned int tint)
{
	std::string outline;
	const char* key = LessonKeyIcon(action, outline);
	const float PERIOD     = 0.9f;		// 1回押すごとの間隔(秒)
	const float PRESS_FRAC = 0.2f;		// 押している割合
	const float BURST_GROW = 1.5f;		// 押すたびに弾ける輪の広がり(元の半径の倍率)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * scale;
	const ImVec2 c(disp.x * LESSON_CENTER_X, disp.y * LESSON_CENTER_Y);
	const float p = fmodf(m_lessonClock, PERIOD) / PERIOD;
	if (p < PRESS_FRAC) DrawLessonIcon(dl, key, ImVec2(c.x, c.y + size * LESSON_PRESS_SINK), size, alpha, tint);
	else                DrawLessonIcon(dl, outline.c_str(), c, size, alpha, tint);
	DrawLessonBurst(dl, c, size, p, alpha, LESSON_RING_COL, BURST_GROW);
}

//--- 矢印(影つき)。tip = 先端、dirY = +1 下向き / −1 上向き。
static void DrawLessonArrow(ImDrawList* dl, ImVec2 tip, float dirY, float size, float alpha01, ImU32 col)
{
	const float HALF_W = 0.30f, HEAD_H = 0.45f, STEM_HALF_W = 0.10f, STEM_H = 0.45f;	// 矢印の形(大きさ size に対する比)
	const float hw = size * HALF_W, hh = size * HEAD_H, sw = size * STEM_HALF_W, sh = size * STEM_H;
	const ImVec2 b(tip.x, tip.y - dirY * hh);							// 三角の底辺の中央
	const int   a   = (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha01);
	const ImU32 fg  = (col & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
	const ImU32 shd = IM_COL32(0, 0, 0, (int)(a * 0.7f));
	const float y0 = fminf(b.y, b.y - dirY * sh), y1 = fmaxf(b.y, b.y - dirY * sh);
	for (int pass = 0; pass < 2; ++pass)	// 影 → 本体
	{
		const float o = (pass == 0) ? LESSON_SHADOW_PX : 0.0f;
		const ImU32 k = (pass == 0) ? shd : fg;
		dl->AddTriangleFilled(ImVec2(tip.x + o, tip.y + o), ImVec2(b.x - hw + o, b.y + o), ImVec2(b.x + hw + o, b.y + o), k);
		dl->AddRectFilled(ImVec2(b.x - sw + o, y0 + o), ImVec2(b.x + sw + o, y1 + o), k);
	}
}

//--- 温度ゲージの [lo, hi] を脈打つ枠で囲み、上から矢印で指す。位置は DrawHeatGauge と同じ定数から=必ず一致。
void SceneForge::DrawGaugePointLesson(float lo, float hi, float alpha, float scale)
{
	const float FRAME_PAD  = 0.006f;	// 枠を槽からはみ出させる量(画面高さ比)
	const float FRAME_W    = 3.0f;		// 枠の太さ(px)
	const float PULSE_HZ   = 1.5f;		// 枠が脈打つ速さ(回/秒)
	const float PULSE_MIN  = 0.45f;		// 一番淡い時の濃さ(0..1)
	const float ARROW_SIZE = 0.050f;	// 矢印の大きさ(画面高さ比)
	const float ARROW_GAP  = 0.010f;	// 矢印の先と枠の間(画面高さ比)
	const float BOB_AMP    = 0.008f;	// 矢印の上下の揺れ(画面高さ比)
	const float BOB_HZ     = 1.5f;		// 揺れの速さ(回/秒)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float tw = disp.x * HEAT_TRACK_W_RATIO, th = disp.y * HEAT_TRACK_H_RATIO;
	const float tx0 = (disp.x - tw) * 0.5f, ty0 = disp.y * HEAT_TRACK_Y_RATIO;	// 温度 t は tx0 + tw*t(DrawHeatGauge の heatX と同じ)
	const float x0 = tx0 + tw * lo, x1 = tx0 + tw * hi, pad = disp.y * FRAME_PAD;
	const float pulse = Lerp::Linear(PULSE_MIN, 1.0f, 0.5f + 0.5f * sinf(m_lessonClock * PULSE_HZ * XM_2PI));
	const int a = (int)(255 * alpha * pulse);
	dl->AddRect(ImVec2(x0 - pad, ty0 - pad), ImVec2(x1 + pad, ty0 + th + pad), (LESSON_RING_COL & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT), 0.0f, 0, FRAME_W);
	const float bob = sinf(m_lessonClock * BOB_HZ * XM_2PI) * disp.y * BOB_AMP;
	DrawLessonArrow(dl, ImVec2((x0 + x1) * 0.5f, ty0 - pad - disp.y * ARROW_GAP + bob), +1.0f, disp.y * ARROW_SIZE * scale, alpha, LESSON_RING_COL);
}

//====================================================================
//  指示図(何をするか)= ピクトグラム。キーの絵(どう操作するか)の左に出す。
//    1コマ = 台(base)+ 姿勢(pose)の2層。少数の決まった姿勢へ「跳ばして止める」:
//    リミテッドアニメーション(Fallout の Vault Boy の様な、コマを減らして各姿勢を見せるアニメ)。
//    滑らかに動かすと「どこにいるか」が一瞬も決まらず、貼り絵が滑っている様に見えた(ユーザーの評価)。
//    絵と各コマの秒数は ChatGPT の書き出し(Assets/UI/Lessons/v2 の *_animation.json)をそのまま写した。
//    視点の決まり: 刃の「どこ」の課(瞄準・氧化皮)= 真上から(画面の刃と同じ) / 動作の課(叩く・裏返す・炉へ)= 横から。
//====================================================================
static const char* LESSON_PICT_DIR = "Assets/UI/Lessons/v2/";	// 指示図の置き場所(表のファイル名はここからの名前)

const SceneForge::LessonPicture SceneForge::LESSON_PICTURES[] =
{
	// 瞄準: 鎚が刃の上 → 真ん中 → 下 → 真ん中 …(マウスの上下と同じ向き)。上下の端で長く止め、真ん中は「通過」の短いコマ。
	//   同じ手の絵を ±56px(lesson_aim_animation.json の範囲=刃の上に収まる)跳ばし、手首を少し傾ける(毎コマ描き直した様な手作り感)。
	//   支点 = 鎚頭の中心(図から測った値)= 傾けても鎚頭は刃の上から外れない(腕の付け根を支点にしたら外れた=プレビューで確認)。
	//   縁 6px: 鎚頭と刃の線がくっつくので暗い縁で切り離す。
	{ LessonId::Aim, 6.0f, 4, {
		//  台                       姿勢                    秒      dx     dy      tilt    支点
		{ "lesson_aim_base.png", "lesson_aim_hand.png", 0.32f, 0.0f, -56.0f, -0.05f, 184.0f, 255.0f },	// 上(刃先の側)
		{ "lesson_aim_base.png", "lesson_aim_hand.png", 0.16f, 0.0f,   0.0f,  0.00f, 184.0f, 255.0f },	// 通過
		{ "lesson_aim_base.png", "lesson_aim_hand.png", 0.32f, 0.0f,  56.0f,  0.05f, 184.0f, 255.0f },	// 下(根元の側)
		{ "lesson_aim_base.png", "lesson_aim_hand.png", 0.16f, 0.0f,   0.0f,  0.00f, 184.0f, 255.0f },	// 通過
	} },
	// 叩く: 鎚を高く上げて長く止める(=押し続けて溜める)→ 一瞬で打つ(=離す)。止まる長さの差が操作の説明になっている。
	{ LessonId::Strike, 0.0f, 3, {
		{ "lesson_strike_base.png", "lesson_strike_pose1.png", 1.15f, 0.0f, 0.0f, 0.0f, 245.0f, 268.0f },	// 振り上げて溜める
		{ "lesson_strike_base.png", "lesson_strike_pose2.png", 0.20f, 0.0f, 0.0f, 0.0f, 245.0f, 268.0f },	// 打つ(衝撃の線)
		{ "lesson_strike_base.png", "lesson_strike_pose1.png", 0.65f, 0.0f, 0.0f, 0.0f, 245.0f, 268.0f },	// また振り上げる
	} },
	// 裏返す: 平ら → 斜め45° → 立つ(細い線)→ 反対へ斜め45° → 反対の面が平ら(=半回転。一回転の矢印は描かない)。
	//   最初の面はきれい(叩き終えた)/ 最後の面は斜線の黒皮(まだ叩いていない)=「なぜ裏返すか」まで見える。
	//   同級生テスト(2026-10-12): 3コマ版は「分かるが硬い」→ 斜めの2コマを足した。途中は短く、最初と最後で長く止める。
	{ LessonId::Flip, 0.0f, 5, {
		{ "lesson_flip_base.png", "lesson_flip_pose1.png", 0.80f, 0.0f, 0.0f, 0.0f, 346.0f, 250.0f },	// 掴む(きれいな面)
		{ "lesson_flip_base.png", "lesson_flip_pose2.png", 0.15f, 0.0f, 0.0f, 0.0f, 346.0f, 250.0f },	// 斜め
		{ "lesson_flip_base.png", "lesson_flip_pose3.png", 0.15f, 0.0f, 0.0f, 0.0f, 346.0f, 250.0f },	// 立つ
		{ "lesson_flip_base.png", "lesson_flip_pose4.png", 0.15f, 0.0f, 0.0f, 0.0f, 346.0f, 250.0f },	// 反対へ斜め
		{ "lesson_flip_base.png", "lesson_flip_pose5.png", 1.50f, 0.0f, 0.0f, 0.0f, 346.0f, 250.0f },	// 反対の面(黒皮あり)
	} },
	// 黒皮を叩く: 横から。斜線 = 黒皮。鎚を上げる → 打つと破片が飛ぶ(台が「真ん中の斑が無い絵」に替わる)→ 引いても無いまま。
	//   同級生テスト(2026-10-12): 真上からの白い斑版は分かりにくかった(白=良い物に見える / 瞄準の図と見分けにくい)→ 作り直し。
	{ LessonId::HitScale, 0.0f, 3, {
		{ "lesson_hit_scale_base.png",  "lesson_hit_scale_pose1.png", 0.90f, 0.0f, 0.0f, 0.0f, 245.0f, 268.0f },	// 斑の上で振り上げる
		{ "lesson_hit_scale_base2.png", "lesson_hit_scale_pose2.png", 0.25f, 0.0f, 0.0f, 0.0f, 245.0f, 268.0f },	// 打つ(破片が飛ぶ)
		{ "lesson_hit_scale_base2.png", "lesson_hit_scale_pose3.png", 1.40f, 0.0f, 0.0f, 0.0f, 245.0f, 268.0f },	// 引く(斑は無いまま)
	} },
	// 冷えたら炉へ: 金床の上の刃 → 炉への矢印。冷えた事(温度)は単色の絵では言えないので、下の一言(「鉄が冷えた」)が補う。
	{ LessonId::LeaveCold, 0.0f, 2, {
		{ "lesson_leave_cold_base.png", "lesson_leave_cold_pose1.png", 0.90f, 0.0f, 0.0f, 0.0f, 256.0f, 256.0f },	// 金床に刃
		{ "lesson_leave_cold_base.png", "lesson_leave_cold_pose2.png", 1.40f, 0.0f, 0.0f, 0.0f, 256.0f, 256.0f },	// 炉へ
	} },
	// --- 第2批(炉・搬運) ---
	// ふいご: 踏む前 → 踏むと風が出て火が大きくなる(=原因と結果)。キーの絵は「R を長押し」なので、図の上下は「踏み続けている」様子。
	{ LessonId::Bellows, 0.0f, 2, {
		{ "lesson_bellows_base.png", "lesson_bellows_pose1.png", 0.80f, 0.0f, 0.0f, 0.0f, 175.0f, 310.0f },	// 足を上げる
		{ "lesson_bellows_base.png", "lesson_bellows_pose2.png", 0.65f, 0.0f, 0.0f, 0.0f, 175.0f, 310.0f },	// 踏む → 風・大きな火
	} },
	// 炉から出す: 火の中の刃 → 火の外へ引き出す(台が「刃の無い火」に替わる)。
	{ LessonId::TakeOut, 0.0f, 2, {
		{ "lesson_take_out_base.png",  "lesson_take_out_pose1.png", 0.80f, 0.0f, 0.0f, 0.0f, 205.0f, 265.0f },	// 火の中
		{ "lesson_take_out_base2.png", "lesson_take_out_pose2.png", 1.30f, 0.0f, 0.0f, 0.0f, 205.0f, 265.0f },	// 引き出す
	} },
	// 掴む / 置く: 同じ3枚の絵を逆の順に。最後のコマで長く止める=結果(持ち上げた / 置いた)で見分けが付く。
	{ LessonId::GripIron, 0.0f, 3, {
		{ "lesson_grip_iron_base.png", "lesson_grip_iron_pose1.png", 0.70f, 0.0f, 0.0f, 0.0f, 292.0f, 305.0f },	// 開いた火ばさみ
		{ "lesson_grip_iron_base.png", "lesson_grip_iron_pose2.png", 0.30f, 0.0f, 0.0f, 0.0f, 292.0f, 305.0f },	// 挟む
		{ "lesson_grip_iron_base.png", "lesson_grip_iron_pose3.png", 1.20f, 0.0f, 0.0f, 0.0f, 292.0f, 305.0f },	// 持ち上げる
	} },
	{ LessonId::PutDown, 0.0f, 3, {
		{ "lesson_put_down_base.png", "lesson_put_down_pose1.png", 0.70f, 0.0f, 0.0f, 0.0f, 292.0f, 305.0f },	// 持って来る
		{ "lesson_put_down_base.png", "lesson_put_down_pose2.png", 0.30f, 0.0f, 0.0f, 0.0f, 292.0f, 305.0f },	// 下ろす
		{ "lesson_put_down_base.png", "lesson_put_down_pose3.png", 1.20f, 0.0f, 0.0f, 0.0f, 292.0f, 305.0f },	// 離す
	} },
	// 歩く: ▼ へ向かう足跡が左右交互に進む。
	{ LessonId::WalkTo, 0.0f, 2, {
		{ "lesson_walk_to_base.png", "lesson_walk_to_pose1.png", 0.55f, 0.0f, 0.0f, 0.0f, 256.0f, 320.0f },	// 左足
		{ "lesson_walk_to_base.png", "lesson_walk_to_pose2.png", 0.55f, 0.0f, 0.0f, 0.0f, 256.0f, 320.0f },	// 右足
	} },
	// --- 第3批(砥石・水槽)。砥石の踏み板は細い棒(ふいごの大きな革袋と見分けが付く様に) ---
	// 研ぐ基本(左の長押し + 右の連打): 砥石の上に構える → 当てると火花。踏む動作はキーの絵(右の連打)が受け持つ。
	{ LessonId::GrindRun, 0.0f, 2, {
		{ "lesson_press_blade_base.png", "lesson_press_blade_pose1.png", 0.65f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },	// 構える
		{ "lesson_press_blade_base.png", "lesson_press_blade_pose2.png", 1.20f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },	// 当てる(火花)
	} },
	// 角度: 寝かせすぎ(火花少)→ 立てすぎ(火花少)→ 正しい角度(火花多・長く止める)。
	//   「正しい角度は火花が多い」= ゲームの中の手掛かりと同じ(角度計を見なくても火花で分かる様に)。支点 = 刃と砥石の接点。
	{ LessonId::GrindAngle, 0.0f, 3, {
		{ "lesson_grind_angle_base.png",  "lesson_grind_angle_pose_few.png",  0.65f, 0.0f, 0.0f,  0.08f, 197.0f, 257.0f },	// 寝かせすぎ
		{ "lesson_grind_angle_base2.png", "lesson_grind_angle_pose_few.png",  0.65f, 0.0f, 0.0f, -0.48f, 197.0f, 257.0f },	// 立てすぎ
		{ "lesson_grind_angle_base3.png", "lesson_grind_angle_pose_many.png", 1.40f, 0.0f, 0.0f, -0.18f, 197.0f, 257.0f },	// 正しい
	} },
	// 滑らせる: 刃先側 → 真ん中 → 根元側 → 真ん中。火花(接点)は砥石の上に留まり、刃だけが左右に動く。
	{ LessonId::GrindSlide, 0.0f, 4, {
		{ "lesson_grind_slide_base.png",  "lesson_grind_slide_pose1.png", 0.40f,  60.0f, 0.0f, 0.0f, 197.0f, 257.0f },	// 刃先側
		{ "lesson_grind_slide_base2.png", "lesson_grind_slide_pose2.png", 0.40f,   0.0f, 0.0f, 0.0f, 197.0f, 257.0f },	// 真ん中
		{ "lesson_grind_slide_base3.png", "lesson_grind_slide_pose3.png", 0.40f, -60.0f, 0.0f, 0.0f, 197.0f, 257.0f },	// 根元側
		{ "lesson_grind_slide_base2.png", "lesson_grind_slide_pose2.png", 0.80f,   0.0f, 0.0f, 0.0f, 197.0f, 257.0f },	// 真ん中
	} },
	// 砥石で裏返す: 金床の裏返しと同じ5コマ(平 → 斜め → 立つ → 斜め → 平)。火ばさみでなく手で持つ。
	{ LessonId::GrindFlip, 0.0f, 5, {
		{ "lesson_grind_flip_base.png", "lesson_grind_flip_pose1.png", 0.80f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },
		{ "lesson_grind_flip_base.png", "lesson_grind_flip_pose2.png", 0.15f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },
		{ "lesson_grind_flip_base.png", "lesson_grind_flip_pose3.png", 0.15f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },
		{ "lesson_grind_flip_base.png", "lesson_grind_flip_pose4.png", 0.15f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },
		{ "lesson_grind_flip_base.png", "lesson_grind_flip_pose5.png", 1.50f, 0.0f, 0.0f, 0.0f, 197.0f, 257.0f },
	} },
	// 水に入れる: 横に持つ → 刃先を下へ → 沈める(水しぶき・蒸気)。ゲームの焼入れの動き(QuenchStep: 回す → 沈める)と同じ順。
	{ LessonId::Plunge, 0.0f, 3, {
		{ "lesson_plunge_base.png",  "lesson_plunge_pose1.png", 0.70f, 0.0f, 0.0f, 0.0f, 245.0f, 145.0f },	// 横に持つ
		{ "lesson_plunge_base2.png", "lesson_plunge_pose2.png", 0.45f, 0.0f, 0.0f, 0.0f, 245.0f, 145.0f },	// 刃先を下へ
		{ "lesson_plunge_base3.png", "lesson_plunge_pose3.png", 1.40f, 0.0f, 0.0f, 0.0f, 245.0f, 145.0f },	// 沈める
	} },
	// 揺する: 水の中で刃を上下(泡の位置も替わる)。
	//   書き出しは ±18px だったが、画面上の図(約150px)では ±5px しか動かず見えにくい → 範囲の中で大きく:
	//   上 −40px(刃先 390→350: 水面 299 より下のまま)/ 下 +22px(刃先 412: 底 422 に触れない)。値は lesson_stir_animation.json の水面・底から。
	{ LessonId::Stir, 0.0f, 2, {
		{ "lesson_stir_base.png",  "lesson_stir_pose1.png", 0.30f, 0.0f, -40.0f, 0.0f, 245.0f, 260.0f },	// 上
		{ "lesson_stir_base2.png", "lesson_stir_pose2.png", 0.30f, 0.0f,  22.0f, 0.0f, 245.0f, 260.0f },	// 下
	} },
};
static const float PICT_SRC_SIZE    = 512.0f;	// 図の原寸(px)
static const float PICT_SIZE_RATIO  = 1.6f;		// 図の大きさ(キーの絵の大きさに対する比)
static const int   PICT_HALO_DIRS   = 8;		// 縁を作る為にずらして描く方向の数
static const ImU32 PICT_HALO_COL    = IM_COL32(20, 22, 26, 230);	// 縁の色(暗い)

//--- 画像を回して描く(4隅を支点まわりに回す)。
static void DrawImageRotated(ImDrawList* dl, ImTextureID id, ImVec2 topLeft, float size, ImVec2 pivot, float angle, ImU32 col)
{
	const float cs = cosf(angle), sn = sinf(angle);
	auto rot = [&](float x, float y) { const float rx = x - pivot.x, ry = y - pivot.y; return ImVec2(pivot.x + rx * cs - ry * sn, pivot.y + rx * sn + ry * cs); };
	dl->AddImageQuad(id, rot(topLeft.x, topLeft.y), rot(topLeft.x + size, topLeft.y), rot(topLeft.x + size, topLeft.y + size), rot(topLeft.x, topLeft.y + size),
	                 ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
}

//--- 指示図の画像(置き場所 + ファイル名)。TextureCache = 一度だけ読み込み、以後は使い回す。
static std::shared_ptr<Texture> PictTexture(const char* file)
{
	return TextureCache::Get((std::string(LESSON_PICT_DIR) + file).c_str());
}

//--- 指示図を描く。中心(画面 px)と濃さ・大きさ・色。表に無い課は何もせず false。
bool SceneForge::DrawLessonPicture(LessonId id, float centerX, float centerY, float alpha, float scale, unsigned int tint)
{
	const LessonPicture* pic = nullptr;
	for (const LessonPicture& p : LESSON_PICTURES) if (p.id == id) { pic = &p; break; }
	if (!pic || pic->frameCount <= 0) return false;

	// 今のコマ(ステップ補間 = コマの間を補間しない。各コマで止まり、次のコマへ跳ぶ)。時計は設定の速さを掛けた m_lessonClock
	float total = 0.0f;
	for (int i = 0; i < pic->frameCount; ++i) total += pic->frames[i].hold;
	float t = fmodf(m_lessonClock, total);
	const LessonPictFrame* f = &pic->frames[0];
	for (int i = 0; i < pic->frameCount; ++i) { if (t < pic->frames[i].hold) { f = &pic->frames[i]; break; } t -= pic->frames[i].hold; }

	std::shared_ptr<Texture> base = PictTexture(f->base), pose = PictTexture(f->pose);
	if (!base || !base->GetResource() || !pose || !pose->GetResource()) return false;	// 画像がまだ無い課は出さない(キーの絵だけ)

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * PICT_SIZE_RATIO * scale;	// 画面上の図の大きさ(台も姿勢も同じ四角に描く=原寸の位置合わせが保たれる)
	const float k    = size / PICT_SRC_SIZE;										// 原寸 px → 画面 px
	const ImVec2 tl(centerX - size * 0.5f, centerY - size * 0.5f);
	const int   a   = (int)(255 * alpha * ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
	const ImU32 col = (tint & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
	const ImU32 halo = (PICT_HALO_COL & ~IM_COL32_A_MASK) | ((ImU32)(((PICT_HALO_COL >> IM_COL32_A_SHIFT) & 0xFF) * alpha) << IM_COL32_A_SHIFT);

	// 台(影 → 本体)
	const ImTextureID bid = (ImTextureID)base->GetResource();
	dl->AddImage(bid, ImVec2(tl.x + LESSON_SHADOW_PX, tl.y + LESSON_SHADOW_PX), ImVec2(tl.x + size + LESSON_SHADOW_PX, tl.y + size + LESSON_SHADOW_PX),
	             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, (int)(a * 0.7f)));
	dl->AddImage(bid, tl, ImVec2(tl.x + size, tl.y + size), ImVec2(0, 0), ImVec2(1, 1), col);

	// 姿勢: (縁がある図は)暗い縁を全方向へ少しずらして描く → 本体。縁が下の線を切る=重なっても形が分かれて見える
	const ImTextureID pid = (ImTextureID)pose->GetResource();
	const ImVec2 ptl(tl.x + f->dx * k, tl.y + f->dy * k);
	const ImVec2 pivot(ptl.x + f->pivotX * k, ptl.y + f->pivotY * k);
	if (pic->halo > 0.0f)
	{
		const float halo_px = pic->halo * k;
		for (int i = 0; i < PICT_HALO_DIRS; ++i)
		{
			const float ang = XM_2PI * i / PICT_HALO_DIRS;
			const ImVec2 o(cosf(ang) * halo_px, sinf(ang) * halo_px);
			DrawImageRotated(dl, pid, ImVec2(ptl.x + o.x, ptl.y + o.y), size, ImVec2(pivot.x + o.x, pivot.y + o.y), f->tilt, halo);
		}
	}
	else	// 縁の無い図も、明るい背景(炉の前)で沈まない様に影だけは付ける
		DrawImageRotated(dl, pid, ImVec2(ptl.x + LESSON_SHADOW_PX, ptl.y + LESSON_SHADOW_PX), size, ImVec2(pivot.x + LESSON_SHADOW_PX, pivot.y + LESSON_SHADOW_PX), f->tilt, IM_COL32(0, 0, 0, (int)(a * 0.7f)));
	DrawImageRotated(dl, pid, ptl, size, pivot, f->tilt, col);
	return true;
}

//--- 課の絵の中心(画面上)。普段は画面中央の刃の少し下。WASD だけは画面の左下:
//    歩いている間は行き先を見る為に画面中央が空いている方が良く、中央には物の上の「E」の提示も出る(重なっていた。2026-10-12 ユーザー指定の位置)。
XMFLOAT2 SceneForge::LessonCenter(LessonAnim anim) const
{
	const float WALK_X_RATIO = 0.10f;	// WASD の中心(画面幅比)= 左下
	const float WALK_Y_RATIO = 0.74f;	// 同(画面高さ比)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	if (anim == LessonAnim::Walk) return XMFLOAT2(disp.x * WALK_X_RATIO, disp.y * WALK_Y_RATIO);
	return XMFLOAT2(disp.x * LESSON_CENTER_X, disp.y * LESSON_CENTER_Y);
}

//--- WASD: 4つのキーを並べ(上段 W / 下段 A S D)、順に押される。
//    手柄: 左スティックの絵が、上→左→下→右 と倒される(キーと同じ順・同じ間隔)。
void SceneForge::DrawWalkLesson(float alpha, float scale, unsigned int tint)
{
	const float KEY_RATIO = 0.55f;		// 1つのキーの大きさ(アイコンの大きさに対する比)
	const float STEP_SEC  = 0.35f;		// 1つのキーを押している秒
	const float STICK_TILT = 0.12f;		// 手柄: スティックの絵を倒した向きへずらす量(アイコンの大きさに対する比)
	static const float OFFS[4][2] = { { 0.0f, -1.0f }, { -1.0f, 0.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f } };	// キー1つ分の並び(W A S D)
	static const float TILT[4][2] = { { 0.0f, -1.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f } };	// 同じ順に倒す向き(上 左 下 右)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const XMFLOAT2 base = LessonCenter(LessonAnim::Walk);
	const int now = (int)(m_lessonClock / STEP_SEC) % 4;
	if (InputPrompts::Device() == InputDevice::Gamepad)
	{
		const float size = disp.y * LESSON_ICON_RATIO * scale;
		const float tilt = size * STICK_TILT;
		DrawLessonIcon(dl, InputPrompts::Icon(AxisPrompt::Move), ImVec2(base.x + TILT[now][0] * tilt, base.y + TILT[now][1] * tilt), size, alpha, tint);
		return;
	}
	const char* keys[4] = {};
	InputPrompts::Icons(AxisPrompt::Move, keys, 4);	// W A S D(割り当て表の順)
	const float k = disp.y * LESSON_ICON_RATIO * scale * KEY_RATIO;
	const ImVec2 c(base.x, base.y + k * 0.5f);	// 2段の中心が案内の中心に来る様に
	for (int i = 0; i < 4; ++i)
	{
		if (!keys[i]) continue;
		const bool pressed = (i == now);
		const std::string name = std::string(keys[i]) + (pressed ? "" : "_outline");
		DrawLessonIcon(dl, name.c_str(), ImVec2(c.x + OFFS[i][0] * k, c.y + OFFS[i][1] * k + (pressed ? k * LESSON_PRESS_SINK : 0.0f)), k, alpha, tint);
	}
}

//--- 今、研げている = 左(刃を当てる)を押していて、砥石が研げる速さで回っている。
bool SceneForge::GrindRunning() const
{
	return InputActions::Held(Action::GrindPress) && m_wheel.Speed01() >= GRIND_WORK_MIN_SPEED;
}

//--- 研ぐ基本: 左の長押しと右の連打を左右に並べる(2026-10-12 ユーザーの絵)。
//    今できている方を薄くする(LESSON_DONE_ALPHA)= 目立っている方が「まだ足りない操作」。両方薄い = 研げている。
//    左 = 押していれば済 / 右 = 砥石が研げる速さで回っていれば済(踏む事の結果で判定=連打が遅ければ済にならない)。
void SceneForge::DrawGrindRunLesson(float alpha, float scale, unsigned int tint, float xShift)
{
	const float GAP          = 0.95f;	// 中心から各キーの絵の中心まで(キーの絵の大きさに対する比)
	const float DONE_ALPHA   = 0.3f;	// できている方の濃さ(ユーザー指定)
	const float HOLD_SEC = 1.4f, REST_SEC = 0.5f;	// 長押しの見本(押して輪が溜まる → 一呼吸)
	const float TAP_PERIOD = 0.35f, TAP_PRESS_FRAC = 0.4f;	// 連打の見本(ペダルの絵と同じ間隔)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float size = disp.y * LESSON_ICON_RATIO * scale;
	const ImVec2 c(disp.x * LESSON_CENTER_X + xShift, disp.y * LESSON_CENTER_Y);
	const bool  holdOK = InputActions::Held(Action::GrindPress);
	const bool  tapOK  = m_wheel.Speed01() >= GRIND_WORK_MIN_SPEED;
	std::string holdOutline, tapOutline;
	const char* holdKey = LessonKeyIcon(Action::GrindPress, holdOutline);	// 左クリック / LT
	const char* tapKey  = LessonKeyIcon(Action::Pedal,      tapOutline);	// 右クリック / RT
	// 絵の左右は手で押す位置と同じ: マウスも手柄も 左 = 長押し(左ボタン / LT)・右 = 連打(右ボタン / RT)
	const float  side = 1.0f;

	// 長押し(押している間は押した絵のまま薄く。押していなければ見本を繰り返す)
	const ImVec2 lc(c.x - side * size * GAP, c.y);
	const float  la = alpha * (holdOK ? DONE_ALPHA : 1.0f);
	if (holdOK) { DrawLessonIcon(dl, holdKey, ImVec2(lc.x, lc.y + size * LESSON_PRESS_SINK), size, la, tint); DrawLessonRing(dl, lc, size, 1.0f, la); }
	else
	{
		const float t = fmodf(m_lessonClock, HOLD_SEC + REST_SEC);
		if (t < HOLD_SEC) { DrawLessonIcon(dl, holdKey, ImVec2(lc.x, lc.y + size * LESSON_PRESS_SINK), size, la, tint); DrawLessonRing(dl, lc, size, t / HOLD_SEC, la); }
		else              { DrawLessonIcon(dl, holdOutline.c_str(), lc, size, la, tint); DrawLessonRing(dl, lc, size, 0.0f, la); }
	}

	// 連打(ペダルと同じ間隔で押す ↔ 離すを繰り返す。回っていれば薄く)
	const ImVec2 rc(c.x + side * size * GAP, c.y);
	const float  ra = alpha * (tapOK ? DONE_ALPHA : 1.0f);
	const float  p  = fmodf(m_lessonClock, TAP_PERIOD) / TAP_PERIOD;
	if (p < TAP_PRESS_FRAC) DrawLessonIcon(dl, tapKey, ImVec2(rc.x, rc.y + size * LESSON_PRESS_SINK), size, ra, tint);
	else                    DrawLessonIcon(dl, tapOutline.c_str(), rc, size, ra, tint);

	// 上に「長押し」「連打」(どちらがどの押し方かを文字でも)
	DrawLessonLabel(dl, (const char*)u8"長押し", lc, size, la);
	DrawLessonLabel(dl, (const char*)u8"連打",   rc, size, ra);
}

//--- 課の絵を種類で描き分ける(表の LessonAnim)。
void SceneForge::DrawLessonAnim(const LessonDef& d, float alpha, float scale, unsigned int tint)
{
	switch (d.anim)
	{
	case LessonAnim::MouseVertical:   DrawAxisLesson(AxisPrompt::HandVertical,   0, alpha, scale, tint); break;	// 狙う / 揺する = 手
	case LessonAnim::GrindAngle:
	{
		// 救急(誤りを重ねた)なら直す向きだけ: 寝かせすぎ → 上へ(立てる) / 立てすぎ → 下へ(寝かせる)。教える時は往復
		const bool rescue = m_lessonShownRescue && m_lessonShown == (int)LessonId::GrindAngle;
		DrawAxisLesson(AxisPrompt::TiltVertical, rescue ? (m_grindTooFlat ? -1 : +1) : 0, alpha, scale, tint);	// 刃の角度 = 回す
		break;
	}
	case LessonAnim::GrindRun:      DrawGrindRunLesson(alpha, scale, tint); break;
	case LessonAnim::GrindSlide:
	{
		// 「長押し+連打を続けたまま、左右に滑らせる」= 3つを1列に。左右だけを見せると、押すのを止めてよいと誤解された(2026-10-19 ユーザー)
		const float PAIR_SHIFT  = 1.0f;		// 長押し+連打の組を左へずらす量(キーの絵の大きさに対する比)
		const float SLIDE_SHIFT = 1.75f;	// 左右の絵を右へずらす量(同)。組の右の絵と矢印が重ならない間隔
		const float size = ImGui::GetIO().DisplaySize.y * LESSON_ICON_RATIO * scale;
		DrawGrindRunLesson(alpha, scale, tint, -size * PAIR_SHIFT);
		DrawAxisLesson(AxisPrompt::HandHorizontal, 0, alpha, scale, tint, size * SLIDE_SHIFT, (const char*)u8"左右");
		break;
	}
	case LessonAnim::GrindFlipHold:
		if (m_grindFlipping) DrawHoldProgressPrompt(Action::Flip, m_grindFlipHold / GRIND_FLIP_SEC, InputActions::Held(Action::Flip), nullptr, alpha, scale, tint);
		else                 DrawHoldKeyLesson(Action::Flip, alpha, scale, tint);
		break;
	case LessonAnim::StrikeHold:    DrawStrikeHoldLesson(alpha, scale, tint);       break;
	case LessonAnim::FlipHold:      DrawFlipHoldPrompt(alpha, scale, tint, nullptr); break;	// 一言は下で表の text を描く
	case LessonAnim::PressKey:      DrawTapKeyLesson(d.key, alpha, scale, tint);    break;
	case LessonAnim::Walk:          DrawWalkLesson(alpha, scale, tint);             break;
	case LessonAnim::PointGauge:
	{
		float lo, hi;
		if (LessonGaugeRange(d.id, lo, hi)) DrawGaugePointLesson(lo, hi, alpha, scale);
		break;
	}
	}
}

//--- 出している課を描く: 淡入/淡出 + 一言。チュートリアルで長く進まなければ脈打つ(アイドルヒント)。
//    できた瞬間(+LESSON_POP_DELAY)は、その絵が金色に光りながら膨らんで消え、金の輪が弾ける(=「できた！」の手応え)。
void SceneForge::DrawLessonPrompts()
{
	const float FADE_LAMBDA = 6.0f;		// 出る/消える速さ(Damp率, 1/秒)=淬火/研ぎの案内と同じ
	const float VISIBLE_EPS = 0.01f;
	const float POP_SEC     = 0.6f;		// できた演出の長さ
	const float POP_GROW    = 0.35f;	// 演出の間にアイコンが膨らむ量(元の大きさに対する比)
	const float POP_BURST   = 2.4f;		// 金の輪が広がる大きさ(元の半径の倍率)
	const float NUDGE_GROW  = 0.12f;	// 脈打つ時に膨らむ量(元の大きさに対する比)
	const float NUDGE_HZ    = 2.0f;		// 脈打つ速さ(回/秒)
	const ImU32 WHITE       = IM_COL32(255, 255, 255, 255);
	const ImU32 GOLD        = IM_COL32(255, 214, 120, 255);	// できた色(金)
	const float dt = ImGui::GetIO().DeltaTime;
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	for (const LessonDef& d : LESSONS)
	{
		LessonState& l = m_lessons[(int)d.id];
		const XMFLOAT2 cf = LessonCenter(d.anim);	// 絵の中心(一言・弾ける輪もここに合わせる)
		const ImVec2 c(cf.x, cf.y);
		const float sincePop = m_time - l.popAt;
		const bool  waitPop  = l.popVisible && sincePop < 0.0f;		// できた〜弾けるまでの間(LESSON_POP_DELAY)は今の絵のまま
		const bool  show     = (m_lessonShown == (int)d.id) || waitPop;
		l.alpha = Lerp::Damp(l.alpha, show ? 1.0f : 0.0f, FADE_LAMBDA, dt);

		float alpha = l.alpha, scale = 1.0f;
		ImU32 tint = WHITE;
		const bool popping = l.popVisible && sincePop >= 0.0f && sincePop < POP_SEC;
		if (popping)
		{
			const float p    = sincePop / POP_SEC;
			const float ease = 1.0f - (1.0f - p) * (1.0f - p);	// ease-out: 最初に大きく膨らみ、ゆっくり止まる
			scale = 1.0f + POP_GROW * ease;
			alpha = 1.0f - p;
			tint  = GOLD;
			DrawLessonBurst(dl, c, disp.y * LESSON_ICON_RATIO, ease, 1.0f, GOLD, POP_BURST);
		}
		else if (alpha < VISIBLE_EPS) continue;
		else if (m_tutorial && !m_lessonShownRescue && m_lessonShown == (int)d.id && l.idle >= LESSON_NUDGE_SEC)
			scale = 1.0f + NUDGE_GROW * (0.5f + 0.5f * sinf(m_time * NUDGE_HZ * XM_2PI));	// 進まない: 脈打って注意を引く

		// 指示図(何をするか)はキーの絵(どう操作するか)の左に。図の無い課はキーの絵だけ
		//   左に入らない時(WASD は画面の左端)は右へ置く
		const float PICT_LEFT_SINGLE = 1.6f;	// キーの絵の中心から図の中心までの距離(キーの絵の大きさに対する比)
		const float PICT_LEFT_DUAL   = 2.5f;	// キーの絵が2つ並ぶ課(GrindRun)は、その分だけ外へ
		const float PICT_LEFT_TRIPLE = 3.4f;	// 長押し+連打+左右の3つが並ぶ課(GrindSlide)
		const float PICT_LEFT = (d.anim == LessonAnim::GrindSlide) ? PICT_LEFT_TRIPLE : (d.anim == LessonAnim::GrindRun) ? PICT_LEFT_DUAL : PICT_LEFT_SINGLE;
		const float off = disp.y * LESSON_ICON_RATIO * PICT_LEFT, half = disp.y * LESSON_ICON_RATIO * PICT_SIZE_RATIO * 0.5f;	// half = 図の半分の幅
		const float px = (c.x - off - half >= 0.0f) ? c.x - off : c.x + off;
		DrawLessonPicture(d.id, px, c.y, alpha, scale, tint);
		DrawLessonAnim(d, alpha, scale, tint);
		if (!popping) DrawLessonText(dl, d.text, c, disp.y * LESSON_ICON_RATIO * scale, alpha);
	}
}

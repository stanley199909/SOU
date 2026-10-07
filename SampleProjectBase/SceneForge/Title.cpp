// Part of SceneForge: タイトル画面(ゲーム世界そのものを固定カメラで映す)。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// ・舞台: 金床の上に熱い鉄。鎚が一定間隔で振りかぶって打つ(火花 + 金床音)。将来ここに矮人が立つ。
// ・カメラ: 固定の標題カメラ(m_titleCamPos/Look。F1「Title」で取景し forge_tuning.txt に保存)。
// ・SPACE: UpdateTitle(SceneForge.cpp) がゲームを開始し、標題カメラ→金床の工位へ導入運鏡。
//          運鏡は走動⇔工位と同じ仕組み(ModeTrans::Enter)を、長さだけ m_titleIntroTime にして再利用。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Audio.h"

using namespace DirectX;

void SceneForge::ResetTitleStage()
{
	m_forging.Reset();
	m_forging.SetHeat(START_HEAT);	// 赤熱した鉄(光って見える)
	m_forgeProg = 0.0f;				// タイトルの鉄の形/黒皮はこの値で描く(非プレイ時)。前の周回の完成形を持ち越さない
									// (旧: 結果→タイトルで完成した剣が残り、開始時に粗坯+黒皮へ急に変わって見えた)
	m_station = Station::Anvil; m_workAt = Station::Anvil; m_carrying = false; m_restFlip = false;
	m_flipAngle = 0.0f;
	m_plunge = 0.0f; m_quenchTurn = 0.0f; m_agitate = 0.0f; m_quenchContact = false; m_clearDecided = false; m_letterbox = 0.0f; m_boil = 0.0f; m_boilStage = BoilStage::None; m_waterSim.Reset(); m_prevAgitate = 0.0f; m_bubbleAcc = 0.0f;
	m_stirTarget = 0.0f; m_filmBreak = 0.0f; m_filmTime = 0.0f; m_stir01 = 0.0f; m_quenchStartHeat = 0.0f;	// 前の周回の淬火/終幕を片付ける
	m_hammer.Reset();
	m_autoTimer = 0.0f;
	m_introPhase = IntroPhase::None;
	m_introTimer = 0.0f;
}

//--- 導入の段階を進める(ユーザーの演出指示: 一つ終わってから次へ)。
//    ① LogoFade  : ロゴが m_logoFadeTime 秒で線形に消える(タイトルのまま。鎚は打ち続ける)。
//    ② CameraMove: 消え切ったらゲームを開始し、カメラを標題→金床へ(走動⇔工位と同じ過渡を長さだけ変えて再利用)。
//                  その間タイトル BGM の音量を「カメラの進み」に合わせて 1→0。
//    ③ 到着     : UpdateModeTrans が FinishIntro を呼ぶ。
void SceneForge::UpdateIntro(float tick)
{
	switch (m_introPhase)
	{
	case IntroPhase::LogoFade:
		m_introTimer += tick;
		if (m_introTimer >= m_logoFadeTime)
		{
			StartGame();
			BeginEnterStation(Station::Anvil);	// 開始視点=今映っている標題カメラ
			m_transDur   = m_titleIntroTime;	// 通常の工位移動より長く=映画的にゆっくり
			m_introPhase = IntroPhase::CameraMove;
		}
		break;
	case IntroPhase::CameraMove:
	{
		const float t = (m_transDur > 0.0f) ? m_transTimer / m_transDur : 1.0f;
		const float left = 1.0f - (t > 1.0f ? 1.0f : t);
		Audio::SetLoop(Audio::BGM_TITLE, TITLE_BGM_VOLUME * left, 1.0f);	// 止めずに音量だけ下げる
		break;
	}
	case IntroPhase::None:
		break;
	}
}

void SceneForge::FinishIntro()
{
	Audio::Stop(Audio::BGM_TITLE);							// 淡出し切ったタイトル BGM を止める
	Audio::PlayLoop(Audio::BGM_PLAY, PLAY_BGM_VOLUME);		// ゲーム BGM を正式に開始
	m_introPhase = IntroPhase::None;
}

void SceneForge::ApplyTitleCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	cam->SetFovY(m_camFov);
	cam->SetPos (XMFLOAT3(m_titleCamPos[0],  m_titleCamPos[1],  m_titleCamPos[2]));
	cam->SetLook(XMFLOAT3(m_titleCamLook[0], m_titleCamLook[1], m_titleCamLook[2]));
	cam->SetUp  (XMFLOAT3(0.0f, 1.0f, 0.0f));
}

//--- 鎚が「振りかぶる → 打つ → 跳ね返る」を繰り返す。ゲーム中と同じ弾簧-阻尼の物理(HammerPhysics)を使う:
//    間隔の終わり TITLE_WINDUP 秒だけ Hold で持ち上げ(ゲームの蓄力と同じ)、Strike で振り下ろし、
//    その後はバネが自然に跳ね返して静止高へ戻す。
void SceneForge::UpdateTitleHammer(float tick)
{
	m_aimWorld = m_barAnchor;	// 鉄の中央を打つ
	UpdateHammerFollow(tick);

	m_autoTimer += tick;
	const float windStart = TITLE_INTERVAL - TITLE_WINDUP;
	if (m_autoTimer >= TITLE_INTERVAL)
	{
		m_autoTimer = 0.0f;
		m_hammer.Strike();
		Strike();				// 火花
		Audio::Play(m_hammerAlt ? Audio::SE_ANVIL2 : Audio::SE_ANVIL1, TITLE_STRIKE_VOLUME);
		m_hammerAlt = !m_hammerAlt;
	}
	else if (m_autoTimer >= windStart)
		m_hammer.Hold((m_autoTimer - windStart) / TITLE_WINDUP);	// 0→1 で振りかぶる
	else
		m_hammer.Update(tick);	// 打った後の跳ね返り→静止
}

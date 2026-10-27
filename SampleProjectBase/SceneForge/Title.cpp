// Part of SceneForge: タイトル画面(ゲーム世界そのものを固定カメラで映す)。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// ・舞台: 金床の上に熱い鉄。鎚が一定間隔で振りかぶって打つ(火花 + 金床音)。将来ここに矮人が立つ。
// ・カメラ: 固定の標題カメラ(m_titleCamPos/Look。F4 のボタンで取景し forge_tuning.txt に保存)。
// ・SPACE: UpdateTitle(SceneForge.cpp) がゲームを開始し、標題カメラ→金床の工位へ導入運鏡。
//          運鏡は走動⇔工位と同じ仕組み(ModeTrans::Enter)を、長さだけ m_titleIntroTime にして再利用。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "CameraBase.h"
#include "Audio.h"

using namespace DirectX;

void SceneForge::ResetTitleStage()
{
	// 鉄・工位・工程・案内・光…1回分の状態は全部共通の1か所で戻す(ResetRunState)。
	//   鉄は赤熱(START_HEAT)、形は粗坯(m_forgeProg 0 = タイトルの鉄の形/黒皮)=前の周回の完成形を持ち越さない。
	ResetRunState();
	m_autoTimer = 0.0f;
	m_introPhase = IntroPhase::None;
	m_introTimer = 0.0f;
	m_modeSelectOpen = false;	// タイトルに戻った時はモード選択を閉じた状態から(SPACE でまた開く)
	m_courseSelectOpen = false;	// 課程の一覧も閉じる(課程を終えて戻る時は ReturnToTitle が開き直す)
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
			// チュートリアルの課程ならその工程の所から(鉄をその工位に置き、カメラはそこへ入る)、通常モードは最初から(金床)
			Station first = Station::Anvil;
			if (m_courseIdx >= 0 && m_recipe && m_courseIdx < (int)m_recipe->courses.size())
			{
				const int step = m_recipe->courses[m_courseIdx].firstStep;
				StartAtStep(step, false);
				first = StepStation(m_recipe->steps[step].type);
			}
			else StartGame();
			BeginEnterStation(first);	// 開始視点=今映っている標題カメラ
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
//    間隔の終わり TITLE_WINDUP 秒だけ Hold で持ち上げ(ゲームの蓄力と同じ)、Release で振り下ろし(届いたら火花と音)、
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
		m_hammer.Release(1.0f);	// 振りかぶり切って振り下ろす(届いた瞬間に火花と音。下の ConsumeContact)
	}
	else if (m_autoTimer >= windStart)
		m_hammer.Hold((m_autoTimer - windStart) / TITLE_WINDUP);	// 0→1 で振りかぶる
	else
		m_hammer.Update(tick);	// 振り下ろし → 鉄に届く → 跳ね返り → 静止
	float impactSpeed = 0.0f;
	if (m_hammer.ConsumeContact(impactSpeed))	// 鉄に届いた = ゲームの打撃と同じ流れ(熱い鉄なので受け止められて弾み少なめ)
	{
		m_hammer.Rebound(m_hammer.restitutionHot);
		Strike();				// 火花
		Audio::PlayAt(m_hammerAlt ? Audio::SE_ANVIL2 : Audio::SE_ANVIL1, m_aimWorld, TITLE_STRIKE_VOLUME);
		m_hammerAlt = !m_hammerAlt;
	}
}

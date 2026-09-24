// Part of SceneForge, split out of the original single SceneForge.cpp.
// This file carries a UTF-8 BOM so the Japanese comments moved from the
// original source keep compiling correctly under MSVC (no C2601/C1075).
// All SceneForge members share the class declaration in SceneForge.h and the
// file-local helpers declared in SceneForge_Internal.h.
#include "CottageRender.h"
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "CameraBase.h"
#include "LightBase.h"
#include "Model.h"
#include "Geometory.h"
#include "Input.h"
#include "DebugUI.h"
#include "Defines.h"
#include "Audio.h"
#include "PostProcess.h"
#include "AimSystem.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

using namespace DirectX;

//====================================================================
//  UI (HUD)
//====================================================================

// 画面中央にウィンドウ枠なしのメッセージを出す小道具。
//   font=nullptr なら既定フォント(メイリオ)。scale は各フォントの実寸への倍率。
//   読みやすさのため暗い影を1枚下に敷く(3D背景の上でも文字が沈まない)。
static void CenterText(const char* text, float yRatio, float scale = 1.0f,
                       ImU32 col = IM_COL32(255, 255, 255, 255), ImFont* font = nullptr)
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;	// 実際の画面サイズ(解像度非依存)
	ImFont* f = font ? font : ImGui::GetFont();
	float px = f->FontSize * scale;
	ImVec2 sz = f->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
	float x = (disp.x - sz.x) * 0.5f;
	float y =  disp.y * yRatio - sz.y * 0.5f;
	ImU32 shadow = IM_COL32(0, 0, 0, (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * 0.6f));
	dl->AddText(f, px, ImVec2(x + 2.0f, y + 2.0f), shadow, text);	// 影
	dl->AddText(f, px, ImVec2(x, y), col, text);					// 本体
}

//--- 羊皮紙パネルを画面中央に敷く(結果/失敗画面の下地)。yCenter/height は画面比。
void SceneForge::DrawParchmentPanel(float yCenter, float heightRatio)
{
	if (!m_uiParchment || !m_uiParchment->GetResource()) return;
	ImDrawList* dl = ImGui::GetBackgroundDrawList();	// 文字より奥に敷く
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	float aspect = (float)m_uiParchment->GetWidth() / (float)m_uiParchment->GetHeight();
	float h = disp.y * heightRatio;
	float w = h * aspect;
	if (w > disp.x * 0.92f) { w = disp.x * 0.92f; h = w / aspect; }	// 横がはみ出す時は幅で制限
	float cx = disp.x * 0.5f, cy = disp.y * yCenter;
	ImVec2 a(cx - w * 0.5f, cy - h * 0.5f), b(cx + w * 0.5f, cy + h * 0.5f);
	dl->AddImage((ImTextureID)m_uiParchment->GetResource(), a, b);
}

//--- 温度(0..1)を鋼の色に変換する(暗赤→赤→橙→黄→白熱)。
//    勾配表は HeatSampleRGB(SceneForge.cpp)と共有(HUDは金属下限を掛けず生の色を見せる)。
static ImU32 HeatColor(float h, float alpha = 1.0f)
{
	float r, g, b; HeatSampleRGB(h, r, g, b);
	return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(alpha * 255));
}

//--- 温度ゲージ(HUD)
void SceneForge::DrawHeatGauge()
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	const float x0 = disp.x * 0.25f;
	const float x1 = disp.x * 0.75f;
	const float y  = disp.y * 0.80f;
	const float hgt = 20.0f;
	auto lerpX = [&](float t) { return x0 + (x1 - x0) * t; };

	// トラック
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(30, 30, 34, 220), 4.0f);
	// 最適温度帯(緑の帯)
	dl->AddRectFilled(ImVec2(lerpX(IDEAL_MIN), y), ImVec2(lerpX(IDEAL_MAX), y + hgt),
		IM_COL32(60, 160, 70, 150));
	// 過熱帯(赤の帯)
	dl->AddRectFilled(ImVec2(lerpX(OVERHEAT), y), ImVec2(x1, y + hgt),
		IM_COL32(180, 40, 40, 160));
	// 現在温度の塗り
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(lerpX(m_heat), y + hgt), HeatColor(m_heat), 4.0f);
	// マーカー
	dl->AddLine(ImVec2(lerpX(m_heat), y - 5), ImVec2(lerpX(m_heat), y + hgt + 5),
		IM_COL32(255, 255, 255, 255), 2.0f);
	// 枠
	dl->AddRect(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(200, 200, 200, 120), 4.0f);

	// KCD式: 温度は緑帯(適温)/赤帯(過熱)の視覚だけで示す。「加熱しろ」等の指示テキストは出さない。
}

void SceneForge::DrawTitleUI()
{
	ImFont* title = DebugUI::FontTitle();
	ImFont* body  = DebugUI::FontBody();
	CenterText("FORGE",                0.32f, 1.5f, IM_COL32(255, 196, 110, 255), title);
	CenterText("A  T I M I N G   B L A C K S M I T H", 0.44f, 0.9f, IM_COL32(230, 215, 195, 235), body);
	// 開始プロンプトは緩やかに明滅させて「操作可能」を伝える
	float p = 0.6f + 0.4f * sinf(m_time * 3.0f);
	CenterText("PRESS  SPACE  TO  START", 0.66f, 1.15f, IM_COL32(255, 255, 255, (int)(255 * p)), body);
}

void SceneForge::DrawPlayUI()
{
	// 鉄条とハンマーは3Dで描画するので、2Dの鉄条(DrawBillet/DrawHammer)は使わない

	// 温度ゲージ
	DrawHeatGauge();

	// 現在の工程の指示文(宏観チュートリアル=「今この工程で何をするか」)。工程が進むと自動で変わる。
	//   文言は配方(GameData/WeaponRecipe)が持つ=換武器で自動的に差し替わる(コードにベタ書きしない)。
	CenterText(CurrentStep().instruction, 0.12f, 1.3f, IM_COL32(255, 240, 200, 255));

	// 翻面の子状態ごとの操作ヒント(宏観チュートリアル)。運鏡ビート中は状況説明、
	// 操作待ちの Ready/Flipping では「何のキーで何が起きるか」を明示する。
	switch (m_flipPhase)
	{
	case FlipPhase::TongsOut: CenterText("Reaching for the tongs...", 0.24f, 1.1f, IM_COL32(255, 230, 180, 220)); break;
	case FlipPhase::Ready:    CenterText("Tongs ready --  LMB: grip the blade    F: put tongs back", 0.24f, 1.1f, IM_COL32(255, 240, 200, 255)); break;
	case FlipPhase::Gripping: CenterText("Gripping the blade...", 0.24f, 1.1f, IM_COL32(255, 230, 180, 220)); break;
	case FlipPhase::Flipping: CenterText("Move the mouse to turn the blade    LMB: set this face", 0.24f, 1.1f, IM_COL32(255, 240, 200, 255)); break;
	case FlipPhase::PutBack:  CenterText("Setting the tongs back...", 0.24f, 1.1f, IM_COL32(255, 230, 180, 220)); break;
	default: break;
	}

	// ※KCD2式: 画面中心の準心は「置かない」。第一人称に固定十字は不自然で、しかも屏幕中央に
	//   死んでいて動かせない。狙いの提示は「動くハンマー＋刃の高亮段」で行う(下の WeaponRender)。

	// 過熱の警告(点滅)
	if (m_heat > OVERHEAT)
	{
		float p = 0.5f + 0.5f * sinf(m_time * 12.0f);
		CenterText("!!  OVERHEAT  !!", 0.20f, 1.6f, IM_COL32(255, 70, 50, (int)(150 + p * 105)));
	}

	// 打撃フィードバックのポップアップ(鉄条の上でフェード)
	if (m_popupLife > 0.0f)
	{
		float a = m_popupLife / POPUP_LIFE;
		if (a > 1.0f) a = 1.0f;
		unsigned int c = (m_popupCol & 0x00FFFFFF) | ((unsigned int)(a * 255) << 24);
		CenterText(m_popupText, 0.36f, 2.0f, c);
	}

	// スコアと形状一致度(左上)
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	char sb[48];
	sprintf_s(sb, sizeof(sb), "SCORE  %d", m_score);
	dl->AddText(ImVec2(40, 40), IM_COL32(255, 235, 200, 255), sb);
	sprintf_s(sb, sizeof(sb), "SHAPE MATCH  %d%%", (int)(m_match * 100));
	dl->AddText(ImVec2(40, 60), IM_COL32(150, 220, 255, 255), sb);

	// 一致度バー(上部中央)
	{
		ImVec2 disp = ImGui::GetIO().DisplaySize;
		float bx0 = disp.x * 0.30f, bx1 = disp.x * 0.70f, by = 30.0f, bh = 14.0f;
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(30, 30, 34, 220), 3.0f);
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * m_match, by + bh),
			IM_COL32(90, 200, 255, 255), 3.0f);
		dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(200, 200, 200, 120), 3.0f);
	}

	// KCD式: 「叩く場所」は指示しない。誤打時だけ主人公の独白(m_popupText)で知らせる。
	//   デバッグ時のみ Pキーで瞄準区域の可視化ON(状態表示)。
	if (m_showAimHi)
		CenterText("[DEBUG] aim highlight ON (P to toggle)", 0.10f, 0.9f, IM_COL32(120, 220, 160, 180));

	// 淬火の準備ができたら促す
	if (m_match >= 0.85f)
		CenterText("Shape looks good!  Press  Q  to Quench", 0.86f, 1.2f,
			IM_COL32(150, 255, 180, 230));

	// 操作ガイド
	CenterText("Mouse : Aim    Hold L-MOUSE : Hammer    Hold R : Heat    Q : Quench",
		0.93f, 1.0f, IM_COL32(255, 255, 255, 170));
}

//--- 出来栄え 0..1: 形の一致度と打撃品質の平均を重み合成する。
//    誤打(冷打/過熱/完成済みを叩く)は品質0の打撃として平均を下げる=罰でなく「腕前」として自然に効く。
float SceneForge::GradeScore() const
{
	// 打撃品質の平均(1打も打たずに淬火した場合は0扱い=除算回避)。
	float qAvg = (m_strikeCount > 0) ? (m_qualitySum / (float)m_strikeCount) : 0.0f;
	float s = GRADE_W_MATCH * m_match + GRADE_W_QUALITY * qAvg;
	if (s < 0.0f) s = 0.0f;
	if (s > 1.0f) s = 1.0f;
	return s;
}

//--- 出来栄えを S/A/B/C に量子化(閾値は header の GRADE_*)。
char SceneForge::GradeLetter() const
{
	float s = GradeScore();
	if (s >= GRADE_S) return 'S';
	if (s >= GRADE_A) return 'A';
	if (s >= GRADE_B) return 'B';
	return 'C';
}

void SceneForge::DrawResultUI()
{
	ImFont* title = DebugUI::FontTitle();
	ImFont* body  = DebugUI::FontBody();
	// 羊皮紙を下地に敷き、その上に成果を書く。文字色は羊皮紙に映える濃い焦茶。
	DrawParchmentPanel(0.50f, 0.72f);
	const ImU32 ink = IM_COL32(60, 34, 18, 255);
	CenterText("FORGED!",              0.30f, 1.30f, IM_COL32(48, 24, 10, 255), title);	// 濃い鉄墨色=紙上で最も重い

	// --- 等級(S/A/B/C): 一番大きく、等級ごとに色を変えて主役にする ---
	char g = GradeLetter();
	ImU32 gcol;
	switch (g)
	{
	case 'S': gcol = IM_COL32(212, 160,  40, 255); break;	// 金
	case 'A': gcol = IM_COL32(150, 110,  60, 255); break;	// 焦茶(紙上で映える)
	case 'B': gcol = IM_COL32( 90,  70,  45, 255); break;
	default:  gcol = IM_COL32(110,  60,  40, 255); break;	// C
	}
	char gbuf[8]; sprintf_s(gbuf, sizeof(gbuf), "%c", g);
	CenterText(gbuf,                   0.46f, 2.6f, gcol, title);	// 等級=最大サイズ

	char buf[64];
	sprintf_s(buf, sizeof(buf), "SHAPE MATCH   %d%%", (int)(m_match * 100));
	CenterText(buf,                    0.62f, 0.85f, ink, body);
	sprintf_s(buf, sizeof(buf), "SCORE   %d", m_score);
	CenterText(buf,                    0.68f, 0.85f, ink, body);
	CenterText("PRESS  SPACE  TO  RETURN", 0.76f, 0.80f, IM_COL32(90, 55, 30, 255), body);
}

void SceneForge::DrawUI()
{
	// 配置/材質/炭火/カメラの編集はすべて SCENE_STAGE_EDITOR に移設。
	// ゲーム側は焼き込み済みの値で表示するだけ(F1はクリーン)。

	switch (m_state)
	{
	case GAME_TITLE:  DrawTitleUI();  break;
	case GAME_PLAY:   DrawPlayUI();   break;
	case GAME_RESULT: DrawResultUI(); break;
	}

	// F1中: 調整パネルは「1つの窓 + 折叠見出し(CollapsingHeader)」にまとめる。
	//   別々の Begin() 窓を5つ開くと画面を覆って見づらいので、1窓に集約し、既定は全部畳んだ状態。
	//   見出しを開いた区画だけ展開 = 画面を殆ど遮らずに調整できる(imgui の定石)。
	//   区画: Weapon(工件姿勢) / Camera(視点と追従) / Hammer(鎚姿勢と反冲) / Aim / Walk。
	if (DebugUI::IsVisible())
	{
		// 初回のみ左上に配置(以後はユーザーが動かせる)。幅は狭め=画面を占有しない。
		ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(340, 560), ImGuiCond_FirstUseEver);
		ImGui::Begin("Forge Tuning (F1)");
	CottageRender::Controls();

		// --- 起動時スナップショットへ一発リセット(F8キーと同じ)。滅茶苦茶にしても戻せる保険。 ---
		if (ImGui::Button("Reset ALL to startup  (F8)")) RestoreTuning();
		ImGui::SameLine();
		if (ImGui::Button("Save tuning")) SaveTuning();	// 手動保存(退出時にも自動保存)
		ImGui::Separator();

		// --- Weapon: 工件モデルを砧面に合わせる(FBXが読めた時だけ) ---
		if (m_wpOk && ImGui::CollapsingHeader("Weapon"))
		{
			ImGui::Text("stages loaded: %d,  verts: %d", (int)m_wpStage.size(), m_wpN);
			ImGui::SliderFloat("Forge progress", &m_forgeProg, 0.0f, 1.0f, "%.2f");
			ImGui::TextDisabled("-- Orientation --");
			ImGui::SliderFloat("Yaw",   &m_wpYaw,   -3.1416f, 3.1416f, "%.3f");
			ImGui::SliderFloat("Pitch", &m_wpPitch, -3.1416f, 3.1416f, "%.3f");
			ImGui::SliderFloat("Roll",  &m_wpRoll,  -3.1416f, 3.1416f, "%.3f");
			ImGui::TextDisabled("-- Scale / Position --");
			ImGui::SliderFloat("Scale##wp", &m_wpScale, 0.2f, 3.0f, "%.2f");	// ##wp=同窓の"Scale"衝突回避
			ImGui::SliderFloat("Off X", &m_wpOff[0], -1.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Off Y", &m_wpOff[1], -1.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Off Z", &m_wpOff[2], -1.0f, 1.0f, "%.3f");
		}

		// --- Camera: 視点・画角・追従・打撃の揺れ ---
		if (ImGui::CollapsingHeader("Camera"))
		{
			ImGui::TextDisabled("-- View (3/4 forge) --");
			ImGui::SliderFloat3("Cam Pos",  m_camPos,  -5.0f, 6.0f, "%.2f");
			ImGui::SliderFloat3("Cam Look", m_camLook, -5.0f, 6.0f, "%.2f");
			{
				float fovDeg = m_camFov * 57.29578f;			// rad→deg で見せる
				if (ImGui::SliderFloat("Cam FOV", &fovDeg, 25.0f, 70.0f, "%.0f deg"))
					m_camFov = fovDeg * 0.01745329f;			// deg→rad へ戻す
			}
			ImGui::TextDisabled("-- Follow (aim along blade) --");
			ImGui::SliderFloat("Rail (aim)",  &m_aimRail,     0.0f, 1.0f, "%.2f");	// 手前0..奥1(手動確認用)
			ImGui::SliderFloat("Follow Z",    &m_camFollowZ,  0.0f, 1.0f, "%.2f");	// カメラ本体のZ追従割合
			ImGui::SliderFloat("Pan gain",    &m_camPanGain,  0.0f, 2.0f, "%.2f");	// 追従量の倍率
			ImGui::SliderFloat("Cam lerp",    &m_camLerpRate, 0.5f, 12.0f, "%.1f");	// 3段切替の速さ(小=重い)
			ImGui::TextDisabled("-- Impact shake --");
			ImGui::SliderFloat("Cam shake",   &CAM_SHAKE_AMP,  0.0f, 0.2f, "%.3f");	// 打撃のカメラ揺れ
			ImGui::TextDisabled("-- Handheld feel (organic) --");
			ImGui::SliderFloat("Breath amp",  &m_camBreathAmp,   0.0f, 0.08f, "%.3f");	// 呼吸の振幅
			ImGui::SliderFloat("Breath speed",&m_camBreathSpeed, 0.1f, 2.0f,  "%.2f");	// 呼吸の速さ
			ImGui::SliderFloat("Tremor amp",  &m_camTremorAmp,   0.0f, 0.01f, "%.4f");	// 蓄力満時の微顫(既定OFF。極小で試す)
			ImGui::SliderFloat("Tremor speed",&m_camTremorSpeed, 8.0f, 40.0f, "%.0f");	// 微顫の速さ
			ImGui::SliderFloat("Tremor ramp", &m_camTremorRamp,  1.0f, 6.0f,  "%.1f");	// 立ち上がりの遅さ(大=満蓄直前で効く)
			ImGui::SliderFloat("Look noise",  &m_camLookNoise,   0.0f, 1.0f,  "%.2f");	// 注視点への伝達
		}

		// --- Flip: 翻面の手感と運鏡 ---
		if (ImGui::CollapsingHeader("Flip"))
		{
			ImGui::TextDisabled("-- Turning feel --");
			ImGui::SliderFloat("Flip sens",      &m_flipSens,     0.0001f, 0.003f, "%.4f");	// マウス→手の狙い(小=大きく振る)
			ImGui::SliderFloat("Flip max speed", &m_flipMaxSpeed, 0.3f, 6.0f, "%.2f rad/s");	// 刃の最大回転速度(小=重い)
			ImGui::TextDisabled("-- Camera choreography --");
			ImGui::SliderFloat("Tongs lean",  &m_tongsLean,  0.0f, 0.6f, "%.2f");	// 火钳へ体を寄せる割合
			ImGui::SliderFloat("Grip dolly",  &m_gripDolly,  0.0f, 0.7f, "%.2f");	// 夹む時に刃へ寄る割合
			ImGui::SliderFloat("Grip return", &m_gripLambda, 1.0f, 15.0f, "%.1f");	// 元の視点へ戻る速さ
			ImGui::TextDisabled("-- Hammer set down --");
			ImGui::SliderFloat3("Stow offset", m_hammerStowOff, -1.5f, 1.5f, "%.2f");	// 置いた位置(構えからのずれ)
			ImGui::SliderFloat("Stow tilt",   &m_hammerStowTilt, -3.1416f, 3.1416f, "%.2f");	// 寝かせる角度
		}

		// --- Hammer: 鎚モデルの姿勢と反冲 ---
		if (ImGui::CollapsingHeader("Hammer"))
		{
			ImGui::TextDisabled("-- Position / Rotation / Scale --");
			ImGui::SliderFloat("Rest lift",   &m_hammer.restLift,   0.0f, 1.2f, "%.3f");	// 待機の高さ(下げる=低く構える)
			ImGui::SliderFloat("Scale##hammer", &m_hammerScale,     0.005f, 0.06f, "%.4f");	// ##hammer=同窓の"Scale"衝突回避
			ImGui::SliderFloat3("Rot(rad)",   m_hammerRot,          -3.1416f, 3.1416f, "%.3f");
			ImGui::SliderFloat3("Offset",     m_hammerOff,          -0.5f, 0.5f, "%.3f");	// Y=高さ微調整
			ImGui::TextDisabled("-- Spring-damper (recoil physics) --");
			ImGui::SliderFloat("Stiffness k", &m_hammer.stiffness,  20.0f, 600.0f, "%.0f");	// 刚度=硬さ/速さ
			ImGui::SliderFloat("Damping c",   &m_hammer.damping,    0.0f, 40.0f, "%.2f");	// 阻尼=収まり(小=よく跳ねる)
			ImGui::SliderFloat("Mass m",      &m_hammer.mass,       0.2f, 4.0f, "%.2f");	// 質量=重さ/鈍さ
			ImGui::SliderFloat("Impulse J",   &m_hammer.impulse,    0.0f, 8.0f, "%.2f");	// 打撃の上向き冲量(初速=J/m)
			ImGui::SliderFloat("Recoil back", &HAMMER_RECOIL_BACK,  0.0f, 1.0f, "%.3f");	// 手前へ後退(見た目)
			ImGui::SliderFloat("Recoil tilt", &HAMMER_RECOIL_TILT,  0.0f, 2.0f, "%.3f");	// 錘頭の上翻り(見た目)
			ImGui::SliderFloat("Charge raise",&m_hammer.chargeRaise,0.0f, 1.5f, "%.3f");	// 蓄力で上がる量
		}

		// --- Aim & Feel: 照準の重さ / 鎚が照準へ追いつく速さ ---
		if (ImGui::CollapsingHeader("Aim & Feel"))
		{
			ImGui::SliderFloat("Aim sens",     &m_aimSens,      0.0006f, 0.0050f, "%.4f");	// 低=重い
			ImGui::SliderFloat("Hammer follow",&m_hammerFollow, 3.0f, 24.0f, "%.1f");		// 低=遅れて重い
		}

		// --- Walk / Player: 一人称の走動 ---
		if (ImGui::CollapsingHeader("Walk / Player"))
		{
			ImGui::Text(m_walkMode ? "mode: WALK (press E to enter station)"
			                       : "mode: STATION (forging)");
			ImGui::SliderFloat("Walk speed",  &m_walkSpeed,    0.5f,  8.0f,   "%.2f");	// 移動速度(単位/秒)
			ImGui::SliderFloat("Mouse sens",  &m_walkSens,     0.0006f, 0.0050f, "%.4f");	// 視角感度(低=重い)
			ImGui::SliderFloat("Pitch limit", &m_walkPitchLim, 0.3f,  1.55f,  "%.2f");	// 上下の振り切り制限(rad)
			ImGui::SliderFloat("Eye height",  &m_walkEyeH,     0.8f,  2.2f,   "%.2f");	// 目線の高さ
		}

		ImGui::End();
	}

	m_fade.Draw();	// 最後に全画面の黒幕(前景層)を重ねる=遷移の淡入淡出
}


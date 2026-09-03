// Part of SceneForge, split out of the original single SceneForge.cpp.
// This file carries a UTF-8 BOM so the Japanese comments moved from the
// original source keep compiling correctly under MSVC (no C2601/C1075).
// All SceneForge members share the class declaration in SceneForge.h and the
// file-local helpers declared in SceneForge_Internal.h.
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

// 画面中央にウィンドウ枠なしのメッセージを出す小道具
static void CenterText(const char* text, float yRatio, float scale = 1.0f,
                       ImU32 col = IM_COL32(255, 255, 255, 255))
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;	// 実際の画面サイズ(解像度非依存)
	ImVec2 sz = ImGui::CalcTextSize(text);
	sz.x *= scale; sz.y *= scale;
	float x = (disp.x - sz.x) * 0.5f;
	float y =  disp.y * yRatio - sz.y * 0.5f;
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(x, y), col, text);
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
	CenterText("- FORGE -",            0.34f, 3.0f, IM_COL32(255, 200, 120, 255));
	CenterText("Timing Blacksmith",    0.44f, 1.4f, IM_COL32(255, 235, 210, 255));
	CenterText("PRESS  SPACE  TO  START", 0.62f, 1.6f, IM_COL32(255, 255, 255, 255));
}

void SceneForge::DrawPlayUI()
{
	// 鉄条とハンマーは3Dで描画するので、2Dの鉄条(DrawBillet/DrawHammer)は使わない

	// 温度ゲージ
	DrawHeatGauge();

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

	// 廃件率(左上, SHAPE MATCHの下)。過熱/冷打/完成済みの段を叩くと溜まり、満ちると失敗。減らない。
	{
		float pct = m_spoil; if (pct > 1.0f) pct = 1.0f;
		sprintf_s(sb, sizeof(sb), "SPOIL  %d%%", (int)(pct * 100));
		dl->AddText(ImVec2(40, 80), IM_COL32(255, 150, 120, 255), sb);
		float bx0 = 130.0f, bx1 = 300.0f, by = 84.0f, bh = 10.0f;
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(30, 30, 34, 220), 2.0f);
		ImU32 sc = (pct > 0.6f) ? IM_COL32(255, 70, 50, 255) : IM_COL32(230, 140, 60, 255);
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * pct, by + bh), sc, 2.0f);
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

void SceneForge::DrawResultUI()
{
	CenterText("FORGED!",              0.30f, 3.0f, IM_COL32(255, 220, 140, 255));
	char buf[64];
	sprintf_s(buf, sizeof(buf), "SHAPE MATCH  %d%%", (int)(m_match * 100));
	CenterText(buf,                    0.46f, 2.0f, IM_COL32(150, 220, 255, 255));
	sprintf_s(buf, sizeof(buf), "SCORE  %d", m_score);
	CenterText(buf,                    0.56f, 2.0f, IM_COL32(255, 255, 255, 255));
	CenterText("PRESS  SPACE  TO  RETURN", 0.70f, 1.4f, IM_COL32(255, 255, 255, 220));
}

//--- 廃件(失敗)画面: 鋼を叩き損じて台無しにした。分数は出すが低評価。
void SceneForge::DrawGameOverUI()
{
	float p = 0.5f + 0.5f * sinf(m_time * 6.0f);
	CenterText("RUINED",               0.28f, 3.2f, IM_COL32(255, 80, 60, (int)(180 + p * 75)));
	CenterText("You spoiled the steel", 0.44f, 1.6f, IM_COL32(255, 170, 150, 255));
	char buf[64];
	sprintf_s(buf, sizeof(buf), "SCORE  %d", m_score);
	CenterText(buf,                    0.56f, 2.0f, IM_COL32(255, 255, 255, 255));
	CenterText("PRESS  SPACE  TO  RETRY", 0.70f, 1.4f, IM_COL32(255, 255, 255, 220));
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
	case GAME_OVER:   DrawGameOverUI(); break;
	}

	// F1中: 調整パネルは「職責ごとに1窓」で分ける。
	//   Weapon = 工件モデルの姿勢 / Camera = 視点と追従 / Hammer = 鎚の姿勢と反冲 / Aim = 照準の手触り。
	//   合った値は SceneForge.h の初期値へ焼き込む。ここで混ぜない(相機は相機窓だけ、鎚は鎚窓だけ)。
	if (DebugUI::IsVisible())
	{
		// --- Weapon: 工件モデルを砧面に合わせる(FBXが読めた時だけ) ---
		if (m_wpOk)
		{
			ImGui::Begin("Weapon (F1)");
			ImGui::Text("stages loaded: %d,  verts: %d", (int)m_wpStage.size(), m_wpN);
			ImGui::SliderFloat("Forge progress", &m_forgeProg, 0.0f, 1.0f, "%.2f");
			ImGui::Separator();
			ImGui::TextDisabled("-- Orientation --");
			ImGui::SliderFloat("Yaw",   &m_wpYaw,   -3.1416f, 3.1416f, "%.3f");
			ImGui::SliderFloat("Pitch", &m_wpPitch, -3.1416f, 3.1416f, "%.3f");
			ImGui::SliderFloat("Roll",  &m_wpRoll,  -3.1416f, 3.1416f, "%.3f");
			ImGui::Separator();
			ImGui::TextDisabled("-- Scale / Position --");
			ImGui::SliderFloat("Scale", &m_wpScale, 0.2f, 3.0f, "%.2f");
			ImGui::SliderFloat("Off X", &m_wpOff[0], -1.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Off Y", &m_wpOff[1], -1.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Off Z", &m_wpOff[2], -1.0f, 1.0f, "%.3f");
			ImGui::End();
		}

		// --- Camera: 視点・画角・追従・打撃の揺れ(相機に属するものは全部ここ) ---
		ImGui::Begin("Camera (F1)");
		ImGui::TextDisabled("-- View (3/4 forge) --");
		ImGui::SliderFloat3("Cam Pos",  m_camPos,  -5.0f, 6.0f, "%.2f");
		ImGui::SliderFloat3("Cam Look", m_camLook, -5.0f, 6.0f, "%.2f");
		{
			float fovDeg = m_camFov * 57.29578f;			// rad→deg で見せる
			if (ImGui::SliderFloat("Cam FOV", &fovDeg, 25.0f, 70.0f, "%.0f deg"))
				m_camFov = fovDeg * 0.01745329f;			// deg→rad へ戻す
		}
		ImGui::Separator();
		ImGui::TextDisabled("-- Follow (aim along blade) --");
		ImGui::SliderFloat("Rail (aim)",  &m_aimRail,     0.0f, 1.0f, "%.2f");	// 手前0..奥1(手動確認用)
		ImGui::SliderFloat("Follow Z",    &m_camFollowZ,  0.0f, 1.0f, "%.2f");	// カメラ本体のZ追従割合
		ImGui::SliderFloat("Pan gain",    &m_camPanGain,  0.0f, 2.0f, "%.2f");	// 追従量の倍率
		ImGui::SliderFloat("Cam lerp",    &m_camLerpRate, 0.5f, 12.0f, "%.1f");	// 3段切替の速さ(小=重い)
		ImGui::Separator();
		ImGui::TextDisabled("-- Impact shake --");
		ImGui::SliderFloat("Cam shake",   &CAM_SHAKE_AMP,  0.0f, 0.2f, "%.3f");	// 打撃のカメラ揺れ
		ImGui::End();

		// --- Hammer: 鎚モデルの姿勢と反冲だけ(相機・照準はここに置かない) ---
		ImGui::Begin("Hammer (F1)");
		ImGui::TextDisabled("-- Position / Rotation / Scale --");
		ImGui::SliderFloat("Rest lift",   &HAMMER_REST_LIFT,    0.0f, 1.2f, "%.3f");	// 待機の高さ(下げる=低く構える)
		ImGui::SliderFloat("Scale",       &m_hammerScale,       0.005f, 0.06f, "%.4f");
		ImGui::SliderFloat3("Rot(rad)",   m_hammerRot,          -3.1416f, 3.1416f, "%.3f");
		ImGui::SliderFloat3("Offset",     m_hammerOff,          -0.5f, 0.5f, "%.3f");	// Y=高さ微調整
		ImGui::Separator();
		ImGui::TextDisabled("-- Recoil (kickback) --");
		ImGui::SliderFloat("Anim time",   &STRIKE_ANIM_TIME,    0.10f, 0.80f, "%.3f s");
		ImGui::SliderFloat("Recoil up",   &HAMMER_RECOIL_AMP,   0.0f, 3.0f, "%.2f");	// 縦の跳ね
		ImGui::SliderFloat("Recoil back", &HAMMER_RECOIL_BACK,  0.0f, 1.0f, "%.3f");	// 手前へ後退
		ImGui::SliderFloat("Recoil tilt", &HAMMER_RECOIL_TILT,  0.0f, 2.0f, "%.3f");	// 錘頭の上翻り
		ImGui::SliderFloat("Charge raise",&HAMMER_CHARGE_RAISE, 0.0f, 1.5f, "%.3f");	// 蓄力で上がる量
		ImGui::End();

		// --- Aim & Feel: 照準の重さ / 鎚が照準へ追いつく速さ ---
		ImGui::Begin("Aim & Feel (F1)");
		ImGui::SliderFloat("Aim sens",     &m_aimSens,      0.0006f, 0.0050f, "%.4f");	// 低=重い
		ImGui::SliderFloat("Hammer follow",&m_hammerFollow, 3.0f, 24.0f, "%.1f");		// 低=遅れて重い
		ImGui::Separator();
		// 手動保存。退出時にも自動保存されるが、確認したい時のボタン(全窓の値をまとめて保存)。
		if (ImGui::Button("Save tuning (forge_tuning.txt)")) SaveTuning();
		ImGui::End();
	}

	m_fade.Draw();	// 最後に全画面の黒幕(前景層)を重ねる=遷移の淡入淡出
}


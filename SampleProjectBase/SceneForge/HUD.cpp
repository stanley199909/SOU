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
#include "TextureCache.h"
#include "CameraBase.h"
#include "LightBase.h"
#include "Model.h"
#include "Geometory.h"
#include "Input.h"
#include "DebugUI.h"
#include "Lerp.h"
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
//   shadow=false: 紙の上にインクで書く文字用(黒い影は紙の上では文字を濁らせる)。
static void CenterText(const char* text, float yRatio, float scale = 1.0f,
                       ImU32 col = IM_COL32(255, 255, 255, 255), ImFont* font = nullptr, bool shadow = true)
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;	// 実際の画面サイズ(解像度非依存)
	ImFont* f = font ? font : ImGui::GetFont();
	float px = f->FontSize * scale;
	if (f == DebugUI::FontJP()) f = DebugUI::FontJPFor(px);	// 日本語は描く大きさに一番近い実寸で焼いた物へ(縮小ぼけ防止)
	ImVec2 sz = f->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
	float x = (disp.x - sz.x) * 0.5f;
	float y =  disp.y * yRatio - sz.y * 0.5f;
	ImU32 shadowCol = IM_COL32(0, 0, 0, (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * 0.6f));
	if (shadow) dl->AddText(f, px, ImVec2(x + 2.0f, y + 2.0f), shadowCol, text);	// 影
	dl->AddText(f, px, ImVec2(x, y), col, text);									// 本体
}

//--- 2色を t(0..1) で線形補間(ImU32 の RGBA 各成分ごと)。
static ImU32 LerpColor(ImU32 a, ImU32 b, float t)
{
	ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
	return ImGui::ColorConvertFloat4ToU32(ImVec4(Lerp::Linear(ca.x, cb.x, t), Lerp::Linear(ca.y, cb.y, t),
	                                             Lerp::Linear(ca.z, cb.z, t), Lerp::Linear(ca.w, cb.w, t)));
}

//--- ナインスライス(9-slice scaling)で画像を矩形に貼る。
//    画像を縦横3x3に切り、四隅は「同じ倍率」のまま、辺は片方向だけ、中央は両方向に伸ばす。
//    → 飾り枠の四隅の模様が、縦長/横長どちらのパネルでも歪まない(UI の定番手法)。
//    さらに上辺/下辺の真ん中に飾りがある画像向けに、辺の中央 srcCenter(px) だけは伸ばさず隅と同じ倍率で描き、
//    その左右の線だけを伸ばす(=上辺中央の飾りも潰れない。0 なら普通の9分割)。
//    srcBorder = 元画像で「隅」とみなす大きさ(px) / scale = 隅を画面に描く倍率。
static void DrawNineSlice(ImDrawList* dl, Texture* tex, ImVec2 a, ImVec2 b, float srcBorder, float scale,
                          ImU32 tint, float srcCenter = 0.0f)
{
	if (!tex || !tex->GetResource()) return;
	const float tw = (float)tex->GetWidth(), th = (float)tex->GetHeight();
	const float d  = srcBorder * scale;					// 画面上の隅の大きさ
	const float xs[4] = { a.x, a.x + d, b.x - d, b.x };	// 画面の切れ目
	const float ys[4] = { a.y, a.y + d, b.y - d, b.y };
	const float us[4] = { 0.0f, srcBorder / tw, 1.0f - srcBorder / tw, 1.0f };	// 画像(UV)の切れ目
	const float vs[4] = { 0.0f, srcBorder / th, 1.0f - srcBorder / th, 1.0f };
	ImTextureID id = (ImTextureID)tex->GetResource();
	for (int j = 0; j < 3; ++j)
		for (int i = 0; i < 3; ++i)
		{
			const bool edgeMid = (i == 1 && j != 1);	// 上辺/下辺の真ん中
			const float cw = srcCenter * scale;			// 画面上の「伸ばさない中央」の幅
			if (!edgeMid || srcCenter <= 0.0f || cw >= xs[2] - xs[1])
			{
				dl->AddImage(id, ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]),
				             ImVec2(us[i], vs[j]), ImVec2(us[i + 1], vs[j + 1]), tint);
				continue;
			}
			// 左の線(伸ばす) | 中央の飾り(そのままの倍率) | 右の線(伸ばす)
			const float mx = (xs[1] + xs[2]) * 0.5f, mu = 0.5f, cu = srcCenter / tw * 0.5f;
			const float px[4] = { xs[1], mx - cw * 0.5f, mx + cw * 0.5f, xs[2] };
			const float pu[4] = { us[1], mu - cu,        mu + cu,        us[2] };
			for (int k = 0; k < 3; ++k)
				dl->AddImage(id, ImVec2(px[k], ys[j]), ImVec2(px[k + 1], ys[j + 1]),
				             ImVec2(pu[k], vs[j]), ImVec2(pu[k + 1], vs[j + 1]), tint);
		}
}

//--- 羊皮紙のパネル(工程リストと一時停止メニューで共用)。parchment_frame.jpg(ChatGPT 生成版)の寸法は「画像に対する比」で持つ
//    =保存解像度が変わっても合う。cornerPx = 画面上の隅の大きさ(px)。
static const float PARCHMENT_CORNER_SRC_RATIO = 0.27f;	// 四隅の飾りが収まる大きさ(画像の高さ比)
static const float PARCHMENT_CENTER_SRC_RATIO = 0.17f;	// 上辺中央の飾りの幅(画像の幅比)。ここは伸ばさない
static const ImU32 PARCHMENT_TINT             = IM_COL32(255, 255, 255, 235);	// 背景がほんの少し透ける
static void DrawParchment(ImDrawList* dl, Texture* frame, ImVec2 a, ImVec2 b, float cornerPx)
{
	if (!frame) return;
	const float srcCorner = frame->GetHeight() * PARCHMENT_CORNER_SRC_RATIO;
	const float srcCenter = frame->GetWidth()  * PARCHMENT_CENTER_SRC_RATIO;
	DrawNineSlice(dl, frame, a, b, srcCorner, cornerPx / srcCorner, PARCHMENT_TINT, srcCenter);
}

//--- 動くマウスの案内(動画型の操作案内)。静止したアイコンより「何をすれば良いか」が一目で分かる(動きそのものを見せる)。
//    マウスの絵 = Kenney の mouse.png。矢印は三角形を描く(光り方を動きの向きに合わせて変える為)。
//    axis = 上下 / 左右。towards = 0: 往復(両方の矢印が動きに合わせて光る) / -1: 上(左)へだけ / +1: 下(右)へだけ。
//    片方向の時は「中央からその向きへ動いて消え、また中央から」を繰り返し、その向きの矢印だけを明るくする。
void SceneForge::DrawMousePrompt(MouseAxis axis, int towards, float alpha)
{
	const float VISIBLE_EPS = 0.01f;
	if (alpha < VISIBLE_EPS) return;

	const std::string ICON = "Assets/UI/Keyboard & Mouse/Default/mouse.png";
	const float CENTER_Y_RATIO = 0.60f;		// 画面の縦位置(刃の少し下)
	const float ICON_RATIO     = 0.085f;	// マウスの絵の大きさ(画面高さ比)
	const float SWING_RATIO    = 0.35f;		// 動く幅(絵の大きさに対する比。片側)
	const float SWING_FREQ     = 1.2f;		// 1秒に何往復(実際に動かして欲しい速さの目安)
	const float ARROW_GAP      = 0.95f;		// 中心から矢印までの距離(絵の大きさに対する比)
	const float ARROW_HALF_W   = 0.22f;		// 矢印の三角形の半幅(〃)
	const float ARROW_H        = 0.18f;		// 矢印の高さ(〃)
	const float ARROW_DIM      = 0.30f;		// 動いていない向きの矢印の明るさ(0..1)
	const float ONE_WAY_FADE   = 0.75f;		// 片方向: 動きの終わりのこの割合から淡出して中央へ戻る
	const float SHADOW_PX      = 2.0f;

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float icon = disp.y * ICON_RATIO;
	const float cx = disp.x * 0.5f, cy = disp.y * CENTER_Y_RATIO;
	const ImVec2 axisDir = (axis == MouseAxis::Vertical) ? ImVec2(0.0f, 1.0f) : ImVec2(1.0f, 0.0f);	// +側 = 下 / 右

	// 動き: offset = 軸方向のずれ(+ = 下/右)。moving = 動いている向き(+ / −)の強さ
	float offset, movePlus, iconAlpha = 1.0f;
	if (towards == 0)
	{
		const float phase = m_time * SWING_FREQ * XM_2PI;
		offset   = sinf(phase) * icon * SWING_RATIO;
		movePlus = cosf(phase);
	}
	else
	{
		const float t = m_time * SWING_FREQ - floorf(m_time * SWING_FREQ);	// 0..1 を繰り返す
		offset   = (float)towards * t * icon * SWING_RATIO * 2.0f;
		movePlus = (float)towards;
		if (t > ONE_WAY_FADE) iconAlpha = 1.0f - (t - ONE_WAY_FADE) / (1.0f - ONE_WAY_FADE);
	}
	const int a = (int)(255 * alpha);
	const ImVec2 c(cx + axisDir.x * offset, cy + axisDir.y * offset);

	// マウスの絵(影 → 本体)
	if (std::shared_ptr<Texture> tex = TextureCache::Get(ICON.c_str()); tex && tex->GetResource())
	{
		ImTextureID id = (ImTextureID)tex->GetResource();
		const int ia = (int)(a * iconAlpha);
		ImVec2 p0(c.x - icon * 0.5f, c.y - icon * 0.5f), p1(c.x + icon * 0.5f, c.y + icon * 0.5f);
		dl->AddImage(id, ImVec2(p0.x + SHADOW_PX, p0.y + SHADOW_PX), ImVec2(p1.x + SHADOW_PX, p1.y + SHADOW_PX),
		             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, (int)(ia * 0.7f)));
		dl->AddImage(id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, ia));
	}

	// 矢印: 今動いている向きの矢印が明るく、反対は暗い(軸に沿って ± 両側)
	auto arrow = [&](float dir, float bright01)	// dir = −1(上/左) / +1(下/右)
	{
		const ImVec2 n(axisDir.x * dir, axisDir.y * dir);		// 矢印の向き
		const ImVec2 perp(n.y, -n.x);							// 矢印の幅の向き
		const float hw = icon * ARROW_HALF_W;
		const ImVec2 base(cx + n.x * icon * ARROW_GAP, cy + n.y * icon * ARROW_GAP);
		const ImVec2 t(base.x + n.x * icon * ARROW_H, base.y + n.y * icon * ARROW_H);
		const ImVec2 l(base.x - perp.x * hw, base.y - perp.y * hw), r(base.x + perp.x * hw, base.y + perp.y * hw);
		const int aa = (int)(a * (ARROW_DIM + (1.0f - ARROW_DIM) * bright01));
		dl->AddTriangleFilled(ImVec2(t.x + SHADOW_PX, t.y + SHADOW_PX), ImVec2(l.x + SHADOW_PX, l.y + SHADOW_PX),
		                      ImVec2(r.x + SHADOW_PX, r.y + SHADOW_PX), IM_COL32(0, 0, 0, (int)(aa * 0.7f)));
		dl->AddTriangleFilled(t, l, r, IM_COL32(245, 235, 215, aa));
	};
	arrow(-1.0f, fmaxf(-movePlus, 0.0f));
	arrow(+1.0f, fmaxf( movePlus, 0.0f));
}

//--- 淬火の「揺する」: 膜を破るまで(=操作を覚えるまで)上下の往復を見せる。
void SceneForge::DrawStirPrompt()
{
	const float FADE_LAMBDA = 6.0f;		// 出る/消える速さ(Damp率, 1/秒)
	const bool  show = QuenchStirring() && m_boilStage == BoilStage::Film;
	m_stirPromptAlpha = Lerp::Damp(m_stirPromptAlpha, show ? 1.0f : 0.0f, FADE_LAMBDA, ImGui::GetIO().DeltaTime);
	DrawMousePrompt(MouseAxis::Vertical, 0, m_stirPromptAlpha);
}

//--- 研ぎ: 同じ誤りを m_hintAfterMistakes 回くり返した時だけ、直し方を動くマウスで見せる(自適応の案内)。
//    寝かせすぎ → 上へ(立てる) / 立てすぎ → 下へ(寝かせる) / 研ぎ上がった所を研ぐ → 左右へ(別の所へ滑らせる)。
void SceneForge::DrawGrindHint()
{
	const float FADE_LAMBDA = 6.0f;
	const bool show = (m_grindHint != GrindHint::None) && AtStation(Station::Grindstone);
	m_grindHintAlpha = Lerp::Damp(m_grindHintAlpha, show ? 1.0f : 0.0f, FADE_LAMBDA, ImGui::GetIO().DeltaTime);
	switch (m_grindHintShown)	// 淡出中も直前の案内のまま描く
	{
	case GrindHint::TiltUp:   DrawMousePrompt(MouseAxis::Vertical,   -1, m_grindHintAlpha); break;
	case GrindHint::TiltDown: DrawMousePrompt(MouseAxis::Vertical,   +1, m_grindHintAlpha); break;
	case GrindHint::Slide:    DrawMousePrompt(MouseAxis::Horizontal,  0, m_grindHintAlpha); break;
	default: break;
	}
	if (m_grindHint != GrindHint::None) m_grindHintShown = m_grindHint;
}

//--- 操作説明を1行に並べる: [アイコン…] 一言 　[アイコン…] 一言 …(画面中央揃え)。
//    アイコンは Kenney Input Prompts(CC0)の白い画像。TextureCache で一度だけ読み、以後は使い回す。
//    明るい炉の前でも沈まない様に、黒く染めた同じ画像を少しずらして下に敷く(影)。
void SceneForge::DrawKeyHints(const KeyHint* hints, int count, float yRatio, float alpha)
{
	// 64px 版。画面上は約45px(1080p)=ほぼ等倍。128px 版だと約2.8倍の縮小になり、ミップマップの平均でぼやけた。
	const std::string ICON_DIR = "Assets/UI/Keyboard & Mouse/Default/";
	const float ICON_RATIO      = 0.042f;	// アイコンの大きさ(画面高さ比)
	const float ICON_GAP_RATIO  = 0.002f;	// 1つの説明の中のアイコン同士の間
	const float LABEL_GAP_RATIO = 0.004f;	// アイコンと一言の間
	const float HINT_GAP_RATIO  = 0.030f;	// 説明と説明の間
	const float TEXT_RATIO      = 0.024f;	// 一言の文字の高さ
	const float SHADOW_PX       = 2.0f;		// 影のずれ(px)
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float icon = disp.y * ICON_RATIO, px = disp.y * TEXT_RATIO;
	ImFont* jp = DebugUI::FontJPFor(px);	// 描く大きさに一番近い実寸のフォント
	const int   a = (int)(255 * alpha);

	// 1) 全体の幅を測って中央揃えの開始位置を決める
	float total = 0.0f;
	for (int h = 0; h < count; ++h)
	{
		int ni = 0;
		while (ni < KeyHint::MAX_ICONS && hints[h].icons[ni]) ++ni;
		total += icon * ni + disp.y * ICON_GAP_RATIO * (ni - 1) + disp.y * LABEL_GAP_RATIO;
		total += jp->CalcTextSizeA(px, FLT_MAX, 0.0f, hints[h].label).x;
		if (h + 1 < count) total += disp.y * HINT_GAP_RATIO;
	}
	float x = (disp.x - total) * 0.5f;
	const float cy = disp.y * yRatio;

	// 2) 描く
	for (int h = 0; h < count; ++h)
	{
		for (int k = 0; k < KeyHint::MAX_ICONS && hints[h].icons[k]; ++k)
		{
			if (k > 0) x += disp.y * ICON_GAP_RATIO;
			std::shared_ptr<Texture> tex = TextureCache::Get((ICON_DIR + hints[h].icons[k] + ".png").c_str());
			if (tex && tex->GetResource())
			{
				ImTextureID id = (ImTextureID)tex->GetResource();
				ImVec2 p0(x, cy - icon * 0.5f), p1(x + icon, cy + icon * 0.5f);
				dl->AddImage(id, ImVec2(p0.x + SHADOW_PX, p0.y + SHADOW_PX), ImVec2(p1.x + SHADOW_PX, p1.y + SHADOW_PX),
				             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, (int)(a * 0.7f)));	// 影
				dl->AddImage(id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a));
			}
			x += icon;
		}
		x += disp.y * LABEL_GAP_RATIO;
		ImVec2 ts = jp->CalcTextSizeA(px, FLT_MAX, 0.0f, hints[h].label);
		ImVec2 tp(x, cy - ts.y * 0.5f);
		dl->AddText(jp, px, ImVec2(tp.x + SHADOW_PX, tp.y + SHADOW_PX), IM_COL32(0, 0, 0, (int)(a * 0.7f)), hints[h].label);
		dl->AddText(jp, px, tp, IM_COL32(245, 235, 215, a), hints[h].label);
		x += ts.x + disp.y * HINT_GAP_RATIO;
	}
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

//--- 温度ゲージ(HUD)。
//    見た目は画像(ChatGPT 製, Assets/UI/Heat_Gauge/): 金属の外框 / 指針 / 適温・過熱の紋理 / 火花の記号。
//    「どこからどこまでが適温か」は画像に描かず、温度の定数(IDEAL_MIN 等)から程序で決めて紋理をその範囲だけ貼る
//    (UV で切り出す)。=温度を調整しても絵が合わなくならない。画像が無ければ従来の矩形の描画に戻る。
//    画像はどれも周りに大きな透明の余白があるので、中身の位置(px)を測って定数にした(元画像の寸法に対する比で使う)。
void SceneForge::DrawHeatGauge()
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	const float x0 = disp.x * 0.25f;
	const float x1 = disp.x * 0.75f;
	const float y  = disp.y * 0.80f;
	const float hgt = 20.0f;
	auto lerpX = [&](float t) { return x0 + (x1 - x0) * t; };

	Texture* frame   = m_gaugeFrame.get();
	Texture* overlay = m_gaugeOverlay.get();
	Texture* marker  = m_gaugeMarker.get();
	if (frame && frame->GetResource() && overlay && overlay->GetResource() && marker && marker->GetResource())
	{
		// 3枚を重ねて描く(レイヤー。下から):
		//   ① 外框(heat_gauge_frame.png)      … 槽の暗い地を含めた全体
		//   ② 適温/過熱の紋理                 … 槽の範囲に、温度の定数で決めた区間だけ
		//   ③ 槽をくり抜いた外框(…_overlay.png) … 金の縁が②の端を覆う=槽の丸い両端にもぴったり収まる
		//   ③ はツールで作った: 槽の中心から各行を左右へ走査し、金の縁で止めた範囲を透明にした(行ごとの外れ値は中央値で除去)。
		// --- 外框の寸法(1586x992 で測定。①③は同じ寸法・同じ位置) ---
		const float F_W = 1586.0f, F_H = 992.0f;			// 測った時の画像の寸法(実際の寸法との比で換算する)
		const float F_LEFT = 27.0f,  F_RIGHT = 1558.0f;	// 不透明な部分の左右端
		const float F_TOP  = 435.0f, F_BOTTOM = 559.0f;	// 不透明な部分の上下端
		const float T_LEFT = 125.0f, T_RIGHT = 1458.0f;	// くり抜いた槽の左右端(=温度 0 と 1 の位置)
		const float T_TOP  = 462.0f, T_BOTTOM = 532.0f;	// 同 上下端
		const float CAP_L  = 190.0f, CAP_R = 1395.0f;	// 端の飾り(鋲+槽の丸い端)の内側の境=ここから内は直線なので横に伸ばしてよい
		// --- 画面での大きさ(画面比) ---
		const float TRACK_W_RATIO = 0.46f;		// 槽の幅(画面幅比)
		const float TRACK_H_RATIO = 0.026f;		// 槽の高さ(画面高さ比)。外框の倍率はこれで決まる(縦横同倍率=鋲が歪まない)
		const float TRACK_Y_RATIO = 0.80f;		// 槽の上端(画面高さ比)
		const float MARKER_H_RATIO = 0.040f;	// 指針の高さ(画面高さ比)。heat_gauge_marker_ui.png はこの2倍(1080p)で縮小済み
		const float MARKER_DIP     = 0.35f;		// 指針の先を槽へ食い込ませる量(槽の高さに対する比)
		const ImU32 TINT = IM_COL32(255, 255, 255, 255);

		const float kx = frame->GetWidth() / F_W, ky = frame->GetHeight() / F_H;	// 書き出し解像度が変わった時の換算
		const float tw = disp.x * TRACK_W_RATIO, th = disp.y * TRACK_H_RATIO;
		const float tx0 = (disp.x - tw) * 0.5f, tx1 = tx0 + tw, ty0 = disp.y * TRACK_Y_RATIO, ty1 = ty0 + th;
		const float s = th / ((T_BOTTOM - T_TOP) * ky);	// 画像1px → 画面px(縦で決め、端の飾りにも同じ倍率)
		auto U = [&](float px) { return px * kx / frame->GetWidth(); };
		auto V = [&](float py) { return py * ky / frame->GetHeight(); };
		const float fy0 = ty0 - (T_TOP - F_TOP) * ky * s, fy1 = ty1 + (F_BOTTOM - T_BOTTOM) * ky * s;
		// 横は3分割(左の飾り / 伸ばす中央 / 右の飾り)。飾りは同倍率、中央だけ槽の幅に合わせて伸ばす
		const float lx0 = tx0 - (T_LEFT - F_LEFT) * kx * s, lx1 = tx0 + (CAP_L - T_LEFT) * kx * s;
		const float rx0 = tx1 - (T_RIGHT - CAP_R) * kx * s, rx1 = tx1 + (F_RIGHT - T_RIGHT) * kx * s;
		auto DrawFrame = [&](Texture* t)	// ① と ③ は同じ切り方で描く
		{
			ImTextureID id = (ImTextureID)t->GetResource();
			dl->AddImage(id, ImVec2(lx0, fy0), ImVec2(lx1, fy1), ImVec2(U(F_LEFT), V(F_TOP)), ImVec2(U(CAP_L),   V(F_BOTTOM)), TINT);
			dl->AddImage(id, ImVec2(lx1, fy0), ImVec2(rx0, fy1), ImVec2(U(CAP_L),  V(F_TOP)), ImVec2(U(CAP_R),   V(F_BOTTOM)), TINT);
			dl->AddImage(id, ImVec2(rx0, fy0), ImVec2(rx1, fy1), ImVec2(U(CAP_R),  V(F_TOP)), ImVec2(U(F_RIGHT), V(F_BOTTOM)), TINT);
		};

		DrawFrame(frame);	// ①

		// ② 温度の範囲: 紋理の「その範囲に当たる部分」だけを槽に貼る(UV の u = 温度)
		//    紋理は上下に透明の余白がある → 不透明な帯(v の範囲)だけ使う(2172x724 で測定)
		auto heatX = [&](float t) { return tx0 + tw * t; };
		const float ZONE_SRC_H = 724.0f;	// 紋理を測った時の画像の高さ(下の帯の位置はこれに対する px)
		auto DrawZone = [&](Texture* z, float from, float to, float vTop, float vBottom)
		{
			if (!z || !z->GetResource() || to <= from) return;
			dl->AddImage((ImTextureID)z->GetResource(), ImVec2(heatX(from), ty0), ImVec2(heatX(to), ty1),
			             ImVec2(from, vTop / ZONE_SRC_H), ImVec2(to, vBottom / ZONE_SRC_H), TINT);
		};
		const float IDEAL_V_TOP = 243.0f, IDEAL_V_BOTTOM = 481.0f;	// heat_zone_ideal.png の不透明な帯
		const float OVER_V_TOP  = 265.0f, OVER_V_BOTTOM  = 462.0f;	// heat_zone_over.png の不透明な帯
		DrawZone(m_gaugeIdeal.get(), IDEAL_MIN, IDEAL_MAX, IDEAL_V_TOP, IDEAL_V_BOTTOM);
		DrawZone(m_gaugeOver.get(),  OVERHEAT,  1.0f,      OVER_V_TOP,  OVER_V_BOTTOM);

		DrawFrame(overlay);	// ③ 金の縁が②の端を覆う

		// --- 指針: 先端(下の頂点)を今の温度の位置に。槽へ少し食い込ませる ---
		//   heat_gauge_marker_ui.png は中身だけを切り出し、画面での大きさの2倍へ高品質(Lanczos)で縮小済み
		//   (元の 1254px を実行時に約20分の1へ縮めると、ミップマップの平均でぼやけるため)。
		{
			const float mh = disp.y * MARKER_H_RATIO;
			const float mw = mh * marker->GetWidth() / (float)marker->GetHeight();
			const float mx = heatX(m_forging.Heat()), tipY = ty0 + th * MARKER_DIP;
			dl->AddImage((ImTextureID)marker->GetResource(), ImVec2(mx - mw * 0.5f, tipY - mh), ImVec2(mx + mw * 0.5f, tipY),
			             ImVec2(0, 0), ImVec2(1, 1), TINT);
		}
		// チュートリアル: 緑の帯の下に「ここで叩く」(この温度の時だけ有効な打撃)。上は指針が動くので下に置く
		if (TutorialAtAnvil())
		{
			const float LABEL_RATIO = 0.024f, GAP = 0.3f;	// 文字の高さ / 外框の下端から離す量(文字の高さ比)
			const float lpx = disp.y * LABEL_RATIO;
			ImFont* f = DebugUI::FontJPFor(lpx);
			const char* label = (const char*)u8"▲ ここで叩く";
			const ImVec2 sz = f->CalcTextSizeA(lpx, FLT_MAX, 0.0f, label);
			const float cxg = (heatX(IDEAL_MIN) + heatX(IDEAL_MAX)) * 0.5f;
			dl->AddText(f, lpx, ImVec2(cxg - sz.x * 0.5f, fy1 + lpx * GAP), IM_COL32(150, 235, 150, 255), label);
		}
		return;
	}

	// ---- 画像が無い時の従来の描画 ----

	// トラック
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(30, 30, 34, 220), 4.0f);
	// 最適温度帯(緑の帯)
	dl->AddRectFilled(ImVec2(lerpX(IDEAL_MIN), y), ImVec2(lerpX(IDEAL_MAX), y + hgt),
		IM_COL32(60, 160, 70, 150));
	// 過熱帯(赤の帯)
	dl->AddRectFilled(ImVec2(lerpX(OVERHEAT), y), ImVec2(x1, y + hgt),
		IM_COL32(180, 40, 40, 160));
	// 現在温度の塗り
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(lerpX(m_forging.Heat()), y + hgt), HeatColor(m_forging.Heat()), 4.0f);
	// マーカー
	dl->AddLine(ImVec2(lerpX(m_forging.Heat()), y - 5), ImVec2(lerpX(m_forging.Heat()), y + hgt + 5),
		IM_COL32(255, 255, 255, 255), 2.0f);
	// 枠
	dl->AddRect(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(200, 200, 200, 120), 4.0f);

	// KCD式: 温度は緑帯(適温)/赤帯(過熱)の視覚だけで示す。「加熱しろ」等の指示テキストは出さない。
}

//--- ロゴ(ユーザーの絵コンテ通り左上)。alpha=1→0 で淡出(SPACE 後の導入運鏡の間)。
//    位置と大きさは画面比(解像度非依存)。フォントは従来のタイトル用(ユーザーが気に入っている)。
void SceneForge::DrawTitleLogo(float alpha)
{
	if (alpha <= 0.0f) return;
	const float LOGO_X_RATIO   = 0.06f;	// 左端からの位置(画面幅比)
	const float LOGO_Y_RATIO   = 0.08f;	// 上端からの位置(画面高さ比)
	const float LOGO_H_RATIO   = 0.13f;	// 「FORGE」の文字の高さ(画面高さ比)
	const float SUB_H_RATIO    = 0.030f;	// 副題の文字の高さ
	const float SUB_GAP_RATIO  = 0.01f;	// ロゴと副題の間
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImFont* title = DebugUI::FontTitle() ? DebugUI::FontTitle() : ImGui::GetFont();
	ImFont* body  = DebugUI::FontBody()  ? DebugUI::FontBody()  : ImGui::GetFont();
	const int a = (int)(255 * alpha);

	float x = disp.x * LOGO_X_RATIO, y = disp.y * LOGO_Y_RATIO;
	float logoPx = disp.y * LOGO_H_RATIO;
	dl->AddText(title, logoPx, ImVec2(x, y), IM_COL32(255, 196, 110, a), "FORGE");
	float subY = y + logoPx + disp.y * SUB_GAP_RATIO;
	dl->AddText(body, disp.y * SUB_H_RATIO, ImVec2(x, subY), IM_COL32(230, 215, 195, (int)(235 * alpha)),
	            "A  T I M I N G   B L A C K S M I T H");
}

void SceneForge::DrawTitleUI()
{
	ImFont* body = DebugUI::FontBody();
	// SPACE 後はロゴと開始プロンプトを m_logoFadeTime 秒で線形に消す(ユーザー指定: 1秒の lerp)
	float alpha = 1.0f;
	if (m_introPhase == IntroPhase::LogoFade && m_logoFadeTime > 0.0f)
		alpha = Lerp::Linear(1.0f, 0.0f, fminf(m_introTimer / m_logoFadeTime, 1.0f));
	DrawTitleLogo(alpha);
	// 開始プロンプトは緩やかに明滅させて「操作可能」を伝える
	if (m_modeSelectOpen)
	{
		static const char* LABELS[(int)ModeItem::Count] = { (const char*)u8"チュートリアル", (const char*)u8"通常モード" };
		static const char* DESCS[(int)ModeItem::Count]  = { (const char*)u8"はじめての方へ。操作と叩く場所を、画面で詳しく案内します", (const char*)u8"案内は最小限。自分の目と耳で鍛冶を進めます" };
		const float PANEL_CENTER_Y = 0.70f, DESC_Y = 0.90f, DESC_SCALE = 0.8f;
		const int clicked = DrawChoicePanel((const char*)u8"モードを選ぶ", LABELS, (int)ModeItem::Count, m_modeSel, PANEL_CENTER_Y, false);
		if (clicked >= 0) m_modeRequest = clicked;	// 実行は UpdateTitle
		CenterText(DESCS[m_modeSel], DESC_Y, DESC_SCALE, IM_COL32(255, 236, 196, 235), DebugUI::FontJP());
		return;
	}
	float p = (0.6f + 0.4f * sinf(m_time * 3.0f)) * alpha;
	CenterText("PRESS  SPACE  TO  START", 0.86f, 1.15f, IM_COL32(255, 255, 255, (int)(255 * p)), body);
}

//--- 映画的な終幕の黒帯(上下)。淬火後に QuenchStep が m_letterbox を 0→1 へ進める。
void SceneForge::DrawLetterbox()
{
	if (m_letterbox <= 0.0f) return;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;			// 解像度非依存(画面比)
	const float h = disp.y * LETTERBOX_RATIO * m_letterbox;
	dl->AddRectFilled(ImVec2(0, 0),            ImVec2(disp.x, h),      IM_COL32(0, 0, 0, 255));
	dl->AddRectFilled(ImVec2(0, disp.y - h),   ImVec2(disp.x, disp.y), IM_COL32(0, 0, 0, 255));
}

void SceneForge::DrawPlayUI()
{
	// 鉄条とハンマーは3Dで描画するので、2Dの鉄条(DrawBillet/DrawHammer)は使わない

	// 終幕: クリアが確定したら(揺すり始め。黒帯はその少し後から入る) HUD を全部消して、映像だけを見せる。
	//   黒帯の有無でなく「確定したか」で判断する=まだ続くのに UI が消える事は無い。
	if (m_clearDecided) { DrawLetterbox(); return; }

	ImFont* jp = DebugUI::FontJP();	// 指引 UI の日本語(游明朝)

	// 温度ゲージ
	DrawHeatGauge();

	// 工程リスト(左)と、走動中に次に向かう点の目印(3D の上)
	DrawStepTracker();
	DrawObjectiveMarker();
	DrawTutorialPanel();	// チュートリアルだけ: 右の説明パネル(通常モードでは何も描かない)
	if (m_tutorial && AtStation(Station::Grindstone) && !m_clearDecided) DrawGrindAngleMeter();

	// 案内文(宏観チュートリアル=「今何をするか」)。工程と状況で変わる(UpdateGuide)。
	//   基本の文言は配方(GameData/WeaponRecipe)が持つ=換武器で自動的に差し替わる。
	//   文が変わった瞬間は淡入+少し下から浮かせる=「変わった」ことに気付かせる(読み落とし防止)。
	{
		const char* text = (m_guideText && m_guideText[0]) ? m_guideText : CurrentStep().instruction;
		const float FADE_IN_SEC   = 0.6f;	// 淡入にかける秒
		const float RISE_RATIO    = 0.015f;	// 淡入中に浮き上がる量(画面高さ比)
		const float TEXT_Y        = 0.10f;	// 文の縦位置(画面高さ比)
		const float TEXT_SCALE    = 1.0f;	// 游明朝の焼き寸(30px)に対する倍率
		const float BAND_H_RATIO  = 0.075f;	// 文の後ろに敷く暗い帯の高さ(明るい炉の前でも読める様に)
		const float BAND_W_RATIO  = 0.36f;	// 帯の半幅(画面幅比)。両端は透明へ溶かす
		const int   BAND_ALPHA    = 150;
		float t = fminf((m_time - m_guideChangedAt) / FADE_IN_SEC, 1.0f);
		if (t < 0.0f) t = 0.0f;
		ImVec2 disp = ImGui::GetIO().DisplaySize;
		ImDrawList* bg = ImGui::GetForegroundDrawList();
		float cy = disp.y * TEXT_Y, bh = disp.y * BAND_H_RATIO * 0.5f, bw = disp.x * BAND_W_RATIO;
		ImU32 dark  = IM_COL32(0, 0, 0, (int)(BAND_ALPHA * t));
		ImU32 clear = IM_COL32(0, 0, 0, 0);
		bg->AddRectFilledMultiColor(ImVec2(disp.x * 0.5f - bw, cy - bh), ImVec2(disp.x * 0.5f, cy + bh), clear, dark, dark, clear);
		bg->AddRectFilledMultiColor(ImVec2(disp.x * 0.5f, cy - bh), ImVec2(disp.x * 0.5f + bw, cy + bh), dark, clear, clear, dark);
		float y = TEXT_Y + RISE_RATIO * (1.0f - t);
		CenterText(text, y, TEXT_SCALE, IM_COL32(255, 236, 196, (int)(255 * t)), jp);
	}

	// 翻面の子状態ごとの操作ヒント(宏観チュートリアル)。運鏡ビート中は状況説明、
	// 操作待ちの Ready/Flipping では「何のキーで何が起きるか」を明示する。
	const ImU32 BEAT_COL  = IM_COL32(255, 230, 180, 220);	// 運鏡中の状況説明(少し淡く)
	const float FLIP_Y = 0.24f, FLIP_SCALE = 0.85f;
	switch (m_flipPhase)
	{
	case FlipPhase::TongsOut: CenterText((const char*)u8"火ばさみを手に取る…",                          FLIP_Y, FLIP_SCALE, BEAT_COL,  jp); break;
	case FlipPhase::Ready:
	{
		static const KeyHint READY_HINTS[] = {
			{ { "mouse_left" }, (const char*)u8"刃を掴む" },
			{ { "keyboard_f" }, (const char*)u8"火ばさみを戻す" },
		};
		DrawKeyHints(READY_HINTS, _countof(READY_HINTS), FLIP_Y);
		break;
	}
	case FlipPhase::Gripping: CenterText((const char*)u8"刃を掴んでいる…",                                FLIP_Y, FLIP_SCALE, BEAT_COL,  jp); break;
	case FlipPhase::Flipping:
	{
		static const KeyHint FLIP_HINTS[] = {
			{ { "mouse_move" }, (const char*)u8"刃を回す" },
			{ { "mouse_left" }, (const char*)u8"この面に決める" },
		};
		DrawKeyHints(FLIP_HINTS, _countof(FLIP_HINTS), FLIP_Y);
		break;
	}
	case FlipPhase::PutBack:  CenterText((const char*)u8"火ばさみを戻している…",                        FLIP_Y, FLIP_SCALE, BEAT_COL,  jp); break;
	default: break;
	}

	// ※KCD2式: 画面中心の準心は「置かない」。第一人称に固定十字は不自然で、しかも屏幕中央に
	//   死んでいて動かせない。狙いの提示は「動くハンマー＋刃の高亮段」で行う(下の WeaponRender)。

	// 過熱の警告(点滅)
	if (m_forging.Heat() > OVERHEAT)
	{
		float p = 0.5f + 0.5f * sinf(m_time * 12.0f);
		CenterText((const char*)u8"！！　過熱　！！", 0.20f, 1.3f, IM_COL32(255, 70, 50, (int)(150 + p * 105)), jp);
	}

	// 打撃フィードバックのポップアップ(鉄条の上でフェード)
	if (m_popupLife > 0.0f)
	{
		float a = m_popupLife / POPUP_LIFE;
		if (a > 1.0f) a = 1.0f;
		unsigned int c = (m_popupCol & 0x00FFFFFF) | ((unsigned int)(a * 255) << 24);
		CenterText(m_popupText, 0.36f, 1.15f, c, jp);	// 主人公の独白も指引 UI と同じ游明朝(旧: メイリオ17pxを2倍=ぼやけた)
	}

	// スコアと形状一致度(左上)+一致度バー。プレイ中の情報を「今やること」に絞る為、F1(デバッグ)の時だけ出す。
	//   成果は結果画面(DrawResultUI)で見せる。
	if (DebugUI::IsVisible())
	{
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		char sb[48];
		sprintf_s(sb, sizeof(sb), "SCORE  %d", m_score);
		dl->AddText(ImVec2(40, 40), IM_COL32(255, 235, 200, 255), sb);
		sprintf_s(sb, sizeof(sb), "SHAPE MATCH  %d%%", (int)(m_match * 100));
		dl->AddText(ImVec2(40, 60), IM_COL32(150, 220, 255, 255), sb);

		// 一致度バー(上部中央)
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

	// 操作ガイド(宏観: 今いる場所で使えるキー)。「どこを叩け」等の微観の指示は出さない。
	//   キーはアイコン(Kenney Input Prompts)で見せる=文字の「左クリック」を読むより一目で分かる。
	//   場所ごとの表(データ)。キー割り当てを変えたらここの表だけ直す。
	static const KeyHint WALK_HINTS[] = {
		{ { "keyboard_w", "keyboard_a", "keyboard_s", "keyboard_d" }, (const char*)u8"移動" },
		{ { "mouse_move" },  (const char*)u8"見回す" },
		{ { "keyboard_e" },  (const char*)u8"使う" },
	};
	static const KeyHint ANVIL_HINTS[] = {
		{ { "mouse_move" },  (const char*)u8"狙う" },
		{ { "mouse_left" },  (const char*)u8"長押し：ハンマー" },
		{ { "keyboard_f" },  (const char*)u8"裏返す" },
		{ { "keyboard_e" },  (const char*)u8"離れる" },
	};
	static const KeyHint HEARTH_HINTS[] = {
		{ { "keyboard_r" },  (const char*)u8"長押し：ふいごで風を送る" },
		{ { "keyboard_e" },  (const char*)u8"炉から出す" },
	};
	static const KeyHint GRIND_HINTS[] = {
		{ { "mouse_right", "keyboard_space" }, (const char*)u8"連打：ペダル" },
		{ { "mouse_left" },  (const char*)u8"長押し：刃を当てる" },
		{ { "mouse_horizontal" }, (const char*)u8"左右：滑らせる" },
		{ { "mouse_vertical" },   (const char*)u8"上下：刃の角度" },
		{ { "keyboard_f" },  (const char*)u8"裏返す" },
		{ { "keyboard_e" },  (const char*)u8"離れる" },
	};
	static const KeyHint TROUGH_HINTS[] = {
		{ { "mouse_left" },  (const char*)u8"水に沈める" },
		{ { "keyboard_e" },  (const char*)u8"離れる" },
	};
	static const KeyHint QUENCH_STIR_HINTS[] = {
		{ { "mouse_vertical" }, (const char*)u8"上下：刃を揺する" },
	};
	const KeyHint* hints = WALK_HINTS; int nHints = _countof(WALK_HINTS);
	if (!m_walkMode && !Transitioning())
	{
		switch (m_station)
		{
		case Station::Anvil:      hints = ANVIL_HINTS;  nHints = _countof(ANVIL_HINTS);  break;
		case Station::Hearth:     hints = HEARTH_HINTS; nHints = _countof(HEARTH_HINTS); break;
		case Station::Grindstone: hints = GRIND_HINTS;  nHints = _countof(GRIND_HINTS);  break;
		case Station::Trough:     hints = TROUGH_HINTS; nHints = _countof(TROUGH_HINTS); break;
		}
		if (QuenchStirring()) { hints = QUENCH_STIR_HINTS; nHints = _countof(QUENCH_STIR_HINTS); }	// 淬火中: 離れられない=揺するだけ
	}
	const float GUIDE_Y = 0.93f;		// 操作ガイドの縦位置(画面高さ比)
	// 今その操作ができない間は出さない(出すと「押せば効く」と誤解させる。ユーザー指摘 2026-10-07):
	//   翻面中(翻面の操作は上の専用の案内が出る) / 拍子表の再生中(掴む・置くの演出) / 走動⇔工位の移動中
	const bool inputLocked = (m_flipPhase != FlipPhase::None) || SequencePlaying() || Transitioning();
	if (!inputLocked) DrawKeyHints(hints, nHints, GUIDE_Y);
	DrawStirPrompt();	// 淬火で揺する時だけ、大きな動く案内(膜を破るまで)
	DrawGrindHint();	// 研ぎで同じ誤りをくり返した時だけ、直し方の動く案内

	// 互動提示(走動中、範囲内で物件を見ている時だけ「E」を物件の上に出す)
	DrawInteractPrompt();
}

//--- 工程リスト(クエストトラッカー)。画面左の羊皮紙に配方の工程を縦に並べ、済/今/未 を見分けさせる。
//    「全部で何工程・今どこか」が分かる=迷子にならない(UX)。工程名は配方の label(データ)。
//    見た目は KCD の「紙にインクで書いた」調: 飾り枠の羊皮紙(ナインスライス)+焦茶のインク、今の工程だけ朱。
//    済の工程は取り消し線(クエストログの定番の表し方)。
void SceneForge::DrawStepTracker()
{
	if (!m_recipe) return;
	ImVec2 disp = ImGui::GetIO().DisplaySize;		// 寸法はすべて画面比(解像度非依存)
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	auto JP = [](float px) { return DebugUI::FontJPFor(px); };	// 見出し/工程名/表裏で大きさが違う→それぞれ一番近い実寸

	// --- 羊皮紙パネル ---
	const float TRACKER_SCALE   = 0.8f;	// パネル全体の大きさ(1 = 初版)。視野を遮らない様に小さく(2026-10-09 ユーザー要望)
	const float PANEL_X_RATIO   = 0.015f;	// パネルの左端(画面幅比)
	const float PANEL_Y_RATIO   = 0.22f;	// パネルの上端(画面高さ比)
	const float PANEL_W_RATIO   = 0.22f * TRACKER_SCALE;	// パネルの幅(画面幅比)。狭すぎると上辺中央の飾りが横に潰れる
	const float FRAME_CORNER_RATIO = 0.085f * TRACKER_SCALE;	// 画面上の隅の大きさ(画面高さ比)
	const float INSET_TOP       = 0.95f;	// 中身の上端=上辺の飾りの下(隅の大きさに対する比)
	const float INSET_BOTTOM    = 0.75f;	// 中身の下端から下辺まで(同)
	const float INSET_SIDE      = 0.60f;	// 中身の左右の余白(同)。枠の二重線の内側

	// --- 行 ---
	const float ROW_RATIO     = 0.050f * TRACKER_SCALE;	// 行の間隔
	const float TEXT_RATIO    = 0.030f * TRACKER_SCALE;	// 工程名の文字の高さ
	const float HEAD_RATIO    = 0.026f * TRACKER_SCALE;	// 見出し「工程」の文字の高さ
	const float HEAD_GAP      = 1.1f;	// 見出しから1行目までの間隔(行単位)
	const float DOT_RATIO     = 0.0065f * TRACKER_SCALE;	// 丸印の半径
	const float TEXT_GAP_RATIO= 0.012f * TRACKER_SCALE;	// 丸と工程名の間(画面高さ比)
	const float NOW_DOT_SCALE = 1.3f;	// 「今」の丸は少し大きく
	const float LINE_W        = 1.5f;	// 輪・取り消し線の太さ(px)
	const float PULSE_SEC     = 1.2f;	// 工程が変わった直後、今の工程を明滅させる長さ
	const float PULSE_FREQ    = 10.0f;	// その明滅の速さ(rad/秒)
	const float PULSE_MIN     = 0.3f;	// 明滅の一番淡い時の濃さ(0..1)
	const ImU32 INK_HEAD    = IM_COL32( 60,  38,  22, 255);	// 見出し: 一番濃い焦茶
	const ImU32 INK_DONE    = IM_COL32(110,  88,  64, 170);	// 済: 薄れたインク+取り消し線
	const ImU32 INK_NOW     = IM_COL32(150,  32,  18, 255);	// 今: 朱(KCD の赤い印章の色)=一番目立つ
	const ImU32 INK_PENDING = IM_COL32( 60,  42,  28, 220);	// 未: 普通のインク

	// --- 今の工程の下の進捗バー(工程の中の進み具合=「止まって見えない」様に) ---
	//   鍛造=表/裏の2本(今上の面を濃く) / 研ぎ=刃の1本。どこを叩け等は出さない(面全体の割合だけ)。
	const float SUB_ROW_RATIO  = 0.030f * TRACKER_SCALE;	// 進捗バー1本分の行の高さ
	const float SUB_TEXT_RATIO = 0.022f * TRACKER_SCALE;	// 「表/裏/刃」の文字の高さ
	const float BAR_W_RATIO    = 0.085f * TRACKER_SCALE;	// バーの長さ(画面幅比)
	const float BAR_H_RATIO    = 0.010f * TRACKER_SCALE;	// バーの太さ(画面高さ比)。細すぎると紙の模様に紛れて見えなかった
	const float BAR_GAP_RATIO  = 0.012f * TRACKER_SCALE;	// 文字とバーの間(画面高さ比)
	const ImU32 BAR_BG_COL     = IM_COL32(60, 42, 28, 110);	// 紙に引いた溝(空でも「ここにバーがある」と分かる濃さ)
	const ImU32 BAR_EDGE_COL   = IM_COL32(60, 42, 28, 200);	// 溝の輪郭(インクの線)
	const ImU32 BAR_IDLE_COL   = INK_PENDING;					// 今は下を向いている面の進み(普通のインク。旧: 薄れたインクで見えにくかった)
	const float BAR_EDGE_W     = 1.0f;							// 輪郭の太さ(px)
	struct SubBar { const char* name; float prog; bool active; };
	SubBar subs[ForgingSim::NSIDES];
	int nSub = 0;
	const StepName nowType = CurrentStep().type;
	if (nowType == StepName::Forge)
	{
		subs[nSub++] = { (const char*)u8"表", m_forging.SideProgress(0), m_forging.Side() == 0 };
		subs[nSub++] = { (const char*)u8"裏", m_forging.SideProgress(1), m_forging.Side() == 1 };
	}
	else if (nowType == StepName::Grind)
	{
		// 両側の刃(Fキーで裏返して研ぐ面を変える)。今研いでいる面を濃く
		subs[nSub++] = { (const char*)u8"表", m_forging.SharpProgress(0), GrindSide() == 0 };
		subs[nSub++] = { (const char*)u8"裏", m_forging.SharpProgress(1), GrindSide() == 1 };
	}
	else if (nowType == StepName::Quench)
		subs[nSub++] = { (const char*)u8"冷", QuenchProgress(), true };	// 冷え切った(焼きが入った)割合

	// 済んでいないバーは満タンに見せない: 進捗は区域の平均なので、1区域だけ少し足りなくても 99% = 見た目は満タンになり、
	// 「全部終わったのに進まない(99% で止まる)」と見えた(2026-10-08)。全区域が済んだ時だけ 1、それ以外は UNFINISHED_MAX まで。
	// 鍛造/研ぎは対象外: 残り作業量の減算(WorkField)で、バーは「済んだ作業 / 全作業」そのもの=満タン=完成が保証される。
	const float UNFINISHED_MAX = 0.9f;
	if (nowType != StepName::Forge && nowType != StepName::Grind)
		for (int k = 0; k < nSub; ++k)
			if (subs[k].prog < 1.0f) subs[k].prog = fminf(subs[k].prog, UNFINISHED_MAX);

	const int   n      = (int)m_recipe->steps.size();
	const float row    = disp.y * ROW_RATIO;
	const float subRow = disp.y * SUB_ROW_RATIO;
	const float r      = disp.y * DOT_RATIO;
	const float corner = disp.y * FRAME_CORNER_RATIO;

	// パネルの大きさは中身から決める(工程数・進捗バーの本数が変わっても枠が合う)
	const float contentH = row * HEAD_GAP + row * n + subRow * nSub;
	ImVec2 pa(disp.x * PANEL_X_RATIO, disp.y * PANEL_Y_RATIO);
	ImVec2 pb(pa.x + disp.x * PANEL_W_RATIO, pa.y + corner * INSET_TOP + contentH + corner * INSET_BOTTOM);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	const float x  = pa.x + corner * INSET_SIDE;
	float       cy = pa.y + corner * INSET_TOP;
	dl->AddText(JP(disp.y * HEAD_RATIO), disp.y * HEAD_RATIO, ImVec2(x, cy), INK_HEAD, (const char*)u8"工程");
	cy += row * HEAD_GAP;

	// 取り消し線のアニメ: 済になった瞬間から左→右へ引き、済でなくなった瞬間から右→左へ消す。
	const float STRIKE_ANIM_SEC = 0.80f;	// 線を引き切る/消し切るまでの秒(SE_PENCIL/SE_ERASER の長さと揃える)
	const float STRIKE_Y        = 0.55f;	// 線の高さ(文字の高さに対する比。中ほど)
	const float STRIKE_W        = 2.0f;		// 線の太さ(px)。ペンで引いた様に輪郭より少し太く
	const float sinceChange = m_time - m_stepChangedAt;
	for (int i = 0; i < n; ++i, cy += row)
	{
		const bool now = i == m_stepIdx;
		const TrackerRow rowState = (i < (int)m_trackerRows.size()) ? m_trackerRows[i] : TrackerRow{};
		// 線の長さ 0..1: 切り替わってからの経過 p を ease-out(最初速く最後ゆっくり=ペンの勢い)で曲げる
		float p = fminf((m_time - rowState.changedAt) / STRIKE_ANIM_SEC, 1.0f);
		p = 1.0f - (1.0f - p) * (1.0f - p);
		const float strike = rowState.completed ? p : 1.0f - p;

		// 色も線の進み具合に合わせて「未」のインク→「済」の薄れたインクへ
		ImU32 col = now ? INK_NOW : LerpColor(INK_PENDING, INK_DONE, strike);
		const float px = disp.y * TEXT_RATIO;
		ImVec2 dot(x + r, cy + px * 0.5f);

		if (now)
		{
			float pulse = 1.0f;
			if (sinceChange < PULSE_SEC) pulse = Lerp::Linear(PULSE_MIN, 1.0f, 0.5f + 0.5f * cosf(sinceChange * PULSE_FREQ));	// 変わった直後だけ明滅
			col = (col & 0x00FFFFFF) | ((ImU32)(255 * pulse) << IM_COL32_A_SHIFT);
			dl->AddCircleFilled(dot, r * NOW_DOT_SCALE, col);
		}
		else if (strike > 0.5f) dl->AddCircleFilled(dot, r, col);			// 済=塗り
		else                    dl->AddCircle(dot, r, col, 0, LINE_W);		// 未=輪だけ

		const char* label = m_recipe->steps[i].label;
		ImVec2 tp(x + r * 2.0f * NOW_DOT_SCALE + disp.y * TEXT_GAP_RATIO, cy);
		dl->AddText(JP(px), px, tp, col, label);
		if (!now && strike > 0.0f)	// 取り消し線(左から strike の割合だけ)
		{
			ImVec2 ts = JP(px)->CalcTextSizeA(px, FLT_MAX, 0.0f, label);
			float ly = tp.y + ts.y * STRIKE_Y;
			dl->AddLine(ImVec2(tp.x, ly), ImVec2(tp.x + ts.x * strike, ly), INK_DONE, STRIKE_W);
		}

		if (!now) continue;
		// 今の工程の下に進捗バー(表/裏 or 刃)。後ろの工程はその分だけ下へずらす
		for (int k = 0; k < nSub; ++k)
		{
			cy += subRow;
			float spx = disp.y * SUB_TEXT_RATIO;
			ImVec2 sp(tp.x, cy + (row - subRow));
			ImU32  sc = subs[k].active ? INK_NOW : BAR_IDLE_COL;
			dl->AddText(JP(spx), spx, sp, sc, subs[k].name);
			float bx0 = sp.x + spx + disp.y * BAR_GAP_RATIO, bx1 = bx0 + disp.x * BAR_W_RATIO;
			float bh  = disp.y * BAR_H_RATIO, by = sp.y + (spx - bh) * 0.5f;
			dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), BAR_BG_COL);
			dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * subs[k].prog, by + bh), sc);
			dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + bh), BAR_EDGE_COL, 0.0f, 0, BAR_EDGE_W);
		}
	}
}

//--- 状況から「今何をするか」と「どこへ向かうか」を決める(コンテキストヒント)。
//    工程(配方)は大きな段階しか持たない: 鍛造の工程は両面が仕上がるまで続き、その間に
//    「冷めたので炉へ→熱くなったら金床へ戻る」「片面が終わったので裏返す」が何度も起きる。
//    それらは工程を増やさず(配方=データは不変)、ここで温度/鉄の在り処/面の進捗を見て言い分ける。
//    上から順に、より緊急な状況を先に見る(冷えて叩けない > 裏返し > 工程の基本文)。
//    閾値は既存の物(COLD_LIMIT=冷打になる温度 / IDEAL_MIN=適温帯の下限 / QUENCH_MIN_TEMP)を使う=判定とズレない。
const char* SceneForge::GuideFor(Station& goal) const
{
	const StepSetting& st = CurrentStep();
	const float heat = m_forging.Heat();
	const bool  inFire = !m_carrying && m_workAt == Station::Hearth;	// 鉄が炉の中に置いてある
	goal = StepStation(st.type);

	// 温度の閾値は工程リスト(RowCompleted)と同じ ReadyTemp/MinWorkTemp を使う=案内文と取り消し線が食い違わない。
	switch (st.type)
	{
	case StepName::Forge:
		if (inFire)
		{
			if (heat < ReadyTemp(st.type)) { goal = Station::Hearth; return (const char*)u8"炉で、赤くなるまで熱する"; }
			goal = Station::Anvil;
			return (const char*)u8"十分に熱くなった。金床へ運ぶ";
		}
		if (heat < MinWorkTemp(st.type)) { goal = Station::Hearth; return (const char*)u8"鉄が冷めた。炉で熱し直す"; }
		if (m_forging.SideDone(m_forging.Side()) && !m_forging.BothSidesDone())
			return (const char*)u8"この面は仕上がった。Fキーで裏返す";
		return st.instruction;

	case StepName::Grind:
	{
		// 「Fキーで裏返す」は、今研いでいる面が仕上がっていて、もう片方が残っている時だけ(鍛造と同じ判断)。
		//   裏返した後(今の面がまだ)は普段の案内に戻る=文が変わるので案内は淡出→淡入する(2026-10-07: 裏返しても消えなかった)。
		const float DONE = 1.0f;
		const int  side = GrindSide();
		const bool thisDone  = m_forging.SharpProgress(side)     >= DONE;
		const bool otherDone = m_forging.SharpProgress(1 - side) >= DONE;
		if (thisDone && !otherDone) return (const char*)u8"この面の刃は仕上がった。Fキーで裏返す";
		return st.instruction;
	}

	case StepName::Quench:
		// 淬火の動画が始まったら(刃を立て始めた後)、水で冷えていくのは正しい=温度の注意は出さない
		if (QuenchStirring()) { goal = Station::Trough; return (const char*)u8"刃を上下に揺すって、蒸気の膜を破る"; }	// 揺する段階(マウス上下)
		if (m_quenchTurn > 0.0f) { goal = Station::Trough; return st.instruction; }
		if (inFire)
		{
			if (heat < ReadyTemp(st.type)) { goal = Station::Hearth; return (const char*)u8"炉で、火花が散るまで熱する"; }
			goal = Station::Trough;
			return (const char*)u8"十分に熱くなった。水槽へ運ぶ";
		}
		if (heat < MinWorkTemp(st.type)) { goal = Station::Hearth; return (const char*)u8"刃が冷めた。炉で熱し直してから水へ"; }
		if (heat > QUENCH_MAX_TEMP) return (const char*)u8"熱すぎる。赤い所から外れるまで少し冷ます";	// 窓の上限(行き先は水槽のまま)
		return st.instruction;

	default:
		return st.instruction;
	}
}

//--- 毎フレーム: 案内文と行き先を更新。文が変わった時だけ時刻を記録(淡入のきっかけ)。
//    文言は文字列リテラル(静的)なのでポインタ比較で「変わったか」が分かる。
void SceneForge::UpdateGuide()
{
	Station goal;
	const char* text = GuideFor(goal);
	m_guideGoal = goal;
	if (text != m_guideText) { m_guideText = text; m_guideChangedAt = m_time; }
	UpdateTracker();
}

//--- 加熱が「済」になる温度 = 次の工程を始めるのに十分な熱さ。
//    鍛造: 温度ゲージの緑帯(適温)に入った所 / 焼入れ: 火花が散る(=加熱工程の完了条件と同じ)。
float SceneForge::ReadyTemp(StepName next) const
{
	switch (next)
	{
	case StepName::Forge:  return IDEAL_MIN + FORGE_READY_MARGIN;	// 緑帯に少し入った所(下の MinWorkTemp との幅=ヒステリシス)
	case StepName::Quench: return ForgingSim::BURN_TEMP;
	default:               return ForgingSim::BURN_TEMP;
	}
}

//--- 加熱が「済」でなくなる温度 = 次の工程がもうできない冷たさ。
//    鍛造: 緑帯の下端(IDEAL_MIN。これ未満の打撃は有効でない) / 焼入れ: 焼きが入らない温度(QUENCH_MIN_TEMP)。
//    ReadyTemp より低い=「済になる温度」と「済でなくなる温度」の間に幅がある(ヒステリシス)。
//    1つの閾値だと、その前後で温度が揺れた時に取り消し線が毎フレーム付いたり消えたりしてしまう。
float SceneForge::MinWorkTemp(StepName next) const
{
	switch (next)
	{
	case StepName::Forge:  return IDEAL_MIN;
	case StepName::Quench: return QUENCH_MIN_TEMP;
	default:               return 0.0f;
	}
}

//--- i 行目は今「済」か。
//    今の工程/まだの工程 → 済でない。過ぎた工程 → 済(形や刃は戻らない)。
//    ただし「加熱」の行で、その加熱が準備した工程(=すぐ次の行)が今進行中なら、温度で生きている:
//      済だった → MinWorkTemp を下回ったら済でなくなる / 済でなかった → ReadyTemp に達したら済。
//    次の工程が終われば温度はもう関係ないので、以後は普通に済のまま(例: 研ぎは冷えていてよい)。
bool SceneForge::RowCompleted(int i, bool wasCompleted) const
{
	if (!m_recipe || i >= m_stepIdx) return false;
	const std::vector<StepSetting>& steps = m_recipe->steps;
	if (steps[i].type == StepName::Heat && i + 1 == m_stepIdx)
	{
		const StepName next = steps[i + 1].type;
		const float heat = m_forging.Heat();
		return wasCompleted ? heat >= MinWorkTemp(next) : heat >= ReadyTemp(next);
	}
	return true;
}

//--- 各行の completed を判定し、前フレームと違えば(エッジ検出)切り替わった時刻を記録する。
//    描画側(DrawStepTracker)はこの時刻からの経過で取り消し線を伸ばす/縮める。
void SceneForge::UpdateTracker()
{
	if (!m_recipe) return;
	const int n = (int)m_recipe->steps.size();
	if ((int)m_trackerRows.size() != n) m_trackerRows.assign(n, TrackerRow{});
	// 同じフレームで複数の行が切り替わる事がある(工程が進んだ瞬間など)。音は種類ごとに1回だけ鳴らす。
	bool struck = false, erased = false;
	for (int i = 0; i < n; ++i)
	{
		TrackerRow& row = m_trackerRows[i];
		const bool now = RowCompleted(i, row.completed);
		if (now != row.completed)
		{
			row.completed = now;
			row.changedAt = m_time;
			(now ? struck : erased) = true;
		}
	}
	const float PENCIL_VOLUME = 0.8f;	// 線を引く音
	const float ERASER_VOLUME = 1.0f;	// 線を消す音(素材の平均音量が鉛筆より約7dB小さいので最大に)
	if (struck) Audio::Play(Audio::SE_PENCIL, PENCIL_VOLUME);
	if (erased) Audio::Play(Audio::SE_ERASER, ERASER_VOLUME);
}

//--- 走動中に「次に向かう点」を決める。鉄は瞬間移動しない設計なので、行き先は鉄の在り処で変わる:
//      鉄を手に持っている / 鉄が既に行き先の工位にある → 行き先の工位へ
//      鉄が別の所に置いてある                          → まず鉄を取りに行く
//    行き先は UpdateGuide が状況で決めた物(例: 鍛造中でも冷めていれば炉)。
//    工位で作業中(走動でない)/視点の移動中は出さない。
bool SceneForge::GuideTarget(XMFLOAT3& pos, const char*& label)
{
	if (m_state != GAME_PLAY || !m_walkMode || Transitioning() || SequencePlaying()) return false;
	const Station goal = m_guideGoal;
	if (!m_carrying && m_workAt != goal)
	{
		pos   = StationBase(m_workAt);
		label = (const char*)u8"鉄を取る";
		return true;
	}
	pos = StationBase(goal);
	switch (goal)
	{
	case Station::Anvil:      label = (const char*)u8"金床"; break;
	case Station::Hearth:     label = (const char*)u8"炉";   break;
	case Station::Grindstone: label = (const char*)u8"砥石"; break;
	case Station::Trough:     label = (const char*)u8"水槽"; break;
	}
	return true;
}

//--- 目印(オブジェクティブマーカー)。行き先が画面内なら真上に▼、画面外(後ろ含む)なら画面端に矢印。
//    「E」提示が出ている間は目印を薄くする(同じ物に2つ重ねない)。
//    CPU 側の投影は転置しない行列(GetView(false)/GetProj(false))=DrawInteractPrompt と同じ規約。
void SceneForge::DrawObjectiveMarker()
{
	XMFLOAT3 target; const char* label = "";
	if (!GuideTarget(target, label)) return;
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;

	const float LIFT        = 0.35f;	// 作業点からどれだけ上に出すか(ワールド単位)。物に重ならない様に
	const float BOB_AMP     = 0.008f;	// 上下にゆっくり揺らす振幅(画面高さ比)=目に留まる
	const float BOB_FREQ    = 3.0f;		// 揺れの速さ(rad/秒)
	const float SIZE_RATIO  = 0.016f;	// ▼/矢印の大きさ(画面高さ比)
	const float LABEL_RATIO = 0.026f;	// 行き先名の文字の高さ
	const float EDGE_RATIO  = 0.07f;	// 画面外の時、画面端からどれだけ内側に置くか
	const float NEAR_Z      = 0.05f;	// これより手前(カメラ側)は「画面外」扱い
	const float TRI_H       = 1.2f;		// ▼の高さ(大きさ s の何倍)
	const float LABEL_GAP   = 0.004f;	// ▼と行き先名の間(画面高さ比)
	const float ARROW_TIP   = 1.4f;		// 画面端の矢印: 先端までの長さ(s の何倍)
	const float ARROW_BACK  = 0.6f;		// 同: 根元までの長さ(s の何倍)
	const float ARROW_LABEL = 2.5f;		// 同: 文字を矢印から内側へずらす量(s の何倍)
	const float alpha = 1.0f - m_promptAlpha;	// E 提示と入れ替わりに消える
	if (alpha < 0.01f) return;
	const int   a   = (int)(255 * alpha);
	const ImU32 col = IM_COL32(255, 200, 110, a);			// 炉の火の色(工程リストの「今」と同じ=同じ意味)
	const ImU32 shd = IM_COL32(0, 0, 0, (int)(160 * alpha));

	XMFLOAT4X4 v4 = cam->GetView(false), p4 = cam->GetProj(false);
	XMVECTOR vp = XMVector3TransformCoord(XMVectorSet(target.x, target.y + LIFT, target.z, 1.0f), XMLoadFloat4x4(&v4));
	XMFLOAT3 v; XMStoreFloat3(&v, vp);		// ビュー空間(+Z=前, +X=右, +Y=上)

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImFont* jp = DebugUI::FontJPFor(disp.y * LABEL_RATIO);	// 行き先名の大きさに一番近い実寸のフォント
	const float s = disp.y * SIZE_RATIO;
	const float edge = disp.y * EDGE_RATIO;

	bool onScreen = false;
	float sx = 0, sy = 0;
	if (v.z > NEAR_Z)
	{
		XMVECTOR clip = XMVector4Transform(XMVectorSet(v.x, v.y, v.z, 1.0f), XMLoadFloat4x4(&p4));
		float w = XMVectorGetW(clip);
		sx = ( XMVectorGetX(clip) / w * 0.5f + 0.5f) * disp.x;
		sy = (-XMVectorGetY(clip) / w * 0.5f + 0.5f) * disp.y;
		onScreen = sx > edge && sx < disp.x - edge && sy > edge && sy < disp.y - edge;
	}

	if (onScreen)
	{
		sy += sinf(m_time * BOB_FREQ) * disp.y * BOB_AMP;
		// ▼(先端が行き先を指す)
		ImVec2 p0(sx - s, sy - s * TRI_H), p1(sx + s, sy - s * TRI_H), p2(sx, sy);
		dl->AddTriangleFilled(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x + 1, p1.y + 1), ImVec2(p2.x + 1, p2.y + 1), shd);
		dl->AddTriangleFilled(p0, p1, p2, col);
		float px = disp.y * LABEL_RATIO;
		ImVec2 ls = jp->CalcTextSizeA(px, FLT_MAX, 0.0f, label);
		ImVec2 lp(sx - ls.x * 0.5f, sy - s * TRI_H - ls.y - disp.y * LABEL_GAP);
		dl->AddText(jp, px, ImVec2(lp.x + 1, lp.y + 1), shd, label);
		dl->AddText(jp, px, lp, col, label);
		return;
	}

	// 画面外: ビュー空間の (x, y) の向き=画面上の向き。後ろにある時もこの向きで「どちらへ振り向けばよいか」が分かる。
	//   真後ろで向きが決まらない時は下向き(=振り返れ)にする。
	float dx = v.x, dy = -v.y;					// 画面座標は y が下向き
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-4f) { dx = 0.0f; dy = 1.0f; len = 1.0f; }
	dx /= len; dy /= len;
	// 画面中心から (dx,dy) 方向へ伸ばし、内側の矩形(端から edge)にぶつかる所に置く
	float hw = disp.x * 0.5f - edge, hh = disp.y * 0.5f - edge;
	float t = fminf(fabsf(dx) > 1e-4f ? hw / fabsf(dx) : FLT_MAX, fabsf(dy) > 1e-4f ? hh / fabsf(dy) : FLT_MAX);
	ImVec2 c(disp.x * 0.5f + dx * t, disp.y * 0.5f + dy * t);
	// 矢印(先端が行き先の方向)
	ImVec2 n(-dy, dx);	// 向きに直交
	ImVec2 tip (c.x + dx * s * ARROW_TIP, c.y + dy * s * ARROW_TIP);
	ImVec2 b0  (c.x - dx * s * ARROW_BACK + n.x * s, c.y - dy * s * ARROW_BACK + n.y * s);
	ImVec2 b1  (c.x - dx * s * ARROW_BACK - n.x * s, c.y - dy * s * ARROW_BACK - n.y * s);
	dl->AddTriangleFilled(tip, b0, b1, col);
	float px = disp.y * LABEL_RATIO;
	ImVec2 ls = jp->CalcTextSizeA(px, FLT_MAX, 0.0f, label);
	// 文字は矢印の内側(画面中心寄り)に置く=画面からはみ出さない
	ImVec2 lp(c.x - dx * s * ARROW_LABEL - ls.x * 0.5f, c.y - dy * s * ARROW_LABEL - ls.y * 0.5f);
	dl->AddText(jp, px, ImVec2(lp.x + 1, lp.y + 1), shd, label);
	dl->AddText(jp, px, lp, col, label);
}

//--- 出来栄え 0..1: 形の一致度・打撃品質の平均・淬火の出来(膜を早く破ったか)を重み合成する。
//    誤打(冷打/過熱/完成済みを叩く)は品質0の打撃として平均を下げる=罰でなく「腕前」として自然に効く。
float SceneForge::GradeScore() const
{
	// 打撃品質の平均(1打も打たずに淬火した場合は0扱い=除算回避)。
	float qAvg = (m_strikeCount > 0) ? (m_qualitySum / (float)m_strikeCount) : 0.0f;
	float s = GRADE_W_MATCH * m_match + GRADE_W_QUALITY * qAvg + GRADE_W_QUENCH * QuenchScore();
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
	// 羊皮紙を下地に敷き、その上に成果を書く。
	//   紙(橙色)の上の文字は「濃いインク・影なし」(黒い影は紙の上で文字を濁らせ、対比を下げていた)。
	//   文字は全部、紙の破れ縁の内側に収める。操作案内だけは紙の外(暗い背景)に白で出す=紙の文字と役割が違う。
	DrawParchmentPanel(0.50f, 0.72f);
	const ImU32 INK_DARK  = IM_COL32(40, 20,  8, 255);	// 見出し/数値: 一番濃い焦茶(橙の紙との対比を最大に)
	const ImU32 INK_LABEL = INK_DARK;	// 項目名も一番濃いインク(旧: 少し薄い焦茶+細い書体で、紙の模様に埋もれた)
	// 縦位置(画面高さ比)。紙の破れ縁の内側 ≒ 0.36〜0.71(DrawParchmentPanel(0.50, 0.72) の時)
	const float TITLE_Y = 0.375f, GRADE_Y = 0.50f, MATCH_Y = 0.615f, SCORE_Y = 0.665f;
	const float PROMPT_Y = 0.83f;	// 紙の下の外
	const float TITLE_SCALE = 1.10f, GRADE_SCALE = 2.2f, PROMPT_SCALE = 0.80f;
	// 成績の2行は太い見出し書体(Cinzel Black, 64px で焼いてある)を半分の大きさで。
	//   本文書体(EB Garamond)は線が細く、模様の多い羊皮紙の上では読めなかった。
	const float STAT_SCALE = 0.50f;
	CenterText("FORGED!", TITLE_Y, TITLE_SCALE, INK_DARK, title, false);

	// --- 等級(S/A/B/C): 一番大きく、等級ごとに色を変えて主役にする(どれも紙の上で読める濃さ) ---
	char g = GradeLetter();
	ImU32 gcol;
	switch (g)
	{
	case 'S': gcol = IM_COL32(150,  25,  10, 255); break;	// 朱(印章の色)=特別
	case 'A': gcol = IM_COL32( 45,  22,   8, 255); break;	// 濃い焦茶
	case 'B': gcol = IM_COL32( 70,  45,  25, 255); break;
	default:  gcol = IM_COL32( 90,  65,  45, 255); break;	// C=少し薄いインク
	}
	char gbuf[8]; sprintf_s(gbuf, sizeof(gbuf), "%c", g);
	CenterText(gbuf, GRADE_Y, GRADE_SCALE, gcol, title, false);	// 等級=最大サイズ

	char buf[64];
	sprintf_s(buf, sizeof(buf), "SHAPE MATCH   %d%%", (int)(m_match * 100));
	CenterText(buf, MATCH_Y, STAT_SCALE, INK_LABEL, title, false);
	sprintf_s(buf, sizeof(buf), "SCORE   %d", m_score);
	CenterText(buf, SCORE_Y, STAT_SCALE, INK_LABEL, title, false);
	CenterText("PRESS  SPACE  TO  RETURN", PROMPT_Y, PROMPT_SCALE, IM_COL32(240, 228, 205, 230), body);	// 暗い背景の上=影あり
	// 素材のクレジット(CC BY は作者名の表示が使用条件。一覧は CREDITS.txt)。目立たない様に画面下端に小さく
	const float CREDIT_Y = 0.95f, CREDIT_SCALE = 0.45f;
	CenterText("Sound: ZijunSANG, soundslikewillem (freesound.org, CC BY-NC 4.0)", CREDIT_Y, CREDIT_SCALE, IM_COL32(200, 190, 170, 170), body);
}

//--- 羊皮紙の選択パネル(一時停止メニュー / タイトルのモード選択)。工程リストと同じ見た目。
//    項目はマウスのホバーで選び、クリックで決定(戻り値)。実行は呼び出し側の Update=描画の途中で状態を変えない。寸法は画面比。
int SceneForge::DrawChoicePanel(const char* head, const char* const* labels, int n, int& sel, float centerY, bool dimBack)
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	const ImU32 DIM_COL       = IM_COL32(0, 0, 0, 150);			// 背後の世界を暗く(止まっている事が分かる)
	const float PANEL_W_RATIO = 0.30f;							// パネルの幅(画面幅比)
	const float CORNER_RATIO  = 0.085f;							// 隅の大きさ(画面高さ比)=工程リストと同じ
	const float HEAD_RATIO    = 0.040f;							// 見出しの文字の高さ
	const float ITEM_RATIO    = 0.034f;							// 項目の文字の高さ
	const float ROW_RATIO     = 0.075f;							// 項目の間隔
	const float HEAD_GAP      = 1.3f;							// 見出しから1項目目まで(行単位)
	const float INSET_TOP     = 0.95f, INSET_BOTTOM = 0.75f;	// 枠の飾りの内側(隅の大きさ比)=工程リストと同じ
	const float MARK_RATIO    = 0.010f;							// 選んでいる項目の左の印(三角)の大きさ
	const float MARK_GAP      = 0.6f;							// 印と文字の間(印の大きさ比)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);		// 工程リストと同じインク
	const ImU32 INK_ITEM = IM_COL32( 60,  42,  28, 220);
	const ImU32 INK_SEL  = IM_COL32(150,  32,  18, 255);		// 選択中 = 朱(工程リストの「今」と同じ)

	if (dimBack) dl->AddRectFilled(ImVec2(0, 0), disp, DIM_COL);

	const float corner = disp.y * CORNER_RATIO;
	const float row    = disp.y * ROW_RATIO;
	const float panelW = disp.x * PANEL_W_RATIO;
	const float panelH = corner * INSET_TOP + row * HEAD_GAP + row * n + corner * INSET_BOTTOM;
	const ImVec2 pa((disp.x - panelW) * 0.5f, disp.y * centerY - panelH * 0.5f);
	const ImVec2 pb(pa.x + panelW, pa.y + panelH);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	const float cx = (pa.x + pb.x) * 0.5f;
	float cy = pa.y + corner * INSET_TOP;
	{
		const float px = disp.y * HEAD_RATIO;
		ImFont* f = DebugUI::FontJPFor(px);
		const ImVec2 sz = f->CalcTextSizeA(px, FLT_MAX, 0.0f, head);
		dl->AddText(f, px, ImVec2(cx - sz.x * 0.5f, cy), INK_HEAD, head);
	}
	cy += row * HEAD_GAP;

	int clicked = -1;
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const float  ipx   = disp.y * ITEM_RATIO;
	ImFont* f = DebugUI::FontJPFor(ipx);
	for (int i = 0; i < n; ++i, cy += row)
	{
		// 当たり判定 = パネルの幅いっぱい × 1行(文字の上だけでなく行のどこでも選べる)
		const ImVec2 ra(pa.x + corner * INSET_BOTTOM, cy - (row - ipx) * 0.5f), rb(pb.x - corner * INSET_BOTTOM, ra.y + row);
		const bool hover = mouse.x >= ra.x && mouse.x <= rb.x && mouse.y >= ra.y && mouse.y <= rb.y;
		if (hover) sel = i;
		if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = i;

		const bool isSel = (i == sel);
		const ImVec2 sz = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, labels[i]);
		const float tx = cx - sz.x * 0.5f;
		dl->AddText(f, ipx, ImVec2(tx, cy), isSel ? INK_SEL : INK_ITEM, labels[i]);
		if (isSel)
		{
			const float m = disp.y * MARK_RATIO, my = cy + ipx * 0.5f, mx = tx - m * (1.0f + MARK_GAP);
			dl->AddTriangleFilled(ImVec2(mx - m, my - m), ImVec2(mx - m, my + m), ImVec2(mx + m * 0.6f, my), INK_SEL);
		}
	}
	return clicked;
}

//--- 一時停止メニュー: 画面を暗くし、中央に選択パネル。クリックは m_pauseRequest へ(実行は UpdatePause)。
void SceneForge::DrawPauseMenu()
{
	static const char* LABELS[(int)PauseItem::Count] = {
		(const char*)u8"ゲームを続ける", (const char*)u8"タイトルへ戻る", (const char*)u8"ゲームを終了する" };
	const float CENTER_Y = 0.5f;
	const int clicked = DrawChoicePanel((const char*)u8"一時停止", LABELS, (int)PauseItem::Count, m_pauseSel, CENTER_Y, true);
	if (clicked >= 0) m_pauseRequest = (PauseItem)clicked;
}

//--- チュートリアル: 金床の工位で鍛造できる状態か(高亮・「ここで叩く」を出す条件)。
bool SceneForge::TutorialAtAnvil() const
{
	return m_tutorial && m_state == GAME_PLAY && !m_walkMode && !Transitioning() && !m_carrying
	    && m_station == Station::Anvil && m_workAt == Station::Anvil && m_flipPhase == FlipPhase::None && !m_clearDecided;
}

//--- チュートリアルの説明文: 今の状況で「何をするか・どの操作で・どこを見るか」を具体的に(通常モードの案内文より詳しく)。
const char* SceneForge::TutorialTip()
{
	if (!m_tutorial || m_state != GAME_PLAY || m_clearDecided || Transitioning() || SequencePlaying()) return nullptr;
	const float heat = m_forging.Heat();
	if (m_flipPhase != FlipPhase::None)
		return (const char*)u8"火ばさみで刃を裏返す。\n① 左クリックで刃を掴む\n② マウスを左右に動かして刃を回す\n③ 左クリックで、その面に決める\n④ もう一度 Fキーを押して火ばさみを戻すと、裏返し完了";
	if (m_walkMode)
		return m_carrying ? (const char*)u8"刃を運んでいる。\n画面の ▼ の印へ歩き、Eキーで置く。\n金床=叩く / 炉=熱する / 砥石=研ぐ / 水槽=焼入れ" : (const char*)u8"WASDキーで歩き、マウスで見回す。\n画面の ▼ の印が次の行き先。\n近づいて Eキーで使う(刃は Eキーで掴んで運ぶ)。";
	switch (m_station)
	{
	case Station::Anvil:
	{
		if (heat < IDEAL_MIN) return (const char*)u8"鉄が冷えて、温度ゲージの緑より下がった。\n冷えた鉄は叩いても形が変わらない。\nEキーで離れ、刃を掴んで炉へ運ぼう。";
		if (heat > IDEAL_MAX) return (const char*)u8"熱すぎる。\n温度ゲージの印が緑の範囲に下がるまで、少し待とう。";
		const int side = m_forging.Side();
		if (m_forging.SideDone(side) && !m_forging.SideDone(1 - side)) return (const char*)u8"この面は完成！\nFキーで裏返して、反対の面を叩こう。";
		return (const char*)u8"左クリック長押しで力を溜め、離すと叩く。\nマウスの上下でハンマーの位置を動かす。\n青く光る所(まだ黒皮が残る所)を叩こう。\n温度ゲージが緑の時だけ形が変わる。";
	}
	case Station::Hearth:
	{
		// 次に使う熱さ: 研ぎまで進んでいれば焼入れ(火花が散るまで) / まだなら鍛造(緑の範囲)
		const bool forQuench = StepReached(StepName::Grind);
		if (heat > OVERHEAT) return (const char*)u8"過熱している！\nすぐに Eキーで炉から出そう。";
		if (heat >= (forQuench ? ForgingSim::BURN_TEMP : IDEAL_MIN + FORGE_READY_MARGIN)) return (const char*)u8"十分に熱くなった。\nEキーで炉から出し、刃を掴んで次の場所へ運ぼう。";
		return forQuench ? (const char*)u8"焼入れの前に、もう一度熱する。\nRキー長押しでふいごを踏む。\n刃から火花が散るまで熱したら、Eキーで出そう。" : (const char*)u8"Rキー長押しでふいごを踏むと、早く熱くなる。\n温度ゲージの印が緑の範囲に入るまで待とう。\n入れたままだと過熱するので注意。";
	}
	case Station::Grindstone:
	{
		if (m_forging.SharpProgress(GrindSide()) >= 1.0f && !m_forging.AllSharp()) return (const char*)u8"この面は研ぎ終わった。\nFキーで裏返して、反対の刃を研ごう。";
		if (m_wheel.Speed01() < GRIND_WORK_MIN_SPEED) return (const char*)u8"右クリック(または Spaceキー)を一定のリズムで押して、\nペダルを踏み、砥石を回し続けよう。";
		return (const char*)u8"左クリック長押しで刃を砥石に当てる。\nマウスの上下で角度を変え、下の角度計の緑に合わせる。\nマウスの左右で刃全体を研ごう。";
	}
	case Station::Trough:
		return QuenchStirring() ? (const char*)u8"マウスを上下に動かして、水の中で刃を揺すろう。\n揺するほど早く冷える。" : (const char*)u8"左クリックで刃を水に入れて焼入れする。\n(冷えすぎていると焼きが入らないので、炉で熱し直す)";
	default: return nullptr;
	}
}

//--- 右の説明パネル(工程リストと同じ羊皮紙。左右対称に置く)。文は折り返して枠に収める。
void SceneForge::DrawTutorialPanel()
{
	const char* tip = TutorialTip();
	if (!tip) return;
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float PANEL_RIGHT_RATIO = 0.985f;	// パネルの右端(画面幅比)= 工程リストの左端の左右対称
	const float PANEL_Y_RATIO     = 0.22f;	// 上端(工程リストと揃える)
	const float PANEL_W_RATIO     = 0.21f;
	const float CORNER_RATIO      = 0.065f;
	const float INSET_TOP = 0.95f, INSET_BOTTOM = 0.75f, INSET_SIDE = 0.60f;
	const float HEAD_RATIO = 0.021f, TEXT_RATIO = 0.019f, HEAD_GAP = 1.6f, LINE_SPACING = 1.35f;
	const ImU32 INK_HEAD = IM_COL32(150, 32, 18, 255);	// 見出し = 朱(チュートリアルだと分かる)
	const ImU32 INK_TEXT = IM_COL32( 50, 34, 22, 255);

	const float corner = disp.y * CORNER_RATIO;
	const float panelW = disp.x * PANEL_W_RATIO;
	const float wrapW  = panelW - corner * INSET_SIDE * 2.0f;
	const float hpx = disp.y * HEAD_RATIO, tpx = disp.y * TEXT_RATIO;
	ImFont* hf = DebugUI::FontJPFor(hpx);
	ImFont* tf = DebugUI::FontJPFor(tpx);

	// 行ごとに折り返して高さを測る(改行 \n で区切った各行)
	std::vector<std::string> lines;
	for (const char* p = tip; ; )
	{
		const char* e = strchr(p, '\n');
		lines.emplace_back(p, e ? e : p + strlen(p));
		if (!e) break;
		p = e + 1;
	}
	float textH = 0.0f;
	for (const std::string& l : lines) textH += tf->CalcTextSizeA(tpx, FLT_MAX, wrapW, l.c_str()).y * LINE_SPACING;
	const float panelH = corner * INSET_TOP + hpx * HEAD_GAP + textH + corner * INSET_BOTTOM;
	const ImVec2 pa(disp.x * PANEL_RIGHT_RATIO - panelW, disp.y * PANEL_Y_RATIO);
	const ImVec2 pb(pa.x + panelW, pa.y + panelH);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	const float x = pa.x + corner * INSET_SIDE;
	float y = pa.y + corner * INSET_TOP;
	dl->AddText(hf, hpx, ImVec2(x, y), INK_HEAD, (const char*)u8"チュートリアル");
	y += hpx * HEAD_GAP;
	for (const std::string& l : lines)
	{
		dl->AddText(tf, tpx, ImVec2(x, y), INK_TEXT, l.c_str(), nullptr, wrapW);
		y += tf->CalcTextSizeA(tpx, FLT_MAX, wrapW, l.c_str()).y * LINE_SPACING;
	}
}

//--- 研ぎの角度計(チュートリアルだけ): 0..GRIND_ANGLE_MAX の横棒。緑 = 研げる角度(効率 > 0)、白い印 = 今の角度。
void SceneForge::DrawGrindAngleMeter()
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float X0_RATIO = 0.38f, X1_RATIO = 0.62f, Y_RATIO = 0.68f, H_RATIO = 0.016f, LABEL_RATIO = 0.024f;
	const float x0 = disp.x * X0_RATIO, x1 = disp.x * X1_RATIO, y = disp.y * Y_RATIO, h = disp.y * H_RATIO;
	auto lerpX = [&](float a) { return x0 + (x1 - x0) * fminf(fmaxf(a / GRIND_ANGLE_MAX, 0.0f), 1.0f); };
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), IM_COL32(20, 16, 12, 200), 4.0f);
	const ImU32 green = IM_COL32(90, 180, 90, 230);
	dl->AddRectFilled(ImVec2(lerpX(GRIND_IDEAL_ANGLE - GRIND_ANGLE_TOL), y), ImVec2(lerpX(GRIND_IDEAL_ANGLE + GRIND_ANGLE_TOL), y + h), green, 4.0f);
	const float mx = lerpX(fabsf(m_grindAngle));
	const float MARK_OVER = 0.6f;	// 印が棒からはみ出す量(棒の高さ比)
	dl->AddLine(ImVec2(mx, y - h * MARK_OVER), ImVec2(mx, y + h * (1.0f + MARK_OVER)), IM_COL32(255, 255, 255, 255), 3.0f);
	const float lpx = disp.y * LABEL_RATIO;
	ImFont* f = DebugUI::FontJPFor(lpx);
	const char* label = (const char*)u8"研ぐ角度";
	const ImVec2 sz = f->CalcTextSizeA(lpx, FLT_MAX, 0.0f, label);
	dl->AddText(f, lpx, ImVec2((x0 + x1 - sz.x) * 0.5f, y - lpx * 1.4f), IM_COL32(255, 236, 196, 240), label);
}

void SceneForge::DrawUI()
{
	// 配置/材質/炭火/カメラの編集はすべて SCENE_STAGE_EDITOR に移設。
	// ゲーム側は焼き込み済みの値で表示するだけ(F1はクリーン)。

	switch (m_state)
	{
	case GAME_TITLE:  DrawTitleUI();  break;
	case GAME_PLAY:
		// タイトル→金床へカメラが移っている間は何も出さない(ロゴは既に消えている。映像だけを見せる)。
		if (m_introPhase != IntroPhase::CameraMove) DrawPlayUI();
		if (m_paused) DrawPauseMenu();
		break;
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
		// 衝突の可視化は折り畳みの中に隠さず最上段に置く(開いてすぐ見つかる様に)。F1を閉じても表示は残る。
		ImGui::Checkbox("Show collision", &m_showCollision);
		ImGui::SameLine();
		ImGui::Checkbox("Show grass map", &m_showGrassMap);	// 草の踏み跡の貼图(真上から正射影)を小窓に表示
		// 【デバッグ】工程へ直接飛ぶ(前の工程は済ませた状態で、その工位から)。名前は英語の状態キー(F1 の字体に日本語が無い)
		if (m_state == GAME_PLAY && m_recipe)
		{
			const int n = (int)m_recipe->steps.size();
			if (m_debugJumpIdx >= n) m_debugJumpIdx = n - 1;
			char cur[32]; sprintf_s(cur, "%d: %s", m_debugJumpIdx + 1, StepKey(m_recipe->steps[m_debugJumpIdx].type));
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::BeginCombo("##jumpstep", cur))
			{
				for (int i = 0; i < n; ++i)
				{
					char item[32]; sprintf_s(item, "%d: %s", i + 1, StepKey(m_recipe->steps[i].type));
					if (ImGui::Selectable(item, i == m_debugJumpIdx)) m_debugJumpIdx = i;
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::Button("Jump to step")) DebugJumpToStep(m_debugJumpIdx);

			// 区域ごとの進捗(鍛造の形 / 研ぎの刃)。済んでいない区域は赤=「どこが残っているか」を確かめる(99% で止まる調査用)
			const ImVec4 DONE_COL(0.6f, 0.9f, 0.6f, 1.0f), TODO_COL(1.0f, 0.45f, 0.4f, 1.0f);
			const char* FACE_NAME[ForgingSim::NSIDES] = { "front", "back " };
			for (int side = 0; side < ForgingSim::NSIDES; ++side)
			{
				ImGui::Text("forge %s:", FACE_NAME[side]);
				for (int s = 0; s < ForgingSim::NSEG; ++s)
				{
					ImGui::SameLine();
					ImGui::TextColored(m_forging.SegDoneOf(side, s) ? DONE_COL : TODO_COL, "%3.0f", m_forging.SegProgOf(side, s) * 100.0f);
				}
			}
			// 長手セルごとの進捗(打撃が実際に書く値)。左=ローカル長手 0 側。今ハンマーの頭がどのセル位置か(head)も出す=端に届くかの確認
			ImGui::Text("head at cell %.2f / %d", StrikeLenCoord(), ForgingSim::NL);
			for (int side = 0; side < ForgingSim::NSIDES; ++side)
			{
				ImGui::Text("cells %s:", FACE_NAME[side]);
				for (int i = 0; i < ForgingSim::NL; ++i)
				{
					ImGui::SameLine(0.0f, 2.0f);
					ImGui::TextColored(m_forging.CellDoneOf(side, i) ? DONE_COL : TODO_COL, "%2.0f", fminf(m_forging.CellProgOf(side, i) * 100.0f, 99.0f));	// 2桁に収める(済=緑)
				}
			}
			for (int side = 0; side < ForgingSim::NSIDES; ++side)
			{
				ImGui::Text("grind %s:", FACE_NAME[side]);
				for (int s = 0; s < ForgingSim::NSEG; ++s)
				{
					ImGui::SameLine();
					ImGui::TextColored(m_forging.SharpDoneOf(side, s) ? DONE_COL : TODO_COL, "%3.0f", m_forging.SharpRatioOf(side, s) * 100.0f);
				}
			}
		}
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
			ImGui::TextDisabled("-- Hot steel (form must read while glowing) --");
			ImGui::SliderFloat("Hot shade", &m_wpHotShade, 0.0f, 1.0f, "%.2f");	// 背光面の明るさ(1=旧の平らな発光)
			ImGui::SliderFloat("Rim",       &m_wpRimK,     0.0f, 2.0f, "%.2f");	// 縁の明るさ
			ImGui::SliderFloat("Rim power", &m_wpRimPow,   1.0f, 8.0f, "%.1f");	// 縁へ寄る鋭さ
			ImGui::SliderFloat("Hot gain",  &m_wpHotGain,  0.3f, 1.5f, "%.2f");	// 発光全体の明るさ(白飛び防止)
			ImGui::TextDisabled("-- Forge scale (black oxide, knocked off by hammering) --");
			ImGui::SliderFloat("Scale tiling",  &m_scaleTiling,  1.0f, 40.0f, "%.1f");	// 大=細かい皮
			ImGui::SliderFloat("Scale soft",    &m_scaleSoft,    0.01f, 0.3f, "%.2f");	// 剥がれ際のぼかし
			ImGui::SliderFloat("Scale opacity", &m_scaleOpacity, 0.0f, 1.0f, "%.2f");	// 黒皮の濃さ
			ImGui::SliderFloat("Scale glow",    &m_scaleGlow,    0.0f, 1.0f, "%.2f");	// 熱い時の皮の鈍い光
			ImGui::SliderFloat("Scale start bare", &m_scaleStart, 0.0f, 0.8f, "%.2f");	// 叩く前から地金が見える量(大=斑)
			ImGui::SliderFloat("Scale hold",       &m_scaleHoldMax, 0.3f, 1.0f, "%.2f");	// 未完成の区域に残す皮(小=多く残る)
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
			ImGui::TextDisabled("-- Camera choreography --");
			ImGui::SliderFloat("Tongs lean",  &m_tongsLean,  0.0f, 0.6f, "%.2f");	// 火钳へ体を寄せる割合
			ImGui::SliderFloat("Grip dolly",  &m_gripDolly,  0.0f, 0.7f, "%.2f");	// 夹む時に刃へ寄る割合
			ImGui::SliderFloat("Grip return", &m_gripLambda, 1.0f, 15.0f, "%.1f");	// 元の視点へ戻る速さ
			ImGui::TextDisabled("-- Hammer set down --");
			ImGui::SliderFloat3("Stow offset", m_hammerStowOff, -1.5f, 1.5f, "%.2f");	// 置いた位置(構えからのずれ)
			ImGui::SliderFloat("Stow tilt",   &m_hammerStowTilt, -3.1416f, 3.1416f, "%.2f");	// 寝かせる角度
		}

		// --- Stations: 炉/砥石/水槽の工位カメラと刃の置き位置、砥石の手感 ---
		if (ImGui::CollapsingHeader("Stations"))
		{
			ImGui::TextDisabled("-- Station camera (hearth / grindstone / trough) --");
			ImGui::SliderFloat("Cam distance", &m_stationCamDist,   0.3f, 3.0f, "%.2f");	// 作業点から手前へ
			ImGui::SliderFloat("Cam height",   &m_stationCamHeight, 0.0f, 2.0f, "%.2f");	// 作業点からの目の高さ
			ImGui::SliderFloat("Look lift",    &m_stationLookLift, -0.5f, 0.5f, "%.2f");	// 注視点の高さ補正
			ImGui::TextDisabled("-- Blade placement --");
			ImGui::SliderFloat("Hearth lift",  &m_hearthLift,  -0.3f, 0.3f, "%.3f");	// 炭床の上の高さ(差し込んだ切っ先の高さ)
			ImGui::SliderAngle("Hearth angle", &m_hearthYaw,    0.0f, 80.0f);			// 炭床の長辺から奥へ振る角(平らに寝かせたまま)
			ImGui::SliderFloat("Hearth tip side",  &m_hearthTipSide,  -0.8f, 0.8f, "%.2f");	// 先端の位置: 炭床の中心から長辺方向
			ImGui::SliderFloat("Hearth tip depth", &m_hearthTipDepth, -0.5f, 0.5f, "%.2f");	// 先端の位置: 炭床の中心から奥へ
			ImGui::SliderFloat("Grind gap",    &m_grindLift,   -0.05f, 0.05f, "%.3f");	// 刃の最低点と砥石上端の隙間(高さ自体は自動)
			ImGui::SliderInt("Hint after mistakes", &m_hintAfterMistakes, 1, 10);	// 研ぎ: 同じ誤りを何回くり返したら動く案内を出すか
			ImGui::SliderFloat("Trough hover", &m_troughHover,  0.0f, 1.0f, "%.3f");	// 水面の上に構える高さ
			// 炉/水槽の正面(視点の側)。自動判定が外れた時だけ ON(次に工位へ入った時から反映)
			ImGui::Checkbox("Hearth front flip", &m_hearthFrontFlip); ImGui::SameLine();
			ImGui::Checkbox("Trough front flip", &m_troughFrontFlip);
			if (ImGui::SliderAngle("Grind view", &m_grindViewYaw, -180.0f, 180.0f)	// 砥石の固定視点の向き(0/180=輪の両側)
			    && m_station == Station::Grindstone && !m_walkMode)
				m_stationViewDir = GrindViewDir();	// 研磨中ならその場で反映
			ImGui::TextDisabled("-- Grindstone feel --");
			ImGui::SliderFloat("Pedal impulse", &m_wheel.pedalImpulse, 0.5f, 6.0f,  "%.2f");	// 1回踏んだ時の加速
			ImGui::SliderFloat("Wheel max",     &m_wheel.maxSpeed,     2.0f, 30.0f, "%.1f");	// 最高回転
			ImGui::SliderFloat("Wheel friction",&m_wheel.friction,     0.05f, 2.0f, "%.2f");	// 空転の減速
			ImGui::SliderFloat("Blade drag",    &m_wheel.bladeDrag,    0.0f, 3.0f,  "%.2f");	// 押し当ての減速
			ImGui::SliderFloat("Slide sens",    &m_grindSens,          0.0002f, 0.005f, "%.4f");	// マウス→刃の滑り
			ImGui::Text("Wheel speed %.2f   Blade U %.2f", m_wheel.Speed01(), m_grindU);
			ImGui::Text("Bevel angle %.0f deg (ideal %.0f)  efficiency %.2f  side %s", XMConvertToDegrees(m_grindAngle),
			            XMConvertToDegrees(GRIND_IDEAL_ANGLE), GrindAngleEfficiency(), GrindSide() == 0 ? "front" : "back");	// 研ぎ角の確認(F1 だけ)
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
			ImGui::SliderFloat("Strikes per face", &m_forging.strikesPerFace, 4.0f, 40.0f, "%.0f");
			ImGui::SliderFloat("Grind sec per side", &m_forging.grindSecondsPerSide, 3.0f, 60.0f, "%.0f s");	// 片側の刃を研ぎ上げるのに要る有効な研ぎ時間
			ImGui::SliderFloat("Grind spread", &m_forging.grindSpread, 0.3f, 3.0f, "%.2f cells");	// 砥石の当たる幅(長手)	// 1面を仕上げるのに要る有効な打撃の回数(毎打ちょうど 1/この数 を減らす)
			ImGui::SliderFloat("Strike spread",&m_forging.strikeSpread, 0.5f, 4.0f, "%.2f cells");	// 一打の成形が長手に広がる幅(小=打った所だけ/大=旧の区域に近い)
			ImGui::SliderFloat("Impact flash time",   &m_impactFlashTime,   0.05f, 1.0f, "%.2f s");	// 打った所が光っている時間
			ImGui::SliderFloat("Impact flash heat",   &m_impactFlashHeat,   0.0f,  0.6f, "%.2f");	// 光る所の見かけの温度の上乗せ
			ImGui::SliderFloat("Impact flash spread", &m_impactFlashSpread, 0.3f,  3.0f, "%.2f cells");
		}

		// --- Walk / Player: 一人称の走動 ---
		if (ImGui::CollapsingHeader("Walk / Player"))
		{
			ImGui::Text(m_walkMode ? "mode: WALK (press E to enter station)"
			                       : "mode: STATION (forging)");
			ImGui::SliderFloat("Walk speed",  &m_walkSpeed,    0.5f,  8.0f,   "%.2f");	// 移動速度(単位/秒)
			ImGui::SliderFloat("Station move speed", &m_transSpeed,  0.5f, 6.0f, "%.2f");	// 工位への移動アニメの速さ(1..2秒に収まる)
			ImGui::SliderFloat("Exit step back",     &m_exitStepBack, 0.0f, 2.0f, "%.2f");	// 退出時に金床から下がる距離
			ImGui::SliderFloat("Interact look pad",  &m_lookPad,      0.0f, 0.5f, "%.2f");	// 視線判定の箱の膨らみ(大=狙いやすい)
			ImGui::Text("interact focus: %s", m_focus >= 0 ? INTERACTABLES[m_focus].propKey : "-");
			ImGui::SliderFloat("Mouse sens",  &m_walkSens,     0.0006f, 0.0050f, "%.4f");	// 視角感度(低=重い)
			ImGui::SliderFloat("Pitch limit", &m_walkPitchLim, 0.3f,  1.55f,  "%.2f");	// 上下の振り切り制限(rad)
			ImGui::SliderFloat("Eye height",  &m_walkEyeH,     0.8f,  2.2f,   "%.2f");	// 目線の高さ
		}

		// --- Collision: 走動の衝突(玩家の円 × 道具の凸包) ---
		if (ImGui::CollapsingHeader("Collision"))
		{
			ImGui::SliderFloat("Player radius", m_player.RadiusPtr(), 0.1f, 0.6f, "%.2f");	// 足元の円(大=道具から遠くで止まる)
			ImGui::SliderFloat("Wall slice low",  &m_wallSliceLowHeight, 0.05f, 1.0f, "%.2f");	// 一番低い切り口(床から)。ここから背丈まで刻みごとに切る
			ImGui::SliderFloat("Wall slice step", &m_wallSliceStep,      0.05f, 1.0f, "%.2f");	// 切り口の間隔
			ImGui::Text("colliders: %d hulls / %d wall segments", (int)m_collision.hulls.size(), (int)m_collision.segments.size());
			ImGui::Text("door: %s", m_door.IsOpen() ? "open" : "closed");
			ImGui::SliderFloat("Door open angle", &m_door.openAngle, 0.5f, 2.2f, "%.2f");	// 開いた時の角度(rad)
			ImGui::SliderFloat("Door swing time", &m_door.swingTime, 0.3f, 3.0f, "%.2f");	// 開閉にかかる秒(大=重い扉)
			ImGui::TextDisabled("cyan = hull / orange = wall / red = touching / yellow = player");
		}

		// --- Carry: 鉄の運搬(火钳で掴んで歩く)。見た目の位置合わせ ---
		if (ImGui::CollapsingHeader("Carry"))
		{
			ImGui::Text(m_carrying ? "holding the iron" : "hands free (tongs on the left hip)");
			ImGui::TextDisabled("-- held iron (first-person viewmodel, camera-relative) --");
			ImGui::SliderFloat3("Grip point (R/U/F)", m_gripOff, -1.0f, 1.5f, "%.2f");	// 火钳が鉄を挟む点(カメラから 右/上/前)
			ImGui::SliderFloat("Iron yaw (right)",  &m_carryYaw,   -1.5f, 1.5f, "%.2f");	// 鉄を視線から右へ振る角
			ImGui::SliderFloat("Iron pitch (up)",   &m_carryPitch, -1.0f, 1.0f, "%.2f");	// 鉄を上へ起こす角
			ImGui::SliderFloat("Grip along iron",   &m_gripAlong,   0.0f, 1.0f, "%.2f");	// 挟む位置(手前の端から何割)
			ImGui::SliderFloat3("Tongs base (R/U/F)", m_tongsBase, -1.5f, 1.0f, "%.2f");	// 火钳の柄の根元(画面の下の外)
			ImGui::SliderFloat("Sway follow", &m_vmSwayLambda, 2.0f, 40.0f, "%.1f");	// 視点を振った時の追従の速さ(小=重く遅れる)
			ImGui::TextDisabled("-- wall-clipping avoidance (purple ray in Show collision) --");
			ImGui::Text("pull now %.2f m   raise now %.0f deg", m_carryPull, XMConvertToDegrees(m_carryRaise));
			ImGui::SliderFloat("Avoid max pull", &m_carryAvoidMaxPull, 0.0f, 0.8f, "%.2f");	// まず手元へ引き寄せる量の上限(m)
			ImGui::SliderAngle("Avoid max raise", &m_carryAvoidMaxRaise, 0.0f, 85.0f);		// 起こす角の上限
			ImGui::SliderFloat("Avoid margin",    &m_carryAvoidMargin,  0.0f, 0.4f, "%.2f");	// 道具/壁の手前に空ける隙間
			ImGui::SliderFloat("Avoid follow",    &m_carryAvoidLambda,  1.0f, 30.0f, "%.1f");	// 起こす/戻す速さ
			ImGui::TextDisabled("-- sequence feel (grip / put-down animation) --");
			ImGui::SliderFloat("Hand lag",     &m_seqHandLag,    0.0f, 0.5f, "%.2f");	// 目が先、手が遅れる割合(フォロースルー/オーバーラップ)
			ImGui::SliderFloat("Arc lift",     &m_seqArcLift,    0.0f, 0.8f, "%.2f");	// 運ぶ時の弧の高さ(水平距離に対する比。アーク)
			ImGui::SliderFloat("Time jitter",  &m_seqTimeJitter, 0.0f, 0.3f, "%.2f");	// 拍の長さのばらつき ±
			ImGui::SliderFloat("Arc jitter",   &m_seqArcJitter,  0.0f, 0.8f, "%.2f");	// 弧の高さのばらつき ±
			ImGui::TextDisabled("-- walking while carrying --");
			ImGui::SliderFloat("Carry walk speed x", &m_carrySpeedMul, 0.2f, 1.0f, "%.2f");	// 運んでいる時の速さの倍率
			ImGui::SliderFloat("Bob up/down",        &m_bobAmp,        0.0f, 0.06f, "%.3f");	// 上下の揺れ幅
			ImGui::SliderFloat("Bob side",           &m_bobSideAmp,    0.0f, 0.04f, "%.3f");	// 左右の揺れ幅
			ImGui::SliderFloat("Bob steps per meter",&m_bobPerMeter,   0.5f, 4.0f,  "%.2f");	// 1m で何歩=揺れの細かさ
			ImGui::SliderFloat("Tongs scale",   &m_tongsScale,   0.3f, 3.0f, "%.2f");	// 台上の火钳に対する大きさ
			ImGui::TextDisabled("-- hammer on the right hip (when not at the anvil) --");
			ImGui::SliderFloat3("Hammer belt (R/H/F)", m_hammerHipOff, -1.0f, 1.5f, "%.2f");	// 腰帯の輪: 体から 右/床からの高さ/前
			ImGui::SliderFloat("Hammer belt yaw",      &m_hammerHipYaw, -3.15f, 3.15f, "%.2f");	// 垂れた柄の軸まわりの向き
			ImGui::SliderFloat3("Hip (L/height/F)", m_hipOff, -0.5f, 1.6f, "%.2f");	// 腰の火钳(体から 左/床からの高さ/前)
		}

		// --- Title: 標題カメラの取景(タイトル画面で開いて動かすと即反映。Save tuning で保存) ---
		if (ImGui::CollapsingHeader("Title"))
		{
			ImGui::TextDisabled(m_state == GAME_TITLE ? "(on the title screen: changes show live)"
			                                          : "(go back to the title screen to see it)");
			// 走動中などで「この画がいい」と思った所で押す=今のカメラの位置と注視点をそのまま標題カメラにする。
			//   (F1 を開くと入力は止まるので、開いた瞬間の画がそのまま取れる。Save tuning で保存)
			if (ImGui::Button("Use current view as title camera"))
				if (CameraBase* cam = GetObj<CameraBase>("Camera"))
				{
					XMFLOAT3 cp = cam->GetPos(), cl = cam->GetLook();
					m_titleCamPos[0]  = cp.x; m_titleCamPos[1]  = cp.y; m_titleCamPos[2]  = cp.z;
					m_titleCamLook[0] = cl.x; m_titleCamLook[1] = cl.y; m_titleCamLook[2] = cl.z;
				}
			ImGui::SliderFloat3("Title cam pos",  m_titleCamPos,  -8.0f, 8.0f, "%.2f");	// 標題カメラの位置
			ImGui::SliderFloat3("Title cam look", m_titleCamLook, -8.0f, 8.0f, "%.2f");	// 注視点
			// SPACE 後に金床へ移る秒数。上限 5 秒: 運鏡中も鉄は冷めるので、長すぎると START_HEAT(0.85)が
			// 燃える温度(0.80)を下回り、最初の「加熱」工程が即完了しなくなる(0.85-0.80=0.05 / 冷却 0.008/秒 ≒ 6秒)。
			ImGui::SliderFloat("Intro time", &m_titleIntroTime, 0.5f, 5.0f, "%.1f");
			ImGui::SliderFloat("Logo fade time", &m_logoFadeTime, 0.2f, 3.0f, "%.1f");	// SPACE 後にロゴが消える秒数(カメラはその後)
		}

		// --- Grass: インタラクティブ草(玩家を避けて倒れ、離れると戻る) ---
		if (ImGui::CollapsingHeader("Grass"))
		{
			ImGui::SliderFloat("Stamp radius",  &m_grassStampRadius,       0.2f, 2.0f, "%.2f");	// 足元で押し分ける円(world)
			ImGui::SliderFloat("Lean",          &m_grassLean,              0.0f, 1.5f, "%.2f");	// 横へ倒れる量(草丈比)
			ImGui::SliderFloat("Press down",    &m_grassPress,             0.0f, 1.0f, "%.2f");	// 足元で沈む量(草丈比)
			ImGui::SliderFloat("Recover time",  &m_grassMap.recoverTime,   0.1f, 5.0f, "%.2f");	// 戻る速さ(秒。大=ゆっくり戻る)
			ImGui::Text("map: %s", m_grassMap.IsReady() ? "ready" : "NOT READY (no StOutdoorGround / shader)");
		}

		ImGui::End();
	}
	DrawGrassMapPreview();	// 草の踏み跡の貼图(F1 のチェックで ON。F1 を閉じて歩きながらも見られる)

	m_fade.Draw();	// 最後に全画面の黒幕(前景層)を重ねる=遷移の淡入淡出
}


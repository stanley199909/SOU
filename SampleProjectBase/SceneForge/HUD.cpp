// Part of SceneForge, split out of the original single SceneForge.cpp.
// This file carries a UTF-8 BOM so the Japanese comments moved from the
// original source keep compiling correctly under MSVC (no C2601/C1075).
// All SceneForge members share the class declaration in SceneForge.h and the
// file-local helpers declared in SceneForge_Internal.h.
#include "CottageRender.h"
#include "LitRender.h"
#include "ReflectionProbe.h"
#include "VolumetricLight.h"
#include "AutoExposure.h"
#include "GpuProfiler.h"
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
#include "InputActions.h"	// 裏返しの「押している」表示(Action::Flip)
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

//--- 「PRESS [ボタンの絵] TO START」の様に、文の途中に今の機器のボタンの絵を挟んで中央に出す(CenterText と同じ見た目)。
//    絵は InputPrompts が選ぶ = キーボードなら Enter/Space、手柄なら A。文字だけの「SPACE」は手柄の人に嘘になる。
static void CenterTextWithIcons(const char* before, InputGlyph glyph, const char* after, float yRatio, float scale,
                                ImU32 col, ImFont* font)
{
	const float ICON_SCALE = 1.35f;		// 絵の大きさ(文字の高さに対する比)。ボタンの絵は余白があるので文字より少し大きく
	const float ICON_GAP   = 0.15f;		// 絵と絵の間(絵の大きさに対する比)
	const float SHADOW_PX  = 2.0f;
	const int   MAX_ICONS  = 4;		// 1つのボタンの絵の最大枚数(キーボードの決定 = Enter + Space の2枚)
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImFont* f = font ? font : ImGui::GetFont();
	const float px = f->FontSize * scale;
	const char* icons[MAX_ICONS] = {};
	const int   n = InputPrompts::Icons(glyph, icons, MAX_ICONS);
	const ImVec2 sb = f->CalcTextSizeA(px, FLT_MAX, 0.0f, before);
	const ImVec2 sa = f->CalcTextSizeA(px, FLT_MAX, 0.0f, after);
	const float icon = sb.y * ICON_SCALE, gap = icon * ICON_GAP;
	const float total = sb.x + n * icon + (n > 0 ? (n - 1) * gap : 0.0f) + sa.x;
	float x = (disp.x - total) * 0.5f;
	const float cy = disp.y * yRatio;
	const int   a  = (int)((col >> IM_COL32_A_SHIFT) & 0xFF);
	DrawSubtitleBox(dl, x, cy - icon * 0.5f, x + total, cy + icon * 0.5f, sb.y, a / 255.0f);	// 3D のタイトル画面の上でも読める様に
	const ImU32 shadowCol = IM_COL32(0, 0, 0, (int)(a * 0.6f));
	auto text = [&](const char* t, const ImVec2& sz)
	{
		const ImVec2 p(x, cy - sz.y * 0.5f);
		dl->AddText(f, px, ImVec2(p.x + SHADOW_PX, p.y + SHADOW_PX), shadowCol, t);
		dl->AddText(f, px, p, col, t);
		x += sz.x;
	};
	text(before, sb);
	for (int i = 0; i < n; ++i)
	{
		if (i > 0) x += gap;
		std::shared_ptr<Texture> tex = TextureCache::Get((std::string(icons[i]) + ".png").c_str());
		if (tex && tex->GetResource())
		{
			ImTextureID id = (ImTextureID)tex->GetResource();
			const ImVec2 p0(x, cy - icon * 0.5f), p1(x + icon, cy + icon * 0.5f);
			dl->AddImage(id, ImVec2(p0.x + SHADOW_PX, p0.y + SHADOW_PX), ImVec2(p1.x + SHADOW_PX, p1.y + SHADOW_PX), ImVec2(0, 0), ImVec2(1, 1), shadowCol);
			dl->AddImage(id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a));
		}
		x += icon;
	}
	text(after, sa);
}

//--- このフレームにマウスが動いたか(メニューのホバーで選び直してよいか)。
static bool MouseMovedThisFrame()
{
	const ImVec2 d = ImGui::GetIO().MouseDelta;
	return d.x != 0.0f || d.y != 0.0f;
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

//--- 横長の紙片を 3スライス(three-slice)で貼る: 左右の端(焦げた縁)は縦横比を保ったまま、真ん中だけを横に伸ばす。
//    = 文の長さが変わっても端の飾りが歪まない(温度ゲージの鋲と同じ手法)。端の幅は「画像の高さに対する比」で持つ。
static const float STRIP_END_SRC_RATIO = 0.6f;	// 端(伸ばさない部分)の幅 = 画像の高さ × これ
static void DrawPaperStrip(ImDrawList* dl, Texture* tex, ImVec2 a, ImVec2 b, int alpha)
{
	if (!tex || !tex->GetResource()) return;
	const float tw = (float)tex->GetWidth(), th = (float)tex->GetHeight();
	const float srcEnd = th * STRIP_END_SRC_RATIO;
	const float end    = fminf(srcEnd * (b.y - a.y) / th, (b.x - a.x) * 0.5f);	// 画面上の端の幅(縦の倍率と同じ倍率=歪まない)
	const float xs[4] = { a.x, a.x + end, b.x - end, b.x };
	const float us[4] = { 0.0f, srcEnd / tw, 1.0f - srcEnd / tw, 1.0f };
	ImTextureID id = (ImTextureID)tex->GetResource();
	for (int i = 0; i < 3; ++i)
		dl->AddImage(id, ImVec2(xs[i], a.y), ImVec2(xs[i + 1], b.y), ImVec2(us[i], 0.0f), ImVec2(us[i + 1], 1.0f), IM_COL32(255, 255, 255, alpha));
}

//--- 紙片の上に墨で1行書く(画面中央揃え)。紙の大きさは文の大きさから決める = 文が長くても短くても余白が同じ。
//    明るい炉の前でも、暗い紙の上の白い字でなく「明るい紙の上の濃い墨」で読ませる(工程リスト・一時停止メニューと同じ見た目)。
//    紙の画像が無ければ、前と同じ暗い帯 + 明るい字で代わりに描く(読めなくはならない)。
static const float STRIP_H_PER_TEXT = 2.1f;	// 紙の高さ = 文字の高さ × これ(破れた縁の分、上下に余裕が要る)
static const float STRIP_PAD_PER_H  = 0.85f;	// 文の左右の余白 = 紙の高さ × これ(端の焦げた縁に字が掛からない様に)
static const ImU32 STRIP_INK        = IM_COL32(60, 38, 22, 255);	// 墨(工程リストの見出しと同じ)
static float StripHeightPx(float textPx) { return textPx * STRIP_H_PER_TEXT; }
//--- 紙片の中心を画面の任意の点に置く版(工程の札が工程リストへ飛ぶ時にも使う)。下の DrawTextOnStrip はこれの「画面の横中央」版。
static void DrawTextOnStripAt(Texture* strip, const char* text, ImVec2 center, float textPx, ImFont* font, float alpha01)
{
	if (!text || !text[0]) return;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImFont* f = (font == DebugUI::FontJP()) ? DebugUI::FontJPFor(textPx) : (font ? font : ImGui::GetFont());
	const ImVec2 ts = f->CalcTextSizeA(textPx, FLT_MAX, 0.0f, text);
	const float h = StripHeightPx(textPx), w = ts.x + h * STRIP_PAD_PER_H * 2.0f;
	const ImVec2 a(center.x - w * 0.5f, center.y - h * 0.5f), b(a.x + w, a.y + h);
	const int alpha = (int)(255 * alpha01);
	const ImVec2 tp(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
	if (strip && strip->GetResource())
	{
		DrawPaperStrip(dl, strip, a, b, alpha);
		dl->AddText(f, textPx, tp, (STRIP_INK & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT), text);
	}
	else
	{
		const int BAND_ALPHA = 150;
		dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, (int)(BAND_ALPHA * alpha01)), h * 0.5f);
		dl->AddText(f, textPx, tp, IM_COL32(255, 236, 196, alpha), text);
	}
}

static void DrawTextOnStrip(Texture* strip, const char* text, float cyRatio, float textPx, ImFont* font, float alpha01)
{
	const ImVec2 disp = ImGui::GetIO().DisplaySize;
	DrawTextOnStripAt(strip, text, ImVec2(disp.x * 0.5f, disp.y * cyRatio), textPx, font, alpha01);
}

//--- 選択パネルの寸法(画面高さ比)。パネルの高さを外から計算できる様にファイルの先頭側で持つ(下に説明の紙片を置く時の配置)
static const float CHOICE_CORNER_RATIO = 0.085f;	// 隅の大きさ=工程リストと同じ
static const float CHOICE_ROW_RATIO    = 0.075f;	// 項目の間隔
static const float CHOICE_HEAD_GAP     = 1.3f;		// 見出しから1項目目まで(行単位)
static const float CHOICE_INSET_TOP    = 0.95f, CHOICE_INSET_BOTTOM = 0.75f;	// 枠の飾りの内側(隅の大きさ比)=工程リストと同じ
static float ChoicePanelHeightRatio(int n)
{
	return CHOICE_CORNER_RATIO * (CHOICE_INSET_TOP + CHOICE_INSET_BOTTOM) + CHOICE_ROW_RATIO * (CHOICE_HEAD_GAP + n);
}

//--- タイトルの選択パネル + その下の説明の紙片(2026-10-14: 説明が羊皮紙に隠れて読めなかった → 紙片に分けて、必ずパネルの下に置く)。
//    紙片の位置(DESC_Y)は固定し、パネルは「紙片の上に収まる」高さまで上げる(項目が多くても重ならない)。
static const float TITLE_DESC_Y       = 0.90f;	// 説明の紙片の中心(画面高さ比)
static const float TITLE_DESC_SCALE   = 0.8f;	// 説明の文字(游明朝の焼き寸に対する倍率)
static const float TITLE_DESC_GAP     = 0.015f;	// パネルと紙片の間(画面高さ比)
static float TitlePanelCenterY(int n, float preferredCenterY)
{
	const float dispH  = ImGui::GetIO().DisplaySize.y;
	const float descPx = DebugUI::FontJP()->FontSize * TITLE_DESC_SCALE;
	const float stripTop = TITLE_DESC_Y - StripHeightPx(descPx) * 0.5f / dispH;
	const float fitCenter = stripTop - TITLE_DESC_GAP - ChoicePanelHeightRatio(n) * 0.5f;
	return fminf(preferredCenterY, fitCenter);	// 元の位置で収まるなら動かさない
}

//--- 主人公の独白 = 字幕(サブタイトル)の見た目: 半透明の暗い角丸の箱 + 細い縁取り(アウトライン)の字。
//    紙片(DrawTextOnStrip)=「ゲームが今やる事を教える」/ 字幕 =「主人公が喋っている」。見た目を分けて、誰の言葉かを一目で分からせる。
//    暗い箱の上なので、色分け(冷たい=青 / 熱すぎ=赤 / 済んだ所=黄)が炉や石壁の前でも沈まない(2026-10-14 試遊: 青い字が読めなかった)。
static const float SUBTITLE_PAD_X_PER_TEXT = 0.9f;	// 箱の左右の余白 = 文字の高さ × これ
static const float SUBTITLE_PAD_Y_PER_TEXT = 0.35f;	// 箱の上下の余白 = 文字の高さ × これ
static const float SUBTITLE_ROUND_PER_TEXT = 0.3f;	// 角の丸み = 文字の高さ × これ
static const int   SUBTITLE_BOX_ALPHA      = 165;		// 箱の濃さ(0..255)。背景が少し透けて、世界を塞がない
static const float SUBTITLE_OUTLINE_PX     = 1.5f;		// 字の縁取りの太さ(px)
static const int   SUBTITLE_OUTLINE_ALPHA  = 200;		// 縁取りの濃さ
//--- 字幕の箱: 中身の矩形(x0,y0)-(x1,y1)の外側に余白を足した、半透明の暗い角丸の箱。余白と丸みは文字の高さから決める
void DrawSubtitleBox(ImDrawList* dl, float x0, float y0, float x1, float y1, float textPx, float alpha01)
{
	const float px = textPx * SUBTITLE_PAD_X_PER_TEXT, py = textPx * SUBTITLE_PAD_Y_PER_TEXT;
	dl->AddRectFilled(ImVec2(x0 - px, y0 - py), ImVec2(x1 + px, y1 + py),
	                  IM_COL32(0, 0, 0, (int)(SUBTITLE_BOX_ALPHA * alpha01)), textPx * SUBTITLE_ROUND_PER_TEXT);
}

//--- 縁取りの字: 同じ字を暗い色で 8 方向に少しずらして下に敷き、その上に本体(シェーダ無しの安価な縁取り)。縁取りの濃さは字の淡出に合わせる
void DrawOutlinedText(ImDrawList* dl, ImFont* f, float textPx, float x, float y, unsigned int col, const char* text)
{
	const float a01 = ((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
	const ImU32 outline = IM_COL32(0, 0, 0, (int)(SUBTITLE_OUTLINE_ALPHA * a01));
	const float o = SUBTITLE_OUTLINE_PX;
	static const float DIRS[8][2] = { {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1} };
	for (const auto& d : DIRS) dl->AddText(f, textPx, ImVec2(x + d[0] * o, y + d[1] * o), outline, text);
	dl->AddText(f, textPx, ImVec2(x, y), col, text);
}

//--- 1行の字幕を画面中央に(主人公の独白・翻面の状況説明・過熱の警告)
static void DrawSubtitle(const char* text, float cyRatio, float textPx, ImFont* font, ImU32 col)
{
	if (!text || !text[0]) return;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImFont* f = (font == DebugUI::FontJP()) ? DebugUI::FontJPFor(textPx) : (font ? font : ImGui::GetFont());
	const ImVec2 ts = f->CalcTextSizeA(textPx, FLT_MAX, 0.0f, text);
	const float a01 = ((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;	// 字の淡出に箱も合わせる
	const ImVec2 tp((disp.x - ts.x) * 0.5f, disp.y * cyRatio - ts.y * 0.5f);
	DrawSubtitleBox(dl, tp.x, tp.y, tp.x + ts.x, tp.y + ts.y, textPx, a01);
	DrawOutlinedText(dl, f, textPx, tp.x, tp.y, col, text);
}

//--- 操作説明を1行に並べる: [アイコン…] 一言 　[アイコン…] 一言 …(画面中央揃え)。
//    アイコンは Kenney Input Prompts(CC0)の白い画像。TextureCache で一度だけ読み、以後は使い回す。
//    明るい炉の前でも沈まない様に、黒く染めた同じ画像を少しずらして下に敷く(影)。
void SceneForge::DrawKeyHints(const KeyHint* hints, int count, float yRatio, float alpha, bool alignRight)
{
	const float RIGHT_MARGIN_RATIO = 0.03f;	// 右揃えの時の画面右端からの余白(画面幅比)
	// 64px 版(Default)。画面上は約45px(1080p)=ほぼ等倍。128px 版だと約2.8倍の縮小になり、ミップマップの平均でぼやけた。
	// 絵は今の機器(キーボード・マウス / 手柄)に合わせて InputPrompts が選ぶ(パスも InputPrompts が持つ)。
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

	// 0) 各説明のアイコンを今の機器で引く(WASD=4枚 / 左スティック=1枚 の様に、機器で枚数が変わる)
	const int MAX_HINTS = 8;
	const char* icons[MAX_HINTS][KeyHint::MAX_ICONS] = {};
	int nIcons[MAX_HINTS] = {};
	if (count > MAX_HINTS) count = MAX_HINTS;
	for (int h = 0; h < count; ++h)
		nIcons[h] = InputPrompts::Icons(hints[h].glyph, icons[h], KeyHint::MAX_ICONS);

	// 1) 全体の幅を測って中央揃えの開始位置を決める
	float total = 0.0f;
	for (int h = 0; h < count; ++h)
	{
		const int ni = nIcons[h];
		total += icon * ni + disp.y * ICON_GAP_RATIO * (ni - 1) + disp.y * LABEL_GAP_RATIO;
		total += jp->CalcTextSizeA(px, FLT_MAX, 0.0f, hints[h].label).x;
		if (h + 1 < count) total += disp.y * HINT_GAP_RATIO;
	}
	float x = alignRight ? disp.x * (1.0f - RIGHT_MARGIN_RATIO) - total : (disp.x - total) * 0.5f;
	const float cy = disp.y * yRatio;
	DrawSubtitleBox(dl, x, cy - icon * 0.5f, x + total, cy + icon * 0.5f, px, alpha);	// 行全体の下に字幕の箱(3D の上でも読める)

	// 2) 描く
	for (int h = 0; h < count; ++h)
	{
		for (int k = 0; k < nIcons[h]; ++k)
		{
			if (k > 0) x += disp.y * ICON_GAP_RATIO;
			std::shared_ptr<Texture> tex = TextureCache::Get((std::string(icons[h][k]) + ".png").c_str());
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
//--- 温度ゲージが今「どの温度の幅」を見せるか。温度が意味を持つ工程の時だけ true。
//    加熱の工程は「次の工程のための加熱」なので、次の工程(鍛造/焼入れ)の幅を見せる。
//    研ぎは温度と関係ない → 緑の帯を出さず、ゲージは薄くする(2026-10-15 試遊: 研ぎ中に濃く出ていて「温度を管理する工程」と誤解された)。
bool SceneForge::HeatGaugeZone(float& lo, float& hi, StepName* forStep) const
{
	if (!m_recipe || m_stepIdx < 0 || m_stepIdx >= (int)m_recipe->steps.size()) return false;
	StepName t = m_recipe->steps[m_stepIdx].type;
	if (t == StepName::Heat && m_stepIdx + 1 < (int)m_recipe->steps.size()) t = m_recipe->steps[m_stepIdx + 1].type;
	if (forStep) *forStep = t;
	switch (t)
	{
	case StepName::Forge:  lo = IDEAL_MIN;       hi = IDEAL_MAX;       return true;	// 叩ける温度
	case StepName::Quench: lo = QUENCH_MIN_TEMP; hi = QUENCH_MAX_TEMP; return true;	// 焼きが入る温度
	default:               return false;
	}
}

//--- 今いる所で、玩家が温度を見て判断するか(=ゲージを濃く出す)。
//    炉 = いつ出すか / 金床(鍛造の工程)= 叩けるか / 水槽(焼入れの工程)= 入れてよいか。
//    歩いている・移動の演出中・その工程と関係ない工位では使わない → 薄くする(2026-10-17 試遊: 使わない時に出ていると誤解を招く)。
//    消さないのは、歩いている間に鉄が冷めていくのが見える様に(次に炉へ戻るかの判断材料)。
bool SceneForge::HeatGaugeInUse() const
{
	StepName step;
	float lo, hi;
	if (m_walkMode || Transitioning() || !HeatGaugeZone(lo, hi, &step)) return false;
	switch (m_station)
	{
	case Station::Hearth: return true;
	case Station::Anvil:  return step == StepName::Forge;
	case Station::Trough: return step == StepName::Quench;
	default:              return false;
	}
}

//--- 色見本(焼き戻しの色の並び)。温度ゲージと同じ位置・大きさの帯に、TEMPER_COLOR_TABLE(刃の shader と同じ表)の色を左→右へ。
//    良い範囲(麦わら〜茶)を枠で囲む。指針は出さない: 今の色は刃を見て、この見本と見比べて判断する(現実の鍛冶の色見本と同じ使い方)。
void SceneForge::DrawTemperChart()
{
	const bool inUse = !m_walkMode && !Transitioning() && m_station == Station::Hearth;	// 炉で焼き戻している時は濃く
	m_gaugeAlpha = Lerp::Damp(m_gaugeAlpha, inUse ? 1.0f : GAUGE_IDLE_ALPHA, inUse ? GAUGE_FADE_IN_LAMBDA : GAUGE_FADE_OUT_LAMBDA, ImGui::GetIO().DeltaTime);
	const int   ga   = (int)(255 * m_gaugeAlpha);
	const ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float tw = disp.x * HEAT_TRACK_W_RATIO, th = disp.y * HEAT_TRACK_H_RATIO;	// 温度ゲージの槽と同じ大きさ・位置
	const float x0 = (disp.x - tw) * 0.5f, y0 = disp.y * HEAT_TRACK_Y_RATIO, y1 = y0 + th;
	auto X = [&](float t) { return x0 + tw * t; };
	auto Col = [&](int k) {
		return IM_COL32((int)(255 * TEMPER_COLOR_TABLE[k][1]), (int)(255 * TEMPER_COLOR_TABLE[k][2]), (int)(255 * TEMPER_COLOR_TABLE[k][3]), ga);
	};
	// 色の帯: 隣り合う2色の間を横のグラデーションで
	for (int k = 0; k < TEMPER_KEYS - 1; ++k)
		dl->AddRectFilledMultiColor(ImVec2(X(TEMPER_COLOR_TABLE[k][0]), y0), ImVec2(X(TEMPER_COLOR_TABLE[k + 1][0]), y1), Col(k), Col(k + 1), Col(k + 1), Col(k));
	const ImU32 INK = IM_COL32(240, 225, 200, ga);
	const float LINE_W = 2.0f, FRAME_PAD_RATIO = 0.3f;	// 枠の太さ(px) / 良い範囲の枠を帯からはみ出させる量(帯の高さ比)
	dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + tw, y1), INK, 0.0f, 0, LINE_W);
	const float pad = th * FRAME_PAD_RATIO;
	dl->AddRect(ImVec2(X(TEMPER_IDEAL_MIN), y0 - pad), ImVec2(X(TEMPER_IDEAL_MAX), y1 + pad), IM_COL32(150, 235, 150, ga), 0.0f, 0, LINE_W * 1.5f);	// 良い範囲(温度ゲージの緑と同じ色)

	// 文字: 左に「刃先の色」、帯の下に色の名前、良い範囲の下に「ここで取り出す」
	const float LABEL_RATIO = 0.024f, NAME_RATIO = 0.018f, LABEL_GAP = 0.6f, NAME_GAP = 0.4f;	// 文字の高さ(画面高さ比) / 間隔(文字の高さ比)
	const float lpx = disp.y * LABEL_RATIO, npx = disp.y * NAME_RATIO;
	ImFont* lf = DebugUI::FontJPFor(lpx);
	ImFont* nf = DebugUI::FontJPFor(npx);
	const char* label = (const char*)u8"刃先の色";
	const ImVec2 ls = lf->CalcTextSizeA(lpx, FLT_MAX, 0.0f, label);
	DrawOutlinedText(dl, lf, lpx, x0 - lpx * LABEL_GAP - ls.x, (y0 + y1) * 0.5f - ls.y * 0.5f, INK, label);
	struct Name { float t; const char* text; };
	static const Name NAMES[] = {	// 色の名前を置く位置(表の位置と同じ。紫と青の間は少し詰まるので青は淡い青の位置)
		{ 0.00f, (const char*)u8"銀" }, { 0.275f, (const char*)u8"麦わら" }, { 0.45f, (const char*)u8"茶" },
		{ 0.60f, (const char*)u8"紫" }, { 0.82f, (const char*)u8"青" },
	};
	for (const Name& nm : NAMES)
	{
		const ImVec2 ns = nf->CalcTextSizeA(npx, FLT_MAX, 0.0f, nm.text);
		const float nx = fminf(fmaxf(X(nm.t) - ns.x * 0.5f, x0), x0 + tw - ns.x);	// 帯の端からはみ出さない
		DrawOutlinedText(dl, nf, npx, nx, y1 + pad + npx * NAME_GAP, INK, nm.text);
	}
}

void SceneForge::DrawHeatGauge()
{
	float zoneLo = 0.0f, zoneHi = 0.0f;
	const bool hasZone = HeatGaugeZone(zoneLo, zoneHi);	// 温度が関係ない工程(研ぎ)は緑の帯なし。ゲージは他の「使わない所」と同じく薄く出す(HeatGaugeInUse)
	const bool gaugeInUse = HeatGaugeInUse();
	m_gaugeAlpha = Lerp::Damp(m_gaugeAlpha, gaugeInUse ? 1.0f : GAUGE_IDLE_ALPHA, gaugeInUse ? GAUGE_FADE_IN_LAMBDA : GAUGE_FADE_OUT_LAMBDA, ImGui::GetIO().DeltaTime);
	const int ga = (int)(255 * m_gaugeAlpha);	// ゲージ全体(画像・文字)に掛ける不透明度
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
		const float TRACK_W_RATIO = HEAT_TRACK_W_RATIO;		// 槽の幅(画面幅比)。課の「ゲージを指す」と共有
		const float TRACK_H_RATIO = HEAT_TRACK_H_RATIO;		// 槽の高さ(画面高さ比)。外框の倍率はこれで決まる(縦横同倍率=鋲が歪まない)
		const float TRACK_Y_RATIO = HEAT_TRACK_Y_RATIO;		// 槽の上端(画面高さ比)
		const float MARKER_H_RATIO = 0.040f;	// 指針の高さ(画面高さ比)。heat_gauge_marker_ui.png はこの2倍(1080p)で縮小済み
		const float MARKER_DIP     = 0.35f;		// 指針の先を槽へ食い込ませる量(槽の高さに対する比)
		const ImU32 TINT = IM_COL32(255, 255, 255, ga);

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
		if (hasZone) DrawZone(m_gaugeIdeal.get(), zoneLo, zoneHi, IDEAL_V_TOP, IDEAL_V_BOTTOM);	// 今の工程に合う温度の幅(鍛造 / 焼入れ)
		DrawZone(m_gaugeOver.get(),  OVERHEAT,  1.0f,      OVER_V_TOP,  OVER_V_BOTTOM);

		DrawFrame(overlay);	// ③ 金の縁が②の端を覆う

		// 何のゲージか一目で分かる様に、左に「鉄の温度」(説明なしでは温度と分からなかった。2026-10-15 試遊)
		{
			const float LABEL_RATIO = 0.024f, LABEL_GAP = 0.6f;	// 文字の高さ(画面高さ比) / 外框の左端との間(文字の高さ比)
			const float lpx = disp.y * LABEL_RATIO;
			ImFont* f = DebugUI::FontJPFor(lpx);
			const char* label = (const char*)u8"鉄の温度";
			const ImVec2 sz = f->CalcTextSizeA(lpx, FLT_MAX, 0.0f, label);
			const float lyc = (ty0 + ty1) * 0.5f;
			DrawOutlinedText(dl, f, lpx, lx0 - lpx * LABEL_GAP - sz.x, lyc - sz.y * 0.5f, IM_COL32(240, 225, 200, ga), label);
		}

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
			dl->AddText(f, lpx, ImVec2(cxg - sz.x * 0.5f, fy1 + lpx * GAP), IM_COL32(150, 235, 150, ga), label);
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
	// 手動リセット(Shift+Delete 長押し)の確認。メニューの開閉に関係なく少しだけ出す
	if (m_time - m_progressResetNoteAt < PROGRESS_RESET_NOTE_SEC)
	{
		const float NOTE_Y     = 0.95f;	// 画面の一番下(メニューや PRESS の文と重ならない)
		const float NOTE_SCALE = 0.7f;
		const float a01 = 1.0f - (m_time - m_progressResetNoteAt) / PROGRESS_RESET_NOTE_SEC;
		CenterText((const char*)u8"チュートリアルの記録をリセットしました", NOTE_Y, NOTE_SCALE,
		           IM_COL32(255, 255, 255, (int)(255 * a01)), DebugUI::FontJP());
	}
	// チュートリアルの課程の一覧(○ = まだ / ✓ = 済)。最後の行 = 戻る
	if (m_courseSelectOpen && m_recipe)
	{
		const float PANEL_CENTER_Y = 0.66f;
		const int nCourse = (int)m_recipe->courses.size();
		std::vector<const char*> labels;
		std::vector<int>         marks;
		for (int i = 0; i < nCourse; ++i)
		{
			labels.push_back(m_recipe->courses[i].label);
			marks.push_back((i < (int)m_courseDone.size() && m_courseDone[i]) ? 2 : 1);
		}
		labels.push_back((const char*)u8"戻る");
		marks.push_back(0);
		const int clicked = DrawChoicePanel((const char*)u8"チュートリアル", labels.data(), (int)labels.size(), m_courseSel,
		                                    TitlePanelCenterY((int)labels.size(), PANEL_CENTER_Y), false, marks.data());
		if (clicked >= 0) m_courseRequest = clicked;	// 実行は UpdateTitle
		const char* desc = (m_courseSel < nCourse) ? m_recipe->courses[m_courseSel].desc : (const char*)u8"モードの選択へ戻る";
		DrawTextOnStrip(m_uiStrip.get(), desc, TITLE_DESC_Y, DebugUI::FontJP()->FontSize * TITLE_DESC_SCALE, DebugUI::FontJP(), 1.0f);
		DrawMenuButtonGuide();
		return;
	}
	// 開始プロンプトは緩やかに明滅させて「操作可能」を伝える
	if (m_modeSelectOpen)
	{
		static const char* LABELS[(int)ModeItem::Count] = { (const char*)u8"チュートリアル", (const char*)u8"通常モード", (const char*)u8"ゲームを終了する" };
		static const char* DESCS[(int)ModeItem::Count]  = { (const char*)u8"はじめての方へ。工程を一つずつ選んで、操作を順に覚えます", (const char*)u8"案内は最小限。自分の目と耳で鍛冶を進めます",
		                                                    (const char*)u8"ゲームを閉じます" };
		const float PANEL_CENTER_Y = 0.70f;
		const int clicked = DrawChoicePanel((const char*)u8"モードを選ぶ", LABELS, (int)ModeItem::Count, m_modeSel,
		                                    TitlePanelCenterY((int)ModeItem::Count, PANEL_CENTER_Y), false);
		if (clicked >= 0) m_modeRequest = clicked;	// 実行は UpdateTitle
		DrawTextOnStrip(m_uiStrip.get(), DESCS[m_modeSel], TITLE_DESC_Y, DebugUI::FontJP()->FontSize * TITLE_DESC_SCALE, DebugUI::FontJP(), 1.0f);
		DrawMenuButtonGuide();
		return;
	}
	float p = (0.6f + 0.4f * sinf(m_time * 3.0f)) * alpha;
	CenterTextWithIcons("PRESS  ", Action::Confirm, "  TO  START", 0.86f, 1.15f, IM_COL32(255, 255, 255, (int)(255 * p)), body);
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
	if (Tempering()) DrawTemperChart();	// 焼き戻し: 温度でなく色で判断する工程=温度ゲージの代わりに色見本
	else            DrawHeatGauge();

	// 工程リスト(左)と、走動中に次に向かう点の目印(3D の上)
	DrawStepTracker();
	DrawObjectiveMarker();
	if (m_tutorial && AtStation(Station::Grindstone) && !m_clearDecided) DrawGrindAngleMeter();

	// 案内文(宏観チュートリアル=「今何をするか」)。工程と状況で変わる(UpdateGuide)。
	//   基本の文言は配方(GameData/WeaponRecipe)が持つ=換武器で自動的に差し替わる。
	//   文が変わった瞬間は淡入+少し下から浮かせる=「変わった」ことに気付かせる(読み落とし防止)。
	{
		const char* text = (m_guideText && m_guideText[0]) ? m_guideText : CurrentStep().instruction;
		text = InputPrompts::Format(text);	// {Flip} 等 → 今の機器の呼び名(「Fキー」/「Yボタン」)
		const float FADE_IN_SEC   = 0.6f;	// 淡入にかける秒
		const float RISE_RATIO    = 0.015f;	// 淡入中に浮き上がる量(画面高さ比)
		const float TEXT_Y        = 0.10f;	// 文の縦位置(画面高さ比)
		const float TEXT_SCALE    = 1.0f;	// 游明朝の焼き寸(30px)に対する倍率
		float t = fminf((m_time - m_guideChangedAt) / FADE_IN_SEC, 1.0f);
		if (t < 0.0f) t = 0.0f;
		// 紙片の上に墨で書く(工程リスト・メニューと同じ羊皮紙の見た目に揃える。2026-10-14 ユーザーと合意)。紙ごと淡入して浮き上がる
		float y = TEXT_Y + RISE_RATIO * (1.0f - t);
		DrawTextOnStrip(m_uiStrip.get(), text, y, jp->FontSize * TEXT_SCALE, jp, t);
	}
	DrawStepBanner();	// 工程が進んだ瞬間の札(案内の紙片より手前。工程リストへ飛んで行く)

	// 翻面の子状態ごとの操作ヒント(宏観チュートリアル)。運鏡ビート中は状況説明、
	// 回している間は「押し続ける/離す」で何が起きるかを明示する。
	const ImU32 BEAT_COL  = IM_COL32(255, 230, 180, 220);	// 運鏡中の状況説明(少し淡く)
	const float FLIP_Y = 0.24f, FLIP_SCALE = 0.85f;
	switch (m_flipPhase)
	{
	case FlipPhase::Holding:
	{
		switch (CurrentFlipBeat())
		{
		case FlipBeat::Tongs: DrawSubtitle((const char*)u8"火ばさみを手に取る…", FLIP_Y, jp->FontSize * FLIP_SCALE, jp, BEAT_COL); break;
		case FlipBeat::Grip:  DrawSubtitle((const char*)u8"刃を掴んでいる…",     FLIP_Y, jp->FontSize * FLIP_SCALE, jp, BEAT_COL); break;
		default: break;
		}
		// 押した長さの輪(F + 溜まる輪。離すと減る)。チュートリアルで未習得の間は案内(DrawLessonPrompts)が同じ物を描くので重ねない。
		if (!LessonDrawsFlip()) DrawFlipHoldPrompt(1.0f, 1.0f, IM_COL32(255, 255, 255, 255), (const char*)u8"長押しで裏返す");
		break;
	}
	case FlipPhase::PutBack:  DrawSubtitle((const char*)u8"火ばさみを戻している…", FLIP_Y, jp->FontSize * FLIP_SCALE, jp, BEAT_COL); break;
	default: break;
	}

	// ※KCD2式: 画面中心の準心は「置かない」。第一人称に固定十字は不自然で、しかも屏幕中央に
	//   死んでいて動かせない。狙いの提示は「動くハンマー＋刃の高亮段」で行う(下の WeaponRender)。

	// 過熱の警告(点滅)
	if (m_forging.Heat() > OVERHEAT)
	{
		float p = 0.5f + 0.5f * sinf(m_time * 12.0f);
		const float OVERHEAT_Y = 0.20f, OVERHEAT_SCALE = 1.3f;
		DrawSubtitle((const char*)u8"！！　過熱　！！", OVERHEAT_Y, jp->FontSize * OVERHEAT_SCALE, jp, IM_COL32(255, 70, 50, (int)(150 + p * 105)));
	}

	// 打撃フィードバックのポップアップ(鉄条の上でフェード)
	if (m_popupLife > 0.0f)
	{
		float a = m_popupLife / POPUP_LIFE;
		if (a > 1.0f) a = 1.0f;
		unsigned int c = (m_popupCol & 0x00FFFFFF) | ((unsigned int)(a * 255) << 24);
		const float POPUP_Y = 0.36f, POPUP_SCALE = 1.15f;
		DrawSubtitle(m_popupText, POPUP_Y, jp->FontSize * POPUP_SCALE, jp, c);	// 主人公の独白 = 字幕の見た目(游明朝)
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
	//   場所ごとの表(データ)。表には「動作」を書く=キー割り当て(InputActions)を変えても、手柄に持ち替えても、ここは直さない。
	static const KeyHint WALK_HINTS[] = {
		{ AxisPrompt::Move,       (const char*)u8"移動" },
		{ AxisPrompt::Look,       (const char*)u8"見回す" },
		{ Action::Interact,       (const char*)u8"使う" },
		{ Action::Grab,           (const char*)u8"掴む" },
	};
	static const KeyHint ANVIL_HINTS[] = {
		{ AxisPrompt::Aim,        (const char*)u8"狙う" },
		{ Action::Use,            (const char*)u8"長押し：ハンマー" },
		{ Action::Flip,           (const char*)u8"長押し：裏返す" },
		{ Action::Grab,           (const char*)u8"掴んで運ぶ" },
		{ Action::Interact,       (const char*)u8"離れる" },
	};
	static const KeyHint HEARTH_HINTS[] = {
		{ Action::Bellows,        (const char*)u8"長押し：ふいごで風を送る" },
		{ Action::Grab,           (const char*)u8"掴んで出す" },
		{ Action::Interact,       (const char*)u8"離れる" },
	};
	static const KeyHint TEMPER_HINTS[] = {	// 同じ炉の工位でも、焼き戻しの工程は刃を炭の上で動かす
		{ AxisPrompt::HandHorizontal, (const char*)u8"左右：炭の上で刃を動かす" },
		{ Action::Bellows,        (const char*)u8"長押し：ふいご(速く色が進む)" },
		{ Action::Grab,           (const char*)u8"掴んで出す" },
		{ Action::Interact,       (const char*)u8"離れる" },
	};
	static const KeyHint GRIND_HINTS[] = {
		{ Action::Pedal,          (const char*)u8"連打：ペダル" },
		{ Action::GrindPress,         (const char*)u8"長押し：刃を当てる" },
		{ AxisPrompt::HandHorizontal, (const char*)u8"左右：滑らせる" },
		{ AxisPrompt::TiltVertical,   (const char*)u8"上下：刃の角度" },
		{ Action::Flip,           (const char*)u8"長押し：裏返す" },
		{ Action::Grab,           (const char*)u8"掴んで運ぶ" },
		{ Action::Interact,       (const char*)u8"離れる" },
	};
	static const KeyHint TROUGH_HINTS[] = {
		{ Action::Use,            (const char*)u8"水に沈める" },
		{ Action::Grab,           (const char*)u8"掴んで運ぶ" },
		{ Action::Interact,       (const char*)u8"離れる" },
	};
	static const KeyHint TESTCUT_HINTS[] = {	// 試し切り: 脚は固定、構えて集中し、離して一太刀
		{ AxisPrompt::Look,       (const char*)u8"狙う" },
		{ Action::Use,            (const char*)u8"長押し：構える／離す：斬る" },
		{ Action::Interact,       (const char*)u8"離れる" },
	};
	static const KeyHint QUENCH_STIR_HINTS[] = {
		{ AxisPrompt::HandVertical, (const char*)u8"上下：刃を揺する" },
	};
	const KeyHint* hints = WALK_HINTS; int nHints = _countof(WALK_HINTS);
	if (!m_walkMode && !Transitioning())
	{
		switch (m_station)
		{
		case Station::Anvil:      hints = ANVIL_HINTS;  nHints = _countof(ANVIL_HINTS);  break;
		case Station::Hearth:
			if (Tempering()) { hints = TEMPER_HINTS; nHints = _countof(TEMPER_HINTS); }
			else             { hints = HEARTH_HINTS; nHints = _countof(HEARTH_HINTS); }
			break;
		case Station::Grindstone: hints = GRIND_HINTS;  nHints = _countof(GRIND_HINTS);  break;
		case Station::Trough:     hints = TROUGH_HINTS; nHints = _countof(TROUGH_HINTS); break;
		case Station::TestCut:    hints = TESTCUT_HINTS; nHints = _countof(TESTCUT_HINTS); break;
		}
		if (QuenchStirring()) { hints = QUENCH_STIR_HINTS; nHints = _countof(QUENCH_STIR_HINTS); }	// 淬火中: 離れられない=揺するだけ
	}
	const float GUIDE_Y = 0.93f;		// 操作ガイドの縦位置(画面高さ比)
	// 今その操作ができない間は出さない(出すと「押せば効く」と誤解させる。ユーザー指摘 2026-10-07):
	//   翻面中(翻面の操作は上の専用の案内が出る) / 拍子表の再生中(掴む・置くの演出) / 走動⇔工位の移動中
	const bool inputLocked = (m_flipPhase != FlipPhase::None) || SequencePlaying() || Transitioning();
	if (!inputLocked) DrawKeyHints(hints, nHints, GUIDE_Y);
	DrawLessonPrompts();	// 操作の案内(チュートリアル=順に教える / 救急=迷った時だけ。全工程で同じ絵・同じ音)
	if (m_grindFlipping && AtStation(Station::Grindstone) && !LessonDrawsGrindFlip())	// 研ぎの裏返し: 金床と同じ「F + 押した長さの輪」(課が描いている時は重ねない)
		DrawHoldProgressPrompt(Action::Flip, m_grindFlipHold / GRIND_FLIP_SEC, InputActions::Held(Action::Flip),
		                       (const char*)u8"長押しで裏返す", 1.0f, 1.0f, IM_COL32(255, 255, 255, 255));

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
	struct SubBar { const char* name; float prog; bool active; int segDone = -1; };	// segDone = 仕上がった区域の数(研ぎ: 棒を格子に分ける)。-1 = 1本の棒
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
		// 両側の刃(Fキー長押しで裏返して研ぐ面を変える)。今研いでいる面を濃く
		auto segsDone = [&](int side) { int n = 0; for (int s = 0; s < ForgingSim::NSEG; ++s) n += m_forging.SharpDoneOf(side, s) ? 1 : 0; return n; };
		subs[nSub++] = { (const char*)u8"表", m_forging.SharpProgress(0), GrindSide() == 0, segsDone(0) };
		subs[nSub++] = { (const char*)u8"裏", m_forging.SharpProgress(1), GrindSide() == 1, segsDone(1) };
	}
	else if (nowType == StepName::Quench)
		subs[nSub++] = { (const char*)u8"冷", QuenchProgress(), true };	// 冷え切った(焼きが入った)割合

	// 済んでいないバーは満タンに見せない: 進捗は区域の平均なので、1区域だけ少し足りなくても 99% = 見た目は満タンになり、
	// 「全部終わったのに進まない(99% で止まる)」と見えた(2026-10-08)。全区域が済んだ時だけ 1、それ以外は UNFINISHED_MAX まで。
	// 鍛造/研ぎは対象外: 残り作業量の減算(WorkField)で、バーは「済んだ作業 / 全作業」そのもの=満タン=完成が保証される。
	// 焼入れも対象外: 進みは区域の平均でなく1つの連続量(冷えた割合)で、100% で必ず終わる。90% で止めると「8〜9割で終わった」と見えた(2026-10-14)
	const float UNFINISHED_MAX = 0.9f;
	if (nowType != StepName::Forge && nowType != StepName::Grind && nowType != StepName::Quench)
		for (int k = 0; k < nSub; ++k)
			if (subs[k].prog < 1.0f) subs[k].prog = fminf(subs[k].prog, UNFINISHED_MAX);

	// チュートリアルの課程では、その課程の工程だけを並べる(他の工程は関係無い)
	int lo = 0, hi = (int)m_recipe->steps.size();
	if (m_courseIdx >= 0 && m_courseIdx < (int)m_recipe->courses.size())
	{
		lo = m_recipe->courses[m_courseIdx].firstStep;
		hi = m_recipe->courses[m_courseIdx].lastStep + 1;
	}
	const int   n      = hi - lo;
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
	for (int i = lo; i < hi; ++i, cy += row)
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
		if (now)	// 工程の札の飛び先 = 今の工程名の中心
		{
			const ImVec2 ls = JP(px)->CalcTextSizeA(px, FLT_MAX, 0.0f, label);
			m_trackerNowPos = XMFLOAT2(tp.x + ls.x * 0.5f, tp.y + ls.y * 0.5f);
		}
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
			// 今作業している面の左に ▶(2026-10-22 試遊「表と裏が分からない」)。裏返すと ▶ も次の行へ移る。
			//   工程の ●/○ と形を変える=「もう1つ工程がある」と読まれない。面が2つある工程(鍛造/研ぎ)だけ。
			//   左へ引いて元の位置へ戻る動きをずっと繰り返す=動く物に目が行く(ユーザー要望)。
			if (nSub >= ForgingSim::NSIDES && subs[k].active)
			{
				const float ARROW_X       = 0.85f;	// ▶ の左端(動いていない時): 文字の左へ文字の高さのこの割合
				const float ARROW_W       = 0.55f;	// ▶ の幅(文字の高さに対する比)
				const float ARROW_H       = 0.30f;	// ▶ の高さの半分(同)
				const float ARROW_BOB     = 0.45f;	// 左へ引く距離(文字の高さに対する比)
				const float ARROW_BOB_SEC = 0.9f;	// 左へ引いて戻るまでの1往復の秒
				const float phase = m_time * XM_2PI / ARROW_BOB_SEC;
				const float pull  = 0.5f - 0.5f * cosf(phase);	// 0(元の位置) → 1(一番左) → 0。両端で緩やかに折り返す
				const float x0 = sp.x - spx * (ARROW_X + ARROW_BOB * pull), cyA = sp.y + spx * 0.5f;
				dl->AddTriangleFilled(ImVec2(x0, cyA - spx * ARROW_H), ImVec2(x0 + spx * ARROW_W, cyA), ImVec2(x0, cyA + spx * ARROW_H), sc);
			}
			float bx0 = sp.x + spx + disp.y * BAR_GAP_RATIO, bx1 = bx0 + disp.x * BAR_W_RATIO;
			float bh  = disp.y * BAR_H_RATIO, by = sp.y + (spx - bh) * 0.5f;
			if (subs[k].segDone < 0)	// 1本の棒(鍛造/焼入れ)
			{
				dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), BAR_BG_COL);
				dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * subs[k].prog, by + bh), sc);
				dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + bh), BAR_EDGE_COL, 0.0f, 0, BAR_EDGE_W);
				continue;
			}
			// 区域の数の格子(研ぎ): 仕上がった区域の数だけ左から埋まる。次の格子だけ、残りの区域の進みを途中まで見せる(止まって見えない様に)。
			//   格子は「何個済んだか」だけで、刃のどこが残っているかは示さない(どこを研ぐかは刃の見た目で分かる=微観の指示を出さない)
			const int   NCELL = ForgingSim::NSEG;
			const float CELL_GAP_RATIO = 0.18f;	// 格子の間の隙間(格子1つの幅に対する比)
			const float cw  = (bx1 - bx0) / NCELL;
			const float gap = cw * CELL_GAP_RATIO;
			const float partial = fminf(fmaxf(subs[k].prog * NCELL - subs[k].segDone, 0.0f), 1.0f);	// 残りの区域の進みの合計(1格子分まで)
			for (int c = 0; c < NCELL; ++c)
			{
				const float x0 = bx0 + cw * c + gap * 0.5f, x1 = bx0 + cw * (c + 1) - gap * 0.5f;
				const float fill = (c < subs[k].segDone) ? 1.0f : (c == subs[k].segDone ? partial : 0.0f);
				dl->AddRectFilled(ImVec2(x0, by), ImVec2(x1, by + bh), BAR_BG_COL);
				if (fill > 0.0f) dl->AddRectFilled(ImVec2(x0, by), ImVec2(x0 + (x1 - x0) * fill, by + bh), sc);
				dl->AddRect(ImVec2(x0, by), ImVec2(x1, by + bh), BAR_EDGE_COL, 0.0f, 0, BAR_EDGE_W);
			}
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
			// 緑帯に入った: 叩けるが、もっと熱い方が長く叩ける → もう少し待たせる(取り出すかは玩家の自由。行き先はまだ炉)
			if (heat < FORGE_FULL_TEMP) { goal = Station::Hearth; return (const char*)u8"叩ける温度になった。もっと熱いほど多く叩ける。もう少し待とう"; }
			goal = Station::Anvil;
			return (const char*)u8"十分に熱くなった。金床へ運ぶ";
		}
		if (heat < MinWorkTemp(st.type)) { goal = Station::Hearth; return (const char*)u8"鉄が冷めた。炉で熱し直す"; }
		if (m_forging.SideDone(m_forging.Side()) && !m_forging.BothSidesDone())
			return (const char*)u8"この面は仕上がった。{Flip}長押しで裏返す";
		return st.instruction;

	case StepName::Grind:
	{
		// 「Fキーで裏返す」は、今研いでいる面が仕上がっていて、もう片方が残っている時だけ(鍛造と同じ判断)。
		//   裏返した後(今の面がまだ)は普段の案内に戻る=文が変わるので案内は淡出→淡入する(2026-10-07: 裏返しても消えなかった)。
		const float DONE = 1.0f;
		const int  side = GrindSide();
		const bool thisDone  = m_forging.SharpProgress(side)     >= DONE;
		const bool otherDone = m_forging.SharpProgress(1 - side) >= DONE;
		if (thisDone && !otherDone) return (const char*)u8"この面の刃は仕上がった。{Flip}長押しで裏返す";
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
	// 工程リストが画面に出ていない間(タイトル→金床の導入カメラ中)は、済の状態だけ合わせて音もアニメも出さない。
	//   開始時は鉄が熱い(START_HEAT)ので最初の「加熱」は1フレーム目で自動で済になる。玩家がした事ではなく、
	//   リストも見えていないのに鉛筆の音だけが鳴った(2026-10-16 F5)。リストが出た時には線が引き終わっている。
	const bool silent = !PlayHudVisible();
	bool struck = false, erased = false;
	for (int i = 0; i < n; ++i)
	{
		TrackerRow& row = m_trackerRows[i];
		const bool now = RowCompleted(i, row.completed);
		if (now != row.completed)
		{
			row.completed = now;
			if (silent) { row.changedAt = TrackerRow{}.changedAt; continue; }	// 既定(ずっと前)=線は引き終わった状態
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
	label = StationName(goal);
	return true;
}

const char* SceneForge::StationName(Station s)
{
	switch (s)
	{
	case Station::Anvil:      return (const char*)u8"金床";
	case Station::Hearth:     return (const char*)u8"炉";
	case Station::Grindstone: return (const char*)u8"砥石";
	case Station::Trough:     return (const char*)u8"水槽";
	case Station::TestCut:    return (const char*)u8"藁人形";
	}
	return "";
}

//--- 工程の札の文を作る: 「<済んだ工程> 完了 → 次：<次の工程>（<工位>へ）」。工程名と工位は配方(データ)から=換武器でも合う。
void SceneForge::ShowStepBanner(int prevIdx)
{
	if (!m_recipe || !PlayHudVisible()) return;	// 導入カメラ中(最初の加熱が自動で済む)は出さない
	const int n = (int)m_recipe->steps.size();
	if (prevIdx < 0 || prevIdx >= n || m_stepIdx < 0 || m_stepIdx >= n) return;
	const StepSetting& next = m_recipe->steps[m_stepIdx];
	m_bannerText  = m_recipe->steps[prevIdx].label;
	m_bannerText += (const char*)u8" 完了　→　次：";
	m_bannerText += next.label;
	m_bannerText += (const char*)u8"（";
	m_bannerText += StationName(StepStation(next.type));
	m_bannerText += (const char*)u8"へ）";
	m_bannerAt = m_time;
	Audio::Play(Audio::SE_NEXT_STEP, BANNER_SE_VOLUME);
}

//--- 札: 出る(淡入+少し大きく) → 中央に留まる → 工程リストの「今」の行へ縮みながら飛んで消える。
//    飛ぶ先は工程リストが毎フレーム書く m_trackerNowPos=工程リストの位置や大きさを変えても追従する。
void SceneForge::DrawStepBanner()
{
	const float t = m_time - m_bannerAt;
	const float total = BANNER_IN_SEC + BANNER_HOLD_SEC + BANNER_FLY_SEC;
	if (t < 0.0f || t >= total || m_bannerText.empty()) return;
	const ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImFont* jp = DebugUI::FontJP();
	const ImVec2 home(disp.x * 0.5f, disp.y * BANNER_Y_RATIO);
	ImVec2 c = home;
	float scale = 1.0f, alpha = 1.0f;
	if (t < BANNER_IN_SEC)
	{
		const float p = t / BANNER_IN_SEC;
		alpha = p;
		scale = Lerp::Linear(BANNER_IN_SCALE, 1.0f, 1.0f - (1.0f - p) * (1.0f - p));	// ease-out
	}
	else if (t >= BANNER_IN_SEC + BANNER_HOLD_SEC)
	{
		const float p = (t - BANNER_IN_SEC - BANNER_HOLD_SEC) / BANNER_FLY_SEC;
		const float e = p * p;	// ease-in: ゆっくり離れて、吸い込まれる様に速く
		c = ImVec2(Lerp::Linear(home.x, m_trackerNowPos.x, e), Lerp::Linear(home.y, m_trackerNowPos.y, e));
		scale = Lerp::Linear(1.0f, BANNER_END_SCALE, e);
		alpha = 1.0f - e;
	}
	DrawTextOnStripAt(m_uiStrip.get(), m_bannerText.c_str(), c, jp->FontSize * BANNER_TEXT_SCALE * scale, jp, alpha);
}

//--- 光らせる工位 = 目印(▼)と同じ行き先(鉄を取りに行く時は鉄の在る工位)。目印が無い時(工位の中・移動中)と、近づいた時は消す。
void SceneForge::UpdateStationHighlight(float dt)
{
	XMFLOAT3 target; const char* label = nullptr;
	bool on = GuideTarget(target, label);
	if (on)
	{
		m_hiStation = (!m_carrying && m_workAt != m_guideGoal) ? m_workAt : m_guideGoal;
		const XMFLOAT3 foot = m_player.GetPosition();
		const float dx = target.x - foot.x, dz = target.z - foot.z;
		if (dx * dx + dz * dz < STATION_HI_NEAR * STATION_HI_NEAR) on = false;	// 着いた=もう指さない
	}
	m_hiAlpha = Lerp::Damp(m_hiAlpha, on ? 1.0f : 0.0f, on ? STATION_HI_IN : STATION_HI_OUT, dt);
}

float SceneForge::StationHighlight(const std::string& propKey) const
{
	const float MIN_VISIBLE = 0.001f;
	if (m_state != GAME_PLAY || m_hiAlpha < MIN_VISIBLE || m_capturingProbe) return 0.0f;	// 遊んでいる時だけ。反射の写真には写さない(光が焼き付く)
	if (m_shotMode != ShotMode::Normal) return 0.0f;	// 撮影モード: 行き先の工位を光らせる案内も UI の一部=隠す
	const char* key = StationPropKey(m_hiStation);
	if (!key || propKey != key) return 0.0f;
	const float pulse = Lerp::Linear(STATION_HI_PULSE_MIN, 1.0f, 0.5f + 0.5f * sinf(m_time * STATION_HI_PULSE_HZ * XM_2PI));
	return m_hiAlpha * pulse;
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
	const float BLINK_MIN   = 0.15f;	// 点滅の一番淡い時の濃さ(0..1)
	float alpha = 1.0f - m_promptAlpha;	// E 提示と入れ替わりに消える
	// 同じ行き先へ WALK_BLINK_AFTER 秒着かなければ、▼と名前を一緒に点滅させる(迷っている人に気付かせる。2026-10-12 ユーザー決定)
	if (m_walkGoalTime >= WALK_BLINK_AFTER)
		alpha *= Lerp::Linear(BLINK_MIN, 1.0f, 0.5f + 0.5f * cosf((m_walkGoalTime - WALK_BLINK_AFTER) * WALK_BLINK_HZ * XM_2PI));
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

//--- 出来栄え 0..1: 形の一致度・打撃品質の平均・淬火の出来(膜を早く破ったか)・焼き戻しの色を重み合成する。
//    誤打(冷打/過熱/完成済みを叩く)は品質0の打撃として平均を下げる=罰でなく「腕前」として自然に効く。
float SceneForge::GradeScore() const
{
	// 打撃品質の平均(1打も打たずに淬火した場合は0扱い=除算回避)。
	float qAvg = (m_strikeCount > 0) ? (m_qualitySum / (float)m_strikeCount) : 0.0f;
	float s = GRADE_W_MATCH * m_match + GRADE_W_QUALITY * qAvg + GRADE_W_QUENCH * QuenchScore() + GRADE_W_TEMPER * TemperScore();
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
	// 試し切りの行がある時は、形/点の2行を少し上げて3行を紙の中に収める
	const bool  cutLine = m_cutSevered;
	const float TITLE_Y = 0.375f, GRADE_Y = 0.50f;
	const float MATCH_Y = cutLine ? 0.595f : 0.615f, SCORE_Y = cutLine ? 0.640f : 0.665f, CUT_Y = 0.685f;
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
	// 試し切り: 何太刀で胴を両断したか(鍛えた出来の証拠。等級とは別の点数にはしない)。派手な演出は後の段階
	if (cutLine)
	{
		sprintf_s(buf, sizeof(buf), "CUT IN TWO   %d STROKE%s", m_cutSeveredHits, m_cutSeveredHits == 1 ? "" : "S");
		CenterText(buf, CUT_Y, STAT_SCALE, INK_LABEL, title, false);
	}
	CenterTextWithIcons("PRESS  ", Action::Confirm, "  TO  RETURN", PROMPT_Y, PROMPT_SCALE, IM_COL32(240, 228, 205, 230), body);	// 暗い背景の上=影あり
	// 素材のクレジット(CC BY は作者名の表示が使用条件。一覧は CREDITS.txt)。目立たない様に画面下端に小さく
	const float CREDIT_Y = 0.95f, CREDIT_SCALE = 0.45f;
	CenterText("Sound: ZijunSANG, soundslikewillem (freesound.org, CC BY-NC 4.0)", CREDIT_Y, CREDIT_SCALE, IM_COL32(200, 190, 170, 170), body);
}

//--- 羊皮紙の選択パネル(一時停止メニュー / タイトルのモード選択)。工程リストと同じ見た目。
//    項目はマウスのホバーで選び、クリックで決定(戻り値)。実行は呼び出し側の Update=描画の途中で状態を変えない。寸法は画面比。
//    ホバーで選ぶのは「マウスが動いたフレーム」だけ: 止まっている光標が項目の上にあるだけで毎フレーム選び直すと、
//    手柄/キーボードで上下に動かしても次のフレームで光標の下へ戻され、操作できなかった(2026-10-14)。
int SceneForge::DrawChoicePanel(const char* head, const char* const* labels, int n, int& sel, float centerY, bool dimBack, const int* marks)
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	const ImU32 DIM_COL       = IM_COL32(0, 0, 0, 150);			// 背後の世界を暗く(止まっている事が分かる)
	const float PANEL_W_RATIO = 0.30f;							// パネルの幅(画面幅比)
	const float CORNER_RATIO  = CHOICE_CORNER_RATIO;
	const float HEAD_RATIO    = 0.040f;							// 見出しの文字の高さ
	const float ITEM_RATIO    = 0.034f;							// 項目の文字の高さ
	const float ROW_RATIO     = CHOICE_ROW_RATIO;
	const float HEAD_GAP      = CHOICE_HEAD_GAP;
	const float INSET_TOP     = CHOICE_INSET_TOP, INSET_BOTTOM = CHOICE_INSET_BOTTOM;
	const float MARK_RATIO    = 0.010f;							// 選んでいる項目の左の印(三角)の大きさ
	const float MARK_GAP      = 0.6f;							// 印と文字の間(印の大きさ比)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);		// 工程リストと同じインク
	const ImU32 INK_ITEM = IM_COL32( 60,  42,  28, 220);
	const ImU32 INK_SEL  = IM_COL32(150,  32,  18, 255);		// 選択中 = 朱(工程リストの「今」と同じ)
	const ImU32 INK_CHECK = IM_COL32( 40, 110,  45, 255);		// 済の ✓ = 緑のインク(ユーザーの絵)
	const float CHECK_R_RATIO = 0.016f;							// 右端の ○ の半径(画面高さ比)
	const float CHECK_X_INSET = 1.4f;							// ○ の中心をパネルの右端からどれだけ内へ(隅の大きさ比)
	const float CHECK_LINE_W  = 2.5f;							// ○ と ✓ の線の太さ(px)

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
	const bool   mouseMoved = MouseMovedThisFrame();
	const float  ipx   = disp.y * ITEM_RATIO;
	ImFont* f = DebugUI::FontJPFor(ipx);
	for (int i = 0; i < n; ++i, cy += row)
	{
		// 当たり判定 = パネルの幅いっぱい × 1行(文字の上だけでなく行のどこでも選べる)
		const ImVec2 ra(pa.x + corner * INSET_BOTTOM, cy - (row - ipx) * 0.5f), rb(pb.x - corner * INSET_BOTTOM, ra.y + row);
		const bool hover = mouse.x >= ra.x && mouse.x <= rb.x && mouse.y >= ra.y && mouse.y <= rb.y;
		if (hover && mouseMoved) sel = i;
		if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { sel = i; clicked = i; }

		const bool isSel = (i == sel);
		const ImVec2 sz = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, labels[i]);
		const float tx = cx - sz.x * 0.5f;
		dl->AddText(f, ipx, ImVec2(tx, cy), isSel ? INK_SEL : INK_ITEM, labels[i]);
		if (isSel)
		{
			const float m = disp.y * MARK_RATIO, my = cy + ipx * 0.5f, mx = tx - m * (1.0f + MARK_GAP);
			dl->AddTriangleFilled(ImVec2(mx - m, my - m), ImVec2(mx - m, my + m), ImVec2(mx + m * 0.6f, my), INK_SEL);
		}
		// 右端の印: ○ = まだ / ○に✓ = 済(課程の一覧)
		if (marks && marks[i] > 0)
		{
			const float r = disp.y * CHECK_R_RATIO;
			const ImVec2 c(pb.x - corner * CHECK_X_INSET, cy + ipx * 0.5f);
			dl->AddCircle(c, r, isSel ? INK_SEL : INK_ITEM, 0, CHECK_LINE_W);
			if (marks[i] == 2)
			{
				const ImVec2 pts[3] = { ImVec2(c.x - r * 0.55f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.5f), ImVec2(c.x + r * 0.9f, c.y - r * 0.8f) };	// ✓ の形(○ から右上へ少しはみ出す)
				dl->AddPolyline(pts, 3, INK_CHECK, 0, CHECK_LINE_W * 1.4f);
			}
		}
	}
	return clicked;
}

//====================================================================
//  設計者用パネル F2 = ライティング確認
//  光源を1つずつ消して「この明るさ/色はどの光から来ているか」を確かめる道具。
//  imgui の調整窓(F1)ではなく、ゲームと同じ羊皮紙+日本語で、項目を絞って描く。
//  光の強さ(調整値)は変えない=見比べる為だけの切り替え(保存しない)。
//====================================================================
void SceneForge::DrawLightPanel()
{
	LightToggles& on = LitRender::Toggles();
	// 項目の表(データ)。recapture = 反射プローブの写真に写る光 → 切り替えたら撮り直す(反射の中だけ古い光が残らない様に)
	struct Row { const char* label; bool* flag; bool recapture; };
	const Row ROWS[] = {
		{ (const char*)u8"炉の火（直接光）",       &on.fire,         true  },
		{ (const char*)u8"太陽（直接光）",         &on.sun,          true  },
		{ (const char*)u8"環境光（部屋の拡散光）", &on.ambient,      true  },
		{ (const char*)u8"環境光の向きと色（IBL）", &on.iblDirection, false },
		{ (const char*)u8"映り込み（IBL 反射）",   &on.reflections,  false },
		{ (const char*)u8"光の筋（体積光）",       &on.volumetric,   false },
		{ (const char*)u8"目の慣れ（自動露出）",   &AutoExposure::Get().enabled, false },	// 明るい窓を見ると暗く、暗い所では明るく(瞳孔)
	};
	const int NROW = (int)_countof(ROWS);
	// まとめて切り替える型(プリセット)。値の並びは ROWS と同じ
	struct Preset { const char* label; bool values[_countof(ROWS)]; };
	const Preset PRESETS[] = {
		{ (const char*)u8"すべて点ける",   { true,  true,  true,  true,  true,  true,  true  } },
		{ (const char*)u8"太陽の光だけ",   { false, true,  false, true,  false, true,  true  } },
		{ (const char*)u8"炉の火の光だけ", { true,  false, false, true,  false, false, true  } },
	};
	const int NPRESET = (int)_countof(PRESETS);

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float PANEL_W_RATIO = 0.30f;		// パネルの幅(画面幅比)。つまみの名前・棒・値が重ならない幅
	const float RIGHT_MARGIN  = 0.02f;		// 画面右端からの余白(画面幅比)
	const float TOP_RATIO     = 0.10f;		// パネル上端(画面高さ比)
	const float CORNER_RATIO  = 0.06f;		// 羊皮紙の隅の大きさ(画面高さ比)
	const float HEAD_RATIO    = 0.034f;		// 見出しの文字の高さ
	const float HINT_RATIO    = 0.020f;		// 操作の案内の文字の高さ
	const float ITEM_RATIO    = 0.022f;		// 項目の文字の高さ
	const float ROW_RATIO     = 0.038f;		// 1行の高さ
	const float SECTION_GAP   = 0.6f;		// 光源とプリセットの間(行の高さ比)
	const float INSET_X       = 0.9f;		// 文字の左右の余白(隅の大きさ比)
	const float INSET_TOP     = 0.8f, INSET_BOTTOM = 0.7f;	// 上下の余白(隅の大きさ比)
	const float DOT_R_RATIO   = 0.009f;		// 点灯の印(●/○)の半径(画面高さ比)
	const float DOT_LINE_W    = 2.0f;		// ○ の線の太さ(px)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);	// 工程リストと同じインク
	const ImU32 INK_ITEM = IM_COL32( 60,  42,  28, 230);
	const ImU32 INK_OFF  = IM_COL32( 60,  42,  28, 120);	// 消している光 = 薄いインク
	const ImU32 INK_SEL  = IM_COL32(150,  32,  18, 255);	// マウスが乗っている行 = 朱
	const ImU32 INK_LINE = IM_COL32( 60,  42,  28,  90);	// 区切り線

	const float corner = disp.y * CORNER_RATIO;
	const float row    = disp.y * ROW_RATIO;
	const float panelW = disp.x * PANEL_W_RATIO;
	const float headH  = disp.y * (HEAD_RATIO + HINT_RATIO) + row * 0.5f;
	// 光の強さのつまみ(調整値そのものを変える = 保存できる)。値は CottageRender::Data() = F1 と同じ所
	CottageRender::Settings& light = CottageRender::Data();
	// 値の見せ方: 数値 / 時計(time01 → 6:00 起点の時刻) / 分(秒で持つ値を分で見せる)
	enum class Show { Number, Clock, Minutes };
	struct Slider { const char* label; float* value; float minValue, maxValue; Show show; };
	const float SUN_MAX = 64.0f, FIRE_MAX = 64.0f;	// つまみの右端(F1 の上限 128 の半分 = 細かく合わせやすい)
	const float BEAM_DENSITY_MAX = 0.3f;			// 光の筋の濃さの右端(1m の空気が散らす割合。0.3 = 煙たい鍛冶場)
	const float DAY_SEC_MIN = 30.0f, DAY_SEC_MAX = 1200.0f;	// 一日の長さ(秒): 30秒(早送り)〜20分
	const float SEC_PER_MIN = 60.0f;
	const Slider SLIDERS[] = {
		{ (const char*)u8"時刻",         &m_sun.time01,     0.0f, 1.0f, Show::Clock },		// 動かすと太陽がそこへ跳ぶ
		{ (const char*)u8"一日の長さ",   &m_sun.dayLengthSec, DAY_SEC_MIN, DAY_SEC_MAX, Show::Minutes },
		{ (const char*)u8"太陽の強さ",   &light.sun.w,     0.0f, SUN_MAX,  Show::Number },	// 昼(真上)の強さ。朝夕・夜は時刻が弱める
		{ (const char*)u8"炉の火の強さ", &light.ambient.w, 0.0f, FIRE_MAX, Show::Number },	// ambient.w = 炉の火の強さ(CottageRender の並び)
		{ (const char*)u8"光の筋の濃さ", &VolumetricLight::Get().density, 0.0f, BEAM_DENSITY_MAX, Show::Number },	// 空気中の塵・煙の量(保存はまだしない)
	};
	const int NSLIDER = (int)_countof(SLIDERS);
	const int NEXTRA  = 2;	// 「時間を進める」+「この明るさを保存」の行
	const float panelH = corner * INSET_TOP + headH + row * NROW + row * SECTION_GAP + row * NSLIDER + row * NEXTRA
	                   + row * SECTION_GAP + row * NPRESET + corner * INSET_BOTTOM;
	const ImVec2 pa(disp.x * (1.0f - RIGHT_MARGIN) - panelW, disp.y * TOP_RATIO);
	const ImVec2 pb(pa.x + panelW, pa.y + panelH);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	const float lx = pa.x + corner * INSET_X, rx = pb.x - corner * INSET_X;
	float y = pa.y + corner * INSET_TOP;
	{
		const float px = disp.y * HEAD_RATIO;
		dl->AddText(DebugUI::FontJPFor(px), px, ImVec2(lx, y), INK_HEAD, (const char*)u8"ライティング確認");
		y += px;
		const float hp = disp.y * HINT_RATIO;
		dl->AddText(DebugUI::FontJPFor(hp), hp, ImVec2(lx, y), INK_OFF, (const char*)u8"クリック / 数字キーで切替・F2 で閉じる");
		y += hp + row * 0.5f;
	}

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool   click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const float  ipx   = disp.y * ITEM_RATIO;
	ImFont* f = DebugUI::FontJPFor(ipx);
	bool needRecapture = false;
	auto rowHit = [&](float top) { return mouse.x >= pa.x && mouse.x <= pb.x && mouse.y >= top && mouse.y < top + row; };

	// --- 光源ごとの切り替え(1〜5 キーでも) ---
	for (int i = 0; i < NROW; ++i, y += row)
	{
		const bool hover = rowHit(y);
		const bool key   = IsKeyTrigger((BYTE)('1' + i));
		if ((hover && click) || key)
		{
			*ROWS[i].flag = !*ROWS[i].flag;
			needRecapture |= ROWS[i].recapture;
		}
		const bool lit = *ROWS[i].flag;
		const float ty = y + (row - ipx) * 0.5f;
		char label[96];
		snprintf(label, sizeof(label), "%d  %s", i + 1, ROWS[i].label);
		dl->AddText(f, ipx, ImVec2(lx, ty), hover ? INK_SEL : (lit ? INK_ITEM : INK_OFF), label);
		const float r = disp.y * DOT_R_RATIO;
		const ImVec2 c(rx - r, y + row * 0.5f);
		if (lit) dl->AddCircleFilled(c, r, hover ? INK_SEL : INK_ITEM);	// ● = 点いている
		else     dl->AddCircle(c, r, hover ? INK_SEL : INK_OFF, 0, DOT_LINE_W);	// ○ = 消している
	}

	// --- 光の強さ(ドラッグ)。離した時に反射の写真を撮り直す(ドラッグ中に毎フレーム撮ると重い) ---
	y += row * SECTION_GAP * 0.5f;
	dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), INK_LINE, 1.0f);
	y += row * SECTION_GAP * 0.5f;
	//   1行 = [名前 | 棒 | 値] の3列。名前の列幅は一番長い名前に合わせる(=名前・棒・値が重ならない。2026-10-17 F5)
	const float BAR_H     = 0.18f;		// 棒の太さ(行の高さ比)
	const float KNOB_R    = 0.22f;		// つまみの丸の半径(行の高さ比)
	const float COL_GAP   = 0.6f;		// 列の間(行の高さ比)
	const char* VALUE_WIDEST = "00:00";	// 値の列の幅はこの文字列で測る(一番長い表示)
	float nameW = 0.0f;
	for (const Slider& sl : SLIDERS) nameW = fmaxf(nameW, f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, sl.label).x);
	const float valueW = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, VALUE_WIDEST).x;
	const float barL = lx + nameW + row * COL_GAP + row * KNOB_R;
	const float barR = rx - valueW - row * COL_GAP - row * KNOB_R;
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_lightPanelDrag >= 0)
	{
		m_lightPanelDrag = -1;
		ReflectionProbe::RequestRecapture();
	}
	for (int s = 0; s < NSLIDER; ++s, y += row)
	{
		const Slider& sl = SLIDERS[s];
		const bool hover = rowHit(y);
		if (hover && click && mouse.x >= barL - row * KNOB_R) { m_lightPanelDrag = s; m_lightPanelSaved = false; }	// 棒の上だけ(名前を押して 0 にならない様に)
		if (m_lightPanelDrag == s)
		{
			const float t = fminf(fmaxf((mouse.x - barL) / (barR - barL), 0.0f), 1.0f);
			*sl.value = sl.minValue + t * (sl.maxValue - sl.minValue);
		}
		const bool active = hover || m_lightPanelDrag == s;
		const ImU32 ink = active ? INK_SEL : INK_ITEM;
		const float ty = y + (row - ipx) * 0.5f;
		dl->AddText(f, ipx, ImVec2(lx, ty), ink, sl.label);
		char value[32];
		if (sl.show == Show::Clock)
		{
			const float h = m_sun.Hour();
			snprintf(value, sizeof(value), "%02d:%02d", (int)h, (int)((h - floorf(h)) * SEC_PER_MIN));
		}
		else if (sl.show == Show::Minutes) snprintf(value, sizeof(value), (const char*)u8"%.1f分", *sl.value / SEC_PER_MIN);
		else snprintf(value, sizeof(value), *sl.value < 1.0f ? "%.2f" : "%.1f", *sl.value);
		const float vw = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, value).x;
		dl->AddText(f, ipx, ImVec2(rx - vw, ty), ink, value);	// 値は右揃え
		const float cy = y + row * 0.5f, bh = row * BAR_H;
		const float t  = fminf(fmaxf((*sl.value - sl.minValue) / (sl.maxValue - sl.minValue), 0.0f), 1.0f);
		const float kx = barL + (barR - barL) * t;
		dl->AddRectFilled(ImVec2(barL, cy - bh * 0.5f), ImVec2(barR, cy + bh * 0.5f), INK_LINE, bh * 0.5f);
		dl->AddRectFilled(ImVec2(barL, cy - bh * 0.5f), ImVec2(kx, cy + bh * 0.5f), ink, bh * 0.5f);
		dl->AddCircleFilled(ImVec2(kx, cy), row * KNOB_R, ink);
	}
	{	// 時間を進める / 止める(止めると今の光で見比べられる)
		const bool hover = rowHit(y);
		if (hover && click) m_sun.running = !m_sun.running;
		dl->AddText(f, ipx, ImVec2(lx, y + (row - ipx) * 0.5f), hover ? INK_SEL : INK_ITEM, (const char*)u8"時間を進める");
		const float r = disp.y * DOT_R_RATIO;
		const ImVec2 c(rx - r, y + row * 0.5f);
		if (m_sun.running) dl->AddCircleFilled(c, r, hover ? INK_SEL : INK_ITEM);
		else               dl->AddCircle(c, r, hover ? INK_SEL : INK_OFF, 0, DOT_LINE_W);
		y += row;
	}
	{
		const bool hover = rowHit(y);
		if (hover && click) m_lightPanelSaved = CottageRender::Save();	// Assets/cottage_materials.txt(F1 の「Save cottage look」と同じ)
		const char* text = m_lightPanelSaved ? (const char*)u8"保存しました" : (const char*)u8"この明るさを保存";
		dl->AddText(f, ipx, ImVec2(lx, y + (row - ipx) * 0.5f), hover ? INK_SEL : INK_ITEM, text);
		y += row;
	}

	// --- まとめて切り替え ---
	y += row * SECTION_GAP * 0.5f;
	dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), INK_LINE, 1.0f);
	y += row * SECTION_GAP * 0.5f;
	for (int p = 0; p < NPRESET; ++p, y += row)
	{
		const bool hover = rowHit(y);
		if (hover && click)
			for (int i = 0; i < NROW; ++i)
				if (*ROWS[i].flag != PRESETS[p].values[i]) { *ROWS[i].flag = PRESETS[p].values[i]; needRecapture |= ROWS[i].recapture; }
		bool active = true;	// 今の状態がこの型と同じか(同じなら ▸ を付ける)
		for (int i = 0; i < NROW; ++i) active &= (*ROWS[i].flag == PRESETS[p].values[i]);
		const float ty = y + (row - ipx) * 0.5f;
		dl->AddText(f, ipx, ImVec2(lx, ty), hover ? INK_SEL : INK_ITEM, PRESETS[p].label);
		if (active)
		{
			const float m = disp.y * DOT_R_RATIO, my = y + row * 0.5f, mx = rx - m;
			dl->AddTriangleFilled(ImVec2(mx - m, my - m), ImVec2(mx - m, my + m), ImVec2(mx + m * 0.6f, my), INK_SEL);
		}
	}

	if (needRecapture) ReflectionProbe::RequestRecapture();	// 反射の写真も今の光で撮り直す(次のフレーム)
}

//====================================================================
//  設計者用パネル F3 = 処理の重さ(GPU 計時)
//  フレームの各段階が GPU で何ミリ秒かかったか(GpuProfiler のタイムスタンプ)。最適化は「推測でなく測定」から:
//  一番重い所(朱)から手を付け、直したらここで数字が下がったかを確かめる。見るだけなのでゲームは止めない。
//====================================================================
void SceneForge::DrawProfilerPanel()
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float PANEL_W_RATIO = 0.34f;		// パネルの幅(画面幅比)
	const float LEFT_MARGIN   = 0.02f;		// 画面左端からの余白
	const float TOP_RATIO     = 0.10f;		// パネル上端
	const float CORNER_RATIO  = 0.06f;
	const float HEAD_RATIO    = 0.032f, HINT_RATIO = 0.019f, ITEM_RATIO = 0.021f, ROW_RATIO = 0.034f;
	const float INSET_X = 0.9f, INSET_TOP = 0.8f, INSET_BOTTOM = 0.7f;
	const float BAR_H   = 0.35f;			// 棒の太さ(行の高さ比)
	const float COL_GAP = 0.5f;				// 列の間(行の高さ比)
	const float FRAME_BUDGET_MS = 1000.0f / 60.0f;	// 60fps で 1 フレームに使える時間(棒の右端の目安)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);
	const ImU32 INK_ITEM = IM_COL32( 60,  42,  28, 230);
	const ImU32 INK_HINT = IM_COL32( 60,  42,  28, 130);
	const ImU32 INK_HOT  = IM_COL32(150,  32,  18, 255);	// 一番重い段階 = 朱
	const ImU32 INK_LINE = IM_COL32( 60,  42,  28,  60);

	const std::vector<GpuProfiler::Entry>& rows = GpuProfiler::Results();
	const float corner = disp.y * CORNER_RATIO, row = disp.y * ROW_RATIO;
	const float panelW = disp.x * PANEL_W_RATIO;
	const int   nInfo  = 3;	// 「1フレーム全体」「CPU」「FPS」の行
	const float headH  = disp.y * (HEAD_RATIO + HINT_RATIO) + row * 0.5f;
	const float panelH = corner * INSET_TOP + headH + row * (nInfo + (float)rows.size()) + row * 0.5f + corner * INSET_BOTTOM;
	const ImVec2 pa(disp.x * LEFT_MARGIN, disp.y * TOP_RATIO), pb(pa.x + panelW, pa.y + panelH);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	const float lx = pa.x + corner * INSET_X, rx = pb.x - corner * INSET_X;
	float y = pa.y + corner * INSET_TOP;
	{
		const float px = disp.y * HEAD_RATIO, hp = disp.y * HINT_RATIO;
		dl->AddText(DebugUI::FontJPFor(px), px, ImVec2(lx, y), INK_HEAD, (const char*)u8"処理の重さ（GPU）");
		y += px;
		dl->AddText(DebugUI::FontJPFor(hp), hp, ImVec2(lx, y), INK_HINT, (const char*)u8"F3 で閉じる・ゲームは止まらない・数フレームの平均");
		y += hp + row * 0.5f;
	}
	const float ipx = disp.y * ITEM_RATIO;
	ImFont* f = DebugUI::FontJPFor(ipx);
	char text[96];
	// 全体
	const float frameMs = GpuProfiler::FrameMs();
	snprintf(text, sizeof(text), (const char*)u8"1フレーム全体（GPU）  %.2f ms  / 60fps の予算 %.1f ms", frameMs, FRAME_BUDGET_MS);
	dl->AddText(f, ipx, ImVec2(lx, y + (row - ipx) * 0.5f), frameMs > FRAME_BUDGET_MS ? INK_HOT : INK_ITEM, text);
	y += row;
	// CPU が描画命令を出すのに掛かった時間。GPU の合計とほぼ同じなら、GPU は CPU を「待って」いる(=CPU 側が遅い)
	snprintf(text, sizeof(text), (const char*)u8"CPU（描画命令を出す時間）  %.2f ms", GpuProfiler::CpuMs());
	dl->AddText(f, ipx, ImVec2(lx, y + (row - ipx) * 0.5f), INK_ITEM, text);
	y += row;
	snprintf(text, sizeof(text), "FPS  %.0f", ImGui::GetIO().Framerate);
	dl->AddText(f, ipx, ImVec2(lx, y + (row - ipx) * 0.5f), INK_ITEM, text);
	y += row + row * 0.25f;
	dl->AddLine(ImVec2(lx, y), ImVec2(rx, y), INK_LINE, 1.0f);
	y += row * 0.25f;

	// 段階ごと: [名前 | 棒(1フレームの予算に対する割合) | ms]。一番重い段階を朱で
	float nameW = 0.0f, maxMs = 0.0f;
	for (const GpuProfiler::Entry& e : rows) { nameW = fmaxf(nameW, f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, e.name).x); maxMs = fmaxf(maxMs, e.ms); }
	const float valueW = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, "00.00 ms").x;
	const float barL = lx + nameW + row * COL_GAP, barR = rx - valueW - row * COL_GAP;
	for (const GpuProfiler::Entry& e : rows)
	{
		const bool hot = (e.ms == maxMs && maxMs > 0.0f);
		const ImU32 ink = hot ? INK_HOT : INK_ITEM;
		const float ty = y + (row - ipx) * 0.5f;
		dl->AddText(f, ipx, ImVec2(lx, ty), ink, e.name);
		snprintf(text, sizeof(text), "%.2f ms", e.ms);
		dl->AddText(f, ipx, ImVec2(rx - f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, text).x, ty), ink, text);
		const float cy = y + row * 0.5f, bh = row * BAR_H;
		const float t = fminf(e.ms / FRAME_BUDGET_MS, 1.0f);
		if (barR > barL)
		{
			dl->AddRectFilled(ImVec2(barL, cy - bh * 0.5f), ImVec2(barR, cy + bh * 0.5f), INK_LINE, bh * 0.5f);
			dl->AddRectFilled(ImVec2(barL, cy - bh * 0.5f), ImVec2(barL + (barR - barL) * t, cy + bh * 0.5f), ink, bh * 0.5f);
		}
		y += row;
	}
}

//--- 「レッスン完了」: 画面中央の羊皮紙に課程の名前と完了。課程の一覧と同じ見た目(羊皮紙+インク)。
void SceneForge::DrawCourseComplete()
{
	if (!m_recipe || m_courseIdx < 0 || m_courseIdx >= (int)m_recipe->courses.size()) return;
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float FADE_SEC      = 0.4f;	// 現れる速さ(秒)
	const float PANEL_W_RATIO = 0.30f, PANEL_H_RATIO = 0.26f, CORNER_RATIO = 0.085f;	// 課程の一覧と同じ隅の大きさ
	const float NAME_RATIO = 0.034f, DONE_RATIO = 0.052f;	// 課程の名前 / 「レッスン完了」の文字の高さ
	const float NAME_Y = 0.42f, DONE_Y = 0.55f;				// 文字の縦位置(パネル内の比)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);
	const ImU32 INK_DONE = IM_COL32(150,  32,  18, 255);	// 朱(印章の色)
	const float a01 = fminf((m_time - m_courseCompleteAt) / FADE_SEC, 1.0f);
	const float pw = disp.x * PANEL_W_RATIO, ph = disp.y * PANEL_H_RATIO;
	const ImVec2 pa((disp.x - pw) * 0.5f, (disp.y - ph) * 0.5f), pb(pa.x + pw, pa.y + ph);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, disp.y * CORNER_RATIO);
	auto center = [&](const char* t, float px, float yRatio, ImU32 col)
	{
		ImFont* f = DebugUI::FontJPFor(px);
		const ImVec2 sz = f->CalcTextSizeA(px, FLT_MAX, 0.0f, t);
		const ImU32 c = (col & ~IM_COL32_A_MASK) | ((ImU32)(255 * a01) << IM_COL32_A_SHIFT);
		dl->AddText(f, px, ImVec2((disp.x - sz.x) * 0.5f, pa.y + ph * yRatio - sz.y * 0.5f), c, t);
	};
	center(m_recipe->courses[m_courseIdx].label, disp.y * NAME_RATIO, NAME_Y, INK_HEAD);
	center((const char*)u8"レッスン完了", disp.y * DONE_RATIO, DONE_Y, INK_DONE);
}

//--- 設定の画面(一時停止メニューと同じ羊皮紙)。行 = 「教学アニメの速さ ◀ 1.0倍 ▶」/「震動 ◀ 100% ▶」/ 戻る。
//    速さの行を選んでいる間は、パネルの右に瞄準の指示図を今の速さで動かして見せる(変えた結果がその場で分かる)。
void SceneForge::DrawOptionsMenu()
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const ImU32 DIM_COL       = IM_COL32(0, 0, 0, 150);
	const float PANEL_W_RATIO = 0.36f, CORNER_RATIO = 0.085f;	// 一時停止メニューより少し広い(値の列がある)
	const float HEAD_RATIO = 0.040f, ITEM_RATIO = 0.032f, ROW_RATIO = 0.075f, HEAD_GAP = 1.3f;
	const float INSET_TOP = 0.95f, INSET_BOTTOM = 0.75f, INSET_SIDE = 0.75f;
	const float ARROW_RATIO = 0.011f;		// ◀ ▶ の大きさ(画面高さ比)
	const float VALUE_W_RATIO = 0.075f;		// ◀ と ▶ の間(値の欄)の幅(画面幅比)
	const float PREVIEW_GAP = 0.13f;		// パネルの右端から見本の中心まで(画面幅比)
	const float PREVIEW_SCALE = 1.2f;		// 見本の大きさ(案内の図に対する倍率)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);
	const ImU32 INK_ITEM = IM_COL32( 60,  42,  28, 220);
	const ImU32 INK_SEL  = IM_COL32(150,  32,  18, 255);
	const ImU32 INK_OFF  = IM_COL32( 60,  42,  28,  70);	// 端でこれ以上変えられない矢印(薄い)

	dl->AddRectFilled(ImVec2(0, 0), disp, DIM_COL);
	const float corner = disp.y * CORNER_RATIO, row = disp.y * ROW_RATIO;
	const int   n = (int)OptionItem::Count;
	const float pw = disp.x * PANEL_W_RATIO, ph = corner * INSET_TOP + row * HEAD_GAP + row * n + corner * INSET_BOTTOM;
	const ImVec2 pa((disp.x - pw) * 0.5f, (disp.y - ph) * 0.5f), pb(pa.x + pw, pa.y + ph);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	float cy = pa.y + corner * INSET_TOP;
	{
		const float px = disp.y * HEAD_RATIO;
		ImFont* f = DebugUI::FontJPFor(px);
		const char* head = (const char*)u8"設定";
		const ImVec2 sz = f->CalcTextSizeA(px, FLT_MAX, 0.0f, head);
		dl->AddText(f, px, ImVec2((pa.x + pb.x - sz.x) * 0.5f, cy), INK_HEAD, head);
	}
	cy += row * HEAD_GAP;

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool   click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool   mouseMoved = MouseMovedThisFrame();	// ホバーで選ぶのはマウスが動いた時だけ(DrawChoicePanel と同じ)
	const float  ipx = disp.y * ITEM_RATIO;
	ImFont* f = DebugUI::FontJPFor(ipx);
	const float xL = pa.x + corner * INSET_SIDE, xR = pb.x - corner * INSET_SIDE;
	auto hit = [&](ImVec2 a, ImVec2 b) { return mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y; };
	for (int i = 0; i < n; ++i, cy += row)
	{
		const ImVec2 ra(xL, cy - (row - ipx) * 0.5f), rb(xR, ra.y + row);
		if (hit(ra, rb) && (mouseMoved || click)) m_optionsSel = i;
		const bool isSel = (i == m_optionsSel);
		const ImU32 ink = isSel ? INK_SEL : INK_ITEM;
		// 値のある行: 名前(左)+ ◀ 値 ▶(右寄せ)。矢印はクリックでも変えられる。端では薄く(もう動かない)
		auto steppedRow = [&](const char* name, const char* value, int idx, int nStep)
		{
			dl->AddText(f, ipx, ImVec2(xL, cy), ink, name);
			const float a = disp.y * ARROW_RATIO, my = cy + ipx * 0.5f;
			const float vx1 = xR - a * 2.0f, vx0 = vx1 - disp.x * VALUE_W_RATIO;
			const ImVec2 lc(vx0 - a * 1.5f, my), rc(xR - a, my);
			const bool canL = idx > 0, canR = idx < nStep - 1;
			dl->AddTriangleFilled(ImVec2(lc.x - a, my), ImVec2(lc.x + a * 0.7f, my - a), ImVec2(lc.x + a * 0.7f, my + a), canL ? ink : INK_OFF);
			dl->AddTriangleFilled(ImVec2(rc.x + a, my), ImVec2(rc.x - a * 0.7f, my - a), ImVec2(rc.x - a * 0.7f, my + a), canR ? ink : INK_OFF);
			const ImVec2 vs = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, value);
			dl->AddText(f, ipx, ImVec2((vx0 + vx1 - vs.x) * 0.5f, cy), ink, value);
			if (click && hit(ImVec2(lc.x - a * 2, my - a * 2), ImVec2(lc.x + a * 2, my + a * 2))) m_optionsStep = -1;
			if (click && hit(ImVec2(rc.x - a * 2, my - a * 2), ImVec2(rc.x + a * 2, my + a * 2))) m_optionsStep = +1;
		};
		const int PERCENT = 100;
		char val[24];
		if (i == (int)OptionItem::LessonSpeed)
		{
			sprintf_s(val, sizeof(val), "%g%s", LessonSpeed(), (const char*)u8"倍");	// 0.5倍 / 1.25倍 …(%g = 余分な 0 を付けない)
			steppedRow((const char*)u8"案内アニメの速さ", val, m_lessonSpeedIdx, (int)_countof(LESSON_SPEED_STEPS));
		}
		else if (i == (int)OptionItem::Rumble)
		{
			if (m_rumbleIdx == 0) strcpy_s(val, sizeof(val), (const char*)u8"オフ");
			else                  sprintf_s(val, sizeof(val), "%d%%", (int)(RUMBLE_STEPS[m_rumbleIdx] * PERCENT + 0.5f));	// 25% / 50% …
			steppedRow((const char*)u8"震動(コントローラー)", val, m_rumbleIdx, (int)_countof(RUMBLE_STEPS));
		}
		else if (i == (int)OptionItem::Quality)
		{
			strcpy_s(val, sizeof(val), m_qualityIdx == 0 ? (const char*)u8"標準" : (const char*)u8"高画質");
			steppedRow((const char*)u8"画質", val, m_qualityIdx, (int)_countof(QUALITY_STEPS));
		}
		else
		{
			const char* back = (const char*)u8"戻る";
			const ImVec2 sz = f->CalcTextSizeA(ipx, FLT_MAX, 0.0f, back);
			dl->AddText(f, ipx, ImVec2((pa.x + pb.x - sz.x) * 0.5f, cy), ink, back);
			if (click && hit(ra, rb)) m_optionsRequest = i;
		}
	}

	// 見本: 瞄準の指示図を今の速さで(m_lessonClock は一時停止中も設定の速さで進む)
	if (m_optionsSel == (int)OptionItem::LessonSpeed)
		DrawLessonPicture(LessonId::Aim, pb.x + disp.x * PREVIEW_GAP, (pa.y + pb.y) * 0.5f, 1.0f, PREVIEW_SCALE, IM_COL32(255, 255, 255, 255));
}

//--- 一時停止メニュー: 画面を暗くし、中央に選択パネル。クリックは m_pauseRequest へ(実行は UpdatePause)。
void SceneForge::DrawPauseMenu()
{
	if (m_optionsOpen) { DrawOptionsMenu(); DrawMenuButtonGuide(); return; }	// 設定の画面
	static const char* LABELS[(int)PauseItem::Count] = {
		(const char*)u8"ゲームを続ける", (const char*)u8"設定", (const char*)u8"タイトルへ戻る", (const char*)u8"ゲームを終了する" };
	const float CENTER_Y = 0.5f;
	const int clicked = DrawChoicePanel((const char*)u8"一時停止", LABELS, (int)PauseItem::Count, m_pauseSel, CENTER_Y, true);
	if (clicked >= 0) m_pauseRequest = (PauseItem)clicked;
	DrawMenuButtonGuide();
}

//--- メニューの右下のボタンガイド(主機のゲームの約束事)。「どのボタンで決める/戻るか」を今の機器の絵で。
void SceneForge::DrawMenuButtonGuide()
{
	const float GUIDE_Y = 0.95f;	// 縦位置(画面高さ比)= 画面の下端近く
	static const KeyHint MENU_HINTS[] = {
		{ Action::Confirm, (const char*)u8"決定" },
		{ Action::Back,    (const char*)u8"戻る" },
	};
	DrawKeyHints(MENU_HINTS, _countof(MENU_HINTS), GUIDE_Y, 1.0f, true);
}

//--- チュートリアル: 金床の工位で鍛造できる状態か(高亮・「ここで叩く」を出す条件)。
bool SceneForge::TutorialAtAnvil() const
{
	return m_tutorial && m_state == GAME_PLAY && !m_walkMode && !Transitioning() && !m_carrying
	    && m_station == Station::Anvil && m_workAt == Station::Anvil && m_flipPhase == FlipPhase::None && !m_clearDecided;
}

//--- 秒 → "分:秒"(例 65.3 → "1:05")
static void FormatMinSec(char* buf, size_t size, float sec)
{
	const int s = (int)sec;
	sprintf_s(buf, size, "%d:%02d", s / 60, s % 60);
}

//--- プレイテスト計時の表示(右下の羊皮紙。工程リスト/説明パネルと同じ見た目)。
//    列 = 工程名 / その工程の時間 / うち移動(歩く+工位への出入り)。下に合計と目標(3分)、目標に対する割合のバー。
//    今の工程と、目標を超えた合計は朱(工程リストの「今」と同じ色)=目に留まる。
void SceneForge::DrawPlaytestTimer()
{
	if (!m_recipe || m_stepTimes.empty()) return;
	ImVec2 disp = ImGui::GetIO().DisplaySize;		// 寸法はすべて画面比(解像度非依存)
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	const float PANEL_RIGHT_RATIO  = 0.985f;	// 右端(画面幅比)=説明パネルと揃える
	const float PANEL_BOTTOM_RATIO = 0.86f;		// 下端(画面高さ比)=下の操作ガイド(0.93)に掛からない
	const float PANEL_W_RATIO      = 0.20f;
	const float CORNER_RATIO       = 0.065f;	// 隅の大きさ=説明パネルと同じ
	const float INSET_TOP = 0.95f, INSET_BOTTOM = 0.75f, INSET_SIDE = 0.60f;
	const float HEAD_RATIO   = 0.021f;			// 見出し「計時」
	const float TEXT_RATIO   = 0.018f;			// 工程の行
	const float COLHEAD_RATIO= 0.014f;			// 列の見出し(時間 / 移動)
	const float ROW_SPACING  = 1.45f;			// 行の間隔(文字の高さに対する比)
	const float HEAD_GAP     = 1.5f;			// 見出しから列見出しまで(〃)
	const float COL_TIME     = 0.62f;			// 「時間」列の右端(中身の幅に対する比)
	const float COL_MOVE     = 1.00f;			// 「移動」列の右端(〃)
	const float RULE_GAP     = 0.5f;			// 合計の上の罫線の前後の余白(行の高さ比)
	const float BAR_H_RATIO  = 0.008f;			// 目標バーの太さ
	const float LINE_W       = 1.0f;			// 罫線/バー輪郭の太さ(px)
	const ImU32 INK_HEAD    = IM_COL32( 60,  38,  22, 255);	// 工程リストと同じインク
	const ImU32 INK_TEXT    = IM_COL32( 60,  42,  28, 220);
	const ImU32 INK_DIM     = IM_COL32(110,  88,  64, 200);	// 列見出し・移動の列(補足=薄く)
	const ImU32 INK_NOW     = IM_COL32(150,  32,  18, 255);	// 今の工程 / 目標超過 = 朱
	const ImU32 BAR_BG_COL  = IM_COL32( 60,  42,  28, 110);	// 工程リストの進捗バーと同じ溝

	const int   n      = (int)m_stepTimes.size();
	const float corner = disp.y * CORNER_RATIO;
	const float panelW = disp.x * PANEL_W_RATIO;
	const float hpx = disp.y * HEAD_RATIO, tpx = disp.y * TEXT_RATIO, cpx = disp.y * COLHEAD_RATIO;
	const float row = tpx * ROW_SPACING;
	ImFont* hf = DebugUI::FontJPFor(hpx);
	ImFont* tf = DebugUI::FontJPFor(tpx);
	ImFont* cf = DebugUI::FontJPFor(cpx);

	// パネルの高さは中身から: 見出し + 列見出し + 工程 n 行 + 罫線 + 合計 + バー
	const float contentH = hpx * HEAD_GAP + row + row * n + row * RULE_GAP * 2.0f + row + disp.y * BAR_H_RATIO;
	const float panelH = corner * INSET_TOP + contentH + corner * INSET_BOTTOM;
	const ImVec2 pb(disp.x * PANEL_RIGHT_RATIO, disp.y * PANEL_BOTTOM_RATIO);
	const ImVec2 pa(pb.x - panelW, pb.y - panelH);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);

	const float x0 = pa.x + corner * INSET_SIDE, x1 = pb.x - corner * INSET_SIDE;
	const float xTime = x0 + (x1 - x0) * COL_TIME, xMove = x0 + (x1 - x0) * COL_MOVE;
	auto rightText = [&](ImFont* f, float px, float xr, float y, ImU32 col, const char* s)	// 右揃え(数字の桁が揃う)
	{
		const float w = f->CalcTextSizeA(px, FLT_MAX, 0.0f, s).x;
		dl->AddText(f, px, ImVec2(xr - w, y), col, s);
	};

	float y = pa.y + corner * INSET_TOP;
	dl->AddText(hf, hpx, ImVec2(x0, y), INK_HEAD, (const char*)u8"計時");
	y += hpx * HEAD_GAP;
	rightText(cf, cpx, xTime, y, INK_DIM, (const char*)u8"時間");
	rightText(cf, cpx, xMove, y, INK_DIM, (const char*)u8"うち移動");
	y += row;

	char buf[16];
	float total = 0.0f;
	for (int i = 0; i < n; ++i, y += row)
	{
		const StepTime& t = m_stepTimes[i];
		total += t.total;
		const bool  now = (m_state == GAME_PLAY && i == m_stepIdx);
		const ImU32 col = now ? INK_NOW : INK_TEXT;
		const char* label = (i < (int)m_recipe->steps.size()) ? m_recipe->steps[i].label : "";
		dl->AddText(tf, tpx, ImVec2(x0, y), col, label);
		FormatMinSec(buf, sizeof(buf), t.total); rightText(tf, tpx, xTime, y, col, buf);
		FormatMinSec(buf, sizeof(buf), t.move);  rightText(tf, tpx, xMove, y, INK_DIM, buf);
	}

	// 罫線 → 合計 / 目標
	y += row * RULE_GAP;
	dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), INK_DIM, LINE_W);
	y += row * RULE_GAP;
	const bool  over     = total > PLAYTEST_BUDGET_SEC;
	const ImU32 totalCol = over ? INK_NOW : INK_HEAD;
	dl->AddText(tf, tpx, ImVec2(x0, y), totalCol, (const char*)u8"合計");
	FormatMinSec(buf, sizeof(buf), total);
	rightText(tf, tpx, xTime, y, totalCol, buf);
	char budget[24], bb[16];
	FormatMinSec(bb, sizeof(bb), PLAYTEST_BUDGET_SEC);
	sprintf_s(budget, sizeof(budget), "/ %s", bb);
	rightText(tf, tpx, xMove, y, INK_DIM, budget);
	y += row;

	// 目標に対する割合のバー(満タン=3分。超えたら朱)
	const float bh = disp.y * BAR_H_RATIO;
	const float fill = fminf(total / PLAYTEST_BUDGET_SEC, 1.0f);
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + bh), BAR_BG_COL);
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + (x1 - x0) * fill, y + bh), totalCol);
	dl->AddRect(ImVec2(x0, y), ImVec2(x1, y + bh), INK_HEAD, 0.0f, 0, LINE_W);
}

//--- 研ぎの角度計(チュートリアルだけ): 0..GRIND_ANGLE_MAX の横棒。緑 = 研げる角度(効率 > 0)、白い印 = 今の角度。
void SceneForge::DrawGrindAngleMeter()
{
	ImVec2 disp = ImGui::GetIO().DisplaySize;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	// 縦の小さな角度計(2026-10-12 ユーザー: 横長で画面の真ん中にあって邪魔だった)。
	//   縦 = マウスの上下と同じ向き(上 = 立てる / 下 = 寝かせる)。操作の案内(画面中央の下)の右に置き、刃と砥石の上には掛からない。
	const float X_RATIO     = 0.62f;	// 棒の中心(画面幅比)= 案内のキーの絵の右
	const float Y0_RATIO    = 0.24f;	// 棒の上端(画面高さ比)。操作の案内(キーの絵と一言)の上に置く=重ならない(2026-10-19)
	const float Y1_RATIO    = 0.38f;	// 棒の下端(画面高さ比)。砥石の上の刃の線より上
	const float W_RATIO     = 0.010f;	// 棒の太さ(画面高さ比)
	const float LABEL_RATIO = 0.020f;	// 「角度」の文字の高さ
	const float MARK_OVER   = 0.8f;		// 印が棒からはみ出す量(棒の太さ比)
	const float MARK_W      = 3.0f;		// 印の太さ(px)
	const float cx = disp.x * X_RATIO, y0 = disp.y * Y0_RATIO, y1 = disp.y * Y1_RATIO, hw = disp.y * W_RATIO * 0.5f;
	auto angleY = [&](float a) { return y1 - (y1 - y0) * fminf(fmaxf(a / GRIND_ANGLE_MAX, 0.0f), 1.0f); };	// 0(寝かせる)= 下 / 最大 = 上
	dl->AddRectFilled(ImVec2(cx - hw, y0), ImVec2(cx + hw, y1), IM_COL32(20, 16, 12, 200), hw);
	const ImU32 green = IM_COL32(90, 180, 90, 230);
	dl->AddRectFilled(ImVec2(cx - hw, angleY(GRIND_IDEAL_ANGLE + GRIND_ANGLE_TOL)), ImVec2(cx + hw, angleY(GRIND_IDEAL_ANGLE - GRIND_ANGLE_TOL)), green, hw);
	const float my = angleY(fabsf(m_grindAngle));
	dl->AddLine(ImVec2(cx - hw * (1.0f + MARK_OVER), my), ImVec2(cx + hw * (1.0f + MARK_OVER), my), IM_COL32(255, 255, 255, 255), MARK_W);
	const float lpx = disp.y * LABEL_RATIO;
	ImFont* f = DebugUI::FontJPFor(lpx);
	const char* label = (const char*)u8"角度";
	const ImVec2 sz = f->CalcTextSizeA(lpx, FLT_MAX, 0.0f, label);
	dl->AddText(f, lpx, ImVec2(cx - sz.x * 0.5f, y0 - lpx * 1.4f), IM_COL32(255, 236, 196, 240), label);
}

void SceneForge::DrawUI()
{
	// 配置/材質/炭火/カメラの編集はすべて SCENE_STAGE_EDITOR に移設。
	// ゲーム側は焼き込み済みの値で表示するだけ(F1はクリーン)。

	switch (m_state)
	{
	case GAME_TITLE:
		// 撮影モードではロゴと「PRESS START」を隠す(メニューを開いている時は操作に要るので出す)
		if (m_shotMode == ShotMode::Normal || m_modeSelectOpen || m_courseSelectOpen) DrawTitleUI();
		break;
	case GAME_PLAY:
		// タイトル→金床へカメラが移っている間は何も出さない(ロゴは既に消えている。映像だけを見せる)。
		if (PlayHudVisible()) DrawPlayUI();
		else if (m_shotMode != ShotMode::Normal && m_clearDecided) DrawLetterbox();	// 撮影モードでも終幕の黒帯は映像の一部として出す
		if (!m_paused)
		{
			DrawFocusOverlay();	// 試し切り: 集中の暗い縁(撮影モードでは照準の輪だけ隠す。TestCut.cpp)
			if (m_shotMode != ShotMode::CleanNoBanner) DrawSeverBanner();	// 「一刀両断」(終幕の間も出す)
		}
		if (m_paused) DrawPauseMenu();
		break;
	case GAME_RESULT: DrawResultUI(); break;
	}
	if (m_state == GAME_PLAY && m_courseComplete && m_shotMode == ShotMode::Normal) DrawCourseComplete();	// チュートリアルの課程を終えた
	if (m_lightPanelOpen) DrawLightPanel();	// F2: 設計者用のライティング確認(ゲームと同じ羊皮紙の見た目)
	if (m_profilerPanelOpen) DrawProfilerPanel();	// F3: 処理の重さ(GPU の各段階の時間)
	// プレイテスト計時: 遊んでいる間(導入カメラ・終幕の映像中は出さない)と結果画面(=最終の内訳を見る)
	if (m_showPlaytestTimer
	 && ((m_state == GAME_PLAY && m_introPhase == IntroPhase::None && !m_clearDecided) || m_state == GAME_RESULT))
		DrawPlaytestTimer();

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
	LitRender::Controls();	// 道具の材質(法線/ORM の見比べ)
	if (ReflectionProbe::Controls())	// 反射プローブ(IBL): 撮り直し・鏡の球で撮れた画像を確かめる
	{
		ImGui::SliderFloat("Probe height above anvil", &m_probeLift, 0.0f, 1.5f, "%.2f m");	// 変えたら Recapture
		ImGui::SliderFloat("Room box inset", &m_probeBoxInset, 0.0f, 1.0f, "%.2f m");		// 橙の箱を壁の内面へ(撮り直し不要)
	}
	if (ImGui::CollapsingHeader("Forge light (flicker)"))	// 炉の火の光: 揺らぎ・位置の揺れ・ふいご(保存しない調整用。良い値は header の既定へ)
	{
		ImGui::SliderFloat("Flicker amount", &m_fireFlickerAmount, 0.0f, 0.8f, "%.2f");		// 明るさの揺れの深さ(±割合)
		ImGui::SliderFloat("Flicker speed",  &m_fireFlickerSpeed,  0.2f, 3.0f, "%.2f");		// 揺れの速さの倍率
		ImGui::SliderFloat("Flame jitter (m)", &m_fireJitter,      0.0f, 0.3f, "%.3f");		// 火の位置の揺れ幅 = 明暗・ハイライトが動く量
		ImGui::SliderFloat("Bellows boost",  &m_fireBellowsBoost,  1.0f, 3.0f, "%.2f");		// ふいごで部屋が明るくなる倍率
	}

		// --- 起動時スナップショットへ一発リセット(F8キーと同じ)。滅茶苦茶にしても戻せる保険。 ---
		if (ImGui::Button("Reset ALL to startup  (F8)")) RestoreTuning();
		ImGui::SameLine();
		if (ImGui::Button("Save tuning")) SaveTuning();	// 手動保存(退出時にも自動保存)
		// 衝突の可視化は折り畳みの中に隠さず最上段に置く(開いてすぐ見つかる様に)。F1を閉じても表示は残る。
		ImGui::Checkbox("Show collision", &m_showCollision);
		ImGui::SameLine();
		ImGui::Checkbox("Show grass map", &m_showGrassMap);	// 草の踏み跡の貼图(真上から正射影)を小窓に表示
		ImGui::SameLine();
		ImGui::Checkbox("Playtest timer", &m_showPlaytestTimer);	// 工程ごとの所要時間(右下)。F1 を閉じても残る
		ImGui::SameLine();
		ImGui::Checkbox("Show interact", &m_showInteract);	// 互動範囲の箱(灰=範囲外/緑=範囲内/黄=E が出ている)+白=目線。F1 を閉じても残る
		// 太陽の影の範囲(水色の正方形)。線の外は影の計算をしない=全部日向。屋内 20m / 屋外 48m で切り替わる
		ImGui::Checkbox("Show shadow range", &m_showShadowRange);
		ImGui::SameLine();
		ImGui::Text("(%s %.0fm)", ShadowCameraIndoors() ? "indoor" : "outdoor", SunStage::Data().width);
		// 戸口の敷居の Z-fighting(屋外の地面 yard と家の地基の上面が 1.4cm で重なる)の確認と調整
		ImGui::Checkbox("Yard depth bias", &m_yardDepthBias);
		ImGui::SameLine();
		ImGui::Checkbox("Hide yard ground", &m_hideYard);
		ImGui::SameLine();
		const float YARD_BIAS_SLIDER_W = 120.0f;	// スライダの幅(px)。同じ行に収める
		const int   YARD_BIAS_SLIDER_MAX = 4000;
		ImGui::SetNextItemWidth(YARD_BIAS_SLIDER_W);
		ImGui::SliderInt("Yard bias units", &m_yardBiasUnits, 0, YARD_BIAS_SLIDER_MAX);
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
			if (ImGui::Button("Jump to step")) StartAtStep(m_debugJumpIdx, true);

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
			// 診断: 研ぎを進めずに「区域が仕上がった」演出だけを出す(光と音が見える/聞こえるかを、判定と切り離して確かめる)
			if (ImGui::Button("Test grind segment glint (random segment, wheel side)"))
			{
				const int seg = (int)frand(0.0f, (float)ForgingSim::NSEG) % ForgingSim::NSEG;
				SpawnGlint(seg, frand(0.0f, 1.0f), GRIND_SEG_FLASH_SEC, 1.0f, true);
				Audio::Play(Audio::SE_SEG_DONE, GRIND_SEG_SE_VOLUME);
			}
			// 焼き戻しの色(刃先の位置 0..1)。スライダで全セルを同じ色にする=色の並びを待たずに確かめられる(0 = 色なし)
			float preview = TemperAvg();
			if (ImGui::SliderFloat("Temper (all cells)", &preview, 0.0f, 1.0f, "%.2f"))
				for (float& t : m_temperCell) t = preview;
			ImGui::SameLine(); ImGui::Text("score %.2f  slide %.2f", TemperScore(), m_temperU);
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
			ImGui::SliderFloat("Reflection while hot", &m_wpHotReflect, 0.0f, 1.0f, "%.2f");	// 1=物理どおり(反射+発光)。0=旧の「熱いと反射を消す」
			ImGui::SliderFloat("Hot gain",  &m_wpHotGain,  0.3f, 6.0f, "%.2f");	// 高=通体が光る
			ImGui::SliderFloat("Hot texture", &m_wpHotTint,   0.0f, 1.0f, "%.2f");	// 低=光っていても鋼の濃淡が見える
			ImGui::SliderFloat("Hot bumps",   &m_wpHotEmisMin, 0.0f, 1.0f, "%.2f");	// 低=法線マップの凹凸が光に出る
			ImGui::Checkbox("Blade: reflection", &m_wpShowReflect); ImGui::SameLine();	// 診断: 白く見える原因が反射か発光かを切り分ける
			ImGui::Checkbox("Blade: glow", &m_wpShowGlow);	// 発光全体の明るさ(白飛び防止)
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
			ImGui::SliderFloat("Half-turn sec",  &m_flipTurnSec,  0.3f, 2.0f, "%.2f");	// F を押し続けて裏返るまでの秒(大=重い)
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
			ImGui::Text("grab focus: %s", m_grabFocus >= 0 ? INTERACTABLES[m_grabFocus].propKey : "-");
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
		TestCutControls();	// 試し切り(藁人形の置き場所・斬撃・断片の物理。TestCut.cpp)

		ImGui::End();
	}
	DrawGrassMapPreview();	// 草の踏み跡の貼图(F1 のチェックで ON。F1 を閉じて歩きながらも見られる)

	m_fade.Draw();	// 最後に全画面の黒幕(前景層)を重ねる=遷移の淡入淡出
}


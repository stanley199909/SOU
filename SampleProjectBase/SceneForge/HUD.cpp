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
		static const char* LABELS[(int)ModeItem::Count] = { (const char*)u8"チュートリアル", (const char*)u8"ガイド付き", (const char*)u8"通常モード", (const char*)u8"ゲームを終了する" };
		static const char* DESCS[(int)ModeItem::Count]  = { (const char*)u8"はじめての方へ。工程を一つずつ選んで、操作を順に覚えます",
		                                                    (const char*)u8"はじめて遊ぶ方へ。最初から最後まで、遊びながら操作を教えます",
		                                                    (const char*)u8"案内は最小限。自分の目と耳で鍛冶を進めます",
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
	const ImU32 SEG_DONE_COL   = IM_COL32(52, 128, 52, 255);	// 済んだ区域の格子(緑のインク。今の面の朱/他の面の墨と見分けが付く)
	// 鍛造/研ぎ: 刃の区域(NSEG)ごとに1格子 = その区域の進み(0..満)。どこが足りないか一目で分かる(2026-10-26 ユーザー要望:
	//   旧は「全体の進み」を左から順に埋めていた=格子の位置と刃の場所が対応しなかった)。焼入れ: 1本の棒。
	//   格子の左右は画面上の刃の左右に合わせる(SegLowEndOnRight)。済んでいない区域は満タンに見せない(SEG_UNFINISHED_MAX)。
	struct SubBar { const char* name; float prog; bool active; bool segmented = false; float seg[ForgingSim::NSEG] = {}; };
	SubBar subs[ForgingSim::NSIDES];
	int nSub = 0;
	const StepName nowType = CurrentStep().type;
	const float SEG_UNFINISHED_MAX = 0.9f;	// 区域の平均が 99% でも済んでいなければ、格子は 9 割まで(満タン=済 を保証)
	auto segBar = [&](const char* name, bool active, int side, bool grind) {
		SubBar b{ name, 0.0f, active, true };
		for (int s = 0; s < ForgingSim::NSEG; ++s)
		{
			const bool  done = grind ? m_forging.SharpDoneOf(side, s)  : m_forging.SegDoneOf(side, s);
			const float prog = grind ? m_forging.SharpRatioOf(side, s) : m_forging.SegProgOf(side, s);
			b.seg[s] = done ? 1.0f : fminf(prog, SEG_UNFINISHED_MAX);
		}
		return b;
	};
	if (nowType == StepName::Forge)
	{
		subs[nSub++] = segBar((const char*)u8"表", m_forging.Side() == 0, 0, false);
		subs[nSub++] = segBar((const char*)u8"裏", m_forging.Side() == 1, 1, false);
	}
	else if (nowType == StepName::Grind)
	{
		// 両側の刃(Fキー長押しで裏返して研ぐ面を変える)。今研いでいる面を濃く
		subs[nSub++] = segBar((const char*)u8"表", GrindSide() == 0, 0, true);
		subs[nSub++] = segBar((const char*)u8"裏", GrindSide() == 1, 1, true);
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
			if (!subs[k].segmented)	// 1本の棒(焼入れ)
			{
				dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), BAR_BG_COL);
				dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * subs[k].prog, by + bh), sc);
				dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + bh), BAR_EDGE_COL, 0.0f, 0, BAR_EDGE_W);
				continue;
			}
			// 区域ごとの格子(鍛造/研ぎ): 格子 c = 画面で左から c 番目の区域。その区域の進みだけ埋まる。
			const int   NCELL = ForgingSim::NSEG;
			const float CELL_GAP_RATIO = 0.18f;	// 格子の間の隙間(格子1つの幅に対する比)
			const float cw  = (bx1 - bx0) / NCELL;
			const float gap = cw * CELL_GAP_RATIO;
			const bool  mirrored = SegLowEndOnRight();	// 区域 0(刃の長手の小さい端)が画面の右にある → 格子を左右反転
			for (int c = 0; c < NCELL; ++c)
			{
				const float x0 = bx0 + cw * c + gap * 0.5f, x1 = bx0 + cw * (c + 1) - gap * 0.5f;
				const float fill = subs[k].seg[mirrored ? NCELL - 1 - c : c];
				// 済んだ区域は緑(2026-10-26 ユーザー要望: 「ここはもう加工しなくてよい」と一目で分かる様に)。
				//   済んでいない区域は 9 割で止めてあるので、fill が 1 = 済。
				const ImU32 fillCol = (fill >= 1.0f) ? SEG_DONE_COL : sc;
				dl->AddRectFilled(ImVec2(x0, by), ImVec2(x1, by + bh), BAR_BG_COL);
				if (fill > 0.0f) dl->AddRectFilled(ImVec2(x0, by), ImVec2(x0 + (x1 - x0) * fill, by + bh), fillCol);
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

//--- 工程リストの区域の格子を画面の刃と同じ左右に並べる為: 区域 0 の端(刃のローカル長軸の最小)が、反対の端より画面の右にあるか。
//    刃の両端をビュー空間へ運び、透視の画面上の横位置 x/z で比べる(CPU 側は転置しない行列 GetView(false))。
//    刃が無い・端がカメラの後ろにある時は false(既定の並び)。
bool SceneForge::SegLowEndOnRight()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!m_wpOk || !cam) return false;
	const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
	XMFLOAT3 lo((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f), hi = lo;
	(&lo.x)[la] = (&m_wpMin.x)[la];
	(&hi.x)[la] = (&m_wpMax.x)[la];
	XMFLOAT4X4 v4 = cam->GetView(false);
	const XMMATRIX toView = WeaponWorld() * XMLoadFloat4x4(&v4);
	XMFLOAT3 a, b;
	XMStoreFloat3(&a, XMVector3TransformCoord(XMLoadFloat3(&lo), toView));
	XMStoreFloat3(&b, XMVector3TransformCoord(XMLoadFloat3(&hi), toView));
	const float NEAR_Z = 0.05f;	// これより手前(カメラ側)は比べない
	if (a.z <= NEAR_Z || b.z <= NEAR_Z) return false;
	return a.x / a.z > b.x / b.z;
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
//  imgui の標準の窓ではなく、ゲームと同じ羊皮紙+日本語で、項目を絞って描く。
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
	// 光の強さのつまみ(調整値そのものを変える = 保存できる)。値は CottageRender::Data() = 編集シーンのパネルと同じ所
	CottageRender::Settings& light = CottageRender::Data();
	// 値の見せ方: 数値 / 時計(time01 → 6:00 起点の時刻) / 分(秒で持つ値を分で見せる)
	enum class Show { Number, Clock, Minutes };
	struct Slider { const char* label; float* value; float minValue, maxValue; Show show; };
	const float SUN_MAX = 64.0f, FIRE_MAX = 64.0f;	// つまみの右端(編集シーンの上限 128 の半分 = 細かく合わせやすい)
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
		if (hover && click) m_lightPanelSaved = CottageRender::Save();	// Assets/cottage_materials.txt(編集シーンの「Save cottage look」と同じ)
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
	const float PANEL_W_RATIO = 0.42f, CORNER_RATIO = 0.085f;	// 一時停止メニューより少し広い(値の列がある)
	const float HEAD_RATIO = 0.040f, ITEM_RATIO = 0.028f, ROW_RATIO = 0.052f, HEAD_GAP = 1.3f;	// 行は 9 個(2026-10-26 画面/音量を追加)= 画面に収まる行の高さ
	// 分類の見出し(2026-10-26 ユーザー: 画面と音が混ざって分かりにくい)。見出しは選べない=上下の移動は項目だけ
	const float SECTION_RATIO = 0.022f;		// 見出しの文字の高さ(画面高さ比)
	const float SECTION_ROW   = 0.040f;		// 見出し1つ分の高さ(画面高さ比)
	struct Section { OptionItem first; const char* name; };
	const Section SECTIONS[] = {
		{ OptionItem::DisplayMode, (const char*)u8"画面" },
		{ OptionItem::VolMaster,   (const char*)u8"音" },
		{ OptionItem::LessonSpeed, (const char*)u8"操作" },
	};
	const ImU32 INK_SECTION = IM_COL32(120, 70, 40, 230);	// 見出し(項目より淡い茶)
	const ImU32 INK_RULE    = IM_COL32( 60, 42, 28,  60);	// 見出しの下の細い線
	const float INSET_TOP = 0.95f, INSET_BOTTOM = 0.75f, INSET_SIDE = 0.75f;
	const float ARROW_RATIO = 0.011f;		// ◀ ▶ の大きさ(画面高さ比)
	const float VALUE_W_RATIO = 0.115f;		// ◀ と ▶ の間(値の欄)の幅(画面幅比)。「フルスクリーン」が矢印に重ならない幅(2026-10-26)
	const float PREVIEW_GAP = 0.13f;		// パネルの右端から見本の中心まで(画面幅比)
	const float PREVIEW_SCALE = 1.2f;		// 見本の大きさ(案内の図に対する倍率)
	const ImU32 INK_HEAD = IM_COL32( 60,  38,  22, 255);
	const ImU32 INK_ITEM = IM_COL32( 60,  42,  28, 220);
	const ImU32 INK_SEL  = IM_COL32(150,  32,  18, 255);
	const ImU32 INK_OFF  = IM_COL32( 60,  42,  28,  70);	// 端でこれ以上変えられない矢印(薄い)

	dl->AddRectFilled(ImVec2(0, 0), disp, DIM_COL);
	const float corner = disp.y * CORNER_RATIO, row = disp.y * ROW_RATIO;
	const int   n = (int)OptionItem::Count;
	const float secRow = disp.y * SECTION_ROW;
	const float pw = disp.x * PANEL_W_RATIO, ph = corner * INSET_TOP + row * HEAD_GAP + row * n + secRow * _countof(SECTIONS) + corner * INSET_BOTTOM;
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
		for (const Section& sec : SECTIONS)	// この項目から新しい分類が始まる → 見出し + 細い線
		{
			if ((int)sec.first != i) continue;
			const float spx = disp.y * SECTION_RATIO;
			dl->AddText(DebugUI::FontJPFor(spx), spx, ImVec2(xL, cy), INK_SECTION, sec.name);
			const float ly = cy + spx * 1.25f;
			dl->AddLine(ImVec2(xL, ly), ImVec2(xR, ly), INK_RULE, 1.0f);
			cy += secRow;
		}
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
		else if (i == (int)OptionItem::DisplayMode)
		{
			const char* MODE_NAMES[] = { (const char*)u8"ウィンドウ", (const char*)u8"ボーダーレス", (const char*)u8"フルスクリーン" };
			static_assert(_countof(MODE_NAMES) == (size_t)Display::Mode::Count, "one name per display mode");
			steppedRow((const char*)u8"画面モード", MODE_NAMES[m_displayMode], m_displayMode, (int)Display::Mode::Count);
		}
		else if (i == (int)OptionItem::Resolution)	// 3つの画面モードで同じ一覧(Display)
		{
			const Display::Size s = Display::SizeAt(m_displaySizeIdx);
			sprintf_s(val, sizeof(val), "%ux%u", s.w, s.h);
			steppedRow((const char*)u8"解像度", val, m_displaySizeIdx, DisplaySizeCount());
		}
		else if (i == (int)OptionItem::VolMaster || i == (int)OptionItem::VolBgm || i == (int)OptionItem::VolSe)
		{
			const int idx = (i == (int)OptionItem::VolMaster) ? m_volMasterIdx : (i == (int)OptionItem::VolBgm) ? m_volBgmIdx : m_volSeIdx;
			const char* name = (i == (int)OptionItem::VolMaster) ? (const char*)u8"音量（全体）"
			                 : (i == (int)OptionItem::VolBgm)    ? (const char*)u8"音量（BGM）" : (const char*)u8"音量（効果音）";
			sprintf_s(val, sizeof(val), "%d%%", (int)(VolumeOf(idx) * PERCENT + 0.5f));
			steppedRow(name, val, idx, VOLUME_STEP_COUNT);
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

void SceneForge::DrawUI()
{
	// 配置/材質/炭火/カメラの編集はすべて SCENE_STAGE_EDITOR に移設。
	// ゲーム側は焼き込み済みの値で表示するだけ(調整パネルはクリーン)。

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

	if (m_toolsPanelOpen) DrawToolsPanel();	// F4: 検査ツール(見える化・工程へ飛ぶ・進み具合)
	if (m_feelPanelOpen)  DrawFeelPanel();	// F5: 手感の調整(試遊の後に触る数値)
	DrawGrassMapPreview();	// 草の踏み跡の貼图(F4 のチェックで ON。F4 を閉じて歩きながらも見られる)

	m_fade.Draw();	// 最後に全画面の黒幕(前景層)を重ねる=遷移の淡入淡出
}


//====================================================================================
//  設計者用パネル F4(検査ツール)/ F5(手感の調整)
//  2026-10-25: 何でも入っていた F1 の調整窓(imgui の折り畳み見出しの山)を廃止し、1キー=1つの仕事に分けた。
//  F2/F3 と同じ羊皮紙+日本語の見た目。ただしスライダ・コンボを使う為、中身は imgui の部品(=自前で当たり判定を書かない)。
//  見えない値は forge_tuning.txt に残る(滑条を消しても値は消えない。SaveTuning/LoadTuning/TuningRefs は別)。
//====================================================================================
static const float TOOL_TOP_RATIO    = 0.08f;	// パネル上端(画面高さ比)
static const float TOOL_MAX_H_RATIO  = 0.84f;	// これより長い中身はパネル内でスクロール
static const float TOOL_CORNER_RATIO = 0.06f;	// 羊皮紙の隅の大きさ(F2/F3 と同じ)
static const float TOOL_INSET        = 0.85f;	// 文字を隅の飾りから離す量(隅の大きさ比)
static const float TOOL_HEAD_RATIO   = 0.032f;	// 見出しの文字の高さ(画面高さ比)
static const float TOOL_HINT_RATIO   = 0.019f;	// 操作の一言
static const float TOOL_ITEM_RATIO   = 0.020f;	// 部品の文字
static const float TOOL_SIDE_MARGIN  = 0.02f;	// 画面の端からの余白(画面幅比)
static const float TOOLS_W_RATIO     = 0.30f;	// F4 の幅(画面幅比)。左に出す(F2 は右)
static const float FEEL_W_RATIO      = 0.30f;	// F5 の幅。右に出す
// 墨の色(F3 の処理の重さパネルと同じ)
static const ImVec4 TOOL_INK      (60 / 255.0f, 42 / 255.0f, 28 / 255.0f, 0.95f);
static const ImVec4 TOOL_INK_HINT (60 / 255.0f, 42 / 255.0f, 28 / 255.0f, 0.55f);
static const ImVec4 TOOL_INK_HOT  (150 / 255.0f, 32 / 255.0f, 18 / 255.0f, 1.0f);	// 朱: つまみ・チェック
static const ImVec4 TOOL_INK_DONE (40 / 255.0f, 110 / 255.0f, 40 / 255.0f, 1.0f);	// 区域が済んだ=緑の墨
static const int TOOL_NUM_COLORS = 19;	// BeginToolWindow で積む部品の色の数(EndToolWindow で同じ数を戻す)
static ImVec4 ToolInk(float a) { return ImVec4(TOOL_INK.x, TOOL_INK.y, TOOL_INK.z, a); }

void SceneForge::BeginToolWindow(const char* id, const char* title, const char* hint, float leftRatio, float widthRatio)
{
	const ImVec2 disp = ImGui::GetIO().DisplaySize;
	const float corner = disp.y * TOOL_CORNER_RATIO;
	const float width  = disp.x * widthRatio;
	ImGui::SetNextWindowPos(ImVec2(disp.x * leftRatio, disp.y * TOOL_TOP_RATIO), ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, disp.y * TOOL_MAX_H_RATIO));

	// 部品の色を墨と朱に(この窓の間だけ。編集シーンの imgui 窓は元の配色のまま)
	const ImVec4 CLEAR(0.0f, 0.0f, 0.0f, 0.0f);
	const ImGuiCol cols[] = { ImGuiCol_Text, ImGuiCol_TextDisabled, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
	                          ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_CheckMark,
	                          ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
	                          ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
	                          ImGuiCol_Separator, ImGuiCol_PopupBg, ImGuiCol_ScrollbarBg, ImGuiCol_ScrollbarGrab, ImGuiCol_Border };
	const ImVec4 vals[] = { TOOL_INK, TOOL_INK_HINT, ToolInk(0.10f), ToolInk(0.18f), ToolInk(0.26f),
	                        ToolInk(0.70f), TOOL_INK_HOT, TOOL_INK_HOT,
	                        ToolInk(0.12f), ToolInk(0.22f), ToolInk(0.32f),
	                        ToolInk(0.18f), ToolInk(0.26f), ToolInk(0.34f),
	                        ToolInk(0.30f), ImVec4(0.93f, 0.87f, 0.74f, 0.98f), CLEAR, ToolInk(0.30f), CLEAR };
	static_assert(_countof(cols) == _countof(vals), "tool panel colors: one value per slot");
	static_assert(_countof(cols) == TOOL_NUM_COLORS, "EndToolWindow pops TOOL_NUM_COLORS colors");
	for (int i = 0; i < (int)_countof(cols); ++i) ImGui::PushStyleColor(cols[i], vals[i]);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(corner * TOOL_INSET, corner * TOOL_INSET));

	const ImGuiWindowFlags FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
	                             | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings
	                             | ImGuiWindowFlags_AlwaysAutoResize;
	ImGui::Begin(id, nullptr, FLAGS);

	// 下地の羊皮紙(窓の描画リストの最初=部品の下)。窓の切り抜きの外(余白)まで描く
	const ImVec2 pa = ImGui::GetWindowPos();
	const ImVec2 pb(pa.x + ImGui::GetWindowSize().x, pa.y + ImGui::GetWindowSize().y);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(pa, pb, false);
	DrawParchment(dl, m_uiFrame.get(), pa, pb, corner);
	dl->PopClipRect();

	// 見出しと一言(F2/F3 と同じ大きさ)。部品の文字は描く大きさに近い実寸の日本語フォント
	const float headPx = disp.y * TOOL_HEAD_RATIO, hintPx = disp.y * TOOL_HINT_RATIO, itemPx = disp.y * TOOL_ITEM_RATIO;
	ImFont* head = DebugUI::FontJPFor(headPx);
	ImGui::PushFont(head);
	ImGui::SetWindowFontScale(headPx / head->FontSize);
	ImGui::TextUnformatted(title);
	ImGui::PopFont();
	ImFont* item = DebugUI::FontJPFor(itemPx);
	ImGui::PushFont(item);
	ImGui::SetWindowFontScale(hintPx / item->FontSize);
	ImGui::TextDisabled("%s", hint);
	ImGui::SetWindowFontScale(itemPx / item->FontSize);	// 以後の部品はこの大きさ(PopFont は EndToolWindow)
}

void SceneForge::EndToolWindow()
{
	ImGui::PopFont();
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(TOOL_NUM_COLORS);
}

//--- F4 = 検査ツール: 見えない物を見る・工程へ飛ぶ・進み具合を確かめる。スライダはほぼ置かない(数値の調整は F5)
void SceneForge::DrawToolsPanel()
{
	BeginToolWindow("##toolsF4", (const char*)u8"検査ツール", (const char*)u8"F4 で閉じる・チェックは閉じても残る",
	                TOOL_SIDE_MARGIN, TOOLS_W_RATIO);

	// --- 見える化(壁越しの X 線表示。F4 を閉じて歩きながらも見られる) ---
	ImGui::SeparatorText((const char*)u8"見える化");
	ImGui::Checkbox((const char*)u8"衝突の形", &m_showCollision);	// 水色=凸包 / 橙=壁 / 赤=接触中 / 黄=玩家
	ImGui::SameLine();
	ImGui::Checkbox((const char*)u8"操作できる範囲（E）", &m_showInteract);	// 灰=範囲外 / 緑=範囲内 / 黄=E が出ている + 白=目線
	ImGui::Checkbox((const char*)u8"道具の箱（金床・鉄）", &m_showPropBoxes);	// 緑=金床の AABB / 黄=鉄条の箱
	ImGui::SameLine();
	ImGui::Checkbox((const char*)u8"草の踏み跡", &m_showGrassMap);	// 真上から見た踏み跡の貼图を小窓で
	ImGui::Checkbox((const char*)u8"影の範囲", &m_showShadowRange);	// 水色の正方形。線の外は影の計算をしない
	ImGui::SameLine();
	ImGui::TextDisabled((const char*)u8"（%s %.0fm）", ShadowCameraIndoors() ? (const char*)u8"屋内" : (const char*)u8"屋外", SunStage::Data().width);
	ImGui::Checkbox((const char*)u8"切り株の断面（試し切り）", &m_showCutDebug);	// 断片が乗っている切り株の断面(橙の円)
	ImGui::Checkbox((const char*)u8"反射の部屋の箱", &ReflectionProbe::ShowRoomBox());	// 橙の箱 = 映り込みを壁へ投影する部屋(壁の内面に合っているか)
	ImGui::Checkbox((const char*)u8"試遊の計時（右下）", &m_showPlaytestTimer);	// 工程ごとの所要時間。調整値と一緒に保存
	bool vsync = GetVSyncEnabled();
	if (ImGui::Checkbox((const char*)u8"垂直同期（GPU の負荷を抑える）", &vsync)) SetVSyncEnabled(vsync);

	// --- 戸口の敷居のチラつき(屋外の地面と家の地基が 1.4cm で重なる)の確認 ---
	ImGui::SeparatorText((const char*)u8"戸口のチラつき");
	ImGui::Checkbox((const char*)u8"地面の深度バイアス", &m_yardDepthBias);
	ImGui::SameLine();
	ImGui::Checkbox((const char*)u8"地面を隠す", &m_hideYard);
	const int YARD_BIAS_SLIDER_MAX = 4000;
	ImGui::SliderInt((const char*)u8"バイアス（単位）", &m_yardBiasUnits, 0, YARD_BIAS_SLIDER_MAX);

	// --- 工程へ飛ぶ・区域の進み(済んでいない区域は朱=「99% で止まる」の調査用) ---
	if (m_state == GAME_PLAY && m_recipe)
	{
		ImGui::SeparatorText((const char*)u8"工程");
		const int n = (int)m_recipe->steps.size();
		if (m_debugJumpIdx >= n) m_debugJumpIdx = n - 1;
		char cur[32]; sprintf_s(cur, "%d: %s", m_debugJumpIdx + 1, StepKey(m_recipe->steps[m_debugJumpIdx].type));	// 名前は英語の状態キー
		const float JUMP_COMBO_W_RATIO = 0.5f;	// コンボの幅(パネルの中身の幅比)
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * JUMP_COMBO_W_RATIO);
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
		if (ImGui::Button((const char*)u8"この工程へ飛ぶ")) StartAtStep(m_debugJumpIdx, true);

		const char* FACE_NAME[ForgingSim::NSIDES] = { (const char*)u8"表", (const char*)u8"裏" };
		for (int side = 0; side < ForgingSim::NSIDES; ++side)
		{
			ImGui::Text((const char*)u8"鍛造 %s:", FACE_NAME[side]);
			for (int s = 0; s < ForgingSim::NSEG; ++s)
			{
				ImGui::SameLine();
				ImGui::TextColored(m_forging.SegDoneOf(side, s) ? TOOL_INK_DONE : TOOL_INK_HOT, "%3.0f", m_forging.SegProgOf(side, s) * 100.0f);
			}
		}
		// 長手セルごとの進み(打撃が実際に書く値)。左=ローカル長手 0 側。ハンマーの頭が今どのセル位置か=端に届くかの確認
		ImGui::Text((const char*)u8"ハンマーの頭: セル %.2f / %d", StrikeLenCoord(), ForgingSim::NL);
		const float CELL_GAP = 2.0f;	// セルの数字の間(px)
		for (int side = 0; side < ForgingSim::NSIDES; ++side)
		{
			ImGui::Text((const char*)u8"セル %s:", FACE_NAME[side]);
			for (int i = 0; i < ForgingSim::NL; ++i)
			{
				ImGui::SameLine(0.0f, CELL_GAP);
				ImGui::TextColored(m_forging.CellDoneOf(side, i) ? TOOL_INK_DONE : TOOL_INK_HOT, "%2.0f", fminf(m_forging.CellProgOf(side, i) * 100.0f, 99.0f));	// 2桁に収める(済=緑)
			}
		}
		for (int side = 0; side < ForgingSim::NSIDES; ++side)
		{
			ImGui::Text((const char*)u8"研ぎ %s:", FACE_NAME[side]);
			for (int s = 0; s < ForgingSim::NSEG; ++s)
			{
				ImGui::SameLine();
				ImGui::TextColored(m_forging.SharpDoneOf(side, s) ? TOOL_INK_DONE : TOOL_INK_HOT, "%3.0f", m_forging.SharpRatioOf(side, s) * 100.0f);
			}
		}
		// 研ぎの長手セルごとの進み(砥石が実際に削る値)。砥石が今どのセル位置か(wheel)も出す=刀尖/刀根の端まで届くかの確認
		ImGui::Text((const char*)u8"砥石の位置: セル %.2f / %d", GrindLenCoord(), ForgingSim::NL);
		for (int side = 0; side < ForgingSim::NSIDES; ++side)
		{
			ImGui::Text((const char*)u8"研ぎセル %s:", FACE_NAME[side]);
			for (int i = 0; i < ForgingSim::NL; ++i)
			{
				ImGui::SameLine(0.0f, CELL_GAP);
				ImGui::TextColored(m_forging.EdgeCellDoneOf(side, i) ? TOOL_INK_DONE : TOOL_INK_HOT, "%2.0f", fminf(m_forging.EdgeCellProgOf(side, i) * 100.0f, 99.0f));	// 2桁に収める(済=緑)
			}
		}
		// 研ぎを進めずに「区域が仕上がった」演出だけを出す(光と音を、判定と切り離して確かめる)
		if (ImGui::Button((const char*)u8"研ぎの光と音を試す"))
		{
			const int seg = (int)frand(0.0f, (float)ForgingSim::NSEG) % ForgingSim::NSEG;
			SpawnGlint(seg, frand(0.0f, 1.0f), GRIND_SEG_FLASH_SEC, 1.0f, true);
			Audio::Play(Audio::SE_SEG_DONE, GRIND_SEG_SE_VOLUME);
		}
		// 焼き戻しの色(刃先の位置 0..1)。全セルを同じ色にする=色の並びを待たずに確かめられる(0 = 色なし)
		float preview = TemperAvg();
		if (ImGui::SliderFloat((const char*)u8"焼き戻しの色（全体）", &preview, 0.0f, 1.0f, "%.2f"))
			for (float& t : m_temperCell) t = preview;
		ImGui::TextDisabled((const char*)u8"評価 %.2f  刃の位置 %.2f", TemperScore(), m_temperU);
	}

	// --- 試し切り・タイトルの取景(一発のボタンだけ) ---
	ImGui::SeparatorText((const char*)u8"置き場所");
	if (ImGui::Button((const char*)u8"わら人形を目の前に置く")) PlaceCutTargetInFront();
	ImGui::SameLine();
	if (ImGui::Button((const char*)u8"わら人形を戻す")) ResetTestCut();
	if (!m_cutModelError.empty())  ImGui::TextWrapped("dummy: %s", m_cutModelError.c_str());	// モデルが読めない時だけ
	if (!m_hiltModelError.empty()) ImGui::TextWrapped("hilt: %s", m_hiltModelError.c_str());
	// 「この画がいい」と思った所で押す=今のカメラの位置と注視点をタイトルのカメラにする(終了時に保存)
	if (ImGui::Button((const char*)u8"今の視点をタイトルのカメラにする"))
		if (CameraBase* cam = GetObj<CameraBase>("Camera"))
		{
			XMFLOAT3 cp = cam->GetPos(), cl = cam->GetLook();
			m_titleCamPos[0]  = cp.x; m_titleCamPos[1]  = cp.y; m_titleCamPos[2]  = cp.z;
			m_titleCamLook[0] = cl.x; m_titleCamLook[1] = cl.y; m_titleCamLook[2] = cl.z;
		}

	EndToolWindow();
}

//--- F5 = 手感の調整: 試遊の後に実際に触る数値だけ。それ以外の値は forge_tuning.txt / header の既定に残る
void SceneForge::DrawFeelPanel()
{
	const float left = 1.0f - TOOL_SIDE_MARGIN - FEEL_W_RATIO;	// 右寄せ
	BeginToolWindow("##feelF5", (const char*)u8"手感の調整", (const char*)u8"F5 で閉じる・終了時に自動で保存", left, FEEL_W_RATIO);

	if (ImGui::Button((const char*)u8"今すぐ保存")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button((const char*)u8"起動時の値に戻す（F8）")) RestoreTuning();

	// --- 鍛造 ---
	ImGui::SeparatorText((const char*)u8"鍛造");
	ImGui::SliderFloat((const char*)u8"1面の打数", &m_forging.strikesPerFace, 4.0f, 40.0f, "%.0f");
	ImGui::SliderFloat((const char*)u8"照準の感度", &m_aimSens, 0.0006f, 0.0050f, "%.4f");	// 低=重い
	ImGui::SliderFloat((const char*)u8"打つ時の画面の揺れ", &CAM_SHAKE_AMP, 0.0f, 0.2f, "%.3f");

	// --- ハンマー(ばね-ダンパ = 打った後の跳ね返り) ---
	ImGui::SeparatorText((const char*)u8"ハンマー（ばね-ダンパ）");
	ImGui::SliderFloat((const char*)u8"硬さ k", &m_hammer.stiffness, 20.0f, 600.0f, "%.0f");	// 剛性=速さ
	ImGui::SliderFloat((const char*)u8"減衰 c", &m_hammer.damping,   0.0f,  40.0f, "%.2f");	// 小=よく跳ねる
	ImGui::SliderFloat((const char*)u8"質量 m", &m_hammer.mass,      0.2f,  4.0f,  "%.2f");	// 重さ/鈍さ
	ImGui::SliderFloat((const char*)u8"振り下ろし（離した速さ）", &m_hammer.swingSpeedMin,  0.0f, 8.0f,  "%.1f m/s");	// 溜め無しで離した時の下向きの速さ
	ImGui::SliderFloat((const char*)u8"振り下ろし（溜めの分）",   &m_hammer.swingSpeedGain, 0.0f, 8.0f,  "%.1f m/s");	// 溜め切った時に足される速さ
	ImGui::SliderFloat((const char*)u8"振り下ろし（加速度）",     &m_hammer.swingAccel,     0.0f, 40.0f, "%.0f m/s2");	// 腕の押し + 重力
	ImGui::SliderFloat((const char*)u8"反発係数（熱い鉄）",   &m_hammer.restitutionHot,   0.0f, 1.0f, "%.2f");	// 跳ね返る速さ / 当たった速さ
	ImGui::SliderFloat((const char*)u8"反発係数（冷たい鉄）", &m_hammer.restitutionCold,  0.0f, 1.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"反発係数（金床）",     &m_hammer.restitutionAnvil, 0.0f, 1.0f, "%.2f");

	// --- 裏返し(F を押し続ける間の拍) ---
	ImGui::SeparatorText((const char*)u8"裏返し（秒）");
	ImGui::SliderFloat((const char*)u8"火ばさみを取る", &m_flipTongsOutTime, 0.2f, 3.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"刃を掴む",       &m_flipGripTime,     0.1f, 2.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"裏返す",         &m_flipTurnSec,      0.3f, 2.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"火ばさみを戻す", &m_flipPutBackTime,  0.2f, 3.0f, "%.2f");

	// --- 動き(掴む/置く動作・工位への移動・歩く) ---
	ImGui::SeparatorText((const char*)u8"動き");
	ImGui::SliderFloat((const char*)u8"動作の時間（倍率・小=速い）", &m_seqTimeScale, 0.3f, 1.5f, "%.2f");	// 拍どうしの比は表のまま
	ImGui::SliderFloat((const char*)u8"工位への移動の速さ", &m_transSpeed,   0.5f, 6.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"移動の最短（秒）",   &m_transMinTime, 0.1f, 2.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"移動の最長（秒）",   &m_transMaxTime, 0.3f, 4.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"歩く速さ",           &m_walkSpeed,    0.5f, 8.0f, "%.2f");

	// --- 研ぎ ---
	ImGui::SeparatorText((const char*)u8"研ぎ");
	ImGui::SliderFloat((const char*)u8"片側を研ぐ時間（秒）", &m_forging.grindSecondsPerSide, 3.0f, 60.0f, "%.0f");
	ImGui::SliderFloat((const char*)u8"研げる範囲（セル）", &m_forging.grindSpread, 0.3f, 4.0f, "%.2f");	// 砥石が当たった所から長手に広がる幅(小=当てた所だけ)
	ImGui::SliderFloat((const char*)u8"1回踏んだ加速", &m_wheel.pedalImpulse, 0.5f, 6.0f,  "%.2f");
	ImGui::SliderFloat((const char*)u8"最高回転",      &m_wheel.maxSpeed,     2.0f, 30.0f, "%.1f");
	ImGui::SliderFloat((const char*)u8"空転の減速",    &m_wheel.friction,     0.05f, 2.0f, "%.2f");
	ImGui::SliderFloat((const char*)u8"刃のギザギザ（深さ）",  &m_edgeJagAmp,  0.0f, 0.4f, "%.2f");	// 研ぐ前の刃先の欠けの深さ(刃の半分の幅に対する割合)
	ImGui::SliderFloat((const char*)u8"刃のギザギザ（細かさ）", &m_edgeJagFreq, 0.5f, 20.0f, "%.1f");	// 長手セル1つあたりの山の数(画素で切るので細かくできる)

	// --- 試し切り ---
	ImGui::SeparatorText((const char*)u8"試し切り");
	ImGui::SliderFloat((const char*)u8"切る強さ", &m_cutStrikeDamage, 0.1f, 3.0f, "%.2f");	// × 出来 × 集中 = 1太刀の強さ
	ImGui::TextDisabled((const char*)u8"出来の倍率 %.2f", GradeScore());

	EndToolWindow();
}

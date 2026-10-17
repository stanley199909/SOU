#ifndef __DEFINES_H__
#define __DEFINES_H__

#include <assert.h>
#include <Windows.h>
#include <stdarg.h>
#include <stdio.h>

#define APP_TITLE "FORGE"	// window title (fixed: no scene name appended)

// ��ʃT�C�Y
#define SCREEN_WIDTH (1280)
#define SCREEN_HEIGHT (720)

// 内部描画解像度の倍率(レンダースケール / render scale)。3D の場面を「画面 × この倍率」の解像度で描き、
// 最後に画面いっぱいへ引き伸ばす(UI は画面の解像度のまま=文字はぼけない)。
//   1 より大 = 超采样(SSAA, チラつき減・重い) / 1 より小 = 内顯向けに軽く(画素数は倍率の2乗)。
//   2026-10-17 F3 で計測: 屋内の GPU は約 21ms のうち「家」「道具」の画素の計算が大半(画素の数で重さが決まる)
//   → 0.75 = 画素数 56% にして 60fps の予算(16.7ms)へ入れる。少しだけ柔らかくなる分は FXAA が均す。
//   設定画面の「画質」で切り替えられる(標準 = 0.75 / 高画質 = 1.0)。変えると PostProcess::Begin が描画先を作り直す。
constexpr float RENDER_SCALE_STANDARD = 0.75f;
constexpr float RENDER_SCALE_HIGH     = 1.0f;
inline float& RenderScale() { static float scale = RENDER_SCALE_STANDARD; return scale; }	// 今の倍率(全員がここを読む)
inline unsigned int RenderPixels(unsigned int screenPixels) { return (unsigned int)(screenPixels * RenderScale() + 0.5f); }	// 画面の画素数 → 内部の画素数

// ���\�[�X�p�X
#define ASSET(path)	"Assets/"path


inline void Error(const char* format, ...)
{
	va_list arg;
	va_start(arg, format);
	static char buf[1024];
	vsprintf_s(buf, sizeof(buf), format, arg);
	va_end(arg);
	MessageBox(NULL, buf, "Error", MB_OK);
}


#endif // __DEFINES_H__
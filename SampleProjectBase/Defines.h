#ifndef __DEFINES_H__
#define __DEFINES_H__

#include <assert.h>
#include <Windows.h>
#include <stdarg.h>
#include <stdio.h>

#define APP_TITLE "SP31 Shader "

// ��ʃT�C�Y
#define SCREEN_WIDTH (1280)
#define SCREEN_HEIGHT (720)

// SSAA(超采样抗锯齿)倍率。シーンを画面のこの倍の解像度で離屏描画し、合成時に縮小する。
// 運動時の紋理/幾何の両方のチラつきを減らす唯一の手(mip/各向異性では消えない残留分)。
// コストは2乗(2=4倍画素)。核显が重ければ1に下げて無効化。
#define SSAA_SCALE (1)

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
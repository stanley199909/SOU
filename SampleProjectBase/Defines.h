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
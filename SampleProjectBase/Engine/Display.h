#pragma once
#include <Windows.h>

// Screen mode and resolution (the player's settings screen, 2026-10-26).
//   Windowed   = a normal window with a title bar; its client area = the resolution
//   Borderless = a frameless window covering the whole monitor; the picture is drawn at the resolution
//                and stretched to the monitor (same shape, because every offered resolution has the
//                monitor's aspect ratio)
//   Fullscreen = DXGI exclusive fullscreen at the resolution (the monitor switches mode)
// All three modes offer the SAME list of resolutions: common sizes with the monitor's aspect ratio
// that fit on it, plus the monitor's own (native) size.
// A change is only REQUESTED here; Main applies it at the start of the next frame (never in the middle
// of drawing), then resizes the swap chain and every screen-sized target.
// Search: "DXGI ResizeBuffers", "borderless windowed", "SetFullscreenState", "render resolution scaling".
namespace Display
{
	enum class Mode { Windowed, Borderless, Fullscreen, Count };

	struct Size { UINT w, h; };

	void Init(HWND hWnd);                     // after InitDirectX: remember the window, build the resolution list, stop DXGI's own Alt+Enter
	int  SizeCount();                         // resolutions offered (small -> large, the last one = native)
	Size SizeAt(int i);
	int  FindSize(UINT w, UINT h);            // index of that resolution, or of the closest one
	void Request(Mode mode, int sizeIdx);     // applied at the start of the next frame
	bool ApplyPending(UINT& outW, UINT& outH);// true when the back buffer must be resized to outW x outH
	void BufferSize(UINT& w, UINT& h);        // the back buffer = the size the game draws at (UI layout space)
	LPARAM ScaleMouse(LPARAM lParam);         // window client pixel -> back buffer pixel (borderless is stretched)
	void OnActivate(bool active);            // WM_ACTIVATEAPP: exclusive fullscreen is lost on Alt+Tab -> minimize, and re-enter it when the game comes back
	void Shutdown();                          // leave exclusive fullscreen before the swap chain is released
}

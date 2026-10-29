#include "Display.h"
#include "DirectX.h"
#include "Defines.h"
#include <dxgi.h>
#include <vector>
#include <cstdlib>
#include <cmath>

namespace
{
	HWND          g_hWnd = nullptr;
	bool          g_pending = false;
	Display::Mode g_mode = Display::Mode::Windowed;
	int           g_sizeIdx = 0;
	UINT          g_bufW = SCREEN_WIDTH, g_bufH = SCREEN_HEIGHT;   // the window is created at this size (Startup.cpp)
	std::vector<Display::Size> g_sizes;

	// The window styles of each mode (Startup.cpp creates the window with the windowed ones).
	const DWORD WINDOWED_STYLE    = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	const DWORD WINDOWED_EX_STYLE = WS_EX_OVERLAPPEDWINDOW;
	const DWORD BORDERLESS_STYLE  = WS_POPUP;

	// Common resolutions per aspect ratio. Only the list matching the monitor's shape is offered, so a
	// borderless picture stretched to the monitor keeps its shape (16:10 monitor -> 16:10 sizes).
	const Display::Size SIZES_16_9[]  = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
	const Display::Size SIZES_16_10[] = { { 1280, 800 }, { 1440, 900 }, { 1680, 1050 }, { 1920, 1200 }, { 2560, 1600 } };
	const float ASPECT_16_10   = 16.0f / 10.0f;
	const float ASPECT_EPSILON = 0.01f;   // "same shape"

	RECT MonitorRect(bool workArea)
	{
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
		return workArea ? mi.rcWork : mi.rcMonitor;
	}

	// The monitor's desktop resolution from the registry: unlike the monitor rect it does not change while
	// we hold exclusive fullscreen at a lower mode (that made the list shrink to one entry).
	void NativeSize(UINT& w, UINT& h)
	{
		MONITORINFOEX mi = {}; mi.cbSize = sizeof(mi);
		GetMonitorInfo(MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTOPRIMARY), &mi);
		DEVMODE dm = {}; dm.dmSize = sizeof(dm);
		if (EnumDisplaySettings(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
		{
			w = dm.dmPelsWidth; h = dm.dmPelsHeight;
			return;
		}
		const RECT r = MonitorRect(false);
		w = (UINT)(r.right - r.left); h = (UINT)(r.bottom - r.top);
	}

	void BuildSizes()
	{
		g_sizes.clear();
		UINT nw, nh; NativeSize(nw, nh);
		const bool is1610 = fabsf((float)nw / (float)nh - ASPECT_16_10) < ASPECT_EPSILON;
		const Display::Size* list = is1610 ? SIZES_16_10 : SIZES_16_9;
		const int n = is1610 ? (int)_countof(SIZES_16_10) : (int)_countof(SIZES_16_9);
		for (int i = 0; i < n; ++i)
			if (list[i].w < nw && list[i].h <= nh) g_sizes.push_back(list[i]);   // smaller than native
		g_sizes.push_back({ nw, nh });                                          // + native, always last
	}
}

namespace Display
{
	void Init(HWND hWnd)
	{
		g_hWnd = hWnd;
		BuildSizes();
		// DXGI switches to fullscreen on Alt+Enter by itself; that would bypass our settings (and leave the
		// buffers the wrong size), so the settings screen is the only way to change the mode.
		IDXGIFactory* factory = nullptr;
		if (GetSwapChain() && SUCCEEDED(GetSwapChain()->GetParent(__uuidof(IDXGIFactory), (void**)&factory)))
		{
			factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
			factory->Release();
		}
	}

	int  SizeCount() { return (int)g_sizes.size(); }
	Size SizeAt(int i)
	{
		if (g_sizes.empty()) return { SCREEN_WIDTH, SCREEN_HEIGHT };
		return g_sizes[(i < 0) ? 0 : (i >= (int)g_sizes.size() ? (int)g_sizes.size() - 1 : i)];
	}

	int FindSize(UINT w, UINT h)
	{
		int best = 0; long bestD = -1;
		for (int i = 0; i < (int)g_sizes.size(); ++i)
		{
			const long d = labs((long)g_sizes[i].w - (long)w) + labs((long)g_sizes[i].h - (long)h);
			if (bestD < 0 || d < bestD) { best = i; bestD = d; }
		}
		return best;
	}

	void Request(Mode mode, int sizeIdx)
	{
		g_mode    = mode;
		g_sizeIdx = sizeIdx;
		g_pending = true;
	}

	void BufferSize(UINT& w, UINT& h) { w = g_bufW; h = g_bufH; }

	LPARAM ScaleMouse(LPARAM lParam)
	{
		RECT rc;
		if (!g_hWnd || !GetClientRect(g_hWnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return lParam;
		if ((UINT)rc.right == g_bufW && (UINT)rc.bottom == g_bufH) return lParam;   // windowed / fullscreen: 1:1
		const int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
		const int sx = MulDiv(x, (int)g_bufW, rc.right), sy = MulDiv(y, (int)g_bufH, rc.bottom);
		return MAKELPARAM((WORD)(short)sx, (WORD)(short)sy);
	}

	bool ApplyPending(UINT& outW, UINT& outH)
	{
		if (!g_pending || !g_hWnd || !GetSwapChain()) return false;
		g_pending = false;
		IDXGISwapChain* sc = GetSwapChain();

		// Leave exclusive fullscreen first (window styles cannot change while DXGI owns the screen).
		BOOL fullscreen = FALSE;
		sc->GetFullscreenState(&fullscreen, nullptr);
		if (fullscreen && g_mode != Mode::Fullscreen) sc->SetFullscreenState(FALSE, nullptr);

		const Size s = SizeAt(g_sizeIdx);
		Mode mode = g_mode;
		if (mode == Mode::Fullscreen)
		{
			// Coming back from Alt+Tab: the window is minimized and DXGI cannot take the screen from an icon.
			if (IsIconic(g_hWnd)) ShowWindow(g_hWnd, SW_RESTORE);
			// A frameless window under the fullscreen picture: when DXGI drops fullscreen (focus lost) the window
			// it falls back to has no title bar cutting into the picture.
			if (!fullscreen)
			{
				SetWindowLongPtr(g_hWnd, GWL_STYLE, BORDERLESS_STYLE | WS_VISIBLE);
				SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, 0);
				const RECT m = MonitorRect(false);
				SetWindowPos(g_hWnd, HWND_TOP, m.left, m.top, (int)s.w, (int)s.h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
			}
			// ResizeTarget before AND after entering fullscreen: the first picks the display mode, the
			// second (refresh rate 0 = keep) is what DXGI documents to avoid a mode mismatch.
			DXGI_MODE_DESC md = {};
			md.Width = s.w; md.Height = s.h;
			md.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sc->ResizeTarget(&md);
			if (FAILED(sc->SetFullscreenState(TRUE, nullptr)))
				mode = Mode::Borderless;   // the screen cannot be taken now (e.g. not focused): fall back
			else
			{
				md.RefreshRate.Numerator = 0; md.RefreshRate.Denominator = 0;
				sc->ResizeTarget(&md);
			}
		}
		if (mode == Mode::Borderless)
		{
			// The window covers the monitor; the back buffer stays at the chosen resolution and the
			// swap chain stretches it to the window on Present (same aspect ratio -> no distortion).
			const RECT m = MonitorRect(false);
			SetWindowLongPtr(g_hWnd, GWL_STYLE, BORDERLESS_STYLE | WS_VISIBLE);
			SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, 0);
			SetWindowPos(g_hWnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		}
		else if (mode == Mode::Windowed)
		{
			SetWindowLongPtr(g_hWnd, GWL_STYLE, WINDOWED_STYLE | WS_VISIBLE);
			SetWindowLongPtr(g_hWnd, GWL_EXSTYLE, WINDOWED_EX_STYLE);
			RECT r = { 0, 0, (LONG)s.w, (LONG)s.h };
			AdjustWindowRectEx(&r, WINDOWED_STYLE, FALSE, WINDOWED_EX_STYLE);
			const int ww = r.right - r.left, wh = r.bottom - r.top;
			const RECT work = MonitorRect(true);   // centre in the usable area (not under the taskbar)
			const int x = work.left + ((work.right - work.left) - ww) / 2;
			const int y = work.top  + ((work.bottom - work.top) - wh) / 2;
			SetWindowPos(g_hWnd, HWND_NOTOPMOST, x < work.left ? work.left : x, y < work.top ? work.top : y,
			             ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		}
		g_bufW = s.w; g_bufH = s.h;
		outW = s.w; outH = s.h;
		return true;
	}

	void OnActivate(bool active)
	{
		if (g_mode != Mode::Fullscreen || !g_hWnd) return;
		if (!active)
		{
			// DXGI leaves exclusive fullscreen on its own when focus goes; minimizing keeps the half-restored
			// window (wrong size, picture cut off) from showing on the desktop.
			ShowWindow(g_hWnd, SW_MINIMIZE);
		}
		else
			g_pending = true;   // back in front: re-enter fullscreen at the start of the next frame (+ resize the buffers)
	}

	void Shutdown()
	{
		// DXGI requires leaving exclusive fullscreen before the swap chain is destroyed.
		if (GetSwapChain()) GetSwapChain()->SetFullscreenState(FALSE, nullptr);
	}
}

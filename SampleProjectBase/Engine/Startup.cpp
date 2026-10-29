#include <windows.h>
#include "resource.h"	// IDI_APPICON
#include "Defines.h"
#include "Main.h"
#include "Display.h"	// 無枠の全画面: マウス座標をバックバッファの座標へ
#include <stdio.h>
#include <crtdbg.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"


// timeGetTime����̎g�p
#pragma comment(lib, "winmm.lib")

//--- �v���g�^�C�v�錾
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


// �G���g���|�C���g
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	// �R���\�[������
#ifdef _DEBUG	// 調試用のコンソール窓は Debug ビルドだけ(配布する Release 版では黒い窓を出さない)
	AllocConsole();
	FILE* fp;
	// �W���o�͂̊��蓖��
	freopen_s(&fp, "CON", "w", stdout);
#endif

	//--- �ϐ��錾
	WNDCLASSEX wcex;
	HWND hWnd;
	MSG message;

	// �E�B���h�N���X���̐ݒ�
	ZeroMemory(&wcex, sizeof(wcex));
	wcex.hInstance = hInstance;
	wcex.lpszClassName = "Class Name";
	wcex.lpfnWndProc = WndProc;
	wcex.style = CS_CLASSDC | CS_DBLCLKS;
	wcex.cbSize = sizeof(WNDCLASSEX);
	// アプリのアイコン(app.rc に埋め込んだ forge.ico)。大=Alt+Tab 等 / 小=タイトルバーとタスクバー
	wcex.hIcon   = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON),   GetSystemMetrics(SM_CYICON),   0);
	wcex.hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);

	// �E�B���h�E�N���X���̓o�^
	if (!RegisterClassEx(&wcex))
	{
		MessageBox(NULL, "Failed to RegisterClassEx", "Error", MB_OK);
		return 0;
	}

	// �E�B���h�E�̍쐬
	RECT rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
	DWORD style = WS_CAPTION | WS_SYSMENU;
	DWORD exStyle = WS_EX_OVERLAPPEDWINDOW;
	AdjustWindowRectEx(&rect, style, false, exStyle);
	hWnd = CreateWindowEx(
		exStyle, wcex.lpszClassName,
		APP_TITLE, style,
		CW_USEDEFAULT,CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		HWND_DESKTOP,
		NULL, hInstance, NULL
	);

	// �E�B���h�E�̕\��
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	// ����������
	const HRESULT initHr = Init(hWnd, SCREEN_WIDTH, SCREEN_HEIGHT);
	if (FAILED(initHr))
	{
		// 黙って終わらない: 他の PC で起動できない時、原因(HRESULT)を伝えてもらえる様に(2026-10-23 同級生の PC で窓が一瞬で閉じた)
		char msg[128];
		sprintf_s(msg, "Failed to initialize the game (DirectX 11).\nError code: 0x%08X", (unsigned)initHr);
		MessageBoxA(hWnd, msg, "FORGE", MB_OK | MB_ICONERROR);
		Uninit();
		UnregisterClass(wcex.lpszClassName, hInstance);
		return 0;
	}

	//--- FPS����
	timeBeginPeriod(1);
	DWORD countStartTime = timeGetTime();
	DWORD preExecTime = countStartTime;

	//--- �E�B���h�E�̊Ǘ�
	while (1)
	{
		if (PeekMessage(&message, NULL, 0, 0, PM_NOREMOVE))
		{
			if (!GetMessage(&message, NULL, 0, 0))
			{
				break;
			}
			else
			{
				TranslateMessage(&message);
				DispatchMessage(&message);
			}
		}
		else
		{
			DWORD nowTime = timeGetTime();
			float diff = static_cast<float>(nowTime - preExecTime);
			if (diff >= 1000.0f / 60)
			{
				Update(hWnd, diff * 0.001f);
				Draw();
				preExecTime = nowTime;
			}
		}
	}


	// �I����
	timeEndPeriod(1);
	Uninit();
	// �W���o�̓N���[�Y
#ifdef _DEBUG
	fclose(fp);
#endif
	UnregisterClass(wcex.lpszClassName, hInstance);

	return 0;
}

// �E�B���h�E�v���V�[�W��
// ImGuiがマウス・キーボード入力を受け取るためのハンドラ
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// 無枠の全画面では、窓(モニター)の座標をバックバッファ(UI を描く大きさ)の座標へ直してから ImGui へ渡す
	//   (UI はバッファの座標で並べてあり、映す時に窓へ引き伸ばされる。Display::ScaleMouse)
	if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL)
		lParam = Display::ScaleMouse(lParam);
	// 先にImGuiへ入力を渡す(UI上の操作はImGuiが処理する)
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;

	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_ACTIVATEAPP:
		// 排他フルスクリーンで Alt+Tab: 離れる時は最小化、戻った時にフルスクリーンへ入り直す(Display::OnActivate)
		Display::OnActivate(wParam != FALSE);
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}
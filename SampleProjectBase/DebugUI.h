#ifndef __DEBUG_UI_H__
#define __DEBUG_UI_H__

#include <d3d11.h>
#include <Windows.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

// ImGuiによるデバッグ/操作UI
// 使い方：
//   Init()  … 起動時に1回
//   NewFrame() → 各パネルの描画 → Render()  … 毎フレーム
//   Dispose() … 終了時に1回
class DebugUI
{
public:
	static void Init(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* context);
	static void Dispose();

	static void NewFrame();		// ImGuiフレーム開始
	static void Render();		// ImGuiフレーム確定＆描画

	static void DrawPerformance();	// FPSなどの共通パネル

	// デバッグUI(Performance/Scene選択など)の表示切り替え。F1で使用
	static bool IsVisible();
	static void Toggle();

	// ゲームHUD用の追加フォント(Init で読み込む)。読み込めなければ nullptr=既定フォントで代替。
	//   Title = Cinzel-Black(中世ローマ体・見出し)、Body = EB Garamond(英文本文)。
	//   日本語(主人公セリフ)は既定フォント(メイリオ)のまま。
	static ImFont* FontTitle();		// 大見出し(FORGE / FORGED! など)
	static ImFont* FontBody();		// 英文の本文・ボタン
};

#endif // __DEBUG_UI_H__

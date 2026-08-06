#include "DebugUI.h"

//--- デバッグUIの表示フラグ(F1で切替)
static bool s_debugVisible = true;
bool DebugUI::IsVisible() { return s_debugVisible; }
void DebugUI::Toggle()    { s_debugVisible = !s_debugVisible; }

void DebugUI::Init(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// 日本語フォント(メイリオ)を読み込む。無い環境では標準フォントのまま
	io.Fonts->Clear();
	ImFontConfig cfg;
	cfg.OversampleH = 2;
	cfg.OversampleV = 1;
	if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 17.0f, &cfg,
		io.Fonts->GetGlyphRangesJapanese()))
	{
		io.Fonts->AddFontDefault();
	}
	io.Fonts->Build();

	// --- 見た目を整える(角丸・落ち着いた配色) ---
	ImGui::StyleColorsDark();
	ImGuiStyle& s = ImGui::GetStyle();
	s.WindowRounding    = 8.0f;
	s.FrameRounding     = 5.0f;
	s.GrabRounding      = 5.0f;
	s.PopupRounding     = 5.0f;
	s.ScrollbarRounding = 5.0f;
	s.TabRounding       = 5.0f;
	s.WindowPadding     = ImVec2(12, 10);
	s.FramePadding      = ImVec2(8, 4);
	s.ItemSpacing       = ImVec2(8, 7);
	s.WindowBorderSize  = 0.0f;
	s.Colors[ImGuiCol_WindowBg]        = ImVec4(0.07f, 0.07f, 0.09f, 0.94f);
	s.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
	s.Colors[ImGuiCol_Header]          = ImVec4(0.20f, 0.35f, 0.55f, 0.70f);
	s.Colors[ImGuiCol_HeaderHovered]   = ImVec4(0.26f, 0.45f, 0.70f, 0.80f);
	s.Colors[ImGuiCol_Button]          = ImVec4(0.20f, 0.35f, 0.55f, 0.70f);
	s.Colors[ImGuiCol_ButtonHovered]   = ImVec4(0.26f, 0.45f, 0.70f, 0.90f);
	s.Colors[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
	s.Colors[ImGuiCol_SliderGrab]      = ImVec4(0.40f, 0.65f, 1.00f, 0.90f);
	s.Colors[ImGuiCol_CheckMark]       = ImVec4(0.45f, 0.75f, 1.00f, 1.00f);
	s.Colors[ImGuiCol_PlotLines]       = ImVec4(0.45f, 0.85f, 1.00f, 1.00f);

	// バックエンド初期化
	ImGui_ImplWin32_Init(hWnd);
	ImGui_ImplDX11_Init(device, context);
}

void DebugUI::Dispose()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void DebugUI::NewFrame()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void DebugUI::Render()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::DrawPerformance()
{
	ImGuiIO& io = ImGui::GetIO();

	// FPSの履歴(グラフ用)
	static float hist[120] = {};
	static int   idx = 0;
	hist[idx] = io.Framerate;
	idx = (idx + 1) % IM_ARRAYSIZE(hist);

	ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Performance");

	ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
	ImGui::PlotLines("##fps", hist, IM_ARRAYSIZE(hist), idx, nullptr,
		0.0f, 240.0f, ImVec2(-1, 60));

	ImGui::End();
}

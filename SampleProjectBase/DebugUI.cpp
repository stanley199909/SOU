#include "DebugUI.h"

//--- デバッグUIの表示フラグ(F1で切替)。ゲームとしては既定で非表示
static bool s_debugVisible = false;
bool DebugUI::IsVisible() { return s_debugVisible; }
void DebugUI::Toggle()    { s_debugVisible = !s_debugVisible; }

//--- ゲームHUD用の追加フォント。Init で読み込み、ここに保持する
static ImFont* s_fontTitle = nullptr;
static ImFont* s_fontBody  = nullptr;
static ImFont* s_fontJP    = nullptr;
ImFont* DebugUI::FontTitle() { return s_fontTitle; }
ImFont* DebugUI::FontBody()  { return s_fontBody; }
ImFont* DebugUI::FontJP()    { return s_fontJP ? s_fontJP : ImGui::GetFont(); }	// 無ければ既定(メイリオ)

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

	// ゲームHUD用の追加フォント(ラテン文字のみ。大きめの実寸で焼いて拡大ボケを防ぐ)。
	// 見出しは大きく使うので実寸64px、本文は32pxで焼く。無ければ nullptr のまま=既定で代替。
	ImFontConfig latin;
	latin.OversampleH = 3;
	latin.OversampleV = 2;
	s_fontTitle = io.Fonts->AddFontFromFileTTF("Assets/Font/Cinzel-Black.ttf",       64.0f, &latin);
	s_fontBody  = io.Fonts->AddFontFromFileTTF("Assets/Font/EBGaramond-Medium.ttf",  32.0f, &latin);

	// ゲームHUD用の日本語フォント(工程の案内/操作説明/互動の一言)。
	//   明朝体(游明朝 Demibold)=筆の入り抜きがあり、中世の羊皮紙の雰囲気に合う(メイリオはゴシック=現代的)。
	//   漢字を含むので字数が多い → OversampleH=1 で貼图(フォントアトラス)を小さく保つ。
	//   ファイルが無いと imgui は assert するので、先に存在を確かめてから読む。無ければメイリオ太字→既定。
	const float JP_BAKE_PX = 30.0f;	// 焼く実寸(HUD の文字の高さに近い値=拡大ボケが少ない)
	ImFontConfig jp;
	jp.OversampleH = 1;
	jp.OversampleV = 1;
	// 字形範囲: imgui の日本語範囲は「常用漢字+人名用漢字」(約3000字)だけ。それ以外の漢字は豆腐(?)になる。
	//   → ImFontGlyphRangesBuilder で日本語範囲に「HUD で使う範囲外の漢字」を足す。
	//   新しい文言で ? が出たら、その漢字を JP_EXTRA_CHARS に足すだけでよい。
	//   (全CJK範囲を焼くと2万字超=アトラスが数十MBになるので、必要な字だけ足す)
	const char* JP_EXTRA_CHARS = (const char*)u8"叩掴";
	static ImVector<ImWchar> s_jpRanges;	// フォントアトラスを Build するまで生きている必要がある=static
	{
		ImFontGlyphRangesBuilder rb;
		rb.AddRanges(io.Fonts->GetGlyphRangesJapanese());
		rb.AddText(JP_EXTRA_CHARS);
		s_jpRanges.clear();
		rb.BuildRanges(&s_jpRanges);
	}
	const char* JP_FONTS[] = { "C:\\Windows\\Fonts\\yumindb.ttf", "C:\\Windows\\Fonts\\meiryob.ttc" };
	for (const char* path : JP_FONTS)
	{
		if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
		s_fontJP = io.Fonts->AddFontFromFileTTF(path, JP_BAKE_PX, &jp, s_jpRanges.Data);
		if (s_fontJP) break;
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

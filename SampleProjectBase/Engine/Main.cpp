#include "Main.h"
#include "Defines.h"
#include <memory>
#include "DirectX.h"
#include "Geometory.h"
#include "Sprite.h"
#include "Input.h"
#include "Gamepad.h"
#include "InputPrompts.h"
#include "Haptics.h"
#include "GpuProfiler.h"
#include "SceneRoot.h"
#include "PostProcess.h"
#include "DebugUI.h"
#include "Audio.h"
#include "Display.h"
#include "CameraBase.h"

//--- グローバル変数
std::shared_ptr<SceneRoot> g_pScene;
std::shared_ptr<PostProcess> g_pPost;

//--- 画面の大きさが変わった(設定の画面モード/解像度。Display::ApplyPending)。画面の大きさの物を全部作り直す。
//    バックバッファを握っている物(画面の RT)を先に放さないと ResizeBuffers が失敗する。
static void ResizeScreen(UINT width, UINT height)
{
	auto rtv = g_pScene->GetObj<RenderTarget>("RTV");
	GetContext()->OMSetRenderTargets(0, nullptr, nullptr);	// 描画先として繋がったままのバックバッファも放す
	if (rtv) rtv->ReleaseScreen();
	GetSwapChain()->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
	if (rtv) rtv->CreateFromScreen();
	g_pPost->Resize(width, height);	// 場面の描画先と深度は次の PostProcess::Begin が内部解像度で作り直す
	if (CameraBase* cam = g_pScene->GetObj<CameraBase>("Camera"))
		cam->SetAspect((float)width / (float)height);	// 16:10 の画面(無枠で全画面)でも歪まない
}

HRESULT Init(HWND hWnd, UINT width, UINT height)
{
	HRESULT hr;
	hr = InitDirectX(hWnd, width, height, false);
	if (FAILED(hr)) { return hr; }
	Display::Init(hWnd);	// 画面モードの切り替え(設定画面)。DXGI 自身の Alt+Enter は止める
	Geometory::Init();
	Sprite::Init();
	InitInput();
	Gamepad::Init();	// 手柄(SDL3)。失敗しても(=SDL が起動しない)キーボードとマウスで遊べる
	Audio::Init();	// BGMはロード完了後にゲームシーン側で開始する(起動途中で鳴らない)

	// シーン作成
	g_pScene = std::make_shared<SceneRoot>();
	g_pScene->Init();
	SetWindowText(hWnd, APP_TITLE);	// 作品名だけ(シーン名は付けない=切り替えても変わらない)

	// 初期リソース作成
	auto rtv = g_pScene->CreateObj<RenderTarget>("RTV");
	rtv->CreateFromScreen();
	auto dsv = g_pScene->CreateObj<DepthStencil>("DSV");
	// 深度も超采样解像度(離屏シーンRTと寸法一致=SetRenderTargetsが同じ視口を張る)。
	hr = dsv->Create(RenderPixels(width), RenderPixels(height), false);

	SetRenderTargets(1, &rtv, nullptr);	// 深度は内部解像度(画面と寸法が違う)=画面の RT とは組まない。場面は PostProcess::Begin が sceneRT+DSV で張る

	// ポストプロセスの初期化
	g_pPost = std::make_shared<PostProcess>();
	g_pPost->Init(width, height);

	// ImGuiの初期化
	DebugUI::Init(hWnd, GetDevice(), GetContext());

	return hr;
}

void Uninit()
{
	Display::Shutdown();	// 排他的な全画面から抜けてからスワップチェーンを壊す(DXGI の決まり)
	DebugUI::Dispose();
	GpuProfiler::Uninit();
	g_pPost->Uninit();
	g_pPost.reset();
	g_pScene->Uninit();
	g_pScene.reset();
	Audio::Uninit();
	Gamepad::Uninit();
	UninitInput();
	Sprite::Uninit();
	Geometory::Uninit();
	UninitDirectX();
}

void Update(HWND hWnd, float tick)
{
	UINT newW, newH;
	if (Display::ApplyPending(newW, newH)) ResizeScreen(newW, newH);	// 設定で変えた画面モード/解像度(フレームの頭でだけ切り替える)
	UpdateInput();
	Gamepad::Update(tick);
	InputPrompts::Update();	// 最後に触った機器(キーボード・マウス / 手柄)= 操作案内の絵を切り替える
	g_pScene->_update(tick);
	Haptics::Update(tick);	// このフレームに頼まれた震動を混ぜて手柄へ送る(ゲームの更新の後)
	g_pPost->Update(tick);
}

void Draw()
{
	auto rtv = g_pScene->GetObj<RenderTarget>("RTV");
	auto dsv = g_pScene->GetObj<DepthStencil>("DSV");

	GpuProfiler::BeginFrame();	// F3: このフレームの GPU の時間を測り始める
	// シーンをオフスクリーンRTへ描画 → ポストプロセスを掛けて画面へ合成
	g_pPost->Begin(dsv);
	g_pScene->_draw();
	{ GpuProfiler::Scope t((const char*)u8"後処理（ブルーム・露出・FXAA）"); g_pPost->End(rtv); }

	// UIは最後に画面へ直接描画(ポストプロセスの影響を受けない)
	DebugUI::NewFrame();
	g_pScene->DrawUI();		// ゲームHUDは常に表示(設計者用パネル F2〜F5 はシーンの中で切替)
	{ GpuProfiler::Scope t((const char*)u8"画面の UI"); DebugUI::Render(); }
	GpuProfiler::EndFrame();	// 画面へ出す(垂直同期で待つ)前で締める=待ち時間は GPU の仕事に含めない

	SwapDirectX();
}

// EOF
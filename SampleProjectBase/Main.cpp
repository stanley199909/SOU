#include "Main.h"
#include "Defines.h"
#include <memory>
#include "DirectX.h"
#include "Geometory.h"
#include "Sprite.h"
#include "Input.h"
#include "SceneRoot.h"
#include "PostProcess.h"
#include "DebugUI.h"
#include "Audio.h"

//--- グローバル変数
std::shared_ptr<SceneRoot> g_pScene;
std::shared_ptr<PostProcess> g_pPost;

HRESULT Init(HWND hWnd, UINT width, UINT height)
{
	HRESULT hr;
	hr = InitDirectX(hWnd, width, height, false);
	if (FAILED(hr)) { return hr; }
	Geometory::Init();
	Sprite::Init();
	InitInput();
	Audio::Init();

	// シーン作成
	g_pScene = std::make_shared<SceneRoot>();
	g_pScene->Init();
	SetWindowText(hWnd, (APP_TITLE + g_pScene->GetSceneName()).c_str());

	// 初期リソース作成
	auto rtv = g_pScene->CreateObj<RenderTarget>("RTV");
	rtv->CreateFromScreen();
	auto dsv = g_pScene->CreateObj<DepthStencil>("DSV");
	hr = dsv->Create(width, height, false);

	SetRenderTargets(1, &rtv, dsv);

	// ポストプロセスの初期化
	g_pPost = std::make_shared<PostProcess>();
	g_pPost->Init(width, height);

	// ImGuiの初期化
	DebugUI::Init(hWnd, GetDevice(), GetContext());

	return hr;
}

void Uninit()
{
	DebugUI::Dispose();
	g_pPost->Uninit();
	g_pPost.reset();
	g_pScene->Uninit();
	g_pScene.reset();
	Audio::Uninit();
	UninitInput();
	Sprite::Uninit();
	Geometory::Uninit();
	UninitDirectX();
}

void Update(HWND hWnd, float tick)
{
	UpdateInput();
	if (IsKeyTrigger(VK_F1)) DebugUI::Toggle();	// F1でデバッグUIの表示切替
	g_pScene->_update(tick);
	g_pPost->Update(tick);

	if (g_pScene->isSceneChange()) {
		SetWindowText(hWnd, (APP_TITLE + g_pScene->GetSceneName()).c_str());
	}
}

void Draw()
{
	auto rtv = g_pScene->GetObj<RenderTarget>("RTV");
	auto dsv = g_pScene->GetObj<DepthStencil>("DSV");

	// シーンをオフスクリーンRTへ描画 → ポストプロセスを掛けて画面へ合成
	g_pPost->Begin(dsv);
	g_pScene->_draw();
	g_pPost->End(rtv);

	// UIは最後に画面へ直接描画(ポストプロセスの影響を受けない)
	DebugUI::NewFrame();
	if (DebugUI::IsVisible())
	{
		DebugUI::DrawPerformance();
		g_pPost->DrawUI();
	}
	g_pScene->DrawUI();		// ゲームHUDは常に表示(内部でデバッグ用パネルのみ切替)
	DebugUI::Render();

	SwapDirectX();
}

// EOF
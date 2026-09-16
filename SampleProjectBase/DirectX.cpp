#include "DirectX.h"
#include "Texture.h"

//--- �O���[�o���ϐ�
ID3D11Device *g_pDevice;
ID3D11DeviceContext *g_pContext;
IDXGISwapChain *g_pSwapChain;
ID3D11RasterizerState* g_pRasterizerState[3];
ID3D11BlendState* g_pBlendState[BLEND_MAX];
ID3D11SamplerState* g_pSamplerState[SAMPLER_MAX];
ID3D11DepthStencilState* g_pDepthStencilState[DEPTH_MAX];

ID3D11Device* GetDevice()
{
	return g_pDevice;
}
ID3D11DeviceContext* GetContext()
{
	return g_pContext;
}
IDXGISwapChain* GetSwapChain()
{
	return g_pSwapChain;
}

HRESULT InitDirectX(HWND hWnd, UINT width, UINT height, bool fullscreen)
{
	HRESULT	hr = E_FAIL;
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));						// �[���N���A
	sd.BufferDesc.Width = width;						// �o�b�N�o�b�t�@�̕�
	sd.BufferDesc.Height = height;						// �o�b�N�o�b�t�@�̍���
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// �o�b�N�o�b�t�@�t�H�[�}�b�g(R,G,B,A)
	sd.SampleDesc.Count = 1;							// �}���`�T���v���̐�
	sd.BufferDesc.RefreshRate.Numerator = 1000;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	// �o�b�N�o�b�t�@�̎g�p���@
	sd.BufferCount = 1;									// �o�b�N�o�b�t�@�̐�
	sd.OutputWindow = hWnd;								// �֘A�t����E�C���h�E
	sd.Windowed = fullscreen ? FALSE : TRUE;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// �h���C�o�̎��
	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,	// GPU�ŕ`��
		D3D_DRIVER_TYPE_WARP,		// �����x(�ᑬ
		D3D_DRIVER_TYPE_REFERENCE,	// CPU�ŕ`��
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	UINT createDeviceFlags = 0;
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;

	// �@�\���x��
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,		// DirectX11.1�Ή�GPU���x��
		D3D_FEATURE_LEVEL_11_0,		// DirectX11�Ή�GPU���x��
		D3D_FEATURE_LEVEL_10_1,		// DirectX10.1�Ή�GPU���x��
		D3D_FEATURE_LEVEL_10_0,		// DirectX10�Ή�GPU���x��
		D3D_FEATURE_LEVEL_9_3,		// DirectX9.3�Ή�GPU���x��
		D3D_FEATURE_LEVEL_9_2,		// DirectX9.2�Ή�GPU���x��
		D3D_FEATURE_LEVEL_9_1		// Direct9.1�Ή�GPU���x��
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	D3D_DRIVER_TYPE driverType;
	D3D_FEATURE_LEVEL featureLevel;

	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; ++driverTypeIndex)
	{
		driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDeviceAndSwapChain(
			NULL,					// �f�B�X�v���C�f�o�C�X�̃A�_�v�^�iNULL�̏ꍇ�ŏ��Ɍ��������A�_�v�^�j
			driverType,				// �f�o�C�X�h���C�o�̃^�C�v
			NULL,					// �\�t�g�E�F�A���X�^���C�U���g�p����ꍇ�Ɏw�肷��
			createDeviceFlags,		// �f�o�C�X�t���O
			featureLevels,			// �@�\���x��
			numFeatureLevels,		// �@�\���x����
			D3D11_SDK_VERSION,		// 
			&sd,					// �X���b�v�`�F�C���̐ݒ�
			&g_pSwapChain,			// IDXGIDwapChain�C���^�t�F�[�X	
			&g_pDevice,				// ID3D11Device�C���^�t�F�[�X
			&featureLevel,		// �T�|�[�g����Ă���@�\���x��
			&g_pContext);		// �f�o�C�X�R���e�L�X�g
		if (SUCCEEDED(hr)) {
			break;
		}
	}
	if (FAILED(hr)) {
		return hr;
	}


	//--- �J�����O�ݒ�
	D3D11_RASTERIZER_DESC rasterizer = {};
	D3D11_CULL_MODE cull[] = {
		D3D11_CULL_NONE,
		D3D11_CULL_FRONT,
		D3D11_CULL_BACK
	};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.FrontCounterClockwise = true;
	for (int i = 0; i < 3; ++i)
	{
		rasterizer.CullMode = cull[i];
		hr = g_pDevice->CreateRasterizerState(&rasterizer, &g_pRasterizerState[i]);
		if (FAILED(hr)) { return hr; }
	}
	SetCullingMode(D3D11_CULL_NONE);

	//--- �[�x�e�X�g
	// https://tositeru.github.io/ImasaraDX11/part/ZBuffer-and-depth-stencil
	D3D11_DEPTH_STENCIL_DESC dsDesc = {};
	dsDesc.DepthEnable = true;
	dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsDesc.StencilEnable		= true;
	dsDesc.StencilReadMask		= D3D11_DEFAULT_STENCIL_READ_MASK;
	dsDesc.StencilWriteMask		= D3D11_DEFAULT_STENCIL_WRITE_MASK;
	dsDesc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_KEEP;
	dsDesc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_INCR;
	dsDesc.FrontFace.StencilFunc		= D3D11_COMPARISON_GREATER_EQUAL;
	dsDesc.BackFace = dsDesc.FrontFace;
	bool enablePattern[] = {true, true, false};
	D3D11_DEPTH_WRITE_MASK maskPattern[] = {
		D3D11_DEPTH_WRITE_MASK_ALL,
		D3D11_DEPTH_WRITE_MASK_ZERO,
		D3D11_DEPTH_WRITE_MASK_ZERO,
	};
	for (int i = 0; i < DEPTH_MAX; ++i)
	{
		dsDesc.DepthEnable = dsDesc.StencilEnable = enablePattern[i];
		dsDesc.DepthWriteMask = maskPattern[i];
		hr = g_pDevice->CreateDepthStencilState(&dsDesc, &g_pDepthStencilState[i]);
		if (FAILED(hr)) { return hr; }
	}
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);

	//--- �A���t�@�u�����f�B���O
	// https://pgming-ctrl.com/directx11/blend/
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	D3D11_BLEND blend[BLEND_MAX][2] = {
		{D3D11_BLEND_ONE, D3D11_BLEND_ZERO},
		{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA},
		{D3D11_BLEND_ONE, D3D11_BLEND_ONE},
		{D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE},
		{D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_COLOR},
		{D3D11_BLEND_INV_DEST_COLOR, D3D11_BLEND_ONE},
	};
	for (int i = 0; i < BLEND_MAX; ++i)
	{
		blendDesc.RenderTarget[0].SrcBlend = blend[i][0];
		blendDesc.RenderTarget[0].DestBlend = blend[i][1];
		hr = g_pDevice->CreateBlendState(&blendDesc, &g_pBlendState[i]);
		if (FAILED(hr)) { return hr; }
	}
	SetBlendMode(BLEND_ALPHA);

	// �T���v���[
	D3D11_SAMPLER_DESC samplerDesc = {};
	// 各向異性(掠射角の床でmipだけでは残るボケ/チラつきを大幅に軽減)。16x。
	static const UINT ANISO_MAX = 16;
	// ★mip LODバイアス(正=一段ぼかす)。写真/石畳のような高対比・高頻テクスチャは、
	// カメラ移動時に細部が「跳ねる」(混叠)。少しだけ高いmipを選ばせると sparkle が消える。
	// 大=よりチラつかないがボケる。跳ねとボケのトレードオフの調整つまみ。
	static const float MIP_LOD_BIAS = 1.0f;
	D3D11_FILTER filter[] = {
		D3D11_FILTER_ANISOTROPIC,			// SAMPLER_LINEAR: 各向異性フィルタ
		D3D11_FILTER_MIN_MAG_MIP_POINT,
	};
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MaxAnisotropy = ANISO_MAX;
	samplerDesc.MipLODBias = MIP_LOD_BIAS;
	// ★重要: 既定の {} は MaxLOD=0 で mip0 に固定される=mipmapが効かない。全mip鎖を許可する。
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	for (int i = 0; i < SAMPLER_MAX; ++i)
	{
		samplerDesc.Filter = filter[i];
		hr = g_pDevice->CreateSamplerState(&samplerDesc, &g_pSamplerState[i]);
		if (FAILED(hr)) { return hr; }
	}
	SetSamplerState(SAMPLER_LINEAR);

	return S_OK;
}

void UninitDirectX()
{
	for (int i = 0; i < SAMPLER_MAX; ++i)
		g_pSamplerState[i]->Release();
	for(int i = 0; i < BLEND_MAX; ++ i)
		g_pBlendState[i]->Release();
	for(int i = 0; i < 3; ++ i)
		g_pRasterizerState[i]->Release();
	
	g_pContext->ClearState();
	SAFE_RELEASE(g_pContext);

	g_pSwapChain->SetFullscreenState(false, NULL);
	SAFE_RELEASE(g_pSwapChain);

	SAFE_RELEASE(g_pDevice);
}

void SwapDirectX()
{
	g_pSwapChain->Present(0, 0);
}



void SetRenderTargets(UINT num, RenderTarget** ppViews, DepthStencil* pView)
{
	static ID3D11RenderTargetView* rtvs[4];

	if (num > 4) num = 4;
	for (UINT i = 0; i < num; ++i)
		rtvs[i] = ppViews[i]->GetView();
	g_pContext->OMSetRenderTargets(num, rtvs, pView ? pView->GetView() : nullptr);

	// �r���[�|�[�g�̐ݒ�
	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = (float)ppViews[0]->GetWidth();
	vp.Height = (float)ppViews[0]->GetHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_pContext->RSSetViewports(1, &vp);
}

void SetCullingMode(D3D11_CULL_MODE cull)
{
	switch (cull)
	{
	case D3D11_CULL_NONE: g_pContext->RSSetState(g_pRasterizerState[0]); break;
	case D3D11_CULL_FRONT: g_pContext->RSSetState(g_pRasterizerState[1]); break;
	case D3D11_CULL_BACK: g_pContext->RSSetState(g_pRasterizerState[2]); break;
	}
}
void SetDepthTest(DepthState state)
{
	g_pContext->OMSetDepthStencilState(g_pDepthStencilState[state], 0);
}

void SetBlendMode(BlendMode blend)
{
	if (blend < 0 || blend >= BLEND_MAX) return;
	FLOAT blendFactor[4] = { D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, D3D11_BLEND_ZERO };
	g_pContext->OMSetBlendState(g_pBlendState[blend], blendFactor, 0xffffffff);
}
void SetSamplerState(SamplerState state)
{
	if (state < 0 || state >= SAMPLER_MAX) return;
	g_pContext->VSSetSamplers(0, 1, &g_pSamplerState[state]);
	g_pContext->PSSetSamplers(0, 1, &g_pSamplerState[state]);
	g_pContext->HSSetSamplers(0, 1, &g_pSamplerState[state]);
	g_pContext->DSSetSamplers(0, 1, &g_pSamplerState[state]);
}
#include "SceneVisual.h"
#include "Model.h"
#include "CameraBase.h"
#include "LightBase.h"
#include "Input.h"
#include "Sprite.h"

void SceneVisual::Init()
{
	m_time = 0.0f;

	// シェーダーの読み込み処理
	Shader* shader[] = {
		CreateObj<VertexShader>("VS_Object"),
		CreateObj<PixelShader>("PS_TexColor"),
	};
	const char* file[] = {
		"Assets/Shader/VS_Object.cso",
		"Assets/Shader/PS_TexColor.cso",
	};
	int shaderNum = _countof(shader);
	for (int i = 0; i < shaderNum; ++i)
	{
		if (FAILED(shader[i]->Load(file[i])))
		{
			MessageBox(NULL, file[i], "Shader Error", MB_OK);
		}
	}
}
void SceneVisual::Uninit()
{
}
void SceneVisual::Update(float tick)
{
	m_time += tick;
}
void SceneVisual::Draw()
{
	// 事前情報の取得
	Model* pModel = GetObj<Model>("Model");
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	LightBase* pLight = GetObj<LightBase>("Light");

	// 定数バッファに渡す行列の情報
	DirectX::XMFLOAT4X4 mat[3];
	DirectX::XMStoreFloat4x4(&mat[0], DirectX::XMMatrixIdentity());
	mat[1] = pCamera->GetView();
	mat[2] = pCamera->GetProj();
	// 定数バッファに渡すライトの情報
	DirectX::XMFLOAT3 lightDir =
		pLight->GetDirection();
	DirectX::XMFLOAT4 light[] = {
		pLight->GetDiffuse(),
		pLight->GetAmbient(),
		{lightDir.x, lightDir.y, lightDir.z, 0.0f}
	};
	// 定数バッファに渡すカメラの情報
	DirectX::XMFLOAT3 camPos = pCamera->GetPos();
	DirectX::XMFLOAT4 camera[] = {
		{camPos.x, camPos.y, camPos.z, 0.0f}
	};

	// シェーダーの取得
	Shader* shader[] = {
		GetObj<Shader>("VS_Object"),
		GetObj<Shader>("PS_TexColor"),
	};
	int shaderPair[][2] = {
		{0, 1}, // 通常表示
	};

	// 描画
	int drawNum = _countof(shaderPair);
	for (int i = 0; i < drawNum; ++i)
	{
		// 座標の更新
		DirectX::XMStoreFloat4x4(&mat[0],
			DirectX::XMMatrixTranspose(
				DirectX::XMMatrixTranslation(
				(drawNum - 1) * 0.5f - i, 0.0f, 0.0f
				)));

		// 描画
		if (i == 4)
		{
			// 定数バッファの更新
			shader[shaderPair[i][0]]->WriteBuffer(0, mat);
			shader[shaderPair[i][0]]->WriteBuffer(1, light);
			shader[shaderPair[i][1]]->WriteBuffer(0, light);
			shader[shaderPair[i][1]]->WriteBuffer(1, camera);

			Sprite::SetPixelShader(shader[shaderPair[i][1]]);
			Sprite::SetWorld(mat[0]);
			Sprite::SetView(mat[1]);
			Sprite::SetProjection(mat[2]);
			Sprite::SetTexture(nullptr);
			Sprite::Draw();
		}
		else
		{
			mat[0] = mat[0] * pModel->GetScaleBaseMatrix();
			// 定数バッファの更新
			shader[shaderPair[i][0]]->WriteBuffer(0, mat);
			shader[shaderPair[i][0]]->WriteBuffer(1, light);
			shader[shaderPair[i][1]]->WriteBuffer(0, light);
			shader[shaderPair[i][1]]->WriteBuffer(1, camera);

			pModel->SetVertexShader(shader[shaderPair[i][0]]);
			pModel->SetPixelShader(shader[shaderPair[i][1]]);
			pModel->Draw();
		}
	}
}
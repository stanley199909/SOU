#include "WallScene.h"
#include "Model.h"
#include "CameraBase.h"
#include "LightBase.h"
#include "Shader.h"
#include "Geometory.h"
#include "math.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

void WallScene::Init()
{
	// 牛モデル描画用シェーダーの読み込み(SceneVisualと名前が被らないよう別名で作成)
	Shader* shader[] = {
		CreateObj<VertexShader>("VS_Wall"),
		CreateObj<PixelShader>("PS_Wall"),
	};
	const char* file[] = {
		"Assets/Shader/VS_Object.cso",
		"Assets/Shader/PS_TexTint.cso",	// テクスチャに色を掛けて着色するシェーダー
	};
	for (int i = 0; i < _countof(shader); ++i)
	{
		if (FAILED(shader[i]->Load(file[i])))
		{
			MessageBox(NULL, file[i], "Shader Error", MB_OK);
		}
	}

	// プレイヤと敵を離れた位置に配置(y座標は壁の中心と揃える)
	m_player.Init(XMFLOAT3(-4.0f, 0.5f, -4.0f));
	m_enemy.Init(XMFLOAT3(4.0f, 0.5f, 4.0f));

	// --- ⑤ 壁を複数配置 ---
	// 灰色の壁。XMFLOAT3(pos), XMFLOAT4(color), XMFLOAT3(scale)
	XMFLOAT4 wallColor = XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f);
	m_walls.push_back(Box(XMFLOAT3(0.0f, 0.5f, 0.0f), wallColor, XMFLOAT3(1.0f, 1.0f, 6.0f)));	// 中央の縦壁
	m_walls.push_back(Box(XMFLOAT3(-3.0f, 0.5f, 2.0f), wallColor, XMFLOAT3(4.0f, 1.0f, 1.0f)));	// 横壁
	m_walls.push_back(Box(XMFLOAT3(3.0f, 0.5f, -2.0f), wallColor, XMFLOAT3(4.0f, 1.0f, 1.0f)));	// 横壁
	m_walls.push_back(Box(XMFLOAT3(-5.0f, 0.5f, 5.0f), wallColor, XMFLOAT3(1.0f, 1.0f, 3.0f)));	// 隅の壁

	// 各壁の当たり判定範囲を更新
	for (Box& w : m_walls)
	{
		w.UpdateMinMaxPos();
	}
}

void WallScene::Uninit()
{
	// 生成したシェーダーを破棄
	DestroyObj("VS_Wall");
	DestroyObj("PS_Wall");
	m_walls.clear();
}

void WallScene::Update(float tick)
{
	// プレイヤ移動(壁で止まる)
	m_player.Update(tick, m_walls);
	// 敵はプレイヤを追尾(壁に当たっても追い続ける)
	m_enemy.Update(tick, m_player.GetPosition(), m_walls);

	// --- 攻撃判定：敵とプレイヤの距離が近ければ成功 ---
	XMFLOAT3 pp = m_player.GetPosition();
	XMFLOAT3 ep = m_enemy.GetPosition();
	float dx = pp.x - ep.x;
	float dz = pp.z - ep.z;
	float dist = sqrtf(dx * dx + dz * dz);	// 平面上の距離
	m_isHit = (dist <= m_attackDist);
}

void WallScene::Draw()
{
	Model* pModel = GetObj<Model>("Model");			// SceneRootが読み込んだ牛モデル(spot)
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	LightBase* pLight = GetObj<LightBase>("Light");
	Shader* pVS = GetObj<VertexShader>("VS_Wall");
	Shader* pPS = GetObj<PixelShader>("PS_Wall");

	// --- 壁の描画(Boxをそのまま利用) ---
	Geometory::SetView(pCamera->GetView());
	Geometory::SetProjection(pCamera->GetProj());
	for (Box& w : m_walls)
	{
		w.Drow();
	}

	if (!pModel || !pVS || !pPS) return;

	// 定数バッファに渡す情報の準備
	XMFLOAT4X4 mat[3];
	mat[1] = pCamera->GetView();
	mat[2] = pCamera->GetProj();

	XMFLOAT3 lightDir = pLight->GetDirection();
	XMFLOAT4 light[] = {
		pLight->GetDiffuse(),
		pLight->GetAmbient(),
		{ lightDir.x, lightDir.y, lightDir.z, 0.0f },
	};
	XMFLOAT3 camPos = pCamera->GetPos();
	XMFLOAT4 camera[] = {
		{ camPos.x, camPos.y, camPos.z, 0.0f },
	};

	// --- プレイヤと敵を牛モデルで描画 ---
	// プレイヤ：通常は青、攻撃を受けている間は黄色
	XMFLOAT4 playerColor = m_isHit ?
		XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f) :		// 黄(攻撃成功)
		XMFLOAT4(0.2f, 0.4f, 1.0f, 1.0f);		// 青(通常)
	XMFLOAT4 enemyColor = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);	// 赤

	DrawModel(pModel, pVS, pPS, m_player.GetPosition(), m_player.GetAngleY(), m_player.GetDrawScale(), playerColor, mat, light, camera);
	DrawModel(pModel, pVS, pPS, m_enemy.GetPosition(), m_enemy.GetAngleY(), m_enemy.GetDrawScale(), enemyColor, mat, light, camera);
}

void WallScene::DrawModel(Model* model, Shader* vs, Shader* ps,
	DirectX::XMFLOAT3 pos, float angleY, float scale, DirectX::XMFLOAT4 color,
	DirectX::XMFLOAT4X4* mat, DirectX::XMFLOAT4* light, DirectX::XMFLOAT4* camera)
{
	// ワールド行列 = モデル固有スケール * (拡縮 * Y回転 * 平行移動)
	XMMATRIX world =
		XMMatrixScaling(scale, scale, scale) *
		XMMatrixRotationY(angleY) *
		XMMatrixTranslation(pos.x, pos.y, pos.z);
	world = model->GetScaleBaseMatrix() * world;
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));

	// 定数バッファ更新
	vs->WriteBuffer(0, mat);			// VS: ワールド・ビュー・プロジェクション
	ps->WriteBuffer(0, &color);			// PS: 牛の色(青/赤/黄)を渡す

	// 描画
	model->SetVertexShader(vs);
	model->SetPixelShader(ps);
	model->Draw();
}

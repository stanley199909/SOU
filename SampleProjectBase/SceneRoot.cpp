#include "SceneRoot.h"
#include <stdio.h>
#include "CameraDCC.h"
#include "MoveLight.h"
#include "Model.h"
#include "Input.h"
#include "Geometory.h"

#include "StageEditor.h"	// = 鍛冶場ステージ配置エディタ(SCENE_STAGE_EDITOR)
#include "SceneWeaponEdit.h"	// = 武器(目標形状)デザインエディタ(SCENE_FORGE_WEAPON)
#include "SceneForge/SceneForge.h"
#include "DebugUI.h"

#include "DebugLog.h"


#define STR(var) #var

//--- 定数定義
enum SceneKind
{
	SCENE_FORGE,			// 鍛冶ミニゲーム本体(ゲーム)
	SCENE_STAGE_EDITOR,	// 鍛冶場のステージ配置エディタ
	SCENE_FORGE_WEAPON,		// 武器(目標形状)デザインエディタ
	SCENE_MAX				// 終端
};

/// <summary>
/// シーン切り替え
/// デバッグ出力の実装がダサいがC++だとenumの名前を
/// 文字列として取得するのが手間なので今回はこのまま
/// </summary>
void SceneRoot::ChangeScene()
{
	switch (m_index)
	{
	default:
	case SCENE_FORGE:
		AddSubScene<SceneForge>();
		m_sceneName = "SCENE_FORGE";
		break;
	case SCENE_STAGE_EDITOR:
		AddSubScene<SceneStageEditor>();
		m_sceneName = "SCENE_STAGE_EDITOR";
		break;
	case SCENE_FORGE_WEAPON:
		AddSubScene<SceneWeaponEdit>();
		m_sceneName = "SCENE_FORGE_WEAPON";
		break;
	}
	DebugLog::log(DebugLog::INFO_LOG,"SceneName = " + m_sceneName);
	m_isChangeScene = true;
}


//--- 構造体
// @brief シーン情報保存
struct ViewSetting
{
	DirectX::XMFLOAT3 camPos;
	DirectX::XMFLOAT3 camLook;
	DirectX::XMFLOAT3 camUp;
	float lightRadXZ;
	float lightRadY;
	float lightH;
	float lightSV;
	int index;
};
const char* SettingFileName = "Assets/setting.dat";

void SceneRoot::Init()
{
	// 保存データの読み込み
	ViewSetting setting =
	{
		DirectX::XMFLOAT3(0.0f, 1.0f, -5.0f),
		DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f),
		DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f),
		0.0f, DirectX::XM_PIDIV4,
		0.0f, 1.0f,
		SCENE_FORGE
	};
	FILE* fp;
	fopen_s(&fp, SettingFileName, "rb");
	if (fp)
	{
		fread(&setting, sizeof(ViewSetting), 1, fp);
		fclose(fp);
	}

	// カメラの作成
	CameraBase* pCamera = CreateObj<CameraDCC>("Camera");
	pCamera->SetPos(setting.camPos);
	pCamera->SetLook(setting.camLook);
	pCamera->SetUp(setting.camUp);

	// ライトの作成
	MoveLight* pLight = CreateObj<MoveLight>("Light");
	pLight->SetRot(setting.lightRadXZ, setting.lightRadY);
	pLight->SetHSV(setting.lightH, setting.lightSV);
	pLight->UpdateParam();


	// 教材デモ用モデル(spot牛/Rock)は不要シーン(Visual/Wall)専用で、鍛冶ゲームでは未使用。
	// 実行環境に不要な巨大アセットを持ち込まないよう、読み込みは停止(オブジェクトは空で保持)。
	CreateObj<Model>("Model");
	//GetObj<Model>("Model")->Load("Assets/Model/spot/spot.fbx", 1.0f, true);

	CreateObj<Model>("Rock2");
	//GetObj<Model>("Rock2")->Load("Assets/Model/Rock-Set/Rock_2/Rock_2.fbx", 0.005f, true, true);

	// アニメーション用モデル読み込み 重いので見たいとき以外はコメントアウト
	/*Model* pAnimModel = CreateObj<Model>("Akai");
	if (pAnimModel->Load("Assets/Model/Akai/Akai.fbx", 0.01f, true))
	{
		pAnimModel->LoadAnimation("Assets/Model/Akai/Akai_Run.fbx", "Run", true);
		pAnimModel->LoadAnimation("Assets/Model/Akai/Akai_Idle.fbx", "Idle", true);
		pAnimModel->LoadAnimation("Assets/Model/Akai/Akai_Walk.fbx", "Walk", true);
	}*/
	Model* pPlane = CreateObj<Model>("ModelPlane");
	pPlane->Load("Assets/Model/plane/plane.fbx");

	// field.fbx も教材デモ用(鍛冶ゲームは自前の StGround を使う)。読み込み停止。
	CreateObj<Model>("FieldModel");
	//pField->Load("Assets/Model/field/field.fbx", 1.0f, false, true);

	// 起動時は常にゲーム本体(SCENE_FORGE)から。編集シーンへは SHIFT+←/→ で移動。
	// (setting.dat のカメラ/ライトは復元するが、開始シーンはゲーム固定にする)
	m_index = SCENE_FORGE;
	ChangeScene();
}

void SceneRoot::Uninit()
{
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	MoveLight* pLight = GetObj<MoveLight>("Light");
	ViewSetting setting =
	{
		pCamera->GetPos(),
		pCamera->GetLook(),
		pCamera->GetUp(),
		pLight->GetRotXZ(), pLight->GetRotY(),
		pLight->GetH(), pLight->GetSV(),
		m_index
	};
	FILE* fp;
	fopen_s(&fp, SettingFileName, "wb");
	if (fp)
	{
		fwrite(&setting, sizeof(ViewSetting), 1, fp);
		fclose(fp);
	}
}

void SceneRoot::Update(float tick)
{
	m_isChangeScene = false;
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	LightBase* pLight = GetObj<LightBase>("Light");
	if (!IsKeyPress(VK_SHIFT))
	{
		pCamera->Update();
		pLight->Update();
		return;
	}

	// SHIFTキーが押されてれば、シーンの切り替え処理
	int idx = m_index;
	if (IsKeyTrigger(VK_LEFT)) --idx;
	if (IsKeyTrigger(VK_RIGHT)) ++idx;
	if (idx < 0) idx = SCENE_MAX - 1;
	if (idx >= SCENE_MAX) idx = 0;

	if (idx != m_index)
	{
		m_index = idx;
		RemoveSubScene();
		ChangeScene();
	}

	// カメラ初期化
	if (IsKeyTrigger('R'))
	{
		pCamera->SetPos(DirectX::XMFLOAT3(0.0f, 1.0f, -5.0f));
		pCamera->SetLook(DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
		pCamera->SetUp(DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
	}
}
void SceneRoot::Draw()
{
	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	LightBase* pLight = GetObj<LightBase>("Light");

	// 炎・火花シーンでは補助表示(グリッド・軸・ギズモ)を消して芸術的に見せる
	if (m_index == SCENE_FORGE) return;	// ゲームは補助表示OFF。編集シーンはグリッド等を出す

	DirectX::XMFLOAT4X4 fmat;
	DirectX::XMStoreFloat4x4(&fmat, DirectX::XMMatrixIdentity());
	Geometory::SetWorld(fmat);
	Geometory::SetView(pCamera->GetView());
	Geometory::SetProjection(pCamera->GetProj());

	// 網掛け描画
	const int GridSize = 10;
	Geometory::SetColor(DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));
	for (int i = 1; i <= GridSize; ++i)
	{
		float g = (float)i;
		Geometory::AddLine(DirectX::XMFLOAT3(g, 0.0f, -GridSize), DirectX::XMFLOAT3(g, 0.0f, GridSize));
		Geometory::AddLine(DirectX::XMFLOAT3(-g, 0.0f, -GridSize), DirectX::XMFLOAT3(-g, 0.0f, GridSize));
		Geometory::AddLine(DirectX::XMFLOAT3(-GridSize, 0.0f, g), DirectX::XMFLOAT3(GridSize, 0.0f, g));
		Geometory::AddLine(DirectX::XMFLOAT3(-GridSize, 0.0f, -g), DirectX::XMFLOAT3(GridSize, 0.0f, -g));
	}
	// 軸描画
	Geometory::SetColor(DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
	Geometory::AddLine(DirectX::XMFLOAT3(-GridSize, 0.0f, 0.0f), DirectX::XMFLOAT3(GridSize, 0.0f, 0.0f));
	Geometory::SetColor(DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f));
	Geometory::AddLine(DirectX::XMFLOAT3(0.0f, -GridSize, 0.0f), DirectX::XMFLOAT3(0.0f, GridSize, 0.0f));
	Geometory::SetColor(DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f));
	Geometory::AddLine(DirectX::XMFLOAT3(0.0f, 0.0f, -GridSize), DirectX::XMFLOAT3(0.0f, 0.0f, GridSize));

	Geometory::DrawLines();

	// オブジェクト描画
	pCamera->Draw();
	pLight->Draw();
}

void SceneRoot::DrawUI()
{
	// --- シーン選択パネル ---
	// 実在する3シーンに対応(enum SceneKind の順)。旧SP31テンプレの死にラベルは削除済み。
	static const char* names[] = {
		"SceneForge", "Stage Editor", "Weapon Edit"
	};

	// シーン選択パネルはデバッグUI(F1)がONのときだけ表示
	if (DebugUI::IsVisible())
	{
		ImGui::SetNextWindowPos(ImVec2(12, 170), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
		ImGui::Begin("Scene");

		int idx = m_index;
		if (ImGui::Combo("##scene", &idx, names, IM_ARRAYSIZE(names)))
		{
			if (idx != m_index)
			{
				m_index = idx;
				RemoveSubScene();
				ChangeScene();
			}
		}
		ImGui::TextDisabled("Camera: ALT+Drag / R = Reset");
		ImGui::End();
	}

	// --- 現在のシーン固有のUI ---
	if (m_pSubScene) m_pSubScene->DrawUI();
}

bool SceneRoot::isSceneChange()
{
	return m_isChangeScene;
}

std::string SceneRoot::GetSceneName()
{
	return m_sceneName;
}

// Part of SceneForge, split out of the original single SceneForge.cpp.
// This file carries a UTF-8 BOM so the Japanese comments moved from the
// original source keep compiling correctly under MSVC (no C2601/C1075).
// All SceneForge members share the class declaration in SceneForge.h and the
// file-local helpers declared in SceneForge_Internal.h.
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "CameraBase.h"
#include "LightBase.h"
#include "Model.h"
#include "Geometory.h"
#include "Input.h"
#include "DebugUI.h"
#include "Defines.h"
#include "Audio.h"
#include "PostProcess.h"
#include "AimSystem.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

using namespace DirectX;

//--- 共通のモデル描画(ワールド行列を渡すだけ)
void SceneForge::DrawModelWorld(Model* m, const XMMATRIX& world, const XMFLOAT4& tint)
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_ForgeObj");
	PixelShader*  ps  = GetObj<PixelShader>("PS_ForgeObj");
	if (!m || !cam || !vs || !ps) return;

	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView();
	mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	XMFLOAT4 color = tint;	// テクスチャに色を掛ける(既定=白=そのまま)
	vs->WriteBuffer(0, mat);
	ps->WriteBuffer(0, &color);

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	m->SetVertexShader(vs);
	m->SetPixelShader(ps);
	m->Draw();
}

//--- 装飾プロップを読み込み、BaseColorを割当、AABBをキャッシュ
void SceneForge::LoadProp(const char* key, const char* fbx, const char* tex,
                          float targetSize, float px, float py, float pz, float yaw, bool groundSnap)
{
	Model* m = CreateObj<Model>(key);
	if (!m->Load(fbx, 1.0f, false, true)) return;	// 読込失敗ならスキップ(欠品でも落ちない)
	if (tex && tex[0])
	{
		auto t = std::make_shared<Texture>();
		if (SUCCEEDED(t->Create(tex))) m->SetTexture(t);
	}
	Prop p;
	p.key = key;
	p.label = key;
	p.pos[0] = px; p.pos[1] = py; p.pos[2] = pz;
	p.yaw = yaw;
	p.groundSnap = groundSnap;
	m->GetLocalAABB(p.aabbMin, p.aabbMax);	// 地面設置＆サイズ正規化に使う境界箱

	// モデルごとに生サイズがバラバラなので、AABBの最大辺=targetSize になるよう自動スケール
	// (魔法数字を避け、別モデルに差し替えてもサイズが揃う)
	float ex = p.aabbMax.x - p.aabbMin.x;
	float ey = p.aabbMax.y - p.aabbMin.y;
	float ez = p.aabbMax.z - p.aabbMin.z;
	float maxExtent = ex; if (ey > maxExtent) maxExtent = ey; if (ez > maxExtent) maxExtent = ez;
	p.scale = (maxExtent > 1e-4f) ? (targetSize / maxExtent) : targetSize;
	m_props.push_back(p);
}

//--- プロップのワールド行列(編集シーンSceneStageEditorと同一規約=床は常にY=0)。
//    こうすると同じ stage_layout.txt が両シーンで完全に同じ配置になる。
//    groundSnap時: Pos.Y はAABB下面を床(0)に付けてからの「床からの高さ」。
XMMATRIX SceneForge::PropWorld(Prop& p)
{
	Model* m = GetObj<Model>(p.key.c_str());
	XMMATRIX base = m ? (XMMATRIX)m->GetScaleBaseMatrix() : XMMatrixIdentity();
	// まずY=0で組み、pos.Yは最後に足す(=床からの持ち上げ)
	XMMATRIX world = base *
		XMMatrixScaling(p.scale, p.scale, p.scale) *
		XMMatrixRotationY(p.yaw) *
		XMMatrixTranslation(p.pos[0], 0.0f, p.pos[2]);

	if (p.groundSnap)
	{
		float minY = 1e18f;
		for (int i = 0; i < 8; ++i)
		{
			XMFLOAT3 c(
				(i & 1) ? p.aabbMax.x : p.aabbMin.x,
				(i & 2) ? p.aabbMax.y : p.aabbMin.y,
				(i & 4) ? p.aabbMax.z : p.aabbMin.z);
			float y = XMVectorGetY(XMVector3TransformCoord(XMLoadFloat3(&c), world));
			if (y < minY) minY = y;
		}
		world = world * XMMatrixTranslation(0.0f, -minY, 0.0f);	// 底面を床(0)へ
	}
	world = world * XMMatrixTranslation(0.0f, p.pos[1], 0.0f);	// 床からの持ち上げ/絶対Y
	return world;
}

//--- m_props からキーで検索(無ければnullptr)
SceneForge::Prop* SceneForge::GetProp(const char* key)
{
	for (auto& p : m_props) if (p.key == key) return &p;
	return nullptr;
}

//--- 編集シーンが保存した stage_layout.txt を読み、プロップ/炭の配置を上書きする。
//    SceneStageEditor::LoadLayout と同じパーサ(キーが一致するプロップにだけ適用)。
//    'W'(水面)もゲーム側で対応済み(屈折する水。DrawWater)。
void SceneForge::LoadLayout()
{
	FILE* fp = nullptr;
	fopen_s(&fp, "Assets/stage_layout.txt", "r");
	if (!fp) return;
	char line[256];
	bool hasE = false;
	while (fgets(line, sizeof(line), fp))
	{
		if (line[0] == 'P')
		{
			char key[64]; float x, y, z, yaw, sc; int snap;
			if (sscanf_s(line, "P %63s %f %f %f %f %f %d", key, (unsigned)sizeof(key), &x, &y, &z, &yaw, &sc, &snap) == 7)
				if (Prop* p = GetProp(key))
				{ p->pos[0]=x; p->pos[1]=y; p->pos[2]=z; p->yaw=yaw; p->scale=sc; p->groundSnap=(snap!=0); }
		}
		else if (line[0] == 'C')
		{
			float x, y, z, yaw, sx, sy, g; int on;
			if (sscanf_s(line, "C %f %f %f %f %f %f %f %d", &x, &y, &z, &yaw, &sx, &sy, &g, &on) == 8)
			{ m_coalPos[0]=x; m_coalPos[1]=y; m_coalPos[2]=z; m_coalYaw=yaw; m_coalSize[0]=sx; m_coalSize[1]=sy; m_coalGlow=g; m_coalOn=(on!=0); }
		}
		else if (line[0] == 'W')	// 水面(編集シーンで配置した水槽の水)
		{
			float x, y, z, yaw, sx, sy; int on;
			if (sscanf_s(line, "W %f %f %f %f %f %f %d", &x, &y, &z, &yaw, &sx, &sy, &on) == 7)
			{ m_waterPos[0]=x; m_waterPos[1]=y; m_waterPos[2]=z; m_waterYaw=yaw; m_waterSize[0]=sx; m_waterSize[1]=sy; m_waterOn=(on!=0); }
		}
		else if (line[0] == 'E')	// 余燼発生器(編集シーンで調整した値)
		{
			float x, y, z, sx, sy, rt, ri;
			if (sscanf_s(line, "E %f %f %f %f %f %f %f", &x, &y, &z, &sx, &sy, &rt, &ri) == 7)
			{ m_emberPos[0]=x; m_emberPos[1]=y; m_emberPos[2]=z; m_emberArea[0]=sx; m_emberArea[1]=sy; m_emberRate=rt; m_emberRise=ri; hasE=true; }
		}
	}
	// 旧い配置ファイル(E行なし)なら、余燼を炭の位置に合わせておく
	if (!hasE)
	{
		m_emberPos[0]=m_coalPos[0]; m_emberPos[1]=m_coalPos[1]; m_emberPos[2]=m_coalPos[2];
		m_emberArea[0]=m_coalSize[0]*0.85f; m_emberArea[1]=m_coalSize[1]*0.85f;
	}
	fclose(fp);
}

//--- 炉のマテリアルへ、F1で選んだテクスチャを割り当てる
void SceneForge::ApplyForgeTextures()
{
	Model* forge = GetObj<Model>("StForge");
	if (!forge || m_forgeTex.empty()) return;
	for (size_t i = 0; i < m_forgeMatPick.size(); ++i)
	{
		int pick = m_forgeMatPick[i];
		if (pick >= 0 && pick < (int)m_forgeTex.size())
			forge->SetTextureAt(i, m_forgeTex[pick]);
	}
}

//--- 装飾モデルをまとめて描画(不透明。鉄条より先に)
void SceneForge::DrawScenery()
{
	if (!m_showScenery) return;
	ApplyForgeTextures();	// 炉の貼り分けを反映
	for (auto& p : m_props)
	{
		Model* m = GetObj<Model>(p.key.c_str());
		if (!m) continue;
		// 炉だけ、のっぺり感を抑えるため僅かに暗い暖色を掛ける(炉内が煤けて見える)
		XMFLOAT4 tint = (p.key == "StForge")
			? XMFLOAT4(0.80f, 0.76f, 0.72f, 1.0f)
			: XMFLOAT4(1, 1, 1, 1);
		DrawModelWorld(m, PropWorld(p), tint);
	}
	DrawCoalBed();	// 光る炭ベッド(自作)
}

//--- 自作の光る炭ベッド。合成炭テクスチャを明るく描き、時間で明滅させる(Bloomで光る)
void SceneForge::DrawCoalBed()
{
	if (!m_coalOn || !m_coalMesh || !m_coalTex) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Coal");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Coal");
	if (!cam || !vs || !ps) return;

	XMMATRIX world =
		XMMatrixScaling(m_coalSize[0], 1.0f, m_coalSize[1]) *
		XMMatrixRotationY(m_coalYaw) *
		XMMatrixTranslation(m_coalPos[0], m_coalPos[1], m_coalPos[2]);

	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView();
	mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	vs->WriteBuffer(0, mat);

	// 明滅計算はGPU(PS)へ移した。CPUは「素の明るさ」と「時間」を渡すだけ。
	XMFLOAT4 tint(m_coalGlow, m_coalGlow, m_coalGlow, m_time);	// rgb=明るさ, a=時間
	ps->WriteBuffer(0, &tint);

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind(); ps->Bind();
	ps->SetTexture(0, m_coalTex.get());
	m_coalMesh->Draw();
}

//--- 水槽の水面を描画(真の屈折)。
//    直前までに描かれた不透明シーンを PostProcess がスナップショットし、
//    水面PS(PS_Water)がそれを法線でずらしてサンプルする=槽の中が透けて見える。
//    メッシュは炭と同じ ±1 水平板を流用し、world で位置/大きさを与える。
void SceneForge::DrawWater()
{
	if (!m_waterOn || !m_coalMesh || !g_pPost) return;
	CameraBase*   cam   = GetObj<CameraBase>("Camera");
	VertexShader* vs    = GetObj<VertexShader>("VS_Coal");	// pos/uv/col 共通VS
	PixelShader*  ps    = GetObj<PixelShader>("PS_Water");
	DepthStencil* depth = GetObj<DepthStencil>("DSV");
	RenderTarget* scene = g_pPost->GetSceneRT();
	if (!cam || !vs || !ps || !depth || !scene) return;

	// 背後のシーンをスナップショット(屈折元テクスチャ)。この時点で深度バッファには
	// 金床・鉄条まで含めた全不透明シーンの深度が入っている=水深/遮蔽の判定に使える。
	Texture* refr = g_pPost->CaptureScene();
	if (!refr) return;

	XMMATRIX world =
		XMMatrixScaling(m_waterSize[0], 1.0f, m_waterSize[1]) *
		XMMatrixRotationY(m_waterYaw) *
		XMMatrixTranslation(m_waterPos[0], m_waterPos[1], m_waterPos[2]);

	XMFLOAT4X4 mat[3];
	mat[1] = cam->GetView();
	mat[2] = cam->GetProj();
	XMStoreFloat4x4(&mat[0], XMMatrixTranspose(world));
	vs->WriteBuffer(0, mat);

	// 深度を線形化する係数(A=proj._33, B=proj._43)。転置していない生の投影行列から取る。
	XMFLOAT4X4 projNT = cam->GetProj(false);
	XMFLOAT4 cb[2];
	cb[0] = XMFLOAT4(m_time, (float)refr->GetWidth(), (float)refr->GetHeight(), m_waterBump);
	cb[1] = XMFLOAT4(projNT._33, projNT._43, m_waterFoam, m_waterDepthFade);
	ps->WriteBuffer(0, cb);

	// 深度バッファをテクスチャとして読むため、一旦DSVをOMから外す(sceneRTだけ描画先に)。
	// これで「同一リソースを深度書き込みとSRV読みに同時使用」する競合を避ける。
	// 深度テストは無効化し、遮蔽は水PS側で深度を比較して discard で行う。
	SetRenderTargets(1, &scene, nullptr);
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_DISABLE);
	vs->Bind(); ps->Bind();
	ps->SetTexture(0, refr);		// t0 = 屈折元(背後のシーン)
	ps->SetTexture(1, depth);		// t1 = シーン深度(R32_FLOAT)
	m_coalMesh->Draw();

	// SRVを外し、描画先を sceneRT + 深度に戻す(この後の余燼/火花が深度テストできるように)。
	ID3D11ShaderResourceView* pNull[2] = { nullptr, nullptr };
	GetContext()->PSSetShaderResources(0, 2, pNull);
	SetRenderTargets(1, &scene, depth);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

//--- 鍛冶素材の3Dモデルを描画(金床＋ハンマー)
void SceneForge::DrawModelsTest()
{
	if (!m_show3D) return;
	DrawScenery();	// 床・樹桩・金床・炉・風箱・作業台・水槽・道具などを一括描画(金床もここ)
	DrawHammer3D();
}


//====================================================================
//  炭火の余燼(火の粉) : 炭床から持続的に発生し、熱気で上昇して淡出する
//====================================================================
void SceneForge::UpdateEmbers(float tick)
{
	// 発生: 炭がONのとき、炭床(m_coalPos ± m_coalSize)の各所から少しずつ湧かせる
	if (m_coalOn)
	{
		m_emberSpawn += tick * m_emberRate;
		int n = (int)m_emberSpawn;	// 今フレームで出す整数個
		m_emberSpawn -= n;			// 端数は次フレームへ持ち越し
		for (int k = 0; k < n && (int)m_embers.size() < MAX_EMBERS; ++k)
		{
			Spark e = {};
			// 中心に寄せて発生(frand*frandで中央ほど密)。発生器の位置/範囲は配置ファイル駆動
			float rx = frand(-1.0f, 1.0f) * frand(0.0f, 1.0f) * m_emberArea[0];
			float rz = frand(-1.0f, 1.0f) * frand(0.0f, 1.0f) * m_emberArea[1];
			e.pos = XMFLOAT3(m_emberPos[0] + rx, m_emberPos[1] + 0.05f, m_emberPos[2] + rz);
			// ほぼ真上へ、わずかな横ぶれ(火花のような下向き重力はナシ=熱気で上がる)
			e.vel = XMFLOAT3(frand(-0.15f, 0.15f), m_emberRise * frand(0.7f, 1.3f), frand(-0.15f, 0.15f));
			e.maxLife = frand(1.2f, 2.6f);
			e.life = e.maxLife;
			e.size = frand(0.02f, 0.05f);
			m_embers.push_back(e);
		}
	}

	// シミュレート: 浮力で上昇＋ゆらぎ＋寿命で消滅
	for (size_t i = 0; i < m_embers.size(); )
	{
		Spark& e = m_embers[i];
		e.life -= tick;
		if (e.life <= 0.0f) { e = m_embers.back(); m_embers.pop_back(); continue; }
		e.vel.y += 0.4f * tick;												// 浮力(少し加速して上る)
		e.vel.x += sinf(m_time * 3.0f + e.pos.y * 8.0f) * 0.10f * tick;		// 横ゆらぎ
		e.vel.z += cosf(m_time * 2.3f + e.pos.x * 8.0f) * 0.10f * tick;
		e.pos.x += e.vel.x * tick;
		e.pos.y += e.vel.y * tick;
		e.pos.z += e.vel.z * tick;
		++i;
	}
}

//--- 余燼(火の粉)をカメラ向きの丸い光点(ビルボード)で加算描画。火花と同じシェーダー/グロー貼り
void SceneForge::DrawEmbers()
{
	if (m_embers.empty()) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Forge");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Forge");
	if (!cam || !vs || !ps || !m_mesh) return;

	XMFLOAT3 camPos = cam->GetPos();
	XMVECTOR vcam    = XMLoadFloat3(&camPos);
	XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);

	XMFLOAT4X4 camMat[2];
	camMat[0] = cam->GetView();
	camMat[1] = cam->GetProj();
	vs->WriteBuffer(0, camMat);

	int v = 0;
	for (const Spark& e : m_embers)
	{
		float t = e.life / e.maxLife;		// 1→0(消えるほど暗く小さく)
		// 温かい橙色。消えぎわは赤く、細かくチラつく
		float fl = 0.70f + 0.30f * sinf(m_time * 25.0f + e.pos.x * 10.0f);
		float br = t * fl;
		XMFLOAT4 col(1.0f * br, (0.5f * t + 0.1f) * br, 0.12f * t * br, 1.0f);

		// カメラを向く正方形(右up)を作る = 丸いグロー点
		XMVECTOR c     = XMLoadFloat3(&e.pos);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));
		XMVECTOR right = XMVector3Cross(worldUp, toCam);
		if (XMVectorGetX(XMVector3Length(right)) < 0.001f) right = XMVectorSet(1, 0, 0, 0);
		right = XMVector3Normalize(right);
		XMVECTOR up = XMVector3Normalize(XMVector3Cross(toCam, right));

		float sz = e.size * (0.6f + 0.6f * t);	// 消えるほど少し縮む
		XMVECTOR R = XMVectorScale(right, sz);
		XMVECTOR U = XMVectorScale(up,    sz);

		XMFLOAT3 tl, tr, bl, br3;
		XMStoreFloat3(&tl,  XMVectorAdd(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&tr,  XMVectorAdd(XMVectorAdd(c, R), U));
		XMStoreFloat3(&bl,  XMVectorSubtract(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&br3, XMVectorSubtract(XMVectorAdd(c, R), U));

		Vertex* q = &m_vtx[v];
		q[0] = { tl,  XMFLOAT2(0,0), col };
		q[1] = { tr,  XMFLOAT2(1,0), col };
		q[2] = { bl,  XMFLOAT2(0,1), col };
		q[3] = { bl,  XMFLOAT2(0,1), col };
		q[4] = { tr,  XMFLOAT2(1,0), col };
		q[5] = { br3, XMFLOAT2(1,1), col };
		v += 6;
		if (v + 6 > (int)m_vtx.size()) break;
	}
	if (v == 0) return;

	SetBlendMode(BLEND_ADD);
	SetDepthTest(DEPTH_ENABLE_TEST);	// 深度は見るが書かない(半透明の光)
	ps->SetTexture(0, m_glow.get());
	m_mesh->Write(m_vtx.data());
	vs->Bind();
	ps->Bind();
	m_mesh->Draw(v);

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

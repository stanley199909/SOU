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

//--- 高さ場を「平滑な曲面」として描く(戻り値=頂点数)。
//    玩法はセル単位(一锤一格+流動)だが、見た目は方块にならないよう、セル高さを
//    格子の「角(corner)」で周囲セルの平均に均し、その角高さで連続面を張る=滑らか。
//    色は熱色を高さで明暗変調し、照準セルに接する角を少しハイライト。周縁は薄いスカートで底へ閉じる。
int SceneForge::BuildBarMesh()
{
	int v = 0;
	const float half = m_barLen * 0.5f;
	const float ax   = m_barAnchor.x;
	const float az   = m_barAnchor.z;
	const float cy   = m_barAnchor.y;	// 板の底面(砧面)の高さ

	// 角(i,j) i=0..NL, j=0..NW の高さ = 周囲(最大4)セルの平均(=平滑化)
	auto cornerH = [&](int i, int j) -> float
	{
		float s = 0.0f; int n = 0;
		for (int di = -1; di <= 0; ++di)
		for (int dj = -1; dj <= 0; ++dj)
		{
			int ci = i + di, cj = j + dj;
			if (ci < 0 || ci >= NL || cj < 0 || cj >= NW) continue;
			s += m_h[ci][cj]; ++n;
		}
		return (n > 0) ? s / n : 0.0f;
	};
	auto CP = [&](int i, int j) -> XMFLOAT3
	{
		float z = az - half + m_barLen * (i / (float)NL);
		float x = ax - m_barWidth + 2.0f * m_barWidth * (j / (float)NW);
		return XMFLOAT3(x, cy + cornerH(i, j), z);
	};
	auto CC = [&](int i, int j) -> XMFLOAT4
	{
		float h = cornerH(i, j);
		float dmg = 0.0f;
		for (int di = -1; di <= 0; ++di)
		for (int dj = -1; dj <= 0; ++dj)
		{
			int ci = i + di, cj = j + dj;
			if (ci < 0 || ci >= NL || cj < 0 || cj >= NW) continue;
			if (m_dmgF[ci][cj] > dmg) dmg = m_dmgF[ci][cj];
		}
		XMFLOAT4 c = HeatRGB(m_heat, dmg);
		float norm = h / m_hStart;
		if (norm < 0.0f) norm = 0.0f; if (norm > 1.0f) norm = 1.0f;
		float b = 0.26f + 0.74f * norm;
		c.x *= b; c.y *= b; c.z *= b;
		if (m_aimValid && (i == m_aimI || i == m_aimI + 1) && (j == m_aimJ || j == m_aimJ + 1))
		{
			c.x = (c.x + 0.35f > 1) ? 1 : c.x + 0.35f;
			c.y = (c.y + 0.35f > 1) ? 1 : c.y + 0.35f;
			c.z = (c.z + 0.35f > 1) ? 1 : c.z + 0.35f;
		}
		return c;
	};
	auto tri = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c,
	               const XMFLOAT4& ca, const XMFLOAT4& cb, const XMFLOAT4& cc)
	{
		m_barVtx[v++] = { a, XMFLOAT2(0,0), ca };
		m_barVtx[v++] = { b, XMFLOAT2(0,0), cb };
		m_barVtx[v++] = { c, XMFLOAT2(0,0), cc };
	};

	// 上面(平滑面): 角格子で連続。両面描画。
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j)
	{
		XMFLOAT3 p00 = CP(i, j),     p10 = CP(i + 1, j);
		XMFLOAT3 p01 = CP(i, j + 1), p11 = CP(i + 1, j + 1);
		XMFLOAT4 c00 = CC(i, j),     c10 = CC(i + 1, j);
		XMFLOAT4 c01 = CC(i, j + 1), c11 = CC(i + 1, j + 1);
		tri(p00, p01, p11, c00, c01, c11); tri(p00, p11, p10, c00, c11, c10);
		tri(p00, p11, p01, c00, c11, c01); tri(p00, p10, p11, c00, c10, c11);
	}
	// 周縁スカート: 外周の角から底面(cy)へ薄い壁を張り、横から見て開かないように閉じる
	auto skirt = [&](int i0, int j0, int i1, int j1)
	{
		XMFLOAT3 a = CP(i0, j0), b = CP(i1, j1);
		XMFLOAT3 a0 = { a.x, cy, a.z }, b0 = { b.x, cy, b.z };
		XMFLOAT4 ca = CC(i0, j0), cb = CC(i1, j1);
		tri(a, b, b0, ca, cb, cb); tri(a, b0, a0, ca, cb, ca);
		tri(a, b0, b, ca, cb, cb); tri(a, a0, b0, ca, ca, cb);	// 両面
	};
	for (int i = 0; i < NL; ++i) { skirt(i, 0, i + 1, 0); skirt(i, NW, i + 1, NW); }
	for (int j = 0; j < NW; ++j) { skirt(0, j, 0, j + 1); skirt(NL, j, NL, j + 1); }
	return v;
}

//--- 3Dの光る鉄条を描画
void SceneForge::Draw3DBillet()
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Bar");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Bar");
	if (!cam || !vs || !ps || !m_barMesh) return;

	XMFLOAT4X4 cb[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, cb);

	int n = BuildBarMesh();
	if (n <= 0) return;
	m_barMesh->Write(m_barVtx.data());

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind();
	ps->Bind();
	m_barMesh->Draw(n);
}

//====================================================================
//  武器モーフ: Blenderで作った同拓扑の各段FBXを頂点補間して成形する
//====================================================================
//--- Assets/Model/weapon/stage_0.fbx, stage_1.fbx ... を順に読む。
//    同拓扑補間のため JoinIdenticalVertices は使わない(段ごとに位置が違うと統合結果がズレる)。
void SceneForge::LoadWeaponStages()
{
	m_wpOk = false;
	m_wpStage.clear();
	const char* dir = "Assets/Model/weapon/";
	for (int s = 0; s < 8; ++s)
	{
		char path[256];
		if (s == 0) sprintf_s(path, sizeof(path), "%sstage_0.fbx", dir);
		else        sprintf_s(path, sizeof(path), "%sstage_%d.fbx", dir, s);
		// stage_final.fbx を最終段として許容(stage_1.fbx が無ければ探す)
		FILE* fp = nullptr;
		if (fopen_s(&fp, path, "rb") != 0 || !fp)
		{
			if (s >= 1) { sprintf_s(path, sizeof(path), "%sstage_final.fbx", dir);
			              if (fopen_s(&fp, path, "rb") == 0 && fp) { fclose(fp); }
			              else break; }
			else break;
		}
		else fclose(fp);

		Assimp::Importer imp;
		unsigned int flag = aiProcess_Triangulate | aiProcess_ConvertToLeftHanded | aiProcess_PreTransformVertices;
		const aiScene* sc = imp.ReadFile(path, flag);
		if (!sc || sc->mNumMeshes == 0) break;

		WpStage st;
		std::vector<unsigned int> idx;
		unsigned int base = 0;
		for (unsigned int m = 0; m < sc->mNumMeshes; ++m)
		{
			const aiMesh* me = sc->mMeshes[m];
			for (unsigned int j = 0; j < me->mNumVertices; ++j)
			{
				aiVector3D p = me->mVertices[j];
				aiVector3D n = me->HasNormals() ? me->mNormals[j] : aiVector3D(0, 1, 0);
				st.pos.push_back(XMFLOAT3(p.x, p.y, p.z));
				st.nrm.push_back(XMFLOAT3(n.x, n.y, n.z));
			}
			if (s == 0)	// インデックスは全段共通なので最初の段だけ作る
			{
				for (unsigned int f = 0; f < me->mNumFaces; ++f)
				{
					const aiFace& fa = me->mFaces[f];
					if (fa.mNumIndices != 3) continue;
					idx.push_back(base + fa.mIndices[0]);
					idx.push_back(base + fa.mIndices[1]);
					idx.push_back(base + fa.mIndices[2]);
				}
			}
			base += me->mNumVertices;
		}
		if (s == 0) { m_wpIdx = idx; m_wpN = (int)st.pos.size(); m_wpMin = XMFLOAT3(1e9f,1e9f,1e9f); m_wpMax = XMFLOAT3(-1e9f,-1e9f,-1e9f);
		              for (auto& p : st.pos){ m_wpMin.x=fminf(m_wpMin.x,p.x);m_wpMin.y=fminf(m_wpMin.y,p.y);m_wpMin.z=fminf(m_wpMin.z,p.z);
		                                      m_wpMax.x=fmaxf(m_wpMax.x,p.x);m_wpMax.y=fmaxf(m_wpMax.y,p.y);m_wpMax.z=fmaxf(m_wpMax.z,p.z);} }
		else if ((int)st.pos.size() != m_wpN)
		{
			MessageBox(nullptr, "武器FBXの頂点数が段ごとに一致しません(同拓扑で作り直してください)", "Weapon", MB_OK);
			return;
		}
		m_wpStage.push_back(std::move(st));
	}

	if (m_wpStage.size() < 2 || m_wpN <= 0) return;	// 最低2段必要

	m_wpVtx.resize(m_wpN);
	MeshBuffer::Description d = {};
	d.pVtx = m_wpVtx.data(); d.vtxSize = sizeof(WpVtx); d.vtxCount = (UINT)m_wpN;
	d.pIdx = m_wpIdx.data(); d.idxSize = sizeof(unsigned int); d.idxCount = (UINT)m_wpIdx.size();
	d.isWrite = true; d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_wpMesh = std::make_shared<MeshBuffer>(d);

	// 目標ゴースト用のメッシュ(同じインデックス・頂点数。中身は stage_final を毎フレーム書く)
	m_ghostVtx.resize(m_wpN);
	MeshBuffer::Description gd = d;
	gd.pVtx = m_ghostVtx.data();
	m_ghostMesh = std::make_shared<MeshBuffer>(gd);

	m_wpOk = true;
}

//--- 武器ローカル→ワールドのフィット変換。照準(AimSystem)と描画(BuildWeaponMorph)で共用し、
//    両者が必ず同じ配置を見るようにする(照準と見た目のズレを原理的に無くす)。
XMMATRIX SceneForge::WeaponWorld() const
{
	float ex = m_wpMax.x - m_wpMin.x, ey = m_wpMax.y - m_wpMin.y, ez = m_wpMax.z - m_wpMin.z;
	float maxE = fmaxf(ex, fmaxf(ey, ez)); if (maxE < 1e-5f) maxE = 1.0f;
	float fit = (m_barLen / maxE) * m_wpScale;
	XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
	return
		XMMatrixTranslation(-c.x, -c.y, -c.z) *
		XMMatrixScaling(fit, fit, fit) *
		XMMatrixRotationRollPitchYaw(m_wpPitch, m_wpYaw, m_wpRoll) *
		XMMatrixTranslation(m_barAnchor.x + m_wpOff[0], m_barAnchor.y + m_wpOff[1], m_barAnchor.z + m_wpOff[2]);
}

//--- 各区域の進捗 m_segProg[] で段を補間して m_wpVtx を作る。ローカル→砧面へのフィット変換もCPUで焼く。
//    KCD式の核心: 頂点はその「長手位置が属する区域」の進捗で個別に stage_0→stage_final へ動く。
//    区域分割は AimSystem と同じ「ローカル長手軸」規約なので、高亮する区域＝準心が指す区域＝進む区域。
void SceneForge::BuildWeaponMorph()
{
	if (!m_wpOk) return;
	int ns = (int)m_wpStage.size();

	XMMATRIX world = WeaponWorld();
	XMMATRIX rot   = XMMatrixRotationRollPitchYaw(m_wpPitch, m_wpYaw, m_wpRoll);

	// タイトル等(非プレイ)は全体を一様に m_forgeProg で見せる(F1スライダのプレビュー)。
	const bool  playing = (m_state == GAME_PLAY);
	const int   segAim  = (playing && m_aimValid) ? AimSeg() : -1;	// 準心が鉄の上に無ければ高亮なし
	const float pulse   = 0.5f + 0.5f * sinf(m_time * 8.0f);
	XMFLOAT4 heat = HeatRGB(m_heat, 0.0f);

	for (int i = 0; i < m_wpN; ++i)
	{
		// 頂点のローカル長手位置 → 区域(AimSystemと同一規約)。境界は隣とブレンドして滑らかに。
		const XMFLOAT3& a0 = m_wpStage[0].pos[i];
		int   thisSeg;
		float p;
		if (playing)
		{
			float sc = AimSystem::SegCoordLocal(a0, m_wpMin, m_wpMax, NSEG);	// 0..NSEG
			float fpos = sc - 0.5f;			// 区域中心を基準にした連続座標
			int   s0 = (int)floorf(fpos);
			float ft = fpos - s0;
			int   sa = s0 < 0 ? 0 : (s0 >= NSEG ? NSEG - 1 : s0);
			int   sb = (s0 + 1) < 0 ? 0 : ((s0 + 1) >= NSEG ? NSEG - 1 : (s0 + 1));
			p = m_segProg[sa] + (m_segProg[sb] - m_segProg[sa]) * ft;
			thisSeg = (int)sc; if (thisSeg >= NSEG) thisSeg = NSEG - 1;
		}
		else { p = m_forgeProg; thisSeg = -1; }
		if (p < 0) p = 0; if (p > 1) p = 1;

		// 進捗 p → 段チェーン(stage_0..final)の補間位置
		float g = p * (ns - 1);
		int   k = (int)g; if (k < 0) k = 0; if (k > ns - 2) k = ns - 2;
		float t = g - k; if (t < 0) t = 0; if (t > 1) t = 1;
		const WpStage& A = m_wpStage[k];
		const WpStage& B = m_wpStage[k + 1];

		XMVECTOR pa = XMLoadFloat3(&A.pos[i]), pb = XMLoadFloat3(&B.pos[i]);
		XMVECTOR pp = XMVectorLerp(pa, pb, t);
		pp = XMVector3TransformCoord(pp, world);
		XMVECTOR na = XMLoadFloat3(&A.nrm[i]), nb = XMLoadFloat3(&B.nrm[i]);
		XMVECTOR n = XMVector3Normalize(XMVector3TransformNormal(XMVectorLerp(na, nb, t), rot));
		XMStoreFloat3(&m_wpVtx[i].pos, pp);
		XMStoreFloat3(&m_wpVtx[i].nrm, n);

		// 既定は熱色のみ(KCD式=「叩く場所」を示さない)。Pキーでデバッグ可視化ONの時だけ
		// 「今照準している区域」を青緑で薄く塗る(叩く指示ではなく開発用)。
		XMFLOAT4 col = heat;
		if (m_showAimHi && thisSeg == segAim)
		{
			float b = 0.30f + 0.20f * pulse;
			col.x = col.x * (1.0f - b);
			col.y = col.y + (1.0f - col.y) * b;
			col.z = col.z + (1.0f - col.z) * b;
		}
		m_wpVtx[i].col = col;
	}
}

//--- 武器を描画(発光+簡易ライティング)。
void SceneForge::DrawWeapon()
{
	if (!m_wpOk) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Wp");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Wp");
	if (!cam || !vs || !ps || !m_wpMesh) return;

	XMFLOAT4X4 cb[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, cb);

	BuildWeaponMorph();
	m_wpMesh->Write(m_wpVtx.data());

	SetBlendMode(BLEND_NONE);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind();
	ps->Bind();
	m_wpMesh->Draw();
}


//--- 目標ゴースト: stage_final(最終形)の頂点を WeaponWorld で変換して m_ghostVtx を作る。
//    形は不変なので進捗補間はしない。色は青白い氷色(alphaは基準値、縁強調はPSで行う)。
void SceneForge::BuildGhostMesh()
{
	if (!m_wpOk || m_wpStage.empty()) return;
	const WpStage& F = m_wpStage.back();		// stage_final = 完成形
	XMMATRIX world = WeaponWorld();
	XMMATRIX rot   = XMMatrixRotationRollPitchYaw(m_wpPitch, m_wpYaw, m_wpRoll);
	const XMFLOAT4 tint = { 0.55f, 0.78f, 1.0f, 0.5f };	// 青白い半透明(a=基準)

	for (int i = 0; i < m_wpN; ++i)
	{
		XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&F.pos[i]), world);
		XMVECTOR n = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&F.nrm[i]), rot));
		XMStoreFloat3(&m_ghostVtx[i].pos, p);
		XMStoreFloat3(&m_ghostVtx[i].nrm, n);
		m_ghostVtx[i].col = tint;
	}
}

//--- 完成形の輪郭を半透明で実体に重ねる。実体が到位した区域では重なって見えなくなる=
//    進むほど自然に「埋まって」いく。深度テストは有効(金床に隠れる)だが書き込みはしない
//    (透明が正しく合成され、後続の不透明描画を邪魔しない)。
void SceneForge::DrawGhostTarget()
{
	if (!m_showGhost || !m_wpOk || !m_ghostMesh) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Wp");		// 頂点は武器と同じVSでよい
	PixelShader*  ps  = GetObj<PixelShader>("PS_Ghost");
	if (!cam || !vs || !ps) return;

	XMFLOAT4X4 cb[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, cb);

	BuildGhostMesh();
	m_ghostMesh->Write(m_ghostVtx.data());

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_TEST);	// テストのみ(書き込まない)=透明の重ね描き
	vs->Bind();
	ps->Bind();
	m_ghostMesh->Draw();
	SetBlendMode(BLEND_NONE);			// 後続の不透明描画のために既定へ戻す
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}


//--- 3Dハンマー: 打撃位置の真上に置き、蓄力で上がり打撃で振り下ろす
void SceneForge::DrawHammer3D()
{
	Model* hammer = GetObj<Model>("MdlHammer");
	if (!hammer) return;

	// 準心が当たっているセルの真上にハンマーを置く。準心が板の外に出ても、m_aimWorld/m_aimI/J は
	// 最後に有効だった位置を保持している(UpdateAimは無効時に値を更新しない)ので、そのまま使う=
	// 中央にリセットせずハンマーは最後の位置に留まる(操作の異様感を無くす)。
	// 横位置(XZ)は UpdatePlay で Lerp::Damp 済みの m_hammerPos を読む(格子跳びを吸収)。
	// オフセットは平滑化の目標に既に含まれているので、ここでは足さない。
	float barTop = m_barAnchor.y + m_h[m_aimI][m_aimJ];
	XMFLOAT3 pos = {
		m_hammerPos.x,
		barTop + m_hammerLift + m_hammerOff[1],
		m_hammerPos.z,
	};

	XMMATRIX world =
		XMMatrixScaling(m_hammerScale, m_hammerScale, m_hammerScale) *
		XMMatrixRotationRollPitchYaw(m_hammerRot[0], m_hammerRot[1], m_hammerRot[2]) *
		XMMatrixTranslation(pos.x, pos.y, pos.z);
	world = hammer->GetScaleBaseMatrix() * world;
	DrawModelWorld(hammer, world);
}

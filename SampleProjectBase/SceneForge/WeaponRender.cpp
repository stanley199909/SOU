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
#include "Lerp.h"
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
	// 格子寸法の別名(唯一の定義は ForgingSim)。
	const int NL = ForgingSim::NL, NW = ForgingSim::NW;
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
			s += m_forging.Height(ci, cj); ++n;
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
			float d = m_forging.Damage(ci, cj); if (d > dmg) dmg = d;
		}
		XMFLOAT4 c = HeatRGB(m_forging.Heat(), dmg);
		float norm = h / m_forging.Start();
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
		// stage_<n> を探す。拡張子は .obj / .fbx の両方を許容(Blenderからobjで出せる)。
		// s>=1 で stage_<s> が無ければ stage_final を最終段として許容する。
		auto exists = [](const char* p) { FILE* f = nullptr; if (fopen_s(&f, p, "rb") == 0 && f) { fclose(f); return true; } return false; };
		auto findStage = [&](const char* stem, char* out) -> bool
		{
			sprintf_s(out, 256, "%s%s.obj", dir, stem); if (exists(out)) return true;
			sprintf_s(out, 256, "%s%s.fbx", dir, stem); if (exists(out)) return true;
			return false;
		};
		char path[256];
		char stem[32];
		if (s == 0) strcpy_s(stem, "stage_0");
		else        sprintf_s(stem, sizeof(stem), "stage_%d", s);
		if (!findStage(stem, path))
		{
			if (s >= 1 && findStage("stage_final", path)) {}	// 最終段
			else break;
		}

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
				// UV(真の鋼テクスチャ採样用)。morphでUVは不変なので各段が持つが stage0 のみ使う。
				aiVector3D uv = me->HasTextureCoords(0) ? me->mTextureCoords[0][j] : aiVector3D(0, 0, 0);
				st.uv.push_back(XMFLOAT2(uv.x, uv.y));
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

	// 厚み軸 = 長軸以外の2軸のうち、完成形(stage_final)で一番薄い軸(刃の表裏を貫く方向)。
	// 粗坯(鉄条)は幅と厚みが近く紛らわしいので、薄さがはっきりしている完成形で判定する。
	{
		XMFLOAT3 fmin(1e9f, 1e9f, 1e9f), fmax(-1e9f, -1e9f, -1e9f);
		for (auto& p : m_wpStage.back().pos)
		{
			fmin.x = fminf(fmin.x, p.x); fmin.y = fminf(fmin.y, p.y); fmin.z = fminf(fmin.z, p.z);
			fmax.x = fmaxf(fmax.x, p.x); fmax.y = fmaxf(fmax.y, p.y); fmax.z = fmaxf(fmax.z, p.z);
		}
		m_wpFinMin = fmin; m_wpFinMax = fmax;	// 完成形の箱(研ぎ: 刃先=幅方向の端の判定に使う)
		const float ext[3] = { fmax.x - fmin.x, fmax.y - fmin.y, fmax.z - fmin.z };
		const int longAx = AimSystem::LongAxis(m_wpMin, m_wpMax);
		m_wpThickAxis = -1;
		for (int a = 0; a < 3; ++a)
			if (a != longAx && (m_wpThickAxis < 0 || ext[a] < ext[m_wpThickAxis])) m_wpThickAxis = a;
	}

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
//--- 翻面回転: 刃の「長軸」まわりに m_flipAngle だけ回す。長軸は stage0 AABB の最長辺
//    (AimSystem::LongAxis と同一規約=段分割と一致)。中心を原点に寄せた後に掛けるので、
//    刃はその場で裏返り、位置はずれない。0=表, π=裏。
XMMATRIX SceneForge::WeaponSpin() const
{
	switch (AimSystem::LongAxis(m_wpMin, m_wpMax))
	{
	case 0:  return XMMatrixRotationX(m_flipAngle);
	case 1:  return XMMatrixRotationY(m_flipAngle);
	default: return XMMatrixRotationZ(m_flipAngle);
	}
}

//--- 刃の回転部分 = 翻面(自局所の裏返し) × 向き付け(F1で合わせたRPY) × 工位の揃え(長軸をカメラの左右へ)。
//    WeaponWorld と法線の変換が同じ回転を使う=見た目と陰影が必ず一致する。
XMMATRIX SceneForge::WeaponRot() const
{
	if (m_seqIronOverride) return XMLoadFloat4x4(&m_seqIronRot);	// 拍子表で動かしている間(Sequence.cpp)
	XMMATRIX r = WeaponSpin() *
		XMMatrixRotationRollPitchYaw(m_wpPitch, m_wpYaw, m_wpRoll) *
		XMMatrixRotationY(StationAlignYaw());
	// 手に持っている時: StationAlignYaw で水平の向きは m_heldDir に揃った。残りの「上へ起こす」傾きを足す
	// (水平の向き → m_heldDir への最短回転)。m_heldDir は WorkAnchor→HeldPoint が先に計算している。
	if (m_carrying)
	{
		XMVECTOR d  = XMLoadFloat3(&m_heldDir);
		XMVECTOR dh = XMVectorSet(m_heldDir.x, 0.0f, m_heldDir.z, 0.0f);
		const float HORIZONTAL_EPS = 1e-6f;	// 真上/真下を向いていたら水平成分が無い=傾けようがない
		if (XMVectorGetX(XMVector3LengthSq(dh)) > HORIZONTAL_EPS) r = r * RotationFromTo(dh, d);
	}
	// 淬火で刃を立てる: 今の長軸(ワールド)まわりに回す=平らに寝ていた刃が、刃を下にして縦に立つ
	// (幅の向きが上下になる。どの軸が長軸かはモデルの箱から決まる=ベタ書き無し)。
	// 砥石の研ぎ角も同じ回し方(長軸まわり)。F の裏返しは同じ軸での半回転を足す。
	else if ((m_workAt == Station::Trough && m_quenchTurn > 0.0f) || (m_workAt == Station::Grindstone && (m_grindAngle != 0.0f || m_grindFlipRoll != 0.0f)))
	{
		const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
		const XMVECTOR local = XMVectorSet(la == 0 ? 1.0f : 0.0f, la == 1 ? 1.0f : 0.0f, la == 2 ? 1.0f : 0.0f, 0.0f);
		const XMVECTOR axisW = XMVector3TransformNormal(local, r);	// 今の長軸(ワールド)
		float angle;
		if (m_workAt == Station::Trough) angle = m_quenchTurn * QUENCH_TURN_ANGLE;
		else
		{
			// 研ぎ角の向きを画面に対して一定にする: 長軸のワールドの向きは「どちらの端から置いたか(m_restFlip)」などで
			//   毎回 +StationRight にも -StationRight にもなる → 同じ角度でも傾く向きが局ごとに逆だった(2026-10-08 F5)。
			//   長軸が -StationRight を向いている時は角度の符号を反す=回転は常に +StationRight まわり=マウス上で同じ側が上がる。
			//   裏返し(半回転)は同じ軸まわりなので、傾く向きには影響しない。
			const XMFLOAT3 sr = StationRight();
			const float along = XMVectorGetX(XMVector3Dot(axisW, XMLoadFloat3(&sr)));
			const float tiltSign = (along < 0.0f ? -1.0f : 1.0f) * GRIND_TILT_DIR;
			angle = m_grindFlipRoll + tiltSign * m_grindAngle;	// 砥石: 裏返し(半回転) + 研ぎ角
		}
		r = r * XMMatrixRotationAxis(axisW, angle);
	}
	return r;
}

//--- ローカル厚み軸の「+側の面」が表(面0)か裏(面1)か。翻面回転を除いた向き付けだけで
//    +軸を回し、上(+Y)を向けば +側=表(面0=角0で上を向く面)。
bool SceneForge::PlusIsFront() const
{
	const int ta = m_wpThickAxis;
	if (ta < 0) return true;
	XMVECTOR axis = XMVectorSet(ta == 0 ? 1.0f : 0.0f, ta == 1 ? 1.0f : 0.0f, ta == 2 ? 1.0f : 0.0f, 0.0f);
	XMVECTOR up = XMVector3TransformNormal(axis, XMMatrixRotationRollPitchYaw(m_wpPitch, m_wpYaw, m_wpRoll));
	return XMVectorGetY(up) >= 0.0f;
}

//--- 今研いでいる刃の面 = 砥石へ向いている(下を向いている)面。見た目の向きから決める=研いだ所に光が出る面と必ず一致する。
//    F の裏返し(m_grindFace)で刃が半回転すると、下を向く面が入れ替わる。
int SceneForge::GrindSide() const
{
	const int ta = m_wpThickAxis;
	if (ta < 0) return m_grindFace;
	XMVECTOR axis = XMVectorSet(ta == 0 ? 1.0f : 0.0f, ta == 1 ? 1.0f : 0.0f, ta == 2 ? 1.0f : 0.0f, 0.0f);
	const bool plusDown = XMVectorGetY(XMVector3TransformNormal(axis, WeaponRot())) < 0.0f;	// +側の面が下(砥石)を向いている
	const bool frontDown = (plusDown == PlusIsFront());
	return frontDown ? 0 : 1;
}

//--- 刃の中心から一番低い点までの深さ(m, 正)。今の回転(WeaponRot = 研ぎ角などを含む)で、モデル箱の8隅を
//    フィットの大きさで運んだ最小の y。WeaponWorld が箱の中心を原点に寄せてから回す規約と同じ。
float SceneForge::BladeDepthBelowCentre() const
{
	const float ex = m_wpMax.x - m_wpMin.x, ey = m_wpMax.y - m_wpMin.y, ez = m_wpMax.z - m_wpMin.z;
	const float MIN_EXTENT = 1e-5f;
	const float fit = (m_barLen / fmaxf(fmaxf(ex, fmaxf(ey, ez)), MIN_EXTENT)) * m_wpScale;
	const XMMATRIX rot = WeaponRot();
	float lowest = 0.0f;
	for (int i = 0; i < 8; ++i)
	{
		const XMVECTOR half = XMVectorSet(((i & 1) ? ex : -ex) * 0.5f * fit, ((i & 2) ? ey : -ey) * 0.5f * fit,
		                                  ((i & 4) ? ez : -ez) * 0.5f * fit, 0.0f);
		lowest = fminf(lowest, XMVectorGetY(XMVector3TransformNormal(half, rot)));
	}
	return -lowest;
}

//--- 刃を置く点。工位の作業点に、その工位の「動き」を足す:
//    金床  : F1 の微調整 m_wpOff
//    砥石  : 刃を長軸方向に滑らせ、今研いでいる位置(m_grindU)を砥石の接点に持って来る + 押し当てで少し沈む
//    水槽  : 淬火で刃を立てて(少し持ち上げ)水の中へ沈め、上下に揺する(m_quenchTurn / m_plunge / m_agitate)
XMFLOAT3 SceneForge::WorkAnchor()
{
	if (m_carrying) return HeldPoint();	// 火钳で掴んで運んでいる=手の前(Carry.cpp)
	XMFLOAT3 a = StationBase(m_workAt);
	switch (m_workAt)
	{
	case Station::Anvil:
		a.x += m_wpOff[0]; a.y += m_wpOff[1]; a.z += m_wpOff[2];
		break;
	case Station::Hearth:
	{
		// 炭床に平らに、斜めに寝かせた鉄の中心。先端が炭床の中の決まった点
		// (中心から 長辺方向へ m_hearthTipSide・奥へ m_hearthTipDepth)に来る様に、中心を長軸に沿って半分の長さだけ戻す。
		// 鉄は炭床より長いので、反対の端(掴んだ端)は炉口から斜め手前へはみ出す=実際の鍛冶の置き方。
		// 先端の位置で決めるので、鉄の長さが変わっても先端は炭床から出ない。
		const float half = m_barLen * m_wpScale * 0.5f;
		const XMFLOAT3 r = StationRight(), into = StationInto(), d = HearthDir();
		a.x += r.x * m_hearthTipSide + into.x * m_hearthTipDepth - d.x * half;
		a.z += r.z * m_hearthTipSide + into.z * m_hearthTipDepth - d.z * half;
		break;
	}
	case Station::Grindstone:
	{
		// 長軸は StationRight に揃えてある。長さ方向の位置 u の点を接点へ寄せる=刃を -(u-0.5)*全長 だけ動かす。
		const float len = m_barLen * m_wpScale;	// ワールドでの刃の全長(フィットで最長辺=m_barLen)
		const XMFLOAT3 r = StationRight();
		// 左右反転して置いた(m_restFlip)時はローカルの長手の向きが -StationRight なので、滑らせる向きも逆
		const float s = -(m_grindU - 0.5f) * len * (m_restFlip ? -1.0f : 1.0f);
		a.x += r.x * s; a.z += r.z * s;
		// 高さ: 刃の一番低い点が砥石の上端(StationBase)にちょうど触れる様に、中心を「中心→最低点」の分だけ上げる。
		//   研ぎ角で傾けると最低点が下がるので毎フレーム求め直す=どの角度・どの大きさの刃でも砥石に食い込まない
		//   (入力を止めるのでなく位置で解決する=押し出し。歩きの衝突と同じ考え方)。
		//   押し当てていない時は GRIND_HOVER だけ浮かせ、左長押しで砥石へ下ろす(押した/離したが見て分かる。下ろしても食い込まない)。
		a.y += BladeDepthBelowCentre() + (1.0f - m_grindPress) * GRIND_HOVER;
		break;
	}
	case Station::Trough:
	{
		// 立てる時に少し持ち上げ(予備動作)、沈める間にその持ち上げは消える。水中では上下に揺する。
		a.y += m_quenchTurn * QUENCH_TURN_LIFT * (1.0f - m_plunge) - m_plunge * PLUNGE_DEPTH;
		a.y += m_agitate;	// 揺する=刃の向きに上下(長手に振ると槽の両端に当たる)
		break;
	}
	default: break;
	}
	return a;
}

XMMATRIX SceneForge::WeaponWorld()
{
	if (m_seqIronOverride) return XMLoadFloat4x4(&m_seqIronWorld);	// 拍子表で動かしている間(Sequence.cpp)
	float ex = m_wpMax.x - m_wpMin.x, ey = m_wpMax.y - m_wpMin.y, ez = m_wpMax.z - m_wpMin.z;
	float maxE = fmaxf(ex, fmaxf(ey, ez)); if (maxE < 1e-5f) maxE = 1.0f;
	float fit = (m_barLen / maxE) * m_wpScale;
	XMFLOAT3 c((m_wpMin.x + m_wpMax.x) * 0.5f, (m_wpMin.y + m_wpMax.y) * 0.5f, (m_wpMin.z + m_wpMax.z) * 0.5f);
	XMFLOAT3 a = WorkAnchor();	// 工位の作業点(配置データのプロップ箱を読む)
	return
		XMMatrixTranslation(-c.x, -c.y, -c.z) *
		XMMatrixScaling(fit, fit, fit) *
		WeaponRot() *	// 翻面(自局所の裏返し) → 向き付け → 工位の揃え
		XMMatrixTranslation(a.x, a.y, a.z);
}

//--- 各区域の進捗 m_segProg[] で段を補間して m_wpVtx を作る。ローカル→砧面へのフィット変換もCPUで焼く。
//    KCD式の核心: 頂点はその「長手位置が属する区域」の進捗で個別に stage_0→stage_final へ動く。
//    区域分割は AimSystem と同じ「ローカル長手軸」規約なので、高亮する区域＝準心が指す区域＝進む区域。
void SceneForge::BuildWeaponMorph()
{
	if (!m_wpOk) return;
	const int NSEG = ForgingSim::NSEG;	// 区域数の別名(唯一の定義は ForgingSim)
	int ns = (int)m_wpStage.size();

	XMMATRIX world = WeaponWorld();
	// 法線も本体と同じ回転(翻面 * 向き付け * 工位の揃え)で回す。世界変換と規約を揃える。
	XMMATRIX rot   = WeaponRot();

	// タイトル等(非プレイ)は全体を一様に m_forgeProg で見せる(F1スライダのプレビュー)。
	const bool  playing = (m_state == GAME_PLAY);
	const int   segAim  = (playing && m_aimValid) ? AimSeg() : -1;	// 準心が鉄の上に無ければ高亮なし
	const float pulse   = 0.5f + 0.5f * sinf(m_time * 8.0f);
	XMFLOAT4 heat = HeatRGB(m_forging.Heat(), 0.0f);
	const int NL = ForgingSim::NL;
	// 打撃の跡の光: 打った所は少し熱い色(温度 + m_impactFlashHeat の HeatRGB)へ寄せ、時間で線形に戻す。
	const float MIN_FLASH_TIME = 0.01f, MIN_FLASH_SPREAD = 0.1f;	// 0 割りの防止
	const float flashLife = 1.0f - (m_time - m_impactTime) / fmaxf(m_impactFlashTime, MIN_FLASH_TIME);	// 1=打った瞬間 → 0
	XMFLOAT4 flashCol = HeatRGB(fminf(m_forging.Heat() + m_impactFlashHeat, 1.0f), 0.0f);
	// チュートリアルの「まだ叩く所」の高亮(青)。明滅の強さはフレームごとに1回だけ求める
	const bool  tutorialHi    = TutorialAtAnvil();
	const XMFLOAT3 TUTORIAL_HI_COL(0.35f, 0.85f, 1.0f);	// 熱い鋼の橙と取り違えない青
	const float tutorialPulse = TUTORIAL_HI_STRENGTH * (0.6f + 0.4f * sinf(m_time * XM_2PI * TUTORIAL_HI_PULSE_HZ));

	// 進捗 p(0..1) → 段チェーン(stage_0..final)上の頂点 i の補間位置/法線(ローカル)。
	auto morphAt = [&](int i, float p, XMVECTOR& outPos, XMVECTOR& outNrm)
	{
		if (p < 0) p = 0; if (p > 1) p = 1;
		float g = p * (ns - 1);
		int   k = (int)g; if (k < 0) k = 0; if (k > ns - 2) k = ns - 2;
		float t = g - k; if (t < 0) t = 0; if (t > 1) t = 1;
		const WpStage& A = m_wpStage[k];
		const WpStage& B = m_wpStage[k + 1];
		outPos = XMVectorLerp(XMLoadFloat3(&A.pos[i]), XMLoadFloat3(&B.pos[i]), t);
		outNrm = XMVectorLerp(XMLoadFloat3(&A.nrm[i]), XMLoadFloat3(&B.nrm[i]), t);
	};

	const int ta = m_wpThickAxis;
	const bool plusIsFront = PlusIsFront();
	const float* mn = &m_wpMin.x;	// stage0 AABB を軸番号で引くための別名
	const float* mx = &m_wpMax.x;
	// 研ぎ: 幅の軸 = 長軸でも厚み軸でもない残りの軸(軸番号 0+1+2=3 から引く)。刃先はこの軸の両端。
	const int la = AimSystem::LongAxis(m_wpMin, m_wpMax);
	const int wa = (ta >= 0) ? 3 - la - ta : -1;
	const float* fmn = &m_wpFinMin.x;	// 完成形の箱(研ぐのは完成した刃なので、幅は完成形で正規化)
	const float* fmx = &m_wpFinMax.x;

	for (int i = 0; i < m_wpN; ++i)
	{
		// 頂点のローカル長手位置 → 区域(AimSystemと同一規約)。境界は隣とブレンドして滑らかに。
		const XMFLOAT3& a0 = m_wpStage[0].pos[i];
		int   thisSeg;
		float pFront, pBack;	// この頂点位置での表(面0)/裏(面1)それぞれの進捗
		float vFront, vBack;	// 同じく、氧化皮の剥がれ具合(未完成の区域は厚い皮が残る=どこが未完成か鉄を見て分かる)
		float sFront = 0.0f, sBack = 0.0f;	// この頂点位置での表/裏の刃の研ぎ具合(見た目用。区域を隣とブレンド)
		float flash = 0.0f;					// 打撃の跡の光(0..1)
		if (playing)
		{
			// 成形は長手セル(NL 個)ごとの連続値。隣のセル中心との間を線形補間=叩いた所の周りだけ滑らかに変わる。
			float cc = AimSystem::SegCoordLocal(a0, m_wpMin, m_wpMax, NL);	// 0..NL
			float cpos = cc - 0.5f;			// セル中心を基準にした連続座標
			int   c0 = (int)floorf(cpos);
			float ct = cpos - c0;
			int   ca = c0 < 0 ? 0 : (c0 >= NL ? NL - 1 : c0);
			int   cb = (c0 + 1) < 0 ? 0 : ((c0 + 1) >= NL ? NL - 1 : (c0 + 1));
			auto cellBlend = [&](int side) {
				float a = m_forging.CellProgOf(side, ca), b = m_forging.CellProgOf(side, cb);
				return a + (b - a) * ct;
			};
			pFront = cellBlend(0);
			pBack  = cellBlend(1);
			// 氧化皮用の進捗: 未完成のセルでは m_scaleHoldMax で頭打ち(=一番厚い皮が残る)、完成した瞬間に 1(=全部剥がれる)。
			//   形の進捗(pFront/pBack)をそのまま使うと、完成の手前で皮が無くなり、残りの所が見分けられなかった。
			auto scaleVis = [&](int side, int c) {
				return m_forging.CellDoneOf(side, c) ? 1.0f : m_forging.CellProgOf(side, c) * m_scaleHoldMax;
			};
			vFront = scaleVis(0, ca) + (scaleVis(0, cb) - scaleVis(0, ca)) * ct;
			vBack  = scaleVis(1, ca) + (scaleVis(1, cb) - scaleVis(1, ca)) * ct;
			// 打撃の跡の光: 打った長手位置からの距離でガウス減衰 × 時間で線形に消える。
			if (flashLife > 0.0f)
			{
				const float d = cc - m_impactCoord;
				const float sg = fmaxf(m_impactFlashSpread, MIN_FLASH_SPREAD);
				flash = flashLife * expf(-(d * d) / (2.0f * sg * sg));
			}

			// 研ぎの見た目: 研ぎ上がるまでは SHARP_HOLD_MAX で頭打ち(刃先に暗い所が残る)、研ぎ上がった瞬間に 1(=一気に明るく)。
			//   鍛造の「最後の黒皮が落ちる」と同じ考え方=仕上がった所が見て分かる(ユーザー要望 2026-10-07)。研ぎも長手セル単位。
			auto sharpVis = [&](int side, int c) {
				return m_forging.EdgeCellDoneOf(side, c) ? 1.0f : m_forging.EdgeCellProgOf(side, c) * SHARP_HOLD_MAX;
			};
			sFront = sharpVis(0, ca) + (sharpVis(0, cb) - sharpVis(0, ca)) * ct;
			sBack  = sharpVis(1, ca) + (sharpVis(1, cb) - sharpVis(1, ca)) * ct;
			thisSeg = (int)AimSystem::SegCoordLocal(a0, m_wpMin, m_wpMax, NSEG); if (thisSeg >= NSEG) thisSeg = NSEG - 1;	// P の調試高亮用
		}
		else { pFront = pBack = m_forgeProg; vFront = vBack = m_forgeProg; thisSeg = -1; }

		// ★軸分解モーフ: 輪郭(長さ/幅)は両面で共有 → 両面の平均進捗で動かす(翻しても輪郭が残る)。
		//   厚み方向は「この頂点が属する面」の進捗で動かす(叩いた面だけ斜面が付き、裏はまだ平ら)。
		const float pOutline = (pFront + pBack) * 0.5f;
		float pFace = pOutline;
		float vFace = (vFront + vBack) * 0.5f;	// 氧化皮用(面の重みは下で pFace と同じ物を使う)
		float sharp = (sFront + sBack) * 0.5f;	// 刃の研ぎ(その頂点が属する面の刃。下で面の重みを付ける)
		if (ta >= 0)
		{
			// 頂点の厚み座標を stage0 の中心面基準で -1..+1 に正規化 → +側の面に属する重み。
			const float c  = (mn[ta] + mx[ta]) * 0.5f;
			const float hh = (mx[ta] - mn[ta]) * 0.5f;
			const float dn = (hh > 1e-6f) ? ((&a0.x)[ta] - c) / hh : 0.0f;
			const float wPlus = Lerp::SmoothStep(0.5f + dn / FACE_BLEND_BAND);	// 中心面付近(刃先・側面)は両面を混ぜる
			const float pPlus  = plusIsFront ? pFront : pBack;
			const float pMinus = plusIsFront ? pBack  : pFront;
			pFace = pMinus + (pPlus - pMinus) * wPlus;
			const float vPlus  = plusIsFront ? vFront : vBack;
			const float vMinus = plusIsFront ? vBack  : vFront;
			vFace = vMinus + (vPlus - vMinus) * wPlus;
			const float sPlus  = plusIsFront ? sFront : sBack;	// 研ぐ面 = 刃の表/裏(各面の刃は別々に研ぐ)
			const float sMinus = plusIsFront ? sBack  : sFront;
			sharp = sMinus + (sPlus - sMinus) * wPlus;
		}

		XMVECTOR posO, nrmO, posF, nrmF;
		morphAt(i, pOutline, posO, nrmO);
		morphAt(i, pFace,    posF, nrmF);
		// 輪郭側の位置を基に、厚み軸の成分だけ面側の値に差し替える。
		XMFLOAT3 lp; XMStoreFloat3(&lp, posO);
		if (ta >= 0) { XMFLOAT3 lf; XMStoreFloat3(&lf, posF); (&lp.x)[ta] = (&lf.x)[ta]; }

		// ★研ぎの形: 刃先(幅の端)に近い頂点ほど、鋭さに応じて厚みを中心面へ寄せる=刃が薄く立つ。
		//   edgeW = 幅の正規化座標(0=中心,1=端)が EDGE_BAND_START を越えた分(SmoothStep)。
		float edge = 0.0f;
		if (wa >= 0 && sharp > 0.0f)
		{
			const float wc = (fmn[wa] + fmx[wa]) * 0.5f;
			const float wh = (fmx[wa] - fmn[wa]) * 0.5f;
			const float wn = (wh > 1e-6f) ? fabsf((&lp.x)[wa] - wc) / wh : 0.0f;
			const float edgeW = Lerp::SmoothStep((wn - EDGE_BAND_START) / (1.0f - EDGE_BAND_START));
			edge = sharp * edgeW;
			const float tc = (mn[ta] + mx[ta]) * 0.5f;		// 厚みの中心面
			(&lp.x)[ta] = tc + ((&lp.x)[ta] - tc) * (1.0f - EDGE_THIN * edge);
		}
		m_wpVtx[i].sharp = edge;	// PS へ: 研ぎ面の見た目(磨いた明るい鋼)に使う
		m_wpVtx[i].work  = vFace;	// PS へ: この頂点の面の氧化皮の剥がれ具合(刃先/側面は両面を混ぜた値。完成まで厚い皮が残る)

		XMVECTOR pp = XMVector3TransformCoord(XMLoadFloat3(&lp), world);
		// 法線は面側を使う: 陰影に効くのは表面の傾き(斜面の有無)で、それは厚み方向の変形が決める。
		XMVECTOR n = XMVector3Normalize(XMVector3TransformNormal(ta >= 0 ? nrmF : nrmO, rot));
		XMStoreFloat3(&m_wpVtx[i].pos, pp);
		XMStoreFloat3(&m_wpVtx[i].nrm, n);
		m_wpVtx[i].uv = m_wpStage[0].uv[i];		// UVは全段共通(morphで不変)

		// 既定は熱色のみ(KCD式=「叩く場所」を示さない)。Pキーでデバッグ可視化ONの時だけ
		// 「今照準している区域」を青緑で薄く塗る(叩く指示ではなく開発用)。
		XMFLOAT4 col = heat;
		// チュートリアル: 上を向いた面のまだ完成していない所を青く明滅させる(=どこを叩けば良いか)。通常モードでは出さない。
		if (tutorialHi && playing)
		{
			const float cc = AimSystem::SegCoordLocal(a0, m_wpMin, m_wpMax, NL) - 0.5f;
			int c0 = (int)floorf(cc); const float t = cc - c0;
			const int ca = c0 < 0 ? 0 : (c0 >= NL ? NL - 1 : c0), cb = (c0 + 1) >= NL ? NL - 1 : (c0 + 1 < 0 ? 0 : c0 + 1);
			const int up = m_forging.Side();
			const float todo = (m_forging.CellDoneOf(up, ca) ? 0.0f : 1.0f) * (1.0f - t) + (m_forging.CellDoneOf(up, cb) ? 0.0f : 1.0f) * t;
			const float w = todo * tutorialPulse;
			col.x += (TUTORIAL_HI_COL.x - col.x) * w;
			col.y += (TUTORIAL_HI_COL.y - col.y) * w;
			col.z += (TUTORIAL_HI_COL.z - col.z) * w;
		}
		if (flash > 0.0f)
		{
			col.x += (flashCol.x - col.x) * flash;
			col.y += (flashCol.y - col.y) * flash;
			col.z += (flashCol.z - col.z) * flash;
		}
		if (m_showAimHi && thisSeg == segAim)
		{
			float b = 0.30f + 0.20f * pulse;
			col.x = col.x * (1.0f - b);
			col.y = col.y + (1.0f - col.y) * b;
			col.z = col.z + (1.0f - col.z) * b;
		}
		col.w = m_forging.Heat();			// PSへ温度スカラーを渡す(冷→熱のブレンドに使う)
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

	// 金属質感パラメータをPS(b0)へ。HLSLの cbuffer Mtl とレイアウト一致(各float4境界)。
	struct MtlCB {
		XMFLOAT3 camPos;  float rough;
		XMFLOAT3 lightDir;float metal;
		XMFLOAT3 skyCol;  float specK;
		XMFLOAT3 grdCol;  float envK;
		float    fresK;   float hotShade; float rimK; float rimPow;
		float    scaleTiling; float scaleSoft; float scaleOpacity; float scaleGlow;
		float    hotGain; float scaleStart; XMFLOAT2 pad2;
	} mtl;
	mtl.camPos   = cam->GetPos();
	mtl.rough    = m_wpRough;
	mtl.lightDir = { 0.35f, 0.85f, -0.4f };			// 既存のライト方向(固定)
	mtl.metal    = m_wpMetal;
	mtl.skyCol   = { m_wpSky[0], m_wpSky[1], m_wpSky[2] };
	mtl.specK    = m_wpSpec;
	mtl.grdCol   = { m_wpGround[0], m_wpGround[1], m_wpGround[2] };
	mtl.envK     = m_wpEnv;
	mtl.fresK    = m_wpFresnel;
	mtl.hotShade = m_wpHotShade;
	mtl.rimK     = m_wpRimK;
	mtl.rimPow   = m_wpRimPow;
	mtl.scaleTiling  = m_scaleTiling;
	mtl.scaleSoft    = m_scaleSoft;
	// マスクが読めない時は黒皮を出さない(灰色の既定テクスチャで全面が皮に覆われない様に)
	mtl.scaleOpacity = (m_wpScaleMask && m_wpScaleMask->GetResource()) ? m_scaleOpacity : 0.0f;
	mtl.scaleGlow    = m_scaleGlow;
	mtl.hotGain      = m_wpHotGain;
	mtl.scaleStart   = m_scaleStart;
	mtl.pad2         = { 0, 0 };

	SetBlendMode(BLEND_NONE);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	vs->Bind();
	ps->Bind();
	ps->WriteBuffer(0, &mtl);						// b0 = 金属質感パラメータ
	if (m_wpTex) ps->SetTexture(0, m_wpTex.get());	// t0 = 鋼テクスチャ(BaseColor)
	if (m_wpScaleMask && m_wpScaleMask->GetResource())
		ps->SetTexture(1, m_wpScaleMask.get());		// t1 = 氧化皮の厚みマスク
	m_wpMesh->Draw();
}


//--- 目標ゴースト: stage_final(最終形)の頂点を WeaponWorld で変換して m_ghostVtx を作る。
//    形は不変なので進捗補間はしない。色は青白い氷色(alphaは基準値、縁強調はPSで行う)。
void SceneForge::BuildGhostMesh()
{
	if (!m_wpOk || m_wpStage.empty()) return;
	const WpStage& F = m_wpStage.back();		// stage_final = 完成形
	XMMATRIX world = WeaponWorld();
	XMMATRIX rot   = WeaponRot();	// 翻面・工位の揃え込み
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
	if (HammerOnHip()) { DrawModelWorld(hammer, HammerHipWorld(hammer)); return; }	// 金床を離れたら右腰に下げる
	DrawModelWorld(hammer, HammerWorld(hammer));
}

//--- 打撃の火花の出る所 = ハンマーの頭の真下の、鉄の上面。
//    水平位置は描かれているハンマーの「頭」から求める(照準点やオフセットから推測しない=タイトルとゲームで同じ規則)。
//    旧: モデル箱の一番低い面 → 打った瞬間は柄の端の方が低く、火花が柄の端(頭の横 0.3m)から出ていた(2026-10-08 F1 十字で確認)。
XMFLOAT3 SceneForge::HammerStrikePoint()
{
	Model* hammer = GetObj<Model>("MdlHammer");
	if (!hammer) return m_aimWorld;
	if (!m_hammerHeadReady) { m_hammerHeadLocal = FindHammerHeadLocal(hammer); m_hammerHeadReady = true; }
	// 反冲(打った瞬間に頭が上へ翻る/後ろへ下がる見た目)を除いた姿勢 = 頭が実際に落ちた所。反冲込みだと弾かれた後の位置から出ていた
	const bool WITHOUT_RECOIL = false;
	XMFLOAT3 head; XMStoreFloat3(&head, XMVector3TransformCoord(XMLoadFloat3(&m_hammerHeadLocal), HammerWorld(hammer, WITHOUT_RECOIL)));
	return XMFLOAT3(head.x, WorkAnchor().y + BladeDepthBelowCentre(), head.z);	// 高さ = 鉄の上面(頭が当たる所)
}

//--- 打撃が効く長手位置 = 描かれているハンマーの頭が落ちる所(HammerStrikePoint)を、刃のローカル長手座標へ。
//    照準点(m_aimWorld)でなく頭そのものから取る: 追従の遅れやオフセットがあっても「見えている頭の真下」が変形する=指した所を打つ。
//    描画(BuildWeaponMorph)と同じ WeaponWorld の逆変換 + SegCoordLocal 規約なので、変形する所と頭の位置が原理的に一致する。
float SceneForge::StrikeLenCoord()
{
	if (!m_wpOk) return m_aimI + 0.5f;	// 武器FBXが無い時(旧・高さ場): 照準セルの中心
	const XMFLOAT3 p = HammerStrikePoint();
	XMFLOAT3 lp; XMStoreFloat3(&lp, XMVector3TransformCoord(XMLoadFloat3(&p), XMMatrixInverse(nullptr, WeaponWorld())));
	return AimSystem::SegCoordLocal(lp, m_wpMin, m_wpMax, ForgingSim::NL);	// 0..NL(刃の外は端へ寄せる)
}

//--- 研いでいる長手位置 = 砥石の接点(StationBase: 砥石の上端。刃はここへ滑らせて当てている)を刃のローカル長手座標へ。
//    旧: m_grindU*NSEG で区域を推測 → 置いた向き次第で描画の区域と逆になり、暗い所を当てても別の区域が研げていた(研ぎが 90% で止まる)。
float SceneForge::GrindLenCoord()
{
	if (!m_wpOk) return m_grindU * ForgingSim::NL;
	const XMFLOAT3 p = StationBase(Station::Grindstone);
	XMFLOAT3 lp; XMStoreFloat3(&lp, XMVector3TransformCoord(XMLoadFloat3(&p), XMMatrixInverse(nullptr, WeaponWorld())));
	return AimSystem::SegCoordLocal(lp, m_wpMin, m_wpMax, ForgingSim::NL);
}

//--- ハンマーの頭の中心(モデル空間)。柄は細長く頭は太い → 長軸の両端の帯で「長軸に直交する広がり」を比べ、広い方が頭。
//    その帯の頂点の平均 = 頭の中心。モデルの向きや原点の位置に依らない(別のハンマーに替えても同じ式)。
XMFLOAT3 SceneForge::FindHammerHeadLocal(Model* hammer)
{
	std::vector<XMFLOAT3> v; hammer->AppendLocalVertices(v);
	XMFLOAT3 mn, mx; hammer->GetLocalAABB(mn, mx);
	if (v.empty()) return XMFLOAT3((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
	const int la = AimSystem::LongAxis(mn, mx);
	const float lo = (&mn.x)[la], hi = (&mx.x)[la];
	const float END_BAND = 0.2f;	// 長軸の両端この割合の帯を「端」とみなす
	struct Band { XMFLOAT3 sum{ 0, 0, 0 }; int n = 0; XMFLOAT3 bmn{ FLT_MAX, FLT_MAX, FLT_MAX }, bmx{ -FLT_MAX, -FLT_MAX, -FLT_MAX }; };
	Band ends[2];
	for (const XMFLOAT3& p : v)
	{
		const float t = ((&p.x)[la] - lo) / fmaxf(hi - lo, 1e-6f);
		const int e = (t < END_BAND) ? 0 : (t > 1.0f - END_BAND ? 1 : -1);
		if (e < 0) continue;
		Band& b = ends[e];
		b.sum.x += p.x; b.sum.y += p.y; b.sum.z += p.z; ++b.n;
		b.bmn = XMFLOAT3(fminf(b.bmn.x, p.x), fminf(b.bmn.y, p.y), fminf(b.bmn.z, p.z));
		b.bmx = XMFLOAT3(fmaxf(b.bmx.x, p.x), fmaxf(b.bmx.y, p.y), fmaxf(b.bmx.z, p.z));
	}
	auto spread = [&](const Band& b) {	// 長軸に直交する2軸の広がりの積 = 断面の大きさ
		if (b.n == 0) return 0.0f;
		float s = 1.0f;
		for (int a = 0; a < 3; ++a) if (a != la) s *= (&b.bmx.x)[a] - (&b.bmn.x)[a];
		return s;
	};
	const Band& head = (spread(ends[1]) > spread(ends[0])) ? ends[1] : ends[0];
	if (head.n == 0) return XMFLOAT3((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
	return XMFLOAT3(head.sum.x / head.n, head.sum.y / head.n, head.sum.z / head.n);
}

//--- 金床で構えている(打っている)ハンマーのワールド行列。
XMMATRIX SceneForge::HammerWorld(Model* hammer, bool withRecoil)
{

	// 準心が当たっているセルの真上にハンマーを置く。準心が板の外に出ても、m_aimWorld/m_aimI/J は
	// 最後に有効だった位置を保持している(UpdateAimは無効時に値を更新しない)ので、そのまま使う=
	// 中央にリセットせずハンマーは最後の位置に留まる(操作の異様感を無くす)。
	// 横位置(XZ)は UpdatePlay で Lerp::Damp 済みの m_hammerPos を読む(格子跳びを吸収)。
	// オフセットは平滑化の目標に既に含まれているので、ここでは足さない。
	// 反冲(後座)の位相 rp: 奥行き後退＋錘頭の上翻り。バネの縦速度から直接求める=物理と一致。
	// 打撃直後は上向き速度が最大(=接触の反作用が最も強い)→ rp=1、上昇するにつれ減衰→頂点で0。
	// これで縦の跳ね(m_hammerLift 側)と、後退＋上翻り(ここ)が同じ物理タイミングで起きる。
	float rp = 0.0f;
	float vLaunch = m_hammer.LaunchSpeed();		// 打撃直後の初速 v0 = J/m
	if (withRecoil && vLaunch > 0.0001f && m_hammer.Velocity() > 0.0f)
	{
		rp = m_hammer.Velocity() / vLaunch;		// 0..1 に正規化(速度で駆動)
		if (rp > 1.0f) rp = 1.0f;
	}

	// ハンマーの高さは固定(砧面+平坦時の板厚)。旧2D高度場 m_h は DoStrike で叩いた格子だけ
	// 凹むため、それを読むと「叩いた位置に戻ると錘が沈む」不具合になる。武器モーフの刃面は
	// ほぼ平なので、位置に依らない一定の barTop にする(高さは回弾アニメ m_hammerLift のみで変える)。
	float barTop = m_barAnchor.y + m_forging.Start();
	// 翻面中はハンマーを置く: 構え位置から m_hammerStowOff だけずらし、m_hammerStowTilt だけ寝かせる(重みで補間)。
	const float sw = m_hammerStowW;
	XMFLOAT3 pos = {
		m_hammerPos.x + m_hammerStowOff[0] * sw,
		barTop + m_hammer.Lift() + m_hammerOff[1] + m_hammerStowOff[1] * sw,
		m_hammerPos.z - HAMMER_RECOIL_BACK * rp + m_hammerStowOff[2] * sw,		// 反作用で鉄匠側(-Z)へ後退
	};

	XMMATRIX world =
		XMMatrixScaling(m_hammerScale, m_hammerScale, m_hammerScale) *
		XMMatrixRotationRollPitchYaw(m_hammerRot[0] - HAMMER_RECOIL_TILT * rp	// 錘頭が上へ翻る
			+ m_hammerStowTilt * sw,											// 置く時は寝かせる
			m_hammerRot[1], m_hammerRot[2]) *
		XMMatrixTranslation(pos.x, pos.y, pos.z);
	return hammer->GetScaleBaseMatrix() * world;
}

//--- ハンマーを右腰に下げているか: 遊んでいて、金床で作業していない時(歩いている / 他の工位 / 移動中)。
//    タイトルでは金床で自動で打っているので金床のまま。
bool SceneForge::HammerOnHip() const
{
	if (m_state != GAME_PLAY) return false;
	return m_walkMode || Transitioning() || m_station != Station::Anvil;
}

//--- 右腰のハンマー = 腰帯(ベルトの輪)に下げた状態: 頭が輪に掛かって腰の高さに止まり、柄は真下へ垂れる。
//    腰の点 = 体の右・床からの高さ・前へ m_hammerHipOff(火钳の左腰 HipPoint の左右反対)。体と一緒に動き、腰より上へは出ない。
//    向きは数値で持たずモデルから求める: 頭(FindHammerHeadLocal) → 箱の中心 の向き(=柄の向き)を真下へ回す。
//    旧: 金床で構える回転をそのまま流用 → 頭が上・前へ突き出て、工位の見下ろしカメラの視野に入った(2026-10-08 F5)。
XMMATRIX SceneForge::HammerHipWorld(Model* hammer)
{
	if (!m_hammerHeadReady) { m_hammerHeadLocal = FindHammerHeadLocal(hammer); m_hammerHeadReady = true; }
	const XMFLOAT3 f = BodyForward();
	const XMFLOAT3 r(f.z, 0.0f, -f.x);	// 右(前方を右へ90度)
	const XMFLOAT3 body = BodyPosition();	// 火钳の左腰と同じ体の位置(工位では工位カメラの真下)
	const XMFLOAT3 belt(body.x + r.x * m_hammerHipOff[0] + f.x * m_hammerHipOff[2],
	                    m_walkFloorY + m_hammerHipOff[1],
	                    body.z + r.z * m_hammerHipOff[0] + f.z * m_hammerHipOff[2]);

	// モデル(基準行列+大きさ適用後)の「頭 → 中心」= 柄の向き。これを真下へ向ける最短回転。
	const XMMATRIX scaled = hammer->GetScaleBaseMatrix() * XMMatrixScaling(m_hammerScale, m_hammerScale, m_hammerScale);
	XMFLOAT3 mn, mx; hammer->GetLocalAABB(mn, mx);
	const XMFLOAT3 centre((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
	const XMVECTOR head   = XMVector3TransformCoord(XMLoadFloat3(&m_hammerHeadLocal), scaled);
	const XMVECTOR handle = XMVector3TransformCoord(XMLoadFloat3(&centre), scaled) - head;
	const XMMATRIX hang = RotationFromTo(handle, XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f)) *
	                      XMMatrixRotationY(atan2f(f.x, f.z) + m_hammerHipYaw);	// 体の向きに合わせ、垂れた軸まわりに m_hammerHipYaw だけ回す
	// 頭の中心がちょうど腰の点に来る様に平行移動
	XMFLOAT3 h; XMStoreFloat3(&h, XMVector3TransformCoord(head, hang));
	return scaled * hang * XMMatrixTranslation(belt.x - h.x, belt.y - h.y, belt.z - h.z);
}

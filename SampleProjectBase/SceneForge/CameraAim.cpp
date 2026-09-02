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

//--- ゲーム用の固定カメラを毎フレーム適用(ドラッグで動かされても上書きして固定する)
void SceneForge::ApplyCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;

	// FPS式受限環視: 基準視線(m_camPos→m_camLook)を、マウス累積のyaw/pitchだけ回す。
	// 準心は常に画面中心=カメラ正前方。yaw/pitchは UpdateMouseLook で夹住済み。
	XMVECTOR pos  = XMVectorSet(m_camPos[0], m_camPos[1], m_camPos[2], 0);
	XMVECTOR base = XMVectorSet(m_camLook[0], m_camLook[1], m_camLook[2], 0);
	XMVECTOR fwd0 = XMVector3Normalize(XMVectorSubtract(base, pos));	// 基準の正前方

	// yaw(世界Y軸回り) → pitch(カメラ右軸回り) の順に回す
	XMMATRIX rotY = XMMatrixRotationY(m_lookYaw);
	XMVECTOR fwd  = XMVector3TransformNormal(fwd0, rotY);
	XMVECTOR right= XMVector3Normalize(XMVector3Cross(XMVectorSet(0,1,0,0), fwd));
	XMMATRIX rotP = XMMatrixRotationAxis(right, m_lookPitch);
	fwd = XMVector3Normalize(XMVector3TransformNormal(fwd, rotP));

	XMStoreFloat3(&m_camFwd, fwd);	// 照準射線に使う
	XMVECTOR look = XMVectorAdd(pos, fwd);

	XMFLOAT3 lf; XMStoreFloat3(&lf, look);
	cam->SetPos (XMFLOAT3(m_camPos[0], m_camPos[1], m_camPos[2]));
	cam->SetLook(lf);
	cam->SetUp  (XMFLOAT3(0.0f, 1.0f, 0.0f));
}

//--- マウス移動を視角(yaw/pitch)へ累積する。FPS方式: 毎フレーム、カーソルを画面中心へ
//    戻し(再センタリング)、その差分を回転量にする。範囲は板の周囲に夹住する。
void SceneForge::UpdateMouseLook()
{
	HWND hwnd = GetActiveWindow();
	if (!hwnd) return;
	RECT rc; GetClientRect(hwnd, &rc);
	POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
	POINT cp; GetCursorPos(&cp);
	POINT cs = center; ClientToScreen(hwnd, &cs);	// 画面座標の中心
	POINT co = center; // クライアント基準
	// 現在のカーソルをクライアント座標へ
	POINT cc = cp; ScreenToClient(hwnd, &cc);
	float dx = (float)(cc.x - co.x);
	float dy = (float)(cc.y - co.y);

	const float SENS = 0.0026f;			// 感度(rad/px)
	m_lookYaw   += dx * SENS;
	m_lookPitch += dy * SENS;			// 下げると見下ろす(dyは下が正)
	// 板の周囲に夹住(見失わないように)
	const float YAW_LIM = 0.55f, PITCH_LIM = 0.40f;
	if (m_lookYaw   >  YAW_LIM) m_lookYaw   =  YAW_LIM;
	if (m_lookYaw   < -YAW_LIM) m_lookYaw   = -YAW_LIM;
	if (m_lookPitch >  PITCH_LIM) m_lookPitch =  PITCH_LIM;
	if (m_lookPitch < -PITCH_LIM) m_lookPitch = -PITCH_LIM;

	SetCursorPos(cs.x, cs.y);			// 中心へ戻す(累積の基準を保つ)
}

//--- FPS式ヒットスキャン照準。準心(画面中心=カメラ正前方 m_camFwd)から射線を飛ばし、
//    武器を長手にNSEG分割したボックス群と交差させる。当たった一番手前の区域が照準区域。
//    どの区域にも当たらなければ空振り(m_aimValid=false)。判定対象が「武器の実体」なので、
//    準心が刃に乗っている＝射線が刃に当たる、が一致する(Valorant等と同じ原理)。
void SceneForge::UpdateAim()
{
	if (m_wpOk)
	{
		XMFLOAT3 o = { m_camPos[0], m_camPos[1], m_camPos[2] };
		AimSystem::Hit h = AimSystem::Raycast(o, m_camFwd, WeaponWorld(), m_wpMin, m_wpMax, NSEG);
		m_aimValid = h.valid;
		if (h.valid)
		{
			m_aimSeg   = h.seg;
			m_aimWorld = h.world;			// ハンマーはこの実際の命中点へ
			// 旧2Dグリッド(死にコードだが index を要求)が破綻しないよう妥当な値を入れておく
			m_aimI = (h.seg * NL) / NSEG + (NL / (2 * NSEG));
			if (m_aimI < 0) m_aimI = 0; if (m_aimI > NL - 1) m_aimI = NL - 1;
			m_aimJ = NW / 2;
		}
		return;
	}

	// --- フォールバック(武器FBXが無い時): 旧・射線×砧面水平矩形 ---
	XMFLOAT3 o = { m_camPos[0], m_camPos[1], m_camPos[2] };
	XMFLOAT3 d = m_camFwd;
	// 板の上面を代表する水平面 y = planeY と交差
	float planeY = m_barAnchor.y + m_hStart * 0.5f;
	if (fabsf(d.y) < 1e-5f) { m_aimValid = false; return; }
	float t = (planeY - o.y) / d.y;
	if (t <= 0.0f) { m_aimValid = false; return; }	// 前方でない
	float hx = o.x + d.x * t;
	float hz = o.z + d.z * t;

	// ワールド → 工件の footprint(長手×幅)内の正規化座標。FPS判定=準心が実際に鉄の上に無ければ
	//   空振り(m_aimValid=false)。端に夹まない=狙っていない所を叩いても無効(空揮音のみ)。
	float half = m_barLen * 0.5f;
	float nj = (hx - (m_barAnchor.x - m_barWidth)) / (2.0f * m_barWidth);	// 0..1(幅)
	float ni = (hz - (m_barAnchor.z - half)) / m_barLen;					// 0..1(長さ)
	if (ni < 0.0f || ni > 1.0f || nj < 0.0f || nj > 1.0f) { m_aimValid = false; return; }
	if (ni > 0.9999f) ni = 0.9999f;
	if (nj > 0.9999f) nj = 0.9999f;
	m_aimI = (int)(ni * NL);
	m_aimJ = (int)(nj * NW);
	if (m_aimI < 0) m_aimI = 0; if (m_aimI > NL - 1) m_aimI = NL - 1;
	if (m_aimJ < 0) m_aimJ = 0; if (m_aimJ > NW - 1) m_aimJ = NW - 1;
	// 照準セルの中心のワールド座標(ハンマー配置に使う)
	float cx = (m_barAnchor.x - m_barWidth) + 2.0f * m_barWidth * ((m_aimJ + 0.5f) / NW);
	float cz = (m_barAnchor.z - half)       + m_barLen        * ((m_aimI + 0.5f) / NL);
	m_aimWorld = XMFLOAT3(cx, m_barAnchor.y + m_h[m_aimI][m_aimJ], cz);
	m_aimValid = true;
}

//--- Unity風ドラッグ配置: F1中に、選択プロップを地面(XZ)上でLMBドラッグ移動する。
//    ALT+ドラッグはカメラなので、素のLMBドラッグだけを移動に使う(競合しない)。
void SceneForge::UpdateEditorDrag()
{
	if (!DebugUI::IsVisible()) { m_editDragging = false; return; }
	if (m_editSel < 0 || m_editSel >= (int)m_props.size()) { m_editDragging = false; return; }
	// ImGuiのウィンドウ上を操作中は無視(スライダ等と競合しない)
	if (ImGui::GetIO().WantCaptureMouse) { m_editDragging = false; return; }
	// ALT(カメラ操作)中や、LMB非押下なら移動しない
	if (IsKeyPress(VK_MENU) || !IsKeyPress(VK_LBUTTON)) { m_editDragging = false; return; }

	float mx, my, cw, ch; GetMouseClient(mx, my, cw, ch);
	if (!m_editDragging) { m_editDragging = true; m_editPrevX = mx; m_editPrevY = my; return; }
	float dx = mx - m_editPrevX, dy = my - m_editPrevY;
	m_editPrevX = mx; m_editPrevY = my;
	if (dx == 0.0f && dy == 0.0f) return;

	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;
	XMFLOAT3 cp = cam->GetPos(), cl = cam->GetLook();
	XMVECTOR vp = XMLoadFloat3(&cp), vl = XMLoadFloat3(&cl);
	XMVECTOR fwd  = XMVector3Normalize(XMVectorSubtract(vl, vp));
	XMVECTOR up   = XMVectorSet(0, 1, 0, 0);
	XMVECTOR right= XMVector3Normalize(XMVector3Cross(up, fwd));	// 画面右=+
	XMVECTOR fwdG = XMVector3Normalize(XMVectorSetY(fwd, 0.0f));	// 地面に投影した前方
	float dist; XMStoreFloat(&dist, XMVector3Length(XMVectorSubtract(vl, vp)));
	float k = dist * 0.0016f;	// 距離に応じた移動感度

	// マウス右→+right、マウス下→カメラ手前(=-fwdG)
	XMVECTOR move = XMVectorAdd(XMVectorScale(right, dx * k), XMVectorScale(fwdG, -dy * k));
	Prop& p = m_props[m_editSel];
	p.pos[0] += XMVectorGetX(move);
	p.pos[2] += XMVectorGetZ(move);
}

//--- 金床のAABBを現在のワールド変換で評価し、砧面(上面)の高さに鉄条を乗せる
//    アンカー方式: 金床のスケール/位置/回転を変えても鉄条が自動で追従する。
//    別の金床モデルに差し替えてもAABBが変わるだけで再調整不要。
void SceneForge::UpdateBarAnchor()
{
	// 金床はプロップ(StAnvil)。配置ファイルで動かしても、そのワールド変換＋AABBから
	// 砧面(上面中心)を毎フレーム求めるので鉄条が自動追従する。
	Prop* anvil = GetProp("StAnvil");
	if (!anvil) return;
	XMMATRIX world = PropWorld(*anvil);

	// AABBの8隅をワールドへ変換し、一番高いY(砧面)と、X/Zの中心を求める
	float topY = -1e18f, botY = 1e18f;
	float minX = 1e18f, maxX = -1e18f, minZ = 1e18f, maxZ = -1e18f;
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 p(
			(i & 1) ? anvil->aabbMax.x : anvil->aabbMin.x,
			(i & 2) ? anvil->aabbMax.y : anvil->aabbMin.y,
			(i & 4) ? anvil->aabbMax.z : anvil->aabbMin.z);
		XMVECTOR wv = XMVector3TransformCoord(XMLoadFloat3(&p), world);
		float x = XMVectorGetX(wv), y = XMVectorGetY(wv), z = XMVectorGetZ(wv);
		if (y > topY) topY = y;
		if (y < botY) botY = y;
		if (x < minX) minX = x; if (x > maxX) maxX = x;
		if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
	}
	m_groundY = botY;	// 床の高さ=金床の底面。装飾もこの床に自動設置する
	// 砧面中心(X/Z)＋上面の高さ。鉄条の下面がここに接するよう厚み分持ち上げる
	m_barAnchor.x = (minX + maxX) * 0.5f;
	m_barAnchor.z = (minZ + maxZ) * 0.5f;
	m_barAnchor.y = topY + m_barThick + m_barLift;
	m_barY = m_barAnchor.y;	// F1表示用
}

//--- デバッグ: 8隅の点から箱の12辺を線で描く
static void DrawBoxEdges(const XMFLOAT3 c[8])
{
	// ビット: 1=x, 2=y, 4=z。1ビットだけ違う隅同士が辺
	for (int i = 0; i < 8; ++i)
		for (int b = 1; b <= 4; b <<= 1)
			if (!(i & b))
				Geometory::AddLine(c[i], c[i | b]);
}

//--- デバッグ: 金床AABBと鉄条の箱を線で可視化(F1中のみ)
void SceneForge::DrawDebugBoxes()
{
	CameraBase* cam   = GetObj<CameraBase>("Camera");
	Prop*       anvil = GetProp("StAnvil");
	if (!cam || !anvil) return;

	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());

	// 金床のワールドAABB(緑)
	XMMATRIX world = PropWorld(*anvil);
	XMFLOAT3 ac[8];
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 p(
			(i & 1) ? anvil->aabbMax.x : anvil->aabbMin.x,
			(i & 2) ? anvil->aabbMax.y : anvil->aabbMin.y,
			(i & 4) ? anvil->aabbMax.z : anvil->aabbMin.z);
		XMStoreFloat3(&ac[i], XMVector3TransformCoord(XMLoadFloat3(&p), world));
	}
	Geometory::SetColor(XMFLOAT4(0.2f, 1.0f, 0.3f, 1.0f));
	DrawBoxEdges(ac);

	// 鉄条の箱(黄) + アンカー点
	float hl = m_barLen * 0.5f, w = m_barWidth, th = m_barThick;
	float ax = m_barAnchor.x, ay = m_barAnchor.y, az = m_barAnchor.z;
	XMFLOAT3 bc[8] = {
		{ ax - w, ay - th, az - hl }, { ax + w, ay - th, az - hl },
		{ ax - w, ay + th, az - hl }, { ax + w, ay + th, az - hl },
		{ ax - w, ay - th, az + hl }, { ax + w, ay - th, az + hl },
		{ ax - w, ay + th, az + hl }, { ax + w, ay + th, az + hl },
	};
	Geometory::SetColor(XMFLOAT4(1.0f, 0.9f, 0.2f, 1.0f));
	DrawBoxEdges(bc);

	Geometory::DrawLines();
}

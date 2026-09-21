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
// 非周期に見える有機ノイズ: 無理数比に近い3本の正弦を重ねる。単一sin(=固定周期=工整)を
// 避けつつ、乱数(白色ノイズ)のようにガクつかず滑らかに漂う。範囲おおよそ ±(合計の重み)。
namespace {
	// 3本の倍音(周波数比・振幅重み・位相ずらし)。マジックナンバー回避のため名前付き表に。
	struct NoiseHarmonic { float freq, weight, phase; };
	constexpr NoiseHarmonic kNoiseHarmonics[] = {
		{ 1.00f, 0.60f, 1.0f },	// 基音(一番ゆっくり・一番強い)
		{ 2.30f, 0.30f, 1.7f },	// 第2倍音
		{ 3.70f, 0.18f, 2.9f },	// 第3倍音(一番速い・一番弱い)
	};
	// 3軸を独立に揺らすための seed(値そのものに意味は無い。互いに十分離れていればよい)。
	constexpr float kNoiseSeedBreath[3] = { 11.0f, 27.0f, 43.0f };	// 呼吸用(X,Y,Z)
	constexpr float kNoiseSeedTremor[3] = {  5.0f, 15.0f, 31.0f };	// 微顫用(X,Y,Z)
}
static float OrganicNoise(float t, float seed)
{
	float s = 0.0f;
	for (const NoiseHarmonic& h : kNoiseHarmonics)
		s += h.weight * sinf(t * h.freq + seed * h.phase);
	return s;
}

void SceneForge::ApplyCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;

	cam->SetFovY(m_camFov);	// 狭画角=寄り=鉄匠目線の沈浸感

	// 刃の長手(Z)に沿った狙い位置追従: m_aimRailSmooth(0=手前,1=奥)が指す刃上の点へ、
	// 注視点とカメラ本体をZ方向に寄せる。中央(rail=0.5)が既定 m_camLook/Pos の位置。
	// これで下半段(手前)も準心に入り、KCDのように「視角と錘が一緒に付いてくる」。
	float railZ  = (m_barAnchor.z - m_barLen * 0.5f) + m_aimRailSmooth * m_barLen;
	float deltaZ = (railZ - m_barAnchor.z) * m_camPanGain;	// 中央基準のZずれ

	// FPS式受限環視: 基準視線(m_camPos→m_camLook)を、マウス累積の yaw だけ回す。
	// 準心は常に画面中心=カメラ正前方。上下は追従(deltaZ)で表現する。
	XMVECTOR pos  = XMVectorSet(m_camPos[0],  m_camPos[1],  m_camPos[2]  + deltaZ * m_camFollowZ, 0);
	XMVECTOR base = XMVectorSet(m_camLook[0], m_camLook[1], m_camLook[2] + deltaZ, 0);
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

	// 打撃の反冲をカメラに伝える。m_shake(打撃で~0.9→0へ減衰)を振幅に、その値自身で
	// 位相を回して減衰振動を作る(sin(shake*K))=別のタイマ不要で「ガッ」と一発揺れて収まる。
	// pos と look を同じ量だけ縦に動かす→視線方向(m_camFwd=照準)は不変。framing だけ揺れる。
	// === 手持ち感の三層(すべて極小振幅) =================================
	// 第3層(既存): 落錘の冲撃。m_shake(打撃で~0.9→0へ減衰)で「ガッ」と一発縦揺れして収まる。
	// SHAKE_OSC_FREQ = 減衰振動の速さ(大=細かく振れて速く収まる)。
	constexpr float SHAKE_OSC_FREQ = 42.0f;
	float dy = (m_shake > 0.0f) ? (CAM_SHAKE_AMP * m_shake * sinf(m_shake * SHAKE_OSC_FREQ)) : 0.0f;

	// 第1層: 呼吸(常駐)。低頻ノイズで機位を三軸ともゆっくり漂わせる=「据えてるが生きてる」。
	float bt = m_time * m_camBreathSpeed;
	float ox = OrganicNoise(bt, kNoiseSeedBreath[0]) * m_camBreathAmp;
	float oy = OrganicNoise(bt, kNoiseSeedBreath[1]) * m_camBreathAmp;
	float oz = OrganicNoise(bt, kNoiseSeedBreath[2]) * m_camBreathAmp;

	// 第2層: 蓄力の微顫。高頻ノイズを charge^rate で効かせる=満蓄近くで手が最も震える(張力)。
	// m_camTremorRamp が大きいほど「満蓄直前まで殆ど震えず、最後に効く」= 立ち上がりが遅い。
	float cAmp = m_camTremorAmp * powf(m_charge, m_camTremorRamp);
	if (cAmp > 0.0f)
	{
		float ct = m_time * m_camTremorSpeed;
		ox += OrganicNoise(ct, kNoiseSeedTremor[0]) * cAmp;
		oy += OrganicNoise(ct, kNoiseSeedTremor[1]) * cAmp;
		oz += OrganicNoise(ct, kNoiseSeedTremor[2]) * cAmp;
	}

	// 機位は全量、注視点は m_camLookNoise 倍(=視線は概ね工件に残しつつ少しだけ飄る)。冲撃dyは framing。
	const float LN = m_camLookNoise;
	cam->SetPos (XMFLOAT3(m_camPos[0] + ox, m_camPos[1] + oy + dy, m_camPos[2] + oz));
	cam->SetLook(XMFLOAT3(lf.x + ox * LN,   lf.y + oy * LN + dy,   lf.z + oz * LN));
	cam->SetUp  (XMFLOAT3(0.0f, 1.0f, 0.0f));
}

//--- 走動モードのマウス視角。UpdateMouseLook と同じ「毎フレーム中心へ戻す」相対方式だが、
//    左右(yaw)は玩家の向き(m_player)に、上下(pitch)は m_walkPitch に入れる(一人称の見回し)。
void SceneForge::UpdateWalkLook()
{
	HWND hwnd = GetActiveWindow();
	if (!hwnd) return;
	RECT rc; GetClientRect(hwnd, &rc);
	POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
	POINT cp; GetCursorPos(&cp);
	POINT cs = center; ClientToScreen(hwnd, &cs);	// 画面座標の中心
	POINT cc = cp;     ScreenToClient(hwnd, &cc);	// 現在カーソルをクライアント座標へ
	float dx = (float)(cc.x - center.x);
	float dy = (float)(cc.y - center.y);

	// 左右: マウス右(dx>0)=右へ振り向く=yaw増加(前方が+xへ回る)
	m_player.SetYaw(m_player.GetYaw() + dx * m_walkSens);
	// 上下: マウス上(dy<0)=見上げる=pitch増加。真上/真下で反転しない様に夹住。
	m_walkPitch -= dy * m_walkSens;
	if (m_walkPitch >  m_walkPitchLim) m_walkPitch =  m_walkPitchLim;
	if (m_walkPitch < -m_walkPitchLim) m_walkPitch = -m_walkPitchLim;

	SetCursorPos(cs.x, cs.y);	// 中心へ戻す(累積の基準を保つ)
}

//--- 走動モードのカメラ = 玩家の目線に置く一人称カメラ。
//    位置 = 玩家座標 + 目線高。向き = 玩家yaw(左右) + m_walkPitch(上下)。
void SceneForge::ApplyWalkCamera()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam) return;

	cam->SetFovY(m_camFov);	// 画角は工位と揃える(必要なら後で走動用に広げる)

	// 目線位置: 足元(m_player)の真上 m_walkEyeH
	XMFLOAT3 p = m_player.GetPosition();
	XMFLOAT3 eye = XMFLOAT3(p.x, p.y + m_walkEyeH, p.z);

	// yaw/pitch から前方ベクトル。yaw=0 で +Z を向く(Player::GetForward と同規約)。
	float cy = cosf(m_walkPitch);
	float yaw = m_player.GetYaw();
	XMFLOAT3 fwd = XMFLOAT3(sinf(yaw) * cy, sinf(m_walkPitch), cosf(yaw) * cy);
	m_camFwd = fwd;	// 照準射線に使う変数も更新(走動中は未使用だが整合させる)

	cam->SetPos (eye);
	cam->SetLook(XMFLOAT3(eye.x + fwd.x, eye.y + fwd.y, eye.z + fwd.z));
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

	const float SENS = 0.0017f;			// 左右(yaw)の感度(rad/px)。低め=据わった視点
	m_lookYaw += dx * SENS;
	// 左右は小さく夹住(KCDの鍛冶は据えカメラ寄り)。上下(pitch)の自由回転は使わない=
	// 代わりにマウス縦で「刃の長手上の狙い位置」m_aimRail を動かし、カメラごと寄せる。
	const float YAW_LIM = 0.26f;
	if (m_lookYaw >  YAW_LIM) m_lookYaw =  YAW_LIM;
	if (m_lookYaw < -YAW_LIM) m_lookYaw = -YAW_LIM;

	// マウスを上げる(dy<0)=奥(far, rail 1)へ / 下げる(dy>0)=手前(near, rail 0)へ。
	// ※ここの符号が「瞄準の上下反転」を決める。rail→世界Z の写像(UpdateAim/ApplyCamera の
	//   rz = anchor.z - half + rail*len)と対で効く。どちらか片方だけ反すと反転する。
	// 感度 m_aimSens は F1「Aim sens」で調整。低い=同じ移動に鼠標を多く要る=慎重で「重い」手応え。
	m_aimRail -= dy * m_aimSens;
	if (m_aimRail < 0.0f) m_aimRail = 0.0f;
	if (m_aimRail > 1.0f) m_aimRail = 1.0f;

	SetCursorPos(cs.x, cs.y);			// 中心へ戻す(累積の基準を保つ)
}

//--- FPS式ヒットスキャン照準。準心(画面中心=カメラ正前方 m_camFwd)から射線を飛ばし、
//    武器を長手にNSEG分割したボックス群と交差させる。当たった一番手前の区域が照準区域。
//    どの区域にも当たらなければ空振り(m_aimValid=false)。判定対象が「武器の実体」なので、
//    準心が刃に乗っている＝射線が刃に当たる、が一致する(Valorant等と同じ原理)。
void SceneForge::UpdateAim()
{
	// 格子寸法の別名(唯一の定義は ForgingSim。ここは読みやすさのための局所別名)。
	const int NL = ForgingSim::NL, NW = ForgingSim::NW, NSEG = ForgingSim::NSEG;
	if (m_wpOk)
	{
		// 狙いは m_aimRail(マウス縦で動く 0=手前..1=奥)で直接決める。射線の当たり外れに依存しないので、
		// カメラが3段のどこに居ても、刃の全長(下半段=手前も)にハンマーが必ず付いてくる。
		// カメラの追従(ApplyCamera)も同じ railZ 式を使うので、視角・準心・ハンマーが一致する。
		float half = m_barLen * 0.5f;
		float rz   = (m_barAnchor.z - half) + m_aimRail * m_barLen;	// 刃上のワールドZ(ハンマー位置)
		m_aimWorld = XMFLOAT3(m_barAnchor.x, m_barAnchor.y, rz);

		// 命中/高亮の段は「武器の局部長軸」規約(WeaponRender の SegCoordLocal)で決まる。
		// m_wpYaw 等で武器が回っていると局部軸と世界Zは向きが逆になり得るので、ハンマーの
		// 世界位置を武器ローカルへ逆変換してから段を求める → 「命中段=ハンマー真下の段」で一致。
		XMVECTOR lp = XMVector3TransformCoord(XMLoadFloat3(&m_aimWorld),
			XMMatrixInverse(nullptr, WeaponWorld()));
		XMFLOAT3 lpf; XMStoreFloat3(&lpf, lp);
		float sc = AimSystem::SegCoordLocal(lpf, m_wpMin, m_wpMax, NSEG);	// 0..NSEG
		m_aimSeg = (int)sc;
		if (m_aimSeg < 0) m_aimSeg = 0; if (m_aimSeg > NSEG - 1) m_aimSeg = NSEG - 1;
		m_aimI = (int)(m_aimRail * NL);
		if (m_aimI < 0) m_aimI = 0; if (m_aimI > NL - 1) m_aimI = NL - 1;
		m_aimJ = NW / 2;
		m_aimValid = true;				// 常に刃のどこかを狙っている(空振り無し)
		return;
	}

	// --- フォールバック(武器FBXが無い時): 旧・射線×砧面水平矩形 ---
	XMFLOAT3 o = { m_camPos[0], m_camPos[1], m_camPos[2] };
	XMFLOAT3 d = m_camFwd;
	// 板の上面を代表する水平面 y = planeY と交差
	float planeY = m_barAnchor.y + m_forging.Start() * 0.5f;
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
	m_aimWorld = XMFLOAT3(cx, m_barAnchor.y + m_forging.Height(m_aimI, m_aimJ), cz);
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

//====================================================================================
//  F1調整値の永続化: Assets/forge_tuning.txt (単純な key value テキスト)
//  行頭のキーで分岐し、必要な数だけ数値を読む。未知キー/欠損は無視(前方互換)。
//  Init で LoadTuning(), Uninit と F1「Save tuning」ボタンで SaveTuning()。
//====================================================================================
static const char* kTuningPath = "Assets/forge_tuning.txt";

void SceneForge::SaveTuning()
{
	FILE* fp = nullptr;
	if (fopen_s(&fp, kTuningPath, "w") != 0 || !fp) return;

	// -- ハンマー --
	fprintf(fp, "restlift %.5f\n",   m_hammer.restLift);
	fprintf(fp, "hscale %.5f\n",     m_hammerScale);
	fprintf(fp, "hrot %.5f %.5f %.5f\n", m_hammerRot[0], m_hammerRot[1], m_hammerRot[2]);
	fprintf(fp, "hoff %.5f %.5f %.5f\n", m_hammerOff[0], m_hammerOff[1], m_hammerOff[2]);
	fprintf(fp, "stiffness %.5f\n",  m_hammer.stiffness);
	fprintf(fp, "damping %.5f\n",    m_hammer.damping);
	fprintf(fp, "mass %.5f\n",       m_hammer.mass);
	fprintf(fp, "impulse %.5f\n",    m_hammer.impulse);
	fprintf(fp, "recoilback %.5f\n", HAMMER_RECOIL_BACK);
	fprintf(fp, "recoiltilt %.5f\n", HAMMER_RECOIL_TILT);
	fprintf(fp, "chargeraise %.5f\n",m_hammer.chargeRaise);
	fprintf(fp, "camshake %.5f\n",   CAM_SHAKE_AMP);
	fprintf(fp, "breathamp %.5f\n",   m_camBreathAmp);
	fprintf(fp, "breathspd %.5f\n",   m_camBreathSpeed);
	fprintf(fp, "tremoramp %.5f\n",   m_camTremorAmp);
	fprintf(fp, "tremorspd %.5f\n",   m_camTremorSpeed);
	fprintf(fp, "tremorramp %.5f\n",  m_camTremorRamp);
	fprintf(fp, "looknoise %.5f\n",   m_camLookNoise);
	fprintf(fp, "aimsens %.6f\n",    m_aimSens);
	fprintf(fp, "hfollow %.5f\n",    m_hammerFollow);
	// -- カメラ --
	fprintf(fp, "walkfloor %.5f\n", m_walkFloorY);
	fprintf(fp, "campos %.5f %.5f %.5f\n",  m_camPos[0],  m_camPos[1],  m_camPos[2]);
	fprintf(fp, "camlook %.5f %.5f %.5f\n", m_camLook[0], m_camLook[1], m_camLook[2]);
	fprintf(fp, "camfov %.5f\n",     m_camFov);
	fprintf(fp, "followz %.5f\n",    m_camFollowZ);
	fprintf(fp, "pangain %.5f\n",    m_camPanGain);
	fprintf(fp, "camlerp %.5f\n",    m_camLerpRate);
	// -- 武器アライン --
	fprintf(fp, "wpyaw %.5f\n",      m_wpYaw);
	fprintf(fp, "wppitch %.5f\n",    m_wpPitch);
	fprintf(fp, "wproll %.5f\n",     m_wpRoll);
	fprintf(fp, "wpscale %.5f\n",    m_wpScale);
	fprintf(fp, "wpoff %.5f %.5f %.5f\n", m_wpOff[0], m_wpOff[1], m_wpOff[2]);

	fclose(fp);
}

//------------------------------------------------------------------------------------
//  可調値の「アドレス表」= 全 tunable を1箇所だけ列挙する。
//  Save/Load はキー文字列と対で書くので別物。ここは Snapshot/Restore 用に
//  「メモリ上の値そのもの」を順序どおり往復コピーするための一覧。
//  ※CAM_SHAKE_AMP / HAMMER_RECOIL_* はこの .cpp の file-static なので、
//    この関数を同じ .cpp に置くことでアドレスが取れる(他ファイルからは不可)。
//------------------------------------------------------------------------------------
void SceneForge::TuningRefs(std::vector<float*>& out)
{
	float* r[] = {
		// -- Hammer --
		&m_hammer.restLift, &m_hammerScale,
		&m_hammerRot[0], &m_hammerRot[1], &m_hammerRot[2],
		&m_hammerOff[0], &m_hammerOff[1], &m_hammerOff[2],
		&m_hammer.stiffness, &m_hammer.damping, &m_hammer.mass, &m_hammer.impulse,
		&HAMMER_RECOIL_BACK, &HAMMER_RECOIL_TILT, &m_hammer.chargeRaise,
		// -- Camera (organic feel) --
		&CAM_SHAKE_AMP, &m_camBreathAmp, &m_camBreathSpeed,
		&m_camTremorAmp, &m_camTremorSpeed, &m_camTremorRamp, &m_camLookNoise,
		// -- Aim & follow --
		&m_aimSens, &m_hammerFollow,
		// -- Camera (view / follow) --
		&m_camPos[0], &m_camPos[1], &m_camPos[2],
		&m_camLook[0], &m_camLook[1], &m_camLook[2],
		&m_camFov, &m_camFollowZ, &m_camPanGain, &m_camLerpRate,
		// -- Weapon align --
		&m_wpYaw, &m_wpPitch, &m_wpRoll, &m_wpScale,
		&m_wpOff[0], &m_wpOff[1], &m_wpOff[2],
	};
	out.assign(r, r + _countof(r));
}

void SceneForge::SnapshotTuning()	// 現在値 → m_tuneStartup(起動時の姿を記録)
{
	std::vector<float*> refs; TuningRefs(refs);
	m_tuneStartup.resize(refs.size());
	for (size_t i = 0; i < refs.size(); ++i) m_tuneStartup[i] = *refs[i];
}

void SceneForge::RestoreTuning()	// m_tuneStartup → 現在値(一発で起動時へ戻す)
{
	if (m_tuneStartup.empty()) return;	// まだ Snapshot していなければ何もしない
	std::vector<float*> refs; TuningRefs(refs);
	for (size_t i = 0; i < refs.size() && i < m_tuneStartup.size(); ++i)
		*refs[i] = m_tuneStartup[i];
}

void SceneForge::LoadTuning()
{
	FILE* fp = nullptr;
	if (fopen_s(&fp, kTuningPath, "r") != 0 || !fp) return;	// 無ければ header の既定のまま

	char line[256];
	while (fgets(line, sizeof(line), fp))
	{
		char key[32];
		if (sscanf_s(line, "%31s", key, (unsigned)_countof(key)) != 1) continue;
		const char* v = line + strlen(key);	// キーの後ろ(数値部)

		if      (strcmp(key, "restlift")   == 0) sscanf_s(v, "%f", &m_hammer.restLift);
		else if (strcmp(key, "hscale")     == 0) sscanf_s(v, "%f", &m_hammerScale);
		else if (strcmp(key, "hrot")       == 0) sscanf_s(v, "%f %f %f", &m_hammerRot[0], &m_hammerRot[1], &m_hammerRot[2]);
		else if (strcmp(key, "hoff")       == 0) sscanf_s(v, "%f %f %f", &m_hammerOff[0], &m_hammerOff[1], &m_hammerOff[2]);
		else if (strcmp(key, "stiffness")  == 0) sscanf_s(v, "%f", &m_hammer.stiffness);
		else if (strcmp(key, "damping")    == 0) sscanf_s(v, "%f", &m_hammer.damping);
		else if (strcmp(key, "mass")       == 0) sscanf_s(v, "%f", &m_hammer.mass);
		else if (strcmp(key, "impulse")    == 0) sscanf_s(v, "%f", &m_hammer.impulse);
		else if (strcmp(key, "recoilback") == 0) sscanf_s(v, "%f", &HAMMER_RECOIL_BACK);
		else if (strcmp(key, "recoiltilt") == 0) sscanf_s(v, "%f", &HAMMER_RECOIL_TILT);
		else if (strcmp(key, "chargeraise")== 0) sscanf_s(v, "%f", &m_hammer.chargeRaise);
		else if (strcmp(key, "camshake")   == 0) sscanf_s(v, "%f", &CAM_SHAKE_AMP);
		else if (strcmp(key, "breathamp")  == 0) sscanf_s(v, "%f", &m_camBreathAmp);
		else if (strcmp(key, "breathspd")  == 0) sscanf_s(v, "%f", &m_camBreathSpeed);
		else if (strcmp(key, "tremoramp")  == 0) sscanf_s(v, "%f", &m_camTremorAmp);
		else if (strcmp(key, "tremorspd")  == 0) sscanf_s(v, "%f", &m_camTremorSpeed);
		else if (strcmp(key, "tremorramp") == 0) sscanf_s(v, "%f", &m_camTremorRamp);
		else if (strcmp(key, "looknoise")  == 0) sscanf_s(v, "%f", &m_camLookNoise);
		else if (strcmp(key, "aimsens")    == 0) sscanf_s(v, "%f", &m_aimSens);
		else if (strcmp(key, "hfollow")    == 0) sscanf_s(v, "%f", &m_hammerFollow);
		else if (strcmp(key, "walkfloor") == 0) sscanf_s(v, "%f", &m_walkFloorY);
		else if (strcmp(key, "campos")     == 0) sscanf_s(v, "%f %f %f", &m_camPos[0],  &m_camPos[1],  &m_camPos[2]);
		else if (strcmp(key, "camlook")    == 0) sscanf_s(v, "%f %f %f", &m_camLook[0], &m_camLook[1], &m_camLook[2]);
		else if (strcmp(key, "camfov")     == 0) sscanf_s(v, "%f", &m_camFov);
		else if (strcmp(key, "followz")    == 0) sscanf_s(v, "%f", &m_camFollowZ);
		else if (strcmp(key, "pangain")    == 0) sscanf_s(v, "%f", &m_camPanGain);
		else if (strcmp(key, "camlerp")    == 0) sscanf_s(v, "%f", &m_camLerpRate);
		else if (strcmp(key, "wpyaw")      == 0) sscanf_s(v, "%f", &m_wpYaw);
		else if (strcmp(key, "wppitch")    == 0) sscanf_s(v, "%f", &m_wpPitch);
		else if (strcmp(key, "wproll")     == 0) sscanf_s(v, "%f", &m_wpRoll);
		else if (strcmp(key, "wpscale")    == 0) sscanf_s(v, "%f", &m_wpScale);
		else if (strcmp(key, "wpoff")      == 0) sscanf_s(v, "%f %f %f", &m_wpOff[0], &m_wpOff[1], &m_wpOff[2]);
	}
	fclose(fp);
}

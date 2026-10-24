// Part of SceneForge, split out of the original single SceneForge.cpp.
// This file carries a UTF-8 BOM so the Japanese comments moved from the
// original source keep compiling correctly under MSVC (no C2601/C1075).
// All SceneForge members share the class declaration in SceneForge.h and the
// file-local helpers declared in SceneForge_Internal.h.
#include "CottageRender.h"
#include "LitRender.h"
#include "PropMaterials.h"
#include "OutdoorStage.h"
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "PropParts.h"
#include "ReflectionProbe.h"
#include "OutlineRender.h"	// 次の工位の輪郭線(背面法)
#include "GpuProfiler.h"
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

//--- 光を受ける物の描画(道具・手に持った鎚/火ばさみ)。全部 LitRender = 壁と同じ PBR の光の式。
//    材質(粗さ・金属度・三平面投影・色)は表(GameData/PropMaterials)をキーで引く=ここに物ごとの特例を書かない
void SceneForge::DrawModelWorld(Model* m, const XMMATRIX& world, const char* materialKey, float highlight)
{
	LitRender::Draw(m, world, GetObj<CameraBase>("Camera"), PropMaterials::Find(materialKey), highlight);
}

// Shared PBR opaque pass; clear glass is drawn after opaque scenery.
void SceneForge::DrawWall(Model* m, const XMMATRIX& world)
{
    CottageRender::Draw(m, world, GetObj<CameraBase>("Camera"),
        GetObj<VertexShader>("VS_Wall"), GetObj<PixelShader>("PS_Wall"), m_coalPos, false);
}

void SceneForge::LoadProp(const char* key, const char* fbx, const char* tex,
                          float targetSize, float px, float py, float pz, float yaw, bool groundSnap)
{
	// 共有キャッシュ: 共有オブジェクトmapはstaticでシーン跨ぎで生存する。編集シーンと
	// キー(St...)を統一済みなので、どちらかが一度読めば assimp 再インポート無しで再利用できる
	// (=シーン切替の数秒ストールを解消)。摩擦: 一度読んだモデルは常駐(許容)。
	Model* m = GetObj<Model>(key);
	if (!m)
	{
		m = CreateObj<Model>(key);
		if (!m->Load(fbx, 1.0f, false, true)) return;	// 読込失敗ならスキップ(欠品でも落ちない)
		if (tex && tex[0])
		{
			auto t = std::make_shared<Texture>();
			if (SUCCEEDED(t->Create(tex))) m->SetTexture(t);
		}
	}
	Prop p;
	p.key = key;
	p.label = key;
	p.pos[0] = px; p.pos[1] = py; p.pos[2] = pz;
	p.yaw = yaw;
	p.groundSnap = groundSnap;
	m->GetLocalAABB(p.aabbMin, p.aabbMax);	// 地面設置＆サイズ正規化に使う境界箱
	if (const PropPart* part = FindPropPart(key))	// 動く部品を別モデルにした道具は、部品込みの箱で配置(元の一体と同じ)
		UnionPartAABB(GetObj<Model>(part->partKey), p.aabbMin, p.aabbMax);

	// モデルごとに生サイズがバラバラなので、AABBの最大辺=targetSize になるよう自動スケール
	// (魔法数字を避け、別モデルに差し替えてもサイズが揃う)
	float ex = p.aabbMax.x - p.aabbMin.x;
	float ey = p.aabbMax.y - p.aabbMin.y;
	float ez = p.aabbMax.z - p.aabbMin.z;
	float maxExtent = ex; if (ey > maxExtent) maxExtent = ey; if (ez > maxExtent) maxExtent = ez;
	p.scale = (maxExtent > 1e-4f) ? (targetSize / maxExtent) : targetSize;
	m_props.push_back(p);	// 衝突用の凸包は配置が決まった後に作る(BuildPropHulls。背丈で頂点を選ぶので高さが要る)
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

//--- 物の境界球(ワールド)。読込時にキャッシュしたモデル空間の境界箱(p.aabbMin/Max)から。
//    配置は 一様スケール × Y回転 × 平行移動 なので、半径 = スケール × 対角線の半分。
void SceneForge::PropSphere(const Prop& p, FXMMATRIX world, XMFLOAT3& center, float& radius) const
{
	const XMFLOAT3& a = p.aabbMin; const XMFLOAT3& b = p.aabbMax;
	XMStoreFloat3(&center, XMVector3TransformCoord(XMVectorSet((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f, 1.0f), world));
	radius = XMVectorGetX(XMVector3Length(world.r[0])) * 0.5f * XMVectorGetX(XMVector3Length(XMVectorSet(b.x - a.x, b.y - a.y, b.z - a.z, 0.0f)));
}

namespace
{
	// 視錐台カリング(view frustum culling): カメラに写る範囲(視錐台)の6枚の面を作り、
	// 物の境界球が1枚でも面の外側に完全に出ていれば描かない。屋内から見えない屋外の木・草、
	// 反射の写真の各面で写らない物を GPU に送らずに済む。
	// 面の取り出し方 = Gribb & Hartmann 2001(view×proj の列の和/差。D3D は z が 0..1 なので近い面は第3列そのまま)
	// 検索: "view frustum culling", "Gribb Hartmann frustum planes" / JP: 視錐台カリング
	struct Frustum { XMVECTOR planes[6]; };

	Frustum MakeFrustum(CameraBase* cam)
	{
		const XMFLOAT4X4 v = cam->GetView(false), pr = cam->GetProj(false);
		const XMMATRIX m = XMMatrixTranspose(XMLoadFloat4x4(&v) * XMLoadFloat4x4(&pr));	// 行 = 元の列
		Frustum f;
		f.planes[0] = m.r[3] + m.r[0];	// 左
		f.planes[1] = m.r[3] - m.r[0];	// 右
		f.planes[2] = m.r[3] + m.r[1];	// 下
		f.planes[3] = m.r[3] - m.r[1];	// 上
		f.planes[4] = m.r[2];			// 近
		f.planes[5] = m.r[3] - m.r[2];	// 遠
		for (XMVECTOR& pl : f.planes) pl = XMPlaneNormalize(pl);
		return f;
	}

	bool SphereVisible(const Frustum& f, const XMFLOAT3& c, float r)
	{
		const XMVECTOR p = XMLoadFloat3(&c);
		for (const XMVECTOR& pl : f.planes)
			if (XMVectorGetX(XMPlaneDotCoord(pl, p)) < -r) return false;	// 球が丸ごと面の外側
		return true;
	}
}

//--- 装飾モデルをまとめて描画(不透明。鉄条より先に)
void SceneForge::DrawScenery()
{
    std::vector<SunStage::Item> casters;
    XMFLOAT3 shadowCenter(0,3,0);
    // 影を落とす物は「影の範囲(カメラ中心の正方形)」に掛かる物だけ。範囲外の屋外の木・石・草まで毎フレーム
    // 影の地図へ描いていた(2026-10-17 F3: 太陽の影 約3ms)。境界箱の球で判定する(安い・取りこぼさない)
    CameraBase* camForShadow = GetObj<CameraBase>("Camera");
    const XMFLOAT3 camPos = camForShadow ? camForShadow->GetPos() : shadowCenter;
    // 範囲 = 屋内 20m(窓の光の縁を細かく) / 屋外 48m(見ている家が丸ごと入る)。
    //   旧: いつも 20m → 屋外で家を見ると家の半分が範囲の外=「影無し(日向)」扱いになり、歩くと影の境目が家の上を這った
    const float shadowRange = ShadowCameraIndoors() ? SHADOW_RANGE_M : SHADOW_RANGE_OUTDOOR_M;
    const XMFLOAT4 sunNow=LitRender::Sun();
    const XMFLOAT4 sunDirOnly(sunNow.x,sunNow.y,sunNow.z,1.0f);   // the shadow map depends only on the DIRECTION (brightness changes must not re-render it)
    for(auto& p:m_props) {
        if(p.key=="StGround" || p.hidden) continue;
        auto world=PropWorld(p);
        if(p.key=="StCottage") XMStoreFloat3(&shadowCenter,XMVector3TransformCoord(XMVectorSet(0,150,0,1),world));
        Model* model = GetObj<Model>(p.key.c_str());
        if (!model) continue;
        float radius; XMFLOAT3 cc;
        PropSphere(p, world, cc, radius);	// 読込時にキャッシュした境界箱から(毎フレーム全頂点を調べない)
        // 太陽から見た正方形に球が掛かるかで判定(正射影の影の地図の形そのもの)。
        //   旧: 地面の上の距離 → 太陽が低いと、範囲の外の木・屋根の長い影が範囲の中へ落ちるのに捨てていた
        if (!SunStage::CasterInRange(sunDirOnly, camPos, shadowRange, cc, radius)) continue;
        casters.push_back(SunStage::MakeItem(model,world));
    }
    if (Model* door = GetObj<Model>(CottageDoor::DOOR_KEY); door && GetProp(CottageDoor::HOUSE_KEY))	// 扉も影を落とす(今の角度で)
        casters.push_back(SunStage::MakeItem(door, DoorWorld()));
    // 影の範囲は「家全体の 48m」でなく「見ている人の周り」だけ(1テクセル 2.3cm → 約 1cm)。
    //   窓から差す光の縁が細かくなる=太陽が動いた時に光の縁が前後に揺れて見えるのを抑える(2026-10-17 F5「先-1再+1」)。
    //   中心はカメラ(歩けば付いて来る。テクセル単位に吸着するので歩いても影の縁は這わない)。
    if (CameraBase* cam = GetObj<CameraBase>("Camera")) shadowCenter = cam->GetPos();
    // 反射の写真を撮っている間は影の地図を作り直さない(本描画で作った物をそのまま使う)。
    //   撮影のカメラ(金床の上)を中心に作り直すと、撮影で1回 + 本描画に戻って1回 = 毎回 2 回余計に描いていた
    if (!m_capturingProbe)
    { GpuProfiler::Scope t((const char*)u8"　・太陽の影（場面の内訳）"); SunStage::Prepare(casters,sunDirOnly,shadowCenter,shadowRange); }
    const XMFLOAT4& skySaved=CottageRender::Data().exteriorSky; const float skyK=LitRender::SkyScale();
    // 空は不透明な物を全部描いた「後」に、何も描かれていない画素だけへ(深度テスト)。
    //   前は最初に画面全体へ描いて、その上を家で塗り潰していた=屋内では丸ごと無駄(2026-10-17 F3: 空 0.8ms)
    auto drawSky = [&]()
    {
        GpuProfiler::Scope t(!m_capturingProbe ? (const char*)u8"　・空" : nullptr);
        SunStage::Sky(GetObj<CameraBase>("Camera"),sunNow,XMFLOAT4(skySaved.x*skyK,skySaved.y*skyK,skySaved.z*skyK,skyK),true);   // the sky darkens at night (w = the same factor for the horizon/clouds)
    };

	if (!m_showScenery) { drawSky(); return; }
	CameraBase* viewCam = GetObj<CameraBase>("Camera");
	const Frustum frustum = viewCam ? MakeFrustum(viewCam) : Frustum{};	// 反射の写真の時は各面のカメラ=面ごとに写る物だけ
	// 家(壁・床)は最後に描く: 先に描いた道具が深度を書いているので、道具の後ろに隠れる壁の画素は
	// 画素シェーダーを走らせる前に捨てられる(アーリー Z / early-Z rejection)。逆順だと壁を塗ってから道具で塗り直していた
	// 描く順 = 屋内の道具 → 家 → 屋外。屋外の木・草は屋内からは壁の向こう=家の深度で丸ごと捨てられる
	//   (家より先に描くと、見えない木・草まで全部塗っていた。2026-10-17 F3: 屋外 0.03ms → 2.7ms になった)
	Prop* housePropToDraw = nullptr;
	std::vector<Prop*> outdoorToDraw;
	for (auto& p : m_props)
	{
		if(p.key=="StGround" || p.hidden) continue;	// hidden=手に取った火钳など
		Model* m = GetObj<Model>(p.key.c_str());
		if (!m) continue;
		if (viewCam)
		{
			XMFLOAT3 c; float r;
			PropSphere(p, PropWorld(p), c, r);
			if (!SphereVisible(frustum, c, r)) continue;	// カメラに写らない=描かない(影の地図には上で別に描いている)
		}
		// F3 の内訳(反射の写真の撮影中は測らない=本描画の数字に混ざらない様に)
		const bool timed = !m_capturingProbe;
		// 整屋は専用のPBRシェーダー(triplanar)で描く。他プロップは共通シェーダー。
		if (p.key == CottageDoor::HOUSE_KEY) { housePropToDraw = &p; continue; }	// 下で最後に描く
        if(OutdoorStage::IsOutdoor(p.key)) { outdoorToDraw.push_back(&p); continue; }	// 家の後で描く(下)
		GpuProfiler::Scope t(timed ? (const char*)u8"　・道具（金床・炉など）" : nullptr);
		const float hi = StationHighlight(p.key);	// 次に向かう工位なら 0..1(縁の光 + 輪郭線)
		XMMATRIX propWorld = PropWorld(p);
		if (p.key == "StBellows" && m_bellowsSquash > 0.0f)
		{
			// ふいごを踏んだ: 底を支点に縦へ押し縮める(上の板が下がる=袋が潰れて風が出る)。底の高さはワールドの箱から
			XMFLOAT3 bmn, bmx;
			if (PropWorldBox(p, bmn, bmx))
				propWorld = propWorld * XMMatrixTranslation(0.0f, -bmn.y, 0.0f)
				                      * XMMatrixScaling(1.0f, 1.0f - BELLOWS_SQUASH * m_bellowsSquash, 1.0f)
				                      * XMMatrixTranslation(0.0f, bmn.y, 0.0f);
		}
		DrawModelWorld(m, propWorld, p.key.c_str(), hi * STATION_HI_GLOW);	// 炉の暗い暖色・三平面投影も材質表の値(旧: ここに炉だけの特例)
		// 動く部品(砥石の輪)は本体と同じ行列の手前で、自分の軸まわりに回してから描く。
		// 角度は砥石の物理(GrindWheel)が積分した値=踏んだ分だけ回り、摩擦で止まる。
		const PropPart* part = FindPropPart(p.key.c_str());
		Model* pm = part ? GetObj<Model>(part->partKey) : nullptr;
		if (pm) DrawModelWorld(pm, PartSpin(pm, m_wheel.Angle()) * PropWorld(p), p.key.c_str(), hi * STATION_HI_GLOW);	// 部品は本体と同じ材質
		if (hi > 0.0f)	// 輪郭線(背面法)。本体の後に描く=本体が覆う所は深度で隠れ、縁の外側だけ残る
		{
			const XMFLOAT4 OUTLINE_COL(1.0f * STATION_OUTLINE_BRIGHT, 0.62f * STATION_OUTLINE_BRIGHT, 0.25f * STATION_OUTLINE_BRIGHT, STATION_OUTLINE_ALPHA * hi);	// 縁の光(PS_StageProp HIGHLIGHT_COL)と同じ暖色
			OutlineRender::Draw(m, PropWorld(p), GetObj<CameraBase>("Camera"), OUTLINE_COL, STATION_OUTLINE_M);
			if (pm) OutlineRender::Draw(pm, PartSpin(pm, m_wheel.Angle()) * PropWorld(p), GetObj<CameraBase>("Camera"), OUTLINE_COL, STATION_OUTLINE_M);
		}
	}
	{ GpuProfiler::Scope t(!m_capturingProbe ? (const char*)u8"　・炭" : nullptr); DrawCoalBed(); }	// 光る炭ベッド(自作)
	if (housePropToDraw)
	{
		GpuProfiler::Scope t(!m_capturingProbe ? (const char*)u8"　・家（壁・床・屋根）" : nullptr);
		DrawWall(GetObj<Model>(housePropToDraw->key.c_str()), PropWorld(*housePropToDraw));
		// 裏口の扉(家から切り出した部品)。家と同じ行列 × 蝶番の回転=閉じていれば戸口にぴったり収まる。
		if (Model* door = GetObj<Model>(CottageDoor::DOOR_KEY)) DrawWall(door, DoorWorld());
	}
	for (Prop* op : outdoorToDraw)
	{
		GpuProfiler::Scope t(!m_capturingProbe ? (const char*)u8"　・屋外（木・草・石）" : nullptr);
		Model* om = GetObj<Model>(op->key.c_str());
		const bool yard = (op->key == "StOutdoorGround");
		if (yard && m_hideYard) continue;	// F1「Hide yard ground」(Z-fighting の原因確認用)
		if (yard) SetYardRaster(true);		// 地面だけ深度バイアス(戸口の敷居=家の地基の上面と 1.4cm で重なる)
		// 草だけ VS_Grass(玩家を避けて倒れる)。出力は VS_Wall と同じなので PS/材質はそのまま。
		const bool grass = IsGrass(op->key) && m_grassMap.IsReady();
		VertexShader* vs = GetObj<VertexShader>(grass ? "VS_Grass" : "VS_Wall");
		if (grass && vs) BindGrassParams(vs, *op);
		CottageRender::Draw(om,PropWorld(*op),GetObj<CameraBase>("Camera"),vs,GetObj<PixelShader>("PS_Wall"),m_coalPos,false,true);
		if (grass && vs) vs->SetTexture(0, nullptr);	// 次のフレームで貼图を描画先にする前に、読み取り側から外す
		if (yard) SetYardRaster(false);
	}
	drawSky();	// 不透明な物を全部描いた後(空の見える画素だけ)。窓ガラスより前(ガラスは空の上に重ねる)
    if (Prop* house = GetProp("StCottage"))
    {
        GpuProfiler::Scope t(!m_capturingProbe ? (const char*)u8"　・窓ガラス" : nullptr);
        CottageRender::Draw(GetObj<Model>("StCottage"), PropWorld(*house),
            GetObj<CameraBase>("Camera"), GetObj<VertexShader>("VS_Wall"),
            GetObj<PixelShader>("PS_Wall"), m_coalPos, true);
    }

}

//--- 反射プローブ(IBL 第1段): 金床の少し上から、動かない場面(部屋・道具・炭)を 6 方向に撮って立方体の画像にする。
//    撮らない物: 刃・ハンマー・手の火钳・火花/余燼 = 動く物。毎フレームは撮り直さないので、写すと位置がずれた反射になる。
//    炉の火は揺らぎの平均の明るさ(倍率 1)で撮る=反射の中の火は揺れない(静的プローブの普通の割り切り)。
void SceneForge::CaptureReflectionProbe()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	if (!cam || m_props.empty()) return;
	const XMFLOAT3 pos(m_barAnchor.x, m_barAnchor.y + m_probeLift, m_barAnchor.z);
	float firePos[3]; FireLightPos(firePos);
	LitRender::SetFire(firePos, 1.0f);
	m_capturingProbe = true;	// 撮影中: 影の地図は作り直さない(DrawScenery)
	ReflectionProbe::Capture(cam, pos, [this]()
	{
		CottageRender::ClearExterior();	// 本描画と同じく空の色で塗ってから
		DrawScenery();
	});
	m_capturingProbe = false;
	LitRender::SetFire(firePos, FireLightScale());	// このフレームの本描画の火に戻す
}

//--- 太陽の時刻を進め、描画に渡す太陽を決める(毎フレーム)。
//    向きは毎フレーム渡す=太陽の影(シャドウマップ)も毎フレーム描き直す。光の筋・床の光斑が滑らかに流れる。
//    (旧: 0.25° 動くごとに更新=影が「カクッ」と段ごとに跳んだ。2026-10-17 F5「一下一下的」)
//    反射の写真(6面の描画)はずっと重いので、PROBE_RECAPTURE_DEG 動くごとに撮り直す。
void SceneForge::UpdateSun(float dt)
{
	m_sun.Update(dt);
	const XMFLOAT3 dir = m_sun.Direction();
	const float d = dir.x * m_sunApplied.x + dir.y * m_sunApplied.y + dir.z * m_sunApplied.z;
	const float moved = m_sunAppliedSet ? XMConvertToDegrees(acosf(fminf(fmaxf(d, -1.0f), 1.0f))) : 0.0f;
	m_sunApplied = dir;
	m_sunAppliedSet = true;
	m_sunSinceCaptureDeg += moved;
	if (m_sunSinceCaptureDeg >= PROBE_RECAPTURE_DEG)
	{
		m_sunSinceCaptureDeg = 0.0f;
		ReflectionProbe::RequestRecapture();
	}
	LitRender::SetSun(m_sunApplied, m_sun.Daylight());
}

//--- ボックスプロジェクションの部屋の箱: 家のモデルの境界箱(8隅)を今の配置でワールドへ運び、m_probeBoxInset だけ内側へ縮める。
//    軸に沿った箱(AABB)なので、家の向き(Yaw)が 90° の倍数の時に壁とぴったり合う(今は 0°)。
void SceneForge::UpdateProbeRoomBox()
{
	Prop*  house = GetProp(CottageDoor::HOUSE_KEY);
	Model* m     = GetObj<Model>(CottageDoor::HOUSE_KEY);
	if (!house || !m) return;
	XMFLOAT3 lmn, lmx; m->GetLocalAABB(lmn, lmx);
	const XMMATRIX world = PropWorld(*house);
	XMVECTOR mn = XMVectorReplicate(FLT_MAX), mx = XMVectorReplicate(-FLT_MAX);
	for (int i = 0; i < 8; ++i)
	{
		const XMVECTOR c = XMVector3TransformCoord(XMVectorSet((i & 1) ? lmx.x : lmn.x, (i & 2) ? lmx.y : lmn.y, (i & 4) ? lmx.z : lmn.z, 1.0f), world);
		mn = XMVectorMin(mn, c); mx = XMVectorMax(mx, c);
	}
	const XMVECTOR inset = XMVectorReplicate(m_probeBoxInset);
	XMFLOAT3 bmn, bmx;
	XMStoreFloat3(&bmn, XMVectorAdd(mn, inset));
	XMStoreFloat3(&bmx, XMVectorSubtract(mx, inset));
	ReflectionProbe::SetRoomBox(bmn, bmx);
}

//--- カメラが家の中か = 反射プローブの部屋の箱(家の境界箱を内側へ縮めた物)の中。UpdateProbeRoomBox の後で呼ぶ。
bool SceneForge::ShadowCameraIndoors()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	XMFLOAT3 mn, mx;
	if (!cam || !ReflectionProbe::RoomBox(mn, mx)) return true;	// 家が無い=従来通り 20m
	const XMFLOAT3 e = cam->GetPos();
	return e.x > mn.x && e.x < mx.x && e.y > mn.y && e.y < mx.y && e.z > mn.z && e.z < mx.z;
}

//--- 地面用の深度バイアス(depth bias)付きラスタライザ。今のステート(既定 = 裏面も描く)を写し、バイアスだけ足す。
//    深度バイアス = 深度テストの時だけ面を奥へずらす GPU の機能(D3D11_RASTERIZER_DESC::DepthBias / SlopeScaledDepthBias)。
//    重なった2枚の面のどちらを前にするかを決めて Z-fighting を止める定番の手(デカールや影の地図と同じ)。
//    検索: "depth bias z-fighting", "polygon offset" / JP: 深度バイアス
void SceneForge::SetYardRaster(bool on)
{
	static ID3D11RasterizerState* s_prev = nullptr;	// 差し替える前のステート(戻す用)
	static ID3D11RasterizerState* s_bias = nullptr;	// バイアス付き(値が変わった時だけ作り直す)
	static int s_biasUnits = 0;
	ID3D11DeviceContext* ctx = GetContext();
	if (!on)
	{
		if (s_prev) { ctx->RSSetState(s_prev); s_prev->Release(); s_prev = nullptr; }
		return;
	}
	if (!m_yardDepthBias) return;	// F1 で OFF = 何も変えない(原因の確認用)
	ctx->RSGetState(&s_prev);
	if (!s_prev) return;
	if (!s_bias || s_biasUnits != m_yardBiasUnits)
	{
		if (s_bias) { s_bias->Release(); s_bias = nullptr; }
		D3D11_RASTERIZER_DESC desc;
		s_prev->GetDesc(&desc);
		desc.DepthBias = m_yardBiasUnits;
		desc.SlopeScaledDepthBias = YARD_SLOPE_DEPTH_BIAS;
		if (FAILED(GetDevice()->CreateRasterizerState(&desc, &s_bias))) s_bias = nullptr;
		s_biasUnits = m_yardBiasUnits;
	}
	if (s_bias) ctx->RSSetState(s_bias);
}

//--- F1「Show shadow range」: 影の地図が掛かる正方形(太陽から見た正方形)を、太陽の向きに沿って床の高さへ落として描く。
//    この線の外側は「影の計算をしない=全部日向」。屋外 48m / 屋内 20m が切り替わるのも見える。
void SceneForge::DrawShadowRange()
{
	CameraBase* cam = GetObj<CameraBase>("Camera");
	const SunStage::Resources& r = SunStage::Data();
	if (!cam || !r.hasShadow) return;
	const XMFLOAT4 sun = LitRender::Sun();
	XMVECTOR right, upL; SunStage::LightAxes(sun, right, upL);
	const XMVECTOR toSun = XMVector3Normalize(XMVectorSet(sun.x, sun.y, sun.z, 0.0f));
	const float sunY = XMVectorGetY(toSun);
	const float MIN_SUN_Y = 0.05f;	// 太陽が地平線すれすれ=床へ落とすと無限に伸びる。それ以下は描かない
	if (sunY < MIN_SUN_Y) return;
	const XMVECTOR c = XMLoadFloat3(&r.center);
	const float h = r.width * 0.5f;
	const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
	XMFLOAT3 q[4];
	for (int i = 0; i < 4; ++i)
	{
		const XMVECTOR p = c + right * (sx[i] * h) + upL * (sy[i] * h);
		const float t = (XMVectorGetY(p) - m_walkFloorY) / sunY;	// 太陽の光線に沿って床まで下ろす
		XMStoreFloat3(&q[i], p - toSun * t);
	}
	XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
	Geometory::SetWorld(id);
	Geometory::SetView(cam->GetView());
	Geometory::SetProjection(cam->GetProj());
	const XMFLOAT4 SHADOW_RANGE_COLOR(0.2f, 0.9f, 1.0f, 1.0f);	// 水色 = 影の範囲
	Geometory::SetColor(SHADOW_RANGE_COLOR);
	for (int i = 0; i < 4; ++i) Geometory::AddLine(q[i], q[(i + 1) % 4]);
	SetDepthTest(DEPTH_DISABLE);	// 壁越しに見える(デバッグ表示)
	Geometory::DrawLines();
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

//--- 自作の光る炭ベッド。合成炭テクスチャを明るく描き、時間で明滅させる(Bloomで光る)
void SceneForge::DrawCoalBed()
{
	if (m_hideCoalTest) return;	// 【診断】Kキーで炭を隠して炉のtexture跳動を切り分ける
	if (!m_coalOn || !m_coalBedMesh) return;
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
	// Geometry-driven charcoal needs no atlas texture.
	m_coalBedMesh->Draw();
}

//--- 水槽の水面を描画(真の屈折)。
//    直前までに描かれた不透明シーンを PostProcess がスナップショットし、
//    水面PS(PS_Water)がそれを法線でずらしてサンプルする=槽の中が透けて見える。
//    メッシュは炭と同じ ±1 水平板を流用し、world で位置/大きさを与える。
void SceneForge::DrawWater()
{
	if (!m_waterOn || !m_waterMesh || !m_waterHeightTex || !g_pPost) return;
	CameraBase*   cam   = GetObj<CameraBase>("Camera");
	VertexShader* vs    = GetObj<VertexShader>("VS_Water");	// 波の高さで格子を上下させる
	PixelShader*  ps    = GetObj<PixelShader>("PS_Water");
	DepthStencil* depth = GetObj<DepthStencil>("DSV");
	RenderTarget* scene = g_pPost->GetSceneRT();
	if (!cam || !vs || !ps || !depth || !scene) return;

	// 波の高さ(WaterSim)を貼图へ送る。1行 = NX 個の float(R32_FLOAT)。
	GetContext()->UpdateSubresource(m_waterHeightTex->GetTexture2D(), 0, nullptr, m_waterSim.Heights(),
		WaterSim::NX * sizeof(float), 0);

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
	XMFLOAT4 cb[6];
	cb[0] = XMFLOAT4(m_time, (float)refr->GetWidth(), (float)refr->GetHeight(), m_waterBump);
	cb[1] = XMFLOAT4(projNT._33, projNT._43, m_waterFoam, m_waterDepthFade);
	// cb[2] = 視点位置 + 水面の縦横比(PSが両端を丸く切る=水槽の角丸に合わせる)
	// cb[3] = 室内の環境光(水面の反射色に使う。空の明るい色を映すと室内で浮く)
	const float MIN_WATER_WIDTH = 0.001f;	// 幅0で割らない為の下限
	XMFLOAT3 eye = cam->GetPos();
	cb[2] = XMFLOAT4(eye.x, eye.y, eye.z, m_waterSize[0] / std::max(m_waterSize[1], MIN_WATER_WIDTH));
	const auto& lighting = CottageRender::Data();
	cb[3] = XMFLOAT4(lighting.ambient.x, lighting.ambient.y, lighting.ambient.z, lighting.windowExtra.w * LitRender::FillScale());	// F2「環境光」と昼夜に従う
	// cb[4] = 水面の全長/全幅 + yaw の cos/sin(PS が波の傾きを板のローカルからワールドへ回す)
	// cb[5] = 高さ貼图の1テクセルの UV 幅(中心差分の幅)
	cb[4] = XMFLOAT4(m_waterSize[0] * 2.0f, m_waterSize[1] * 2.0f, cosf(m_waterYaw), sinf(m_waterYaw));
	cb[5] = XMFLOAT4(1.0f / WaterSim::NX, 1.0f / WaterSim::NZ, 0.0f, 0.0f);
	ps->WriteBuffer(0, cb);

	// 深度バッファをテクスチャとして読むため、一旦DSVをOMから外す(sceneRTだけ描画先に)。
	// これで「同一リソースを深度書き込みとSRV読みに同時使用」する競合を避ける。
	// 深度テストは無効化し、遮蔽は水PS側で深度を比較して discard で行う。
	SetRenderTargets(1, &scene, nullptr);
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_DISABLE);
	vs->Bind(); ps->Bind();
	ReflectionProbe::BindForLighting();			// t6〜t8 = 部屋の反射(IBL)。水面は窓や炉口を映す
	vs->SetTexture(0, m_waterHeightTex.get());	// VS t0 = 波の高さ(頂点を上下)
	ps->SetTexture(0, refr);					// t0 = 屈折元(背後のシーン)
	ps->SetTexture(1, depth);					// t1 = シーン深度(R32_FLOAT)
	ps->SetTexture(2, m_waterHeightTex.get());	// t2 = 波の高さ(法線)
	m_waterMesh->Draw();

	// SRVを外し、描画先を sceneRT + 深度に戻す(この後の余燼/火花が深度テストできるように)。
	// 外すのは ps->SetTexture(…, nullptr) で: Shader は貼った画像を覚えていて次の Bind() で貼り直す。
	//   GPU から外すだけ(PSSetShaderResources)だと、画質を変えて場面の画像/深度を作り直した後、
	//   次の Bind() が「もう解放された古い画像」を貼り直して落ちた(2026-10-17 F5: 高画質へ切替で例外)
	ps->SetTexture(0, nullptr);
	ps->SetTexture(1, nullptr);
	ps->SetTexture(2, nullptr);
	ID3D11ShaderResourceView* pNull[1] = { nullptr };
	GetContext()->VSSetShaderResources(0, 1, pNull);
	SetRenderTargets(1, &scene, depth);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

//--- 鍛冶素材の3Dモデルを描画(金床＋ハンマー)
void SceneForge::DrawModelsTest()
{
	if (!m_show3D) return;
	DrawScenery();	// 床・樹桩・金床・炉・風箱・作業台・水槽・道具などを一括描画(金床もここ)
	DrawHammer3D();
	DrawCarry();	// 火钳(手に持っている時=鉄を咥える / そうでない時=左腰)
}


//====================================================================
//  炭火の余燼(火の粉) : 炭床から持続的に発生し、熱気で上昇して淡出する
//====================================================================
// 余燼の発生と運動(浮力/上昇/淡出)は Physics/Particles が担当(SceneForge::Update から駆動)。

//--- 余燼(火の粉)をカメラ向きの丸い光点(ビルボード)で加算描画。火花と同じシェーダー/グロー貼り
void SceneForge::DrawEmbers()
{
	if (m_particles.Embers().empty()) return;
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
	for (const Particles::Particle& e : m_particles.Embers())
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

//--- 淬火の蒸気と水しぶき: カメラ向きの柔らかい円(余燼と同じ光の粒テクスチャ)を「アルファ合成」で重ねる。
//    余燼(加算=光る)と違い、蒸気は光らず背景を白く覆う物なので BLEND_ALPHA。
//    寿命の始めにふわっと濃くなり、終わりに向かって薄れる(大きさは Particles 側で膨らむ)。
//    ※本番の蒸気の見た目(ソフトパーティクル等)は外観担当が差し替えてよい。発生/運動は Particles が持つ。
void SceneForge::DrawSteam()
{
	if (m_particles.Steam().empty() && m_particles.Splash().empty()) return;
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Forge");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Forge");
	if (!cam || !vs || !ps || !m_mesh) return;

	const XMFLOAT3 STEAM_COLOR(0.86f, 0.88f, 0.90f);	// 白っぽい灰(室内の暖色光で少し沈む)
	const float    STEAM_OPACITY = 0.35f;				// 1粒の最大不透明度(重なって濃くなる)
	const float    FADE_IN       = 0.15f;				// 寿命の最初この割合で濃くなる
	const XMFLOAT3 SPLASH_COLOR(0.92f, 0.95f, 1.0f);	// 水しぶき: 光を受けた水滴(蒸気より白く、少し青み)
	const float    SPLASH_OPACITY = 0.85f;				// 小さく濃い(蒸気の様に重ねて濃くする物ではない)

	XMFLOAT3 camPos = cam->GetPos();
	XMVECTOR vcam    = XMLoadFloat3(&camPos);
	XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
	XMFLOAT4X4 camMat[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, camMat);

	// カメラ向きの四角(ビルボード)を1枚足す。蒸気と水しぶきで同じ形、色と濃さだけ違う。
	int v = 0;
	auto addQuad = [&](const Particles::Particle& p, const XMFLOAT4& col) -> bool
	{
		if (v + 6 > (int)m_vtx.size()) return false;
		XMVECTOR c     = XMLoadFloat3(&p.pos);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));
		XMVECTOR right = XMVector3Cross(worldUp, toCam);
		if (XMVectorGetX(XMVector3Length(right)) < 0.001f) right = XMVectorSet(1, 0, 0, 0);
		right = XMVector3Normalize(right);
		XMVECTOR up = XMVector3Normalize(XMVector3Cross(toCam, right));
		XMVECTOR R = XMVectorScale(right, p.size);
		XMVECTOR U = XMVectorScale(up,    p.size);

		XMFLOAT3 tl, tr, bl, br;
		XMStoreFloat3(&tl, XMVectorAdd(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&tr, XMVectorAdd(XMVectorAdd(c, R), U));
		XMStoreFloat3(&bl, XMVectorSubtract(XMVectorSubtract(c, R), U));
		XMStoreFloat3(&br, XMVectorSubtract(XMVectorAdd(c, R), U));

		Vertex* q = &m_vtx[v];
		q[0] = { tl, XMFLOAT2(0,0), col };
		q[1] = { tr, XMFLOAT2(1,0), col };
		q[2] = { bl, XMFLOAT2(0,1), col };
		q[3] = { bl, XMFLOAT2(0,1), col };
		q[4] = { tr, XMFLOAT2(1,0), col };
		q[5] = { br, XMFLOAT2(1,1), col };
		v += 6;
		return true;
	};

	for (const Particles::Particle& p : m_particles.Steam())
	{
		float age = 1.0f - p.life / p.maxLife;			// 0→1
		float a = (age < FADE_IN) ? age / FADE_IN : (1.0f - age) / (1.0f - FADE_IN);
		if (!addQuad(p, XMFLOAT4(STEAM_COLOR.x, STEAM_COLOR.y, STEAM_COLOR.z, a * STEAM_OPACITY))) break;
	}
	for (const Particles::Particle& p : m_particles.Splash())	// 水へ落ちた瞬間に消える=薄れさせない
		if (!addQuad(p, XMFLOAT4(SPLASH_COLOR.x, SPLASH_COLOR.y, SPLASH_COLOR.z, SPLASH_OPACITY))) break;
	if (v == 0) return;

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_TEST);	// 深度は見るが書かない(半透明)
	ps->SetTexture(0, m_glow.get());
	m_mesh->Write(m_vtx.data());
	vs->Bind();
	ps->Bind();
	m_mesh->Draw(v);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

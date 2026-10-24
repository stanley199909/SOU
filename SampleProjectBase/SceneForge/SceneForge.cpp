#include "CoalBedMesh.h"
#include "CottageRender.h"
#include "OutdoorStage.h"
#include "SceneForge.h"
#include "DirectX.h"
#include "MeshBuffer.h"
#include "Shader.h"
#include "Texture.h"
#include "TextureCache.h"	// パス単位の貼图キャッシュ(シーン跨ぎで PNG を再解码しない)
#include "CameraBase.h"
#include "LightBase.h"
#include "Model.h"
#include "Geometory.h"
#include "Input.h"
#include "InputActions.h"	// ゲームの操作は動作(Action)で聞く(キーの割り当ては表=データ)
#include "DebugUI.h"
#include "Defines.h"
#include "Audio.h"
#include "PostProcess.h"
#include "AimSystem.h"
#include "Lerp.h"
#include "LitRender.h"
#include "ReflectionProbe.h"
#include "VolumetricLight.h"
#include "OutlineRender.h"
#include "GpuProfiler.h"
#include "SceneForge/SceneForge_Internal.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

using namespace DirectX;

// 刀身のシェーダーは VS_Wp.hlsl / PS_Wp.hlsl(.cso)。Lighting.hlsli を #include する為に文字列からファイルへ移した(2026-10-15)。

//--- 目標ゴースト用PS: VS_Wp を流用し、氷のような半透明+輪郭(菲涅尔風)で「完成形」を薄く示す。
//    氧化斑や強い陰影は入れない(実体と区別できる、クリーンな輪郭にする)。
static const char* g_ghostPS = R"EOT(
struct PIN{ float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; };
float4 main(PIN i):SV_TARGET{
  float3 n = normalize(i.nrm);
  float d = 0.55 + 0.45*saturate(dot(n, normalize(float3(0.35,0.85,-0.4))));
  float rim = pow(1.0 - saturate(abs(n.z)), 2.0);   // 縁を立てる安価な擬似フレネル
  float a = i.col.a * (0.35 + 0.65*rim);            // 縁は濃く、面は薄く
  return float4(i.col.rgb * d, a);
}
)EOT";

// 水面の屈折用に、シーンのスナップショットを撮る PostProcess を参照する(Mainのグローバル)。
extern std::shared_ptr<PostProcess> g_pPost;

// パーティクル用シェーダーは Assets/Shader/VS_Particle.cso / PS_Particle.cso として
// .hlsl から fxc でコンパイルし、Init で Load する(火花・余燼で共用)。

float frand()                 { return (float)rand() / (float)RAND_MAX; }
float frand(float a, float b)  { return a + (b - a) * frand(); }

//--- 3D鉄条用シェーダー(頂点色をそのまま出す=発光する熱い金属) ---
static const char* g_barVS = R"EOT(
cbuffer Cam : register(b0){ float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float4 col:TEXCOORD1; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.col=v.col; return o; }
)EOT";
static const char* g_barPS = R"EOT(
struct PIN{ float4 pos:SV_POSITION; float4 col:TEXCOORD1; };
float4 main(PIN i):SV_TARGET{ return i.col; }
)EOT";

// 光る炭ベッド用シェーダーは Assets/Shader/VS_Coal.cso / PS_Coal.cso として
// .hlsl から fxc でコンパイルし、Init で Load する(実行時Compileの文字列は廃止)。

//--- 温度(0..1)を鋼の色勾配(7ストップ)でサンプル。r,g,b は 0..1。
//    HeatRGB(3D武器/鉄条)と HUD の HeatColor がこの1つの表を共有する(表の二重定義を避ける)。
void HeatSampleRGB(float h, float& r, float& g, float& b)
{
	static const float sH[] = { 0.00f, 0.20f, 0.40f, 0.55f, 0.70f, 0.85f, 1.00f };
	static const float sR[] = { 0.15f, 0.45f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f };
	static const float sG[] = { 0.05f, 0.06f, 0.15f, 0.45f, 0.65f, 0.88f, 1.00f };
	static const float sB[] = { 0.05f, 0.02f, 0.02f, 0.05f, 0.15f, 0.45f, 0.92f };
	const int N = 7;
	if (h < sH[0]) h = sH[0];
	if (h > sH[N - 1]) h = sH[N - 1];
	int i = 0; while (i < N - 1 && h > sH[i + 1]) ++i;
	float t = (h - sH[i]) / (sH[i + 1] - sH[i]);
	r = sR[i] + (sR[i + 1] - sR[i]) * t;
	g = sG[i] + (sG[i + 1] - sG[i]) * t;
	b = sB[i] + (sB[i + 1] - sB[i]) * t;
}

//--- 温度(0..1)を鋼の色(float4)に変換。冷たいときも暗い金属色で見える
DirectX::XMFLOAT4 HeatRGB(float h, float dmg)
{
	// 冷たくても暗い金属として見えるよう各成分に下限を設ける
	constexpr float FLOOR_R = 0.22f, FLOOR_G = 0.20f, FLOOR_B = 0.20f;
	constexpr float DMG_DARKEN = 0.7f;	// 損傷1.0で明るさを最大この割合だけ落とす
	float r, g, b; HeatSampleRGB(h, r, g, b);
	if (r < FLOOR_R) r = FLOOR_R;
	if (g < FLOOR_G) g = FLOOR_G;
	if (b < FLOOR_B) b = FLOOR_B;
	float d = 1.0f - DMG_DARKEN * dmg;	// 損傷で暗くなる
	return DirectX::XMFLOAT4(r * d, g * d, b * d, 1.0f);
}

//--- マウス位置をクライアント座標(ピクセル)で取得。ImGuiのタイミングに依存しない。
//    cw/ch はクライアント(=描画)の実サイズ。どのフレーム段階でも同じ値になる。
void GetMouseClient(float& mx, float& my, float& cw, float& ch)
{
	POINT p; GetCursorPos(&p);
	HWND hwnd = GetActiveWindow();
	RECT rc = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	if (hwnd) { ScreenToClient(hwnd, &p); GetClientRect(hwnd, &rc); }
	mx = (float)p.x; my = (float)p.y;
	cw = (float)(rc.right - rc.left); ch = (float)(rc.bottom - rc.top);
	if (cw < 1.0f) cw = (float)SCREEN_WIDTH;
	if (ch < 1.0f) ch = (float)SCREEN_HEIGHT;
}

void SceneForge::Init()
{
	m_fade.StartCovered();	// 起動時は黒からタイトルへ淡入
	m_vtx.resize(Particles::MAX_SPARKS * 6);	// 火花描画の頂点バッファ(粒子上限×6頂点)

	// 一人称プレイヤ: 砧の手前(−Z側)に立たせ、砧の方(+Z)を向かせる。走動速度を設定。
	m_player.Init(DirectX::XMFLOAT3(0.0f, m_walkFloorY, -2.0f), 0.0f);
	m_player.SetMoveSpeed(m_walkSpeed);	// 単位/秒(歩き)。F1「Walk / Player」で調整

	// 火花/余燼用パーティクルシェーダー(.hlsl → fxc → .cso をLoad)
	VertexShader* vs = CreateObj<VertexShader>("VS_Forge");
	if (FAILED(vs->Load("Assets/Shader/VS_Particle.cso")))
		MessageBox(nullptr, "VS_Particle.cso", "Shader Error", MB_OK);
	PixelShader* ps = CreateObj<PixelShader>("PS_Forge");
	if (FAILED(ps->Load("Assets/Shader/PS_Particle.cso")))
		MessageBox(nullptr, "PS_Particle.cso", "Shader Error", MB_OK);
	// 火花の線は貼图でなく式で描く(PS_Spark)=どの大きさでもぼやけない。余燼/蒸気の丸い粒は PS_Particle のまま。
	PixelShader* sps = CreateObj<PixelShader>("PS_Spark");
	if (FAILED(sps->Load("Assets/Shader/PS_Spark.cso")))
		MessageBox(nullptr, "PS_Spark.cso", "Shader Error", MB_OK);
	// 研ぎの区域が仕上がった瞬間の光芒(スターグリント)。形は式で描く(貼图なし)
	PixelShader* glintPS = CreateObj<PixelShader>("PS_Glint");
	if (FAILED(glintPS->Load("Assets/Shader/PS_Glint.cso")))
		MessageBox(nullptr, "PS_Glint.cso", "Shader Error", MB_OK);
	// 試し切りの刀光(ウェポントレイル)。形は式で描く(貼图なし)
	PixelShader* trailPS = CreateObj<PixelShader>("PS_Trail");
	if (FAILED(trailPS->Load("Assets/Shader/PS_Trail.cso")))
		MessageBox(nullptr, "PS_Trail.cso", "Shader Error", MB_OK);

	// 光の粒テクスチャ(中心が明るい)
	const int S = 64;
	std::vector<unsigned char> pix(S * S * 4);
	for (int y = 0; y < S; ++y)
	for (int x = 0; x < S; ++x)
	{
		float dx = (x + 0.5f) / S * 2 - 1;
		float dy = (y + 0.5f) / S * 2 - 1;
		float f = 1.0f - sqrtf(dx * dx + dy * dy);
		if (f < 0) f = 0;
		f = f * f;
		unsigned char c = (unsigned char)(f * 255);
		int idx = (y * S + x) * 4;
		pix[idx] = pix[idx + 1] = pix[idx + 2] = pix[idx + 3] = c;
	}
	m_glow = std::make_shared<Texture>();
	m_glow->Create(DXGI_FORMAT_R8G8B8A8_UNORM, S, S, pix.data());

	MeshBuffer::Description desc = {};
	desc.pVtx = m_vtx.data();
	desc.vtxSize = sizeof(Vertex);
	desc.vtxCount = (UINT)m_vtx.size();
	desc.isWrite = true;
	desc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_mesh = std::make_shared<MeshBuffer>(desc);

	// 鉄を一様な厚板(進捗0)・無傷に初期化し、目標形状も作る(全て ForgingSim が担当)。
	// タイトルは金床の上で熱い鉄を打ち続ける画なので、その状態で用意する(Title.cpp)。
	ResetTitleStage();


	// --- 石墙専用シェーダー(triplanarで Poly Haven の実PBR貼图を投影) ---
	VertexShader* wallVS = CreateObj<VertexShader>("VS_Wall");
	if (FAILED(wallVS->Load("Assets/Shader/VS_Wall.cso")))
		MessageBox(nullptr, "VS_Wall.cso", "Shader Error", MB_OK);
	PixelShader* wallPS = CreateObj<PixelShader>("PS_Wall");
	if (FAILED(wallPS->Load("Assets/Shader/PS_Wall.cso")))
		MessageBox(nullptr, "PS_Wall.cso", "Shader Error", MB_OK);
	// 草: VS_Wall と同じ出力 + 玩家を避けて倒れる(InteractionMap を読む)。PS は PS_Wall をそのまま使う。
	VertexShader* grassVS = CreateObj<VertexShader>("VS_Grass");
	if (FAILED(grassVS->Load("Assets/Shader/VS_Grass.cso")))
		MessageBox(nullptr, "VS_Grass.cso", "Shader Error", MB_OK);

	// --- 3D鉄条メッシュ用シェーダーと動的メッシュ ---
	VertexShader* bvs = CreateObj<VertexShader>("VS_Bar");
	bvs->Compile(g_barVS);
	PixelShader* bps = CreateObj<PixelShader>("PS_Bar");
	bps->Compile(g_barPS);

	// --- 武器モーフ用シェーダー(PBR の鋼 + 熱の発光) + FBX各段の読込 ---
	VertexShader* wpvs = CreateObj<VertexShader>("VS_Wp");
	if (FAILED(wpvs->Load("Assets/Shader/VS_Wp.cso"))) MessageBox(nullptr, "VS_Wp.cso", "Shader Error", MB_OK);
	PixelShader*  wpps = CreateObj<PixelShader>("PS_Wp");
	if (FAILED(wpps->Load("Assets/Shader/PS_Wp.cso"))) MessageBox(nullptr, "PS_Wp.cso", "Shader Error", MB_OK);
	PixelShader*  gps  = CreateObj<PixelShader>("PS_Ghost"); gps->Compile(g_ghostPS);
	LoadWeaponStages();

	// 武器の鋼テクスチャ(BaseColor=冷鋼の地色)。発光は温度 m_forging.Heat() で駆動するので Emissive は直貼りしない。
	{
		m_wpTex = TextureCache::Get("Assets/MM_Blacksmith_Pack/Medieval_Sword_Blade/Blade_BaseColor.png");
		// 氧化皮マスクは「色」でなく「厚みのデータ」なので sRGB 変換をしない(false)=灰度がそのまま閾値と比べられる
		m_wpScaleMask = TextureCache::Get("Assets/Model/weapon/forge_scale_mask.png", false);
		// 法線マップも「データ」= sRGB 変換しない。OpenGL 式(緑+ = UV の V+)= PerturbNormal の規約そのまま
		m_wpNormalTex = TextureCache::Get("Assets/MM_Blacksmith_Pack/Medieval_Sword_Blade/Blade_Normal_OpenGL.png", false);
	}

	// 光る炭ベッド用シェーダー(pos/uv/col レイアウト + テクスチャ)。
	// 実行時Compileではなく、正規に .hlsl → fxc → .cso をLoadする(VS_Object等と同じ流儀)。
	VertexShader* cvs = CreateObj<VertexShader>("VS_Coal");
	if (FAILED(cvs->Load("Assets/Shader/VS_Coal.cso")))
		MessageBox(nullptr, "VS_Coal.cso", "Shader Error", MB_OK);
	PixelShader* cps = CreateObj<PixelShader>("PS_Coal");
	if (FAILED(cps->Load("Assets/Shader/PS_Coal.cso")))
		MessageBox(nullptr, "PS_Coal.cso", "Shader Error", MB_OK);

	// 水面: 頂点は波の高さで格子を上下させる VS_Water、画素は屈折+深度+波の法線の PS_Water。
	VertexShader* wvs = CreateObj<VertexShader>("VS_Water");
	if (FAILED(wvs->Load("Assets/Shader/VS_Water.cso")))
		MessageBox(nullptr, "VS_Water.cso", "Shader Error", MB_OK);
	PixelShader* wps = CreateObj<PixelShader>("PS_Water");
	if (FAILED(wps->Load("Assets/Shader/PS_Water.cso")))
		MessageBox(nullptr, "PS_Water.cso", "Shader Error", MB_OK);

	// ブロックメッシュ: 各セルを箱(上面+側面4=5面, 各面両面12頂点=60頂点)で描く分を確保
	m_barVtx.resize(ForgingSim::NL * ForgingSim::NW * 60 + 64);
	MeshBuffer::Description bdesc = {};
	bdesc.pVtx     = m_barVtx.data();
	bdesc.vtxSize  = sizeof(Vertex);
	bdesc.vtxCount = (UINT)m_barVtx.size();
	bdesc.isWrite  = true;
	bdesc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	m_barMesh = std::make_shared<MeshBuffer>(bdesc);

	// 金床はもう特別扱いしない。他の道具と同じ "StAnvil" プロップとして下でロードする。
	// 光る鉄条の落点(UpdateBarAnchor)は StAnvil のワールド変換＋AABBから毎フレーム算出するので、
	// 配置ファイルで金床を動かしても鉄条が自動追従する(第8節の最重要ポイント)。

	// 3Dハンマー(MdlHammerはSceneForge専用だが、再入場時の再インポートを避けキャッシュ)
	Model* hammer = GetObj<Model>("MdlHammer");
	if (!hammer)
	{
		hammer = CreateObj<Model>("MdlHammer");
		hammer->Load("Assets/MM_Blacksmith_Pack/Tools/SM_BS_Hammer_1.fbx", 1.0f, false, true);
		auto tex = std::make_shared<Texture>();
		if (SUCCEEDED(tex->Create("Assets/MM_Blacksmith_Pack/Tools/Textures/1024x512/T_BS_Tools_BaseColor.png")))
			hammer->SetTexture(tex);
	}

	// UI: 羊皮紙パネル(結果/失敗画面の下地)。透明PNGを読み、ImGuiのAddImageで貼る。
	m_uiParchment = TextureCache::Get("Assets/Ui/parchment.png");
	// UI: 飾り枠付きの羊皮紙(工程リストの下地)。ナインスライスで縦長に伸ばしても四隅の飾りは歪まない。
	//   parchment_frame_ui.png = 元画像を Tools/ui_prescale.py で高さ 680 へ縮小した物(四隅の飾りが画面の約2倍=ぼけない)
	m_uiFrame = TextureCache::Get("Assets/UI/parchment_frame_ui.png");
	m_uiStrip = TextureCache::Get("Assets/UI/paper_strip.png");	// 無ければ nullptr(DrawTextOnStrip が暗い帯で代わりに描く)
	// UI: 温度ゲージ(ChatGPT 製の絵。範囲の位置は程序で決める=DrawHeatGauge)
	m_gaugeFrame   = TextureCache::Get("Assets/UI/Heat_Gauge/heat_gauge_frame.png");
	m_gaugeOverlay = TextureCache::Get("Assets/UI/Heat_Gauge/heat_gauge_frame_overlay.png");	// 槽をくり抜いた外框(ツールで生成)
	m_gaugeMarker  = TextureCache::Get("Assets/UI/Heat_Gauge/heat_gauge_marker_ui.png");		// 表示サイズへ縮小済みの指針
	m_gaugeIdeal   = TextureCache::Get("Assets/UI/Heat_Gauge/heat_zone_ideal.png");
	m_gaugeOver    = TextureCache::Get("Assets/UI/Heat_Gauge/heat_zone_over.png");

	// --- シーン装飾: 編集シーン(StageEditor)と同じ道具一式・同じキー(St...)で読み込む ---
	//   キーを St... に統一したので Assets/stage_layout.txt を両シーンで共有できる。
	//   ここは既定値。この後 LoadLayout() が保存済み配置で上書きする。
	//   LoadProp 引数: (key, fbx, tex, targetSize, X, Y, Z, Yaw, groundSnap)
	//     targetSize = AABBの最大辺がこの大きさになる自動スケール(魔法数字を避ける)
	const std::string P = "Assets/MM_Blacksmith_Pack/";
	const std::string kAnvilTex = P + "Anvil/Textures/T_Anvil_BaseColor.png";
	const std::string kWood     = P + "Ballows/Textures/T_Wood_BaseColor.png";
	const std::string kTable    = P + "Worktable/Textures/T_BS_Worktable_V1_BaseColor.png";
	const std::string kBucket   = P + "Buckets/Textures/T_Buckets_V1_BaseColor.png";
	const std::string kSharp    = P + "Sharpner/Textures/T_Sharpner_V1_BaseColor.png";
	const std::string kTools    = P + "Tools/Textures/1024x512/T_BS_Tools_BaseColor.png";
	const std::string kMetal    = P + "Metal Parts/Textures/T_Metal_parts_BaseColor.png";
	// 炉は石壁の実写テクスチャを triplanar(炉自身の物体空間)で投影する(UV/材質の継ぎ目で石の大きさが揃わない対策。PS_StageProp)
	const char* kForgeStone = "Assets/PolyHaven_RockWall17/rock_wall_17_Diffuse_2k.png";

	LoadProp("StGround",   "Assets/Model/plane/plane.fbx", "Assets/Model/field/wooden-plank-textured-background-material.jpg", 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
	LoadProp("StStump",    (P+"Anvil/SM_Stump.fbx").c_str(),             kAnvilTex.c_str(), 0.60f, 0.0f, 0.0f,  0.0f, 0.0f, true);
	LoadProp("StAnvil",    (P+"Anvil/SM_Anvil.fbx").c_str(),             kAnvilTex.c_str(), 0.70f, 0.0f, 0.0f,  0.0f, 0.0f, true);
	LoadProp("StForge",    (P+"Forges/SM_BS_Forge_2_.fbx").c_str(),      kForgeStone,       2.60f, 1.8f, 0.0f,  0.6f, 0.0f, true);	// Forge_1は可視メッシュが重複(未merge)で破図→Forge_2に差替
	LoadProp("StStand",    (P+"Ballows/SM_Bellows_stand_1.fbx").c_str(), kWood.c_str(),     1.20f, 3.2f, 0.0f,  1.0f, 0.0f, true);
	LoadProp("StBellows",  (P+"Ballows/SM_Bellows.fbx").c_str(),         kWood.c_str(),     1.40f, 3.2f, 0.0f,  1.0f, 0.0f, true);
	LoadProp("StWorktable",(P+"Worktable/SM_BS_Worktable.fbx").c_str(),  kTable.c_str(),    2.20f,-2.6f, 0.0f,  0.6f, 0.0f, true);
	LoadProp("StTrough",   (P+"Buckets/SM_Trough.fbx").c_str(),          kBucket.c_str(),   1.60f,-1.4f, 0.0f, -0.9f, 0.0f, true);
	LoadProp("StBucket",   (P+"Buckets/SM_B_Bucket_1.fbx").c_str(),      kBucket.c_str(),   0.70f, 0.9f, 0.0f, -0.9f, 0.0f, true);
	LoadProp("StGrind",    (P+"Sharpner/SM_Sharpner.fbx").c_str(),       kSharp.c_str(),    1.40f,-4.2f, 0.0f, -1.8f, 0.0f, true);
	LoadProp("StPoker",    (P+"Tools/SM_BS_Poker.fbx").c_str(),          kTools.c_str(),    1.20f, 1.8f, 0.0f,  0.3f, 0.0f, true);
	LoadProp("StPliers",   (P+"Tools/SM_BS_Pliers_1.fbx").c_str(),       kTools.c_str(),    0.60f,-2.4f, 0.0f,  0.6f, 0.0f, true);
	LoadProp("StMetal1",   (P+"Metal Parts/SM_Metal_part_1.fbx").c_str(),kMetal.c_str(),    0.40f,-2.8f, 0.0f,  0.6f, 0.0f, true);
	LoadProp("StMetal2",   (P+"Metal Parts/SM_Metal_part_2.fbx").c_str(),kMetal.c_str(),    0.40f,-3.1f, 0.0f,  0.7f, 0.0f, true);

	// 整屋(AI生成 Cottage_Clean.fbx)。モデルと材質貼りは SceneRoot::LoadSharedProps が一度だけ用意
	// (両シーン共有)。ここは m_props への登録だけ(LoadProp はキャッシュ命中で再ロードしない)。
	//   大きさ/位置は編集シーン(StageEditor)で調整し stage_layout.txt 経由で此処が読む(下の LoadLayout)。
	//   既定は大きめ(初回=配置ファイルに StCottage 行が無い時のみ使用)。石/灰泥は DrawWall で triplanar。
	LoadProp("StCottage", "Assets/Medieval_Blacksmith_Cottage_Production/Cottage_Clean.fbx",
	         "Assets/PolyHaven_RockWall17/rock_wall_17_Diffuse_2k.png", 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);

	// 既定の積み重ね(金床=樹桩の上、風箱=支架の上、道具=作業台の上)。この後 LoadLayout で上書きされる。
	{
		auto worldH = [](Prop* p)->float { return (p->aabbMax.y - p->aabbMin.y) * p->scale; };
		Prop* stump = GetProp("StStump"); Prop* anvil = GetProp("StAnvil");
		if (stump && anvil) { anvil->pos[0] = stump->pos[0]; anvil->pos[2] = stump->pos[2]; anvil->pos[1] = worldH(stump); }
		Prop* stand = GetProp("StStand"); Prop* bel = GetProp("StBellows");
		if (stand && bel) { bel->pos[0] = stand->pos[0]; bel->pos[2] = stand->pos[2]; bel->pos[1] = worldH(stand) * 0.55f; }
		Prop* table = GetProp("StWorktable");
		float tableH = table ? worldH(table) : 0.0f;
		if (Prop* pl = GetProp("StPliers")) if (table) { pl->pos[0] = table->pos[0] + 0.3f; pl->pos[2] = table->pos[2]; pl->pos[1] = tableH; }
		if (Prop* m1 = GetProp("StMetal1")) if (table) { m1->pos[0] = table->pos[0] - 0.3f; m1->pos[2] = table->pos[2] + 0.1f; m1->pos[1] = tableH; }
		if (Prop* m2 = GetProp("StMetal2")) if (table) { m2->pos[0] = table->pos[0] + 0.0f; m2->pos[2] = table->pos[2] - 0.2f; m2->pos[1] = tableH; }
	}

	// --- 炭ベッド(低ポリ炭塊) + 水面の格子(±1。両面。実サイズはDrawWaterのworldで拡縮) ---
	//   水面は波の高さ場(WaterSim)と同じ細かさの格子にする=頂点ごとに高さを読んで本当に上下させる(VS_Water)。
	//   uv は板の端から端へ 0..1(u = ローカル X = 槽の長さ、v = ローカル Z = 幅)。
	{
		const int NX = WaterSim::NX, NZ = WaterSim::NZ;
		auto vert = [&](int i, int k) -> Vertex
		{
			const float u = (float)i / (NX - 1), v = (float)k / (NZ - 1);
			return Vertex{ { u * 2.0f - 1.0f, 0.0f, v * 2.0f - 1.0f }, { u, v }, { 1, 1, 1, 1 } };
		};
		std::vector<Vertex> q;
		const int TRIS_PER_CELL_BOTH_SIDES = 12;	// 2三角形 × 3頂点 × 表裏
		q.reserve((NX - 1) * (NZ - 1) * TRIS_PER_CELL_BOTH_SIDES);
		for (int k = 0; k < NZ - 1; ++k)
		for (int i = 0; i < NX - 1; ++i)
		{
			const Vertex a = vert(i, k), b = vert(i + 1, k), c = vert(i, k + 1), d = vert(i + 1, k + 1);
			q.insert(q.end(), { a, c, b,  b, c, d });	// 表(上向き)
			q.insert(q.end(), { a, b, c,  b, d, c });	// 裏(カリング対策で逆巻き)
		}
		MeshBuffer::Description cd = {};
		cd.pVtx = q.data(); cd.vtxSize = sizeof(Vertex); cd.vtxCount = (UINT)q.size();
		cd.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		m_waterMesh = std::make_shared<MeshBuffer>(cd);
		m_coalBedMesh = CoalBedMesh::Create();

		m_waterHeightTex = std::make_unique<Texture>();
		m_waterHeightTex->Create(DXGI_FORMAT_R32_FLOAT, NX, NZ, m_waterSim.Heights());
	}

	// 編集シーンで作った配置(Assets/stage_layout.txt)を反映。無ければ上の既定のまま。
	// StCottage も普通のプロップとして round-trip する(編集シーンで大きさ/位置を決めれば此処が読む)。
	for (const auto& e : OutdoorStage::Read()) {
		LoadProp(e.key.c_str(),e.path.c_str(),"",1.0f,e.x,e.y,e.z,e.yaw,false);
		for(auto& p:m_props) if(p.key==e.key) {p.scale=e.scale;p.pos[1]=e.y;p.groundSnap=false;}
	}
	LoadLayout();
	m_waterSim.Init(m_waterSize[0] * 2.0f, m_waterSize[1] * 2.0f);	// 水面の大きさ(配置ファイルの W 行。±1 の板なので全長は2倍)
	InitBuildingCollision();	// 家の壁線用の三角形と、裏口の扉の蝶番/凸包(Collision.cpp)
	InitGrassMap();				// 草の踏み跡の貼图(範囲=屋外の地面。配置が決まった後に)
	InitTongsGeometry();		// 火钳モデルの形(口の端・挟む点・輪の向き)を読む(Carry.cpp)
	CottageRender::Load();
	LoadTuning();	// F1で調整したハンマー/カメラ値(forge_tuning.txt)を復元
	ResetCourseProgress();	// チュートリアルの ✓ は起動の度に空(展示で次の人が来る=保存しない)
	LoadOptions();			// 玩家の設定(教学アニメの速さ)
	BuildPropHulls();	// 衝突の凸包: 配置(LoadLayout)と床の高さ(LoadTuning)が決まった後=背丈以下の頂点を選べる
	// Layout/tuning must be loaded before assigning the walking spawn height.
	m_player.Init(DirectX::XMFLOAT3(0.0f, m_walkFloorY, -2.0f), 0.0f);
	SnapshotTuning();	// ↑復元直後の値を「起動時の姿」として記録(F8/ボタンでここへ戻せる)

	SetupSteps();	// 工程(step)状態を生成し状態機へ登録(遷移は StartGame で開始)

	Strike();	// 開始直後から火花を出す
	// ロード完了後にここで音を開始(起動途中でBGMが鳴らないように Main から移動)
	// 起動はタイトル状態 → タイトルBGM(工場環境音)をループ。
	Audio::PlayLoop(Audio::BGM_TITLE, TITLE_BGM_VOLUME);
}

void SceneForge::Uninit()
{
	SaveTuning();	// F1で調整したハンマー/カメラ値を書き出す(次回起動で復元)
	ResetRunState();	// 遊びの途中で出ても(編集シーンへ切替・ゲーム終了)1回分の状態を残さない。鳴り続ける音もここで止まる
	m_grassMap.Uninit();	// 草の踏み跡の貼图(ブレンドステートを解放)
	ReflectionProbe::Uninit();	// 反射プローブの立方体画像(次に入った時に撮り直す)
	VolumetricLight::Uninit();	// 光の筋の作業用画像とシェーダー
	OutlineRender::Uninit();	// 次の工位の輪郭線のシェーダー
	DestroyObj("VS_Forge");
	DestroyObj("PS_Forge");
	DestroyObj("PS_Spark");
	DestroyObj("PS_Glint");
	DestroyObj("PS_Trail");
	DestroyObj("VS_Bar");
	DestroyObj("PS_Bar");
	DestroyObj("VS_Coal");
	DestroyObj("PS_Coal");
	DestroyObj("PS_Water");
	DestroyObj("VS_Water");
	m_waterMesh.reset();
	m_waterHeightTex.reset();
	m_coalBedMesh.reset();
	// プロップのモデル(St...)とハンマーは破棄しない = static map に常駐させ、編集シーンと
	// 共有する。両シーンはキー(St...)を統一済みなので、片方が読んだモデルをもう片方が
	// そのまま再利用でき、シーン切替の再インポート(数秒)が消える。摩擦: 常駐メモリ(許容)。
	m_props.clear();	// m_props は死ぬインスタンス側の配置データだけ。モデル実体は map に残す
	m_barMesh.reset();
	m_mesh.reset();
	m_glow.reset();
	m_particles.Clear();
	// シーンを離れる時は「このシーンが鳴らしている全ループ音」を止める。
	// どの状態(TITLE/PLAY/RESULT)で抜けても対応する BGM が残るのを防ぐ。
	// 残ると: 編集シーンへ切替→BGMが鳴りっぱなし、戻ると新旧BGMが二重再生になる。
	Audio::Stop(Audio::BGM_TITLE);	// タイトルBGM
	Audio::Stop(Audio::BGM_PLAY);	// ゲーム中BGM(PLAYで抜けた時)
	Audio::Stop(Audio::BGM_RESULT);	// 結果BGM(RESULTで抜けた時)
	if (m_heatSndOn) { Audio::Stop(Audio::SE_FORGE_LOOP); m_heatSndOn = false; }	// 加熱ループ音(Rを押しながら抜けた時)
	if (m_burnSndOn)  { Audio::Stop(Audio::SE_BURN_LOOP);  m_burnSndOn  = false; }	// 燃焼ループ音
	if (m_grindSndOn) { Audio::Stop(Audio::SE_GRIND_LOOP); m_grindSndOn = false; }	// 研磨ループ音
	if (!m_cursorShown) { ShowCursor(TRUE); m_cursorShown = true; }	// カーソルを戻す
}

void SceneForge::Strike(float scale)
{
	// 1回叩くと火花をまとめて発生(バースト)。量と勢いの物理は Particles が持つ。
	const int N = (int)(m_burst * scale);
	// 出る所 = 叩いた所: 描かれているハンマーの頭の打つ面(HammerStrikePoint)。照準点やオフセットからの推測でなくモデルから。
	//   (旧: (0,1,0) のベタ書き → 照準点。照準点は m_hammerOff と追従の遅れの分だけ頭とずれ、タイトルで合わなかった)。
	XMFLOAT3 origin = HammerStrikePoint();
	m_lastStrikeOrigin = origin;	// F1 のデバッグ表示(洋紅の十字)
	//   タイトルの自動打ちも同じ Strike() を通る=ゲームとタイトルで火花の出る所の規則が同じ。
	m_particles.SpawnSparks(origin, N, m_power, scale);
}

// 目標形状(BuildTarget)と一致度(ShapeMatch)は Physics/ForgingSim へ移動した。

//====================================================================
//  ゲーム進行
//====================================================================
//--- 1回の鍛造(遊び1回分)の状態を、全部まとめて最初に戻す。2026-10-19 ユーザー要望で一本化:
//    旧: StartGame と ResetTitleStage がそれぞれ別の一部だけを戻していた → 片方に足し忘れた物(工位の光など)が
//        タイトルへ持ち越された。ここに書けば「開始・タイトルへ戻る・結果から戻る・シーンに入る/出る・終了」の全部で戻る。
//    ★1回の遊びの間に変わる変数を新しく足したら、必ずここで戻すこと。
//    戻さない物: 調整値(F1/forge_tuning.txt)・済んだ課程の記録・モード/課程の選択・導入の段階(呼ぶ側が決める)。
void SceneForge::ResetRunState()
{
	// 鳴り続ける音を止める(状態だけ false にすると、鳴っている音が止まらず残る)
	if (m_heatSndOn)  { Audio::Stop(Audio::SE_FORGE_LOOP); m_heatSndOn  = false; }
	if (m_burnSndOn)  { Audio::Stop(Audio::SE_BURN_LOOP);  m_burnSndOn  = false; }
	if (m_grindSndOn) { Audio::Stop(Audio::SE_GRIND_LOOP); m_grindSndOn = false; }
	m_modeTrans = ModeTrans::None;	// 走動⇔工位の移動アニメも解除
	m_focus = -1; m_promptAlpha = 0.0f;	// 互動の状態も初期化
	m_grabFocus = -1; m_grabPromptAlpha = 0.0f; m_gripAfterExit = false;
	m_bannerAt = -1000.0f; m_bannerText.clear(); m_hiAlpha = 0.0f;	// 工程の札・工位の光も初期化
	m_walkPitch = 0.0f;
	m_player.Init(DirectX::XMFLOAT3(0.0f, m_walkFloorY, -2.0f), 0.0f);	// 開始位置/向きを戻す
	m_door.Reset();			// 裏口の扉は閉じた状態から
	ResetTestCut();			// 試し切りの藁人形を切る前の姿へ(作り直しは次の更新で)
	m_score    = 0;
	m_forging.Reset();		// 鉄を厚板・無傷・進捗0へ(表面が上に戻る。目標形状も再生成)
	m_forging.SetHeat(START_HEAT);	// タイトルで打っていた熱い鉄のまま始まる(最初の加熱工程は即完了)
	m_forgeProg = 0.0f;		// 武器モーフのプレビュー進捗も戻す
	m_match = 0.0f;
	m_flipAngle = 0.0f;		// 翻面回転も表(0)へ戻す
	m_flipPhase = FlipPhase::None;	// 翻面子状態機も初期化(鍛打中に戻す)
	m_camTongsW = 0.0f; m_camGripW = 0.0f;	// 運鏡の寄りも解除
	m_hammerStowW = 0.0f;					// ハンマーは構え位置へ
	m_tongsInHand = false;					// 火钳は左腰に掛かっている

	// 工位と刃の置き場所: 鉄坯は金床の上から始まる(熱い状態=そのまま鍛打へ)。
	m_station = Station::Anvil; m_workAt = Station::Anvil; m_carrying = false; m_restFlip = false;
	m_overheatWarned = false;
	m_burnSparkAcc = 0.0f;
	m_wheel.Reset();						// 砥石は止まっている
	m_grindPress = 0.0f;
	m_grindU = 0.5f; m_grindFace = 0; m_grindFaceInit = false; m_grindFlipRoll = 0.0f; m_grindFlipping = false; m_grindFlipHold = 0.0f; m_grindSparkAcc = 0.0f; m_grindVol = 0.0f; m_grindAngle = m_grindAngleTarget = 0.0f;
	m_plunge = 0.0f; m_quenchTurn = 0.0f; m_agitate = 0.0f; m_quenchContact = false; m_clearDecided = false; m_letterbox = 0.0f; m_boil = 0.0f; m_boilStage = BoilStage::None; m_waterSim.Reset(); m_prevAgitate = 0.0f; m_bubbleAcc = 0.0f;
	m_stirTarget = 0.0f; m_filmBreak = 0.0f; m_filmTime = 0.0f; m_stir01 = 0.0f; m_quenchStartHeat = 0.0f;	// 淬火/終幕の演出も解除

	m_charging    = false;
	m_charge      = 0.0f;
	m_strikeCD    = 0.0f;
	m_hammer.Reset();			// 鎚を静止高へ・速度ゼロに戻す
	m_aimI = ForgingSim::NL / 2; m_aimJ = ForgingSim::NW / 2; m_aimSeg = 0; m_aimValid = false;
	m_aimWorld = m_barAnchor;	// 最初の有効照準までのハンマー既定位置(板中心)
	m_impactTime = -1000.0f;	// 前の回の打撃の光を消す
	m_lookYaw = 0.0f; m_lookPitch = 0.0f;
	m_canStrike   = false;		// SPACEを一度離すまで蓄力しない
	m_shake        = 0.0f;
	m_popupLife    = 0.0f;
	m_sinceStrike  = 999.0f;	// 最初の一打はリズム対象外
	m_rhythmStreak = 0;
	m_sizzleTimer  = 0.0f;
	m_qualitySum   = 0.0f;
	m_strikeCount  = 0;

	m_stepIdx = 0;
	m_stepChangedAt = m_time;	// 指引 UI: 最初の案内文も淡入させる
	m_guideText = nullptr; m_guideGoal = Station::Anvil;	// 案内文と行き先(UpdateGuide が毎フレーム決め直す)
	m_trackerRows.clear();		// 工程リストの済状態を作り直す(UpdateTracker が配方の工程数で用意する)
	m_stepTimes.assign(m_recipe ? m_recipe->steps.size() : 0, StepTime{});	// プレイテスト計時も 0 から
	for (LessonState& l : m_lessons) l = LessonState{};		// 操作の案内は毎回最初から(新しい人が遊ぶ)
	m_lessonShown = -1; m_lessonShownRescue = false;
	m_aimTravel = 0.0f; m_lastAimRail = -1.0f; m_validStrikes = 0; m_bellowsHeldSec = 0.0f; m_flipCount = 0;	// 案内の「できた」量
	m_leaveCount = 0; m_gripCount = 0; m_putCount = 0; m_reachCount = 0; m_heatReadyCount = 0;
	m_prevFocusGoal = false; m_prevHeatReady = false;
	m_grindRunTime = 0.0f; m_grindGoodStreak = 0.0f; m_grindAngleHolds = 0; m_grindSlideTravel = 0.0f; m_grindFlipCount = 0;
	m_grindTooFlat = false; m_plungeCount = 0; m_stirTravel = 0.0f;
	m_walkGoalTime = 0.0f; m_walkGoalLabel = nullptr;
	StopSequence();				// 前の回の拍子表が途中なら打ち切る
	m_seqBreathW = 0.0f;		// 拍子表の呼吸の重み
	m_bobPhase = 0.0f; m_bobWeight = 0.0f;	// 運ぶ時の揺れ
	m_carryPull = 0.0f; m_carryRaise = 0.0f;	// 手の鉄が道具/壁を避ける量
	m_grindFlipStart = 0.0f;
	for (float& t : m_temperCell) t = 0.0f;	// 焼き戻しの色
	m_temperU = 0.5f; m_temperStarted = false; m_temperPoseW = 0.0f; m_exitFromTemper = false; m_tongsHeat = 0.0f;
	m_bellowsAir = 0.0f; m_bellowsSquash = 0.0f; m_bellowsPumpAt = -1000.0f;	// 炉に溜まった風・ふいごの動き
	ResetGrindSegments();		// 研ぎの区域の「仕上がった」記録と光
}

void SceneForge::StartGame()
{
	// BGM はここでは切り替えない: カメラが金床へ移る間にタイトル BGM を淡出し、着いてから切り替える(FinishIntro)。
	m_state = GAME_PLAY;
	ResetRunState();			// 1回分の状態を全部最初へ(共通の1か所)
	// 状態は一旦「走動」にし、UpdateTitle が直後に金床への導入運鏡を始める(着くと工位=鍛打へ)。
	m_walkMode = true;
	// 工程(step)状態機を最初の工程から開始する。
	//   遷移先の名前は「配方(m_recipe)の順序」から取る=データ駆動(chase は名前を状態に直書きだった)。
	m_stepMachine.ChangeState(StepKey(m_recipe->steps[0].type));
}

//--- 工程(step)状態を生成し、状態機へ登録する(Init で一度だけ)。
//    各状態は owner=this を持ち、完了時に AdvanceStep() を呼ぶ。登録名(GetStateName)が状態機のキー。
void SceneForge::SetupSteps()
{
	m_heatStep   = std::make_unique<HeatStep>(this);
	m_forgeStep  = std::make_unique<ForgeStep>(this);
	m_grindStep  = std::make_unique<GrindStep>(this);
	m_quenchStep = std::make_unique<QuenchStep>(this);
	m_temperStep = std::make_unique<TemperStep>(this);
	m_testCutStep = std::make_unique<TestCutStep>(this);
	m_heatStep->RegisterState(m_stepMachine);
	m_forgeStep->RegisterState(m_stepMachine);
	m_grindStep->RegisterState(m_stepMachine);
	m_quenchStep->RegisterState(m_stepMachine);
	m_temperStep->RegisterState(m_stepMachine);
	m_testCutStep->RegisterState(m_stepMachine);
}

//--- 配方でその種類の工程まで進んだか(その工程が今か、もう済んだ)。工位に入れるかの判定に使う(データ駆動=順序は配方が決める)。
bool SceneForge::StepReached(StepName type) const
{
	if (!m_recipe) return true;
	for (int k = 0; k <= m_stepIdx && k < (int)m_recipe->steps.size(); ++k)
		if (m_recipe->steps[k].type == type) return true;
	return false;
}

//--- 次の工程へ進む。配方(データ)が順序の正。最後の工程を越えたら完成(淬火済み)へ。
void SceneForge::AdvanceStep()
{
	if (m_courseComplete) return;	// 課程を終えた後は進めない(最後の工程の状態は完了条件を満たしたまま毎フレーム呼んでくる)
	const int prevIdx = m_stepIdx;
	++m_stepIdx;
	m_stepChangedAt = m_time;	// 指引 UI: 新しい案内文を淡入+工程リストで強調
	// チュートリアルの課程: その課程の最後の工程が済んだら、次の工程へ進まず「レッスン完了」
	if (m_recipe && m_courseIdx >= 0 && m_courseIdx < (int)m_recipe->courses.size() && m_stepIdx > m_recipe->courses[m_courseIdx].lastStep)
	{
		CompleteCourse();
		return;
	}
	if (!m_recipe || m_stepIdx >= (int)m_recipe->steps.size()) { WritePlaytestLog(); FinishGame(); return; }
	m_stepMachine.ChangeState(StepKey(m_recipe->steps[m_stepIdx].type));
	ShowStepBanner(prevIdx);	// 「〜 完了 → 次：〜(工位へ)」の札+合図の音(HUD.cpp)
}

//--- プレイテスト計時: 今の工程に dt を足す。歩いている/工位へ出入りしている間は move にも足す
//    (= 工程の時間のうち、何割が「操作」でなく「移動」か が分かる)。
void SceneForge::TickPlaytestTimer(float dt)
{
	if (m_stepIdx < 0 || m_stepIdx >= (int)m_stepTimes.size()) return;	// 完成後(工程の外)は数えない
	StepTime& t = m_stepTimes[m_stepIdx];
	t.total += dt;
	if (m_walkMode || Transitioning()) t.move += dt;
}

//--- 完成した1プレイ分を1行追記する。形式: 日時 / モード / 合計 / 工程ごとの「時間(うち移動)」。
//    追記(上書きしない)=何人分・何回分も溜まり、調整の前後を見比べられる。
void SceneForge::WritePlaytestLog() const
{
	if (!m_recipe) return;
	FILE* fp = nullptr;
	if (fopen_s(&fp, "Assets/playtest_log.txt", "a") != 0 || !fp) return;
	SYSTEMTIME now; GetLocalTime(&now);
	float total = 0.0f;
	for (const StepTime& t : m_stepTimes) total += t.total;
	fprintf(fp, "%04d-%02d-%02d %02d:%02d  %s  total %.1fs (budget %.0fs)",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
		m_tutorial ? "tutorial" : "normal", total, PLAYTEST_BUDGET_SEC);
	for (size_t i = 0; i < m_stepTimes.size() && i < m_recipe->steps.size(); ++i)
		fprintf(fp, "  | %s %.1fs (move %.1fs)", m_recipe->steps[i].label, m_stepTimes[i].total, m_stepTimes[i].move);	// label は UTF-8
	fprintf(fp, "\n");
	fclose(fp);
}

//--- 配方の idx 番目の工程から始める。F1 の「Jump to step」(試す為)と、チュートリアルの課程(その工程だけを遊ぶ)が使う。
//    新しい一局として始め直し(StartGame)、配方で前にある工程を「済」にしてから、その工程・その工位から始める。
//    温度はその工程を試せる値にする: 加熱 = 冷えた鉄 / 鍛造・研磨 = 開始時の熱 / 淬火 = 淬火できる窓の真ん中。
//    placeNow = true : その工位に即座に立つ(F1。過渡アニメ無し)
//               false: 鉄だけその工位に置き、呼び出し側が BeginEnterStation で導入カメラを動かす(タイトルから入る)
void SceneForge::StartAtStep(int idx, bool placeNow)
{
	if (!m_recipe || idx < 0 || idx >= (int)m_recipe->steps.size()) return;
	StartGame();

	for (int k = 0; k < idx; ++k)
	{
		switch (m_recipe->steps[k].type)
		{
		case StepName::Forge: m_forging.CompleteForging();  break;
		case StepName::Grind: m_forging.CompleteGrinding(); break;
		case StepName::Temper:	// 焼き戻し済み = 刃先が良い色(試し切りの課程: 色の悪さで「欠けた/まくれた」と言わない様に)
			for (float& c : m_temperCell) c = (TEMPER_IDEAL_MIN + TEMPER_IDEAL_MAX) * 0.5f;
			break;
		default: break;	// 加熱/淬火は形に残らない
		}
	}

	const StepName type = m_recipe->steps[idx].type;
	switch (type)
	{
	case StepName::Heat:   m_forging.SetHeat(COLD_LIMIT); break;	// 冷えた鉄(すぐ「済」にならない)
	case StepName::Quench: m_forging.SetHeat((QUENCH_MIN_TEMP + QUENCH_MAX_TEMP) * 0.5f); break;
	case StepName::Temper: m_forging.SetHeat(0.0f); break;	// 焼入れ後=冷えた刃(焼き戻しは温度を上げない)
	case StepName::TestCut: m_forging.SetHeat(0.0f); break;	// 仕上がった冷たい剣
	default:               m_forging.SetHeat(START_HEAT); break;
	}

	m_stepIdx = idx;
	m_stepChangedAt = m_time;
	// 工程リストの「済」を今の状態で初めから揃えておく(線は引き終わった状態・音なし)。
	//   空にしておくと、次の UpdateTracker が前の工程を「今まさに済になった」と見て、取り消し線の音(SE_PENCIL)を一斉に鳴らした
	//   (2026-10-12 不具合: チュートリアルの研ぎ/焼入れの課程に入った瞬間に完成の音)。changedAt は既定(ずっと前)=アニメも無し。
	m_trackerRows.assign(m_recipe->steps.size(), TrackerRow{});
	for (int i = 0; i < (int)m_trackerRows.size(); ++i) m_trackerRows[i].completed = RowCompleted(i, false);
	m_stepMachine.ChangeState(StepKey(type));

	// その工程の工位に鉄を置く。placeNow なら玩家もそこで作業中にする(歩いて運ぶ手間を省く。過渡アニメ無し)
	const Station s = StepStation(type);
	m_workAt = s;
	m_canStrike = false;
	if (!placeNow) return;	// 導入カメラ(BeginEnterStation)が工位へ連れて行く
	m_station = s;
	SetupStationView(s);
	m_walkMode = false;
	m_modeTrans = ModeTrans::None;
	m_lookYaw = 0.0f; m_lookPitch = 0.0f;
}

//====================================================================
//  チュートリアルの課程
//====================================================================
//--- 課程の最後の工程が済んだ: ✓ を付け、「レッスン完了」を見せる(COURSE_DONE_SHOW_SEC 後に一覧へ。Update)。
void SceneForge::CompleteCourse()
{
	const float DONE_VOLUME = 0.8f;
	m_courseComplete   = true;
	m_courseCompleteAt = m_time;
	m_clearDecided     = true;	// HUD を消して「レッスン完了」だけを見せる
	if (m_courseIdx >= 0 && m_courseIdx < (int)m_courseDone.size()) m_courseDone[m_courseIdx] = true;
	Audio::Play(Audio::SE_SUCCESS, DONE_VOLUME);
}

//--- 黒幕の裏でタイトルへ戻る(一時停止の「タイトルへ戻る」と課程の完了で共通)。途中の工程の状態も片付ける。
//    openCourses = 課程の一覧を開いた状態で戻る(次の課程を選びやすい様に、今の次の課程を選んでおく)。
void SceneForge::ReturnToTitle(bool openCourses)
{
	m_fade.Transition([this, openCourses] {
		const int nextCourse = m_courseIdx + 1;
		m_state = GAME_TITLE;
		ResetTitleStage();						// 1回分の状態を全部戻す(拍子表・翻面・鳴り続ける音も ResetRunState が片付ける)+金床に新しい熱い鉄
		m_courseComplete = false;
		m_courseIdx      = -1;
		bool openList = openCourses;
		if (openList && AllCoursesDone())	// 全課程を終えた=この人のチュートリアルは終わり → ✓ を消し、一覧でなく最初のタイトルで次の人を待つ
		{
			ResetCourseProgress();
			openList = false;
		}
		if (openList && m_recipe)
		{
			m_courseSelectOpen = true;
			m_courseSel        = (nextCourse < (int)m_recipe->courses.size()) ? nextCourse : (int)m_recipe->courses.size();	// 次の課程(無ければ「戻る」)
			m_courseRequest    = -1;
		}
		Audio::Stop(Audio::BGM_PLAY);
		Audio::PlayLoop(Audio::BGM_TITLE, TITLE_BGM_VOLUME);
	});
}

//--- ✓ を全部外す(起動時 / 全課程を終えた / 通常モードを遊び終えた / タイトル放置 / 手動)。
void SceneForge::ResetCourseProgress()
{
	m_courseDone.assign(m_recipe ? m_recipe->courses.size() : 0, false);
}

bool SceneForge::AllCoursesDone() const
{
	if (m_courseDone.empty()) return false;
	for (bool done : m_courseDone) if (!done) return false;
	return true;
}

//--- タイトルで何か触られたか。キー/マウスのボタン(GetKeyboardState は両方を持つ)・マウスの移動・手柄のボタンとスティック。
bool SceneForge::TitleInputThisFrame()
{
	bool touched = false;
	for (int key = 1; key < 256; ++key)
		if (IsKeyPress((BYTE)key)) { touched = true; break; }
	for (int a = 0; a < (int)Action::Count && !touched; ++a)
		if (InputActions::Held((Action)a)) touched = true;		// 手柄のボタン(キーは上で見た)
	float lx, ly, hx, hy;
	InputActions::LookDelta(lx, ly);
	InputActions::HandDelta(hx, hy);
	if (lx != 0.0f || ly != 0.0f || hx != 0.0f || hy != 0.0f) touched = true;	// 手柄のスティック
	float mx, my, cw, ch;
	GetMouseClient(mx, my, cw, ch);
	if (fabsf(mx - m_titleMouseX) > TITLE_MOUSE_MOVE_PX || fabsf(my - m_titleMouseY) > TITLE_MOUSE_MOVE_PX) touched = true;
	m_titleMouseX = mx; m_titleMouseY = my;
	return touched;
}

//--- タイトル: ✓ の自動リセット(放置)と手動リセット(Shift+Delete 長押し)。
//    放置 = 前の人が去った合図。メニューも閉じて、最初のタイトルで次の人を待つ。
void SceneForge::UpdateProgressReset(float tick)
{
	// 導入の運鏡/黒転じ中は数えない(=遊び始めた/終えた途中)
	if (m_introPhase != IntroPhase::None || m_fade.IsBusy()) { m_titleIdleSec = 0.0f; m_progressResetHold = 0.0f; return; }

	const bool holding = IsKeyPress(VK_SHIFT) && IsKeyPress(VK_DELETE);
	m_progressResetHold = holding ? m_progressResetHold + tick : 0.0f;
	if (m_progressResetHold >= PROGRESS_RESET_HOLD_SEC)
	{
		const float NOTE_VOLUME = 0.8f;
		ResetCourseProgress();
		m_progressResetHold   = -FLT_MAX;	// 離すまで2回目を出さない(押し続けても1回だけ)
		m_progressResetNoteAt = m_time;
		Audio::Play(Audio::SE_LEARNED, NOTE_VOLUME);
	}

	m_titleIdleSec = TitleInputThisFrame() ? 0.0f : m_titleIdleSec + tick;
	if (m_titleIdleSec < TITLE_IDLE_RESET_SEC) return;
	m_titleIdleSec     = 0.0f;
	ResetCourseProgress();
	m_modeSelectOpen   = false;
	m_courseSelectOpen = false;
}

//--- 今実行中の工程設定(HUD の指示文表示などが読む)。範囲外は端にクランプ。
const StepSetting& SceneForge::CurrentStep() const
{
	int i = m_stepIdx;
	if (i < 0) i = 0;
	int last = (int)m_recipe->steps.size() - 1;
	if (i > last) i = last;
	return m_recipe->steps[i];
}

//--- 両面とも成形完了したか(鍛打工程の完了条件)。武器FBXが無い時は自動完成しない(要素材)。
bool SceneForge::BothSidesDone() const
{
	if (!m_wpOk) return false;
	return m_forging.BothSidesDone();
}

//--- 翻面の子状態機(鍛打工程の内部)。F を押し続けると動作が進み、離すと巻き戻る(『鬼武者』の剛力と同じ流儀)。
//    命名 enum + switch の軽量な下層FSM。上層(工程FSM)は触らない=翻面は鍛打の内部交互。
//    ※呼び出し側(UpdatePlay)は、None 以外の間はハンマー入力を止める(火钳を扱っている最中)。
void SceneForge::UpdateFlip(float tick, bool inputOn)
{
	// 翻面は工程に関係なく、金床にいればいつでも F で始められる(玩家の自由)。
	// 入力凍結中(F1/遷移)は「一時停止」: 再生位置も計時も進めない。
	if (!inputOn) return;

	const bool holding = InputActions::Held(Action::Flip);
	switch (m_flipPhase)
	{
	case FlipPhase::None:									// 金床で F を押した瞬間から再生位置が進み始める
		if (m_station == Station::Anvil && InputActions::Pressed(Action::Flip))
		{
			m_flipPhase = FlipPhase::Holding;
			m_flipHold  = 0.0f;
			// 今の面の清潔な角(0 か π)から半回転する。確定後の Damp でほぼそこにあるので、揃えても見た目は変わらない。
			m_flipStartAngle = m_flipAngle = (float)m_forging.Side() * DirectX::XM_PI;
		}
		break;

	case FlipPhase::Holding:
	{
		// 押している=再生位置が進む / 離した=巻き戻る。これだけ(判定のタイミングが無い=途中のどこで離しても自然に戻る)。
		const float total = FlipHoldTotal();
		m_flipHold += holding ? tick : -tick * FLIP_REWIND_RATE;
		if (m_flipHold > total) m_flipHold = total;

		// 再生位置 → 画。全部 m_flipHold の関数なので、前へ進めても巻き戻しても同じ画を通る。
		//   火钳: 振り向いて静止している区間の中点=「手に取った」瞬間(巻き戻してそこを過ぎれば腰へ戻る)。
		const float grabAt = FLIP_TONGS_OUT_TIME * (FLIP_REACH_FRAC + FLIP_RETURN_FRAC) * 0.5f;
		m_tongsInHand = m_flipHold >= grabAt;
		//   回転: Turn 区間の進み 0..1 で半回転。面の判定は「最も近い半回転の番号」k(半分を越えると面が替わる)。
		const float turn01 = fminf(fmaxf((m_flipHold - FLIP_TONGS_OUT_TIME - FLIP_GRIP_TIME) / m_flipTurnSec, 0.0f), 1.0f);
		m_flipAngle = m_flipStartAngle + turn01 * DirectX::XM_PI;
		const int k = (int)floorf(m_flipAngle / DirectX::XM_PI + 0.5f);
		m_forging.SetSide(((k % 2) + 2) % 2);

		if (m_flipHold >= total)							// 押し切った = 裏返し完成 → 火钳を戻す(完成の動作)
		{
			// 角度を「今の面の清潔な角」(0 か π)へ巻き戻す。2π の倍数を引くだけなので見た目は不変(角度が増え続けない)。
			const int wrap = k - m_forging.Side();		// 2 の倍数
			m_flipAngle -= wrap * DirectX::XM_PI;
			++m_flipCount;								// 操作の案内: 裏返せた(LessonMetric: Flip)
			Haptics::Pulse(RUMBLE_FLIP, RUMBLE_FLIP_SEC);	// 刃が置かれた「コトン」
			m_flipPhase = FlipPhase::PutBack; m_flipTimer = 0.0f;
		}
		else if (m_flipHold <= 0.0f)						// 最初まで巻き戻った = 何もしなかった事に
		{
			m_flipHold = 0.0f;
			m_flipPhase = FlipPhase::None;
		}
		break;
	}

	case FlipPhase::PutBack:								// 火钳を戻す運鏡→鍛打へ復帰(時間で進む。巻き戻さない)
	{
		m_flipTimer += tick;
		// Tongs 区間と対称: 振り向いた静止区間の中点で火钳を腰へ戻す(モデルを再表示)。
		const float putAt = FLIP_PUTBACK_TIME * (FLIP_REACH_FRAC + FLIP_RETURN_FRAC) * 0.5f;
		if (m_flipTimer >= putAt) m_tongsInHand = false;
		if (m_flipTimer >= FLIP_PUTBACK_TIME) m_flipPhase = FlipPhase::None;
		break;
	}
	}
}

//--- 今の再生位置(m_flipHold)がどの区間か。案内文と運鏡が使う。
SceneForge::FlipBeat SceneForge::CurrentFlipBeat() const
{
	if (m_flipHold < FLIP_TONGS_OUT_TIME)                  return FlipBeat::Tongs;
	if (m_flipHold < FLIP_TONGS_OUT_TIME + FLIP_GRIP_TIME) return FlipBeat::Grip;
	return FlipBeat::Turn;
}

//--- 翻面の運鏡。再生位置(Holding)/ 計時(PutBack)から3つの重みを決める。カメラへの適用は ApplyCamera が行う。
//    どれも位置/時刻の純関数(同じ値なら同じ画)=決定論的で、巻き戻しても同じ道を逆にたどる。
void SceneForge::UpdateFlipCamera(float tick)
{
	// 火钳へ振り向く曲線: 0→1(振り向く)→1(手に取る)→0(元の視点へ)。両端は SmoothStep で緩急。
	auto tongsCurve = [](float t01) {
		t01 = fminf(fmaxf(t01, 0.0f), 1.0f);
		if (t01 < FLIP_REACH_FRAC)  return Lerp::SmoothStep(t01 / FLIP_REACH_FRAC);
		if (t01 < FLIP_RETURN_FRAC) return 1.0f;
		return 1.0f - Lerp::SmoothStep((t01 - FLIP_RETURN_FRAC) / (1.0f - FLIP_RETURN_FRAC));
	};

	switch (m_flipPhase)
	{
	case FlipPhase::Holding:
	{
		const float tongs01 = m_flipHold / FLIP_TONGS_OUT_TIME;							// Tongs 区間の進み(超えたら 1)
		m_camTongsW   = tongsCurve(tongs01);
		m_camGripW    = Lerp::SmoothStep((m_flipHold - FLIP_TONGS_OUT_TIME) / FLIP_GRIP_TIME);	// Grip 区間で刃へ寄る(Turn 中は 1)
		m_hammerStowW = Lerp::SmoothStep(tongs01 / FLIP_REACH_FRAC);					// 振り向く間にハンマーを置く
		break;
	}
	case FlipPhase::PutBack:
	{
		const float t01 = m_flipTimer / FLIP_PUTBACK_TIME;
		m_camTongsW   = tongsCurve(t01);
		m_camGripW    = Lerp::Damp(m_camGripW, 0.0f, m_gripLambda, tick);				// 刃への寄りは元の視点へ
		m_hammerStowW = 1.0f - Lerp::SmoothStep((t01 - FLIP_RETURN_FRAC) / (1.0f - FLIP_RETURN_FRAC));	// 戻る間に取り上げる
		break;
	}
	default:
		m_camTongsW   = 0.0f;
		m_camGripW    = Lerp::Damp(m_camGripW, 0.0f, m_gripLambda, tick);
		m_hammerStowW = 0.0f;
		break;
	}
	// 腰の火钳モデルの表示は m_tongsInHand に従う(DrawCarry が読む。作業台の火钳は飾りで、隠さない)。
}

void SceneForge::FinishGame()
{
	// PLAY中のBGM/ループ音を止め、成功音→結果BGMへ切り替える
	// (淬火の「ジュワ〜」と蒸気音は、刃を水に入れた瞬間に TryQuench が鳴らし済み)
	Audio::Stop(Audio::BGM_PLAY);				// ゲーム中BGMを止める
	if (m_heatSndOn)  { Audio::Stop(Audio::SE_FORGE_LOOP); m_heatSndOn  = false; }
	if (m_burnSndOn)  { Audio::Stop(Audio::SE_BURN_LOOP);  m_burnSndOn  = false; }
	if (m_grindSndOn) { Audio::Stop(Audio::SE_GRIND_LOOP); m_grindSndOn = false; }
	Audio::Play(Audio::SE_SUCCESS, 0.8f);		// 完成の合図
	Audio::PlayLoop(Audio::BGM_RESULT, 0.5f);	// 結果画面BGM
	m_state = GAME_RESULT;
	m_walkMode = false;	// 結果画面は通常カメラで見せる(走動カメラを解除)
	m_modeTrans = ModeTrans::None;
}

//--- タイトル: 雰囲気で自動的に火花を出しつつ、SPACEで開始
void SceneForge::UpdateTitle(float tick)
{
	UpdateProgressReset(tick);	// チュートリアルの ✓: 放置で自動リセット / Shift+Delete 長押しで手動リセット

	// 黒転じは使わない: タイトルはゲーム世界そのもの。
	// SPACE → モード選択(チュートリアル / 通常モード) → 決定 → ロゴ淡出 → カメラが金床へ → 鍛打(Title.cpp UpdateIntro)。
	if (m_introPhase != IntroPhase::None || m_fade.IsBusy()) return;

	// チュートリアルの課程の一覧(モード選択で「チュートリアル」を選んだ後)。最後の行 = 戻る(モード選択へ)
	if (m_courseSelectOpen && m_recipe)
	{
		const int nCourse = (int)m_recipe->courses.size();
		const int N = nCourse + 1;
		if (InputActions::Pressed(Action::Back)) m_courseRequest = nCourse;	// ESC / B = 戻る
		if (InputActions::Pressed(Action::MenuUp))   m_courseSel = (m_courseSel + N - 1) % N;
		if (InputActions::Pressed(Action::MenuDown)) m_courseSel = (m_courseSel + 1) % N;
		if (InputActions::Pressed(Action::Confirm)) m_courseRequest = m_courseSel;
		if (m_courseRequest < 0) return;
		const int req = m_courseRequest;
		m_courseRequest    = -1;
		m_courseSelectOpen = false;
		if (req >= nCourse) { m_modeSelectOpen = true; m_modeSel = (int)ModeItem::Tutorial; m_modeRequest = -1; return; }	// 戻る
		m_tutorial   = true;
		m_courseIdx  = req;
		m_introPhase = IntroPhase::LogoFade;
		m_introTimer = 0.0f;
		Audio::Play(Audio::SE_TITLE_FADE, TITLE_FADE_SE_VOLUME);
		return;
	}

	if (!m_modeSelectOpen)
	{
		if (InputActions::Pressed(Action::Confirm)) { m_modeSelectOpen = true; m_modeSel = (int)ModeItem::Tutorial; m_modeRequest = -1; }
		return;	// 開いたフレームは決定を読まない(同じ SPACE で即決定しない様に)
	}
	const int N = (int)ModeItem::Count;
	if (InputActions::Pressed(Action::Back)) { m_modeSelectOpen = false; return; }	// 選ばずに戻る(ESC / B)
	if (InputActions::Pressed(Action::MenuUp))   m_modeSel = (m_modeSel + N - 1) % N;
	if (InputActions::Pressed(Action::MenuDown)) m_modeSel = (m_modeSel + 1) % N;
	if (InputActions::Pressed(Action::Confirm)) m_modeRequest = m_modeSel;
	if (m_modeRequest < 0) return;

	if (m_modeRequest == (int)ModeItem::Quit)
	{
		PostQuitMessage(0);	// 一時停止メニューの「ゲームを終了する」と同じ(メインループが抜けて Uninit=調整値の保存も走る)
		m_modeRequest = -1;
		return;
	}
	const bool tutorial = (m_modeRequest == (int)ModeItem::Tutorial);
	m_modeSelectOpen = false;
	m_modeRequest    = -1;
	if (tutorial)	// チュートリアル = まず課程の一覧を開く(選んだ課程から始まる)
	{
		m_courseSelectOpen = true;
		m_courseSel        = 0;
		m_courseRequest    = -1;
		return;
	}
	m_tutorial   = false;
	m_courseIdx  = -1;	// 通常モード = 最初から最後まで一通り
	m_introPhase = IntroPhase::LogoFade;
	m_introTimer = 0.0f;
	Audio::Play(Audio::SE_TITLE_FADE, TITLE_FADE_SE_VOLUME);
}

//--- 鎚の横位置を平滑追従: 準心が格子単位で跳ぶのを Lerp::Damp で滑らかに。
//    目標は現在の照準点(m_aimWorld)＋既定オフセット。Draw はこの m_hammerPos を読む。
void SceneForge::UpdateHammerFollow(float tick)
{
	DirectX::XMFLOAT3 tgt = {
		m_aimWorld.x + m_hammerOff[0],
		0.0f,							// y は使わない(高さは m_hammerLift のアニメで別途)
		m_aimWorld.z + m_hammerOff[2],
	};
	if (!m_hammerPosInit) { m_hammerPos = tgt; m_hammerPosInit = true; }	// 起動時は瞬間セット
	m_hammerPos = Lerp::Damp(m_hammerPos, tgt, m_hammerFollow, tick);
}

//--- 鍛造中
void SceneForge::UpdatePlay(float tick)
{
	// F1(デバッグUI)を開いている間、画面フェード(遷移)中、および走動モード中はゲーム入力を凍結する。
	// ※走動中・走動⇔工位の移動アニメ中に打鉄/加熱/工程FSMが動かないよう条件に含める。
	bool inputOn = !ToolUiOpen() && !m_fade.IsBusy() && !m_walkMode && !Transitioning();

	// --- 工位から出る: E。翻面中は出ない(火钳を持ったまま離れない)。ESC は一時停止メニュー専用(2026-10-08)。
	//   淬火の動画(刃が水に入った後)も取り消せない=出られない。
	const bool quenchLocked = (m_quenchTurn > 0.0f || m_plunge > 0.0f);	// 刃を立て始めたら取り消せない
	if (inputOn && m_flipPhase == FlipPhase::None && !quenchLocked && InputActions::Pressed(Action::Interact))
	{
		m_charging = false; m_charge = 0.0f;	// 蓄力中なら破棄(暴発させない)
		BeginExitStation();
		return;
	}
	// --- 工位から鉄を掴んで出る: Q/X。E と同じ条件で出て、出終わったら続けて火钳で掴む(UpdateModeTrans)。
	//   鉄匠は鉄を置いて離れてから取りに戻らない=火钳で掴んだまま出る。「E で出る → 鉄を見て Q」の2手を1手に(2026-10-17 ユーザー決定)。
	//   試し切りでは使わない(柄を付けた剣を火ばさみで掴む事は無い。離れるのは E)
	if (inputOn && m_flipPhase == FlipPhase::None && !quenchLocked && InputActions::Pressed(Action::Grab)
	    && m_workAt == m_station && !m_carrying && m_station != Station::TestCut)
	{
		m_charging = false; m_charge = 0.0f;
		BeginExitStation();
		m_gripAfterExit = Transitioning();	// 出始めた時だけ(既に過渡中で出られなかった時は掴まない)
		return;
	}

	// Pキー: 瞄準区域の可視化トグル(デバッグ用。既定OFF=KCD式に「叩く場所」を示さない)
	if (inputOn && IsKeyTrigger('P')) m_showAimHi = !m_showAimHi;
	if (inputOn && IsKeyTrigger('G')) m_showGhost = !m_showGhost;	// 目標ゴースト表示切替
	if (inputOn && IsKeyTrigger('K')) m_hideCoalTest = !m_hideCoalTest;	// 【診断】炭床の表示/非表示(炉の跳動切り分け)

	// --- 翻面の子状態機を先に駆動(F起動→火钳運鏡→夹む→マウス翻し→面確定) ---
	//   None 以外の間はこの後のハンマー入力を止める(火钳を扱っている＝叩けない)。
	UpdateFlip(tick, inputOn);
	if (inputOn) UpdateFlipCamera(tick);	// 段階→運鏡の重み(F1中は一時停止=画も止まる)
	const bool flipping = (m_flipPhase != FlipPhase::None);

	// --- 温度: 「入力」と「世界の時間」を分ける ---
	//   simOn  = 世界の時間が流れているか(F1デバッグ中・画面フェード中だけ止まる)。
	//   inputOn= 玩家が工位で操作できるか(走動中/移動アニメ中は false)。
	//   自然冷却は鉄そのものの物理なので simOn で毎フレーム進める=走動中も冷める。
	//   熱源は炉の炭火: 鉄が炉に置かれている間(m_workAt==Hearth)だけ熱が入る。
	//   風箱は炉の工位で玩家が行う行為なので inputOn の時だけ=炭火に追加で風を送る。
	//   2026-10-22(ユーザー決定。10-19 の「押す毎に1回踏む」を置き換え): R を押している間だけ風が送られる。
	//     押す = 風(m_bellowsAir 0..1)が Lerp::Damp でだんだん強くなる / 離す = だんだん弱まり、炭火だけの速さへ戻る。
	//     旧方式は「離しても溜まった風で数秒速いまま」「踏んだ後 2.2 秒は押しても無反応」で、操作と結果が結び付かなかった。
	const bool simOn  = !ToolUiOpen() && !m_fade.IsBusy();
	const bool inFire = !m_carrying && m_workAt == Station::Hearth;
	const bool pumping = inputOn && m_station == Station::Hearth && InputActions::Held(Action::Bellows);
	if (pumping)
	{
		m_bellowsHeldSec += tick;	// 操作の案内: ふいごを押していた秒(LessonMetric: Bellows)
		// 押した瞬間に「フッ」+ 震動。押し続けている間は、音1回分(BELLOWS_PUMP_INTERVAL)ごとに鳴らし直す=踏み続けている音
		if (InputActions::Pressed(Action::Bellows) || (m_time - m_bellowsPumpAt) >= BELLOWS_PUMP_INTERVAL)
		{
			Haptics::Pulse(RUMBLE_BELLOWS, BELLOWS_PUMP_RUMBLE_SEC);
			Audio::PlayAt(Audio::SE_BELLOWS_PUMP, PropCenter("StBellows", CoalPos()), BELLOWS_PUMP_VOLUME);
			m_bellowsPumpAt = m_time;
		}
	}
	if (simOn)
		m_bellowsAir = Lerp::Damp(m_bellowsAir, pumping ? 1.0f : 0.0f, pumping ? BELLOWS_AIR_RISE : BELLOWS_AIR_FALL, tick);
	// ふいごの見た目: 押している間は押し下がり、離すと BELLOWS_REFILL_SEC で膨らみ直す
	m_bellowsSquash = Lerp::Damp(m_bellowsSquash, pumping ? 1.0f : 0.0f, pumping ? BELLOWS_PRESS_LAMBDA : BELLOWS_REFILL_LAMBDA, tick);
	const float air = m_bellowsAir;	// 0 = 炭火だけ / 1 = 風を送り続けて一番熾った状態
	if (simOn)
	{
		const bool tempering = Tempering();
		if (inFire && tempering)
		{
			// 焼き戻し: 刃は炭の上(TEMPER_LIFT)=温度は上げない(赤くなれば焼入れが消える)。表面の酸化膜の色だけが進む
			//   (色の進みと滑らせる入力は UpdateTemper)。
			m_forging.Cool(tick);
		}
		else if (inFire)
		{
			// 火の中: 周囲=火なので、火の温度へ指数的に近づく(ニュートンの法則。室温への自然冷却の代わり)。
			const float fireTemp = Lerp::Linear(COAL_FIRE_TEMP, BELLOWS_FIRE_TEMP, air);	// 溜まった風の分だけ炭が熱く・速く熱が入る
			const float k        = Lerp::Linear(COAL_HEAT_K,    BELLOWS_HEAT_K,    air);
			m_forging.AddHeat((fireTemp - m_forging.Heat()) * (1.0f - expf(-k * tick)));
		}
		else m_forging.Cool(tick);							// 火の外: 鉄が自分で冷める
		UpdateBoil(tick);	// 水中: 沸騰の段階ごとの速さで冷える + 沸き立ち(蒸気/水面)。水の外では何もしない
	}
	// 炉火の唸り: 風が溜まっている間だけ、溜まり具合の大きさで鳴らす(止めて鳴らし直さない=踏むたびに膨らんで引く)
	if (air > BELLOWS_SOUND_MIN)
	{
		if (!m_heatSndOn) { Audio::PlayLoop(Audio::SE_FORGE_LOOP, 0.0f); m_heatSndOn = true; }
		Audio::SetLoop(Audio::SE_FORGE_LOOP, BELLOWS_SOUND_VOLUME * air, 1.0f);
		Audio::SetLoopPos(Audio::SE_FORGE_LOOP, CoalPos());	// 唸りは炉の火床から
	}
	else if (m_heatSndOn) { Audio::Stop(Audio::SE_FORGE_LOOP); m_heatSndOn = false; }
	// 炉の光: 風の分だけ燃え上がって部屋が明るくなり、抜けるとゆっくり落ち着く(上りと下りで速さが違う)
	const float boostTarget = Lerp::Linear(1.0f, m_fireBellowsBoost, air);
	m_fireBoost = Lerp::Damp(m_fireBoost, boostTarget, boostTarget > m_fireBoost ? FIRE_BOOST_RISE : FIRE_BOOST_FALL, tick);

	// --- 過熱で放置すると鋼全体が焼けていく(損傷が蓄積)＋ジュー音 ---
	//   冷却と同じく鉄の物理なので simOn で進める(F1中は温度と一緒に止まる)。
	if (simOn && m_forging.Heat() > OVERHEAT)
	{
		m_forging.BurnAll(BURN_RATE * tick);	// 過熱で鉄全体が焼ける(損傷が蓄積)
		m_sizzleTimer -= tick;
		if (m_sizzleTimer <= 0.0f) { Audio::PlayAt(Audio::SE_SIZZLE, WorkAnchor()); m_sizzleTimer = 0.22f; }
		// 火に入れたまま過熱させた=誤り。独白で一度だけ知らせる(罰は無い)。
		if (inFire && !m_overheatWarned)
		{
			Say((const char*)u8"熱しすぎだ！早く火から出せ", IM_COL32(255, 120, 120, 255));
			m_overheatWarned = true;
		}
	}
	else m_sizzleTimer = 0.0f;
	if (m_forging.Heat() < OVERHEAT) m_overheatWarned = false;	// 冷めたら次の過熱でまた言う

	// --- 燃える鋼(火花)・研磨(砥石)の物理と演出 ---
	if (simOn)
	{
		UpdateBurnFx(tick);
		UpdateGrind(tick, inputOn);
		UpdateTemper(tick, inputOn);	// 焼き戻し(炉の工位で、工程が焼き戻しの時だけ働く)
		UpdateTestCutStation(tick, inputOn);	// 試し切り(藁人形の工位で剣を構えている時だけ働く。TestCut.cpp)
	}
	const bool atAnvil = (m_station == Station::Anvil);	// 鍛打(照準/蓄力/打撃)は金床の工位だけ

	// --- 打撃テンポの計測(前回打撃からの経過時間) ---
	m_sinceStrike += tick;
	// 長く止まっていたらリズムはリセット(遅すぎ)
	if (m_sinceStrike > CADENCE_MAX) m_rhythmStreak = 0;

	// --- 照準(FPS方式): 画面中心の準心=カメラ正前方の射線を板と交差させ、当たったセルを求める ---
	//   マウス移動はUpdateMouseLook(Updateの先頭)で視角に累積済み。ApplyCameraがm_camFwdを更新。
	if (inputOn && !flipping && atAnvil) UpdateAim();	// 翻面中はハンマーを置いている=照準判定もしない

	// --- 蓄力ハンマー: 左クリック押しっぱなしで蓄力、離すと打撃。打撃後はクールダウン ---
	if (m_strikeCD > 0.0f) m_strikeCD -= tick;	// クールダウン消化

	// 打撃は金床にいればいつでもできる(工程では縛らない=玩家の自由。工程は UI の案内だけ)。
	//   冷たい鉄を叩けば ForgingSim が ColdHit を返し、主人公の独白で知らせる(負向フィードバック)。
	if (!inputOn || flipping || !atAnvil)
	{
		// F1操作中・翻面中・金床にいない=蓄力をキャンセル(暴発しないように)。
		// 翻面中は左键を火钳に使うので、翻面明けは一度離すまで蓄力させない(m_canStrike=false)。
		m_charging = false;
		m_charge   = 0.0f;
		if (flipping) m_canStrike = false;
	}
	// 開始直後の誤爆防止(一度ボタンを離すまで蓄力しない)
	else if (!m_canStrike)
	{
		if (!InputActions::Held(Action::Use)) m_canStrike = true;
	}
	else if (m_strikeCD <= 0.0f && InputActions::Held(Action::Use))
	{
		m_charging = true;
		m_charge  += CHARGE_RATE * tick;
		if (m_charge > 1.0f) m_charge = 1.0f;
	}
	else if (m_charging)
	{
		DoStrike();
		m_charging = false;
		m_charge   = 0.0f;
		m_strikeCD = m_strikeCDMax;	// 腕を戻す時間=すぐには次を打てない
	}

	// --- 目標形状との一致度を更新(武器時=全区域の平均進度) ---
	if (m_wpOk)
	{
		m_forgeProg = m_forging.SegAverage();	// 全体進捗(表示・モーフのプレビュー用)
		m_match = m_forgeProg;
	}
	else m_match = m_forging.ShapeMatch();

	// --- 翻面の見た目 ---
	//   F を押している間は再生位置が m_flipAngle を直接決める(UpdateFlip 内)。それ以外の時は、確定済みの
	//   面の清潔な角(0 か π)へ Damp で落ち着かせる。Damp はフレームレート非依存。
	if (m_flipPhase != FlipPhase::Holding)
	{
		float flipTarget = (float)m_forging.Side() * DirectX::XM_PI;	// 表=0, 裏=π
		m_flipAngle = Lerp::Damp(m_flipAngle, flipTarget, FLIP_TURN_LAMBDA, tick);
	}

	// --- ハンマーの上下: 真の弾簧-阻尼(spring-damper)物理 ---
	// 自然長 HAMMER_REST_LIFT のバネに質量 m の錘が付く模型(老師の SceneSpring と同じ流儀)。
	// 打撃(DoStrike)の瞬間に錘を接触位置(lift=0)まで沈め、上向きの初速 v0=J/m を与える。
	// 以後は毎フレーム、老師の手順どおりに合力→加速度→速度→位置を積分するだけ:
	//   ・張力(復元力) = -k * (現在位置 - 自然長)      … フックの法則
	//   ・抵抗力(阻尼) = -c * 速度                       … 速度比例の減衰
	//   ・合力 = 張力 + 抵抗力  (重力は静止高に折込み済みなので単列しない)
	// 上死点を越える過冲(overshoot)も静止高への収束も、係数 k/c/m から自動的に生まれる
	// =手描きの sin 曲線を廃止。蓄力中だけは手で保持する(離した後にバネが働く)。
	if (m_charging) m_hammer.Hold(m_charge);	// 蓄力中は手で保持(高さ=静止高+蓄力量, 速度0)
	else            m_hammer.Update(tick);		// 離した後はバネ-阻尼で静止高へ収束(積分はクラス内)

	// 手柄の震動(鎚の ① 溜め / ③ 跳ね返り。② 当たる は DoStrike)
	if (m_charging) Haptics::Hold(ScaleRumble(RUMBLE_CHARGE, m_charge));	// 引き絞るほど指に張り詰める
	UpdateReboundHaptics();

	UpdateHammerFollow(tick);	// 鎚の横位置を照準点へ平滑追従

	// --- フィードバックの減衰 ---
	if (m_shake > 0.0f)     { m_shake -= tick * 3.0f; if (m_shake < 0.0f) m_shake = 0.0f; }
	if (m_popupLife > 0.0f) m_popupLife -= tick;

	// --- 工程(step)状態機を進める ---
	//   各工程状態の OnUpdate が完了条件を見て AdvanceStep() を呼ぶ:
	//     Heat  … 鋼が燃え始めたら(BURN_TEMP)次へ
	//     Forge … 両面の全区域が到位したら次へ(叩く行為自体は上の蓄力/DoStrike が担当)
	//     Grind … 刃の全区域が研ぎ上がったら次へ(研ぐ行為は UpdateGrind が担当)
	//     Quench… 水槽で左クリック→沈める→黒帯→完成(FinishGame)
	//   進める条件は「世界の時間が流れている」(simOn)。旧: inputOn(=工位で操作できる時)だけ進めていた為、
	//   炉に鉄を置いて歩いている間に熱くなっても加熱が「済」にならず、工位に戻るまで判定されなかった(2026-10-12 不具合)。
	//   完了の条件は鉄の状態(温度/形/鋭さ)なので、どこにいても判定してよい。入力を読む工程(淬火の「水に入れる」)は
	//   自分で AtStation(水槽の工位にいる)を確かめてから読むので、歩いている間に誤って進む事は無い。
	if (simOn) m_stepMachine.Update(tick);
}

//--- 鎚の今の揺れの大きさ(ばねの振幅)A = √(x² + (v/ω)²)。x = 静止位置からのずれ、ω = √(k/m)(ばねの固有角振動数)。
//    単振動では位置と速さが入れ替わりながら A は一定 → 阻尼があれば A だけが滑らかに小さくなる(位相に左右されない「揺れの量」)。
float SceneForge::HammerSwingAmplitude() const
{
	const float MIN_OMEGA = 1e-3f;
	const float omega = sqrtf(m_hammer.stiffness / fmaxf(m_hammer.mass, MIN_OMEGA));
	const float x = m_hammer.Lift() - m_hammer.restLift;
	const float v = m_hammer.Velocity() / fmaxf(omega, MIN_OMEGA);
	return sqrtf(x * x + v * v);
}

void SceneForge::StartReboundHaptics()
{
	m_reboundRef    = HammerSwingAmplitude();	// 打った瞬間 = 一番大きい揺れ = 1
	m_reboundActive = m_reboundRef > 0.0f;
}

//--- ③ 跳ね返りの震動。画面の鎚と同じばね(HammerPhysics)から毎フレーム作る:
//    軽い錘・扳机 = 揺れの大きさ(滑らかに弱まる「ジーン」)/ 重い錘 = 揺れの大きさ × 今の速さの割合(行き来に合わせて「ブルン、ブルン」)。
//    k/c/m(F1)を変えれば、画面の跳ね方と手の震え(長さ・脈の速さ・弱まり方)が一緒に変わる。
void SceneForge::UpdateReboundHaptics()
{
	if (!m_reboundActive) return;
	if (m_charging) { m_reboundActive = false; return; }	// 次を溜め始めた = 手で押さえた
	const float amp = HammerSwingAmplitude();
	const float env = amp / m_reboundRef;	// 1(打った瞬間)→ 0
	if (env < REBOUND_END) { m_reboundActive = false; return; }
	const float MIN_OMEGA = 1e-3f;
	const float omega   = sqrtf(m_hammer.stiffness / fmaxf(m_hammer.mass, MIN_OMEGA));
	const float speed01 = fminf(fabsf(m_hammer.Velocity()) / fmaxf(omega * amp, MIN_OMEGA), 1.0f);	// 今の速さ ÷ この揺れの最高速
	const float k = fminf(env, 1.0f) * m_reboundHardness;
	Rumble r;
	r.low          = RUMBLE_REBOUND.low          * k * speed01;
	r.high         = RUMBLE_REBOUND.high         * k;
	r.rightTrigger = RUMBLE_REBOUND.rightTrigger * k;
	Haptics::Hold(r);
}

//--- 炉の火の明るさの倍率 = 揺らぎ(正弦波の重ね合わせ)× ふいご(m_fireBoost)
float SceneForge::FireLightScale() const
{
	const float t = m_time * m_fireFlickerSpeed;
	float wave = 0.0f;
	for (int i = 0; i < 3; ++i) wave += FIRE_FLICKER_WEIGHT[i] * sinf(t * FIRE_FLICKER_HZ[i] * XM_2PI + FIRE_FLICKER_PHASE[i]);
	return fmaxf(1.0f + m_fireFlickerAmount * wave, 0.0f) * m_fireBoost;
}

//--- 揺れた火の位置 = 炉床の位置 + 軸ごとに速さの違う正弦波。炎が跳ねると、照らされた物の明暗とハイライトが動く
void SceneForge::FireLightPos(float out[3]) const
{
	const float t = m_time * m_fireFlickerSpeed;
	for (int i = 0; i < 3; ++i)
		out[i] = m_coalPos[i] + m_fireJitter * sinf(t * FIRE_JITTER_HZ[i] * XM_2PI + FIRE_FLICKER_PHASE[i]);
}

//--- 鉄の柔らかさ = 緑帯の中の位置 0(下端=硬い)..1(上端=柔らかい)。打撃の音色と跳ね返りの量に使う
float SceneForge::IronSoftness(float heat) const
{
	return fminf(fmaxf((heat - IDEAL_MIN) / (IDEAL_MAX - IDEAL_MIN), 0.0f), 1.0f);
}

//--- 打撃の震動(② 当たる)。強さ = 溜めた力 / 音色(重い錘と軽い錘の配分)= 鉄の柔らかさ。
//    熱い鉄は柔らかく、鎚を受け止めて潰れる → 重い錘の「ドスッ」が主。
//    緑帯の下端に近い(冷えてきた)鉄は硬く、鎚を跳ね返す → 軽い錘の「カンッ」が主。
//    頭にごく短い「カッ」(トランジェント)を重ねる = 鉄と鉄がぶつかった瞬間の鋭さ。叩く指(RT)にも「コツン」。
void SceneForge::PulseStrike(float power, float heat)
{
	const float amp  = RUMBLE_STRIKE_BASE + RUMBLE_STRIKE_GAIN * power;
	const float soft = IronSoftness(heat);
	Haptics::Pulse(ScaleRumble(RUMBLE_STRIKE_TRANSIENT, amp), RUMBLE_STRIKE_TRANSIENT_SEC);
	Rumble body;
	body.low  = amp * Lerp::Linear(RUMBLE_STRIKE_LOW_HARD,  RUMBLE_STRIKE_LOW_SOFT,  soft);
	body.high = amp * Lerp::Linear(RUMBLE_STRIKE_HIGH_HARD, RUMBLE_STRIKE_HIGH_SOFT, soft);
	Haptics::Pulse(body, RUMBLE_STRIKE_SEC);
	Rumble finger;
	finger.rightTrigger = amp * RUMBLE_STRIKE_TRIGGER;
	Haptics::Pulse(finger, RUMBLE_STRIKE_TRIGGER_SEC);
}

static const ImU32 SAY_COLD_COL = IM_COL32(160, 205, 255, 255);	// 独白の色: 冷たい(青)。字幕の暗い箱の上で読める明るさ(旧 120,170,255 は石壁の前で沈んだ)

//--- 蓄力を解放して1打: 変形＋フィードバック
void SceneForge::DoStrike()
{
	float power = m_charge;			// 0..1
	const float heat = m_forging.Heat();
	// 有効な打撃 = 温度ゲージの緑帯(IDEAL_MIN..IDEAL_MAX)の中だけ。ゲージで緑に塗っている範囲と判定を一致させる
	//   (緑の外でも成功するなら、緑で示す意味が無い。ユーザー指摘 2026-10-08)。下=冷たすぎ / 上=熱すぎ。
	bool cold = (heat < IDEAL_MIN);
	bool over = (heat > IDEAL_MAX);

	// --- リズム判定: 前回打撃からの間隔が「速すぎず遅すぎず」なら良いテンポ ---
	float interval = m_sinceStrike;
	m_sinceStrike = 0.0f;
	bool goodTempo = (interval >= CADENCE_MIN && interval <= CADENCE_MAX) && !cold;
	if (goodTempo) ++m_rhythmStreak;
	else           m_rhythmStreak = 0;
	bool inGroove = (m_rhythmStreak >= GROOVE_HITS);	// テンポが乗ると効率アップ
	float grooveMult = inGroove ? GROOVE_MULT : 1.0f;	// 変形効率の上昇

	// 準心が板の上に無いなら空振り: 変形も評価もせず、鉄には当たっていないので打鉄音も出さない
	// (冷打音は誤解のもと。清脆な打鉄音が鳴らないこと自体が「外した」合図になる)。
	if (!m_aimValid)
	{
		// 空振りでも錘は砧へ振り下ろされ弾む(鉄は変形しないだけ)=バネに接触＋初速を与える
		m_hammer.Strike();
		StartReboundHaptics();
		Audio::PlayAt(Audio::SE_SWING, m_aimWorld, 0.7f);	// 空を切る「ヒュッ」(鉄に当たっていない合図。文字は出さない)
		Haptics::Pulse(RUMBLE_SWING, RUMBLE_SWING_SEC);	// 金床だけを打った軽い手応え
		m_reboundHardness = REBOUND_HARDNESS_HARD;		// 金床(硬い鋼)を直接打った = よく跳ね返る
	
		NoteLessonMistake(LessonId::Aim);	// 救急: 空振りを重ねたら狙い方を見せる
		return;
	}

	// 温度係数(冷たい→ほぼ効かない, 過熱→効くが品質悪, 適温→最大)。玩家側で算した修正値。
	float heatFactor = cold ? HEAT_EFF_COLD : (over ? HEAT_EFF_OVER : 1.0f);
	// 打撃の「鉄の反応」(体積守恒の金属流動・損傷・成形進度)は ForgingSim が担当。
	//   ここは玩家の動作側=修正値を渡して結果(outcome)を受け取るだけ。結果で下の回饋を出す。
	//   成形は「ハンマーの頭が落ちた所」を中心に効く(StrikeLenCoord。区域単位でなく連続位置=指した所が変わる)。
	int ci = m_aimI, cj = m_aimJ;
	const float lenCoord   = StrikeLenCoord();
	const int  side        = m_forging.Side();
	const bool faceWasDone = m_forging.SideDone(side);
	ForgingSim::StrikeOutcome outcome =
		m_forging.ApplyStrike(ci, cj, lenCoord, power, heatFactor, grooveMult, cold, over);
	m_impactCoord = lenCoord; m_impactTime = m_time;	// 打った所を一瞬光らせる(BuildWeaponMorph)
	// この一打で「上を向いている面」全体が仕上がった瞬間(未完成→完成のエッジ検出)=口笛で「この面は終わり、裏返せ」。
	//   区域ごとの完成は音を鳴らさず、最後の黒皮が落ちる見た目だけで伝える(ユーザー決定 2026-10-04: 視覚=どこ / 音=面の完成)。
	const bool faceJustDone = !faceWasDone && m_forging.SideDone(side);

	// 温度が下がる / 火花 / 振動
	m_forging.AddHeat(-STRIKE_COOL);	// 打撃で熱が金床/鎚へ逃げる
	float sparkScale = (0.4f + power * 1.2f) * (over ? 0.7f : 1.0f);
	if (!cold) Strike(sparkScale);

	// 打撃音: 冷打は鈍い音。通常打撃は金床音を 1→2→1→2 と交互に鳴らす
	if (cold) Audio::PlayAt(Audio::SE_COLD, m_aimWorld, 0.9f);
	else
	{
		// リズムに乗った打撃は、金床が少し高く澄んで鳴る(口笛の代わりのリズムの手応え。口笛は区域完成の合図専用)
		Audio::PlayAt(m_hammerAlt ? Audio::SE_ANVIL2 : Audio::SE_ANVIL1, m_aimWorld, 0.55f + power * 0.45f,
		            inGroove ? GROOVE_RING_PITCH : 1.0f);
		m_hammerAlt = !m_hammerAlt;
	}
	// 手柄の震動: 冷打 = 硬い「ガツン」/ 通常 = 溜めた力と温度で強さと音色(熱い=重く柔らかい / 冷たい=鋭く硬い)
	if (cold) Haptics::Pulse(RUMBLE_COLD_HIT, RUMBLE_COLD_HIT_SEC);
	else      PulseStrike(power, heat);
	// ③ 跳ね返りの量: 冷たい(硬い)鉄ほど鎚を跳ね返す / 熱い(柔らかい)鉄は受け止める(死に打ち)。物理(鎚の動き)は変えない=震動だけ
	m_reboundHardness = cold ? REBOUND_HARDNESS_HARD
	                         : Lerp::Linear(REBOUND_HARDNESS_HARD, REBOUND_HARDNESS_SOFT, IronSoftness(heat));

	// 冷打は「ガツン」と大きく揺れる(手応えが悪い=衝撃だけ大きい)
	m_shake = cold ? (0.6f + power * 0.6f) : (0.3f + power * 0.7f);
	// 打撃=錘を接触位置(lift=0)まで沈め、反発の上向き初速をバネに与える(以後は物理で跳ね返る)
	m_hammer.Strike();
	StartReboundHaptics();

	// 評価: KCD式に「指示せず、誤りだけ知らせる」。負向フィードバックは日本語(主人公の独白)。
	//   過熱/冷打/完成済みの区域を叩く=誤り→独白で知らせるだけ(罰は無し=誰でも最後まで遊べる)。
	//   ForgingSim が返した「鉄がどうなったか」で分岐する(cold/over/完成済みの再判定は不要)。
	const char* label; unsigned int col; float quality = 0.0f;
	// u8"" は C++20 では char8_t。ImGuiはUTF-8バイトを要求するので(const char*)へ再解釈する。
	switch (outcome)
	{
	case ForgingSim::StrikeOutcome::ColdHit:
		label = (const char*)u8"まだ冷たい…赤くなるまで熱して"; col = SAY_COLD_COL;
		NoteLessonMistake(LessonId::LeaveCold);	// 救急: 冷えた鉄を叩き続けたら「離れて炉へ」を見せる
		break;
	case ForgingSim::StrikeOutcome::OverHit:
		// 緑帯の上: 過熱(鋼が焼ける)まで行っていれば強く、手前なら「少し冷ませ」(どちらも有効打ではない)
		label = (heat > OVERHEAT) ? (const char*)u8"熱しすぎだ！鋼が焼ける" : (const char*)u8"熱すぎる…少し冷めるまで待つ";
		col = IM_COL32(255, 120, 120, 255); break;
	case ForgingSim::StrikeOutcome::AlreadyDone:
		label = (const char*)u8"ここはもう完成済みだ";         col = IM_COL32(255, 200,  90, 255);
		if (m_forging.SideDone(side)) NoteLessonMistake(LessonId::Flip);	// 救急: 面が仕上がっているのに叩き続けたら裏返し方を見せる
		break;
	default:	// Shaped = 適温 & 未完成の区域に命中 = 成功。得点のみ
		if      (power > POWER_PERFECT) { label = "PERFECT!"; col = IM_COL32(255, 220, 120, 255); quality = QUALITY_PERFECT; }
		else if (power > POWER_GOOD)    { label = "GOOD";     col = IM_COL32(180, 255, 150, 255); quality = QUALITY_GOOD; }
		else                            { label = "WEAK";     col = IM_COL32(200, 200, 200, 255); quality = QUALITY_WEAK; }
		if (inGroove) quality += GROOVE_QUALITY_BONUS;
		++m_validStrikes;	// 操作の案内: 有効打(LessonMetric: Strike)
		// 完成の合図 = 面が仕上がった瞬間だけ(リズムの手応えは上の金床音の音程で返す)。両面とも済んだ(工程の完了)なら一段高く。
		if (faceJustDone) Audio::Play(Audio::SE_FACE_DONE, FACE_DONE_VOLUME, m_forging.BothSidesDone() ? FACE_DONE_FINAL_PITCH : 1.0f);
		m_qualitySum += quality;
		m_score += (int)(quality * SCORE_PER_QUALITY);
		break;
	}
	m_strikeCount++;

	// ポップアップ表示
	if (inGroove && quality > 0.0f) sprintf_s(m_popupText, sizeof(m_popupText), "%s  (in rhythm)", label);
	else                            strcpy_s(m_popupText, sizeof(m_popupText), label);
	m_popupLife = POPUP_LIFE;
	m_popupCol  = col;
}

//====================================================================
//  加熱/研磨/淬火(工程の「行為」。完了判定は各 Step クラス)
//====================================================================

//--- 主人公の独白(負向フィードバック)。打撃のポップアップと同じ枠に出す。
void SceneForge::Say(const char* text, unsigned int col)
{
	strcpy_s(m_popupText, sizeof(m_popupText), text);
	m_popupLife = POPUP_LIFE;
	m_popupCol  = col;
}

//--- 刃の上のランダムな点(ワールド)。前フレームで作った変形後の頂点から1つ選ぶ=形が変わっても表面から出る。
XMFLOAT3 SceneForge::RandomBladePoint() const
{
	if (!m_wpOk || m_wpVtx.empty()) return m_barAnchor;
	return m_wpVtx[rand() % m_wpVtx.size()].pos;
}

//--- 燃える鋼: BURN_TEMP を越えている間、刃の表面から小さな火花を弾き、パチパチ音をループ。
//    温度(状態)で発火するイベント駆動の演出=アニメや工程に紐付けない(§8 デカップリング)。
void SceneForge::UpdateBurnFx(float tick)
{
	// 運んでいる間も燃えていれば火花を噴く(鉄は手の前に描かれ、RandomBladePoint もその位置を返す)。
	const bool show = (m_state == GAME_PLAY) && m_forging.IsBurning() && !m_quenchContact;
	if (show)
	{
		m_burnSparkAcc += BURN_SPARK_RATE * tick;
		int n = (int)m_burnSparkAcc;
		m_burnSparkAcc -= n;
		for (int i = 0; i < n; ++i)
			m_particles.SpawnBurnSpark(RandomBladePoint());	// 白く枝分かれする燃焼の火花(打撃の橙の火花と見分けがつく)
	}
	else m_burnSparkAcc = 0.0f;

	if (show && !m_burnSndOn)      { Audio::PlayLoop(Audio::SE_BURN_LOOP, 0.5f); m_burnSndOn = true; }
	else if (!show && m_burnSndOn) { Audio::Stop(Audio::SE_BURN_LOOP);           m_burnSndOn = false; }
	if (m_burnSndOn) Audio::SetLoopPos(Audio::SE_BURN_LOOP, WorkAnchor());	// 燃える鉄から(運んでいれば手の前)
}

//--- 研磨: 右クリック点按=足踏み(GrindWheel が回転を持つ)、左長押し=押し当て、マウス左右=刃を滑らす。
//    押し当てた区域の鋭さが「砥石の回転速度」に比例して上がる(速く回すほど早く研げる)。
void SceneForge::ResetGrindSegments()
{
	for (int side = 0; side < ForgingSim::NSIDES; ++side)
		for (int s = 0; s < ForgingSim::NSEG; ++s) m_grindSegWasDone[side][s] = false;
	m_glints.clear();
	m_twinkleTimer = GLINT_TWINKLE_GAP_MIN;
}

void SceneForge::SpawnGlint(int seg, float lenT, float life, float scale, bool atWheel)
{
	if ((int)m_glints.size() >= GLINT_MAX) return;
	m_glints.push_back({ seg, lenT, m_time, life, scale, atWheel });
}

//--- 光芒の寿命と「時々のきらめき」。研ぎ上がった区域(表/裏のどちらかが済み=刃先は両面の境なので同じ所)から1つ選び、
//    その区域の中のランダムな位置に小さく出す。間隔もランダム=規則的な点滅に見えない。砥石の工位にいる間だけ。
void SceneForge::UpdateGlints(float dt)
{
	for (size_t i = 0; i < m_glints.size(); )
	{
		if (m_time - m_glints[i].bornAt >= m_glints[i].life) { m_glints[i] = m_glints.back(); m_glints.pop_back(); }
		else ++i;
	}
	if (!AtStation(Station::Grindstone) || m_workAt != Station::Grindstone || m_carrying) return;
	m_twinkleTimer -= dt;
	if (m_twinkleTimer > 0.0f) return;
	m_twinkleTimer = frand(GLINT_TWINKLE_GAP_MIN, GLINT_TWINKLE_GAP_MAX);
	int done[ForgingSim::NSEG]; int n = 0;
	for (int s = 0; s < ForgingSim::NSEG; ++s)
		if (m_forging.SharpDoneOf(0, s) || m_forging.SharpDoneOf(1, s)) done[n++] = s;
	if (n == 0) return;
	const int pick = (int)frand(0.0f, (float)n) % n;
	SpawnGlint(done[pick], frand(0.0f, 1.0f), GLINT_TWINKLE_SEC, GLINT_TWINKLE_SCALE, false);
}

void SceneForge::UpdateGrind(float tick, bool inputOn)
{
	const int NSEG = ForgingSim::NSEG;
	const bool here = inputOn && m_station == Station::Grindstone;
	// この回で初めて砥石に置いた時は、表(面0)の刃を砥石へ向ける。下を向く面は見た目の向き(GrindSide=鍛造で最後に
	//   上にした面などで変わる)で決まるので、裏が下なら F の裏返しを済ませた状態(半回転)から始める=最初は必ず表。
	if (!m_grindFaceInit && m_workAt == Station::Grindstone && !m_carrying && !m_seqIronOverride)
	{
		m_grindFace = 0; m_grindFlipRoll = 0.0f;
		if (GrindSide() != 0) { m_grindFace = 1; m_grindFlipRoll = XM_PI; }	// 裏が下だった → 半回転して表を下へ(動画でなく即座に)
		m_grindFaceInit = true;
	}
	bool pressing = false;
	if (here)	// 工程に関係なく、砥石の工位にいればいつでも研げる(玩家の自由)
	{
		float dx, dy; ReadMouseDelta(dx, dy);	// 砥石の工位ではマウスを視角でなく刃の滑りに使う
		float hx, hy; InputActions::HandDelta(hx, hy);	// 手柄: 左スティック左右 = 刃を滑らせる(手で動かす)
		dx += hx;
		// 刃が右へ動く=砥石に当たる位置は刃の左側へ移る(刃の長軸は StationRight に揃えてある)
		// 左右反転して置いた(m_restFlip)時は長手の向きが逆 → 画面上の手応え(マウス右=刃が右)を保つ為に符号も逆
		const float prevU = m_grindU;
		m_grindU -= dx * m_grindSens * (m_restFlip ? -1.0f : 1.0f);
		if (m_grindU < 0.0f) m_grindU = 0.0f;
		if (m_grindU > 1.0f) m_grindU = 1.0f;
		if (InputActions::Held(Action::GrindPress)) m_grindSlideTravel += fabsf(m_grindU - prevU);	// 操作の案内: 当てたまま滑らせた量
		// 上下 = 研ぎ角(上へ = 立てる)。刃は少し遅れて追う(手で角度を保つ重さ)。0 = 平らに寝ている
		//   マウスも手柄(右スティック = 回す)も速度制御: 動かした分だけ角度が変わり、離せばその角度のまま。
		//   位置制御(倒した深さ = 角度)は、長い研ぎの間ずっと親指で半分倒し続けることになり疲れて不正確(2026-10-14 試遊で変更)
		float tx, ty; InputActions::LookDelta(tx, ty);
		m_grindAngleTarget = fminf(fmaxf(m_grindAngleTarget - (dy + ty) * GRIND_ANGLE_SENS, 0.0f), GRIND_ANGLE_MAX);
		// F を押し続ける = 刃を裏返してもう片方の刃を研ぐ(金床の裏返しと同じ操作)
		if (!m_grindFlipping && InputActions::Pressed(Action::Flip))
		{
			m_grindFlipping  = true;
			m_grindFlipHold  = 0.0f;
			m_grindFlipStart = m_grindFlipRoll = m_grindFace * XM_PI;	// 今の面の清潔な角から(Damp でほぼそこにある)
		}
		if (InputActions::Pressed(Action::Pedal) && m_wheel.Pedal()) Haptics::Pulse(RUMBLE_PEDAL, RUMBLE_PEDAL_SEC);	// 点按=足で一回踏む(右クリック/Space。速すぎる連打は GrindWheel が無視)
		pressing = InputActions::Held(Action::GrindPress) && !m_grindFlipping;	// 裏返している最中は砥石に当てられない(刃が回っている)
	}
	m_wheel.Update(tick, pressing);			// 足を止めれば摩擦で止まる。押し当て中は余計に減速

	// 裏返し: 押している間は進み、離すと戻る(工位を離れた時も「離した」扱い=途中で残らない)
	if (m_grindFlipping)
	{
		const bool held = here && InputActions::Held(Action::Flip);
		m_grindFlipHold += held ? tick : -tick * FLIP_REWIND_RATE;
		if (m_grindFlipHold >= GRIND_FLIP_SEC)
		{
			m_grindFace ^= 1;								// 裏返り切った = 研ぐ面が替わる
			++m_grindFlipCount;								// 操作の案内: 砥石で裏返せた
			Haptics::Pulse(RUMBLE_FLIP, RUMBLE_FLIP_SEC);
			m_grindFlipping = false;
			m_grindFlipRoll = m_grindFace * XM_PI;			// 半回転先と同じ向き(2π 違うだけ)=見た目は不変で角度が増え続けない
		}
		else if (m_grindFlipHold <= 0.0f) { m_grindFlipHold = 0.0f; m_grindFlipping = false; }	// 最初まで戻った = やめた
		else m_grindFlipRoll = m_grindFlipStart + XM_PI * Lerp::SmoothStep(m_grindFlipHold / GRIND_FLIP_SEC);	// 再生位置 → 回転(巻き戻しも同じ道)
	}
	if (!m_grindFlipping)
		m_grindFlipRoll = Lerp::Damp(m_grindFlipRoll, m_grindFace * XM_PI, GRIND_FLIP_LAMBDA, tick);	// 普段は今の面の清潔な角へ落ち着く
	m_grindPress    = Lerp::Damp(m_grindPress, pressing ? 1.0f : 0.0f, GRIND_PRESS_LAMBDA, tick);	// 押し当て=浮いた所から砥石へ下ろす
	m_grindAngle = Lerp::Damp(m_grindAngle, m_grindAngleTarget, GRIND_ANGLE_FOLLOW, tick);

	const float speed = m_wheel.Speed01();
	constexpr float MIN_GRIND_SPEED = 0.02f;	// これ未満=砥石がほぼ止まっている(研げない・火花も出ない)
	const bool grinding = pressing && speed > MIN_GRIND_SPEED;
	bool onDoneSpot = false;	// 研ぎ上がった所を研いでいる(火花が細り、音が軽く高くなる=もう削る物が無い)
	bool goodAngle  = false;	// このフレーム、使える角度で研げた(操作の案内: 角度の課は「途切れずに」続いた長さで測る)
	if (grinding)
	{
		if (speed >= GRIND_WORK_MIN_SPEED) m_grindRunTime += tick;	// 操作の案内: 左の長押し + 右の連打 で研げていた秒
		// 研いでいる所 = 砥石の接点(刃の長手のどこが砥石に当たっているか)。m_grindU から区域を推測せず、
		//   描画と同じ WeaponWorld の逆変換で求める=見えている暗い所を当てれば、そこが研げる(鍛造の StrikeLenCoord と同じ考え方)。
		const float lenCoord = GrindLenCoord();
		int cell = (int)lenCoord; if (cell < 0) cell = 0; if (cell > ForgingSim::NL - 1) cell = ForgingSim::NL - 1;
		const float eff = GrindAngleEfficiency();	// 角度の正しさ: 火花の量と評価だけに効く(研ぎ進みには効かない)
		const int  side = GrindSide();	// 砥石へ向いている刃の面
		onDoneSpot = m_forging.EdgeCellDoneOf(side, cell);
		const bool faceWasDone = m_forging.SharpProgress(side) >= 1.0f;
		// 誤りは独白で知らせ(1回の独白 = 1回の誤り)、同じ誤りを重ねたら操作の案内(救急)が直し方を見せる(Tutorial.cpp)。
		if (eff > 0.0f)
		{
			// 有効な研ぎ = 使える角度 + 砥石が十分に回っている。有効な間は一定の速さで残り作業量を減らす(ForgingSim::ApplyGrind)。
			if (speed < GRIND_WORK_MIN_SPEED) { /* 砥石が遅すぎる: 研げない(火花も細い=踏めと分かる) */ }
			else if (m_forging.ApplyGrind(side, lenCoord, tick) == ForgingSim::GrindOutcome::AlreadySharp)
			{
				if (m_popupLife <= 0.0f)
				{
					Say((const char*)u8"そこはもう十分だ", IM_COL32(255, 200, 90, 255));	// 研ぎ上がった所を削る=誤り
					// 面が全部済んでいる → 裏返すべき / まだ残りがある → 別の所へ滑らせるべき
					NoteLessonMistake(faceWasDone ? LessonId::GrindFlip : LessonId::GrindSlide);
				}
			}
			else goodAngle = true;	// 正しく研げた
		}
		else if (m_popupLife <= 0.0f)
		{
			// 角度が外れて研げていない: どちらに外れたかだけ独白で言う(正しい角度そのものは教えない=KCD式の負向フィードバック)
			const bool tooFlat = fabsf(m_grindAngle) < GRIND_IDEAL_ANGLE;
			Say(tooFlat ? (const char*)u8"寝かせすぎだ…刃に当たっていない" : (const char*)u8"立てすぎだ…刃先が丸まる",
			    IM_COL32(255, 200, 90, 255));
			m_grindTooFlat = tooFlat;	// 救急の絵で「どちらへ直すか」の矢印を明るくする
			NoteLessonMistake(LessonId::GrindAngle);
		}

		// この面の刃が全部研ぎ上がった瞬間: 「完成」の合図(鍛造の面の完成と同じ音。両面とも済めば一段高く)
		const bool faceJustDone = !faceWasDone && m_forging.SharpProgress(side) >= 1.0f;
		if (faceJustDone)
			Audio::Play(Audio::SE_FACE_DONE, FACE_DONE_VOLUME, m_forging.AllSharp() ? FACE_DONE_FINAL_PITCH : 1.0f);

		// 区域(NSEG)が研ぎ上がった瞬間(立ち上がり): その区域の刃を光らせ、小さな「チン」。
		//   面の最後の区域なら面の完成の音だけにする(2つ重ねない=大きな区切りが小さな音に埋もれない)。
		for (int s = 0; s < ForgingSim::NSEG; ++s)
		{
			const bool done = m_forging.SharpDoneOf(side, s);
			if (done && !m_grindSegWasDone[side][s])
			{
				// 光芒は砥石が当たっている所(=今研ぎ上げた所。玩家が見ていて火花が出ている所)。その区域の中に収める
				const float per  = (float)ForgingSim::NL / ForgingSim::NSEG;	// 1区域のセル数
				const float lenT = fminf(fmaxf(lenCoord / per - s, 0.0f), 1.0f);
				SpawnGlint(s, lenT, GRIND_SEG_FLASH_SEC, 1.0f, true);
				if (!faceJustDone) Audio::Play(Audio::SE_SEG_DONE, GRIND_SEG_SE_VOLUME);
			}
			m_grindSegWasDone[side][s] = done;
		}

		// 研ぎ火花: 砥石と刃の接点から、回転が速いほど・角度が正しいほど多く。研ぎ上がった所ではまばらに
		m_grindSparkAcc += GRIND_SPARK_RATE * speed * eff * (onDoneSpot ? GRIND_DONE_SPARK_FACTOR : 1.0f) * tick;
		int n = (int)m_grindSparkAcc;
		m_grindSparkAcc -= n;
		// 出る所 = 刃と砥石の接点(砥石の上端。刃はそこへ滑らせて当てている)。
		// 向き = 砥石の縁が動く向き(接線)= 車軸に直交する水平方向へ、奥へ向けて飛ばす(手前へ飛ぶと画面を覆う)。
		if (n > 0)
		{
			const XMFLOAT3 away(-m_stationViewDir.x, GRIND_SPARK_DIP, -m_stationViewDir.z);
			m_particles.SpawnSparksDir(StationBase(Station::Grindstone), away, GRIND_SPARK_SPREAD, n, GRIND_SPARK_SPEED_MIN, GRIND_SPARK_SPEED_MAX);
		}
	}
	else m_grindSparkAcc = 0.0f;
	// 角度の課の「できた」量 = 使える角度を GRIND_ANGLE_HOLD_SEC 途切れずに保てた回数(マウスを端から端へ振って通り過ぎるだけでは増えない)
	const float prevStreak = m_grindGoodStreak;
	m_grindGoodStreak = goodAngle ? m_grindGoodStreak + tick : 0.0f;
	if (prevStreak < GRIND_ANGLE_HOLD_SEC && m_grindGoodStreak >= GRIND_ANGLE_HOLD_SEC) ++m_grindAngleHolds;	// 保てた瞬間(エッジ)に1回

	UpdateGlints(tick);	// 研ぎ上がった区域の光芒(仕上がった瞬間 + 時々のきらめき)

	// --- 研磨音: 止めずに鳴らし続け、音量と音程だけを砥石の状態に追従させる ---
	//   押す/離すたびに Stop→PlayLoop すると、毎回素材の頭から鳴り直して機械的に聞こえる。
	//   本物の砥石は連続音で、速く回すほど高く大きい → 砥石の工位にいる間はループを流しっぱなしにし、
	//   目標音量 = (押し当てていれば)回転速度、音程 = 回転速度で補間。Damp で滑らかに(プツッと鳴らない)。
	const bool atGrind = (m_state == GAME_PLAY) && !m_walkMode && m_station == Station::Grindstone;
	// 押し当てて砥石が回っていれば、遅くても最低 GRIND_SND_MIN_LEVEL は聞こえる(速いほど大きい)。
	float target = grinding ? GRIND_SND_MIN_LEVEL + (1.0f - GRIND_SND_MIN_LEVEL) * speed : 0.0f;
	if (onDoneSpot) target *= GRIND_DONE_VOL_MUL;	// 研ぎ上がった所: 削る手応えが無く、音が軽い
	m_grindVol = Lerp::Damp(m_grindVol, target, GRIND_SND_LAMBDA, tick);
	if (atGrind && !m_grindSndOn) { Audio::PlayLoop(Audio::SE_GRIND_LOOP, 0.0f); m_grindSndOn = true; }	// 無音で開始
	if (atGrind) Haptics::Hold(ScaleRumble(RUMBLE_GRIND, m_grindVol));	// 手の震えも研ぎ音と同じ量(速く回すほど強く、研ぎ上がった所は軽い)
	if (m_grindSndOn)
	{
		const float pitch = (GRIND_PITCH_MIN + (GRIND_PITCH_MAX - GRIND_PITCH_MIN) * speed) * (onDoneSpot ? GRIND_DONE_PITCH_MUL : 1.0f);
		Audio::SetLoop(Audio::SE_GRIND_LOOP, m_grindVol * GRIND_SND_VOLUME, pitch);
		Audio::SetLoopPos(Audio::SE_GRIND_LOOP, PropCenter("StGrind", WorkAnchor()));	// 砥石から(工位を離れて鉄を運んでも、音の消え際は砥石に残る)
		if (!atGrind && m_grindVol < GRIND_SND_OFF)	// 工位を離れ、音が消えきってから止める
		{
			Audio::Stop(Audio::SE_GRIND_LOOP);
			m_grindSndOn = false;
		}
	}
}

//--- 研ぎの効率: 正しい研ぎ角からのずれ e に対して 1 − (e/TOL)²(正しい角で 1、TOL ずれで 0。山なりに落ちる)。
//    傾けた向き(符号)は面を選ぶだけなので、角度の大きさ |θ| で測る。
float SceneForge::GrindAngleEfficiency() const
{
	const float e = (fabsf(m_grindAngle) - GRIND_IDEAL_ANGLE) / GRIND_ANGLE_TOL;
	return fmaxf(1.0f - e * e, 0.0f);
}

//--- 淬火を試みる(QuenchStep が水槽で左クリックされた時に呼ぶ)。
//    冷めすぎた鋼は焼きが入らない=断って炉へ戻らせる(誤りは独白で知らせるだけ)。
bool SceneForge::TryQuench()
{
	if (m_forging.Heat() < QUENCH_MIN_TEMP)
	{
		Say((const char*)u8"冷めてしまった…炉でもう一度熱してから", SAY_COLD_COL);
		return false;
	}
	if (m_forging.Heat() > QUENCH_MAX_TEMP)
	{
		// 過熱のまま水に入れると割れる。少し冷めるのを待たせる(負向フィードバック=独白だけ。罰は無し)
		Say((const char*)u8"熱すぎる…このまま水に入れたら割れる。少し冷ましてから", IM_COL32(255, 120, 120, 255));
		return false;
	}
	// 音と蒸気はクリックの瞬間でなく、刃が水面に触れた瞬間に出す(SetPlunge)
	m_quenchContact = false;
	++m_plungeCount;	// 操作の案内: 水に入れた
	return true;
}

//--- ① 刃を立てる(刃を下へ)。出だしと止まりがゆっくり(smoothstep)。
void SceneForge::SetQuenchTurn(float t01)
{
	m_quenchTurn = Lerp::SmoothStep(t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01));
}

//--- ② 沈める。smoothstep = ゆっくり構えから動き出し、水の抵抗で止まる。
//    刃の一番低い点が水面を越えた瞬間 = 水に触れた瞬間に「ジュワッ」と蒸気(以後、水中の急冷も始まる)。
void SceneForge::SetPlunge(float t01)
{
	m_plunge = Lerp::SmoothStep(t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01));
	if (!m_quenchContact && m_plunge > 0.0f && IronLowestY() <= m_waterPos[1]) OnQuenchContact();
}

//--- 刃が水面に触れた瞬間。千度近い鋼が水を一気に気化させる「爆発」= 音・噴き出す蒸気・水しぶき・揺れを同時に。
//    以後は蒸気膜段階から沸騰の段階が始まる(UpdateBoil)。
void SceneForge::OnQuenchContact()
{
	m_quenchContact = true;
	m_boilStage     = BoilStage::Film;
	m_quenchStartHeat = m_forging.Heat();	// 淬火の完成度 % の起点
	UpdateBladeWaterline();
	Audio::PlayAt(Audio::SE_QUENCH, WorkAnchor(), 0.9f);	// 水に入った瞬間の「ジュワッ」
	Audio::PlayAt(Audio::SE_STEAM,  WorkAnchor(), 0.9f);	// 続く大量の蒸気「シュワーーッ」
	Haptics::Pulse(RUMBLE_QUENCH_CONTACT, RUMBLE_QUENCH_CONTACT_SEC);
	const float ONE_FRAME = 1.0f;			// rate × dt = 個数。dt=1 として一度に STEAM_BURST 個を出す
	m_particles.EmitSteamLine(m_bladeLineA, m_bladeLineB, STEAM_SPREAD, (float)STEAM_BURST, ONE_FRAME, STEAM_BURST_SPEED);
	m_particles.SpawnSplash(m_bladeLineA, m_bladeLineB, SPLASH_COUNT, SPLASH_SPEED, m_waterPos[1]);
	m_shake = QUENCH_SHAKE;
	// 刃の線で水面を押し下げる → 波動方程式がそれを大きな波にして槽中へ広げ、壁で跳ね返す
	float u0, v0, u1, v1;
	WorldToWaterLocal(m_bladeLineA, u0, v0); WorldToWaterLocal(m_bladeLineB, u1, v1);
	m_waterSim.PushLine(u0, v0, u1, v1, WATER_PUSH_RADIUS, WATER_ENTRY_PUSH);
}

//--- ワールドの点 → 水面の板のローカル(-1..1)。DrawWater の world = 拡縮(大きさ) × Y回転(yaw) × 平行移動 の逆。
//    Y回転でローカル X はワールドの (cos, -sin)、ローカル Z は (sin, cos) を向く → 内積で戻して大きさで割る。
void SceneForge::WorldToWaterLocal(const XMFLOAT3& p, float& u, float& v) const
{
	const float dx = p.x - m_waterPos[0], dz = p.z - m_waterPos[2];
	const float c = cosf(m_waterYaw), s = sinf(m_waterYaw);
	const float MIN_SIZE = 1e-4f;
	u = (dx * c - dz * s) / fmaxf(m_waterSize[0], MIN_SIZE);
	v = (dx * s + dz * c) / fmaxf(m_waterSize[1], MIN_SIZE);
}

//--- 水面を押して波を進める(どの状態でも=結果画面へ移っても波は自然に収まっていく)。
//    押す物: ①揺すり = 刃が上下に動いた分だけ周りの水を押し上げる/引き込む
//            ②沸騰の泡 = 沸き立ち(m_boil)に比例した数の泡が、刃の線の周りで水面を小さく持ち上げる
void SceneForge::UpdateWaterSim(float tick)
{
	if (m_quenchContact)
	{
		float u0, v0, u1, v1;
		WorldToWaterLocal(m_bladeLineA, u0, v0); WorldToWaterLocal(m_bladeLineB, u1, v1);

		const float moved = m_agitate - m_prevAgitate;	// 刃が上下に動いた量(下がると負)
		const float STILL_EPS = 1e-5f;
		if (fabsf(moved) > STILL_EPS) m_waterSim.PushLine(u0, v0, u1, v1, WATER_PUSH_RADIUS, -moved * WATER_STROKE_PUSH);

		m_bubbleAcc += WATER_BUBBLE_RATE * m_boil * tick;
		for (; m_bubbleAcc >= 1.0f; m_bubbleAcc -= 1.0f)
		{
			const float t = frand();							// 刃の線上のどこか
			XMFLOAT3 p(m_bladeLineA.x + (m_bladeLineB.x - m_bladeLineA.x) * t + frand(-WATER_BUBBLE_SPREAD, WATER_BUBBLE_SPREAD), 0.0f,
			           m_bladeLineA.z + (m_bladeLineB.z - m_bladeLineA.z) * t + frand(-WATER_BUBBLE_SPREAD, WATER_BUBBLE_SPREAD));
			float u, v; WorldToWaterLocal(p, u, v);
			const float BUBBLE_MIN_SCALE = 0.5f;				// 泡の大きさのばらつき(0.5..1 倍)
			m_waterSim.Push(u, v, WATER_BUBBLE_RADIUS, WATER_BUBBLE_PUSH * frand(BUBBLE_MIN_SCALE, 1.0f));
		}
	}
	m_prevAgitate = m_agitate;
	m_waterSim.Step(tick);
}

//--- ③ 水中で刃の向きに上下に揺する(刃の向きに上げ下げ。左右に振ると刃が曲がる)。
//    揺すると蒸気の膜が破れる → 核沸騰へ(実際の焼入れで刃を動かす理由そのもの)。
//    玩家がマウスの上下で揺する: マウスの移動量で目標の高さを動かし(手を止めると中央へ戻る)、
//    刃は水の抵抗で少し遅れてそれを追う。膜沸騰の間に刃が動いた道のりが FILM_BREAK_WORK に達すると膜が破れる。
void SceneForge::StirQuench(float dt)
{
	float dx, dy; ReadMouseDelta(dx, dy);
	if (InputPrompts::Device() == InputDevice::Gamepad)
	{
		// 手柄: 左スティック(手)の上下の深さ = 刃の高さ(位置制御)。スティックを上下に弾く = 刃を上下に揺する。離せば中央へ
		m_stirTarget = InputActions::HandStickY() * QUENCH_STIR_RANGE;
	}
	else
	{
		m_stirTarget -= dy * QUENCH_STIR_SENS;								// マウスを上へ = 刃が上へ(画面の y は下向き)
		m_stirTarget  = Lerp::Damp(m_stirTarget, 0.0f, QUENCH_STIR_RETURN, dt);
	}
	m_stirTarget  = fminf(fmaxf(m_stirTarget, -QUENCH_STIR_RANGE), QUENCH_STIR_RANGE);

	const float before = m_agitate;
	m_agitate = Lerp::Damp(m_agitate, m_stirTarget, QUENCH_STIR_FOLLOW, dt);
	const float moved = fabsf(m_agitate - before);
	m_stirTravel += moved;	// 操作の案内: 揺すった量

	const float MIN_DT = 1e-4f;
	const float speed01 = fminf(moved / fmaxf(dt, MIN_DT) / STIR_FULL_SPEED, 1.0f);
	m_stir01 = Lerp::Damp(m_stir01, speed01, STIR_SPEED_LAMBDA, dt);

	if (m_boilStage == BoilStage::Film)
	{
		m_filmBreak += moved;
		if (m_filmBreak >= FILM_BREAK_WORK)
		{
			m_boilStage = BoilStage::Nucleate;	// 揺すって膜を破った
			Haptics::Pulse(RUMBLE_FILM_BREAK, RUMBLE_FILM_BREAK_SEC);	// 「破れた」が手で分かる
		}
	}
}

//--- 淬火の完成度 = 入水時の温度から、沸騰が止む温度(BOIL_END_TEMP)までにどれだけ冷えたか。
//    冷える速さは沸騰の段階で決まる(膜沸騰は遅く、核沸騰は速い。揺するほど速い)=揺すれば早く 100% になる。
float SceneForge::QuenchProgress() const
{
	if (!m_quenchContact) return 0.0f;
	const float MIN_RANGE = 1e-4f;
	const float p = (m_quenchStartHeat - m_forging.Heat()) / fmaxf(m_quenchStartHeat - BOIL_END_TEMP, MIN_RANGE);
	return fminf(fmaxf(p, 0.0f), 1.0f);
}

bool SceneForge::QuenchStirring() const
{
	return m_quenchContact && QuenchProgress() < 1.0f;	// 入水してから、冷え切る(100%)まで
}

//--- 淬火の出来: 膜沸騰が短いほど良い(早く揺すって膜を破った=均一に冷えた)。BEST 以下で 1、WORST 以上で 0。
float SceneForge::QuenchScore() const
{
	const float t = (m_filmTime - QUENCH_FILM_TIME_BEST) / (QUENCH_FILM_TIME_WORST - QUENCH_FILM_TIME_BEST);
	return 1.0f - fminf(fmaxf(t, 0.0f), 1.0f);
}

//--- 刃の長軸の両端(火钳で掴んだ端/反対の端)を水面の高さへ下ろした線。蒸気・水しぶき・水面の泡立ちはこの線から。
void SceneForge::UpdateBladeWaterline()
{
	XMVECTOR a, b; IronEnds(WeaponWorld(), a, b);
	XMStoreFloat3(&m_bladeLineA, a); XMStoreFloat3(&m_bladeLineB, b);
	m_bladeLineA.y = m_bladeLineB.y = m_waterPos[1];
}

//--- 沸騰の段階を進め、その段階の速さで冷やし、沸き立ち(蒸気の量・水面)を追従させる。
//    段階の目標の沸き立ち: 膜沸騰 = 静か(FILM_BOIL) / 核沸騰 = 熱いほど激しい / 対流・水の外 = 0。
void SceneForge::UpdateBoil(float tick)
{
	const float heat = m_forging.Heat();
	float coolRate = 0.0f, target = 0.0f;
	switch (m_boilStage)
	{
	case BoilStage::Film:
		if (heat < LEIDENFROST_TEMP) { m_boilStage = BoilStage::Nucleate; break; }	// 温度が下がって膜が保てない
		coolRate = FILM_COOL_RATE; target = FILM_BOIL;
		m_filmTime += tick;
		break;
	case BoilStage::Nucleate:
	{
		if (heat < BOIL_END_TEMP) { m_boilStage = BoilStage::Convection; break; }
		// 揺するほど新しい冷水が刃に当たる → 速く冷え、激しく沸く
		coolRate = NUCLEATE_COOL_RATE * (1.0f + NUCLEATE_STIR_BONUS * m_stir01);
		const float hot01 = fminf(fmaxf((heat - BOIL_END_TEMP) / (LEIDENFROST_TEMP - BOIL_END_TEMP), 0.0f), 1.0f);
		target = hot01 * (NUCLEATE_CALM_BOIL + (1.0f - NUCLEATE_CALM_BOIL) * m_stir01);
		break;
	}
	case BoilStage::Convection:
		coolRate = CONVECTION_COOL_RATE;
		break;
	default: break;
	}
	m_forging.AddHeat(-coolRate * tick);
	m_boil = Lerp::Damp(m_boil, target, BOIL_FOLLOW_LAMBDA, tick);
	if (m_quenchContact) Haptics::Hold(ScaleRumble(RUMBLE_BOIL, m_boil));	// 膜沸騰は静か / 核沸騰は激しい(蒸気の量と同じ)
	if (m_quenchContact) UpdateBladeWaterline();
}

//--- 終幕へ入る。HUD を消してよいのは「ゲームが必ず終わる」と確定した時だけ
//    (確定前に消すと、まだ続くのに UI が無くなる)。淬火が配方の最後の工程である事をデータで確かめる
//    =別の配方で淬火の後に工程がある武器でも誤って消さない。
void SceneForge::BeginFinale()
{
	m_clearDecided = IsLastStep();
}

bool SceneForge::IsLastStep() const
{
	return m_recipe && m_stepIdx == (int)m_recipe->steps.size() - 1;
}

//--- 焼入れの後に工程が続く時: 刃を沈めた姿勢・揺すり・沸騰を解いて、水槽の上(焼入れ前の構え)に戻す。
//    解かないと m_quenchTurn/m_plunge が残り、工位から出られない(取り消せない演出の扱い)。焼入れの出来(m_filmTime)は残す。
void SceneForge::EndQuenchPose()
{
	m_quenchTurn = 0.0f; m_plunge = 0.0f; m_agitate = 0.0f; m_prevAgitate = 0.0f;
	m_quenchContact = false; m_boilStage = BoilStage::None; m_boil = 0.0f;
	m_stirTarget = 0.0f; m_stir01 = 0.0f;
}

//--- 炉の工位の振る舞いは「今の工程」だけで決まる: 焼き戻しなら色だけが進み温度は上がらない / それ以外は加熱。
bool SceneForge::TemperHolding() const
{
	return Tempering() && !m_carrying && m_workAt == Station::Hearth && m_station == Station::Hearth
	    && (!m_walkMode || Transitioning());
}

bool SceneForge::Tempering() const
{
	return m_recipe && m_stepIdx >= 0 && m_stepIdx < (int)m_recipe->steps.size()
	    && m_recipe->steps[m_stepIdx].type == StepName::Temper;
}

//--- 完了 = 炉の上に置いて色が変わり始めた刃を、火ばさみで取り出した。
//    色が変わる前に取り出したら終わらない: 独白で知らせ、置き直すまで判定しない(水槽から運んで来る間は判定しない)。
bool SceneForge::TemperTakenOut()
{
	if (!m_carrying || !m_temperStarted) return false;
	if (TemperAvg() >= TEMPER_COUNT_MIN) return true;
	Say((const char*)u8"まだ色が変わっていない…炉の上で温める", IM_COL32(255, 200, 90, 255));
	m_temperStarted = false;	// 置き直したらまた判定する
	return false;
}

float SceneForge::TemperAvg() const
{
	float sum = 0.0f;
	for (float t : m_temperCell) sum += t;
	return sum / ForgingSim::NL;
}

//--- 1セルの刃先の色で出来を決める: 麦わら〜茶 = 1 / それより手前(銀に近い=硬く脆い)と先(紫・青=柔らかい)は離れるほど下がる。
float SceneForge::TemperCellScore(float t) const
{
	if (t < TEMPER_IDEAL_MIN) return t / TEMPER_IDEAL_MIN;
	if (t > TEMPER_IDEAL_MAX) return fmaxf(1.0f - (t - TEMPER_IDEAL_MAX) / (1.0f - TEMPER_IDEAL_MAX), 0.0f);
	return 1.0f;
}

//--- 全セルの平均=刃先全体が揃って良い色なら 1。一か所だけ青い・一か所だけ銀のまま、はその分下がる(ムラの評価)。
float SceneForge::TemperScore() const
{
	float sum = 0.0f;
	for (float t : m_temperCell) sum += TemperCellScore(t);
	return sum / ForgingSim::NL;
}

//--- 炭の中心(炉の作業点)の真上に来ている刃の長手位置。描画と同じ WeaponWorld の逆変換で求める
//    =見えている刃のどこが炭の上にあるかと必ず一致する(研ぎの GrindLenCoord と同じ考え方)。
float SceneForge::TemperLenCoord()
{
	if (!m_wpOk) return m_temperU * ForgingSim::NL;
	const XMFLOAT3 p = StationBase(Station::Hearth);
	XMFLOAT3 lp; XMStoreFloat3(&lp, XMVector3TransformCoord(XMLoadFloat3(&p), XMMatrixInverse(nullptr, WeaponWorld())));
	return AimSystem::SegCoordLocal(lp, m_wpMin, m_wpMax, ForgingSim::NL);
}

//--- 焼き戻し: マウス左右(左スティック)で刃を炭の上で滑らせ、炭の中心に近いセルほど色が速く進む(ガウス)。
//    色は戻らない(酸化膜は残る)=取り出しても冷ましても減らない。ふいごで炭が熾り、速く進む=行き過ぎやすい。
void SceneForge::UpdateTemper(float tick, bool inputOn)
{
	// 構え(火ばさみで持ち上げて起こす): 火钳で構えている間(入る/出る過渡中も)1 へ。
	//   挟んだまま運んで来た所から持ち上がって起き、出る時は構えたまま=持ち上げ(Q)/置く(E)の拍子表へそのまま繋がる
	m_temperPoseW = Lerp::Damp(m_temperPoseW, TemperHolding() ? 1.0f : 0.0f, TEMPER_POSE_LAMBDA, tick);
	const bool held = TemperHolding() && !m_walkMode && !Transitioning();
	// 火钳の口の熱さ: 口が炭床の上(四角の中)にあれば熱くなり、外へ離れるほど熱くなりにくい
	//   (刃の手元の端を焼こうとすると口も火の上に来る=現実もそう)。常に少しずつ冷める=一時的な変化(火から離せば数秒で元の色)。
	//   2026-10-20: 旧「炭の中心の真上だけ」では、手元の端を挟む口がほとんど中心に来ず、熱くならなかった
	m_tongsHeat = Lerp::Damp(m_tongsHeat, 0.0f, TONGS_COOL_LAMBDA, tick);
	if (held)
	{
		const XMFLOAT3 jaw = TemperTongsPose().grip;
		// 炭床の四角(DrawCoalBed と同じ: ローカル X を sizeX、Z を sizeY、yaw で回す)のローカル座標へ
		const float dx = jaw.x - m_coalPos[0], dz = jaw.z - m_coalPos[2];
		const float c = cosf(m_coalYaw), s = sinf(m_coalYaw);
		const float lx = dx * c - dz * s, lz = dx * s + dz * c;
		const float ox = fmaxf(fabsf(lx) - m_coalSize[0], 0.0f);	// 四角の外へはみ出した距離(中なら 0)。m_coalSize は半径(半分の大きさ)
		const float oz = fmaxf(fabsf(lz) - m_coalSize[1], 0.0f);
		m_tongsHeat = fminf(m_tongsHeat + TONGS_HEAT_RATE * tick * expf(-(ox * ox + oz * oz) / (2.0f * TONGS_HEAT_SPREAD * TONGS_HEAT_SPREAD)), 1.0f);
	}
	if (!held) return;	// 色が進むのは工位で構えている間だけ(炉に置いて離れたら火から外して置いた扱い)
	m_temperStarted = true;
	if (inputOn)
	{
		float dx, dy; ReadMouseDelta(dx, dy);			// 炉の工位(焼き戻し)ではマウスを刃の滑りに使う
		float hx, hy; InputActions::HandDelta(hx, hy);	// 手柄: 左スティック左右
		// マウス右 = 画面の右へ(2026-10-19 F5: 逆だった)。感度は研ぎの滑りと同じ
		m_temperU = fminf(fmaxf(m_temperU - (dx + hx) * m_grindSens, TEMPER_U_MIN), TEMPER_U_MAX);
	}
	const float rate = TEMPER_RATE * Lerp::Linear(1.0f, TEMPER_BELLOWS_MUL, m_bellowsAir) * tick;	// ふいごの風の分だけ速く
	const float center = TemperLenCoord();				// 炭の中心の真上のセル(連続値)
	const float twoSigma2 = 2.0f * TEMPER_HEAT_SPREAD * TEMPER_HEAT_SPREAD;
	for (int c = 0; c < ForgingSim::NL; ++c)
	{
		const float d = (c + 0.5f) - center;
		m_temperCell[c] = fminf(m_temperCell[c] + rate * expf(-(d * d) / twoSigma2), 1.0f);
	}
}

//--- 今の鉄の一番低い点(モデル箱の8隅をワールドへ運んだ最小の y)。どの軸が刃の幅でも正しい。
float SceneForge::IronLowestY()
{
	const XMMATRIX w = WeaponWorld();
	float lowest = FLT_MAX;
	for (int i = 0; i < 8; ++i)
	{
		XMFLOAT3 c((i & 1) ? m_wpMax.x : m_wpMin.x, (i & 2) ? m_wpMax.y : m_wpMin.y, (i & 4) ? m_wpMax.z : m_wpMin.z);
		XMFLOAT3 p; XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&c), w));
		lowest = fminf(lowest, p.y);
	}
	return lowest;
}

//--- 水槽では鉄を置かず、火钳で挟んだまま構える(淬火は手で持ったまま行う)。
//    水槽の工位にいる間と、そこへ入る過渡の間。歩いて離れたら火钳は腰へ戻る。
bool SceneForge::HoldingAtTrough() const
{
	if (m_state != GAME_PLAY || m_carrying || m_workAt != Station::Trough || m_station != Station::Trough) return false;
	return !m_walkMode || m_modeTrans == ModeTrans::Enter;
}

void SceneForge::SetLetterbox(float t01)
{
	m_letterbox = Lerp::SmoothStep(t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01));
}

//--- 結果: SPACEでタイトルへ戻る
void SceneForge::UpdateResult(float /*tick*/)
{
	if (InputActions::Pressed(Action::Confirm) && !m_fade.IsBusy())
	{
		m_fade.Transition([this] {
			m_state = GAME_TITLE;
			ResetTitleStage();	// 金床の上を「新しい熱い鉄」に戻す(完成した剣は片付ける)
			ResetCourseProgress();	// 通常モードを最後まで遊んだ=この人は終わり → 次の人の為にチュートリアルの ✓ を消す
			Audio::Stop(Audio::BGM_RESULT);				// 結果BGMを止める
			Audio::PlayLoop(Audio::BGM_TITLE, TITLE_BGM_VOLUME);	// タイトルBGMを再開
		});
	}
}

//====================================================================
//  一時停止メニュー(ESC): 続ける / タイトルへ戻る / ゲームを終了する
//====================================================================
//--- 開ける時: 遊んでいて、F1 でも画面遷移中でもなく、タイトルからの導入運鏡も終わっている。
bool SceneForge::CanPause() const
{
	return m_state == GAME_PLAY && !ToolUiOpen() && !m_fade.IsBusy() && m_introPhase == IntroPhase::None;
}

void SceneForge::OpenPause()
{
	m_paused       = true;
	m_pauseSel     = (int)PauseItem::Resume;	// 既定 = 続ける(誤って終了しない様に)
	m_pauseRequest = PauseItem::None;
	m_charging = false; m_charge = 0.0f;		// 蓄力中なら破棄(再開した瞬間に暴発させない)
	// 鳴り続ける効果音(風箱/燃焼/研ぎ)を止める。再開後、必要なら各 Update がまた鳴らす(状態フラグを戻しておく)
	if (m_heatSndOn)  { Audio::Stop(Audio::SE_FORGE_LOOP); m_heatSndOn  = false; }
	if (m_burnSndOn)  { Audio::Stop(Audio::SE_BURN_LOOP);  m_burnSndOn  = false; }
	if (m_grindSndOn) { Audio::Stop(Audio::SE_GRIND_LOOP); m_grindSndOn = false; }
}

//--- OS の光標を出すか。出すのは「マウスで選ぶ画面(タイトル/一時停止/結果)をキーボード・マウスで操作している時」と F1 だけ。
//    遊んでいる間(PLAY)は隠す(照準は準心で示す)。手柄で操作している間はメニューでも隠す
//    (止まった光標が画面中央に残ると、手柄の人には邪魔で、マウスで選ぶ物と誤解させる)。マウスを動かせば InputPrompts が
//    キーボード・マウスへ切り替え、次のフレームで光標が出る。
void SceneForge::UpdateCursorVisibility()
{
	const bool menuScreen = (m_state != GAME_PLAY) || m_paused;
	const bool want = ToolUiOpen() || (menuScreen && InputPrompts::Device() == InputDevice::KeyboardMouse);
	if (want != m_cursorShown) { ShowCursor(want); m_cursorShown = want; }
}

void SceneForge::ClosePause()
{
	m_paused = false;
	m_optionsOpen = false;
	m_canStrike = false;					// 押しっぱなしのボタンで再開直後に打たない(一度離すまで)
	float dx, dy; ReadMouseDelta(dx, dy);	// メニュー中に動かしたカーソルの分を捨てる(再開で視点が跳ばない)
	// カーソルの表示は Update の通常処理が PLAY 中は隠す
}

//--- メニューを開いている間の更新。ESC = 続ける(「続ける」と同じ処理)。キーボードでも選べる。
//    マウスの選択/決定は描画側(DrawPauseMenu)が当たり判定をして m_pauseSel / m_pauseRequest に書く=ここで実行する。
void SceneForge::UpdatePause()
{
	if (m_optionsOpen) { UpdateOptions(); return; }	// 設定の画面(閉じると一時停止メニューへ戻る)
	const int N = (int)PauseItem::Count;
	if (InputActions::Pressed(Action::Pause) || InputActions::Pressed(Action::Back)) m_pauseRequest = PauseItem::Resume;	// ESC / Start / B
	if (InputActions::Pressed(Action::MenuUp))   m_pauseSel = (m_pauseSel + N - 1) % N;
	if (InputActions::Pressed(Action::MenuDown)) m_pauseSel = (m_pauseSel + 1) % N;
	if (InputActions::Pressed(Action::Confirm)) m_pauseRequest = (PauseItem)m_pauseSel;

	const PauseItem req = m_pauseRequest;
	m_pauseRequest = PauseItem::None;
	switch (req)
	{
	case PauseItem::Resume:
		ClosePause();
		break;
	case PauseItem::Options:
		m_optionsOpen = true; m_optionsSel = (int)OptionItem::LessonSpeed; m_optionsRequest = -1; m_optionsStep = 0;
		break;
	case PauseItem::Title:
		ClosePause();
		ReturnToTitle(false);	// 結果画面からタイトルへ戻るのと同じ流れ(黒幕の裏で状態を切り替える)
		break;
	case PauseItem::Quit:
		PostQuitMessage(0);						// 通常の終了と同じ(メインループが抜けて Uninit=調整値の保存も走る)
		break;
	default: break;
	}
}

//--- 設定の画面: 上下 = 行 / 左右 = 値 / 決定(戻る) / ESC = 一時停止メニューへ戻る。値は変えた時に保存。
void SceneForge::UpdateOptions()
{
	const int N = (int)OptionItem::Count;
	if (InputActions::Pressed(Action::Pause) || InputActions::Pressed(Action::Back)) m_optionsRequest = (int)OptionItem::Back;
	if (InputActions::Pressed(Action::MenuUp))   m_optionsSel = (m_optionsSel + N - 1) % N;
	if (InputActions::Pressed(Action::MenuDown)) m_optionsSel = (m_optionsSel + 1) % N;
	if (InputActions::Pressed(Action::Confirm)) m_optionsRequest = m_optionsSel;
	int step = m_optionsStep;
	m_optionsStep = 0;
	if (m_optionsSel != (int)OptionItem::Back)	// 値のある行: 左右で値を変える
	{
		if (InputActions::Pressed(Action::MenuLeft))  step = -1;
		if (InputActions::Pressed(Action::MenuRight)) step = +1;
	}
	if (step != 0)
	{
		// 選んでいる行の「何段目か」を1つ動かす。端で止まる(回り込まない=今どこか分かる)
		auto stepIndex = [step](int& idx, int n) { const int next = idx + step; if (next < 0 || next >= n) return false; idx = next; return true; };
		if (m_optionsSel == (int)OptionItem::LessonSpeed && stepIndex(m_lessonSpeedIdx, (int)_countof(LESSON_SPEED_STEPS)))
			SaveOptions();
		if (m_optionsSel == (int)OptionItem::Rumble && stepIndex(m_rumbleIdx, (int)_countof(RUMBLE_STEPS)))
		{
			Haptics::SetStrength(RUMBLE_STEPS[m_rumbleIdx]);
			Haptics::Pulse(RUMBLE_PREVIEW, RUMBLE_PREVIEW_SEC);	// 新しい強さで一度震わせる
			SaveOptions();
		}
		if (m_optionsSel == (int)OptionItem::Quality && stepIndex(m_qualityIdx, (int)_countof(QUALITY_STEPS)))
		{
			RenderScale() = QUALITY_STEPS[m_qualityIdx];	// 次の描画で PostProcess::Begin が描画先を新しい大きさで作り直す
			SaveOptions();
		}
	}
	const int req = m_optionsRequest;
	m_optionsRequest = -1;
	if (req == (int)OptionItem::Back) { m_optionsOpen = false; m_pauseSel = (int)PauseItem::Options; }
}

//--- 設定の保存/読み込み。1行 = "<キー> <値>"。無い/壊れた行は既定のまま。
static const char* OPTIONS_PATH = "Assets/options.txt";
void SceneForge::LoadOptions()
{
	FILE* fp = nullptr;
	if (fopen_s(&fp, OPTIONS_PATH, "r") != 0 || !fp) return;
	char line[64];
	while (fgets(line, sizeof(line), fp))
	{
		int v = 0;
		if (sscanf_s(line, "lessonspeed %d", &v) == 1 && v >= 0 && v < (int)_countof(LESSON_SPEED_STEPS)) m_lessonSpeedIdx = v;
		if (sscanf_s(line, "rumble %d", &v) == 1 && v >= 0 && v < (int)_countof(RUMBLE_STEPS)) m_rumbleIdx = v;
		if (sscanf_s(line, "quality %d", &v) == 1 && v >= 0 && v < (int)_countof(QUALITY_STEPS)) m_qualityIdx = v;
	}
	fclose(fp);
	Haptics::SetStrength(RUMBLE_STEPS[m_rumbleIdx]);
	RenderScale() = QUALITY_STEPS[m_qualityIdx];
}

void SceneForge::SaveOptions() const
{
	FILE* fp = nullptr;
	if (fopen_s(&fp, OPTIONS_PATH, "w") != 0 || !fp) return;
	fprintf(fp, "lessonspeed %d\n", m_lessonSpeedIdx);	// LESSON_SPEED_STEPS の番号
	fprintf(fp, "rumble %d\n", m_rumbleIdx);				// RUMBLE_STEPS の番号
	fprintf(fp, "quality %d\n", m_qualityIdx);			// QUALITY_STEPS の番号(0 = 標準 / 1 = 高画質)
	fclose(fp);
}

void SceneForge::Update(float tick)
{
	m_time += tick;
	m_lessonClock += tick * LessonSpeed();	// 教学アニメの時計(設定の速さ。一時停止中も進む=設定の見本が動く)
	m_fade.Update(tick);	// 画面フェード(黒幕)を進める。遷移はTransitionの黒転じで実行される
	m_door.Update(tick);	// 扉の開閉の回転(E で切り替えた後、入力と無関係に最後まで回り切る)
	m_grassMapDt += tick;	// 草の踏み跡が薄れる時間(GPU の Fade は Draw の最初でまとめて)

	if (IsKeyTrigger(VK_F2)) m_lightPanelOpen = !m_lightPanelOpen;	// 設計者用: ライティング確認パネル(HUD.cpp DrawLightPanel)
	if (IsKeyTrigger(VK_F3)) m_profilerPanelOpen = !m_profilerPanelOpen;	// 設計者用: 処理の重さ(GPU 計時)。遊びながら見られる様に入力は止めない
	if (IsKeyTrigger('H')) m_shotMode = (ShotMode)(((int)m_shotMode + 1) % (int)ShotMode::Count);	// 撮影用: 普通 → UI なし → UI なし+「一刀両断」なし → 普通
	UpdateCursorVisibility();	// 一時停止中も決める(下で return する前に)

	// --- 一時停止メニュー(ESC)。開いている間は世界を止める(以下の更新を全部飛ばす=時間・温度・粒子も止まる) ---
	//   開いたフレームは UpdatePause を呼ばない: 同じフレームの ESC の押下を「続ける」と読んで即座に閉じていた(2026-10-08 F5)。
	if (!m_paused && CanPause() && InputActions::Pressed(Action::Pause)) { OpenPause(); return; }
	if (m_paused) { UpdatePause(); return; }
	UpdateSun(tick);	// 太陽の時刻(F1/F2 を開いていても進む=光の筋が動くのを見ながら調整できる。一時停止では止まる)

	// チュートリアルの課程を終えた: 「レッスン完了」を見せてから課程の一覧へ
	if (m_state == GAME_PLAY && m_courseComplete && !m_fade.IsBusy() && m_time - m_courseCompleteAt >= COURSE_DONE_SHOW_SEC)
		ReturnToTitle(true);

	// プレイテスト計時: 遊んでいる時間だけ(導入カメラ・F1・画面フェード中は数えない。一時停止は上で return 済み)
	if (m_state == GAME_PLAY && m_introPhase == IntroPhase::None && !ToolUiOpen() && !m_fade.IsBusy())
		TickPlaytestTimer(tick);

	// F8 = 全調整値を起動時スナップショットへ一発リセット(F1デバッグ表示中のみ=誤爆防止)。
	if (DebugUI::IsVisible() && IsKeyTrigger(VK_F8)) RestoreTuning();
	// PLAY中かつF1非表示・遷移中でないときだけ操作を受け付ける(ApplyCameraより先に)。
	bool canControl = (m_state == GAME_PLAY && !ToolUiOpen() && !m_fade.IsBusy());
	if (canControl)
	{
		// 互動の注視判定(①範囲 ②視線)。走動中以外(工位/移動アニメ中)は対象なし=提示がフェードアウト。
		UpdateInteract(tick);
		UpdateGuide();		// 指引 UI: 今の状況から案内文と行き先を決める(HUD.cpp)
		UpdateStationHighlight(tick);	// 次に向かう工位を光らせる濃さ(行き先は UpdateGuide が決めた物)
		UpdateLessons(tick);	// 操作の案内(チュートリアル / 救急)を進める(Tutorial.cpp)
		UpdateSeqBreath(tick);	// 拍子表の再生中だけカメラに呼吸を乗せる重み(Sequence.cpp)
		UpdateCarryAvoid(tick);	// 手の鉄が道具/壁に入らない様に起こす(Carry.cpp)。運んでいない時は 0 へ戻る

		if (Transitioning())
		{
			// 走動⇔工位の移動アニメ中: 取り消し不可。計時だけ進め、入力は捨てる(F1中は一時停止)。
			UpdateModeTrans(tick);
		}
		else if (SequencePlaying())
		{
			// 拍子表の再生中(火钳を抜いて鉄を掴む等): 取り消し不可。カメラも物も拍子表が動かす(Sequence.cpp)。
			// マウス移動は読んで捨てる=終わった瞬間に溜まった移動量で視点が跳ばない。
			UpdateSequence(tick);
			float dx, dy; ReadMouseDelta(dx, dy);
		}
		else if (m_walkMode)
		{
			// --- 走動モード: 一人称で工坊を歩く ---
			UpdateWalkLook();				// マウス→玩家yaw(左右)/カメラpitch(上下)
			// F1スライダの速度を毎フレーム反映。重い鉄を運んでいる間は遅くなる(m_carrySpeedMul, F1「Carry」)
			m_player.SetMoveSpeed(m_walkSpeed * (m_carrying ? m_carrySpeedMul : 1.0f));
			BuildCollisionWorld();					// 道具の凸包/壁線/扉を今の配置でワールドへ(Collision.cpp)
			const XMFLOAT3 before = m_player.GetPosition();
			m_player.Update(tick, m_collision);		// WASDで一人称移動+道具/壁から押し出す(壁に沿って滑る)
			const XMFLOAT3 after = m_player.GetPosition();
			const float walked = sqrtf((after.x - before.x) * (after.x - before.x) + (after.z - before.z) * (after.z - before.z));
			UpdateCarryBob(tick, walked);			// 実際に進んだ距離で揺らす(壁に当たって止まれば揺れも止まる)

			// 互動: ①範囲 ②視線 の両方が true の物件だけ E が効く(Interaction.cpp)。
			//   金床=工位へ移動 / 火钳=取って工位へ移動→翻面。退出(工位→走動)は UpdatePlay 側で E。
			if (m_player.WantInteract() && m_focus >= 0) DoInteract(INTERACTABLES[m_focus]);
			// 鉄を掴むのは別のキー(Q/X)。鉄は工位の上にあるので、同じキーだと「工位に入る」つもりで掴んでしまう(試遊 10-15)
			else if (m_grabFocus >= 0 && InputActions::Pressed(Action::Grab)) DoInteract(INTERACTABLES[m_grabFocus]);
		}
		else if (m_flipPhase != FlipPhase::None)
		{
			// 翻面中(F を押し続けている間)はハンマーを置いている=狙いも視角も動かさない(回すのは F だけ)。
			// マウス移動は読んで捨てる(光標は中心へ戻す)=翻面明けに溜まった移動量で視点が跳ばない。
			float dx, dy; ReadMouseDelta(dx, dy);
		}
		else if (m_station == Station::TestCut)
		{
			UpdateTestCutLook();			// 試し切り: 脚は固定、マウス/右スティックで見回す(準心の所を斬る)
		}
		else if (m_station != Station::Anvil)
		{
			// 金床以外の工位は固定カメラ。砥石だけはマウス左右を刃の滑りに使う(UpdatePlay→UpdateGrind が読む)。
			// それ以外(炉/水槽)は読んで捨てる=光標を中心へ戻し、戻った時に視点が跳ばない。
			// 水槽で揺すっている間はマウスの上下を刃に使う(QuenchStep→StirQuench が読む)。
			// 焼き戻し中の炉もマウス左右を刃の滑りに使う(UpdateTemper が読む)
			if (m_station != Station::Grindstone && !QuenchStirring() && !(m_station == Station::Hearth && Tempering())) { float dx, dy; ReadMouseDelta(dx, dy); }
		}
		else
		{
			UpdateMouseLook();				// 金床: 従来のFPS式受限環視(準心/rail)
		}
	}
	// カメラは KCD式に3つの固定視角へ吸着する。狙い(m_aimRail)自体は連続でハンマーは全長を動くが、
	// カメラ用の rail は現在の段(m_viewSeg)の中心へ寄せる → 視角は3段でカチッと切り替わる。
	{
		int vs = (int)(m_aimRail * NVIEW);
		if (vs < 0) vs = 0; if (vs > NVIEW - 1) vs = NVIEW - 1;
		m_viewSeg = vs;
		float railCam = (m_viewSeg + 0.5f) / (float)NVIEW;	// 段の中心(1/6, 1/2, 5/6)
		float k = tick * m_camLerpRate; if (k > 1.0f) k = 1.0f;	// 速さは F1「Cam lerp」で調整
		m_aimRailSmooth += (railCam - m_aimRailSmooth) * k;	// 停位へ滑らかに切替(リアリティ)
	}
	// 過渡中=補間カメラ / 走動=玩家目線 / 工位=FPS式受限環視カメラ(編集は STAGESETTING シーンで行う)。
	ApplyViewCamera();
	UpdateListener();	// 3D音の聞き手をカメラに合わせる(声源の左右/遠近が振り向きに追従)
	UpdateViewmodelSway(tick);	// 手に持った物がカメラの向きへ少し遅れて付いて来る(武器の揺れ。Carry.cpp)
	UpdateBarAnchor();	// 金床の砧面の高さに鉄条を自動配置


	// タイトル中は金床で鎚が自動で打ち続ける(振りかぶり→打撃→跳ね返り、火花+金床音。Title.cpp)
	if (m_state == GAME_TITLE) UpdateTitleHammer(tick);
	UpdateIntro(tick);	// SPACE 後の導入(ロゴ淡出→カメラ移動+BGM 淡出)。導入中でなければ何もしない

	// 状態ごとの処理
	switch (m_state)
	{
	case GAME_TITLE:  UpdateTitle(tick);  break;
	case GAME_PLAY:   UpdatePlay(tick);   break;
	case GAME_RESULT: UpdateResult(tick); break;
	}

	// 火花・余燼の粒子シミュ(重力/バウンド, 浮力/上昇/淡出)はどの状態でも動かす。
	//   炭がONなら炭床(m_emberPos±m_emberArea)から余燼を発生させ、その後まとめて積分する。
	if (m_coalOn)
		m_particles.EmitEmbers(XMFLOAT3(m_emberPos[0], m_emberPos[1], m_emberPos[2]),
			m_emberArea[0], m_emberArea[1], m_emberRate, m_emberRise, tick);
	// 淬火の蒸気: 沸き立ち(m_boil = 沸騰の段階と鉄の温度から UpdateBoil が決める)に比例して、刃の水面の線から湧く。
	// 結果画面へ移った後(UpdatePlay が止まる)は沸き立ちを 0 へ追従させ、消えゆく様にする。
	if (m_state != GAME_PLAY) m_boil = Lerp::Damp(m_boil, 0.0f, BOIL_FOLLOW_LAMBDA, tick);
	const float BOIL_VISIBLE_EPS = 0.001f;
	if (m_boil > BOIL_VISIBLE_EPS)
	{
		const float NORMAL_SPEED = 1.0f;
		m_particles.EmitSteamLine(m_bladeLineA, m_bladeLineB, STEAM_SPREAD, STEAM_RATE * m_boil, tick, NORMAL_SPEED);
	}
	m_particles.Update(tick, m_time);
	UpdateTestCut(tick);	// 試し切りの藁人形: 切った断片の落下(剛体)。どの状態でも動かす(切った直後に工位へ入っても落ち切る)
	UpdateWaterSim(tick);	// 水槽の水面の波(刃・泡で押し、波動方程式で広げる)
}


void SceneForge::Draw()
{
	// F3 の処理の重さ: 各ブロックを GpuProfiler::Scope で囲む(名前 = F3 に出る名前)
	{ GpuProfiler::Scope t((const char*)u8"草の踏み跡"); UpdateGrassMap(); }	// 草の踏み跡の貼图を更新(一時的に描画先を差し替え→戻す)。シーンを描き始める前に
	float firePos[3]; FireLightPos(firePos);
	LitRender::SetFire(firePos, FireLightScale());	// 炉の火(揺れた位置 + 明るさ)= このフレームの描画で使う全員がここから読む
	// 反射プローブ: まだ撮っていなければ(=開始の最初のフレーム)、または F1「Recapture」で撮る。本描画の前に(描画先を一時的に差し替える)
	if (!ReflectionProbe::HasCapture() || ReflectionProbe::ConsumeRecaptureRequest())
	{
		GpuProfiler::Scope t((const char*)u8"反射の写真（撮り直し）");
		CaptureReflectionProbe();
	}
	UpdateProbeRoomBox();	// ボックスプロジェクションの部屋の箱(家を動かしても追従。変わった時だけ GPU へ)
    CottageRender::ClearExterior();
	ApplyViewCamera();	// Update と同じ規約でDrawでも適用(GetViewの前に)
	{ GpuProfiler::Scope t((const char*)u8"場面（太陽の影を含む）"); DrawModelsTest(); }	// 先に不透明な3Dモデル(金床)を描く
	{ GpuProfiler::Scope t((const char*)u8"試し切りの藁人形"); DrawTestCut(); }	// 藁人形と切れて落ちた断片(不透明。水・体積光より前)
	ReflectionProbe::DrawDebugBall(GetObj<CameraBase>("Camera"));	// F1「Show probe ball」: 撮れた画像を映す鏡の球
	ReflectionProbe::DrawRoomBox(GetObj<CameraBase>("Camera"));		// F1「Show room box」: ボックスプロジェクションの部屋の箱(橙の線)
	// 鉄は置かれた工位に描く。運んでいる間は手のビューモデルとして最後に描く(DrawViewmodel。めり込み防止)。
	// 拍子表の再生中は普通に描く(置く時、炉の中などへ入って行く鉄が道具に正しく隠れる様に)。
	if (!m_carrying || SequencePlaying())
	{
		GpuProfiler::Scope t((const char*)u8"刀身");
		if (m_wpOk) DrawWeapon();	// Blender武器モデルを進捗でモーフ(あれば優先)
		else        Draw3DBillet();	// 無ければ従来の高さ場メッシュ
		if (m_wpOk && !m_carrying) DrawGhostTarget();	// 実体の後に完成形の半透明ゴーストを重ねる
	}
	{ GpuProfiler::Scope t((const char*)u8"水"); DrawWater(); }		// 水槽の水面(屈折。背後のシーンを撮ってから描く=不透明の後)
	if (LitRender::Toggles().volumetric)	// 窓から差す光の筋(体積光)。不透明と水の後・粒子の前(深度を読む)。F2 で ON/OFF
	{
		GpuProfiler::Scope t((const char*)u8"光の筋（体積光）");
		XMFLOAT3 roomMin, roomMax;
		if (ReflectionProbe::RoomBox(roomMin, roomMax))	// 空気は部屋の中だけ(反射プローブと同じ箱)
			VolumetricLight::Render(GetObj<CameraBase>("Camera"), g_pPost ? g_pPost->GetSceneRT() : nullptr, GetObj<DepthStencil>("DSV"), roomMin, roomMax);
	}
	if (DebugUI::IsVisible()) DrawDebugBoxes();	// F1中はAABB/箱を線で表示
	if (DebugUI::IsVisible() || m_showInteract) DrawInteractBoxes();	// 互動範囲の箱 + 目線(F1 最上段「Show interact」なら F1 を閉じて歩きながらも)
	if (m_showShadowRange) DrawShadowRange();	// 太陽の影の範囲(F1 最上段「Show shadow range」。水色の正方形。壁越しに透視)
	if (m_showCollision) DrawCollision();	// 衝突形状(F1 最上段「Show collision」で ON。壁越しに透視。F1を閉じて歩きながらも見られる)

	{ GpuProfiler::Scope t((const char*)u8"火の粉・蒸気"); DrawEmbers(); DrawSteam(); }	// 炭火から立ち上る余燼(火花描画より前に) / 淬火の蒸気
	DrawGrindGlints();	// 研ぎの区域が仕上がった瞬間の光芒(刃の手前に加算で重ねる)
	DrawSwordTrail();	// 試し切りの刀光(加算。剣の後ろに残る光の帯。TestCut.cpp)
	{ GpuProfiler::Scope t((const char*)u8"手に持った物"); DrawViewmodel(); }	// 手に持った鉄+火钳(深度範囲を詰めて手前に。運んでいない時は何もしない。Carry.cpp)

	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	VertexShader* vs = GetObj<VertexShader>("VS_Forge");
	PixelShader*  ps = GetObj<PixelShader>("PS_Spark");	// 線は式で描く(ぼやけない)
	if (!pCamera || !vs || !ps || !m_mesh) return;

	XMFLOAT3 camPos = pCamera->GetPos();
	XMVECTOR vcam = XMLoadFloat3(&camPos);

	XMFLOAT4X4 cam[2];
	cam[0] = pCamera->GetView();
	cam[1] = pCamera->GetProj();
	vs->WriteBuffer(0, cam);

	// 速度方向に伸びたストリーク(火花の線)。uv.x = 横(0..1) / uv.y = 長手(0 = 頭=進む先, 1 = 尾)。PS_Spark がこれで線を描く。
	const float STREAK_BASE_LEN  = 0.6f;	// 止まっている時の半長(大きさに対する比)
	const float STREAK_SPEED_LEN = 0.12f;	// 速さ 1m/秒あたり伸びる半長(〃)=速いほど長い線
	const float STREAK_HALF_W    = 0.5f;	// 半幅(〃)。芯は細く PS が描くので、光の裾が入る幅を取る
	const float STILL_SPEED      = 0.001f;	// これ未満は向きが決まらない=上向きとみなす
	int v = 0;
	auto addStreak = [&](const Particles::Particle& s, const XMFLOAT4& col) -> bool
	{
		if (v + 6 > (int)m_vtx.size()) return false;
		XMVECTOR c = XMLoadFloat3(&s.pos);
		XMVECTOR vel = XMLoadFloat3(&s.vel);
		float speed = XMVectorGetX(XMVector3Length(vel));
		XMVECTOR dir = (speed > STILL_SPEED) ? XMVector3Normalize(vel) : XMVectorSet(0, 1, 0, 0);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));	// カメラへ向く
		XMVECTOR side = XMVector3Cross(dir, toCam);
		if (XMVectorGetX(XMVector3Length(side)) < STILL_SPEED) side = XMVectorSet(1, 0, 0, 0);
		side = XMVector3Normalize(side);
		XMVECTOR L = XMVectorScale(dir,  s.size * (STREAK_BASE_LEN + speed * STREAK_SPEED_LEN));
		XMVECTOR W = XMVectorScale(side, s.size * STREAK_HALF_W);

		XMFLOAT3 tl, tr, bl, br;
		XMStoreFloat3(&tl, XMVectorSubtract(XMVectorAdd(c, L), W));
		XMStoreFloat3(&tr, XMVectorAdd(XMVectorAdd(c, L), W));
		XMStoreFloat3(&bl, XMVectorSubtract(XMVectorSubtract(c, L), W));
		XMStoreFloat3(&br, XMVectorAdd(XMVectorSubtract(c, L), W));
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

	// 打撃/研ぎの火花 = 飛び散る酸化スケールの破片: 白黄 → 橙 → 赤、消えるほど暗く
	const float COOL_DOWN_AT = 0.5f;	// 寿命の残りがこの割合を切ると橙→赤へ冷える
	for (const Particles::Particle& s : m_particles.Sparks())
	{
		const float t = s.life / s.maxLife;		// 1→0
		const float br = t * t;
		const XMFLOAT4 col = (t > COOL_DOWN_AT) ? XMFLOAT4(1.0f, 0.9f * br + 0.1f, 0.5f * br, 1.0f)
		                                        : XMFLOAT4(1.0f * br, 0.35f * br, 0.05f * br, 1.0f);
		if (!addStreak(s, col)) break;
	}
	// 燃える鋼の火花 = 鋼の中の炭素が燃える: 白く(橙にならない)、短く、途中で枝分かれする(Particles)。
	//   色で打撃の火花と見分けられる様にする(ユーザー要望 2026-10-07: 過熱と打撃の火花が区別できない)。
	const XMFLOAT3 BURN_COLOR(1.0f, 0.97f, 0.88f);	// ほぼ白(わずかに暖色)
	for (const Particles::Particle& s : m_particles.BurnSparks())
	{
		const float t = s.life / s.maxLife;
		if (!addStreak(s, XMFLOAT4(BURN_COLOR.x * t, BURN_COLOR.y * t, BURN_COLOR.z * t, 1.0f))) break;
	}

	if (v == 0) return;

	// 加算合成・深度書き込みなしで描画
	SetBlendMode(BLEND_ADD);
	SetDepthTest(DEPTH_ENABLE_TEST);
	m_mesh->Write(m_vtx.data());
	vs->Bind();
	ps->Bind();
	m_mesh->Draw(v);

	// 状態を戻す
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}


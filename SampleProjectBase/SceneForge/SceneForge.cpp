#include "SceneForge.h"
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

//--- 武器モーフ用シェーダー(頂点色=熱色 + 簡易ライティング + 程序化の黒い酸化スケール)。
//    KCD風: 熱い鋼の表面に黒い酸化皮(スケール)の斑が乗る。UV不要=世界座標の値ノイズで生成(核显向け)。
static const char* g_wpVS = R"EOT(
cbuffer Cam : register(b0){ float4x4 view; float4x4 proj; };
struct VIN  { float3 pos:POSITION0; float3 nrm:NORMAL0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.nrm=v.nrm; o.col=v.col; o.wp=v.pos; return o; }
)EOT";
static const char* g_wpPS = R"EOT(
struct PIN{ float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; };
float  h21(float2 p){ return frac(sin(dot(p,float2(41.3,289.1)))*43758.5453); }
float  vnoise(float2 p){ float2 i=floor(p),f=frac(p); f=f*f*(3.0-2.0*f);
  float a=h21(i),b=h21(i+float2(1,0)),c=h21(i+float2(0,1)),d=h21(i+float2(1,1));
  return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y); }
float4 main(PIN i):SV_TARGET{
  float3 n = normalize(i.nrm);
  float3 l = normalize(float3(0.35,0.85,-0.4));
  float d = 0.5 + 0.5*saturate(dot(n,l));                 // 発光ベース+ライトで立体感
  // 黒い酸化スケール: 刃長(z)方向に伸ばした斑。2オクターブの安価な値ノイズ。
  float2 uv = float2(i.wp.z*6.0, i.wp.x*11.0);
  float s = vnoise(uv)*0.6 + vnoise(uv*3.1 + 7.0)*0.4;
  float scale = lerp(0.28, 1.0, smoothstep(0.34, 0.72, s)); // 暗い酸化斑
  return float4(i.col.rgb * d * scale, i.col.a);
}
)EOT";

//--- 目標ゴースト用PS: g_wpVSを流用し、氷のような半透明+輪郭(菲涅尔風)で「完成形」を薄く示す。
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
	m_sparks.reserve(MAX_SPARKS);
	m_vtx.resize(MAX_SPARKS * 6);

	// 火花/余燼用パーティクルシェーダー(.hlsl → fxc → .cso をLoad)
	VertexShader* vs = CreateObj<VertexShader>("VS_Forge");
	if (FAILED(vs->Load("Assets/Shader/VS_Particle.cso")))
		MessageBox(nullptr, "VS_Particle.cso", "Shader Error", MB_OK);
	PixelShader* ps = CreateObj<PixelShader>("PS_Forge");
	if (FAILED(ps->Load("Assets/Shader/PS_Particle.cso")))
		MessageBox(nullptr, "PS_Particle.cso", "Shader Error", MB_OK);

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

	// 鉄板を一様な厚板(=進捗0)・無傷で初期化。BuildTargetが目標高さ場を作る。
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j) { m_h[i][j] = m_hStart; m_dmgF[i][j] = 0.0f; }
	BuildTarget();

	// --- 【3D化テスト】鍛冶素材モデルの読み込み ---
	VertexShader* mvs = CreateObj<VertexShader>("VS_ForgeObj");
	if (FAILED(mvs->Load("Assets/Shader/VS_Object.cso")))
		MessageBox(nullptr, "VS_Object.cso", "Shader Error", MB_OK);
	PixelShader* mps = CreateObj<PixelShader>("PS_ForgeObj");
	if (FAILED(mps->Load("Assets/Shader/PS_TexTint.cso")))
		MessageBox(nullptr, "PS_TexTint.cso", "Shader Error", MB_OK);

	// --- 3D鉄条メッシュ用シェーダーと動的メッシュ ---
	VertexShader* bvs = CreateObj<VertexShader>("VS_Bar");
	bvs->Compile(g_barVS);
	PixelShader* bps = CreateObj<PixelShader>("PS_Bar");
	bps->Compile(g_barPS);

	// --- 武器モーフ用シェーダー(pos/normal/col, 簡易ライティング) + FBX各段の読込 ---
	VertexShader* wpvs = CreateObj<VertexShader>("VS_Wp"); wpvs->Compile(g_wpVS);
	PixelShader*  wpps = CreateObj<PixelShader>("PS_Wp");  wpps->Compile(g_wpPS);
	PixelShader*  gps  = CreateObj<PixelShader>("PS_Ghost"); gps->Compile(g_ghostPS);
	LoadWeaponStages();

	// 光る炭ベッド用シェーダー(pos/uv/col レイアウト + テクスチャ)。
	// 実行時Compileではなく、正規に .hlsl → fxc → .cso をLoadする(VS_Object等と同じ流儀)。
	VertexShader* cvs = CreateObj<VertexShader>("VS_Coal");
	if (FAILED(cvs->Load("Assets/Shader/VS_Coal.cso")))
		MessageBox(nullptr, "VS_Coal.cso", "Shader Error", MB_OK);
	PixelShader* cps = CreateObj<PixelShader>("PS_Coal");
	if (FAILED(cps->Load("Assets/Shader/PS_Coal.cso")))
		MessageBox(nullptr, "PS_Coal.cso", "Shader Error", MB_OK);

	// 水面用ピクセルシェーダー(屈折)。頂点は VS_Coal を流用(pos/uv/col レイアウト)。
	PixelShader* wps = CreateObj<PixelShader>("PS_Water");
	if (FAILED(wps->Load("Assets/Shader/PS_Water.cso")))
		MessageBox(nullptr, "PS_Water.cso", "Shader Error", MB_OK);

	// ブロックメッシュ: 各セルを箱(上面+側面4=5面, 各面両面12頂点=60頂点)で描く分を確保
	m_barVtx.resize(NL * NW * 60 + 64);
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

	// 3Dハンマー
	Model* hammer = CreateObj<Model>("MdlHammer");
	hammer->Load("Assets/MM_Blacksmith_Pack/Tools/SM_BS_Hammer_1.fbx", 1.0f, false, true);
	{
		auto tex = std::make_shared<Texture>();
		if (SUCCEEDED(tex->Create("Assets/MM_Blacksmith_Pack/Tools/Textures/1024x512/T_BS_Tools_BaseColor.png")))
			hammer->SetTexture(tex);
	}

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
	const char* kForgeDir   = "Assets/MM_Blacksmith_Pack/Forges/Textures/";
	const char* kForgeStone = "Assets/MM_Blacksmith_Pack/Forges/Textures/T_Forge_1_UV1_BaseColor.PNG";

	LoadProp("StGround",   "Assets/Model/plane/plane.fbx", "Assets/Model/field/wooden-plank-textured-background-material.jpg", 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
	LoadProp("StStump",    (P+"Anvil/SM_Stump.fbx").c_str(),             kAnvilTex.c_str(), 0.60f, 0.0f, 0.0f,  0.0f, 0.0f, true);
	LoadProp("StAnvil",    (P+"Anvil/SM_Anvil.fbx").c_str(),             kAnvilTex.c_str(), 0.70f, 0.0f, 0.0f,  0.0f, 0.0f, true);
	LoadProp("StForge",    (P+"Forges/SM_BS_Forge_1.fbx").c_str(),       kForgeStone,       2.60f, 1.8f, 0.0f,  0.6f, 0.0f, true);
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

	// 炉のマテリアル別貼り分け用に、候補テクスチャを全部読んでおく
	// ※ImGui標準フォントはCJK非対応なので名前は英数字で
	struct { const char* file; const char* name; } forgeTexList[] = {
		{ "T_Forge_1_UV1_BaseColor.PNG", "UV1 Stone(blocks)" },
		{ "T_Forge_1_UV2_BaseColor.PNG", "UV2" },
		{ "T_Forge_1_UV3_BaseColor.PNG", "UV3 Firebox(ash/coal)" },
		{ "T_Forge_1_UV4_BaseColor.PNG", "UV4" },
		{ "T_Forge_1_UV3_Emissive.PNG",  "UV3 Ember(glow)" },
		{ "T_Forge_1_UV3_Combined.PNG",  "UV3 Fire(ash+glow)" },	// 灰炭+発光を合成した1枚(Bloomで光る)
	};
	for (auto& t : forgeTexList)
	{
		auto tex = std::make_shared<Texture>();
		std::string path = std::string(kForgeDir) + t.file;
		if (SUCCEEDED(tex->Create(path.c_str())))
		{
			m_forgeTex.push_back(tex);
			m_forgeTexName.push_back(t.name);
		}
	}
	// マテリアル割当を「作者が付けた材質名(MI_Forge_1_UVx)」から自動判定する。
	//   m_forgeTex index: 0=UV1石 1=UV2 2=UV3火室 3=UV4 4=UV3炭(発光) 5=UV3合成(灰炭+発光)
	if (Model* forge = GetObj<Model>("StForge"))
	{
		size_t mc = forge->GetMaterialCount();
		m_forgeMatPick.assign(mc, 0);				// 既定は石(該当なしの保険)
		for (size_t i = 0; i < mc; ++i)
		{
			std::string n = forge->GetMaterialName(i);	// 例 "MI_Forge_1_UV1"
			if      (n.find("UV1") != std::string::npos) m_forgeMatPick[i] = 0;			// 石
			else if (n.find("UV2") != std::string::npos) m_forgeMatPick[i] = 1;
			else if (n.find("UV3") != std::string::npos) m_forgeMatPick[i] = 2;			// 火室=煤けた灰石(発光は自作の炭ベッドが担当)
			else if (n.find("UV4") != std::string::npos) m_forgeMatPick[i] = 3;
		}
	}

	// --- 自作の光る炭ベッド(水平な板。両面。合成炭テクスチャを貼る) ---
	{
		float h = 1.0f;	// 単位板(±1)。実サイズはDrawCoalBedのworldで拡縮
		Vertex q[12];
		// 上向き(法線+Y)の2三角形
		Vertex a{ {-h,0,-h},{0,0},{1,1,1,1} }, b{ {h,0,-h},{1,0},{1,1,1,1} };
		Vertex c{ {-h,0, h},{0,1},{1,1,1,1} }, d{ {h,0, h},{1,1},{1,1,1,1} };
		q[0]=a; q[1]=c; q[2]=b;  q[3]=b; q[4]=c; q[5]=d;			// 表
		q[6]=a; q[7]=b; q[8]=c;  q[9]=b; q[10]=d; q[11]=c;			// 裏(カリング対策で逆巻き)
		MeshBuffer::Description cd = {};
		cd.pVtx = q; cd.vtxSize = sizeof(Vertex); cd.vtxCount = 12;
		cd.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		m_coalMesh = std::make_shared<MeshBuffer>(cd);
		// 炭テクスチャは合成済みのものを流用(m_forgeTexの最後)
		if (!m_forgeTex.empty()) m_coalTex = m_forgeTex.back();
	}

	// 編集シーンで作った配置(Assets/stage_layout.txt)を反映。無ければ上の既定のまま。
	LoadLayout();
	LoadTuning();	// F1で調整したハンマー/カメラ値(forge_tuning.txt)を復元

	Strike();	// 開始直後から火花を出す
	// ロード完了後にここで音を開始(起動途中でBGMが鳴らないように Main から移動)
	Audio::PlayLoop(Audio::BGM_MAIN, 0.40f);	// BGMは常時ループ
	Audio::PlayLoop(Audio::SE_TITLE, 0.7f);		// 起動時はタイトル状態なので専用ループ
}

void SceneForge::Uninit()
{
	SaveTuning();	// F1で調整したハンマー/カメラ値を書き出す(次回起動で復元)
	DestroyObj("VS_Forge");
	DestroyObj("PS_Forge");
	DestroyObj("VS_ForgeObj");
	DestroyObj("PS_ForgeObj");
	DestroyObj("VS_Bar");
	DestroyObj("PS_Bar");
	DestroyObj("VS_Coal");
	DestroyObj("PS_Coal");
	DestroyObj("PS_Water");
	m_coalMesh.reset();
	m_coalTex.reset();
	DestroyObj("MdlHammer");	// 金床(StAnvil)は m_props ループで破棄される
	for (auto& p : m_props) DestroyObj(p.key.c_str());
	m_props.clear();
	m_barMesh.reset();
	m_mesh.reset();
	m_glow.reset();
	m_sparks.clear();
	m_embers.clear();
	Audio::Stop(Audio::SE_TITLE);	// ゲームシーンを離れるときタイトルループを止める(BGMは継続)
	if (!m_cursorShown) { ShowCursor(TRUE); m_cursorShown = true; }	// カーソルを戻す
}

void SceneForge::Strike(float scale)
{
	// 1回叩くと火花をまとめて発生(バースト)。scaleで量と勢いを変える
	const int N = (int)(m_burst * scale);
	XMFLOAT3 origin = XMFLOAT3(0.0f, 1.0f, 0.0f);	// 金床の位置(カメラ注視点の高さ)
	for (int i = 0; i < N; ++i)
	{
		if ((int)m_sparks.size() >= MAX_SPARKS) break;
		Spark s = {};
		s.pos = origin;
		float a = frand(0.0f, 6.2832f);		// 水平方向の角度
		float elev = frand(0.25f, 1.4f);	// 上向きの仰角
		float speed = frand(2.5f, 7.0f) * m_power * scale;
		float h = cosf(elev) * speed;
		s.vel = XMFLOAT3(cosf(a) * h, sinf(elev) * speed + frand(1.0f, 3.0f), sinf(a) * h);
		s.maxLife = frand(0.5f, 1.1f);
		s.life = s.maxLife;
		s.size = frand(0.16f, 0.30f);
		m_sparks.push_back(s);
	}
}

//====================================================================
//  目標形状(剣のシルエット)
//====================================================================
//--- 完成武器(短剣)の目標「高さ場」を定義する。
//    i = 長さ方向(0=柄端タング → 切先) / j = 幅方向(中央=刃の峰)。
//    各(i,j)セルに目標高さを入れる。武器の内側=高い(峰は最も高い), 武器の外側=飛边でほぼ0。
//    起点の厚板(m_hStart)を周囲だけ叩き下げると、この高い所=武器が浮き出る(注定成形)。
void SceneForge::BuildTarget()
{
	const float flashH = 0.012f;	// 武器の外側(飛边=叩き落とす余肉)のごく薄い高さ
	for (int i = 0; i < NL; ++i)
	{
		float u = (i + 0.5f) / NL;	// セル中心 0=柄端 → 1=切先

		// この長さ位置での「武器が占める半幅の割合」wfrac(0..1) と「峰の高さ」ridgeH
		float wfrac, ridgeH;
		if (u < 0.20f)					// タング(柄の芯): 細い/厚い
		{
			wfrac  = 0.34f;
			ridgeH = 0.90f;
		}
		else if (u < 0.30f)				// 護手・刃の付け根(肩): 幅が最大に張り出す
		{
			float v = (u - 0.20f) / 0.10f;
			wfrac  = 0.34f + (0.95f - 0.34f) * v;
			ridgeH = 0.90f + (0.72f - 0.90f) * v;
		}
		else							// 刀身: 幅も高さも切先へテーパー
		{
			float v = (u - 0.30f) / 0.70f;
			wfrac  = 0.95f + (0.05f - 0.95f) * v;	// 幅広い付け根 → 尖った切先
			ridgeH = 0.72f + (0.14f - 0.72f) * v;	// 厚い付け根 → 薄い切先
		}

		for (int j = 0; j < NW; ++j)
		{
			float cv = fabsf((j + 0.5f) / NW - 0.5f) * 2.0f;	// セル中心 0=中央 .. 1=端
			float h;
			if (cv <= wfrac)
			{
				// 武器の内側: 中央(峰)が高く、刃の縁に向かって薄くなる断面
				float e = cv / (wfrac > 1e-4f ? wfrac : 1.0f);	// 0=峰 .. 1=刃縁
				float bevel = 1.0f - 0.75f * e;					// 峰1.0 → 刃縁0.25
				h = ridgeH * bevel;
			}
			else h = flashH;	// 武器の外側=飛边

			m_hTgt[i][j] = h * m_hStart;	// 倍率を実寸へ
		}
	}

	// 体積守恒: 目標の総体積を鉄坯(=全セルhStart)の総体積に一致させる。
	// これで「料を再分配するだけで目標に到達できる」ことが保証される(過不足なし)。
	float sumT = 0.0f;
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j) sumT += m_hTgt[i][j];
	float sumS = (float)(NL * NW) * m_hStart;
	if (sumT > 1e-6f)
	{
		float k = sumS / sumT;
		for (int i = 0; i < NL; ++i)
		for (int j = 0; j < NW; ++j) m_hTgt[i][j] *= k;
	}
}

//--- 成形の一致度(0..1)。現在の高さ場と目標高さ場の差(L1)が小さいほど100%へ。
//    体積守恒なので、料を目標どおりに再分配できているほど誤差が減る。
float SceneForge::ShapeMatch() const
{
	float err = 0.0f;
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j) err += fabsf(m_h[i][j] - m_hTgt[i][j]);
	// 誤差の基準: 全セルが「起点(hStart)」にある初期状態の誤差。ここから0へ近づく。
	float ref = 0.0f;
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j) ref += fabsf(m_hStart - m_hTgt[i][j]);
	if (ref < 1e-6f) return 1.0f;
	float m = 1.0f - err / ref;
	if (m < 0.0f) m = 0.0f; if (m > 1.0f) m = 1.0f;
	return m;
}

//====================================================================
//  ゲーム進行
//====================================================================
void SceneForge::StartGame()
{
	Audio::Stop(Audio::SE_TITLE);	// タイトル専用ループを止める(BGMは継続)
	m_state    = GAME_PLAY;
	m_score    = 0;
	m_heat     = 0.0f;
	for (int i = 0; i < NL; ++i)
	for (int j = 0; j < NW; ++j) { m_h[i][j] = m_hStart; m_dmgF[i][j] = 0.0f; }	// 厚板に戻す
	BuildTarget();
	m_forgeProg = 0.0f;		// 武器モーフを未打磨(stage_0)に戻す
	for (int s = 0; s < NSEG; ++s) m_segProg[s] = 0.0f;	// 各区域を未成形に
	m_match = 0.0f;

	m_charging    = false;
	m_charge      = 0.0f;
	m_strikeCD    = 0.0f;
	m_hammerVel   = 0.0f;
	m_hammerLift  = HAMMER_REST_LIFT;
	m_aimI = NL / 2; m_aimJ = NW / 2; m_aimSeg = 0; m_aimValid = false;
	m_aimWorld = m_barAnchor;	// 最初の有効照準までのハンマー既定位置(板中心)
	m_lookYaw = 0.0f; m_lookPitch = 0.0f;
	m_canStrike   = false;		// SPACEを一度離すまで蓄力しない
	m_shake        = 0.0f;
	m_popupLife    = 0.0f;
	m_sinceStrike  = 999.0f;	// 最初の一打はリズム対象外
	m_rhythmStreak = 0;
	m_sizzleTimer  = 0.0f;
	m_qualitySum   = 0.0f;
	m_strikeCount  = 0;
	m_spoil        = 0.0f;		// 廃件率リセット
}

void SceneForge::FinishGame()
{
	m_state = GAME_RESULT;
}

void SceneForge::GameOverGame()
{
	m_state = GAME_OVER;		// 廃件(失敗)。分数はそのまま結果画面で見せる
}

//--- タイトル: 雰囲気で自動的に火花を出しつつ、SPACEで開始
void SceneForge::UpdateTitle(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE) && !m_fade.IsBusy())
	{
		m_fade.Transition([this] { StartGame(); });	// 黒転じでゲーム開始(淡入淡出)
		return;		// 開始したフレームでは叩かない
	}
}

//--- 鍛造中
void SceneForge::UpdatePlay(float tick)
{
	// F1(デバッグUI)を開いている間、および画面フェード(遷移)中はゲーム入力を凍結する。
	bool inputOn = !DebugUI::IsVisible() && !m_fade.IsBusy();

	// Pキー: 瞄準区域の可視化トグル(デバッグ用。既定OFF=KCD式に「叩く場所」を示さない)
	if (inputOn && IsKeyTrigger('P')) m_showAimHi = !m_showAimHi;
	if (inputOn && IsKeyTrigger('G')) m_showGhost = !m_showGhost;	// 目標ゴースト表示切替

	// --- 加熱: R長押しで炉で加熱 / 常にゆっくり自然冷却 ---
	if (inputOn)
	{
		if (IsKeyPress('R')) m_heat += HEAT_RATE * tick;
		m_heat -= m_coolRate * tick;
		if (m_heat < 0.0f) m_heat = 0.0f;
		if (m_heat > 1.0f) m_heat = 1.0f;
	}

	// --- 過熱で放置すると鋼全体が焼けていく(損傷が蓄積)＋ジュー音 ---
	if (m_heat > OVERHEAT)
	{
		for (int i = 0; i < NL; ++i)
		for (int j = 0; j < NW; ++j)
		{
			m_dmgF[i][j] += BURN_RATE * tick;
			if (m_dmgF[i][j] > 1.0f) m_dmgF[i][j] = 1.0f;
		}
		m_sizzleTimer -= tick;
		if (m_sizzleTimer <= 0.0f) { Audio::Play(Audio::SE_SIZZLE); m_sizzleTimer = 0.22f; }
	}
	else m_sizzleTimer = 0.0f;

	// --- 打撃テンポの計測(前回打撃からの経過時間) ---
	m_sinceStrike += tick;
	// 長く止まっていたらリズムはリセット(遅すぎ)
	if (m_sinceStrike > CADENCE_MAX) m_rhythmStreak = 0;

	// --- 照準(FPS方式): 画面中心の準心=カメラ正前方の射線を板と交差させ、当たったセルを求める ---
	//   マウス移動はUpdateMouseLook(Updateの先頭)で視角に累積済み。ApplyCameraがm_camFwdを更新。
	if (inputOn) UpdateAim();

	// --- 蓄力ハンマー: 左クリック押しっぱなしで蓄力、離すと打撃。打撃後はクールダウン ---
	if (m_strikeCD > 0.0f) m_strikeCD -= tick;	// クールダウン消化

	if (!inputOn)
	{
		// F1操作中は蓄力をキャンセル(暴発しないように)
		m_charging = false;
		m_charge   = 0.0f;
	}
	// 開始直後の誤爆防止(一度ボタンを離すまで蓄力しない)
	else if (!m_canStrike)
	{
		if (!IsKeyPress(VK_LBUTTON)) m_canStrike = true;
	}
	else if (m_strikeCD <= 0.0f && IsKeyPress(VK_LBUTTON))
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
		float sum = 0.0f; for (int s = 0; s < NSEG; ++s) sum += m_segProg[s];
		m_forgeProg = sum / NSEG;	// 全体進捗(表示・モーフのプレビュー用)
		m_match = m_forgeProg;
	}
	else m_match = ShapeMatch();

	// --- ハンマーの上下: 真の弾簧-阻尼(spring-damper)物理 ---
	// 自然長 HAMMER_REST_LIFT のバネに質量 m の錘が付く模型(老師の SceneSpring と同じ流儀)。
	// 打撃(DoStrike)の瞬間に錘を接触位置(lift=0)まで沈め、上向きの初速 v0=J/m を与える。
	// 以後は毎フレーム、老師の手順どおりに合力→加速度→速度→位置を積分するだけ:
	//   ・張力(復元力) = -k * (現在位置 - 自然長)      … フックの法則
	//   ・抵抗力(阻尼) = -c * 速度                       … 速度比例の減衰
	//   ・合力 = 張力 + 抵抗力  (重力は静止高に折込み済みなので単列しない)
	// 上死点を越える過冲(overshoot)も静止高への収束も、係数 k/c/m から自動的に生まれる
	// =手描きの sin 曲線を廃止。蓄力中だけは手で保持する(離した後にバネが働く)。
	if (m_charging)
	{
		m_hammerLift = HAMMER_REST_LIFT + m_charge * HAMMER_CHARGE_RAISE;
		m_hammerVel  = 0.0f;	// 手で保持=速度ゼロ
	}
	else
	{
		float dt = tick; if (dt > 0.033f) dt = 0.033f;	// 大コマ落ち時の発散防止(上限クランプ)
		float tension    = -HAMMER_STIFFNESS * (m_hammerLift - HAMMER_REST_LIFT);	// 張力(復元)
		float resistance = -HAMMER_DAMPING   *  m_hammerVel;						// 抵抗力(阻尼)
		float accel      = (tension + resistance) / HAMMER_MASS;					// 合力→加速度
		m_hammerVel  += accel * dt;			// 速度に加速度を加える(セミ暗黙オイラー=安定)
		m_hammerLift += m_hammerVel * dt;	// 速度から位置を移動
	}

	// --- ハンマーの横位置を平滑追従: 準心が格子単位で跳ぶのを Lerp::Damp で滑らかに ---
	// 目標は現在の照準点(m_aimWorld)＋既定オフセット。Draw はこの m_hammerPos を読む。
	{
		DirectX::XMFLOAT3 tgt = {
			m_aimWorld.x + m_hammerOff[0],
			0.0f,							// y は使わない(高さは m_hammerLift のアニメで別途)
			m_aimWorld.z + m_hammerOff[2],
		};
		if (!m_hammerPosInit) { m_hammerPos = tgt; m_hammerPosInit = true; }	// 起動時は瞬間セット
		m_hammerPos = Lerp::Damp(m_hammerPos, tgt, m_hammerFollow, tick);
	}

	// --- フィードバックの減衰 ---
	if (m_shake > 0.0f)     { m_shake -= tick * 3.0f; if (m_shake < 0.0f) m_shake = 0.0f; }
	if (m_popupLife > 0.0f) m_popupLife -= tick;

	// --- 淬火(仕上げ): Qでいつでも完成にできる。一致度と損傷で品質が決まる ---
	if (inputOn && IsKeyTrigger('Q')) FinishGame();

	// 武器モーフ使用時: 全区域が到位したら自動で完成(淬火)へ。
	if (m_wpOk)
	{
		bool allDone = true;
		for (int s = 0; s < NSEG; ++s) if (m_segProg[s] < SEG_DONE) { allDone = false; break; }
		if (allDone) FinishGame();
	}
}

//--- 蓄力を解放して1打: 変形＋フィードバック
void SceneForge::DoStrike()
{
	float power = m_charge;			// 0..1
	bool cold = (m_heat < COLD_LIMIT);
	bool over = (m_heat > OVERHEAT);

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
		m_hammerLift = 0.0f;
		m_hammerVel  = HAMMER_IMPULSE / HAMMER_MASS;
		Audio::Play(Audio::SE_SWING, 0.7f);	// 空を切る「ヒュッ」(鉄に当たっていない合図。文字は出さない)
		return;
	}

	// 温度係数(冷たい→ほぼ効かない, 過熱→効くが品質悪, 適温→最大)
	float heatFactor = cold ? HEAT_EFF_COLD : (over ? HEAT_EFF_OVER : 1.0f);
	// 引导式流动(体積守恒 + 結果注定): 命中セルは「目標高さ」までしか下げない(削り過ぎない)。
	// 押し出した料は「設計がより料を欲しがっている(=e が小さい)隣」へ多く流す。
	//   e = 現在高 - 目標高 (>0=余り/削るべき, <0=不足/盛るべき)。料は e の高→低へ流れる。
	// これで叩くほど形は設計の目標へ収束し、余肉(飛边)は不足部(刃身/峰)へ引かれる=可控。
	float want = FLOW_DROP * power * heatFactor * grooveMult;
	int ci = m_aimI, cj = m_aimJ;
	float eC = m_h[ci][cj] - m_hTgt[ci][cj];	// 命中セルの余り(surplus)
	float delta = want;
	if (delta > eC) delta = eC;					// 目標より下げない=結果が壊れない
	if (delta < 0.0f) delta = 0.0f;				// 既に目標以下=削る余肉なし(空砕き)

	if (delta > 0.0f)
	{
		// 板の内側にある4近傍(端では隣が減る=料は板から出ない)
		int ni[4], nj[4], n = 0;
		if (ci > 0)      { ni[n] = ci - 1; nj[n] = cj;     ++n; }
		if (ci < NL - 1) { ni[n] = ci + 1; nj[n] = cj;     ++n; }
		if (cj > 0)      { ni[n] = ci;     nj[n] = cj - 1; ++n; }
		if (cj < NW - 1) { ni[n] = ci;     nj[n] = cj + 1; ++n; }
		if (n > 0)
		{
			// 各隣の重み = max(0, eC - eK): 命中セルより「不足寄り(e が小)」の隣ほど多く受ける。
			float w[4], wsum = 0.0f;
			for (int k = 0; k < n; ++k)
			{
				float eK = m_h[ni[k]][nj[k]] - m_hTgt[ni[k]][nj[k]];
				float ww = eC - eK;
				if (ww < 0.0f) ww = 0.0f;
				w[k] = ww;
				wsum += ww;
			}
			if (wsum < 1e-6f) { for (int k = 0; k < n; ++k) w[k] = 1.0f; wsum = (float)n; }	// 全隣が余り側→均等
			m_h[ci][cj] -= delta;				// 命中セルは目標へ近づく
			for (int k = 0; k < n; ++k)			// 押し出した料を不足寄りの隣へ流す
				m_h[ni[k]][nj[k]] += delta * (w[k] / wsum);
		}
	}

	// 冷打=割れ / 過熱打=焼け として、命中セルに損傷を刻む
	float dmgAdd = cold ? DMG_COLD_HIT : (over ? DMG_OVER_HIT : 0.0f);
	if (dmgAdd > 0.0f)
	{
		m_dmgF[ci][cj] += dmgAdd;
		if (m_dmgF[ci][cj] > 1.0f) m_dmgF[ci][cj] = 1.0f;
	}

	// KCD式: 「叩く場所」は指示しない。玩家が「まだ出来ていない区域」を自分で見つけて叩く。
	//   照準セル → 区域番号。その区域が既に完成していれば無用打撃(=誤り)。
	int  seg     = AimSeg();
	bool segDone = (m_segProg[seg] >= SEG_DONE);
	bool goodHit = !cold && !over && !segDone;
	// 良い打撃だけ、その区域の進捗を前進させる(只進不退)。全区域到位で完成。
	if (goodHit)
	{
		// FORGE_STEPは「全体で何%進むか」の値。区域は1/NSEGの長さなので×NSEGして、
		// 1区域あたりの必要打数を旧・全体と同程度に保つ(分区域化で5倍にならないように)。
		m_segProg[seg] += FORGE_STEP * NSEG * power * grooveMult;
		if (m_segProg[seg] > 1.0f) m_segProg[seg] = 1.0f;
	}

	// 温度が下がる / 火花 / 振動
	m_heat -= STRIKE_COOL;
	if (m_heat < 0.0f) m_heat = 0.0f;
	float sparkScale = (0.4f + power * 1.2f) * (over ? 0.7f : 1.0f);
	if (!cold) Strike(sparkScale);

	// 打撃音: 冷打は鈍い音。通常打撃は金床音を 1→2→1→2 と交互に鳴らす
	if (cold) Audio::Play(Audio::SE_COLD, 0.9f);
	else
	{
		Audio::Play(m_hammerAlt ? Audio::SE_ANVIL2 : Audio::SE_ANVIL1, 0.55f + power * 0.45f);
		m_hammerAlt = !m_hammerAlt;
	}
	// 冷打は「ガツン」と大きく揺れる(手応えが悪い=衝撃だけ大きい)
	m_shake = cold ? (0.6f + power * 0.6f) : (0.3f + power * 0.7f);
	// 打撃=錘を接触位置(lift=0)まで沈め、反発の上向き初速をバネに与える(以後は物理で跳ね返る)
	m_hammerLift = 0.0f;
	m_hammerVel  = HAMMER_IMPULSE / HAMMER_MASS;

	// 評価 & 廃件率: KCD式に「指示せず、誤りだけ知らせる」。負向フィードバックは日本語(主人公の独白)。
	//   廃件率は不可逆(減らない)。過熱/冷打/完成済みの区域を叩く=誤り→廃件率↑。
	const char* label; unsigned int col; float quality = 0.0f;
	// u8"" は C++20 では char8_t。ImGuiはUTF-8バイトを要求するので(const char*)へ再解釈する。
	if (cold)         { label = (const char*)u8"まだ冷たい…赤くなるまで熱して"; col = IM_COL32(120, 170, 255, 255); m_spoil += SPOIL_COLD; }
	else if (over)    { label = (const char*)u8"熱しすぎだ！鋼が焼ける";       col = IM_COL32(255, 120, 120, 255); m_spoil += SPOIL_BURN; }
	else if (segDone) { label = (const char*)u8"ここはもう完成済みだ";         col = IM_COL32(255, 200,  90, 255); m_spoil += SPOIL_WASTE; }
	else	// 適温 & 未完成の区域に命中 = 成功。得点のみ(廃件率は回復しない)
	{
		if      (power > POWER_PERFECT) { label = "PERFECT!"; col = IM_COL32(255, 220, 120, 255); quality = QUALITY_PERFECT; }
		else if (power > POWER_GOOD)    { label = "GOOD";     col = IM_COL32(180, 255, 150, 255); quality = QUALITY_GOOD; }
		else                            { label = "WEAK";     col = IM_COL32(200, 200, 200, 255); quality = QUALITY_WEAK; }
		if (inGroove) { quality += GROOVE_QUALITY_BONUS; Audio::Play(Audio::SE_WHISTLE, 0.5f); }	// テンポで口笛
		m_qualitySum += quality;
		m_score += (int)(quality * SCORE_PER_QUALITY);
	}
	m_strikeCount++;
	if (m_spoil > 1.0f) m_spoil = 1.0f;	// 上限のみ(下限クランプ不要=減らないので)

	// ポップアップ表示
	if (inGroove && quality > 0.0f) sprintf_s(m_popupText, sizeof(m_popupText), "%s  (in rhythm)", label);
	else                            strcpy_s(m_popupText, sizeof(m_popupText), label);
	m_popupLife = POPUP_LIFE;
	m_popupCol  = col;

	// 廃件槽が満ちたら失敗 → GameOver
	if (m_spoil >= 1.0f) GameOverGame();
}

//--- 結果: SPACEでタイトルへ戻る
void SceneForge::UpdateResult(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE) && !m_fade.IsBusy())
	{
		m_fade.Transition([this] {
			m_state = GAME_TITLE;
			Audio::PlayLoop(Audio::SE_TITLE, 0.7f);	// タイトルへ戻ったので専用ループ再開
		});
	}
}

//--- 廃件(失敗): SPACEでタイトルへ戻る(=もう一度挑戦)
void SceneForge::UpdateGameOver(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE) && !m_fade.IsBusy())
	{
		m_fade.Transition([this] {
			m_state = GAME_TITLE;
			Audio::PlayLoop(Audio::SE_TITLE, 0.7f);
		});
	}
}

void SceneForge::Update(float tick)
{
	m_time += tick;
	m_fade.Update(tick);	// 画面フェード(黒幕)を進める。遷移はTransitionの黒転じで実行される
	// PLAY中かつF1非表示のときだけ、マウスをFPS式に視角へ累積(ApplyCameraより先に)。
	if (m_state == GAME_PLAY && !DebugUI::IsVisible() && !m_fade.IsBusy()) UpdateMouseLook();
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
	// デバッグUI表示中はカメラ固定を外し、DCCの自由カメラ(ALT+ドラッグでオービット)を許可。
	// 非表示時(=プレイ中)はKCD風の固定カメラに上書きする。
	ApplyCamera();		// FPS式受限環視カメラ(編集は STAGESETTING シーンで行う)
	UpdateBarAnchor();	// 金床の砧面の高さに鉄条を自動配置

	// PLAY中はOSカーソルを隠す(照準は光るセグメントで示す)。デバッグUI表示中は出す
	bool wantCursor = (m_state != GAME_PLAY) || DebugUI::IsVisible();
	if (wantCursor != m_cursorShown) { ShowCursor(wantCursor); m_cursorShown = wantCursor; }

	// タイトル中は雰囲気用に自動で火花を出す
	if (m_state == GAME_TITLE)
	{
		m_autoTimer += tick;
		if (m_autoTimer >= TITLE_INTERVAL) { Strike(); m_autoTimer = 0.0f; }
	}

	// 状態ごとの処理
	switch (m_state)
	{
	case GAME_TITLE:  UpdateTitle(tick);  break;
	case GAME_PLAY:   UpdatePlay(tick);   break;
	case GAME_RESULT: UpdateResult(tick); break;
	case GAME_OVER:   UpdateGameOver(tick); break;
	}

	// 火花シミュレーション(重力＋地面バウンド)はどの状態でも動かす
	for (size_t i = 0; i < m_sparks.size(); )
	{
		Spark& s = m_sparks[i];
		s.life -= tick;
		if (s.life <= 0.0f)
		{
			s = m_sparks.back();
			m_sparks.pop_back();
			continue;
		}
		s.vel.y -= GRAVITY * tick;				// 重力
		s.pos.x += s.vel.x * tick;
		s.pos.y += s.vel.y * tick;
		s.pos.z += s.vel.z * tick;
		if (s.pos.y < 0.0f && s.vel.y < 0.0f)	// 地面で弾む
		{
			s.pos.y = 0.0f;
			s.vel.y = -s.vel.y * 0.3f;
			s.vel.x *= 0.6f;
			s.vel.z *= 0.6f;
		}
		++i;
	}

	// 炭火の余燼(火の粉)も常時シミュレート
	UpdateEmbers(tick);
}


void SceneForge::Draw()
{
	ApplyCamera();	// 固定カメラを適用(GetViewの前に)
	DrawModelsTest();	// 先に不透明な3Dモデル(金床)を描く
	if (m_wpOk) DrawWeapon();	// Blender武器モデルを進捗でモーフ(あれば優先)
	else        Draw3DBillet();	// 無ければ従来の高さ場メッシュ
	if (m_wpOk) DrawGhostTarget();	// 実体の後に完成形の半透明ゴーストを重ねる
	DrawWater();		// 水槽の水面(屈折。背後のシーンを撮ってから描く=不透明の後)
	if (DebugUI::IsVisible()) DrawDebugBoxes();	// F1中はAABB/箱を線で表示

	DrawEmbers();		// 炭火から立ち上る余燼(火花描画より前に。火花が無くても出す)

	CameraBase* pCamera = GetObj<CameraBase>("Camera");
	VertexShader* vs = GetObj<VertexShader>("VS_Forge");
	PixelShader*  ps = GetObj<PixelShader>("PS_Forge");
	if (!pCamera || !vs || !ps || !m_mesh) return;

	XMFLOAT3 camPos = pCamera->GetPos();
	XMVECTOR vcam = XMLoadFloat3(&camPos);

	XMFLOAT4X4 cam[2];
	cam[0] = pCamera->GetView();
	cam[1] = pCamera->GetProj();
	vs->WriteBuffer(0, cam);

	// 速度方向に伸びたストリーク(火花の線)を作る
	int v = 0;
	for (const Spark& s : m_sparks)
	{
		float t = s.life / s.maxLife;			// 1→0

		// 色：白黄 → 橙 → 赤、消えるほど暗く
		XMFLOAT4 col;
		float br = t * t;
		if (t > 0.5f) col = XMFLOAT4(1.0f, 0.9f * br + 0.1f, 0.5f * br, 1.0f);
		else          col = XMFLOAT4(1.0f * br, 0.35f * br, 0.05f * br, 1.0f);

		XMVECTOR c = XMLoadFloat3(&s.pos);
		XMVECTOR vel = XMLoadFloat3(&s.vel);
		float speed = XMVectorGetX(XMVector3Length(vel));

		XMVECTOR dir = (speed > 0.001f) ? XMVector3Normalize(vel) : XMVectorSet(0, 1, 0, 0);
		XMVECTOR toCam = XMVector3Normalize(XMVectorSubtract(vcam, c));	// カメラへ向く
		XMVECTOR side = XMVector3Cross(dir, toCam);
		if (XMVectorGetX(XMVector3Length(side)) < 0.001f) side = XMVectorSet(1, 0, 0, 0);
		side = XMVector3Normalize(side);

		float halfLen = s.size * (0.6f + speed * 0.12f);	// 速いほど長い線に
		float halfWid = s.size * 0.35f;
		XMVECTOR L = XMVectorScale(dir, halfLen);
		XMVECTOR W = XMVectorScale(side, halfWid);

		XMFLOAT3 tl, tr, bl, br3;
		XMStoreFloat3(&tl,  XMVectorSubtract(XMVectorAdd(c, L), W));
		XMStoreFloat3(&tr,  XMVectorAdd(XMVectorAdd(c, L), W));
		XMStoreFloat3(&bl,  XMVectorSubtract(XMVectorSubtract(c, L), W));
		XMStoreFloat3(&br3, XMVectorAdd(XMVectorSubtract(c, L), W));

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

	// 加算合成・深度書き込みなしで描画
	SetBlendMode(BLEND_ADD);
	SetDepthTest(DEPTH_ENABLE_TEST);
	ps->SetTexture(0, m_glow.get());
	m_mesh->Write(m_vtx.data());
	vs->Bind();
	ps->Bind();
	m_mesh->Draw(v);

	// 状態を戻す
	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}


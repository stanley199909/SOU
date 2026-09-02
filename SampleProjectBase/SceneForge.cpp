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

// 水面の屈折用に、シーンのスナップショットを撮る PostProcess を参照する(Mainのグローバル)。
extern std::shared_ptr<PostProcess> g_pPost;

// パーティクル用シェーダーは Assets/Shader/VS_Particle.cso / PS_Particle.cso として
// .hlsl から fxc でコンパイルし、Init で Load する(火花・余燼で共用)。

static float frand()                 { return (float)rand() / (float)RAND_MAX; }
static float frand(float a, float b)  { return a + (b - a) * frand(); }

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

//--- 温度(0..1)を鋼の色(float4)に変換。冷たいときも暗い金属色で見える
static DirectX::XMFLOAT4 HeatRGB(float h, float dmg)
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
	float r = sR[i] + (sR[i + 1] - sR[i]) * t;
	float g = sG[i] + (sG[i + 1] - sG[i]) * t;
	float b = sB[i] + (sB[i + 1] - sB[i]) * t;
	// 冷たくても暗い金属として見えるよう下限を設ける
	if (r < 0.22f) r = 0.22f;
	if (g < 0.20f) g = 0.20f;
	if (b < 0.20f) b = 0.20f;
	float d = 1.0f - 0.7f * dmg;	// 損傷で暗くなる
	return DirectX::XMFLOAT4(r * d, g * d, b * d, 1.0f);
}

//--- マウス位置をクライアント座標(ピクセル)で取得。ImGuiのタイミングに依存しない。
//    cw/ch はクライアント(=描画)の実サイズ。どのフレーム段階でも同じ値になる。
static void GetMouseClient(float& mx, float& my, float& cw, float& ch)
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

	// --- シーン装飾: 編集シーン(StageSetting)と同じ道具一式・同じキー(St...)で読み込む ---
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

	Strike();	// 開始直後から火花を出す
	// ロード完了後にここで音を開始(起動途中でBGMが鳴らないように Main から移動)
	Audio::PlayLoop(Audio::BGM_MAIN, 0.40f);	// BGMは常時ループ
	Audio::PlayLoop(Audio::SE_TITLE, 0.7f);		// 起動時はタイトル状態なので専用ループ
}

void SceneForge::Uninit()
{
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
	m_progress = 0.0f;
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
	m_strikeAnim  = 0.0f;
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
	if (IsKeyTrigger(VK_SPACE))
	{
		StartGame();
		return;		// 開始したフレームでは叩かない
	}
}

//--- 鍛造中
void SceneForge::UpdatePlay(float tick)
{
	// F1(デバッグUI)を開いている間はゲーム入力を凍結する。閉じていれば通常通り。
	bool inputOn = !DebugUI::IsVisible();

	// Pキー: 瞄準区域の可視化トグル(デバッグ用。既定OFF=KCD式に「叩く場所」を示さない)
	if (inputOn && IsKeyTrigger('P')) m_showAimHi = !m_showAimHi;

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

	// --- ハンマーの上下: 蓄力で上がる / 打撃で振り下ろして戻る ---
	if (m_strikeAnim > 0.0f)
	{
		m_strikeAnim -= tick / STRIKE_ANIM_TIME;
		if (m_strikeAnim < 0.0f) m_strikeAnim = 0.0f;
		m_hammerLift = HAMMER_REST_LIFT * (1.0f - m_strikeAnim);	// 1(接触=低い)→0(元の高さ)
	}
	else if (m_charging)
	{
		m_hammerLift = HAMMER_REST_LIFT + m_charge * HAMMER_CHARGE_RAISE;	// 蓄力で持ち上がる
	}
	else
	{
		float k = tick * 8.0f; if (k > 1.0f) k = 1.0f;
		m_hammerLift += (HAMMER_REST_LIFT - m_hammerLift) * k;	// 待機高さへ緩やかに戻る
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
	float grooveMult = inGroove ? 1.3f : 1.0f;			// 変形効率の上昇

	// 準心が板の上に無いなら空振り: 変形も評価もせず、鉄には当たっていないので打鉄音も出さない
	// (冷打音は誤解のもと。清脆な打鉄音が鳴らないこと自体が「外した」合図になる)。
	if (!m_aimValid)
	{
		m_strikeAnim = 1.0f;			// 空振りのモーションだけ
		Audio::Play(Audio::SE_SWING, 0.7f);	// 空を切る「ヒュッ」(鉄に当たっていない合図。文字は出さない)
		return;
	}

	// 温度係数(冷たい→ほぼ効かない, 過熱→効くが品質悪, 適温→最大)
	float heatFactor = cold ? 0.10f : (over ? 0.7f : 1.0f);
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
	float dmgAdd = cold ? 0.35f : (over ? 0.25f : 0.0f);
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
	m_strikeAnim = 1.0f;	// ハンマーを振り下ろすアニメ開始

	// 評価 & 廃件率: KCD式に「指示せず、誤りだけ知らせる」。負向フィードバックは日本語(主人公の独白)。
	//   廃件率は不可逆(減らない)。過熱/冷打/完成済みの区域を叩く=誤り→廃件率↑。
	const char* label; unsigned int col; float quality = 0.0f;
	// u8"" は C++20 では char8_t。ImGuiはUTF-8バイトを要求するので(const char*)へ再解釈する。
	if (cold)         { label = (const char*)u8"まだ冷たい…赤くなるまで熱して"; col = IM_COL32(120, 170, 255, 255); m_spoil += SPOIL_COLD; }
	else if (over)    { label = (const char*)u8"熱しすぎだ！鋼が焼ける";       col = IM_COL32(255, 120, 120, 255); m_spoil += SPOIL_BURN; }
	else if (segDone) { label = (const char*)u8"ここはもう完成済みだ";         col = IM_COL32(255, 200,  90, 255); m_spoil += SPOIL_WASTE; }
	else	// 適温 & 未完成の区域に命中 = 成功。得点のみ(廃件率は回復しない)
	{
		if      (power > 0.85f) { label = "PERFECT!"; col = IM_COL32(255, 220, 120, 255); quality = 1.0f; }
		else if (power > 0.50f) { label = "GOOD";     col = IM_COL32(180, 255, 150, 255); quality = 0.7f; }
		else                    { label = "WEAK";     col = IM_COL32(200, 200, 200, 255); quality = 0.4f; }
		if (inGroove) { quality += 0.2f; Audio::Play(Audio::SE_WHISTLE, 0.5f); }	// テンポで口笛
		m_qualitySum += quality;
		m_score += (int)(quality * 100);
	}
	m_strikeCount++;
	if (m_spoil > 1.0f) m_spoil = 1.0f;	// 上限のみ(下限クランプ不要=減らないので)

	// ポップアップ表示
	if (inGroove && quality > 0.0f) sprintf_s(m_popupText, sizeof(m_popupText), "%s  (in rhythm)", label);
	else                            strcpy_s(m_popupText, sizeof(m_popupText), label);
	m_popupLife = 0.8f;
	m_popupCol  = col;

	// 廃件槽が満ちたら失敗 → GameOver
	if (m_spoil >= 1.0f) GameOverGame();
}

//--- 結果: SPACEでタイトルへ戻る
void SceneForge::UpdateResult(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE))
	{
		m_state = GAME_TITLE;
		Audio::PlayLoop(Audio::SE_TITLE, 0.7f);	// タイトルへ戻ったので専用ループ再開
	}
}

//--- 廃件(失敗): SPACEでタイトルへ戻る(=もう一度挑戦)
void SceneForge::UpdateGameOver(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE))
	{
		m_state = GAME_TITLE;
		Audio::PlayLoop(Audio::SE_TITLE, 0.7f);
	}
}

void SceneForge::Update(float tick)
{
	m_time += tick;
	// PLAY中かつF1非表示のときだけ、マウスをFPS式に視角へ累積(ApplyCameraより先に)。
	if (m_state == GAME_PLAY && !DebugUI::IsVisible()) UpdateMouseLook();
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

//====================================================================
//  UI (HUD)
//====================================================================

// 画面中央にウィンドウ枠なしのメッセージを出す小道具
static void CenterText(const char* text, float yRatio, float scale = 1.0f,
                       ImU32 col = IM_COL32(255, 255, 255, 255))
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 disp = ImGui::GetIO().DisplaySize;	// 実際の画面サイズ(解像度非依存)
	ImVec2 sz = ImGui::CalcTextSize(text);
	sz.x *= scale; sz.y *= scale;
	float x = (disp.x - sz.x) * 0.5f;
	float y =  disp.y * yRatio - sz.y * 0.5f;
	dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(x, y), col, text);
}

//--- 温度(0..1)を鋼の色に変換する(暗赤→赤→橙→黄→白熱)
static ImU32 HeatColor(float h, float alpha = 1.0f)
{
	static const float stopH[] = { 0.00f, 0.20f, 0.40f, 0.55f, 0.70f, 0.85f, 1.00f };
	static const float stopR[] = { 0.15f, 0.45f, 0.85f, 1.00f, 1.00f, 1.00f, 1.00f };
	static const float stopG[] = { 0.05f, 0.06f, 0.15f, 0.45f, 0.65f, 0.88f, 1.00f };
	static const float stopB[] = { 0.05f, 0.02f, 0.02f, 0.05f, 0.15f, 0.45f, 0.92f };
	const int N = 7;
	if (h <= stopH[0])   h = stopH[0];
	if (h >= stopH[N-1]) h = stopH[N-1];
	int i = 0;
	while (i < N - 1 && h > stopH[i + 1]) ++i;
	float t = (h - stopH[i]) / (stopH[i + 1] - stopH[i]);
	float r = stopR[i] + (stopR[i + 1] - stopR[i]) * t;
	float g = stopG[i] + (stopG[i + 1] - stopG[i]) * t;
	float b = stopB[i] + (stopB[i + 1] - stopB[i]) * t;
	return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(alpha * 255));
}

//--- 温度ゲージ(HUD)
void SceneForge::DrawHeatGauge()
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	ImVec2 disp = ImGui::GetIO().DisplaySize;
	const float x0 = disp.x * 0.25f;
	const float x1 = disp.x * 0.75f;
	const float y  = disp.y * 0.80f;
	const float hgt = 20.0f;
	auto lerpX = [&](float t) { return x0 + (x1 - x0) * t; };

	// トラック
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(30, 30, 34, 220), 4.0f);
	// 最適温度帯(緑の帯)
	dl->AddRectFilled(ImVec2(lerpX(IDEAL_MIN), y), ImVec2(lerpX(IDEAL_MAX), y + hgt),
		IM_COL32(60, 160, 70, 150));
	// 過熱帯(赤の帯)
	dl->AddRectFilled(ImVec2(lerpX(OVERHEAT), y), ImVec2(x1, y + hgt),
		IM_COL32(180, 40, 40, 160));
	// 現在温度の塗り
	dl->AddRectFilled(ImVec2(x0, y), ImVec2(lerpX(m_heat), y + hgt), HeatColor(m_heat), 4.0f);
	// マーカー
	dl->AddLine(ImVec2(lerpX(m_heat), y - 5), ImVec2(lerpX(m_heat), y + hgt + 5),
		IM_COL32(255, 255, 255, 255), 2.0f);
	// 枠
	dl->AddRect(ImVec2(x0, y), ImVec2(x1, y + hgt), IM_COL32(200, 200, 200, 120), 4.0f);

	// ラベルと状態
	dl->AddText(ImVec2(x0, y - 22), IM_COL32(230, 230, 230, 255), "TEMPERATURE");
	const char* st; ImU32 stc;
	if      (m_heat < IDEAL_MIN) { st = "COLD - heat it up! (hold R)"; stc = IM_COL32(120, 170, 255, 255); }
	else if (m_heat > OVERHEAT)  { st = "OVERHEAT!";                   stc = IM_COL32(255, 120, 120, 255); }
	else                          { st = "GOOD HEAT";                   stc = IM_COL32(150, 255, 150, 255); }
	dl->AddText(ImVec2(x1 - 220, y - 22), stc, st);
}

void SceneForge::DrawTitleUI()
{
	CenterText("- FORGE -",            0.34f, 3.0f, IM_COL32(255, 200, 120, 255));
	CenterText("Timing Blacksmith",    0.44f, 1.4f, IM_COL32(255, 235, 210, 255));
	CenterText("PRESS  SPACE  TO  START", 0.62f, 1.6f, IM_COL32(255, 255, 255, 255));
}

void SceneForge::DrawPlayUI()
{
	// 鉄条とハンマーは3Dで描画するので、2Dの鉄条(DrawBillet/DrawHammer)は使わない

	// 温度ゲージ
	DrawHeatGauge();

	// FPS準心: 画面中心に十字。鉄の上(照準有効)なら橙で光り、外れていれば暗い白。
	{
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		ImVec2 disp = ImGui::GetIO().DisplaySize;
		float cx = disp.x * 0.5f, cy = disp.y * 0.5f;
		float r = 12.0f, g = 4.0f;	// 半径と中央の隙間
		ImU32 col = m_aimValid ? IM_COL32(255, 180, 70, 255) : IM_COL32(220, 220, 220, 130);
		float th = m_aimValid ? 3.0f : 2.0f;
		dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx - g, cy), col, th);
		dl->AddLine(ImVec2(cx + g, cy), ImVec2(cx + r, cy), col, th);
		dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy - g), col, th);
		dl->AddLine(ImVec2(cx, cy + g), ImVec2(cx, cy + r), col, th);
		if (m_aimValid) dl->AddCircle(ImVec2(cx, cy), r + 3.0f, col, 0, 1.5f);
	}

	// 過熱の警告(点滅)
	if (m_heat > OVERHEAT)
	{
		float p = 0.5f + 0.5f * sinf(m_time * 12.0f);
		CenterText("!!  OVERHEAT  !!", 0.20f, 1.6f, IM_COL32(255, 70, 50, (int)(150 + p * 105)));
	}

	// 打撃フィードバックのポップアップ(鉄条の上でフェード)
	if (m_popupLife > 0.0f)
	{
		float a = m_popupLife / 0.8f;
		if (a > 1.0f) a = 1.0f;
		unsigned int c = (m_popupCol & 0x00FFFFFF) | ((unsigned int)(a * 255) << 24);
		CenterText(m_popupText, 0.36f, 2.0f, c);
	}

	// スコアと形状一致度(左上)
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	char sb[48];
	sprintf_s(sb, sizeof(sb), "SCORE  %d", m_score);
	dl->AddText(ImVec2(40, 40), IM_COL32(255, 235, 200, 255), sb);
	sprintf_s(sb, sizeof(sb), "SHAPE MATCH  %d%%", (int)(m_match * 100));
	dl->AddText(ImVec2(40, 60), IM_COL32(150, 220, 255, 255), sb);

	// 一致度バー(上部中央)
	{
		ImVec2 disp = ImGui::GetIO().DisplaySize;
		float bx0 = disp.x * 0.30f, bx1 = disp.x * 0.70f, by = 30.0f, bh = 14.0f;
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(30, 30, 34, 220), 3.0f);
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * m_match, by + bh),
			IM_COL32(90, 200, 255, 255), 3.0f);
		dl->AddRect(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(200, 200, 200, 120), 3.0f);
	}

	// 廃件率(左上, SHAPE MATCHの下)。過熱/冷打/完成済みの段を叩くと溜まり、満ちると失敗。減らない。
	{
		float pct = m_spoil; if (pct > 1.0f) pct = 1.0f;
		sprintf_s(sb, sizeof(sb), "SPOIL  %d%%", (int)(pct * 100));
		dl->AddText(ImVec2(40, 80), IM_COL32(255, 150, 120, 255), sb);
		float bx0 = 130.0f, bx1 = 300.0f, by = 84.0f, bh = 10.0f;
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + bh), IM_COL32(30, 30, 34, 220), 2.0f);
		ImU32 sc = (pct > 0.6f) ? IM_COL32(255, 70, 50, 255) : IM_COL32(230, 140, 60, 255);
		dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * pct, by + bh), sc, 2.0f);
	}

	// KCD式: 「叩く場所」は指示しない。誤打時だけ主人公の独白(m_popupText)で知らせる。
	//   デバッグ時のみ Pキーで瞄準区域の可視化ON(状態表示)。
	if (m_showAimHi)
		CenterText("[DEBUG] aim highlight ON (P to toggle)", 0.10f, 0.9f, IM_COL32(120, 220, 160, 180));

	// 淬火の準備ができたら促す
	if (m_match >= 0.85f)
		CenterText("Shape looks good!  Press  Q  to Quench", 0.86f, 1.2f,
			IM_COL32(150, 255, 180, 230));

	// 操作ガイド
	CenterText("Mouse : Aim    Hold L-MOUSE : Hammer    Hold R : Heat    Q : Quench",
		0.93f, 1.0f, IM_COL32(255, 255, 255, 170));
}

void SceneForge::DrawResultUI()
{
	CenterText("FORGED!",              0.30f, 3.0f, IM_COL32(255, 220, 140, 255));
	char buf[64];
	sprintf_s(buf, sizeof(buf), "SHAPE MATCH  %d%%", (int)(m_match * 100));
	CenterText(buf,                    0.46f, 2.0f, IM_COL32(150, 220, 255, 255));
	sprintf_s(buf, sizeof(buf), "SCORE  %d", m_score);
	CenterText(buf,                    0.56f, 2.0f, IM_COL32(255, 255, 255, 255));
	CenterText("PRESS  SPACE  TO  RETURN", 0.70f, 1.4f, IM_COL32(255, 255, 255, 220));
}

//--- 廃件(失敗)画面: 鋼を叩き損じて台無しにした。分数は出すが低評価。
void SceneForge::DrawGameOverUI()
{
	float p = 0.5f + 0.5f * sinf(m_time * 6.0f);
	CenterText("RUINED",               0.28f, 3.2f, IM_COL32(255, 80, 60, (int)(180 + p * 75)));
	CenterText("You spoiled the steel", 0.44f, 1.6f, IM_COL32(255, 170, 150, 255));
	char buf[64];
	sprintf_s(buf, sizeof(buf), "SCORE  %d", m_score);
	CenterText(buf,                    0.56f, 2.0f, IM_COL32(255, 255, 255, 255));
	CenterText("PRESS  SPACE  TO  RETRY", 0.70f, 1.4f, IM_COL32(255, 255, 255, 220));
}

void SceneForge::DrawUI()
{
	// 配置/材質/炭火/カメラの編集はすべて SCENE_FORGE_STAGESETTING に移設。
	// ゲーム側は焼き込み済みの値で表示するだけ(F1はクリーン)。

	switch (m_state)
	{
	case GAME_TITLE:  DrawTitleUI();  break;
	case GAME_PLAY:   DrawPlayUI();   break;
	case GAME_RESULT: DrawResultUI(); break;
	case GAME_OVER:   DrawGameOverUI(); break;
	}

	// F1中: 武器モデルの向き/大きさ/位置を砧面に合わせる調整パネル(合ったら数値を焼き込む)
	if (DebugUI::IsVisible() && m_wpOk)
	{
		ImGui::Begin("Weapon Align (F1)");
		ImGui::Text("stages loaded: %d,  verts: %d", (int)m_wpStage.size(), m_wpN);
		ImGui::SliderFloat("Forge progress", &m_forgeProg, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Yaw",   &m_wpYaw,   -3.1416f, 3.1416f, "%.3f");
		ImGui::SliderFloat("Pitch", &m_wpPitch, -3.1416f, 3.1416f, "%.3f");
		ImGui::SliderFloat("Roll",  &m_wpRoll,  -3.1416f, 3.1416f, "%.3f");
		ImGui::SliderFloat("Scale", &m_wpScale, 0.2f, 3.0f, "%.2f");
		ImGui::SliderFloat("Off X", &m_wpOff[0], -1.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Off Y", &m_wpOff[1], -1.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Off Z", &m_wpOff[2], -1.0f, 1.0f, "%.3f");
		ImGui::Separator();
		ImGui::TextDisabled("Camera (3/4 forge view)");
		ImGui::SliderFloat3("Cam Pos",  m_camPos,  -5.0f, 6.0f, "%.2f");
		ImGui::SliderFloat3("Cam Look", m_camLook, -5.0f, 6.0f, "%.2f");
		ImGui::End();
	}
}

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

//--- プロップのワールド行列(編集シーンSceneBlankと同一規約=床は常にY=0)。
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
//    SceneBlank::LoadLayout と同じパーサ(キーが一致するプロップにだけ適用)。
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

//--- 3Dハンマー: 打撃位置の真上に置き、蓄力で上がり打撃で振り下ろす
void SceneForge::DrawHammer3D()
{
	Model* hammer = GetObj<Model>("MdlHammer");
	if (!hammer) return;

	// 準心が当たっているセルの真上にハンマーを置く。準心が板の外に出ても、m_aimWorld/m_aimI/J は
	// 最後に有効だった位置を保持している(UpdateAimは無効時に値を更新しない)ので、そのまま使う=
	// 中央にリセットせずハンマーは最後の位置に留まる(操作の異様感を無くす)。
	float wx = m_aimWorld.x, wz = m_aimWorld.z;
	float barTop = m_barAnchor.y + m_h[m_aimI][m_aimJ];
	XMFLOAT3 pos = {
		wx + m_hammerOff[0],
		barTop + m_hammerLift + m_hammerOff[1],
		wz + m_hammerOff[2],
	};

	XMMATRIX world =
		XMMatrixScaling(m_hammerScale, m_hammerScale, m_hammerScale) *
		XMMatrixRotationRollPitchYaw(m_hammerRot[0], m_hammerRot[1], m_hammerRot[2]) *
		XMMatrixTranslation(pos.x, pos.y, pos.z);
	world = hammer->GetScaleBaseMatrix() * world;
	DrawModelWorld(hammer, world);
}

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

#if 0	// 旧: 半透明ゴースト輪郭(P0の初期案)。KCD式「注定成形」に切替えたため未使用。
//--- m_target[] から半透明ゴースト目標の頂点を生成(戻り値=頂点数)。
//    実体の鉄条(BuildBarMesh)と同じ箱の作り方だが、太さは目標プロファイル、
//    色は「逐段フィードバック」= まだ厚すぎる段は赤、合致した段は緑、削りすぎは青。
//    見やすさのため目標より少しだけ膨らませて外殻の輪郭にする。
int SceneForge::BuildGhostMesh()
{
	int v = 0;
	const float half = m_barLen * 0.5f;
	const float ax   = m_barAnchor.x;
	const float az   = m_barAnchor.z;
	const float cy   = m_barAnchor.y;
	const float inflate = 1.04f;	// 目標をわずかに包む外殻

	auto tri = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT4& col)
	{
		m_ghostVtx[v++] = { a, XMFLOAT2(0,0), col };
		m_ghostVtx[v++] = { b, XMFLOAT2(0,0), col };
		m_ghostVtx[v++] = { c, XMFLOAT2(0,0), col };
	};
	auto quad = [&](const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT3& d, const XMFLOAT4& col)
	{
		tri(a, b, c, col); tri(a, c, d, col);
		tri(a, c, b, col); tri(a, d, c, col);
	};

	// 段ごとの誤差から色を決める(diff>0=まだ厚い/削りが足りない, diff<0=削りすぎ)
	auto segColor = [&](int i) -> XMFLOAT4
	{
		float diff = m_th[i] - m_target[i];
		const float tol = 0.06f;	// この範囲内なら合致とみなす
		XMFLOAT4 c;
		if (diff > tol)      c = XMFLOAT4(1.0f, 0.35f, 0.25f, 0.30f);	// 赤: もっと叩く
		else if (diff < -tol)c = XMFLOAT4(0.35f, 0.55f, 1.0f, 0.30f);	// 青: 叩きすぎ
		else                 c = XMFLOAT4(0.40f, 1.0f, 0.45f, 0.34f);	// 緑: OK
		return c;
	};

	const float w = m_barWidth * inflate;
	for (int i = 0; i < SEG - 1; ++i)
	{
		float z0 = az - half + m_barLen * (i       / (float)(SEG - 1));
		float z1 = az - half + m_barLen * ((i + 1) / (float)(SEG - 1));
		float y0 = m_barThick * m_target[i]     * inflate;
		float y1 = m_barThick * m_target[i + 1] * inflate;
		XMFLOAT4 col = segColor(i);

		XMFLOAT3 a_tL = { ax - w, cy + y0, z0 }, a_tR = { ax + w, cy + y0, z0 };
		XMFLOAT3 a_bL = { ax - w, cy - y0, z0 }, a_bR = { ax + w, cy - y0, z0 };
		XMFLOAT3 b_tL = { ax - w, cy + y1, z1 }, b_tR = { ax + w, cy + y1, z1 };
		XMFLOAT3 b_bL = { ax - w, cy - y1, z1 }, b_bR = { ax + w, cy - y1, z1 };

		quad(a_tL, a_tR, b_tR, b_tL, col);
		quad(a_bR, a_bL, b_bL, b_bR, col);
		quad(a_tR, a_bR, b_bR, b_tR, col);
		quad(a_bL, a_tL, b_tL, b_bL, col);
	}
	// 端の蓋
	{
		float y = m_barThick * m_target[0] * inflate; float zc = az - half; XMFLOAT4 c = segColor(0);
		quad({ ax - w,cy + y,zc }, { ax + w,cy + y,zc }, { ax + w,cy - y,zc }, { ax - w,cy - y,zc }, c);
	}
	{
		float y = m_barThick * m_target[SEG - 1] * inflate; float zc = az + half; XMFLOAT4 c = segColor(SEG - 1);
		quad({ ax - w,cy + y,zc }, { ax + w,cy + y,zc }, { ax + w,cy - y,zc }, { ax - w,cy - y,zc }, c);
	}
	return v;
}

//--- 半透明の目標剣形を実体の鉄条に重ねて描く(P0)。
//    深度テストOFF(DEPTH_DISABLE)でX線オーバーレイにし、太い実体の中に埋もれた
//    目標も透けて見えるようにする。段ごとの色で厚すぎ/薄すぎが一目で分かる。
void SceneForge::DrawGhostTarget()
{
	CameraBase*   cam = GetObj<CameraBase>("Camera");
	VertexShader* vs  = GetObj<VertexShader>("VS_Bar");
	PixelShader*  ps  = GetObj<PixelShader>("PS_Bar");
	if (!cam || !vs || !ps || !m_ghostMesh) return;

	XMFLOAT4X4 cb[2] = { cam->GetView(), cam->GetProj() };
	vs->WriteBuffer(0, cb);

	int n = BuildGhostMesh();
	if (n <= 0) return;
	m_ghostMesh->Write(m_ghostVtx.data());

	SetBlendMode(BLEND_ALPHA);
	SetDepthTest(DEPTH_DISABLE);	// 実体に埋もれても透けて見えるX線オーバーレイ
	vs->Bind();
	ps->Bind();
	m_ghostMesh->Draw(n);
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);	// 後続の描画のため元に戻す
}
#endif

void SceneForge::Draw()
{
	ApplyCamera();	// 固定カメラを適用(GetViewの前に)
	DrawModelsTest();	// 先に不透明な3Dモデル(金床)を描く
	if (m_wpOk) DrawWeapon();	// Blender武器モデルを進捗でモーフ(あれば優先)
	else        Draw3DBillet();	// 無ければ従来の高さ場メッシュ
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

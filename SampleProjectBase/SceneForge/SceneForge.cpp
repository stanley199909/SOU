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
struct VIN  { float3 pos:POSITION0; float3 nrm:NORMAL0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };
struct VOUT { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.nrm=v.nrm; o.uv=v.uv; o.col=v.col; o.wp=v.pos; return o; }
)EOT";
//--- 武器PS: 真の鋼テクスチャ(BaseColor)を地色にし、発光はゲームの実時温度 m_forging.Heat() で駆動する。
//    col.rgb = 温度勾配色(HeatRGB)、col.a = 温度スカラー m_heat。
//    冷たい時=鋼テクスチャをライティングした金属色。熱い時=温度色で発光(テクスチャの陰影は残す)。
static const char* g_wpPS = R"EOT(
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
// 金属質感パラメータ(CPUの m_wp* から供給)。UE5風の質感の要=環境反射を擬似環境で安価に再現する。
cbuffer Mtl : register(b0){
  float3 camPos;  float rough;     // 相機位置 / 粗さ
  float3 lightDir;float metal;     // 光方向 / 金属度
  float3 skyCol;  float specK;     // 擬似環境の空色 / 直接光高光強度
  float3 grdCol;  float envK;      // 擬似環境の地色 / 環境反射強度
  float  fresK;   float3 _pad;     // 菲涅尔強度
};
struct PIN{ float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; };
float4 main(PIN i):SV_TARGET{
  float3 N = normalize(i.nrm);
  float3 L = normalize(lightDir);
  float3 V = normalize(camPos - i.wp);                     // 視線(鋼→相機)
  float3 H = normalize(L + V);
  float  nl = saturate(dot(N,L));
  float  nv = saturate(dot(N,V));
  float  nh = saturate(dot(N,H));
  float3 steel = tex.Sample(samp, i.uv).rgb;               // 冷鋼の地色(真のテクスチャ)

  // 拡散(金属は拡散が弱い=metalで減衰)。環境の底上げ(ambient)で真っ黒を防ぐ。
  float3 diff = steel * (0.25 + 0.75*nl) * (1.0 - 0.85*metal);

  // 直接光の鏡面高光(Blinn-Phong。粗さ→光沢指数。核显向けに安価)。
  float  shin = lerp(128.0, 8.0, rough);                   // 小rough=鋭い/大rough=広い
  float3 specTint = lerp((float3)1.0, steel, metal);       // 金属は高光が地色に色付く
  float3 spec = specTint * pow(nh, shin) * specK * nl;

  // 擬似環境反射(HDRI無し): 反射向きの上下で空色↔地色を補間=「周囲を映す」金属感の主因。
  float3 R   = reflect(-V, N);
  float3 env = lerp(grdCol, skyCol, saturate(R.y*0.5+0.5));
  float  fre = fresK * pow(1.0 - nv, 5.0);                 // 縁で反射が強まる(菲涅尔)
  float3 refl = env * (envK + fre) * lerp(0.15, 1.0, metal) * steel;

  float3 cold = diff + spec + refl;                        // 冷: 金属らしくライティング
  float3 hot  = i.col.rgb * (0.35 + 0.65*steel) + spec;    // 熱: 温度色で発光+高光は残す
  float  k    = smoothstep(0.06, 0.45, i.col.a);           // 温度(col.a)で冷→熱をブレンド
  float3 col  = lerp(cold, hot, k);
  return float4(col, 1.0);
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
	m_forging.Reset();

	// --- 【3D化テスト】鍛冶素材モデルの読み込み ---
	VertexShader* mvs = CreateObj<VertexShader>("VS_ForgeObj");
	if (FAILED(mvs->Load("Assets/Shader/VS_Object.cso")))
		MessageBox(nullptr, "VS_Object.cso", "Shader Error", MB_OK);
	PixelShader* mps = CreateObj<PixelShader>("PS_ForgeObj");
	if (FAILED(mps->Load("Assets/Shader/PS_TexTint.cso")))
		MessageBox(nullptr, "PS_TexTint.cso", "Shader Error", MB_OK);

	// --- 石墙専用シェーダー(triplanarで Poly Haven の実PBR貼图を投影) ---
	VertexShader* wallVS = CreateObj<VertexShader>("VS_Wall");
	if (FAILED(wallVS->Load("Assets/Shader/VS_Wall.cso")))
		MessageBox(nullptr, "VS_Wall.cso", "Shader Error", MB_OK);
	PixelShader* wallPS = CreateObj<PixelShader>("PS_Wall");
	if (FAILED(wallPS->Load("Assets/Shader/PS_Wall.cso")))
		MessageBox(nullptr, "PS_Wall.cso", "Shader Error", MB_OK);

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

	// 武器の鋼テクスチャ(BaseColor=冷鋼の地色)。発光は温度 m_forging.Heat() で駆動するので Emissive は直貼りしない。
	{
		m_wpTex = TextureCache::Get("Assets/MM_Blacksmith_Pack/Medieval_Sword_Blade/Blade_BaseColor.png");
	}

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
	const char* kForgeStone = "Assets/MM_Blacksmith_Pack/Forges/Textures/T_Forge_1_UV1_BaseColor.PNG";

	LoadProp("StGround",   "Assets/Model/plane/plane.fbx", "Assets/Model/field/wooden-plank-textured-background-material.jpg", 12.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
	LoadProp("StStump",    (P+"Anvil/SM_Stump.fbx").c_str(),             kAnvilTex.c_str(), 0.60f, 0.0f, 0.0f,  0.0f, 0.0f, true);
	LoadProp("StAnvil",    (P+"Anvil/SM_Anvil.fbx").c_str(),             kAnvilTex.c_str(), 0.70f, 0.0f, 0.0f,  0.0f, 0.0f, true);
	LoadProp("StForge",    (P+"Forges/SM_BS_Forge_2_.fbx").c_str(),      kForgeStone,       2.60f, 1.8f, 0.0f,  0.6f, 0.0f, true);	// Forge_1は可視メッシュが重複(未merge)で破図→Forge_2に差替。材質名Forge_UVxは既存のUVx判定で自動一致
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
		m_coalBedMesh = CoalBedMesh::Create();
	}

	// 編集シーンで作った配置(Assets/stage_layout.txt)を反映。無ければ上の既定のまま。
	// StCottage も普通のプロップとして round-trip する(編集シーンで大きさ/位置を決めれば此処が読む)。
	for (const auto& e : OutdoorStage::Read()) {
		LoadProp(e.key.c_str(),e.path.c_str(),"",1.0f,e.x,e.y,e.z,e.yaw,false);
		for(auto& p:m_props) if(p.key==e.key) {p.scale=e.scale;p.pos[1]=e.y;p.groundSnap=false;}
	}
	LoadLayout();
	CottageRender::Load();
	LoadTuning();	// F1で調整したハンマー/カメラ値(forge_tuning.txt)を復元
	// Layout/tuning must be loaded before assigning the walking spawn height.
	m_player.Init(DirectX::XMFLOAT3(0.0f, m_walkFloorY, -2.0f), 0.0f);
	SnapshotTuning();	// ↑復元直後の値を「起動時の姿」として記録(F8/ボタンでここへ戻せる)

	SetupSteps();	// 工程(step)状態を生成し状態機へ登録(遷移は StartGame で開始)

	Strike();	// 開始直後から火花を出す
	// ロード完了後にここで音を開始(起動途中でBGMが鳴らないように Main から移動)
	// 起動はタイトル状態 → タイトルBGM(工場環境音)をループ。
	Audio::PlayLoop(Audio::BGM_TITLE, 0.40f);
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
	if (!m_cursorShown) { ShowCursor(TRUE); m_cursorShown = true; }	// カーソルを戻す
}

void SceneForge::Strike(float scale)
{
	// 1回叩くと火花をまとめて発生(バースト)。量と勢いの物理は Particles が持つ。
	const int N = (int)(m_burst * scale);
	XMFLOAT3 origin = XMFLOAT3(0.0f, 1.0f, 0.0f);	// 金床の位置(カメラ注視点の高さ)
	m_particles.SpawnSparks(origin, N, m_power, scale);
}

// 目標形状(BuildTarget)と一致度(ShapeMatch)は Physics/ForgingSim へ移動した。

//====================================================================
//  ゲーム進行
//====================================================================
void SceneForge::StartGame()
{
	Audio::Stop(Audio::BGM_TITLE);				// タイトルBGMを止める
	Audio::PlayLoop(Audio::BGM_PLAY, 0.45f);	// ゲーム中BGM(medieval)
	m_heatSndOn = false;						// 加熱持続音の状態をリセット
	m_state    = GAME_PLAY;
	// ゲーム開始時は「走動モード」から。工坊を歩いて工位に着き、Eで鍛造に入る。
	m_walkMode = true;
	m_modeTrans = ModeTrans::None;	// 走動⇔工位の移動アニメも解除
	m_pendingFlip = false; m_focus = -1; m_promptAlpha = 0.0f;	// 互動の状態も初期化
	m_walkPitch = 0.0f;
	m_player.Init(DirectX::XMFLOAT3(0.0f, m_walkFloorY, -2.0f), 0.0f);	// 開始位置/向きを戻す
	m_score    = 0;
	m_forging.Reset();		// 鉄を厚板・無傷・進捗0へ(表面が上に戻る。目標形状も再生成)
	m_forgeProg = 0.0f;		// 武器モーフのプレビュー進捗も戻す
	m_match = 0.0f;
	m_flipAngle = 0.0f;		// 翻面回転も表(0)へ戻す
	m_flipPhase = FlipPhase::None;	// 翻面子状態機も初期化(鍛打中に戻す)
	m_camTongsW = 0.0f; m_camGripW = 0.0f;	// 運鏡の寄りも解除
	m_hammerStowW = 0.0f;					// ハンマーは構え位置へ
	m_tongsInHand = false;					// 火钳は台の上へ
	if (Prop* pl = GetProp("StPliers")) pl->hidden = false;

	m_charging    = false;
	m_charge      = 0.0f;
	m_strikeCD    = 0.0f;
	m_hammer.Reset();			// 鎚を静止高へ・速度ゼロに戻す
	m_aimI = ForgingSim::NL / 2; m_aimJ = ForgingSim::NW / 2; m_aimSeg = 0; m_aimValid = false;
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

	// 工程(step)状態機を最初の工程から開始する。
	//   遷移先の名前は「配方(m_recipe)の順序」から取る=データ駆動(chase は名前を状態に直書きだった)。
	m_stepIdx = 0;
	m_stepMachine.ChangeState(StepKey(m_recipe->steps[0].type));
}

//--- 工程(step)状態を生成し、状態機へ登録する(Init で一度だけ)。
//    各状態は owner=this を持ち、完了時に AdvanceStep() を呼ぶ。登録名(GetStateName)が状態機のキー。
void SceneForge::SetupSteps()
{
	m_heatStep   = std::make_unique<HeatStep>(this);
	m_forgeStep  = std::make_unique<ForgeStep>(this);
	m_quenchStep = std::make_unique<QuenchStep>(this);
	m_heatStep->RegisterState(m_stepMachine);
	m_forgeStep->RegisterState(m_stepMachine);
	m_quenchStep->RegisterState(m_stepMachine);
}

//--- 次の工程へ進む。配方(データ)が順序の正。最後の工程を越えたら完成(淬火済み)へ。
void SceneForge::AdvanceStep()
{
	++m_stepIdx;
	if (!m_recipe || m_stepIdx >= (int)m_recipe->steps.size()) { FinishGame(); return; }
	m_stepMachine.ChangeState(StepKey(m_recipe->steps[m_stepIdx].type));
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

//--- 翻面の子状態機(鍛打工程の内部)。玩家が F で起動→火钳運鏡→夹む→マウスで翻す→面を確定。
//    命名 enum + switch の軽量な下層FSM。上層(工程FSM)は触らない=翻面は鍛打の内部交互。
//    ※呼び出し側(UpdatePlay)は、None 以外の間はハンマー入力を止める(火钳を扱っている最中)。
//--- 取り消し不可の運鏡ビートか。ここが true の間は F/左键/マウスを一切受け付けない
//    (「再生中の動画は途中で止められない」= 火钳が空中で消える様な破綻を構造的に防ぐ)。
bool SceneForge::FlipIsCutscene() const
{
	return m_flipPhase == FlipPhase::TongsOut
		|| m_flipPhase == FlipPhase::Gripping
		|| m_flipPhase == FlipPhase::PutBack;
}

void SceneForge::UpdateFlip(float tick, bool inputOn)
{
	// 工程の制限は「F で火钳を取る」入口だけ(下の None)。台の火钳を取って来た場合は加熱工程でも
	// 翻面が始まるので、翻面そのものは工程に関係なく最後まで進める(途中で打ち切ると火钳が手に残る)。
	const bool forgePhase = (CurrentStep().type == StepName::Forge);
	// 入力凍結中(F1/遷移)は「一時停止」: 段階も計時も進めない。打ち切らない=動画は取り消されない。
	if (!inputOn) return;

	// 運鏡ビート中に押されたキーは「読んで捨てる」。IsKeyTrigger は押した瞬間の1フレームだけ true なので、
	// ビート中に判定しなければ、そのキーはビート明けに持ち越されない(=バッファされず暴発しない)。
	switch (m_flipPhase)
	{
	case FlipPhase::None:									// 鍛打工程の工位でだけ: F で火钳を取って翻面を起動
		if (forgePhase && IsKeyTrigger('F')) { m_flipPhase = FlipPhase::TongsOut; m_flipTimer = 0.0f; }
		break;

	case FlipPhase::TongsOut:								// 火钳を取り出す運鏡(慢い)
	{
		m_flipTimer += tick;
		// 振り向いて静止している区間の中点=「手に取った」瞬間。台上の火钳を消す。
		const float grabAt = FLIP_TONGS_OUT_TIME * (FLIP_REACH_FRAC + FLIP_RETURN_FRAC) * 0.5f;
		if (m_flipTimer >= grabAt) m_tongsInHand = true;
		if (m_flipTimer >= FLIP_TONGS_OUT_TIME) m_flipPhase = FlipPhase::Ready;
		break;
	}

	case FlipPhase::Ready:									// 火钳待命: F/ESC=戻す / 左键=夹む
		if      (IsKeyTrigger('F') || IsKeyTrigger(VK_ESCAPE)) { m_flipPhase = FlipPhase::PutBack; m_flipTimer = 0.0f; }
		else if (IsKeyTrigger(VK_LBUTTON)){ m_flipPhase = FlipPhase::Gripping; m_flipTimer = 0.0f; }
		break;

	case FlipPhase::Gripping:								// 铁を夹む運鏡→翻し開始(今の面から)
		m_flipTimer += tick;
		if (m_flipTimer >= FLIP_GRIP_TIME) m_flipPhase = FlipPhase::Flipping;	// 今の刃の角度から翻し始める
		break;

	case FlipPhase::Flipping:								// マウス左右で铁を翻す。左键で面を確定
	{
		// マウス移動量をそのまま角度へ(1:1, FPS の視点操作と同じ raw input)。
		//   マウスが止まれば刃も止まる=入力が溜まらない。「重さ」は低い感度(m_flipSens)で出す。
		//   ※旧方式(狙い角を最大角速度で追う)は、速く振ると狙いが先行して溜まり、手を止めても
		//     刃が回り続けた(入力のバックログ)。加速/平滑/上限は手感を裏切るので使わない。
		//   上限なし=同じ方向へ回し続ければ何回でも翻る(左へ回せば逆回転)。
		float dx, dy; ReadMouseDelta(dx, dy);			// 相対マウス(ここで光標を中心へ戻す)
		m_flipAngle += dx * m_flipSens * DirectX::XM_PI;	// 感度の単位=「1pxあたり何半回転」

		// 面の判定: 刃角を「最も近い半回転の番号」k に丸める(境界=π/2, 3π/2, ...=半回転ごとに0.5の線)。
		//   k が偶数=表, 奇数=裏。0.5 を越えるたびに k が1つ進む=一回ずつ翻る。見た目の刃と必ず一致。
		const int k = (int)floorf(m_flipAngle / DirectX::XM_PI + 0.5f);
		m_forging.SetSide(((k % 2) + 2) % 2);			// 負の k(逆回転)でも 0/1 に正規化
		if (IsKeyTrigger(VK_LBUTTON) || IsKeyTrigger(VK_ESCAPE))	// 現在の面を確定→待命へ(ESC=一段戻る)
		{
			// 角度を「今の面の清潔な角」(0 か π)の近くへ巻き戻す。2π の倍数を引くだけなので見た目は不変。
			// これで確定後の Damp(目標=面×π)が最短で落ち着き、回し続けても角度が無限に増えない。
			const int wrap = k - m_forging.Side();		// 2 の倍数
			m_flipAngle -= wrap * DirectX::XM_PI;
			m_flipPhase = FlipPhase::Ready;
		}
		break;
	}

	case FlipPhase::PutBack:								// 火钳を戻す運鏡→鍛打へ復帰
	{
		m_flipTimer += tick;
		// TongsOut と対称: 振り向いた静止区間の中点で火钳を台へ置く(モデルを再表示)。
		const float putAt = FLIP_PUTBACK_TIME * (FLIP_REACH_FRAC + FLIP_RETURN_FRAC) * 0.5f;
		if (m_flipTimer >= putAt) m_tongsInHand = false;
		if (m_flipTimer >= FLIP_PUTBACK_TIME) m_flipPhase = FlipPhase::None;
		break;
	}
	}
}

//--- 翻面の運鏡。段階(と計時)から2つの重みを決める。カメラへの適用は ApplyCamera が行う。
//    火钳の曲線は計時の純関数(同じ時刻なら同じ画)=決定論的で、途中で揺れない。
void SceneForge::UpdateFlipCamera(float tick)
{
	// 火钳へ振り向く曲線: 0→1(振り向く)→1(手に取る)→0(元の視点へ)。両端は SmoothStep で緩急。
	auto tongsCurve = [](float t01) {
		if (t01 < FLIP_REACH_FRAC)  return Lerp::SmoothStep(t01 / FLIP_REACH_FRAC);
		if (t01 < FLIP_RETURN_FRAC) return 1.0f;
		return 1.0f - Lerp::SmoothStep((t01 - FLIP_RETURN_FRAC) / (1.0f - FLIP_RETURN_FRAC));
	};

	switch (m_flipPhase)
	{
	case FlipPhase::TongsOut: m_camTongsW = tongsCurve(m_flipTimer / FLIP_TONGS_OUT_TIME); break;
	case FlipPhase::PutBack:  m_camTongsW = tongsCurve(m_flipTimer / FLIP_PUTBACK_TIME);   break;
	default:                  m_camTongsW = 0.0f; break;
	}

	switch (m_flipPhase)
	{
	case FlipPhase::Gripping: m_camGripW = Lerp::SmoothStep(m_flipTimer / FLIP_GRIP_TIME); break;	// 刃へ寄る
	case FlipPhase::Flipping: m_camGripW = 1.0f; break;								// 寄ったまま刃を見て翻す
	default: m_camGripW = Lerp::Damp(m_camGripW, 0.0f, m_gripLambda, tick); break;	// 面確定後、元の視点へ戻る
	}

	// ハンマーを置く/取る。火钳へ振り向く間に置き、戻ってくる間に取り上げる(同じ時間割を再利用)。
	switch (m_flipPhase)
	{
	case FlipPhase::TongsOut: m_hammerStowW = Lerp::SmoothStep((m_flipTimer / FLIP_TONGS_OUT_TIME) / FLIP_REACH_FRAC); break;
	case FlipPhase::PutBack:
	{
		const float t01 = m_flipTimer / FLIP_PUTBACK_TIME;
		m_hammerStowW = 1.0f - Lerp::SmoothStep((t01 - FLIP_RETURN_FRAC) / (1.0f - FLIP_RETURN_FRAC));
		break;
	}
	case FlipPhase::None:     m_hammerStowW = 0.0f; break;
	default:                  m_hammerStowW = 1.0f; break;	// Ready/Gripping/Flipping=火钳を持っている
	}

	// 台上の火钳モデルの表示は「手に持っているか」に従う。
	if (Prop* pl = GetProp("StPliers")) pl->hidden = m_tongsInHand;
}

void SceneForge::FinishGame()
{
	// PLAY中のBGM/加熱音を止め、淬火→成功音→結果BGMへ切り替える
	Audio::Stop(Audio::BGM_PLAY);				// ゲーム中BGMを止める
	if (m_heatSndOn) { Audio::Stop(Audio::SE_FORGE_LOOP); m_heatSndOn = false; }
	Audio::Play(Audio::SE_QUENCH, 0.9f);		// 水に入れる「ジュワ〜」(仕上げの淬火)
	Audio::Play(Audio::SE_SUCCESS, 0.8f);		// 完成の合図
	Audio::PlayLoop(Audio::BGM_RESULT, 0.5f);	// 結果画面BGM
	m_state = GAME_RESULT;
	m_walkMode = false;	// 結果画面は通常カメラで見せる(走動カメラを解除)
	m_modeTrans = ModeTrans::None;
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
	// F1(デバッグUI)を開いている間、画面フェード(遷移)中、および走動モード中はゲーム入力を凍結する。
	// ※走動中・走動⇔工位の移動アニメ中に打鉄/加熱/工程FSMが動かないよう条件に含める。
	bool inputOn = !DebugUI::IsVisible() && !m_fade.IsBusy() && !m_walkMode && !Transitioning();

	// --- 工位から出る: E または ESC(汎用の「戻る」)。翻面中は出ない(火钳を持ったまま離れない)。
	//   翻面中の ESC は UpdateFlip が「一段戻る」として扱う(Flipping→Ready→火钳を戻す)。
	if (inputOn && m_flipPhase == FlipPhase::None && (IsKeyTrigger('E') || IsKeyTrigger(VK_ESCAPE)))
	{
		m_charging = false; m_charge = 0.0f;	// 蓄力中なら破棄(暴発させない)
		BeginExitForge();
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
	//   加熱(R)は炉の前で玩家が行う行為なので inputOn の時だけ。
	const bool simOn = !DebugUI::IsVisible() && !m_fade.IsBusy();
	bool heating = inputOn && IsKeyPress('R');
	if (simOn)
	{
		if (heating) m_forging.AddHeat(HEAT_RATE * tick);	// 熱源=炉(外から熱を入れる)
		m_forging.Cool(tick);								// 鉄が自分で冷める
	}
	// 加熱中は炉火/風箱の持続音をループ。離した(またはF1/遷移で入力停止)瞬間に停止。
	if (heating && !m_heatSndOn)      { Audio::PlayLoop(Audio::SE_FORGE_LOOP, 0.5f); m_heatSndOn = true; }
	else if (!heating && m_heatSndOn) { Audio::Stop(Audio::SE_FORGE_LOOP);           m_heatSndOn = false; }

	// --- 過熱で放置すると鋼全体が焼けていく(損傷が蓄積)＋ジュー音 ---
	//   冷却と同じく鉄の物理なので simOn で進める(F1中は温度と一緒に止まる)。
	if (simOn && m_forging.Heat() > OVERHEAT)
	{
		m_forging.BurnAll(BURN_RATE * tick);	// 過熱で鉄全体が焼ける(損傷が蓄積)
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
	if (inputOn && !flipping) UpdateAim();	// 翻面中はハンマーを置いている=照準判定もしない

	// --- 蓄力ハンマー: 左クリック押しっぱなしで蓄力、離すと打撃。打撃後はクールダウン ---
	if (m_strikeCD > 0.0f) m_strikeCD -= tick;	// クールダウン消化

	// 打撃は「鍛打(Forge)工程」の時だけ許す。加熱/淬火の工程では叩けない(=工程で行為をゲート)。
	//   判定は文字列比較でなく強型列挙 StepName で行う: 打ち間違え(StepName::Foge 等)は
	//   コンパイルエラーで即座に弾ける(文字列 "Forge" だと綴り間違いが黙って false になる)。
	const bool forgePhase = (CurrentStep().type == StepName::Forge);

	if (!inputOn || !forgePhase || flipping)
	{
		// F1操作中・鍛打工程でない・翻面中=蓄力をキャンセル(暴発しないように)。
		// 翻面中は左键を火钳に使うので、翻面明けは一度離すまで蓄力させない(m_canStrike=false)。
		m_charging = false;
		m_charge   = 0.0f;
		if (flipping) m_canStrike = false;
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
		m_forgeProg = m_forging.SegAverage();	// 全体進捗(表示・モーフのプレビュー用)
		m_match = m_forgeProg;
	}
	else m_match = m_forging.ShapeMatch();

	// --- 翻面の見た目 ---
	//   Flipping 中はマウスが m_flipAngle を直接動かす(UpdateFlip 内)。それ以外の時は、確定済みの
	//   面の清潔な角(0 か π)へ Damp で落ち着かせる。Damp はフレームレート非依存。
	if (m_flipPhase != FlipPhase::Flipping)
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

	// --- 工程(step)状態機を進める ---
	//   各工程状態の OnUpdate が完了条件を見て AdvanceStep() を呼ぶ:
	//     Heat  … 目標温度に達したら次へ
	//     Forge … 全区域が到位したら次へ(叩く行為自体は上の蓄力/DoStrike が担当)
	//     Quench… Q を押したら完成(FinishGame)
	//   完了判定に入力(Q)を読む工程があるので、入力凍結中(F1/遷移中)は進めない。
	if (inputOn) m_stepMachine.Update(tick);
}

//--- 蓄力を解放して1打: 変形＋フィードバック
void SceneForge::DoStrike()
{
	float power = m_charge;			// 0..1
	const float heat = m_forging.Heat();
	bool cold = (heat < COLD_LIMIT);
	bool over = (heat > OVERHEAT);

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
		Audio::Play(Audio::SE_SWING, 0.7f);	// 空を切る「ヒュッ」(鉄に当たっていない合図。文字は出さない)
		return;
	}

	// 温度係数(冷たい→ほぼ効かない, 過熱→効くが品質悪, 適温→最大)。玩家側で算した修正値。
	float heatFactor = cold ? HEAT_EFF_COLD : (over ? HEAT_EFF_OVER : 1.0f);
	// 打撃の「鉄の反応」(体積守恒の金属流動・損傷・成形進度)は ForgingSim が担当。
	//   ここは玩家の動作側=修正値を渡して結果(outcome)を受け取るだけ。結果で下の回饋を出す。
	int ci = m_aimI, cj = m_aimJ, seg = AimSeg();
	ForgingSim::StrikeOutcome outcome =
		m_forging.ApplyStrike(ci, cj, seg, power, heatFactor, grooveMult, cold, over);

	// 温度が下がる / 火花 / 振動
	m_forging.AddHeat(-STRIKE_COOL);	// 打撃で熱が金床/鎚へ逃げる
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
	m_hammer.Strike();

	// 評価: KCD式に「指示せず、誤りだけ知らせる」。負向フィードバックは日本語(主人公の独白)。
	//   過熱/冷打/完成済みの区域を叩く=誤り→独白で知らせるだけ(罰は無し=誰でも最後まで遊べる)。
	//   ForgingSim が返した「鉄がどうなったか」で分岐する(cold/over/完成済みの再判定は不要)。
	const char* label; unsigned int col; float quality = 0.0f;
	// u8"" は C++20 では char8_t。ImGuiはUTF-8バイトを要求するので(const char*)へ再解釈する。
	switch (outcome)
	{
	case ForgingSim::StrikeOutcome::ColdHit:
		label = (const char*)u8"まだ冷たい…赤くなるまで熱して"; col = IM_COL32(120, 170, 255, 255); break;
	case ForgingSim::StrikeOutcome::OverHit:
		label = (const char*)u8"熱しすぎだ！鋼が焼ける";       col = IM_COL32(255, 120, 120, 255); break;
	case ForgingSim::StrikeOutcome::AlreadyDone:
		label = (const char*)u8"ここはもう完成済みだ";         col = IM_COL32(255, 200,  90, 255); break;
	default:	// Shaped = 適温 & 未完成の区域に命中 = 成功。得点のみ
		if      (power > POWER_PERFECT) { label = "PERFECT!"; col = IM_COL32(255, 220, 120, 255); quality = QUALITY_PERFECT; }
		else if (power > POWER_GOOD)    { label = "GOOD";     col = IM_COL32(180, 255, 150, 255); quality = QUALITY_GOOD; }
		else                            { label = "WEAK";     col = IM_COL32(200, 200, 200, 255); quality = QUALITY_WEAK; }
		if (inGroove) { quality += GROOVE_QUALITY_BONUS; Audio::Play(Audio::SE_WHISTLE, 0.5f); }	// テンポで口笛
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

//--- 結果: SPACEでタイトルへ戻る
void SceneForge::UpdateResult(float /*tick*/)
{
	if (IsKeyTrigger(VK_SPACE) && !m_fade.IsBusy())
	{
		m_fade.Transition([this] {
			m_state = GAME_TITLE;
			Audio::Stop(Audio::BGM_RESULT);				// 結果BGMを止める
			Audio::PlayLoop(Audio::BGM_TITLE, 0.40f);	// タイトルBGMを再開
		});
	}
}

void SceneForge::Update(float tick)
{
	m_time += tick;
	m_fade.Update(tick);	// 画面フェード(黒幕)を進める。遷移はTransitionの黒転じで実行される

	// F8 = 全調整値を起動時スナップショットへ一発リセット(F1デバッグ表示中のみ=誤爆防止)。
	if (DebugUI::IsVisible() && IsKeyTrigger(VK_F8)) RestoreTuning();
	// PLAY中かつF1非表示・遷移中でないときだけ操作を受け付ける(ApplyCameraより先に)。
	bool canControl = (m_state == GAME_PLAY && !DebugUI::IsVisible() && !m_fade.IsBusy());
	if (canControl)
	{
		// 互動の注視判定(①範囲 ②視線)。走動中以外(工位/移動アニメ中)は対象なし=提示がフェードアウト。
		UpdateInteract(tick);

		if (Transitioning())
		{
			// 走動⇔工位の移動アニメ中: 取り消し不可。計時だけ進め、入力は捨てる(F1中は一時停止)。
			UpdateModeTrans(tick);
		}
		else if (m_walkMode)
		{
			// --- 走動モード: 一人称で工坊を歩く ---
			UpdateWalkLook();				// マウス→玩家yaw(左右)/カメラpitch(上下)
			m_player.SetMoveSpeed(m_walkSpeed);	// F1スライダの速度を毎フレーム反映
			std::vector<Box> walls;			// TODO(Step3): シーンのプロップから壁を組む。今は衝突なし
			m_player.Update(tick, walls);	// WASDで一人称移動

			// 互動: ①範囲 ②視線 の両方が true の物件だけ E が効く(Interaction.cpp)。
			//   金床=工位へ移動 / 火钳=取って工位へ移動→翻面。退出(工位→走動)は UpdatePlay 側で E/ESC。
			if (m_player.WantInteract() && m_focus >= 0) DoInteract(INTERACTABLES[m_focus].action);
		}
		else if (m_flipPhase == FlipPhase::Flipping)
		{
			// 翻面の「翻す」中はマウスを翻し量に使う(UpdatePlay→UpdateFlip が読む)。
			// ここで視角を読むと同フレームで光標を二重に中心へ戻す=偏移が消えるので、視角は止める。
		}
		else if (m_flipPhase != FlipPhase::None)
		{
			// 翻面中(運鏡ビート/火钳待命)はハンマーを置いている=狙いも視角も動かさない。
			// マウス移動は読んで捨てる(光標は中心へ戻す)=翻面明けに溜まった移動量で視点が跳ばない。
			float dx, dy; ReadMouseDelta(dx, dy);
		}
		else
		{
			UpdateMouseLook();				// 工位: 従来のFPS式受限環視(準心/rail)
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
	}

	// 火花・余燼の粒子シミュ(重力/バウンド, 浮力/上昇/淡出)はどの状態でも動かす。
	//   炭がONなら炭床(m_emberPos±m_emberArea)から余燼を発生させ、その後まとめて積分する。
	if (m_coalOn)
		m_particles.EmitEmbers(XMFLOAT3(m_emberPos[0], m_emberPos[1], m_emberPos[2]),
			m_emberArea[0], m_emberArea[1], m_emberRate, m_emberRise, tick);
	m_particles.Update(tick, m_time);
}


void SceneForge::Draw()
{
    CottageRender::ClearExterior();
	ApplyViewCamera();	// Update と同じ規約でDrawでも適用(GetViewの前に)
	DrawModelsTest();	// 先に不透明な3Dモデル(金床)を描く
	if (m_wpOk) DrawWeapon();	// Blender武器モデルを進捗でモーフ(あれば優先)
	else        Draw3DBillet();	// 無ければ従来の高さ場メッシュ
	if (m_wpOk) DrawGhostTarget();	// 実体の後に完成形の半透明ゴーストを重ねる
	DrawWater();		// 水槽の水面(屈折。背後のシーンを撮ってから描く=不透明の後)
	if (DebugUI::IsVisible()) { DrawDebugBoxes(); DrawInteractBoxes(); }	// F1中はAABB/箱・互動範囲を線で表示

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
	for (const Particles::Particle& s : m_particles.Sparks())
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


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
struct VIN  { float3 pos:POSITION0; float3 nrm:NORMAL0; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; float sharp:TEXCOORD2; float work:TEXCOORD3; };
struct VOUT { float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; float sharp:TEXCOORD4; float work:TEXCOORD5; };
VOUT main(VIN v){ VOUT o; o.pos=mul(float4(v.pos,1),view); o.pos=mul(o.pos,proj); o.nrm=v.nrm; o.uv=v.uv; o.col=v.col; o.wp=v.pos; o.sharp=v.sharp; o.work=v.work; return o; }
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
  float  fresK;                    // 菲涅尔強度
  float  hotShade;                 // 熱い時、光の当たらない面の明るさ(0..1。1=明暗なし=旧の平らな発光)
  float  rimK;   float rimPow;     // 熱い時の縁の明るさ / 縁へ寄る鋭さ
  float  scaleTiling; float scaleSoft; float scaleOpacity; float scaleGlow;	// 氧化皮(黒皮)
  float  hotGain;                  // 熱い鋼の発光全体の明るさ
  float  scaleStart;               // 叩く前から剥がれている割合(マスクの灰度がこれ以下=開局から地金が見える)
  float2 _pad2;
};
Texture2D scaleMask : register(t1);   // 氧化皮の厚み(灰度: 白=厚い / 黒=地金)。無縫=UV に繰り返して貼る
struct PIN{ float4 pos:SV_POSITION; float3 nrm:TEXCOORD0; float2 uv:TEXCOORD3; float4 col:TEXCOORD1; float3 wp:TEXCOORD2; float sharp:TEXCOORD4; float work:TEXCOORD5; };
// 氧化皮の見た目(色は固定の物理的な性質なので定数。量/明るさは cbuffer で調整)
static const float3 SCALE_COLD_COL = float3(0.06, 0.055, 0.05);   // 冷えた黒皮=煤けたほぼ黒(艶なし)
static const float3 SCALE_EMBER    = float3(1.0, 0.30, 0.08);     // 熱い黒皮の鈍い光の色(暗い赤)。地金の黄白色には寄せない
static const float3 LUMA_WEIGHTS   = float3(0.2126, 0.7152, 0.0722); // 輝度(Rec.709)= 温度色から「明るさ」だけ取る
// 研ぎ面(sharp: 0=鍛造のまま 1=研ぎ上がった刃先)。仮の見た目: 黒皮が取れて明るい地金が出て、面が滑らかになる。
// ※本番の研ぎ面の見た目は外観担当(ChatGPT)が差し替える。値の意味(sharp)はCPU側で確定済み。
static const float3 GROUND_STEEL  = float3(0.78, 0.80, 0.83);   // 研いだ地金の色
static const float  GROUND_ROUGH  = 0.08;                       // 研いだ面の粗さ(鏡面寄り)
static const float  GROUND_SPEC_BOOST = 1.5;                    // 研いだ刃線の高光の増し
float4 main(PIN i):SV_TARGET{
  float3 N = normalize(i.nrm);
  float3 L = normalize(lightDir);
  float3 V = normalize(camPos - i.wp);                     // 視線(鋼→相機)
  float3 H = normalize(L + V);
  float  nl = saturate(dot(N,L));
  float  nv = saturate(dot(N,V));
  float  nh = saturate(dot(N,H));
  float  e  = saturate(i.sharp);
  float3 steel = lerp(tex.Sample(samp, i.uv).rgb, GROUND_STEEL, e);   // 冷鋼の地色 → 研いだ所は明るい地金

  // 拡散(金属は拡散が弱い=metalで減衰)。環境の底上げ(ambient)で真っ黒を防ぐ。
  float3 diff = steel * (0.25 + 0.75*nl) * (1.0 - 0.85*metal);

  // 直接光の鏡面高光(Blinn-Phong。粗さ→光沢指数。核显向けに安価)。研いだ面は滑らか=鋭い高光。
  float  shin = lerp(128.0, 8.0, lerp(rough, GROUND_ROUGH, e));   // 小rough=鋭い/大rough=広い
  float3 specTint = lerp((float3)1.0, steel, metal);       // 金属は高光が地色に色付く
  float3 spec = specTint * pow(nh, shin) * specK * nl * (1.0 + GROUND_SPEC_BOOST * e);

  // 擬似環境反射(HDRI無し): 反射向きの上下で空色↔地色を補間=「周囲を映す」金属感の主因。
  float3 R   = reflect(-V, N);
  float3 env = lerp(grdCol, skyCol, saturate(R.y*0.5+0.5));
  float  fre = fresK * pow(1.0 - nv, 5.0);                 // 縁で反射が強まる(菲涅尔)
  float3 refl = env * (envK + fre) * lerp(0.15, 1.0, metal) * steel;

  float3 cold = diff + spec + refl;                        // 冷: 金属らしくライティング
  // 熱: 温度色で発光。旧は法線を一切使わず(面の向きに関係なく同じ明るさ)=倒角/凹凸/表裏が平らに潰れて見えた。
  //   → 発光にも面の向きの明暗を掛ける。ハーフランバート(Half-Lambert: N・L を 0..1 に寄せる)=背光面も真っ黒にならない。
  //   → 縁(視線に対して寝ている面)を温度色で明るくする(菲涅尔型のリムライト)=輪郭と刃の稜線が浮く。
  float  halfLambert = saturate(dot(N, L) * 0.5 + 0.5);
  float  shade = lerp(hotShade, 1.0, halfLambert);
  float  rim   = rimK * pow(1.0 - nv, rimPow);
  float3 hot  = (i.col.rgb * (0.35 + 0.65*steel) * shade + i.col.rgb * rim + spec) * hotGain;
  float  k    = smoothstep(0.06, 0.45, i.col.a);           // 温度(col.a)で冷→熱をブレンド
  float3 col  = lerp(cold, hot, k);

  // --- 氧化皮(黒皮): 叩いた面ほど剥がれる ---
  //   マスクの灰度=皮の厚み。この面の鍛造進捗 work を閾値にして「閾値より薄い皮」から消す
  //   =進捗が上がるにつれ、薄い所から少しずつ剥がれ、最後に厚い所が落ちる(突然消えない)。
  //   研いだ所(sharp)は削られて皮が無い。冷えても皮は残る(本物と同じ)。
  float  m       = scaleMask.Sample(samp, i.uv * scaleTiling).r;
  //   開局(work=0)でも scaleStart 以下の薄い皮は既に剥がれている=炉から出したての「ひび割れて斑な黒皮」。
  //   一様に全面を覆うと皮に見えず「刃の地の色」に見えてしまった(2026-10-03 F5 で判明)。
  float  thr     = lerp(scaleStart, 1.0 + scaleSoft, saturate(i.work));
  float  covered = smoothstep(thr - scaleSoft, thr, m) * (1.0 - e);
  float3 scaleCold = SCALE_COLD_COL * (0.25 + 0.75 * nl);   // 艶の無い黒皮(拡散だけ)
  // 熱い時の黒皮: 黒い地に、暗い赤の鈍い光を少しだけ乗せる。温度色をそのまま使うと、黄白に光る地金と
  //   同じ色になって皮が溶け込み(特に開局の高温時)、ブルームにも埋もれた → 色は固定の暗赤、温度からは明るさだけ取る。
  float  heatLuma  = dot(i.col.rgb, LUMA_WEIGHTS);
  float3 scaleHot  = SCALE_COLD_COL + SCALE_EMBER * heatLuma * scaleGlow * shade;
  col = lerp(col, lerp(scaleCold, scaleHot, k), covered * scaleOpacity);
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
	// 火花の線は貼图でなく式で描く(PS_Spark)=どの大きさでもぼやけない。余燼/蒸気の丸い粒は PS_Particle のまま。
	PixelShader* sps = CreateObj<PixelShader>("PS_Spark");
	if (FAILED(sps->Load("Assets/Shader/PS_Spark.cso")))
		MessageBox(nullptr, "PS_Spark.cso", "Shader Error", MB_OK);

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
	// 草: VS_Wall と同じ出力 + 玩家を避けて倒れる(InteractionMap を読む)。PS は PS_Wall をそのまま使う。
	VertexShader* grassVS = CreateObj<VertexShader>("VS_Grass");
	if (FAILED(grassVS->Load("Assets/Shader/VS_Grass.cso")))
		MessageBox(nullptr, "VS_Grass.cso", "Shader Error", MB_OK);

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
		// 氧化皮マスクは「色」でなく「厚みのデータ」なので sRGB 変換をしない(false)=灰度がそのまま閾値と比べられる
		m_wpScaleMask = TextureCache::Get("Assets/Model/weapon/forge_scale_mask.png", false);
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
	m_grassMap.Uninit();	// 草の踏み跡の貼图(ブレンドステートを解放)
	DestroyObj("VS_Forge");
	DestroyObj("PS_Forge");
	DestroyObj("PS_Spark");
	DestroyObj("VS_ForgeObj");
	DestroyObj("PS_ForgeObj");
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
	// 出る所 = 叩いた所: ハンマーの真下(照準点 m_aimWorld の水平位置)の、刃の上面
	//   (旧: (0,1,0) のベタ書き=金床がどこにあっても同じ所から出ていた)。上面 = 刃の中心 + 中心→表面の深さ。
	XMFLOAT3 origin(m_aimWorld.x, WorkAnchor().y + BladeDepthBelowCentre(), m_aimWorld.z);
	m_particles.SpawnSparks(origin, N, m_power, scale);
}

// 目標形状(BuildTarget)と一致度(ShapeMatch)は Physics/ForgingSim へ移動した。

//====================================================================
//  ゲーム進行
//====================================================================
void SceneForge::StartGame()
{
	// BGM はここでは切り替えない: カメラが金床へ移る間にタイトル BGM を淡出し、着いてから切り替える(FinishIntro)。
	m_heatSndOn = false;						// 加熱持続音の状態をリセット
	m_state    = GAME_PLAY;
	// 状態は一旦「走動」にし、UpdateTitle が直後に金床への導入運鏡を始める(着くと工位=鍛打へ)。
	m_walkMode = true;
	m_modeTrans = ModeTrans::None;	// 走動⇔工位の移動アニメも解除
	m_focus = -1; m_promptAlpha = 0.0f;	// 互動の状態も初期化
	m_walkPitch = 0.0f;
	m_player.Init(DirectX::XMFLOAT3(0.0f, m_walkFloorY, -2.0f), 0.0f);	// 開始位置/向きを戻す
	m_door.Reset();			// 裏口の扉は閉じた状態から
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
	ClearGrindHint(); m_grindPress = 0.0f;
	m_grindU = 0.5f; m_grindFace = 0; m_grindFlipRoll = 0.0f; m_grindSparkAcc = 0.0f; m_grindVol = 0.0f; m_grindAngle = m_grindAngleTarget = 0.0f;
	m_plunge = 0.0f; m_quenchTurn = 0.0f; m_agitate = 0.0f; m_quenchContact = false; m_clearDecided = false; m_letterbox = 0.0f; m_boil = 0.0f; m_boilStage = BoilStage::None; m_waterSim.Reset(); m_prevAgitate = 0.0f; m_bubbleAcc = 0.0f;
	m_stirTarget = 0.0f; m_filmBreak = 0.0f; m_filmTime = 0.0f; m_stir01 = 0.0f; m_quenchStartHeat = 0.0f;	// 淬火/終幕の演出も解除
	if (m_burnSndOn)  { Audio::Stop(Audio::SE_BURN_LOOP);  m_burnSndOn  = false; }
	if (m_grindSndOn) { Audio::Stop(Audio::SE_GRIND_LOOP); m_grindSndOn = false; }

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
	m_stepChangedAt = m_time;	// 指引 UI: 最初の案内文も淡入させる
	m_trackerRows.clear();		// 工程リストの済状態を作り直す(UpdateTracker が配方の工程数で用意する)
	StopSequence();				// 前の回の拍子表が途中なら打ち切る
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
	m_heatStep->RegisterState(m_stepMachine);
	m_forgeStep->RegisterState(m_stepMachine);
	m_grindStep->RegisterState(m_stepMachine);
	m_quenchStep->RegisterState(m_stepMachine);
}

//--- 次の工程へ進む。配方(データ)が順序の正。最後の工程を越えたら完成(淬火済み)へ。
void SceneForge::AdvanceStep()
{
	++m_stepIdx;
	m_stepChangedAt = m_time;	// 指引 UI: 新しい案内文を淡入+工程リストで強調
	if (!m_recipe || m_stepIdx >= (int)m_recipe->steps.size()) { FinishGame(); return; }
	m_stepMachine.ChangeState(StepKey(m_recipe->steps[m_stepIdx].type));
}

//--- 【デバッグ】工程へ直接飛ぶ(淬火などを最初から通さずに試す為。F1 最上段の「Jump to step」)。
//    新しい一局として始め直し(StartGame)、配方で前にある工程を「済」にしてから、その工程・その工位から始める。
//    温度はその工程を試せる値にする: 加熱 = 冷えた鉄 / 鍛造・研磨 = 開始時の熱 / 淬火 = 淬火できる窓の真ん中。
void SceneForge::DebugJumpToStep(int idx)
{
	if (!m_recipe || idx < 0 || idx >= (int)m_recipe->steps.size()) return;
	StartGame();

	for (int k = 0; k < idx; ++k)
	{
		switch (m_recipe->steps[k].type)
		{
		case StepName::Forge: m_forging.CompleteForging();  break;
		case StepName::Grind: m_forging.CompleteGrinding(); break;
		default: break;	// 加熱/淬火は形に残らない
		}
	}

	const StepName type = m_recipe->steps[idx].type;
	switch (type)
	{
	case StepName::Heat:   m_forging.SetHeat(COLD_LIMIT); break;	// 冷えた鉄(すぐ「済」にならない)
	case StepName::Quench: m_forging.SetHeat((QUENCH_MIN_TEMP + QUENCH_MAX_TEMP) * 0.5f); break;
	default:               m_forging.SetHeat(START_HEAT); break;
	}

	m_stepIdx = idx;
	m_stepChangedAt = m_time;
	m_trackerRows.clear();
	m_stepMachine.ChangeState(StepKey(type));

	// その工程の工位に鉄を置き、玩家もそこで作業中にする(歩いて運ぶ手間を省く。過渡アニメ無し)
	const Station s = StepStation(type);
	m_workAt = s; m_station = s;
	SetupStationView(s);
	m_walkMode = false;
	m_modeTrans = ModeTrans::None;
	m_lookYaw = 0.0f; m_lookPitch = 0.0f;
	m_canStrike = false;
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
	// 翻面は工程に関係なく、金床にいればいつでも F で始められる(玩家の自由)。
	// 入力凍結中(F1/遷移)は「一時停止」: 段階も計時も進めない。打ち切らない=動画は取り消されない。
	if (!inputOn) return;

	// 運鏡ビート中に押されたキーは「読んで捨てる」。IsKeyTrigger は押した瞬間の1フレームだけ true なので、
	// ビート中に判定しなければ、そのキーはビート明けに持ち越されない(=バッファされず暴発しない)。
	switch (m_flipPhase)
	{
	case FlipPhase::None:									// 金床で: F で火钳を取って翻面を起動
		if (m_station == Station::Anvil && IsKeyTrigger('F')) { m_flipPhase = FlipPhase::TongsOut; m_flipTimer = 0.0f; }
		break;

	case FlipPhase::TongsOut:								// 火钳を取り出す運鏡(慢い)
	{
		m_flipTimer += tick;
		// 腰へ振り向いて静止している区間の中点=「手に取った」瞬間。腰の火钳を消す。
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
		// TongsOut と対称: 振り向いた静止区間の中点で火钳を腰へ戻す(モデルを再表示)。
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
void SceneForge::UpdateTitle(float /*tick*/)
{
	// 黒転じは使わない: タイトルはゲーム世界そのもの。SPACE → ロゴ淡出 → カメラが金床へ → 鍛打(Title.cpp UpdateIntro)。
	if (m_introPhase == IntroPhase::None && IsKeyTrigger(VK_SPACE) && !m_fade.IsBusy())
	{
		m_introPhase = IntroPhase::LogoFade;
		m_introTimer = 0.0f;
		Audio::Play(Audio::SE_TITLE_FADE, TITLE_FADE_SE_VOLUME);
	}
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
	bool inputOn = !DebugUI::IsVisible() && !m_fade.IsBusy() && !m_walkMode && !Transitioning();

	// --- 工位から出る: E または ESC(汎用の「戻る」)。翻面中は出ない(火钳を持ったまま離れない)。
	//   翻面中の ESC は UpdateFlip が「一段戻る」として扱う(Flipping→Ready→火钳を戻す)。
	//   淬火の動画(刃が水に入った後)も取り消せない=出られない。
	const bool quenchLocked = (m_quenchTurn > 0.0f || m_plunge > 0.0f);	// 刃を立て始めたら取り消せない
	if (inputOn && m_flipPhase == FlipPhase::None && !quenchLocked && (IsKeyTrigger('E') || IsKeyTrigger(VK_ESCAPE)))
	{
		m_charging = false; m_charge = 0.0f;	// 蓄力中なら破棄(暴発させない)
		BeginExitStation();
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
	//   風箱(R長押し)は炉の工位で玩家が行う行為なので inputOn の時だけ=炭火に追加で熱を送る。
	const bool simOn  = !DebugUI::IsVisible() && !m_fade.IsBusy();
	const bool inFire = !m_carrying && m_workAt == Station::Hearth;
	const bool bellows = inputOn && m_station == Station::Hearth && IsKeyPress('R');
	if (simOn)
	{
		if (inFire)
		{
			// 火の中: 周囲=火なので、火の温度へ指数的に近づく(ニュートンの法則。室温への自然冷却の代わり)。
			const float fireTemp = bellows ? BELLOWS_FIRE_TEMP : COAL_FIRE_TEMP;
			const float k        = bellows ? BELLOWS_HEAT_K    : COAL_HEAT_K;
			m_forging.AddHeat((fireTemp - m_forging.Heat()) * (1.0f - expf(-k * tick)));
		}
		else m_forging.Cool(tick);							// 火の外: 鉄が自分で冷める
		UpdateBoil(tick);	// 水中: 沸騰の段階ごとの速さで冷える + 沸き立ち(蒸気/水面)。水の外では何もしない
	}
	// 風箱を踏んでいる間は炉火の唸りをループ。離した(またはF1/遷移で入力停止)瞬間に停止。
	if (bellows && !m_heatSndOn)      { Audio::PlayLoop(Audio::SE_FORGE_LOOP, 0.5f); m_heatSndOn = true; }
	else if (!bellows && m_heatSndOn) { Audio::Stop(Audio::SE_FORGE_LOOP);           m_heatSndOn = false; }

	// --- 過熱で放置すると鋼全体が焼けていく(損傷が蓄積)＋ジュー音 ---
	//   冷却と同じく鉄の物理なので simOn で進める(F1中は温度と一緒に止まる)。
	if (simOn && m_forging.Heat() > OVERHEAT)
	{
		m_forging.BurnAll(BURN_RATE * tick);	// 過熱で鉄全体が焼ける(損傷が蓄積)
		m_sizzleTimer -= tick;
		if (m_sizzleTimer <= 0.0f) { Audio::Play(Audio::SE_SIZZLE); m_sizzleTimer = 0.22f; }
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
	//   完了判定に入力を読む工程があるので、入力凍結中(F1/遷移中)は進めない。
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
	const int  side        = m_forging.Side();
	const bool faceWasDone = m_forging.SideDone(side);
	ForgingSim::StrikeOutcome outcome =
		m_forging.ApplyStrike(ci, cj, seg, power, heatFactor, grooveMult, cold, over);
	// この一打で「上を向いている面」全体が仕上がった瞬間(未完成→完成のエッジ検出)=口笛で「この面は終わり、裏返せ」。
	//   区域ごとの完成は音を鳴らさず、最後の黒皮が落ちる見た目だけで伝える(ユーザー決定 2026-10-04: 視覚=どこ / 音=面の完成)。
	const bool faceJustDone = !faceWasDone && m_forging.SideDone(side);

	// 温度が下がる / 火花 / 振動
	m_forging.AddHeat(-STRIKE_COOL);	// 打撃で熱が金床/鎚へ逃げる
	float sparkScale = (0.4f + power * 1.2f) * (over ? 0.7f : 1.0f);
	if (!cold) Strike(sparkScale);

	// 打撃音: 冷打は鈍い音。通常打撃は金床音を 1→2→1→2 と交互に鳴らす
	if (cold) Audio::Play(Audio::SE_COLD, 0.9f);
	else
	{
		// リズムに乗った打撃は、金床が少し高く澄んで鳴る(口笛の代わりのリズムの手応え。口笛は区域完成の合図専用)
		Audio::Play(m_hammerAlt ? Audio::SE_ANVIL2 : Audio::SE_ANVIL1, 0.55f + power * 0.45f,
		            inGroove ? GROOVE_RING_PITCH : 1.0f);
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
		if (inGroove) quality += GROOVE_QUALITY_BONUS;
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
}

//--- 研磨: 右クリック点按=足踏み(GrindWheel が回転を持つ)、左長押し=押し当て、マウス左右=刃を滑らす。
//    押し当てた区域の鋭さが「砥石の回転速度」に比例して上がる(速く回すほど早く研げる)。
void SceneForge::UpdateGrind(float tick, bool inputOn)
{
	const int NSEG = ForgingSim::NSEG;
	const bool here = inputOn && m_station == Station::Grindstone;
	bool pressing = false;
	if (here)	// 工程に関係なく、砥石の工位にいればいつでも研げる(玩家の自由)
	{
		float dx, dy; ReadMouseDelta(dx, dy);	// 砥石の工位ではマウスを視角でなく刃の滑りに使う
		// 刃が右へ動く=砥石に当たる位置は刃の左側へ移る(刃の長軸は StationRight に揃えてある)
		// 左右反転して置いた(m_restFlip)時は長手の向きが逆 → 画面上の手応え(マウス右=刃が右)を保つ為に符号も逆
		m_grindU -= dx * m_grindSens * (m_restFlip ? -1.0f : 1.0f);
		if (m_grindU < 0.0f) m_grindU = 0.0f;
		if (m_grindU > 1.0f) m_grindU = 1.0f;
		// 上下 = 研ぎ角(マウスを上へ = 立てる)。刃は少し遅れて追う(手で角度を保つ重さ)。0 = 平らに寝ている
		m_grindAngleTarget = fminf(fmaxf(m_grindAngleTarget - dy * GRIND_ANGLE_SENS, 0.0f), GRIND_ANGLE_MAX);
		// F = 刃を裏返してもう片方の刃を研ぐ(鍛造の F と同じ操作。ユーザー指定 2026-10-07)
		if (IsKeyTrigger('F')) m_grindFace ^= 1;
		if (IsKeyTrigger(VK_RBUTTON) || IsKeyTrigger(VK_SPACE)) m_wheel.Pedal();	// 点按=足で一回踏む(右クリック/Space。速すぎる連打は GrindWheel が無視)
		pressing = IsKeyPress(VK_LBUTTON);
	}
	m_wheel.Update(tick, pressing);			// 足を止めれば摩擦で止まる。押し当て中は余計に減速
	m_grindFlipRoll = Lerp::Damp(m_grindFlipRoll, m_grindFace * XM_PI, GRIND_FLIP_LAMBDA, tick);	// 裏返しは半回転を滑らかに
	m_grindPress    = Lerp::Damp(m_grindPress, pressing ? 1.0f : 0.0f, GRIND_PRESS_LAMBDA, tick);	// 押し当て=浮いた所から砥石へ下ろす
	m_grindAngle = Lerp::Damp(m_grindAngle, m_grindAngleTarget, GRIND_ANGLE_FOLLOW, tick);

	const float speed = m_wheel.Speed01();
	constexpr float MIN_GRIND_SPEED = 0.02f;	// これ未満=砥石がほぼ止まっている(研げない・火花も出ない)
	const bool grinding = pressing && speed > MIN_GRIND_SPEED;
	bool onDoneSpot = false;	// 研ぎ上がった所を研いでいる(火花が細り、音が軽く高くなる=もう削る物が無い)
	if (grinding)
	{
		int seg = (int)(m_grindU * NSEG);
		if (seg >= NSEG) seg = NSEG - 1;
		const float eff = GrindAngleEfficiency();	// 角度が正しいほど速く研げ、火花も多い
		const int  side = GrindSide();	// 砥石へ向いている刃の面
		onDoneSpot = m_forging.SharpDoneOf(side, seg);
		const bool faceWasDone = m_forging.SharpProgress(side) >= 1.0f;
		// 誤りは独白で知らせ(1回の独白 = 1回の誤り)、同じ誤りをくり返したら直し方の案内を出す(DrawGrindHint)。
		if (eff > 0.0f)
		{
			if (m_forging.ApplyGrind(side, seg, GRIND_RATE * speed * eff * tick) == ForgingSim::GrindOutcome::AlreadySharp)
			{
				if (m_popupLife <= 0.0f)
				{
					Say((const char*)u8"そこはもう十分だ", IM_COL32(255, 200, 90, 255));	// 研ぎ上がった所を削る=誤り
					CountGrindMistake(m_grindDoneMistakes, GrindHint::Slide);
				}
			}
			else ClearGrindHint();	// 正しく研げた
		}
		else if (m_popupLife <= 0.0f)
		{
			// 角度が外れて研げていない: どちらに外れたかだけ独白で言う(正しい角度そのものは教えない=KCD式の負向フィードバック)
			const bool tooFlat = fabsf(m_grindAngle) < GRIND_IDEAL_ANGLE;
			Say(tooFlat ? (const char*)u8"寝かせすぎだ…刃に当たっていない" : (const char*)u8"立てすぎだ…刃先が丸まる",
			    IM_COL32(255, 200, 90, 255));
			if (tooFlat) CountGrindMistake(m_grindFlatMistakes,  GrindHint::TiltUp);
			else         CountGrindMistake(m_grindSteepMistakes, GrindHint::TiltDown);
		}

		// この面の刃が全部研ぎ上がった瞬間: 「完成」の合図(鍛造の面の完成と同じ音。両面とも済めば一段高く)
		if (!faceWasDone && m_forging.SharpProgress(side) >= 1.0f)
			Audio::Play(Audio::SE_FACE_DONE, FACE_DONE_VOLUME, m_forging.AllSharp() ? FACE_DONE_FINAL_PITCH : 1.0f);

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
	if (m_grindSndOn)
	{
		const float pitch = (GRIND_PITCH_MIN + (GRIND_PITCH_MAX - GRIND_PITCH_MIN) * speed) * (onDoneSpot ? GRIND_DONE_PITCH_MUL : 1.0f);
		Audio::SetLoop(Audio::SE_GRIND_LOOP, m_grindVol * GRIND_SND_VOLUME, pitch);
		if (!atGrind && m_grindVol < GRIND_SND_OFF)	// 工位を離れ、音が消えきってから止める
		{
			Audio::Stop(Audio::SE_GRIND_LOOP);
			m_grindSndOn = false;
		}
	}
}

void SceneForge::CountGrindMistake(int& count, GrindHint hint)
{
	if (++count >= m_hintAfterMistakes) m_grindHint = hint;
}

void SceneForge::ClearGrindHint()
{
	m_grindHint = GrindHint::None;
	m_grindFlatMistakes = m_grindSteepMistakes = m_grindDoneMistakes = 0;
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
		Say((const char*)u8"冷めてしまった…炉でもう一度熱してから", IM_COL32(120, 170, 255, 255));
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
	Audio::Play(Audio::SE_QUENCH, 0.9f);	// 水に入った瞬間の「ジュワッ」
	Audio::Play(Audio::SE_STEAM,  0.9f);	// 続く大量の蒸気「シュワーーッ」
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
	m_stirTarget -= dy * QUENCH_STIR_SENS;								// マウスを上へ = 刃が上へ(画面の y は下向き)
	m_stirTarget  = Lerp::Damp(m_stirTarget, 0.0f, QUENCH_STIR_RETURN, dt);
	m_stirTarget  = fminf(fmaxf(m_stirTarget, -QUENCH_STIR_RANGE), QUENCH_STIR_RANGE);

	const float before = m_agitate;
	m_agitate = Lerp::Damp(m_agitate, m_stirTarget, QUENCH_STIR_FOLLOW, dt);
	const float moved = fabsf(m_agitate - before);

	const float MIN_DT = 1e-4f;
	const float speed01 = fminf(moved / fmaxf(dt, MIN_DT) / STIR_FULL_SPEED, 1.0f);
	m_stir01 = Lerp::Damp(m_stir01, speed01, STIR_SPEED_LAMBDA, dt);

	if (m_boilStage == BoilStage::Film)
	{
		m_filmBreak += moved;
		if (m_filmBreak >= FILM_BREAK_WORK) m_boilStage = BoilStage::Nucleate;	// 揺すって膜を破った
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
	if (m_quenchContact) UpdateBladeWaterline();
}

//--- 終幕へ入る。HUD を消してよいのは「ゲームが必ず終わる」と確定した時だけ
//    (確定前に消すと、まだ続くのに UI が無くなる)。淬火が配方の最後の工程である事をデータで確かめる
//    =別の配方で淬火の後に工程がある武器でも誤って消さない。
void SceneForge::BeginFinale()
{
	m_clearDecided = m_recipe && m_stepIdx == (int)m_recipe->steps.size() - 1;
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
	if (IsKeyTrigger(VK_SPACE) && !m_fade.IsBusy())
	{
		m_fade.Transition([this] {
			m_state = GAME_TITLE;
			ResetTitleStage();	// 金床の上を「新しい熱い鉄」に戻す(完成した剣は片付ける)
			Audio::Stop(Audio::BGM_RESULT);				// 結果BGMを止める
			Audio::PlayLoop(Audio::BGM_TITLE, TITLE_BGM_VOLUME);	// タイトルBGMを再開
		});
	}
}

void SceneForge::Update(float tick)
{
	m_time += tick;
	m_fade.Update(tick);	// 画面フェード(黒幕)を進める。遷移はTransitionの黒転じで実行される
	m_door.Update(tick);	// 扉の開閉の回転(E で切り替えた後、入力と無関係に最後まで回り切る)
	m_grassMapDt += tick;	// 草の踏み跡が薄れる時間(GPU の Fade は Draw の最初でまとめて)

	// F8 = 全調整値を起動時スナップショットへ一発リセット(F1デバッグ表示中のみ=誤爆防止)。
	if (DebugUI::IsVisible() && IsKeyTrigger(VK_F8)) RestoreTuning();
	// PLAY中かつF1非表示・遷移中でないときだけ操作を受け付ける(ApplyCameraより先に)。
	bool canControl = (m_state == GAME_PLAY && !DebugUI::IsVisible() && !m_fade.IsBusy());
	if (canControl)
	{
		// 互動の注視判定(①範囲 ②視線)。走動中以外(工位/移動アニメ中)は対象なし=提示がフェードアウト。
		UpdateInteract(tick);
		UpdateGuide();		// 指引 UI: 今の状況から案内文と行き先を決める(HUD.cpp)
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
			//   金床=工位へ移動 / 火钳=取って工位へ移動→翻面。退出(工位→走動)は UpdatePlay 側で E/ESC。
			if (m_player.WantInteract() && m_focus >= 0) DoInteract(INTERACTABLES[m_focus]);
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
		else if (m_station != Station::Anvil)
		{
			// 金床以外の工位は固定カメラ。砥石だけはマウス左右を刃の滑りに使う(UpdatePlay→UpdateGrind が読む)。
			// それ以外(炉/水槽)は読んで捨てる=光標を中心へ戻し、戻った時に視点が跳ばない。
			// 水槽で揺すっている間はマウスの上下を刃に使う(QuenchStep→StirQuench が読む)。
			if (m_station != Station::Grindstone && !QuenchStirring()) { float dx, dy; ReadMouseDelta(dx, dy); }
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
	UpdateViewmodelSway(tick);	// 手に持った物がカメラの向きへ少し遅れて付いて来る(武器の揺れ。Carry.cpp)
	UpdateBarAnchor();	// 金床の砧面の高さに鉄条を自動配置

	// PLAY中はOSカーソルを隠す(照準は光るセグメントで示す)。デバッグUI表示中は出す
	bool wantCursor = (m_state != GAME_PLAY) || DebugUI::IsVisible();
	if (wantCursor != m_cursorShown) { ShowCursor(wantCursor); m_cursorShown = wantCursor; }

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
	UpdateWaterSim(tick);	// 水槽の水面の波(刃・泡で押し、波動方程式で広げる)
}


void SceneForge::Draw()
{
	UpdateGrassMap();	// 草の踏み跡の貼图を更新(一時的に描画先を差し替え→戻す)。シーンを描き始める前に
    CottageRender::ClearExterior();
	ApplyViewCamera();	// Update と同じ規約でDrawでも適用(GetViewの前に)
	DrawModelsTest();	// 先に不透明な3Dモデル(金床)を描く
	// 鉄は置かれた工位に描く。運んでいる間は手のビューモデルとして最後に描く(DrawViewmodel。めり込み防止)。
	// 拍子表の再生中は普通に描く(置く時、炉の中などへ入って行く鉄が道具に正しく隠れる様に)。
	if (!m_carrying || SequencePlaying())
	{
		if (m_wpOk) DrawWeapon();	// Blender武器モデルを進捗でモーフ(あれば優先)
		else        Draw3DBillet();	// 無ければ従来の高さ場メッシュ
		if (m_wpOk && !m_carrying) DrawGhostTarget();	// 実体の後に完成形の半透明ゴーストを重ねる
	}
	DrawWater();		// 水槽の水面(屈折。背後のシーンを撮ってから描く=不透明の後)
	if (DebugUI::IsVisible()) { DrawDebugBoxes(); DrawInteractBoxes(); }	// F1中はAABB/箱・互動範囲を線で表示
	if (m_showCollision) DrawCollision();	// 衝突形状(F1 最上段「Show collision」で ON。壁越しに透視。F1を閉じて歩きながらも見られる)

	DrawEmbers();		// 炭火から立ち上る余燼(火花描画より前に。火花が無くても出す)
	DrawSteam();		// 淬火の蒸気(柔らかい白い煙)
	DrawViewmodel();	// 手に持った鉄+火钳(深度範囲を詰めて手前に。運んでいない時は何もしない。Carry.cpp)

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


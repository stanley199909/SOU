#include "LitRender.h"
#include "SunStage.h"
#include "ReflectionProbe.h"
#include "CottageRender.h"
#include "TextureCache.h"
#include "PropMaterials.h"
#include "imgui/imgui.h"

using namespace DirectX;

namespace
{
	LightToggles s_toggles;	// F2 パネルの光源 ON/OFF(全員がここを読む)
	// 時刻(SunCycle)が決める太陽。SetSun が呼ばれるまでは保存した向き・昼の明るさ(編集シーン等)
	XMFLOAT3 s_sunDir   = { 0.0f, 1.0f, 0.0f };
	float    s_daylight = 1.0f;
	bool     s_sunSet   = false;
	const float NIGHT_FILL = 0.35f;	// 夜の環境光 = 昼の何割(月・星・外の残り光)。0.12 では夜に何も見えなかった(2026-10-17)。ゲームとして読める明るさを優先
	const float NIGHT_SKY  = 0.015f;	// 夜の空(窓の外)の明るさ = 昼の何割(ほぼ黒い紺。0.04 では夜でも灰色に見えた)
	float s_firePos[3] = {};
	float s_fireScale  = 1.0f;
	// F1 の見比べ用(既定 = 全部使う)。保存はしない(確かめる為だけの切り替え)
	bool s_useNormalMaps = true;
	bool s_useOrmMaps    = true;
	bool s_flipNormalY   = false;	// 表の DirectX/OpenGL 式の指定を全部逆にする(凹凸が逆に見えたら表が間違い)

	// PS_StageProp の cbuffer PropParam と同じ並び(順番を変えたら両方直す)
	struct PropParams
	{
		XMFLOAT4   tint;
		SceneLightParams light;	// eye, sun, ambient, fire, lightVP, shadow
		XMFLOAT4   material;	// x 三平面投影, y 物の大きさ(倍率), z 粗さ, w 金属度
		XMFLOAT4X4 worldToLocal;
		XMFLOAT4   maps;		// x 法線マップ有り, y ORM マップ有り, z 法線マップが DirectX 式, w 次の工位の光(0..1)
		XMFLOAT4   hotSpot;	// xyz 熱い所(ワールド), w 半径(m)。焼き戻しで炭の上にかざした火钳の口
		XMFLOAT4   hotGlow;	// x 熱さ 0..1(0 = 無し)
	};
	static_assert(sizeof(PropParams) == 288, "must match cbuffer PropParam in PS_StageProp.hlsl");

	// 画像のパス → 画像(無ければ nullptr)。TextureCache は失敗を覚えないので、パスが nullptr の時は読みに行かない
	Texture* Optional(const char* path, bool& has)
	{
		has = false;
		if (!path) return nullptr;
		std::shared_ptr<Texture> t = TextureCache::Get(path, false);	// 法線/ORM は色でなくデータ = リニアのまま読む(sRGB 変換しない)
		has = (t != nullptr);
		return t.get();
	}
}

namespace LitRender
{
	void SetFire(const float pos[3], float intensityScale)
	{
		for (int i = 0; i < 3; ++i) s_firePos[i] = pos[i];
		s_fireScale = intensityScale;
	}

	SceneLightParams Lights(CameraBase* camera)
	{
		SceneLightParams l{};
		if (!SunStage::Init()) return l;
		const auto& r     = SunStage::Data();
		const auto& light = CottageRender::Data();	// 光の情報はここだけから読む
		const XMFLOAT3 eye = camera ? camera->GetPos() : XMFLOAT3(0, 0, 0);
		const LightToggles& on = s_toggles;
		l.eye     = { eye.x, eye.y, eye.z, 0.0f };
		l.sun     = Sun();	// 今の太陽(時刻で動く向き × 昼の明るさ × F2 の ON/OFF)
		l.ambient = { light.ambient.x, light.ambient.y, light.ambient.z, light.windowExtra.w * FillScale() };	// 色 + 屋内の環境光の強さ(夜は暗い)
		l.fire    = { s_firePos[0], s_firePos[1], s_firePos[2], light.ambient.w * s_fireScale * (on.fire ? 1.0f : 0.0f) };	// 位置 + 炉の火の強さ(F1 の調整値 × ゲームの状態)
		l.lightVP = r.lightVP;
		l.shadow  = { r.hasShadow ? 1.0f : 0.0f, 1.0f / SunStage::ShadowSize, SunStage::ShadowDepthBias, SunStage::ReceiverOffset };
		return l;
	}

	void SetSun(const XMFLOAT3& towardSun, float daylight)
	{
		s_sunDir   = towardSun;
		s_daylight = daylight;
		s_sunSet   = true;
	}

	XMFLOAT4 Sun()
	{
		const XMFLOAT4& saved = CottageRender::Data().sun;	// w = 昼の太陽の強さ(調整値)。向きは時刻が決める(未設定なら保存値)
		const XMFLOAT3 dir = s_sunSet ? s_sunDir : XMFLOAT3(saved.x, saved.y, saved.z);
		return XMFLOAT4(dir.x, dir.y, dir.z, saved.w * s_daylight * (s_toggles.sun ? 1.0f : 0.0f));
	}

	// 夜も真っ暗にはしない(月明かり・星明かり・外の残り光)。床の値は調整値と別の固定の割合
	float FillScale() { return (s_toggles.ambient ? 1.0f : 0.0f) * (NIGHT_FILL + (1.0f - NIGHT_FILL) * s_daylight); }
	float SkyScale()  { return NIGHT_SKY + (1.0f - NIGHT_SKY) * s_daylight; }

	void BindShadow() { SunStage::BindShadow(); }
	float FireIntensityScale() { return s_fireScale; }
	bool NormalMapsEnabled() { return s_useNormalMaps; }
	LightToggles& Toggles() { return s_toggles; }

	// Draw と DrawMesh の共通部分: 行列・光・材質を GPU へ送り、深度/合成を決め、法線/ORM の画像を置く。
	//   この後は「メッシュごとに基本色を置いて描く」だけが違う
	static void BeginDraw(FXMMATRIX world, CameraBase* camera, const PropMaterial& mat, float highlight, const XMFLOAT4& hotSpot, float hot)
	{
		auto& r = SunStage::Data();

		XMFLOAT4X4 wvp[3];
		XMStoreFloat4x4(&wvp[0], XMMatrixTranspose(world));
		wvp[1] = camera->GetView();
		wvp[2] = camera->GetProj();
		r.vs.WriteBuffer(0, wvp);

		bool hasNormal = false, hasOrm = false;
		Texture* normalTex = s_useNormalMaps ? Optional(mat.normalTex, hasNormal) : nullptr;
		Texture* ormTex    = s_useOrmMaps    ? Optional(mat.ormTex,    hasOrm)    : nullptr;
		const bool normalDirectX = mat.normalDirectX != s_flipNormalY;

		PropParams p{};
		p.tint     = { mat.tint[0], mat.tint[1], mat.tint[2], 1.0f };
		p.light    = Lights(camera);
		p.material = { mat.triplanar ? 1.0f : 0.0f, XMVectorGetX(XMVector3Length(world.r[0])), mat.roughness, mat.metallic };
		XMStoreFloat4x4(&p.worldToLocal, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
		p.maps     = { hasNormal ? 1.0f : 0.0f, hasOrm ? 1.0f : 0.0f, normalDirectX ? 1.0f : 0.0f, highlight };
		p.hotSpot  = hotSpot;
		p.hotGlow  = { hot, 0.0f, 0.0f, 0.0f };
		r.propPS.WriteBuffer(0, &p);

		r.vs.Bind();
		SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
		SetBlendMode(BLEND_NONE);
		r.propPS.SetTexture(1, normalTex);
		r.propPS.SetTexture(2, ormTex);
	}

	static void EndDraw()
	{
		auto& r = SunStage::Data();
		r.propPS.SetTexture(1, nullptr);
		r.propPS.SetTexture(2, nullptr);
		SetBlendMode(BLEND_ALPHA);
	}

	void Draw(Model* model, FXMMATRIX world, CameraBase* camera, const PropMaterial& mat, float highlight, const XMFLOAT4& hotSpot, float hot)
	{
		if (!model || !camera || !SunStage::Init()) return;
		auto& r = SunStage::Data();
		BeginDraw(world, camera, mat, highlight, hotSpot, hot);
		for (unsigned i = 0; i < model->GetMeshNum(); ++i)
		{
			auto* mesh = model->GetMesh(i);
			r.propPS.SetTexture(0, model->GetTextureAt(mesh->materialID));
			r.propPS.Bind();
			SunStage::BindShadow();
			ReflectionProbe::BindForLighting();	// t6/t7 = 部屋の反射(IBL)。Bind() が空にした後に置く
			mesh->mesh->Draw();
		}
		EndDraw();
	}

	void DrawMesh(MeshBuffer* mesh, Texture* baseColor, FXMMATRIX world, CameraBase* camera, const PropMaterial& mat, float highlight)
	{
		if (!mesh || !camera || !SunStage::Init()) return;
		auto& r = SunStage::Data();
		BeginDraw(world, camera, mat, highlight, XMFLOAT4(0, 0, 0, 0), 0.0f);
		r.propPS.SetTexture(0, baseColor);
		r.propPS.Bind();
		SunStage::BindShadow();
		ReflectionProbe::BindForLighting();
		mesh->Draw();
		EndDraw();
	}

	void Controls()
	{
		if (!ImGui::CollapsingHeader("Prop materials (PBR)")) return;
		ImGui::Checkbox("Normal maps", &s_useNormalMaps);	// 切ると凹凸の陰影が消える = 法線マップの効果が分かる
		ImGui::Checkbox("ORM maps", &s_useOrmMaps);			// 切ると粗さ/金属度が表の定数になる(鎚の柄まで同じ質感)
		ImGui::Checkbox("Flip normal green (convention test)", &s_flipNormalY);
		ImGui::TextDisabled("Flip ON looks right (bolts/edges pop out)? -> the table's DX flag is wrong");
	}
}

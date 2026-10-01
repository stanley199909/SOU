// Part of SceneForge: インタラクティブ草(interactive grass)。
// This file carries a UTF-8 BOM so the Japanese comments compile correctly under MSVC.
//
// 流れ(毎フレーム):
//   Update: 経過時間を m_grassMapDt に溜めるだけ(GPU の処理は Draw でまとめて行う)。
//   Draw の最初(UpdateGrassMap):
//     ① Fade  : 貼图全体を exp(-dt/recoverTime) 倍 = 押した跡が時間で薄れる=草が戻る
//     ② Stamp : 玩家の足元に「圧力のドーム」を MAX で描く=今いる所は常に最大
//     ③ 描画先をシーンの RT + 深度へ戻す(貼图を描くために一時的に差し替えたので)
//   草の描画(DrawScenery): VS_Grass がこの貼图を読み、葉先を「圧力の下り坂」へ倒す。
// 貼图の仕組み自体(InteractionMap)は草を知らない=炭/床/水 など他の読み手にも使い回せる。
#include "SceneForge/SceneForge.h"
#include "SceneForge/SceneForge_Internal.h"
#include "PostProcess.h"
#include "DirectX.h"
#include "imgui.h"
#include <cmath>
#include <cstring>

using namespace DirectX;

bool SceneForge::IsGrass(const std::string& key) const
{
	return key.compare(0, strlen(GRASS_KEY_PREFIX), GRASS_KEY_PREFIX) == 0;
}

//--- 貼图の範囲 = 屋外の地面(StOutdoorGround)のワールドAABB を包む正方形(手で範囲を書かない)。
void SceneForge::InitGrassMap()
{
	Prop* ground = GetProp("StOutdoorGround");
	XMFLOAT3 mn, mx;
	if (!ground || !PropWorldBox(*ground, mn, mx)) return;	// 屋外が無い配置では草も無い
	const float size = fmaxf(mx.x - mn.x, mx.z - mn.z);	// (std::max は windows.h の max マクロと衝突する)
	const float cx = (mn.x + mx.x) * 0.5f, cz = (mn.z + mx.z) * 0.5f;
	m_grassMap.Init(cx - size * 0.5f, cz - size * 0.5f, size, GRASS_MAP_RESOLUTION);
}

void SceneForge::UpdateGrassMap()
{
	if (!m_grassMap.IsReady() || !g_pPost) return;
	m_grassMap.Fade(m_grassMapDt);
	m_grassMapDt = 0.0f;
	const XMFLOAT3 foot = m_player.GetPosition();
	m_grassMap.Stamp(foot.x, foot.z, m_grassStampRadius);

	// 描画先をシーンへ戻す(PostProcess::Begin が作った状態と同じに)
	RenderTarget* scene = g_pPost->GetSceneRT();
	SetRenderTargets(1, &scene, GetObj<DepthStencil>("DSV"));
	SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
	SetBlendMode(BLEND_ALPHA);
	SetCullingMode(D3D11_CULL_NONE);
}

//--- 草1株(プロップ)ぶんの定数と貼图を VS_Grass へ。根元と草丈はその株のワールドAABB から自動。
void SceneForge::BindGrassParams(VertexShader* vs, Prop& p)
{
	XMFLOAT3 mn, mx;
	if (!PropWorldBox(p, mn, mx)) return;
	const float bladeH = mx.y - mn.y;
	GrassParams gp;
	gp.area  = m_grassMap.AreaParams();
	gp.bend  = XMFLOAT4(m_grassLean * bladeH, m_grassPress * bladeH, mn.y, bladeH);
	gp.slope = XMFLOAT4(m_grassStampRadius, 0.0f, 0.0f, 0.0f);
	vs->WriteBuffer(1, &gp);
	vs->SetTexture(0, m_grassMap.GetTexture());
}

//--- F1: 貼图そのものを小窓に表示。白いほど強く押されている(R チャンネルだけなので赤く見える)。
void SceneForge::DrawGrassMapPreview()
{
	if (!m_showGrassMap || !m_grassMap.IsReady()) return;
	const float PREVIEW_RATIO = 0.3f;	// 小窓の一辺 = 画面の高さの 30%
	const float side = ImGui::GetIO().DisplaySize.y * PREVIEW_RATIO;
	ImGui::SetNextWindowSize(ImVec2(side + 16.0f, side + 60.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Grass interaction map");
	ImGui::TextDisabled("top-down orthographic view, +Z up");
	ImGui::Image((ImTextureID)m_grassMap.GetTexture()->GetResource(), ImVec2(side, side));
	ImGui::End();
}

#ifndef __POST_PROCESS_H__
#define __POST_PROCESS_H__

#include <DirectXMath.h>
#include <memory>
#include "Texture.h"	// RenderTarget / DepthStencil
#include "Shader.h"		// PixelShader

// ポストプロセス管理クラス
// シーンを一旦オフスクリーンRT(HDR)へ描画し、ブルーム → 自動露出 + ACES(+FXAA)で画面に合成する
class PostProcess
{
public:
	void Init(UINT width, UINT height);
	void Uninit();

	// 画面の大きさが変わった(設定の画面モード/解像度)。ブルームの半解像度の描画先を作り直す。
	// 場面の描画先と深度は次の Begin が内部解像度に合わせて作り直す
	void Resize(UINT width, UINT height);

	// 自動露出(目の慣れ)を進める
	void Update(float tick);

	// シーン描画の前後で呼ぶ
	void Begin(DepthStencil* pDSV);		// オフスクリーンRTへ切り替え
	void End(RenderTarget* pScreen);	// 画面へ効果を掛けて合成

	// Snapshot the scene rendered so far (opaque geometry) into an offscreen
	// texture, so transparent water can sample "what is behind it" and refract.
	// Call after opaque draw, before drawing water; returns the SRV texture to
	// bind on the water pixel shader. The scene RT stays bound as render target.
	Texture* CaptureScene();

	// The offscreen scene render target (so the water pass can re-bind it while
	// temporarily unbinding the depth buffer to sample it as a texture).
	RenderTarget* GetSceneRT() { return &m_sceneRT; }

private:
	// ブルーム合成
	void DrawBloom(int mode, Texture* src, float threshold, float dx, float dy, float tx, float ty);
	void DrawFXAA(Texture* src, float exposure);	// エッジ抗鋸歯(幾何の輪郭のチラつき)+ 露出 + ACES
	void DrawFull(Texture* src, Shader* ps, DirectX::XMFLOAT4 color);

private:
	RenderTarget                 m_sceneRT;	// シーンを描くオフスクリーンRT
	RenderTarget                 m_refractRT;	// water refraction: copy of scene behind the water
	RenderTarget                 m_brightRT;	// ブルーム用(高輝度抽出/縦ぼかし)
	RenderTarget                 m_blurRT;		// ブルーム用(横ぼかし)
	std::shared_ptr<PixelShader> m_bloomPS;	// ブルーム用ピクセルシェーダー
	std::shared_ptr<PixelShader> m_fxaaPS;	// FXAA(エッジ抗鋸歯)
	std::shared_ptr<PixelShader> m_tonemapPS;	// トーンマッピング専用(ACESでHDR→0..1に収光)
	UINT  m_width  = 0;
	UINT  m_height = 0;

	float m_bloomThreshold = 0.55f;	// ブルームの高輝度しきい値
	float m_bloomStrength  = 1.4f;	// ブルームの強さ
	bool  m_fxaa    = true;			// FXAA(エッジ抗鋸歯) ON/OFF
};

#endif // __POST_PROCESS_H__

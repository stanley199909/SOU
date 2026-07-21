#ifndef __POST_PROCESS_H__
#define __POST_PROCESS_H__

#include <DirectXMath.h>
#include <memory>
#include "Texture.h"	// RenderTarget / DepthStencil
#include "Shader.h"		// PixelShader

// ポストプロセス管理クラス
// シーンを一旦オフスクリーンRTへ描画し、効果を掛けて画面に合成する
//  ・モノクローム / セピア / モザイク / ポスタリゼーション
//  ・色収差(RGB Split) / ノイズ / CRT(授業外・おまけ)
//  ・矢印キーで切り替え、分割一括表示、時間による強度フェード
class PostProcess
{
public:
	void Init(UINT width, UINT height);
	void Uninit();

	// 入力・時間の更新
	void Update(float tick);

	// ImGuiの操作パネル
	void DrawUI();

	// シーン描画の前後で呼ぶ
	void Begin(DepthStencil* pDSV);		// オフスクリーンRTへ切り替え
	void End(RenderTarget* pScreen);	// 画面へ効果を掛けて合成

	// 画面下部などに出したい時用(現在の状態文字列)
	int  GetCurrent() const { return m_current; }
	bool IsSplit()    const { return m_split; }

	// 表示モード
	enum Mode
	{
		MODE_BLOOM,	// ブルーム(発光にじみ)…パーティクル向け・既定
		MODE_DEMO,	// 6種ポストプロセスのデモ(課題05)
	};

private:
	// 効果を1枚分描画する(クリップ空間の中心offset・サイズsize)
	void DrawEffect(int effect, float ox, float oy, float sx, float sy, float intensity);
	// ブルーム合成
	void DrawBloom(int mode, Texture* src, float threshold, float dx, float dy, float tx, float ty);
	void DrawFull(Texture* src, Shader* ps, DirectX::XMFLOAT4 color);

private:
	// GPUへ渡すパラメータ(16バイト境界に合わせる)
	struct Param
	{
		int   effect;		// 効果の種類
		float intensity;	// 効果の強さ(0..1)
		float time;			// 経過時間
		float _pad0;
		float resolution[2];// 画面解像度
		float _pad1[2];
	};

	RenderTarget                 m_sceneRT;	// シーンを描くオフスクリーンRT
	RenderTarget                 m_brightRT;	// ブルーム用(高輝度抽出/縦ぼかし)
	RenderTarget                 m_blurRT;		// ブルーム用(横ぼかし)
	std::shared_ptr<PixelShader> m_ppPS;	// 6種効果用ピクセルシェーダー
	std::shared_ptr<PixelShader> m_bloomPS;	// ブルーム用ピクセルシェーダー
	UINT  m_width  = 0;
	UINT  m_height = 0;

	int   m_mode    = MODE_BLOOM;	// 表示モード(既定はブルーム)
	int   m_current = 0;		// 単体表示中の効果番号
	bool  m_split   = true;		// 分割一括表示(起動時から6画面)
	bool  m_fade    = false;	// 時間による強度変化
	float m_time    = 0.0f;
	float m_bloomThreshold = 0.55f;	// ブルームの高輝度しきい値
	float m_bloomStrength  = 1.4f;	// ブルームの強さ

public:
	static const int EFFECT_MAX   = 7;	// 効果総数(単体切り替え用)
	static const int SPLIT_COUNT  = 6;	// 分割表示する数(課題の6種)
};

#endif // __POST_PROCESS_H__

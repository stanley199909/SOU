#ifndef __AUDIO_H__
#define __AUDIO_H__

// XAudio2 による簡易サウンド再生
//  ・Assets/Sound/<name>.wav があれば読み込み、無ければプログラムで合成した音を使う
//  ・Play(id, volume) で多重再生可能(ボイスプールをラウンドロビン)
namespace Audio
{
	enum SoundId
	{
		SE_WHISTLE,	// 良いリズムのときの口笛(効率アップの合図)
		SE_HAMMER,	// 打撃の「カン」(合成音フォールバック)
		SE_COLD,	// 冷打の鈍い「ドン」
		SE_SIZZLE,	// 過熱の「ジュー」
		SE_ANVIL1,	// 金床打撃1(実音源)。打撃ごとに1→2→1→2で交互再生
		SE_ANVIL2,	// 金床打撃2(実音源)
		BGM_MAIN,	// BGM(常時ループ)
		SE_TITLE,	// タイトル専用ループ(鉄を打つ男)。PLAY中は停止
		SE_MAX
	};

	void Init();
	void Uninit();
	void Play(SoundId id, float volume = 1.0f);		// 一回再生(効果音)
	void PlayLoop(SoundId id, float volume = 1.0f);	// 無限ループ再生(BGM/タイトル)
	void Stop(SoundId id);							// ループ停止
}

#endif // __AUDIO_H__

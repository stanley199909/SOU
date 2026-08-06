#ifndef __AUDIO_H__
#define __AUDIO_H__

// XAudio2 による簡易サウンド再生
//  ・Assets/Sound/<name>.wav があれば読み込み、無ければプログラムで合成した音を使う
//  ・Play(id, volume) で多重再生可能(ボイスプールをラウンドロビン)
namespace Audio
{
	enum SoundId
	{
		SE_BEAT,	// 金床の拍(リズムの基準)
		SE_HAMMER,	// 打撃の「カン」
		SE_COLD,	// 冷打の鈍い「ドン」
		SE_SIZZLE,	// 過熱の「ジュー」
		SE_MAX
	};

	void Init();
	void Uninit();
	void Play(SoundId id, float volume = 1.0f);
}

#endif // __AUDIO_H__

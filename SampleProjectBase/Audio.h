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
		SE_SWING,	// 空振り(鉄に当たらなかった)の「ヒュッ」
		SE_ANVIL1,	// 金床打撃1(実音源)。打撃ごとに1→2→1→2で交互再生
		SE_ANVIL2,	// 金床打撃2(実音源)
		BGM_MAIN,	// 工場の環境音(炉火/機械)。PLAY中に低音量で常時ループ(底噪)
		SE_TITLE,	// タイトル専用ループ(鉄を打つ男)。PLAY中は停止
		SE_QUENCH,	// 淬火(Q)＝水に入れる「ジュワ〜」(一回)
		SE_FORGE_LOOP,	// 加熱(R長押し)中の炉火/風箱の持続音。離すと停止
		SE_SUCCESS,	// 鍛造完了(成功)の合図(一回)
		SE_FAIL,	// 廃件(失敗/GameOver)の合図(一回)
		BGM_PLAY,	// ゲーム中BGM(PLAY状態でループ)
		BGM_RESULT,	// 結果画面BGM(RESULT状態でループ)
		SE_MAX
	};

	void Init();
	void Uninit();
	void Play(SoundId id, float volume = 1.0f);		// 一回再生(効果音)
	void PlayLoop(SoundId id, float volume = 1.0f);	// 無限ループ再生(BGM/タイトル)
	void Stop(SoundId id);							// ループ停止
}

#endif // __AUDIO_H__

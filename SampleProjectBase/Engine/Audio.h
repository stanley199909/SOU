#ifndef __AUDIO_H__
#define __AUDIO_H__

#include <DirectXMath.h>

// XAudio2 による簡易サウンド再生
//  ・Assets/Sound/<name>.wav があれば読み込み、無ければプログラムで合成した音を使う
//  ・Play(id, volume) で多重再生可能(ボイスプールをラウンドロビン)
namespace Audio
{
	enum SoundId
	{
		SE_FACE_DONE,	// 「完成した」の合図=鍛造/研ぎで面(表/裏)が仕上がった瞬間(2026-10-07: 口笛 → 上がっていく打音。Assets/Sound/SE/face_done.wav があればそれを使う)
		SE_HAMMER,	// 打撃の「カン」(合成音フォールバック)
		SE_COLD,	// 冷打の鈍い「ドン」
		SE_SIZZLE,	// 過熱の「ジュー」
		SE_SWING,	// 空振り(鉄に当たらなかった)の「ヒュッ」
		SE_ANVIL1,	// 金床打撃1(実音源)。打撃ごとに1→2→1→2で交互再生
		SE_ANVIL2,	// 金床打撃2(実音源)
		SE_QUENCH,	// 淬火(Q)＝水に入れる「ジュワ〜」(一回)
		SE_FORGE_LOOP,	// 加熱(R長押し)中の炉火/風箱の持続音。離すと停止
		SE_SUCCESS,	// 鍛造完了(成功)の合図(一回)
		SE_BURN_LOOP,	// 鋼が燃えている(火花を噴く)間のパチパチ音。燃えている間ループ
		SE_GRIND_LOOP,	// 砥石で研いでいる間の「シャー」。刃を押し当てている間ループ
		SE_STEAM,	// 淬火の大量の蒸気「シュワーーッ」(一回・長め。SE_QUENCH の後ろに重ねる)
		SE_TITLE_FADE,	// タイトルのロゴが淡出する時の効果音(一回。ユーザーが素材を用意。無ければ無音)
		SE_PENCIL,	// 工程リスト: 取り消し線を引く「シャッ」(鉛筆が紙を走る)。無ければ合成音
		SE_ERASER,	// 工程リスト: 取り消し線を消す「ゴシゴシ」(消しゴム)。無ければ合成音
		SE_LEARNED,	// チュートリアル: 操作を覚えた瞬間の確認音(短く明るい1音)。Assets/Sound/SE/learned.wav があればそれ、無ければ合成音
		SE_NEXT_STEP,	// 工程が進んだ合図(中央の札「〜完了 → 次」と同時。低い鐘2つ)。Assets/Sound/SE/next_step.wav があればそれ、無ければ合成音
		SE_SEG_DONE,	// 研ぎの区域が仕上がった「チン」(小さな区切り。面の完成 SE_FACE_DONE とは別)。Assets/Sound/SE/seg_done.wav があればそれ、無ければ合成音
		SE_BELLOWS_PUMP,	// ふいごを1回踏んだ「フッ」(押す毎に1回。炉火の唸りの上に乗る)。Assets/Sound/SE/bellows_pump.wav があればそれ、無ければ合成音
		// ※BGM_* は必ず最後にまとめる(IsBgm が BGM_TITLE..BGM_RESULT の範囲で判定する)
		BGM_TITLE,	// タイトル画面BGM(TITLE状態でループ)
		BGM_PLAY,	// ゲーム中BGM(PLAY状態でループ)
		BGM_RESULT,	// 結果画面BGM(RESULT状態でループ)
		SE_MAX
	};

	void Init();
	void Uninit();
	void Play(SoundId id, float volume = 1.0f, float pitch = 1.0f);	// 一回再生(効果音)。pitch=音程の倍率(1=原音, 2まで)
	void PlayLoop(SoundId id, float volume = 1.0f);	// 無限ループ再生(BGM/タイトル)
	void Stop(SoundId id);							// ループ停止
	void SetLoop(SoundId id, float volume, float pitch);	// 鳴っているループの音量/音程を止めずに変える(物理に追従させる)
	void ToggleBgmMute();							// BGMだけミュート切替(SEは鳴る)。録画用

	//--- 3D定位(3D positional audio)。XAudio2 付属の X3DAudio で、声源と聞き手の位置から左右の比率と距離の減衰を計算する。
	//    座標はゲームと同じワールド座標(左手系)。BGM/UI の合図は今まで通り Play/PlayLoop(=2D, 画面の中央で鳴る)。
	void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& front, const DirectX::XMFLOAT3& up);	// 聞き手=カメラ。front=視線の向き(長さは任意), up=上(front と直交していなくてよい)。毎フレーム呼ぶ(鳴っている3D音も向きに追従する)
	void PlayAt(SoundId id, const DirectX::XMFLOAT3& pos, float volume = 1.0f, float pitch = 1.0f);	// その位置から一回鳴らす
	void SetLoopPos(SoundId id, const DirectX::XMFLOAT3& pos);	// 鳴っているループを3Dにし、声源の位置を決める(PlayLoop の後に呼ぶ。動く物なら毎フレーム)
}

#endif // __AUDIO_H__

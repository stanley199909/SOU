#include "Audio.h"
#include <xaudio2.h>
#include <x3daudio.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "xaudio2.lib")

namespace
{
	//--- 1つの効果音(フォーマット＋波形＋再生用ボイスプール)
	struct Sound
	{
		WAVEFORMATEX               fmt = {};
		std::vector<BYTE>          data;
		std::vector<IXAudio2SourceVoice*> voices;
		int                        next = 0;
		// 3D定位: ボイスごとに「3Dで鳴っているか」と声源の位置。聞き手が動く/向きを変える度に左右の比率を計算し直す
		std::vector<bool>              is3D;
		std::vector<DirectX::XMFLOAT3> pos;
		std::vector<float>             flatMatrix;	// 2D(画面中央)で鳴らす時の出力行列=作った時の既定値。3Dで使った後のボイスを2Dへ戻す
	};

	IXAudio2*               g_xa     = nullptr;
	IXAudio2MasteringVoice* g_master = nullptr;
	Sound                   g_sound[Audio::SE_MAX];
	const int               VOICES_PER_SOUND = 6;

	//--- 3D定位(X3DAudio)
	X3DAUDIO_HANDLE   g_x3d       = {};
	bool              g_x3dOk     = false;
	UINT32            g_outCh     = 2;	// 出力(スピーカー/ヘッドホン)のチャンネル数。Init で実機から読む
	X3DAUDIO_LISTENER g_listener  = {};
	const UINT32      MAX_SRC_CH  = 2;	// 素材は mono/stereo のみ
	const UINT32      MAX_OUT_CH  = 8;	// 7.1 まで
	// 距離の減衰: X3DAudio の既定曲線は「距離 / CurveDistanceScaler」の逆数(1/d)。この距離までは等倍、2倍の距離で半分
	const float AUDIO_3D_REF_DIST     = 1.5f;	// (m) 工房の広さ(数m)で、隣の工位の音が少し小さく聞こえる程度
	// 声源のすぐ近くでは左右に振り切らない(頭のすぐ横の音が片耳だけになるのを防ぐ)。この半径の中は方向が中央へ寄る
	const float AUDIO_3D_INNER_RADIUS = 0.6f;	// (m)
	const float AUDIO_3D_INNER_ANGLE  = X3DAUDIO_PI / 4.0f;	// 内側の範囲が広がる角度(X3DAudio の InnerRadiusAngle)
	// 左右の振り幅: 1=X3DAudio の計算そのまま(横の音はほぼ片耳)、0=2D と同じ。ヘッドホンで真横が極端すぎない様に少し戻す
	const float AUDIO_3D_PAN_WIDTH    = 0.85f;

	//--- 44100Hz/mono/16bit のPCMフォーマットを設定
	void SetPCM(WAVEFORMATEX& f)
	{
		f.wFormatTag      = WAVE_FORMAT_PCM;
		f.nChannels       = 1;
		f.nSamplesPerSec  = 44100;
		f.wBitsPerSample  = 16;
		f.nBlockAlign     = f.nChannels * f.wBitsPerSample / 8;
		f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
		f.cbSize          = 0;
	}

	//--- float(-1..1)の波形を16bit PCMバイト列へ
	void ToPCM16(const std::vector<float>& wave, std::vector<BYTE>& out)
	{
		out.resize(wave.size() * sizeof(short));
		short* p = reinterpret_cast<short*>(out.data());
		for (size_t i = 0; i < wave.size(); ++i)
		{
			float v = wave[i];
			if (v >  1.0f) v =  1.0f;
			if (v < -1.0f) v = -1.0f;
			p[i] = (short)(v * 32767.0f);
		}
	}

	float noise() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

	//--- ループ音の継ぎ目を消す(クロスフェード)。
	//    素材の末尾 n サンプルを先頭へ重ねて溶かし、末尾は捨てる:
	//      新しい先頭[i] = 先頭[i]*w + 末尾[i]*(1-w)   (w: 0→1)
	//    すると「新しい末尾の次」=新しい先頭[0]=元の末尾の続き になり、ループの継ぎ目が連続になる。
	//    → ループ用に作られていない素材(普通の録音)でも、継ぎ目で「プツッ」と鳴らない。
	const float LOOP_CROSSFADE_SEC = 0.25f;	// 溶かす長さ(秒)
	void MakeSeamlessLoop(Sound& s)
	{
		if (s.fmt.wBitsPerSample != 16 || s.fmt.nChannels == 0) return;	// 16bit PCM のみ対応
		const size_t ch     = s.fmt.nChannels;
		const size_t frames = s.data.size() / (sizeof(short) * ch);
		size_t n = (size_t)(LOOP_CROSSFADE_SEC * s.fmt.nSamplesPerSec);
		if (n > frames / 4) n = frames / 4;		// 短い素材は溶かしすぎない
		if (n == 0) return;
		short* p = reinterpret_cast<short*>(s.data.data());
		for (size_t i = 0; i < n; ++i)
		{
			const float w = (float)i / (float)n;
			for (size_t c = 0; c < ch; ++c)
			{
				const float head = p[i * ch + c];
				const float tail = p[(frames - n + i) * ch + c];
				p[i * ch + c] = (short)(head * w + tail * (1.0f - w));
			}
		}
		s.data.resize((frames - n) * ch * sizeof(short));
	}

	//--- WAVファイルの読み込み(PCMのみ対応)。成功でtrue
	bool LoadWav(const char* path, Sound& s)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, path, "rb");
		if (!fp) return false;

		fseek(fp, 0, SEEK_END);
		long size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		std::vector<BYTE> buf(size);
		size_t rd = fread(buf.data(), 1, size, fp);
		fclose(fp);
		if (rd < 12 || memcmp(buf.data(), "RIFF", 4) != 0 || memcmp(&buf[8], "WAVE", 4) != 0)
			return false;

		bool haveFmt = false, haveData = false;
		size_t pos = 12;
		while (pos + 8 <= (size_t)size)
		{
			char id[5] = {}; memcpy(id, &buf[pos], 4);
			unsigned int csz = *reinterpret_cast<unsigned int*>(&buf[pos + 4]);
			size_t body = pos + 8;
			if (memcmp(id, "fmt ", 4) == 0 && body + 16 <= (size_t)size)
			{
				memcpy(&s.fmt, &buf[body], (csz < sizeof(WAVEFORMATEX)) ? csz : sizeof(WAVEFORMATEX));
				s.fmt.cbSize = 0;
				haveFmt = true;
			}
			else if (memcmp(id, "data", 4) == 0)
			{
				size_t n = (body + csz <= (size_t)size) ? csz : (size - body);
				s.data.assign(buf.begin() + body, buf.begin() + body + n);
				haveData = true;
			}
			pos = body + csz + (csz & 1);	// チャンクは偶数境界
		}
		return haveFmt && haveData && s.fmt.wFormatTag == WAVE_FORMAT_PCM;
	}

	//--- ファイルが無いとき用に、それらしい音をプログラム合成する
	void Synthesize(Audio::SoundId id, Sound& s)
	{
		SetPCM(s.fmt);
		const int   sr = 44100;
		std::vector<float> w;

		switch (id)
		{
		case Audio::SE_FACE_DONE:
		{
			// 「完成した」の合図(ユーザー要望 2026-10-07: 口笛は聞こえにくい。対戦ゲームの連続撃破音の様に
			//  「バン・バン・バン」と上がっていく、手応えのある音)。3発の打音が音程を上げながら続く。
			//  1発 = 胸に来る低い「ドン」(音程が下がる正弦=キック) + 金属的な「キン」(非整数倍音の減衰和音)。
			const int   HITS        = 3;
			const float GAP         = 0.085f;	// 打音の間隔(秒)
			const float TAIL        = 0.45f;	// 最後の打音の余韻(秒)
			const float CHIME[HITS] = { 659.25f, 830.61f, 987.77f };	// E5 → G#5 → B5(上がっていく長三和音)
			const float PARTIAL[3]  = { 1.0f, 2.76f, 5.40f };		// 金属の非整数倍音(SE_HAMMER と同じ鐘の比)
			const float PART_AMP[3] = { 1.0f, 0.45f, 0.2f };
			const float CHIME_DECAY = 9.0f;		// 「キン」の減衰(1/秒)
			const float KICK_START  = 180.0f, KICK_END = 60.0f;	// 「ドン」の音程(Hz)。始めから終わりへ下がる
			const float KICK_SWEEP  = 30.0f;	// 音程が下がる速さ(1/秒)
			const float KICK_DECAY  = 18.0f;	// 「ドン」の減衰
			const float KICK_LEVEL  = 0.55f, CHIME_LEVEL = 0.30f;
			const float CLICK_LEVEL = 0.25f, CLICK_DECAY = 400.0f;	// 打った瞬間のアタック
			const float MASTER      = 0.55f;
			const int n = (int)(sr * (GAP * (HITS - 1) + TAIL));
			w.assign(n, 0.0f);
			for (int h = 0; h < HITS; ++h)
			{
				const int start = (int)(sr * GAP * h);
				for (int i = start; i < n; ++i)
				{
					const float t = (float)(i - start) / sr;
					const float kickHz = KICK_END + (KICK_START - KICK_END) * expf(-t * KICK_SWEEP);
					float v = sinf(6.2832f * kickHz * t) * expf(-t * KICK_DECAY) * KICK_LEVEL;
					for (int k = 0; k < 3; ++k)
						v += PART_AMP[k] * sinf(6.2832f * CHIME[h] * PARTIAL[k] * t) * expf(-t * CHIME_DECAY * (1.0f + k)) * CHIME_LEVEL;
					v += noise() * CLICK_LEVEL * expf(-t * CLICK_DECAY);
					w[i] += v * MASTER;
				}
			}
			break;
		}
		case Audio::SE_HAMMER:
		{
			int n = sr * 35 / 100;	// 0.35s
			w.resize(n);
			const float f0 = 190.0f;
			const float part[4] = { 1.0f, 2.76f, 5.40f, 8.90f };
			const float amp [4] = { 1.0f, 0.6f,  0.4f,  0.25f };
			const float dec [4] = { 8.0f, 12.0f, 16.0f, 20.0f };
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				float v = 0.0f;
				for (int k = 0; k < 4; ++k)
					v += amp[k] * sinf(6.2832f * f0 * part[k] * t) * expf(-t * dec[k]);
				v += noise() * 0.6f * expf(-t * 300.0f);	// 立ち上がりの打撃ノイズ
				w[i] = v * 0.35f;
			}
			break;
		}
		case Audio::SE_COLD:
		{
			int n = sr * 14 / 100;	// 0.14s
			w.resize(n);
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				float v = sinf(6.2832f * 120.0f * t) * expf(-t * 30.0f) * 0.6f;
				v += noise() * 0.2f * expf(-t * 40.0f);
				w[i] = v;
			}
			break;
		}
		case Audio::SE_SIZZLE:
		{
			int n = sr * 25 / 100;	// 0.25s
			w.resize(n);
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				float env = expf(-t * 6.0f) * (1.0f - expf(-t * 60.0f));
				w[i] = noise() * 0.15f * env;
			}
			break;
		}
		case Audio::SE_SWING:
		{
			int n = sr * 18 / 100;	// 0.18s の「ヒュッ」(空を切る音=帯域ノイズが立ち上がって減衰)
			w.resize(n);
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				float env = (1.0f - expf(-t * 60.0f)) * expf(-t * 18.0f);	// 素早く立ち上がり素早く減衰
				w[i] = noise() * 0.22f * env;
			}
			break;
		}
		case Audio::SE_QUENCH:
		{
			// 淬火: 熱鋼を水に入れた「ジュワ〜」。高周波ノイズを一次ローパスで蒸気っぽくし、急峻に立ち上げ緩やかに減衰。
			const float DUR      = 0.70f;	// 長さ(秒)
			const float RISE     = 120.0f;	// 立ち上がりの速さ(大=速い)
			const float DECAY    = 5.5f;	// 減衰の速さ
			const float LP_COEF  = 0.55f;	// ローパス係数(0..1, 大=こもる=蒸気感)
			const float LEVEL    = 0.35f;	// 音量
			int n = (int)(sr * DUR);
			w.resize(n);
			float lp = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				float t   = (float)i / sr;
				lp += LP_COEF * (noise() - lp);	// 一次ローパス(ホワイト→ピンク寄り)
				float env = (1.0f - expf(-t * RISE)) * expf(-t * DECAY);
				w[i] = lp * env * LEVEL;
			}
			break;
		}
		case Audio::SE_FORGE_LOOP:
		{
			// 加熱の持続音(炉火/風箱): 低周波のこもったノイズ+緩い揺らぎ。無縫ループ向けに定常で作る。
			const float DUR      = 1.00f;	// 1秒(ループ単位)
			const float LP_COEF  = 0.10f;	// 強めのローパス(低い唸り)
			const float SWELL_HZ = 2.5f;	// 風箱の揺らぎ周期(Hz)
			const float SWELL    = 0.35f;	// 揺らぎの深さ
			const float LEVEL    = 0.28f;	// 音量
			int n = (int)(sr * DUR);
			w.resize(n);
			float lp = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				lp += LP_COEF * (noise() - lp);
				float swell = 1.0f - SWELL + SWELL * (0.5f + 0.5f * sinf(6.2832f * SWELL_HZ * t));
				w[i] = lp * swell * LEVEL;
			}
			break;
		}
		case Audio::SE_SUCCESS:
		{
			// 完成の合図: 上行する3音のアルペジオ(明るい)。
			const float NOTE[3] = { 523.25f, 659.25f, 783.99f };	// C5-E5-G5(長三和音)
			const float NOTE_DUR = 0.16f;	// 1音の長さ
			const float LEVEL    = 0.28f;
			int per = (int)(sr * NOTE_DUR);
			w.resize(per * 3);
			for (int k = 0; k < 3; ++k)
			for (int i = 0; i < per; ++i)
			{
				float t   = (float)i / sr;
				float env = (1.0f - expf(-t * 60.0f)) * expf(-t * 4.0f);
				w[k * per + i] = sinf(6.2832f * NOTE[k] * t) * env * LEVEL;
			}
			break;
		}
		case Audio::SE_BURN_LOOP:
		{
			// 燃える鋼のパチパチ: 静かなシューという下地に、ランダムな短い破裂(クラックル)を散らす。
			const float DUR        = 1.00f;	// 1秒(ループ単位)
			const float HISS_LEVEL = 0.05f;	// 下地のシュー音
			const float POP_CHANCE = 0.0009f;	// 1サンプルあたりの破裂の発生確率(≒40回/秒)
			const float POP_DECAY  = 900.0f;	// 破裂の減衰(大=短いパチッ)
			const float POP_LEVEL  = 0.35f;
			int n = (int)(sr * DUR);
			w.resize(n);
			float pop = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				if ((float)rand() / RAND_MAX < POP_CHANCE) pop = 1.0f;	// 新しい破裂
				w[i] = noise() * (HISS_LEVEL + POP_LEVEL * pop);
				pop *= expf(-POP_DECAY / sr);
			}
			break;
		}
		case Audio::SE_GRIND_LOOP:
		{
			// 砥石の「シャー」: 明るいノイズ(高域寄り)を、砥石の回転周期で少し揺らす。
			const float DUR     = 1.00f;
			const float HP_COEF = 0.85f;	// 一次ハイパス(大=シャリシャリした高域)
			const float WOB_HZ  = 6.0f;	// 回転の揺らぎ周期
			const float WOB     = 0.25f;
			const float LEVEL   = 0.22f;
			int n = (int)(sr * DUR);
			w.resize(n);
			float prev = 0.0f, hp = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				float x = noise();
				hp = HP_COEF * (hp + x - prev);	// 一次ハイパス
				prev = x;
				float wob = 1.0f - WOB + WOB * (0.5f + 0.5f * sinf(6.2832f * WOB_HZ * t));
				w[i] = hp * wob * LEVEL;
			}
			break;
		}
		case Audio::SE_STEAM:
		{
			// 淬火の蒸気: SE_QUENCH より長く、最初に強く噴き出し、ゆっくり細くなる。
			const float DUR     = 2.60f;
			const float RISE    = 60.0f;
			const float DECAY   = 1.3f;
			const float LP_COEF = 0.35f;	// やや明るい(蒸気の噴出音)
			const float LEVEL   = 0.40f;
			int n = (int)(sr * DUR);
			w.resize(n);
			float lp = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				lp += LP_COEF * (noise() - lp);
				float env = (1.0f - expf(-t * RISE)) * expf(-t * DECAY);
				w[i] = lp * env * LEVEL;
			}
			break;
		}
		case Audio::SE_PENCIL:
		{
			// 鉛筆が紙を一息に走る「シャーッ」: 高域のノイズ(黒鉛が紙の繊維に引っかかる音)に、
			// 細かい不規則な粒(紙のざらつき)で振幅を揺らす。速く立ち上がり、線の終わりで抜ける。
			// 長さは工程リストの線アニメ(HUD.cpp の STRIKE_ANIM_SEC)と同じくらいにしておく。
			const float DUR        = 0.80f;	// 長さ(秒)
			const float HP_COEF    = 0.92f;	// 一次ハイパス(大=シャリッとした高域だけ残る)
			const float GRAIN_LP   = 0.02f;	// 粒の揺らぎの滑らかさ(小=ゆっくり揺れる)
			const float GRAIN_DEPTH= 0.6f;	// 粒による振幅の揺れの深さ
			const float RISE       = 80.0f;	// 書き始めの立ち上がり
			const float FADE_START = 0.75f;	// 長さのこの割合から抜け始める
			const float LEVEL      = 0.30f;
			int n = (int)(sr * DUR);
			w.resize(n);
			float prev = 0.0f, hp = 0.0f, grain = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr, prog = t / DUR;
				float x = noise();
				hp = HP_COEF * (hp + x - prev);	prev = x;		// 一次ハイパス
				grain += GRAIN_LP * (fabsf(noise()) - grain);		// 不規則に揺れる 0..1 の包絡
				float env = (1.0f - expf(-t * RISE));
				if (prog > FADE_START) env *= 1.0f - (prog - FADE_START) / (1.0f - FADE_START);
				w[i] = hp * env * (1.0f - GRAIN_DEPTH + GRAIN_DEPTH * grain) * LEVEL;
			}
			break;
		}
		case Audio::SE_ERASER:
		{
			// 消しゴムの「ゴシゴシ」: 鉛筆より低くこもったノイズ(ゴムが紙を擦る)を、
			// 往復の周期で強弱させる(押して擦る→戻す、を数回)。
			const float DUR     = 0.80f;	// 長さ(秒)。線アニメと同じくらい
			const float LP_COEF = 0.25f;	// 一次ローパス(小=こもった擦れ音)
			const float RUB_HZ  = 7.0f;		// 往復の速さ(回/秒)
			const float RUB_SHARP = 3.0f;	// 往復の山の鋭さ(大=1回ずつ区切れて聞こえる)
			const float RISE    = 40.0f;
			const float DECAY   = 1.5f;
			const float LEVEL   = 0.45f;
			int n = (int)(sr * DUR);
			w.resize(n);
			float lp = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				lp += LP_COEF * (noise() - lp);
				float rub = powf(0.5f + 0.5f * sinf(6.2832f * RUB_HZ * t), RUB_SHARP);	// 往復ごとの山
				float env = (1.0f - expf(-t * RISE)) * expf(-t * DECAY);
				w[i] = lp * rub * env * LEVEL;
			}
			break;
		}
		case Audio::SE_LEARNED:
		{
			// 仮の確認音(ユーザーが素材を選ぶまで): 上がる2音の短いチャイム。
			//   SE_SUCCESS(完成の3音)より短く小さく=「小さな達成」。基音に1オクターブ上を少し混ぜて鈴の様な響きに。
			const float NOTE[2]   = { 880.0f, 1318.51f };	// A5 → E6(完全5度上=明るく区切りが付く)
			const float NOTE_DUR  = 0.12f;	// 1音の長さ
			const float OCTAVE_MIX= 0.25f;	// 1オクターブ上の倍音の混ぜ具合
			const float ATTACK    = 80.0f;	// 立ち上がり(大=速い)
			const float DECAY     = 9.0f;	// 減衰(大=短い)
			const float LEVEL     = 0.22f;
			int per = (int)(sr * NOTE_DUR);
			w.resize(per * 2);
			for (int k = 0; k < 2; ++k)
			for (int i = 0; i < per; ++i)
			{
				float t   = (float)i / sr;
				float env = (1.0f - expf(-t * ATTACK)) * expf(-t * DECAY);
				float ph  = 6.2832f * NOTE[k] * t;
				w[k * per + i] = (sinf(ph) + OCTAVE_MIX * sinf(2.0f * ph)) * env * LEVEL;
			}
			break;
		}
		case Audio::SE_BELLOWS_PUMP:
		{
			// 仮の音(ユーザーが素材を選ぶまで): ふいごを1回踏んだ「フッ」= 風が一気に押し出される息の音。
			//   低めに濾した雑音を、速く立ち上げてすぐ抜く。炉火の唸り(SE_FORGE_LOOP)の上に1回ずつ乗る=段落のある音
			const float LENGTH = 0.45f;	// 長さ(秒)
			const float LP     = 0.08f;	// 1次ローパスの係数(小=こもった風の音)
			const float ATTACK = 60.0f;	// 立ち上がり(大=速い。踏み込んだ瞬間)
			const float DECAY  = 7.0f;	// 抜け(大=短い)
			const float LEVEL  = 0.9f;
			const int n = (int)(sr * LENGTH);
			w.resize(n);
			float lp = 0.0f;
			for (int i = 0; i < n; ++i)
			{
				const float t = (float)i / sr;
				lp += LP * (noise() - lp);
				w[i] = lp * (1.0f - expf(-t * ATTACK)) * expf(-t * DECAY) * LEVEL;
			}
			break;
		}
		case Audio::SE_SEG_DONE:
		{
			// 仮の合図(ユーザーが素材を選ぶまで): 研ぎの区域が仕上がった「チン」。短く高い金属の1音。
			//   面の完成(SE_FACE_DONE)より小さく短い=「小さな区切り」。素材は Assets/Sound/SE/seg_done.wav で差し替え
			const float NOTE      = 2093.0f;	// C7(高い=小さな金属片が鳴る感じ)
			const float INHARMONIC= 2.76f;		// 金属の打音の非整数倍音(棒の振動の第2モード比に近い)=鈴でなく金属に聞こえる
			const float PARTIAL   = 0.35f;		// その倍音の混ぜ具合
			const float LENGTH    = 0.35f;		// 長さ(秒)
			const float ATTACK    = 400.0f;		// 立ち上がり(大=速い。打った瞬間)
			const float DECAY     = 14.0f;		// 減衰(大=短い)
			const float LEVEL     = 0.35f;	// 研ぎのループ音(広い帯域の雑音)の上でも聞こえる大きさ(0.16 では埋もれた)
			const int n = (int)(sr * LENGTH);
			w.resize(n);
			for (int i = 0; i < n; ++i)
			{
				float t   = (float)i / sr;
				float env = (1.0f - expf(-t * ATTACK)) * expf(-t * DECAY);
				float ph  = 6.2832f * NOTE * t;
				w[i] = (sinf(ph) + PARTIAL * sinf(ph * INHARMONIC)) * env * LEVEL;
			}
			break;
		}
		case Audio::SE_NEXT_STEP:
		{
			// 仮の合図(ユーザーが素材を選ぶまで): 低めの鐘を2つ、重ねて鳴らす(2つ目は少し遅れて入る)。
			//   SE_LEARNED(高く短い2音)と聞き分けられる様に、低く・長く余韻を残す=「次の段階へ」。
			const float NOTE[2]    = { 523.25f, 783.99f };	// C5 → G5(完全5度)
			const float NOTE_DELAY = 0.14f;	// 2つ目が入るまでの秒
			const float LENGTH     = 1.1f;	// 全体の長さ(余韻を含む)
			const float OCTAVE_MIX = 0.30f;	// 1オクターブ上の倍音(鐘らしさ)
			const float ATTACK     = 60.0f;	// 立ち上がり(大=速い)
			const float DECAY      = 3.5f;	// 減衰(小=長く響く)
			const float LEVEL      = 0.18f;
			const int n = (int)(sr * LENGTH);
			w.assign(n, 0.0f);
			for (int k = 0; k < 2; ++k)
			{
				const int start = (int)(sr * NOTE_DELAY * k);
				for (int i = start; i < n; ++i)
				{
					float t   = (float)(i - start) / sr;
					float env = (1.0f - expf(-t * ATTACK)) * expf(-t * DECAY);
					float ph  = 6.2832f * NOTE[k] * t;
					w[i] += (sinf(ph) + OCTAVE_MIX * sinf(2.0f * ph)) * env * LEVEL;
				}
			}
			break;
		}
		default: break;
		}
		ToPCM16(w, s.data);
	}

	void CreateVoices(Sound& s)
	{
		if (!g_xa || s.data.empty()) return;
		for (int i = 0; i < VOICES_PER_SOUND; ++i)
		{
			IXAudio2SourceVoice* v = nullptr;
			if (SUCCEEDED(g_xa->CreateSourceVoice(&v, &s.fmt)))
				s.voices.push_back(v);
		}
		s.is3D.assign(s.voices.size(), false);
		s.pos.assign(s.voices.size(), DirectX::XMFLOAT3(0, 0, 0));
		// 既定の出力行列(2D)を覚えておく。全ボイス同じフォーマットなので 1 つ目から読めば足りる
		const UINT32 srcCh = s.fmt.nChannels;
		if (!s.voices.empty() && srcCh <= MAX_SRC_CH)
		{
			s.flatMatrix.assign(srcCh * g_outCh, 0.0f);
			s.voices[0]->GetOutputMatrix(g_master, srcCh, g_outCh, s.flatMatrix.data());
		}
	}

	//--- 3D定位: ボイス i の左右/距離の比率を、今の聞き手と声源の位置から計算して出力行列に入れる
	void Apply3D(Sound& s, size_t i)
	{
		const UINT32 srcCh = s.fmt.nChannels;
		if (!g_x3dOk || srcCh > MAX_SRC_CH || g_outCh > MAX_OUT_CH || s.flatMatrix.empty()) return;

		float azimuths[MAX_SRC_CH] = {};	// stereo 素材も声源の1点から出す(左右の広がりは持たせない)
		X3DAUDIO_EMITTER e = {};
		e.Position            = { s.pos[i].x, s.pos[i].y, s.pos[i].z };
		e.OrientFront         = { 0.0f, 0.0f, 1.0f };	// 声源の向き(指向性を使わないので任意。X3DAudio は必須なので与える)
		e.OrientTop           = { 0.0f, 1.0f, 0.0f };
		e.ChannelCount        = srcCh;
		e.pChannelAzimuths    = azimuths;
		e.CurveDistanceScaler = AUDIO_3D_REF_DIST;
		e.DopplerScaler       = 1.0f;
		e.InnerRadius         = AUDIO_3D_INNER_RADIUS;
		e.InnerRadiusAngle    = AUDIO_3D_INNER_ANGLE;

		float matrix[MAX_SRC_CH * MAX_OUT_CH] = {};
		X3DAUDIO_DSP_SETTINGS dsp = {};
		dsp.SrcChannelCount     = srcCh;
		dsp.DstChannelCount     = g_outCh;
		dsp.pMatrixCoefficients = matrix;
		X3DAudioCalculate(g_x3d, &g_listener, &e, X3DAUDIO_CALCULATE_MATRIX, &dsp);

		// 振り幅を少し中央へ戻す。距離の減衰は残す(2D 行列にも同じ音量の比を掛ける)
		const UINT32 n = srcCh * g_outCh;
		float sum3D = 0.0f, sumFlat = 0.0f;
		for (UINT32 k = 0; k < n; ++k) { sum3D += matrix[k]; sumFlat += s.flatMatrix[k]; }
		const float gain = (sumFlat > 0.0f) ? sum3D / sumFlat : 0.0f;	// 距離で小さくなった分
		for (UINT32 k = 0; k < n; ++k)
			matrix[k] = matrix[k] * AUDIO_3D_PAN_WIDTH + s.flatMatrix[k] * gain * (1.0f - AUDIO_3D_PAN_WIDTH);
		s.voices[i]->SetOutputMatrix(g_master, srcCh, g_outCh, matrix);
	}

	//--- 3Dで使ったボイスを2D(画面中央)へ戻す
	void Make2D(Sound& s, size_t i)
	{
		if (!s.is3D[i]) return;
		s.is3D[i] = false;
		if (!s.flatMatrix.empty())
			s.voices[i]->SetOutputMatrix(g_master, s.fmt.nChannels, g_outCh, s.flatMatrix.data());
	}
}

namespace
{
	// BGMミュート(録画用)。止めずに音量0で再生を続ける＝解除時に途中から自然に戻る
	bool  g_bgmMuted = false;
	float g_loopVolume[Audio::SE_MAX] = {};	// PlayLoop で指定された本来の音量(解除時に戻す)

	bool IsBgm(Audio::SoundId id) { return id >= Audio::BGM_TITLE && id <= Audio::BGM_RESULT; }
}

namespace Audio
{
	void Init()
	{
		if (FAILED(XAudio2Create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) { g_xa = nullptr; return; }
		if (FAILED(g_xa->CreateMasteringVoice(&g_master))) { g_xa->Release(); g_xa = nullptr; return; }

		// 3D定位の準備: 出力のスピーカー配置(stereo/5.1…)を実機から読み、X3DAudio に渡す
		{
			DWORD mask = 0;
			XAUDIO2_VOICE_DETAILS md = {};
			g_master->GetVoiceDetails(&md);
			g_outCh = md.InputChannels;
			g_x3dOk = SUCCEEDED(g_master->GetChannelMask(&mask))
			       && SUCCEEDED(X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, g_x3d));
			g_listener.OrientFront = { 0.0f, 0.0f, 1.0f };
			g_listener.OrientTop   = { 0.0f, 1.0f, 0.0f };
		}

		static const char* files[SE_MAX] = {
			"Assets/Sound/SE/face_done.wav",	// SE_FACE_DONE(無ければ合成音)
			"Assets/Sound/hammer.wav",
			"Assets/Sound/cold.wav",
			"Assets/Sound/sizzle.wav",
			"Assets/Sound/swing.wav",			// SE_SWING(無ければ合成音)
			"Assets/Sound/SE/anvil_hit_1.wav",	// SE_ANVIL1
			"Assets/Sound/SE/anvil_hit_2.wav",	// SE_ANVIL2
			"Assets/Sound/SE/quench.wav",		// SE_QUENCH(無ければ合成音)
			"Assets/Sound/SE/forge_loop.wav",	// SE_FORGE_LOOP(無ければ合成音)
			"Assets/Sound/SE/success.wav",		// SE_SUCCESS(無ければ合成音)
			"Assets/Sound/SE/burn_loop.wav",	// SE_BURN_LOOP(無ければ合成音)
			"Assets/Sound/SE/grind_loop.wav",	// SE_GRIND_LOOP(無ければ合成音)
			"Assets/Sound/SE/steam.wav",		// SE_STEAM(無ければ合成音)
			"Assets/Sound/SE/title_fade.wav",	// SE_TITLE_FADE(ロゴ淡出。無ければ無音)
			"Assets/Sound/SE/pencil.wav",		// SE_PENCIL(取り消し線を引く。無ければ合成音)
			"Assets/Sound/SE/eraser.wav",		// SE_ERASER(取り消し線を消す。無ければ合成音)
			"Assets/Sound/SE/learned.wav",		// SE_LEARNED(操作を覚えた確認音。無ければ合成音)
			"Assets/Sound/SE/next_step.wav",	// SE_NEXT_STEP(工程が進んだ合図。無ければ合成音)
			"Assets/Sound/SE/seg_done.wav",	// SE_SEG_DONE(研ぎの区域が仕上がった。無ければ合成音)
			"Assets/Sound/SE/bellows_pump.wav",	// SE_BELLOWS_PUMP(ふいごを1回踏んだ。無ければ合成音)
			"Assets/Sound/BGM/title_bgm.wav",	// BGM_TITLE(工場環境音。無ければ無音)
			"Assets/Sound/BGM/play_bgm.wav",	// BGM_PLAY(medieval。無ければ無音)
			"Assets/Sound/BGM/result_bgm.wav",	// BGM_RESULT(無ければ無音)
		};
		for (int i = 0; i < SE_MAX; ++i)
		{
			if (!LoadWav(files[i], g_sound[i]))	// WAVが無ければ合成音で代用
				Synthesize((SoundId)i, g_sound[i]);
			// 途切れず鳴り続けるループ効果音は、継ぎ目を溶かしておく(素材がループ用でなくても使える)
			if (i == SE_FORGE_LOOP || i == SE_BURN_LOOP || i == SE_GRIND_LOOP)
				MakeSeamlessLoop(g_sound[i]);
			CreateVoices(g_sound[i]);
		}
	}

	void Uninit()
	{
		for (int i = 0; i < SE_MAX; ++i)
		{
			for (auto v : g_sound[i].voices) { if (v) { v->Stop(0); v->DestroyVoice(); } }
			g_sound[i].voices.clear();
			g_sound[i].data.clear();
		}
		if (g_master) { g_master->DestroyVoice(); g_master = nullptr; }
		if (g_xa)     { g_xa->Release(); g_xa = nullptr; }
	}

	//--- 一回再生の本体。pos があればその位置から(3D)、無ければ画面中央(2D)
	static void PlayVoice(SoundId id, float volume, float pitch, const DirectX::XMFLOAT3* pos)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty()) return;

		const size_t i = (size_t)s.next;
		IXAudio2SourceVoice* v = s.voices[i];
		s.next = (s.next + 1) % (int)s.voices.size();
		if (pos) { s.is3D[i] = true; s.pos[i] = *pos; Apply3D(s, i); }	// 鳴り始める前に左右の比率を決める(最初の一瞬が中央で鳴らない様に)
		else     Make2D(s, i);

		v->Stop(0);
		v->FlushSourceBuffers();

		XAUDIO2_BUFFER b = {};
		b.AudioBytes = (UINT32)s.data.size();
		b.pAudioData = s.data.data();
		b.Flags      = XAUDIO2_END_OF_STREAM;
		v->SubmitSourceBuffer(&b);
		v->SetVolume(volume);
		v->SetFrequencyRatio(pitch);	// 毎回設定する(ボイスは使い回し=前回の音程が残らない様に)
		v->Start(0);
	}

	void Play(SoundId id, float volume, float pitch) { PlayVoice(id, volume, pitch, nullptr); }
	void PlayAt(SoundId id, const DirectX::XMFLOAT3& pos, float volume, float pitch) { PlayVoice(id, volume, pitch, &pos); }

	//--- 聞き手(カメラ)を更新し、3Dで鳴っている全ボイスの左右の比率を計算し直す(振り向くと音の方向も回る)
	void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& front, const DirectX::XMFLOAT3& up)
	{
		if (!g_xa || !g_x3dOk) return;
		// X3DAudio は front と top が長さ1で直交している事を求める。カメラの up は世界の上(0,1,0)のままで、
		// 見下ろすと front と直交しないので、グラム・シュミットの直交化で top を front に直交させる
		using namespace DirectX;
		const XMVECTOR f = XMVector3Normalize(XMLoadFloat3(&front));
		XMVECTOR t = XMLoadFloat3(&up);
		t = XMVector3Normalize(XMVectorSubtract(t, XMVectorMultiply(XMVector3Dot(t, f), f)));
		XMFLOAT3 ff, tt; XMStoreFloat3(&ff, f); XMStoreFloat3(&tt, t);
		g_listener.Position    = { pos.x, pos.y, pos.z };
		g_listener.OrientFront = { ff.x, ff.y, ff.z };
		g_listener.OrientTop   = { tt.x, tt.y, tt.z };
		for (int id = 0; id < SE_MAX; ++id)
		{
			Sound& s = g_sound[id];
			for (size_t i = 0; i < s.voices.size(); ++i)
				if (s.is3D[i]) Apply3D(s, i);
		}
	}

	//--- 鳴っているループ(voices[0])を3Dにする。PlayLoop は2Dで始めるので、その後に呼ぶ
	void SetLoopPos(SoundId id, const DirectX::XMFLOAT3& pos)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty()) return;
		s.is3D[0] = true;
		s.pos[0]  = pos;
		Apply3D(s, 0);
	}

	//--- 無限ループ再生(BGM/タイトル)。専用に voices[0] を使う(Playのラウンドロビンとは別)
	void PlayLoop(SoundId id, float volume)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty() || s.data.empty()) return;

		IXAudio2SourceVoice* v = s.voices[0];
		Make2D(s, 0);	// 3D にしたい時は呼び出し側がこの後 SetLoopPos を呼ぶ
		v->Stop(0);
		v->FlushSourceBuffers();

		XAUDIO2_BUFFER b = {};
		b.AudioBytes = (UINT32)s.data.size();
		b.pAudioData = s.data.data();
		b.LoopCount  = XAUDIO2_LOOP_INFINITE;	// 閉じるまでループ
		v->SubmitSourceBuffer(&b);
		g_loopVolume[id] = volume;
		v->SetVolume((IsBgm(id) && g_bgmMuted) ? 0.0f : volume);	// ミュート中は無音で開始
		v->SetFrequencyRatio(1.0f);	// 前回 SetLoop で変えた音程を戻す
		v->Start(0);
	}

	//--- 鳴っているループの音量と音程を、止めずに変える(止める→最初から鳴り直す、を避ける)。
	//    pitch = 再生速度の倍率(1=原音。2で1オクターブ上。上限は XAudio2 既定の 2.0)
	void SetLoop(SoundId id, float volume, float pitch)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty()) return;
		g_loopVolume[id] = volume;
		s.voices[0]->SetVolume((IsBgm(id) && g_bgmMuted) ? 0.0f : volume);	// BGM ミュート中は無音のまま(PlayLoop と同じ規則)
		s.voices[0]->SetFrequencyRatio(pitch);
	}

	//--- BGMだけミュート切替。SE(Play/加熱ループ)には影響しない
	void ToggleBgmMute()
	{
		g_bgmMuted = !g_bgmMuted;
		if (!g_xa) return;
		for (int i = BGM_TITLE; i <= BGM_RESULT; ++i)
		{
			Sound& s = g_sound[i];
			if (s.voices.empty()) continue;
			s.voices[0]->SetVolume(g_bgmMuted ? 0.0f : g_loopVolume[i]);
		}
	}

	//--- ループ停止(タイトル→ゲーム移行時など)
	void Stop(SoundId id)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty()) return;
		s.voices[0]->Stop(0);
		s.voices[0]->FlushSourceBuffers();
	}
}

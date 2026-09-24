#include "Audio.h"
#include <xaudio2.h>
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
	};

	IXAudio2*               g_xa     = nullptr;
	IXAudio2MasteringVoice* g_master = nullptr;
	Sound                   g_sound[Audio::SE_MAX];
	const int               VOICES_PER_SOUND = 6;

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
		case Audio::SE_WHISTLE:
		{
			int n = sr * 32 / 100;	// 0.32s の口笛(少し上がって下がる, ビブラート付き)
			w.resize(n);
			for (int i = 0; i < n; ++i)
			{
				float t = (float)i / sr;
				float prog = t / 0.32f;
				float pitch = 900.0f + 220.0f * sinf(prog * 3.1416f);	// 山なりに音程変化
				float vib   = 1.0f + 0.02f * sinf(6.2832f * 6.0f * t);	// ビブラート
				float env   = (1.0f - expf(-t * 40.0f)) * expf(-t * 3.5f);
				w[i] = sinf(6.2832f * pitch * vib * t) * env * 0.30f;
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
	}
}

namespace Audio
{
	void Init()
	{
		if (FAILED(XAudio2Create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) { g_xa = nullptr; return; }
		if (FAILED(g_xa->CreateMasteringVoice(&g_master))) { g_xa->Release(); g_xa = nullptr; return; }

		static const char* files[SE_MAX] = {
			"Assets/Sound/whistle.wav",
			"Assets/Sound/hammer.wav",
			"Assets/Sound/cold.wav",
			"Assets/Sound/sizzle.wav",
			"Assets/Sound/swing.wav",			// SE_SWING(無ければ合成音)
			"Assets/Sound/SE/anvil_hit_1.wav",	// SE_ANVIL1
			"Assets/Sound/SE/anvil_hit_2.wav",	// SE_ANVIL2
			"Assets/Sound/SE/quench.wav",		// SE_QUENCH(無ければ合成音)
			"Assets/Sound/SE/forge_loop.wav",	// SE_FORGE_LOOP(無ければ合成音)
			"Assets/Sound/SE/success.wav",		// SE_SUCCESS(無ければ合成音)
			"Assets/Sound/BGM/title_bgm.wav",	// BGM_TITLE(工場環境音。無ければ無音)
			"Assets/Sound/BGM/play_bgm.wav",	// BGM_PLAY(medieval。無ければ無音)
			"Assets/Sound/BGM/result_bgm.wav",	// BGM_RESULT(無ければ無音)
		};
		for (int i = 0; i < SE_MAX; ++i)
		{
			if (!LoadWav(files[i], g_sound[i]))	// WAVが無ければ合成音で代用
				Synthesize((SoundId)i, g_sound[i]);
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

	void Play(SoundId id, float volume)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty()) return;

		IXAudio2SourceVoice* v = s.voices[s.next];
		s.next = (s.next + 1) % (int)s.voices.size();

		v->Stop(0);
		v->FlushSourceBuffers();

		XAUDIO2_BUFFER b = {};
		b.AudioBytes = (UINT32)s.data.size();
		b.pAudioData = s.data.data();
		b.Flags      = XAUDIO2_END_OF_STREAM;
		v->SubmitSourceBuffer(&b);
		v->SetVolume(volume);
		v->Start(0);
	}

	//--- 無限ループ再生(BGM/タイトル)。専用に voices[0] を使う(Playのラウンドロビンとは別)
	void PlayLoop(SoundId id, float volume)
	{
		if (!g_xa || id < 0 || id >= SE_MAX) return;
		Sound& s = g_sound[id];
		if (s.voices.empty() || s.data.empty()) return;

		IXAudio2SourceVoice* v = s.voices[0];
		v->Stop(0);
		v->FlushSourceBuffers();

		XAUDIO2_BUFFER b = {};
		b.AudioBytes = (UINT32)s.data.size();
		b.pAudioData = s.data.data();
		b.LoopCount  = XAUDIO2_LOOP_INFINITE;	// 閉じるまでループ
		v->SubmitSourceBuffer(&b);
		v->SetVolume(volume);
		v->Start(0);
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

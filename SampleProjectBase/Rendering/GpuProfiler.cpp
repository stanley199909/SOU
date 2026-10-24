#include "GpuProfiler.h"
#include "DirectX.h"
#include <cstring>

namespace
{
    const int   LATENCY      = 4;      // frames between issuing and reading the queries (no stall)
    const int   MAX_SECTIONS = 64;     // timed sections per frame (a name may repeat: every outdoor prop)
    const float SMOOTHING    = 0.1f;   // exponential moving average: share of the newest frame
    const double MS_PER_S    = 1000.0;

    template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct Frame
    {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* frameBegin = nullptr;
        ID3D11Query* frameEnd = nullptr;
        ID3D11Query* begin[MAX_SECTIONS] = {};
        ID3D11Query* end[MAX_SECTIONS] = {};
        const char*  names[MAX_SECTIONS] = {};
        bool         ended[MAX_SECTIONS] = {};
        int          count = 0;
        bool         issued = false;
    };

    struct State
    {
        Frame frames[LATENCY];
        int   current = 0;
        bool  created = false, failed = false, inFrame = false;
        std::vector<GpuProfiler::Entry> results;
        float frameMs = 0.0f;
        float cpuMs = 0.0f;                 // CPU time between BeginFrame and EndFrame (issuing the draw calls)
        LARGE_INTEGER cpuStart = {}, qpcFreq = {};
    } s;

    bool CreateQueries()
    {
        if (s.created) return !s.failed;
        s.created = true;
        ID3D11Device* dev = GetDevice();
        D3D11_QUERY_DESC dj = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
        D3D11_QUERY_DESC ts = { D3D11_QUERY_TIMESTAMP, 0 };
        for (Frame& f : s.frames)
        {
            if (FAILED(dev->CreateQuery(&dj, &f.disjoint)) || FAILED(dev->CreateQuery(&ts, &f.frameBegin)) ||
                FAILED(dev->CreateQuery(&ts, &f.frameEnd))) { s.failed = true; return false; }
            for (int i = 0; i < MAX_SECTIONS; ++i)
                if (FAILED(dev->CreateQuery(&ts, &f.begin[i])) || FAILED(dev->CreateQuery(&ts, &f.end[i]))) { s.failed = true; return false; }
        }
        return true;
    }

    void Smooth(float& value, float sample) { value += (sample - value) * SMOOTHING; }

    GpuProfiler::Entry* FindOrAdd(const char* name)
    {
        for (GpuProfiler::Entry& e : s.results)
            if (e.name == name || strcmp(e.name, name) == 0) return &e;
        s.results.push_back({ name, 0.0f });
        return &s.results.back();
    }

    // Read the oldest frame if the GPU has finished it (never waits).
    void Collect(Frame& f)
    {
        if (!f.issued) return;
        ID3D11DeviceContext* ctx = GetContext();
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
        if (ctx->GetData(f.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;   // not ready yet
        f.issued = false;
        if (dj.Disjoint || dj.Frequency == 0) return;   // the GPU clock jumped: this frame's numbers are meaningless
        auto read = [&](ID3D11Query* q, UINT64& t) { return ctx->GetData(q, &t, sizeof(t), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK; };
        const double toMs = MS_PER_S / (double)dj.Frequency;
        UINT64 a = 0, b = 0;
        if (read(f.frameBegin, a) && read(f.frameEnd, b) && b >= a) Smooth(s.frameMs, (float)((b - a) * toMs));
        // Sections with the same name in one frame (e.g. every outdoor prop) are SUMMED, then smoothed once.
        const char* names[MAX_SECTIONS]; float sums[MAX_SECTIONS]; int n = 0;
        for (int i = 0; i < f.count; ++i)
        {
            if (!f.ended[i] || !read(f.begin[i], a) || !read(f.end[i], b) || b < a) continue;
            int k = 0;
            while (k < n && strcmp(names[k], f.names[i]) != 0) ++k;
            if (k == n) { names[n] = f.names[i]; sums[n] = 0.0f; ++n; }
            sums[k] += (float)((b - a) * toMs);
        }
        for (int k = 0; k < n; ++k) Smooth(FindOrAdd(names[k])->ms, sums[k]);
    }
}

namespace GpuProfiler
{
    void BeginFrame()
    {
        if (!CreateQueries()) return;
        Frame& f = s.frames[s.current];
        if (f.issued) Collect(f);   // still pending after LATENCY frames: give it one last try, then reuse
        f.issued = false;
        f.count = 0;
        QueryPerformanceCounter(&s.cpuStart);
        GetContext()->Begin(f.disjoint);
        GetContext()->End(f.frameBegin);
        s.inFrame = true;
    }

    int Begin(const char* name)
    {
        if (!s.inFrame || !name) return -1;   // name = nullptr: "do not time this one" (e.g. inside the probe capture)
        Frame& f = s.frames[s.current];
        if (f.count >= MAX_SECTIONS) return -1;
        const int slot = f.count++;
        f.names[slot] = name;
        f.ended[slot] = false;
        GetContext()->End(f.begin[slot]);   // a timestamp query is written by End()
        return slot;
    }

    void End(int slot)
    {
        if (!s.inFrame || slot < 0) return;
        Frame& f = s.frames[s.current];
        GetContext()->End(f.end[slot]);
        f.ended[slot] = true;
    }

    void EndFrame()
    {
        if (!s.inFrame) return;
        Frame& f = s.frames[s.current];
        GetContext()->End(f.frameEnd);
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (!s.qpcFreq.QuadPart) QueryPerformanceFrequency(&s.qpcFreq);
        Smooth(s.cpuMs, (float)((now.QuadPart - s.cpuStart.QuadPart) * MS_PER_S / (double)s.qpcFreq.QuadPart));
        GetContext()->End(f.disjoint);
        f.issued = true;
        s.inFrame = false;
        s.current = (s.current + 1) % LATENCY;
        Collect(s.frames[s.current]);   // the frame issued LATENCY-1 frames ago
    }

    const std::vector<Entry>& Results() { return s.results; }
    float FrameMs() { return s.frameMs; }
    float CpuMs() { return s.cpuMs; }

    void Uninit()
    {
        for (Frame& f : s.frames)
        {
            SafeRelease(f.disjoint); SafeRelease(f.frameBegin); SafeRelease(f.frameEnd);
            for (int i = 0; i < MAX_SECTIONS; ++i) { SafeRelease(f.begin[i]); SafeRelease(f.end[i]); }
            f.issued = false; f.count = 0;
        }
        s.created = s.failed = s.inFrame = false;
        s.results.clear();
    }
}

#pragma once
#include <vector>

//====================================================================
//  GPU profiler: how many milliseconds each part of the frame costs ON THE GPU.
//
//  The CPU only queues draw calls; the GPU runs them later, so a CPU stopwatch cannot tell which
//  pass is slow. D3D11 timestamp queries let the GPU itself write the time when it reaches a
//  point in the command stream. A "disjoint" query around the frame gives the clock frequency
//  (and says if the clock jumped, e.g. a power-state change -> that frame is thrown away).
//  Results are read LATENCY frames later without waiting (the GPU is behind the CPU), then
//  smoothed so the numbers are readable.
//  Usage:  GpuProfiler::BeginFrame();  { GpuProfiler::Scope s("shadow"); ...draw... }  EndFrame();
//  Search: "D3D11 timestamp query profiling", "D3D11_QUERY_TIMESTAMP_DISJOINT"
//====================================================================
namespace GpuProfiler
{
    struct Entry { const char* name; float ms; };   // name = the literal passed to Scope (pointer kept)

    void BeginFrame();
    void EndFrame();
    int  Begin(const char* name);   // returns a slot for End (-1 if the frame is full)
    void End(int slot);

    struct Scope
    {
        int slot;
        explicit Scope(const char* name) : slot(Begin(name)) {}
        ~Scope() { End(slot); }
    };

    const std::vector<Entry>& Results();   // sections in first-seen order, smoothed ms
    float FrameMs();                        // whole GPU frame (BeginFrame .. EndFrame), smoothed
    float CpuMs();                          // CPU time spent issuing the frame (if close to FrameMs, the GPU may be WAITING for the CPU)
    void  Uninit();
}

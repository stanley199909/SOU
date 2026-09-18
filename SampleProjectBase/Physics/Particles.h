#pragma once
#include <DirectXMath.h>
#include <vector>

// CPU particle simulation for the forge (self-built physics).
// Two pools: hammer sparks (spawned in bursts, fall under gravity, bounce on the
// ground) and coal embers (emitted from the coal bed, rise on buoyancy, fade out).
// This owns the particle STATE and the motion physics. Drawing (building the
// billboards/streaks with shaders) stays in the renderer, which reads the pools.
class Particles
{
public:
    struct Particle
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 vel;
        float life;
        float maxLife;
        float size;
    };

    // Spawn a burst of sparks at a strike point. count = number, power/scale set energy.
    void SpawnSparks(const DirectX::XMFLOAT3& origin, int count, float power, float scale);

    // Emit embers this frame from an emitter box (centre + XZ radius) at `rate`
    // per second with upward speed `rise`. Fractional counts carry between frames.
    void EmitEmbers(const DirectX::XMFLOAT3& centre, float areaX, float areaZ,
                    float rate, float rise, float dt);

    // Advance both pools: sparks (gravity + ground bounce), embers (buoyancy + drift + fade).
    // `time` drives the embers' sideways shimmer.
    void Update(float dt, float time);

    void Clear();

    const std::vector<Particle>& Sparks() const { return m_sparks; }
    const std::vector<Particle>& Embers() const { return m_embers; }

    static const int MAX_SPARKS = 3000;
    static const int MAX_EMBERS = 500;

private:
    static constexpr float GRAVITY = 9.8f; // downward accel on sparks

    std::vector<Particle> m_sparks;
    std::vector<Particle> m_embers;
    float m_emberSpawn = 0.0f; // fractional ember count carried to the next frame
};

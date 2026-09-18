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

    // Tunable physics constants. Defaults reproduce the original hardcoded behaviour,
    // so callers that ignore this (the game) are unchanged; the Particle Lab scene edits
    // these live to see the effect. Grouped: spark spawn / spark motion / ember spawn / ember motion.
    struct Tune
    {
        // -- spark spawn (SpawnSparks) --
        float sparkSpeedMin = 2.5f, sparkSpeedMax = 7.0f;   // launch speed range (x power*scale)
        float sparkElevMin  = 0.25f, sparkElevMax = 1.4f;   // launch elevation angle (rad)
        float sparkUpBonusMin = 1.0f, sparkUpBonusMax = 3.0f; // extra straight-up kick
        float sparkLifeMin  = 0.5f, sparkLifeMax = 1.1f;    // seconds alive
        float sparkSizeMin  = 0.16f, sparkSizeMax = 0.30f;  // streak size
        // -- spark motion (Update) --
        float sparkGravity      = 9.8f;   // downward accel
        float sparkRestitution  = 0.3f;   // vertical energy kept per ground bounce
        float sparkFriction     = 0.6f;   // horizontal speed kept per bounce
        // -- ember spawn (EmitEmbers) --
        float emberLifeMin  = 1.2f, emberLifeMax = 2.6f;
        float emberSizeMin  = 0.02f, emberSizeMax = 0.05f;
        float emberDrift    = 0.15f;      // initial random sideways speed
        float emberRiseJitterMin = 0.7f, emberRiseJitterMax = 1.3f; // x the caller's rise
        // -- ember motion (Update) --
        float emberBuoyancy = 0.4f;       // upward accel (hot air)
        float emberShimmer  = 0.10f;      // sideways wobble strength
    };
    Tune tune;

private:
    std::vector<Particle> m_sparks;
    std::vector<Particle> m_embers;
    float m_emberSpawn = 0.0f; // fractional ember count carried to the next frame
};

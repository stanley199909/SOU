#pragma once
#include <DirectXMath.h>
#include <vector>

// CPU particle simulation for the forge (self-built physics).
// Three pools: hammer sparks (spawned in bursts, fall under gravity, bounce on the
// ground), coal embers (emitted from the coal bed, rise on buoyancy, fade out) and
// quench steam (rises on buoyancy, slowed by air drag, expands) and quench splash (water
// droplets thrown up when the hot blade hits the water; gravity, gone when back in the water).
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

    // Emit steam along a line (the blade at the waterline) at `rate` per second.
    // Steam = hot vapour: rises fast at first, is slowed by the air, spreads and grows. Each puff
    // starts within `spread` of the line; speedMul scales its launch speed (a burst at the
    // moment of contact is fast and violent, steady boiling is gentle).
    void EmitSteamLine(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float spread,
                       float rate, float dt, float speedMul);

    // Throw water droplets up from a line (the blade hitting the water). They fly under
    // gravity and disappear when they fall back below surfaceY (back into the water).
    void SpawnSplash(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, int count,
                     float speed, float surfaceY);

    // Advance all pools: sparks (gravity + ground bounce), embers (buoyancy + drift + fade),
    // steam (buoyancy + air drag + growth). `time` drives the embers' sideways shimmer.
    void Update(float dt, float time);

    void Clear();

    const std::vector<Particle>& Sparks() const { return m_sparks; }
    const std::vector<Particle>& Embers() const { return m_embers; }
    const std::vector<Particle>& Steam()  const { return m_steam; }
    const std::vector<Particle>& Splash() const { return m_splash; }

    static const int MAX_SPARKS = 3000;
    static const int MAX_EMBERS = 500;
    static const int MAX_STEAM  = 800;
    static const int MAX_SPLASH = 400;

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
        // -- steam (EmitSteam / Update) --
        float steamLifeMin  = 1.2f, steamLifeMax = 2.4f;
        float steamSizeMin  = 0.08f, steamSizeMax = 0.16f;  // starting puff radius
        float steamRiseMin  = 0.8f, steamRiseMax = 1.6f;    // initial upward speed
        float steamDrift    = 0.25f;      // initial random sideways speed
        float steamBuoyancy = 0.6f;       // upward accel (hot vapour)
        float steamDrag     = 1.4f;       // air drag rate (1/s): puffs slow down and hang
        float steamGrowth   = 0.35f;      // radius growth (/s): puffs expand as they cool
        // -- splash droplets (SpawnSplash / Update) --
        float splashLifeMax  = 1.2f;       // safety cap; normally they die on falling back into the water
        float splashSizeMin  = 0.010f, splashSizeMax = 0.022f;
        float splashUpMin    = 0.5f, splashUpMax = 1.0f;   // upward speed (x the caller's speed)
        float splashSide     = 0.35f;      // sideways speed (x the caller's speed)
        float splashGravity  = 9.8f;
    };
    Tune tune;

private:
    std::vector<Particle> m_sparks;
    std::vector<Particle> m_embers;
    std::vector<Particle> m_steam;
    std::vector<Particle> m_splash;
    float m_splashSurfaceY = 0.0f; // droplets below this (falling) have landed back in the water
    float m_emberSpawn = 0.0f; // fractional ember count carried to the next frame
    float m_steamSpawn = 0.0f; // fractional steam count carried to the next frame
};

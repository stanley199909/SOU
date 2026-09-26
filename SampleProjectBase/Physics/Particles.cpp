#include "Particles.h"
#include <cstdlib>
#include <cmath>

using namespace DirectX;

// Local random helpers (kept here so the module is self-contained).
static float rnd()                  { return (float)rand() / (float)RAND_MAX; }
static float rnd(float a, float b)  { return a + (b - a) * rnd(); }

void Particles::SpawnSparks(const XMFLOAT3& origin, int count, float power, float scale)
{
    for (int i = 0; i < count; ++i)
    {
        if ((int)m_sparks.size() >= MAX_SPARKS) break;
        Particle s = {};
        s.pos = origin;
        float a     = rnd(0.0f, 6.2832f);          // horizontal angle
        float elev  = rnd(tune.sparkElevMin, tune.sparkElevMax);   // elevation
        float speed = rnd(tune.sparkSpeedMin, tune.sparkSpeedMax) * power * scale;
        float h     = cosf(elev) * speed;
        s.vel     = XMFLOAT3(cosf(a) * h,
                             sinf(elev) * speed + rnd(tune.sparkUpBonusMin, tune.sparkUpBonusMax),
                             sinf(a) * h);
        s.maxLife = rnd(tune.sparkLifeMin, tune.sparkLifeMax);
        s.life    = s.maxLife;
        s.size    = rnd(tune.sparkSizeMin, tune.sparkSizeMax);
        m_sparks.push_back(s);
    }
}

void Particles::EmitEmbers(const XMFLOAT3& centre, float areaX, float areaZ,
    float rate, float rise, float dt)
{
    m_emberSpawn += dt * rate;
    int n = (int)m_emberSpawn; // whole embers to spawn this frame
    m_emberSpawn -= n;         // keep the fraction for next frame
    for (int k = 0; k < n && (int)m_embers.size() < MAX_EMBERS; ++k)
    {
        Particle e = {};
        // Denser toward the centre (rnd*rnd). No gravity: hot air carries them up.
        float rx = rnd(-1.0f, 1.0f) * rnd(0.0f, 1.0f) * areaX;
        float rz = rnd(-1.0f, 1.0f) * rnd(0.0f, 1.0f) * areaZ;
        e.pos     = XMFLOAT3(centre.x + rx, centre.y + 0.05f, centre.z + rz);
        e.vel     = XMFLOAT3(rnd(-tune.emberDrift, tune.emberDrift),
                             rise * rnd(tune.emberRiseJitterMin, tune.emberRiseJitterMax),
                             rnd(-tune.emberDrift, tune.emberDrift));
        e.maxLife = rnd(tune.emberLifeMin, tune.emberLifeMax);
        e.life    = e.maxLife;
        e.size    = rnd(tune.emberSizeMin, tune.emberSizeMax);
        m_embers.push_back(e);
    }
}

void Particles::EmitSteam(const XMFLOAT3& centre, float radius, float rate, float dt)
{
    m_steamSpawn += dt * rate;
    int n = (int)m_steamSpawn;
    m_steamSpawn -= n;
    for (int k = 0; k < n && (int)m_steam.size() < MAX_STEAM; ++k)
    {
        Particle p = {};
        // Uniform point on the disc (sqrt keeps the density even, not bunched at the centre).
        float a = rnd(0.0f, 6.2832f), r = sqrtf(rnd()) * radius;
        p.pos     = XMFLOAT3(centre.x + cosf(a) * r, centre.y, centre.z + sinf(a) * r);
        p.vel     = XMFLOAT3(rnd(-tune.steamDrift, tune.steamDrift),
                             rnd(tune.steamRiseMin, tune.steamRiseMax),
                             rnd(-tune.steamDrift, tune.steamDrift));
        p.maxLife = rnd(tune.steamLifeMin, tune.steamLifeMax);
        p.life    = p.maxLife;
        p.size    = rnd(tune.steamSizeMin, tune.steamSizeMax);
        m_steam.push_back(p);
    }
}

void Particles::Update(float dt, float time)
{
    // Sparks: gravity + bounce on the ground (y = 0).
    for (size_t i = 0; i < m_sparks.size(); )
    {
        Particle& s = m_sparks[i];
        s.life -= dt;
        if (s.life <= 0.0f) { s = m_sparks.back(); m_sparks.pop_back(); continue; }
        s.vel.y -= tune.sparkGravity * dt;
        s.pos.x += s.vel.x * dt;
        s.pos.y += s.vel.y * dt;
        s.pos.z += s.vel.z * dt;
        if (s.pos.y < 0.0f && s.vel.y < 0.0f) // bounce, losing energy
        {
            s.pos.y = 0.0f;
            s.vel.y = -s.vel.y * tune.sparkRestitution;
            s.vel.x *= tune.sparkFriction;
            s.vel.z *= tune.sparkFriction;
        }
        ++i;
    }

    // Embers: buoyancy (rise, accelerating) + a little sideways shimmer + fade.
    for (size_t i = 0; i < m_embers.size(); )
    {
        Particle& e = m_embers[i];
        e.life -= dt;
        if (e.life <= 0.0f) { e = m_embers.back(); m_embers.pop_back(); continue; }
        e.vel.y += tune.emberBuoyancy * dt;
        e.vel.x += sinf(time * 3.0f + e.pos.y * 8.0f) * tune.emberShimmer * dt;
        e.vel.z += cosf(time * 2.3f + e.pos.x * 8.0f) * tune.emberShimmer * dt;
        e.pos.x += e.vel.x * dt;
        e.pos.y += e.vel.y * dt;
        e.pos.z += e.vel.z * dt;
        ++i;
    }

    // Steam: buoyancy pushes it up, air drag (exact exponential decay, like the grind wheel)
    // slows it so puffs rise quickly then hang and spread; they grow while fading.
    const float steamKeep = expf(-tune.steamDrag * dt);
    for (size_t i = 0; i < m_steam.size(); )
    {
        Particle& p = m_steam[i];
        p.life -= dt;
        if (p.life <= 0.0f) { p = m_steam.back(); m_steam.pop_back(); continue; }
        p.vel.y += tune.steamBuoyancy * dt;
        p.vel.x *= steamKeep; p.vel.y *= steamKeep; p.vel.z *= steamKeep;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.z += p.vel.z * dt;
        p.size  += tune.steamGrowth * dt;
        ++i;
    }
}

void Particles::Clear()
{
    m_steam.clear();
    m_steamSpawn = 0.0f;
    m_sparks.clear();
    m_embers.clear();
    m_emberSpawn = 0.0f;
}

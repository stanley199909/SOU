#include "WaterSim.h"
#include <cmath>
#include <algorithm>

namespace
{
    const float PREFERRED_STEP = 1.0f / 120.0f; // sub-step when the CFL condition allows it
    const float CFL_SAFETY     = 0.7f;          // stay well under the stability limit of 1
    const int   MAX_SUBSTEPS   = 8;             // after a long frame (hitch), drop time instead of freezing
}

void WaterSim::Init(float sizeX, float sizeZ)
{
    m_sizeX = sizeX; m_sizeZ = sizeZ;
    m_dx = sizeX / NX;
    m_dz = sizeZ / NZ;

    // CFL: c*dt*sqrt(1/dx^2 + 1/dz^2) <= 1. Take the preferred step unless it is too big.
    const float limit = 1.0f / (waveSpeed * sqrtf(1.0f / (m_dx * m_dx) + 1.0f / (m_dz * m_dz)));
    m_stepDt = std::min(PREFERRED_STEP, CFL_SAFETY * limit);

    // Walls: outside the capsule outline (straight sides, round ends of radius = half width).
    // Same shape the water shader trims to, so the waves bounce exactly at the visible edge.
    const float aspect = std::max(sizeX / std::max(sizeZ, 1e-4f), 1.0f);
    for (int k = 0; k < NZ; ++k)
    for (int i = 0; i < NX; ++i)
    {
        const float u = ((i + 0.5f) / NX) * 2.0f - 1.0f;
        const float v = ((k + 0.5f) / NZ) * 2.0f - 1.0f;
        const float endU = std::max(fabsf(u) * aspect - (aspect - 1.0f), 0.0f);
        const bool border = (i == 0 || k == 0 || i == NX - 1 || k == NZ - 1);
        m_wall[k][i] = border || (endU * endU + v * v > 1.0f);
    }
    Reset();
}

void WaterSim::Reset()
{
    for (int k = 0; k < NZ; ++k)
    for (int i = 0; i < NX; ++i) { m_h[k][i] = 0.0f; m_prev[k][i] = 0.0f; }
    m_acc = 0.0f;
}

void WaterSim::Step(float dt)
{
    m_acc += dt;
    int steps = 0;
    while (m_acc >= m_stepDt && steps < MAX_SUBSTEPS)
    {
        m_acc -= m_stepDt;
        ++steps;

        const float cx = (waveSpeed * m_stepDt) * (waveSpeed * m_stepDt) / (m_dx * m_dx);
        const float cz = (waveSpeed * m_stepDt) * (waveSpeed * m_stepDt) / (m_dz * m_dz);
        const float keep = 1.0f - damping * m_stepDt;   // fraction of the velocity kept this step

        // The new heights overwrite m_prev (it is no longer needed), then the buffers swap.
        for (int k = 1; k < NZ - 1; ++k)
        for (int i = 1; i < NX - 1; ++i)
        {
            if (Wall(i, k)) { m_prev[k][i] = 0.0f; continue; }
            const float h = m_h[k][i];
            const float lap = cx * (m_h[k][i + 1] + m_h[k][i - 1] - 2.0f * h)
                            + cz * (m_h[k + 1][i] + m_h[k - 1][i] - 2.0f * h);
            float next = h + (h - m_prev[k][i]) * keep + lap;
            next = std::clamp(next, -maxHeight, maxHeight);
            m_prev[k][i] = next;
        }
        for (int k = 0; k < NZ; ++k)
        for (int i = 0; i < NX; ++i) std::swap(m_h[k][i], m_prev[k][i]);
    }
    if (steps == MAX_SUBSTEPS) m_acc = 0.0f;   // hitch: forget the backlog
}

void WaterSim::Push(float u, float v, float radius, float amount)
{
    // Cell under the point, and how many cells the radius covers in each direction.
    const float ci = (u * 0.5f + 0.5f) * NX - 0.5f;
    const float ck = (v * 0.5f + 0.5f) * NZ - 0.5f;
    const int   ri = (int)ceilf(radius / m_dx), rk = (int)ceilf(radius / m_dz);
    for (int k = std::max(0, (int)ck - rk); k <= std::min(NZ - 1, (int)ck + rk); ++k)
    for (int i = std::max(0, (int)ci - ri); i <= std::min(NX - 1, (int)ci + ri); ++i)
    {
        if (Wall(i, k)) continue;
        const float dx = (i - ci) * m_dx, dz = (k - ck) * m_dz;
        const float t = 1.0f - (dx * dx + dz * dz) / (radius * radius);
        if (t <= 0.0f) continue;
        m_h[k][i] = std::clamp(m_h[k][i] + amount * t * t, -maxHeight, maxHeight);   // smooth bump (t^2 falloff)
    }
}

void WaterSim::PushLine(float u0, float v0, float u1, float v1, float radius, float amount)
{
    // Stamp bumps along the line about half a radius apart, sharing the amount so a long
    // line pushes no harder per cell than a short one.
    const float lenX = (u1 - u0) * 0.5f * m_sizeX, lenZ = (v1 - v0) * 0.5f * m_sizeZ;
    const float len  = sqrtf(lenX * lenX + lenZ * lenZ);
    const float SPACING = 0.5f;
    const int   n = std::max(1, (int)(len / (radius * SPACING)));
    for (int s = 0; s <= n; ++s)
    {
        const float t = (float)s / n;
        Push(u0 + (u1 - u0) * t, v0 + (v1 - v0) * t, radius, amount * SPACING);
    }
}

#pragma once
#include <DirectXMath.h>

// Water surface of the quench trough as a height field driven by the 2D wave equation
// (self-built physics). Search: "2D wave equation heightfield", "water ripple simulation".
//
//   d2h/dt2 = c^2 * (d2h/dx2 + d2h/dz2) - damping * dh/dt
//
//   h = height of the surface at a grid cell (0 = still water). A disturbance (the blade
//   going in, a stroke, a boiling bubble) changes h locally; every cell is then pulled
//   toward the average of its neighbours, so the bump spreads as a real wave, bounces off
//   the trough walls and slowly calms down (damping).
//
// Discretisation (explicit, "leapfrog"/Verlet in time, central differences in space):
//   next = h + (h - prev) * (1 - damping*dt) + (c*dt)^2 * laplacian(h)
// Only the current and previous heights are stored; the velocity is implicit (h - prev).
// Stable only if c*dt*sqrt(1/dx^2 + 1/dz^2) <= 1 (the CFL condition), so the simulation
// runs in fixed sub-steps chosen at Init to satisfy it, independent of the frame rate.
//
// The grid covers the water quad in its own local space: u (length) and v (width) in -1..1.
// Cells outside the trough's rounded (capsule) outline are walls: height held at 0.
class WaterSim
{
public:
    static const int NX = 128;  // cells along the trough's length (local u)
    static const int NZ = 40;   // cells across its width (local v)

    // sizeX/sizeZ = full world size of the water quad. aspect is derived from them.
    void  Init(float sizeX, float sizeZ);
    void  Reset();                                  // still water
    void  Step(float dt);                           // advance (runs the fixed sub-steps)

    // Push the surface by `amount` (m, + = up) within `radius` (m) of a point / a line,
    // with a smooth falloff. Positions are in the quad's local space (-1..1).
    void  Push(float u, float v, float radius, float amount);
    void  PushLine(float u0, float v0, float u1, float v1, float radius, float amount);

    const float* Heights() const { return &m_h[0][0]; }   // NZ rows of NX floats (for the GPU)

    float waveSpeed = 1.0f;   // c (m/s). Shallow water moves at about sqrt(g*depth): ~1.4 m/s at 20 cm
    float damping   = 1.5f;   // energy loss (1/s): how fast the water calms down
    float maxHeight = 0.05f;  // clamp (m): keeps a violent burst from tearing the mesh

private:
    bool  Wall(int i, int k) const { return m_wall[k][i]; }

    float m_h[NZ][NX]    = {};
    float m_prev[NZ][NX] = {};
    bool  m_wall[NZ][NX] = {};
    float m_dx = 1.0f, m_dz = 1.0f;   // world size of one cell
    float m_sizeX = 1.0f, m_sizeZ = 1.0f;
    float m_stepDt = 1.0f / 120.0f;   // fixed sub-step (s), set by Init from the CFL condition
    float m_acc = 0.0f;               // unsimulated time carried to the next frame
};

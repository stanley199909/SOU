#pragma once
#include "MeshBuffer.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// Low-poly charcoal bed, built once on the CPU (shared by the game and the editor).
//
// Layout (local space, the bed spans -1..+1 in X/Z; DrawCoalBed scales it):
//   1. a flat glowing floor quad  -> the "gaps" between lumps (shade.a = 1 = emits)
//   2. COLUMNS x ROWS lumps on top -> each lump is a low cone: a bottom ring, a
//      narrower "shoulder" ring, and a peak. Lumps are dark (shade.a = 0) and cover
//      most of the floor, so only the cracks between them glow.
// A cheap deterministic sin/cos "variation" jitters position/height/shape per lump,
// so the bed looks irregular without a random generator (same result every run).
// No UVs are needed for a texture; uv is only the 0..1 floor position for PS_Coal.
// ---------------------------------------------------------------------------
namespace CoalBedMesh
{
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT2 uv;
        DirectX::XMFLOAT4 shade;   // rgb = facet brightness, a = 1 glowing gap / 0 charcoal
    };

    // --- layout ---
    const int   COLUMNS = 5;                 // lumps across X
    const int   ROWS    = 7;                 // lumps across Z
    const int   SIDES   = 7;                 // ring segments per lump (low-poly look)
    const float COVERAGE        = 0.94f;     // fraction of the bed the lumps occupy
    const float RADIUS_FRACTION = 0.46f;     // lump radius relative to its grid cell
    const float HEIGHT          = 0.045f;    // lump height (local units)
    const float SHOULDER_RADIUS = 0.72f;     // shoulder ring radius relative to the base ring
    const float SHOULDER_HEIGHT = 0.65f;     // shoulder ring height relative to the peak

    // --- per-lump variation (deterministic) ---
    const float VARIATION_FREQUENCY = 2.17f; // sin() step between lump indices
    const float ROW_PHASE           = 1.37f; // phase shift per row for the Z jitter
    const float JITTER_FRACTION     = 0.12f; // position jitter, in grid cells
    const float HEIGHT_VARIATION    = 0.25f; // +-25% height
    const float SIDE_VARIATION      = 0.10f; // +-10% radius wobble around the ring

    // --- facet shading (fake lighting baked into vertex colour) ---
    const float SHADE_BASE  = 0.36f;
    const float SHADE_RANGE = 0.30f;         // shade = base + range * cos(facet angle)

    const float TAU = 6.28318530718f;

    inline std::shared_ptr<MeshBuffer> Create()
    {
        std::vector<Vertex> vertices;

        auto vertex = [](float x, float y, float z, float shade, float ember)
        {
            return Vertex{ { x, y, z }, { (x + 1) * 0.5f, (z + 1) * 0.5f }, { shade, shade, shade, ember } };
        };
        // Emit both windings so the triangle is visible regardless of cull mode.
        auto triangle = [&](Vertex a, Vertex b, Vertex c)
        {
            vertices.insert(vertices.end(), { a, b, c, c, b, a });
        };

        // 1. Glowing floor quad (the gaps).
        triangle(vertex(-1, 0, -1, 0, 1), vertex(-1, 0, 1, 0, 1), vertex(1, 0, -1, 0, 1));
        triangle(vertex( 1, 0, -1, 0, 1), vertex(-1, 0, 1, 0, 1), vertex(1, 0,  1, 0, 1));

        // 2. Charcoal lumps.
        const float cellX = COVERAGE * 2 / COLUMNS;  // grid cell size in local units
        const float cellZ = COVERAGE * 2 / ROWS;
        for (int row = 0; row < ROWS; ++row)
        for (int col = 0; col < COLUMNS; ++col)
        {
            const float variation = std::sin((row * COLUMNS + col) * VARIATION_FREQUENCY);   // -1..1
            const float x = COVERAGE * ((col + 0.5f + JITTER_FRACTION * variation) * 2 / COLUMNS - 1);
            const float z = COVERAGE * ((row + 0.5f + JITTER_FRACTION * std::cos(col + row * ROW_PHASE)) * 2 / ROWS - 1);
            const float height = HEIGHT * (1 + HEIGHT_VARIATION * variation);

            for (int side = 0; side < SIDES; ++side)
            {
                const float a = TAU * side / SIDES + variation;
                const float b = TAU * (side + 1) / SIDES + variation;
                const float shade = SHADE_BASE + SHADE_RANGE * std::cos(a);

                // A point on a ring around the lump centre.
                auto ring = [&](float angle, float radius, float y)
                {
                    radius *= 1 + SIDE_VARIATION * std::sin(angle * SIDES + col + row);
                    return vertex(x + std::cos(angle) * cellX * RADIUS_FRACTION * radius, y,
                                  z + std::sin(angle) * cellZ * RADIUS_FRACTION * radius, shade, 0);
                };
                Vertex lowA  = ring(a, 1, 0);
                Vertex lowB  = ring(b, 1, 0);
                Vertex highA = ring(a, SHOULDER_RADIUS, height * SHOULDER_HEIGHT);
                Vertex highB = ring(b, SHOULDER_RADIUS, height * SHOULDER_HEIGHT);

                triangle(lowA, highA, lowB);                         // side wall
                triangle(lowB, highA, highB);
                triangle(highA, vertex(x, height, z, shade, 0), highB); // cap to the peak
            }
        }

        MeshBuffer::Description d = {};
        d.pVtx     = vertices.data();
        d.vtxSize  = sizeof(Vertex);
        d.vtxCount = static_cast<UINT>(vertices.size());
        d.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        return std::make_shared<MeshBuffer>(d);
    }
}

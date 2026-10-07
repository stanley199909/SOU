#pragma once
#include <DirectXMath.h>
#include <vector>

// Top-down (XZ plane) collision for walking around the smithy (self-built physics).
//
// The player only walks on the floor, so 3D collision is flattened to 2D:
//   - The player is a CIRCLE (a capsule seen from above). No corners -> slides smoothly
//     along slanted edges and round corners instead of snagging.
//   - Two kinds of collision proxy, picked by the SHAPE of the object:
//       * Hull:    a free-standing prop (anvil, bucket...) is roughly convex -> the 2D
//                  CONVEX HULL of its vertices projected onto the floor. Cheap, tight.
//       * Segment: a building is concave (a room!) - a hull would fill the whole room.
//                  Instead the mesh is SLICED by a horizontal plane at waist height; the
//                  cut is the floor plan as line segments. Openings at that height (a
//                  doorway) produce no segments, so they are passable automatically.
//                  This is the 2D version of per-triangle ("complex") collision.
//     Both are built automatically from the model: no hand-placed boxes.
//   - Response = push-out: after moving, if the circle overlaps a proxy it is pushed
//     back along the contact normal by the overlap depth. What remains of the motion
//     is the part parallel to the wall -> the player slides along it.
//
// Points use XMFLOAT2 where .x = world X and .y = world Z.
namespace Collision2D
{
    using Hull = std::vector<DirectX::XMFLOAT2>;   // convex polygon, counter-clockwise in (x, z)
    struct Segment { DirectX::XMFLOAT2 a, b; };     // one piece of a wall's cross-section

    // Everything the player can bump into this frame.
    struct World
    {
        std::vector<Hull>    hulls;
        std::vector<Segment> segments;
        void Clear() { hulls.clear(); segments.clear(); }
    };

    // Andrew's monotone chain: sort points, build lower and upper chains, O(n log n).
    // Returns the hull counter-clockwise. Fewer than 3 distinct points -> empty hull.
    Hull ConvexHull(std::vector<DirectX::XMFLOAT2> pts);

    // Split a triangle soup into connected pieces (triangles sharing a vertex position
    // belong together). A model like "a pile of rocks" is several separate rocks: one hull
    // around all of them would also block the gaps between them, one hull PER PIECE does not
    // (a compound collider). Uses union-find over vertex positions.
    std::vector<std::vector<DirectX::XMFLOAT3>> SplitConnected(const std::vector<DirectX::XMFLOAT3>& tris);

    // Cut a triangle soup (every 3 points = one triangle) with the plane y = sliceY and
    // append the cut lines. A triangle crossing the plane has exactly two edges crossing
    // it; the two crossing points form one segment.
    void SliceTriangles(const std::vector<DirectX::XMFLOAT3>& tris, float sliceY, std::vector<Segment>& out);

    // If the circle (center, radius) overlaps the shape, move center out along the
    // contact normal so they just touch, and return true.
    bool PushCircleOut(DirectX::XMFLOAT2& center, float radius, const Hull& hull);
    bool PushCircleOut(DirectX::XMFLOAT2& center, float radius, const Segment& seg);

    // 2D ray cast: distance from origin along dir (unit) to the first hull edge or wall
    // segment it crosses, or maxDist if nothing is hit. Used to keep a held object out of
    // props/walls (first-person weapon wall-clipping avoidance).
    float RayCast(const World& world, DirectX::XMFLOAT2 origin, DirectX::XMFLOAT2 dir, float maxDist);
}

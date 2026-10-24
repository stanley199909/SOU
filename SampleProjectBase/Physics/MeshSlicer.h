#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cstdint>

// Real-time mesh slicing by a plane (self-built, engine-independent).
// Search keywords: "mesh slicing plane", "mesh cutting capping", "ear clipping triangulation".
//
// Input: a CLOSED (watertight) triangle mesh and a plane (point + normal).
// Output: two closed meshes, one on each side of the plane.
//   1. Classify: signed distance of every vertex to the plane, d = dot(n, p - p0).
//   2. Split:    a triangle whose vertices are all on one side is copied as is.
//                A triangle that crosses the plane has one vertex alone on one side;
//                its two edges that cross are cut at the crossing points P, Q
//                (linear interpolation of position/normal/uv by t = d0 / (d0 - d1)).
//                The lone side gets 1 triangle, the other side gets 2 (a quad).
//                Each split triangle adds one segment P-Q to the cut outline.
//   3. Cap:      the segments are joined into closed loops (the cross-section outline).
//                Each loop is flattened onto the plane and triangulated by EAR CLIPPING
//                (works for concave outlines, e.g. a curved breastplate), then added to
//                both sides with opposite facing (front side's cap faces -n, back side's +n).
// Limits (fine for the test-cut parts, which are simple closed solids):
//   - a cross-section with a hole (a hollow tube) is capped as if it were solid;
//   - one side may consist of several disconnected pieces; they stay in one mesh.
namespace MeshSlicer
{
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 uv;
    };

    struct Mesh
    {
        std::vector<Vertex>   verts;
        std::vector<uint32_t> indices;   // 3 per triangle
        bool Empty() const { return indices.empty(); }
    };

    // Slice 'in' by the plane through 'point' with normal 'normal' (need not be unit).
    // front = the side the normal points to, back = the other side.
    // Returns false (and leaves front/back empty) if the plane misses the mesh.
    bool Slice(const Mesh& in, const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal,
               Mesh& front, Mesh& back);

    // Winding convention of a mesh: +1 if cross(b-a, c-a) points along the stored vertex
    // normals, -1 if against. Caps are emitted with the same convention as the input, so
    // they survive back-face culling whatever handedness the model was exported with.
    int WindingSign(const Mesh& m);

    // Average vertex position (used as the pivot of a falling piece).
    DirectX::XMFLOAT3 Centroid(const Mesh& m);

    // Volume of a closed mesh (m^3). Each triangle and the origin form a tetrahedron whose
    // SIGNED volume is a . (b x c) / 6; summed over a closed surface, the parts outside cancel
    // and the inside remains (divergence theorem). Search: "mesh volume signed tetrahedron".
    float Volume(const Mesh& m);
}

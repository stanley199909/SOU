#include "MeshSlicer.h"
#include <unordered_map>
#include <cmath>
#include <algorithm>

using namespace DirectX;

namespace
{
    // A vertex closer to the plane than this counts as "on" the plane. Such vertices make
    // zero-length cut segments and break the outline loops, so the plane is nudged instead.
    const float PLANE_EPS      = 1e-5f;  // metres
    const float PLANE_NUDGE    = 4e-5f;  // how far the plane is moved per retry
    const int   MAX_NUDGES     = 8;
    // Cut points closer than this are the same point (the two triangles sharing an edge
    // compute the same crossing point, up to float rounding).
    const float WELD_CELL      = 1e-4f;
    // Cross products smaller than this are treated as zero (collinear / degenerate).
    const float AREA_EPS       = 1e-12f;

    XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) { return XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z); }
    float    Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        return XMFLOAT3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }
    XMFLOAT3 Normalize(const XMFLOAT3& a)
    {
        float l = sqrtf(Dot(a, a));
        return (l > 0.0f) ? XMFLOAT3(a.x / l, a.y / l, a.z / l) : a;
    }

    // Vertex on the edge a-b at parameter t (0 = a, 1 = b).
    MeshSlicer::Vertex LerpVertex(const MeshSlicer::Vertex& a, const MeshSlicer::Vertex& b, float t)
    {
        MeshSlicer::Vertex v;
        v.pos    = XMFLOAT3(a.pos.x + (b.pos.x - a.pos.x) * t, a.pos.y + (b.pos.y - a.pos.y) * t, a.pos.z + (b.pos.z - a.pos.z) * t);
        v.normal = Normalize(XMFLOAT3(a.normal.x + (b.normal.x - a.normal.x) * t, a.normal.y + (b.normal.y - a.normal.y) * t,
                                      a.normal.z + (b.normal.z - a.normal.z) * t));
        v.uv     = XMFLOAT2(a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t);
        return v;
    }

    void AddTri(MeshSlicer::Mesh& m, const MeshSlicer::Vertex& a, const MeshSlicer::Vertex& b, const MeshSlicer::Vertex& c)
    {
        uint32_t base = (uint32_t)m.verts.size();
        m.verts.push_back(a); m.verts.push_back(b); m.verts.push_back(c);
        m.indices.push_back(base); m.indices.push_back(base + 1); m.indices.push_back(base + 2);
    }

    // ---- cut outline: segments -> welded points -> closed loops ----
    struct Outline
    {
        std::vector<XMFLOAT3> points;                 // welded cut points
        std::vector<std::pair<int, int>> edges;       // segments between point ids
        std::unordered_map<long long, int> lookup;    // grid cell -> point id

        int PointId(const XMFLOAT3& p)
        {
            // Hash the grid cell of the point. Two crossings of the same mesh edge land in
            // the same cell (a point exactly on a cell border could split, but the plane
            // nudge keeps crossings away from vertices, and the cell is far below model detail).
            const long long CELL_RANGE = 1 << 20;     // cells per axis in the key (+-~100 m at 1e-4)
            long long ix = (long long)floorf(p.x / WELD_CELL) & (CELL_RANGE - 1);
            long long iy = (long long)floorf(p.y / WELD_CELL) & (CELL_RANGE - 1);
            long long iz = (long long)floorf(p.z / WELD_CELL) & (CELL_RANGE - 1);
            long long key = (ix << 40) | (iy << 20) | iz;
            auto it = lookup.find(key);
            if (it != lookup.end()) return it->second;
            int id = (int)points.size();
            points.push_back(p);
            lookup[key] = id;
            return id;
        }
        void AddSegment(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            int ia = PointId(a), ib = PointId(b);
            if (ia != ib) edges.push_back({ ia, ib });   // zero-length segment = no outline
        }

        // Walk the segments into closed loops. On a closed mesh every cut point has exactly
        // two segments (the two triangles sharing the cut edge), so the walk is unambiguous.
        // Direction does not matter here: the loop is re-oriented when it is triangulated.
        std::vector<std::vector<int>> Loops() const
        {
            std::vector<std::vector<int>> adj(points.size());
            for (int e = 0; e < (int)edges.size(); ++e)
            {
                adj[edges[e].first].push_back(e);
                adj[edges[e].second].push_back(e);
            }
            std::vector<bool> used(edges.size(), false);
            std::vector<std::vector<int>> loops;
            for (int e0 = 0; e0 < (int)edges.size(); ++e0)
            {
                if (used[e0]) continue;
                used[e0] = true;
                std::vector<int> loop{ edges[e0].first };
                int start = edges[e0].first, cur = edges[e0].second;
                while (cur != start)
                {
                    loop.push_back(cur);
                    int next = -1;
                    for (int e : adj[cur])
                        if (!used[e]) { used[e] = true; next = (edges[e].first == cur) ? edges[e].second : edges[e].first; break; }
                    if (next < 0) break;                  // open outline (mesh not closed): drop it
                    cur = next;
                }
                if (cur == start && loop.size() >= 3) loops.push_back(loop);
            }
            return loops;
        }
    };

    // ---- ear clipping (search: "ear clipping triangulation", JP: mimi-kiri-hou) ----
    // An "ear" = three consecutive corners (a, b, c) where b is convex and no other corner
    // lies inside triangle abc. Cutting it off leaves a polygon with one corner fewer;
    // repeat until a triangle remains. O(n^2), fine for a cross-section of a few hundred points.
    float Cross2(const XMFLOAT2& o, const XMFLOAT2& a, const XMFLOAT2& b)
    {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }
    bool InTriangle(const XMFLOAT2& p, const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
    {
        // counter-clockwise triangle: inside = left of all three edges
        return Cross2(a, b, p) > AREA_EPS && Cross2(b, c, p) > AREA_EPS && Cross2(c, a, p) > AREA_EPS;
    }

    // Triangulate a simple polygon. 'poly' holds 2D points; output triangles index into it,
    // all counter-clockwise.
    void EarClip(const std::vector<XMFLOAT2>& poly, std::vector<int>& outTris)
    {
        std::vector<int> idx(poly.size());
        for (int i = 0; i < (int)poly.size(); ++i) idx[i] = i;

        // Make the polygon counter-clockwise (shoelace formula = twice the signed area).
        float area2 = 0.0f;
        for (size_t i = 0; i < poly.size(); ++i)
        {
            const XMFLOAT2& a = poly[i];
            const XMFLOAT2& b = poly[(i + 1) % poly.size()];
            area2 += a.x * b.y - b.x * a.y;
        }
        if (area2 < 0.0f) std::reverse(idx.begin(), idx.end());

        int guard = (int)(idx.size() * idx.size()) + 8;   // an ear always exists for a simple polygon; this only stops bad input
        while (idx.size() > 3 && guard-- > 0)
        {
            bool clipped = false;
            const int n = (int)idx.size();
            for (int i = 0; i < n; ++i)
            {
                int ia = idx[(i + n - 1) % n], ib = idx[i], ic = idx[(i + 1) % n];
                const XMFLOAT2 &a = poly[ia], &b = poly[ib], &c = poly[ic];
                float turn = Cross2(a, b, c);
                if (fabsf(turn) <= AREA_EPS) { idx.erase(idx.begin() + i); clipped = true; break; }  // collinear corner: drop it
                if (turn < 0.0f) continue;                                                           // reflex corner: not an ear
                bool empty = true;
                for (int k = 0; k < n && empty; ++k)
                {
                    int ik = idx[k];
                    if (ik == ia || ik == ib || ik == ic) continue;
                    if (InTriangle(poly[ik], a, b, c)) empty = false;
                }
                if (!empty) continue;
                outTris.push_back(ia); outTris.push_back(ib); outTris.push_back(ic);
                idx.erase(idx.begin() + i);
                clipped = true;
                break;
            }
            if (!clipped) break;                       // self-intersecting outline: fall back to a fan below
        }
        // The last triangle, or a fan over whatever could not be clipped (bad input only).
        for (size_t i = 1; i + 1 < idx.size(); ++i)
        {
            outTris.push_back(idx[0]); outTris.push_back(idx[i]); outTris.push_back(idx[i + 1]);
        }
    }
}

namespace MeshSlicer
{
    int WindingSign(const Mesh& m)
    {
        float sum = 0.0f;
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
        {
            const Vertex& a = m.verts[m.indices[i]];
            const Vertex& b = m.verts[m.indices[i + 1]];
            const Vertex& c = m.verts[m.indices[i + 2]];
            XMFLOAT3 g = Cross(Sub(b.pos, a.pos), Sub(c.pos, a.pos));
            XMFLOAT3 n(a.normal.x + b.normal.x + c.normal.x, a.normal.y + b.normal.y + c.normal.y, a.normal.z + b.normal.z + c.normal.z);
            sum += Dot(g, n);
        }
        return (sum >= 0.0f) ? 1 : -1;
    }

    XMFLOAT3 Centroid(const Mesh& m)
    {
        XMFLOAT3 c(0, 0, 0);
        if (m.verts.empty()) return c;
        for (const Vertex& v : m.verts) { c.x += v.pos.x; c.y += v.pos.y; c.z += v.pos.z; }
        float inv = 1.0f / (float)m.verts.size();
        return XMFLOAT3(c.x * inv, c.y * inv, c.z * inv);
    }

    float Volume(const Mesh& m)
    {
        float six = 0.0f;   // 6 x the signed volume
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
        {
            const XMFLOAT3& a = m.verts[m.indices[i]].pos;
            const XMFLOAT3& b = m.verts[m.indices[i + 1]].pos;
            const XMFLOAT3& c = m.verts[m.indices[i + 2]].pos;
            six += Dot(a, Cross(b, c));
        }
        return fabsf(six) / 6.0f;   // the sign only says which way the triangles are wound
    }

    bool Slice(const Mesh& in, const XMFLOAT3& point, const XMFLOAT3& normalIn, Mesh& front, Mesh& back)
    {
        front = Mesh(); back = Mesh();
        const XMFLOAT3 n = Normalize(normalIn);
        float offset = Dot(n, point);                 // plane: dot(n, p) = offset

        // 1. signed distances (nudge the plane off any vertex lying on it)
        std::vector<float> dist(in.verts.size());
        for (int tries = 0; tries <= MAX_NUDGES; ++tries)
        {
            bool onPlane = false;
            for (size_t i = 0; i < in.verts.size(); ++i)
            {
                dist[i] = Dot(n, in.verts[i].pos) - offset;
                if (fabsf(dist[i]) < PLANE_EPS) onPlane = true;
            }
            if (!onPlane) break;
            offset += PLANE_NUDGE;
        }
        bool anyFront = false, anyBack = false;
        for (float d : dist) { if (d > 0.0f) anyFront = true; else anyBack = true; }
        if (!anyFront || !anyBack) return false;      // the plane misses the mesh

        const int winding = WindingSign(in);
        Outline outline;

        // 2. split every triangle
        for (size_t i = 0; i + 2 < in.indices.size(); i += 3)
        {
            const uint32_t id[3] = { in.indices[i], in.indices[i + 1], in.indices[i + 2] };
            const bool side[3] = { dist[id[0]] > 0.0f, dist[id[1]] > 0.0f, dist[id[2]] > 0.0f };
            const int nFront = (int)side[0] + (int)side[1] + (int)side[2];
            if (nFront == 3) { AddTri(front, in.verts[id[0]], in.verts[id[1]], in.verts[id[2]]); continue; }
            if (nFront == 0) { AddTri(back,  in.verts[id[0]], in.verts[id[1]], in.verts[id[2]]); continue; }

            // the lone vertex = the one whose side differs from the other two
            int lone = 0;
            for (int k = 0; k < 3; ++k)
                if (side[k] != side[(k + 1) % 3] && side[k] != side[(k + 2) % 3]) lone = k;
            // rotate so the order stays a, b, c (keeps the winding)
            const Vertex& A = in.verts[id[lone]];
            const Vertex& B = in.verts[id[(lone + 1) % 3]];
            const Vertex& C = in.verts[id[(lone + 2) % 3]];
            const float dA = dist[id[lone]], dB = dist[id[(lone + 1) % 3]], dC = dist[id[(lone + 2) % 3]];
            const Vertex P = LerpVertex(A, B, dA / (dA - dB));   // crossing on edge A-B
            const Vertex Q = LerpVertex(A, C, dA / (dA - dC));   // crossing on edge A-C

            Mesh& loneSide  = side[lone] ? front : back;
            Mesh& otherSide = side[lone] ? back  : front;
            AddTri(loneSide, A, P, Q);                            // the tip
            AddTri(otherSide, P, B, C);                           // the quad P-B-C-Q as two triangles
            AddTri(otherSide, P, C, Q);
            outline.AddSegment(P.pos, Q.pos);
        }

        // 3. cap every closed loop of the outline
        // 2D basis on the plane: u, v with cross(u, v) = n, so a counter-clockwise
        // triangle in (u, v) faces +n.
        XMFLOAT3 helper = (fabsf(n.y) < 0.9f) ? XMFLOAT3(0, 1, 0) : XMFLOAT3(1, 0, 0);
        XMFLOAT3 u = Normalize(Cross(helper, n));
        XMFLOAT3 v = Cross(n, u);
        const XMFLOAT3 negN(-n.x, -n.y, -n.z);

        for (const std::vector<int>& loop : outline.Loops())
        {
            std::vector<XMFLOAT2> poly(loop.size());
            for (size_t k = 0; k < loop.size(); ++k)
            {
                const XMFLOAT3& p = outline.points[loop[k]];
                poly[k] = XMFLOAT2(Dot(p, u), Dot(p, v));
            }
            std::vector<int> tris;
            EarClip(poly, tris);
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
            {
                Vertex c[3];
                for (int k = 0; k < 3; ++k)
                {
                    c[k].pos = outline.points[loop[tris[t + k]]];
                    c[k].uv  = poly[tris[t + k]];             // planar uv on the cut face
                }
                // back side: cap faces +n (toward the front side); front side: cap faces -n.
                // A CCW triangle faces +n geometrically; flip it when the mesh's winding
                // convention is the opposite one.
                for (int k = 0; k < 3; ++k) c[k].normal = n;
                if (winding > 0) AddTri(back, c[0], c[1], c[2]); else AddTri(back, c[0], c[2], c[1]);
                for (int k = 0; k < 3; ++k) c[k].normal = negN;
                if (winding > 0) AddTri(front, c[0], c[2], c[1]); else AddTri(front, c[0], c[1], c[2]);
            }
        }
        return true;
    }
}

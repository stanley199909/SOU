#include "Collision2D.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <tuple>

using namespace DirectX;

namespace
{
    // 2D cross product of (a - o) and (b - o). > 0 = counter-clockwise turn o->a->b.
    float Cross(const XMFLOAT2& o, const XMFLOAT2& a, const XMFLOAT2& b)
    {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }

    // Distances below this are treated as zero (avoids dividing by ~0 when normalizing).
    constexpr float EPS = 1e-6f;

    // Closest point to c on the segment a-b: project c onto the line, clamp to the ends.
    XMFLOAT2 ClosestOnSegment(const XMFLOAT2& c, const XMFLOAT2& a, const XMFLOAT2& b)
    {
        const float ex = b.x - a.x, ez = b.y - a.y;
        const float len2 = ex * ex + ez * ez;
        if (len2 < EPS * EPS) return a;
        float t = ((c.x - a.x) * ex + (c.y - a.y) * ez) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
        return XMFLOAT2(a.x + ex * t, a.y + ez * t);
    }

    float DistSq(const XMFLOAT2& p, const XMFLOAT2& q)
    {
        return (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y);
    }

    // Shared "outside" response: if the nearest shape point p is within the radius,
    // push c straight away from p until the distance equals the radius.
    bool PushAwayFrom(XMFLOAT2& c, float r, const XMFLOAT2& p)
    {
        const float d2 = DistSq(c, p);
        if (d2 >= r * r) return false;
        const float d = sqrtf(d2);
        if (d < EPS) return false;                           // exactly on the boundary: no direction
        const float push = r - d;
        c.x += (c.x - p.x) / d * push;
        c.y += (c.y - p.y) / d * push;
        return true;
    }
}

namespace Collision2D
{
    Hull ConvexHull(std::vector<XMFLOAT2> pts)
    {
        // Sort by x, then by y (= world z). Duplicates end up adjacent and are removed.
        std::sort(pts.begin(), pts.end(), [](const XMFLOAT2& a, const XMFLOAT2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        pts.erase(std::unique(pts.begin(), pts.end(), [](const XMFLOAT2& a, const XMFLOAT2& b) {
            return a.x == b.x && a.y == b.y;
        }), pts.end());
        const size_t n = pts.size();
        if (n < 3) return Hull();

        // Walk left->right for the lower chain, then right->left for the upper chain.
        // Whenever the last two kept points and the new one do NOT turn counter-clockwise,
        // the middle point is inside (or on) the hull -> pop it.
        Hull h(2 * n);
        size_t k = 0;
        for (size_t i = 0; i < n; ++i)                        // lower chain
        {
            while (k >= 2 && Cross(h[k - 2], h[k - 1], pts[i]) <= 0.0f) --k;
            h[k++] = pts[i];
        }
        for (size_t i = n - 1, lower = k + 1; i-- > 0; )      // upper chain
        {
            while (k >= lower && Cross(h[k - 2], h[k - 1], pts[i]) <= 0.0f) --k;
            h[k++] = pts[i];
        }
        h.resize(k - 1);    // the last point equals the first one
        if (h.size() < 3) return Hull();
        return h;
    }

    std::vector<std::vector<XMFLOAT3>> SplitConnected(const std::vector<XMFLOAT3>& tris)
    {
        // 1) Give every distinct position an id (the importer already merged identical vertices,
        //    so shared corners have bit-identical coordinates).
        std::map<std::tuple<float, float, float>, int> idOf;
        std::vector<int> vid(tris.size());
        for (size_t i = 0; i < tris.size(); ++i)
        {
            auto key = std::make_tuple(tris[i].x, tris[i].y, tris[i].z);
            auto it = idOf.find(key);
            if (it == idOf.end()) it = idOf.emplace(key, (int)idOf.size()).first;
            vid[i] = it->second;
        }

        // 2) Union-find: the 3 corners of a triangle are in the same piece.
        std::vector<int> parent(idOf.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
        auto find = [&](int x) {
            while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }   // path halving
            return x;
        };
        for (size_t i = 0; i + 2 < tris.size(); i += 3)
        {
            int a = find(vid[i]), b = find(vid[i + 1]), c = find(vid[i + 2]);
            parent[b] = a;
            parent[find(c)] = a;
        }

        // 3) Bucket the triangle corners by their root.
        std::map<int, size_t> pieceOf;
        std::vector<std::vector<XMFLOAT3>> pieces;
        for (size_t i = 0; i < tris.size(); ++i)
        {
            int root = find(vid[i]);
            auto it = pieceOf.find(root);
            if (it == pieceOf.end()) { it = pieceOf.emplace(root, pieces.size()).first; pieces.emplace_back(); }
            pieces[it->second].push_back(tris[i]);
        }
        return pieces;
    }

    void SliceTriangles(const std::vector<XMFLOAT3>& tris, float y, std::vector<Segment>& out)
    {
        for (size_t i = 0; i + 2 < tris.size(); i += 3)
        {
            const XMFLOAT3* v[3] = { &tris[i], &tris[i + 1], &tris[i + 2] };
            // Walk the 3 edges; an edge crosses the plane when its ends are on opposite sides.
            // Linear interpolation along the edge gives the crossing point.
            XMFLOAT2 hit[2];
            int n = 0;
            for (int e = 0; e < 3 && n < 2; ++e)
            {
                const XMFLOAT3& p = *v[e];
                const XMFLOAT3& q = *v[(e + 1) % 3];
                const bool pBelow = p.y < y, qBelow = q.y < y;
                if (pBelow == qBelow) continue;              // both on the same side: no crossing
                const float t = (y - p.y) / (q.y - p.y);     // q.y != p.y because they are on different sides
                hit[n++] = XMFLOAT2(p.x + (q.x - p.x) * t, p.z + (q.z - p.z) * t);
            }
            if (n == 2 && DistSq(hit[0], hit[1]) > EPS * EPS)
                out.push_back(Segment{ hit[0], hit[1] });
        }
    }

    bool PushCircleOut(XMFLOAT2& c, float r, const Hull& hull)
    {
        const size_t n = hull.size();
        if (n < 3) return false;

        // For each edge: the signed distance of the center from the edge line along the
        // OUTWARD normal (> 0 = outside that edge), and the closest point on the edge segment.
        bool  inside   = true;
        float maxSide  = -FLT_MAX;   // inside case: the edge the center is closest to
        XMFLOAT2 maxN  = { 0, 0 };
        float bestD2   = FLT_MAX;    // outside case: closest point on the whole boundary
        XMFLOAT2 bestP = { 0, 0 };
        for (size_t i = 0; i < n; ++i)
        {
            const XMFLOAT2& a = hull[i];
            const XMFLOAT2& b = hull[(i + 1) % n];
            const float ex = b.x - a.x, ez = b.y - a.y;
            const float len = sqrtf(ex * ex + ez * ez);
            if (len < EPS) continue;
            const XMFLOAT2 nrm = { ez / len, -ex / len };     // outward normal of a CCW polygon

            const float side = (c.x - a.x) * nrm.x + (c.y - a.y) * nrm.y;
            if (side > 0.0f) inside = false;
            if (side > maxSide) { maxSide = side; maxN = nrm; }

            const XMFLOAT2 p = ClosestOnSegment(c, a, b);
            const float d2 = DistSq(c, p);
            if (d2 < bestD2) { bestD2 = d2; bestP = p; }
        }

        if (inside)
        {
            // Center is inside the hull: leave through the nearest edge (maxSide <= 0 is
            // how deep we are behind it), ending exactly one radius outside it.
            const float push = r - maxSide;
            c.x += maxN.x * push;
            c.y += maxN.y * push;
            return true;
        }

        // Center is outside: overlap only if the nearest boundary point is within the radius.
        return PushAwayFrom(c, r, bestP);
    }

    bool PushCircleOut(XMFLOAT2& c, float r, const Segment& s)
    {
        // A segment has no inside, so only the "outside" case exists.
        return PushAwayFrom(c, r, ClosestOnSegment(c, s.a, s.b));
    }
}

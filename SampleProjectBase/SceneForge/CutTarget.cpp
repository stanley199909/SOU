// Part of the test cut (tameshigiri). See CutTarget.h.
// ASCII comments only (new file without BOM).
#include "SceneForge/CutTarget.h"
#include "MeshBuffer.h"
#include "LitRender.h"
#include "PropMaterials.h"
#include "TextureCache.h"
#include "Geometory.h"
#include "CameraBase.h"
#include "AimSystem.h"
#include "Collision2D.h"
#include "DirectX.h"
#include <cfloat>
#include <cmath>

using namespace DirectX;

namespace
{
    // The point that decides which side of a cut keeps standing: the foot of the post,
    // a little above the ground so a cut exactly at ground level still has a side.
    const XMFLOAT3 FOOT_ANCHOR(0.0f, 0.02f, 0.0f);
    // Cut-face vertices: closer to the cut plane than this (metres) belong to the cap.
    const float CAP_EPS = 1e-3f;
    const float MIN_DEBRIS_MASS = 0.2f;   // kg: a thin sliver still gets a sensible mass
    const int   DISC_SEGMENTS = 24;       // F4 debug circle
    const float LAND_SLOP = 0.01f;        // m: a falling piece this close to the ground has landed

    // A closed box: 6 faces x 2 triangles, wound so cross(b - a, c - a) points outward
    // (the D3D front face). Each face has its own 4 vertices (sharp edges).
    MeshSlicer::Mesh MakeBox(const XMFLOAT3& c, const XMFLOAT3& h)
    {
        // n = outward normal, u x v = n
        struct Face { XMFLOAT3 n, u, v; };
        const Face faces[6] = {
            { XMFLOAT3( 1, 0, 0), XMFLOAT3(0, 1, 0), XMFLOAT3(0, 0, 1) },
            { XMFLOAT3(-1, 0, 0), XMFLOAT3(0, 0, 1), XMFLOAT3(0, 1, 0) },
            { XMFLOAT3( 0, 1, 0), XMFLOAT3(0, 0, 1), XMFLOAT3(1, 0, 0) },
            { XMFLOAT3( 0,-1, 0), XMFLOAT3(1, 0, 0), XMFLOAT3(0, 0, 1) },
            { XMFLOAT3( 0, 0, 1), XMFLOAT3(1, 0, 0), XMFLOAT3(0, 1, 0) },
            { XMFLOAT3( 0, 0,-1), XMFLOAT3(0, 1, 0), XMFLOAT3(1, 0, 0) },
        };
        auto scale = [&](const XMFLOAT3& a) { return XMFLOAT3(a.x * h.x, a.y * h.y, a.z * h.z); };
        MeshSlicer::Mesh m;
        for (const Face& f : faces)
        {
            XMFLOAT3 fc = scale(f.n), u = scale(f.u), v = scale(f.v);
            XMFLOAT3 mid(c.x + fc.x, c.y + fc.y, c.z + fc.z);
            const float su[4] = { -1, 1, 1, -1 }, sv[4] = { -1, -1, 1, 1 };
            uint32_t base = (uint32_t)m.verts.size();
            for (int k = 0; k < 4; ++k)
            {
                MeshSlicer::Vertex vx;
                vx.pos = XMFLOAT3(mid.x + u.x * su[k] + v.x * sv[k], mid.y + u.y * su[k] + v.y * sv[k], mid.z + u.z * su[k] + v.z * sv[k]);
                vx.normal = f.n;
                vx.uv = XMFLOAT2((su[k] + 1) * 0.5f, (sv[k] + 1) * 0.5f);
                m.verts.push_back(vx);
            }
            const uint32_t tri[6] = { 0, 1, 2, 0, 2, 3 };
            for (uint32_t t : tri) m.indices.push_back(base + t);
        }
        return m;
    }

    float PlaneDist(const XMFLOAT3& p, const XMFLOAT3& planePoint, const XMFLOAT3& n)
    {
        return (p.x - planePoint.x) * n.x + (p.y - planePoint.y) * n.y + (p.z - planePoint.z) * n.z;
    }
}

MeshSlicer::Mesh MakeBoxMesh(const XMFLOAT3& center, const XMFLOAT3& halfSize)
{
    return MakeBox(center, halfSize);
}

bool CutTarget::WorldBox(XMFLOAT3& mn, XMFLOAT3& mx) const
{
    if (m_standing.empty()) return false;
    XMFLOAT3 lmn, lmx;
    StandingBox(lmn, lmx);
    const XMMATRIX w = World();
    mn = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    mx = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        XMFLOAT3 c((i & 1) ? lmx.x : lmn.x, (i & 2) ? lmx.y : lmn.y, (i & 4) ? lmx.z : lmn.z);
        XMFLOAT3 p; XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&c), w));
        mn.x = fminf(mn.x, p.x); mn.y = fminf(mn.y, p.y); mn.z = fminf(mn.z, p.z);
        mx.x = fmaxf(mx.x, p.x); mx.y = fmaxf(mx.y, p.y); mx.z = fmaxf(mx.z, p.z);
    }
    return true;
}

namespace
{
    // --- the slit (where the blade goes through), found by looking along the line of sight ---
    const float SLIT_RANGE      = 1.5f;    // m: offsets along the swing searched on each side of the line of sight
    const int   SLIT_SAMPLES    = 96;      // rays over that range (about 3 cm apart) ...
    const int   SLIT_REFINE     = 8;       // ... then the entry / far side refined by bisection
    const int   AREA_STEPS      = 48;      // thickness samples along the cut (the cross-section's area)

    // A cut that has not got through opens at most this share of the area. Deeper, the straw left could not
    // hold the upper part up against gravity (2026-11-01 F5: a cut nearly through looked absurd).
    const float WOUND_DEEPEST   = 0.6f;
    // --- the wound's spring (the upper part sags into place and rocks a little) ---
    const float WOUND_STIFFNESS = 60.0f;   // 1/s^2
    const float WOUND_DAMPING   = 6.0f;    // 1/s (below critical = it overshoots once)

    XMFLOAT3 ToLocalPoint(const XMFLOAT3& w, FXMMATRIX inv) { XMFLOAT3 o; XMStoreFloat3(&o, XMVector3TransformCoord(XMLoadFloat3(&w), inv)); return o; }
    XMFLOAT3 ToLocalDir(const XMFLOAT3& w, FXMMATRIX inv)   { XMFLOAT3 o; XMStoreFloat3(&o, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&w), inv))); return o; }
    XMFLOAT3 Along(const XMFLOAT3& o, const XMFLOAT3& d, float t) { return XMFLOAT3(o.x + d.x * t, o.y + d.y * t, o.z + d.z * t); }
    float Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
}

template<class F> void CutTarget::ForIntact(F visit) const
{
    for (const Piece& p : m_standing) visit(p);
    if (m_wound.open)
        for (const Piece& p : m_debris.back().pieces) visit(p);
}

bool CutTarget::RayMesh(const XMFLOAT3& o, const XMFLOAT3& d, float& tBest, float* tFar) const
{
    // Moller-Trumbore ray / triangle test over every triangle; the nearest (and farthest) hit in front.
    const float PARALLEL_EPS = 1e-9f;
    bool any = false;
    tBest = FLT_MAX;
    if (tFar) *tFar = 0.0f;
    const XMVECTOR O = XMLoadFloat3(&o), D = XMLoadFloat3(&d);
    ForIntact([&](const Piece& p)
    {
        const std::vector<MeshSlicer::Vertex>& v = p.mesh.verts;
        const std::vector<uint32_t>& ix = p.mesh.indices;
        for (size_t i = 0; i + 2 < ix.size(); i += 3)
        {
            const XMVECTOR a = XMLoadFloat3(&v[ix[i]].pos), b = XMLoadFloat3(&v[ix[i + 1]].pos), c = XMLoadFloat3(&v[ix[i + 2]].pos);
            const XMVECTOR e1 = XMVectorSubtract(b, a), e2 = XMVectorSubtract(c, a);
            const XMVECTOR pv = XMVector3Cross(D, e2);
            const float det = XMVectorGetX(XMVector3Dot(e1, pv));
            if (fabsf(det) < PARALLEL_EPS) continue;
            const float inv = 1.0f / det;
            const XMVECTOR tv = XMVectorSubtract(O, a);
            const float u = XMVectorGetX(XMVector3Dot(tv, pv)) * inv;
            if (u < 0.0f || u > 1.0f) continue;
            const XMVECTOR qv = XMVector3Cross(tv, e1);
            const float w = XMVectorGetX(XMVector3Dot(D, qv)) * inv;
            if (w < 0.0f || u + w > 1.0f) continue;
            const float t = XMVectorGetX(XMVector3Dot(e2, qv)) * inv;
            if (t <= 0.0f) continue;
            if (t < tBest) { tBest = t; any = true; }
            if (tFar && t > *tFar) *tFar = t;
        }
    });
    return any;
}

CutTarget::StrikeResult CutTarget::Judge(const Aim& aim, float power, float needed) const
{
    StrikeResult r;
    // the plane, the swing and the line of sight in local space
    const XMMATRIX inv = XMMatrixInverse(nullptr, World());
    const XMFLOAT3 p = ToLocalPoint(aim.point, inv), n = ToLocalDir(aim.normal, inv);
    const XMFLOAT3 s = ToLocalDir(aim.swingDir, inv), eye = ToLocalPoint(aim.eye, inv), view = ToLocalDir(aim.viewDir, inv);

    // does the plane cross any part (a vertex on each side)?
    ForIntact([&](const Piece& piece)
    {
        bool pos = false, neg = false;
        for (const MeshSlicer::Vertex& v : piece.mesh.verts)
        {
            if (PlaneDist(v.pos, p, n) > 0.0f) pos = true; else neg = true;
            if (pos && neg) break;
        }
        if (pos && neg) r.hit = true;
    });
    if (!r.hit) return r;

    // the whole rule: through the middle of the torso = it counts; enough gathered = cut in two
    r.coreHit = fabsf(PlaneDist(m_coreCenter, p, n)) < coreZone;
    const float gathered = m_gathered + power;
    r.cut   = r.coreHit && gathered >= needed;
    r.depth = r.cut ? 1.0f : (r.coreHit ? gathered / needed * WOUND_DEEPEST : offBite);
    r.stuck = !r.cut;

    // the slit: look along the line of sight from points shifted along the swing; the run of hits
    // around the line of sight (or nearest to it) is where the blade goes through
    auto hitAt = [&](float u, float& t) { return RayMesh(Along(eye, s, u), view, t); };
    int best0 = -1, best1 = -1, run0 = -1;
    float bestDist = FLT_MAX;
    const float du = 2.0f * SLIT_RANGE / (SLIT_SAMPLES - 1);
    for (int i = 0; i <= SLIT_SAMPLES; ++i)
    {
        float t;
        const bool h = (i < SLIT_SAMPLES) && hitAt(-SLIT_RANGE + du * i, t);
        if (h && run0 < 0) run0 = i;
        if (!h && run0 >= 0)
        {
            const int run1 = i - 1;
            const float a = -SLIT_RANGE + du * run0, b = -SLIT_RANGE + du * run1;
            const float dist = (a <= 0.0f && b >= 0.0f) ? 0.0f : fminf(fabsf(a), fabsf(b));
            if (dist < bestDist) { bestDist = dist; best0 = run0; best1 = run1; }
            run0 = -1;
        }
    }
    if (best0 < 0) { r.hit = false; return r; }   // the plane crosses a part, but nothing is seen along the line of sight
    // refine an end between a hit ('in') and the miss next to it ('out')
    auto refine = [&](float in, float out)
    {
        for (int k = 0; k < SLIT_REFINE; ++k) { const float m = 0.5f * (in + out); float t; if (hitAt(m, t)) in = m; else out = m; }
        return in;
    };
    const float uIn  = refine(-SLIT_RANGE + du * best0, -SLIT_RANGE + du * (best0 - 1));
    const float uOut = refine(-SLIT_RANGE + du * best1, -SLIT_RANGE + du * (best1 + 1));

    // Where the blade stops: the cutting energy is spent on the AREA it opens (fracture energy grows with
    // the new surface), so the cross-section of the real mesh decides how far it gets. Rays along the line
    // of sight lie in the cut plane: their length through the dummy is the section's thickness at that
    // point; adding the thicknesses up along the cut (trapezoid rule) gives the area. The blade gets
    // slower through the thick parts and stops where the share of the area it can open is used up.
    float thick[AREA_STEPS + 1];
    float area = 0.0f;
    for (int i = 0; i <= AREA_STEPS; ++i)
    {
        float tNear, tFar;
        const bool h = RayMesh(Along(eye, s, uIn + (uOut - uIn) * i / AREA_STEPS), view, tNear, &tFar);
        thick[i] = h ? tFar - tNear : 0.0f;
        if (i > 0) area += 0.5f * (thick[i - 1] + thick[i]);
    }
    const float spend = area * r.depth;
    float opened = 0.0f;
    r.reach = 1.0f;
    for (int i = 0; i < AREA_STEPS; ++i)
    {
        const float slice = 0.5f * (thick[i] + thick[i + 1]);
        if (opened + slice >= spend)
        {
            r.reach = (i + (slice > 0.0f ? (spend - opened) / slice : 0.0f)) / AREA_STEPS;
            break;
        }
        opened += slice;
    }
    r.entryOffset = uIn;
    r.stopOffset  = uIn + (uOut - uIn) * r.reach;
    // the two points on the surface (world)
    const XMMATRIX world = World();
    auto surface = [&](float u)
    {
        float t = 0.0f;
        hitAt(u, t);
        const XMFLOAT3 l = Along(Along(eye, s, u), view, t);
        XMFLOAT3 w; XMStoreFloat3(&w, XMVector3TransformCoord(XMLoadFloat3(&l), world));
        return w;
    };
    r.entry = surface(r.entryOffset);
    r.stop  = surface(r.stopOffset);
    r.through = surface(uOut);
    r.depthAlongView = Dot(XMFLOAT3(r.stop.x - aim.eye.x, r.stop.y - aim.eye.y, r.stop.z - aim.eye.z), aim.viewDir);
    return r;
}

void CutTarget::OpenWound(const Aim& aim, const StrikeResult& r, float pushScale)
{
    const XMMATRIX inv = XMMatrixInverse(nullptr, World());
    m_wound.aim.point    = ToLocalPoint(aim.point, inv);
    m_wound.aim.normal   = ToLocalDir(aim.normal, inv);
    m_wound.aim.swingDir = ToLocalDir(aim.swingDir, inv);
    m_wound.aim.eye      = ToLocalPoint(aim.eye, inv);
    m_wound.aim.viewDir  = ToLocalDir(aim.viewDir, inv);
    m_wound.entry   = ToLocalPoint(r.entry, inv);
    m_wound.through = ToLocalPoint(r.through, inv);
    const XMFLOAT3 n = m_wound.aim.normal;
    const bool standFront = PlaneDist(FOOT_ANCHOR, m_wound.aim.point, n) > 0.0f;
    m_wound.side = standFront ? XMFLOAT3(-n.x, -n.y, -n.z) : n;
    if (!Cut(aim.point, aim.normal, aim.swingDir, pushScale) || m_debris.empty()) return;
    m_wound.open = true;
    m_wound.depth = m_wound.speed = 0.0f;
}

CutTarget::Aim CutTarget::WoundAim() const
{
    const XMMATRIX w = World();
    const auto point = [&](const XMFLOAT3& l) { XMFLOAT3 o; XMStoreFloat3(&o, XMVector3TransformCoord(XMLoadFloat3(&l), w)); return o; };
    const auto dir   = [&](const XMFLOAT3& l) { XMFLOAT3 o; XMStoreFloat3(&o, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&l), w))); return o; };
    return { point(m_wound.aim.point), dir(m_wound.aim.normal), dir(m_wound.aim.swingDir), point(m_wound.aim.eye), dir(m_wound.aim.viewDir) };
}

XMMATRIX CutTarget::WoundWorld() const
{
    // The turning point runs down the cut with its depth. The axis lies in the cut plane, across the
    // swing; turning about h x s by a positive angle lifts the entry side toward h, so the wedge opens.
    const XMVECTOR hinge = XMVectorLerp(XMLoadFloat3(&m_wound.entry), XMLoadFloat3(&m_wound.through), m_wound.depth);
    const XMVECTOR h = XMLoadFloat3(&m_wound.side), s = XMLoadFloat3(&m_wound.aim.swingDir);
    const XMVECTOR axis = XMVector3Normalize(XMVector3Cross(h, s));
    // Depth squared: the less straw is left, the less it resists; a shallow cut barely opens.
    return XMMatrixTranslationFromVector(XMVectorNegate(hinge)) * XMMatrixRotationAxis(axis, woundOpen * m_wound.depth * m_wound.depth)
         * XMMatrixTranslationFromVector(hinge) * World();
}

XMMATRIX CutTarget::DebrisWorld(const Debris& d) const
{
    if (m_wound.open && &d == &m_debris.back()) return WoundWorld();
    return XMMatrixTranslation(-d.com.x, -d.com.y, -d.com.z) * d.body.World();   // undo the center of mass, then the body's motion
}

void CutTarget::ReleaseWound()
{
    Debris& d = m_debris.back();
    const XMMATRIX w = WoundWorld();
    XMFLOAT3 comW;
    XMStoreFloat3(&comW, XMVector3TransformCoord(XMLoadFloat3(&d.com), w));
    XMFLOAT4 rot;
    XMStoreFloat4(&rot, XMQuaternionRotationMatrix(w));
    d.body.SetPose(comW, rot);   // it falls from where it hangs, at once: nothing holds it any more
    m_wound.open = false;
}

CutTarget::StrikeResult CutTarget::Strike(const Aim& aim, float power, float needed, float pushScale, float holdSevered)
{
    StrikeResult r = Judge(aim, power, needed);
    if (!r.hit || !r.coreHit) return r;   // off the middle: it counts for nothing and leaves nothing behind
    m_gathered += power;

    if (r.stuck)
    {
        // the blade bites in and stops: the wound opens down to the depth gathered so far
        if (!m_wound.open) OpenWound(aim, r, pushScale);
        m_wound.target = r.reach;
        return r;
    }

    // cut in two: the falling half hangs a moment before it slides off (the "beat")
    if (m_wound.open) ReleaseWound();
    else if (!Cut(aim.point, aim.normal, aim.swingDir, pushScale, holdSevered)) r.cut = false;
    return r;
}

void CutTarget::Upload(Piece& p)
{
    if (p.mesh.Empty()) { p.gpu.reset(); return; }
    MeshBuffer::Description desc = {};
    desc.pVtx     = p.mesh.verts.data();
    desc.vtxSize  = sizeof(MeshSlicer::Vertex);   // pos, normal, uv = the VS_Wall input
    desc.vtxCount = (UINT)p.mesh.verts.size();
    desc.isWrite  = false;
    desc.pIdx     = p.mesh.indices.data();
    desc.idxSize  = sizeof(uint32_t);
    desc.idxCount = (UINT)p.mesh.indices.size();
    desc.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    p.gpu = std::make_shared<MeshBuffer>(desc);
}

void CutTarget::Build(const PartDef* defs, int count, const std::vector<MeshSlicer::Mesh>* meshes)
{
    m_defs.assign(defs, defs + count);
    m_wound = Wound{};
    m_gathered = 0.0f;
    m_landedNow = false;
    m_tilt = m_tiltVel = XMFLOAT3(0, 0, 0);   // a fresh dummy stands still
    m_standing.clear();
    m_debris.clear();
    for (int i = 0; i < count; ++i)
    {
        Piece p;
        const bool fromModel = meshes && i < (int)meshes->size() && !(*meshes)[i].Empty();
        p.mesh = fromModel ? (*meshes)[i] : MakeBox(defs[i].center, defs[i].halfSize);
        p.part = i;
        Upload(p);
        m_standing.push_back(p);
    }
    // the center of the torso's bounding box: where a slash has to pass to count
    XMFLOAT3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const Piece& p : m_standing)
    {
        if (!m_defs[p.part].core) continue;
        for (const MeshSlicer::Vertex& v : p.mesh.verts)
        {
            mn.x = fminf(mn.x, v.pos.x); mn.y = fminf(mn.y, v.pos.y); mn.z = fminf(mn.z, v.pos.z);
            mx.x = fmaxf(mx.x, v.pos.x); mx.y = fmaxf(mx.y, v.pos.y); mx.z = fmaxf(mx.z, v.pos.z);
        }
    }
    m_coreCenter = XMFLOAT3((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
}

void CutTarget::SetPlacement(const XMFLOAT3& foot, float yaw)
{
    m_foot = foot;
    m_yaw  = yaw;
}

XMMATRIX CutTarget::World() const
{
    // turned to face its way, then tilted about the foot by the sway, then put in place
    const XMVECTOR tilt = XMLoadFloat3(&m_tilt);
    const float angle = XMVectorGetX(XMVector3Length(tilt));
    const float TINY = 1e-6f;
    const XMMATRIX sw = (angle > TINY) ? XMMatrixRotationAxis(XMVectorScale(tilt, 1.0f / angle), angle) : XMMatrixIdentity();
    return XMMatrixRotationY(m_yaw) * sw * XMMatrixTranslation(m_foot.x, m_foot.y, m_foot.z);
}

void CutTarget::Push(const XMFLOAT3& impulse, const XMFLOAT3& at)
{
    if (m_standing.empty()) return;
    // angular impulse about the foot = r x J; only the horizontal part tips it (twisting about the post is ignored)
    const XMVECTOR r = XMVectorSubtract(XMLoadFloat3(&at), XMLoadFloat3(&m_foot));
    XMFLOAT3 L; XMStoreFloat3(&L, XMVector3Cross(r, XMLoadFloat3(&impulse)));
    m_tiltVel.x += L.x / sway.inertia;
    m_tiltVel.z += L.z / sway.inertia;
}

RigidBody::PlaneCollider CutTarget::Ground() const
{
    return { m_foot, XMFLOAT3(0, 1, 0), 0.0f };
}

void CutTarget::StandingBox(XMFLOAT3& mn, XMFLOAT3& mx) const
{
    mn = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    mx = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    ForIntact([&](const Piece& p)
    {
        for (const MeshSlicer::Vertex& v : p.mesh.verts)
        {
            mn.x = fminf(mn.x, v.pos.x); mn.y = fminf(mn.y, v.pos.y); mn.z = fminf(mn.z, v.pos.z);
            mx.x = fmaxf(mx.x, v.pos.x); mx.y = fmaxf(mx.y, v.pos.y); mx.z = fmaxf(mx.z, v.pos.z);
        }
    });
}

bool CutTarget::RayHit(const XMFLOAT3& origin, const XMFLOAT3& dir, float& t) const
{
    if (m_standing.empty()) return false;
    XMFLOAT3 mn, mx;
    StandingBox(mn, mx);
    AimSystem::Hit h = AimSystem::Raycast(origin, dir, World(), mn, mx, 1);
    if (!h.valid) return false;
    t = h.t;
    return true;
}

bool CutTarget::Cut(const XMFLOAT3& pointW, const XMFLOAT3& normalW, const XMFLOAT3& swingDir, float pushScale, float holdSec)
{
    // the plane in the target's local space
    const XMMATRIX world = World();
    const XMMATRIX inv = XMMatrixInverse(nullptr, world);
    XMFLOAT3 p, n;
    XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&pointW), inv));
    XMStoreFloat3(&n, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&normalW), inv)));

    // the side holding the post's foot keeps standing
    const bool standFront = PlaneDist(FOOT_ANCHOR, p, n) > 0.0f;

    std::vector<Piece> standing, falling;
    bool anyCut = false;
    for (const Piece& piece : m_standing)
    {
        Piece front, back;
        front.part = back.part = piece.part;
        if (MeshSlicer::Slice(piece.mesh, p, n, front.mesh, back.mesh, m_defs[piece.part].capUv))
        {
            anyCut = true;
            Piece& stay = standFront ? front : back;
            Piece& fall = standFront ? back : front;
            if (!stay.mesh.Empty()) { Upload(stay); standing.push_back(stay); }
            if (!fall.mesh.Empty()) { Upload(fall); falling.push_back(fall); }
        }
        else
        {
            // not crossed: the whole part is on one side
            const bool onFront = PlaneDist(MeshSlicer::Centroid(piece.mesh), p, n) > 0.0f;
            (onFront == standFront ? standing : falling).push_back(piece);
        }
    }
    if (!anyCut) return false;
    m_standing = standing;

    // earlier fallen pieces lose the old cut face they were resting on (the stump changed)
    for (Debris& d : m_debris)
    {
        d.planes.assign(1, Ground());
        d.body.Wake();
    }
    if (falling.empty()) return true;

    // --- the stump's new cut face: a disc around the cap vertices (local), as a collider (world) ---
    XMFLOAT3 capC(0, 0, 0);
    int capN = 0;
    for (const Piece& s : m_standing)
        for (const MeshSlicer::Vertex& v : s.mesh.verts)
            if (fabsf(PlaneDist(v.pos, p, n)) < CAP_EPS) { capC.x += v.pos.x; capC.y += v.pos.y; capC.z += v.pos.z; ++capN; }
    float capR = 0.0f;
    if (capN > 0)
    {
        capC = XMFLOAT3(capC.x / capN, capC.y / capN, capC.z / capN);
        for (const Piece& s : m_standing)
            for (const MeshSlicer::Vertex& v : s.mesh.verts)
                if (fabsf(PlaneDist(v.pos, p, n)) < CAP_EPS)
                {
                    float dx = v.pos.x - capC.x, dy = v.pos.y - capC.y, dz = v.pos.z - capC.z;
                    capR = fmaxf(capR, sqrtf(dx * dx + dy * dy + dz * dz));
                }
    }
    // out of the stump = toward the falling side
    const XMFLOAT3 faceN = standFront ? XMFLOAT3(-n.x, -n.y, -n.z) : n;
    XMFLOAT3 faceNW, capCW;
    XMStoreFloat3(&faceNW, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&faceN), world)));
    XMStoreFloat3(&capCW, XMVector3TransformCoord(XMLoadFloat3(&capC), world));

    // --- the falling side = one rigid body ---
    Debris d;
    d.pieces = falling;
    XMFLOAT3 com(0, 0, 0);
    int count = 0;
    for (const Piece& f : falling)
        for (const MeshSlicer::Vertex& v : f.mesh.verts) { com.x += v.pos.x; com.y += v.pos.y; com.z += v.pos.z; ++count; }
    com = XMFLOAT3(com.x / count, com.y / count, com.z / count);
    d.com = com;

    std::vector<XMFLOAT3> pts;
    XMFLOAT3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const Piece& f : falling)
        for (const MeshSlicer::Vertex& v : f.mesh.verts)
        {
            XMFLOAT3 q(v.pos.x - com.x, v.pos.y - com.y, v.pos.z - com.z);
            pts.push_back(q);
            mn.x = fminf(mn.x, q.x); mn.y = fminf(mn.y, q.y); mn.z = fminf(mn.z, q.z);
            mx.x = fmaxf(mx.x, q.x); mx.y = fmaxf(mx.y, q.y); mx.z = fmaxf(mx.z, q.z);
        }
    const XMFLOAT3 half((mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f);
    const float mass = fmaxf(density * 8.0f * half.x * half.y * half.z, MIN_DEBRIS_MASS);

    XMFLOAT3 comW;
    XMStoreFloat3(&comW, XMVector3TransformCoord(XMLoadFloat3(&com), world));
    XMFLOAT4 rot;
    XMStoreFloat4(&rot, XMQuaternionRotationMatrix(world));   // the standing dummy's turn and its sway at this moment
    d.body.Init(pts, mass, half, comW, rot);

    // the blade's push, applied where the blade went through (so it also sets the piece turning).
    // Given when the piece starts to fall (after holdSec), not now.
    XMVECTOR swing = XMVector3Normalize(XMLoadFloat3(&swingDir));
    XMVECTOR vel = XMVectorAdd(XMVectorScale(swing, pushSpeed * pushScale), XMVectorScale(XMLoadFloat3(&faceNW), sepSpeed));
    XMStoreFloat3(&d.impulse, XMVectorScale(vel, mass));
    d.impulseAt = pointW;
    d.hold = holdSec;

    d.planes.push_back(Ground());
    if (capN > 0)
    {
        // the cut face's real outline (convex hull of the cap vertices in the plane), so the falling
        // half rests only on the stump itself and slides off its true edge
        RigidBody::PlaneCollider face{ capCW, faceNW, capR };
        XMFLOAT3 t1, t2;
        RigidBody::PlaneBasis(faceNW, t1, t2);
        std::vector<XMFLOAT2> flat;
        for (const Piece& s : m_standing)
            for (const MeshSlicer::Vertex& v : s.mesh.verts)
                if (fabsf(PlaneDist(v.pos, p, n)) < CAP_EPS)
                {
                    XMFLOAT3 w; XMStoreFloat3(&w, XMVector3TransformCoord(XMLoadFloat3(&v.pos), world));
                    const XMFLOAT3 rel(w.x - capCW.x, w.y - capCW.y, w.z - capCW.z);
                    flat.push_back(XMFLOAT2(Dot(rel, t1), Dot(rel, t2)));
                }
        face.outline = Collision2D::ConvexHull(flat);
        d.planes.push_back(face);
    }
    m_debris.push_back(d);
    return true;
}

void CutTarget::Update(float dt)
{
    // the sway: semi-implicit Euler (velocity first, then position with the new velocity) = stable for a spring
    const float ax = (-sway.stiffness * m_tilt.x - sway.damping * m_tiltVel.x) / sway.inertia;
    const float az = (-sway.stiffness * m_tilt.z - sway.damping * m_tiltVel.z) / sway.inertia;
    m_tiltVel.x += ax * dt;  m_tiltVel.z += az * dt;
    m_tilt.x += m_tiltVel.x * dt;  m_tilt.z += m_tiltVel.z * dt;
    const float lean = sqrtf(m_tilt.x * m_tilt.x + m_tilt.z * m_tilt.z);
    if (lean > sway.maxTilt)
    {
        const float k = sway.maxTilt / lean;
        m_tilt.x *= k;  m_tilt.z *= k;
        m_tiltVel = XMFLOAT3(0, 0, 0);   // it hit the stop
    }

    // the wound: the upper part sags toward the depth of the cut (same integration as the sway)
    if (m_wound.open)
    {
        m_wound.speed += (WOUND_STIFFNESS * (m_wound.target - m_wound.depth) - WOUND_DAMPING * m_wound.speed) * dt;
        m_wound.depth += m_wound.speed * dt;
    }

    for (Debris& d : m_debris)
    {
        if (m_wound.open && &d == &m_debris.back()) continue;   // still hanging in the wound
        if (d.hold > 0.0f) { d.hold -= dt; continue; }   // still hanging in place (the beat after a clean cut)
        if (d.impulse.x != 0.0f || d.impulse.y != 0.0f || d.impulse.z != 0.0f)
        {
            d.body.ApplyImpulse(d.impulse, d.impulseAt);   // now the blade's push sends it off
            d.impulse = XMFLOAT3(0, 0, 0);
        }
        d.body.Update(dt, d.planes, physics);
        if (!d.landed && d.body.LowestY() <= m_foot.y + LAND_SLOP)
        {
            d.landed = true;
            m_landedNow = true;
            m_landedAt = d.body.Position();
        }
    }
}

bool CutTarget::TakeLanded(XMFLOAT3& at)
{
    if (!m_landedNow) return false;
    m_landedNow = false;
    at = m_landedAt;
    return true;
}

void CutTarget::Draw(CameraBase* camera, float highlight)
{
    if (!camera) return;
    auto drawPiece = [&](const Piece& p, FXMMATRIX world)
    {
        if (!p.gpu) return;
        const PartDef& def = m_defs[p.part];
        std::shared_ptr<Texture> tex = TextureCache::Get(def.texture);
        LitRender::DrawMesh(p.gpu.get(), tex.get(), world, camera, PropMaterials::Find(def.materialKey), highlight);
    };
    const XMMATRIX world = World();
    for (const Piece& p : m_standing) drawPiece(p, world);
    for (const Debris& d : m_debris)
    {
        const XMMATRIX w = DebrisWorld(d);
        for (const Piece& p : d.pieces) drawPiece(p, w);
    }
}

void CutTarget::CollisionPoints(float footY, float stepY, float headY, std::vector<std::vector<XMFLOAT3>>& out) const
{
    // vertices of 'pieces' moved by 'world', kept if between the feet and the head
    auto gather = [&](const std::vector<Piece>& pieces, FXMMATRIX world, std::vector<XMFLOAT3>& pts, float& top)
    {
        for (const Piece& p : pieces)
            for (const MeshSlicer::Vertex& v : p.mesh.verts)
            {
                XMFLOAT3 w;
                XMStoreFloat3(&w, XMVector3TransformCoord(XMLoadFloat3(&v.pos), world));
                top = fmaxf(top, w.y);
                if (w.y >= footY && w.y <= headY) pts.push_back(w);
            }
    };
    // the standing dummy: one solid
    {
        std::vector<XMFLOAT3> pts;
        float top = -FLT_MAX;
        gather(m_standing, World(), pts, top);
        if (!pts.empty()) out.push_back(pts);
    }
    // each fallen piece (same transform as drawing: undo the center of mass, then the body)
    for (const Debris& d : m_debris)
    {
        std::vector<XMFLOAT3> pts;
        float top = -FLT_MAX;
        gather(d.pieces, DebrisWorld(d), pts, top);
        if (top >= stepY && !pts.empty()) out.push_back(pts);
    }
}

void CutTarget::DrawDebug(CameraBase* camera)
{
    if (!camera) return;
    XMFLOAT4X4 id; XMStoreFloat4x4(&id, XMMatrixIdentity());
    Geometory::SetWorld(id);
    Geometory::SetView(camera->GetView());
    Geometory::SetProjection(camera->GetProj());
    Geometory::SetColor(XMFLOAT4(1.0f, 0.4f, 0.1f, 1.0f));
    for (const Debris& d : m_debris)
        for (const RigidBody::PlaneCollider& pc : d.planes)
        {
            if (pc.radius <= 0.0f && pc.outline.empty()) continue;   // the ground is everywhere
            if (!pc.outline.empty())   // the real outline of the cut face
            {
                XMFLOAT3 t1, t2;
                RigidBody::PlaneBasis(pc.normal, t1, t2);
                auto at = [&](const XMFLOAT2& q) { return XMFLOAT3(pc.point.x + t1.x * q.x + t2.x * q.y, pc.point.y + t1.y * q.x + t2.y * q.y,
                                                                   pc.point.z + t1.z * q.x + t2.z * q.y); };
                for (size_t k = 0; k < pc.outline.size(); ++k)
                    Geometory::AddLine(at(pc.outline[k]), at(pc.outline[(k + 1) % pc.outline.size()]));
                continue;
            }
            XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&pc.normal));
            XMVECTOR helper = (fabsf(pc.normal.y) < 0.9f) ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
            XMVECTOR u = XMVector3Normalize(XMVector3Cross(helper, n));
            XMVECTOR v = XMVector3Cross(n, u);
            XMVECTOR c = XMLoadFloat3(&pc.point);
            for (int i = 0; i < DISC_SEGMENTS; ++i)
            {
                float a0 = XM_2PI * i / DISC_SEGMENTS, a1 = XM_2PI * (i + 1) / DISC_SEGMENTS;
                XMFLOAT3 p0, p1;
                XMStoreFloat3(&p0, XMVectorAdd(c, XMVectorScale(XMVectorAdd(XMVectorScale(u, cosf(a0)), XMVectorScale(v, sinf(a0))), pc.radius)));
                XMStoreFloat3(&p1, XMVectorAdd(c, XMVectorScale(XMVectorAdd(XMVectorScale(u, cosf(a1)), XMVectorScale(v, sinf(a1))), pc.radius)));
                Geometory::AddLine(p0, p1);
            }
        }
    // the middle of the torso (yellow circle facing the eye, radius coreZone): a slash counts when its
    // cut line passes through this circle
    Geometory::SetColor(XMFLOAT4(1.0f, 0.9f, 0.1f, 1.0f));
    {
        const XMVECTOR c = XMVector3TransformCoord(XMLoadFloat3(&m_coreCenter), World());
        const XMFLOAT3 eye = camera->GetPos();
        const XMVECTOR toEye = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&eye), c));
        const XMVECTOR u = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), toEye));
        const XMVECTOR v = XMVector3Cross(toEye, u);
        for (int i = 0; i < DISC_SEGMENTS; ++i)
        {
            const float a0 = XM_2PI * i / DISC_SEGMENTS, a1 = XM_2PI * (i + 1) / DISC_SEGMENTS;
            XMFLOAT3 p0, p1;
            XMStoreFloat3(&p0, XMVectorAdd(c, XMVectorScale(XMVectorAdd(XMVectorScale(u, cosf(a0)), XMVectorScale(v, sinf(a0))), coreZone)));
            XMStoreFloat3(&p1, XMVectorAdd(c, XMVectorScale(XMVectorAdd(XMVectorScale(u, cosf(a1)), XMVectorScale(v, sinf(a1))), coreZone)));
            Geometory::AddLine(p0, p1);
        }
    }
    // the wound: the line the cut runs along (its turning point moves down it with the depth)
    if (m_wound.open)
    {
        XMFLOAT3 a, b;
        XMStoreFloat3(&a, XMVector3TransformCoord(XMLoadFloat3(&m_wound.entry), World()));
        XMStoreFloat3(&b, XMVector3TransformCoord(XMLoadFloat3(&m_wound.through), World()));
        Geometory::AddLine(a, b);
    }
    // drawn without the depth test (X-ray): these sit inside the straw
    SetDepthTest(DEPTH_DISABLE);
    Geometory::DrawLines();
    SetDepthTest(DEPTH_ENABLE_WRITE_TEST);
}

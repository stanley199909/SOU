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

CutTarget::StrikeResult CutTarget::Strike(const XMFLOAT3& pointW, const XMFLOAT3& normalW, const XMFLOAT3& swingDir, float damage,
                                          float pushScale, float holdSevered)
{
    StrikeResult r;
    // the plane in local space (same as Cut)
    const XMMATRIX inv = XMMatrixInverse(nullptr, World());
    XMFLOAT3 p, n;
    XMStoreFloat3(&p, XMVector3TransformCoord(XMLoadFloat3(&pointW), inv));
    XMStoreFloat3(&n, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&normalW), inv)));

    // which parts does the plane cross (a vertex on each side)
    std::vector<bool> crossed(m_defs.size(), false);
    for (const Piece& piece : m_standing)
    {
        bool pos = false, neg = false;
        for (const MeshSlicer::Vertex& v : piece.mesh.verts)
        {
            if (PlaneDist(v.pos, p, n) > 0.0f) pos = true; else neg = true;
            if (pos && neg) { crossed[piece.part] = true; break; }
        }
    }

    // every crossed part takes the damage; the slash goes through only if all of them give
    float worstLeft = 0.0f;
    bool allGive = true;
    for (size_t i = 0; i < m_defs.size(); ++i)
    {
        if (!crossed[i]) continue;
        r.hit = true;
        if (m_defs[i].kind == PartKind::Armor) r.armorHit = true;
        m_damage[i] += damage;
        const float left = m_defs[i].durability - m_damage[i];
        if (left > 0.0f) allGive = false;
        if (r.stoppedBy < 0 || left > worstLeft) { worstLeft = left; r.stoppedBy = (int)i; }
    }
    if (!r.hit) return r;
    if (!allGive) return r;            // stopped by r.stoppedBy

    r.stoppedBy = -1;
    // How much of the torso stands before the cut (to tell "cut in two" from "a chip off it")
    const float coreBefore = StandingVolume(true);
    if (!Cut(pointW, normalW, swingDir, pushScale)) return r;
    r.cut = true;
    for (size_t i = 0; i < m_defs.size(); ++i)
    {
        if (!crossed[i]) continue;
        m_damage[i] = 0.0f;           // a fresh edge: the remaining part has to be worked again
        if (m_defs[i].core) r.coreCut = true;
    }
    if (r.coreCut && coreBefore > 0.0f)
    {
        // The smaller of the two sides of the torso, as a share of the torso that stood before this cut.
        //   Cut through the middle -> both sides are big. A slice off the side -> one side is tiny.
        //   (2026-10-21 F5: measured against the ORIGINAL torso, a torso first trimmed by a few chips
        //    could never be "cut in two" any more -- every later half was below the share.)
        const float coreAfter = StandingVolume(true);
        const float fell = coreBefore - coreAfter;
        r.coreSplit = fminf(coreAfter, fell) / coreBefore;
        r.severed = r.coreSplit >= severMinShare;
        // cut in two: the falling half hangs a moment before it slides off (the "beat")
        if (r.severed && !m_debris.empty()) m_debris.back().hold = holdSevered;
    }
    return r;
}

float CutTarget::StandingVolume(bool coreOnly) const
{
    float v = 0.0f;
    for (const Piece& p : m_standing)
        if (!coreOnly || m_defs[p.part].core) v += MeshSlicer::Volume(p.mesh);
    return v;
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
    m_damage.assign(count, 0.0f);
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
}

void CutTarget::SetPlacement(const XMFLOAT3& foot, float yaw)
{
    m_foot = foot;
    m_yaw  = yaw;
}

XMMATRIX CutTarget::World() const
{
    return XMMatrixRotationY(m_yaw) * XMMatrixTranslation(m_foot.x, m_foot.y, m_foot.z);
}

RigidBody::PlaneCollider CutTarget::Ground() const
{
    return { m_foot, XMFLOAT3(0, 1, 0), 0.0f };
}

void CutTarget::StandingBox(XMFLOAT3& mn, XMFLOAT3& mx) const
{
    mn = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    mx = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const Piece& p : m_standing)
        for (const MeshSlicer::Vertex& v : p.mesh.verts)
        {
            mn.x = fminf(mn.x, v.pos.x); mn.y = fminf(mn.y, v.pos.y); mn.z = fminf(mn.z, v.pos.z);
            mx.x = fmaxf(mx.x, v.pos.x); mx.y = fmaxf(mx.y, v.pos.y); mx.z = fmaxf(mx.z, v.pos.z);
        }
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
        if (MeshSlicer::Slice(piece.mesh, p, n, front.mesh, back.mesh))
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
    XMStoreFloat4(&rot, XMQuaternionRotationRollPitchYaw(0.0f, m_yaw, 0.0f));
    d.body.Init(pts, mass, half, comW, rot);

    // the blade's push, applied where the blade went through (so it also sets the piece turning).
    // Given when the piece starts to fall (after holdSec), not now.
    XMVECTOR swing = XMVector3Normalize(XMLoadFloat3(&swingDir));
    XMVECTOR vel = XMVectorAdd(XMVectorScale(swing, pushSpeed * pushScale), XMVectorScale(XMLoadFloat3(&faceNW), sepSpeed));
    XMStoreFloat3(&d.impulse, XMVectorScale(vel, mass));
    d.impulseAt = pointW;
    d.hold = holdSec;

    d.planes.push_back(Ground());
    if (capN > 0) d.planes.push_back({ capCW, faceNW, capR });
    m_debris.push_back(d);
    return true;
}

void CutTarget::Update(float dt)
{
    for (Debris& d : m_debris)
    {
        if (d.hold > 0.0f) { d.hold -= dt; continue; }   // still hanging in place (the beat after a clean cut)
        if (d.impulse.x != 0.0f || d.impulse.y != 0.0f || d.impulse.z != 0.0f)
        {
            d.body.ApplyImpulse(d.impulse, d.impulseAt);   // now the blade's push sends it off
            d.impulse = XMFLOAT3(0, 0, 0);
        }
        d.body.Update(dt, d.planes, physics);
    }
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
        // local -> (undo the center of mass) -> the body's motion
        const XMMATRIX w = XMMatrixTranslation(-d.com.x, -d.com.y, -d.com.z) * d.body.World();
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
        gather(d.pieces, XMMatrixTranslation(-d.com.x, -d.com.y, -d.com.z) * d.body.World(), pts, top);
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
            if (pc.radius <= 0.0f) continue;   // the ground is everywhere
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
    Geometory::DrawLines();
}

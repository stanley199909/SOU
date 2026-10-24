#pragma once
#include "Model.h"
#include <DirectXMath.h>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Props with a moving part (data). The FBX part (a node) is loaded as its own
// model, the prop body is loaded without it, and both are drawn with the same
// prop world matrix -- the part additionally spins about its own axle.
// Adding a new moving part = one more row here (+ drive its angle where needed).
// ---------------------------------------------------------------------------
struct PropPart
{
    const char* propKey;   // the prop body (drawn without the part)
    const char* partKey;   // shared-object key of the part model
    const char* nodeName;  // FBX node of the part
};

static const PropPart PROP_PARTS[] = {
    { "StGrind", "StGrindWheel", "Stone_low" },   // grindstone wheel (pedal driven)
};

inline const PropPart* FindPropPart(const char* propKey)
{
    for (const PropPart& p : PROP_PARTS)
        if (std::strcmp(p.propKey, propKey) == 0) return &p;
    return nullptr;
}

// Rotation of a part about its own axle, in the part's model space.
// Seen along its axle a wheel is a CIRCLE, so the two box sides perpendicular to
// the axle are equal (= the diameter). Pick the axis whose other two sides match
// best. (Not "the thinnest side": if the shaft runs through the wheel, the axle
// direction is the LONGEST side, and "thinnest" picks a diameter -> wrong spin.)
// Result is applied before the prop world matrix:
//   partWorld = PartSpin(part, angle) * PropWorld(prop)
inline DirectX::XMMATRIX PartSpin(Model* part, float angle)
{
    using namespace DirectX;
    XMFLOAT3 mn, mx;
    part->GetLocalAABB(mn, mx);
    const float ext[3] = { mx.x - mn.x, mx.y - mn.y, mx.z - mn.z };
    int axle = 0;
    float bestDiff = 1e30f;
    for (int a = 0; a < 3; ++a)
    {
        const float diff = fabsf(ext[(a + 1) % 3] - ext[(a + 2) % 3]);   // the two sides across axis a
        if (diff < bestDiff) { bestDiff = diff; axle = a; }
    }
    const XMVECTOR axis = XMVectorSet(axle == 0 ? 1.0f : 0.0f, axle == 1 ? 1.0f : 0.0f, axle == 2 ? 1.0f : 0.0f, 0.0f);
    const XMFLOAT3 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
    return XMMatrixTranslation(-c.x, -c.y, -c.z) * XMMatrixRotationAxis(axis, angle) * XMMatrixTranslation(c.x, c.y, c.z);
}

// Grow a prop's bounding box to include its part, so placement/scale/ground snap
// stay exactly as when the prop was one piece (saved layouts keep working).
inline void UnionPartAABB(Model* part, DirectX::XMFLOAT3& mn, DirectX::XMFLOAT3& mx)
{
    if (!part) return;
    DirectX::XMFLOAT3 pmn, pmx;
    part->GetLocalAABB(pmn, pmx);
    if (pmn.x < mn.x) mn.x = pmn.x; if (pmn.y < mn.y) mn.y = pmn.y; if (pmn.z < mn.z) mn.z = pmn.z;
    if (pmx.x > mx.x) mx.x = pmx.x; if (pmx.y > mx.y) mx.y = pmx.y; if (pmx.z > mx.z) mx.z = pmx.z;
}

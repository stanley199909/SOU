#pragma once
#include "Model.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The cottage's back door (data + hinge geometry).
// The door leaf is split off the cottage FBX at load time (SceneRoot), exactly like
// the grindstone wheel in PropParts.h: the house is loaded WITHOUT these nodes, the
// door WITH ONLY these nodes. Both share one coordinate system, so drawing the door
// with (hinge * houseWorld) puts it back into the doorway when the angle is 0.
// The frame (ASM_Back_Door_Frame) is NOT listed: it stays in the wall.
// ---------------------------------------------------------------------------
namespace CottageDoor
{
    constexpr const char* HOUSE_KEY = "StCottage";
    constexpr const char* DOOR_KEY  = "StCottageDoor";

    // Everything that swings with the door: planks, iron straps (outside) and the
    // thin backing board (inside, blocks light between the planks).
    inline std::vector<std::string> NodeNames()
    {
        return { "ASM_Back_Door_Leaf_Planked", "ASM_Back_Door_Ironwork", "ASM_Back_Door_Solid_Backing" };
    }

    // Which vertical edge carries the hinges, seen along the door's width axis:
    // true = the edge with the smaller coordinate. (Data: flip it to hang the door the other way.)
    constexpr bool HINGE_ON_MIN_SIDE = true;

    // Where the hinge axis is and which way is "into the room". Computed once from
    // the models' bounding boxes (no hand-typed coordinates).
    struct Hinge
    {
        DirectX::XMFLOAT3 pivot = { 0, 0, 0 };  // a point on the vertical hinge axis (house model space)
        float sign = 1.0f;                      // +1/-1: rotation direction that swings the door INTO the room
        bool  valid = false;
    };

    // The door is a thin slab. Its thinnest horizontal axis is the wall normal; the
    // other horizontal axis is its width. The room is on the side of the house's center.
    // Hinge = the inner face (room side) at one end of the width.
    // The swing direction is found by TRYING +angle and checking whether the free edge
    // moved into the room -- simpler and safer than deriving the sign by hand.
    inline Hinge ComputeHinge(Model* door, Model* house)
    {
        using namespace DirectX;
        Hinge h;
        if (!door || !house) return h;
        XMFLOAT3 dmn, dmx, hmn, hmx;
        door->GetLocalAABB(dmn, dmx);
        house->GetLocalAABB(hmn, hmx);
        if (dmn.x > dmx.x) return h;                        // no door geometry

        const bool thinX = (dmx.x - dmn.x) < (dmx.z - dmn.z);   // wall normal = X ? else Z
        const float doorT  = thinX ? (dmn.x + dmx.x) * 0.5f : (dmn.z + dmx.z) * 0.5f;
        const float houseT = thinX ? (hmn.x + hmx.x) * 0.5f : (hmn.z + hmx.z) * 0.5f;
        const float inward = (houseT > doorT) ? 1.0f : -1.0f;  // room side along the wall normal

        const float tFace  = (inward > 0.0f) ? (thinX ? dmx.x : dmx.z) : (thinX ? dmn.x : dmn.z);
        const float wHinge = HINGE_ON_MIN_SIDE ? (thinX ? dmn.z : dmn.x) : (thinX ? dmx.z : dmx.x);
        const float wFree  = HINGE_ON_MIN_SIDE ? (thinX ? dmx.z : dmx.x) : (thinX ? dmn.z : dmn.x);
        h.pivot = thinX ? XMFLOAT3(tFace, 0.0f, wHinge) : XMFLOAT3(wHinge, 0.0f, tFace);
        const XMFLOAT3 freeEdge = thinX ? XMFLOAT3(tFace, 0.0f, wFree) : XMFLOAT3(wFree, 0.0f, tFace);

        // Try a quarter turn in the + direction: did the free edge go into the room?
        const XMVECTOR p = XMLoadFloat3(&h.pivot);
        const XMVECTOR moved = XMVector3Transform(XMLoadFloat3(&freeEdge) - p, XMMatrixRotationY(XM_PIDIV2)) + p;
        const float movedT = thinX ? XMVectorGetX(moved) : XMVectorGetZ(moved);
        h.sign  = ((movedT - tFace) * inward > 0.0f) ? 1.0f : -1.0f;
        h.valid = true;
        return h;
    }

    // Rotation about the hinge axis, in house model space. Apply before the house world:
    //   doorWorld = HingeMatrix(h, angle) * houseWorld
    inline DirectX::XMMATRIX HingeMatrix(const Hinge& h, float angle)
    {
        using namespace DirectX;
        if (!h.valid) return XMMatrixIdentity();
        return XMMatrixTranslation(-h.pivot.x, 0.0f, -h.pivot.z) *
               XMMatrixRotationY(angle * h.sign) *
               XMMatrixTranslation(h.pivot.x, 0.0f, h.pivot.z);
    }
}

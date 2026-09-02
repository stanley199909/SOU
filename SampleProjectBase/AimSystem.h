#ifndef __AIM_SYSTEM_H__
#define __AIM_SYSTEM_H__

#include <DirectXMath.h>

// ============================================================================
//  AimSystem : hitscan aiming, the way FPS games (Valorant / CS) do it.
//
//  A ray is cast from the camera along its forward direction (which passes
//  through the screen-center crosshair) and tested against the weapon's
//  geometry, split into "nseg" oriented boxes along its long axis.
//  The nearest box the ray ENTERS is the aimed segment; if the ray misses
//  every box the shot is a whiff (valid == false).
//
//  Why this is correct: the crosshair pixel unprojects to exactly this ray,
//  so "the crosshair is on the blade" and "the ray hits the blade" are the
//  same event. No fake ground plane, no screen-space projection guesswork.
//
//  The segment numbering (SegOfLocal) is defined purely in the weapon's LOCAL
//  space, so the aimed segment, the highlighted segment and the morph segment
//  all use one identical rule and can never disagree.
// ============================================================================
namespace AimSystem
{
	struct Hit
	{
		bool              valid;	// true if the ray entered any segment box
		int               seg;		// aimed segment index in [0, nseg)
		float             t;		// ray parameter at entry (world units)
		DirectX::XMFLOAT3 world;	// world-space entry point (used to place the hammer)
	};

	// Longest local axis of the weapon AABB (0=x, 1=y, 2=z). Segments cut along it.
	int LongAxis(const DirectX::XMFLOAT3& lmin, const DirectX::XMFLOAT3& lmax);

	// Segment index [0, nseg) for a local-space position. The mesh morph calls the
	// same function per vertex, so aim / highlight / progress stay in lockstep.
	int SegOfLocal(const DirectX::XMFLOAT3& lpos,
	               const DirectX::XMFLOAT3& lmin, const DirectX::XMFLOAT3& lmax, int nseg);

	// Continuous segment position [0, nseg] for a local-space position (for smooth
	// blending across segment borders in the morph). Same axis as SegOfLocal.
	float SegCoordLocal(const DirectX::XMFLOAT3& lpos,
	                    const DirectX::XMFLOAT3& lmin, const DirectX::XMFLOAT3& lmax, int nseg);

	// Ray (world space) vs the weapon's per-segment boxes.
	//   camPos/camFwd : ray origin and (unit) direction through the crosshair.
	//   world         : matrix mapping weapon LOCAL space to world space.
	//   lmin/lmax     : weapon local AABB (of the rest pose / stage_0).
	Hit Raycast(const DirectX::XMFLOAT3& camPos, const DirectX::XMFLOAT3& camFwd,
	            const DirectX::XMMATRIX& world,
	            const DirectX::XMFLOAT3& lmin, const DirectX::XMFLOAT3& lmax, int nseg);
}

#endif // __AIM_SYSTEM_H__

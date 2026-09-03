#ifndef __LERP_H__
#define __LERP_H__

#include <DirectXMath.h>

// ============================================================================
//  Lerp : small, reusable smoothing / interpolation helpers.
//
//  Many systems (hammer follow, camera sway, gauges, fade-in feedback) want to
//  move a value TOWARD a target smoothly instead of snapping. The naive form
//    a = lerp(a, target, k);
//  is convenient but frame-rate DEPENDENT: at 30fps it lands somewhere else
//  than at 144fps, because k is applied once per frame regardless of dt.
//
//  Damp() fixes that. It is exponential smoothing done right:
//    a = lerp(a, target, 1 - exp(-lambda * dt));
//  which is identical no matter how dt is chopped up. "lambda" is a rate in
//  1/seconds: bigger = snappier. Rough intuition: the value covers ~63% of the
//  remaining distance every (1 / lambda) seconds.
//
//  MoveTowards() is the linear cousin: constant speed, and it actually REACHES
//  the target (Damp only approaches it asymptotically). Good for things that
//  should arrive exactly, like an animation returning to a rest pose.
// ============================================================================
namespace Lerp
{
	//--- plain linear interpolation ---------------------------------------------
	float             Linear(float a, float b, float t);
	DirectX::XMFLOAT3 Linear(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t);

	//--- frame-rate independent exponential smoothing ---------------------------
	//  Returns the new value; call every frame:  x = Lerp::Damp(x, target, k, dt);
	float             Damp(float a, float b, float lambda, float dt);
	DirectX::XMFLOAT3 Damp(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float lambda, float dt);

	//--- constant-speed approach that exactly reaches the target ----------------
	//  maxDelta = speed * dt (max distance moved this frame).
	float             MoveTowards(float a, float b, float maxDelta);
	DirectX::XMFLOAT3 MoveTowards(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float maxDelta);

	//--- easing (t is clamped to [0,1]) -----------------------------------------
	float SmoothStep(float t);	// 3t^2 - 2t^3, zero slope at both ends
	float EaseOut(float t);		// quadratic, fast then settling
	float EaseIn(float t);		// quadratic, slow start
}

#endif // __LERP_H__

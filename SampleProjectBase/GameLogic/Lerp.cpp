#include "Lerp.h"
#include <cmath>

using namespace DirectX;

namespace Lerp
{
	float Linear(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	XMFLOAT3 Linear(const XMFLOAT3& a, const XMFLOAT3& b, float t)
	{
		return XMFLOAT3(a.x + (b.x - a.x) * t,
		                a.y + (b.y - a.y) * t,
		                a.z + (b.z - a.z) * t);
	}

	float Damp(float a, float b, float lambda, float dt)
	{
		float t = 1.0f - expf(-lambda * dt);
		return a + (b - a) * t;
	}

	XMFLOAT3 Damp(const XMFLOAT3& a, const XMFLOAT3& b, float lambda, float dt)
	{
		float t = 1.0f - expf(-lambda * dt);
		return Linear(a, b, t);
	}

	float MoveTowards(float a, float b, float maxDelta)
	{
		float d = b - a;
		if (d >  maxDelta) return a + maxDelta;
		if (d < -maxDelta) return a - maxDelta;
		return b;
	}

	XMFLOAT3 MoveTowards(const XMFLOAT3& a, const XMFLOAT3& b, float maxDelta)
	{
		float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
		float len = sqrtf(dx * dx + dy * dy + dz * dz);
		if (len <= maxDelta || len < 1e-6f) return b;
		float s = maxDelta / len;
		return XMFLOAT3(a.x + dx * s, a.y + dy * s, a.z + dz * s);
	}

	float SmoothStep(float t)
	{
		if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
		return t * t * (3.0f - 2.0f * t);
	}

	float EaseOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
	float EaseIn(float t)  { return t * t; }
}

#include "AimSystem.h"
#include <cmath>

using namespace DirectX;

namespace
{
	// Read one component of a float3 by axis index.
	inline float AxisOf(const XMFLOAT3& v, int a)
	{
		return (a == 0) ? v.x : (a == 1) ? v.y : v.z;
	}
}

namespace AimSystem
{
	int LongAxis(const XMFLOAT3& lmin, const XMFLOAT3& lmax)
	{
		float ex = lmax.x - lmin.x;
		float ey = lmax.y - lmin.y;
		float ez = lmax.z - lmin.z;
		if (ex >= ey && ex >= ez) return 0;
		return (ey >= ez) ? 1 : 2;
	}

	float SegCoordLocal(const XMFLOAT3& lpos, const XMFLOAT3& lmin, const XMFLOAT3& lmax, int nseg)
	{
		int   a  = LongAxis(lmin, lmax);
		float mn = AxisOf(lmin, a);
		float mx = AxisOf(lmax, a);
		float ext = mx - mn; if (ext < 1e-6f) ext = 1.0f;
		float f = (AxisOf(lpos, a) - mn) / ext;	// 0..1 along the blade
		if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
		return f * nseg;						// 0..nseg
	}

	int SegOfLocal(const XMFLOAT3& lpos, const XMFLOAT3& lmin, const XMFLOAT3& lmax, int nseg)
	{
		int s = (int)SegCoordLocal(lpos, lmin, lmax, nseg);
		if (s < 0) s = 0; if (s >= nseg) s = nseg - 1;
		return s;
	}

	Hit Raycast(const XMFLOAT3& camPos, const XMFLOAT3& camFwd,
	            const XMMATRIX& world,
	            const XMFLOAT3& lmin, const XMFLOAT3& lmax, int nseg)
	{
		Hit out; out.valid = false; out.seg = 0; out.t = 0.0f; out.world = XMFLOAT3(0, 0, 0);
		if (nseg <= 0) return out;

		// Bring the ray into weapon LOCAL space. The direction is transformed as a
		// vector but NOT re-normalized, so the ray parameter t stays identical to
		// world space (affine maps preserve the parameter along a ray).
		XMVECTOR det;
		XMMATRIX inv = XMMatrixInverse(&det, world);
		XMVECTOR o = XMVector3TransformCoord (XMLoadFloat3(&camPos), inv);
		XMVECTOR d = XMVector3TransformNormal(XMLoadFloat3(&camFwd), inv);

		XMFLOAT3 ol, dl; XMStoreFloat3(&ol, o); XMStoreFloat3(&dl, d);

		int   a    = LongAxis(lmin, lmax);
		float mn   = AxisOf(lmin, a);
		float mx   = AxisOf(lmax, a);
		float step = (mx - mn) / nseg;

		const float FAR = 3.4e38f;
		float best = FAR; int bestSeg = -1;

		for (int s = 0; s < nseg; ++s)
		{
			// This segment's box: full extent on the two short axes, one slab on the long axis.
			XMFLOAT3 bmin = lmin, bmax = lmax;
			float a0 = mn + step * s;
			float a1 = mn + step * (s + 1);
			if      (a == 0) { bmin.x = a0; bmax.x = a1; }
			else if (a == 1) { bmin.y = a0; bmax.y = a1; }
			else             { bmin.z = a0; bmax.z = a1; }

			// Standard slab test: ray vs axis-aligned box (in local space).
			float tenter = -FAR, texit = FAR; bool ok = true;
			for (int ax = 0; ax < 3; ++ax)
			{
				float oo = AxisOf(ol, ax);
				float dd = AxisOf(dl, ax);
				float lo = AxisOf(bmin, ax);
				float hi = AxisOf(bmax, ax);
				if (fabsf(dd) < 1e-8f)
				{
					if (oo < lo || oo > hi) { ok = false; break; }	// parallel & outside slab
				}
				else
				{
					float t1 = (lo - oo) / dd;
					float t2 = (hi - oo) / dd;
					if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
					if (t1 > tenter) tenter = t1;
					if (t2 < texit)  texit  = t2;
					if (tenter > texit) { ok = false; break; }
				}
			}
			if (!ok) continue;
			if (texit < 0.0f) continue;						// box entirely behind the camera
			float th = (tenter >= 0.0f) ? tenter : 0.0f;	// entry point (0 if origin is inside)
			if (th < best) { best = th; bestSeg = s; }		// keep the FIRST box the ray meets
		}

		if (bestSeg >= 0)
		{
			out.valid = true;
			out.seg   = bestSeg;
			out.t     = best;
			XMVECTOR wp = XMVectorAdd(XMLoadFloat3(&camPos), XMVectorScale(XMLoadFloat3(&camFwd), best));
			XMStoreFloat3(&out.world, wp);
		}
		return out;
	}
}

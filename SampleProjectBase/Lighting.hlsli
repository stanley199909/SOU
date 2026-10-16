// Lighting.hlsli -- the ONE lighting model of the scene (2026-10-15).
// Every lit surface (cottage walls, props, held tools, ...) includes this file, so all of
// them answer "how does light reflect here?" with the same formula. Before this, walls used
// GGX, props used Lambert + Blinn-Phong and the held tools were unlit -> the reviewer could
// see three different looks side by side.
// Values are LINEAR (PostProcess applies ACES + output encoding once at the end).
// ASCII comments only so this file compiles regardless of code page.

static const float PI             = 3.14159265;
static const float EPSILON        = 0.0001;
static const float MIN_ROUGHNESS  = 0.08;	// below this the GGX highlight becomes a sub-pixel spike (aliasing)
static const float DIELECTRIC_F0  = 0.04;	// reflectance at normal incidence of non-metals (~4%, wood/stone/plastic)
static const float3 FIRE_COLOR    = float3(1.0, 0.30, 0.065);	// forge light color (linear)
static const float FIRE_SOFTENING = 1.0;	// added to distance^2 so the light does not blow up right next to the fire

// Base reflectance F0: non-metals reflect ~4% (grey), metals reflect their own color.
float3 SurfaceF0(float3 base, float metal)
{
    return lerp(DIELECTRIC_F0.xxx, base, metal);
}

// Cook-Torrance microfacet BRDF times N.L, for one light direction l.
//   D = GGX normal distribution (how many micro-facets face the half vector -> size/shape of the highlight)
//   G = Smith-Schlick geometry  (micro-facets shadowing/masking each other at grazing angles)
//   F = Schlick Fresnel         (more reflection at grazing angles)
//   diffuse = Lambert, scaled by (1-F) (energy not reflected) and (1-metal) (metals have no diffuse)
float3 BRDF(float3 base, float rough, float metal, float3 n, float3 v, float3 l)
{
    float nl = saturate(dot(n, l)), nv = max(saturate(dot(n, v)), EPSILON);
    float3 h = normalize(v + l);
    float nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    float a = rough * rough, a2 = a * a;
    float d = a2 / max(PI * pow(nh * nh * (a2 - 1) + 1, 2), EPSILON);
    float k = (rough + 1) * (rough + 1) / 8;
    float g = (nv / (nv * (1 - k) + k)) * (nl / max(nl * (1 - k) + k, EPSILON));
    float3 f0 = SurfaceF0(base, metal);
    float3 f = f0 + (1 - f0) * pow(1 - vh, 5);
    return ((1 - f) * (1 - metal) * base / PI + f * d * g / max(4 * nv * nl, EPSILON)) * nl;
}

// Light from the whole surroundings (sky through the window, bounce light) until a real
// environment map (IBL) is added. Two different "ambient" inputs on purpose:
//   diffuse  = base * diffuseAmbient * (1-metal)       diffuseAmbient  = room color x indoor FILL boost
//   specular = F * specularAmbient, ONLY for metals     specularAmbient = room color WITHOUT the boost
// The FILL boost (x4, F1 "Indoor fill strength") is an artist lift so dark corners stay readable --
// it is not how bright the room really is. Reflecting it (a mirror shows the real surroundings):
//   - on every surface turned the interior milky white, worst at grazing angles (2026-10-15 F5)
//   - on metals made the blade and the anvil top a bluish white at any temperature (2026-10-16 F5)
// Metals still need some reflection: they have no diffuse, so without it they turn pitch black in
// the shade (the classic "PBR metal looks like black plastic" pitfall).
// The proper fix is IBL: reflect what the room actually looks like (dark room -> dark reflection).
float3 AmbientLight(float3 base, float rough, float metal, float3 n, float3 v, float3 diffuseAmbient, float3 specularAmbient)
{
    float nv = saturate(dot(n, v));
    float3 f0 = SurfaceF0(base, metal);
    float3 f = f0 + (max((1 - rough).xxx, f0) - f0) * pow(1 - nv, 5);	// roughness-aware Schlick Fresnel (Lagarde)
    return (1 - metal) * base * diffuseAmbient + f * metal * specularAmbient;
}

// The forge fire as a point light at firePos with the given intensity (inverse-square falloff,
// softened near the source).
float3 FireLight(float3 base, float rough, float metal, float3 n, float3 v, float3 worldPos,
                 float3 firePos, float intensity)
{
    float3 toFire = firePos - worldPos;
    float dist2 = max(dot(toFire, toFire), EPSILON);
    return BRDF(base, rough, metal, n, v, toFire * rsqrt(dist2)) * FIRE_COLOR * intensity / (FIRE_SOFTENING + dist2);
}

// Normal map without a tangent stream: build the tangent frame from screen-space derivatives
// of the position and UV ("cotangent frame", Schueler 2013). mapN = sampled normal * 2 - 1 in
// OpenGL convention (+green = +V); flip .y before calling for DirectX-convention maps.
float3 PerturbNormal(float3 n, float3 position, float2 uv, float3 mapN)
{
    float3 dp1 = ddx(position), dp2 = ddy(position);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 p2 = cross(dp2, n), p1 = cross(n, dp1);
    float3 t = p2 * duv1.x + p1 * duv2.x;
    float3 b = p2 * duv1.y + p1 * duv2.y;
    // t and b are TINY (pixel size in metres x UV change per pixel, squared ~1e-12), so the guard
    // must be far smaller than EPSILON (1e-4): with EPSILON the guard always won, t/b shrank to ~0
    // and the normal map had no visible effect (2026-10-16, measured with Tools/pbr_material_probe).
    const float TANGENT_LEN2_MIN = 1e-30;	// only stops a true divide-by-zero
    float invScale = rsqrt(max(max(dot(t, t), dot(b, b)), TANGENT_LEN2_MIN));
    float3 detail = t * invScale * mapN.x + b * invScale * mapN.y + n * mapN.z;
    return dot(detail, detail) > EPSILON ? normalize(detail) : n;
}

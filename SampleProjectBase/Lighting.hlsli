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
static const float3 LUMA          = float3(0.2126, 0.7152, 0.0722);	// Rec.709 luminance weights (brightness of a color)
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

// --- IBL (image based lighting, 2026-10-16) ---
// The reflection probe (ReflectionProbe.cpp) photographs the room once and bakes:
//   iblEnv = the photo blurred per roughness (mip 0 = mirror ... last mip = fully rough)
//   iblLut = the BRDF half of the split-sum: (A, B) for every (n.v, roughness)
// Not bound (editor scene, before the first capture, or F1 "Use IBL" off) -> GetDimensions
// returns 0 and AmbientLight falls back to the old flat room color below.
TextureCube  iblEnv  : register(t6);
Texture2D    iblLut  : register(t7);
SamplerState iblSamp : register(s1);   // trilinear, clamp, no LOD bias (ReflectionProbe's own sampler)
// IBL data texture (ReflectionProbe::BindForLighting), 12 texels:
//   0 = probe position (w = 1: box projection on)
//   1 = room box min (w = diffuse IBL strength, 0 = off), 2 = room box max (w = 1: reflections on)
//   3..11 = irradiance SH coefficients L00, L1-1, L10, L11, L2-2, L2-1, L20, L21, L22 (rgb)
// A small texture instead of a cbuffer: every lit shader has its own cbuffer layout at b0, and the
// engine's Shader class binds cbuffers by declaration order, so a shared extra cbuffer would land
// on the wrong slot. Unbound -> Load returns 0 -> w = 0 -> no correction.
Texture2D<float4> iblBox : register(t8);

// Box projection (parallax-corrected cubemap, Lagarde 2012): the photo was taken from ONE point,
// so looking the reflection direction up as-is shows what the PROBE sees in that direction, not
// what this pixel sees. Treat the room as a box: walk from this pixel along r to the box wall it
// hits, then look up the direction from the probe to that hit point. Same ray-vs-box "slab" math
// as AimSystem::Raycast. Search: "box projected cubemap", "parallax corrected cubemap".
float3 BoxProjectedDirection(float3 wp, float3 r)
{
    float4 probe = iblBox.Load(int3(0, 0, 0));
    if (probe.w < 0.5) return r;
    float3 bmin = iblBox.Load(int3(1, 0, 0)).xyz;
    float3 bmax = iblBox.Load(int3(2, 0, 0)).xyz;
    if (any(wp < bmin) || any(wp > bmax)) return r;    // outside the room (yard, roof outside): no correction
    float3 tFar = max((bmax - wp) / r, (bmin - wp) / r);   // distance to the wall ahead on each axis
    float  t = min(min(tFar.x, tFar.y), tFar.z);           // the nearest of them is the wall actually hit
    return wp + r * t - probe.xyz;
}

// Diffuse IBL: the light arriving at a surface from its whole hemisphere of the room (irradiance),
// rebuilt from 9 spherical-harmonics numbers (Ramamoorthi & Hanrahan 2001, the standard constants).
// Light that enters wood/stone scatters inside and leaves in every direction tinted by the material
// (diffuse), so what matters is the SUM of the light from everywhere it faces: a face toward the forge
// gets orange, a face toward a dark corner stays dark. Off/unbound/outside the room -> the flat fill.
// Returns light such that "base * result" = the outgoing diffuse light (the 1/pi of Lambert included).
float3 IblDiffuseOr(float3 n, float3 wp, float3 flatFill)
{
    float strength = iblBox.Load(int3(1, 0, 0)).w;
    if (strength <= 0) return flatFill;
    float3 bmin = iblBox.Load(int3(1, 0, 0)).xyz, bmax = iblBox.Load(int3(2, 0, 0)).xyz;
    if (any(wp < bmin) || any(wp > bmax)) return flatFill;   // outside the room (yard): the photo is of the INSIDE
    const float C1 = 0.429043, C2 = 0.511664, C3 = 0.743125, C4 = 0.886227, C5 = 0.247708;
    float3 L00  = iblBox.Load(int3(3, 0, 0)).rgb,  L1m1 = iblBox.Load(int3(4, 0, 0)).rgb, L10 = iblBox.Load(int3(5, 0, 0)).rgb;
    float3 L11  = iblBox.Load(int3(6, 0, 0)).rgb,  L2m2 = iblBox.Load(int3(7, 0, 0)).rgb, L2m1 = iblBox.Load(int3(8, 0, 0)).rgb;
    float3 L20  = iblBox.Load(int3(9, 0, 0)).rgb,  L21  = iblBox.Load(int3(10, 0, 0)).rgb, L22 = iblBox.Load(int3(11, 0, 0)).rgb;
    float x = n.x, y = n.y, z = n.z;
    float3 e = C4 * L00
             + 2 * C2 * (L11 * x + L1m1 * y + L10 * z)
             + C1 * L22 * (x * x - y * y) + C3 * L20 * z * z - C5 * L20
             + 2 * C1 * (L2m2 * x * y + L21 * x * z + L2m1 * y * z);
    // Brightness matched to the user-tuned fill (2026-10-16 F5: the raw result made the room near black).
    // The photo holds only ONE bounce of the room's light, while "Indoor fill" stands for all the light
    // a real room collects (many bounces, daylight through the window and the door). So the IBL keeps
    // its DIRECTION and COLOR (orange toward the forge, dark toward the corners) and the fill keeps the
    // overall LEVEL: scale so the average over all directions (= C4 * L00, the other terms average
    // to 0) has the fill's brightness. "Diffuse IBL strength" then lifts/lowers it on top.
    float avgLuma  = dot(C4 * L00 / PI, LUMA);
    float fillLuma = dot(flatFill, LUMA);
    float level    = fillLuma / max(avgLuma, EPSILON);
    return max(e, 0) / PI * level * strength;
}

// Specular reflection of the surroundings (split-sum, Karis 2013):
//   = (the room in the reflected direction, blurred for this roughness) x (F0 * A + B)
// Every surface reflects, not only metals: wood and stone reflect ~4% (F0) head-on and much more
// at grazing angles (Fresnel). The room is dark, so for them it stays subtle.
float3 IblSpecular(float3 f0, float rough, float3 n, float3 v, float3 wp, out bool available)
{
    uint w, h, mips;
    iblEnv.GetDimensions(0, w, h, mips);
    available = w > 0;
    if (!available) return 0;
    if (iblBox.Load(int3(2, 0, 0)).w < 0.5) return 0;   // F2 "reflections" off: no reflection of the room at all
    float nv = saturate(dot(n, v));
    float3 r = BoxProjectedDirection(wp, reflect(-v, n));         // where a mirror here would look
    float3 env = iblEnv.SampleLevel(iblSamp, r, rough * (mips - 1)).rgb;
    float2 ab = iblLut.SampleLevel(iblSamp, float2(nv, rough), 0).rg;
    return env * (f0 * ab.x + ab.y);
}

// Light from the whole surroundings (sky through the window, bounce light).
// With IBL: diffuse = the flat fill below (unchanged, user-tuned), specular = IblSpecular.
// Without IBL (fallback) -- two different "ambient" inputs on purpose:
//   diffuse  = base * diffuseAmbient * (1-metal)       diffuseAmbient  = room color x indoor FILL boost
//   specular = F * specularAmbient, ONLY for metals     specularAmbient = room color WITHOUT the boost
// The FILL boost (x4, F1 "Indoor fill strength") is an artist lift so dark corners stay readable --
// it is not how bright the room really is. Reflecting it (a mirror shows the real surroundings):
//   - on every surface turned the interior milky white, worst at grazing angles (2026-10-15 F5)
//   - on metals made the blade and the anvil top a bluish white at any temperature (2026-10-16 F5)
// Metals still need some reflection: they have no diffuse, so without it they turn pitch black in
// the shade (the classic "PBR metal looks like black plastic" pitfall).
// The proper fix is IBL: reflect what the room actually looks like (dark room -> dark reflection).
float3 AmbientLight(float3 base, float rough, float metal, float3 n, float3 v, float3 wp, float3 diffuseAmbient, float3 specularAmbient)
{
    float nv = saturate(dot(n, v));
    float3 f0 = SurfaceF0(base, metal);
    float3 diffuse = (1 - metal) * base * IblDiffuseOr(n, wp, diffuseAmbient);
    bool ibl;
    float3 specular = IblSpecular(f0, rough, n, v, wp, ibl);
    if (ibl) return diffuse + specular;
    float3 f = f0 + (max((1 - rough).xxx, f0) - f0) * pow(1 - nv, 5);	// roughness-aware Schlick Fresnel (Lagarde)
    return diffuse + f * metal * specularAmbient;
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

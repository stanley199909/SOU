// InteractionMap stamp: a smooth dome of "pressure" under the player.
// Drawn with MAX blend into the R16_FLOAT map, seen from a top-down orthographic camera.
// ASCII comments only so this file compiles regardless of code page.
//
// Profile p(r) = (1 - r^2)^2, r = distance from the centre / radius (0..1):
//   - 1 at the centre, 0 at the rim, with zero slope at the rim (no hard edge).
//   - Its slope is 0 at the centre and at the rim and steepest in between (r ~ 0.58).
//     The grass shader bends along the slope -> an "O": blades lean outward in a ring,
//     while the ones right under the feet are pressed down by the height itself.
struct PS_IN
{
	float4 pos   : SV_POSITION;
	float2 uv    : TEXCOORD0;
	float4 color : TEXCOORD1;
};

float4 main(PS_IN pin) : SV_TARGET
{
	float2 d = pin.uv * 2.0f - 1.0f;		// -1..1 across the quad
	float  r2 = dot(d, d);
	if (r2 >= 1.0f) discard;				// the quad is square, the stamp is round
	float  k = 1.0f - r2;
	float  pressure = k * k;
	return float4(pressure, 0.0f, 0.0f, 1.0f);
}

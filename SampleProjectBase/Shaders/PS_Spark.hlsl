// Pixel shader for spark streaks (hammer sparks, grinding sparks, burning sparks).
//
// The old look stretched a 64x64 round dot texture along the spark's motion, so a long
// streak became a blurry smear. Here the streak is computed from the UV with a formula
// instead of read from a texture, so it stays sharp at any size on screen
// (resolution independent):
//   across the streak : a thin hot core + a wider soft glow (two gaussians)
//   along the streak  : bright at the head (where it is going), fading to the tail,
//                       with a short rounded front so the head is not cut flat
// The CPU builds the quad with uv.x = across (0..1) and uv.y = along (0 = head, 1 = tail),
// and passes the colour (already faded over the particle's life). Additive blending,
// values above 1 feed the Bloom.
struct PS_IN
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : TEXCOORD1;
};

static const float CORE_SHARPNESS = 60.0;  // how thin the white-hot core is (larger = thinner)
static const float GLOW_SHARPNESS = 6.0;   // how wide the soft glow is
static const float GLOW_WEIGHT    = 0.35;  // glow brightness relative to the core
static const float CORE_BOOST     = 2.0;   // core brighter than 1 -> picked up by Bloom
static const float HEAD_ROUND     = 0.12;  // length of the rounded front (share of the streak)
static const float TAIL_POWER     = 1.5;   // how quickly it fades toward the tail

float4 main(PS_IN p) : SV_TARGET
{
    float x = p.uv.x * 2 - 1;                    // -1..1 across, 0 = centre line
    float along = p.uv.y;                        // 0 = head, 1 = tail
    float core = exp(-x * x * CORE_SHARPNESS) * CORE_BOOST;
    float glow = exp(-x * x * GLOW_SHARPNESS) * GLOW_WEIGHT;
    float head = smoothstep(0, HEAD_ROUND, along);   // rounded front
    float tail = pow(saturate(1 - along), TAIL_POWER); // fade to the tail
    float i = (core + glow) * head * tail;
    return float4(p.col.rgb * i, i * p.col.a);
}

// Flat color of the inverted hull outline (see VS_Outline.hlsl). a = fade.
// ASCII comments only so this file compiles regardless of code page.
cbuffer OutlinePS : register(b0)
{
    float4 color;
};

float4 main() : SV_TARGET
{
    return color;
}

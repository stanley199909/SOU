// One triangle that covers the whole render target, generated from the vertex index alone
// (no vertex buffer). Used by the one-time IBL bakes that write one value per texel.
// ASCII comments only so this file compiles regardless of code page.
struct VOUT { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
VOUT main(uint id : SV_VertexID)
{
    VOUT o;
    o.uv  = float2((id << 1) & 2, id & 2);                       // (0,0) (2,0) (0,2)
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);  // uv (0,0) = top-left
    return o;
}

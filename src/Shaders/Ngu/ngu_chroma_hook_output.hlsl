// Storage adapter for the target PS_1516 CbCr surface. Target PS_396 packs
// realign .x as Cb and .y as Cr, and libplacebo's chroma hook consumes Cb first.
// Evidence: OpenMadVR E-178, PS_396_e3199f71.
Texture2D<float4> SourceTex : register(t0);

cbuffer NguChromaHookParams : register(b0)
{
    float4 ChromaRange;
};

struct VSOut
{
    float4 position : SV_Position;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float4 value = SourceTex.Load(int3(pixel, 0));
    // Hardware decoder surfaces and CPU planes reach the NGU final texture in
    // different storage order. Convert the selected Cb,Cr pair from the
    // target's limited range to the shared renderer's full range.
    const float2 cbcr = ChromaRange.w > 0.5 ? value.yx : value.xy;
    const float2 full = ChromaRange.z > 0.5
                            ? cbcr
                            : saturate((cbcr - ChromaRange.x) * ChromaRange.y + 0.5);
    return float4(full, 0.0, 1.0);
}

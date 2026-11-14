// Storage adapter for returning native NGU output through the PL_HOOK_RGB
// texture contract. Native NGU writes logical RGB into its RGBA8 surface.
// Evidence: OpenMadVR E-025/E-059 and the target-backed NV12 orientation gate.
Texture2D<float4> SourceTex : register(t0);

struct VSOut
{
    float4 position : SV_Position;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    return SourceTex.Load(int3(pixel, 0));
}

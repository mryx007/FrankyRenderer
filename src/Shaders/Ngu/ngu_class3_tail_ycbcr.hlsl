// Target-derived PS_536 class-3 variant. Evidence: OpenMadVR E-057/E-058/E-073.
// The class-3 f36 input carries three RGB channels; the luma-only transport
// path used by P010/P016 broadcasts channel X and remains unchanged.
Texture2D<float4> SourceTex : register(t0);
SamplerState PointSampler : register(s0);

struct VSOut { float4 position : SV_Position; };

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
    float3 sourceRgb = SourceTex.SampleLevel(
        PointSampler,
        input.position.xy / float2(5120.0, 2880.0),
        0.0).xyz;

    float3 curved = log2((sourceRgb + 0.0989999995) * 0.909918129);
    curved = exp2(curved * 2.22222233);
    float3 linearRgb = sourceRgb * 0.221543506;
    float3 rgb = sourceRgb >= 0.0810000002 ? curved : linearRgb;

    float3 ycbcr = float3(
        dot(rgb, float3(0.212599993, 0.715200007, 0.0722000003)),
        dot(rgb, float3(-0.114572108, -0.385427892, 0.5)),
        dot(rgb, float3(0.5, -0.454152912, -0.0458470918)));
    ycbcr += float3(0.0, 0.5, 0.5);
    return float4(ycbcr, 0.0);
}

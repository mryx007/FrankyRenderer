Texture2D<float4> SourceTex : register(t0);
SamplerState SourceSampler : register(s0);

struct VSOut { float4 position : SV_Position; };
struct PSOut
{
    float4 luma : SV_Target0;
    float4 cbcr : SV_Target1;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

PSOut PSMain(VSOut input)
{
    float4 source = SourceTex.SampleLevel(SourceSampler, input.position.xy / float2(1920.0, 1080.0), 0.0);
    float3 rgb = source.xyz * 1.16438353 + -0.0730593577;
    float3 ycbcr = float3(
        dot(rgb, float3(0.212599993, 0.715200007, 0.0722000003)),
        dot(rgb, float3(-0.114572108, -0.385427892, 0.5)),
        dot(rgb, float3(0.5, -0.454152912, -0.0458470918)));
    ycbcr = ycbcr + float3(0.0, 0.5, 0.5);
    ycbcr = ycbcr * 0.858823538 + 0.0627451017;

    PSOut output;
    output.luma = float4(ycbcr.x, 0.0, 0.0, 0.0);
    output.cbcr = float4(ycbcr.yz, 0.0, 0.0);
    return output;
}

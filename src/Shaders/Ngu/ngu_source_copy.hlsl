Texture2D<float4> SourceTex : register(t0);
SamplerState SourceSampler : register(s0);

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
    int width, height;
    SourceTex.GetDimensions(width, height);
    const float4 value = SourceTex.SampleLevel(
        SourceSampler, input.position.xy / float2(width, height), 0.0);
    return float4(value.xy, 0.0, 0.0);
}

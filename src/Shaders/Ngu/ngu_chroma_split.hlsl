Texture2D<float4> CbCrTex : register(t0);
SamplerState CbCrSampler : register(s0);

struct VSOut { float4 position : SV_Position; };
struct PSOut
{
    float4 cb : SV_Target0;
    float4 cr : SV_Target1;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

PSOut PSMain(VSOut input)
{
    int width, height;
    CbCrTex.GetDimensions(width, height);
    const float4 packed = CbCrTex.SampleLevel(
        CbCrSampler, input.position.xy / float2(width, height), 0.0);
    PSOut output;
    output.cb = float4(packed.x, 0.0, 0.0, 0.0);
    output.cr = float4(packed.y, 0.0, 0.0, 0.0);
    return output;
}

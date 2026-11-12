// Target-derived class-3 PS_520 X scale. Evidence: E-155.
Texture2D<float4> SourceTex : register(t0);
Texture2D<float2> ScaleTex : register(t1);
SamplerState PointSampler : register(s0);
cbuffer Params : register(b0) { float4 U[1]; };

struct VSOut { float4 position : SV_Position; };

float EncodeTargetUnorm16(float value)
{
    const uint fixed20 =
        min(uint(floor(saturate(value) * 1048576.0)), 1048575u);
    const uint high = fixed20 >> 16;
    const uint low = fixed20 & 65535u;
    const uint highScaled = high * 65535u;
    const uint lowProduct = low * 65535u;
    const uint remainder =
        ((highScaled & 15u) << 16) +
        (lowProduct & 1048575u) + 524287u;
    const uint code =
        (highScaled >> 4) + (lowProduct >> 20) + (remainder >> 20);
    return float(code) / 65535.0;
}

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
    const float2 uv = input.position.xy / float2(640.0, 720.0);
    float3 result = 0.0;
    float tap = U[0].z;
    [loop]
    for (int i = 0; i < 4; ++i)
    {
        int tableWidth, tableHeight;
        ScaleTex.GetDimensions(tableWidth, tableHeight);
        const int2 tablePixel = clamp(
            int2(floor(tap * float(tableWidth)),
                 floor(uv.x * float(tableHeight))),
            int2(0, 0),
            int2(tableWidth - 1, tableHeight - 1));
        float2 scale = ScaleTex.Load(int3(tablePixel, 0));
        float3 sample =
            SourceTex.SampleLevel(PointSampler, float2(scale.x, uv.y), 0.0).xyz;
        result = mad(sample, scale.y, result);
        tap += U[0].w;
    }
    return float4(EncodeTargetUnorm16(result.x),
                  EncodeTargetUnorm16(result.y),
                  EncodeTargetUnorm16(result.z),
                  0.0);
}

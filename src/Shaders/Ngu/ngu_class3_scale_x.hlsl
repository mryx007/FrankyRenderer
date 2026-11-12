// Target-derived class-3 PS_520 X scale. Evidence: E-155.
Texture2D<float4> SourceTex : register(t0);
Texture2D<float2> ScaleTex : register(t1);
SamplerState PointSampler : register(s0);
cbuffer Params : register(b0) { float4 U[1]; };

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
    return float4(result, 0.0);
}

#ifndef NGU_SUPERRES_DOWN_WIDTH
#define NGU_SUPERRES_DOWN_WIDTH 640
#endif

Texture2D<float4> SourceTex : register(t0);
Texture2D<float2> ScaleTex : register(t1);
Texture2D<float2> UvLut : register(t2);
SamplerState PointSampler : register(s0);
cbuffer Params : register(b0) { float4 U0; };

struct VSOut { float4 position : SV_Position; };

float2 LoadExactUv(int2 pixel)
{
    float2 horizontal = UvLut.Load(int3(pixel.x, 0, 0));
    float2 vertical = UvLut.Load(int3(NGU_SUPERRES_DOWN_WIDTH + pixel.y, 0, 0));
    return float2(horizontal.x, vertical.y);
}

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    int tableWidth, tableHeight;
    ScaleTex.GetDimensions(tableWidth, tableHeight);
    int2 pixel = (int2)floor(input.position.xy);
    float2 uv = LoadExactUv(pixel);
    float3 result = 0.0;
    float tap = U0.z;
    [loop]
    for (int i = 0; i < 4; ++i)
    {
        float2 scale = ScaleTex.SampleLevel(PointSampler, float2(tap, uv.y), 0.0);
        float3 sample = SourceTex.SampleLevel(PointSampler, float2(uv.x, scale.x), 0.0).xyz;
        result = mad(sample, scale.y, result);
        tap += U0.w;
    }
    return float4(result, 0.0);
}

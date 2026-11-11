#ifndef NGU_SUPERRES_DOWN_WIDTH
#define NGU_SUPERRES_DOWN_WIDTH 640
#endif

Texture2D<float4> OriginalTex : register(t0);
Texture2D<float4> DownsampleTex : register(t1);
Texture2D<float2> UvLut : register(t2);
SamplerState PointSampler : register(s0);

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
    int2 pixel = (int2)floor(input.position.xy);
    float2 uv = LoadExactUv(pixel);
    float4 original = OriginalTex.SampleLevel(PointSampler, uv, 0.0);
    float4 down = DownsampleTex.SampleLevel(PointSampler, uv, 0.0);
    return float4(down.y - original.x, down.z - original.y, 0.0, 0.0);
}

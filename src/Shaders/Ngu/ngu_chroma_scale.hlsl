Texture2D<float4> SourceTex : register(t0);
SamplerState SourceSampler : register(s0);

cbuffer NguChromaScale : register(b0)
{
    float4 floatConsts;
    float4 weightArray;
    float4 rasterSize;
};

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
    const float2 uv = input.position.xy / rasterSize.xy;
#if defined(NGU_SCALE_Y)
    const float source = uv.y;
    const float other = uv.x;
#else
    const float source = uv.x;
    const float other = uv.y;
#endif

    const precise float scaled = floatConsts.x * source;
    const precise float fraction = frac(scaled);
    const float negative = (-fraction >= 0.0) ? 0.0 : 1.0;
    const precise float whole = scaled - fraction;
    const precise float base = (scaled >= 0.0) ? 0.0 : negative;
    const precise float x = base + whole;
    const float yCoord = x - 3.0;
    const float zCoord = x - 2.0;
    const float shifted = frac(x * 0.5) - 0.349999994;
    const float coordinated = (shifted >= 0.0) ? zCoord : yCoord;
    const float4 weights =
        (shifted >= 0.0) ? weightArray.zxyw : weightArray.wyxz;

    const float first = coordinated * floatConsts.y;
    const float second = mad(coordinated, floatConsts.y, floatConsts.z);
    const float third = second + floatConsts.z;
    const float fourth = third + floatConsts.z;
#if defined(NGU_SCALE_Y)
    const float2 uv0 = float2(other, first);
    const float2 uv1 = float2(other, second);
    const float2 uv2 = float2(other, third);
    const float2 uv3 = float2(other, fourth);
#else
    const float2 uv0 = float2(first, other);
    const float2 uv1 = float2(second, other);
    const float2 uv2 = float2(third, other);
    const float2 uv3 = float2(fourth, other);
#endif
    const float4 sample0 = SourceTex.SampleLevel(SourceSampler, uv0, 0.0);
    const float4 sample1 = SourceTex.SampleLevel(SourceSampler, uv1, 0.0);
    const float4 sample2 = SourceTex.SampleLevel(SourceSampler, uv2, 0.0);
    const float4 sample3 = SourceTex.SampleLevel(SourceSampler, uv3, 0.0);
    float4 result = mad(sample0, weights.x, sample1 * weights.y);
    result = mad(sample2, weights.z, result);
    result = mad(sample3, weights.w, result);
    return float4(result.xyz, 0.0);
}

// Target-derived class-3 PS_748_4085ea73 scale-Y translation.
// Evidence: OpenMadVR E-090/E-099/E-138, PS_748_4085ea73.
// The target's L16 output behaves as a 20-bit fixed intermediate followed by
// round-half-down to 16 bits. Encode that conversion explicitly so D3D11's
// R16_UNORM output merger does not introduce a second rounding rule.
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
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position =
        float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    const float2 uv = input.position.xy / rasterSize.xy;
    const precise float scaled = floatConsts.x * uv.y;
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
    const float4 sample0 = SourceTex.SampleLevel(
        SourceSampler, float2(uv.x, first), 0.0);
    const float4 sample1 = SourceTex.SampleLevel(
        SourceSampler, float2(uv.x, second), 0.0);
    const float4 sample2 = SourceTex.SampleLevel(
        SourceSampler, float2(uv.x, third), 0.0);
    const float4 sample3 = SourceTex.SampleLevel(
        SourceSampler, float2(uv.x, fourth), 0.0);
    float4 result = mad(sample0, weights.x, sample1 * weights.y);
    result = mad(sample2, weights.z, result);
    result = mad(sample3, weights.w, result);

    const uint fixed20 =
        min(uint(floor(saturate(result.x) * 1048576.0)), 1048575u);
    const uint high = fixed20 >> 16;
    const uint low = fixed20 & 65535u;
    const uint highScaled = high * 65535u;
    const uint lowProduct = low * 65535u;
    const uint remainder =
        ((highScaled & 15u) << 16) +
        (lowProduct & 1048575u) + 524287u;
    const uint code =
        (highScaled >> 4) + (lowProduct >> 20) + (remainder >> 20);
    return float4(float(code) / 65535.0, 0.0, 0.0, 0.0);
}

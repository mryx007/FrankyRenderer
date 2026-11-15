// Target-derived NGU luma quality vertical tail.
// PS_1404 is used by Low/Medium; PS_1576 adds the nonlinear output transform
// for High. PS_1612 has already applied the High source transform.
Texture2D<float4> SourceTex : register(t0);
Texture2D<float2> ScaleTex : register(t1);
SamplerState SourceSampler : register(s0);
SamplerState ScaleSampler : register(s2);

cbuffer Params : register(b0)
{
    float4 U0;
    float4 U1;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    const float2 corner =
        float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0),
        0.0,
        1.0);
    output.uv = corner;
    return output;
}

float3 Select3(float condition, float3 whenNonNegative, float3 otherwise)
{
    return condition >= 0.0 ? whenNonNegative : otherwise;
}

float3 SoftClip(float3 minimum, float3 maximum, float3 value)
{
    const float3 range = saturate((maximum - minimum) * 8.0);
    const float3 sum = minimum + maximum;
    const float3 upper = max(mad(sum, 0.5, 0.064453125), maximum);
    const float3 lower = min(minimum, mad(sum, 0.5, -0.064453125));
    const float3 lowOffset = mad(abs(lower - value), -range, lower);
    const float3 highOffset = mad(abs(value - upper), range, upper);
    return min(highOffset, max(value, lowOffset));
}

float4 PSMain(VSOut input) : SV_Target0
{
    const float2 uv = input.uv;
    const float3 zero3 = 0.0;
    const float3 one3 = 1.0;

    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;

    r0.x = U1.x + uv.y;
    r0.yz = mad(r0.xx, U0.yy, float2(-0.5, 0.5));
    r0.yz *= U0.xx;
    r0.w = rcp(U0.y);
    r0.yz *= r0.ww;
    r0.w = frac(r0.y);
    r0.y -= r0.w;
    r0.w = rcp(U0.x);
    r1.x = frac(-r0.z);
    r0.z += r1.x;

    r2.y = uv.y;
    float3 sourceCoordinate = float3(uv.x, 0.0, 0.0);
    float3 result = 0.0;

    float3 minimum = zero3;
    float3 maximum = one3;
    float3 lower = one3;
    float3 upper = zero3;
    float tapCoordinate = U0.z;

    [loop] for (int tap = 0; tap < 6; ++tap)
    {
        r2.x = tapCoordinate;
        const float2 scale = ScaleTex.SampleLevel(
            ScaleSampler, r2.xy, 0.0);
        sourceCoordinate.y = scale.x;
        const float3 sampleValue = SourceTex.SampleLevel(
            SourceSampler, sourceCoordinate.xy, 0.0).xyz;

        float vertical = mad(r0.y, r0.w, -scale.x);
        float reversed = mad(r0.z, -r0.w, scale.x);
        const float edgeDistance =
            mad(abs(r0.x - scale.x), U0.y, -1.0);

        const float3 lowerClamp = min(sampleValue, maximum);
        const float3 upperClamp = max(minimum, sampleValue);
        const float3 lowerSelect = min(sampleValue, lower);
        const float3 upperSelect = max(upper, sampleValue);
        const float3 raised = max(sampleValue, lower);
        const float3 lowered = min(upper, raised);
        float3 lowCandidate = Select3(edgeDistance, lower, lowerSelect);
        float3 highCandidate = Select3(edgeDistance, upper, upperSelect);
        float3 valueCandidate = Select3(edgeDistance, lowered, sampleValue);
        valueCandidate = SoftClip(maximum, minimum, valueCandidate);

        reversed = reversed >= 0.0 ? 0.0 : -1.0;
        vertical = vertical >= 0.0 ? -0.0 : reversed;

        maximum = Select3(vertical, maximum, lowerClamp);
        minimum = Select3(vertical, minimum, upperClamp);
        lower = Select3(vertical, lowCandidate, lowerSelect);
        upper = Select3(vertical, highCandidate, upperSelect);
        const float3 accumulated =
            Select3(vertical, valueCandidate, sampleValue);
        result = mad(accumulated, scale.y, result);
        tapCoordinate += U0.w;
    }

#ifdef NGU_QUALITY_TAIL_NONLINEAR
    {
        const float3 threshold = result + (-0.0179450233);
        const float3 linearValue = result * 4.51378632;
        const float3 curved =
            mad(exp2(log2(result) * 0.449999988),
                1.09899998,
                -0.0989999995);
        result = Select3(threshold, curved, linearValue);
    }
#endif
    return float4(result, 0.0);
}

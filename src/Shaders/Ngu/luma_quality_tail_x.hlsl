// Target-derived NGU luma quality horizontal tail.
// PS_1416 is used by Low/Medium; PS_1612 adds the nonlinear source transform
// for High. Both consume the target L16 dense-add surface.
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

    r0.x = U1.x + uv.x;
    r0.yz = mad(r0.xx, U0.yy, float2(-0.5, 0.5));
    r0.yz *= U0.xx;
    r0.w = rcp(U0.y);
    r0.yz *= r0.ww;
    r0.w = frac(r0.y);
    r0.y -= r0.w;
    r0.w = rcp(U0.x);
    r1.x = frac(-r0.z);
    r0.z += r1.x;

    float3 minimum = zero3;
    float3 maximum = one3;
    float3 lower = one3;
    float3 upper = zero3;
    float3 result = zero3;
    float tapCoordinate = U0.z;
    r2.y = uv.y;

    [loop] for (int tap = 0; tap < 6; ++tap)
    {
        const float2 scaleCoordinate =
            float2(tapCoordinate, uv.x);
        const float2 scale = ScaleTex.SampleLevel(
            ScaleSampler, scaleCoordinate, 0.0);

        const float2 sourceCoordinate =
            float2(scale.x, uv.y);
        float3 source = SourceTex.SampleLevel(
            SourceSampler, sourceCoordinate, 0.0).xxx;
#ifdef NGU_QUALITY_TAIL_NONLINEAR
        {
            const float3 threshold = source + (-0.0810000002);
            const float3 linearValue = source * 0.221543506;
            precise float3 offset = source + 0.0989999995;
            precise float3 logInput = offset * 0.909918129;
            const float3 curved =
                exp2(log2(logInput) * 2.22222233);
            source = Select3(threshold, curved, linearValue);
        }
#endif
        const float3 sampleValue = source;

        const float3 lowerClamp = min(sampleValue, maximum);
        const float3 upperClamp = max(minimum, sampleValue);
        const float3 lowerSelect = min(sampleValue, lower);
        const float3 upperSelect = max(upper, sampleValue);

        float horizontal = mad(r0.y, r0.w, -scale.x);
        float reversed = mad(r0.z, -r0.w, scale.x);
        const float edgeDistance =
            mad(abs(r0.x - scale.x), U0.y, -1.0);

        const float3 raised = max(sampleValue, lower);
        const float3 lowered = min(upper, raised);
        float3 lowCandidate = Select3(edgeDistance, lower, lowerSelect);
        float3 highCandidate = Select3(edgeDistance, upper, upperSelect);
        float3 valueCandidate = Select3(edgeDistance, lowered, sampleValue);
        valueCandidate = SoftClip(maximum, minimum, valueCandidate);

        reversed = reversed >= 0.0 ? 0.0 : -1.0;
        horizontal = horizontal >= 0.0 ? -0.0 : reversed;

        maximum = Select3(horizontal, maximum, lowerClamp);
        minimum = Select3(horizontal, minimum, upperClamp);
        lower = Select3(horizontal, lowCandidate, lowerSelect);
        upper = Select3(horizontal, highCandidate, upperSelect);
        const float3 accumulated =
            Select3(horizontal, valueCandidate, sampleValue);
        result = mad(accumulated, scale.y, result);
        tapCoordinate += U0.w;
    }

    return float4(result, 0.0);
}

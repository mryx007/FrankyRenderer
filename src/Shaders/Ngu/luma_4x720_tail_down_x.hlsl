Texture2D<float4> SourceTex : register(t0);
Texture2D<float2> ScaleTex : register(t1);
Texture2D<float2> UvLut : register(t2);
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
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float3 Select3(float condition, float3 whenNonNegative, float3 otherwise)
{
    return condition >= 0.0 ? whenNonNegative : otherwise;
}

float4 PSMain(VSOut input) : SV_Target0
{
    const int2 outputPixel = (int2)floor(input.position.xy);
    const float2 uv = float2(
        UvLut.Load(int3(outputPixel.x, 0, 0)).x,
        UvLut.Load(int3(outputPixel.y, 1, 0)).y);
    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;
    float4 r3 = 0.0;
    float4 r4 = 0.0;
    float4 r5 = 0.0;
    float4 r6 = 0.0;
    float4 r7 = 0.0;
    float4 r8 = 0.0;
    float4 r9 = 0.0;
    float4 r10 = 0.0;

    r0.x = U1.x + uv.x;
    r1.xy = float2(-0.5, 0.5);
    r0.yz = mad(r0.xx, U0.yy, r1.xy);
    r0.yz *= U0.xx;
    r0.w = rcp(U0.y);
    r0.yz *= r0.ww;
    r0.w = frac(r0.y);
    r0.y -= r0.w;
    r0.w = rcp(U0.x);
    r1.x = frac(-r0.z);
    r0.z += r1.x;

    r1.y = uv.x;
    r2.y = uv.y;
    r3.xyz = 0.0;
    r4.xyz = 1.0;
    r3.w = U0.z;
    r4.w = 0.0;
    r5 = 0.0;
    r6.x = 0.0;
    float tapCoordinate = r3.w;

    [loop] for (int tap = 0; tap < 6; ++tap)
    {
        r7 = float4(
            ScaleTex.Sample(
                ScaleSampler,
                float2(tapCoordinate, uv.x)),
            0.0,
            0.0);
        r8 = SourceTex.Sample(
            SourceSampler,
            float2(r7.x, uv.y));

        r1.x = -r5.w + r8.x;
        r1.z = r7.y * r1.x;
        r1.w = r4.w + r7.y;
        r2.x = rcp(r1.w);
        r2.z = r1.z * r2.x;
        r5.w = mad(r1.z, r2.x, r5.w);
        r1.x *= r4.w;
        r6.x = mad(r1.x, r2.z, r6.x);

        r1.x = mad(r0.y, r0.w, -r7.x);
        r1.z = mad(r0.z, -r0.w, r7.x);
        r2.xzw = min(float3(r8.x, r8.y, r8.z), r4.xyz);
        r6.yzw = max(float3(r5.x, r5.y, r5.z), r8.xyz);
        r7.x = r0.x - r7.x;
        r7.z = -1.0;
        r7.x = mad(abs(r7.x), U0.y, -1.0);
        r9.xyz = max(r8.xyz, r4.xyz);
        r10.xyz = min(r5.xyz, r9.xyz);
        r7.xzw = Select3(r7.x, r10.xyz, r8.xyz);
        r1.z = r1.z >= 0.0 ? 0.0 : -1.0;
        r1.x = r1.x >= 0.0 ? -0.0 : r1.z;
        r4.xyz = Select3(r1.x, r4.xyz, float3(r2.x, r2.z, r2.w));
        r5.xyz = Select3(r1.x, r5.xyz, float3(r6.y, r6.z, r6.w));
        r2.xzw = Select3(r1.x, float3(r7.x, r7.z, r7.w), r8.xyz);
        r3.xyz = mad(float3(r2.x, r2.z, r2.w), r7.y, r3.xyz);
        r3.w += U0.w;
        tapCoordinate += U0.w;
        r4.w = r1.w;
    }

    return float4(abs(r6.x), r3.xyz);
}

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

struct VSOut { float4 position : SV_Position; };

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
    float4 r11 = 0.0;

    r0.x = U1.x + uv.y;
    r1.xyz = float3(-0.5, 0.5, 1.0);
    r0.yz = mad(r0.xx, U0.yy, r1.xy);
    r0.yz *= U0.xx;
    r0.w = rcp(U0.y);
    r0.yz *= r0.ww;
    r0.w = frac(r0.y);
    r0.y -= r0.w;
    r0.w = rcp(U0.x);
    r1.x = frac(-r0.z);
    r0.z += r1.x;

    r2.y = uv.y;
    r3.xzw = float3(uv.x, 0.0, 0.0);
    r1.xyw = 0.0;
    r4.xyz = 1.0;
    r4.w = U0.z;
    r5 = 0.0;
    r6.xyz = 0.0;
    float tapCoordinate = r4.w;

    [loop] for (int tap = 0; tap < 6; ++tap)
    {
        r2.xzw = float3(tapCoordinate, 0.0, 0.0);
        r7 = float4(
            ScaleTex.Sample(ScaleSampler, r2.xy),
            0.0,
            0.0);
        r3.y = r7.x;
        r8 = SourceTex.Sample(SourceSampler, r3.xy);

        r6.z = mad(r8.x, r7.y, r6.z);
        r2.x = -r6.x + r8.y;
        r2.z = r7.y * r2.x;
        r2.w = r5.w + r7.y;
        r3.y = rcp(r2.w);
        r6.w = r2.z * r3.y;
        r6.x = mad(r2.z, r3.y, r6.x);
        r2.x *= r5.w;
        r6.y = mad(r2.x, r6.w, r6.y);

        r2.x = mad(r0.y, r0.w, -r7.x);
        r2.z = mad(r0.z, -r0.w, r7.x);
        r9.xyz = min(float3(r8.y, r8.z, r8.w), r4.xyz);
        r10.xyz = max(r5.xyz, float3(r8.y, r8.z, r8.w));
        r3.y = r0.x - r7.x;
        r3.y = mad(abs(r3.y), U0.y, -r1.z);
        r7.xzw = float3(
            max(r8.y, r4.x),
            max(r8.z, r4.y),
            max(r8.w, r4.z));
        r11.xyz = min(r5.xyz, float3(r7.x, r7.z, r7.w));
        r7.xzw = Select3(r3.y, r11.xyz, float3(r8.y, r8.z, r8.w));
        r2.z = r2.z >= 0.0 ? 0.0 : -1.0;
        r2.x = r2.x >= 0.0 ? -0.0 : r2.z;
        r4.xyz = Select3(r2.x, r4.xyz, r9.xyz);
        r5.xyz = Select3(r2.x, r5.xyz, r10.xyz);
        r7.xzw = Select3(r2.x, float3(r7.x, r7.z, r7.w), float3(r8.y, r8.z, r8.w));
        r1.xyw = mad(float3(r7.x, r7.z, r7.w), r7.y, float3(r1.x, r1.y, r1.w));
        r4.w += U0.w;
        tapCoordinate += U0.w;
        r5.w = r2.w;
    }

    return float4(abs(r6.y) + r6.z, r1.x, r1.y, r1.w);
}

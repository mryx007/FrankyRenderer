Texture2D<float4> RgbTex : register(t0);
Texture2D<float4> DitherTex : register(t1);
Texture2D<float4> WeightTex : register(t2);
SamplerState RgbSampler : register(s0);
SamplerState DitherSampler : register(s5);
SamplerState WeightSampler : register(s7);
cbuffer NguFinalParams : register(b0) { float4 U2; float4 U3; float4 U4; float4 U5; };

struct VSOut { float4 position : SV_Position; };

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    float4 v0 = float4(input.position.xy / U3.zw, 0.0, 1.0);
    const float4 c0 = float4(0.000199999995, -9.99999975e-05, 2.22222233, -0.5);
    const float4 c1 = float4(0.03125, 0.0, 0.0, 0.0);
    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;
    float4 r3 = 0.0;
    float4 r4 = 0.0;

    r0 = RgbTex.SampleLevel(RgbSampler, v0.xy, 0.0);
    r0.xyz = r0.xyz * U3.x + U3.y;
    r1.xy = c0.xy;
    r0.w = r1.x + U4.x;
    r0.xyz = r0.xyz * r0.w + c0.y;
    const float3 transformed = r0.xyz;

    r1.x = frac(r0.x);
    r1.z = frac(r0.y);
    r1.w = frac(r0.z);
    r1.xzw = r0.xyz - r1.xzw;

    r2.xyz = r1.xzw * U5.x;
    r3.x = log2(r2.x);
    r3.y = log2(r2.y);
    r3.z = log2(r2.z);
    r3.xyz *= c0.z;
    r3.x = exp2(r3.x);
    r3.y = exp2(r3.y);
    r3.z = exp2(r3.z);

    r2.xyz = r0.xyz * U5.x;
    r4.x = log2(r2.x);
    r4.y = log2(r2.y);
    r4.z = log2(r2.z);
    r4.xyz *= c0.z;
    r4.x = exp2(r4.x);
    r4.y = exp2(r4.y);
    r4.z = exp2(r4.z);
    r2.xyz = r4.xyz - r3.xyz;

    r4.xyz = frac(-r0.xyz);
    r0.xyz += r4.xyz;
    r0.xyz *= U5.x;
    r4.x = log2(r0.x);
    r4.y = log2(r0.y);
    r4.z = log2(r0.z);
    r4.xyz *= c0.z;
    r4.x = exp2(r4.x);
    r4.y = exp2(r4.y);
    r4.z = exp2(r4.z);
    r0.xyz = r4.xyz - r3.xyz;

    r3.x = rcp(r0.x);
    r3.y = rcp(r0.y);
    r3.z = rcp(r0.z);
    r0.xyz = r2.xyz * r3.xyz;
    r1.x = r2.x * r3.x + r1.x;
    r1.z = r2.y * r3.y + r1.z;
    r1.w = r2.z * r3.z + r1.w;
    r0.xyz = frac(r0.xyz);
    r0.xyz = r1.xzw - r0.xyz;

    r2.xy = U2.yz + v0.xy;
    r2 = DitherTex.SampleLevel(DitherSampler, r2.xy, 0.0);
    r0.w = r2.x + c0.w;

    r2.xy = U3.zw * v0.xy;
    r3.y = U2.x;
    r3.z = U2.y;
    r2.xy = r2.xy * c1.x + r3.yz;
    r2 = WeightTex.SampleLevel(WeightSampler, r2.xy, 0.0);
    r0.w = r0.w * U2.w - r2.x;

    r2.xyz = r0.w + r1.xzw;
    r3.xyz = frac(-r2.xyz);
    r2.xyz += r3.xyz;
    r3.xyz = max(r2.xyz, r0.xyz);
    r0.xyz = frac(-r1.xzw);
    r0.xyz += r1.xzw;
    r1.x = min(r0.x, r3.x);
    r1.z = min(r0.y, r3.y);
    r1.w = min(r0.z, r3.z);
    float3 finalValue = r1.xzw * U5.y - r1.y;
    finalValue = (frac(transformed) == 0.0 && transformed > 0.0) ?
                     saturate(transformed) : finalValue;
    return float4(finalValue, 0.0);
}

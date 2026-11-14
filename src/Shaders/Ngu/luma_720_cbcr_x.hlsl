Texture2D<float4> SourceTex : register(t0);
Texture2D<float4> ScaleTex : register(t1);
SamplerState PointSampler : register(s0);
cbuffer NguUpscaleParams : register(b0) { float4 U[2]; };
struct VSOut { float4 position : SV_Position; };
VSOut VSMain(uint vertex : SV_VertexID) { VSOut output; float2 corner = float2((vertex << 1) & 2, vertex & 2); output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); return output; }
float3 FetchSource(float2 position) { float4 value = SourceTex.SampleLevel(PointSampler, position, 0.0); return float3(value.x, value.y, 1.0); }
float4 FetchScale(float2 position) { return ScaleTex.SampleLevel(PointSampler, position, 0.0); }
float SelectCmp(float test, float valueIfPositive, float valueIfNegative) { return test < 0.0 ? valueIfNegative : valueIfPositive; }
float4 PSMain(VSOut input) : SV_Target0 { int sourceWidth, sourceHeight; SourceTex.GetDimensions(sourceWidth, sourceHeight); // c0.y is the captured output-axis size. The 4x tail is not 2x.
    float2 uv = input.position.xy / float2(U[0].y, sourceHeight); const float4 k2 = float4(-0.5, 0.0, 1.0, 0.5);
    const float4 k3 = float4(0.5, 1.5, 8.0, 0.064453125);
    const float4 k4 = float4(0.5, -0.064453125, 0.0, 0.0);
    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;
    float4 r3 = 0.0;
    float4 r4 = 0.0;
    float4 r5 = 0.0;
    float4 r6 = 0.0;
    float4 r7 = 0.0;

    r0.x = U[1].x + uv.x;
    r1.x = k2.x;
    r0.x = r0.x * U[0].x + r1.x;
    r0.y = frac(r0.x);
    r0.z = r0.x - r0.y;
    r0.y = SelectCmp(-r0.y, k2.y, k2.z);
    r0.y = SelectCmp(r0.x, k2.y, r0.y);
    r0.y += r0.z;
    r0.yz = r0.y + k2.xw;
    r0.w = rcp(U[0].x);
    r1.xy = r0.w * r0.yz;
    r1.z = uv.y;
    r2.xyz = FetchSource(r1.xzzw);
    r1.xyz = FetchSource(r1.yzzw);
    r2.z = 1.0;
    r1.z = 1.0;

    r0.y = frac(-r0.x);
    r0.x += r0.y;
    r0.xy = r0.xx + k3.xy;
    r0.xy *= r0.w;
    r0.z = uv.y;
    r3.xyz = FetchSource(r0.xzzw);
    r0.xyz = FetchSource(r0.yzzw);
    r3.z = 1.0;
    r0.z = 1.0;
    r4.xyz = r1.xyz + r3.xyz;
    r2.xyz = r4.xyz * -k2.w + r2.xyz;
    r0.xyz = r4.xyz * -k2.w + r0.xyz;
    r0.xyz = saturate(r0.xyz * r2.xyz);
    r2.xyz = min(r3.xyz, r1.xyz);
    r2.xyz = saturate(-r0.xyz + r2.xyz);
    r4.xyz = max(r1.xyz, r3.xyz);
    r0.xyz = saturate(r0.xyz + r4.xyz);
    r1.xyz = -r2.xyz + r0.xyz;
    r1.xyz = saturate(r1.xyz * k3.z);
    r3.xyz = r2.xyz + r0.xyz;
    r4.xyz = r3.xyz * k3.x + k3.w;
    r5.xyz = max(r4.xyz, r0.xyz);
    r0.xyz = r3.xyz * k4.x + k4.y;
    r3.xyz = min(r2.xyz, r0.xyz);
    r0.y = uv.x;
    r2.y = uv.y;
    r4.xyz = k2.y;
    r1.w = U[0].z;
    [loop]
    for (int tap = 0; tap < 4; ++tap)
    {
        r0.xzw = r1.www * k2.zyy;
        r6 = FetchScale(r0);
        r2.xzw = r6.xxx * k2.zyy;
        r7.xyz = FetchSource(r2);
        r7.z = 1.0;
        r0.x = r7.x - r5.x;
        r0.z = r7.y - r5.y;
        r0.w = r7.z - r5.z;
        r0.x = abs(r0.x) * r1.x + r5.x;
        r0.z = abs(r0.z) * r1.y + r5.y;
        r0.w = abs(r0.w) * r1.z + r5.z;
        r2.x = min(r0.x, r7.x);
        r2.z = min(r0.z, r7.y);
        r2.w = min(r0.w, r7.z);
        r0.x = r3.x - r2.x;
        r0.z = r3.y - r2.z;
        r0.w = r3.z - r2.w;
        r0.x = abs(r0.x) * -r1.x + r3.x;
        r0.z = abs(r0.z) * -r1.y + r3.y;
        r0.w = abs(r0.w) * -r1.z + r3.z;
        r6.x = max(r2.x, r0.x);
        r6.z = max(r2.z, r0.z);
        r6.w = max(r2.w, r0.w);
        r4.xyz = r6.xzw * r6.yyy + r4.xyz;
        r1.w += U[0].w;
    }
    return float4(r4.xyz, k2.y);
}

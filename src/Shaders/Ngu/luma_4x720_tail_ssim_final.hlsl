Texture2D<float4> RtTex : register(t0);
Texture2D<float4> SourceTex : register(t1);
Texture2D<float2> UvLut : register(t2);
SamplerState PointSampler : register(s0);
cbuffer Params : register(b0) { float4 U0; };

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
    const int2 pixel = (int2)floor(input.position.xy);
    const float2 uv = float2(
        UvLut.Load(int3(pixel.x, 0, 0)).x,
        UvLut.Load(int3(pixel.y, 1, 0)).y);
    const float4 c1 = float4(
        0.360000014, 0.0399999991, -0.0179450233, 4.51378632);
    const float4 c2 = float4(0.449999988, 1.09899998, -0.0989999995, 0);
    const float4 c3 = float4(-1, 0, 1, 0.119999997);
    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;
    float4 r3 = 0.0;

    r0.xy = U0.xy;
    r1 = mad(r0.xyxy, c3.xyyx, uv.xyxy);
    r2 = RtTex.Sample(PointSampler, r1.xy);
    r1 = RtTex.Sample(PointSampler, r1.zw);
    r1.xyz += r2.xyz;
    r2 = mad(r0.xyxy, c3.yzzy, uv.xyxy);
    r3 = RtTex.Sample(PointSampler, r2.xy);
    r2 = RtTex.Sample(PointSampler, r2.zw);
    r1.xyz += r3.xyz;
    r1.xyz += r2.xyz;
    r1.xyz *= c3.w;
    r2 = RtTex.Sample(PointSampler, uv);
    r1.xyz = mad(r2.xyz, c1.xxx, r1.xyz);

    r0.zw = -U0.xy + uv.xy;
    r2 = RtTex.Sample(PointSampler, r0.zw);
    r0.zw = U0.xy + uv.xy;
    r3 = RtTex.Sample(PointSampler, r0.zw);
    r2.xyz += r3.xyz;
    r0 = mad(r0.xyxy, c3.xzzx, uv.xyxy);
    r3 = RtTex.Sample(PointSampler, r0.xy);
    r0 = RtTex.Sample(PointSampler, r0.zw);
    r2.xyz += r3.xyz;
    r0.xyz += r2.xyz;
    r0.xyz = mad(r0.xyz, c1.yyy, r1.xyz);

    r0.w = rcp(U0.z);
    r0.z = mad(r0.z, r0.w, c3.z);
    r0.z = rcp(r0.z);
    r0.z = -r0.z + c3.z;

    r1 = SourceTex.Sample(PointSampler, uv);
    r0.x = mad(r0.x, r1.y, r0.y);
    r0.y = -r0.x + r1.y;
    r0.y = mad(r0.z, r0.y, r0.x);
    float2 logged = float2(log2(r1.y), log2(r0.y));
    r0.zw = logged * c2.x;
    float2 exponent = float2(exp2(r0.z), exp2(r0.w));
    r0.zw = mad(exponent, c2.y, c2.z);
    r0.x = r1.y;

    float2 linearRgb = r0.xy * c1.w;
    float2 curvedRgb = r0.xy + c1.z;
    float2 rgb = curvedRgb >= 0.0 ? r0.zw : linearRgb;
    return float4(rgb, r1.z, r1.w);
}

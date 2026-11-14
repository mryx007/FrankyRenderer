Texture2D<float4> SourceTex : register(t0);
SamplerState PointSampler : register(s0);
cbuffer Ngu720ScaleY : register(b0) {
    float4 floatConsts;
    float4 weightArray;
};

struct VSOut { float4 position : SV_Position; };

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position =
        float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float SelectCmp(float test, float positive, float negative)
{
    return test < 0.0 ? negative : positive;
}

float4 PSMain(VSOut input) : SV_Target0
{
    const float2 uv = input.position.xy / float2(2560.0, 1440.0);
    const float4 c2 = float4(0.0, 1.0, 0.5, -0.35);
    const float4 c3 = float4(-3.0, -2.0, 0.0, 0.0);
    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;
    float4 r3 = 0.0;

    r0.x = floatConsts.x * uv.y;
    r0.y = frac(r0.x);
    r0.z = SelectCmp(-r0.y, c2.x, c2.y);
    r0.y = r0.x - r0.y;
    r0.x = SelectCmp(r0.x, c2.x, r0.z);
    r0.x = r0.x + r0.y;
    r0.yz = r0.xx + c3.xy;
    r0.x = frac(r0.x * c2.z) + c2.w;
    r0.y = SelectCmp(r0.x, r0.z, r0.y);
    r1 = float4(
        SelectCmp(r0.x, weightArray.z, weightArray.w),
        SelectCmp(r0.x, weightArray.x, weightArray.y),
        SelectCmp(r0.x, weightArray.y, weightArray.x),
        SelectCmp(r0.x, weightArray.w, weightArray.z));

    r2.y = r0.y * floatConsts.y;
    r2.w = r0.y * floatConsts.y + floatConsts.z;
    r2.xz = uv.xx;
    r0 = SourceTex.SampleLevel(PointSampler, r2.xy, 0.0);
    r3 = SourceTex.SampleLevel(PointSampler, r2.zw, 0.0);
    r2.y = r2.w + floatConsts.z;
    r3.xyz = r1.yyy * r3.xyz;
    r0.xyz = r0.xyz * r1.xxx + r3.xyz;

    r2.xz = uv.xx;
    r3 = SourceTex.SampleLevel(PointSampler, r2.xy, 0.0);
    r2.w = r2.y + floatConsts.z;
    r2 = SourceTex.SampleLevel(PointSampler, r2.zw, 0.0);
    r0.xyz = r3.xyz * r1.zzz + r0.xyz;
    r0.xyz = r2.xyz * r1.www + r0.xyz;
    r0.w = c2.x;
    return r0;
}

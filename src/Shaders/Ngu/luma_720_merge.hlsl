Texture2D<float4> YTex : register(t0);
Texture2D<float4> CbCrTex : register(t1);
SamplerState PointSampler : register(s0);
cbuffer Ngu720Merge : register(b0) {
    float4 floatConsts1;
    float4 reserved;
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

float4 PSMain(VSOut input) : SV_Target0
{
    const float2 uv = input.position.xy / float2(2560.0, 1440.0);
    float4 position = floatConsts1.zwxy + uv.xyxy;
    const float4 y = YTex.SampleLevel(PointSampler, position.xy, 0.0);
    const float4 cbcr =
        CbCrTex.SampleLevel(PointSampler, position.zw, 0.0);
    float4 converted;
    // Keep the target's Y/Cb/Cr channel order explicit. A vector
    // assignment here is re-swizzled by the SM5 compiler for this
    // texture-load pattern and does not preserve cbcr.x -> Y and
    // cbcr.y -> Cr.
    converted.x = y.x * 1.16438353 - 0.0730593577;
    converted.y = cbcr.x * 1.16438353 - 0.57305938;
    converted.z = cbcr.y * 1.16438353 - 0.57305938;
    converted.w = cbcr.w;
    converted.w = cbcr.w;
    float3 rgb;
    rgb.x = converted.x + 1.57480001 * converted.z;
    rgb.y = converted.x - 0.187324271 * converted.y -
        0.46812427 * converted.z;
    rgb.z = converted.x + 1.8556 * converted.y;
    return float4(rgb * 0.858823538 + 0.0627451017, 0.0);
}

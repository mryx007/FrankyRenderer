// Target-derived P010 720p Double RGB split.
// Evidence: E-163, PS_484_c7b4dcca.
Texture2D<float4> RGBTex : register(t0);

struct VSOut
{
    float4 position : SV_Position;
};

struct PSOut
{
    float4 y : SV_Target0;
    float4 cb : SV_Target1;
    float4 cr : SV_Target2;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position =
        float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 Fetch(Texture2D<float4> tex, float2 uv)
{
    int width, height;
    tex.GetDimensions(width, height);
    int2 pixel = (int2)floor(uv * float2(width, height));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return tex.Load(int3(pixel, 0));
}

PSOut PSMain(VSOut input)
{
    int width, height;
    RGBTex.GetDimensions(width, height);
    const float2 uv = input.position.xy / float2(width, height);
    const float3 converted =
        Fetch(RGBTex, uv).xyz * 1.16438353 +
        float3(-0.0730593577, -0.0730593577, -0.0730593577);
    const float3 ycbcr = float3(
        dot(converted, float3(0.212599993, 0.715200007, 0.0722000003)),
        dot(converted, float3(-0.114572108, -0.385427892, 0.5)),
        dot(converted, float3(0.5, -0.454152912, -0.0458470918)));
    const float3 biased = (ycbcr + float3(0.0, 0.5, 0.5)) *
        0.858823538 + 0.0627451017;

    PSOut output;
    output.y = float4(biased.x, 0.0, 0.0, 0.0);
    output.cb = float4(biased.y, 0.0, 0.0, 0.0);
    output.cr = float4(biased.z, 0.0, 0.0, 0.0);
    return output;
}

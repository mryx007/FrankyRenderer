// Target-derived P010 720p Double Y/Cb/Cr composition.
// Evidence: E-163, PS_908_2a1d27a9.
Texture2D<float4> YTex : register(t0);
Texture2D<float4> CbTex : register(t1);
Texture2D<float4> CrTex : register(t2);
Texture2D<float4> NNTex : register(t3);

cbuffer NguHighBitToRgb : register(b0)
{
    float4 pixSize;
    float4 srcSize;
};

struct VSOut
{
    float4 position : SV_Position;
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

float4 PSMain(VSOut input) : SV_Target0
{
    int outputWidth, outputHeight;
    YTex.GetDimensions(outputWidth, outputHeight);
    const float2 uv =
        input.position.xy / float2(outputWidth, outputHeight);
    const float2 scaled = uv * srcSize.xy;
    const float2 whole = floor(scaled + 0.5);
    const float2 nearest = (whole + 0.5) * pixSize.xy;
    const float2 parity = frac(scaled) - 0.5;
    const float4 residual = Fetch(NNTex, nearest);
    float2 residualPosition =
        parity.y >= 0.0 ? residual.yx : residual.wz;
    const float chosen =
        parity.x >= 0.0 ? residualPosition.y : residualPosition.x;
    const float2 chromaCoordinate = uv;

    const float3 source = float3(
        Fetch(YTex, uv).x + chosen,
        Fetch(CbTex, chromaCoordinate).x,
        Fetch(CrTex, chromaCoordinate).x);
    const float3 converted = source * 1.16438353 +
        float3(-0.0730593577, -0.57305938, -0.57305938);
    const float3 rgb = float3(
        converted.x + 1.57480001 * converted.z,
        converted.x - 0.187324271 * converted.y -
            0.46812427 * converted.z,
        converted.x + 1.8556 * converted.y);
    return float4(rgb * 0.858823538 + 0.0627451017, 0.0);
}

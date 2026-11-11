Texture2D<float4> InputX : register(t0);
Texture2D<float4> InputY : register(t1);
Texture2D<float4> InputZ : register(t2);
Texture2D<float4> InputW : register(t3);
SamplerState PointSampler : register(s0);
cbuffer Params : register(b0) { float4 U[2]; };

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
    const float2 texcoord = input.position.xy / float2(2560.0, 1440.0);
    const float2 source = texcoord * U[1].xy;
    const float2 rounded = mad(texcoord, U[1].xy, float2(0.5, 0.5));
    const float2 base = floor(rounded) + float2(0.5, 0.5);
    const float2 uv = base * U[0].xy;
    const float2 fraction = frac(source);
    const bool xHigh = fraction.x >= 0.5;
    const bool yHigh = fraction.y >= 0.5;

    if (!xHigh && !yHigh)
        return InputW.SampleLevel(PointSampler, uv, 0.0);
    if (xHigh && !yHigh)
        return InputZ.SampleLevel(PointSampler, uv, 0.0);
    if (!xHigh && yHigh)
        return InputY.SampleLevel(PointSampler, uv, 0.0);
    return InputX.SampleLevel(PointSampler, uv, 0.0);
}

Texture2D<float4> Input0 : register(t0);
Texture2D<float4> Input1 : register(t1);
SamplerState PointSampler : register(s0);
cbuffer Params : register(b0) { float4 U[132]; };

struct VSOut { float4 position : SV_Position; };
struct PSOut
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(
        corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 DotRows(float4 sample, int base)
{
    return float4(dot(sample, U[base]),
                  dot(sample, U[base + 1]),
                  dot(sample, U[base + 2]),
                  dot(sample, U[base + 3]));
}

float4 LeakyReLU(float4 value)
{
    float4 negative = (value < 0.0) ? value * U[129].x : 0.0;
    return negative + max(value, 0.0);
}

float4 Output0(float4 s00, float4 s10, float4 s01, float4 s11,
               float4 s02, float4 s12, float4 s03, float4 s13)
{
    precise float4 value = DotRows(s00, 40) + U[128];
    value = value + DotRows(s10, 104);
    value = value + DotRows(s01, 32);
    value = value + DotRows(s11, 96);
    value = value + DotRows(s02, 8);
    value = value + DotRows(s12, 72);
    value = value + DotRows(s03, 0);
    value = value + DotRows(s13, 64);
    return LeakyReLU(value);
}

float4 Output1(float4 s00, float4 s10, float4 s01, float4 s11,
               float4 s02, float4 s12, float4 s03, float4 s13)
{
    precise float4 value = DotRows(s00, 44) + U[128];
    value = value + DotRows(s10, 108);
    value = value + DotRows(s01, 36);
    value = value + DotRows(s11, 100);
    value = value + DotRows(s02, 12);
    value = value + DotRows(s12, 76);
    value = value + DotRows(s03, 4);
    value = value + DotRows(s13, 68);
    return LeakyReLU(value);
}

float4 Output2(float4 s00, float4 s10, float4 s01, float4 s11,
               float4 s02, float4 s12, float4 s03, float4 s13)
{
    precise float4 value = DotRows(s00, 56) + U[128];
    value = value + DotRows(s10, 120);
    value = value + DotRows(s01, 48);
    value = value + DotRows(s11, 112);
    value = value + DotRows(s02, 24);
    value = value + DotRows(s12, 88);
    value = value + DotRows(s03, 16);
    value = value + DotRows(s13, 80);
    return LeakyReLU(value);
}

float4 Output3(float4 s00, float4 s10, float4 s01, float4 s11,
               float4 s02, float4 s12, float4 s03, float4 s13)
{
    precise float4 value = DotRows(s00, 60) + U[128];
    value = value + DotRows(s10, 124);
    value = value + DotRows(s01, 52);
    value = value + DotRows(s11, 116);
    value = value + DotRows(s02, 28);
    value = value + DotRows(s12, 92);
    value = value + DotRows(s03, 20);
    value = value + DotRows(s13, 84);
    return LeakyReLU(value);
}

PSOut PSMain(VSOut input)
{
    const float2 sourceSize = U[131].xy;
    const float2 pixelSize = U[130].xy;
    const float2 texcoord = input.position.xy / sourceSize;
    const float2 minusOne = mad(texcoord, sourceSize, float2(-1.0, -1.0));
    const float2 base = minusOne * pixelSize;
    const float2 nextX = base + float2(pixelSize.x, 0.0);
    const float2 nextY = base + float2(0.0, pixelSize.y);
    const float2 diagonal = mad(minusOne, pixelSize, pixelSize);

    float4 s00 = Input0.SampleLevel(PointSampler, base, 0.0);
    float4 s10 = Input1.SampleLevel(PointSampler, base, 0.0);
    float4 s01 = Input0.SampleLevel(PointSampler, nextX, 0.0);
    float4 s11 = Input1.SampleLevel(PointSampler, nextX, 0.0);
    float4 s02 = Input0.SampleLevel(PointSampler, nextY, 0.0);
    float4 s12 = Input1.SampleLevel(PointSampler, nextY, 0.0);
    float4 s03 = Input0.SampleLevel(PointSampler, diagonal, 0.0);
    float4 s13 = Input1.SampleLevel(PointSampler, diagonal, 0.0);

    PSOut output;
    output.target0 = Output0(s00, s10, s01, s11, s02, s12, s03, s13);
    output.target1 = Output1(s00, s10, s01, s11, s02, s12, s03, s13);
    output.target2 = Output2(s00, s10, s01, s11, s02, s12, s03, s13);
    output.target3 = Output3(s00, s10, s01, s11, s02, s12, s03, s13);
    return output;
}

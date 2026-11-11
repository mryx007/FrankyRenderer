// Target-derived 1080p NGU Sharp VeryHigh Cubici1 initializer.
// Evidence: OpenMadVR E-095, PS_1412_aad01c2f.
Texture2D<float4> SourceTex : register(t0);

cbuffer NguParams : register(b0)
{
    float4 U[32];
};

struct VSOut
{
    float4 position : SV_Position;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 Fetch(float2 position)
{
    int width, height;
    SourceTex.GetDimensions(width, height);
    int2 pixel = (int2)floor(position * float2(width, height));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return SourceTex.Load(int3(pixel, 0));
}

float4 LeakyReLU(float4 value)
{
    float4 negative = (value < 0.0) ? value * U[30].x : 0.0;
    return negative + max(value, 0.0);
}

struct PSOut
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
};

PSOut PSMain(float4 position : SV_Position)
{
    const float4 center = float4(position.xy * U[31].xy, 0.0, 1.0);
    const float4 offsets = float4(0.0, -1.0, 1.0, 0.0);

    float4 s0 = Fetch(center - U[31]);
    float4 accumulator0 = mad(s0.x, U[0], U[27]);

    float4 offsetA = float4(offsets.x, offsets.y, offsets.z, 0.0);
    float4 positionA = U[31].xyxy * offsetA.xyzy + center.xyxy;
    float4 sampleA0 = Fetch(positionA.xy);
    float4 sampleA1 = Fetch(positionA.zw);
    accumulator0 = mad(sampleA0.x, U[1], accumulator0);
    accumulator0 = mad(sampleA1.x, U[2], accumulator0);

    float4 positionB = U[31].xyxy * offsetA.yxzx + center.xyxy;
    float4 sampleB0 = Fetch(positionB.xy);
    float4 sampleB1 = Fetch(positionB.zw);
    accumulator0 = mad(sampleB0.x, U[3], accumulator0);
    float4 centerSample = Fetch(center.xy);
    accumulator0 = mad(centerSample.x, U[4], accumulator0);
    accumulator0 = mad(sampleB1.x, U[5], accumulator0);

    float4 positionC = U[31].xyxy * offsetA.yzxz + center.xyxy;
    float4 sampleC0 = Fetch(positionC.xy);
    float4 sampleC1 = Fetch(positionC.zw);
    accumulator0 = mad(sampleC0.x, U[6], accumulator0);
    accumulator0 = mad(sampleC1.x, U[7], accumulator0);

    float4 offsetD = U[31].xxyw + center.xxyw;
    float4 diagonal = Fetch(offsetD.yz);
    accumulator0 = mad(diagonal.x, U[8], accumulator0);
    float4 target0 = LeakyReLU(accumulator0);

    float4 feature1 = mad(s0.x, U[9], U[28]);
    feature1 = mad(sampleA0.x, U[10], feature1);
    feature1 = mad(sampleA1.x, U[11], feature1);
    feature1 = mad(sampleB0.x, U[12], feature1);
    feature1 = mad(centerSample.x, U[13], feature1);
    feature1 = mad(sampleB1.x, U[14], feature1);
    feature1 = mad(sampleC0.x, U[15], feature1);
    feature1 = mad(sampleC1.x, U[16], feature1);
    feature1 = mad(diagonal.x, U[17], feature1);
    float4 target1 = LeakyReLU(feature1);

    float4 feature2 = mad(s0.x, U[18], U[29]);
    feature2 = mad(sampleA0.x, U[19], feature2);
    feature2 = mad(sampleA1.x, U[20], feature2);
    feature2 = mad(sampleB0.x, U[21], feature2);
    feature2 = mad(centerSample.x, U[22], feature2);
    feature2 = mad(sampleB1.x, U[23], feature2);
    feature2 = mad(sampleC0.x, U[24], feature2);
    feature2 = mad(sampleC1.x, U[25], feature2);
    feature2 = mad(diagonal.x, U[26], feature2);
    float4 target2 = LeakyReLU(feature2);

    PSOut output;
    output.target0 = target0;
    output.target1 = target1;
    output.target2 = target2;
    return output;
}

// Target-derived NGU Sharp Low Cubici0 initializer.
// Evidence: OpenMadVR E-025, PS_1152_a9a5033a.
// The two outputs are the Low profile's complete feature state.
Texture2D<float4> SourceTex : register(t0);

cbuffer NguQualityLowInitialParams : register(b0)
{
    float4 U[22];
};

struct VSOut { float4 position : SV_Position; };

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position =
        float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float Fetch(float2 position)
{
    int width, height;
    SourceTex.GetDimensions(width, height);
    int2 pixel = int2(floor(position * float2(width, height)));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return SourceTex.Load(int3(pixel, 0));
}

float4 LeakyReLU(float4 value)
{
    const float4 negative = (value < 0.0) ? value * U[20].x : 0.0;
    return negative + max(value, 0.0);
}

struct PSOut
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
};

PSOut PSMain(float4 position : SV_Position)
{
    const float4 center = float4(position.xy * U[21].xy, 0.0, 1.0);
    const float4 offsets = float4(0.0, -1.0, 1.0, 0.0);

    const float4 downLeft = Fetch(center - U[21]);
    const float4 positionA = U[21].xyxy * offsets.xyzy + center.xyxy;
    const float4 left = Fetch(positionA.xy);
    const float4 right = Fetch(positionA.zw);
    const float4 positionB = U[21].xyxy * offsets.yxzx + center.xyxy;
    const float4 upLeft = Fetch(positionB.xy);
    const float4 up = Fetch(positionB.zw);
    const float4 centerSample = Fetch(center.xy);
    const float4 positionC = U[21].xyxy * offsets.yzxz + center.xyxy;
    const float4 downRight = Fetch(positionC.xy);
    const float4 down = Fetch(positionC.zw);
    const float4 offsetD = U[21].xxyw + center.xxyw;
    const float4 bottom = Fetch(offsetD.yz);

    float4 feature0 = mad(downLeft.x, U[0], U[18]);
    feature0 = mad(left.x, U[1], feature0);
    feature0 = mad(right.x, U[2], feature0);
    feature0 = mad(upLeft.x, U[3], feature0);
    feature0 = mad(centerSample.x, U[4], feature0);
    feature0 = mad(up.x, U[5], feature0);
    feature0 = mad(downRight.x, U[6], feature0);
    feature0 = mad(down.x, U[7], feature0);
    feature0 = mad(bottom.x, U[8], feature0);

    float4 feature1 = mad(downLeft.x, U[9], U[19]);
    feature1 = mad(left.x, U[10], feature1);
    feature1 = mad(right.x, U[11], feature1);
    feature1 = mad(upLeft.x, U[12], feature1);
    feature1 = mad(centerSample.x, U[13], feature1);
    feature1 = mad(up.x, U[14], feature1);
    feature1 = mad(downRight.x, U[15], feature1);
    feature1 = mad(down.x, U[16], feature1);
    feature1 = mad(bottom.x, U[17], feature1);

    PSOut output;
    output.target0 = LeakyReLU(feature0);
    output.target1 = LeakyReLU(feature1);
    return output;
}

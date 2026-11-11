// Target-derived NGU Sharp VeryHigh Cubic11 hidden layer.
// Evidence: OpenMadVR E-095, PS_3588_80fb311e.
Texture2D<float4> Feature0 : register(t0);
Texture2D<float4> Feature1 : register(t1);
Texture2D<float4> Feature2 : register(t2);

cbuffer NguLayerParams : register(b0)
{
    float4 U[111];
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

float4 Fetch0(float2 position)
{
    int width, height;
    Feature0.GetDimensions(width, height);
    int2 pixel = (int2)floor(position * float2(width, height));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature0.Load(int3(pixel, 0));
}

float4 Fetch1(float2 position)
{
    int width, height;
    Feature1.GetDimensions(width, height);
    int2 pixel = (int2)floor(position * float2(width, height));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature1.Load(int3(pixel, 0));
}

float4 Fetch2(float2 position)
{
    int width, height;
    Feature2.GetDimensions(width, height);
    int2 pixel = (int2)floor(position * float2(width, height));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature2.Load(int3(pixel, 0));
}

float4 Group0(float2 position, int base)
{
    float4 sample = Fetch0(position);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 Group1(float2 position, int base)
{
    float4 sample = Fetch1(position);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 Group2(float2 position, int base)
{
    float4 sample = Fetch2(position);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 SampleGroup(float2 position, int base)
{
    float4 a = Group0(position, base);
    float4 b = Group1(position, base + 36);
    float4 c = Group2(position, base + 72);
    return c + (a + b);
}

float4 LeakyReLU(float4 value)
{
    float4 negative = (value < 0.0) ? value * U[109].x : 0.0;
    return negative + max(value, 0.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const float2 center = position.xy * U[110].xy;
    float4 value = SampleGroup(center - U[110].xy, 0) + U[108];

    const float4 offsets = float4(0.0, -1.0, 1.0, -1.0);
    float4 positions0 = U[110].xyxy * offsets + center.xyxy;
    value += SampleGroup(positions0.xy, 4);
    value += SampleGroup(positions0.zw, 8);

    float4 positions1 = U[110].xyxy * float4(-1.0, 0.0, 1.0, 0.0) + center.xyxy;
    value += SampleGroup(positions1.xy, 12);
    value += SampleGroup(center, 16);
    value += SampleGroup(positions1.zw, 20);

    float4 positions2 = U[110].xyxy * float4(-1.0, 1.0, 0.0, 1.0) + center.xyxy;
    value += SampleGroup(positions2.xy, 24);
    value += SampleGroup(positions2.zw, 28);
    value += SampleGroup(center + U[110].xy, 32);
    return LeakyReLU(value);
}

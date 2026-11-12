// Target-derived native NGU VeryHigh chroma Cubic22 layer.
// Evidence: OpenMadVR E-085, PS_4620_342cc74b.
Texture2D<float4> Feature0 : register(t0);
Texture2D<float4> Feature1 : register(t1);
Texture2D<float4> Feature2 : register(t2);
Texture2D<float4> Feature3 : register(t3);

cbuffer NguChromaParams : register(b0)
{
    float4 U[147];
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

float4 Fetch0(float2 position, int2 dimensions)
{
    int2 pixel = int2(floor(position * float2(dimensions)));
    pixel = clamp(pixel, int2(0, 0), dimensions - int2(1, 1));
    return Feature0.Load(int3(pixel, 0));
}

float4 Fetch1(float2 position, int2 dimensions)
{
    int2 pixel = int2(floor(position * float2(dimensions)));
    pixel = clamp(pixel, int2(0, 0), dimensions - int2(1, 1));
    return Feature1.Load(int3(pixel, 0));
}

float4 Fetch2(float2 position, int2 dimensions)
{
    int2 pixel = int2(floor(position * float2(dimensions)));
    pixel = clamp(pixel, int2(0, 0), dimensions - int2(1, 1));
    return Feature2.Load(int3(pixel, 0));
}

float4 Fetch3(float2 position, int2 dimensions)
{
    int2 pixel = int2(floor(position * float2(dimensions)));
    pixel = clamp(pixel, int2(0, 0), dimensions - int2(1, 1));
    return Feature3.Load(int3(pixel, 0));
}

float4 Group0(float2 position, int base, int2 dimensions)
{
    const float4 sample = Fetch0(position, dimensions);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 Group1(float2 position, int base, int2 dimensions)
{
    const float4 sample = Fetch1(position, dimensions);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 Group2(float2 position, int base, int2 dimensions)
{
    const float4 sample = Fetch2(position, dimensions);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 Group3(float2 position, int base, int2 dimensions)
{
    const float4 sample = Fetch3(position, dimensions);
    precise float4 value = sample.y * U[base + 1];
    value = mad(sample.x, U[base], value);
    value = mad(sample.z, U[base + 2], value);
    value = mad(sample.w, U[base + 3], value);
    return value;
}

float4 Tap(float2 position, int base, int2 dimensions)
{
    precise float4 value =
        Group0(position, base, dimensions) +
        Group1(position, base + 36, dimensions);
    value += Group2(position, base + 72, dimensions);
    value += Group3(position, base + 108, dimensions);
    return value;
}

float4 LeakyReLU(float4 value)
{
    const float4 negative = (value < 0.0) ? value * U[145].x : 0.0;
    return negative + max(value, 0.0);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    int width, height;
    Feature0.GetDimensions(width, height);
    const float2 uv = position.xy / float2(width, height);
    const int2 featureDimensions = int2(width, height);
    const float2 step = U[146].xy;

    const precise float4 center = Tap(uv, 16, featureDimensions);
    precise float4 top = Tap(uv - step, 0, featureDimensions);
    top += U[144];
    top += Tap(uv + float2(0.0, -1.0) * step, 4, featureDimensions);
    top += Tap(uv + float2(1.0, -1.0) * step, 8, featureDimensions);
    precise float4 value = center + top;
    value += Tap(uv + float2(-1.0, 0.0) * step, 12, featureDimensions);
    value += Tap(uv + float2(1.0, 0.0) * step, 20, featureDimensions);
    value += Tap(uv + float2(-1.0, 1.0) * step, 24, featureDimensions);
    value += Tap(uv + float2(0.0, 1.0) * step, 28, featureDimensions);
    value += Tap(uv + step, 32, featureDimensions);
    return LeakyReLU(value);
}

// Target-derived P010 720p Double NGU layer.
// Evidence: E-163, PS_4280_d591580d.
Texture2D<float4> NN1 : register(t0);
Texture2D<float4> NN2 : register(t1);

cbuffer NguHighBit4280 : register(b0)
{
    float4 U[148];
};

struct VSOut
{
    float4 position : SV_Position;
};

struct PSOut
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
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

float4 Weighted(float4 sample, int constant)
{
    precise float4 value = sample.y * U[constant + 1];
    value = mad(sample.x, U[constant], value);
    value = mad(sample.z, U[constant + 2], value);
    value = mad(sample.w, U[constant + 3], value);
    return value;
}

float4 LeakyReLU(float4 value)
{
    const float4 negative = (value < 0.0) ? value * U[146].x : 0.0;
    return negative + max(value, 0.0);
}

PSOut PSMain(VSOut input)
{
    const float2 center = input.position.xy * U[147].xy;
    float4 target0 = U[144];
    float4 target1 = U[145];

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const int sampleIndex = (y + 1) * 3 + (x + 1);
            const float2 position =
                center + float2(x, y) * U[147].xy;
            const int base = sampleIndex * 4;
            target0 += Weighted(Fetch(NN1, position), base);
            target0 += Weighted(Fetch(NN2, position), 36 + base);
            target1 += Weighted(Fetch(NN1, position), 72 + base);
            target1 += Weighted(Fetch(NN2, position), 108 + base);
        }
    }

    PSOut output;
    output.target0 = LeakyReLU(target0);
    output.target1 = LeakyReLU(target1);
    return output;
}

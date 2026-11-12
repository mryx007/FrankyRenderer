// Target-derived native NGU VeryHigh chroma Cubici2 initializer.
// Evidence: OpenMadVR E-085, PS_1672_b083dfa8.
Texture2D<float4> SourceTex : register(t0);

cbuffer NguChromaCubici2Params : register(b0)
{
    float4 U[42];
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

float Fetch(int2 offset, float2 position)
{
    int width, height;
    SourceTex.GetDimensions(width, height);
    const int2 center =
        int2(floor(position * float2(width, height)));
    const int2 pixel =
        clamp(center + offset, int2(0, 0), int2(width - 1, height - 1));
    return SourceTex.Load(int3(pixel, 0)).x;
}

float4 LeakyReLU(float4 value)
{
    const float4 negative = (value < 0.0) ? value * U[40].x : 0.0;
    return negative + max(value, 0.0);
}

struct PSOut
{
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
    float4 target2 : SV_Target2;
    float4 target3 : SV_Target3;
};

PSOut PSMain(float4 position : SV_Position)
{
    int width, height;
    SourceTex.GetDimensions(width, height);
    const float2 uv = position.xy / float2(width, height);
    const float down_left = Fetch(int2(-1, -1), uv);
    const float down = Fetch(int2(0, -1), uv);
    const float down_right = Fetch(int2(1, -1), uv);
    const float left = Fetch(int2(-1, 0), uv);
    const float center = Fetch(int2(0, 0), uv);
    const float right = Fetch(int2(1, 0), uv);
    const float up_left = Fetch(int2(-1, 1), uv);
    const float up = Fetch(int2(0, 1), uv);
    const float up_right = Fetch(int2(1, 1), uv);

    float4 a0 = mad(down_left, U[0], U[36]);
    a0 = mad(down, U[1], a0);
    a0 = mad(down_right, U[2], a0);
    a0 = mad(left, U[3], a0);
    a0 = mad(center, U[4], a0);
    a0 = mad(right, U[5], a0);
    a0 = mad(up_left, U[6], a0);
    a0 = mad(up, U[7], a0);
    a0 = mad(up_right, U[8], a0);

    float4 a1 = mad(down_left, U[9], U[37]);
    a1 = mad(down, U[10], a1);
    a1 = mad(down_right, U[11], a1);
    a1 = mad(left, U[12], a1);
    a1 = mad(center, U[13], a1);
    a1 = mad(right, U[14], a1);
    a1 = mad(up_left, U[15], a1);
    a1 = mad(up, U[16], a1);
    a1 = mad(up_right, U[17], a1);

    float4 a2 = mad(down_left, U[18], U[38]);
    a2 = mad(down, U[19], a2);
    a2 = mad(down_right, U[20], a2);
    a2 = mad(left, U[21], a2);
    a2 = mad(center, U[22], a2);
    a2 = mad(right, U[23], a2);
    a2 = mad(up_left, U[24], a2);
    a2 = mad(up, U[25], a2);
    a2 = mad(up_right, U[26], a2);

    float4 a3 = mad(down_left, U[27], U[39]);
    a3 = mad(down, U[28], a3);
    a3 = mad(down_right, U[29], a3);
    a3 = mad(left, U[30], a3);
    a3 = mad(center, U[31], a3);
    a3 = mad(right, U[32], a3);
    a3 = mad(up_left, U[33], a3);
    a3 = mad(up, U[34], a3);
    a3 = mad(up_right, U[35], a3);

    PSOut output;
    output.target0 = LeakyReLU(a0);
    output.target1 = LeakyReLU(a1);
    output.target2 = LeakyReLU(a2);
    output.target3 = LeakyReLU(a3);
    return output;
}

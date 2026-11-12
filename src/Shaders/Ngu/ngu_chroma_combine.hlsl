// Target-derived NGU VeryHigh chroma Cubicd2x combine.
// Evidence: OpenMadVR E-091/E-092, PS_564_500335c8 / PS_644_512f7798.
Texture2D<float4> AddInput : register(t0);
Texture2D<float4> NnInput : register(t1);
Texture2D<float4> CbInput : register(t2);

cbuffer NguChromaCombineParams : register(b0)
{
    float4 U[2];
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

float4 FetchAdd(float2 position)
{
    int width, height;
    AddInput.GetDimensions(width, height);
    // Captured D3D9 point sampling lands on the current 1:1 output pixel.
    int2 pixel = int2(floor(position));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return AddInput.Load(int3(pixel, 0));
}

float4 FetchNn(int2 pixel)
{
    int width, height;
    NnInput.GetDimensions(width, height);
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return NnInput.Load(int3(pixel, 0));
}

float4 FetchCb(float2 position)
{
    int width, height;
    CbInput.GetDimensions(width, height);
    int2 pixel = int2(floor(position));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return CbInput.Load(int3(pixel, 0));
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    // E-124: target Cubicd2x maps output coordinate n to dense (n + 1) / 2.
    // Even output coordinates select the positive half; odd select negative.
    const int2 outputPixel = int2(position.xy);
    const int2 densePixel = (outputPixel + 1) >> 1;
    const float4 nn = FetchNn(densePixel);
    const bool xPositive = (outputPixel.x & 1) == 0;
    const bool yPositive = (outputPixel.y & 1) == 0;
    const float selected =
        xPositive ? (yPositive ? nn.w : nn.y)
                  : (yPositive ? nn.z : nn.x);
    const float value = selected + FetchAdd(position.xy).x;

#if defined(NGU_COMBINE_2)
    return float4(FetchCb(position.xy).x, value, 0.0, 1.0);
#else
    return float4(value, 0.0, 0.0, 1.0);
#endif
}

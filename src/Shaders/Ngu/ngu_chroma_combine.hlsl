// Target-derived NGU VeryHigh chroma Cubicd2x combine.
// Evidence: OpenMadVR E-091/E-092, PS_564_500335c8 / PS_644_512f7798.
Texture2D<float4> AddInput : register(t0);
Texture2D<float4> NnInput : register(t1);
Texture2D<float4> CbInput : register(t2);

cbuffer NguChromaCombineParams : register(b0)
{
    float4 U[2];
};

static const float2 RasterSize = float2(1280.0, 720.0);

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

float4 FetchNn(float2 position)
{
    int width, height;
    NnInput.GetDimensions(width, height);
    int2 pixel = int2(floor(position * float2(width, height)));
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

float Quantize16(float value)
{
    return floor(saturate(value) * 65535.0 + 0.5) / 65535.0;
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    // D3D9 v0 is SV_Position plus half a pixel under the captured vertices.
    const float2 uv = (position.xy + 0.5) / RasterSize;
    const float2 grid = uv * U[1].xy;
    const float2 fraction = frac(grid);
    const float2 center = grid - fraction + 0.5;
    const float4 nn = FetchNn(center * U[0].xy);

    const float2 phase = frac(uv * U[1].xy) - 0.5;
    const float2 y = (phase.y < 0.0) ? nn.xy : nn.zw;
    const float selected = (phase.x < 0.0) ? y.x : y.y;
    const float value = selected + FetchAdd(position.xy).x;

#if defined(NGU_COMBINE_2)
    return float4(Quantize16(FetchCb(position.xy).x), Quantize16(value), 0.0, 1.0);
#else
    return float4(Quantize16(value), 0.0, 0.0, 1.0);
#endif
}

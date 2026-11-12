// Target-derived NGU chroma YCbCr42xToRGB_0Taps_8 pack.
// Evidence: OpenMadVR E-035/E-093, PS_396_e3199f71.
Texture2D<float4> YInput : register(t0);
Texture2D<float4> CbCrInput : register(t1);
SamplerState PointSampler : register(s0);

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

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const float2 uv = position.xy / RasterSize;
    const float y = YInput.SampleLevel(PointSampler, uv, 0).x;
    const float2 cbcr = CbCrInput.SampleLevel(PointSampler, uv, 0).xy;
    return float4(y, cbcr, 0.0);
}

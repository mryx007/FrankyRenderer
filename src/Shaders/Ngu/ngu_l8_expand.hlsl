// D3D9 L8 sampling expands luma into RGB with alpha one.
Texture2D<float4> Input : register(t0);

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
    const int2 pixel = int2(floor(position.xy));
    const float value = Input.Load(int3(pixel, 0)).x;
    return float4(value, value, value, 1.0);
}

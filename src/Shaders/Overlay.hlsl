// Minimal bitmap drawing contract for ISubRender11 callbacks.
// Video processing and final overlay composition remain in libplacebo.
struct Vertex { float4 position : POSITION; float2 uv : TEXCOORD; };
struct Fragment { float4 position : SV_POSITION; float2 uv : TEXCOORD; };
Fragment vertex_main(Vertex input) {
    Fragment output; output.position = input.position; output.uv = input.uv;
    return output;
}
Texture2D bitmap : register(t0);
SamplerState bitmapSampler : register(s0);
float4 pixel_main(Fragment input) : SV_Target {
    return bitmap.Sample(bitmapSampler, input.uv);
}
float4 fullscreen_main(uint index : SV_VertexID) : SV_POSITION {
    return float4(index == 2 ? 3.0 : -1.0, index == 1 ? 3.0 : -1.0, 0, 1);
}
float4 alpha_main() : SV_Target { return float4(0, 0, 0, 1); }

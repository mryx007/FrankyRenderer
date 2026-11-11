#ifndef NGU_SUPERRES_FULL_WIDTH
#define NGU_SUPERRES_FULL_WIDTH 1280
#endif
#ifndef NGU_SUPERRES_FULL_HEIGHT
#define NGU_SUPERRES_FULL_HEIGHT 720
#endif

Texture2D<float4> SourceTex : register(t0);
Texture2D<float4> WorkTex : register(t1);
Texture2D<float4> DifferenceTex : register(t2);
Texture2D<float2> UvLut : register(t3);
SamplerState SourceSampler : register(s0);
SamplerState WorkSampler : register(s2);
SamplerState DifferenceSampler : register(s3);

// PS3 G16R16F texld returns (r, g, 1, 1); D3D11 returns (r, g, 0, 1).
float4 SampleDifference(float2 uv)
{
    float4 value = DifferenceTex.SampleLevel(DifferenceSampler, uv, 0.0);
    value.z = 1.0;
    return value;
}

float4 SamplePoint(Texture2D<float4> tex, float2 uv)
{
    int width, height;
    tex.GetDimensions(width, height);
    int2 pixel = (int2)floor(uv * float2(width, height));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return tex.Load(int3(pixel, 0));
}

float4 SampleLinear(Texture2D<float4> tex, float2 uv)
{
    int width, height;
    tex.GetDimensions(width, height);
    float2 position = uv * float2(width, height) - 0.5;
    int2 base = (int2)floor(position);
    float2 fraction = position - float2(base);
    int2 maximum = int2(width - 1, height - 1);
    int2 p00 = clamp(base, int2(0, 0), maximum);
    int2 p10 = clamp(base + int2(1, 0), int2(0, 0), maximum);
    int2 p01 = clamp(base + int2(0, 1), int2(0, 0), maximum);
    int2 p11 = clamp(base + int2(1, 1), int2(0, 0), maximum);
    float4 v00 = tex.Load(int3(p00, 0));
    float4 v10 = tex.Load(int3(p10, 0));
    float4 v01 = tex.Load(int3(p01, 0));
    float4 v11 = tex.Load(int3(p11, 0));
    int2 inverse = 256 - fraction;
    float4 top = lerp(v00, v10, fraction.x);
    float4 bottom = lerp(v01, v11, fraction.x);
    return lerp(top, bottom, fraction.y);
}

float2 LoadExactUv(int2 pixel)
{
    float2 horizontal = UvLut.Load(int3(pixel.x, 0, 0));
    float2 vertical = UvLut.Load(int3(NGU_SUPERRES_FULL_WIDTH + pixel.y, 0, 0));
    return float2(horizontal.x, vertical.y);
}

cbuffer Params : register(b0) {
    float4 c0;
    float4 c1;
    float4 c4;
    float4 c5;
    float4 c6;
    float4 c7;
};

struct VSOut { float4 position : SV_Position; };

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
    int2 pixel = (int2)floor(input.position.xy);
    float4 v0 = float4(LoadExactUv(pixel), 0.0, 1.0);
    float4 r0 = 0.0;
    float4 r1 = 0.0;
    float4 r2 = 0.0;
    float4 r3 = 0.0;
    float4 r4 = 0.0;
    float4 r5 = 0.0;
    float4 r6 = 0.0;
    float4 r7 = 0.0;
    float4 r8 = 0.0;
    float4 r9 = 0.0;
    float4 r10 = 0.0;
    float4 r11 = 0.0;
    float4 r12 = 0.0;
    float4 oC0 = 0.0;
    const float4 c2 = float4(-1, 0, 1.44269502, 1);
    const float4 c3 = float4(-1, 1, 2, 0);
    const float4 c8 = float4(-0.5, 0.5, 6, 0);
    const float4 c9 = float4(-0.5, 0.5, 1.5, 2.5);
    const float4 c10 = float4(7.96875, 0, 0, 0);

    // Preserve the target ps_3_0 instruction rounding, including every mad.
        r0.x = c0.x;
        r0.y = c0.y;
    {
        float4 prev_r0 = r0;
        r0.x = mad(v0.x, prev_r0.x, -(c1.z));
        r0.y = mad(v0.y, prev_r0.y, -(c1.w));
        r0.z = mad(v0.x, prev_r0.x, -(c1.z));
        r0.w = mad(v0.y, prev_r0.y, -(c1.w));
    }
    {
        float4 prev_r0 = r0;
        r0.x = (prev_r0.x + c8.x);
        r0.y = (prev_r0.y + c8.x);
        r0.z = (prev_r0.z + c8.x);
        r0.w = (prev_r0.w + c8.x);
    }
    {
        float4 prev_r0 = r0;
        r1.x = frac(prev_r0.z);
        r1.y = frac(prev_r0.w);
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r1.x = (prev_r0.z + -(prev_r1.x));
        r1.y = (prev_r0.w + -(prev_r1.y));
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r1.x = (prev_r0.z + -(prev_r1.x));
        r1.y = (prev_r0.w + -(prev_r1.y));
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r0.x = (prev_r0.x + -(prev_r1.x));
        r0.y = (prev_r0.y + -(prev_r1.y));
        r0.z = (prev_r0.z + -(prev_r1.x));
        r0.w = (prev_r0.w + -(prev_r1.y));
    }
    {
        float4 prev_r0 = r0;
        r2.x = (prev_r0.z + c8.y);
        r2.y = (prev_r0.w + c8.y);
        r2.z = (prev_r0.z + c8.x);
        r2.w = (prev_r0.w + c8.x);
    }
    {
        float4 prev_r2 = r2;
        r2.x = (prev_r2.x * c0.z);
        r2.y = (prev_r2.y * c0.w);
        r2.z = (prev_r2.z * c0.z);
        r2.w = (prev_r2.w * c0.w);
    }
    {
        float4 prev_r2 = r2;
        r3 = SampleDifference(float2(prev_r2.z, prev_r2.w));
    }
    {
        float4 prev_r0 = r0;
        r4.x = (prev_r0.z + c9.x);
        r4.y = (prev_r0.w + c9.y);
        r4.z = (prev_r0.z + c9.x);
        r4.w = (prev_r0.w + c9.z);
    }
    {
        float4 prev_r4 = r4;
        r4.x = (prev_r4.x * c0.z);
        r4.y = (prev_r4.y * c0.w);
        r4.z = (prev_r4.z * c0.z);
        r4.w = (prev_r4.w * c0.w);
    }
    {
        float4 prev_r4 = r4;
        r5 = SampleDifference(float2(prev_r4.x, prev_r4.y));
    }
    {
        float4 prev_r1 = r1;
        r6.x = (-(prev_r1.x) + c2.x);
        r6.y = (-(prev_r1.y) + c2.x);
        r6.z = (-(prev_r1.x) + c2.x);
        r6.w = (-(prev_r1.y) + c2.y);
    }
    {
        float4 prev_r6 = r6;
        r1.z = dot(float2(prev_r6.z, prev_r6.w), float2(prev_r6.z, prev_r6.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        r1.w = dot(float2(prev_r6.x, prev_r6.y), float2(prev_r6.x, prev_r6.y)) + c8.w;
    }
        r6.x = (c1.y + c1.y);
    {
        float4 prev_r6 = r6;
        r6.x = rcp(prev_r6.x);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r6 = r6;
        r1.z = (-(prev_r1.z) * prev_r6.x);
        r1.w = (-(prev_r1.w) * prev_r6.x);
    }
    {
        float4 prev_r1 = r1;
        r1.z = (prev_r1.z * c2.z);
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.z = exp2(prev_r1.z);
    }
        r7 = WorkTex.SampleLevel(WorkSampler, float2(v0.x, v0.y), 0.0);
    {
        float4 prev_r7 = r7;
        float4 prev_r5 = r5;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r5.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r5.w));
    }
    {
        float4 prev_r5 = r5;
        r5.z = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r5 = r5;
        r5.z = rcp(prev_r5.z);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r5 = r5;
        r1.z = (prev_r1.z * prev_r5.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r1 = r1;
        r5.x = (prev_r5.x * prev_r1.z);
        r5.y = (prev_r5.y * prev_r1.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r3 = r3;
        r3.z = mad(prev_r7.y, c8.z, -(prev_r3.z));
        r3.w = mad(prev_r7.z, c8.z, -(prev_r3.w));
    }
    {
        float4 prev_r3 = r3;
        r3.z = dot(float2(prev_r3.z, prev_r3.w), float2(prev_r3.z, prev_r3.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.z = rcp(prev_r3.z);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.w * prev_r3.z);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.z, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r5 = r5;
        r3.x = mad(prev_r3.w, prev_r3.x, prev_r5.x);
        r3.y = mad(prev_r3.w, prev_r3.y, prev_r5.y);
    }
    {
        float4 prev_r4 = r4;
        r5 = SampleDifference(float2(prev_r4.z, prev_r4.w));
    }
    {
        float4 prev_r1 = r1;
        r8.x = (-(prev_r1.x) + c3.x);
        r8.y = (-(prev_r1.y) + c3.y);
        r8.z = (-(prev_r1.x) + c3.x);
        r8.w = (-(prev_r1.y) + c3.z);
    }
    {
        float4 prev_r8 = r8;
        r1.w = dot(float2(prev_r8.x, prev_r8.y), float2(prev_r8.x, prev_r8.y)) + c8.w;
    }
    {
        float4 prev_r8 = r8;
        r3.z = dot(float2(prev_r8.z, prev_r8.w), float2(prev_r8.z, prev_r8.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r3 = r3;
        r3.z = (prev_r6.x * -(prev_r3.z));
    }
    {
        float4 prev_r3 = r3;
        r3.z = (prev_r3.z * c2.z);
    }
    {
        float4 prev_r3 = r3;
        r3.z = exp2(prev_r3.z);
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r5 = r5;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r5.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r5.w));
    }
    {
        float4 prev_r5 = r5;
        r3.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.w = rcp(prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r5.z = (prev_r1.w * prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.w, prev_r1.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r3 = r3;
        r3.x = mad(prev_r5.z, prev_r5.x, prev_r3.x);
        r3.y = mad(prev_r5.z, prev_r5.y, prev_r3.y);
    }
    {
        float4 prev_r0 = r0;
        r5.x = (prev_r0.z + c9.x);
        r5.y = (prev_r0.w + c9.w);
        r5.z = (prev_r0.z + c9.y);
        r5.w = (prev_r0.w + c9.x);
    }
    {
        float4 prev_r5 = r5;
        r5.x = (prev_r5.x * c0.z);
        r5.y = (prev_r5.y * c0.w);
        r5.z = (prev_r5.z * c0.z);
        r5.w = (prev_r5.w * c0.w);
    }
    {
        float4 prev_r5 = r5;
        r8 = SampleDifference(float2(prev_r5.x, prev_r5.y));
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r8 = r8;
        r5.x = mad(prev_r7.y, c8.z, -(prev_r8.z));
        r5.y = mad(prev_r7.z, c8.z, -(prev_r8.w));
    }
    {
        float4 prev_r5 = r5;
        r1.w = dot(float2(prev_r5.x, prev_r5.y), float2(prev_r5.x, prev_r5.y)) + c2.w;
    }
    {
        float4 prev_r1 = r1;
        r1.w = rcp(prev_r1.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.w * prev_r3.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r1 = r1;
        r1.z = mad(prev_r3.z, prev_r1.w, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r8 = r8;
        r3.x = mad(prev_r3.w, prev_r8.x, prev_r3.x);
        r3.y = mad(prev_r3.w, prev_r8.y, prev_r3.y);
    }
    {
        float4 prev_r5 = r5;
        r8 = SampleDifference(float2(prev_r5.z, prev_r5.w));
    }
    {
        float4 prev_r5 = r5;
        r5 = SourceTex.SampleLevel(SourceSampler, float2(prev_r5.z, prev_r5.w), 0.0);
    }
    {
        float4 prev_r1 = r1;
        r9.x = (-(prev_r1.x) + c2.y);
        r9.y = (-(prev_r1.y) + c2.x);
        r9.z = (-(prev_r1.x) + c2.y);
        r9.w = (-(prev_r1.y) + c2.w);
    }
    {
        float4 prev_r9 = r9;
        r1.w = dot(float2(prev_r9.x, prev_r9.y), float2(prev_r9.x, prev_r9.y)) + c8.w;
    }
    {
        float4 prev_r9 = r9;
        r3.z = dot(float2(prev_r9.z, prev_r9.w), float2(prev_r9.z, prev_r9.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r3 = r3;
        r3.z = (prev_r6.x * -(prev_r3.z));
    }
    {
        float4 prev_r3 = r3;
        r3.z = (prev_r3.z * c2.z);
    }
    {
        float4 prev_r3 = r3;
        r3.z = exp2(prev_r3.z);
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r8 = r8;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r8.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r8.w));
    }
    {
        float4 prev_r5 = r5;
        r3.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.w = rcp(prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r5.z = (prev_r1.w * prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.w, prev_r1.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r8 = r8;
        float4 prev_r3 = r3;
        r3.x = mad(prev_r5.z, prev_r8.x, prev_r3.x);
        r3.y = mad(prev_r5.z, prev_r8.y, prev_r3.y);
    }
    {
        float4 prev_r2 = r2;
        r8 = SampleDifference(float2(prev_r2.x, prev_r2.y));
    }
    {
        float4 prev_r1 = r1;
        r1.w = dot(float2(-(prev_r1.x), -(prev_r1.y)), float2(-(prev_r1.x), -(prev_r1.y))) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r8 = r8;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r8.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r8.w));
    }
    {
        float4 prev_r5 = r5;
        r3.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.w = rcp(prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r5.z = (prev_r1.w * prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.w, prev_r1.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r8 = r8;
        float4 prev_r3 = r3;
        r3.x = mad(prev_r5.z, prev_r8.x, prev_r3.x);
        r3.y = mad(prev_r5.z, prev_r8.y, prev_r3.y);
    }
    {
        float4 prev_r0 = r0;
        r8.x = (prev_r0.z + c9.y);
        r8.y = (prev_r0.w + c9.z);
        r8.z = (prev_r0.z + c9.y);
        r8.w = (prev_r0.w + c9.w);
    }
    {
        float4 prev_r8 = r8;
        r8.x = (prev_r8.x * c0.z);
        r8.y = (prev_r8.y * c0.w);
        r8.z = (prev_r8.z * c0.z);
        r8.w = (prev_r8.w * c0.w);
    }
    {
        float4 prev_r8 = r8;
        r9 = SampleDifference(float2(prev_r8.x, prev_r8.y));
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r9 = r9;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r9.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r9.w));
    }
    {
        float4 prev_r5 = r5;
        r1.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r1 = r1;
        r1.w = rcp(prev_r1.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.w * prev_r3.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r1 = r1;
        r1.z = mad(prev_r3.z, prev_r1.w, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r9 = r9;
        r3.x = mad(prev_r3.w, prev_r9.x, prev_r3.x);
        r3.y = mad(prev_r3.w, prev_r9.y, prev_r3.y);
    }
    {
        float4 prev_r8 = r8;
        r9 = SampleDifference(float2(prev_r8.z, prev_r8.w));
    }
    {
        float4 prev_r8 = r8;
        r8 = SourceTex.SampleLevel(SourceSampler, float2(prev_r8.x, prev_r8.y), 0.0);
    }
    {
        float4 prev_r1 = r1;
        r10.x = (-(prev_r1.x) + c3.w);
        r10.y = (-(prev_r1.y) + c3.z);
        r10.z = (-(prev_r1.x) + c3.y);
        r10.w = (-(prev_r1.y) + c3.x);
    }
    {
        float4 prev_r10 = r10;
        r1.w = dot(float2(prev_r10.x, prev_r10.y), float2(prev_r10.x, prev_r10.y)) + c8.w;
    }
    {
        float4 prev_r10 = r10;
        r3.z = dot(float2(prev_r10.z, prev_r10.w), float2(prev_r10.z, prev_r10.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r3 = r3;
        r3.z = (prev_r6.x * -(prev_r3.z));
    }
    {
        float4 prev_r3 = r3;
        r3.z = (prev_r3.z * c2.z);
    }
    {
        float4 prev_r3 = r3;
        r3.z = exp2(prev_r3.z);
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r9 = r9;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r9.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r9.w));
    }
    {
        float4 prev_r5 = r5;
        r3.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.w = rcp(prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r5.z = (prev_r1.w * prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.w, prev_r1.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r9 = r9;
        float4 prev_r3 = r3;
        r3.x = mad(prev_r5.z, prev_r9.x, prev_r3.x);
        r3.y = mad(prev_r5.z, prev_r9.y, prev_r3.y);
    }
    {
        float4 prev_r0 = r0;
        r9.x = (prev_r0.z + c9.z);
        r9.y = (prev_r0.w + c9.x);
        r9.z = (prev_r0.z + c9.z);
        r9.w = (prev_r0.w + c9.y);
    }
    {
        float4 prev_r9 = r9;
        r9.x = (prev_r9.x * c0.z);
        r9.y = (prev_r9.y * c0.w);
        r9.z = (prev_r9.z * c0.z);
        r9.w = (prev_r9.w * c0.w);
    }
    {
        float4 prev_r9 = r9;
        r10 = SampleDifference(float2(prev_r9.x, prev_r9.y));
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r10 = r10;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r10.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r10.w));
    }
    {
        float4 prev_r5 = r5;
        r1.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r1 = r1;
        r1.w = rcp(prev_r1.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.w * prev_r3.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r1 = r1;
        r1.z = mad(prev_r3.z, prev_r1.w, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r10 = r10;
        r3.x = mad(prev_r3.w, prev_r10.x, prev_r3.x);
        r3.y = mad(prev_r3.w, prev_r10.y, prev_r3.y);
    }
    {
        float4 prev_r9 = r9;
        r10 = SampleDifference(float2(prev_r9.z, prev_r9.w));
    }
    {
        float4 prev_r1 = r1;
        r11.x = (-(prev_r1.x) + c2.w);
        r11.y = (-(prev_r1.y) + c2.y);
        r11.z = (-(prev_r1.x) + c2.w);
        r11.w = (-(prev_r1.y) + c2.w);
    }
    {
        float4 prev_r11 = r11;
        r1.w = dot(float2(prev_r11.x, prev_r11.y), float2(prev_r11.x, prev_r11.y)) + c8.w;
    }
    {
        float4 prev_r11 = r11;
        r3.z = dot(float2(prev_r11.z, prev_r11.w), float2(prev_r11.z, prev_r11.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r3 = r3;
        r3.z = (prev_r6.x * -(prev_r3.z));
    }
    {
        float4 prev_r3 = r3;
        r3.z = (prev_r3.z * c2.z);
    }
    {
        float4 prev_r3 = r3;
        r3.z = exp2(prev_r3.z);
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r10 = r10;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r10.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r10.w));
    }
    {
        float4 prev_r5 = r5;
        r3.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.w = rcp(prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r5.z = (prev_r1.w * prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.w, prev_r1.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r10 = r10;
        float4 prev_r3 = r3;
        r3.x = mad(prev_r5.z, prev_r10.x, prev_r3.x);
        r3.y = mad(prev_r5.z, prev_r10.y, prev_r3.y);
    }
    {
        float4 prev_r0 = r0;
        r10.x = (prev_r0.z + c9.z);
        r10.y = (prev_r0.w + c9.z);
        r10.z = (prev_r0.z + c9.z);
        r10.w = (prev_r0.w + c9.w);
    }
    {
        float4 prev_r10 = r10;
        r10.x = (prev_r10.x * c0.z);
        r10.y = (prev_r10.y * c0.w);
        r10.z = (prev_r10.z * c0.z);
        r10.w = (prev_r10.w * c0.w);
    }
    {
        float4 prev_r10 = r10;
        r11 = SampleDifference(float2(prev_r10.x, prev_r10.y));
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r11 = r11;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r11.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r11.w));
    }
    {
        float4 prev_r5 = r5;
        r1.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r1 = r1;
        r1.w = rcp(prev_r1.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.w * prev_r3.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r1 = r1;
        r1.z = mad(prev_r3.z, prev_r1.w, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r11 = r11;
        r3.x = mad(prev_r3.w, prev_r11.x, prev_r3.x);
        r3.y = mad(prev_r3.w, prev_r11.y, prev_r3.y);
    }
    {
        float4 prev_r10 = r10;
        r11 = SampleDifference(float2(prev_r10.z, prev_r10.w));
    }
    {
        float4 prev_r10 = r10;
        r10 = SourceTex.SampleLevel(SourceSampler, float2(prev_r10.x, prev_r10.y), 0.0);
    }
    {
        float4 prev_r1 = r1;
        r12.x = (-(prev_r1.x) + c3.y);
        r12.y = (-(prev_r1.y) + c3.z);
        r12.z = (-(prev_r1.x) + c3.z);
        r12.w = (-(prev_r1.y) + c3.x);
    }
    {
        float4 prev_r12 = r12;
        r1.w = dot(float2(prev_r12.x, prev_r12.y), float2(prev_r12.x, prev_r12.y)) + c8.w;
    }
    {
        float4 prev_r12 = r12;
        r3.z = dot(float2(prev_r12.z, prev_r12.w), float2(prev_r12.z, prev_r12.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r3 = r3;
        r3.z = (prev_r6.x * -(prev_r3.z));
    }
    {
        float4 prev_r3 = r3;
        r3.z = (prev_r3.z * c2.z);
    }
    {
        float4 prev_r3 = r3;
        r3.z = exp2(prev_r3.z);
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r11 = r11;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r11.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r11.w));
    }
    {
        float4 prev_r5 = r5;
        r3.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.w = rcp(prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r5.z = (prev_r1.w * prev_r3.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.z = mad(prev_r1.w, prev_r3.w, prev_r1.z);
    }
    {
        float4 prev_r5 = r5;
        float4 prev_r11 = r11;
        float4 prev_r3 = r3;
        r3.x = mad(prev_r5.z, prev_r11.x, prev_r3.x);
        r3.y = mad(prev_r5.z, prev_r11.y, prev_r3.y);
    }
    {
        float4 prev_r0 = r0;
        r11.x = (prev_r0.z + c9.w);
        r11.y = (prev_r0.w + c9.x);
        r11.z = (prev_r0.z + c9.w);
        r11.w = (prev_r0.w + c9.y);
    }
    {
        float4 prev_r0 = r0;
        r0.x = (prev_r0.x + c9.w);
        r0.y = (prev_r0.y + c9.z);
        r0.z = (prev_r0.z + c9.w);
        r0.w = (prev_r0.w + c9.w);
    }
    {
        float4 prev_r0 = r0;
        r0.x = (prev_r0.x * c0.z);
        r0.y = (prev_r0.y * c0.w);
        r0.z = (prev_r0.z * c0.z);
        r0.w = (prev_r0.w * c0.w);
    }
    {
        float4 prev_r11 = r11;
        r11.x = (prev_r11.x * c0.z);
        r11.y = (prev_r11.y * c0.w);
        r11.z = (prev_r11.z * c0.z);
        r11.w = (prev_r11.w * c0.w);
    }
    {
        float4 prev_r11 = r11;
        r12 = SampleDifference(float2(prev_r11.x, prev_r11.y));
    }
    {
        float4 prev_r11 = r11;
        r11 = SampleDifference(float2(prev_r11.z, prev_r11.w));
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r12 = r12;
        r5.z = mad(prev_r7.y, c8.z, -(prev_r12.z));
        r5.w = mad(prev_r7.z, c8.z, -(prev_r12.w));
    }
    {
        float4 prev_r5 = r5;
        r1.w = dot(float2(prev_r5.z, prev_r5.w), float2(prev_r5.z, prev_r5.w)) + c2.w;
    }
    {
        float4 prev_r1 = r1;
        r1.w = rcp(prev_r1.w);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.w * prev_r3.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r1 = r1;
        r1.z = mad(prev_r3.z, prev_r1.w, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r12 = r12;
        r3.x = mad(prev_r3.w, prev_r12.x, prev_r3.x);
        r3.y = mad(prev_r3.w, prev_r12.y, prev_r3.y);
    }
    {
        float4 prev_r1 = r1;
        r12.x = (-(prev_r1.x) + c3.z);
        r12.y = (-(prev_r1.y) + c3.w);
        r12.z = (-(prev_r1.x) + c3.z);
        r12.w = (-(prev_r1.y) + c3.y);
    }
    {
        float4 prev_r1 = r1;
        r1.x = (-(prev_r1.x) + c3.z);
        r1.y = (-(prev_r1.y) + c3.z);
    }
    {
        float4 prev_r1 = r1;
        r1.x = dot(float2(prev_r1.x, prev_r1.y), float2(prev_r1.x, prev_r1.y)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.x = (prev_r6.x * -(prev_r1.x));
    }
    {
        float4 prev_r1 = r1;
        r1.x = (prev_r1.x * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.x = exp2(prev_r1.x);
    }
    {
        float4 prev_r12 = r12;
        r1.y = dot(float2(prev_r12.x, prev_r12.y), float2(prev_r12.x, prev_r12.y)) + c8.w;
    }
    {
        float4 prev_r12 = r12;
        r1.w = dot(float2(prev_r12.z, prev_r12.w), float2(prev_r12.z, prev_r12.w)) + c8.w;
    }
    {
        float4 prev_r6 = r6;
        float4 prev_r1 = r1;
        r1.y = (prev_r6.x * -(prev_r1.y));
        r1.w = (prev_r6.x * -(prev_r1.w));
    }
    {
        float4 prev_r1 = r1;
        r1.y = (prev_r1.y * c2.z);
        r1.w = (prev_r1.w * c2.z);
    }
    {
        float4 prev_r1 = r1;
        r1.y = exp2(prev_r1.y);
    }
    {
        float4 prev_r1 = r1;
        r1.w = exp2(prev_r1.w);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r11 = r11;
        r3.z = mad(prev_r7.y, c8.z, -(prev_r11.z));
        r3.w = mad(prev_r7.z, c8.z, -(prev_r11.w));
    }
    {
        float4 prev_r3 = r3;
        r3.z = dot(float2(prev_r3.z, prev_r3.w), float2(prev_r3.z, prev_r3.w)) + c2.w;
    }
    {
        float4 prev_r3 = r3;
        r3.z = rcp(prev_r3.z);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r3.w = (prev_r1.y * prev_r3.z);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r3 = r3;
        r1.y = mad(prev_r1.y, prev_r3.z, prev_r1.z);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r11 = r11;
        r3.x = mad(prev_r3.w, prev_r11.x, prev_r3.x);
        r3.y = mad(prev_r3.w, prev_r11.y, prev_r3.y);
    }
    {
        float4 prev_r0 = r0;
        r6 = SampleDifference(float2(prev_r0.x, prev_r0.y));
    }
    {
        float4 prev_r0 = r0;
        r0 = SampleDifference(float2(prev_r0.z, prev_r0.w));
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r6 = r6;
        r3.z = mad(prev_r7.y, c8.z, -(prev_r6.z));
        r3.w = mad(prev_r7.z, c8.z, -(prev_r6.w));
    }
    {
        float4 prev_r3 = r3;
        r1.z = dot(float2(prev_r3.z, prev_r3.w), float2(prev_r3.z, prev_r3.w)) + c2.w;
    }
    {
        float4 prev_r1 = r1;
        r1.z = rcp(prev_r1.z);
    }
    {
        float4 prev_r1 = r1;
        r3.z = (prev_r1.z * prev_r1.w);
    }
    {
        float4 prev_r1 = r1;
        r1.y = mad(prev_r1.w, prev_r1.z, prev_r1.y);
    }
    {
        float4 prev_r3 = r3;
        float4 prev_r6 = r6;
        r1.z = mad(prev_r3.z, prev_r6.x, prev_r3.x);
        r1.w = mad(prev_r3.z, prev_r6.y, prev_r3.y);
    }
    {
        float4 prev_r7 = r7;
        float4 prev_r0 = r0;
        r0.z = mad(prev_r7.y, c8.z, -(prev_r0.z));
        r0.w = mad(prev_r7.z, c8.z, -(prev_r0.w));
    }
    {
        float4 prev_r0 = r0;
        r0.z = dot(float2(prev_r0.z, prev_r0.w), float2(prev_r0.z, prev_r0.w)) + c2.w;
    }
    {
        float4 prev_r0 = r0;
        r0.z = rcp(prev_r0.z);
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r0.w = (prev_r0.z * prev_r1.x);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r0 = r0;
        r0.z = mad(prev_r1.x, prev_r0.z, prev_r1.y);
    }
    {
        float4 prev_r0 = r0;
        r0.z = rcp(prev_r0.z);
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r0.x = mad(prev_r0.w, prev_r0.x, prev_r1.z);
        r0.y = mad(prev_r0.w, prev_r0.y, prev_r1.w);
    }
    {
        float4 prev_r0 = r0;
        r0.x = (prev_r0.x * c1.x);
        r0.y = (prev_r0.y * c1.x);
    }
    {
        float4 prev_r0 = r0;
        r0.x = (prev_r0.z * prev_r0.x);
        r0.y = (prev_r0.z * prev_r0.y);
    }
    {
        float4 prev_r2 = r2;
        r1 = SourceTex.SampleLevel(SourceSampler, float2(prev_r2.z, prev_r2.w), 0.0);
    }
    {
        float4 prev_r2 = r2;
        r2 = SourceTex.SampleLevel(SourceSampler, float2(prev_r2.x, prev_r2.y), 0.0);
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r2 = r2;
        r0.z = (prev_r1.x + -(prev_r2.x));
        r0.w = (prev_r1.y + -(prev_r2.y));
    }
    {
        float4 prev_r4 = r4;
        r1 = SourceTex.SampleLevel(SourceSampler, float2(prev_r4.x, prev_r4.y), 0.0);
    }
    {
        float4 prev_r4 = r4;
        r3 = SourceTex.SampleLevel(SourceSampler, float2(prev_r4.z, prev_r4.w), 0.0);
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r3 = r3;
        r1.z = (-(prev_r2.x) + prev_r3.x);
        r1.w = (-(prev_r2.y) + prev_r3.y);
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r1 = r1;
        r1.x = (-(prev_r2.x) + prev_r1.x);
        r1.y = (-(prev_r2.y) + prev_r1.y);
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r2.z = max(abs(prev_r0.z), abs(prev_r1.x));
        r2.w = max(abs(prev_r0.w), abs(prev_r1.y));
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r1 = r1;
        r0.z = max(prev_r2.z, abs(prev_r1.z));
        r0.w = max(prev_r2.w, abs(prev_r1.w));
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r5 = r5;
        r1.x = (-(prev_r2.x) + prev_r5.x);
        r1.y = (-(prev_r2.y) + prev_r5.y);
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r1 = r1;
        r2.z = max(prev_r0.z, abs(prev_r1.x));
        r2.w = max(prev_r0.w, abs(prev_r1.y));
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r8 = r8;
        r0.z = (-(prev_r2.x) + prev_r8.x);
        r0.w = (-(prev_r2.y) + prev_r8.y);
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r0 = r0;
        r1.x = max(prev_r2.z, abs(prev_r0.z));
        r1.y = max(prev_r2.w, abs(prev_r0.w));
    }
    {
        float4 prev_r9 = r9;
        r3 = SourceTex.SampleLevel(SourceSampler, float2(prev_r9.x, prev_r9.y), 0.0);
    }
    {
        float4 prev_r9 = r9;
        r4 = SourceTex.SampleLevel(SourceSampler, float2(prev_r9.z, prev_r9.w), 0.0);
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r4 = r4;
        r0.z = (-(prev_r2.x) + prev_r4.x);
        r0.w = (-(prev_r2.y) + prev_r4.y);
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r3 = r3;
        r1.z = (-(prev_r2.x) + prev_r3.x);
        r1.w = (-(prev_r2.y) + prev_r3.y);
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r10 = r10;
        r2.x = (-(prev_r2.x) + prev_r10.x);
        r2.y = (-(prev_r2.y) + prev_r10.y);
    }
    {
        float4 prev_r1 = r1;
        r2.z = max(prev_r1.x, abs(prev_r1.z));
        r2.w = max(prev_r1.y, abs(prev_r1.w));
    }
    {
        float4 prev_r2 = r2;
        float4 prev_r0 = r0;
        r1.x = max(prev_r2.z, abs(prev_r0.z));
        r1.y = max(prev_r2.w, abs(prev_r0.w));
    }
    {
        float4 prev_r1 = r1;
        float4 prev_r2 = r2;
        r0.z = max(prev_r1.x, abs(prev_r2.x));
        r0.w = max(prev_r1.y, abs(prev_r2.y));
    }
    {
        float4 prev_r0 = r0;
        r0.z = saturate((prev_r0.z * c10.x));
        r0.w = saturate((prev_r0.w * c10.x));
    }
    {
        float4 prev_r0 = r0;
        float4 prev_r7 = r7;
        r7.y = mad(prev_r0.x, -(prev_r0.z), prev_r7.y);
        r7.z = mad(prev_r0.y, -(prev_r0.w), prev_r7.z);
    }
    {
        float4 prev_r7 = r7;
        r0.x = dot(float3(prev_r7.x, prev_r7.y, prev_r7.z), float3(c5.x, c5.y, c5.z));
    }
    {
        float4 prev_r7 = r7;
        r0.y = dot(float3(prev_r7.x, prev_r7.y, prev_r7.z), float3(c6.x, c6.y, c6.z));
    }
    {
        float4 prev_r7 = r7;
        r0.z = dot(float3(prev_r7.x, prev_r7.y, prev_r7.z), float3(c7.x, c7.y, c7.z));
    }
    {
        float4 prev_r0 = r0;
        oC0.x = (prev_r0.x + c4.x);
        oC0.y = (prev_r0.y + c4.y);
        oC0.z = (prev_r0.z + c4.z);
    }
        oC0.w = c8.w;
    return oC0;
}

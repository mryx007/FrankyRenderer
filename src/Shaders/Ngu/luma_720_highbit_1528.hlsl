// Generated from PS_1528_f0e4bb92.bin. Do not edit by hand.
Texture2D<float4> NN1 : register(t0);
Texture2D<float4> NN2 : register(t1);

cbuffer NguHighBit1528 : register(b0)
{
    float4 weights[32];
    float4 bias;
    float4 pixSize;
    float4 dstSize;
};

struct VSOut { float4 position : SV_Position; };

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(corner * float2(2.0, -2.0) +
        float2(-1.0, 1.0), 0.0, 1.0);
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

float4 PSMain(VSOut input) : SV_Target0
{
    float4 v0 = float4(input.position.xy / dstSize.xy, 0.0, 1.0);
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
    float4 oC0 = 0.0;
    const float4 c0 = weights[0];
    const float4 c1 = weights[1];
    const float4 c2 = weights[2];
    const float4 c3 = weights[3];
    const float4 c4 = weights[4];
    const float4 c5 = weights[5];
    const float4 c6 = weights[6];
    const float4 c7 = weights[7];
    const float4 c8 = weights[8];
    const float4 c9 = weights[9];
    const float4 c10 = weights[10];
    const float4 c11 = weights[11];
    const float4 c12 = weights[12];
    const float4 c13 = weights[13];
    const float4 c14 = weights[14];
    const float4 c15 = weights[15];
    const float4 c16 = weights[16];
    const float4 c17 = weights[17];
    const float4 c18 = weights[18];
    const float4 c19 = weights[19];
    const float4 c20 = weights[20];
    const float4 c21 = weights[21];
    const float4 c22 = weights[22];
    const float4 c23 = weights[23];
    const float4 c24 = weights[24];
    const float4 c25 = weights[25];
    const float4 c26 = weights[26];
    const float4 c27 = weights[27];
    const float4 c28 = weights[28];
    const float4 c29 = weights[29];
    const float4 c30 = weights[30];
    const float4 c31 = weights[31];
    const float4 c32 = bias;
    const float4 c33 = pixSize;
    const float4 c34 = dstSize;
    const float4 c35 = float4(-1, 1, 0, 0);

        r0.x = c35.x;
        r0.y = c35.y;
        r0.z = c35.z;
    {
        const float4 prev_r0 = r0;
        r0.x = mad(v0.x, c34.x, prev_r0.x);
        r0.w = mad(v0.y, c34.y, prev_r0.x);
    }
        r1.x = (r0.x * c33.x);
        r1.y = (r0.w * c33.y);
    {
        const float4 prev_r0 = r0;
        r0.x = mad(prev_r0.x, c33.x, c33.x);
        r0.w = mad(prev_r0.w, c33.y, c33.y);
    }
        r2 = Fetch(NN1, float2(r1.x, r1.y));
        r1.z = dot(float4(r2.x, r2.y, r2.z, r2.w), float4(c10.x, c10.y, c10.z, c10.w));
        r3 = Fetch(NN2, float2(r1.x, r1.y));
        r4.x = mad(c33.x, r0.y, r1.x);
        r4.y = mad(c33.y, r0.z, r1.y);
        r4.z = mad(c33.x, r0.z, r1.x);
        r4.w = mad(c33.y, r0.y, r1.y);
        r0.y = dot(float4(r3.x, r3.y, r3.z, r3.w), float4(c26.x, c26.y, c26.z, c26.w));
    {
        const float4 prev_r0 = r0;
        r0.y = (prev_r0.y + r1.z);
    }
        r1 = Fetch(NN1, float2(r4.x, r4.y));
        r0.z = dot(float4(r1.x, r1.y, r1.z, r1.w), float4(c8.x, c8.y, c8.z, c8.w));
    {
        const float4 prev_r0 = r0;
        r0.y = (prev_r0.z + prev_r0.y);
    }
        r5 = Fetch(NN2, float2(r4.x, r4.y));
        r0.z = dot(float4(r5.x, r5.y, r5.z, r5.w), float4(c24.x, c24.y, c24.z, c24.w));
    {
        const float4 prev_r0 = r0;
        r0.y = (prev_r0.z + prev_r0.y);
    }
        r6 = Fetch(NN1, float2(r4.z, r4.w));
    {
        const float4 prev_r4 = r4;
        r4 = Fetch(NN2, float2(prev_r4.z, prev_r4.w));
    }
        r0.z = dot(float4(r6.x, r6.y, r6.z, r6.w), float4(c2.x, c2.y, c2.z, c2.w));
    {
        const float4 prev_r0 = r0;
        r0.y = (prev_r0.z + prev_r0.y);
    }
        r0.z = dot(float4(r4.x, r4.y, r4.z, r4.w), float4(c18.x, c18.y, c18.z, c18.w));
    {
        const float4 prev_r0 = r0;
        r0.y = (prev_r0.z + prev_r0.y);
    }
        r7 = Fetch(NN1, float2(r0.x, r0.w));
        r8 = Fetch(NN2, float2(r0.x, r0.w));
        r0.x = dot(float4(r7.x, r7.y, r7.z, r7.w), float4(c0.x, c0.y, c0.z, c0.w));
    {
        const float4 prev_r0 = r0;
        r0.x = (prev_r0.x + prev_r0.y);
    }
        r0.y = dot(float4(r8.x, r8.y, r8.z, r8.w), float4(c16.x, c16.y, c16.z, c16.w));
    {
        const float4 prev_r0 = r0;
        r0.x = (prev_r0.y + prev_r0.x);
    }
        r9.x = dot(float4(r1.x, r1.y, r1.z, r1.w), float4(c9.x, c9.y, c9.z, c9.w));
        r9.y = dot(float4(r2.x, r2.y, r2.z, r2.w), float4(c11.x, c11.y, c11.z, c11.w));
        r9.z = dot(float4(r3.x, r3.y, r3.z, r3.w), float4(c27.x, c27.y, c27.z, c27.w));
    {
        const float4 prev_r9 = r9;
        r9.y = (prev_r9.z + prev_r9.y);
    }
    {
        const float4 prev_r9 = r9;
        r9.x = (prev_r9.x + prev_r9.y);
    }
        r9.y = dot(float4(r5.x, r5.y, r5.z, r5.w), float4(c25.x, c25.y, c25.z, c25.w));
    {
        const float4 prev_r9 = r9;
        r9.x = (prev_r9.y + prev_r9.x);
    }
        r9.y = dot(float4(r6.x, r6.y, r6.z, r6.w), float4(c3.x, c3.y, c3.z, c3.w));
    {
        const float4 prev_r9 = r9;
        r9.x = (prev_r9.y + prev_r9.x);
    }
        r9.y = dot(float4(r4.x, r4.y, r4.z, r4.w), float4(c19.x, c19.y, c19.z, c19.w));
    {
        const float4 prev_r9 = r9;
        r9.x = (prev_r9.y + prev_r9.x);
    }
        r9.y = dot(float4(r7.x, r7.y, r7.z, r7.w), float4(c1.x, c1.y, c1.z, c1.w));
    {
        const float4 prev_r9 = r9;
        r9.x = (prev_r9.y + prev_r9.x);
    }
        r9.y = dot(float4(r8.x, r8.y, r8.z, r8.w), float4(c17.x, c17.y, c17.z, c17.w));
        r0.y = (r9.y + r9.x);
        r9.x = dot(float4(r2.x, r2.y, r2.z, r2.w), float4(c14.x, c14.y, c14.z, c14.w));
    {
        const float4 prev_r2 = r2;
        r2.x = dot(float4(prev_r2.x, prev_r2.y, prev_r2.z, prev_r2.w), float4(c15.x, c15.y, c15.z, c15.w));
    }
        r2.y = dot(float4(r3.x, r3.y, r3.z, r3.w), float4(c30.x, c30.y, c30.z, c30.w));
        r2.z = dot(float4(r3.x, r3.y, r3.z, r3.w), float4(c31.x, c31.y, c31.z, c31.w));
    {
        const float4 prev_r2 = r2;
        r2.x = (prev_r2.z + prev_r2.x);
    }
    {
        const float4 prev_r2 = r2;
        r2.y = (prev_r2.y + r9.x);
    }
        r2.z = dot(float4(r1.x, r1.y, r1.z, r1.w), float4(c12.x, c12.y, c12.z, c12.w));
    {
        const float4 prev_r1 = r1;
        r1.x = dot(float4(prev_r1.x, prev_r1.y, prev_r1.z, prev_r1.w), float4(c13.x, c13.y, c13.z, c13.w));
    }
    {
        const float4 prev_r1 = r1;
        r1.x = (prev_r1.x + r2.x);
    }
        r1.y = (r2.z + r2.y);
        r1.z = dot(float4(r5.x, r5.y, r5.z, r5.w), float4(c28.x, c28.y, c28.z, c28.w));
        r1.w = dot(float4(r5.x, r5.y, r5.z, r5.w), float4(c29.x, c29.y, c29.z, c29.w));
    {
        const float4 prev_r1 = r1;
        r1.x = (prev_r1.w + prev_r1.x);
        r1.y = (prev_r1.z + prev_r1.y);
    }
        r1.z = dot(float4(r6.x, r6.y, r6.z, r6.w), float4(c6.x, c6.y, c6.z, c6.w));
        r1.w = dot(float4(r6.x, r6.y, r6.z, r6.w), float4(c7.x, c7.y, c7.z, c7.w));
    {
        const float4 prev_r1 = r1;
        r1.x = (prev_r1.w + prev_r1.x);
        r1.y = (prev_r1.z + prev_r1.y);
    }
        r1.z = dot(float4(r4.x, r4.y, r4.z, r4.w), float4(c22.x, c22.y, c22.z, c22.w));
        r1.w = dot(float4(r4.x, r4.y, r4.z, r4.w), float4(c23.x, c23.y, c23.z, c23.w));
    {
        const float4 prev_r1 = r1;
        r1.x = (prev_r1.w + prev_r1.x);
        r1.y = (prev_r1.z + prev_r1.y);
    }
        r1.z = dot(float4(r7.x, r7.y, r7.z, r7.w), float4(c4.x, c4.y, c4.z, c4.w));
        r1.w = dot(float4(r7.x, r7.y, r7.z, r7.w), float4(c5.x, c5.y, c5.z, c5.w));
    {
        const float4 prev_r1 = r1;
        r1.x = (prev_r1.w + prev_r1.x);
        r1.y = (prev_r1.z + prev_r1.y);
    }
        r1.z = dot(float4(r8.x, r8.y, r8.z, r8.w), float4(c20.x, c20.y, c20.z, c20.w));
        r1.w = dot(float4(r8.x, r8.y, r8.z, r8.w), float4(c21.x, c21.y, c21.z, c21.w));
        r0.z = (r1.z + r1.y);
        r0.w = (r1.w + r1.x);
        oC0.x = (r0.x + c32.x);
        oC0.y = (r0.y + c32.x);
        oC0.z = (r0.z + c32.x);
        oC0.w = (r0.w + c32.x);
    return oC0;
}

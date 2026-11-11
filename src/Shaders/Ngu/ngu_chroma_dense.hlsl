// Target-derived native NGU VeryHigh chroma network stage.
// Evidence: OpenMadVR E-085; generated from the pass-equivalent GLSL oracle.
Texture2D<float4> Feature0 : register(t0);
Texture2D<float4> Feature1 : register(t1);
Texture2D<float4> Feature2 : register(t2);
Texture2D<float4> Feature3 : register(t3);

cbuffer NguChromaParams : register(b0)
{
    float4 U[67];
};

static const float2 RasterSize = float2(642.0, 362.0);

struct VSOut { float4 position : SV_Position; };

VSOut VSMain(uint vertex : SV_VertexID)
{
    VSOut output;
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    output.position =
        float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float4 Fetch0(float2 position)
{
    int width, height;
    Feature0.GetDimensions(width, height);
    int2 pixel = int2(floor(position * float2(width, height)));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature0.Load(int3(pixel, 0));
}

float4 Fetch1(float2 position)
{
    int width, height;
    Feature1.GetDimensions(width, height);
    int2 pixel = int2(floor(position * float2(width, height)));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature1.Load(int3(pixel, 0));
}

float4 Fetch2(float2 position)
{
    int width, height;
    Feature2.GetDimensions(width, height);
    int2 pixel = int2(floor(position * float2(width, height)));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature2.Load(int3(pixel, 0));
}

float4 Fetch3(float2 position)
{
    int width, height;
    Feature3.GetDimensions(width, height);
    int2 pixel = int2(floor(position * float2(width, height)));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return Feature3.Load(int3(pixel, 0));
}

float4 Fetch0(float4 position) { return Fetch0(position.xy); }
float4 Fetch1(float4 position) { return Fetch1(position.xy); }
float4 Fetch2(float4 position) { return Fetch2(position.xy); }
float4 Fetch3(float4 position) { return Fetch3(position.xy); }

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const float4 HOOKED_pos = float4(position.xy / RasterSize, 0.0, 1.0);

    float4 DL67 = float4(-1,1,0,0);
  float4 r0_init = float4(0,0,0,0);
  float4 r1_init = float4(0,0,0,0);
  float4 r2_init = float4(0,0,0,0);
  float4 r3_init = float4(0,0,0,0);
  float4 r4_init = float4(0,0,0,0);
  float4 r5_init = float4(0,0,0,0);
  float4 r6_init = float4(0,0,0,0);
  float4 r7_init = float4(0,0,0,0);
  float4 r8_init = float4(0,0,0,0);
  float4 r9_init = float4(0,0,0,0);
  float4 r10_init = float4(0,0,0,0);
  float4 r11_init = float4(0,0,0,0);
  float4 r12_init = float4(0,0,0,0);
  float4 r13_init = float4(0,0,0,0);
  float4 r14_init = float4(0,0,0,0);
  float4 r15_init = float4(0,0,0,0);
  float4 r16_init = float4(0,0,0,0);
  float4 r17_init = float4(0,0,0,0);
  float4 r18_init = float4(0,0,0,0);
  float4 r19_init = float4(0,0,0,0);
  float4 r20_init = float4(0,0,0,0);
  float4 r21_init = float4(0,0,0,0);
  float4 r22_init = float4(0,0,0,0);
  float4 r23_init = float4(0,0,0,0);
  float4 r24_init = float4(0,0,0,0);
  float4 r25_init = float4(0,0,0,0);
  float4 r26_init = float4(0,0,0,0);
  float4 r27_init = float4(0,0,0,0);
  float4 r28_init = float4(0,0,0,0);
  float4 r29_init = float4(0,0,0,0);
  float4 r30_init = float4(0,0,0,0);
  float4 r31_init = float4(0,0,0,0);
  precise float4 e1 = DL67;
  precise float4 w1 = float4(e1.x, e1.y, e1.z, r0_init.w);
  precise float4 e2 = (HOOKED_pos.xyzy * U[66].xyzy + w1.xxxx);
  precise float4 w2 = float4(e2.x, w1.y, w1.z, e2.w);
  precise float4 e3 = (w2.xwzw * U[65]);
  precise float4 w3 = float4(e3.x, e3.y, r1_init.z, r1_init.w);
  precise float4 e4 = (w2 * U[65].xyzy + U[65].xyzy);
  precise float4 w4 = float4(e4.x, w2.y, w2.z, e4.w);
  precise float4 w5 = Fetch0(w3);
  precise float4 e6 = dot(w5, U[10]);
  precise float4 w6 = float4(w3.x, w3.y, e6.z, w3.w);
  precise float4 w7 = Fetch1(w6);
  precise float4 e8 = dot(w7, U[26]);
  precise float4 w8 = float4(w6.x, w6.y, w6.z, e8.w);
  precise float4 e9 = (w8.wwww + w8.zzzz);
  precise float4 w9 = float4(w8.x, w8.y, e9.z, w8.w);
  precise float4 w10 = Fetch2(w9);
  precise float4 e11 = dot(w10, U[42]);
  precise float4 w11 = float4(w9.x, w9.y, w9.z, e11.w);
  precise float4 e12 = (w11.wwww + w11.zzzz);
  precise float4 w12 = float4(w11.x, w11.y, e12.z, w11.w);
  precise float4 w13 = Fetch3(w12);
  precise float4 w14 = (U[65].xyxy * w4.yzzy + w12.xyxy);
  precise float4 e15 = dot(w13, U[58]);
  precise float4 w15 = float4(w4.x, e15.y, w4.z, w4.w);
  precise float4 e16 = (w15.yyyy + w12.zzzz);
  precise float4 w16 = float4(w15.x, e16.y, w15.z, w15.w);
  precise float4 w17 = Fetch0(w14);
  precise float4 e18 = dot(w17, U[8]);
  precise float4 w18 = float4(w16.x, w16.y, e18.z, w16.w);
  precise float4 e19 = (w18.zzzz + w18.yyyy);
  precise float4 w19 = float4(w18.x, e19.y, w18.z, w18.w);
  precise float4 w20 = Fetch1(w14);
  precise float4 e21 = dot(w20, U[24]);
  precise float4 w21 = float4(w19.x, w19.y, e21.z, w19.w);
  precise float4 e22 = (w21.zzzz + w21.yyyy);
  precise float4 w22 = float4(w21.x, e22.y, w21.z, w21.w);
  precise float4 w23 = Fetch2(w14);
  precise float4 e24 = dot(w23, U[40]);
  precise float4 w24 = float4(w22.x, w22.y, e24.z, w22.w);
  precise float4 e25 = (w24.zzzz + w24.yyyy);
  precise float4 w25 = float4(w24.x, e25.y, w24.z, w24.w);
  precise float4 w26 = Fetch3(w14);
  precise float4 e27 = dot(w26, U[56]);
  precise float4 w27 = float4(w25.x, w25.y, e27.z, w25.w);
  precise float4 e28 = (w27.zzzz + w27.yyyy);
  precise float4 w28 = float4(w27.x, e28.y, w27.z, w27.w);
  precise float4 w29 = Fetch0(w14.zwzw);
  precise float4 e30 = dot(w29, U[2]);
  precise float4 w30 = float4(w28.x, w28.y, e30.z, w28.w);
  precise float4 e31 = (w30.zzzz + w30.yyyy);
  precise float4 w31 = float4(w30.x, e31.y, w30.z, w30.w);
  precise float4 w32 = Fetch1(w14.zwzw);
  precise float4 e33 = dot(w32, U[18]);
  precise float4 w33 = float4(w31.x, w31.y, e33.z, w31.w);
  precise float4 e34 = (w33.zzzz + w33.yyyy);
  precise float4 w34 = float4(w33.x, e34.y, w33.z, w33.w);
  precise float4 w35 = Fetch2(w14.zwzw);
  precise float4 w36 = Fetch3(w14.zwzw);
  precise float4 e37 = dot(w35, U[34]);
  precise float4 w37 = float4(w34.x, w34.y, e37.z, w34.w);
  precise float4 e38 = (w37.zzzz + w37.yyyy);
  precise float4 w38 = float4(w37.x, e38.y, w37.z, w37.w);
  precise float4 e39 = dot(w36, U[50]);
  precise float4 w39 = float4(w38.x, w38.y, e39.z, w38.w);
  precise float4 e40 = (w39.zzzz + w39.yyyy);
  precise float4 w40 = float4(w39.x, e40.y, w39.z, w39.w);
  precise float4 w41 = Fetch0(w40.xwzw);
  precise float4 e42 = dot(w41, U[0]);
  precise float4 w42 = float4(w40.x, w40.y, e42.z, w40.w);
  precise float4 e43 = (w42.zzzz + w42.yyyy);
  precise float4 w43 = float4(w42.x, e43.y, w42.z, w42.w);
  precise float4 w44 = Fetch1(w43.xwzw);
  precise float4 e45 = dot(w44, U[16]);
  precise float4 w45 = float4(w43.x, w43.y, e45.z, w43.w);
  precise float4 e46 = (w45.zzzz + w45.yyyy);
  precise float4 w46 = float4(w45.x, e46.y, w45.z, w45.w);
  precise float4 w47 = Fetch2(w46.xwzw);
  precise float4 w48 = Fetch3(w46.xwzw);
  precise float4 e49 = dot(w47, U[32]);
  precise float4 w49 = float4(e49.x, w46.y, w46.z, w46.w);
  precise float4 e50 = (w49.xxxx + w49.yyyy);
  precise float4 w50 = float4(e50.x, w49.y, w49.z, w49.w);
  precise float4 e51 = dot(w48, U[48]);
  precise float4 w51 = float4(w50.x, e51.y, w50.z, w50.w);
  precise float4 e52 = (w51.yyyy + w51.xxxx);
  precise float4 w52 = float4(e52.x, w51.y, w51.z, w51.w);
  precise float4 e53 = dot(w17, U[9]);
  precise float4 w53 = float4(e53.x, r17_init.y, r17_init.z, r17_init.w);
  precise float4 e54 = dot(w5, U[11]);
  precise float4 w54 = float4(w53.x, e54.y, w53.z, w53.w);
  precise float4 e55 = dot(w7, U[27]);
  precise float4 w55 = float4(w54.x, w54.y, e55.z, w54.w);
  precise float4 e56 = (w55.zzzz + w55.yyyy);
  precise float4 w56 = float4(w55.x, e56.y, w55.z, w55.w);
  precise float4 e57 = dot(w10, U[43]);
  precise float4 w57 = float4(w56.x, w56.y, e57.z, w56.w);
  precise float4 e58 = (w57.zzzz + w57.yyyy);
  precise float4 w58 = float4(w57.x, e58.y, w57.z, w57.w);
  precise float4 e59 = dot(w13, U[59]);
  precise float4 w59 = float4(w58.x, w58.y, e59.z, w58.w);
  precise float4 e60 = (w59.zzzz + w59.yyyy);
  precise float4 w60 = float4(w59.x, e60.y, w59.z, w59.w);
  precise float4 e61 = (w60.xxxx + w60.yyyy);
  precise float4 w61 = float4(e61.x, w60.y, w60.z, w60.w);
  precise float4 e62 = dot(w20, U[25]);
  precise float4 w62 = float4(w61.x, e62.y, w61.z, w61.w);
  precise float4 e63 = (w62.yyyy + w62.xxxx);
  precise float4 w63 = float4(e63.x, w62.y, w62.z, w62.w);
  precise float4 e64 = dot(w23, U[41]);
  precise float4 w64 = float4(w63.x, e64.y, w63.z, w63.w);
  precise float4 e65 = (w64.yyyy + w64.xxxx);
  precise float4 w65 = float4(e65.x, w64.y, w64.z, w64.w);
  precise float4 e66 = dot(w26, U[57]);
  precise float4 w66 = float4(w65.x, e66.y, w65.z, w65.w);
  precise float4 e67 = (w66.yyyy + w66.xxxx);
  precise float4 w67 = float4(e67.x, w66.y, w66.z, w66.w);
  precise float4 e68 = dot(w29, U[3]);
  precise float4 w68 = float4(w67.x, e68.y, w67.z, w67.w);
  precise float4 e69 = (w68.yyyy + w68.xxxx);
  precise float4 w69 = float4(e69.x, w68.y, w68.z, w68.w);
  precise float4 e70 = dot(w32, U[19]);
  precise float4 w70 = float4(w69.x, e70.y, w69.z, w69.w);
  precise float4 e71 = (w70.yyyy + w70.xxxx);
  precise float4 w71 = float4(e71.x, w70.y, w70.z, w70.w);
  precise float4 e72 = dot(w35, U[35]);
  precise float4 w72 = float4(w71.x, e72.y, w71.z, w71.w);
  precise float4 e73 = (w72.yyyy + w72.xxxx);
  precise float4 w73 = float4(e73.x, w72.y, w72.z, w72.w);
  precise float4 e74 = dot(w36, U[51]);
  precise float4 w74 = float4(w73.x, e74.y, w73.z, w73.w);
  precise float4 e75 = (w74.yyyy + w74.xxxx);
  precise float4 w75 = float4(e75.x, w74.y, w74.z, w74.w);
  precise float4 e76 = dot(w41, U[1]);
  precise float4 w76 = float4(w75.x, e76.y, w75.z, w75.w);
  precise float4 e77 = (w76.yyyy + w76.xxxx);
  precise float4 w77 = float4(e77.x, w76.y, w76.z, w76.w);
  precise float4 e78 = dot(w44, U[17]);
  precise float4 w78 = float4(w77.x, e78.y, w77.z, w77.w);
  precise float4 e79 = (w78.yyyy + w78.xxxx);
  precise float4 w79 = float4(e79.x, w78.y, w78.z, w78.w);
  precise float4 e80 = dot(w47, U[33]);
  precise float4 w80 = float4(w79.x, e80.y, w79.z, w79.w);
  precise float4 e81 = (w80.yyyy + w80.xxxx);
  precise float4 w81 = float4(e81.x, w80.y, w80.z, w80.w);
  precise float4 e82 = dot(w48, U[49]);
  precise float4 w82 = float4(w81.x, e82.y, w81.z, w81.w);
  precise float4 e83 = (w82.yyyy + w82.xxxx);
  precise float4 w83 = float4(w52.x, e83.y, w52.z, w52.w);
  precise float4 e84 = dot(w5, U[14]);
  precise float4 w84 = float4(e84.x, w82.y, w82.z, w82.w);
  precise float4 e85 = dot(w5, U[15]);
  precise float4 w85 = float4(e85.x, w5.y, w5.z, w5.w);
  precise float4 e86 = dot(w7, U[30]);
  precise float4 w86 = float4(w85.x, e86.y, w85.z, w85.w);
  precise float4 e87 = dot(w7, U[31]);
  precise float4 w87 = float4(w86.x, w86.y, e87.z, w86.w);
  precise float4 e88 = (w87.zzzz + w87.xxxx);
  precise float4 w88 = float4(e88.x, w87.y, w87.z, w87.w);
  precise float4 e89 = (w88.yyyy + w84.xxxx);
  precise float4 w89 = float4(w88.x, e89.y, w88.z, w88.w);
  precise float4 e90 = dot(w10, U[46]);
  precise float4 w90 = float4(w89.x, w89.y, e90.z, w89.w);
  precise float4 e91 = dot(w10, U[47]);
  precise float4 w91 = float4(w90.x, w90.y, w90.z, e91.w);
  precise float4 e92 = (w91.wzzw + w91);
  precise float4 w92 = float4(e92.x, e92.y, w91.z, w91.w);
  precise float4 e93 = dot(w13, U[62]);
  precise float4 w93 = float4(w92.x, w92.y, e93.z, w92.w);
  precise float4 e94 = dot(w13, U[63]);
  precise float4 w94 = float4(w93.x, w93.y, w93.z, e94.w);
  precise float4 e95 = (w94.wzzw + w94);
  precise float4 w95 = float4(e95.x, e95.y, w94.z, w94.w);
  precise float4 e96 = dot(w17, U[12]);
  precise float4 w96 = float4(w95.x, w95.y, e96.z, w95.w);
  precise float4 e97 = dot(w17, U[13]);
  precise float4 w97 = float4(e97.x, w17.y, w17.z, w17.w);
  precise float4 e98 = (w97.xxxx + w96.xxxx);
  precise float4 w98 = float4(e98.x, w97.y, w97.z, w97.w);
  precise float4 e99 = (w96.zzzz + w96.yyyy);
  precise float4 w99 = float4(w98.x, e99.y, w98.z, w98.w);
  precise float4 e100 = dot(w20, U[28]);
  precise float4 w100 = float4(w99.x, w99.y, e100.z, w99.w);
  precise float4 e101 = dot(w20, U[29]);
  precise float4 w101 = float4(w100.x, w100.y, w100.z, e101.w);
  precise float4 e102 = (w101.wzzw + w101);
  precise float4 w102 = float4(e102.x, e102.y, w101.z, w101.w);
  precise float4 e103 = dot(w23, U[44]);
  precise float4 w103 = float4(w102.x, w102.y, e103.z, w102.w);
  precise float4 e104 = dot(w23, U[45]);
  precise float4 w104 = float4(w103.x, w103.y, w103.z, e104.w);
  precise float4 e105 = (w104.wzzw + w104);
  precise float4 w105 = float4(e105.x, e105.y, w104.z, w104.w);
  precise float4 e106 = dot(w26, U[60]);
  precise float4 w106 = float4(w105.x, w105.y, e106.z, w105.w);
  precise float4 e107 = dot(w26, U[61]);
  precise float4 w107 = float4(w106.x, w106.y, w106.z, e107.w);
  precise float4 e108 = (w107.wzzw + w107);
  precise float4 w108 = float4(e108.x, e108.y, w107.z, w107.w);
  precise float4 e109 = dot(w29, U[6]);
  precise float4 w109 = float4(w108.x, w108.y, e109.z, w108.w);
  precise float4 e110 = dot(w29, U[7]);
  precise float4 w110 = float4(w109.x, w109.y, w109.z, e110.w);
  precise float4 e111 = (w110.wzzw + w110);
  precise float4 w111 = float4(e111.x, e111.y, w110.z, w110.w);
  precise float4 e112 = dot(w32, U[22]);
  precise float4 w112 = float4(w111.x, w111.y, e112.z, w111.w);
  precise float4 e113 = dot(w32, U[23]);
  precise float4 w113 = float4(w112.x, w112.y, w112.z, e113.w);
  precise float4 e114 = (w113.wzzw + w113);
  precise float4 w114 = float4(e114.x, e114.y, w113.z, w113.w);
  precise float4 e115 = dot(w35, U[38]);
  precise float4 w115 = float4(w114.x, w114.y, e115.z, w114.w);
  precise float4 e116 = dot(w35, U[39]);
  precise float4 w116 = float4(w115.x, w115.y, w115.z, e116.w);
  precise float4 e117 = (w116.wzzw + w116);
  precise float4 w117 = float4(e117.x, e117.y, w116.z, w116.w);
  precise float4 e118 = dot(w36, U[54]);
  precise float4 w118 = float4(w117.x, w117.y, e118.z, w117.w);
  precise float4 e119 = dot(w36, U[55]);
  precise float4 w119 = float4(w118.x, w118.y, w118.z, e119.w);
  precise float4 e120 = (w119.wzzw + w119);
  precise float4 w120 = float4(e120.x, e120.y, w119.z, w119.w);
  precise float4 e121 = dot(w41, U[4]);
  precise float4 w121 = float4(w120.x, w120.y, e121.z, w120.w);
  precise float4 e122 = dot(w41, U[5]);
  precise float4 w122 = float4(w121.x, w121.y, w121.z, e122.w);
  precise float4 e123 = (w122.wzzw + w122);
  precise float4 w123 = float4(e123.x, e123.y, w122.z, w122.w);
  precise float4 e124 = dot(w44, U[20]);
  precise float4 w124 = float4(w123.x, w123.y, e124.z, w123.w);
  precise float4 e125 = dot(w44, U[21]);
  precise float4 w125 = float4(w124.x, w124.y, w124.z, e125.w);
  precise float4 e126 = (w125.wzzw + w125);
  precise float4 w126 = float4(e126.x, e126.y, w125.z, w125.w);
  precise float4 e127 = dot(w47, U[36]);
  precise float4 w127 = float4(w126.x, w126.y, e127.z, w126.w);
  precise float4 e128 = dot(w47, U[37]);
  precise float4 w128 = float4(w127.x, w127.y, w127.z, e128.w);
  precise float4 e129 = (w128.wzzw + w128);
  precise float4 w129 = float4(e129.x, e129.y, w128.z, w128.w);
  precise float4 e130 = dot(w48, U[52]);
  precise float4 w130 = float4(w129.x, w129.y, e130.z, w129.w);
  precise float4 e131 = dot(w48, U[53]);
  precise float4 w131 = float4(w130.x, w130.y, w130.z, e131.w);
  precise float4 e132 = (w131 + w131.xyyx);
  precise float4 w132 = float4(w83.x, w83.y, e132.z, e132.w);
  precise float4 w133 = (w132 + U[64].xxxx);
  return w133;
}

// Target-derived NGU VeryHigh chroma RealignSuperXbrChroma.
// Evidence: OpenMadVR E-092/E-124, PS_1516_11f763dd.
Texture2D<float4> Input : register(t0);
SamplerState PointSampler : register(s0);

cbuffer NguChromaRealignParams : register(b0)
{
    float4 U0;
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

float4 Fetch(float2 position)
{
    return Input.SampleLevel(PointSampler, position, 0);
}

float4 Evaluate(float4 v0)
{
float4 DL1 = float4(2,3,0.5,8);
    float4 DL2 = float4(0.5,0.064453125,-0.064453125,-0.135869563);
    float4 DL3 = float4(0.0244565215,0.611413062,0,0);
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
  float4 e1 = DL1;
  float4 w1 = float4(e1.x, e1.y, r0_init.z, r0_init.w);
  float4 e2 = (U0.xxxx * (-w1.xxxx) + v0.xxxx);
  float4 w2 = float4(e2.x, r1_init.y, r1_init.z, r1_init.w);
  float4 e3 = v0.yyyy;
  float4 w3 = float4(w2.x, e3.y, w2.z, e3.w);
  float4 w4 = Fetch(w3);
  float4 e5 = ((-U0.xxxx) + v0.xxxx);
  float4 w5 = float4(w3.x, w3.y, e5.z, w3.w);
  float4 w6 = Fetch(w5.zwzw);
  float4 e7 = (U0.xxxx + v0.xxxx);
  float4 w7 = float4(e7.x, r3_init.y, r3_init.z, r3_init.w);
  float4 e8 = v0.yyyy;
  float4 w8 = float4(w7.x, e8.y, w7.z, e8.w);
  float4 w9 = Fetch(w8);
  float4 w10 = Fetch(v0);
  float4 e11 = (w9.xyxy + w10.xyxy);
  float4 w11 = float4(w1.x, w1.y, e11.z, e11.w);
  float4 e12 = (w11 * (-DL1.zzzz) + w6.xyxy);
  float4 w12 = float4(w6.x, w6.y, e12.z, e12.w);
  float4 e13 = (U0.xxxx * w11.xxxx + v0.xxxx);
  float4 w13 = float4(w8.x, w8.y, e13.z, w8.w);
  float4 w14 = Fetch(w13.zwzw);
  float4 e15 = (w11.zyww * (-DL1.zzzz) + w14.xyyw);
  float4 w15 = float4(e15.x, w11.y, e15.z, w11.w);
  float4 e16 = (((w15 * w12.zyww)) < 0.0 ? 0.0 : min(((w15 * w12.zyww)), 1.0));
  float4 w16 = float4(e16.x, w15.y, e16.z, w15.w);
  float4 e17 = min(w9.xyxy, w10.xyxy);
  float4 w17 = float4(w12.x, w12.y, e17.z, e17.w);
  float4 e18 = ((((-w16.xyxz) + w17)) < 0.0 ? 0.0 : min((((-w16.xyxz) + w17)), 1.0));
  float4 w18 = float4(w17.x, w17.y, e18.z, e18.w);
  float4 e19 = max(w10.xyxy, w9.xyxy);
  float4 w19 = float4(w4.x, w4.y, e19.z, e19.w);
  float4 e20 = (((w16 + w19.zyww)) < 0.0 ? 0.0 : min(((w16 + w19.zyww)), 1.0));
  float4 w20 = float4(e20.x, w16.y, e20.z, w16.w);
  float4 w21 = (w18.zwzw + w20.xzxz);
  float4 w22 = (w21 * DL2.xxxx + DL2.yyzz);
  float4 e23 = max(w22.xyxy, w20.xyxz);
  float4 w23 = float4(w19.x, w19.y, e23.z, e23.w);
  float4 e24 = min(w18, w22);
  float4 w24 = float4(w14.x, w14.y, e24.z, e24.w);
  float4 e25 = ((-w18.zyww) + w20);
  float4 w25 = float4(e25.x, w20.y, e25.z, w20.w);
  float4 e26 = (((w25 * DL1.wwww)) < 0.0 ? 0.0 : min(((w25 * DL1.wwww)), 1.0));
  float4 w26 = float4(e26.x, w25.y, e26.z, w25.w);
  float4 e27 = ((-w23) + w23.xyxy);
  float4 w27 = float4(w18.x, w18.y, e27.z, e27.w);
  float4 e28 = (abs(w27) * w26.xyxz + w23);
  float4 w28 = float4(w27.x, w27.y, e28.z, e28.w);
  float4 e29 = min(w28, w23.xyxy);
  float4 w29 = float4(w9.x, w9.y, e29.z, e29.w);
  float4 e30 = (w24 + (-w29));
  float4 w30 = float4(w28.x, w28.y, e30.z, e30.w);
  float4 e31 = (abs(w30) * (-w26.xyxz) + w24);
  float4 w31 = float4(w30.x, w30.y, e31.z, e31.w);
  float4 e32 = max(w29.zwzw, w31.zwzw);
  float4 w32 = float4(e32.x, e32.y, w23.z, w23.w);
  float4 e33 = (w31.xyxy + (-w32));
  float4 w33 = float4(w31.x, w31.y, e33.z, e33.w);
  float4 e34 = (abs(w33) * w26.xyxz + w32);
  float4 w34 = float4(w33.x, w33.y, e34.z, e34.w);
  float4 e35 = min(w34, w34.xyxy);
  float4 w35 = float4(w29.x, w29.y, e35.z, e35.w);
  float4 e36 = (w24.zwzw + (-w35.zwzw));
  float4 w36 = float4(e36.x, e36.y, w34.z, w34.w);
  float4 e37 = (abs(w36) * (-w26.xzzw) + w24.zwzw);
  float4 w37 = float4(e37.x, e37.y, w36.z, w36.w);
  float4 e38 = max(w35, w37.xyxy);
  float4 w38 = float4(w10.x, w10.y, e38.z, e38.w);
  float4 e39 = (w38.zwzw * DL2.wwww);
  float4 w39 = float4(e39.x, e39.y, w37.z, w37.w);
  float4 e40 = (w32 * DL3.xxxx + w39);
  float4 w40 = float4(e40.x, e40.y, w39.z, w39.w);
  float4 e41 = ((-w32) + w38.xyxy);
  float4 w41 = float4(w40.x, w40.y, e41.z, e41.w);
  float4 e42 = (abs(w41) * w26.xyxz + w32);
  float4 w42 = float4(w41.x, w41.y, e42.z, e42.w);
  float4 e43 = min(w42.zwzw, w38);
  float4 w43 = float4(e43.x, e43.y, w32.z, w32.w);
  float4 e44 = ((-w43.xyxy) + w24);
  float4 w44 = float4(w42.x, w42.y, e44.z, e44.w);
  float4 e45 = (abs(w44) * (-w26.xyxz) + w24);
  float4 w45 = float4(w44.x, w44.y, e45.z, e45.w);
  float4 e46 = max(w43.xyxy, w45);
  float4 w46 = float4(w35.x, w35.y, e46.z, e46.w);
  float4 e47 = (w46.zwzw * DL3.yyyy + w45);
  float4 w47 = float4(e47.x, e47.y, w45.z, w45.w);
  float4 e48 = ((-w43) + w46.xyxy);
  float4 w48 = float4(w47.x, w47.y, e48.z, e48.w);
  float4 e49 = (abs(w48) * w26.xyxz + w43);
  float4 w49 = float4(w48.x, w48.y, e49.z, e49.w);
  float4 e50 = min(w49.zwzw, w46);
  float4 w50 = float4(e50.x, e50.y, w43.z, w43.w);
  float4 e51 = ((-w50.xyxy) + w24);
  float4 w51 = float4(w49.x, w49.y, e51.z, e51.w);
  float4 e52 = (abs(w51) * (-w26.xyxz) + w24);
  float4 w52 = float4(w51.x, w51.y, e52.z, e52.w);
  float4 e53 = max(w50, w52.zwzw);
  float4 w53 = float4(e53.x, e53.y, w46.z, w46.w);
  float4 e54 = (w53 * DL3.yyyy + w52);
  float4 w54 = float4(e54.x, e54.y, w52.z, w52.w);
  float4 e55 = ((-w50) + w24.xyxy);
  float4 w55 = float4(w54.x, w54.y, e55.z, e55.w);
  float4 e56 = (abs(w55) * w26.xyxz + w50);
  float4 w56 = float4(w55.x, w55.y, e56.z, e56.w);
  float4 e57 = min(w56.zwzw, w24);
  float4 w57 = float4(e57.x, e57.y, w50.z, w50.w);
  float4 e58 = ((-w57.xyxy) + w24);
  float4 w58 = float4(w56.x, w56.y, e58.z, e58.w);
  float4 e59 = (abs(w58) * (-w26.xyxz) + w24);
  float4 w59 = float4(w58.x, w58.y, e59.z, e59.w);
  float4 e60 = max(w57, w59.zwzw);
  float4 w60 = float4(e60.x, e60.y, w24.z, w24.w);
  float4 e61 = (w60 * DL2.wwww + w59);
  float4 w61 = float4(e61.x, e61.y, w59.z, w59.w);
  float4 e62 = (U0.xxxx * w26.yyyy + v0.xxxx);
  float4 w62 = float4(e62.x, w57.y, w57.z, w57.w);
  float4 e63 = v0.yyyy;
  float4 w63 = float4(w62.x, e63.y, w62.z, w62.w);
  float4 w64 = Fetch(w63);
  float4 e65 = ((-w63.xzzw) + w64.xxzy);
  float4 w65 = float4(w26.x, e65.y, w26.z, e65.w);
  float4 e66 = (abs(w65) * w65.xxzz + w63.xzzw);
  float4 w66 = float4(w65.x, e66.y, w65.z, e66.w);
  float4 e67 = min(w66.xyyw, w64.xyxy);
  float4 w67 = float4(w61.x, w61.y, e67.z, e67.w);
  float4 e68 = ((-w67.xzzw) + w60.xzzw);
  float4 w68 = float4(w66.x, e68.y, w66.z, e68.w);
  float4 e69 = (abs(w68.ywzw) * (-w68.xzzw) + w60.zwzw);
  float4 w69 = float4(e69.x, e69.y, w68.z, w68.w);
  float4 e70 = max(w67.zwzw, w69);
  float4 w70 = float4(e70.x, e70.y, w63.z, w63.w);
  float4 e71 = (w70 * DL3.xxxx + w67);
  float4 w71 = float4(e71.x, e71.y, float4(0,0,0,0).z, float4(0,0,0,0).w);
  float4 e72 = DL3.zzzz;
  float4 w72 = float4(w71.x, w71.y, e72.z, e72.w);
  return w72;
}

float Quantize16(float value)
{
    return floor(saturate(value) * 65535.0 + 0.5) / 65535.0;
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const float2 uv = position.xy / RasterSize;
    const float4 result = Evaluate(float4(uv, 0.0, 1.0));
    return float4(Quantize16(result.x), Quantize16(result.y), 0.0, 0.0);
}

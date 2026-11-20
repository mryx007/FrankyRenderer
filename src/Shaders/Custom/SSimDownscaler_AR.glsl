// SSimDownscaler_AR: SSimDownscaler with anti-ringing at hard edges. Where an
// edge is found, the detail SSIM restores is bounded to the source range, so
// it does not overshoot; fine lines and textures keep their detail.
// In FrankyRenderer ANTIRING follows Global Anti-Ringing (Scaling page);
// 0 gives the original SSimDownscaler. Based on igv's SSimDownscaler; the
// original license below applies.

// SSimDownscaler by igv: https://gist.github.com/igv/36508af3ffc84410fe39761d6969be10
// FrankyRenderer keeps igv's math and adds the THR (oversharp) and DEBUG
// parameters of the PlayKit variant it replaces. The PlayKit variant sized
// its L2 kernel in source pixels, which oversharpens strong downscaling.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3.0 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library.

//!PARAM ANTIRING
//!TYPE CONSTANT float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0

//!PARAM THR
//!TYPE float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0

//!PARAM DEBUG
//!TYPE DEFINE
//!MINIMUM 0
//!MAXIMUM 1
0

//!HOOK POSTKERNEL
//!BIND PREKERNEL
//!BIND HOOKED
//!SAVE L2
//!WIDTH PREKERNEL.w
//!WHEN PREKERNEL.w POSTKERNEL.w > PREKERNEL.h POSTKERNEL.h > *
//!COMPONENTS 3
//!DESC SSimDownscaler L2 pass 1

#define axis 1

#define offset      vec2(0,0)

#define MN(B,C,x)   (x < 1.0 ? ((2.-1.5*B-(C))*x + (-3.+2.*B+C))*x*x + (1.-(B)/3.) : (((-(B)/6.-(C))*x + (B+5.*C))*x + (-2.*B-8.*C))*x+((4./3.)*B+4.*C))
#define Kernel(x)   MN(.0, .5, abs(x))
#define taps        2.0

vec4 hook() {
    vec2 base = PREKERNEL_pt * (PREKERNEL_pos * input_size + tex_offset);

    float low  = ceil((PREKERNEL_pos - taps*POSTKERNEL_pt) * input_size - offset + tex_offset - 0.5)[axis];
    float high = floor((PREKERNEL_pos + taps*POSTKERNEL_pt) * input_size - offset + tex_offset - 0.5)[axis];

    float W = 0.0;
    vec4 avg = vec4(0);
    vec2 pos = base;

    for (float k = low; k <= high; k++) {
        pos[axis] = PREKERNEL_pt[axis] * (k - offset[axis] + 0.5);
        float rel = (pos[axis] - base[axis])*POSTKERNEL_size[axis];
        float w = Kernel(rel);

        vec4 tex = textureLod(PREKERNEL_raw, pos, 0.0) * PREKERNEL_mul;
        avg += w * tex * tex;
        W += w;
    }
    avg /= W;

    return avg;
}

//!HOOK POSTKERNEL
//!BIND L2
//!BIND HOOKED
//!SAVE L2
//!WHEN PREKERNEL.w POSTKERNEL.w > PREKERNEL.h POSTKERNEL.h > *
//!COMPONENTS 3
//!DESC SSimDownscaler L2 pass 2

#define axis 0

#define offset      vec2(0,0)

#define MN(B,C,x)   (x < 1.0 ? ((2.-1.5*B-(C))*x + (-3.+2.*B+C))*x*x + (1.-(B)/3.) : (((-(B)/6.-(C))*x + (B+5.*C))*x + (-2.*B-8.*C))*x+((4./3.)*B+4.*C))
#define Kernel(x)   MN(.0, .5, abs(x))
#define taps        2.0

vec4 hook() {
    float low  = ceil((L2_pos - taps*POSTKERNEL_pt) * L2_size - offset - 0.5)[axis];
    float high = floor((L2_pos + taps*POSTKERNEL_pt) * L2_size - offset - 0.5)[axis];

    float W = 0.0;
    vec4 avg = vec4(0);
    vec2 pos = L2_pos;

    for (float k = low; k <= high; k++) {
        pos[axis] = L2_pt[axis] * (k - offset[axis] + 0.5);
        float rel = (pos[axis] - L2_pos[axis])*POSTKERNEL_size[axis];
        float w = Kernel(rel);

        avg += w * textureLod(L2_raw, pos, 0.0) * L2_mul;
        W += w;
    }
    avg /= W;

    return avg;
}

//!HOOK POSTKERNEL
//!BIND HOOKED
//!BIND L2
//!SAVE MR
//!WHEN PREKERNEL.w POSTKERNEL.w > PREKERNEL.h POSTKERNEL.h > *
//!COMPONENTS 4
//!DESC SSimDownscaler mean & R

#define oversharp   THR

#define sigma_nsq   10. / (255.*255.)
#define locality    2.0

#define offset      vec2(0,0)

#define Kernel(x)   pow(1.0 / locality, abs(x))
#define taps        3.0

#define Luma(rgb)   ( dot(rgb, vec3(0.2126, 0.7152, 0.0722)) )

mat3x3 ScaleH(vec2 pos) {
    float low  = ceil(-0.5*taps - offset)[0];
    float high = floor(0.5*taps - offset)[0];

    float W = 0.0;
    mat3x3 avg = mat3x3(0);

    for (float k = low; k <= high; k++) {
        pos[0] = HOOKED_pos[0] + HOOKED_pt[0] * k;
        float rel = k + offset[0];
        float w = Kernel(rel);

        vec3 L = POSTKERNEL_tex(pos).rgb;
        avg += w * mat3x3(L, L*L, L2_tex(pos).rgb);
        W += w;
    }
    avg /= W;

    return avg;
}

vec4 hook() {
    vec2 pos = HOOKED_pos;

    float low  = ceil(-0.5*taps - offset)[1];
    float high = floor(0.5*taps - offset)[1];

    float W = 0.0;
    mat3x3 avg = mat3x3(0);

    for (float k = low; k <= high; k++) {
        pos[1] = HOOKED_pos[1] + HOOKED_pt[1] * k;
        float rel = k + offset[1];
        float w = Kernel(rel);

        avg += w * ScaleH(pos);
        W += w;
    }
    avg /= W;

    float Sl = Luma(max(avg[1] - avg[0] * avg[0], 0.));
    float Sh = Luma(max(avg[2] - avg[0] * avg[0], 0.));
    return vec4(avg[0], mix(sqrt((Sh + sigma_nsq) / (Sl + sigma_nsq)) * (1. + oversharp), clamp(Sh / Sl, 0., 1.), float(Sl > Sh)));
}

//!HOOK POSTKERNEL
//!BIND HOOKED
//!BIND MR
//!BIND PREKERNEL
//!WHEN PREKERNEL.w POSTKERNEL.w > PREKERNEL.h POSTKERNEL.h > *
//!DESC SSimDownscaler final pass

#define locality    2.0

#define offset      vec2(0,0)

#define Kernel(x)   pow(1.0 / locality, abs(x))
#define taps        3.0

float light(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float edgeWeight(out vec3 edgeLow, out vec3 edgeHigh) {
    vec2 center = (floor(PREKERNEL_pos * PREKERNEL_size - 0.5) + 0.5) * PREKERNEL_pt;
    int radius = min(12, int(ceil(3.0 * max(PREKERNEL_size.x / HOOKED_size.x,
                                          PREKERNEL_size.y / HOOKED_size.y))));
    vec2 reach = float(radius) * PREKERNEL_pt;
    vec4 referenceA = vec4(light(PREKERNEL_tex(center + vec2(reach.x,0)).rgb),
                          light(PREKERNEL_tex(center - vec2(reach.x,0)).rgb),
                          light(PREKERNEL_tex(center + vec2(0,reach.y)).rgb),
                          light(PREKERNEL_tex(center - vec2(0,reach.y)).rgb));
    vec4 referenceB = vec4(light(PREKERNEL_tex(center + reach).rgb),
                          light(PREKERNEL_tex(center - reach).rgb),
                          light(PREKERNEL_tex(center + vec2(reach.x,-reach.y)).rgb),
                          light(PREKERNEL_tex(center + vec2(-reach.x,reach.y)).rgb));
    vec4 matchesA = vec4(0), matchesB = vec4(0);
    vec3 low = vec3(1e20), high = vec3(-1e20);
    vec2 signedChange = vec2(0), variation = vec2(0);
    for (int y = -radius; y <= radius; ++y) {
        float previousLight = 0.0;
        for (int x = -radius; x <= radius; ++x) {
            vec2 pos = center + vec2(x,y) * PREKERNEL_pt;
            vec3 c = PREKERNEL_tex(pos).rgb;
            low = min(low,c); high = max(high,c);
            float a = light(c);
            vec4 agreeA = 1.0 - smoothstep(vec4(0.5/255.0), vec4(2.0/255.0), abs(vec4(a)-referenceA));
            vec4 agreeB = 1.0 - smoothstep(vec4(0.5/255.0), vec4(2.0/255.0), abs(vec4(a)-referenceB));
            matchesA += agreeA * vec4(float(x>0),float(x<0),float(y>0),float(y<0));
            matchesB += agreeB * vec4(float(x+y>0),float(x+y<0),float(x-y>0),float(x-y<0));
            // Reuse the previous horizontal sample instead of fetching it again.
            if (x > -radius) {
                float dx = a - previousLight;
                signedChange.x += dx; variation.x += abs(dx);
            }
            previousLight = a;
            if (y < radius) {
                float dy = light(PREKERNEL_tex(pos + vec2(0,PREKERNEL_pt.y)).rgb) - a;
                signedChange.y += dy; variation.y += abs(dy);
            }
        }
    }
    // All eight half-planes contain exactly r*(2*r+1) grid points.
    float halfCount = float(radius * (2*radius+1));
    vec4 flatA = smoothstep(vec4(0.74),vec4(0.80),matchesA/halfCount);
    vec4 flatB = smoothstep(vec4(0.74),vec4(0.80),matchesB/halfCount);
    float exterior = max(max(max(flatA.x,flatA.y),max(flatA.z,flatA.w)),
                         max(max(flatB.x,flatB.y),max(flatB.z,flatB.w)));
    float coherence = length(signedChange) / max(variation.x + variation.y, 1e-6);
    float contrast = light(high-low);
    edgeLow=low; edgeHigh=high;
    return max(smoothstep(0.45,0.80,coherence),exterior) * smoothstep(0.01,0.04,contrast);
}

mat3x3 ScaleH(vec2 pos) {
    float low  = ceil(-0.5*taps - offset)[0];
    float high = floor(0.5*taps - offset)[0];

    float W = 0.0;
    mat3x3 avg = mat3x3(0);

    for (float k = low; k <= high; k++) {
        pos[0] = HOOKED_pos[0] + HOOKED_pt[0] * k;
        float rel = k + offset[0];
        float w = Kernel(rel);

        vec4 MR = MR_tex(pos);
        avg += w * mat3x3(MR.a*MR.rgb, MR.rgb, MR.aaa);
        W += w;
    }
    avg /= W;

    return avg;
}

vec4 hook() {
#if (DEBUG == 1)
    // Red where the shader sharpens, blue where it softens or keeps detail.
    if (MR_tex(HOOKED_pos).a > 1.0 + THR)
        return vec4(1.0, 0.0, 0.0, 1.0);
    return vec4(0.0, 0.0, 1.0, 1.0);
#else
    vec2 pos = HOOKED_pos;

    float low  = ceil(-0.5*taps - offset)[1];
    float high = floor(0.5*taps - offset)[1];

    float W = 0.0;
    mat3x3 avg = mat3x3(0);

    for (float k = low; k <= high; k++) {
        pos[1] = HOOKED_pos[1] + HOOKED_pt[1] * k;
        float rel = k + offset[1];
        float w = Kernel(rel);

        avg += w * ScaleH(pos);
        W += w;
    }
    avg /= W;

    vec4 L = POSTKERNEL_texOff(0);
    vec3 enhanced = avg[1] + avg[2] * L.rgb - avg[0];
    if (ANTIRING <= 0.0)
        return vec4(enhanced, L.a);
    // The four source samples are a subset of the full analysis footprint.
    // If the reconstruction is already inside their range, it cannot overshoot
    // the wider source bounds. Preserve that pixel without evaluating the mask.
    vec2 probeCenter = (floor(PREKERNEL_pos * PREKERNEL_size - 0.5) + 0.5) * PREKERNEL_pt;
    vec3 probeLow = vec3(1e20), probeHigh = vec3(-1e20);
    for (int py = 0; py < 2; ++py) {
        for (int px = 0; px < 2; ++px) {
            vec3 sampleColor = PREKERNEL_tex(probeCenter + vec2(px,py) * PREKERNEL_pt).rgb;
            probeLow = min(probeLow, sampleColor);
            probeHigh = max(probeHigh, sampleColor);
        }
    }
    if (all(greaterThanEqual(enhanced, probeLow - vec3(1e-6))) &&
        all(lessThanEqual(enhanced, probeHigh + vec3(1e-6))))
        return vec4(enhanced, L.a);
    vec3 edgeLow, edgeHigh;
    float mask = ANTIRING * edgeWeight(edgeLow, edgeHigh);
    if (mask <= 0.0)
        return vec4(enhanced, L.a);
    // Reconstruct from the local mean plus SSIM detail. Bound one shared
    // RGB detail multiplier, rather than clipping finished RGB channels.
    vec3 mean = mix(avg[1], clamp(avg[1], edgeLow, edgeHigh), mask);
    vec3 detail = enhanced - avg[1];
    vec3 allowed = vec3(1.0);
    for (int c = 0; c < 3; ++c) {
        if (detail[c] > 1e-6)
            allowed[c] = (edgeHigh[c] - mean[c]) / detail[c];
        else if (detail[c] < -1e-6)
            allowed[c] = (edgeLow[c] - mean[c]) / detail[c];
    }
    float limit = clamp(min(allowed.r, min(allowed.g, allowed.b)), 0.0, 1.0);
    float gain = mix(1.0, limit, mask);
    return vec4(mean + gain * detail, L.a);
#endif
}



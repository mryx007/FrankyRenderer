#define U(index) texelFetch(ngu_cubici2_constants, ivec2((index), 0), 0)
float ngu_cubici2_fetch(int x, int y) {
    ivec2 center = ivec2(floor(HOOKED_pos * HOOKED_size));
    ivec2 upper = ivec2(HOOKED_size) - ivec2(1);
    return texelFetch(HOOKED_raw, clamp(center + ivec2(x, y), ivec2(0), upper), 0).x;
}
vec4 ngu_cubici2_body(int output_index) {
    float down_left = ngu_cubici2_fetch(-1, -1);
    float down = ngu_cubici2_fetch(0, -1);
    float down_right = ngu_cubici2_fetch(1, -1);
    float right = ngu_cubici2_fetch(1, 0);
    float left = ngu_cubici2_fetch(-1, 0);
    float center = ngu_cubici2_fetch(0, 0);
    float up_left = ngu_cubici2_fetch(-1, 1);
    float up = ngu_cubici2_fetch(0, 1);
    float up_right = ngu_cubici2_fetch(1, 1);

    vec4 a0 = down_left * U(0) + U(36);
    a0 += down * U(1);
    a0 += down_right * U(2);
    a0 += left * U(3);
    a0 += center * U(4);
    a0 += right * U(5);
    a0 += up_left * U(6);
    a0 += up * U(7);
    a0 += up_right * U(8);
    vec4 a1 = down_left * U(9) + U(37);
    a1 += down * U(10);
    a1 += down_right * U(11);
    a1 += left * U(12);
    a1 += center * U(13);
    a1 += right * U(14);
    a1 += up_left * U(15);
    a1 += up * U(16);
    a1 += up_right * U(17);
    vec4 a2 = down_left * U(18) + U(38);
    vec4 a3 = down_left * U(27) + U(39);
    a2 += down * U(19);
    a3 += down * U(28);
    a2 += down_right * U(20);
    a3 += down_right * U(29);
    a2 += left * U(21);
    a3 += left * U(30);
    a2 += center * U(22);
    a3 += center * U(31);
    a2 += right * U(23);
    a3 += right * U(32);
    a2 += up_left * U(24);
    a3 += up_left * U(33);
    a2 += up * U(25);
    a3 += up * U(34);
    a2 += up_right * U(26);
    a3 += up_right * U(35);

    vec4 leak = U(40).xxxx;
    vec4 o0 = mix(vec4(0.0), a0 * leak, lessThan(a0, vec4(0.0)));
    o0 += max(a0, vec4(0.0));
    vec4 o1 = mix(vec4(0.0), a1 * leak, lessThan(a1, vec4(0.0)));
    o1 += max(a1, vec4(0.0));
    vec4 o2 = mix(vec4(0.0), a2 * leak, lessThan(a2, vec4(0.0)));
    o2 += max(a2, vec4(0.0));
    vec4 o3 = mix(vec4(0.0), a3 * leak, lessThan(a3, vec4(0.0)));
    o3 += max(a3, vec4(0.0));
    return output_index == 0 ? o0 : output_index == 1 ? o1 : output_index == 2 ? o2 : o3;
}
//!DESC [NGU Sharp VeryHigh chroma Cubici2] MRT output 0
//!HOOK CHROMA
//!BIND HOOKED
//!BIND ngu_cubici2_constants
//!SAVE NGU_CUBICI2_O0
//!WIDTH HOOKED.w
//!HEIGHT HOOKED.h
//!COMPONENTS 4
#define U(index) texelFetch(ngu_cubici2_constants, ivec2((index), 0), 0)
float ngu_cubici2_fetch(int x, int y) {
    ivec2 center = ivec2(floor(HOOKED_pos * HOOKED_size));
    ivec2 upper = ivec2(HOOKED_size) - ivec2(1);
    return texelFetch(HOOKED_raw, clamp(center + ivec2(x, y), ivec2(0), upper), 0).x;
}
vec4 ngu_cubici2_body(int output_index) {
    float down_left = ngu_cubici2_fetch(-1, -1);
    float down = ngu_cubici2_fetch(0, -1);
    float down_right = ngu_cubici2_fetch(1, -1);
    float right = ngu_cubici2_fetch(1, 0);
    float left = ngu_cubici2_fetch(-1, 0);
    float center = ngu_cubici2_fetch(0, 0);
    float up_left = ngu_cubici2_fetch(-1, 1);
    float up = ngu_cubici2_fetch(0, 1);
    float up_right = ngu_cubici2_fetch(1, 1);

    vec4 a0 = down_left * U(0) + U(36);
    a0 += down * U(1);
    a0 += down_right * U(2);
    a0 += left * U(3);
    a0 += center * U(4);
    a0 += right * U(5);
    a0 += up_left * U(6);
    a0 += up * U(7);
    a0 += up_right * U(8);
    vec4 a1 = down_left * U(9) + U(37);
    a1 += down * U(10);
    a1 += down_right * U(11);
    a1 += left * U(12);
    a1 += center * U(13);
    a1 += right * U(14);
    a1 += up_left * U(15);
    a1 += up * U(16);
    a1 += up_right * U(17);
    vec4 a2 = down_left * U(18) + U(38);
    vec4 a3 = down_left * U(27) + U(39);
    a2 += down * U(19);
    a3 += down * U(28);
    a2 += down_right * U(20);
    a3 += down_right * U(29);
    a2 += left * U(21);
    a3 += left * U(30);
    a2 += center * U(22);
    a3 += center * U(31);
    a2 += right * U(23);
    a3 += right * U(32);
    a2 += up_left * U(24);
    a3 += up_left * U(33);
    a2 += up * U(25);
    a3 += up * U(34);
    a2 += up_right * U(26);
    a3 += up_right * U(35);

    vec4 leak = U(40).xxxx;
    vec4 o0 = mix(vec4(0.0), a0 * leak, lessThan(a0, vec4(0.0)));
    o0 += max(a0, vec4(0.0));
    vec4 o1 = mix(vec4(0.0), a1 * leak, lessThan(a1, vec4(0.0)));
    o1 += max(a1, vec4(0.0));
    vec4 o2 = mix(vec4(0.0), a2 * leak, lessThan(a2, vec4(0.0)));
    o2 += max(a2, vec4(0.0));
    vec4 o3 = mix(vec4(0.0), a3 * leak, lessThan(a3, vec4(0.0)));
    o3 += max(a3, vec4(0.0));
    return output_index == 0 ? o0 : output_index == 1 ? o1 : output_index == 2 ? o2 : o3;
}
vec4 hook() {
    return ngu_cubici2_body(0);
}

//!DESC [NGU Sharp VeryHigh chroma Cubici2] MRT output 1
//!HOOK CHROMA
//!BIND HOOKED
//!BIND ngu_cubici2_constants
//!SAVE NGU_CUBICI2_O1
//!WIDTH HOOKED.w
//!HEIGHT HOOKED.h
//!COMPONENTS 4
#define U(index) texelFetch(ngu_cubici2_constants, ivec2((index), 0), 0)
float ngu_cubici2_fetch(int x, int y) {
    ivec2 center = ivec2(floor(HOOKED_pos * HOOKED_size));
    ivec2 upper = ivec2(HOOKED_size) - ivec2(1);
    return texelFetch(HOOKED_raw, clamp(center + ivec2(x, y), ivec2(0), upper), 0).x;
}
vec4 ngu_cubici2_body(int output_index) {
    float down_left = ngu_cubici2_fetch(-1, -1);
    float down = ngu_cubici2_fetch(0, -1);
    float down_right = ngu_cubici2_fetch(1, -1);
    float right = ngu_cubici2_fetch(1, 0);
    float left = ngu_cubici2_fetch(-1, 0);
    float center = ngu_cubici2_fetch(0, 0);
    float up_left = ngu_cubici2_fetch(-1, 1);
    float up = ngu_cubici2_fetch(0, 1);
    float up_right = ngu_cubici2_fetch(1, 1);

    vec4 a0 = down_left * U(0) + U(36);
    a0 += down * U(1);
    a0 += down_right * U(2);
    a0 += left * U(3);
    a0 += center * U(4);
    a0 += right * U(5);
    a0 += up_left * U(6);
    a0 += up * U(7);
    a0 += up_right * U(8);
    vec4 a1 = down_left * U(9) + U(37);
    a1 += down * U(10);
    a1 += down_right * U(11);
    a1 += left * U(12);
    a1 += center * U(13);
    a1 += right * U(14);
    a1 += up_left * U(15);
    a1 += up * U(16);
    a1 += up_right * U(17);
    vec4 a2 = down_left * U(18) + U(38);
    vec4 a3 = down_left * U(27) + U(39);
    a2 += down * U(19);
    a3 += down * U(28);
    a2 += down_right * U(20);
    a3 += down_right * U(29);
    a2 += left * U(21);
    a3 += left * U(30);
    a2 += center * U(22);
    a3 += center * U(31);
    a2 += right * U(23);
    a3 += right * U(32);
    a2 += up_left * U(24);
    a3 += up_left * U(33);
    a2 += up * U(25);
    a3 += up * U(34);
    a2 += up_right * U(26);
    a3 += up_right * U(35);

    vec4 leak = U(40).xxxx;
    vec4 o0 = mix(vec4(0.0), a0 * leak, lessThan(a0, vec4(0.0)));
    o0 += max(a0, vec4(0.0));
    vec4 o1 = mix(vec4(0.0), a1 * leak, lessThan(a1, vec4(0.0)));
    o1 += max(a1, vec4(0.0));
    vec4 o2 = mix(vec4(0.0), a2 * leak, lessThan(a2, vec4(0.0)));
    o2 += max(a2, vec4(0.0));
    vec4 o3 = mix(vec4(0.0), a3 * leak, lessThan(a3, vec4(0.0)));
    o3 += max(a3, vec4(0.0));
    return output_index == 0 ? o0 : output_index == 1 ? o1 : output_index == 2 ? o2 : o3;
}
vec4 hook() {
    return ngu_cubici2_body(1);
}

//!DESC [NGU Sharp VeryHigh chroma Cubici2] MRT output 2
//!HOOK CHROMA
//!BIND HOOKED
//!BIND ngu_cubici2_constants
//!SAVE NGU_CUBICI2_O2
//!WIDTH HOOKED.w
//!HEIGHT HOOKED.h
//!COMPONENTS 4
#define U(index) texelFetch(ngu_cubici2_constants, ivec2((index), 0), 0)
float ngu_cubici2_fetch(int x, int y) {
    ivec2 center = ivec2(floor(HOOKED_pos * HOOKED_size));
    ivec2 upper = ivec2(HOOKED_size) - ivec2(1);
    return texelFetch(HOOKED_raw, clamp(center + ivec2(x, y), ivec2(0), upper), 0).x;
}
vec4 ngu_cubici2_body(int output_index) {
    float down_left = ngu_cubici2_fetch(-1, -1);
    float down = ngu_cubici2_fetch(0, -1);
    float down_right = ngu_cubici2_fetch(1, -1);
    float right = ngu_cubici2_fetch(1, 0);
    float left = ngu_cubici2_fetch(-1, 0);
    float center = ngu_cubici2_fetch(0, 0);
    float up_left = ngu_cubici2_fetch(-1, 1);
    float up = ngu_cubici2_fetch(0, 1);
    float up_right = ngu_cubici2_fetch(1, 1);

    vec4 a0 = down_left * U(0) + U(36);
    a0 += down * U(1);
    a0 += down_right * U(2);
    a0 += left * U(3);
    a0 += center * U(4);
    a0 += right * U(5);
    a0 += up_left * U(6);
    a0 += up * U(7);
    a0 += up_right * U(8);
    vec4 a1 = down_left * U(9) + U(37);
    a1 += down * U(10);
    a1 += down_right * U(11);
    a1 += left * U(12);
    a1 += center * U(13);
    a1 += right * U(14);
    a1 += up_left * U(15);
    a1 += up * U(16);
    a1 += up_right * U(17);
    vec4 a2 = down_left * U(18) + U(38);
    vec4 a3 = down_left * U(27) + U(39);
    a2 += down * U(19);
    a3 += down * U(28);
    a2 += down_right * U(20);
    a3 += down_right * U(29);
    a2 += left * U(21);
    a3 += left * U(30);
    a2 += center * U(22);
    a3 += center * U(31);
    a2 += right * U(23);
    a3 += right * U(32);
    a2 += up_left * U(24);
    a3 += up_left * U(33);
    a2 += up * U(25);
    a3 += up * U(34);
    a2 += up_right * U(26);
    a3 += up_right * U(35);

    vec4 leak = U(40).xxxx;
    vec4 o0 = mix(vec4(0.0), a0 * leak, lessThan(a0, vec4(0.0)));
    o0 += max(a0, vec4(0.0));
    vec4 o1 = mix(vec4(0.0), a1 * leak, lessThan(a1, vec4(0.0)));
    o1 += max(a1, vec4(0.0));
    vec4 o2 = mix(vec4(0.0), a2 * leak, lessThan(a2, vec4(0.0)));
    o2 += max(a2, vec4(0.0));
    vec4 o3 = mix(vec4(0.0), a3 * leak, lessThan(a3, vec4(0.0)));
    o3 += max(a3, vec4(0.0));
    return output_index == 0 ? o0 : output_index == 1 ? o1 : output_index == 2 ? o2 : o3;
}
vec4 hook() {
    return ngu_cubici2_body(2);
}

//!DESC [NGU Sharp VeryHigh chroma Cubici2] MRT output 3
//!HOOK CHROMA
//!BIND HOOKED
//!BIND ngu_cubici2_constants
//!SAVE NGU_CUBICI2_O3
//!WIDTH HOOKED.w
//!HEIGHT HOOKED.h
//!COMPONENTS 4
#define U(index) texelFetch(ngu_cubici2_constants, ivec2((index), 0), 0)
float ngu_cubici2_fetch(int x, int y) {
    ivec2 center = ivec2(floor(HOOKED_pos * HOOKED_size));
    ivec2 upper = ivec2(HOOKED_size) - ivec2(1);
    return texelFetch(HOOKED_raw, clamp(center + ivec2(x, y), ivec2(0), upper), 0).x;
}
vec4 ngu_cubici2_body(int output_index) {
    float down_left = ngu_cubici2_fetch(-1, -1);
    float down = ngu_cubici2_fetch(0, -1);
    float down_right = ngu_cubici2_fetch(1, -1);
    float right = ngu_cubici2_fetch(1, 0);
    float left = ngu_cubici2_fetch(-1, 0);
    float center = ngu_cubici2_fetch(0, 0);
    float up_left = ngu_cubici2_fetch(-1, 1);
    float up = ngu_cubici2_fetch(0, 1);
    float up_right = ngu_cubici2_fetch(1, 1);

    vec4 a0 = down_left * U(0) + U(36);
    a0 += down * U(1);
    a0 += down_right * U(2);
    a0 += left * U(3);
    a0 += center * U(4);
    a0 += right * U(5);
    a0 += up_left * U(6);
    a0 += up * U(7);
    a0 += up_right * U(8);
    vec4 a1 = down_left * U(9) + U(37);
    a1 += down * U(10);
    a1 += down_right * U(11);
    a1 += left * U(12);
    a1 += center * U(13);
    a1 += right * U(14);
    a1 += up_left * U(15);
    a1 += up * U(16);
    a1 += up_right * U(17);
    vec4 a2 = down_left * U(18) + U(38);
    vec4 a3 = down_left * U(27) + U(39);
    a2 += down * U(19);
    a3 += down * U(28);
    a2 += down_right * U(20);
    a3 += down_right * U(29);
    a2 += left * U(21);
    a3 += left * U(30);
    a2 += center * U(22);
    a3 += center * U(31);
    a2 += right * U(23);
    a3 += right * U(32);
    a2 += up_left * U(24);
    a3 += up_left * U(33);
    a2 += up * U(25);
    a3 += up * U(34);
    a2 += up_right * U(26);
    a3 += up_right * U(35);

    vec4 leak = U(40).xxxx;
    vec4 o0 = mix(vec4(0.0), a0 * leak, lessThan(a0, vec4(0.0)));
    o0 += max(a0, vec4(0.0));
    vec4 o1 = mix(vec4(0.0), a1 * leak, lessThan(a1, vec4(0.0)));
    o1 += max(a1, vec4(0.0));
    vec4 o2 = mix(vec4(0.0), a2 * leak, lessThan(a2, vec4(0.0)));
    o2 += max(a2, vec4(0.0));
    vec4 o3 = mix(vec4(0.0), a3 * leak, lessThan(a3, vec4(0.0)));
    o3 += max(a3, vec4(0.0));
    return output_index == 0 ? o0 : output_index == 1 ? o1 : output_index == 2 ? o2 : o3;
}
vec4 hook() {
    return ngu_cubici2_body(3);
}

//!TEXTURE ngu_cubici2_constants
//!SIZE 42 1
//!FORMAT rgba32f
//!FILTER NEAREST
b946eeb7389f08be98eb9b3d55efa0b9a898a6b8ad3620bdb1b37c3dc9339fb9b9b2afb83c21a53de65d843a40dcf7b9e8dc1838910519bf9d61573f78e25cb9cb7f8eb75ce8293d8587af3dcdb752b99d8182b84323f53d1b1ca53c1b87c4b98ac6be388a6a4ebcffd66cbf09f3563960c85638e934043ee67c92bdb4d24b397ba2b0b7595f393e875463bcb1cbd1b56037dd3df3d080b89524823d087276bc49f9cdbca00b22b838e916bf8445d0bd0536a53dad82ca3882b30a3db688c5bc5f16fabc4dcd2239eb9a473cfdd5e2bd235e90be14ae2139c2a4363fd7a636bfe8109cbe68ab6639402f64bd0cc8febde737a23deace57383613b73c99a8283eb6e2c0be617404b71bf1e4bd67d44d3f27dafbbe2026a2b6cfdc6c3d52ff173e417cb9bce55053bdb10d7bb9f454bcb90290013d0c5a53bdcfef1eb9edcab1b93d9b15bc406d34bd9de44bb99b0f88b9881ed4bdc8af563c160b1fb91cfcb1b9cf33913d33936c3cc59c4bb98ae0a7b9ceaccc3cbb1e433ca7f191b9df417ab928677ebf96f6a13cd91f4db8024ab8b93313823f3f31b53c6a5e4db9fb72b2b908b84a3a47b1f33c317e8fb9210f67b9568949bee287ebb70222fe3c357fed3d8c8e61bd1880be37875621bd76954a3e452046bd97c169b8e9e44f3cfffe78bd19ca743ddfb9a9b7bd9ae23d4302d93d227c8a3e24ee70385a4d91bd24ae343e0a8082be8d4f83b669b8b0bc0a7121bff1854ebeffe7e2b401c3813f29d9eb3d8c19b23e0aea62b75b5981bffdac0e3d13bce1bd90188db6c91b02bc6841a8bdb4c4b1b3be73ecb85d257738fcaa95b37265633a12f5aa35b8c98f39ea39a238ad66feb7d795bfb8d2ee9ab5dc058b3612cfee39099d2135cf3829b833f03db9cdcccc3d000000000000000000000000cdcccc3a610b363b0000000000000000

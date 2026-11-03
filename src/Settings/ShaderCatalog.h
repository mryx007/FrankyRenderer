#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
inline constexpr int ShaderCount = 23, ShaderParameterLimit = 12;

struct ShaderParameterInfo
{
    const char* name;
    float initial, minimum, maximum;
    bool integer;
};

struct ShaderInfo
{
    const char* file;
    const wchar_t* label;
    const wchar_t* category;
    const wchar_t* help;
    int count;
    ShaderParameterInfo parameters[ShaderParameterLimit];
};

inline constexpr ShaderInfo ShaderCatalog[] = {
    {"AMD_CAS_scl_RT.glsl",
     L"AMD CAS",
     L"Sharpening",
     L"Contrast-adaptive sharpening after scaling.",
     1,
     {{"STR", 0.5f, 0.0f, 1.0f, false}}},
    {"FineDehalo_lite_RT.glsl",
     L"FineDehalo",
     L"Edge Cleanup",
     L"Reduce bright and dark halos around edges.",
     11,
     {{"RX", 2.0f, 1.0f, 4.0f, false},
      {"RY", 2.0f, 1.0f, 4.0f, false},
      {"THMI", 80.0f, 0.0f, 255.0f, false},
      {"THMA", 128.0f, 0.0f, 255.0f, false},
      {"THLIMI", 50.0f, 0.0f, 255.0f, false},
      {"THLIMA", 100.0f, 0.0f, 255.0f, false},
      {"STR_D", 1.0f, 0.0f, 2.0f, false},
      {"STR_B", 1.0f, 0.0f, 2.0f, false},
      {"SENS_L", 50.0f, 0.0f, 100.0f, false},
      {"SENS_H", 50.0f, 0.0f, 100.0f, false},
      {"DEBUG", 0.0f, 0.0f, 4.0f, true}}},
    {"kantiring_ms_RT.glsl",
     L"Anti-Ringing",
     L"Edge Cleanup",
     L"Limit ringing introduced by image upscaling.",
     2,
     {{"TAP", 2.0f, 2.0f, 4.0f, true}, {"STR", 0.75f, 0.0f, 1.0f, false}}},
    {"PixelClipper_RT.glsl",
     L"PixelClipper",
     L"Edge Cleanup",
     L"Limit overshoot during image and chroma scaling.",
     5,
     {{"TAPS_scale", 0.0f, 0.0f, 1.0f, true},
      {"TAPS_cscale", 0.0f, 0.0f, 1.0f, true},
      {"CLIP_scale", 0.8f, 0.0f, 1.0f, false},
      {"CLIP_cscale", 0.8f, 0.0f, 1.0f, false},
      {"CLIP_dscale", 1.0f, 0.0f, 1.0f, false}}},
    {"KrigBilateral.glsl",
     L"KrigBilateral",
     L"Chroma",
     L"Reconstruct chroma guided by luma. Requires subsampled YUV input.",
     0,
     {}},
    {"CfL_Prediction_RT.glsl",
     L"CfL Prediction",
     L"Chroma",
     L"Predict chroma from luma. Requires subsampled YUV input.",
     4,
     {{"chroma_offset_x", 0.0f, -3.402823e+38f, 3.402823e+38f, false},
      {"chroma_offset_y", 0.0f, -3.402823e+38f, 3.402823e+38f, false},
      {"distance_coeff", 2.0f, 0.0f, 3.402823e+38f, false},
      {"intensity_coeff", 128.0f, 0.0f, 3.402823e+38f, false}}},
    {"nlmeans_luma_RT.glsl",
     L"NLMeans",
     L"Denoising",
     L"Reduce luma noise using similar image patches.",
     1,
     {{"REF", 0.0f, 0.0f, 1.0f, true}}},
    {"kBFDN_RT.glsl",
     L"Bilateral Denoise",
     L"Denoising",
     L"Edge-aware luma noise reduction.",
     3,
     {{"TAP", 5.0f, 1.0f, 9.0f, true}, {"SS", 0.8f, 0.0f, 10.0f, false}, {"SR", 0.1f, 0.0f, 1.0f, false}}},
    {"kCCD_RT.glsl", L"Chroma Denoise", L"Denoising", L"Reduce color noise.", 1, {{"THR", 8.0f, 0.0f, 255.0f, false}}},
    {"hdeband_rgb_RT.glsl",
     L"hdeband",
     L"Debanding",
     L"Reduce banding in smooth gradients.",
     9,
     {{"S", 0.0f, 0.0f, 10.0f, false},
      {"SI", 50.0f, 0.0f, 100.0f, false},
      {"SR", 0.0f, 0.0f, 1.0f, false},
      {"SW", 5.0f, 0.0f, 10.0f, false},
      {"RADIUS", 16.0f, 1.0f, 32.0f, true},
      {"SPARSITY", 2.0f, 0.0f, 10.0f, false},
      {"DIRECTIONS", 8.0f, 1.0f, 8.0f, true},
      {"RUN_START", 1.0f, 0.0f, 1.0f, true},
      {"TOLERANCE", 0.0f, 0.0f, 0.1f, false}}},
    {"neo_f3kdb_rgb_RT.glsl",
     L"neo-f3kdb",
     L"Debanding",
     L"Reduce gradient banding with adjustable thresholds and grain.",
     9,
     {{"RANGE", 15.0f, 1.0f, 64.0f, true},
      {"GRAIN", 0.0f, 0.0f, 1.0f, false},
      {"GRAD", 0.0f, 0.0f, 1.0f, true},
      {"THR_AVG", 0.015f, 0.0f, 1.0f, false},
      {"THR_MAX", 0.015f, 0.0f, 1.0f, false},
      {"THR_MID", 0.015f, 0.0f, 1.0f, false},
      {"THR_ALL", 0.0f, 0.0f, 1.0f, true},
      {"AB", 1.5f, 1.0f, 4.0f, false},
      {"AM", 0.15f, 0.0f, 1.0f, false}}},
    {"Fast_Guided_enh_rgb_RT.glsl",
     L"Guided Detail Enhance",
     L"Detail / Contrast",
     L"Enhance detail with guided filtering.",
     4,
     {{"RAD", 2.0f, 1.0f, 8.0f, true},
      {"EPS", 0.004f, 0.001f, 0.1f, false},
      {"SUBSAM", 2.0f, 1.0f, 4.0f, true},
      {"STR", 0.5f, 0.0f, 5.0f, false}}},
    {"eq_clarity_RT.glsl",
     L"Clarity",
     L"Detail / Contrast",
     L"Adjust local contrast in dark and bright areas.",
     9,
     {{"RAD", 4.0f, 1.0f, 5.0f, true},
      {"SHIFT", 2.0f, 1.0f, 5.0f, false},
      {"STR", 0.4f, 0.0f, 1.0f, false},
      {"STRD", 0.4f, 0.0f, 1.0f, false},
      {"STRL", 0.0f, 0.0f, 1.0f, false},
      {"BM", 3.0f, 1.0f, 7.0f, true},
      {"CD", 50.0f, 0.0f, 255.0f, true},
      {"CL", 205.0f, 0.0f, 255.0f, true},
      {"DEBUG", 0.0f, 0.0f, 2.0f, true}}},
    {"FSRCNNX_x2_8_0_4_1.glsl",
     L"FSRCNNX 2x",
     L"Image Upscaling",
     L"Neural 2x upscaling. Activates only when enlargement is sufficient.",
     0,
     {}},
    {"AiUpscale_Fast_2x_Photo_RT.glsl",
     L"AiUpscale Photo 2x",
     L"Image Upscaling",
     L"Photo-oriented neural 2x upscaling.",
     1,
     {{"SHARP", 0.0f, 0.0f, 1.0f, true}}},
    {"ravu_zoom_ar_r2.glsl",
     L"RAVU Zoom AR R2",
     L"Image Upscaling",
     L"Adaptive upscaling with anti-ringing. Radius 2, the fastest RAVU Zoom.",
     0,
     {}},
    {"Anime4K_Upscale_CNN_x2_M.glsl",
     L"Anime4K CNN 2x M",
     L"Anime Upscaling",
     L"For 2D animation. Neural 2x upscaling.",
     0,
     {}},
    {"ArtCNN_C4F16.glsl",
     L"ArtCNN C4F16",
     L"Anime Upscaling",
     L"For 2D animation. Initial shader compilation can take several seconds.",
     0,
     {}},
    {"CuNNy_2x12_DS.glsl",
     L"CuNNy 2x12 DS",
     L"Anime Upscaling",
     L"For 2D animation. Neural 2x upscaling with denoising.",
     0,
     {}},
    {"SSimDownscaler_RT.glsl",
     L"SSimDownscaler",
     L"Image Downscaling",
     L"Refine image downscaling. Requires a smaller output size; works best with Mitchell as downscaler.",
     2,
     {{"THR", 0.0f, 0.0f, 1.0f, false}, {"DEBUG", 0.0f, 0.0f, 1.0f, true}}},
    {"DPID_RT.glsl",
     L"DPID",
     L"Image Downscaling",
     L"Detail-preserving downscaling. Requires a smaller output size.",
     1,
     {{"LBD", 1.0f, 0.0f, 3.0f, false}}},
    // Appended so stored shader indices keep their meaning; the chain order
    // places them after RAVU Zoom AR R2.
    {"ravu_zoom_r3.glsl",
     L"RAVU Zoom R3",
     L"Image Upscaling",
     L"Adaptive upscaling with radius 3. More detail than R2, slower.",
     0,
     {}},
    {"ravu_zoom_ar_r3.glsl",
     L"RAVU Zoom AR R3",
     L"Image Upscaling",
     L"Radius 3 with anti-ringing. Highest RAVU quality, slowest.",
     0,
     {}},
};

struct ShaderConfig
{
    uint32_t enabled = 0, overrides = 0;
    float values[ShaderParameterLimit] = {};
};

struct UserShaderSettings
{
    ShaderConfig shaders[ShaderCount];
    int order[ShaderCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 21, 22, 16, 17, 18, 19, 20};
};

// Index of RAVU Zoom AR R2; shaders added later join the chain after it.
inline constexpr int ShaderRavuR2 = 15, ShaderAddedFirst = 21;

// Completes a chain order saved before shaders were added: keeps the first
// occurrence of every known shader and inserts missing ones after RAVU Zoom
// AR R2 (or at the end). A complete order is left unchanged.
inline void CompleteShaderOrder(int (&order)[ShaderCount], int savedCount = ShaderCount)
{
    int kept[ShaderCount] = {}, count = 0;
    uint32_t seen = 0;
    for (int i = 0; i < savedCount; i++)
        if (order[i] >= 0 && order[i] < ShaderCount && !(seen & (1u << order[i])))
        {
            seen |= 1u << order[i];
            kept[count++] = order[i];
        }
    if (count == ShaderCount)
    {
        std::copy(kept, kept + ShaderCount, order);
        return;
    }
    int at = count;
    for (int i = 0; i < count; i++)
        if (kept[i] == ShaderRavuR2)
            at = i + 1;
    int merged[ShaderCount] = {}, n = 0;
    for (int i = 0; i < at; i++)
        merged[n++] = kept[i];
    for (int id = ShaderAddedFirst; id < ShaderCount; id++)
        if (!(seen & (1u << id)))
            merged[n++] = id, seen |= 1u << id;
    for (int i = at; i < count; i++)
        merged[n++] = kept[i];
    for (int id = 0; id < ShaderCount; id++)
        if (!(seen & (1u << id)))
            merged[n++] = id;
    std::copy(merged, merged + ShaderCount, order);
}

inline bool ValidUserShaders(const UserShaderSettings& s)
{
    uint32_t seen = 0;
    for (int id : s.order)
    {
        if (id < 0 || id >= ShaderCount || (seen & (1u << id)))
            return false;
        seen |= 1u << id;
    }
    for (int i = 0; i < ShaderCount; i++)
    {
        const auto& c = s.shaders[i];
        const auto& meta = ShaderCatalog[i];
        if (c.enabled > 1 || c.overrides & ~((1u << meta.count) - 1))
            return false;
        for (int p = 0; p < ShaderParameterLimit; p++)
        {
            const float v = c.values[p];
            if (!std::isfinite(v))
                return false;
            if (c.overrides & (1u << p))
            {
                const auto& range = meta.parameters[p];
                if (v < range.minimum || v > range.maximum || (range.integer && std::floor(v) != v))
                    return false;
            }
        }
    }
    return true;
}

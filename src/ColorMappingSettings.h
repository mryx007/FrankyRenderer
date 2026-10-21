#pragma once
#include <libplacebo/options.h>
#include <cstring>
#include <cmath>
#include <cstdlib>

// Append-only indices: persisted overrides are tied to these keys.
struct MappingOption {const char* key;const wchar_t* label;int group;const wchar_t* help;};
inline constexpr MappingOption MappingOptions[]={
 {"knee_adaptation",L"Knee Adaptation",0,L"Adapt the knee to scene brightness. Used by ST 2094 and Spline."},
 {"knee_minimum",L"Knee Minimum",0,L"Lower bound for the adaptive knee, in PQ-normalized space."},
 {"knee_maximum",L"Knee Maximum",0,L"Upper bound for the adaptive knee, in PQ-normalized space."},
 {"knee_default",L"Knee Default",0,L"Knee used when average scene brightness is unavailable. Keep within Minimum / Maximum."},
 {"knee_offset",L"BT.2390 Knee Offset",0,L"BT.2390 shoulder offset. The specification uses 0.5; libplacebo defaults to 1."},
 {"slope_tuning",L"Spline Slope Tuning",1,L"Strength of scene-dependent Spline slope adjustment."},
 {"slope_offset",L"Spline Slope Offset",1,L"Offset for Spline slope adjustment."},
 {"spline_contrast",L"Spline Contrast",1,L"Spline contrast around the knee. Only affects the Spline function."},
 {"reinhard_contrast",L"Reinhard Contrast",1,L"Contrast for Reinhard. Larger values increase contrast."},
 {"linear_knee",L"Mobius / Gamma Knee",1,L"Transition point for Mobius and Gamma tone mapping."},
 {"exposure",L"Linear Exposure",1,L"Exposure multiplier for Linear and Linear Light tone mapping. 1 is unchanged."},
 {"contrast_recovery",L"Contrast Recovery",2,L"Recover local contrast lost in tone mapping. 0 disables recovery."},
 {"contrast_smoothness",L"Contrast Smoothness",2,L"Spatial scale for contrast recovery. Used only when recovery is above 0."},
 {"perceptual_deadzone",L"Perceptual Deadzone",3,L"Protect colors already inside the target gamut from perceptual remapping."},
 {"perceptual_strength",L"Perceptual Strength",3,L"Balance perceptual gamut compression and colorimetric mapping."},
 {"colorimetric_gamma",L"Colorimetric Gamma",3,L"Gamma used for colorimetric clipping; affects Relative and related mapping."},
 {"softclip_knee",L"Softclip Knee",3,L"Saturation threshold where soft clipping starts."},
 {"softclip_desat",L"Softclip Desaturation",3,L"Desaturation near the gamut boundary when using Softclip."},
 {"lut3d_size_I",L"Gamut LUT Intensity Size",4,L"Internal gamut LUT resolution along intensity. 0 selects the library size."},
 {"lut3d_size_C",L"Gamut LUT Chroma Size",4,L"Internal gamut LUT resolution along chroma. 0 selects the library size."},
 {"lut3d_size_h",L"Gamut LUT Hue Size",4,L"Internal gamut LUT resolution along hue. 0 selects the library size."},
 {"lut3d_tricubic",L"Tricubic Gamut Interpolation",4,L"Use tricubic interpolation for the internal gamut LUT; costs additional GPU work."},
 {"tone_lut_size",L"Tone Mapping LUT Size",4,L"Internal tone curve lookup resolution. 0 selects the library size."},
 {"force_tone_mapping_lut",L"Force Tone Mapping LUT",5,L"Use a lookup table even for tone curves that could be evaluated directly."},
 {"visualize_lut",L"Visualize Mapping LUT",5,L"Draw a diagnostic visualization over the video."},
 {"visualize_lut_x0",L"Visualization Left",5,L"Left edge of the visualization in normalized output coordinates (0 to 1)."},
 {"visualize_lut_y0",L"Visualization Top",5,L"Top edge of the visualization in normalized output coordinates (0 to 1)."},
 {"visualize_lut_x1",L"Visualization Right",5,L"Right edge of the visualization in normalized output coordinates (0 to 1)."},
 {"visualize_lut_y1",L"Visualization Bottom",5,L"Bottom edge of the visualization in normalized output coordinates (0 to 1)."},
 {"visualize_hue",L"Visualization Hue (radians)",5,L"Gamut visualization hue slice, from -pi to pi."},
 {"visualize_theta",L"Visualization Rotation (radians)",5,L"Gamut visualization angle, from 0 to pi/2."},
 {"inverse_tone_mapping",L"Inverse Tone Mapping",6,L"Allow expansion of SDR brightness into HDR. Requires a supporting tone curve."},
 {"gamut_expansion",L"Gamut Expansion",6,L"Allow expansion into a wider display gamut."},
 {"tone_map_metadata",L"Tone Mapping Metadata",6,L"Default retains automatic Dolby Vision / HDR10+ metadata selection. A manual choice overrides it."},
};
inline constexpr int MappingOptionCount=sizeof(MappingOptions)/sizeof(MappingOptions[0]);
struct ColorMappingSettings {char values[MappingOptionCount][32]={};};
inline int MappingIndex(const char* key){for(int i=0;i<MappingOptionCount;i++)if(!strcmp(key,MappingOptions[i].key))return i;return -1;}
inline bool ApplyMappingOverrides(pl_options options,const ColorMappingSettings& s){
    for(int i=0;i<MappingOptionCount;i++){
        const auto* value=s.values[i];if(strnlen_s(value,32)>=32)return false;
        const auto opt=pl_find_option(MappingOptions[i].key);if(!opt)return false;
        if(value[0]&&(opt->type==PL_OPT_FLOAT||opt->type==PL_OPT_INT)){
            char* end=nullptr;const double number=strtod(value,&end);
            if(end==value||*end||!std::isfinite(number)||(opt->type==PL_OPT_INT&&std::floor(number)!=number))return false;
        }
        if(value[0]&&!pl_options_set_str(options,MappingOptions[i].key,value))return false;
    }
    const auto& m=options->color_map_params;const auto& t=m.tone_constants;
    if(!(t.knee_minimum>0&&t.knee_minimum<.5f&&t.knee_maximum>.5f&&t.knee_maximum<1))return false;
    if(t.knee_default<t.knee_minimum||t.knee_default>t.knee_maximum)return false;
    if(!(t.reinhard_contrast>0&&t.reinhard_contrast<1&&t.linear_knee>0&&t.linear_knee<1))return false;
    const auto& r=m.visualize_rect;
    if(!std::isfinite(r.x0)||!std::isfinite(r.y0)||!std::isfinite(r.x1)||!std::isfinite(r.y1)||r.x0<0||r.y0<0||r.x1>1||r.y1>1||r.x0>=r.x1||r.y0>=r.y1)return false;
    return std::isfinite(m.visualize_hue)&&std::isfinite(m.visualize_theta)&&std::abs(m.visualize_hue)<=3.141593f&&m.visualize_theta>=0&&m.visualize_theta<=1.570797f;
}
inline bool ValidMappingOverrides(const ColorMappingSettings& s){auto o=pl_options_alloc(nullptr);if(!o)return false;pl_options_reset(o,&pl_render_default_params);const bool ok=ApplyMappingOverrides(o,s);pl_options_free(&o);return ok;}
template<class Settings> bool ValidCombinedMapping(const Settings& s){
    auto o=pl_options_alloc(nullptr);if(!o)return false;pl_options_reset(o,&pl_render_default_params);
    auto& m=o->color_map_params;auto& t=m.tone_constants;
    t.knee_adaptation=s.kneeAdaptation;t.knee_minimum=s.kneeMinimum;t.knee_maximum=s.kneeMaximum;t.spline_contrast=s.splineContrast;
    t.knee_default=t.knee_default<s.kneeMinimum?s.kneeMinimum:t.knee_default>s.kneeMaximum?s.kneeMaximum:t.knee_default;
    m.gamut_constants.perceptual_strength=s.perceptualStrength;m.gamut_constants.softclip_knee=s.softclipKnee;
    m.contrast_recovery=s.contrastRecovery;m.contrast_smoothness=s.contrastSmoothness;
    const bool ok=ApplyMappingOverrides(o,s.colorMapping);pl_options_free(&o);return ok;
}

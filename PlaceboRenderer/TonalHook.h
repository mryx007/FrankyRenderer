#pragma once
#include "TonalSettings.h"
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/shaders/sampling.h>
#include <cstdio>

// Independent HDR implementation of tonal-zone grading, inspired by prod80's
// Shadows / Midtones / Highlights. Not an unchanged SDR ReShade port.
// The source transfer function is decoded with libplacebo, graded in linear
// light (1 = 203 nits), then restored for normal scaling and color management.
// No upper RGB clamp: valid HDR values above reference white must survive.
struct TonalHook {
    TonalSettings settings;
    pl_hook hook={};
    TonalHook(){hook.stages=PL_HOOK_RGB;hook.input=PL_HOOK_SIG_TEX;hook.priv=this;
        hook.signature=0x544f4e414c484452ULL;hook.hook=Run;}
    static pl_hook_res Run(void* priv,const pl_hook_params* p){
        auto& s=static_cast<TonalHook*>(priv)->settings;
        pl_hook_res r={};r.output=PL_HOOK_SIG_NONE;r.repr=p->repr;
        r.color=p->color;r.components=p->components;r.rect=p->rect;
        if(!p->orig_color||!pl_color_space_is_hdr(p->orig_color)||!ActiveTonal(s))return r;
        // A pre-sampled shader may have unspecified output dimensions (0).
        // Returning that from a resizable RGB hook loses the image size and
        // trips libplacebo's no-op scaler assertion with other user hooks.
        // Sample the texture at its exact size to establish explicit geometry.
        r.output=PL_HOOK_SIG_COLOR;r.sh=pl_dispatch_begin(p->dispatch);
        pl_sample_src source={};source.tex=p->tex;source.components=p->components;
        source.new_w=p->tex->params.w;source.new_h=p->tex->params.h;
        if(!pl_shader_sample_direct(r.sh,&source)){r.failed=true;return r;}
        const auto xyz=pl_get_rgb2xyz_matrix(pl_raw_primaries_get(p->color.primaries));
        char body[4096];
        // Smooth, complementary masks from the unmodified source luminance:
        // shadows fade from 1 to 50 nits; highlights rise from 50 to 203 nits.
        // Middle tones occupy the transition. Exposure and contrast preserve
        // RGB ratios; saturation changes chroma around source-primary luminance.
        sprintf_s(body,R"GLSL({
            vec3 lum=vec3(%.9f,%.9f,%.9f);
            float y=max(dot(color.rgb,lum),0.0);
            vec3 w;
            w.x=1.0-smoothstep(0.004926108,0.246305419,y);
            w.z=smoothstep(0.246305419,1.0,y);
            w.y=max(1.0-w.x-w.z,0.0);
            vec3 e=vec3(%.9f,%.9f,%.9f);
            vec3 c=vec3(%.9f,%.9f,%.9f);
            vec3 s=vec3(%.9f,%.9f,%.9f);
            vec3 pivots=vec3(0.049261084,0.246305419,1.0);
            float stops=dot(w,e)+dot(w*c,log2(vec3(max(y,1e-8))/pivots));
            color.rgb*=exp2(clamp(stops,-16.0,16.0));
            float adjusted=max(dot(color.rgb,lum),0.0);
            color.rgb=max(mix(vec3(adjusted),color.rgb,1.0+dot(w,s)),vec3(0.0));
        })GLSL",double(xyz.m[1][0]),double(xyz.m[1][1]),double(xyz.m[1][2]),
            double(s.exposure[0]),double(s.exposure[1]),double(s.exposure[2]),
            double(s.contrast[0]),double(s.contrast[1]),double(s.contrast[2]),
            double(s.saturation[0]),double(s.saturation[1]),double(s.saturation[2]));
        pl_shader_linearize(r.sh,&p->color);
        pl_custom_shader custom={};custom.description="HDR Shadows / Midtones / Highlights";
        custom.input=custom.output=PL_SHADER_SIG_COLOR;custom.body=body;
        custom.output_w=p->tex->params.w;custom.output_h=p->tex->params.h;
        r.failed=!pl_shader_custom(r.sh,&custom);
        pl_shader_delinearize(r.sh,&p->color);
        return r;
    }
};

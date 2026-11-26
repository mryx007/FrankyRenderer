#pragma once
#include <libplacebo/colorspace.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/sampling.h>
#include <cstdio>
#include <cstring>

// Exact pure-power display gamma between libplacebo's fixed curves. The output
// is already encoded for the nearest supported gamma; raising its full-range
// signal by base / display makes the display show that curve's light. Runs
// before dithering, so the result is dithered like any other output.
// It reads the output as a texture: with peak detection or colour adjustment
// libplacebo's output shader has no fixed size, and a colour hook there is
// taken as resizing the output and switched off for good after one picture.
struct OutputGammaHook
{
    pl_hook hook = {};
    char body[512] = {};

    OutputGammaHook()
    {
        hook.stages = PL_HOOK_OUTPUT;
        hook.input = PL_HOOK_SIG_TEX;
        hook.priv = this;
        hook.hook = Run;
    }

    const pl_hook* Prepare(float exponent, const pl_color_repr& repr)
    {
        // Undo the output range (e.g. TV levels) around the power.
        auto normalized = repr;
        pl_color_repr_normalize(&normalized);
        const auto decode = pl_color_repr_decode(&normalized, nullptr);
        const double scale = decode.mat.m[0][0], offset = decode.c[0];
        sprintf_s(body,
                  "color.rgb = (pow(max(color.rgb * %.9f + vec3(%.9f), vec3(0.0)), vec3(%.9f)) - vec3(%.9f)) / %.9f;\n",
                  scale,
                  offset,
                  double(exponent),
                  offset,
                  scale);
        return Sign();
    }

    // Linear output (16-bit float): moves black to 0 and scales the light,
    // e.g. SDR white to Windows' SDR content brightness.
    const pl_hook* PrepareLevels(float black, float gain)
    {
        sprintf_s(body, "color.rgb = (color.rgb - vec3(%.9f)) * %.9f;\n", double(black), double(gain));
        return Sign();
    }

    const pl_hook* Sign()
    {
        uint64_t signature = 0x4f55544741ULL;
        for (const char* c = body; *c; ++c)
            signature = (signature ^ uint8_t(*c)) * 0x100000001b3ULL;
        hook.signature = signature;
        return &hook;
    }

    static pl_hook_res Run(void* priv, const pl_hook_params* p)
    {
        auto* self = static_cast<OutputGammaHook*>(priv);
        pl_hook_res r = {};
        r.output = PL_HOOK_SIG_TEX;
        r.repr = p->repr;
        r.color = p->color;
        r.components = p->components;
        r.rect = p->rect;
        // A shader reading a texture has no fixed size either, so the result
        // goes into a texture of the output's size.
        r.tex = p->tex ? p->get_tex(p->priv, p->tex->params.w, p->tex->params.h) : nullptr;
        pl_shader sh = r.tex ? pl_dispatch_begin(p->dispatch) : nullptr;
        if (!sh)
        {
            r.failed = true;
            return r;
        }
        pl_sample_src source = {};
        source.tex = p->tex;
        pl_custom_shader custom = {};
        custom.description = "display gamma";
        custom.input = custom.output = PL_SHADER_SIG_COLOR;
        custom.body = self->body;
        if (!pl_shader_sample_direct(sh, &source) || !pl_shader_custom(sh, &custom))
        {
            pl_dispatch_abort(p->dispatch, &sh);
            r.failed = true;
            return r;
        }
        pl_dispatch_params dispatch = {};
        dispatch.shader = &sh;
        dispatch.target = r.tex;
        r.failed = !pl_dispatch_finish(p->dispatch, &dispatch);
        return r;
    }
};

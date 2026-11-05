#pragma once
#include <libplacebo/colorspace.h>
#include <libplacebo/shaders/custom.h>
#include <cstdio>
#include <cstring>

// Exact pure-power display gamma between libplacebo's fixed curves. The output
// is already encoded for the nearest supported gamma; raising its full-range
// signal by base / display makes the display show that curve's light. Runs
// before dithering, so the result is dithered like any other output.
struct OutputGammaHook
{
    pl_hook hook = {};
    char body[512] = {};

    OutputGammaHook()
    {
        hook.stages = PL_HOOK_OUTPUT;
        hook.input = PL_HOOK_SIG_COLOR;
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
        r.output = PL_HOOK_SIG_COLOR;
        r.sh = p->sh;
        r.repr = p->repr;
        r.color = p->color;
        r.components = p->components;
        r.rect = p->rect;
        pl_custom_shader custom = {};
        custom.description = "display gamma";
        custom.input = custom.output = PL_SHADER_SIG_COLOR;
        custom.body = self->body;
        r.failed = !p->sh || !pl_shader_custom(r.sh, &custom);
        return r;
    }
};

#pragma once
// Parsing of the user shader chain and Adaptive Sharpen, shared by playback
// and the quality measurement so both run the same hooks.
#include "Renderer/SharpenShader.h"
#include "Renderer/UserShaderSource.h"
#include <libplacebo/shaders/custom.h>
#include <charconv>
#include <cstring>
#include <string>

// Catalogue shader `id` with the overridden parameter values of `c`; nullptr
// when the file is missing, does not parse or lacks a configured parameter.
inline const pl_hook* ParseUserShader(pl_gpu gpu, int id, const ShaderConfig& c)
{
    const auto source = LoadUserShaderSource(id);
    const pl_hook* hook = source.empty() ? nullptr : pl_mpv_user_shader_parse(gpu, source.data(), source.size());
    if (hook)
        for (int p = 0; p < ShaderCatalog[id].count; p++)
            if (c.overrides & (1u << p))
            {
                const pl_hook_par* found = nullptr;
                for (int n = 0; n < hook->num_parameters; n++)
                    if (!strcmp(hook->parameters[n].name, ShaderCatalog[id].parameters[p].name))
                    {
                        found = &hook->parameters[n];
                        break;
                    }
                if (!found)
                {
                    pl_mpv_user_shader_destroy(&hook);
                    break;
                }
                if (found->type == PL_VAR_FLOAT)
                    found->data->f = c.values[p];
                else if (found->type == PL_VAR_SINT)
                    found->data->i = static_cast<int>(c.values[p]);
                else if (found->type == PL_VAR_UINT)
                    found->data->u = static_cast<unsigned>(c.values[p]);
            }
    return hook;
}

// Adaptive Sharpen before output conversion, with the given strength.
inline const pl_hook* ParseSharpenShader(pl_gpu gpu, float strength)
{
    std::string shaderText = AdaptiveSharpenSource;
    auto stage = shaderText.find("//!HOOK OUTPUT");
    if (stage == std::string::npos)
        return nullptr;
    shaderText.replace(stage, 14, "//!HOOK PREOUTPUT");
    auto first = shaderText.find("#define curve_height"), last = shaderText.find('\n', first);
    if (first == std::string::npos || last == std::string::npos)
        return nullptr;
    char value[32];
    auto converted = std::to_chars(value, value + 32, strength);
    if (converted.ec != std::errc{})
        return nullptr;
    shaderText.replace(first, last - first, std::string("#define curve_height ") + std::string(value, converted.ptr));
    return pl_mpv_user_shader_parse(gpu, shaderText.data(), shaderText.size());
}

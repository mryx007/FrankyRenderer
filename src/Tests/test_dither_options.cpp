// Every entry of the dither list must reach libplacebo as what its name says.
#define NOMINMAX
#include "Renderer/RenderOptions.h"
#include <cstdio>
#include <cstdlib>

static void Require(bool ok, const char* message)
{
    if (ok)
        return;
    printf("FAIL %s\n", message);
    exit(1);
}

int main()
{
    pl_options options = pl_options_alloc(nullptr);
    Require(options != nullptr, "options");
    RenderSettings settings;
    const int count = int(std::size(DitherChoices));
    Require(count - 4 == pl_num_error_diffusion_kernels, "every libplacebo kernel is offered");
    for (int method = 0; method < count; ++method)
    {
        settings.ditherMethod = method;
        Require(SUCCEEDED(BuildRenderOptions(options, settings)), "options build");
        if (method < 3)
            Require(options->params.dither_params && !options->params.error_diffusion, "ordinary dithering");
        else if (method == 3)
            Require(!options->params.dither_params && !options->params.error_diffusion, "off");
        else
            Require(options->params.dither_params &&
                        options->params.error_diffusion == pl_error_diffusion_kernels[method - 4],
                    "kernel in libplacebo's order, with a fallback dither");
    }
    pl_options_free(&options);
    puts("PASS dither options: every list entry reaches libplacebo");
    return 0;
}

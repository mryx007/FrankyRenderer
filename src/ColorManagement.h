#pragma once
#include "Backend.h"
#include <libplacebo/shaders/icc.h>
#include <libplacebo/shaders/lut.h>

struct ColorAssets
{
    pl_icc_object icc = nullptr;
    pl_custom_lut* lut = nullptr;

    ~ColorAssets()
    {
        pl_icc_close(&icc);
        pl_lut_free(&lut);
    }
};

HRESULT PrepareColorAssets(const RenderSettings&, std::shared_ptr<ColorAssets>&);

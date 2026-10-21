#pragma once
#include <string>
#include <cstring>
inline std::wstring ShaderParameterLabel(const char* key){
    struct Label{const char* key;const wchar_t* text;};
    for(const auto& p:{Label{"STR",L"Strength"},Label{"RAD",L"Radius"},Label{"RADIUS",L"Radius"},Label{"RX",L"Horizontal Radius"},Label{"RY",L"Vertical Radius"},Label{"STR_D",L"Dark Halo Strength"},Label{"STR_B",L"Bright Halo Strength"},Label{"STRD",L"Shadow Strength"},Label{"STRL",L"Highlight Strength"},Label{"DEBUG",L"Debug View"},Label{"GRAIN",L"Grain"},Label{"THR",L"Threshold"},Label{"TAP",L"Sample Taps"},Label{"SHARP",L"Sharpen"},Label{"REF",L"Reference Mode"},Label{"SUBSAM",L"Subsampling"},Label{"THMI",L"Strong Edge Low"},Label{"THMA",L"Strong Edge High"},Label{"THLIMI",L"Light Edge Low"},Label{"THLIMA",L"Light Edge High"},Label{"SENS_L",L"Low Sensitivity"},Label{"SENS_H",L"High Sensitivity"},Label{"EPS",L"Regularization"},Label{"TAPS_scale",L"Image Sampling Mode"},Label{"TAPS_cscale",L"Chroma Sampling Mode"},Label{"CLIP_scale",L"Upscaling Limit"},Label{"CLIP_cscale",L"Chroma Limit"},Label{"CLIP_dscale",L"Downscaling Limit"},Label{"THR_AVG",L"Average Threshold"},Label{"THR_MAX",L"Maximum Threshold"},Label{"THR_MID",L"Middle Threshold"},Label{"THR_ALL",L"Combined Threshold"}})if(!strcmp(key,p.key))return p.text;
    std::wstring s;for(const char* p=key;*p;p++)s+=wchar_t(static_cast<unsigned char>(*p));for(auto& c:s)if(c==L'_')c=L' ';return s;
}

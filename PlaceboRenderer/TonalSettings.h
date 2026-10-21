#pragma once
#include <cmath>
// HDR source grading. All zeros bypass the hook entirely.
struct TonalSettings {
    float exposure[3]={},contrast[3]={},saturation[3]={};
};
inline bool ValidTonal(const TonalSettings& s){
    for(int i=0;i<3;i++)if(!std::isfinite(s.exposure[i])||std::abs(s.exposure[i])>4||
        !std::isfinite(s.contrast[i])||std::abs(s.contrast[i])>1||
        !std::isfinite(s.saturation[i])||std::abs(s.saturation[i])>1)return false;
    return true;
}
inline bool ActiveTonal(const TonalSettings& s){
    for(int i=0;i<3;i++)if(s.exposure[i]!=0||s.contrast[i]!=0||s.saturation[i]!=0)return true;
    return false;
}

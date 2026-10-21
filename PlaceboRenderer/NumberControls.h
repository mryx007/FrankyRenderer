#pragma once
#include <algorithm>
#include <cmath>

struct NumberControl {
    int id;
    double step,minimum,maximum;
    // Some settings have a special automatic value 0, then a valid manual range.
    bool automaticZero=false,openBounds=false;
};
inline constexpr NumberControl NumberControls[]={
    {110,.005,-1,99},{111,.005,-1,99},{112,.001,-1,1},{113,.005,-1,99},
    {114,10,40,1000},{115,10,80,10000,true},
    {117,.05,0,1},{118,.05,.5,2,true},{119,.05,.5,2,true},
    {210,1,0,16},{211,.5,0,1000},{212,1,0,1000},{213,.1,0,1000},
    {410,1,0,1000},{411,.1,0,100},{412,.1,0,100},{413,.001,0,100},
    {414,.1,0,100},{420,.05,0,2},{421,.5,1,32},
    {440,.05,0,1},{441,.05,0,1.5},{442,.01,0,.5,false,true},
    {443,.01,.5,1,false,true},{450,.05,0,1},{451,.05,0,1},
    {510,.1,0,2},{730,.1,-180,180},{731,10,2500,25000},
};
inline constexpr int NumberButtonId(int field,bool increase){return 10000+field*2+(increase?1:0);}
inline double StepNumber(const NumberControl& control,double value,bool increase){
    if(!std::isfinite(value))return value;
    if(control.automaticZero){
        if(value==0)return increase?control.minimum:0;
        if(!increase&&value<=control.minimum)return 0;
    }
    double next=std::round((value+(increase?control.step:-control.step))*1e6)/1e6;
    if(control.openBounds){if(next<=control.minimum||next>=control.maximum)return value;}
    else next=std::clamp(next,control.minimum,control.maximum);
    // A typed value outside the UI range must never move in the wrong direction.
    if((increase&&next<value)||(!increase&&next>value))return value;
    return next;
}

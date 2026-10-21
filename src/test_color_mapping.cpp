#include "Settings.h"
#include "SettingsChoices.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool ok,const char* text){if(!ok){printf("FAIL %s\n",text);exit(1);}}
static long long Delta(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){long long d=0;for(size_t i=0;i<a.size();i++)if(i%4!=3)d+=abs(int(a[i])-b[i]);return d;}
int main(){
    wchar_t root[512];Check(GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT",root,512)>0,"private root");
    ColorMappingSettings mapping;Check(ValidMappingOverrides(mapping),"native defaults");auto o=pl_options_alloc(nullptr);pl_options_reset(o,&pl_render_default_params);
    for(int i=0;i<MappingOptionCount;i++){
        const auto opt=pl_find_option(MappingOptions[i].key);Check(opt!=nullptr,MappingOptions[i].key);auto data=pl_options_get(o,opt->key);Check(data!=nullptr,"native option query");
        strcpy_s(mapping.values[i],data->text);Check(ValidMappingOverrides(mapping),opt->key);
    }
    for(const auto& c:ToneChoices)if(strcmp(c.name,"auto"))Check(pl_options_set_str(o,"tone_mapping",c.name),c.name);
    for(const auto& c:GamutChoices)if(strcmp(c.name,"auto"))Check(pl_options_set_str(o,"gamut_mapping",c.name),c.name);
    const int exposure=MappingIndex("exposure"),tricubic=MappingIndex("lut3d_tricubic"),metadata=MappingIndex("tone_map_metadata");
    for(auto bad:{"nan","inf","-1","11","garbage"}){auto invalid=mapping;strcpy_s(invalid.values[exposure],bad);Check(!ValidMappingOverrides(invalid),"invalid numeric rejected");}
    {auto invalid=mapping;memset(invalid.values[exposure],'1',32);Check(!ValidMappingOverrides(invalid),"unterminated string rejected");}
    auto s=LoadSettings();s.colorMapping=mapping;strcpy_s(s.colorMapping.values[tricubic],"yes");strcpy_s(s.colorMapping.values[metadata],"hdr10");Check(SUCCEEDED(SaveSettings(s)),"persist mapping");auto loaded=LoadSettings();Check(!memcmp(&s.colorMapping,&loaded.colorMapping,sizeof(mapping)),"all overrides persist");
    PlaceboBackend backend;RenderSettings render=BackendSettings(s);render.colorMapping={};render.ditherMethod=3;render.peakDetect=false;render.toneMapping="linear";render.colorAdjustment=false;render.autoHdr=false;
    Check(SUCCEEDED(backend.SetSettings(render)),"render settings");VideoFrame f;f.width=f.height=64;f.stride=128;f.format=PixelFormat::P010;f.matrix=PL_COLOR_SYSTEM_BT_2020_NC;f.color=pl_color_space_hdr10;f.color.hdr.max_luma=4000;
    std::vector<uint16_t> input(64*64*3/2,512<<6);for(int i=0;i<4096;i++)input[i]=(64+i%64*876/63)<<6;Check(SUCCEEDED(backend.Upload(f,reinterpret_cast<uint8_t*>(input.data()),input.size()*2)),"HDR upload");
    auto draw=[&](){Check(SUCCEEDED(backend.SetSettings(render)),"live mapping settings");std::vector<uint8_t> out;Check(SUCCEEDED(backend.RenderOffscreen(64,64,out)),"GPU mapping pixels");return out;};auto original=draw();strcpy_s(render.colorMapping.values[exposure],"0.5");auto adjusted=draw();Check(Delta(original,adjusted)>500,"exposure reaches GPU");render.colorMapping={};Check(Delta(original,draw())<10,"Default restores GPU output");
    render.toneMapping="spline";strcpy_s(render.colorMapping.values[tricubic],"yes");strcpy_s(render.colorMapping.values[MappingIndex("lut3d_size_I")],"16");strcpy_s(render.colorMapping.values[MappingIndex("lut3d_size_C")],"16");strcpy_s(render.colorMapping.values[MappingIndex("lut3d_size_h")],"32");draw();
    pl_options_free(&o);printf("PASS %d native Color Mapping options; all tone/gamut functions; defaults, validation, persistence; live GPU exposure and tricubic LUT rendering\n",MappingOptionCount);
}

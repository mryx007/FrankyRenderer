#include "Settings.h"
#include "SettingsChoices.h"
#include "HdrTuning.h"
#include <algorithm>
#include <cmath>
#include <cstring>
static std::wstring Key(){wchar_t test[512];DWORD size=GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT",test,512);return size&&size<512?test:L"Software\\PlaceboRenderer";}
static void Float(HKEY key,const wchar_t* name,float& value){DWORD type=0,bytes=sizeof(value);float saved;if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&saved),&bytes)==ERROR_SUCCESS&&type==REG_BINARY&&bytes==sizeof(saved)&&std::isfinite(saved))value=saved;}
static void Bool(HKEY key,const wchar_t* name,BOOL& value){DWORD type=0,bytes=sizeof(DWORD),saved;if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(&saved),&bytes)==ERROR_SUCCESS&&type==REG_DWORD&&bytes==sizeof(saved))value=!!saved;}
static void String(HKEY key,const wchar_t* name,wchar_t (&value)[64]){DWORD type=0,bytes=sizeof(value);wchar_t saved[64]={};if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(saved),&bytes)==ERROR_SUCCESS&&type==REG_SZ&&bytes<=sizeof(saved)&&saved[63]==0)wcscpy_s(value,saved);}
static void Path(HKEY key,const wchar_t* name,wchar_t (&value)[1024]){DWORD type=0,bytes=sizeof(value);wchar_t saved[1024]={};if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(saved),&bytes)==ERROR_SUCCESS&&type==REG_SZ&&bytes<=sizeof(saved)&&saved[1023]==0)wcscpy_s(value,saved);}
static void LoadScaling(HKEY key,PlaceboSettings& settings){
    DWORD type=0,bytes=sizeof(ScalingSettings);ScalingSettings saved;
    const LONG result=RegQueryValueExW(key,L"ScalingV1",nullptr,&type,reinterpret_cast<BYTE*>(&saved),&bytes);
    if(result!=ERROR_FILE_NOT_FOUND){if(result==ERROR_SUCCESS&&type==REG_BINARY&&bytes==sizeof(saved)&&ValidScaling(saved))settings.scaling=saved;return;}
    // Migrate explicit legacy selections without overwriting existing pictures.
    auto name=[](const wchar_t* value,char (&out)[64]){size_t i=0;for(;i<63&&value[i];i++)out[i]=static_cast<char>(value[i]);out[i]=0;};
    if(RegQueryValueExW(key,L"Upscaler",nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS)name(settings.upscaler,settings.scaling.filters[0].filter);
    if(RegQueryValueExW(key,L"Downscaler",nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS)name(settings.downscaler,settings.scaling.filters[1].filter);
    if(settings.chromaScaler)strcpy_s(settings.scaling.filters[2].filter,ScalerChoices[settings.chromaScaler-1].name);
    settings.scaling.antiringing=settings.antiringing>0?settings.antiringing:-1;
    for(int i=0;i<2;i++){const float blur=i?settings.downscalerBlur:settings.upscalerBlur;if(blur>0){auto& f=settings.scaling.filters[i];strcpy_s(f.preset,f.filter);strcpy_s(f.filter,"custom");f.overrides|=4;f.values[2]=blur;}}
    if(!ValidScaling(settings.scaling))settings.scaling={};
}
PlaceboSettings LoadSettings(){
    PlaceboSettings settings;HKEY key;if(RegOpenKeyExW(HKEY_CURRENT_USER,Key().c_str(),0,KEY_READ,&key)!=ERROR_SUCCESS)return settings;
    {DWORD t=0,n=sizeof(UserShaderSettings);UserShaderSettings saved;
        const auto result=RegQueryValueExW(key,L"UserShadersV2",nullptr,&t,reinterpret_cast<BYTE*>(&saved),&n);
        if(result==ERROR_SUCCESS&&t==REG_BINARY&&n==sizeof(saved)&&ValidUserShaders(saved))settings.userShaders=saved;
        else if(result==ERROR_FILE_NOT_FOUND){
            // V1 contained an additional Adaptive Sharpen at index 0. Keep the
            // Processing control and preserve all other shader settings/order.
            struct LegacyShaders {ShaderConfig shaders[22];int order[22];} old;
            n=sizeof(old);if(RegQueryValueExW(key,L"UserShadersV1",nullptr,&t,reinterpret_cast<BYTE*>(&old),&n)==ERROR_SUCCESS&&t==REG_BINARY&&n==sizeof(old)){
                UserShaderSettings migrated;for(int i=0;i<ShaderCount;i++)migrated.shaders[i]=old.shaders[i+1];
                uint32_t seen=0;int count=0;bool valid=true;for(int id:old.order){if(id<0||id>=22||(seen&(1u<<id))){valid=false;break;}seen|=1u<<id;if(id)migrated.order[count++]=id-1;}
                if(valid&&count==ShaderCount&&ValidUserShaders(migrated))settings.userShaders=migrated;
            }
        }
    }
    {DWORD t=0,n=sizeof(TonalSettings);TonalSettings saved;if(RegQueryValueExW(key,L"HdrTonalV1",nullptr,&t,reinterpret_cast<BYTE*>(&saved),&n)==ERROR_SUCCESS&&t==REG_BINARY&&n==sizeof(saved)&&ValidTonal(saved))settings.tonal=saved;}
    {DWORD t=0,n=sizeof(ColorMappingSettings);ColorMappingSettings saved;if(RegQueryValueExW(key,L"ColorMappingV1",nullptr,&t,reinterpret_cast<BYTE*>(&saved),&n)==ERROR_SUCCESS&&t==REG_BINARY&&n==sizeof(saved)&&ValidMappingOverrides(saved))settings.colorMapping=saved;}
    {DWORD type=0,size=sizeof(RendererExtras);RendererExtras saved;if(RegQueryValueExW(key,L"RendererExtrasV1",nullptr,&type,reinterpret_cast<BYTE*>(&saved),&size)==ERROR_SUCCESS&&type==REG_BINARY&&size==sizeof(saved)&&ValidExtras(saved))settings.extras=saved;}
    {DWORD t=0,n=sizeof(BlackBarSettings);BlackBarSettings saved;if(RegQueryValueExW(key,L"BlackBarsV1",nullptr,&t,reinterpret_cast<BYTE*>(&saved),&n)==ERROR_SUCCESS&&t==REG_BINARY&&n==sizeof(saved)&&ValidBlackBars(saved))settings.blackBars=saved;}
    Float(key,L"Hue",settings.hue);Float(key,L"Temperature",settings.temperature);Bool(key,L"ColorAdjustment",settings.colorAdjustment);Float(key,L"Saturation",settings.saturation);Float(key,L"Contrast",settings.contrast);Float(key,L"Brightness",settings.brightness);Float(key,L"Gamma",settings.gamma);Float(key,L"SDRNits",settings.sdrNits);
    Float(key,L"KneeAdaptation",settings.kneeAdaptation);Float(key,L"SplineContrast",settings.splineContrast);Float(key,L"KneeMinimum",settings.kneeMinimum);Float(key,L"KneeMaximum",settings.kneeMaximum);
    Float(key,L"BlackCutoff",settings.blackCutoff);Float(key,L"PerceptualStrength",settings.perceptualStrength);Float(key,L"SoftclipKnee",settings.softclipKnee);Bool(key,L"ShowClipping",settings.showClipping);
    Bool(key,L"AdaptiveSharpen",settings.adaptiveSharpen);Float(key,L"SharpenStrength",settings.sharpenStrength);
    Bool(key,L"AllowDelayedPeak",settings.allowDelayedPeak);Bool(key,L"PeakDetect",settings.peakDetect);Float(key,L"PeakSmoothing",settings.peakSmoothing);Float(key,L"SceneLow",settings.sceneLow);Float(key,L"SceneHigh",settings.sceneHigh);Float(key,L"PeakPercentile",settings.peakPercentile);Float(key,L"ContrastRecovery",settings.contrastRecovery);Float(key,L"ContrastSmoothness",settings.contrastSmoothness);
    Float(key,L"HDRNits",settings.hdrNits);Float(key,L"Antiringing",settings.antiringing);Float(key,L"UpscalerBlur",settings.upscalerBlur);Float(key,L"DownscalerBlur",settings.downscalerBlur);
    Float(key,L"DebandThreshold",settings.debandThreshold);Float(key,L"DebandRadius",settings.debandRadius);Float(key,L"DebandGrain",settings.debandGrain);Bool(key,L"DitherTemporal",settings.ditherTemporal);
    Bool(key,L"SmoothMotion",settings.smoothMotion);Bool(key,L"ICCEnabled",settings.iccEnabled);Bool(key,L"LUTEnabled",settings.lutEnabled);Path(key,L"ICCPath",settings.iccPath);Path(key,L"LUTPath",settings.lutPath);
    {DWORD saved=0,type=0,bytes=4;if(RegQueryValueExW(key,L"Deband",nullptr,&type,reinterpret_cast<BYTE*>(&saved),&bytes)==ERROR_SUCCESS&&type==REG_DWORD&&bytes==4&&(saved<=1||saved==0xffffffff))settings.deband=saved==0xffffffff?FALSE:static_cast<BOOL>(saved);}Bool(key,L"HDRPassthrough",settings.hdrPassthrough);Bool(key,L"AutoHDR",settings.autoHdr);String(key,L"Upscaler",settings.upscaler);String(key,L"Downscaler",settings.downscaler);
    {DWORD mode=0,type=0,bytes=4;const auto result=RegQueryValueExW(key,L"HDRMode",nullptr,&type,reinterpret_cast<BYTE*>(&mode),&bytes);if(result==ERROR_SUCCESS&&type==REG_DWORD&&bytes==4&&mode<=2)settings.hdrMode=static_cast<int>(mode);else if(result==ERROR_FILE_NOT_FOUND)settings.hdrMode=settings.hdrPassthrough?0:2;}
    DWORD type=0,size=4,value=1;if(RegQueryValueExW(key,L"Deinterlacing",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=2)settings.deinterlacing=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"FieldOverride",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=2)settings.fieldOverride=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"ToneMapping",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<std::size(ToneChoices))settings.toneMapping=static_cast<int>(value);
    size=4;value=1;if(RegQueryValueExW(key,L"DebandIterations",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&(value<=16||value==0xffffffff))settings.debandIterations=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"DitherMethod",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<std::size(DitherChoices))settings.ditherMethod=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"ChromaScaler",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=std::size(ScalerChoices))settings.chromaScaler=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"MaximumFramesInFlight",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=16)settings.maximumFramesInFlight=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"OutputFormat",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<std::size(OutputChoices))settings.outputFormat=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"GamutMapping",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<std::size(GamutChoices))settings.gamutMapping=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"OutputLevels",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=2)settings.outputLevels=static_cast<int>(value);
    size=4;value=0;if(RegQueryValueExW(key,L"DisplayDepth",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4)for(int depth:DisplayDepths)if(value==static_cast<DWORD>(depth))settings.displayDepth=depth;
    size=4;value=0;if(RegQueryValueExW(key,L"ICCIntent",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size)==ERROR_SUCCESS&&type==REG_DWORD&&size==4&&value<=1)settings.iccIntent=static_cast<int>(value);LoadScaling(key,settings);RegCloseKey(key);
    // An incomplete saved color path must not reset unrelated choices (AutoHDR).
    if(settings.iccEnabled&&!settings.iccPath[0])settings.iccEnabled=FALSE;
    if(settings.lutEnabled&&!settings.lutPath[0])settings.lutEnabled=FALSE;
    if(FAILED(ValidateSettings(settings)))return PlaceboSettings{};return settings;
}
HRESULT ValidateSettings(const PlaceboSettings& s){
    if(!ValidTonal(s.tonal))return E_INVALIDARG;
    if(!ValidUserShaders(s.userShaders))return E_INVALIDARG;
    if(!ValidBlackBars(s.blackBars))return E_INVALIDARG;
    if(!ValidExtras(s.extras))return E_INVALIDARG;
    if(!ValidCombinedMapping(s))return E_INVALIDARG;
    if(s.hdrMode<0||s.hdrMode>2)return E_INVALIDARG;
    if(!ValidScaling(s.scaling))return E_INVALIDARG;
    if(s.maximumFramesInFlight<0||s.maximumFramesInFlight>16)return E_INVALIDARG;
    if(!ValidHdrTuning(s))return E_INVALIDARG;
    auto range=[](float value,float low,float high){return std::isfinite(value)&&value>=low&&value<=high;};
    if(!range(s.peakSmoothing,0,1000)||!range(s.sceneLow,0,100)||!range(s.sceneHigh,0,100)||(s.sceneLow>0&&s.sceneHigh>0&&s.sceneLow>s.sceneHigh)||!range(s.peakPercentile,0,100)||!range(s.contrastRecovery,0,2)||!range(s.contrastSmoothness,1,32)||s.gamutMapping<0||s.gamutMapping>=static_cast<int>(std::size(GamutChoices)))return E_INVALIDARG;
    if(!range(s.sharpenStrength,0,2))return E_INVALIDARG;
    if(s.deinterlacing<0||s.deinterlacing>2)return E_INVALIDARG;
    if(s.fieldOverride<0||s.fieldOverride>2)return E_INVALIDARG;
    if(s.chromaScaler<0||s.chromaScaler>static_cast<int>(std::size(ScalerChoices)))return E_INVALIDARG;
    if(s.outputLevels<0||s.outputLevels>2||std::find(std::begin(DisplayDepths),std::end(DisplayDepths),s.displayDepth)==std::end(DisplayDepths))return E_INVALIDARG;
    if(s.outputFormat<0||s.outputFormat>=static_cast<int>(std::size(OutputChoices)))return E_INVALIDARG;
    if(s.iccIntent<0||s.iccIntent>1||wcsnlen_s(s.iccPath,1024)>=1024||wcsnlen_s(s.lutPath,1024)>=1024||(s.iccEnabled&&!s.iccPath[0])||(s.lutEnabled&&!s.lutPath[0]))return E_INVALIDARG;
    if(s.toneMapping<0||s.toneMapping>=static_cast<int>(std::size(ToneChoices)))return E_INVALIDARG;
    if(s.deband<0||s.deband>1||s.debandIterations<-1||s.debandIterations>16||s.ditherMethod<0||s.ditherMethod>=static_cast<int>(std::size(DitherChoices))||(s.debandThreshold!=-1&&!range(s.debandThreshold,0,1000))||(s.debandRadius!=-1&&!range(s.debandRadius,0,1000))||(s.debandGrain!=-1&&!range(s.debandGrain,0,1000)))return E_INVALIDARG;
    if((s.hdrNits!=0&&!range(s.hdrNits,80,10000))||!range(s.antiringing,0,1))return E_INVALIDARG;
    for(float width:{s.upscalerBlur,s.downscalerBlur})if(width!=0&&!range(width,.5f,2))return E_INVALIDARG;
    if(!range(s.hue,-3.141593f,3.141593f)||!range(s.temperature,-1.143f,5.286f)||!range(s.saturation,0,100)||!range(s.contrast,0,100)||!range(s.brightness,-1,1)||!range(s.gamma,0,100)||!range(s.sdrNits,40,1000))return E_INVALIDARG;
    for(const auto* name:{s.upscaler,s.downscaler}){if(!name[0]||wcsnlen_s(name,64)>=64)return E_INVALIDARG;for(size_t i=0;name[i];i++)if(name[i]>127)return E_INVALIDARG;}
    return S_OK;
}
RenderSettings BackendSettings(const PlaceboSettings& s){RenderSettings out;out.tonal=s.tonal;out.userShaders=s.userShaders;out.blackBars=s.blackBars;out.extras=s.extras;out.colorMapping=s.colorMapping;out.hdrMode=s.hdrMode;out.hue=s.hue;out.temperature=s.temperature;out.colorAdjustment=!!s.colorAdjustment;out.scaling=s.scaling;ValidScaling(s.scaling,&out.motionRadius);out.maximumFramesInFlight=s.maximumFramesInFlight;out.kneeAdaptation=s.kneeAdaptation;out.splineContrast=s.splineContrast;out.kneeMinimum=s.kneeMinimum;out.kneeMaximum=s.kneeMaximum;out.blackCutoff=s.blackCutoff;out.perceptualStrength=s.perceptualStrength;out.softclipKnee=s.softclipKnee;out.showClipping=!!s.showClipping;out.adaptiveSharpen=!!s.adaptiveSharpen;out.sharpenStrength=s.sharpenStrength;out.peakDetect=!!s.peakDetect;out.allowDelayedPeak=!!s.allowDelayedPeak;out.peakSmoothing=s.peakSmoothing;out.sceneLow=s.sceneLow;out.sceneHigh=s.sceneHigh;out.peakPercentile=s.peakPercentile;out.contrastRecovery=s.contrastRecovery;out.contrastSmoothness=s.contrastSmoothness;out.gamutMapping=s.gamutMapping;out.outputLevels=s.outputLevels;out.displayDepth=s.displayDepth;out.deinterlacing=s.deinterlacing;out.smoothMotion=!!s.smoothMotion;out.iccEnabled=!!s.iccEnabled;out.lutEnabled=!!s.lutEnabled;out.iccIntent=s.iccIntent;out.iccPath=s.iccPath;out.lutPath=s.lutPath;out.outputFormat=s.outputFormat;out.chromaUpscaler=s.chromaScaler?ScalerChoices[s.chromaScaler-1].name:"auto";out.saturation=s.saturation;out.contrast=s.contrast;out.brightness=s.brightness;out.gamma=s.gamma;out.sdrNits=s.sdrNits;out.hdrNits=s.hdrNits;out.antiringing=s.antiringing;out.upscalerBlur=s.upscalerBlur;out.downscalerBlur=s.downscalerBlur;out.toneMapping=ToneChoices[s.toneMapping].name;const auto& debandDefaults=pl_render_default_params.deband_params?*pl_render_default_params.deband_params:pl_deband_default_params;out.deband=!!s.deband;out.debandIterations=s.debandIterations<0?debandDefaults.iterations:s.debandIterations;out.debandThreshold=s.debandThreshold<0?debandDefaults.threshold:s.debandThreshold;out.debandRadius=s.debandRadius<0?debandDefaults.radius:s.debandRadius;out.debandGrain=s.debandGrain<0?debandDefaults.grain:s.debandGrain;out.ditherMethod=s.ditherMethod;out.ditherTemporal=!!s.ditherTemporal;out.hdrPassthrough=s.hdrMode!=2;out.autoHdr=!!s.autoHdr;out.upscaler.clear();out.downscaler.clear();for(const auto* p=s.upscaler;*p;p++)out.upscaler+=static_cast<char>(*p);for(const auto* p=s.downscaler;*p;p++)out.downscaler+=static_cast<char>(*p);return out;}
HRESULT SaveSettings(const PlaceboSettings& s){
    HRESULT valid=ValidateSettings(s);if(FAILED(valid))return valid;HKEY key;LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,Key().c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);if(result)return HRESULT_FROM_WIN32(result);
    auto save=[&](const wchar_t* name,DWORD type,const void* data,DWORD bytes){if(!result)result=RegSetValueExW(key,name,0,type,static_cast<const BYTE*>(data),bytes);};
    save(L"HdrTonalV1",REG_BINARY,&s.tonal,sizeof(s.tonal));
    save(L"UserShadersV2",REG_BINARY,&s.userShaders,sizeof(s.userShaders));
    save(L"BlackBarsV1",REG_BINARY,&s.blackBars,sizeof(s.blackBars));
    save(L"RendererExtrasV1",REG_BINARY,&s.extras,sizeof(s.extras));
    save(L"ColorMappingV1",REG_BINARY,&s.colorMapping,sizeof(s.colorMapping));
    save(L"HDRMode",REG_DWORD,&s.hdrMode,4);
    save(L"AllowDelayedPeak",REG_DWORD,&s.allowDelayedPeak,4);
    save(L"Hue",REG_BINARY,&s.hue,4);save(L"Temperature",REG_BINARY,&s.temperature,4);save(L"ColorAdjustment",REG_DWORD,&s.colorAdjustment,4);
    save(L"ScalingV1",REG_BINARY,&s.scaling,sizeof(s.scaling));
    save(L"KneeAdaptation",REG_BINARY,&s.kneeAdaptation,4);save(L"SplineContrast",REG_BINARY,&s.splineContrast,4);save(L"KneeMinimum",REG_BINARY,&s.kneeMinimum,4);save(L"KneeMaximum",REG_BINARY,&s.kneeMaximum,4);
    save(L"BlackCutoff",REG_BINARY,&s.blackCutoff,4);save(L"PerceptualStrength",REG_BINARY,&s.perceptualStrength,4);save(L"SoftclipKnee",REG_BINARY,&s.softclipKnee,4);save(L"ShowClipping",REG_DWORD,&s.showClipping,4);
    save(L"AdaptiveSharpen",REG_DWORD,&s.adaptiveSharpen,4);save(L"SharpenStrength",REG_BINARY,&s.sharpenStrength,4);
    save(L"PeakDetect",REG_DWORD,&s.peakDetect,4);save(L"PeakSmoothing",REG_BINARY,&s.peakSmoothing,4);save(L"SceneLow",REG_BINARY,&s.sceneLow,4);save(L"SceneHigh",REG_BINARY,&s.sceneHigh,4);save(L"PeakPercentile",REG_BINARY,&s.peakPercentile,4);save(L"ContrastRecovery",REG_BINARY,&s.contrastRecovery,4);save(L"ContrastSmoothness",REG_BINARY,&s.contrastSmoothness,4);save(L"GamutMapping",REG_DWORD,&s.gamutMapping,4);
    save(L"Saturation",REG_BINARY,&s.saturation,4);save(L"Contrast",REG_BINARY,&s.contrast,4);save(L"Brightness",REG_BINARY,&s.brightness,4);save(L"Gamma",REG_BINARY,&s.gamma,4);save(L"SDRNits",REG_BINARY,&s.sdrNits,4);save(L"Deinterlacing",REG_DWORD,&s.deinterlacing,4);
    save(L"ToneMapping",REG_DWORD,&s.toneMapping,4);save(L"HDRNits",REG_BINARY,&s.hdrNits,4);save(L"Antiringing",REG_BINARY,&s.antiringing,4);save(L"UpscalerBlur",REG_BINARY,&s.upscalerBlur,4);save(L"DownscalerBlur",REG_BINARY,&s.downscalerBlur,4);
    save(L"DebandIterations",REG_DWORD,&s.debandIterations,4);save(L"DebandThreshold",REG_BINARY,&s.debandThreshold,4);save(L"DebandRadius",REG_BINARY,&s.debandRadius,4);save(L"DebandGrain",REG_BINARY,&s.debandGrain,4);save(L"DitherMethod",REG_DWORD,&s.ditherMethod,4);save(L"DitherTemporal",REG_DWORD,&s.ditherTemporal,4);
    save(L"ChromaScaler",REG_DWORD,&s.chromaScaler,4);
    save(L"OutputLevels",REG_DWORD,&s.outputLevels,4);save(L"DisplayDepth",REG_DWORD,&s.displayDepth,4);
    save(L"MaximumFramesInFlight",REG_DWORD,&s.maximumFramesInFlight,4);
    save(L"OutputFormat",REG_DWORD,&s.outputFormat,4);
    save(L"SmoothMotion",REG_DWORD,&s.smoothMotion,4);save(L"ICCEnabled",REG_DWORD,&s.iccEnabled,4);save(L"LUTEnabled",REG_DWORD,&s.lutEnabled,4);save(L"ICCIntent",REG_DWORD,&s.iccIntent,4);save(L"ICCPath",REG_SZ,s.iccPath,static_cast<DWORD>((wcslen(s.iccPath)+1)*2));save(L"LUTPath",REG_SZ,s.lutPath,static_cast<DWORD>((wcslen(s.lutPath)+1)*2));
    save(L"FieldOverride",REG_DWORD,&s.fieldOverride,4);save(L"Deband",REG_DWORD,&s.deband,4);save(L"HDRPassthrough",REG_DWORD,&s.hdrPassthrough,4);save(L"AutoHDR",REG_DWORD,&s.autoHdr,4);save(L"Upscaler",REG_SZ,s.upscaler,static_cast<DWORD>((wcslen(s.upscaler)+1)*2));save(L"Downscaler",REG_SZ,s.downscaler,static_cast<DWORD>((wcslen(s.downscaler)+1)*2));RegCloseKey(key);return HRESULT_FROM_WIN32(result);
}

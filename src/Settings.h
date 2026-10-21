#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
#include "Backend.h"
inline constexpr CLSID CLSID_FrankyDirect2DPage={0x19a4f3e2,0xf52e,0x4dbe,{0x91,0x67,0x33,0x2e,0x7c,0x38,0xe8,0x0c}};
inline constexpr CLSID CLSID_PlaceboRenderer={0xb864e92a,0x1bdf,0x4da1,{0x90,0x04,0xcb,0xf4,0x67,0xdf,0x23,0x9a}};
inline constexpr CLSID CLSID_MPCCompatible={0x71f080aa,0x8661,0x4093,{0xb1,0x5e,0x4f,0x69,0x03,0xe7,0x7d,0x0a}};
inline constexpr CLSID CLSID_PlaceboSettingsPage={0x6b35ef45,0x9c5e,0x40b4,{0x8e,0xac,0xde,0x2c,0x51,0x61,0x17,0x4d}};
inline constexpr CLSID CLSID_PlaceboProcessingPage={0x7d274b8b,0x7c98,0x43d7,{0xb4,0x48,0x78,0x2c,0xba,0x0c,0x6a,0x33}};
inline constexpr CLSID CLSID_PlaceboColorPage={0x5f6faac4,0x5b83,0x4dbe,{0xab,0x84,0xc0,0x4a,0x1c,0x57,0x53,0xa6}};
inline constexpr CLSID CLSID_PlaceboHdrPage={0x72eb4d7a,0x7d02,0x4080,{0xa8,0x5e,0x6e,0x65,0xcb,0x42,0x23,0xd1}};
inline constexpr CLSID CLSID_PlaceboEffectsPage={0xaf0d638a,0x478b,0x43b0,{0x81,0xd0,0xe7,0xb0,0xfa,0x46,0xa3,0xe1}};
inline constexpr CLSID CLSID_PlaceboAdjustmentPage={0x60585d47,0x776f,0x4b48,{0xa3,0x57,0x24,0x2d,0x8b,0x8a,0x3f,0x42}};
inline constexpr CLSID CLSID_PlaceboDebandingPage={0x10dd7ade,0xf6d7,0x4a4d,{0xa1,0xe9,0x5d,0x88,0x61,0x3a,0x4e,0xf2}};
inline constexpr CLSID CLSID_PlaceboScalingPage={0x4c64b3b7,0x5485,0x4230,{0xae,0x73,0x84,0xc7,0x90,0x7e,0x4f,0x3b}};
inline constexpr CLSID CLSID_PlaceboMappingPage={0xc5b9232e,0xdd14,0x43bb,{0xb8,0xd9,0x91,0x75,0x52,0xc0,0xa7,0x68}};
struct PlaceboSettings {
    TonalSettings tonal;
    UserShaderSettings userShaders;
    BlackBarSettings blackBars;
    RendererExtras extras;
    ColorMappingSettings colorMapping;
    float saturation=1,contrast=1,brightness=0,gamma=1,sdrNits=203;
    // Debanding numeric -1 means Default; explicit zero remains a manual value.
    BOOL deband=FALSE,hdrPassthrough=TRUE,autoHdr=TRUE;
    wchar_t upscaler[64]=L"ewa_lanczos",downscaler[64]=L"mitchell";
    int deinterlacing=1;
    int fieldOverride=0; // 0=sample flags, 1=force TFF, 2=force BFF
    int toneMapping=0;
    float hdrNits=0,antiringing=0,upscalerBlur=0,downscalerBlur=0;
    int debandIterations=-1,ditherMethod=0;
    float debandThreshold=-1,debandRadius=-1,debandGrain=-1;
    BOOL ditherTemporal=FALSE;
    int chromaScaler=0; // 0=libplacebo default, otherwise ScalerChoices index + 1
    int outputFormat=0; // auto, 8-bit SDR, prefer 10-bit, prefer 16-bit float
    BOOL iccEnabled=FALSE,lutEnabled=FALSE;
    int iccIntent=0;
    wchar_t iccPath[1024]={},lutPath[1024]={};
    BOOL adaptiveSharpen=FALSE;float sharpenStrength=.5f;
    BOOL smoothMotion=FALSE;
    int outputLevels=0,displayDepth=0;
    BOOL peakDetect=TRUE;
    float peakSmoothing=20,sceneLow=1,sceneHigh=3,peakPercentile=100;
    float contrastRecovery=0,contrastSmoothness=3.5f;
    int gamutMapping=0;
    float kneeAdaptation=.4f,splineContrast=.5f,kneeMinimum=.1f,kneeMaximum=.8f;
    float blackCutoff=1,perceptualStrength=.8f,softclipKnee=.7f;
    BOOL showClipping=FALSE;
    float hue=0,temperature=0;BOOL colorAdjustment=TRUE;
    BOOL allowDelayedPeak=FALSE;
    int hdrMode=0; // 0=Auto, 1=HDR passthrough, 2=GPU tone mapping to SDR
    ScalingSettings scaling;
    int maximumFramesInFlight=0; // 0=retain the device's original DXGI latency
};
struct PlaceboStatus {
    uint64_t rendered=0,hardwareFrames=0;
    int width=0,height=0;BOOL hdrSource=FALSE;
    HRESULT lastResult=S_OK;
    float masteringMaxNits=0,masteringMinNits=0,maxCLL=0,maxFALL=0;
    uint64_t deinterlacedFields=0,mixedFrames=0;
};
// New IID because the settings structure grew; old clients must fail QI
// rather than receive a larger structure into their smaller buffer.
interface __declspec(uuid("B1924CA6-FCE8-4506-BD4B-1E404ED310C4")) IPlaceboSettings:public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetSettings(PlaceboSettings*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetSettings(const PlaceboSettings*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetStatus(PlaceboStatus*)=0;
    virtual HRESULT STDMETHODCALLTYPE ReadPixels(int width,int height,BYTE* rgba,SIZE_T bytes)=0;
};
PlaceboSettings LoadSettings();
HRESULT SaveSettings(const PlaceboSettings&);
HRESULT ValidateSettings(const PlaceboSettings&);
RenderSettings BackendSettings(const PlaceboSettings&);

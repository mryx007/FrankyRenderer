// Adapted from MPC Video Renderer DisplayConfig/HDR-toggle logic (GPLv3+).
#include "HdrDisplay.h"
#include "Trace.h"
#include <vector>
static bool State(LUID adapter,UINT32 target,bool& supported,bool& enabled){
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 modern={};modern.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;modern.header.size=sizeof(modern);modern.header.adapterId=adapter;modern.header.id=target;
    if(DisplayConfigGetDeviceInfo(&modern.header)==ERROR_SUCCESS){supported=modern.highDynamicRangeSupported;enabled=modern.highDynamicRangeUserEnabled;return true;}
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy={};legacy.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;legacy.header.size=sizeof(legacy);legacy.header.adapterId=adapter;legacy.header.id=target;
    if(DisplayConfigGetDeviceInfo(&legacy.header)!=ERROR_SUCCESS)return false;supported=legacy.advancedColorSupported&&!legacy.advancedColorForceDisabled;enabled=legacy.advancedColorEnabled;return true;
}
bool HdrDisplay::Set(bool enabled){
    Trace("Windows HDR change begin: enabled=%d",enabled);
    DISPLAYCONFIG_SET_HDR_STATE modern={};modern.header.type=DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE;modern.header.size=sizeof(modern);modern.header.adapterId=adapter;modern.header.id=target;modern.enableHdr=enabled;
    LONG result=DisplayConfigSetDeviceInfo(&modern.header);if(result==ERROR_SUCCESS){Trace("Windows HDR change completed");return true;}
    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE legacy={};legacy.header.type=DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;legacy.header.size=sizeof(legacy);legacy.header.adapterId=adapter;legacy.header.id=target;legacy.enableAdvancedColor=enabled;
    result=DisplayConfigSetDeviceInfo(&legacy.header);Trace("Windows HDR legacy change result=%ld",result);return result==ERROR_SUCCESS;
}
void HdrDisplay::Restore(){
    if(changed){bool supported=false,enabled=false;if(State(adapter,target,supported,enabled)&&enabled)Set(false);}
    changed=false;requested=false;monitor=nullptr;
}
bool HdrDisplay::Update(HWND window,bool enable){
    HMONITOR current=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST):nullptr;
    if(current==monitor&&enable==requested)return false;
    bool displayChanged=changed;Restore();monitor=current;requested=enable;if(!enable||!current)return displayChanged;
    MONITORINFOEXW info={};info.cbSize=sizeof(info);if(!GetMonitorInfoW(current,&info))return displayChanged;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;std::vector<DISPLAYCONFIG_MODE_INFO> modes;LONG result=ERROR_INSUFFICIENT_BUFFER;
    for(int attempt=0;attempt<3&&result==ERROR_INSUFFICIENT_BUFFER;attempt++){
        UINT32 pathCount=0,modeCount=0;result=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);if(result!=ERROR_SUCCESS)return displayChanged;
        paths.resize(pathCount);modes.resize(modeCount);result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);if(result==ERROR_SUCCESS)paths.resize(pathCount);
    }
    if(result!=ERROR_SUCCESS)return displayChanged;
    for(const auto& path:paths){
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name={};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);name.header.adapterId=path.sourceInfo.adapterId;name.header.id=path.sourceInfo.id;
        if(DisplayConfigGetDeviceInfo(&name.header)!=ERROR_SUCCESS||_wcsicmp(name.viewGdiDeviceName,info.szDevice))continue;
        adapter=path.targetInfo.adapterId;target=path.targetInfo.id;bool supported=false,enabled=false;if(State(adapter,target,supported,enabled)&&supported&&!enabled){changed=Set(true);displayChanged|=changed;}break;
    }
    return displayChanged;
}

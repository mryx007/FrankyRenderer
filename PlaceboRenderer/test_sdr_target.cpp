#include "Backend.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static void Check(bool ok,const char* text){if(!ok){printf("FAIL %s\n",text);exit(1);}}
static long long Delta(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){long long d=0;for(size_t i=0;i<a.size();i++)if(i%4!=3)d+=abs(int(a[i])-int(b[i]));return d;}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);PlaceboBackend backend;RenderSettings s;s.autoHdr=false;s.ditherMethod=3;s.peakDetect=false;s.colorAdjustment=false;
    VideoFrame f;f.width=f.height=64;f.stride=128;f.format=PixelFormat::P010;f.matrix=PL_COLOR_SYSTEM_BT_709;f.color=pl_color_space_bt709;
    std::vector<uint16_t> input(64*64*3/2,512<<6);for(int y=0;y<64;y++)for(int x=0;x<64;x++)input[y*64+x]=static_cast<uint16_t>((64+x*876/63)<<6);
    auto upload=[&](){Check(SUCCEEDED(backend.Upload(f,reinterpret_cast<uint8_t*>(input.data()),input.size()*2)),"source upload");};
    auto draw=[&](float nits){s.sdrNits=nits;Check(SUCCEEDED(backend.SetSettings(s)),"set target nits");std::vector<uint8_t> out;Check(SUCCEEDED(backend.RenderOffscreen(64,64,out)),"SDR GPU output");return out;};
    for(float metadata:{0.f,100.f,203.f}){f.color.hdr.max_luma=metadata;upload();auto base=draw(203);for(float n:{40.f,100.f,120.f,199.f,200.f,202.f,203.f,204.f,210.f,300.f,1000.f}){auto delta=Delta(base,draw(n));printf("SDR source peak %.0f target %.0f pixel delta %lld\n",metadata,n,delta);Check(delta==0,"SDR output stays unchanged across 203 nits");}}
    f.color=pl_color_space_hdr10;f.color.hdr.max_luma=4000;f.matrix=PL_COLOR_SYSTEM_BT_2020_NC;upload();
    Check(Delta(draw(120),draw(300))>100,"HDR to SDR still uses target nits");
    puts("PASS SDR target: unchanged SDR at 40..1000 nits, including 203 boundary and source metadata variants; HDR to SDR target remains active");
}

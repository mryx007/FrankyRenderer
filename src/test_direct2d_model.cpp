#include "Direct2DSettingsHost.cpp"
#include <stdexcept>

static void Require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int wmain(){
    try{
        PlaceboSettings s;auto pages=BuildD2DPages(s);int controls=0,parameters=0;for(const auto& info:ShaderCatalog)parameters+=info.count;
        Require(pages.size()==8,"all sections");
        Require(MatchesFilter(L"HDR Highlights Exposure",L"  exposure HDR "),"search ignores case and outer spaces; all words match");
        Require(!MatchesFilter(L"HDR Highlights Exposure",L"exposure Gamma"),"search excludes missing terms");
        Require(MatchesFilter(L"Anything",L" \t"),"empty search");
        int exposureHits=0,combinedHits=0;for(const auto& p:pages)for(const auto& card:p.cards)for(const auto& c:card.controls){if(ControlMatches(c,card,p,L"exposure"))exposureHits++;if(ControlMatches(c,card,p,L"highlights exposure"))combinedHits++;}
        Require(exposureHits>=4&&combinedHits==1,"search across sections and group names");
        for(auto& p:pages)for(auto& card:p.cards)for(auto& c:card.controls){
            controls++;if(c.get)Require(std::isfinite(c.Value()),"finite displayed default");
            if(c.desc.empty()){fwprintf(stderr,L"Missing explanation: %ls / %ls / %ls\n",p.name.c_str(),card.title.c_str(),c.name.c_str());Require(false,"every control has an explanation");}
            if(c.kind==ControlKind::SliderInt||c.kind==ControlKind::SliderFloat){
                Require(bool(c.defaultValue),"every slider has a real default");
                if(c.Default()<c.minVal||c.Default()>c.maxVal){fwprintf(stderr,L"Default outside range: %ls / %ls: %g [%g, %g]\n",p.name.c_str(),c.name.c_str(),double(c.Default()),double(c.minVal),double(c.maxVal));Require(false,"default inside supported range");}
                if(c.Default()==c.minVal||c.Default()==c.maxVal){Require(SliderFraction(c,c.minVal)==0&&SliderFraction(c,c.maxVal)==1,"boundary defaults map to slider ends");Require(SliderValue(c,0)==c.minVal&&SliderValue(c,1)==c.maxVal,"boundary slider reaches both bounds");}
                else Require(SliderFraction(c,c.Default())==.5f&&SliderValue(c,.5f)==c.Default(),"interior defaults stay centered, including positive-only ranges");
                for(float f:{0.f,.1f,.25f,.5f,.75f,.9f,1.f}){float v=SliderValue(c,f);Require(std::isfinite(v)&&v>=c.minVal&&v<=c.maxVal,"slider stays in range");}
            }
        }
        auto find=[&](const wchar_t* page,const wchar_t* card,const wchar_t* name)->D2DControl&{for(auto& p:pages)if(p.name==page)for(auto& g:p.cards)if(g.title==card)for(auto& c:g.controls)if(c.name==name)return c;throw std::runtime_error("missing control");};
        auto& algorithm=find(L"Processing",L"Deinterlacing",L"Algorithm");Require(algorithm.Value()==1&&algorithm.choices[1].label==L"Bwdif","Bwdif index");algorithm.set(2);Require(s.deinterlacing==2,"Yadif index");algorithm.reset();
        auto& temp=find(L"Color Adjustment",L"Picture Controls",L"Temperature (K)");Require(temp.maxVal==6500,"temperature capped at neutral white");temp.set(3000);Require(std::abs(s.temperature+1)<1e-6,"Kelvin conversion");Require(temp.Value()==3000,"Kelvin readback");temp.reset();
        auto& tone=find(L"HDR & Color Mapping",L"HDR Output",L"Tone Mapping Function");tone.set(1);Require(s.toneMapping==1&&tone.choices[1].label==L"Spline","Spline index");tone.reset();
        auto& scaler=find(L"Scaling",L"Filters",L"Image Upscaler");scaler.set(2);Require(!strcmp(s.scaling.filters[0].filter,"custom"),"canonical scaling");
        auto& blur=find(L"Scaling",L"Custom: Image Upscaler",L"Blur");Require(blur.Enabled(),"custom editing enabled");blur.set(1.1f);Require((s.scaling.filters[0].overrides&4)&&std::abs(s.scaling.filters[0].values[2]-1.1f)<1e-6,"scaling override");Require(SUCCEEDED(ValidateSettings(s)),"valid custom scaling");
        scaler.reset();Require(!blur.Enabled(),"preset disables editor");
        auto& exposure=find(L"HDR & Color Mapping",L"Highlights",L"Exposure");exposure.set(-.001f);Require(s.tonal.exposure[2]==-.001f&&exposure.stepVal==.001f,"fine highlight adjustment");
        auto& order=find(L"Shaders",L"AMD CAS",L"Chain Position");order.set(20);Require(s.userShaders.order[20]==0&&ValidUserShaders(s.userShaders),"shader reorder permutation");order.reset();
        auto& recovery=find(L"HDR & Color Mapping",L"Contrast Recovery",L"Contrast Recovery");s.contrastRecovery=.5f;Require(recovery.Value()==.5f,"preserve legacy values");recovery.set(.7f);Require(std::abs(D2DMappingValue(s,MappingIndex("contrast_recovery"))-.7f)<1e-6,"mapping override");
        for(auto& p:pages)for(auto& card:p.cards)for(auto& c:card.controls)if(c.reset)c.reset();
        Require(SUCCEEDED(ValidateSettings(s)),"reset entire menu valid");Require(s.colorMapping.values[MappingIndex("contrast_recovery")][0]==0&&s.contrastRecovery==0,"reset override and legacy");
        Require(parameters>60&&controls>200,"shader parameters and advanced options covered");
        HWND parent=CreateWindowExW(0,L"STATIC",L"Hidden menu rendering test",WS_POPUP,-10000,-10000,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(parent!=nullptr,"hidden rendering parent");RECT bounds={0,0,1100,850};HWND view=Direct2DSettingsHost::Create(parent,bounds,&s);
        Require(view!=nullptr,"rendering view");auto host=reinterpret_cast<Direct2DHostWindow*>(GetWindowLongPtrW(view,GWLP_USERDATA));host->Render();
        Require(host->m_pRenderTarget!=nullptr,"Direct2D render target");
        host->Render();Require(host->m_fmtNav->GetTextAlignment()==DWRITE_TEXT_ALIGNMENT_LEADING,"navigation remains left aligned after repeated paint");
        Require(host->m_fmtDesc->GetFontWeight()>=DWRITE_FONT_WEIGHT_MEDIUM,"descriptions use stronger font weight");
        bool resetInContent=false;for(const auto& hit:host->m_hitRegions)if(hit.type==HitRegion::ResetBtn)resetInContent=hit.rect.left>240;Require(resetInContent,"reset belongs to content header");
        bool scrollbar=false,apply=false;for(const auto& hit:host->m_hitRegions){scrollbar|=hit.type==HitRegion::Scrollbar&&hit.pageIndex==-1;apply|=hit.type==HitRegion::ApplyBtn;}Require(scrollbar&&!apply,"visible page scrollbar and no live preview button");
        for(int index:{3,4,6}){host->m_activePage=index;host->m_scrollY=0;host->Render();Require(host->m_maxScrollY==0,"short sections fit without unnecessary scrolling");}
        host->m_activePage=7;host->Render();int shaderRows=0,enabledSwitches=0;bool shaderScrollbar=false;for(const auto& hit:host->m_hitRegions){shaderRows+=hit.type==HitRegion::ShaderItem;enabledSwitches+=hit.type==HitRegion::Toggle;shaderScrollbar|=hit.type==HitRegion::Scrollbar&&hit.pageIndex==-2;}
        Require(shaderRows==7&&enabledSwitches==1&&shaderScrollbar,"compact shader list and only selected shader editor");
        host->m_shaderOffset=ShaderCount-7;host->m_selectedShader=20;host->Render();Require(host->m_shaderList.right>host->m_shaderList.left,"last shader reachable");
        Direct2DSettingsHost::Destroy(view);DestroyWindow(parent);
        printf("Direct2D model: %d controls, %d shader parameters; search, explanations, slider ranges, bindings, reset and ordering passed.\n",controls,parameters);return 0;
    }catch(const std::exception& e){fprintf(stderr,"Direct2D model FAILED: %s\n",e.what());return 1;}
}

#pragma once
#include <libplacebo/options.h>
#include <cmath>
#include <charconv>
#include <string>
struct ExtraOption {const char* key;const wchar_t* label;float minimum,maximum,step;bool integer;const wchar_t* help;};
inline constexpr ExtraOption ExtraOptions[]={
 {"background_r",L"Background Red",0,1,.05f,false,L"Red channel for uncovered borders and transparent pixels. RGB range: 0 to 1."},
 {"background_g",L"Background Green",0,1,.05f,false,L"Green channel for uncovered borders and transparent pixels. RGB range: 0 to 1."},
 {"background_b",L"Background Blue",0,1,.05f,false,L"Blue channel for uncovered borders and transparent pixels. RGB range: 0 to 1."},
 {"background_transparency",L"Background Transparency",0,1,.05f,false,L"0 is opaque; 1 is transparent. Changes rendered alpha, not Windows desktop/window transparency."},
 {"corner_rounding",L"Corner Rounding",0,1,.05f,false,L"Relative amount of rounded video corners. 0 keeps square corners; 1 applies the maximum rounding."},
 {"skip_target_clearing",L"Skip Target Clearing",0,1,1,true,L"Skip clearing uncovered output borders. May leave old or undefined pixels there; no benefit for full-frame video. Default is Off."},
 {"blend_against_tiles",L"Blend Against Tiles",0,1,1,true,L"Composite transparent image regions onto a checkerboard. Default is Off. Tile colors do not recolor opaque video."},
 {"tile_color_hi_r",L"Light Tile Red",0,1,.05f,false,L"Red channel of the light checkerboard tiles. Used with Blend Against Tiles."},
 {"tile_color_hi_g",L"Light Tile Green",0,1,.05f,false,L"Green channel of the light checkerboard tiles. Used with Blend Against Tiles."},
 {"tile_color_hi_b",L"Light Tile Blue",0,1,.05f,false,L"Blue channel of the light checkerboard tiles. Used with Blend Against Tiles."},
 {"tile_color_lo_r",L"Dark Tile Red",0,1,.05f,false,L"Red channel of the dark checkerboard tiles. Used with Blend Against Tiles."},
 {"tile_color_lo_g",L"Dark Tile Green",0,1,.05f,false,L"Green channel of the dark checkerboard tiles. Used with Blend Against Tiles."},
 {"tile_color_lo_b",L"Dark Tile Blue",0,1,.05f,false,L"Blue channel of the dark checkerboard tiles. Used with Blend Against Tiles."},
 {"tile_size",L"Tile Size (pixels)",2,256,1,true,L"Checkerboard repeat size in output pixels. Default: 32; manual range: 2 to 256."},
 {"lut_type",L"LUT Type",0,3,1,true,L"Default keeps the existing SDR calibration LUT stage. Manual types use this file as a color-mapping LUT. Normalized uses linear-light RGB; Conversion replaces color conversion, tone mapping and ICC correction. Match the LUT's intended data representation."},
 {"skip_anti_aliasing",L"Skip Downscale Anti-Aliasing",0,1,1,true,L"Faster downscaling by skipping anti-aliasing. Can introduce moire and jagged edges. Default: Off; leave Off for best quality."},
 {"preserve_mixing_cache",L"Preserve Frame Mixer Cache",0,1,1,true,L"Reuse mixed frames after resizing or parameter changes. May make transitions smoother but briefly reuse lower-quality or older processing. Default: Off; used with Smooth Motion."},
};
inline constexpr int ExtraCount=sizeof(ExtraOptions)/sizeof(ExtraOptions[0]);
// Persisted indices are append-only. -1 means fixed library Default.
struct RendererExtras {float values[ExtraCount]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};};
inline bool ValidExtras(const RendererExtras& s){
    for(int i=0;i<ExtraCount;i++){const float v=s.values[i];const auto& o=ExtraOptions[i];if(!std::isfinite(v)||(v!=-1&&(v<o.minimum||v>o.maximum||(o.integer&&std::floor(v)!=v))))return false;}return true;
}
inline bool ApplyExtras(pl_options options,const RendererExtras& s){
    if(!ValidExtras(s))return false;
    for(int i=0;i<ExtraCount;i++){
        const float value=s.values[i];if(value<0)continue;
        // Public modern equivalents of the deprecated boolean aliases.
        if(i==5){if(!pl_options_set_str(options,"border",value?"skip":"color"))return false;continue;}
        if(i==6){if(!pl_options_set_str(options,"background",value?"tiles":"color"))return false;continue;}
        if(i==14){constexpr const char* types[]={"unknown","native","normalized","conversion"};if(!pl_options_set_str(options,"lut_type",types[static_cast<int>(value)]))return false;continue;}
        if(i>=15){if(!pl_options_set_str(options,ExtraOptions[i].key,value?"yes":"no"))return false;continue;}
        char text[32];auto end=std::to_chars(text,text+31,value);if(end.ec!=std::errc{})return false;*end.ptr=0;
        if(!pl_options_set_str(options,ExtraOptions[i].key,text))return false;
    }
    return true;
}

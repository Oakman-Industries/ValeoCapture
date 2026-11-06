// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/raw_pipeline.hpp"
#include "valeo/capture_config.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
using namespace valeo;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
// Independent scalar reference for tone-table acceleration, edge interpolation,
// software exposure, per-channel WB and gamma. Expected values are not table copies.
std::vector<std::uint16_t> scalar(const std::vector<std::uint16_t>& raw,const RawMetadata& m,const std::array<double,3>& gamma) {
    std::vector<std::uint16_t> out(raw.size()*3);
    for(int y=0;y<int(m.height);++y)for(int x=0;x<int(m.width);++x)for(unsigned c=0;c<3;++c) {
        double sum=0;unsigned n=0;
        if(m.cfa[(y%2)*2+x%2]==c){sum=raw[std::size_t(y)*m.width+unsigned(x)];n=1;}
        else for(int yy=std::max(0,y-1);yy<=std::min(int(m.height)-1,y+1);++yy)
            for(int xx=std::max(0,x-1);xx<=std::min(int(m.width)-1,x+1);++xx)
                if(m.cfa[(yy%2)*2+xx%2]==c){sum+=raw[std::size_t(yy)*m.width+unsigned(xx)];++n;}
        const auto v=std::clamp((sum/n-m.black_level)/(m.white_level-m.black_level)/m.as_shot_neutral[c]*std::exp2(m.baseline_exposure_ev),0.0,1.0);
        out[(std::size_t(y)*m.width+unsigned(x))*3+c]=static_cast<std::uint16_t>(std::llround(std::pow(v,1/gamma[c])*65535));
    }
    return out;
}
int main(){try{
    Bytes light(reference_frame_bytes),dark(reference_frame_bytes);
    for(std::size_t i=0;i<light.size();i+=2){light[i]=3;light[i+1]=232;dark[i+1]=100;}
    const auto first=2*(std::size_t(2677)*4056+4031);light[first]=9;light[first+1]=95; //2399
    const auto last=2*(std::size_t(6)*4056+24);light[last]=0;light[last+1]=50;
    auto corrected=subtract_valeo11_pair(light,dark);
    require(corrected.size()==4008u*2672u && corrected.front()==2299 && corrected.back()==0 && corrected[1]==900,"Crop/normalization/dark floor disagrees with sensor coordinates");
    light[first]=255;light[first+1]=255;bool rejected=false;
    try{subtract_valeo11_pair(light,dark);}catch(const Error&){rejected=true;}require(rejected,"Out-of-range native frame was corrected");
    RawMetadata m;m.width=4;m.height=4;m.white_level=4000;m.cfa={2,1,1,0};m.as_shot_neutral={1,1,1};
    std::vector<std::uint16_t> flat(16);const std::uint16_t levels[]={1000,2000,3000};
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)flat[y*4+x]=levels[m.cfa[(y%2)*2+x%2]];
    const auto rgb=demosaic_camera_rgb(flat,m,{1,1,1});
    for(unsigned i=0;i<16;++i)for(unsigned c=0;c<3;++c)require(std::abs(int(rgb[i*3+c])-int(std::llround(levels[c]/4000.0*65535)))<=1,"Flat-color Bayer channels/edges changed");
    const auto encoded=demosaic_camera_rgb(flat,m,{2,2,2});require(encoded[0]>rgb[0],"ICC encoding was omitted");
    for(auto iso:valeo11_iso_values) {
        m.baseline_exposure_ev=valeo11_software_exposure_stops(iso);
        const auto gained=demosaic_camera_rgb(flat,m,{1,1,1});
        const auto expected=std::llround(std::min(1.0,0.25*(iso/25))*65535);
        require(gained[0]==expected,"Software ISO factor/clipping differs from Leaf stops");
        require(flat[0]==3000,"Software ISO modified the original mosaic");
    }
    bool invalid_iso=false;try{valeo11_software_exposure_stops(400);}catch(const Error&){invalid_iso=true;}require(invalid_iso,"Unlisted software gain accepted");
    m.width=5;m.height=6;m.black_level=43;m.white_level=4000;m.as_shot_neutral={.8,1,.6};
    std::vector<std::uint16_t> varied(30);for(unsigned i=0;i<varied.size();++i)varied[i]=static_cast<std::uint16_t>((i*389+19)%1700);
    const std::array<double,3> curves={1.80078125,2.2,1};
    for(auto cfa:{std::array<std::uint8_t,4>{2,1,1,0},std::array<std::uint8_t,4>{0,1,1,2},std::array<std::uint8_t,4>{1,2,0,1},std::array<std::uint8_t,4>{1,0,2,1}}) {
        m.cfa=cfa;
        for(auto ev:{-1.0,0.0,1.0,3.0}){m.baseline_exposure_ev=ev;require(demosaic_camera_rgb(varied,m,curves)==scalar(varied,m,curves),"Accelerated render changed scalar samples/edges/gain");}
    }
    m.baseline_exposure_ev=std::numeric_limits<double>::infinity();rejected=false;
    try{demosaic_camera_rgb(varied,m,curves);}catch(const Error&){rejected=true;}require(rejected,"Nonfinite software exposure accepted");
    m.width=4;m.height=4;m.baseline_exposure_ev=0;
    m.as_shot_neutral[0]=0;rejected=false;try{demosaic_camera_rgb(flat,m,{1,1,1});}catch(const Error&){rejected=true;}require(rejected,"Unknown neutral accepted");
    std::cout<<"PASS paired sensor geometry, dark floor/range guard, Bayer channels/edges and explicit RGB encoding\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

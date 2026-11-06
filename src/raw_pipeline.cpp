// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/raw_pipeline.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/raw_validation.hpp"
#include <algorithm>
#include <cmath>

namespace valeo {
double valeo11_software_exposure_stops(std::uint16_t iso) {
    switch(iso){case 25:return 0;case 50:return 1;case 100:return 2;case 200:return 3;
        default:throw Error(ErrorCode::unsupported,"Valeo11 software gain supports ISO25,50,100,200");}
}
std::vector<std::uint16_t> subtract_valeo11_pair(const Bytes& light,const Bytes& auxiliary) {
    for(const auto* plane:{&light,&auxiliary}) {
        Raw14FrameValidator validator(reference_frame_bytes);validator.append(*plane);validator.finish();
    }
    constexpr std::uint32_t width=4008,height=2672;
    std::vector<std::uint16_t> result(std::size_t(width)*height);
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
        const auto i=2*(std::size_t(2677-y)*reference_width+4031-x);
        const int l=int(light[i])*256+light[i+1],d=int(auxiliary[i])*256+auxiliary[i+1];
        result[std::size_t(y)*width+x]=static_cast<std::uint16_t>(std::max(0,l-d));
    }
    return result;
}
std::vector<std::uint16_t> demosaic_camera_rgb(const std::vector<std::uint16_t>& raw,const RawMetadata& m,const std::array<double,3>& gamma) {
    const auto n=std::uint64_t(m.width)*m.height;
    if(m.width<2 || m.height<2 || n>32ull*1024*1024 || raw.size()!=n || m.black_level>=m.white_level || m.white_level>65535)
        throw Error(ErrorCode::invalid,"Invalid demosaic dimensions or levels");
    std::array<unsigned,3> colors{};
    for(auto c:m.cfa){if(c>2)throw Error(ErrorCode::invalid,"Invalid CFA channel");++colors[c];}
    if(colors!=std::array<unsigned,3>{1,2,1} || !((m.cfa[0]==1 && m.cfa[3]==1) || (m.cfa[1]==1 && m.cfa[2]==1)))
        throw Error(ErrorCode::invalid,"A Bayer RGB CFA is required");
    for(unsigned c=0;c<3;++c)if(!std::isfinite(gamma[c]) || gamma[c]<0.1 || gamma[c]>8 || !std::isfinite(m.as_shot_neutral[c]) || m.as_shot_neutral[c]<=0)
        throw Error(ErrorCode::invalid,"Explicit finite white balance and ICC gamma are required");
    if(!std::isfinite(m.baseline_exposure_ev) || std::abs(m.baseline_exposure_ev)>10)
        throw Error(ErrorCode::invalid,"Software exposure outside bounds");
    const double gain=std::exp2(m.baseline_exposure_ev);
    // Exact integer-sum/count tone tables replace millions of repeated powers.
    // Bayer interpolation uses at most4 neighbors. This retains scalar rounding
    // and applies gain before WB/gamma/clipping, with no noise synthesis.
    const auto maximum=*std::max_element(raw.begin(),raw.end());
    std::array<std::array<std::vector<std::uint16_t>,5>,3> encoded;
    for(unsigned c=0;c<3;++c)for(unsigned count=1;count<=4;++count) {
        auto& table=encoded[c][count];table.resize(std::size_t(maximum)*count+1);
        for(std::size_t sum=0;sum<table.size();++sum) {
            const double linear=std::clamp((double(sum)/count-m.black_level)/(m.white_level-m.black_level)/m.as_shot_neutral[c]*gain,0.0,1.0);
            table[sum]=static_cast<std::uint16_t>(std::llround(std::pow(linear,1/gamma[c])*65535));
        }
    }
    std::vector<std::uint16_t> rgb(static_cast<std::size_t>(n)*3);
    const auto color=[&](std::uint32_t x,std::uint32_t y){return m.cfa[(y%2)*2+x%2];};
    for(std::uint32_t y=0;y<m.height;++y)for(std::uint32_t x=0;x<m.width;++x)for(unsigned c=0;c<3;++c) {
        unsigned sum=0,count=0;
        if(color(x,y)==c){sum=raw[std::size_t(y)*m.width+x];count=1;}
        else for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const auto xx=static_cast<std::int64_t>(x)+dx,yy=static_cast<std::int64_t>(y)+dy;
            if(xx<0 || yy<0 || xx>=m.width || yy>=m.height)continue;
            const auto ux=static_cast<std::uint32_t>(xx),uy=static_cast<std::uint32_t>(yy);
            if(color(ux,uy)==c){sum+=raw[std::size_t(uy)*m.width+ux];++count;}
        }
        if(!count)throw Error(ErrorCode::protocol,"Missing Bayer interpolation neighbors");
        rgb[(std::size_t(y)*m.width+x)*3+c]=encoded[c][count][sum];
    }
    return rgb;
}
} // namespace valeo

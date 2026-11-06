// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/color.hpp"
#include "valeo/protocol.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <cmath>
#include <algorithm>

namespace valeo {
namespace {
template<class T,void(*Release)(T)> class Owned {
    T value_{};
public: explicit Owned(T p):value_(p){}~Owned(){if(value_)Release(value_);}T get()const{return value_;}Owned(const Owned&)=delete;
    Owned& operator=(const Owned&)=delete;
};
using Space=Owned<CGColorSpaceRef,CGColorSpaceRelease>;
void release_data(CFDataRef p){CFRelease(p);}
using Data=Owned<CFDataRef,release_data>;
CGColorSpaceRef load_profile(const Bytes& bytes){
    if(bytes.size()<128||bytes.size()>16*1024*1024||read_be32(bytes,0)!=bytes.size()||
       std::string(bytes.begin()+36,bytes.begin()+40)!="acsp"||std::string(bytes.begin()+16,bytes.begin()+20)!="RGB ")
        throw Error(ErrorCode::invalid,"ICC profile header/size is not a bounded RGB profile");
    Data data(CFDataCreate(kCFAllocatorDefault,bytes.data(),static_cast<CFIndex>(bytes.size())));
    auto space=CGColorSpaceCreateWithICCData(data.get());
    if(!space||CGColorSpaceGetModel(space)!=kCGColorSpaceModelRGB||CGColorSpaceGetNumberOfComponents(space)!=3){if(space)CGColorSpaceRelease(space);throw Error(ErrorCode::invalid,"A valid three-channel RGB ICC profile is required");}
    return space;
}
Bytes profile_bytes(CGColorSpaceRef space){Data d(CGColorSpaceCopyICCData(space));if(!d.get())throw Error(ErrorCode::io,"Cannot serialize output ICC profile");return Bytes(CFDataGetBytePtr(d.get()),CFDataGetBytePtr(d.get())+CFDataGetLength(d.get()));}
}
Bytes srgb_profile(){Space space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));return profile_bytes(space.get());}
void validate_rgb_profile(const Bytes& b){Space space(load_profile(b));}
std::array<double,3> rgb_profile_gamma(const Bytes& b) {
    validate_rgb_profile(b);
    const auto count=read_be32(b,128);
    if(count>4096 || 132ull+12ull*count>b.size())throw Error(ErrorCode::invalid,"ICC tag directory exceeds bounds");
    std::array<double,3> gamma{};
    const std::array<std::string,3> names={"rTRC","gTRC","bTRC"};
    for(std::uint32_t i=0;i<count;++i) {
        const auto entry=std::size_t(132)+12*i;
        const std::string name(b.begin()+static_cast<std::ptrdiff_t>(entry),b.begin()+static_cast<std::ptrdiff_t>(entry+4));
        for(unsigned c=0;c<3;++c)if(name==names[c]) {
            const auto offset=read_be32(b,entry+4),size=read_be32(b,entry+8);
            if(gamma[c] || size<14 || std::uint64_t(offset)+size>b.size() ||
               std::string(b.begin()+offset,b.begin()+offset+4)!="curv" || read_be32(b,offset+8)!=1)
                throw Error(ErrorCode::unsupported,"Leaf preset requires three unique single-gamma RGB curves");
            gamma[c]=(unsigned(b[offset+12])*256+b[offset+13])/256.0;
            if(gamma[c]<0.1 || gamma[c]>8)throw Error(ErrorCode::invalid,"ICC input gamma outside bounds");
        }
    }
    for(auto g:gamma)if(!g)throw Error(ErrorCode::unsupported,"ICC input encoding is not specified by RGB gamma curves");
    return gamma;
}
RenderedRgb render_profiled_rgb(std::uint32_t w,std::uint32_t h,const std::vector<std::uint16_t>& rgb,const Bytes& icc){
    const auto n=std::uint64_t(w)*h;if(!w||!h||n>32ull*1024*1024||rgb.size()!=n*3)throw Error(ErrorCode::invalid,"RGB dimensions/data exceed render limits");
    Space source_space(load_profile(icc));Space destination_space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    Owned<CGDataProviderRef,CGDataProviderRelease> provider(CGDataProviderCreateWithData(nullptr,rgb.data(),rgb.size()*2,nullptr));
    if(!provider.get())throw Error(ErrorCode::io,"Cannot create image data provider");
    Owned<CGImageRef,CGImageRelease> image(CGImageCreate(w,h,16,48,std::size_t(w)*6,source_space.get(),kCGBitmapByteOrder16Little|kCGImageAlphaNone,provider.get(),nullptr,false,kCGRenderingIntentRelativeColorimetric));
    if(!image.get())throw Error(ErrorCode::unsupported,"CoreGraphics rejected 16-bit camera RGB");
    std::vector<float> rgba(static_cast<std::size_t>(n)*4);
    const auto flags=static_cast<CGBitmapInfo>(kCGBitmapFloatComponents|kCGBitmapByteOrder32Host|kCGImageAlphaNoneSkipLast);
    Owned<CGContextRef,CGContextRelease> context(CGBitmapContextCreate(rgba.data(),w,h,32,std::size_t(w)*16,destination_space.get(),flags));
    if(!context.get())throw Error(ErrorCode::unsupported,"Cannot create floating point color transform context");
    CGContextSetRenderingIntent(context.get(),kCGRenderingIntentRelativeColorimetric);
    CGContextSetBlendMode(context.get(),kCGBlendModeCopy);
    CGContextDrawImage(context.get(),CGRectMake(0,0,w,h),image.get());
    RenderedRgb result;result.pixels.resize(rgb.size());result.output_icc=profile_bytes(destination_space.get());
    for(std::size_t i=0;i<static_cast<std::size_t>(n);++i)for(std::size_t c=0;c<3;++c){const double x=rgba[i*4+c];if(!std::isfinite(x))throw Error(ErrorCode::protocol,"Non-finite color transform result");result.pixels[i*3+c]=static_cast<std::uint16_t>(std::llround(std::clamp(x,0.0,1.0)*65535));}
    return result;
}
} // namespace valeo

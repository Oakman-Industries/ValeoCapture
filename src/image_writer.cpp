// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/image_writer.hpp"
#include "valeo/protocol.hpp"
#include "valeo/journal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>

namespace valeo {
namespace {
void u16(Bytes& b,std::uint16_t n){b.push_back(static_cast<std::uint8_t>(n));b.push_back(static_cast<std::uint8_t>(n>>8));}
void u32(Bytes& b,std::uint32_t n){u16(b,static_cast<std::uint16_t>(n));u16(b,static_cast<std::uint16_t>(n>>16));}
void set16(Bytes& b,std::size_t o,std::uint16_t n){b.at(o)=static_cast<std::uint8_t>(n);b.at(o+1)=static_cast<std::uint8_t>(n>>8);}
void set32(Bytes& b,std::size_t o,std::uint32_t n){set16(b,o,static_cast<std::uint16_t>(n));set16(b,o+2,static_cast<std::uint16_t>(n>>16));}
struct Entry {std::uint16_t tag,type;std::uint32_t count;Bytes data;};
Entry shorts(std::uint16_t tag,std::initializer_list<std::uint16_t> v){Bytes b;for(auto x:v)u16(b,x);return {tag,3,static_cast<std::uint32_t>(v.size()),std::move(b)};}
Entry longs(std::uint16_t tag,std::initializer_list<std::uint32_t> v){Bytes b;for(auto x:v)u32(b,x);return {tag,4,static_cast<std::uint32_t>(v.size()),std::move(b)};}
Entry bytes(std::uint16_t tag,Bytes b,std::uint16_t type=1){auto n=static_cast<std::uint32_t>(b.size());return {tag,type,n,std::move(b)};}
Entry text(std::uint16_t tag,const std::string& s){if(s.empty()||s.size()>1024||std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32||c>126;}))throw Error(ErrorCode::invalid,"Missing/invalid TIFF ASCII metadata");Bytes b(s.begin(),s.end());b.push_back(0);return bytes(tag,std::move(b),2);}
Entry rationals(std::uint16_t tag,const double* v,std::size_t n,bool signed_value){Bytes b;for(std::size_t i=0;i<n;++i){if(!std::isfinite(v[i])||std::abs(v[i])>1000||(!signed_value&&v[i]<0))throw Error(ErrorCode::invalid,"Invalid DNG rational");auto num=static_cast<std::int32_t>(std::llround(v[i]*1000000));u32(b,static_cast<std::uint32_t>(num));u32(b,1000000);}return {tag,static_cast<std::uint16_t>(signed_value?10:5),static_cast<std::uint32_t>(n),std::move(b)};}
bool valid_orientation(std::uint16_t o){return o==1||o==3||o==6||o==8;}
std::uint32_t image_bytes(std::uint32_t w,std::uint32_t h,std::uint32_t channels,std::size_t samples){const auto pixels=std::uint64_t(w)*h;if(!w||!h||pixels>128ull*1024*1024||pixels*channels!=samples||pixels*channels*2>511ull*1024*1024)throw Error(ErrorCode::invalid,"Image dimensions/data exceed bounded writer limits");return static_cast<std::uint32_t>(pixels*channels*2);}
void emit(const std::filesystem::path& path,std::vector<Entry> entries,const std::vector<std::uint16_t>& samples,std::uint16_t iso){
    if(iso)entries.push_back(longs(34665,{0}));
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.tag<b.tag;});
    const auto n=static_cast<std::uint16_t>(entries.size());
    const auto exif=static_cast<std::uint32_t>(8+2+12*n+4);
    const auto metadata_start=(std::size_t(exif)+(iso?18:0)+3)&~std::size_t(3);
    Bytes b(metadata_start,0);b[0]='I';b[1]='I';set16(b,2,42);set32(b,4,8);set16(b,8,n);
    if(iso){set16(b,exif,1);set16(b,exif+2,34855);set16(b,exif+4,3);set32(b,exif+6,1);set16(b,exif+10,iso);}
    std::size_t strip_slot=0;
    for(std::size_t i=0;i<entries.size();++i){auto& e=entries[i];auto p=10+12*i;set16(b,p,e.tag);set16(b,p+2,e.type);set32(b,p+4,e.count);
        if(e.tag==34665){set32(b,p+8,exif);continue;}
        if(e.data.size()<=4)std::copy(e.data.begin(),e.data.end(),b.begin()+static_cast<std::ptrdiff_t>(p+8));
        else{while(b.size()%4)b.push_back(0);set32(b,p+8,static_cast<std::uint32_t>(b.size()));b.insert(b.end(),e.data.begin(),e.data.end());}
        if(e.tag==273)strip_slot=p+8;
    }
    while(b.size()%4)b.push_back(0);
    if(!strip_slot)throw Error(ErrorCode::invalid,"Missing TIFF strip offset");
    set32(b,strip_slot,static_cast<std::uint32_t>(b.size()));
    if(b.size()+samples.size()*2>512ull*1024*1024)throw Error(ErrorCode::invalid,"Image plus metadata exceeds journal size limit");
    Journal journal(path,b.size()+samples.size()*2);journal.append(b);
    // Bounded chunks avoid constructing a second full-size image buffer.
    Bytes chunk;chunk.reserve(0x80000);
    for(auto x:samples){u16(chunk,x);if(chunk.size()==0x80000){journal.append(chunk);chunk.clear();}}
    if(!chunk.empty())journal.append(chunk);
    journal.commit();
}
Bytes provenance_mark(){
    Bytes b;for(const char* s:{provenance_owner,provenance_payload}){for(;*s;++s)b.push_back(static_cast<std::uint8_t>(*s));b.push_back(0);}
    return b;
}
std::vector<Entry> common(std::uint32_t w,std::uint32_t h,std::uint16_t o,std::uint32_t size,std::uint16_t channels){if(!valid_orientation(o))throw Error(ErrorCode::invalid,"Unsupported orientation");return {longs(254,{0}),longs(256,{w}),longs(257,{h}),shorts(259,{1}),longs(273,{0}),shorts(274,{o}),shorts(277,{channels}),longs(278,{h}),longs(279,{size}),shorts(284,{1}),text(305,"ValeoCapture 0.1.1"),bytes(provenance_tag,provenance_mark())};}
}
bool has_provenance(const std::filesystem::path& path){
    // Bounded read of a little-endian TIFF/DNG header and first IFD only.
    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec||size<16)return false;
    std::FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;
    auto read=[&](std::uint64_t offset,std::size_t n,Bytes& out){out.assign(n,0);return offset+n<=size && std::fseek(f,static_cast<long>(offset),SEEK_SET)==0 && std::fread(out.data(),1,n,f)==n;};
    auto le16=[](const Bytes& b,std::size_t o){return static_cast<std::uint16_t>(b[o]|(b[o+1]<<8));};
    auto le32=[](const Bytes& b,std::size_t o){return static_cast<std::uint32_t>(b[o]|(b[o+1]<<8)|(b[o+2]<<16)|(std::uint32_t(b[o+3])<<24));};
    bool found=false;Bytes h,count,entries,value;
    if(read(0,8,h) && h[0]=='I' && h[1]=='I' && le16(h,2)==42) {
        const auto ifd=le32(h,4);
        if(read(ifd,2,count)) {
            const auto n=le16(count,0);
            if(n<=512 && read(ifd+2,std::size_t(n)*12,entries)) {
                const auto mark=provenance_mark();
                for(std::uint16_t i=0;i<n && !found;++i){
                    const auto p=std::size_t(i)*12;
                    if(le16(entries,p)!=provenance_tag)continue;
                    if(le16(entries,p+2)!=1 || le32(entries,p+4)!=mark.size())break;
                    found=mark.size()<=4?std::equal(mark.begin(),mark.end(),entries.begin()+static_cast<std::ptrdiff_t>(p+8))
                                         :read(le32(entries,p+8),mark.size(),value) && value==mark;
                }
            }
        }
    }
    std::fclose(f);return found;
}
void validate_raw_metadata(const RawMetadata& m){
    if(!std::isfinite(m.baseline_exposure_ev) || std::abs(m.baseline_exposure_ev)>10)
        throw Error(ErrorCode::invalid,"DNG baseline exposure outside bounds");
    image_bytes(m.width,m.height,1,static_cast<std::size_t>(std::uint64_t(m.width)*m.height));
    if(!valid_orientation(m.orientation))throw Error(ErrorCode::invalid,"Unsupported orientation");
    if(m.black_level>=m.white_level||m.white_level>65535)throw Error(ErrorCode::invalid,"Unvalidated sensor levels");
    unsigned colors[3]{};for(auto c:m.cfa){if(c>2)throw Error(ErrorCode::invalid,"Only validated RGB CFA tiles supported");++colors[c];}
    if(colors[0]!=1||colors[1]!=2||colors[2]!=1)throw Error(ErrorCode::invalid,"Expected validated 2x2 RGB CFA");
    const auto& a=m.active_area;if(a[0]>=a[2]||a[1]>=a[3]||a[2]>m.height||a[3]>m.width)throw Error(ErrorCode::invalid,"Active area outside sensor");
    std::array<double,9> encoded{};for(std::size_t i=0;i<encoded.size();++i){const auto v=m.xyz_to_camera[i];if(!std::isfinite(v)||std::abs(v)>1000)throw Error(ErrorCode::invalid,"Invalid camera color matrix");encoded[i]=std::round(v*1000000)/1000000;}
    const auto& c=encoded;const double determinant=c[0]*(c[4]*c[8]-c[5]*c[7])-c[1]*(c[3]*c[8]-c[5]*c[6])+c[2]*(c[3]*c[7]-c[4]*c[6]);
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-10)throw Error(ErrorCode::invalid,"Missing or singular camera color matrix");
    for(auto n:m.as_shot_neutral)if(!std::isfinite(n)||n<=0||n>1)throw Error(ErrorCode::invalid,"Invalid as-shot neutral");
}
void write_dng(const std::filesystem::path& path,const RawMetadata& m,const std::vector<std::uint16_t>& pixels){
    validate_raw_metadata(m);
    const auto size=image_bytes(m.width,m.height,1,pixels.size());
    const auto& a=m.active_area;
    std::array<double,9> c{};for(std::size_t i=0;i<c.size();++i)c[i]=std::round(m.xyz_to_camera[i]*1000000)/1000000;
    auto e=common(m.width,m.height,m.orientation,size,1);
    e.push_back(shorts(258,{16}));e.push_back(shorts(262,{32803}));e.push_back(text(271,m.make));e.push_back(text(272,m.model));
    e.push_back(shorts(33421,{2,2}));e.push_back(bytes(33422,Bytes(m.cfa.begin(),m.cfa.end())));
    e.push_back(bytes(50706,{1,4,0,0}));e.push_back(bytes(50707,{1,1,0,0}));
    e.push_back(text(50708,m.unique_camera_model.empty()?m.make+" "+m.model:m.unique_camera_model));
    e.push_back(bytes(50710,{0,1,2}));e.push_back(shorts(50711,{1}));
    e.push_back(longs(50714,{m.black_level}));e.push_back(longs(50717,{m.white_level}));
    // DNG DefaultCropOrigin is relative to ActiveArea, not the sensor origin.
    e.push_back(longs(50719,{0,0}));e.push_back(longs(50720,{a[3]-a[1],a[2]-a[0]}));
    e.push_back(rationals(50721,c.data(),9,true));e.push_back(rationals(50728,m.as_shot_neutral.data(),3,false));
    e.push_back(rationals(50730,&m.baseline_exposure_ev,1,true));
    e.push_back(shorts(50778,{m.illuminant}));e.push_back(longs(50829,{a[0],a[1],a[2],a[3]}));
    if(!m.serial.empty())e.push_back(text(50735,m.serial));
    emit(path,std::move(e),pixels,m.iso);
}
void write_rgb_tiff(const std::filesystem::path& path,std::uint32_t w,std::uint32_t h,std::uint16_t orientation,const std::vector<std::uint16_t>& rgb,const Bytes& icc){
    const auto size=image_bytes(w,h,3,rgb.size());
    if(icc.size()<128||icc.size()>16*1024*1024||read_be32(icc,0)!=icc.size()||std::string(icc.begin()+36,icc.begin()+40)!="acsp"||std::string(icc.begin()+16,icc.begin()+20)!="RGB ")throw Error(ErrorCode::invalid,"A valid RGB output ICC profile is required");
    auto e=common(w,h,orientation,size,3);e.push_back(shorts(258,{16,16,16}));e.push_back(shorts(262,{2}));e.push_back(bytes(34675,icc,7));emit(path,std::move(e),rgb,0);
}
RawMetadata synthetic_metadata(){RawMetadata m;m.width=64;m.height=64;m.black_level=0;m.white_level=65535;m.cfa={0,1,1,2};m.xyz_to_camera={1,0,0,0,1,0,0,0,1};m.as_shot_neutral={1,1,1};m.active_area={0,0,64,64};m.make="ValeoCapture";m.model="Synthetic Fixture";return m;}
std::vector<std::uint16_t> synthetic_mosaic(){std::vector<std::uint16_t> p(64*64);std::uint32_t state=0x12345678;for(auto& x:p){state=1664525u*state+1013904223u;x=static_cast<std::uint16_t>(state>>16);}return p;}
} // namespace valeo

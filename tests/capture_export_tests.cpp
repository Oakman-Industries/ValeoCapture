// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/capture_export.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/raw_pipeline.hpp"
#include "valeo/color.hpp"
#include "valeo/protocol.hpp"
#include "valeo/image_writer.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
static valeo::Bytes read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return valeo::Bytes((std::istreambuf_iterator<char>(f)),{});}
int main(){try{
    char path[]="/tmp/valeo-export-XXXXXX";const auto folder=mkdtemp(path);require(folder,"temporary export directory");
    const std::filesystem::path base(folder);
    const double white[]={0.95047,1,1.08883},gamma[]={1.8,1.8,1.8};
    const double matrix[]={0.4124564,0.2126729,0.0193339,0.3575761,0.7151522,0.1191920,0.1804375,0.0721750,0.9503041};
    auto space=CGColorSpaceCreateCalibratedRGB(white,nullptr,gamma,matrix);require(space,"synthetic RGB space");
    auto data=CGColorSpaceCopyICCData(space);require(data,"synthetic ICC serialization");
    valeo::Bytes icc(CFDataGetBytePtr(data),CFDataGetBytePtr(data)+CFDataGetLength(data));CFRelease(data);CGColorSpaceRelease(space);
    // Replace the generated sampled TRCs with explicit public single-gamma
    // curves. This fixture contains no vendor bytes or per-camera metadata.
    const auto be32=[&](std::size_t at,std::uint32_t v){for(unsigned c=0;c<4;++c)icc[at+c]=std::uint8_t(v>>(24-8*c));};
    const auto tags=valeo::read_be32(icc,128);
    for(std::size_t i=0;i<tags;++i){const auto at=132+12*i;const std::string name(icc.begin()+at,icc.begin()+at+4);
        if(name=="rTRC"||name=="gTRC"||name=="bTRC") {
            const auto offset=valeo::read_be32(icc,at+4);require(valeo::read_be32(icc,at+8)>=14,"TRC fixture storage");
            be32(at+8,14);be32(offset,0x63757276);be32(offset+4,0);be32(offset+8,1);icc[offset+12]=1;icc[offset+13]=205;
        }
    }
    std::fill(icc.begin()+84,icc.begin()+100,0);
    valeo::CapturedPair pair;pair.stem=(base/"async").string();pair.began=std::chrono::steady_clock::now();
    pair.light.resize(valeo::reference_frame_bytes);pair.dark.resize(valeo::reference_frame_bytes);
    for(std::size_t i=0;i<pair.light.size();i+=2){const auto value=1000+(i/2)%3000;pair.light[i]=std::uint8_t(value>>8);pair.light[i+1]=std::uint8_t(value&255);pair.dark[i+1]=100;}
    auto& m=pair.metadata;m=valeo::synthetic_metadata();m.width=4008;m.height=2672;m.cfa={2,1,1,0};m.active_area={0,0,2672,4008};
    m.white_level=16383;m.iso=200;m.baseline_exposure_ev=3;m.unique_camera_model="ValeoCapture Synthetic Pair";
    // Independent serial invocation provides an exact-byte rendering oracle for
    // moving the immutable pair/metadata/profile snapshot to another thread.
    const auto mosaic=valeo::subtract_valeo11_pair(pair.light,pair.dark);
    valeo::write_dng(base/"serial.dng",m,mosaic);
    const auto rgb=valeo::demosaic_camera_rgb(mosaic,m,valeo::rgb_profile_gamma(icc));
    const auto rendered=valeo::render_profiled_rgb(m.width,m.height,rgb,icc);
    valeo::write_rgb_tiff(base/"serial.tiff",m.width,m.height,m.orientation,rendered.pixels,rendered.output_icc);
    valeo::CaptureExporter worker(icc,true);worker.submit(std::move(pair));
    bool bounded=false;try{worker.submit({});}catch(const valeo::Error&){bounded=true;}require(bounded,"unbounded export queue accepted");
    const auto result=worker.take();require(!worker.busy(),"taken job retained");
    require(read(result.dng)==read(base/"serial.dng"),"threaded DNG bytes/metadata changed");
    require(read(result.tiff)==read(base/"serial.tiff"),"threaded TIFF pixels/profile changed");
    require(valeo::has_provenance(result.dng) && valeo::has_provenance(result.tiff),"exported DNG/TIFF lack the provenance mark");
    require(!read(base/"async.timing.json").empty(),"timing sidecar absent");
    valeo::CapturedPair invalid;invalid.stem=(base/"invalid").string();worker.submit(std::move(invalid));
    bool failed=false;try{worker.take();}catch(const valeo::Error&){failed=true;}require(failed&&!worker.busy(),"worker exception lost or job retained");
    std::filesystem::remove_all(base);
    std::cout<<"PASS bounded asynchronous export: full synthetic pair, exact serial DNG/TIFF bytes, frozen gain/profile, error propagation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
